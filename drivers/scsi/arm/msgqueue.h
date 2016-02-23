/*
 *  linux/drivers/acorn/scsi/msgqueue.h
 *
 *  Copyright (C) 1997 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  message queue handling
 */
#ifndef MSGQUEUE_H
#define MSGQUEUE_H

struct message {
    char msg[8];
    int length;
    int fifo;
};

struct msgqueue_entry {
    struct message msg;
    struct msgqueue_entry *next;
};

#define NR_MESSAGES 4

typedef struct {
    struct msgqueue_entry *qe;
    struct msgqueue_entry *free;
    struct msgqueue_entry entries[NR_MESSAGES];
} MsgQueue_t;

/*
 * Function: void msgqueue_initialise(MsgQueue_t *msgq)
 * Purpose : initialise a message queue
 * Params  : msgq - queue to initialise
 */
extern void msgqueue_initialise(MsgQueue_t *msgq);

/*
 * Function: void msgqueue_free(MsgQueue_t *msgq)
 * Purpose : free a queue
 * Params  : msgq - queue to free
 */
extern void msgqueue_free(MsgQueue_t *msgq);

/*
 * Function: int msgqueue_msglength(MsgQueue_t *msgq)
 * Purpose : calculate the total length of all messages on the message queue
 * Params  : msgq - queue to examine
 * Returns : number of bytes of messages in queue
 */
extern int msgqueue_msglength(MsgQueue_t *msgq);

/*
 * Function: struct message *msgqueue_getmsg(MsgQueue_t *msgq, int msgno)
 * Purpose : return a message & its length
 * Params  : msgq   - queue to obtain message from
 *         : msgno  - message number
 * Returns : pointer to message string, or NULL
 */
extern struct message *msgqueue_getmsg(MsgQueue_t *msgq, int msgno);

/*
 * Function: int msgqueue_addmsg(MsgQueue_t *msgq, int length, ...)
 * Purpose : add a message onto a message queue
 * Params  : msgq   - queue to add message on
 *	     length - length of message
 *	     ...    - message bytes
 * Returns : != 0 if successful
 */
extern int msgqueue_addmsg(MsgQueue_t *msgq, int length, ...);

/*
 * Function: void msgqueue_flush(MsgQueue_t *msgq)
 * Purpose : flush all messages from message queue
 * Params  : msgq - queue to flush
 */
extern void msgqueue_flush(MsgQueue_t *msgq);

#endif
