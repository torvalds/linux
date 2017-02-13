/*
 * Asus PC WMI hotkey driver
 *
 * Copyright(C) 2010 Intel Corporation.
 * Copyright(C) 2010-2011 Corentin Chary <corentin.chary@gmail.com>
 *
 * Portions based on wistron_btns.c:
 * Copyright (C) 2005 Miloslav Trmac <mitr@volny.cz>
 * Copyright (C) 2005 Bernhard Rosenkraenzer <bero@arklinux.org>
 * Copyright (C) 2005 Dmitry Torokhov <dtor@mail.ru>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef _ASUS_WMI_H_
#define _ASUS_WMI_H_

#include <linux/platform_device.h>
#include <linux/i8042.h>

#define ASUS_WMI_KEY_IGNORE (-1)
#define ASUS_WMI_BRN_DOWN	0x20
#define ASUS_WMI_BRN_UP		0x2f

struct module;
struct key_entry;
struct asus_wmi;

struct quirk_entry {
	bool no_rfkill;
	bool hotplug_wireless;
	bool scalar_panel_brightness;
	bool store_backlight_power;
	bool wmi_backlight_power;
	bool wmi_backlight_native;
	int wapf;
	/*
	 * For machines with AMD graphic chips, it will send out WMI event
	 * and ACPI interrupt at the same time while hitting the hotkey.
	 * To simplify the problem, we just have to ignore the WMI event,
	 * and let the ACPI interrupt to send out the key event.
	 */
	int no_display_toggle;
	u32 xusb2pr;

	bool (*i8042_filter)(unsigned char data, unsigned char str,
			     struct serio *serio);
};

struct asus_wmi_driver {
	int			brightness;
	int			panel_power;
	int			wlan_ctrl_by_user;

	const char		*name;
	struct module		*owner;

	const char		*event_guid;

	const struct key_entry	*keymap;
	const char		*input_name;
	const char		*input_phys;
	struct quirk_entry	*quirks;
	/* Returns new code, value, and autorelease values in arguments.
	 * Return ASUS_WMI_KEY_IGNORE in code if event should be ignored. */
	void (*key_filter) (struct asus_wmi_driver *driver, int *code,
			    unsigned int *value, bool *autorelease);

	int (*probe) (struct platform_device *device);
	void (*detect_quirks) (struct asus_wmi_driver *driver);

	struct platform_driver	platform_driver;
	struct platform_device *platform_device;
};

int asus_wmi_register_driver(struct asus_wmi_driver *driver);
void asus_wmi_unregister_driver(struct asus_wmi_driver *driver);

#endif /* !_ASUS_WMI_H_ */
