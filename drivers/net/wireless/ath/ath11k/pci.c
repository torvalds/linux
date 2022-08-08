// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2019-2020 The Linux Foundation. All rights reserved.
 */

#include <linux/module.h>
#include <linux/msi.h>
#include <linux/pci.h>

#include "pci.h"
#include "core.h"
#include "hif.h"
#include "mhi.h"
#include "debug.h"

#define ATH11K_PCI_BAR_NUM		0
#define ATH11K_PCI_DMA_MASK		32

#define ATH11K_PCI_IRQ_CE0_OFFSET		3

#define WINDOW_ENABLE_BIT		0x40000000
#define WINDOW_REG_ADDRESS		0x310c
#define WINDOW_VALUE_MASK		GENMASK(24, 19)
#define WINDOW_START			0x80000
#define WINDOW_RANGE_MASK		GENMASK(18, 0)

#define TCSR_SOC_HW_VERSION		0x0224
#define TCSR_SOC_HW_VERSION_MAJOR_MASK	GENMASK(16, 8)
#define TCSR_SOC_HW_VERSION_MINOR_MASK	GENMASK(7, 0)

/* BAR0 + 4k is always accessible, and no
 * need to force wakeup.
 * 4K - 32 = 0xFE0
 */
#define ACCESS_ALWAYS_OFF 0xFE0

#define QCA6390_DEVICE_ID		0x1101

static const struct pci_device_id ath11k_pci_id_table[] = {
	{ PCI_VDEVICE(QCOM, QCA6390_DEVICE_ID) },
	{0}
};

MODULE_DEVICE_TABLE(pci, ath11k_pci_id_table);

static const struct ath11k_bus_params ath11k_pci_bus_params = {
	.mhi_support = true,
	.m3_fw_support = true,
	.fixed_bdf_addr = false,
	.fixed_mem_region = false,
};

static const struct ath11k_msi_config msi_config = {
	.total_vectors = 32,
	.total_users = 4,
	.users = (struct ath11k_msi_user[]) {
		{ .name = "MHI", .num_vectors = 3, .base_vector = 0 },
		{ .name = "CE", .num_vectors = 10, .base_vector = 3 },
		{ .name = "WAKE", .num_vectors = 1, .base_vector = 13 },
		{ .name = "DP", .num_vectors = 18, .base_vector = 14 },
	},
};

static const char *irq_name[ATH11K_IRQ_NUM_MAX] = {
	"bhi",
	"mhi-er0",
	"mhi-er1",
	"ce0",
	"ce1",
	"ce2",
	"ce3",
	"ce4",
	"ce5",
	"ce6",
	"ce7",
	"ce8",
	"ce9",
	"ce10",
	"ce11",
	"host2wbm-desc-feed",
	"host2reo-re-injection",
	"host2reo-command",
	"host2rxdma-monitor-ring3",
	"host2rxdma-monitor-ring2",
	"host2rxdma-monitor-ring1",
	"reo2ost-exception",
	"wbm2host-rx-release",
	"reo2host-status",
	"reo2host-destination-ring4",
	"reo2host-destination-ring3",
	"reo2host-destination-ring2",
	"reo2host-destination-ring1",
	"rxdma2host-monitor-destination-mac3",
	"rxdma2host-monitor-destination-mac2",
	"rxdma2host-monitor-destination-mac1",
	"ppdu-end-interrupts-mac3",
	"ppdu-end-interrupts-mac2",
	"ppdu-end-interrupts-mac1",
	"rxdma2host-monitor-status-ring-mac3",
	"rxdma2host-monitor-status-ring-mac2",
	"rxdma2host-monitor-status-ring-mac1",
	"host2rxdma-host-buf-ring-mac3",
	"host2rxdma-host-buf-ring-mac2",
	"host2rxdma-host-buf-ring-mac1",
	"rxdma2host-destination-ring-mac3",
	"rxdma2host-destination-ring-mac2",
	"rxdma2host-destination-ring-mac1",
	"host2tcl-input-ring4",
	"host2tcl-input-ring3",
	"host2tcl-input-ring2",
	"host2tcl-input-ring1",
	"wbm2host-tx-completions-ring3",
	"wbm2host-tx-completions-ring2",
	"wbm2host-tx-completions-ring1",
	"tcl2host-status-ring",
};

static inline void ath11k_pci_select_window(struct ath11k_pci *ab_pci, u32 offset)
{
	struct ath11k_base *ab = ab_pci->ab;

	u32 window = FIELD_GET(WINDOW_VALUE_MASK, offset);

	lockdep_assert_held(&ab_pci->window_lock);

	if (window != ab_pci->register_window) {
		iowrite32(WINDOW_ENABLE_BIT | window,
			  ab->mem + WINDOW_REG_ADDRESS);
		ioread32(ab->mem + WINDOW_REG_ADDRESS);
		ab_pci->register_window = window;
	}
}

void ath11k_pci_write32(struct ath11k_base *ab, u32 offset, u32 value)
{
	struct ath11k_pci *ab_pci = ath11k_pci_priv(ab);

	/* for offset beyond BAR + 4K - 32, may
	 * need to wakeup MHI to access.
	 */
	if (test_bit(ATH11K_PCI_FLAG_INIT_DONE, &ab_pci->flags) &&
	    offset >= ACCESS_ALWAYS_OFF)
		mhi_device_get_sync(ab_pci->mhi_ctrl->mhi_dev);

	if (offset < WINDOW_START) {
		iowrite32(value, ab->mem  + offset);
	} else {
		spin_lock_bh(&ab_pci->window_lock);
		ath11k_pci_select_window(ab_pci, offset);
		iowrite32(value, ab->mem + WINDOW_START + (offset & WINDOW_RANGE_MASK));
		spin_unlock_bh(&ab_pci->window_lock);
	}

	if (test_bit(ATH11K_PCI_FLAG_INIT_DONE, &ab_pci->flags) &&
	    offset >= ACCESS_ALWAYS_OFF)
		mhi_device_put(ab_pci->mhi_ctrl->mhi_dev);
}

