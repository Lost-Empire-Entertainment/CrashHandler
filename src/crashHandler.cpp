﻿//Copyright(C) 2025 Lost Empire Entertainment
//This program comes with ABSOLUTELY NO WARRANTY.
//This is free software, and you are welcome to redistribute it under certain conditions.
//Read LICENSE.md for more information.

#include <iostream>
#include <algorithm>
#include <ctime>
#include <iomanip>
#ifdef _WIN32
#include <Windows.h>
#include <dbghelp.h>
#include <shlwapi.h>
#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "Shlwapi.lib")
#endif

//crash handler
#include "crashHandler.hpp"
#include "errorPopup.hpp"

using std::wstring;
using std::to_string;
using std::cout;
using std::replace;
using std::setfill;
using std::setw;
using std::time;
using std::tm;
#ifdef _WIN32
using std::wstring;
using std::hex;
using std::dec;
#endif

namespace ElypsoUtils
{
#ifdef _WIN32
	LONG WINAPI CrashHandler::HandleCrash(EXCEPTION_POINTERS* info)
	{
		DWORD code = info->ExceptionRecord->ExceptionCode;

		ostringstream oss;
		oss << "Crash detected!\n\n";

		oss << "Exception code: " << hex << code << "\n";
		oss << "Address: 0x" << hex << (uintptr_t)info->ExceptionRecord->ExceptionAddress << "\n\n";

		switch (code)
		{
		case EXCEPTION_ACCESS_VIOLATION:
			oss << "Reason: Access violation (nullptr or invalid memory access)\n";
			break;
		case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
			oss << "Reason: Array index out of bounds\n";
			break;
		case EXCEPTION_INT_DIVIDE_BY_ZERO:
			oss << "Reason: Integer divide by zero\n";
			break;
		case EXCEPTION_INT_OVERFLOW:
			oss << "Reason: Integer overflow\n";
			break;
		case EXCEPTION_STACK_OVERFLOW:
			oss << "Reason: Stack overflow (likely due to infinite recursion)\n";
			break;
		case EXCEPTION_ILLEGAL_INSTRUCTION:
			oss << "Reason: Invalid CPU instruction (corrupted memory or jump to bad code)\n";
			break;
		case EXCEPTION_DATATYPE_MISALIGNMENT:
			oss << "Reason: Misaligned data access (bad struct pointer or cast)\n";
			break;
		case EXCEPTION_IN_PAGE_ERROR:
			oss << "Reason: Memory access failed (missing or swapped-out memory page)\n";
			break;
		case EXCEPTION_FLT_DIVIDE_BY_ZERO:
			oss << "Reason: Floating-point divide by zero\n";
			break;
		case EXCEPTION_FLT_INVALID_OPERATION:
			oss << "Reason: Invalid floating-point operation (NaN, corrupted float math)\n";
			break;
		default:
			oss << "Reason: Unknown exception\n";
			break;
		}

		AppendCallStackToStream(oss, info->ContextRecord);

		string dumpFileName = WriteMiniDump(info);
		oss << "A dump file '" << dumpFileName << "' was created to exe root folder.";

		ErrorPopup::CreateErrorPopup(oss.str());

		return EXCEPTION_EXECUTE_HANDLER;
	}

	string CrashHandler::WriteMiniDump(EXCEPTION_POINTERS* info)
	{
		string timeStamp = "crash_" + GetCurrentTimeStamp();

		//replace characters not allowed in filenames
		replace(timeStamp.begin(), timeStamp.end(), ':', '-');
		timeStamp.erase(remove(timeStamp.begin(), timeStamp.end(), ']'), timeStamp.end());
		timeStamp.erase(remove(timeStamp.begin(), timeStamp.end(), '['), timeStamp.end());
		timeStamp.erase(remove(timeStamp.begin(), timeStamp.end(), ' '), timeStamp.end());
		timeStamp += ".dmp";

		//get executable directory
		WCHAR exePath[MAX_PATH];
		GetModuleFileNameW(nullptr, exePath, MAX_PATH);
		PathRemoveFileSpecW(exePath);

		//build full path to dump file
		wstring wideFilename = exePath;
		wideFilename += L"\\" + wstring(timeStamp.begin(), timeStamp.end());

		HANDLE hFile = CreateFileW(
			wideFilename.c_str(),
			GENERIC_WRITE,
			0,
			nullptr,
			CREATE_ALWAYS,
			FILE_ATTRIBUTE_NORMAL,
			nullptr);

		if (hFile != INVALID_HANDLE_VALUE)
		{
			MINIDUMP_EXCEPTION_INFORMATION dumpInfo{};

			DWORD mdThreadID = GetThreadId(GetCurrentThread());
			dumpInfo.ThreadId = mdThreadID;
			cout << "minidump thread: " << mdThreadID << "\n";

			dumpInfo.ExceptionPointers = info;
			dumpInfo.ClientPointers = FALSE;

			MINIDUMP_TYPE dumpFlags = static_cast<MINIDUMP_TYPE>(
#ifdef _DEBUG
				//full stack + thread context
				MiniDumpWithThreadInfo
				//global/static vars
				| MiniDumpWithDataSegs
				//heap allocations
				| MiniDumpWithPrivateReadWriteMemory
				//debugging leaks or resource state
				| MiniDumpWithHandleData
#else
				//full stack + thread context
				MiniDumpWithThreadInfo
				//global/static vars
				| MiniDumpWithDataSegs
				//heap/local memory relevant to the current crash context
				| MiniDumpWithIndirectlyReferencedMemory
#endif
				);

			MiniDumpWriteDump(
				GetCurrentProcess(),
				GetCurrentProcessId(),
				hFile,
				dumpFlags,
				&dumpInfo,
				nullptr,
				nullptr);

			CloseHandle(hFile);
		}

		return timeStamp;
	}

