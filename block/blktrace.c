/*
 * Copyright (C) 2006 Jens Axboe <axboe@kernel.dk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */
#include <linux/kernel.h>
#include <linux/blkdev.h>
#include <linux/blktrace_api.h>
#include <linux/percpu.h>
#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/debugfs.h>
#include <linux/time.h>
#include <asm/uaccess.h>

static DEFINE_PER_CPU(unsigned long long, blk_trace_cpu_offset) = { 0, };
static unsigned int blktrace_seq __read_mostly = 1;

/*
 * Send out a notify message.
 */
static void trace_note(struct blk_trace *bt, pid_t pid, int action,
		       const void *data, size_t len)
{
	struct blk_io_trace *t;

	t = relay_reserve(bt->rchan, sizeof(*t) + len);
	if (t) {
		const int cpu = smp_processor_id();

		t->magic = BLK_IO_TRACE_MAGIC | BLK_IO_TRACE_VERSION;
		t->time = sched_clock() - per_cpu(blk_trace_cpu_offset, cpu);
		t->device = bt->dev;
		t->action = action;
		t->pid = pid;
		t->cpu = cpu;
		t->pdu_len = len;
		memcpy((void *) t + sizeof(*t), data, len);
	}
}

/*
 * Send out a notify for this process, if we haven't done so since a trace
 * started
 */
static void trace_note_tsk(struct blk_trace *bt, struct task_struct *tsk)
{
	tsk->btrace_seq = blktrace_seq;
	trace_note(bt, tsk->pid, BLK_TN_PROCESS, tsk->comm, sizeof(tsk->comm));
}

static void trace_note_time(struct blk_trace *bt)
{
	struct timespec now;
	unsigned long flags;
	u32 words[2];

	getnstimeofday(&now);
	words[0] = now.tv_sec;
	words[1] = now.tv_nsec;

	local_irq_save(flags);
	trace_note(bt, 0, BLK_TN_TIMESTAMP, words, sizeof(words));
	local_irq_restore(flags);
}

static int act_log_check(struct blk_trace *bt, u32 what, sector_t sector,
			 pid_t pid)
{
	if (((bt->act_mask << BLK_TC_SHIFT) & what) == 0)
		return 1;
	if (sector < bt->start_lba || sector > bt->end_lba)
		return 1;
	if (bt->pid && pid != bt->pid)
		return 1;

	return 0;
}

/*
 * Data direction bit lookup
 */
static u32 ddir_act[2] __read_mostly = { BLK_TC_ACT(BLK_TC_READ), BLK_TC_ACT(BLK_TC_WRITE) };

/*
 * Bio action bits of interest
 */
static u32 bio_act[9] __read_mostly = { 0, BLK_TC_ACT(BLK_TC_BARRIER), BLK_TC_ACT(BLK_TC_SYNC), 0, BLK_TC_ACT(BLK_TC_AHEAD), 0, 0, 0, BLK_TC_ACT(BLK_TC_META) };

/*
 * More could be added as needed, taking care to increment the decrementer
 * to get correct indexing
 */
#define trace_barrier_bit(rw)	\
	(((rw) & (1 << BIO_RW_BARRIER)) >> (BIO_RW_BARRIER - 0))
#define trace_sync_bit(rw)	\
	(((rw) & (1 << BIO_RW_SYNC)) >> (BIO_RW_SYNC - 1))
#define trace_ahead_bit(rw)	\
	(((rw) & (1 << BIO_RW_AHEAD)) << (2 - BIO_RW_AHEAD))
#define trace_meta_bit(rw)	\
	(((rw) & (1 << BIO_RW_META)) >> (BIO_RW_META - 3))

/*
 * The worker for the various blk_add_trace*() types. Fills out a
 * blk_io_trace structure and places it in a per-cpu subbuffer.
 */
