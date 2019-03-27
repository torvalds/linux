//===-- asan_win.cc -------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
// Windows-specific details.
//===----------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_platform.h"
#if SANITIZER_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdlib.h>

#include "asan_interceptors.h"
#include "asan_internal.h"
#include "asan_report.h"
#include "asan_stack.h"
#include "asan_thread.h"
#include "asan_mapping.h"
#include "sanitizer_common/sanitizer_libc.h"
#include "sanitizer_common/sanitizer_mutex.h"
#include "sanitizer_common/sanitizer_win.h"
#include "sanitizer_common/sanitizer_win_defs.h"

using namespace __asan;  // NOLINT

extern "C" {
SANITIZER_INTERFACE_ATTRIBUTE
int __asan_should_detect_stack_use_after_return() {
  __asan_init();
  return __asan_option_detect_stack_use_after_return;
}

SANITIZER_INTERFACE_ATTRIBUTE
uptr __asan_get_shadow_memory_dynamic_address() {
  __asan_init();
  return __asan_shadow_memory_dynamic_address;
}
}  // extern "C"

// ---------------------- Windows-specific interceptors ---------------- {{{
static LPTOP_LEVEL_EXCEPTION_FILTER default_seh_handler;
static LPTOP_LEVEL_EXCEPTION_FILTER user_seh_handler;

extern "C" SANITIZER_INTERFACE_ATTRIBUTE
long __asan_unhandled_exception_filter(EXCEPTION_POINTERS *info) {
  EXCEPTION_RECORD *exception_record = info->ExceptionRecord;
  CONTEXT *context = info->ContextRecord;

  // FIXME: Handle EXCEPTION_STACK_OVERFLOW here.

  SignalContext sig(exception_record, context);
  ReportDeadlySignal(sig);
  UNREACHABLE("returned from reporting deadly signal");
}

// Wrapper SEH Handler. If the exception should be handled by asan, we call
// __asan_unhandled_exception_filter, otherwise, we execute the user provided
// exception handler or the default.
static long WINAPI SEHHandler(EXCEPTION_POINTERS *info) {
  DWORD exception_code = info->ExceptionRecord->ExceptionCode;
  if (__sanitizer::IsHandledDeadlyException(exception_code))
    return __asan_unhandled_exception_filter(info);
  if (user_seh_handler)
    return user_seh_handler(info);
  // Bubble out to the default exception filter.
  if (default_seh_handler)
    return default_seh_handler(info);
  return EXCEPTION_CONTINUE_SEARCH;
}

INTERCEPTOR_WINAPI(LPTOP_LEVEL_EXCEPTION_FILTER, SetUnhandledExceptionFilter,
    LPTOP_LEVEL_EXCEPTION_FILTER ExceptionFilter) {
  CHECK(REAL(SetUnhandledExceptionFilter));
  if (ExceptionFilter == &SEHHandler)
    return REAL(SetUnhandledExceptionFilter)(ExceptionFilter);
  // We record the user provided exception handler to be called for all the
  // exceptions unhandled by asan.
  Swap(ExceptionFilter, user_seh_handler);
  return ExceptionFilter;
}

INTERCEPTOR_WINAPI(void, RtlRaiseException, EXCEPTION_RECORD *ExceptionRecord) {
  CHECK(REAL(RtlRaiseException));
  // This is a noreturn function, unless it's one of the exceptions raised to
  // communicate with the debugger, such as the one from OutputDebugString.
  if (ExceptionRecord->ExceptionCode != DBG_PRINTEXCEPTION_C)
    __asan_handle_no_return();
  REAL(RtlRaiseException)(ExceptionRecord);
}

INTERCEPTOR_WINAPI(void, RaiseException, void *a, void *b, void *c, void *d) {
  CHECK(REAL(RaiseException));
  __asan_handle_no_return();
  REAL(RaiseException)(a, b, c, d);
}

#ifdef _WIN64

INTERCEPTOR_WINAPI(int, __C_specific_handler, void *a, void *b, void *c, void *d) {  // NOLINT
  CHECK(REAL(__C_specific_handler));
  __asan_handle_no_return();
  return REAL(__C_specific_handler)(a, b, c, d);
}

#else

INTERCEPTOR(int, _except_handler3, void *a, void *b, void *c, void *d) {
  CHECK(REAL(_except_handler3));
  __asan_handle_no_return();
  return REAL(_except_handler3)(a, b, c, d);
}

