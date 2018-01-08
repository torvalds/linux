/*
 * Intel MIC Platform Software Stack (MPSS)
 *
 * Copyright(c) 2014 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * Intel SCIF driver.
 *
 */
#include <linux/circ_buf.h>
#include <linux/types.h>
#include <linux/io.h>
#include <linux/errno.h>

#include "scif_rb.h"

#define scif_rb_ring_cnt(head, tail, size) CIRC_CNT(head, tail, size)
#define scif_rb_ring_space(head, tail, size) CIRC_SPACE(head, tail, size)

/**
 * scif_rb_init - Initializes the ring buffer
 * @rb: ring buffer
 * @read_ptr: A pointer to the read offset
 * @write_ptr: A pointer to the write offset
 * @rb_base: A pointer to the base of the ring buffer
 * @size: The size of the ring buffer in powers of two
 */
void scif_rb_init(struct scif_rb *rb, u32 *read_ptr, u32 *write_ptr,
		  void *rb_base, u8 size)
{
	rb->rb_base = rb_base;
	rb->size = (1 << size);
	rb->read_ptr = read_ptr;
	rb->write_ptr = write_ptr;
	rb->current_read_offset = *read_ptr;
	rb->current_write_offset = *write_ptr;
}

/* Copies a message to the ring buffer -- handles the wrap around case */
static void memcpy_torb(struct scif_rb *rb, void *header,
			void *msg, u32 size)
{
	u32 size1, size2;

	if (header + size >= rb->rb_base + rb->size) {
		/* Need to call two copies if it wraps around */
		size1 = (u32)(rb->rb_base + rb->size - header);
		size2 = size - size1;
		memcpy_toio((void __iomem __force *)header, msg, size1);
		memcpy_toio((void __iomem __force *)rb->rb_base,
			    msg + size1, size2);
	} else {
		memcpy_toio((void __iomem __force *)header, msg, size);
	}
}

/* Copies a message from the ring buffer -- handles the wrap around case */
static void memcpy_fromrb(struct scif_rb *rb, void *header,
			  void *msg, u32 size)
{
	u32 size1, size2;

	if (header + size >= rb->rb_base + rb->size) {
		/* Need to call two copies if it wraps around */
		size1 = (u32)(rb->rb_base + rb->size - header);
		size2 = size - size1;
		memcpy_fromio(msg, (void __iomem __force *)header, size1);
		memcpy_fromio(msg + size1,
			      (void __iomem __force *)rb->rb_base, size2);
	} else {
		memcpy_fromio(msg, (void __iomem __force *)header, size);
	}
}

/**
 * scif_rb_space - Query space available for writing to the RB
 * @rb: ring buffer
 *
 * Return: size available for writing to RB in bytes.
 */
u32 scif_rb_space(struct scif_rb *rb)
{
	rb->current_read_offset = *rb->read_ptr;
	/*
	 * Update from the HW read pointer only once the peer has exposed the
	 * new empty slot. This barrier is paired with the memory barrier
	 * scif_rb_update_read_ptr()
	 */
	mb();
	return scif_rb_ring_space(rb->current_write_offset,
				  rb->current_read_offset, rb->size);
}

/**
 * scif_rb_write - Write a message to the RB
 * @rb: ring buffer
 * @msg: buffer to send the message.  Must be at least size bytes long
 * @size: the size (in bytes) to be copied to the RB
 *
 * This API does not block if there isn't enough space in the RB.
 * Returns: 0 on success or -ENOMEM on failure
 */
int scif_rb_write(struct scif_rb *rb, void *msg, u32 size)
{
	void *header;

	if (scif_rb_space(rb) < size)
		return -ENOMEM;
	header = rb->rb_base + rb->current_write_offset;
	memcpy_torb(rb, header, msg, size);
	/*
	 * Wait until scif_rb_commit(). Update the local ring
	 * buffer data, not the shared data until commit.
	 */
	rb->current_write_offset =
		(rb->current_write_offset + size) & (rb->size - 1);
	return 0;
}

