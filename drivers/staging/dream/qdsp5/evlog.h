/* arch/arm/mach-msm/qdsp5/evlog.h
 *
 * simple event log debugging facility
 *
 * Copyright (C) 2008 Google, Inc.
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

#include <linux/fs.h>
#include <linux/hrtimer.h>
#include <linux/debugfs.h>

#define EV_LOG_ENTRY_NAME(n) n##_entry

#define DECLARE_LOG(_name, _size, _str) \
static struct ev_entry EV_LOG_ENTRY_NAME(_name)[_size]; \
static struct ev_log _name = { \
	.name = #_name, \
	.strings = _str, \
	.num_strings = ARRAY_SIZE(_str), \
	.entry = EV_LOG_ENTRY_NAME(_name), \
	.max = ARRAY_SIZE(EV_LOG_ENTRY_NAME(_name)), \
}

struct ev_entry {
	ktime_t when;
	uint32_t id;
	uint32_t arg;
};

struct ev_log {
	struct ev_entry *entry;
	unsigned max;
	unsigned next;
	unsigned fault;
	const char **strings;
	unsigned num_strings;
	const char *name;
};

static char ev_buf[4096];

static ssize_t ev_log_read(struct file *file, char __user *buf,
			   size_t count, loff_t *ppos)
{
	struct ev_log *log = file->private_data;
	struct ev_entry *entry;
	unsigned long flags;
	int size = 0;
	unsigned n, id, max;
	ktime_t now, t;

	max = log->max;
	now = ktime_get();
	local_irq_save(flags);
	n = (log->next - 1) & (max - 1);
	entry = log->entry;
	while (n != log->next) {
		t = ktime_sub(now, entry[n].when);
		id = entry[n].id;
		if (id) {
			const char *str;
			if (id < log->num_strings)
				str = log->strings[id];
			else
				str = "UNKNOWN";
			size += scnprintf(ev_buf + size, 4096 - size,
					  "%8d.%03d %08x %s\n",
					  t.tv.sec, t.tv.nsec / 1000000,
					  entry[n].arg, str);
		}
		n = (n - 1) & (max - 1);
	}
	log->fault = 0;
	local_irq_restore(flags);
	return simple_read_from_buffer(buf, count, ppos, ev_buf, size);
}

static void ev_log_write(struct ev_log *log, unsigned id, unsigned arg)
{
	struct ev_entry *entry;
	unsigned long flags;
	local_irq_save(flags);

	if (log->fault) {
		if (log->fault == 1)
			goto done;
		log->fault--;
	}

	entry = log->entry + log->next;
	entry->when = ktime_get();
	entry->id = id;
	entry->arg = arg;
	log->next = (log->next + 1) & (log->max - 1);
done:
	local_irq_restore(flags);
}

static void ev_log_freeze(struct ev_log *log, unsigned count)
{
	unsigned long flags;
	local_irq_save(flags);
	log->fault = count;
	local_irq_restore(flags);
}

static int ev_log_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

static const struct file_operations ev_log_ops = {
	.read = ev_log_read,
	.open = ev_log_open,
};

static int ev_log_init(struct ev_log *log)
{
	debugfs_create_file(log->name, 0444, 0, log, &ev_log_ops);
	return 0;
}

