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
#include <core/falcon.h>
#include <core/firmware.h>
#include <nvfw/fw.h>
#include <nvfw/ls.h>

void
nvkm_acr_lsfw_del(struct nvkm_acr_lsfw *lsfw)
{
	nvkm_blob_dtor(&lsfw->img);
	kfree(lsfw->sigs);
	nvkm_firmware_put(lsfw->sig);
	list_del(&lsfw->head);
	kfree(lsfw);
}

void
nvkm_acr_lsfw_del_all(struct nvkm_acr *acr)
{
	struct nvkm_acr_lsfw *lsfw, *lsft;
	list_for_each_entry_safe(lsfw, lsft, &acr->lsfw, head) {
		nvkm_acr_lsfw_del(lsfw);
	}
}

static struct nvkm_acr_lsfw *
nvkm_acr_lsfw_get(struct nvkm_acr *acr, enum nvkm_acr_lsf_id id)
{
	struct nvkm_acr_lsfw *lsfw;
	list_for_each_entry(lsfw, &acr->lsfw, head) {
		if (lsfw->id == id)
			return lsfw;
	}
	return NULL;
}

struct nvkm_acr_lsfw *
nvkm_acr_lsfw_add(const struct nvkm_acr_lsf_func *func, struct nvkm_acr *acr,
		 struct nvkm_falcon *falcon, enum nvkm_acr_lsf_id id)
{
	struct nvkm_acr_lsfw *lsfw;

	if (!acr || list_empty(&acr->hsfw))
		return ERR_PTR(-ENOSYS);

	lsfw = nvkm_acr_lsfw_get(acr, id);
	if (lsfw && lsfw->func) {
		nvkm_error(&acr->subdev, "LSFW %d redefined\n", id);
		return ERR_PTR(-EEXIST);
	}

	if (!lsfw) {
		if (!(lsfw = kzalloc(sizeof(*lsfw), GFP_KERNEL)))
			return ERR_PTR(-ENOMEM);

		lsfw->id = id;
		list_add_tail(&lsfw->head, &acr->lsfw);
	}

	lsfw->func = func;
	lsfw->falcon = falcon;
	return lsfw;
}

static struct nvkm_acr_lsfw *
nvkm_acr_lsfw_load_sig_image_desc_(struct nvkm_subdev *subdev,
				   struct nvkm_falcon *falcon,
				   enum nvkm_acr_lsf_id id,
				   const char *path, int ver,
				   const struct nvkm_acr_lsf_func *func,
				   const struct firmware **pdesc)
{
	struct nvkm_acr *acr = subdev->device->acr;
	struct nvkm_acr_lsfw *lsfw;
	int ret;

	if (IS_ERR((lsfw = nvkm_acr_lsfw_add(func, acr, falcon, id))))
		return lsfw;

	ret = nvkm_firmware_load_name(subdev, path, "sig", ver, &lsfw->sig);
	if (ret)
		goto done;

	ret = nvkm_firmware_load_blob(subdev, path, "image", ver, &lsfw->img);
	if (ret)
		goto done;

	ret = nvkm_firmware_load_name(subdev, path, "desc", ver, pdesc);
done:
	if (ret) {
		nvkm_acr_lsfw_del(lsfw);
		return ERR_PTR(ret);
	}

	return lsfw;
}

static void
nvkm_acr_lsfw_from_desc(const struct nvfw_ls_desc_head *desc,
			struct nvkm_acr_lsfw *lsfw)
{
	lsfw->bootloader_size = ALIGN(desc->bootloader_size, 256);
	lsfw->bootloader_imem_offset = desc->bootloader_imem_offset;

	lsfw->app_size = ALIGN(desc->app_size, 256);
	lsfw->app_start_offset = desc->app_start_offset;
	lsfw->app_imem_entry = desc->app_imem_entry;
	lsfw->app_resident_code_offset = desc->app_resident_code_offset;
	lsfw->app_resident_code_size = desc->app_resident_code_size;
	lsfw->app_resident_data_offset = desc->app_resident_data_offset;
	lsfw->app_resident_data_size = desc->app_resident_data_size;

