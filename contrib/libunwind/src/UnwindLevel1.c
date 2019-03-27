//===------------------------- UnwindLevel1.c -----------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//
// Implements C++ ABI Exception Handling Level 1 as documented at:
//      http://mentorembedded.github.io/cxx-abi/abi-eh.html
// using libunwind
//
//===----------------------------------------------------------------------===//

// ARM EHABI does not specify _Unwind_{Get,Set}{GR,IP}().  Thus, we are
// defining inline functions to delegate the function calls to
// _Unwind_VRS_{Get,Set}().  However, some applications might declare the
// function protetype directly (instead of including <unwind.h>), thus we need
// to export these functions from libunwind.so as well.
#define _LIBUNWIND_UNWIND_LEVEL1_EXTERNAL_LINKAGE 1

#include <inttypes.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "libunwind.h"
#include "unwind.h"
#include "config.h"

#if !defined(_LIBUNWIND_ARM_EHABI) && !defined(__USING_SJLJ_EXCEPTIONS__)

#ifndef _LIBUNWIND_SUPPORT_SEH_UNWIND

static _Unwind_Reason_Code
unwind_phase1(unw_context_t *uc, unw_cursor_t *cursor, _Unwind_Exception *exception_object) {
  unw_init_local(cursor, uc);

  // Walk each frame looking for a place to stop.
  bool handlerNotFound = true;
  while (handlerNotFound) {
    // Ask libunwind to get next frame (skip over first which is
    // _Unwind_RaiseException).
    int stepResult = unw_step(cursor);
    if (stepResult == 0) {
      _LIBUNWIND_TRACE_UNWINDING("unwind_phase1(ex_ojb=%p): unw_step() reached "
                                 "bottom => _URC_END_OF_STACK",
                                 (void *)exception_object);
      return _URC_END_OF_STACK;
    } else if (stepResult < 0) {
      _LIBUNWIND_TRACE_UNWINDING("unwind_phase1(ex_ojb=%p): unw_step failed => "
                                 "_URC_FATAL_PHASE1_ERROR",
                                 (void *)exception_object);
      return _URC_FATAL_PHASE1_ERROR;
    }

    // See if frame has code to run (has personality routine).
    unw_proc_info_t frameInfo;
    unw_word_t sp;
    if (unw_get_proc_info(cursor, &frameInfo) != UNW_ESUCCESS) {
      _LIBUNWIND_TRACE_UNWINDING("unwind_phase1(ex_ojb=%p): unw_get_proc_info "
                                 "failed => _URC_FATAL_PHASE1_ERROR",
                                 (void *)exception_object);
      return _URC_FATAL_PHASE1_ERROR;
    }

    // When tracing, print state information.
    if (_LIBUNWIND_TRACING_UNWINDING) {
      char functionBuf[512];
      const char *functionName = functionBuf;
      unw_word_t offset;
      if ((unw_get_proc_name(cursor, functionBuf, sizeof(functionBuf),
                             &offset) != UNW_ESUCCESS) ||
          (frameInfo.start_ip + offset > frameInfo.end_ip))
        functionName = ".anonymous.";
      unw_word_t pc;
      unw_get_reg(cursor, UNW_REG_IP, &pc);
      _LIBUNWIND_TRACE_UNWINDING(
          "unwind_phase1(ex_ojb=%p): pc=0x%" PRIxPTR ", start_ip=0x%" PRIxPTR
          ", func=%s, lsda=0x%" PRIxPTR ", personality=0x%" PRIxPTR "",
          (void *)exception_object, pc, frameInfo.start_ip, functionName,
          frameInfo.lsda, frameInfo.handler);
    }

    // If there is a personality routine, ask it if it will want to stop at
    // this frame.
    if (frameInfo.handler != 0) {
      __personality_routine p =
          (__personality_routine)(uintptr_t)(frameInfo.handler);
      _LIBUNWIND_TRACE_UNWINDING(
          "unwind_phase1(ex_ojb=%p): calling personality function %p",
          (void *)exception_object, (void *)(uintptr_t)p);
      _Unwind_Reason_Code personalityResult =
          (*p)(1, _UA_SEARCH_PHASE, exception_object->exception_class,
               exception_object, (struct _Unwind_Context *)(cursor));
      switch (personalityResult) {
      case _URC_HANDLER_FOUND:
        // found a catch clause or locals that need destructing in this frame
        // stop search and remember stack pointer at the frame
        handlerNotFound = false;
        unw_get_reg(cursor, UNW_REG_SP, &sp);
        exception_object->private_2 = (uintptr_t)sp;
        _LIBUNWIND_TRACE_UNWINDING(
            "unwind_phase1(ex_ojb=%p): _URC_HANDLER_FOUND",
            (void *)exception_object);
        return _URC_NO_REASON;

      case _URC_CONTINUE_UNWIND:
        _LIBUNWIND_TRACE_UNWINDING(
            "unwind_phase1(ex_ojb=%p): _URC_CONTINUE_UNWIND",
            (void *)exception_object);
        // continue unwinding
        break;

      default:
        // something went wrong
        _LIBUNWIND_TRACE_UNWINDING(
            "unwind_phase1(ex_ojb=%p): _URC_FATAL_PHASE1_ERROR",
            (void *)exception_object);
        return _URC_FATAL_PHASE1_ERROR;
      }
    }
  }
  return _URC_NO_REASON;
}