u32 ath11k_pci_read32(struct ath11k_base *ab, u32 offset)
{
	struct ath11k_pci *ab_pci = ath11k_pci_priv(ab);
	u32 val;

	/* for offset beyond BAR + 4K - 32, may
	 * need to wakeup MHI to access.
	 */
	if (test_bit(ATH11K_PCI_FLAG_INIT_DONE, &ab_pci->flags) &&
	    offset >= ACCESS_ALWAYS_OFF)
		mhi_device_get_sync(ab_pci->mhi_ctrl->mhi_dev);

	if (offset < WINDOW_START) {
		val = ioread32(ab->mem + offset);
	} else {
		spin_lock_bh(&ab_pci->window_lock);
		ath11k_pci_select_window(ab_pci, offset);
		val = ioread32(ab->mem + WINDOW_START + (offset & WINDOW_RANGE_MASK));
		spin_unlock_bh(&ab_pci->window_lock);
	}

	if (test_bit(ATH11K_PCI_FLAG_INIT_DONE, &ab_pci->flags) &&
	    offset >= ACCESS_ALWAYS_OFF)
		mhi_device_put(ab_pci->mhi_ctrl->mhi_dev);

	return val;
}

static void ath11k_pci_soc_global_reset(struct ath11k_base *ab)
{
	u32 val, delay;

	val = ath11k_pci_read32(ab, PCIE_SOC_GLOBAL_RESET);

	val |= PCIE_SOC_GLOBAL_RESET_V;

	ath11k_pci_write32(ab, PCIE_SOC_GLOBAL_RESET, val);

	/* TODO: exact time to sleep is uncertain */
	delay = 10;
	mdelay(delay);

	/* Need to toggle V bit back otherwise stuck in reset status */
	val &= ~PCIE_SOC_GLOBAL_RESET_V;

	ath11k_pci_write32(ab, PCIE_SOC_GLOBAL_RESET, val);

	mdelay(delay);

	val = ath11k_pci_read32(ab, PCIE_SOC_GLOBAL_RESET);
	if (val == 0xffffffff)
		ath11k_warn(ab, "link down error during global reset\n");
}

static void ath11k_pci_clear_dbg_registers(struct ath11k_base *ab)
{
	u32 val;

	/* read cookie */
	val = ath11k_pci_read32(ab, PCIE_Q6_COOKIE_ADDR);
	ath11k_dbg(ab, ATH11K_DBG_PCI, "cookie:0x%x\n", val);

	val = ath11k_pci_read32(ab, WLAON_WARM_SW_ENTRY);
	ath11k_dbg(ab, ATH11K_DBG_PCI, "WLAON_WARM_SW_ENTRY 0x%x\n", val);

	/* TODO: exact time to sleep is uncertain */
	mdelay(10);

	/* write 0 to WLAON_WARM_SW_ENTRY to prevent Q6 from
	 * continuing warm path and entering dead loop.
	 */
	ath11k_pci_write32(ab, WLAON_WARM_SW_ENTRY, 0);
	mdelay(10);

	val = ath11k_pci_read32(ab, WLAON_WARM_SW_ENTRY);
	ath11k_dbg(ab, ATH11K_DBG_PCI, "WLAON_WARM_SW_ENTRY 0x%x\n", val);

	/* A read clear register. clear the register to prevent
	 * Q6 from entering wrong code path.
	 */
	val = ath11k_pci_read32(ab, WLAON_SOC_RESET_CAUSE_REG);
	ath11k_dbg(ab, ATH11K_DBG_PCI, "soc reset cause:%d\n", val);
}

