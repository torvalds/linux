/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2019-2020 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2022,2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef _ATH11K_PCI_H
#define _ATH11K_PCI_H

#include <linux/mhi.h>

#include "core.h"

#define PCIE_SOC_GLOBAL_RESET			0x3008
#define PCIE_SOC_GLOBAL_RESET_V			1

#define WLAON_WARM_SW_ENTRY			0x1f80504
#define WLAON_SOC_RESET_CAUSE_REG		0x01f8060c

#define PCIE_Q6_COOKIE_ADDR			0x01f80500
#define PCIE_Q6_COOKIE_DATA			0xc0000000

/* register to wake the UMAC from power collapse */
#define PCIE_SCRATCH_0_SOC_PCIE_REG		0x4040

/* register used for handshake mechanism to validate UMAC is awake */
#define PCIE_SOC_WAKE_PCIE_LOCAL_REG		0x3004

#define PCIE_PCIE_PARF_LTSSM			0x1e081b0
#define PARM_LTSSM_VALUE			0x111

#define GCC_GCC_PCIE_HOT_RST			0x1e402bc
#define GCC_GCC_PCIE_HOT_RST_VAL		0x10

#define PCIE_PCIE_INT_ALL_CLEAR			0x1e08228
#define PCIE_SMLH_REQ_RST_LINK_DOWN		0x2
#define PCIE_INT_CLEAR_ALL			0xffffffff

#define PCIE_QSERDES_COM_SYSCLK_EN_SEL_REG(x) \
		(ab->hw_params.regs->pcie_qserdes_sysclk_en_sel)
#define PCIE_QSERDES_COM_SYSCLK_EN_SEL_VAL	0x10
#define PCIE_QSERDES_COM_SYSCLK_EN_SEL_MSK	0xffffffff
#define PCIE_PCS_OSC_DTCT_CONFIG1_REG(x) \
		(ab->hw_params.regs->pcie_pcs_osc_dtct_config_base)
#define PCIE_PCS_OSC_DTCT_CONFIG1_VAL		0x02
#define PCIE_PCS_OSC_DTCT_CONFIG2_REG(x) \
		(ab->hw_params.regs->pcie_pcs_osc_dtct_config_base + 0x4)
#define PCIE_PCS_OSC_DTCT_CONFIG2_VAL		0x52
#define PCIE_PCS_OSC_DTCT_CONFIG4_REG(x) \
		(ab->hw_params.regs->pcie_pcs_osc_dtct_config_base + 0xc)
#define PCIE_PCS_OSC_DTCT_CONFIG4_VAL		0xff
#define PCIE_PCS_OSC_DTCT_CONFIG_MSK		0x000000ff

#define WLAON_QFPROM_PWR_CTRL_REG		0x01f8031c
#define QFPROM_PWR_CTRL_VDD4BLOW_MASK		0x4

enum ath11k_pci_flags {
	ATH11K_PCI_ASPM_RESTORE,
};

struct ath11k_pci {
	struct pci_dev *pdev;
	struct ath11k_base *ab;
	u16 dev_id;
	char amss_path[100];
	struct mhi_controller *mhi_ctrl;
	const struct ath11k_msi_config *msi_config;
	u32 register_window;

	/* protects register_window above */
	spinlock_t window_lock;

	/* enum ath11k_pci_flags */
	unsigned long flags;
	u16 link_ctl;
	u64 dma_mask;
};

static inline struct ath11k_pci *ath11k_pci_priv(struct ath11k_base *ab)
{
	return (struct ath11k_pci *)ab->drv_priv;
}

int ath11k_pci_get_msi_irq(struct ath11k_base *ab, unsigned int vector);
#endif
