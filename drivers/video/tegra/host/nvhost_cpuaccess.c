/*
 * drivers/video/tegra/host/nvhost_cpuaccess.c
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

#include "nvhost_cpuaccess.h"
#include "dev.h"
#include <linux/string.h>

#define cpuaccess_to_dev(ctx) container_of(ctx, struct nvhost_master, cpuaccess)

int nvhost_cpuaccess_init(struct nvhost_cpuaccess *ctx,
			struct platform_device *pdev)
{
	int i;
	for (i = 0; i < NVHOST_MODULE_NUM; i++) {
		struct resource *mem;
		mem = platform_get_resource(pdev, IORESOURCE_MEM, i+1);
		if (!mem) {
			dev_err(&pdev->dev, "missing module memory resource\n");
			return -ENXIO;
		}

		ctx->regs[i] = ioremap(mem->start, resource_size(mem));
		if (!ctx->regs[i]) {
			dev_err(&pdev->dev, "failed to map module registers\n");
			return -ENXIO;
		}
	}

	return 0;
}

void nvhost_cpuaccess_deinit(struct nvhost_cpuaccess *ctx)
{
	int i;
	for (i = 0; i < NVHOST_MODULE_NUM; i++) {
		iounmap(ctx->regs[i]);
		release_resource(ctx->reg_mem[i]);
	}
}

int nvhost_mutex_try_lock(struct nvhost_cpuaccess *ctx, unsigned int idx)
{
	struct nvhost_master *dev = cpuaccess_to_dev(ctx);
	void __iomem *sync_regs = dev->sync_aperture;
	u32 reg;

	/* mlock registers returns 0 when the lock is aquired.
	 * writing 0 clears the lock. */
	nvhost_module_busy(&dev->mod);
	reg = readl(sync_regs + (HOST1X_SYNC_MLOCK_0 + idx * 4));
	if (reg) {
		nvhost_module_idle(&dev->mod);
		return -ERESTARTSYS;
	}
	return 0;
}

void nvhost_mutex_unlock(struct nvhost_cpuaccess *ctx, unsigned int idx)
{
	struct nvhost_master *dev = cpuaccess_to_dev(ctx);
	void __iomem *sync_regs = dev->sync_aperture;
	writel(0, sync_regs + (HOST1X_SYNC_MLOCK_0 + idx * 4));
	nvhost_module_idle(&dev->mod);
}

void nvhost_read_module_regs(struct nvhost_cpuaccess *ctx, u32 module,
			u32 offset, size_t size, void *values)
{
	struct nvhost_master *dev = cpuaccess_to_dev(ctx);
	void __iomem *p = ctx->regs[module] + offset;
	u32* out = (u32*)values;
	BUG_ON(size & 3);
	size >>= 2;
	nvhost_module_busy(&dev->mod);
	while (size--) {
		*(out++) = readl(p);
		p += 4;
	}
	rmb();
	nvhost_module_idle(&dev->mod);
}

void nvhost_write_module_regs(struct nvhost_cpuaccess *ctx, u32 module,
			u32 offset, size_t size, const void *values)
{
	struct nvhost_master *dev = cpuaccess_to_dev(ctx);
	void __iomem *p = ctx->regs[module] + offset;
	const u32* in = (const u32*)values;
	BUG_ON(size & 3);
	size >>= 2;
	nvhost_module_busy(&dev->mod);
	while (size--) {
		writel(*(in++), p);
		p += 4;
	}
	wmb();
	nvhost_module_idle(&dev->mod);
}
