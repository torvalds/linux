//===--------------------------- Unwind-seh.cpp ---------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  Implements SEH-based Itanium C++ exceptions.
//
//===----------------------------------------------------------------------===//

#include "config.h"

#if defined(_LIBUNWIND_SUPPORT_SEH_UNWIND)

#include <unwind.h>

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include <windef.h>
#include <excpt.h>
#include <winnt.h>
#include <ntstatus.h>

#include "libunwind_ext.h"
#include "UnwindCursor.hpp"

using namespace libunwind;

#define STATUS_USER_DEFINED (1u << 29)

#define STATUS_GCC_MAGIC  (('G' << 16) | ('C' << 8) | 'C')

#define MAKE_CUSTOM_STATUS(s, c) \
  ((NTSTATUS)(((s) << 30) | STATUS_USER_DEFINED | (c)))
#define MAKE_GCC_EXCEPTION(c) \
  MAKE_CUSTOM_STATUS(STATUS_SEVERITY_SUCCESS, STATUS_GCC_MAGIC | ((c) << 24))

/// SEH exception raised by libunwind when the program calls
/// \c _Unwind_RaiseException.
#define STATUS_GCC_THROW MAKE_GCC_EXCEPTION(0) // 0x20474343
/// SEH exception raised by libunwind to initiate phase 2 of exception
/// handling.
#define STATUS_GCC_UNWIND MAKE_GCC_EXCEPTION(1) // 0x21474343

/// Class of foreign exceptions based on unrecognized SEH exceptions.
static const uint64_t kSEHExceptionClass = 0x434C4E4753454800; // CLNGSEH\0

/// Exception cleanup routine used by \c _GCC_specific_handler to
/// free foreign exceptions.
static void seh_exc_cleanup(_Unwind_Reason_Code urc, _Unwind_Exception *exc) {
  if (exc->exception_class != kSEHExceptionClass)
    _LIBUNWIND_ABORT("SEH cleanup called on non-SEH exception");
  free(exc);
}

static int _unw_init_seh(unw_cursor_t *cursor, CONTEXT *ctx);
static DISPATCHER_CONTEXT *_unw_seh_get_disp_ctx(unw_cursor_t *cursor);
static void _unw_seh_set_disp_ctx(unw_cursor_t *cursor, DISPATCHER_CONTEXT *disp);

