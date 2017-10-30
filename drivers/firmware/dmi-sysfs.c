/*
 * dmi-sysfs.c
 *
 * This module exports the DMI tables read-only to userspace through the
 * sysfs file system.
 *
 * Data is currently found below
 *    /sys/firmware/dmi/...
 *
 * DMI attributes are presented in attribute files with names
 * formatted using %d-%d, so that the first integer indicates the
 * structure type (0-255), and the second field is the instance of that
 * entry.
 *
 * Copyright 2011 Google, Inc.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kobject.h>
#include <linux/dmi.h>
#include <linux/capability.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/io.h>
#include <asm/dmi.h>

#define MAX_ENTRY_TYPE 255 /* Most of these aren't used, but we consider
			      the top entry type is only 8 bits */

struct dmi_sysfs_entry {
	struct dmi_header dh;
	struct kobject kobj;
	int instance;
	int position;
	struct list_head list;
	struct kobject *child;
};

/*
 * Global list of dmi_sysfs_entry.  Even though this should only be
 * manipulated at setup and teardown, the lazy nature of the kobject
 * system means we get lazy removes.
 */
static LIST_HEAD(entry_list);
static DEFINE_SPINLOCK(entry_list_lock);

/* dmi_sysfs_attribute - Top level attribute. used by all entries. */
struct dmi_sysfs_attribute {
	struct attribute attr;
	ssize_t (*show)(struct dmi_sysfs_entry *entry, char *buf);
};

#define DMI_SYSFS_ATTR(_entry, _name) \
struct dmi_sysfs_attribute dmi_sysfs_attr_##_entry##_##_name = { \
	.attr = {.name = __stringify(_name), .mode = 0400}, \
	.show = dmi_sysfs_##_entry##_##_name, \
}

/*
 * dmi_sysfs_mapped_attribute - Attribute where we require the entry be
 * mapped in.  Use in conjunction with dmi_sysfs_specialize_attr_ops.
 */
struct dmi_sysfs_mapped_attribute {
	struct attribute attr;
	ssize_t (*show)(struct dmi_sysfs_entry *entry,
			const struct dmi_header *dh,
			char *buf);
};

#define DMI_SYSFS_MAPPED_ATTR(_entry, _name) \
struct dmi_sysfs_mapped_attribute dmi_sysfs_attr_##_entry##_##_name = { \
	.attr = {.name = __stringify(_name), .mode = 0400}, \
	.show = dmi_sysfs_##_entry##_##_name, \
}

/*************************************************
 * Generic DMI entry support.
 *************************************************/
static void dmi_entry_free(struct kobject *kobj)
{
	kfree(kobj);
}

static struct dmi_sysfs_entry *to_entry(struct kobject *kobj)
{
	return container_of(kobj, struct dmi_sysfs_entry, kobj);
}

static struct dmi_sysfs_attribute *to_attr(struct attribute *attr)
{
	return container_of(attr, struct dmi_sysfs_attribute, attr);
}

static ssize_t dmi_sysfs_attr_show(struct kobject *kobj,
				   struct attribute *_attr, char *buf)
{
	struct dmi_sysfs_entry *entry = to_entry(kobj);
	struct dmi_sysfs_attribute *attr = to_attr(_attr);

	/* DMI stuff is only ever admin visible */
	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	return attr->show(entry, buf);
}

static const struct sysfs_ops dmi_sysfs_attr_ops = {
	.show = dmi_sysfs_attr_show,
};

typedef ssize_t (*dmi_callback)(struct dmi_sysfs_entry *,
				const struct dmi_header *dh, void *);

struct find_dmi_data {
	struct dmi_sysfs_entry	*entry;
	dmi_callback		callback;
	void			*private;
	int			instance_countdown;
	ssize_t			ret;
};

static void find_dmi_entry_helper(const struct dmi_header *dh,
				  void *_data)
{
	struct find_dmi_data *data = _data;
	struct dmi_sysfs_entry *entry = data->entry;

	/* Is this the entry we want? */
	if (dh->type != entry->dh.type)
		return;

	if (data->instance_countdown != 0) {
		/* try the next instance? */
		data->instance_countdown--;
		return;
	}

	/*
	 * Don't ever revisit the instance.  Short circuit later
	 * instances by letting the instance_countdown run negative
	 */
	data->instance_countdown--;

	/* Found the entry */
	data->ret = data->callback(entry, dh, data->private);
}

