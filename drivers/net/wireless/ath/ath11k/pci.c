// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2019-2020 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/msi.h>
#include <linux/pci.h>
#include <linux/of.h>
#include <linux/time.h>
#include <linux/vmalloc.h>

#include "pci.h"
#include "core.h"
#include "hif.h"
#include "mhi.h"
#include "debug.h"
#include "pcic.h"
#include "qmi.h"

#define ATH11K_PCI_BAR_NUM		0
#define ATH11K_PCI_DMA_MASK		36
#define ATH11K_PCI_COHERENT_DMA_MASK	32

#define TCSR_SOC_HW_VERSION		0x0224
#define TCSR_SOC_HW_VERSION_MAJOR_MASK	GENMASK(11, 8)
#define TCSR_SOC_HW_VERSION_MINOR_MASK	GENMASK(7, 0)

#define QCA6390_DEVICE_ID		0x1101
#define QCN9074_DEVICE_ID		0x1104
#define WCN6855_DEVICE_ID		0x1103

#define TCSR_SOC_HW_SUB_VER	0x1910010

static const struct pci_device_id ath11k_pci_id_table[] = {
	{ PCI_VDEVICE(QCOM, QCA6390_DEVICE_ID) },
	{ PCI_VDEVICE(QCOM, WCN6855_DEVICE_ID) },
	{ PCI_VDEVICE(QCOM, QCN9074_DEVICE_ID) },
	{0}
};

MODULE_DEVICE_TABLE(pci, ath11k_pci_id_table);

static int ath11k_pci_bus_wake_up(struct ath11k_base *ab)
{
	struct ath11k_pci *ab_pci = ath11k_pci_priv(ab);

	return mhi_device_get_sync(ab_pci->mhi_ctrl->mhi_dev);
}

static void ath11k_pci_bus_release(struct ath11k_base *ab)
{
	struct ath11k_pci *ab_pci = ath11k_pci_priv(ab);

	mhi_device_put(ab_pci->mhi_ctrl->mhi_dev);
}

static u32 ath11k_pci_get_window_start(struct ath11k_base *ab, u32 offset)
{
	if (!ab->hw_params.static_window_map)
		return ATH11K_PCI_WINDOW_START;

	if ((offset ^ HAL_SEQ_WCSS_UMAC_OFFSET) < ATH11K_PCI_WINDOW_RANGE_MASK)
		/* if offset lies within DP register range, use 3rd window */
		return 3 * ATH11K_PCI_WINDOW_START;
	else if ((offset ^ HAL_SEQ_WCSS_UMAC_CE0_SRC_REG(ab)) <
		 ATH11K_PCI_WINDOW_RANGE_MASK)
		 /* if offset lies within CE register range, use 2nd window */
		return 2 * ATH11K_PCI_WINDOW_START;
	else
		return ATH11K_PCI_WINDOW_START;
}

static inline void ath11k_pci_select_window(struct ath11k_pci *ab_pci, u32 offset)
{
	struct ath11k_base *ab = ab_pci->ab;

	u32 window = FIELD_GET(ATH11K_PCI_WINDOW_VALUE_MASK, offset);

	lockdep_assert_held(&ab_pci->window_lock);

	if (window != ab_pci->register_window) {
		iowrite32(ATH11K_PCI_WINDOW_ENABLE_BIT | window,
			  ab->mem + ATH11K_PCI_WINDOW_REG_ADDRESS);
		ioread32(ab->mem + ATH11K_PCI_WINDOW_REG_ADDRESS);
		ab_pci->register_window = window;
	}
}

static void
ath11k_pci_window_write32(struct ath11k_base *ab, u32 offset, u32 value)
{
	struct ath11k_pci *ab_pci = ath11k_pci_priv(ab);
	u32 window_start;

	window_start = ath11k_pci_get_window_start(ab, offset);

	if (window_start == ATH11K_PCI_WINDOW_START) {
		spin_lock_bh(&ab_pci->window_lock);
		ath11k_pci_select_window(ab_pci, offset);
		iowrite32(value, ab->mem + window_start +
			  (offset & ATH11K_PCI_WINDOW_RANGE_MASK));
		spin_unlock_bh(&ab_pci->window_lock);
	} else {
		iowrite32(value, ab->mem + window_start +
			  (offset & ATH11K_PCI_WINDOW_RANGE_MASK));
	}
}

static u32 ath11k_pci_window_read32(struct ath11k_base *ab, u32 offset)
{
	struct ath11k_pci *ab_pci = ath11k_pci_priv(ab);
	u32 window_start, val;

	window_start = ath11k_pci_get_window_start(ab, offset);

	if (window_start == ATH11K_PCI_WINDOW_START) {
		spin_lock_bh(&ab_pci->window_lock);
		ath11k_pci_select_window(ab_pci, offset);
		val = ioread32(ab->mem + window_start +
			       (offset & ATH11K_PCI_WINDOW_RANGE_MASK));
		spin_unlock_bh(&ab_pci->window_lock);
	} else {
		val = ioread32(ab->mem + window_start +
			       (offset & ATH11K_PCI_WINDOW_RANGE_MASK));
	}

	return val;
}

int ath11k_pci_get_msi_irq(struct ath11k_base *ab, unsigned int vector)
{
	struct pci_dev *pci_dev = to_pci_dev(ab->dev);

	return pci_irq_vector(pci_dev, vector);
}

static const struct ath11k_pci_ops ath11k_pci_ops_qca6390 = {
	.wakeup = ath11k_pci_bus_wake_up,
	.release = ath11k_pci_bus_release,
	.get_msi_irq = ath11k_pci_get_msi_irq,
	.window_write32 = ath11k_pci_window_write32,
	.window_read32 = ath11k_pci_window_read32,
};

static const struct ath11k_pci_ops ath11k_pci_ops_qcn9074 = {
	.wakeup = NULL,
	.release = NULL,
	.get_msi_irq = ath11k_pci_get_msi_irq,
	.window_write32 = ath11k_pci_window_write32,
	.window_read32 = ath11k_pci_window_read32,
};

static const struct ath11k_msi_config msi_config_one_msi = {
	.total_vectors = 1,
	.total_users = 4,
	.users = (struct ath11k_msi_user[]) {
		{ .name = "MHI", .num_vectors = 3, .base_vector = 0 },
		{ .name = "CE", .num_vectors = 1, .base_vector = 0 },
		{ .name = "WAKE", .num_vectors = 1, .base_vector = 0 },
		{ .name = "DP", .num_vectors = 1, .base_vector = 0 },
	},
};

