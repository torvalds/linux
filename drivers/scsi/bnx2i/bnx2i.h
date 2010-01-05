/* bnx2i.h: Broadcom NetXtreme II iSCSI driver.
 *
 * Copyright (c) 2006 - 2009 Broadcom Corporation
 * Copyright (c) 2007, 2008 Red Hat, Inc.  All rights reserved.
 * Copyright (c) 2007, 2008 Mike Christie
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 * Written by: Anil Veerabhadrappa (anilgv@broadcom.com)
 */

#ifndef _BNX2I_H_
#define _BNX2I_H_

#include <linux/module.h>
#include <linux/moduleparam.h>

#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/in.h>
#include <linux/kfifo.h>
#include <linux/netdevice.h>
#include <linux/completion.h>

#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi.h>
#include <scsi/iscsi_proto.h>
#include <scsi/libiscsi.h>
#include <scsi/scsi_transport_iscsi.h>

#include "../../net/cnic_if.h"
#include "57xx_iscsi_hsi.h"
#include "57xx_iscsi_constants.h"

#define BNX2_ISCSI_DRIVER_NAME		"bnx2i"

#define BNX2I_MAX_ADAPTERS		8

#define ISCSI_MAX_CONNS_PER_HBA		128
#define ISCSI_MAX_SESS_PER_HBA		ISCSI_MAX_CONNS_PER_HBA
#define ISCSI_MAX_CMDS_PER_SESS		128

/* Total active commands across all connections supported by devices */
#define ISCSI_MAX_CMDS_PER_HBA_5708	(28 * (ISCSI_MAX_CMDS_PER_SESS - 1))
#define ISCSI_MAX_CMDS_PER_HBA_5709	(128 * (ISCSI_MAX_CMDS_PER_SESS - 1))
#define ISCSI_MAX_CMDS_PER_HBA_57710	(256 * (ISCSI_MAX_CMDS_PER_SESS - 1))

#define ISCSI_MAX_BDS_PER_CMD		32

#define MAX_PAGES_PER_CTRL_STRUCT_POOL	8
#define BNX2I_RESERVED_SLOW_PATH_CMD_SLOTS	4

/* 5706/08 hardware has limit on maximum buffer size per BD it can handle */
#define MAX_BD_LENGTH			65535
#define BD_SPLIT_SIZE			32768

/* min, max & default values for SQ/RQ/CQ size, configurable via' modparam */
#define BNX2I_SQ_WQES_MIN 		16
#define BNX2I_570X_SQ_WQES_MAX 		128
#define BNX2I_5770X_SQ_WQES_MAX 	512
#define BNX2I_570X_SQ_WQES_DEFAULT 	128
#define BNX2I_5770X_SQ_WQES_DEFAULT 	256

#define BNX2I_570X_CQ_WQES_MAX 		128
#define BNX2I_5770X_CQ_WQES_MAX 	512

#define BNX2I_RQ_WQES_MIN 		16
#define BNX2I_RQ_WQES_MAX 		32
#define BNX2I_RQ_WQES_DEFAULT 		16

/* CCELLs per conn */
#define BNX2I_CCELLS_MIN		16
#define BNX2I_CCELLS_MAX		96
#define BNX2I_CCELLS_DEFAULT		64

#define ITT_INVALID_SIGNATURE		0xFFFF

#define ISCSI_CMD_CLEANUP_TIMEOUT	100

#define BNX2I_CONN_CTX_BUF_SIZE		16384

#define BNX2I_SQ_WQE_SIZE		64
#define BNX2I_RQ_WQE_SIZE		256
#define BNX2I_CQE_SIZE			64

#define MB_KERNEL_CTX_SHIFT		8
#define MB_KERNEL_CTX_SIZE		(1 << MB_KERNEL_CTX_SHIFT)

#define CTX_SHIFT			7
#define GET_CID_NUM(cid_addr)		((cid_addr) >> CTX_SHIFT)

