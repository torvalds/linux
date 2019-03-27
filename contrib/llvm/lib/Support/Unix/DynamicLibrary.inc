//===- Unix/DynamicLibrary.cpp - Unix DL Implementation ---------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file provides the UNIX specific implementation of DynamicLibrary.
//
//===----------------------------------------------------------------------===//

#if defined(HAVE_DLFCN_H) && defined(HAVE_DLOPEN)
#include <dlfcn.h>

DynamicLibrary::HandleSet::~HandleSet() {
  // Close the libraries in reverse order.
  for (void *Handle : llvm::reverse(Handles))
    ::dlclose(Handle);
  if (Process)
    ::dlclose(Process);

  // llvm_shutdown called, Return to default
  DynamicLibrary::SearchOrder = DynamicLibrary::SO_Linker;
}

void *DynamicLibrary::HandleSet::DLOpen(const char *File, std::string *Err) {
  void *Handle = ::dlopen(File, RTLD_LAZY|RTLD_GLOBAL);
  if (!Handle) {
    if (Err) *Err = ::dlerror();
    return &DynamicLibrary::Invalid;
  }

#ifdef __CYGWIN__
  // Cygwin searches symbols only in the main
  // with the handle of dlopen(NULL, RTLD_GLOBAL).
  if (!File)
    Handle = RTLD_DEFAULT;
#endif

  return Handle;
}

void DynamicLibrary::HandleSet::DLClose(void *Handle) {
  ::dlclose(Handle);
}

void *DynamicLibrary::HandleSet::DLSym(void *Handle, const char *Symbol) {
  return ::dlsym(Handle, Symbol);
}

#else // !HAVE_DLOPEN

DynamicLibrary::HandleSet::~HandleSet() {}

void *DynamicLibrary::HandleSet::DLOpen(const char *File, std::string *Err) {
  if (Err) *Err = "dlopen() not supported on this platform";
  return &Invalid;
}

void DynamicLibrary::HandleSet::DLClose(void *Handle) {
}

void *DynamicLibrary::HandleSet::DLSym(void *Handle, const char *Symbol) {
  return nullptr;
}

#endif

// Must declare the symbols in the global namespace.
static void *DoSearch(const char* SymbolName) {
#define EXPLICIT_SYMBOL(SYM) \
   extern void *SYM; if (!strcmp(SymbolName, #SYM)) return (void*)&SYM

  // If this is darwin, it has some funky issues, try to solve them here.  Some
  // important symbols are marked 'private external' which doesn't allow
  // SearchForAddressOfSymbol to find them.  As such, we special case them here,
  // there is only a small handful of them.

#ifdef __APPLE__
  {
    // __eprintf is sometimes used for assert() handling on x86.
    //
    // FIXME: Currently disabled when using Clang, as we don't always have our
    // runtime support libraries available.
#ifndef __clang__
#ifdef __i386__
    EXPLICIT_SYMBOL(__eprintf);
#endif
#endif
  }
#endif

#ifdef __CYGWIN__
  {
    EXPLICIT_SYMBOL(_alloca);
    EXPLICIT_SYMBOL(__main);
  }
#endif

#undef EXPLICIT_SYMBOL

// This macro returns the address of a well-known, explicit symbol
#define EXPLICIT_SYMBOL(SYM) \
   if (!strcmp(SymbolName, #SYM)) return &SYM

// Under glibc we have a weird situation. The stderr/out/in symbols are both
// macros and global variables because of standards requirements. So, we
// boldly use the EXPLICIT_SYMBOL macro without checking for a #define first.
#if defined(__GLIBC__)
  {
    EXPLICIT_SYMBOL(stderr);
    EXPLICIT_SYMBOL(stdout);
    EXPLICIT_SYMBOL(stdin);
  }
#else
  // For everything else, we want to check to make sure the symbol isn't defined
  // as a macro before using EXPLICIT_SYMBOL.
  {
#ifndef stdin
    EXPLICIT_SYMBOL(stdin);
#endif
#ifndef stdout
    EXPLICIT_SYMBOL(stdout);
#endif
#ifndef stderr
    EXPLICIT_SYMBOL(stderr);
#endif
  }
#endif
#undef EXPLICIT_SYMBOL

  return nullptr;
}
