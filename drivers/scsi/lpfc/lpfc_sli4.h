/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2017-2022 Broadcom. All Rights Reserved. The term *
 * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.     *
 * Copyright (C) 2009-2016 Emulex.  All rights reserved.           *
 * EMULEX and SLI are trademarks of Emulex.                        *
 * www.broadcom.com                                                *
 *                                                                 *
 * This program is free software; you can redistribute it and/or   *
 * modify it under the terms of version 2 of the GNU General       *
 * Public License as published by the Free Software Foundation.    *
 * This program is distributed in the hope that it will be useful. *
 * ALL EXPRESS OR IMPLIED CONDITIONS, REPRESENTATIONS AND          *
 * WARRANTIES, INCLUDING ANY IMPLIED WARRANTY OF MERCHANTABILITY,  *
 * FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT, ARE      *
 * DISCLAIMED, EXCEPT TO THE EXTENT THAT SUCH DISCLAIMERS ARE HELD *
 * TO BE LEGALLY INVALID.  See the GNU General Public License for  *
 * more details, a copy of which can be found in the file COPYING  *
 * included with this package.                                     *
 *******************************************************************/

#include <linux/irq_poll.h>
#include <linux/cpufreq.h>

#if defined(CONFIG_DEBUG_FS) && !defined(CONFIG_SCSI_LPFC_DEBUG_FS)
#define CONFIG_SCSI_LPFC_DEBUG_FS
#endif

#define LPFC_ACTIVE_MBOX_WAIT_CNT               100
#define LPFC_XRI_EXCH_BUSY_WAIT_TMO		10000
#define LPFC_XRI_EXCH_BUSY_WAIT_T1   		10
#define LPFC_XRI_EXCH_BUSY_WAIT_T2              30000
#define LPFC_RPI_LOW_WATER_MARK			10

#define LPFC_UNREG_FCF                          1
#define LPFC_SKIP_UNREG_FCF                     0

/* Amount of time in seconds for waiting FCF rediscovery to complete */
#define LPFC_FCF_REDISCOVER_WAIT_TMO		2000 /* msec */

/* Number of SGL entries can be posted in a 4KB nonembedded mbox command */
#define LPFC_NEMBED_MBOX_SGL_CNT		254

/* Multi-queue arrangement for FCP EQ/CQ/WQ tuples */
#define LPFC_HBA_HDWQ_MIN	0
#define LPFC_HBA_HDWQ_MAX	256
#define LPFC_HBA_HDWQ_DEF	LPFC_HBA_HDWQ_MIN

/* irq_chann range, values */
#define LPFC_IRQ_CHANN_MIN	0
#define LPFC_IRQ_CHANN_MAX	256
#define LPFC_IRQ_CHANN_DEF	LPFC_IRQ_CHANN_MIN

/* FCP MQ queue count limiting */
#define LPFC_FCP_MQ_THRESHOLD_MIN	0
#define LPFC_FCP_MQ_THRESHOLD_MAX	256
#define LPFC_FCP_MQ_THRESHOLD_DEF	8

/*
 * Provide the default FCF Record attributes used by the driver
 * when nonFIP mode is configured and there is no other default
 * FCF Record attributes.
 */
#define LPFC_FCOE_FCF_DEF_INDEX	0
#define LPFC_FCOE_FCF_GET_FIRST	0xFFFF
#define LPFC_FCOE_FCF_NEXT_NONE	0xFFFF

#define LPFC_FCOE_NULL_VID	0xFFF
#define LPFC_FCOE_IGNORE_VID	0xFFFF

/* First 3 bytes of default FCF MAC is specified by FC_MAP */
#define LPFC_FCOE_FCF_MAC3	0xFF
#define LPFC_FCOE_FCF_MAC4	0xFF
#define LPFC_FCOE_FCF_MAC5	0xFE
#define LPFC_FCOE_FCF_MAP0	0x0E
#define LPFC_FCOE_FCF_MAP1	0xFC
#define LPFC_FCOE_FCF_MAP2	0x00
#define LPFC_FCOE_MAX_RCV_SIZE	0x800
#define LPFC_FCOE_FKA_ADV_PER	0
#define LPFC_FCOE_FIP_PRIORITY	0x80

#define sli4_sid_from_fc_hdr(fc_hdr)  \
	((fc_hdr)->fh_s_id[0] << 16 | \
	 (fc_hdr)->fh_s_id[1] <<  8 | \
	 (fc_hdr)->fh_s_id[2])

#define sli4_did_from_fc_hdr(fc_hdr)  \
	((fc_hdr)->fh_d_id[0] << 16 | \
	 (fc_hdr)->fh_d_id[1] <<  8 | \
	 (fc_hdr)->fh_d_id[2])

#define sli4_fctl_from_fc_hdr(fc_hdr)  \
	((fc_hdr)->fh_f_ctl[0] << 16 | \
	 (fc_hdr)->fh_f_ctl[1] <<  8 | \
	 (fc_hdr)->fh_f_ctl[2])

#define sli4_type_from_fc_hdr(fc_hdr)  \
	((fc_hdr)->fh_type)

#define LPFC_FW_RESET_MAXIMUM_WAIT_10MS_CNT 12000

#define INT_FW_UPGRADE	0
#define RUN_FW_UPGRADE	1

enum lpfc_sli4_queue_type {
	LPFC_EQ,
	LPFC_GCQ,
	LPFC_MCQ,
	LPFC_WCQ,
	LPFC_RCQ,
	LPFC_MQ,
	LPFC_WQ,
	LPFC_HRQ,
	LPFC_DRQ
};

/* The queue sub-type defines the functional purpose of the queue */
enum lpfc_sli4_queue_subtype {
	LPFC_NONE,
	LPFC_MBOX,
	LPFC_IO,
	LPFC_ELS,
	LPFC_NVMET,
	LPFC_NVME_LS,
	LPFC_USOL
};

/* RQ buffer list */
struct lpfc_rqb {
	uint16_t entry_count;	  /* Current number of RQ slots */
	uint16_t buffer_count;	  /* Current number of buffers posted */
	struct list_head rqb_buffer_list;  /* buffers assigned to this HBQ */
				  /* Callback for HBQ buffer allocation */
	struct rqb_dmabuf *(*rqb_alloc_buffer)(struct lpfc_hba *);
				  /* Callback for HBQ buffer free */
	void               (*rqb_free_buffer)(struct lpfc_hba *,
					       struct rqb_dmabuf *);
};

enum lpfc_poll_mode {
	LPFC_QUEUE_WORK,
	LPFC_IRQ_POLL
};

struct lpfc_idle_stat {
	u64 prev_idle;
	u64 prev_wall;
};

struct lpfc_queue {
	struct list_head list;
	struct list_head wq_list;

