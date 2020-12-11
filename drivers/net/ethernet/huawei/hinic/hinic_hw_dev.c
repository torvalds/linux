// SPDX-License-Identifier: GPL-2.0-only
/*
 * Huawei HiNIC PCI Express Linux driver
 * Copyright(c) 2017 Huawei Technologies Co., Ltd
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/log2.h>
#include <linux/err.h>
#include <linux/netdevice.h>
#include <net/devlink.h>

#include "hinic_devlink.h"
#include "hinic_sriov.h"
#include "hinic_dev.h"
#include "hinic_hw_if.h"
#include "hinic_hw_eqs.h"
#include "hinic_hw_mgmt.h"
#include "hinic_hw_qp_ctxt.h"
#include "hinic_hw_qp.h"
#include "hinic_hw_io.h"
#include "hinic_hw_dev.h"

#define IO_STATUS_TIMEOUT               100
#define OUTBOUND_STATE_TIMEOUT          100
#define DB_STATE_TIMEOUT                100

#define MAX_IRQS(max_qps, num_aeqs, num_ceqs)   \
		 (2 * (max_qps) + (num_aeqs) + (num_ceqs))

#define ADDR_IN_4BYTES(addr)            ((addr) >> 2)

enum intr_type {
	INTR_MSIX_TYPE,
};

enum io_status {
	IO_STOPPED = 0,
	IO_RUNNING = 1,
};

/**
 * get_capability - convert device capabilities to NIC capabilities
 * @hwdev: the HW device to set and convert device capabilities for
 * @dev_cap: device capabilities from FW
 *
 * Return 0 - Success, negative - Failure
 **/
static int parse_capability(struct hinic_hwdev *hwdev,
			    struct hinic_dev_cap *dev_cap)
{
	struct hinic_cap *nic_cap = &hwdev->nic_cap;
	int num_aeqs, num_ceqs, num_irqs;

	if (!HINIC_IS_VF(hwdev->hwif) && dev_cap->intr_type != INTR_MSIX_TYPE)
		return -EFAULT;

	num_aeqs = HINIC_HWIF_NUM_AEQS(hwdev->hwif);
	num_ceqs = HINIC_HWIF_NUM_CEQS(hwdev->hwif);
	num_irqs = HINIC_HWIF_NUM_IRQS(hwdev->hwif);

	/* Each QP has its own (SQ + RQ) interrupts */
	nic_cap->num_qps = (num_irqs - (num_aeqs + num_ceqs)) / 2;

	if (nic_cap->num_qps > HINIC_Q_CTXT_MAX)
		nic_cap->num_qps = HINIC_Q_CTXT_MAX;

	if (!HINIC_IS_VF(hwdev->hwif))
		nic_cap->max_qps = dev_cap->max_sqs + 1;
	else
		nic_cap->max_qps = dev_cap->max_sqs;

	if (nic_cap->num_qps > nic_cap->max_qps)
		nic_cap->num_qps = nic_cap->max_qps;

	if (!HINIC_IS_VF(hwdev->hwif)) {
		nic_cap->max_vf = dev_cap->max_vf;
		nic_cap->max_vf_qps = dev_cap->max_vf_sqs + 1;
	}

	hwdev->port_id = dev_cap->port_id;

	return 0;
}

/**
 * get_cap_from_fw - get device capabilities from FW
 * @pfhwdev: the PF HW device to get capabilities for
 *
 * Return 0 - Success, negative - Failure
 **/
static int get_capability(struct hinic_pfhwdev *pfhwdev)
{
	struct hinic_hwdev *hwdev = &pfhwdev->hwdev;
	struct hinic_hwif *hwif = hwdev->hwif;
	struct pci_dev *pdev = hwif->pdev;
	struct hinic_dev_cap dev_cap;
	u16 out_len;
	int err;

	out_len = sizeof(dev_cap);

	err = hinic_msg_to_mgmt(&pfhwdev->pf_to_mgmt, HINIC_MOD_CFGM,
				HINIC_CFG_NIC_CAP, &dev_cap, sizeof(dev_cap),
				&dev_cap, &out_len, HINIC_MGMT_MSG_SYNC);
	if (err) {
		dev_err(&pdev->dev, "Failed to get capability from FW\n");
		return err;
	}

	return parse_capability(hwdev, &dev_cap);
}

/**
 * get_dev_cap - get device capabilities
 * @hwdev: the NIC HW device to get capabilities for
 *
 * Return 0 - Success, negative - Failure
 **/
static int get_dev_cap(struct hinic_hwdev *hwdev)
{
	struct hinic_hwif *hwif = hwdev->hwif;
	struct pci_dev *pdev = hwif->pdev;
	struct hinic_pfhwdev *pfhwdev;
	int err;

	switch (HINIC_FUNC_TYPE(hwif)) {
	case HINIC_PPF:
	case HINIC_PF:
	case HINIC_VF:
		pfhwdev = container_of(hwdev, struct hinic_pfhwdev, hwdev);
		err = get_capability(pfhwdev);
		if (err) {
			dev_err(&pdev->dev, "Failed to get capability\n");
			return err;
		}
		break;
	default:
		dev_err(&pdev->dev, "Unsupported PCI Function type\n");
		return -EINVAL;
	}

	return 0;
}

/**
 * init_msix - enable the msix and save the entries
 * @hwdev: the NIC HW device
 *
 * Return 0 - Success, negative - Failure
 **/
