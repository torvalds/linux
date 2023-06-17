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
#include <linux/mm.h>
#include <linux/proc_fs.h>
#include <linux/profile.h>
#include <linux/rtmutex.h>
#include <linux/sched/cputime.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/uaccess.h>


#define UID_HASH_BITS	10
DECLARE_HASHTABLE(hash_table, UID_HASH_BITS);

static DEFINE_RT_MUTEX(uid_lock);
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
#define UID_STATE_BUCKET_SIZE	2

#define UID_STATE_TOTAL_CURR	2
#define UID_STATE_TOTAL_LAST	3
#define UID_STATE_DEAD_TASKS	4
#define UID_STATE_SIZE		5

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
	u64 active_utime;
	u64 active_stime;
	int state;
	struct io_stats io[UID_STATE_SIZE];
	struct hlist_node hash;
#ifdef CONFIG_UID_SYS_STATS_DEBUG
	DECLARE_HASHTABLE(task_entries, UID_HASH_BITS);
#endif
};

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

#ifdef CONFIG_UID_SYS_STATS_DEBUG
static void get_full_task_comm(struct task_entry *task_entry,
		struct task_struct *task)
{
	int i = 0, offset = 0, len = 0;
	/* save one byte for terminating null character */
	int unused_len = MAX_TASK_COMM_LEN - TASK_COMM_LEN - 1;
	char buf[MAX_TASK_COMM_LEN - TASK_COMM_LEN - 1];
	struct mm_struct *mm = task->mm;

	/* fill the first TASK_COMM_LEN bytes with thread name */
	__get_task_comm(task_entry->comm, TASK_COMM_LEN, task);
	i = strlen(task_entry->comm);
	while (i < TASK_COMM_LEN)
		task_entry->comm[i++] = ' ';

	/* next the executable file name */
	if (mm) {
		mmap_write_lock(mm);
		if (mm->exe_file) {
			char *pathname = d_path(&mm->exe_file->f_path, buf,
					unused_len);

			if (!IS_ERR(pathname)) {
				len = strlcpy(task_entry->comm + i, pathname,
						unused_len);
				i += len;
				task_entry->comm[i++] = ' ';
				unused_len--;
			}
		}
		mmap_write_unlock(mm);
	}
	unused_len -= len;

	/* fill the rest with command line argument
	 * replace each null or new line character
	 * between args in argv with whitespace */
	len = get_cmdline(task, buf, unused_len);
	while (offset < len) {
		if (buf[offset] != '\0' && buf[offset] != '\n')
			task_entry->comm[i++] = buf[offset];
		else
			task_entry->comm[i++] = ' ';
		offset++;
	}

	/* get rid of trailing whitespaces in case when arg is memset to
	 * zero before being reset in userspace
	 */
	while (task_entry->comm[i-1] == ' ')
		i--;
	task_entry->comm[i] = '\0';
}

static struct task_entry *find_task_entry(struct uid_entry *uid_entry,
		struct task_struct *task)
{
	struct task_entry *task_entry;

	hash_for_each_possible(uid_entry->task_entries, task_entry, hash,
			task->pid) {
		if (task->pid == task_entry->pid) {
			/* if thread name changed, update the entire command */
			int len = strnchr(task_entry->comm, ' ', TASK_COMM_LEN)
				- task_entry->comm;

			if (strncmp(task_entry->comm, task->comm, len))
				get_full_task_comm(task_entry, task);
			return task_entry;
		}
	}
	return NULL;
}

static struct task_entry *find_or_register_task(struct uid_entry *uid_entry,
		struct task_struct *task)
{
	struct task_entry *task_entry;
	pid_t pid = task->pid;

	task_entry = find_task_entry(uid_entry, task);
	if (task_entry)
		return task_entry;

	task_entry = kzalloc(sizeof(struct task_entry), GFP_ATOMIC);
	if (!task_entry)
		return NULL;

	get_full_task_comm(task_entry, task);

