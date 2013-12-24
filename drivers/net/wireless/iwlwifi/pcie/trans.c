/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2007 - 2013 Intel Corporation. All rights reserved.
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
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2005 - 2013 Intel Corporation. All rights reserved.
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

#include "iwl-drv.h"
#include "iwl-trans.h"
#include "iwl-csr.h"
#include "iwl-prph.h"
#include "iwl-agn-hw.h"
#include "internal.h"

static void __iwl_trans_pcie_set_bits_mask(struct iwl_trans *trans,
						  u32 reg, u32 mask, u32 value)
{
	u32 v;

#ifdef CONFIG_IWLWIFI_DEBUG
	WARN_ON_ONCE(value & ~mask);
#endif

	v = iwl_read32(trans, reg);
	v &= ~mask;
	v |= value;
	iwl_write32(trans, reg, v);
}

static inline void __iwl_trans_pcie_clear_bit(struct iwl_trans *trans,
					      u32 reg, u32 mask)
{
	__iwl_trans_pcie_set_bits_mask(trans, reg, mask, 0);
}

static inline void __iwl_trans_pcie_set_bit(struct iwl_trans *trans,
					    u32 reg, u32 mask)
{
	__iwl_trans_pcie_set_bits_mask(trans, reg, mask, mask);
}

static void iwl_pcie_set_pwr(struct iwl_trans *trans, bool vaux)
{
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

	/*
	 * HW bug W/A for instability in PCIe bus L0S->L1 transition.
	 * Check if BIOS (or OS) enabled L1-ASPM on this device.
	 * If so (likely), disable L0S, so device moves directly L0->L1;
	 *    costs negligible amount of power savings.
	 * If not (unlikely), enable L0S, so there is at least some
	 *    power savings, even without L1.
	 */
	pcie_capability_read_word(trans_pcie->pci_dev, PCI_EXP_LNKCTL, &lctl);
	if (lctl & PCI_EXP_LNKCTL_ASPM_L1) {
		/* L1-ASPM enabled; disable(!) L0S */
		iwl_set_bit(trans, CSR_GIO_REG, CSR_GIO_REG_VAL_L0S_ENABLED);
		dev_info(trans->dev, "L1 Enabled; Disabling L0S\n");
	} else {
		/* L1-ASPM disabled; enable(!) L0S */
		iwl_clear_bit(trans, CSR_GIO_REG, CSR_GIO_REG_VAL_L0S_ENABLED);
		dev_info(trans->dev, "L1 Disabled; Enabling L0S\n");
	}
	trans->pm_support = !(lctl & PCI_EXP_LNKCTL_ASPM_L0S);
}

/*
 * Start up NIC's basic functionality after it has been reset
 * (e.g. after platform boot, or shutdown via iwl_pcie_apm_stop())
 * NOTE:  This does not load uCode nor start the embedded processor
 */
static int iwl_pcie_apm_init(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	int ret = 0;
	IWL_DEBUG_INFO(trans, "Init card's basic functions\n");

	/*
	 * Use "set_bit" below rather than "write", to preserve any hardware
	 * bits already set by default after reset.
	 */

	/* Disable L0S exit timer (platform NMI Work/Around) */
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

	/*
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

	/*
	 * Enable DMA clock and wait for it to stabilize.
	 *
	 * Write to "CLK_EN_REG"; "1" bits enable clocks, while "0" bits
	 * do not disable clocks.  This preserves any hardware bits already
	 * set by default in "CLK_CTRL_REG" after reset.
	 */
	iwl_write_prph(trans, APMG_CLK_EN_REG, APMG_CLK_VAL_DMA_CLK_RQT);
	udelay(20);

	/* Disable L1-Active */
	iwl_set_bits_prph(trans, APMG_PCIDEV_STT_REG,
			  APMG_PCIDEV_STT_VAL_L1_ACT_DIS);

	set_bit(STATUS_DEVICE_ENABLED, &trans_pcie->status);

out:
	return ret;
}