static int init_msix(struct hinic_hwdev *hwdev)
{
	struct hinic_hwif *hwif = hwdev->hwif;
	struct pci_dev *pdev = hwif->pdev;
	int nr_irqs, num_aeqs, num_ceqs;
	size_t msix_entries_size;
	int i, err;

	num_aeqs = HINIC_HWIF_NUM_AEQS(hwif);
	num_ceqs = HINIC_HWIF_NUM_CEQS(hwif);
	nr_irqs = MAX_IRQS(HINIC_MAX_QPS, num_aeqs, num_ceqs);
	if (nr_irqs > HINIC_HWIF_NUM_IRQS(hwif))
		nr_irqs = HINIC_HWIF_NUM_IRQS(hwif);

	msix_entries_size = nr_irqs * sizeof(*hwdev->msix_entries);
	hwdev->msix_entries = devm_kzalloc(&pdev->dev, msix_entries_size,
					   GFP_KERNEL);
	if (!hwdev->msix_entries)
		return -ENOMEM;

	for (i = 0; i < nr_irqs; i++)
		hwdev->msix_entries[i].entry = i;

	err = pci_enable_msix_exact(pdev, hwdev->msix_entries, nr_irqs);
	if (err) {
		dev_err(&pdev->dev, "Failed to enable pci msix\n");
		return err;
	}

	return 0;
}

/**
 * disable_msix - disable the msix
 * @hwdev: the NIC HW device
 **/
static void disable_msix(struct hinic_hwdev *hwdev)
{
	struct hinic_hwif *hwif = hwdev->hwif;
	struct pci_dev *pdev = hwif->pdev;

	pci_disable_msix(pdev);
}

/**
 * hinic_port_msg_cmd - send port msg to mgmt
 * @hwdev: the NIC HW device
 * @cmd: the port command
 * @buf_in: input buffer
 * @in_size: input size
 * @buf_out: output buffer
 * @out_size: returned output size
 *
 * Return 0 - Success, negative - Failure
 **/
int hinic_port_msg_cmd(struct hinic_hwdev *hwdev, enum hinic_port_cmd cmd,
		       void *buf_in, u16 in_size, void *buf_out, u16 *out_size)
{
	struct hinic_pfhwdev *pfhwdev;

	pfhwdev = container_of(hwdev, struct hinic_pfhwdev, hwdev);

	return hinic_msg_to_mgmt(&pfhwdev->pf_to_mgmt, HINIC_MOD_L2NIC, cmd,
				 buf_in, in_size, buf_out, out_size,
				 HINIC_MGMT_MSG_SYNC);
}

int hinic_hilink_msg_cmd(struct hinic_hwdev *hwdev, enum hinic_hilink_cmd cmd,
			 void *buf_in, u16 in_size, void *buf_out,
			 u16 *out_size)
{
	struct hinic_pfhwdev *pfhwdev;

	pfhwdev = container_of(hwdev, struct hinic_pfhwdev, hwdev);

	return hinic_msg_to_mgmt(&pfhwdev->pf_to_mgmt, HINIC_MOD_HILINK, cmd,
				 buf_in, in_size, buf_out, out_size,
				 HINIC_MGMT_MSG_SYNC);
}

/**
 * init_fw_ctxt- Init Firmware tables before network mgmt and io operations
 * @hwdev: the NIC HW device
 *
 * Return 0 - Success, negative - Failure
 **/
static int init_fw_ctxt(struct hinic_hwdev *hwdev)
{
	struct hinic_hwif *hwif = hwdev->hwif;
	struct pci_dev *pdev = hwif->pdev;
	struct hinic_cmd_fw_ctxt fw_ctxt;
	u16 out_size = sizeof(fw_ctxt);
	int err;

	fw_ctxt.func_idx = HINIC_HWIF_FUNC_IDX(hwif);
	fw_ctxt.rx_buf_sz = HINIC_RX_BUF_SZ;

	err = hinic_port_msg_cmd(hwdev, HINIC_PORT_CMD_FWCTXT_INIT,
				 &fw_ctxt, sizeof(fw_ctxt),
				 &fw_ctxt, &out_size);
	if (err || (out_size != sizeof(fw_ctxt)) || fw_ctxt.status) {
		dev_err(&pdev->dev, "Failed to init FW ctxt, err: %d, status: 0x%x, out size: 0x%x\n",
			err, fw_ctxt.status, out_size);
		return -EIO;
	}

	return 0;
}

/**
 * set_hw_ioctxt - set the shape of the IO queues in FW
 * @hwdev: the NIC HW device
 * @rq_depth: rq depth
 * @sq_depth: sq depth
 *
 * Return 0 - Success, negative - Failure
 **/
static int set_hw_ioctxt(struct hinic_hwdev *hwdev, unsigned int sq_depth,
			 unsigned int rq_depth)
{
	struct hinic_hwif *hwif = hwdev->hwif;
	struct hinic_cmd_hw_ioctxt hw_ioctxt;
	struct hinic_pfhwdev *pfhwdev;

	hw_ioctxt.func_idx = HINIC_HWIF_FUNC_IDX(hwif);
	hw_ioctxt.ppf_idx = HINIC_HWIF_PPF_IDX(hwif);

	hw_ioctxt.set_cmdq_depth = HW_IOCTXT_SET_CMDQ_DEPTH_DEFAULT;
	hw_ioctxt.cmdq_depth = 0;

	hw_ioctxt.lro_en = 1;

	hw_ioctxt.rq_depth  = ilog2(rq_depth);

	hw_ioctxt.rx_buf_sz_idx = HINIC_RX_BUF_SZ_IDX;

	hw_ioctxt.sq_depth  = ilog2(sq_depth);

	pfhwdev = container_of(hwdev, struct hinic_pfhwdev, hwdev);

	return hinic_msg_to_mgmt(&pfhwdev->pf_to_mgmt, HINIC_MOD_COMM,
				 HINIC_COMM_CMD_HWCTXT_SET,
				 &hw_ioctxt, sizeof(hw_ioctxt), NULL,
				 NULL, HINIC_MGMT_MSG_SYNC);
}

