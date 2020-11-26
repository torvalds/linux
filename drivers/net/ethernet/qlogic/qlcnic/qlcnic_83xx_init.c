// SPDX-License-Identifier: GPL-2.0-only
/*
 * QLogic qlcnic NIC Driver
 * Copyright (c) 2009-2013 QLogic Corporation
 */

#include "qlcnic_sriov.h"
#include "qlcnic.h"
#include "qlcnic_hw.h"

/* Reset template definitions */
#define QLC_83XX_RESTART_TEMPLATE_SIZE		0x2000
#define QLC_83XX_RESET_TEMPLATE_ADDR		0x4F0000
#define QLC_83XX_RESET_SEQ_VERSION		0x0101

#define QLC_83XX_OPCODE_NOP			0x0000
#define QLC_83XX_OPCODE_WRITE_LIST		0x0001
#define QLC_83XX_OPCODE_READ_WRITE_LIST		0x0002
#define QLC_83XX_OPCODE_POLL_LIST		0x0004
#define QLC_83XX_OPCODE_POLL_WRITE_LIST		0x0008
#define QLC_83XX_OPCODE_READ_MODIFY_WRITE	0x0010
#define QLC_83XX_OPCODE_SEQ_PAUSE		0x0020
#define QLC_83XX_OPCODE_SEQ_END			0x0040
#define QLC_83XX_OPCODE_TMPL_END		0x0080
#define QLC_83XX_OPCODE_POLL_READ_LIST		0x0100

/* EPORT control registers */
#define QLC_83XX_RESET_CONTROL			0x28084E50
#define QLC_83XX_RESET_REG			0x28084E60
#define QLC_83XX_RESET_PORT0			0x28084E70
#define QLC_83XX_RESET_PORT1			0x28084E80
#define QLC_83XX_RESET_PORT2			0x28084E90
#define QLC_83XX_RESET_PORT3			0x28084EA0
#define QLC_83XX_RESET_SRESHIM			0x28084EB0
#define QLC_83XX_RESET_EPGSHIM			0x28084EC0
#define QLC_83XX_RESET_ETHERPCS			0x28084ED0

static int qlcnic_83xx_init_default_driver(struct qlcnic_adapter *adapter);
static int qlcnic_83xx_check_heartbeat(struct qlcnic_adapter *p_dev);
static int qlcnic_83xx_restart_hw(struct qlcnic_adapter *adapter);
static int qlcnic_83xx_check_hw_status(struct qlcnic_adapter *p_dev);
static int qlcnic_83xx_get_reset_instruction_template(struct qlcnic_adapter *);
static void qlcnic_83xx_stop_hw(struct qlcnic_adapter *);

/* Template header */
struct qlc_83xx_reset_hdr {
#if defined(__LITTLE_ENDIAN)
	u16	version;
	u16	signature;
	u16	size;
	u16	entries;
	u16	hdr_size;
	u16	checksum;
	u16	init_offset;
	u16	start_offset;
#elif defined(__BIG_ENDIAN)
	u16	signature;
	u16	version;
	u16	entries;
	u16	size;
	u16	checksum;
	u16	hdr_size;
	u16	start_offset;
	u16	init_offset;
#endif
} __packed;

/* Command entry header. */
struct qlc_83xx_entry_hdr {
#if defined(__LITTLE_ENDIAN)
	u16	cmd;
	u16	size;
	u16	count;
	u16	delay;
#elif defined(__BIG_ENDIAN)
	u16	size;
	u16	cmd;
	u16	delay;
	u16	count;
#endif
} __packed;

/* Generic poll command */
struct qlc_83xx_poll {
	u32	mask;
	u32	status;
} __packed;

/* Read modify write command */
struct qlc_83xx_rmw {
	u32	mask;
	u32	xor_value;
	u32	or_value;
#if defined(__LITTLE_ENDIAN)
	u8	shl;
	u8	shr;
	u8	index_a;
	u8	rsvd;
#elif defined(__BIG_ENDIAN)
	u8	rsvd;
	u8	index_a;
	u8	shr;
	u8	shl;
#endif
} __packed;

/* Generic command with 2 DWORD */
struct qlc_83xx_entry {
	u32 arg1;
	u32 arg2;
} __packed;

/* Generic command with 4 DWORD */
struct qlc_83xx_quad_entry {
	u32 dr_addr;
	u32 dr_value;
	u32 ar_addr;
	u32 ar_value;
} __packed;
static const char *const qlc_83xx_idc_states[] = {
	"Unknown",
	"Cold",
	"Init",
	"Ready",
	"Need Reset",
	"Need Quiesce",
	"Failed",
	"Quiesce"
};

static int
qlcnic_83xx_idc_check_driver_presence_reg(struct qlcnic_adapter *adapter)
{
	u32 val;

	val = QLCRDX(adapter->ahw, QLC_83XX_IDC_DRV_PRESENCE);
	if ((val & 0xFFFF))
		return 1;
	else
		return 0;
}

static void qlcnic_83xx_idc_log_state_history(struct qlcnic_adapter *adapter)
{
	u32 cur, prev;
	cur = adapter->ahw->idc.curr_state;
	prev = adapter->ahw->idc.prev_state;

	dev_info(&adapter->pdev->dev,
		 "current state  = %s,  prev state = %s\n",
		 adapter->ahw->idc.name[cur],
		 adapter->ahw->idc.name[prev]);
}

static int qlcnic_83xx_idc_update_audit_reg(struct qlcnic_adapter *adapter,
					    u8 mode, int lock)
{
	u32 val;
	int seconds;

	if (lock) {
		if (qlcnic_83xx_lock_driver(adapter))
			return -EBUSY;
	}

	val = QLCRDX(adapter->ahw, QLC_83XX_IDC_DRV_AUDIT);
	val |= (adapter->portnum & 0xf);
	val |= mode << 7;
	if (mode)
		seconds = jiffies / HZ - adapter->ahw->idc.sec_counter;
	else
		seconds = jiffies / HZ;

	val |= seconds << 8;
	QLCWRX(adapter->ahw, QLC_83XX_IDC_DRV_AUDIT, val);
	adapter->ahw->idc.sec_counter = jiffies / HZ;

	if (lock)
		qlcnic_83xx_unlock_driver(adapter);

	return 0;
}

static void qlcnic_83xx_idc_update_minor_version(struct qlcnic_adapter *adapter)
{
	u32 val;

	val = QLCRDX(adapter->ahw, QLC_83XX_IDC_MIN_VERSION);
	val = val & ~(0x3 << (adapter->portnum * 2));
	val = val | (QLC_83XX_IDC_MINOR_VERSION << (adapter->portnum * 2));
	QLCWRX(adapter->ahw, QLC_83XX_IDC_MIN_VERSION, val);
}

static int qlcnic_83xx_idc_update_major_version(struct qlcnic_adapter *adapter,
						int lock)
{
	u32 val;

	if (lock) {
		if (qlcnic_83xx_lock_driver(adapter))
			return -EBUSY;
	}

	val = QLCRDX(adapter->ahw, QLC_83XX_IDC_MAJ_VERSION);
	val = val & ~0xFF;
	val = val | QLC_83XX_IDC_MAJOR_VERSION;
	QLCWRX(adapter->ahw, QLC_83XX_IDC_MAJ_VERSION, val);

	if (lock)
		qlcnic_83xx_unlock_driver(adapter);

	return 0;
}

static int
qlcnic_83xx_idc_update_drv_presence_reg(struct qlcnic_adapter *adapter,
					int status, int lock)
{
	u32 val;

	if (lock) {
		if (qlcnic_83xx_lock_driver(adapter))
			return -EBUSY;
	}

	val = QLCRDX(adapter->ahw, QLC_83XX_IDC_DRV_PRESENCE);

	if (status)
		val = val | (1 << adapter->portnum);
	else
		val = val & ~(1 << adapter->portnum);

	QLCWRX(adapter->ahw, QLC_83XX_IDC_DRV_PRESENCE, val);
	qlcnic_83xx_idc_update_minor_version(adapter);

	if (lock)
		qlcnic_83xx_unlock_driver(adapter);

	return 0;
}

static int qlcnic_83xx_idc_check_major_version(struct qlcnic_adapter *adapter)
{
	u32 val;
	u8 version;

	val = QLCRDX(adapter->ahw, QLC_83XX_IDC_MAJ_VERSION);
	version = val & 0xFF;

	if (version != QLC_83XX_IDC_MAJOR_VERSION) {
		dev_info(&adapter->pdev->dev,
			 "%s:mismatch. version 0x%x, expected version 0x%x\n",
			 __func__, version, QLC_83XX_IDC_MAJOR_VERSION);
		return -EIO;
	}

	return 0;
}

static int qlcnic_83xx_idc_clear_registers(struct qlcnic_adapter *adapter,
					   int lock)
{
	u32 val;

	if (lock) {
		if (qlcnic_83xx_lock_driver(adapter))
			return -EBUSY;
	}

	QLCWRX(adapter->ahw, QLC_83XX_IDC_DRV_ACK, 0);
	/* Clear graceful reset bit */
	val = QLCRDX(adapter->ahw, QLC_83XX_IDC_CTRL);
	val &= ~QLC_83XX_IDC_GRACEFULL_RESET;
	QLCWRX(adapter->ahw, QLC_83XX_IDC_CTRL, val);

	if (lock)
		qlcnic_83xx_unlock_driver(adapter);

	return 0;
}

static int qlcnic_83xx_idc_update_drv_ack_reg(struct qlcnic_adapter *adapter,
					      int flag, int lock)
{
	u32 val;

	if (lock) {
		if (qlcnic_83xx_lock_driver(adapter))
			return -EBUSY;
	}

	val = QLCRDX(adapter->ahw, QLC_83XX_IDC_DRV_ACK);
	if (flag)
		val = val | (1 << adapter->portnum);
	else
		val = val & ~(1 << adapter->portnum);
	QLCWRX(adapter->ahw, QLC_83XX_IDC_DRV_ACK, val);

	if (lock)
		qlcnic_83xx_unlock_driver(adapter);

	return 0;
}

static int qlcnic_83xx_idc_check_timeout(struct qlcnic_adapter *adapter,
					 int time_limit)
{
	u64 seconds;

	seconds = jiffies / HZ - adapter->ahw->idc.sec_counter;
	if (seconds <= time_limit)
		return 0;
	else
		return -EBUSY;
}

/**
 * qlcnic_83xx_idc_check_reset_ack_reg
 *
 * @adapter: adapter structure
 *
 * Check ACK wait limit and clear the functions which failed to ACK
 *
 * Return 0 if all functions have acknowledged the reset request.
 **/
static int qlcnic_83xx_idc_check_reset_ack_reg(struct qlcnic_adapter *adapter)
{
	int timeout;
	u32 ack, presence, val;

	timeout = QLC_83XX_IDC_RESET_TIMEOUT_SECS;
	ack = QLCRDX(adapter->ahw, QLC_83XX_IDC_DRV_ACK);
	presence = QLCRDX(adapter->ahw, QLC_83XX_IDC_DRV_PRESENCE);
	dev_info(&adapter->pdev->dev,
		 "%s: ack = 0x%x, presence = 0x%x\n", __func__, ack, presence);
	if (!((ack & presence) == presence)) {
		if (qlcnic_83xx_idc_check_timeout(adapter, timeout)) {
			/* Clear functions which failed to ACK */
			dev_info(&adapter->pdev->dev,
				 "%s: ACK wait exceeds time limit\n", __func__);
			val = QLCRDX(adapter->ahw, QLC_83XX_IDC_DRV_PRESENCE);
			val = val & ~(ack ^ presence);
			if (qlcnic_83xx_lock_driver(adapter))
				return -EBUSY;
			QLCWRX(adapter->ahw, QLC_83XX_IDC_DRV_PRESENCE, val);
			dev_info(&adapter->pdev->dev,
				 "%s: updated drv presence reg = 0x%x\n",
				 __func__, val);
			qlcnic_83xx_unlock_driver(adapter);
			return 0;

		} else {
			return 1;
		}
	} else {
		dev_info(&adapter->pdev->dev,
			 "%s: Reset ACK received from all functions\n",
			 __func__);
		return 0;
	}
}

