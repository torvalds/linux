/*
 * QLogic qlcnic NIC Driver
 * Copyright (c) 2009-2013 QLogic Corporation
 *
 * See LICENSE.qlcnic for copyright and licensing details.
 */

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

static int qlcnic_83xx_init_default_driver(struct qlcnic_adapter *adapter);
static int qlcnic_83xx_configure_opmode(struct qlcnic_adapter *adapter);
static int qlcnic_83xx_check_heartbeat(struct qlcnic_adapter *p_dev);
static int qlcnic_83xx_restart_hw(struct qlcnic_adapter *adapter);

/* Template header */
struct qlc_83xx_reset_hdr {
	u16	version;
	u16	signature;
	u16	size;
	u16	entries;
	u16	hdr_size;
	u16	checksum;
	u16	init_offset;
	u16	start_offset;
} __packed;

/* Command entry header. */
struct qlc_83xx_entry_hdr {
	u16 cmd;
	u16 size;
	u16 count;
	u16 delay;
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
	u8	shl;
	u8	shr;
	u8	index_a;
	u8	rsvd;
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

/* Device States */
enum qlcnic_83xx_states {
	QLC_83XX_IDC_DEV_UNKNOWN,
	QLC_83XX_IDC_DEV_COLD,
	QLC_83XX_IDC_DEV_INIT,
	QLC_83XX_IDC_DEV_READY,
	QLC_83XX_IDC_DEV_NEED_RESET,
	QLC_83XX_IDC_DEV_NEED_QUISCENT,
	QLC_83XX_IDC_DEV_FAILED,
	QLC_83XX_IDC_DEV_QUISCENT
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

	val = adapter->portnum & 0xf;
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
	/* Clear gracefull reset bit */
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
	dev_err(&adapter->pdev->dev, "%s:\n", __func__);