	/*
	 * If interrupts are in effect on _all_ the eq's the footprint
	 * of polling code is zero (except mode). This memory is chec-
	 * ked for every io to see if the io needs to be polled and
	 * while completion to check if the eq's needs to be rearmed.
	 * Keep in same cacheline as the queue ptr to avoid cpu fetch
	 * stalls. Using 1B memory will leave us with 7B hole. Fill
	 * it with other frequently used members.
	 */
	uint16_t last_cpu;	/* most recent cpu */
	uint16_t hdwq;
	uint8_t	 qe_valid;
	uint8_t  mode;	/* interrupt or polling */
#define LPFC_EQ_INTERRUPT	0
#define LPFC_EQ_POLL		1

	struct list_head wqfull_list;
	enum lpfc_sli4_queue_type type;
	enum lpfc_sli4_queue_subtype subtype;
	struct lpfc_hba *phba;
	struct list_head child_list;
	struct list_head page_list;
	struct list_head sgl_list;
	struct list_head cpu_list;
	uint32_t entry_count;	/* Number of entries to support on the queue */
	uint32_t entry_size;	/* Size of each queue entry. */
	uint32_t entry_cnt_per_pg;
	uint32_t notify_interval; /* Queue Notification Interval
				   * For chip->host queues (EQ, CQ, RQ):
				   *  specifies the interval (number of
				   *  entries) where the doorbell is rung to
				   *  notify the chip of entry consumption.
				   * For host->chip queues (WQ):
				   *  specifies the interval (number of
				   *  entries) where consumption CQE is
				   *  requested to indicate WQ entries
				   *  consumed by the chip.
				   * Not used on an MQ.
				   */
#define LPFC_EQ_NOTIFY_INTRVL	16
#define LPFC_CQ_NOTIFY_INTRVL	16
#define LPFC_WQ_NOTIFY_INTRVL	16
#define LPFC_RQ_NOTIFY_INTRVL	16
	uint32_t max_proc_limit; /* Queue Processing Limit
				  * For chip->host queues (EQ, CQ):
				  *  specifies the maximum number of
				  *  entries to be consumed in one
				  *  processing iteration sequence. Queue
				  *  will be rearmed after each iteration.
				  * Not used on an MQ, RQ or WQ.
				  */
#define LPFC_EQ_MAX_PROC_LIMIT		256
#define LPFC_CQ_MIN_PROC_LIMIT		64
#define LPFC_CQ_MAX_PROC_LIMIT		LPFC_CQE_EXP_COUNT	// 4096
#define LPFC_CQ_DEF_MAX_PROC_LIMIT	LPFC_CQE_DEF_COUNT	// 1024
#define LPFC_CQ_MIN_THRESHOLD_TO_POLL	64
#define LPFC_CQ_MAX_THRESHOLD_TO_POLL	LPFC_CQ_DEF_MAX_PROC_LIMIT
#define LPFC_CQ_DEF_THRESHOLD_TO_POLL	LPFC_CQ_DEF_MAX_PROC_LIMIT
	uint32_t queue_claimed; /* indicates queue is being processed */
	uint32_t queue_id;	/* Queue ID assigned by the hardware */
	uint32_t assoc_qid;     /* Queue ID associated with, for CQ/WQ/MQ */
	uint32_t host_index;	/* The host's index for putting or getting */
	uint32_t hba_index;	/* The last known hba index for get or put */
	uint32_t q_mode;

	struct lpfc_sli_ring *pring; /* ptr to io ring associated with q */
	struct lpfc_rqb *rqbp;	/* ptr to RQ buffers */

	uint16_t page_count;	/* Number of pages allocated for this queue */
	uint16_t page_size;	/* size of page allocated for this queue */
#define LPFC_EXPANDED_PAGE_SIZE	16384
#define LPFC_DEFAULT_PAGE_SIZE	4096
	uint16_t chann;		/* Hardware Queue association WQ/CQ */
				/* CPU affinity for EQ */
#define LPFC_FIND_BY_EQ		0
#define LPFC_FIND_BY_HDWQ	1
	uint8_t db_format;
#define LPFC_DB_RING_FORMAT	0x01
#define LPFC_DB_LIST_FORMAT	0x02
	uint8_t q_flag;
#define HBA_NVMET_WQFULL	0x1 /* We hit WQ Full condition for NVMET */
#define HBA_NVMET_CQ_NOTIFY	0x1 /* LPFC_NVMET_CQ_NOTIFY CQEs this EQE */
#define HBA_EQ_DELAY_CHK	0x2 /* EQ is a candidate for coalescing */
#define LPFC_NVMET_CQ_NOTIFY	4
	void __iomem *db_regaddr;
	uint16_t dpp_enable;
	uint16_t dpp_id;
	void __iomem *dpp_regaddr;

	/* For q stats */
	uint32_t q_cnt_1;
	uint32_t q_cnt_2;
	uint32_t q_cnt_3;
	uint64_t q_cnt_4;
/* defines for EQ stats */
#define	EQ_max_eqe		q_cnt_1
#define	EQ_no_entry		q_cnt_2
#define	EQ_cqe_cnt		q_cnt_3
#define	EQ_processed		q_cnt_4

/* defines for CQ stats */
#define	CQ_mbox			q_cnt_1
#define	CQ_max_cqe		q_cnt_1
#define	CQ_release_wqe		q_cnt_2
#define	CQ_xri_aborted		q_cnt_3
#define	CQ_wq			q_cnt_4

/* defines for WQ stats */
#define	WQ_overflow		q_cnt_1
#define	WQ_posted		q_cnt_4

/* defines for RQ stats */
#define	RQ_no_posted_buf	q_cnt_1
#define	RQ_no_buf_found		q_cnt_2
#define	RQ_buf_posted		q_cnt_3
#define	RQ_rcv_buf		q_cnt_4

	struct work_struct	irqwork;
	struct work_struct	spwork;
	struct delayed_work	sched_irqwork;
	struct delayed_work	sched_spwork;

	uint64_t isr_timestamp;
	struct lpfc_queue *assoc_qp;
	struct list_head _poll_list;
	void **q_pgs;	/* array to index entries per page */

#define LPFC_IRQ_POLL_WEIGHT 256
	struct irq_poll iop;
	enum lpfc_poll_mode poll_mode;
};

struct lpfc_sli4_link {
	uint32_t speed;
	uint8_t duplex;
	uint8_t status;
	uint8_t type;
	uint8_t number;
	uint8_t fault;
	uint32_t logical_speed;
	uint16_t topology;
};

struct lpfc_fcf_rec {
	uint8_t  fabric_name[8];
	uint8_t  switch_name[8];
	uint8_t  mac_addr[6];
	uint16_t fcf_indx;
	uint32_t priority;
	uint16_t vlan_id;
	uint32_t addr_mode;
	uint32_t flag;
#define BOOT_ENABLE	0x01
#define RECORD_VALID	0x02
};

