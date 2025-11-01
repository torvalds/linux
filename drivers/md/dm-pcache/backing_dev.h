/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _BACKING_DEV_H
#define _BACKING_DEV_H

#include <linux/device-mapper.h>

#include "pcache_internal.h"

struct pcache_backing_dev_req;
typedef void (*backing_req_end_fn_t)(struct pcache_backing_dev_req *backing_req, int ret);

#define BACKING_DEV_REQ_TYPE_REQ		1
#define BACKING_DEV_REQ_TYPE_KMEM		2

#define BACKING_DEV_REQ_INLINE_BVECS		4

struct pcache_request;
struct pcache_backing_dev_req {
	u8				type;
	struct bio			bio;
	struct pcache_backing_dev	*backing_dev;

	void				*priv_data;
	backing_req_end_fn_t		end_req;

	struct list_head		node;
	int				ret;

	union {
		struct {
			struct pcache_request		*upper_req;
			u32				bio_off;
		} req;
		struct {
			struct bio_vec	inline_bvecs[BACKING_DEV_REQ_INLINE_BVECS];
			struct bio_vec	*bvecs;
			u32		n_vecs;
		} kmem;
	};
};

struct pcache_backing_dev {
	struct pcache_cache		*cache;

	struct dm_dev			*dm_dev;
	mempool_t			req_pool;
	mempool_t			bvec_pool;

	struct list_head		submit_list;
	spinlock_t			submit_lock;
	struct work_struct		req_submit_work;

	struct list_head		complete_list;
	spinlock_t			complete_lock;
	struct work_struct		req_complete_work;

	atomic_t			inflight_reqs;
	wait_queue_head_t		inflight_wq;

	u64				dev_size;
};

struct dm_pcache;
int backing_dev_start(struct dm_pcache *pcache);
void backing_dev_stop(struct dm_pcache *pcache);

struct pcache_backing_dev_req_opts {
	u32 type;
	union {
		struct {
			struct pcache_request *upper_req;
			u32 req_off;
			u32 len;
		} req;
		struct {
			void *data;
			blk_opf_t opf;
			u32 len;
			u64 backing_off;
		} kmem;
	};

	gfp_t gfp_mask;
	backing_req_end_fn_t	end_fn;
	void			*priv_data;
};

static inline u32 backing_dev_req_coalesced_max_len(const void *data, u32 len)
{
	const void *p = data;
	u32 done = 0, in_page, to_advance;
	struct page *first_page, *next_page;

	if (!is_vmalloc_addr(data))
		return len;

	first_page = vmalloc_to_page(p);
advance:
	in_page = PAGE_SIZE - offset_in_page(p);
	to_advance = min_t(u32, in_page, len - done);

	done += to_advance;
	p += to_advance;

	if (done == len)
		return done;

	next_page = vmalloc_to_page(p);
	if (zone_device_pages_have_same_pgmap(first_page, next_page))
		goto advance;

	return done;
}

void backing_dev_req_submit(struct pcache_backing_dev_req *backing_req, bool direct);
void backing_dev_req_end(struct pcache_backing_dev_req *backing_req);
struct pcache_backing_dev_req *backing_dev_req_create(struct pcache_backing_dev *backing_dev,
						struct pcache_backing_dev_req_opts *opts);
struct pcache_backing_dev_req *backing_dev_req_alloc(struct pcache_backing_dev *backing_dev,
						struct pcache_backing_dev_req_opts *opts);
void backing_dev_req_init(struct pcache_backing_dev_req *backing_req,
			struct pcache_backing_dev_req_opts *opts);
void backing_dev_flush(struct pcache_backing_dev *backing_dev);

int pcache_backing_init(void);
void pcache_backing_exit(void);
#endif /* _BACKING_DEV_H */
