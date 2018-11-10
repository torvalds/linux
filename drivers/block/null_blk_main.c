/*
 * Add configfs and memory store: Kyungchan Koh <kkc6196@fb.com> and
 * Shaohua Li <shli@fb.com>
 */
#include <linux/module.h>

#include <linux/moduleparam.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/init.h>
#include "null_blk.h"

#define PAGE_SECTORS_SHIFT	(PAGE_SHIFT - SECTOR_SHIFT)
#define PAGE_SECTORS		(1 << PAGE_SECTORS_SHIFT)
#define SECTOR_MASK		(PAGE_SECTORS - 1)

#define FREE_BATCH		16

#define TICKS_PER_SEC		50ULL
#define TIMER_INTERVAL		(NSEC_PER_SEC / TICKS_PER_SEC)

#ifdef CONFIG_BLK_DEV_NULL_BLK_FAULT_INJECTION
static DECLARE_FAULT_ATTR(null_timeout_attr);
static DECLARE_FAULT_ATTR(null_requeue_attr);
#endif

static inline u64 mb_per_tick(int mbps)
{
	return (1 << 20) / TICKS_PER_SEC * ((u64) mbps);
}

/*
 * Status flags for nullb_device.
 *
 * CONFIGURED:	Device has been configured and turned on. Cannot reconfigure.
 * UP:		Device is currently on and visible in userspace.
 * THROTTLED:	Device is being throttled.
 * CACHE:	Device is using a write-back cache.
 */
enum nullb_device_flags {
	NULLB_DEV_FL_CONFIGURED	= 0,
	NULLB_DEV_FL_UP		= 1,
	NULLB_DEV_FL_THROTTLED	= 2,
	NULLB_DEV_FL_CACHE	= 3,
};

#define MAP_SZ		((PAGE_SIZE >> SECTOR_SHIFT) + 2)
/*
 * nullb_page is a page in memory for nullb devices.
 *
 * @page:	The page holding the data.
 * @bitmap:	The bitmap represents which sector in the page has data.
 *		Each bit represents one block size. For example, sector 8
 *		will use the 7th bit
 * The highest 2 bits of bitmap are for special purpose. LOCK means the cache
 * page is being flushing to storage. FREE means the cache page is freed and
 * should be skipped from flushing to storage. Please see
 * null_make_cache_space
 */
struct nullb_page {
	struct page *page;
	DECLARE_BITMAP(bitmap, MAP_SZ);
};
#define NULLB_PAGE_LOCK (MAP_SZ - 1)
#define NULLB_PAGE_FREE (MAP_SZ - 2)

static LIST_HEAD(nullb_list);
static struct mutex lock;
static int null_major;
static DEFINE_IDA(nullb_indexes);
static struct blk_mq_tag_set tag_set;

enum {
	NULL_IRQ_NONE		= 0,
	NULL_IRQ_SOFTIRQ	= 1,
	NULL_IRQ_TIMER		= 2,
};

enum {
	NULL_Q_BIO		= 0,
	NULL_Q_RQ		= 1,
	NULL_Q_MQ		= 2,
};

static int g_no_sched;
module_param_named(no_sched, g_no_sched, int, 0444);
MODULE_PARM_DESC(no_sched, "No io scheduler");

static int g_submit_queues = 1;
module_param_named(submit_queues, g_submit_queues, int, 0444);
MODULE_PARM_DESC(submit_queues, "Number of submission queues");

static int g_home_node = NUMA_NO_NODE;
module_param_named(home_node, g_home_node, int, 0444);
MODULE_PARM_DESC(home_node, "Home node for the device");

#ifdef CONFIG_BLK_DEV_NULL_BLK_FAULT_INJECTION
static char g_timeout_str[80];
module_param_string(timeout, g_timeout_str, sizeof(g_timeout_str), 0444);

static char g_requeue_str[80];
module_param_string(requeue, g_requeue_str, sizeof(g_requeue_str), 0444);
#endif

static int g_queue_mode = NULL_Q_MQ;

static int null_param_store_val(const char *str, int *val, int min, int max)
{
	int ret, new_val;

	ret = kstrtoint(str, 10, &new_val);
	if (ret)
		return -EINVAL;

	if (new_val < min || new_val > max)
		return -EINVAL;

	*val = new_val;
	return 0;
}

static int null_set_queue_mode(const char *str, const struct kernel_param *kp)
{
	return null_param_store_val(str, &g_queue_mode, NULL_Q_BIO, NULL_Q_MQ);
}

static const struct kernel_param_ops null_queue_mode_param_ops = {
	.set	= null_set_queue_mode,
	.get	= param_get_int,
};

device_param_cb(queue_mode, &null_queue_mode_param_ops, &g_queue_mode, 0444);
MODULE_PARM_DESC(queue_mode, "Block interface to use (0=bio,1=rq,2=multiqueue)");

static int g_gb = 250;
module_param_named(gb, g_gb, int, 0444);
MODULE_PARM_DESC(gb, "Size in GB");

static int g_bs = 512;
module_param_named(bs, g_bs, int, 0444);
MODULE_PARM_DESC(bs, "Block size (in bytes)");

static int nr_devices = 1;
module_param(nr_devices, int, 0444);
MODULE_PARM_DESC(nr_devices, "Number of devices to register");

static bool g_blocking;
module_param_named(blocking, g_blocking, bool, 0444);
MODULE_PARM_DESC(blocking, "Register as a blocking blk-mq driver device");

static bool shared_tags;
module_param(shared_tags, bool, 0444);
MODULE_PARM_DESC(shared_tags, "Share tag set between devices for blk-mq");

static int g_irqmode = NULL_IRQ_SOFTIRQ;

static int null_set_irqmode(const char *str, const struct kernel_param *kp)
{
	return null_param_store_val(str, &g_irqmode, NULL_IRQ_NONE,
					NULL_IRQ_TIMER);
}

static const struct kernel_param_ops null_irqmode_param_ops = {
	.set	= null_set_irqmode,
	.get	= param_get_int,
};

device_param_cb(irqmode, &null_irqmode_param_ops, &g_irqmode, 0444);
MODULE_PARM_DESC(irqmode, "IRQ completion handler. 0-none, 1-softirq, 2-timer");

static unsigned long g_completion_nsec = 10000;
module_param_named(completion_nsec, g_completion_nsec, ulong, 0444);
MODULE_PARM_DESC(completion_nsec, "Time in ns to complete a request in hardware. Default: 10,000ns");

static int g_hw_queue_depth = 64;
module_param_named(hw_queue_depth, g_hw_queue_depth, int, 0444);
MODULE_PARM_DESC(hw_queue_depth, "Queue depth for each hardware queue. Default: 64");

static bool g_use_per_node_hctx;
module_param_named(use_per_node_hctx, g_use_per_node_hctx, bool, 0444);
MODULE_PARM_DESC(use_per_node_hctx, "Use per-node allocation for hardware context queues. Default: false");

static bool g_zoned;
module_param_named(zoned, g_zoned, bool, S_IRUGO);
MODULE_PARM_DESC(zoned, "Make device as a host-managed zoned block device. Default: false");

static unsigned long g_zone_size = 256;
module_param_named(zone_size, g_zone_size, ulong, S_IRUGO);
MODULE_PARM_DESC(zone_size, "Zone size in MB when block device is zoned. Must be power-of-two: Default: 256");