struct lpfc_fcf_pri_rec {
	uint16_t fcf_index;
#define LPFC_FCF_ON_PRI_LIST 0x0001
#define LPFC_FCF_FLOGI_FAILED 0x0002
	uint16_t flag;
	uint32_t priority;
};

struct lpfc_fcf_pri {
	struct list_head list;
	struct lpfc_fcf_pri_rec fcf_rec;
};

/*
 * Maximum FCF table index, it is for driver internal book keeping, it
 * just needs to be no less than the supported HBA's FCF table size.
 */
#define LPFC_SLI4_FCF_TBL_INDX_MAX	32

struct lpfc_fcf {
	uint16_t fcfi;
	uint32_t fcf_flag;
#define FCF_AVAILABLE	0x01 /* FCF available for discovery */
#define FCF_REGISTERED	0x02 /* FCF registered with FW */
#define FCF_SCAN_DONE	0x04 /* FCF table scan done */
#define FCF_IN_USE	0x08 /* Atleast one discovery completed */
#define FCF_INIT_DISC	0x10 /* Initial FCF discovery */
#define FCF_DEAD_DISC	0x20 /* FCF DEAD fast FCF failover discovery */
#define FCF_ACVL_DISC	0x40 /* All CVL fast FCF failover discovery */
#define FCF_DISCOVERY	(FCF_INIT_DISC | FCF_DEAD_DISC | FCF_ACVL_DISC)
#define FCF_REDISC_PEND	0x80 /* FCF rediscovery pending */
#define FCF_REDISC_EVT	0x100 /* FCF rediscovery event to worker thread */
#define FCF_REDISC_FOV	0x200 /* Post FCF rediscovery fast failover */
#define FCF_REDISC_PROG (FCF_REDISC_PEND | FCF_REDISC_EVT)
	uint16_t fcf_redisc_attempted;
	uint32_t addr_mode;
	uint32_t eligible_fcf_cnt;
	struct lpfc_fcf_rec current_rec;
	struct lpfc_fcf_rec failover_rec;
	struct list_head fcf_pri_list;
	struct lpfc_fcf_pri fcf_pri[LPFC_SLI4_FCF_TBL_INDX_MAX];
	uint32_t current_fcf_scan_pri;
	struct timer_list redisc_wait;
	unsigned long *fcf_rr_bmask; /* Eligible FCF indexes for RR failover */
};


#define LPFC_REGION23_SIGNATURE "RG23"
#define LPFC_REGION23_VERSION	1
#define LPFC_REGION23_LAST_REC  0xff
#define DRIVER_SPECIFIC_TYPE	0xA2
#define LINUX_DRIVER_ID		0x20
#define PORT_STE_TYPE		0x1

struct lpfc_fip_param_hdr {
	uint8_t type;
#define FCOE_PARAM_TYPE		0xA0
	uint8_t length;
#define FCOE_PARAM_LENGTH	2
	uint8_t parm_version;
#define FIPP_VERSION		0x01
	uint8_t parm_flags;
#define	lpfc_fip_param_hdr_fipp_mode_SHIFT	6
#define	lpfc_fip_param_hdr_fipp_mode_MASK	0x3
#define lpfc_fip_param_hdr_fipp_mode_WORD	parm_flags
#define	FIPP_MODE_ON				0x1
#define	FIPP_MODE_OFF				0x0
#define FIPP_VLAN_VALID				0x1
};

struct lpfc_fcoe_params {
	uint8_t fc_map[3];
	uint8_t reserved1;
	uint16_t vlan_tag;
	uint8_t reserved[2];
};

struct lpfc_fcf_conn_hdr {
	uint8_t type;
#define FCOE_CONN_TBL_TYPE		0xA1
	uint8_t length;   /* words */
	uint8_t reserved[2];
};

struct lpfc_fcf_conn_rec {
	uint16_t flags;
#define	FCFCNCT_VALID		0x0001
#define	FCFCNCT_BOOT		0x0002
#define	FCFCNCT_PRIMARY		0x0004   /* if not set, Secondary */
#define	FCFCNCT_FBNM_VALID	0x0008
#define	FCFCNCT_SWNM_VALID	0x0010
#define	FCFCNCT_VLAN_VALID	0x0020
#define	FCFCNCT_AM_VALID	0x0040
#define	FCFCNCT_AM_PREFERRED	0x0080   /* if not set, AM Required */
#define	FCFCNCT_AM_SPMA		0x0100	 /* if not set, FPMA */

	uint16_t vlan_tag;
	uint8_t fabric_name[8];
	uint8_t switch_name[8];
};

struct lpfc_fcf_conn_entry {
	struct list_head list;
	struct lpfc_fcf_conn_rec conn_rec;
};

/*
 * Define the host's bootstrap mailbox.  This structure contains
 * the member attributes needed to create, use, and destroy the
 * bootstrap mailbox region.
 *
 * The macro definitions for the bmbx data structure are defined
 * in lpfc_hw4.h with the register definition.
 */
struct lpfc_bmbx {
	struct lpfc_dmabuf *dmabuf;
	struct dma_address dma_address;
	void *avirt;
	dma_addr_t aphys;
	uint32_t bmbx_size;
};

#define LPFC_EQE_SIZE LPFC_EQE_SIZE_4

#define LPFC_EQE_SIZE_4B 	4
#define LPFC_EQE_SIZE_16B	16
#define LPFC_CQE_SIZE		16
#define LPFC_WQE_SIZE		64
#define LPFC_WQE128_SIZE	128
#define LPFC_MQE_SIZE		256
#define LPFC_RQE_SIZE		8

#define LPFC_EQE_DEF_COUNT	1024
#define LPFC_CQE_DEF_COUNT      1024
#define LPFC_CQE_EXP_COUNT      4096
#define LPFC_WQE_DEF_COUNT      256
#define LPFC_WQE_EXP_COUNT      1024
#define LPFC_MQE_DEF_COUNT      16
#define LPFC_RQE_DEF_COUNT	512

#define LPFC_QUEUE_NOARM	false
#define LPFC_QUEUE_REARM	true


/*
 * SLI4 CT field defines
 */
#define SLI4_CT_RPI 0
#define SLI4_CT_VPI 1
#define SLI4_CT_VFI 2
#define SLI4_CT_FCFI 3

/*
 * SLI4 specific data structures
 */
struct lpfc_max_cfg_param {
	uint16_t max_xri;
	uint16_t xri_base;
	uint16_t xri_used;
	uint16_t max_rpi;
	uint16_t rpi_base;
	uint16_t rpi_used;
	uint16_t max_vpi;
	uint16_t vpi_base;
	uint16_t vpi_used;
	uint16_t max_vfi;
	uint16_t vfi_base;
	uint16_t vfi_used;
	uint16_t max_fcfi;
	uint16_t fcfi_used;
	uint16_t max_eq;
	uint16_t max_rq;
	uint16_t max_cq;
	uint16_t max_wq;
};

