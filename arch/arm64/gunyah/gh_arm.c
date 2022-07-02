// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 *
 */

#include <linux/module.h>

static int __init gh_arm_init(void)
{
	return 0;
}
module_init(gh_arm_init);

static void __exit gh_arm_exit(void)
{
}
module_exit(gh_arm_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Qualcomm Technologies, Inc. Gunyah ARM64 Driver");
