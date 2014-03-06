#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/percpu_ida.h>

#include <linux/blk-mq.h>
#include "blk.h"
#include "blk-mq.h"
#include "blk-mq-tag.h"

/*
 * Per tagged queue (tag address space) map
 */
struct blk_mq_tags {
	unsigned int nr_tags;
	unsigned int nr_reserved_tags;
	unsigned int nr_batch_move;
	unsigned int nr_max_cache;

	struct percpu_ida free_tags;
	struct percpu_ida reserved_tags;
};

void blk_mq_wait_for_tags(struct blk_mq_tags *tags)
{
	int tag = blk_mq_get_tag(tags, __GFP_WAIT, false);
	blk_mq_put_tag(tags, tag);
}

bool blk_mq_has_free_tags(struct blk_mq_tags *tags)
{
	return !tags ||
		percpu_ida_free_tags(&tags->free_tags, nr_cpu_ids) != 0;
}

static unsigned int __blk_mq_get_tag(struct blk_mq_tags *tags, gfp_t gfp)
{
	int tag;

	tag = percpu_ida_alloc(&tags->free_tags, (gfp & __GFP_WAIT) ?
			       TASK_UNINTERRUPTIBLE : TASK_RUNNING);
	if (tag < 0)
		return BLK_MQ_TAG_FAIL;
	return tag + tags->nr_reserved_tags;
}

static unsigned int __blk_mq_get_reserved_tag(struct blk_mq_tags *tags,
					      gfp_t gfp)
{
	int tag;

	if (unlikely(!tags->nr_reserved_tags)) {
		WARN_ON_ONCE(1);
		return BLK_MQ_TAG_FAIL;
	}

	tag = percpu_ida_alloc(&tags->reserved_tags, (gfp & __GFP_WAIT) ?
			       TASK_UNINTERRUPTIBLE : TASK_RUNNING);
	if (tag < 0)
		return BLK_MQ_TAG_FAIL;
	return tag;
}

unsigned int blk_mq_get_tag(struct blk_mq_tags *tags, gfp_t gfp, bool reserved)
{
	if (!reserved)
		return __blk_mq_get_tag(tags, gfp);

	return __blk_mq_get_reserved_tag(tags, gfp);
}

static void __blk_mq_put_tag(struct blk_mq_tags *tags, unsigned int tag)
{
	BUG_ON(tag >= tags->nr_tags);

	percpu_ida_free(&tags->free_tags, tag - tags->nr_reserved_tags);
}

static void __blk_mq_put_reserved_tag(struct blk_mq_tags *tags,
				      unsigned int tag)
{
	BUG_ON(tag >= tags->nr_reserved_tags);

	percpu_ida_free(&tags->reserved_tags, tag);
}

void blk_mq_put_tag(struct blk_mq_tags *tags, unsigned int tag)
{
	if (tag >= tags->nr_reserved_tags)
		__blk_mq_put_tag(tags, tag);
	else
		__blk_mq_put_reserved_tag(tags, tag);
}

static int __blk_mq_tag_iter(unsigned id, void *data)
{
	unsigned long *tag_map = data;
	__set_bit(id, tag_map);
	return 0;
}

void blk_mq_tag_busy_iter(struct blk_mq_tags *tags,
			  void (*fn)(void *, unsigned long *), void *data)
{
	unsigned long *tag_map;
	size_t map_size;

	map_size = ALIGN(tags->nr_tags, BITS_PER_LONG) / BITS_PER_LONG;
	tag_map = kzalloc(map_size * sizeof(unsigned long), GFP_ATOMIC);
	if (!tag_map)
		return;

	percpu_ida_for_each_free(&tags->free_tags, __blk_mq_tag_iter, tag_map);
	if (tags->nr_reserved_tags)
		percpu_ida_for_each_free(&tags->reserved_tags, __blk_mq_tag_iter,
			tag_map);

	fn(data, tag_map);
	kfree(tag_map);
}

struct blk_mq_tags *blk_mq_init_tags(unsigned int total_tags,
				     unsigned int reserved_tags, int node)
{
	unsigned int nr_tags, nr_cache;
	struct blk_mq_tags *tags;
	int ret;

	if (total_tags > BLK_MQ_TAG_MAX) {
		pr_err("blk-mq: tag depth too large\n");
		return NULL;
	}

	tags = kzalloc_node(sizeof(*tags), GFP_KERNEL, node);
	if (!tags)
		return NULL;

	nr_tags = total_tags - reserved_tags;
	nr_cache = nr_tags / num_possible_cpus();

	if (nr_cache < BLK_MQ_TAG_CACHE_MIN)
		nr_cache = BLK_MQ_TAG_CACHE_MIN;
	else if (nr_cache > BLK_MQ_TAG_CACHE_MAX)
		nr_cache = BLK_MQ_TAG_CACHE_MAX;

	tags->nr_tags = total_tags;
	tags->nr_reserved_tags = reserved_tags;
	tags->nr_max_cache = nr_cache;
	tags->nr_batch_move = max(1u, nr_cache / 2);

	ret = __percpu_ida_init(&tags->free_tags, tags->nr_tags -
				tags->nr_reserved_tags,
				tags->nr_max_cache,
				tags->nr_batch_move);
	if (ret)
		goto err_free_tags;

	if (reserved_tags) {
		/*
		 * With max_cahe and batch set to 1, the allocator fallbacks to
		 * no cached. It's fine reserved tags allocation is slow.
		 */
		ret = __percpu_ida_init(&tags->reserved_tags, reserved_tags,
				1, 1);
		if (ret)
			goto err_reserved_tags;
	}

	return tags;

err_reserved_tags:
	percpu_ida_destroy(&tags->free_tags);
err_free_tags:
	kfree(tags);
	return NULL;
}

void blk_mq_free_tags(struct blk_mq_tags *tags)
{
	percpu_ida_destroy(&tags->free_tags);
	percpu_ida_destroy(&tags->reserved_tags);
	kfree(tags);
}

ssize_t blk_mq_tag_sysfs_show(struct blk_mq_tags *tags, char *page)
{
	char *orig_page = page;
	int cpu;

	if (!tags)
		return 0;

	page += sprintf(page, "nr_tags=%u, reserved_tags=%u, batch_move=%u,"
			" max_cache=%u\n", tags->nr_tags, tags->nr_reserved_tags,
			tags->nr_batch_move, tags->nr_max_cache);

	page += sprintf(page, "nr_free=%u, nr_reserved=%u\n",
			percpu_ida_free_tags(&tags->free_tags, nr_cpu_ids),
			percpu_ida_free_tags(&tags->reserved_tags, nr_cpu_ids));

	for_each_possible_cpu(cpu) {
		page += sprintf(page, "  cpu%02u: nr_free=%u\n", cpu,
				percpu_ida_free_tags(&tags->free_tags, cpu));
	}

	return page - orig_page;
}
