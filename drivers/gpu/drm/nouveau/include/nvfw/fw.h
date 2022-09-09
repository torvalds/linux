/* SPDX-License-Identifier: MIT */
#ifndef __NVFW_FW_H__
#define __NVFW_FW_H__
#include <core/os.h>
struct nvkm_subdev;

struct nvfw_bin_hdr {
	u32 bin_magic;
	u32 bin_ver;
	u32 bin_size;
	u32 header_offset;
	u32 data_offset;
	u32 data_size;
};

const struct nvfw_bin_hdr *nvfw_bin_hdr(struct nvkm_subdev *, const void *);

struct nvfw_bl_desc {
	u32 start_tag;
	u32 dmem_load_off;
	u32 code_off;
	u32 code_size;
	u32 data_off;
	u32 data_size;
};

const struct nvfw_bl_desc *nvfw_bl_desc(struct nvkm_subdev *, const void *);
#endif
