/*
 * transport.c - ktap transport functionality
 *
 * This file is part of ktap by Jovi Zhangwei.
 *
 * Copyright (C) 2012-2013 Jovi Zhangwei <jovi.zhangwei@gmail.com>.
 *
 * ktap is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * ktap is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/debugfs.h>
#include <linux/ftrace_event.h>
#include <linux/stacktrace.h>
#include <linux/clocksource.h>
#include <asm/uaccess.h>
#include "../include/ktap.h"

struct ktap_trace_iterator {
	struct ring_buffer	*buffer;
	int			print_timestamp;
	void			*private;

	struct trace_iterator	iter;
};

enum ktap_trace_type {
	__TRACE_FIRST_TYPE = 0,

	TRACE_FN = 1, /* must be same as ftrace definition in kernel */
	TRACE_PRINT,
	TRACE_BPUTS,
	TRACE_STACK,
	TRACE_USER_STACK,

	__TRACE_LAST_TYPE,
};

#define KTAP_TRACE_ITER(iter)	\
	container_of(iter, struct ktap_trace_iterator, iter)

ssize_t trace_seq_to_user(struct trace_seq *s, char __user *ubuf, size_t cnt)
{
	int len;
	int ret;

	if (!cnt)
		return 0;

	if (s->len <= s->readpos)
		return -EBUSY;

	len = s->len - s->readpos;
	if (cnt > len)
		cnt = len;
	ret = copy_to_user(ubuf, s->buffer + s->readpos, cnt);
	if (ret == cnt)
		return -EFAULT;

	cnt -= ret;

	s->readpos += cnt;
	return cnt;
}

int trace_seq_puts(struct trace_seq *s, const char *str)
{
	int len = strlen(str);

	if (s->full)
		return 0;

	if (len > ((PAGE_SIZE - 1) - s->len)) {
		s->full = 1;
		return 0;
	}

	memcpy(s->buffer + s->len, str, len);
	s->len += len;

	return len;
}

static int trace_empty(struct trace_iterator *iter)
{
	struct ktap_trace_iterator *ktap_iter = KTAP_TRACE_ITER(iter);
	int cpu;

	for_each_online_cpu(cpu) {
		if (!ring_buffer_empty_cpu(ktap_iter->buffer, cpu))
			return 0;
	}

	return 1;
}

static void trace_consume(struct trace_iterator *iter)
{
	struct ktap_trace_iterator *ktap_iter = KTAP_TRACE_ITER(iter);

	ring_buffer_consume(ktap_iter->buffer, iter->cpu, &iter->ts,
			    &iter->lost_events);
}

unsigned long long ns2usecs(cycle_t nsec)
{
	nsec += 500;
	do_div(nsec, 1000);
	return nsec;
}

static int trace_print_timestamp(struct trace_iterator *iter)
{
	struct trace_seq *s = &iter->seq;
	unsigned long long t;
	unsigned long secs, usec_rem;

	t = ns2usecs(iter->ts);
	usec_rem = do_div(t, USEC_PER_SEC);
	secs = (unsigned long)t;

	return trace_seq_printf(s, "%5lu.%06lu: ", secs, usec_rem);
}

/* todo: export kernel function ftrace_find_event in future, and make faster */
static struct trace_event *(*ftrace_find_event)(int type);

static enum print_line_t print_trace_fmt(struct trace_iterator *iter)
{
	struct ktap_trace_iterator *ktap_iter = KTAP_TRACE_ITER(iter);
	struct trace_entry *entry = iter->ent;
	struct trace_event *ev;

	ev = ftrace_find_event(entry->type);

	if (ktap_iter->print_timestamp && !trace_print_timestamp(iter))
		return TRACE_TYPE_PARTIAL_LINE;

	if (ev) {
		int ret = ev->funcs->trace(iter, 0, ev);

		/* overwrite '\n' at the ending */
		iter->seq.buffer[iter->seq.len - 1] = '\0';
		iter->seq.len--;
		return ret;
	}

	return TRACE_TYPE_PARTIAL_LINE;
}

static enum print_line_t print_trace_stack(struct trace_iterator *iter)
{
	struct trace_entry *entry = iter->ent;
	struct stack_trace trace;
	char str[KSYM_SYMBOL_LEN];
	int i;

	trace.entries = (unsigned long *)(entry + 1);
	trace.nr_entries = (iter->ent_size - sizeof(*entry)) /
			   sizeof(unsigned long);

	if (!trace_seq_puts(&iter->seq, "<stack trace>\n"))
		return TRACE_TYPE_PARTIAL_LINE;

