/*
 * Linux network driver for Brocade Converged Network Adapter.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License (GPL) Version 2 as
 * published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */
/*
 * Copyright (c) 2006-2010 Brocade Communications Systems, Inc.
 * All rights reserved
 * www.brocade.com
 */

#ifndef __CNA_H__
#define __CNA_H__

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/bitops.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/if_vlan.h>
#include <linux/if_ether.h>

#define bfa_sm_fault(__event)    do {                            \
	pr_err("SM Assertion failure: %s: %d: event = %d\n",	\
		 __FILE__, __LINE__, __event);			\
} while (0)

extern char bfa_version[];

#define CNA_FW_FILE_CT	"ctfw-3.2.1.0.bin"
#define CNA_FW_FILE_CT2	"ct2fw-3.2.1.0.bin"
#define FC_SYMNAME_MAX	256	/*!< max name server symbolic name size */

#pragma pack(1)

typedef struct mac { u8 mac[ETH_ALEN]; } mac_t;

#pragma pack()

#define bfa_q_first(_q) ((void *)(((struct list_head *) (_q))->next))
#define bfa_q_next(_qe)	(((struct list_head *) (_qe))->next)
#define bfa_q_prev(_qe) (((struct list_head *) (_qe))->prev)

/*
 * bfa_q_qe_init - to initialize a queue element
 */
#define bfa_q_qe_init(_qe) {						\
	bfa_q_next(_qe) = (struct list_head *) NULL;			\
	bfa_q_prev(_qe) = (struct list_head *) NULL;			\
}

/*
 * bfa_q_deq - dequeue an element from head of the queue
 */
#define bfa_q_deq(_q, _qe) {						\
	if (!list_empty(_q)) {						\
		(*((struct list_head **) (_qe))) = bfa_q_next(_q);	\
		bfa_q_prev(bfa_q_next(*((struct list_head **) _qe))) =	\
						(struct list_head *) (_q); \
		bfa_q_next(_q) = bfa_q_next(*((struct list_head **) _qe)); \
		bfa_q_qe_init(*((struct list_head **) _qe));		\
	} else {							\
		*((struct list_head **)(_qe)) = NULL;			\
	}								\
}

/*
 * bfa_q_deq_tail - dequeue an element from tail of the queue
 */
#define bfa_q_deq_tail(_q, _qe) {					\
	if (!list_empty(_q)) {						\
		*((struct list_head **) (_qe)) = bfa_q_prev(_q);	\
		bfa_q_next(bfa_q_prev(*((struct list_head **) _qe))) =  \
						(struct list_head *) (_q); \
		bfa_q_prev(_q) = bfa_q_prev(*(struct list_head **) _qe);\
		bfa_q_qe_init(*((struct list_head **) _qe));		\
	} else {							\
		*((struct list_head **) (_qe)) = (struct list_head *) NULL; \
	}								\
}

/*
 * bfa_add_tail_head - enqueue an element at the head of queue
 */
#define bfa_q_enq_head(_q, _qe) {					\
	if (!(bfa_q_next(_qe) == NULL) && (bfa_q_prev(_qe) == NULL))	\
		pr_err("Assertion failure: %s:%d: %d",			\
			__FILE__, __LINE__,				\
		(bfa_q_next(_qe) == NULL) && (bfa_q_prev(_qe) == NULL));\
	bfa_q_next(_qe) = bfa_q_next(_q);				\
	bfa_q_prev(_qe) = (struct list_head *) (_q);			\
	bfa_q_prev(bfa_q_next(_q)) = (struct list_head *) (_qe);	\
	bfa_q_next(_q) = (struct list_head *) (_qe);			\
}

#endif /* __CNA_H__ */