static inline void ath11k_pci_select_static_window(struct ath11k_pci *ab_pci)
{
	u32 umac_window;
	u32 ce_window;
	u32 window;

	umac_window = FIELD_GET(ATH11K_PCI_WINDOW_VALUE_MASK, HAL_SEQ_WCSS_UMAC_OFFSET);
	ce_window = FIELD_GET(ATH11K_PCI_WINDOW_VALUE_MASK, HAL_CE_WFSS_CE_REG_BASE);
	window = (umac_window << 12) | (ce_window << 6);

	iowrite32(ATH11K_PCI_WINDOW_ENABLE_BIT | window,
		  ab_pci->ab->mem + ATH11K_PCI_WINDOW_REG_ADDRESS);
}

static void ath11k_pci_soc_global_reset(struct ath11k_base *ab)
{
	u32 val, delay;

	val = ath11k_pcic_read32(ab, PCIE_SOC_GLOBAL_RESET);

	val |= PCIE_SOC_GLOBAL_RESET_V;

	ath11k_pcic_write32(ab, PCIE_SOC_GLOBAL_RESET, val);

	/* TODO: exact time to sleep is uncertain */
	delay = 10;
	mdelay(delay);

	/* Need to toggle V bit back otherwise stuck in reset status */
	val &= ~PCIE_SOC_GLOBAL_RESET_V;

	ath11k_pcic_write32(ab, PCIE_SOC_GLOBAL_RESET, val);

	mdelay(delay);

	val = ath11k_pcic_read32(ab, PCIE_SOC_GLOBAL_RESET);
	if (val == 0xffffffff)
		ath11k_warn(ab, "link down error during global reset\n");
}

static void ath11k_pci_clear_dbg_registers(struct ath11k_base *ab)
{
	u32 val;

	/* read cookie */
	val = ath11k_pcic_read32(ab, PCIE_Q6_COOKIE_ADDR);
	ath11k_dbg(ab, ATH11K_DBG_PCI, "pcie_q6_cookie_addr 0x%x\n", val);

	val = ath11k_pcic_read32(ab, WLAON_WARM_SW_ENTRY);
	ath11k_dbg(ab, ATH11K_DBG_PCI, "wlaon_warm_sw_entry 0x%x\n", val);

	/* TODO: exact time to sleep is uncertain */
	mdelay(10);

	/* write 0 to WLAON_WARM_SW_ENTRY to prevent Q6 from
	 * continuing warm path and entering dead loop.
	 */
	ath11k_pcic_write32(ab, WLAON_WARM_SW_ENTRY, 0);
	mdelay(10);

	val = ath11k_pcic_read32(ab, WLAON_WARM_SW_ENTRY);
	ath11k_dbg(ab, ATH11K_DBG_PCI, "wlaon_warm_sw_entry 0x%x\n", val);

	/* A read clear register. clear the register to prevent
	 * Q6 from entering wrong code path.
	 */
	val = ath11k_pcic_read32(ab, WLAON_SOC_RESET_CAUSE_REG);
	ath11k_dbg(ab, ATH11K_DBG_PCI, "soc reset cause %d\n", val);
}

static int ath11k_pci_set_link_reg(struct ath11k_base *ab,
				   u32 offset, u32 value, u32 mask)
{
	u32 v;
	int i;

	v = ath11k_pcic_read32(ab, offset);
	if ((v & mask) == value)
		return 0;

	for (i = 0; i < 10; i++) {
		ath11k_pcic_write32(ab, offset, (v & ~mask) | value);

		v = ath11k_pcic_read32(ab, offset);
		if ((v & mask) == value)
			return 0;

		mdelay(2);
	}

	ath11k_warn(ab, "failed to set pcie link register 0x%08x: 0x%08x != 0x%08x\n",
		    offset, v & mask, value);

	return -ETIMEDOUT;
}

static int ath11k_pci_fix_l1ss(struct ath11k_base *ab)
{
	int ret;

	ret = ath11k_pci_set_link_reg(ab,
				      PCIE_QSERDES_COM_SYSCLK_EN_SEL_REG(ab),
				      PCIE_QSERDES_COM_SYSCLK_EN_SEL_VAL,
				      PCIE_QSERDES_COM_SYSCLK_EN_SEL_MSK);
	if (ret) {
		ath11k_warn(ab, "failed to set sysclk: %d\n", ret);
		return ret;
	}

	ret = ath11k_pci_set_link_reg(ab,
				      PCIE_PCS_OSC_DTCT_CONFIG1_REG(ab),
				      PCIE_PCS_OSC_DTCT_CONFIG1_VAL,
				      PCIE_PCS_OSC_DTCT_CONFIG_MSK);
	if (ret) {
		ath11k_warn(ab, "failed to set dtct config1 error: %d\n", ret);
		return ret;
	}

	ret = ath11k_pci_set_link_reg(ab,
				      PCIE_PCS_OSC_DTCT_CONFIG2_REG(ab),
				      PCIE_PCS_OSC_DTCT_CONFIG2_VAL,
				      PCIE_PCS_OSC_DTCT_CONFIG_MSK);
	if (ret) {
		ath11k_warn(ab, "failed to set dtct config2: %d\n", ret);
		return ret;
	}

	ret = ath11k_pci_set_link_reg(ab,
				      PCIE_PCS_OSC_DTCT_CONFIG4_REG(ab),
				      PCIE_PCS_OSC_DTCT_CONFIG4_VAL,
				      PCIE_PCS_OSC_DTCT_CONFIG_MSK);
	if (ret) {
		ath11k_warn(ab, "failed to set dtct config4: %d\n", ret);
		return ret;
	}

	return 0;
}

