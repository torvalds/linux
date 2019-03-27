//===--------------------------- Unwind-EHABI.cpp -------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//
//  Implements ARM zero-cost C++ exceptions
//
//===----------------------------------------------------------------------===//

#include "Unwind-EHABI.h"

#if defined(_LIBUNWIND_ARM_EHABI)

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <type_traits>

#include "config.h"
#include "libunwind.h"
#include "libunwind_ext.h"
#include "unwind.h"

namespace {

// Strange order: take words in order, but inside word, take from most to least
// signinficant byte.
uint8_t getByte(const uint32_t* data, size_t offset) {
  const uint8_t* byteData = reinterpret_cast<const uint8_t*>(data);
  return byteData[(offset & ~(size_t)0x03) + (3 - (offset & (size_t)0x03))];
}

const char* getNextWord(const char* data, uint32_t* out) {
  *out = *reinterpret_cast<const uint32_t*>(data);
  return data + 4;
}

const char* getNextNibble(const char* data, uint32_t* out) {
  *out = *reinterpret_cast<const uint16_t*>(data);
  return data + 2;
}

struct Descriptor {
  // See # 9.2
  typedef enum {
    SU16 = 0, // Short descriptor, 16-bit entries
    LU16 = 1, // Long descriptor,  16-bit entries
    LU32 = 3, // Long descriptor,  32-bit entries
    RESERVED0 =  4, RESERVED1 =  5, RESERVED2  = 6,  RESERVED3  =  7,
    RESERVED4 =  8, RESERVED5 =  9, RESERVED6  = 10, RESERVED7  = 11,
    RESERVED8 = 12, RESERVED9 = 13, RESERVED10 = 14, RESERVED11 = 15
  } Format;

  // See # 9.2
  typedef enum {
    CLEANUP = 0x0,
    FUNC    = 0x1,
    CATCH   = 0x2,
    INVALID = 0x4
  } Kind;
};

_Unwind_Reason_Code ProcessDescriptors(
    _Unwind_State state,
    _Unwind_Control_Block* ucbp,
    struct _Unwind_Context* context,
    Descriptor::Format format,
    const char* descriptorStart,
    uint32_t flags) {

  // EHT is inlined in the index using compact form. No descriptors. #5
  if (flags & 0x1)
    return _URC_CONTINUE_UNWIND;

  // TODO: We should check the state here, and determine whether we need to
  // perform phase1 or phase2 unwinding.
  (void)state;

  const char* descriptor = descriptorStart;
  uint32_t descriptorWord;
  getNextWord(descriptor, &descriptorWord);
  while (descriptorWord) {
    // Read descriptor based on # 9.2.
    uint32_t length;
    uint32_t offset;
    switch (format) {
      case Descriptor::LU32:
        descriptor = getNextWord(descriptor, &length);
        descriptor = getNextWord(descriptor, &offset);
      case Descriptor::LU16:
        descriptor = getNextNibble(descriptor, &length);
        descriptor = getNextNibble(descriptor, &offset);
      default:
        assert(false);
        return _URC_FAILURE;
    }

    // See # 9.2 table for decoding the kind of descriptor. It's a 2-bit value.
    Descriptor::Kind kind =
        static_cast<Descriptor::Kind>((length & 0x1) | ((offset & 0x1) << 1));

    // Clear off flag from last bit.
    length &= ~1u;
    offset &= ~1u;
    uintptr_t scopeStart = ucbp->pr_cache.fnstart + offset;
    uintptr_t scopeEnd = scopeStart + length;
    uintptr_t pc = _Unwind_GetIP(context);
    bool isInScope = (scopeStart <= pc) && (pc < scopeEnd);

    switch (kind) {
      case Descriptor::CLEANUP: {
        // TODO(ajwong): Handle cleanup descriptors.
        break;
      }
      case Descriptor::FUNC: {
        // TODO(ajwong): Handle function descriptors.
        break;
      }
      case Descriptor::CATCH: {
        // Catch descriptors require gobbling one more word.
        uint32_t landing_pad;
        descriptor = getNextWord(descriptor, &landing_pad);

        if (isInScope) {
          // TODO(ajwong): This is only phase1 compatible logic. Implement
          // phase2.
          landing_pad = signExtendPrel31(landing_pad & ~0x80000000);
          if (landing_pad == 0xffffffff) {
            return _URC_HANDLER_FOUND;
          } else if (landing_pad == 0xfffffffe) {
            return _URC_FAILURE;
          } else {
            /*
            bool is_reference_type = landing_pad & 0x80000000;
            void* matched_object;
            if (__cxxabiv1::__cxa_type_match(
                    ucbp, reinterpret_cast<const std::type_info *>(landing_pad),
                    is_reference_type,
                    &matched_object) != __cxxabiv1::ctm_failed)
                return _URC_HANDLER_FOUND;
                */
            _LIBUNWIND_ABORT("Type matching not implemented");
          }
        }
        break;
      }
      default:
        _LIBUNWIND_ABORT("Invalid descriptor kind found.");
    }

    getNextWord(descriptor, &descriptorWord);
  }

  return _URC_CONTINUE_UNWIND;
}

static _Unwind_Reason_Code unwindOneFrame(_Unwind_State state,
                                          _Unwind_Control_Block* ucbp,
                                          struct _Unwind_Context* context) {
  // Read the compact model EHT entry's header # 6.3
  const uint32_t* unwindingData = ucbp->pr_cache.ehtp;
  assert((*unwindingData & 0xf0000000) == 0x80000000 && "Must be a compact entry");
  Descriptor::Format format =
      static_cast<Descriptor::Format>((*unwindingData & 0x0f000000) >> 24);