/* State for passing the read parameters through dmi_find_entry() */
struct dmi_read_state {
	char *buf;
	loff_t pos;
	size_t count;
};

static ssize_t find_dmi_entry(struct dmi_sysfs_entry *entry,
			      dmi_callback callback, void *private)
{
	struct find_dmi_data data = {
		.entry = entry,
		.callback = callback,
		.private = private,
		.instance_countdown = entry->instance,
		.ret = -EIO,  /* To signal the entry disappeared */
	};
	int ret;

	ret = dmi_walk(find_dmi_entry_helper, &data);
	/* This shouldn't happen, but just in case. */
	if (ret)
		return -EINVAL;
	return data.ret;
}

/*
 * Calculate and return the byte length of the dmi entry identified by
 * dh.  This includes both the formatted portion as well as the
 * unformatted string space, including the two trailing nul characters.
 */
static size_t dmi_entry_length(const struct dmi_header *dh)
{
	const char *p = (const char *)dh;

	p += dh->length;

	while (p[0] || p[1])
		p++;

	return 2 + p - (const char *)dh;
}

/*************************************************
 * Support bits for specialized DMI entry support
 *************************************************/
struct dmi_entry_attr_show_data {
	struct attribute *attr;
	char *buf;
};

static ssize_t dmi_entry_attr_show_helper(struct dmi_sysfs_entry *entry,
					  const struct dmi_header *dh,
					  void *_data)
{
	struct dmi_entry_attr_show_data *data = _data;
	struct dmi_sysfs_mapped_attribute *attr;

	attr = container_of(data->attr,
			    struct dmi_sysfs_mapped_attribute, attr);
	return attr->show(entry, dh, data->buf);
}

static ssize_t dmi_entry_attr_show(struct kobject *kobj,
				   struct attribute *attr,
				   char *buf)
{
	struct dmi_entry_attr_show_data data = {
		.attr = attr,
		.buf  = buf,
	};
	/* Find the entry according to our parent and call the
	 * normalized show method hanging off of the attribute */
	return find_dmi_entry(to_entry(kobj->parent),
			      dmi_entry_attr_show_helper, &data);
}

static const struct sysfs_ops dmi_sysfs_specialize_attr_ops = {
	.show = dmi_entry_attr_show,
};

/*************************************************
 * Specialized DMI entry support.
 *************************************************/

/*** Type 15 - System Event Table ***/

#define DMI_SEL_ACCESS_METHOD_IO8	0x00
#define DMI_SEL_ACCESS_METHOD_IO2x8	0x01
#define DMI_SEL_ACCESS_METHOD_IO16	0x02
#define DMI_SEL_ACCESS_METHOD_PHYS32	0x03
#define DMI_SEL_ACCESS_METHOD_GPNV	0x04

struct dmi_system_event_log {
	struct dmi_header header;
	u16	area_length;
	u16	header_start_offset;
	u16	data_start_offset;
	u8	access_method;
	u8	status;
	u32	change_token;
	union {
		struct {
			u16 index_addr;
			u16 data_addr;
		} io;
		u32	phys_addr32;
		u16	gpnv_handle;
		u32	access_method_address;
	};
	u8	header_format;
	u8	type_descriptors_supported_count;
	u8	per_log_type_descriptor_length;
	u8	supported_log_type_descriptos[0];
} __packed;

#define DMI_SYSFS_SEL_FIELD(_field) \
static ssize_t dmi_sysfs_sel_##_field(struct dmi_sysfs_entry *entry, \
				      const struct dmi_header *dh, \
				      char *buf) \
{ \
	struct dmi_system_event_log sel; \
	if (sizeof(sel) > dmi_entry_length(dh)) \
		return -EIO; \
	memcpy(&sel, dh, sizeof(sel)); \
	return sprintf(buf, "%u\n", sel._field); \
} \
static DMI_SYSFS_MAPPED_ATTR(sel, _field)

DMI_SYSFS_SEL_FIELD(area_length);
DMI_SYSFS_SEL_FIELD(header_start_offset);
DMI_SYSFS_SEL_FIELD(data_start_offset);
DMI_SYSFS_SEL_FIELD(access_method);
DMI_SYSFS_SEL_FIELD(status);
DMI_SYSFS_SEL_FIELD(change_token);
DMI_SYSFS_SEL_FIELD(access_method_address);
DMI_SYSFS_SEL_FIELD(header_format);
DMI_SYSFS_SEL_FIELD(type_descriptors_supported_count);
DMI_SYSFS_SEL_FIELD(per_log_type_descriptor_length);