static int iwl_pcie_apm_stop_master(struct iwl_trans *trans)
{
	int ret = 0;

	/* stop device's busmaster DMA activity */
	iwl_set_bit(trans, CSR_RESET, CSR_RESET_REG_FLAG_STOP_MASTER);

	ret = iwl_poll_bit(trans, CSR_RESET,
			   CSR_RESET_REG_FLAG_MASTER_DISABLED,
			   CSR_RESET_REG_FLAG_MASTER_DISABLED, 100);
	if (ret)
		IWL_WARN(trans, "Master Disable Timed Out, 100 usec\n");

	IWL_DEBUG_INFO(trans, "stop master\n");

	return ret;
}

static void iwl_pcie_apm_stop(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	IWL_DEBUG_INFO(trans, "Stop card, put in low power state\n");

	clear_bit(STATUS_DEVICE_ENABLED, &trans_pcie->status);

	/* Stop device's DMA activity */
	iwl_pcie_apm_stop_master(trans);

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
	unsigned long flags;

	/* nic_init */
	spin_lock_irqsave(&trans_pcie->irq_lock, flags);
	iwl_pcie_apm_init(trans);

	/* Set interrupt coalescing calibration timer to default (512 usecs) */
	iwl_write8(trans, CSR_INT_COALESCING, IWL_HOST_INT_CALIB_TIMEOUT_DEF);

	spin_unlock_irqrestore(&trans_pcie->irq_lock, flags);

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

	IWL_DEBUG_INFO(trans, "hardware%s ready\n", ret < 0 ? " not" : "");
	return ret;
}

/* Note: returns standard 0/-ERROR code */
static int iwl_pcie_prepare_card_hw(struct iwl_trans *trans)
{
	int ret;
	int t = 0;

	IWL_DEBUG_INFO(trans, "iwl_trans_prepare_card_hw enter\n");

	ret = iwl_pcie_set_hw_ready(trans);
	/* If the card is ready, exit 0 */
	if (ret >= 0)
		return 0;

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
	u32 offset;
	int ret = 0;

	IWL_DEBUG_FW(trans, "[%d] uCode section being loaded...\n",
		     section_num);

	v_addr = dma_alloc_coherent(trans->dev, PAGE_SIZE, &p_addr, GFP_KERNEL);
	if (!v_addr)
		return -ENOMEM;

	for (offset = 0; offset < section->len; offset += PAGE_SIZE) {
		u32 copy_size;

		copy_size = min_t(u32, PAGE_SIZE, section->len - offset);

		memcpy(v_addr, (u8 *)section->data + offset, copy_size);
		ret = iwl_pcie_load_firmware_chunk(trans,
						   section->offset + offset,
						   p_addr, copy_size);
		if (ret) {
			IWL_ERR(trans,
				"Could not load the [%d] uCode section\n",
				section_num);
			break;
		}
	}

	dma_free_coherent(trans->dev, PAGE_SIZE, v_addr, p_addr);
	return ret;
}

static int iwl_pcie_load_given_ucode(struct iwl_trans *trans,
				const struct fw_img *image)
{
	int i, ret = 0;

	for (i = 0; i < IWL_UCODE_SECTION_MAX; i++) {
		if (!image->sec[i].data)
			break;

		ret = iwl_pcie_load_section(trans, i, &image->sec[i]);
		if (ret)
			return ret;
	}

	/* Remove all resets to allow NIC to operate */
	iwl_write32(trans, CSR_RESET, 0);

	return 0;
}

