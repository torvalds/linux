/*
 * support.c - provides standard pnp functions for the use of pnp protocol drivers,
 *
 * Copyright 2003 Adam Belay <ambx1@neo.rr.com>
 *
 */

#include <linux/module.h>
#include <linux/ctype.h>
#include <linux/pnp.h>
#include "base.h"

/**
 * pnp_is_active - Determines if a device is active based on its current resources
 * @dev: pointer to the desired PnP device
 *
 */

int pnp_is_active(struct pnp_dev *dev)
{
	if (!pnp_port_start(dev, 0) && pnp_port_len(dev, 0) <= 1 &&
	    !pnp_mem_start(dev, 0) && pnp_mem_len(dev, 0) <= 1 &&
	    pnp_irq(dev, 0) == -1 && pnp_dma(dev, 0) == -1)
		return 0;
	else
		return 1;
}

EXPORT_SYMBOL(pnp_is_active);
