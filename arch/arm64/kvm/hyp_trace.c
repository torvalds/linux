// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Google LLC
 */

#include <linux/arm-smccc.h>
#include <linux/list.h>
#include <linux/percpu-defs.h>
#include <linux/ring_buffer.h>
#include <linux/trace_events.h>
#include <linux/tracefs.h>

#include <asm/kvm_host.h>
#include <asm/kvm_hyptrace.h>
#include <asm/kvm_hypevents_defs.h>

#include "hyp_constants.h"
#include "hyp_trace.h"

#define RB_POLL_MS 1000

#define TRACEFS_DIR "hyp"

static bool hyp_trace_on;
static int hyp_trace_readers;
static struct trace_buffer *hyp_trace_buffer;
static size_t hyp_trace_buffer_size = 7 << 10;
static struct hyp_buffer_pages_backing hyp_buffer_pages_backing;
static DEFINE_MUTEX(hyp_trace_lock);
static DEFINE_PER_CPU(struct mutex, hyp_trace_reader_lock);

static int bpage_backing_setup(struct hyp_trace_pack *pack)
{
	size_t backing_size;
	void *start;

	if (hyp_buffer_pages_backing.start)
		return -EBUSY;

	backing_size = STRUCT_HYP_BUFFER_PAGE_SIZE *
		       pack->trace_buffer_pack.total_pages;
	backing_size = PAGE_ALIGN(backing_size);

	start = alloc_pages_exact(backing_size, GFP_KERNEL_ACCOUNT);
	if (!start)
		return -ENOMEM;

	hyp_buffer_pages_backing.start = (unsigned long)start;
	hyp_buffer_pages_backing.size = backing_size;
	pack->backing.start = (unsigned long)start;
	pack->backing.size = backing_size;

	return 0;
}

static void bpage_backing_teardown(void)
{
	unsigned long backing = hyp_buffer_pages_backing.start;

	if (!hyp_buffer_pages_backing.start)
		return;

	free_pages_exact((void *)backing, hyp_buffer_pages_backing.size);

	hyp_buffer_pages_backing.start = 0;
	hyp_buffer_pages_backing.size = 0;
}

/*
 * Configure the hyp tracing clock. So far, only one is supported: "boot". This
 * clock doesn't stop during suspend making it a good candidate. The downside is
 * if this clock is corrected by NTP while tracing, the hyp clock will slightly
 * drift compared to the host version.
 */
static void hyp_clock_setup(struct hyp_trace_pack *pack)
{
	struct kvm_nvhe_clock_data *clock_data = &pack->trace_clock_data;
	struct system_time_snapshot snap;

	ktime_get_snapshot(&snap);

	clock_data->epoch_cyc = snap.cycles;
	clock_data->epoch_ns = snap.boot;
	clock_data->mult = snap.mono_mult;
	clock_data->shift = snap.mono_shift;
}

static int __swap_reader_page(int cpu)
{
	return kvm_call_hyp_nvhe(__pkvm_rb_swap_reader_page, cpu);
}

static int __update_footers(int cpu)
{
	return kvm_call_hyp_nvhe(__pkvm_rb_update_footers, cpu);
}

struct ring_buffer_ext_cb hyp_cb = {
	.update_footers = __update_footers,
	.swap_reader = __swap_reader_page,
};

static inline int share_page(unsigned long va)
{
	return kvm_call_hyp_nvhe(__pkvm_host_share_hyp, virt_to_pfn(va), 1);
}

static inline int unshare_page(unsigned long va)
{
	return kvm_call_hyp_nvhe(__pkvm_host_unshare_hyp, virt_to_pfn(va), 1);
}

static int trace_pack_pages_apply(struct trace_buffer_pack *trace_pack,
				  int (*func)(unsigned long))
{
	struct ring_buffer_pack *rb_pack;
	int cpu, i, ret;

	for_each_ring_buffer_pack(rb_pack, cpu, trace_pack) {
		ret = func(rb_pack->reader_page_va);
		if (ret)
			return ret;

		for (i = 0; i < rb_pack->nr_pages; i++) {
			ret = func(rb_pack->page_va[i]);
			if (ret)
				return ret;
		}
	}

	return 0;
}