static int iwl_trans_pcie_start_fw(struct iwl_trans *trans,
				   const struct fw_img *fw, bool run_in_rfkill)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	int ret;
	bool hw_rfkill;

	/* This may fail if AMT took ownership of the device */
	if (iwl_pcie_prepare_card_hw(trans)) {
		IWL_WARN(trans, "Exit HW not ready\n");
		return -EIO;
	}

	clear_bit(STATUS_FW_ERROR, &trans_pcie->status);

	iwl_enable_rfkill_int(trans);

	/* If platform's RF_KILL switch is NOT set to KILL */
	hw_rfkill = iwl_is_rfkill_set(trans);
	if (hw_rfkill)
		set_bit(STATUS_RFKILL, &trans_pcie->status);
	else
		clear_bit(STATUS_RFKILL, &trans_pcie->status);
	iwl_op_mode_hw_rf_kill(trans->op_mode, hw_rfkill);
	if (hw_rfkill && !run_in_rfkill)
		return -ERFKILL;

	iwl_write32(trans, CSR_INT, 0xFFFFFFFF);

	ret = iwl_pcie_nic_init(trans);
	if (ret) {
		IWL_ERR(trans, "Unable to init nic\n");
		return ret;
	}

	/* make sure rfkill handshake bits are cleared */
	iwl_write32(trans, CSR_UCODE_DRV_GP1_CLR, CSR_UCODE_SW_BIT_RFKILL);
	iwl_write32(trans, CSR_UCODE_DRV_GP1_CLR,
		    CSR_UCODE_DRV_GP1_BIT_CMD_BLOCKED);

	/* clear (again), then enable host interrupts */
	iwl_write32(trans, CSR_INT, 0xFFFFFFFF);
	iwl_enable_interrupts(trans);

	/* really make sure rfkill handshake bits are cleared */
	iwl_write32(trans, CSR_UCODE_DRV_GP1_CLR, CSR_UCODE_SW_BIT_RFKILL);
	iwl_write32(trans, CSR_UCODE_DRV_GP1_CLR, CSR_UCODE_SW_BIT_RFKILL);

	/* Load the given image to the HW */
	return iwl_pcie_load_given_ucode(trans, fw);
}

static void iwl_trans_pcie_fw_alive(struct iwl_trans *trans, u32 scd_addr)
{
	iwl_pcie_reset_ict(trans);
	iwl_pcie_tx_start(trans, scd_addr);
}

static void iwl_trans_pcie_stop_device(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	unsigned long flags;

	/* tell the device to stop sending interrupts */
	spin_lock_irqsave(&trans_pcie->irq_lock, flags);
	iwl_disable_interrupts(trans);
	spin_unlock_irqrestore(&trans_pcie->irq_lock, flags);

	/* device going down, Stop using ICT table */
	iwl_pcie_disable_ict(trans);

	/*
	 * If a HW restart happens during firmware loading,
	 * then the firmware loading might call this function
	 * and later it might be called again due to the
	 * restart. So don't process again if the device is
	 * already dead.
	 */
	if (test_bit(STATUS_DEVICE_ENABLED, &trans_pcie->status)) {
		iwl_pcie_tx_stop(trans);
		iwl_pcie_rx_stop(trans);

		/* Power-down device's busmaster DMA clocks */
		iwl_write_prph(trans, APMG_CLK_DIS_REG,
			       APMG_CLK_VAL_DMA_CLK_RQT);
		udelay(5);
	}

	/* Make sure (redundant) we've released our request to stay awake */
	iwl_clear_bit(trans, CSR_GP_CNTRL,
		      CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);

	/* Stop the device, and put it in low power state */
	iwl_pcie_apm_stop(trans);

	/* Upon stop, the APM issues an interrupt if HW RF kill is set.
	 * Clean again the interrupt here
	 */
	spin_lock_irqsave(&trans_pcie->irq_lock, flags);
	iwl_disable_interrupts(trans);
	spin_unlock_irqrestore(&trans_pcie->irq_lock, flags);

	iwl_enable_rfkill_int(trans);

	/* stop and reset the on-board processor */
	iwl_write32(trans, CSR_RESET, CSR_RESET_REG_FLAG_NEVO_RESET);

	/* clear all status bits */
	clear_bit(STATUS_HCMD_ACTIVE, &trans_pcie->status);
	clear_bit(STATUS_INT_ENABLED, &trans_pcie->status);
	clear_bit(STATUS_DEVICE_ENABLED, &trans_pcie->status);
	clear_bit(STATUS_TPOWER_PMI, &trans_pcie->status);
	clear_bit(STATUS_RFKILL, &trans_pcie->status);
}

static void iwl_trans_pcie_d3_suspend(struct iwl_trans *trans)
{
	/* let the ucode operate on its own */
	iwl_write32(trans, CSR_UCODE_DRV_GP1_SET,
		    CSR_UCODE_DRV_GP1_BIT_D3_CFG_COMPLETE);

	iwl_disable_interrupts(trans);
	iwl_pcie_disable_ict(trans);

	iwl_clear_bit(trans, CSR_GP_CNTRL,
		      CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);
	iwl_clear_bit(trans, CSR_GP_CNTRL,
		      CSR_GP_CNTRL_REG_FLAG_INIT_DONE);

	/*
	 * reset TX queues -- some of their registers reset during S3
	 * so if we don't reset everything here the D3 image would try
	 * to execute some invalid memory upon resume
	 */
	iwl_trans_pcie_tx_reset(trans);

	iwl_pcie_set_pwr(trans, true);
}

