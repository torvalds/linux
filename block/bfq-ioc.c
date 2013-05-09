/*
 * BFQ: I/O context handling.
 *
 * Based on ideas and code from CFQ:
 * Copyright (C) 2003 Jens Axboe <axboe@kernel.dk>
 *
 * Copyright (C) 2008 Fabio Checconi <fabio@gandalf.sssup.it>
 *		      Paolo Valente <paolo.valente@unimore.it>
 *
 * Copyright (C) 2010 Paolo Valente <paolo.valente@unimore.it>
 */

/**
 * icq_to_bic - convert iocontext queue structure to bfq_io_cq.
 * @icq: the iocontext queue.
 */
static inline struct bfq_io_cq *icq_to_bic(struct io_cq *icq)
{
	/* bic->icq is the first member, %NULL will convert to %NULL */
	return container_of(icq, struct bfq_io_cq, icq);
}

/**
 * bfq_bic_lookup - search into @ioc a bic associated to @bfqd.
 * @bfqd: the lookup key.
 * @ioc: the io_context of the process doing I/O.
 *
 * Queue lock must be held.
 */
static inline struct bfq_io_cq *bfq_bic_lookup(struct bfq_data *bfqd,
					       struct io_context *ioc)
{
	if(ioc)
		return icq_to_bic(ioc_lookup_icq(ioc, bfqd->queue));
	return NULL;
}
