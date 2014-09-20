/* Intel Ethernet Switch Host Interface Driver
 * Copyright(c) 2013 - 2014 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 */

#include "fm10k_common.h"

/**
 *  fm10k_fifo_init - Initialize a message FIFO
 *  @fifo: pointer to FIFO
 *  @buffer: pointer to memory to be used to store FIFO
 *  @size: maximum message size to store in FIFO, must be 2^n - 1
 **/
static void fm10k_fifo_init(struct fm10k_mbx_fifo *fifo, u32 *buffer, u16 size)
{
	fifo->buffer = buffer;
	fifo->size = size;
	fifo->head = 0;
	fifo->tail = 0;
}

/**
 *  fm10k_fifo_used - Retrieve used space in FIFO
 *  @fifo: pointer to FIFO
 *
 *  This function returns the number of DWORDs used in the FIFO
 **/
static u16 fm10k_fifo_used(struct fm10k_mbx_fifo *fifo)
{
	return fifo->tail - fifo->head;
}

/**
 *  fm10k_fifo_unused - Retrieve unused space in FIFO
 *  @fifo: pointer to FIFO
 *
 *  This function returns the number of unused DWORDs in the FIFO
 **/
static u16 fm10k_fifo_unused(struct fm10k_mbx_fifo *fifo)
{
	return fifo->size + fifo->head - fifo->tail;
}

/**
 *  fm10k_fifo_empty - Test to verify if fifo is empty
 *  @fifo: pointer to FIFO
 *
 *  This function returns true if the FIFO is empty, else false
 **/
static bool fm10k_fifo_empty(struct fm10k_mbx_fifo *fifo)
{
	return fifo->head == fifo->tail;
}

/**
 *  fm10k_fifo_head_offset - returns indices of head with given offset
 *  @fifo: pointer to FIFO
 *  @offset: offset to add to head
 *
 *  This function returns the indicies into the fifo based on head + offset
 **/
static u16 fm10k_fifo_head_offset(struct fm10k_mbx_fifo *fifo, u16 offset)
{
	return (fifo->head + offset) & (fifo->size - 1);
}

/**
 *  fm10k_fifo_tail_offset - returns indices of tail with given offset
 *  @fifo: pointer to FIFO
 *  @offset: offset to add to tail
 *
 *  This function returns the indicies into the fifo based on tail + offset
 **/
static u16 fm10k_fifo_tail_offset(struct fm10k_mbx_fifo *fifo, u16 offset)
{
	return (fifo->tail + offset) & (fifo->size - 1);
}

/**
 *  fm10k_fifo_head_len - Retrieve length of first message in FIFO
 *  @fifo: pointer to FIFO
 *
 *  This function returns the size of the first message in the FIFO
 **/
static u16 fm10k_fifo_head_len(struct fm10k_mbx_fifo *fifo)
{
	u32 *head = fifo->buffer + fm10k_fifo_head_offset(fifo, 0);

	/* verify there is at least 1 DWORD in the fifo so *head is valid */
	if (fm10k_fifo_empty(fifo))
		return 0;

	/* retieve the message length */
	return FM10K_TLV_DWORD_LEN(*head);
}

/**
 *  fm10k_fifo_head_drop - Drop the first message in FIFO
 *  @fifo: pointer to FIFO
 *
 *  This function returns the size of the message dropped from the FIFO
 **/
static u16 fm10k_fifo_head_drop(struct fm10k_mbx_fifo *fifo)
{
	u16 len = fm10k_fifo_head_len(fifo);

	/* update head so it is at the start of next frame */
	fifo->head += len;

	return len;
}

/**
 *  fm10k_mbx_index_len - Convert a head/tail index into a length value
 *  @mbx: pointer to mailbox
 *  @head: head index
 *  @tail: head index
 *
 *  This function takes the head and tail index and determines the length
 *  of the data indicated by this pair.
 **/
static u16 fm10k_mbx_index_len(struct fm10k_mbx_info *mbx, u16 head, u16 tail)
{
	u16 len = tail - head;

	/* we wrapped so subtract 2, one for index 0, one for all 1s index */
	if (len > tail)
		len -= 2;

	return len & ((mbx->mbmem_len << 1) - 1);
}

/**
 *  fm10k_mbx_tail_add - Determine new tail value with added offset
 *  @mbx: pointer to mailbox
 *  @offset: length to add to head offset
 *
 *  This function takes the local tail index and recomputes it for
 *  a given length added as an offset.
 **/
static u16 fm10k_mbx_tail_add(struct fm10k_mbx_info *mbx, u16 offset)
{
	u16 tail = (mbx->tail + offset + 1) & ((mbx->mbmem_len << 1) - 1);

	/* add/sub 1 because we cannot have offset 0 or all 1s */
	return (tail > mbx->tail) ? --tail : ++tail;
}

/**
 *  fm10k_mbx_tail_sub - Determine new tail value with subtracted offset
 *  @mbx: pointer to mailbox
 *  @offset: length to add to head offset
 *
 *  This function takes the local tail index and recomputes it for
 *  a given length added as an offset.
 **/
static u16 fm10k_mbx_tail_sub(struct fm10k_mbx_info *mbx, u16 offset)
{
	u16 tail = (mbx->tail - offset - 1) & ((mbx->mbmem_len << 1) - 1);

	/* sub/add 1 because we cannot have offset 0 or all 1s */
	return (tail < mbx->tail) ? ++tail : --tail;
}

/**
 *  fm10k_mbx_head_add - Determine new head value with added offset
 *  @mbx: pointer to mailbox
 *  @offset: length to add to head offset
 *
 *  This function takes the local head index and recomputes it for
 *  a given length added as an offset.
 **/
static u16 fm10k_mbx_head_add(struct fm10k_mbx_info *mbx, u16 offset)
{
	u16 head = (mbx->head + offset + 1) & ((mbx->mbmem_len << 1) - 1);

	/* add/sub 1 because we cannot have offset 0 or all 1s */
	return (head > mbx->head) ? --head : ++head;
}