static int iwl_trans_pcie_d3_resume(struct iwl_trans *trans,
				    enum iwl_d3_status *status)
{
	u32 val;
	int ret;

	iwl_pcie_set_pwr(trans, false);

	val = iwl_read32(trans, CSR_RESET);
	if (val & CSR_RESET_REG_FLAG_NEVO_RESET) {
		*status = IWL_D3_STATUS_RESET;
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

	ret = iwl_poll_bit(trans, CSR_GP_CNTRL,
			   CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY,
			   CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY,
			   25000);
	if (ret) {
		IWL_ERR(trans, "Failed to resume the device (mac ready)\n");
		return ret;
	}

	iwl_trans_pcie_tx_reset(trans);

	ret = iwl_pcie_rx_init(trans);
	if (ret) {
		IWL_ERR(trans, "Failed to resume the device (RX reset)\n");
		return ret;
	}

	iwl_write32(trans, CSR_UCODE_DRV_GP1_CLR,
		    CSR_UCODE_DRV_GP1_BIT_D3_CFG_COMPLETE);

	*status = IWL_D3_STATUS_ALIVE;
	return 0;
}

static int iwl_trans_pcie_start_hw(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	bool hw_rfkill;
	int err;

	err = iwl_pcie_prepare_card_hw(trans);
	if (err) {
		IWL_ERR(trans, "Error while preparing HW: %d\n", err);
		return err;
	}

	iwl_pcie_apm_init(trans);

	/* From now on, the op_mode will be kept updated about RF kill state */
	iwl_enable_rfkill_int(trans);

	hw_rfkill = iwl_is_rfkill_set(trans);
	if (hw_rfkill)
		set_bit(STATUS_RFKILL, &trans_pcie->status);
	else
		clear_bit(STATUS_RFKILL, &trans_pcie->status);
	iwl_op_mode_hw_rf_kill(trans->op_mode, hw_rfkill);

	return 0;
}

static void iwl_trans_pcie_stop_hw(struct iwl_trans *trans,
				   bool op_mode_leaving)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	bool hw_rfkill;
	unsigned long flags;

	spin_lock_irqsave(&trans_pcie->irq_lock, flags);
	iwl_disable_interrupts(trans);
	spin_unlock_irqrestore(&trans_pcie->irq_lock, flags);

	iwl_pcie_apm_stop(trans);

	spin_lock_irqsave(&trans_pcie->irq_lock, flags);
	iwl_disable_interrupts(trans);
	spin_unlock_irqrestore(&trans_pcie->irq_lock, flags);

	iwl_pcie_disable_ict(trans);

	if (!op_mode_leaving) {
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
		 */
		hw_rfkill = iwl_is_rfkill_set(trans);
		if (hw_rfkill)
			set_bit(STATUS_RFKILL, &trans_pcie->status);
		else
			clear_bit(STATUS_RFKILL, &trans_pcie->status);
		iwl_op_mode_hw_rf_kill(trans->op_mode, hw_rfkill);
	}
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

static void iwl_trans_pcie_configure(struct iwl_trans *trans,
				     const struct iwl_trans_config *trans_cfg)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	trans_pcie->cmd_queue = trans_cfg->cmd_queue;
	trans_pcie->cmd_fifo = trans_cfg->cmd_fifo;
	if (WARN_ON(trans_cfg->n_no_reclaim_cmds > MAX_NO_RECLAIM_CMDS))
		trans_pcie->n_no_reclaim_cmds = 0;
	else
		trans_pcie->n_no_reclaim_cmds = trans_cfg->n_no_reclaim_cmds;
	if (trans_pcie->n_no_reclaim_cmds)
		memcpy(trans_pcie->no_reclaim_cmds, trans_cfg->no_reclaim_cmds,
		       trans_pcie->n_no_reclaim_cmds * sizeof(u8));

