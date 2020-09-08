/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Copyright IBM Corp. 2006
 *  Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com)
 */
#ifndef __S390_STP_H
#define __S390_STP_H

#include <linux/compiler.h>

/* notifier for syncs */
extern struct atomic_notifier_head s390_epoch_delta_notifier;

/* STP interruption parameter */
struct stp_irq_parm {
	u32		: 14;
	u32 tsc		:  1;	/* Timing status change */
	u32 lac		:  1;	/* Link availability change */
	u32 tcpc	:  1;	/* Time control parameter change */
	u32		: 15;
} __packed;

#define STP_OP_SYNC	1
#define STP_OP_CTRL	3

struct stp_sstpi {
	u32		: 32;
	u32		:  8;
	u32 stratum	:  8;
	u32 vbits	: 16;
	u32 leaps	: 16;
	u32 tmd		:  4;
	u32 ctn		:  4;
	u32		:  3;
	u32 c		:  1;
	u32 tst		:  4;
	u32 tzo		: 16;
	u32 dsto	: 16;
	u32 ctrl	: 16;
	u32		: 16;
	u32 tto;
	u32		: 32;
	u32 ctnid[3];
	u32		: 32;
	u32 todoff[4];
	u32 rsvd[48];
} __packed;

/* Functions needed by the machine check handler */
int stp_sync_check(void);
int stp_island_check(void);
void stp_queue_work(void);

#endif /* __S390_STP_H */
