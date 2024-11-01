/* drivers/misc/uid_sys_stats.c
 *
 * Copyright (C) 2014 - 2015 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/atomic.h>
#include <linux/err.h>
#include <linux/hashtable.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/llist.h>
#include <linux/mm.h>
#include <linux/proc_fs.h>
#include <linux/profile.h>
#include <linux/sched/cputime.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/spinlock_types.h>

#define UID_HASH_BITS	10
#define UID_HASH_NUMS	(1 << UID_HASH_BITS)
DECLARE_HASHTABLE(hash_table, UID_HASH_BITS);
/*
 * uid_lock[bkt] ensure consistency of hash_table[bkt]
 */
spinlock_t uid_lock[UID_HASH_NUMS];

static struct proc_dir_entry *cpu_parent;
static struct proc_dir_entry *io_parent;
static struct proc_dir_entry *proc_parent;

struct io_stats {
	u64 read_bytes;
	u64 write_bytes;
	u64 rchar;
	u64 wchar;
	u64 fsync;
};

#define UID_STATE_FOREGROUND	0
#define UID_STATE_BACKGROUND	1
#define UID_STATE_TOTAL_LAST	2
#define UID_STATE_DEAD_TASKS	3
#define UID_STATE_SIZE		4

#define MAX_TASK_COMM_LEN 256

struct task_entry {
	char comm[MAX_TASK_COMM_LEN];
	pid_t pid;
	struct io_stats io[UID_STATE_SIZE];
	struct hlist_node hash;
};

struct uid_entry {
	uid_t uid;
	u64 utime;
	u64 stime;
	int state;
	struct io_stats io[UID_STATE_SIZE];
	struct hlist_node hash;
};

static inline int trylock_uid(uid_t uid)
{
	return spin_trylock(
		&uid_lock[hash_min(uid, HASH_BITS(hash_table))]);
}

static inline void lock_uid(uid_t uid)
{
	spin_lock(&uid_lock[hash_min(uid, HASH_BITS(hash_table))]);
}

static inline void unlock_uid(uid_t uid)
{
	spin_unlock(&uid_lock[hash_min(uid, HASH_BITS(hash_table))]);
}

static inline void lock_uid_by_bkt(u32 bkt)
{
	spin_lock(&uid_lock[bkt]);
}

static inline void unlock_uid_by_bkt(u32 bkt)
{
	spin_unlock(&uid_lock[bkt]);
}

static u64 compute_write_bytes(struct task_io_accounting *ioac)
{
	if (ioac->write_bytes <= ioac->cancelled_write_bytes)
		return 0;

	return ioac->write_bytes - ioac->cancelled_write_bytes;
}

static void compute_io_bucket_stats(struct io_stats *io_bucket,
					struct io_stats *io_curr,
					struct io_stats *io_last,
					struct io_stats *io_dead)
{
	/* tasks could switch to another uid group, but its io_last in the
	 * previous uid group could still be positive.
	 * therefore before each update, do an overflow check first
	 */
	int64_t delta;

	delta = io_curr->read_bytes + io_dead->read_bytes -
		io_last->read_bytes;
	io_bucket->read_bytes += delta > 0 ? delta : 0;
	delta = io_curr->write_bytes + io_dead->write_bytes -
		io_last->write_bytes;
	io_bucket->write_bytes += delta > 0 ? delta : 0;
	delta = io_curr->rchar + io_dead->rchar - io_last->rchar;
	io_bucket->rchar += delta > 0 ? delta : 0;
	delta = io_curr->wchar + io_dead->wchar - io_last->wchar;
	io_bucket->wchar += delta > 0 ? delta : 0;
	delta = io_curr->fsync + io_dead->fsync - io_last->fsync;
	io_bucket->fsync += delta > 0 ? delta : 0;

	io_last->read_bytes = io_curr->read_bytes;
	io_last->write_bytes = io_curr->write_bytes;
	io_last->rchar = io_curr->rchar;
	io_last->wchar = io_curr->wchar;
	io_last->fsync = io_curr->fsync;

	memset(io_dead, 0, sizeof(struct io_stats));
}

static struct uid_entry *find_uid_entry(uid_t uid)
{
	struct uid_entry *uid_entry;
	hash_for_each_possible(hash_table, uid_entry, hash, uid) {
		if (uid_entry->uid == uid)
			return uid_entry;
	}
	return NULL;
}