	trans_pcie->rx_buf_size_8k = trans_cfg->rx_buf_size_8k;
	if (trans_pcie->rx_buf_size_8k)
		trans_pcie->rx_page_order = get_order(8 * 1024);
	else
		trans_pcie->rx_page_order = get_order(4 * 1024);

	trans_pcie->wd_timeout =
		msecs_to_jiffies(trans_cfg->queue_watchdog_timeout);

	trans_pcie->command_names = trans_cfg->command_names;
	trans_pcie->bc_table_dword = trans_cfg->bc_table_dword;
}

void iwl_trans_pcie_free(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	synchronize_irq(trans_pcie->pci_dev->irq);

	iwl_pcie_tx_free(trans);
	iwl_pcie_rx_free(trans);

	free_irq(trans_pcie->pci_dev->irq, trans);
	iwl_pcie_free_ict(trans);

	pci_disable_msi(trans_pcie->pci_dev);
	iounmap(trans_pcie->hw_base);
	pci_release_regions(trans_pcie->pci_dev);
	pci_disable_device(trans_pcie->pci_dev);
	kmem_cache_destroy(trans->dev_cmd_pool);

	kfree(trans);
}

static void iwl_trans_pcie_set_pmi(struct iwl_trans *trans, bool state)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	if (state)
		set_bit(STATUS_TPOWER_PMI, &trans_pcie->status);
	else
		clear_bit(STATUS_TPOWER_PMI, &trans_pcie->status);
}

#ifdef CONFIG_PM_SLEEP
static int iwl_trans_pcie_suspend(struct iwl_trans *trans)
{
	return 0;
}

static int iwl_trans_pcie_resume(struct iwl_trans *trans)
{
	bool hw_rfkill;

	iwl_enable_rfkill_int(trans);

	hw_rfkill = iwl_is_rfkill_set(trans);
	iwl_op_mode_hw_rf_kill(trans->op_mode, hw_rfkill);

	return 0;
}
#endif /* CONFIG_PM_SLEEP */

static bool iwl_trans_pcie_grab_nic_access(struct iwl_trans *trans, bool silent,
						unsigned long *flags)
{
	int ret;
	struct iwl_trans_pcie *pcie_trans = IWL_TRANS_GET_PCIE_TRANS(trans);
	spin_lock_irqsave(&pcie_trans->reg_lock, *flags);

	/* this bit wakes up the NIC */
	__iwl_trans_pcie_set_bit(trans, CSR_GP_CNTRL,
				 CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);

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
		if (!silent) {
			u32 val = iwl_read32(trans, CSR_GP_CNTRL);
			WARN_ONCE(1,
				  "Timeout waiting for hardware access (CSR_GP_CNTRL 0x%08x)\n",
				  val);
			spin_unlock_irqrestore(&pcie_trans->reg_lock, *flags);
			return false;
		}
	}

	/*
	 * Fool sparse by faking we release the lock - sparse will
	 * track nic_access anyway.
	 */
	__release(&pcie_trans->reg_lock);
	return true;
}

static void iwl_trans_pcie_release_nic_access(struct iwl_trans *trans,
					      unsigned long *flags)
{
	struct iwl_trans_pcie *pcie_trans = IWL_TRANS_GET_PCIE_TRANS(trans);

	lockdep_assert_held(&pcie_trans->reg_lock);

	/*
	 * Fool sparse by faking we acquiring the lock - sparse will
	 * track nic_access anyway.
	 */
	__acquire(&pcie_trans->reg_lock);

	__iwl_trans_pcie_clear_bit(trans, CSR_GP_CNTRL,
				   CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);
	/*
	 * Above we read the CSR_GP_CNTRL register, which will flush
	 * any previous writes, but we need the write that clears the
	 * MAC_ACCESS_REQ bit to be performed before any other writes
	 * scheduled on different CPUs (after we drop reg_lock).
	 */
	mmiowb();
	spin_unlock_irqrestore(&pcie_trans->reg_lock, *flags);
}

