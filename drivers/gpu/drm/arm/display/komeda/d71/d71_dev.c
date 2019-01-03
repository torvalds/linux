// SPDX-License-Identifier: GPL-2.0
/*
 * (C) COPYRIGHT 2018 ARM Limited. All rights reserved.
 * Author: James.Qian.Wang <james.qian.wang@arm.com>
 *
 */
#include "malidp_io.h"
#include "komeda_dev.h"

static int d71_enum_resources(struct komeda_dev *mdev)
{
	/* TODO add enum resources */
	return -1;
}

static struct komeda_dev_funcs d71_chip_funcs = {
	.enum_resources	= d71_enum_resources,
	.cleanup	= NULL,
};

#define GLB_ARCH_ID		0x000
#define GLB_CORE_ID		0x004
#define GLB_CORE_INFO		0x008

struct komeda_dev_funcs *
d71_identify(u32 __iomem *reg_base, struct komeda_chip_info *chip)
{
	chip->arch_id	= malidp_read32(reg_base, GLB_ARCH_ID);
	chip->core_id	= malidp_read32(reg_base, GLB_CORE_ID);
	chip->core_info	= malidp_read32(reg_base, GLB_CORE_INFO);

	return &d71_chip_funcs;
}