static unsigned int g_zone_nr_conv;
module_param_named(zone_nr_conv, g_zone_nr_conv, uint, 0444);
MODULE_PARM_DESC(zone_nr_conv, "Number of conventional zones when block device is zoned. Default: 0");

static struct nullb_device *null_alloc_dev(void);
static void null_free_dev(struct nullb_device *dev);
static void null_del_dev(struct nullb *nullb);
static int null_add_dev(struct nullb_device *dev);
static void null_free_device_storage(struct nullb_device *dev, bool is_cache);

static inline struct nullb_device *to_nullb_device(struct config_item *item)
{
	return item ? container_of(item, struct nullb_device, item) : NULL;
}

static inline ssize_t nullb_device_uint_attr_show(unsigned int val, char *page)
{
	return snprintf(page, PAGE_SIZE, "%u\n", val);
}

static inline ssize_t nullb_device_ulong_attr_show(unsigned long val,
	char *page)
{
	return snprintf(page, PAGE_SIZE, "%lu\n", val);
}

static inline ssize_t nullb_device_bool_attr_show(bool val, char *page)
{
	return snprintf(page, PAGE_SIZE, "%u\n", val);
}

static ssize_t nullb_device_uint_attr_store(unsigned int *val,
	const char *page, size_t count)
{
	unsigned int tmp;
	int result;

	result = kstrtouint(page, 0, &tmp);
	if (result)
		return result;

	*val = tmp;
	return count;
}

static ssize_t nullb_device_ulong_attr_store(unsigned long *val,
	const char *page, size_t count)
{
	int result;
	unsigned long tmp;

	result = kstrtoul(page, 0, &tmp);
	if (result)
		return result;

	*val = tmp;
	return count;
}

static ssize_t nullb_device_bool_attr_store(bool *val, const char *page,
	size_t count)
{
	bool tmp;
	int result;

	result = kstrtobool(page,  &tmp);
	if (result)
		return result;

	*val = tmp;
	return count;
}

/* The following macro should only be used with TYPE = {uint, ulong, bool}. */
#define NULLB_DEVICE_ATTR(NAME, TYPE)						\
static ssize_t									\
nullb_device_##NAME##_show(struct config_item *item, char *page)		\
{										\
	return nullb_device_##TYPE##_attr_show(					\
				to_nullb_device(item)->NAME, page);		\
}										\
static ssize_t									\
nullb_device_##NAME##_store(struct config_item *item, const char *page,		\
			    size_t count)					\
{										\
	if (test_bit(NULLB_DEV_FL_CONFIGURED, &to_nullb_device(item)->flags))	\
		return -EBUSY;							\
	return nullb_device_##TYPE##_attr_store(				\
			&to_nullb_device(item)->NAME, page, count);		\
}										\
CONFIGFS_ATTR(nullb_device_, NAME);

NULLB_DEVICE_ATTR(size, ulong);
NULLB_DEVICE_ATTR(completion_nsec, ulong);
NULLB_DEVICE_ATTR(submit_queues, uint);
NULLB_DEVICE_ATTR(home_node, uint);
NULLB_DEVICE_ATTR(queue_mode, uint);
NULLB_DEVICE_ATTR(blocksize, uint);
NULLB_DEVICE_ATTR(irqmode, uint);
NULLB_DEVICE_ATTR(hw_queue_depth, uint);
NULLB_DEVICE_ATTR(index, uint);
NULLB_DEVICE_ATTR(blocking, bool);
NULLB_DEVICE_ATTR(use_per_node_hctx, bool);
NULLB_DEVICE_ATTR(memory_backed, bool);
NULLB_DEVICE_ATTR(discard, bool);
NULLB_DEVICE_ATTR(mbps, uint);
NULLB_DEVICE_ATTR(cache_size, ulong);
NULLB_DEVICE_ATTR(zoned, bool);
NULLB_DEVICE_ATTR(zone_size, ulong);
NULLB_DEVICE_ATTR(zone_nr_conv, uint);

static ssize_t nullb_device_power_show(struct config_item *item, char *page)
{
	return nullb_device_bool_attr_show(to_nullb_device(item)->power, page);
}

static ssize_t nullb_device_power_store(struct config_item *item,
				     const char *page, size_t count)
{
	struct nullb_device *dev = to_nullb_device(item);
	bool newp = false;
	ssize_t ret;

	ret = nullb_device_bool_attr_store(&newp, page, count);
	if (ret < 0)
		return ret;

	if (!dev->power && newp) {
		if (test_and_set_bit(NULLB_DEV_FL_UP, &dev->flags))
			return count;
		if (null_add_dev(dev)) {
			clear_bit(NULLB_DEV_FL_UP, &dev->flags);
			return -ENOMEM;
		}

		set_bit(NULLB_DEV_FL_CONFIGURED, &dev->flags);
		dev->power = newp;
	} else if (dev->power && !newp) {
		mutex_lock(&lock);
		dev->power = newp;
		null_del_dev(dev->nullb);
		mutex_unlock(&lock);
		clear_bit(NULLB_DEV_FL_UP, &dev->flags);
		clear_bit(NULLB_DEV_FL_CONFIGURED, &dev->flags);
	}

	return count;
}

CONFIGFS_ATTR(nullb_device_, power);

static ssize_t nullb_device_badblocks_show(struct config_item *item, char *page)
{
	struct nullb_device *t_dev = to_nullb_device(item);

	return badblocks_show(&t_dev->badblocks, page, 0);
}

static ssize_t nullb_device_badblocks_store(struct config_item *item,
				     const char *page, size_t count)
{
	struct nullb_device *t_dev = to_nullb_device(item);
	char *orig, *buf, *tmp;
	u64 start, end;
	int ret;

	orig = kstrndup(page, count, GFP_KERNEL);
	if (!orig)
		return -ENOMEM;

	buf = strstrip(orig);

	ret = -EINVAL;
	if (buf[0] != '+' && buf[0] != '-')
		goto out;
	tmp = strchr(&buf[1], '-');
	if (!tmp)
		goto out;
	*tmp = '\0';
	ret = kstrtoull(buf + 1, 0, &start);
	if (ret)
		goto out;
	ret = kstrtoull(tmp + 1, 0, &end);
	if (ret)
		goto out;
	ret = -EINVAL;
	if (start > end)
		goto out;
	/* enable badblocks */
	cmpxchg(&t_dev->badblocks.shift, -1, 0);
	if (buf[0] == '+')
		ret = badblocks_set(&t_dev->badblocks, start,
			end - start + 1, 1);
	else
		ret = badblocks_clear(&t_dev->badblocks, start,
			end - start + 1);
	if (ret == 0)
		ret = count;
out:
	kfree(orig);
	return ret;
}
CONFIGFS_ATTR(nullb_device_, badblocks);

static struct configfs_attribute *nullb_device_attrs[] = {
	&nullb_device_attr_size,
	&nullb_device_attr_completion_nsec,
	&nullb_device_attr_submit_queues,
	&nullb_device_attr_home_node,
	&nullb_device_attr_queue_mode,
	&nullb_device_attr_blocksize,
	&nullb_device_attr_irqmode,
	&nullb_device_attr_hw_queue_depth,
	&nullb_device_attr_index,
	&nullb_device_attr_blocking,
	&nullb_device_attr_use_per_node_hctx,
	&nullb_device_attr_power,
	&nullb_device_attr_memory_backed,
	&nullb_device_attr_discard,
	&nullb_device_attr_mbps,
	&nullb_device_attr_cache_size,
	&nullb_device_attr_badblocks,
	&nullb_device_attr_zoned,
	&nullb_device_attr_zone_size,
	&nullb_device_attr_zone_nr_conv,
	NULL,
};