static int iwl_trans_pcie_read_mem(struct iwl_trans *trans, u32 addr,
				   void *buf, int dwords)
{
	unsigned long flags;
	int offs, ret = 0;
	u32 *vals = buf;

	if (iwl_trans_grab_nic_access(trans, false, &flags)) {
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
				    void *buf, int dwords)
{
	unsigned long flags;
	int offs, ret = 0;
	u32 *vals = buf;

	if (iwl_trans_grab_nic_access(trans, false, &flags)) {
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

#define IWL_FLUSH_WAIT_MS	2000

static int iwl_trans_pcie_wait_txq_empty(struct iwl_trans *trans)
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
		if (cnt == trans_pcie->cmd_queue)
			continue;
		txq = &trans_pcie->txq[cnt];
		q = &txq->q;
		while (q->read_ptr != q->write_ptr && !time_after(jiffies,
		       now + msecs_to_jiffies(IWL_FLUSH_WAIT_MS)))
			msleep(1);

		if (q->read_ptr != q->write_ptr) {
			IWL_ERR(trans,
				"fail to flush all tx fifo queues Q %d\n", cnt);
			ret = -ETIMEDOUT;
			break;
		}
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
			iwl_read_prph(trans,
				      SCD_QUEUE_RDPTR(cnt)) & (txq->q.n_bd - 1),
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

static const char *get_fh_string(int cmd)
{
#define IWL_CMD(x) case x: return #x
	switch (cmd) {
	IWL_CMD(FH_RSCSR_CHNL0_STTS_WPTR_REG);
	IWL_CMD(FH_RSCSR_CHNL0_RBDCB_BASE_REG);
	IWL_CMD(FH_RSCSR_CHNL0_WPTR);
	IWL_CMD(FH_MEM_RCSR_CHNL0_CONFIG_REG);
	IWL_CMD(FH_MEM_RSSR_SHARED_CTRL_REG);
	IWL_CMD(FH_MEM_RSSR_RX_STATUS_REG);
	IWL_CMD(FH_MEM_RSSR_RX_ENABLE_ERR_IRQ2DRV);
	IWL_CMD(FH_TSSR_TX_STATUS_REG);
	IWL_CMD(FH_TSSR_TX_ERROR_REG);
	default:
		return "UNKNOWN";
	}
#undef IWL_CMD
}

int iwl_pcie_dump_fh(struct iwl_trans *trans, char **buf)
{
	int i;
	static const u32 fh_tbl[] = {
		FH_RSCSR_CHNL0_STTS_WPTR_REG,
		FH_RSCSR_CHNL0_RBDCB_BASE_REG,
		FH_RSCSR_CHNL0_WPTR,
		FH_MEM_RCSR_CHNL0_CONFIG_REG,
		FH_MEM_RSSR_SHARED_CTRL_REG,
		FH_MEM_RSSR_RX_STATUS_REG,
		FH_MEM_RSSR_RX_ENABLE_ERR_IRQ2DRV,
		FH_TSSR_TX_STATUS_REG,
		FH_TSSR_TX_ERROR_REG
	};

#ifdef CONFIG_IWLWIFI_DEBUGFS
	if (buf) {
		int pos = 0;
		size_t bufsz = ARRAY_SIZE(fh_tbl) * 48 + 40;

		*buf = kmalloc(bufsz, GFP_KERNEL);
		if (!*buf)
			return -ENOMEM;

		pos += scnprintf(*buf + pos, bufsz - pos,
				"FH register values:\n");

		for (i = 0; i < ARRAY_SIZE(fh_tbl); i++)
			pos += scnprintf(*buf + pos, bufsz - pos,
				"  %34s: 0X%08x\n",
				get_fh_string(fh_tbl[i]),
				iwl_read_direct32(trans, fh_tbl[i]));

		return pos;
	}
#endif

	IWL_ERR(trans, "FH register values:\n");
	for (i = 0; i <  ARRAY_SIZE(fh_tbl); i++)
		IWL_ERR(trans, "  %34s: 0X%08x\n",
			get_fh_string(fh_tbl[i]),
			iwl_read_direct32(trans, fh_tbl[i]));

	return 0;
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
#define DEBUGFS_READ_FUNC(name)                                         \
static ssize_t iwl_dbgfs_##name##_read(struct file *file,               \
					char __user *user_buf,          \
					size_t count, loff_t *ppos);

#define DEBUGFS_WRITE_FUNC(name)                                        \
static ssize_t iwl_dbgfs_##name##_write(struct file *file,              \
					const char __user *user_buf,    \
					size_t count, loff_t *ppos);

#define DEBUGFS_READ_FILE_OPS(name)					\
	DEBUGFS_READ_FUNC(name);					\
static const struct file_operations iwl_dbgfs_##name##_ops = {		\
	.read = iwl_dbgfs_##name##_read,				\
	.open = simple_open,						\
	.llseek = generic_file_llseek,					\
};

#define DEBUGFS_WRITE_FILE_OPS(name)                                    \
	DEBUGFS_WRITE_FUNC(name);                                       \
static const struct file_operations iwl_dbgfs_##name##_ops = {          \
	.write = iwl_dbgfs_##name##_write,                              \
	.open = simple_open,						\
	.llseek = generic_file_llseek,					\
};

