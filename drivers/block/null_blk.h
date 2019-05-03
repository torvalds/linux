/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __BLK_NULL_BLK_H
#define __BLK_NULL_BLK_H

#include <linux/blkdev.h>
#include <linux/slab.h>
#include <linux/blk-mq.h>
#include <linux/hrtimer.h>
#include <linux/configfs.h>
#include <linux/badblocks.h>
#include <linux/fault-inject.h>

struct nullb_cmd {
	struct list_head list;
	struct llist_node ll_list;
	struct __call_single_data csd;
	struct request *rq;
	struct bio *bio;
	unsigned int tag;
	blk_status_t error;
	struct nullb_queue *nq;
	struct hrtimer timer;
};

struct nullb_queue {
	unsigned long *tag_map;
	wait_queue_head_t wait;
	unsigned int queue_depth;
	struct nullb_device *dev;
	unsigned int requeue_selection;

	struct nullb_cmd *cmds;
};

struct nullb_device {
	struct nullb *nullb;
	struct config_item item;
	struct radix_tree_root data; /* data stored in the disk */
	struct radix_tree_root cache; /* disk cache data */
	unsigned long flags; /* device flags */
	unsigned int curr_cache;
	struct badblocks badblocks;

	unsigned int nr_zones;
	struct blk_zone *zones;
	sector_t zone_size_sects;

	unsigned long size; /* device size in MB */
	unsigned long completion_nsec; /* time in ns to complete a request */
	unsigned long cache_size; /* disk cache size in MB */
	unsigned long zone_size; /* zone size in MB if device is zoned */
	unsigned int zone_nr_conv; /* number of conventional zones */
	unsigned int submit_queues; /* number of submission queues */
	unsigned int home_node; /* home node for the device */
	unsigned int queue_mode; /* block interface */
	unsigned int blocksize; /* block size */
	unsigned int irqmode; /* IRQ completion handler */
	unsigned int hw_queue_depth; /* queue depth */
	unsigned int index; /* index of the disk, only valid with a disk */
	unsigned int mbps; /* Bandwidth throttle cap (in MB/s) */
	bool blocking; /* blocking blk-mq device */
	bool use_per_node_hctx; /* use per-node allocation for hardware context */
	bool power; /* power on/off the device */
	bool memory_backed; /* if data is stored in memory */
	bool discard; /* if support discard */
	bool zoned; /* if device is zoned */
};

struct nullb {
	struct nullb_device *dev;
	struct list_head list;
	unsigned int index;
	struct request_queue *q;
	struct gendisk *disk;
	struct blk_mq_tag_set *tag_set;
	struct blk_mq_tag_set __tag_set;
	unsigned int queue_depth;
	atomic_long_t cur_bytes;
	struct hrtimer bw_timer;
	unsigned long cache_flush_pos;
	spinlock_t lock;

	struct nullb_queue *queues;
	unsigned int nr_queues;
	char disk_name[DISK_NAME_LEN];
};

#ifdef CONFIG_BLK_DEV_ZONED
int null_zone_init(struct nullb_device *dev);
void null_zone_exit(struct nullb_device *dev);
int null_zone_report(struct gendisk *disk, sector_t sector,
		     struct blk_zone *zones, unsigned int *nr_zones,
		     gfp_t gfp_mask);
void null_zone_write(struct nullb_cmd *cmd, sector_t sector,
			unsigned int nr_sectors);
void null_zone_reset(struct nullb_cmd *cmd, sector_t sector);
#else
static inline int null_zone_init(struct nullb_device *dev)
{
	pr_err("null_blk: CONFIG_BLK_DEV_ZONED not enabled\n");
	return -EINVAL;
}
static inline void null_zone_exit(struct nullb_device *dev) {}
static inline int null_zone_report(struct gendisk *disk, sector_t sector,
				   struct blk_zone *zones,
				   unsigned int *nr_zones, gfp_t gfp_mask)
{
	return -EOPNOTSUPP;
}
static inline void null_zone_write(struct nullb_cmd *cmd, sector_t sector,
				   unsigned int nr_sectors)
{
}
static inline void null_zone_reset(struct nullb_cmd *cmd, sector_t sector) {}
#endif /* CONFIG_BLK_DEV_ZONED */
#endif /* __NULL_BLK_H */