static int ath11k_pci_set_link_reg(struct ath11k_base *ab,
				   u32 offset, u32 value, u32 mask)
{
	u32 v;
	int i;

	v = ath11k_pci_read32(ab, offset);
	if ((v & mask) == value)
		return 0;

	for (i = 0; i < 10; i++) {
		ath11k_pci_write32(ab, offset, (v & ~mask) | value);

		v = ath11k_pci_read32(ab, offset);
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
				      PCIE_QSERDES_COM_SYSCLK_EN_SEL_REG,
				      PCIE_QSERDES_COM_SYSCLK_EN_SEL_VAL,
				      PCIE_QSERDES_COM_SYSCLK_EN_SEL_MSK);
	if (ret) {
		ath11k_warn(ab, "failed to set sysclk: %d\n", ret);
		return ret;
	}

	ret = ath11k_pci_set_link_reg(ab,
				      PCIE_USB3_PCS_MISC_OSC_DTCT_CONFIG1_REG,
				      PCIE_USB3_PCS_MISC_OSC_DTCT_CONFIG1_VAL,
				      PCIE_USB3_PCS_MISC_OSC_DTCT_CONFIG_MSK);
	if (ret) {
		ath11k_warn(ab, "failed to set dtct config1 error: %d\n", ret);
		return ret;
	}

	ret = ath11k_pci_set_link_reg(ab,
				      PCIE_USB3_PCS_MISC_OSC_DTCT_CONFIG2_REG,
				      PCIE_USB3_PCS_MISC_OSC_DTCT_CONFIG2_VAL,
				      PCIE_USB3_PCS_MISC_OSC_DTCT_CONFIG_MSK);
	if (ret) {
		ath11k_warn(ab, "failed to set dtct config2: %d\n", ret);
		return ret;
	}

	ret = ath11k_pci_set_link_reg(ab,
				      PCIE_USB3_PCS_MISC_OSC_DTCT_CONFIG4_REG,
				      PCIE_USB3_PCS_MISC_OSC_DTCT_CONFIG4_VAL,
				      PCIE_USB3_PCS_MISC_OSC_DTCT_CONFIG_MSK);
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

	val = ath11k_pci_read32(ab, PCIE_PCIE_PARF_LTSSM);

	/* PCIE link seems very unstable after the Hot Reset*/
	for (i = 0; val != PARM_LTSSM_VALUE && i < 5; i++) {
		if (val == 0xffffffff)
			mdelay(5);

		ath11k_pci_write32(ab, PCIE_PCIE_PARF_LTSSM, PARM_LTSSM_VALUE);
		val = ath11k_pci_read32(ab, PCIE_PCIE_PARF_LTSSM);
	}

	ath11k_dbg(ab, ATH11K_DBG_PCI, "pci ltssm 0x%x\n", val);

	val = ath11k_pci_read32(ab, GCC_GCC_PCIE_HOT_RST);
	val |= GCC_GCC_PCIE_HOT_RST_VAL;
	ath11k_pci_write32(ab, GCC_GCC_PCIE_HOT_RST, val);
	val = ath11k_pci_read32(ab, GCC_GCC_PCIE_HOT_RST);

	ath11k_dbg(ab, ATH11K_DBG_PCI, "pci pcie_hot_rst 0x%x\n", val);

	mdelay(5);
}

static void ath11k_pci_clear_all_intrs(struct ath11k_base *ab)
{
	/* This is a WAR for PCIE Hotreset.
	 * When target receive Hotreset, but will set the interrupt.
	 * So when download SBL again, SBL will open Interrupt and
	 * receive it, and crash immediately.
	 */
	ath11k_pci_write32(ab, PCIE_PCIE_INT_ALL_CLEAR, PCIE_INT_CLEAR_ALL);
}

static void ath11k_pci_set_wlaon_pwr_ctrl(struct ath11k_base *ab)
{
	u32 val;

	val = ath11k_pci_read32(ab, WLAON_QFPROM_PWR_CTRL_REG);
	val &= ~QFPROM_PWR_CTRL_VDD4BLOW_MASK;
	ath11k_pci_write32(ab, WLAON_QFPROM_PWR_CTRL_REG, val);
}

static void ath11k_pci_force_wake(struct ath11k_base *ab)
{
	ath11k_pci_write32(ab, PCIE_SOC_WAKE_PCIE_LOCAL_REG, 1);
	mdelay(5);
}

static void ath11k_pci_sw_reset(struct ath11k_base *ab, bool power_on)
{
	if (power_on) {
		ath11k_pci_enable_ltssm(ab);
		ath11k_pci_clear_all_intrs(ab);
		ath11k_pci_set_wlaon_pwr_ctrl(ab);
		ath11k_pci_fix_l1ss(ab);
	}

	ath11k_mhi_clear_vector(ab);
	ath11k_pci_soc_global_reset(ab);
	ath11k_mhi_set_mhictrl_reset(ab);
	ath11k_pci_clear_dbg_registers(ab);
}

int ath11k_pci_get_msi_irq(struct device *dev, unsigned int vector)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);

	return pci_irq_vector(pci_dev, vector);
}

static void ath11k_pci_get_msi_address(struct ath11k_base *ab, u32 *msi_addr_lo,
				       u32 *msi_addr_hi)
{
	struct ath11k_pci *ab_pci = ath11k_pci_priv(ab);
	struct pci_dev *pci_dev = to_pci_dev(ab->dev);

	pci_read_config_dword(pci_dev, pci_dev->msi_cap + PCI_MSI_ADDRESS_LO,
			      msi_addr_lo);

	if (test_bit(ATH11K_PCI_FLAG_IS_MSI_64, &ab_pci->flags)) {
		pci_read_config_dword(pci_dev, pci_dev->msi_cap + PCI_MSI_ADDRESS_HI,
				      msi_addr_hi);
	} else {
		*msi_addr_hi = 0;
	}
}

int ath11k_pci_get_user_msi_assignment(struct ath11k_pci *ab_pci, char *user_name,
				       int *num_vectors, u32 *user_base_data,
				       u32 *base_vector)
{
	struct ath11k_base *ab = ab_pci->ab;
	int idx;

	for (idx = 0; idx < msi_config.total_users; idx++) {
		if (strcmp(user_name, msi_config.users[idx].name) == 0) {
			*num_vectors = msi_config.users[idx].num_vectors;
			*user_base_data = msi_config.users[idx].base_vector
				+ ab_pci->msi_ep_base_data;
			*base_vector = msi_config.users[idx].base_vector;

			ath11k_dbg(ab, ATH11K_DBG_PCI, "Assign MSI to user: %s, num_vectors: %d, user_base_data: %u, base_vector: %u\n",
				   user_name, *num_vectors, *user_base_data,
				   *base_vector);

			return 0;
		}
	}

	ath11k_err(ab, "Failed to find MSI assignment for %s!\n", user_name);

	return -EINVAL;
}