struct lpfc_hba;
/* SLI4 HBA multi-fcp queue handler struct */
#define LPFC_SLI4_HANDLER_NAME_SZ	16
struct lpfc_hba_eq_hdl {
	uint32_t idx;
	uint16_t irq;
	char handler_name[LPFC_SLI4_HANDLER_NAME_SZ];
	struct lpfc_hba *phba;
	struct lpfc_queue *eq;
	struct cpumask aff_mask;
};

#define lpfc_get_eq_hdl(eqidx) (&phba->sli4_hba.hba_eq_hdl[eqidx])
#define lpfc_get_aff_mask(eqidx) (&phba->sli4_hba.hba_eq_hdl[eqidx].aff_mask)
#define lpfc_get_irq(eqidx) (phba->sli4_hba.hba_eq_hdl[eqidx].irq)

/*BB Credit recovery value*/
struct lpfc_bbscn_params {
	uint32_t word0;
#define lpfc_bbscn_min_SHIFT		0
#define lpfc_bbscn_min_MASK		0x0000000F
#define lpfc_bbscn_min_WORD		word0
#define lpfc_bbscn_max_SHIFT		4
#define lpfc_bbscn_max_MASK		0x0000000F
#define lpfc_bbscn_max_WORD		word0
#define lpfc_bbscn_def_SHIFT		8
#define lpfc_bbscn_def_MASK		0x0000000F
#define lpfc_bbscn_def_WORD		word0
};

/* Port Capabilities for SLI4 Parameters */
struct lpfc_pc_sli4_params {
	uint32_t supported;
	uint32_t if_type;
	uint32_t sli_rev;
	uint32_t sli_family;
	uint32_t featurelevel_1;
	uint32_t featurelevel_2;
	uint32_t proto_types;
#define LPFC_SLI4_PROTO_FCOE	0x0000001
#define LPFC_SLI4_PROTO_FC	0x0000002
#define LPFC_SLI4_PROTO_NIC	0x0000004
#define LPFC_SLI4_PROTO_ISCSI	0x0000008
#define LPFC_SLI4_PROTO_RDMA	0x0000010
	uint32_t sge_supp_len;
	uint32_t if_page_sz;
	uint32_t rq_db_window;
	uint32_t loopbk_scope;
	uint32_t oas_supported;
	uint32_t eq_pages_max;
	uint32_t eqe_size;
	uint32_t cq_pages_max;
	uint32_t cqe_size;
	uint32_t mq_pages_max;
	uint32_t mqe_size;
	uint32_t mq_elem_cnt;
	uint32_t wq_pages_max;
	uint32_t wqe_size;
	uint32_t rq_pages_max;
	uint32_t rqe_size;
	uint32_t hdr_pages_max;
	uint32_t hdr_size;
	uint32_t hdr_pp_align;
	uint32_t sgl_pages_max;
	uint32_t sgl_pp_align;
	uint32_t mib_size;
	uint16_t mi_ver;
#define LPFC_MIB1_SUPPORT	1
#define LPFC_MIB2_SUPPORT	2
#define LPFC_MIB3_SUPPORT	3
	uint16_t mi_value;
#define LPFC_DFLT_MIB_VAL	2
	uint8_t mib_bde_cnt;
	uint8_t cmf;
	uint8_t cqv;
	uint8_t mqv;
	uint8_t wqv;
	uint8_t rqv;
	uint8_t eqav;
	uint8_t cqav;
	uint8_t wqsize;
	uint8_t bv1s;
	uint8_t pls;
#define LPFC_WQ_SZ64_SUPPORT	1
#define LPFC_WQ_SZ128_SUPPORT	2
	uint8_t wqpcnt;
	uint8_t nvme;
};

#define LPFC_CQ_4K_PAGE_SZ	0x1
#define LPFC_CQ_16K_PAGE_SZ	0x4
#define LPFC_WQ_4K_PAGE_SZ	0x1
#define LPFC_WQ_16K_PAGE_SZ	0x4

struct lpfc_iov {
	uint32_t pf_number;
	uint32_t vf_number;
};

struct lpfc_sli4_lnk_info {
	uint8_t lnk_dv;
#define LPFC_LNK_DAT_INVAL	0
#define LPFC_LNK_DAT_VAL	1
	uint8_t lnk_tp;
#define LPFC_LNK_GE		0x0 /* FCoE */
#define LPFC_LNK_FC		0x1 /* FC */
#define LPFC_LNK_FC_TRUNKED	0x2 /* FC_Trunked */
	uint8_t lnk_no;
	uint8_t optic_state;
};

#define LPFC_SLI4_HANDLER_CNT		(LPFC_HBA_IO_CHAN_MAX+ \
					 LPFC_FOF_IO_CHAN_NUM)

/* Used for tracking CPU mapping attributes */
struct lpfc_vector_map_info {
	uint16_t	phys_id;
	uint16_t	core_id;
	uint16_t	eq;
	uint16_t	hdwq;
	uint16_t	flag;
#define LPFC_CPU_MAP_HYPER	0x1
#define LPFC_CPU_MAP_UNASSIGN	0x2
#define LPFC_CPU_FIRST_IRQ	0x4
};
#define LPFC_VECTOR_MAP_EMPTY	0xffff

/* Multi-XRI pool */
#define XRI_BATCH               8

struct lpfc_pbl_pool {
	struct list_head list;
	u32 count;
	spinlock_t lock;	/* lock for pbl_pool*/
};

struct lpfc_pvt_pool {
	u32 low_watermark;
	u32 high_watermark;

	struct list_head list;
	u32 count;
	spinlock_t lock;	/* lock for pvt_pool */
};

struct lpfc_multixri_pool {
	u32 xri_limit;

	/* Starting point when searching a pbl_pool with round-robin method */
	u32 rrb_next_hwqid;

	/* Used by lpfc_adjust_pvt_pool_count.
	 * io_req_count is incremented by 1 during IO submission. The heartbeat
	 * handler uses these two variables to determine if pvt_pool is idle or
	 * busy.
	 */
	u32 prev_io_req_count;
	u32 io_req_count;

	/* statistics */
	u32 pbl_empty_count;
#ifdef LPFC_MXP_STAT
	u32 above_limit_count;
	u32 below_limit_count;
	u32 local_pbl_hit_count;
	u32 other_pbl_hit_count;
	u32 stat_max_hwm;

#define LPFC_MXP_SNAPSHOT_TAKEN 3 /* snapshot is taken at 3rd heartbeats */
	u32 stat_pbl_count;
	u32 stat_pvt_count;
	u32 stat_busy_count;
	u32 stat_snapshot_taken;
#endif

