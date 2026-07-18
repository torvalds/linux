/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2023-2026, Advanced Micro Devices, Inc.
 */

#ifndef _AIE2_PCI_H_
#define _AIE2_PCI_H_

#include <drm/amdxdna_accel.h>
#include <linux/limits.h>
#include <linux/semaphore.h>

#include "aie.h"
#include "aie2_msg_priv.h"
#include "amdxdna_mailbox.h"

/* Firmware determines device memory base address and size */
#define AIE2_DEVM_BASE		0x4000000
#define AIE2_DEVM_SIZE		SZ_64M
#define AIE2_DEVM_MAX_SIZE	SZ_512M

#define NDEV2PDEV(ndev) (to_pci_dev((ndev)->aie.xdna->ddev.dev))

#define AIE2_SRAM_OFF(ndev, addr) ((addr) - (ndev)->priv->sram_dev_addr)
#define AIE2_MBOX_OFF(ndev, addr) ((addr) - (ndev)->priv->mbox_dev_addr)

#define SRAM_REG_OFF(ndev, idx) ((ndev)->priv->sram_offs[(idx)].offset)

#define SRAM_GET_ADDR(ndev, idx) \
({ \
	typeof(ndev) _ndev = ndev; \
	((_ndev)->sram_base + SRAM_REG_OFF((_ndev), (idx))); \
})

#define CHAN_SLOT_SZ SZ_8K
#define MBOX_SIZE(ndev) \
({ \
	typeof(ndev) _ndev = (ndev); \
	((_ndev)->priv->mbox_size) ? (_ndev)->priv->mbox_size : \
	pci_resource_len(NDEV2PDEV(_ndev), (_ndev)->aie.xdna->dev_info->mbox_bar); \
})

#if IS_ENABLED(CONFIG_AMD_PMF)
#define AIE2_GET_PMF_NPU_METRICS(metrics) amd_pmf_get_npu_data(metrics)
#define AIE2_GET_PMF_NPU_DATA(field, val)				\
({									\
	struct amd_pmf_npu_metrics _npu_metrics;			\
	int _ret;							\
									\
	_ret = amd_pmf_get_npu_data(&_npu_metrics);			\
	val = _ret ? U32_MAX : _npu_metrics.field;			\
	(_ret);								\
})
#else
#define AIE2_GET_PMF_NPU_METRICS(metrics)				\
({									\
	typeof(metrics) _m = metrics;					\
	memset(_m, 0xff, sizeof(*_m));					\
	(-EOPNOTSUPP);							\
})

#define SENSOR_DEFAULT_npu_power	U32_MAX
#define AIE2_GET_PMF_NPU_DATA(field, val)				\
({									\
	val = SENSOR_DEFAULT_##field;					\
	(-EOPNOTSUPP);							\
})
#endif

enum aie2_sram_reg_idx {
	MBOX_CHANN_OFF = 0,
	FW_ALIVE_OFF,
	SRAM_MAX_INDEX /* Keep this at the end */
};

struct amdxdna_client;
struct amdxdna_fw_ver;
struct amdxdna_hwctx;
struct amdxdna_sched_job;

enum rt_config_category {
	AIE2_RT_CFG_INIT,
	AIE2_RT_CFG_CLK_GATING,
	AIE2_RT_CFG_FORCE_PREEMPT,
	AIE2_RT_CFG_FRAME_BOUNDARY_PREEMPT,
};

struct rt_config {
	u32	type;
	u32	value;
	u32	category;
	unsigned long feature_mask;
};

struct dpm_clk_freq {
	u32	npuclk;
	u32	hclk;
};

/*
 * Define the maximum number of pending commands in a hardware context.
 * Must be power of 2!
 */
#define HWCTX_MAX_CMDS		4
#define get_job_idx(seq) ((seq) & (HWCTX_MAX_CMDS - 1))
struct amdxdna_hwctx_priv {
	struct amdxdna_gem_obj		*heap;
	void				*mbox_chann;

	struct drm_gpu_scheduler	sched;
	struct drm_sched_entity		entity;

	struct mutex			io_lock; /* protect seq and cmd order */
	struct wait_queue_head		job_free_wq;
	u32				num_pending;
	u64				seq;
	struct semaphore		job_sem;
	bool				job_done;

	/* Completed job counter */
	u64				completed;

	struct amdxdna_gem_obj		*cmd_buf[HWCTX_MAX_CMDS];
	struct drm_syncobj		*syncobj;
};

enum aie2_dev_status {
	AIE2_DEV_UNINIT,
	AIE2_DEV_INIT,
	AIE2_DEV_START,
};