static void ath11k_pci_enable_ltssm(struct ath11k_base *ab)
{
	u32 val;
	int i;

	val = ath11k_pcic_read32(ab, PCIE_PCIE_PARF_LTSSM);

	/* PCIE link seems very unstable after the Hot Reset*/
	for (i = 0; val != PARM_LTSSM_VALUE && i < 5; i++) {
		if (val == 0xffffffff)
			mdelay(5);

		ath11k_pcic_write32(ab, PCIE_PCIE_PARF_LTSSM, PARM_LTSSM_VALUE);
		val = ath11k_pcic_read32(ab, PCIE_PCIE_PARF_LTSSM);
	}

	ath11k_dbg(ab, ATH11K_DBG_PCI, "ltssm 0x%x\n", val);

	val = ath11k_pcic_read32(ab, GCC_GCC_PCIE_HOT_RST);
	val |= GCC_GCC_PCIE_HOT_RST_VAL;
	ath11k_pcic_write32(ab, GCC_GCC_PCIE_HOT_RST, val);
	val = ath11k_pcic_read32(ab, GCC_GCC_PCIE_HOT_RST);

	ath11k_dbg(ab, ATH11K_DBG_PCI, "pcie_hot_rst 0x%x\n", val);

	mdelay(5);
}

static void ath11k_pci_clear_all_intrs(struct ath11k_base *ab)
{
	/* This is a WAR for PCIE Hotreset.
	 * When target receive Hotreset, but will set the interrupt.
	 * So when download SBL again, SBL will open Interrupt and
	 * receive it, and crash immediately.
	 */
	ath11k_pcic_write32(ab, PCIE_PCIE_INT_ALL_CLEAR, PCIE_INT_CLEAR_ALL);
}

static void ath11k_pci_set_wlaon_pwr_ctrl(struct ath11k_base *ab)
{
	u32 val;

	val = ath11k_pcic_read32(ab, WLAON_QFPROM_PWR_CTRL_REG);
	val &= ~QFPROM_PWR_CTRL_VDD4BLOW_MASK;
	ath11k_pcic_write32(ab, WLAON_QFPROM_PWR_CTRL_REG, val);
}

static void ath11k_pci_force_wake(struct ath11k_base *ab)
{
	ath11k_pcic_write32(ab, PCIE_SOC_WAKE_PCIE_LOCAL_REG, 1);
	mdelay(5);
}

static void ath11k_pci_sw_reset(struct ath11k_base *ab, bool power_on)
{
	mdelay(100);

	if (power_on) {
		ath11k_pci_enable_ltssm(ab);
		ath11k_pci_clear_all_intrs(ab);
		ath11k_pci_set_wlaon_pwr_ctrl(ab);
		if (ab->hw_params.fix_l1ss)
			ath11k_pci_fix_l1ss(ab);
	}

	ath11k_mhi_clear_vector(ab);
	ath11k_pci_clear_dbg_registers(ab);
	ath11k_pci_soc_global_reset(ab);
	ath11k_mhi_set_mhictrl_reset(ab);
}

static void ath11k_pci_init_qmi_ce_config(struct ath11k_base *ab)
{
	struct ath11k_qmi_ce_cfg *cfg = &ab->qmi.ce_cfg;

	cfg->tgt_ce = ab->hw_params.target_ce_config;
	cfg->tgt_ce_len = ab->hw_params.target_ce_count;

	cfg->svc_to_ce_map = ab->hw_params.svc_to_ce_map;
	cfg->svc_to_ce_map_len = ab->hw_params.svc_to_ce_map_len;
	ab->qmi.service_ins_id = ab->hw_params.qmi_service_ins_id;

	ath11k_ce_get_shadow_config(ab, &cfg->shadow_reg_v2,
				    &cfg->shadow_reg_v2_len);
}

static void ath11k_pci_msi_config(struct ath11k_pci *ab_pci, bool enable)
{
	struct pci_dev *dev = ab_pci->pdev;
	u16 control;

	pci_read_config_word(dev, dev->msi_cap + PCI_MSI_FLAGS, &control);

	if (enable)
		control |= PCI_MSI_FLAGS_ENABLE;
	else
		control &= ~PCI_MSI_FLAGS_ENABLE;

	pci_write_config_word(dev, dev->msi_cap + PCI_MSI_FLAGS, control);
}

static void ath11k_pci_msi_enable(struct ath11k_pci *ab_pci)
{
	ath11k_pci_msi_config(ab_pci, true);
}

static void ath11k_pci_msi_disable(struct ath11k_pci *ab_pci)
{
	ath11k_pci_msi_config(ab_pci, false);
}

static int ath11k_pci_alloc_msi(struct ath11k_pci *ab_pci)
{
	struct ath11k_base *ab = ab_pci->ab;
	const struct ath11k_msi_config *msi_config = ab->pci.msi.config;
	struct pci_dev *pci_dev = ab_pci->pdev;
	struct msi_desc *msi_desc;
	int num_vectors;
	int ret;

	num_vectors = pci_alloc_irq_vectors(pci_dev,
					    msi_config->total_vectors,
					    msi_config->total_vectors,
					    PCI_IRQ_MSI);
	if (num_vectors == msi_config->total_vectors) {
		set_bit(ATH11K_FLAG_MULTI_MSI_VECTORS, &ab->dev_flags);
	} else {
		num_vectors = pci_alloc_irq_vectors(ab_pci->pdev,
						    1,
						    1,
						    PCI_IRQ_MSI);
		if (num_vectors < 0) {
			ret = -EINVAL;
			goto reset_msi_config;
		}
		clear_bit(ATH11K_FLAG_MULTI_MSI_VECTORS, &ab->dev_flags);
		ab->pci.msi.config = &msi_config_one_msi;
		ath11k_dbg(ab, ATH11K_DBG_PCI, "request one msi vector\n");
	}
	ath11k_info(ab, "MSI vectors: %d\n", num_vectors);

	ath11k_pci_msi_disable(ab_pci);

	msi_desc = irq_get_msi_desc(ab_pci->pdev->irq);
	if (!msi_desc) {
		ath11k_err(ab, "msi_desc is NULL!\n");
		ret = -EINVAL;
		goto free_msi_vector;
	}

	ab->pci.msi.ep_base_data = msi_desc->msg.data;

	pci_read_config_dword(pci_dev, pci_dev->msi_cap + PCI_MSI_ADDRESS_LO,
			      &ab->pci.msi.addr_lo);

	if (msi_desc->pci.msi_attrib.is_64) {
		pci_read_config_dword(pci_dev, pci_dev->msi_cap + PCI_MSI_ADDRESS_HI,
				      &ab->pci.msi.addr_hi);
	} else {
		ab->pci.msi.addr_hi = 0;
	}

	ath11k_dbg(ab, ATH11K_DBG_PCI, "msi base data is %d\n", ab->pci.msi.ep_base_data);

	return 0;

free_msi_vector:
	pci_free_irq_vectors(ab_pci->pdev);

reset_msi_config:
	return ret;
}

