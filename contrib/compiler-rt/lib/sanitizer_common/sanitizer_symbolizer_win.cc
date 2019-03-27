//===-- sanitizer_symbolizer_win.cc ---------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is shared between AddressSanitizer and ThreadSanitizer
// run-time libraries.
// Windows-specific implementation of symbolizer parts.
//===----------------------------------------------------------------------===//

#include "sanitizer_platform.h"
#if SANITIZER_WINDOWS

#include "sanitizer_dbghelp.h"
#include "sanitizer_symbolizer_internal.h"

namespace __sanitizer {

decltype(::StackWalk64) *StackWalk64;
decltype(::SymCleanup) *SymCleanup;
decltype(::SymFromAddr) *SymFromAddr;
decltype(::SymFunctionTableAccess64) *SymFunctionTableAccess64;
decltype(::SymGetLineFromAddr64) *SymGetLineFromAddr64;
decltype(::SymGetModuleBase64) *SymGetModuleBase64;
decltype(::SymGetSearchPathW) *SymGetSearchPathW;
decltype(::SymInitialize) *SymInitialize;
decltype(::SymSetOptions) *SymSetOptions;
decltype(::SymSetSearchPathW) *SymSetSearchPathW;
decltype(::UnDecorateSymbolName) *UnDecorateSymbolName;

namespace {

class WinSymbolizerTool : public SymbolizerTool {
 public:
  bool SymbolizePC(uptr addr, SymbolizedStack *stack) override;
  bool SymbolizeData(uptr addr, DataInfo *info) override {
    return false;
  }
  const char *Demangle(const char *name) override;
};

bool is_dbghelp_initialized = false;

bool TrySymInitialize() {
  SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_LOAD_LINES);
  return SymInitialize(GetCurrentProcess(), 0, TRUE);
  // FIXME: We don't call SymCleanup() on exit yet - should we?
}

}  // namespace

