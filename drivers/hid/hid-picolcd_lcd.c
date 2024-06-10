// SPDX-License-Identifier: GPL-2.0-only
/***************************************************************************
 *   Copyright (C) 2010-2012 by Bruno Prémont <bonbons@linux-vserver.org>  *
 *                                                                         *
 *   Based on Logitech G13 driver (v0.4)                                   *
 *     Copyright (C) 2009 by Rick L. Vinyard, Jr. <rvinyard@cs.nmsu.edu>   *
 *                                                                         *
 ***************************************************************************/

#include <linux/hid.h>

#include <linux/fb.h>
#include <linux/lcd.h>

#include "hid-picolcd.h"

/*
 * lcd class device
 */
static int picolcd_get_contrast(struct lcd_device *ldev)
{
	struct picolcd_data *data = lcd_get_data(ldev);
	return data->lcd_contrast;
}

static int picolcd_set_contrast(struct lcd_device *ldev, int contrast)
{
	struct picolcd_data *data = lcd_get_data(ldev);
	struct hid_report *report = picolcd_out_report(REPORT_CONTRAST, data->hdev);
	unsigned long flags;

	if (!report || report->maxfield != 1 || report->field[0]->report_count != 1)
		return -ENODEV;

	data->lcd_contrast = contrast & 0x0ff;
	spin_lock_irqsave(&data->lock, flags);
	hid_set_field(report->field[0], 0, data->lcd_contrast);
	if (!(data->status & PICOLCD_FAILED))
		hid_hw_request(data->hdev, report, HID_REQ_SET_REPORT);
	spin_unlock_irqrestore(&data->lock, flags);
	return 0;
}

static int picolcd_check_lcd_fb(struct lcd_device *ldev, struct fb_info *fb)
{
	return fb && fb == picolcd_fbinfo((struct picolcd_data *)lcd_get_data(ldev));
}

static const struct lcd_ops picolcd_lcdops = {
	.get_contrast   = picolcd_get_contrast,
	.set_contrast   = picolcd_set_contrast,
	.check_fb       = picolcd_check_lcd_fb,
};

int picolcd_init_lcd(struct picolcd_data *data, struct hid_report *report)
{
	struct device *dev = &data->hdev->dev;
	struct lcd_device *ldev;

	if (!report)
		return -ENODEV;
	if (report->maxfield != 1 || report->field[0]->report_count != 1 ||
			report->field[0]->report_size != 8) {
		dev_err(dev, "unsupported CONTRAST report");
		return -EINVAL;
	}

	ldev = lcd_device_register(dev_name(dev), dev, data, &picolcd_lcdops);
	if (IS_ERR(ldev)) {
		dev_err(dev, "failed to register LCD\n");
		return PTR_ERR(ldev);
	}
	ldev->props.max_contrast = 0x0ff;
	data->lcd_contrast = 0xe5;
	data->lcd = ldev;
	picolcd_set_contrast(ldev, 0xe5);
	return 0;
}

void picolcd_exit_lcd(struct picolcd_data *data)
{
	struct lcd_device *ldev = data->lcd;

	data->lcd = NULL;
	lcd_device_unregister(ldev);
}

int picolcd_resume_lcd(struct picolcd_data *data)
{
	if (!data->lcd)
		return 0;
	return picolcd_set_contrast(data->lcd, data->lcd_contrast);
}