static void nullb_device_release(struct config_item *item)
{
	struct nullb_device *dev = to_nullb_device(item);

	null_free_device_storage(dev, false);
	null_free_dev(dev);
}

static struct configfs_item_operations nullb_device_ops = {
	.release	= nullb_device_release,
};

static const struct config_item_type nullb_device_type = {
	.ct_item_ops	= &nullb_device_ops,
	.ct_attrs	= nullb_device_attrs,
	.ct_owner	= THIS_MODULE,
};

static struct
config_item *nullb_group_make_item(struct config_group *group, const char *name)
{
	struct nullb_device *dev;

	dev = null_alloc_dev();
	if (!dev)
		return ERR_PTR(-ENOMEM);

	config_item_init_type_name(&dev->item, name, &nullb_device_type);

	return &dev->item;
}

static void
nullb_group_drop_item(struct config_group *group, struct config_item *item)
{
	struct nullb_device *dev = to_nullb_device(item);

	if (test_and_clear_bit(NULLB_DEV_FL_UP, &dev->flags)) {
		mutex_lock(&lock);
		dev->power = false;
		null_del_dev(dev->nullb);
		mutex_unlock(&lock);
	}

	config_item_put(item);
}

static ssize_t memb_group_features_show(struct config_item *item, char *page)
{
	return snprintf(page, PAGE_SIZE, "memory_backed,discard,bandwidth,cache,badblocks,zoned,zone_size\n");
}

CONFIGFS_ATTR_RO(memb_group_, features);

static struct configfs_attribute *nullb_group_attrs[] = {
	&memb_group_attr_features,
	NULL,
};

static struct configfs_group_operations nullb_group_ops = {
	.make_item	= nullb_group_make_item,
	.drop_item	= nullb_group_drop_item,
};

static const struct config_item_type nullb_group_type = {
	.ct_group_ops	= &nullb_group_ops,
	.ct_attrs	= nullb_group_attrs,
	.ct_owner	= THIS_MODULE,
};

static struct configfs_subsystem nullb_subsys = {
	.su_group = {
		.cg_item = {
			.ci_namebuf = "nullb",
			.ci_type = &nullb_group_type,
		},
	},
};

static inline int null_cache_active(struct nullb *nullb)
{
	return test_bit(NULLB_DEV_FL_CACHE, &nullb->dev->flags);
}

static struct nullb_device *null_alloc_dev(void)
{
	struct nullb_device *dev;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return NULL;
	INIT_RADIX_TREE(&dev->data, GFP_ATOMIC);
	INIT_RADIX_TREE(&dev->cache, GFP_ATOMIC);
	if (badblocks_init(&dev->badblocks, 0)) {
		kfree(dev);
		return NULL;
	}

	dev->size = g_gb * 1024;
	dev->completion_nsec = g_completion_nsec;
	dev->submit_queues = g_submit_queues;
	dev->home_node = g_home_node;
	dev->queue_mode = g_queue_mode;
	dev->blocksize = g_bs;
	dev->irqmode = g_irqmode;
	dev->hw_queue_depth = g_hw_queue_depth;
	dev->blocking = g_blocking;
	dev->use_per_node_hctx = g_use_per_node_hctx;
	dev->zoned = g_zoned;
	dev->zone_size = g_zone_size;
	dev->zone_nr_conv = g_zone_nr_conv;
	return dev;
}

static void null_free_dev(struct nullb_device *dev)
{
	if (!dev)
		return;

	null_zone_exit(dev);
	badblocks_exit(&dev->badblocks);
	kfree(dev);
}

static void put_tag(struct nullb_queue *nq, unsigned int tag)
{
	clear_bit_unlock(tag, nq->tag_map);

	if (waitqueue_active(&nq->wait))
		wake_up(&nq->wait);
}

static unsigned int get_tag(struct nullb_queue *nq)
{
	unsigned int tag;

	do {
		tag = find_first_zero_bit(nq->tag_map, nq->queue_depth);
		if (tag >= nq->queue_depth)
			return -1U;
	} while (test_and_set_bit_lock(tag, nq->tag_map));

	return tag;
}

static void free_cmd(struct nullb_cmd *cmd)
{
	put_tag(cmd->nq, cmd->tag);
}

static enum hrtimer_restart null_cmd_timer_expired(struct hrtimer *timer);

static struct nullb_cmd *__alloc_cmd(struct nullb_queue *nq)
{
	struct nullb_cmd *cmd;
	unsigned int tag;

	tag = get_tag(nq);
	if (tag != -1U) {
		cmd = &nq->cmds[tag];
		cmd->tag = tag;
		cmd->nq = nq;
		if (nq->dev->irqmode == NULL_IRQ_TIMER) {
			hrtimer_init(&cmd->timer, CLOCK_MONOTONIC,
				     HRTIMER_MODE_REL);
			cmd->timer.function = null_cmd_timer_expired;
		}
		return cmd;
	}

	return NULL;
}

static struct nullb_cmd *alloc_cmd(struct nullb_queue *nq, int can_wait)
{
	struct nullb_cmd *cmd;
	DEFINE_WAIT(wait);

	cmd = __alloc_cmd(nq);
	if (cmd || !can_wait)
		return cmd;

	do {
		prepare_to_wait(&nq->wait, &wait, TASK_UNINTERRUPTIBLE);
		cmd = __alloc_cmd(nq);
		if (cmd)
			break;

		io_schedule();
	} while (1);

	finish_wait(&nq->wait, &wait);
	return cmd;
}

static void end_cmd(struct nullb_cmd *cmd)
{
	int queue_mode = cmd->nq->dev->queue_mode;

	switch (queue_mode)  {
	case NULL_Q_MQ:
		blk_mq_end_request(cmd->rq, cmd->error);
		return;
	case NULL_Q_BIO:
		cmd->bio->bi_status = cmd->error;
		bio_endio(cmd->bio);
		break;
	}

	free_cmd(cmd);
}

static enum hrtimer_restart null_cmd_timer_expired(struct hrtimer *timer)
{
	end_cmd(container_of(timer, struct nullb_cmd, timer));

	return HRTIMER_NORESTART;
}

static void null_cmd_end_timer(struct nullb_cmd *cmd)
{
	ktime_t kt = cmd->nq->dev->completion_nsec;

	hrtimer_start(&cmd->timer, kt, HRTIMER_MODE_REL);
}

static void null_complete_rq(struct request *rq)
{
	end_cmd(blk_mq_rq_to_pdu(rq));
}

static struct nullb_page *null_alloc_page(gfp_t gfp_flags)
{
	struct nullb_page *t_page;

	t_page = kmalloc(sizeof(struct nullb_page), gfp_flags);
	if (!t_page)
		goto out;

	t_page->page = alloc_pages(gfp_flags, 0);
	if (!t_page->page)
		goto out_freepage;

	memset(t_page->bitmap, 0, sizeof(t_page->bitmap));
	return t_page;
out_freepage:
	kfree(t_page);
out:
	return NULL;
}

static void null_free_page(struct nullb_page *t_page)
{
	__set_bit(NULLB_PAGE_FREE, t_page->bitmap);
	if (test_bit(NULLB_PAGE_LOCK, t_page->bitmap))
		return;
	__free_page(t_page->page);
	kfree(t_page);
}

