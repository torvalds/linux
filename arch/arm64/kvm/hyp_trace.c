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

#define RB_POLL_MS 100

#define TRACEFS_DIR "hyp"
#define TRACEFS_MODE_WRITE 0640
#define TRACEFS_MODE_READ 0440

static bool hyp_trace_on;
static bool hyp_free_tracing_deferred;
static int hyp_trace_readers;
static LIST_HEAD(hyp_pipe_readers);
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

static int hyp_load_tracing(void)
{
	struct hyp_trace_pack *pack;
	size_t pack_size;
	int ret;

	ret = trace_buffer_setup(&pack, &pack_size);
	if (ret)
		return ret;

	hyp_clock_setup(pack);

	ret = bpage_backing_setup(pack);
	if (ret)
		goto end_buffer_teardown;

	ret = kvm_call_hyp_nvhe(__pkvm_load_tracing, (unsigned long)pack, pack_size);
	if (!ret)
		goto end_free_pack;

	bpage_backing_teardown();
end_buffer_teardown:
	trace_buffer_teardown(&pack->trace_buffer_pack);
end_free_pack:
	free_pages_exact(pack, pack_size);

	return ret;
}

static void hyp_free_tracing(void)
{
	WARN_ON(hyp_trace_readers || hyp_trace_on);

	if (WARN_ON(kvm_call_hyp_nvhe(__pkvm_teardown_tracing)))
		return;

	trace_buffer_teardown(NULL);
	bpage_backing_teardown();
}

void hyp_poke_tracing(int cpu, const struct cpumask *cpus)
{
	if (cpu == RING_BUFFER_ALL_CPUS) {
		for_each_cpu(cpu, cpus)
			WARN_ON_ONCE(ring_buffer_poke(hyp_trace_buffer, cpu));
	} else {
		WARN_ON_ONCE(ring_buffer_poke(hyp_trace_buffer, cpu));
	}
}

static int hyp_start_tracing(void)
{
	int ret = 0;

	if (hyp_trace_on)
		return -EBUSY;

	if (!hyp_trace_buffer) {
		ret = hyp_load_tracing();
		if (ret)
			return ret;
	}

	ret = kvm_call_hyp_nvhe(__pkvm_enable_tracing, true);
	if (!ret) {
		struct ht_iterator *iter;

		list_for_each_entry(iter, &hyp_pipe_readers, list)
			schedule_delayed_work(&iter->poke_work,
					      msecs_to_jiffies(RB_POLL_MS));
		hyp_trace_on = true;
	}

	return ret;
}

static void hyp_stop_tracing(void)
{
	struct ht_iterator *iter;
	int ret;

	if (!hyp_trace_buffer || !hyp_trace_on)
		return;

	ret = kvm_call_hyp_nvhe(__pkvm_enable_tracing, false);
	if (ret) {
		WARN_ON(1);
		return;
	}

	hyp_trace_on = false;

	list_for_each_entry(iter, &hyp_pipe_readers, list) {
		cancel_delayed_work_sync(&iter->poke_work);
		hyp_poke_tracing(iter->cpu, iter->cpus);
	}
}

static ssize_t
hyp_tracing_on(struct file *filp, const char __user *ubuf, size_t cnt, loff_t *ppos)
{
	int err = 0;
	char c;

	if (!cnt || cnt > 2)
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
	if (cpu != RING_BUFFER_ALL_CPUS) {
		mutex_lock(&per_cpu(hyp_trace_reader_lock, cpu));
		return;
	}

	for_each_possible_cpu(cpu)
		mutex_lock(&per_cpu(hyp_trace_reader_lock, cpu));
}

static inline void hyp_trace_read_stop(int cpu)
{
	if (cpu != RING_BUFFER_ALL_CPUS) {
		mutex_unlock(&per_cpu(hyp_trace_reader_lock, cpu));
		return;
	}

	for_each_possible_cpu(cpu)
		mutex_unlock(&per_cpu(hyp_trace_reader_lock, cpu));
}