  const char *lsda =
      reinterpret_cast<const char *>(_Unwind_GetLanguageSpecificData(context));

  // Handle descriptors before unwinding so they are processed in the context
  // of the correct stack frame.
  _Unwind_Reason_Code result =
      ProcessDescriptors(state, ucbp, context, format, lsda,
                         ucbp->pr_cache.additional);

  if (result != _URC_CONTINUE_UNWIND)
    return result;

  if (unw_step(reinterpret_cast<unw_cursor_t*>(context)) != UNW_STEP_SUCCESS)
    return _URC_FAILURE;
  return _URC_CONTINUE_UNWIND;
}

// Generates mask discriminator for _Unwind_VRS_Pop, e.g. for _UVRSC_CORE /
// _UVRSD_UINT32.
uint32_t RegisterMask(uint8_t start, uint8_t count_minus_one) {
  return ((1U << (count_minus_one + 1)) - 1) << start;
}

// Generates mask discriminator for _Unwind_VRS_Pop, e.g. for _UVRSC_VFP /
// _UVRSD_DOUBLE.
uint32_t RegisterRange(uint8_t start, uint8_t count_minus_one) {
  return ((uint32_t)start << 16) | ((uint32_t)count_minus_one + 1);
}

} // end anonymous namespace

/**
 * Decodes an EHT entry.
 *
 * @param data Pointer to EHT.
 * @param[out] off Offset from return value (in bytes) to begin interpretation.
 * @param[out] len Number of bytes in unwind code.
 * @return Pointer to beginning of unwind code.
 */
extern "C" const uint32_t*
decode_eht_entry(const uint32_t* data, size_t* off, size_t* len) {
  if ((*data & 0x80000000) == 0) {
    // 6.2: Generic Model
    //
    // EHT entry is a prel31 pointing to the PR, followed by data understood
    // only by the personality routine. Fortunately, all existing assembler
    // implementations, including GNU assembler, LLVM integrated assembler,
    // and ARM assembler, assume that the unwind opcodes come after the
    // personality rountine address.
    *off = 1; // First byte is size data.
    *len = (((data[1] >> 24) & 0xff) + 1) * 4;
    data++; // Skip the first word, which is the prel31 offset.
  } else {
    // 6.3: ARM Compact Model
    //
    // EHT entries here correspond to the __aeabi_unwind_cpp_pr[012] PRs indeded
    // by format:
    Descriptor::Format format =
        static_cast<Descriptor::Format>((*data & 0x0f000000) >> 24);
    switch (format) {
      case Descriptor::SU16:
        *len = 4;
        *off = 1;
        break;
      case Descriptor::LU16:
      case Descriptor::LU32:
        *len = 4 + 4 * ((*data & 0x00ff0000) >> 16);
        *off = 2;
        break;
      default:
        return nullptr;
    }
  }
  return data;
}

