/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2004-2016 Emulex.  All rights reserved.           *
 * EMULEX and SLI are trademarks of Emulex.                        *
 * www.emulex.com                                                  *
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

/* forward declaration for LPFC_IOCB_t's use */
struct lpfc_hba;
struct lpfc_vport;

/* Define the context types that SLI handles for abort and sums. */
typedef enum _lpfc_ctx_cmd {
	LPFC_CTX_LUN,
	LPFC_CTX_TGT,
	LPFC_CTX_HOST
} lpfc_ctx_cmd;

struct lpfc_cq_event {
	struct list_head list;
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
	struct lpfc_wcqe_complete wcqe_cmpl;	/* WQE cmpl */
	uint64_t isr_timestamp;

	/* Be careful here */
	union lpfc_wqe wqe;	/* WQE cmd */
	IOCB_t iocb;		/* For IOCB cmd or if we want 128 byte WQE */

	uint8_t rsvd2;
	uint8_t priority;	/* OAS priority */
	uint8_t retry;		/* retry counter for IOCB cmd - if needed */
	uint32_t iocb_flag;
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

	uint32_t drvrTimeout;	/* driver timeout in seconds */
	struct lpfc_vport *vport;/* virtual port pointer */
	void *context1;		/* caller context information */
	void *context2;		/* caller context information */
	void *context3;		/* caller context information */
	union {
		wait_queue_head_t    *wait_queue;
		struct lpfc_iocbq    *rsp_iocb;
		struct lpfcMboxq     *mbox;
		struct lpfc_nodelist *ndlp;
		struct lpfc_node_rrq *rrq;
	} context_un;

	void (*fabric_iocb_cmpl)(struct lpfc_hba *, struct lpfc_iocbq *,
			   struct lpfc_iocbq *);
	void (*wait_iocb_cmpl)(struct lpfc_hba *, struct lpfc_iocbq *,
			   struct lpfc_iocbq *);
	void (*iocb_cmpl)(struct lpfc_hba *, struct lpfc_iocbq *,
			   struct lpfc_iocbq *);
	void (*wqe_cmpl)(struct lpfc_hba *, struct lpfc_iocbq *,
			  struct lpfc_wcqe_complete *);
};

#define SLI_IOCB_RET_IOCB      1	/* Return IOCB if cmd ring full */

#define IOCB_SUCCESS        0
#define IOCB_BUSY           1
#define IOCB_ERROR          2
#define IOCB_TIMEDOUT       3

#define SLI_WQE_RET_WQE    1    /* Return WQE if cmd ring full */

#define WQE_SUCCESS        0
#define WQE_BUSY           1
#define WQE_ERROR          2
#define WQE_TIMEDOUT       3
#define WQE_ABORTED        4

#define LPFC_MBX_WAKE		1
#define LPFC_MBX_IMED_UNREG	2

typedef struct lpfcMboxq {
	/* MBOXQs are used in single linked lists */
	struct list_head list;	/* ptr to next mailbox command */
	union {
		MAILBOX_t mb;		/* Mailbox cmd */
		struct lpfc_mqe mqe;
	} u;
	struct lpfc_vport *vport;/* virtual port pointer */
	void *context1;		/* caller context information */
	void *context2;		/* caller context information */

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
#define LPFC_MENLO_MAINT          0x1000 /* need for menl fw download */
#define LPFC_SLI_ASYNC_MBX_BLK    0x2000 /* Async mailbox is blocked */
#define LPFC_SLI_SUPPRESS_RSP     0x4000 /* Suppress RSP feature is supported */

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
	unsigned long  stats_start;        /* in seconds */
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
