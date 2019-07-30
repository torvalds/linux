/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Memory barrier implementations for PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2012 GUAN Xue-tao
 */
#ifndef __UNICORE_BARRIER_H__
#define __UNICORE_BARRIER_H__

#define isb() __asm__ __volatile__ ("" : : : "memory")
#define dsb() __asm__ __volatile__ ("" : : : "memory")
#define dmb() __asm__ __volatile__ ("" : : : "memory")

#include <asm-generic/barrier.h>

#endif /* __UNICORE_BARRIER_H__ */
