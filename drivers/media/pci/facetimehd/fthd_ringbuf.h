/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * FacetimeHD camera driver
 *
 * Copyright (C) 2015 Sven Schnelle <svens@stackframe.org>
 *
 */

#ifndef _FTHD_RINGBUF_H
#define _FTHD_RINGBUF_H

#define FTHD_RINGBUF_ENTRY_SIZE 64

#define FTHD_RINGBUF_ADDRESS_FLAGS 0
#define FTHD_RINGBUF_REQUEST_SIZE 4
#define FTHD_RINGBUF_RESPONSE_SIZE 8

enum ringbuf_type_t {
	RINGBUF_TYPE_H2T=0,
	RINGBUF_TYPE_T2H=1,
	RINGBUF_TYPE_UNIDIRECTIONAL,
};

struct fthd_ringbuf {
	void *doorbell;
	int idx;
};

struct fw_channel;
struct fthd_private;
extern void fthd_channel_ringbuf_dump(struct fthd_private *dev_priv, struct fw_channel *chan);
extern void fthd_channel_ringbuf_init(struct fthd_private *dev_priv, struct fw_channel *chan);
extern u32 fthd_channel_ringbuf_get_entry(struct fthd_private *, struct fw_channel *);
extern int fthd_channel_ringbuf_send(struct fthd_private *dev_priv, struct fw_channel *chan,
				     u32 data_offset, u32 request_size, u32 response_size, u32 *entry);

extern u32 fthd_channel_ringbuf_receive(struct fthd_private *dev_priv,
					struct fw_channel *chan);

extern int fthd_channel_wait_ready(struct fthd_private *dev_priv, struct fw_channel *chan, u32 entry, int timeout);
extern u32 get_entry_addr(struct fthd_private *dev_priv,
			  struct fw_channel *chan, int num);
#endif