static struct attribute *dmi_sysfs_sel_attrs[] = {
	&dmi_sysfs_attr_sel_area_length.attr,
	&dmi_sysfs_attr_sel_header_start_offset.attr,
	&dmi_sysfs_attr_sel_data_start_offset.attr,
	&dmi_sysfs_attr_sel_access_method.attr,
	&dmi_sysfs_attr_sel_status.attr,
	&dmi_sysfs_attr_sel_change_token.attr,
	&dmi_sysfs_attr_sel_access_method_address.attr,
	&dmi_sysfs_attr_sel_header_format.attr,
	&dmi_sysfs_attr_sel_type_descriptors_supported_count.attr,
	&dmi_sysfs_attr_sel_per_log_type_descriptor_length.attr,
	NULL,
};


static struct kobj_type dmi_system_event_log_ktype = {
	.release = dmi_entry_free,
	.sysfs_ops = &dmi_sysfs_specialize_attr_ops,
	.default_attrs = dmi_sysfs_sel_attrs,
};

typedef u8 (*sel_io_reader)(const struct dmi_system_event_log *sel,
			    loff_t offset);

static DEFINE_MUTEX(io_port_lock);

static u8 read_sel_8bit_indexed_io(const struct dmi_system_event_log *sel,
				   loff_t offset)
{
	u8 ret;

	mutex_lock(&io_port_lock);
	outb((u8)offset, sel->io.index_addr);
	ret = inb(sel->io.data_addr);
	mutex_unlock(&io_port_lock);
	return ret;
}

static u8 read_sel_2x8bit_indexed_io(const struct dmi_system_event_log *sel,
				     loff_t offset)
{
	u8 ret;

	mutex_lock(&io_port_lock);
	outb((u8)offset, sel->io.index_addr);
	outb((u8)(offset >> 8), sel->io.index_addr + 1);
	ret = inb(sel->io.data_addr);
	mutex_unlock(&io_port_lock);
	return ret;
}

static u8 read_sel_16bit_indexed_io(const struct dmi_system_event_log *sel,
				    loff_t offset)
{
	u8 ret;

	mutex_lock(&io_port_lock);
	outw((u16)offset, sel->io.index_addr);
	ret = inb(sel->io.data_addr);
	mutex_unlock(&io_port_lock);
	return ret;
}

static sel_io_reader sel_io_readers[] = {
	[DMI_SEL_ACCESS_METHOD_IO8]	= read_sel_8bit_indexed_io,
	[DMI_SEL_ACCESS_METHOD_IO2x8]	= read_sel_2x8bit_indexed_io,
	[DMI_SEL_ACCESS_METHOD_IO16]	= read_sel_16bit_indexed_io,
};

static ssize_t dmi_sel_raw_read_io(struct dmi_sysfs_entry *entry,
				   const struct dmi_system_event_log *sel,
				   char *buf, loff_t pos, size_t count)
{
	ssize_t wrote = 0;

	sel_io_reader io_reader = sel_io_readers[sel->access_method];

	while (count && pos < sel->area_length) {
		count--;
		*(buf++) = io_reader(sel, pos++);
		wrote++;
	}

	return wrote;
}

static ssize_t dmi_sel_raw_read_phys32(struct dmi_sysfs_entry *entry,
				       const struct dmi_system_event_log *sel,
				       char *buf, loff_t pos, size_t count)
{
	u8 __iomem *mapped;
	ssize_t wrote = 0;

	mapped = dmi_remap(sel->access_method_address, sel->area_length);
	if (!mapped)
		return -EIO;

	while (count && pos < sel->area_length) {
		count--;
		*(buf++) = readb(mapped + pos++);
		wrote++;
	}

	dmi_unmap(mapped);
	return wrote;
}

