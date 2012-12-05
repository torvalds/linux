/*
 * Copyright (C) 2012 IBM Corporation
 *
 * Author: Ashley Lai <adlai@us.ibm.com>
 *
 * Maintained by: <tpmdd-devel@lists.sourceforge.net>
 *
 * Device driver for TCG/TCPA TPM (trusted platform module).
 * Specifications at www.trustedcomputinggroup.org
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 *
 */

#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/slab.h>
#include <asm/vio.h>
#include <asm/irq.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <asm/prom.h>

#include "tpm.h"
#include "tpm_ibmvtpm.h"

static const char tpm_ibmvtpm_driver_name[] = "tpm_ibmvtpm";

static struct vio_device_id tpm_ibmvtpm_device_table[] = {
	{ "IBM,vtpm", "IBM,vtpm"},
	{ "", "" }
};
MODULE_DEVICE_TABLE(vio, tpm_ibmvtpm_device_table);

/**
 * ibmvtpm_send_crq - Send a CRQ request
 * @vdev:	vio device struct
 * @w1:		first word
 * @w2:		second word
 *
 * Return value:
 *	0 -Sucess
 *	Non-zero - Failure
 */
static int ibmvtpm_send_crq(struct vio_dev *vdev, u64 w1, u64 w2)
{
	return plpar_hcall_norets(H_SEND_CRQ, vdev->unit_address, w1, w2);
}

/**
 * ibmvtpm_get_data - Retrieve ibm vtpm data
 * @dev:	device struct
 *
 * Return value:
 *	vtpm device struct
 */
static struct ibmvtpm_dev *ibmvtpm_get_data(const struct device *dev)
{
	struct tpm_chip *chip = dev_get_drvdata(dev);
	if (chip)
		return (struct ibmvtpm_dev *)TPM_VPRIV(chip);
	return NULL;
}

/**
 * tpm_ibmvtpm_recv - Receive data after send
 * @chip:	tpm chip struct
 * @buf:	buffer to read
 * count:	size of buffer
 *
 * Return value:
 *	Number of bytes read
 */
static int tpm_ibmvtpm_recv(struct tpm_chip *chip, u8 *buf, size_t count)
{
	struct ibmvtpm_dev *ibmvtpm;
	u16 len;
	int sig;

	ibmvtpm = (struct ibmvtpm_dev *)TPM_VPRIV(chip);

	if (!ibmvtpm->rtce_buf) {
		dev_err(ibmvtpm->dev, "ibmvtpm device is not ready\n");
		return 0;
	}

	sig = wait_event_interruptible(ibmvtpm->wq, ibmvtpm->res_len != 0);
	if (sig)
		return -EINTR;

	len = ibmvtpm->res_len;

	if (count < len) {
		dev_err(ibmvtpm->dev,
			"Invalid size in recv: count=%ld, crq_size=%d\n",
			count, len);
		return -EIO;
	}

	spin_lock(&ibmvtpm->rtce_lock);
	memcpy((void *)buf, (void *)ibmvtpm->rtce_buf, len);
	memset(ibmvtpm->rtce_buf, 0, len);
	ibmvtpm->res_len = 0;
	spin_unlock(&ibmvtpm->rtce_lock);
	return len;
}

/**
 * tpm_ibmvtpm_send - Send tpm request
 * @chip:	tpm chip struct
 * @buf:	buffer contains data to send
 * count:	size of buffer
 *
 * Return value:
 *	Number of bytes sent
 */