	task_entry->pid = pid;
	hash_add(uid_entry->task_entries, &task_entry->hash, (unsigned int)pid);

	return task_entry;
}

static void remove_uid_tasks(struct uid_entry *uid_entry)
{
	struct task_entry *task_entry;
	unsigned long bkt_task;
	struct hlist_node *tmp_task;

	hash_for_each_safe(uid_entry->task_entries, bkt_task,
			tmp_task, task_entry, hash) {
		hash_del(&task_entry->hash);
		kfree(task_entry);
	}
}

static void set_io_uid_tasks_zero(struct uid_entry *uid_entry)
{
	struct task_entry *task_entry;
	unsigned long bkt_task;

	hash_for_each(uid_entry->task_entries, bkt_task, task_entry, hash) {
		memset(&task_entry->io[UID_STATE_TOTAL_CURR], 0,
			sizeof(struct io_stats));
	}
}

static void add_uid_tasks_io_stats(struct task_entry *task_entry,
				   struct task_io_accounting *ioac, int slot)
{
	struct io_stats *task_io_slot = &task_entry->io[slot];

	task_io_slot->read_bytes += ioac->read_bytes;
	task_io_slot->write_bytes += compute_write_bytes(ioac);
	task_io_slot->rchar += ioac->rchar;
	task_io_slot->wchar += ioac->wchar;
	task_io_slot->fsync += ioac->syscfs;
}

static void compute_io_uid_tasks(struct uid_entry *uid_entry)
{
	struct task_entry *task_entry;
	unsigned long bkt_task;

	hash_for_each(uid_entry->task_entries, bkt_task, task_entry, hash) {
		compute_io_bucket_stats(&task_entry->io[uid_entry->state],
					&task_entry->io[UID_STATE_TOTAL_CURR],
					&task_entry->io[UID_STATE_TOTAL_LAST],
					&task_entry->io[UID_STATE_DEAD_TASKS]);
	}
}

static void show_io_uid_tasks(struct seq_file *m, struct uid_entry *uid_entry)
{
	struct task_entry *task_entry;
	unsigned long bkt_task;

	hash_for_each(uid_entry->task_entries, bkt_task, task_entry, hash) {
		/* Separated by comma because space exists in task comm */
		seq_printf(m, "task,%s,%lu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",
				task_entry->comm,
				(unsigned long)task_entry->pid,
				task_entry->io[UID_STATE_FOREGROUND].rchar,
				task_entry->io[UID_STATE_FOREGROUND].wchar,
				task_entry->io[UID_STATE_FOREGROUND].read_bytes,
				task_entry->io[UID_STATE_FOREGROUND].write_bytes,
				task_entry->io[UID_STATE_BACKGROUND].rchar,
				task_entry->io[UID_STATE_BACKGROUND].wchar,
				task_entry->io[UID_STATE_BACKGROUND].read_bytes,
				task_entry->io[UID_STATE_BACKGROUND].write_bytes,
				task_entry->io[UID_STATE_FOREGROUND].fsync,
				task_entry->io[UID_STATE_BACKGROUND].fsync);
	}
}
#else
static void remove_uid_tasks(struct uid_entry *uid_entry) {};
static void set_io_uid_tasks_zero(struct uid_entry *uid_entry) {};
static void compute_io_uid_tasks(struct uid_entry *uid_entry) {};
static void show_io_uid_tasks(struct seq_file *m,
		struct uid_entry *uid_entry) {}
#endif

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
#ifdef CONFIG_UID_SYS_STATS_DEBUG
	hash_init(uid_entry->task_entries);
#endif
	hash_add(hash_table, &uid_entry->hash, uid);

	return uid_entry;
}

