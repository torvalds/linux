/*
 * Huawei HiNIC PCI Express Linux driver
 * Copyright(c) 2017 Huawei Technologies Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
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

enum hw_ioctxt_set_cmdq_depth {
	HW_IOCTXT_SET_CMDQ_DEPTH_DEFAULT,
};

/* HW struct */
struct hinic_dev_cap {
	u8      status;
	u8      version;
	u8      rsvd0[6];

	u8      rsvd1[5];
	u8      intr_type;
	u8      rsvd2[66];
	u16     max_sqs;
	u16     max_rqs;
	u8      rsvd3[208];
};

/**
 * get_capability - convert device capabilities to NIC capabilities
 * @hwdev: the HW device to set and convert device capabilities for
 * @dev_cap: device capabilities from FW
 *
 * Return 0 - Success, negative - Failure
 **/
static int get_capability(struct hinic_hwdev *hwdev,
			  struct hinic_dev_cap *dev_cap)
{
	struct hinic_cap *nic_cap = &hwdev->nic_cap;
	int num_aeqs, num_ceqs, num_irqs;

	if (!HINIC_IS_PF(hwdev->hwif) && !HINIC_IS_PPF(hwdev->hwif))
		return -EINVAL;

	if (dev_cap->intr_type != INTR_MSIX_TYPE)
		return -EFAULT;

	num_aeqs = HINIC_HWIF_NUM_AEQS(hwdev->hwif);
	num_ceqs = HINIC_HWIF_NUM_CEQS(hwdev->hwif);
	num_irqs = HINIC_HWIF_NUM_IRQS(hwdev->hwif);

	/* Each QP has its own (SQ + RQ) interrupts */
	nic_cap->num_qps = (num_irqs - (num_aeqs + num_ceqs)) / 2;

	if (nic_cap->num_qps > HINIC_Q_CTXT_MAX)
		nic_cap->num_qps = HINIC_Q_CTXT_MAX;

	/* num_qps must be power of 2 */
	nic_cap->num_qps = BIT(fls(nic_cap->num_qps) - 1);

	nic_cap->max_qps = dev_cap->max_sqs + 1;
	if (nic_cap->max_qps != (dev_cap->max_rqs + 1))
		return -EFAULT;

	if (nic_cap->num_qps > nic_cap->max_qps)
		nic_cap->num_qps = nic_cap->max_qps;

	return 0;
}

/**
 * get_cap_from_fw - get device capabilities from FW
 * @pfhwdev: the PF HW device to get capabilities for
 *
 * Return 0 - Success, negative - Failure
 **/
