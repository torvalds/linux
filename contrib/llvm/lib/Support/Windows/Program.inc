//===- Win32/Program.cpp - Win32 Program Implementation ------- -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file provides the Win32 specific implementation of the Program class.
//
//===----------------------------------------------------------------------===//

#include "WindowsSupport.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/WindowsError.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdio>
#include <fcntl.h>
#include <io.h>
#include <malloc.h>
#include <numeric>

//===----------------------------------------------------------------------===//
//=== WARNING: Implementation here must contain only Win32 specific code
//===          and must not be UNIX code
//===----------------------------------------------------------------------===//

namespace llvm {

ProcessInfo::ProcessInfo() : Pid(0), Process(0), ReturnCode(0) {}

ErrorOr<std::string> sys::findProgramByName(StringRef Name,
                                            ArrayRef<StringRef> Paths) {
  assert(!Name.empty() && "Must have a name!");

  if (Name.find_first_of("/\\") != StringRef::npos)
    return std::string(Name);

  const wchar_t *Path = nullptr;
  std::wstring PathStorage;
  if (!Paths.empty()) {
    PathStorage.reserve(Paths.size() * MAX_PATH);
    for (unsigned i = 0; i < Paths.size(); ++i) {
      if (i)
        PathStorage.push_back(L';');
      StringRef P = Paths[i];
      SmallVector<wchar_t, MAX_PATH> TmpPath;
      if (std::error_code EC = windows::UTF8ToUTF16(P, TmpPath))
        return EC;
      PathStorage.append(TmpPath.begin(), TmpPath.end());
    }
    Path = PathStorage.c_str();
  }

  SmallVector<wchar_t, MAX_PATH> U16Name;
  if (std::error_code EC = windows::UTF8ToUTF16(Name, U16Name))
    return EC;

  SmallVector<StringRef, 12> PathExts;
  PathExts.push_back("");
  PathExts.push_back(".exe"); // FIXME: This must be in %PATHEXT%.
  if (const char *PathExtEnv = std::getenv("PATHEXT"))
    SplitString(PathExtEnv, PathExts, ";");

  SmallVector<wchar_t, MAX_PATH> U16Result;
  DWORD Len = MAX_PATH;
  for (StringRef Ext : PathExts) {
    SmallVector<wchar_t, MAX_PATH> U16Ext;
    if (std::error_code EC = windows::UTF8ToUTF16(Ext, U16Ext))
      return EC;

    do {
      U16Result.reserve(Len);
      // Lets attach the extension manually. That is needed for files
      // with a point in name like aaa.bbb. SearchPathW will not add extension
      // from its argument to such files because it thinks they already had one.
      SmallVector<wchar_t, MAX_PATH> U16NameExt;
      if (std::error_code EC =
              windows::UTF8ToUTF16(Twine(Name + Ext).str(), U16NameExt))
        return EC;

      Len = ::SearchPathW(Path, c_str(U16NameExt), nullptr,
                          U16Result.capacity(), U16Result.data(), nullptr);
    } while (Len > U16Result.capacity());

    if (Len != 0)
      break; // Found it.
  }

  if (Len == 0)
    return mapWindowsError(::GetLastError());

  U16Result.set_size(Len);

  SmallVector<char, MAX_PATH> U8Result;
  if (std::error_code EC =
          windows::UTF16ToUTF8(U16Result.data(), U16Result.size(), U8Result))
    return EC;

  return std::string(U8Result.begin(), U8Result.end());
}

bool MakeErrMsg(std::string *ErrMsg, const std::string &prefix) {
  if (!ErrMsg)
    return true;
  char *buffer = NULL;
  DWORD LastError = GetLastError();
  DWORD R = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                               FORMAT_MESSAGE_FROM_SYSTEM |
                               FORMAT_MESSAGE_MAX_WIDTH_MASK,
                           NULL, LastError, 0, (LPSTR)&buffer, 1, NULL);
  if (R)
    *ErrMsg = prefix + ": " + buffer;
  else
    *ErrMsg = prefix + ": Unknown error";
  *ErrMsg += " (0x" + llvm::utohexstr(LastError) + ")";

  LocalFree(buffer);
  return R != 0;
}

