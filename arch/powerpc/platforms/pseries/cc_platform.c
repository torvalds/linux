// SPDX-License-Identifier: GPL-2.0-only
/*
 * Confidential Computing Platform Capability checks
 *
 * Copyright (C) 2021 Advanced Micro Devices, Inc.
 *
 * Author: Tom Lendacky <thomas.lendacky@amd.com>
 */

#include <linux/export.h>
#include <linux/cc_platform.h>

#include <asm/machdep.h>
#include <asm/svm.h>

bool cc_platform_has(enum cc_attr attr)
{
	switch (attr) {
	case CC_ATTR_MEM_ENCRYPT:
		return is_secure_guest();

	default:
		return false;
	}
}
EXPORT_SYMBOL_GPL(cc_platform_has);