	/* TODO: Separate pvt_pool into get and put list */
	struct lpfc_pbl_pool pbl_pool;   /* Public free XRI pool */
	struct lpfc_pvt_pool pvt_pool;   /* Private free XRI pool */
};

struct lpfc_fc4_ctrl_stat {
	u32 input_requests;
	u32 output_requests;
	u32 control_requests;
	u32 io_cmpls;
};

#ifdef LPFC_HDWQ_LOCK_STAT
struct lpfc_lock_stat {
	uint32_t alloc_xri_get;
	uint32_t alloc_xri_put;
	uint32_t free_xri;
	uint32_t wq_access;
	uint32_t alloc_pvt_pool;
	uint32_t mv_from_pvt_pool;
	uint32_t mv_to_pub_pool;
	uint32_t mv_to_pvt_pool;
	uint32_t free_pub_pool;
	uint32_t free_pvt_pool;
};
#endif

struct lpfc_eq_intr_info {
	struct list_head list;
	uint32_t icnt;
};

/* SLI4 HBA data structure entries */
struct lpfc_sli4_hdw_queue {
	/* Pointers to the constructed SLI4 queues */
	struct lpfc_queue *hba_eq;  /* Event queues for HBA */
	struct lpfc_queue *io_cq;   /* Fast-path FCP & NVME compl queue */
	struct lpfc_queue *io_wq;   /* Fast-path FCP & NVME work queue */
	uint16_t io_cq_map;

	/* Keep track of IO buffers for this hardware queue */
	spinlock_t io_buf_list_get_lock;  /* Common buf alloc list lock */
	struct list_head lpfc_io_buf_list_get;
	spinlock_t io_buf_list_put_lock;  /* Common buf free list lock */
	struct list_head lpfc_io_buf_list_put;
	spinlock_t abts_io_buf_list_lock; /* list of aborted IOs */
	struct list_head lpfc_abts_io_buf_list;
	uint32_t total_io_bufs;
	uint32_t get_io_bufs;
	uint32_t put_io_bufs;
	uint32_t empty_io_bufs;
	uint32_t abts_scsi_io_bufs;
	uint32_t abts_nvme_io_bufs;

	/* Multi-XRI pool per HWQ */
	struct lpfc_multixri_pool *p_multixri_pool;

	/* FC-4 Stats counters */
	struct lpfc_fc4_ctrl_stat nvme_cstat;
	struct lpfc_fc4_ctrl_stat scsi_cstat;
#ifdef LPFC_HDWQ_LOCK_STAT
	struct lpfc_lock_stat lock_conflict;
#endif

	/* Per HDWQ pool resources */
	struct list_head sgl_list;
	struct list_head cmd_rsp_buf_list;

	/* Lock for syncing Per HDWQ pool resources */
	spinlock_t hdwq_lock;
};

#ifdef LPFC_HDWQ_LOCK_STAT
/* compile time trylock stats */
#define lpfc_qp_spin_lock_irqsave(lock, flag, qp, lstat) \
	{ \
	int only_once = 1; \
	while (spin_trylock_irqsave(lock, flag) == 0) { \
		if (only_once) { \
			only_once = 0; \
			qp->lock_conflict.lstat++; \
		} \
	} \
	}
#define lpfc_qp_spin_lock(lock, qp, lstat) \
	{ \
	int only_once = 1; \
	while (spin_trylock(lock) == 0) { \
		if (only_once) { \
			only_once = 0; \
			qp->lock_conflict.lstat++; \
		} \
	} \
	}
#else
#define lpfc_qp_spin_lock_irqsave(lock, flag, qp, lstat) \
	spin_lock_irqsave(lock, flag)
#define lpfc_qp_spin_lock(lock, qp, lstat) spin_lock(lock)
#endif

#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
struct lpfc_hdwq_stat {
	u32 hdwq_no;
	u32 rcv_io;
	u32 xmt_io;
	u32 cmpl_io;
};
#endif

struct lpfc_sli4_hba {
	void __iomem *conf_regs_memmap_p; /* Kernel memory mapped address for
					   * config space registers
					   */
	void __iomem *ctrl_regs_memmap_p; /* Kernel memory mapped address for
					   * control registers
					   */
	void __iomem *drbl_regs_memmap_p; /* Kernel memory mapped address for
					   * doorbell registers
					   */
	void __iomem *dpp_regs_memmap_p;  /* Kernel memory mapped address for
					   * dpp registers
					   */
	union {
		struct {
			/* IF Type 0, BAR 0 PCI cfg space reg mem map */
			void __iomem *UERRLOregaddr;
			void __iomem *UERRHIregaddr;
			void __iomem *UEMASKLOregaddr;
			void __iomem *UEMASKHIregaddr;
		} if_type0;
		struct {
			/* IF Type 2, BAR 0 PCI cfg space reg mem map. */
			void __iomem *STATUSregaddr;
			void __iomem *CTRLregaddr;
			void __iomem *ERR1regaddr;
#define SLIPORT_ERR1_REG_ERR_CODE_1		0x1
#define SLIPORT_ERR1_REG_ERR_CODE_2		0x2
			void __iomem *ERR2regaddr;
#define SLIPORT_ERR2_REG_FW_RESTART		0x0
#define SLIPORT_ERR2_REG_FUNC_PROVISON		0x1
#define SLIPORT_ERR2_REG_FORCED_DUMP		0x2
#define SLIPORT_ERR2_REG_FAILURE_EQ		0x3
#define SLIPORT_ERR2_REG_FAILURE_CQ		0x4
#define SLIPORT_ERR2_REG_FAILURE_BUS		0x5
#define SLIPORT_ERR2_REG_FAILURE_RQ		0x6
			void __iomem *EQDregaddr;
		} if_type2;
	} u;

	/* IF type 0, BAR1 and if type 2, Bar 0 CSR register memory map */
	void __iomem *PSMPHRregaddr;

	/* Well-known SLI INTF register memory map. */
	void __iomem *SLIINTFregaddr;

	/* IF type 0, BAR 1 function CSR register memory map */
	void __iomem *ISRregaddr;	/* HST_ISR register */
	void __iomem *IMRregaddr;	/* HST_IMR register */
	void __iomem *ISCRregaddr;	/* HST_ISCR register */
	/* IF type 0, BAR 0 and if type 2, BAR 0 doorbell register memory map */
	void __iomem *RQDBregaddr;	/* RQ_DOORBELL register */
	void __iomem *WQDBregaddr;	/* WQ_DOORBELL register */
	void __iomem *CQDBregaddr;	/* CQ_DOORBELL register */
	void __iomem *EQDBregaddr;	/* EQ_DOORBELL register */
	void __iomem *MQDBregaddr;	/* MQ_DOORBELL register */
	void __iomem *BMBXregaddr;	/* BootStrap MBX register */

