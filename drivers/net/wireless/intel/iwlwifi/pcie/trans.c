/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2007 - 2015 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016 Intel Deutschland GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110,
 * USA
 *
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING.
 *
 * Contact Information:
 *  Intel Linux Wireless <linuxwifi@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2005 - 2015 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016 Intel Deutschland GmbH
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/
#include <linux/pci.h>
#include <linux/pci-aspm.h>
#include <linux/interrupt.h>
#include <linux/debugfs.h>
#include <linux/sched.h>
#include <linux/bitops.h>
#include <linux/gfp.h>
#include <linux/vmalloc.h>

#include "iwl-drv.h"
#include "iwl-trans.h"
#include "iwl-csr.h"
#include "iwl-prph.h"
#include "iwl-scd.h"
#include "iwl-agn-hw.h"
#include "iwl-fw-error-dump.h"
#include "internal.h"
#include "iwl-fh.h"

/* extended range in FW SRAM */
#define IWL_FW_MEM_EXTENDED_START	0x40000
#define IWL_FW_MEM_EXTENDED_END		0x57FFF

static void iwl_pcie_free_fw_monitor(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	if (!trans_pcie->fw_mon_page)
		return;

	dma_unmap_page(trans->dev, trans_pcie->fw_mon_phys,
		       trans_pcie->fw_mon_size, DMA_FROM_DEVICE);
	__free_pages(trans_pcie->fw_mon_page,
		     get_order(trans_pcie->fw_mon_size));
	trans_pcie->fw_mon_page = NULL;
	trans_pcie->fw_mon_phys = 0;
	trans_pcie->fw_mon_size = 0;
}

static void iwl_pcie_alloc_fw_monitor(struct iwl_trans *trans, u8 max_power)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct page *page = NULL;
	dma_addr_t phys;
	u32 size = 0;
	u8 power;

	if (!max_power) {
		/* default max_power is maximum */
		max_power = 26;
	} else {
		max_power += 11;
	}

	if (WARN(max_power > 26,
		 "External buffer size for monitor is too big %d, check the FW TLV\n",
		 max_power))
		return;

	if (trans_pcie->fw_mon_page) {
		dma_sync_single_for_device(trans->dev, trans_pcie->fw_mon_phys,
					   trans_pcie->fw_mon_size,
					   DMA_FROM_DEVICE);
		return;
	}

	phys = 0;
	for (power = max_power; power >= 11; power--) {
		int order;

		size = BIT(power);
		order = get_order(size);
		page = alloc_pages(__GFP_COMP | __GFP_NOWARN | __GFP_ZERO,
				   order);
		if (!page)
			continue;

		phys = dma_map_page(trans->dev, page, 0, PAGE_SIZE << order,
				    DMA_FROM_DEVICE);
		if (dma_mapping_error(trans->dev, phys)) {
			__free_pages(page, order);
			page = NULL;
			continue;
		}
		IWL_INFO(trans,
			 "Allocated 0x%08x bytes (order %d) for firmware monitor.\n",
			 size, order);
		break;
	}

	if (WARN_ON_ONCE(!page))
		return;

	if (power != max_power)
		IWL_ERR(trans,
			"Sorry - debug buffer is only %luK while you requested %luK\n",
			(unsigned long)BIT(power - 10),
			(unsigned long)BIT(max_power - 10));

	trans_pcie->fw_mon_page = page;
	trans_pcie->fw_mon_phys = phys;
	trans_pcie->fw_mon_size = size;
}

static u32 iwl_trans_pcie_read_shr(struct iwl_trans *trans, u32 reg)
{
	iwl_write32(trans, HEEP_CTRL_WRD_PCIEX_CTRL_REG,
		    ((reg & 0x0000ffff) | (2 << 28)));
	return iwl_read32(trans, HEEP_CTRL_WRD_PCIEX_DATA_REG);
}

static void iwl_trans_pcie_write_shr(struct iwl_trans *trans, u32 reg, u32 val)
{
	iwl_write32(trans, HEEP_CTRL_WRD_PCIEX_DATA_REG, val);
	iwl_write32(trans, HEEP_CTRL_WRD_PCIEX_CTRL_REG,
		    ((reg & 0x0000ffff) | (3 << 28)));
}

static void iwl_pcie_set_pwr(struct iwl_trans *trans, bool vaux)
{
	if (trans->cfg->apmg_not_supported)
		return;

	if (vaux && pci_pme_capable(to_pci_dev(trans->dev), PCI_D3cold))
		iwl_set_bits_mask_prph(trans, APMG_PS_CTRL_REG,
				       APMG_PS_CTRL_VAL_PWR_SRC_VAUX,
				       ~APMG_PS_CTRL_MSK_PWR_SRC);
	else
		iwl_set_bits_mask_prph(trans, APMG_PS_CTRL_REG,
				       APMG_PS_CTRL_VAL_PWR_SRC_VMAIN,
				       ~APMG_PS_CTRL_MSK_PWR_SRC);
}

/* PCI registers */
#define PCI_CFG_RETRY_TIMEOUT	0x041

static void iwl_pcie_apm_config(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	u16 lctl;
	u16 cap;

	/*
	 * HW bug W/A for instability in PCIe bus L0S->L1 transition.
	 * Check if BIOS (or OS) enabled L1-ASPM on this device.
	 * If so (likely), disable L0S, so device moves directly L0->L1;
	 *    costs negligible amount of power savings.
	 * If not (unlikely), enable L0S, so there is at least some
	 *    power savings, even without L1.
	 */
	pcie_capability_read_word(trans_pcie->pci_dev, PCI_EXP_LNKCTL, &lctl);
	if (lctl & PCI_EXP_LNKCTL_ASPM_L1)
		iwl_set_bit(trans, CSR_GIO_REG, CSR_GIO_REG_VAL_L0S_ENABLED);
	else
		iwl_clear_bit(trans, CSR_GIO_REG, CSR_GIO_REG_VAL_L0S_ENABLED);
	trans->pm_support = !(lctl & PCI_EXP_LNKCTL_ASPM_L0S);

	pcie_capability_read_word(trans_pcie->pci_dev, PCI_EXP_DEVCTL2, &cap);
	trans->ltr_enabled = cap & PCI_EXP_DEVCTL2_LTR_EN;
	dev_info(trans->dev, "L1 %sabled - LTR %sabled\n",
		 (lctl & PCI_EXP_LNKCTL_ASPM_L1) ? "En" : "Dis",
		 trans->ltr_enabled ? "En" : "Dis");
}

/*
 * Start up NIC's basic functionality after it has been reset
 * (e.g. after platform boot, or shutdown via iwl_pcie_apm_stop())
 * NOTE:  This does not load uCode nor start the embedded processor
 */
static int iwl_pcie_apm_init(struct iwl_trans *trans)
{
	int ret = 0;
	IWL_DEBUG_INFO(trans, "Init card's basic functions\n");

	/*
	 * Use "set_bit" below rather than "write", to preserve any hardware
	 * bits already set by default after reset.
	 */

	/* Disable L0S exit timer (platform NMI Work/Around) */
	if (trans->cfg->device_family != IWL_DEVICE_FAMILY_8000)
		iwl_set_bit(trans, CSR_GIO_CHICKEN_BITS,
			    CSR_GIO_CHICKEN_BITS_REG_BIT_DIS_L0S_EXIT_TIMER);

	/*
	 * Disable L0s without affecting L1;
	 *  don't wait for ICH L0s (ICH bug W/A)
	 */
	iwl_set_bit(trans, CSR_GIO_CHICKEN_BITS,
		    CSR_GIO_CHICKEN_BITS_REG_BIT_L1A_NO_L0S_RX);

	/* Set FH wait threshold to maximum (HW error during stress W/A) */
	iwl_set_bit(trans, CSR_DBG_HPET_MEM_REG, CSR_DBG_HPET_MEM_REG_VAL);

	/*
	 * Enable HAP INTA (interrupt from management bus) to
	 * wake device's PCI Express link L1a -> L0s
	 */
	iwl_set_bit(trans, CSR_HW_IF_CONFIG_REG,
		    CSR_HW_IF_CONFIG_REG_BIT_HAP_WAKE_L1A);

	iwl_pcie_apm_config(trans);

	/* Configure analog phase-lock-loop before activating to D0A */
	if (trans->cfg->base_params->pll_cfg_val)
		iwl_set_bit(trans, CSR_ANA_PLL_CFG,
			    trans->cfg->base_params->pll_cfg_val);

	/*
	 * Set "initialization complete" bit to move adapter from
	 * D0U* --> D0A* (powered-up active) state.
	 */
	iwl_set_bit(trans, CSR_GP_CNTRL, CSR_GP_CNTRL_REG_FLAG_INIT_DONE);

	/*
	 * Wait for clock stabilization; once stabilized, access to
	 * device-internal resources is supported, e.g. iwl_write_prph()
	 * and accesses to uCode SRAM.
	 */
	ret = iwl_poll_bit(trans, CSR_GP_CNTRL,
			   CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY,
			   CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY, 25000);
	if (ret < 0) {
		IWL_DEBUG_INFO(trans, "Failed to init the card\n");
		goto out;
	}

	if (trans->cfg->host_interrupt_operation_mode) {
		/*
		 * This is a bit of an abuse - This is needed for 7260 / 3160
		 * only check host_interrupt_operation_mode even if this is
		 * not related to host_interrupt_operation_mode.
		 *
		 * Enable the oscillator to count wake up time for L1 exit. This
		 * consumes slightly more power (100uA) - but allows to be sure
		 * that we wake up from L1 on time.
		 *
		 * This looks weird: read twice the same register, discard the
		 * value, set a bit, and yet again, read that same register
		 * just to discard the value. But that's the way the hardware
		 * seems to like it.
		 */
		iwl_read_prph(trans, OSC_CLK);
		iwl_read_prph(trans, OSC_CLK);
		iwl_set_bits_prph(trans, OSC_CLK, OSC_CLK_FORCE_CONTROL);
		iwl_read_prph(trans, OSC_CLK);
		iwl_read_prph(trans, OSC_CLK);
	}

	/*
	 * Enable DMA clock and wait for it to stabilize.
	 *
	 * Write to "CLK_EN_REG"; "1" bits enable clocks, while "0"
	 * bits do not disable clocks.  This preserves any hardware
	 * bits already set by default in "CLK_CTRL_REG" after reset.
	 */
	if (!trans->cfg->apmg_not_supported) {
		iwl_write_prph(trans, APMG_CLK_EN_REG,
			       APMG_CLK_VAL_DMA_CLK_RQT);
		udelay(20);

		/* Disable L1-Active */
		iwl_set_bits_prph(trans, APMG_PCIDEV_STT_REG,
				  APMG_PCIDEV_STT_VAL_L1_ACT_DIS);

		/* Clear the interrupt in APMG if the NIC is in RFKILL */
		iwl_write_prph(trans, APMG_RTC_INT_STT_REG,
			       APMG_RTC_INT_STT_RFKILL);
	}

	set_bit(STATUS_DEVICE_ENABLED, &trans->status);

out:
	return ret;
}

/*
 * Enable LP XTAL to avoid HW bug where device may consume much power if
 * FW is not loaded after device reset. LP XTAL is disabled by default
 * after device HW reset. Do it only if XTAL is fed by internal source.
 * Configure device's "persistence" mode to avoid resetting XTAL again when
 * SHRD_HW_RST occurs in S3.
 */
