/*
 * Copyright (C) 2010 Google, Inc.
 * Author: Erik Gilling <konkers@android.com>
 *
 * Copyright (C) 2011-2013 NVIDIA Corporation
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

#include <linux/defs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>

#include <linux/io.h>

#include "dev.h"
#include "de.h"
#include "channel.h"

unsigned int host1x_de_trace_cmdbuf;

static pid_t host1x_de_force_timeout_pid;
static u32 host1x_de_force_timeout_val;
static u32 host1x_de_force_timeout_channel;

void host1x_de_output(struct output *o, const char *fmt, ...)
{
	va_list args;
	int len;

	va_start(args, fmt);
	len = vsnprintf(o->buf, sizeof(o->buf), fmt, args);
	va_end(args);

	o->fn(o->ctx, o->buf, len, false);
}

void host1x_de_cont(struct output *o, const char *fmt, ...)
{
	va_list args;
	int len;

	va_start(args, fmt);
	len = vsnprintf(o->buf, sizeof(o->buf), fmt, args);
	va_end(args);

	o->fn(o->ctx, o->buf, len, true);
}

static int show_channel(struct host1x_channel *ch, void *data, bool show_fifo)
{
	struct host1x *m = dev_get_drvdata(ch->dev->parent);
	struct output *o = data;

	mutex_lock(&ch->cdma.lock);

	if (show_fifo)
		host1x_hw_show_channel_fifo(m, ch, o);

	host1x_hw_show_channel_cdma(m, ch, o);

	mutex_unlock(&ch->cdma.lock);

	return 0;
}

static void show_syncpts(struct host1x *m, struct output *o)
{
	unsigned int i;

	host1x_de_output(o, "---- syncpts ----\n");

	for (i = 0; i < host1x_syncpt_nb_pts(m); i++) {
		u32 max = host1x_syncpt_read_max(m->syncpt + i);
		u32 min = host1x_syncpt_load(m->syncpt + i);

		if (!min && !max)
			continue;

		host1x_de_output(o, "id %u (%s) min %d max %d\n",
				    i, m->syncpt[i].name, min, max);
	}

	for (i = 0; i < host1x_syncpt_nb_bases(m); i++) {
		u32 base_val;

		base_val = host1x_syncpt_load_wait_base(m->syncpt + i);
		if (base_val)
			host1x_de_output(o, "waitbase id %u val %d\n", i,
					    base_val);
	}

	host1x_de_output(o, "\n");
}

static void show_all(struct host1x *m, struct output *o, bool show_fifo)
{
	unsigned int i;

	host1x_hw_show_mlocks(m, o);
	show_syncpts(m, o);
	host1x_de_output(o, "---- channels ----\n");

	for (i = 0; i < m->info->nb_channels; ++i) {
		struct host1x_channel *ch = host1x_channel_get_index(m, i);

		if (ch) {
			show_channel(ch, o, show_fifo);
			host1x_channel_put(ch);
		}
	}
}

static int host1x_de_show_all(struct seq_file *s, void *unused)
{
	struct output o = {
		.fn = write_to_seqfile,
		.ctx = s
	};

	show_all(s->private, &o, true);

	return 0;
}

static int host1x_de_show(struct seq_file *s, void *unused)
{
	struct output o = {
		.fn = write_to_seqfile,
		.ctx = s
	};

	show_all(s->private, &o, false);

	return 0;
}

static int host1x_de_open_all(struct inode *inode, struct file *file)
{
	return single_open(file, host1x_de_show_all, inode->i_private);
}

static const struct file_operations host1x_de_all_fops = {
	.open = host1x_de_open_all,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int host1x_de_open(struct inode *inode, struct file *file)
{
	return single_open(file, host1x_de_show, inode->i_private);
}

static const struct file_operations host1x_de_fops = {
	.open = host1x_de_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static void host1x_defs_init(struct host1x *host1x)
{
	struct dentry *de = defs_create_dir("tegra-host1x", NULL);

	if (!de)
		return;

	/* Store the created entry */
	host1x->defs = de;

	defs_create_file("status", S_IRUGO, de, host1x, &host1x_de_fops);
	defs_create_file("status_all", S_IRUGO, de, host1x,
			    &host1x_de_all_fops);

	defs_create_u32("trace_cmdbuf", S_IRUGO|S_IWUSR, de,
			   &host1x_de_trace_cmdbuf);

	host1x_hw_de_init(host1x, de);

	defs_create_u32("force_timeout_pid", S_IRUGO|S_IWUSR, de,
			   &host1x_de_force_timeout_pid);
	defs_create_u32("force_timeout_val", S_IRUGO|S_IWUSR, de,
			   &host1x_de_force_timeout_val);
	defs_create_u32("force_timeout_channel", S_IRUGO|S_IWUSR, de,
			   &host1x_de_force_timeout_channel);
}

static void host1x_defs_exit(struct host1x *host1x)
{
	defs_remove_recursive(host1x->defs);
}

void host1x_de_init(struct host1x *host1x)
{
	if (IS_ENABLED(CONFIG_DE_FS))
		host1x_defs_init(host1x);
}

void host1x_de_deinit(struct host1x *host1x)
{
	if (IS_ENABLED(CONFIG_DE_FS))
		host1x_defs_exit(host1x);
}

void host1x_de_dump(struct host1x *host1x)
{
	struct output o = {
		.fn = write_to_printk
	};

	show_all(host1x, &o, true);
}

void host1x_de_dump_syncpts(struct host1x *host1x)
{
	struct output o = {
		.fn = write_to_printk
	};

	show_syncpts(host1x, &o);
}