static HANDLE RedirectIO(Optional<StringRef> Path, int fd,
                         std::string *ErrMsg) {
  HANDLE h;
  if (!Path) {
    if (!DuplicateHandle(GetCurrentProcess(), (HANDLE)_get_osfhandle(fd),
                         GetCurrentProcess(), &h,
                         0, TRUE, DUPLICATE_SAME_ACCESS))
      return INVALID_HANDLE_VALUE;
    return h;
  }

  std::string fname;
  if (Path->empty())
    fname = "NUL";
  else
    fname = *Path;

  SECURITY_ATTRIBUTES sa;
  sa.nLength = sizeof(sa);
  sa.lpSecurityDescriptor = 0;
  sa.bInheritHandle = TRUE;

  SmallVector<wchar_t, 128> fnameUnicode;
  if (Path->empty()) {
    // Don't play long-path tricks on "NUL".
    if (windows::UTF8ToUTF16(fname, fnameUnicode))
      return INVALID_HANDLE_VALUE;
  } else {
    if (path::widenPath(fname, fnameUnicode))
      return INVALID_HANDLE_VALUE;
  }
  h = CreateFileW(fnameUnicode.data(), fd ? GENERIC_WRITE : GENERIC_READ,
                  FILE_SHARE_READ, &sa, fd == 0 ? OPEN_EXISTING : CREATE_ALWAYS,
                  FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) {
    MakeErrMsg(ErrMsg, fname + ": Can't open file for " +
        (fd ? "input" : "output"));
  }

  return h;
}

}

static bool Execute(ProcessInfo &PI, StringRef Program,
                    ArrayRef<StringRef> Args, Optional<ArrayRef<StringRef>> Env,
                    ArrayRef<Optional<StringRef>> Redirects,
                    unsigned MemoryLimit, std::string *ErrMsg) {
  if (!sys::fs::can_execute(Program)) {
    if (ErrMsg)
      *ErrMsg = "program not executable";
    return false;
  }

  // can_execute may succeed by looking at Program + ".exe". CreateProcessW
  // will implicitly add the .exe if we provide a command line without an
  // executable path, but since we use an explicit executable, we have to add
  // ".exe" ourselves.
  SmallString<64> ProgramStorage;
  if (!sys::fs::exists(Program))
    Program = Twine(Program + ".exe").toStringRef(ProgramStorage);

  // Windows wants a command line, not an array of args, to pass to the new
  // process.  We have to concatenate them all, while quoting the args that
  // have embedded spaces (or are empty).
  std::string Command = flattenWindowsCommandLine(Args);

  // The pointer to the environment block for the new process.
  std::vector<wchar_t> EnvBlock;

  if (Env) {
    // An environment block consists of a null-terminated block of
    // null-terminated strings. Convert the array of environment variables to
    // an environment block by concatenating them.
    for (const auto E : *Env) {
      SmallVector<wchar_t, MAX_PATH> EnvString;
      if (std::error_code ec = windows::UTF8ToUTF16(E, EnvString)) {
        SetLastError(ec.value());
        MakeErrMsg(ErrMsg, "Unable to convert environment variable to UTF-16");
        return false;
      }

      EnvBlock.insert(EnvBlock.end(), EnvString.begin(), EnvString.end());
      EnvBlock.push_back(0);
    }
    EnvBlock.push_back(0);
  }

  // Create a child process.
  STARTUPINFOW si;
  memset(&si, 0, sizeof(si));
  si.cb = sizeof(si);
  si.hStdInput = INVALID_HANDLE_VALUE;
  si.hStdOutput = INVALID_HANDLE_VALUE;
  si.hStdError = INVALID_HANDLE_VALUE;

  if (!Redirects.empty()) {
    si.dwFlags = STARTF_USESTDHANDLES;

    si.hStdInput = RedirectIO(Redirects[0], 0, ErrMsg);
    if (si.hStdInput == INVALID_HANDLE_VALUE) {
      MakeErrMsg(ErrMsg, "can't redirect stdin");
      return false;
    }
    si.hStdOutput = RedirectIO(Redirects[1], 1, ErrMsg);
    if (si.hStdOutput == INVALID_HANDLE_VALUE) {
      CloseHandle(si.hStdInput);
      MakeErrMsg(ErrMsg, "can't redirect stdout");
      return false;
    }
    if (Redirects[1] && Redirects[2] && *Redirects[1] == *Redirects[2]) {
      // If stdout and stderr should go to the same place, redirect stderr
      // to the handle already open for stdout.
      if (!DuplicateHandle(GetCurrentProcess(), si.hStdOutput,
                           GetCurrentProcess(), &si.hStdError,
                           0, TRUE, DUPLICATE_SAME_ACCESS)) {
        CloseHandle(si.hStdInput);
        CloseHandle(si.hStdOutput);
        MakeErrMsg(ErrMsg, "can't dup stderr to stdout");
        return false;
      }
    } else {
      // Just redirect stderr
      si.hStdError = RedirectIO(Redirects[2], 2, ErrMsg);
      if (si.hStdError == INVALID_HANDLE_VALUE) {
        CloseHandle(si.hStdInput);
        CloseHandle(si.hStdOutput);
        MakeErrMsg(ErrMsg, "can't redirect stderr");
        return false;
      }
    }
  }

  PROCESS_INFORMATION pi;
  memset(&pi, 0, sizeof(pi));

  fflush(stdout);
  fflush(stderr);

  SmallVector<wchar_t, MAX_PATH> ProgramUtf16;
  if (std::error_code ec = path::widenPath(Program, ProgramUtf16)) {
    SetLastError(ec.value());
    MakeErrMsg(ErrMsg,
               std::string("Unable to convert application name to UTF-16"));
    return false;
  }

  SmallVector<wchar_t, MAX_PATH> CommandUtf16;
  if (std::error_code ec = windows::UTF8ToUTF16(Command, CommandUtf16)) {
    SetLastError(ec.value());
    MakeErrMsg(ErrMsg,
               std::string("Unable to convert command-line to UTF-16"));
    return false;
  }

  BOOL rc = CreateProcessW(ProgramUtf16.data(), CommandUtf16.data(), 0, 0,
                           TRUE, CREATE_UNICODE_ENVIRONMENT,
                           EnvBlock.empty() ? 0 : EnvBlock.data(), 0, &si,
                           &pi);
  DWORD err = GetLastError();

  // Regardless of whether the process got created or not, we are done with
  // the handles we created for it to inherit.
  CloseHandle(si.hStdInput);
  CloseHandle(si.hStdOutput);
  CloseHandle(si.hStdError);

  // Now return an error if the process didn't get created.
  if (!rc) {
    SetLastError(err);
    MakeErrMsg(ErrMsg, std::string("Couldn't execute program '") +
               Program.str() + "'");
    return false;
  }

  PI.Pid = pi.dwProcessId;
  PI.Process = pi.hProcess;

  // Make sure these get closed no matter what.
  ScopedCommonHandle hThread(pi.hThread);

  // Assign the process to a job if a memory limit is defined.
  ScopedJobHandle hJob;
  if (MemoryLimit != 0) {
    hJob = CreateJobObjectW(0, 0);
    bool success = false;
    if (hJob) {
      JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli;
      memset(&jeli, 0, sizeof(jeli));
      jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_PROCESS_MEMORY;
      jeli.ProcessMemoryLimit = uintptr_t(MemoryLimit) * 1048576;
      if (SetInformationJobObject(hJob, JobObjectExtendedLimitInformation,
                                  &jeli, sizeof(jeli))) {
        if (AssignProcessToJobObject(hJob, pi.hProcess))
          success = true;
      }
    }
    if (!success) {
      SetLastError(GetLastError());
      MakeErrMsg(ErrMsg, std::string("Unable to set memory limit"));
      TerminateProcess(pi.hProcess, 1);
      WaitForSingleObject(pi.hProcess, INFINITE);
      return false;
    }
  }

  return true;
}