_LIBUNWIND_EXPORT _Unwind_Reason_Code
_Unwind_VRS_Interpret(_Unwind_Context *context, const uint32_t *data,
                      size_t offset, size_t len) {
  bool wrotePC = false;
  bool finish = false;
  while (offset < len && !finish) {
    uint8_t byte = getByte(data, offset++);
    if ((byte & 0x80) == 0) {
      uint32_t sp;
      _Unwind_VRS_Get(context, _UVRSC_CORE, UNW_ARM_SP, _UVRSD_UINT32, &sp);
      if (byte & 0x40)
        sp -= (((uint32_t)byte & 0x3f) << 2) + 4;
      else
        sp += ((uint32_t)byte << 2) + 4;
      _Unwind_VRS_Set(context, _UVRSC_CORE, UNW_ARM_SP, _UVRSD_UINT32, &sp);
    } else {
      switch (byte & 0xf0) {
        case 0x80: {
          if (offset >= len)
            return _URC_FAILURE;
          uint32_t registers =
              (((uint32_t)byte & 0x0f) << 12) |
              (((uint32_t)getByte(data, offset++)) << 4);
          if (!registers)
            return _URC_FAILURE;
          if (registers & (1 << 15))
            wrotePC = true;
          _Unwind_VRS_Pop(context, _UVRSC_CORE, registers, _UVRSD_UINT32);
          break;
        }
        case 0x90: {
          uint8_t reg = byte & 0x0f;
          if (reg == 13 || reg == 15)
            return _URC_FAILURE;
          uint32_t sp;
          _Unwind_VRS_Get(context, _UVRSC_CORE, UNW_ARM_R0 + reg,
                          _UVRSD_UINT32, &sp);
          _Unwind_VRS_Set(context, _UVRSC_CORE, UNW_ARM_SP, _UVRSD_UINT32,
                          &sp);
          break;
        }
        case 0xa0: {
          uint32_t registers = RegisterMask(4, byte & 0x07);
          if (byte & 0x08)
            registers |= 1 << 14;
          _Unwind_VRS_Pop(context, _UVRSC_CORE, registers, _UVRSD_UINT32);
          break;
        }
        case 0xb0: {
          switch (byte) {
            case 0xb0:
              finish = true;
              break;
            case 0xb1: {
              if (offset >= len)
                return _URC_FAILURE;
              uint8_t registers = getByte(data, offset++);
              if (registers & 0xf0 || !registers)
                return _URC_FAILURE;
              _Unwind_VRS_Pop(context, _UVRSC_CORE, registers, _UVRSD_UINT32);
              break;
            }
            case 0xb2: {
              uint32_t addend = 0;
              uint32_t shift = 0;
              // This decodes a uleb128 value.
              while (true) {
                if (offset >= len)
                  return _URC_FAILURE;
                uint32_t v = getByte(data, offset++);
                addend |= (v & 0x7f) << shift;
                if ((v & 0x80) == 0)
                  break;
                shift += 7;
              }
              uint32_t sp;
              _Unwind_VRS_Get(context, _UVRSC_CORE, UNW_ARM_SP, _UVRSD_UINT32,
                              &sp);
              sp += 0x204 + (addend << 2);
              _Unwind_VRS_Set(context, _UVRSC_CORE, UNW_ARM_SP, _UVRSD_UINT32,
                              &sp);
              break;
            }
            case 0xb3: {
              uint8_t v = getByte(data, offset++);
              _Unwind_VRS_Pop(context, _UVRSC_VFP,
                              RegisterRange(static_cast<uint8_t>(v >> 4),
                                            v & 0x0f), _UVRSD_VFPX);
              break;
            }
            case 0xb4:
            case 0xb5:
            case 0xb6:
            case 0xb7:
              return _URC_FAILURE;
            default:
              _Unwind_VRS_Pop(context, _UVRSC_VFP,
                              RegisterRange(8, byte & 0x07), _UVRSD_VFPX);
              break;
          }
          break;
        }
        case 0xc0: {
          switch (byte) {
#if defined(__ARM_WMMX)
            case 0xc0:
            case 0xc1:
            case 0xc2:
            case 0xc3:
            case 0xc4:
            case 0xc5:
              _Unwind_VRS_Pop(context, _UVRSC_WMMXD,
                              RegisterRange(10, byte & 0x7), _UVRSD_DOUBLE);
              break;
            case 0xc6: {
              uint8_t v = getByte(data, offset++);
              uint8_t start = static_cast<uint8_t>(v >> 4);
              uint8_t count_minus_one = v & 0xf;
              if (start + count_minus_one >= 16)
                return _URC_FAILURE;
              _Unwind_VRS_Pop(context, _UVRSC_WMMXD,
                              RegisterRange(start, count_minus_one),
                              _UVRSD_DOUBLE);
              break;
            }
            case 0xc7: {
              uint8_t v = getByte(data, offset++);
              if (!v || v & 0xf0)
                return _URC_FAILURE;
              _Unwind_VRS_Pop(context, _UVRSC_WMMXC, v, _UVRSD_DOUBLE);
              break;
            }
#endif
            case 0xc8:
            case 0xc9: {
              uint8_t v = getByte(data, offset++);
              uint8_t start =
                  static_cast<uint8_t>(((byte == 0xc8) ? 16 : 0) + (v >> 4));
              uint8_t count_minus_one = v & 0xf;
              if (start + count_minus_one >= 32)
                return _URC_FAILURE;
              _Unwind_VRS_Pop(context, _UVRSC_VFP,
                              RegisterRange(start, count_minus_one),
                              _UVRSD_DOUBLE);
              break;
            }
            default:
              return _URC_FAILURE;
          }
          break;
        }
        case 0xd0: {
          if (byte & 0x08)
            return _URC_FAILURE;
          _Unwind_VRS_Pop(context, _UVRSC_VFP, RegisterRange(8, byte & 0x7),
                          _UVRSD_DOUBLE);
          break;
        }
        default:
          return _URC_FAILURE;
      }
    }
  }
  if (!wrotePC) {
    uint32_t lr;
    _Unwind_VRS_Get(context, _UVRSC_CORE, UNW_ARM_LR, _UVRSD_UINT32, &lr);
    _Unwind_VRS_Set(context, _UVRSC_CORE, UNW_ARM_IP, _UVRSD_UINT32, &lr);
  }
  return _URC_CONTINUE_UNWIND;
}