/*
 * hyp_trace_pack size depends on trace_buffer_pack's, so
 * trace_buffer_setup is in charge of the allocation for the former.
 */
static int trace_buffer_setup(struct hyp_trace_pack **pack, size_t *pack_size)
{
	struct trace_buffer_pack *trace_pack;
	int ret;

	hyp_trace_buffer = ring_buffer_alloc_ext(hyp_trace_buffer_size, &hyp_cb);
	if (!hyp_trace_buffer)
		return -ENOMEM;

	*pack_size = offsetof(struct hyp_trace_pack, trace_buffer_pack) +
		     trace_buffer_pack_size(hyp_trace_buffer);
	/*
	 * The hypervisor will unmap the pack from the host to protect the
	 * reading. Page granularity for the pack allocation ensures no other
	 * useful data will be unmapped.
	 */
	*pack_size = PAGE_ALIGN(*pack_size);
	*pack = alloc_pages_exact(*pack_size, GFP_KERNEL);
	if (!*pack) {
		ret = -ENOMEM;
		goto err;
	}

	trace_pack = &(*pack)->trace_buffer_pack;
	WARN_ON(trace_buffer_pack(hyp_trace_buffer, trace_pack));

	ret = trace_pack_pages_apply(trace_pack, share_page);
	if (ret) {
		trace_pack_pages_apply(trace_pack, unshare_page);
		free_pages_exact(*pack, *pack_size);
		goto err;
	}

	return 0;
err:
	ring_buffer_free(hyp_trace_buffer);
	hyp_trace_buffer = NULL;

	return ret;
}

static void trace_buffer_teardown(struct trace_buffer_pack *trace_pack)
{
	bool alloc_trace_pack = !trace_pack;

	if (alloc_trace_pack) {
		trace_pack = kzalloc(trace_buffer_pack_size(hyp_trace_buffer), GFP_KERNEL);
		if (!trace_pack) {
			WARN_ON(1);
			goto end;
		}
	}

	WARN_ON(trace_buffer_pack(hyp_trace_buffer, trace_pack));
	WARN_ON(trace_pack_pages_apply(trace_pack, unshare_page));

	if (alloc_trace_pack)
		kfree(trace_pack);
end:
	ring_buffer_free(hyp_trace_buffer);
	hyp_trace_buffer = NULL;
}

static void hyp_free_tracing(void)
{
	if (!hyp_trace_buffer)
		return;

	trace_buffer_teardown(NULL);
	bpage_backing_teardown();
}

static int hyp_start_tracing(void)
{
	struct hyp_trace_pack *pack;
	size_t pack_size;
	int ret = 0;

	if (hyp_trace_on || hyp_trace_readers)
		return -EBUSY;

	hyp_free_tracing();

	ret = trace_buffer_setup(&pack, &pack_size);
	if (ret)
		return ret;

	hyp_clock_setup(pack);

	ret = bpage_backing_setup(pack);
	if (ret)
		goto end_buffer_teardown;

	ret = kvm_call_hyp_nvhe(__pkvm_start_tracing, (unsigned long)pack, pack_size);
	if (!ret) {
		hyp_trace_on = true;
		goto end_free_pack;
	}

	bpage_backing_teardown();
end_buffer_teardown:
	trace_buffer_teardown(&pack->trace_buffer_pack);
end_free_pack:
	free_pages_exact(pack, pack_size);

	return ret;
}

static void hyp_stop_tracing(void)
{
	int ret;

	if (!hyp_trace_buffer || !hyp_trace_on)
		return;

	ret = kvm_call_hyp_nvhe(__pkvm_stop_tracing);
	if (ret) {
		WARN_ON(1);
		return;
	}

	hyp_trace_on = false;
}