	lsfw->ucode_size = ALIGN(lsfw->app_resident_data_offset, 256) +
			   lsfw->bootloader_size;
	lsfw->data_size = lsfw->app_size + lsfw->bootloader_size -
			  lsfw->ucode_size;
}

int
nvkm_acr_lsfw_load_sig_image_desc(struct nvkm_subdev *subdev,
				  struct nvkm_falcon *falcon,
				  enum nvkm_acr_lsf_id id,
				  const char *path, int ver,
				  const struct nvkm_acr_lsf_func *func)
{
	const struct firmware *fw;
	struct nvkm_acr_lsfw *lsfw;

	lsfw = nvkm_acr_lsfw_load_sig_image_desc_(subdev, falcon, id, path, ver,
						  func, &fw);
	if (IS_ERR(lsfw))
		return PTR_ERR(lsfw);

	nvkm_acr_lsfw_from_desc(&nvfw_ls_desc(subdev, fw->data)->head, lsfw);
	nvkm_firmware_put(fw);
	return 0;
}

int
nvkm_acr_lsfw_load_sig_image_desc_v1(struct nvkm_subdev *subdev,
				     struct nvkm_falcon *falcon,
				     enum nvkm_acr_lsf_id id,
				     const char *path, int ver,
				     const struct nvkm_acr_lsf_func *func)
{
	const struct firmware *fw;
	struct nvkm_acr_lsfw *lsfw;

	lsfw = nvkm_acr_lsfw_load_sig_image_desc_(subdev, falcon, id, path, ver,
						  func, &fw);
	if (IS_ERR(lsfw))
		return PTR_ERR(lsfw);

	nvkm_acr_lsfw_from_desc(&nvfw_ls_desc_v1(subdev, fw->data)->head, lsfw);
	nvkm_firmware_put(fw);
	return 0;
}

int
nvkm_acr_lsfw_load_sig_image_desc_v2(struct nvkm_subdev *subdev,
				     struct nvkm_falcon *falcon,
				     enum nvkm_acr_lsf_id id,
				     const char *path, int ver,
				     const struct nvkm_acr_lsf_func *func)
{
	const struct firmware *fw;
	struct nvkm_acr_lsfw *lsfw;
	const struct nvfw_ls_desc_v2 *desc;
	int ret = 0;

	lsfw = nvkm_acr_lsfw_load_sig_image_desc_(subdev, falcon, id, path, ver, func, &fw);
	if (IS_ERR(lsfw))
		return PTR_ERR(lsfw);

	desc = nvfw_ls_desc_v2(subdev, fw->data);

	lsfw->secure_bootloader = desc->secure_bootloader;
	lsfw->bootloader_size = ALIGN(desc->bootloader_size, 256);
	lsfw->bootloader_imem_offset = desc->bootloader_imem_offset;

	lsfw->app_size = ALIGN(desc->app_size, 256);
	lsfw->app_start_offset = desc->app_start_offset;
	lsfw->app_imem_entry = desc->app_imem_entry;
	lsfw->app_resident_code_offset = desc->app_resident_code_offset;
	lsfw->app_resident_code_size = desc->app_resident_code_size;
	lsfw->app_resident_data_offset = desc->app_resident_data_offset;
	lsfw->app_resident_data_size = desc->app_resident_data_size;
	lsfw->app_imem_offset = desc->app_imem_offset;
	lsfw->app_dmem_offset = desc->app_dmem_offset;

	lsfw->ucode_size = ALIGN(lsfw->app_resident_data_offset, 256) + lsfw->bootloader_size;
	lsfw->data_size = lsfw->app_size + lsfw->bootloader_size - lsfw->ucode_size;

	nvkm_firmware_put(fw);

	if (lsfw->secure_bootloader) {
		const struct firmware *hsbl;
		const struct nvfw_ls_hsbl_bin_hdr *hdr;
		const struct nvfw_ls_hsbl_hdr *hshdr;
		u32 sig, cnt, *meta;

		ret = nvkm_firmware_load_name(subdev, path, "hs_bl_sig", ver, &hsbl);
		if (ret)
			return ret;

		hdr = nvfw_ls_hsbl_bin_hdr(subdev, hsbl->data);
		hshdr = nvfw_ls_hsbl_hdr(subdev, hsbl->data + hdr->header_offset);
		meta = (u32 *)(hsbl->data + hshdr->meta_data_offset);
		sig = *(u32 *)(hsbl->data + hshdr->patch_sig);
		cnt = *(u32 *)(hsbl->data + hshdr->num_sig);

		lsfw->fuse_ver = meta[0];
		lsfw->engine_id = meta[1];
		lsfw->ucode_id = meta[2];
		lsfw->sig_size = hshdr->sig_prod_size / cnt;
		lsfw->sig_nr = cnt;
		lsfw->sigs = kmemdup(hsbl->data + hshdr->sig_prod_offset + sig,
				     lsfw->sig_nr * lsfw->sig_size, GFP_KERNEL);
		nvkm_firmware_put(hsbl);
		if (!lsfw->sigs)
			ret = -ENOMEM;
	}

	return ret;
}