	uint32_t ue_mask_lo;
	uint32_t ue_mask_hi;
	uint32_t ue_to_sr;
	uint32_t ue_to_rp;
	struct lpfc_register sli_intf;
	struct lpfc_pc_sli4_params pc_sli4_params;
	struct lpfc_bbscn_params bbscn_params;
	struct lpfc_hba_eq_hdl *hba_eq_hdl; /* HBA per-WQ handle */

	void (*sli4_eq_clr_intr)(struct lpfc_queue *q);
	void (*sli4_write_eq_db)(struct lpfc_hba *phba, struct lpfc_queue *eq,
				uint32_t count, bool arm);
	void (*sli4_write_cq_db)(struct lpfc_hba *phba, struct lpfc_queue *cq,
				uint32_t count, bool arm);

	/* Pointers to the constructed SLI4 queues */
	struct lpfc_sli4_hdw_queue *hdwq;
	struct list_head lpfc_wq_list;

	/* Pointers to the constructed SLI4 queues for NVMET */
	struct lpfc_queue **nvmet_cqset; /* Fast-path NVMET CQ Set queues */
	struct lpfc_queue **nvmet_mrq_hdr; /* Fast-path NVMET hdr MRQs */
	struct lpfc_queue **nvmet_mrq_data; /* Fast-path NVMET data MRQs */

	struct lpfc_queue *mbx_cq; /* Slow-path mailbox complete queue */
	struct lpfc_queue *els_cq; /* Slow-path ELS response complete queue */
	struct lpfc_queue *nvmels_cq; /* NVME LS complete queue */
	struct lpfc_queue *mbx_wq; /* Slow-path MBOX work queue */
	struct lpfc_queue *els_wq; /* Slow-path ELS work queue */
	struct lpfc_queue *nvmels_wq; /* NVME LS work queue */
	struct lpfc_queue *hdr_rq; /* Slow-path Header Receive queue */
	struct lpfc_queue *dat_rq; /* Slow-path Data Receive queue */

	struct lpfc_name wwnn;
	struct lpfc_name wwpn;

	uint32_t fw_func_mode;	/* FW function protocol mode */
	uint32_t ulp0_mode;	/* ULP0 protocol mode */
	uint32_t ulp1_mode;	/* ULP1 protocol mode */

	/* Optimized Access Storage specific queues/structures */
	uint64_t oas_next_lun;
	uint8_t oas_next_tgt_wwpn[8];
	uint8_t oas_next_vpt_wwpn[8];

	/* Setup information for various queue parameters */
	int eq_esize;
	int eq_ecount;
	int cq_esize;
	int cq_ecount;
	int wq_esize;
	int wq_ecount;
	int mq_esize;
	int mq_ecount;
	int rq_esize;
	int rq_ecount;
#define LPFC_SP_EQ_MAX_INTR_SEC         10000
#define LPFC_FP_EQ_MAX_INTR_SEC         10000

	uint32_t intr_enable;
	struct lpfc_bmbx bmbx;
	struct lpfc_max_cfg_param max_cfg_param;
	uint16_t extents_in_use; /* must allocate resource extents. */
	uint16_t rpi_hdrs_in_use; /* must post rpi hdrs if set. */
	uint16_t next_xri; /* last_xri - max_cfg_param.xri_base = used */
	uint16_t next_rpi;
	uint16_t io_xri_max;
	uint16_t io_xri_cnt;
	uint16_t io_xri_start;
	uint16_t els_xri_cnt;
	uint16_t nvmet_xri_cnt;
	uint16_t nvmet_io_wait_cnt;
	uint16_t nvmet_io_wait_total;
	uint16_t cq_max;
	struct lpfc_queue **cq_lookup;
	struct list_head lpfc_els_sgl_list;
	struct list_head lpfc_abts_els_sgl_list;
	spinlock_t abts_io_buf_list_lock; /* list of aborted SCSI IOs */
	struct list_head lpfc_abts_io_buf_list;
	struct list_head lpfc_nvmet_sgl_list;
	spinlock_t abts_nvmet_buf_list_lock; /* list of aborted NVMET IOs */
	struct list_head lpfc_abts_nvmet_ctx_list;
	spinlock_t t_active_list_lock; /* list of active NVMET IOs */
	struct list_head t_active_ctx_list;
	struct list_head lpfc_nvmet_io_wait_list;
	struct lpfc_nvmet_ctx_info *nvmet_ctx_info;
	struct lpfc_sglq **lpfc_sglq_active_list;
	struct list_head lpfc_rpi_hdr_list;
	unsigned long *rpi_bmask;
	uint16_t *rpi_ids;
	uint16_t rpi_count;
	struct list_head lpfc_rpi_blk_list;
	unsigned long *xri_bmask;
	uint16_t *xri_ids;
	struct list_head lpfc_xri_blk_list;
	unsigned long *vfi_bmask;
	uint16_t *vfi_ids;
	uint16_t vfi_count;
	struct list_head lpfc_vfi_blk_list;
	struct lpfc_sli4_flags sli4_flags;
	struct list_head sp_queue_event;
	struct list_head sp_cqe_event_pool;
	struct list_head sp_asynce_work_queue;
	spinlock_t asynce_list_lock; /* protect sp_asynce_work_queue list */
	struct list_head sp_els_xri_aborted_work_queue;
	spinlock_t els_xri_abrt_list_lock; /* protect els_xri_aborted list */
	struct list_head sp_unsol_work_queue;
	struct lpfc_sli4_link link_state;
	struct lpfc_sli4_lnk_info lnk_info;
	uint32_t pport_name_sta;
#define LPFC_SLI4_PPNAME_NON	0
#define LPFC_SLI4_PPNAME_GET	1
	struct lpfc_iov iov;
	spinlock_t sgl_list_lock; /* list of aborted els IOs */
	spinlock_t nvmet_io_wait_lock; /* IOs waiting for ctx resources */
	uint32_t physical_port;

