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

#include <core/memory.h>
#include <subdev/mmu.h>

#include <nvfw/fw.h>
#include <nvfw/hs.h>

int
nvkm_falcon_fw_patch(struct nvkm_falcon_fw *fw)
{
	struct nvkm_falcon *falcon = fw->falcon;
	u32 sig_base_src = fw->sig_base_prd;
	u32 src, dst, len, i;
	int idx = 0;

	FLCNFW_DBG(fw, "patching sigs:%d size:%d", fw->sig_nr, fw->sig_size);
	if (fw->func->signature) {
		idx = fw->func->signature(fw, &sig_base_src);
		if (idx < 0)
			return idx;
	}

	src = idx * fw->sig_size;
	dst = fw->sig_base_img;
	len = fw->sig_size / 4;
	FLCNFW_DBG(fw, "patch idx:%d src:%08x dst:%08x", idx, sig_base_src + src, dst);
	for (i = 0; i < len; i++) {
		u32 sig = *(u32 *)(fw->sigs + src);

		if (nvkm_printk_ok(falcon->owner, falcon->user, NV_DBG_TRACE)) {
			if (i % 8 == 0)
				printk(KERN_INFO "sig -> %08x:", dst);
			printk(KERN_CONT " %08x", sig);
		}

		*(u32 *)(fw->fw.img + dst) = sig;
		src += 4;
		dst += 4;
	}

	return 0;
}

static void
nvkm_falcon_fw_dtor_sigs(struct nvkm_falcon_fw *fw)
{
	kfree(fw->sigs);
	fw->sigs = NULL;
}

int
nvkm_falcon_fw_boot(struct nvkm_falcon_fw *fw, struct nvkm_subdev *user,
		    bool release, u32 *pmbox0, u32 *pmbox1, u32 mbox0_ok, u32 irqsclr)
{
	struct nvkm_falcon *falcon = fw->falcon;
	int ret;

	ret = nvkm_falcon_get(falcon, user);
	if (ret)
		return ret;

	if (fw->sigs) {
		ret = nvkm_falcon_fw_patch(fw);
		if (ret)
			goto done;

		nvkm_falcon_fw_dtor_sigs(fw);
	}

	FLCNFW_DBG(fw, "resetting");
	fw->func->reset(fw);

	FLCNFW_DBG(fw, "loading");
	if (fw->func->setup) {
		ret = fw->func->setup(fw);
		if (ret)
			goto done;
	}

	ret = fw->func->load(fw);
	if (ret)
		goto done;

	FLCNFW_DBG(fw, "booting");
	ret = fw->func->boot(fw, pmbox0, pmbox1, mbox0_ok, irqsclr);
	if (ret)
		FLCNFW_ERR(fw, "boot failed: %d", ret);
	else
		FLCNFW_DBG(fw, "booted");

done:
	if (ret || release)
		nvkm_falcon_put(falcon, user);
	return ret;
}

int
nvkm_falcon_fw_oneinit(struct nvkm_falcon_fw *fw, struct nvkm_falcon *falcon,
		       struct nvkm_vmm *vmm, struct nvkm_memory *inst)
{
	int ret;

	fw->falcon = falcon;
	fw->vmm = nvkm_vmm_ref(vmm);
	fw->inst = nvkm_memory_ref(inst);

	if (fw->boot) {
		FLCN_DBG(falcon, "mapping %s fw", fw->fw.name);
		ret = nvkm_vmm_get(fw->vmm, 12, nvkm_memory_size(&fw->fw.mem.memory), &fw->vma);
		if (ret) {
			FLCN_ERR(falcon, "get %d", ret);
			return ret;
		}

		ret = nvkm_memory_map(&fw->fw.mem.memory, 0, fw->vmm, fw->vma, NULL, 0);
		if (ret) {
			FLCN_ERR(falcon, "map %d", ret);
			return ret;
		}
	}

	return 0;
}

void
nvkm_falcon_fw_dtor(struct nvkm_falcon_fw *fw)
{
	nvkm_vmm_put(fw->vmm, &fw->vma);
	nvkm_vmm_unref(&fw->vmm);
	nvkm_memory_unref(&fw->inst);
	nvkm_falcon_fw_dtor_sigs(fw);
	nvkm_firmware_dtor(&fw->fw);
}

static const struct nvkm_firmware_func
nvkm_falcon_fw_dma = {
	.type = NVKM_FIRMWARE_IMG_DMA,
};

static const struct nvkm_firmware_func
nvkm_falcon_fw = {
	.type = NVKM_FIRMWARE_IMG_RAM,
};

int
nvkm_falcon_fw_sign(struct nvkm_falcon_fw *fw, u32 sig_base_img, u32 sig_size, const u8 *sigs,
		    int sig_nr_prd, u32 sig_base_prd, int sig_nr_dbg, u32 sig_base_dbg)
{
	fw->sig_base_prd = sig_base_prd;
	fw->sig_base_dbg = sig_base_dbg;
	fw->sig_base_img = sig_base_img;
	fw->sig_size = sig_size;
	fw->sig_nr = sig_nr_prd + sig_nr_dbg;

	fw->sigs = kmalloc_array(fw->sig_nr, fw->sig_size, GFP_KERNEL);
	if (!fw->sigs)
		return -ENOMEM;

	memcpy(fw->sigs, sigs + sig_base_prd, sig_nr_prd * fw->sig_size);
	if (sig_nr_dbg)
		memcpy(fw->sigs + sig_size, sigs + sig_base_dbg, sig_nr_dbg * fw->sig_size);

	return 0;
}

