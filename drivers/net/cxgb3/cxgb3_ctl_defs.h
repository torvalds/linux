/*
 * Copyright (c) 2003-2007 Chelsio, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#ifndef _CXGB3_OFFLOAD_CTL_DEFS_H
#define _CXGB3_OFFLOAD_CTL_DEFS_H

enum {
	GET_MAX_OUTSTANDING_WR 	= 0,
	GET_TX_MAX_CHUNK	= 1,
	GET_TID_RANGE		= 2,
	GET_STID_RANGE		= 3,
	GET_RTBL_RANGE		= 4,
	GET_L2T_CAPACITY	= 5,
	GET_MTUS		= 6,
	GET_WR_LEN		= 7,
	GET_IFF_FROM_MAC	= 8,
	GET_DDP_PARAMS		= 9,
	GET_PORTS		= 10,

	ULP_ISCSI_GET_PARAMS	= 11,
	ULP_ISCSI_SET_PARAMS	= 12,

	RDMA_GET_PARAMS		= 13,
	RDMA_CQ_OP		= 14,
	RDMA_CQ_SETUP		= 15,
	RDMA_CQ_DISABLE		= 16,
	RDMA_CTRL_QP_SETUP	= 17,
	RDMA_GET_MEM		= 18,

	GET_RX_PAGE_INFO	= 50,
};

/*
 * Structure used to describe a TID range.  Valid TIDs are [base, base+num).
 */
struct tid_range {
	unsigned int base;	/* first TID */
	unsigned int num;	/* number of TIDs in range */
};

/*
 * Structure used to request the size and contents of the MTU table.
 */
struct mtutab {
	unsigned int size;	/* # of entries in the MTU table */
	const unsigned short *mtus;	/* the MTU table values */
};

struct net_device;

/*
 * Structure used to request the adapter net_device owning a given MAC address.
 */
struct iff_mac {
	struct net_device *dev;	/* the net_device */
	const unsigned char *mac_addr;	/* MAC address to lookup */
	u16 vlan_tag;
};

struct pci_dev;

/*
 * Structure used to request the TCP DDP parameters.
 */
struct ddp_params {
	unsigned int llimit;	/* TDDP region start address */
	unsigned int ulimit;	/* TDDP region end address */
	unsigned int tag_mask;	/* TDDP tag mask */
	struct pci_dev *pdev;
};

struct adap_ports {
	unsigned int nports;	/* number of ports on this adapter */
	struct net_device *lldevs[2];
};

/*
 * Structure used to return information to the iscsi layer.
 */
struct ulp_iscsi_info {
	unsigned int offset;
	unsigned int llimit;
	unsigned int ulimit;
	unsigned int tagmask;
	unsigned int pgsz3;
	unsigned int pgsz2;
	unsigned int pgsz1;
	unsigned int pgsz0;
	unsigned int max_rxsz;
	unsigned int max_txsz;
	struct pci_dev *pdev;
};

/*
 * Structure used to return information to the RDMA layer.
 */
struct rdma_info {
	unsigned int tpt_base;	/* TPT base address */
	unsigned int tpt_top;	/* TPT last entry address */
	unsigned int pbl_base;	/* PBL base address */
	unsigned int pbl_top;	/* PBL last entry address */
	unsigned int rqt_base;	/* RQT base address */
	unsigned int rqt_top;	/* RQT last entry address */
	unsigned int udbell_len;	/* user doorbell region length */
	unsigned long udbell_physbase;	/* user doorbell physical start addr */
	void __iomem *kdb_addr;	/* kernel doorbell register address */
	struct pci_dev *pdev;	/* associated PCI device */
};

/*
 * Structure used to request an operation on an RDMA completion queue.
 */
struct rdma_cq_op {
	unsigned int id;
	unsigned int op;
	unsigned int credits;
};

/*
 * Structure used to setup RDMA completion queues.
 */
struct rdma_cq_setup {
	unsigned int id;
	unsigned long long base_addr;
	unsigned int size;
	unsigned int credits;
	unsigned int credit_thres;
	unsigned int ovfl_mode;
};

/*
 * Structure used to setup the RDMA control egress context.
 */
struct rdma_ctrlqp_setup {
	unsigned long long base_addr;
	unsigned int size;
};

/*
 * Offload TX/RX page information.
 */
struct ofld_page_info {
	unsigned int page_size;  /* Page size, should be a power of 2 */
	unsigned int num;        /* Number of pages */
};
#endif				/* _CXGB3_OFFLOAD_CTL_DEFS_H */