static int uid_cputime_show(struct seq_file *m, void *v)
{
	struct uid_entry *uid_entry = NULL;
	struct task_struct *task, *temp;
	struct user_namespace *user_ns = current_user_ns();
	u64 utime;
	u64 stime;
	unsigned long bkt;
	uid_t uid;

	rt_mutex_lock(&uid_lock);

	hash_for_each(hash_table, bkt, uid_entry, hash) {
		uid_entry->active_stime = 0;
		uid_entry->active_utime = 0;
	}

	rcu_read_lock();
	do_each_thread(temp, task) {
		uid = from_kuid_munged(user_ns, task_uid(task));
		if (!uid_entry || uid_entry->uid != uid)
			uid_entry = find_or_register_uid(uid);
		if (!uid_entry) {
			rcu_read_unlock();
			rt_mutex_unlock(&uid_lock);
			pr_err("%s: failed to find the uid_entry for uid %d\n",
				__func__, uid);
			return -ENOMEM;
		}
		/* avoid double accounting of dying threads */
		if (!(task->flags & PF_EXITING)) {
			task_cputime_adjusted(task, &utime, &stime);
			uid_entry->active_utime += utime;
			uid_entry->active_stime += stime;
		}
	} while_each_thread(temp, task);
	rcu_read_unlock();

	hash_for_each(hash_table, bkt, uid_entry, hash) {
		u64 total_utime = uid_entry->utime +
							uid_entry->active_utime;
		u64 total_stime = uid_entry->stime +
							uid_entry->active_stime;
		seq_printf(m, "%d: %llu %llu\n", uid_entry->uid,
			ktime_to_us(total_utime), ktime_to_us(total_stime));
	}

	rt_mutex_unlock(&uid_lock);
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

	rt_mutex_lock(&uid_lock);

	for (; uid_start <= uid_end; uid_start++) {
		hash_for_each_possible_safe(hash_table, uid_entry, tmp,
							hash, (uid_t)uid_start) {
			if (uid_start == uid_entry->uid) {
				remove_uid_tasks(uid_entry);
				hash_del(&uid_entry->hash);
				kfree(uid_entry);
			}
		}
	}

	rt_mutex_unlock(&uid_lock);
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

#ifdef CONFIG_UID_SYS_STATS_DEBUG
	task_entry = find_or_register_task(uid_entry, task);
	add_uid_tasks_io_stats(task_entry, &task->ioac, slot);
#endif
	__add_uid_io_stats(uid_entry, &task->ioac, slot);
}

static void update_io_stats_all_locked(void)
{
	struct uid_entry *uid_entry = NULL;
	struct task_struct *task, *temp;
	struct user_namespace *user_ns = current_user_ns();
	unsigned long bkt;
	uid_t uid;

	hash_for_each(hash_table, bkt, uid_entry, hash) {
		memset(&uid_entry->io[UID_STATE_TOTAL_CURR], 0,
			sizeof(struct io_stats));
		set_io_uid_tasks_zero(uid_entry);
	}

	rcu_read_lock();
	do_each_thread(temp, task) {
		uid = from_kuid_munged(user_ns, task_uid(task));
		if (!uid_entry || uid_entry->uid != uid)
			uid_entry = find_or_register_uid(uid);
		if (!uid_entry)
			continue;
		add_uid_io_stats(uid_entry, task, UID_STATE_TOTAL_CURR);
	} while_each_thread(temp, task);
	rcu_read_unlock();

	hash_for_each(hash_table, bkt, uid_entry, hash) {
		compute_io_bucket_stats(&uid_entry->io[uid_entry->state],
					&uid_entry->io[UID_STATE_TOTAL_CURR],
					&uid_entry->io[UID_STATE_TOTAL_LAST],
					&uid_entry->io[UID_STATE_DEAD_TASKS]);
		compute_io_uid_tasks(uid_entry);
	}
}

