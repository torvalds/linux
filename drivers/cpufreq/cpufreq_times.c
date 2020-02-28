/* drivers/cpufreq/cpufreq_times.c
 *
 * Copyright (C) 2018 Google, Inc.
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

#include <linux/cpufreq.h>
#include <linux/cpufreq_times.h>
#include <linux/hashtable.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/threads.h>

#define UID_HASH_BITS 10

static DECLARE_HASHTABLE(uid_hash_table, UID_HASH_BITS);

static DEFINE_SPINLOCK(task_time_in_state_lock); /* task->time_in_state */
static DEFINE_SPINLOCK(uid_lock); /* uid_hash_table */

struct uid_entry {
	uid_t uid;
	unsigned int max_state;
	struct hlist_node hash;
	struct rcu_head rcu;
	u64 time_in_state[0];
};

/**
 * struct cpu_freqs - per-cpu frequency information
 * @offset: start of these freqs' stats in task time_in_state array
 * @max_state: number of entries in freq_table
 * @last_index: index in freq_table of last frequency switched to
 * @freq_table: list of available frequencies
 */
struct cpu_freqs {
	unsigned int offset;
	unsigned int max_state;
	unsigned int last_index;
	unsigned int freq_table[0];
};

static struct cpu_freqs *all_freqs[NR_CPUS];

static unsigned int next_offset;

/* Caller must hold uid lock */
static struct uid_entry *find_uid_entry_locked(uid_t uid)
{
	struct uid_entry *uid_entry;

	hash_for_each_possible(uid_hash_table, uid_entry, hash, uid) {
		if (uid_entry->uid == uid)
			return uid_entry;
	}
	return NULL;
}

/* Caller must hold uid lock */
static struct uid_entry *find_or_register_uid_locked(uid_t uid)
{
	struct uid_entry *uid_entry, *temp;
	unsigned int max_state = READ_ONCE(next_offset);
	size_t alloc_size = sizeof(*uid_entry) + max_state *
		sizeof(uid_entry->time_in_state[0]);

	uid_entry = find_uid_entry_locked(uid);
	if (uid_entry) {
		if (uid_entry->max_state == max_state)
			return uid_entry;
		/* uid_entry->time_in_state is too small to track all freqs, so
		 * expand it.
		 */
		/* no more __krealloc() so roll our own */
		temp = kmalloc(alloc_size, GFP_ATOMIC);
		if (!temp)
			return uid_entry;
		memcpy(temp, uid_entry, alloc_size);
		temp->max_state = max_state;
		memset(temp->time_in_state + uid_entry->max_state, 0,
		       (max_state - uid_entry->max_state) *
		       sizeof(uid_entry->time_in_state[0]));
		if (temp != uid_entry) {
			hlist_replace_rcu(&uid_entry->hash, &temp->hash);
			kfree_rcu(uid_entry, rcu);
		}
		return temp;
	}

	uid_entry = kzalloc(alloc_size, GFP_ATOMIC);
	if (!uid_entry)
		return NULL;

	uid_entry->uid = uid;
	uid_entry->max_state = max_state;

	hash_add_rcu(uid_hash_table, &uid_entry->hash, uid);

	return uid_entry;
}

static void *uid_seq_start(struct seq_file *seq, loff_t *pos)
{
	if (*pos >= HASH_SIZE(uid_hash_table))
		return NULL;

	return &uid_hash_table[*pos];
}

static void *uid_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	do {
		(*pos)++;

		if (*pos >= HASH_SIZE(uid_hash_table))
			return NULL;
	} while (hlist_empty(&uid_hash_table[*pos]));

	return &uid_hash_table[*pos];
}

static void uid_seq_stop(struct seq_file *seq, void *v) { }