extern "C" _LIBUNWIND_EXPORT _Unwind_Reason_Code
__aeabi_unwind_cpp_pr0(_Unwind_State state, _Unwind_Control_Block *ucbp,
                       _Unwind_Context *context) {
  return unwindOneFrame(state, ucbp, context);
}

extern "C" _LIBUNWIND_EXPORT _Unwind_Reason_Code
__aeabi_unwind_cpp_pr1(_Unwind_State state, _Unwind_Control_Block *ucbp,
                       _Unwind_Context *context) {
  return unwindOneFrame(state, ucbp, context);
}

extern "C" _LIBUNWIND_EXPORT _Unwind_Reason_Code
__aeabi_unwind_cpp_pr2(_Unwind_State state, _Unwind_Control_Block *ucbp,
                       _Unwind_Context *context) {
  return unwindOneFrame(state, ucbp, context);
}

static _Unwind_Reason_Code
unwind_phase1(unw_context_t *uc, unw_cursor_t *cursor, _Unwind_Exception *exception_object) {
  // EHABI #7.3 discusses preserving the VRS in a "temporary VRS" during
  // phase 1 and then restoring it to the "primary VRS" for phase 2. The
  // effect is phase 2 doesn't see any of the VRS manipulations from phase 1.
  // In this implementation, the phases don't share the VRS backing store.
  // Instead, they are passed the original |uc| and they create a new VRS
  // from scratch thus achieving the same effect.
  unw_init_local(cursor, uc);

  // Walk each frame looking for a place to stop.
  for (bool handlerNotFound = true; handlerNotFound;) {

    // See if frame has code to run (has personality routine).
    unw_proc_info_t frameInfo;
    if (unw_get_proc_info(cursor, &frameInfo) != UNW_ESUCCESS) {
      _LIBUNWIND_TRACE_UNWINDING("unwind_phase1(ex_ojb=%p): unw_get_proc_info "
                                 "failed => _URC_FATAL_PHASE1_ERROR",
                                 static_cast<void *>(exception_object));
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
          "unwind_phase1(ex_ojb=%p): pc=0x%" PRIxPTR ", start_ip=0x%" PRIxPTR ", func=%s, "
          "lsda=0x%" PRIxPTR ", personality=0x%" PRIxPTR,
          static_cast<void *>(exception_object), pc,
          frameInfo.start_ip, functionName,
          frameInfo.lsda, frameInfo.handler);
    }

    // If there is a personality routine, ask it if it will want to stop at
    // this frame.
    if (frameInfo.handler != 0) {
      __personality_routine p =
          (__personality_routine)(long)(frameInfo.handler);
      _LIBUNWIND_TRACE_UNWINDING(
          "unwind_phase1(ex_ojb=%p): calling personality function %p",
          static_cast<void *>(exception_object),
          reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(p)));
      struct _Unwind_Context *context = (struct _Unwind_Context *)(cursor);
      exception_object->pr_cache.fnstart = frameInfo.start_ip;
      exception_object->pr_cache.ehtp =
          (_Unwind_EHT_Header *)frameInfo.unwind_info;
      exception_object->pr_cache.additional = frameInfo.flags;
      _Unwind_Reason_Code personalityResult =
          (*p)(_US_VIRTUAL_UNWIND_FRAME, exception_object, context);
      _LIBUNWIND_TRACE_UNWINDING(
          "unwind_phase1(ex_ojb=%p): personality result %d start_ip %x ehtp %p "
          "additional %x",
          static_cast<void *>(exception_object), personalityResult,
          exception_object->pr_cache.fnstart,
          static_cast<void *>(exception_object->pr_cache.ehtp),
          exception_object->pr_cache.additional);
      switch (personalityResult) {
      case _URC_HANDLER_FOUND:
        // found a catch clause or locals that need destructing in this frame
        // stop search and remember stack pointer at the frame
        handlerNotFound = false;
        // p should have initialized barrier_cache. EHABI #7.3.5
        _LIBUNWIND_TRACE_UNWINDING(
            "unwind_phase1(ex_ojb=%p): _URC_HANDLER_FOUND",
            static_cast<void *>(exception_object));
        return _URC_NO_REASON;

      case _URC_CONTINUE_UNWIND:
        _LIBUNWIND_TRACE_UNWINDING(
            "unwind_phase1(ex_ojb=%p): _URC_CONTINUE_UNWIND",
            static_cast<void *>(exception_object));
        // continue unwinding
        break;

      // EHABI #7.3.3
      case _URC_FAILURE:
        return _URC_FAILURE;

      default:
        // something went wrong
        _LIBUNWIND_TRACE_UNWINDING(
            "unwind_phase1(ex_ojb=%p): _URC_FATAL_PHASE1_ERROR",
            static_cast<void *>(exception_object));
        return _URC_FATAL_PHASE1_ERROR;
      }
    }
  }
  return _URC_NO_REASON;
}

