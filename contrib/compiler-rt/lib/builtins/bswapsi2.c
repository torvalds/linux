/* ===-- bswapsi2.c - Implement __bswapsi2 ---------------------------------===
 *
 *               The LLVM Compiler Infrastructure
 *
 * This file is dual licensed under the MIT and the University of Illinois Open
 * Source Licenses. See LICENSE.TXT for details.
 *
 * ===----------------------------------------------------------------------===
 *
 * This file implements __bswapsi2 for the compiler_rt library.
 *
 * ===----------------------------------------------------------------------===
 */

#include "int_lib.h"

COMPILER_RT_ABI uint32_t __bswapsi2(uint32_t u) {
  return (
      (((u)&0xff000000) >> 24) |
      (((u)&0x00ff0000) >> 8)  |
      (((u)&0x0000ff00) << 8)  |
      (((u)&0x000000ff) << 24));
}