	for (i = 0; i < trace.nr_entries; i++) {
		unsigned long p = trace.entries[i];

		if (p == ULONG_MAX)
			break;

		sprint_symbol(str, p);
		if (!trace_seq_printf(&iter->seq, " => %s\n", str))
			return TRACE_TYPE_PARTIAL_LINE;
	}

	return TRACE_TYPE_HANDLED;
}

struct ktap_ftrace_entry {
	struct trace_entry entry;
	unsigned long ip;
	unsigned long parent_ip;
};

static enum print_line_t print_trace_fn(struct trace_iterator *iter)
{
	struct ktap_trace_iterator *ktap_iter = KTAP_TRACE_ITER(iter);
	struct ktap_ftrace_entry *field = (struct ktap_ftrace_entry *)iter->ent;
	char str[KSYM_SYMBOL_LEN];

	if (ktap_iter->print_timestamp && !trace_print_timestamp(iter))
		return TRACE_TYPE_PARTIAL_LINE;

	sprint_symbol(str, field->ip);
	if (!trace_seq_puts(&iter->seq, str))
		return TRACE_TYPE_PARTIAL_LINE;

	if (!trace_seq_puts(&iter->seq, " <- "))
		return TRACE_TYPE_PARTIAL_LINE;

	sprint_symbol(str, field->parent_ip);
	if (!trace_seq_puts(&iter->seq, str))
		return TRACE_TYPE_PARTIAL_LINE;

	return TRACE_TYPE_HANDLED;
}

static enum print_line_t print_trace_bputs(struct trace_iterator *iter)
{
	if (!trace_seq_puts(&iter->seq,
			    (const char *)(*(unsigned long *)(iter->ent + 1))))
		return TRACE_TYPE_PARTIAL_LINE;

	return TRACE_TYPE_HANDLED;
}

static enum print_line_t print_trace_line(struct trace_iterator *iter)
{
	struct trace_entry *entry = iter->ent;
	char *str = (char *)(entry + 1);

	if (entry->type == TRACE_PRINT) {
		if (!trace_seq_printf(&iter->seq, "%s", str))
			return TRACE_TYPE_PARTIAL_LINE;

		return TRACE_TYPE_HANDLED;
	}

	if (entry->type == TRACE_BPUTS)
		return print_trace_bputs(iter);

	if (entry->type == TRACE_STACK)
		return print_trace_stack(iter);

	if (entry->type == TRACE_FN)
		return print_trace_fn(iter);

	return print_trace_fmt(iter);
}

static struct trace_entry *
peek_next_entry(struct trace_iterator *iter, int cpu, u64 *ts,
		unsigned long *lost_events)
{
	struct ktap_trace_iterator *ktap_iter = KTAP_TRACE_ITER(iter);
	struct ring_buffer_event *event;

	event = ring_buffer_peek(ktap_iter->buffer, cpu, ts, lost_events);
	if (event) {
		iter->ent_size = ring_buffer_event_length(event);
		return ring_buffer_event_data(event);
	}

	return NULL;
}

static struct trace_entry *
__find_next_entry(struct trace_iterator *iter, int *ent_cpu,
		  unsigned long *missing_events, u64 *ent_ts)
{
	struct ktap_trace_iterator *ktap_iter = KTAP_TRACE_ITER(iter);
	struct ring_buffer *buffer = ktap_iter->buffer;
	struct trace_entry *ent, *next = NULL;
	unsigned long lost_events = 0, next_lost = 0;
	u64 next_ts = 0, ts;
	int next_cpu = -1;
	int next_size = 0;
	int cpu;

	for_each_online_cpu(cpu) {
		if (ring_buffer_empty_cpu(buffer, cpu))
			continue;

		ent = peek_next_entry(iter, cpu, &ts, &lost_events);
		/*
		 * Pick the entry with the smallest timestamp:
		 */
		if (ent && (!next || ts < next_ts)) {
			next = ent;
			next_cpu = cpu;
			next_ts = ts;
			next_lost = lost_events;
			next_size = iter->ent_size;
		}
	}

	iter->ent_size = next_size;

	if (ent_cpu)
		*ent_cpu = next_cpu;

	if (ent_ts)
		*ent_ts = next_ts;

	if (missing_events)
		*missing_events = next_lost;

	return next;
}

/* Find the next real entry, and increment the iterator to the next entry */
static void *trace_find_next_entry_inc(struct trace_iterator *iter)
{
	iter->ent = __find_next_entry(iter, &iter->cpu,
				      &iter->lost_events, &iter->ts);
	if (iter->ent)
		iter->idx++;

	return iter->ent ? iter : NULL;
}