/**
 *  fm10k_mbx_head_sub - Determine new head value with subtracted offset
 *  @mbx: pointer to mailbox
 *  @offset: length to add to head offset
 *
 *  This function takes the local head index and recomputes it for
 *  a given length added as an offset.
 **/
static u16 fm10k_mbx_head_sub(struct fm10k_mbx_info *mbx, u16 offset)
{
	u16 head = (mbx->head - offset - 1) & ((mbx->mbmem_len << 1) - 1);

	/* sub/add 1 because we cannot have offset 0 or all 1s */
	return (head < mbx->head) ? ++head : --head;
}

/**
 *  fm10k_mbx_pushed_tail_len - Retrieve the length of message being pushed
 *  @mbx: pointer to mailbox
 *
 *  This function will return the length of the message currently being
 *  pushed onto the tail of the Rx queue.
 **/
static u16 fm10k_mbx_pushed_tail_len(struct fm10k_mbx_info *mbx)
{
	u32 *tail = mbx->rx.buffer + fm10k_fifo_tail_offset(&mbx->rx, 0);

	/* pushed tail is only valid if pushed is set */
	if (!mbx->pushed)
		return 0;

	return FM10K_TLV_DWORD_LEN(*tail);
}

/**
 *  fm10k_fifo_write_copy - pulls data off of msg and places it in fifo
 *  @fifo: pointer to FIFO
 *  @msg: message array to populate
 *  @tail_offset: additional offset to add to tail pointer
 *  @len: length of FIFO to copy into message header
 *
 *  This function will take a message and copy it into a section of the
 *  FIFO.  In order to get something into a location other than just
 *  the tail you can use tail_offset to adjust the pointer.
 **/
static void fm10k_fifo_write_copy(struct fm10k_mbx_fifo *fifo,
				  const u32 *msg, u16 tail_offset, u16 len)
{
	u16 end = fm10k_fifo_tail_offset(fifo, tail_offset);
	u32 *tail = fifo->buffer + end;

	/* track when we should cross the end of the FIFO */
	end = fifo->size - end;

	/* copy end of message before start of message */
	if (end < len)
		memcpy(fifo->buffer, msg + end, (len - end) << 2);
	else
		end = len;

	/* Copy remaining message into Tx FIFO */
	memcpy(tail, msg, end << 2);
}

/**
 *  fm10k_fifo_enqueue - Enqueues the message to the tail of the FIFO
 *  @fifo: pointer to FIFO
 *  @msg: message array to read
 *
 *  This function enqueues a message up to the size specified by the length
 *  contained in the first DWORD of the message and will place at the tail
 *  of the FIFO.  It will return 0 on success, or a negative value on error.
 **/
static s32 fm10k_fifo_enqueue(struct fm10k_mbx_fifo *fifo, const u32 *msg)
{
	u16 len = FM10K_TLV_DWORD_LEN(*msg);

	/* verify parameters */
	if (len > fifo->size)
		return FM10K_MBX_ERR_SIZE;

	/* verify there is room for the message */
	if (len > fm10k_fifo_unused(fifo))
		return FM10K_MBX_ERR_NO_SPACE;

	/* Copy message into FIFO */
	fm10k_fifo_write_copy(fifo, msg, 0, len);

	/* memory barrier to guarantee FIFO is written before tail update */
	wmb();

	/* Update Tx FIFO tail */
	fifo->tail += len;

	return 0;
}

/**
 *  fm10k_mbx_validate_msg_size - Validate incoming message based on size
 *  @mbx: pointer to mailbox
 *  @len: length of data pushed onto buffer
 *
 *  This function analyzes the frame and will return a non-zero value when
 *  the start of a message larger than the mailbox is detected.
 **/
static u16 fm10k_mbx_validate_msg_size(struct fm10k_mbx_info *mbx, u16 len)
{
	struct fm10k_mbx_fifo *fifo = &mbx->rx;
	u16 total_len = 0, msg_len;
	u32 *msg;

	/* length should include previous amounts pushed */
	len += mbx->pushed;

	/* offset in message is based off of current message size */
	do {
		msg = fifo->buffer + fm10k_fifo_tail_offset(fifo, total_len);
		msg_len = FM10K_TLV_DWORD_LEN(*msg);
		total_len += msg_len;
	} while (total_len < len);

	/* message extends out of pushed section, but fits in FIFO */
	if ((len < total_len) && (msg_len <= mbx->rx.size))
		return 0;

	/* return length of invalid section */
	return (len < total_len) ? len : (len - total_len);
}

/**
 *  fm10k_mbx_write_copy - pulls data off of Tx FIFO and places it in mbmem
 *  @mbx: pointer to mailbox
 *
 *  This function will take a seciton of the Rx FIFO and copy it into the
		mbx->tail--;
 *  mailbox memory.  The offset in mbmem is based on the lower bits of the
 *  tail and len determines the length to copy.
 **/
static void fm10k_mbx_write_copy(struct fm10k_hw *hw,
				 struct fm10k_mbx_info *mbx)
{
	struct fm10k_mbx_fifo *fifo = &mbx->tx;
	u32 mbmem = mbx->mbmem_reg;
	u32 *head = fifo->buffer;
	u16 end, len, tail, mask;

	if (!mbx->tail_len)
		return;

	/* determine data length and mbmem tail index */
	mask = mbx->mbmem_len - 1;
	len = mbx->tail_len;
	tail = fm10k_mbx_tail_sub(mbx, len);
	if (tail > mask)
		tail++;

	/* determine offset in the ring */
	end = fm10k_fifo_head_offset(fifo, mbx->pulled);
	head += end;

	/* memory barrier to guarantee data is ready to be read */
	rmb();

	/* Copy message from Tx FIFO */
	for (end = fifo->size - end; len; head = fifo->buffer) {
		do {
			/* adjust tail to match offset for FIFO */
			tail &= mask;
			if (!tail)
				tail++;

			/* write message to hardware FIFO */
			fm10k_write_reg(hw, mbmem + tail++, *(head++));
		} while (--len && --end);
	}
}