static void update_io_stats_uid_locked(struct uid_entry *uid_entry)
{
	struct task_struct *task, *temp;
	struct user_namespace *user_ns = current_user_ns();

	memset(&uid_entry->io[UID_STATE_TOTAL_CURR], 0,
		sizeof(struct io_stats));
	set_io_uid_tasks_zero(uid_entry);

	rcu_read_lock();
	do_each_thread(temp, task) {
		if (from_kuid_munged(user_ns, task_uid(task)) != uid_entry->uid)
			continue;
		add_uid_io_stats(uid_entry, task, UID_STATE_TOTAL_CURR);
	} while_each_thread(temp, task);
	rcu_read_unlock();

	compute_io_bucket_stats(&uid_entry->io[uid_entry->state],
				&uid_entry->io[UID_STATE_TOTAL_CURR],
				&uid_entry->io[UID_STATE_TOTAL_LAST],
				&uid_entry->io[UID_STATE_DEAD_TASKS]);
	compute_io_uid_tasks(uid_entry);
}


static int uid_io_show(struct seq_file *m, void *v)
{
	struct uid_entry *uid_entry;
	unsigned long bkt;

	rt_mutex_lock(&uid_lock);

	update_io_stats_all_locked();

	hash_for_each(hash_table, bkt, uid_entry, hash) {
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

		show_io_uid_tasks(m, uid_entry);
	}

	rt_mutex_unlock(&uid_lock);
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

	rt_mutex_lock(&uid_lock);

	uid_entry = find_or_register_uid(uid);
	if (!uid_entry) {
		rt_mutex_unlock(&uid_lock);
		return -EINVAL;
	}

	if (uid_entry->state == state) {
		rt_mutex_unlock(&uid_lock);
		return count;
	}

	update_io_stats_uid_locked(uid_entry);

	uid_entry->state = state;

	rt_mutex_unlock(&uid_lock);

	return count;
}

static const struct proc_ops uid_procstat_fops = {
	.proc_open	= uid_procstat_open,
	.proc_release	= single_release,
	.proc_write	= uid_procstat_write,
};

struct update_stats_work {
	struct work_struct work;
	uid_t uid;
#ifdef CONFIG_UID_SYS_STATS_DEBUG
	struct task_struct *task;
#endif
	struct task_io_accounting ioac;
	u64 utime;
	u64 stime;
};

static void update_stats_workfn(struct work_struct *work)
{
	struct update_stats_work *usw =
		container_of(work, struct update_stats_work, work);
	struct uid_entry *uid_entry;
	struct task_entry *task_entry __maybe_unused;

	rt_mutex_lock(&uid_lock);
	uid_entry = find_uid_entry(usw->uid);
	if (!uid_entry)
		goto exit;

	uid_entry->utime += usw->utime;
	uid_entry->stime += usw->stime;

#ifdef CONFIG_UID_SYS_STATS_DEBUG
	task_entry = find_task_entry(uid_entry, usw->task);
	if (!task_entry)
		goto exit;
	add_uid_tasks_io_stats(task_entry, &usw->ioac,
			       UID_STATE_DEAD_TASKS);
#endif
	__add_uid_io_stats(uid_entry, &usw->ioac, UID_STATE_DEAD_TASKS);
exit:
	rt_mutex_unlock(&uid_lock);
#ifdef CONFIG_UID_SYS_STATS_DEBUG
	put_task_struct(usw->task);
#endif
	kfree(usw);
}

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
	if (!rt_mutex_trylock(&uid_lock)) {
		struct update_stats_work *usw;

		usw = kmalloc(sizeof(struct update_stats_work), GFP_KERNEL);
		if (usw) {
			INIT_WORK(&usw->work, update_stats_workfn);
			usw->uid = uid;
#ifdef CONFIG_UID_SYS_STATS_DEBUG
			usw->task = get_task_struct(task);
#endif
			/*
			 * Copy task->ioac since task might be destroyed before
			 * the work is later performed.
			 */
			usw->ioac = task->ioac;
			task_cputime_adjusted(task, &usw->utime, &usw->stime);
			schedule_work(&usw->work);
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
	rt_mutex_unlock(&uid_lock);
	return NOTIFY_OK;
}

static struct notifier_block process_notifier_block = {
	.notifier_call	= process_notifier,
};

static int __init proc_uid_sys_stats_init(void)
{
	hash_init(hash_table);

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
