/*
 * Copyright 2022 Advanced Micro Devices, Inc.
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
 *
 */

#ifndef AMDGPU_XCP_H
#define AMDGPU_XCP_H

#include <linux/pci.h>
#include <linux/xarray.h>

#include "amdgpu_ctx.h"

#define MAX_XCP 8

#define AMDGPU_XCP_MODE_NONE -1
#define AMDGPU_XCP_MODE_TRANS -2

#define AMDGPU_XCP_FL_NONE 0
#define AMDGPU_XCP_FL_LOCKED (1 << 0)

#define AMDGPU_XCP_NO_PARTITION (~0)

#define AMDGPU_XCP_OPS_KFD	(1 << 0)

struct amdgpu_fpriv;

enum AMDGPU_XCP_IP_BLOCK {
	AMDGPU_XCP_GFXHUB,
	AMDGPU_XCP_GFX,
	AMDGPU_XCP_SDMA,
	AMDGPU_XCP_VCN,
	AMDGPU_XCP_MAX_BLOCKS
};

enum AMDGPU_XCP_STATE {
	AMDGPU_XCP_PREPARE_SUSPEND,
	AMDGPU_XCP_SUSPEND,
	AMDGPU_XCP_PREPARE_RESUME,
	AMDGPU_XCP_RESUME,
};

enum amdgpu_xcp_res_id {
	AMDGPU_XCP_RES_XCC,
	AMDGPU_XCP_RES_DMA,
	AMDGPU_XCP_RES_DEC,
	AMDGPU_XCP_RES_JPEG,
	AMDGPU_XCP_RES_MAX,
};

struct amdgpu_xcp_res_details {
	enum amdgpu_xcp_res_id id;
	u8 num_inst;
	u8 num_shared;
	struct kobject kobj;
};

struct amdgpu_xcp_cfg {
	u8 mode;
	struct amdgpu_xcp_res_details xcp_res[AMDGPU_XCP_RES_MAX];
	u8 num_res;
	struct amdgpu_xcp_mgr *xcp_mgr;
	struct kobject kobj;
	u16 compatible_nps_modes;
};

struct amdgpu_xcp_ip_funcs {
	int (*prepare_suspend)(void *handle, uint32_t inst_mask);
	int (*suspend)(void *handle, uint32_t inst_mask);
	int (*prepare_resume)(void *handle, uint32_t inst_mask);
	int (*resume)(void *handle, uint32_t inst_mask);
};

struct amdgpu_xcp_ip {
	struct amdgpu_xcp_ip_funcs *ip_funcs;
	uint32_t inst_mask;

	enum AMDGPU_XCP_IP_BLOCK ip_id;
	bool valid;
};

struct amdgpu_xcp {
	struct amdgpu_xcp_ip ip[AMDGPU_XCP_MAX_BLOCKS];

	uint8_t id;
	uint8_t mem_id;
	bool valid;
	atomic_t	ref_cnt;
	struct drm_device *ddev;
	struct drm_device *rdev;
	struct drm_device *pdev;
	struct drm_driver *driver;
	struct drm_vma_offset_manager *vma_offset_manager;
	struct amdgpu_sched	gpu_sched[AMDGPU_HW_IP_NUM][AMDGPU_RING_PRIO_MAX];
	struct amdgpu_xcp_mgr *xcp_mgr;
	struct kobject kobj;
	uint64_t unique_id;
};

struct amdgpu_xcp_mgr {
	struct amdgpu_device *adev;
	struct mutex xcp_lock;
	struct amdgpu_xcp_mgr_funcs *funcs;

	struct amdgpu_xcp xcp[MAX_XCP];
	uint8_t num_xcps;
	int8_t mode;

	 /* Used to determine KFD memory size limits per XCP */
	unsigned int num_xcp_per_mem_partition;
	struct amdgpu_xcp_cfg *xcp_cfg;
	uint32_t supp_xcp_modes;
	uint32_t avail_xcp_modes;
};

