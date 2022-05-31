/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 ARM Ltd.
 */
#ifndef __ASM_BITOPS_H
#define __ASM_BITOPS_H

#include <linux/compiler.h>

#ifndef _LINUX_BITOPS_H
#error only <linux/bitops.h> can be included directly
#endif

#include <asm-generic/bitops/builtin-__ffs.h>
#include <asm-generic/bitops/builtin-ffs.h>
#include <asm-generic/bitops/builtin-__fls.h>
#include <asm-generic/bitops/builtin-fls.h>

#include <asm-generic/bitops/ffz.h>
#include <asm-generic/bitops/fls64.h>

#include <asm-generic/bitops/sched.h>
#include <asm-generic/bitops/hweight.h>

#include <asm-generic/bitops/atomic.h>
#include <asm-generic/bitops/lock.h>
#include <asm-generic/bitops/non-atomic.h>
#include <asm-generic/bitops/le.h>
#include <asm-generic/bitops/ext2-atomic-setbit.h>

#endif /* __ASM_BITOPS_H */
