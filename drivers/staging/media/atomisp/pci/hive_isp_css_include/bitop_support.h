/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __BITOP_SUPPORT_H_INCLUDED__
#define __BITOP_SUPPORT_H_INCLUDED__

#define bitop_setbit(a, b) ((a) |= (1UL << (b)))

#define bitop_getbit(a, b) (((a) & (1UL << (b))) != 0)

#define bitop_clearbit(a, b) ((a) &= ~(1UL << (b)))

#endif /* __BITOP_SUPPORT_H_INCLUDED__ */