static void iwl_pcie_apm_lp_xtal_enable(struct iwl_trans *trans)
{
	int ret;
	u32 apmg_gp1_reg;
	u32 apmg_xtal_cfg_reg;
	u32 dl_cfg_reg;

	/* Force XTAL ON */
	__iwl_trans_pcie_set_bit(trans, CSR_GP_CNTRL,
				 CSR_GP_CNTRL_REG_FLAG_XTAL_ON);

	/* Reset entire device - do controller reset (results in SHRD_HW_RST) */
	iwl_set_bit(trans, CSR_RESET, CSR_RESET_REG_FLAG_SW_RESET);

	udelay(10);

	/*
	 * Set "initialization complete" bit to move adapter from
	 * D0U* --> D0A* (powered-up active) state.
	 */
	iwl_set_bit(trans, CSR_GP_CNTRL, CSR_GP_CNTRL_REG_FLAG_INIT_DONE);

	/*
	 * Wait for clock stabilization; once stabilized, access to
	 * device-internal resources is possible.
	 */
	ret = iwl_poll_bit(trans, CSR_GP_CNTRL,
			   CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY,
			   CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY,
			   25000);
	if (WARN_ON(ret < 0)) {
		IWL_ERR(trans, "Access time out - failed to enable LP XTAL\n");
		/* Release XTAL ON request */
		__iwl_trans_pcie_clear_bit(trans, CSR_GP_CNTRL,
					   CSR_GP_CNTRL_REG_FLAG_XTAL_ON);
		return;
	}

	/*
	 * Clear "disable persistence" to avoid LP XTAL resetting when
	 * SHRD_HW_RST is applied in S3.
	 */
	iwl_clear_bits_prph(trans, APMG_PCIDEV_STT_REG,
				    APMG_PCIDEV_STT_VAL_PERSIST_DIS);

	/*
	 * Force APMG XTAL to be active to prevent its disabling by HW
	 * caused by APMG idle state.
	 */
	apmg_xtal_cfg_reg = iwl_trans_pcie_read_shr(trans,
						    SHR_APMG_XTAL_CFG_REG);
	iwl_trans_pcie_write_shr(trans, SHR_APMG_XTAL_CFG_REG,
				 apmg_xtal_cfg_reg |
				 SHR_APMG_XTAL_CFG_XTAL_ON_REQ);

	/*
	 * Reset entire device again - do controller reset (results in
	 * SHRD_HW_RST). Turn MAC off before proceeding.
	 */
	iwl_set_bit(trans, CSR_RESET, CSR_RESET_REG_FLAG_SW_RESET);

	udelay(10);

	/* Enable LP XTAL by indirect access through CSR */
	apmg_gp1_reg = iwl_trans_pcie_read_shr(trans, SHR_APMG_GP1_REG);
	iwl_trans_pcie_write_shr(trans, SHR_APMG_GP1_REG, apmg_gp1_reg |
				 SHR_APMG_GP1_WF_XTAL_LP_EN |
				 SHR_APMG_GP1_CHICKEN_BIT_SELECT);

	/* Clear delay line clock power up */
	dl_cfg_reg = iwl_trans_pcie_read_shr(trans, SHR_APMG_DL_CFG_REG);
	iwl_trans_pcie_write_shr(trans, SHR_APMG_DL_CFG_REG, dl_cfg_reg &
				 ~SHR_APMG_DL_CFG_DL_CLOCK_POWER_UP);

	/*
	 * Enable persistence mode to avoid LP XTAL resetting when
	 * SHRD_HW_RST is applied in S3.
	 */
	iwl_set_bit(trans, CSR_HW_IF_CONFIG_REG,
		    CSR_HW_IF_CONFIG_REG_PERSIST_MODE);

	/*
	 * Clear "initialization complete" bit to move adapter from
	 * D0A* (powered-up Active) --> D0U* (Uninitialized) state.
	 */
	iwl_clear_bit(trans, CSR_GP_CNTRL,
		      CSR_GP_CNTRL_REG_FLAG_INIT_DONE);

	/* Activates XTAL resources monitor */
	__iwl_trans_pcie_set_bit(trans, CSR_MONITOR_CFG_REG,
				 CSR_MONITOR_XTAL_RESOURCES);

	/* Release XTAL ON request */
	__iwl_trans_pcie_clear_bit(trans, CSR_GP_CNTRL,
				   CSR_GP_CNTRL_REG_FLAG_XTAL_ON);
	udelay(10);

	/* Release APMG XTAL */
	iwl_trans_pcie_write_shr(trans, SHR_APMG_XTAL_CFG_REG,
				 apmg_xtal_cfg_reg &
				 ~SHR_APMG_XTAL_CFG_XTAL_ON_REQ);
}

static int iwl_pcie_apm_stop_master(struct iwl_trans *trans)
{
	int ret = 0;

	/* stop device's busmaster DMA activity */
	iwl_set_bit(trans, CSR_RESET, CSR_RESET_REG_FLAG_STOP_MASTER);

	ret = iwl_poll_bit(trans, CSR_RESET,
			   CSR_RESET_REG_FLAG_MASTER_DISABLED,
			   CSR_RESET_REG_FLAG_MASTER_DISABLED, 100);
	if (ret < 0)
		IWL_WARN(trans, "Master Disable Timed Out, 100 usec\n");

	IWL_DEBUG_INFO(trans, "stop master\n");

	return ret;
}

static void iwl_pcie_apm_stop(struct iwl_trans *trans, bool op_mode_leave)
{
	IWL_DEBUG_INFO(trans, "Stop card, put in low power state\n");

	if (op_mode_leave) {
		if (!test_bit(STATUS_DEVICE_ENABLED, &trans->status))
			iwl_pcie_apm_init(trans);

		/* inform ME that we are leaving */
		if (trans->cfg->device_family == IWL_DEVICE_FAMILY_7000)
			iwl_set_bits_prph(trans, APMG_PCIDEV_STT_REG,
					  APMG_PCIDEV_STT_VAL_WAKE_ME);
		else if (trans->cfg->device_family == IWL_DEVICE_FAMILY_8000) {
			iwl_set_bit(trans, CSR_DBG_LINK_PWR_MGMT_REG,
				    CSR_RESET_LINK_PWR_MGMT_DISABLED);
			iwl_set_bit(trans, CSR_HW_IF_CONFIG_REG,
				    CSR_HW_IF_CONFIG_REG_PREPARE |
				    CSR_HW_IF_CONFIG_REG_ENABLE_PME);
			mdelay(1);
			iwl_clear_bit(trans, CSR_DBG_LINK_PWR_MGMT_REG,
				      CSR_RESET_LINK_PWR_MGMT_DISABLED);
		}
		mdelay(5);
	}

	clear_bit(STATUS_DEVICE_ENABLED, &trans->status);

	/* Stop device's DMA activity */
	iwl_pcie_apm_stop_master(trans);

	if (trans->cfg->lp_xtal_workaround) {
		iwl_pcie_apm_lp_xtal_enable(trans);
		return;
	}

	/* Reset the entire device */
	iwl_set_bit(trans, CSR_RESET, CSR_RESET_REG_FLAG_SW_RESET);

	udelay(10);

	/*
	 * Clear "initialization complete" bit to move adapter from
	 * D0A* (powered-up Active) --> D0U* (Uninitialized) state.
	 */
	iwl_clear_bit(trans, CSR_GP_CNTRL,
		      CSR_GP_CNTRL_REG_FLAG_INIT_DONE);
}

static int iwl_pcie_nic_init(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	/* nic_init */
	spin_lock(&trans_pcie->irq_lock);
	iwl_pcie_apm_init(trans);

	spin_unlock(&trans_pcie->irq_lock);

	iwl_pcie_set_pwr(trans, false);

	iwl_op_mode_nic_config(trans->op_mode);

	/* Allocate the RX queue, or reset if it is already allocated */
	iwl_pcie_rx_init(trans);

	/* Allocate or reset and init all Tx and Command queues */
	if (iwl_pcie_tx_init(trans))
		return -ENOMEM;

	if (trans->cfg->base_params->shadow_reg_enable) {
		/* enable shadow regs in HW */
		iwl_set_bit(trans, CSR_MAC_SHADOW_REG_CTRL, 0x800FFFFF);
		IWL_DEBUG_INFO(trans, "Enabling shadow registers in device\n");
	}

	return 0;
}

#define HW_READY_TIMEOUT (50)

/* Note: returns poll_bit return value, which is >= 0 if success */
static int iwl_pcie_set_hw_ready(struct iwl_trans *trans)
{
	int ret;

	iwl_set_bit(trans, CSR_HW_IF_CONFIG_REG,
		    CSR_HW_IF_CONFIG_REG_BIT_NIC_READY);

	/* See if we got it */
	ret = iwl_poll_bit(trans, CSR_HW_IF_CONFIG_REG,
			   CSR_HW_IF_CONFIG_REG_BIT_NIC_READY,
			   CSR_HW_IF_CONFIG_REG_BIT_NIC_READY,
			   HW_READY_TIMEOUT);

	if (ret >= 0)
		iwl_set_bit(trans, CSR_MBOX_SET_REG, CSR_MBOX_SET_REG_OS_ALIVE);

	IWL_DEBUG_INFO(trans, "hardware%s ready\n", ret < 0 ? " not" : "");
	return ret;
}

/* Note: returns standard 0/-ERROR code */
static int iwl_pcie_prepare_card_hw(struct iwl_trans *trans)
{
	int ret;
	int t = 0;
	int iter;

	IWL_DEBUG_INFO(trans, "iwl_trans_prepare_card_hw enter\n");

	ret = iwl_pcie_set_hw_ready(trans);
	/* If the card is ready, exit 0 */
	if (ret >= 0)
		return 0;

	iwl_set_bit(trans, CSR_DBG_LINK_PWR_MGMT_REG,
		    CSR_RESET_LINK_PWR_MGMT_DISABLED);
	msleep(1);

	for (iter = 0; iter < 10; iter++) {
		/* If HW is not ready, prepare the conditions to check again */
		iwl_set_bit(trans, CSR_HW_IF_CONFIG_REG,
			    CSR_HW_IF_CONFIG_REG_PREPARE);

		do {
			ret = iwl_pcie_set_hw_ready(trans);
			if (ret >= 0)
				return 0;

			usleep_range(200, 1000);
			t += 200;
		} while (t < 150000);
		msleep(25);
	}

	IWL_ERR(trans, "Couldn't prepare the card\n");

	return ret;
}

/*
 * ucode
 */