static void poll_wait_pipe(void)
{
	set_current_state(TASK_INTERRUPTIBLE);
	/* sleep for 100 msecs, and try again. */
	schedule_timeout(HZ / 10);
}

static int tracing_wait_pipe(struct file *filp)
{
	struct trace_iterator *iter = filp->private_data;
	struct ktap_trace_iterator *ktap_iter = KTAP_TRACE_ITER(iter);
	ktap_state *ks = ktap_iter->private;

	while (trace_empty(iter)) {

		if ((filp->f_flags & O_NONBLOCK)) {
			return -EAGAIN;
		}

		mutex_unlock(&iter->mutex);

		poll_wait_pipe();

		mutex_lock(&iter->mutex);

		if (G(ks)->wait_user && trace_empty(iter))
			return -EINTR;
	}

	return 1;
}

static ssize_t
tracing_read_pipe(struct file *filp, char __user *ubuf, size_t cnt,
		  loff_t *ppos)
{
	struct trace_iterator *iter = filp->private_data;
	ssize_t sret;

	/* return any leftover data */
	sret = trace_seq_to_user(&iter->seq, ubuf, cnt);
	if (sret != -EBUSY)
		return sret;
	/*
	 * Avoid more than one consumer on a single file descriptor
	 * This is just a matter of traces coherency, the ring buffer itself
	 * is protected.
	 */
	mutex_lock(&iter->mutex);

waitagain:
	sret = tracing_wait_pipe(filp);
	if (sret <= 0)
		goto out;

	/* stop when tracing is finished */
	if (trace_empty(iter)) {
		sret = 0;
		goto out;
	}

	if (cnt >= PAGE_SIZE)
		cnt = PAGE_SIZE - 1;

	/* reset all but tr, trace, and overruns */
	memset(&iter->seq, 0,
	       sizeof(struct trace_iterator) -
	       offsetof(struct trace_iterator, seq));
	iter->pos = -1;

	while (trace_find_next_entry_inc(iter) != NULL) {
		enum print_line_t ret;
		int len = iter->seq.len;

		ret = print_trace_line(iter);
		if (ret == TRACE_TYPE_PARTIAL_LINE) {
			/* don't print partial lines */
			iter->seq.len = len;
			break;
		}
		if (ret != TRACE_TYPE_NO_CONSUME)
			trace_consume(iter);

		if (iter->seq.len >= cnt)
			break;

		/*
		 * Setting the full flag means we reached the trace_seq buffer
		 * size and we should leave by partial output condition above.
		 * One of the trace_seq_* functions is not used properly.
		 */
		WARN_ONCE(iter->seq.full, "full flag set for trace type %d",
			  iter->ent->type);
	}

	/* Now copy what we have to the user */
	sret = trace_seq_to_user(&iter->seq, ubuf, cnt);
	if (iter->seq.readpos >= iter->seq.len)
		trace_seq_init(&iter->seq);

	/*
	 * If there was nothing to send to user, in spite of consuming trace
	 * entries, go back to wait for more entries.
	 */
	if (sret == -EBUSY)
		goto waitagain;

out:
	mutex_unlock(&iter->mutex);

	return sret;
}

static int tracing_open_pipe(struct inode *inode, struct file *filp)
{
	struct ktap_trace_iterator *ktap_iter;
	ktap_state *ks = inode->i_private;

	/* create a buffer to store the information to pass to userspace */
	ktap_iter = kzalloc(sizeof(*ktap_iter), GFP_KERNEL);
	if (!ktap_iter)
		return -ENOMEM;

	ktap_iter->private = ks;
	ktap_iter->buffer = G(ks)->buffer;
	ktap_iter->print_timestamp = G(ks)->parm->print_timestamp;
	mutex_init(&ktap_iter->iter.mutex);
	filp->private_data = &ktap_iter->iter;

	nonseekable_open(inode, filp);

	return 0;
}

static int tracing_release_pipe(struct inode *inode, struct file *file)
{
	struct trace_iterator *iter = file->private_data;
	struct ktap_trace_iterator *ktap_iter = KTAP_TRACE_ITER(iter);

	mutex_destroy(&iter->mutex);
	kfree(ktap_iter);
	return 0;
}

static const struct file_operations tracing_pipe_fops = {
	.open		= tracing_open_pipe,
	.read		= tracing_read_pipe,
	.splice_read	= NULL,
	.release	= tracing_release_pipe,
	.llseek		= no_llseek,
};

/*
 * print_backtrace maybe called from ktap mainthread, so be
 * care on race with event closure thread.
 *
 * preempt disabled in ring_buffer_lock_reserve
 *
 * The implementation is similar with funtion __ftrace_trace_stack.
 */