static void ath11k_pci_free_msi(struct ath11k_pci *ab_pci)
{
	pci_free_irq_vectors(ab_pci->pdev);
}

static int ath11k_pci_config_msi_data(struct ath11k_pci *ab_pci)
{
	struct msi_desc *msi_desc;

	msi_desc = irq_get_msi_desc(ab_pci->pdev->irq);
	if (!msi_desc) {
		ath11k_err(ab_pci->ab, "msi_desc is NULL!\n");
		pci_free_irq_vectors(ab_pci->pdev);
		return -EINVAL;
	}

	ab_pci->ab->pci.msi.ep_base_data = msi_desc->msg.data;

	ath11k_dbg(ab_pci->ab, ATH11K_DBG_PCI, "after request_irq msi_ep_base_data %d\n",
		   ab_pci->ab->pci.msi.ep_base_data);

	return 0;
}

static int ath11k_pci_claim(struct ath11k_pci *ab_pci, struct pci_dev *pdev)
{
	struct ath11k_base *ab = ab_pci->ab;
	u16 device_id;
	int ret = 0;

	pci_read_config_word(pdev, PCI_DEVICE_ID, &device_id);
	if (device_id != ab_pci->dev_id)  {
		ath11k_err(ab, "pci device id mismatch: 0x%x 0x%x\n",
			   device_id, ab_pci->dev_id);
		ret = -EIO;
		goto out;
	}

	ret = pci_assign_resource(pdev, ATH11K_PCI_BAR_NUM);
	if (ret) {
		ath11k_err(ab, "failed to assign pci resource: %d\n", ret);
		goto out;
	}

	ret = pci_enable_device(pdev);
	if (ret) {
		ath11k_err(ab, "failed to enable pci device: %d\n", ret);
		goto out;
	}

	ret = pci_request_region(pdev, ATH11K_PCI_BAR_NUM, "ath11k_pci");
	if (ret) {
		ath11k_err(ab, "failed to request pci region: %d\n", ret);
		goto disable_device;
	}

	ret = dma_set_mask(&pdev->dev,
			   DMA_BIT_MASK(ATH11K_PCI_DMA_MASK));
	if (ret) {
		ath11k_err(ab, "failed to set pci dma mask to %d: %d\n",
			   ATH11K_PCI_DMA_MASK, ret);
		goto release_region;
	}

	ab_pci->dma_mask = DMA_BIT_MASK(ATH11K_PCI_DMA_MASK);

	ret = dma_set_coherent_mask(&pdev->dev,
				    DMA_BIT_MASK(ATH11K_PCI_COHERENT_DMA_MASK));
	if (ret) {
		ath11k_err(ab, "failed to set pci coherent dma mask to %d: %d\n",
			   ATH11K_PCI_COHERENT_DMA_MASK, ret);
		goto release_region;
	}

	pci_set_master(pdev);

	ab->mem_len = pci_resource_len(pdev, ATH11K_PCI_BAR_NUM);
	ab->mem = pci_iomap(pdev, ATH11K_PCI_BAR_NUM, 0);
	if (!ab->mem) {
		ath11k_err(ab, "failed to map pci bar %d\n", ATH11K_PCI_BAR_NUM);
		ret = -EIO;
		goto release_region;
	}

	ab->mem_ce = ab->mem;

	ath11k_dbg(ab, ATH11K_DBG_BOOT, "pci_mem 0x%p\n", ab->mem);
	return 0;

release_region:
	pci_release_region(pdev, ATH11K_PCI_BAR_NUM);
disable_device:
	pci_disable_device(pdev);
out:
	return ret;
}

static void ath11k_pci_free_region(struct ath11k_pci *ab_pci)
{
	struct ath11k_base *ab = ab_pci->ab;
	struct pci_dev *pci_dev = ab_pci->pdev;

	pci_iounmap(pci_dev, ab->mem);
	ab->mem = NULL;
	pci_release_region(pci_dev, ATH11K_PCI_BAR_NUM);
	if (pci_is_enabled(pci_dev))
		pci_disable_device(pci_dev);
}

static void ath11k_pci_aspm_disable(struct ath11k_pci *ab_pci)
{
	struct ath11k_base *ab = ab_pci->ab;

	pcie_capability_read_word(ab_pci->pdev, PCI_EXP_LNKCTL,
				  &ab_pci->link_ctl);

	ath11k_dbg(ab, ATH11K_DBG_PCI, "link_ctl 0x%04x L0s %d L1 %d\n",
		   ab_pci->link_ctl,
		   u16_get_bits(ab_pci->link_ctl, PCI_EXP_LNKCTL_ASPM_L0S),
		   u16_get_bits(ab_pci->link_ctl, PCI_EXP_LNKCTL_ASPM_L1));

	/* disable L0s and L1 */
	pcie_capability_clear_word(ab_pci->pdev, PCI_EXP_LNKCTL,
				   PCI_EXP_LNKCTL_ASPMC);

	set_bit(ATH11K_PCI_ASPM_RESTORE, &ab_pci->flags);
}

static void ath11k_pci_aspm_restore(struct ath11k_pci *ab_pci)
{
	if (test_and_clear_bit(ATH11K_PCI_ASPM_RESTORE, &ab_pci->flags))
		pcie_capability_clear_and_set_word(ab_pci->pdev, PCI_EXP_LNKCTL,
						   PCI_EXP_LNKCTL_ASPMC,
						   ab_pci->link_ctl &
						   PCI_EXP_LNKCTL_ASPMC);
}

