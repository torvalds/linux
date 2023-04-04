// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Google LLC
 */

#include <linux/tracefs.h>
#include <linux/trace_events.h>

#include <asm/kvm_host.h>
#include <asm/kvm_hypevents_defs.h>
#include <asm/setup.h>

#include "hyp_trace.h"

#define HYP_EVENT_NAME_MAX 32

struct hyp_event {
	struct trace_event_call *call;
	char name[HYP_EVENT_NAME_MAX];
	bool *enabled;
};

#define HYP_EVENT(__name, __proto, __struct, __assign, __printk)		\
	HYP_EVENT_FORMAT(__name, __struct);					\
	enum print_line_t hyp_event_trace_##__name(struct trace_iterator *iter,	\
					  int flags, struct trace_event *event) \
	{									\
		struct ht_iterator *ht_iter = (struct ht_iterator *)iter;	\
		struct trace_hyp_format_##__name __maybe_unused *__entry =	\
			(struct trace_hyp_format_##__name *)ht_iter->ent;	\
		trace_seq_puts(&ht_iter->seq, #__name);				\
		trace_seq_putc(&ht_iter->seq, ' ');				\
		trace_seq_printf(&ht_iter->seq, __printk);			\
		trace_seq_putc(&ht_iter->seq, '\n');				\
		return TRACE_TYPE_HANDLED;					\
	}
#include <asm/kvm_hypevents.h>

#undef he_field
#define he_field(_type, _item)						\
	{								\
		.type = #_type, .name = #_item,				\
		.size = sizeof(_type), .align = __alignof__(_type),	\
		.is_signed = is_signed_type(_type),			\
	},
#undef HYP_EVENT
#define HYP_EVENT(__name, __proto, __struct, __assign, __printk)		\
	static struct trace_event_fields hyp_event_fields_##__name[] = {	\
		__struct							\
		{}								\
	};									\

#undef __ARM64_KVM_HYPEVENTS_H_
#include <asm/kvm_hypevents.h>

#undef HYP_EVENT
#undef HE_PRINTK
#define __entry REC
#define HE_PRINTK(fmt, args...) "\"" fmt "\", " __stringify(args)
#define HYP_EVENT(__name, __proto, __struct, __assign, __printk)		\
	static char hyp_event_print_fmt_##__name[] = __printk;			\
	static struct trace_event_functions hyp_event_funcs_##__name = {	\
		.trace = &hyp_event_trace_##__name,				\
	};									\
	static struct trace_event_class hyp_event_class_##__name = {		\
		.system		= "nvhe-hypervisor",				\
		.fields_array	= hyp_event_fields_##__name,			\
		.fields		= LIST_HEAD_INIT(hyp_event_class_##__name.fields),\
	};									\
	static struct trace_event_call hyp_event_call_##__name = {		\
		.class = &hyp_event_class_##__name,				\
		.event.funcs = &hyp_event_funcs_##__name,			\
		.print_fmt = hyp_event_print_fmt_##__name,			\
	};									\
	static bool hyp_event_enabled_##__name;					\
	struct hyp_event __section("_hyp_events") hyp_event_##__name = {	\
		.name = #__name,						\
		.call = &hyp_event_call_##__name,				\
		.enabled = &hyp_event_enabled_##__name,				\
	}

#undef __ARM64_KVM_HYPEVENTS_H_
#include <asm/kvm_hypevents.h>

extern struct hyp_event __hyp_events_start[];
extern struct hyp_event __hyp_events_end[];

/* hyp_event section used by the hypervisor */
extern struct hyp_event_id __hyp_event_ids_start[];
extern struct hyp_event_id __hyp_event_ids_end[];

static struct hyp_event *find_hyp_event(const char *name)
{
	struct hyp_event *event = __hyp_events_start;

	for (; (unsigned long)event < (unsigned long)__hyp_events_end;
		event++) {
		if (!strncmp(name, event->name, HYP_EVENT_NAME_MAX))
			return event;
	}