static _Unwind_Reason_Code unwind_phase2(unw_context_t *uc, unw_cursor_t *cursor,
                                         _Unwind_Exception *exception_object,
                                         bool resume) {
  // See comment at the start of unwind_phase1 regarding VRS integrity.
  unw_init_local(cursor, uc);

  _LIBUNWIND_TRACE_UNWINDING("unwind_phase2(ex_ojb=%p)",
                             static_cast<void *>(exception_object));
  int frame_count = 0;

  // Walk each frame until we reach where search phase said to stop.
  while (true) {
    // Ask libunwind to get next frame (skip over first which is
    // _Unwind_RaiseException or _Unwind_Resume).
    //
    // Resume only ever makes sense for 1 frame.
    _Unwind_State state =
        resume ? _US_UNWIND_FRAME_RESUME : _US_UNWIND_FRAME_STARTING;
    if (resume && frame_count == 1) {
      // On a resume, first unwind the _Unwind_Resume() frame. The next frame
      // is now the landing pad for the cleanup from a previous execution of
      // phase2. To continue unwindingly correctly, replace VRS[15] with the
      // IP of the frame that the previous run of phase2 installed the context
      // for. After this, continue unwinding as if normal.
      //
      // See #7.4.6 for details.
      unw_set_reg(cursor, UNW_REG_IP,
                  exception_object->unwinder_cache.reserved2);
      resume = false;
    }

    // Get info about this frame.
    unw_word_t sp;
    unw_proc_info_t frameInfo;
    unw_get_reg(cursor, UNW_REG_SP, &sp);
    if (unw_get_proc_info(cursor, &frameInfo) != UNW_ESUCCESS) {
      _LIBUNWIND_TRACE_UNWINDING("unwind_phase2(ex_ojb=%p): unw_get_proc_info "
                                 "failed => _URC_FATAL_PHASE2_ERROR",
                                 static_cast<void *>(exception_object));
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
          "unwind_phase2(ex_ojb=%p): start_ip=0x%" PRIxPTR ", func=%s, sp=0x%" PRIxPTR ", "
          "lsda=0x%" PRIxPTR ", personality=0x%" PRIxPTR "",
          static_cast<void *>(exception_object), frameInfo.start_ip,
          functionName, sp, frameInfo.lsda,
          frameInfo.handler);
    }

    // If there is a personality routine, tell it we are unwinding.
    if (frameInfo.handler != 0) {
      __personality_routine p =
          (__personality_routine)(long)(frameInfo.handler);
      struct _Unwind_Context *context = (struct _Unwind_Context *)(cursor);
      // EHABI #7.2
      exception_object->pr_cache.fnstart = frameInfo.start_ip;
      exception_object->pr_cache.ehtp =
          (_Unwind_EHT_Header *)frameInfo.unwind_info;
      exception_object->pr_cache.additional = frameInfo.flags;
      _Unwind_Reason_Code personalityResult =
          (*p)(state, exception_object, context);
      switch (personalityResult) {
      case _URC_CONTINUE_UNWIND:
        // Continue unwinding
        _LIBUNWIND_TRACE_UNWINDING(
            "unwind_phase2(ex_ojb=%p): _URC_CONTINUE_UNWIND",
            static_cast<void *>(exception_object));
        // EHABI #7.2
        if (sp == exception_object->barrier_cache.sp) {
          // Phase 1 said we would stop at this frame, but we did not...
          _LIBUNWIND_ABORT("during phase1 personality function said it would "
                           "stop here, but now in phase2 it did not stop here");
        }
        break;
      case _URC_INSTALL_CONTEXT:
        _LIBUNWIND_TRACE_UNWINDING(
            "unwind_phase2(ex_ojb=%p): _URC_INSTALL_CONTEXT",
            static_cast<void *>(exception_object));
        // Personality routine says to transfer control to landing pad.
        // We may get control back if landing pad calls _Unwind_Resume().
        if (_LIBUNWIND_TRACING_UNWINDING) {
          unw_word_t pc;
          unw_get_reg(cursor, UNW_REG_IP, &pc);
          unw_get_reg(cursor, UNW_REG_SP, &sp);
          _LIBUNWIND_TRACE_UNWINDING("unwind_phase2(ex_ojb=%p): re-entering "
                                     "user code with ip=0x%" PRIxPTR ", sp=0x%" PRIxPTR,
                                     static_cast<void *>(exception_object),
                                     pc, sp);
        }

        {
          // EHABI #7.4.1 says we need to preserve pc for when _Unwind_Resume
          // is called back, to find this same frame.
          unw_word_t pc;
          unw_get_reg(cursor, UNW_REG_IP, &pc);
          exception_object->unwinder_cache.reserved2 = (uint32_t)pc;
        }
        unw_resume(cursor);
        // unw_resume() only returns if there was an error.
        return _URC_FATAL_PHASE2_ERROR;

      // # EHABI #7.4.3
      case _URC_FAILURE:
        abort();

      default:
        // Personality routine returned an unknown result code.
        _LIBUNWIND_DEBUG_LOG("personality function returned unknown result %d",
                      personalityResult);
        return _URC_FATAL_PHASE2_ERROR;
      }
    }
    frame_count++;
  }

  // Clean up phase did not resume at the frame that the search phase
  // said it would...
  return _URC_FATAL_PHASE2_ERROR;
}