static int ath11k_get_user_msi_assignment(struct ath11k_base *ab, char *user_name,
					  int *num_vectors, u32 *user_base_data,
					  u32 *base_vector)
{
	struct ath11k_pci *ab_pci = ath11k_pci_priv(ab);

	return ath11k_pci_get_user_msi_assignment(ab_pci, user_name,
						  num_vectors, user_base_data,
						  base_vector);
}

static void ath11k_pci_free_ext_irq(struct ath11k_base *ab)
{
	int i, j;

	for (i = 0; i < ATH11K_EXT_IRQ_GRP_NUM_MAX; i++) {
		struct ath11k_ext_irq_grp *irq_grp = &ab->ext_irq_grp[i];

		for (j = 0; j < irq_grp->num_irq; j++)
			free_irq(ab->irq_num[irq_grp->irqs[j]], irq_grp);

		netif_napi_del(&irq_grp->napi);
	}
}

static void ath11k_pci_free_irq(struct ath11k_base *ab)
{
	int i, irq_idx;

	for (i = 0; i < ab->hw_params.ce_count; i++) {
		if (ath11k_ce_get_attr_flags(ab, i) & CE_ATTR_DIS_INTR)
			continue;
		irq_idx = ATH11K_PCI_IRQ_CE0_OFFSET + i;
		free_irq(ab->irq_num[irq_idx], &ab->ce.ce_pipe[i]);
	}

	ath11k_pci_free_ext_irq(ab);
}

static void ath11k_pci_ce_irq_enable(struct ath11k_base *ab, u16 ce_id)
{
	u32 irq_idx;

	irq_idx = ATH11K_PCI_IRQ_CE0_OFFSET + ce_id;
	enable_irq(ab->irq_num[irq_idx]);
}

static void ath11k_pci_ce_irq_disable(struct ath11k_base *ab, u16 ce_id)
{
	u32 irq_idx;

	irq_idx = ATH11K_PCI_IRQ_CE0_OFFSET + ce_id;
	disable_irq_nosync(ab->irq_num[irq_idx]);
}

static void ath11k_pci_ce_irqs_disable(struct ath11k_base *ab)
{
	int i;

	for (i = 0; i < ab->hw_params.ce_count; i++) {
		if (ath11k_ce_get_attr_flags(ab, i) & CE_ATTR_DIS_INTR)
			continue;
		ath11k_pci_ce_irq_disable(ab, i);
	}
}

static void ath11k_pci_sync_ce_irqs(struct ath11k_base *ab)
{
	int i;
	int irq_idx;

	for (i = 0; i < ab->hw_params.ce_count; i++) {
		if (ath11k_ce_get_attr_flags(ab, i) & CE_ATTR_DIS_INTR)
			continue;

		irq_idx = ATH11K_PCI_IRQ_CE0_OFFSET + i;
		synchronize_irq(ab->irq_num[irq_idx]);
	}
}

static void ath11k_pci_ce_tasklet(struct tasklet_struct *t)
{
	struct ath11k_ce_pipe *ce_pipe = from_tasklet(ce_pipe, t, intr_tq);

	ath11k_ce_per_engine_service(ce_pipe->ab, ce_pipe->pipe_num);

	ath11k_pci_ce_irq_enable(ce_pipe->ab, ce_pipe->pipe_num);
}

static irqreturn_t ath11k_pci_ce_interrupt_handler(int irq, void *arg)
{
	struct ath11k_ce_pipe *ce_pipe = arg;

	ath11k_pci_ce_irq_disable(ce_pipe->ab, ce_pipe->pipe_num);
	tasklet_schedule(&ce_pipe->intr_tq);

	return IRQ_HANDLED;
}

static void ath11k_pci_ext_grp_disable(struct ath11k_ext_irq_grp *irq_grp)
{
	int i;

	for (i = 0; i < irq_grp->num_irq; i++)
		disable_irq_nosync(irq_grp->ab->irq_num[irq_grp->irqs[i]]);
}

static void __ath11k_pci_ext_irq_disable(struct ath11k_base *sc)
{
	int i;

	for (i = 0; i < ATH11K_EXT_IRQ_GRP_NUM_MAX; i++) {
		struct ath11k_ext_irq_grp *irq_grp = &sc->ext_irq_grp[i];

		ath11k_pci_ext_grp_disable(irq_grp);

		napi_synchronize(&irq_grp->napi);
		napi_disable(&irq_grp->napi);
	}
}

static void ath11k_pci_ext_grp_enable(struct ath11k_ext_irq_grp *irq_grp)
{
	int i;

	for (i = 0; i < irq_grp->num_irq; i++)
		enable_irq(irq_grp->ab->irq_num[irq_grp->irqs[i]]);
}

static void ath11k_pci_ext_irq_enable(struct ath11k_base *ab)
{
	int i;

	for (i = 0; i < ATH11K_EXT_IRQ_GRP_NUM_MAX; i++) {
		struct ath11k_ext_irq_grp *irq_grp = &ab->ext_irq_grp[i];

		napi_enable(&irq_grp->napi);
		ath11k_pci_ext_grp_enable(irq_grp);
	}
}

static void ath11k_pci_sync_ext_irqs(struct ath11k_base *ab)
{
	int i, j, irq_idx;

	for (i = 0; i < ATH11K_EXT_IRQ_GRP_NUM_MAX; i++) {
		struct ath11k_ext_irq_grp *irq_grp = &ab->ext_irq_grp[i];

		for (j = 0; j < irq_grp->num_irq; j++) {
			irq_idx = irq_grp->irqs[j];
			synchronize_irq(ab->irq_num[irq_idx]);
		}
	}
}

