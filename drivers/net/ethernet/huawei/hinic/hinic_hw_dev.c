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
#include <linux/err.h>

#include "hinic_hw_if.h"
#include "hinic_hw_eqs.h"
#include "hinic_hw_mgmt.h"
#include "hinic_hw_qp.h"
#include "hinic_hw_io.h"
#include "hinic_hw_dev.h"

#define MAX_IRQS(max_qps, num_aeqs, num_ceqs)   \
		 (2 * (max_qps) + (num_aeqs) + (num_ceqs))

enum intr_type {
	INTR_MSIX_TYPE,
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
	err = hinic_io_init(func_to_io, hwif, nic_cap->max_qps, 0, NULL);
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

	return 0;

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

	return hwdev;

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
