/*
 * drivers/video/tegra/host/nvhost_cpuaccess.h
 *
 * Tegra Graphics Host Cpu Register Access
 *
 * Copyright (c) 2010, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __NVHOST_CPUACCESS_H
#define __NVHOST_CPUACCESS_H

#include "nvhost_hardware.h"
#include <linux/platform_device.h>
#include <linux/io.h>

enum nvhost_module_id {
	NVHOST_MODULE_DISPLAY_A = 0,
	NVHOST_MODULE_DISPLAY_B,
	NVHOST_MODULE_VI,
	NVHOST_MODULE_ISP,
	NVHOST_MODULE_MPE,
#if 0
	/* TODO: [ahatala 2010-07-02] find out if these are needed */
	NVHOST_MODULE_FUSE,
	NVHOST_MODULE_APB_MISC,
	NVHOST_MODULE_CLK_RESET,
#endif
	NVHOST_MODULE_NUM
};

struct nvhost_cpuaccess {
	struct resource *reg_mem[NVHOST_MODULE_NUM];
	void __iomem *regs[NVHOST_MODULE_NUM];
};

int nvhost_cpuaccess_init(struct nvhost_cpuaccess *ctx,
			struct platform_device *pdev);

void nvhost_cpuaccess_deinit(struct nvhost_cpuaccess *ctx);

int nvhost_mutex_try_lock(struct nvhost_cpuaccess *ctx, unsigned int idx);

void nvhost_mutex_unlock(struct nvhost_cpuaccess *ctx, unsigned int idx);

static inline bool nvhost_access_module_regs(
	struct nvhost_cpuaccess *ctx, u32 module)
{
	return (module < NVHOST_MODULE_NUM);
}

void nvhost_read_module_regs(struct nvhost_cpuaccess *ctx, u32 module,
			u32 offset, size_t size, void *values);

void nvhost_write_module_regs(struct nvhost_cpuaccess *ctx, u32 module,
			u32 offset, size_t size, const void *values);

#endif
