/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __HID_PIDFF_H
#define __HID_PIDFF_H

#include <linux/hid.h>

/* HID PIDFF quirks */

/* Delay field (0xA7) missing. Skip it during set effect report upload */
#define HID_PIDFF_QUIRK_MISSING_DELAY		BIT(0)

/* Missing Paramter block offset (0x23). Skip it during SET_CONDITION upload */
#define HID_PIDFF_QUIRK_MISSING_PBO		BIT(1)

/* Initialise device control field even if logical_minimum != 1 */
#define HID_PIDFF_QUIRK_PERMISSIVE_CONTROL	BIT(2)

/* Use fixed 0x4000 direction during SET_EFFECT report upload */
#define HID_PIDFF_QUIRK_FIX_CONDITIONAL_DIRECTION	BIT(3)

/* Force all periodic effects to be uploaded as SINE */
#define HID_PIDFF_QUIRK_PERIODIC_SINE_ONLY	BIT(4)

#ifdef CONFIG_HID_PID
int hid_pidff_init(struct hid_device *hid);
int hid_pidff_init_with_quirks(struct hid_device *hid, u32 initial_quirks);
#else
#define hid_pidff_init NULL
#define hid_pidff_init_with_quirks NULL
#endif

#endif