static void ath11k_pci_ext_irq_disable(struct ath11k_base *ab)
{
	__ath11k_pci_ext_irq_disable(ab);
	ath11k_pci_sync_ext_irqs(ab);
}

static int ath11k_pci_ext_grp_napi_poll(struct napi_struct *napi, int budget)
{
	struct ath11k_ext_irq_grp *irq_grp = container_of(napi,
						struct ath11k_ext_irq_grp,
						napi);
	struct ath11k_base *ab = irq_grp->ab;
	int work_done;

	work_done = ath11k_dp_service_srng(ab, irq_grp, budget);
	if (work_done < budget) {
		napi_complete_done(napi, work_done);
		ath11k_pci_ext_grp_enable(irq_grp);
	}

	if (work_done > budget)
		work_done = budget;

	return work_done;
}

static irqreturn_t ath11k_pci_ext_interrupt_handler(int irq, void *arg)
{
	struct ath11k_ext_irq_grp *irq_grp = arg;

	ath11k_dbg(irq_grp->ab, ATH11K_DBG_PCI, "ext irq:%d\n", irq);

	ath11k_pci_ext_grp_disable(irq_grp);

	napi_schedule(&irq_grp->napi);

	return IRQ_HANDLED;
}

static int ath11k_pci_ext_irq_config(struct ath11k_base *ab)
{
	int i, j, ret, num_vectors = 0;
	u32 user_base_data = 0, base_vector = 0;

	ret = ath11k_pci_get_user_msi_assignment(ath11k_pci_priv(ab), "DP",
						 &num_vectors,
						 &user_base_data,
						 &base_vector);
	if (ret < 0)
		return ret;

	for (i = 0; i < ATH11K_EXT_IRQ_GRP_NUM_MAX; i++) {
		struct ath11k_ext_irq_grp *irq_grp = &ab->ext_irq_grp[i];
		u32 num_irq = 0;

		irq_grp->ab = ab;
		irq_grp->grp_id = i;
		init_dummy_netdev(&irq_grp->napi_ndev);
		netif_napi_add(&irq_grp->napi_ndev, &irq_grp->napi,
			       ath11k_pci_ext_grp_napi_poll, NAPI_POLL_WEIGHT);

		if (ab->hw_params.ring_mask->tx[i] ||
		    ab->hw_params.ring_mask->rx[i] ||
		    ab->hw_params.ring_mask->rx_err[i] ||
		    ab->hw_params.ring_mask->rx_wbm_rel[i] ||
		    ab->hw_params.ring_mask->reo_status[i] ||
		    ab->hw_params.ring_mask->rxdma2host[i] ||
		    ab->hw_params.ring_mask->host2rxdma[i] ||
		    ab->hw_params.ring_mask->rx_mon_status[i]) {
			num_irq = 1;
		}

		irq_grp->num_irq = num_irq;
		irq_grp->irqs[0] = base_vector + i;

		for (j = 0; j < irq_grp->num_irq; j++) {
			int irq_idx = irq_grp->irqs[j];
			int vector = (i % num_vectors) + base_vector;
			int irq = ath11k_pci_get_msi_irq(ab->dev, vector);

			ab->irq_num[irq_idx] = irq;

			ath11k_dbg(ab, ATH11K_DBG_PCI,
				   "irq:%d group:%d\n", irq, i);
			ret = request_irq(irq, ath11k_pci_ext_interrupt_handler,
					  IRQF_SHARED,
					  "DP_EXT_IRQ", irq_grp);
			if (ret) {
				ath11k_err(ab, "failed request irq %d: %d\n",
					   vector, ret);
				return ret;
			}

			disable_irq_nosync(ab->irq_num[irq_idx]);
		}
	}

	return 0;
}

static int ath11k_pci_config_irq(struct ath11k_base *ab)
{
	struct ath11k_ce_pipe *ce_pipe;
	u32 msi_data_start;
	u32 msi_data_count;
	u32 msi_irq_start;
	unsigned int msi_data;
	int irq, i, ret, irq_idx;

	ret = ath11k_pci_get_user_msi_assignment(ath11k_pci_priv(ab),
						 "CE", &msi_data_count,
						 &msi_data_start, &msi_irq_start);
	if (ret)
		return ret;

	/* Configure CE irqs */
	for (i = 0; i < ab->hw_params.ce_count; i++) {
		msi_data = (i % msi_data_count) + msi_irq_start;
		irq = ath11k_pci_get_msi_irq(ab->dev, msi_data);
		ce_pipe = &ab->ce.ce_pipe[i];

		if (ath11k_ce_get_attr_flags(ab, i) & CE_ATTR_DIS_INTR)
			continue;

		irq_idx = ATH11K_PCI_IRQ_CE0_OFFSET + i;

		tasklet_setup(&ce_pipe->intr_tq, ath11k_pci_ce_tasklet);

		ret = request_irq(irq, ath11k_pci_ce_interrupt_handler,
				  IRQF_SHARED, irq_name[irq_idx],
				  ce_pipe);
		if (ret) {
			ath11k_err(ab, "failed to request irq %d: %d\n",
				   irq_idx, ret);
			return ret;
		}

		ab->irq_num[irq_idx] = irq;
		ath11k_pci_ce_irq_disable(ab, i);
	}

	ret = ath11k_pci_ext_irq_config(ab);
	if (ret)
		return ret;

	return 0;
}