/// Common implementation of SEH-style handler functions used by Itanium-
/// style frames.  Depending on how and why it was called, it may do one of:
///  a) Delegate to the given Itanium-style personality function; or
///  b) Initiate a collided unwind to halt unwinding.
_LIBUNWIND_EXPORT EXCEPTION_DISPOSITION
_GCC_specific_handler(PEXCEPTION_RECORD ms_exc, PVOID frame, PCONTEXT ms_ctx,
                      DISPATCHER_CONTEXT *disp, __personality_routine pers) {
  unw_context_t uc;
  unw_cursor_t cursor;
  _Unwind_Exception *exc;
  _Unwind_Action action;
  struct _Unwind_Context *ctx = nullptr;
  _Unwind_Reason_Code urc;
  uintptr_t retval, target;
  bool ours = false;

  _LIBUNWIND_TRACE_UNWINDING("_GCC_specific_handler(%#010x(%x), %p)", ms_exc->ExceptionCode, ms_exc->ExceptionFlags, frame);
  if (ms_exc->ExceptionCode == STATUS_GCC_UNWIND) {
    if (IS_TARGET_UNWIND(ms_exc->ExceptionFlags)) {
      // Set up the upper return value (the lower one and the target PC
      // were set in the call to RtlUnwindEx()) for the landing pad.
#ifdef __x86_64__
      disp->ContextRecord->Rdx = ms_exc->ExceptionInformation[3];
#elif defined(__arm__)
      disp->ContextRecord->R1 = ms_exc->ExceptionInformation[3];
#elif defined(__aarch64__)
      disp->ContextRecord->X1 = ms_exc->ExceptionInformation[3];
#endif
    }
    // This is the collided unwind to the landing pad. Nothing to do.
    return ExceptionContinueSearch;
  }

  if (ms_exc->ExceptionCode == STATUS_GCC_THROW) {
    // This is (probably) a libunwind-controlled exception/unwind. Recover the
    // parameters which we set below, and pass them to the personality function.
    ours = true;
    exc = (_Unwind_Exception *)ms_exc->ExceptionInformation[0];
    if (!IS_UNWINDING(ms_exc->ExceptionFlags) && ms_exc->NumberParameters > 1) {
      ctx = (struct _Unwind_Context *)ms_exc->ExceptionInformation[1];
      action = (_Unwind_Action)ms_exc->ExceptionInformation[2];
    }
  } else {
    // Foreign exception.
    exc = (_Unwind_Exception *)malloc(sizeof(_Unwind_Exception));
    exc->exception_class = kSEHExceptionClass;
    exc->exception_cleanup = seh_exc_cleanup;
    memset(exc->private_, 0, sizeof(exc->private_));
  }
  if (!ctx) {
    _unw_init_seh(&cursor, disp->ContextRecord);
    _unw_seh_set_disp_ctx(&cursor, disp);
    unw_set_reg(&cursor, UNW_REG_IP, disp->ControlPc-1);
    ctx = (struct _Unwind_Context *)&cursor;

    if (!IS_UNWINDING(ms_exc->ExceptionFlags)) {
      if (ours && ms_exc->NumberParameters > 1)
        action =  (_Unwind_Action)(_UA_CLEANUP_PHASE | _UA_FORCE_UNWIND);
      else
        action = _UA_SEARCH_PHASE;
    } else {
      if (ours && ms_exc->ExceptionInformation[1] == (ULONG_PTR)frame)
        action = (_Unwind_Action)(_UA_CLEANUP_PHASE | _UA_HANDLER_FRAME);
      else
        action = _UA_CLEANUP_PHASE;
    }
  }

  _LIBUNWIND_TRACE_UNWINDING("_GCC_specific_handler() calling personality function %p(1, %d, %llx, %p, %p)", pers, action, exc->exception_class, exc, ctx);
  urc = pers(1, action, exc->exception_class, exc, ctx);
  _LIBUNWIND_TRACE_UNWINDING("_GCC_specific_handler() personality returned %d", urc);
  switch (urc) {
  case _URC_CONTINUE_UNWIND:
    // If we're in phase 2, and the personality routine said to continue
    // at the target frame, we're in real trouble.
    if (action & _UA_HANDLER_FRAME)
      _LIBUNWIND_ABORT("Personality continued unwind at the target frame!");
    return ExceptionContinueSearch;
  case _URC_HANDLER_FOUND:
    // If we were called by __libunwind_seh_personality(), indicate that
    // a handler was found; otherwise, initiate phase 2 by unwinding.
    if (ours && ms_exc->NumberParameters > 1)
      return 4 /* ExecptionExecuteHandler in mingw */;
    // This should never happen in phase 2.
    if (IS_UNWINDING(ms_exc->ExceptionFlags))
      _LIBUNWIND_ABORT("Personality indicated exception handler in phase 2!");
    exc->private_[1] = (ULONG_PTR)frame;
    if (ours) {
      ms_exc->NumberParameters = 4;
      ms_exc->ExceptionInformation[1] = (ULONG_PTR)frame;
    }
    // FIXME: Indicate target frame in foreign case!
    // phase 2: the clean up phase
    RtlUnwindEx(frame, (PVOID)disp->ControlPc, ms_exc, exc, ms_ctx, disp->HistoryTable);
    _LIBUNWIND_ABORT("RtlUnwindEx() failed");
  case _URC_INSTALL_CONTEXT: {
    // If we were called by __libunwind_seh_personality(), indicate that
    // a handler was found; otherwise, it's time to initiate a collided
    // unwind to the target.
    if (ours && !IS_UNWINDING(ms_exc->ExceptionFlags) && ms_exc->NumberParameters > 1)
      return 4 /* ExecptionExecuteHandler in mingw */;
    // This should never happen in phase 1.
    if (!IS_UNWINDING(ms_exc->ExceptionFlags))
      _LIBUNWIND_ABORT("Personality installed context during phase 1!");
#ifdef __x86_64__
    exc->private_[2] = disp->TargetIp;
    unw_get_reg(&cursor, UNW_X86_64_RAX, &retval);
    unw_get_reg(&cursor, UNW_X86_64_RDX, &exc->private_[3]);
#elif defined(__arm__)
    exc->private_[2] = disp->TargetPc;
    unw_get_reg(&cursor, UNW_ARM_R0, &retval);
    unw_get_reg(&cursor, UNW_ARM_R1, &exc->private_[3]);
#elif defined(__aarch64__)
    exc->private_[2] = disp->TargetPc;
    unw_get_reg(&cursor, UNW_ARM64_X0, &retval);
    unw_get_reg(&cursor, UNW_ARM64_X1, &exc->private_[3]);
#endif
    unw_get_reg(&cursor, UNW_REG_IP, &target);
    ms_exc->ExceptionCode = STATUS_GCC_UNWIND;
#ifdef __x86_64__
    ms_exc->ExceptionInformation[2] = disp->TargetIp;
#elif defined(__arm__) || defined(__aarch64__)
    ms_exc->ExceptionInformation[2] = disp->TargetPc;
#endif
    ms_exc->ExceptionInformation[3] = exc->private_[3];
    // Give NTRTL some scratch space to keep track of the collided unwind.
    // Don't use the one that was passed in; we don't want to overwrite the
    // context in the DISPATCHER_CONTEXT.
    CONTEXT new_ctx;
    RtlUnwindEx(frame, (PVOID)target, ms_exc, (PVOID)retval, &new_ctx, disp->HistoryTable);
    _LIBUNWIND_ABORT("RtlUnwindEx() failed");
  }
  // Anything else indicates a serious problem.
  default: return ExceptionContinueExecution;
  }
}

