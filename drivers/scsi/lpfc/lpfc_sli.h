/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2017-2022 Broadcom. All Rights Reserved. The term *
 * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.     *
 * Copyright (C) 2004-2016 Emulex.  All rights reserved.           *
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

#if defined(CONFIG_DEBUG_FS) && !defined(CONFIG_SCSI_LPFC_DEBUG_FS)
#define CONFIG_SCSI_LPFC_DEBUG_FS
#endif

/* forward declaration for LPFC_IOCB_t's use */
struct lpfc_hba;
struct lpfc_vport;

/* Define the context types that SLI handles for abort and sums. */
typedef enum _lpfc_ctx_cmd {
	LPFC_CTX_LUN,
	LPFC_CTX_TGT,
	LPFC_CTX_HOST
} lpfc_ctx_cmd;

/* Enumeration to describe the thread lock context. */
enum lpfc_mbox_ctx {
	MBOX_THD_UNLOCKED,
	MBOX_THD_LOCKED
};

union lpfc_vmid_tag {
	uint32_t app_id;
	uint8_t cs_ctl_vmid;
	struct lpfc_vmid_context *vmid_context;	/* UVEM context information */
};

struct lpfc_cq_event {
	struct list_head list;
	uint16_t hdwq;
	union {
		struct lpfc_mcqe		mcqe_cmpl;
		struct lpfc_acqe_link		acqe_link;
		struct lpfc_acqe_fip		acqe_fip;
		struct lpfc_acqe_dcbx		acqe_dcbx;
		struct lpfc_acqe_grp5		acqe_grp5;
		struct lpfc_acqe_fc_la		acqe_fc;
		struct lpfc_acqe_sli		acqe_sli;
		struct lpfc_rcqe		rcqe_cmpl;
		struct sli4_wcqe_xri_aborted	wcqe_axri;
		struct lpfc_wcqe_complete	wcqe_cmpl;
	} cqe;
};

/* This structure is used to handle IOCB requests / responses */
struct lpfc_iocbq {
	/* lpfc_iocbqs are used in double linked lists */
	struct list_head list;
	struct list_head clist;
	struct list_head dlist;
	uint16_t iotag;         /* pre-assigned IO tag */
	uint16_t sli4_lxritag;  /* logical pre-assigned XRI. */
	uint16_t sli4_xritag;   /* pre-assigned XRI, (OXID) tag. */
	uint16_t hba_wqidx;     /* index to HBA work queue */
	struct lpfc_cq_event cq_event;
	uint64_t isr_timestamp;

	union lpfc_wqe128 wqe;	/* SLI-4 */
	IOCB_t iocb;		/* SLI-3 */
	struct lpfc_wcqe_complete wcqe_cmpl;	/* WQE cmpl */

	u32 unsol_rcv_len;	/* Receive len in usol path */

	/* Pack the u8's together and make them module-4. */
	u8 num_bdes;	/* Number of BDEs */
	u8 abort_bls;	/* ABTS by initiator or responder */
	u8 abort_rctl;	/* ACC or RJT flag */
	u8 priority;	/* OAS priority */
	u8 retry;	/* retry counter for IOCB cmd - if needed */
	u8 rsvd1;       /* Pad for u32 */
	u8 rsvd2;       /* Pad for u32 */
	u8 rsvd3;	/* Pad for u32 */

