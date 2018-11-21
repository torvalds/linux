/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Adjunct processor (AP) interfaces
 *
 * Copyright IBM Corp. 2017
 *
 * Author(s): Tony Krowiak <akrowia@linux.vnet.ibm.com>
 *	      Martin Schwidefsky <schwidefsky@de.ibm.com>
 *	      Harald Freudenberger <freude@de.ibm.com>
 */

#ifndef _ASM_S390_AP_H_
#define _ASM_S390_AP_H_

/**
 * The ap_qid_t identifier of an ap queue.
 * If the AP facilities test (APFT) facility is available,
 * card and queue index are 8 bit values, otherwise
 * card index is 6 bit and queue index a 4 bit value.
 */
typedef unsigned int ap_qid_t;

#define AP_MKQID(_card, _queue) (((_card) & 0xff) << 8 | ((_queue) & 0xff))
#define AP_QID_CARD(_qid) (((_qid) >> 8) & 0xff)
#define AP_QID_QUEUE(_qid) ((_qid) & 0xff)

/**
 * struct ap_queue_status - Holds the AP queue status.
 * @queue_empty: Shows if queue is empty
 * @replies_waiting: Waiting replies
 * @queue_full: Is 1 if the queue is full
 * @irq_enabled: Shows if interrupts are enabled for the AP
 * @response_code: Holds the 8 bit response code
 *
 * The ap queue status word is returned by all three AP functions
 * (PQAP, NQAP and DQAP).  There's a set of flags in the first
 * byte, followed by a 1 byte response code.
 */
struct ap_queue_status {
	unsigned int queue_empty	: 1;
	unsigned int replies_waiting	: 1;
	unsigned int queue_full		: 1;
	unsigned int _pad1		: 4;
	unsigned int irq_enabled	: 1;
	unsigned int response_code	: 8;
	unsigned int _pad2		: 16;
};

/**
 * ap_test_queue(): Test adjunct processor queue.
 * @qid: The AP queue number
 * @tbit: Test facilities bit
 * @info: Pointer to queue descriptor
 *
 * Returns AP queue status structure.
 */
struct ap_queue_status ap_test_queue(ap_qid_t qid,
				     int tbit,
				     unsigned long *info);

struct ap_config_info {
	unsigned int apsc	 : 1;	/* S bit */
	unsigned int apxa	 : 1;	/* N bit */
	unsigned int qact	 : 1;	/* C bit */
	unsigned int rc8a	 : 1;	/* R bit */
	unsigned char _reserved1 : 4;
	unsigned char _reserved2[3];
	unsigned char Na;		/* max # of APs - 1 */
	unsigned char Nd;		/* max # of Domains - 1 */
	unsigned char _reserved3[10];
	unsigned int apm[8];		/* AP ID mask */
	unsigned int aqm[8];		/* AP queue mask */
	unsigned int adm[8];		/* AP domain mask */
	unsigned char _reserved4[16];
} __aligned(8);

/*
 * ap_query_configuration(): Fetch cryptographic config info
 *
 * Returns the ap configuration info fetched via PQAP(QCI).
 * On success 0 is returned, on failure a negative errno
 * is returned, e.g. if the PQAP(QCI) instruction is not
 * available, the return value will be -EOPNOTSUPP.
 */
int ap_query_configuration(struct ap_config_info *info);

/*
 * struct ap_qirq_ctrl - convenient struct for easy invocation
 * of the ap_queue_irq_ctrl() function. This struct is passed
 * as GR1 parameter to the PQAP(AQIC) instruction. For details
 * please see the AR documentation.
 */
struct ap_qirq_ctrl {
	unsigned int _res1 : 8;
	unsigned int zone  : 8;  /* zone info */
	unsigned int ir    : 1;  /* ir flag: enable (1) or disable (0) irq */
	unsigned int _res2 : 4;
	unsigned int gisc  : 3;  /* guest isc field */
	unsigned int _res3 : 6;
	unsigned int gf    : 2;  /* gisa format */
	unsigned int _res4 : 1;
	unsigned int gisa  : 27; /* gisa origin */
	unsigned int _res5 : 1;
	unsigned int isc   : 3;  /* irq sub class */
};

/**
 * ap_queue_irq_ctrl(): Control interruption on a AP queue.
 * @qid: The AP queue number
 * @qirqctrl: struct ap_qirq_ctrl, see above
 * @ind: The notification indicator byte
 *
 * Returns AP queue status.
 *
 * Control interruption on the given AP queue.
 * Just a simple wrapper function for the low level PQAP(AQIC)
 * instruction available for other kernel modules.
 */
struct ap_queue_status ap_queue_irq_ctrl(ap_qid_t qid,
					 struct ap_qirq_ctrl qirqctrl,
					 void *ind);

#endif /* _ASM_S390_AP_H_ */