static int tpm_ibmvtpm_send(struct tpm_chip *chip, u8 *buf, size_t count)
{
	struct ibmvtpm_dev *ibmvtpm;
	struct ibmvtpm_crq crq;
	u64 *word = (u64 *) &crq;
	int rc;

	ibmvtpm = (struct ibmvtpm_dev *)TPM_VPRIV(chip);

	if (!ibmvtpm->rtce_buf) {
		dev_err(ibmvtpm->dev, "ibmvtpm device is not ready\n");
		return 0;
	}

	if (count > ibmvtpm->rtce_size) {
		dev_err(ibmvtpm->dev,
			"Invalid size in send: count=%ld, rtce_size=%d\n",
			count, ibmvtpm->rtce_size);
		return -EIO;
	}

	spin_lock(&ibmvtpm->rtce_lock);
	memcpy((void *)ibmvtpm->rtce_buf, (void *)buf, count);
	crq.valid = (u8)IBMVTPM_VALID_CMD;
	crq.msg = (u8)VTPM_TPM_COMMAND;
	crq.len = (u16)count;
	crq.data = ibmvtpm->rtce_dma_handle;

	rc = ibmvtpm_send_crq(ibmvtpm->vdev, word[0], word[1]);
	if (rc != H_SUCCESS) {
		dev_err(ibmvtpm->dev, "tpm_ibmvtpm_send failed rc=%d\n", rc);
		rc = 0;
	} else
		rc = count;

	spin_unlock(&ibmvtpm->rtce_lock);
	return rc;
}

static void tpm_ibmvtpm_cancel(struct tpm_chip *chip)
{
	return;
}

static u8 tpm_ibmvtpm_status(struct tpm_chip *chip)
{
	return 0;
}

/**
 * ibmvtpm_crq_get_rtce_size - Send a CRQ request to get rtce size
 * @ibmvtpm:	vtpm device struct
 *
 * Return value:
 *	0 - Success
 *	Non-zero - Failure
 */
static int ibmvtpm_crq_get_rtce_size(struct ibmvtpm_dev *ibmvtpm)
{
	struct ibmvtpm_crq crq;
	u64 *buf = (u64 *) &crq;
	int rc;

	crq.valid = (u8)IBMVTPM_VALID_CMD;
	crq.msg = (u8)VTPM_GET_RTCE_BUFFER_SIZE;

	rc = ibmvtpm_send_crq(ibmvtpm->vdev, buf[0], buf[1]);
	if (rc != H_SUCCESS)
		dev_err(ibmvtpm->dev,
			"ibmvtpm_crq_get_rtce_size failed rc=%d\n", rc);

	return rc;
}

/**
 * ibmvtpm_crq_get_version - Send a CRQ request to get vtpm version
 *			   - Note that this is vtpm version and not tpm version
 * @ibmvtpm:	vtpm device struct
 *
 * Return value:
 *	0 - Success
 *	Non-zero - Failure
 */
static int ibmvtpm_crq_get_version(struct ibmvtpm_dev *ibmvtpm)
{
	struct ibmvtpm_crq crq;
	u64 *buf = (u64 *) &crq;
	int rc;

	crq.valid = (u8)IBMVTPM_VALID_CMD;
	crq.msg = (u8)VTPM_GET_VERSION;

	rc = ibmvtpm_send_crq(ibmvtpm->vdev, buf[0], buf[1]);
	if (rc != H_SUCCESS)
		dev_err(ibmvtpm->dev,
			"ibmvtpm_crq_get_version failed rc=%d\n", rc);

	return rc;
}

/**
 * ibmvtpm_crq_send_init_complete - Send a CRQ initialize complete message
 * @ibmvtpm:	vtpm device struct
 *
 * Return value:
 *	0 - Success
 *	Non-zero - Failure
 */
static int ibmvtpm_crq_send_init_complete(struct ibmvtpm_dev *ibmvtpm)
{
	int rc;

	rc = ibmvtpm_send_crq(ibmvtpm->vdev, INIT_CRQ_COMP_CMD, 0);
	if (rc != H_SUCCESS)
		dev_err(ibmvtpm->dev,
			"ibmvtpm_crq_send_init_complete failed rc=%d\n", rc);

	return rc;
}

/**
 * ibmvtpm_crq_send_init - Send a CRQ initialize message
 * @ibmvtpm:	vtpm device struct
 *
 * Return value:
 *	0 - Success
 *	Non-zero - Failure
 */
static int ibmvtpm_crq_send_init(struct ibmvtpm_dev *ibmvtpm)
{
	int rc;

	rc = ibmvtpm_send_crq(ibmvtpm->vdev, INIT_CRQ_CMD, 0);
	if (rc != H_SUCCESS)
		dev_err(ibmvtpm->dev,
			"ibmvtpm_crq_send_init failed rc=%d\n", rc);

	return rc;
}

/**
 * tpm_ibmvtpm_remove - ibm vtpm remove entry point
 * @vdev:	vio device struct
 *
 * Return value:
 *	0
 */
