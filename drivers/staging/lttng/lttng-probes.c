/*
 * lttng-probes.c
 *
 * Holds LTTng probes registry.
 *
 * Copyright (C) 2010-2012 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; only
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <linux/module.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/seq_file.h>

#include "lttng-events.h"

static LIST_HEAD(probe_list);
static DEFINE_MUTEX(probe_mutex);

static
const struct lttng_event_desc *find_event(const char *name)
{
	struct lttng_probe_desc *probe_desc;
	int i;

	list_for_each_entry(probe_desc, &probe_list, head) {
		for (i = 0; i < probe_desc->nr_events; i++) {
			if (!strcmp(probe_desc->event_desc[i]->name, name))
				return probe_desc->event_desc[i];
		}
	}
	return NULL;
}

int lttng_probe_register(struct lttng_probe_desc *desc)
{
	int ret = 0;
	int i;

	mutex_lock(&probe_mutex);
	/*
	 * TODO: This is O(N^2). Turn into a hash table when probe registration
	 * overhead becomes an issue.
	 */
	for (i = 0; i < desc->nr_events; i++) {
		if (find_event(desc->event_desc[i]->name)) {
			ret = -EEXIST;
			goto end;
		}
	}
	list_add(&desc->head, &probe_list);
end:
	mutex_unlock(&probe_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(lttng_probe_register);

void lttng_probe_unregister(struct lttng_probe_desc *desc)
{
	mutex_lock(&probe_mutex);
	list_del(&desc->head);
	mutex_unlock(&probe_mutex);
}
EXPORT_SYMBOL_GPL(lttng_probe_unregister);

const struct lttng_event_desc *lttng_event_get(const char *name)
{
	const struct lttng_event_desc *event;
	int ret;

	mutex_lock(&probe_mutex);
	event = find_event(name);
	mutex_unlock(&probe_mutex);
	if (!event)
		return NULL;
	ret = try_module_get(event->owner);
	WARN_ON_ONCE(!ret);
	return event;
}
EXPORT_SYMBOL_GPL(lttng_event_get);

void lttng_event_put(const struct lttng_event_desc *event)
{
	module_put(event->owner);
}
EXPORT_SYMBOL_GPL(lttng_event_put);

static
void *tp_list_start(struct seq_file *m, loff_t *pos)
{
	struct lttng_probe_desc *probe_desc;
	int iter = 0, i;

	mutex_lock(&probe_mutex);
	list_for_each_entry(probe_desc, &probe_list, head) {
		for (i = 0; i < probe_desc->nr_events; i++) {
			if (iter++ >= *pos)
				return (void *) probe_desc->event_desc[i];
		}
	}
	/* End of list */
	return NULL;
}

static
void *tp_list_next(struct seq_file *m, void *p, loff_t *ppos)
{
	struct lttng_probe_desc *probe_desc;
	int iter = 0, i;

	(*ppos)++;
	list_for_each_entry(probe_desc, &probe_list, head) {
		for (i = 0; i < probe_desc->nr_events; i++) {
			if (iter++ >= *ppos)
				return (void *) probe_desc->event_desc[i];
		}
	}
	/* End of list */
	return NULL;
}

static
void tp_list_stop(struct seq_file *m, void *p)
{
	mutex_unlock(&probe_mutex);
}

static
int tp_list_show(struct seq_file *m, void *p)
{
	const struct lttng_event_desc *probe_desc = p;

	seq_printf(m,	"event { name = %s; };\n",
		   probe_desc->name);
	return 0;
}

static
const struct seq_operations lttng_tracepoint_list_seq_ops = {
	.start = tp_list_start,
	.next = tp_list_next,
	.stop = tp_list_stop,
	.show = tp_list_show,
};

static
int lttng_tracepoint_list_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &lttng_tracepoint_list_seq_ops);
}

const struct file_operations lttng_tracepoint_list_fops = {
	.owner = THIS_MODULE,
	.open = lttng_tracepoint_list_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};
