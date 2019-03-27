//===-- scudo_crc32.cpp -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// CRC32 function leveraging hardware specific instructions. This has to be
/// kept separated to restrict the use of compiler specific flags to this file.
///
//===----------------------------------------------------------------------===//

#include "scudo_crc32.h"

namespace __scudo {

#if defined(__SSE4_2__) || defined(__ARM_FEATURE_CRC32)
u32 computeHardwareCRC32(u32 Crc, uptr Data) {
  return CRC32_INTRINSIC(Crc, Data);
}
#endif  // defined(__SSE4_2__) || defined(__ARM_FEATURE_CRC32)

}  // namespace __scudo