static int tpm_ibmvtpm_remove(struct vio_dev *vdev)
{
	struct ibmvtpm_dev *ibmvtpm = ibmvtpm_get_data(&vdev->dev);
	int rc = 0;

	free_irq(vdev->irq, ibmvtpm);

	do {
		if (rc)
			msleep(100);
		rc = plpar_hcall_norets(H_FREE_CRQ, vdev->unit_address);
	} while (rc == H_BUSY || H_IS_LONG_BUSY(rc));

	dma_unmap_single(ibmvtpm->dev, ibmvtpm->crq_dma_handle,
			 CRQ_RES_BUF_SIZE, DMA_BIDIRECTIONAL);
	free_page((unsigned long)ibmvtpm->crq_queue.crq_addr);

	if (ibmvtpm->rtce_buf) {
		dma_unmap_single(ibmvtpm->dev, ibmvtpm->rtce_dma_handle,
				 ibmvtpm->rtce_size, DMA_BIDIRECTIONAL);
		kfree(ibmvtpm->rtce_buf);
	}

	tpm_remove_hardware(ibmvtpm->dev);

	kfree(ibmvtpm);

	return 0;
}

/**
 * tpm_ibmvtpm_get_desired_dma - Get DMA size needed by this driver
 * @vdev:	vio device struct
 *
 * Return value:
 *	Number of bytes the driver needs to DMA map
 */
static unsigned long tpm_ibmvtpm_get_desired_dma(struct vio_dev *vdev)
{
	struct ibmvtpm_dev *ibmvtpm = ibmvtpm_get_data(&vdev->dev);
	return CRQ_RES_BUF_SIZE + ibmvtpm->rtce_size;
}

/**
 * tpm_ibmvtpm_suspend - Suspend
 * @dev:	device struct
 *
 * Return value:
 *	0
 */
static int tpm_ibmvtpm_suspend(struct device *dev)
{
	struct ibmvtpm_dev *ibmvtpm = ibmvtpm_get_data(dev);
	struct ibmvtpm_crq crq;
	u64 *buf = (u64 *) &crq;
	int rc = 0;

	crq.valid = (u8)IBMVTPM_VALID_CMD;
	crq.msg = (u8)VTPM_PREPARE_TO_SUSPEND;

	rc = ibmvtpm_send_crq(ibmvtpm->vdev, buf[0], buf[1]);
	if (rc != H_SUCCESS)
		dev_err(ibmvtpm->dev,
			"tpm_ibmvtpm_suspend failed rc=%d\n", rc);

	return rc;
}

/**
 * ibmvtpm_reset_crq - Reset CRQ
 * @ibmvtpm:	ibm vtpm struct
 *
 * Return value:
 *	0 - Success
 *	Non-zero - Failure
 */
static int ibmvtpm_reset_crq(struct ibmvtpm_dev *ibmvtpm)
{
	int rc = 0;

	do {
		if (rc)
			msleep(100);
		rc = plpar_hcall_norets(H_FREE_CRQ,
					ibmvtpm->vdev->unit_address);
	} while (rc == H_BUSY || H_IS_LONG_BUSY(rc));

	memset(ibmvtpm->crq_queue.crq_addr, 0, CRQ_RES_BUF_SIZE);
	ibmvtpm->crq_queue.index = 0;

	return plpar_hcall_norets(H_REG_CRQ, ibmvtpm->vdev->unit_address,
				  ibmvtpm->crq_dma_handle, CRQ_RES_BUF_SIZE);
}

/**
 * tpm_ibmvtpm_resume - Resume from suspend
 * @dev:	device struct
 *
 * Return value:
 *	0
 */
static int tpm_ibmvtpm_resume(struct device *dev)
{
	struct ibmvtpm_dev *ibmvtpm = ibmvtpm_get_data(dev);
	int rc = 0;

	do {
		if (rc)
			msleep(100);
		rc = plpar_hcall_norets(H_ENABLE_CRQ,
					ibmvtpm->vdev->unit_address);
	} while (rc == H_IN_PROGRESS || rc == H_BUSY || H_IS_LONG_BUSY(rc));

	if (rc) {
		dev_err(dev, "Error enabling ibmvtpm rc=%d\n", rc);
		return rc;
	}

	rc = vio_enable_interrupts(ibmvtpm->vdev);
	if (rc) {
		dev_err(dev, "Error vio_enable_interrupts rc=%d\n", rc);
		return rc;
	}

	rc = ibmvtpm_crq_send_init(ibmvtpm);
	if (rc)
		dev_err(dev, "Error send_init rc=%d\n", rc);

	return rc;
}

