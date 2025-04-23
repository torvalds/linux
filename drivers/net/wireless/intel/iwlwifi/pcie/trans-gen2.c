// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2017 Intel Deutschland GmbH
 * Copyright (C) 2018-2025 Intel Corporation
 */
#include "iwl-trans.h"
#include "iwl-prph.h"
#include "iwl-context-info.h"
#include "iwl-context-info-gen3.h"
#include "internal.h"
#include "fw/dbg.h"

#define FW_RESET_TIMEOUT (HZ / 5)

/*
 * Start up NIC's basic functionality after it has been reset
 * (e.g. after platform boot, or shutdown via iwl_pcie_apm_stop())
 * NOTE:  This does not load uCode nor start the embedded processor
 */
int iwl_pcie_gen2_apm_init(struct iwl_trans *trans)
{
	int ret = 0;

	IWL_DEBUG_INFO(trans, "Init card's basic functions\n");

	/*
	 * Use "set_bit" below rather than "write", to preserve any hardware
	 * bits already set by default after reset.
	 */

	/*
	 * Disable L0s without affecting L1;
	 * don't wait for ICH L0s (ICH bug W/A)
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
		    CSR_HW_IF_CONFIG_REG_HAP_WAKE);

	iwl_pcie_apm_config(trans);

	ret = iwl_finish_nic_init(trans);
	if (ret)
		return ret;

	set_bit(STATUS_DEVICE_ENABLED, &trans->status);

	return 0;
}

static void iwl_pcie_gen2_apm_stop(struct iwl_trans *trans, bool op_mode_leave)
{
	IWL_DEBUG_INFO(trans, "Stop card, put in low power state\n");

	if (op_mode_leave) {
		if (!test_bit(STATUS_DEVICE_ENABLED, &trans->status))
			iwl_pcie_gen2_apm_init(trans);

		/* inform ME that we are leaving */
		iwl_set_bit(trans, CSR_DBG_LINK_PWR_MGMT_REG,
			    CSR_RESET_LINK_PWR_MGMT_DISABLED);
		iwl_set_bit(trans, CSR_HW_IF_CONFIG_REG,
			    CSR_HW_IF_CONFIG_REG_WAKE_ME |
			    CSR_HW_IF_CONFIG_REG_WAKE_ME_PCIE_OWNER_EN);
		mdelay(1);
		iwl_clear_bit(trans, CSR_DBG_LINK_PWR_MGMT_REG,
			      CSR_RESET_LINK_PWR_MGMT_DISABLED);
		mdelay(5);
	}

	clear_bit(STATUS_DEVICE_ENABLED, &trans->status);

	/* Stop device's DMA activity */
	iwl_pcie_apm_stop_master(trans);

	iwl_trans_sw_reset(trans, false);

	/*
	 * Clear "initialization complete" bit to move adapter from
	 * D0A* (powered-up Active) --> D0U* (Uninitialized) state.
	 */
	if (trans->trans_cfg->device_family >= IWL_DEVICE_FAMILY_BZ)
		iwl_clear_bit(trans, CSR_GP_CNTRL,
			      CSR_GP_CNTRL_REG_FLAG_MAC_INIT);
	else
		iwl_clear_bit(trans, CSR_GP_CNTRL,
			      CSR_GP_CNTRL_REG_FLAG_INIT_DONE);
}

void iwl_trans_pcie_fw_reset_handshake(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	int ret;

	trans_pcie->fw_reset_state = FW_RESET_REQUESTED;

	if (trans->trans_cfg->device_family < IWL_DEVICE_FAMILY_AX210)
		iwl_write_umac_prph(trans, UREG_NIC_SET_NMI_DRIVER,
				    UREG_NIC_SET_NMI_DRIVER_RESET_HANDSHAKE);
	else if (trans->trans_cfg->device_family == IWL_DEVICE_FAMILY_AX210)
		iwl_write_umac_prph(trans, UREG_DOORBELL_TO_ISR6,
				    UREG_DOORBELL_TO_ISR6_RESET_HANDSHAKE);
	else
		iwl_write32(trans, CSR_DOORBELL_VECTOR,
			    UREG_DOORBELL_TO_ISR6_RESET_HANDSHAKE);

	/* wait 200ms */
	ret = wait_event_timeout(trans_pcie->fw_reset_waitq,
				 trans_pcie->fw_reset_state != FW_RESET_REQUESTED,
				 FW_RESET_TIMEOUT);
	if (!ret || trans_pcie->fw_reset_state == FW_RESET_ERROR) {
		u32 inta_hw = iwl_read32(trans, CSR_MSIX_HW_INT_CAUSES_AD);

		IWL_ERR(trans,
			"timeout waiting for FW reset ACK (inta_hw=0x%x)\n",
			inta_hw);

		if (!(inta_hw & MSIX_HW_INT_CAUSES_REG_RESET_DONE)) {
			struct iwl_fw_error_dump_mode mode = {
				.type = IWL_ERR_TYPE_RESET_HS_TIMEOUT,
				.context = IWL_ERR_CONTEXT_FROM_OPMODE,
			};
			iwl_op_mode_nic_error(trans->op_mode,
					      IWL_ERR_TYPE_RESET_HS_TIMEOUT);
			iwl_op_mode_dump_error(trans->op_mode, &mode);
		}
	}

	trans_pcie->fw_reset_state = FW_RESET_IDLE;
}