#define CTX_OFFSET 			0x10000
#define MAX_CID_CNT			0x4000

#define BNX2I_570X_PAGE_SIZE_DEFAULT	4096

/* 5709 context registers */
#define BNX2_MQ_CONFIG2			0x00003d00
#define BNX2_MQ_CONFIG2_CONT_SZ		(0x7L<<4)
#define BNX2_MQ_CONFIG2_FIRST_L4L5	(0x1fL<<8)

/* 57710's BAR2 is mapped to doorbell registers */
#define BNX2X_DOORBELL_PCI_BAR		2
#define BNX2X_MAX_CQS			8

#define CNIC_ARM_CQE			1
#define CNIC_DISARM_CQE			0

#define REG_RD(__hba, offset)				\
		readl(__hba->regview + offset)
#define REG_WR(__hba, offset, val)			\
		writel(val, __hba->regview + offset)


/**
 * struct generic_pdu_resc - login pdu resource structure
 *
 * @req_buf:            driver buffer used to stage payload associated with
 *                      the login request
 * @req_dma_addr:       dma address for iscsi login request payload buffer
 * @req_buf_size:       actual login request payload length
 * @req_wr_ptr:         pointer into login request buffer when next data is
 *                      to be written
 * @resp_hdr:           iscsi header where iscsi login response header is to
 *                      be recreated
 * @resp_buf:           buffer to stage login response payload
 * @resp_dma_addr:      login response payload buffer dma address
 * @resp_buf_size:      login response paylod length
 * @resp_wr_ptr:        pointer into login response buffer when next data is
 *                      to be written
 * @req_bd_tbl:         iscsi login request payload BD table
 * @req_bd_dma:         login request BD table dma address
 * @resp_bd_tbl:        iscsi login response payload BD table
 * @resp_bd_dma:        login request BD table dma address
 *
 * following structure defines buffer info for generic pdus such as iSCSI Login,
 *	Logout and NOP
 */
struct generic_pdu_resc {
	char *req_buf;
	dma_addr_t req_dma_addr;
	u32 req_buf_size;
	char *req_wr_ptr;
	struct iscsi_hdr resp_hdr;
	char *resp_buf;
	dma_addr_t resp_dma_addr;
	u32 resp_buf_size;
	char *resp_wr_ptr;
	char *req_bd_tbl;
	dma_addr_t req_bd_dma;
	char *resp_bd_tbl;
	dma_addr_t resp_bd_dma;
};


/**
 * struct bd_resc_page - tracks DMA'able memory allocated for BD tables
 *
 * @link:               list head to link elements
 * @max_ptrs:           maximun pointers that can be stored in this page
 * @num_valid:          number of pointer valid in this page
 * @page:               base addess for page pointer array
 *
 * structure to track DMA'able memory allocated for command BD tables
 */
struct bd_resc_page {
	struct list_head link;
	u32 max_ptrs;
	u32 num_valid;
	void *page[1];
};


/**
 * struct io_bdt - I/O buffer destricptor table
 *
 * @bd_tbl:             BD table's virtual address
 * @bd_tbl_dma:         BD table's dma address
 * @bd_valid:           num valid BD entries
 *
 * IO BD table
 */
struct io_bdt {
	struct iscsi_bd *bd_tbl;
	dma_addr_t bd_tbl_dma;
	u16 bd_valid;
};


/**
 * bnx2i_cmd - iscsi command structure
 *
 * @scsi_cmd:           SCSI-ML task pointer corresponding to this iscsi cmd
 * @sg:                 SG list
 * @io_tbl:             buffer descriptor (BD) table
 * @bd_tbl_dma:         buffer descriptor (BD) table's dma address
 */
struct bnx2i_cmd {
	struct iscsi_hdr hdr;
	struct bnx2i_conn *conn;
	struct scsi_cmnd *scsi_cmd;
	struct scatterlist *sg;
	struct io_bdt io_tbl;
	dma_addr_t bd_tbl_dma;
	struct bnx2i_cmd_request req;
};