static int iwl_pcie_load_firmware_chunk(struct iwl_trans *trans, u32 dst_addr,
				   dma_addr_t phy_addr, u32 byte_cnt)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	int ret;

	trans_pcie->ucode_write_complete = false;

	iwl_write_direct32(trans,
			   FH_TCSR_CHNL_TX_CONFIG_REG(FH_SRVC_CHNL),
			   FH_TCSR_TX_CONFIG_REG_VAL_DMA_CHNL_PAUSE);

	iwl_write_direct32(trans,
			   FH_SRVC_CHNL_SRAM_ADDR_REG(FH_SRVC_CHNL),
			   dst_addr);

	iwl_write_direct32(trans,
			   FH_TFDIB_CTRL0_REG(FH_SRVC_CHNL),
			   phy_addr & FH_MEM_TFDIB_DRAM_ADDR_LSB_MSK);

	iwl_write_direct32(trans,
			   FH_TFDIB_CTRL1_REG(FH_SRVC_CHNL),
			   (iwl_get_dma_hi_addr(phy_addr)
				<< FH_MEM_TFDIB_REG1_ADDR_BITSHIFT) | byte_cnt);

	iwl_write_direct32(trans,
			   FH_TCSR_CHNL_TX_BUF_STS_REG(FH_SRVC_CHNL),
			   1 << FH_TCSR_CHNL_TX_BUF_STS_REG_POS_TB_NUM |
			   1 << FH_TCSR_CHNL_TX_BUF_STS_REG_POS_TB_IDX |
			   FH_TCSR_CHNL_TX_BUF_STS_REG_VAL_TFDB_VALID);

	iwl_write_direct32(trans,
			   FH_TCSR_CHNL_TX_CONFIG_REG(FH_SRVC_CHNL),
			   FH_TCSR_TX_CONFIG_REG_VAL_DMA_CHNL_ENABLE	|
			   FH_TCSR_TX_CONFIG_REG_VAL_DMA_CREDIT_DISABLE	|
			   FH_TCSR_TX_CONFIG_REG_VAL_CIRQ_HOST_ENDTFD);

	ret = wait_event_timeout(trans_pcie->ucode_write_waitq,
				 trans_pcie->ucode_write_complete, 5 * HZ);
	if (!ret) {
		IWL_ERR(trans, "Failed to load firmware chunk!\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static int iwl_pcie_load_section(struct iwl_trans *trans, u8 section_num,
			    const struct fw_desc *section)
{
	u8 *v_addr;
	dma_addr_t p_addr;
	u32 offset, chunk_sz = min_t(u32, FH_MEM_TB_MAX_LENGTH, section->len);
	int ret = 0;

	IWL_DEBUG_FW(trans, "[%d] uCode section being loaded...\n",
		     section_num);

	v_addr = dma_alloc_coherent(trans->dev, chunk_sz, &p_addr,
				    GFP_KERNEL | __GFP_NOWARN);
	if (!v_addr) {
		IWL_DEBUG_INFO(trans, "Falling back to small chunks of DMA\n");
		chunk_sz = PAGE_SIZE;
		v_addr = dma_alloc_coherent(trans->dev, chunk_sz,
					    &p_addr, GFP_KERNEL);
		if (!v_addr)
			return -ENOMEM;
	}

	for (offset = 0; offset < section->len; offset += chunk_sz) {
		u32 copy_size, dst_addr;
		bool extended_addr = false;

		copy_size = min_t(u32, chunk_sz, section->len - offset);
		dst_addr = section->offset + offset;

		if (dst_addr >= IWL_FW_MEM_EXTENDED_START &&
		    dst_addr <= IWL_FW_MEM_EXTENDED_END)
			extended_addr = true;

		if (extended_addr)
			iwl_set_bits_prph(trans, LMPM_CHICK,
					  LMPM_CHICK_EXTENDED_ADDR_SPACE);

		memcpy(v_addr, (u8 *)section->data + offset, copy_size);
		ret = iwl_pcie_load_firmware_chunk(trans, dst_addr, p_addr,
						   copy_size);

		if (extended_addr)
			iwl_clear_bits_prph(trans, LMPM_CHICK,
					    LMPM_CHICK_EXTENDED_ADDR_SPACE);

		if (ret) {
			IWL_ERR(trans,
				"Could not load the [%d] uCode section\n",
				section_num);
			break;
		}
	}

	dma_free_coherent(trans->dev, chunk_sz, v_addr, p_addr);
	return ret;
}

/*
 * Driver Takes the ownership on secure machine before FW load
 * and prevent race with the BT load.
 * W/A for ROM bug. (should be remove in the next Si step)
 */
static int iwl_pcie_rsa_race_bug_wa(struct iwl_trans *trans)
{
	u32 val, loop = 1000;

	/*
	 * Check the RSA semaphore is accessible.
	 * If the HW isn't locked and the rsa semaphore isn't accessible,
	 * we are in trouble.
	 */
	val = iwl_read_prph(trans, PREG_AUX_BUS_WPROT_0);
	if (val & (BIT(1) | BIT(17))) {
		IWL_INFO(trans,
			 "can't access the RSA semaphore it is write protected\n");
		return 0;
	}

	/* take ownership on the AUX IF */
	iwl_write_prph(trans, WFPM_CTRL_REG, WFPM_AUX_CTL_AUX_IF_MAC_OWNER_MSK);
	iwl_write_prph(trans, AUX_MISC_MASTER1_EN, AUX_MISC_MASTER1_EN_SBE_MSK);

	do {
		iwl_write_prph(trans, AUX_MISC_MASTER1_SMPHR_STATUS, 0x1);
		val = iwl_read_prph(trans, AUX_MISC_MASTER1_SMPHR_STATUS);
		if (val == 0x1) {
			iwl_write_prph(trans, RSA_ENABLE, 0);
			return 0;
		}

		udelay(10);
		loop--;
	} while (loop > 0);

	IWL_ERR(trans, "Failed to take ownership on secure machine\n");
	return -EIO;
}

static int iwl_pcie_load_cpu_sections_8000(struct iwl_trans *trans,
					   const struct fw_img *image,
					   int cpu,
					   int *first_ucode_section)
{
	int shift_param;
	int i, ret = 0, sec_num = 0x1;
	u32 val, last_read_idx = 0;

	if (cpu == 1) {
		shift_param = 0;
		*first_ucode_section = 0;
	} else {
		shift_param = 16;
		(*first_ucode_section)++;
	}

	for (i = *first_ucode_section; i < IWL_UCODE_SECTION_MAX; i++) {
		last_read_idx = i;

		/*
		 * CPU1_CPU2_SEPARATOR_SECTION delimiter - separate between
		 * CPU1 to CPU2.
		 * PAGING_SEPARATOR_SECTION delimiter - separate between
		 * CPU2 non paged to CPU2 paging sec.
		 */
		if (!image->sec[i].data ||
		    image->sec[i].offset == CPU1_CPU2_SEPARATOR_SECTION ||
		    image->sec[i].offset == PAGING_SEPARATOR_SECTION) {
			IWL_DEBUG_FW(trans,
				     "Break since Data not valid or Empty section, sec = %d\n",
				     i);
			break;
		}

		ret = iwl_pcie_load_section(trans, i, &image->sec[i]);
		if (ret)
			return ret;

		/* Notify the ucode of the loaded section number and status */
		val = iwl_read_direct32(trans, FH_UCODE_LOAD_STATUS);
		val = val | (sec_num << shift_param);
		iwl_write_direct32(trans, FH_UCODE_LOAD_STATUS, val);
		sec_num = (sec_num << 1) | 0x1;
	}

	*first_ucode_section = last_read_idx;

	if (cpu == 1)
		iwl_write_direct32(trans, FH_UCODE_LOAD_STATUS, 0xFFFF);
	else
		iwl_write_direct32(trans, FH_UCODE_LOAD_STATUS, 0xFFFFFFFF);

	return 0;
}

static int iwl_pcie_load_cpu_sections(struct iwl_trans *trans,
				      const struct fw_img *image,
				      int cpu,
				      int *first_ucode_section)
{
	int shift_param;
	int i, ret = 0;
	u32 last_read_idx = 0;

	if (cpu == 1) {
		shift_param = 0;
		*first_ucode_section = 0;
	} else {
		shift_param = 16;
		(*first_ucode_section)++;
	}

	for (i = *first_ucode_section; i < IWL_UCODE_SECTION_MAX; i++) {
		last_read_idx = i;

		/*
		 * CPU1_CPU2_SEPARATOR_SECTION delimiter - separate between
		 * CPU1 to CPU2.
		 * PAGING_SEPARATOR_SECTION delimiter - separate between
		 * CPU2 non paged to CPU2 paging sec.
		 */
		if (!image->sec[i].data ||
		    image->sec[i].offset == CPU1_CPU2_SEPARATOR_SECTION ||
		    image->sec[i].offset == PAGING_SEPARATOR_SECTION) {
			IWL_DEBUG_FW(trans,
				     "Break since Data not valid or Empty section, sec = %d\n",
				     i);
			break;
		}

		ret = iwl_pcie_load_section(trans, i, &image->sec[i]);
		if (ret)
			return ret;
	}

	if (trans->cfg->device_family == IWL_DEVICE_FAMILY_8000)
		iwl_set_bits_prph(trans,
				  CSR_UCODE_LOAD_STATUS_ADDR,
				  (LMPM_CPU_UCODE_LOADING_COMPLETED |
				   LMPM_CPU_HDRS_LOADING_COMPLETED |
				   LMPM_CPU_UCODE_LOADING_STARTED) <<
					shift_param);

	*first_ucode_section = last_read_idx;

	return 0;
}

static void iwl_pcie_apply_destination(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	const struct iwl_fw_dbg_dest_tlv *dest = trans->dbg_dest_tlv;
	int i;

	if (dest->version)
		IWL_ERR(trans,
			"DBG DEST version is %d - expect issues\n",
			dest->version);

	IWL_INFO(trans, "Applying debug destination %s\n",
		 get_fw_dbg_mode_string(dest->monitor_mode));

	if (dest->monitor_mode == EXTERNAL_MODE)
		iwl_pcie_alloc_fw_monitor(trans, dest->size_power);
	else
		IWL_WARN(trans, "PCI should have external buffer debug\n");

	for (i = 0; i < trans->dbg_dest_reg_num; i++) {
		u32 addr = le32_to_cpu(dest->reg_ops[i].addr);
		u32 val = le32_to_cpu(dest->reg_ops[i].val);

		switch (dest->reg_ops[i].op) {
		case CSR_ASSIGN:
			iwl_write32(trans, addr, val);
			break;
		case CSR_SETBIT:
			iwl_set_bit(trans, addr, BIT(val));
			break;
		case CSR_CLEARBIT:
			iwl_clear_bit(trans, addr, BIT(val));
			break;
		case PRPH_ASSIGN:
			iwl_write_prph(trans, addr, val);
			break;
		case PRPH_SETBIT:
			iwl_set_bits_prph(trans, addr, BIT(val));
			break;
		case PRPH_CLEARBIT:
			iwl_clear_bits_prph(trans, addr, BIT(val));
			break;
		case PRPH_BLOCKBIT:
			if (iwl_read_prph(trans, addr) & BIT(val)) {
				IWL_ERR(trans,
					"BIT(%u) in address 0x%x is 1, stopping FW configuration\n",
					val, addr);
				goto monitor;
			}
			break;
		default:
			IWL_ERR(trans, "FW debug - unknown OP %d\n",
				dest->reg_ops[i].op);
			break;
		}
	}

monitor:
	if (dest->monitor_mode == EXTERNAL_MODE && trans_pcie->fw_mon_size) {
		iwl_write_prph(trans, le32_to_cpu(dest->base_reg),
			       trans_pcie->fw_mon_phys >> dest->base_shift);
		if (trans->cfg->device_family == IWL_DEVICE_FAMILY_8000)
			iwl_write_prph(trans, le32_to_cpu(dest->end_reg),
				       (trans_pcie->fw_mon_phys +
					trans_pcie->fw_mon_size - 256) >>
						dest->end_shift);
		else
			iwl_write_prph(trans, le32_to_cpu(dest->end_reg),
				       (trans_pcie->fw_mon_phys +
					trans_pcie->fw_mon_size) >>
						dest->end_shift);
	}
}

static int iwl_pcie_load_given_ucode(struct iwl_trans *trans,
				const struct fw_img *image)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	int ret = 0;
	int first_ucode_section;

	IWL_DEBUG_FW(trans, "working with %s CPU\n",
		     image->is_dual_cpus ? "Dual" : "Single");

	/* load to FW the binary non secured sections of CPU1 */
	ret = iwl_pcie_load_cpu_sections(trans, image, 1, &first_ucode_section);
	if (ret)
		return ret;

	if (image->is_dual_cpus) {
		/* set CPU2 header address */
		iwl_write_prph(trans,
			       LMPM_SECURE_UCODE_LOAD_CPU2_HDR_ADDR,
			       LMPM_SECURE_CPU2_HDR_MEM_SPACE);

		/* load to FW the binary sections of CPU2 */
		ret = iwl_pcie_load_cpu_sections(trans, image, 2,
						 &first_ucode_section);
		if (ret)
			return ret;
	}

	/* supported for 7000 only for the moment */
	if (iwlwifi_mod_params.fw_monitor &&
	    trans->cfg->device_family == IWL_DEVICE_FAMILY_7000) {
		iwl_pcie_alloc_fw_monitor(trans, 0);

		if (trans_pcie->fw_mon_size) {
			iwl_write_prph(trans, MON_BUFF_BASE_ADDR,
				       trans_pcie->fw_mon_phys >> 4);
			iwl_write_prph(trans, MON_BUFF_END_ADDR,
				       (trans_pcie->fw_mon_phys +
					trans_pcie->fw_mon_size) >> 4);
		}
	} else if (trans->dbg_dest_tlv) {
		iwl_pcie_apply_destination(trans);
	}

	/* release CPU reset */
	iwl_write32(trans, CSR_RESET, 0);

	return 0;
}

static int iwl_pcie_load_given_ucode_8000(struct iwl_trans *trans,
					  const struct fw_img *image)
{
	int ret = 0;
	int first_ucode_section;

	IWL_DEBUG_FW(trans, "working with %s CPU\n",
		     image->is_dual_cpus ? "Dual" : "Single");

	if (trans->dbg_dest_tlv)
		iwl_pcie_apply_destination(trans);

	/* TODO: remove in the next Si step */
	ret = iwl_pcie_rsa_race_bug_wa(trans);
	if (ret)
		return ret;

	/* configure the ucode to be ready to get the secured image */
	/* release CPU reset */
	iwl_write_prph(trans, RELEASE_CPU_RESET, RELEASE_CPU_RESET_BIT);

	/* load to FW the binary Secured sections of CPU1 */
	ret = iwl_pcie_load_cpu_sections_8000(trans, image, 1,
					      &first_ucode_section);
	if (ret)
		return ret;

	/* load to FW the binary sections of CPU2 */
	return iwl_pcie_load_cpu_sections_8000(trans, image, 2,
					       &first_ucode_section);
}