/**
 * qlcnic_83xx_idc_tx_soft_reset
 *
 * @adapter: adapter structure
 *
 * Handle context deletion and recreation request from transmit routine
 *
 * Returns -EBUSY  or Success (0)
 *
 **/
static int qlcnic_83xx_idc_tx_soft_reset(struct qlcnic_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;

	if (test_and_set_bit(__QLCNIC_RESETTING, &adapter->state))
		return -EBUSY;

	netif_device_detach(netdev);
	qlcnic_down(adapter, netdev);
	qlcnic_up(adapter, netdev);
	netif_device_attach(netdev);
	clear_bit(__QLCNIC_RESETTING, &adapter->state);
	netdev_info(adapter->netdev, "%s: soft reset complete.\n", __func__);

	return 0;
}

/**
 * qlcnic_83xx_idc_detach_driver
 *
 * @adapter: adapter structure
 * Detach net interface, stop TX and cleanup resources before the HW reset.
 * Returns: None
 *
 **/
static void qlcnic_83xx_idc_detach_driver(struct qlcnic_adapter *adapter)
{
	int i;
	struct net_device *netdev = adapter->netdev;

	netif_device_detach(netdev);
	qlcnic_83xx_detach_mailbox_work(adapter);

	/* Disable mailbox interrupt */
	qlcnic_83xx_disable_mbx_intr(adapter);
	qlcnic_down(adapter, netdev);
	for (i = 0; i < adapter->ahw->num_msix; i++) {
		adapter->ahw->intr_tbl[i].id = i;
		adapter->ahw->intr_tbl[i].enabled = 0;
		adapter->ahw->intr_tbl[i].src = 0;
	}

	if (qlcnic_sriov_pf_check(adapter))
		qlcnic_sriov_pf_reset(adapter);
}

/**
 * qlcnic_83xx_idc_attach_driver
 *
 * @adapter: adapter structure
 *
 * Re-attach and re-enable net interface
 * Returns: None
 *
 **/
static void qlcnic_83xx_idc_attach_driver(struct qlcnic_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;

	if (netif_running(netdev)) {
		if (qlcnic_up(adapter, netdev))
			goto done;
		qlcnic_restore_indev_addr(netdev, NETDEV_UP);
	}
done:
	netif_device_attach(netdev);
}

static int qlcnic_83xx_idc_enter_failed_state(struct qlcnic_adapter *adapter,
					      int lock)
{
	if (lock) {
		if (qlcnic_83xx_lock_driver(adapter))
			return -EBUSY;
	}

	qlcnic_83xx_idc_clear_registers(adapter, 0);
	QLCWRX(adapter->ahw, QLC_83XX_IDC_DEV_STATE, QLC_83XX_IDC_DEV_FAILED);
	if (lock)
		qlcnic_83xx_unlock_driver(adapter);

	qlcnic_83xx_idc_log_state_history(adapter);
	dev_info(&adapter->pdev->dev, "Device will enter failed state\n");

	return 0;
}

static int qlcnic_83xx_idc_enter_init_state(struct qlcnic_adapter *adapter,
					    int lock)
{
	if (lock) {
		if (qlcnic_83xx_lock_driver(adapter))
			return -EBUSY;
	}

	QLCWRX(adapter->ahw, QLC_83XX_IDC_DEV_STATE, QLC_83XX_IDC_DEV_INIT);

	if (lock)
		qlcnic_83xx_unlock_driver(adapter);

	return 0;
}

static int qlcnic_83xx_idc_enter_need_quiesce(struct qlcnic_adapter *adapter,
					      int lock)
{
	if (lock) {
		if (qlcnic_83xx_lock_driver(adapter))
			return -EBUSY;
	}

	QLCWRX(adapter->ahw, QLC_83XX_IDC_DEV_STATE,
	       QLC_83XX_IDC_DEV_NEED_QUISCENT);

	if (lock)
		qlcnic_83xx_unlock_driver(adapter);

	return 0;
}

static int
qlcnic_83xx_idc_enter_need_reset_state(struct qlcnic_adapter *adapter, int lock)
{
	if (lock) {
		if (qlcnic_83xx_lock_driver(adapter))
			return -EBUSY;
	}

	QLCWRX(adapter->ahw, QLC_83XX_IDC_DEV_STATE,
	       QLC_83XX_IDC_DEV_NEED_RESET);

	if (lock)
		qlcnic_83xx_unlock_driver(adapter);

	return 0;
}

static int qlcnic_83xx_idc_enter_ready_state(struct qlcnic_adapter *adapter,
					     int lock)
{
	if (lock) {
		if (qlcnic_83xx_lock_driver(adapter))
			return -EBUSY;
	}

	QLCWRX(adapter->ahw, QLC_83XX_IDC_DEV_STATE, QLC_83XX_IDC_DEV_READY);
	if (lock)
		qlcnic_83xx_unlock_driver(adapter);

	return 0;
}

/**
 * qlcnic_83xx_idc_find_reset_owner_id
 *
 * @adapter: adapter structure
 *
 * NIC gets precedence over ISCSI and ISCSI has precedence over FCOE.
 * Within the same class, function with lowest PCI ID assumes ownership
 *
 * Returns: reset owner id or failure indication (-EIO)
 *
 **/
static int qlcnic_83xx_idc_find_reset_owner_id(struct qlcnic_adapter *adapter)
{
	u32 reg, reg1, reg2, i, j, owner, class;

	reg1 = QLCRDX(adapter->ahw, QLC_83XX_IDC_DEV_PARTITION_INFO_1);
	reg2 = QLCRDX(adapter->ahw, QLC_83XX_IDC_DEV_PARTITION_INFO_2);
	owner = QLCNIC_TYPE_NIC;
	i = 0;
	j = 0;
	reg = reg1;

	do {
		class = (((reg & (0xF << j * 4)) >> j * 4) & 0x3);
		if (class == owner)
			break;
		if (i == (QLC_83XX_IDC_MAX_FUNC_PER_PARTITION_INFO - 1)) {
			reg = reg2;
			j = 0;
		} else {
			j++;
		}

		if (i == (QLC_83XX_IDC_MAX_CNA_FUNCTIONS - 1)) {
			if (owner == QLCNIC_TYPE_NIC)
				owner = QLCNIC_TYPE_ISCSI;
			else if (owner == QLCNIC_TYPE_ISCSI)
				owner = QLCNIC_TYPE_FCOE;
			else if (owner == QLCNIC_TYPE_FCOE)
				return -EIO;
			reg = reg1;
			j = 0;
			i = 0;
		}
	} while (i++ < QLC_83XX_IDC_MAX_CNA_FUNCTIONS);

	return i;
}

static int qlcnic_83xx_idc_restart_hw(struct qlcnic_adapter *adapter, int lock)
{
	int ret = 0;

	ret = qlcnic_83xx_restart_hw(adapter);

	if (ret) {
		qlcnic_83xx_idc_enter_failed_state(adapter, lock);
	} else {
		qlcnic_83xx_idc_clear_registers(adapter, lock);
		ret = qlcnic_83xx_idc_enter_ready_state(adapter, lock);
	}

	return ret;
}

static int qlcnic_83xx_idc_check_fan_failure(struct qlcnic_adapter *adapter)
{
	u32 status;

	status = QLC_SHARED_REG_RD32(adapter, QLCNIC_PEG_HALT_STATUS1);

	if (status & QLCNIC_RCODE_FATAL_ERROR) {
		dev_err(&adapter->pdev->dev,
			"peg halt status1=0x%x\n", status);
		if (QLCNIC_FWERROR_CODE(status) == QLCNIC_FWERROR_FAN_FAILURE) {
			dev_err(&adapter->pdev->dev,
				"On board active cooling fan failed. "
				"Device has been halted.\n");
			dev_err(&adapter->pdev->dev,
				"Replace the adapter.\n");
			return -EIO;
		}
	}

	return 0;
}

int qlcnic_83xx_idc_reattach_driver(struct qlcnic_adapter *adapter)
{
	int err;

	qlcnic_83xx_reinit_mbx_work(adapter->ahw->mailbox);
	qlcnic_83xx_enable_mbx_interrupt(adapter);

	qlcnic_83xx_initialize_nic(adapter, 1);

	err = qlcnic_sriov_pf_reinit(adapter);
	if (err)
		return err;

	qlcnic_83xx_enable_mbx_interrupt(adapter);

	if (qlcnic_83xx_configure_opmode(adapter)) {
		qlcnic_83xx_idc_enter_failed_state(adapter, 1);
		return -EIO;
	}

	if (adapter->nic_ops->init_driver(adapter)) {
		qlcnic_83xx_idc_enter_failed_state(adapter, 1);
		return -EIO;
	}

	if (adapter->portnum == 0)
		qlcnic_set_drv_version(adapter);

	qlcnic_dcb_get_info(adapter->dcb);
	qlcnic_83xx_idc_attach_driver(adapter);

	return 0;
}

static void qlcnic_83xx_idc_update_idc_params(struct qlcnic_adapter *adapter)
{
	struct qlcnic_hardware_context *ahw = adapter->ahw;

	qlcnic_83xx_idc_update_drv_presence_reg(adapter, 1, 1);
	qlcnic_83xx_idc_update_audit_reg(adapter, 0, 1);
	set_bit(QLC_83XX_MODULE_LOADED, &adapter->ahw->idc.status);

	ahw->idc.quiesce_req = 0;
	ahw->idc.delay = QLC_83XX_IDC_FW_POLL_DELAY;
	ahw->idc.err_code = 0;
	ahw->idc.collect_dump = 0;
	ahw->reset_context = 0;
	adapter->tx_timeo_cnt = 0;
	ahw->idc.delay_reset = 0;

	clear_bit(__QLCNIC_RESETTING, &adapter->state);
}

/**
 * qlcnic_83xx_idc_ready_state_entry
 *
 * @adapter: adapter structure
 *
 * Perform ready state initialization, this routine will get invoked only
 * once from READY state.
 *
 * Returns: Error code or Success(0)
 *
 **/
int qlcnic_83xx_idc_ready_state_entry(struct qlcnic_adapter *adapter)
{
	struct qlcnic_hardware_context *ahw = adapter->ahw;

	if (ahw->idc.prev_state != QLC_83XX_IDC_DEV_READY) {
		qlcnic_83xx_idc_update_idc_params(adapter);
		/* Re-attach the device if required */
		if ((ahw->idc.prev_state == QLC_83XX_IDC_DEV_NEED_RESET) ||
		    (ahw->idc.prev_state == QLC_83XX_IDC_DEV_INIT)) {
			if (qlcnic_83xx_idc_reattach_driver(adapter))
				return -EIO;
		}
	}

	return 0;
}

/**
 * qlcnic_83xx_idc_vnic_pf_entry
 *
 * @adapter: adapter structure
 *
 * Ensure vNIC mode privileged function starts only after vNIC mode is
 * enabled by management function.
 * If vNIC mode is ready, start initialization.
 *
 * Returns: -EIO or 0
 *
 **/
int qlcnic_83xx_idc_vnic_pf_entry(struct qlcnic_adapter *adapter)
{
	u32 state;
	struct qlcnic_hardware_context *ahw = adapter->ahw;

	/* Privileged function waits till mgmt function enables VNIC mode */
	state = QLCRDX(adapter->ahw, QLC_83XX_VNIC_STATE);
	if (state != QLCNIC_DEV_NPAR_OPER) {
		if (!ahw->idc.vnic_wait_limit--) {
			qlcnic_83xx_idc_enter_failed_state(adapter, 1);
			return -EIO;
		}
		dev_info(&adapter->pdev->dev, "vNIC mode disabled\n");
		return -EIO;

	} else {
		/* Perform one time initialization from ready state */
		if (ahw->idc.vnic_state != QLCNIC_DEV_NPAR_OPER) {
			qlcnic_83xx_idc_update_idc_params(adapter);

			/* If the previous state is UNKNOWN, device will be
			   already attached properly by Init routine*/
			if (ahw->idc.prev_state != QLC_83XX_IDC_DEV_UNKNOWN) {
				if (qlcnic_83xx_idc_reattach_driver(adapter))
					return -EIO;
			}
			adapter->ahw->idc.vnic_state =  QLCNIC_DEV_NPAR_OPER;
			dev_info(&adapter->pdev->dev, "vNIC mode enabled\n");
		}
	}

	return 0;
}

