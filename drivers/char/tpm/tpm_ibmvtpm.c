/*
 * Copyright (C) 2012-2020 IBM Corporation
 *
 * Author: Ashley Lai <ashleydlai@gmail.com>
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

static const struct vio_device_id tpm_ibmvtpm_device_table[] = {
	{ "IBM,vtpm", "IBM,vtpm"},
	{ "", "" }
};
MODULE_DEVICE_TABLE(vio, tpm_ibmvtpm_device_table);

/**
 *
 * ibmvtpm_send_crq_word - Send a CRQ request
 * @vdev:	vio device struct
 * @w1:		pre-constructed first word of tpm crq (second word is reserved)
 *
 * Return:
 *	0 - Success
 *	Non-zero - Failure
 */
static int ibmvtpm_send_crq_word(struct vio_dev *vdev, u64 w1)
{
	return plpar_hcall_norets(H_SEND_CRQ, vdev->unit_address, w1, 0);
}

/**
 *
 * ibmvtpm_send_crq - Send a CRQ request
 *
 * @vdev:	vio device struct
 * @valid:	Valid field
 * @msg:	Type field
 * @len:	Length field
 * @data:	Data field
 *
 * The ibmvtpm crq is defined as follows:
 *
 * Byte  |   0   |   1   |   2   |   3   |   4   |   5   |   6   |   7
 * -----------------------------------------------------------------------
 * Word0 | Valid | Type  |     Length    |              Data
 * -----------------------------------------------------------------------
 * Word1 |                Reserved
 * -----------------------------------------------------------------------
 *
 * Which matches the following structure (on bigendian host):
 *
 * struct ibmvtpm_crq {
 *         u8 valid;
 *         u8 msg;
 *         __be16 len;
 *         __be32 data;
 *         __be64 reserved;
 * } __attribute__((packed, aligned(8)));
 *
 * However, the value is passed in a register so just compute the numeric value
 * to load into the register avoiding byteswap altogether. Endian only affects
 * memory loads and stores - registers are internally represented the same.
 *
 * Return:
 *	0 (H_SUCCESS) - Success
 *	Non-zero - Failure
 */
static int ibmvtpm_send_crq(struct vio_dev *vdev,
		u8 valid, u8 msg, u16 len, u32 data)
{
	u64 w1 = ((u64)valid << 56) | ((u64)msg << 48) | ((u64)len << 32) |
		(u64)data;
	return ibmvtpm_send_crq_word(vdev, w1);
}

/**
 * tpm_ibmvtpm_recv - Receive data after send
 *
 * @chip:	tpm chip struct
 * @buf:	buffer to read
 * @count:	size of buffer
 *
 * Return:
 *	Number of bytes read
 */
