/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * arch/arm/mach-tegra/board.h
 *
 * Copyright (c) 2013 NVIDIA Corporation. All rights reserved.
 * Copyright (C) 2010 Google, Inc.
 *
 * Author:
 *	Colin Cross <ccross@google.com>
 *	Erik Gilling <konkers@google.com>
 */

#ifndef __MACH_TEGRA_BOARD_H
#define __MACH_TEGRA_BOARD_H

#include <linux/types.h>
#include <linux/reboot.h>

void __init tegra_map_common_io(void);
void __init tegra_init_irq(void);

void __init tegra_paz00_wifikill_init(void);

#endif
