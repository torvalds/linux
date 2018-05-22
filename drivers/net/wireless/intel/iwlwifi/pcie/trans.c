/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2007 - 2015 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016 - 2017 Intel Deutschland GmbH
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
 * Copyright(c) 2016 - 2017 Intel Deutschland GmbH
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
#include <linux/pm_runtime.h>

#include "iwl-drv.h"
#include "iwl-trans.h"
#include "iwl-csr.h"
#include "iwl-prph.h"
#include "iwl-scd.h"
#include "iwl-agn-hw.h"
#include "fw/error-dump.h"
#include "internal.h"
#include "iwl-fh.h"

/* extended range in FW SRAM */
#define IWL_FW_MEM_EXTENDED_START	0x40000
#define IWL_FW_MEM_EXTENDED_END		0x57FFF

static void iwl_trans_pcie_dump_regs(struct iwl_trans *trans)
{
#define PCI_DUMP_SIZE	64
#define PREFIX_LEN	32
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct pci_dev *pdev = trans_pcie->pci_dev;
	u32 i, pos, alloc_size, *ptr, *buf;
	char *prefix;

	if (trans_pcie->pcie_dbg_dumped_once)
		return;

	/* Should be a multiple of 4 */
	BUILD_BUG_ON(PCI_DUMP_SIZE > 4096 || PCI_DUMP_SIZE & 0x3);
	/* Alloc a max size buffer */
	if (PCI_ERR_ROOT_ERR_SRC +  4 > PCI_DUMP_SIZE)
		alloc_size = PCI_ERR_ROOT_ERR_SRC +  4 + PREFIX_LEN;
	else
		alloc_size = PCI_DUMP_SIZE + PREFIX_LEN;
	buf = kmalloc(alloc_size, GFP_ATOMIC);
	if (!buf)
		return;
	prefix = (char *)buf + alloc_size - PREFIX_LEN;

	IWL_ERR(trans, "iwlwifi transaction failed, dumping registers\n");

	/* Print wifi device registers */
	sprintf(prefix, "iwlwifi %s: ", pci_name(pdev));
	IWL_ERR(trans, "iwlwifi device config registers:\n");
	for (i = 0, ptr = buf; i < PCI_DUMP_SIZE; i += 4, ptr++)
		if (pci_read_config_dword(pdev, i, ptr))
			goto err_read;
	print_hex_dump(KERN_ERR, prefix, DUMP_PREFIX_OFFSET, 32, 4, buf, i, 0);

	IWL_ERR(trans, "iwlwifi device memory mapped registers:\n");
	for (i = 0, ptr = buf; i < PCI_DUMP_SIZE; i += 4, ptr++)
		*ptr = iwl_read32(trans, i);
	print_hex_dump(KERN_ERR, prefix, DUMP_PREFIX_OFFSET, 32, 4, buf, i, 0);

	pos = pci_find_ext_capability(pdev, PCI_EXT_CAP_ID_ERR);
	if (pos) {
		IWL_ERR(trans, "iwlwifi device AER capability structure:\n");
		for (i = 0, ptr = buf; i < PCI_ERR_ROOT_COMMAND; i += 4, ptr++)
			if (pci_read_config_dword(pdev, pos + i, ptr))
				goto err_read;
		print_hex_dump(KERN_ERR, prefix, DUMP_PREFIX_OFFSET,
			       32, 4, buf, i, 0);
	}

	/* Print parent device registers next */
	if (!pdev->bus->self)
		goto out;

	pdev = pdev->bus->self;
	sprintf(prefix, "iwlwifi %s: ", pci_name(pdev));

	IWL_ERR(trans, "iwlwifi parent port (%s) config registers:\n",
		pci_name(pdev));
	for (i = 0, ptr = buf; i < PCI_DUMP_SIZE; i += 4, ptr++)
		if (pci_read_config_dword(pdev, i, ptr))
			goto err_read;
	print_hex_dump(KERN_ERR, prefix, DUMP_PREFIX_OFFSET, 32, 4, buf, i, 0);

	/* Print root port AER registers */
	pos = 0;
	pdev = pcie_find_root_port(pdev);
	if (pdev)
		pos = pci_find_ext_capability(pdev, PCI_EXT_CAP_ID_ERR);
	if (pos) {
		IWL_ERR(trans, "iwlwifi root port (%s) AER cap structure:\n",
			pci_name(pdev));
		sprintf(prefix, "iwlwifi %s: ", pci_name(pdev));
		for (i = 0, ptr = buf; i <= PCI_ERR_ROOT_ERR_SRC; i += 4, ptr++)
			if (pci_read_config_dword(pdev, pos + i, ptr))
				goto err_read;
		print_hex_dump(KERN_ERR, prefix, DUMP_PREFIX_OFFSET, 32,
			       4, buf, i, 0);
	}
	goto out;

err_read:
	print_hex_dump(KERN_ERR, prefix, DUMP_PREFIX_OFFSET, 32, 4, buf, i, 0);
	IWL_ERR(trans, "Read failed at 0x%X\n", i);
out:
	trans_pcie->pcie_dbg_dumped_once = 1;
	kfree(buf);
}

static void iwl_trans_pcie_sw_reset(struct iwl_trans *trans)
{
	/* Reset entire device - do controller reset (results in SHRD_HW_RST) */
	iwl_set_bit(trans, CSR_RESET, CSR_RESET_REG_FLAG_SW_RESET);
	usleep_range(5000, 6000);
}

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