static void _iwl_trans_pcie_gen2_stop_device(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	lockdep_assert_held(&trans_pcie->mutex);

	if (trans_pcie->is_down)
		return;

	if (trans->state >= IWL_TRANS_FW_STARTED &&
	    trans_pcie->fw_reset_handshake) {
		/*
		 * Reset handshake can dump firmware on timeout, but that
		 * should assume that the firmware is already dead.
		 */
		trans->state = IWL_TRANS_NO_FW;
		iwl_trans_pcie_fw_reset_handshake(trans);
	}

	trans_pcie->is_down = true;

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
		iwl_pcie_synchronize_irqs(trans);
		iwl_pcie_rx_napi_sync(trans);
		iwl_txq_gen2_tx_free(trans);
		iwl_pcie_rx_stop(trans);
	}

	iwl_pcie_ctxt_info_free_paging(trans);
	if (trans->trans_cfg->device_family >= IWL_DEVICE_FAMILY_AX210)
		iwl_pcie_ctxt_info_gen3_free(trans, false);
	else
		iwl_pcie_ctxt_info_free(trans);

	/* Stop the device, and put it in low power state */
	iwl_pcie_gen2_apm_stop(trans, false);

	/* re-take ownership to prevent other users from stealing the device */
	iwl_trans_sw_reset(trans, true);

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
}

void iwl_trans_pcie_gen2_stop_device(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	bool was_in_rfkill;

	iwl_op_mode_time_point(trans->op_mode,
			       IWL_FW_INI_TIME_POINT_HOST_DEVICE_DISABLE,
			       NULL);

	mutex_lock(&trans_pcie->mutex);
	trans_pcie->opmode_down = true;
	was_in_rfkill = test_bit(STATUS_RFKILL_OPMODE, &trans->status);
	_iwl_trans_pcie_gen2_stop_device(trans);
	iwl_trans_pcie_handle_stop_rfkill(trans, was_in_rfkill);
	mutex_unlock(&trans_pcie->mutex);
}

static int iwl_pcie_gen2_nic_init(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	int queue_size = max_t(u32, IWL_CMD_QUEUE_SIZE,
			       trans->cfg->min_txq_size);
	int ret;

	/* TODO: most of the logic can be removed in A0 - but not in Z0 */
	spin_lock_bh(&trans_pcie->irq_lock);
	ret = iwl_pcie_gen2_apm_init(trans);
	spin_unlock_bh(&trans_pcie->irq_lock);
	if (ret)
		return ret;

	iwl_op_mode_nic_config(trans->op_mode);

	/* Allocate the RX queue, or reset if it is already allocated */
	if (iwl_pcie_gen2_rx_init(trans))
		return -ENOMEM;

	/* Allocate or reset and init all Tx and Command queues */
	if (iwl_txq_gen2_init(trans, trans_pcie->txqs.cmd.q_id, queue_size))
		return -ENOMEM;

	/* enable shadow regs in HW */
	iwl_set_bit(trans, CSR_MAC_SHADOW_REG_CTRL, 0x800FFFFF);
	IWL_DEBUG_INFO(trans, "Enabling shadow registers in device\n");

	return 0;
}