static int qlcnic_83xx_idc_unknown_state(struct qlcnic_adapter *adapter)
{
	adapter->ahw->idc.err_code = -EIO;
	dev_err(&adapter->pdev->dev,
		"%s: Device in unknown state\n", __func__);
	clear_bit(__QLCNIC_RESETTING, &adapter->state);
	return 0;
}

/**
 * qlcnic_83xx_idc_cold_state
 *
 * @adapter: adapter structure
 *
 * If HW is up and running device will enter READY state.
 * If firmware image from host needs to be loaded, device is
 * forced to start with the file firmware image.
 *
 * Returns: Error code or Success(0)
 *
 **/
static int qlcnic_83xx_idc_cold_state_handler(struct qlcnic_adapter *adapter)
{
	qlcnic_83xx_idc_update_drv_presence_reg(adapter, 1, 0);
	qlcnic_83xx_idc_update_audit_reg(adapter, 1, 0);

	if (qlcnic_load_fw_file) {
		qlcnic_83xx_idc_restart_hw(adapter, 0);
	} else {
		if (qlcnic_83xx_check_hw_status(adapter)) {
			qlcnic_83xx_idc_enter_failed_state(adapter, 0);
			return -EIO;
		} else {
			qlcnic_83xx_idc_enter_ready_state(adapter, 0);
		}
	}
	return 0;
}

/**
 * qlcnic_83xx_idc_init_state
 *
 * @adapter: adapter structure
 *
 * Reset owner will restart the device from this state.
 * Device will enter failed state if it remains
 * in this state for more than DEV_INIT time limit.
 *
 * Returns: Error code or Success(0)
 *
 **/
static int qlcnic_83xx_idc_init_state(struct qlcnic_adapter *adapter)
{
	int timeout, ret = 0;
	u32 owner;

	timeout = QLC_83XX_IDC_INIT_TIMEOUT_SECS;
	if (adapter->ahw->idc.prev_state == QLC_83XX_IDC_DEV_NEED_RESET) {
		owner = qlcnic_83xx_idc_find_reset_owner_id(adapter);
		if (adapter->ahw->pci_func == owner)
			ret = qlcnic_83xx_idc_restart_hw(adapter, 1);
	} else {
		ret = qlcnic_83xx_idc_check_timeout(adapter, timeout);
	}

	return ret;
}

/**
 * qlcnic_83xx_idc_ready_state
 *
 * @adapter: adapter structure
 *
 * Perform IDC protocol specicifed actions after monitoring device state and
 * events.
 *
 * Returns: Error code or Success(0)
 *
 **/
static int qlcnic_83xx_idc_ready_state(struct qlcnic_adapter *adapter)
{
	struct qlcnic_hardware_context *ahw = adapter->ahw;
	struct qlcnic_mailbox *mbx = ahw->mailbox;
	int ret = 0;
	u32 val;

	/* Perform NIC configuration based ready state entry actions */
	if (ahw->idc.state_entry(adapter))
		return -EIO;

	if (qlcnic_check_temp(adapter)) {
		if (ahw->temp == QLCNIC_TEMP_PANIC) {
			qlcnic_83xx_idc_check_fan_failure(adapter);
			dev_err(&adapter->pdev->dev,
				"Error: device temperature %d above limits\n",
				adapter->ahw->temp);
			clear_bit(QLC_83XX_MBX_READY, &mbx->status);
			set_bit(__QLCNIC_RESETTING, &adapter->state);
			qlcnic_83xx_idc_detach_driver(adapter);
			qlcnic_83xx_idc_enter_failed_state(adapter, 1);
			return -EIO;
		}
	}

	val = QLCRDX(adapter->ahw, QLC_83XX_IDC_CTRL);
	ret = qlcnic_83xx_check_heartbeat(adapter);
	if (ret) {
		adapter->flags |= QLCNIC_FW_HANG;
		if (!(val & QLC_83XX_IDC_DISABLE_FW_RESET_RECOVERY)) {
			clear_bit(QLC_83XX_MBX_READY, &mbx->status);
			set_bit(__QLCNIC_RESETTING, &adapter->state);
			qlcnic_83xx_idc_enter_need_reset_state(adapter, 1);
		}  else {
			netdev_info(adapter->netdev, "%s: Auto firmware recovery is disabled\n",
				    __func__);
			qlcnic_83xx_idc_enter_failed_state(adapter, 1);
		}
		return -EIO;
	}

	if ((val & QLC_83XX_IDC_GRACEFULL_RESET) || ahw->idc.collect_dump) {
		clear_bit(QLC_83XX_MBX_READY, &mbx->status);

		/* Move to need reset state and prepare for reset */
		qlcnic_83xx_idc_enter_need_reset_state(adapter, 1);
		return ret;
	}

	/* Check for soft reset request */
	if (ahw->reset_context &&
	    !(val & QLC_83XX_IDC_DISABLE_FW_RESET_RECOVERY)) {
		adapter->ahw->reset_context = 0;
		qlcnic_83xx_idc_tx_soft_reset(adapter);
		return ret;
	}

	/* Move to need quiesce state if requested */
	if (adapter->ahw->idc.quiesce_req) {
		qlcnic_83xx_idc_enter_need_quiesce(adapter, 1);
		qlcnic_83xx_idc_update_audit_reg(adapter, 0, 1);
		return ret;
	}

	return ret;
}

/**
 * qlcnic_83xx_idc_need_reset_state
 *
 * @adapter: adapter structure
 *
 * Device will remain in this state until:
 *	Reset request ACK's are received from all the functions
 *	Wait time exceeds max time limit
 *
 * Returns: Error code or Success(0)
 *
 **/
static int qlcnic_83xx_idc_need_reset_state(struct qlcnic_adapter *adapter)
{
	struct qlcnic_mailbox *mbx = adapter->ahw->mailbox;
	int ret = 0;

	if (adapter->ahw->idc.prev_state != QLC_83XX_IDC_DEV_NEED_RESET) {
		qlcnic_83xx_idc_update_audit_reg(adapter, 0, 1);
		set_bit(__QLCNIC_RESETTING, &adapter->state);
		clear_bit(QLC_83XX_MBX_READY, &mbx->status);
		if (adapter->ahw->nic_mode == QLCNIC_VNIC_MODE)
			qlcnic_83xx_disable_vnic_mode(adapter, 1);

		if (qlcnic_check_diag_status(adapter)) {
			dev_info(&adapter->pdev->dev,
				 "%s: Wait for diag completion\n", __func__);
			adapter->ahw->idc.delay_reset = 1;
			return 0;
		} else {
			qlcnic_83xx_idc_update_drv_ack_reg(adapter, 1, 1);
			qlcnic_83xx_idc_detach_driver(adapter);
		}
	}

	if (qlcnic_check_diag_status(adapter)) {
		dev_info(&adapter->pdev->dev,
			 "%s: Wait for diag completion\n", __func__);
		return  -1;
	} else {
		if (adapter->ahw->idc.delay_reset) {
			qlcnic_83xx_idc_update_drv_ack_reg(adapter, 1, 1);
			qlcnic_83xx_idc_detach_driver(adapter);
			adapter->ahw->idc.delay_reset = 0;
		}

		/* Check for ACK from other functions */
		ret = qlcnic_83xx_idc_check_reset_ack_reg(adapter);
		if (ret) {
			dev_info(&adapter->pdev->dev,
				 "%s: Waiting for reset ACK\n", __func__);
			return -1;
		}
	}

	/* Transit to INIT state and restart the HW */
	qlcnic_83xx_idc_enter_init_state(adapter, 1);

	return ret;
}

static int qlcnic_83xx_idc_need_quiesce_state(struct qlcnic_adapter *adapter)
{
	dev_err(&adapter->pdev->dev, "%s: TBD\n", __func__);
	return 0;
}

static void qlcnic_83xx_idc_failed_state(struct qlcnic_adapter *adapter)
{
	struct qlcnic_hardware_context *ahw = adapter->ahw;
	u32 val, owner;

	val = QLCRDX(adapter->ahw, QLC_83XX_IDC_CTRL);
	if (val & QLC_83XX_IDC_DISABLE_FW_RESET_RECOVERY) {
		owner = qlcnic_83xx_idc_find_reset_owner_id(adapter);
		if (ahw->pci_func == owner) {
			qlcnic_83xx_stop_hw(adapter);
			qlcnic_dump_fw(adapter);
		}
	}

	netdev_warn(adapter->netdev, "%s: Reboot will be required to recover the adapter!!\n",
		    __func__);
	clear_bit(__QLCNIC_RESETTING, &adapter->state);
	ahw->idc.err_code = -EIO;

	return;
}

static int qlcnic_83xx_idc_quiesce_state(struct qlcnic_adapter *adapter)
{
	dev_info(&adapter->pdev->dev, "%s: TBD\n", __func__);
	return 0;
}

static int qlcnic_83xx_idc_check_state_validity(struct qlcnic_adapter *adapter,
						u32 state)
{
	u32 cur, prev, next;

	cur = adapter->ahw->idc.curr_state;
	prev = adapter->ahw->idc.prev_state;
	next = state;

	if ((next < QLC_83XX_IDC_DEV_COLD) ||
	    (next > QLC_83XX_IDC_DEV_QUISCENT)) {
		dev_err(&adapter->pdev->dev,
			"%s: curr %d, prev %d, next state %d is  invalid\n",
			__func__, cur, prev, state);
		return 1;
	}

	if ((cur == QLC_83XX_IDC_DEV_UNKNOWN) &&
	    (prev == QLC_83XX_IDC_DEV_UNKNOWN)) {
		if ((next != QLC_83XX_IDC_DEV_COLD) &&
		    (next != QLC_83XX_IDC_DEV_READY)) {
			dev_err(&adapter->pdev->dev,
				"%s: failed, cur %d prev %d next %d\n",
				__func__, cur, prev, next);
			return 1;
		}
	}

	if (next == QLC_83XX_IDC_DEV_INIT) {
		if ((prev != QLC_83XX_IDC_DEV_INIT) &&
		    (prev != QLC_83XX_IDC_DEV_COLD) &&
		    (prev != QLC_83XX_IDC_DEV_NEED_RESET)) {
			dev_err(&adapter->pdev->dev,
				"%s: failed, cur %d prev %d next %d\n",
				__func__, cur, prev, next);
			return 1;
		}
	}

	return 0;
}

#define QLC_83XX_ENCAP_TYPE_VXLAN	BIT_1
#define QLC_83XX_MATCH_ENCAP_ID		BIT_2
#define QLC_83XX_SET_VXLAN_UDP_DPORT	BIT_3
#define QLC_83XX_VXLAN_UDP_DPORT(PORT)	((PORT & 0xffff) << 16)

#define QLCNIC_ENABLE_INGRESS_ENCAP_PARSING 1
#define QLCNIC_DISABLE_INGRESS_ENCAP_PARSING 0