/**
 * struct bnx2i_conn - iscsi connection structure
 *
 * @cls_conn:              pointer to iscsi cls conn
 * @hba:                   adapter structure pointer
 * @iscsi_conn_cid:        iscsi conn id
 * @fw_cid:                firmware iscsi context id
 * @ep:                    endpoint structure pointer
 * @gen_pdu:               login/nopout/logout pdu resources
 * @violation_notified:    bit mask used to track iscsi error/warning messages
 *                         already printed out
 *
 * iSCSI connection structure
 */
struct bnx2i_conn {
	struct iscsi_cls_conn *cls_conn;
	struct bnx2i_hba *hba;
	struct completion cmd_cleanup_cmpl;

	u32 iscsi_conn_cid;
#define BNX2I_CID_RESERVED	0x5AFF
	u32 fw_cid;

	struct timer_list poll_timer;
	/*
	 * Queue Pair (QP) related structure elements.
	 */
	struct bnx2i_endpoint *ep;

	/*
	 * Buffer for login negotiation process
	 */
	struct generic_pdu_resc gen_pdu;
	u64 violation_notified;
};



/**
 * struct iscsi_cid_queue - Per adapter iscsi cid queue
 *
 * @cid_que_base:           queue base memory
 * @cid_que:                queue memory pointer
 * @cid_q_prod_idx:         produce index
 * @cid_q_cons_idx:         consumer index
 * @cid_q_max_idx:          max index. used to detect wrap around condition
 * @cid_free_cnt:           queue size
 * @conn_cid_tbl:           iscsi cid to conn structure mapping table
 *
 * Per adapter iSCSI CID Queue
 */
struct iscsi_cid_queue {
	void *cid_que_base;
	u32 *cid_que;
	u32 cid_q_prod_idx;
	u32 cid_q_cons_idx;
	u32 cid_q_max_idx;
	u32 cid_free_cnt;
	struct bnx2i_conn **conn_cid_tbl;
};

/**
 * struct bnx2i_hba - bnx2i adapter structure
 *
 * @link:                  list head to link elements
 * @cnic:                  pointer to cnic device
 * @pcidev:                pointer to pci dev
 * @netdev:                pointer to netdev structure
 * @regview:               mapped PCI register space
 * @age:                   age, incremented by every recovery
 * @cnic_dev_type:         cnic device type, 5706/5708/5709/57710
 * @mail_queue_access:     mailbox queue access mode, applicable to 5709 only
 * @reg_with_cnic:         indicates whether the device is register with CNIC
 * @adapter_state:         adapter state, UP, GOING_DOWN, LINK_DOWN
 * @mtu_supported:         Ethernet MTU supported
 * @shost:                 scsi host pointer
 * @max_sqes:              SQ size
 * @max_rqes:              RQ size
 * @max_cqes:              CQ size
 * @num_ccell:             number of command cells per connection
 * @ofld_conns_active:     active connection list
 * @max_active_conns:      max offload connections supported by this device
 * @cid_que:               iscsi cid queue
 * @ep_rdwr_lock:          read / write lock to synchronize various ep lists
 * @ep_ofld_list:          connection list for pending offload completion
 * @ep_destroy_list:       connection list for pending offload completion
 * @mp_bd_tbl:             BD table to be used with middle path requests
 * @mp_bd_dma:             DMA address of 'mp_bd_tbl' memory buffer
 * @dummy_buffer:          Dummy buffer to be used with zero length scsicmd reqs
 * @dummy_buf_dma:         DMA address of 'dummy_buffer' memory buffer
 * @lock:              	   lock to synchonize access to hba structure
 * @pci_did:               PCI device ID
 * @pci_vid:               PCI vendor ID
 * @pci_sdid:              PCI subsystem device ID
 * @pci_svid:              PCI subsystem vendor ID
 * @pci_func:              PCI function number in system pci tree
 * @pci_devno:             PCI device number in system pci tree
 * @num_wqe_sent:          statistic counter, total wqe's sent
 * @num_cqe_rcvd:          statistic counter, total cqe's received
 * @num_intr_claimed:      statistic counter, total interrupts claimed
 * @link_changed_count:    statistic counter, num of link change notifications
 *                         received
 * @ipaddr_changed_count:  statistic counter, num times IP address changed while
 *                         at least one connection is offloaded
 * @num_sess_opened:       statistic counter, total num sessions opened
 * @num_conn_opened:       statistic counter, total num conns opened on this hba
 * @ctx_ccell_tasks:       captures number of ccells and tasks supported by
 *                         currently offloaded connection, used to decode
 *                         context memory
 *
 * Adapter Data Structure
 */
