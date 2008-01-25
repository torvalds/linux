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
/* This is needed in order to pull in tuner type ids... */
#include <linux/i2c.h>
#include <media/tuner.h>



/*------------------------------------------------------------------------*/
/* Hauppauge PVR-USB2 Model 29xxx */

static const char *pvr2_client_29xxx[] = {
	"msp3400",
	"saa7115",
	"tuner",
};

static const char *pvr2_fw1_names_29xxx[] = {
		"v4l-pvrusb2-29xxx-01.fw",
};

static const struct pvr2_device_desc pvr2_device_29xxx = {
		.description = "WinTV PVR USB2 Model Category 29xxxx",
		.shortname = "29xxx",
		.client_modules.lst = pvr2_client_29xxx,
		.client_modules.cnt = ARRAY_SIZE(pvr2_client_29xxx),
		.fx2_firmware.lst = pvr2_fw1_names_29xxx,
		.fx2_firmware.cnt = ARRAY_SIZE(pvr2_fw1_names_29xxx),
		.flag_has_hauppauge_rom = !0,
		.signal_routing_scheme = PVR2_ROUTING_SCHEME_HAUPPAUGE,
};



/*------------------------------------------------------------------------*/
/* Hauppauge PVR-USB2 Model 24xxx */

static const char *pvr2_client_24xxx[] = {
	"cx25840",
	"tuner",
	"wm8775",
};

static const char *pvr2_fw1_names_24xxx[] = {
		"v4l-pvrusb2-24xxx-01.fw",
};

static const struct pvr2_device_desc pvr2_device_24xxx = {
		.description = "WinTV PVR USB2 Model Category 24xxxx",
		.shortname = "24xxx",
		.client_modules.lst = pvr2_client_24xxx,
		.client_modules.cnt = ARRAY_SIZE(pvr2_client_24xxx),
		.fx2_firmware.lst = pvr2_fw1_names_24xxx,
		.fx2_firmware.cnt = ARRAY_SIZE(pvr2_fw1_names_24xxx),
		.flag_has_cx25840 = !0,
		.flag_has_wm8775 = !0,
		.flag_has_hauppauge_rom = !0,
		.flag_has_hauppauge_custom_ir = !0,
		.signal_routing_scheme = PVR2_ROUTING_SCHEME_HAUPPAUGE,
};



/*------------------------------------------------------------------------*/
/* GOTVIEW USB2.0 DVD2 */

static const char *pvr2_client_gotview_2[] = {
	"cx25840",
	"tuner",
};

static const struct pvr2_device_desc pvr2_device_gotview_2 = {
		.description = "Gotview USB 2.0 DVD 2",
		.shortname = "gv2",
		.client_modules.lst = pvr2_client_gotview_2,
		.client_modules.cnt = ARRAY_SIZE(pvr2_client_gotview_2),
		.flag_has_cx25840 = !0,
		.default_tuner_type = TUNER_PHILIPS_FM1216ME_MK3,
		.signal_routing_scheme = PVR2_ROUTING_SCHEME_GOTVIEW,
};



#ifdef CONFIG_VIDEO_PVRUSB2_ONAIR_CREATOR
/*------------------------------------------------------------------------*/
/* OnAir Creator */

static const char *pvr2_client_onair_creator[] = {
	"saa7115",
	"tuner",
	"cs53l32a",
};

static const struct pvr2_device_desc pvr2_device_onair_creator = {
		.description = "OnAir Creator Hybrid USB tuner",
		.shortname = "oac",
		.client_modules.lst = pvr2_client_onair_creator,
		.client_modules.cnt = ARRAY_SIZE(pvr2_client_onair_creator),
		.default_tuner_type = TUNER_LG_TDVS_H06XF,
		.signal_routing_scheme = PVR2_ROUTING_SCHEME_HAUPPAUGE,
};
#endif



#ifdef CONFIG_VIDEO_PVRUSB2_ONAIR_USB2
/*------------------------------------------------------------------------*/
/* OnAir USB 2.0 */

static const char *pvr2_client_onair_usb2[] = {
	"saa7115",
	"tuner",
	"cs53l32a",
};

static const struct pvr2_device_desc pvr2_device_onair_usb2 = {
		.description = "OnAir USB2 Hybrid USB tuner",
		.shortname = "oa2",
		.client_modules.lst = pvr2_client_onair_usb2,
		.client_modules.cnt = ARRAY_SIZE(pvr2_client_onair_usb2),
		.default_tuner_type = TUNER_PHILIPS_ATSC,
		.signal_routing_scheme = PVR2_ROUTING_SCHEME_HAUPPAUGE,
};
#endif



/*------------------------------------------------------------------------*/
/* Hauppauge PVR-USB2 Model 75xxx */

static const char *pvr2_client_75xxx[] = {
	"cx25840",
	"tuner",
};

static const char *pvr2_fw1_names_75xxx[] = {
		"v4l-pvrusb2-73xxx-01.fw",
};

static const struct pvr2_device_desc pvr2_device_75xxx = {
		.description = "WinTV PVR USB2 Model Category 75xxxx",
		.shortname = "75xxx",
		.client_modules.lst = pvr2_client_75xxx,
		.client_modules.cnt = ARRAY_SIZE(pvr2_client_75xxx),
		.fx2_firmware.lst = pvr2_fw1_names_75xxx,
		.fx2_firmware.cnt = ARRAY_SIZE(pvr2_fw1_names_75xxx),
		.flag_has_cx25840 = !0,
		.flag_has_hauppauge_rom = !0,
		.signal_routing_scheme = PVR2_ROUTING_SCHEME_HAUPPAUGE,
		.default_std_mask = V4L2_STD_NTSC_M,
};



/*------------------------------------------------------------------------*/

struct usb_device_id pvr2_device_table[] = {
	{ USB_DEVICE(0x2040, 0x2900),
	  .driver_info = (kernel_ulong_t)&pvr2_device_29xxx},
	{ USB_DEVICE(0x2040, 0x2400),
	  .driver_info = (kernel_ulong_t)&pvr2_device_24xxx},
	{ USB_DEVICE(0x1164, 0x0622),
	  .driver_info = (kernel_ulong_t)&pvr2_device_gotview_2},
#ifdef CONFIG_VIDEO_PVRUSB2_ONAIR_CREATOR
	{ USB_DEVICE(0x11ba, 0x1003),
	  .driver_info = (kernel_ulong_t)&pvr2_device_onair_creator},
#endif
#ifdef CONFIG_VIDEO_PVRUSB2_ONAIR_USB2
	{ USB_DEVICE(0x11ba, 0x1001),
	  .driver_info = (kernel_ulong_t)&pvr2_device_onair_usb2},
#endif
	{ USB_DEVICE(0x2040, 0x7500),
	  .driver_info = (kernel_ulong_t)&pvr2_device_75xxx},
	{ }
};

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
