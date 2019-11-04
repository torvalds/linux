// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2018 Rockchip Electronics Co. Ltd. */

#include <linux/kernel.h>

#include "rkflash_api.h"
#include "rk_sftl.h"

struct SFNAND_DEV *sfnand_dev;

static int snand_init(void __iomem *reg_addr)
{
	int ret;

	sfc_init(reg_addr);
	ret = sfc_nand_init();
	if (ret == 0) {
		sfnand_dev = sfc_nand_get_private_dev();
#ifndef CONFIG_RK_SFC_NAND_MTD
		sfc_nand_ftl_ops_init();
		ret = sftl_init();
#endif
	}

	return ret;
}

static unsigned int snand_get_capacity(void)
{
	return sftl_get_density();
}

static int snand_write(u32 sec, u32 n_sec, void *p_data)
{
	return sftl_write(sec, n_sec, p_data);
}

static int snand_read(u32 sec, u32 n_sec, void *p_data)
{
	return sftl_read(sec, n_sec, p_data);
}

static int snand_vendor_read(u32 sec, u32 n_sec, void *p_data)
{
	return sftl_vendor_read(sec, n_sec, p_data);
}

static int snand_vendor_write(u32 sec, u32 n_sec, void *p_data)
{
	return sftl_vendor_write(sec, n_sec, p_data);
}

static int snand_gc(void)
{
	return sftl_gc();
}

static int snand_discard(u32 sec, u32 n_sec)
{
	return sftl_discard(sec, n_sec);
}

static void snand_deinit(void)
{
#ifndef CONFIG_RK_SFC_NAND_MTD
	sftl_deinit();
#endif
	sfc_nand_deinit();
}

static int snand_resume(void __iomem *reg_addr)
{
	sfc_init(reg_addr);
	return sfc_nand_init();
}

const struct flash_boot_ops sfc_nand_ops = {
	snand_init,
	snand_read,
	snand_write,
	snand_get_capacity,
	snand_deinit,
	snand_resume,
	snand_vendor_read,
	snand_vendor_write,
	snand_gc,
	snand_discard,
};
