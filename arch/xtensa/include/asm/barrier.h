/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2012 Tensilica Inc.
 */

#ifndef _XTENSA_SYSTEM_H
#define _XTENSA_SYSTEM_H

#include <asm/core.h>

#define mb()  ({ __asm__ __volatile__("memw" : : : "memory"); })
#define rmb() barrier()
#define wmb() mb()

#if XCHAL_HAVE_S32C1I
#define __smp_mb__before_atomic()		barrier()
#define __smp_mb__after_atomic()		barrier()
#endif

#include <asm-generic/barrier.h>

#endif /* _XTENSA_SYSTEM_H */