/// Personality function returned by \c unw_get_proc_info() in SEH contexts.
/// This is a wrapper that calls the real SEH handler function, which in
/// turn (at least, for Itanium-style frames) calls the real Itanium
/// personality function (see \c _GCC_specific_handler()).
extern "C" _Unwind_Reason_Code
__libunwind_seh_personality(int version, _Unwind_Action state,
                            uint64_t klass, _Unwind_Exception *exc,
                            struct _Unwind_Context *context) {
  EXCEPTION_RECORD ms_exc;
  bool phase2 = (state & (_UA_SEARCH_PHASE|_UA_CLEANUP_PHASE)) == _UA_CLEANUP_PHASE;
  ms_exc.ExceptionCode = STATUS_GCC_THROW;
  ms_exc.ExceptionFlags = 0;
  ms_exc.NumberParameters = 3;
  ms_exc.ExceptionInformation[0] = (ULONG_PTR)exc;
  ms_exc.ExceptionInformation[1] = (ULONG_PTR)context;
  ms_exc.ExceptionInformation[2] = state;
  DISPATCHER_CONTEXT *disp_ctx = _unw_seh_get_disp_ctx((unw_cursor_t *)context);
  EXCEPTION_DISPOSITION ms_act = disp_ctx->LanguageHandler(&ms_exc,
                                                           (PVOID)disp_ctx->EstablisherFrame,
                                                           disp_ctx->ContextRecord,
                                                           disp_ctx);
  switch (ms_act) {
  case ExceptionContinueSearch: return _URC_CONTINUE_UNWIND;
  case 4 /*ExceptionExecuteHandler*/:
    return phase2 ? _URC_INSTALL_CONTEXT : _URC_HANDLER_FOUND;
  default:
    return phase2 ? _URC_FATAL_PHASE2_ERROR : _URC_FATAL_PHASE1_ERROR;
  }
}

