/* SPDX-License-Identifier: GPL-2.0-or-later */

/*
 * ibmvfc-nvme.h -- IBM Power Virtual Fibre Channel NVMeoF HBA driver
 *
 * Written By: Tyrel Datwyler <tyreld@linux.ibm.com>, IBM Corporation
 *
 * Copyright (C) IBM Corporation, 2026
 */

#ifndef _IBMVFC_NVME_H
#define _IBMVFC_NVME_H

#include <scsi/fc/fc_fs.h>
#include <scsi/fc/fc_els.h>
#include <linux/nvme-fc-driver.h>
#include <linux/nvme.h>
#include <linux/nvme-fc.h>

#include "ibmvfc.h"

#define IBMVFC_NVME		0
#define IBMVFC_NVME_HW_QUEUES	8
#define IBMVFC_MAX_NVME_QUEUES	16
#define IBMVFC_NVME_CHANNELS	8

#define IBMVFC_FC4_LS_TIMEOUT	15
#define IBMVFC_FC4_LS_CANCEL_TIMEOUT	45
#define IBMVFC_FC4_LS_PLUS_CANCEL_TIMEOUT	\
	(IBMVFC_FC4_LS_TIMEOUT + IBMVFC_FC4_LS_CANCEL_TIMEOUT)

extern unsigned int ibmvfc_debug;

struct ibmvfc_host;
struct ibmvfc_target;
struct ibmvfc_queue;

struct ibmvfc_nvme_qhandle {
	unsigned int qidx;
	u16 cpu_id;
	unsigned long index;
	struct ibmvfc_queue *queue;
};

int ibmvfc_nvme_register_remoteport(struct ibmvfc_target *tgt);
void ibmvfc_nvme_unregister_remoteport(struct ibmvfc_target *tgt);
int ibmvfc_nvme_register(struct ibmvfc_host *vhost);
void ibmvfc_nvme_unregister(struct ibmvfc_host *vhost);
#endif