static const struct file_operations ibmvtpm_ops = {
	.owner = THIS_MODULE,
	.llseek = no_llseek,
	.open = tpm_open,
	.read = tpm_read,
	.write = tpm_write,
	.release = tpm_release,
};

static DEVICE_ATTR(pubek, S_IRUGO, tpm_show_pubek, NULL);
static DEVICE_ATTR(pcrs, S_IRUGO, tpm_show_pcrs, NULL);
static DEVICE_ATTR(enabled, S_IRUGO, tpm_show_enabled, NULL);
static DEVICE_ATTR(active, S_IRUGO, tpm_show_active, NULL);
static DEVICE_ATTR(owned, S_IRUGO, tpm_show_owned, NULL);
static DEVICE_ATTR(temp_deactivated, S_IRUGO, tpm_show_temp_deactivated,
		   NULL);
static DEVICE_ATTR(caps, S_IRUGO, tpm_show_caps_1_2, NULL);
static DEVICE_ATTR(cancel, S_IWUSR | S_IWGRP, NULL, tpm_store_cancel);
static DEVICE_ATTR(durations, S_IRUGO, tpm_show_durations, NULL);
static DEVICE_ATTR(timeouts, S_IRUGO, tpm_show_timeouts, NULL);

static struct attribute *ibmvtpm_attrs[] = {
	&dev_attr_pubek.attr,
	&dev_attr_pcrs.attr,
	&dev_attr_enabled.attr,
	&dev_attr_active.attr,
	&dev_attr_owned.attr,
	&dev_attr_temp_deactivated.attr,
	&dev_attr_caps.attr,
	&dev_attr_cancel.attr,
	&dev_attr_durations.attr,
	&dev_attr_timeouts.attr, NULL,
};

static struct attribute_group ibmvtpm_attr_grp = { .attrs = ibmvtpm_attrs };

static const struct tpm_vendor_specific tpm_ibmvtpm = {
	.recv = tpm_ibmvtpm_recv,
	.send = tpm_ibmvtpm_send,
	.cancel = tpm_ibmvtpm_cancel,
	.status = tpm_ibmvtpm_status,
	.req_complete_mask = 0,
	.req_complete_val = 0,
	.req_canceled = 0,
	.attr_group = &ibmvtpm_attr_grp,
	.miscdev = { .fops = &ibmvtpm_ops, },
};

static const struct dev_pm_ops tpm_ibmvtpm_pm_ops = {
	.suspend = tpm_ibmvtpm_suspend,
	.resume = tpm_ibmvtpm_resume,
};

/**
 * ibmvtpm_crq_get_next - Get next responded crq
 * @ibmvtpm	vtpm device struct
 *
 * Return value:
 *	vtpm crq pointer
 */
static struct ibmvtpm_crq *ibmvtpm_crq_get_next(struct ibmvtpm_dev *ibmvtpm)
{
	struct ibmvtpm_crq_queue *crq_q = &ibmvtpm->crq_queue;
	struct ibmvtpm_crq *crq = &crq_q->crq_addr[crq_q->index];

	if (crq->valid & VTPM_MSG_RES) {
		if (++crq_q->index == crq_q->num_entry)
			crq_q->index = 0;
		smp_rmb();
	} else
		crq = NULL;
	return crq;
}

/**
 * ibmvtpm_crq_process - Process responded crq
 * @crq		crq to be processed
 * @ibmvtpm	vtpm device struct
 *
 * Return value:
 *	Nothing
 */
static void ibmvtpm_crq_process(struct ibmvtpm_crq *crq,
				struct ibmvtpm_dev *ibmvtpm)
{
	int rc = 0;

