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
/**
 * gm20b_secboot_tegra_read_wpr() - read the WPR registers on Tegra
 *
 * On dGPU, we can manage the WPR region ourselves, but on Tegra this region
 * is allocated from system memory by the secure firmware. The region is then
 * marked as a "secure carveout" and irreversibly locked. Furthermore, the WPR
 * secure carveout is also configured to be sent to the GPU via a dedicated
 * serial bus between the memory controller and the GPU. The GPU requests this
 * information upon leaving reset and exposes it through a FIFO register at
 * offset 0x100cd4.
 *
 * The FIFO register's lower 4 bits can be used to set the read index into the
 * FIFO. After each read of the FIFO register, the read index is incremented.
 *
 * Indices 2 and 3 contain the lower and upper addresses of the WPR. These are
 * stored in units of 256 B. The WPR is inclusive of both addresses.
 *
 * Unfortunately, for some reason the WPR info register doesn't contain the
 * correct values for the secure carveout. It seems like the upper address is
 * always too small by 128 KiB - 1. Given that the secure carvout size in the
 * memory controller configuration is specified in units of 128 KiB, it's
 * possible that the computation of the upper address of the WPR is wrong and
 * causes this difference.
 */
int
gm20b_secboot_tegra_read_wpr(struct gm200_secboot *gsb)
{
	struct nvkm_device *device = gsb->base.subdev.device;
	struct nvkm_secboot *sb = &gsb->base;
	u64 base, limit;
	u32 value;

	/* set WPR info register to point at WPR base address register */
	value = nvkm_rd32(device, 0x100cd4);
	value &= ~0xf;
	value |= 0x2;
	nvkm_wr32(device, 0x100cd4, value);

	/* read base address */
	value = nvkm_rd32(device, 0x100cd4);
	base = (u64)(value >> 4) << 12;

	/* read limit */
	value = nvkm_rd32(device, 0x100cd4);
	limit = (u64)(value >> 4) << 12;

	/*
	 * The upper address of the WPR seems to be computed wrongly and is
	 * actually SZ_128K - 1 bytes lower than it should be. Adjust the
	 * value accordingly.
	 */
	limit += SZ_128K - 1;

	sb->wpr_size = limit - base + 1;
	sb->wpr_addr = base;

	nvkm_info(&sb->subdev, "WPR: %016llx-%016llx\n", sb->wpr_addr,
		  sb->wpr_addr + sb->wpr_size - 1);

	/* Check that WPR settings are valid */
	if (sb->wpr_size == 0) {
		nvkm_error(&sb->subdev, "WPR region is empty\n");
		return -EINVAL;
	}

	return 0;
}
#else
int
gm20b_secboot_tegra_read_wpr(struct gm200_secboot *gsb)
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

	ret = gm20b_secboot_tegra_read_wpr(gsb);
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

	*psb = NULL;
	acr = acr_r352_new(BIT(NVKM_SECBOOT_FALCON_FECS) |
			   BIT(NVKM_SECBOOT_FALCON_PMU));
	if (IS_ERR(acr))
		return PTR_ERR(acr);
	/* Support the initial GM20B firmware release without PMU */
	acr->optional_falcons = BIT(NVKM_SECBOOT_FALCON_PMU);

	gsb = kzalloc(sizeof(*gsb), GFP_KERNEL);
	if (!gsb)
		return -ENOMEM;
	*psb = &gsb->base;

	ret = nvkm_secboot_ctor(&gm20b_secboot, acr, device, index, &gsb->base);
	if (ret)
		return ret;

	return 0;
}

#if IS_ENABLED(CONFIG_ARCH_TEGRA_210_SOC)
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
MODULE_FIRMWARE("nvidia/gm20b/pmu/desc.bin");
MODULE_FIRMWARE("nvidia/gm20b/pmu/image.bin");
MODULE_FIRMWARE("nvidia/gm20b/pmu/sig.bin");
#endif
