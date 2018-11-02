// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2018 Rockchip Electronics Co. Ltd. */

#include <linux/kernel.h>
#include <linux/mutex.h>

#include "rkflash_api.h"
#include "rk_sftl.h"
#include "sfc.h"
#include "sfc_nand.h"

int snand_init(void __iomem *reg_addr)
{
	int ret;

	sfc_init(reg_addr);
	ret = sfc_nand_init();
	if (ret == 0)
		ret = sftl_init();

	return ret;
}
EXPORT_SYMBOL_GPL(snand_init);

unsigned int snand_get_capacity(void)
{
	return sftl_get_density();
}

int snand_write(u32 sec, u32 n_sec, void *p_data)
{
	return sftl_write(sec, n_sec, p_data);
}

int snand_read(u32 sec, u32 n_sec, void *p_data)
{
	return sftl_read(sec, n_sec, p_data);
}

int snand_vendor_read(u32 sec, u32 n_sec, void *p_data)
{
	return sftl_vendor_read(sec, n_sec, p_data);
}

int snand_vendor_write(u32 sec, u32 n_sec, void *p_data)
{
	return sftl_vendor_write(sec, n_sec, p_data);
}

int snand_gc(void)
{
	return sftl_gc();
}

int snand_discard(u32 sec, u32 n_sec)
{
	return sftl_discard(sec, n_sec);
}

void snand_deinit(void)
{
	sftl_deinit();
	sfc_nand_deinit();
}

int snand_resume(void __iomem *reg_addr)
{
	sfc_init(reg_addr);
	return sfc_nand_init();
}
