/*
 * cxgb3i.h: Chelsio S3xx iSCSI driver.
 *
 * Copyright (c) 2008 Chelsio Communications, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 * Written by: Karen Xie (kxie@chelsio.com)
 */

#ifndef __CXGB3I_H__
#define __CXGB3I_H__

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/netdevice.h>
#include <linux/scatterlist.h>
#include <linux/skbuff.h>
#include <scsi/libiscsi_tcp.h>

/* from cxgb3 LLD */
#include "common.h"
#include "t3_cpl.h"
#include "t3cdev.h"
#include "cxgb3_ctl_defs.h"
#include "cxgb3_offload.h"
#include "firmware_exports.h"

#include "cxgb3i_offload.h"
#include "cxgb3i_ddp.h"

#define CXGB3I_SCSI_HOST_QDEPTH 1024
#define CXGB3I_MAX_TARGET	CXGB3I_MAX_CONN
#define CXGB3I_MAX_LUN		512
#define ISCSI_PDU_NONPAYLOAD_MAX \
	(sizeof(struct iscsi_hdr) + ISCSI_MAX_AHS_SIZE + 2*ISCSI_DIGEST_SIZE)

struct cxgb3i_adapter;
struct cxgb3i_hba;
struct cxgb3i_endpoint;

/**
 * struct cxgb3i_hba - cxgb3i iscsi structure (per port)
 *
 * @snic:	cxgb3i adapter containing this port
 * @ndev:	pointer to netdev structure
 * @shost:	pointer to scsi host structure
 */
struct cxgb3i_hba {
	struct cxgb3i_adapter *snic;
	struct net_device *ndev;
	struct Scsi_Host *shost;
};

/**
 * struct cxgb3i_adapter - cxgb3i adapter structure (per pci)
 *
 * @listhead:	list head to link elements
 * @lock:	lock for this structure
 * @tdev:	pointer to t3cdev used by cxgb3 driver
 * @pdev:	pointer to pci dev
 * @hba_cnt:	# of hbas (the same as # of ports)
 * @hba:	all the hbas on this adapter
 * @flags:	bit flag for adapter event/status
 * @tx_max_size: max. tx packet size supported
 * @rx_max_size: max. rx packet size supported
 * @tag_format: ddp tag format settings
 */
#define CXGB3I_ADAPTER_FLAG_RESET	0x1
struct cxgb3i_adapter {
	struct list_head list_head;
	spinlock_t lock;
	struct t3cdev *tdev;
	struct pci_dev *pdev;
	unsigned char hba_cnt;
	struct cxgb3i_hba *hba[MAX_NPORTS];

	unsigned int flags;
	unsigned int tx_max_size;
	unsigned int rx_max_size;

	struct cxgb3i_tag_format tag_format;
};

/**
 * struct cxgb3i_conn - cxgb3i iscsi connection
 *
 * @listhead:	list head to link elements
 * @cep:	pointer to iscsi_endpoint structure
 * @conn:	pointer to iscsi_conn structure
 * @hba:	pointer to the hba this conn. is going through
 * @task_idx_bits: # of bits needed for session->cmds_max
 */
struct cxgb3i_conn {
	struct list_head list_head;
	struct cxgb3i_endpoint *cep;
	struct iscsi_conn *conn;
	struct cxgb3i_hba *hba;
	unsigned int task_idx_bits;
};

/**
 * struct cxgb3i_endpoint - iscsi tcp endpoint
 *
 * @c3cn:	the h/w tcp connection representation
 * @hba:	pointer to the hba this conn. is going through
 * @cconn:	pointer to the associated cxgb3i iscsi connection
 */
struct cxgb3i_endpoint {
	struct s3_conn *c3cn;
	struct cxgb3i_hba *hba;
	struct cxgb3i_conn *cconn;
};

/**
 * struct cxgb3i_task_data - private iscsi task data
 *
 * @nr_frags:	# of coalesced page frags (from scsi sgl)
 * @frags:	coalesced page frags (from scsi sgl)
 * @skb:	tx pdu skb
 * @offset:	data offset for the next pdu
 * @count:	max. possible pdu payload
 * @sgoffset:	offset to the first sg entry for a given offset
 */
#define MAX_PDU_FRAGS	((ULP2_MAX_PDU_PAYLOAD + 512 - 1) / 512)
struct cxgb3i_task_data {
	unsigned short nr_frags;
	skb_frag_t frags[MAX_PDU_FRAGS];
	struct sk_buff *skb;
	unsigned int offset;
	unsigned int count;
	unsigned int sgoffset;
};

int cxgb3i_iscsi_init(void);
void cxgb3i_iscsi_cleanup(void);

struct cxgb3i_adapter *cxgb3i_adapter_find_by_tdev(struct t3cdev *);
void cxgb3i_adapter_open(struct t3cdev *);
void cxgb3i_adapter_close(struct t3cdev *);

struct cxgb3i_hba *cxgb3i_hba_find_by_netdev(struct net_device *);
struct cxgb3i_hba *cxgb3i_hba_host_add(struct cxgb3i_adapter *,
				       struct net_device *);
void cxgb3i_hba_host_remove(struct cxgb3i_hba *);

int cxgb3i_pdu_init(void);
void cxgb3i_pdu_cleanup(void);
void cxgb3i_conn_cleanup_task(struct iscsi_task *);
int cxgb3i_conn_alloc_pdu(struct iscsi_task *, u8);
int cxgb3i_conn_init_pdu(struct iscsi_task *, unsigned int, unsigned int);
int cxgb3i_conn_xmit_pdu(struct iscsi_task *);

void cxgb3i_release_itt(struct iscsi_task *task, itt_t hdr_itt);
int cxgb3i_reserve_itt(struct iscsi_task *task, itt_t *hdr_itt);

#endif