void kp_transport_print_backtrace(ktap_state *ks)
{
	struct ring_buffer *buffer = G(ks)->buffer;
	struct ring_buffer_event *event;
	struct trace_entry *entry;
	int size;

	size = KTAP_STACK_MAX_ENTRIES * sizeof(unsigned long);
	event = ring_buffer_lock_reserve(buffer, sizeof(*entry) + size);
	if (!event) {
		return;
	} else {
		struct stack_trace trace;

		entry = ring_buffer_event_data(event);
		tracing_generic_entry_update(entry, 0, 0);
		entry->type = TRACE_STACK;

		trace.nr_entries = 0;
		trace.skip = 10;
		trace.max_entries = KTAP_STACK_MAX_ENTRIES;
		trace.entries = (unsigned long *)(entry + 1);
		save_stack_trace(&trace);

		ring_buffer_unlock_commit(buffer, event);
	}

	return;
}

void kp_transport_event_write(ktap_state *ks, struct ktap_event *e)
{
	struct ring_buffer *buffer = G(ks)->buffer;
	struct ring_buffer_event *event;
	struct trace_entry *entry;

	event = ring_buffer_lock_reserve(buffer, e->entry_size +
					 sizeof(struct ftrace_event_call *));
	if (!event) {
		return;
	} else {
		entry = ring_buffer_event_data(event);

		memcpy(entry, e->entry, e->entry_size);

		ring_buffer_unlock_commit(buffer, event);
	}
}

void kp_transport_write(ktap_state *ks, const void *data, size_t length)
{
	struct ring_buffer *buffer = G(ks)->buffer;
	struct ring_buffer_event *event;
	struct trace_entry *entry;
	int size;

	size = sizeof(struct trace_entry) + length;

	event = ring_buffer_lock_reserve(buffer, size);
	if (!event) {
		return;
	} else {
		entry = ring_buffer_event_data(event);

		tracing_generic_entry_update(entry, 0, 0);
		entry->type = TRACE_PRINT;
		memcpy(entry + 1, data, length);

		ring_buffer_unlock_commit(buffer, event);
	}
}

/* general print function */
void kp_printf(ktap_state *ks, const char *fmt, ...)
{
	char buff[1024];
	va_list args;
	int len;

	va_start(args, fmt);
	len = vscnprintf(buff, 1024, fmt, args);
	va_end(args);

	buff[len] = '\0';
	kp_transport_write(ks, buff, len + 1);
}

void __kp_puts(ktap_state *ks, const char *str)
{
	kp_transport_write(ks, str, strlen(str) + 1);
}

void __kp_bputs(ktap_state *ks, const char *str)
{
	struct ring_buffer *buffer = G(ks)->buffer;
	struct ring_buffer_event *event;
	struct trace_entry *entry;
	int size;

	size = sizeof(struct trace_entry) + sizeof(unsigned long *);

	event = ring_buffer_lock_reserve(buffer, size);
	if (!event) {
		return;
	} else {
		entry = ring_buffer_event_data(event);

		tracing_generic_entry_update(entry, 0, 0);
		entry->type = TRACE_BPUTS;
		*(unsigned long *)(entry + 1) = (unsigned long)str;

		ring_buffer_unlock_commit(buffer, event);
	}
}

void kp_transport_exit(ktap_state *ks)
{
	ring_buffer_free(G(ks)->buffer);
	debugfs_remove(G(ks)->trace_pipe_dentry);
}

#define TRACE_BUF_SIZE_DEFAULT	1441792UL /* 16384 * 88 (sizeof(entry)) */

int kp_transport_init(ktap_state *ks, struct dentry *dir)
{
	struct ring_buffer *buffer;
	struct dentry *dentry;
	char filename[32] = {0};

	ftrace_find_event = (void *)kallsyms_lookup_name("ftrace_find_event");
	if (!ftrace_find_event) {
		printk("ktap: cannot lookup ftrace_find_event in kallsyms\n");
		return -EINVAL;
	}

	buffer = ring_buffer_alloc(TRACE_BUF_SIZE_DEFAULT, RB_FL_OVERWRITE);
	if (!buffer)
		return -ENOMEM;

	sprintf(filename, "trace_pipe_%d", (int)task_tgid_vnr(current));

	dentry = debugfs_create_file(filename, 0444, dir,
				     ks, &tracing_pipe_fops);
	if (!dentry) {
		pr_err("ktapvm: cannot create trace_pipe file in debugfs\n");
		ring_buffer_free(buffer);
		return -1;
	}

	G(ks)->buffer = buffer;
	G(ks)->trace_pipe_dentry = dentry;

	return 0;
}