	return NULL;
}

static int enable_hyp_event(struct hyp_event *event, bool enable)
{
	unsigned short id = event->call->event.type;
	int ret;

	if (enable == *event->enabled)
		return 0;

	ret = kvm_call_hyp_nvhe(__pkvm_enable_event, id, enable);
	if (ret)
		return ret;

	*event->enabled = enable;

	return 0;
}

static ssize_t
hyp_event_write(struct file *filp, const char __user *ubuf, size_t cnt, loff_t *ppos)
{
	struct seq_file *seq_file = (struct seq_file *)filp->private_data;
	struct hyp_event *evt = (struct hyp_event *)seq_file->private;
	bool enabling;
	int ret;
	char c;

	if (!cnt || cnt > 2)
		return -EINVAL;

	if (get_user(c, ubuf))
		return -EFAULT;

	switch (c) {
	case '1':
		enabling = true;
		break;
	case '0':
		enabling = false;
		break;
	default:
		return -EINVAL;
	}

	ret = enable_hyp_event(evt, enabling);
	if (ret)
		return ret;

	return cnt;
}

static int hyp_event_show(struct seq_file *m, void *v)
{
	struct hyp_event *evt = (struct hyp_event *)m->private;

	/* lock ?? Ain't no time for that ! */
	seq_printf(m, "%d\n", *evt->enabled);

	return 0;
}

static int hyp_event_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, hyp_event_show, inode->i_private);
}

static const struct file_operations hyp_event_fops = {
	.open		= hyp_event_open,
	.write		= hyp_event_write,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int hyp_event_id_show(struct seq_file *m, void *v)
{
	struct hyp_event *evt = (struct hyp_event *)m->private;

	seq_printf(m, "%d\n", evt->call->event.type);

	return 0;
}

static int hyp_event_id_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, hyp_event_id_show, inode->i_private);
}