int
nvkm_acr_lsfw_load_bl_inst_data_sig(struct nvkm_subdev *subdev,
				    struct nvkm_falcon *falcon,
				    enum nvkm_acr_lsf_id id,
				    const char *path, int ver,
				    const struct nvkm_acr_lsf_func *func)
{
	struct nvkm_acr *acr = subdev->device->acr;
	struct nvkm_acr_lsfw *lsfw;
	const struct firmware *bl = NULL, *inst = NULL, *data = NULL;
	const struct nvfw_bin_hdr *hdr;
	const struct nvfw_bl_desc *desc;
	u32 *bldata;
	int ret;

	if (IS_ERR((lsfw = nvkm_acr_lsfw_add(func, acr, falcon, id))))
		return PTR_ERR(lsfw);

	ret = nvkm_firmware_load_name(subdev, path, "bl", ver, &bl);
	if (ret)
		goto done;

	hdr = nvfw_bin_hdr(subdev, bl->data);
	desc = nvfw_bl_desc(subdev, bl->data + hdr->header_offset);
	bldata = (void *)(bl->data + hdr->data_offset);

	ret = nvkm_firmware_load_name(subdev, path, "inst", ver, &inst);
	if (ret)
		goto done;

	ret = nvkm_firmware_load_name(subdev, path, "data", ver, &data);
	if (ret)
		goto done;

	ret = nvkm_firmware_load_name(subdev, path, "sig", ver, &lsfw->sig);
	if (ret)
		goto done;

	lsfw->bootloader_size = ALIGN(desc->code_size, 256);
	lsfw->bootloader_imem_offset = desc->start_tag << 8;

	lsfw->app_start_offset = lsfw->bootloader_size;
	lsfw->app_imem_entry = 0;
	lsfw->app_resident_code_offset = 0;
	lsfw->app_resident_code_size = ALIGN(inst->size, 256);
	lsfw->app_resident_data_offset = lsfw->app_resident_code_size;
	lsfw->app_resident_data_size = ALIGN(data->size, 256);
	lsfw->app_size = lsfw->app_resident_code_size +
			 lsfw->app_resident_data_size;

	lsfw->img.size = lsfw->bootloader_size + lsfw->app_size;
	if (!(lsfw->img.data = kzalloc(lsfw->img.size, GFP_KERNEL))) {
		ret = -ENOMEM;
		goto done;
	}

	memcpy(lsfw->img.data, bldata, lsfw->bootloader_size);
	memcpy(lsfw->img.data + lsfw->app_start_offset +
	       lsfw->app_resident_code_offset, inst->data, inst->size);
	memcpy(lsfw->img.data + lsfw->app_start_offset +
	       lsfw->app_resident_data_offset, data->data, data->size);

	lsfw->ucode_size = ALIGN(lsfw->app_resident_data_offset, 256) +
			   lsfw->bootloader_size;
	lsfw->data_size = lsfw->app_size + lsfw->bootloader_size -
			  lsfw->ucode_size;

done:
	if (ret)
		nvkm_acr_lsfw_del(lsfw);
	nvkm_firmware_put(data);
	nvkm_firmware_put(inst);
	nvkm_firmware_put(bl);
	return ret;
}

