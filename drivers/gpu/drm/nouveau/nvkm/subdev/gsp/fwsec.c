/*
 * Copyright 2023 Red Hat Inc.
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

#include <subdev/bios.h>
#include <subdev/bios/pmu.h>

#include <nvfw/fw.h>

union nvfw_falcon_appif_hdr {
	struct nvfw_falcon_appif_hdr_v1 {
		u8 ver;
		u8 hdr;
		u8 len;
		u8 cnt;
	} v1;
};

union nvfw_falcon_appif {
	struct nvfw_falcon_appif_v1 {
#define NVFW_FALCON_APPIF_ID_DMEMMAPPER 0x00000004
		u32 id;
		u32 dmem_base;
	} v1;
};

union nvfw_falcon_appif_dmemmapper {
	struct {
		u32 signature;
		u16 version;
		u16 size;
		u32 cmd_in_buffer_offset;
		u32 cmd_in_buffer_size;
		u32 cmd_out_buffer_offset;
		u32 cmd_out_buffer_size;
		u32 nvf_img_data_buffer_offset;
		u32 nvf_img_data_buffer_size;
		u32 printf_buffer_hdr;
		u32 ucode_build_time_stamp;
		u32 ucode_signature;
#define NVFW_FALCON_APPIF_DMEMMAPPER_CMD_FRTS 0x00000015
#define NVFW_FALCON_APPIF_DMEMMAPPER_CMD_SB   0x00000019
		u32 init_cmd;
		u32 ucode_feature;
		u32 ucode_cmd_mask0;
		u32 ucode_cmd_mask1;
		u32 multi_tgt_tbl;
	} v3;
};

struct nvfw_fwsec_frts_cmd {
	struct {
	    u32 ver;
	    u32 hdr;
	    u64 addr;
	    u32 size;
	    u32 flags;
	} read_vbios;
	struct {
	    u32 ver;
	    u32 hdr;
	    u32 addr;
	    u32 size;
#define NVFW_FRTS_CMD_REGION_TYPE_FB 0x00000002
	    u32 type;
	} frts_region;
};

static int
nvkm_gsp_fwsec_patch(struct nvkm_gsp *gsp, struct nvkm_falcon_fw *fw, u32 if_offset, u32 init_cmd)
{
	union nvfw_falcon_appif_hdr *hdr = (void *)(fw->fw.img + fw->dmem_base_img + if_offset);
	const u8 *dmem = fw->fw.img + fw->dmem_base_img;
	int i;

	if (WARN_ON(hdr->v1.ver != 1))
		return -EINVAL;

	for (i = 0; i < hdr->v1.cnt; i++) {
		union nvfw_falcon_appif *app = (void *)((u8 *)hdr + hdr->v1.hdr + i * hdr->v1.len);
		union nvfw_falcon_appif_dmemmapper *dmemmap;
		struct nvfw_fwsec_frts_cmd *frtscmd;

		if (app->v1.id != NVFW_FALCON_APPIF_ID_DMEMMAPPER)
			continue;

		dmemmap = (void *)(dmem + app->v1.dmem_base);
		dmemmap->v3.init_cmd = init_cmd;

		frtscmd = (void *)(dmem + dmemmap->v3.cmd_in_buffer_offset);

		frtscmd->read_vbios.ver = 1;
		frtscmd->read_vbios.hdr = sizeof(frtscmd->read_vbios);
		frtscmd->read_vbios.addr = 0;
		frtscmd->read_vbios.size = 0;
		frtscmd->read_vbios.flags = 2;

		if (init_cmd == NVFW_FALCON_APPIF_DMEMMAPPER_CMD_FRTS) {
			frtscmd->frts_region.ver = 1;
			frtscmd->frts_region.hdr = sizeof(frtscmd->frts_region);
			frtscmd->frts_region.addr = gsp->fb.wpr2.frts.addr >> 12;
			frtscmd->frts_region.size = gsp->fb.wpr2.frts.size >> 12;
			frtscmd->frts_region.type = NVFW_FRTS_CMD_REGION_TYPE_FB;
		}

		break;
	}

	if (WARN_ON(i == hdr->v1.cnt))
		return -EINVAL;

	return 0;
}

union nvfw_falcon_ucode_desc {
	struct nvkm_falcon_ucode_desc_v2 {
		u32 Hdr;
		u32 StoredSize;
		u32 UncompressedSize;
		u32 VirtualEntry;
		u32 InterfaceOffset;
		u32 IMEMPhysBase;
		u32 IMEMLoadSize;
		u32 IMEMVirtBase;
		u32 IMEMSecBase;
		u32 IMEMSecSize;
		u32 DMEMOffset;
		u32 DMEMPhysBase;
		u32 DMEMLoadSize;
		u32 altIMEMLoadSize;
		u32 altDMEMLoadSize;
	} v2;

	struct nvkm_falcon_ucode_desc_v3 {
		u32 Hdr;
		u32 StoredSize;
		u32 PKCDataOffset;
		u32 InterfaceOffset;
		u32 IMEMPhysBase;
		u32 IMEMLoadSize;
		u32 IMEMVirtBase;
		u32 DMEMPhysBase;
		u32 DMEMLoadSize;
		u16 EngineIdMask;
		u8  UcodeId;
		u8  SignatureCount;
		u16 SignatureVersions;
		u16 Reserved;
	} v3;
};

static int
nvkm_gsp_fwsec_v2(struct nvkm_gsp *gsp, const char *name,
		  const struct nvkm_falcon_ucode_desc_v2 *desc, u32 size, u32 init_cmd,
		  struct nvkm_falcon_fw *fw)
{
	struct nvkm_subdev *subdev = &gsp->subdev;
	const struct firmware *bl;
	const struct nvfw_bin_hdr *hdr;
	const struct nvfw_bl_desc *bld;
	int ret;

	/* Build ucode. */
	ret = nvkm_falcon_fw_ctor(gsp->func->fwsec, name, subdev->device, true,
				  (u8 *)desc + size, desc->IMEMLoadSize + desc->DMEMLoadSize,
				  &gsp->falcon, fw);
	if (WARN_ON(ret))
		return ret;

	fw->nmem_base_img = 0;
	fw->nmem_base = desc->IMEMPhysBase;
	fw->nmem_size = desc->IMEMLoadSize - desc->IMEMSecSize;

	fw->imem_base_img = 0;
	fw->imem_base = desc->IMEMSecBase;
	fw->imem_size = desc->IMEMSecSize;

	fw->dmem_base_img = desc->DMEMOffset;
	fw->dmem_base = desc->DMEMPhysBase;
	fw->dmem_size = desc->DMEMLoadSize;

	/* Bootloader. */
	ret = nvkm_firmware_get(subdev, "acr/bl", 0, &bl);
	if (ret)
		return ret;

	hdr = nvfw_bin_hdr(subdev, bl->data);
	bld = nvfw_bl_desc(subdev, bl->data + hdr->header_offset);

	fw->boot_addr = bld->start_tag << 8;
	fw->boot_size = bld->code_size;
	fw->boot = kmemdup(bl->data + hdr->data_offset + bld->code_off, fw->boot_size, GFP_KERNEL);
	if (!fw->boot)
		ret = -ENOMEM;

	nvkm_firmware_put(bl);

	/* Patch in interface data. */
	return nvkm_gsp_fwsec_patch(gsp, fw, desc->InterfaceOffset, init_cmd);
}