	u32 cmd_flag;
#define LPFC_IO_LIBDFC		1	/* libdfc iocb */
#define LPFC_IO_WAKE		2	/* Synchronous I/O completed */
#define LPFC_IO_WAKE_TMO	LPFC_IO_WAKE /* Synchronous I/O timed out */
#define LPFC_IO_FCP		4	/* FCP command -- iocbq in scsi_buf */
#define LPFC_DRIVER_ABORTED	8	/* driver aborted this request */
#define LPFC_IO_FABRIC		0x10	/* Iocb send using fabric scheduler */
#define LPFC_DELAY_MEM_FREE	0x20    /* Defer free'ing of FC data */
#define LPFC_EXCHANGE_BUSY	0x40    /* SLI4 hba reported XB in response */
#define LPFC_USE_FCPWQIDX	0x80    /* Submit to specified FCPWQ index */
#define DSS_SECURITY_OP		0x100	/* security IO */
#define LPFC_IO_ON_TXCMPLQ	0x200	/* The IO is still on the TXCMPLQ */
#define LPFC_IO_DIF_PASS	0x400	/* T10 DIF IO pass-thru prot */
#define LPFC_IO_DIF_STRIP	0x800	/* T10 DIF IO strip prot */
#define LPFC_IO_DIF_INSERT	0x1000	/* T10 DIF IO insert prot */
#define LPFC_IO_CMD_OUTSTANDING	0x2000 /* timeout handler abort window */

#define LPFC_FIP_ELS_ID_MASK	0xc000	/* ELS_ID range 0-3, non-shifted mask */
#define LPFC_FIP_ELS_ID_SHIFT	14

#define LPFC_IO_OAS		0x10000 /* OAS FCP IO */
#define LPFC_IO_FOF		0x20000 /* FOF FCP IO */
#define LPFC_IO_LOOPBACK	0x40000 /* Loopback IO */
#define LPFC_PRLI_NVME_REQ	0x80000 /* This is an NVME PRLI. */
#define LPFC_PRLI_FCP_REQ	0x100000 /* This is an NVME PRLI. */
#define LPFC_IO_NVME	        0x200000 /* NVME FCP command */
#define LPFC_IO_NVME_LS		0x400000 /* NVME LS command */
#define LPFC_IO_NVMET		0x800000 /* NVMET command */
#define LPFC_IO_VMID            0x1000000 /* VMID tagged IO */
#define LPFC_IO_CMF		0x4000000 /* CMF command */

	uint32_t drvrTimeout;	/* driver timeout in seconds */
	struct lpfc_vport *vport;/* virtual port pointer */
	struct lpfc_dmabuf *cmd_dmabuf;
	struct lpfc_dmabuf *rsp_dmabuf;
	struct lpfc_dmabuf *bpl_dmabuf;
	uint32_t event_tag;	/* LA Event tag */
	union {
		wait_queue_head_t    *wait_queue;
		struct lpfcMboxq     *mbox;
		struct lpfc_node_rrq *rrq;
		struct nvmefc_ls_req *nvme_lsreq;
		struct lpfc_async_xchg_ctx *axchg;
		struct bsg_job_data *dd_data;
	} context_un;

	struct lpfc_io_buf *io_buf;
	struct lpfc_iocbq *rsp_iocb;
	struct lpfc_nodelist *ndlp;
	union lpfc_vmid_tag vmid_tag;
	void (*fabric_cmd_cmpl)(struct lpfc_hba *phba, struct lpfc_iocbq *cmd,
				struct lpfc_iocbq *rsp);
	void (*wait_cmd_cmpl)(struct lpfc_hba *phba, struct lpfc_iocbq *cmd,
			      struct lpfc_iocbq *rsp);
	void (*cmd_cmpl)(struct lpfc_hba *phba, struct lpfc_iocbq *cmd,
			 struct lpfc_iocbq *rsp);
};

#define SLI_IOCB_RET_IOCB      1	/* Return IOCB if cmd ring full */

#define IOCB_SUCCESS        0
#define IOCB_BUSY           1
#define IOCB_ERROR          2
#define IOCB_TIMEDOUT       3
#define IOCB_ABORTED        4
#define IOCB_ABORTING	    5
#define IOCB_NORESOURCE	    6

#define SLI_WQE_RET_WQE    1    /* Return WQE if cmd ring full */

#define WQE_SUCCESS        0
#define WQE_BUSY           1
#define WQE_ERROR          2
#define WQE_TIMEDOUT       3
#define WQE_ABORTED        4
#define WQE_ABORTING	   5
#define WQE_NORESOURCE	   6

#define LPFC_MBX_WAKE		1
#define LPFC_MBX_IMED_UNREG	2

typedef struct lpfcMboxq {
	/* MBOXQs are used in single linked lists */
	struct list_head list;	/* ptr to next mailbox command */
	union {
		MAILBOX_t mb;		/* Mailbox cmd */
		struct lpfc_mqe mqe;
	} u;
	struct lpfc_vport *vport; /* virtual port pointer */
	void *ctx_ndlp;		  /* caller ndlp information */
	void *ctx_buf;		  /* caller buffer information */
	void *context3;

	void (*mbox_cmpl) (struct lpfc_hba *, struct lpfcMboxq *);
	uint8_t mbox_flag;
	uint16_t in_ext_byte_len;
	uint16_t out_ext_byte_len;
	uint8_t  mbox_offset_word;
	struct lpfc_mcqe mcqe;
	struct lpfc_mbx_nembed_sge_virt *sge_array;
} LPFC_MBOXQ_t;