	string CrashHandler::GetCurrentTimeStamp()
	{
		time_t now = time(nullptr);
		tm localTime{};
		localtime_s(&localTime, &now);

		ostringstream oss;
		oss << setfill('0')
			<< localTime.tm_mday << "_"
			<< setw(2) << localTime.tm_hour << "_"
			<< setw(2) << localTime.tm_min << "_"
			<< setw(2) << localTime.tm_sec;

		return oss.str();
	}

	void CrashHandler::AppendCallStackToStream(ostringstream& oss, CONTEXT* context)
	{
		HANDLE process = GetCurrentProcess();

		HANDLE thread = GetCurrentThread();
		DWORD mdThreadID = GetThreadId(thread);
		cout << "stackwalk thread: " << mdThreadID << "\n";

		SymSetOptions(
			SYMOPT_LOAD_LINES         //file/line info
			| SYMOPT_UNDNAME          //demangle c++ symbols
			| SYMOPT_DEFERRED_LOADS); //don't load all symbols immediately (faster)
		SymInitialize(process, nullptr, TRUE);

		STACKFRAME64 stack = {};
#if defined(_M_X64)
		DWORD machineType = IMAGE_FILE_MACHINE_AMD64;
		stack.AddrPC.Offset = context->Rip;
		stack.AddrFrame.Offset = context->Rbp;
		stack.AddrStack.Offset = context->Rsp;
#else
		DWORD machineType = IMAGE_FILE_MACHINE_I386;
		stack.AddrPC.Offset = context->Eip;
		stack.AddrFrame.Offset = context->Ebp;
		stack.AddrStack.Offset = context->Esp;
#endif

		stack.AddrPC.Mode = AddrModeFlat;
		stack.AddrFrame.Mode = AddrModeFlat;
		stack.AddrStack.Mode = AddrModeFlat;

		oss << "\n========================================\n\n";
		oss << "Call stack:\n";

		for (int i = 0; i < 10; ++i)
		{
			if (!StackWalk64(
				machineType,
				process,
				thread,
				&stack,
				context,
				nullptr,
				SymFunctionTableAccess64,
				SymGetModuleBase64,
				nullptr))
			{
				break;
			}

			DWORD64 addr = stack.AddrPC.Offset;
			if (addr == 0) break;

			char symbolBuffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME]{};
			SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolBuffer);
			symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
			symbol->MaxNameLen = MAX_SYM_NAME;

			oss << "  " << i << ": ";
			DWORD64 displacement = 0;
			if (SymFromAddr(
				process,
				addr,
				&displacement,
				symbol))
			{
				char demangled[1024];
				if (UnDecorateSymbolName(
					symbol->Name,
					demangled,
					sizeof(demangled),
					UNDNAME_COMPLETE))
				{
					oss << demangled;
				}
				else
				{
					oss << symbol->Name;
				}
			}
			else
			{
				oss << "(symbol not found)\n";
			}

			//file and line info
			IMAGEHLP_LINE64 lineInfo;
			DWORD lineDisplacement = 0;
			ZeroMemory(&lineInfo, sizeof(lineInfo));
			lineInfo.SizeOfStruct = sizeof(lineInfo);

			if (SymGetLineFromAddr64(
				process,
				addr,
				&lineDisplacement,
				&lineInfo))
			{
				string fullPath = lineInfo.FileName;
				size_t lastSlash = fullPath.find_last_of("\\/");
				size_t secondLastSlash = fullPath.find_last_of("\\/", lastSlash - 1);

				string shortPath = (secondLastSlash != string::npos)
					? fullPath.substr(secondLastSlash + 1)
					: fullPath;

				oss << "\n        script: " << shortPath;
				oss << "\n        line: " << dec << lineInfo.LineNumber;
			}

			oss << " [0x" << hex << addr << "]\n";
		}

		SymCleanup(process);

		oss << "\n========================================\n\n";
	}
#endif
}