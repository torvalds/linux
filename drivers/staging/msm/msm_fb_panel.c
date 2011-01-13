/* Copyright (c) 2008-2009, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>
#include <linux/string.h>
#include <linux/version.h>
#include <linux/proc_fs.h>
#include <linux/vmalloc.h>
#include <linux/debugfs.h>

#include "msm_fb_panel.h"

int panel_next_on(struct platform_device *pdev)
{
	int ret = 0;
	struct msm_fb_panel_data *pdata;
	struct msm_fb_panel_data *next_pdata;
	struct platform_device *next_pdev;

	pdata = (struct msm_fb_panel_data *)pdev->dev.platform_data;

	if (pdata) {
		next_pdev = pdata->next;
		if (next_pdev) {
			next_pdata =
			    (struct msm_fb_panel_data *)next_pdev->dev.
			    platform_data;
			if ((next_pdata) && (next_pdata->on))
				ret = next_pdata->on(next_pdev);
		}
	}

	return ret;
}

int panel_next_off(struct platform_device *pdev)
{
	int ret = 0;
	struct msm_fb_panel_data *pdata;
	struct msm_fb_panel_data *next_pdata;
	struct platform_device *next_pdev;

	pdata = (struct msm_fb_panel_data *)pdev->dev.platform_data;

	if (pdata) {
		next_pdev = pdata->next;
		if (next_pdev) {
			next_pdata =
			    (struct msm_fb_panel_data *)next_pdev->dev.
			    platform_data;
			if ((next_pdata) && (next_pdata->on))
				ret = next_pdata->off(next_pdev);
		}
	}

	return ret;
}

struct platform_device *msm_fb_device_alloc(struct msm_fb_panel_data *pdata,
						u32 type, u32 id)
{
	struct platform_device *this_dev = NULL;
	char dev_name[16];

	switch (type) {
	case EBI2_PANEL:
		snprintf(dev_name, sizeof(dev_name), "ebi2_lcd");
		break;

	case MDDI_PANEL:
		snprintf(dev_name, sizeof(dev_name), "mddi");
		break;

	case EXT_MDDI_PANEL:
		snprintf(dev_name, sizeof(dev_name), "mddi_ext");
		break;

	case TV_PANEL:
		snprintf(dev_name, sizeof(dev_name), "tvenc");
		break;

	case HDMI_PANEL:
	case LCDC_PANEL:
		snprintf(dev_name, sizeof(dev_name), "lcdc");
		break;

	default:
		return NULL;
	}

	if (pdata != NULL)
		pdata->next = NULL;
	else
		return NULL;

	this_dev =
	    platform_device_alloc(dev_name, ((u32) type << 16) | (u32) id);

	if (this_dev) {
		if (platform_device_add_data
		    (this_dev, pdata, sizeof(struct msm_fb_panel_data))) {
			printk
			    ("msm_fb_device_alloc: platform_device_add_data failed!\n");
			platform_device_put(this_dev);
			return NULL;
		}
	}

	return this_dev;
}