struct aie2_exec_msg_ops {
	int (*init_cu_req)(struct amdxdna_gem_obj *cmd_bo, void *req,
			   size_t *size, u32 *msg_op);
	int (*init_dpu_req)(struct amdxdna_gem_obj *cmd_bo, void *req,
			    size_t *size, u32 *msg_op);
	void (*init_chain_req)(void *req, u64 slot_addr, size_t size, u32 cmd_cnt);
	int (*fill_cf_slot)(struct amdxdna_gem_obj *cmd_bo, void *slot, size_t *size);
	int (*fill_dpu_slot)(struct amdxdna_gem_obj *cmd_bo, void *slot, size_t *size);
	int (*fill_preempt_slot)(struct amdxdna_gem_obj *cmd_bo, void *slot, size_t *size);
	int (*fill_elf_slot)(struct amdxdna_gem_obj *cmd_bo, void *slot, size_t *size);
	u32 (*get_chain_msg_op)(u32 cmd_op);
};

enum aie2_tdr_status {
	AIE2_TDR_WAIT,
	AIE2_TDR_SIGNALED,
};

struct amdxdna_dev_hdl {
	struct aie_device		aie;
	const struct amdxdna_dev_priv	*priv;
	void			__iomem *sram_base;
	void			__iomem *mbox_base;

	u32				total_col;
	struct amdxdna_drm_query_aie_version version;
	struct aie2_exec_msg_ops	*exec_msg_ops;

	/* power management and clock*/
	enum amdxdna_power_mode_type	pw_mode;
	u32				dpm_level;
	u32				dft_dpm_level;
	u32				max_dpm_level;
	u32				clk_gating;
	u32				npuclk_freq;
	u32				hclk_freq;
	u32				max_tops;
	u32				curr_tops;
	u32				force_preempt_enabled;
	u32				frame_boundary_preempt;

	/* Mailbox and the management channel */
	struct mailbox			*mbox;
	struct async_events		*async_events;

	enum aie2_dev_status		dev_status;
	u32				hwctx_num;

	struct amdxdna_async_error	last_async_err;
	enum aie2_tdr_status		tdr_status;
};

struct aie2_hw_ops {
	int (*set_dpm)(struct amdxdna_dev_hdl *ndev, u32 dpm_level);
	int (*update_counters)(struct amdxdna_dev_hdl *ndev);
};

#define aie2_update_counters(ndev)				\
({								\
	typeof(ndev) _ndev = ndev;				\
	if (_ndev->priv->hw_ops->update_counters)		\
		_ndev->priv->hw_ops->update_counters(_ndev);	\
})

enum aie2_fw_feature {
	AIE2_NPU_COMMAND,
	AIE2_PREEMPT,
	AIE2_TEMPORAL_ONLY,
	AIE2_APP_HEALTH,
	AIE2_ADD_HOST_BUFFER,
	AIE2_UPDATE_PROPERTY,
	AIE2_GET_DEV_REVISION,
	AIE2_FEATURE_MAX
};

#define AIE2_ALL_FEATURES	GENMASK_ULL(AIE2_FEATURE_MAX - 1, AIE2_NPU_COMMAND)

struct amdxdna_dev_priv {
	const char			*fw_path;
	const struct rt_config		*rt_config;
	const struct dpm_clk_freq	*dpm_clk_tbl;

#define COL_ALIGN_NONE   0
#define COL_ALIGN_NATURE 1
	u32				col_align;
	u32				col_opc;
	u32				mbox_dev_addr;
	/* If mbox_size is 0, use BAR size. See MBOX_SIZE macro */
	u32				mbox_size;
	u32				hwctx_limit;
	u32				sram_dev_addr;
	struct aie_bar_off_pair		sram_offs[SRAM_MAX_INDEX];
	struct aie_bar_off_pair		psp_regs_off[PSP_MAX_REGS];
	struct aie_bar_off_pair		smu_regs_off[SMU_MAX_REGS];
	const struct aie2_hw_ops	*hw_ops;
};

extern const struct amdxdna_dev_ops aie2_ops;

int aie2_runtime_cfg(struct amdxdna_dev_hdl *ndev,
		     enum rt_config_category category, u32 *val);

/* aie2 npu hw config */
extern const struct dpm_clk_freq npu1_dpm_clk_table[];
extern const struct dpm_clk_freq npu4_dpm_clk_table[];
extern const struct rt_config npu1_default_rt_cfg[];
extern const struct rt_config npu4_default_rt_cfg[];
extern const struct amdxdna_fw_feature_tbl npu4_fw_feature_table[];
extern const struct amdxdna_rev_vbnv npu4_rev_vbnv_tbl[];
extern const struct aie2_hw_ops npu4_hw_ops;

/* aie2_pm.c */
int aie2_pm_init(struct amdxdna_dev_hdl *ndev);
int aie2_pm_set_mode(struct amdxdna_dev_hdl *ndev, enum amdxdna_power_mode_type target);
int aie2_pm_set_dpm(struct amdxdna_dev_hdl *ndev, u32 dpm_level);