#ifdef CONFIG_DEV_COREDUMP
static int ath11k_pci_coredump_calculate_size(struct ath11k_base *ab, u32 *dump_seg_sz)
{
	struct ath11k_pci *ab_pci = ath11k_pci_priv(ab);
	struct mhi_controller *mhi_ctrl = ab_pci->mhi_ctrl;
	struct image_info *rddm_img, *fw_img;
	struct ath11k_tlv_dump_data *dump_tlv;
	enum ath11k_fw_crash_dump_type mem_type;
	u32 len = 0, rddm_tlv_sz = 0, paging_tlv_sz = 0;
	struct ath11k_dump_file_data *file_data;
	int i;

	rddm_img = mhi_ctrl->rddm_image;
	if (!rddm_img) {
		ath11k_err(ab, "No RDDM dump found\n");
		return 0;
	}

	fw_img = mhi_ctrl->fbc_image;

	for (i = 0; i < fw_img->entries ; i++) {
		if (!fw_img->mhi_buf[i].buf)
			continue;

		paging_tlv_sz += fw_img->mhi_buf[i].len;
	}
	dump_seg_sz[FW_CRASH_DUMP_PAGING_DATA] = paging_tlv_sz;

	for (i = 0; i < rddm_img->entries; i++) {
		if (!rddm_img->mhi_buf[i].buf)
			continue;

		rddm_tlv_sz += rddm_img->mhi_buf[i].len;
	}
	dump_seg_sz[FW_CRASH_DUMP_RDDM_DATA] = rddm_tlv_sz;

	for (i = 0; i < ab->qmi.mem_seg_count; i++) {
		mem_type = ath11k_coredump_get_dump_type(ab->qmi.target_mem[i].type);

		if (mem_type == FW_CRASH_DUMP_NONE)
			continue;

		if (mem_type == FW_CRASH_DUMP_TYPE_MAX) {
			ath11k_dbg(ab, ATH11K_DBG_PCI,
				   "target mem region type %d not supported",
				   ab->qmi.target_mem[i].type);
			continue;
		}

		if (!ab->qmi.target_mem[i].anyaddr)
			continue;

		dump_seg_sz[mem_type] += ab->qmi.target_mem[i].size;
	}

	for (i = 0; i < FW_CRASH_DUMP_TYPE_MAX; i++) {
		if (!dump_seg_sz[i])
			continue;

		len += sizeof(*dump_tlv) + dump_seg_sz[i];
	}

	if (len)
		len += sizeof(*file_data);

	return len;
}

static void ath11k_pci_coredump_download(struct ath11k_base *ab)
{
	struct ath11k_pci *ab_pci = ath11k_pci_priv(ab);
	struct mhi_controller *mhi_ctrl = ab_pci->mhi_ctrl;
	struct image_info *rddm_img, *fw_img;
	struct timespec64 timestamp;
	int i, len, mem_idx;
	enum ath11k_fw_crash_dump_type mem_type;
	struct ath11k_dump_file_data *file_data;
	struct ath11k_tlv_dump_data *dump_tlv;
	size_t hdr_len = sizeof(*file_data);
	void *buf;
	u32 dump_seg_sz[FW_CRASH_DUMP_TYPE_MAX] = { 0 };

	ath11k_mhi_coredump(mhi_ctrl, false);

	len = ath11k_pci_coredump_calculate_size(ab, dump_seg_sz);
	if (!len) {
		ath11k_warn(ab, "No crash dump data found for devcoredump");
		return;
	}

	rddm_img = mhi_ctrl->rddm_image;
	fw_img = mhi_ctrl->fbc_image;

	/* dev_coredumpv() requires vmalloc data */
	buf = vzalloc(len);
	if (!buf)
		return;

	ab->dump_data = buf;
	ab->ath11k_coredump_len = len;
	file_data = ab->dump_data;
	strscpy(file_data->df_magic, "ATH11K-FW-DUMP", sizeof(file_data->df_magic));
	file_data->len = cpu_to_le32(len);
	file_data->version = cpu_to_le32(ATH11K_FW_CRASH_DUMP_V2);
	file_data->chip_id = cpu_to_le32(ab_pci->dev_id);
	file_data->qrtr_id = cpu_to_le32(ab_pci->ab->qmi.service_ins_id);
	file_data->bus_id = cpu_to_le32(pci_domain_nr(ab_pci->pdev->bus));
	guid_gen(&file_data->guid);
	ktime_get_real_ts64(&timestamp);
	file_data->tv_sec = cpu_to_le64(timestamp.tv_sec);
	file_data->tv_nsec = cpu_to_le64(timestamp.tv_nsec);
	buf += hdr_len;
	dump_tlv = buf;
	dump_tlv->type = cpu_to_le32(FW_CRASH_DUMP_PAGING_DATA);
	dump_tlv->tlv_len = cpu_to_le32(dump_seg_sz[FW_CRASH_DUMP_PAGING_DATA]);
	buf += COREDUMP_TLV_HDR_SIZE;

	/* append all segments together as they are all part of a single contiguous
	 * block of memory
	 */
	for (i = 0; i < fw_img->entries ; i++) {
		if (!fw_img->mhi_buf[i].buf)
			continue;

		memcpy_fromio(buf, (void const __iomem *)fw_img->mhi_buf[i].buf,
			      fw_img->mhi_buf[i].len);
		buf += fw_img->mhi_buf[i].len;
	}

	dump_tlv = buf;
	dump_tlv->type = cpu_to_le32(FW_CRASH_DUMP_RDDM_DATA);
	dump_tlv->tlv_len = cpu_to_le32(dump_seg_sz[FW_CRASH_DUMP_RDDM_DATA]);
	buf += COREDUMP_TLV_HDR_SIZE;

	/* append all segments together as they are all part of a single contiguous
	 * block of memory
	 */
	for (i = 0; i < rddm_img->entries; i++) {
		if (!rddm_img->mhi_buf[i].buf)
			continue;

		memcpy_fromio(buf, (void const __iomem *)rddm_img->mhi_buf[i].buf,
			      rddm_img->mhi_buf[i].len);
		buf += rddm_img->mhi_buf[i].len;
	}

	mem_idx = FW_CRASH_DUMP_REMOTE_MEM_DATA;
	for (; mem_idx < FW_CRASH_DUMP_TYPE_MAX; mem_idx++) {
		if (mem_idx == FW_CRASH_DUMP_NONE)
			continue;

		for (i = 0; i < ab->qmi.mem_seg_count; i++) {
			mem_type = ath11k_coredump_get_dump_type
						(ab->qmi.target_mem[i].type);

			if (mem_type != mem_idx)
				continue;

			if (!ab->qmi.target_mem[i].anyaddr) {
				ath11k_dbg(ab, ATH11K_DBG_PCI,
					   "Skipping mem region type %d",
					   ab->qmi.target_mem[i].type);
				continue;
			}

			dump_tlv = buf;
			dump_tlv->type = cpu_to_le32(mem_idx);
			dump_tlv->tlv_len = cpu_to_le32(dump_seg_sz[mem_idx]);
			buf += COREDUMP_TLV_HDR_SIZE;

			memcpy_fromio(buf, ab->qmi.target_mem[i].iaddr,
				      ab->qmi.target_mem[i].size);

			buf += ab->qmi.target_mem[i].size;
		}
	}

	queue_work(ab->workqueue, &ab->dump_work);
}
#endif