/**
 *  fm10k_mbx_pull_head - Pulls data off of head of Tx FIFO
 *  @hw: pointer to hardware structure
 *  @mbx: pointer to mailbox
 *  @head: acknowledgement number last received
 *
 *  This function will push the tail index forward based on the remote
 *  head index.  It will then pull up to mbmem_len DWORDs off of the
 *  head of the FIFO and will place it in the MBMEM registers
 *  associated with the mailbox.
 **/
static void fm10k_mbx_pull_head(struct fm10k_hw *hw,
				struct fm10k_mbx_info *mbx, u16 head)
{
	u16 mbmem_len, len, ack = fm10k_mbx_index_len(mbx, head, mbx->tail);
	struct fm10k_mbx_fifo *fifo = &mbx->tx;

	/* update number of bytes pulled and update bytes in transit */
	mbx->pulled += mbx->tail_len - ack;

	/* determine length of data to pull, reserve space for mbmem header */
	mbmem_len = mbx->mbmem_len - 1;
	len = fm10k_fifo_used(fifo) - mbx->pulled;
	if (len > mbmem_len)
		len = mbmem_len;

	/* update tail and record number of bytes in transit */
	mbx->tail = fm10k_mbx_tail_add(mbx, len - ack);
	mbx->tail_len = len;

	/* drop pulled messages from the FIFO */
	for (len = fm10k_fifo_head_len(fifo);
	     len && (mbx->pulled >= len);
	     len = fm10k_fifo_head_len(fifo)) {
		mbx->pulled -= fm10k_fifo_head_drop(fifo);
		mbx->tx_messages++;
		mbx->tx_dwords += len;
	}

	/* Copy message out from the Tx FIFO */
	fm10k_mbx_write_copy(hw, mbx);
}

/**
 *  fm10k_mbx_read_copy - pulls data off of mbmem and places it in Rx FIFO
 *  @hw: pointer to hardware structure
 *  @mbx: pointer to mailbox
 *
 *  This function will take a seciton of the mailbox memory and copy it
 *  into the Rx FIFO.  The offset is based on the lower bits of the
 *  head and len determines the length to copy.
 **/
static void fm10k_mbx_read_copy(struct fm10k_hw *hw,
				struct fm10k_mbx_info *mbx)
{
	struct fm10k_mbx_fifo *fifo = &mbx->rx;
	u32 mbmem = mbx->mbmem_reg ^ mbx->mbmem_len;
	u32 *tail = fifo->buffer;
	u16 end, len, head;

	/* determine data length and mbmem head index */
	len = mbx->head_len;
	head = fm10k_mbx_head_sub(mbx, len);
	if (head >= mbx->mbmem_len)
		head++;

	/* determine offset in the ring */
	end = fm10k_fifo_tail_offset(fifo, mbx->pushed);
	tail += end;

	/* Copy message into Rx FIFO */
	for (end = fifo->size - end; len; tail = fifo->buffer) {
		do {
			/* adjust head to match offset for FIFO */
			head &= mbx->mbmem_len - 1;
			if (!head)
				head++;

			/* read message from hardware FIFO */
			*(tail++) = fm10k_read_reg(hw, mbmem + head++);
		} while (--len && --end);
	}

	/* memory barrier to guarantee FIFO is written before tail update */
	wmb();
}

/**
 *  fm10k_mbx_push_tail - Pushes up to 15 DWORDs on to tail of FIFO
 *  @hw: pointer to hardware structure
 *  @mbx: pointer to mailbox
 *  @tail: tail index of message
 *
 *  This function will first validate the tail index and size for the
 *  incoming message.  It then updates the acknowlegment number and
 *  copies the data into the FIFO.  It will return the number of messages
 *  dequeued on success and a negative value on error.
 **/
static s32 fm10k_mbx_push_tail(struct fm10k_hw *hw,
			       struct fm10k_mbx_info *mbx,
			       u16 tail)
{
	struct fm10k_mbx_fifo *fifo = &mbx->rx;
	u16 len, seq = fm10k_mbx_index_len(mbx, mbx->head, tail);

	/* determine length of data to push */
	len = fm10k_fifo_unused(fifo) - mbx->pushed;
	if (len > seq)
		len = seq;

	/* update head and record bytes received */
	mbx->head = fm10k_mbx_head_add(mbx, len);
	mbx->head_len = len;

	/* nothing to do if there is no data */
	if (!len)
		return 0;

	/* Copy msg into Rx FIFO */
	fm10k_mbx_read_copy(hw, mbx);

	/* determine if there are any invalid lengths in message */
	if (fm10k_mbx_validate_msg_size(mbx, len))
		return FM10K_MBX_ERR_SIZE;

	/* Update pushed */
	mbx->pushed += len;

	/* flush any completed messages */
	for (len = fm10k_mbx_pushed_tail_len(mbx);
	     len && (mbx->pushed >= len);
	     len = fm10k_mbx_pushed_tail_len(mbx)) {
		fifo->tail += len;
		mbx->pushed -= len;
		mbx->rx_messages++;
		mbx->rx_dwords += len;
	}

	return 0;
}

/**
 *  fm10k_mbx_rx_ready - Indicates that a message is ready in the Rx FIFO
 *  @mbx: pointer to mailbox
 *
 *  This function returns true if there is a message in the Rx FIFO to dequeue.
 **/
static bool fm10k_mbx_rx_ready(struct fm10k_mbx_info *mbx)
{
	u16 msg_size = fm10k_fifo_head_len(&mbx->rx);

	return msg_size && (fm10k_fifo_used(&mbx->rx) >= msg_size);
}

/**
 *  fm10k_mbx_tx_ready - Indicates that the mailbox is in state ready for Tx
 *  @mbx: pointer to mailbox
 *  @len: verify free space is >= this value
 *
 *  This function returns true if the mailbox is in a state ready to transmit.
 **/