static _Unwind_Reason_Code
unwind_phase2(unw_context_t *uc, unw_cursor_t *cursor, _Unwind_Exception *exception_object) {
  unw_init_local(cursor, uc);

  _LIBUNWIND_TRACE_UNWINDING("unwind_phase2(ex_ojb=%p)",
                             (void *)exception_object);

  // Walk each frame until we reach where search phase said to stop.
  while (true) {

    // Ask libunwind to get next frame (skip over first which is
    // _Unwind_RaiseException).
    int stepResult = unw_step(cursor);
    if (stepResult == 0) {
      _LIBUNWIND_TRACE_UNWINDING("unwind_phase2(ex_ojb=%p): unw_step() reached "
                                 "bottom => _URC_END_OF_STACK",
                                 (void *)exception_object);
      return _URC_END_OF_STACK;
    } else if (stepResult < 0) {
      _LIBUNWIND_TRACE_UNWINDING("unwind_phase2(ex_ojb=%p): unw_step failed => "
                                 "_URC_FATAL_PHASE1_ERROR",
                                 (void *)exception_object);
      return _URC_FATAL_PHASE2_ERROR;
    }

    // Get info about this frame.
    unw_word_t sp;
    unw_proc_info_t frameInfo;
    unw_get_reg(cursor, UNW_REG_SP, &sp);
    if (unw_get_proc_info(cursor, &frameInfo) != UNW_ESUCCESS) {
      _LIBUNWIND_TRACE_UNWINDING("unwind_phase2(ex_ojb=%p): unw_get_proc_info "
                                 "failed => _URC_FATAL_PHASE1_ERROR",
                                 (void *)exception_object);
      return _URC_FATAL_PHASE2_ERROR;
    }

    // When tracing, print state information.
    if (_LIBUNWIND_TRACING_UNWINDING) {
      char functionBuf[512];
      const char *functionName = functionBuf;
      unw_word_t offset;
      if ((unw_get_proc_name(cursor, functionBuf, sizeof(functionBuf),
                             &offset) != UNW_ESUCCESS) ||
          (frameInfo.start_ip + offset > frameInfo.end_ip))
        functionName = ".anonymous.";
      _LIBUNWIND_TRACE_UNWINDING("unwind_phase2(ex_ojb=%p): start_ip=0x%" PRIxPTR
                                 ", func=%s, sp=0x%" PRIxPTR ", lsda=0x%" PRIxPTR
                                 ", personality=0x%" PRIxPTR,
                                 (void *)exception_object, frameInfo.start_ip,
                                 functionName, sp, frameInfo.lsda,
                                 frameInfo.handler);
    }

    // If there is a personality routine, tell it we are unwinding.
    if (frameInfo.handler != 0) {
      __personality_routine p =
          (__personality_routine)(uintptr_t)(frameInfo.handler);
      _Unwind_Action action = _UA_CLEANUP_PHASE;
      if (sp == exception_object->private_2) {
        // Tell personality this was the frame it marked in phase 1.
        action = (_Unwind_Action)(_UA_CLEANUP_PHASE | _UA_HANDLER_FRAME);
      }
       _Unwind_Reason_Code personalityResult =
          (*p)(1, action, exception_object->exception_class, exception_object,
               (struct _Unwind_Context *)(cursor));
      switch (personalityResult) {
      case _URC_CONTINUE_UNWIND:
        // Continue unwinding
        _LIBUNWIND_TRACE_UNWINDING(
            "unwind_phase2(ex_ojb=%p): _URC_CONTINUE_UNWIND",
            (void *)exception_object);
        if (sp == exception_object->private_2) {
          // Phase 1 said we would stop at this frame, but we did not...
          _LIBUNWIND_ABORT("during phase1 personality function said it would "
                           "stop here, but now in phase2 it did not stop here");
        }
        break;
      case _URC_INSTALL_CONTEXT:
        _LIBUNWIND_TRACE_UNWINDING(
            "unwind_phase2(ex_ojb=%p): _URC_INSTALL_CONTEXT",
            (void *)exception_object);
        // Personality routine says to transfer control to landing pad.
        // We may get control back if landing pad calls _Unwind_Resume().
        if (_LIBUNWIND_TRACING_UNWINDING) {
          unw_word_t pc;
          unw_get_reg(cursor, UNW_REG_IP, &pc);
          unw_get_reg(cursor, UNW_REG_SP, &sp);
          _LIBUNWIND_TRACE_UNWINDING("unwind_phase2(ex_ojb=%p): re-entering "
                                     "user code with ip=0x%" PRIxPTR
                                     ", sp=0x%" PRIxPTR,
                                     (void *)exception_object, pc, sp);
        }
        unw_resume(cursor);
        // unw_resume() only returns if there was an error.
        return _URC_FATAL_PHASE2_ERROR;
      default:
        // Personality routine returned an unknown result code.
        _LIBUNWIND_DEBUG_LOG("personality function returned unknown result %d",
                             personalityResult);
        return _URC_FATAL_PHASE2_ERROR;
      }
    }
  }

  // Clean up phase did not resume at the frame that the search phase
  // said it would...
  return _URC_FATAL_PHASE2_ERROR;
}