static bool argNeedsQuotes(StringRef Arg) {
  if (Arg.empty())
    return true;
  return StringRef::npos != Arg.find_first_of("\t \"&\'()*<>\\`^|\n");
}

static std::string quoteSingleArg(StringRef Arg) {
  std::string Result;
  Result.push_back('"');

  while (!Arg.empty()) {
    size_t FirstNonBackslash = Arg.find_first_not_of('\\');
    size_t BackslashCount = FirstNonBackslash;
    if (FirstNonBackslash == StringRef::npos) {
      // The entire remainder of the argument is backslashes.  Escape all of
      // them and just early out.
      BackslashCount = Arg.size();
      Result.append(BackslashCount * 2, '\\');
      break;
    }

    if (Arg[FirstNonBackslash] == '\"') {
      // This is an embedded quote.  Escape all preceding backslashes, then
      // add one additional backslash to escape the quote.
      Result.append(BackslashCount * 2 + 1, '\\');
      Result.push_back('\"');
    } else {
      // This is just a normal character.  Don't escape any of the preceding
      // backslashes, just append them as they are and then append the
      // character.
      Result.append(BackslashCount, '\\');
      Result.push_back(Arg[FirstNonBackslash]);
    }

    // Drop all the backslashes, plus the following character.
    Arg = Arg.drop_front(FirstNonBackslash + 1);
  }

  Result.push_back('"');
  return Result;
}

