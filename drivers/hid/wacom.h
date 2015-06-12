/*
 * drivers/input/tablet/wacom.h
 *
 *  USB Wacom tablet support
 *
 *  Copyright (c) 2000-2004 Vojtech Pavlik	<vojtech@ucw.cz>
 *  Copyright (c) 2000 Andreas Bach Aaen	<abach@stofanet.dk>
 *  Copyright (c) 2000 Clifford Wolf		<clifford@clifford.at>
 *  Copyright (c) 2000 Sam Mosel		<sam.mosel@computer.org>
 *  Copyright (c) 2000 James E. Blair		<corvus@gnu.org>
 *  Copyright (c) 2000 Daniel Egger		<egger@suse.de>
 *  Copyright (c) 2001 Frederic Lepied		<flepied@mandrakesoft.com>
 *  Copyright (c) 2004 Panagiotis Issaris	<panagiotis.issaris@mech.kuleuven.ac.be>
 *  Copyright (c) 2002-2011 Ping Cheng		<pingc@wacom.com>
 *  Copyright (c) 2014 Benjamin Tissoires	<benjamin.tissoires@redhat.com>
 *
 *  ChangeLog:
 *      v0.1 (vp)  - Initial release
 *      v0.2 (aba) - Support for all buttons / combinations
 *      v0.3 (vp)  - Support for Intuos added
 *	v0.4 (sm)  - Support for more Intuos models, menustrip
 *			relative mode, proximity.
 *	v0.5 (vp)  - Big cleanup, nifty features removed,
 *			they belong in userspace
 *	v1.8 (vp)  - Submit URB only when operating, moved to CVS,
 *			use input_report_key instead of report_btn and
 *			other cleanups
 *	v1.11 (vp) - Add URB ->dev setting for new kernels
 *	v1.11 (jb) - Add support for the 4D Mouse & Lens
 *	v1.12 (de) - Add support for two more inking pen IDs
 *	v1.14 (vp) - Use new USB device id probing scheme.
 *		     Fix Wacom Graphire mouse wheel
 *	v1.18 (vp) - Fix mouse wheel direction
 *		     Make mouse relative
 *      v1.20 (fl) - Report tool id for Intuos devices
 *                 - Multi tools support
 *                 - Corrected Intuos protocol decoding (airbrush, 4D mouse, lens cursor...)
 *                 - Add PL models support
 *		   - Fix Wacom Graphire mouse wheel again
 *	v1.21 (vp) - Removed protocol descriptions
 *		   - Added MISC_SERIAL for tool serial numbers
 *	      (gb) - Identify version on module load.
 *    v1.21.1 (fl) - added Graphire2 support
 *    v1.21.2 (fl) - added Intuos2 support
 *                 - added all the PL ids
 *    v1.21.3 (fl) - added another eraser id from Neil Okamoto
 *                 - added smooth filter for Graphire from Peri Hankey
 *                 - added PenPartner support from Olaf van Es
 *                 - new tool ids from Ole Martin Bjoerndalen
 *	v1.29 (pc) - Add support for more tablets
 *		   - Fix pressure reporting
 *	v1.30 (vp) - Merge 2.4 and 2.5 drivers
 *		   - Since 2.5 now has input_sync(), remove MSC_SERIAL abuse
 *		   - Cleanups here and there
 *    v1.30.1 (pi) - Added Graphire3 support
 *	v1.40 (pc) - Add support for several new devices, fix eraser reporting, ...
 *	v1.43 (pc) - Added support for Cintiq 21UX
 *		   - Fixed a Graphire bug
 *		   - Merged wacom_intuos3_irq into wacom_intuos_irq
 *	v1.44 (pc) - Added support for Graphire4, Cintiq 710, Intuos3 6x11, etc.
 *		   - Report Device IDs
 *      v1.45 (pc) - Added support for DTF 521, Intuos3 12x12 and 12x19
 *                 - Minor data report fix
 *      v1.46 (pc) - Split wacom.c into wacom_sys.c and wacom_wac.c,
 *		   - where wacom_sys.c deals with system specific code,
 *		   - and wacom_wac.c deals with Wacom specific code
 *		   - Support Intuos3 4x6
 *      v1.47 (pc) - Added support for Bamboo
 *      v1.48 (pc) - Added support for Bamboo1, BambooFun, and Cintiq 12WX
 *      v1.49 (pc) - Added support for USB Tablet PC (0x90, 0x93, and 0x9A)
 *      v1.50 (pc) - Fixed a TabletPC touch bug in 2.6.28
 *      v1.51 (pc) - Added support for Intuos4
 *      v1.52 (pc) - Query Wacom data upon system resume
 *                 - add defines for features->type
 *                 - add new devices (0x9F, 0xE2, and 0XE3)
 *      v2.00 (bt) - conversion to a HID driver
 *                 - integration of the Bluetooth devices
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef WACOM_H
#define WACOM_H
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/hid.h>
#include <linux/usb/input.h>
#include <linux/power_supply.h>
#include <asm/unaligned.h>

/*
 * Version Information
 */
#define DRIVER_VERSION "v2.00"
#define DRIVER_AUTHOR "Vojtech Pavlik <vojtech@ucw.cz>"
#define DRIVER_DESC "USB Wacom tablet driver"
#define DRIVER_LICENSE "GPL"

#define USB_VENDOR_ID_WACOM	0x056a
#define USB_VENDOR_ID_LENOVO	0x17ef

struct wacom {
	struct usb_device *usbdev;
	struct usb_interface *intf;
	struct wacom_wac wacom_wac;
	struct hid_device *hdev;
	struct mutex lock;
	struct work_struct work;
	struct wacom_led {
		u8 select[2]; /* status led selector (0..3) */
		u8 llv;       /* status led brightness no button (1..127) */
		u8 hlv;       /* status led brightness button pressed (1..127) */
		u8 img_lum;   /* OLED matrix display brightness */
	} led;
	bool led_initialized;
	struct power_supply *battery;
	struct power_supply *ac;
	struct power_supply_desc battery_desc;
	struct power_supply_desc ac_desc;
};

static inline void wacom_schedule_work(struct wacom_wac *wacom_wac)
{
	struct wacom *wacom = container_of(wacom_wac, struct wacom, wacom_wac);
	schedule_work(&wacom->work);
}

extern const struct hid_device_id wacom_ids[];

void wacom_wac_irq(struct wacom_wac *wacom_wac, size_t len);
void wacom_setup_device_quirks(struct wacom_features *features);
int wacom_setup_pentouch_input_capabilities(struct input_dev *input_dev,
				   struct wacom_wac *wacom_wac);
int wacom_setup_pad_input_capabilities(struct input_dev *input_dev,
				       struct wacom_wac *wacom_wac);
void wacom_wac_usage_mapping(struct hid_device *hdev,
		struct hid_field *field, struct hid_usage *usage);
int wacom_wac_event(struct hid_device *hdev, struct hid_field *field,
		struct hid_usage *usage, __s32 value);
void wacom_wac_report(struct hid_device *hdev, struct hid_report *report);
void wacom_battery_work(struct work_struct *work);
#endif