static ssize_t dmi_sel_raw_read_helper(struct dmi_sysfs_entry *entry,
				       const struct dmi_header *dh,
				       void *_state)
{
	struct dmi_read_state *state = _state;
	struct dmi_system_event_log sel;

	if (sizeof(sel) > dmi_entry_length(dh))
		return -EIO;

	memcpy(&sel, dh, sizeof(sel));

	switch (sel.access_method) {
	case DMI_SEL_ACCESS_METHOD_IO8:
	case DMI_SEL_ACCESS_METHOD_IO2x8:
	case DMI_SEL_ACCESS_METHOD_IO16:
		return dmi_sel_raw_read_io(entry, &sel, state->buf,
					   state->pos, state->count);
	case DMI_SEL_ACCESS_METHOD_PHYS32:
		return dmi_sel_raw_read_phys32(entry, &sel, state->buf,
					       state->pos, state->count);
	case DMI_SEL_ACCESS_METHOD_GPNV:
		pr_info("dmi-sysfs: GPNV support missing.\n");
		return -EIO;
	default:
		pr_info("dmi-sysfs: Unknown access method %02x\n",
			sel.access_method);
		return -EIO;
	}
}

static ssize_t dmi_sel_raw_read(struct file *filp, struct kobject *kobj,
				struct bin_attribute *bin_attr,
				char *buf, loff_t pos, size_t count)
{
	struct dmi_sysfs_entry *entry = to_entry(kobj->parent);
	struct dmi_read_state state = {
		.buf = buf,
		.pos = pos,
		.count = count,
	};

	return find_dmi_entry(entry, dmi_sel_raw_read_helper, &state);
}

static struct bin_attribute dmi_sel_raw_attr = {
	.attr = {.name = "raw_event_log", .mode = 0400},
	.read = dmi_sel_raw_read,
};

static int dmi_system_event_log(struct dmi_sysfs_entry *entry)
{
	int ret;

	entry->child = kzalloc(sizeof(*entry->child), GFP_KERNEL);
	if (!entry->child)
		return -ENOMEM;
	ret = kobject_init_and_add(entry->child,
				   &dmi_system_event_log_ktype,
				   &entry->kobj,
				   "system_event_log");
	if (ret)
		goto out_free;

	ret = sysfs_create_bin_file(entry->child, &dmi_sel_raw_attr);
	if (ret)
		goto out_del;

	return 0;

out_del:
	kobject_del(entry->child);
out_free:
	kfree(entry->child);
	return ret;
}

/*************************************************
 * Generic DMI entry support.
 *************************************************/

static ssize_t dmi_sysfs_entry_length(struct dmi_sysfs_entry *entry, char *buf)
{
	return sprintf(buf, "%d\n", entry->dh.length);
}

static ssize_t dmi_sysfs_entry_handle(struct dmi_sysfs_entry *entry, char *buf)
{
	return sprintf(buf, "%d\n", entry->dh.handle);
}

static ssize_t dmi_sysfs_entry_type(struct dmi_sysfs_entry *entry, char *buf)
{
	return sprintf(buf, "%d\n", entry->dh.type);
}

static ssize_t dmi_sysfs_entry_instance(struct dmi_sysfs_entry *entry,
					char *buf)
{
	return sprintf(buf, "%d\n", entry->instance);
}

static ssize_t dmi_sysfs_entry_position(struct dmi_sysfs_entry *entry,
					char *buf)
{
	return sprintf(buf, "%d\n", entry->position);
}

static DMI_SYSFS_ATTR(entry, length);
static DMI_SYSFS_ATTR(entry, handle);
static DMI_SYSFS_ATTR(entry, type);
static DMI_SYSFS_ATTR(entry, instance);
static DMI_SYSFS_ATTR(entry, position);

static struct attribute *dmi_sysfs_entry_attrs[] = {
	&dmi_sysfs_attr_entry_length.attr,
	&dmi_sysfs_attr_entry_handle.attr,
	&dmi_sysfs_attr_entry_type.attr,
	&dmi_sysfs_attr_entry_instance.attr,
	&dmi_sysfs_attr_entry_position.attr,
	NULL,
};

static ssize_t dmi_entry_raw_read_helper(struct dmi_sysfs_entry *entry,
					 const struct dmi_header *dh,
					 void *_state)
{
	struct dmi_read_state *state = _state;
	size_t entry_length;

	entry_length = dmi_entry_length(dh);

	return memory_read_from_buffer(state->buf, state->count,
				       &state->pos, dh, entry_length);
}

