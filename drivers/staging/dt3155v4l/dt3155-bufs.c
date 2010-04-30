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

#include "dt3155-bufs.h"

/**
 * dt3155_init_chunks_buf - creates a chunk buffer and allocates memory for it
 *
 * returns:	a pointer to the struct dt3155_buf or NULL if failed
 *
 * Creates a struct dt3155_buf, then allocates a chunk of memory of
 * size DT3155_CHUNK_SIZE and sets all the pages in it as Reserved.
 * This is done to be able to use remap_pfn_range() on these buffers
 * (which do not work on normal memory if Reserved bit is not set)
 */
struct dt3155_buf *
dt3155_init_chunks_buf(void)
{	/*  could sleep  */
	struct dt3155_buf *buf;
	int i;

	buf = kzalloc(sizeof(*buf), GFP_KERNEL);
	if (!buf)
		return NULL;
	buf->cpu = (void *)__get_free_pages(DT3155_CHUNK_FLAGS,
					get_order(DT3155_CHUNK_SIZE));
	if (!buf->cpu) {
		kfree(buf);
		return NULL;
	}
	for (i = 0; i < DT3155_CHUNK_SIZE; i += PAGE_SIZE)
		SetPageReserved(virt_to_page(buf->cpu + i));
	return buf;  /*   success  */
}

/**
 * dt3155_free_chunks_buf - destroys the specified buffer
 *
 * @buf:	the buffer to be freed
 *
 * Clears Reserved bit of all pages in the chunk, frees the chunk memory
 * and destroys struct dt3155_buf.
 */
void
dt3155_free_chunks_buf(struct dt3155_buf *buf)
{
	int i;

	for (i = 0; i < DT3155_CHUNK_SIZE; i += PAGE_SIZE)
		ClearPageReserved(virt_to_page(buf->cpu + i));
	free_pages((unsigned long)buf->cpu, get_order(DT3155_CHUNK_SIZE));
	kfree(buf);
}

/**
 * dt3155_init_fifo - creates and initializes a fifo
 *
 * returns:	a pointer to the crated and initialized struct dt3155_fifo
 *		or NULL if failed
 */
struct dt3155_fifo *
dt3155_init_fifo(void)
{	/* could sleep  */
	struct dt3155_fifo *fifo = kzalloc(sizeof(*fifo), GFP_KERNEL);
	if (fifo)
		spin_lock_init(&fifo->lock);
	return fifo;
}

/*	dt3155_free_fifo(x)  defined as macro in dt3155.h   */

/**
 * dt3155_get_buf - gets a buffer from the fifo
 *
 * @fifo:	the fifo to get a buffer from
 *
 * returns:	a pointer to the buffer or NULL if failed
 *
 * dt3155_get_buf gets the fifo's spin_lock and returns the
 * buffer pointed by the head. Could be used in any context.
 */
struct dt3155_buf *
dt3155_get_buf(struct dt3155_fifo *fifo)
{
	unsigned long flags;
	struct dt3155_buf *tmp_buf;

	spin_lock_irqsave(&fifo->lock, flags);
	tmp_buf = fifo->head;
	if (fifo->head)
		fifo->head = fifo->head->next;
	if (!fifo->head)
		fifo->tail = NULL;
	spin_unlock_irqrestore(&fifo->lock, flags);
	return tmp_buf;
}

/**
 * dt3155_put_buf - puts a buffer into a fifo
 *
 * @buf:	the buffer to put
 * @fifo:	the fifo to put the buffer in
 *
 * dt3155_put_buf gets the fifo's spin_lock and puts the buf
 * at the tail of the fifo. Could be used in any context.
 */
void
dt3155_put_buf(struct dt3155_buf *buf, struct dt3155_fifo *fifo)
{
	unsigned long flags;

	spin_lock_irqsave(&fifo->lock, flags);
	buf->next = NULL;
	if (fifo->tail)
		fifo->tail->next = buf;
	fifo->tail = buf;
	if (!fifo->head)
		fifo->head = buf;
	spin_unlock_irqrestore(&fifo->lock, flags);
}