struct bnx2i_hba {
	struct list_head link;
	struct cnic_dev *cnic;
	struct pci_dev *pcidev;
	struct net_device *netdev;
	void __iomem *regview;

	u32 age;
	unsigned long cnic_dev_type;
		#define BNX2I_NX2_DEV_5706		0x0
		#define BNX2I_NX2_DEV_5708		0x1
		#define BNX2I_NX2_DEV_5709		0x2
		#define BNX2I_NX2_DEV_57710		0x3
	u32 mail_queue_access;
		#define BNX2I_MQ_KERNEL_MODE		0x0
		#define BNX2I_MQ_KERNEL_BYPASS_MODE	0x1
		#define BNX2I_MQ_BIN_MODE		0x2
	unsigned long  reg_with_cnic;
		#define BNX2I_CNIC_REGISTERED		1

	unsigned long  adapter_state;
		#define ADAPTER_STATE_UP		0
		#define ADAPTER_STATE_GOING_DOWN	1
		#define ADAPTER_STATE_LINK_DOWN		2
		#define ADAPTER_STATE_INIT_FAILED	31
	unsigned int mtu_supported;
		#define BNX2I_MAX_MTU_SUPPORTED		1500

	struct Scsi_Host *shost;

	u32 max_sqes;
	u32 max_rqes;
	u32 max_cqes;
	u32 num_ccell;

	int ofld_conns_active;

	int max_active_conns;
	struct iscsi_cid_queue cid_que;

	rwlock_t ep_rdwr_lock;
	struct list_head ep_ofld_list;
	struct list_head ep_destroy_list;

	/*
	 * BD table to be used with MP (Middle Path requests.
	 */
	char *mp_bd_tbl;
	dma_addr_t mp_bd_dma;
	char *dummy_buffer;
	dma_addr_t dummy_buf_dma;

	spinlock_t lock;	/* protects hba structure access */
	struct mutex net_dev_lock;/* sync net device access */

	/*
	 * PCI related info.
	 */
	u16 pci_did;
	u16 pci_vid;
	u16 pci_sdid;
	u16 pci_svid;
	u16 pci_func;
	u16 pci_devno;

	/*
	 * Following are a bunch of statistics useful during development
	 * and later stage for score boarding.
	 */
	u32 num_wqe_sent;
	u32 num_cqe_rcvd;
	u32 num_intr_claimed;
	u32 link_changed_count;
	u32 ipaddr_changed_count;
	u32 num_sess_opened;
	u32 num_conn_opened;
	unsigned int ctx_ccell_tasks;
};


/*******************************************************************************
 * 	QP [ SQ / RQ / CQ ] info.
 ******************************************************************************/

/*
 * SQ/RQ/CQ generic structure definition
 */
struct 	sqe {
	u8 sqe_byte[BNX2I_SQ_WQE_SIZE];
};

struct 	rqe {
	u8 rqe_byte[BNX2I_RQ_WQE_SIZE];
};

