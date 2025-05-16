// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2024 Intel Corporation. All rights reserved. */
#include <linux/acpi.h>
#include "cxl.h"
#include "core.h"

int cxl_acpi_get_extended_linear_cache_size(struct resource *backing_res,
					    int nid, resource_size_t *size)
{
	return hmat_get_extended_linear_cache_size(backing_res, nid, size);
}