void __blk_add_trace(struct blk_trace *bt, sector_t sector, int bytes,
		     int rw, u32 what, int error, int pdu_len, void *pdu_data)
{
	struct task_struct *tsk = current;
	struct blk_io_trace *t;
	unsigned long flags;
	unsigned long *sequence;
	pid_t pid;
	int cpu;

	if (unlikely(bt->trace_state != Blktrace_running))
		return;

	what |= ddir_act[rw & WRITE];
	what |= bio_act[trace_barrier_bit(rw)];
	what |= bio_act[trace_sync_bit(rw)];
	what |= bio_act[trace_ahead_bit(rw)];
	what |= bio_act[trace_meta_bit(rw)];

	pid = tsk->pid;
	if (unlikely(act_log_check(bt, what, sector, pid)))
		return;

	/*
	 * A word about the locking here - we disable interrupts to reserve
	 * some space in the relay per-cpu buffer, to prevent an irq
	 * from coming in and stepping on our toes. Once reserved, it's
	 * enough to get preemption disabled to prevent read of this data
	 * before we are through filling it. get_cpu()/put_cpu() does this
	 * for us
	 */
	local_irq_save(flags);

	if (unlikely(tsk->btrace_seq != blktrace_seq))
		trace_note_tsk(bt, tsk);

	t = relay_reserve(bt->rchan, sizeof(*t) + pdu_len);
	if (t) {
		cpu = smp_processor_id();
		sequence = per_cpu_ptr(bt->sequence, cpu);

		t->magic = BLK_IO_TRACE_MAGIC | BLK_IO_TRACE_VERSION;
		t->sequence = ++(*sequence);
		t->time = sched_clock() - per_cpu(blk_trace_cpu_offset, cpu);
		t->sector = sector;
		t->bytes = bytes;
		t->action = what;
		t->pid = pid;
		t->device = bt->dev;
		t->cpu = cpu;
		t->error = error;
		t->pdu_len = pdu_len;

		if (pdu_len)
			memcpy((void *) t + sizeof(*t), pdu_data, pdu_len);
	}

	local_irq_restore(flags);
}

EXPORT_SYMBOL_GPL(__blk_add_trace);

static struct dentry *blk_tree_root;
static struct mutex blk_tree_mutex;
static unsigned int root_users;

static inline void blk_remove_root(void)
{
	if (blk_tree_root) {
		debugfs_remove(blk_tree_root);
		blk_tree_root = NULL;
	}
}

static void blk_remove_tree(struct dentry *dir)
{
	mutex_lock(&blk_tree_mutex);
	debugfs_remove(dir);
	if (--root_users == 0)
		blk_remove_root();
	mutex_unlock(&blk_tree_mutex);
}

static struct dentry *blk_create_tree(const char *blk_name)
{
	struct dentry *dir = NULL;

	mutex_lock(&blk_tree_mutex);

	if (!blk_tree_root) {
		blk_tree_root = debugfs_create_dir("block", NULL);
		if (!blk_tree_root)
			goto err;
	}

	dir = debugfs_create_dir(blk_name, blk_tree_root);
	if (dir)
		root_users++;
	else
		blk_remove_root();

err:
	mutex_unlock(&blk_tree_mutex);
	return dir;
}

static void blk_trace_cleanup(struct blk_trace *bt)
{
	relay_close(bt->rchan);
	debugfs_remove(bt->dropped_file);
	blk_remove_tree(bt->dir);
	free_percpu(bt->sequence);
	kfree(bt);
}

static int blk_trace_remove(request_queue_t *q)
{
	struct blk_trace *bt;

	bt = xchg(&q->blk_trace, NULL);
	if (!bt)
		return -EINVAL;

	if (bt->trace_state == Blktrace_setup ||
	    bt->trace_state == Blktrace_stopped)
		blk_trace_cleanup(bt);

	return 0;
}

static int blk_dropped_open(struct inode *inode, struct file *filp)
{
	filp->private_data = inode->i_private;

	return 0;
}

static ssize_t blk_dropped_read(struct file *filp, char __user *buffer,
				size_t count, loff_t *ppos)
{
	struct blk_trace *bt = filp->private_data;
	char buf[16];

	snprintf(buf, sizeof(buf), "%u\n", atomic_read(&bt->dropped));

	return simple_read_from_buffer(buffer, count, ppos, buf, strlen(buf));
}