struct 	cqe {
	u8 cqe_byte[BNX2I_CQE_SIZE];
};


enum {
#if defined(__LITTLE_ENDIAN)
	CNIC_EVENT_COAL_INDEX	= 0x0,
	CNIC_SEND_DOORBELL	= 0x4,
	CNIC_EVENT_CQ_ARM	= 0x7,
	CNIC_RECV_DOORBELL	= 0x8
#elif defined(__BIG_ENDIAN)
	CNIC_EVENT_COAL_INDEX	= 0x2,
	CNIC_SEND_DOORBELL	= 0x6,
	CNIC_EVENT_CQ_ARM	= 0x4,
	CNIC_RECV_DOORBELL	= 0xa
#endif
};


/*
 * CQ DB
 */
struct bnx2x_iscsi_cq_pend_cmpl {
	/* CQ producer, updated by Ustorm */
	u16 ustrom_prod;
	/* CQ pending completion counter */
	u16 pend_cntr;
};


struct bnx2i_5771x_cq_db {
	struct bnx2x_iscsi_cq_pend_cmpl qp_pend_cmpl[BNX2X_MAX_CQS];
	/* CQ pending completion ITT array */
	u16 itt[BNX2X_MAX_CQS];
	/* Cstorm CQ sequence to notify array, updated by driver */;
	u16 sqn[BNX2X_MAX_CQS];
	u32 reserved[4] /* 16 byte allignment */;
};


struct bnx2i_5771x_sq_rq_db {
	u16 prod_idx;
	u8 reserved0[14]; /* Pad structure size to 16 bytes */
};


struct bnx2i_5771x_dbell_hdr {
	u8 header;
	/* 1 for rx doorbell, 0 for tx doorbell */
#define B577XX_DOORBELL_HDR_RX				(0x1<<0)
#define B577XX_DOORBELL_HDR_RX_SHIFT			0
	/* 0 for normal doorbell, 1 for advertise wnd doorbell */
#define B577XX_DOORBELL_HDR_DB_TYPE			(0x1<<1)
#define B577XX_DOORBELL_HDR_DB_TYPE_SHIFT		1
	/* rdma tx only: DPM transaction size specifier (64/128/256/512B) */
#define B577XX_DOORBELL_HDR_DPM_SIZE			(0x3<<2)
#define B577XX_DOORBELL_HDR_DPM_SIZE_SHIFT		2
	/* connection type */
#define B577XX_DOORBELL_HDR_CONN_TYPE			(0xF<<4)
#define B577XX_DOORBELL_HDR_CONN_TYPE_SHIFT		4
};

struct bnx2i_5771x_dbell {
	struct bnx2i_5771x_dbell_hdr dbell;
	u8 pad[3];

};

