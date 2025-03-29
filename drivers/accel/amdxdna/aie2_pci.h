/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2023-2024, Advanced Micro Devices, Inc.
 */

#ifndef _AIE2_PCI_H_
#define _AIE2_PCI_H_

#include <drm/amdxdna_accel.h>
#include <linux/semaphore.h>

#include "amdxdna_mailbox.h"

#define AIE2_INTERVAL	20000	/* us */
#define AIE2_TIMEOUT	1000000	/* us */

/* Firmware determines device memory base address and size */
#define AIE2_DEVM_BASE	0x4000000
#define AIE2_DEVM_SIZE	SZ_64M

#define NDEV2PDEV(ndev) (to_pci_dev((ndev)->xdna->ddev.dev))

#define AIE2_SRAM_OFF(ndev, addr) ((addr) - (ndev)->priv->sram_dev_addr)
#define AIE2_MBOX_OFF(ndev, addr) ((addr) - (ndev)->priv->mbox_dev_addr)

#define PSP_REG_BAR(ndev, idx) ((ndev)->priv->psp_regs_off[(idx)].bar_idx)
#define PSP_REG_OFF(ndev, idx) ((ndev)->priv->psp_regs_off[(idx)].offset)
#define SRAM_REG_OFF(ndev, idx) ((ndev)->priv->sram_offs[(idx)].offset)

#define SMU_REG(ndev, idx) \
({ \
	typeof(ndev) _ndev = ndev; \
	((_ndev)->smu_base + (_ndev)->priv->smu_regs_off[(idx)].offset); \
})
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
	pci_resource_len(NDEV2PDEV(_ndev), (_ndev)->xdna->dev_info->mbox_bar); \
})

enum aie2_smu_reg_idx {
	SMU_CMD_REG = 0,
	SMU_ARG_REG,
	SMU_INTR_REG,
	SMU_RESP_REG,
	SMU_OUT_REG,
	SMU_MAX_REGS /* Keep this at the end */
};

enum aie2_sram_reg_idx {
	MBOX_CHANN_OFF = 0,
	FW_ALIVE_OFF,
	SRAM_MAX_INDEX /* Keep this at the end */
};

enum psp_reg_idx {
	PSP_CMD_REG = 0,
	PSP_ARG0_REG,
	PSP_ARG1_REG,
	PSP_ARG2_REG,
	PSP_NUM_IN_REGS, /* number of input registers */
	PSP_INTR_REG = PSP_NUM_IN_REGS,
	PSP_STATUS_REG,
	PSP_RESP_REG,
	PSP_MAX_REGS /* Keep this at the end */
};

struct amdxdna_client;
struct amdxdna_fw_ver;
struct amdxdna_hwctx;
struct amdxdna_sched_job;

struct psp_config {
	const void	*fw_buf;
	u32		fw_size;
	void __iomem	*psp_regs[PSP_MAX_REGS];
};

struct aie_version {
	u16 major;
	u16 minor;
};

struct aie_tile_metadata {
	u16 row_count;
	u16 row_start;
	u16 dma_channel_count;
	u16 lock_count;
	u16 event_reg_count;
};

struct aie_metadata {
	u32 size;
	u16 cols;
	u16 rows;
	struct aie_version version;
	struct aie_tile_metadata core;
	struct aie_tile_metadata mem;
	struct aie_tile_metadata shim;
};

enum rt_config_category {
	AIE2_RT_CFG_INIT,
	AIE2_RT_CFG_CLK_GATING,
};

struct rt_config {
	u32	type;
	u32	value;
	u32	category;
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

struct amdxdna_dev_hdl {
	struct amdxdna_dev		*xdna;
	const struct amdxdna_dev_priv	*priv;
	void			__iomem *sram_base;
	void			__iomem *smu_base;
	void			__iomem *mbox_base;
	struct psp_device		*psp_hdl;

	struct xdna_mailbox_chann_res	mgmt_x2i;
	struct xdna_mailbox_chann_res	mgmt_i2x;
	u32				mgmt_chan_idx;
	u32				mgmt_prot_major;
	u32				mgmt_prot_minor;

	u32				total_col;
	struct aie_version		version;
	struct aie_metadata		metadata;

	/* power management and clock*/
	enum amdxdna_power_mode_type	pw_mode;
	u32				dpm_level;
	u32				dft_dpm_level;
	u32				max_dpm_level;
	u32				clk_gating;
	u32				npuclk_freq;
	u32				hclk_freq;

	/* Mailbox and the management channel */
	struct mailbox			*mbox;
	struct mailbox_channel		*mgmt_chann;
	struct async_events		*async_events;

