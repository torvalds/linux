// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2018 Rockchip Electronics Co. Ltd. */

#include <linux/kernel.h>
#include <linux/mutex.h>

#include "rk_sftl.h"
#include "sfc_nand.h"
#include "sfc_nand_boot.h"
#include "typedef.h"

int snand_init(void __iomem *reg_addr)
{
	int ret;

	ret = sfc_nand_init(reg_addr);
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

void snand_deinit(void)
{
	sftl_deinit();
	sfc_nand_deinit();
}

int snand_resume(void __iomem *reg_addr)
{
	return sfc_nand_init(reg_addr);
}
