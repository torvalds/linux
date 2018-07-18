// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2018 Rockchip Electronics Co. Ltd. */

#include <linux/kernel.h>
#include <linux/mutex.h>

#include "flash.h"
#include "rkflash_api.h"
#include "rk_sftl.h"

int sftl_flash_init(void __iomem *reg_addr)
{
	int ret;

	ret = nandc_flash_init(reg_addr);
	if (ret == 0)
		ret = sftl_init();

	return ret;
}
EXPORT_SYMBOL_GPL(sftl_flash_init);

void sftl_flash_read_id(u8 chip_sel, void *buf)
{
	nandc_flash_get_id(chip_sel, buf);
}
EXPORT_SYMBOL_GPL(sftl_flash_read_id);

unsigned int sftl_flash_get_capacity(void)
{
	return sftl_get_density();
}

int sftl_flash_read(u32 sec, u32 n_sec, void *p_data)
{
	return sftl_read(sec, n_sec, p_data);
}

int sftl_flash_write(u32 sec, u32 n_sec, void *p_data)
{
	return sftl_write(sec, n_sec, p_data);
}

int sftl_flash_vendor_read(u32 sec, u32 n_sec, void *p_data)
{
	return sftl_vendor_read(sec, n_sec, p_data);
}

int sftl_flash_vendor_write(u32 sec, u32 n_sec, void *p_data)
{
	return sftl_vendor_write(sec, n_sec, p_data);
}

void sftl_flash_deinit(void)
{
	u8 chip_sel = 0;

	sftl_deinit();
	nandc_flash_reset(chip_sel);
}

int sftl_flash_resume(void __iomem *reg_addr)
{
	return nandc_flash_init(reg_addr);
}

void sftl_flash_clean_irq(void)
{
}
