/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022-2024, Advanced Micro Devices, Inc.
 */

#ifndef _AMDXDNA_CTX_H_
#define _AMDXDNA_CTX_H_

#include <linux/bitfield.h>

#include "amdxdna_gem.h"

struct amdxdna_hwctx_priv;

enum ert_cmd_opcode {
	ERT_START_CU = 0,
	ERT_CMD_CHAIN = 19,
	ERT_START_NPU = 20,
	ERT_START_NPU_PREEMPT = 21,
	ERT_START_NPU_PREEMPT_ELF = 22,
	ERT_INVALID_CMD	= ~0U,
};

enum ert_cmd_state {
	ERT_CMD_STATE_INVALID,
	ERT_CMD_STATE_NEW,
	ERT_CMD_STATE_QUEUED,
	ERT_CMD_STATE_RUNNING,
	ERT_CMD_STATE_COMPLETED,
	ERT_CMD_STATE_ERROR,
	ERT_CMD_STATE_ABORT,
	ERT_CMD_STATE_SUBMITTED,
	ERT_CMD_STATE_TIMEOUT,
	ERT_CMD_STATE_NORESPONSE,
};

/*
 * Interpretation of the beginning of data payload for ERT_START_NPU in
 * amdxdna_cmd. The rest of the payload in amdxdna_cmd is regular kernel args.
 */
struct amdxdna_cmd_start_npu {
	u64 buffer;       /* instruction buffer address */
	u32 buffer_size;  /* size of buffer in bytes */
	u32 prop_count;	  /* properties count */
	u32 prop_args[];  /* properties and regular kernel arguments */
};

/*
 * Interpretation of the beginning of data payload for ERT_CMD_CHAIN in
 * amdxdna_cmd. The rest of the payload in amdxdna_cmd is cmd BO handles.
 */
struct amdxdna_cmd_chain {
	u32 command_count;
	u32 submit_index;
	u32 error_index;
	u32 reserved[3];
	u64 data[] __counted_by(command_count);
};

/*
 * Interpretation of the beginning of data payload for ERT_START_NPU_PREEMPT in
 * amdxdna_cmd. The rest of the payload in amdxdna_cmd is regular kernel args.
 */
struct amdxdna_cmd_preempt_data {
	u64 inst_buf;	    /* instruction buffer address */
	u64 save_buf;	    /* save buffer address */
	u64 restore_buf;    /* restore buffer address */
	u32 inst_size;	    /* size of instruction buffer in bytes */
	u32 save_size;	    /* size of save buffer in bytes */
	u32 restore_size;   /* size of restore buffer in bytes */
	u32 inst_prop_cnt;  /* properties count */
	u32 prop_args[];    /* properties and regular kernel arguments */
};

/* Exec buffer command header format */
#define AMDXDNA_CMD_STATE		GENMASK(3, 0)
#define AMDXDNA_CMD_EXTRA_CU_MASK	GENMASK(11, 10)
#define AMDXDNA_CMD_COUNT		GENMASK(22, 12)
#define AMDXDNA_CMD_OPCODE		GENMASK(27, 23)
struct amdxdna_cmd {
	u32 header;
	u32 data[];
};

#define INVALID_CU_IDX		(~0U)

struct amdxdna_hwctx {
	struct amdxdna_client		*client;
	struct amdxdna_hwctx_priv	*priv;
	char				*name;

	u32				id;
	u32				max_opc;
	u32				num_tiles;
	u32				mem_size;
	u32				fw_ctx_id;
	u32				col_list_len;
	u32				*col_list;
	u32				start_col;
	u32				num_col;
	u32				num_unused_col;

	struct amdxdna_qos_info		     qos;
	struct amdxdna_hwctx_param_config_cu *cus;
	u32				syncobj_hdl;

	atomic64_t			job_submit_cnt;
	atomic64_t			job_free_cnt ____cacheline_aligned_in_smp;
};

#define drm_job_to_xdna_job(j) \
	container_of(j, struct amdxdna_sched_job, base)

enum amdxdna_job_opcode {
	SYNC_DEBUG_BO,
	ATTACH_DEBUG_BO,
	DETACH_DEBUG_BO,
};

struct amdxdna_drv_cmd {
	enum amdxdna_job_opcode	opcode;
	u32			result;
};

struct amdxdna_sched_job {
	struct drm_sched_job	base;
	struct kref		refcnt;
	struct amdxdna_hwctx	*hwctx;
	struct mm_struct	*mm;
	/* The fence to notice DRM scheduler that job is done by hardware */
	struct dma_fence	*fence;
	/* user can wait on this fence */
	struct dma_fence	*out_fence;
	bool			job_done;
	bool			job_timeout;
	u64			seq;
	struct amdxdna_drv_cmd	*drv_cmd;
	struct amdxdna_gem_obj	*cmd_bo;
	size_t			bo_cnt;
	struct drm_gem_object	*bos[] __counted_by(bo_cnt);
};

static inline u32
amdxdna_cmd_get_op(struct amdxdna_gem_obj *abo)
{
	struct amdxdna_cmd *cmd = abo->mem.kva;

	return FIELD_GET(AMDXDNA_CMD_OPCODE, cmd->header);
}

static inline void
amdxdna_cmd_set_state(struct amdxdna_gem_obj *abo, enum ert_cmd_state s)
{
	struct amdxdna_cmd *cmd = abo->mem.kva;

	cmd->header &= ~AMDXDNA_CMD_STATE;
	cmd->header |= FIELD_PREP(AMDXDNA_CMD_STATE, s);
}

static inline enum ert_cmd_state
amdxdna_cmd_get_state(struct amdxdna_gem_obj *abo)
{
	struct amdxdna_cmd *cmd = abo->mem.kva;

	return FIELD_GET(AMDXDNA_CMD_STATE, cmd->header);
}

void *amdxdna_cmd_get_payload(struct amdxdna_gem_obj *abo, u32 *size);
u32 amdxdna_cmd_get_cu_idx(struct amdxdna_gem_obj *abo);

void amdxdna_sched_job_cleanup(struct amdxdna_sched_job *job);
void amdxdna_hwctx_remove_all(struct amdxdna_client *client);
int amdxdna_hwctx_walk(struct amdxdna_client *client, void *arg,
		       int (*walk)(struct amdxdna_hwctx *hwctx, void *arg));
int amdxdna_hwctx_sync_debug_bo(struct amdxdna_client *client, u32 debug_bo_hdl);

int amdxdna_cmd_submit(struct amdxdna_client *client,
		       struct amdxdna_drv_cmd *drv_cmd, u32 cmd_bo_hdls,
		       u32 *arg_bo_hdls, u32 arg_bo_cnt,
		       u32 hwctx_hdl, u64 *seq);

int amdxdna_cmd_wait(struct amdxdna_client *client, u32 hwctx_hdl,
		     u64 seq, u32 timeout);

int amdxdna_drm_create_hwctx_ioctl(struct drm_device *dev, void *data, struct drm_file *filp);
int amdxdna_drm_config_hwctx_ioctl(struct drm_device *dev, void *data, struct drm_file *filp);
int amdxdna_drm_destroy_hwctx_ioctl(struct drm_device *dev, void *data, struct drm_file *filp);
int amdxdna_drm_submit_cmd_ioctl(struct drm_device *dev, void *data, struct drm_file *filp);

#endif /* _AMDXDNA_CTX_H_ */
