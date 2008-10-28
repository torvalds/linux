/*
 *
 *
 *  Copyright (C) 2005 Mike Isely <isely@pobox.com>
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
#ifndef __PVRUSB2_DEVATTR_H
#define __PVRUSB2_DEVATTR_H

#include <linux/mod_devicetable.h>
#include <linux/videodev2.h>
#ifdef CONFIG_VIDEO_PVRUSB2_DVB
#include "pvrusb2-dvb.h"
#endif

/*

  This header defines structures used to describe attributes of a device.

*/


struct pvr2_string_table {
	const char **lst;
	unsigned int cnt;
};

#define PVR2_ROUTING_SCHEME_HAUPPAUGE 0
#define PVR2_ROUTING_SCHEME_GOTVIEW 1

#define PVR2_DIGITAL_SCHEME_NONE 0
#define PVR2_DIGITAL_SCHEME_HAUPPAUGE 1
#define PVR2_DIGITAL_SCHEME_ONAIR 2

#define PVR2_LED_SCHEME_NONE 0
#define PVR2_LED_SCHEME_HAUPPAUGE 1

#define PVR2_IR_SCHEME_NONE 0
#define PVR2_IR_SCHEME_24XXX 1
#define PVR2_IR_SCHEME_ZILOG 2

/* This describes a particular hardware type (except for the USB device ID
   which must live in a separate structure due to environmental
   constraints).  See the top of pvrusb2-hdw.c for where this is
   instantiated. */
struct pvr2_device_desc {
	/* Single line text description of hardware */
	const char *description;

	/* Single token identifier for hardware */
	const char *shortname;

	/* List of additional client modules we need to load */
	struct pvr2_string_table client_modules;

	/* List of FX2 firmware file names we should search; if empty then
	   FX2 firmware check / load is skipped and we assume the device
	   was initialized from internal ROM. */
	struct pvr2_string_table fx2_firmware;

#ifdef CONFIG_VIDEO_PVRUSB2_DVB
	/* callback functions to handle attachment of digital tuner & demod */
	struct pvr2_dvb_props *dvb_props;

#endif
	/* Initial standard bits to use for this device, if not zero.
	   Anything set here is also implied as an available standard.
	   Note: This is ignored if overridden on the module load line via
	   the video_std module option. */
	v4l2_std_id default_std_mask;

	/* V4L tuner type ID to use with this device (only used if the
	   driver could not discover the type any other way). */
	int default_tuner_type;

	/* Signal routing scheme used by device, contains one of
	   PVR2_ROUTING_SCHEME_XXX.  Schemes have to be defined as we
	   encounter them.  This is an arbitrary integer scheme id; its
	   meaning is contained entirely within the driver and is
	   interpreted by logic which must send commands to the chip-level
	   drivers (search for things which touch this field). */
	unsigned char signal_routing_scheme;

	/* Indicates scheme for controlling device's LED (if any).  The
	   driver will turn on the LED when streaming is underway.  This
	   contains one of PVR2_LED_SCHEME_XXX. */
	unsigned char led_scheme;

	/* Control scheme to use if there is a digital tuner.  This
	   contains one of PVR2_DIGITAL_SCHEME_XXX.  This is an arbitrary
	   integer scheme id; its meaning is contained entirely within the
	   driver and is interpreted by logic which must control the
	   streaming pathway (search for things which touch this field). */
	unsigned char digital_control_scheme;

	/* If set, we don't bother trying to load cx23416 firmware. */
	unsigned int flag_skip_cx23416_firmware:1;

	/* If set, the encoder must be healthy in order for digital mode to
	   work (otherwise we assume that digital streaming will work even
	   if we fail to locate firmware for the encoder).  If the device
	   doesn't support digital streaming then this flag has no
	   effect. */
	unsigned int flag_digital_requires_cx23416:1;

	/* Device has a hauppauge eeprom which we can interrogate. */
	unsigned int flag_has_hauppauge_rom:1;

	/* Device does not require a powerup command to be issued. */
	unsigned int flag_no_powerup:1;

	/* Device has a cx25840 - this enables special additional logic to
	   handle it. */
	unsigned int flag_has_cx25840:1;

	/* Device has a wm8775 - this enables special additional logic to
	   ensure that it is found. */
	unsigned int flag_has_wm8775:1;

	/* Indicate any specialized IR scheme that might need to be
	   supported by this driver.  If not set, then it is assumed that
	   IR can work without help from the driver (which is frequently
	   the case).  This is otherwise set to one of
	   PVR2_IR_SCHEME_xxxx.  For "xxxx", the value "24XXX" indicates a
	   Hauppauge 24xxx class device which has an FPGA-hosted IR
	   receiver that can only be reached via FX2 command codes.  In
	   that case the pvrusb2 driver will emulate the behavior of the
	   older 29xxx device's IR receiver (a "virtual" I2C chip) in terms
	   of those command codes.  For the value "ZILOG", we're dealing
	   with an IR chip that must be taken out of reset via another FX2
	   command code (which is the case for HVR-1950 devices). */
	unsigned int ir_scheme:2;

	/* These bits define which kinds of sources the device can handle.
	   Note: Digital tuner presence is inferred by the
	   digital_control_scheme enumeration. */
	unsigned int flag_has_fmradio:1;       /* Has FM radio receiver */
	unsigned int flag_has_analogtuner:1;   /* Has analog tuner */
	unsigned int flag_has_composite:1;     /* Has composite input */
	unsigned int flag_has_svideo:1;        /* Has s-video input */
};

extern struct usb_device_id pvr2_device_table[];

#endif /* __PVRUSB2_HDW_INTERNAL_H */

/*
  Stuff for Emacs to see, in order to encourage consistent editing style:
  *** Local Variables: ***
  *** mode: c ***
  *** fill-column: 75 ***
  *** tab-width: 8 ***
  *** c-basic-offset: 8 ***
  *** End: ***
  */