int
nvkm_falcon_fw_ctor(const struct nvkm_falcon_fw_func *func, const char *name,
		    struct nvkm_device *device, bool dma, const void *src, u32 len,
		    struct nvkm_falcon *falcon, struct nvkm_falcon_fw *fw)
{
	const struct nvkm_firmware_func *type = dma ? &nvkm_falcon_fw_dma : &nvkm_falcon_fw;
	int ret;

	fw->func = func;

	ret = nvkm_firmware_ctor(type, name, device, src, len, &fw->fw);
	if (ret)
		return ret;

	return falcon ? nvkm_falcon_fw_oneinit(fw, falcon, NULL, NULL) : 0;
}

int
nvkm_falcon_fw_ctor_hs(const struct nvkm_falcon_fw_func *func, const char *name,
		       struct nvkm_subdev *subdev, const char *bl, const char *img, int ver,
		       struct nvkm_falcon *falcon, struct nvkm_falcon_fw *fw)
{
	const struct firmware *blob;
	const struct nvfw_bin_hdr *hdr;
	const struct nvfw_hs_header *hshdr;
	const struct nvfw_hs_load_header *lhdr;
	const struct nvfw_bl_desc *desc;
	u32 loc, sig;
	int ret;

	ret = nvkm_firmware_load_name(subdev, img, "", ver, &blob);
	if (ret)
		return ret;

	hdr = nvfw_bin_hdr(subdev, blob->data);
	hshdr = nvfw_hs_header(subdev, blob->data + hdr->header_offset);

	ret = nvkm_falcon_fw_ctor(func, name, subdev->device, bl != NULL,
				  blob->data + hdr->data_offset, hdr->data_size, falcon, fw);
	if (ret)
		goto done;

	/* Earlier FW releases by NVIDIA for Nouveau's use aren't in NVIDIA's
	 * standard format, and don't have the indirection seen in the 0x10de
	 * case.
	 */
	switch (hdr->bin_magic) {
	case 0x000010de:
		loc = *(u32 *)(blob->data + hshdr->patch_loc);
		sig = *(u32 *)(blob->data + hshdr->patch_sig);
		break;
	case 0x3b1d14f0:
		loc = hshdr->patch_loc;
		sig = hshdr->patch_sig;
		break;
	default:
		WARN_ON(1);
		ret = -EINVAL;
		goto done;
	}

	ret = nvkm_falcon_fw_sign(fw, loc, hshdr->sig_prod_size, blob->data,
				  1, hshdr->sig_prod_offset + sig,
				  1, hshdr->sig_dbg_offset + sig);
	if (ret)
		goto done;

	lhdr = nvfw_hs_load_header(subdev, blob->data + hshdr->hdr_offset);

	fw->nmem_base_img = 0;
	fw->nmem_base = lhdr->non_sec_code_off;
	fw->nmem_size = lhdr->non_sec_code_size;

	fw->imem_base_img = lhdr->apps[0];
	fw->imem_base = ALIGN(lhdr->apps[0], 0x100);
	fw->imem_size = lhdr->apps[lhdr->num_apps + 0];

	fw->dmem_base_img = lhdr->data_dma_base;
	fw->dmem_base = 0;
	fw->dmem_size = lhdr->data_size;
	fw->dmem_sign = loc - lhdr->data_dma_base;

	if (bl) {
		nvkm_firmware_put(blob);

		ret = nvkm_firmware_load_name(subdev, bl, "", ver, &blob);
		if (ret)
			return ret;

		hdr = nvfw_bin_hdr(subdev, blob->data);
		desc = nvfw_bl_desc(subdev, blob->data + hdr->header_offset);

		fw->boot_addr = desc->start_tag << 8;
		fw->boot_size = desc->code_size;
		fw->boot = kmemdup(blob->data + hdr->data_offset + desc->code_off,
				   fw->boot_size, GFP_KERNEL);
		if (!fw->boot)
			ret = -ENOMEM;
	} else {
		fw->boot_addr = fw->nmem_base;
	}

done:
	if (ret)
		nvkm_falcon_fw_dtor(fw);

	nvkm_firmware_put(blob);
	return ret;
}

int
nvkm_falcon_fw_ctor_hs_v2(const struct nvkm_falcon_fw_func *func, const char *name,
			  struct nvkm_subdev *subdev, const char *img, int ver,
			  struct nvkm_falcon *falcon, struct nvkm_falcon_fw *fw)
{
	const struct nvfw_bin_hdr *hdr;
	const struct nvfw_hs_header_v2 *hshdr;
	const struct nvfw_hs_load_header_v2 *lhdr;
	const struct firmware *blob;
	u32 loc, sig, cnt, *meta;
	int ret;

	ret = nvkm_firmware_load_name(subdev, img, "", ver, &blob);
	if (ret)
		return ret;

	hdr = nvfw_bin_hdr(subdev, blob->data);
	hshdr = nvfw_hs_header_v2(subdev, blob->data + hdr->header_offset);
	meta = (u32 *)(blob->data + hshdr->meta_data_offset);
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

	fw->imem_base_img = lhdr->app[0].offset;
	fw->imem_base = 0;
	fw->imem_size = lhdr->app[0].size;

	fw->dmem_base_img = lhdr->os_data_offset;
	fw->dmem_base = 0;
	fw->dmem_size = lhdr->os_data_size;
	fw->dmem_sign = loc - lhdr->os_data_offset;

	fw->boot_addr = lhdr->app[0].offset;

	fw->fuse_ver = meta[0];
	fw->engine_id = meta[1];
	fw->ucode_id = meta[2];

done:
	if (ret)
		nvkm_falcon_fw_dtor(fw);

	nvkm_firmware_put(blob);
	return ret;
}