int qlcnic_set_vxlan_port(struct qlcnic_adapter *adapter, u16 port)
{
	struct qlcnic_cmd_args cmd;
	int ret = 0;

	memset(&cmd, 0, sizeof(cmd));

	ret = qlcnic_alloc_mbx_args(&cmd, adapter,
				    QLCNIC_CMD_INIT_NIC_FUNC);
	if (ret)
		return ret;

	cmd.req.arg[1] = QLC_83XX_MULTI_TENANCY_INFO;
	cmd.req.arg[2] = QLC_83XX_ENCAP_TYPE_VXLAN |
			 QLC_83XX_SET_VXLAN_UDP_DPORT |
			 QLC_83XX_VXLAN_UDP_DPORT(port);

	ret = qlcnic_issue_cmd(adapter, &cmd);
	if (ret)
		netdev_err(adapter->netdev,
			   "Failed to set VXLAN port %d in adapter\n",
			   port);

	qlcnic_free_mbx_args(&cmd);

	return ret;
}

int qlcnic_set_vxlan_parsing(struct qlcnic_adapter *adapter, u16 port)
{
	struct qlcnic_cmd_args cmd;
	int ret = 0;

	memset(&cmd, 0, sizeof(cmd));

	ret = qlcnic_alloc_mbx_args(&cmd, adapter,
				    QLCNIC_CMD_SET_INGRESS_ENCAP);
	if (ret)
		return ret;

	cmd.req.arg[1] = port ? QLCNIC_ENABLE_INGRESS_ENCAP_PARSING :
				QLCNIC_DISABLE_INGRESS_ENCAP_PARSING;

	ret = qlcnic_issue_cmd(adapter, &cmd);
	if (ret)
		netdev_err(adapter->netdev,
			   "Failed to %s VXLAN parsing for port %d\n",
			   port ? "enable" : "disable", port);
	else
		netdev_info(adapter->netdev,
			    "%s VXLAN parsing for port %d\n",
			    port ? "Enabled" : "Disabled", port);

	qlcnic_free_mbx_args(&cmd);

	return ret;
}

static void qlcnic_83xx_periodic_tasks(struct qlcnic_adapter *adapter)
{
	if (adapter->fhash.fnum)
		qlcnic_prune_lb_filters(adapter);
}

/**
 * qlcnic_83xx_idc_poll_dev_state
 *
 * @work: kernel work queue structure used to schedule the function
 *
 * Poll device state periodically and perform state specific
 * actions defined by Inter Driver Communication (IDC) protocol.
 *
 * Returns: None
 *
 **/
void qlcnic_83xx_idc_poll_dev_state(struct work_struct *work)
{
	struct qlcnic_adapter *adapter;
	u32 state;

	adapter = container_of(work, struct qlcnic_adapter, fw_work.work);
	state =	QLCRDX(adapter->ahw, QLC_83XX_IDC_DEV_STATE);

	if (qlcnic_83xx_idc_check_state_validity(adapter, state)) {
		qlcnic_83xx_idc_log_state_history(adapter);
		adapter->ahw->idc.curr_state = QLC_83XX_IDC_DEV_UNKNOWN;
	} else {
		adapter->ahw->idc.curr_state = state;
	}

	switch (adapter->ahw->idc.curr_state) {
	case QLC_83XX_IDC_DEV_READY:
		qlcnic_83xx_idc_ready_state(adapter);
		break;
	case QLC_83XX_IDC_DEV_NEED_RESET:
		qlcnic_83xx_idc_need_reset_state(adapter);
		break;
	case QLC_83XX_IDC_DEV_NEED_QUISCENT:
		qlcnic_83xx_idc_need_quiesce_state(adapter);
		break;
	case QLC_83XX_IDC_DEV_FAILED:
		qlcnic_83xx_idc_failed_state(adapter);
		return;
	case QLC_83XX_IDC_DEV_INIT:
		qlcnic_83xx_idc_init_state(adapter);
		break;
	case QLC_83XX_IDC_DEV_QUISCENT:
		qlcnic_83xx_idc_quiesce_state(adapter);
		break;
	default:
		qlcnic_83xx_idc_unknown_state(adapter);
		return;
	}
	adapter->ahw->idc.prev_state = adapter->ahw->idc.curr_state;
	qlcnic_83xx_periodic_tasks(adapter);

	/* Re-schedule the function */
	if (test_bit(QLC_83XX_MODULE_LOADED, &adapter->ahw->idc.status))
		qlcnic_schedule_work(adapter, qlcnic_83xx_idc_poll_dev_state,
				     adapter->ahw->idc.delay);
}

static void qlcnic_83xx_setup_idc_parameters(struct qlcnic_adapter *adapter)
{
	u32 idc_params, val;

	if (qlcnic_83xx_flash_read32(adapter, QLC_83XX_IDC_FLASH_PARAM_ADDR,
				     (u8 *)&idc_params, 1)) {
		dev_info(&adapter->pdev->dev,
			 "%s:failed to get IDC params from flash\n", __func__);
		adapter->dev_init_timeo = QLC_83XX_IDC_INIT_TIMEOUT_SECS;
		adapter->reset_ack_timeo = QLC_83XX_IDC_RESET_TIMEOUT_SECS;
	} else {
		adapter->dev_init_timeo = idc_params & 0xFFFF;
		adapter->reset_ack_timeo = ((idc_params >> 16) & 0xFFFF);
	}

	adapter->ahw->idc.curr_state = QLC_83XX_IDC_DEV_UNKNOWN;
	adapter->ahw->idc.prev_state = QLC_83XX_IDC_DEV_UNKNOWN;
	adapter->ahw->idc.delay = QLC_83XX_IDC_FW_POLL_DELAY;
	adapter->ahw->idc.err_code = 0;
	adapter->ahw->idc.collect_dump = 0;
	adapter->ahw->idc.name = (char **)qlc_83xx_idc_states;

	clear_bit(__QLCNIC_RESETTING, &adapter->state);
	set_bit(QLC_83XX_MODULE_LOADED, &adapter->ahw->idc.status);

	/* Check if reset recovery is disabled */
	if (!qlcnic_auto_fw_reset) {
		/* Propagate do not reset request to other functions */
		val = QLCRDX(adapter->ahw, QLC_83XX_IDC_CTRL);
		val = val | QLC_83XX_IDC_DISABLE_FW_RESET_RECOVERY;
		QLCWRX(adapter->ahw, QLC_83XX_IDC_CTRL, val);
	}
}

static int
qlcnic_83xx_idc_first_to_load_function_handler(struct qlcnic_adapter *adapter)
{
	u32 state, val;

	if (qlcnic_83xx_lock_driver(adapter))
		return -EIO;

	/* Clear driver lock register */
	QLCWRX(adapter->ahw, QLC_83XX_RECOVER_DRV_LOCK, 0);
	if (qlcnic_83xx_idc_update_major_version(adapter, 0)) {
		qlcnic_83xx_unlock_driver(adapter);
		return -EIO;
	}

	state =	QLCRDX(adapter->ahw, QLC_83XX_IDC_DEV_STATE);
	if (qlcnic_83xx_idc_check_state_validity(adapter, state)) {
		qlcnic_83xx_unlock_driver(adapter);
		return -EIO;
	}

	if (state != QLC_83XX_IDC_DEV_COLD && qlcnic_load_fw_file) {
		QLCWRX(adapter->ahw, QLC_83XX_IDC_DEV_STATE,
		       QLC_83XX_IDC_DEV_COLD);
		state = QLC_83XX_IDC_DEV_COLD;
	}

	adapter->ahw->idc.curr_state = state;
	/* First to load function should cold boot the device */
	if (state == QLC_83XX_IDC_DEV_COLD)
		qlcnic_83xx_idc_cold_state_handler(adapter);

	/* Check if reset recovery is enabled */
	if (qlcnic_auto_fw_reset) {
		val = QLCRDX(adapter->ahw, QLC_83XX_IDC_CTRL);
		val = val & ~QLC_83XX_IDC_DISABLE_FW_RESET_RECOVERY;
		QLCWRX(adapter->ahw, QLC_83XX_IDC_CTRL, val);
	}

	qlcnic_83xx_unlock_driver(adapter);

	return 0;
}

int qlcnic_83xx_idc_init(struct qlcnic_adapter *adapter)
{
	int ret = -EIO;

	qlcnic_83xx_setup_idc_parameters(adapter);

	if (qlcnic_83xx_get_reset_instruction_template(adapter))
		return ret;

	if (!qlcnic_83xx_idc_check_driver_presence_reg(adapter)) {
		if (qlcnic_83xx_idc_first_to_load_function_handler(adapter))
			return -EIO;
	} else {
		if (qlcnic_83xx_idc_check_major_version(adapter))
			return -EIO;
	}

	qlcnic_83xx_idc_update_audit_reg(adapter, 0, 1);

	return 0;
}

void qlcnic_83xx_idc_exit(struct qlcnic_adapter *adapter)
{
	int id;
	u32 val;

	while (test_and_set_bit(__QLCNIC_RESETTING, &adapter->state))
		usleep_range(10000, 11000);

	id = QLCRDX(adapter->ahw, QLC_83XX_DRV_LOCK_ID);
	id = id & 0xFF;

	if (id == adapter->portnum) {
		dev_err(&adapter->pdev->dev,
			"%s: wait for lock recovery.. %d\n", __func__, id);
		msleep(20);
		id = QLCRDX(adapter->ahw, QLC_83XX_DRV_LOCK_ID);
		id = id & 0xFF;
	}

	/* Clear driver presence bit */
	val = QLCRDX(adapter->ahw, QLC_83XX_IDC_DRV_PRESENCE);
	val = val & ~(1 << adapter->portnum);
	QLCWRX(adapter->ahw, QLC_83XX_IDC_DRV_PRESENCE, val);
	clear_bit(QLC_83XX_MODULE_LOADED, &adapter->ahw->idc.status);
	clear_bit(__QLCNIC_RESETTING, &adapter->state);

	cancel_delayed_work_sync(&adapter->fw_work);
}

void qlcnic_83xx_idc_request_reset(struct qlcnic_adapter *adapter, u32 key)
{
	u32 val;

	if (qlcnic_sriov_vf_check(adapter))
		return;

	if (qlcnic_83xx_lock_driver(adapter)) {
		dev_err(&adapter->pdev->dev,
			"%s:failed, please retry\n", __func__);
		return;
	}

	val = QLCRDX(adapter->ahw, QLC_83XX_IDC_CTRL);
	if (val & QLC_83XX_IDC_DISABLE_FW_RESET_RECOVERY) {
		netdev_info(adapter->netdev, "%s: Auto firmware recovery is disabled\n",
			    __func__);
		qlcnic_83xx_idc_enter_failed_state(adapter, 0);
		qlcnic_83xx_unlock_driver(adapter);
		return;
	}

	if (key == QLCNIC_FORCE_FW_RESET) {
		val = QLCRDX(adapter->ahw, QLC_83XX_IDC_CTRL);
		val = val | QLC_83XX_IDC_GRACEFULL_RESET;
		QLCWRX(adapter->ahw, QLC_83XX_IDC_CTRL, val);
	} else if (key == QLCNIC_FORCE_FW_DUMP_KEY) {
		adapter->ahw->idc.collect_dump = 1;
	}

	qlcnic_83xx_unlock_driver(adapter);
	return;
}

static int qlcnic_83xx_copy_bootloader(struct qlcnic_adapter *adapter)
{
	u8 *p_cache;
	u32 src, size;
	u64 dest;
	int ret = -EIO;

	src = QLC_83XX_BOOTLOADER_FLASH_ADDR;
	dest = QLCRDX(adapter->ahw, QLCNIC_BOOTLOADER_ADDR);
	size = QLCRDX(adapter->ahw, QLCNIC_BOOTLOADER_SIZE);

	/* alignment check */
	if (size & 0xF)
		size = (size + 16) & ~0xF;

	p_cache = vzalloc(size);
	if (p_cache == NULL)
		return -ENOMEM;

	ret = qlcnic_83xx_lockless_flash_read32(adapter, src, p_cache,
						size / sizeof(u32));
	if (ret) {
		vfree(p_cache);
		return ret;
	}
	/* 16 byte write to MS memory */
	ret = qlcnic_ms_mem_write128(adapter, dest, (u32 *)p_cache,
				     size / 16);
	if (ret) {
		vfree(p_cache);
		return ret;
	}
	vfree(p_cache);

	return ret;
}

