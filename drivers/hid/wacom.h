/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
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

#ifndef WACOM_H
#define WACOM_H

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/hid.h>
#include <linux/kfifo.h>
#include <linux/leds.h>
#include <linux/usb/input.h>
#include <linux/power_supply.h>
#include <linux/timer.h>
#include <asm/unaligned.h>

/*
 * Version Information
 */
#define DRIVER_VERSION "v2.00"
#define DRIVER_AUTHOR "Vojtech Pavlik <vojtech@ucw.cz>"
#define DRIVER_DESC "USB Wacom tablet driver"

#define USB_VENDOR_ID_WACOM	0x056a
#define USB_VENDOR_ID_LENOVO	0x17ef

enum wacom_worker {
	WACOM_WORKER_WIRELESS,
	WACOM_WORKER_BATTERY,
	WACOM_WORKER_REMOTE,
	WACOM_WORKER_MODE_CHANGE,
};

struct wacom;

struct wacom_led {
	struct led_classdev cdev;
	struct led_trigger trigger;
	struct wacom *wacom;
	unsigned int group;
	unsigned int id;
	u8 llv;
	u8 hlv;
	bool held;
};

struct wacom_group_leds {
	u8 select; /* status led selector (0..3) */
	struct wacom_led *leds;
	unsigned int count;
	struct device *dev;
};

struct wacom_battery {
	struct wacom *wacom;
	struct power_supply_desc bat_desc;
	struct power_supply *battery;
	char bat_name[WACOM_NAME_MAX];
	int bat_status;
	int battery_capacity;
	int bat_charging;
	int bat_connected;
	int ps_connected;
};

struct wacom_remote {
	spinlock_t remote_lock;
	struct kfifo remote_fifo;
	struct kobject *remote_dir;
	struct {
		struct attribute_group group;
		u32 serial;
		struct input_dev *input;
		bool registered;
		struct wacom_battery battery;
	} remotes[WACOM_MAX_REMOTES];
};

struct wacom {
	struct usb_device *usbdev;
	struct usb_interface *intf;
	struct wacom_wac wacom_wac;
	struct hid_device *hdev;
	struct mutex lock;
	struct work_struct wireless_work;
	struct work_struct battery_work;
	struct work_struct remote_work;
	struct delayed_work init_work;
	struct wacom_remote *remote;
	struct work_struct mode_change_work;
	struct timer_list idleprox_timer;
	bool generic_has_leds;
	struct wacom_leds {
		struct wacom_group_leds *groups;
		unsigned int count;
		u8 llv;       /* status led brightness no button (1..127) */
		u8 hlv;       /* status led brightness button pressed (1..127) */
		u8 img_lum;   /* OLED matrix display brightness */
		u8 max_llv;   /* maximum brightness of LED (llv) */
		u8 max_hlv;   /* maximum brightness of LED (hlv) */
	} led;
	struct wacom_battery battery;
	bool resources;
};

static inline void wacom_schedule_work(struct wacom_wac *wacom_wac,
				       enum wacom_worker which)
{
	struct wacom *wacom = container_of(wacom_wac, struct wacom, wacom_wac);

	switch (which) {
	case WACOM_WORKER_WIRELESS:
		schedule_work(&wacom->wireless_work);
		break;
	case WACOM_WORKER_BATTERY:
		schedule_work(&wacom->battery_work);
		break;
	case WACOM_WORKER_REMOTE:
		schedule_work(&wacom->remote_work);
		break;
	case WACOM_WORKER_MODE_CHANGE:
		schedule_work(&wacom->mode_change_work);
		break;
	}
}

/*
 * Convert a signed 32-bit integer to an unsigned n-bit integer. Undoes
 * the normally-helpful work of 'hid_snto32' for fields that use signed
 * ranges for questionable reasons.
 */
static inline __u32 wacom_s32tou(s32 value, __u8 n)
{
	switch (n) {
	case 8:  return ((__u8)value);
	case 16: return ((__u16)value);
	case 32: return ((__u32)value);
	}
	return value & (1 << (n - 1)) ? value & (~(~0U << n)) : value;
}

extern const struct hid_device_id wacom_ids[];

void wacom_wac_irq(struct wacom_wac *wacom_wac, size_t len);
void wacom_setup_device_quirks(struct wacom *wacom);
int wacom_setup_pen_input_capabilities(struct input_dev *input_dev,
				   struct wacom_wac *wacom_wac);
int wacom_setup_touch_input_capabilities(struct input_dev *input_dev,
				   struct wacom_wac *wacom_wac);
int wacom_setup_pad_input_capabilities(struct input_dev *input_dev,
				       struct wacom_wac *wacom_wac);
void wacom_wac_usage_mapping(struct hid_device *hdev,
		struct hid_field *field, struct hid_usage *usage);
void wacom_wac_event(struct hid_device *hdev, struct hid_field *field,
		struct hid_usage *usage, __s32 value);
void wacom_wac_report(struct hid_device *hdev, struct hid_report *report);
void wacom_battery_work(struct work_struct *work);
enum led_brightness wacom_leds_brightness_get(struct wacom_led *led);
struct wacom_led *wacom_led_find(struct wacom *wacom, unsigned int group,
				 unsigned int id);
struct wacom_led *wacom_led_next(struct wacom *wacom, struct wacom_led *cur);
int wacom_equivalent_usage(int usage);
int wacom_initialize_leds(struct wacom *wacom);
void wacom_idleprox_timeout(struct timer_list *list);
#endif
