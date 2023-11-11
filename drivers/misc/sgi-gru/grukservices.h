/* SPDX-License-Identifier: GPL-2.0-or-later */

/*
 *  Copyright (c) 2008 Silicon Graphics, Inc.  All Rights Reserved.
 */
#ifndef __GRU_KSERVICES_H_
#define __GRU_KSERVICES_H_


/*
 * Message queues using the GRU to send/receive messages.
 *
 * These function allow the user to create a message queue for
 * sending/receiving 1 or 2 cacheline messages using the GRU.
 *
 * Processes SENDING messages will use a kernel CBR/DSR to send
 * the message. This is transparent to the caller.
 *
 * The receiver does not use any GRU resources.
 *
 * The functions support:
 * 	- single receiver
 * 	- multiple senders
 *	- cross partition message
 *
 * Missing features ZZZ:
 * 	- user options for dealing with timeouts, queue full, etc.
 * 	- gru_create_message_queue() needs interrupt vector info
 */

struct gru_message_queue_desc {
	void		*mq;			/* message queue vaddress */
	unsigned long	mq_gpa;			/* global address of mq */
	int		qlines;			/* queue size in CL */
	int		interrupt_vector;	/* interrupt vector */
	int		interrupt_pnode;	/* pnode for interrupt */
	int		interrupt_apicid;	/* lapicid for interrupt */
};

/*
 * Initialize a user allocated chunk of memory to be used as
 * a message queue. The caller must ensure that the queue is
 * in contiguous physical memory and is cacheline aligned.
 *
 * Message queue size is the total number of bytes allocated
 * to the queue including a 2 cacheline header that is used
 * to manage the queue.
 *
 *  Input:
 * 	mqd	pointer to message queue descriptor
 * 	p	pointer to user allocated mesq memory.
 * 	bytes	size of message queue in bytes
 *      vector	interrupt vector (zero if no interrupts)
 *      nasid	nasid of blade where interrupt is delivered
 *      apicid	apicid of cpu for interrupt
 *
 *  Errors:
 *  	0	OK
 *  	>0	error
 */
extern int gru_create_message_queue(struct gru_message_queue_desc *mqd,
		void *p, unsigned int bytes, int nasid, int vector, int apicid);

/*
 * Send a message to a message queue.
 *
 * Note: The message queue transport mechanism uses the first 32
 * bits of the message. Users should avoid using these bits.
 *
 *
 *   Input:
 * 	mqd	pointer to message queue descriptor
 * 	mesg	pointer to message. Must be 64-bit aligned
 * 	bytes	size of message in bytes
 *
 *   Output:
 *      0	message sent
 *     >0	Send failure - see error codes below
 *
 */
extern int gru_send_message_gpa(struct gru_message_queue_desc *mqd,
			void *mesg, unsigned int bytes);

/* Status values for gru_send_message() */
#define MQE_OK			0	/* message sent successfully */
#define MQE_CONGESTION		1	/* temporary congestion, try again */
#define MQE_QUEUE_FULL		2	/* queue is full */
#define MQE_UNEXPECTED_CB_ERR	3	/* unexpected CB error */
#define MQE_PAGE_OVERFLOW	10	/* BUG - queue overflowed a page */
#define MQE_BUG_NO_RESOURCES	11	/* BUG - could not alloc GRU cb/dsr */

/*
 * Advance the receive pointer for the message queue to the next message.
 * Note: current API requires messages to be gotten & freed in order. Future
 * API extensions may allow for out-of-order freeing.
 *
 *   Input
 * 	mqd	pointer to message queue descriptor
 * 	mesq	message being freed
 */
extern void gru_free_message(struct gru_message_queue_desc *mqd,
			     void *mesq);

/*
 * Get next message from message queue. Returns pointer to
 * message OR NULL if no message present.
 * User must call gru_free_message() after message is processed
 * in order to move the queue pointers to next message.
 *
 *   Input
 * 	mqd	pointer to message queue descriptor
 *
 *   Output:
 *	p	pointer to message
 *	NULL	no message available
 */
extern void *gru_get_next_message(struct gru_message_queue_desc *mqd);


/*
 * Read a GRU global GPA. Source can be located in a remote partition.
 *
 *    Input:
 *    	value		memory address where MMR value is returned
 *    	gpa		source numalink physical address of GPA
 *
 *    Output:
 *	0		OK
 *	>0		error
 */
int gru_read_gpa(unsigned long *value, unsigned long gpa);


/*
 * Copy data using the GRU. Source or destination can be located in a remote
 * partition.
 *
 *    Input:
 *    	dest_gpa	destination global physical address
 *    	src_gpa		source global physical address
 *    	bytes		number of bytes to copy
 *
 *    Output:
 *	0		OK
 *	>0		error
 */
extern int gru_copy_gpa(unsigned long dest_gpa, unsigned long src_gpa,
							unsigned int bytes);

/*
 * Reserve GRU resources to be used asynchronously.
 *
 * 	input:
 * 		blade_id  - blade on which resources should be reserved
 * 		cbrs	  - number of CBRs
 * 		dsr_bytes - number of DSR bytes needed
 * 		cmp	  - completion structure for waiting for
 * 			    async completions
 *	output:
 *		handle to identify resource
 *		(0 = no resources)
 */
extern unsigned long gru_reserve_async_resources(int blade_id, int cbrs, int dsr_bytes,
				struct completion *cmp);

/*
 * Release async resources previously reserved.
 *
 *	input:
 *		han - handle to identify resources
 */
extern void gru_release_async_resources(unsigned long han);

/*
 * Wait for async GRU instructions to complete.
 *
 *	input:
 *		han - handle to identify resources
 */
extern void gru_wait_async_cbr(unsigned long han);

/*
 * Lock previous reserved async GRU resources
 *
 *	input:
 *		han - handle to identify resources
 *	output:
 *		cb  - pointer to first CBR
 *		dsr - pointer to first DSR
 */
extern void gru_lock_async_resource(unsigned long han,  void **cb, void **dsr);

/*
 * Unlock previous reserved async GRU resources
 *
 *	input:
 *		han - handle to identify resources
 */
extern void gru_unlock_async_resource(unsigned long han);

#endif 		/* __GRU_KSERVICES_H_ */
