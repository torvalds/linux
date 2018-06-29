/*
 * mfld.c: Intel Medfield platform setup code
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

static void __init penwell_arch_setup(void)
{
}

static struct intel_mid_ops penwell_ops = {
	.arch_setup = penwell_arch_setup,
};

void *get_penwell_ops(void)
{
	return &penwell_ops;
}

void *get_cloverview_ops(void)
{
	return &penwell_ops;
}
