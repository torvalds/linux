/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _PAPR_HVPIPE_H
#define _PAPR_HVPIPE_H

#define	HVPIPE_HMC_ID_MASK	0x02000000 /*02-HMC,00-reserved and HMC ID */

struct hvpipe_source_info {
	struct list_head list;	/* list of sources */
	u32 srcID;
	wait_queue_head_t recv_wqh;	 /* wake up poll() waitq */
	struct task_struct *tsk;
};

#endif /* _PAPR_HVPIPE_H */
