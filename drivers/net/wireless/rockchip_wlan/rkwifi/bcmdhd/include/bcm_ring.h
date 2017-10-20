/*
 * bcm_ring.h : Ring context abstraction
 * The ring context tracks the WRITE and READ indices where elements may be
 * produced and consumed respectively. All elements in the ring need to be
 * fixed size.
 *
 * NOTE: A ring of size N, may only hold N-1 elements.
 *
 * Copyright (C) 1999-2017, Broadcom Corporation
 * 
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: bcm_ring.h 596126 2015-10-29 19:53:48Z $
 */
#ifndef __bcm_ring_included__
#define __bcm_ring_included__
/*
 * API Notes:
 *
 * Ring manipulation API allows for:
 *  Pending operations: Often before some work can be completed, it may be
 *  desired that several resources are available, e.g. space for production in
 *  a ring. Approaches such as, #1) reserve resources one by one and return them
 *  if another required resource is not available, or #2) employ a two pass
 *  algorithm of first testing whether all resources are available, have a
 *  an impact on performance critical code. The approach taken here is more akin
 *  to approach #2, where a test for resource availability essentially also
 *  provides the index for production in an un-committed state.
 *  The same approach is taken for the consumer side.
 *
 *  - Pending production: Fetch the next index where a ring element may be
 *    produced. The caller may not commit the WRITE of the element.
 *  - Pending consumption: Fetch the next index where a ring element may be
 *    consumed. The caller may not commut the READ of the element.
 *
 *  Producer side API:
 *  - bcm_ring_is_full  : Test whether ring is full
 *  - bcm_ring_prod     : Fetch index where an element may be produced (commit)
 *  - bcm_ring_prod_pend: Fetch index where an element may be produced (pending)
 *  - bcm_ring_prod_done: Commit a previous pending produce fetch
 *  - bcm_ring_prod_avail: Fetch total number free slots eligible for production
 *
 * Consumer side API:
 *  - bcm_ring_is_empty : Test whether ring is empty
 *  - bcm_ring_cons     : Fetch index where an element may be consumed (commit)
 *  - bcm_ring_cons_pend: Fetch index where an element may be consumed (pending)
 *  - bcm_ring_cons_done: Commit a previous pending consume fetch
 *  - bcm_ring_cons_avail: Fetch total number elements eligible for consumption
 *
 *  - bcm_ring_sync_read: Sync read offset in peer ring, from local ring
 *  - bcm_ring_sync_write: Sync write offset in peer ring, from local ring
 *
 * +----------------------------------------------------------------------------
 *
 * Design Notes:
 * Following items are not tracked in a ring context (design decision)
 *  - width of a ring element.
 *  - depth of the ring.
 *  - base of the buffer, where the elements are stored.
 *  - count of number of free slots in the ring
 *
 * Implementation Notes:
 *  - When BCM_RING_DEBUG is enabled, need explicit bcm_ring_init().
 *  - BCM_RING_EMPTY and BCM_RING_FULL are (-1)
 *
 * +----------------------------------------------------------------------------
 *
 * Usage Notes:
 * An application may incarnate a ring of some fixed sized elements, by defining
 *  - a ring data buffer to store the ring elements.
 *  - depth of the ring (max number of elements managed by ring context).
 *    Preferrably, depth may be represented as a constant.
 *  - width of a ring element: to be used in pointer arithmetic with the ring's
 *    data buffer base and an index to fetch the ring element.
 *
 * Use bcm_workq_t to instantiate a pair of workq constructs, one for the
 * producer and the other for the consumer, both pointing to the same circular
 * buffer. The producer may operate on it's own local workq and flush the write
 * index to the consumer. Likewise the consumer may use its local workq and
 * flush the read index to the producer. This way we do not repeatedly access
 * the peer's context. The two peers may reside on different CPU cores with a
 * private L1 data cache.
 * +----------------------------------------------------------------------------
 *
 * Copyright (C) 1999-2017, Broadcom Corporation
 * 
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 * $Id: bcm_ring.h 596126 2015-10-29 19:53:48Z $
 *
 * -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*-
 * vim: set ts=4 noet sw=4 tw=80:
 *
 * +----------------------------------------------------------------------------
 */

#ifdef ____cacheline_aligned
#define __ring_aligned                      ____cacheline_aligned
#else
#define __ring_aligned
#endif

/* Conditional compile for debug */
/* #define BCM_RING_DEBUG */