#define MBX_POLL        1	/* poll mailbox till command done, then
				   return */
#define MBX_NOWAIT      2	/* issue command then return immediately */

#define LPFC_MAX_RING_MASK  5	/* max num of rctl/type masks allowed per
				   ring */
#define LPFC_SLI3_MAX_RING  4	/* Max num of SLI3 rings used by driver.
				   For SLI4, an additional ring for each
				   FCP WQ will be allocated.  */

struct lpfc_sli_ring;

struct lpfc_sli_ring_mask {
	uint8_t profile;	/* profile associated with ring */
	uint8_t rctl;	/* rctl / type pair configured for ring */
	uint8_t type;	/* rctl / type pair configured for ring */
	uint8_t rsvd;
	/* rcv'd unsol event */
	void (*lpfc_sli_rcv_unsol_event) (struct lpfc_hba *,
					 struct lpfc_sli_ring *,
					 struct lpfc_iocbq *);
};


/* Structure used to hold SLI statistical counters and info */
struct lpfc_sli_ring_stat {
	uint64_t iocb_event;	 /* IOCB event counters */
	uint64_t iocb_cmd;	 /* IOCB cmd issued */
	uint64_t iocb_rsp;	 /* IOCB rsp received */
	uint64_t iocb_cmd_delay; /* IOCB cmd ring delay */
	uint64_t iocb_cmd_full;	 /* IOCB cmd ring full */
	uint64_t iocb_cmd_empty; /* IOCB cmd ring is now empty */
	uint64_t iocb_rsp_full;	 /* IOCB rsp ring full */
};

struct lpfc_sli3_ring {
	uint32_t local_getidx;  /* last available cmd index (from cmdGetInx) */
	uint32_t next_cmdidx;   /* next_cmd index */
	uint32_t rspidx;	/* current index in response ring */
	uint32_t cmdidx;	/* current index in command ring */
	uint16_t numCiocb;	/* number of command iocb's per ring */
	uint16_t numRiocb;	/* number of rsp iocb's per ring */
	uint16_t sizeCiocb;	/* Size of command iocb's in this ring */
	uint16_t sizeRiocb;	/* Size of response iocb's in this ring */
	uint32_t *cmdringaddr;	/* virtual address for cmd rings */
	uint32_t *rspringaddr;	/* virtual address for rsp rings */
};

struct lpfc_sli4_ring {
	struct lpfc_queue *wqp;	/* Pointer to associated WQ */
};


/* Structure used to hold SLI ring information */
struct lpfc_sli_ring {
	uint16_t flag;		/* ring flags */
#define LPFC_DEFERRED_RING_EVENT 0x001	/* Deferred processing a ring event */
#define LPFC_CALL_RING_AVAILABLE 0x002	/* indicates cmd was full */
#define LPFC_STOP_IOCB_EVENT     0x020	/* Stop processing IOCB cmds event */
	uint16_t abtsiotag;	/* tracks next iotag to use for ABTS */

	uint8_t rsvd;
	uint8_t ringno;		/* ring number */

	spinlock_t ring_lock;	/* lock for issuing commands */

	uint32_t fast_iotag;	/* max fastlookup based iotag           */
	uint32_t iotag_ctr;	/* keeps track of the next iotag to use */
	uint32_t iotag_max;	/* max iotag value to use               */
	struct list_head txq;
	uint16_t txq_cnt;	/* current length of queue */
	uint16_t txq_max;	/* max length */
	struct list_head txcmplq;
	uint16_t txcmplq_cnt;	/* current length of queue */
	uint16_t txcmplq_max;	/* max length */
	uint32_t missbufcnt;	/* keep track of buffers to post */
	struct list_head postbufq;
	uint16_t postbufq_cnt;	/* current length of queue */
	uint16_t postbufq_max;	/* max length */
	struct list_head iocb_continueq;
	uint16_t iocb_continueq_cnt;	/* current length of queue */
	uint16_t iocb_continueq_max;	/* max length */
	struct list_head iocb_continue_saveq;

