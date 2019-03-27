//===-- RTDyldMemoryManager.cpp - Memory manager for MC-JIT -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Implementation of the runtime dynamic memory manager base class.
//
//===----------------------------------------------------------------------===//

#include "llvm/Config/config.h"
#include "llvm/ExecutionEngine/RTDyldMemoryManager.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/ErrorHandling.h"
#include <cstdlib>

#ifdef __linux__
  // These includes used by RTDyldMemoryManager::getPointerToNamedFunction()
  // for Glibc trickery. See comments in this function for more information.
  #ifdef HAVE_SYS_STAT_H
    #include <sys/stat.h>
  #endif
  #include <fcntl.h>
  #include <unistd.h>
#endif

namespace llvm {

RTDyldMemoryManager::~RTDyldMemoryManager() {}

// Determine whether we can register EH tables.
#if (defined(__GNUC__) && !defined(__ARM_EABI__) && !defined(__ia64__) && \
     !defined(__SEH__) && !defined(__USING_SJLJ_EXCEPTIONS__))
#define HAVE_EHTABLE_SUPPORT 1
#else
#define HAVE_EHTABLE_SUPPORT 0
#endif

#if HAVE_EHTABLE_SUPPORT
extern "C" void __register_frame(void *);
extern "C" void __deregister_frame(void *);
#else
// The building compiler does not have __(de)register_frame but
// it may be found at runtime in a dynamically-loaded library.
// For example, this happens when building LLVM with Visual C++
// but using the MingW runtime.
void __register_frame(void *p) {
  static bool Searched = false;
  static void((*rf)(void *)) = 0;

  if (!Searched) {
    Searched = true;
    *(void **)&rf =
        llvm::sys::DynamicLibrary::SearchForAddressOfSymbol("__register_frame");
  }
  if (rf)
    rf(p);
}

void __deregister_frame(void *p) {
  static bool Searched = false;
  static void((*df)(void *)) = 0;

  if (!Searched) {
    Searched = true;
    *(void **)&df = llvm::sys::DynamicLibrary::SearchForAddressOfSymbol(
        "__deregister_frame");
  }
  if (df)
    df(p);
}
#endif

#ifdef __APPLE__

static const char *processFDE(const char *Entry, bool isDeregister) {
  const char *P = Entry;
  uint32_t Length = *((const uint32_t *)P);
  P += 4;
  uint32_t Offset = *((const uint32_t *)P);
  if (Offset != 0) {
    if (isDeregister)
      __deregister_frame(const_cast<char *>(Entry));
    else
      __register_frame(const_cast<char *>(Entry));
  }
  return P + Length;
}

// This implementation handles frame registration for local targets.
// Memory managers for remote targets should re-implement this function
// and use the LoadAddr parameter.
void RTDyldMemoryManager::registerEHFramesInProcess(uint8_t *Addr,
                                                    size_t Size) {
  // On OS X OS X __register_frame takes a single FDE as an argument.
  // See http://lists.llvm.org/pipermail/llvm-dev/2013-April/061737.html
  // and projects/libunwind/src/UnwindLevel1-gcc-ext.c.
  const char *P = (const char *)Addr;
  const char *End = P + Size;
  do  {
    P = processFDE(P, false);
  } while(P != End);
}

void RTDyldMemoryManager::deregisterEHFramesInProcess(uint8_t *Addr,
                                                      size_t Size) {
  const char *P = (const char *)Addr;
  const char *End = P + Size;
  do  {
    P = processFDE(P, true);
  } while(P != End);
}

#else

void RTDyldMemoryManager::registerEHFramesInProcess(uint8_t *Addr,
                                                    size_t Size) {
  // On Linux __register_frame takes a single argument:
  // a pointer to the start of the .eh_frame section.

  // How can it find the end? Because crtendS.o is linked
  // in and it has an .eh_frame section with four zero chars.
  __register_frame(Addr);
}

void RTDyldMemoryManager::deregisterEHFramesInProcess(uint8_t *Addr,
                                                      size_t Size) {
  __deregister_frame(Addr);
}

#endif

void RTDyldMemoryManager::registerEHFrames(uint8_t *Addr, uint64_t LoadAddr,
                                          size_t Size) {
  registerEHFramesInProcess(Addr, Size);
  EHFrames.push_back({Addr, Size});
}

void RTDyldMemoryManager::deregisterEHFrames() {
  for (auto &Frame : EHFrames)
    deregisterEHFramesInProcess(Frame.Addr, Frame.Size);
  EHFrames.clear();
}

static int jit_noop() {
  return 0;
}