/**
 * struct qp_info - QP (share queue region) atrributes structure
 *
 * @ctx_base:           ioremapped pci register base to access doorbell register
 *                      pertaining to this offloaded connection
 * @sq_virt:            virtual address of send queue (SQ) region
 * @sq_phys:            DMA address of SQ memory region
 * @sq_mem_size:        SQ size
 * @sq_prod_qe:         SQ producer entry pointer
 * @sq_cons_qe:         SQ consumer entry pointer
 * @sq_first_qe:        virtaul address of first entry in SQ
 * @sq_last_qe:         virtaul address of last entry in SQ
 * @sq_prod_idx:        SQ producer index
 * @sq_cons_idx:        SQ consumer index
 * @sqe_left:           number sq entry left
 * @sq_pgtbl_virt:      page table describing buffer consituting SQ region
 * @sq_pgtbl_phys:      dma address of 'sq_pgtbl_virt'
 * @sq_pgtbl_size:      SQ page table size
 * @cq_virt:            virtual address of completion queue (CQ) region
 * @cq_phys:            DMA address of RQ memory region
 * @cq_mem_size:        CQ size
 * @cq_prod_qe:         CQ producer entry pointer
 * @cq_cons_qe:         CQ consumer entry pointer
 * @cq_first_qe:        virtaul address of first entry in CQ
 * @cq_last_qe:         virtaul address of last entry in CQ
 * @cq_prod_idx:        CQ producer index
 * @cq_cons_idx:        CQ consumer index
 * @cqe_left:           number cq entry left
 * @cqe_size:           size of each CQ entry
 * @cqe_exp_seq_sn:     next expected CQE sequence number
 * @cq_pgtbl_virt:      page table describing buffer consituting CQ region
 * @cq_pgtbl_phys:      dma address of 'cq_pgtbl_virt'
 * @cq_pgtbl_size:    	CQ page table size
 * @rq_virt:            virtual address of receive queue (RQ) region
 * @rq_phys:            DMA address of RQ memory region
 * @rq_mem_size:        RQ size
 * @rq_prod_qe:         RQ producer entry pointer
 * @rq_cons_qe:         RQ consumer entry pointer
 * @rq_first_qe:        virtaul address of first entry in RQ
 * @rq_last_qe:         virtaul address of last entry in RQ
 * @rq_prod_idx:        RQ producer index
 * @rq_cons_idx:        RQ consumer index
 * @rqe_left:           number rq entry left
 * @rq_pgtbl_virt:      page table describing buffer consituting RQ region
 * @rq_pgtbl_phys:      dma address of 'rq_pgtbl_virt'
 * @rq_pgtbl_size:      RQ page table size
 *
 * queue pair (QP) is a per connection shared data structure which is used
 *	to send work requests (SQ), receive completion notifications (CQ)
 *	and receive asynchoronous / scsi sense info (RQ). 'qp_info' structure
 *	below holds queue memory, consumer/producer indexes and page table
 *	information
 */
struct qp_info {
	void __iomem *ctx_base;
#define DPM_TRIGER_TYPE			0x40

#define BNX2I_570x_QUE_DB_SIZE		0
#define BNX2I_5771x_QUE_DB_SIZE		16
	struct sqe *sq_virt;
	dma_addr_t sq_phys;
	u32 sq_mem_size;

	struct sqe *sq_prod_qe;
	struct sqe *sq_cons_qe;
	struct sqe *sq_first_qe;
	struct sqe *sq_last_qe;
	u16 sq_prod_idx;
	u16 sq_cons_idx;
	u32 sqe_left;

	void *sq_pgtbl_virt;
	dma_addr_t sq_pgtbl_phys;
	u32 sq_pgtbl_size;	/* set to PAGE_SIZE for 5708 & 5709 */

	struct cqe *cq_virt;
	dma_addr_t cq_phys;
	u32 cq_mem_size;

	struct cqe *cq_prod_qe;
	struct cqe *cq_cons_qe;
	struct cqe *cq_first_qe;
	struct cqe *cq_last_qe;
	u16 cq_prod_idx;
	u16 cq_cons_idx;
	u32 cqe_left;
	u32 cqe_size;
	u32 cqe_exp_seq_sn;

	void *cq_pgtbl_virt;
	dma_addr_t cq_pgtbl_phys;
	u32 cq_pgtbl_size;	/* set to PAGE_SIZE for 5708 & 5709 */

	struct rqe *rq_virt;
	dma_addr_t rq_phys;
	u32 rq_mem_size;

	struct rqe *rq_prod_qe;
	struct rqe *rq_cons_qe;
	struct rqe *rq_first_qe;
	struct rqe *rq_last_qe;
	u16 rq_prod_idx;
	u16 rq_cons_idx;
	u32 rqe_left;

	void *rq_pgtbl_virt;
	dma_addr_t rq_pgtbl_phys;
	u32 rq_pgtbl_size;	/* set to PAGE_SIZE for 5708 & 5709 */
};



/*
 * CID handles
 */
struct ep_handles {
	u32 fw_cid;
	u32 drv_iscsi_cid;
	u16 pg_cid;
	u16 rsvd;
};