static int
nvkm_gsp_fwsec_v3(struct nvkm_gsp *gsp, const char *name,
		  const struct nvkm_falcon_ucode_desc_v3 *desc, u32 size, u32 init_cmd,
		  struct nvkm_falcon_fw *fw)
{
	struct nvkm_device *device = gsp->subdev.device;
	struct nvkm_bios *bios = device->bios;
	int ret;

	/* Build ucode. */
	ret = nvkm_falcon_fw_ctor(gsp->func->fwsec, name, device, true,
				  (u8 *)desc + size, desc->IMEMLoadSize + desc->DMEMLoadSize,
				  &gsp->falcon, fw);
	if (WARN_ON(ret))
		return ret;

	fw->imem_base_img = 0;
	fw->imem_base = desc->IMEMPhysBase;
	fw->imem_size = desc->IMEMLoadSize;
	fw->dmem_base_img = desc->IMEMLoadSize;
	fw->dmem_base = desc->DMEMPhysBase;
	fw->dmem_size = ALIGN(desc->DMEMLoadSize, 256);
	fw->dmem_sign = desc->PKCDataOffset;
	fw->boot_addr = 0;
	fw->fuse_ver = desc->SignatureVersions;
	fw->ucode_id = desc->UcodeId;
	fw->engine_id = desc->EngineIdMask;