	struct lpfc_sli_ring_mask prt[LPFC_MAX_RING_MASK];
	uint32_t num_mask;	/* number of mask entries in prt array */
	void (*lpfc_sli_rcv_async_status) (struct lpfc_hba *,
		struct lpfc_sli_ring *, struct lpfc_iocbq *);

	struct lpfc_sli_ring_stat stats;	/* SLI statistical info */

	/* cmd ring available */
	void (*lpfc_sli_cmd_available) (struct lpfc_hba *,
					struct lpfc_sli_ring *);
	union {
		struct lpfc_sli3_ring sli3;
		struct lpfc_sli4_ring sli4;
	} sli;
};

/* Structure used for configuring rings to a specific profile or rctl / type */
struct lpfc_hbq_init {
	uint32_t rn;		/* Receive buffer notification */
	uint32_t entry_count;	/* max # of entries in HBQ */
	uint32_t headerLen;	/* 0 if not profile 4 or 5 */
	uint32_t logEntry;	/* Set to 1 if this HBQ used for LogEntry */
	uint32_t profile;	/* Selection profile 0=all, 7=logentry */
	uint32_t ring_mask;	/* Binds HBQ to a ring e.g. Ring0=b0001,
				 * ring2=b0100 */
	uint32_t hbq_index;	/* index of this hbq in ring .HBQs[] */

	uint32_t seqlenoff;
	uint32_t maxlen;
	uint32_t seqlenbcnt;
	uint32_t cmdcodeoff;
	uint32_t cmdmatch[8];
	uint32_t mask_count;	/* number of mask entries in prt array */
	struct hbq_mask hbqMasks[6];

	/* Non-config rings fields to keep track of buffer allocations */
	uint32_t buffer_count;	/* number of buffers allocated */
	uint32_t init_count;	/* number to allocate when initialized */
	uint32_t add_count;	/* number to allocate when starved */
} ;

/* Structure used to hold SLI statistical counters and info */
struct lpfc_sli_stat {
	uint64_t mbox_stat_err;  /* Mbox cmds completed status error */
	uint64_t mbox_cmd;       /* Mailbox commands issued */
	uint64_t sli_intr;       /* Count of Host Attention interrupts */
	uint64_t sli_prev_intr;  /* Previous cnt of Host Attention interrupts */
	uint64_t sli_ips;        /* Host Attention interrupts per sec */
	uint32_t err_attn_event; /* Error Attn event counters */
	uint32_t link_event;     /* Link event counters */
	uint32_t mbox_event;     /* Mailbox event counters */
	uint32_t mbox_busy;	 /* Mailbox cmd busy */
};

/* Structure to store link status values when port stats are reset */
struct lpfc_lnk_stat {
	uint32_t link_failure_count;
	uint32_t loss_of_sync_count;
	uint32_t loss_of_signal_count;
	uint32_t prim_seq_protocol_err_count;
	uint32_t invalid_tx_word_count;
	uint32_t invalid_crc_count;
	uint32_t error_frames;
	uint32_t link_events;
};

/* Structure used to hold SLI information */
struct lpfc_sli {
	uint32_t num_rings;
	uint32_t sli_flag;

	/* Additional sli_flags */
#define LPFC_SLI_MBOX_ACTIVE      0x100	/* HBA mailbox is currently active */
#define LPFC_SLI_ACTIVE           0x200	/* SLI in firmware is active */
#define LPFC_PROCESS_LA           0x400	/* Able to process link attention */
#define LPFC_BLOCK_MGMT_IO        0x800	/* Don't allow mgmt mbx or iocb cmds */
#define LPFC_SLI_ASYNC_MBX_BLK    0x2000 /* Async mailbox is blocked */
#define LPFC_SLI_SUPPRESS_RSP     0x4000 /* Suppress RSP feature is supported */
#define LPFC_SLI_USE_EQDR         0x8000 /* EQ Delay Register is supported */
#define LPFC_QUEUE_FREE_INIT	  0x10000 /* Queue freeing is in progress */
#define LPFC_QUEUE_FREE_WAIT	  0x20000 /* Hold Queue free as it is being
					   * used outside worker thread
					   */

	struct lpfc_sli_ring *sli3_ring;

	struct lpfc_sli_stat slistat;	/* SLI statistical info */
	struct list_head mboxq;
	uint16_t mboxq_cnt;	/* current length of queue */
	uint16_t mboxq_max;	/* max length */
	LPFC_MBOXQ_t *mbox_active;	/* active mboxq information */
	struct list_head mboxq_cmpl;