static _Unwind_Reason_Code
unwind_phase2_forced(unw_context_t *uc,
                     _Unwind_Exception *exception_object,
                     _Unwind_Stop_Fn stop, void *stop_parameter) {
  unw_cursor_t cursor2;
  unw_init_local(&cursor2, uc);

  // Walk each frame until we reach where search phase said to stop
  while (unw_step(&cursor2) > 0) {

    // Update info about this frame.
    unw_proc_info_t frameInfo;
    if (unw_get_proc_info(&cursor2, &frameInfo) != UNW_ESUCCESS) {
      _LIBUNWIND_TRACE_UNWINDING("unwind_phase2_forced(ex_ojb=%p): unw_step "
                                 "failed => _URC_END_OF_STACK",
                                 (void *)exception_object);
      return _URC_FATAL_PHASE2_ERROR;
    }

    // When tracing, print state information.
    if (_LIBUNWIND_TRACING_UNWINDING) {
      char functionBuf[512];
      const char *functionName = functionBuf;
      unw_word_t offset;
      if ((unw_get_proc_name(&cursor2, functionBuf, sizeof(functionBuf),
                             &offset) != UNW_ESUCCESS) ||
          (frameInfo.start_ip + offset > frameInfo.end_ip))
        functionName = ".anonymous.";
      _LIBUNWIND_TRACE_UNWINDING(
          "unwind_phase2_forced(ex_ojb=%p): start_ip=0x%" PRIx64
          ", func=%s, lsda=0x%" PRIx64 ", personality=0x%" PRIx64,
          (void *)exception_object, frameInfo.start_ip, functionName,
          frameInfo.lsda, frameInfo.handler);
    }

    // Call stop function at each frame.
    _Unwind_Action action =
        (_Unwind_Action)(_UA_FORCE_UNWIND | _UA_CLEANUP_PHASE);
    _Unwind_Reason_Code stopResult =
        (*stop)(1, action, exception_object->exception_class, exception_object,
                (struct _Unwind_Context *)(&cursor2), stop_parameter);
    _LIBUNWIND_TRACE_UNWINDING(
        "unwind_phase2_forced(ex_ojb=%p): stop function returned %d",
        (void *)exception_object, stopResult);
    if (stopResult != _URC_NO_REASON) {
      _LIBUNWIND_TRACE_UNWINDING(
          "unwind_phase2_forced(ex_ojb=%p): stopped by stop function",
          (void *)exception_object);
      return _URC_FATAL_PHASE2_ERROR;
    }

    // If there is a personality routine, tell it we are unwinding.
    if (frameInfo.handler != 0) {
      __personality_routine p =
          (__personality_routine)(intptr_t)(frameInfo.handler);
      _LIBUNWIND_TRACE_UNWINDING(
          "unwind_phase2_forced(ex_ojb=%p): calling personality function %p",
          (void *)exception_object, (void *)(uintptr_t)p);
      _Unwind_Reason_Code personalityResult =
          (*p)(1, action, exception_object->exception_class, exception_object,
               (struct _Unwind_Context *)(&cursor2));
      switch (personalityResult) {
      case _URC_CONTINUE_UNWIND:
        _LIBUNWIND_TRACE_UNWINDING("unwind_phase2_forced(ex_ojb=%p): "
                                   "personality returned "
                                   "_URC_CONTINUE_UNWIND",
                                   (void *)exception_object);
        // Destructors called, continue unwinding
        break;
      case _URC_INSTALL_CONTEXT:
        _LIBUNWIND_TRACE_UNWINDING("unwind_phase2_forced(ex_ojb=%p): "
                                   "personality returned "
                                   "_URC_INSTALL_CONTEXT",
                                   (void *)exception_object);
        // We may get control back if landing pad calls _Unwind_Resume().
        unw_resume(&cursor2);
        break;
      default:
        // Personality routine returned an unknown result code.
        _LIBUNWIND_TRACE_UNWINDING("unwind_phase2_forced(ex_ojb=%p): "
                                   "personality returned %d, "
                                   "_URC_FATAL_PHASE2_ERROR",
                                   (void *)exception_object, personalityResult);
        return _URC_FATAL_PHASE2_ERROR;
      }
    }
  }

  // Call stop function one last time and tell it we've reached the end
  // of the stack.
  _LIBUNWIND_TRACE_UNWINDING("unwind_phase2_forced(ex_ojb=%p): calling stop "
                             "function with _UA_END_OF_STACK",
                             (void *)exception_object);
  _Unwind_Action lastAction =
      (_Unwind_Action)(_UA_FORCE_UNWIND | _UA_CLEANUP_PHASE | _UA_END_OF_STACK);
  (*stop)(1, lastAction, exception_object->exception_class, exception_object,
          (struct _Unwind_Context *)(&cursor2), stop_parameter);

  // Clean up phase did not resume at the frame that the search phase said it
  // would.
  return _URC_FATAL_PHASE2_ERROR;
}