enum {
	EP_STATE_IDLE                   = 0x0,
	EP_STATE_PG_OFLD_START          = 0x1,
	EP_STATE_PG_OFLD_COMPL          = 0x2,
	EP_STATE_OFLD_START             = 0x4,
	EP_STATE_OFLD_COMPL             = 0x8,
	EP_STATE_CONNECT_START          = 0x10,
	EP_STATE_CONNECT_COMPL          = 0x20,
	EP_STATE_ULP_UPDATE_START       = 0x40,
	EP_STATE_ULP_UPDATE_COMPL       = 0x80,
	EP_STATE_DISCONN_START          = 0x100,
	EP_STATE_DISCONN_COMPL          = 0x200,
	EP_STATE_CLEANUP_START          = 0x400,
	EP_STATE_CLEANUP_CMPL           = 0x800,
	EP_STATE_TCP_FIN_RCVD           = 0x1000,
	EP_STATE_TCP_RST_RCVD           = 0x2000,
	EP_STATE_PG_OFLD_FAILED         = 0x1000000,
	EP_STATE_ULP_UPDATE_FAILED      = 0x2000000,
	EP_STATE_CLEANUP_FAILED         = 0x4000000,
	EP_STATE_OFLD_FAILED            = 0x8000000,
	EP_STATE_CONNECT_FAILED         = 0x10000000,
	EP_STATE_DISCONN_TIMEDOUT       = 0x20000000,
};

/**
 * struct bnx2i_endpoint - representation of tcp connection in NX2 world
 *
 * @link:               list head to link elements
 * @hba:                adapter to which this connection belongs
 * @conn:               iscsi connection this EP is linked to
 * @sess:               iscsi session this EP is linked to
 * @cm_sk:              cnic sock struct
 * @hba_age:            age to detect if 'iscsid' issues ep_disconnect()
 *                      after HBA reset is completed by bnx2i/cnic/bnx2
 *                      modules
 * @state:              tracks offload connection state machine
 * @teardown_mode:      indicates if conn teardown is abortive or orderly
 * @qp:                 QP information
 * @ids:                contains chip allocated *context id* & driver assigned
 *                      *iscsi cid*
 * @ofld_timer:         offload timer to detect timeout
 * @ofld_wait:          wait queue
 *
 * Endpoint Structure - equivalent of tcp socket structure
 */
struct bnx2i_endpoint {
	struct list_head link;
	struct bnx2i_hba *hba;
	struct bnx2i_conn *conn;
	struct cnic_sock *cm_sk;
	u32 hba_age;
	u32 state;
	unsigned long timestamp;
	int num_active_cmds;

	struct qp_info qp;
	struct ep_handles ids;
		#define ep_iscsi_cid	ids.drv_iscsi_cid
		#define ep_cid		ids.fw_cid
		#define ep_pg_cid	ids.pg_cid
	struct timer_list ofld_timer;
	wait_queue_head_t ofld_wait;
};



/* Global variables */
extern unsigned int error_mask1, error_mask2;
extern u64 iscsi_error_mask;
extern unsigned int en_tcp_dack;
extern unsigned int event_coal_div;
extern unsigned int event_coal_min;

extern struct scsi_transport_template *bnx2i_scsi_xport_template;
extern struct iscsi_transport bnx2i_iscsi_transport;
extern struct cnic_ulp_ops bnx2i_cnic_cb;

extern unsigned int sq_size;
extern unsigned int rq_size;

extern struct device_attribute *bnx2i_dev_attributes[];



/*
 * Function Prototypes
 */
extern void bnx2i_identify_device(struct bnx2i_hba *hba);
extern void bnx2i_register_device(struct bnx2i_hba *hba);

