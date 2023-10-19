/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2015 - 2020 Xilinx, Inc. All rights reserved.
 */

#ifndef _ASM_MICROBLAZE_BARRIER_H
#define _ASM_MICROBLAZE_BARRIER_H

#define mb()	__asm__ __volatile__ ("mbar 1" : : : "memory")

#include <asm-generic/barrier.h>

#endif /* _ASM_MICROBLAZE_BARRIER_H */
