/*
 * Adjunct processor (AP) interfaces
 *
 * Copyright IBM Corp. 2017
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (version 2 only)
 * as published by the Free Software Foundation.
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

#define AP_MKQID(_card, _queue) (((_card) & 63) << 8 | ((_queue) & 255))
#define AP_QID_CARD(_qid) (((_qid) >> 8) & 63)
#define AP_QID_QUEUE(_qid) ((_qid) & 255)

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

#endif /* _ASM_S390_AP_H_ */
