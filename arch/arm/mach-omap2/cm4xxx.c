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

/* XXX move this to cm.h */
/* MAX_MODULE_READY_TIME: max milliseconds for module to leave idle */
#define MAX_MODULE_READY_TIME			20000

/*
 * OMAP4_PRCM_CM_CLKCTRL_IDLEST_MASK: isolates the IDLEST field in the
 * CM_CLKCTRL register.
 */
#define OMAP4_PRCM_CM_CLKCTRL_IDLEST_MASK	(0x2 << 16)

/*
 * OMAP4 prcm_mod u32 fields contain packed data: the CM ID in bit 16 and
 * the PRCM module offset address (from the CM module base) in bits 15-0.
 */
#define OMAP4_PRCM_MOD_CM_ID_SHIFT		16
#define OMAP4_PRCM_MOD_OFFS_MASK		0xffff

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

