/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * SSH message parser.
 *
 * Copyright (C) 2019-2020 Maximilian Luz <luzmaximilian@gmail.com>
 */

#ifndef _SURFACE_AGGREGATOR_SSH_PARSER_H
#define _SURFACE_AGGREGATOR_SSH_PARSER_H

#include <linux/device.h>
#include <linux/kfifo.h>
#include <linux/slab.h>
#include <linux/types.h>

#include <linux/surface_aggregator/serial_hub.h>

/**
 * struct sshp_buf - Parser buffer for SSH messages.
 * @ptr: Pointer to the beginning of the buffer.
 * @len: Number of bytes used in the buffer.
 * @cap: Maximum capacity of the buffer.
 */
struct sshp_buf {
	u8    *ptr;
	size_t len;
	size_t cap;
};

/**
 * sshp_buf_init() - Initialize a SSH parser buffer.
 * @buf: The buffer to initialize.
 * @ptr: The memory backing the buffer.
 * @cap: The length of the memory backing the buffer, i.e. its capacity.
 *
 * Initializes the buffer with the given memory as backing and set its used
 * length to zero.
 */
static inline void sshp_buf_init(struct sshp_buf *buf, u8 *ptr, size_t cap)
{
	buf->ptr = ptr;
	buf->len = 0;
	buf->cap = cap;
}

/**
 * sshp_buf_alloc() - Allocate and initialize a SSH parser buffer.
 * @buf:   The buffer to initialize/allocate to.
 * @cap:   The desired capacity of the buffer.
 * @flags: The flags used for allocating the memory.
 *
 * Allocates @cap bytes and initializes the provided buffer struct with the
 * allocated memory.
 *
 * Return: Returns zero on success and %-ENOMEM if allocation failed.
 */
static inline int sshp_buf_alloc(struct sshp_buf *buf, size_t cap, gfp_t flags)
{
	u8 *ptr;

	ptr = kzalloc(cap, flags);
	if (!ptr)
		return -ENOMEM;

	sshp_buf_init(buf, ptr, cap);
	return 0;
}

/**
 * sshp_buf_free() - Free a SSH parser buffer.
 * @buf: The buffer to free.
 *
 * Frees a SSH parser buffer by freeing the memory backing it and then
 * resetting its pointer to %NULL and length and capacity to zero. Intended to
 * free a buffer previously allocated with sshp_buf_alloc().
 */
static inline void sshp_buf_free(struct sshp_buf *buf)
{
	kfree(buf->ptr);
	buf->ptr = NULL;
	buf->len = 0;
	buf->cap = 0;
}

/**
 * sshp_buf_drop() - Drop data from the beginning of the buffer.
 * @buf: The buffer to drop data from.
 * @n:   The number of bytes to drop.
 *
 * Drops the first @n bytes from the buffer. Re-aligns any remaining data to
 * the beginning of the buffer.
 */
static inline void sshp_buf_drop(struct sshp_buf *buf, size_t n)
{
	memmove(buf->ptr, buf->ptr + n, buf->len - n);
	buf->len -= n;
}

/**
 * sshp_buf_read_from_fifo() - Transfer data from a fifo to the buffer.
 * @buf:  The buffer to write the data into.
 * @fifo: The fifo to read the data from.
 *
 * Transfers the data contained in the fifo to the buffer, removing it from
 * the fifo. This function will try to transfer as much data as possible,
 * limited either by the remaining space in the buffer or by the number of
 * bytes available in the fifo.
 *
 * Return: Returns the number of bytes transferred.
 */
static inline size_t sshp_buf_read_from_fifo(struct sshp_buf *buf,
					     struct kfifo *fifo)
{
	size_t n;

	n =  kfifo_out(fifo, buf->ptr + buf->len, buf->cap - buf->len);
	buf->len += n;

	return n;
}

/**
 * sshp_buf_span_from() - Initialize a span from the given buffer and offset.
 * @buf:    The buffer to create the span from.
 * @offset: The offset in the buffer at which the span should start.
 * @span:   The span to initialize (output).
 *
 * Initializes the provided span to point to the memory at the given offset in
 * the buffer, with the length of the span being capped by the number of bytes
 * used in the buffer after the offset (i.e. bytes remaining after the
 * offset).
 *
 * Warning: This function does not validate that @offset is less than or equal
 * to the number of bytes used in the buffer or the buffer capacity. This must
 * be guaranteed by the caller.
 */
static inline void sshp_buf_span_from(struct sshp_buf *buf, size_t offset,
				      struct ssam_span *span)
{
	span->ptr = buf->ptr + offset;
	span->len = buf->len - offset;
}

bool sshp_find_syn(const struct ssam_span *src, struct ssam_span *rem);

int sshp_parse_frame(const struct device *dev, const struct ssam_span *source,
		     struct ssh_frame **frame, struct ssam_span *payload,
		     size_t maxlen);

int sshp_parse_command(const struct device *dev, const struct ssam_span *source,
		       struct ssh_command **command,
		       struct ssam_span *command_data);

#endif /* _SURFACE_AGGREGATOR_SSH_PARSER_h */
