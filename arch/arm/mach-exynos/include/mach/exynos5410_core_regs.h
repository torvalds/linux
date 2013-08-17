/*
 * Copyright 2013 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __EXYNOS5410_CORE_REG_H
#define __EXYNOS5410_CORE_REG_H
static struct core_register a7_regs[] = {
	{
		.reg = &reg_sctlr,
		.val = 0x10c5387d,
	}, {
		.reg = &reg_actlr,
		.val = 0x6040,
	}, {
		.reg = &reg_prrr,
		.val = 0xff0a81a8,
	}, {
		.reg = &reg_nmrr,
		.val = 0x40e040e0,
	}, {
		.reg = &reg_l2ctlr,
		.val = 0x3000000,
	}, {
		.reg = &reg_l2ectlr,
		.val = 0x0,
	}, {}
};

static struct core_register a15_regs[] = {
	{
		.reg = &reg_sctlr,
		.val = 0x10c5387d,
	}, {
		.reg = &reg_actlr,
		.val = 0x1000040,
	}, {
		.reg = &reg_prrr,
		.val = 0xff0a81a8,
	}, {
		.reg = &reg_nmrr,
		.val = 0x40e040e0,
	}, {
		.reg = &reg_l2ctlr,
		.val = 0x3200482,
	}, {
		.reg = &reg_l2ectlr,
		.val = 0x0,
	}, {
		.reg = &reg_l2actlr,
		.val = 0x80,
	}, {
		.reg = &reg_l2pfr,
		.val = 0x9b0,
	}, {}
};
#endif
