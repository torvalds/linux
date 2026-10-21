/* SPDX-License-Identifier: GPL-2.0-only */
/****************************************************************************
 * Driver for AMD network controllers and boards
 * Copyright (C) 2025, Advanced Micro Devices, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef EFX_CXL_H
#define EFX_CXL_H

#ifdef CONFIG_SFC_CXL

#include <cxl/cxl.h>

struct efx_probe_data;

struct efx_cxl {
	struct cxl_dev_state cxlds;
	struct cxl_memdev *cxlmd;
	void __iomem *ctpio_cxl;
};

int efx_cxl_init(struct efx_probe_data *probe_data);
void efx_cxl_exit(struct efx_probe_data *probe_data);
#else
static inline int efx_cxl_init(struct efx_probe_data *probe_data) { return 0; }
static inline void efx_cxl_exit(struct efx_probe_data *probe_data) {}
#endif
#endif