static int ath11k_pci_power_up(struct ath11k_base *ab)
{
	struct ath11k_pci *ab_pci = ath11k_pci_priv(ab);
	int ret;

	ab_pci->register_window = 0;
	clear_bit(ATH11K_FLAG_DEVICE_INIT_DONE, &ab->dev_flags);
	ath11k_pci_sw_reset(ab_pci->ab, true);

	/* Disable ASPM during firmware download due to problems switching
	 * to AMSS state.
	 */
	ath11k_pci_aspm_disable(ab_pci);

	ath11k_pci_msi_enable(ab_pci);

	ret = ath11k_mhi_start(ab_pci);
	if (ret) {
		ath11k_err(ab, "failed to start mhi: %d\n", ret);
		return ret;
	}

	if (ab->hw_params.static_window_map)
		ath11k_pci_select_static_window(ab_pci);

	return 0;
}

static void ath11k_pci_power_down(struct ath11k_base *ab, bool is_suspend)
{
	struct ath11k_pci *ab_pci = ath11k_pci_priv(ab);

	/* restore aspm in case firmware bootup fails */
	ath11k_pci_aspm_restore(ab_pci);

	ath11k_pci_force_wake(ab_pci->ab);

	ath11k_pci_msi_disable(ab_pci);

	ath11k_mhi_stop(ab_pci, is_suspend);
	clear_bit(ATH11K_FLAG_DEVICE_INIT_DONE, &ab->dev_flags);
	ath11k_pci_sw_reset(ab_pci->ab, false);
}

static int ath11k_pci_hif_suspend(struct ath11k_base *ab)
{
	struct ath11k_pci *ar_pci = ath11k_pci_priv(ab);

	return ath11k_mhi_suspend(ar_pci);
}

static int ath11k_pci_hif_resume(struct ath11k_base *ab)
{
	struct ath11k_pci *ar_pci = ath11k_pci_priv(ab);

	return ath11k_mhi_resume(ar_pci);
}

static void ath11k_pci_hif_ce_irq_enable(struct ath11k_base *ab)
{
	ath11k_pcic_ce_irqs_enable(ab);
}

static void ath11k_pci_hif_ce_irq_disable(struct ath11k_base *ab)
{
	ath11k_pcic_ce_irq_disable_sync(ab);
}

static int ath11k_pci_start(struct ath11k_base *ab)
{
	struct ath11k_pci *ab_pci = ath11k_pci_priv(ab);

	/* TODO: for now don't restore ASPM in case of single MSI
	 * vector as MHI register reading in M2 causes system hang.
	 */
	if (test_bit(ATH11K_FLAG_MULTI_MSI_VECTORS, &ab->dev_flags))
		ath11k_pci_aspm_restore(ab_pci);
	else
		ath11k_info(ab, "leaving PCI ASPM disabled to avoid MHI M2 problems\n");

	ath11k_pcic_start(ab);

	return 0;
}

static const struct ath11k_hif_ops ath11k_pci_hif_ops = {
	.start = ath11k_pci_start,
	.stop = ath11k_pcic_stop,
	.read32 = ath11k_pcic_read32,
	.write32 = ath11k_pcic_write32,
	.read = ath11k_pcic_read,
	.power_down = ath11k_pci_power_down,
	.power_up = ath11k_pci_power_up,
	.suspend = ath11k_pci_hif_suspend,
	.resume = ath11k_pci_hif_resume,
	.irq_enable = ath11k_pcic_ext_irq_enable,
	.irq_disable = ath11k_pcic_ext_irq_disable,
	.get_msi_address =  ath11k_pcic_get_msi_address,
	.get_user_msi_vector = ath11k_pcic_get_user_msi_assignment,
	.map_service_to_pipe = ath11k_pcic_map_service_to_pipe,
	.ce_irq_enable = ath11k_pci_hif_ce_irq_enable,
	.ce_irq_disable = ath11k_pci_hif_ce_irq_disable,
	.get_ce_msi_idx = ath11k_pcic_get_ce_msi_idx,
#ifdef CONFIG_DEV_COREDUMP
	.coredump_download = ath11k_pci_coredump_download,
#endif
};

static void ath11k_pci_read_hw_version(struct ath11k_base *ab, u32 *major, u32 *minor)
{
	u32 soc_hw_version;

	soc_hw_version = ath11k_pcic_read32(ab, TCSR_SOC_HW_VERSION);
	*major = FIELD_GET(TCSR_SOC_HW_VERSION_MAJOR_MASK,
			   soc_hw_version);
	*minor = FIELD_GET(TCSR_SOC_HW_VERSION_MINOR_MASK,
			   soc_hw_version);

	ath11k_dbg(ab, ATH11K_DBG_PCI, "tcsr_soc_hw_version major %d minor %d\n",
		   *major, *minor);
}

static int ath11k_pci_set_irq_affinity_hint(struct ath11k_pci *ab_pci,
					    const struct cpumask *m)
{
	if (test_bit(ATH11K_FLAG_MULTI_MSI_VECTORS, &ab_pci->ab->dev_flags))
		return 0;

	return irq_set_affinity_and_hint(ab_pci->pdev->irq, m);
}

