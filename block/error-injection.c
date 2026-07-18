// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2026 Christoph Hellwig.
 */
#include <linux/debugfs.h>
#include <linux/blkdev.h>
#include <linux/parser.h>
#include <linux/seq_file.h>
#include "blk.h"
#include "error-injection.h"

struct blk_error_inject {
	struct list_head		entry;
	sector_t			start;
	sector_t			end;
	enum req_op			op;
	blk_status_t			status;

	/* only inject every 1 / chance times */
	unsigned int			chance;
};

DEFINE_STATIC_KEY_FALSE(blk_error_injection_enabled);

bool __blk_error_inject(struct bio *bio)
{
	struct gendisk *disk = bio->bi_bdev->bd_disk;
	struct blk_error_inject *inj;

	rcu_read_lock();
	list_for_each_entry_rcu(inj, &disk->error_injection_list, entry) {
		if (bio_op(bio) != inj->op)
			continue;
		/*
		 * This never matches 0-sized bios like empty WRITEs with
		 * REQ_PREFLUSH or ZONE_RESET_ALL.  While adding a special case
		 * for them would be trivial, that means any WRITE rule would
		 * trigger for flushes.  So before we can make this work
		 * properly, we'll need to start using REQ_OP_FLUSH for pure
		 * flushes at the bio level like we already do in blk-mq.
		 */
		if (bio->bi_iter.bi_sector > inj->end ||
		    bio_end_sector(bio) <= inj->start)
			continue;
		if (inj->chance > 1 && (get_random_u32() % inj->chance) != 0)
			continue;

		pr_info_ratelimited("%pg: injecting %s error for %s at sector %llu:%u\n",
				disk->part0, blk_status_to_str(inj->status),
				blk_op_str(inj->op), bio->bi_iter.bi_sector,
				bio_sectors(bio));
		bio->bi_status = inj->status;
		rcu_read_unlock();
		bio_endio(bio);
		return true;
	}
	rcu_read_unlock();
	return false;
}

static int error_inject_add(struct gendisk *disk, enum req_op op,
		sector_t start, u64 nr_sectors, blk_status_t status,
		unsigned int chance)
{
	struct blk_error_inject *inj;
	int error = -EINVAL;

	if (op == REQ_OP_LAST)
		return -EINVAL;
	if (status == BLK_STS_OK)
		return -EINVAL;

	inj = kzalloc_obj(*inj);
	if (!inj)
		return -ENOMEM;

	if (nr_sectors) {
		if (U64_MAX - nr_sectors < start)
			goto out_free_inj;
		inj->end = start + nr_sectors - 1;
	} else {
		inj->end = U64_MAX;
	}

	inj->op = op;
	inj->start = start;
	inj->status = status;
	inj->chance = chance;

	pr_debug_ratelimited("%pg: adding %s injection for %s at sector %llu:%llu\n",
			disk->part0, blk_status_to_str(status),
			blk_op_str(op),
			start, nr_sectors);

	/*
	 * Add to the front of the list so that newer entries can partially
	 * override other entries.  This also intentionally allows duplicate
	 * entries as there is no real reason to reject them.
	 */
	mutex_lock(&disk->error_injection_lock);
	if (!disk_live(disk)) {
		mutex_unlock(&disk->error_injection_lock);
		error = -ENODEV;
		goto out_free_inj;
	}
	if (list_empty(&disk->error_injection_list))
		static_branch_inc(&blk_error_injection_enabled);
	list_add_rcu(&inj->entry, &disk->error_injection_list);
	set_bit(GD_ERROR_INJECT, &disk->state);
	mutex_unlock(&disk->error_injection_lock);
	return 0;

out_free_inj:
	kfree(inj);
	return error;
}

static void error_inject_removeall(struct gendisk *disk)
{
	struct blk_error_inject *inj;

	mutex_lock(&disk->error_injection_lock);
	if (test_and_clear_bit(GD_ERROR_INJECT, &disk->state))
		static_branch_dec(&blk_error_injection_enabled);
	while ((inj = list_first_entry_or_null(&disk->error_injection_list,
			struct blk_error_inject, entry))) {
		list_del_rcu(&inj->entry);
		kfree_rcu_mightsleep(inj);
	}
	mutex_unlock(&disk->error_injection_lock);
}

enum options {
	Opt_add			= (1u << 0),
	Opt_removeall		= (1u << 1),

	Opt_op			= (1u << 16),
	Opt_start		= (1u << 17),
	Opt_nr_sectors		= (1u << 18),
	Opt_status		= (1u << 19),
	Opt_chance		= (1u << 20),

	Opt_invalid,
};

