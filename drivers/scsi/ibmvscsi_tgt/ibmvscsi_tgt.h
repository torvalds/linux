/*******************************************************************************
 * IBM Virtual SCSI Target Driver
 * Copyright (C) 2003-2005 Dave Boutcher (boutcher@us.ibm.com) IBM Corp.
 *			   Santiago Leon (santil@us.ibm.com) IBM Corp.
 *			   Linda Xie (lxie@us.ibm.com) IBM Corp.
 *
 * Copyright (C) 2005-2011 FUJITA Tomonori <tomof@acm.org>
 * Copyright (C) 2010 Nicholas A. Bellinger <nab@kernel.org>
 * Copyright (C) 2016 Bryant G. Ly <bryantly@linux.vnet.ibm.com> IBM Corp.
 *
 * Authors: Bryant G. Ly <bryantly@linux.vnet.ibm.com>
 * Authors: Michael Cyr <mikecyr@linux.vnet.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 ****************************************************************************/

#ifndef __H_IBMVSCSI_TGT
#define __H_IBMVSCSI_TGT

#include "libsrp.h"

#define SYS_ID_NAME_LEN		64
#define PARTITION_NAMELEN	96
#define IBMVSCSIS_NAMELEN       32

#define MSG_HI  0
#define MSG_LOW 1

#define MAX_CMD_Q_PAGES       4
#define CRQ_PER_PAGE          (PAGE_SIZE / sizeof(struct viosrp_crq))
/* in terms of number of elements */
#define DEFAULT_CMD_Q_SIZE    CRQ_PER_PAGE
#define MAX_CMD_Q_SIZE        (DEFAULT_CMD_Q_SIZE * MAX_CMD_Q_PAGES)

#define SRP_VIOLATION           0x102  /* general error code */

/*
 * SRP buffer formats defined as of 16.a supported by this driver.
 */
#define SUPPORTED_FORMATS  ((SRP_DATA_DESC_DIRECT << 1) | \
			    (SRP_DATA_DESC_INDIRECT << 1))

#define SCSI_LUN_ADDR_METHOD_FLAT	1

struct dma_window {
	u32 liobn;	/* Unique per vdevice */
	u64 tce_base;	/* Physical location of the TCE table */
	u64 tce_size;	/* Size of the TCE table in bytes */
};

struct target_dds {
	u64 unit_id;                /* 64 bit will force alignment */
#define NUM_DMA_WINDOWS 2
#define LOCAL  0
#define REMOTE 1
	struct dma_window  window[NUM_DMA_WINDOWS];

	/* root node property "ibm,partition-no" */
	uint partition_num;
	char partition_name[PARTITION_NAMELEN];
};

#define MAX_NUM_PORTS        1
#define MAX_H_COPY_RDMA      (128 * 1024)

#define MAX_EYE   64

/* Return codes */
#define ADAPT_SUCCESS            0L
/* choose error codes that do not conflict with PHYP */
#define ERROR                   -40L

struct format_code {
	u8 reserved;
	u8 buffers;
};

struct client_info {
#define SRP_VERSION "16.a"
	char srp_version[8];
	/* root node property ibm,partition-name */
	char partition_name[PARTITION_NAMELEN];
	/* root node property ibm,partition-no */
	u32 partition_number;
	/* initially 1 */
	u32 mad_version;
	u32 os_type;
};

/*
 * Changing this constant changes the number of seconds to wait before
 * considering the client will never service its queue again.
 */
#define SECONDS_TO_CONSIDER_FAILED 30
/*
 * These constants set the polling period used to determine if the client
 * has freed at least one element in the response queue.
 */
#define WAIT_SECONDS 1
#define WAIT_NANO_SECONDS 5000
#define MAX_TIMER_POPS ((1000000 / WAIT_NANO_SECONDS) * \
			SECONDS_TO_CONSIDER_FAILED)
/*
 * general purpose timer control block
 * which can be used for multiple functions
 */
