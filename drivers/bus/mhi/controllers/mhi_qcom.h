/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved. */

#ifndef _MHI_QCOM_
#define _MHI_QCOM_

#define MHI_PCIE_VENDOR_ID (0x17cb)
#define MHI_PCIE_DEBUG_ID (0xffff)

/* runtime suspend timer */
#define MHI_RPM_SUSPEND_TMR_MS (250)
#define MHI_PCI_BAR_NUM (0)
#define MHI_MAX_SFR_LEN (256)

#define PCI_INVALID_READ(val) ((val) == U32_MAX)

#define MHI_IPC_LOG_PAGES (100)

#define MHI_CNTRL_LOG(fmt, ...) do {	\
	struct mhi_qcom_priv *mhi_priv = \
			mhi_controller_get_privdata(mhi_cntrl); \
	ipc_log_string(mhi_priv->cntrl_ipc_log, "[I][%s] " fmt, __func__, \
		       ##__VA_ARGS__); \
} while (0)

#define MHI_CNTRL_ERR(fmt, ...) do {	\
	struct mhi_qcom_priv *mhi_priv = \
			mhi_controller_get_privdata(mhi_cntrl); \
	pr_err("[E][%s] " fmt, __func__, ##__VA_ARGS__); \
	ipc_log_string(mhi_priv->cntrl_ipc_log, "[E][%s] " fmt, __func__, \
		       ##__VA_ARGS__); \
} while (0)

extern const char * const mhi_ee_str[MHI_EE_MAX];
#define TO_MHI_EXEC_STR(ee) (ee >= MHI_EE_MAX ? "INVALID_EE" : mhi_ee_str[ee])

enum mhi_debug_mode {
	MHI_DEBUG_OFF,
	MHI_DEBUG_ON, /* delayed power up */
	MHI_DEBUG_NO_LPM, /* delayed power up, low power modes disabled */
	MHI_DEBUG_MODE_MAX,
};

extern const char * const mhi_debug_mode_str[MHI_DEBUG_MODE_MAX];
#define TO_MHI_DEBUG_MODE_STR(mode) \
	(mode >= MHI_DEBUG_MODE_MAX ? "Invalid" : mhi_debug_mode_str[mode])

enum mhi_suspend_mode {
	MHI_ACTIVE_STATE,
	MHI_DEFAULT_SUSPEND,
	MHI_FAST_LINK_OFF,
	MHI_FAST_LINK_ON,
	MHI_SUSPEND_MODE_MAX,
};

extern const char * const mhi_suspend_mode_str[MHI_SUSPEND_MODE_MAX];
#define TO_MHI_SUSPEND_MODE_STR(mode) \
	(mode >= MHI_SUSPEND_MODE_MAX ? "Invalid" : mhi_suspend_mode_str[mode])

/**
 * struct mhi_pci_dev_info - MHI PCI device specific information
 * @config: MHI controller configuration
 * @device_id: PCI device ID
 * @name: name of the PCI module
 * @fw_image: firmware path (if any)
 * @edl_image: emergency download mode firmware path (if any)
 * @bar_num: PCI base address register to use for MHI MMIO register space
 * @dma_data_width: DMA transfer word size (32 or 64 bits)
 */
struct mhi_pci_dev_info {
	const struct mhi_controller_config *config;
	unsigned int device_id;
	const char *name;
	const char *fw_image;
	const char *edl_image;
	unsigned int bar_num;
	unsigned int dma_data_width;
	bool allow_m1;
	bool skip_forced_suspend;
	bool sfr_support;
	bool timesync;
	bool drv_support;
};

struct mhi_qcom_priv {
	const struct mhi_pci_dev_info *dev_info;
	struct work_struct fatal_worker;
	void *cntrl_ipc_log;
	void *arch_info;
	bool powered_on;
	bool mdm_state;
	bool disable_pci_lpm;
	enum mhi_suspend_mode suspend_mode;
	bool driver_remove;
};

void mhi_deinit_pci_dev(struct pci_dev *pci_dev,
			const struct mhi_pci_dev_info *dev_info);
int mhi_qcom_pci_probe(struct pci_dev *pci_dev,
		       struct mhi_controller *mhi_cntrl,
		       struct mhi_qcom_priv *mhi_priv);

#ifdef CONFIG_ARCH_QCOM

int mhi_arch_pcie_init(struct mhi_controller *mhi_cntrl);
void mhi_arch_pcie_deinit(struct mhi_controller *mhi_cntrl);
int mhi_arch_link_suspend(struct mhi_controller *mhi_cntrl);
int mhi_arch_link_resume(struct mhi_controller *mhi_cntrl);
void mhi_arch_mission_mode_enter(struct mhi_controller *mhi_cntrl);
u64 mhi_arch_time_get(struct mhi_controller *mhi_cntrl);
int mhi_arch_link_lpm_disable(struct mhi_controller *mhi_cntrl);
int mhi_arch_link_lpm_enable(struct mhi_controller *mhi_cntrl);

#else

static inline int mhi_arch_pcie_init(struct mhi_controller *mhi_cntrl)
{
	return 0;
}

static inline void mhi_arch_pcie_deinit(struct mhi_controller *mhi_cntrl)
{
}

static inline int mhi_arch_link_suspend(struct mhi_controller *mhi_cntrl)
{
	return 0;
}

static inline int mhi_arch_link_resume(struct mhi_controller *mhi_cntrl)
{
	return 0;
}

static inline void mhi_arch_mission_mode_enter(struct mhi_controller *mhi_cntrl)
{
}

static inline u64 mhi_arch_time_get(struct mhi_controller *mhi_cntrl)
{
	return 0;
}

static inline int mhi_arch_link_lpm_disable(struct mhi_controller *mhi_cntrl)
{
	return 0;
}

static inline int mhi_arch_link_lpm_enable(struct mhi_controller *mhi_cntrl)
{
	return 0;
}

#endif

#endif /* _MHI_QCOM_ */
