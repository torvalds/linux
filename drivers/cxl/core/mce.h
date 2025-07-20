/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2024 Intel Corporation. All rights reserved. */
#ifndef _CXL_CORE_MCE_H_
#define _CXL_CORE_MCE_H_

#include <linux/notifier.h>

#ifdef CONFIG_CXL_MCE
int devm_cxl_register_mce_notifier(struct device *dev,
				   struct notifier_block *mce_notifer);
#else
static inline int
devm_cxl_register_mce_notifier(struct device *dev,
			       struct notifier_block *mce_notifier)
{
	return -EOPNOTSUPP;
}
#endif

#endif
