//===------------------------- Unwind-EHABI.hpp ---------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//
//===----------------------------------------------------------------------===//

#ifndef __UNWIND_EHABI_H__
#define __UNWIND_EHABI_H__

#include <__libunwind_config.h>

#if defined(_LIBUNWIND_ARM_EHABI)

#include <stdint.h>
#include <unwind.h>

// Unable to unwind in the ARM index table (section 5 EHABI).
#define UNW_EXIDX_CANTUNWIND 0x1

static inline uint32_t signExtendPrel31(uint32_t data) {
  return data | ((data & 0x40000000u) << 1);
}

static inline uint32_t readPrel31(const uint32_t *data) {
  return (((uint32_t)(uintptr_t)data) + signExtendPrel31(*data));
}

#if defined(__cplusplus)
extern "C" {
#endif

extern _Unwind_Reason_Code __aeabi_unwind_cpp_pr0(
    _Unwind_State state, _Unwind_Control_Block *ucbp, _Unwind_Context *context);

extern _Unwind_Reason_Code __aeabi_unwind_cpp_pr1(
    _Unwind_State state, _Unwind_Control_Block *ucbp, _Unwind_Context *context);

extern _Unwind_Reason_Code __aeabi_unwind_cpp_pr2(
    _Unwind_State state, _Unwind_Control_Block *ucbp, _Unwind_Context *context);

#if defined(__cplusplus)
} // extern "C"
#endif

#endif // defined(_LIBUNWIND_ARM_EHABI)

#endif  // __UNWIND_EHABI_H__