static void iwl_pcie_get_rf_name(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	char *buf = trans_pcie->rf_name;
	size_t buflen = sizeof(trans_pcie->rf_name);
	size_t pos;
	u32 version;

	if (buf[0])
		return;

	switch (CSR_HW_RFID_TYPE(trans->hw_rf_id)) {
	case CSR_HW_RFID_TYPE(CSR_HW_RF_ID_TYPE_JF):
		pos = scnprintf(buf, buflen, "JF");
		break;
	case CSR_HW_RFID_TYPE(CSR_HW_RF_ID_TYPE_GF):
		pos = scnprintf(buf, buflen, "GF");
		break;
	case CSR_HW_RFID_TYPE(CSR_HW_RF_ID_TYPE_GF4):
		pos = scnprintf(buf, buflen, "GF4");
		break;
	case CSR_HW_RFID_TYPE(CSR_HW_RF_ID_TYPE_HR):
		pos = scnprintf(buf, buflen, "HR");
		break;
	case CSR_HW_RFID_TYPE(CSR_HW_RF_ID_TYPE_HR1):
		pos = scnprintf(buf, buflen, "HR1");
		break;
	case CSR_HW_RFID_TYPE(CSR_HW_RF_ID_TYPE_HRCDB):
		pos = scnprintf(buf, buflen, "HRCDB");
		break;
	case CSR_HW_RFID_TYPE(CSR_HW_RF_ID_TYPE_FM):
		pos = scnprintf(buf, buflen, "FM");
		break;
	case CSR_HW_RFID_TYPE(CSR_HW_RF_ID_TYPE_WP):
		if (SILICON_Z_STEP ==
		    CSR_HW_RFID_STEP(trans->hw_rf_id))
			pos = scnprintf(buf, buflen, "WHTC");
		else
			pos = scnprintf(buf, buflen, "WH");
		break;
	default:
		return;
	}

	switch (CSR_HW_RFID_TYPE(trans->hw_rf_id)) {
	case CSR_HW_RFID_TYPE(CSR_HW_RF_ID_TYPE_HR):
	case CSR_HW_RFID_TYPE(CSR_HW_RF_ID_TYPE_HR1):
	case CSR_HW_RFID_TYPE(CSR_HW_RF_ID_TYPE_HRCDB):
		version = iwl_read_prph(trans, CNVI_MBOX_C);
		switch (version) {
		case 0x20000:
			pos += scnprintf(buf + pos, buflen - pos, " B3");
			break;
		case 0x120000:
			pos += scnprintf(buf + pos, buflen - pos, " B5");
			break;
		default:
			pos += scnprintf(buf + pos, buflen - pos,
					 " (0x%x)", version);
			break;
		}
		break;
	default:
		break;
	}

	pos += scnprintf(buf + pos, buflen - pos, ", rfid=0x%x",
			 trans->hw_rf_id);

	IWL_INFO(trans, "Detected RF %s\n", buf);

	/*
	 * also add a \n for debugfs - need to do it after printing
	 * since our IWL_INFO machinery wants to see a static \n at
	 * the end of the string
	 */
	pos += scnprintf(buf + pos, buflen - pos, "\n");
}

void iwl_trans_pcie_gen2_fw_alive(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	iwl_pcie_reset_ict(trans);

	/* make sure all queue are not stopped/used */
	memset(trans_pcie->txqs.queue_stopped, 0,
	       sizeof(trans_pcie->txqs.queue_stopped));
	memset(trans_pcie->txqs.queue_used, 0,
	       sizeof(trans_pcie->txqs.queue_used));

	/* now that we got alive we can free the fw image & the context info.
	 * paging memory cannot be freed included since FW will still use it
	 */
	if (trans->trans_cfg->device_family >= IWL_DEVICE_FAMILY_AX210)
		iwl_pcie_ctxt_info_gen3_free(trans, true);
	else
		iwl_pcie_ctxt_info_free(trans);

	/*
	 * Re-enable all the interrupts, including the RF-Kill one, now that
	 * the firmware is alive.
	 */
	iwl_enable_interrupts(trans);
	mutex_lock(&trans_pcie->mutex);
	iwl_pcie_check_hw_rf_kill(trans);

	iwl_pcie_get_rf_name(trans);
	mutex_unlock(&trans_pcie->mutex);

	if (trans->trans_cfg->device_family >= IWL_DEVICE_FAMILY_BZ)
		trans->step_urm = !!(iwl_read_umac_prph(trans,
							CNVI_PMU_STEP_FLOW) &
					CNVI_PMU_STEP_FLOW_FORCE_URM);
}

static bool iwl_pcie_set_ltr(struct iwl_trans *trans)
{
	u32 ltr_val = CSR_LTR_LONG_VAL_AD_NO_SNOOP_REQ |
		      u32_encode_bits(CSR_LTR_LONG_VAL_AD_SCALE_USEC,
				      CSR_LTR_LONG_VAL_AD_NO_SNOOP_SCALE) |
		      u32_encode_bits(250,
				      CSR_LTR_LONG_VAL_AD_NO_SNOOP_VAL) |
		      CSR_LTR_LONG_VAL_AD_SNOOP_REQ |
		      u32_encode_bits(CSR_LTR_LONG_VAL_AD_SCALE_USEC,
				      CSR_LTR_LONG_VAL_AD_SNOOP_SCALE) |
		      u32_encode_bits(250, CSR_LTR_LONG_VAL_AD_SNOOP_VAL);

	/*
	 * To workaround hardware latency issues during the boot process,
	 * initialize the LTR to ~250 usec (see ltr_val above).
	 * The firmware initializes this again later (to a smaller value).
	 */
	if ((trans->trans_cfg->device_family == IWL_DEVICE_FAMILY_AX210 ||
	     trans->trans_cfg->device_family == IWL_DEVICE_FAMILY_22000) &&
	    !trans->trans_cfg->integrated) {
		iwl_write32(trans, CSR_LTR_LONG_VAL_AD, ltr_val);
		return true;
	}

	if (trans->trans_cfg->integrated &&
	    trans->trans_cfg->device_family == IWL_DEVICE_FAMILY_22000) {
		iwl_write_prph(trans, HPM_MAC_LTR_CSR, HPM_MAC_LRT_ENABLE_ALL);
		iwl_write_prph(trans, HPM_UMAC_LTR, ltr_val);
		return true;
	}

	if (trans->trans_cfg->device_family == IWL_DEVICE_FAMILY_AX210) {
		/* First clear the interrupt, just in case */
		iwl_write32(trans, CSR_MSIX_HW_INT_CAUSES_AD,
			    MSIX_HW_INT_CAUSES_REG_IML);
		/* In this case, unfortunately the same ROM bug exists in the
		 * device (not setting LTR correctly), but we don't have control
		 * over the settings from the host due to some hardware security
		 * features. The only workaround we've been able to come up with
		 * so far is to try to keep the CPU and device busy by polling
		 * it and the IML (image loader) completed interrupt.
		 */
		return false;
	}

	/* nothing needs to be done on other devices */
	return true;
}