	/* CPU to vector mapping information */
	struct lpfc_vector_map_info *cpu_map;
	uint16_t num_possible_cpu;
	uint16_t num_present_cpu;
	struct cpumask irq_aff_mask;
	uint16_t curr_disp_cpu;
	struct lpfc_eq_intr_info __percpu *eq_info;
#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
	struct lpfc_hdwq_stat __percpu *c_stat;
#endif
	struct lpfc_idle_stat *idle_stat;
	uint32_t conf_trunk;
#define lpfc_conf_trunk_port0_WORD	conf_trunk
#define lpfc_conf_trunk_port0_SHIFT	0
#define lpfc_conf_trunk_port0_MASK	0x1
#define lpfc_conf_trunk_port1_WORD	conf_trunk
#define lpfc_conf_trunk_port1_SHIFT	1
#define lpfc_conf_trunk_port1_MASK	0x1
#define lpfc_conf_trunk_port2_WORD	conf_trunk
#define lpfc_conf_trunk_port2_SHIFT	2
#define lpfc_conf_trunk_port2_MASK	0x1
#define lpfc_conf_trunk_port3_WORD	conf_trunk
#define lpfc_conf_trunk_port3_SHIFT	3
#define lpfc_conf_trunk_port3_MASK	0x1
#define lpfc_conf_trunk_port0_nd_WORD	conf_trunk
#define lpfc_conf_trunk_port0_nd_SHIFT	4
#define lpfc_conf_trunk_port0_nd_MASK	0x1
#define lpfc_conf_trunk_port1_nd_WORD	conf_trunk
#define lpfc_conf_trunk_port1_nd_SHIFT	5
#define lpfc_conf_trunk_port1_nd_MASK	0x1
#define lpfc_conf_trunk_port2_nd_WORD	conf_trunk
#define lpfc_conf_trunk_port2_nd_SHIFT	6
#define lpfc_conf_trunk_port2_nd_MASK	0x1
#define lpfc_conf_trunk_port3_nd_WORD	conf_trunk
#define lpfc_conf_trunk_port3_nd_SHIFT	7
#define lpfc_conf_trunk_port3_nd_MASK	0x1
	uint8_t flash_id;
	uint8_t asic_rev;
	uint16_t fawwpn_flag;	/* FA-WWPN support state */
#define LPFC_FAWWPN_CONFIG	0x1 /* FA-PWWN is configured */
#define LPFC_FAWWPN_FABRIC	0x2 /* FA-PWWN success with Fabric */
};

enum lpfc_sge_type {
	GEN_BUFF_TYPE,
	SCSI_BUFF_TYPE,
	NVMET_BUFF_TYPE
};

enum lpfc_sgl_state {
	SGL_FREED,
	SGL_ALLOCATED,
	SGL_XRI_ABORTED
};

struct lpfc_sglq {
	/* lpfc_sglqs are used in double linked lists */
	struct list_head list;
	struct list_head clist;
	enum lpfc_sge_type buff_type; /* is this a scsi sgl */
	enum lpfc_sgl_state state;
	struct lpfc_nodelist *ndlp; /* ndlp associated with IO */
	uint16_t iotag;         /* pre-assigned IO tag */
	uint16_t sli4_lxritag;  /* logical pre-assigned xri. */
	uint16_t sli4_xritag;   /* pre-assigned XRI, (OXID) tag. */
	struct sli4_sge *sgl;	/* pre-assigned SGL */
	void *virt;		/* virtual address. */
	dma_addr_t phys;	/* physical address */
};

struct lpfc_rpi_hdr {
	struct list_head list;
	uint32_t len;
	struct lpfc_dmabuf *dmabuf;
	uint32_t page_count;
	uint32_t start_rpi;
	uint16_t next_rpi;
};

struct lpfc_rsrc_blks {
	struct list_head list;
	uint16_t rsrc_start;
	uint16_t rsrc_size;
	uint16_t rsrc_used;
};

struct lpfc_rdp_context {
	struct lpfc_nodelist *ndlp;
	uint16_t ox_id;
	uint16_t rx_id;
	READ_LNK_VAR link_stat;
	uint8_t page_a0[DMP_SFF_PAGE_A0_SIZE];
	uint8_t page_a2[DMP_SFF_PAGE_A2_SIZE];
	void (*cmpl)(struct lpfc_hba *, struct lpfc_rdp_context*, int);
};

struct lpfc_lcb_context {
	uint8_t  sub_command;
	uint8_t  type;
	uint8_t  capability;
	uint8_t  frequency;
	uint16_t  duration;
	uint16_t ox_id;
	uint16_t rx_id;
	struct lpfc_nodelist *ndlp;
};


/*
 * SLI4 specific function prototypes
 */
int lpfc_pci_function_reset(struct lpfc_hba *);
int lpfc_sli4_pdev_status_reg_wait(struct lpfc_hba *);
int lpfc_sli4_hba_setup(struct lpfc_hba *);
int lpfc_sli4_config(struct lpfc_hba *, struct lpfcMboxq *, uint8_t,
		     uint8_t, uint32_t, bool);
void lpfc_sli4_mbox_cmd_free(struct lpfc_hba *, struct lpfcMboxq *);
void lpfc_sli4_mbx_sge_set(struct lpfcMboxq *, uint32_t, dma_addr_t, uint32_t);
void lpfc_sli4_mbx_sge_get(struct lpfcMboxq *, uint32_t,
			   struct lpfc_mbx_sge *);
int lpfc_sli4_mbx_read_fcf_rec(struct lpfc_hba *, struct lpfcMboxq *,
			       uint16_t);

void lpfc_sli4_hba_reset(struct lpfc_hba *);
struct lpfc_queue *lpfc_sli4_queue_alloc(struct lpfc_hba *phba,
					 uint32_t page_size,
					 uint32_t entry_size,
					 uint32_t entry_count, int cpu);
void lpfc_sli4_queue_free(struct lpfc_queue *);
int lpfc_eq_create(struct lpfc_hba *, struct lpfc_queue *, uint32_t);
void lpfc_modify_hba_eq_delay(struct lpfc_hba *phba, uint32_t startq,
			     uint32_t numq, uint32_t usdelay);
int lpfc_cq_create(struct lpfc_hba *, struct lpfc_queue *,
			struct lpfc_queue *, uint32_t, uint32_t);
int lpfc_cq_create_set(struct lpfc_hba *phba, struct lpfc_queue **cqp,
			struct lpfc_sli4_hdw_queue *hdwq, uint32_t type,
			uint32_t subtype);
int32_t lpfc_mq_create(struct lpfc_hba *, struct lpfc_queue *,
		       struct lpfc_queue *, uint32_t);
int lpfc_wq_create(struct lpfc_hba *, struct lpfc_queue *,
			struct lpfc_queue *, uint32_t);
int lpfc_rq_create(struct lpfc_hba *, struct lpfc_queue *,
			struct lpfc_queue *, struct lpfc_queue *, uint32_t);
int lpfc_mrq_create(struct lpfc_hba *phba, struct lpfc_queue **hrqp,
			struct lpfc_queue **drqp, struct lpfc_queue **cqp,
			uint32_t subtype);
int lpfc_eq_destroy(struct lpfc_hba *, struct lpfc_queue *);
int lpfc_cq_destroy(struct lpfc_hba *, struct lpfc_queue *);
int lpfc_mq_destroy(struct lpfc_hba *, struct lpfc_queue *);
int lpfc_wq_destroy(struct lpfc_hba *, struct lpfc_queue *);
int lpfc_rq_destroy(struct lpfc_hba *, struct lpfc_queue *,
			 struct lpfc_queue *);
