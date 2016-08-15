/*
 * include/asm-sh/spinlock.h
 *
 * Copyright (C) 2002, 2003 Paul Mundt
 * Copyright (C) 2006, 2007 Akio Idehara
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#ifndef __ASM_SH_SPINLOCK_H
#define __ASM_SH_SPINLOCK_H

#if defined(CONFIG_CPU_SH4A)
#include <asm/spinlock-llsc.h>
#elif defined(CONFIG_CPU_J2)
#include <asm/spinlock-cas.h>
#else
#error "The configured cpu type does not support spinlocks"
#endif

#endif /* __ASM_SH_SPINLOCK_H */