static int wait_for_outbound_state(struct hinic_hwdev *hwdev)
{
	enum hinic_outbound_state outbound_state;
	struct hinic_hwif *hwif = hwdev->hwif;
	struct pci_dev *pdev = hwif->pdev;
	unsigned long end;

	end = jiffies + msecs_to_jiffies(OUTBOUND_STATE_TIMEOUT);
	do {
		outbound_state = hinic_outbound_state_get(hwif);

		if (outbound_state == HINIC_OUTBOUND_ENABLE)
			return 0;

		msleep(20);
	} while (time_before(jiffies, end));

	dev_err(&pdev->dev, "Wait for OUTBOUND - Timeout\n");
	return -EFAULT;
}

static int wait_for_db_state(struct hinic_hwdev *hwdev)
{
	struct hinic_hwif *hwif = hwdev->hwif;
	struct pci_dev *pdev = hwif->pdev;
	enum hinic_db_state db_state;
	unsigned long end;

	end = jiffies + msecs_to_jiffies(DB_STATE_TIMEOUT);
	do {
		db_state = hinic_db_state_get(hwif);

		if (db_state == HINIC_DB_ENABLE)
			return 0;

		msleep(20);
	} while (time_before(jiffies, end));

	dev_err(&pdev->dev, "Wait for DB - Timeout\n");
	return -EFAULT;
}

/**
 * clear_io_resource - set the IO resources as not active in the NIC
 * @hwdev: the NIC HW device
 *
 * Return 0 - Success, negative - Failure
 **/
static int clear_io_resources(struct hinic_hwdev *hwdev)
{
	struct hinic_cmd_clear_io_res cmd_clear_io_res;
	struct hinic_hwif *hwif = hwdev->hwif;
	struct pci_dev *pdev = hwif->pdev;
	struct hinic_pfhwdev *pfhwdev;
	int err;

	/* sleep 100ms to wait for firmware stopping I/O */
	msleep(100);

	cmd_clear_io_res.func_idx = HINIC_HWIF_FUNC_IDX(hwif);

	pfhwdev = container_of(hwdev, struct hinic_pfhwdev, hwdev);

	err = hinic_msg_to_mgmt(&pfhwdev->pf_to_mgmt, HINIC_MOD_COMM,
				HINIC_COMM_CMD_IO_RES_CLEAR, &cmd_clear_io_res,
				sizeof(cmd_clear_io_res), NULL, NULL,
				HINIC_MGMT_MSG_SYNC);
	if (err) {
		dev_err(&pdev->dev, "Failed to clear IO resources\n");
		return err;
	}

	return 0;
}

/**
 * set_resources_state - set the state of the resources in the NIC
 * @hwdev: the NIC HW device
 * @state: the state to set
 *
 * Return 0 - Success, negative - Failure
 **/
static int set_resources_state(struct hinic_hwdev *hwdev,
			       enum hinic_res_state state)
{
	struct hinic_cmd_set_res_state res_state;
	struct hinic_hwif *hwif = hwdev->hwif;
	struct hinic_pfhwdev *pfhwdev;

	res_state.func_idx = HINIC_HWIF_FUNC_IDX(hwif);
	res_state.state = state;

	pfhwdev = container_of(hwdev, struct hinic_pfhwdev, hwdev);

	return hinic_msg_to_mgmt(&pfhwdev->pf_to_mgmt,
				 HINIC_MOD_COMM,
				 HINIC_COMM_CMD_RES_STATE_SET,
				 &res_state, sizeof(res_state), NULL,
				 NULL, HINIC_MGMT_MSG_SYNC);
}

/**
 * get_base_qpn - get the first qp number
 * @hwdev: the NIC HW device
 * @base_qpn: returned qp number
 *
 * Return 0 - Success, negative - Failure
 **/
static int get_base_qpn(struct hinic_hwdev *hwdev, u16 *base_qpn)
{
	struct hinic_cmd_base_qpn cmd_base_qpn;
	struct hinic_hwif *hwif = hwdev->hwif;
	u16 out_size = sizeof(cmd_base_qpn);
	struct pci_dev *pdev = hwif->pdev;
	int err;

	cmd_base_qpn.func_idx = HINIC_HWIF_FUNC_IDX(hwif);

	err = hinic_port_msg_cmd(hwdev, HINIC_PORT_CMD_GET_GLOBAL_QPN,
				 &cmd_base_qpn, sizeof(cmd_base_qpn),
				 &cmd_base_qpn, &out_size);
	if (err || (out_size != sizeof(cmd_base_qpn)) || cmd_base_qpn.status) {
		dev_err(&pdev->dev, "Failed to get base qpn, err: %d, status: 0x%x, out size: 0x%x\n",
			err, cmd_base_qpn.status, out_size);
		return -EIO;
	}

	*base_qpn = cmd_base_qpn.qpn;
	return 0;
}

/**
 * hinic_hwdev_ifup - Preparing the HW for passing IO
 * @hwdev: the NIC HW device
 * @sq_depth: the send queue depth
 * @rq_depth: the receive queue depth
 *
 * Return 0 - Success, negative - Failure
 **/
