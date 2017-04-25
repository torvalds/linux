/*
 * Copyright (c) 2016, NVIDIA CORPORATION. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "acr.h"
#include "gm200.h"

#ifdef CONFIG_ARCH_TEGRA
#define TEGRA_MC_BASE				0x70019000
#define MC_SECURITY_CARVEOUT2_CFG0		0xc58
#define MC_SECURITY_CARVEOUT2_BOM_0		0xc5c
#define MC_SECURITY_CARVEOUT2_BOM_HI_0		0xc60
#define MC_SECURITY_CARVEOUT2_SIZE_128K		0xc64
#define TEGRA_MC_SECURITY_CARVEOUT_CFG_LOCKED	(1 << 1)
/**
 * sb_tegra_read_wpr() - read the WPR registers on Tegra
 *
 * On dGPU, we can manage the WPR region ourselves, but on Tegra the WPR region
 * is reserved from system memory by the bootloader and irreversibly locked.
 * This function reads the address and size of the pre-configured WPR region.
 */
static int
gm20b_tegra_read_wpr(struct gm200_secboot *gsb)
{
	struct nvkm_secboot *sb = &gsb->base;
	void __iomem *mc;
	u32 cfg;

	mc = ioremap(TEGRA_MC_BASE, 0xd00);
	if (!mc) {
		nvkm_error(&sb->subdev, "Cannot map Tegra MC registers\n");
		return PTR_ERR(mc);
	}
	sb->wpr_addr = ioread32_native(mc + MC_SECURITY_CARVEOUT2_BOM_0) |
	      ((u64)ioread32_native(mc + MC_SECURITY_CARVEOUT2_BOM_HI_0) << 32);
	sb->wpr_size = ioread32_native(mc + MC_SECURITY_CARVEOUT2_SIZE_128K)
		<< 17;
	cfg = ioread32_native(mc + MC_SECURITY_CARVEOUT2_CFG0);
	iounmap(mc);

	/* Check that WPR settings are valid */
	if (sb->wpr_size == 0) {
		nvkm_error(&sb->subdev, "WPR region is empty\n");
		return -EINVAL;
	}

	if (!(cfg & TEGRA_MC_SECURITY_CARVEOUT_CFG_LOCKED)) {
		nvkm_error(&sb->subdev, "WPR region not locked\n");
		return -EINVAL;
	}

	return 0;
}
#else
static int
gm20b_tegra_read_wpr(struct gm200_secboot *gsb)
{
	nvkm_error(&gsb->base.subdev, "Tegra support not compiled in\n");
	return -EINVAL;
}
#endif

static int
gm20b_secboot_oneinit(struct nvkm_secboot *sb)
{
	struct gm200_secboot *gsb = gm200_secboot(sb);
	int ret;

	ret = gm20b_tegra_read_wpr(gsb);
	if (ret)
		return ret;

	return gm200_secboot_oneinit(sb);
}

static const struct nvkm_secboot_func
gm20b_secboot = {
	.dtor = gm200_secboot_dtor,
	.oneinit = gm20b_secboot_oneinit,
	.fini = gm200_secboot_fini,
	.run_blob = gm200_secboot_run_blob,
};

int
gm20b_secboot_new(struct nvkm_device *device, int index,
		  struct nvkm_secboot **psb)
{
	int ret;
	struct gm200_secboot *gsb;
	struct nvkm_acr *acr;

	acr = acr_r352_new(BIT(NVKM_SECBOOT_FALCON_FECS));
	if (IS_ERR(acr))
		return PTR_ERR(acr);

	gsb = kzalloc(sizeof(*gsb), GFP_KERNEL);
	if (!gsb) {
		psb = NULL;
		return -ENOMEM;
	}
	*psb = &gsb->base;

	ret = nvkm_secboot_ctor(&gm20b_secboot, acr, device, index, &gsb->base);
	if (ret)
		return ret;

	return 0;
}

MODULE_FIRMWARE("nvidia/gm20b/acr/bl.bin");
MODULE_FIRMWARE("nvidia/gm20b/acr/ucode_load.bin");
MODULE_FIRMWARE("nvidia/gm20b/gr/fecs_bl.bin");
MODULE_FIRMWARE("nvidia/gm20b/gr/fecs_inst.bin");
MODULE_FIRMWARE("nvidia/gm20b/gr/fecs_data.bin");
MODULE_FIRMWARE("nvidia/gm20b/gr/fecs_sig.bin");
MODULE_FIRMWARE("nvidia/gm20b/gr/gpccs_inst.bin");
MODULE_FIRMWARE("nvidia/gm20b/gr/gpccs_data.bin");
MODULE_FIRMWARE("nvidia/gm20b/gr/sw_ctx.bin");
MODULE_FIRMWARE("nvidia/gm20b/gr/sw_nonctx.bin");
MODULE_FIRMWARE("nvidia/gm20b/gr/sw_bundle_init.bin");
MODULE_FIRMWARE("nvidia/gm20b/gr/sw_method_init.bin");