static int qlcnic_83xx_copy_fw_file(struct qlcnic_adapter *adapter)
{
	struct qlc_83xx_fw_info *fw_info = adapter->ahw->fw_info;
	const struct firmware *fw = fw_info->fw;
	u32 dest, *p_cache, *temp;
	int i, ret = -EIO;
	__le32 *temp_le;
	u8 data[16];
	size_t size;
	u64 addr;

	temp = vzalloc(fw->size);
	if (!temp) {
		release_firmware(fw);
		fw_info->fw = NULL;
		return -ENOMEM;
	}

	temp_le = (__le32 *)fw->data;

	/* FW image in file is in little endian, swap the data to nullify
	 * the effect of writel() operation on big endian platform.
	 */
	for (i = 0; i < fw->size / sizeof(u32); i++)
		temp[i] = __le32_to_cpu(temp_le[i]);

	dest = QLCRDX(adapter->ahw, QLCNIC_FW_IMAGE_ADDR);
	size = (fw->size & ~0xF);
	p_cache = temp;
	addr = (u64)dest;

	ret = qlcnic_ms_mem_write128(adapter, addr,
				     p_cache, size / 16);
	if (ret) {
		dev_err(&adapter->pdev->dev, "MS memory write failed\n");
		goto exit;
	}

	/* alignment check */
	if (fw->size & 0xF) {
		addr = dest + size;
		for (i = 0; i < (fw->size & 0xF); i++)
			data[i] = ((u8 *)temp)[size + i];
		for (; i < 16; i++)
			data[i] = 0;
		ret = qlcnic_ms_mem_write128(adapter, addr,
					     (u32 *)data, 1);
		if (ret) {
			dev_err(&adapter->pdev->dev,
				"MS memory write failed\n");
			goto exit;
		}
	}

exit:
	release_firmware(fw);
	fw_info->fw = NULL;
	vfree(temp);

	return ret;
}

static void qlcnic_83xx_dump_pause_control_regs(struct qlcnic_adapter *adapter)
{
	int i, j;
	u32 val = 0, val1 = 0, reg = 0;
	int err = 0;

	val = QLCRD32(adapter, QLC_83XX_SRE_SHIM_REG, &err);
	if (err == -EIO)
		return;
	dev_info(&adapter->pdev->dev, "SRE-Shim Ctrl:0x%x\n", val);

	for (j = 0; j < 2; j++) {
		if (j == 0) {
			dev_info(&adapter->pdev->dev,
				 "Port 0 RxB Pause Threshold Regs[TC7..TC0]:");
			reg = QLC_83XX_PORT0_THRESHOLD;
		} else if (j == 1) {
			dev_info(&adapter->pdev->dev,
				 "Port 1 RxB Pause Threshold Regs[TC7..TC0]:");
			reg = QLC_83XX_PORT1_THRESHOLD;
		}
		for (i = 0; i < 8; i++) {
			val = QLCRD32(adapter, reg + (i * 0x4), &err);
			if (err == -EIO)
				return;
			dev_info(&adapter->pdev->dev, "0x%x  ", val);
		}
		dev_info(&adapter->pdev->dev, "\n");
	}

	for (j = 0; j < 2; j++) {
		if (j == 0) {
			dev_info(&adapter->pdev->dev,
				 "Port 0 RxB TC Max Cell Registers[4..1]:");
			reg = QLC_83XX_PORT0_TC_MC_REG;
		} else if (j == 1) {
			dev_info(&adapter->pdev->dev,
				 "Port 1 RxB TC Max Cell Registers[4..1]:");
			reg = QLC_83XX_PORT1_TC_MC_REG;
		}
		for (i = 0; i < 4; i++) {
			val = QLCRD32(adapter, reg + (i * 0x4), &err);
			if (err == -EIO)
				return;
			dev_info(&adapter->pdev->dev, "0x%x  ", val);
		}
		dev_info(&adapter->pdev->dev, "\n");
	}

	for (j = 0; j < 2; j++) {
		if (j == 0) {
			dev_info(&adapter->pdev->dev,
				 "Port 0 RxB Rx TC Stats[TC7..TC0]:");
			reg = QLC_83XX_PORT0_TC_STATS;
		} else if (j == 1) {
			dev_info(&adapter->pdev->dev,
				 "Port 1 RxB Rx TC Stats[TC7..TC0]:");
			reg = QLC_83XX_PORT1_TC_STATS;
		}
		for (i = 7; i >= 0; i--) {
			val = QLCRD32(adapter, reg, &err);
			if (err == -EIO)
				return;
			val &= ~(0x7 << 29);    /* Reset bits 29 to 31 */
			QLCWR32(adapter, reg, (val | (i << 29)));
			val = QLCRD32(adapter, reg, &err);
			if (err == -EIO)
				return;
			dev_info(&adapter->pdev->dev, "0x%x  ", val);
		}
		dev_info(&adapter->pdev->dev, "\n");
	}

	val = QLCRD32(adapter, QLC_83XX_PORT2_IFB_THRESHOLD, &err);
	if (err == -EIO)
		return;
	val1 = QLCRD32(adapter, QLC_83XX_PORT3_IFB_THRESHOLD, &err);
	if (err == -EIO)
		return;
	dev_info(&adapter->pdev->dev,
		 "IFB-Pause Thresholds: Port 2:0x%x, Port 3:0x%x\n",
		 val, val1);
}


static void qlcnic_83xx_disable_pause_frames(struct qlcnic_adapter *adapter)
{
	u32 reg = 0, i, j;

	if (qlcnic_83xx_lock_driver(adapter)) {
		dev_err(&adapter->pdev->dev,
			"%s:failed to acquire driver lock\n", __func__);
		return;
	}

	qlcnic_83xx_dump_pause_control_regs(adapter);
	QLCWR32(adapter, QLC_83XX_SRE_SHIM_REG, 0x0);

	for (j = 0; j < 2; j++) {
		if (j == 0)
			reg = QLC_83XX_PORT0_THRESHOLD;
		else if (j == 1)
			reg = QLC_83XX_PORT1_THRESHOLD;

		for (i = 0; i < 8; i++)
			QLCWR32(adapter, reg + (i * 0x4), 0x0);
	}

	for (j = 0; j < 2; j++) {
		if (j == 0)
			reg = QLC_83XX_PORT0_TC_MC_REG;
		else if (j == 1)
			reg = QLC_83XX_PORT1_TC_MC_REG;

		for (i = 0; i < 4; i++)
			QLCWR32(adapter, reg + (i * 0x4), 0x03FF03FF);
	}

	QLCWR32(adapter, QLC_83XX_PORT2_IFB_THRESHOLD, 0);
	QLCWR32(adapter, QLC_83XX_PORT3_IFB_THRESHOLD, 0);
	dev_info(&adapter->pdev->dev,
		 "Disabled pause frames successfully on all ports\n");
	qlcnic_83xx_unlock_driver(adapter);
}

static void qlcnic_83xx_take_eport_out_of_reset(struct qlcnic_adapter *adapter)
{
	QLCWR32(adapter, QLC_83XX_RESET_REG, 0);
	QLCWR32(adapter, QLC_83XX_RESET_PORT0, 0);
	QLCWR32(adapter, QLC_83XX_RESET_PORT1, 0);
	QLCWR32(adapter, QLC_83XX_RESET_PORT2, 0);
	QLCWR32(adapter, QLC_83XX_RESET_PORT3, 0);
	QLCWR32(adapter, QLC_83XX_RESET_SRESHIM, 0);
	QLCWR32(adapter, QLC_83XX_RESET_EPGSHIM, 0);
	QLCWR32(adapter, QLC_83XX_RESET_ETHERPCS, 0);
	QLCWR32(adapter, QLC_83XX_RESET_CONTROL, 1);
}

static int qlcnic_83xx_check_heartbeat(struct qlcnic_adapter *p_dev)
{
	u32 heartbeat, peg_status;
	int retries, ret = -EIO, err = 0;

	retries = QLCNIC_HEARTBEAT_CHECK_RETRY_COUNT;
	p_dev->heartbeat = QLC_SHARED_REG_RD32(p_dev,
					       QLCNIC_PEG_ALIVE_COUNTER);

	do {
		msleep(QLCNIC_HEARTBEAT_PERIOD_MSECS);
		heartbeat = QLC_SHARED_REG_RD32(p_dev,
						QLCNIC_PEG_ALIVE_COUNTER);
		if (heartbeat != p_dev->heartbeat) {
			ret = QLCNIC_RCODE_SUCCESS;
			break;
		}
	} while (--retries);

	if (ret) {
		dev_err(&p_dev->pdev->dev, "firmware hang detected\n");
		qlcnic_83xx_take_eport_out_of_reset(p_dev);
		qlcnic_83xx_disable_pause_frames(p_dev);
		peg_status = QLC_SHARED_REG_RD32(p_dev,
						 QLCNIC_PEG_HALT_STATUS1);
		dev_info(&p_dev->pdev->dev, "Dumping HW/FW registers\n"
			 "PEG_HALT_STATUS1: 0x%x, PEG_HALT_STATUS2: 0x%x,\n"
			 "PEG_NET_0_PC: 0x%x, PEG_NET_1_PC: 0x%x,\n"
			 "PEG_NET_2_PC: 0x%x, PEG_NET_3_PC: 0x%x,\n"
			 "PEG_NET_4_PC: 0x%x\n", peg_status,
			 QLC_SHARED_REG_RD32(p_dev, QLCNIC_PEG_HALT_STATUS2),
			 QLCRD32(p_dev, QLC_83XX_CRB_PEG_NET_0, &err),
			 QLCRD32(p_dev, QLC_83XX_CRB_PEG_NET_1, &err),
			 QLCRD32(p_dev, QLC_83XX_CRB_PEG_NET_2, &err),
			 QLCRD32(p_dev, QLC_83XX_CRB_PEG_NET_3, &err),
			 QLCRD32(p_dev, QLC_83XX_CRB_PEG_NET_4, &err));

		if (QLCNIC_FWERROR_CODE(peg_status) == 0x67)
			dev_err(&p_dev->pdev->dev,
				"Device is being reset err code 0x00006700.\n");
	}

	return ret;
}

static int qlcnic_83xx_check_cmd_peg_status(struct qlcnic_adapter *p_dev)
{
	int retries = QLCNIC_CMDPEG_CHECK_RETRY_COUNT;
	u32 val;

	do {
		val = QLC_SHARED_REG_RD32(p_dev, QLCNIC_CMDPEG_STATE);
		if (val == QLC_83XX_CMDPEG_COMPLETE)
			return 0;
		msleep(QLCNIC_CMDPEG_CHECK_DELAY);
	} while (--retries);

	dev_err(&p_dev->pdev->dev, "%s: failed, state = 0x%x\n", __func__, val);
	return -EIO;
}

static int qlcnic_83xx_check_hw_status(struct qlcnic_adapter *p_dev)
{
	int err;

	err = qlcnic_83xx_check_cmd_peg_status(p_dev);
	if (err)
		return err;

	err = qlcnic_83xx_check_heartbeat(p_dev);
	if (err)
		return err;

	return err;
}

static int qlcnic_83xx_poll_reg(struct qlcnic_adapter *p_dev, u32 addr,
				int duration, u32 mask, u32 status)
{
	int timeout_error, err = 0;
	u32 value;
	u8 retries;

	value = QLCRD32(p_dev, addr, &err);
	if (err == -EIO)
		return err;
	retries = duration / 10;

	do {
		if ((value & mask) != status) {
			timeout_error = 1;
			msleep(duration / 10);
			value = QLCRD32(p_dev, addr, &err);
			if (err == -EIO)
				return err;
		} else {
			timeout_error = 0;
			break;
		}
	} while (retries--);

	if (timeout_error) {
		p_dev->ahw->reset.seq_error++;
		dev_err(&p_dev->pdev->dev,
			"%s: Timeout Err, entry_num = %d\n",
			__func__, p_dev->ahw->reset.seq_index);
		dev_err(&p_dev->pdev->dev,
			"0x%08x 0x%08x 0x%08x\n",
			value, mask, status);
	}

	return timeout_error;
}