static bool fm10k_mbx_tx_ready(struct fm10k_mbx_info *mbx, u16 len)
{
	u16 fifo_unused = fm10k_fifo_unused(&mbx->tx);

	return (mbx->state == FM10K_STATE_OPEN) && (fifo_unused >= len);
}

/**
 *  fm10k_mbx_tx_complete - Indicates that the Tx FIFO has been emptied
 *  @mbx: pointer to mailbox
 *
 *  This function returns true if the Tx FIFO is empty.
 **/
static bool fm10k_mbx_tx_complete(struct fm10k_mbx_info *mbx)
{
	return fm10k_fifo_empty(&mbx->tx);
}

/**
 *  fm10k_mbx_deqeueue_rx - Dequeues the message from the head in the Rx FIFO
 *  @hw: pointer to hardware structure
 *  @mbx: pointer to mailbox
 *
 *  This function dequeues messages and hands them off to the tlv parser.
 *  It will return the number of messages processed when called.
 **/
static u16 fm10k_mbx_dequeue_rx(struct fm10k_hw *hw,
				struct fm10k_mbx_info *mbx)
{
	struct fm10k_mbx_fifo *fifo = &mbx->rx;
	s32 err;
	u16 cnt;

	/* parse Rx messages out of the Rx FIFO to empty it */
	for (cnt = 0; !fm10k_fifo_empty(fifo); cnt++) {
		err = fm10k_tlv_msg_parse(hw, fifo->buffer + fifo->head,
					  mbx, mbx->msg_data);
		if (err < 0)
			mbx->rx_parse_err++;

		fm10k_fifo_head_drop(fifo);
	}

	/* shift remaining bytes back to start of FIFO */
	memmove(fifo->buffer, fifo->buffer + fifo->tail, mbx->pushed << 2);

	/* shift head and tail based on the memory we moved */
	fifo->tail -= fifo->head;
	fifo->head = 0;

	return cnt;
}

/**
 *  fm10k_mbx_enqueue_tx - Enqueues the message to the tail of the Tx FIFO
 *  @hw: pointer to hardware structure
 *  @mbx: pointer to mailbox
 *  @msg: message array to read
 *
 *  This function enqueues a message up to the size specified by the length
 *  contained in the first DWORD of the message and will place at the tail
 *  of the FIFO.  It will return 0 on success, or a negative value on error.
 **/
static s32 fm10k_mbx_enqueue_tx(struct fm10k_hw *hw,
				struct fm10k_mbx_info *mbx, const u32 *msg)
{
	u32 countdown = mbx->timeout;
	s32 err;

	switch (mbx->state) {
	case FM10K_STATE_CLOSED:
	case FM10K_STATE_DISCONNECT:
		return FM10K_MBX_ERR_NO_MBX;
	default:
		break;
	}

	/* enqueue the message on the Tx FIFO */
	err = fm10k_fifo_enqueue(&mbx->tx, msg);

	/* if it failed give the FIFO a chance to drain */
	while (err && countdown) {
		countdown--;
		udelay(mbx->udelay);
		mbx->ops.process(hw, mbx);
		err = fm10k_fifo_enqueue(&mbx->tx, msg);
	}

	/* if we failed trhead the error */
	if (err) {
		mbx->timeout = 0;
		mbx->tx_busy++;
	}

	/* begin processing message, ignore errors as this is just meant
	 * to start the mailbox flow so we are not concerned if there
	 * is a bad error, or the mailbox is already busy with a request
	 */
	if (!mbx->tail_len)
		mbx->ops.process(hw, mbx);

	return 0;
}

/**
 *  fm10k_mbx_read - Copies the mbmem to local message buffer
 *  @hw: pointer to hardware structure
 *  @mbx: pointer to mailbox
 *
 *  This function copies the message from the mbmem to the message array
 **/
static s32 fm10k_mbx_read(struct fm10k_hw *hw, struct fm10k_mbx_info *mbx)
{
	/* only allow one reader in here at a time */
	if (mbx->mbx_hdr)
		return FM10K_MBX_ERR_BUSY;

	/* read to capture initial interrupt bits */
	if (fm10k_read_reg(hw, mbx->mbx_reg) & FM10K_MBX_REQ_INTERRUPT)
		mbx->mbx_lock = FM10K_MBX_ACK;

	/* write back interrupt bits to clear */
	fm10k_write_reg(hw, mbx->mbx_reg,
			FM10K_MBX_REQ_INTERRUPT | FM10K_MBX_ACK_INTERRUPT);

	/* read remote header */
	mbx->mbx_hdr = fm10k_read_reg(hw, mbx->mbmem_reg ^ mbx->mbmem_len);

	return 0;
}

/**
 *  fm10k_mbx_write - Copies the local message buffer to mbmem
 *  @hw: pointer to hardware structure
 *  @mbx: pointer to mailbox
 *
 *  This function copies the message from the the message array to mbmem
 **/
static void fm10k_mbx_write(struct fm10k_hw *hw, struct fm10k_mbx_info *mbx)
{
	u32 mbmem = mbx->mbmem_reg;

	/* write new msg header to notify recepient of change */
	fm10k_write_reg(hw, mbmem, mbx->mbx_hdr);

	/* write mailbox to sent interrupt */
	if (mbx->mbx_lock)
		fm10k_write_reg(hw, mbx->mbx_reg, mbx->mbx_lock);

	/* we no longer are using the header so free it */
	mbx->mbx_hdr = 0;
	mbx->mbx_lock = 0;
}

/**
 *  fm10k_mbx_reset_work- Reset internal pointers for any pending work
 *  @mbx: pointer to mailbox
 *
 *  This function will reset all internal pointers so any work in progress
 *  is dropped.  This call should occur every time we transition from the
 *  open state to the connect state.
 **/
