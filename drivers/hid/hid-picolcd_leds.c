/***************************************************************************
 *   Copyright (C) 2010-2012 by Bruno Prémont <bonbons@linux-vserver.org>  *
 *                                                                         *
 *   Based on Logitech G13 driver (v0.4)                                   *
 *     Copyright (C) 2009 by Rick L. Vinyard, Jr. <rvinyard@cs.nmsu.edu>   *
 *                                                                         *
 *   This program is free software: you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation, version 2 of the License.               *
 *                                                                         *
 *   This driver is distributed in the hope that it will be useful, but    *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of            *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU      *
 *   General Public License for more details.                              *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this software. If not see <http://www.gnu.org/licenses/>.  *
 ***************************************************************************/

#include <linux/hid.h>
#include <linux/hid-debug.h>
#include <linux/input.h>
#include "hid-ids.h"

#include <linux/fb.h>
#include <linux/vmalloc.h>
#include <linux/backlight.h>
#include <linux/lcd.h>

#include <linux/leds.h>

#include <linux/seq_file.h>
#include <linux/debugfs.h>

#include <linux/completion.h>
#include <linux/uaccess.h>
#include <linux/module.h>

#include "hid-picolcd.h"


void picolcd_leds_set(struct picolcd_data *data)
{
	struct hid_report *report;
	unsigned long flags;

	if (!data->led[0])
		return;
	report = picolcd_out_report(REPORT_LED_STATE, data->hdev);
	if (!report || report->maxfield != 1 || report->field[0]->report_count != 1)
		return;

	spin_lock_irqsave(&data->lock, flags);
	hid_set_field(report->field[0], 0, data->led_state);
	if (!(data->status & PICOLCD_FAILED))
		hid_hw_request(data->hdev, report, HID_REQ_SET_REPORT);
	spin_unlock_irqrestore(&data->lock, flags);
}

static void picolcd_led_set_brightness(struct led_classdev *led_cdev,
			enum led_brightness value)
{
	struct device *dev;
	struct hid_device *hdev;
	struct picolcd_data *data;
	int i, state = 0;

	dev  = led_cdev->dev->parent;
	hdev = to_hid_device(dev);
	data = hid_get_drvdata(hdev);
	if (!data)
		return;
	for (i = 0; i < 8; i++) {
		if (led_cdev != data->led[i])
			continue;
		state = (data->led_state >> i) & 1;
		if (value == LED_OFF && state) {
			data->led_state &= ~(1 << i);
			picolcd_leds_set(data);
		} else if (value != LED_OFF && !state) {
			data->led_state |= 1 << i;
			picolcd_leds_set(data);
		}
		break;
	}
}

static enum led_brightness picolcd_led_get_brightness(struct led_classdev *led_cdev)
{
	struct device *dev;
	struct hid_device *hdev;
	struct picolcd_data *data;
	int i, value = 0;

	dev  = led_cdev->dev->parent;
	hdev = to_hid_device(dev);
	data = hid_get_drvdata(hdev);
	for (i = 0; i < 8; i++)
		if (led_cdev == data->led[i]) {
			value = (data->led_state >> i) & 1;
			break;
		}
	return value ? LED_FULL : LED_OFF;
}

int picolcd_init_leds(struct picolcd_data *data, struct hid_report *report)
{
	struct device *dev = &data->hdev->dev;
	struct led_classdev *led;
	size_t name_sz = strlen(dev_name(dev)) + 8;
	char *name;
	int i, ret = 0;

	if (!report)
		return -ENODEV;
	if (report->maxfield != 1 || report->field[0]->report_count != 1 ||
			report->field[0]->report_size != 8) {
		dev_err(dev, "unsupported LED_STATE report");
		return -EINVAL;
	}

	for (i = 0; i < 8; i++) {
		led = kzalloc(sizeof(struct led_classdev)+name_sz, GFP_KERNEL);
		if (!led) {
			dev_err(dev, "can't allocate memory for LED %d\n", i);
			ret = -ENOMEM;
			goto err;
		}
		name = (void *)(&led[1]);
		snprintf(name, name_sz, "%s::GPO%d", dev_name(dev), i);
		led->name = name;
		led->brightness = 0;
		led->max_brightness = 1;
		led->brightness_get = picolcd_led_get_brightness;
		led->brightness_set = picolcd_led_set_brightness;

		data->led[i] = led;
		ret = led_classdev_register(dev, data->led[i]);
		if (ret) {
			data->led[i] = NULL;
			kfree(led);
			dev_err(dev, "can't register LED %d\n", i);
			goto err;
		}
	}
	return 0;
err:
	for (i = 0; i < 8; i++)
		if (data->led[i]) {
			led = data->led[i];
			data->led[i] = NULL;
			led_classdev_unregister(led);
			kfree(led);
		}
	return ret;
}

void picolcd_exit_leds(struct picolcd_data *data)
{
	struct led_classdev *led;
	int i;

	for (i = 0; i < 8; i++) {
		led = data->led[i];
		data->led[i] = NULL;
		if (!led)
			continue;
		led_classdev_unregister(led);
		kfree(led);
	}
}