	switch (crq->valid) {
	case VALID_INIT_CRQ:
		switch (crq->msg) {
		case INIT_CRQ_RES:
			dev_info(ibmvtpm->dev, "CRQ initialized\n");
			rc = ibmvtpm_crq_send_init_complete(ibmvtpm);
			if (rc)
				dev_err(ibmvtpm->dev, "Unable to send CRQ init complete rc=%d\n", rc);
			return;
		case INIT_CRQ_COMP_RES:
			dev_info(ibmvtpm->dev,
				 "CRQ initialization completed\n");
			return;
		default:
			dev_err(ibmvtpm->dev, "Unknown crq message type: %d\n", crq->msg);
			return;
		}
		return;
	case IBMVTPM_VALID_CMD:
		switch (crq->msg) {
		case VTPM_GET_RTCE_BUFFER_SIZE_RES:
			if (crq->len <= 0) {
				dev_err(ibmvtpm->dev, "Invalid rtce size\n");
				return;
			}
			ibmvtpm->rtce_size = crq->len;
			ibmvtpm->rtce_buf = kmalloc(ibmvtpm->rtce_size,
						    GFP_KERNEL);
			if (!ibmvtpm->rtce_buf) {
				dev_err(ibmvtpm->dev, "Failed to allocate memory for rtce buffer\n");
				return;
			}

			ibmvtpm->rtce_dma_handle = dma_map_single(ibmvtpm->dev,
				ibmvtpm->rtce_buf, ibmvtpm->rtce_size,
				DMA_BIDIRECTIONAL);

			if (dma_mapping_error(ibmvtpm->dev,
					      ibmvtpm->rtce_dma_handle)) {
				kfree(ibmvtpm->rtce_buf);
				ibmvtpm->rtce_buf = NULL;
				dev_err(ibmvtpm->dev, "Failed to dma map rtce buffer\n");
			}

			return;
		case VTPM_GET_VERSION_RES:
			ibmvtpm->vtpm_version = crq->data;
			return;
		case VTPM_TPM_COMMAND_RES:
			/* len of the data in rtce buffer */
			ibmvtpm->res_len = crq->len;
			wake_up_interruptible(&ibmvtpm->wq);
			return;
		default:
			return;
		}
	}
	return;
}

/**
 * ibmvtpm_interrupt -	Interrupt handler
 * @irq:		irq number to handle
 * @vtpm_instance:	vtpm that received interrupt
 *
 * Returns:
 *	IRQ_HANDLED
 **/
static irqreturn_t ibmvtpm_interrupt(int irq, void *vtpm_instance)
{
	struct ibmvtpm_dev *ibmvtpm = (struct ibmvtpm_dev *) vtpm_instance;
	struct ibmvtpm_crq *crq;

	/* while loop is needed for initial setup (get version and
	 * get rtce_size). There should be only one tpm request at any
	 * given time.
	 */
	while ((crq = ibmvtpm_crq_get_next(ibmvtpm)) != NULL) {
		ibmvtpm_crq_process(crq, ibmvtpm);
		crq->valid = 0;
		smp_wmb();
	}

	return IRQ_HANDLED;
}

/**
 * tpm_ibmvtpm_probe - ibm vtpm initialize entry point
 * @vio_dev:	vio device struct
 * @id:		vio device id struct
 *
 * Return value:
 *	0 - Success
 *	Non-zero - Failure
 */