static struct file_operations blk_dropped_fops = {
	.owner =	THIS_MODULE,
	.open =		blk_dropped_open,
	.read =		blk_dropped_read,
};

/*
 * Keep track of how many times we encountered a full subbuffer, to aid
 * the user space app in telling how many lost events there were.
 */
static int blk_subbuf_start_callback(struct rchan_buf *buf, void *subbuf,
				     void *prev_subbuf, size_t prev_padding)
{
	struct blk_trace *bt;

	if (!relay_buf_full(buf))
		return 1;

	bt = buf->chan->private_data;
	atomic_inc(&bt->dropped);
	return 0;
}

static int blk_remove_buf_file_callback(struct dentry *dentry)
{
	debugfs_remove(dentry);
	return 0;
}

static struct dentry *blk_create_buf_file_callback(const char *filename,
						   struct dentry *parent,
						   int mode,
						   struct rchan_buf *buf,
						   int *is_global)
{
	return debugfs_create_file(filename, mode, parent, buf,
					&relay_file_operations);
}

static struct rchan_callbacks blk_relay_callbacks = {
	.subbuf_start		= blk_subbuf_start_callback,
	.create_buf_file	= blk_create_buf_file_callback,
	.remove_buf_file	= blk_remove_buf_file_callback,
};

/*
 * Setup everything required to start tracing
 */
static int blk_trace_setup(request_queue_t *q, struct block_device *bdev,
			   char __user *arg)
{
	struct blk_user_trace_setup buts;
	struct blk_trace *old_bt, *bt = NULL;
	struct dentry *dir = NULL;
	char b[BDEVNAME_SIZE];
	int ret, i;

	if (copy_from_user(&buts, arg, sizeof(buts)))
		return -EFAULT;

	if (!buts.buf_size || !buts.buf_nr)
		return -EINVAL;

	strcpy(buts.name, bdevname(bdev, b));

	/*
	 * some device names have larger paths - convert the slashes
	 * to underscores for this to work as expected
	 */
	for (i = 0; i < strlen(buts.name); i++)
		if (buts.name[i] == '/')
			buts.name[i] = '_';

	if (copy_to_user(arg, &buts, sizeof(buts)))
		return -EFAULT;

	ret = -ENOMEM;
	bt = kzalloc(sizeof(*bt), GFP_KERNEL);
	if (!bt)
		goto err;

	bt->sequence = alloc_percpu(unsigned long);
	if (!bt->sequence)
		goto err;

	ret = -ENOENT;
	dir = blk_create_tree(buts.name);
	if (!dir)
		goto err;

	bt->dir = dir;
	bt->dev = bdev->bd_dev;
	atomic_set(&bt->dropped, 0);

	ret = -EIO;
	bt->dropped_file = debugfs_create_file("dropped", 0444, dir, bt, &blk_dropped_fops);
	if (!bt->dropped_file)
		goto err;

	bt->rchan = relay_open("trace", dir, buts.buf_size, buts.buf_nr, &blk_relay_callbacks);
	if (!bt->rchan)
		goto err;
	bt->rchan->private_data = bt;

	bt->act_mask = buts.act_mask;
	if (!bt->act_mask)
		bt->act_mask = (u16) -1;

	bt->start_lba = buts.start_lba;
	bt->end_lba = buts.end_lba;
	if (!bt->end_lba)
		bt->end_lba = -1ULL;

	bt->pid = buts.pid;
	bt->trace_state = Blktrace_setup;

	ret = -EBUSY;
	old_bt = xchg(&q->blk_trace, bt);
	if (old_bt) {
		(void) xchg(&q->blk_trace, old_bt);
		goto err;
	}

	return 0;
err:
	if (dir)
		blk_remove_tree(dir);
	if (bt) {
		if (bt->dropped_file)
			debugfs_remove(bt->dropped_file);
		free_percpu(bt->sequence);
		if (bt->rchan)
			relay_close(bt->rchan);
		kfree(bt);
	}
	return ret;
}