static int uid_time_in_state_seq_show(struct seq_file *m, void *v)
{
	struct uid_entry *uid_entry;
	struct cpu_freqs *freqs, *last_freqs = NULL;
	int i, cpu;

	if (v == uid_hash_table) {
		seq_puts(m, "uid:");
		for_each_possible_cpu(cpu) {
			freqs = all_freqs[cpu];
			if (!freqs || freqs == last_freqs)
				continue;
			last_freqs = freqs;
			for (i = 0; i < freqs->max_state; i++) {
				seq_put_decimal_ull(m, " ",
						    freqs->freq_table[i]);
			}
		}
		seq_putc(m, '\n');
	}

	rcu_read_lock();

	hlist_for_each_entry_rcu(uid_entry, (struct hlist_head *)v, hash) {
		if (uid_entry->max_state) {
			seq_put_decimal_ull(m, "", uid_entry->uid);
			seq_putc(m, ':');
		}
		for (i = 0; i < uid_entry->max_state; ++i) {
			u64 time = nsec_to_clock_t(uid_entry->time_in_state[i]);
			seq_put_decimal_ull(m, " ", time);
		}
		if (uid_entry->max_state)
			seq_putc(m, '\n');
	}

	rcu_read_unlock();
	return 0;
}

void cpufreq_task_times_init(struct task_struct *p)
{
	unsigned long flags;

	spin_lock_irqsave(&task_time_in_state_lock, flags);
	p->time_in_state = NULL;
	spin_unlock_irqrestore(&task_time_in_state_lock, flags);
	p->max_state = 0;
}

void cpufreq_task_times_alloc(struct task_struct *p)
{
	void *temp;
	unsigned long flags;
	unsigned int max_state = READ_ONCE(next_offset);

	/* We use one array to avoid multiple allocs per task */
	temp = kcalloc(max_state, sizeof(p->time_in_state[0]), GFP_ATOMIC);
	if (!temp)
		return;

	spin_lock_irqsave(&task_time_in_state_lock, flags);
	p->time_in_state = temp;
	spin_unlock_irqrestore(&task_time_in_state_lock, flags);
	p->max_state = max_state;
}

/* Caller must hold task_time_in_state_lock */
static int cpufreq_task_times_realloc_locked(struct task_struct *p)
{
	void *temp;
	unsigned int max_state = READ_ONCE(next_offset);

	temp = krealloc(p->time_in_state, max_state * sizeof(u64), GFP_ATOMIC);
	if (!temp)
		return -ENOMEM;
	p->time_in_state = temp;
	memset(p->time_in_state + p->max_state, 0,
	       (max_state - p->max_state) * sizeof(u64));
	p->max_state = max_state;
	return 0;
}

void cpufreq_task_times_exit(struct task_struct *p)
{
	unsigned long flags;
	void *temp;

	if (!p->time_in_state)
		return;

	spin_lock_irqsave(&task_time_in_state_lock, flags);
	temp = p->time_in_state;
	p->time_in_state = NULL;
	spin_unlock_irqrestore(&task_time_in_state_lock, flags);
	kfree(temp);
}

int proc_time_in_state_show(struct seq_file *m, struct pid_namespace *ns,
	struct pid *pid, struct task_struct *p)
{
	unsigned int cpu, i;
	u64 cputime;
	unsigned long flags;
	struct cpu_freqs *freqs;
	struct cpu_freqs *last_freqs = NULL;

	spin_lock_irqsave(&task_time_in_state_lock, flags);
	for_each_possible_cpu(cpu) {
		freqs = all_freqs[cpu];
		if (!freqs || freqs == last_freqs)
			continue;
		last_freqs = freqs;

		seq_printf(m, "cpu%u\n", cpu);
		for (i = 0; i < freqs->max_state; i++) {
			cputime = 0;
			if (freqs->offset + i < p->max_state &&
			    p->time_in_state)
				cputime = p->time_in_state[freqs->offset + i];
			seq_printf(m, "%u %lu\n", freqs->freq_table[i],
				   (unsigned long)nsec_to_clock_t(cputime));
		}
	}
	spin_unlock_irqrestore(&task_time_in_state_lock, flags);
	return 0;
}