#define BCM_RING_EMPTY                      (-1)
#define BCM_RING_FULL                       (-1)
#define BCM_RING_NULL                       ((bcm_ring_t *)NULL)

#if defined(BCM_RING_DEBUG)
#define RING_ASSERT(exp)                    ASSERT(exp)
#define BCM_RING_IS_VALID(ring)             (((ring) != BCM_RING_NULL) && \
	                                         ((ring)->self == (ring)))
#else  /* ! BCM_RING_DEBUG */
#define RING_ASSERT(exp)                    do {} while (0)
#define BCM_RING_IS_VALID(ring)             ((ring) != BCM_RING_NULL)
#endif /* ! BCM_RING_DEBUG */

#define BCM_RING_SIZE_IS_VALID(ring_size)   ((ring_size) > 0)

/*
 * +----------------------------------------------------------------------------
 * Ring Context
 * +----------------------------------------------------------------------------
 */
typedef struct bcm_ring {     /* Ring context */
#if defined(BCM_RING_DEBUG)
	struct bcm_ring *self;    /* ptr to self for IS VALID test */
#endif /* BCM_RING_DEBUG */
	int write __ring_aligned; /* WRITE index in a circular ring */
	int read  __ring_aligned; /* READ index in a circular ring */
} bcm_ring_t;


static INLINE void bcm_ring_init(bcm_ring_t *ring);
static INLINE void bcm_ring_copy(bcm_ring_t *to, bcm_ring_t *from);
static INLINE bool bcm_ring_is_empty(bcm_ring_t *ring);

static INLINE int  __bcm_ring_next_write(bcm_ring_t *ring, const int ring_size);

static INLINE bool __bcm_ring_full(bcm_ring_t *ring, int next_write);
static INLINE bool bcm_ring_is_full(bcm_ring_t *ring, const int ring_size);

static INLINE void bcm_ring_prod_done(bcm_ring_t *ring, int write);
static INLINE int  bcm_ring_prod_pend(bcm_ring_t *ring, int *pend_write,
                                      const int ring_size);
static INLINE int  bcm_ring_prod(bcm_ring_t *ring, const int ring_size);

static INLINE void bcm_ring_cons_done(bcm_ring_t *ring, int read);
static INLINE int  bcm_ring_cons_pend(bcm_ring_t *ring, int *pend_read,
                                      const int ring_size);
static INLINE int  bcm_ring_cons(bcm_ring_t *ring, const int ring_size);

static INLINE void bcm_ring_sync_read(bcm_ring_t *peer, const bcm_ring_t *self);
static INLINE void bcm_ring_sync_write(bcm_ring_t *peer, const bcm_ring_t *self);

static INLINE int  bcm_ring_prod_avail(const bcm_ring_t *ring,
                                       const int ring_size);
static INLINE int  bcm_ring_cons_avail(const bcm_ring_t *ring,
                                       const int ring_size);
static INLINE void bcm_ring_cons_all(bcm_ring_t *ring);


/**
 * bcm_ring_init - initialize a ring context.
 * @ring: pointer to a ring context
 */
static INLINE void
bcm_ring_init(bcm_ring_t *ring)
{
	ASSERT(ring != (bcm_ring_t *)NULL);
#if defined(BCM_RING_DEBUG)
	ring->self = ring;
#endif /* BCM_RING_DEBUG */
	ring->write = 0;
	ring->read = 0;
}

/**
 * bcm_ring_copy - copy construct a ring
 * @to: pointer to the new ring context
 * @from: pointer to orig ring context
 */
static INLINE void
bcm_ring_copy(bcm_ring_t *to, bcm_ring_t *from)
{
	bcm_ring_init(to);

	to->write = from->write;
	to->read  = from->read;
}

/**
 * bcm_ring_is_empty - "Boolean" test whether ring is empty.
 * @ring: pointer to a ring context
 *
 * PS. does not return BCM_RING_EMPTY value.
 */
static INLINE bool
bcm_ring_is_empty(bcm_ring_t *ring)
{
	RING_ASSERT(BCM_RING_IS_VALID(ring));
	return (ring->read == ring->write);
}


/**
 * __bcm_ring_next_write - determine the index where the next write may occur
 *                         (with wrap-around).
 * @ring: pointer to a ring context
 * @ring_size: size of the ring
 *
 * PRIVATE INTERNAL USE ONLY.
 */
static INLINE int
__bcm_ring_next_write(bcm_ring_t *ring, const int ring_size)
{
	RING_ASSERT(BCM_RING_IS_VALID(ring) && BCM_RING_SIZE_IS_VALID(ring_size));
	return ((ring->write + 1) % ring_size);
}