static struct uid_entry *find_or_register_uid(uid_t uid)
{
	struct uid_entry *uid_entry;

	uid_entry = find_uid_entry(uid);
	if (uid_entry)
		return uid_entry;

	uid_entry = kzalloc(sizeof(struct uid_entry), GFP_ATOMIC);
	if (!uid_entry)
		return NULL;

	uid_entry->uid = uid;
	hash_add(hash_table, &uid_entry->hash, uid);

	return uid_entry;
}

static void calc_uid_cputime(struct uid_entry *uid_entry,
			u64 *total_utime, u64 *total_stime)
{
	struct user_namespace *user_ns = current_user_ns();
	struct task_struct *p, *t;
	u64 utime, stime;
	uid_t uid;

	rcu_read_lock();
	for_each_process(p) {
		uid = from_kuid_munged(user_ns, task_uid(p));

		if (uid != uid_entry->uid)
			continue;

		for_each_thread(p, t) {
			/* avoid double accounting of dying threads */
			if (!(t->flags & PF_EXITING)) {
				task_cputime_adjusted(t, &utime, &stime);
				*total_utime += utime;
				*total_stime += stime;
			}
		}
	}
	rcu_read_unlock();
}

static int uid_cputime_show(struct seq_file *m, void *v)
{
	struct uid_entry *uid_entry = NULL;
	u32 bkt;

	for (bkt = 0, uid_entry = NULL; uid_entry == NULL &&
		bkt < HASH_SIZE(hash_table); bkt++) {

		lock_uid_by_bkt(bkt);
		hlist_for_each_entry(uid_entry, &hash_table[bkt], hash) {
			u64 total_utime = uid_entry->utime;
			u64 total_stime = uid_entry->stime;

			calc_uid_cputime(uid_entry, &total_utime, &total_stime);
			seq_printf(m, "%d: %llu %llu\n", uid_entry->uid,
				ktime_to_us(total_utime), ktime_to_us(total_stime));
		}
		unlock_uid_by_bkt(bkt);
	}

	return 0;
}

static int uid_cputime_open(struct inode *inode, struct file *file)
{
	return single_open(file, uid_cputime_show, pde_data(inode));
}

static const struct proc_ops uid_cputime_fops = {
	.proc_open	= uid_cputime_open,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
};

static int uid_remove_open(struct inode *inode, struct file *file)
{
	return single_open(file, NULL, NULL);
}

static ssize_t uid_remove_write(struct file *file,
			const char __user *buffer, size_t count, loff_t *ppos)
{
	struct uid_entry *uid_entry;
	struct hlist_node *tmp;
	char uids[128];
	char *start_uid, *end_uid = NULL;
	long int uid_start = 0, uid_end = 0;

	if (count >= sizeof(uids))
		count = sizeof(uids) - 1;

	if (copy_from_user(uids, buffer, count))
		return -EFAULT;

	uids[count] = '\0';
	end_uid = uids;
	start_uid = strsep(&end_uid, "-");

	if (!start_uid || !end_uid)
		return -EINVAL;

	if (kstrtol(start_uid, 10, &uid_start) != 0 ||
		kstrtol(end_uid, 10, &uid_end) != 0) {
		return -EINVAL;
	}

	for (; uid_start <= uid_end; uid_start++) {
		lock_uid(uid_start);
		hash_for_each_possible_safe(hash_table, uid_entry, tmp,
							hash, (uid_t)uid_start) {
			if (uid_start == uid_entry->uid) {
				hash_del(&uid_entry->hash);
				kfree(uid_entry);
			}
		}
		unlock_uid(uid_start);
	}

	return count;
}

static const struct proc_ops uid_remove_fops = {
	.proc_open	= uid_remove_open,
	.proc_release	= single_release,
	.proc_write	= uid_remove_write,
};

static void __add_uid_io_stats(struct uid_entry *uid_entry,
			struct task_io_accounting *ioac, int slot)
{
	struct io_stats *io_slot = &uid_entry->io[slot];

	io_slot->read_bytes += ioac->read_bytes;
	io_slot->write_bytes += compute_write_bytes(ioac);
	io_slot->rchar += ioac->rchar;
	io_slot->wchar += ioac->wchar;
	io_slot->fsync += ioac->syscfs;
}

