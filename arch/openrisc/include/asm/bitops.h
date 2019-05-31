/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * OpenRISC Linux
 *
 * Linux architectural port borrowing liberally from similar works of
 * others.  All original copyrights apply as per the original source
 * declaration.
 *
 * OpenRISC implementation:
 * Copyright (C) 2003 Matjaz Breskvar <phoenix@bsemi.com>
 * Copyright (C) 2010-2011 Jonas Bonn <jonas@southpole.se>
 * et al.
 */

#ifndef __ASM_OPENRISC_BITOPS_H
#define __ASM_OPENRISC_BITOPS_H

/*
 * Where we haven't written assembly versions yet, we fall back to the
 * generic implementations.  Otherwise, we pull in our (hopefully)
 * optimized versions.
 */

#include <linux/irqflags.h>
#include <linux/compiler.h>
#include <asm/barrier.h>

#include <asm/bitops/__ffs.h>
#include <asm-generic/bitops/ffz.h>
#include <asm/bitops/fls.h>
#include <asm/bitops/__fls.h>
#include <asm-generic/bitops/fls64.h>
#include <asm-generic/bitops/find.h>

#ifndef _LINUX_BITOPS_H
#error only <linux/bitops.h> can be included directly
#endif

#include <asm-generic/bitops/sched.h>
#include <asm/bitops/ffs.h>
#include <asm-generic/bitops/hweight.h>
#include <asm-generic/bitops/lock.h>

#include <asm/bitops/atomic.h>
#include <asm-generic/bitops/non-atomic.h>
#include <asm-generic/bitops/le.h>
#include <asm-generic/bitops/ext2-atomic.h>

#endif /* __ASM_GENERIC_BITOPS_H */