/**
 * __bcm_ring_full - support function for ring full test.
 * @ring: pointer to a ring context
 * @next_write: next location in ring where an element is to be produced
 *
 * PRIVATE INTERNAL USE ONLY.
 */
static INLINE bool
__bcm_ring_full(bcm_ring_t *ring, int next_write)
{
	return (next_write == ring->read);
}


/**
 * bcm_ring_is_full - "Boolean" test whether a ring is full.
 * @ring: pointer to a ring context
 * @ring_size: size of the ring
 *
 * PS. does not return BCM_RING_FULL value.
 */
static INLINE bool
bcm_ring_is_full(bcm_ring_t *ring, const int ring_size)
{
	int next_write;
	RING_ASSERT(BCM_RING_IS_VALID(ring) && BCM_RING_SIZE_IS_VALID(ring_size));
	next_write = __bcm_ring_next_write(ring, ring_size);
	return __bcm_ring_full(ring, next_write);
}


/**
 * bcm_ring_prod_done - commit a previously pending index where production
 * was requested.
 * @ring: pointer to a ring context
 * @write: index into ring upto where production was done.
 * +----------------------------------------------------------------------------
 */
static INLINE void
bcm_ring_prod_done(bcm_ring_t *ring, int write)
{
	RING_ASSERT(BCM_RING_IS_VALID(ring));
	ring->write = write;
}


/**
 * bcm_ring_prod_pend - Fetch in "pend" mode, the index where an element may be
 * produced.
 * @ring: pointer to a ring context
 * @pend_write: next index, after the returned index
 * @ring_size: size of the ring
 */
static INLINE int
bcm_ring_prod_pend(bcm_ring_t *ring, int *pend_write, const int ring_size)
{
	int rtn;
	RING_ASSERT(BCM_RING_IS_VALID(ring) && BCM_RING_SIZE_IS_VALID(ring_size));
	*pend_write = __bcm_ring_next_write(ring, ring_size);
	if (__bcm_ring_full(ring, *pend_write)) {
		*pend_write = BCM_RING_FULL;
		rtn = BCM_RING_FULL;
	} else {
		/* production is not committed, caller needs to explicitly commit */
		rtn = ring->write;
	}
	return rtn;
}


/**
 * bcm_ring_prod - Fetch and "commit" the next index where a ring element may
 * be produced.
 * @ring: pointer to a ring context
 * @ring_size: size of the ring
 */
static INLINE int
bcm_ring_prod(bcm_ring_t *ring, const int ring_size)
{
	int next_write, prod_write;
	RING_ASSERT(BCM_RING_IS_VALID(ring) && BCM_RING_SIZE_IS_VALID(ring_size));

	next_write = __bcm_ring_next_write(ring, ring_size);
	if (__bcm_ring_full(ring, next_write)) {
		prod_write = BCM_RING_FULL;
	} else {
		prod_write = ring->write;
		bcm_ring_prod_done(ring, next_write); /* "commit" production */
	}
	return prod_write;
}


/**
 * bcm_ring_cons_done - commit a previously pending read
 * @ring: pointer to a ring context
 * @read: index upto which elements have been consumed.
 */
static INLINE void
bcm_ring_cons_done(bcm_ring_t *ring, int read)
{
	RING_ASSERT(BCM_RING_IS_VALID(ring));
	ring->read = read;
}


/**
 * bcm_ring_cons_pend - fetch in "pend" mode, the next index where a ring
 * element may be consumed.
 * @ring: pointer to a ring context
 * @pend_read: index into ring upto which elements may be consumed.
 * @ring_size: size of the ring
 */
static INLINE int
bcm_ring_cons_pend(bcm_ring_t *ring, int *pend_read, const int ring_size)
{
	int rtn;
	RING_ASSERT(BCM_RING_IS_VALID(ring) && BCM_RING_SIZE_IS_VALID(ring_size));
	if (bcm_ring_is_empty(ring)) {
		*pend_read = BCM_RING_EMPTY;
		rtn = BCM_RING_EMPTY;
	} else {
		*pend_read = (ring->read + 1) % ring_size;
		/* production is not committed, caller needs to explicitly commit */
		rtn = ring->read;
	}
	return rtn;
}


/**
 * bcm_ring_cons - fetch and "commit" the next index where a ring element may
 * be consumed.
 * @ring: pointer to a ring context
 * @ring_size: size of the ring
 */