static void add_uid_io_stats(struct uid_entry *uid_entry,
			struct task_struct *task, int slot)
{
	struct task_entry *task_entry __maybe_unused;

	/* avoid double accounting of dying threads */
	if (slot != UID_STATE_DEAD_TASKS && (task->flags & PF_EXITING))
		return;

	__add_uid_io_stats(uid_entry, &task->ioac, slot);
}

static void update_io_stats_uid(struct uid_entry *uid_entry)
{
	struct user_namespace *user_ns = current_user_ns();
	struct task_struct *p, *t;
	struct io_stats io;

	memset(&io, 0, sizeof(struct io_stats));

	rcu_read_lock();
	for_each_process(p) {
		uid_t uid = from_kuid_munged(user_ns, task_uid(p));

		if (uid != uid_entry->uid)
			continue;

		for_each_thread(p, t) {
			/* avoid double accounting of dying threads */
			if (!(t->flags & PF_EXITING)) {
				io.read_bytes += t->ioac.read_bytes;
				io.write_bytes += compute_write_bytes(&t->ioac);
				io.rchar += t->ioac.rchar;
				io.wchar += t->ioac.wchar;
				io.fsync += t->ioac.syscfs;
			}
		}
	}
	rcu_read_unlock();

	compute_io_bucket_stats(&uid_entry->io[uid_entry->state], &io,
					&uid_entry->io[UID_STATE_TOTAL_LAST],
					&uid_entry->io[UID_STATE_DEAD_TASKS]);
}

static int uid_io_show(struct seq_file *m, void *v)
{

	struct uid_entry *uid_entry = NULL;
	u32 bkt;

	for (bkt = 0, uid_entry = NULL; uid_entry == NULL && bkt < HASH_SIZE(hash_table);
		bkt++) {
		lock_uid_by_bkt(bkt);
		hlist_for_each_entry(uid_entry, &hash_table[bkt], hash) {

			update_io_stats_uid(uid_entry);

			seq_printf(m, "%d %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu\n",
				uid_entry->uid,
				uid_entry->io[UID_STATE_FOREGROUND].rchar,
				uid_entry->io[UID_STATE_FOREGROUND].wchar,
				uid_entry->io[UID_STATE_FOREGROUND].read_bytes,
				uid_entry->io[UID_STATE_FOREGROUND].write_bytes,
				uid_entry->io[UID_STATE_BACKGROUND].rchar,
				uid_entry->io[UID_STATE_BACKGROUND].wchar,
				uid_entry->io[UID_STATE_BACKGROUND].read_bytes,
				uid_entry->io[UID_STATE_BACKGROUND].write_bytes,
				uid_entry->io[UID_STATE_FOREGROUND].fsync,
				uid_entry->io[UID_STATE_BACKGROUND].fsync);
		}
		unlock_uid_by_bkt(bkt);
	}

	return 0;
}

static int uid_io_open(struct inode *inode, struct file *file)
{
	return single_open(file, uid_io_show, pde_data(inode));
}

static const struct proc_ops uid_io_fops = {
	.proc_open	= uid_io_open,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
};

static int uid_procstat_open(struct inode *inode, struct file *file)
{
	return single_open(file, NULL, NULL);
}

static ssize_t uid_procstat_write(struct file *file,
			const char __user *buffer, size_t count, loff_t *ppos)
{
	struct uid_entry *uid_entry;
	uid_t uid;
	int argc, state;
	char input[128];

	if (count >= sizeof(input))
		return -EINVAL;

	if (copy_from_user(input, buffer, count))
		return -EFAULT;

	input[count] = '\0';

	argc = sscanf(input, "%u %d", &uid, &state);
	if (argc != 2)
		return -EINVAL;

	if (state != UID_STATE_BACKGROUND && state != UID_STATE_FOREGROUND)
		return -EINVAL;

	lock_uid(uid);
	uid_entry = find_or_register_uid(uid);
	if (!uid_entry) {
		unlock_uid(uid);
		return -EINVAL;
	}

	if (uid_entry->state == state) {
		unlock_uid(uid);
		return count;
	}

	update_io_stats_uid(uid_entry);
	uid_entry->state = state;
	unlock_uid(uid);

	return count;
}

