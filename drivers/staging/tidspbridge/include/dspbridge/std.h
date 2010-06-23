/*
 * std.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Copyright (C) 2008 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef STD_
#define STD_

#include <linux/types.h>

/*
 *  ======== _TI_ ========
 *  _TI_ is defined for all TI targets
 */
#if defined(_29_) || defined(_30_) || defined(_40_) || defined(_50_) || \
    defined(_54_) || defined(_55_) || defined(_6x_) || defined(_80_) || \
    defined(_28_) || defined(_24_)
#define _TI_	1
#endif

/*
 *  ======== _FLOAT_ ========
 *  _FLOAT_ is defined for all targets that natively support floating point
 */
#if defined(_SUN_) || defined(_30_) || defined(_40_) || defined(_67_) || \
    defined(_80_)
#define _FLOAT_	1
#endif

/*
 *  ======== _FIXED_ ========
 *  _FIXED_ is defined for all fixed point target architectures
 */
#if defined(_29_) || defined(_50_) || defined(_54_) || defined(_55_) || \
    defined(_62_) || defined(_64_) || defined(_28_)
#define _FIXED_	1
#endif

/*
 *  ======== _TARGET_ ========
 *  _TARGET_ is defined for all target architectures (as opposed to
 *  host-side software)
 */
#if defined(_FIXED_) || defined(_FLOAT_)
#define _TARGET_ 1
#endif

/*
 *  8, 16, 32-bit type definitions
 *
 *  Sm*	- 8-bit type
 *  Md* - 16-bit type
 *  Lg* - 32-bit type
 *
 *  *s32 - signed type
 *  *u32 - unsigned type
 *  *Bits - unsigned type (bit-maps)
 */

/*
 *  Aliases for standard C types
 */

typedef s32(*fxn) (void);	/* generic function type */

#ifndef NULL
#define NULL 0
#endif

/*
 * These macros are used to cast 'Arg' types to 's32' or 'Ptr'.
 * These macros were added for the 55x since Arg is not the same
 * size as s32 and Ptr in 55x large model.
 */
#if defined(_28l_) || defined(_55l_)
#define ARG_TO_INT(A)	((s32)((long)(A) & 0xffff))
#define ARG_TO_PTR(A)	((Ptr)(A))
#else
#define ARG_TO_INT(A)	((s32)(A))
#define ARG_TO_PTR(A)	((Ptr)(A))
#endif

#endif /* STD_ */
