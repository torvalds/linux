/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _DM_PCACHE_H
#define _DM_PCACHE_H
#include <linux/device-mapper.h>

#include "../dm-core.h"

#define CACHE_DEV_TO_PCACHE(cache_dev)		(container_of(cache_dev, struct dm_pcache, cache_dev))
#define BACKING_DEV_TO_PCACHE(backing_dev)	(container_of(backing_dev, struct dm_pcache, backing_dev))
#define CACHE_TO_PCACHE(cache)			(container_of(cache, struct dm_pcache, cache))

#define PCACHE_STATE_RUNNING			1
#define PCACHE_STATE_STOPPING			2

struct pcache_cache_dev;
struct pcache_backing_dev;
struct pcache_cache;
struct pcache_cache_options;
struct dm_pcache {
	struct dm_target *ti;
	struct pcache_cache_dev cache_dev;
	struct pcache_backing_dev backing_dev;
	struct pcache_cache cache;
	struct pcache_cache_options opts;

	spinlock_t			defered_req_list_lock;
	struct list_head		defered_req_list;
	struct workqueue_struct		*task_wq;

	struct work_struct		defered_req_work;

	atomic_t			state;
	atomic_t			inflight_reqs;
	wait_queue_head_t		inflight_wq;
};

static inline bool pcache_is_stopping(struct dm_pcache *pcache)
{
	return (atomic_read(&pcache->state) == PCACHE_STATE_STOPPING);
}

#define pcache_dev_err(pcache, fmt, ...)							\
	pcache_err("%s " fmt, pcache->ti->table->md->name, ##__VA_ARGS__)
#define pcache_dev_info(pcache, fmt, ...)							\
	pcache_info("%s " fmt, pcache->ti->table->md->name, ##__VA_ARGS__)
#define pcache_dev_debug(pcache, fmt, ...)							\
	pcache_debug("%s " fmt, pcache->ti->table->md->name, ##__VA_ARGS__)

struct pcache_request {
	struct dm_pcache	*pcache;
	struct bio		*bio;

	u64			off;
	u32			data_len;

	struct kref		ref;
	int			ret;

	struct list_head	list_node;
};

void pcache_req_get(struct pcache_request *pcache_req);
void pcache_req_put(struct pcache_request *pcache_req, int ret);

void pcache_defer_reqs_kick(struct dm_pcache *pcache);

#endif /* _DM_PCACHE_H */