static _Unwind_Reason_Code
unwind_phase2_forced(unw_context_t *uc, unw_cursor_t *cursor,
                     _Unwind_Exception *exception_object,
                     _Unwind_Stop_Fn stop, void *stop_parameter) {
  unw_init_local(cursor, uc);

  // Walk each frame until we reach where search phase said to stop
  while (unw_step(cursor) > 0) {

    // Update info about this frame.
    unw_proc_info_t frameInfo;
    if (unw_get_proc_info(cursor, &frameInfo) != UNW_ESUCCESS) {
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
      if ((unw_get_proc_name(cursor, functionBuf, sizeof(functionBuf),
                             &offset) != UNW_ESUCCESS) ||
          (frameInfo.start_ip + offset > frameInfo.end_ip))
        functionName = ".anonymous.";
      _LIBUNWIND_TRACE_UNWINDING(
          "unwind_phase2_forced(ex_ojb=%p): start_ip=0x%" PRIxPTR
          ", func=%s, lsda=0x%" PRIxPTR ", personality=0x%" PRIxPTR,
          (void *)exception_object, frameInfo.start_ip, functionName,
          frameInfo.lsda, frameInfo.handler);
    }

    // Call stop function at each frame.
    _Unwind_Action action =
        (_Unwind_Action)(_UA_FORCE_UNWIND | _UA_CLEANUP_PHASE);
    _Unwind_Reason_Code stopResult =
        (*stop)(1, action, exception_object->exception_class, exception_object,
                (struct _Unwind_Context *)(cursor), stop_parameter);
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
               (struct _Unwind_Context *)(cursor));
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
        unw_resume(cursor);
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
          (struct _Unwind_Context *)(cursor), stop_parameter);

  // Clean up phase did not resume at the frame that the search phase said it
  // would.
  return _URC_FATAL_PHASE2_ERROR;
}