	adapter->netdev->trans_start = jiffies;

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
	/* Disable mailbox interrupt */
	QLCWRX(adapter->ahw, QLCNIC_MBX_INTR_ENBL, 0);
	qlcnic_down(adapter, netdev);
	for (i = 0; i < adapter->ahw->num_msix; i++) {
		adapter->ahw->intr_tbl[i].id = i;
		adapter->ahw->intr_tbl[i].enabled = 0;
		adapter->ahw->intr_tbl[i].src = 0;
	}
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
	if (netif_running(netdev)) {
		netif_carrier_on(netdev);
		netif_wake_queue(netdev);
	}
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

static int qlcnic_83xx_idc_reattach_driver(struct qlcnic_adapter *adapter)
{
	/* register for NIC IDC AEN Events */
	qlcnic_83xx_register_nic_idc_func(adapter, 1);

	qlcnic_83xx_enable_mbx_intrpt(adapter);
	if ((adapter->flags & QLCNIC_MSIX_ENABLED)) {
		if (qlcnic_83xx_config_intrpt(adapter, 1)) {
			netdev_err(adapter->netdev,
				   "Failed to enable mbx intr\n");
			return -EIO;
		}
	}

	if (qlcnic_83xx_configure_opmode(adapter)) {
		qlcnic_83xx_idc_enter_failed_state(adapter, 1);
		return -EIO;
	}

	if (adapter->nic_ops->init_driver(adapter)) {
		qlcnic_83xx_idc_enter_failed_state(adapter, 1);
		return -EIO;
	}

	qlcnic_83xx_idc_attach_driver(adapter);

	return 0;
}

static void qlcnic_83xx_idc_update_idc_params(struct qlcnic_adapter *adapter)
{
	qlcnic_83xx_idc_update_drv_presence_reg(adapter, 1, 1);
	clear_bit(__QLCNIC_RESETTING, &adapter->state);
	set_bit(QLC_83XX_MBX_READY, &adapter->ahw->idc.status);
	qlcnic_83xx_idc_update_audit_reg(adapter, 0, 1);
	set_bit(QLC_83XX_MODULE_LOADED, &adapter->ahw->idc.status);
	adapter->ahw->idc.quiesce_req = 0;
	adapter->ahw->idc.delay = QLC_83XX_IDC_FW_POLL_DELAY;
	adapter->ahw->idc.err_code = 0;
	adapter->ahw->idc.collect_dump = 0;
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
		return ret;
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
	u32 val;
	struct qlcnic_hardware_context *ahw = adapter->ahw;
	int ret = 0;

	/* Perform NIC configuration based ready state entry actions */
	if (ahw->idc.state_entry(adapter))
		return -EIO;

	if (qlcnic_check_temp(adapter)) {
		if (ahw->temp == QLCNIC_TEMP_PANIC) {
			qlcnic_83xx_idc_check_fan_failure(adapter);
			dev_err(&adapter->pdev->dev,
				"Error: device temperature %d above limits\n",
				adapter->ahw->temp);
			clear_bit(QLC_83XX_MBX_READY, &ahw->idc.status);
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
			clear_bit(QLC_83XX_MBX_READY, &ahw->idc.status);
			set_bit(__QLCNIC_RESETTING, &adapter->state);
			qlcnic_83xx_idc_enter_need_reset_state(adapter, 1);
		}
		return -EIO;
	}

	if ((val & QLC_83XX_IDC_GRACEFULL_RESET) || ahw->idc.collect_dump) {
		/* Move to need reset state and prepare for reset */
		qlcnic_83xx_idc_enter_need_reset_state(adapter, 1);
		return ret;
	}

	/* Check for soft reset request */
	if (ahw->reset_context &&
	    !(val & QLC_83XX_IDC_DISABLE_FW_RESET_RECOVERY)) {
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
 *	Reset request ACK's are recieved from all the functions
 *	Wait time exceeds max time limit
 *
 * Returns: Error code or Success(0)
 *
 **/
static int qlcnic_83xx_idc_need_reset_state(struct qlcnic_adapter *adapter)
{
	int ret = 0;

	if (adapter->ahw->idc.prev_state != QLC_83XX_IDC_DEV_NEED_RESET) {
		qlcnic_83xx_idc_update_drv_ack_reg(adapter, 1, 1);
		qlcnic_83xx_idc_update_audit_reg(adapter, 0, 1);
		set_bit(__QLCNIC_RESETTING, &adapter->state);
		clear_bit(QLC_83XX_MBX_READY, &adapter->ahw->idc.status);
		if (adapter->ahw->nic_mode == QLC_83XX_VIRTUAL_NIC_MODE)
			qlcnic_83xx_disable_vnic_mode(adapter, 1);
		qlcnic_83xx_idc_detach_driver(adapter);
	}

	/* Check ACK from other functions */
	ret = qlcnic_83xx_idc_check_reset_ack_reg(adapter);
	if (ret) {
		dev_info(&adapter->pdev->dev,
			 "%s: Waiting for reset ACK\n", __func__);
		return 0;
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

static int qlcnic_83xx_idc_failed_state(struct qlcnic_adapter *adapter)
{
	dev_err(&adapter->pdev->dev, "%s: please restart!!\n", __func__);
	adapter->ahw->idc.err_code = -EIO;

	return 0;
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

	if (qlcnic_83xx_lockless_flash_read32(adapter,
					      QLC_83XX_IDC_FLASH_PARAM_ADDR,
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
	set_bit(QLC_83XX_MBX_READY, &adapter->ahw->idc.status);
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

static int qlcnic_83xx_idc_init(struct qlcnic_adapter *adapter)
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

	if (qlcnic_83xx_lock_driver(adapter)) {
		dev_err(&adapter->pdev->dev,
			"%s:failed, please retry\n", __func__);
		return;
	}

	val = QLCRDX(adapter->ahw, QLC_83XX_IDC_CTRL);
	if ((val & QLC_83XX_IDC_DISABLE_FW_RESET_RECOVERY) ||
	    !qlcnic_auto_fw_reset) {
		dev_err(&adapter->pdev->dev,
			"%s:failed, device in non reset mode\n", __func__);
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

	p_cache = kzalloc(size, GFP_KERNEL);
	if (p_cache == NULL)
		return -ENOMEM;

	ret = qlcnic_83xx_lockless_flash_read32(adapter, src, p_cache,
						size / sizeof(u32));
	if (ret) {
		kfree(p_cache);
		return ret;
	}
	/* 16 byte write to MS memory */
	ret = qlcnic_83xx_ms_mem_write128(adapter, dest, (u32 *)p_cache,
					  size / 16);
	if (ret) {
		kfree(p_cache);
		return ret;
	}
	kfree(p_cache);

	return ret;
}

static int qlcnic_83xx_copy_fw_file(struct qlcnic_adapter *adapter)
{
	u32 dest, *p_cache;
	u64 addr;
	u8 data[16];
	size_t size;
	int i, ret = -EIO;

	dest = QLCRDX(adapter->ahw, QLCNIC_FW_IMAGE_ADDR);
	size = (adapter->ahw->fw_info.fw->size & ~0xF);
	p_cache = (u32 *)adapter->ahw->fw_info.fw->data;
	addr = (u64)dest;

	ret = qlcnic_83xx_ms_mem_write128(adapter, addr,
					  (u32 *)p_cache, size / 16);
	if (ret) {
		dev_err(&adapter->pdev->dev, "MS memory write failed\n");
		release_firmware(adapter->ahw->fw_info.fw);
		adapter->ahw->fw_info.fw = NULL;
		return -EIO;
	}

	/* alignment check */
	if (adapter->ahw->fw_info.fw->size & 0xF) {
		addr = dest + size;
		for (i = 0; i < (adapter->ahw->fw_info.fw->size & 0xF); i++)
			data[i] = adapter->ahw->fw_info.fw->data[size + i];
		for (; i < 16; i++)
			data[i] = 0;
		ret = qlcnic_83xx_ms_mem_write128(adapter, addr,
						  (u32 *)data, 1);
		if (ret) {
			dev_err(&adapter->pdev->dev,
				"MS memory write failed\n");
			release_firmware(adapter->ahw->fw_info.fw);
			adapter->ahw->fw_info.fw = NULL;
			return -EIO;
		}
	}
	release_firmware(adapter->ahw->fw_info.fw);
	adapter->ahw->fw_info.fw = NULL;

	return 0;
}

static void qlcnic_83xx_dump_pause_control_regs(struct qlcnic_adapter *adapter)
{
	int i, j;
	u32 val = 0, val1 = 0, reg = 0;

	val = QLCRD32(adapter, QLC_83XX_SRE_SHIM_REG);
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
			val = QLCRD32(adapter, reg + (i * 0x4));
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
			val = QLCRD32(adapter, reg + (i * 0x4));
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
			val = QLCRD32(adapter, reg);
			val &= ~(0x7 << 29);    /* Reset bits 29 to 31 */
			QLCWR32(adapter, reg, (val | (i << 29)));
			val = QLCRD32(adapter, reg);
			dev_info(&adapter->pdev->dev, "0x%x  ", val);
		}
		dev_info(&adapter->pdev->dev, "\n");
	}

	val = QLCRD32(adapter, QLC_83XX_PORT2_IFB_THRESHOLD);
	val1 = QLCRD32(adapter, QLC_83XX_PORT3_IFB_THRESHOLD);
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

static int qlcnic_83xx_check_heartbeat(struct qlcnic_adapter *p_dev)
{
	u32 heartbeat, peg_status;
	int retries, ret = -EIO;

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
		qlcnic_83xx_disable_pause_frames(p_dev);
		peg_status = QLC_SHARED_REG_RD32(p_dev,
						 QLCNIC_PEG_HALT_STATUS1);
		dev_info(&p_dev->pdev->dev, "Dumping HW/FW registers\n"
			 "PEG_HALT_STATUS1: 0x%x, PEG_HALT_STATUS2: 0x%x,\n"
			 "PEG_NET_0_PC: 0x%x, PEG_NET_1_PC: 0x%x,\n"
			 "PEG_NET_2_PC: 0x%x, PEG_NET_3_PC: 0x%x,\n"
			 "PEG_NET_4_PC: 0x%x\n", peg_status,
			 QLC_SHARED_REG_RD32(p_dev, QLCNIC_PEG_HALT_STATUS2),
			 QLCRD32(p_dev, QLC_83XX_CRB_PEG_NET_0),
			 QLCRD32(p_dev, QLC_83XX_CRB_PEG_NET_1),
			 QLCRD32(p_dev, QLC_83XX_CRB_PEG_NET_2),
			 QLCRD32(p_dev, QLC_83XX_CRB_PEG_NET_3),
			 QLCRD32(p_dev, QLC_83XX_CRB_PEG_NET_4));

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

int qlcnic_83xx_check_hw_status(struct qlcnic_adapter *p_dev)
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
	u32 value;
	int timeout_error;
	u8 retries;

	value = qlcnic_83xx_rd_reg_indirect(p_dev, addr);
	retries = duration / 10;

	do {
		if ((value & mask) != status) {
			timeout_error = 1;
			msleep(duration / 10);
			value = qlcnic_83xx_rd_reg_indirect(p_dev, addr);
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

int qlcnic_83xx_get_reset_instruction_template(struct qlcnic_adapter *p_dev)
{
	u8 *p_buff;
	u32 addr, count;
	struct qlcnic_hardware_context *ahw = p_dev->ahw;

	ahw->reset.seq_error = 0;
	ahw->reset.buff = kzalloc(QLC_83XX_RESTART_TEMPLATE_SIZE, GFP_KERNEL);
	if (p_dev->ahw->reset.buff == NULL)
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
	int value;

	value = qlcnic_83xx_rd_reg_indirect(p_dev, raddr);
	qlcnic_83xx_wrt_reg_indirect(p_dev, waddr, value);
}

/* Read Modify Write HW register command */
static void qlcnic_83xx_rmw_crb_reg(struct qlcnic_adapter *p_dev,
				    u32 raddr, u32 waddr,
				    struct qlc_83xx_rmw *p_rmw_hdr)
{
	int value;

	if (p_rmw_hdr->index_a)
		value = p_dev->ahw->reset.array[p_rmw_hdr->index_a];
	else
		value = qlcnic_83xx_rd_reg_indirect(p_dev, raddr);

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
	int i;
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
					qlcnic_83xx_rd_reg_indirect(p_dev,
								    arg1);
					qlcnic_83xx_rd_reg_indirect(p_dev,
								    arg2);
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
	int index, i, j;
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
				j = qlcnic_83xx_rd_reg_indirect(p_dev, addr);
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

static int qlcnic_83xx_load_fw_image_from_host(struct qlcnic_adapter *adapter)
{
	int err = -EIO;

	if (request_firmware(&adapter->ahw->fw_info.fw,
			     QLC_83XX_FW_FILE_NAME, &(adapter->pdev->dev))) {
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
	qlcnic_83xx_init_hw(adapter);

	if (qlcnic_83xx_copy_bootloader(adapter))
		return err;
	/* Boot either flash image or firmware image from host file system */
	if (qlcnic_load_fw_file) {
		if (qlcnic_83xx_load_fw_image_from_host(adapter))
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

/**
* qlcnic_83xx_config_default_opmode
*
* @adapter: adapter structure
*
* Configure default driver operating mode
*
* Returns: Error code or Success(0)
* */
int qlcnic_83xx_config_default_opmode(struct qlcnic_adapter *adapter)
{
	u32 op_mode;
	struct qlcnic_hardware_context *ahw = adapter->ahw;

	qlcnic_get_func_no(adapter);
	op_mode = QLCRDX(ahw, QLC_83XX_DRV_OP_MODE);

	if (op_mode == QLC_83XX_DEFAULT_OPMODE) {
		adapter->nic_ops->init_driver = qlcnic_83xx_init_default_driver;
		ahw->idc.state_entry = qlcnic_83xx_idc_ready_state_entry;
	} else {
		return -EIO;
	}

	return 0;
}

int qlcnic_83xx_get_nic_configuration(struct qlcnic_adapter *adapter)
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

	if (ahw->capabilities & BIT_23)
		ahw->nic_mode = QLC_83XX_VIRTUAL_NIC_MODE;
	else
		ahw->nic_mode = QLC_83XX_DEFAULT_MODE;

	return ahw->nic_mode;
}

static int qlcnic_83xx_configure_opmode(struct qlcnic_adapter *adapter)
{
	int ret;

	ret = qlcnic_83xx_get_nic_configuration(adapter);
	if (ret == -EIO)
		return -EIO;

	if (ret == QLC_83XX_VIRTUAL_NIC_MODE) {
		if (qlcnic_83xx_config_vnic_opmode(adapter))
			return -EIO;
	} else if (ret == QLC_83XX_DEFAULT_MODE) {
		if (qlcnic_83xx_config_default_opmode(adapter))
			return -EIO;
	}

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
		qlcnic_alloc_mbx_args(&cmd, adapter, QLCNIC_CMD_STOP_NIC_FUNC);
		cmd.req.arg[1] = BIT_31;
		status = qlcnic_issue_cmd(adapter, &cmd);
		if (status)
			dev_err(&adapter->pdev->dev,
				"Failed to clean up the function resources\n");
		qlcnic_free_mbx_args(&cmd);
	}
}

int qlcnic_83xx_init(struct qlcnic_adapter *adapter)
{
	struct qlcnic_hardware_context *ahw = adapter->ahw;

	if (qlcnic_83xx_check_hw_status(adapter))
		return -EIO;

	/* Initilaize 83xx mailbox spinlock */
	spin_lock_init(&ahw->mbx_lock);

	set_bit(QLC_83XX_MBX_READY, &adapter->ahw->idc.status);
	qlcnic_83xx_clear_function_resources(adapter);

	/* register for NIC IDC AEN Events */
	qlcnic_83xx_register_nic_idc_func(adapter, 1);

	if (!qlcnic_83xx_read_flash_descriptor_table(adapter))
		qlcnic_83xx_read_flash_mfg_id(adapter);

	if (qlcnic_83xx_idc_init(adapter))
		return -EIO;

	/* Configure default, SR-IOV or Virtual NIC mode of operation */
	if (qlcnic_83xx_configure_opmode(adapter))
		return -EIO;

	/* Perform operating mode specific initialization */
	if (adapter->nic_ops->init_driver(adapter))
		return -EIO;

	INIT_DELAYED_WORK(&adapter->idc_aen_work, qlcnic_83xx_idc_aen_work);

	/* Periodically monitor device status */
	qlcnic_83xx_idc_poll_dev_state(&adapter->fw_work.work);

	return adapter->ahw->idc.err_code;
}
