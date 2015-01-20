/*
 * Driver for the NXP ISP1760 chip
 *
 * Copyright 2014 Laurent Pinchart
 * Copyright 2007 Sebastian Siewior
 *
 * Contacts:
 *	Sebastian Siewior <bigeasy@linutronix.de>
 *	Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 */

#ifndef _ISP1760_CORE_H_
#define _ISP1760_CORE_H_

#include <linux/ioport.h>

#include "isp1760-hcd.h"

struct isp1760_device {
	void __iomem *regs;

	struct isp1760_hcd hcd;
};

int isp1760_register(struct resource *mem, int irq, unsigned long irqflags,
		     struct device *dev, unsigned int devflags);
void isp1760_unregister(struct device *dev);

#endif
