/*
 *  arch/xtensa/lib/memcpy.S
 *
 *  ANSI C standard library function memcpy
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License.  See the file "COPYING" in the main directory of
 *  this archive for more details.
 *
 *  Copyright (C) 2002 Tensilica Inc.
 */

#define _ASMLANGUAGE
#include <xtensa/config/core.h>

.text
.align	4
.global	bcopy
.type   bcopy,@function
bcopy:
        movi    a14, xthal_bcopy        // a14 safe to use regardless of whether caller
                                        //   used call4 or call8 (can't have used call12)
        jx      a14                     // let the Core HAL do the work

.text
.align	4
.global	memcpy
.type   memcpy,@function
memcpy:
.global	memmove
.type   memmove,@function
memmove:
        movi    a14, xthal_memcpy      // a14 safe to use regardless of whether caller
                                        //   used call4 or call8 (can't have used call12)
        jx      a14                     // let the Core HAL do the work