/// Called by __cxa_throw.  Only returns if there is a fatal error.
_LIBUNWIND_EXPORT _Unwind_Reason_Code
_Unwind_RaiseException(_Unwind_Exception *exception_object) {
  _LIBUNWIND_TRACE_API("_Unwind_RaiseException(ex_obj=%p)",
                       static_cast<void *>(exception_object));
  unw_context_t uc;
  unw_cursor_t cursor;
  unw_getcontext(&uc);

  // This field for is for compatibility with GCC to say this isn't a forced
  // unwind. EHABI #7.2
  exception_object->unwinder_cache.reserved1 = 0;

  // phase 1: the search phase
  _Unwind_Reason_Code phase1 = unwind_phase1(&uc, &cursor, exception_object);
  if (phase1 != _URC_NO_REASON)
    return phase1;

  // phase 2: the clean up phase
  return unwind_phase2(&uc, &cursor, exception_object, false);
}

_LIBUNWIND_EXPORT void _Unwind_Complete(_Unwind_Exception* exception_object) {
  // This is to be called when exception handling completes to give us a chance
  // to perform any housekeeping. EHABI #7.2. But we have nothing to do here.
  (void)exception_object;
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
  _LIBUNWIND_TRACE_API("_Unwind_Resume(ex_obj=%p)",
                       static_cast<void *>(exception_object));
  unw_context_t uc;
  unw_cursor_t cursor;
  unw_getcontext(&uc);

  // _Unwind_RaiseException on EHABI will always set the reserved1 field to 0,
  // which is in the same position as private_1 below.
  // TODO(ajwong): Who wronte the above? Why is it true?
  unwind_phase2(&uc, &cursor, exception_object, true);

  // Clients assume _Unwind_Resume() does not return, so all we can do is abort.
  _LIBUNWIND_ABORT("_Unwind_Resume() can't return");
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
      "_Unwind_GetLanguageSpecificData(context=%p) => 0x%llx",
      static_cast<void *>(context), (long long)result);
  return result;
}

static uint64_t ValueAsBitPattern(_Unwind_VRS_DataRepresentation representation,
                                  void* valuep) {
  uint64_t value = 0;
  switch (representation) {
    case _UVRSD_UINT32:
    case _UVRSD_FLOAT:
      memcpy(&value, valuep, sizeof(uint32_t));
      break;

    case _UVRSD_VFPX:
    case _UVRSD_UINT64:
    case _UVRSD_DOUBLE:
      memcpy(&value, valuep, sizeof(uint64_t));
      break;
  }
  return value;
}