	struct timer_list mbox_tmo;	/* Hold clk to timeout active mbox
					   cmd */

#define LPFC_IOCBQ_LOOKUP_INCREMENT  1024
	struct lpfc_iocbq ** iocbq_lookup; /* array to lookup IOCB by IOTAG */
	size_t iocbq_lookup_len;           /* current lengs of the array */
	uint16_t  last_iotag;              /* last allocated IOTAG */
	time64_t  stats_start;		   /* in seconds */
	struct lpfc_lnk_stat lnk_stat_offsets;
};

/* Timeout for normal outstanding mbox command (Seconds) */
#define LPFC_MBOX_TMO				30
/* Timeout for non-flash-based outstanding sli_config mbox command (Seconds) */
#define LPFC_MBOX_SLI4_CONFIG_TMO		60
/* Timeout for flash-based outstanding sli_config mbox command (Seconds) */
#define LPFC_MBOX_SLI4_CONFIG_EXTENDED_TMO	300
/* Timeout for other flash-based outstanding mbox command (Seconds) */
#define LPFC_MBOX_TMO_FLASH_CMD			300

struct lpfc_io_buf {
	/* Common fields */
	struct list_head list;
	void *data;

	dma_addr_t dma_handle;
	dma_addr_t dma_phys_sgl;

	struct sli4_sge *dma_sgl; /* initial segment chunk */

	/* linked list of extra sli4_hybrid_sge */
	struct list_head dma_sgl_xtra_list;

	/* list head for fcp_cmd_rsp buf */
	struct list_head dma_cmd_rsp_list;

	struct lpfc_iocbq cur_iocbq;
	struct lpfc_sli4_hdw_queue *hdwq;
	uint16_t hdwq_no;
	uint16_t cpu;

	struct lpfc_nodelist *ndlp;
	uint32_t timeout;
	uint16_t flags;
#define LPFC_SBUF_XBUSY		0x1	/* SLI4 hba reported XB on WCQE cmpl */
#define LPFC_SBUF_BUMP_QDEPTH	0x2	/* bumped queue depth counter */
					/* External DIF device IO conversions */
#define LPFC_SBUF_NORMAL_DIF	0x4	/* normal mode to insert/strip */
#define LPFC_SBUF_PASS_DIF	0x8	/* insert/strip mode to passthru */
#define LPFC_SBUF_NOT_POSTED    0x10    /* SGL failed post to FW. */
	uint16_t status;	/* From IOCB Word 7- ulpStatus */
	uint32_t result;	/* From IOCB Word 4. */

	uint32_t   seg_cnt;	/* Number of scatter-gather segments returned by
				 * dma_map_sg.  The driver needs this for calls
				 * to dma_unmap_sg.
				 */
	unsigned long start_time;
	spinlock_t buf_lock;	/* lock used in case of simultaneous abort */
	bool expedite;		/* this is an expedite io_buf */

	union {
		/* SCSI specific fields */
		struct {
			struct scsi_cmnd *pCmd;
			struct lpfc_rport_data *rdata;
			uint32_t prot_seg_cnt;  /* seg_cnt's counterpart for
						 * protection data
						 */

			/*
			 * data and dma_handle are the kernel virtual and bus
			 * address of the dma-able buffer containing the
			 * fcp_cmd, fcp_rsp and a scatter gather bde list that
			 * supports the sg_tablesize value.
			 */
			struct fcp_cmnd *fcp_cmnd;
			struct fcp_rsp *fcp_rsp;

			wait_queue_head_t *waitq;

#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
			/* Used to restore any changes to protection data for
			 * error injection
			 */
			void *prot_data_segment;
			uint32_t prot_data;
			uint32_t prot_data_type;
#define	LPFC_INJERR_REFTAG	1
#define	LPFC_INJERR_APPTAG	2
#define	LPFC_INJERR_GUARD	3
#endif
		};

		/* NVME specific fields */
		struct {
			struct nvmefc_fcp_req *nvmeCmd;
			uint16_t qidx;
		};
	};
#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
	uint64_t ts_cmd_start;
	uint64_t ts_last_cmd;
	uint64_t ts_cmd_wqput;
	uint64_t ts_isr_cmpl;
	uint64_t ts_data_io;
#endif
	uint64_t rx_cmd_start;
};