int hinic_hwdev_ifup(struct hinic_hwdev *hwdev, u16 sq_depth, u16 rq_depth)
{
	struct hinic_func_to_io *func_to_io = &hwdev->func_to_io;
	struct hinic_cap *nic_cap = &hwdev->nic_cap;
	struct hinic_hwif *hwif = hwdev->hwif;
	int err, num_aeqs, num_ceqs, num_qps;
	struct msix_entry *ceq_msix_entries;
	struct msix_entry *sq_msix_entries;
	struct msix_entry *rq_msix_entries;
	struct pci_dev *pdev = hwif->pdev;
	u16 base_qpn;

	err = get_base_qpn(hwdev, &base_qpn);
	if (err) {
		dev_err(&pdev->dev, "Failed to get global base qp number\n");
		return err;
	}

	num_aeqs = HINIC_HWIF_NUM_AEQS(hwif);
	num_ceqs = HINIC_HWIF_NUM_CEQS(hwif);

	ceq_msix_entries = &hwdev->msix_entries[num_aeqs];
	func_to_io->hwdev = hwdev;
	func_to_io->sq_depth = sq_depth;
	func_to_io->rq_depth = rq_depth;
	func_to_io->global_qpn = base_qpn;

	err = hinic_io_init(func_to_io, hwif, nic_cap->max_qps, num_ceqs,
			    ceq_msix_entries);
	if (err) {
		dev_err(&pdev->dev, "Failed to init IO channel\n");
		return err;
	}

	num_qps = nic_cap->num_qps;
	sq_msix_entries = &hwdev->msix_entries[num_aeqs + num_ceqs];
	rq_msix_entries = &hwdev->msix_entries[num_aeqs + num_ceqs + num_qps];

	err = hinic_io_create_qps(func_to_io, base_qpn, num_qps,
				  sq_msix_entries, rq_msix_entries);
	if (err) {
		dev_err(&pdev->dev, "Failed to create QPs\n");
		goto err_create_qps;
	}

	err = wait_for_db_state(hwdev);
	if (err) {
		dev_warn(&pdev->dev, "db - disabled, try again\n");
		hinic_db_state_set(hwif, HINIC_DB_ENABLE);
	}

	err = set_hw_ioctxt(hwdev, sq_depth, rq_depth);
	if (err) {
		dev_err(&pdev->dev, "Failed to set HW IO ctxt\n");
		goto err_hw_ioctxt;
	}

	return 0;

err_hw_ioctxt:
	hinic_io_destroy_qps(func_to_io, num_qps);

err_create_qps:
	hinic_io_free(func_to_io);
	return err;
}

/**
 * hinic_hwdev_ifdown - Closing the HW for passing IO
 * @hwdev: the NIC HW device
 *
 **/
void hinic_hwdev_ifdown(struct hinic_hwdev *hwdev)
{
	struct hinic_func_to_io *func_to_io = &hwdev->func_to_io;
	struct hinic_cap *nic_cap = &hwdev->nic_cap;

	clear_io_resources(hwdev);

	hinic_io_destroy_qps(func_to_io, nic_cap->num_qps);
	hinic_io_free(func_to_io);
}

/**
 * hinic_hwdev_cb_register - register callback handler for MGMT events
 * @hwdev: the NIC HW device
 * @cmd: the mgmt event
 * @handle: private data for the handler
 * @handler: event handler
 **/
void hinic_hwdev_cb_register(struct hinic_hwdev *hwdev,
			     enum hinic_mgmt_msg_cmd cmd, void *handle,
			     void (*handler)(void *handle, void *buf_in,
					     u16 in_size, void *buf_out,
					     u16 *out_size))
{
	struct hinic_pfhwdev *pfhwdev;
	struct hinic_nic_cb *nic_cb;
	u8 cmd_cb;

	pfhwdev = container_of(hwdev, struct hinic_pfhwdev, hwdev);

	cmd_cb = cmd - HINIC_MGMT_MSG_CMD_BASE;
	nic_cb = &pfhwdev->nic_cb[cmd_cb];

	nic_cb->handler = handler;
	nic_cb->handle = handle;
	nic_cb->cb_state = HINIC_CB_ENABLED;
}

/**
 * hinic_hwdev_cb_unregister - unregister callback handler for MGMT events
 * @hwdev: the NIC HW device
 * @cmd: the mgmt event
 **/
void hinic_hwdev_cb_unregister(struct hinic_hwdev *hwdev,
			       enum hinic_mgmt_msg_cmd cmd)
{
	struct hinic_hwif *hwif = hwdev->hwif;
	struct hinic_pfhwdev *pfhwdev;
	struct hinic_nic_cb *nic_cb;
	u8 cmd_cb;

	if (!HINIC_IS_PF(hwif) && !HINIC_IS_PPF(hwif))
		return;

	pfhwdev = container_of(hwdev, struct hinic_pfhwdev, hwdev);

	cmd_cb = cmd - HINIC_MGMT_MSG_CMD_BASE;
	nic_cb = &pfhwdev->nic_cb[cmd_cb];

	nic_cb->cb_state &= ~HINIC_CB_ENABLED;

	while (nic_cb->cb_state & HINIC_CB_RUNNING)
		schedule();

	nic_cb->handler = NULL;
}

/**
 * nic_mgmt_msg_handler - nic mgmt event handler
 * @handle: private data for the handler
 * @cmd: message command
 * @buf_in: input buffer
 * @in_size: input size
 * @buf_out: output buffer
 * @out_size: returned output size
 **/
static void nic_mgmt_msg_handler(void *handle, u8 cmd, void *buf_in,
				 u16 in_size, void *buf_out, u16 *out_size)
{
	struct hinic_pfhwdev *pfhwdev = handle;
	enum hinic_cb_state cb_state;
	struct hinic_nic_cb *nic_cb;
	struct hinic_hwdev *hwdev;
	struct hinic_hwif *hwif;
	struct pci_dev *pdev;
	u8 cmd_cb;

	hwdev = &pfhwdev->hwdev;
	hwif = hwdev->hwif;
	pdev = hwif->pdev;

	if ((cmd < HINIC_MGMT_MSG_CMD_BASE) ||
	    (cmd >= HINIC_MGMT_MSG_CMD_MAX)) {
		dev_err(&pdev->dev, "unknown L2NIC event, cmd = %d\n", cmd);
		return;
	}

	cmd_cb = cmd - HINIC_MGMT_MSG_CMD_BASE;

	nic_cb = &pfhwdev->nic_cb[cmd_cb];

	cb_state = cmpxchg(&nic_cb->cb_state,
			   HINIC_CB_ENABLED,
			   HINIC_CB_ENABLED | HINIC_CB_RUNNING);

	if ((cb_state == HINIC_CB_ENABLED) && (nic_cb->handler))
		nic_cb->handler(nic_cb->handle, buf_in,
				in_size, buf_out, out_size);
	else
		dev_err(&pdev->dev, "Unhandled NIC Event %d\n", cmd);

	nic_cb->cb_state &= ~HINIC_CB_RUNNING;
}