static void _iwl_trans_pcie_stop_device(struct iwl_trans *trans, bool low_power)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	bool hw_rfkill, was_hw_rfkill;

	lockdep_assert_held(&trans_pcie->mutex);

	if (trans_pcie->is_down)
		return;

	trans_pcie->is_down = true;

	was_hw_rfkill = iwl_is_rfkill_set(trans);

	/* tell the device to stop sending interrupts */
	spin_lock(&trans_pcie->irq_lock);
	iwl_disable_interrupts(trans);
	spin_unlock(&trans_pcie->irq_lock);

	/* device going down, Stop using ICT table */
	iwl_pcie_disable_ict(trans);

	/*
	 * If a HW restart happens during firmware loading,
	 * then the firmware loading might call this function
	 * and later it might be called again due to the
	 * restart. So don't process again if the device is
	 * already dead.
	 */
	if (test_and_clear_bit(STATUS_DEVICE_ENABLED, &trans->status)) {
		IWL_DEBUG_INFO(trans,
			       "DEVICE_ENABLED bit was set and is now cleared\n");
		iwl_pcie_tx_stop(trans);
		iwl_pcie_rx_stop(trans);

		/* Power-down device's busmaster DMA clocks */
		if (!trans->cfg->apmg_not_supported) {
			iwl_write_prph(trans, APMG_CLK_DIS_REG,
				       APMG_CLK_VAL_DMA_CLK_RQT);
			udelay(5);
		}
	}

	/* Make sure (redundant) we've released our request to stay awake */
	iwl_clear_bit(trans, CSR_GP_CNTRL,
		      CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);

	/* Stop the device, and put it in low power state */
	iwl_pcie_apm_stop(trans, false);

	/* stop and reset the on-board processor */
	iwl_write32(trans, CSR_RESET, CSR_RESET_REG_FLAG_SW_RESET);
	udelay(20);

	/*
	 * Upon stop, the APM issues an interrupt if HW RF kill is set.
	 * This is a bug in certain verions of the hardware.
	 * Certain devices also keep sending HW RF kill interrupt all
	 * the time, unless the interrupt is ACKed even if the interrupt
	 * should be masked. Re-ACK all the interrupts here.
	 */
	spin_lock(&trans_pcie->irq_lock);
	iwl_disable_interrupts(trans);
	spin_unlock(&trans_pcie->irq_lock);

	/* clear all status bits */
	clear_bit(STATUS_SYNC_HCMD_ACTIVE, &trans->status);
	clear_bit(STATUS_INT_ENABLED, &trans->status);
	clear_bit(STATUS_TPOWER_PMI, &trans->status);
	clear_bit(STATUS_RFKILL, &trans->status);

	/*
	 * Even if we stop the HW, we still want the RF kill
	 * interrupt
	 */
	iwl_enable_rfkill_int(trans);

	/*
	 * Check again since the RF kill state may have changed while
	 * all the interrupts were disabled, in this case we couldn't
	 * receive the RF kill interrupt and update the state in the
	 * op_mode.
	 * Don't call the op_mode if the rkfill state hasn't changed.
	 * This allows the op_mode to call stop_device from the rfkill
	 * notification without endless recursion. Under very rare
	 * circumstances, we might have a small recursion if the rfkill
	 * state changed exactly now while we were called from stop_device.
	 * This is very unlikely but can happen and is supported.
	 */
	hw_rfkill = iwl_is_rfkill_set(trans);
	if (hw_rfkill)
		set_bit(STATUS_RFKILL, &trans->status);
	else
		clear_bit(STATUS_RFKILL, &trans->status);
	if (hw_rfkill != was_hw_rfkill)
		iwl_trans_pcie_rf_kill(trans, hw_rfkill);

	/* re-take ownership to prevent other users from stealing the device */
	iwl_pcie_prepare_card_hw(trans);
}

static int iwl_trans_pcie_start_fw(struct iwl_trans *trans,
				   const struct fw_img *fw, bool run_in_rfkill)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	bool hw_rfkill;
	int ret;

	/* This may fail if AMT took ownership of the device */
	if (iwl_pcie_prepare_card_hw(trans)) {
		IWL_WARN(trans, "Exit HW not ready\n");
		ret = -EIO;
		goto out;
	}

	iwl_enable_rfkill_int(trans);

	iwl_write32(trans, CSR_INT, 0xFFFFFFFF);

	/*
	 * We enabled the RF-Kill interrupt and the handler may very
	 * well be running. Disable the interrupts to make sure no other
	 * interrupt can be fired.
	 */
	iwl_disable_interrupts(trans);

	/* Make sure it finished running */
	synchronize_irq(trans_pcie->pci_dev->irq);

	mutex_lock(&trans_pcie->mutex);

	/* If platform's RF_KILL switch is NOT set to KILL */
	hw_rfkill = iwl_is_rfkill_set(trans);
	if (hw_rfkill)
		set_bit(STATUS_RFKILL, &trans->status);
	else
		clear_bit(STATUS_RFKILL, &trans->status);
	iwl_trans_pcie_rf_kill(trans, hw_rfkill);
	if (hw_rfkill && !run_in_rfkill) {
		ret = -ERFKILL;
		goto out;
	}

	/* Someone called stop_device, don't try to start_fw */
	if (trans_pcie->is_down) {
		IWL_WARN(trans,
			 "Can't start_fw since the HW hasn't been started\n");
		ret = -EIO;
		goto out;
	}

	/* make sure rfkill handshake bits are cleared */
	iwl_write32(trans, CSR_UCODE_DRV_GP1_CLR, CSR_UCODE_SW_BIT_RFKILL);
	iwl_write32(trans, CSR_UCODE_DRV_GP1_CLR,
		    CSR_UCODE_DRV_GP1_BIT_CMD_BLOCKED);

	/* clear (again), then enable host interrupts */
	iwl_write32(trans, CSR_INT, 0xFFFFFFFF);

	ret = iwl_pcie_nic_init(trans);
	if (ret) {
		IWL_ERR(trans, "Unable to init nic\n");
		goto out;
	}

	/*
	 * Now, we load the firmware and don't want to be interrupted, even
	 * by the RF-Kill interrupt (hence mask all the interrupt besides the
	 * FH_TX interrupt which is needed to load the firmware). If the
	 * RF-Kill switch is toggled, we will find out after having loaded
	 * the firmware and return the proper value to the caller.
	 */
	iwl_enable_fw_load_int(trans);

	/* really make sure rfkill handshake bits are cleared */
	iwl_write32(trans, CSR_UCODE_DRV_GP1_CLR, CSR_UCODE_SW_BIT_RFKILL);
	iwl_write32(trans, CSR_UCODE_DRV_GP1_CLR, CSR_UCODE_SW_BIT_RFKILL);

	/* Load the given image to the HW */
	if (trans->cfg->device_family == IWL_DEVICE_FAMILY_8000)
		ret = iwl_pcie_load_given_ucode_8000(trans, fw);
	else
		ret = iwl_pcie_load_given_ucode(trans, fw);
	iwl_enable_interrupts(trans);

	/* re-check RF-Kill state since we may have missed the interrupt */
	hw_rfkill = iwl_is_rfkill_set(trans);
	if (hw_rfkill)
		set_bit(STATUS_RFKILL, &trans->status);
	else
		clear_bit(STATUS_RFKILL, &trans->status);

	iwl_trans_pcie_rf_kill(trans, hw_rfkill);
	if (hw_rfkill && !run_in_rfkill)
		ret = -ERFKILL;

out:
	mutex_unlock(&trans_pcie->mutex);
	return ret;
}

static void iwl_trans_pcie_fw_alive(struct iwl_trans *trans, u32 scd_addr)
{
	iwl_pcie_reset_ict(trans);
	iwl_pcie_tx_start(trans, scd_addr);
}

static void iwl_trans_pcie_stop_device(struct iwl_trans *trans, bool low_power)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	mutex_lock(&trans_pcie->mutex);
	_iwl_trans_pcie_stop_device(trans, low_power);
	mutex_unlock(&trans_pcie->mutex);
}

void iwl_trans_pcie_rf_kill(struct iwl_trans *trans, bool state)
{
	struct iwl_trans_pcie __maybe_unused *trans_pcie =
		IWL_TRANS_GET_PCIE_TRANS(trans);

	lockdep_assert_held(&trans_pcie->mutex);

	if (iwl_op_mode_hw_rf_kill(trans->op_mode, state))
		_iwl_trans_pcie_stop_device(trans, true);
}

static void iwl_trans_pcie_d3_suspend(struct iwl_trans *trans, bool test)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	if (trans->system_pm_mode == IWL_PLAT_PM_MODE_D0I3) {
		/* Enable persistence mode to avoid reset */
		iwl_set_bit(trans, CSR_HW_IF_CONFIG_REG,
			    CSR_HW_IF_CONFIG_REG_PERSIST_MODE);
	}

	iwl_disable_interrupts(trans);

	/*
	 * in testing mode, the host stays awake and the
	 * hardware won't be reset (not even partially)
	 */
	if (test)
		return;

	iwl_pcie_disable_ict(trans);

	synchronize_irq(trans_pcie->pci_dev->irq);

	iwl_clear_bit(trans, CSR_GP_CNTRL,
		      CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);
	iwl_clear_bit(trans, CSR_GP_CNTRL,
		      CSR_GP_CNTRL_REG_FLAG_INIT_DONE);

	if (trans->system_pm_mode == IWL_PLAT_PM_MODE_D3) {
		/*
		 * reset TX queues -- some of their registers reset during S3
		 * so if we don't reset everything here the D3 image would try
		 * to execute some invalid memory upon resume
		 */
		iwl_trans_pcie_tx_reset(trans);
	}

	iwl_pcie_set_pwr(trans, true);
}

static int iwl_trans_pcie_d3_resume(struct iwl_trans *trans,
				    enum iwl_d3_status *status,
				    bool test)
{
	u32 val;
	int ret;

	if (test) {
		iwl_enable_interrupts(trans);
		*status = IWL_D3_STATUS_ALIVE;
		return 0;
	}

	/*
	 * Also enables interrupts - none will happen as the device doesn't
	 * know we're waking it up, only when the opmode actually tells it
	 * after this call.
	 */
	iwl_pcie_reset_ict(trans);

	iwl_set_bit(trans, CSR_GP_CNTRL, CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);
	iwl_set_bit(trans, CSR_GP_CNTRL, CSR_GP_CNTRL_REG_FLAG_INIT_DONE);

	if (trans->cfg->device_family == IWL_DEVICE_FAMILY_8000)
		udelay(2);

	ret = iwl_poll_bit(trans, CSR_GP_CNTRL,
			   CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY,
			   CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY,
			   25000);
	if (ret < 0) {
		IWL_ERR(trans, "Failed to resume the device (mac ready)\n");
		return ret;
	}

	iwl_pcie_set_pwr(trans, false);

	if (trans->system_pm_mode == IWL_PLAT_PM_MODE_D0I3) {
		iwl_clear_bit(trans, CSR_GP_CNTRL,
			      CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);
	} else {
		iwl_trans_pcie_tx_reset(trans);

		ret = iwl_pcie_rx_init(trans);
		if (ret) {
			IWL_ERR(trans,
				"Failed to resume the device (RX reset)\n");
			return ret;
		}
	}

	val = iwl_read32(trans, CSR_RESET);
	if (val & CSR_RESET_REG_FLAG_NEVO_RESET)
		*status = IWL_D3_STATUS_RESET;
	else
		*status = IWL_D3_STATUS_ALIVE;

	return 0;
}

static int _iwl_trans_pcie_start_hw(struct iwl_trans *trans, bool low_power)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	bool hw_rfkill;
	int err;

	lockdep_assert_held(&trans_pcie->mutex);

	err = iwl_pcie_prepare_card_hw(trans);
	if (err) {
		IWL_ERR(trans, "Error while preparing HW: %d\n", err);
		return err;
	}

	/* Reset the entire device */
	iwl_write32(trans, CSR_RESET, CSR_RESET_REG_FLAG_SW_RESET);

	usleep_range(10, 15);

	iwl_pcie_apm_init(trans);

	/* From now on, the op_mode will be kept updated about RF kill state */
	iwl_enable_rfkill_int(trans);

	/* Set is_down to false here so that...*/
	trans_pcie->is_down = false;

	hw_rfkill = iwl_is_rfkill_set(trans);
	if (hw_rfkill)
		set_bit(STATUS_RFKILL, &trans->status);
	else
		clear_bit(STATUS_RFKILL, &trans->status);
	/* ... rfkill can call stop_device and set it false if needed */
	iwl_trans_pcie_rf_kill(trans, hw_rfkill);

	return 0;
}

static int iwl_trans_pcie_start_hw(struct iwl_trans *trans, bool low_power)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	int ret;

	mutex_lock(&trans_pcie->mutex);
	ret = _iwl_trans_pcie_start_hw(trans, low_power);
	mutex_unlock(&trans_pcie->mutex);

	return ret;
}

static void iwl_trans_pcie_op_mode_leave(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	mutex_lock(&trans_pcie->mutex);

	/* disable interrupts - don't enable HW RF kill interrupt */
	spin_lock(&trans_pcie->irq_lock);
	iwl_disable_interrupts(trans);
	spin_unlock(&trans_pcie->irq_lock);

	iwl_pcie_apm_stop(trans, true);

	spin_lock(&trans_pcie->irq_lock);
	iwl_disable_interrupts(trans);
	spin_unlock(&trans_pcie->irq_lock);

	iwl_pcie_disable_ict(trans);

	mutex_unlock(&trans_pcie->mutex);

	synchronize_irq(trans_pcie->pci_dev->irq);
}

