/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * vDSO implementation for Hexagon
 *
 * Copyright (c) 2011, The Linux Foundation. All rights reserved.
 */

#ifndef __ASM_VDSO_H
#define __ASM_VDSO_H

#include <linux/types.h>

struct hexagon_vdso {
	u32 rt_signal_trampoline[2];
};

#endif /* __ASM_VDSO_H */