static bool null_page_empty(struct nullb_page *page)
{
	int size = MAP_SZ - 2;

	return find_first_bit(page->bitmap, size) == size;
}

static void null_free_sector(struct nullb *nullb, sector_t sector,
	bool is_cache)
{
	unsigned int sector_bit;
	u64 idx;
	struct nullb_page *t_page, *ret;
	struct radix_tree_root *root;

	root = is_cache ? &nullb->dev->cache : &nullb->dev->data;
	idx = sector >> PAGE_SECTORS_SHIFT;
	sector_bit = (sector & SECTOR_MASK);

	t_page = radix_tree_lookup(root, idx);
	if (t_page) {
		__clear_bit(sector_bit, t_page->bitmap);

		if (null_page_empty(t_page)) {
			ret = radix_tree_delete_item(root, idx, t_page);
			WARN_ON(ret != t_page);
			null_free_page(ret);
			if (is_cache)
				nullb->dev->curr_cache -= PAGE_SIZE;
		}
	}
}

static struct nullb_page *null_radix_tree_insert(struct nullb *nullb, u64 idx,
	struct nullb_page *t_page, bool is_cache)
{
	struct radix_tree_root *root;

	root = is_cache ? &nullb->dev->cache : &nullb->dev->data;

	if (radix_tree_insert(root, idx, t_page)) {
		null_free_page(t_page);
		t_page = radix_tree_lookup(root, idx);
		WARN_ON(!t_page || t_page->page->index != idx);
	} else if (is_cache)
		nullb->dev->curr_cache += PAGE_SIZE;

	return t_page;
}

static void null_free_device_storage(struct nullb_device *dev, bool is_cache)
{
	unsigned long pos = 0;
	int nr_pages;
	struct nullb_page *ret, *t_pages[FREE_BATCH];
	struct radix_tree_root *root;

	root = is_cache ? &dev->cache : &dev->data;

	do {
		int i;

		nr_pages = radix_tree_gang_lookup(root,
				(void **)t_pages, pos, FREE_BATCH);

		for (i = 0; i < nr_pages; i++) {
			pos = t_pages[i]->page->index;
			ret = radix_tree_delete_item(root, pos, t_pages[i]);
			WARN_ON(ret != t_pages[i]);
			null_free_page(ret);
		}

		pos++;
	} while (nr_pages == FREE_BATCH);

	if (is_cache)
		dev->curr_cache = 0;
}

static struct nullb_page *__null_lookup_page(struct nullb *nullb,
	sector_t sector, bool for_write, bool is_cache)
{
	unsigned int sector_bit;
	u64 idx;
	struct nullb_page *t_page;
	struct radix_tree_root *root;

	idx = sector >> PAGE_SECTORS_SHIFT;
	sector_bit = (sector & SECTOR_MASK);

	root = is_cache ? &nullb->dev->cache : &nullb->dev->data;
	t_page = radix_tree_lookup(root, idx);
	WARN_ON(t_page && t_page->page->index != idx);

	if (t_page && (for_write || test_bit(sector_bit, t_page->bitmap)))
		return t_page;

	return NULL;
}

static struct nullb_page *null_lookup_page(struct nullb *nullb,
	sector_t sector, bool for_write, bool ignore_cache)
{
	struct nullb_page *page = NULL;

	if (!ignore_cache)
		page = __null_lookup_page(nullb, sector, for_write, true);
	if (page)
		return page;
	return __null_lookup_page(nullb, sector, for_write, false);
}

static struct nullb_page *null_insert_page(struct nullb *nullb,
					   sector_t sector, bool ignore_cache)
	__releases(&nullb->lock)
	__acquires(&nullb->lock)
{
	u64 idx;
	struct nullb_page *t_page;

	t_page = null_lookup_page(nullb, sector, true, ignore_cache);
	if (t_page)
		return t_page;

	spin_unlock_irq(&nullb->lock);

	t_page = null_alloc_page(GFP_NOIO);
	if (!t_page)
		goto out_lock;

	if (radix_tree_preload(GFP_NOIO))
		goto out_freepage;

	spin_lock_irq(&nullb->lock);
	idx = sector >> PAGE_SECTORS_SHIFT;
	t_page->page->index = idx;
	t_page = null_radix_tree_insert(nullb, idx, t_page, !ignore_cache);
	radix_tree_preload_end();

	return t_page;
out_freepage:
	null_free_page(t_page);
out_lock:
	spin_lock_irq(&nullb->lock);
	return null_lookup_page(nullb, sector, true, ignore_cache);
}

static int null_flush_cache_page(struct nullb *nullb, struct nullb_page *c_page)
{
	int i;
	unsigned int offset;
	u64 idx;
	struct nullb_page *t_page, *ret;
	void *dst, *src;

	idx = c_page->page->index;

	t_page = null_insert_page(nullb, idx << PAGE_SECTORS_SHIFT, true);

	__clear_bit(NULLB_PAGE_LOCK, c_page->bitmap);
	if (test_bit(NULLB_PAGE_FREE, c_page->bitmap)) {
		null_free_page(c_page);
		if (t_page && null_page_empty(t_page)) {
			ret = radix_tree_delete_item(&nullb->dev->data,
				idx, t_page);
			null_free_page(t_page);
		}
		return 0;
	}

	if (!t_page)
		return -ENOMEM;

	src = kmap_atomic(c_page->page);
	dst = kmap_atomic(t_page->page);

	for (i = 0; i < PAGE_SECTORS;
			i += (nullb->dev->blocksize >> SECTOR_SHIFT)) {
		if (test_bit(i, c_page->bitmap)) {
			offset = (i << SECTOR_SHIFT);
			memcpy(dst + offset, src + offset,
				nullb->dev->blocksize);
			__set_bit(i, t_page->bitmap);
		}
	}

	kunmap_atomic(dst);
	kunmap_atomic(src);

	ret = radix_tree_delete_item(&nullb->dev->cache, idx, c_page);
	null_free_page(ret);
	nullb->dev->curr_cache -= PAGE_SIZE;

	return 0;
}

static int null_make_cache_space(struct nullb *nullb, unsigned long n)
{
	int i, err, nr_pages;
	struct nullb_page *c_pages[FREE_BATCH];
	unsigned long flushed = 0, one_round;

again:
	if ((nullb->dev->cache_size * 1024 * 1024) >
	     nullb->dev->curr_cache + n || nullb->dev->curr_cache == 0)
		return 0;

	nr_pages = radix_tree_gang_lookup(&nullb->dev->cache,
			(void **)c_pages, nullb->cache_flush_pos, FREE_BATCH);
	/*
	 * nullb_flush_cache_page could unlock before using the c_pages. To
	 * avoid race, we don't allow page free
	 */
	for (i = 0; i < nr_pages; i++) {
		nullb->cache_flush_pos = c_pages[i]->page->index;
		/*
		 * We found the page which is being flushed to disk by other
		 * threads
		 */
		if (test_bit(NULLB_PAGE_LOCK, c_pages[i]->bitmap))
			c_pages[i] = NULL;
		else
			__set_bit(NULLB_PAGE_LOCK, c_pages[i]->bitmap);
	}

	one_round = 0;
	for (i = 0; i < nr_pages; i++) {
		if (c_pages[i] == NULL)
			continue;
		err = null_flush_cache_page(nullb, c_pages[i]);
		if (err)
			return err;
		one_round++;
	}
	flushed += one_round << PAGE_SHIFT;

	if (n > flushed) {
		if (nr_pages == 0)
			nullb->cache_flush_pos = 0;
		if (one_round == 0) {
			/* give other threads a chance */
			spin_unlock_irq(&nullb->lock);
			spin_lock_irq(&nullb->lock);
		}
		goto again;
	}
	return 0;
}