static int qlcnic_83xx_reset_template_checksum(struct qlcnic_adapter *p_dev)
{
	u32 sum = 0;
	u16 *buff = (u16 *)p_dev->ahw->reset.buff;
	int count = p_dev->ahw->reset.hdr->size / sizeof(u16);

	while (count-- > 0)
		sum += *buff++;

	while (sum >> 16)
		sum = (sum & 0xFFFF) + (sum >> 16);

	if (~sum) {
		return 0;
	} else {
		dev_err(&p_dev->pdev->dev, "%s: failed\n", __func__);
		return -1;
	}
}

static int qlcnic_83xx_get_reset_instruction_template(struct qlcnic_adapter *p_dev)
{
	struct qlcnic_hardware_context *ahw = p_dev->ahw;
	u32 addr, count, prev_ver, curr_ver;
	u8 *p_buff;

	if (ahw->reset.buff != NULL) {
		prev_ver = p_dev->fw_version;
		curr_ver = qlcnic_83xx_get_fw_version(p_dev);
		if (curr_ver > prev_ver)
			kfree(ahw->reset.buff);
		else
			return 0;
	}

	ahw->reset.seq_error = 0;
	ahw->reset.buff = kzalloc(QLC_83XX_RESTART_TEMPLATE_SIZE, GFP_KERNEL);
	if (ahw->reset.buff == NULL)
		return -ENOMEM;

	p_buff = p_dev->ahw->reset.buff;
	addr = QLC_83XX_RESET_TEMPLATE_ADDR;
	count = sizeof(struct qlc_83xx_reset_hdr) / sizeof(u32);

	/* Copy template header from flash */
	if (qlcnic_83xx_flash_read32(p_dev, addr, p_buff, count)) {
		dev_err(&p_dev->pdev->dev, "%s: flash read failed\n", __func__);
		return -EIO;
	}
	ahw->reset.hdr = (struct qlc_83xx_reset_hdr *)ahw->reset.buff;
	addr = QLC_83XX_RESET_TEMPLATE_ADDR + ahw->reset.hdr->hdr_size;
	p_buff = ahw->reset.buff + ahw->reset.hdr->hdr_size;
	count = (ahw->reset.hdr->size - ahw->reset.hdr->hdr_size) / sizeof(u32);

	/* Copy rest of the template */
	if (qlcnic_83xx_flash_read32(p_dev, addr, p_buff, count)) {
		dev_err(&p_dev->pdev->dev, "%s: flash read failed\n", __func__);
		return -EIO;
	}

	if (qlcnic_83xx_reset_template_checksum(p_dev))
		return -EIO;
	/* Get Stop, Start and Init command offsets */
	ahw->reset.init_offset = ahw->reset.buff + ahw->reset.hdr->init_offset;
	ahw->reset.start_offset = ahw->reset.buff +
				  ahw->reset.hdr->start_offset;
	ahw->reset.stop_offset = ahw->reset.buff + ahw->reset.hdr->hdr_size;
	return 0;
}

/* Read Write HW register command */
static void qlcnic_83xx_read_write_crb_reg(struct qlcnic_adapter *p_dev,
					   u32 raddr, u32 waddr)
{
	int err = 0;
	u32 value;

	value = QLCRD32(p_dev, raddr, &err);
	if (err == -EIO)
		return;
	qlcnic_83xx_wrt_reg_indirect(p_dev, waddr, value);
}

/* Read Modify Write HW register command */
static void qlcnic_83xx_rmw_crb_reg(struct qlcnic_adapter *p_dev,
				    u32 raddr, u32 waddr,
				    struct qlc_83xx_rmw *p_rmw_hdr)
{
	int err = 0;
	u32 value;

	if (p_rmw_hdr->index_a) {
		value = p_dev->ahw->reset.array[p_rmw_hdr->index_a];
	} else {
		value = QLCRD32(p_dev, raddr, &err);
		if (err == -EIO)
			return;
	}

	value &= p_rmw_hdr->mask;
	value <<= p_rmw_hdr->shl;
	value >>= p_rmw_hdr->shr;
	value |= p_rmw_hdr->or_value;
	value ^= p_rmw_hdr->xor_value;
	qlcnic_83xx_wrt_reg_indirect(p_dev, waddr, value);
}

/* Write HW register command */
static void qlcnic_83xx_write_list(struct qlcnic_adapter *p_dev,
				   struct qlc_83xx_entry_hdr *p_hdr)
{
	int i;
	struct qlc_83xx_entry *entry;

	entry = (struct qlc_83xx_entry *)((char *)p_hdr +
					  sizeof(struct qlc_83xx_entry_hdr));

	for (i = 0; i < p_hdr->count; i++, entry++) {
		qlcnic_83xx_wrt_reg_indirect(p_dev, entry->arg1,
					     entry->arg2);
		if (p_hdr->delay)
			udelay((u32)(p_hdr->delay));
	}
}

/* Read and Write instruction */
static void qlcnic_83xx_read_write_list(struct qlcnic_adapter *p_dev,
					struct qlc_83xx_entry_hdr *p_hdr)
{
	int i;
	struct qlc_83xx_entry *entry;

	entry = (struct qlc_83xx_entry *)((char *)p_hdr +
					  sizeof(struct qlc_83xx_entry_hdr));

	for (i = 0; i < p_hdr->count; i++, entry++) {
		qlcnic_83xx_read_write_crb_reg(p_dev, entry->arg1,
					       entry->arg2);
		if (p_hdr->delay)
			udelay((u32)(p_hdr->delay));
	}
}

/* Poll HW register command */
static void qlcnic_83xx_poll_list(struct qlcnic_adapter *p_dev,
				  struct qlc_83xx_entry_hdr *p_hdr)
{
	long delay;
	struct qlc_83xx_entry *entry;
	struct qlc_83xx_poll *poll;
	int i, err = 0;
	unsigned long arg1, arg2;

	poll = (struct qlc_83xx_poll *)((char *)p_hdr +
					sizeof(struct qlc_83xx_entry_hdr));

	entry = (struct qlc_83xx_entry *)((char *)poll +
					  sizeof(struct qlc_83xx_poll));
	delay = (long)p_hdr->delay;

	if (!delay) {
		for (i = 0; i < p_hdr->count; i++, entry++)
			qlcnic_83xx_poll_reg(p_dev, entry->arg1,
					     delay, poll->mask,
					     poll->status);
	} else {
		for (i = 0; i < p_hdr->count; i++, entry++) {
			arg1 = entry->arg1;
			arg2 = entry->arg2;
			if (delay) {
				if (qlcnic_83xx_poll_reg(p_dev,
							 arg1, delay,
							 poll->mask,
							 poll->status)){
					QLCRD32(p_dev, arg1, &err);
					if (err == -EIO)
						return;
					QLCRD32(p_dev, arg2, &err);
					if (err == -EIO)
						return;
				}
			}
		}
	}
}

/* Poll and write HW register command */
static void qlcnic_83xx_poll_write_list(struct qlcnic_adapter *p_dev,
					struct qlc_83xx_entry_hdr *p_hdr)
{
	int i;
	long delay;
	struct qlc_83xx_quad_entry *entry;
	struct qlc_83xx_poll *poll;

	poll = (struct qlc_83xx_poll *)((char *)p_hdr +
					sizeof(struct qlc_83xx_entry_hdr));
	entry = (struct qlc_83xx_quad_entry *)((char *)poll +
					       sizeof(struct qlc_83xx_poll));
	delay = (long)p_hdr->delay;

	for (i = 0; i < p_hdr->count; i++, entry++) {
		qlcnic_83xx_wrt_reg_indirect(p_dev, entry->dr_addr,
					     entry->dr_value);
		qlcnic_83xx_wrt_reg_indirect(p_dev, entry->ar_addr,
					     entry->ar_value);
		if (delay)
			qlcnic_83xx_poll_reg(p_dev, entry->ar_addr, delay,
					     poll->mask, poll->status);
	}
}

/* Read Modify Write register command */
static void qlcnic_83xx_read_modify_write(struct qlcnic_adapter *p_dev,
					  struct qlc_83xx_entry_hdr *p_hdr)
{
	int i;
	struct qlc_83xx_entry *entry;
	struct qlc_83xx_rmw *rmw_hdr;

	rmw_hdr = (struct qlc_83xx_rmw *)((char *)p_hdr +
					  sizeof(struct qlc_83xx_entry_hdr));

	entry = (struct qlc_83xx_entry *)((char *)rmw_hdr +
					  sizeof(struct qlc_83xx_rmw));

	for (i = 0; i < p_hdr->count; i++, entry++) {
		qlcnic_83xx_rmw_crb_reg(p_dev, entry->arg1,
					entry->arg2, rmw_hdr);
		if (p_hdr->delay)
			udelay((u32)(p_hdr->delay));
	}
}

static void qlcnic_83xx_pause(struct qlc_83xx_entry_hdr *p_hdr)
{
	if (p_hdr->delay)
		mdelay((u32)((long)p_hdr->delay));
}

/* Read and poll register command */
static void qlcnic_83xx_poll_read_list(struct qlcnic_adapter *p_dev,
				       struct qlc_83xx_entry_hdr *p_hdr)
{
	long delay;
	int index, i, j, err;
	struct qlc_83xx_quad_entry *entry;
	struct qlc_83xx_poll *poll;
	unsigned long addr;

	poll = (struct qlc_83xx_poll *)((char *)p_hdr +
					sizeof(struct qlc_83xx_entry_hdr));

	entry = (struct qlc_83xx_quad_entry *)((char *)poll +
					       sizeof(struct qlc_83xx_poll));
	delay = (long)p_hdr->delay;

	for (i = 0; i < p_hdr->count; i++, entry++) {
		qlcnic_83xx_wrt_reg_indirect(p_dev, entry->ar_addr,
					     entry->ar_value);
		if (delay) {
			if (!qlcnic_83xx_poll_reg(p_dev, entry->ar_addr, delay,
						  poll->mask, poll->status)){
				index = p_dev->ahw->reset.array_index;
				addr = entry->dr_addr;
				j = QLCRD32(p_dev, addr, &err);
				if (err == -EIO)
					return;

				p_dev->ahw->reset.array[index++] = j;

				if (index == QLC_83XX_MAX_RESET_SEQ_ENTRIES)
					p_dev->ahw->reset.array_index = 1;
			}
		}
	}
}

static inline void qlcnic_83xx_seq_end(struct qlcnic_adapter *p_dev)
{
	p_dev->ahw->reset.seq_end = 1;
}

static void qlcnic_83xx_template_end(struct qlcnic_adapter *p_dev)
{
	p_dev->ahw->reset.template_end = 1;
	if (p_dev->ahw->reset.seq_error == 0)
		dev_err(&p_dev->pdev->dev,
			"HW restart process completed successfully.\n");
	else
		dev_err(&p_dev->pdev->dev,
			"HW restart completed with timeout errors.\n");
}

