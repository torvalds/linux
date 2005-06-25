/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2004-2005 Emulex.  All rights reserved.           *
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

/* Define the context types that SLI handles for abort and sums. */
typedef enum _lpfc_ctx_cmd {
	LPFC_CTX_LUN,
	LPFC_CTX_TGT,
	LPFC_CTX_CTX,
	LPFC_CTX_HOST
} lpfc_ctx_cmd;

/* This structure is used to handle IOCB requests / responses */
struct lpfc_iocbq {
	/* lpfc_iocbqs are used in double linked lists */
	struct list_head list;
	IOCB_t iocb;		/* IOCB cmd */
	uint8_t retry;		/* retry counter for IOCB cmd - if needed */
	uint8_t iocb_flag;
#define LPFC_IO_POLL	1	/* Polling mode iocb */
#define LPFC_IO_LIBDFC	2	/* libdfc iocb */
#define LPFC_IO_WAIT	4
#define LPFC_IO_HIPRI	8	/* High Priority Queue signal flag */

	uint8_t abort_count;
	uint8_t rsvd2;
	uint32_t drvrTimeout;	/* driver timeout in seconds */
	void *context1;		/* caller context information */
	void *context2;		/* caller context information */
	void *context3;		/* caller context information */
	union {
		wait_queue_head_t *hipri_wait_queue; /* High Priority Queue wait
							queue */
		struct lpfc_iocbq  *rsp_iocb;
		struct lpfcMboxq   *mbox;
	} context_un;

	void (*iocb_cmpl) (struct lpfc_hba *, struct lpfc_iocbq *,
			   struct lpfc_iocbq *);

};

#define SLI_IOCB_RET_IOCB      1	/* Return IOCB if cmd ring full */
#define SLI_IOCB_HIGH_PRIORITY 2	/* High priority command */

#define IOCB_SUCCESS        0
#define IOCB_BUSY           1
#define IOCB_ERROR          2
#define IOCB_TIMEDOUT       3

typedef struct lpfcMboxq {
	/* MBOXQs are used in single linked lists */
	struct list_head list;	/* ptr to next mailbox command */
	MAILBOX_t mb;		/* Mailbox cmd */
	void *context1;		/* caller context information */
	void *context2;		/* caller context information */

	void (*mbox_cmpl) (struct lpfc_hba *, struct lpfcMboxq *);

} LPFC_MBOXQ_t;

#define MBX_POLL        1	/* poll mailbox till command done, then
				   return */
#define MBX_NOWAIT      2	/* issue command then return immediately */
#define MBX_STOP_IOCB   4	/* Stop iocb processing till mbox cmds
				   complete */

#define LPFC_MAX_RING_MASK  4	/* max num of rctl/type masks allowed per
				   ring */
#define LPFC_MAX_RING       4	/* max num of SLI rings used by driver */

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

/* Structure used to hold SLI ring information */
struct lpfc_sli_ring {
	uint16_t flag;		/* ring flags */
#define LPFC_DEFERRED_RING_EVENT 0x001	/* Deferred processing a ring event */
#define LPFC_CALL_RING_AVAILABLE 0x002	/* indicates cmd was full */
#define LPFC_STOP_IOCB_MBX       0x010	/* Stop processing IOCB cmds mbox */
#define LPFC_STOP_IOCB_EVENT     0x020	/* Stop processing IOCB cmds event */
#define LPFC_STOP_IOCB_MASK      0x030	/* Stop processing IOCB cmds mask */
	uint16_t abtsiotag;	/* tracks next iotag to use for ABTS */

	uint32_t local_getidx;   /* last available cmd index (from cmdGetInx) */
	uint32_t next_cmdidx;    /* next_cmd index */
	uint8_t rsvd;
	uint8_t ringno;		/* ring number */
	uint8_t rspidx;		/* current index in response ring */
	uint8_t cmdidx;		/* current index in command ring */
	uint16_t numCiocb;	/* number of command iocb's per ring */
	uint16_t numRiocb;	/* number of rsp iocb's per ring */

	uint32_t fast_iotag;	/* max fastlookup based iotag           */
	uint32_t iotag_ctr;	/* keeps track of the next iotag to use */
	uint32_t iotag_max;	/* max iotag value to use               */
	struct lpfc_iocbq ** fast_lookup; /* array of IOCB ptrs indexed by
					   iotag */
	struct list_head txq;
	uint16_t txq_cnt;	/* current length of queue */
	uint16_t txq_max;	/* max length */
	struct list_head txcmplq;
	uint16_t txcmplq_cnt;	/* current length of queue */
	uint16_t txcmplq_max;	/* max length */
	uint32_t *cmdringaddr;	/* virtual address for cmd rings */
	uint32_t *rspringaddr;	/* virtual address for rsp rings */
	uint32_t missbufcnt;	/* keep track of buffers to post */
	struct list_head postbufq;
	uint16_t postbufq_cnt;	/* current length of queue */
	uint16_t postbufq_max;	/* max length */
	struct list_head iocb_continueq;
	uint16_t iocb_continueq_cnt;	/* current length of queue */
	uint16_t iocb_continueq_max;	/* max length */

	struct lpfc_sli_ring_mask prt[LPFC_MAX_RING_MASK];
	uint32_t num_mask;	/* number of mask entries in prt array */

	struct lpfc_sli_ring_stat stats;	/* SLI statistical info */

	/* cmd ring available */
	void (*lpfc_sli_cmd_available) (struct lpfc_hba *,
					struct lpfc_sli_ring *);
};

/* Structure used to hold SLI statistical counters and info */
struct lpfc_sli_stat {
	uint64_t mbox_stat_err;  /* Mbox cmds completed status error */
	uint64_t mbox_cmd;       /* Mailbox commands issued */
	uint64_t sli_intr;       /* Count of Host Attention interrupts */
	uint32_t err_attn_event; /* Error Attn event counters */
	uint32_t link_event;     /* Link event counters */
	uint32_t mbox_event;     /* Mailbox event counters */
	uint32_t mbox_busy;	 /* Mailbox cmd busy */
};

/* Structure used to hold SLI information */
struct lpfc_sli {
	uint32_t num_rings;
	uint32_t sli_flag;

	/* Additional sli_flags */
#define LPFC_SLI_MBOX_ACTIVE      0x100	/* HBA mailbox is currently active */
#define LPFC_SLI2_ACTIVE          0x200	/* SLI2 overlay in firmware is active */
#define LPFC_PROCESS_LA           0x400	/* Able to process link attention */

	struct lpfc_sli_ring ring[LPFC_MAX_RING];
	int fcp_ring;		/* ring used for FCP initiator commands */
	int next_ring;

	int ip_ring;		/* ring used for IP network drv cmds */

	struct lpfc_sli_stat slistat;	/* SLI statistical info */
	struct list_head mboxq;
	uint16_t mboxq_cnt;	/* current length of queue */
	uint16_t mboxq_max;	/* max length */
	LPFC_MBOXQ_t *mbox_active;	/* active mboxq information */

	struct timer_list mbox_tmo;	/* Hold clk to timeout active mbox
					   cmd */

	uint32_t *MBhostaddr;	/* virtual address for mbox cmds */
};

/* Given a pointer to the start of the ring, and the slot number of
 * the desired iocb entry, calc a pointer to that entry.
 * (assume iocb entry size is 32 bytes, or 8 words)
 */
#define IOCB_ENTRY(ring,slot) ((IOCB_t *)(((char *)(ring)) + ((slot) * 32)))

#define LPFC_MBOX_TMO           30	/* Sec tmo for outstanding mbox
					   command */
