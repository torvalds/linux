/* SPDX-License-Identifier: MIT */
#ifndef __NVFW_HS_H__
#define __NVFW_HS_H__
#include <core/os.h>
struct nvkm_subdev;

struct nvfw_hs_header {
	u32 sig_dbg_offset;
	u32 sig_dbg_size;
	u32 sig_prod_offset;
	u32 sig_prod_size;
	u32 patch_loc;
	u32 patch_sig;
	u32 hdr_offset;
	u32 hdr_size;
};

const struct nvfw_hs_header *nvfw_hs_header(struct nvkm_subdev *, const void *);

struct nvfw_hs_header_v2 {
	u32 sig_prod_offset;
	u32 sig_prod_size;
	u32 patch_loc;
	u32 patch_sig;
	u32 meta_data_offset;
	u32 meta_data_size;
	u32 num_sig;
	u32 header_offset;
	u32 header_size;
};

const struct nvfw_hs_header_v2 *nvfw_hs_header_v2(struct nvkm_subdev *, const void *);

struct nvfw_hs_load_header {
	u32 non_sec_code_off;
	u32 non_sec_code_size;
	u32 data_dma_base;
	u32 data_size;
	u32 num_apps;
	u32 apps[];
};

const struct nvfw_hs_load_header *
nvfw_hs_load_header(struct nvkm_subdev *, const void *);

struct nvfw_hs_load_header_v2 {
	u32 os_code_offset;
	u32 os_code_size;
	u32 os_data_offset;
	u32 os_data_size;
	u32 num_apps;
	struct {
		u32 offset;
		u32 size;
	} app[];
};

const struct nvfw_hs_load_header_v2 *nvfw_hs_load_header_v2(struct nvkm_subdev *, const void *);
#endif