struct timer_cb {
	struct hrtimer timer;
	/*
	 * how long has it been since the client
	 * serviced the queue. The variable is incrmented
	 * in the service_wait_q routine and cleared
	 * in send messages
	 */
	int timer_pops;
	/* the timer is started */
	bool started;
};

struct cmd_queue {
	/* kva */
	struct viosrp_crq *base_addr;
	dma_addr_t crq_token;
	/* used to maintain index */
	uint mask;
	/* current element */
	uint index;
	int size;
};

#define SCSOLNT_RESP_SHIFT	1
#define UCSOLNT_RESP_SHIFT	2

#define SCSOLNT         BIT(SCSOLNT_RESP_SHIFT)
#define UCSOLNT         BIT(UCSOLNT_RESP_SHIFT)

enum cmd_type {
	SCSI_CDB	= 0x01,
	TASK_MANAGEMENT	= 0x02,
	/* MAD or addressed to port 0 */
	ADAPTER_MAD	= 0x04,
	UNSET_TYPE	= 0x08,
};

struct iu_rsp {
	u8 format;
	u8 sol_not;
	u16 len;
	/* tag is just to help client identify cmd, so don't translate be/le */
	u64 tag;
};

struct ibmvscsis_cmd {
	struct list_head list;
	/* Used for TCM Core operations */
	struct se_cmd se_cmd;
	struct iu_entry *iue;
	struct iu_rsp rsp;
	struct work_struct work;
	struct scsi_info *adapter;
	/* Sense buffer that will be mapped into outgoing status */
	unsigned char sense_buf[TRANSPORT_SENSE_BUFFER];
	u64 init_time;
#define CMD_FAST_FAIL	BIT(0)
	u32 flags;
	char type;
};

struct ibmvscsis_nexus {
	struct se_session *se_sess;
};

struct ibmvscsis_tport {
	/* SCSI protocol the tport is providing */
	u8 tport_proto_id;
	/* ASCII formatted WWPN for SRP Target port */
	char tport_name[IBMVSCSIS_NAMELEN];
	/* Returned by ibmvscsis_make_tport() */
	struct se_wwn tport_wwn;
	/* Returned by ibmvscsis_make_tpg() */
	struct se_portal_group se_tpg;
	/* ibmvscsis port target portal group tag for TCM */
	u16 tport_tpgt;
	/* Pointer to TCM session for I_T Nexus */
	struct ibmvscsis_nexus *ibmv_nexus;
	bool enabled;
	bool releasing;
};

struct scsi_info {
	struct list_head list;
	char eye[MAX_EYE];

	/* commands waiting for space on repsonse queue */
	struct list_head waiting_rsp;
#define NO_QUEUE                    0x00
#define WAIT_ENABLED                0X01
#define WAIT_CONNECTION             0x04
	/* have established a connection */
#define CONNECTED                   0x08
	/* at least one port is processing SRP IU */
#define SRP_PROCESSING              0x10
	/* remove request received */
#define UNCONFIGURING               0x20
	/* disconnect by letting adapter go idle, no error */
#define WAIT_IDLE                   0x40
	/* disconnecting to clear an error */
#define ERR_DISCONNECT              0x80
	/* disconnect to clear error state, then come back up */
#define ERR_DISCONNECT_RECONNECT    0x100
	/* disconnected after clearing an error */
#define ERR_DISCONNECTED            0x200
	/* A series of errors caused unexpected errors */
#define UNDEFINED                   0x400
	u16  state;
	int fast_fail;
	struct target_dds dds;
	char *cmd_pool;
	/* list of free commands */
	struct list_head free_cmd;
	/* command elements ready for scheduler */
	struct list_head schedule_q;
	/* commands sent to TCM */
	struct list_head active_q;
	caddr_t *map_buf;
	/* ioba of map buffer */
	dma_addr_t map_ioba;
	/* allowable number of outstanding SRP requests */
	int request_limit;
	/* extra credit */
	int credit;
	/* outstanding transactions against credit limit */
	int debit;