struct amdgpu_xcp_mgr_funcs {
	int (*switch_partition_mode)(struct amdgpu_xcp_mgr *xcp_mgr, int mode,
				     int *num_xcps);
	int (*query_partition_mode)(struct amdgpu_xcp_mgr *xcp_mgr);
	int (*get_ip_details)(struct amdgpu_xcp_mgr *xcp_mgr, int xcp_id,
			      enum AMDGPU_XCP_IP_BLOCK ip_id,
			      struct amdgpu_xcp_ip *ip);
	int (*get_xcp_mem_id)(struct amdgpu_xcp_mgr *xcp_mgr,
			      struct amdgpu_xcp *xcp, uint8_t *mem_id);
	int (*get_xcp_res_info)(struct amdgpu_xcp_mgr *xcp_mgr,
				int mode,
				struct amdgpu_xcp_cfg *xcp_cfg);
	int (*prepare_suspend)(struct amdgpu_xcp_mgr *xcp_mgr, int xcp_id);
	int (*suspend)(struct amdgpu_xcp_mgr *xcp_mgr, int xcp_id);
	int (*prepare_resume)(struct amdgpu_xcp_mgr *xcp_mgr, int xcp_id);
	int (*resume)(struct amdgpu_xcp_mgr *xcp_mgr, int xcp_id);
};

int amdgpu_xcp_prepare_suspend(struct amdgpu_xcp_mgr *xcp_mgr, int xcp_id);
int amdgpu_xcp_suspend(struct amdgpu_xcp_mgr *xcp_mgr, int xcp_id);
int amdgpu_xcp_prepare_resume(struct amdgpu_xcp_mgr *xcp_mgr, int xcp_id);
int amdgpu_xcp_resume(struct amdgpu_xcp_mgr *xcp_mgr, int xcp_id);

int amdgpu_xcp_mgr_init(struct amdgpu_device *adev, int init_mode,
			int init_xcps, struct amdgpu_xcp_mgr_funcs *xcp_funcs);
int amdgpu_xcp_init(struct amdgpu_xcp_mgr *xcp_mgr, int num_xcps, int mode);
int amdgpu_xcp_query_partition_mode(struct amdgpu_xcp_mgr *xcp_mgr, u32 flags);
int amdgpu_xcp_switch_partition_mode(struct amdgpu_xcp_mgr *xcp_mgr, int mode);
int amdgpu_xcp_restore_partition_mode(struct amdgpu_xcp_mgr *xcp_mgr);
int amdgpu_xcp_get_partition(struct amdgpu_xcp_mgr *xcp_mgr,
			     enum AMDGPU_XCP_IP_BLOCK ip, int instance);

int amdgpu_xcp_get_inst_details(struct amdgpu_xcp *xcp,
				enum AMDGPU_XCP_IP_BLOCK ip,
				uint32_t *inst_mask);

int amdgpu_xcp_dev_register(struct amdgpu_device *adev,
				const struct pci_device_id *ent);
void amdgpu_xcp_dev_unplug(struct amdgpu_device *adev);
int amdgpu_xcp_open_device(struct amdgpu_device *adev,
			   struct amdgpu_fpriv *fpriv,
			   struct drm_file *file_priv);
void amdgpu_xcp_release_sched(struct amdgpu_device *adev,
			      struct amdgpu_ctx_entity *entity);
int amdgpu_xcp_select_scheds(struct amdgpu_device *adev,
			     u32 hw_ip, u32 hw_prio,
			     struct amdgpu_fpriv *fpriv,
			     unsigned int *num_scheds,
			     struct drm_gpu_scheduler ***scheds);
void amdgpu_xcp_update_supported_modes(struct amdgpu_xcp_mgr *xcp_mgr);
int amdgpu_xcp_update_partition_sched_list(struct amdgpu_device *adev);
int amdgpu_xcp_pre_partition_switch(struct amdgpu_xcp_mgr *xcp_mgr, u32 flags);
int amdgpu_xcp_post_partition_switch(struct amdgpu_xcp_mgr *xcp_mgr, u32 flags);
void amdgpu_xcp_sysfs_init(struct amdgpu_device *adev);
void amdgpu_xcp_sysfs_fini(struct amdgpu_device *adev);

static inline int amdgpu_xcp_get_num_xcp(struct amdgpu_xcp_mgr *xcp_mgr)
{
	if (!xcp_mgr)
		return 1;
	else
		return xcp_mgr->num_xcps;
}

static inline struct amdgpu_xcp *
amdgpu_get_next_xcp(struct amdgpu_xcp_mgr *xcp_mgr, int *from)
{
	if (!xcp_mgr)
		return NULL;

	while (*from < MAX_XCP) {
		if (xcp_mgr->xcp[*from].valid)
			return &xcp_mgr->xcp[*from];
		++(*from);
	}

	return NULL;
}

#define for_each_xcp(xcp_mgr, xcp, i)                            \
	for (i = 0, xcp = amdgpu_get_next_xcp(xcp_mgr, &i); xcp; \
	     ++i, xcp = amdgpu_get_next_xcp(xcp_mgr, &i))

#endif