static INLINE int
bcm_ring_cons(bcm_ring_t *ring, const int ring_size)
{
	int cons_read;
	RING_ASSERT(BCM_RING_IS_VALID(ring) && BCM_RING_SIZE_IS_VALID(ring_size));
	if (bcm_ring_is_empty(ring)) {
		cons_read = BCM_RING_EMPTY;
	} else {
		cons_read = ring->read;
		ring->read = (ring->read + 1) % ring_size; /* read is committed */
	}
	return cons_read;
}


/**
 * bcm_ring_sync_read - on consumption, update peer's read index.
 * @peer: pointer to peer's producer ring context
 * @self: pointer to consumer's ring context
 */
static INLINE void
bcm_ring_sync_read(bcm_ring_t *peer, const bcm_ring_t *self)
{
	RING_ASSERT(BCM_RING_IS_VALID(peer));
	RING_ASSERT(BCM_RING_IS_VALID(self));
	peer->read = self->read; /* flush read update to peer producer */
}


/**
 * bcm_ring_sync_write - on consumption, update peer's write index.
 * @peer: pointer to peer's consumer ring context
 * @self: pointer to producer's ring context
 */
static INLINE void
bcm_ring_sync_write(bcm_ring_t *peer, const bcm_ring_t *self)
{
	RING_ASSERT(BCM_RING_IS_VALID(peer));
	RING_ASSERT(BCM_RING_IS_VALID(self));
	peer->write = self->write; /* flush write update to peer consumer */
}


/**
 * bcm_ring_prod_avail - fetch total number of available empty slots in the
 * ring for production.
 * @ring: pointer to a ring context
 * @ring_size: size of the ring
 */
static INLINE int
bcm_ring_prod_avail(const bcm_ring_t *ring, const int ring_size)
{
	int prod_avail;
	RING_ASSERT(BCM_RING_IS_VALID(ring) && BCM_RING_SIZE_IS_VALID(ring_size));
	if (ring->write >= ring->read) {
		prod_avail = (ring_size - (ring->write - ring->read) - 1);
	} else {
		prod_avail = (ring->read - (ring->write + 1));
	}
	ASSERT(prod_avail < ring_size);
	return prod_avail;
}


/**
 * bcm_ring_cons_avail - fetch total number of available elements for consumption.
 * @ring: pointer to a ring context
 * @ring_size: size of the ring
 */
static INLINE int
bcm_ring_cons_avail(const bcm_ring_t *ring, const int ring_size)
{
	int cons_avail;
	RING_ASSERT(BCM_RING_IS_VALID(ring) && BCM_RING_SIZE_IS_VALID(ring_size));
	if (ring->read == ring->write) {
		cons_avail = 0;
	} else if (ring->read > ring->write) {
		cons_avail = ((ring_size - ring->read) + ring->write);
	} else {
		cons_avail = ring->write - ring->read;
	}
	ASSERT(cons_avail < ring_size);
	return cons_avail;
}


/**
 * bcm_ring_cons_all - set ring in state where all elements are consumed.
 * @ring: pointer to a ring context
 */
static INLINE void
bcm_ring_cons_all(bcm_ring_t *ring)
{
	ring->read = ring->write;
}


/**
 * Work Queue
 * A work Queue is composed of a ring of work items, of a specified depth.
 * It HAS-A bcm_ring object, comprising of a RD and WR offset, to implement a
 * producer/consumer circular ring.
 */

struct bcm_workq {
	bcm_ring_t ring;        /* Ring context abstraction */
	struct bcm_workq *peer; /* Peer workq context */
	void       *buffer;     /* Buffer storage for work items in workQ */
	int        ring_size;   /* Depth of workQ */
} __ring_aligned;

typedef struct bcm_workq bcm_workq_t;


/* #define BCM_WORKQ_DEBUG */
#if defined(BCM_WORKQ_DEBUG)
#define WORKQ_ASSERT(exp)               ASSERT(exp)
#else  /* ! BCM_WORKQ_DEBUG */
#define WORKQ_ASSERT(exp)               do {} while (0)
#endif /* ! BCM_WORKQ_DEBUG */

#define WORKQ_AUDIT(workq) \
	WORKQ_ASSERT((workq) != BCM_WORKQ_NULL); \
	WORKQ_ASSERT(WORKQ_PEER(workq) != BCM_WORKQ_NULL); \
	WORKQ_ASSERT((workq)->buffer == WORKQ_PEER(workq)->buffer); \
	WORKQ_ASSERT((workq)->ring_size == WORKQ_PEER(workq)->ring_size);

