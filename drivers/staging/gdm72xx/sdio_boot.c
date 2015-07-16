/*
 * Copyright (c) 2012 GCT Semiconductor, Inc. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include <linux/mmc/core.h>
#include <linux/mmc/card.h>
#include <linux/mmc/sdio_func.h>

#include <linux/firmware.h>

#include "gdm_sdio.h"
#include "sdio_boot.h"

#define TYPE_A_HEADER_SIZE	4
#define TYPE_A_LOOKAHEAD_SIZE   16
#define YMEM0_SIZE		0x8000	/* 32kbytes */
#define DOWNLOAD_SIZE		(YMEM0_SIZE - TYPE_A_HEADER_SIZE)

#define FW_DIR			"gdm72xx/"
#define FW_KRN			"gdmskrn.bin"
#define FW_RFS			"gdmsrfs.bin"

static u8 *tx_buf;

static int ack_ready(struct sdio_func *func)
{
	unsigned long wait = jiffies + HZ;
	u8 val;
	int ret;

	while (time_before(jiffies, wait)) {
		val = sdio_readb(func, 0x13, &ret);
		if (val & 0x01)
			return 1;
		schedule();
	}

	return 0;
}

static int download_image(struct sdio_func *func, const char *img_name)
{
	int ret = 0, len, pno;
	u8 *buf = tx_buf;
	loff_t pos = 0;
	int img_len;
	const struct firmware *firm;

	ret = request_firmware(&firm, img_name, &func->dev);
	if (ret < 0) {
		dev_err(&func->dev,
			"requesting firmware %s failed with error %d\n",
			img_name, ret);
		return ret;
	}

	buf = kmalloc(DOWNLOAD_SIZE + TYPE_A_HEADER_SIZE, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	img_len = firm->size;

	if (img_len <= 0) {
		ret = -1;
		goto out;
	}

	pno = 0;
	while (img_len > 0) {
		if (img_len > DOWNLOAD_SIZE) {
			len = DOWNLOAD_SIZE;
			buf[3] = 0;
		} else {
			len = img_len; /* the last packet */
			buf[3] = 2;
		}

		buf[0] = len & 0xff;
		buf[1] = (len >> 8) & 0xff;
		buf[2] = (len >> 16) & 0xff;

		memcpy(buf+TYPE_A_HEADER_SIZE, firm->data + pos, len);
		ret = sdio_memcpy_toio(func, 0, buf, len + TYPE_A_HEADER_SIZE);
		if (ret < 0) {
			dev_err(&func->dev,
				"send image error: packet number = %d ret = %d\n",
				pno, ret);
			goto out;
		}

		if (buf[3] == 2)	/* The last packet */
			break;
		if (!ack_ready(func)) {
			ret = -EIO;
			dev_err(&func->dev, "Ack is not ready.\n");
			goto out;
		}
		ret = sdio_memcpy_fromio(func, buf, 0, TYPE_A_LOOKAHEAD_SIZE);
		if (ret < 0) {
			dev_err(&func->dev,
				"receive ack error: packet number = %d ret = %d\n",
				pno, ret);
			goto out;
		}
		sdio_writeb(func, 0x01, 0x13, &ret);
		sdio_writeb(func, 0x00, 0x10, &ret);	/* PCRRT */

		img_len -= DOWNLOAD_SIZE;
		pos += DOWNLOAD_SIZE;
		pno++;
	}

out:
	kfree(buf);
	return ret;
}

int sdio_boot(struct sdio_func *func)
{
	int ret;
	const char *krn_name = FW_DIR FW_KRN;
	const char *rfs_name = FW_DIR FW_RFS;

	tx_buf = kmalloc(YMEM0_SIZE, GFP_KERNEL);
	if (tx_buf == NULL)
		return -ENOMEM;

	ret = download_image(func, krn_name);
	if (ret)
		goto restore_fs;
	dev_info(&func->dev, "GCT: Kernel download success.\n");

	ret = download_image(func, rfs_name);
	if (ret)
		goto restore_fs;
	dev_info(&func->dev, "GCT: Filesystem download success.\n");

restore_fs:
	kfree(tx_buf);
	return ret;
}
