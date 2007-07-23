/*
 * TI DaVinci clock definitions
 *
 * Copyright (C) 2006 Texas Instruments.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ARCH_ARM_DAVINCI_CLOCK_H
#define __ARCH_ARM_DAVINCI_CLOCK_H

struct clk {
	struct list_head	node;
	struct module		*owner;
	const char		*name;
	unsigned int		*rate;
	int			id;
	__s8			usecount;
	__u8			flags;
	__u8			lpsc;
};

/* Clock flags */
#define RATE_CKCTL		1
#define RATE_FIXED		2
#define RATE_PROPAGATES		4
#define VIRTUAL_CLOCK		8
#define ALWAYS_ENABLED		16
#define ENABLE_REG_32BIT	32

#endif
