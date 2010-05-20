/*
 * OMAP4 CM module functions
 *
 * Copyright (C) 2009 Nokia Corporation
 * Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/io.h>

#include <asm/atomic.h>

#include "cm.h"
#include "cm-regbits-44xx.h"

/**
 * omap4_cm_wait_idlest_ready - wait for a module to leave idle or standby
 * @prcm_mod: PRCM module offset (XXX example)
 * @prcm_dev_offs: PRCM device offset (e.g. MCASP XXX example)
 *
 * XXX document
 */
int omap4_cm_wait_idlest_ready(u32 prcm_mod, u8 prcm_dev_offs)
{
	/* FIXME: Add clock manager related code */
	return 0;
}

