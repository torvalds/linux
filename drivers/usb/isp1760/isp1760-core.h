/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Driver for the NXP ISP1760 chip
 *
 * Copyright 2014 Laurent Pinchart
 * Copyright 2007 Sebastian Siewior
 *
 * Contacts:
 *	Sebastian Siewior <bigeasy@linutronix.de>
 *	Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 */

#ifndef _ISP1760_CORE_H_
#define _ISP1760_CORE_H_

#include <linux/ioport.h>

#include "isp1760-hcd.h"
#include "isp1760-udc.h"

struct device;
struct gpio_desc;

/*
 * Device flags that can vary from board to board.  All of these
 * indicate the most "atypical" case, so that a devflags of 0 is
 * a sane default configuration.
 */
#define ISP1760_FLAG_BUS_WIDTH_16	0x00000002 /* 16-bit data bus width */
#define ISP1760_FLAG_OTG_EN		0x00000004 /* Port 1 supports OTG */
#define ISP1760_FLAG_ANALOG_OC		0x00000008 /* Analog overcurrent */
#define ISP1760_FLAG_DACK_POL_HIGH	0x00000010 /* DACK active high */
#define ISP1760_FLAG_DREQ_POL_HIGH	0x00000020 /* DREQ active high */
#define ISP1760_FLAG_ISP1761		0x00000040 /* Chip is ISP1761 */
#define ISP1760_FLAG_INTR_POL_HIGH	0x00000080 /* Interrupt polarity active high */
#define ISP1760_FLAG_INTR_EDGE_TRIG	0x00000100 /* Interrupt edge triggered */

struct isp1760_device {
	struct device *dev;

	void __iomem *regs;
	unsigned int devflags;
	struct gpio_desc *rst_gpio;

	struct isp1760_hcd hcd;
	struct isp1760_udc udc;
};

int isp1760_register(struct resource *mem, int irq, unsigned long irqflags,
		     struct device *dev, unsigned int devflags);
void isp1760_unregister(struct device *dev);

void isp1760_set_pullup(struct isp1760_device *isp, bool enable);

static inline u32 isp1760_read32(void __iomem *base, u32 reg)
{
	return readl(base + reg);
}

static inline void isp1760_write32(void __iomem *base, u32 reg, u32 val)
{
	writel(val, base + reg);
}

#endif