#define DEBUGFS_READ_WRITE_FILE_OPS(name)				\
	DEBUGFS_READ_FUNC(name);					\
	DEBUGFS_WRITE_FUNC(name);					\
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

	bufsz = sizeof(char) * 64 * trans->cfg->base_params->num_of_queues;

	if (!trans_pcie->txq)
		return -EAGAIN;

	buf = kzalloc(bufsz, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	for (cnt = 0; cnt < trans->cfg->base_params->num_of_queues; cnt++) {
		txq = &trans_pcie->txq[cnt];
		q = &txq->q;
		pos += scnprintf(buf + pos, bufsz - pos,
				"hwq %.2d: read=%u write=%u use=%d stop=%d\n",
				cnt, q->read_ptr, q->write_ptr,
				!!test_bit(cnt, trans_pcie->queue_used),
				!!test_bit(cnt, trans_pcie->queue_stopped));
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
	int pos = 0;
	ssize_t ret = -EFAULT;

	ret = pos = iwl_pcie_dump_fh(trans, &buf);
	if (buf) {
		ret = simple_read_from_buffer(user_buf,
					      count, ppos, buf, pos);
		kfree(buf);
	}

	return ret;
}

DEBUGFS_READ_WRITE_FILE_OPS(interrupt);
DEBUGFS_READ_FILE_OPS(fh_reg);
DEBUGFS_READ_FILE_OPS(rx_queue);
DEBUGFS_READ_FILE_OPS(tx_queue);
DEBUGFS_WRITE_FILE_OPS(csr);

/*
 * Create the debugfs files and directories
 *
 */
static int iwl_trans_pcie_dbgfs_register(struct iwl_trans *trans,
					 struct dentry *dir)
{
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
#else
static int iwl_trans_pcie_dbgfs_register(struct iwl_trans *trans,
					 struct dentry *dir)
{
	return 0;
}
#endif /*CONFIG_IWLWIFI_DEBUGFS */

static const struct iwl_trans_ops trans_ops_pcie = {
	.start_hw = iwl_trans_pcie_start_hw,
	.stop_hw = iwl_trans_pcie_stop_hw,
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

	.dbgfs_register = iwl_trans_pcie_dbgfs_register,

	.wait_tx_queue_empty = iwl_trans_pcie_wait_txq_empty,

#ifdef CONFIG_PM_SLEEP
	.suspend = iwl_trans_pcie_suspend,
	.resume = iwl_trans_pcie_resume,
#endif
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
};

struct iwl_trans *iwl_trans_pcie_alloc(struct pci_dev *pdev,
				       const struct pci_device_id *ent,
				       const struct iwl_cfg *cfg)
{
	struct iwl_trans_pcie *trans_pcie;
	struct iwl_trans *trans;
	u16 pci_cmd;
	int err;

	trans = kzalloc(sizeof(struct iwl_trans) +
			sizeof(struct iwl_trans_pcie), GFP_KERNEL);

	if (!trans)
		return NULL;

	trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	trans->ops = &trans_ops_pcie;
	trans->cfg = cfg;
	trans_lockdep_init(trans);
	trans_pcie->trans = trans;
	spin_lock_init(&trans_pcie->irq_lock);
	spin_lock_init(&trans_pcie->reg_lock);
	init_waitqueue_head(&trans_pcie->ucode_write_waitq);

	if (pci_enable_device(pdev)) {
		err = -ENODEV;
		goto out_no_pci;
	}

	/* W/A - seems to solve weird behavior. We need to remove this if we
	 * don't want to stay in L1 all the time. This wastes a lot of power */
	pci_disable_link_state(pdev, PCIE_LINK_STATE_L0S | PCIE_LINK_STATE_L1 |
			       PCIE_LINK_STATE_CLKPM);

	pci_set_master(pdev);

	err = pci_set_dma_mask(pdev, DMA_BIT_MASK(36));
	if (!err)
		err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(36));
	if (err) {
		err = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
		if (!err)
			err = pci_set_consistent_dma_mask(pdev,
							  DMA_BIT_MASK(32));
		/* both attempts failed: */
		if (err) {
			dev_err(&pdev->dev, "No suitable DMA available\n");
			goto out_pci_disable_device;
		}
	}

	err = pci_request_regions(pdev, DRV_NAME);
	if (err) {
		dev_err(&pdev->dev, "pci_request_regions failed\n");
		goto out_pci_disable_device;
	}

	trans_pcie->hw_base = pci_ioremap_bar(pdev, 0);
	if (!trans_pcie->hw_base) {
		dev_err(&pdev->dev, "pci_ioremap_bar failed\n");
		err = -ENODEV;
		goto out_pci_release_regions;
	}

	/* We disable the RETRY_TIMEOUT register (0x41) to keep
	 * PCI Tx retries from interfering with C3 CPU state */
	pci_write_config_byte(pdev, PCI_CFG_RETRY_TIMEOUT, 0x00);

	err = pci_enable_msi(pdev);
	if (err) {
		dev_err(&pdev->dev, "pci_enable_msi failed(0X%x)\n", err);
		/* enable rfkill interrupt: hw bug w/a */
		pci_read_config_word(pdev, PCI_COMMAND, &pci_cmd);
		if (pci_cmd & PCI_COMMAND_INTX_DISABLE) {
			pci_cmd &= ~PCI_COMMAND_INTX_DISABLE;
			pci_write_config_word(pdev, PCI_COMMAND, pci_cmd);
		}
	}

	trans->dev = &pdev->dev;
	trans_pcie->pci_dev = pdev;
	trans->hw_rev = iwl_read32(trans, CSR_HW_REV);
	trans->hw_id = (pdev->device << 16) + pdev->subsystem_device;
	snprintf(trans->hw_id_str, sizeof(trans->hw_id_str),
		 "PCI ID: 0x%04X:0x%04X", pdev->device, pdev->subsystem_device);

	/* Initialize the wait queue for commands */
	init_waitqueue_head(&trans_pcie->wait_command_queue);

	snprintf(trans->dev_cmd_pool_name, sizeof(trans->dev_cmd_pool_name),
		 "iwl_cmd_pool:%s", dev_name(trans->dev));

	trans->dev_cmd_headroom = 0;
	trans->dev_cmd_pool =
		kmem_cache_create(trans->dev_cmd_pool_name,
				  sizeof(struct iwl_device_cmd)
				  + trans->dev_cmd_headroom,
				  sizeof(void *),
				  SLAB_HWCACHE_ALIGN,
				  NULL);

	if (!trans->dev_cmd_pool)
		goto out_pci_disable_msi;

	trans_pcie->inta_mask = CSR_INI_SET_MASK;

	if (iwl_pcie_alloc_ict(trans))
		goto out_free_cmd_pool;

	if (request_threaded_irq(pdev->irq, iwl_pcie_isr_ict,
				 iwl_pcie_irq_handler,
				 IRQF_SHARED, DRV_NAME, trans)) {
		IWL_ERR(trans, "Error allocating IRQ %d\n", pdev->irq);
		goto out_free_ict;
	}

	return trans;

out_free_ict:
	iwl_pcie_free_ict(trans);
out_free_cmd_pool:
	kmem_cache_destroy(trans->dev_cmd_pool);
out_pci_disable_msi:
	pci_disable_msi(pdev);
out_pci_release_regions:
	pci_release_regions(pdev);
out_pci_disable_device:
	pci_disable_device(pdev);
out_no_pci:
	kfree(trans);
	return NULL;
}