static int copy_to_nullb(struct nullb *nullb, struct page *source,
	unsigned int off, sector_t sector, size_t n, bool is_fua)
{
	size_t temp, count = 0;
	unsigned int offset;
	struct nullb_page *t_page;
	void *dst, *src;

	while (count < n) {
		temp = min_t(size_t, nullb->dev->blocksize, n - count);

		if (null_cache_active(nullb) && !is_fua)
			null_make_cache_space(nullb, PAGE_SIZE);

		offset = (sector & SECTOR_MASK) << SECTOR_SHIFT;
		t_page = null_insert_page(nullb, sector,
			!null_cache_active(nullb) || is_fua);
		if (!t_page)
			return -ENOSPC;

		src = kmap_atomic(source);
		dst = kmap_atomic(t_page->page);
		memcpy(dst + offset, src + off + count, temp);
		kunmap_atomic(dst);
		kunmap_atomic(src);

		__set_bit(sector & SECTOR_MASK, t_page->bitmap);

		if (is_fua)
			null_free_sector(nullb, sector, true);

		count += temp;
		sector += temp >> SECTOR_SHIFT;
	}
	return 0;
}

static int copy_from_nullb(struct nullb *nullb, struct page *dest,
	unsigned int off, sector_t sector, size_t n)
{
	size_t temp, count = 0;
	unsigned int offset;
	struct nullb_page *t_page;
	void *dst, *src;

	while (count < n) {
		temp = min_t(size_t, nullb->dev->blocksize, n - count);

		offset = (sector & SECTOR_MASK) << SECTOR_SHIFT;
		t_page = null_lookup_page(nullb, sector, false,
			!null_cache_active(nullb));

		dst = kmap_atomic(dest);
		if (!t_page) {
			memset(dst + off + count, 0, temp);
			goto next;
		}
		src = kmap_atomic(t_page->page);
		memcpy(dst + off + count, src + offset, temp);
		kunmap_atomic(src);
next:
		kunmap_atomic(dst);

		count += temp;
		sector += temp >> SECTOR_SHIFT;
	}
	return 0;
}

static void null_handle_discard(struct nullb *nullb, sector_t sector, size_t n)
{
	size_t temp;

	spin_lock_irq(&nullb->lock);
	while (n > 0) {
		temp = min_t(size_t, n, nullb->dev->blocksize);
		null_free_sector(nullb, sector, false);
		if (null_cache_active(nullb))
			null_free_sector(nullb, sector, true);
		sector += temp >> SECTOR_SHIFT;
		n -= temp;
	}
	spin_unlock_irq(&nullb->lock);
}

static int null_handle_flush(struct nullb *nullb)
{
	int err;

	if (!null_cache_active(nullb))
		return 0;

	spin_lock_irq(&nullb->lock);
	while (true) {
		err = null_make_cache_space(nullb,
			nullb->dev->cache_size * 1024 * 1024);
		if (err || nullb->dev->curr_cache == 0)
			break;
	}

	WARN_ON(!radix_tree_empty(&nullb->dev->cache));
	spin_unlock_irq(&nullb->lock);
	return err;
}

static int null_transfer(struct nullb *nullb, struct page *page,
	unsigned int len, unsigned int off, bool is_write, sector_t sector,
	bool is_fua)
{
	int err = 0;

	if (!is_write) {
		err = copy_from_nullb(nullb, page, off, sector, len);
		flush_dcache_page(page);
	} else {
		flush_dcache_page(page);
		err = copy_to_nullb(nullb, page, off, sector, len, is_fua);
	}

	return err;
}

static int null_handle_rq(struct nullb_cmd *cmd)
{
	struct request *rq = cmd->rq;
	struct nullb *nullb = cmd->nq->dev->nullb;
	int err;
	unsigned int len;
	sector_t sector;
	struct req_iterator iter;
	struct bio_vec bvec;

	sector = blk_rq_pos(rq);

	if (req_op(rq) == REQ_OP_DISCARD) {
		null_handle_discard(nullb, sector, blk_rq_bytes(rq));
		return 0;
	}

	spin_lock_irq(&nullb->lock);
	rq_for_each_segment(bvec, rq, iter) {
		len = bvec.bv_len;
		err = null_transfer(nullb, bvec.bv_page, len, bvec.bv_offset,
				     op_is_write(req_op(rq)), sector,
				     req_op(rq) & REQ_FUA);
		if (err) {
			spin_unlock_irq(&nullb->lock);
			return err;
		}
		sector += len >> SECTOR_SHIFT;
	}
	spin_unlock_irq(&nullb->lock);

	return 0;
}

static int null_handle_bio(struct nullb_cmd *cmd)
{
	struct bio *bio = cmd->bio;
	struct nullb *nullb = cmd->nq->dev->nullb;
	int err;
	unsigned int len;
	sector_t sector;
	struct bio_vec bvec;
	struct bvec_iter iter;

	sector = bio->bi_iter.bi_sector;

	if (bio_op(bio) == REQ_OP_DISCARD) {
		null_handle_discard(nullb, sector,
			bio_sectors(bio) << SECTOR_SHIFT);
		return 0;
	}

	spin_lock_irq(&nullb->lock);
	bio_for_each_segment(bvec, bio, iter) {
		len = bvec.bv_len;
		err = null_transfer(nullb, bvec.bv_page, len, bvec.bv_offset,
				     op_is_write(bio_op(bio)), sector,
				     bio_op(bio) & REQ_FUA);
		if (err) {
			spin_unlock_irq(&nullb->lock);
			return err;
		}
		sector += len >> SECTOR_SHIFT;
	}
	spin_unlock_irq(&nullb->lock);
	return 0;
}

static void null_stop_queue(struct nullb *nullb)
{
	struct request_queue *q = nullb->q;

	if (nullb->dev->queue_mode == NULL_Q_MQ)
		blk_mq_stop_hw_queues(q);
}

static void null_restart_queue_async(struct nullb *nullb)
{
	struct request_queue *q = nullb->q;

	if (nullb->dev->queue_mode == NULL_Q_MQ)
		blk_mq_start_stopped_hw_queues(q, true);
}