static const struct proc_ops uid_procstat_fops = {
	.proc_open	= uid_procstat_open,
	.proc_release	= single_release,
	.proc_write	= uid_procstat_write,
};

struct update_stats_work {
	uid_t uid;
	struct task_io_accounting ioac;
	u64 utime;
	u64 stime;
	struct llist_node node;
};

static LLIST_HEAD(work_usw);

static void update_stats_workfn(struct work_struct *work)
{
	struct update_stats_work *usw, *t;
	struct uid_entry *uid_entry;
	struct task_entry *task_entry __maybe_unused;
	struct llist_node *node;

	node = llist_del_all(&work_usw);
	llist_for_each_entry_safe(usw, t, node, node) {
		lock_uid(usw->uid);
		uid_entry = find_uid_entry(usw->uid);
		if (!uid_entry)
			goto next;

		uid_entry->utime += usw->utime;
		uid_entry->stime += usw->stime;

		__add_uid_io_stats(uid_entry, &usw->ioac, UID_STATE_DEAD_TASKS);
next:
		unlock_uid(usw->uid);
		kfree(usw);
	}

}
static DECLARE_WORK(update_stats_work, update_stats_workfn);

static int process_notifier(struct notifier_block *self,
			unsigned long cmd, void *v)
{
	struct task_struct *task = v;
	struct uid_entry *uid_entry;
	u64 utime, stime;
	uid_t uid;

	if (!task)
		return NOTIFY_OK;

	uid = from_kuid_munged(current_user_ns(), task_uid(task));
	if (!trylock_uid(uid)) {
		struct update_stats_work *usw;

		usw = kmalloc(sizeof(struct update_stats_work), GFP_KERNEL);
		if (usw) {
			usw->uid = uid;
			/*
			 * Copy task->ioac since task might be destroyed before
			 * the work is later performed.
			 */
			usw->ioac = task->ioac;
			task_cputime_adjusted(task, &usw->utime, &usw->stime);
			llist_add(&usw->node, &work_usw);
			schedule_work(&update_stats_work);
		}
		return NOTIFY_OK;
	}

	uid_entry = find_or_register_uid(uid);
	if (!uid_entry) {
		pr_err("%s: failed to find uid %d\n", __func__, uid);
		goto exit;
	}

	task_cputime_adjusted(task, &utime, &stime);
	uid_entry->utime += utime;
	uid_entry->stime += stime;

	add_uid_io_stats(uid_entry, task, UID_STATE_DEAD_TASKS);

exit:
	unlock_uid(uid);
	return NOTIFY_OK;
}

static struct notifier_block process_notifier_block = {
	.notifier_call	= process_notifier,
};

static void init_hash_table_and_lock(void)
{
	int i;

	hash_init(hash_table);
	for (i = 0; i < UID_HASH_NUMS; i++)
		spin_lock_init(&uid_lock[i]);
}

static int __init proc_uid_sys_stats_init(void)
{
	init_hash_table_and_lock();

	cpu_parent = proc_mkdir("uid_cputime", NULL);
	if (!cpu_parent) {
		pr_err("%s: failed to create uid_cputime proc entry\n",
			__func__);
		goto err;
	}

	proc_create_data("remove_uid_range", 0222, cpu_parent,
		&uid_remove_fops, NULL);
	proc_create_data("show_uid_stat", 0444, cpu_parent,
		&uid_cputime_fops, NULL);

	io_parent = proc_mkdir("uid_io", NULL);
	if (!io_parent) {
		pr_err("%s: failed to create uid_io proc entry\n",
			__func__);
		goto err;
	}

	proc_create_data("stats", 0444, io_parent,
		&uid_io_fops, NULL);

	proc_parent = proc_mkdir("uid_procstat", NULL);
	if (!proc_parent) {
		pr_err("%s: failed to create uid_procstat proc entry\n",
			__func__);
		goto err;
	}

	proc_create_data("set", 0222, proc_parent,
		&uid_procstat_fops, NULL);

	profile_event_register(PROFILE_TASK_EXIT, &process_notifier_block);

	return 0;

err:
	remove_proc_subtree("uid_cputime", NULL);
	remove_proc_subtree("uid_io", NULL);
	remove_proc_subtree("uid_procstat", NULL);
	return -ENOMEM;
}

early_initcall(proc_uid_sys_stats_init);
