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
#include <core/subdev.h>
#include <nvfw/ls.h>

static void
nvfw_ls_desc_head(struct nvkm_subdev *subdev,
		  const struct nvfw_ls_desc_head *hdr)
{
	char *date;

	nvkm_debug(subdev, "lsUcodeImgDesc:\n");
	nvkm_debug(subdev, "\tdescriptorSize       : %d\n",
			   hdr->descriptor_size);
	nvkm_debug(subdev, "\timageSize            : %d\n", hdr->image_size);
	nvkm_debug(subdev, "\ttoolsVersion         : 0x%x\n",
			   hdr->tools_version);
	nvkm_debug(subdev, "\tappVersion           : 0x%x\n", hdr->app_version);

	date = kstrndup(hdr->date, sizeof(hdr->date), GFP_KERNEL);
	nvkm_debug(subdev, "\tdate                 : %s\n", date);
	kfree(date);

	nvkm_debug(subdev, "\tbootloaderStartOffset: 0x%x\n",
			   hdr->bootloader_start_offset);
	nvkm_debug(subdev, "\tbootloaderSize       : 0x%x\n",
			   hdr->bootloader_size);
	nvkm_debug(subdev, "\tbootloaderImemOffset : 0x%x\n",
			   hdr->bootloader_imem_offset);
	nvkm_debug(subdev, "\tbootloaderEntryPoint : 0x%x\n",
			   hdr->bootloader_entry_point);

	nvkm_debug(subdev, "\tappStartOffset       : 0x%x\n",
			   hdr->app_start_offset);
	nvkm_debug(subdev, "\tappSize              : 0x%x\n", hdr->app_size);
	nvkm_debug(subdev, "\tappImemOffset        : 0x%x\n",
			   hdr->app_imem_offset);
	nvkm_debug(subdev, "\tappImemEntry         : 0x%x\n",
			   hdr->app_imem_entry);
	nvkm_debug(subdev, "\tappDmemOffset        : 0x%x\n",
			   hdr->app_dmem_offset);
	nvkm_debug(subdev, "\tappResidentCodeOffset: 0x%x\n",
			   hdr->app_resident_code_offset);
	nvkm_debug(subdev, "\tappResidentCodeSize  : 0x%x\n",
			   hdr->app_resident_code_size);
	nvkm_debug(subdev, "\tappResidentDataOffset: 0x%x\n",
			   hdr->app_resident_data_offset);
	nvkm_debug(subdev, "\tappResidentDataSize  : 0x%x\n",
			   hdr->app_resident_data_size);
}

const struct nvfw_ls_desc *
nvfw_ls_desc(struct nvkm_subdev *subdev, const void *data)
{
	const struct nvfw_ls_desc *hdr = data;
	int i;

	nvfw_ls_desc_head(subdev, &hdr->head);

	nvkm_debug(subdev, "\tnbOverlays           : %d\n", hdr->nb_overlays);
	for (i = 0; i < ARRAY_SIZE(hdr->load_ovl); i++) {
		nvkm_debug(subdev, "\tloadOvl[%d]          : 0x%x %d\n", i,
			   hdr->load_ovl[i].start, hdr->load_ovl[i].size);
	}
	nvkm_debug(subdev, "\tcompressed           : %d\n", hdr->compressed);

	return hdr;
}

const struct nvfw_ls_desc_v1 *
nvfw_ls_desc_v1(struct nvkm_subdev *subdev, const void *data)
{
	const struct nvfw_ls_desc_v1 *hdr = data;
	int i;

	nvfw_ls_desc_head(subdev, &hdr->head);

	nvkm_debug(subdev, "\tnbImemOverlays       : %d\n",
			   hdr->nb_imem_overlays);
	nvkm_debug(subdev, "\tnbDmemOverlays       : %d\n",
			   hdr->nb_imem_overlays);
	for (i = 0; i < ARRAY_SIZE(hdr->load_ovl); i++) {
		nvkm_debug(subdev, "\tloadOvl[%2d]          : 0x%x %d\n", i,
			   hdr->load_ovl[i].start, hdr->load_ovl[i].size);
	}
	nvkm_debug(subdev, "\tcompressed           : %d\n", hdr->compressed);

	return hdr;
}