#define BCM_WORKQ_NULL                  ((bcm_workq_t *)NULL)

#define WORKQ_PEER(workq)               ((workq)->peer)
#define WORKQ_RING(workq)               (&((workq)->ring))
#define WORKQ_PEER_RING(workq)          (&((workq)->peer->ring))

#define WORKQ_ELEMENT(__elem_type, __workq, __index) ({ \
	WORKQ_ASSERT((__workq) != BCM_WORKQ_NULL); \
	WORKQ_ASSERT((__index) < ((__workq)->ring_size)); \
	((__elem_type *)((__workq)->buffer)) + (__index); \
})


static INLINE void bcm_workq_init(bcm_workq_t *workq, bcm_workq_t *workq_peer,
                                  void *buffer, int ring_size);

static INLINE bool bcm_workq_is_empty(bcm_workq_t *workq_prod);

static INLINE void bcm_workq_prod_sync(bcm_workq_t *workq_prod);
static INLINE void bcm_workq_cons_sync(bcm_workq_t *workq_cons);

static INLINE void bcm_workq_prod_refresh(bcm_workq_t *workq_prod);
static INLINE void bcm_workq_cons_refresh(bcm_workq_t *workq_cons);

/**
 * bcm_workq_init - initialize a workq
 * @workq: pointer to a workq context
 * @buffer: pointer to a pre-allocated circular buffer to serve as a ring
 * @ring_size: size of the ring in terms of max number of elements.
 */
static INLINE void
bcm_workq_init(bcm_workq_t *workq, bcm_workq_t *workq_peer,
               void *buffer, int ring_size)
{
	ASSERT(workq != BCM_WORKQ_NULL);
	ASSERT(workq_peer != BCM_WORKQ_NULL);
	ASSERT(buffer != NULL);
	ASSERT(ring_size > 0);

	WORKQ_PEER(workq) = workq_peer;
	WORKQ_PEER(workq_peer) = workq;

	bcm_ring_init(WORKQ_RING(workq));
	bcm_ring_init(WORKQ_RING(workq_peer));

	workq->buffer = workq_peer->buffer = buffer;
	workq->ring_size = workq_peer->ring_size = ring_size;
}

/**
 * bcm_workq_empty - test whether there is work
 * @workq_prod: producer's workq
 */
static INLINE bool
bcm_workq_is_empty(bcm_workq_t *workq_prod)
{
	return bcm_ring_is_empty(WORKQ_RING(workq_prod));
}

/**
 * bcm_workq_prod_sync - Commit the producer write index to peer workq's ring
 * @workq_prod: producer's workq whose write index must be synced to peer
 */
static INLINE void
bcm_workq_prod_sync(bcm_workq_t *workq_prod)
{
	WORKQ_AUDIT(workq_prod);

	/* cons::write <--- prod::write */
	bcm_ring_sync_write(WORKQ_PEER_RING(workq_prod), WORKQ_RING(workq_prod));
}

/**
 * bcm_workq_cons_sync - Commit the consumer read index to the peer workq's ring
 * @workq_cons: consumer's workq whose read index must be synced to peer
 */
static INLINE void
bcm_workq_cons_sync(bcm_workq_t *workq_cons)
{
	WORKQ_AUDIT(workq_cons);

	/* prod::read <--- cons::read */
	bcm_ring_sync_read(WORKQ_PEER_RING(workq_cons), WORKQ_RING(workq_cons));
}


/**
 * bcm_workq_prod_refresh - Fetch the updated consumer's read index
 * @workq_prod: producer's workq whose read index must be refreshed from peer
 */
static INLINE void
bcm_workq_prod_refresh(bcm_workq_t *workq_prod)
{
	WORKQ_AUDIT(workq_prod);

	/* prod::read <--- cons::read */
	bcm_ring_sync_read(WORKQ_RING(workq_prod), WORKQ_PEER_RING(workq_prod));
}

/**
 * bcm_workq_cons_refresh - Fetch the updated producer's write index
 * @workq_cons: consumer's workq whose write index must be refreshed from peer
 */
static INLINE void
bcm_workq_cons_refresh(bcm_workq_t *workq_cons)
{
	WORKQ_AUDIT(workq_cons);

	/* cons::write <--- prod::write */
	bcm_ring_sync_write(WORKQ_RING(workq_cons), WORKQ_PEER_RING(workq_cons));
}


#endif /* ! __bcm_ring_h_included__ */