static int blk_trace_startstop(request_queue_t *q, int start)
{
	struct blk_trace *bt;
	int ret;

	if ((bt = q->blk_trace) == NULL)
		return -EINVAL;

	/*
	 * For starting a trace, we can transition from a setup or stopped
	 * trace. For stopping a trace, the state must be running
	 */
	ret = -EINVAL;
	if (start) {
		if (bt->trace_state == Blktrace_setup ||
		    bt->trace_state == Blktrace_stopped) {
			blktrace_seq++;
			smp_mb();
			bt->trace_state = Blktrace_running;

			trace_note_time(bt);
			ret = 0;
		}
	} else {
		if (bt->trace_state == Blktrace_running) {
			bt->trace_state = Blktrace_stopped;
			relay_flush(bt->rchan);
			ret = 0;
		}
	}

	return ret;
}

/**
 * blk_trace_ioctl: - handle the ioctls associated with tracing
 * @bdev:	the block device
 * @cmd: 	the ioctl cmd
 * @arg:	the argument data, if any
 *
 **/
int blk_trace_ioctl(struct block_device *bdev, unsigned cmd, char __user *arg)
{
	request_queue_t *q;
	int ret, start = 0;

	q = bdev_get_queue(bdev);
	if (!q)
		return -ENXIO;

	mutex_lock(&bdev->bd_mutex);

	switch (cmd) {
	case BLKTRACESETUP:
		ret = blk_trace_setup(q, bdev, arg);
		break;
	case BLKTRACESTART:
		start = 1;
	case BLKTRACESTOP:
		ret = blk_trace_startstop(q, start);
		break;
	case BLKTRACETEARDOWN:
		ret = blk_trace_remove(q);
		break;
	default:
		ret = -ENOTTY;
		break;
	}

	mutex_unlock(&bdev->bd_mutex);
	return ret;
}

/**
 * blk_trace_shutdown: - stop and cleanup trace structures
 * @q:    the request queue associated with the device
 *
 **/
void blk_trace_shutdown(request_queue_t *q)
{
	if (q->blk_trace) {
		blk_trace_startstop(q, 0);
		blk_trace_remove(q);
	}
}

/*
 * Average offset over two calls to sched_clock() with a gettimeofday()
 * in the middle
 */
static void blk_check_time(unsigned long long *t)
{
	unsigned long long a, b;
	struct timeval tv;

	a = sched_clock();
	do_gettimeofday(&tv);
	b = sched_clock();

	*t = tv.tv_sec * 1000000000 + tv.tv_usec * 1000;
	*t -= (a + b) / 2;
}

/*
 * calibrate our inter-CPU timings
 */
static void blk_trace_check_cpu_time(void *data)
{
	unsigned long long *t;
	int cpu = get_cpu();

	t = &per_cpu(blk_trace_cpu_offset, cpu);

	/*
	 * Just call it twice, hopefully the second call will be cache hot
	 * and a little more precise
	 */
	blk_check_time(t);
	blk_check_time(t);

	put_cpu();
}

static void blk_trace_set_ht_offsets(void)
{
#if defined(CONFIG_SCHED_SMT)
	int cpu, i;

	/*
	 * now make sure HT siblings have the same time offset
	 */
	preempt_disable();
	for_each_online_cpu(cpu) {
		unsigned long long *cpu_off, *sibling_off;

		for_each_cpu_mask(i, cpu_sibling_map[cpu]) {
			if (i == cpu)
				continue;

			cpu_off = &per_cpu(blk_trace_cpu_offset, cpu);
			sibling_off = &per_cpu(blk_trace_cpu_offset, i);
			*sibling_off = *cpu_off;
		}
	}
	preempt_enable();
#endif
}

static __init int blk_trace_init(void)
{
	mutex_init(&blk_tree_mutex);
	on_each_cpu(blk_trace_check_cpu_time, NULL, 1, 1);
	blk_trace_set_ht_offsets();

	return 0;
}

module_init(blk_trace_init);