/* aie2_error.c */
int aie2_error_async_events_alloc(struct amdxdna_dev_hdl *ndev);
void aie2_error_async_events_free(struct amdxdna_dev_hdl *ndev);
int aie2_error_async_msg_thread(void *data);
int aie2_get_array_async_error(struct amdxdna_dev_hdl *ndev,
			       struct amdxdna_drm_get_array *args);

/* aie2_message.c */
void aie2_msg_init(struct amdxdna_dev_hdl *ndev);
void aie2_destroy_mgmt_chann(struct amdxdna_dev_hdl *ndev);
int aie2_suspend_fw(struct amdxdna_dev_hdl *ndev);
int aie2_resume_fw(struct amdxdna_dev_hdl *ndev);
int aie2_set_runtime_cfg(struct amdxdna_dev_hdl *ndev, u32 type, u64 value);
int aie2_get_runtime_cfg(struct amdxdna_dev_hdl *ndev, u32 type, u64 *value);
int aie2_assign_mgmt_pasid(struct amdxdna_dev_hdl *ndev, u16 pasid);
int aie2_query_aie_version(struct amdxdna_dev_hdl *ndev,
			   struct amdxdna_drm_query_aie_version *version);
int aie2_query_aie_metadata(struct amdxdna_dev_hdl *ndev,
			    struct amdxdna_drm_query_aie_metadata *metadata);
int aie2_query_firmware_version(struct amdxdna_dev_hdl *ndev,
				struct amdxdna_fw_ver *fw_ver);
int aie2_query_app_health(struct amdxdna_dev_hdl *ndev, u32 context_id,
			  struct app_health_report *report);
int aie2_get_dev_revision(struct amdxdna_dev_hdl *ndev, enum aie2_dev_revision *rev);
int aie2_create_context(struct amdxdna_dev_hdl *ndev, struct amdxdna_hwctx *hwctx);
int aie2_destroy_context(struct amdxdna_dev_hdl *ndev, struct amdxdna_hwctx *hwctx);
int aie2_map_host_buf(struct amdxdna_dev_hdl *ndev, u32 context_id, u64 addr, u64 size);
int aie2_add_host_buf(struct amdxdna_dev_hdl *ndev, u32 context_id, u64 addr, u64 size);
int aie2_query_status(struct amdxdna_dev_hdl *ndev, char __user *buf, u32 size, u32 *cols_filled);
int aie2_query_telemetry(struct amdxdna_dev_hdl *ndev,
			 char __user *buf, u32 size,
			 struct amdxdna_drm_query_telemetry_header *header);
int aie2_register_asyn_event_msg(struct amdxdna_dev_hdl *ndev, dma_addr_t addr, u32 size,
				 void *handle, int (*cb)(void*, void __iomem *, size_t));
int aie2_config_cu(struct amdxdna_hwctx *hwctx,
		   int (*notify_cb)(void *, void __iomem *, size_t));
int aie2_execbuf(struct amdxdna_hwctx *hwctx, struct amdxdna_sched_job *job,
		 int (*notify_cb)(void *, void __iomem *, size_t));
int aie2_cmdlist_single_execbuf(struct amdxdna_hwctx *hwctx,
				struct amdxdna_sched_job *job,
				int (*notify_cb)(void *, void __iomem *, size_t));
int aie2_cmdlist_multi_execbuf(struct amdxdna_hwctx *hwctx,
			       struct amdxdna_sched_job *job,
			       int (*notify_cb)(void *, void __iomem *, size_t));
int aie2_sync_bo(struct amdxdna_hwctx *hwctx, struct amdxdna_sched_job *job,
		 int (*notify_cb)(void *, void __iomem *, size_t));
int aie2_config_debug_bo(struct amdxdna_hwctx *hwctx, struct amdxdna_sched_job *job,
			 int (*notify_cb)(void *, void __iomem *, size_t));
int aie2_update_prop_time_quota(struct amdxdna_dev_hdl *ndev, u32 us);

/* aie2_hwctx.c */
int aie2_hwctx_init(struct amdxdna_hwctx *hwctx);
void aie2_hwctx_fini(struct amdxdna_hwctx *hwctx);
int aie2_hwctx_config(struct amdxdna_hwctx *hwctx, u32 type, u64 value, void *buf, u32 size);
int aie2_hwctx_sync_debug_bo(struct amdxdna_hwctx *hwctx, u32 debug_bo_hdl);
void aie2_hwctx_suspend(struct amdxdna_client *client);
int aie2_hwctx_resume(struct amdxdna_client *client);
int aie2_cmd_submit(struct amdxdna_hwctx *hwctx, struct amdxdna_sched_job *job, u64 *seq);
void aie2_hmm_invalidate(struct amdxdna_gem_obj *abo, unsigned long cur_seq);
int aie2_hwctx_heap_expand(struct amdxdna_hwctx *hwctx, struct amdxdna_gem_obj *heap);

#endif /* _AIE2_PCI_H_ */
