/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * arch/arm/include/asm/hardware/cache-tauros2.h
 *
 * Copyright (C) 2008 Marvell Semiconductor
 */

#define CACHE_TAUROS2_PREFETCH_ON	(1 << 0)
#define CACHE_TAUROS2_LINEFILL_BURST8	(1 << 1)

extern void __init tauros2_init(unsigned int features);