static void ht_print_trace_time(struct ht_iterator *iter)
{
	unsigned long usecs_rem;
	u64 ts_ns = iter->ts;

	do_div(ts_ns, 1000);
	usecs_rem = do_div(ts_ns, USEC_PER_SEC);

	trace_seq_printf(&iter->seq, "%5lu.%06lu: ",
			 (unsigned long)ts_ns, usecs_rem);
}

static void ht_print_trace_cpu(struct ht_iterator *iter)
{
	trace_seq_printf(&iter->seq, "[%03d]\t", iter->ent_cpu);
}

extern struct trace_event *ftrace_find_event(int type);

static int ht_print_trace_fmt(struct ht_iterator *iter)
{
	struct trace_event *e;

	if (iter->lost_events)
		trace_seq_printf(&iter->seq, "CPU:%d [LOST %lu EVENTS]\n",
				 iter->ent_cpu, iter->lost_events);

	ht_print_trace_cpu(iter);
	ht_print_trace_time(iter);

	e = ftrace_find_event(iter->ent->id);
	if (e)
		e->funcs->trace((struct trace_iterator *)iter, 0, e);
	else
		trace_seq_printf(&iter->seq, "Unknown event id %d\n", iter->ent->id);

	return trace_seq_has_overflowed(&iter->seq) ? -EOVERFLOW : 0;
};

static struct ring_buffer_event *ht_next_event(struct ht_iterator *iter,
					       u64 *ts, int *cpu)
{
	struct ring_buffer_event *evt = NULL;
	int _cpu;
	u64 _ts;

	if (!iter->buf_iter)
		return NULL;

	if (iter->cpu != RING_BUFFER_ALL_CPUS) {
		evt = ring_buffer_iter_peek(iter->buf_iter[iter->cpu], ts);
		if (!evt)
			return NULL;

		*cpu = iter->cpu;
		ring_buffer_iter_advance(iter->buf_iter[*cpu]);

		return evt;
	}

	*ts = LLONG_MAX;
	for_each_cpu(_cpu, iter->cpus) {
		struct ring_buffer_event *_evt;

		_evt = ring_buffer_iter_peek(iter->buf_iter[_cpu], &_ts);
		if (!_evt)
			continue;

		if (_ts >= *ts)
			continue;

		*ts = _ts;
		*cpu = _cpu;
		evt = _evt;
	}

	if (evt)
		ring_buffer_iter_advance(iter->buf_iter[*cpu]);

	return evt;
}

static void *ht_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct ht_iterator *iter = m->private;
	struct ring_buffer_event *evt;
	int cpu;
	u64 ts;

	(*pos)++;

	evt = ht_next_event(iter, &ts, &cpu);
	if (!evt)
		return NULL;

	iter->ent = (struct hyp_entry_hdr *)&evt->array[1];
	iter->ts = ts;
	iter->ent_size = evt->array[0];
	iter->ent_cpu = cpu;

	return iter;
}

static void ht_iter_reset(struct ht_iterator *iter)
{
	int cpu = iter->cpu;

	if (!iter->buf_iter)
		return;

	if (cpu != RING_BUFFER_ALL_CPUS) {
		ring_buffer_iter_reset(iter->buf_iter[cpu]);
		return;
	}

	for_each_cpu(cpu, iter->cpus)
		ring_buffer_iter_reset(iter->buf_iter[cpu]);
}