/// Called by \c __cxa_throw().  Only returns if there is a fatal error.
_LIBUNWIND_EXPORT _Unwind_Reason_Code
_Unwind_RaiseException(_Unwind_Exception *exception_object) {
  _LIBUNWIND_TRACE_API("_Unwind_RaiseException(ex_obj=%p)",
                       (void *)exception_object);

  // Mark that this is a non-forced unwind, so _Unwind_Resume()
  // can do the right thing.
  memset(exception_object->private_, 0, sizeof(exception_object->private_));

  // phase 1: the search phase
  // We'll let the system do that for us.
  RaiseException(STATUS_GCC_THROW, 0, 1, (ULONG_PTR *)&exception_object);

  // If we get here, either something went horribly wrong or we reached the
  // top of the stack. Either way, let libc++abi call std::terminate().
  return _URC_END_OF_STACK;
}

/// When \c _Unwind_RaiseException() is in phase2, it hands control
/// to the personality function at each frame.  The personality
/// may force a jump to a landing pad in that function; the landing
/// pad code may then call \c _Unwind_Resume() to continue with the
/// unwinding.  Note: the call to \c _Unwind_Resume() is from compiler
/// geneated user code.  All other \c _Unwind_* routines are called
/// by the C++ runtime \c __cxa_* routines.
///
/// Note: re-throwing an exception (as opposed to continuing the unwind)
/// is implemented by having the code call \c __cxa_rethrow() which
/// in turn calls \c _Unwind_Resume_or_Rethrow().
_LIBUNWIND_EXPORT void
_Unwind_Resume(_Unwind_Exception *exception_object) {
  _LIBUNWIND_TRACE_API("_Unwind_Resume(ex_obj=%p)", (void *)exception_object);

  if (exception_object->private_[0] != 0) {
    unw_context_t uc;

    unw_getcontext(&uc);
    unwind_phase2_forced(&uc, exception_object,
                         (_Unwind_Stop_Fn) exception_object->private_[0],
                         (void *)exception_object->private_[4]);
  } else {
    // Recover the parameters for the unwind from the exception object
    // so we can start unwinding again.
    EXCEPTION_RECORD ms_exc;
    CONTEXT ms_ctx;
    UNWIND_HISTORY_TABLE hist;

    memset(&ms_exc, 0, sizeof(ms_exc));
    memset(&hist, 0, sizeof(hist));
    ms_exc.ExceptionCode = STATUS_GCC_THROW;
    ms_exc.ExceptionFlags = EXCEPTION_NONCONTINUABLE;
    ms_exc.NumberParameters = 4;
    ms_exc.ExceptionInformation[0] = (ULONG_PTR)exception_object;
    ms_exc.ExceptionInformation[1] = exception_object->private_[1];
    ms_exc.ExceptionInformation[2] = exception_object->private_[2];
    ms_exc.ExceptionInformation[3] = exception_object->private_[3];
    RtlUnwindEx((PVOID)exception_object->private_[1],
                (PVOID)exception_object->private_[2], &ms_exc,
                exception_object, &ms_ctx, &hist);
  }

  // Clients assume _Unwind_Resume() does not return, so all we can do is abort.
  _LIBUNWIND_ABORT("_Unwind_Resume() can't return");
}

/// Not used by C++.
/// Unwinds stack, calling "stop" function at each frame.
/// Could be used to implement \c longjmp().
_LIBUNWIND_EXPORT _Unwind_Reason_Code
_Unwind_ForcedUnwind(_Unwind_Exception *exception_object,
                     _Unwind_Stop_Fn stop, void *stop_parameter) {
  _LIBUNWIND_TRACE_API("_Unwind_ForcedUnwind(ex_obj=%p, stop=%p)",
                       (void *)exception_object, (void *)(uintptr_t)stop);
  unw_context_t uc;
  unw_getcontext(&uc);

  // Mark that this is a forced unwind, so _Unwind_Resume() can do
  // the right thing.
  exception_object->private_[0] = (uintptr_t) stop;
  exception_object->private_[4] = (uintptr_t) stop_parameter;

  // do it
  return unwind_phase2_forced(&uc, exception_object, stop, stop_parameter);
}