static void fm10k_mbx_reset_work(struct fm10k_mbx_info *mbx)
{
	/* reset our outgoing max size back to Rx limits */
	mbx->max_size = mbx->rx.size - 1;

	/* just do a quick resysnc to start of message */
	mbx->pushed = 0;
	mbx->pulled = 0;
	mbx->tail_len = 0;
	mbx->head_len = 0;
	mbx->rx.tail = 0;
	mbx->rx.head = 0;
}

/**
 *  fm10k_mbx_update_max_size - Update the max_size and drop any large messages
 *  @mbx: pointer to mailbox
 *  @size: new value for max_size
 *
 *  This function will update the max_size value and drop any outgoing messages
 *  from the head of the Tx FIFO that are larger than max_size.
 **/
static void fm10k_mbx_update_max_size(struct fm10k_mbx_info *mbx, u16 size)
{
	u16 len;

	mbx->max_size = size;

	/* flush any oversized messages from the queue */
	for (len = fm10k_fifo_head_len(&mbx->tx);
	     len > size;
	     len = fm10k_fifo_head_len(&mbx->tx)) {
		fm10k_fifo_head_drop(&mbx->tx);
		mbx->tx_dropped++;
	}
}

/**
 *  fm10k_mbx_validate_handlers - Validate layout of message parsing data
 *  @msg_data: handlers for mailbox events
 *
 *  This function validates the layout of the message parsing data.  This
 *  should be mostly static, but it is important to catch any errors that
 *  are made when constructing the parsers.
 **/
static s32 fm10k_mbx_validate_handlers(const struct fm10k_msg_data *msg_data)
{
	const struct fm10k_tlv_attr *attr;
	unsigned int id;

	/* Allow NULL mailboxes that transmit but don't receive */
	if (!msg_data)
		return 0;

	while (msg_data->id != FM10K_TLV_ERROR) {
		/* all messages should have a function handler */
		if (!msg_data->func)
			return FM10K_ERR_PARAM;

		/* parser is optional */
		attr = msg_data->attr;
		if (attr) {
			while (attr->id != FM10K_TLV_ERROR) {
				id = attr->id;
				attr++;
				/* ID should always be increasing */
				if (id >= attr->id)
					return FM10K_ERR_PARAM;
				/* ID should fit in results array */
				if (id >= FM10K_TLV_RESULTS_MAX)
					return FM10K_ERR_PARAM;
			}

			/* verify terminator is in the list */
			if (attr->id != FM10K_TLV_ERROR)
				return FM10K_ERR_PARAM;
		}

		id = msg_data->id;
		msg_data++;
		/* ID should always be increasing */
		if (id >= msg_data->id)
			return FM10K_ERR_PARAM;
	}

	/* verify terminator is in the list */
	if ((msg_data->id != FM10K_TLV_ERROR) || !msg_data->func)
		return FM10K_ERR_PARAM;

	return 0;
}

/**
 *  fm10k_mbx_register_handlers - Register a set of handler ops for mailbox
 *  @mbx: pointer to mailbox
 *  @msg_data: handlers for mailbox events
 *
 *  This function associates a set of message handling ops with a mailbox.
 **/
static s32 fm10k_mbx_register_handlers(struct fm10k_mbx_info *mbx,
				       const struct fm10k_msg_data *msg_data)
{
	/* validate layout of handlers before assigning them */
	if (fm10k_mbx_validate_handlers(msg_data))
		return FM10K_ERR_PARAM;

	/* initialize the message handlers */
	mbx->msg_data = msg_data;

	return 0;
}

/**
 *  fm10k_sm_mbx_create_data_hdr - Generate a mailbox header for local FIFO
 *  @mbx: pointer to mailbox
 *
 *  This function returns a connection mailbox header
 **/
static void fm10k_sm_mbx_create_data_hdr(struct fm10k_mbx_info *mbx)
{
	if (mbx->tail_len)
		mbx->mbx_lock |= FM10K_MBX_REQ;

	mbx->mbx_hdr = FM10K_MSG_HDR_FIELD_SET(mbx->tail, SM_TAIL) |
		       FM10K_MSG_HDR_FIELD_SET(mbx->remote, SM_VER) |
		       FM10K_MSG_HDR_FIELD_SET(mbx->head, SM_HEAD);
}

/**
 *  fm10k_sm_mbx_create_connect_hdr - Generate a mailbox header for local FIFO
 *  @mbx: pointer to mailbox
 *  @err: error flags to report if any
 *
 *  This function returns a connection mailbox header
 **/
static void fm10k_sm_mbx_create_connect_hdr(struct fm10k_mbx_info *mbx, u8 err)
{
	if (mbx->local)
		mbx->mbx_lock |= FM10K_MBX_REQ;

	mbx->mbx_hdr = FM10K_MSG_HDR_FIELD_SET(mbx->tail, SM_TAIL) |
		       FM10K_MSG_HDR_FIELD_SET(mbx->remote, SM_VER) |
		       FM10K_MSG_HDR_FIELD_SET(mbx->head, SM_HEAD) |
		       FM10K_MSG_HDR_FIELD_SET(err, SM_ERR);
}

/**
 *  fm10k_sm_mbx_connect_reset - Reset following request for reset
 *  @mbx: pointer to mailbox
 *
 *  This function resets the mailbox to a just connected state
 **/
static void fm10k_sm_mbx_connect_reset(struct fm10k_mbx_info *mbx)
{
	/* flush any uncompleted work */
	fm10k_mbx_reset_work(mbx);

	/* set local version to max and remote version to 0 */
	mbx->local = FM10K_SM_MBX_VERSION;
	mbx->remote = 0;

	/* initalize tail and head */
	mbx->tail = 1;
	mbx->head = 1;

	/* reset state back to connect */
	mbx->state = FM10K_STATE_CONNECT;
}

/**
 *  fm10k_sm_mbx_connect - Start switch manager mailbox connection
 *  @hw: pointer to hardware structure
 *  @mbx: pointer to mailbox
 *
 *  This function will initiate a mailbox connection with the switch
 *  manager.  To do this it will first disconnect the mailbox, and then
 *  reconnect it in order to complete a reset of the mailbox.
 *
 *  This function will return an error if the mailbox has not been initiated
 *  or is currently in use.
 **/