static ssize_t dmi_entry_raw_read(struct file *filp,
				  struct kobject *kobj,
				  struct bin_attribute *bin_attr,
				  char *buf, loff_t pos, size_t count)
{
	struct dmi_sysfs_entry *entry = to_entry(kobj);
	struct dmi_read_state state = {
		.buf = buf,
		.pos = pos,
		.count = count,
	};

	return find_dmi_entry(entry, dmi_entry_raw_read_helper, &state);
}

static const struct bin_attribute dmi_entry_raw_attr = {
	.attr = {.name = "raw", .mode = 0400},
	.read = dmi_entry_raw_read,
};

static void dmi_sysfs_entry_release(struct kobject *kobj)
{
	struct dmi_sysfs_entry *entry = to_entry(kobj);

	spin_lock(&entry_list_lock);
	list_del(&entry->list);
	spin_unlock(&entry_list_lock);
	kfree(entry);
}

static struct kobj_type dmi_sysfs_entry_ktype = {
	.release = dmi_sysfs_entry_release,
	.sysfs_ops = &dmi_sysfs_attr_ops,
	.default_attrs = dmi_sysfs_entry_attrs,
};

static struct kset *dmi_kset;

/* Global count of all instances seen.  Only for setup */
static int __initdata instance_counts[MAX_ENTRY_TYPE + 1];

/* Global positional count of all entries seen.  Only for setup */
static int __initdata position_count;

static void __init dmi_sysfs_register_handle(const struct dmi_header *dh,
					     void *_ret)
{
	struct dmi_sysfs_entry *entry;
	int *ret = _ret;

	/* If a previous entry saw an error, short circuit */
	if (*ret)
		return;

	/* Allocate and register a new entry into the entries set */
	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry) {
		*ret = -ENOMEM;
		return;
	}

	/* Set the key */
	memcpy(&entry->dh, dh, sizeof(*dh));
	entry->instance = instance_counts[dh->type]++;
	entry->position = position_count++;

	entry->kobj.kset = dmi_kset;
	*ret = kobject_init_and_add(&entry->kobj, &dmi_sysfs_entry_ktype, NULL,
				    "%d-%d", dh->type, entry->instance);

	if (*ret) {
		kfree(entry);
		return;
	}

	/* Thread on the global list for cleanup */
	spin_lock(&entry_list_lock);
	list_add_tail(&entry->list, &entry_list);
	spin_unlock(&entry_list_lock);

	/* Handle specializations by type */
	switch (dh->type) {
	case DMI_ENTRY_SYSTEM_EVENT_LOG:
		*ret = dmi_system_event_log(entry);
		break;
	default:
		/* No specialization */
		break;
	}
	if (*ret)
		goto out_err;

	/* Create the raw binary file to access the entry */
	*ret = sysfs_create_bin_file(&entry->kobj, &dmi_entry_raw_attr);
	if (*ret)
		goto out_err;

	return;
out_err:
	kobject_put(entry->child);
	kobject_put(&entry->kobj);
	return;
}

static void cleanup_entry_list(void)
{
	struct dmi_sysfs_entry *entry, *next;

	/* No locks, we are on our way out */
	list_for_each_entry_safe(entry, next, &entry_list, list) {
		kobject_put(entry->child);
		kobject_put(&entry->kobj);
	}
}

static int __init dmi_sysfs_init(void)
{
	int error;
	int val;

	if (!dmi_kobj) {
		pr_err("dmi-sysfs: dmi entry is absent.\n");
		error = -ENODATA;
		goto err;
	}

	dmi_kset = kset_create_and_add("entries", NULL, dmi_kobj);
	if (!dmi_kset) {
		error = -ENOMEM;
		goto err;
	}

	val = 0;
	error = dmi_walk(dmi_sysfs_register_handle, &val);
	if (error)
		goto err;
	if (val) {
		error = val;
		goto err;
	}

	pr_debug("dmi-sysfs: loaded.\n");

	return 0;
err:
	cleanup_entry_list();
	kset_unregister(dmi_kset);
	return error;
}

/* clean up everything. */
static void __exit dmi_sysfs_exit(void)
{
	pr_debug("dmi-sysfs: unloading.\n");
	cleanup_entry_list();
	kset_unregister(dmi_kset);
}

module_init(dmi_sysfs_init);
module_exit(dmi_sysfs_exit);

MODULE_AUTHOR("Mike Waychison <mikew@google.com>");
MODULE_DESCRIPTION("DMI sysfs support");
MODULE_LICENSE("GPL");
