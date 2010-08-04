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
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/usb/otg.h>
#include <linux/usb/ulpi.h>

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
		flags |= ULPI_OTG_CTRL_ID_PULLUP;

	if (otg->flags & USB_OTG_PULLDOWN_DM)
		flags |= ULPI_OTG_CTRL_DM_PULLDOWN;

	if (otg->flags & USB_OTG_PULLDOWN_DP)
		flags |= ULPI_OTG_CTRL_DP_PULLDOWN;

	if (otg->flags & USB_OTG_EXT_VBUS_INDICATOR)
		flags |= ULPI_OTG_CTRL_EXTVBUSIND;

	return otg_io_write(otg, flags, ULPI_SET(ULPI_OTG_CTRL));
}

static int ulpi_init(struct otg_transceiver *otg)
{
	int i, vid, pid, ret;
	u32 ulpi_id = 0;

	for (i = 0; i < 4; i++) {
		ret = otg_io_read(otg, ULPI_PRODUCT_ID_HIGH - i);
		if (ret < 0)
			return ret;
		ulpi_id = (ulpi_id << 8) | ret;
	}
	vid = ulpi_id & 0xffff;
	pid = ulpi_id >> 16;

	pr_info("ULPI transceiver vendor/product ID 0x%04x/0x%04x\n", vid, pid);

	for (i = 0; i < ARRAY_SIZE(ulpi_ids); i++)
		if (ulpi_ids[i] == ULPI_ID(vid, pid))
			return ulpi_set_flags(otg);

	pr_err("ULPI ID does not match any known transceiver.\n");
	return -ENODEV;
}

static int ulpi_set_vbus(struct otg_transceiver *otg, bool on)
{
	unsigned int flags = otg_io_read(otg, ULPI_OTG_CTRL);

	flags &= ~(ULPI_OTG_CTRL_DRVVBUS | ULPI_OTG_CTRL_DRVVBUS_EXT);

	if (on) {
		if (otg->flags & USB_OTG_DRV_VBUS)
			flags |= ULPI_OTG_CTRL_DRVVBUS;

		if (otg->flags & USB_OTG_DRV_VBUS_EXT)
			flags |= ULPI_OTG_CTRL_DRVVBUS_EXT;
	}

	return otg_io_write(otg, flags, ULPI_SET(ULPI_OTG_CTRL));
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

