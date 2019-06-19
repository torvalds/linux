/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 IBM Corporation
 *
 * Author: Ashley Lai <ashleydlai@gmail.com>
 *
 * Maintained by: <tpmdd-devel@lists.sourceforge.net>
 *
 * Device driver for TCG/TCPA TPM (trusted platform module).
 * Specifications at www.trustedcomputinggroup.org
 */

#ifndef __TPM_IBMVTPM_H__
#define __TPM_IBMVTPM_H__

/* vTPM Message Format 1 */
struct ibmvtpm_crq {
	u8 valid;
	u8 msg;
	__be16 len;
	__be32 data;
	__be64 reserved;
} __attribute__((packed, aligned(8)));

struct ibmvtpm_crq_queue {
	struct ibmvtpm_crq *crq_addr;
	u32 index;
	u32 num_entry;
};

struct ibmvtpm_dev {
	struct device *dev;
	struct vio_dev *vdev;
	struct ibmvtpm_crq_queue crq_queue;
	dma_addr_t crq_dma_handle;
	u32 rtce_size;
	void __iomem *rtce_buf;
	dma_addr_t rtce_dma_handle;
	spinlock_t rtce_lock;
	wait_queue_head_t wq;
	u16 res_len;
	u32 vtpm_version;
	bool tpm_processing_cmd;
};

#define CRQ_RES_BUF_SIZE	PAGE_SIZE

/* Initialize CRQ */
#define INIT_CRQ_CMD		0xC001000000000000LL /* Init cmd */
#define INIT_CRQ_COMP_CMD	0xC002000000000000LL /* Init complete cmd */
#define INIT_CRQ_RES		0x01	/* Init respond */
#define INIT_CRQ_COMP_RES	0x02	/* Init complete respond */
#define VALID_INIT_CRQ		0xC0	/* Valid command for init crq */

/* vTPM CRQ response is the message type | 0x80 */
#define VTPM_MSG_RES		0x80
#define IBMVTPM_VALID_CMD	0x80

/* vTPM CRQ message types */
#define VTPM_GET_VERSION			0x01
#define VTPM_GET_VERSION_RES			(0x01 | VTPM_MSG_RES)

#define VTPM_TPM_COMMAND			0x02
#define VTPM_TPM_COMMAND_RES			(0x02 | VTPM_MSG_RES)

#define VTPM_GET_RTCE_BUFFER_SIZE		0x03
#define VTPM_GET_RTCE_BUFFER_SIZE_RES		(0x03 | VTPM_MSG_RES)

#define VTPM_PREPARE_TO_SUSPEND			0x04
#define VTPM_PREPARE_TO_SUSPEND_RES		(0x04 | VTPM_MSG_RES)

#endif