static void iwl_pcie_spin_for_iml(struct iwl_trans *trans)
{
/* in practice, this seems to complete in around 20-30ms at most, wait 100 */
#define IML_WAIT_TIMEOUT	(HZ / 10)
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	unsigned long end_time = jiffies + IML_WAIT_TIMEOUT;
	u32 value, loops = 0;
	bool irq = false;

	if (WARN_ON(!trans_pcie->iml))
		return;

	value = iwl_read32(trans, CSR_LTR_LAST_MSG);
	IWL_DEBUG_INFO(trans, "Polling for IML load - CSR_LTR_LAST_MSG=0x%x\n",
		       value);

	while (time_before(jiffies, end_time)) {
		if (iwl_read32(trans, CSR_MSIX_HW_INT_CAUSES_AD) &
				MSIX_HW_INT_CAUSES_REG_IML) {
			irq = true;
			break;
		}
		/* Keep the CPU and device busy. */
		value = iwl_read32(trans, CSR_LTR_LAST_MSG);
		loops++;
	}

	IWL_DEBUG_INFO(trans,
		       "Polled for IML load: irq=%d, loops=%d, CSR_LTR_LAST_MSG=0x%x\n",
		       irq, loops, value);

	/* We don't fail here even if we timed out - maybe we get lucky and the
	 * interrupt comes in later (and we get alive from firmware) and then
	 * we're all happy - but if not we'll fail on alive timeout or get some
	 * other error out.
	 */
}

int iwl_trans_pcie_gen2_start_fw(struct iwl_trans *trans,
				 const struct fw_img *fw, bool run_in_rfkill)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	bool hw_rfkill, keep_ram_busy;
	int ret;

	/* This may fail if AMT took ownership of the device */
	if (iwl_pcie_prepare_card_hw(trans)) {
		IWL_WARN(trans, "Exit HW not ready\n");
		return -EIO;
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

	ret = iwl_pcie_gen2_nic_init(trans);
	if (ret) {
		IWL_ERR(trans, "Unable to init nic\n");
		goto out;
	}

	if (trans->trans_cfg->device_family >= IWL_DEVICE_FAMILY_AX210)
		ret = iwl_pcie_ctxt_info_gen3_init(trans, fw);
	else
		ret = iwl_pcie_ctxt_info_init(trans, fw);
	if (ret)
		goto out;

	keep_ram_busy = !iwl_pcie_set_ltr(trans);

	if (trans->trans_cfg->device_family >= IWL_DEVICE_FAMILY_BZ) {
		IWL_DEBUG_POWER(trans, "function scratch register value is 0x%08x\n",
				iwl_read32(trans, CSR_FUNC_SCRATCH));
		iwl_write32(trans, CSR_FUNC_SCRATCH, CSR_FUNC_SCRATCH_INIT_VALUE);
		iwl_set_bit(trans, CSR_GP_CNTRL,
			    CSR_GP_CNTRL_REG_FLAG_ROM_START);
	} else if (trans->trans_cfg->device_family >= IWL_DEVICE_FAMILY_AX210) {
		iwl_write_umac_prph(trans, UREG_CPU_INIT_RUN, 1);
	} else {
		iwl_write_prph(trans, UREG_CPU_INIT_RUN, 1);
	}

	if (keep_ram_busy)
		iwl_pcie_spin_for_iml(trans);

	/* re-check RF-Kill state since we may have missed the interrupt */
	hw_rfkill = iwl_pcie_check_hw_rf_kill(trans);
	if (hw_rfkill && !run_in_rfkill)
		ret = -ERFKILL;

out:
	mutex_unlock(&trans_pcie->mutex);
	return ret;
}