static void *ht_start(struct seq_file *m, loff_t *pos)
{
	struct ht_iterator *iter = m->private;

	if (*pos == 0) {
		ht_iter_reset(iter);
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

static void ht_total_entries(struct ht_iterator *iter, unsigned long *entries,
			     unsigned long *overrun)
{
	int cpu = iter->cpu;

	*entries = 0;
	*overrun = 0;

	if (!hyp_trace_buffer)
		return;

	if (cpu != RING_BUFFER_ALL_CPUS) {
		*entries = ring_buffer_entries_cpu(hyp_trace_buffer, cpu);
		*overrun = ring_buffer_overrun_cpu(hyp_trace_buffer, cpu);
		return;
	}

	for_each_cpu(cpu, iter->cpus) {
		*entries += ring_buffer_entries_cpu(hyp_trace_buffer, cpu);
		*overrun += ring_buffer_overrun_cpu(hyp_trace_buffer, cpu);
	}
}

static int ht_show(struct seq_file *m, void *v)
{
	struct ht_iterator *iter = v;

	if (!iter->ent) {
		unsigned long entries, overrun;

		ht_total_entries(iter, &entries, &overrun);
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

static int hyp_trace_reset(int cpu)
{
	if (!hyp_trace_buffer)
		return 0;

	if (hyp_trace_on)
		return -EBUSY;

	if (cpu == RING_BUFFER_ALL_CPUS) {
		if (hyp_trace_readers)
			hyp_free_tracing_deferred = true;
		else
			hyp_free_tracing();

		return 0;
	}

	ring_buffer_reset_cpu(hyp_trace_buffer, cpu);

	return 0;
}

static void hyp_inc_readers(void)
{
	hyp_trace_readers++;
}

static void hyp_dec_readers(void)
{
	hyp_trace_readers--;

	WARN_ON(hyp_trace_readers < 0);

	if (hyp_trace_readers)
		return;

	if (hyp_free_tracing_deferred) {
		hyp_free_tracing();
		hyp_free_tracing_deferred = false;
	}
}

static int hyp_trace_open(struct inode *inode, struct file *file)
{
	int cpu = (s64)inode->i_private;
	int ret = 0;

	mutex_lock(&hyp_trace_lock);

	if (file->f_mode & FMODE_WRITE)
		ret = hyp_trace_reset(cpu);

	mutex_unlock(&hyp_trace_lock);

	return ret;
}

static ssize_t hyp_trace_read(struct file *filp, char __user *ubuf,
			      size_t cnt, loff_t *ppos)
{
	char buf[] = "** Reading trace not yet supported **\n";

	return simple_read_from_buffer(ubuf, cnt, ppos, buf, strlen(buf));
}

static ssize_t hyp_trace_write(struct file *filp, const char __user *ubuf,
			       size_t count, loff_t *ppos)
{
	/* No matter the input, writing resets the buffer */
	return count;
}

static const struct file_operations hyp_trace_fops = {
	.open		= hyp_trace_open,
	.read		= hyp_trace_read,
	.write		= hyp_trace_write,
	.release	= NULL,
};

static struct ring_buffer_event *__ht_next_pipe_event(struct ht_iterator *iter)
{
	struct ring_buffer_event *evt = NULL;
	int cpu = iter->cpu;

	if (cpu != RING_BUFFER_ALL_CPUS) {
		if (ring_buffer_empty_cpu(hyp_trace_buffer, cpu))
			return NULL;

		iter->ent_cpu = cpu;

		return ring_buffer_peek(hyp_trace_buffer, cpu, &iter->ts,
					&iter->lost_events);
	}

	iter->ts = LLONG_MAX;
	for_each_cpu(cpu, iter->cpus) {
		struct ring_buffer_event *_evt;
		unsigned long lost_events;
		u64 ts;

		if (ring_buffer_empty_cpu(hyp_trace_buffer, cpu))
			continue;

		_evt = ring_buffer_peek(hyp_trace_buffer, cpu, &ts,
					&lost_events);
		if (!_evt)
			continue;

		if (ts >= iter->ts)
			continue;

		iter->ts = ts;
		iter->ent_cpu = cpu;
		iter->lost_events = lost_events;
		evt = _evt;

	}

	return evt;
}

static void *ht_next_pipe_event(struct ht_iterator *iter)
{
	struct ring_buffer_event *event;

	event = __ht_next_pipe_event(iter);
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
	int ret;

	/* seq_buf buffer size */
	if (cnt != PAGE_SIZE)
		return -EINVAL;

	trace_seq_init(&iter->seq);
again:
	ret = ring_buffer_wait(hyp_trace_buffer, iter->cpu, 0);
	if (ret < 0)
		return ret;

	hyp_trace_read_start(iter->cpu);
	while (ht_next_pipe_event(iter)) {
		int prev_len = iter->seq.seq.len;

		if (ht_print_trace_fmt(iter)) {
			iter->seq.seq.len = prev_len;
			break;
		}

		ring_buffer_consume(hyp_trace_buffer, iter->ent_cpu, NULL,
				    NULL);
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

	hyp_poke_tracing(iter->cpu, iter->cpus);

	schedule_delayed_work((struct delayed_work *)work,
			      msecs_to_jiffies(RB_POLL_MS));
}

static int hyp_trace_pipe_open(struct inode *inode, struct file *file)
{
	int cpu = (s64)inode->i_private;
	struct ht_iterator *iter;
	int ret;

	mutex_lock(&hyp_trace_lock);

	if (!hyp_trace_buffer) {
		ret = hyp_load_tracing();
		if (ret)
			goto unlock;
	}

	iter = kzalloc(sizeof(*iter), GFP_KERNEL);
	if (!iter) {
		ret = -ENOMEM;
		goto unlock;
	}

	iter->cpu = cpu;
	file->private_data = iter;

	if (cpu == RING_BUFFER_ALL_CPUS) {
		if (!zalloc_cpumask_var(&iter->cpus, GFP_KERNEL)) {
			ret = -ENOMEM;
			goto unlock;
		}
		for_each_possible_cpu(cpu) {
			if (!ring_buffer_poke(hyp_trace_buffer, cpu))
				cpumask_set_cpu(cpu, iter->cpus);
		}
	} else {
		ret = ring_buffer_poke(hyp_trace_buffer, cpu);
		if (ret)
			goto unlock;
	}

	INIT_DELAYED_WORK(&iter->poke_work, __poke_reader);
	if (hyp_trace_on)
		schedule_delayed_work(&iter->poke_work,
				      msecs_to_jiffies(RB_POLL_MS));
	list_add(&iter->list, &hyp_pipe_readers);
	hyp_inc_readers();
unlock:
	mutex_unlock(&hyp_trace_lock);
	if (ret)
		kfree(iter);

	return ret;
}

static int hyp_trace_pipe_release(struct inode *inode, struct file *file)
{
	struct ht_iterator *iter = file->private_data;

	mutex_lock(&hyp_trace_lock);
	hyp_dec_readers();
	list_del(&iter->list);
	mutex_unlock(&hyp_trace_lock);

	cancel_delayed_work_sync(&iter->poke_work);

	free_cpumask_var(iter->cpus);
	kfree(iter);

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
	size_t size;
	int ret;

	if (iter->copy_leftover)
		goto read;
again:
	hyp_trace_read_start(iter->cpu);
	ret = ring_buffer_read_page(hyp_trace_buffer, &iter->spare,
				    cnt, iter->cpu, 0);
	hyp_trace_read_stop(iter->cpu);
	if (ret < 0) {
		if (!ring_buffer_empty_cpu(hyp_trace_buffer, iter->cpu))
			return 0;

		ret = ring_buffer_wait(hyp_trace_buffer, iter->cpu, 0);
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
	iter->spare = ring_buffer_alloc_read_page(hyp_trace_buffer, iter->cpu);
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

	ring_buffer_free_read_page(hyp_trace_buffer, iter->cpu, iter->spare);

	return hyp_trace_pipe_release(inode, file);
}

static const struct file_operations hyp_trace_raw_fops = {
	.open		= hyp_trace_raw_open,
	.read		= hyp_trace_raw_read,
	.release	= hyp_trace_raw_release,
	.llseek		= no_llseek,
};

static int hyp_trace_clock_show(struct seq_file *m, void *v)
{
	seq_printf(m, "[boot]\n");
	return 0;
}

static int hyp_trace_clock_open(struct inode *inode, struct file *file)
{
	return single_open(file, hyp_trace_clock_show, NULL);
}

static const struct file_operations hyp_trace_clock_fops = {
	.open = hyp_trace_clock_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static void hyp_tracefs_create_cpu_file(const char *file_name,
					int cpu,
					umode_t mode,
					const struct file_operations *fops,
					struct dentry *parent)
{
	if (!tracefs_create_file(file_name, mode, parent, (void *)(s64)cpu, fops))
		pr_warn("Failed to create tracefs %pd/%s\n", parent, file_name);
}

void kvm_hyp_init_events_tracefs(struct dentry *parent);
bool kvm_hyp_events_enable_early(void);

int init_hyp_tracefs(void)
{
	struct dentry *d, *root_dir, *per_cpu_root_dir;
	char per_cpu_name[16];
	int err, cpu;

	if (!is_protected_kvm_enabled())
		return 0;

	root_dir = tracefs_create_dir(TRACEFS_DIR, NULL);
	if (!root_dir) {
		pr_err("Failed to create tracefs "TRACEFS_DIR"/\n");
		return -ENODEV;
	}

	d = tracefs_create_file("tracing_on", TRACEFS_MODE_WRITE, root_dir,
				NULL, &hyp_tracing_on_fops);
	if (!d) {
		pr_err("Failed to create tracefs "TRACEFS_DIR"/tracing_on\n");
		return -ENODEV;
	}

	d = tracefs_create_file("buffer_size_kb", TRACEFS_MODE_WRITE, root_dir,
				NULL, &hyp_buffer_size_fops);
	if (!d)
		pr_err("Failed to create tracefs "TRACEFS_DIR"/buffer_size_kb\n");

	d = tracefs_create_file("trace_clock", TRACEFS_MODE_READ, root_dir, NULL,
				&hyp_trace_clock_fops);
	if (!d)
		pr_err("Failed to create tracefs "TRACEFS_DIR"/trace_clock\n");

	hyp_tracefs_create_cpu_file("trace", RING_BUFFER_ALL_CPUS,
				    TRACEFS_MODE_WRITE, &hyp_trace_fops,
				    root_dir);

	hyp_tracefs_create_cpu_file("trace_pipe", RING_BUFFER_ALL_CPUS,
				    TRACEFS_MODE_READ, &hyp_trace_pipe_fops,
				    root_dir);

	per_cpu_root_dir = tracefs_create_dir("per_cpu", root_dir);
	if (!per_cpu_root_dir) {
		pr_err("Failed to create tracefs "TRACEFS_DIR"/per_cpu/\n");
		return -ENODEV;
	}

	for_each_possible_cpu(cpu) {
		struct dentry *dir;

		snprintf(per_cpu_name, sizeof(per_cpu_name), "cpu%d", cpu);
		dir = tracefs_create_dir(per_cpu_name, per_cpu_root_dir);
		if (!dir) {
			pr_warn("Failed to create tracefs "TRACEFS_DIR"/per_cpu/cpu%d\n",
				cpu);
			continue;
		}

		hyp_tracefs_create_cpu_file("trace", cpu, TRACEFS_MODE_WRITE,
					    &hyp_trace_fops, dir);
		hyp_tracefs_create_cpu_file("trace_pipe", cpu, TRACEFS_MODE_READ,
					    &hyp_trace_pipe_fops, dir);
		hyp_tracefs_create_cpu_file("trace_pipe_raw", cpu,
					    TRACEFS_MODE_READ,
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
