/* SPDX-License-Identifier: MIT */
#ifndef __NVFW_FLCN_H__
#define __NVFW_FLCN_H__
#include <core/os.h>
struct nvkm_subdev;

struct loader_config {
	u32 dma_idx;
	u32 code_dma_base;
	u32 code_size_total;
	u32 code_size_to_load;
	u32 code_entry_point;
	u32 data_dma_base;
	u32 data_size;
	u32 overlay_dma_base;
	u32 argc;
	u32 argv;
	u32 code_dma_base1;
	u32 data_dma_base1;
	u32 overlay_dma_base1;
};

void
loader_config_dump(struct nvkm_subdev *, const struct loader_config *);

struct loader_config_v1 {
	u32 reserved;
	u32 dma_idx;
	u64 code_dma_base;
	u32 code_size_total;
	u32 code_size_to_load;
	u32 code_entry_point;
	u64 data_dma_base;
	u32 data_size;
	u64 overlay_dma_base;
	u32 argc;
	u32 argv;
} __packed;

void
loader_config_v1_dump(struct nvkm_subdev *, const struct loader_config_v1 *);

struct flcn_bl_dmem_desc {
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
	u32 code_dma_base1;
	u32 data_dma_base1;
};

void
flcn_bl_dmem_desc_dump(struct nvkm_subdev *, const struct flcn_bl_dmem_desc *);

struct flcn_bl_dmem_desc_v1 {
	u32 reserved[4];
	u32 signature[4];
	u32 ctx_dma;
	u64 code_dma_base;
	u32 non_sec_code_off;
	u32 non_sec_code_size;
	u32 sec_code_off;
	u32 sec_code_size;
	u32 code_entry_point;
	u64 data_dma_base;
	u32 data_size;
} __packed;

void flcn_bl_dmem_desc_v1_dump(struct nvkm_subdev *,
			       const struct flcn_bl_dmem_desc_v1 *);

struct flcn_bl_dmem_desc_v2 {
	u32 reserved[4];
	u32 signature[4];
	u32 ctx_dma;
	u64 code_dma_base;
	u32 non_sec_code_off;
	u32 non_sec_code_size;
	u32 sec_code_off;
	u32 sec_code_size;
	u32 code_entry_point;
	u64 data_dma_base;
	u32 data_size;
	u32 argc;
	u32 argv;
} __packed;

void flcn_bl_dmem_desc_v2_dump(struct nvkm_subdev *,
			       const struct flcn_bl_dmem_desc_v2 *);
#endif