static void hinic_comm_recv_mgmt_self_cmd_reg(struct hinic_pfhwdev *pfhwdev,
					      u8 cmd,
					      comm_mgmt_self_msg_proc proc)
{
	u8 cmd_idx;

	cmd_idx = pfhwdev->proc.cmd_num;
	if (cmd_idx >= HINIC_COMM_SELF_CMD_MAX) {
		dev_err(&pfhwdev->hwdev.hwif->pdev->dev,
			"Register recv mgmt process failed, cmd: 0x%x\n", cmd);
		return;
	}

	pfhwdev->proc.info[cmd_idx].cmd = cmd;
	pfhwdev->proc.info[cmd_idx].proc = proc;
	pfhwdev->proc.cmd_num++;
}

static void hinic_comm_recv_mgmt_self_cmd_unreg(struct hinic_pfhwdev *pfhwdev,
						u8 cmd)
{
	u8 cmd_idx;

	cmd_idx = pfhwdev->proc.cmd_num;
	if (cmd_idx >= HINIC_COMM_SELF_CMD_MAX) {
		dev_err(&pfhwdev->hwdev.hwif->pdev->dev, "Unregister recv mgmt process failed, cmd: 0x%x\n",
			cmd);
		return;
	}

	for (cmd_idx = 0; cmd_idx < HINIC_COMM_SELF_CMD_MAX; cmd_idx++) {
		if (cmd == pfhwdev->proc.info[cmd_idx].cmd) {
			pfhwdev->proc.info[cmd_idx].cmd = 0;
			pfhwdev->proc.info[cmd_idx].proc = NULL;
			pfhwdev->proc.cmd_num--;
		}
	}
}

static void comm_mgmt_msg_handler(void *handle, u8 cmd, void *buf_in,
				  u16 in_size, void *buf_out, u16 *out_size)
{
	struct hinic_pfhwdev *pfhwdev = handle;
	u8 cmd_idx;

	for (cmd_idx = 0; cmd_idx < pfhwdev->proc.cmd_num; cmd_idx++) {
		if (cmd == pfhwdev->proc.info[cmd_idx].cmd) {
			if (!pfhwdev->proc.info[cmd_idx].proc) {
				dev_warn(&pfhwdev->hwdev.hwif->pdev->dev,
					 "PF recv mgmt comm msg handle null, cmd: 0x%x\n",
					 cmd);
			} else {
				pfhwdev->proc.info[cmd_idx].proc
					(&pfhwdev->hwdev, buf_in, in_size,
					 buf_out, out_size);
			}

			return;
		}
	}

	dev_warn(&pfhwdev->hwdev.hwif->pdev->dev, "Received unknown mgmt cpu event: 0x%x\n",
		 cmd);

	*out_size = 0;
}

/* pf fault report event */
static void pf_fault_event_handler(void *dev, void *buf_in, u16 in_size,
				   void *buf_out, u16 *out_size)
{
	struct hinic_cmd_fault_event *fault_event = buf_in;
	struct hinic_hwdev *hwdev = dev;

	if (in_size != sizeof(*fault_event)) {
		dev_err(&hwdev->hwif->pdev->dev, "Invalid fault event report, length: %d, should be %zu\n",
			in_size, sizeof(*fault_event));
		return;
	}

	if (!hwdev->devlink_dev || IS_ERR_OR_NULL(hwdev->devlink_dev->hw_fault_reporter))
		return;

	devlink_health_report(hwdev->devlink_dev->hw_fault_reporter,
			      "HW fatal error reported", &fault_event->event);
}

static void mgmt_watchdog_timeout_event_handler(void *dev,
						void *buf_in, u16 in_size,
						void *buf_out, u16 *out_size)
{
	struct hinic_mgmt_watchdog_info *watchdog_info = buf_in;
	struct hinic_hwdev *hwdev = dev;

	if (in_size != sizeof(*watchdog_info)) {
		dev_err(&hwdev->hwif->pdev->dev, "Invalid mgmt watchdog report, length: %d, should be %zu\n",
			in_size, sizeof(*watchdog_info));
		return;
	}

	if (!hwdev->devlink_dev || IS_ERR_OR_NULL(hwdev->devlink_dev->fw_fault_reporter))
		return;

	devlink_health_report(hwdev->devlink_dev->fw_fault_reporter,
			      "FW fatal error reported", watchdog_info);
}

/**
 * init_pfhwdev - Initialize the extended components of PF
 * @pfhwdev: the HW device for PF
 *
 * Return 0 - success, negative - failure
 **/
