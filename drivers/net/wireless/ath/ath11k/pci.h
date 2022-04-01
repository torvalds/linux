/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2019-2020 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2022, Qualcomm Innovation Center, Inc. All rights reserved.
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

#define ATH11K_PCI_IRQ_CE0_OFFSET	3
#define ATH11K_PCI_IRQ_DP_OFFSET	14

#define ATH11K_PCI_WINDOW_ENABLE_BIT		0x40000000
#define ATH11K_PCI_WINDOW_REG_ADDRESS		0x310c
#define ATH11K_PCI_WINDOW_VALUE_MASK		GENMASK(24, 19)
#define ATH11K_PCI_WINDOW_START			0x80000
#define ATH11K_PCI_WINDOW_RANGE_MASK		GENMASK(18, 0)

/* BAR0 + 4k is always accessible, and no
 * need to force wakeup.
 * 4K - 32 = 0xFE0
 */
#define ATH11K_PCI_ACCESS_ALWAYS_OFF 0xFE0

struct ath11k_msi_user {
	char *name;
	int num_vectors;
	u32 base_vector;
};

struct ath11k_msi_config {
	int total_vectors;
	int total_users;
	struct ath11k_msi_user *users;
};

enum ath11k_pci_flags {
	ATH11K_PCI_FLAG_INIT_DONE,
	ATH11K_PCI_FLAG_IS_MSI_64,
	ATH11K_PCI_ASPM_RESTORE,
	ATH11K_PCI_FLAG_MULTI_MSI_VECTORS,
};

struct ath11k_pci {
	struct pci_dev *pdev;
	struct ath11k_base *ab;
	u16 dev_id;
	char amss_path[100];
	u32 msi_ep_base_data;
	struct mhi_controller *mhi_ctrl;
	const struct ath11k_msi_config *msi_config;
	unsigned long mhi_state;
	u32 register_window;

	/* protects register_window above */
	spinlock_t window_lock;

	/* enum ath11k_pci_flags */
	unsigned long flags;
	u16 link_ctl;

	unsigned long irq_flags;
};

static inline struct ath11k_pci *ath11k_pci_priv(struct ath11k_base *ab)
{
	return (struct ath11k_pci *)ab->drv_priv;
}

int ath11k_pci_get_user_msi_assignment(struct ath11k_pci *ar_pci, char *user_name,
				       int *num_vectors, u32 *user_base_data,
				       u32 *base_vector);
int ath11k_pci_get_msi_irq(struct device *dev, unsigned int vector);
void ath11k_pci_write32(struct ath11k_base *ab, u32 offset, u32 value);
u32 ath11k_pci_read32(struct ath11k_base *ab, u32 offset);
void ath11k_pci_get_msi_address(struct ath11k_base *ab, u32 *msi_addr_lo,
				u32 *msi_addr_hi);
void ath11k_pci_get_ce_msi_idx(struct ath11k_base *ab, u32 ce_id, u32 *msi_idx);
void ath11k_pci_free_irq(struct ath11k_base *ab);
int ath11k_pci_config_irq(struct ath11k_base *ab);
void ath11k_pci_ext_irq_enable(struct ath11k_base *ab);
void ath11k_pci_ext_irq_disable(struct ath11k_base *ab);
void ath11k_pci_stop(struct ath11k_base *ab);
int ath11k_pci_start(struct ath11k_base *ab);
int ath11k_pci_map_service_to_pipe(struct ath11k_base *ab, u16 service_id,
				   u8 *ul_pipe, u8 *dl_pipe);
void ath11k_pci_ce_irqs_enable(struct ath11k_base *ab);
void ath11k_pci_ce_irq_disable_sync(struct ath11k_base *ab);
int ath11k_get_user_msi_assignment(struct ath11k_base *ab, char *user_name,
				   int *num_vectors, u32 *user_base_data,
				   u32 *base_vector);
void ath11k_pci_aspm_restore(struct ath11k_pci *ab_pci);
int ath11k_pci_set_irq_affinity_hint(struct ath11k_pci *ab_pci,
				     const struct cpumask *m);

#endif
