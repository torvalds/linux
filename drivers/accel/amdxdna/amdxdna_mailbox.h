/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022-2024, Advanced Micro Devices, Inc.
 */

#ifndef _AIE2_MAILBOX_H_
#define _AIE2_MAILBOX_H_

struct mailbox;
struct mailbox_channel;

/*
 * xdna_mailbox_msg - message struct
 *
 * @opcode:	opcode for firmware
 * @handle:	handle used for the notify callback
 * @notify_cb:  callback function to notify the sender when there is response
 * @send_data:	pointing to sending data
 * @send_size:	size of the sending data
 *
 * The mailbox will split the sending data in to multiple firmware message if
 * the size of the data is too big. This is transparent to the sender. The
 * sender will receive one notification.
 */
struct xdna_mailbox_msg {
	u32		opcode;
	void		*handle;
	int		(*notify_cb)(void *handle, const u32 *data, size_t size);
	u8		*send_data;
	size_t		send_size;
};

/*
 * xdna_mailbox_res - mailbox hardware resource
 *
 * @ringbuf_base:	ring buffer base address
 * @ringbuf_size:	ring buffer size
 * @mbox_base:		mailbox base address
 * @mbox_size:		mailbox size
 */
struct xdna_mailbox_res {
	void __iomem	*ringbuf_base;
	size_t		ringbuf_size;
	void __iomem	*mbox_base;
	size_t		mbox_size;
	const char	*name;
};

/*
 * xdna_mailbox_chann_res - resources
 *
 * @rb_start_addr:	ring buffer start address
 * @rb_size:		ring buffer size
 * @mb_head_ptr_reg:	mailbox head pointer register
 * @mb_tail_ptr_reg:	mailbox tail pointer register
 */
struct xdna_mailbox_chann_res {
	u32 rb_start_addr;
	u32 rb_size;
	u32 mb_head_ptr_reg;
	u32 mb_tail_ptr_reg;
};

/*
 * xdna_mailbox_create() -- create mailbox subsystem and initialize
 *
 * @ddev: device pointer
 * @res: SRAM and mailbox resources
 *
 * Return: If success, return a handle of mailbox subsystem.
 * Otherwise, return NULL pointer.
 */
struct mailbox *xdnam_mailbox_create(struct drm_device *ddev,
				     const struct xdna_mailbox_res *res);

/*
 * xdna_mailbox_create_channel() -- Create a mailbox channel instance
 *
 * @mailbox: the handle return from xdna_mailbox_create()
 * @x2i: host to firmware mailbox resources
 * @i2x: firmware to host mailbox resources
 * @xdna_mailbox_intr_reg: register addr of MSI-X interrupt
 * @mb_irq: Linux IRQ number associated with mailbox MSI-X interrupt vector index
 *
 * Return: If success, return a handle of mailbox channel. Otherwise, return NULL.
 */
struct mailbox_channel *
xdna_mailbox_create_channel(struct mailbox *mailbox,
			    const struct xdna_mailbox_chann_res *x2i,
			    const struct xdna_mailbox_chann_res *i2x,
			    u32 xdna_mailbox_intr_reg,
			    int mb_irq);

/*
 * xdna_mailbox_destroy_channel() -- destroy mailbox channel
 *
 * @mailbox_chann: the handle return from xdna_mailbox_create_channel()
 *
 * Return: if success, return 0. otherwise return error code
 */
int xdna_mailbox_destroy_channel(struct mailbox_channel *mailbox_chann);

/*
 * xdna_mailbox_stop_channel() -- stop mailbox channel
 *
 * @mailbox_chann: the handle return from xdna_mailbox_create_channel()
 *
 * Return: if success, return 0. otherwise return error code
 */
void xdna_mailbox_stop_channel(struct mailbox_channel *mailbox_chann);

/*
 * xdna_mailbox_send_msg() -- Send a message
 *
 * @mailbox_chann: Mailbox channel handle
 * @msg: message struct for message information
 * @tx_timeout: the timeout value for sending the message in ms.
 *
 * Return: If success return 0, otherwise, return error code
 */
int xdna_mailbox_send_msg(struct mailbox_channel *mailbox_chann,
			  const struct xdna_mailbox_msg *msg, u64 tx_timeout);

#endif /* _AIE2_MAILBOX_ */
