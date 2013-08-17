/*
 * Copyright (C) 2012 Google, Inc.
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

#include <linux/debugfs.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/persistent_ram.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/slab.h>

#include "../../../kernel/trace/trace.h"

struct persistent_trace_record {
	unsigned long ip;
	unsigned long parent_ip;
};

#define REC_SIZE sizeof(struct persistent_trace_record)

static struct persistent_ram_zone *persistent_trace;

static int persistent_trace_enabled;

static struct trace_array *persistent_trace_array;

static struct ftrace_ops trace_ops;

static int persistent_tracer_init(struct trace_array *tr)
{
	persistent_trace_array = tr;
	tr->cpu = get_cpu();
	put_cpu();

	tracing_start_cmdline_record();

	persistent_trace_enabled = 0;
	smp_wmb();

	register_ftrace_function(&trace_ops);

	smp_wmb();
	persistent_trace_enabled = 1;

	return 0;
}

static void persistent_trace_reset(struct trace_array *tr)
{
	persistent_trace_enabled = 0;
	smp_wmb();

	unregister_ftrace_function(&trace_ops);

	tracing_stop_cmdline_record();
}

static void persistent_trace_start(struct trace_array *tr)
{
	tracing_reset_online_cpus(tr);
}

static void persistent_trace_call(unsigned long ip, unsigned long parent_ip)
{
	struct trace_array *tr = persistent_trace_array;
	struct trace_array_cpu *data;
	long disabled;
	struct persistent_trace_record rec;
	unsigned long flags;
	int cpu;

	smp_rmb();
	if (unlikely(!persistent_trace_enabled))
		return;

	if (unlikely(oops_in_progress))
		return;

	/*
	 * Need to use raw, since this must be called before the
	 * recursive protection is performed.
	 */
	local_irq_save(flags);
	cpu = raw_smp_processor_id();
	data = tr->data[cpu];
	disabled = atomic_inc_return(&data->disabled);

	if (likely(disabled == 1)) {
		rec.ip = ip;
		rec.parent_ip = parent_ip;
		rec.ip |= cpu;
		persistent_ram_write(persistent_trace, &rec, sizeof(rec));
	}

	atomic_dec(&data->disabled);
	local_irq_restore(flags);
}

static struct ftrace_ops trace_ops __read_mostly = {
	.func = persistent_trace_call,
	.flags = FTRACE_OPS_FL_GLOBAL,
};

static struct tracer persistent_tracer __read_mostly = {
	.name		= "persistent",
	.init		= persistent_tracer_init,
	.reset		= persistent_trace_reset,
	.start		= persistent_trace_start,
	.wait_pipe	= poll_wait_pipe,
};

struct persistent_trace_seq_data {
	const void *ptr;
	size_t off;
	size_t size;
};

void *persistent_trace_seq_start(struct seq_file *s, loff_t *pos)
{
	struct persistent_trace_seq_data *data;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return NULL;

	data->ptr = persistent_ram_old(persistent_trace);
	data->size = persistent_ram_old_size(persistent_trace);
	data->off = data->size % REC_SIZE;

	data->off += *pos * REC_SIZE;

	if (data->off + REC_SIZE > data->size) {
		kfree(data);
		return NULL;
	}

	return data;

}
void persistent_trace_seq_stop(struct seq_file *s, void *v)
{
	kfree(v);
}

void *persistent_trace_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	struct persistent_trace_seq_data *data = v;

	data->off += REC_SIZE;

	if (data->off + REC_SIZE > data->size)
		return NULL;

	(*pos)++;

	return data;
}

int persistent_trace_seq_show(struct seq_file *s, void *v)
{
	struct persistent_trace_seq_data *data = v;
	struct persistent_trace_record *rec;

	rec = (struct persistent_trace_record *)(data->ptr + data->off);

	seq_printf(s, "%ld %08lx  %08lx  %pf <- %pF\n",
		rec->ip & 3, rec->ip, rec->parent_ip,
		(void *)rec->ip, (void *)rec->parent_ip);

	return 0;
}

static const struct seq_operations persistent_trace_seq_ops = {
	.start = persistent_trace_seq_start,
	.next = persistent_trace_seq_next,
	.stop = persistent_trace_seq_stop,
	.show = persistent_trace_seq_show,
};

static int persistent_trace_old_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &persistent_trace_seq_ops);
}

static const struct file_operations persistent_trace_old_fops = {
	.open		= persistent_trace_old_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static int __devinit persistent_trace_probe(struct platform_device *pdev)
{
	struct dentry *d;
	int ret;

	persistent_trace = persistent_ram_init_ringbuffer(&pdev->dev, false);
	if (IS_ERR(persistent_trace)) {
		pr_err("persistent_trace: failed to init ringbuffer: %ld\n",
				PTR_ERR(persistent_trace));
		return PTR_ERR(persistent_trace);
	}

	ret = register_tracer(&persistent_tracer);
	if (ret)
		pr_err("persistent_trace: failed to register tracer");

	if (persistent_ram_old_size(persistent_trace) > 0) {
		d = debugfs_create_file("persistent_trace", S_IRUGO, NULL,
			NULL, &persistent_trace_old_fops);
		if (IS_ERR_OR_NULL(d))
			pr_err("persistent_trace: failed to create old file\n");
	}

	return 0;
}

static struct platform_driver persistent_trace_driver  = {
	.probe = persistent_trace_probe,
	.driver		= {
		.name	= "persistent_trace",
	},
};

static int __init persistent_trace_init(void)
{
	return platform_driver_register(&persistent_trace_driver);
}
core_initcall(persistent_trace_init);