	enum aie2_dev_status		dev_status;
	u32				hwctx_num;
};

#define DEFINE_BAR_OFFSET(reg_name, bar, reg_addr) \
	[reg_name] = {bar##_BAR_INDEX, (reg_addr) - bar##_BAR_BASE}

struct aie2_bar_off_pair {
	int	bar_idx;
	u32	offset;
};

struct aie2_hw_ops {
	int (*set_dpm)(struct amdxdna_dev_hdl *ndev, u32 dpm_level);
};

struct amdxdna_dev_priv {
	const char			*fw_path;
	u64				protocol_major;
	u64				protocol_minor;
	const struct rt_config		*rt_config;
	const struct dpm_clk_freq	*dpm_clk_tbl;

#define COL_ALIGN_NONE   0
#define COL_ALIGN_NATURE 1
	u32				col_align;
	u32				mbox_dev_addr;
	/* If mbox_size is 0, use BAR size. See MBOX_SIZE macro */
	u32				mbox_size;
	u32				sram_dev_addr;
	struct aie2_bar_off_pair	sram_offs[SRAM_MAX_INDEX];
	struct aie2_bar_off_pair	psp_regs_off[PSP_MAX_REGS];
	struct aie2_bar_off_pair	smu_regs_off[SMU_MAX_REGS];
	struct aie2_hw_ops		hw_ops;
};

extern const struct amdxdna_dev_ops aie2_ops;

int aie2_runtime_cfg(struct amdxdna_dev_hdl *ndev,
		     enum rt_config_category category, u32 *val);

/* aie2 npu hw config */
extern const struct dpm_clk_freq npu1_dpm_clk_table[];
extern const struct dpm_clk_freq npu4_dpm_clk_table[];
extern const struct rt_config npu1_default_rt_cfg[];
extern const struct rt_config npu4_default_rt_cfg[];

/* aie2_smu.c */
int aie2_smu_init(struct amdxdna_dev_hdl *ndev);
void aie2_smu_fini(struct amdxdna_dev_hdl *ndev);
int npu1_set_dpm(struct amdxdna_dev_hdl *ndev, u32 dpm_level);
int npu4_set_dpm(struct amdxdna_dev_hdl *ndev, u32 dpm_level);

/* aie2_pm.c */
int aie2_pm_init(struct amdxdna_dev_hdl *ndev);
int aie2_pm_set_mode(struct amdxdna_dev_hdl *ndev, enum amdxdna_power_mode_type target);

/* aie2_psp.c */
struct psp_device *aie2m_psp_create(struct drm_device *ddev, struct psp_config *conf);
int aie2_psp_start(struct psp_device *psp);
void aie2_psp_stop(struct psp_device *psp);

/* aie2_error.c */
int aie2_error_async_events_alloc(struct amdxdna_dev_hdl *ndev);
void aie2_error_async_events_free(struct amdxdna_dev_hdl *ndev);
int aie2_error_async_events_send(struct amdxdna_dev_hdl *ndev);
int aie2_error_async_msg_thread(void *data);

/* aie2_message.c */
int aie2_suspend_fw(struct amdxdna_dev_hdl *ndev);
int aie2_resume_fw(struct amdxdna_dev_hdl *ndev);
int aie2_set_runtime_cfg(struct amdxdna_dev_hdl *ndev, u32 type, u64 value);
int aie2_get_runtime_cfg(struct amdxdna_dev_hdl *ndev, u32 type, u64 *value);
int aie2_assign_mgmt_pasid(struct amdxdna_dev_hdl *ndev, u16 pasid);
int aie2_query_aie_version(struct amdxdna_dev_hdl *ndev, struct aie_version *version);
int aie2_query_aie_metadata(struct amdxdna_dev_hdl *ndev, struct aie_metadata *metadata);
int aie2_query_firmware_version(struct amdxdna_dev_hdl *ndev,
				struct amdxdna_fw_ver *fw_ver);
int aie2_create_context(struct amdxdna_dev_hdl *ndev, struct amdxdna_hwctx *hwctx);
int aie2_destroy_context(struct amdxdna_dev_hdl *ndev, struct amdxdna_hwctx *hwctx);
int aie2_map_host_buf(struct amdxdna_dev_hdl *ndev, u32 context_id, u64 addr, u64 size);
int aie2_query_status(struct amdxdna_dev_hdl *ndev, char __user *buf, u32 size, u32 *cols_filled);
int aie2_register_asyn_event_msg(struct amdxdna_dev_hdl *ndev, dma_addr_t addr, u32 size,
				 void *handle, int (*cb)(void*, void __iomem *, size_t));
int aie2_config_cu(struct amdxdna_hwctx *hwctx);
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

/* aie2_hwctx.c */
int aie2_hwctx_init(struct amdxdna_hwctx *hwctx);
void aie2_hwctx_fini(struct amdxdna_hwctx *hwctx);
int aie2_hwctx_config(struct amdxdna_hwctx *hwctx, u32 type, u64 value, void *buf, u32 size);
void aie2_hwctx_suspend(struct amdxdna_hwctx *hwctx);
void aie2_hwctx_resume(struct amdxdna_hwctx *hwctx);
int aie2_cmd_submit(struct amdxdna_hwctx *hwctx, struct amdxdna_sched_job *job, u64 *seq);
void aie2_hmm_invalidate(struct amdxdna_gem_obj *abo, unsigned long cur_seq);
void aie2_restart_ctx(struct amdxdna_client *client);

#endif /* _AIE2_PCI_H_ */
