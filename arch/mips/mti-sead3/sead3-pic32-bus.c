/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2012 MIPS Technologies, Inc.  All rights reserved.
 */
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/errno.h>

#define PIC32_NULL	0x00
#define PIC32_RD	0x01
#define PIC32_SYSRD	0x02
#define PIC32_WR	0x10
#define PIC32_SYSWR	0x20
#define PIC32_IRQ_CLR   0x40
#define PIC32_STATUS	0x80

#define DELAY()	udelay(100)	/* FIXME: needed? */

/* spinlock to ensure atomic access to PIC32 */
static DEFINE_SPINLOCK(pic32_bus_lock);

/* FIXME: io_remap these */
static void __iomem *bus_xfer   = (void __iomem *)0xbf000600;
static void __iomem *bus_status = (void __iomem *)0xbf000060;

static inline unsigned int ioready(void)
{
	return readl(bus_status) & 1;
}

static inline void wait_ioready(void)
{
	do { } while (!ioready());
}

static inline void wait_ioclear(void)
{
	do { } while (ioready());
}

static inline void check_ioclear(void)
{
	if (ioready()) {
		pr_debug("ioclear: initially busy\n");
		do {
			(void) readl(bus_xfer);
			DELAY();
		} while (ioready());
		pr_debug("ioclear: cleared busy\n");
	}
}

u32 pic32_bus_readl(u32 reg)
{
	unsigned long flags;
	u32 status, val;

	spin_lock_irqsave(&pic32_bus_lock, flags);

	check_ioclear();

	writel((PIC32_RD << 24) | (reg & 0x00ffffff), bus_xfer);
	DELAY();
	wait_ioready();
	status = readl(bus_xfer);
	DELAY();
	val = readl(bus_xfer);
	wait_ioclear();

	pr_debug("pic32_bus_readl: *%x -> %x (status=%x)\n", reg, val, status);

	spin_unlock_irqrestore(&pic32_bus_lock, flags);

	return val;
}

void pic32_bus_writel(u32 val, u32 reg)
{
	unsigned long flags;
	u32 status;

	spin_lock_irqsave(&pic32_bus_lock, flags);

	check_ioclear();

	writel((PIC32_WR << 24) | (reg & 0x00ffffff), bus_xfer);
	DELAY();
	writel(val, bus_xfer);
	DELAY();
	wait_ioready();
	status = readl(bus_xfer);
	wait_ioclear();

	pr_debug("pic32_bus_writel: *%x <- %x (status=%x)\n", reg, val, status);

	spin_unlock_irqrestore(&pic32_bus_lock, flags);
}
