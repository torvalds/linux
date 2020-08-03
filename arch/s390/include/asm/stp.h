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
	unsigned int _pad0	: 14;
	unsigned int tsc	: 1;	/* Timing status change */
	unsigned int lac	: 1;	/* Link availability change */
	unsigned int tcpc	: 1;	/* Time control parameter change */
	unsigned int _pad2	: 15;
} __packed;

#define STP_OP_SYNC	1
#define STP_OP_CTRL	3

struct stp_sstpi {
	unsigned int rsvd0;
	unsigned int rsvd1 : 8;
	unsigned int stratum : 8;
	unsigned int vbits : 16;
	unsigned int leaps : 16;
	unsigned int tmd : 4;
	unsigned int ctn : 4;
	unsigned int rsvd2 : 3;
	unsigned int c : 1;
	unsigned int tst : 4;
	unsigned int tzo : 16;
	unsigned int dsto : 16;
	unsigned int ctrl : 16;
	unsigned int rsvd3 : 16;
	unsigned int tto;
	unsigned int rsvd4;
	unsigned int ctnid[3];
	unsigned int rsvd5;
	unsigned int todoff[4];
	unsigned int rsvd6[48];
} __packed;

/* Functions needed by the machine check handler */
int stp_sync_check(void);
int stp_island_check(void);
void stp_queue_work(void);

#endif /* __S390_STP_H */
