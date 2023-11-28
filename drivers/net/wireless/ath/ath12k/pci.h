/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2019-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef ATH12K_PCI_H
#define ATH12K_PCI_H

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

#define GCC_GCC_PCIE_HOT_RST			0x1e38338
#define GCC_GCC_PCIE_HOT_RST_VAL		0x10

#define PCIE_PCIE_INT_ALL_CLEAR			0x1e08228
#define PCIE_SMLH_REQ_RST_LINK_DOWN		0x2
#define PCIE_INT_CLEAR_ALL			0xffffffff

#define PCIE_QSERDES_COM_SYSCLK_EN_SEL_REG(ab) \
	((ab)->hw_params->regs->pcie_qserdes_sysclk_en_sel)
#define PCIE_QSERDES_COM_SYSCLK_EN_SEL_VAL	0x10
#define PCIE_QSERDES_COM_SYSCLK_EN_SEL_MSK	0xffffffff
#define PCIE_PCS_OSC_DTCT_CONFIG1_REG(ab) \
	((ab)->hw_params->regs->pcie_pcs_osc_dtct_config_base)
#define PCIE_PCS_OSC_DTCT_CONFIG1_VAL		0x02
#define PCIE_PCS_OSC_DTCT_CONFIG2_REG(ab) \
	((ab)->hw_params->regs->pcie_pcs_osc_dtct_config_base + 0x4)
#define PCIE_PCS_OSC_DTCT_CONFIG2_VAL		0x52
#define PCIE_PCS_OSC_DTCT_CONFIG4_REG(ab) \
	((ab)->hw_params->regs->pcie_pcs_osc_dtct_config_base + 0xc)
#define PCIE_PCS_OSC_DTCT_CONFIG4_VAL		0xff
#define PCIE_PCS_OSC_DTCT_CONFIG_MSK		0x000000ff

#define WLAON_QFPROM_PWR_CTRL_REG		0x01f8031c
#define QFPROM_PWR_CTRL_VDD4BLOW_MASK		0x4

#define PCI_BAR_WINDOW0_BASE	0x1E00000
#define PCI_BAR_WINDOW0_END	0x1E7FFFC
#define PCI_SOC_RANGE_MASK	0x3FFF
#define PCI_SOC_PCI_REG_BASE	0x1E04000
#define PCI_SOC_PCI_REG_END	0x1E07FFC
#define PCI_PARF_BASE		0x1E08000
#define PCI_PARF_END		0x1E0BFFC
#define PCI_MHIREGLEN_REG	0x1E0E100
#define PCI_MHI_REGION_END	0x1E0EFFC
#define QRTR_PCI_DOMAIN_NR_MASK		GENMASK(7, 4)
#define QRTR_PCI_BUS_NUMBER_MASK	GENMASK(3, 0)

#define ATH12K_PCI_SOC_HW_VERSION_1	1
#define ATH12K_PCI_SOC_HW_VERSION_2	2

struct ath12k_msi_user {
	const char *name;
	int num_vectors;
	u32 base_vector;
};

struct ath12k_msi_config {
	int total_vectors;
	int total_users;
	const struct ath12k_msi_user *users;
};

enum ath12k_pci_flags {
	ATH12K_PCI_FLAG_INIT_DONE,
	ATH12K_PCI_FLAG_IS_MSI_64,
	ATH12K_PCI_ASPM_RESTORE,
};

struct ath12k_pci_ops {
	int (*wakeup)(struct ath12k_base *ab);
	void (*release)(struct ath12k_base *ab);
};

struct ath12k_pci {
	struct pci_dev *pdev;
	struct ath12k_base *ab;
	u16 dev_id;
	char amss_path[100];
	u32 msi_ep_base_data;
	struct mhi_controller *mhi_ctrl;
	const struct ath12k_msi_config *msi_config;
	unsigned long mhi_state;
	u32 register_window;

	/* protects register_window above */
	spinlock_t window_lock;

	/* enum ath12k_pci_flags */
	unsigned long flags;
	u16 link_ctl;
	const struct ath12k_pci_ops *pci_ops;
};

static inline struct ath12k_pci *ath12k_pci_priv(struct ath12k_base *ab)
{
	return (struct ath12k_pci *)ab->drv_priv;
}

int ath12k_pci_get_user_msi_assignment(struct ath12k_base *ab, char *user_name,
				       int *num_vectors, u32 *user_base_data,
				       u32 *base_vector);
int ath12k_pci_get_msi_irq(struct device *dev, unsigned int vector);
void ath12k_pci_write32(struct ath12k_base *ab, u32 offset, u32 value);
u32 ath12k_pci_read32(struct ath12k_base *ab, u32 offset);
int ath12k_pci_map_service_to_pipe(struct ath12k_base *ab, u16 service_id,
				   u8 *ul_pipe, u8 *dl_pipe);
void ath12k_pci_get_msi_address(struct ath12k_base *ab, u32 *msi_addr_lo,
				u32 *msi_addr_hi);
void ath12k_pci_get_ce_msi_idx(struct ath12k_base *ab, u32 ce_id,
			       u32 *msi_idx);
void ath12k_pci_hif_ce_irq_enable(struct ath12k_base *ab);
void ath12k_pci_hif_ce_irq_disable(struct ath12k_base *ab);
void ath12k_pci_ext_irq_enable(struct ath12k_base *ab);
void ath12k_pci_ext_irq_disable(struct ath12k_base *ab);
int ath12k_pci_hif_suspend(struct ath12k_base *ab);
int ath12k_pci_hif_resume(struct ath12k_base *ab);
void ath12k_pci_stop(struct ath12k_base *ab);
int ath12k_pci_start(struct ath12k_base *ab);
int ath12k_pci_power_up(struct ath12k_base *ab);
void ath12k_pci_power_down(struct ath12k_base *ab);
#endif /* ATH12K_PCI_H */