static void ath11k_pci_init_qmi_ce_config(struct ath11k_base *ab)
{
	struct ath11k_qmi_ce_cfg *cfg = &ab->qmi.ce_cfg;

	cfg->tgt_ce = ab->hw_params.target_ce_config;
	cfg->tgt_ce_len = ab->hw_params.target_ce_count;

	cfg->svc_to_ce_map = ab->hw_params.svc_to_ce_map;
	cfg->svc_to_ce_map_len = ab->hw_params.svc_to_ce_map_len;
	ab->qmi.service_ins_id = ATH11K_QMI_WLFW_SERVICE_INS_ID_V01_QCA6390;

	ath11k_ce_get_shadow_config(ab, &cfg->shadow_reg_v2,
				    &cfg->shadow_reg_v2_len);
}

static void ath11k_pci_ce_irqs_enable(struct ath11k_base *ab)
{
	int i;

	for (i = 0; i < ab->hw_params.ce_count; i++) {
		if (ath11k_ce_get_attr_flags(ab, i) & CE_ATTR_DIS_INTR)
			continue;
		ath11k_pci_ce_irq_enable(ab, i);
	}
}

static int ath11k_pci_enable_msi(struct ath11k_pci *ab_pci)
{
	struct ath11k_base *ab = ab_pci->ab;
	struct msi_desc *msi_desc;
	int num_vectors;
	int ret;

	num_vectors = pci_alloc_irq_vectors(ab_pci->pdev,
					    msi_config.total_vectors,
					    msi_config.total_vectors,
					    PCI_IRQ_MSI);
	if (num_vectors != msi_config.total_vectors) {
		ath11k_err(ab, "failed to get %d MSI vectors, only %d available",
			   msi_config.total_vectors, num_vectors);

		if (num_vectors >= 0)
			return -EINVAL;
		else
			return num_vectors;
	}

	msi_desc = irq_get_msi_desc(ab_pci->pdev->irq);
	if (!msi_desc) {
		ath11k_err(ab, "msi_desc is NULL!\n");
		ret = -EINVAL;
		goto free_msi_vector;
	}

	ab_pci->msi_ep_base_data = msi_desc->msg.data;
	if (msi_desc->msi_attrib.is_64)
		set_bit(ATH11K_PCI_FLAG_IS_MSI_64, &ab_pci->flags);

	ath11k_dbg(ab, ATH11K_DBG_PCI, "msi base data is %d\n", ab_pci->msi_ep_base_data);

	return 0;

free_msi_vector:
	pci_free_irq_vectors(ab_pci->pdev);

	return ret;
}

static void ath11k_pci_disable_msi(struct ath11k_pci *ab_pci)
{
	pci_free_irq_vectors(ab_pci->pdev);
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

	ret = pci_set_dma_mask(pdev, DMA_BIT_MASK(ATH11K_PCI_DMA_MASK));
	if (ret) {
		ath11k_err(ab, "failed to set pci dma mask to %d: %d\n",
			   ATH11K_PCI_DMA_MASK, ret);
		goto release_region;
	}

	ret = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(ATH11K_PCI_DMA_MASK));
	if (ret) {
		ath11k_err(ab, "failed to set pci consistent dma mask to %d: %d\n",
			   ATH11K_PCI_DMA_MASK, ret);
		goto release_region;
	}

	pci_set_master(pdev);

	ab->mem_len = pci_resource_len(pdev, ATH11K_PCI_BAR_NUM);
	ab->mem = pci_iomap(pdev, ATH11K_PCI_BAR_NUM, 0);
	if (!ab->mem) {
		ath11k_err(ab, "failed to map pci bar %d\n", ATH11K_PCI_BAR_NUM);
		ret = -EIO;
		goto clear_master;
	}

	ath11k_dbg(ab, ATH11K_DBG_BOOT, "boot pci_mem 0x%pK\n", ab->mem);
	return 0;

clear_master:
	pci_clear_master(pdev);
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
	pci_clear_master(pci_dev);
	pci_release_region(pci_dev, ATH11K_PCI_BAR_NUM);
	if (pci_is_enabled(pci_dev))
		pci_disable_device(pci_dev);
}

static void ath11k_pci_aspm_disable(struct ath11k_pci *ab_pci)
{
	struct ath11k_base *ab = ab_pci->ab;

	pcie_capability_read_word(ab_pci->pdev, PCI_EXP_LNKCTL,
				  &ab_pci->link_ctl);

	ath11k_dbg(ab, ATH11K_DBG_PCI, "pci link_ctl 0x%04x L0s %d L1 %d\n",
		   ab_pci->link_ctl,
		   u16_get_bits(ab_pci->link_ctl, PCI_EXP_LNKCTL_ASPM_L0S),
		   u16_get_bits(ab_pci->link_ctl, PCI_EXP_LNKCTL_ASPM_L1));

	/* disable L0s and L1 */
	pcie_capability_write_word(ab_pci->pdev, PCI_EXP_LNKCTL,
				   ab_pci->link_ctl & ~PCI_EXP_LNKCTL_ASPMC);

	set_bit(ATH11K_PCI_ASPM_RESTORE, &ab_pci->flags);
}

static void ath11k_pci_aspm_restore(struct ath11k_pci *ab_pci)
{
	if (test_and_clear_bit(ATH11K_PCI_ASPM_RESTORE, &ab_pci->flags))
		pcie_capability_write_word(ab_pci->pdev, PCI_EXP_LNKCTL,
					   ab_pci->link_ctl);
}