static void iwl_trans_pcie_write8(struct iwl_trans *trans, u32 ofs, u8 val)
{
	writeb(val, IWL_TRANS_GET_PCIE_TRANS(trans)->hw_base + ofs);
}

static void iwl_trans_pcie_write32(struct iwl_trans *trans, u32 ofs, u32 val)
{
	writel(val, IWL_TRANS_GET_PCIE_TRANS(trans)->hw_base + ofs);
}

static u32 iwl_trans_pcie_read32(struct iwl_trans *trans, u32 ofs)
{
	return readl(IWL_TRANS_GET_PCIE_TRANS(trans)->hw_base + ofs);
}

static u32 iwl_trans_pcie_read_prph(struct iwl_trans *trans, u32 reg)
{
	iwl_trans_pcie_write32(trans, HBUS_TARG_PRPH_RADDR,
			       ((reg & 0x000FFFFF) | (3 << 24)));
	return iwl_trans_pcie_read32(trans, HBUS_TARG_PRPH_RDAT);
}

static void iwl_trans_pcie_write_prph(struct iwl_trans *trans, u32 addr,
				      u32 val)
{
	iwl_trans_pcie_write32(trans, HBUS_TARG_PRPH_WADDR,
			       ((addr & 0x000FFFFF) | (3 << 24)));
	iwl_trans_pcie_write32(trans, HBUS_TARG_PRPH_WDAT, val);
}

static int iwl_pcie_dummy_napi_poll(struct napi_struct *napi, int budget)
{
	WARN_ON(1);
	return 0;
}

static void iwl_trans_pcie_configure(struct iwl_trans *trans,
				     const struct iwl_trans_config *trans_cfg)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	trans_pcie->cmd_queue = trans_cfg->cmd_queue;
	trans_pcie->cmd_fifo = trans_cfg->cmd_fifo;
	trans_pcie->cmd_q_wdg_timeout = trans_cfg->cmd_q_wdg_timeout;
	if (WARN_ON(trans_cfg->n_no_reclaim_cmds > MAX_NO_RECLAIM_CMDS))
		trans_pcie->n_no_reclaim_cmds = 0;
	else
		trans_pcie->n_no_reclaim_cmds = trans_cfg->n_no_reclaim_cmds;
	if (trans_pcie->n_no_reclaim_cmds)
		memcpy(trans_pcie->no_reclaim_cmds, trans_cfg->no_reclaim_cmds,
		       trans_pcie->n_no_reclaim_cmds * sizeof(u8));

	trans_pcie->rx_buf_size = trans_cfg->rx_buf_size;
	trans_pcie->rx_page_order =
		iwl_trans_get_rb_size_order(trans_pcie->rx_buf_size);

	trans_pcie->wide_cmd_header = trans_cfg->wide_cmd_header;
	trans_pcie->bc_table_dword = trans_cfg->bc_table_dword;
	trans_pcie->scd_set_active = trans_cfg->scd_set_active;
	trans_pcie->sw_csum_tx = trans_cfg->sw_csum_tx;

	trans->command_groups = trans_cfg->command_groups;
	trans->command_groups_size = trans_cfg->command_groups_size;

	/* init ref_count to 1 (should be cleared when ucode is loaded) */
	trans_pcie->ref_count = 1;

	/* Initialize NAPI here - it should be before registering to mac80211
	 * in the opmode but after the HW struct is allocated.
	 * As this function may be called again in some corner cases don't
	 * do anything if NAPI was already initialized.
	 */
	if (!trans_pcie->napi.poll) {
		init_dummy_netdev(&trans_pcie->napi_dev);
		netif_napi_add(&trans_pcie->napi_dev, &trans_pcie->napi,
			       iwl_pcie_dummy_napi_poll, 64);
	}
}

void iwl_trans_pcie_free(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	int i;

	synchronize_irq(trans_pcie->pci_dev->irq);

	iwl_pcie_tx_free(trans);
	iwl_pcie_rx_free(trans);

	free_irq(trans_pcie->pci_dev->irq, trans);
	iwl_pcie_free_ict(trans);

	pci_disable_msi(trans_pcie->pci_dev);
	iounmap(trans_pcie->hw_base);
	pci_release_regions(trans_pcie->pci_dev);
	pci_disable_device(trans_pcie->pci_dev);

	if (trans_pcie->napi.poll)
		netif_napi_del(&trans_pcie->napi);

	iwl_pcie_free_fw_monitor(trans);

	for_each_possible_cpu(i) {
		struct iwl_tso_hdr_page *p =
			per_cpu_ptr(trans_pcie->tso_hdr_page, i);

		if (p->page)
			__free_page(p->page);
	}

	free_percpu(trans_pcie->tso_hdr_page);
	iwl_trans_free(trans);
}

static void iwl_trans_pcie_set_pmi(struct iwl_trans *trans, bool state)
{
	if (state)
		set_bit(STATUS_TPOWER_PMI, &trans->status);
	else
		clear_bit(STATUS_TPOWER_PMI, &trans->status);
}

static bool iwl_trans_pcie_grab_nic_access(struct iwl_trans *trans,
					   unsigned long *flags)
{
	int ret;
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	spin_lock_irqsave(&trans_pcie->reg_lock, *flags);

	if (trans_pcie->cmd_hold_nic_awake)
		goto out;

	/* this bit wakes up the NIC */
	__iwl_trans_pcie_set_bit(trans, CSR_GP_CNTRL,
				 CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);
	if (trans->cfg->device_family == IWL_DEVICE_FAMILY_8000)
		udelay(2);

	/*
	 * These bits say the device is running, and should keep running for
	 * at least a short while (at least as long as MAC_ACCESS_REQ stays 1),
	 * but they do not indicate that embedded SRAM is restored yet;
	 * 3945 and 4965 have volatile SRAM, and must save/restore contents
	 * to/from host DRAM when sleeping/waking for power-saving.
	 * Each direction takes approximately 1/4 millisecond; with this
	 * overhead, it's a good idea to grab and hold MAC_ACCESS_REQUEST if a
	 * series of register accesses are expected (e.g. reading Event Log),
	 * to keep device from sleeping.
	 *
	 * CSR_UCODE_DRV_GP1 register bit MAC_SLEEP == 0 indicates that
	 * SRAM is okay/restored.  We don't check that here because this call
	 * is just for hardware register access; but GP1 MAC_SLEEP check is a
	 * good idea before accessing 3945/4965 SRAM (e.g. reading Event Log).
	 *
	 * 5000 series and later (including 1000 series) have non-volatile SRAM,
	 * and do not save/restore SRAM when power cycling.
	 */
	ret = iwl_poll_bit(trans, CSR_GP_CNTRL,
			   CSR_GP_CNTRL_REG_VAL_MAC_ACCESS_EN,
			   (CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY |
			    CSR_GP_CNTRL_REG_FLAG_GOING_TO_SLEEP), 15000);
	if (unlikely(ret < 0)) {
		iwl_write32(trans, CSR_RESET, CSR_RESET_REG_FLAG_FORCE_NMI);
		WARN_ONCE(1,
			  "Timeout waiting for hardware access (CSR_GP_CNTRL 0x%08x)\n",
			  iwl_read32(trans, CSR_GP_CNTRL));
		spin_unlock_irqrestore(&trans_pcie->reg_lock, *flags);
		return false;
	}

out:
	/*
	 * Fool sparse by faking we release the lock - sparse will
	 * track nic_access anyway.
	 */
	__release(&trans_pcie->reg_lock);
	return true;
}

static void iwl_trans_pcie_release_nic_access(struct iwl_trans *trans,
					      unsigned long *flags)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	lockdep_assert_held(&trans_pcie->reg_lock);

	/*
	 * Fool sparse by faking we acquiring the lock - sparse will
	 * track nic_access anyway.
	 */
	__acquire(&trans_pcie->reg_lock);

	if (trans_pcie->cmd_hold_nic_awake)
		goto out;

	__iwl_trans_pcie_clear_bit(trans, CSR_GP_CNTRL,
				   CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);
	/*
	 * Above we read the CSR_GP_CNTRL register, which will flush
	 * any previous writes, but we need the write that clears the
	 * MAC_ACCESS_REQ bit to be performed before any other writes
	 * scheduled on different CPUs (after we drop reg_lock).
	 */
	mmiowb();
out:
	spin_unlock_irqrestore(&trans_pcie->reg_lock, *flags);
}

static int iwl_trans_pcie_read_mem(struct iwl_trans *trans, u32 addr,
				   void *buf, int dwords)
{
	unsigned long flags;
	int offs, ret = 0;
	u32 *vals = buf;

	if (iwl_trans_grab_nic_access(trans, &flags)) {
		iwl_write32(trans, HBUS_TARG_MEM_RADDR, addr);
		for (offs = 0; offs < dwords; offs++)
			vals[offs] = iwl_read32(trans, HBUS_TARG_MEM_RDAT);
		iwl_trans_release_nic_access(trans, &flags);
	} else {
		ret = -EBUSY;
	}
	return ret;
}

static int iwl_trans_pcie_write_mem(struct iwl_trans *trans, u32 addr,
				    const void *buf, int dwords)
{
	unsigned long flags;
	int offs, ret = 0;
	const u32 *vals = buf;

	if (iwl_trans_grab_nic_access(trans, &flags)) {
		iwl_write32(trans, HBUS_TARG_MEM_WADDR, addr);
		for (offs = 0; offs < dwords; offs++)
			iwl_write32(trans, HBUS_TARG_MEM_WDAT,
				    vals ? vals[offs] : 0);
		iwl_trans_release_nic_access(trans, &flags);
	} else {
		ret = -EBUSY;
	}
	return ret;
}

static void iwl_trans_pcie_freeze_txq_timer(struct iwl_trans *trans,
					    unsigned long txqs,
					    bool freeze)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	int queue;

	for_each_set_bit(queue, &txqs, BITS_PER_LONG) {
		struct iwl_txq *txq = &trans_pcie->txq[queue];
		unsigned long now;

		spin_lock_bh(&txq->lock);

		now = jiffies;

		if (txq->frozen == freeze)
			goto next_queue;

		IWL_DEBUG_TX_QUEUES(trans, "%s TXQ %d\n",
				    freeze ? "Freezing" : "Waking", queue);

		txq->frozen = freeze;

		if (txq->q.read_ptr == txq->q.write_ptr)
			goto next_queue;

		if (freeze) {
			if (unlikely(time_after(now,
						txq->stuck_timer.expires))) {
				/*
				 * The timer should have fired, maybe it is
				 * spinning right now on the lock.
				 */
				goto next_queue;
			}
			/* remember how long until the timer fires */
			txq->frozen_expiry_remainder =
				txq->stuck_timer.expires - now;
			del_timer(&txq->stuck_timer);
			goto next_queue;
		}

		/*
		 * Wake a non-empty queue -> arm timer with the
		 * remainder before it froze
		 */
		mod_timer(&txq->stuck_timer,
			  now + txq->frozen_expiry_remainder);

next_queue:
		spin_unlock_bh(&txq->lock);
	}
}

static void iwl_trans_pcie_block_txq_ptrs(struct iwl_trans *trans, bool block)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	int i;

	for (i = 0; i < trans->cfg->base_params->num_of_queues; i++) {
		struct iwl_txq *txq = &trans_pcie->txq[i];

		if (i == trans_pcie->cmd_queue)
			continue;

		spin_lock_bh(&txq->lock);

		if (!block && !(WARN_ON_ONCE(!txq->block))) {
			txq->block--;
			if (!txq->block) {
				iwl_write32(trans, HBUS_TARG_WRPTR,
					    txq->q.write_ptr | (i << 8));
			}
		} else if (block) {
			txq->block++;
		}

		spin_unlock_bh(&txq->lock);
	}
}

#define IWL_FLUSH_WAIT_MS	2000