// ARM math functions are statically linked on Android from libgcc.a, but not
// available at runtime for dynamic linking. On Linux these are usually placed
// in libgcc_s.so so can be found by normal dynamic lookup.
#if defined(__BIONIC__) && defined(__arm__)
// List of functions which are statically linked on Android and can be generated
// by LLVM. This is done as a nested macro which is used once to declare the
// imported functions with ARM_MATH_DECL and once to compare them to the
// user-requested symbol in getSymbolAddress with ARM_MATH_CHECK. The test
// assumes that all functions start with __aeabi_ and getSymbolAddress must be
// modified if that changes.
#define ARM_MATH_IMPORTS(PP) \
  PP(__aeabi_d2f) \
  PP(__aeabi_d2iz) \
  PP(__aeabi_d2lz) \
  PP(__aeabi_d2uiz) \
  PP(__aeabi_d2ulz) \
  PP(__aeabi_dadd) \
  PP(__aeabi_dcmpeq) \
  PP(__aeabi_dcmpge) \
  PP(__aeabi_dcmpgt) \
  PP(__aeabi_dcmple) \
  PP(__aeabi_dcmplt) \
  PP(__aeabi_dcmpun) \
  PP(__aeabi_ddiv) \
  PP(__aeabi_dmul) \
  PP(__aeabi_dsub) \
  PP(__aeabi_f2d) \
  PP(__aeabi_f2iz) \
  PP(__aeabi_f2lz) \
  PP(__aeabi_f2uiz) \
  PP(__aeabi_f2ulz) \
  PP(__aeabi_fadd) \
  PP(__aeabi_fcmpeq) \
  PP(__aeabi_fcmpge) \
  PP(__aeabi_fcmpgt) \
  PP(__aeabi_fcmple) \
  PP(__aeabi_fcmplt) \
  PP(__aeabi_fcmpun) \
  PP(__aeabi_fdiv) \
  PP(__aeabi_fmul) \
  PP(__aeabi_fsub) \
  PP(__aeabi_i2d) \
  PP(__aeabi_i2f) \
  PP(__aeabi_idiv) \
  PP(__aeabi_idivmod) \
  PP(__aeabi_l2d) \
  PP(__aeabi_l2f) \
  PP(__aeabi_lasr) \
  PP(__aeabi_ldivmod) \
  PP(__aeabi_llsl) \
  PP(__aeabi_llsr) \
  PP(__aeabi_lmul) \
  PP(__aeabi_ui2d) \
  PP(__aeabi_ui2f) \
  PP(__aeabi_uidiv) \
  PP(__aeabi_uidivmod) \
  PP(__aeabi_ul2d) \
  PP(__aeabi_ul2f) \
  PP(__aeabi_uldivmod)

// Declare statically linked math functions on ARM. The function declarations
// here do not have the correct prototypes for each function in
// ARM_MATH_IMPORTS, but it doesn't matter because only the symbol addresses are
// needed. In particular the __aeabi_*divmod functions do not have calling
// conventions which match any C prototype.
#define ARM_MATH_DECL(name) extern "C" void name();
ARM_MATH_IMPORTS(ARM_MATH_DECL)
#undef ARM_MATH_DECL
#endif

#if defined(__linux__) && defined(__GLIBC__) && \
      (defined(__i386__) || defined(__x86_64__))
extern "C" LLVM_ATTRIBUTE_WEAK void __morestack();
#endif

uint64_t
RTDyldMemoryManager::getSymbolAddressInProcess(const std::string &Name) {
  // This implementation assumes that the host program is the target.
  // Clients generating code for a remote target should implement their own
  // memory manager.
#if defined(__linux__) && defined(__GLIBC__)
  //===--------------------------------------------------------------------===//
  // Function stubs that are invoked instead of certain library calls
  //
  // Force the following functions to be linked in to anything that uses the
  // JIT. This is a hack designed to work around the all-too-clever Glibc
  // strategy of making these functions work differently when inlined vs. when
  // not inlined, and hiding their real definitions in a separate archive file
  // that the dynamic linker can't see. For more info, search for
  // 'libc_nonshared.a' on Google, or read http://llvm.org/PR274.
  if (Name == "stat") return (uint64_t)&stat;
  if (Name == "fstat") return (uint64_t)&fstat;
  if (Name == "lstat") return (uint64_t)&lstat;
  if (Name == "stat64") return (uint64_t)&stat64;
  if (Name == "fstat64") return (uint64_t)&fstat64;
  if (Name == "lstat64") return (uint64_t)&lstat64;
  if (Name == "atexit") return (uint64_t)&atexit;
  if (Name == "mknod") return (uint64_t)&mknod;

#if defined(__i386__) || defined(__x86_64__)
  // __morestack lives in libgcc, a static library.
  if (&__morestack && Name == "__morestack")
    return (uint64_t)&__morestack;
#endif
#endif // __linux__ && __GLIBC__

  // See ARM_MATH_IMPORTS definition for explanation
#if defined(__BIONIC__) && defined(__arm__)
  if (Name.compare(0, 8, "__aeabi_") == 0) {
    // Check if the user has requested any of the functions listed in
    // ARM_MATH_IMPORTS, and if so redirect to the statically linked symbol.
#define ARM_MATH_CHECK(fn) if (Name == #fn) return (uint64_t)&fn;
    ARM_MATH_IMPORTS(ARM_MATH_CHECK)
#undef ARM_MATH_CHECK
  }
#endif

  // We should not invoke parent's ctors/dtors from generated main()!
  // On Mingw and Cygwin, the symbol __main is resolved to
  // callee's(eg. tools/lli) one, to invoke wrong duplicated ctors
  // (and register wrong callee's dtors with atexit(3)).
  // We expect ExecutionEngine::runStaticConstructorsDestructors()
  // is called before ExecutionEngine::runFunctionAsMain() is called.
  if (Name == "__main") return (uint64_t)&jit_noop;

  const char *NameStr = Name.c_str();

  // DynamicLibrary::SearchForAddresOfSymbol expects an unmangled 'C' symbol
  // name so ff we're on Darwin, strip the leading '_' off.
#ifdef __APPLE__
  if (NameStr[0] == '_')
    ++NameStr;
#endif

  return (uint64_t)sys::DynamicLibrary::SearchForAddressOfSymbol(NameStr);
}

void *RTDyldMemoryManager::getPointerToNamedFunction(const std::string &Name,
                                                     bool AbortOnFailure) {
  uint64_t Addr = getSymbolAddress(Name);

  if (!Addr && AbortOnFailure)
    report_fatal_error("Program used external function '" + Name +
                       "' which could not be resolved!");

  return (void*)Addr;
}

void RTDyldMemoryManager::anchor() {}
void MCJITMemoryManager::anchor() {}
} // namespace llvm
