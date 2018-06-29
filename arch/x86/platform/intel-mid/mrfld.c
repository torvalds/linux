/*
 * Intel Merrifield platform specific setup code
 *
 * (C) Copyright 2013 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

#include <linux/init.h>

#include <asm/intel-mid.h>

#include "intel_mid_weak_decls.h"

static void __init tangier_arch_setup(void)
{
	x86_platform.legacy.rtc = 1;
}

/* tangier arch ops */
static struct intel_mid_ops tangier_ops = {
	.arch_setup = tangier_arch_setup,
};

void *get_tangier_ops(void)
{
	return &tangier_ops;
}