static int tpm_ibmvtpm_probe(struct vio_dev *vio_dev,
				   const struct vio_device_id *id)
{
	struct ibmvtpm_dev *ibmvtpm;
	struct device *dev = &vio_dev->dev;
	struct ibmvtpm_crq_queue *crq_q;
	struct tpm_chip *chip;
	int rc = -ENOMEM, rc1;

	chip = tpm_register_hardware(dev, &tpm_ibmvtpm);
	if (!chip) {
		dev_err(dev, "tpm_register_hardware failed\n");
		return -ENODEV;
	}

	ibmvtpm = kzalloc(sizeof(struct ibmvtpm_dev), GFP_KERNEL);
	if (!ibmvtpm) {
		dev_err(dev, "kzalloc for ibmvtpm failed\n");
		goto cleanup;
	}

	crq_q = &ibmvtpm->crq_queue;
	crq_q->crq_addr = (struct ibmvtpm_crq *)get_zeroed_page(GFP_KERNEL);
	if (!crq_q->crq_addr) {
		dev_err(dev, "Unable to allocate memory for crq_addr\n");
		goto cleanup;
	}

	crq_q->num_entry = CRQ_RES_BUF_SIZE / sizeof(*crq_q->crq_addr);
	ibmvtpm->crq_dma_handle = dma_map_single(dev, crq_q->crq_addr,
						 CRQ_RES_BUF_SIZE,
						 DMA_BIDIRECTIONAL);

	if (dma_mapping_error(dev, ibmvtpm->crq_dma_handle)) {
		dev_err(dev, "dma mapping failed\n");
		goto cleanup;
	}

	rc = plpar_hcall_norets(H_REG_CRQ, vio_dev->unit_address,
				ibmvtpm->crq_dma_handle, CRQ_RES_BUF_SIZE);
	if (rc == H_RESOURCE)
		rc = ibmvtpm_reset_crq(ibmvtpm);

	if (rc) {
		dev_err(dev, "Unable to register CRQ rc=%d\n", rc);
		goto reg_crq_cleanup;
	}

	rc = request_irq(vio_dev->irq, ibmvtpm_interrupt, 0,
			 tpm_ibmvtpm_driver_name, ibmvtpm);
	if (rc) {
		dev_err(dev, "Error %d register irq 0x%x\n", rc, vio_dev->irq);
		goto init_irq_cleanup;
	}

	rc = vio_enable_interrupts(vio_dev);
	if (rc) {
		dev_err(dev, "Error %d enabling interrupts\n", rc);
		goto init_irq_cleanup;
	}

	init_waitqueue_head(&ibmvtpm->wq);

	crq_q->index = 0;

	ibmvtpm->dev = dev;
	ibmvtpm->vdev = vio_dev;
	TPM_VPRIV(chip) = (void *)ibmvtpm;

	spin_lock_init(&ibmvtpm->rtce_lock);

	rc = ibmvtpm_crq_send_init(ibmvtpm);
	if (rc)
		goto init_irq_cleanup;

	rc = ibmvtpm_crq_get_version(ibmvtpm);
	if (rc)
		goto init_irq_cleanup;

	rc = ibmvtpm_crq_get_rtce_size(ibmvtpm);
	if (rc)
		goto init_irq_cleanup;

	return rc;
init_irq_cleanup:
	do {
		rc1 = plpar_hcall_norets(H_FREE_CRQ, vio_dev->unit_address);
	} while (rc1 == H_BUSY || H_IS_LONG_BUSY(rc1));
reg_crq_cleanup:
	dma_unmap_single(dev, ibmvtpm->crq_dma_handle, CRQ_RES_BUF_SIZE,
			 DMA_BIDIRECTIONAL);
cleanup:
	if (ibmvtpm) {
		if (crq_q->crq_addr)
			free_page((unsigned long)crq_q->crq_addr);
		kfree(ibmvtpm);
	}

	tpm_remove_hardware(dev);

	return rc;
}

static struct vio_driver ibmvtpm_driver = {
	.id_table	 = tpm_ibmvtpm_device_table,
	.probe		 = tpm_ibmvtpm_probe,
	.remove		 = tpm_ibmvtpm_remove,
	.get_desired_dma = tpm_ibmvtpm_get_desired_dma,
	.name		 = tpm_ibmvtpm_driver_name,
	.pm		 = &tpm_ibmvtpm_pm_ops,
};

/**
 * ibmvtpm_module_init - Initialize ibm vtpm module
 *
 * Return value:
 *	0 -Success
 *	Non-zero - Failure
 */
static int __init ibmvtpm_module_init(void)
{
	return vio_register_driver(&ibmvtpm_driver);
}

/**
 * ibmvtpm_module_exit - Teardown ibm vtpm module
 *
 * Return value:
 *	Nothing
 */
static void __exit ibmvtpm_module_exit(void)
{
	vio_unregister_driver(&ibmvtpm_driver);
}

module_init(ibmvtpm_module_init);
module_exit(ibmvtpm_module_exit);

MODULE_AUTHOR("adlai@us.ibm.com");
MODULE_DESCRIPTION("IBM vTPM Driver");
MODULE_VERSION("1.0");
MODULE_LICENSE("GPL");
