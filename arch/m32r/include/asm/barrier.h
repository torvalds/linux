/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001  Hiroyuki Kondo, Hirokazu Takata, and Hitoshi Yamamoto
 * Copyright (C) 2004, 2006  Hirokazu Takata <takata at linux-m32r.org>
 */
#ifndef _ASM_M32R_BARRIER_H
#define _ASM_M32R_BARRIER_H

#define nop()  __asm__ __volatile__ ("nop" : : )

#include <asm-generic/barrier.h>

#endif /* _ASM_M32R_BARRIER_H */