static int iwl_trans_pcie_wait_txq_empty(struct iwl_trans *trans, u32 txq_bm)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_txq *txq;
	struct iwl_queue *q;
	int cnt;
	unsigned long now = jiffies;
	u32 scd_sram_addr;
	u8 buf[16];
	int ret = 0;

	/* waiting for all the tx frames complete might take a while */
	for (cnt = 0; cnt < trans->cfg->base_params->num_of_queues; cnt++) {
		u8 wr_ptr;

		if (cnt == trans_pcie->cmd_queue)
			continue;
		if (!test_bit(cnt, trans_pcie->queue_used))
			continue;
		if (!(BIT(cnt) & txq_bm))
			continue;

		IWL_DEBUG_TX_QUEUES(trans, "Emptying queue %d...\n", cnt);
		txq = &trans_pcie->txq[cnt];
		q = &txq->q;
		wr_ptr = ACCESS_ONCE(q->write_ptr);

		while (q->read_ptr != ACCESS_ONCE(q->write_ptr) &&
		       !time_after(jiffies,
				   now + msecs_to_jiffies(IWL_FLUSH_WAIT_MS))) {
			u8 write_ptr = ACCESS_ONCE(q->write_ptr);

			if (WARN_ONCE(wr_ptr != write_ptr,
				      "WR pointer moved while flushing %d -> %d\n",
				      wr_ptr, write_ptr))
				return -ETIMEDOUT;
			msleep(1);
		}

		if (q->read_ptr != q->write_ptr) {
			IWL_ERR(trans,
				"fail to flush all tx fifo queues Q %d\n", cnt);
			ret = -ETIMEDOUT;
			break;
		}
		IWL_DEBUG_TX_QUEUES(trans, "Queue %d is now empty.\n", cnt);
	}

	if (!ret)
		return 0;

	IWL_ERR(trans, "Current SW read_ptr %d write_ptr %d\n",
		txq->q.read_ptr, txq->q.write_ptr);

	scd_sram_addr = trans_pcie->scd_base_addr +
			SCD_TX_STTS_QUEUE_OFFSET(txq->q.id);
	iwl_trans_read_mem_bytes(trans, scd_sram_addr, buf, sizeof(buf));

	iwl_print_hex_error(trans, buf, sizeof(buf));

	for (cnt = 0; cnt < FH_TCSR_CHNL_NUM; cnt++)
		IWL_ERR(trans, "FH TRBs(%d) = 0x%08x\n", cnt,
			iwl_read_direct32(trans, FH_TX_TRB_REG(cnt)));

	for (cnt = 0; cnt < trans->cfg->base_params->num_of_queues; cnt++) {
		u32 status = iwl_read_prph(trans, SCD_QUEUE_STATUS_BITS(cnt));
		u8 fifo = (status >> SCD_QUEUE_STTS_REG_POS_TXF) & 0x7;
		bool active = !!(status & BIT(SCD_QUEUE_STTS_REG_POS_ACTIVE));
		u32 tbl_dw =
			iwl_trans_read_mem32(trans, trans_pcie->scd_base_addr +
					     SCD_TRANS_TBL_OFFSET_QUEUE(cnt));

		if (cnt & 0x1)
			tbl_dw = (tbl_dw & 0xFFFF0000) >> 16;
		else
			tbl_dw = tbl_dw & 0x0000FFFF;

		IWL_ERR(trans,
			"Q %d is %sactive and mapped to fifo %d ra_tid 0x%04x [%d,%d]\n",
			cnt, active ? "" : "in", fifo, tbl_dw,
			iwl_read_prph(trans, SCD_QUEUE_RDPTR(cnt)) &
				(TFD_QUEUE_SIZE_MAX - 1),
			iwl_read_prph(trans, SCD_QUEUE_WRPTR(cnt)));
	}

	return ret;
}

static void iwl_trans_pcie_set_bits_mask(struct iwl_trans *trans, u32 reg,
					 u32 mask, u32 value)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	unsigned long flags;

	spin_lock_irqsave(&trans_pcie->reg_lock, flags);
	__iwl_trans_pcie_set_bits_mask(trans, reg, mask, value);
	spin_unlock_irqrestore(&trans_pcie->reg_lock, flags);
}

void iwl_trans_pcie_ref(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	unsigned long flags;

	if (iwlwifi_mod_params.d0i3_disable)
		return;

	spin_lock_irqsave(&trans_pcie->ref_lock, flags);
	IWL_DEBUG_RPM(trans, "ref_counter: %d\n", trans_pcie->ref_count);
	trans_pcie->ref_count++;
	spin_unlock_irqrestore(&trans_pcie->ref_lock, flags);
}

void iwl_trans_pcie_unref(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	unsigned long flags;

	if (iwlwifi_mod_params.d0i3_disable)
		return;

	spin_lock_irqsave(&trans_pcie->ref_lock, flags);
	IWL_DEBUG_RPM(trans, "ref_counter: %d\n", trans_pcie->ref_count);
	if (WARN_ON_ONCE(trans_pcie->ref_count == 0)) {
		spin_unlock_irqrestore(&trans_pcie->ref_lock, flags);
		return;
	}
	trans_pcie->ref_count--;
	spin_unlock_irqrestore(&trans_pcie->ref_lock, flags);
}

static const char *get_csr_string(int cmd)
{
#define IWL_CMD(x) case x: return #x
	switch (cmd) {
	IWL_CMD(CSR_HW_IF_CONFIG_REG);
	IWL_CMD(CSR_INT_COALESCING);
	IWL_CMD(CSR_INT);
	IWL_CMD(CSR_INT_MASK);
	IWL_CMD(CSR_FH_INT_STATUS);
	IWL_CMD(CSR_GPIO_IN);
	IWL_CMD(CSR_RESET);
	IWL_CMD(CSR_GP_CNTRL);
	IWL_CMD(CSR_HW_REV);
	IWL_CMD(CSR_EEPROM_REG);
	IWL_CMD(CSR_EEPROM_GP);
	IWL_CMD(CSR_OTP_GP_REG);
	IWL_CMD(CSR_GIO_REG);
	IWL_CMD(CSR_GP_UCODE_REG);
	IWL_CMD(CSR_GP_DRIVER_REG);
	IWL_CMD(CSR_UCODE_DRV_GP1);
	IWL_CMD(CSR_UCODE_DRV_GP2);
	IWL_CMD(CSR_LED_REG);
	IWL_CMD(CSR_DRAM_INT_TBL_REG);
	IWL_CMD(CSR_GIO_CHICKEN_BITS);
	IWL_CMD(CSR_ANA_PLL_CFG);
	IWL_CMD(CSR_HW_REV_WA_REG);
	IWL_CMD(CSR_MONITOR_STATUS_REG);
	IWL_CMD(CSR_DBG_HPET_MEM_REG);
	default:
		return "UNKNOWN";
	}
#undef IWL_CMD
}

void iwl_pcie_dump_csr(struct iwl_trans *trans)
{
	int i;
	static const u32 csr_tbl[] = {
		CSR_HW_IF_CONFIG_REG,
		CSR_INT_COALESCING,
		CSR_INT,
		CSR_INT_MASK,
		CSR_FH_INT_STATUS,
		CSR_GPIO_IN,
		CSR_RESET,
		CSR_GP_CNTRL,
		CSR_HW_REV,
		CSR_EEPROM_REG,
		CSR_EEPROM_GP,
		CSR_OTP_GP_REG,
		CSR_GIO_REG,
		CSR_GP_UCODE_REG,
		CSR_GP_DRIVER_REG,
		CSR_UCODE_DRV_GP1,
		CSR_UCODE_DRV_GP2,
		CSR_LED_REG,
		CSR_DRAM_INT_TBL_REG,
		CSR_GIO_CHICKEN_BITS,
		CSR_ANA_PLL_CFG,
		CSR_MONITOR_STATUS_REG,
		CSR_HW_REV_WA_REG,
		CSR_DBG_HPET_MEM_REG
	};
	IWL_ERR(trans, "CSR values:\n");
	IWL_ERR(trans, "(2nd byte of CSR_INT_COALESCING is "
		"CSR_INT_PERIODIC_REG)\n");
	for (i = 0; i <  ARRAY_SIZE(csr_tbl); i++) {
		IWL_ERR(trans, "  %25s: 0X%08x\n",
			get_csr_string(csr_tbl[i]),
			iwl_read32(trans, csr_tbl[i]));
	}
}

#ifdef CONFIG_IWLWIFI_DEBUGFS
/* create and remove of files */
#define DEBUGFS_ADD_FILE(name, parent, mode) do {			\
	if (!debugfs_create_file(#name, mode, parent, trans,		\
				 &iwl_dbgfs_##name##_ops))		\
		goto err;						\
} while (0)

/* file operation */
#define DEBUGFS_READ_FILE_OPS(name)					\
static const struct file_operations iwl_dbgfs_##name##_ops = {		\
	.read = iwl_dbgfs_##name##_read,				\
	.open = simple_open,						\
	.llseek = generic_file_llseek,					\
};

#define DEBUGFS_WRITE_FILE_OPS(name)                                    \
static const struct file_operations iwl_dbgfs_##name##_ops = {          \
	.write = iwl_dbgfs_##name##_write,                              \
	.open = simple_open,						\
	.llseek = generic_file_llseek,					\
};

#define DEBUGFS_READ_WRITE_FILE_OPS(name)				\
static const struct file_operations iwl_dbgfs_##name##_ops = {		\
	.write = iwl_dbgfs_##name##_write,				\
	.read = iwl_dbgfs_##name##_read,				\
	.open = simple_open,						\
	.llseek = generic_file_llseek,					\
};