static int init_pfhwdev(struct hinic_pfhwdev *pfhwdev)
{
	struct hinic_hwdev *hwdev = &pfhwdev->hwdev;
	struct hinic_hwif *hwif = hwdev->hwif;
	struct pci_dev *pdev = hwif->pdev;
	int err;

	err = hinic_pf_to_mgmt_init(&pfhwdev->pf_to_mgmt, hwif);
	if (err) {
		dev_err(&pdev->dev, "Failed to initialize PF to MGMT channel\n");
		return err;
	}

	err = hinic_devlink_register(hwdev->devlink_dev, &pdev->dev);
	if (err) {
		dev_err(&hwif->pdev->dev, "Failed to register devlink\n");
		hinic_pf_to_mgmt_free(&pfhwdev->pf_to_mgmt);
		return err;
	}

	err = hinic_func_to_func_init(hwdev);
	if (err) {
		dev_err(&hwif->pdev->dev, "Failed to init mailbox\n");
		hinic_devlink_unregister(hwdev->devlink_dev);
		hinic_pf_to_mgmt_free(&pfhwdev->pf_to_mgmt);
		return err;
	}

	if (!HINIC_IS_VF(hwif)) {
		hinic_register_mgmt_msg_cb(&pfhwdev->pf_to_mgmt,
					   HINIC_MOD_L2NIC, pfhwdev,
					   nic_mgmt_msg_handler);
		hinic_register_mgmt_msg_cb(&pfhwdev->pf_to_mgmt, HINIC_MOD_COMM,
					   pfhwdev, comm_mgmt_msg_handler);
		hinic_comm_recv_mgmt_self_cmd_reg(pfhwdev,
						  HINIC_COMM_CMD_FAULT_REPORT,
						  pf_fault_event_handler);
		hinic_comm_recv_mgmt_self_cmd_reg
			(pfhwdev, HINIC_COMM_CMD_WATCHDOG_INFO,
			 mgmt_watchdog_timeout_event_handler);
	} else {
		hinic_register_vf_mbox_cb(hwdev, HINIC_MOD_L2NIC,
					  nic_mgmt_msg_handler);
	}

	hinic_set_pf_action(hwif, HINIC_PF_MGMT_ACTIVE);

	return 0;
}

/**
 * free_pfhwdev - Free the extended components of PF
 * @pfhwdev: the HW device for PF
 **/
static void free_pfhwdev(struct hinic_pfhwdev *pfhwdev)
{
	struct hinic_hwdev *hwdev = &pfhwdev->hwdev;

	hinic_set_pf_action(hwdev->hwif, HINIC_PF_MGMT_INIT);

	if (!HINIC_IS_VF(hwdev->hwif)) {
		hinic_comm_recv_mgmt_self_cmd_unreg(pfhwdev,
						    HINIC_COMM_CMD_WATCHDOG_INFO);
		hinic_comm_recv_mgmt_self_cmd_unreg(pfhwdev,
						    HINIC_COMM_CMD_FAULT_REPORT);
		hinic_unregister_mgmt_msg_cb(&pfhwdev->pf_to_mgmt,
					     HINIC_MOD_COMM);
		hinic_unregister_mgmt_msg_cb(&pfhwdev->pf_to_mgmt,
					     HINIC_MOD_L2NIC);
	} else {
		hinic_unregister_vf_mbox_cb(hwdev, HINIC_MOD_L2NIC);
	}

	hinic_func_to_func_free(hwdev);

	hinic_devlink_unregister(hwdev->devlink_dev);

	hinic_pf_to_mgmt_free(&pfhwdev->pf_to_mgmt);
}

static int hinic_l2nic_reset(struct hinic_hwdev *hwdev)
{
	struct hinic_cmd_l2nic_reset l2nic_reset = {0};
	u16 out_size = sizeof(l2nic_reset);
	struct hinic_pfhwdev *pfhwdev;
	int err;

	pfhwdev = container_of(hwdev, struct hinic_pfhwdev, hwdev);

	l2nic_reset.func_id = HINIC_HWIF_FUNC_IDX(hwdev->hwif);
	/* 0 represents standard l2nic reset flow */
	l2nic_reset.reset_flag = 0;

	err = hinic_msg_to_mgmt(&pfhwdev->pf_to_mgmt, HINIC_MOD_COMM,
				HINIC_COMM_CMD_L2NIC_RESET, &l2nic_reset,
				sizeof(l2nic_reset), &l2nic_reset,
				&out_size, HINIC_MGMT_MSG_SYNC);
	if (err || !out_size || l2nic_reset.status) {
		dev_err(&hwdev->hwif->pdev->dev, "Failed to reset L2NIC resources, err: %d, status: 0x%x, out_size: 0x%x\n",
			err, l2nic_reset.status, out_size);
		return -EIO;
	}

	return 0;
}

int hinic_get_interrupt_cfg(struct hinic_hwdev *hwdev,
			    struct hinic_msix_config *interrupt_info)
{
	u16 out_size = sizeof(*interrupt_info);
	struct hinic_pfhwdev *pfhwdev;
	int err;

	if (!hwdev || !interrupt_info)
		return -EINVAL;

	pfhwdev = container_of(hwdev, struct hinic_pfhwdev, hwdev);

	interrupt_info->func_id = HINIC_HWIF_FUNC_IDX(hwdev->hwif);

	err = hinic_msg_to_mgmt(&pfhwdev->pf_to_mgmt, HINIC_MOD_COMM,
				HINIC_COMM_CMD_MSI_CTRL_REG_RD_BY_UP,
				interrupt_info, sizeof(*interrupt_info),
				interrupt_info, &out_size, HINIC_MGMT_MSG_SYNC);
	if (err || !out_size || interrupt_info->status) {
		dev_err(&hwdev->hwif->pdev->dev, "Failed to get interrupt config, err: %d, status: 0x%x, out size: 0x%x\n",
			err, interrupt_info->status, out_size);
		return -EIO;
	}

	return 0;
}

int hinic_set_interrupt_cfg(struct hinic_hwdev *hwdev,
			    struct hinic_msix_config *interrupt_info)
{
	u16 out_size = sizeof(*interrupt_info);
	struct hinic_msix_config temp_info;
	struct hinic_pfhwdev *pfhwdev;
	int err;

	if (!hwdev)
		return -EINVAL;

	pfhwdev = container_of(hwdev, struct hinic_pfhwdev, hwdev);

	interrupt_info->func_id = HINIC_HWIF_FUNC_IDX(hwdev->hwif);

	err = hinic_get_interrupt_cfg(hwdev, &temp_info);
	if (err)
		return -EINVAL;

