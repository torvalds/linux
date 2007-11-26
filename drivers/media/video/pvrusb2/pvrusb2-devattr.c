/*
 *
 *  $Id$
 *
 *  Copyright (C) 2007 Mike Isely <isely@pobox.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

/*

This source file should encompass ALL per-device type information for the
driver.  To define a new device, add elements to the pvr2_device_table and
pvr2_device_desc structures.

*/

#include "pvrusb2-devattr.h"
#include <linux/usb.h>

/* Known major hardware variants, keyed from device ID */
#define PVR2_HDW_TYPE_29XXX 0
#define PVR2_HDW_TYPE_24XXX 1

struct usb_device_id pvr2_device_table[] = {
	[PVR2_HDW_TYPE_29XXX] = { USB_DEVICE(0x2040, 0x2900) },
	[PVR2_HDW_TYPE_24XXX] = { USB_DEVICE(0x2040, 0x2400) },
	{ }
};

/* Names of other client modules to request for 24xxx model hardware */
static const char *pvr2_client_24xxx[] = {
	"cx25840",
	"tuner",
	"wm8775",
};

/* Names of other client modules to request for 29xxx model hardware */
static const char *pvr2_client_29xxx[] = {
	"msp3400",
	"saa7115",
	"tuner",
};

/* Firmware file name(s) for 29xxx devices */
static const char *pvr2_fw1_names_29xxx[] = {
		"v4l-pvrusb2-29xxx-01.fw",
};

/* Firmware file name(s) for 29xxx devices */
static const char *pvr2_fw1_names_24xxx[] = {
		"v4l-pvrusb2-24xxx-01.fw",
};

const struct pvr2_device_desc pvr2_device_descriptions[] = {
	[PVR2_HDW_TYPE_29XXX] = {
		.description = "WinTV PVR USB2 Model Category 29xxxx",
		.shortname = "29xxx",
		.client_modules.lst = pvr2_client_29xxx,
		.client_modules.cnt = ARRAY_SIZE(pvr2_client_29xxx),
		.fx2_firmware.lst = pvr2_fw1_names_29xxx,
		.fx2_firmware.cnt = ARRAY_SIZE(pvr2_fw1_names_29xxx),
	},
	[PVR2_HDW_TYPE_24XXX] = {
		.description = "WinTV PVR USB2 Model Category 24xxxx",
		.shortname = "24xxx",
		.client_modules.lst = pvr2_client_24xxx,
		.client_modules.cnt = ARRAY_SIZE(pvr2_client_24xxx),
		.fx2_firmware.lst = pvr2_fw1_names_24xxx,
		.fx2_firmware.cnt = ARRAY_SIZE(pvr2_fw1_names_24xxx),
		.flag_has_cx25840 = !0,
		.flag_has_wm8775 = !0,
	},
};

const unsigned int pvr2_device_count = ARRAY_SIZE(pvr2_device_descriptions);

MODULE_DEVICE_TABLE(usb, pvr2_device_table);


/*
  Stuff for Emacs to see, in order to encourage consistent editing style:
  *** Local Variables: ***
  *** mode: c ***
  *** fill-column: 75 ***
  *** tab-width: 8 ***
  *** c-basic-offset: 8 ***
  *** End: ***
  */