static ssize_t
hyp_tracing_on(struct file *filp, const char __user *ubuf, size_t cnt, loff_t *ppos)
{
	int err = 0;
	char c;

	if (cnt != 2)
		return -EINVAL;

	if (get_user(c, ubuf))
		return -EFAULT;

	mutex_lock(&hyp_trace_lock);

	switch (c) {
	case '1':
		err = hyp_start_tracing();
		break;
	case '0':
		hyp_stop_tracing();
		break;
	default:
		err = -EINVAL;
	}

	mutex_unlock(&hyp_trace_lock);

	return err ? err : cnt;
}

static ssize_t hyp_tracing_on_read(struct file *filp, char __user *ubuf,
				   size_t cnt, loff_t *ppos)
{
	char buf[3];
	int r;

	mutex_lock(&hyp_trace_lock);
	r = sprintf(buf, "%d\n", hyp_trace_on);
	mutex_unlock(&hyp_trace_lock);

	return simple_read_from_buffer(ubuf, cnt, ppos, buf, r);
}

static const struct file_operations hyp_tracing_on_fops = {
	.write	= hyp_tracing_on,
	.read	= hyp_tracing_on_read,
};

static ssize_t hyp_buffer_size(struct file *filp, const char __user *ubuf,
			       size_t cnt, loff_t *ppos)
{
	unsigned long val;
	int ret;

	ret = kstrtoul_from_user(ubuf, cnt, 10, &val);
	if (ret)
		return ret;

	if (!val)
		return -EINVAL;

	mutex_lock(&hyp_trace_lock);
	hyp_trace_buffer_size = val << 10; /* KB to B */
	mutex_unlock(&hyp_trace_lock);

	return cnt;
}

static ssize_t hyp_buffer_size_read(struct file *filp, char __user *ubuf,
				    size_t cnt, loff_t *ppos)
{
	char buf[64];
	int r;

	mutex_lock(&hyp_trace_lock);
	r = sprintf(buf, "%lu\n", hyp_trace_buffer_size >> 10);
	mutex_unlock(&hyp_trace_lock);

	return simple_read_from_buffer(ubuf, cnt, ppos, buf, r);
}

static const struct file_operations hyp_buffer_size_fops = {
	.write	= hyp_buffer_size,
	.read	= hyp_buffer_size_read,
};

static inline void hyp_trace_read_start(int cpu)
{
	mutex_lock(&per_cpu(hyp_trace_reader_lock, cpu));
}

static inline void hyp_trace_read_stop(int cpu)
{
	mutex_unlock(&per_cpu(hyp_trace_reader_lock, cpu));
}

static void ht_print_trace_time(struct ht_iterator *iter)
{
	unsigned long usecs_rem;
	u64 ts_ns = iter->ts;

	do_div(ts_ns, 1000);
	usecs_rem = do_div(ts_ns, USEC_PER_SEC);

	trace_seq_printf(&iter->seq, "[%5lu.%06lu] ",
			 (unsigned long)ts_ns, usecs_rem);
}

extern struct trace_event *ftrace_find_event(int type);

static void ht_print_trace_fmt(struct ht_iterator *iter)
{
	struct trace_event *e;

	if (iter->lost_events)
		trace_seq_printf(&iter->seq, "CPU:%d [LOST %lu EVENTS]\n",
				 iter->cpu, iter->lost_events);

	/* TODO: format bin/hex/raw */

	ht_print_trace_time(iter);

	e = ftrace_find_event(iter->ent->id);
	if (e) {
		e->funcs->trace((struct trace_iterator *)iter, 0, e);
		return;
	}

	trace_seq_printf(&iter->seq, "Unknown event id %d\n", iter->ent->id);
};