void cpufreq_acct_update_power(struct task_struct *p, u64 cputime)
{
	unsigned long flags;
	unsigned int state;
	struct uid_entry *uid_entry;
	struct cpu_freqs *freqs = all_freqs[task_cpu(p)];
	uid_t uid = from_kuid_munged(current_user_ns(), task_uid(p));

	if (!freqs || is_idle_task(p) || p->flags & PF_EXITING)
		return;

	state = freqs->offset + READ_ONCE(freqs->last_index);

	spin_lock_irqsave(&task_time_in_state_lock, flags);
	if ((state < p->max_state || !cpufreq_task_times_realloc_locked(p)) &&
	    p->time_in_state)
		p->time_in_state[state] += cputime;
	spin_unlock_irqrestore(&task_time_in_state_lock, flags);

	spin_lock_irqsave(&uid_lock, flags);
	uid_entry = find_or_register_uid_locked(uid);
	if (uid_entry && state < uid_entry->max_state)
		uid_entry->time_in_state[state] += cputime;
	spin_unlock_irqrestore(&uid_lock, flags);
}

static int cpufreq_times_get_index(struct cpu_freqs *freqs, unsigned int freq)
{
	int index;
        for (index = 0; index < freqs->max_state; ++index) {
		if (freqs->freq_table[index] == freq)
			return index;
        }
	return -1;
}

void cpufreq_times_create_policy(struct cpufreq_policy *policy)
{
	int cpu, index = 0;
	unsigned int count = 0;
	struct cpufreq_frequency_table *pos, *table;
	struct cpu_freqs *freqs;
	void *tmp;

	if (all_freqs[policy->cpu])
		return;

	table = policy->freq_table;
	if (!table)
		return;

	cpufreq_for_each_valid_entry(pos, table)
		count++;

	tmp =  kzalloc(sizeof(*freqs) + sizeof(freqs->freq_table[0]) * count,
		       GFP_KERNEL);
	if (!tmp)
		return;

	freqs = tmp;
	freqs->max_state = count;

	cpufreq_for_each_valid_entry(pos, table)
		freqs->freq_table[index++] = pos->frequency;

	index = cpufreq_times_get_index(freqs, policy->cur);
	if (index >= 0)
		WRITE_ONCE(freqs->last_index, index);

	freqs->offset = next_offset;
	WRITE_ONCE(next_offset, freqs->offset + count);
	for_each_cpu(cpu, policy->related_cpus)
		all_freqs[cpu] = freqs;
}

void cpufreq_task_times_remove_uids(uid_t uid_start, uid_t uid_end)
{
	struct uid_entry *uid_entry;
	struct hlist_node *tmp;
	unsigned long flags;

	spin_lock_irqsave(&uid_lock, flags);

	for (; uid_start <= uid_end; uid_start++) {
		hash_for_each_possible_safe(uid_hash_table, uid_entry, tmp,
			hash, uid_start) {
			if (uid_start == uid_entry->uid) {
				hash_del_rcu(&uid_entry->hash);
				kfree_rcu(uid_entry, rcu);
			}
		}
	}

	spin_unlock_irqrestore(&uid_lock, flags);
}

void cpufreq_times_record_transition(struct cpufreq_policy *policy,
	unsigned int new_freq)
{
	int index;
	struct cpu_freqs *freqs = all_freqs[policy->cpu];
	if (!freqs)
		return;

	index = cpufreq_times_get_index(freqs, new_freq);
	if (index >= 0)
		WRITE_ONCE(freqs->last_index, index);
}

static const struct seq_operations uid_time_in_state_seq_ops = {
	.start = uid_seq_start,
	.next = uid_seq_next,
	.stop = uid_seq_stop,
	.show = uid_time_in_state_seq_show,
};

static int uid_time_in_state_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &uid_time_in_state_seq_ops);
}

static const struct proc_ops uid_time_in_state_fops = {
	.proc_open	= uid_time_in_state_open,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= seq_release,
};

static int __init cpufreq_times_init(void)
{
	proc_create_data("uid_time_in_state", 0444, NULL,
			 &uid_time_in_state_fops, NULL);

	return 0;
}

early_initcall(cpufreq_times_init);