extern void bnx2i_ulp_init(struct cnic_dev *dev);
extern void bnx2i_ulp_exit(struct cnic_dev *dev);
extern void bnx2i_start(void *handle);
extern void bnx2i_stop(void *handle);
extern void bnx2i_reg_dev_all(void);
extern void bnx2i_unreg_dev_all(void);
extern struct bnx2i_hba *get_adapter_list_head(void);

struct bnx2i_conn *bnx2i_get_conn_from_id(struct bnx2i_hba *hba,
					  u16 iscsi_cid);

int bnx2i_alloc_ep_pool(void);
void bnx2i_release_ep_pool(void);
struct bnx2i_endpoint *bnx2i_ep_ofld_list_next(struct bnx2i_hba *hba);
struct bnx2i_endpoint *bnx2i_ep_destroy_list_next(struct bnx2i_hba *hba);

struct bnx2i_hba *bnx2i_find_hba_for_cnic(struct cnic_dev *cnic);

struct bnx2i_hba *bnx2i_alloc_hba(struct cnic_dev *cnic);
void bnx2i_free_hba(struct bnx2i_hba *hba);

void bnx2i_get_rq_buf(struct bnx2i_conn *conn, char *ptr, int len);
void bnx2i_put_rq_buf(struct bnx2i_conn *conn, int count);

void bnx2i_iscsi_unmap_sg_list(struct bnx2i_cmd *cmd);

void bnx2i_drop_session(struct iscsi_cls_session *session);

extern int bnx2i_send_fw_iscsi_init_msg(struct bnx2i_hba *hba);
extern int bnx2i_send_iscsi_login(struct bnx2i_conn *conn,
				  struct iscsi_task *mtask);
extern int bnx2i_send_iscsi_tmf(struct bnx2i_conn *conn,
				  struct iscsi_task *mtask);
extern int bnx2i_send_iscsi_scsicmd(struct bnx2i_conn *conn,
				    struct bnx2i_cmd *cmnd);
extern int bnx2i_send_iscsi_nopout(struct bnx2i_conn *conn,
				   struct iscsi_task *mtask, u32 ttt,
				   char *datap, int data_len, int unsol);
extern int bnx2i_send_iscsi_logout(struct bnx2i_conn *conn,
				   struct iscsi_task *mtask);
extern void bnx2i_send_cmd_cleanup_req(struct bnx2i_hba *hba,
				       struct bnx2i_cmd *cmd);
extern void bnx2i_send_conn_ofld_req(struct bnx2i_hba *hba,
				     struct bnx2i_endpoint *ep);
extern void bnx2i_update_iscsi_conn(struct iscsi_conn *conn);
extern void bnx2i_send_conn_destroy(struct bnx2i_hba *hba,
				    struct bnx2i_endpoint *ep);

extern int bnx2i_alloc_qp_resc(struct bnx2i_hba *hba,
			       struct bnx2i_endpoint *ep);
extern void bnx2i_free_qp_resc(struct bnx2i_hba *hba,
			       struct bnx2i_endpoint *ep);
extern void bnx2i_ep_ofld_timer(unsigned long data);
extern struct bnx2i_endpoint *bnx2i_find_ep_in_ofld_list(
		struct bnx2i_hba *hba, u32 iscsi_cid);
extern struct bnx2i_endpoint *bnx2i_find_ep_in_destroy_list(
		struct bnx2i_hba *hba, u32 iscsi_cid);

extern int bnx2i_map_ep_dbell_regs(struct bnx2i_endpoint *ep);
extern void bnx2i_arm_cq_event_coalescing(struct bnx2i_endpoint *ep, u8 action);

/* Debug related function prototypes */
extern void bnx2i_print_pend_cmd_queue(struct bnx2i_conn *conn);
extern void bnx2i_print_active_cmd_queue(struct bnx2i_conn *conn);
extern void bnx2i_print_xmit_pdu_queue(struct bnx2i_conn *conn);
extern void bnx2i_print_recv_state(struct bnx2i_conn *conn);

#endif