static ssize_t iwl_dbgfs_tx_queue_read(struct file *file,
				       char __user *user_buf,
				       size_t count, loff_t *ppos)
{
	struct iwl_trans *trans = file->private_data;
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_txq *txq;
	struct iwl_queue *q;
	char *buf;
	int pos = 0;
	int cnt;
	int ret;
	size_t bufsz;

	bufsz = sizeof(char) * 75 * trans->cfg->base_params->num_of_queues;

	if (!trans_pcie->txq)
		return -EAGAIN;

	buf = kzalloc(bufsz, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	for (cnt = 0; cnt < trans->cfg->base_params->num_of_queues; cnt++) {
		txq = &trans_pcie->txq[cnt];
		q = &txq->q;
		pos += scnprintf(buf + pos, bufsz - pos,
				"hwq %.2d: read=%u write=%u use=%d stop=%d need_update=%d frozen=%d%s\n",
				cnt, q->read_ptr, q->write_ptr,
				!!test_bit(cnt, trans_pcie->queue_used),
				 !!test_bit(cnt, trans_pcie->queue_stopped),
				 txq->need_update, txq->frozen,
				 (cnt == trans_pcie->cmd_queue ? " HCMD" : ""));
	}
	ret = simple_read_from_buffer(user_buf, count, ppos, buf, pos);
	kfree(buf);
	return ret;
}

static ssize_t iwl_dbgfs_rx_queue_read(struct file *file,
				       char __user *user_buf,
				       size_t count, loff_t *ppos)
{
	struct iwl_trans *trans = file->private_data;
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_rxq *rxq = &trans_pcie->rxq;
	char buf[256];
	int pos = 0;
	const size_t bufsz = sizeof(buf);

	pos += scnprintf(buf + pos, bufsz - pos, "read: %u\n",
						rxq->read);
	pos += scnprintf(buf + pos, bufsz - pos, "write: %u\n",
						rxq->write);
	pos += scnprintf(buf + pos, bufsz - pos, "write_actual: %u\n",
						rxq->write_actual);
	pos += scnprintf(buf + pos, bufsz - pos, "need_update: %d\n",
						rxq->need_update);
	pos += scnprintf(buf + pos, bufsz - pos, "free_count: %u\n",
						rxq->free_count);
	if (rxq->rb_stts) {
		pos += scnprintf(buf + pos, bufsz - pos, "closed_rb_num: %u\n",
			 le16_to_cpu(rxq->rb_stts->closed_rb_num) &  0x0FFF);
	} else {
		pos += scnprintf(buf + pos, bufsz - pos,
					"closed_rb_num: Not Allocated\n");
	}
	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

static ssize_t iwl_dbgfs_interrupt_read(struct file *file,
					char __user *user_buf,
					size_t count, loff_t *ppos)
{
	struct iwl_trans *trans = file->private_data;
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct isr_statistics *isr_stats = &trans_pcie->isr_stats;

	int pos = 0;
	char *buf;
	int bufsz = 24 * 64; /* 24 items * 64 char per item */
	ssize_t ret;

	buf = kzalloc(bufsz, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	pos += scnprintf(buf + pos, bufsz - pos,
			"Interrupt Statistics Report:\n");

	pos += scnprintf(buf + pos, bufsz - pos, "HW Error:\t\t\t %u\n",
		isr_stats->hw);
	pos += scnprintf(buf + pos, bufsz - pos, "SW Error:\t\t\t %u\n",
		isr_stats->sw);
	if (isr_stats->sw || isr_stats->hw) {
		pos += scnprintf(buf + pos, bufsz - pos,
			"\tLast Restarting Code:  0x%X\n",
			isr_stats->err_code);
	}
#ifdef CONFIG_IWLWIFI_DEBUG
	pos += scnprintf(buf + pos, bufsz - pos, "Frame transmitted:\t\t %u\n",
		isr_stats->sch);
	pos += scnprintf(buf + pos, bufsz - pos, "Alive interrupt:\t\t %u\n",
		isr_stats->alive);
#endif
	pos += scnprintf(buf + pos, bufsz - pos,
		"HW RF KILL switch toggled:\t %u\n", isr_stats->rfkill);

	pos += scnprintf(buf + pos, bufsz - pos, "CT KILL:\t\t\t %u\n",
		isr_stats->ctkill);

	pos += scnprintf(buf + pos, bufsz - pos, "Wakeup Interrupt:\t\t %u\n",
		isr_stats->wakeup);

	pos += scnprintf(buf + pos, bufsz - pos,
		"Rx command responses:\t\t %u\n", isr_stats->rx);

	pos += scnprintf(buf + pos, bufsz - pos, "Tx/FH interrupt:\t\t %u\n",
		isr_stats->tx);

	pos += scnprintf(buf + pos, bufsz - pos, "Unexpected INTA:\t\t %u\n",
		isr_stats->unhandled);

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, pos);
	kfree(buf);
	return ret;
}

static ssize_t iwl_dbgfs_interrupt_write(struct file *file,
					 const char __user *user_buf,
					 size_t count, loff_t *ppos)
{
	struct iwl_trans *trans = file->private_data;
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct isr_statistics *isr_stats = &trans_pcie->isr_stats;

	char buf[8];
	int buf_size;
	u32 reset_flag;

	memset(buf, 0, sizeof(buf));
	buf_size = min(count, sizeof(buf) -  1);
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;
	if (sscanf(buf, "%x", &reset_flag) != 1)
		return -EFAULT;
	if (reset_flag == 0)
		memset(isr_stats, 0, sizeof(*isr_stats));

	return count;
}

static ssize_t iwl_dbgfs_csr_write(struct file *file,
				   const char __user *user_buf,
				   size_t count, loff_t *ppos)
{
	struct iwl_trans *trans = file->private_data;
	char buf[8];
	int buf_size;
	int csr;

	memset(buf, 0, sizeof(buf));
	buf_size = min(count, sizeof(buf) -  1);
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;
	if (sscanf(buf, "%d", &csr) != 1)
		return -EFAULT;

	iwl_pcie_dump_csr(trans);

	return count;
}

static ssize_t iwl_dbgfs_fh_reg_read(struct file *file,
				     char __user *user_buf,
				     size_t count, loff_t *ppos)
{
	struct iwl_trans *trans = file->private_data;
	char *buf = NULL;
	ssize_t ret;

	ret = iwl_dump_fh(trans, &buf);
	if (ret < 0)
		return ret;
	if (!buf)
		return -EINVAL;
	ret = simple_read_from_buffer(user_buf, count, ppos, buf, ret);
	kfree(buf);
	return ret;
}

DEBUGFS_READ_WRITE_FILE_OPS(interrupt);
DEBUGFS_READ_FILE_OPS(fh_reg);
DEBUGFS_READ_FILE_OPS(rx_queue);
DEBUGFS_READ_FILE_OPS(tx_queue);
DEBUGFS_WRITE_FILE_OPS(csr);

/* Create the debugfs files and directories */
int iwl_trans_pcie_dbgfs_register(struct iwl_trans *trans)
{
	struct dentry *dir = trans->dbgfs_dir;

	DEBUGFS_ADD_FILE(rx_queue, dir, S_IRUSR);
	DEBUGFS_ADD_FILE(tx_queue, dir, S_IRUSR);
	DEBUGFS_ADD_FILE(interrupt, dir, S_IWUSR | S_IRUSR);
	DEBUGFS_ADD_FILE(csr, dir, S_IWUSR);
	DEBUGFS_ADD_FILE(fh_reg, dir, S_IRUSR);
	return 0;

err:
	IWL_ERR(trans, "failed to create the trans debugfs entry\n");
	return -ENOMEM;
}
#endif /*CONFIG_IWLWIFI_DEBUGFS */

static u32 iwl_trans_pcie_get_cmdlen(struct iwl_tfd *tfd)
{
	u32 cmdlen = 0;
	int i;

	for (i = 0; i < IWL_NUM_OF_TBS; i++)
		cmdlen += iwl_pcie_tfd_tb_get_len(tfd, i);

	return cmdlen;
}

static u32 iwl_trans_pcie_dump_rbs(struct iwl_trans *trans,
				   struct iwl_fw_error_dump_data **data,
				   int allocated_rb_nums)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	int max_len = PAGE_SIZE << trans_pcie->rx_page_order;
	struct iwl_rxq *rxq = &trans_pcie->rxq;
	u32 i, r, j, rb_len = 0;

	spin_lock(&rxq->lock);

	r = le16_to_cpu(ACCESS_ONCE(rxq->rb_stts->closed_rb_num)) & 0x0FFF;

	for (i = rxq->read, j = 0;
	     i != r && j < allocated_rb_nums;
	     i = (i + 1) & RX_QUEUE_MASK, j++) {
		struct iwl_rx_mem_buffer *rxb = rxq->queue[i];
		struct iwl_fw_error_dump_rb *rb;

		dma_unmap_page(trans->dev, rxb->page_dma, max_len,
			       DMA_FROM_DEVICE);

		rb_len += sizeof(**data) + sizeof(*rb) + max_len;

		(*data)->type = cpu_to_le32(IWL_FW_ERROR_DUMP_RB);
		(*data)->len = cpu_to_le32(sizeof(*rb) + max_len);
		rb = (void *)(*data)->data;
		rb->index = cpu_to_le32(i);
		memcpy(rb->data, page_address(rxb->page), max_len);
		/* remap the page for the free benefit */
		rxb->page_dma = dma_map_page(trans->dev, rxb->page, 0,
						     max_len,
						     DMA_FROM_DEVICE);

		*data = iwl_fw_error_next_data(*data);
	}

	spin_unlock(&rxq->lock);

	return rb_len;
}
#define IWL_CSR_TO_DUMP (0x250)

static u32 iwl_trans_pcie_dump_csr(struct iwl_trans *trans,
				   struct iwl_fw_error_dump_data **data)
{
	u32 csr_len = sizeof(**data) + IWL_CSR_TO_DUMP;
	__le32 *val;
	int i;

	(*data)->type = cpu_to_le32(IWL_FW_ERROR_DUMP_CSR);
	(*data)->len = cpu_to_le32(IWL_CSR_TO_DUMP);
	val = (void *)(*data)->data;

	for (i = 0; i < IWL_CSR_TO_DUMP; i += 4)
		*val++ = cpu_to_le32(iwl_trans_pcie_read32(trans, i));

	*data = iwl_fw_error_next_data(*data);

	return csr_len;
}

static u32 iwl_trans_pcie_fh_regs_dump(struct iwl_trans *trans,
				       struct iwl_fw_error_dump_data **data)
{
	u32 fh_regs_len = FH_MEM_UPPER_BOUND - FH_MEM_LOWER_BOUND;
	unsigned long flags;
	__le32 *val;
	int i;

	if (!iwl_trans_grab_nic_access(trans, &flags))
		return 0;

	(*data)->type = cpu_to_le32(IWL_FW_ERROR_DUMP_FH_REGS);
	(*data)->len = cpu_to_le32(fh_regs_len);
	val = (void *)(*data)->data;

	for (i = FH_MEM_LOWER_BOUND; i < FH_MEM_UPPER_BOUND; i += sizeof(u32))
		*val++ = cpu_to_le32(iwl_trans_pcie_read32(trans, i));

	iwl_trans_release_nic_access(trans, &flags);

	*data = iwl_fw_error_next_data(*data);

	return sizeof(**data) + fh_regs_len;
}

static u32
iwl_trans_pci_dump_marbh_monitor(struct iwl_trans *trans,
				 struct iwl_fw_error_dump_fw_mon *fw_mon_data,
				 u32 monitor_len)
{
	u32 buf_size_in_dwords = (monitor_len >> 2);
	u32 *buffer = (u32 *)fw_mon_data->data;
	unsigned long flags;
	u32 i;

	if (!iwl_trans_grab_nic_access(trans, &flags))
		return 0;

	iwl_write_prph_no_grab(trans, MON_DMARB_RD_CTL_ADDR, 0x1);
	for (i = 0; i < buf_size_in_dwords; i++)
		buffer[i] = iwl_read_prph_no_grab(trans,
				MON_DMARB_RD_DATA_ADDR);
	iwl_write_prph_no_grab(trans, MON_DMARB_RD_CTL_ADDR, 0x0);

	iwl_trans_release_nic_access(trans, &flags);

	return monitor_len;
}

static u32
iwl_trans_pcie_dump_monitor(struct iwl_trans *trans,
			    struct iwl_fw_error_dump_data **data,
			    u32 monitor_len)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	u32 len = 0;

	if ((trans_pcie->fw_mon_page &&
	     trans->cfg->device_family == IWL_DEVICE_FAMILY_7000) ||
	    trans->dbg_dest_tlv) {
		struct iwl_fw_error_dump_fw_mon *fw_mon_data;
		u32 base, write_ptr, wrap_cnt;

		/* If there was a dest TLV - use the values from there */
		if (trans->dbg_dest_tlv) {
			write_ptr =
				le32_to_cpu(trans->dbg_dest_tlv->write_ptr_reg);
			wrap_cnt = le32_to_cpu(trans->dbg_dest_tlv->wrap_count);
			base = le32_to_cpu(trans->dbg_dest_tlv->base_reg);
		} else {
			base = MON_BUFF_BASE_ADDR;
			write_ptr = MON_BUFF_WRPTR;
			wrap_cnt = MON_BUFF_CYCLE_CNT;
		}

		(*data)->type = cpu_to_le32(IWL_FW_ERROR_DUMP_FW_MONITOR);
		fw_mon_data = (void *)(*data)->data;
		fw_mon_data->fw_mon_wr_ptr =
			cpu_to_le32(iwl_read_prph(trans, write_ptr));
		fw_mon_data->fw_mon_cycle_cnt =
			cpu_to_le32(iwl_read_prph(trans, wrap_cnt));
		fw_mon_data->fw_mon_base_ptr =
			cpu_to_le32(iwl_read_prph(trans, base));

		len += sizeof(**data) + sizeof(*fw_mon_data);
		if (trans_pcie->fw_mon_page) {
			/*
			 * The firmware is now asserted, it won't write anything
			 * to the buffer. CPU can take ownership to fetch the
			 * data. The buffer will be handed back to the device
			 * before the firmware will be restarted.
			 */
			dma_sync_single_for_cpu(trans->dev,
						trans_pcie->fw_mon_phys,
						trans_pcie->fw_mon_size,
						DMA_FROM_DEVICE);
			memcpy(fw_mon_data->data,
			       page_address(trans_pcie->fw_mon_page),
			       trans_pcie->fw_mon_size);

			monitor_len = trans_pcie->fw_mon_size;
		} else if (trans->dbg_dest_tlv->monitor_mode == SMEM_MODE) {
			/*
			 * Update pointers to reflect actual values after
			 * shifting
			 */
			base = iwl_read_prph(trans, base) <<
			       trans->dbg_dest_tlv->base_shift;
			iwl_trans_read_mem(trans, base, fw_mon_data->data,
					   monitor_len / sizeof(u32));
		} else if (trans->dbg_dest_tlv->monitor_mode == MARBH_MODE) {
			monitor_len =
				iwl_trans_pci_dump_marbh_monitor(trans,
								 fw_mon_data,
								 monitor_len);
		} else {
			/* Didn't match anything - output no monitor data */
			monitor_len = 0;
		}

		len += monitor_len;
		(*data)->len = cpu_to_le32(monitor_len + sizeof(*fw_mon_data));
	}

	return len;
}