int
nvkm_acr_lsfw_load_bl_sig_net(struct nvkm_subdev *subdev,
			      struct nvkm_falcon *falcon,
			      enum nvkm_acr_lsf_id id,
			      const char *path, int ver,
			      const struct nvkm_acr_lsf_func *func,
			      const void *inst_data, u32 inst_size,
			      const void *data_data, u32 data_size)
{
	struct nvkm_acr *acr = subdev->device->acr;
	struct nvkm_acr_lsfw *lsfw;
	const struct firmware _inst = { .data = inst_data, .size = inst_size };
	const struct firmware _data = { .data = data_data, .size = data_size };
	const struct firmware *bl = NULL, *inst = &_inst, *data = &_data;
	const struct {
	    int bin_magic;
	    int bin_version;
	    int bin_size;
	    int header_offset;
	    int header_size;
	} *hdr;
	u32 *bldata;
	int ret;

	if (IS_ERR((lsfw = nvkm_acr_lsfw_add(func, acr, falcon, id))))
		return PTR_ERR(lsfw);

	ret = nvkm_firmware_load_name(subdev, path, "bl", ver, &bl);
	if (ret)
		goto done;

	hdr = (const void *)bl->data;
	bldata = (void *)(bl->data + hdr->header_offset);

	ret = nvkm_firmware_load_name(subdev, path, "sig", ver, &lsfw->sig);
	if (ret)
		goto done;

	lsfw->bootloader_size = ALIGN(hdr->header_size, 256);
	lsfw->bootloader_imem_offset = func->bl_entry;

	lsfw->app_start_offset = lsfw->bootloader_size;
	lsfw->app_imem_entry = 0;
	lsfw->app_resident_code_offset = 0;
	lsfw->app_resident_code_size = ALIGN(inst->size, 256);
	lsfw->app_resident_data_offset = lsfw->app_resident_code_size;
	lsfw->app_resident_data_size = ALIGN(data->size, 256);
	lsfw->app_imem_offset = 0;
	lsfw->app_dmem_offset = 0;
	lsfw->app_size = lsfw->app_resident_code_size + lsfw->app_resident_data_size;

	lsfw->img.size = lsfw->bootloader_size + lsfw->app_size;
	if (!(lsfw->img.data = kzalloc(lsfw->img.size, GFP_KERNEL))) {
		ret = -ENOMEM;
		goto done;
	}

	memcpy(lsfw->img.data, bldata, lsfw->bootloader_size);
	memcpy(lsfw->img.data + lsfw->app_start_offset +
	       lsfw->app_resident_code_offset, inst->data, inst->size);
	memcpy(lsfw->img.data + lsfw->app_start_offset +
	       lsfw->app_resident_data_offset, data->data, data->size);

	lsfw->ucode_size = ALIGN(lsfw->app_resident_data_offset, 256) +
			   lsfw->bootloader_size;
	lsfw->data_size = lsfw->app_size + lsfw->bootloader_size -
			  lsfw->ucode_size;

done:
	if (ret)
		nvkm_acr_lsfw_del(lsfw);
	nvkm_firmware_put(bl);
	return ret;
}
