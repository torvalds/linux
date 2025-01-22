/*
 * Copyright 2022 Red Hat Inc.
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
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */
#include "priv.h"

#include <subdev/fb.h>
#include <engine/sec2.h>

#include <nvfw/flcn.h>
#include <nvfw/fw.h>
#include <nvfw/hs.h>

static int
tu102_gsp_booter_unload(struct nvkm_gsp *gsp, u32 mbox0, u32 mbox1)
{
	struct nvkm_subdev *subdev = &gsp->subdev;
	struct nvkm_device *device = subdev->device;
	u32 wpr2_hi;
	int ret;

	wpr2_hi = nvkm_rd32(device, 0x1fa828);
	if (!wpr2_hi) {
		nvkm_debug(subdev, "WPR2 not set - skipping booter unload\n");
		return 0;
	}

	ret = nvkm_falcon_fw_boot(&gsp->booter.unload, &gsp->subdev, true, &mbox0, &mbox1, 0, 0);
	if (WARN_ON(ret))
		return ret;

	wpr2_hi = nvkm_rd32(device, 0x1fa828);
	if (WARN_ON(wpr2_hi))
		return -EIO;

	return 0;
}

static int
tu102_gsp_booter_load(struct nvkm_gsp *gsp, u32 mbox0, u32 mbox1)
{
	return nvkm_falcon_fw_boot(&gsp->booter.load, &gsp->subdev, true, &mbox0, &mbox1, 0, 0);
}

int
tu102_gsp_booter_ctor(struct nvkm_gsp *gsp, const char *name, const struct firmware *blob,
		      struct nvkm_falcon *falcon, struct nvkm_falcon_fw *fw)
{
	struct nvkm_subdev *subdev = &gsp->subdev;
	const struct nvkm_falcon_fw_func *func = &gm200_flcn_fw;
	const struct nvfw_bin_hdr *hdr;
	const struct nvfw_hs_header_v2 *hshdr;
	const struct nvfw_hs_load_header_v2 *lhdr;
	u32 loc, sig, cnt;
	int ret;

	hdr = nvfw_bin_hdr(subdev, blob->data);
	hshdr = nvfw_hs_header_v2(subdev, blob->data + hdr->header_offset);
	loc = *(u32 *)(blob->data + hshdr->patch_loc);
	sig = *(u32 *)(blob->data + hshdr->patch_sig);
	cnt = *(u32 *)(blob->data + hshdr->num_sig);

	ret = nvkm_falcon_fw_ctor(func, name, subdev->device, true,
				  blob->data + hdr->data_offset, hdr->data_size, falcon, fw);
	if (ret)
		goto done;

	ret = nvkm_falcon_fw_sign(fw, loc, hshdr->sig_prod_size / cnt, blob->data,
				  cnt, hshdr->sig_prod_offset + sig, 0, 0);
	if (ret)
		goto done;

	lhdr = nvfw_hs_load_header_v2(subdev, blob->data + hshdr->header_offset);

	fw->nmem_base_img = 0;
	fw->nmem_base = lhdr->os_code_offset;
	fw->nmem_size = lhdr->os_code_size;
	fw->imem_base_img = fw->nmem_size;
	fw->imem_base = lhdr->app[0].offset;
	fw->imem_size = lhdr->app[0].size;
	fw->dmem_base_img = lhdr->os_data_offset;
	fw->dmem_base = 0;
	fw->dmem_size = lhdr->os_data_size;
	fw->dmem_sign = loc - fw->dmem_base_img;
	fw->boot_addr = lhdr->os_code_offset;

done:
	if (ret)
		nvkm_falcon_fw_dtor(fw);

	return ret;
}