static blk_status_t null_handle_cmd(struct nullb_cmd *cmd)
{
	struct nullb_device *dev = cmd->nq->dev;
	struct nullb *nullb = dev->nullb;
	int err = 0;

	if (test_bit(NULLB_DEV_FL_THROTTLED, &dev->flags)) {
		struct request *rq = cmd->rq;

		if (!hrtimer_active(&nullb->bw_timer))
			hrtimer_restart(&nullb->bw_timer);

		if (atomic_long_sub_return(blk_rq_bytes(rq),
				&nullb->cur_bytes) < 0) {
			null_stop_queue(nullb);
			/* race with timer */
			if (atomic_long_read(&nullb->cur_bytes) > 0)
				null_restart_queue_async(nullb);
			/* requeue request */
			return BLK_STS_DEV_RESOURCE;
		}
	}

	if (nullb->dev->badblocks.shift != -1) {
		int bad_sectors;
		sector_t sector, size, first_bad;
		bool is_flush = true;

		if (dev->queue_mode == NULL_Q_BIO &&
				bio_op(cmd->bio) != REQ_OP_FLUSH) {
			is_flush = false;
			sector = cmd->bio->bi_iter.bi_sector;
			size = bio_sectors(cmd->bio);
		}
		if (dev->queue_mode != NULL_Q_BIO &&
				req_op(cmd->rq) != REQ_OP_FLUSH) {
			is_flush = false;
			sector = blk_rq_pos(cmd->rq);
			size = blk_rq_sectors(cmd->rq);
		}
		if (!is_flush && badblocks_check(&nullb->dev->badblocks, sector,
				size, &first_bad, &bad_sectors)) {
			cmd->error = BLK_STS_IOERR;
			goto out;
		}
	}

	if (dev->memory_backed) {
		if (dev->queue_mode == NULL_Q_BIO) {
			if (bio_op(cmd->bio) == REQ_OP_FLUSH)
				err = null_handle_flush(nullb);
			else
				err = null_handle_bio(cmd);
		} else {
			if (req_op(cmd->rq) == REQ_OP_FLUSH)
				err = null_handle_flush(nullb);
			else
				err = null_handle_rq(cmd);
		}
	}
	cmd->error = errno_to_blk_status(err);

	if (!cmd->error && dev->zoned) {
		sector_t sector;
		unsigned int nr_sectors;
		int op;

		if (dev->queue_mode == NULL_Q_BIO) {
			op = bio_op(cmd->bio);
			sector = cmd->bio->bi_iter.bi_sector;
			nr_sectors = cmd->bio->bi_iter.bi_size >> 9;
		} else {
			op = req_op(cmd->rq);
			sector = blk_rq_pos(cmd->rq);
			nr_sectors = blk_rq_sectors(cmd->rq);
		}

		if (op == REQ_OP_WRITE)
			null_zone_write(cmd, sector, nr_sectors);
		else if (op == REQ_OP_ZONE_RESET)
			null_zone_reset(cmd, sector);
	}
out:
	/* Complete IO by inline, softirq or timer */
	switch (dev->irqmode) {
	case NULL_IRQ_SOFTIRQ:
		switch (dev->queue_mode)  {
		case NULL_Q_MQ:
			blk_mq_complete_request(cmd->rq);
			break;
		case NULL_Q_BIO:
			/*
			 * XXX: no proper submitting cpu information available.
			 */
			end_cmd(cmd);
			break;
		}
		break;
	case NULL_IRQ_NONE:
		end_cmd(cmd);
		break;
	case NULL_IRQ_TIMER:
		null_cmd_end_timer(cmd);
		break;
	}
	return BLK_STS_OK;
}

static enum hrtimer_restart nullb_bwtimer_fn(struct hrtimer *timer)
{
	struct nullb *nullb = container_of(timer, struct nullb, bw_timer);
	ktime_t timer_interval = ktime_set(0, TIMER_INTERVAL);
	unsigned int mbps = nullb->dev->mbps;

	if (atomic_long_read(&nullb->cur_bytes) == mb_per_tick(mbps))
		return HRTIMER_NORESTART;

	atomic_long_set(&nullb->cur_bytes, mb_per_tick(mbps));
	null_restart_queue_async(nullb);

	hrtimer_forward_now(&nullb->bw_timer, timer_interval);

	return HRTIMER_RESTART;
}

