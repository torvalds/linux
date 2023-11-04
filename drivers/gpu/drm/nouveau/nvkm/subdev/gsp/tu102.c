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

#include <nvfw/flcn.h>
#include <nvfw/fw.h>
#include <nvfw/hs.h>

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
	gsp->fb.size = nvkm_fb_vidmem_size(gsp->subdev.device);

	gsp->fb.bios.vga_workspace.addr = tu102_gsp_vga_workspace_addr(gsp, gsp->fb.size);
	gsp->fb.bios.vga_workspace.size = gsp->fb.size - gsp->fb.bios.vga_workspace.addr;
	gsp->fb.bios.addr = gsp->fb.bios.vga_workspace.addr;
	gsp->fb.bios.size = gsp->fb.bios.vga_workspace.size;

	return r535_gsp_oneinit(gsp);
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
	.init = r535_gsp_init,
	.fini = r535_gsp_fini,
	.reset = tu102_gsp_reset,

	.rm = &r535_gsp_rm,
};

static struct nvkm_gsp_fwif
tu102_gsps[] = {
	{  0,  r535_gsp_load, &tu102_gsp_r535_113_01, "535.113.01" },
	{ -1, gv100_gsp_nofw, &gv100_gsp },
	{}
};

int
tu102_gsp_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	      struct nvkm_gsp **pgsp)
{
	return nvkm_gsp_new_(tu102_gsps, device, type, inst, pgsp);
}
