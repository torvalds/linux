/*
 * ramster.h
 *
 * Peer-to-peer transcendent memory
 *
 * Copyright (c) 2009-2012, Dan Magenheimer, Oracle Corp.
 */

#ifndef _RAMSTER_RAMSTER_H_
#define _RAMSTER_RAMSTER_H_

#include "../tmem.h"

enum ramster_remotify_op {
	RAMSTER_REMOTIFY_FLUSH_PAGE,
	RAMSTER_REMOTIFY_FLUSH_OBJ,
};

struct ramster_remotify_hdr {
	enum ramster_remotify_op op;
	struct list_head list;
};

struct flushlist_node {
	struct ramster_remotify_hdr rem_op;
	struct tmem_xhandle xh;
};

struct ramster_preload {
	struct flushlist_node *flnode;
};

union remotify_list_node {
	struct ramster_remotify_hdr rem_op;
	struct {
		struct ramster_remotify_hdr rem_op;
		struct tmem_handle th;
	} zbud_hdr;
	struct flushlist_node flist;
};

/*
 * format of remote pampd:
 *   bit 0 is reserved for zbud (in-page buddy selection)
 *   bit 1 == intransit
 *   bit 2 == is_remote... if this bit is set, then
 *   bit 3-10 == remotenode
 *   bit 11-23 == size
 *   bit 24-31 == cksum
 */
#define FAKE_PAMPD_INTRANSIT_BITS	1
#define FAKE_PAMPD_ISREMOTE_BITS	1
#define FAKE_PAMPD_REMOTENODE_BITS	8
#define FAKE_PAMPD_REMOTESIZE_BITS	13
#define FAKE_PAMPD_CHECKSUM_BITS	8

#define FAKE_PAMPD_INTRANSIT_SHIFT	1
#define FAKE_PAMPD_ISREMOTE_SHIFT	(FAKE_PAMPD_INTRANSIT_SHIFT + \
					 FAKE_PAMPD_INTRANSIT_BITS)
#define FAKE_PAMPD_REMOTENODE_SHIFT	(FAKE_PAMPD_ISREMOTE_SHIFT + \
					 FAKE_PAMPD_ISREMOTE_BITS)
#define FAKE_PAMPD_REMOTESIZE_SHIFT	(FAKE_PAMPD_REMOTENODE_SHIFT + \
					 FAKE_PAMPD_REMOTENODE_BITS)
#define FAKE_PAMPD_CHECKSUM_SHIFT	(FAKE_PAMPD_REMOTESIZE_SHIFT + \
					 FAKE_PAMPD_REMOTESIZE_BITS)

#define FAKE_PAMPD_MASK(x)		((1UL << (x)) - 1)

static inline void *pampd_make_remote(int remotenode, size_t size,
					unsigned char cksum)
{
	unsigned long fake_pampd = 0;
	fake_pampd |= 1UL << FAKE_PAMPD_ISREMOTE_SHIFT;
	fake_pampd |= ((unsigned long)remotenode &
			FAKE_PAMPD_MASK(FAKE_PAMPD_REMOTENODE_BITS)) <<
				FAKE_PAMPD_REMOTENODE_SHIFT;
	fake_pampd |= ((unsigned long)size &
			FAKE_PAMPD_MASK(FAKE_PAMPD_REMOTESIZE_BITS)) <<
				FAKE_PAMPD_REMOTESIZE_SHIFT;
	fake_pampd |= ((unsigned long)cksum &
			FAKE_PAMPD_MASK(FAKE_PAMPD_CHECKSUM_BITS)) <<
				FAKE_PAMPD_CHECKSUM_SHIFT;
	return (void *)fake_pampd;
}

static inline unsigned int pampd_remote_node(void *pampd)
{
	unsigned long fake_pampd = (unsigned long)pampd;
	return (fake_pampd >> FAKE_PAMPD_REMOTENODE_SHIFT) &
		FAKE_PAMPD_MASK(FAKE_PAMPD_REMOTENODE_BITS);
}

static inline unsigned int pampd_remote_size(void *pampd)
{
	unsigned long fake_pampd = (unsigned long)pampd;
	return (fake_pampd >> FAKE_PAMPD_REMOTESIZE_SHIFT) &
		FAKE_PAMPD_MASK(FAKE_PAMPD_REMOTESIZE_BITS);
}

static inline unsigned char pampd_remote_cksum(void *pampd)
{
	unsigned long fake_pampd = (unsigned long)pampd;
	return (fake_pampd >> FAKE_PAMPD_CHECKSUM_SHIFT) &
		FAKE_PAMPD_MASK(FAKE_PAMPD_CHECKSUM_BITS);
}

static inline bool pampd_is_remote(void *pampd)
{
	unsigned long fake_pampd = (unsigned long)pampd;
	return (fake_pampd >> FAKE_PAMPD_ISREMOTE_SHIFT) &
		FAKE_PAMPD_MASK(FAKE_PAMPD_ISREMOTE_BITS);
}

static inline bool pampd_is_intransit(void *pampd)
{
	unsigned long fake_pampd = (unsigned long)pampd;
	return (fake_pampd >> FAKE_PAMPD_INTRANSIT_SHIFT) &
		FAKE_PAMPD_MASK(FAKE_PAMPD_INTRANSIT_BITS);
}

/* note that it is a BUG for intransit to be set without isremote also set */
static inline void *pampd_mark_intransit(void *pampd)
{
	unsigned long fake_pampd = (unsigned long)pampd;

	fake_pampd |= 1UL << FAKE_PAMPD_ISREMOTE_SHIFT;
	fake_pampd |= 1UL << FAKE_PAMPD_INTRANSIT_SHIFT;
	return (void *)fake_pampd;
}

static inline void *pampd_mask_intransit_and_remote(void *marked_pampd)
{
	unsigned long pampd = (unsigned long)marked_pampd;

	pampd &= ~(1UL << FAKE_PAMPD_INTRANSIT_SHIFT);
	pampd &= ~(1UL << FAKE_PAMPD_ISREMOTE_SHIFT);
	return (void *)pampd;
}

extern int r2net_remote_async_get(struct tmem_xhandle *,
				bool, int, size_t, uint8_t, void *extra);
extern int r2net_remote_put(struct tmem_xhandle *, char *, size_t,
				bool, int *);
extern int r2net_remote_flush(struct tmem_xhandle *, int);
extern int r2net_remote_flush_object(struct tmem_xhandle *, int);
extern int r2net_register_handlers(void);
extern int r2net_remote_target_node_set(int);

extern int ramster_remotify_pageframe(bool);
extern void ramster_init(bool, bool, bool, bool);
extern void ramster_register_pamops(struct tmem_pamops *);
extern int ramster_localify(int, struct tmem_oid *oidp, uint32_t, char *,
				unsigned int, void *);
extern void *ramster_pampd_free(void *, struct tmem_pool *, struct tmem_oid *,
				uint32_t, bool);
extern void ramster_count_foreign_pages(bool, int);
extern int ramster_do_preload_flnode(struct tmem_pool *);
extern void ramster_cpu_up(int);
extern void ramster_cpu_down(int);

#endif /* _RAMSTER_RAMSTER_H */
