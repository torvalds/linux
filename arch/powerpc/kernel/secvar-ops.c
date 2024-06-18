// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 IBM Corporation
 * Author: Nayna Jain
 *
 * This file initializes secvar operations for PowerPC Secureboot
 */

#include <linux/cache.h>
#include <asm/secvar.h>
#include <asm/bug.h>

const struct secvar_operations *secvar_ops __ro_after_init = NULL;

int set_secvar_ops(const struct secvar_operations *ops)
{
	if (WARN_ON_ONCE(secvar_ops))
		return -EBUSY;

	secvar_ops = ops;

	return 0;
}
