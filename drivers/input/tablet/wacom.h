/*
 * drivers/input/tablet/wacom.h
 *
 *  USB Wacom Graphire and Wacom Intuos tablet support
 *
 *  Copyright (c) 2000-2004 Vojtech Pavlik	<vojtech@ucw.cz>
 *  Copyright (c) 2000 Andreas Bach Aaen	<abach@stofanet.dk>
 *  Copyright (c) 2000 Clifford Wolf		<clifford@clifford.at>
 *  Copyright (c) 2000 Sam Mosel		<sam.mosel@computer.org>
 *  Copyright (c) 2000 James E. Blair		<corvus@gnu.org>
 *  Copyright (c) 2000 Daniel Egger		<egger@suse.de>
 *  Copyright (c) 2001 Frederic Lepied		<flepied@mandrakesoft.com>
 *  Copyright (c) 2004 Panagiotis Issaris	<panagiotis.issaris@mech.kuleuven.ac.be>
 *  Copyright (c) 2002-2009 Ping Cheng		<pingc@wacom.com>
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
#include <linux/init.h>
#include <linux/usb/input.h>
#include <asm/unaligned.h>

/*
 * Version Information
 */
#define DRIVER_VERSION "v1.50"
#define DRIVER_AUTHOR "Vojtech Pavlik <vojtech@ucw.cz>"
#define DRIVER_DESC "USB Wacom Graphire and Wacom Intuos tablet driver"
#define DRIVER_LICENSE "GPL"

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE(DRIVER_LICENSE);

#define USB_VENDOR_ID_WACOM	0x056a

struct wacom {
	dma_addr_t data_dma;
	struct input_dev *dev;
	struct usb_device *usbdev;
	struct usb_interface *intf;
	struct urb *irq;
	struct wacom_wac *wacom_wac;
	struct mutex lock;
	unsigned int open:1;
	char phys[32];
};

struct wacom_combo {
	struct wacom *wacom;
	struct urb *urb;
};

extern int wacom_wac_irq(struct wacom_wac * wacom_wac, void * wcombo);
extern void wacom_report_abs(void *wcombo, unsigned int abs_type, int abs_data);
extern void wacom_report_rel(void *wcombo, unsigned int rel_type, int rel_data);
extern void wacom_report_key(void *wcombo, unsigned int key_type, int key_data);
extern void wacom_input_event(void *wcombo, unsigned int type, unsigned int code, int value);
extern void wacom_input_sync(void *wcombo);
extern void wacom_init_input_dev(struct input_dev *input_dev, struct wacom_wac *wacom_wac);
extern void input_dev_g4(struct input_dev *input_dev, struct wacom_wac *wacom_wac);
extern void input_dev_g(struct input_dev *input_dev, struct wacom_wac *wacom_wac);
extern void input_dev_i3s(struct input_dev *input_dev, struct wacom_wac *wacom_wac);
extern void input_dev_i3(struct input_dev *input_dev, struct wacom_wac *wacom_wac);
extern void input_dev_i(struct input_dev *input_dev, struct wacom_wac *wacom_wac);
extern void input_dev_pl(struct input_dev *input_dev, struct wacom_wac *wacom_wac);
extern void input_dev_pt(struct input_dev *input_dev, struct wacom_wac *wacom_wac);
extern void input_dev_mo(struct input_dev *input_dev, struct wacom_wac *wacom_wac);
extern void input_dev_bee(struct input_dev *input_dev, struct wacom_wac *wacom_wac);
extern __u16 wacom_le16_to_cpu(unsigned char *data);
extern __u16 wacom_be16_to_cpu(unsigned char *data);
extern struct wacom_features *get_wacom_feature(const struct usb_device_id *id);
extern const struct usb_device_id *get_device_table(void);

#endif