static s32 fm10k_sm_mbx_connect(struct fm10k_hw *hw, struct fm10k_mbx_info *mbx)
{
	/* we cannot connect an uninitialized mailbox */
	if (!mbx->rx.buffer)
		return FM10K_MBX_ERR_NO_SPACE;

	/* we cannot connect an already connected mailbox */
	if (mbx->state != FM10K_STATE_CLOSED)
		return FM10K_MBX_ERR_BUSY;

	/* mailbox timeout can now become active */
	mbx->timeout = FM10K_MBX_INIT_TIMEOUT;

	/* Place mbx in ready to connect state */
	mbx->state = FM10K_STATE_CONNECT;
	mbx->max_size = FM10K_MBX_MSG_MAX_SIZE;

	/* reset interface back to connect */
	fm10k_sm_mbx_connect_reset(mbx);

	/* enable interrupt and notify other party of new message */
	mbx->mbx_lock = FM10K_MBX_REQ_INTERRUPT | FM10K_MBX_ACK_INTERRUPT |
			FM10K_MBX_INTERRUPT_ENABLE;

	/* generate and load connect header into mailbox */
	fm10k_sm_mbx_create_connect_hdr(mbx, 0);
	fm10k_mbx_write(hw, mbx);

	/* enable interrupt and notify other party of new message */

	return 0;
}

/**
 *  fm10k_sm_mbx_disconnect - Shutdown mailbox connection
 *  @hw: pointer to hardware structure
 *  @mbx: pointer to mailbox
 *
 *  This function will shut down the mailbox.  It places the mailbox first
 *  in the disconnect state, it then allows up to a predefined timeout for
 *  the mailbox to transition to close on its own.  If this does not occur
 *  then the mailbox will be forced into the closed state.
 *
 *  Any mailbox transactions not completed before calling this function
 *  are not guaranteed to complete and may be dropped.
 **/
static void fm10k_sm_mbx_disconnect(struct fm10k_hw *hw,
				    struct fm10k_mbx_info *mbx)
{
	int timeout = mbx->timeout ? FM10K_MBX_DISCONNECT_TIMEOUT : 0;

	/* Place mbx in ready to disconnect state */
	mbx->state = FM10K_STATE_DISCONNECT;

	/* trigger interrupt to start shutdown process */
	fm10k_write_reg(hw, mbx->mbx_reg, FM10K_MBX_REQ |
					  FM10K_MBX_INTERRUPT_DISABLE);
	do {
		udelay(FM10K_MBX_POLL_DELAY);
		mbx->ops.process(hw, mbx);
		timeout -= FM10K_MBX_POLL_DELAY;
	} while ((timeout > 0) && (mbx->state != FM10K_STATE_CLOSED));

	/* in case we didn't close just force the mailbox into shutdown */
	mbx->state = FM10K_STATE_CLOSED;
	mbx->remote = 0;
	fm10k_mbx_reset_work(mbx);
	fm10k_mbx_update_max_size(mbx, 0);

	fm10k_write_reg(hw, mbx->mbmem_reg, 0);
}

/**
 *  fm10k_mbx_validate_fifo_hdr - Validate fields in the remote FIFO header
 *  @mbx: pointer to mailbox
 *
 *  This function will parse up the fields in the mailbox header and return
 *  an error if the header contains any of a number of invalid configurations
 *  including unrecognized offsets or version numbers.
 **/
static s32 fm10k_sm_mbx_validate_fifo_hdr(struct fm10k_mbx_info *mbx)
{
	const u32 *hdr = &mbx->mbx_hdr;
	u16 tail, head, ver;

	tail = FM10K_MSG_HDR_FIELD_GET(*hdr, SM_TAIL);
	ver = FM10K_MSG_HDR_FIELD_GET(*hdr, SM_VER);
	head = FM10K_MSG_HDR_FIELD_GET(*hdr, SM_HEAD);

	switch (ver) {
	case 0:
		break;
	case FM10K_SM_MBX_VERSION:
		if (!head || head > FM10K_SM_MBX_FIFO_LEN)
			return FM10K_MBX_ERR_HEAD;
		if (!tail || tail > FM10K_SM_MBX_FIFO_LEN)
			return FM10K_MBX_ERR_TAIL;
		if (mbx->tail < head)
			head += mbx->mbmem_len - 1;
		if (tail < mbx->head)
			tail += mbx->mbmem_len - 1;
		if (fm10k_mbx_index_len(mbx, head, mbx->tail) > mbx->tail_len)
			return FM10K_MBX_ERR_HEAD;
		if (fm10k_mbx_index_len(mbx, mbx->head, tail) < mbx->mbmem_len)
			break;
		return FM10K_MBX_ERR_TAIL;
	default:
		return FM10K_MBX_ERR_SRC;
	}

	return 0;
}

/**
 *  fm10k_sm_mbx_process_error - Process header with error flag set
 *  @mbx: pointer to mailbox
 *
 *  This function is meant to respond to a request where the error flag
 *  is set.  As a result we will terminate a connection if one is present
 *  and fall back into the reset state with a connection header of version
 *  0 (RESET).
 **/
static void fm10k_sm_mbx_process_error(struct fm10k_mbx_info *mbx)
{
	const enum fm10k_mbx_state state = mbx->state;

	switch (state) {
	case FM10K_STATE_DISCONNECT:
		/* if there is an error just disconnect */
		mbx->remote = 0;
		break;
	case FM10K_STATE_OPEN:
		/* flush any uncompleted work */
		fm10k_sm_mbx_connect_reset(mbx);
		break;
	case FM10K_STATE_CONNECT:
		/* try connnecting at lower version */
		if (mbx->remote) {
			while (mbx->local > 1)
				mbx->local--;
			mbx->remote = 0;
		}
		break;
	default:
		break;
	}

	fm10k_sm_mbx_create_connect_hdr(mbx, 0);
}

