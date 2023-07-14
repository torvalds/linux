/* SPDX-License-Identifier: GPL-2.0 */
/*
 * zfcp device driver
 *
 * Data structure and helper functions for tracking pending FSF
 * requests.
 *
 * Copyright IBM Corp. 2009, 2023
 */

#ifndef ZFCP_REQLIST_H
#define ZFCP_REQLIST_H

#include <linux/types.h>

/* number of hash buckets */
#define ZFCP_REQ_LIST_BUCKETS 128u

/**
 * struct zfcp_reqlist - Container for request list (reqlist)
 * @lock: Spinlock for protecting the hash list
 * @buckets: Array of hashbuckets, each is a list of requests in this bucket
 */
struct zfcp_reqlist {
	spinlock_t lock;
	struct list_head buckets[ZFCP_REQ_LIST_BUCKETS];
};

static inline size_t zfcp_reqlist_hash(u64 req_id)
{
	return req_id % ZFCP_REQ_LIST_BUCKETS;
}

/**
 * zfcp_reqlist_alloc - Allocate and initialize reqlist
 *
 * Returns pointer to allocated reqlist on success, or NULL on
 * allocation failure.
 */
static inline struct zfcp_reqlist *zfcp_reqlist_alloc(void)
{
	size_t i;
	struct zfcp_reqlist *rl;

	rl = kzalloc(sizeof(struct zfcp_reqlist), GFP_KERNEL);
	if (!rl)
		return NULL;

	spin_lock_init(&rl->lock);

	for (i = 0; i < ZFCP_REQ_LIST_BUCKETS; i++)
		INIT_LIST_HEAD(&rl->buckets[i]);

	return rl;
}

/**
 * zfcp_reqlist_isempty - Check whether the request list empty
 * @rl: pointer to reqlist
 *
 * Returns: 1 if list is empty, 0 if not
 */
static inline int zfcp_reqlist_isempty(struct zfcp_reqlist *rl)
{
	size_t i;

	for (i = 0; i < ZFCP_REQ_LIST_BUCKETS; i++)
		if (!list_empty(&rl->buckets[i]))
			return 0;
	return 1;
}

/**
 * zfcp_reqlist_free - Free allocated memory for reqlist
 * @rl: The reqlist where to free memory
 */
static inline void zfcp_reqlist_free(struct zfcp_reqlist *rl)
{
	/* sanity check */
	BUG_ON(!zfcp_reqlist_isempty(rl));

	kfree(rl);
}

static inline struct zfcp_fsf_req *
_zfcp_reqlist_find(struct zfcp_reqlist *rl, u64 req_id)
{
	struct zfcp_fsf_req *req;
	size_t i;

	i = zfcp_reqlist_hash(req_id);
	list_for_each_entry(req, &rl->buckets[i], list)
		if (req->req_id == req_id)
			return req;
	return NULL;
}

/**
 * zfcp_reqlist_find - Lookup FSF request by its request id
 * @rl: The reqlist where to lookup the FSF request
 * @req_id: The request id to look for
 *
 * Returns a pointer to the FSF request with the specified request id
 * or NULL if there is no known FSF request with this id.
 */
static inline struct zfcp_fsf_req *
zfcp_reqlist_find(struct zfcp_reqlist *rl, u64 req_id)
{
	unsigned long flags;
	struct zfcp_fsf_req *req;

	spin_lock_irqsave(&rl->lock, flags);
	req = _zfcp_reqlist_find(rl, req_id);
	spin_unlock_irqrestore(&rl->lock, flags);

	return req;
}

/**
 * zfcp_reqlist_find_rm - Lookup request by id and remove it from reqlist
 * @rl: reqlist where to search and remove entry
 * @req_id: The request id of the request to look for
 *
 * This functions tries to find the FSF request with the specified
 * id and then removes it from the reqlist. The reqlist lock is held
 * during both steps of the operation.
 *
 * Returns: Pointer to the FSF request if the request has been found,
 * NULL if it has not been found.
 */
static inline struct zfcp_fsf_req *
zfcp_reqlist_find_rm(struct zfcp_reqlist *rl, u64 req_id)
{
	unsigned long flags;
	struct zfcp_fsf_req *req;

	spin_lock_irqsave(&rl->lock, flags);
	req = _zfcp_reqlist_find(rl, req_id);
	if (req)
		list_del(&req->list);
	spin_unlock_irqrestore(&rl->lock, flags);

	return req;
}

/**
 * zfcp_reqlist_add - Add entry to reqlist
 * @rl: reqlist where to add the entry
 * @req: The entry to add
 *
 * The request id always increases. As an optimization new requests
 * are added here with list_add_tail at the end of the bucket lists
 * while old requests are looked up starting at the beginning of the
 * lists.
 */
static inline void zfcp_reqlist_add(struct zfcp_reqlist *rl,
				    struct zfcp_fsf_req *req)
{
	size_t i;
	unsigned long flags;

	i = zfcp_reqlist_hash(req->req_id);

	spin_lock_irqsave(&rl->lock, flags);
	list_add_tail(&req->list, &rl->buckets[i]);
	spin_unlock_irqrestore(&rl->lock, flags);
}

/**
 * zfcp_reqlist_move - Move all entries from reqlist to simple list
 * @rl: The zfcp_reqlist where to remove all entries
 * @list: The list where to move all entries
 */
static inline void zfcp_reqlist_move(struct zfcp_reqlist *rl,
				     struct list_head *list)
{
	size_t i;
	unsigned long flags;

	spin_lock_irqsave(&rl->lock, flags);
	for (i = 0; i < ZFCP_REQ_LIST_BUCKETS; i++)
		list_splice_init(&rl->buckets[i], list);
	spin_unlock_irqrestore(&rl->lock, flags);
}

/**
 * zfcp_reqlist_apply_for_all() - apply a function to every request.
 * @rl: the requestlist that contains the target requests.
 * @f: the function to apply to each request; the first parameter of the
 *     function will be the target-request; the second parameter is the same
 *     pointer as given with the argument @data.
 * @data: freely chosen argument; passed through to @f as second parameter.
 *
 * Uses :c:macro:`list_for_each_entry` to iterate over the lists in the hash-
 * table (not a 'safe' variant, so don't modify the list).
 *
 * Holds @rl->lock over the entire request-iteration.
 */
static inline void
zfcp_reqlist_apply_for_all(struct zfcp_reqlist *rl,
			   void (*f)(struct zfcp_fsf_req *, void *), void *data)
{
	struct zfcp_fsf_req *req;
	unsigned long flags;
	size_t i;

	spin_lock_irqsave(&rl->lock, flags);
	for (i = 0; i < ZFCP_REQ_LIST_BUCKETS; i++)
		list_for_each_entry(req, &rl->buckets[i], list)
			f(req, data);
	spin_unlock_irqrestore(&rl->lock, flags);
}

#endif /* ZFCP_REQLIST_H */
