/*
 * Copyright 2008 Sascha Hauer, Pengutronix <s.hauer@pengutronix.de>
 * Copyright 2009 Daniel Mack <daniel@caiaq.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA  02110-1301, USA.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/usb/otg.h>
#include <linux/usb/ulpi.h>

#include <mach/ulpi.h>

/* ULPIVIEW register bits */
#define ULPIVW_WU		(1 << 31)	/* Wakeup */
#define ULPIVW_RUN		(1 << 30)	/* read/write run */
#define ULPIVW_WRITE		(1 << 29)	/* 0 = read  1 = write */
#define ULPIVW_SS		(1 << 27)	/* SyncState */
#define ULPIVW_PORT_MASK	0x07	/* Port field */
#define ULPIVW_PORT_SHIFT	24
#define ULPIVW_ADDR_MASK	0xff	/* data address field */
#define ULPIVW_ADDR_SHIFT	16
#define ULPIVW_RDATA_MASK	0xff	/* read data field */
#define ULPIVW_RDATA_SHIFT	8
#define ULPIVW_WDATA_MASK	0xff	/* write data field */
#define ULPIVW_WDATA_SHIFT	0

static int ulpi_poll(void __iomem *view, u32 bit)
{
	int timeout = 10000;

	while (timeout--) {
		u32 data = __raw_readl(view);

		if (!(data & bit))
			return 0;

		cpu_relax();
	};

	printk(KERN_WARNING "timeout polling for ULPI device\n");

	return -ETIMEDOUT;
}

static int ulpi_read(struct usb_phy *otg, u32 reg)
{
	int ret;
	void __iomem *view = otg->io_priv;

	/* make sure interface is running */
	if (!(__raw_readl(view) & ULPIVW_SS)) {
		__raw_writel(ULPIVW_WU, view);

		/* wait for wakeup */
		ret = ulpi_poll(view, ULPIVW_WU);
		if (ret)
			return ret;
	}

	/* read the register */
	__raw_writel((ULPIVW_RUN | (reg << ULPIVW_ADDR_SHIFT)), view);

	/* wait for completion */
	ret = ulpi_poll(view, ULPIVW_RUN);
	if (ret)
		return ret;

	return (__raw_readl(view) >> ULPIVW_RDATA_SHIFT) & ULPIVW_RDATA_MASK;
}

static int ulpi_write(struct usb_phy *otg, u32 val, u32 reg)
{
	int ret;
	void __iomem *view = otg->io_priv;

	/* make sure the interface is running */
	if (!(__raw_readl(view) & ULPIVW_SS)) {
		__raw_writel(ULPIVW_WU, view);
		/* wait for wakeup */
		ret = ulpi_poll(view, ULPIVW_WU);
		if (ret)
			return ret;
	}

	__raw_writel((ULPIVW_RUN | ULPIVW_WRITE |
		      (reg << ULPIVW_ADDR_SHIFT) |
		      ((val & ULPIVW_WDATA_MASK) << ULPIVW_WDATA_SHIFT)), view);

	/* wait for completion */
	return ulpi_poll(view, ULPIVW_RUN);
}

struct usb_phy_io_ops mxc_ulpi_access_ops = {
	.read	= ulpi_read,
	.write	= ulpi_write,
};
EXPORT_SYMBOL_GPL(mxc_ulpi_access_ops);

struct usb_phy *imx_otg_ulpi_create(unsigned int flags)
{
	return otg_ulpi_create(&mxc_ulpi_access_ops, flags);
}