	/* allow only one outstanding mad request */
#define PROCESSING_MAD                0x00002
	/* Waiting to go idle */
#define WAIT_FOR_IDLE		      0x00004
	/* H_REG_CRQ called */
#define CRQ_CLOSED                    0x00010
	/* detected that client has failed */
#define CLIENT_FAILED                 0x00040
	/* detected that transport event occurred */
#define TRANS_EVENT                   0x00080
	/* don't attempt to send anything to the client */
#define RESPONSE_Q_DOWN               0x00100
	/* request made to schedule disconnect handler */
#define SCHEDULE_DISCONNECT           0x00400
	/* disconnect handler is scheduled */
#define DISCONNECT_SCHEDULED          0x00800
	/* remove function is sleeping */
#define CFG_SLEEPING                  0x01000
	u32 flags;
	/* adapter lock */
	spinlock_t intr_lock;
	/* information needed to manage command queue */
	struct cmd_queue cmd_q;
	/* used in hcall to copy response back into srp buffer */
	u64  empty_iu_id;
	/* used in crq, to tag what iu the response is for */
	u64  empty_iu_tag;
	uint new_state;
	/* control block for the response queue timer */
	struct timer_cb rsp_q_timer;
	/* keep last client to enable proper accounting */
	struct client_info client_data;
	/* what can this client do */
	u32 client_cap;
	/*
	 * The following two fields capture state and flag changes that
	 * can occur when the lock is given up.  In the orginal design,
	 * the lock was held during calls into phyp;
	 * however, phyp did not meet PAPR architecture.  This is
	 * a work around.
	 */
	u16  phyp_acr_state;
	u32 phyp_acr_flags;

	struct workqueue_struct *work_q;
	struct completion wait_idle;
	struct completion unconfig;
	struct device dev;
	struct vio_dev *dma_dev;
	struct srp_target target;
	struct ibmvscsis_tport tport;
	struct tasklet_struct work_task;
	struct work_struct proc_work;
};

/*
 * Provide a constant that allows software to detect the adapter is
 * disconnecting from the client from one of several states.
 */
#define IS_DISCONNECTING (UNCONFIGURING | ERR_DISCONNECT_RECONNECT | \
			  ERR_DISCONNECT)

/*
 * Provide a constant that can be used with interrupt handling that
 * essentially lets the interrupt handler know that all requests should
 * be thrown out,
 */
#define DONT_PROCESS_STATE (IS_DISCONNECTING | UNDEFINED | \
			    ERR_DISCONNECTED  | WAIT_IDLE)

/*
 * If any of these flag bits are set then do not allow the interrupt
 * handler to schedule the off level handler.
 */
#define BLOCK (DISCONNECT_SCHEDULED)

/* State and transition events that stop the interrupt handler */
#define TARGET_STOP(VSCSI) (long)(((VSCSI)->state & DONT_PROCESS_STATE) | \
				  ((VSCSI)->flags & BLOCK))

/* flag bit that are not reset during disconnect */
#define PRESERVE_FLAG_FIELDS 0

#define vio_iu(IUE) ((union viosrp_iu *)((IUE)->sbuf->buf))

#define READ_CMD(cdb)	(((cdb)[0] & 0x1F) == 8)
#define WRITE_CMD(cdb)	(((cdb)[0] & 0x1F) == 0xA)

#ifndef H_GET_PARTNER_INFO
#define H_GET_PARTNER_INFO      0x0000000000000008LL
#endif

#define h_copy_rdma(l, sa, sb, da, db) \
		plpar_hcall_norets(H_COPY_RDMA, l, sa, sb, da, db)
#define h_vioctl(u, o, a, u1, u2, u3, u4) \
		plpar_hcall_norets(H_VIOCTL, u, o, a, u1, u2)
#define h_reg_crq(ua, tok, sz) \
		plpar_hcall_norets(H_REG_CRQ, ua, tok, sz)
#define h_free_crq(ua) \
		plpar_hcall_norets(H_FREE_CRQ, ua)
#define h_send_crq(ua, d1, d2) \
		plpar_hcall_norets(H_SEND_CRQ, ua, d1, d2)

#endif