/**
 * dt3155_init_chunks_fifo - creates and fills a chunks_fifo
 *
 * returns:	a pointer to the fifo or NULL if failed
 *
 * dt3155_init_chunks_fifo creates and fills the fifo with
 * a number of chunks <= DT3155_CHUNK_NUM. The returned fifo
 * contains at least one chunk.
 */
struct dt3155_fifo *
dt3155_init_chunks_fifo(void)
{	/*  could sleep  */
	int i;

	struct dt3155_fifo *chunks;
	struct dt3155_buf *tmp_buf;

	chunks = dt3155_init_fifo();
	if (!chunks)
		return NULL;
	tmp_buf = dt3155_init_chunks_buf();
	if (!tmp_buf) {
		dt3155_free_fifo(chunks);
		return NULL;
	}
	dt3155_put_buf(tmp_buf, chunks);
	for (i = 1; i < DT3155_CHUNK_NUM; i++) {
		tmp_buf = dt3155_init_chunks_buf();
		if (!tmp_buf)
			break;
		dt3155_put_buf(tmp_buf, chunks);
	}
	return chunks;
}

/**
 * dt3155_free_chunks_fifo - empties and destroys the chunks_fifo
 *
 * @chunks:	the chunks_fifo to be freed
 *
 * dt3155_free_chunks_fifo deallocates all chunks in the fifo and
 * destroys it.
 */
void
dt3155_free_chunks_fifo(struct dt3155_fifo *chunks)
{
	int buf_count = 0;
	struct dt3155_buf *buf;

	while ((buf = dt3155_get_buf(chunks))) {
		dt3155_free_chunks_buf(buf);
		buf_count++;
	}
	dt3155_free_fifo(chunks);
	printk(KERN_INFO "dt3155: %i chunks freed\n", buf_count);
}

/**
 * dt3155_init_ibufs_fifo - creates and fills an image buffer fifo
 *
 * @chunks:	chunks_fifo to take memory from
 * @buf_size:	the size of image buffers
 *
 * returns:	a pointer to the fifo filled with image buffers
 *
 * dt3155_init_ibufs_fifo takes chunks from chunks_fifo, chops them
 * into pieces of size buf_size and fills image fifo with them.
 */
struct dt3155_fifo *
dt3155_init_ibufs_fifo(struct dt3155_fifo *chunks, int buf_size)
{	/*  could sleep  */
	int i, buf_count = 0;
	struct dt3155_buf *tmp_ibuf, *chunks_buf, *last_chunk;
	struct dt3155_fifo *tmp_fifo;

	tmp_fifo = dt3155_init_fifo();
	if (!tmp_fifo)
		return NULL;
	last_chunk = chunks->tail;
	do {
		chunks_buf = dt3155_get_buf(chunks);
		dt3155_put_buf(chunks_buf, chunks);
		for (i = 0; i < DT3155_CHUNK_SIZE / buf_size; i++) {
			tmp_ibuf = kzalloc(sizeof(*tmp_ibuf), GFP_KERNEL);
			if (tmp_ibuf) {
				tmp_ibuf->cpu =
					chunks_buf->cpu + DT3155_BUF_SIZE * i;
				dt3155_put_buf(tmp_ibuf, tmp_fifo);
				buf_count++;
			} else {
				if (buf_count) {
					goto print_num_bufs;
				} else {
					dt3155_free_fifo(tmp_fifo);
					return NULL;
				}
			}
		}
	} while (chunks_buf != last_chunk);
print_num_bufs:
	printk(KERN_INFO "dt3155: %i image buffers available\n", buf_count);
	return tmp_fifo;
}

/**
 * dt3155_free_ibufs_fifo - empties and destroys an image fifo
 *
 * @fifo:	the fifo to free
 */
void
dt3155_free_ibufs_fifo(struct dt3155_fifo *fifo)
{
	struct dt3155_buf *tmp_ibuf;

	while ((tmp_ibuf = dt3155_get_buf(fifo)))
		kfree(tmp_ibuf);
	kfree(fifo);
}