/**
* qlcnic_83xx_exec_template_cmd
*
* @p_dev: adapter structure
* @p_buff: Poiter to instruction template
*
* Template provides instructions to stop, restart and initalize firmware.
* These instructions are abstracted as a series of read, write and
* poll operations on hardware registers. Register information and operation
* specifics are not exposed to the driver. Driver reads the template from
* flash and executes the instructions located at pre-defined offsets.
*
* Returns: None
* */
static void qlcnic_83xx_exec_template_cmd(struct qlcnic_adapter *p_dev,
					  char *p_buff)
{
	int index, entries;
	struct qlc_83xx_entry_hdr *p_hdr;
	char *entry = p_buff;

	p_dev->ahw->reset.seq_end = 0;
	p_dev->ahw->reset.template_end = 0;
	entries = p_dev->ahw->reset.hdr->entries;
	index = p_dev->ahw->reset.seq_index;

	for (; (!p_dev->ahw->reset.seq_end) && (index < entries); index++) {
		p_hdr = (struct qlc_83xx_entry_hdr *)entry;

		switch (p_hdr->cmd) {
		case QLC_83XX_OPCODE_NOP:
			break;
		case QLC_83XX_OPCODE_WRITE_LIST:
			qlcnic_83xx_write_list(p_dev, p_hdr);
			break;
		case QLC_83XX_OPCODE_READ_WRITE_LIST:
			qlcnic_83xx_read_write_list(p_dev, p_hdr);
			break;
		case QLC_83XX_OPCODE_POLL_LIST:
			qlcnic_83xx_poll_list(p_dev, p_hdr);
			break;
		case QLC_83XX_OPCODE_POLL_WRITE_LIST:
			qlcnic_83xx_poll_write_list(p_dev, p_hdr);
			break;
		case QLC_83XX_OPCODE_READ_MODIFY_WRITE:
			qlcnic_83xx_read_modify_write(p_dev, p_hdr);
			break;
		case QLC_83XX_OPCODE_SEQ_PAUSE:
			qlcnic_83xx_pause(p_hdr);
			break;
		case QLC_83XX_OPCODE_SEQ_END:
			qlcnic_83xx_seq_end(p_dev);
			break;
		case QLC_83XX_OPCODE_TMPL_END:
			qlcnic_83xx_template_end(p_dev);
			break;
		case QLC_83XX_OPCODE_POLL_READ_LIST:
			qlcnic_83xx_poll_read_list(p_dev, p_hdr);
			break;
		default:
			dev_err(&p_dev->pdev->dev,
				"%s: Unknown opcode 0x%04x in template %d\n",
				__func__, p_hdr->cmd, index);
			break;
		}
		entry += p_hdr->size;
		cond_resched();
	}
	p_dev->ahw->reset.seq_index = index;
}

static void qlcnic_83xx_stop_hw(struct qlcnic_adapter *p_dev)
{
	p_dev->ahw->reset.seq_index = 0;

	qlcnic_83xx_exec_template_cmd(p_dev, p_dev->ahw->reset.stop_offset);
	if (p_dev->ahw->reset.seq_end != 1)
		dev_err(&p_dev->pdev->dev, "%s: failed\n", __func__);
}

static void qlcnic_83xx_start_hw(struct qlcnic_adapter *p_dev)
{
	qlcnic_83xx_exec_template_cmd(p_dev, p_dev->ahw->reset.start_offset);
	if (p_dev->ahw->reset.template_end != 1)
		dev_err(&p_dev->pdev->dev, "%s: failed\n", __func__);
}

static void qlcnic_83xx_init_hw(struct qlcnic_adapter *p_dev)
{
	qlcnic_83xx_exec_template_cmd(p_dev, p_dev->ahw->reset.init_offset);
	if (p_dev->ahw->reset.seq_end != 1)
		dev_err(&p_dev->pdev->dev, "%s: failed\n", __func__);
}

/* POST FW related definations*/
#define QLC_83XX_POST_SIGNATURE_REG	0x41602014
#define QLC_83XX_POST_MODE_REG		0x41602018
#define QLC_83XX_POST_FAST_MODE		0
#define QLC_83XX_POST_MEDIUM_MODE	1
#define QLC_83XX_POST_SLOW_MODE		2

/* POST Timeout values in milliseconds */
#define QLC_83XX_POST_FAST_MODE_TIMEOUT	690
#define QLC_83XX_POST_MED_MODE_TIMEOUT	2930
#define QLC_83XX_POST_SLOW_MODE_TIMEOUT	7500

/* POST result values */
#define QLC_83XX_POST_PASS			0xfffffff0
#define QLC_83XX_POST_ASIC_STRESS_TEST_FAIL	0xffffffff
#define QLC_83XX_POST_DDR_TEST_FAIL		0xfffffffe
#define QLC_83XX_POST_ASIC_MEMORY_TEST_FAIL	0xfffffffc
#define QLC_83XX_POST_FLASH_TEST_FAIL		0xfffffff8

static int qlcnic_83xx_run_post(struct qlcnic_adapter *adapter)
{
	struct qlc_83xx_fw_info *fw_info = adapter->ahw->fw_info;
	struct device *dev = &adapter->pdev->dev;
	int timeout, count, ret = 0;
	u32 signature;

	/* Set timeout values with extra 2 seconds of buffer */
	switch (adapter->ahw->post_mode) {
	case QLC_83XX_POST_FAST_MODE:
		timeout = QLC_83XX_POST_FAST_MODE_TIMEOUT + 2000;
		break;
	case QLC_83XX_POST_MEDIUM_MODE:
		timeout = QLC_83XX_POST_MED_MODE_TIMEOUT + 2000;
		break;
	case QLC_83XX_POST_SLOW_MODE:
		timeout = QLC_83XX_POST_SLOW_MODE_TIMEOUT + 2000;
		break;
	default:
		return -EINVAL;
	}

	strncpy(fw_info->fw_file_name, QLC_83XX_POST_FW_FILE_NAME,
		QLC_FW_FILE_NAME_LEN);

	ret = request_firmware(&fw_info->fw, fw_info->fw_file_name, dev);
	if (ret) {
		dev_err(dev, "POST firmware can not be loaded, skipping POST\n");
		return 0;
	}

	ret = qlcnic_83xx_copy_fw_file(adapter);
	if (ret)
		return ret;

	/* clear QLC_83XX_POST_SIGNATURE_REG register */
	qlcnic_ind_wr(adapter, QLC_83XX_POST_SIGNATURE_REG, 0);

	/* Set POST mode */
	qlcnic_ind_wr(adapter, QLC_83XX_POST_MODE_REG,
		      adapter->ahw->post_mode);

	QLC_SHARED_REG_WR32(adapter, QLCNIC_FW_IMG_VALID,
			    QLC_83XX_BOOT_FROM_FILE);

	qlcnic_83xx_start_hw(adapter);

	count = 0;
	do {
		msleep(100);
		count += 100;

		signature = qlcnic_ind_rd(adapter, QLC_83XX_POST_SIGNATURE_REG);
		if (signature == QLC_83XX_POST_PASS)
			break;
	} while (timeout > count);

	if (timeout <= count) {
		dev_err(dev, "POST timed out, signature = 0x%08x\n", signature);
		return -EIO;
	}

	switch (signature) {
	case QLC_83XX_POST_PASS:
		dev_info(dev, "POST passed, Signature = 0x%08x\n", signature);
		break;
	case QLC_83XX_POST_ASIC_STRESS_TEST_FAIL:
		dev_err(dev, "POST failed, Test case : ASIC STRESS TEST, Signature = 0x%08x\n",
			signature);
		ret = -EIO;
		break;
	case QLC_83XX_POST_DDR_TEST_FAIL:
		dev_err(dev, "POST failed, Test case : DDT TEST, Signature = 0x%08x\n",
			signature);
		ret = -EIO;
		break;
	case QLC_83XX_POST_ASIC_MEMORY_TEST_FAIL:
		dev_err(dev, "POST failed, Test case : ASIC MEMORY TEST, Signature = 0x%08x\n",
			signature);
		ret = -EIO;
		break;
	case QLC_83XX_POST_FLASH_TEST_FAIL:
		dev_err(dev, "POST failed, Test case : FLASH TEST, Signature = 0x%08x\n",
			signature);
		ret = -EIO;
		break;
	default:
		dev_err(dev, "POST failed, Test case : INVALID, Signature = 0x%08x\n",
			signature);
		ret = -EIO;
		break;
	}

	return ret;
}

static int qlcnic_83xx_load_fw_image_from_host(struct qlcnic_adapter *adapter)
{
	struct qlc_83xx_fw_info *fw_info = adapter->ahw->fw_info;
	int err = -EIO;

	if (request_firmware(&fw_info->fw, fw_info->fw_file_name,
			     &(adapter->pdev->dev))) {
		dev_err(&adapter->pdev->dev,
			"No file FW image, loading flash FW image.\n");
		QLC_SHARED_REG_WR32(adapter, QLCNIC_FW_IMG_VALID,
				    QLC_83XX_BOOT_FROM_FLASH);
	} else {
		if (qlcnic_83xx_copy_fw_file(adapter))
			return err;
		QLC_SHARED_REG_WR32(adapter, QLCNIC_FW_IMG_VALID,
				    QLC_83XX_BOOT_FROM_FILE);
	}

	return 0;
}

static int qlcnic_83xx_restart_hw(struct qlcnic_adapter *adapter)
{
	u32 val;
	int err = -EIO;

	qlcnic_83xx_stop_hw(adapter);

	/* Collect FW register dump if required */
	val = QLCRDX(adapter->ahw, QLC_83XX_IDC_CTRL);
	if (!(val & QLC_83XX_IDC_GRACEFULL_RESET))
		qlcnic_dump_fw(adapter);

	if (val & QLC_83XX_IDC_DISABLE_FW_RESET_RECOVERY) {
		netdev_info(adapter->netdev, "%s: Auto firmware recovery is disabled\n",
			    __func__);
		qlcnic_83xx_idc_enter_failed_state(adapter, 1);
		return err;
	}

	qlcnic_83xx_init_hw(adapter);

	if (qlcnic_83xx_copy_bootloader(adapter))
		return err;

	/* Check if POST needs to be run */
	if (adapter->ahw->run_post) {
		err = qlcnic_83xx_run_post(adapter);
		if (err)
			return err;

		/* No need to run POST in next reset sequence */
		adapter->ahw->run_post = false;

		/* Again reset the adapter to load regular firmware  */
		qlcnic_83xx_stop_hw(adapter);
		qlcnic_83xx_init_hw(adapter);

		err = qlcnic_83xx_copy_bootloader(adapter);
		if (err)
			return err;
	}

	/* Boot either flash image or firmware image from host file system */
	if (qlcnic_load_fw_file == 1) {
		err = qlcnic_83xx_load_fw_image_from_host(adapter);
		if (err)
			return err;
	} else {
		QLC_SHARED_REG_WR32(adapter, QLCNIC_FW_IMG_VALID,
				    QLC_83XX_BOOT_FROM_FLASH);
	}

	qlcnic_83xx_start_hw(adapter);
	if (qlcnic_83xx_check_hw_status(adapter))
		return -EIO;

	return 0;
}

static int qlcnic_83xx_get_nic_configuration(struct qlcnic_adapter *adapter)
{
	int err;
	struct qlcnic_info nic_info;
	struct qlcnic_hardware_context *ahw = adapter->ahw;

	memset(&nic_info, 0, sizeof(struct qlcnic_info));
	err = qlcnic_get_nic_info(adapter, &nic_info, ahw->pci_func);
	if (err)
		return -EIO;

	ahw->physical_port = (u8) nic_info.phys_port;
	ahw->switch_mode = nic_info.switch_mode;
	ahw->max_tx_ques = nic_info.max_tx_ques;
	ahw->max_rx_ques = nic_info.max_rx_ques;
	ahw->capabilities = nic_info.capabilities;
	ahw->max_mac_filters = nic_info.max_mac_filters;
	ahw->max_mtu = nic_info.max_mtu;

	/* eSwitch capability indicates vNIC mode.
	 * vNIC and SRIOV are mutually exclusive operational modes.
	 * If SR-IOV capability is detected, SR-IOV physical function
	 * will get initialized in default mode.
	 * SR-IOV virtual function initialization follows a
	 * different code path and opmode.
	 * SRIOV mode has precedence over vNIC mode.
	 */
	if (test_bit(__QLCNIC_SRIOV_CAPABLE, &adapter->state))
		return QLC_83XX_DEFAULT_OPMODE;

	if (ahw->capabilities & QLC_83XX_ESWITCH_CAPABILITY)
		return QLCNIC_VNIC_MODE;

	return QLC_83XX_DEFAULT_OPMODE;
}

