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

#include <linux/fb.h>
#include <linux/backlight.h>

#include "hid-picolcd.h"

static int picolcd_get_brightness(struct backlight_device *bdev)
{
	struct picolcd_data *data = bl_get_data(bdev);
	return data->lcd_brightness;
}

static int picolcd_set_brightness(struct backlight_device *bdev)
{
	struct picolcd_data *data = bl_get_data(bdev);
	struct hid_report *report = picolcd_out_report(REPORT_BRIGHTNESS, data->hdev);
	unsigned long flags;

	if (!report || report->maxfield != 1 || report->field[0]->report_count != 1)
		return -ENODEV;

	data->lcd_brightness = bdev->props.brightness & 0x0ff;
	data->lcd_power      = bdev->props.power;
	spin_lock_irqsave(&data->lock, flags);
	hid_set_field(report->field[0], 0, data->lcd_power == FB_BLANK_UNBLANK ? data->lcd_brightness : 0);
	if (!(data->status & PICOLCD_FAILED))
		hid_hw_request(data->hdev, report, HID_REQ_SET_REPORT);
	spin_unlock_irqrestore(&data->lock, flags);
	return 0;
}

static int picolcd_check_bl_fb(struct backlight_device *bdev, struct fb_info *fb)
{
	return fb && fb == picolcd_fbinfo((struct picolcd_data *)bl_get_data(bdev));
}

static const struct backlight_ops picolcd_blops = {
	.update_status  = picolcd_set_brightness,
	.get_brightness = picolcd_get_brightness,
	.check_fb       = picolcd_check_bl_fb,
};

int picolcd_init_backlight(struct picolcd_data *data, struct hid_report *report)
{
	struct device *dev = &data->hdev->dev;
	struct backlight_device *bdev;
	struct backlight_properties props;
	if (!report)
		return -ENODEV;
	if (report->maxfield != 1 || report->field[0]->report_count != 1 ||
			report->field[0]->report_size != 8) {
		dev_err(dev, "unsupported BRIGHTNESS report");
		return -EINVAL;
	}

	memset(&props, 0, sizeof(props));
	props.type = BACKLIGHT_RAW;
	props.max_brightness = 0xff;
	bdev = backlight_device_register(dev_name(dev), dev, data,
			&picolcd_blops, &props);
	if (IS_ERR(bdev)) {
		dev_err(dev, "failed to register backlight\n");
		return PTR_ERR(bdev);
	}
	bdev->props.brightness     = 0xff;
	data->lcd_brightness       = 0xff;
	data->backlight            = bdev;
	picolcd_set_brightness(bdev);
	return 0;
}

void picolcd_exit_backlight(struct picolcd_data *data)
{
	struct backlight_device *bdev = data->backlight;

	data->backlight = NULL;
	if (bdev)
		backlight_device_unregister(bdev);
}

int picolcd_resume_backlight(struct picolcd_data *data)
{
	if (!data->backlight)
		return 0;
	return picolcd_set_brightness(data->backlight);
}

#ifdef CONFIG_PM
void picolcd_suspend_backlight(struct picolcd_data *data)
{
	int bl_power = data->lcd_power;
	if (!data->backlight)
		return;

	data->backlight->props.power = FB_BLANK_POWERDOWN;
	picolcd_set_brightness(data->backlight);
	data->lcd_power = data->backlight->props.power = bl_power;
}
#endif /* CONFIG_PM */

