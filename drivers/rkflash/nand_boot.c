// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2018 Rockchip Electronics Co. Ltd. */

#include <linux/kernel.h>
#include <linux/mutex.h>

#include "flash.h"
#include "rkflash_api.h"
#include "rk_sftl.h"

static int sftl_flash_init(void __iomem *reg_addr)
{
	int ret;

	ret = nandc_flash_init(reg_addr);
	if (ret == 0)
		ret = sftl_init();

	return ret;
}

static unsigned int sftl_flash_get_capacity(void)
{
	return sftl_get_density();
}

static int sftl_flash_read(u32 sec, u32 n_sec, void *p_data)
{
	return sftl_read(sec, n_sec, p_data);
}

static int sftl_flash_write(u32 sec, u32 n_sec, void *p_data)
{
	return sftl_write(sec, n_sec, p_data);
}

static int sftl_flash_vendor_read(u32 sec, u32 n_sec, void *p_data)
{
	return sftl_vendor_read(sec, n_sec, p_data);
}

static int sftl_flash_vendor_write(u32 sec, u32 n_sec, void *p_data)
{
	return sftl_vendor_write(sec, n_sec, p_data);
}

static int sftl_flash_gc(void)
{
	return sftl_gc();
}

static int sftl_flash_discard(u32 sec, u32 n_sec)
{
	return sftl_discard(sec, n_sec);
}

static void sftl_flash_deinit(void)
{
	u8 chip_sel = 0;

	sftl_deinit();
	nandc_flash_reset(chip_sel);
}

static int sftl_flash_resume(void __iomem *reg_addr)
{
	return nandc_flash_init(reg_addr);
}

const struct flash_boot_ops nandc_nand_ops = {
	sftl_flash_init,
	sftl_flash_read,
	sftl_flash_write,
	sftl_flash_get_capacity,
	sftl_flash_deinit,
	sftl_flash_resume,
	sftl_flash_vendor_read,
	sftl_flash_vendor_write,
	sftl_flash_gc,
	sftl_flash_discard,
};