/// Called by __cxa_throw.  Only returns if there is a fatal error.
_LIBUNWIND_EXPORT _Unwind_Reason_Code
_Unwind_RaiseException(_Unwind_Exception *exception_object) {
  _LIBUNWIND_TRACE_API("_Unwind_RaiseException(ex_obj=%p)",
                       (void *)exception_object);
  unw_context_t uc;
  unw_cursor_t cursor;
  unw_getcontext(&uc);

  // Mark that this is a non-forced unwind, so _Unwind_Resume()
  // can do the right thing.
  exception_object->private_1 = 0;
  exception_object->private_2 = 0;

  // phase 1: the search phase
  _Unwind_Reason_Code phase1 = unwind_phase1(&uc, &cursor, exception_object);
  if (phase1 != _URC_NO_REASON)
    return phase1;

  // phase 2: the clean up phase
  return unwind_phase2(&uc, &cursor, exception_object);
}



/// When _Unwind_RaiseException() is in phase2, it hands control
/// to the personality function at each frame.  The personality
/// may force a jump to a landing pad in that function, the landing
/// pad code may then call _Unwind_Resume() to continue with the
/// unwinding.  Note: the call to _Unwind_Resume() is from compiler
/// geneated user code.  All other _Unwind_* routines are called
/// by the C++ runtime __cxa_* routines.
///
/// Note: re-throwing an exception (as opposed to continuing the unwind)
/// is implemented by having the code call __cxa_rethrow() which
/// in turn calls _Unwind_Resume_or_Rethrow().
_LIBUNWIND_EXPORT void
_Unwind_Resume(_Unwind_Exception *exception_object) {
  _LIBUNWIND_TRACE_API("_Unwind_Resume(ex_obj=%p)", (void *)exception_object);
  unw_context_t uc;
  unw_cursor_t cursor;
  unw_getcontext(&uc);

  if (exception_object->private_1 != 0)
    unwind_phase2_forced(&uc, &cursor, exception_object,
                         (_Unwind_Stop_Fn) exception_object->private_1,
                         (void *)exception_object->private_2);
  else
    unwind_phase2(&uc, &cursor, exception_object);

  // Clients assume _Unwind_Resume() does not return, so all we can do is abort.
  _LIBUNWIND_ABORT("_Unwind_Resume() can't return");
}



/// Not used by C++.
/// Unwinds stack, calling "stop" function at each frame.
/// Could be used to implement longjmp().
_LIBUNWIND_EXPORT _Unwind_Reason_Code
_Unwind_ForcedUnwind(_Unwind_Exception *exception_object,
                     _Unwind_Stop_Fn stop, void *stop_parameter) {
  _LIBUNWIND_TRACE_API("_Unwind_ForcedUnwind(ex_obj=%p, stop=%p)",
                       (void *)exception_object, (void *)(uintptr_t)stop);
  unw_context_t uc;
  unw_cursor_t cursor;
  unw_getcontext(&uc);

  // Mark that this is a forced unwind, so _Unwind_Resume() can do
  // the right thing.
  exception_object->private_1 = (uintptr_t) stop;
  exception_object->private_2 = (uintptr_t) stop_parameter;

  // do it
  return unwind_phase2_forced(&uc, &cursor, exception_object, stop, stop_parameter);
}