static int tpm_ibmvtpm_recv(struct tpm_chip *chip, u8 *buf, size_t count)
{
	struct ibmvtpm_dev *ibmvtpm = dev_get_drvdata(&chip->dev);
	u16 len;
	int sig;

	if (!ibmvtpm->rtce_buf) {
		dev_err(ibmvtpm->dev, "ibmvtpm device is not ready\n");
		return 0;
	}

	sig = wait_event_interruptible(ibmvtpm->wq, !ibmvtpm->tpm_processing_cmd);
	if (sig)
		return -EINTR;

	len = ibmvtpm->res_len;

	if (count < len) {
		dev_err(ibmvtpm->dev,
			"Invalid size in recv: count=%zd, crq_size=%d\n",
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
 * ibmvtpm_crq_send_init - Send a CRQ initialize message
 * @ibmvtpm:	vtpm device struct
 *
 * Return:
 *	0 on success.
 *	Non-zero on failure.
 */
static int ibmvtpm_crq_send_init(struct ibmvtpm_dev *ibmvtpm)
{
	int rc;

	rc = ibmvtpm_send_crq_word(ibmvtpm->vdev, INIT_CRQ_CMD);
	if (rc != H_SUCCESS)
		dev_err(ibmvtpm->dev,
			"%s failed rc=%d\n", __func__, rc);

	return rc;
}

/**
 * tpm_ibmvtpm_resume - Resume from suspend
 *
 * @dev:	device struct
 *
 * Return: Always 0.
 */
static int tpm_ibmvtpm_resume(struct device *dev)
{
	struct tpm_chip *chip = dev_get_drvdata(dev);
	struct ibmvtpm_dev *ibmvtpm = dev_get_drvdata(&chip->dev);
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

/**
 * tpm_ibmvtpm_send() - Send a TPM command
 * @chip:	tpm chip struct
 * @buf:	buffer contains data to send
 * @count:	size of buffer
 *
 * Return:
 *   0 on success,
 *   -errno on error
 */
static int tpm_ibmvtpm_send(struct tpm_chip *chip, u8 *buf, size_t count)
{
	struct ibmvtpm_dev *ibmvtpm = dev_get_drvdata(&chip->dev);
	bool retry = true;
	int rc, sig;

	if (!ibmvtpm->rtce_buf) {
		dev_err(ibmvtpm->dev, "ibmvtpm device is not ready\n");
		return 0;
	}

	if (count > ibmvtpm->rtce_size) {
		dev_err(ibmvtpm->dev,
			"Invalid size in send: count=%zd, rtce_size=%d\n",
			count, ibmvtpm->rtce_size);
		return -EIO;
	}

	if (ibmvtpm->tpm_processing_cmd) {
		dev_info(ibmvtpm->dev,
		         "Need to wait for TPM to finish\n");
		/* wait for previous command to finish */
		sig = wait_event_interruptible(ibmvtpm->wq, !ibmvtpm->tpm_processing_cmd);
		if (sig)
			return -EINTR;
	}

	spin_lock(&ibmvtpm->rtce_lock);
	ibmvtpm->res_len = 0;
	memcpy((void *)ibmvtpm->rtce_buf, (void *)buf, count);

	/*
	 * set the processing flag before the Hcall, since we may get the
	 * result (interrupt) before even being able to check rc.
	 */
	ibmvtpm->tpm_processing_cmd = true;

again:
	rc = ibmvtpm_send_crq(ibmvtpm->vdev,
			IBMVTPM_VALID_CMD, VTPM_TPM_COMMAND,
			count, ibmvtpm->rtce_dma_handle);
	if (rc != H_SUCCESS) {
		/*
		 * H_CLOSED can be returned after LPM resume.  Call
		 * tpm_ibmvtpm_resume() to re-enable the CRQ then retry
		 * ibmvtpm_send_crq() once before failing.
		 */
		if (rc == H_CLOSED && retry) {
			tpm_ibmvtpm_resume(ibmvtpm->dev);
			retry = false;
			goto again;
		}
		dev_err(ibmvtpm->dev, "tpm_ibmvtpm_send failed rc=%d\n", rc);
		ibmvtpm->tpm_processing_cmd = false;
	}

	spin_unlock(&ibmvtpm->rtce_lock);
	return 0;
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
 *
 * @ibmvtpm:	vtpm device struct
 *
 * Return:
 *	0 on success.
 *	Non-zero on failure.
 */
static int ibmvtpm_crq_get_rtce_size(struct ibmvtpm_dev *ibmvtpm)
{
	int rc;

	rc = ibmvtpm_send_crq(ibmvtpm->vdev,
			IBMVTPM_VALID_CMD, VTPM_GET_RTCE_BUFFER_SIZE, 0, 0);
	if (rc != H_SUCCESS)
		dev_err(ibmvtpm->dev,
			"ibmvtpm_crq_get_rtce_size failed rc=%d\n", rc);

	return rc;
}

/**
 * ibmvtpm_crq_get_version - Send a CRQ request to get vtpm version
 *			   - Note that this is vtpm version and not tpm version
 *
 * @ibmvtpm:	vtpm device struct
 *
 * Return:
 *	0 on success.
 *	Non-zero on failure.
 */
static int ibmvtpm_crq_get_version(struct ibmvtpm_dev *ibmvtpm)
{
	int rc;

	rc = ibmvtpm_send_crq(ibmvtpm->vdev,
			IBMVTPM_VALID_CMD, VTPM_GET_VERSION, 0, 0);
	if (rc != H_SUCCESS)
		dev_err(ibmvtpm->dev,
			"ibmvtpm_crq_get_version failed rc=%d\n", rc);

	return rc;
}

/**
 * ibmvtpm_crq_send_init_complete - Send a CRQ initialize complete message
 * @ibmvtpm:	vtpm device struct
 *
 * Return:
 *	0 on success.
 *	Non-zero on failure.
 */
static int ibmvtpm_crq_send_init_complete(struct ibmvtpm_dev *ibmvtpm)
{
	int rc;

	rc = ibmvtpm_send_crq_word(ibmvtpm->vdev, INIT_CRQ_COMP_CMD);
	if (rc != H_SUCCESS)
		dev_err(ibmvtpm->dev,
			"ibmvtpm_crq_send_init_complete failed rc=%d\n", rc);

	return rc;
}

/**
 * tpm_ibmvtpm_remove - ibm vtpm remove entry point
 * @vdev:	vio device struct
 *
 * Return: Always 0.
 */
static int tpm_ibmvtpm_remove(struct vio_dev *vdev)
{
	struct tpm_chip *chip = dev_get_drvdata(&vdev->dev);
	struct ibmvtpm_dev *ibmvtpm = dev_get_drvdata(&chip->dev);
	int rc = 0;

	tpm_chip_unregister(chip);

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

	kfree(ibmvtpm);
	/* For tpm_ibmvtpm_get_desired_dma */
	dev_set_drvdata(&vdev->dev, NULL);

	return 0;
}

/**
 * tpm_ibmvtpm_get_desired_dma - Get DMA size needed by this driver
 * @vdev:	vio device struct
 *
 * Return:
 *	Number of bytes the driver needs to DMA map.
 */
static unsigned long tpm_ibmvtpm_get_desired_dma(struct vio_dev *vdev)
{
	struct tpm_chip *chip = dev_get_drvdata(&vdev->dev);
	struct ibmvtpm_dev *ibmvtpm;

	/*
	 * ibmvtpm initializes at probe time, so the data we are
	 * asking for may not be set yet. Estimate that 4K required
	 * for TCE-mapped buffer in addition to CRQ.
	 */
	if (chip)
		ibmvtpm = dev_get_drvdata(&chip->dev);
	else
		return CRQ_RES_BUF_SIZE + PAGE_SIZE;

	return CRQ_RES_BUF_SIZE + ibmvtpm->rtce_size;
}

/**
 * tpm_ibmvtpm_suspend - Suspend
 * @dev:	device struct
 *
 * Return: Always 0.
 */
static int tpm_ibmvtpm_suspend(struct device *dev)
{
	struct tpm_chip *chip = dev_get_drvdata(dev);
	struct ibmvtpm_dev *ibmvtpm = dev_get_drvdata(&chip->dev);
	int rc = 0;

	rc = ibmvtpm_send_crq(ibmvtpm->vdev,
			IBMVTPM_VALID_CMD, VTPM_PREPARE_TO_SUSPEND, 0, 0);
	if (rc != H_SUCCESS)
		dev_err(ibmvtpm->dev,
			"tpm_ibmvtpm_suspend failed rc=%d\n", rc);

	return rc;
}

/**
 * ibmvtpm_reset_crq - Reset CRQ
 *
 * @ibmvtpm:	ibm vtpm struct
 *
 * Return:
 *	0 on success.
 *	Non-zero on failure.
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

static bool tpm_ibmvtpm_req_canceled(struct tpm_chip *chip, u8 status)
{
	return (status == 0);
}

static const struct tpm_class_ops tpm_ibmvtpm = {
	.recv = tpm_ibmvtpm_recv,
	.send = tpm_ibmvtpm_send,
	.cancel = tpm_ibmvtpm_cancel,
	.status = tpm_ibmvtpm_status,
	.req_complete_mask = 0,
	.req_complete_val = 0,
	.req_canceled = tpm_ibmvtpm_req_canceled,
};

static const struct dev_pm_ops tpm_ibmvtpm_pm_ops = {
	.suspend = tpm_ibmvtpm_suspend,
	.resume = tpm_ibmvtpm_resume,
};

/**
 * ibmvtpm_crq_get_next - Get next responded crq
 *
 * @ibmvtpm:	vtpm device struct
 *
 * Return: vtpm crq pointer or NULL.
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
 *
 * @crq:	crq to be processed
 * @ibmvtpm:	vtpm device struct
 *
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
	case IBMVTPM_VALID_CMD:
		switch (crq->msg) {
		case VTPM_GET_RTCE_BUFFER_SIZE_RES:
			if (be16_to_cpu(crq->len) <= 0) {
				dev_err(ibmvtpm->dev, "Invalid rtce size\n");
				return;
			}
			ibmvtpm->rtce_size = be16_to_cpu(crq->len);
			ibmvtpm->rtce_buf = kmalloc(ibmvtpm->rtce_size,
						    GFP_ATOMIC);
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
			ibmvtpm->vtpm_version = be32_to_cpu(crq->data);
			return;
		case VTPM_TPM_COMMAND_RES:
			/* len of the data in rtce buffer */
			ibmvtpm->res_len = be16_to_cpu(crq->len);
			ibmvtpm->tpm_processing_cmd = false;
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
 *
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
 *
 * @vio_dev:	vio device struct
 * @id:		vio device id struct
 *
 * Return:
 *	0 on success.
 *	Non-zero on failure.
 */
static int tpm_ibmvtpm_probe(struct vio_dev *vio_dev,
				   const struct vio_device_id *id)
{
	struct ibmvtpm_dev *ibmvtpm;
	struct device *dev = &vio_dev->dev;
	struct ibmvtpm_crq_queue *crq_q;
	struct tpm_chip *chip;
	int rc = -ENOMEM, rc1;

	chip = tpmm_chip_alloc(dev, &tpm_ibmvtpm);
	if (IS_ERR(chip))
		return PTR_ERR(chip);

	ibmvtpm = kzalloc(sizeof(struct ibmvtpm_dev), GFP_KERNEL);
	if (!ibmvtpm) {
		dev_err(dev, "kzalloc for ibmvtpm failed\n");
		goto cleanup;
	}

	ibmvtpm->dev = dev;
	ibmvtpm->vdev = vio_dev;

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

	dev_set_drvdata(&chip->dev, ibmvtpm);

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

	return tpm_chip_register(chip);
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
 * ibmvtpm_module_init - Initialize ibm vtpm module.
 *
 *
 * Return:
 *	0 on success.
 *	Non-zero on failure.
 */
static int __init ibmvtpm_module_init(void)
{
	return vio_register_driver(&ibmvtpm_driver);
}

/**
 * ibmvtpm_module_exit - Tear down ibm vtpm module.
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
