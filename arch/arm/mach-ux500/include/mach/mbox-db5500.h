/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Stefan Nilsson <stefan.xk.nilsson@stericsson.com> for ST-Ericsson.
 * Author: Martin Persson <martin.persson@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL), version 2.
 */

#ifndef __INC_STE_MBOX_H
#define __INC_STE_MBOX_H

#define MBOX_BUF_SIZE 16
#define MBOX_NAME_SIZE 8

/**
  * mbox_recv_cb_t - Definition of the mailbox callback.
  * @mbox_msg:	The mailbox message.
  * @priv:	The clients private data as specified in the call to mbox_setup.
  *
  * This function will be called upon reception of new mailbox messages.
  */
typedef void mbox_recv_cb_t (u32 mbox_msg, void *priv);

/**
  * struct mbox - Mailbox instance struct
  * @list:		Linked list head.
  * @pdev:		Pointer to device struct.
  * @cb:		Callback function. Will be called
  *			when new data is received.
  * @client_data:	Clients private data. Will be sent back
  *			in the callback function.
  * @virtbase_peer:	Virtual address for outgoing mailbox.
  * @virtbase_local:	Virtual address for incoming mailbox.
  * @buffer:		Then internal queue for outgoing messages.
  * @name:		Name of this mailbox.
  * @buffer_available:	Completion variable to achieve "blocking send".
  *			This variable will be signaled when there is
  *			internal buffer space available.
  * @client_blocked:	To keep track if any client is currently
  *			blocked.
  * @lock:		Spinlock to protect this mailbox instance.
  * @write_index:	Index in internal buffer to write to.
  * @read_index:	Index in internal buffer to read from.
  * @allocated:		Indicates whether this particular mailbox
  *			id has been allocated by someone.
  */
struct mbox {
	struct list_head list;
	struct platform_device *pdev;
	mbox_recv_cb_t *cb;
	void *client_data;
	void __iomem *virtbase_peer;
	void __iomem *virtbase_local;
	u32 buffer[MBOX_BUF_SIZE];
	char name[MBOX_NAME_SIZE];
	struct completion buffer_available;
	u8 client_blocked;
	spinlock_t lock;
	u8 write_index;
	u8 read_index;
	bool allocated;
};

/**
  * mbox_setup - Set up a mailbox and return its instance.
  * @mbox_id:	The ID number of the mailbox. 0 or 1 for modem CPU,
  *		2 for modem DSP.
  * @mbox_cb:	Pointer to the callback function to be called when a new message
  *		is received.
  * @priv:	Client user data which will be returned in the callback.
  *
  * Returns a mailbox instance to be specified in subsequent calls to mbox_send.
  */
struct mbox *mbox_setup(u8 mbox_id, mbox_recv_cb_t *mbox_cb, void *priv);

/**
  * mbox_send - Send a mailbox message.
  * @mbox:	Mailbox instance (returned by mbox_setup)
  * @mbox_msg:	The mailbox message to send.
  * @block:	Specifies whether this call will block until send is possible,
  *		or return an error if the mailbox buffer is full.
  *
  * Returns 0 on success or a negative error code on error. -ENOMEM indicates
  * that the internal buffer is full and you have to try again later (or
  * specify "block" in order to block until send is possible).
  */
int mbox_send(struct mbox *mbox, u32 mbox_msg, bool block);

#endif /*INC_STE_MBOX_H*/
