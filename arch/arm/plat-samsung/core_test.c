/*
 * Copyright 2013 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/err.h>
#include <linux/module.h>
#include <plat/core_regs.h>

enum armv7_core_type {
	A15_CORE,
	A7_CORE,
};

static enum armv7_core_type get_core_type(void)
{
	u32 cluster_id = read_mpidr();

	if ((cluster_id >> 8) & 0xf)
		return A7_CORE;
	else
		return A15_CORE;
}

int __init coretest_init(void)
{
	u32 val;
	u32 fail_cnt = 0;
	enum armv7_core_type core_type = get_core_type();
	struct core_register *regs;

	pr_info("[Core type: %s]\n", core_type ? "A7" : "A15");

	switch (core_type) {
	case A15_CORE:
		regs = a15_regs;
		break;
	case A7_CORE:
		regs = a7_regs;
		break;

	}

	while (regs->reg) {
		val = regs->reg->read_reg();
		if (val != regs->val) {
			pr_info("Fail: %s: req: 0x%x -> set: 0x%x\n", regs->reg->name, regs->val, val);
			fail_cnt++;
		} else {
			pr_info("%s: 0x%x\n", regs->reg->name, val);
		}
		regs++;
	}

	pr_info("Test result: %d fail\n", fail_cnt);
	return 0;
}
module_init(coretest_init)

void coretest_exit(void) {}
module_exit(coretest_exit);
MODULE_LICENSE("GPL");