static int get_cap_from_fw(struct hinic_pfhwdev *pfhwdev)
{
	struct hinic_hwdev *hwdev = &pfhwdev->hwdev;
	struct hinic_hwif *hwif = hwdev->hwif;
	struct pci_dev *pdev = hwif->pdev;
	struct hinic_dev_cap dev_cap;
	u16 in_len, out_len;
	int err;

	in_len = 0;
	out_len = sizeof(dev_cap);

	err = hinic_msg_to_mgmt(&pfhwdev->pf_to_mgmt, HINIC_MOD_CFGM,
				HINIC_CFG_NIC_CAP, &dev_cap, in_len, &dev_cap,
				&out_len, HINIC_MGMT_MSG_SYNC);
	if (err) {
		dev_err(&pdev->dev, "Failed to get capability from FW\n");
		return err;
	}

	return get_capability(hwdev, &dev_cap);
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
		pfhwdev = container_of(hwdev, struct hinic_pfhwdev, hwdev);

		err = get_cap_from_fw(pfhwdev);
		if (err) {
			dev_err(&pdev->dev, "Failed to get capability from FW\n");
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
	struct hinic_hwif *hwif = hwdev->hwif;
	struct pci_dev *pdev = hwif->pdev;
	struct hinic_pfhwdev *pfhwdev;

	if (!HINIC_IS_PF(hwif) && !HINIC_IS_PPF(hwif)) {
		dev_err(&pdev->dev, "unsupported PCI Function type\n");
		return -EINVAL;
	}

	pfhwdev = container_of(hwdev, struct hinic_pfhwdev, hwdev);

	return hinic_msg_to_mgmt(&pfhwdev->pf_to_mgmt, HINIC_MOD_L2NIC, cmd,
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
	u16 out_size;
	int err;

	if (!HINIC_IS_PF(hwif) && !HINIC_IS_PPF(hwif)) {
		dev_err(&pdev->dev, "Unsupported PCI Function type\n");
		return -EINVAL;
	}

	fw_ctxt.func_idx = HINIC_HWIF_FUNC_IDX(hwif);
	fw_ctxt.rx_buf_sz = HINIC_RX_BUF_SZ;

	err = hinic_port_msg_cmd(hwdev, HINIC_PORT_CMD_FWCTXT_INIT,
				 &fw_ctxt, sizeof(fw_ctxt),
				 &fw_ctxt, &out_size);
	if (err || (out_size != sizeof(fw_ctxt)) || fw_ctxt.status) {
		dev_err(&pdev->dev, "Failed to init FW ctxt, ret = %d\n",
			fw_ctxt.status);
		return -EFAULT;
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
static int set_hw_ioctxt(struct hinic_hwdev *hwdev, unsigned int rq_depth,
			 unsigned int sq_depth)
{
	struct hinic_hwif *hwif = hwdev->hwif;
	struct hinic_cmd_hw_ioctxt hw_ioctxt;
	struct pci_dev *pdev = hwif->pdev;
	struct hinic_pfhwdev *pfhwdev;

	if (!HINIC_IS_PF(hwif) && !HINIC_IS_PPF(hwif)) {
		dev_err(&pdev->dev, "Unsupported PCI Function type\n");
		return -EINVAL;
	}

	hw_ioctxt.func_idx = HINIC_HWIF_FUNC_IDX(hwif);

	hw_ioctxt.set_cmdq_depth = HW_IOCTXT_SET_CMDQ_DEPTH_DEFAULT;
	hw_ioctxt.cmdq_depth = 0;

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

static int wait_for_io_stopped(struct hinic_hwdev *hwdev)
{
	struct hinic_cmd_io_status cmd_io_status;
	struct hinic_hwif *hwif = hwdev->hwif;
	struct pci_dev *pdev = hwif->pdev;
	struct hinic_pfhwdev *pfhwdev;
	unsigned long end;
	u16 out_size;
	int err;

	if (!HINIC_IS_PF(hwif) && !HINIC_IS_PPF(hwif)) {
		dev_err(&pdev->dev, "Unsupported PCI Function type\n");
		return -EINVAL;
	}

	pfhwdev = container_of(hwdev, struct hinic_pfhwdev, hwdev);

	cmd_io_status.func_idx = HINIC_HWIF_FUNC_IDX(hwif);

	end = jiffies + msecs_to_jiffies(IO_STATUS_TIMEOUT);
	do {
		err = hinic_msg_to_mgmt(&pfhwdev->pf_to_mgmt, HINIC_MOD_COMM,
					HINIC_COMM_CMD_IO_STATUS_GET,
					&cmd_io_status, sizeof(cmd_io_status),
					&cmd_io_status, &out_size,
					HINIC_MGMT_MSG_SYNC);
		if ((err) || (out_size != sizeof(cmd_io_status))) {
			dev_err(&pdev->dev, "Failed to get IO status, ret = %d\n",
				err);
			return err;
		}

		if (cmd_io_status.status == IO_STOPPED) {
			dev_info(&pdev->dev, "IO stopped\n");
			return 0;
		}

		msleep(20);
	} while (time_before(jiffies, end));

	dev_err(&pdev->dev, "Wait for IO stopped - Timeout\n");
	return -ETIMEDOUT;
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

	if (!HINIC_IS_PF(hwif) && !HINIC_IS_PPF(hwif)) {
		dev_err(&pdev->dev, "Unsupported PCI Function type\n");
		return -EINVAL;
	}

	err = wait_for_io_stopped(hwdev);
	if (err) {
		dev_err(&pdev->dev, "IO has not stopped yet\n");
		return err;
	}

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
	struct pci_dev *pdev = hwif->pdev;
	struct hinic_pfhwdev *pfhwdev;

	if (!HINIC_IS_PF(hwif) && !HINIC_IS_PPF(hwif)) {
		dev_err(&pdev->dev, "Unsupported PCI Function type\n");
		return -EINVAL;
	}

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
	struct pci_dev *pdev = hwif->pdev;
	u16 out_size;
	int err;

	cmd_base_qpn.func_idx = HINIC_HWIF_FUNC_IDX(hwif);

	err = hinic_port_msg_cmd(hwdev, HINIC_PORT_CMD_GET_GLOBAL_QPN,
				 &cmd_base_qpn, sizeof(cmd_base_qpn),
				 &cmd_base_qpn, &out_size);
	if (err || (out_size != sizeof(cmd_base_qpn)) || cmd_base_qpn.status) {
		dev_err(&pdev->dev, "Failed to get base qpn, status = %d\n",
			cmd_base_qpn.status);
		return -EFAULT;
	}

	*base_qpn = cmd_base_qpn.qpn;
	return 0;
}

/**
 * hinic_hwdev_ifup - Preparing the HW for passing IO
 * @hwdev: the NIC HW device
 *
 * Return 0 - Success, negative - Failure
 **/
int hinic_hwdev_ifup(struct hinic_hwdev *hwdev)
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

	err = set_hw_ioctxt(hwdev, HINIC_SQ_DEPTH, HINIC_RQ_DEPTH);
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
	struct hinic_hwif *hwif = hwdev->hwif;
	struct pci_dev *pdev = hwif->pdev;
	struct hinic_pfhwdev *pfhwdev;
	struct hinic_nic_cb *nic_cb;
	u8 cmd_cb;

	if (!HINIC_IS_PF(hwif) && !HINIC_IS_PPF(hwif)) {
		dev_err(&pdev->dev, "unsupported PCI Function type\n");
		return;
	}

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
	struct pci_dev *pdev = hwif->pdev;
	struct hinic_pfhwdev *pfhwdev;
	struct hinic_nic_cb *nic_cb;
	u8 cmd_cb;

	if (!HINIC_IS_PF(hwif) && !HINIC_IS_PPF(hwif)) {
		dev_err(&pdev->dev, "unsupported PCI Function type\n");
		return;
	}

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

	hinic_register_mgmt_msg_cb(&pfhwdev->pf_to_mgmt, HINIC_MOD_L2NIC,
				   pfhwdev, nic_mgmt_msg_handler);

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

	hinic_unregister_mgmt_msg_cb(&pfhwdev->pf_to_mgmt, HINIC_MOD_L2NIC);

	hinic_pf_to_mgmt_free(&pfhwdev->pf_to_mgmt);
}

/**
 * hinic_init_hwdev - Initialize the NIC HW
 * @pdev: the NIC pci device
 *
 * Return initialized NIC HW device
 *
 * Initialize the NIC HW device and return a pointer to it
 **/
struct hinic_hwdev *hinic_init_hwdev(struct pci_dev *pdev)
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

	if (!HINIC_IS_PF(hwif) && !HINIC_IS_PPF(hwif)) {
		dev_err(&pdev->dev, "Unsupported PCI Function type\n");
		err = -EFAULT;
		goto err_func_type;
	}

	pfhwdev = devm_kzalloc(&pdev->dev, sizeof(*pfhwdev), GFP_KERNEL);
	if (!pfhwdev) {
		err = -ENOMEM;
		goto err_pfhwdev_alloc;
	}

	hwdev = &pfhwdev->hwdev;
	hwdev->hwif = hwif;

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

	err = get_dev_cap(hwdev);
	if (err) {
		dev_err(&pdev->dev, "Failed to get device capabilities\n");
		goto err_dev_cap;
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
err_dev_cap:
	free_pfhwdev(pfhwdev);

err_init_pfhwdev:
	hinic_aeqs_free(&hwdev->aeqs);

err_aeqs_init:
	disable_msix(hwdev);

err_init_msix:
err_pfhwdev_alloc:
err_func_type:
	hinic_free_hwif(hwif);
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

	free_pfhwdev(pfhwdev);

	hinic_aeqs_free(&hwdev->aeqs);

	disable_msix(hwdev);

	hinic_free_hwif(hwdev->hwif);
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
 * @lli_timer: replenishing period for low latency credit (unit 8 us)
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
	struct pci_dev *pdev = hwif->pdev;
	struct hinic_pfhwdev *pfhwdev;
	struct hinic_cmd_hw_ci hw_ci;

	if (!HINIC_IS_PF(hwif) && !HINIC_IS_PPF(hwif)) {
		dev_err(&pdev->dev, "Unsupported PCI Function type\n");
		return -EINVAL;
	}

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