static int ath11k_pci_power_up(struct ath11k_base *ab)
{
	struct ath11k_pci *ab_pci = ath11k_pci_priv(ab);
	int ret;

	ab_pci->register_window = 0;
	clear_bit(ATH11K_PCI_FLAG_INIT_DONE, &ab_pci->flags);
	ath11k_pci_sw_reset(ab_pci->ab, true);

	/* Disable ASPM during firmware download due to problems switching
	 * to AMSS state.
	 */
	ath11k_pci_aspm_disable(ab_pci);

	ret = ath11k_mhi_start(ab_pci);
	if (ret) {
		ath11k_err(ab, "failed to start mhi: %d\n", ret);
		return ret;
	}

	return 0;
}

static void ath11k_pci_power_down(struct ath11k_base *ab)
{
	struct ath11k_pci *ab_pci = ath11k_pci_priv(ab);

	/* restore aspm in case firmware bootup fails */
	ath11k_pci_aspm_restore(ab_pci);

	ath11k_pci_force_wake(ab_pci->ab);
	ath11k_mhi_stop(ab_pci);
	clear_bit(ATH11K_PCI_FLAG_INIT_DONE, &ab_pci->flags);
	ath11k_pci_sw_reset(ab_pci->ab, false);
}

static int ath11k_pci_hif_suspend(struct ath11k_base *ab)
{
	struct ath11k_pci *ar_pci = ath11k_pci_priv(ab);

	ath11k_mhi_suspend(ar_pci);

	return 0;
}

static int ath11k_pci_hif_resume(struct ath11k_base *ab)
{
	struct ath11k_pci *ar_pci = ath11k_pci_priv(ab);

	ath11k_mhi_resume(ar_pci);

	return 0;
}

static void ath11k_pci_kill_tasklets(struct ath11k_base *ab)
{
	int i;

	for (i = 0; i < ab->hw_params.ce_count; i++) {
		struct ath11k_ce_pipe *ce_pipe = &ab->ce.ce_pipe[i];

		if (ath11k_ce_get_attr_flags(ab, i) & CE_ATTR_DIS_INTR)
			continue;

		tasklet_kill(&ce_pipe->intr_tq);
	}
}

static void ath11k_pci_ce_irq_disable_sync(struct ath11k_base *ab)
{
	ath11k_pci_ce_irqs_disable(ab);
	ath11k_pci_sync_ce_irqs(ab);
	ath11k_pci_kill_tasklets(ab);
}

static void ath11k_pci_stop(struct ath11k_base *ab)
{
	ath11k_pci_ce_irq_disable_sync(ab);
	ath11k_ce_cleanup_pipes(ab);
}

static int ath11k_pci_start(struct ath11k_base *ab)
{
	struct ath11k_pci *ab_pci = ath11k_pci_priv(ab);

	set_bit(ATH11K_PCI_FLAG_INIT_DONE, &ab_pci->flags);

	ath11k_pci_aspm_restore(ab_pci);

	ath11k_pci_ce_irqs_enable(ab);
	ath11k_ce_rx_post_buf(ab);

	return 0;
}

static void ath11k_pci_hif_ce_irq_enable(struct ath11k_base *ab)
{
	ath11k_pci_ce_irqs_enable(ab);
}

static void ath11k_pci_hif_ce_irq_disable(struct ath11k_base *ab)
{
	ath11k_pci_ce_irq_disable_sync(ab);
}

static int ath11k_pci_map_service_to_pipe(struct ath11k_base *ab, u16 service_id,
					  u8 *ul_pipe, u8 *dl_pipe)
{
	const struct service_to_pipe *entry;
	bool ul_set = false, dl_set = false;
	int i;

	for (i = 0; i < ab->hw_params.svc_to_ce_map_len; i++) {
		entry = &ab->hw_params.svc_to_ce_map[i];

		if (__le32_to_cpu(entry->service_id) != service_id)
			continue;

		switch (__le32_to_cpu(entry->pipedir)) {
		case PIPEDIR_NONE:
			break;
		case PIPEDIR_IN:
			WARN_ON(dl_set);
			*dl_pipe = __le32_to_cpu(entry->pipenum);
			dl_set = true;
			break;
		case PIPEDIR_OUT:
			WARN_ON(ul_set);
			*ul_pipe = __le32_to_cpu(entry->pipenum);
			ul_set = true;
			break;
		case PIPEDIR_INOUT:
			WARN_ON(dl_set);
			WARN_ON(ul_set);
			*dl_pipe = __le32_to_cpu(entry->pipenum);
			*ul_pipe = __le32_to_cpu(entry->pipenum);
			dl_set = true;
			ul_set = true;
			break;
		}
	}

	if (WARN_ON(!ul_set || !dl_set))
		return -ENOENT;

	return 0;
}

static const struct ath11k_hif_ops ath11k_pci_hif_ops = {
	.start = ath11k_pci_start,
	.stop = ath11k_pci_stop,
	.read32 = ath11k_pci_read32,
	.write32 = ath11k_pci_write32,
	.power_down = ath11k_pci_power_down,
	.power_up = ath11k_pci_power_up,
	.suspend = ath11k_pci_hif_suspend,
	.resume = ath11k_pci_hif_resume,
	.irq_enable = ath11k_pci_ext_irq_enable,
	.irq_disable = ath11k_pci_ext_irq_disable,
	.get_msi_address =  ath11k_pci_get_msi_address,
	.get_user_msi_vector = ath11k_get_user_msi_assignment,
	.map_service_to_pipe = ath11k_pci_map_service_to_pipe,
	.ce_irq_enable = ath11k_pci_hif_ce_irq_enable,
	.ce_irq_disable = ath11k_pci_hif_ce_irq_disable,
};