_LIBUNWIND_EXPORT _Unwind_VRS_Result
_Unwind_VRS_Set(_Unwind_Context *context, _Unwind_VRS_RegClass regclass,
                uint32_t regno, _Unwind_VRS_DataRepresentation representation,
                void *valuep) {
  _LIBUNWIND_TRACE_API("_Unwind_VRS_Set(context=%p, regclass=%d, reg=%d, "
                       "rep=%d, value=0x%llX)",
                       static_cast<void *>(context), regclass, regno,
                       representation,
                       ValueAsBitPattern(representation, valuep));
  unw_cursor_t *cursor = (unw_cursor_t *)context;
  switch (regclass) {
    case _UVRSC_CORE:
      if (representation != _UVRSD_UINT32 || regno > 15)
        return _UVRSR_FAILED;
      return unw_set_reg(cursor, (unw_regnum_t)(UNW_ARM_R0 + regno),
                         *(unw_word_t *)valuep) == UNW_ESUCCESS
                 ? _UVRSR_OK
                 : _UVRSR_FAILED;
    case _UVRSC_VFP:
      if (representation != _UVRSD_VFPX && representation != _UVRSD_DOUBLE)
        return _UVRSR_FAILED;
      if (representation == _UVRSD_VFPX) {
        // Can only touch d0-15 with FSTMFDX.
        if (regno > 15)
          return _UVRSR_FAILED;
        unw_save_vfp_as_X(cursor);
      } else {
        if (regno > 31)
          return _UVRSR_FAILED;
      }
      return unw_set_fpreg(cursor, (unw_regnum_t)(UNW_ARM_D0 + regno),
                           *(unw_fpreg_t *)valuep) == UNW_ESUCCESS
                 ? _UVRSR_OK
                 : _UVRSR_FAILED;
#if defined(__ARM_WMMX)
    case _UVRSC_WMMXC:
      if (representation != _UVRSD_UINT32 || regno > 3)
        return _UVRSR_FAILED;
      return unw_set_reg(cursor, (unw_regnum_t)(UNW_ARM_WC0 + regno),
                         *(unw_word_t *)valuep) == UNW_ESUCCESS
                 ? _UVRSR_OK
                 : _UVRSR_FAILED;
    case _UVRSC_WMMXD:
      if (representation != _UVRSD_DOUBLE || regno > 31)
        return _UVRSR_FAILED;
      return unw_set_fpreg(cursor, (unw_regnum_t)(UNW_ARM_WR0 + regno),
                           *(unw_fpreg_t *)valuep) == UNW_ESUCCESS
                 ? _UVRSR_OK
                 : _UVRSR_FAILED;
#else
    case _UVRSC_WMMXC:
    case _UVRSC_WMMXD:
      break;
#endif
  }
  _LIBUNWIND_ABORT("unsupported register class");
}

static _Unwind_VRS_Result
_Unwind_VRS_Get_Internal(_Unwind_Context *context,
                         _Unwind_VRS_RegClass regclass, uint32_t regno,
                         _Unwind_VRS_DataRepresentation representation,
                         void *valuep) {
  unw_cursor_t *cursor = (unw_cursor_t *)context;
  switch (regclass) {
    case _UVRSC_CORE:
      if (representation != _UVRSD_UINT32 || regno > 15)
        return _UVRSR_FAILED;
      return unw_get_reg(cursor, (unw_regnum_t)(UNW_ARM_R0 + regno),
                         (unw_word_t *)valuep) == UNW_ESUCCESS
                 ? _UVRSR_OK
                 : _UVRSR_FAILED;
    case _UVRSC_VFP:
      if (representation != _UVRSD_VFPX && representation != _UVRSD_DOUBLE)
        return _UVRSR_FAILED;
      if (representation == _UVRSD_VFPX) {
        // Can only touch d0-15 with FSTMFDX.
        if (regno > 15)
          return _UVRSR_FAILED;
        unw_save_vfp_as_X(cursor);
      } else {
        if (regno > 31)
          return _UVRSR_FAILED;
      }
      return unw_get_fpreg(cursor, (unw_regnum_t)(UNW_ARM_D0 + regno),
                           (unw_fpreg_t *)valuep) == UNW_ESUCCESS
                 ? _UVRSR_OK
                 : _UVRSR_FAILED;
#if defined(__ARM_WMMX)
    case _UVRSC_WMMXC:
      if (representation != _UVRSD_UINT32 || regno > 3)
        return _UVRSR_FAILED;
      return unw_get_reg(cursor, (unw_regnum_t)(UNW_ARM_WC0 + regno),
                         (unw_word_t *)valuep) == UNW_ESUCCESS
                 ? _UVRSR_OK
                 : _UVRSR_FAILED;
    case _UVRSC_WMMXD:
      if (representation != _UVRSD_DOUBLE || regno > 31)
        return _UVRSR_FAILED;
      return unw_get_fpreg(cursor, (unw_regnum_t)(UNW_ARM_WR0 + regno),
                           (unw_fpreg_t *)valuep) == UNW_ESUCCESS
                 ? _UVRSR_OK
                 : _UVRSR_FAILED;
#else
    case _UVRSC_WMMXC:
    case _UVRSC_WMMXD:
      break;
#endif
  }
  _LIBUNWIND_ABORT("unsupported register class");
}

_LIBUNWIND_EXPORT _Unwind_VRS_Result
_Unwind_VRS_Get(_Unwind_Context *context, _Unwind_VRS_RegClass regclass,
                uint32_t regno, _Unwind_VRS_DataRepresentation representation,
                void *valuep) {
  _Unwind_VRS_Result result =
      _Unwind_VRS_Get_Internal(context, regclass, regno, representation,
                               valuep);
  _LIBUNWIND_TRACE_API("_Unwind_VRS_Get(context=%p, regclass=%d, reg=%d, "
                       "rep=%d, value=0x%llX, result = %d)",
                       static_cast<void *>(context), regclass, regno,
                       representation,
                       ValueAsBitPattern(representation, valuep), result);
  return result;
}

