/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2023-2024, Advanced Micro Devices, Inc.
 */

#ifndef _AIE2_PCI_H_
#define _AIE2_PCI_H_

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
#define CHANN_INDEX(ndev, rbuf_off) \
	(((rbuf_off) - SRAM_REG_OFF((ndev), MBOX_CHANN_OFF)) / CHAN_SLOT_SZ)

#define MBOX_SIZE(ndev) \
({ \
	typeof(ndev) _ndev = (ndev); \
	((_ndev)->priv->mbox_size) ? (_ndev)->priv->mbox_size : \
	pci_resource_len(NDEV2PDEV(_ndev), (_ndev)->xdna->dev_info->mbox_bar); \
})

#define SMU_MPNPUCLK_FREQ_MAX(ndev) ((ndev)->priv->smu_mpnpuclk_freq_max)
#define SMU_HCLK_FREQ_MAX(ndev) ((ndev)->priv->smu_hclk_freq_max)

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

struct amdxdna_fw_ver;
struct amdxdna_hwctx;

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

struct clock_entry {
	char name[16];
	u32 freq_mhz;
};

struct rt_config {
	u32	type;
	u32	value;
};

struct amdxdna_hwctx_priv {
	struct amdxdna_gem_obj		*heap;
	void				*mbox_chann;
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

	u32				total_col;
	struct aie_version		version;
	struct aie_metadata		metadata;
	struct clock_entry		mp_npu_clock;
	struct clock_entry		h_clock;

	/* Mailbox and the management channel */
	struct mailbox			*mbox;
	struct mailbox_channel		*mgmt_chann;
};

#define DEFINE_BAR_OFFSET(reg_name, bar, reg_addr) \
	[reg_name] = {bar##_BAR_INDEX, (reg_addr) - bar##_BAR_BASE}

struct aie2_bar_off_pair {
	int	bar_idx;
	u32	offset;
};

struct amdxdna_dev_priv {
	const char			*fw_path;
	u64				protocol_major;
	u64				protocol_minor;
	struct rt_config		rt_config;
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
	u32				smu_mpnpuclk_freq_max;
	u32				smu_hclk_freq_max;
};

extern const struct amdxdna_dev_ops aie2_ops;

/* aie2_smu.c */
int aie2_smu_init(struct amdxdna_dev_hdl *ndev);
void aie2_smu_fini(struct amdxdna_dev_hdl *ndev);

/* aie2_psp.c */
struct psp_device *aie2m_psp_create(struct drm_device *ddev, struct psp_config *conf);
int aie2_psp_start(struct psp_device *psp);
void aie2_psp_stop(struct psp_device *psp);

/* aie2_message.c */
int aie2_suspend_fw(struct amdxdna_dev_hdl *ndev);
int aie2_resume_fw(struct amdxdna_dev_hdl *ndev);
int aie2_set_runtime_cfg(struct amdxdna_dev_hdl *ndev, u32 type, u64 value);
int aie2_get_runtime_cfg(struct amdxdna_dev_hdl *ndev, u32 type, u64 *value);
int aie2_check_protocol_version(struct amdxdna_dev_hdl *ndev);
int aie2_assign_mgmt_pasid(struct amdxdna_dev_hdl *ndev, u16 pasid);
int aie2_query_aie_version(struct amdxdna_dev_hdl *ndev, struct aie_version *version);
int aie2_query_aie_metadata(struct amdxdna_dev_hdl *ndev, struct aie_metadata *metadata);
int aie2_query_firmware_version(struct amdxdna_dev_hdl *ndev,
				struct amdxdna_fw_ver *fw_ver);
int aie2_create_context(struct amdxdna_dev_hdl *ndev, struct amdxdna_hwctx *hwctx);
int aie2_destroy_context(struct amdxdna_dev_hdl *ndev, struct amdxdna_hwctx *hwctx);
int aie2_map_host_buf(struct amdxdna_dev_hdl *ndev, u32 context_id, u64 addr, u64 size);
int aie2_config_cu(struct amdxdna_hwctx *hwctx);

/* aie2_hwctx.c */
int aie2_hwctx_init(struct amdxdna_hwctx *hwctx);
void aie2_hwctx_fini(struct amdxdna_hwctx *hwctx);
int aie2_hwctx_config(struct amdxdna_hwctx *hwctx, u32 type, u64 value, void *buf, u32 size);

#endif /* _AIE2_PCI_H_ */
