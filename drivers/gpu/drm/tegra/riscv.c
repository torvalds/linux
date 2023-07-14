// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022, NVIDIA Corporation.
 */

#include <linux/dev_printk.h>
#include <linux/device.h>
#include <linux/iopoll.h>
#include <linux/of.h>

#include "riscv.h"

#define RISCV_CPUCTL					0x4388
#define RISCV_CPUCTL_STARTCPU_TRUE			(1 << 0)
#define RISCV_BR_RETCODE				0x465c
#define RISCV_BR_RETCODE_RESULT_V(x)			((x) & 0x3)
#define RISCV_BR_RETCODE_RESULT_PASS_V			3
#define RISCV_BCR_CTRL					0x4668
#define RISCV_BCR_CTRL_CORE_SELECT_RISCV		(1 << 4)
#define RISCV_BCR_DMACFG				0x466c
#define RISCV_BCR_DMACFG_TARGET_LOCAL_FB		(0 << 0)
#define RISCV_BCR_DMACFG_LOCK_LOCKED			(1 << 31)
#define RISCV_BCR_DMAADDR_PKCPARAM_LO			0x4670
#define RISCV_BCR_DMAADDR_PKCPARAM_HI			0x4674
#define RISCV_BCR_DMAADDR_FMCCODE_LO			0x4678
#define RISCV_BCR_DMAADDR_FMCCODE_HI			0x467c
#define RISCV_BCR_DMAADDR_FMCDATA_LO			0x4680
#define RISCV_BCR_DMAADDR_FMCDATA_HI			0x4684
#define RISCV_BCR_DMACFG_SEC				0x4694
#define RISCV_BCR_DMACFG_SEC_GSCID(v)			((v) << 16)

static void riscv_writel(struct tegra_drm_riscv *riscv, u32 value, u32 offset)
{
	writel(value, riscv->regs + offset);
}

int tegra_drm_riscv_read_descriptors(struct tegra_drm_riscv *riscv)
{
	struct tegra_drm_riscv_descriptor *bl = &riscv->bl_desc;
	struct tegra_drm_riscv_descriptor *os = &riscv->os_desc;
	const struct device_node *np = riscv->dev->of_node;
	int err;

#define READ_PROP(name, location) \
	err = of_property_read_u32(np, name, location); \
	if (err) { \
		dev_err(riscv->dev, "failed to read " name ": %d\n", err); \
		return err; \
	}

	READ_PROP("nvidia,bl-manifest-offset", &bl->manifest_offset);
	READ_PROP("nvidia,bl-code-offset", &bl->code_offset);
	READ_PROP("nvidia,bl-data-offset", &bl->data_offset);
	READ_PROP("nvidia,os-manifest-offset", &os->manifest_offset);
	READ_PROP("nvidia,os-code-offset", &os->code_offset);
	READ_PROP("nvidia,os-data-offset", &os->data_offset);
#undef READ_PROP

	if (bl->manifest_offset == 0 && bl->code_offset == 0 &&
	    bl->data_offset == 0 && os->manifest_offset == 0 &&
	    os->code_offset == 0 && os->data_offset == 0) {
		dev_err(riscv->dev, "descriptors not available\n");
		return -EINVAL;
	}

	return 0;
}

int tegra_drm_riscv_boot_bootrom(struct tegra_drm_riscv *riscv, phys_addr_t image_address,
				 u32 gscid, const struct tegra_drm_riscv_descriptor *desc)
{
	phys_addr_t addr;
	int err;
	u32 val;

	riscv_writel(riscv, RISCV_BCR_CTRL_CORE_SELECT_RISCV, RISCV_BCR_CTRL);

	addr = image_address + desc->manifest_offset;
	riscv_writel(riscv, lower_32_bits(addr >> 8), RISCV_BCR_DMAADDR_PKCPARAM_LO);
	riscv_writel(riscv, upper_32_bits(addr >> 8), RISCV_BCR_DMAADDR_PKCPARAM_HI);

	addr = image_address + desc->code_offset;
	riscv_writel(riscv, lower_32_bits(addr >> 8), RISCV_BCR_DMAADDR_FMCCODE_LO);
	riscv_writel(riscv, upper_32_bits(addr >> 8), RISCV_BCR_DMAADDR_FMCCODE_HI);

	addr = image_address + desc->data_offset;
	riscv_writel(riscv, lower_32_bits(addr >> 8), RISCV_BCR_DMAADDR_FMCDATA_LO);
	riscv_writel(riscv, upper_32_bits(addr >> 8), RISCV_BCR_DMAADDR_FMCDATA_HI);

	riscv_writel(riscv, RISCV_BCR_DMACFG_SEC_GSCID(gscid), RISCV_BCR_DMACFG_SEC);
	riscv_writel(riscv,
		RISCV_BCR_DMACFG_TARGET_LOCAL_FB | RISCV_BCR_DMACFG_LOCK_LOCKED, RISCV_BCR_DMACFG);

	riscv_writel(riscv, RISCV_CPUCTL_STARTCPU_TRUE, RISCV_CPUCTL);

	err = readl_poll_timeout(
		riscv->regs + RISCV_BR_RETCODE, val,
		RISCV_BR_RETCODE_RESULT_V(val) == RISCV_BR_RETCODE_RESULT_PASS_V,
		10, 100000);
	if (err) {
		dev_err(riscv->dev, "error during bootrom execution. BR_RETCODE=%d\n", val);
		return err;
	}

	return 0;
}