_Unwind_VRS_Result
_Unwind_VRS_Pop(_Unwind_Context *context, _Unwind_VRS_RegClass regclass,
                uint32_t discriminator,
                _Unwind_VRS_DataRepresentation representation) {
  _LIBUNWIND_TRACE_API("_Unwind_VRS_Pop(context=%p, regclass=%d, "
                       "discriminator=%d, representation=%d)",
                       static_cast<void *>(context), regclass, discriminator,
                       representation);
  switch (regclass) {
    case _UVRSC_WMMXC:
#if !defined(__ARM_WMMX)
      break;
#endif
    case _UVRSC_CORE: {
      if (representation != _UVRSD_UINT32)
        return _UVRSR_FAILED;
      // When popping SP from the stack, we don't want to override it from the
      // computed new stack location. See EHABI #7.5.4 table 3.
      bool poppedSP = false;
      uint32_t* sp;
      if (_Unwind_VRS_Get(context, _UVRSC_CORE, UNW_ARM_SP,
                          _UVRSD_UINT32, &sp) != _UVRSR_OK) {
        return _UVRSR_FAILED;
      }
      for (uint32_t i = 0; i < 16; ++i) {
        if (!(discriminator & static_cast<uint32_t>(1 << i)))
          continue;
        uint32_t value = *sp++;
        if (regclass == _UVRSC_CORE && i == 13)
          poppedSP = true;
        if (_Unwind_VRS_Set(context, regclass, i,
                            _UVRSD_UINT32, &value) != _UVRSR_OK) {
          return _UVRSR_FAILED;
        }
      }
      if (!poppedSP) {
        return _Unwind_VRS_Set(context, _UVRSC_CORE, UNW_ARM_SP,
                               _UVRSD_UINT32, &sp);
      }
      return _UVRSR_OK;
    }
    case _UVRSC_WMMXD:
#if !defined(__ARM_WMMX)
      break;
#endif
    case _UVRSC_VFP: {
      if (representation != _UVRSD_VFPX && representation != _UVRSD_DOUBLE)
        return _UVRSR_FAILED;
      uint32_t first = discriminator >> 16;
      uint32_t count = discriminator & 0xffff;
      uint32_t end = first+count;
      uint32_t* sp;
      if (_Unwind_VRS_Get(context, _UVRSC_CORE, UNW_ARM_SP,
                          _UVRSD_UINT32, &sp) != _UVRSR_OK) {
        return _UVRSR_FAILED;
      }
      // For _UVRSD_VFPX, we're assuming the data is stored in FSTMX "standard
      // format 1", which is equivalent to FSTMD + a padding word.
      for (uint32_t i = first; i < end; ++i) {
        // SP is only 32-bit aligned so don't copy 64-bit at a time.
        uint64_t value = *sp++;
        value |= ((uint64_t)(*sp++)) << 32;
        if (_Unwind_VRS_Set(context, regclass, i, representation, &value) !=
            _UVRSR_OK)
          return _UVRSR_FAILED;
      }
      if (representation == _UVRSD_VFPX)
        ++sp;
      return _Unwind_VRS_Set(context, _UVRSC_CORE, UNW_ARM_SP, _UVRSD_UINT32,
                             &sp);
    }
  }
  _LIBUNWIND_ABORT("unsupported register class");
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
  _LIBUNWIND_TRACE_API("_Unwind_GetRegionStart(context=%p) => 0x%llX",
                       static_cast<void *>(context), (long long)result);
  return result;
}


/// Called by personality handler during phase 2 if a foreign exception
// is caught.
_LIBUNWIND_EXPORT void
_Unwind_DeleteException(_Unwind_Exception *exception_object) {
  _LIBUNWIND_TRACE_API("_Unwind_DeleteException(ex_obj=%p)",
                       static_cast<void *>(exception_object));
  if (exception_object->exception_cleanup != NULL)
    (*exception_object->exception_cleanup)(_URC_FOREIGN_EXCEPTION_CAUGHT,
                                           exception_object);
}

extern "C" _LIBUNWIND_EXPORT _Unwind_Reason_Code
__gnu_unwind_frame(_Unwind_Exception *exception_object,
                   struct _Unwind_Context *context) {
  unw_cursor_t *cursor = (unw_cursor_t *)context;
  if (unw_step(cursor) != UNW_STEP_SUCCESS)
    return _URC_FAILURE;
  return _URC_OK;
}

#endif  // defined(_LIBUNWIND_ARM_EHABI)