static void nullb_setup_bwtimer(struct nullb *nullb)
{
	ktime_t timer_interval = ktime_set(0, TIMER_INTERVAL);

	hrtimer_init(&nullb->bw_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	nullb->bw_timer.function = nullb_bwtimer_fn;
	atomic_long_set(&nullb->cur_bytes, mb_per_tick(nullb->dev->mbps));
	hrtimer_start(&nullb->bw_timer, timer_interval, HRTIMER_MODE_REL);
}

static struct nullb_queue *nullb_to_queue(struct nullb *nullb)
{
	int index = 0;

	if (nullb->nr_queues != 1)
		index = raw_smp_processor_id() / ((nr_cpu_ids + nullb->nr_queues - 1) / nullb->nr_queues);

	return &nullb->queues[index];
}

static blk_qc_t null_queue_bio(struct request_queue *q, struct bio *bio)
{
	struct nullb *nullb = q->queuedata;
	struct nullb_queue *nq = nullb_to_queue(nullb);
	struct nullb_cmd *cmd;

	cmd = alloc_cmd(nq, 1);
	cmd->bio = bio;

	null_handle_cmd(cmd);
	return BLK_QC_T_NONE;
}

static bool should_timeout_request(struct request *rq)
{
#ifdef CONFIG_BLK_DEV_NULL_BLK_FAULT_INJECTION
	if (g_timeout_str[0])
		return should_fail(&null_timeout_attr, 1);
#endif
	return false;
}

static bool should_requeue_request(struct request *rq)
{
#ifdef CONFIG_BLK_DEV_NULL_BLK_FAULT_INJECTION
	if (g_requeue_str[0])
		return should_fail(&null_requeue_attr, 1);
#endif
	return false;
}

static enum blk_eh_timer_return null_timeout_rq(struct request *rq, bool res)
{
	pr_info("null: rq %p timed out\n", rq);
	blk_mq_complete_request(rq);
	return BLK_EH_DONE;
}

static blk_status_t null_queue_rq(struct blk_mq_hw_ctx *hctx,
			 const struct blk_mq_queue_data *bd)
{
	struct nullb_cmd *cmd = blk_mq_rq_to_pdu(bd->rq);
	struct nullb_queue *nq = hctx->driver_data;

	might_sleep_if(hctx->flags & BLK_MQ_F_BLOCKING);

	if (nq->dev->irqmode == NULL_IRQ_TIMER) {
		hrtimer_init(&cmd->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		cmd->timer.function = null_cmd_timer_expired;
	}
	cmd->rq = bd->rq;
	cmd->nq = nq;

	blk_mq_start_request(bd->rq);

	if (should_requeue_request(bd->rq)) {
		/*
		 * Alternate between hitting the core BUSY path, and the
		 * driver driven requeue path
		 */
		nq->requeue_selection++;
		if (nq->requeue_selection & 1)
			return BLK_STS_RESOURCE;
		else {
			blk_mq_requeue_request(bd->rq, true);
			return BLK_STS_OK;
		}
	}
	if (should_timeout_request(bd->rq))
		return BLK_STS_OK;

	return null_handle_cmd(cmd);
}

static const struct blk_mq_ops null_mq_ops = {
	.queue_rq       = null_queue_rq,
	.complete	= null_complete_rq,
	.timeout	= null_timeout_rq,
};

static void cleanup_queue(struct nullb_queue *nq)
{
	kfree(nq->tag_map);
	kfree(nq->cmds);
}

static void cleanup_queues(struct nullb *nullb)
{
	int i;

	for (i = 0; i < nullb->nr_queues; i++)
		cleanup_queue(&nullb->queues[i]);

	kfree(nullb->queues);
}

static void null_del_dev(struct nullb *nullb)
{
	struct nullb_device *dev = nullb->dev;

	ida_simple_remove(&nullb_indexes, nullb->index);

	list_del_init(&nullb->list);

	del_gendisk(nullb->disk);

	if (test_bit(NULLB_DEV_FL_THROTTLED, &nullb->dev->flags)) {
		hrtimer_cancel(&nullb->bw_timer);
		atomic_long_set(&nullb->cur_bytes, LONG_MAX);
		null_restart_queue_async(nullb);
	}

	blk_cleanup_queue(nullb->q);
	if (dev->queue_mode == NULL_Q_MQ &&
	    nullb->tag_set == &nullb->__tag_set)
		blk_mq_free_tag_set(nullb->tag_set);
	put_disk(nullb->disk);
	cleanup_queues(nullb);
	if (null_cache_active(nullb))
		null_free_device_storage(nullb->dev, true);
	kfree(nullb);
	dev->nullb = NULL;
}

static void null_config_discard(struct nullb *nullb)
{
	if (nullb->dev->discard == false)
		return;
	nullb->q->limits.discard_granularity = nullb->dev->blocksize;
	nullb->q->limits.discard_alignment = nullb->dev->blocksize;
	blk_queue_max_discard_sectors(nullb->q, UINT_MAX >> 9);
	blk_queue_flag_set(QUEUE_FLAG_DISCARD, nullb->q);
}

static int null_open(struct block_device *bdev, fmode_t mode)
{
	return 0;
}

static void null_release(struct gendisk *disk, fmode_t mode)
{
}

static const struct block_device_operations null_fops = {
	.owner =	THIS_MODULE,
	.open =		null_open,
	.release =	null_release,
	.report_zones =	null_zone_report,
};

static void null_init_queue(struct nullb *nullb, struct nullb_queue *nq)
{
	BUG_ON(!nullb);
	BUG_ON(!nq);

	init_waitqueue_head(&nq->wait);
	nq->queue_depth = nullb->queue_depth;
	nq->dev = nullb->dev;
}

static void null_init_queues(struct nullb *nullb)
{
	struct request_queue *q = nullb->q;
	struct blk_mq_hw_ctx *hctx;
	struct nullb_queue *nq;
	int i;

	queue_for_each_hw_ctx(q, hctx, i) {
		if (!hctx->nr_ctx || !hctx->tags)
			continue;
		nq = &nullb->queues[i];
		hctx->driver_data = nq;
		null_init_queue(nullb, nq);
		nullb->nr_queues++;
	}
}

static int setup_commands(struct nullb_queue *nq)
{
	struct nullb_cmd *cmd;
	int i, tag_size;

	nq->cmds = kcalloc(nq->queue_depth, sizeof(*cmd), GFP_KERNEL);
	if (!nq->cmds)
		return -ENOMEM;

	tag_size = ALIGN(nq->queue_depth, BITS_PER_LONG) / BITS_PER_LONG;
	nq->tag_map = kcalloc(tag_size, sizeof(unsigned long), GFP_KERNEL);
	if (!nq->tag_map) {
		kfree(nq->cmds);
		return -ENOMEM;
	}

	for (i = 0; i < nq->queue_depth; i++) {
		cmd = &nq->cmds[i];
		INIT_LIST_HEAD(&cmd->list);
		cmd->ll_list.next = NULL;
		cmd->tag = -1U;
	}

	return 0;
}

static int setup_queues(struct nullb *nullb)
{
	nullb->queues = kcalloc(nullb->dev->submit_queues,
				sizeof(struct nullb_queue),
				GFP_KERNEL);
	if (!nullb->queues)
		return -ENOMEM;

	nullb->nr_queues = 0;
	nullb->queue_depth = nullb->dev->hw_queue_depth;

	return 0;
}

static int init_driver_queues(struct nullb *nullb)
{
	struct nullb_queue *nq;
	int i, ret = 0;

	for (i = 0; i < nullb->dev->submit_queues; i++) {
		nq = &nullb->queues[i];

		null_init_queue(nullb, nq);

		ret = setup_commands(nq);
		if (ret)
			return ret;
		nullb->nr_queues++;
	}
	return 0;
}

static int null_gendisk_register(struct nullb *nullb)
{
	struct gendisk *disk;
	sector_t size;

	disk = nullb->disk = alloc_disk_node(1, nullb->dev->home_node);
	if (!disk)
		return -ENOMEM;
	size = (sector_t)nullb->dev->size * 1024 * 1024ULL;
	set_capacity(disk, size >> 9);

	disk->flags |= GENHD_FL_EXT_DEVT | GENHD_FL_SUPPRESS_PARTITION_INFO;
	disk->major		= null_major;
	disk->first_minor	= nullb->index;
	disk->fops		= &null_fops;
	disk->private_data	= nullb;
	disk->queue		= nullb->q;
	strncpy(disk->disk_name, nullb->disk_name, DISK_NAME_LEN);

	if (nullb->dev->zoned) {
		int ret = blk_revalidate_disk_zones(disk);

		if (ret != 0)
			return ret;
	}

	add_disk(disk);
	return 0;
}

static int null_init_tag_set(struct nullb *nullb, struct blk_mq_tag_set *set)
{
	set->ops = &null_mq_ops;
	set->nr_hw_queues = nullb ? nullb->dev->submit_queues :
						g_submit_queues;
	set->queue_depth = nullb ? nullb->dev->hw_queue_depth :
						g_hw_queue_depth;
	set->numa_node = nullb ? nullb->dev->home_node : g_home_node;
	set->cmd_size	= sizeof(struct nullb_cmd);
	set->flags = BLK_MQ_F_SHOULD_MERGE;
	if (g_no_sched)
		set->flags |= BLK_MQ_F_NO_SCHED;
	set->driver_data = NULL;

	if ((nullb && nullb->dev->blocking) || g_blocking)
		set->flags |= BLK_MQ_F_BLOCKING;

	return blk_mq_alloc_tag_set(set);
}

static void null_validate_conf(struct nullb_device *dev)
{
	dev->blocksize = round_down(dev->blocksize, 512);
	dev->blocksize = clamp_t(unsigned int, dev->blocksize, 512, 4096);

	if (dev->queue_mode == NULL_Q_MQ && dev->use_per_node_hctx) {
		if (dev->submit_queues != nr_online_nodes)
			dev->submit_queues = nr_online_nodes;
	} else if (dev->submit_queues > nr_cpu_ids)
		dev->submit_queues = nr_cpu_ids;
	else if (dev->submit_queues == 0)
		dev->submit_queues = 1;

	dev->queue_mode = min_t(unsigned int, dev->queue_mode, NULL_Q_MQ);
	dev->irqmode = min_t(unsigned int, dev->irqmode, NULL_IRQ_TIMER);

	/* Do memory allocation, so set blocking */
	if (dev->memory_backed)
		dev->blocking = true;
	else /* cache is meaningless */
		dev->cache_size = 0;
	dev->cache_size = min_t(unsigned long, ULONG_MAX / 1024 / 1024,
						dev->cache_size);
	dev->mbps = min_t(unsigned int, 1024 * 40, dev->mbps);
	/* can not stop a queue */
	if (dev->queue_mode == NULL_Q_BIO)
		dev->mbps = 0;
}

#ifdef CONFIG_BLK_DEV_NULL_BLK_FAULT_INJECTION
static bool __null_setup_fault(struct fault_attr *attr, char *str)
{
	if (!str[0])
		return true;

	if (!setup_fault_attr(attr, str))
		return false;

	attr->verbose = 0;
	return true;
}
#endif

static bool null_setup_fault(void)
{
#ifdef CONFIG_BLK_DEV_NULL_BLK_FAULT_INJECTION
	if (!__null_setup_fault(&null_timeout_attr, g_timeout_str))
		return false;
	if (!__null_setup_fault(&null_requeue_attr, g_requeue_str))
		return false;
#endif
	return true;
}

static int null_add_dev(struct nullb_device *dev)
{
	struct nullb *nullb;
	int rv;

	null_validate_conf(dev);

	nullb = kzalloc_node(sizeof(*nullb), GFP_KERNEL, dev->home_node);
	if (!nullb) {
		rv = -ENOMEM;
		goto out;
	}
	nullb->dev = dev;
	dev->nullb = nullb;

	spin_lock_init(&nullb->lock);

	rv = setup_queues(nullb);
	if (rv)
		goto out_free_nullb;

	if (dev->queue_mode == NULL_Q_MQ) {
		if (shared_tags) {
			nullb->tag_set = &tag_set;
			rv = 0;
		} else {
			nullb->tag_set = &nullb->__tag_set;
			rv = null_init_tag_set(nullb, nullb->tag_set);
		}

		if (rv)
			goto out_cleanup_queues;

		if (!null_setup_fault())
			goto out_cleanup_queues;

		nullb->tag_set->timeout = 5 * HZ;
		nullb->q = blk_mq_init_queue(nullb->tag_set);
		if (IS_ERR(nullb->q)) {
			rv = -ENOMEM;
			goto out_cleanup_tags;
		}
		null_init_queues(nullb);
	} else if (dev->queue_mode == NULL_Q_BIO) {
		nullb->q = blk_alloc_queue_node(GFP_KERNEL, dev->home_node,
						NULL);
		if (!nullb->q) {
			rv = -ENOMEM;
			goto out_cleanup_queues;
		}
		blk_queue_make_request(nullb->q, null_queue_bio);
		rv = init_driver_queues(nullb);
		if (rv)
			goto out_cleanup_blk_queue;
	}

	if (dev->mbps) {
		set_bit(NULLB_DEV_FL_THROTTLED, &dev->flags);
		nullb_setup_bwtimer(nullb);
	}

	if (dev->cache_size > 0) {
		set_bit(NULLB_DEV_FL_CACHE, &nullb->dev->flags);
		blk_queue_write_cache(nullb->q, true, true);
		blk_queue_flush_queueable(nullb->q, true);
	}

	if (dev->zoned) {
		rv = null_zone_init(dev);
		if (rv)
			goto out_cleanup_blk_queue;

		blk_queue_chunk_sectors(nullb->q, dev->zone_size_sects);
		nullb->q->limits.zoned = BLK_ZONED_HM;
	}

	nullb->q->queuedata = nullb;
	blk_queue_flag_set(QUEUE_FLAG_NONROT, nullb->q);
	blk_queue_flag_clear(QUEUE_FLAG_ADD_RANDOM, nullb->q);

	mutex_lock(&lock);
	nullb->index = ida_simple_get(&nullb_indexes, 0, 0, GFP_KERNEL);
	dev->index = nullb->index;
	mutex_unlock(&lock);

	blk_queue_logical_block_size(nullb->q, dev->blocksize);
	blk_queue_physical_block_size(nullb->q, dev->blocksize);

	null_config_discard(nullb);

	sprintf(nullb->disk_name, "nullb%d", nullb->index);

	rv = null_gendisk_register(nullb);
	if (rv)
		goto out_cleanup_zone;

	mutex_lock(&lock);
	list_add_tail(&nullb->list, &nullb_list);
	mutex_unlock(&lock);

	return 0;
out_cleanup_zone:
	if (dev->zoned)
		null_zone_exit(dev);
out_cleanup_blk_queue:
	blk_cleanup_queue(nullb->q);
out_cleanup_tags:
	if (dev->queue_mode == NULL_Q_MQ && nullb->tag_set == &nullb->__tag_set)
		blk_mq_free_tag_set(nullb->tag_set);
out_cleanup_queues:
	cleanup_queues(nullb);
out_free_nullb:
	kfree(nullb);
out:
	return rv;
}

static int __init null_init(void)
{
	int ret = 0;
	unsigned int i;
	struct nullb *nullb;
	struct nullb_device *dev;

	if (g_bs > PAGE_SIZE) {
		pr_warn("null_blk: invalid block size\n");
		pr_warn("null_blk: defaults block size to %lu\n", PAGE_SIZE);
		g_bs = PAGE_SIZE;
	}

	if (!is_power_of_2(g_zone_size)) {
		pr_err("null_blk: zone_size must be power-of-two\n");
		return -EINVAL;
	}

	if (g_queue_mode == NULL_Q_RQ) {
		pr_err("null_blk: legacy IO path no longer available\n");
		return -EINVAL;
	}
	if (g_queue_mode == NULL_Q_MQ && g_use_per_node_hctx) {
		if (g_submit_queues != nr_online_nodes) {
			pr_warn("null_blk: submit_queues param is set to %u.\n",
							nr_online_nodes);
			g_submit_queues = nr_online_nodes;
		}
	} else if (g_submit_queues > nr_cpu_ids)
		g_submit_queues = nr_cpu_ids;
	else if (g_submit_queues <= 0)
		g_submit_queues = 1;

	if (g_queue_mode == NULL_Q_MQ && shared_tags) {
		ret = null_init_tag_set(NULL, &tag_set);
		if (ret)
			return ret;
	}

	config_group_init(&nullb_subsys.su_group);
	mutex_init(&nullb_subsys.su_mutex);

	ret = configfs_register_subsystem(&nullb_subsys);
	if (ret)
		goto err_tagset;

	mutex_init(&lock);

	null_major = register_blkdev(0, "nullb");
	if (null_major < 0) {
		ret = null_major;
		goto err_conf;
	}

	for (i = 0; i < nr_devices; i++) {
		dev = null_alloc_dev();
		if (!dev) {
			ret = -ENOMEM;
			goto err_dev;
		}
		ret = null_add_dev(dev);
		if (ret) {
			null_free_dev(dev);
			goto err_dev;
		}
	}

	pr_info("null: module loaded\n");
	return 0;

err_dev:
	while (!list_empty(&nullb_list)) {
		nullb = list_entry(nullb_list.next, struct nullb, list);
		dev = nullb->dev;
		null_del_dev(nullb);
		null_free_dev(dev);
	}
	unregister_blkdev(null_major, "nullb");
err_conf:
	configfs_unregister_subsystem(&nullb_subsys);
err_tagset:
	if (g_queue_mode == NULL_Q_MQ && shared_tags)
		blk_mq_free_tag_set(&tag_set);
	return ret;
}

static void __exit null_exit(void)
{
	struct nullb *nullb;

	configfs_unregister_subsystem(&nullb_subsys);

	unregister_blkdev(null_major, "nullb");

	mutex_lock(&lock);
	while (!list_empty(&nullb_list)) {
		struct nullb_device *dev;

		nullb = list_entry(nullb_list.next, struct nullb, list);
		dev = nullb->dev;
		null_del_dev(nullb);
		null_free_dev(dev);
	}
	mutex_unlock(&lock);

	if (g_queue_mode == NULL_Q_MQ && shared_tags)
		blk_mq_free_tag_set(&tag_set);
}

module_init(null_init);
module_exit(null_exit);

MODULE_AUTHOR("Jens Axboe <axboe@kernel.dk>");
MODULE_LICENSE("GPL");