static int
tu102_gsp_fwsec_load_bld(struct nvkm_falcon_fw *fw)
{
	struct flcn_bl_dmem_desc_v2 desc = {
		.ctx_dma = FALCON_DMAIDX_PHYS_SYS_NCOH,
		.code_dma_base = fw->fw.phys,
		.non_sec_code_off = fw->nmem_base,
		.non_sec_code_size = fw->nmem_size,
		.sec_code_off = fw->imem_base,
		.sec_code_size = fw->imem_size,
		.code_entry_point = 0,
		.data_dma_base = fw->fw.phys + fw->dmem_base_img,
		.data_size = fw->dmem_size,
		.argc = 0,
		.argv = 0,
	};

	flcn_bl_dmem_desc_v2_dump(fw->falcon->user, &desc);

	nvkm_falcon_mask(fw->falcon, 0x600 + desc.ctx_dma * 4, 0x00000007, 0x00000005);

	return nvkm_falcon_pio_wr(fw->falcon, (u8 *)&desc, 0, 0, DMEM, 0, sizeof(desc), 0, 0);
}

const struct nvkm_falcon_fw_func
tu102_gsp_fwsec = {
	.reset = gm200_flcn_fw_reset,
	.load = gm200_flcn_fw_load,
	.load_bld = tu102_gsp_fwsec_load_bld,
	.boot = gm200_flcn_fw_boot,
};

int
tu102_gsp_reset(struct nvkm_gsp *gsp)
{
	return gsp->falcon.func->reset_eng(&gsp->falcon);
}

int
tu102_gsp_fini(struct nvkm_gsp *gsp, bool suspend)
{
	u32 mbox0 = 0xff, mbox1 = 0xff;
	int ret;

	ret = r535_gsp_fini(gsp, suspend);
	if (ret && suspend)
		return ret;

	nvkm_falcon_reset(&gsp->falcon);

	ret = nvkm_gsp_fwsec_sb(gsp);
	WARN_ON(ret);

	if (suspend) {
		mbox0 = lower_32_bits(gsp->sr.meta.addr);
		mbox1 = upper_32_bits(gsp->sr.meta.addr);
	}

	ret = tu102_gsp_booter_unload(gsp, mbox0, mbox1);
	WARN_ON(ret);
	return 0;
}

int
tu102_gsp_init(struct nvkm_gsp *gsp)
{
	u32 mbox0, mbox1;
	int ret;

	if (!gsp->sr.meta.data) {
		mbox0 = lower_32_bits(gsp->wpr_meta.addr);
		mbox1 = upper_32_bits(gsp->wpr_meta.addr);
	} else {
		r535_gsp_rmargs_init(gsp, true);

		mbox0 = lower_32_bits(gsp->sr.meta.addr);
		mbox1 = upper_32_bits(gsp->sr.meta.addr);
	}

	/* Execute booter to handle (eventually...) booting GSP-RM. */
	ret = tu102_gsp_booter_load(gsp, mbox0, mbox1);
	if (WARN_ON(ret))
		return ret;

	return r535_gsp_init(gsp);
}

static u64
tu102_gsp_vga_workspace_addr(struct nvkm_gsp *gsp, u64 fb_size)
{
	struct nvkm_device *device = gsp->subdev.device;
	const u64 base = fb_size - 0x100000;
	u64 addr = 0;

	if (device->disp)
		addr = nvkm_rd32(gsp->subdev.device, 0x625f04);
	if (!(addr & 0x00000008))
		return base;

	addr = (addr & 0xffffff00) << 8;
	if (addr < base)
		return fb_size - 0x20000;

	return addr;
}

int
tu102_gsp_oneinit(struct nvkm_gsp *gsp)
{
	struct nvkm_device *device = gsp->subdev.device;
	int ret;

	gsp->fb.size = nvkm_fb_vidmem_size(device);

	gsp->fb.bios.vga_workspace.addr = tu102_gsp_vga_workspace_addr(gsp, gsp->fb.size);
	gsp->fb.bios.vga_workspace.size = gsp->fb.size - gsp->fb.bios.vga_workspace.addr;
	gsp->fb.bios.addr = gsp->fb.bios.vga_workspace.addr;
	gsp->fb.bios.size = gsp->fb.bios.vga_workspace.size;

	ret = gsp->func->booter.ctor(gsp, "booter-load", gsp->fws.booter.load,
				     &device->sec2->falcon, &gsp->booter.load);
	if (ret)
		return ret;

	ret = gsp->func->booter.ctor(gsp, "booter-unload", gsp->fws.booter.unload,
				     &device->sec2->falcon, &gsp->booter.unload);
	if (ret)
		return ret;

	ret = r535_gsp_oneinit(gsp);
	if (ret)
		return ret;

	/* Reset GSP into RISC-V mode. */
	ret = gsp->func->reset(gsp);
	if (ret)
		return ret;

	nvkm_falcon_wr32(&gsp->falcon, 0x040, lower_32_bits(gsp->libos.addr));
	nvkm_falcon_wr32(&gsp->falcon, 0x044, upper_32_bits(gsp->libos.addr));
	return 0;
}

