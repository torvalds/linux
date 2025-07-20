/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 */

#ifndef _ASM_HEXAGON_SETUP_H
#define _ASM_HEXAGON_SETUP_H

#include <linux/init.h>
#include <uapi/asm/setup.h>

extern char external_cmdline_buffer;

void __init setup_arch_memory(void);

#endif
