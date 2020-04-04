/* SPDX-License-Identifier: GPL-2.0
 * Marvell OcteonTX CPT driver
 *
 * Copyright (C) 2019 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __OTX_CPTVF_H
#define __OTX_CPTVF_H

#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include "otx_cpt_common.h"
#include "otx_cptvf_reqmgr.h"

/* Flags to indicate the features supported */
#define OTX_CPT_FLAG_DEVICE_READY  BIT(1)
#define otx_cpt_device_ready(cpt)  ((cpt)->flags & OTX_CPT_FLAG_DEVICE_READY)
/* Default command queue length */
#define OTX_CPT_CMD_QLEN	(4*2046)
#define OTX_CPT_CMD_QCHUNK_SIZE	1023
#define OTX_CPT_NUM_QS_PER_VF	1

struct otx_cpt_cmd_chunk {
	u8 *head;
	dma_addr_t dma_addr;
	u32 size; /* Chunk size, max OTX_CPT_INST_CHUNK_MAX_SIZE */
	struct list_head nextchunk;
};

struct otx_cpt_cmd_queue {
	u32 idx;	/* Command queue host write idx */
	u32 num_chunks;	/* Number of command chunks */
	struct otx_cpt_cmd_chunk *qhead;/*
					 * Command queue head, instructions
					 * are inserted here
					 */
	struct otx_cpt_cmd_chunk *base;
	struct list_head chead;
};

struct otx_cpt_cmd_qinfo {
	u32 qchunksize; /* Command queue chunk size */
	struct otx_cpt_cmd_queue queue[OTX_CPT_NUM_QS_PER_VF];
};

struct otx_cpt_pending_qinfo {
	u32 num_queues;	/* Number of queues supported */
	struct otx_cpt_pending_queue queue[OTX_CPT_NUM_QS_PER_VF];
};

#define for_each_pending_queue(qinfo, q, i)	\
		for (i = 0, q = &qinfo->queue[i]; i < qinfo->num_queues; i++, \
		     q = &qinfo->queue[i])

struct otx_cptvf_wqe {
	struct tasklet_struct twork;
	struct otx_cptvf *cptvf;
};

struct otx_cptvf_wqe_info {
	struct otx_cptvf_wqe vq_wqe[OTX_CPT_NUM_QS_PER_VF];
};

struct otx_cptvf {
	u16 flags;	/* Flags to hold device status bits */
	u8 vfid;	/* Device Index 0...OTX_CPT_MAX_VF_NUM */
	u8 num_vfs;	/* Number of enabled VFs */
	u8 vftype;	/* VF type of SE_TYPE(2) or AE_TYPE(1) */
	u8 vfgrp;	/* VF group (0 - 8) */
	u8 node;	/* Operating node: Bits (46:44) in BAR0 address */
	u8 priority;	/*
			 * VF priority ring: 1-High proirity round
			 * robin ring;0-Low priority round robin ring;
			 */
	struct pci_dev *pdev;	/* Pci device handle */
	void __iomem *reg_base;	/* Register start address */
	void *wqe_info;		/* BH worker info */
	/* MSI-X */
	cpumask_var_t affinity_mask[OTX_CPT_VF_MSIX_VECTORS];
	/* Command and Pending queues */
	u32 qsize;
	u32 num_queues;
	struct otx_cpt_cmd_qinfo cqinfo; /* Command queue information */
	struct otx_cpt_pending_qinfo pqinfo; /* Pending queue information */
	/* VF-PF mailbox communication */
	bool pf_acked;
	bool pf_nacked;
};

int otx_cptvf_send_vf_up(struct otx_cptvf *cptvf);
int otx_cptvf_send_vf_down(struct otx_cptvf *cptvf);
int otx_cptvf_send_vf_to_grp_msg(struct otx_cptvf *cptvf, int group);
int otx_cptvf_send_vf_priority_msg(struct otx_cptvf *cptvf);
int otx_cptvf_send_vq_size_msg(struct otx_cptvf *cptvf);
int otx_cptvf_check_pf_ready(struct otx_cptvf *cptvf);
void otx_cptvf_handle_mbox_intr(struct otx_cptvf *cptvf);
void otx_cptvf_write_vq_doorbell(struct otx_cptvf *cptvf, u32 val);

#endif /* __OTX_CPTVF_H */