// Initializes DbgHelp library, if it's not yet initialized. Calls to this
// function should be synchronized with respect to other calls to DbgHelp API
// (e.g. from WinSymbolizerTool).
void InitializeDbgHelpIfNeeded() {
  if (is_dbghelp_initialized)
    return;

  HMODULE dbghelp = LoadLibraryA("dbghelp.dll");
  CHECK(dbghelp && "failed to load dbghelp.dll");

#define DBGHELP_IMPORT(name)                                                  \
  do {                                                                        \
    name =                                                                    \
        reinterpret_cast<decltype(::name) *>(GetProcAddress(dbghelp, #name)); \
    CHECK(name != nullptr);                                                   \
  } while (0)
  DBGHELP_IMPORT(StackWalk64);
  DBGHELP_IMPORT(SymCleanup);
  DBGHELP_IMPORT(SymFromAddr);
  DBGHELP_IMPORT(SymFunctionTableAccess64);
  DBGHELP_IMPORT(SymGetLineFromAddr64);
  DBGHELP_IMPORT(SymGetModuleBase64);
  DBGHELP_IMPORT(SymGetSearchPathW);
  DBGHELP_IMPORT(SymInitialize);
  DBGHELP_IMPORT(SymSetOptions);
  DBGHELP_IMPORT(SymSetSearchPathW);
  DBGHELP_IMPORT(UnDecorateSymbolName);
#undef DBGHELP_IMPORT

  if (!TrySymInitialize()) {
    // OK, maybe the client app has called SymInitialize already.
    // That's a bit unfortunate for us as all the DbgHelp functions are
    // single-threaded and we can't coordinate with the app.
    // FIXME: Can we stop the other threads at this point?
    // Anyways, we have to reconfigure stuff to make sure that SymInitialize
    // has all the appropriate options set.
    // Cross our fingers and reinitialize DbgHelp.
    Report("*** WARNING: Failed to initialize DbgHelp!              ***\n");
    Report("*** Most likely this means that the app is already      ***\n");
    Report("*** using DbgHelp, possibly with incompatible flags.    ***\n");
    Report("*** Due to technical reasons, symbolization might crash ***\n");
    Report("*** or produce wrong results.                           ***\n");
    SymCleanup(GetCurrentProcess());
    TrySymInitialize();
  }
  is_dbghelp_initialized = true;

  // When an executable is run from a location different from the one where it
  // was originally built, we may not see the nearby PDB files.
  // To work around this, let's append the directory of the main module
  // to the symbol search path.  All the failures below are not fatal.
  const size_t kSymPathSize = 2048;
  static wchar_t path_buffer[kSymPathSize + 1 + MAX_PATH];
  if (!SymGetSearchPathW(GetCurrentProcess(), path_buffer, kSymPathSize)) {
    Report("*** WARNING: Failed to SymGetSearchPathW ***\n");
    return;
  }
  size_t sz = wcslen(path_buffer);
  if (sz) {
    CHECK_EQ(0, wcscat_s(path_buffer, L";"));
    sz++;
  }
  DWORD res = GetModuleFileNameW(NULL, path_buffer + sz, MAX_PATH);
  if (res == 0 || res == MAX_PATH) {
    Report("*** WARNING: Failed to getting the EXE directory ***\n");
    return;
  }
  // Write the zero character in place of the last backslash to get the
  // directory of the main module at the end of path_buffer.
  wchar_t *last_bslash = wcsrchr(path_buffer + sz, L'\\');
  CHECK_NE(last_bslash, 0);
  *last_bslash = L'\0';
  if (!SymSetSearchPathW(GetCurrentProcess(), path_buffer)) {
    Report("*** WARNING: Failed to SymSetSearchPathW\n");
    return;
  }
}

bool WinSymbolizerTool::SymbolizePC(uptr addr, SymbolizedStack *frame) {
  InitializeDbgHelpIfNeeded();

  // See http://msdn.microsoft.com/en-us/library/ms680578(VS.85).aspx
  char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(CHAR)];
  PSYMBOL_INFO symbol = (PSYMBOL_INFO)buffer;
  symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
  symbol->MaxNameLen = MAX_SYM_NAME;
  DWORD64 offset = 0;
  BOOL got_objname = SymFromAddr(GetCurrentProcess(),
                                 (DWORD64)addr, &offset, symbol);
  if (!got_objname)
    return false;

  DWORD unused;
  IMAGEHLP_LINE64 line_info;
  line_info.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
  BOOL got_fileline = SymGetLineFromAddr64(GetCurrentProcess(), (DWORD64)addr,
                                           &unused, &line_info);
  frame->info.function = internal_strdup(symbol->Name);
  frame->info.function_offset = (uptr)offset;
  if (got_fileline) {
    frame->info.file = internal_strdup(line_info.FileName);
    frame->info.line = line_info.LineNumber;
  }
  // Only consider this a successful symbolization attempt if we got file info.
  // Otherwise, try llvm-symbolizer.
  return got_fileline;
}

const char *WinSymbolizerTool::Demangle(const char *name) {
  CHECK(is_dbghelp_initialized);
  static char demangle_buffer[1000];
  if (name[0] == '\01' &&
      UnDecorateSymbolName(name + 1, demangle_buffer, sizeof(demangle_buffer),
                           UNDNAME_NAME_ONLY))
    return demangle_buffer;
  else
    return name;
}

const char *Symbolizer::PlatformDemangle(const char *name) {
  return name;
}

namespace {
struct ScopedHandle {
  ScopedHandle() : h_(nullptr) {}
  explicit ScopedHandle(HANDLE h) : h_(h) {}
  ~ScopedHandle() {
    if (h_)
      ::CloseHandle(h_);
  }
  HANDLE get() { return h_; }
  HANDLE *receive() { return &h_; }
  HANDLE release() {
    HANDLE h = h_;
    h_ = nullptr;
    return h;
  }
  HANDLE h_;
};
} // namespace

bool SymbolizerProcess::StartSymbolizerSubprocess() {
  // Create inherited pipes for stdin and stdout.
  ScopedHandle stdin_read, stdin_write;
  ScopedHandle stdout_read, stdout_write;
  SECURITY_ATTRIBUTES attrs;
  attrs.nLength = sizeof(SECURITY_ATTRIBUTES);
  attrs.bInheritHandle = TRUE;
  attrs.lpSecurityDescriptor = nullptr;
  if (!::CreatePipe(stdin_read.receive(), stdin_write.receive(), &attrs, 0) ||
      !::CreatePipe(stdout_read.receive(), stdout_write.receive(), &attrs, 0)) {
    VReport(2, "WARNING: %s CreatePipe failed (error code: %d)\n",
            SanitizerToolName, path_, GetLastError());
    return false;
  }

  // Don't inherit the writing end of stdin or the reading end of stdout.
  if (!SetHandleInformation(stdin_write.get(), HANDLE_FLAG_INHERIT, 0) ||
      !SetHandleInformation(stdout_read.get(), HANDLE_FLAG_INHERIT, 0)) {
    VReport(2, "WARNING: %s SetHandleInformation failed (error code: %d)\n",
            SanitizerToolName, path_, GetLastError());
    return false;
  }

  // Compute the command line. Wrap double quotes around everything.
  const char *argv[kArgVMax];
  GetArgV(path_, argv);
  InternalScopedString command_line(kMaxPathLength * 3);
  for (int i = 0; argv[i]; i++) {
    const char *arg = argv[i];
    int arglen = internal_strlen(arg);
    // Check that tool command lines are simple and that complete escaping is
    // unnecessary.
    CHECK(!internal_strchr(arg, '"') && "quotes in args unsupported");
    CHECK(!internal_strstr(arg, "\\\\") &&
          "double backslashes in args unsupported");
    CHECK(arglen > 0 && arg[arglen - 1] != '\\' &&
          "args ending in backslash and empty args unsupported");
    command_line.append("\"%s\" ", arg);
  }
  VReport(3, "Launching symbolizer command: %s\n", command_line.data());

  // Launch llvm-symbolizer with stdin and stdout redirected.
  STARTUPINFOA si;
  memset(&si, 0, sizeof(si));
  si.cb = sizeof(si);
  si.dwFlags |= STARTF_USESTDHANDLES;
  si.hStdInput = stdin_read.get();
  si.hStdOutput = stdout_write.get();
  PROCESS_INFORMATION pi;
  memset(&pi, 0, sizeof(pi));
  if (!CreateProcessA(path_,               // Executable
                      command_line.data(), // Command line
                      nullptr,             // Process handle not inheritable
                      nullptr,             // Thread handle not inheritable
                      TRUE,                // Set handle inheritance to TRUE
                      0,                   // Creation flags
                      nullptr,             // Use parent's environment block
                      nullptr,             // Use parent's starting directory
                      &si, &pi)) {
    VReport(2, "WARNING: %s failed to create process for %s (error code: %d)\n",
            SanitizerToolName, path_, GetLastError());
    return false;
  }

  // Process creation succeeded, so transfer handle ownership into the fields.
  input_fd_ = stdout_read.release();
  output_fd_ = stdin_write.release();

  // The llvm-symbolizer process is responsible for quitting itself when the
  // stdin pipe is closed, so we don't need these handles. Close them to prevent
  // leaks. If we ever want to try to kill the symbolizer process from the
  // parent, we'll want to hang on to these handles.
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  return true;
}

static void ChooseSymbolizerTools(IntrusiveList<SymbolizerTool> *list,
                                  LowLevelAllocator *allocator) {
  if (!common_flags()->symbolize) {
    VReport(2, "Symbolizer is disabled.\n");
    return;
  }

  // Add llvm-symbolizer in case the binary has dwarf.
  const char *user_path = common_flags()->external_symbolizer_path;
  const char *path =
      user_path ? user_path : FindPathToBinary("llvm-symbolizer.exe");
  if (path) {
    VReport(2, "Using llvm-symbolizer at %spath: %s\n",
            user_path ? "user-specified " : "", path);
    list->push_back(new(*allocator) LLVMSymbolizer(path, allocator));
  } else {
    if (user_path && user_path[0] == '\0') {
      VReport(2, "External symbolizer is explicitly disabled.\n");
    } else {
      VReport(2, "External symbolizer is not present.\n");
    }
  }

  // Add the dbghelp based symbolizer.
  list->push_back(new(*allocator) WinSymbolizerTool());
}

Symbolizer *Symbolizer::PlatformInit() {
  IntrusiveList<SymbolizerTool> list;
  list.clear();
  ChooseSymbolizerTools(&list, &symbolizer_allocator_);

  return new(symbolizer_allocator_) Symbolizer(list);
}

void Symbolizer::LateInitialize() {
  Symbolizer::GetOrInit();
}

}  // namespace __sanitizer

#endif  // _WIN32