static struct iwl_trans_dump_data
*iwl_trans_pcie_dump_data(struct iwl_trans *trans,
			  const struct iwl_fw_dbg_trigger_tlv *trigger)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_fw_error_dump_data *data;
	struct iwl_txq *cmdq = &trans_pcie->txq[trans_pcie->cmd_queue];
	struct iwl_fw_error_dump_txcmd *txcmd;
	struct iwl_trans_dump_data *dump_data;
	u32 len, num_rbs;
	u32 monitor_len;
	int i, ptr;
	bool dump_rbs = test_bit(STATUS_FW_ERROR, &trans->status);

	/* transport dump header */
	len = sizeof(*dump_data);

	/* host commands */
	len += sizeof(*data) +
		cmdq->q.n_window * (sizeof(*txcmd) + TFD_MAX_PAYLOAD_SIZE);

	/* FW monitor */
	if (trans_pcie->fw_mon_page) {
		len += sizeof(*data) + sizeof(struct iwl_fw_error_dump_fw_mon) +
		       trans_pcie->fw_mon_size;
		monitor_len = trans_pcie->fw_mon_size;
	} else if (trans->dbg_dest_tlv) {
		u32 base, end;

		base = le32_to_cpu(trans->dbg_dest_tlv->base_reg);
		end = le32_to_cpu(trans->dbg_dest_tlv->end_reg);

		base = iwl_read_prph(trans, base) <<
		       trans->dbg_dest_tlv->base_shift;
		end = iwl_read_prph(trans, end) <<
		      trans->dbg_dest_tlv->end_shift;

		/* Make "end" point to the actual end */
		if (trans->cfg->device_family == IWL_DEVICE_FAMILY_8000 ||
		    trans->dbg_dest_tlv->monitor_mode == MARBH_MODE)
			end += (1 << trans->dbg_dest_tlv->end_shift);
		monitor_len = end - base;
		len += sizeof(*data) + sizeof(struct iwl_fw_error_dump_fw_mon) +
		       monitor_len;
	} else {
		monitor_len = 0;
	}

	if (trigger && (trigger->mode & IWL_FW_DBG_TRIGGER_MONITOR_ONLY)) {
		dump_data = vzalloc(len);
		if (!dump_data)
			return NULL;

		data = (void *)dump_data->data;
		len = iwl_trans_pcie_dump_monitor(trans, &data, monitor_len);
		dump_data->len = len;

		return dump_data;
	}

	/* CSR registers */
	len += sizeof(*data) + IWL_CSR_TO_DUMP;

	/* FH registers */
	len += sizeof(*data) + (FH_MEM_UPPER_BOUND - FH_MEM_LOWER_BOUND);

	if (dump_rbs) {
		/* RBs */
		num_rbs = le16_to_cpu(ACCESS_ONCE(
				      trans_pcie->rxq.rb_stts->closed_rb_num))
				      & 0x0FFF;
		num_rbs = (num_rbs - trans_pcie->rxq.read) & RX_QUEUE_MASK;
		len += num_rbs * (sizeof(*data) +
				  sizeof(struct iwl_fw_error_dump_rb) +
				  (PAGE_SIZE << trans_pcie->rx_page_order));
	}

	dump_data = vzalloc(len);
	if (!dump_data)
		return NULL;

	len = 0;
	data = (void *)dump_data->data;
	data->type = cpu_to_le32(IWL_FW_ERROR_DUMP_TXCMD);
	txcmd = (void *)data->data;
	spin_lock_bh(&cmdq->lock);
	ptr = cmdq->q.write_ptr;
	for (i = 0; i < cmdq->q.n_window; i++) {
		u8 idx = get_cmd_index(&cmdq->q, ptr);
		u32 caplen, cmdlen;

		cmdlen = iwl_trans_pcie_get_cmdlen(&cmdq->tfds[ptr]);
		caplen = min_t(u32, TFD_MAX_PAYLOAD_SIZE, cmdlen);

		if (cmdlen) {
			len += sizeof(*txcmd) + caplen;
			txcmd->cmdlen = cpu_to_le32(cmdlen);
			txcmd->caplen = cpu_to_le32(caplen);
			memcpy(txcmd->data, cmdq->entries[idx].cmd, caplen);
			txcmd = (void *)((u8 *)txcmd->data + caplen);
		}

		ptr = iwl_queue_dec_wrap(ptr);
	}
	spin_unlock_bh(&cmdq->lock);

	data->len = cpu_to_le32(len);
	len += sizeof(*data);
	data = iwl_fw_error_next_data(data);

	len += iwl_trans_pcie_dump_csr(trans, &data);
	len += iwl_trans_pcie_fh_regs_dump(trans, &data);
	if (dump_rbs)
		len += iwl_trans_pcie_dump_rbs(trans, &data, num_rbs);

	len += iwl_trans_pcie_dump_monitor(trans, &data, monitor_len);

	dump_data->len = len;

	return dump_data;
}

static const struct iwl_trans_ops trans_ops_pcie = {
	.start_hw = iwl_trans_pcie_start_hw,
	.op_mode_leave = iwl_trans_pcie_op_mode_leave,
	.fw_alive = iwl_trans_pcie_fw_alive,
	.start_fw = iwl_trans_pcie_start_fw,
	.stop_device = iwl_trans_pcie_stop_device,

	.d3_suspend = iwl_trans_pcie_d3_suspend,
	.d3_resume = iwl_trans_pcie_d3_resume,

	.send_cmd = iwl_trans_pcie_send_hcmd,

	.tx = iwl_trans_pcie_tx,
	.reclaim = iwl_trans_pcie_reclaim,

	.txq_disable = iwl_trans_pcie_txq_disable,
	.txq_enable = iwl_trans_pcie_txq_enable,

	.wait_tx_queue_empty = iwl_trans_pcie_wait_txq_empty,
	.freeze_txq_timer = iwl_trans_pcie_freeze_txq_timer,
	.block_txq_ptrs = iwl_trans_pcie_block_txq_ptrs,

	.write8 = iwl_trans_pcie_write8,
	.write32 = iwl_trans_pcie_write32,
	.read32 = iwl_trans_pcie_read32,
	.read_prph = iwl_trans_pcie_read_prph,
	.write_prph = iwl_trans_pcie_write_prph,
	.read_mem = iwl_trans_pcie_read_mem,
	.write_mem = iwl_trans_pcie_write_mem,
	.configure = iwl_trans_pcie_configure,
	.set_pmi = iwl_trans_pcie_set_pmi,
	.grab_nic_access = iwl_trans_pcie_grab_nic_access,
	.release_nic_access = iwl_trans_pcie_release_nic_access,
	.set_bits_mask = iwl_trans_pcie_set_bits_mask,

	.ref = iwl_trans_pcie_ref,
	.unref = iwl_trans_pcie_unref,

	.dump_data = iwl_trans_pcie_dump_data,
};

struct iwl_trans *iwl_trans_pcie_alloc(struct pci_dev *pdev,
				       const struct pci_device_id *ent,
				       const struct iwl_cfg *cfg)
{
	struct iwl_trans_pcie *trans_pcie;
	struct iwl_trans *trans;
	u16 pci_cmd;
	int ret;

	trans = iwl_trans_alloc(sizeof(struct iwl_trans_pcie),
				&pdev->dev, cfg, &trans_ops_pcie, 0);
	if (!trans)
		return ERR_PTR(-ENOMEM);

	trans->max_skb_frags = IWL_PCIE_MAX_FRAGS;

	trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	trans_pcie->trans = trans;
	spin_lock_init(&trans_pcie->irq_lock);
	spin_lock_init(&trans_pcie->reg_lock);
	spin_lock_init(&trans_pcie->ref_lock);
	mutex_init(&trans_pcie->mutex);
	init_waitqueue_head(&trans_pcie->ucode_write_waitq);
	trans_pcie->tso_hdr_page = alloc_percpu(struct iwl_tso_hdr_page);
	if (!trans_pcie->tso_hdr_page) {
		ret = -ENOMEM;
		goto out_no_pci;
	}

	ret = pci_enable_device(pdev);
	if (ret)
		goto out_no_pci;

	if (!cfg->base_params->pcie_l1_allowed) {
		/*
		 * W/A - seems to solve weird behavior. We need to remove this
		 * if we don't want to stay in L1 all the time. This wastes a
		 * lot of power.
		 */
		pci_disable_link_state(pdev, PCIE_LINK_STATE_L0S |
				       PCIE_LINK_STATE_L1 |
				       PCIE_LINK_STATE_CLKPM);
	}

	pci_set_master(pdev);

	ret = pci_set_dma_mask(pdev, DMA_BIT_MASK(36));
	if (!ret)
		ret = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(36));
	if (ret) {
		ret = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
		if (!ret)
			ret = pci_set_consistent_dma_mask(pdev,
							  DMA_BIT_MASK(32));
		/* both attempts failed: */
		if (ret) {
			dev_err(&pdev->dev, "No suitable DMA available\n");
			goto out_pci_disable_device;
		}
	}

	ret = pci_request_regions(pdev, DRV_NAME);
	if (ret) {
		dev_err(&pdev->dev, "pci_request_regions failed\n");
		goto out_pci_disable_device;
	}

	trans_pcie->hw_base = pci_ioremap_bar(pdev, 0);
	if (!trans_pcie->hw_base) {
		dev_err(&pdev->dev, "pci_ioremap_bar failed\n");
		ret = -ENODEV;
		goto out_pci_release_regions;
	}

	/* We disable the RETRY_TIMEOUT register (0x41) to keep
	 * PCI Tx retries from interfering with C3 CPU state */
	pci_write_config_byte(pdev, PCI_CFG_RETRY_TIMEOUT, 0x00);

	trans->dev = &pdev->dev;
	trans_pcie->pci_dev = pdev;
	iwl_disable_interrupts(trans);

	ret = pci_enable_msi(pdev);
	if (ret) {
		dev_err(&pdev->dev, "pci_enable_msi failed(0X%x)\n", ret);
		/* enable rfkill interrupt: hw bug w/a */
		pci_read_config_word(pdev, PCI_COMMAND, &pci_cmd);
		if (pci_cmd & PCI_COMMAND_INTX_DISABLE) {
			pci_cmd &= ~PCI_COMMAND_INTX_DISABLE;
			pci_write_config_word(pdev, PCI_COMMAND, pci_cmd);
		}
	}

	trans->hw_rev = iwl_read32(trans, CSR_HW_REV);
	/*
	 * In the 8000 HW family the format of the 4 bytes of CSR_HW_REV have
	 * changed, and now the revision step also includes bit 0-1 (no more
	 * "dash" value). To keep hw_rev backwards compatible - we'll store it
	 * in the old format.
	 */
	if (trans->cfg->device_family == IWL_DEVICE_FAMILY_8000) {
		unsigned long flags;

		trans->hw_rev = (trans->hw_rev & 0xfff0) |
				(CSR_HW_REV_STEP(trans->hw_rev << 2) << 2);

		ret = iwl_pcie_prepare_card_hw(trans);
		if (ret) {
			IWL_WARN(trans, "Exit HW not ready\n");
			goto out_pci_disable_msi;
		}

		/*
		 * in-order to recognize C step driver should read chip version
		 * id located at the AUX bus MISC address space.
		 */
		iwl_set_bit(trans, CSR_GP_CNTRL,
			    CSR_GP_CNTRL_REG_FLAG_INIT_DONE);
		udelay(2);

		ret = iwl_poll_bit(trans, CSR_GP_CNTRL,
				   CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY,
				   CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY,
				   25000);
		if (ret < 0) {
			IWL_DEBUG_INFO(trans, "Failed to wake up the nic\n");
			goto out_pci_disable_msi;
		}

		if (iwl_trans_grab_nic_access(trans, &flags)) {
			u32 hw_step;

			hw_step = iwl_read_prph_no_grab(trans, WFPM_CTRL_REG);
			hw_step |= ENABLE_WFPM;
			iwl_write_prph_no_grab(trans, WFPM_CTRL_REG, hw_step);
			hw_step = iwl_read_prph_no_grab(trans, AUX_MISC_REG);
			hw_step = (hw_step >> HW_STEP_LOCATION_BITS) & 0xF;
			if (hw_step == 0x3)
				trans->hw_rev = (trans->hw_rev & 0xFFFFFFF3) |
						(SILICON_C_STEP << 2);
			iwl_trans_release_nic_access(trans, &flags);
		}
	}

	trans->hw_id = (pdev->device << 16) + pdev->subsystem_device;
	snprintf(trans->hw_id_str, sizeof(trans->hw_id_str),
		 "PCI ID: 0x%04X:0x%04X", pdev->device, pdev->subsystem_device);

	/* Initialize the wait queue for commands */
	init_waitqueue_head(&trans_pcie->wait_command_queue);

	ret = iwl_pcie_alloc_ict(trans);
	if (ret)
		goto out_pci_disable_msi;

	ret = request_threaded_irq(pdev->irq, iwl_pcie_isr,
				   iwl_pcie_irq_handler,
				   IRQF_SHARED, DRV_NAME, trans);
	if (ret) {
		IWL_ERR(trans, "Error allocating IRQ %d\n", pdev->irq);
		goto out_free_ict;
	}

	trans_pcie->inta_mask = CSR_INI_SET_MASK;

	return trans;

out_free_ict:
	iwl_pcie_free_ict(trans);
out_pci_disable_msi:
	pci_disable_msi(pdev);
out_pci_release_regions:
	pci_release_regions(pdev);
out_pci_disable_device:
	pci_disable_device(pdev);
out_no_pci:
	free_percpu(trans_pcie->tso_hdr_page);
	iwl_trans_free(trans);
	return ERR_PTR(ret);
}