static int ath11k_pci_probe(struct pci_dev *pdev,
			    const struct pci_device_id *pci_dev)
{
	struct ath11k_base *ab;
	struct ath11k_pci *ab_pci;
	u32 soc_hw_version, soc_hw_version_major, soc_hw_version_minor;
	int ret;

	ab = ath11k_core_alloc(&pdev->dev, sizeof(*ab_pci), ATH11K_BUS_PCI,
			       &ath11k_pci_bus_params);
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
	pci_set_drvdata(pdev, ab);
	spin_lock_init(&ab_pci->window_lock);

	ret = ath11k_pci_claim(ab_pci, pdev);
	if (ret) {
		ath11k_err(ab, "failed to claim device: %d\n", ret);
		goto err_free_core;
	}

	switch (pci_dev->device) {
	case QCA6390_DEVICE_ID:
		soc_hw_version = ath11k_pci_read32(ab, TCSR_SOC_HW_VERSION);
		soc_hw_version_major = FIELD_GET(TCSR_SOC_HW_VERSION_MAJOR_MASK,
						 soc_hw_version);
		soc_hw_version_minor = FIELD_GET(TCSR_SOC_HW_VERSION_MINOR_MASK,
						 soc_hw_version);

		ath11k_dbg(ab, ATH11K_DBG_PCI, "pci tcsr_soc_hw_version major %d minor %d\n",
			   soc_hw_version_major, soc_hw_version_minor);

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
	default:
		dev_err(&pdev->dev, "Unknown PCI device found: 0x%x\n",
			pci_dev->device);
		ret = -EOPNOTSUPP;
		goto err_pci_free_region;
	}

	ret = ath11k_pci_enable_msi(ab_pci);
	if (ret) {
		ath11k_err(ab, "failed to enable msi: %d\n", ret);
		goto err_pci_free_region;
	}

	ret = ath11k_core_pre_init(ab);
	if (ret)
		goto err_pci_disable_msi;

	ret = ath11k_mhi_register(ab_pci);
	if (ret) {
		ath11k_err(ab, "failed to register mhi: %d\n", ret);
		goto err_pci_disable_msi;
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

	ret = ath11k_pci_config_irq(ab);
	if (ret) {
		ath11k_err(ab, "failed to config irq: %d\n", ret);
		goto err_ce_free;
	}

	ret = ath11k_core_init(ab);
	if (ret) {
		ath11k_err(ab, "failed to init core: %d\n", ret);
		goto err_free_irq;
	}
	return 0;

err_free_irq:
	ath11k_pci_free_irq(ab);

err_ce_free:
	ath11k_ce_free_pipes(ab);

err_hal_srng_deinit:
	ath11k_hal_srng_deinit(ab);

err_mhi_unregister:
	ath11k_mhi_unregister(ab_pci);

err_pci_disable_msi:
	ath11k_pci_disable_msi(ab_pci);

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

	if (test_bit(ATH11K_FLAG_QMI_FAIL, &ab->dev_flags)) {
		ath11k_pci_power_down(ab);
		ath11k_debugfs_soc_destroy(ab);
		ath11k_qmi_deinit_service(ab);
		goto qmi_fail;
	}

	set_bit(ATH11K_FLAG_UNREGISTERING, &ab->dev_flags);

	ath11k_core_deinit(ab);

qmi_fail:
	ath11k_mhi_unregister(ab_pci);

	ath11k_pci_free_irq(ab);
	ath11k_pci_disable_msi(ab_pci);
	ath11k_pci_free_region(ab_pci);

	ath11k_hal_srng_deinit(ab);
	ath11k_ce_free_pipes(ab);
	ath11k_core_free(ab);
}

static void ath11k_pci_shutdown(struct pci_dev *pdev)
{
	struct ath11k_base *ab = pci_get_drvdata(pdev);

	ath11k_pci_power_down(ab);
}

static __maybe_unused int ath11k_pci_pm_suspend(struct device *dev)
{
	struct ath11k_base *ab = dev_get_drvdata(dev);
	int ret;

	ret = ath11k_core_suspend(ab);
	if (ret)
		ath11k_warn(ab, "failed to suspend core: %d\n", ret);

	return ret;
}

static __maybe_unused int ath11k_pci_pm_resume(struct device *dev)
{
	struct ath11k_base *ab = dev_get_drvdata(dev);
	int ret;

	ret = ath11k_core_resume(ab);
	if (ret)
		ath11k_warn(ab, "failed to resume core: %d\n", ret);

	return ret;
}

static SIMPLE_DEV_PM_OPS(ath11k_pci_pm_ops,
			 ath11k_pci_pm_suspend,
			 ath11k_pci_pm_resume);

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

MODULE_DESCRIPTION("Driver support for Qualcomm Technologies 802.11ax WLAN PCIe devices");
MODULE_LICENSE("Dual BSD/GPL");

/* QCA639x 2.0 firmware files */
MODULE_FIRMWARE(ATH11K_FW_DIR "/QCA6390/hw2.0/" ATH11K_BOARD_API2_FILE);
MODULE_FIRMWARE(ATH11K_FW_DIR "/QCA6390/hw2.0/" ATH11K_AMSS_FILE);
MODULE_FIRMWARE(ATH11K_FW_DIR "/QCA6390/hw2.0/" ATH11K_M3_FILE);