	/* Patch in signature. */
	ret = nvkm_falcon_fw_sign(fw, fw->dmem_base_img + desc->PKCDataOffset, 96 * 4,
				  nvbios_pointer(bios, 0), desc->SignatureCount,
				  (u8 *)desc + 0x2c - (u8 *)nvbios_pointer(bios, 0), 0, 0);
	if (WARN_ON(ret))
		return ret;

	/* Patch in interface data. */
	return nvkm_gsp_fwsec_patch(gsp, fw, desc->InterfaceOffset, init_cmd);
}

static int
nvkm_gsp_fwsec(struct nvkm_gsp *gsp, const char *name, u32 init_cmd)
{
	struct nvkm_subdev *subdev = &gsp->subdev;
	struct nvkm_device *device = subdev->device;
	struct nvkm_bios *bios = device->bios;
	const union nvfw_falcon_ucode_desc *desc;
	struct nvbios_pmuE flcn_ucode;
	u8 idx, ver, hdr;
	u32 data;
	u16 size, vers;
	struct nvkm_falcon_fw fw = {};
	u32 mbox0 = 0;
	int ret;

	/* Lookup in VBIOS. */
	for (idx = 0; (data = nvbios_pmuEp(bios, idx, &ver, &hdr, &flcn_ucode)); idx++) {
		if (flcn_ucode.type == 0x85)
			break;
	}

	if (WARN_ON(!data))
		return -EINVAL;

	/* Deteremine version. */
	desc = nvbios_pointer(bios, flcn_ucode.data);
	if (WARN_ON(!(desc->v2.Hdr & 0x00000001)))
		return -EINVAL;

	size = (desc->v2.Hdr & 0xffff0000) >> 16;
	vers = (desc->v2.Hdr & 0x0000ff00) >> 8;

	switch (vers) {
	case 2: ret = nvkm_gsp_fwsec_v2(gsp, name, &desc->v2, size, init_cmd, &fw); break;
	case 3: ret = nvkm_gsp_fwsec_v3(gsp, name, &desc->v3, size, init_cmd, &fw); break;
	default:
		nvkm_error(subdev, "%s(v%d): version unknown\n", name, vers);
		return -EINVAL;
	}

	if (ret) {
		nvkm_error(subdev, "%s(v%d): %d\n", name, vers, ret);
		return ret;
	}

	/* Boot. */
	ret = nvkm_falcon_fw_boot(&fw, subdev, true, &mbox0, NULL, 0, 0);
	nvkm_falcon_fw_dtor(&fw);
	if (ret)
		return ret;

	return 0;
}

int
nvkm_gsp_fwsec_sb(struct nvkm_gsp *gsp)
{
	struct nvkm_subdev *subdev = &gsp->subdev;
	struct nvkm_device *device = subdev->device;
	int ret;
	u32 err;

	ret = nvkm_gsp_fwsec(gsp, "fwsec-sb", NVFW_FALCON_APPIF_DMEMMAPPER_CMD_SB);
	if (ret)
		return ret;

	/* Verify. */
	err = nvkm_rd32(device, 0x001400 + (0xf * 4)) & 0x0000ffff;
	if (err) {
		nvkm_error(subdev, "fwsec-sb: 0x%04x\n", err);
		return -EIO;
	}

	return 0;
}

int
nvkm_gsp_fwsec_frts(struct nvkm_gsp *gsp)
{
	struct nvkm_subdev *subdev = &gsp->subdev;
	struct nvkm_device *device = subdev->device;
	int ret;
	u32 err, wpr2_lo, wpr2_hi;

	ret = nvkm_gsp_fwsec(gsp, "fwsec-frts", NVFW_FALCON_APPIF_DMEMMAPPER_CMD_FRTS);
	if (ret)
		return ret;

	/* Verify. */
	err = nvkm_rd32(device, 0x001400 + (0xe * 4)) >> 16;
	if (err) {
		nvkm_error(subdev, "fwsec-frts: 0x%04x\n", err);
		return -EIO;
	}

	wpr2_lo = nvkm_rd32(device, 0x1fa824);
	wpr2_hi = nvkm_rd32(device, 0x1fa828);
	nvkm_debug(subdev, "fwsec-frts: WPR2 @ %08x - %08x\n", wpr2_lo, wpr2_hi);
	return 0;
}