void iwl_pcie_apm_config(struct iwl_trans *trans)
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
	IWL_DEBUG_POWER(trans, "L1 %sabled - LTR %sabled\n",
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
	int ret;

	IWL_DEBUG_INFO(trans, "Init card's basic functions\n");

	/*
	 * Use "set_bit" below rather than "write", to preserve any hardware
	 * bits already set by default after reset.
	 */

	/* Disable L0S exit timer (platform NMI Work/Around) */
	if (trans->cfg->device_family < IWL_DEVICE_FAMILY_8000)
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
	if (trans->cfg->base_params->pll_cfg)
		iwl_set_bit(trans, CSR_ANA_PLL_CFG, CSR50_ANA_PLL_CFG_VAL);

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
		IWL_ERR(trans, "Failed to init the card\n");
		return ret;
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

	return 0;
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

	iwl_trans_pcie_sw_reset(trans);

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

	iwl_trans_pcie_sw_reset(trans);

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

void iwl_pcie_apm_stop_master(struct iwl_trans *trans)
{
	int ret;

	/* stop device's busmaster DMA activity */
	iwl_set_bit(trans, CSR_RESET, CSR_RESET_REG_FLAG_STOP_MASTER);

	ret = iwl_poll_bit(trans, CSR_RESET,
			   CSR_RESET_REG_FLAG_MASTER_DISABLED,
			   CSR_RESET_REG_FLAG_MASTER_DISABLED, 100);
	if (ret < 0)
		IWL_WARN(trans, "Master Disable Timed Out, 100 usec\n");

	IWL_DEBUG_INFO(trans, "stop master\n");
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
		else if (trans->cfg->device_family >= IWL_DEVICE_FAMILY_8000) {
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

	iwl_trans_pcie_sw_reset(trans);

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
	int ret;

	/* nic_init */
	spin_lock(&trans_pcie->irq_lock);
	ret = iwl_pcie_apm_init(trans);
	spin_unlock(&trans_pcie->irq_lock);

	if (ret)
		return ret;

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
int iwl_pcie_prepare_card_hw(struct iwl_trans *trans)
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
	usleep_range(1000, 2000);

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
static void iwl_pcie_load_firmware_chunk_fh(struct iwl_trans *trans,
					    u32 dst_addr, dma_addr_t phy_addr,
					    u32 byte_cnt)
{
	iwl_write32(trans, FH_TCSR_CHNL_TX_CONFIG_REG(FH_SRVC_CHNL),
		    FH_TCSR_TX_CONFIG_REG_VAL_DMA_CHNL_PAUSE);

	iwl_write32(trans, FH_SRVC_CHNL_SRAM_ADDR_REG(FH_SRVC_CHNL),
		    dst_addr);

	iwl_write32(trans, FH_TFDIB_CTRL0_REG(FH_SRVC_CHNL),
		    phy_addr & FH_MEM_TFDIB_DRAM_ADDR_LSB_MSK);

	iwl_write32(trans, FH_TFDIB_CTRL1_REG(FH_SRVC_CHNL),
		    (iwl_get_dma_hi_addr(phy_addr)
			<< FH_MEM_TFDIB_REG1_ADDR_BITSHIFT) | byte_cnt);

	iwl_write32(trans, FH_TCSR_CHNL_TX_BUF_STS_REG(FH_SRVC_CHNL),
		    BIT(FH_TCSR_CHNL_TX_BUF_STS_REG_POS_TB_NUM) |
		    BIT(FH_TCSR_CHNL_TX_BUF_STS_REG_POS_TB_IDX) |
		    FH_TCSR_CHNL_TX_BUF_STS_REG_VAL_TFDB_VALID);

	iwl_write32(trans, FH_TCSR_CHNL_TX_CONFIG_REG(FH_SRVC_CHNL),
		    FH_TCSR_TX_CONFIG_REG_VAL_DMA_CHNL_ENABLE |
		    FH_TCSR_TX_CONFIG_REG_VAL_DMA_CREDIT_DISABLE |
		    FH_TCSR_TX_CONFIG_REG_VAL_CIRQ_HOST_ENDTFD);
}

static int iwl_pcie_load_firmware_chunk(struct iwl_trans *trans,
					u32 dst_addr, dma_addr_t phy_addr,
					u32 byte_cnt)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	unsigned long flags;
	int ret;

	trans_pcie->ucode_write_complete = false;

	if (!iwl_trans_grab_nic_access(trans, &flags))
		return -EIO;

	iwl_pcie_load_firmware_chunk_fh(trans, dst_addr, phy_addr,
					byte_cnt);
	iwl_trans_release_nic_access(trans, &flags);

	ret = wait_event_timeout(trans_pcie->ucode_write_waitq,
				 trans_pcie->ucode_write_complete, 5 * HZ);
	if (!ret) {
		IWL_ERR(trans, "Failed to load firmware chunk!\n");
		iwl_trans_pcie_dump_regs(trans);
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

	for (i = *first_ucode_section; i < image->num_sec; i++) {
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

		/* Notify ucode of loaded section number and status */
		val = iwl_read_direct32(trans, FH_UCODE_LOAD_STATUS);
		val = val | (sec_num << shift_param);
		iwl_write_direct32(trans, FH_UCODE_LOAD_STATUS, val);

		sec_num = (sec_num << 1) | 0x1;
	}

	*first_ucode_section = last_read_idx;

	iwl_enable_interrupts(trans);

	if (trans->cfg->use_tfh) {
		if (cpu == 1)
			iwl_write_prph(trans, UREG_UCODE_LOAD_STATUS,
				       0xFFFF);
		else
			iwl_write_prph(trans, UREG_UCODE_LOAD_STATUS,
				       0xFFFFFFFF);
	} else {
		if (cpu == 1)
			iwl_write_direct32(trans, FH_UCODE_LOAD_STATUS,
					   0xFFFF);
		else
			iwl_write_direct32(trans, FH_UCODE_LOAD_STATUS,
					   0xFFFFFFFF);
	}

	return 0;
}

static int iwl_pcie_load_cpu_sections(struct iwl_trans *trans,
				      const struct fw_img *image,
				      int cpu,
				      int *first_ucode_section)
{
	int i, ret = 0;
	u32 last_read_idx = 0;

	if (cpu == 1)
		*first_ucode_section = 0;
	else
		(*first_ucode_section)++;

	for (i = *first_ucode_section; i < image->num_sec; i++) {
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

	*first_ucode_section = last_read_idx;

	return 0;
}

void iwl_pcie_apply_destination(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	const struct iwl_fw_dbg_dest_tlv_v1 *dest = trans->dbg_dest_tlv;
	int i;

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
		if (trans->cfg->device_family >= IWL_DEVICE_FAMILY_8000)
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

	iwl_enable_interrupts(trans);

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

	IWL_DEBUG_POWER(trans, "Original WFPM value = 0x%08X\n",
			iwl_read_prph(trans, WFPM_GP2));

	/*
	 * Set default value. On resume reading the values that were
	 * zeored can provide debug data on the resume flow.
	 * This is for debugging only and has no functional impact.
	 */
	iwl_write_prph(trans, WFPM_GP2, 0x01010101);

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

bool iwl_pcie_check_hw_rf_kill(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie =  IWL_TRANS_GET_PCIE_TRANS(trans);
	bool hw_rfkill = iwl_is_rfkill_set(trans);
	bool prev = test_bit(STATUS_RFKILL_OPMODE, &trans->status);
	bool report;

	if (hw_rfkill) {
		set_bit(STATUS_RFKILL_HW, &trans->status);
		set_bit(STATUS_RFKILL_OPMODE, &trans->status);
	} else {
		clear_bit(STATUS_RFKILL_HW, &trans->status);
		if (trans_pcie->opmode_down)
			clear_bit(STATUS_RFKILL_OPMODE, &trans->status);
	}

	report = test_bit(STATUS_RFKILL_OPMODE, &trans->status);

	if (prev != report)
		iwl_trans_pcie_rf_kill(trans, report);

	return hw_rfkill;
}

struct iwl_causes_list {
	u32 cause_num;
	u32 mask_reg;
	u8 addr;
};

static struct iwl_causes_list causes_list[] = {
	{MSIX_FH_INT_CAUSES_D2S_CH0_NUM,	CSR_MSIX_FH_INT_MASK_AD, 0},
	{MSIX_FH_INT_CAUSES_D2S_CH1_NUM,	CSR_MSIX_FH_INT_MASK_AD, 0x1},
	{MSIX_FH_INT_CAUSES_S2D,		CSR_MSIX_FH_INT_MASK_AD, 0x3},
	{MSIX_FH_INT_CAUSES_FH_ERR,		CSR_MSIX_FH_INT_MASK_AD, 0x5},
	{MSIX_HW_INT_CAUSES_REG_ALIVE,		CSR_MSIX_HW_INT_MASK_AD, 0x10},
	{MSIX_HW_INT_CAUSES_REG_WAKEUP,		CSR_MSIX_HW_INT_MASK_AD, 0x11},
	{MSIX_HW_INT_CAUSES_REG_CT_KILL,	CSR_MSIX_HW_INT_MASK_AD, 0x16},
	{MSIX_HW_INT_CAUSES_REG_RF_KILL,	CSR_MSIX_HW_INT_MASK_AD, 0x17},
	{MSIX_HW_INT_CAUSES_REG_PERIODIC,	CSR_MSIX_HW_INT_MASK_AD, 0x18},
	{MSIX_HW_INT_CAUSES_REG_SW_ERR,		CSR_MSIX_HW_INT_MASK_AD, 0x29},
	{MSIX_HW_INT_CAUSES_REG_SCD,		CSR_MSIX_HW_INT_MASK_AD, 0x2A},
	{MSIX_HW_INT_CAUSES_REG_FH_TX,		CSR_MSIX_HW_INT_MASK_AD, 0x2B},
	{MSIX_HW_INT_CAUSES_REG_HW_ERR,		CSR_MSIX_HW_INT_MASK_AD, 0x2D},
	{MSIX_HW_INT_CAUSES_REG_HAP,		CSR_MSIX_HW_INT_MASK_AD, 0x2E},
};

static void iwl_pcie_map_non_rx_causes(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie =  IWL_TRANS_GET_PCIE_TRANS(trans);
	int val = trans_pcie->def_irq | MSIX_NON_AUTO_CLEAR_CAUSE;
	int i;

	/*
	 * Access all non RX causes and map them to the default irq.
	 * In case we are missing at least one interrupt vector,
	 * the first interrupt vector will serve non-RX and FBQ causes.
	 */
	for (i = 0; i < ARRAY_SIZE(causes_list); i++) {
		iwl_write8(trans, CSR_MSIX_IVAR(causes_list[i].addr), val);
		iwl_clear_bit(trans, causes_list[i].mask_reg,
			      causes_list[i].cause_num);
	}
}

static void iwl_pcie_map_rx_causes(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	u32 offset =
		trans_pcie->shared_vec_mask & IWL_SHARED_IRQ_FIRST_RSS ? 1 : 0;
	u32 val, idx;

	/*
	 * The first RX queue - fallback queue, which is designated for
	 * management frame, command responses etc, is always mapped to the
	 * first interrupt vector. The other RX queues are mapped to
	 * the other (N - 2) interrupt vectors.
	 */
	val = BIT(MSIX_FH_INT_CAUSES_Q(0));
	for (idx = 1; idx < trans->num_rx_queues; idx++) {
		iwl_write8(trans, CSR_MSIX_RX_IVAR(idx),
			   MSIX_FH_INT_CAUSES_Q(idx - offset));
		val |= BIT(MSIX_FH_INT_CAUSES_Q(idx));
	}
	iwl_write32(trans, CSR_MSIX_FH_INT_MASK_AD, ~val);

	val = MSIX_FH_INT_CAUSES_Q(0);
	if (trans_pcie->shared_vec_mask & IWL_SHARED_IRQ_NON_RX)
		val |= MSIX_NON_AUTO_CLEAR_CAUSE;
	iwl_write8(trans, CSR_MSIX_RX_IVAR(0), val);

	if (trans_pcie->shared_vec_mask & IWL_SHARED_IRQ_FIRST_RSS)
		iwl_write8(trans, CSR_MSIX_RX_IVAR(1), val);
}

void iwl_pcie_conf_msix_hw(struct iwl_trans_pcie *trans_pcie)
{
	struct iwl_trans *trans = trans_pcie->trans;

	if (!trans_pcie->msix_enabled) {
		if (trans->cfg->mq_rx_supported &&
		    test_bit(STATUS_DEVICE_ENABLED, &trans->status))
			iwl_write_prph(trans, UREG_CHICK,
				       UREG_CHICK_MSI_ENABLE);
		return;
	}
	/*
	 * The IVAR table needs to be configured again after reset,
	 * but if the device is disabled, we can't write to
	 * prph.
	 */
	if (test_bit(STATUS_DEVICE_ENABLED, &trans->status))
		iwl_write_prph(trans, UREG_CHICK, UREG_CHICK_MSIX_ENABLE);

	/*
	 * Each cause from the causes list above and the RX causes is
	 * represented as a byte in the IVAR table. The first nibble
	 * represents the bound interrupt vector of the cause, the second
	 * represents no auto clear for this cause. This will be set if its
	 * interrupt vector is bound to serve other causes.
	 */
	iwl_pcie_map_rx_causes(trans);

	iwl_pcie_map_non_rx_causes(trans);
}

static void iwl_pcie_init_msix(struct iwl_trans_pcie *trans_pcie)
{
	struct iwl_trans *trans = trans_pcie->trans;

	iwl_pcie_conf_msix_hw(trans_pcie);

	if (!trans_pcie->msix_enabled)
		return;

	trans_pcie->fh_init_mask = ~iwl_read32(trans, CSR_MSIX_FH_INT_MASK_AD);
	trans_pcie->fh_mask = trans_pcie->fh_init_mask;
	trans_pcie->hw_init_mask = ~iwl_read32(trans, CSR_MSIX_HW_INT_MASK_AD);
	trans_pcie->hw_mask = trans_pcie->hw_init_mask;
}

static void _iwl_trans_pcie_stop_device(struct iwl_trans *trans, bool low_power)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	lockdep_assert_held(&trans_pcie->mutex);

	if (trans_pcie->is_down)
		return;

	trans_pcie->is_down = true;

	/* Stop dbgc before stopping device */
	if (trans->cfg->device_family == IWL_DEVICE_FAMILY_7000) {
		iwl_set_bits_prph(trans, MON_BUFF_SAMPLE_CTL, 0x100);
	} else {
		iwl_write_prph(trans, DBGC_IN_SAMPLE, 0);
		udelay(100);
		iwl_write_prph(trans, DBGC_OUT_CTRL, 0);
	}

	/* tell the device to stop sending interrupts */
	iwl_disable_interrupts(trans);

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

	iwl_trans_pcie_sw_reset(trans);

	/*
	 * Upon stop, the IVAR table gets erased, so msi-x won't
	 * work. This causes a bug in RF-KILL flows, since the interrupt
	 * that enables radio won't fire on the correct irq, and the
	 * driver won't be able to handle the interrupt.
	 * Configure the IVAR table again after reset.
	 */
	iwl_pcie_conf_msix_hw(trans_pcie);

	/*
	 * Upon stop, the APM issues an interrupt if HW RF kill is set.
	 * This is a bug in certain verions of the hardware.
	 * Certain devices also keep sending HW RF kill interrupt all
	 * the time, unless the interrupt is ACKed even if the interrupt
	 * should be masked. Re-ACK all the interrupts here.
	 */
	iwl_disable_interrupts(trans);

	/* clear all status bits */
	clear_bit(STATUS_SYNC_HCMD_ACTIVE, &trans->status);
	clear_bit(STATUS_INT_ENABLED, &trans->status);
	clear_bit(STATUS_TPOWER_PMI, &trans->status);

	/*
	 * Even if we stop the HW, we still want the RF kill
	 * interrupt
	 */
	iwl_enable_rfkill_int(trans);

	/* re-take ownership to prevent other users from stealing the device */
	iwl_pcie_prepare_card_hw(trans);
}

void iwl_pcie_synchronize_irqs(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	if (trans_pcie->msix_enabled) {
		int i;

		for (i = 0; i < trans_pcie->alloc_vecs; i++)
			synchronize_irq(trans_pcie->msix_entries[i].vector);
	} else {
		synchronize_irq(trans_pcie->pci_dev->irq);
	}
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
	iwl_pcie_synchronize_irqs(trans);

	mutex_lock(&trans_pcie->mutex);

	/* If platform's RF_KILL switch is NOT set to KILL */
	hw_rfkill = iwl_pcie_check_hw_rf_kill(trans);
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
	if (trans->cfg->device_family >= IWL_DEVICE_FAMILY_8000)
		ret = iwl_pcie_load_given_ucode_8000(trans, fw);
	else
		ret = iwl_pcie_load_given_ucode(trans, fw);

	/* re-check RF-Kill state since we may have missed the interrupt */
	hw_rfkill = iwl_pcie_check_hw_rf_kill(trans);
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

void iwl_trans_pcie_handle_stop_rfkill(struct iwl_trans *trans,
				       bool was_in_rfkill)
{
	bool hw_rfkill;

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
	if (hw_rfkill) {
		set_bit(STATUS_RFKILL_HW, &trans->status);
		set_bit(STATUS_RFKILL_OPMODE, &trans->status);
	} else {
		clear_bit(STATUS_RFKILL_HW, &trans->status);
		clear_bit(STATUS_RFKILL_OPMODE, &trans->status);
	}
	if (hw_rfkill != was_in_rfkill)
		iwl_trans_pcie_rf_kill(trans, hw_rfkill);
}

static void iwl_trans_pcie_stop_device(struct iwl_trans *trans, bool low_power)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	bool was_in_rfkill;

	mutex_lock(&trans_pcie->mutex);
	trans_pcie->opmode_down = true;
	was_in_rfkill = test_bit(STATUS_RFKILL_OPMODE, &trans->status);
	_iwl_trans_pcie_stop_device(trans, low_power);
	iwl_trans_pcie_handle_stop_rfkill(trans, was_in_rfkill);
	mutex_unlock(&trans_pcie->mutex);
}

void iwl_trans_pcie_rf_kill(struct iwl_trans *trans, bool state)
{
	struct iwl_trans_pcie __maybe_unused *trans_pcie =
		IWL_TRANS_GET_PCIE_TRANS(trans);

	lockdep_assert_held(&trans_pcie->mutex);

	IWL_WARN(trans, "reporting RF_KILL (radio %s)\n",
		 state ? "disabled" : "enabled");
	if (iwl_op_mode_hw_rf_kill(trans->op_mode, state)) {
		if (trans->cfg->gen2)
			_iwl_trans_pcie_gen2_stop_device(trans, true);
		else
			_iwl_trans_pcie_stop_device(trans, true);
	}
}

static void iwl_trans_pcie_d3_suspend(struct iwl_trans *trans, bool test,
				      bool reset)
{
	if (!reset) {
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

	iwl_pcie_synchronize_irqs(trans);

	iwl_clear_bit(trans, CSR_GP_CNTRL,
		      CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);
	iwl_clear_bit(trans, CSR_GP_CNTRL,
		      CSR_GP_CNTRL_REG_FLAG_INIT_DONE);

	iwl_pcie_enable_rx_wake(trans, false);

	if (reset) {
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
				    bool test,  bool reset)
{
	struct iwl_trans_pcie *trans_pcie =  IWL_TRANS_GET_PCIE_TRANS(trans);
	u32 val;
	int ret;

	if (test) {
		iwl_enable_interrupts(trans);
		*status = IWL_D3_STATUS_ALIVE;
		return 0;
	}

	iwl_pcie_enable_rx_wake(trans, true);

	/*
	 * Reconfigure IVAR table in case of MSIX or reset ict table in
	 * MSI mode since HW reset erased it.
	 * Also enables interrupts - none will happen as
	 * the device doesn't know we're waking it up, only when
	 * the opmode actually tells it after this call.
	 */
	iwl_pcie_conf_msix_hw(trans_pcie);
	if (!trans_pcie->msix_enabled)
		iwl_pcie_reset_ict(trans);
	iwl_enable_interrupts(trans);

	iwl_set_bit(trans, CSR_GP_CNTRL, CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);
	iwl_set_bit(trans, CSR_GP_CNTRL, CSR_GP_CNTRL_REG_FLAG_INIT_DONE);

	if (trans->cfg->device_family >= IWL_DEVICE_FAMILY_8000)
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

	if (!reset) {
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

	IWL_DEBUG_POWER(trans, "WFPM value upon resume = 0x%08X\n",
			iwl_read_prph(trans, WFPM_GP2));

	val = iwl_read32(trans, CSR_RESET);
	if (val & CSR_RESET_REG_FLAG_NEVO_RESET)
		*status = IWL_D3_STATUS_RESET;
	else
		*status = IWL_D3_STATUS_ALIVE;

	return 0;
}

static void iwl_pcie_set_interrupt_capa(struct pci_dev *pdev,
					struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	int max_irqs, num_irqs, i, ret, nr_online_cpus;
	u16 pci_cmd;

	if (!trans->cfg->mq_rx_supported)
		goto enable_msi;

	nr_online_cpus = num_online_cpus();
	max_irqs = min_t(u32, nr_online_cpus + 2, IWL_MAX_RX_HW_QUEUES);
	for (i = 0; i < max_irqs; i++)
		trans_pcie->msix_entries[i].entry = i;

	num_irqs = pci_enable_msix_range(pdev, trans_pcie->msix_entries,
					 MSIX_MIN_INTERRUPT_VECTORS,
					 max_irqs);
	if (num_irqs < 0) {
		IWL_DEBUG_INFO(trans,
			       "Failed to enable msi-x mode (ret %d). Moving to msi mode.\n",
			       num_irqs);
		goto enable_msi;
	}
	trans_pcie->def_irq = (num_irqs == max_irqs) ? num_irqs - 1 : 0;

	IWL_DEBUG_INFO(trans,
		       "MSI-X enabled. %d interrupt vectors were allocated\n",
		       num_irqs);

	/*
	 * In case the OS provides fewer interrupts than requested, different
	 * causes will share the same interrupt vector as follows:
	 * One interrupt less: non rx causes shared with FBQ.
	 * Two interrupts less: non rx causes shared with FBQ and RSS.
	 * More than two interrupts: we will use fewer RSS queues.
	 */
	if (num_irqs <= nr_online_cpus) {
		trans_pcie->trans->num_rx_queues = num_irqs + 1;
		trans_pcie->shared_vec_mask = IWL_SHARED_IRQ_NON_RX |
			IWL_SHARED_IRQ_FIRST_RSS;
	} else if (num_irqs == nr_online_cpus + 1) {
		trans_pcie->trans->num_rx_queues = num_irqs;
		trans_pcie->shared_vec_mask = IWL_SHARED_IRQ_NON_RX;
	} else {
		trans_pcie->trans->num_rx_queues = num_irqs - 1;
	}

	trans_pcie->alloc_vecs = num_irqs;
	trans_pcie->msix_enabled = true;
	return;

enable_msi:
	ret = pci_enable_msi(pdev);
	if (ret) {
		dev_err(&pdev->dev, "pci_enable_msi failed - %d\n", ret);
		/* enable rfkill interrupt: hw bug w/a */
		pci_read_config_word(pdev, PCI_COMMAND, &pci_cmd);
		if (pci_cmd & PCI_COMMAND_INTX_DISABLE) {
			pci_cmd &= ~PCI_COMMAND_INTX_DISABLE;
			pci_write_config_word(pdev, PCI_COMMAND, pci_cmd);
		}
	}
}

static void iwl_pcie_irq_set_affinity(struct iwl_trans *trans)
{
	int iter_rx_q, i, ret, cpu, offset;
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	i = trans_pcie->shared_vec_mask & IWL_SHARED_IRQ_FIRST_RSS ? 0 : 1;
	iter_rx_q = trans_pcie->trans->num_rx_queues - 1 + i;
	offset = 1 + i;
	for (; i < iter_rx_q ; i++) {
		/*
		 * Get the cpu prior to the place to search
		 * (i.e. return will be > i - 1).
		 */
		cpu = cpumask_next(i - offset, cpu_online_mask);
		cpumask_set_cpu(cpu, &trans_pcie->affinity_mask[i]);
		ret = irq_set_affinity_hint(trans_pcie->msix_entries[i].vector,
					    &trans_pcie->affinity_mask[i]);
		if (ret)
			IWL_ERR(trans_pcie->trans,
				"Failed to set affinity mask for IRQ %d\n",
				i);
	}
}

static const char *queue_name(struct device *dev,
			      struct iwl_trans_pcie *trans_p, int i)
{
	if (trans_p->shared_vec_mask) {
		int vec = trans_p->shared_vec_mask &
			  IWL_SHARED_IRQ_FIRST_RSS ? 1 : 0;

		if (i == 0)
			return DRV_NAME ": shared IRQ";

		return devm_kasprintf(dev, GFP_KERNEL,
				      DRV_NAME ": queue %d", i + vec);
	}
	if (i == 0)
		return DRV_NAME ": default queue";

	if (i == trans_p->alloc_vecs - 1)
		return DRV_NAME ": exception";

	return devm_kasprintf(dev, GFP_KERNEL,
			      DRV_NAME  ": queue %d", i);
}

static int iwl_pcie_init_msix_handler(struct pci_dev *pdev,
				      struct iwl_trans_pcie *trans_pcie)
{
	int i;

	for (i = 0; i < trans_pcie->alloc_vecs; i++) {
		int ret;
		struct msix_entry *msix_entry;
		const char *qname = queue_name(&pdev->dev, trans_pcie, i);

		if (!qname)
			return -ENOMEM;

		msix_entry = &trans_pcie->msix_entries[i];
		ret = devm_request_threaded_irq(&pdev->dev,
						msix_entry->vector,
						iwl_pcie_msix_isr,
						(i == trans_pcie->def_irq) ?
						iwl_pcie_irq_msix_handler :
						iwl_pcie_irq_rx_msix_handler,
						IRQF_SHARED,
						qname,
						msix_entry);
		if (ret) {
			IWL_ERR(trans_pcie->trans,
				"Error allocating IRQ %d\n", i);

			return ret;
		}
	}
	iwl_pcie_irq_set_affinity(trans_pcie->trans);

	return 0;
}

static int _iwl_trans_pcie_start_hw(struct iwl_trans *trans, bool low_power)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	int err;

	lockdep_assert_held(&trans_pcie->mutex);

	err = iwl_pcie_prepare_card_hw(trans);
	if (err) {
		IWL_ERR(trans, "Error while preparing HW: %d\n", err);
		return err;
	}

	iwl_trans_pcie_sw_reset(trans);

	err = iwl_pcie_apm_init(trans);
	if (err)
		return err;

	iwl_pcie_init_msix(trans_pcie);

	/* From now on, the op_mode will be kept updated about RF kill state */
	iwl_enable_rfkill_int(trans);

	trans_pcie->opmode_down = false;

	/* Set is_down to false here so that...*/
	trans_pcie->is_down = false;

	/* ...rfkill can call stop_device and set it false if needed */
	iwl_pcie_check_hw_rf_kill(trans);

	/* Make sure we sync here, because we'll need full access later */
	if (low_power)
		pm_runtime_resume(trans->dev);

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
	iwl_disable_interrupts(trans);

	iwl_pcie_apm_stop(trans, true);

	iwl_disable_interrupts(trans);

	iwl_pcie_disable_ict(trans);

	mutex_unlock(&trans_pcie->mutex);

	iwl_pcie_synchronize_irqs(trans);
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

	trans_pcie->bc_table_dword = trans_cfg->bc_table_dword;
	trans_pcie->scd_set_active = trans_cfg->scd_set_active;
	trans_pcie->sw_csum_tx = trans_cfg->sw_csum_tx;

	trans_pcie->page_offs = trans_cfg->cb_data_offs;
	trans_pcie->dev_cmd_offs = trans_cfg->cb_data_offs + sizeof(void *);

	trans->command_groups = trans_cfg->command_groups;
	trans->command_groups_size = trans_cfg->command_groups_size;

	/* Initialize NAPI here - it should be before registering to mac80211
	 * in the opmode but after the HW struct is allocated.
	 * As this function may be called again in some corner cases don't
	 * do anything if NAPI was already initialized.
	 */
	if (trans_pcie->napi_dev.reg_state != NETREG_DUMMY)
		init_dummy_netdev(&trans_pcie->napi_dev);
}

void iwl_trans_pcie_free(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	int i;

	iwl_pcie_synchronize_irqs(trans);

	if (trans->cfg->gen2)
		iwl_pcie_gen2_tx_free(trans);
	else
		iwl_pcie_tx_free(trans);
	iwl_pcie_rx_free(trans);

	if (trans_pcie->rba.alloc_wq) {
		destroy_workqueue(trans_pcie->rba.alloc_wq);
		trans_pcie->rba.alloc_wq = NULL;
	}

	if (trans_pcie->msix_enabled) {
		for (i = 0; i < trans_pcie->alloc_vecs; i++) {
			irq_set_affinity_hint(
				trans_pcie->msix_entries[i].vector,
				NULL);
		}

		trans_pcie->msix_enabled = false;
	} else {
		iwl_pcie_free_ict(trans);
	}

	iwl_pcie_free_fw_monitor(trans);

	for_each_possible_cpu(i) {
		struct iwl_tso_hdr_page *p =
			per_cpu_ptr(trans_pcie->tso_hdr_page, i);

		if (p->page)
			__free_page(p->page);
	}

	free_percpu(trans_pcie->tso_hdr_page);
	mutex_destroy(&trans_pcie->mutex);
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
	if (trans->cfg->device_family >= IWL_DEVICE_FAMILY_8000)
		udelay(2);

	/*
	 * These bits say the device is running, and should keep running for
	 * at least a short while (at least as long as MAC_ACCESS_REQ stays 1),
	 * but they do not indicate that embedded SRAM is restored yet;
	 * HW with volatile SRAM must save/restore contents to/from
	 * host DRAM when sleeping/waking for power-saving.
	 * Each direction takes approximately 1/4 millisecond; with this
	 * overhead, it's a good idea to grab and hold MAC_ACCESS_REQUEST if a
	 * series of register accesses are expected (e.g. reading Event Log),
	 * to keep device from sleeping.
	 *
	 * CSR_UCODE_DRV_GP1 register bit MAC_SLEEP == 0 indicates that
	 * SRAM is okay/restored.  We don't check that here because this call
	 * is just for hardware register access; but GP1 MAC_SLEEP
	 * check is a good idea before accessing the SRAM of HW with
	 * volatile SRAM (e.g. reading Event Log).
	 *
	 * 5000 series and later (including 1000 series) have non-volatile SRAM,
	 * and do not save/restore SRAM when power cycling.
	 */
	ret = iwl_poll_bit(trans, CSR_GP_CNTRL,
			   CSR_GP_CNTRL_REG_VAL_MAC_ACCESS_EN,
			   (CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY |
			    CSR_GP_CNTRL_REG_FLAG_GOING_TO_SLEEP), 15000);
	if (unlikely(ret < 0)) {
		iwl_trans_pcie_dump_regs(trans);
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
		struct iwl_txq *txq = trans_pcie->txq[queue];
		unsigned long now;

		spin_lock_bh(&txq->lock);

		now = jiffies;

		if (txq->frozen == freeze)
			goto next_queue;

		IWL_DEBUG_TX_QUEUES(trans, "%s TXQ %d\n",
				    freeze ? "Freezing" : "Waking", queue);

		txq->frozen = freeze;

		if (txq->read_ptr == txq->write_ptr)
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
		struct iwl_txq *txq = trans_pcie->txq[i];

		if (i == trans_pcie->cmd_queue)
			continue;

		spin_lock_bh(&txq->lock);

		if (!block && !(WARN_ON_ONCE(!txq->block))) {
			txq->block--;
			if (!txq->block) {
				iwl_write32(trans, HBUS_TARG_WRPTR,
					    txq->write_ptr | (i << 8));
			}
		} else if (block) {
			txq->block++;
		}

		spin_unlock_bh(&txq->lock);
	}
}

#define IWL_FLUSH_WAIT_MS	2000

void iwl_trans_pcie_log_scd_error(struct iwl_trans *trans, struct iwl_txq *txq)
{
	u32 txq_id = txq->id;
	u32 status;
	bool active;
	u8 fifo;

	if (trans->cfg->use_tfh) {
		IWL_ERR(trans, "Queue %d is stuck %d %d\n", txq_id,
			txq->read_ptr, txq->write_ptr);
		/* TODO: access new SCD registers and dump them */
		return;
	}

	status = iwl_read_prph(trans, SCD_QUEUE_STATUS_BITS(txq_id));
	fifo = (status >> SCD_QUEUE_STTS_REG_POS_TXF) & 0x7;
	active = !!(status & BIT(SCD_QUEUE_STTS_REG_POS_ACTIVE));

	IWL_ERR(trans,
		"Queue %d is %sactive on fifo %d and stuck for %u ms. SW [%d, %d] HW [%d, %d] FH TRB=0x0%x\n",
		txq_id, active ? "" : "in", fifo,
		jiffies_to_msecs(txq->wd_timeout),
		txq->read_ptr, txq->write_ptr,
		iwl_read_prph(trans, SCD_QUEUE_RDPTR(txq_id)) &
			(TFD_QUEUE_SIZE_MAX - 1),
		iwl_read_prph(trans, SCD_QUEUE_WRPTR(txq_id)) &
			(TFD_QUEUE_SIZE_MAX - 1),
		iwl_read_direct32(trans, FH_TX_TRB_REG(fifo)));
}

static int iwl_trans_pcie_wait_txq_empty(struct iwl_trans *trans, int txq_idx)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_txq *txq;
	unsigned long now = jiffies;
	u8 wr_ptr;

	if (!test_bit(txq_idx, trans_pcie->queue_used))
		return -EINVAL;

	IWL_DEBUG_TX_QUEUES(trans, "Emptying queue %d...\n", txq_idx);
	txq = trans_pcie->txq[txq_idx];
	wr_ptr = READ_ONCE(txq->write_ptr);

	while (txq->read_ptr != READ_ONCE(txq->write_ptr) &&
	       !time_after(jiffies,
			   now + msecs_to_jiffies(IWL_FLUSH_WAIT_MS))) {
		u8 write_ptr = READ_ONCE(txq->write_ptr);

		if (WARN_ONCE(wr_ptr != write_ptr,
			      "WR pointer moved while flushing %d -> %d\n",
			      wr_ptr, write_ptr))
			return -ETIMEDOUT;
		usleep_range(1000, 2000);
	}

	if (txq->read_ptr != txq->write_ptr) {
		IWL_ERR(trans,
			"fail to flush all tx fifo queues Q %d\n", txq_idx);
		iwl_trans_pcie_log_scd_error(trans, txq);
		return -ETIMEDOUT;
	}

	IWL_DEBUG_TX_QUEUES(trans, "Queue %d is now empty.\n", txq_idx);

	return 0;
}

static int iwl_trans_pcie_wait_txqs_empty(struct iwl_trans *trans, u32 txq_bm)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	int cnt;
	int ret = 0;

	/* waiting for all the tx frames complete might take a while */
	for (cnt = 0; cnt < trans->cfg->base_params->num_of_queues; cnt++) {

		if (cnt == trans_pcie->cmd_queue)
			continue;
		if (!test_bit(cnt, trans_pcie->queue_used))
			continue;
		if (!(BIT(cnt) & txq_bm))
			continue;

		ret = iwl_trans_pcie_wait_txq_empty(trans, cnt);
		if (ret)
			break;
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

static void iwl_trans_pcie_ref(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	if (iwlwifi_mod_params.d0i3_disable)
		return;

	pm_runtime_get(&trans_pcie->pci_dev->dev);

#ifdef CONFIG_PM
	IWL_DEBUG_RPM(trans, "runtime usage count: %d\n",
		      atomic_read(&trans_pcie->pci_dev->dev.power.usage_count));
#endif /* CONFIG_PM */
}

static void iwl_trans_pcie_unref(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	if (iwlwifi_mod_params.d0i3_disable)
		return;

	pm_runtime_mark_last_busy(&trans_pcie->pci_dev->dev);
	pm_runtime_put_autosuspend(&trans_pcie->pci_dev->dev);

#ifdef CONFIG_PM
	IWL_DEBUG_RPM(trans, "runtime usage count: %d\n",
		      atomic_read(&trans_pcie->pci_dev->dev.power.usage_count));
#endif /* CONFIG_PM */
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
	char *buf;
	int pos = 0;
	int cnt;
	int ret;
	size_t bufsz;

	bufsz = sizeof(char) * 75 * trans->cfg->base_params->num_of_queues;

	if (!trans_pcie->txq_memory)
		return -EAGAIN;

	buf = kzalloc(bufsz, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	for (cnt = 0; cnt < trans->cfg->base_params->num_of_queues; cnt++) {
		txq = trans_pcie->txq[cnt];
		pos += scnprintf(buf + pos, bufsz - pos,
				"hwq %.2d: read=%u write=%u use=%d stop=%d need_update=%d frozen=%d%s\n",
				cnt, txq->read_ptr, txq->write_ptr,
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
	char *buf;
	int pos = 0, i, ret;
	size_t bufsz = sizeof(buf);

	bufsz = sizeof(char) * 121 * trans->num_rx_queues;

	if (!trans_pcie->rxq)
		return -EAGAIN;

	buf = kzalloc(bufsz, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	for (i = 0; i < trans->num_rx_queues && pos < bufsz; i++) {
		struct iwl_rxq *rxq = &trans_pcie->rxq[i];

		pos += scnprintf(buf + pos, bufsz - pos, "queue#: %2d\n",
				 i);
		pos += scnprintf(buf + pos, bufsz - pos, "\tread: %u\n",
				 rxq->read);
		pos += scnprintf(buf + pos, bufsz - pos, "\twrite: %u\n",
				 rxq->write);
		pos += scnprintf(buf + pos, bufsz - pos, "\twrite_actual: %u\n",
				 rxq->write_actual);
		pos += scnprintf(buf + pos, bufsz - pos, "\tneed_update: %2d\n",
				 rxq->need_update);
		pos += scnprintf(buf + pos, bufsz - pos, "\tfree_count: %u\n",
				 rxq->free_count);
		if (rxq->rb_stts) {
			pos += scnprintf(buf + pos, bufsz - pos,
					 "\tclosed_rb_num: %u\n",
					 le16_to_cpu(rxq->rb_stts->closed_rb_num) &
					 0x0FFF);
		} else {
			pos += scnprintf(buf + pos, bufsz - pos,
					 "\tclosed_rb_num: Not Allocated\n");
		}
	}
	ret = simple_read_from_buffer(user_buf, count, ppos, buf, pos);
	kfree(buf);

	return ret;
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
	u32 reset_flag;
	int ret;

	ret = kstrtou32_from_user(user_buf, count, 16, &reset_flag);
	if (ret)
		return ret;
	if (reset_flag == 0)
		memset(isr_stats, 0, sizeof(*isr_stats));

	return count;
}

static ssize_t iwl_dbgfs_csr_write(struct file *file,
				   const char __user *user_buf,
				   size_t count, loff_t *ppos)
{
	struct iwl_trans *trans = file->private_data;

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

static ssize_t iwl_dbgfs_rfkill_read(struct file *file,
				     char __user *user_buf,
				     size_t count, loff_t *ppos)
{
	struct iwl_trans *trans = file->private_data;
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	char buf[100];
	int pos;

	pos = scnprintf(buf, sizeof(buf), "debug: %d\nhw: %d\n",
			trans_pcie->debug_rfkill,
			!(iwl_read32(trans, CSR_GP_CNTRL) &
				CSR_GP_CNTRL_REG_FLAG_HW_RF_KILL_SW));

	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

static ssize_t iwl_dbgfs_rfkill_write(struct file *file,
				      const char __user *user_buf,
				      size_t count, loff_t *ppos)
{
	struct iwl_trans *trans = file->private_data;
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	bool old = trans_pcie->debug_rfkill;
	int ret;

	ret = kstrtobool_from_user(user_buf, count, &trans_pcie->debug_rfkill);
	if (ret)
		return ret;
	if (old == trans_pcie->debug_rfkill)
		return count;
	IWL_WARN(trans, "changing debug rfkill %d->%d\n",
		 old, trans_pcie->debug_rfkill);
	iwl_pcie_handle_rfkill_irq(trans);

	return count;
}

DEBUGFS_READ_WRITE_FILE_OPS(interrupt);
DEBUGFS_READ_FILE_OPS(fh_reg);
DEBUGFS_READ_FILE_OPS(rx_queue);
DEBUGFS_READ_FILE_OPS(tx_queue);
DEBUGFS_WRITE_FILE_OPS(csr);
DEBUGFS_READ_WRITE_FILE_OPS(rfkill);

/* Create the debugfs files and directories */
int iwl_trans_pcie_dbgfs_register(struct iwl_trans *trans)
{
	struct dentry *dir = trans->dbgfs_dir;

	DEBUGFS_ADD_FILE(rx_queue, dir, S_IRUSR);
	DEBUGFS_ADD_FILE(tx_queue, dir, S_IRUSR);
	DEBUGFS_ADD_FILE(interrupt, dir, S_IWUSR | S_IRUSR);
	DEBUGFS_ADD_FILE(csr, dir, S_IWUSR);
	DEBUGFS_ADD_FILE(fh_reg, dir, S_IRUSR);
	DEBUGFS_ADD_FILE(rfkill, dir, S_IWUSR | S_IRUSR);
	return 0;

err:
	IWL_ERR(trans, "failed to create the trans debugfs entry\n");
	return -ENOMEM;
}
#endif /*CONFIG_IWLWIFI_DEBUGFS */

static u32 iwl_trans_pcie_get_cmdlen(struct iwl_trans *trans, void *tfd)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	u32 cmdlen = 0;
	int i;

	for (i = 0; i < trans_pcie->max_tbs; i++)
		cmdlen += iwl_pcie_tfd_tb_get_len(trans, tfd, i);

	return cmdlen;
}

static u32 iwl_trans_pcie_dump_rbs(struct iwl_trans *trans,
				   struct iwl_fw_error_dump_data **data,
				   int allocated_rb_nums)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	int max_len = PAGE_SIZE << trans_pcie->rx_page_order;
	/* Dump RBs is supported only for pre-9000 devices (1 queue) */
	struct iwl_rxq *rxq = &trans_pcie->rxq[0];
	u32 i, r, j, rb_len = 0;

	spin_lock(&rxq->lock);

	r = le16_to_cpu(READ_ONCE(rxq->rb_stts->closed_rb_num)) & 0x0FFF;

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

	if (!trans->cfg->gen2)
		for (i = FH_MEM_LOWER_BOUND; i < FH_MEM_UPPER_BOUND;
		     i += sizeof(u32))
			*val++ = cpu_to_le32(iwl_trans_pcie_read32(trans, i));
	else
		for (i = FH_MEM_LOWER_BOUND_GEN2; i < FH_MEM_UPPER_BOUND_GEN2;
		     i += sizeof(u32))
			*val++ = cpu_to_le32(iwl_trans_pcie_read_prph(trans,
								      i));

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
			if (trans->dbg_dest_tlv->version) {
				base = (iwl_read_prph(trans, base) &
					IWL_LDBG_M2S_BUF_BA_MSK) <<
				       trans->dbg_dest_tlv->base_shift;
				base *= IWL_M2S_UNIT_SIZE;
				base += trans->cfg->smem_offset;
			} else {
				base = iwl_read_prph(trans, base) <<
				       trans->dbg_dest_tlv->base_shift;
			}

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
	struct iwl_txq *cmdq = trans_pcie->txq[trans_pcie->cmd_queue];
	struct iwl_fw_error_dump_txcmd *txcmd;
	struct iwl_trans_dump_data *dump_data;
	u32 len, num_rbs;
	u32 monitor_len;
	int i, ptr;
	bool dump_rbs = test_bit(STATUS_FW_ERROR, &trans->status) &&
			!trans->cfg->mq_rx_supported;

	/* transport dump header */
	len = sizeof(*dump_data);

	/* host commands */
	len += sizeof(*data) +
		cmdq->n_window * (sizeof(*txcmd) + TFD_MAX_PAYLOAD_SIZE);

	/* FW monitor */
	if (trans_pcie->fw_mon_page) {
		len += sizeof(*data) + sizeof(struct iwl_fw_error_dump_fw_mon) +
		       trans_pcie->fw_mon_size;
		monitor_len = trans_pcie->fw_mon_size;
	} else if (trans->dbg_dest_tlv) {
		u32 base, end, cfg_reg;

		if (trans->dbg_dest_tlv->version == 1) {
			cfg_reg = le32_to_cpu(trans->dbg_dest_tlv->base_reg);
			cfg_reg = iwl_read_prph(trans, cfg_reg);
			base = (cfg_reg & IWL_LDBG_M2S_BUF_BA_MSK) <<
				trans->dbg_dest_tlv->base_shift;
			base *= IWL_M2S_UNIT_SIZE;
			base += trans->cfg->smem_offset;

			monitor_len =
				(cfg_reg & IWL_LDBG_M2S_BUF_SIZE_MSK) >>
				trans->dbg_dest_tlv->end_shift;
			monitor_len *= IWL_M2S_UNIT_SIZE;
		} else {
			base = le32_to_cpu(trans->dbg_dest_tlv->base_reg);
			end = le32_to_cpu(trans->dbg_dest_tlv->end_reg);

			base = iwl_read_prph(trans, base) <<
			       trans->dbg_dest_tlv->base_shift;
			end = iwl_read_prph(trans, end) <<
			      trans->dbg_dest_tlv->end_shift;

			/* Make "end" point to the actual end */
			if (trans->cfg->device_family >=
			    IWL_DEVICE_FAMILY_8000 ||
			    trans->dbg_dest_tlv->monitor_mode == MARBH_MODE)
				end += (1 << trans->dbg_dest_tlv->end_shift);
			monitor_len = end - base;
		}
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
	if (trans->cfg->gen2)
		len += sizeof(*data) +
		       (FH_MEM_UPPER_BOUND_GEN2 - FH_MEM_LOWER_BOUND_GEN2);
	else
		len += sizeof(*data) +
		       (FH_MEM_UPPER_BOUND - FH_MEM_LOWER_BOUND);

	if (dump_rbs) {
		/* Dump RBs is supported only for pre-9000 devices (1 queue) */
		struct iwl_rxq *rxq = &trans_pcie->rxq[0];
		/* RBs */
		num_rbs = le16_to_cpu(READ_ONCE(rxq->rb_stts->closed_rb_num))
				      & 0x0FFF;
		num_rbs = (num_rbs - rxq->read) & RX_QUEUE_MASK;
		len += num_rbs * (sizeof(*data) +
				  sizeof(struct iwl_fw_error_dump_rb) +
				  (PAGE_SIZE << trans_pcie->rx_page_order));
	}

	/* Paged memory for gen2 HW */
	if (trans->cfg->gen2)
		for (i = 0; i < trans_pcie->init_dram.paging_cnt; i++)
			len += sizeof(*data) +
			       sizeof(struct iwl_fw_error_dump_paging) +
			       trans_pcie->init_dram.paging[i].size;

	dump_data = vzalloc(len);
	if (!dump_data)
		return NULL;

	len = 0;
	data = (void *)dump_data->data;
	data->type = cpu_to_le32(IWL_FW_ERROR_DUMP_TXCMD);
	txcmd = (void *)data->data;
	spin_lock_bh(&cmdq->lock);
	ptr = cmdq->write_ptr;
	for (i = 0; i < cmdq->n_window; i++) {
		u8 idx = iwl_pcie_get_cmd_index(cmdq, ptr);
		u32 caplen, cmdlen;

		cmdlen = iwl_trans_pcie_get_cmdlen(trans, cmdq->tfds +
						   trans_pcie->tfd_size * ptr);
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

	/* Paged memory for gen2 HW */
	if (trans->cfg->gen2) {
		for (i = 0; i < trans_pcie->init_dram.paging_cnt; i++) {
			struct iwl_fw_error_dump_paging *paging;
			dma_addr_t addr =
				trans_pcie->init_dram.paging[i].physical;
			u32 page_len = trans_pcie->init_dram.paging[i].size;

			data->type = cpu_to_le32(IWL_FW_ERROR_DUMP_PAGING);
			data->len = cpu_to_le32(sizeof(*paging) + page_len);
			paging = (void *)data->data;
			paging->index = cpu_to_le32(i);
			dma_sync_single_for_cpu(trans->dev, addr, page_len,
						DMA_BIDIRECTIONAL);
			memcpy(paging->data,
			       trans_pcie->init_dram.paging[i].block, page_len);
			data = iwl_fw_error_next_data(data);

			len += sizeof(*data) + sizeof(*paging) + page_len;
		}
	}

	len += iwl_trans_pcie_dump_monitor(trans, &data, monitor_len);

	dump_data->len = len;

	return dump_data;
}

#ifdef CONFIG_PM_SLEEP
static int iwl_trans_pcie_suspend(struct iwl_trans *trans)
{
	if (trans->runtime_pm_mode == IWL_PLAT_PM_MODE_D0I3 &&
	    (trans->system_pm_mode == IWL_PLAT_PM_MODE_D0I3))
		return iwl_pci_fw_enter_d0i3(trans);

	return 0;
}

static void iwl_trans_pcie_resume(struct iwl_trans *trans)
{
	if (trans->runtime_pm_mode == IWL_PLAT_PM_MODE_D0I3 &&
	    (trans->system_pm_mode == IWL_PLAT_PM_MODE_D0I3))
		iwl_pci_fw_exit_d0i3(trans);
}
#endif /* CONFIG_PM_SLEEP */

#define IWL_TRANS_COMMON_OPS						\
	.op_mode_leave = iwl_trans_pcie_op_mode_leave,			\
	.write8 = iwl_trans_pcie_write8,				\
	.write32 = iwl_trans_pcie_write32,				\
	.read32 = iwl_trans_pcie_read32,				\
	.read_prph = iwl_trans_pcie_read_prph,				\
	.write_prph = iwl_trans_pcie_write_prph,			\
	.read_mem = iwl_trans_pcie_read_mem,				\
	.write_mem = iwl_trans_pcie_write_mem,				\
	.configure = iwl_trans_pcie_configure,				\
	.set_pmi = iwl_trans_pcie_set_pmi,				\
	.sw_reset = iwl_trans_pcie_sw_reset,				\
	.grab_nic_access = iwl_trans_pcie_grab_nic_access,		\
	.release_nic_access = iwl_trans_pcie_release_nic_access,	\
	.set_bits_mask = iwl_trans_pcie_set_bits_mask,			\
	.ref = iwl_trans_pcie_ref,					\
	.unref = iwl_trans_pcie_unref,					\
	.dump_data = iwl_trans_pcie_dump_data,				\
	.dump_regs = iwl_trans_pcie_dump_regs,				\
	.d3_suspend = iwl_trans_pcie_d3_suspend,			\
	.d3_resume = iwl_trans_pcie_d3_resume

#ifdef CONFIG_PM_SLEEP
#define IWL_TRANS_PM_OPS						\
	.suspend = iwl_trans_pcie_suspend,				\
	.resume = iwl_trans_pcie_resume,
#else
#define IWL_TRANS_PM_OPS
#endif /* CONFIG_PM_SLEEP */

static const struct iwl_trans_ops trans_ops_pcie = {
	IWL_TRANS_COMMON_OPS,
	IWL_TRANS_PM_OPS
	.start_hw = iwl_trans_pcie_start_hw,
	.fw_alive = iwl_trans_pcie_fw_alive,
	.start_fw = iwl_trans_pcie_start_fw,
	.stop_device = iwl_trans_pcie_stop_device,

	.send_cmd = iwl_trans_pcie_send_hcmd,

	.tx = iwl_trans_pcie_tx,
	.reclaim = iwl_trans_pcie_reclaim,

	.txq_disable = iwl_trans_pcie_txq_disable,
	.txq_enable = iwl_trans_pcie_txq_enable,

	.txq_set_shared_mode = iwl_trans_pcie_txq_set_shared_mode,

	.wait_tx_queues_empty = iwl_trans_pcie_wait_txqs_empty,

	.freeze_txq_timer = iwl_trans_pcie_freeze_txq_timer,
	.block_txq_ptrs = iwl_trans_pcie_block_txq_ptrs,
};

static const struct iwl_trans_ops trans_ops_pcie_gen2 = {
	IWL_TRANS_COMMON_OPS,
	IWL_TRANS_PM_OPS
	.start_hw = iwl_trans_pcie_start_hw,
	.fw_alive = iwl_trans_pcie_gen2_fw_alive,
	.start_fw = iwl_trans_pcie_gen2_start_fw,
	.stop_device = iwl_trans_pcie_gen2_stop_device,

	.send_cmd = iwl_trans_pcie_gen2_send_hcmd,

	.tx = iwl_trans_pcie_gen2_tx,
	.reclaim = iwl_trans_pcie_reclaim,

	.txq_alloc = iwl_trans_pcie_dyn_txq_alloc,
	.txq_free = iwl_trans_pcie_dyn_txq_free,
	.wait_txq_empty = iwl_trans_pcie_wait_txq_empty,
};

struct iwl_trans *iwl_trans_pcie_alloc(struct pci_dev *pdev,
				       const struct pci_device_id *ent,
				       const struct iwl_cfg *cfg)
{
	struct iwl_trans_pcie *trans_pcie;
	struct iwl_trans *trans;
	int ret, addr_size;

	ret = pcim_enable_device(pdev);
	if (ret)
		return ERR_PTR(ret);

	if (cfg->gen2)
		trans = iwl_trans_alloc(sizeof(struct iwl_trans_pcie),
					&pdev->dev, cfg, &trans_ops_pcie_gen2);
	else
		trans = iwl_trans_alloc(sizeof(struct iwl_trans_pcie),
					&pdev->dev, cfg, &trans_ops_pcie);
	if (!trans)
		return ERR_PTR(-ENOMEM);

	trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	trans_pcie->trans = trans;
	trans_pcie->opmode_down = true;
	spin_lock_init(&trans_pcie->irq_lock);
	spin_lock_init(&trans_pcie->reg_lock);
	mutex_init(&trans_pcie->mutex);
	init_waitqueue_head(&trans_pcie->ucode_write_waitq);
	trans_pcie->tso_hdr_page = alloc_percpu(struct iwl_tso_hdr_page);
	if (!trans_pcie->tso_hdr_page) {
		ret = -ENOMEM;
		goto out_no_pci;
	}


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

	if (cfg->use_tfh) {
		addr_size = 64;
		trans_pcie->max_tbs = IWL_TFH_NUM_TBS;
		trans_pcie->tfd_size = sizeof(struct iwl_tfh_tfd);
	} else {
		addr_size = 36;
		trans_pcie->max_tbs = IWL_NUM_OF_TBS;
		trans_pcie->tfd_size = sizeof(struct iwl_tfd);
	}
	trans->max_skb_frags = IWL_PCIE_MAX_FRAGS(trans_pcie);

	pci_set_master(pdev);

	ret = pci_set_dma_mask(pdev, DMA_BIT_MASK(addr_size));
	if (!ret)
		ret = pci_set_consistent_dma_mask(pdev,
						  DMA_BIT_MASK(addr_size));
	if (ret) {
		ret = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
		if (!ret)
			ret = pci_set_consistent_dma_mask(pdev,
							  DMA_BIT_MASK(32));
		/* both attempts failed: */
		if (ret) {
			dev_err(&pdev->dev, "No suitable DMA available\n");
			goto out_no_pci;
		}
	}

	ret = pcim_iomap_regions_request_all(pdev, BIT(0), DRV_NAME);
	if (ret) {
		dev_err(&pdev->dev, "pcim_iomap_regions_request_all failed\n");
		goto out_no_pci;
	}

	trans_pcie->hw_base = pcim_iomap_table(pdev)[0];
	if (!trans_pcie->hw_base) {
		dev_err(&pdev->dev, "pcim_iomap_table failed\n");
		ret = -ENODEV;
		goto out_no_pci;
	}

	/* We disable the RETRY_TIMEOUT register (0x41) to keep
	 * PCI Tx retries from interfering with C3 CPU state */
	pci_write_config_byte(pdev, PCI_CFG_RETRY_TIMEOUT, 0x00);

	trans_pcie->pci_dev = pdev;
	iwl_disable_interrupts(trans);

	trans->hw_rev = iwl_read32(trans, CSR_HW_REV);
	/*
	 * In the 8000 HW family the format of the 4 bytes of CSR_HW_REV have
	 * changed, and now the revision step also includes bit 0-1 (no more
	 * "dash" value). To keep hw_rev backwards compatible - we'll store it
	 * in the old format.
	 */
	if (trans->cfg->device_family >= IWL_DEVICE_FAMILY_8000) {
		unsigned long flags;

		trans->hw_rev = (trans->hw_rev & 0xfff0) |
				(CSR_HW_REV_STEP(trans->hw_rev << 2) << 2);

		ret = iwl_pcie_prepare_card_hw(trans);
		if (ret) {
			IWL_WARN(trans, "Exit HW not ready\n");
			goto out_no_pci;
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
			goto out_no_pci;
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

	/*
	 * 9000-series integrated A-step has a problem with suspend/resume
	 * and sometimes even causes the whole platform to get stuck. This
	 * workaround makes the hardware not go into the problematic state.
	 */
	if (trans->cfg->integrated &&
	    trans->cfg->device_family == IWL_DEVICE_FAMILY_9000 &&
	    CSR_HW_REV_STEP(trans->hw_rev) == SILICON_A_STEP)
		iwl_set_bit(trans, CSR_HOST_CHICKEN,
			    CSR_HOST_CHICKEN_PM_IDLE_SRC_DIS_SB_PME);

#if IS_ENABLED(CONFIG_IWLMVM)
	trans->hw_rf_id = iwl_read32(trans, CSR_HW_RF_ID);
	if (trans->hw_rf_id == CSR_HW_RF_ID_TYPE_HR) {
		u32 hw_status;

		hw_status = iwl_read_prph(trans, UMAG_GEN_HW_STATUS);
		if (hw_status & UMAG_GEN_HW_IS_FPGA)
			trans->cfg = &iwl22000_2ax_cfg_qnj_hr_f0;
		else
			trans->cfg = &iwl22000_2ac_cfg_hr;
	}
#endif

	iwl_pcie_set_interrupt_capa(pdev, trans);
	trans->hw_id = (pdev->device << 16) + pdev->subsystem_device;
	snprintf(trans->hw_id_str, sizeof(trans->hw_id_str),
		 "PCI ID: 0x%04X:0x%04X", pdev->device, pdev->subsystem_device);

	/* Initialize the wait queue for commands */
	init_waitqueue_head(&trans_pcie->wait_command_queue);

	init_waitqueue_head(&trans_pcie->d0i3_waitq);

	if (trans_pcie->msix_enabled) {
		ret = iwl_pcie_init_msix_handler(pdev, trans_pcie);
		if (ret)
			goto out_no_pci;
	 } else {
		ret = iwl_pcie_alloc_ict(trans);
		if (ret)
			goto out_no_pci;

		ret = devm_request_threaded_irq(&pdev->dev, pdev->irq,
						iwl_pcie_isr,
						iwl_pcie_irq_handler,
						IRQF_SHARED, DRV_NAME, trans);
		if (ret) {
			IWL_ERR(trans, "Error allocating IRQ %d\n", pdev->irq);
			goto out_free_ict;
		}
		trans_pcie->inta_mask = CSR_INI_SET_MASK;
	 }

	trans_pcie->rba.alloc_wq = alloc_workqueue("rb_allocator",
						   WQ_HIGHPRI | WQ_UNBOUND, 1);
	INIT_WORK(&trans_pcie->rba.rx_alloc, iwl_pcie_rx_allocator_work);

#ifdef CONFIG_IWLWIFI_PCIE_RTPM
	trans->runtime_pm_mode = IWL_PLAT_PM_MODE_D0I3;
#else
	trans->runtime_pm_mode = IWL_PLAT_PM_MODE_DISABLED;
#endif /* CONFIG_IWLWIFI_PCIE_RTPM */

	return trans;

out_free_ict:
	iwl_pcie_free_ict(trans);
out_no_pci:
	free_percpu(trans_pcie->tso_hdr_page);
	iwl_trans_free(trans);
	return ERR_PTR(ret);
}
