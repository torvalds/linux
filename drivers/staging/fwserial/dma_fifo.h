/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * DMA-able FIFO interface
 *
 * Copyright (C) 2012 Peter Hurley <peter@hurleysoftware.com>
 */

#ifndef _DMA_FIFO_H_
#define _DMA_FIFO_H_

/**
 * The design basis for the DMA FIFO is to provide an output side that
 * complies with the streaming DMA API design that can be DMA'd from directly
 * (without additional copying), coupled with an input side that maintains a
 * logically consistent 'apparent' size (ie, bytes in + bytes avail is static
 * for the lifetime of the FIFO).
 *
 * DMA output transactions originate on a cache line boundary and can be
 * variably-sized. DMA output transactions can be retired out-of-order but
 * the FIFO will only advance the output in the original input sequence.
 * This means the FIFO will eventually stall if a transaction is never retired.
 *
 * Chunking the output side into cache line multiples means that some FIFO
 * memory is unused. For example, if all the avail input has been pended out,
 * then the in and out markers are re-aligned to the next cache line.
 * The maximum possible waste is
 *     (cache line alignment - 1) * (max outstanding dma transactions)
 * This potential waste requires additional hidden capacity within the FIFO
 * to be able to accept input while the 'apparent' size has not been reached.
 *
 * Additional cache lines (ie, guard area) are used to minimize DMA
 * fragmentation when wrapping at the end of the FIFO. Input is allowed into the
 * guard area, but the in and out FIFO markers are wrapped when DMA is pended.
 */

#define DMA_FIFO_GUARD 3   /* # of cache lines to reserve for the guard area */

struct dma_fifo {
	unsigned int	 in;
	unsigned int	 out;		/* updated when dma is pended         */
	unsigned int	 done;		/* updated upon dma completion        */
	struct {
		unsigned corrupt:1;
	};
	int		 size;		/* 'apparent' size of fifo	      */
	int		 guard;		/* ofs of guard area		      */
	int		 capacity;	/* size + reserved                    */
	int		 avail;		/* # of unused bytes in fifo          */
	unsigned int	 align;		/* must be power of 2                 */
	int		 tx_limit;	/* max # of bytes per dma transaction */
	int		 open_limit;	/* max # of outstanding allowed       */
	int		 open;		/* # of outstanding dma transactions  */
	struct list_head pending;	/* fifo markers for outstanding dma   */
	void		 *data;
};

struct dma_pending {
	struct list_head link;
	void		 *data;
	unsigned int	 len;
	unsigned int	 next;
	unsigned int	 out;
};

static inline void dp_mark_completed(struct dma_pending *dp)
{
	dp->data += 1;
}

static inline bool dp_is_completed(struct dma_pending *dp)
{
	return (unsigned long)dp->data & 1UL;
}

void dma_fifo_init(struct dma_fifo *fifo);
int dma_fifo_alloc(struct dma_fifo *fifo, int size, unsigned int align,
		   int tx_limit, int open_limit, gfp_t gfp_mask);
void dma_fifo_free(struct dma_fifo *fifo);
void dma_fifo_reset(struct dma_fifo *fifo);
int dma_fifo_in(struct dma_fifo *fifo, const void *src, int n);
int dma_fifo_out_pend(struct dma_fifo *fifo, struct dma_pending *pended);
int dma_fifo_out_complete(struct dma_fifo *fifo,
			  struct dma_pending *complete);

/* returns the # of used bytes in the fifo */
static inline int dma_fifo_level(struct dma_fifo *fifo)
{
	return fifo->size - fifo->avail;
}

/* returns the # of bytes ready for output in the fifo */
static inline int dma_fifo_out_level(struct dma_fifo *fifo)
{
	return fifo->in - fifo->out;
}

/* returns the # of unused bytes in the fifo */
static inline int dma_fifo_avail(struct dma_fifo *fifo)
{
	return fifo->avail;
}

/* returns true if fifo has max # of outstanding dmas */
static inline bool dma_fifo_busy(struct dma_fifo *fifo)
{
	return fifo->open == fifo->open_limit;
}

/* changes the max size of dma returned from dma_fifo_out_pend() */
static inline int dma_fifo_change_tx_limit(struct dma_fifo *fifo, int tx_limit)
{
	tx_limit = round_down(tx_limit, fifo->align);
	fifo->tx_limit = max_t(int, tx_limit, fifo->align);
	return 0;
}

#endif /* _DMA_FIFO_H_ */