static void *ht_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct ht_iterator *iter = m->private;
	struct ring_buffer_event *evt;
	u64 ts;

	(*pos)++;

	evt = ring_buffer_iter_peek(iter->buf_iter, &ts);
	if (!evt)
		return NULL;

	iter->ent = (struct hyp_entry_hdr *)&evt->array[1];
	iter->ts = ts;
	iter->ent_size = evt->array[0];
	ring_buffer_iter_advance(iter->buf_iter);

	return iter;
}

static void *ht_start(struct seq_file *m, loff_t *pos)
{
	struct ht_iterator *iter = m->private;

	if (*pos == 0) {
		ring_buffer_iter_reset(iter->buf_iter);
		(*pos)++;
		iter->ent = NULL;

		return iter;
	}

	hyp_trace_read_start(iter->cpu);

	return ht_next(m, NULL, pos);
}

static void ht_stop(struct seq_file *m, void *v)
{
	struct ht_iterator *iter = m->private;

	hyp_trace_read_stop(iter->cpu);
}

static int ht_show(struct seq_file *m, void *v)
{
	struct ht_iterator *iter = v;

	if (!iter->ent) {
		unsigned long entries, overrun;

		entries = ring_buffer_entries_cpu(hyp_trace_buffer, iter->cpu);
		overrun = ring_buffer_overrun_cpu(hyp_trace_buffer, iter->cpu);

		seq_printf(m, "# entries-in-buffer/entries-written: %lu/%lu\n",
			  entries, overrun + entries);
	} else {
		ht_print_trace_fmt(iter);
		trace_print_seq(m, &iter->seq);
	}

	return 0;
}

static const struct seq_operations hyp_trace_ops = {
	.start	= ht_start,
	.next	= ht_next,
	.stop	= ht_stop,
	.show	= ht_show,
};

static int hyp_trace_open(struct inode *inode, struct file *file)
{
	unsigned long cpu = (unsigned long)inode->i_private;
	struct ht_iterator *iter;
	int ret = 0;

	mutex_lock(&hyp_trace_lock);

	if (!hyp_trace_buffer) {
		ret = -ENODEV;
		goto unlock;
	}

	iter = __seq_open_private(file, &hyp_trace_ops, sizeof(*iter));
	if (!iter) {
		ret = -ENOMEM;
		goto unlock;
	}

	iter->buf_iter = ring_buffer_read_prepare(hyp_trace_buffer, cpu, GFP_KERNEL);
	if (!iter->buf_iter) {
		seq_release_private(inode, file);
		ret = -ENOMEM;
		goto unlock;
	}

	iter->cpu = cpu;

	ring_buffer_read_prepare_sync();
	ring_buffer_read_start(iter->buf_iter);

	hyp_trace_readers++;
unlock:
	mutex_unlock(&hyp_trace_lock);

	return ret;
}

int hyp_trace_release(struct inode *inode, struct file *file)
{
	struct seq_file *m = file->private_data;
	struct ht_iterator *iter = m->private;

	ring_buffer_read_finish(iter->buf_iter);

	mutex_lock(&hyp_trace_lock);
	hyp_trace_readers--;
	mutex_unlock(&hyp_trace_lock);

	return seq_release_private(inode, file);
}

static const struct file_operations hyp_trace_fops = {
	.open  = hyp_trace_open,
	.read  = seq_read,
	.llseek = seq_lseek,
	.release = hyp_trace_release,
};

/*
 * TODO: should be merged with the ring_buffer_iterator version
 */
static void *trace_buffer_peek(struct ht_iterator *iter)
{
	struct ring_buffer_event *event;

	if (ring_buffer_empty_cpu(iter->trace_buffer, iter->cpu))
		return NULL;

	event = ring_buffer_peek(iter->trace_buffer, iter->cpu, &iter->ts, &iter->lost_events);
	if (!event)
		return NULL;

	iter->ent = (struct hyp_entry_hdr *)&event->array[1];
	iter->ent_size = event->array[0];

	return iter;
}

