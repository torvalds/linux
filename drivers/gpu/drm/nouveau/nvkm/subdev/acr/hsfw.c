/*
 * Copyright 2019 Red Hat Inc.
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

#include <core/firmware.h>

#include <nvfw/fw.h>
#include <nvfw/hs.h>

static void
nvkm_acr_hsfw_del(struct nvkm_acr_hsfw *hsfw)
{
	list_del(&hsfw->head);
	kfree(hsfw->imem);
	kfree(hsfw->image);
	kfree(hsfw->sig.prod.data);
	kfree(hsfw->sig.dbg.data);
	kfree(hsfw);
}

void
nvkm_acr_hsfw_del_all(struct nvkm_acr *acr)
{
	struct nvkm_acr_hsfw *hsfw, *hsft;
	list_for_each_entry_safe(hsfw, hsft, &acr->hsfw, head) {
		nvkm_acr_hsfw_del(hsfw);
	}
}

static int
nvkm_acr_hsfw_load_image(struct nvkm_acr *acr, const char *name, int ver,
			 struct nvkm_acr_hsfw *hsfw)
{
	struct nvkm_subdev *subdev = &acr->subdev;
	const struct firmware *fw;
	const struct nvfw_bin_hdr *hdr;
	const struct nvfw_hs_header *fwhdr;
	const struct nvfw_hs_load_header *lhdr;
	u32 loc, sig;
	int ret;

	ret = nvkm_firmware_get(subdev, name, ver, &fw);
	if (ret < 0)
		return ret;

	hdr = nvfw_bin_hdr(subdev, fw->data);
	fwhdr = nvfw_hs_header(subdev, fw->data + hdr->header_offset);

	/* Earlier FW releases by NVIDIA for Nouveau's use aren't in NVIDIA's
	 * standard format, and don't have the indirection seen in the 0x10de
	 * case.
	 */
	switch (hdr->bin_magic) {
	case 0x000010de:
		loc = *(u32 *)(fw->data + fwhdr->patch_loc);
		sig = *(u32 *)(fw->data + fwhdr->patch_sig);
		break;
	case 0x3b1d14f0:
		loc = fwhdr->patch_loc;
		sig = fwhdr->patch_sig;
		break;
	default:
		ret = -EINVAL;
		goto done;
	}

	lhdr = nvfw_hs_load_header(subdev, fw->data + fwhdr->hdr_offset);

	if (!(hsfw->image = kmalloc(hdr->data_size, GFP_KERNEL))) {
		ret = -ENOMEM;
		goto done;
	}

	memcpy(hsfw->image, fw->data + hdr->data_offset, hdr->data_size);
	hsfw->image_size = hdr->data_size;
	hsfw->non_sec_addr = lhdr->non_sec_code_off;
	hsfw->non_sec_size = lhdr->non_sec_code_size;
	hsfw->sec_addr = lhdr->apps[0];
	hsfw->sec_size = lhdr->apps[lhdr->num_apps];
	hsfw->data_addr = lhdr->data_dma_base;
	hsfw->data_size = lhdr->data_size;

	hsfw->sig.prod.size = fwhdr->sig_prod_size;
	hsfw->sig.prod.data = kmemdup(fw->data + fwhdr->sig_prod_offset + sig,
				      hsfw->sig.prod.size, GFP_KERNEL);
	if (!hsfw->sig.prod.data) {
		ret = -ENOMEM;
		goto done;
	}

	hsfw->sig.dbg.size = fwhdr->sig_dbg_size;
	hsfw->sig.dbg.data = kmemdup(fw->data + fwhdr->sig_dbg_offset + sig,
				     hsfw->sig.dbg.size, GFP_KERNEL);
	if (!hsfw->sig.dbg.data) {
		ret = -ENOMEM;
		goto done;
	}

	hsfw->sig.patch_loc = loc;
done:
	nvkm_firmware_put(fw);
	return ret;
}

static int
nvkm_acr_hsfw_load_bl(struct nvkm_acr *acr, const char *name, int ver,
		      struct nvkm_acr_hsfw *hsfw)
{
	struct nvkm_subdev *subdev = &acr->subdev;
	const struct nvfw_bin_hdr *hdr;
	const struct nvfw_bl_desc *desc;
	const struct firmware *fw;
	u8 *data;
	int ret;

	ret = nvkm_firmware_get(subdev, name, ver, &fw);
	if (ret)
		return ret;

	hdr = nvfw_bin_hdr(subdev, fw->data);
	desc = nvfw_bl_desc(subdev, fw->data + hdr->header_offset);
	data = (void *)fw->data + hdr->data_offset;

	hsfw->imem_size = desc->code_size;
	hsfw->imem_tag = desc->start_tag;
	hsfw->imem = kmemdup(data + desc->code_off, desc->code_size, GFP_KERNEL);
	nvkm_firmware_put(fw);
	if (!hsfw->imem)
		return -ENOMEM;
	else
		return 0;
}

int
nvkm_acr_hsfw_load(struct nvkm_acr *acr, const char *bl, const char *fw,
		   const char *name, int version,
		   const struct nvkm_acr_hsf_fwif *fwif)
{
	struct nvkm_acr_hsfw *hsfw;
	int ret;

	if (!(hsfw = kzalloc(sizeof(*hsfw), GFP_KERNEL)))
		return -ENOMEM;

	hsfw->func = fwif->func;
	hsfw->name = name;
	list_add_tail(&hsfw->head, &acr->hsfw);

	ret = nvkm_acr_hsfw_load_bl(acr, bl, version, hsfw);
	if (ret)
		goto done;

	ret = nvkm_acr_hsfw_load_image(acr, fw, version, hsfw);
done:
	if (ret)
		nvkm_acr_hsfw_del(hsfw);
	return ret;
}
