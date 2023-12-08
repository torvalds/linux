/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2013 Ma Haijun <mahaijuns@gmail.com>
 * Copyright (c) 2003 ARM Limited
 * All Rights Reserved
 */
#include <linux/linkage.h>
#include <linux/init.h>

	__INIT

/*
 * OX820 specific entry point for secondary CPUs.
 */
ENTRY(ox820_secondary_startup)
	mov r4, #0
	/* invalidate both caches and branch target cache */
	mcr p15, 0, r4, c7, c7, 0
	/*
	 * we've been released from the holding pen: secondary_stack
	 * should now contain the SVC stack for this core
	 */
	b	secondary_startup