static ssize_t
hyp_trace_pipe_read(struct file *file, char __user *ubuf,
		    size_t cnt, loff_t *ppos)
{
	struct ht_iterator *iter = (struct ht_iterator *)file->private_data;
	struct trace_buffer *trace_buffer = iter->trace_buffer;
	int ret;

	trace_seq_init(&iter->seq);
again:
	ret = ring_buffer_wait(trace_buffer, iter->cpu, 0);
	if (ret < 0)
		return ret;

	hyp_trace_read_start(iter->cpu);
	while (trace_buffer_peek(iter)) {
		unsigned long lost_events;

		ht_print_trace_fmt(iter);
		ring_buffer_consume(iter->trace_buffer, iter->cpu, NULL, &lost_events);
	}
	hyp_trace_read_stop(iter->cpu);

	ret = trace_seq_to_user(&iter->seq, ubuf, cnt);
	if (ret == -EBUSY)
		goto again;

	return ret;
}

static void __poke_reader(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct ht_iterator *iter;

	iter = container_of(dwork, struct ht_iterator, poke_work);

	WARN_ON_ONCE(ring_buffer_poke(iter->trace_buffer, iter->cpu));

	schedule_delayed_work((struct delayed_work *)work,
			      msecs_to_jiffies(RB_POLL_MS));
}

static int hyp_trace_pipe_open(struct inode *inode, struct file *file)
{
	unsigned long cpu = (unsigned long)inode->i_private;
	struct ht_iterator *iter;
	int ret = -EINVAL;

	mutex_lock(&hyp_trace_lock);

	if (!hyp_trace_buffer)
		goto unlock;

	ret = ring_buffer_poke(hyp_trace_buffer, cpu);
	if (ret)
		goto unlock;

	iter = kzalloc(sizeof(*iter), GFP_KERNEL);
	if (!iter) {
		ret = -ENOMEM;
		goto unlock;
	}

	iter->cpu = cpu;
	iter->trace_buffer = hyp_trace_buffer;

	INIT_DELAYED_WORK(&iter->poke_work, __poke_reader);
	schedule_delayed_work(&iter->poke_work, msecs_to_jiffies(RB_POLL_MS));

	file->private_data = iter;

	hyp_trace_readers++;
unlock:
	mutex_unlock(&hyp_trace_lock);

	return ret;
}

static int hyp_trace_pipe_release(struct inode *inode, struct file *file)
{
	struct ht_iterator *iter = file->private_data;

	cancel_delayed_work_sync(&iter->poke_work);

	kfree(iter);

	mutex_lock(&hyp_trace_lock);
	hyp_trace_readers--;
	mutex_unlock(&hyp_trace_lock);

	return 0;
}

static const struct file_operations hyp_trace_pipe_fops = {
	.open		= hyp_trace_pipe_open,
	.read		= hyp_trace_pipe_read,
	.release	= hyp_trace_pipe_release,
	.llseek		= no_llseek,
};

static ssize_t
hyp_trace_raw_read(struct file *file, char __user *ubuf,
		    size_t cnt, loff_t *ppos)
{
	struct ht_iterator *iter = (struct ht_iterator *)file->private_data;
	struct trace_buffer *trace_buffer = iter->trace_buffer;
	size_t size;
	int ret;

	if (iter->copy_leftover)
		goto read;
again:
	hyp_trace_read_start(iter->cpu);
	ret = ring_buffer_read_page(trace_buffer, &iter->spare,
				    cnt, iter->cpu, 0);
	hyp_trace_read_stop(iter->cpu);
	if (ret < 0) {
		if (!ring_buffer_empty_cpu(iter->trace_buffer, iter->cpu))
			return 0;

		ret = ring_buffer_wait(trace_buffer, iter->cpu, 0);
		if (ret < 0)
			return ret;

		goto again;
	}

	iter->copy_leftover = 0;
read:
	size = PAGE_SIZE - iter->copy_leftover;
	if (size > cnt)
		size = cnt;

	ret = copy_to_user(ubuf, iter->spare + PAGE_SIZE - size, size);
	if (ret == size)
		return -EFAULT;

	size -= ret;
	*ppos += size;
	iter->copy_leftover = ret;

	return size;
}