/**
 * scif_rb_commit - To submit the message to let the peer fetch it
 * @rb: ring buffer
 */
void scif_rb_commit(struct scif_rb *rb)
{
	/*
	 * We must ensure ordering between the all the data committed
	 * previously before we expose the new message to the peer by
	 * updating the write_ptr. This write barrier is paired with
	 * the read barrier in scif_rb_count(..)
	 */
	wmb();
	WRITE_ONCE(*rb->write_ptr, rb->current_write_offset);
#ifdef CONFIG_INTEL_MIC_CARD
	/*
	 * X100 Si bug: For the case where a Core is performing an EXT_WR
	 * followed by a Doorbell Write, the Core must perform two EXT_WR to the
	 * same address with the same data before it does the Doorbell Write.
	 * This way, if ordering is violated for the Interrupt Message, it will
	 * fall just behind the first Posted associated with the first EXT_WR.
	 */
	WRITE_ONCE(*rb->write_ptr, rb->current_write_offset);
#endif
}

/**
 * scif_rb_get - To get next message from the ring buffer
 * @rb: ring buffer
 * @size: Number of bytes to be read
 *
 * Return: NULL if no bytes to be read from the ring buffer, otherwise the
 *	pointer to the next byte
 */
static void *scif_rb_get(struct scif_rb *rb, u32 size)
{
	void *header = NULL;

	if (scif_rb_count(rb, size) >= size)
		header = rb->rb_base + rb->current_read_offset;
	return header;
}

/*
 * scif_rb_get_next - Read from ring buffer.
 * @rb: ring buffer
 * @msg: buffer to hold the message.  Must be at least size bytes long
 * @size: Number of bytes to be read
 *
 * Return: number of bytes read if available bytes are >= size, otherwise
 * returns zero.
 */
u32 scif_rb_get_next(struct scif_rb *rb, void *msg, u32 size)
{
	void *header = NULL;
	int read_size = 0;

	header = scif_rb_get(rb, size);
	if (header) {
		u32 next_cmd_offset =
			(rb->current_read_offset + size) & (rb->size - 1);

		read_size = size;
		rb->current_read_offset = next_cmd_offset;
		memcpy_fromrb(rb, header, msg, size);
	}
	return read_size;
}

/**
 * scif_rb_update_read_ptr
 * @rb: ring buffer
 */
void scif_rb_update_read_ptr(struct scif_rb *rb)
{
	u32 new_offset;

	new_offset = rb->current_read_offset;
	/*
	 * We must ensure ordering between the all the data committed or read
	 * previously before we expose the empty slot to the peer by updating
	 * the read_ptr. This barrier is paired with the memory barrier in
	 * scif_rb_space(..)
	 */
	mb();
	WRITE_ONCE(*rb->read_ptr, new_offset);
#ifdef CONFIG_INTEL_MIC_CARD
	/*
	 * X100 Si Bug: For the case where a Core is performing an EXT_WR
	 * followed by a Doorbell Write, the Core must perform two EXT_WR to the
	 * same address with the same data before it does the Doorbell Write.
	 * This way, if ordering is violated for the Interrupt Message, it will
	 * fall just behind the first Posted associated with the first EXT_WR.
	 */
	WRITE_ONCE(*rb->read_ptr, new_offset);
#endif
}

/**
 * scif_rb_count
 * @rb: ring buffer
 * @size: Number of bytes expected to be read
 *
 * Return: number of bytes that can be read from the RB
 */
u32 scif_rb_count(struct scif_rb *rb, u32 size)
{
	if (scif_rb_ring_cnt(rb->current_write_offset,
			     rb->current_read_offset,
			     rb->size) < size) {
		rb->current_write_offset = *rb->write_ptr;
		/*
		 * Update from the HW write pointer if empty only once the peer
		 * has exposed the new message. This read barrier is paired
		 * with the write barrier in scif_rb_commit(..)
		 */
		smp_rmb();
	}
	return scif_rb_ring_cnt(rb->current_write_offset,
				rb->current_read_offset,
				rb->size);
}