static const struct file_operations hyp_event_id_fops = {
	.open = hyp_event_id_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int hyp_event_format_show(struct seq_file *m, void *v)
{
	struct hyp_event *evt = (struct hyp_event *)m->private;
	struct trace_event_fields *field;
	unsigned int offset = sizeof(struct hyp_entry_hdr);

	seq_printf(m, "name: %s\n", evt->name);
	seq_printf(m, "ID: %d\n", evt->call->event.type);
	seq_puts(m, "format:\n\tfield:unsigned short common_type;\toffset:0;\tsize:2;\tsigned:0;\n");
	seq_puts(m, "\n");

	field = &evt->call->class->fields_array[0];
	while (field->name) {
		seq_printf(m, "\tfield:%s %s;\toffset:%u;\tsize:%u;\tsigned:%d;\n",
			  field->type, field->name, offset, field->size,
			  !!field->is_signed);
		offset += field->size;
		field++;
	}

	if (field != &evt->call->class->fields_array[0])
		seq_puts(m, "\n");

	seq_printf(m, "print fmt: %s\n", evt->call->print_fmt);

	return 0;
}

static int hyp_event_format_open(struct inode *inode, struct file *file)
{
	return single_open(file, hyp_event_format_show, inode->i_private);
}

static const struct file_operations hyp_event_format_fops = {
	.open = hyp_event_format_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int hyp_header_page_show(struct seq_file *m, void *v)
{
	struct buffer_data_page bpage;

	seq_printf(m, "\tfield: u64 timestamp;\t"
		   "offset:0;\tsize:%lu;\tsigned:%d;\n",
		   sizeof(bpage.time_stamp),
		   is_signed_type(u64));

	seq_printf(m, "\tfield: local_t commit;\t"
		   "offset:%lu;\tsize:%lu;\tsigned:%d;\n",
		   offsetof(typeof(bpage), commit),
		   sizeof(bpage.commit),
		   is_signed_type(long));

	seq_printf(m, "\tfield: char data;\t"
		   "offset:%lu;\tsize:%lu;\tsigned:%d;\n",
		   offsetof(typeof(bpage), data),
		   BUF_EXT_PAGE_SIZE,
		   is_signed_type(char));

	return 0;
}

static int hyp_header_page_open(struct inode *inode, struct file *file)
{
	return single_open(file, hyp_header_page_show, NULL);
}

static const struct file_operations hyp_header_page_fops = {
	.open = hyp_header_page_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static char early_events[COMMAND_LINE_SIZE];

static __init int setup_hyp_event_early(char *str)
{
	strscpy(early_events, str, COMMAND_LINE_SIZE);

	return 1;
}
__setup("hyp_event=", setup_hyp_event_early);

bool kvm_hyp_events_enable_early(void)
{
	char *token, *buf = early_events;
	bool enabled = false;

	while (true) {
		token = strsep(&buf, ",");

		if (!token)
			break;

		if (*token) {
			struct hyp_event *event;
			int ret;

			event = find_hyp_event(token);
			if (event) {
				ret = enable_hyp_event(event, true);
				if (ret)
					pr_warn("Couldn't enable hyp event %s:%d\n",
						token, ret);
				else
					enabled = true;
			} else {
				pr_warn("Couldn't find hyp event %s\n", token);
			}
		}

		if (buf)
			*(buf - 1) = ',';
	}

	return enabled;
}

void kvm_hyp_init_events_tracefs(struct dentry *parent)
{
	struct hyp_event *event = __hyp_events_start;
	struct dentry *d, *event_dir;

	parent = tracefs_create_dir("events", parent);
	if (!parent) {
		pr_err("Failed to create tracefs folder for hyp events\n");
		return;
	}

	d = tracefs_create_file("header_page", 0400, parent, NULL,
				&hyp_header_page_fops);
	if (!d)
		pr_err("Failed to create events/header_page\n");

	parent = tracefs_create_dir("hyp", parent);
	if (!parent) {
		pr_err("Failed to create tracefs folder for hyp events\n");
		return;
	}

	for (; (unsigned long)event < (unsigned long)__hyp_events_end; event++) {
		event_dir = tracefs_create_dir(event->name, parent);
		if (!event_dir) {
			pr_err("Failed to create events/hyp/%s\n", event->name);
			continue;
		}
		d = tracefs_create_file("enable", 0700, event_dir, (void *)event,
				&hyp_event_fops);
		if (!d)
			pr_err("Failed to create events/hyp/%s/enable\n", event->name);

		d = tracefs_create_file("id", 0400, event_dir, (void *)event,
				&hyp_event_id_fops);
		if (!d)
			pr_err("Failed to create events/hyp/%s/id\n", event->name);

		d = tracefs_create_file("format", 0400, event_dir, (void *)event,
					&hyp_event_format_fops);
		if (!d)
			pr_err("Failed to create events/hyp/%s/format\n",
			       event->name);

	}
}

/*
 * Register hyp events and write their id into the hyp section _hyp_event_ids.
 */
int kvm_hyp_init_events(void)
{
	struct hyp_event *event = __hyp_events_start;
	struct hyp_event_id *hyp_event_id = __hyp_event_ids_start;
	int ret, err = -ENODEV;

	/* TODO: BUILD_BUG nr events host side / hyp side */

	for (; (unsigned long)event < (unsigned long)__hyp_events_end;
		event++, hyp_event_id++) {
		event->call->name = event->name;
		ret = register_trace_event(&event->call->event);
		if (!ret) {
			pr_warn("Couldn't register trace event for %s\n", event->name);
			continue;
		}

		/*
		 * Both the host and the hypervisor relies on the same hyp event
		 * declarations from kvm_hypevents.h. We have then a 1:1
		 * mapping.
		 */
		hyp_event_id->id = ret;

		err = 0;
	}

	return err;
}
