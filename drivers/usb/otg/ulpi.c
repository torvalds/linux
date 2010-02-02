/*
 * Generic ULPI USB transceiver support
 *
 * Copyright (C) 2009 Daniel Mack <daniel@caiaq.de>
 *
 * Based on sources from
 *
 *   Sascha Hauer <s.hauer@pengutronix.de>
 *   Freescale Semiconductors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/kernel.h>
#include <linux/usb.h>
#include <linux/usb/otg.h>
#include <linux/usb/ulpi.h>

/* ULPI register addresses */
#define ULPI_VID_LOW         0x00    /* Vendor ID low */
#define ULPI_VID_HIGH        0x01    /* Vendor ID high */
#define ULPI_PID_LOW         0x02    /* Product ID low */
#define ULPI_PID_HIGH        0x03    /* Product ID high */
#define ULPI_ITFCTL          0x07    /* Interface Control */
#define ULPI_OTGCTL          0x0A    /* OTG Control */

/* add to above register address to access Set/Clear functions */
#define ULPI_REG_SET         0x01
#define ULPI_REG_CLEAR       0x02

/* ULPI OTG Control Register bits */
#define ID_PULL_UP              (1 << 0)        /* enable ID Pull Up */
#define DP_PULL_DOWN            (1 << 1)        /* enable DP Pull Down */
#define DM_PULL_DOWN            (1 << 2)        /* enable DM Pull Down */
#define DISCHRG_VBUS            (1 << 3)        /* Discharge Vbus */
#define CHRG_VBUS               (1 << 4)        /* Charge Vbus */
#define DRV_VBUS                (1 << 5)        /* Drive Vbus */
#define DRV_VBUS_EXT            (1 << 6)        /* Drive Vbus external */
#define USE_EXT_VBUS_IND        (1 << 7)        /* Use ext. Vbus indicator */

#define ULPI_ID(vendor, product) (((vendor) << 16) | (product))

#define TR_FLAG(flags, a, b)	(((flags) & a) ? b : 0)

/* ULPI hardcoded IDs, used for probing */
static unsigned int ulpi_ids[] = {
	ULPI_ID(0x04cc, 0x1504),	/* NXP ISP1504 */
};

static int ulpi_set_flags(struct otg_transceiver *otg)
{
	unsigned int flags = 0;

	if (otg->flags & USB_OTG_PULLUP_ID)
		flags |= ID_PULL_UP;

	if (otg->flags & USB_OTG_PULLDOWN_DM)
		flags |= DM_PULL_DOWN;

	if (otg->flags & USB_OTG_PULLDOWN_DP)
		flags |= DP_PULL_DOWN;

	if (otg->flags & USB_OTG_EXT_VBUS_INDICATOR)
		flags |= USE_EXT_VBUS_IND;

	return otg_io_write(otg, flags, ULPI_OTGCTL + ULPI_REG_SET);
}

static int ulpi_init(struct otg_transceiver *otg)
{
	int i, vid, pid;

	vid = (otg_io_read(otg, ULPI_VID_HIGH) << 8) |
	       otg_io_read(otg, ULPI_VID_LOW);
	pid = (otg_io_read(otg, ULPI_PID_HIGH) << 8) |
	       otg_io_read(otg, ULPI_PID_LOW);

	pr_info("ULPI transceiver vendor/product ID 0x%04x/0x%04x\n", vid, pid);

	for (i = 0; i < ARRAY_SIZE(ulpi_ids); i++)
		if (ulpi_ids[i] == ULPI_ID(vid, pid))
			return ulpi_set_flags(otg);

	pr_err("ULPI ID does not match any known transceiver.\n");
	return -ENODEV;
}

static int ulpi_set_vbus(struct otg_transceiver *otg, bool on)
{
	unsigned int flags = otg_io_read(otg, ULPI_OTGCTL);

	flags &= ~(DRV_VBUS | DRV_VBUS_EXT);

	if (on) {
		if (otg->flags & USB_OTG_DRV_VBUS)
			flags |= DRV_VBUS;

		if (otg->flags & USB_OTG_DRV_VBUS_EXT)
			flags |= DRV_VBUS_EXT;
	}

	return otg_io_write(otg, flags, ULPI_OTGCTL + ULPI_REG_SET);
}

struct otg_transceiver *
otg_ulpi_create(struct otg_io_access_ops *ops,
		unsigned int flags)
{
	struct otg_transceiver *otg;

	otg = kzalloc(sizeof(*otg), GFP_KERNEL);
	if (!otg)
		return NULL;

	otg->label	= "ULPI";
	otg->flags	= flags;
	otg->io_ops	= ops;
	otg->init	= ulpi_init;
	otg->set_vbus	= ulpi_set_vbus;

	return otg;
}
EXPORT_SYMBOL_GPL(otg_ulpi_create);