const struct nvkm_falcon_func
tu102_gsp_flcn = {
	.disable = gm200_flcn_disable,
	.enable = gm200_flcn_enable,
	.addr2 = 0x1000,
	.riscv_irqmask = 0x2b4,
	.reset_eng = gp102_flcn_reset_eng,
	.reset_wait_mem_scrubbing = gm200_flcn_reset_wait_mem_scrubbing,
	.bind_inst = gm200_flcn_bind_inst,
	.bind_stat = gm200_flcn_bind_stat,
	.bind_intr = true,
	.imem_pio = &gm200_flcn_imem_pio,
	.dmem_pio = &gm200_flcn_dmem_pio,
	.riscv_active = tu102_flcn_riscv_active,
};

static const struct nvkm_gsp_func
tu102_gsp_r535_113_01 = {
	.flcn = &tu102_gsp_flcn,
	.fwsec = &tu102_gsp_fwsec,

	.sig_section = ".fwsignature_tu10x",

	.wpr_heap.base_size = 8 << 20,
	.wpr_heap.min_size = 64 << 20,

	.booter.ctor = tu102_gsp_booter_ctor,

	.dtor = r535_gsp_dtor,
	.oneinit = tu102_gsp_oneinit,
	.init = tu102_gsp_init,
	.fini = tu102_gsp_fini,
	.reset = tu102_gsp_reset,

	.rm = &r535_gsp_rm,
};

static int
tu102_gsp_load_rm(struct nvkm_gsp *gsp, const struct nvkm_gsp_fwif *fwif)
{
	struct nvkm_subdev *subdev = &gsp->subdev;
	bool enable_gsp = fwif->enable;
	int ret;

#if IS_ENABLED(CONFIG_DRM_NOUVEAU_GSP_DEFAULT)
	enable_gsp = true;
#endif
	if (!nvkm_boolopt(subdev->device->cfgopt, "NvGspRm", enable_gsp))
		return -EINVAL;

	ret = nvkm_gsp_load_fw(gsp, "gsp", fwif->ver, &gsp->fws.rm);
	if (ret)
		return ret;

	ret = nvkm_gsp_load_fw(gsp, "bootloader", fwif->ver, &gsp->fws.bl);
	if (ret)
		return ret;

	return 0;
}

int
tu102_gsp_load(struct nvkm_gsp *gsp, int ver, const struct nvkm_gsp_fwif *fwif)
{
	int ret;

	ret = tu102_gsp_load_rm(gsp, fwif);
	if (ret)
		goto done;

	ret = nvkm_gsp_load_fw(gsp, "booter_load", fwif->ver, &gsp->fws.booter.load);
	if (ret)
		goto done;

	ret = nvkm_gsp_load_fw(gsp, "booter_unload", fwif->ver, &gsp->fws.booter.unload);

done:
	if (ret)
		nvkm_gsp_dtor_fws(gsp);

	return ret;
}

static struct nvkm_gsp_fwif
tu102_gsps[] = {
	{  0, tu102_gsp_load, &tu102_gsp_r535_113_01, "535.113.01" },
	{ -1, gv100_gsp_nofw, &gv100_gsp },
	{}
};

int
tu102_gsp_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	      struct nvkm_gsp **pgsp)
{
	return nvkm_gsp_new_(tu102_gsps, device, type, inst, pgsp);
}

NVKM_GSP_FIRMWARE_BOOTER(tu102, 535.113.01);
NVKM_GSP_FIRMWARE_BOOTER(tu104, 535.113.01);
NVKM_GSP_FIRMWARE_BOOTER(tu106, 535.113.01);