#if ASAN_DYNAMIC
// This handler is named differently in -MT and -MD CRTs.
#define _except_handler4 _except_handler4_common
#endif
INTERCEPTOR(int, _except_handler4, void *a, void *b, void *c, void *d) {
  CHECK(REAL(_except_handler4));
  __asan_handle_no_return();
  return REAL(_except_handler4)(a, b, c, d);
}
#endif

static thread_return_t THREAD_CALLING_CONV asan_thread_start(void *arg) {
  AsanThread *t = (AsanThread*)arg;
  SetCurrentThread(t);
  return t->ThreadStart(GetTid(), /* signal_thread_is_registered */ nullptr);
}

INTERCEPTOR_WINAPI(DWORD, CreateThread,
                   void* security, uptr stack_size,
                   DWORD (__stdcall *start_routine)(void*), void* arg,
                   DWORD thr_flags, void* tid) {
  // Strict init-order checking is thread-hostile.
  if (flags()->strict_init_order)
    StopInitOrderChecking();
  GET_STACK_TRACE_THREAD;
  // FIXME: The CreateThread interceptor is not the same as a pthread_create
  // one.  This is a bandaid fix for PR22025.
  bool detached = false;  // FIXME: how can we determine it on Windows?
  u32 current_tid = GetCurrentTidOrInvalid();
  AsanThread *t =
        AsanThread::Create(start_routine, arg, current_tid, &stack, detached);
  return REAL(CreateThread)(security, stack_size,
                            asan_thread_start, t, thr_flags, tid);
}

// }}}

namespace __asan {

void InitializePlatformInterceptors() {
  // The interceptors were not designed to be removable, so we have to keep this
  // module alive for the life of the process.
  HMODULE pinned;
  CHECK(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_PIN,
                           (LPCWSTR)&InitializePlatformInterceptors,
                           &pinned));

  ASAN_INTERCEPT_FUNC(CreateThread);
  ASAN_INTERCEPT_FUNC(SetUnhandledExceptionFilter);

#ifdef _WIN64
  ASAN_INTERCEPT_FUNC(__C_specific_handler);
#else
  ASAN_INTERCEPT_FUNC(_except_handler3);
  ASAN_INTERCEPT_FUNC(_except_handler4);
#endif

  // Try to intercept kernel32!RaiseException, and if that fails, intercept
  // ntdll!RtlRaiseException instead.
  if (!::__interception::OverrideFunction("RaiseException",
                                          (uptr)WRAP(RaiseException),
                                          (uptr *)&REAL(RaiseException))) {
    CHECK(::__interception::OverrideFunction("RtlRaiseException",
                                             (uptr)WRAP(RtlRaiseException),
                                             (uptr *)&REAL(RtlRaiseException)));
  }
}

void AsanApplyToGlobals(globals_op_fptr op, const void *needle) {
  UNIMPLEMENTED();
}

// ---------------------- TSD ---------------- {{{
static bool tsd_key_inited = false;

static __declspec(thread) void *fake_tsd = 0;

void AsanTSDInit(void (*destructor)(void *tsd)) {
  // FIXME: we're ignoring the destructor for now.
  tsd_key_inited = true;
}

void *AsanTSDGet() {
  CHECK(tsd_key_inited);
  return fake_tsd;
}

void AsanTSDSet(void *tsd) {
  CHECK(tsd_key_inited);
  fake_tsd = tsd;
}

void PlatformTSDDtor(void *tsd) {
  AsanThread::TSDDtor(tsd);
}
// }}}

// ---------------------- Various stuff ---------------- {{{
void *AsanDoesNotSupportStaticLinkage() {
#if defined(_DEBUG)
#error Please build the runtime with a non-debug CRT: /MD or /MT
#endif
  return 0;
}

uptr FindDynamicShadowStart() {
  uptr granularity = GetMmapGranularity();
  uptr alignment = 8 * granularity;
  uptr left_padding = granularity;
  uptr space_size = kHighShadowEnd + left_padding;
  uptr shadow_start = FindAvailableMemoryRange(space_size, alignment,
                                               granularity, nullptr, nullptr);
  CHECK_NE((uptr)0, shadow_start);
  CHECK(IsAligned(shadow_start, alignment));
  return shadow_start;
}

void AsanCheckDynamicRTPrereqs() {}

void AsanCheckIncompatibleRT() {}

void ReadContextStack(void *context, uptr *stack, uptr *ssize) {
  UNIMPLEMENTED();
}

void AsanOnDeadlySignal(int, void *siginfo, void *context) {
  UNIMPLEMENTED();
}

