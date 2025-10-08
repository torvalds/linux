/*
 * drivers/video/fb_ddc.c - DDC/EDID read support.
 *
 *  Copyright (C) 2006 Dennis Munsie <dmunsie@cecropia.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/export.h>
#include <linux/module.h>
#include <linux/fb.h>
#include <linux/i2c-algo-bit.h>
#include <linux/slab.h>

#include "../edid.h"

#define DDC_ADDR	0x50

static unsigned char *fb_do_probe_ddc_edid(struct i2c_adapter *adapter)
{
	unsigned char start = 0x0;
	unsigned char *buf = kmalloc(EDID_LENGTH, GFP_KERNEL);
	struct i2c_msg msgs[] = {
		{
			.addr	= DDC_ADDR,
			.flags	= 0,
			.len	= 1,
			.buf	= &start,
		}, {
			.addr	= DDC_ADDR,
			.flags	= I2C_M_RD,
			.len	= EDID_LENGTH,
			.buf	= buf,
		}
	};

	if (!buf) {
		dev_warn(&adapter->dev, "unable to allocate memory for EDID "
			 "block.\n");
		return NULL;
	}

	if (i2c_transfer(adapter, msgs, 2) == 2)
		return buf;

	dev_warn(&adapter->dev, "unable to read EDID block.\n");
	kfree(buf);
	return NULL;
}

unsigned char *fb_ddc_read(struct i2c_adapter *adapter)
{
	struct i2c_algo_bit_data *algo_data = adapter->algo_data;
	unsigned char *edid = NULL;
	int i, j;

	algo_data->setscl(algo_data->data, 1);

	for (i = 0; i < 3; i++) {
		/* For some old monitors we need the
		 * following process to initialize/stop DDC
		 */
		algo_data->setsda(algo_data->data, 1);
		msleep(13);

		algo_data->setscl(algo_data->data, 1);
		if (algo_data->getscl) {
			for (j = 0; j < 5; j++) {
				msleep(10);
				if (algo_data->getscl(algo_data->data))
					break;
			}
			if (j == 5)
				continue;
		} else {
			udelay(algo_data->udelay);
		}

		algo_data->setsda(algo_data->data, 0);
		msleep(15);
		algo_data->setscl(algo_data->data, 0);
		msleep(15);
		algo_data->setsda(algo_data->data, 1);
		msleep(15);

		/* Do the real work */
		edid = fb_do_probe_ddc_edid(adapter);
		algo_data->setsda(algo_data->data, 0);
		algo_data->setscl(algo_data->data, 0);
		msleep(15);

		algo_data->setscl(algo_data->data, 1);
		if (algo_data->getscl) {
			for (j = 0; j < 10; j++) {
				msleep(10);
				if (algo_data->getscl(algo_data->data))
					break;
			}
		} else {
			udelay(algo_data->udelay);
		}

		algo_data->setsda(algo_data->data, 1);
		msleep(15);
		algo_data->setscl(algo_data->data, 0);
		algo_data->setsda(algo_data->data, 0);
		if (edid)
			break;
	}
	/* Release the DDC lines when done or the Apple Cinema HD display
	 * will switch off
	 */
	algo_data->setsda(algo_data->data, 1);
	algo_data->setscl(algo_data->data, 1);

	return edid;
}

EXPORT_SYMBOL_GPL(fb_ddc_read);

MODULE_AUTHOR("Dennis Munsie <dmunsie@cecropia.com>");
MODULE_DESCRIPTION("DDC/EDID reading support");
MODULE_LICENSE("GPL");
