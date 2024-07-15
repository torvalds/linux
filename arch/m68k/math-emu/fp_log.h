/*

  fp_log.h: floating-point math routines for the Linux-m68k
  floating point emulator.

  Copyright (c) 1998-1999 David Huggins-Daines / Roman Zippel.

  I hereby give permission, free of charge, to copy, modify, and
  redistribute this software, in source or binary form, provided that
  the above copyright notice and the following disclaimer are included
  in all such copies.

  THIS SOFTWARE IS PROVIDED "AS IS", WITH ABSOLUTELY NO WARRANTY, REAL
  OR IMPLIED.

*/

#ifndef _FP_LOG_H
#define _FP_LOG_H

#include "fp_emu.h"

/* floating point logarithmic instructions:

   the arguments to these are in the "internal" extended format, that
   is, an "exploded" version of the 96-bit extended fp format used by
   the 68881.

   they return a status code, which should end up in %d0, if all goes
   well.  */

struct fp_ext *fp_fsqrt(struct fp_ext *dest, struct fp_ext *src);
struct fp_ext *fp_fetoxm1(struct fp_ext *dest, struct fp_ext *src);
struct fp_ext *fp_fetox(struct fp_ext *dest, struct fp_ext *src);
struct fp_ext *fp_ftwotox(struct fp_ext *dest, struct fp_ext *src);
struct fp_ext *fp_ftentox(struct fp_ext *dest, struct fp_ext *src);
struct fp_ext *fp_flogn(struct fp_ext *dest, struct fp_ext *src);
struct fp_ext *fp_flognp1(struct fp_ext *dest, struct fp_ext *src);
struct fp_ext *fp_flog10(struct fp_ext *dest, struct fp_ext *src);
struct fp_ext *fp_flog2(struct fp_ext *dest, struct fp_ext *src);
struct fp_ext *fp_fgetexp(struct fp_ext *dest, struct fp_ext *src);
struct fp_ext *fp_fgetman(struct fp_ext *dest, struct fp_ext *src);

#endif /* _FP_LOG_H */
