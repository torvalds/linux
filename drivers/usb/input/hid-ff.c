/*
 * $Id: hid-ff.c,v 1.2 2002/04/18 22:02:47 jdeneux Exp $
 *
 *  Force feedback support for hid devices.
 *  Not all hid devices use the same protocol. For example, some use PID,
 *  other use their own proprietary procotol.
 *
 *  Copyright (c) 2002-2004 Johann Deneux
 */

/*
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Should you need to contact me, the author, you can do so by
 * e-mail - mail your message to <johann.deneux@it.uu.se>
 */

#include <linux/input.h>

#undef DEBUG
#include <linux/usb.h>

#include "hid.h"

/*
 * This table contains pointers to initializers. To add support for new
 * devices, you need to add the USB vendor and product ids here.
 */
struct hid_ff_initializer {
	u16 idVendor;
	u16 idProduct;
	int (*init)(struct hid_device*);
};

static struct hid_ff_initializer inits[] = {
#ifdef CONFIG_LOGITECH_FF
	{0x46d, 0xc211, hid_lgff_init}, // Logitech Cordless rumble pad
	{0x46d, 0xc283, hid_lgff_init}, // Logitech Wingman Force 3d
	{0x46d, 0xc295, hid_lgff_init},	// Logitech MOMO force wheel
	{0x46d, 0xc219, hid_lgff_init}, // Logitech Cordless rumble pad 2
#endif
#ifdef CONFIG_HID_PID
	{0x45e, 0x001b, hid_pid_init},
#endif
#ifdef CONFIG_THRUSTMASTER_FF
	{0x44f, 0xb304, hid_tmff_init},
#endif
	{0, 0, NULL} /* Terminating entry */
};

static struct hid_ff_initializer *hid_get_ff_init(__u16 idVendor,
						  __u16 idProduct)
{
	struct hid_ff_initializer *init;
	for (init = inits;
	     init->idVendor
	     && !(init->idVendor == idVendor
		  && init->idProduct == idProduct);
	     init++);

	return init->idVendor? init : NULL;
}

int hid_ff_init(struct hid_device* hid)
{
	struct hid_ff_initializer *init;

	init = hid_get_ff_init(le16_to_cpu(hid->dev->descriptor.idVendor),
			       le16_to_cpu(hid->dev->descriptor.idProduct));

	if (!init) {
		dbg("hid_ff_init could not find initializer");
		return -ENOSYS;
	}
	return init->init(hid);
}
