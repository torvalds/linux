// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2018 Rockchip Electronics Co. Ltd. */

#include <linux/kernel.h>
#include <linux/mutex.h>

#include "sfc.h"
#include "sfc_nor.h"
#include "rkflash_api.h"

#define VENDOR_PART_NUM			4

#define	FLASH_VENDOR_PART_START		8
#define FLASH_VENDOR_PART_SIZE		8
#define FLASH_VENDOR_ITEM_NUM		62
#define	FLASH_VENDOR_PART_END		\
	(FLASH_VENDOR_PART_START +\
	FLASH_VENDOR_PART_SIZE * VENDOR_PART_NUM - 1)

struct SFNOR_DEV sfnor_dev;

/* SFNOR_DEV sfnor_dev is in the sfc_nor.h */
int spi_flash_init(void __iomem	*reg_addr)
{
	int ret;

	sfc_init(reg_addr);
	ret = snor_init(&sfnor_dev);

	return ret;
}
EXPORT_SYMBOL_GPL(spi_flash_init);

void spi_flash_read_id(u8 chip_sel, void *buf)
{
	snor_read_id(buf);
}
EXPORT_SYMBOL_GPL(spi_flash_read_id);

int snor_read_lba(u32 sec, u32 n_sec, void *p_data)
{
	int ret = 0;
	u32 count, offset;
	char *buf;

	if (sec + n_sec - 1 < FLASH_VENDOR_PART_START ||
	    sec > FLASH_VENDOR_PART_END) {
		ret = snor_read(&sfnor_dev, sec, n_sec, p_data);
	} else {
		memset(p_data, 0, 512 * n_sec);
		if (sec < FLASH_VENDOR_PART_START) {
			count = FLASH_VENDOR_PART_START - sec;
			buf = p_data;
			ret = snor_read(&sfnor_dev, sec, count, buf);
		}
		if ((sec + n_sec - 1) > FLASH_VENDOR_PART_END) {
			count = sec + n_sec - 1 - FLASH_VENDOR_PART_END;
			offset = FLASH_VENDOR_PART_END - sec + 1;
			buf = p_data + offset * 512;
			ret = snor_read(&sfnor_dev,
					FLASH_VENDOR_PART_END + 1,
					count, buf);
		}
	}

	return (u32)ret == n_sec ? 0 : ret;
}

int snor_write_lba(u32 sec, u32 n_sec, void *p_data)
{
	int ret = 0;

	ret = snor_write(&sfnor_dev, sec, n_sec, p_data);

	return (u32)ret == n_sec ? 0 : ret;
}

int snor_vendor_read(u32 sec, u32 n_sec, void *p_data)
{
	int ret = 0;

	ret = snor_read(&sfnor_dev, sec, n_sec, p_data);

	return (u32)ret == n_sec ? 0 : ret;
}

int snor_vendor_write(u32 sec, u32 n_sec, void *p_data)
{
	int ret = 0;

	ret = snor_write(&sfnor_dev, sec, n_sec, p_data);

	return (u32)ret == n_sec ? 0 : ret;
}

int snor_gc(void)
{
	return 0;
}

unsigned int snor_capacity(void)
{
	return snor_get_capacity(&sfnor_dev);
}

void snor_deinit(void)
{
	snor_disable_QE(&sfnor_dev);
	snor_reset_device();
}

int snor_resume(void __iomem *reg_addr)
{
	return snor_init(&sfnor_dev);
}
