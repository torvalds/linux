/*

  fp_trig.h: floating-point math routines for the Linux-m68k
  floating point emulator.

  Copyright (c) 1998 David Huggins-Daines.

  I hereby give permission, free of charge, to copy, modify, and
  redistribute this software, in source or binary form, provided that
  the above copyright notice and the following disclaimer are included
  in all such copies.

  THIS SOFTWARE IS PROVIDED "AS IS", WITH ABSOLUTELY NO WARRANTY, REAL
  OR IMPLIED.

*/

#ifndef _FP_TRIG_H
#define _FP_TRIG_H

#include "fp_emu.h"

/* floating point trigonometric instructions:

   the arguments to these are in the "internal" extended format, that
   is, an "exploded" version of the 96-bit extended fp format used by
   the 68881.

   they return a status code, which should end up in %d0, if all goes
   well.  */

struct fp_ext *fp_fsin(struct fp_ext *dest, struct fp_ext *src);
struct fp_ext *fp_fcos(struct fp_ext *dest, struct fp_ext *src);
struct fp_ext *fp_ftan(struct fp_ext *dest, struct fp_ext *src);
struct fp_ext *fp_fasin(struct fp_ext *dest, struct fp_ext *src);
struct fp_ext *fp_facos(struct fp_ext *dest, struct fp_ext *src);
struct fp_ext *fp_fatan(struct fp_ext *dest, struct fp_ext *src);
struct fp_ext *fp_fsinh(struct fp_ext *dest, struct fp_ext *src);
struct fp_ext *fp_fcosh(struct fp_ext *dest, struct fp_ext *src);
struct fp_ext *fp_ftanh(struct fp_ext *dest, struct fp_ext *src);
struct fp_ext *fp_fatanh(struct fp_ext *dest, struct fp_ext *src);
struct fp_ext *fp_fsincos0(struct fp_ext *dest, struct fp_ext *src);
struct fp_ext *fp_fsincos1(struct fp_ext *dest, struct fp_ext *src);
struct fp_ext *fp_fsincos2(struct fp_ext *dest, struct fp_ext *src);
struct fp_ext *fp_fsincos3(struct fp_ext *dest, struct fp_ext *src);
struct fp_ext *fp_fsincos4(struct fp_ext *dest, struct fp_ext *src);
struct fp_ext *fp_fsincos5(struct fp_ext *dest, struct fp_ext *src);
struct fp_ext *fp_fsincos6(struct fp_ext *dest, struct fp_ext *src);
struct fp_ext *fp_fsincos7(struct fp_ext *dest, struct fp_ext *src);

#endif /* _FP_TRIG_H */
