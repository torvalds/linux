// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2022 Intel Corporation. All rights reserved. */
#include <linux/atomic.h>
#include <linux/export.h>
#include "cxlmem.h"

static atomic_t mem_active;

bool cxl_mem_active(void)
{
	return atomic_read(&mem_active) != 0;
}

void cxl_mem_active_inc(void)
{
	atomic_inc(&mem_active);
}
EXPORT_SYMBOL_NS_GPL(cxl_mem_active_inc, CXL);

void cxl_mem_active_dec(void)
{
	atomic_dec(&mem_active);
}
EXPORT_SYMBOL_NS_GPL(cxl_mem_active_dec, CXL);