int lpfc_sli4_queue_setup(struct lpfc_hba *);
void lpfc_sli4_queue_unset(struct lpfc_hba *);
int lpfc_sli4_post_sgl(struct lpfc_hba *, dma_addr_t, dma_addr_t, uint16_t);
int lpfc_repost_io_sgl_list(struct lpfc_hba *phba);
uint16_t lpfc_sli4_next_xritag(struct lpfc_hba *);
void lpfc_sli4_free_xri(struct lpfc_hba *, int);
int lpfc_sli4_post_async_mbox(struct lpfc_hba *);
struct lpfc_cq_event *__lpfc_sli4_cq_event_alloc(struct lpfc_hba *);
struct lpfc_cq_event *lpfc_sli4_cq_event_alloc(struct lpfc_hba *);
void __lpfc_sli4_cq_event_release(struct lpfc_hba *, struct lpfc_cq_event *);
void lpfc_sli4_cq_event_release(struct lpfc_hba *, struct lpfc_cq_event *);
int lpfc_sli4_init_rpi_hdrs(struct lpfc_hba *);
int lpfc_sli4_post_rpi_hdr(struct lpfc_hba *, struct lpfc_rpi_hdr *);
int lpfc_sli4_post_all_rpi_hdrs(struct lpfc_hba *);
struct lpfc_rpi_hdr *lpfc_sli4_create_rpi_hdr(struct lpfc_hba *);
void lpfc_sli4_remove_rpi_hdrs(struct lpfc_hba *);
int lpfc_sli4_alloc_rpi(struct lpfc_hba *);
void lpfc_sli4_free_rpi(struct lpfc_hba *, int);
void lpfc_sli4_remove_rpis(struct lpfc_hba *);
void lpfc_sli4_async_event_proc(struct lpfc_hba *);
void lpfc_sli4_fcf_redisc_event_proc(struct lpfc_hba *);
int lpfc_sli4_resume_rpi(struct lpfc_nodelist *,
			void (*)(struct lpfc_hba *, LPFC_MBOXQ_t *), void *);
void lpfc_sli4_els_xri_abort_event_proc(struct lpfc_hba *phba);
void lpfc_sli4_nvme_pci_offline_aborted(struct lpfc_hba *phba,
					struct lpfc_io_buf *lpfc_ncmd);
void lpfc_sli4_nvme_xri_aborted(struct lpfc_hba *phba,
				struct sli4_wcqe_xri_aborted *axri,
				struct lpfc_io_buf *lpfc_ncmd);
void lpfc_sli4_io_xri_aborted(struct lpfc_hba *phba,
			      struct sli4_wcqe_xri_aborted *axri, int idx);
void lpfc_sli4_nvmet_xri_aborted(struct lpfc_hba *phba,
				 struct sli4_wcqe_xri_aborted *axri);
void lpfc_sli4_els_xri_aborted(struct lpfc_hba *,
			       struct sli4_wcqe_xri_aborted *);
void lpfc_sli4_vport_delete_els_xri_aborted(struct lpfc_vport *);
void lpfc_sli4_vport_delete_fcp_xri_aborted(struct lpfc_vport *);
int lpfc_sli4_brdreset(struct lpfc_hba *);
int lpfc_sli4_add_fcf_record(struct lpfc_hba *, struct fcf_record *);
void lpfc_sli_remove_dflt_fcf(struct lpfc_hba *);
int lpfc_sli4_get_els_iocb_cnt(struct lpfc_hba *);
int lpfc_sli4_get_iocb_cnt(struct lpfc_hba *phba);
int lpfc_sli4_init_vpi(struct lpfc_vport *);
void lpfc_sli4_eq_clr_intr(struct lpfc_queue *);
void lpfc_sli4_write_cq_db(struct lpfc_hba *phba, struct lpfc_queue *q,
			   uint32_t count, bool arm);
void lpfc_sli4_write_eq_db(struct lpfc_hba *phba, struct lpfc_queue *q,
			   uint32_t count, bool arm);
void lpfc_sli4_if6_eq_clr_intr(struct lpfc_queue *q);
void lpfc_sli4_if6_write_cq_db(struct lpfc_hba *phba, struct lpfc_queue *q,
			       uint32_t count, bool arm);
void lpfc_sli4_if6_write_eq_db(struct lpfc_hba *phba, struct lpfc_queue *q,
			       uint32_t count, bool arm);
void lpfc_sli4_fcfi_unreg(struct lpfc_hba *, uint16_t);
int lpfc_sli4_fcf_scan_read_fcf_rec(struct lpfc_hba *, uint16_t);
int lpfc_sli4_fcf_rr_read_fcf_rec(struct lpfc_hba *, uint16_t);
int lpfc_sli4_read_fcf_rec(struct lpfc_hba *, uint16_t);
void lpfc_mbx_cmpl_fcf_scan_read_fcf_rec(struct lpfc_hba *, LPFC_MBOXQ_t *);
void lpfc_mbx_cmpl_fcf_rr_read_fcf_rec(struct lpfc_hba *, LPFC_MBOXQ_t *);
void lpfc_mbx_cmpl_read_fcf_rec(struct lpfc_hba *, LPFC_MBOXQ_t *);
int lpfc_sli4_unregister_fcf(struct lpfc_hba *);
int lpfc_sli4_post_status_check(struct lpfc_hba *);
uint8_t lpfc_sli_config_mbox_subsys_get(struct lpfc_hba *, LPFC_MBOXQ_t *);
uint8_t lpfc_sli_config_mbox_opcode_get(struct lpfc_hba *, LPFC_MBOXQ_t *);
void lpfc_sli4_ras_dma_free(struct lpfc_hba *phba);
struct sli4_hybrid_sgl *lpfc_get_sgl_per_hdwq(struct lpfc_hba *phba,
					      struct lpfc_io_buf *buf);
struct fcp_cmd_rsp_buf *lpfc_get_cmd_rsp_buf_per_hdwq(struct lpfc_hba *phba,
						      struct lpfc_io_buf *buf);
int lpfc_put_sgl_per_hdwq(struct lpfc_hba *phba, struct lpfc_io_buf *buf);
int lpfc_put_cmd_rsp_buf_per_hdwq(struct lpfc_hba *phba,
				  struct lpfc_io_buf *buf);
void lpfc_free_sgl_per_hdwq(struct lpfc_hba *phba,
			    struct lpfc_sli4_hdw_queue *hdwq);
void lpfc_free_cmd_rsp_buf_per_hdwq(struct lpfc_hba *phba,
				    struct lpfc_sli4_hdw_queue *hdwq);
static inline void *lpfc_sli4_qe(struct lpfc_queue *q, uint16_t idx)
{
	return q->q_pgs[idx / q->entry_cnt_per_pg] +
		(q->entry_size * (idx % q->entry_cnt_per_pg));
}