const struct nvfw_ls_desc_v2 *
nvfw_ls_desc_v2(struct nvkm_subdev *subdev, const void *data)
{
	const struct nvfw_ls_desc_v2 *hdr = data;
	char *date;
	int i;

	nvkm_debug(subdev, "lsUcodeImgDesc:\n");
	nvkm_debug(subdev, "\tdescriptorSize       : %d\n", hdr->descriptor_size);
	nvkm_debug(subdev, "\timageSize            : %d\n", hdr->image_size);
	nvkm_debug(subdev, "\ttoolsVersion         : 0x%x\n", hdr->tools_version);
	nvkm_debug(subdev, "\tappVersion           : 0x%x\n", hdr->app_version);

	date = kstrndup(hdr->date, sizeof(hdr->date), GFP_KERNEL);
	nvkm_debug(subdev, "\tdate                 : %s\n", date);
	kfree(date);

	nvkm_debug(subdev, "\tsecureBootloader     : 0x%x\n", hdr->secure_bootloader);
	nvkm_debug(subdev, "\tbootloaderStartOffset: 0x%x\n", hdr->bootloader_start_offset);
	nvkm_debug(subdev, "\tbootloaderSize       : 0x%x\n", hdr->bootloader_size);
	nvkm_debug(subdev, "\tbootloaderImemOffset : 0x%x\n", hdr->bootloader_imem_offset);
	nvkm_debug(subdev, "\tbootloaderEntryPoint : 0x%x\n", hdr->bootloader_entry_point);

	nvkm_debug(subdev, "\tappStartOffset       : 0x%x\n", hdr->app_start_offset);
	nvkm_debug(subdev, "\tappSize              : 0x%x\n", hdr->app_size);
	nvkm_debug(subdev, "\tappImemOffset        : 0x%x\n", hdr->app_imem_offset);
	nvkm_debug(subdev, "\tappImemEntry         : 0x%x\n", hdr->app_imem_entry);
	nvkm_debug(subdev, "\tappDmemOffset        : 0x%x\n", hdr->app_dmem_offset);
	nvkm_debug(subdev, "\tappResidentCodeOffset: 0x%x\n", hdr->app_resident_code_offset);
	nvkm_debug(subdev, "\tappResidentCodeSize  : 0x%x\n", hdr->app_resident_code_size);
	nvkm_debug(subdev, "\tappResidentDataOffset: 0x%x\n", hdr->app_resident_data_offset);
	nvkm_debug(subdev, "\tappResidentDataSize  : 0x%x\n", hdr->app_resident_data_size);

	nvkm_debug(subdev, "\tnbImemOverlays       : %d\n", hdr->nb_imem_overlays);
	nvkm_debug(subdev, "\tnbDmemOverlays       : %d\n", hdr->nb_dmem_overlays);
	for (i = 0; i < ARRAY_SIZE(hdr->load_ovl); i++) {
		nvkm_debug(subdev, "\tloadOvl[%d]          : 0x%x %d\n", i,
			   hdr->load_ovl[i].start, hdr->load_ovl[i].size);
	}

	return hdr;
}

const struct nvfw_ls_hsbl_bin_hdr *
nvfw_ls_hsbl_bin_hdr(struct nvkm_subdev *subdev, const void *data)
{
	const struct nvfw_ls_hsbl_bin_hdr *hdr = data;

	nvkm_debug(subdev, "lsHsblBinHdr:\n");
	nvkm_debug(subdev, "\tbinMagic         : 0x%08x\n", hdr->bin_magic);
	nvkm_debug(subdev, "\tbinVer           : %d\n", hdr->bin_ver);
	nvkm_debug(subdev, "\tbinSize          : %d\n", hdr->bin_size);
	nvkm_debug(subdev, "\theaderOffset     : 0x%x\n", hdr->header_offset);
	return hdr;
}

const struct nvfw_ls_hsbl_hdr *
nvfw_ls_hsbl_hdr(struct nvkm_subdev *subdev, const void *data)
{
	const struct nvfw_ls_hsbl_hdr *hdr = data;

	nvkm_debug(subdev, "lsHsblHdr:\n");
	nvkm_debug(subdev, "\tsigProdOffset    : 0x%x\n", hdr->sig_prod_offset);
	nvkm_debug(subdev, "\tsigProdSize      : 0x%x\n", hdr->sig_prod_size);
	nvkm_debug(subdev, "\tpatchLoc         : 0x%x\n", hdr->patch_loc);
	nvkm_debug(subdev, "\tpatchSig         : 0x%x\n", hdr->patch_sig);
	nvkm_debug(subdev, "\tmetadataOffset   : 0x%x\n", hdr->meta_data_offset);
	nvkm_debug(subdev, "\tmetadataSize     : 0x%x\n", hdr->meta_data_size);
	nvkm_debug(subdev, "\tnumSig           : 0x%x\n", hdr->num_sig);
	return hdr;
}