	interrupt_info->lli_credit_cnt = temp_info.lli_timer_cnt;
	interrupt_info->lli_timer_cnt = temp_info.lli_timer_cnt;

	err = hinic_msg_to_mgmt(&pfhwdev->pf_to_mgmt, HINIC_MOD_COMM,
				HINIC_COMM_CMD_MSI_CTRL_REG_WR_BY_UP,
				interrupt_info, sizeof(*interrupt_info),
				interrupt_info, &out_size, HINIC_MGMT_MSG_SYNC);
	if (err || !out_size || interrupt_info->status) {
		dev_err(&hwdev->hwif->pdev->dev, "Failed to get interrupt config, err: %d, status: 0x%x, out size: 0x%x\n",
			err, interrupt_info->status, out_size);
		return -EIO;
	}

	return 0;
}

/**
 * hinic_init_hwdev - Initialize the NIC HW
 * @pdev: the NIC pci device
 * @devlink: the poniter of hinic devlink
 *
 * Return initialized NIC HW device
 *
 * Initialize the NIC HW device and return a pointer to it
 **/
struct hinic_hwdev *hinic_init_hwdev(struct pci_dev *pdev, struct devlink *devlink)
{
	struct hinic_pfhwdev *pfhwdev;
	struct hinic_hwdev *hwdev;
	struct hinic_hwif *hwif;
	int err, num_aeqs;

	hwif = devm_kzalloc(&pdev->dev, sizeof(*hwif), GFP_KERNEL);
	if (!hwif)
		return ERR_PTR(-ENOMEM);

	err = hinic_init_hwif(hwif, pdev);
	if (err) {
		dev_err(&pdev->dev, "Failed to init HW interface\n");
		return ERR_PTR(err);
	}

	pfhwdev = devm_kzalloc(&pdev->dev, sizeof(*pfhwdev), GFP_KERNEL);
	if (!pfhwdev) {
		err = -ENOMEM;
		goto err_pfhwdev_alloc;
	}

	hwdev = &pfhwdev->hwdev;
	hwdev->hwif = hwif;
	hwdev->devlink_dev = devlink_priv(devlink);
	hwdev->devlink_dev->hwdev = hwdev;

	err = init_msix(hwdev);
	if (err) {
		dev_err(&pdev->dev, "Failed to init msix\n");
		goto err_init_msix;
	}

	err = wait_for_outbound_state(hwdev);
	if (err) {
		dev_warn(&pdev->dev, "outbound - disabled, try again\n");
		hinic_outbound_state_set(hwif, HINIC_OUTBOUND_ENABLE);
	}

	num_aeqs = HINIC_HWIF_NUM_AEQS(hwif);

	err = hinic_aeqs_init(&hwdev->aeqs, hwif, num_aeqs,
			      HINIC_DEFAULT_AEQ_LEN, HINIC_EQ_PAGE_SIZE,
			      hwdev->msix_entries);
	if (err) {
		dev_err(&pdev->dev, "Failed to init async event queues\n");
		goto err_aeqs_init;
	}

	err = init_pfhwdev(pfhwdev);
	if (err) {
		dev_err(&pdev->dev, "Failed to init PF HW device\n");
		goto err_init_pfhwdev;
	}

	err = hinic_l2nic_reset(hwdev);
	if (err)
		goto err_l2nic_reset;

	err = get_dev_cap(hwdev);
	if (err) {
		dev_err(&pdev->dev, "Failed to get device capabilities\n");
		goto err_dev_cap;
	}

	mutex_init(&hwdev->func_to_io.nic_cfg.cfg_mutex);

	err = hinic_vf_func_init(hwdev);
	if (err) {
		dev_err(&pdev->dev, "Failed to init nic mbox\n");
		goto err_vf_func_init;
	}

	err = init_fw_ctxt(hwdev);
	if (err) {
		dev_err(&pdev->dev, "Failed to init function table\n");
		goto err_init_fw_ctxt;
	}

	err = set_resources_state(hwdev, HINIC_RES_ACTIVE);
	if (err) {
		dev_err(&pdev->dev, "Failed to set resources state\n");
		goto err_resources_state;
	}

	return hwdev;

err_resources_state:
err_init_fw_ctxt:
	hinic_vf_func_free(hwdev);
err_vf_func_init:
err_l2nic_reset:
err_dev_cap:
	free_pfhwdev(pfhwdev);

err_init_pfhwdev:
	hinic_aeqs_free(&hwdev->aeqs);

err_aeqs_init:
	disable_msix(hwdev);

err_init_msix:
err_pfhwdev_alloc:
	hinic_free_hwif(hwif);
	if (err > 0)
		err = -EIO;
	return ERR_PTR(err);
}

/**
 * hinic_free_hwdev - Free the NIC HW device
 * @hwdev: the NIC HW device
 **/
void hinic_free_hwdev(struct hinic_hwdev *hwdev)
{
	struct hinic_pfhwdev *pfhwdev = container_of(hwdev,
						     struct hinic_pfhwdev,
						     hwdev);

	set_resources_state(hwdev, HINIC_RES_CLEAN);

	hinic_vf_func_free(hwdev);

	free_pfhwdev(pfhwdev);

	hinic_aeqs_free(&hwdev->aeqs);

	disable_msix(hwdev);

	hinic_free_hwif(hwdev->hwif);
}

int hinic_hwdev_max_num_qps(struct hinic_hwdev *hwdev)
{
	struct hinic_cap *nic_cap = &hwdev->nic_cap;

	return nic_cap->max_qps;
}

/**
 * hinic_hwdev_num_qps - return the number QPs available for use
 * @hwdev: the NIC HW device
 *
 * Return number QPs available for use
 **/
int hinic_hwdev_num_qps(struct hinic_hwdev *hwdev)
{
	struct hinic_cap *nic_cap = &hwdev->nic_cap;

	return nic_cap->num_qps;
}