static int ath11k_pci_probe(struct pci_dev *pdev,
			    const struct pci_device_id *pci_dev)
{
	struct ath11k_base *ab;
	struct ath11k_pci *ab_pci;
	u32 soc_hw_version_major, soc_hw_version_minor, addr;
	int ret;
	u32 sub_version;

	ab = ath11k_core_alloc(&pdev->dev, sizeof(*ab_pci), ATH11K_BUS_PCI);

	if (!ab) {
		dev_err(&pdev->dev, "failed to allocate ath11k base\n");
		return -ENOMEM;
	}

	ab->dev = &pdev->dev;
	pci_set_drvdata(pdev, ab);
	ab_pci = ath11k_pci_priv(ab);
	ab_pci->dev_id = pci_dev->device;
	ab_pci->ab = ab;
	ab_pci->pdev = pdev;
	ab->hif.ops = &ath11k_pci_hif_ops;
	ab->fw_mode = ATH11K_FIRMWARE_MODE_NORMAL;
	pci_set_drvdata(pdev, ab);
	spin_lock_init(&ab_pci->window_lock);

	/* Set fixed_mem_region to true for platforms support reserved memory
	 * from DT. If memory is reserved from DT for FW, ath11k driver need not
	 * allocate memory.
	 */
	ret = of_property_read_u32(ab->dev->of_node, "memory-region", &addr);
	if (!ret)
		set_bit(ATH11K_FLAG_FIXED_MEM_RGN, &ab->dev_flags);

	ret = ath11k_pci_claim(ab_pci, pdev);
	if (ret) {
		ath11k_err(ab, "failed to claim device: %d\n", ret);
		goto err_free_core;
	}

	ath11k_dbg(ab, ATH11K_DBG_BOOT, "pci probe %04x:%04x %04x:%04x\n",
		   pdev->vendor, pdev->device,
		   pdev->subsystem_vendor, pdev->subsystem_device);

	ab->id.vendor = pdev->vendor;
	ab->id.device = pdev->device;
	ab->id.subsystem_vendor = pdev->subsystem_vendor;
	ab->id.subsystem_device = pdev->subsystem_device;

	switch (pci_dev->device) {
	case QCA6390_DEVICE_ID:
		ret = ath11k_pcic_register_pci_ops(ab, &ath11k_pci_ops_qca6390);
		if (ret) {
			ath11k_err(ab, "failed to register PCI ops: %d\n", ret);
			goto err_pci_free_region;
		}

		ath11k_pci_read_hw_version(ab, &soc_hw_version_major,
					   &soc_hw_version_minor);
		switch (soc_hw_version_major) {
		case 2:
			ab->hw_rev = ATH11K_HW_QCA6390_HW20;
			break;
		default:
			dev_err(&pdev->dev, "Unsupported QCA6390 SOC hardware version: %d %d\n",
				soc_hw_version_major, soc_hw_version_minor);
			ret = -EOPNOTSUPP;
			goto err_pci_free_region;
		}

		break;
	case QCN9074_DEVICE_ID:
		ret = ath11k_pcic_register_pci_ops(ab, &ath11k_pci_ops_qcn9074);
		if (ret) {
			ath11k_err(ab, "failed to register PCI ops: %d\n", ret);
			goto err_pci_free_region;
		}
		ab->hw_rev = ATH11K_HW_QCN9074_HW10;
		break;
	case WCN6855_DEVICE_ID:
		ret = ath11k_pcic_register_pci_ops(ab, &ath11k_pci_ops_qca6390);
		if (ret) {
			ath11k_err(ab, "failed to register PCI ops: %d\n", ret);
			goto err_pci_free_region;
		}
		ab->id.bdf_search = ATH11K_BDF_SEARCH_BUS_AND_BOARD;
		ath11k_pci_read_hw_version(ab, &soc_hw_version_major,
					   &soc_hw_version_minor);
		switch (soc_hw_version_major) {
		case 2:
			switch (soc_hw_version_minor) {
			case 0x00:
			case 0x01:
				ab->hw_rev = ATH11K_HW_WCN6855_HW20;
				break;
			case 0x10:
			case 0x11:
				sub_version = ath11k_pcic_read32(ab, TCSR_SOC_HW_SUB_VER);
				ath11k_dbg(ab, ATH11K_DBG_PCI, "sub_version 0x%x\n",
					   sub_version);
				switch (sub_version) {
				case 0x1019A0E1:
				case 0x1019B0E1:
				case 0x1019C0E1:
				case 0x1019D0E1:
					ab->hw_rev = ATH11K_HW_QCA2066_HW21;
					break;
				case 0x001e60e1:
					ab->hw_rev = ATH11K_HW_QCA6698AQ_HW21;
					break;
				default:
					ab->hw_rev = ATH11K_HW_WCN6855_HW21;
				}
				break;
			default:
				goto unsupported_wcn6855_soc;
			}
			break;
		default:
unsupported_wcn6855_soc:
			dev_err(&pdev->dev, "Unsupported WCN6855 SOC hardware version: %d %d\n",
				soc_hw_version_major, soc_hw_version_minor);
			ret = -EOPNOTSUPP;
			goto err_pci_free_region;
		}

		break;
	default:
		dev_err(&pdev->dev, "Unknown PCI device found: 0x%x\n",
			pci_dev->device);
		ret = -EOPNOTSUPP;
		goto err_pci_free_region;
	}

	ret = ath11k_pcic_init_msi_config(ab);
	if (ret) {
		ath11k_err(ab, "failed to init msi config: %d\n", ret);
		goto err_pci_free_region;
	}

	ret = ath11k_pci_alloc_msi(ab_pci);
	if (ret) {
		ath11k_err(ab, "failed to enable msi: %d\n", ret);
		goto err_pci_free_region;
	}

	ret = ath11k_core_pre_init(ab);
	if (ret)
		goto err_pci_disable_msi;

	ret = ath11k_pci_set_irq_affinity_hint(ab_pci, cpumask_of(0));
	if (ret) {
		ath11k_err(ab, "failed to set irq affinity %d\n", ret);
		goto err_pci_disable_msi;
	}

	ret = ath11k_mhi_register(ab_pci);
	if (ret) {
		ath11k_err(ab, "failed to register mhi: %d\n", ret);
		goto err_irq_affinity_cleanup;
	}

	ret = ath11k_hal_srng_init(ab);
	if (ret)
		goto err_mhi_unregister;

	ret = ath11k_ce_alloc_pipes(ab);
	if (ret) {
		ath11k_err(ab, "failed to allocate ce pipes: %d\n", ret);
		goto err_hal_srng_deinit;
	}

	ath11k_pci_init_qmi_ce_config(ab);

	ret = ath11k_pcic_config_irq(ab);
	if (ret) {
		ath11k_err(ab, "failed to config irq: %d\n", ret);
		goto err_ce_free;
	}

	/* kernel may allocate a dummy vector before request_irq and
	 * then allocate a real vector when request_irq is called.
	 * So get msi_data here again to avoid spurious interrupt
	 * as msi_data will configured to srngs.
	 */
	ret = ath11k_pci_config_msi_data(ab_pci);
	if (ret) {
		ath11k_err(ab, "failed to config msi_data: %d\n", ret);
		goto err_free_irq;
	}

	ret = ath11k_core_init(ab);
	if (ret) {
		ath11k_err(ab, "failed to init core: %d\n", ret);
		goto err_free_irq;
	}
	ath11k_qmi_fwreset_from_cold_boot(ab);
	return 0;

err_free_irq:
	/* __free_irq() expects the caller to have cleared the affinity hint */
	ath11k_pci_set_irq_affinity_hint(ab_pci, NULL);
	ath11k_pcic_free_irq(ab);

err_ce_free:
	ath11k_ce_free_pipes(ab);

err_hal_srng_deinit:
	ath11k_hal_srng_deinit(ab);

err_mhi_unregister:
	ath11k_mhi_unregister(ab_pci);

err_irq_affinity_cleanup:
	ath11k_pci_set_irq_affinity_hint(ab_pci, NULL);

err_pci_disable_msi:
	ath11k_pci_free_msi(ab_pci);

err_pci_free_region:
	ath11k_pci_free_region(ab_pci);

err_free_core:
	ath11k_core_free(ab);

	return ret;
}