int qlcnic_83xx_configure_opmode(struct qlcnic_adapter *adapter)
{
	struct qlcnic_hardware_context *ahw = adapter->ahw;
	u16 max_sds_rings, max_tx_rings;
	int ret;

	ret = qlcnic_83xx_get_nic_configuration(adapter);
	if (ret == -EIO)
		return -EIO;

	if (ret == QLCNIC_VNIC_MODE) {
		ahw->nic_mode = QLCNIC_VNIC_MODE;

		if (qlcnic_83xx_config_vnic_opmode(adapter))
			return -EIO;

		max_sds_rings = QLCNIC_MAX_VNIC_SDS_RINGS;
		max_tx_rings = QLCNIC_MAX_VNIC_TX_RINGS;
	} else if (ret == QLC_83XX_DEFAULT_OPMODE) {
		ahw->nic_mode = QLCNIC_DEFAULT_MODE;
		adapter->nic_ops->init_driver = qlcnic_83xx_init_default_driver;
		ahw->idc.state_entry = qlcnic_83xx_idc_ready_state_entry;
		max_sds_rings = QLCNIC_MAX_SDS_RINGS;
		max_tx_rings = QLCNIC_MAX_TX_RINGS;
	} else {
		dev_err(&adapter->pdev->dev, "%s: Invalid opmode %d\n",
			__func__, ret);
		return -EIO;
	}

	adapter->max_sds_rings = min(ahw->max_rx_ques, max_sds_rings);
	adapter->max_tx_rings = min(ahw->max_tx_ques, max_tx_rings);

	return 0;
}

static void qlcnic_83xx_config_buff_descriptors(struct qlcnic_adapter *adapter)
{
	struct qlcnic_hardware_context *ahw = adapter->ahw;

	if (ahw->port_type == QLCNIC_XGBE) {
		adapter->num_rxd = DEFAULT_RCV_DESCRIPTORS_10G;
		adapter->max_rxd = MAX_RCV_DESCRIPTORS_10G;
		adapter->num_jumbo_rxd = MAX_JUMBO_RCV_DESCRIPTORS_10G;
		adapter->max_jumbo_rxd = MAX_JUMBO_RCV_DESCRIPTORS_10G;

	} else if (ahw->port_type == QLCNIC_GBE) {
		adapter->num_rxd = DEFAULT_RCV_DESCRIPTORS_1G;
		adapter->num_jumbo_rxd = MAX_JUMBO_RCV_DESCRIPTORS_1G;
		adapter->max_jumbo_rxd = MAX_JUMBO_RCV_DESCRIPTORS_1G;
		adapter->max_rxd = MAX_RCV_DESCRIPTORS_1G;
	}
	adapter->num_txd = MAX_CMD_DESCRIPTORS;
	adapter->max_rds_rings = MAX_RDS_RINGS;
}

static int qlcnic_83xx_init_default_driver(struct qlcnic_adapter *adapter)
{
	int err = -EIO;

	qlcnic_83xx_get_minidump_template(adapter);
	if (qlcnic_83xx_get_port_info(adapter))
		return err;

	qlcnic_83xx_config_buff_descriptors(adapter);
	adapter->ahw->msix_supported = !!qlcnic_use_msi_x;
	adapter->flags |= QLCNIC_ADAPTER_INITIALIZED;

	dev_info(&adapter->pdev->dev, "HAL Version: %d\n",
		 adapter->ahw->fw_hal_version);

	return 0;
}

#define IS_QLC_83XX_USED(a, b, c) (((1 << a->portnum) & b) || ((c >> 6) & 0x1))
static void qlcnic_83xx_clear_function_resources(struct qlcnic_adapter *adapter)
{
	struct qlcnic_cmd_args cmd;
	u32 presence_mask, audit_mask;
	int status;

	presence_mask = QLCRDX(adapter->ahw, QLC_83XX_IDC_DRV_PRESENCE);
	audit_mask = QLCRDX(adapter->ahw, QLC_83XX_IDC_DRV_AUDIT);

	if (IS_QLC_83XX_USED(adapter, presence_mask, audit_mask)) {
		status = qlcnic_alloc_mbx_args(&cmd, adapter,
					       QLCNIC_CMD_STOP_NIC_FUNC);
		if (status)
			return;

		cmd.req.arg[1] = BIT_31;
		status = qlcnic_issue_cmd(adapter, &cmd);
		if (status)
			dev_err(&adapter->pdev->dev,
				"Failed to clean up the function resources\n");
		qlcnic_free_mbx_args(&cmd);
	}
}

static int qlcnic_83xx_get_fw_info(struct qlcnic_adapter *adapter)
{
	struct qlcnic_hardware_context *ahw = adapter->ahw;
	struct pci_dev *pdev = adapter->pdev;
	struct qlc_83xx_fw_info *fw_info;
	int err = 0;

	ahw->fw_info = kzalloc(sizeof(*fw_info), GFP_KERNEL);
	if (!ahw->fw_info) {
		err = -ENOMEM;
	} else {
		fw_info = ahw->fw_info;
		switch (pdev->device) {
		case PCI_DEVICE_ID_QLOGIC_QLE834X:
		case PCI_DEVICE_ID_QLOGIC_QLE8830:
			strncpy(fw_info->fw_file_name, QLC_83XX_FW_FILE_NAME,
				QLC_FW_FILE_NAME_LEN);
			break;
		case PCI_DEVICE_ID_QLOGIC_QLE844X:
			strncpy(fw_info->fw_file_name, QLC_84XX_FW_FILE_NAME,
				QLC_FW_FILE_NAME_LEN);
			break;
		default:
			dev_err(&pdev->dev, "%s: Invalid device id\n",
				__func__);
			err = -EINVAL;
			break;
		}
	}

	return err;
}

static void qlcnic_83xx_init_rings(struct qlcnic_adapter *adapter)
{
	u8 rx_cnt = QLCNIC_DEF_SDS_RINGS;
	u8 tx_cnt = QLCNIC_DEF_TX_RINGS;

	adapter->max_tx_rings = QLCNIC_MAX_TX_RINGS;
	adapter->max_sds_rings = QLCNIC_MAX_SDS_RINGS;

	if (!adapter->ahw->msix_supported) {
		rx_cnt = QLCNIC_SINGLE_RING;
		tx_cnt = QLCNIC_SINGLE_RING;
	}

	/* compute and set drv sds rings */
	qlcnic_set_tx_ring_count(adapter, tx_cnt);
	qlcnic_set_sds_ring_count(adapter, rx_cnt);
}

int qlcnic_83xx_init(struct qlcnic_adapter *adapter, int pci_using_dac)
{
	struct qlcnic_hardware_context *ahw = adapter->ahw;
	int err = 0;

	adapter->rx_mac_learn = false;
	ahw->msix_supported = !!qlcnic_use_msi_x;

	/* Check if POST needs to be run */
	switch (qlcnic_load_fw_file) {
	case 2:
		ahw->post_mode = QLC_83XX_POST_FAST_MODE;
		ahw->run_post = true;
		break;
	case 3:
		ahw->post_mode = QLC_83XX_POST_MEDIUM_MODE;
		ahw->run_post = true;
		break;
	case 4:
		ahw->post_mode = QLC_83XX_POST_SLOW_MODE;
		ahw->run_post = true;
		break;
	default:
		ahw->run_post = false;
		break;
	}

	qlcnic_83xx_init_rings(adapter);

	err = qlcnic_83xx_init_mailbox_work(adapter);
	if (err)
		goto exit;

	if (qlcnic_sriov_vf_check(adapter)) {
		err = qlcnic_sriov_vf_init(adapter, pci_using_dac);
		if (err)
			goto detach_mbx;
		else
			return err;
	}

	if (qlcnic_83xx_read_flash_descriptor_table(adapter) ||
	    qlcnic_83xx_read_flash_mfg_id(adapter)) {
		dev_err(&adapter->pdev->dev, "Failed reading flash mfg id\n");
		err = -ENOTRECOVERABLE;
		goto detach_mbx;
	}

	err = qlcnic_83xx_check_hw_status(adapter);
	if (err)
		goto detach_mbx;

	err = qlcnic_83xx_get_fw_info(adapter);
	if (err)
		goto detach_mbx;

	err = qlcnic_83xx_idc_init(adapter);
	if (err)
		goto detach_mbx;

	err = qlcnic_setup_intr(adapter);
	if (err) {
		dev_err(&adapter->pdev->dev, "Failed to setup interrupt\n");
		goto disable_intr;
	}

	INIT_DELAYED_WORK(&adapter->idc_aen_work, qlcnic_83xx_idc_aen_work);

	err = qlcnic_83xx_setup_mbx_intr(adapter);
	if (err)
		goto disable_mbx_intr;

	qlcnic_83xx_clear_function_resources(adapter);
	qlcnic_dcb_enable(adapter->dcb);
	qlcnic_83xx_initialize_nic(adapter, 1);
	qlcnic_dcb_get_info(adapter->dcb);

	/* Configure default, SR-IOV or Virtual NIC mode of operation */
	err = qlcnic_83xx_configure_opmode(adapter);
	if (err)
		goto disable_mbx_intr;


	/* Perform operating mode specific initialization */
	err = adapter->nic_ops->init_driver(adapter);
	if (err)
		goto disable_mbx_intr;

	/* Periodically monitor device status */
	qlcnic_83xx_idc_poll_dev_state(&adapter->fw_work.work);
	return 0;

disable_mbx_intr:
	qlcnic_83xx_free_mbx_intr(adapter);

disable_intr:
	qlcnic_teardown_intr(adapter);

detach_mbx:
	qlcnic_83xx_detach_mailbox_work(adapter);
	qlcnic_83xx_free_mailbox(ahw->mailbox);
	ahw->mailbox = NULL;
exit:
	return err;
}

void qlcnic_83xx_aer_stop_poll_work(struct qlcnic_adapter *adapter)
{
	struct qlcnic_hardware_context *ahw = adapter->ahw;
	struct qlc_83xx_idc *idc = &ahw->idc;

	clear_bit(QLC_83XX_MBX_READY, &idc->status);
	cancel_delayed_work_sync(&adapter->fw_work);

	if (ahw->nic_mode == QLCNIC_VNIC_MODE)
		qlcnic_83xx_disable_vnic_mode(adapter, 1);

	qlcnic_83xx_idc_detach_driver(adapter);
	qlcnic_83xx_initialize_nic(adapter, 0);

	cancel_delayed_work_sync(&adapter->idc_aen_work);
}

int qlcnic_83xx_aer_reset(struct qlcnic_adapter *adapter)
{
	struct qlcnic_hardware_context *ahw = adapter->ahw;
	struct qlc_83xx_idc *idc = &ahw->idc;
	int ret = 0;
	u32 owner;

	/* Mark the previous IDC state as NEED_RESET so
	 * that state_entry() will perform the reattachment
	 * and bringup the device
	 */
	idc->prev_state = QLC_83XX_IDC_DEV_NEED_RESET;
	owner = qlcnic_83xx_idc_find_reset_owner_id(adapter);
	if (ahw->pci_func == owner) {
		ret = qlcnic_83xx_restart_hw(adapter);
		if (ret < 0)
			return ret;
		qlcnic_83xx_idc_clear_registers(adapter, 0);
	}

	ret = idc->state_entry(adapter);
	return ret;
}

void qlcnic_83xx_aer_start_poll_work(struct qlcnic_adapter *adapter)
{
	struct qlcnic_hardware_context *ahw = adapter->ahw;
	struct qlc_83xx_idc *idc = &ahw->idc;
	u32 owner;

	idc->prev_state = QLC_83XX_IDC_DEV_READY;
	owner = qlcnic_83xx_idc_find_reset_owner_id(adapter);
	if (ahw->pci_func == owner)
		qlcnic_83xx_idc_enter_ready_state(adapter, 0);

	qlcnic_schedule_work(adapter, qlcnic_83xx_idc_poll_dev_state, 0);
}