/// Called by personality handler during phase 2 to get LSDA for current frame.
_LIBUNWIND_EXPORT uintptr_t
_Unwind_GetLanguageSpecificData(struct _Unwind_Context *context) {
  uintptr_t result = (uintptr_t)_unw_seh_get_disp_ctx((unw_cursor_t *)context)->HandlerData;
  _LIBUNWIND_TRACE_API(
      "_Unwind_GetLanguageSpecificData(context=%p) => 0x%" PRIxPTR,
      (void *)context, result);
  return result;
}

/// Called by personality handler during phase 2 to find the start of the
/// function.
_LIBUNWIND_EXPORT uintptr_t
_Unwind_GetRegionStart(struct _Unwind_Context *context) {
  DISPATCHER_CONTEXT *disp = _unw_seh_get_disp_ctx((unw_cursor_t *)context);
  uintptr_t result = (uintptr_t)disp->FunctionEntry->BeginAddress + disp->ImageBase;
  _LIBUNWIND_TRACE_API("_Unwind_GetRegionStart(context=%p) => 0x%" PRIxPTR,
                       (void *)context, result);
  return result;
}

static int
_unw_init_seh(unw_cursor_t *cursor, CONTEXT *context) {
#ifdef _LIBUNWIND_TARGET_X86_64
  new ((void *)cursor) UnwindCursor<LocalAddressSpace, Registers_x86_64>(
      context, LocalAddressSpace::sThisAddressSpace);
  auto *co = reinterpret_cast<AbstractUnwindCursor *>(cursor);
  co->setInfoBasedOnIPRegister();
  return UNW_ESUCCESS;
#elif defined(_LIBUNWIND_TARGET_ARM)
  new ((void *)cursor) UnwindCursor<LocalAddressSpace, Registers_arm>(
      context, LocalAddressSpace::sThisAddressSpace);
  auto *co = reinterpret_cast<AbstractUnwindCursor *>(cursor);
  co->setInfoBasedOnIPRegister();
  return UNW_ESUCCESS;
#elif defined(_LIBUNWIND_TARGET_AARCH64)
  new ((void *)cursor) UnwindCursor<LocalAddressSpace, Registers_arm64>(
      context, LocalAddressSpace::sThisAddressSpace);
  auto *co = reinterpret_cast<AbstractUnwindCursor *>(cursor);
  co->setInfoBasedOnIPRegister();
  return UNW_ESUCCESS;
#else
  return UNW_EINVAL;
#endif
}

static DISPATCHER_CONTEXT *
_unw_seh_get_disp_ctx(unw_cursor_t *cursor) {
#ifdef _LIBUNWIND_TARGET_X86_64
  return reinterpret_cast<UnwindCursor<LocalAddressSpace, Registers_x86_64> *>(cursor)->getDispatcherContext();
#elif defined(_LIBUNWIND_TARGET_ARM)
  return reinterpret_cast<UnwindCursor<LocalAddressSpace, Registers_arm> *>(cursor)->getDispatcherContext();
#elif defined(_LIBUNWIND_TARGET_AARCH64)
  return reinterpret_cast<UnwindCursor<LocalAddressSpace, Registers_arm64> *>(cursor)->getDispatcherContext();
#else
  return nullptr;
#endif
}

static void
_unw_seh_set_disp_ctx(unw_cursor_t *cursor, DISPATCHER_CONTEXT *disp) {
#ifdef _LIBUNWIND_TARGET_X86_64
  reinterpret_cast<UnwindCursor<LocalAddressSpace, Registers_x86_64> *>(cursor)->setDispatcherContext(disp);
#elif defined(_LIBUNWIND_TARGET_ARM)
  reinterpret_cast<UnwindCursor<LocalAddressSpace, Registers_arm> *>(cursor)->setDispatcherContext(disp);
#elif defined(_LIBUNWIND_TARGET_AARCH64)
  reinterpret_cast<UnwindCursor<LocalAddressSpace, Registers_arm64> *>(cursor)->setDispatcherContext(disp);
#endif
}

#endif // defined(_LIBUNWIND_SUPPORT_SEH_UNWIND)