static int hyp_trace_raw_open(struct inode *inode, struct file *file)
{
	int ret = hyp_trace_pipe_open(inode, file);
	struct ht_iterator *iter;

	if (ret)
		return ret;

	iter = file->private_data;
	iter->spare = ring_buffer_alloc_read_page(iter->trace_buffer, iter->cpu);
	if (IS_ERR(iter->spare)) {
		ret = PTR_ERR(iter->spare);
		iter->spare = NULL;
		return ret;
	}

	return 0;
}

static int hyp_trace_raw_release(struct inode *inode, struct file *file)
{
	struct ht_iterator *iter = file->private_data;

	ring_buffer_free_read_page(iter->trace_buffer, iter->cpu, iter->spare);

	return hyp_trace_pipe_release(inode, file);
}

static const struct file_operations hyp_trace_raw_fops = {
	.open		= hyp_trace_raw_open,
	.read		= hyp_trace_raw_read,
	.release	= hyp_trace_raw_release,
	.llseek		= no_llseek,
};

static void hyp_tracefs_create_cpu_file(const char *file_name,
					unsigned long cpu,
					const struct file_operations *fops,
					struct dentry *parent)
{
	if (!tracefs_create_file(file_name, 0440, parent, (void *)cpu, fops))
		pr_warn("Failed to create tracefs %pd/%s\n", parent, file_name);
}

void kvm_hyp_init_events_tracefs(struct dentry *parent);
bool kvm_hyp_events_enable_early(void);

int init_hyp_tracefs(void)
{
	struct dentry *d, *root_dir, *per_cpu_root_dir;
	char per_cpu_name[16];
	unsigned long cpu;
	int err;

	if (!is_protected_kvm_enabled())
		return 0;

	root_dir = tracefs_create_dir(TRACEFS_DIR, NULL);
	if (!root_dir) {
		pr_err("Failed to create tracefs "TRACEFS_DIR"/\n");
		return -ENODEV;
	}

	d = tracefs_create_file("tracing_on", 0640, root_dir, NULL,
				&hyp_tracing_on_fops);
	if (!d) {
		pr_err("Failed to create tracefs "TRACEFS_DIR"/tracing_on\n");
		return -ENODEV;
	}

	d = tracefs_create_file("buffer_size_kb", 0640, root_dir, NULL,
				&hyp_buffer_size_fops);
	if (!d)
		pr_err("Failed to create tracefs "TRACEFS_DIR"/buffer_size_kb\n");


	per_cpu_root_dir = tracefs_create_dir("per_cpu", root_dir);
	if (!per_cpu_root_dir) {
		pr_err("Failed to create tracefs "TRACEFS_DIR"/per_cpu/\n");
		return -ENODEV;
	}

	for_each_possible_cpu(cpu) {
		struct dentry *dir;

		snprintf(per_cpu_name, sizeof(per_cpu_name), "cpu%lu", cpu);
		dir = tracefs_create_dir(per_cpu_name, per_cpu_root_dir);
		if (!dir) {
			pr_warn("Failed to create tracefs "TRACEFS_DIR"/per_cpu/cpu%lu\n",
				cpu);
			continue;
		}

		hyp_tracefs_create_cpu_file("trace", cpu, &hyp_trace_fops, dir);
		hyp_tracefs_create_cpu_file("trace_pipe", cpu,
					    &hyp_trace_pipe_fops, dir);
		hyp_tracefs_create_cpu_file("trace_pipe_raw", cpu,
					    &hyp_trace_raw_fops, dir);
	}

	kvm_hyp_init_events_tracefs(root_dir);
	if (kvm_hyp_events_enable_early()) {
		err = hyp_start_tracing();
		if (err)
			pr_warn("Failed to start early events tracing: %d\n", err);
	}

	return 0;
}