static const match_table_t opt_tokens = {
	{ Opt_add,			"add",			},
	{ Opt_removeall,		"removeall",		},
	{ Opt_op,			"op=%s",		},
	{ Opt_start,			"start=%u"		},
	{ Opt_nr_sectors,		"nr_sectors=%u"		},
	{ Opt_status,			"status=%s"		},
	{ Opt_chance,			"chance=%u"		},
	{ Opt_invalid,			NULL,			},
};

static int match_op(substring_t *args, enum req_op *op)
{
	const char *tag;

	tag = match_strdup(args);
	if (!tag)
		return -ENOMEM;
	*op = str_to_blk_op(tag);
	if (*op == REQ_OP_LAST)
		pr_warn("invalid op '%s'\n", tag);
	kfree(tag);
	return 0;
}

static int match_status(substring_t *args, blk_status_t *status)
{
	const char *tag;

	tag = match_strdup(args);
	if (!tag)
		return -ENOMEM;
	*status = tag_to_blk_status(tag);
	if (!*status)
		pr_warn("invalid status '%s'\n", tag);
	kfree(tag);
	return 0;
}

static ssize_t blk_error_injection_parse_options(struct gendisk *disk,
		char *options)
{
	enum { Unset, Add, Removeall } action = Unset;
	unsigned int option_mask = 0, chance = 1;
	enum req_op op = REQ_OP_LAST;
	u64 start = 0, nr_sectors = 0;
	blk_status_t status = BLK_STS_OK;
	substring_t args[MAX_OPT_ARGS];
	char *p;

	while ((p = strsep(&options, ",\n")) != NULL) {
		int error = 0;
		ssize_t token;

		if (!*p)
			continue;
		token = match_token(p, opt_tokens, args);
		option_mask |= token;
		switch (token) {
		case Opt_add:
			if (action != Unset)
				return -EINVAL;
			action = Add;
			break;
		case Opt_removeall:
			if (action != Unset)
				return -EINVAL;
			action = Removeall;
			break;
		case Opt_op:
			error = match_op(args, &op);
			break;
		case Opt_start:
			error = match_u64(args, &start);
			break;
		case Opt_nr_sectors:
			error = match_u64(args, &nr_sectors);
			break;
		case Opt_status:
			error = match_status(args, &status);
			break;
		case Opt_chance:
			error = match_uint(args, &chance);
			if (!error && chance == 0)
				error = -EINVAL;
			break;
		default:
			pr_warn("unknown parameter or missing value '%s'\n", p);
			error = -EINVAL;
		}
		if (error)
			return error;
	}

	switch (action) {
	case Add:
		return error_inject_add(disk, op, start, nr_sectors, status,
				chance);
	case Removeall:
		if (option_mask & ~Opt_removeall)
			return -EINVAL;
		error_inject_removeall(disk);
		return 0;
	default:
		return -EINVAL;
	}
}

static ssize_t blk_error_injection_write(struct file *file,
		const char __user *ubuf, size_t count, loff_t *pos)
{
	struct gendisk *disk = file_inode(file)->i_private;
	char *options;
	int error;

	options = memdup_user_nul(ubuf, count);
	if (IS_ERR(options))
		return PTR_ERR(options);
	error = blk_error_injection_parse_options(disk, options);
	kfree(options);

	if (error)
		return error;
	return count;
}

static int blk_error_injection_show(struct seq_file *s, void *private)
{
	struct gendisk *disk = s->private;
	struct blk_error_inject *inj;

	rcu_read_lock();
	list_for_each_entry_rcu(inj, &disk->error_injection_list, entry) {
		seq_printf(s, "%llu:%llu status=%s,chance=%u",
			inj->start, inj->end,
			blk_status_to_tag(inj->status), inj->chance);
		seq_putc(s, '\n');
	}
	rcu_read_unlock();
	return 0;
}

static int blk_error_injection_open(struct inode *inode, struct file *file)
{
	return single_open(file, blk_error_injection_show, inode->i_private);
}

static int blk_error_injection_release(struct inode *inode, struct file *file)
{
	return single_release(inode, file);
}

static const struct file_operations blk_error_injection_fops = {
	.owner		= THIS_MODULE,
	.write		= blk_error_injection_write,
	.read		= seq_read,
	.open		= blk_error_injection_open,
	.release	= blk_error_injection_release,
};

void blk_error_injection_init(struct gendisk *disk)
{
	debugfs_create_file("error_injection", 0600, disk->queue->debugfs_dir,
			disk, &blk_error_injection_fops);
}

void blk_error_injection_exit(struct gendisk *disk)
{
	error_inject_removeall(disk);
}
