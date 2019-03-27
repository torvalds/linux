//===-- lib/builtins/ppc/floattitf.c - Convert int128->long double -*-C -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements converting a signed 128 bit integer to a 128bit IBM /
// PowerPC long double (double-double) value.
//
//===----------------------------------------------------------------------===//

#include <stdint.h>

/* Conversions from signed and unsigned 64-bit int to long double. */
long double __floatditf(int64_t);
long double __floatunditf(uint64_t);

/* Convert a signed 128-bit integer to long double.
 * This uses the following property:  Let hi and lo be 64-bits each,
 * and let signed_val_k() and unsigned_val_k() be the value of the
 * argument interpreted as a signed or unsigned k-bit integer. Then,
 *
 * signed_val_128(hi,lo) = signed_val_64(hi) * 2^64 + unsigned_val_64(lo)
 * = (long double)hi * 2^64 + (long double)lo,
 *
 * where (long double)hi and (long double)lo are signed and
 * unsigned 64-bit integer to long double conversions, respectively.
 */
long double __floattitf(__int128_t arg) {
  /* Split the int128 argument into 64-bit high and low int64 parts. */
  int64_t ArgHiPart = (int64_t)(arg >> 64);
  uint64_t ArgLoPart = (uint64_t)arg;

  /* Convert each 64-bit part into long double. The high part
   * must be a signed conversion and the low part an unsigned conversion
   * to ensure the correct result. */
  long double ConvertedHiPart = __floatditf(ArgHiPart);
  long double ConvertedLoPart = __floatunditf(ArgLoPart);

  /* The low bit of ArgHiPart corresponds to the 2^64 bit in arg.
   * Multiply the high part by 2^64 to undo the right shift by 64-bits
   * done in the splitting. Then, add to the low part to obtain the
   * final result. */
  return ((ConvertedHiPart * 0x1.0p64) + ConvertedLoPart);
}
