#ifndef _LIB_RING_BUFFER_BACKEND_TYPES_H
#define _LIB_RING_BUFFER_BACKEND_TYPES_H

/*
 * lib/ringbuffer/backend_types.h
 *
 * Ring buffer backend (types).
 *
 * Copyright (C) 2008-2012 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; only
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <linux/cpumask.h>
#include <linux/types.h>

struct lib_ring_buffer_backend_page {
	void *virt;			/* page virtual address (cached) */
	struct page *page;		/* pointer to page structure */
};

struct lib_ring_buffer_backend_pages {
	unsigned long mmap_offset;	/* offset of the subbuffer in mmap */
	union v_atomic records_commit;	/* current records committed count */
	union v_atomic records_unread;	/* records to read */
	unsigned long data_size;	/* Amount of data to read from subbuf */
	struct lib_ring_buffer_backend_page p[];
};

struct lib_ring_buffer_backend_subbuffer {
	/* Identifier for subbuf backend pages. Exchanged atomically. */
	unsigned long id;		/* backend subbuffer identifier */
};

/*
 * Forward declaration of frontend-specific channel and ring_buffer.
 */
struct channel;
struct lib_ring_buffer;

struct lib_ring_buffer_backend {
	/* Array of ring_buffer_backend_subbuffer for writer */
	struct lib_ring_buffer_backend_subbuffer *buf_wsb;
	/* ring_buffer_backend_subbuffer for reader */
	struct lib_ring_buffer_backend_subbuffer buf_rsb;
	/*
	 * Pointer array of backend pages, for whole buffer.
	 * Indexed by ring_buffer_backend_subbuffer identifier (id) index.
	 */
	struct lib_ring_buffer_backend_pages **array;
	unsigned int num_pages_per_subbuf;

	struct channel *chan;		/* Associated channel */
	int cpu;			/* This buffer's cpu. -1 if global. */
	union v_atomic records_read;	/* Number of records read */
	unsigned int allocated:1;	/* is buffer allocated ? */
};

struct channel_backend {
	unsigned long buf_size;		/* Size of the buffer */
	unsigned long subbuf_size;	/* Sub-buffer size */
	unsigned int subbuf_size_order;	/* Order of sub-buffer size */
	unsigned int num_subbuf_order;	/*
					 * Order of number of sub-buffers/buffer
					 * for writer.
					 */
	unsigned int buf_size_order;	/* Order of buffer size */
	unsigned int extra_reader_sb:1;	/* has extra reader subbuffer ? */
	struct lib_ring_buffer *buf;	/* Channel per-cpu buffers */

	unsigned long num_subbuf;	/* Number of sub-buffers for writer */
	u64 start_tsc;			/* Channel creation TSC value */
	void *priv;			/* Client-specific information */
	struct notifier_block cpu_hp_notifier;	 /* CPU hotplug notifier */
	/*
	 * We need to copy config because the module containing the
	 * source config can vanish before the last reference to this
	 * channel's streams is released.
	 */
	struct lib_ring_buffer_config config; /* Ring buffer configuration */
	cpumask_var_t cpumask;		/* Allocated per-cpu buffers cpumask */
	char name[NAME_MAX];		/* Channel name */
};

#endif /* _LIB_RING_BUFFER_BACKEND_TYPES_H */