namespace llvm {
std::string sys::flattenWindowsCommandLine(ArrayRef<StringRef> Args) {
  std::string Command;
  for (StringRef Arg : Args) {
    if (argNeedsQuotes(Arg))
      Command += quoteSingleArg(Arg);
    else
      Command += Arg;

    Command.push_back(' ');
  }

  return Command;
}

ProcessInfo sys::Wait(const ProcessInfo &PI, unsigned SecondsToWait,
                      bool WaitUntilChildTerminates, std::string *ErrMsg) {
  assert(PI.Pid && "invalid pid to wait on, process not started?");
  assert((PI.Process && PI.Process != INVALID_HANDLE_VALUE) &&
         "invalid process handle to wait on, process not started?");
  DWORD milliSecondsToWait = 0;
  if (WaitUntilChildTerminates)
    milliSecondsToWait = INFINITE;
  else if (SecondsToWait > 0)
    milliSecondsToWait = SecondsToWait * 1000;

  ProcessInfo WaitResult = PI;
  DWORD WaitStatus = WaitForSingleObject(PI.Process, milliSecondsToWait);
  if (WaitStatus == WAIT_TIMEOUT) {
    if (SecondsToWait) {
      if (!TerminateProcess(PI.Process, 1)) {
        if (ErrMsg)
          MakeErrMsg(ErrMsg, "Failed to terminate timed-out program");

        // -2 indicates a crash or timeout as opposed to failure to execute.
        WaitResult.ReturnCode = -2;
        CloseHandle(PI.Process);
        return WaitResult;
      }
      WaitForSingleObject(PI.Process, INFINITE);
      CloseHandle(PI.Process);
    } else {
      // Non-blocking wait.
      return ProcessInfo();
    }
  }

  // Get its exit status.
  DWORD status;
  BOOL rc = GetExitCodeProcess(PI.Process, &status);
  DWORD err = GetLastError();
  if (err != ERROR_INVALID_HANDLE)
    CloseHandle(PI.Process);

  if (!rc) {
    SetLastError(err);
    if (ErrMsg)
      MakeErrMsg(ErrMsg, "Failed getting status for program");

    // -2 indicates a crash or timeout as opposed to failure to execute.
    WaitResult.ReturnCode = -2;
    return WaitResult;
  }

  if (!status)
    return WaitResult;

  // Pass 10(Warning) and 11(Error) to the callee as negative value.
  if ((status & 0xBFFF0000U) == 0x80000000U)
    WaitResult.ReturnCode = static_cast<int>(status);
  else if (status & 0xFF)
    WaitResult.ReturnCode = status & 0x7FFFFFFF;
  else
    WaitResult.ReturnCode = 1;

  return WaitResult;
}

std::error_code sys::ChangeStdinToBinary() {
  int result = _setmode(_fileno(stdin), _O_BINARY);
  if (result == -1)
    return std::error_code(errno, std::generic_category());
  return std::error_code();
}

std::error_code sys::ChangeStdoutToBinary() {
  int result = _setmode(_fileno(stdout), _O_BINARY);
  if (result == -1)
    return std::error_code(errno, std::generic_category());
  return std::error_code();
}

std::error_code
llvm::sys::writeFileWithEncoding(StringRef FileName, StringRef Contents,
                                 WindowsEncodingMethod Encoding) {
  std::error_code EC;
  llvm::raw_fd_ostream OS(FileName, EC, llvm::sys::fs::F_Text);
  if (EC)
    return EC;

  if (Encoding == WEM_UTF8) {
    OS << Contents;
  } else if (Encoding == WEM_CurrentCodePage) {
    SmallVector<wchar_t, 1> ArgsUTF16;
    SmallVector<char, 1> ArgsCurCP;

    if ((EC = windows::UTF8ToUTF16(Contents, ArgsUTF16)))
      return EC;

    if ((EC = windows::UTF16ToCurCP(
             ArgsUTF16.data(), ArgsUTF16.size(), ArgsCurCP)))
      return EC;

    OS.write(ArgsCurCP.data(), ArgsCurCP.size());
  } else if (Encoding == WEM_UTF16) {
    SmallVector<wchar_t, 1> ArgsUTF16;

    if ((EC = windows::UTF8ToUTF16(Contents, ArgsUTF16)))
      return EC;

    // Endianness guessing
    char BOM[2];
    uint16_t src = UNI_UTF16_BYTE_ORDER_MARK_NATIVE;
    memcpy(BOM, &src, 2);
    OS.write(BOM, 2);
    OS.write((char *)ArgsUTF16.data(), ArgsUTF16.size() << 1);
  } else {
    llvm_unreachable("Unknown encoding");
  }

  if (OS.has_error())
    return make_error_code(errc::io_error);

  return EC;
}

bool llvm::sys::commandLineFitsWithinSystemLimits(StringRef Program,
                                                  ArrayRef<StringRef> Args) {
  // The documented max length of the command line passed to CreateProcess.
  static const size_t MaxCommandStringLength = 32768;
  SmallVector<StringRef, 8> FullArgs;
  FullArgs.push_back(Program);
  FullArgs.append(Args.begin(), Args.end());
  std::string Result = flattenWindowsCommandLine(FullArgs);
  return (Result.size() + 1) <= MaxCommandStringLength;
}
}