/**
 *  fm10k_sm_mbx_create_error_message - Process an error in FIFO hdr
 *  @mbx: pointer to mailbox
 *  @err: local error encountered
 *
 *  This function will interpret the error provided by err, and based on
 *  that it may set the error bit in the local message header
 **/
static void fm10k_sm_mbx_create_error_msg(struct fm10k_mbx_info *mbx, s32 err)
{
	/* only generate an error message for these types */
	switch (err) {
	case FM10K_MBX_ERR_TAIL:
	case FM10K_MBX_ERR_HEAD:
	case FM10K_MBX_ERR_SRC:
	case FM10K_MBX_ERR_SIZE:
	case FM10K_MBX_ERR_RSVD0:
		break;
	default:
		return;
	}

	/* process it as though we received an error, and send error reply */
	fm10k_sm_mbx_process_error(mbx);
	fm10k_sm_mbx_create_connect_hdr(mbx, 1);
}

/**
 *  fm10k_sm_mbx_receive - Take message from Rx mailbox FIFO and put it in Rx
 *  @hw: pointer to hardware structure
 *  @mbx: pointer to mailbox
 *
 *  This function will dequeue one message from the Rx switch manager mailbox
 *  FIFO and place it in the Rx mailbox FIFO for processing by software.
 **/
static s32 fm10k_sm_mbx_receive(struct fm10k_hw *hw,
				struct fm10k_mbx_info *mbx,
				u16 tail)
{
	/* reduce length by 1 to convert to a mask */
	u16 mbmem_len = mbx->mbmem_len - 1;
	s32 err;

	/* push tail in front of head */
	if (tail < mbx->head)
		tail += mbmem_len;

	/* copy data to the Rx FIFO */
	err = fm10k_mbx_push_tail(hw, mbx, tail);
	if (err < 0)
		return err;

	/* process messages if we have received any */
	fm10k_mbx_dequeue_rx(hw, mbx);

	/* guarantee head aligns with the end of the last message */
	mbx->head = fm10k_mbx_head_sub(mbx, mbx->pushed);
	mbx->pushed = 0;

	/* clear any extra bits left over since index adds 1 extra bit */
	if (mbx->head > mbmem_len)
		mbx->head -= mbmem_len;

	return err;
}

/**
 *  fm10k_sm_mbx_transmit - Take message from Tx and put it in Tx mailbox FIFO
 *  @hw: pointer to hardware structure
 *  @mbx: pointer to mailbox
 *
 *  This function will dequeue one message from the Tx mailbox FIFO and place
 *  it in the Tx switch manager mailbox FIFO for processing by hardware.
 **/
static void fm10k_sm_mbx_transmit(struct fm10k_hw *hw,
				  struct fm10k_mbx_info *mbx, u16 head)
{
	struct fm10k_mbx_fifo *fifo = &mbx->tx;
	/* reduce length by 1 to convert to a mask */
	u16 mbmem_len = mbx->mbmem_len - 1;
	u16 tail_len, len = 0;
	u32 *msg;

	/* push head behind tail */
	if (mbx->tail < head)
		head += mbmem_len;

	fm10k_mbx_pull_head(hw, mbx, head);

	/* determine msg aligned offset for end of buffer */
	do {
		msg = fifo->buffer + fm10k_fifo_head_offset(fifo, len);
		tail_len = len;
		len += FM10K_TLV_DWORD_LEN(*msg);
	} while ((len <= mbx->tail_len) && (len < mbmem_len));

	/* guarantee we stop on a message boundary */
	if (mbx->tail_len > tail_len) {
		mbx->tail = fm10k_mbx_tail_sub(mbx, mbx->tail_len - tail_len);
		mbx->tail_len = tail_len;
	}

	/* clear any extra bits left over since index adds 1 extra bit */
	if (mbx->tail > mbmem_len)
		mbx->tail -= mbmem_len;
}

/**
 *  fm10k_sm_mbx_create_reply - Generate reply based on state and remote head
 *  @mbx: pointer to mailbox
 *  @head: acknowledgement number
 *
 *  This function will generate an outgoing message based on the current
 *  mailbox state and the remote fifo head.  It will return the length
 *  of the outgoing message excluding header on success, and a negative value
 *  on error.
 **/
static void fm10k_sm_mbx_create_reply(struct fm10k_hw *hw,
				      struct fm10k_mbx_info *mbx, u16 head)
{
	switch (mbx->state) {
	case FM10K_STATE_OPEN:
	case FM10K_STATE_DISCONNECT:
		/* flush out Tx data */
		fm10k_sm_mbx_transmit(hw, mbx, head);

		/* generate new header based on data */
		if (mbx->tail_len || (mbx->state == FM10K_STATE_OPEN)) {
			fm10k_sm_mbx_create_data_hdr(mbx);
		} else {
			mbx->remote = 0;
			fm10k_sm_mbx_create_connect_hdr(mbx, 0);
		}
		break;
	case FM10K_STATE_CONNECT:
	case FM10K_STATE_CLOSED:
		fm10k_sm_mbx_create_connect_hdr(mbx, 0);
		break;
	default:
		break;
	}
}

/**
 *  fm10k_sm_mbx_process_reset - Process header with version == 0 (RESET)
 *  @hw: pointer to hardware structure
 *  @mbx: pointer to mailbox
 *
 *  This function is meant to respond to a request where the version data
 *  is set to 0.  As such we will either terminate the connection or go
 *  into the connect state in order to re-establish the connection.  This
 *  function can also be used to respond to an error as the connection
 *  resetting would also be a means of dealing with errors.
 **/
static void fm10k_sm_mbx_process_reset(struct fm10k_hw *hw,
				       struct fm10k_mbx_info *mbx)
{
	const enum fm10k_mbx_state state = mbx->state;