/// Called by personality handler during phase 2 to get LSDA for current frame.
_LIBUNWIND_EXPORT uintptr_t
_Unwind_GetLanguageSpecificData(struct _Unwind_Context *context) {
  unw_cursor_t *cursor = (unw_cursor_t *)context;
  unw_proc_info_t frameInfo;
  uintptr_t result = 0;
  if (unw_get_proc_info(cursor, &frameInfo) == UNW_ESUCCESS)
    result = (uintptr_t)frameInfo.lsda;
  _LIBUNWIND_TRACE_API(
      "_Unwind_GetLanguageSpecificData(context=%p) => 0x%" PRIxPTR,
      (void *)context, result);
  if (result != 0) {
    if (*((uint8_t *)result) != 0xFF)
      _LIBUNWIND_DEBUG_LOG("lsda at 0x%" PRIxPTR " does not start with 0xFF",
                           result);
  }
  return result;
}


/// Called by personality handler during phase 2 to find the start of the
/// function.
_LIBUNWIND_EXPORT uintptr_t
_Unwind_GetRegionStart(struct _Unwind_Context *context) {
  unw_cursor_t *cursor = (unw_cursor_t *)context;
  unw_proc_info_t frameInfo;
  uintptr_t result = 0;
  if (unw_get_proc_info(cursor, &frameInfo) == UNW_ESUCCESS)
    result = (uintptr_t)frameInfo.start_ip;
  _LIBUNWIND_TRACE_API("_Unwind_GetRegionStart(context=%p) => 0x%" PRIxPTR,
                       (void *)context, result);
  return result;
}

#endif // !_LIBUNWIND_SUPPORT_SEH_UNWIND

/// Called by personality handler during phase 2 if a foreign exception
// is caught.
_LIBUNWIND_EXPORT void
_Unwind_DeleteException(_Unwind_Exception *exception_object) {
  _LIBUNWIND_TRACE_API("_Unwind_DeleteException(ex_obj=%p)",
                       (void *)exception_object);
  if (exception_object->exception_cleanup != NULL)
    (*exception_object->exception_cleanup)(_URC_FOREIGN_EXCEPTION_CAUGHT,
                                           exception_object);
}

/// Called by personality handler during phase 2 to get register values.
_LIBUNWIND_EXPORT uintptr_t
_Unwind_GetGR(struct _Unwind_Context *context, int index) {
  unw_cursor_t *cursor = (unw_cursor_t *)context;
  unw_word_t result;
  unw_get_reg(cursor, index, &result);
  _LIBUNWIND_TRACE_API("_Unwind_GetGR(context=%p, reg=%d) => 0x%" PRIxPTR,
                       (void *)context, index, result);
  return (uintptr_t)result;
}

/// Called by personality handler during phase 2 to alter register values.
_LIBUNWIND_EXPORT void _Unwind_SetGR(struct _Unwind_Context *context, int index,
                                     uintptr_t value) {
  _LIBUNWIND_TRACE_API("_Unwind_SetGR(context=%p, reg=%d, value=0x%0" PRIxPTR
                       ")",
                       (void *)context, index, value);
  unw_cursor_t *cursor = (unw_cursor_t *)context;
  unw_set_reg(cursor, index, value);
}

/// Called by personality handler during phase 2 to get instruction pointer.
_LIBUNWIND_EXPORT uintptr_t _Unwind_GetIP(struct _Unwind_Context *context) {
  unw_cursor_t *cursor = (unw_cursor_t *)context;
  unw_word_t result;
  unw_get_reg(cursor, UNW_REG_IP, &result);
  _LIBUNWIND_TRACE_API("_Unwind_GetIP(context=%p) => 0x%" PRIxPTR,
                       (void *)context, result);
  return (uintptr_t)result;
}

/// Called by personality handler during phase 2 to alter instruction pointer,
/// such as setting where the landing pad is, so _Unwind_Resume() will
/// start executing in the landing pad.
_LIBUNWIND_EXPORT void _Unwind_SetIP(struct _Unwind_Context *context,
                                     uintptr_t value) {
  _LIBUNWIND_TRACE_API("_Unwind_SetIP(context=%p, value=0x%0" PRIxPTR ")",
                       (void *)context, value);
  unw_cursor_t *cursor = (unw_cursor_t *)context;
  unw_set_reg(cursor, UNW_REG_IP, value);
}

#endif // !defined(_LIBUNWIND_ARM_EHABI) && !defined(__USING_SJLJ_EXCEPTIONS__)
