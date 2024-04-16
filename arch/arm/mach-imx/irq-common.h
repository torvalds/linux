/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) BitBox Ltd 2010
 */

#ifndef __PLAT_MXC_IRQ_COMMON_H__
#define __PLAT_MXC_IRQ_COMMON_H__

/* all normal IRQs can be FIQs */
#define FIQ_START	0

struct mxc_extra_irq
{
	int (*set_irq_fiq)(unsigned int irq, unsigned int type);
};

#endif
