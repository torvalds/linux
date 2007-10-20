/*
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

#include <linux/hid.h>
#include "usbhid.h"

/*
 * This table contains pointers to initializers. To add support for new
 * devices, you need to add the USB vendor and product ids here.
 */
struct hid_ff_initializer {
	u16 idVendor;
	u16 idProduct;
	int (*init)(struct hid_device*);
};

/*
 * We try pidff when no other driver is found because PID is the
 * standards compliant way of implementing force feedback in HID.
 * pidff_init() will quickly abort if the device doesn't appear to
 * be a PID device
 */
static struct hid_ff_initializer inits[] = {
#ifdef CONFIG_LOGITECH_FF
	{ 0x46d, 0xc211, hid_lgff_init }, /* Logitech Cordless rumble pad */
	{ 0x46d, 0xc219, hid_lgff_init }, /* Logitech Cordless rumble pad 2 */
	{ 0x46d, 0xc283, hid_lgff_init }, /* Logitech Wingman Force 3d */
	{ 0x46d, 0xc286, hid_lgff_init }, /* Logitech Force 3D Pro Joystick */
	{ 0x46d, 0xc294, hid_lgff_init }, /* Logitech Formula Force EX */
	{ 0x46d, 0xc295, hid_lgff_init }, /* Logitech MOMO force wheel */
	{ 0x46d, 0xca03, hid_lgff_init }, /* Logitech MOMO force wheel */
#endif
#ifdef CONFIG_PANTHERLORD_FF
	{ 0x810, 0x0001, hid_plff_init }, /* "Twin USB Joystick" */
	{ 0xe8f, 0x0003, hid_plff_init }, /* "GreenAsia Inc.    USB Joystick     " */
#endif
#ifdef CONFIG_THRUSTMASTER_FF
	{ 0x44f, 0xb300, hid_tmff_init },
	{ 0x44f, 0xb304, hid_tmff_init },
	{ 0x44f, 0xb651, hid_tmff_init }, /* FGT Rumble Force Wheel */
	{ 0x44f, 0xb654, hid_tmff_init }, /* FGT Force Feedback Wheel */
#endif
#ifdef CONFIG_ZEROPLUS_FF
	{ 0xc12, 0x0005, hid_zpff_init },
	{ 0xc12, 0x0030, hid_zpff_init },
#endif
	{ 0,	 0,	 hid_pidff_init}  /* Matches anything */
};

int hid_ff_init(struct hid_device* hid)
{
	struct hid_ff_initializer *init;
	int vendor = le16_to_cpu(hid_to_usb_dev(hid)->descriptor.idVendor);
	int product = le16_to_cpu(hid_to_usb_dev(hid)->descriptor.idProduct);

	for (init = inits; init->idVendor; init++)
		if (init->idVendor == vendor && init->idProduct == product)
			break;

	return init->init(hid);
}
EXPORT_SYMBOL_GPL(hid_ff_init);