/**
 * hinic_hwdev_get_sq - get SQ
 * @hwdev: the NIC HW device
 * @i: the position of the SQ
 *
 * Return: the SQ in the i position
 **/
struct hinic_sq *hinic_hwdev_get_sq(struct hinic_hwdev *hwdev, int i)
{
	struct hinic_func_to_io *func_to_io = &hwdev->func_to_io;
	struct hinic_qp *qp = &func_to_io->qps[i];

	if (i >= hinic_hwdev_num_qps(hwdev))
		return NULL;

	return &qp->sq;
}

/**
 * hinic_hwdev_get_sq - get RQ
 * @hwdev: the NIC HW device
 * @i: the position of the RQ
 *
 * Return: the RQ in the i position
 **/
struct hinic_rq *hinic_hwdev_get_rq(struct hinic_hwdev *hwdev, int i)
{
	struct hinic_func_to_io *func_to_io = &hwdev->func_to_io;
	struct hinic_qp *qp = &func_to_io->qps[i];

	if (i >= hinic_hwdev_num_qps(hwdev))
		return NULL;

	return &qp->rq;
}

/**
 * hinic_hwdev_msix_cnt_set - clear message attribute counters for msix entry
 * @hwdev: the NIC HW device
 * @msix_index: msix_index
 *
 * Return 0 - Success, negative - Failure
 **/
int hinic_hwdev_msix_cnt_set(struct hinic_hwdev *hwdev, u16 msix_index)
{
	return hinic_msix_attr_cnt_clear(hwdev->hwif, msix_index);
}

/**
 * hinic_hwdev_msix_set - set message attribute for msix entry
 * @hwdev: the NIC HW device
 * @msix_index: msix_index
 * @pending_limit: the maximum pending interrupt events (unit 8)
 * @coalesc_timer: coalesc period for interrupt (unit 8 us)
 * @lli_timer_cfg: replenishing period for low latency credit (unit 8 us)
 * @lli_credit_limit: maximum credits for low latency msix messages (unit 8)
 * @resend_timer: maximum wait for resending msix (unit coalesc period)
 *
 * Return 0 - Success, negative - Failure
 **/
int hinic_hwdev_msix_set(struct hinic_hwdev *hwdev, u16 msix_index,
			 u8 pending_limit, u8 coalesc_timer,
			 u8 lli_timer_cfg, u8 lli_credit_limit,
			 u8 resend_timer)
{
	return hinic_msix_attr_set(hwdev->hwif, msix_index,
				   pending_limit, coalesc_timer,
				   lli_timer_cfg, lli_credit_limit,
				   resend_timer);
}

/**
 * hinic_hwdev_hw_ci_addr_set - set cons idx addr and attributes in HW for sq
 * @hwdev: the NIC HW device
 * @sq: send queue
 * @pending_limit: the maximum pending update ci events (unit 8)
 * @coalesc_timer: coalesc period for update ci (unit 8 us)
 *
 * Return 0 - Success, negative - Failure
 **/
int hinic_hwdev_hw_ci_addr_set(struct hinic_hwdev *hwdev, struct hinic_sq *sq,
			       u8 pending_limit, u8 coalesc_timer)
{
	struct hinic_qp *qp = container_of(sq, struct hinic_qp, sq);
	struct hinic_hwif *hwif = hwdev->hwif;
	struct hinic_pfhwdev *pfhwdev;
	struct hinic_cmd_hw_ci hw_ci;

	hw_ci.dma_attr_off  = 0;
	hw_ci.pending_limit = pending_limit;
	hw_ci.coalesc_timer = coalesc_timer;

	hw_ci.msix_en = 1;
	hw_ci.msix_entry_idx = sq->msix_entry;

	hw_ci.func_idx = HINIC_HWIF_FUNC_IDX(hwif);

	hw_ci.sq_id = qp->q_id;

	hw_ci.ci_addr = ADDR_IN_4BYTES(sq->hw_ci_dma_addr);

	pfhwdev = container_of(hwdev, struct hinic_pfhwdev, hwdev);
	return hinic_msg_to_mgmt(&pfhwdev->pf_to_mgmt,
				 HINIC_MOD_COMM,
				 HINIC_COMM_CMD_SQ_HI_CI_SET,
				 &hw_ci, sizeof(hw_ci), NULL,
				 NULL, HINIC_MGMT_MSG_SYNC);
}

/**
 * hinic_hwdev_set_msix_state- set msix state
 * @hwdev: the NIC HW device
 * @msix_index: IRQ corresponding index number
 * @flag: msix state
 *
 **/
void hinic_hwdev_set_msix_state(struct hinic_hwdev *hwdev, u16 msix_index,
				enum hinic_msix_state flag)
{
	hinic_set_msix_state(hwdev->hwif, msix_index, flag);
}

int hinic_get_board_info(struct hinic_hwdev *hwdev,
			 struct hinic_comm_board_info *board_info)
{
	u16 out_size = sizeof(*board_info);
	struct hinic_pfhwdev *pfhwdev;
	int err;

	if (!hwdev || !board_info)
		return -EINVAL;

	pfhwdev = container_of(hwdev, struct hinic_pfhwdev, hwdev);

	err = hinic_msg_to_mgmt(&pfhwdev->pf_to_mgmt, HINIC_MOD_COMM,
				HINIC_COMM_CMD_GET_BOARD_INFO,
				board_info, sizeof(*board_info),
				board_info, &out_size, HINIC_MGMT_MSG_SYNC);
	if (err || board_info->status || !out_size) {
		dev_err(&hwdev->hwif->pdev->dev,
			"Failed to get board info, err: %d, status: 0x%x, out size: 0x%x\n",
			err, board_info->status, out_size);
		return -EIO;
	}

	return 0;
}
