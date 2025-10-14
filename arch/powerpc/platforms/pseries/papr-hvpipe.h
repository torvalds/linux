/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _PAPR_HVPIPE_H
#define _PAPR_HVPIPE_H

#define	HVPIPE_HMC_ID_MASK	0x02000000 /*02-HMC,00-reserved and HMC ID */
#define	HVPIPE_MAX_WRITE_BUFFER_SIZE	4048
/*
 * hvpipe specific RTAS return values
 */
#define	RTAS_HVPIPE_CLOSED		-4

#define	HVPIPE_HDR_LEN	sizeof(struct papr_hvpipe_hdr)

enum hvpipe_migrate_action {
	HVPIPE_SUSPEND,
	HVPIPE_RESUME,
};

struct hvpipe_source_info {
	struct list_head list;	/* list of sources */
	u32 srcID;
	u32 hvpipe_status;
	wait_queue_head_t recv_wqh;	 /* wake up poll() waitq */
	struct task_struct *tsk;
};

/*
 * Source ID Format 0xCCRRQQQQ
 * CC = indicating value is source type (ex: 0x02 for HMC)
 * RR = 0x00 (reserved)
 * QQQQ = 0x0000 – 0xFFFF indicating the source index indetifier
 */
struct hvpipe_event_buf {
	__be32	srcID;		/* Source ID */
	u8	event_type;	/* 0x01 for hvpipe message available */
				/* from specified src ID */
				/* 0x02 for loss of pipe connection */
				/* with specified src ID */
};

void hvpipe_migration_handler(int action);
#endif /* _PAPR_HVPIPE_H */
