/***************************************************************************
 *   Copyright (C) 2006-2010 by Marin Mitov                                *
 *   mitov@issp.bas.bg                                                     *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#ifndef _DT3155_BUFS_H_
#define _DT3155_BUFS_H_

#include <linux/pci.h>

/* 4 chunks of 4MB, 9 buffers each = 36 buffers (> VIDEO_MAX_FRAME) */
#define DT3155_CHUNK_NUM 4

/* DT3155_CHUNK_SIZE should be 4M (2^22) or less, but more than image size */
#define DT3155_CHUNK_SIZE (1U << 22)
#define DT3155_CHUNK_FLAGS (GFP_KERNEL | GFP_DMA32 | __GFP_COLD | __GFP_NOWARN)

/* DT3155_BUF_SIZE = 108 * PAGE_SIZE, so each buf is PAGE_SIZE alligned  */
#define DT3155_BUF_SIZE (768 * 576)

/**
 * struct dt3155_buf - image buffer structure
 *
 * @cpu:	virtual kernel address of the buffer
 * @dma:	dma (bus) address of the buffer
 * @next:	pointer to the next buffer in the fifo
 * @tv:		time value when the image has been acquired
 */
struct dt3155_buf {
	void *cpu;
	dma_addr_t dma;
	struct dt3155_buf *next;
	struct timeval tv;
};

/**
 * struct dt3155_fifo - fifo structure
 *
 * @head:	pointer to the head of the fifo
 * @tail:	pionter to the tail of the fifo
 * @lock:	spin_lock to protect the fifo
 */
struct dt3155_fifo {
	struct dt3155_buf *head;
	struct dt3155_buf *tail;
	spinlock_t lock;
};

struct dt3155_buf * __must_check
dt3155_init_chunks_buf(void);
void
dt3155_free_chunks_buf(struct dt3155_buf *buf);

struct dt3155_fifo * __must_check
dt3155_init_fifo(void);
#define dt3155_free_fifo(x) kfree(x)

struct dt3155_buf * __must_check
dt3155_get_buf(struct dt3155_fifo *fifo);
void
dt3155_put_buf(struct dt3155_buf *buf, struct dt3155_fifo *fifo);

struct dt3155_fifo * __must_check
dt3155_init_chunks_fifo(void);
void
dt3155_free_chunks_fifo(struct dt3155_fifo *chunks);

struct dt3155_fifo * __must_check
dt3155_init_ibufs_fifo(struct dt3155_fifo *chunks, int buf_size);
void
dt3155_free_ibufs_fifo(struct dt3155_fifo *fifo);

#endif /*  _DT3155_BUFS_H_  */