	switch (state) {
	case FM10K_STATE_DISCONNECT:
		/* drop remote connections and disconnect */
		mbx->state = FM10K_STATE_CLOSED;
		mbx->remote = 0;
		mbx->local = 0;
		break;
	case FM10K_STATE_OPEN:
		/* flush any incomplete work */
		fm10k_sm_mbx_connect_reset(mbx);
		break;
	case FM10K_STATE_CONNECT:
		/* Update remote value to match local value */
		mbx->remote = mbx->local;
	default:
		break;
	}

	fm10k_sm_mbx_create_reply(hw, mbx, mbx->tail);
}

/**
 *  fm10k_sm_mbx_process_version_1 - Process header with version == 1
 *  @hw: pointer to hardware structure
 *  @mbx: pointer to mailbox
 *
 *  This function is meant to process messages received when the remote
 *  mailbox is active.
 **/
static s32 fm10k_sm_mbx_process_version_1(struct fm10k_hw *hw,
					  struct fm10k_mbx_info *mbx)
{
	const u32 *hdr = &mbx->mbx_hdr;
	u16 head, tail;
	s32 len;

	/* pull all fields needed for verification */
	tail = FM10K_MSG_HDR_FIELD_GET(*hdr, SM_TAIL);
	head = FM10K_MSG_HDR_FIELD_GET(*hdr, SM_HEAD);

	/* if we are in connect and wanting version 1 then start up and go */
	if (mbx->state == FM10K_STATE_CONNECT) {
		if (!mbx->remote)
			goto send_reply;
		if (mbx->remote != 1)
			return FM10K_MBX_ERR_SRC;

		mbx->state = FM10K_STATE_OPEN;
	}

	do {
		/* abort on message size errors */
		len = fm10k_sm_mbx_receive(hw, mbx, tail);
		if (len < 0)
			return len;

		/* continue until we have flushed the Rx FIFO */
	} while (len);

send_reply:
	fm10k_sm_mbx_create_reply(hw, mbx, head);

	return 0;
}

/**
 *  fm10k_sm_mbx_process - Process mailbox switch mailbox interrupt
 *  @hw: pointer to hardware structure
 *  @mbx: pointer to mailbox
 *
 *  This function will process incoming mailbox events and generate mailbox
 *  replies.  It will return a value indicating the number of DWORDs
 *  transmitted excluding header on success or a negative value on error.
 **/
static s32 fm10k_sm_mbx_process(struct fm10k_hw *hw,
				struct fm10k_mbx_info *mbx)
{
	s32 err;

	/* we do not read mailbox if closed */
	if (mbx->state == FM10K_STATE_CLOSED)
		return 0;

	/* retrieve data from switch manager */
	err = fm10k_mbx_read(hw, mbx);
	if (err)
		return err;

	err = fm10k_sm_mbx_validate_fifo_hdr(mbx);
	if (err < 0)
		goto fifo_err;

	if (FM10K_MSG_HDR_FIELD_GET(mbx->mbx_hdr, SM_ERR)) {
		fm10k_sm_mbx_process_error(mbx);
		goto fifo_err;
	}

	switch (FM10K_MSG_HDR_FIELD_GET(mbx->mbx_hdr, SM_VER)) {
	case 0:
		fm10k_sm_mbx_process_reset(hw, mbx);
		break;
	case FM10K_SM_MBX_VERSION:
		err = fm10k_sm_mbx_process_version_1(hw, mbx);
		break;
	}

fifo_err:
	if (err < 0)
		fm10k_sm_mbx_create_error_msg(mbx, err);

	/* report data to switch manager */
	fm10k_mbx_write(hw, mbx);

	return err;
}

/**
 *  fm10k_sm_mbx_init - Initialize mailbox memory for PF/SM mailbox
 *  @hw: pointer to hardware structure
 *  @mbx: pointer to mailbox
 *  @msg_data: handlers for mailbox events
 *
 *  This function for now is used to stub out the PF/SM mailbox
 **/
s32 fm10k_sm_mbx_init(struct fm10k_hw *hw, struct fm10k_mbx_info *mbx,
		      const struct fm10k_msg_data *msg_data)
{
	mbx->mbx_reg = FM10K_GMBX;
	mbx->mbmem_reg = FM10K_MBMEM_PF(0);
	/* start out in closed state */
	mbx->state = FM10K_STATE_CLOSED;

	/* validate layout of handlers before assigning them */
	if (fm10k_mbx_validate_handlers(msg_data))
		return FM10K_ERR_PARAM;

	/* initialize the message handlers */
	mbx->msg_data = msg_data;

	/* start mailbox as timed out and let the reset_hw call
	 * set the timeout value to begin communications
	 */
	mbx->timeout = 0;
	mbx->udelay = FM10K_MBX_INIT_DELAY;

	/* Split buffer for use by Tx/Rx FIFOs */
	mbx->max_size = FM10K_MBX_MSG_MAX_SIZE;
	mbx->mbmem_len = FM10K_MBMEM_PF_XOR;

	/* initialize the FIFOs, sizes are in 4 byte increments */
	fm10k_fifo_init(&mbx->tx, mbx->buffer, FM10K_MBX_TX_BUFFER_SIZE);
	fm10k_fifo_init(&mbx->rx, &mbx->buffer[FM10K_MBX_TX_BUFFER_SIZE],
			FM10K_MBX_RX_BUFFER_SIZE);

	/* initialize function pointers */
	mbx->ops.connect = fm10k_sm_mbx_connect;
	mbx->ops.disconnect = fm10k_sm_mbx_disconnect;
	mbx->ops.rx_ready = fm10k_mbx_rx_ready;
	mbx->ops.tx_ready = fm10k_mbx_tx_ready;
	mbx->ops.tx_complete = fm10k_mbx_tx_complete;
	mbx->ops.enqueue_tx = fm10k_mbx_enqueue_tx;
	mbx->ops.process = fm10k_sm_mbx_process;
	mbx->ops.register_handlers = fm10k_mbx_register_handlers;

	return 0;
}