#if SANITIZER_WINDOWS64
// Exception handler for dealing with shadow memory.
static LONG CALLBACK
ShadowExceptionHandler(PEXCEPTION_POINTERS exception_pointers) {
  uptr page_size = GetPageSizeCached();
  // Only handle access violations.
  if (exception_pointers->ExceptionRecord->ExceptionCode !=
      EXCEPTION_ACCESS_VIOLATION) {
    return EXCEPTION_CONTINUE_SEARCH;
  }

  // Only handle access violations that land within the shadow memory.
  uptr addr =
      (uptr)(exception_pointers->ExceptionRecord->ExceptionInformation[1]);

  // Check valid shadow range.
  if (!AddrIsInShadow(addr)) return EXCEPTION_CONTINUE_SEARCH;

  // This is an access violation while trying to read from the shadow. Commit
  // the relevant page and let execution continue.

  // Determine the address of the page that is being accessed.
  uptr page = RoundDownTo(addr, page_size);

  // Commit the page.
  uptr result =
      (uptr)::VirtualAlloc((LPVOID)page, page_size, MEM_COMMIT, PAGE_READWRITE);
  if (result != page) return EXCEPTION_CONTINUE_SEARCH;

  // The page mapping succeeded, so continue execution as usual.
  return EXCEPTION_CONTINUE_EXECUTION;
}

#endif

void InitializePlatformExceptionHandlers() {
#if SANITIZER_WINDOWS64
  // On Win64, we map memory on demand with access violation handler.
  // Install our exception handler.
  CHECK(AddVectoredExceptionHandler(TRUE, &ShadowExceptionHandler));
#endif
}

bool IsSystemHeapAddress(uptr addr) {
  return ::HeapValidate(GetProcessHeap(), 0, (void*)addr) != FALSE;
}

// We want to install our own exception handler (EH) to print helpful reports
// on access violations and whatnot.  Unfortunately, the CRT initializers assume
// they are run before any user code and drop any previously-installed EHs on
// the floor, so we can't install our handler inside __asan_init.
// (See crt0dat.c in the CRT sources for the details)
//
// Things get even more complicated with the dynamic runtime, as it finishes its
// initialization before the .exe module CRT begins to initialize.
//
// For the static runtime (-MT), it's enough to put a callback to
// __asan_set_seh_filter in the last section for C initializers.
//
// For the dynamic runtime (-MD), we want link the same
// asan_dynamic_runtime_thunk.lib to all the modules, thus __asan_set_seh_filter
// will be called for each instrumented module.  This ensures that at least one
// __asan_set_seh_filter call happens after the .exe module CRT is initialized.
extern "C" SANITIZER_INTERFACE_ATTRIBUTE
int __asan_set_seh_filter() {
  // We should only store the previous handler if it's not our own handler in
  // order to avoid loops in the EH chain.
  auto prev_seh_handler = SetUnhandledExceptionFilter(SEHHandler);
  if (prev_seh_handler != &SEHHandler)
    default_seh_handler = prev_seh_handler;
  return 0;
}

bool HandleDlopenInit() {
  // Not supported on this platform.
  static_assert(!SANITIZER_SUPPORTS_INIT_FOR_DLOPEN,
                "Expected SANITIZER_SUPPORTS_INIT_FOR_DLOPEN to be false");
  return false;
}

#if !ASAN_DYNAMIC
// The CRT runs initializers in this order:
// - C initializers, from XIA to XIZ
// - C++ initializers, from XCA to XCZ
// Prior to 2015, the CRT set the unhandled exception filter at priority XIY,
// near the end of C initialization. Starting in 2015, it was moved to the
// beginning of C++ initialization. We set our priority to XCAB to run
// immediately after the CRT runs. This way, our exception filter is called
// first and we can delegate to their filter if appropriate.
#pragma section(".CRT$XCAB", long, read)  // NOLINT
__declspec(allocate(".CRT$XCAB")) int (*__intercept_seh)() =
    __asan_set_seh_filter;

// Piggyback on the TLS initialization callback directory to initialize asan as
// early as possible. Initializers in .CRT$XL* are called directly by ntdll,
// which run before the CRT. Users also add code to .CRT$XLC, so it's important
// to run our initializers first.
static void NTAPI asan_thread_init(void *module, DWORD reason, void *reserved) {
  if (reason == DLL_PROCESS_ATTACH) __asan_init();
}

#pragma section(".CRT$XLAB", long, read)  // NOLINT
__declspec(allocate(".CRT$XLAB")) void (NTAPI *__asan_tls_init)(void *,
    unsigned long, void *) = asan_thread_init;
#endif

WIN_FORCE_LINK(__asan_dso_reg_hook)

// }}}
}  // namespace __asan

#endif  // SANITIZER_WINDOWS