static void ath11k_pci_remove(struct pci_dev *pdev)
{
	struct ath11k_base *ab = pci_get_drvdata(pdev);
	struct ath11k_pci *ab_pci = ath11k_pci_priv(ab);

	ath11k_pci_set_irq_affinity_hint(ab_pci, NULL);

	if (test_bit(ATH11K_FLAG_QMI_FAIL, &ab->dev_flags)) {
		ath11k_pci_power_down(ab, false);
		ath11k_debugfs_soc_destroy(ab);
		ath11k_qmi_deinit_service(ab);
		goto qmi_fail;
	}

	set_bit(ATH11K_FLAG_UNREGISTERING, &ab->dev_flags);

	cancel_work_sync(&ab->reset_work);
	cancel_work_sync(&ab->dump_work);
	ath11k_core_deinit(ab);

qmi_fail:
	ath11k_fw_destroy(ab);
	ath11k_mhi_unregister(ab_pci);

	ath11k_pcic_free_irq(ab);
	ath11k_pci_free_msi(ab_pci);
	ath11k_pci_free_region(ab_pci);

	ath11k_hal_srng_deinit(ab);
	ath11k_ce_free_pipes(ab);
	ath11k_core_free(ab);
}

static void ath11k_pci_shutdown(struct pci_dev *pdev)
{
	struct ath11k_base *ab = pci_get_drvdata(pdev);
	struct ath11k_pci *ab_pci = ath11k_pci_priv(ab);

	ath11k_pci_set_irq_affinity_hint(ab_pci, NULL);
	ath11k_pci_power_down(ab, false);
}

static __maybe_unused int ath11k_pci_pm_suspend(struct device *dev)
{
	struct ath11k_base *ab = dev_get_drvdata(dev);
	int ret;

	if (test_bit(ATH11K_FLAG_QMI_FAIL, &ab->dev_flags)) {
		ath11k_dbg(ab, ATH11K_DBG_BOOT, "boot skipping pci suspend as qmi is not initialised\n");
		return 0;
	}

	ret = ath11k_core_suspend(ab);
	if (ret)
		ath11k_warn(ab, "failed to suspend core: %d\n", ret);

	return 0;
}

static __maybe_unused int ath11k_pci_pm_resume(struct device *dev)
{
	struct ath11k_base *ab = dev_get_drvdata(dev);
	int ret;

	if (test_bit(ATH11K_FLAG_QMI_FAIL, &ab->dev_flags)) {
		ath11k_dbg(ab, ATH11K_DBG_BOOT, "boot skipping pci resume as qmi is not initialised\n");
		return 0;
	}

	ret = ath11k_core_resume(ab);
	if (ret)
		ath11k_warn(ab, "failed to resume core: %d\n", ret);

	return ret;
}

static __maybe_unused int ath11k_pci_pm_suspend_late(struct device *dev)
{
	struct ath11k_base *ab = dev_get_drvdata(dev);
	int ret;

	ret = ath11k_core_suspend_late(ab);
	if (ret)
		ath11k_warn(ab, "failed to late suspend core: %d\n", ret);

	/* Similar to ath11k_pci_pm_suspend(), we return success here
	 * even error happens, to allow system suspend/hibernation survive.
	 */
	return 0;
}

static __maybe_unused int ath11k_pci_pm_resume_early(struct device *dev)
{
	struct ath11k_base *ab = dev_get_drvdata(dev);
	int ret;

	ret = ath11k_core_resume_early(ab);
	if (ret)
		ath11k_warn(ab, "failed to early resume core: %d\n", ret);

	return ret;
}

static const struct dev_pm_ops __maybe_unused ath11k_pci_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(ath11k_pci_pm_suspend,
				ath11k_pci_pm_resume)
	SET_LATE_SYSTEM_SLEEP_PM_OPS(ath11k_pci_pm_suspend_late,
				     ath11k_pci_pm_resume_early)
};

static struct pci_driver ath11k_pci_driver = {
	.name = "ath11k_pci",
	.id_table = ath11k_pci_id_table,
	.probe = ath11k_pci_probe,
	.remove = ath11k_pci_remove,
	.shutdown = ath11k_pci_shutdown,
#ifdef CONFIG_PM
	.driver.pm = &ath11k_pci_pm_ops,
#endif
};

static int ath11k_pci_init(void)
{
	int ret;

	ret = pci_register_driver(&ath11k_pci_driver);
	if (ret)
		pr_err("failed to register ath11k pci driver: %d\n",
		       ret);

	return ret;
}
module_init(ath11k_pci_init);

static void ath11k_pci_exit(void)
{
	pci_unregister_driver(&ath11k_pci_driver);
}

module_exit(ath11k_pci_exit);

MODULE_DESCRIPTION("Driver support for Qualcomm Technologies PCIe 802.11ax WLAN devices");
MODULE_LICENSE("Dual BSD/GPL");

/* firmware files */
MODULE_FIRMWARE(ATH11K_FW_DIR "/QCA6390/hw2.0/*");
MODULE_FIRMWARE(ATH11K_FW_DIR "/QCN9074/hw1.0/*");
MODULE_FIRMWARE(ATH11K_FW_DIR "/WCN6855/hw2.0/*");
MODULE_FIRMWARE(ATH11K_FW_DIR "/WCN6855/hw2.1/*");
