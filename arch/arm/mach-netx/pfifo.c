// SPDX-License-Identifier: GPL-2.0-only
/*
 * arch/arm/mach-netx/pfifo.c
 *
 * Copyright (c) 2005 Sascha Hauer <s.hauer@pengutronix.de>, Pengutronix
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/io.h>

#include <mach/hardware.h>
#include <mach/netx-regs.h>
#include <mach/pfifo.h>

static DEFINE_MUTEX(pfifo_lock);

static unsigned int pfifo_used = 0;

int pfifo_request(unsigned int pfifo_mask)
{
	int err = 0;
	unsigned int val;

	mutex_lock(&pfifo_lock);

	if (pfifo_mask & pfifo_used) {
		err = -EBUSY;
		goto out;
	}

	pfifo_used |= pfifo_mask;

	val = readl(NETX_PFIFO_RESET);
	writel(val | pfifo_mask, NETX_PFIFO_RESET);
	writel(val, NETX_PFIFO_RESET);

out:
	mutex_unlock(&pfifo_lock);
	return err;
}

void pfifo_free(unsigned int pfifo_mask)
{
	mutex_lock(&pfifo_lock);
	pfifo_used &= ~pfifo_mask;
	mutex_unlock(&pfifo_lock);
}

EXPORT_SYMBOL(pfifo_push);
EXPORT_SYMBOL(pfifo_pop);
EXPORT_SYMBOL(pfifo_fill_level);
EXPORT_SYMBOL(pfifo_empty);
EXPORT_SYMBOL(pfifo_request);
EXPORT_SYMBOL(pfifo_free);
