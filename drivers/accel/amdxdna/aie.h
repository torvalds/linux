/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2026, Advanced Micro Devices, Inc.
 */
#ifndef _AIE_H_
#define _AIE_H_

#include <drm/amdxdna_accel.h>
#include "amdxdna_pci_drv.h"
#include "amdxdna_mailbox.h"

#define AIE_INTERVAL	20000	/* us */
#define AIE_TIMEOUT	1000000	/* us */

struct psp_device;
struct smu_device;

struct aie_device {
	struct amdxdna_dev *xdna;
	struct mailbox_channel *mgmt_chann;
	struct xdna_mailbox_chann_res mgmt_x2i;
	struct xdna_mailbox_chann_res mgmt_i2x;
	u32 mgmt_chan_idx;
	u32 mgmt_prot_major;
	u32 mgmt_prot_minor;
	unsigned long feature_mask;

	struct psp_device *psp_hdl;
	struct smu_device *smu_hdl;

	struct amdxdna_drm_query_aie_metadata metadata;
};

#define DECLARE_AIE_MSG(name, op) \
	DECLARE_XDNA_MSG_COMMON(name, op, -1)
#define AIE_FEATURE_ON(aie, feature) test_bit(feature, &(aie)->feature_mask)

#define PSP_REG_BAR(ndev, idx) ((ndev)->priv->psp_regs_off[(idx)].bar_idx)
#define PSP_REG_OFF(ndev, idx) ((ndev)->priv->psp_regs_off[(idx)].offset)

#define SMU_REG_BAR(ndev, idx) ((ndev)->priv->smu_regs_off[(idx)].bar_idx)
#define SMU_REG_OFF(ndev, idx) ((ndev)->priv->smu_regs_off[(idx)].offset)

#define DEFINE_BAR_OFFSET(reg_name, bar, reg_addr) \
	[reg_name] = {bar##_BAR_INDEX, (reg_addr) - bar##_BAR_BASE}

enum smu_reg_idx {
	SMU_CMD_REG = 0,
	SMU_ARG_REG,
	SMU_INTR_REG,
	SMU_RESP_REG,
	SMU_OUT_REG,
	SMU_MAX_REGS /* Keep this at the end */
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
	PSP_PWAITMODE_REG,
	PSP_MAX_REGS /* Keep this at the end */
};

struct aie_bar_off_pair {
	int	bar_idx;
	u32	offset;
};

struct smu_config {
	void __iomem    *smu_regs[SMU_MAX_REGS];
};

struct psp_config {
	const void		*fw_buf;
	u32			fw_size;
	const void              *certfw_buf;
	u32                     certfw_size;
	void __iomem		*psp_regs[PSP_MAX_REGS];
	u32			arg2_mask;
	u32			notify_val;
};

/* Device revision to VBNV string mapping table entry */
struct amdxdna_rev_vbnv {
	u32		revision;
	const char	*vbnv;
};

/* aie.c */
void aie_dump_mgmt_chann_debug(struct aie_device *aie);
void aie_destroy_chann(struct aie_device *aie, struct mailbox_channel **chann);
int aie_send_mgmt_msg_wait(struct aie_device *aie, struct xdna_mailbox_msg *msg);
int aie_check_protocol(struct aie_device *aie, u32 fw_major, u32 fw_minor);
void amdxdna_vbnv_init(struct amdxdna_dev *xdna);
int amdxdna_get_metadata(struct aie_device *aie, struct amdxdna_client *client,
			 struct amdxdna_drm_get_info *args);
void *amdxdna_alloc_msg_buffer(struct amdxdna_dev *xdna, u32 *size,
			       dma_addr_t *dma_addr);
void amdxdna_free_msg_buffer(struct amdxdna_dev *xdna, size_t size,
			     void *cpu_addr, dma_addr_t dma_addr);

/* aie_psp.c */
struct psp_device *aiem_psp_create(struct drm_device *ddev, struct psp_config *conf);
int aie_psp_start(struct psp_device *psp);
void aie_psp_stop(struct psp_device *psp);
int aie_psp_waitmode_poll(struct psp_device *psp);

/* aie_smu.c */
struct smu_device *aiem_smu_create(struct drm_device *ddev, struct smu_config *conf);
int aie_smu_init(struct smu_device *smu);
void aie_smu_fini(struct smu_device *smu);
int aie_smu_set_clocks(struct smu_device *smu, u32 *npuclk, u32 *hclk);
int aie_smu_set_dpm(struct smu_device *smu, u32 dpm_level);

#endif /* _AIE_H_ */
