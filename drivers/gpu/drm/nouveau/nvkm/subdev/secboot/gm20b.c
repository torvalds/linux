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

#include "priv.h"

#include <core/gpuobj.h>
#include <engine/falcon.h>

/*
 * The BL header format used by GM20B's firmware is slightly different
 * from the one of GM200. Fix the differences here.
 */
struct gm20b_flcn_bl_desc {
	u32 reserved[4];
	u32 signature[4];
	u32 ctx_dma;
	u32 code_dma_base;
	u32 non_sec_code_off;
	u32 non_sec_code_size;
	u32 sec_code_off;
	u32 sec_code_size;
	u32 code_entry_point;
	u32 data_dma_base;
	u32 data_size;
};

static void
gm20b_secboot_ls_bl_desc(const struct ls_ucode_img *img, u64 wpr_addr,
			 void *_desc)
{
	struct gm20b_flcn_bl_desc *desc = _desc;
	const struct ls_ucode_img_desc *pdesc = &img->ucode_desc;
	u64 base;

	base = wpr_addr + img->lsb_header.ucode_off + pdesc->app_start_offset;

	memset(desc, 0, sizeof(*desc));
	desc->ctx_dma = FALCON_DMAIDX_UCODE;
	desc->code_dma_base = (base + pdesc->app_resident_code_offset) >> 8;
	desc->non_sec_code_size = pdesc->app_resident_code_size;
	desc->data_dma_base = (base + pdesc->app_resident_data_offset) >> 8;
	desc->data_size = pdesc->app_resident_data_size;
	desc->code_entry_point = pdesc->app_imem_entry;
}

static int
gm20b_secboot_prepare_blobs(struct gm200_secboot *gsb)
{
	struct nvkm_subdev *subdev = &gsb->base.subdev;
	int acr_size;
	int ret;

	ret = gm20x_secboot_prepare_blobs(gsb);
	if (ret)
		return ret;

	acr_size = gsb->acr_load_blob->size;
	/*
	 * On Tegra the WPR region is set by the bootloader. It is illegal for
	 * the HS blob to be larger than this region.
	 */
	if (acr_size > gsb->wpr_size) {
		nvkm_error(subdev, "WPR region too small for FW blob!\n");
		nvkm_error(subdev, "required: %dB\n", acr_size);
		nvkm_error(subdev, "WPR size: %dB\n", gsb->wpr_size);
		return -ENOSPC;
	}

	return 0;
}

static void
gm20b_secboot_generate_bl_desc(const struct hsf_load_header *load_hdr,
			       void *_bl_desc, u64 offset)
{
	struct gm20b_flcn_bl_desc *bl_desc = _bl_desc;

	memset(bl_desc, 0, sizeof(*bl_desc));
	bl_desc->ctx_dma = FALCON_DMAIDX_VIRT;
	bl_desc->non_sec_code_off = load_hdr->non_sec_code_off;
	bl_desc->non_sec_code_size = load_hdr->non_sec_code_size;
	bl_desc->sec_code_off = load_hdr->app[0].sec_code_off;
	bl_desc->sec_code_size = load_hdr->app[0].sec_code_size;
	bl_desc->code_entry_point = 0;
	bl_desc->code_dma_base = offset >> 8;
	bl_desc->data_dma_base = (offset + load_hdr->data_dma_base) >> 8;
	bl_desc->data_size = load_hdr->data_size;
}

static const struct gm200_secboot_func
gm20b_secboot_func = {
	.bl_desc_size = sizeof(struct gm20b_flcn_bl_desc),
	.generate_bl_desc = gm20b_secboot_generate_bl_desc,
	.prepare_blobs = gm20b_secboot_prepare_blobs,
};


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
	gsb->wpr_addr = ioread32_native(mc + MC_SECURITY_CARVEOUT2_BOM_0) |
	      ((u64)ioread32_native(mc + MC_SECURITY_CARVEOUT2_BOM_HI_0) << 32);
	gsb->wpr_size = ioread32_native(mc + MC_SECURITY_CARVEOUT2_SIZE_128K)
		<< 17;
	cfg = ioread32_native(mc + MC_SECURITY_CARVEOUT2_CFG0);
	iounmap(mc);

	/* Check that WPR settings are valid */
	if (gsb->wpr_size == 0) {
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
	.reset = gm200_secboot_reset,
	.managed_falcons = BIT(NVKM_SECBOOT_FALCON_FECS),
	.boot_falcon = NVKM_SECBOOT_FALCON_PMU,
};

static const secboot_ls_func
gm20b_ls_func = {
	[NVKM_SECBOOT_FALCON_FECS] = &(struct secboot_ls_single_func) {
		.load = gm200_ls_load_fecs,
		.generate_bl_desc = gm20b_secboot_ls_bl_desc,
		.bl_desc_size = sizeof(struct gm20b_flcn_bl_desc),
	},
};

int
gm20b_secboot_new(struct nvkm_device *device, int index,
		  struct nvkm_secboot **psb)
{
	int ret;
	struct gm200_secboot *gsb;

	gsb = kzalloc(sizeof(*gsb), GFP_KERNEL);
	if (!gsb) {
		psb = NULL;
		return -ENOMEM;
	}
	*psb = &gsb->base;

	ret = nvkm_secboot_ctor(&gm20b_secboot, device, index, &gsb->base);
	if (ret)
		return ret;

	gsb->func = &gm20b_secboot_func;
	gsb->ls_func = &gm20b_ls_func;

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
