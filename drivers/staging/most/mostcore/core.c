/*
 * core.c - Implementation of core module of MOST Linux driver stack
 *
 * Copyright (C) 2013-2015 Microchip Technology Germany II GmbH & Co. KG
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * This file is licensed under GPLv2.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/kobject.h>
#include <linux/mutex.h>
#include <linux/completion.h>
#include <linux/sysfs.h>
#include <linux/kthread.h>
#include <linux/dma-mapping.h>
#include <linux/idr.h>
#include "mostcore.h"

#define MAX_CHANNELS	64
#define STRING_SIZE	80

static struct class *most_class;
static struct device *core_dev;
static struct ida mdev_id;
static int dummy_num_buffers;

struct most_c_aim_obj {
	struct most_aim *ptr;
	int refs;
	int num_buffers;
};

struct most_c_obj {
	struct kobject kobj;
	struct completion cleanup;
	atomic_t mbo_ref;
	atomic_t mbo_nq_level;
	u16 channel_id;
	bool is_poisoned;
	struct mutex start_mutex;
	struct mutex nq_mutex; /* nq thread synchronization */
	int is_starving;
	struct most_interface *iface;
	struct most_inst_obj *inst;
	struct most_channel_config cfg;
	bool keep_mbo;
	bool enqueue_halt;
	struct list_head fifo;
	spinlock_t fifo_lock;
	struct list_head halt_fifo;
	struct list_head list;
	struct most_c_aim_obj aim0;
	struct most_c_aim_obj aim1;
	struct list_head trash_fifo;
	struct task_struct *hdm_enqueue_task;
	wait_queue_head_t hdm_fifo_wq;
};

#define to_c_obj(d) container_of(d, struct most_c_obj, kobj)

struct most_inst_obj {
	int dev_id;
	struct most_interface *iface;
	struct list_head channel_list;
	struct most_c_obj *channel[MAX_CHANNELS];
	struct kobject kobj;
	struct list_head list;
};

static const struct {
	int most_ch_data_type;
	char *name;
} ch_data_type[] = {
	{ MOST_CH_CONTROL, "control\n" },
	{ MOST_CH_ASYNC, "async\n" },
	{ MOST_CH_SYNC, "sync\n" },
	{ MOST_CH_ISOC, "isoc\n"},
	{ MOST_CH_ISOC, "isoc_avp\n"},
};

#define to_inst_obj(d) container_of(d, struct most_inst_obj, kobj)

/**
 * list_pop_mbo - retrieves the first MBO of the list and removes it
 * @ptr: the list head to grab the MBO from.
 */
#define list_pop_mbo(ptr)						\
({									\
	struct mbo *_mbo = list_first_entry(ptr, struct mbo, list);	\
	list_del(&_mbo->list);						\
	_mbo;								\
})

/*		     ___	     ___
 *		     ___C H A N N E L___
 */

/**
 * struct most_c_attr - to access the attributes of a channel object
 * @attr: attributes of a channel
 * @show: pointer to the show function
 * @store: pointer to the store function
 */
struct most_c_attr {
	struct attribute attr;
	ssize_t (*show)(struct most_c_obj *d,
			struct most_c_attr *attr,
			char *buf);
	ssize_t (*store)(struct most_c_obj *d,
			 struct most_c_attr *attr,
			 const char *buf,
			 size_t count);
};

#define to_channel_attr(a) container_of(a, struct most_c_attr, attr)

#define MOST_CHNL_ATTR(_name, _mode, _show, _store) \
		struct most_c_attr most_chnl_attr_##_name = \
		__ATTR(_name, _mode, _show, _store)

/**
 * channel_attr_show - show function of channel object
 * @kobj: pointer to its kobject
 * @attr: pointer to its attributes
 * @buf: buffer
 */
static ssize_t channel_attr_show(struct kobject *kobj, struct attribute *attr,
				 char *buf)
{
	struct most_c_attr *channel_attr = to_channel_attr(attr);
	struct most_c_obj *c_obj = to_c_obj(kobj);

	if (!channel_attr->show)
		return -EIO;

	return channel_attr->show(c_obj, channel_attr, buf);
}

/**
 * channel_attr_store - store function of channel object
 * @kobj: pointer to its kobject
 * @attr: pointer to its attributes
 * @buf: buffer
 * @len: length of buffer
 */
static ssize_t channel_attr_store(struct kobject *kobj,
				  struct attribute *attr,
				  const char *buf,
				  size_t len)
{
	struct most_c_attr *channel_attr = to_channel_attr(attr);
	struct most_c_obj *c_obj = to_c_obj(kobj);

	if (!channel_attr->store)
		return -EIO;
	return channel_attr->store(c_obj, channel_attr, buf, len);
}

static const struct sysfs_ops most_channel_sysfs_ops = {
	.show = channel_attr_show,
	.store = channel_attr_store,
};

/**
 * most_free_mbo_coherent - free an MBO and its coherent buffer
 * @mbo: buffer to be released
 *
 */
static void most_free_mbo_coherent(struct mbo *mbo)
{
	struct most_c_obj *c = mbo->context;
	u16 const coherent_buf_size = c->cfg.buffer_size + c->cfg.extra_len;

	dma_free_coherent(NULL, coherent_buf_size, mbo->virt_address,
			  mbo->bus_address);
	kfree(mbo);
	if (atomic_sub_and_test(1, &c->mbo_ref))
		complete(&c->cleanup);
}

/**
 * flush_channel_fifos - clear the channel fifos
 * @c: pointer to channel object
 */
static void flush_channel_fifos(struct most_c_obj *c)
{
	unsigned long flags, hf_flags;
	struct mbo *mbo, *tmp;

	if (list_empty(&c->fifo) && list_empty(&c->halt_fifo))
		return;

	spin_lock_irqsave(&c->fifo_lock, flags);
	list_for_each_entry_safe(mbo, tmp, &c->fifo, list) {
		list_del(&mbo->list);
		spin_unlock_irqrestore(&c->fifo_lock, flags);
		most_free_mbo_coherent(mbo);
		spin_lock_irqsave(&c->fifo_lock, flags);
	}
	spin_unlock_irqrestore(&c->fifo_lock, flags);

	spin_lock_irqsave(&c->fifo_lock, hf_flags);
	list_for_each_entry_safe(mbo, tmp, &c->halt_fifo, list) {
		list_del(&mbo->list);
		spin_unlock_irqrestore(&c->fifo_lock, hf_flags);
		most_free_mbo_coherent(mbo);
		spin_lock_irqsave(&c->fifo_lock, hf_flags);
	}
	spin_unlock_irqrestore(&c->fifo_lock, hf_flags);

	if (unlikely((!list_empty(&c->fifo) || !list_empty(&c->halt_fifo))))
		pr_info("WARN: fifo | trash fifo not empty\n");
}

/**
 * flush_trash_fifo - clear the trash fifo
 * @c: pointer to channel object
 */
static int flush_trash_fifo(struct most_c_obj *c)
{
	struct mbo *mbo, *tmp;
	unsigned long flags;

	spin_lock_irqsave(&c->fifo_lock, flags);
	list_for_each_entry_safe(mbo, tmp, &c->trash_fifo, list) {
		list_del(&mbo->list);
		spin_unlock_irqrestore(&c->fifo_lock, flags);
		most_free_mbo_coherent(mbo);
		spin_lock_irqsave(&c->fifo_lock, flags);
	}
	spin_unlock_irqrestore(&c->fifo_lock, flags);
	return 0;
}

/**
 * most_channel_release - release function of channel object
 * @kobj: pointer to channel's kobject
 */
static void most_channel_release(struct kobject *kobj)
{
	struct most_c_obj *c = to_c_obj(kobj);

	kfree(c);
}

static ssize_t show_available_directions(struct most_c_obj *c,
					 struct most_c_attr *attr,
					 char *buf)
{
	unsigned int i = c->channel_id;

	strcpy(buf, "");
	if (c->iface->channel_vector[i].direction & MOST_CH_RX)
		strcat(buf, "rx ");
	if (c->iface->channel_vector[i].direction & MOST_CH_TX)
		strcat(buf, "tx ");
	strcat(buf, "\n");
	return strlen(buf);
}

static ssize_t show_available_datatypes(struct most_c_obj *c,
					struct most_c_attr *attr,
					char *buf)
{
	unsigned int i = c->channel_id;

	strcpy(buf, "");
	if (c->iface->channel_vector[i].data_type & MOST_CH_CONTROL)
		strcat(buf, "control ");
	if (c->iface->channel_vector[i].data_type & MOST_CH_ASYNC)
		strcat(buf, "async ");
	if (c->iface->channel_vector[i].data_type & MOST_CH_SYNC)
		strcat(buf, "sync ");
	if (c->iface->channel_vector[i].data_type & MOST_CH_ISOC)
		strcat(buf, "isoc ");
	strcat(buf, "\n");
	return strlen(buf);
}

static
ssize_t show_number_of_packet_buffers(struct most_c_obj *c,
				      struct most_c_attr *attr,
				      char *buf)
{
	unsigned int i = c->channel_id;

	return snprintf(buf, PAGE_SIZE, "%d\n",
			c->iface->channel_vector[i].num_buffers_packet);
}

static
ssize_t show_number_of_stream_buffers(struct most_c_obj *c,
				      struct most_c_attr *attr,
				      char *buf)
{
	unsigned int i = c->channel_id;

	return snprintf(buf, PAGE_SIZE, "%d\n",
			c->iface->channel_vector[i].num_buffers_streaming);
}

static
ssize_t show_size_of_packet_buffer(struct most_c_obj *c,
				   struct most_c_attr *attr,
				   char *buf)
{
	unsigned int i = c->channel_id;

	return snprintf(buf, PAGE_SIZE, "%d\n",
			c->iface->channel_vector[i].buffer_size_packet);
}

static
ssize_t show_size_of_stream_buffer(struct most_c_obj *c,
				   struct most_c_attr *attr,
				   char *buf)
{
	unsigned int i = c->channel_id;

	return snprintf(buf, PAGE_SIZE, "%d\n",
			c->iface->channel_vector[i].buffer_size_streaming);
}

static ssize_t show_channel_starving(struct most_c_obj *c,
				     struct most_c_attr *attr,
				     char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", c->is_starving);
}

#define create_show_channel_attribute(val) \
	static MOST_CHNL_ATTR(val, 0444, show_##val, NULL)

create_show_channel_attribute(available_directions);
create_show_channel_attribute(available_datatypes);
create_show_channel_attribute(number_of_packet_buffers);
create_show_channel_attribute(number_of_stream_buffers);
create_show_channel_attribute(size_of_stream_buffer);
create_show_channel_attribute(size_of_packet_buffer);
create_show_channel_attribute(channel_starving);

static ssize_t show_set_number_of_buffers(struct most_c_obj *c,
					  struct most_c_attr *attr,
					  char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", c->cfg.num_buffers);
}

static ssize_t store_set_number_of_buffers(struct most_c_obj *c,
					   struct most_c_attr *attr,
					   const char *buf,
					   size_t count)
{
	int ret = kstrtou16(buf, 0, &c->cfg.num_buffers);

	if (ret)
		return ret;
	return count;
}

static ssize_t show_set_buffer_size(struct most_c_obj *c,
				    struct most_c_attr *attr,
				    char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", c->cfg.buffer_size);
}

static ssize_t store_set_buffer_size(struct most_c_obj *c,
				     struct most_c_attr *attr,
				     const char *buf,
				     size_t count)
{
	int ret = kstrtou16(buf, 0, &c->cfg.buffer_size);

	if (ret)
		return ret;
	return count;
}

static ssize_t show_set_direction(struct most_c_obj *c,
				  struct most_c_attr *attr,
				  char *buf)
{
	if (c->cfg.direction & MOST_CH_TX)
		return snprintf(buf, PAGE_SIZE, "tx\n");
	else if (c->cfg.direction & MOST_CH_RX)
		return snprintf(buf, PAGE_SIZE, "rx\n");
	return snprintf(buf, PAGE_SIZE, "unconfigured\n");
}

static ssize_t store_set_direction(struct most_c_obj *c,
				   struct most_c_attr *attr,
				   const char *buf,
				   size_t count)
{
	if (!strcmp(buf, "dir_rx\n")) {
		c->cfg.direction = MOST_CH_RX;
	} else if (!strcmp(buf, "rx\n")) {
		c->cfg.direction = MOST_CH_RX;
	} else if (!strcmp(buf, "dir_tx\n")) {
		c->cfg.direction = MOST_CH_TX;
	} else if (!strcmp(buf, "tx\n")) {
		c->cfg.direction = MOST_CH_TX;
	} else {
		pr_info("WARN: invalid attribute settings\n");
		return -EINVAL;
	}
	return count;
}

static ssize_t show_set_datatype(struct most_c_obj *c,
				 struct most_c_attr *attr,
				 char *buf)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ch_data_type); i++) {
		if (c->cfg.data_type & ch_data_type[i].most_ch_data_type)
			return snprintf(buf, PAGE_SIZE, ch_data_type[i].name);
	}
	return snprintf(buf, PAGE_SIZE, "unconfigured\n");
}

static ssize_t store_set_datatype(struct most_c_obj *c,
				  struct most_c_attr *attr,
				  const char *buf,
				  size_t count)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ch_data_type); i++) {
		if (!strcmp(buf, ch_data_type[i].name)) {
			c->cfg.data_type = ch_data_type[i].most_ch_data_type;
			break;
		}
	}

	if (i == ARRAY_SIZE(ch_data_type)) {
		pr_info("WARN: invalid attribute settings\n");
		return -EINVAL;
	}
	return count;
}

static ssize_t show_set_subbuffer_size(struct most_c_obj *c,
				       struct most_c_attr *attr,
				       char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", c->cfg.subbuffer_size);
}

static ssize_t store_set_subbuffer_size(struct most_c_obj *c,
					struct most_c_attr *attr,
					const char *buf,
					size_t count)
{
	int ret = kstrtou16(buf, 0, &c->cfg.subbuffer_size);

	if (ret)
		return ret;
	return count;
}

static ssize_t show_set_packets_per_xact(struct most_c_obj *c,
					 struct most_c_attr *attr,
					 char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", c->cfg.packets_per_xact);
}

static ssize_t store_set_packets_per_xact(struct most_c_obj *c,
					  struct most_c_attr *attr,
					  const char *buf,
					  size_t count)
{
	int ret = kstrtou16(buf, 0, &c->cfg.packets_per_xact);

	if (ret)
		return ret;
	return count;
}

#define create_channel_attribute(value) \
	static MOST_CHNL_ATTR(value, 0644, show_##value, store_##value)

create_channel_attribute(set_buffer_size);
create_channel_attribute(set_number_of_buffers);
create_channel_attribute(set_direction);
create_channel_attribute(set_datatype);
create_channel_attribute(set_subbuffer_size);
create_channel_attribute(set_packets_per_xact);

/**
 * most_channel_def_attrs - array of default attributes of channel object
 */
static struct attribute *most_channel_def_attrs[] = {
	&most_chnl_attr_available_directions.attr,
	&most_chnl_attr_available_datatypes.attr,
	&most_chnl_attr_number_of_packet_buffers.attr,
	&most_chnl_attr_number_of_stream_buffers.attr,
	&most_chnl_attr_size_of_packet_buffer.attr,
	&most_chnl_attr_size_of_stream_buffer.attr,
	&most_chnl_attr_set_number_of_buffers.attr,
	&most_chnl_attr_set_buffer_size.attr,
	&most_chnl_attr_set_direction.attr,
	&most_chnl_attr_set_datatype.attr,
	&most_chnl_attr_set_subbuffer_size.attr,
	&most_chnl_attr_set_packets_per_xact.attr,
	&most_chnl_attr_channel_starving.attr,
	NULL,
};

static struct kobj_type most_channel_ktype = {
	.sysfs_ops = &most_channel_sysfs_ops,
	.release = most_channel_release,
	.default_attrs = most_channel_def_attrs,
};

static struct kset *most_channel_kset;

/**
 * create_most_c_obj - allocates a channel object
 * @name: name of the channel object
 * @parent: parent kobject
 *
 * This create a channel object and registers it with sysfs.
 * Returns a pointer to the object or NULL when something went wrong.
 */
static struct most_c_obj *
create_most_c_obj(const char *name, struct kobject *parent)
{
	struct most_c_obj *c;
	int retval;

	c = kzalloc(sizeof(*c), GFP_KERNEL);
	if (!c)
		return NULL;
	c->kobj.kset = most_channel_kset;
	retval = kobject_init_and_add(&c->kobj, &most_channel_ktype, parent,
				      "%s", name);
	if (retval) {
		kobject_put(&c->kobj);
		return NULL;
	}
	kobject_uevent(&c->kobj, KOBJ_ADD);
	return c;
}

/*		     ___	       ___
 *		     ___I N S T A N C E___
 */
#define MOST_INST_ATTR(_name, _mode, _show, _store) \
		struct most_inst_attribute most_inst_attr_##_name = \
		__ATTR(_name, _mode, _show, _store)

static struct list_head instance_list;

/**
 * struct most_inst_attribute - to access the attributes of instance object
 * @attr: attributes of an instance
 * @show: pointer to the show function
 * @store: pointer to the store function
 */
struct most_inst_attribute {
	struct attribute attr;
	ssize_t (*show)(struct most_inst_obj *d,
			struct most_inst_attribute *attr,
			char *buf);
	ssize_t (*store)(struct most_inst_obj *d,
			 struct most_inst_attribute *attr,
			 const char *buf,
			 size_t count);
};

#define to_instance_attr(a) \
	container_of(a, struct most_inst_attribute, attr)

/**
 * instance_attr_show - show function for an instance object
 * @kobj: pointer to kobject
 * @attr: pointer to attribute struct
 * @buf: buffer
 */
static ssize_t instance_attr_show(struct kobject *kobj,
				  struct attribute *attr,
				  char *buf)
{
	struct most_inst_attribute *instance_attr;
	struct most_inst_obj *instance_obj;

	instance_attr = to_instance_attr(attr);
	instance_obj = to_inst_obj(kobj);

	if (!instance_attr->show)
		return -EIO;

	return instance_attr->show(instance_obj, instance_attr, buf);
}

/**
 * instance_attr_store - store function for an instance object
 * @kobj: pointer to kobject
 * @attr: pointer to attribute struct
 * @buf: buffer
 * @len: length of buffer
 */
static ssize_t instance_attr_store(struct kobject *kobj,
				   struct attribute *attr,
				   const char *buf,
				   size_t len)
{
	struct most_inst_attribute *instance_attr;
	struct most_inst_obj *instance_obj;

	instance_attr = to_instance_attr(attr);
	instance_obj = to_inst_obj(kobj);

	if (!instance_attr->store)
		return -EIO;

	return instance_attr->store(instance_obj, instance_attr, buf, len);
}

static const struct sysfs_ops most_inst_sysfs_ops = {
	.show = instance_attr_show,
	.store = instance_attr_store,
};

/**
 * most_inst_release - release function for instance object
 * @kobj: pointer to instance's kobject
 *
 * This frees the allocated memory for the instance object
 */
static void most_inst_release(struct kobject *kobj)
{
	struct most_inst_obj *inst = to_inst_obj(kobj);

	kfree(inst);
}

static ssize_t show_description(struct most_inst_obj *instance_obj,
				struct most_inst_attribute *attr,
				char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s\n",
			instance_obj->iface->description);
}

static ssize_t show_interface(struct most_inst_obj *instance_obj,
			      struct most_inst_attribute *attr,
			      char *buf)
{
	switch (instance_obj->iface->interface) {
	case ITYPE_LOOPBACK:
		return snprintf(buf, PAGE_SIZE, "loopback\n");
	case ITYPE_I2C:
		return snprintf(buf, PAGE_SIZE, "i2c\n");
	case ITYPE_I2S:
		return snprintf(buf, PAGE_SIZE, "i2s\n");
	case ITYPE_TSI:
		return snprintf(buf, PAGE_SIZE, "tsi\n");
	case ITYPE_HBI:
		return snprintf(buf, PAGE_SIZE, "hbi\n");
	case ITYPE_MEDIALB_DIM:
		return snprintf(buf, PAGE_SIZE, "mlb_dim\n");
	case ITYPE_MEDIALB_DIM2:
		return snprintf(buf, PAGE_SIZE, "mlb_dim2\n");
	case ITYPE_USB:
		return snprintf(buf, PAGE_SIZE, "usb\n");
	case ITYPE_PCIE:
		return snprintf(buf, PAGE_SIZE, "pcie\n");
	}
	return snprintf(buf, PAGE_SIZE, "unknown\n");
}

#define create_inst_attribute(value) \
	static MOST_INST_ATTR(value, 0444, show_##value, NULL)

create_inst_attribute(description);
create_inst_attribute(interface);

static struct attribute *most_inst_def_attrs[] = {
	&most_inst_attr_description.attr,
	&most_inst_attr_interface.attr,
	NULL,
};

static struct kobj_type most_inst_ktype = {
	.sysfs_ops = &most_inst_sysfs_ops,
	.release = most_inst_release,
	.default_attrs = most_inst_def_attrs,
};

static struct kset *most_inst_kset;

/**
 * create_most_inst_obj - creates an instance object
 * @name: name of the object to be created
 *
 * This allocates memory for an instance structure, assigns the proper kset
 * and registers it with sysfs.
 *
 * Returns a pointer to the instance object or NULL when something went wrong.
 */
static struct most_inst_obj *create_most_inst_obj(const char *name)
{
	struct most_inst_obj *inst;
	int retval;

	inst = kzalloc(sizeof(*inst), GFP_KERNEL);
	if (!inst)
		return NULL;
	inst->kobj.kset = most_inst_kset;
	retval = kobject_init_and_add(&inst->kobj, &most_inst_ktype, NULL,
				      "%s", name);
	if (retval) {
		kobject_put(&inst->kobj);
		return NULL;
	}
	kobject_uevent(&inst->kobj, KOBJ_ADD);
	return inst;
}

/**
 * destroy_most_inst_obj - MOST instance release function
 * @inst: pointer to the instance object
 *
 * This decrements the reference counter of the instance object.
 * If the reference count turns zero, its release function is called
 */
static void destroy_most_inst_obj(struct most_inst_obj *inst)
{
	struct most_c_obj *c, *tmp;

	list_for_each_entry_safe(c, tmp, &inst->channel_list, list) {
		flush_trash_fifo(c);
		flush_channel_fifos(c);
		kobject_put(&c->kobj);
	}
	kobject_put(&inst->kobj);
}

/*		     ___     ___
 *		     ___A I M___
 */
struct most_aim_obj {
	struct kobject kobj;
	struct list_head list;
	struct most_aim *driver;
};

#define to_aim_obj(d) container_of(d, struct most_aim_obj, kobj)

static struct list_head aim_list;

/**
 * struct most_aim_attribute - to access the attributes of AIM object
 * @attr: attributes of an AIM
 * @show: pointer to the show function
 * @store: pointer to the store function
 */
struct most_aim_attribute {
	struct attribute attr;
	ssize_t (*show)(struct most_aim_obj *d,
			struct most_aim_attribute *attr,
			char *buf);
	ssize_t (*store)(struct most_aim_obj *d,
			 struct most_aim_attribute *attr,
			 const char *buf,
			 size_t count);
};

#define to_aim_attr(a) container_of(a, struct most_aim_attribute, attr)

/**
 * aim_attr_show - show function of an AIM object
 * @kobj: pointer to kobject
 * @attr: pointer to attribute struct
 * @buf: buffer
 */
static ssize_t aim_attr_show(struct kobject *kobj,
			     struct attribute *attr,
			     char *buf)
{
	struct most_aim_attribute *aim_attr;
	struct most_aim_obj *aim_obj;

	aim_attr = to_aim_attr(attr);
	aim_obj = to_aim_obj(kobj);

	if (!aim_attr->show)
		return -EIO;

	return aim_attr->show(aim_obj, aim_attr, buf);
}

/**
 * aim_attr_store - store function of an AIM object
 * @kobj: pointer to kobject
 * @attr: pointer to attribute struct
 * @buf: buffer
 * @len: length of buffer
 */
static ssize_t aim_attr_store(struct kobject *kobj,
			      struct attribute *attr,
			      const char *buf,
			      size_t len)
{
	struct most_aim_attribute *aim_attr;
	struct most_aim_obj *aim_obj;

	aim_attr = to_aim_attr(attr);
	aim_obj = to_aim_obj(kobj);

	if (!aim_attr->store)
		return -EIO;
	return aim_attr->store(aim_obj, aim_attr, buf, len);
}

static const struct sysfs_ops most_aim_sysfs_ops = {
	.show = aim_attr_show,
	.store = aim_attr_store,
};

/**
 * most_aim_release - AIM release function
 * @kobj: pointer to AIM's kobject
 */
static void most_aim_release(struct kobject *kobj)
{
	struct most_aim_obj *aim_obj = to_aim_obj(kobj);

	kfree(aim_obj);
}

static ssize_t add_link_show(struct most_aim_obj *aim_obj,
			     struct most_aim_attribute *attr,
			     char *buf)
{
	struct most_c_obj *c;
	struct most_inst_obj *i;
	int offs = 0;

	list_for_each_entry(i, &instance_list, list) {
		list_for_each_entry(c, &i->channel_list, list) {
			if (c->aim0.ptr == aim_obj->driver ||
			    c->aim1.ptr == aim_obj->driver) {
				offs += snprintf(buf + offs, PAGE_SIZE - offs,
						 "%s:%s\n",
						 kobject_name(&i->kobj),
						 kobject_name(&c->kobj));
			}
		}
	}

	return offs;
}

/**
 * split_string - parses and changes string in the buffer buf and
 * splits it into two mandatory and one optional substrings.
 *
 * @buf: complete string from attribute 'add_channel'
 * @a: address of pointer to 1st substring (=instance name)
 * @b: address of pointer to 2nd substring (=channel name)
 * @c: optional address of pointer to 3rd substring (=user defined name)
 *
 * Examples:
 *
 * Input: "mdev0:ch6:my_channel\n" or
 *        "mdev0:ch6:my_channel"
 *
 * Output: *a -> "mdev0", *b -> "ch6", *c -> "my_channel"
 *
 * Input: "mdev1:ep81\n"
 * Output: *a -> "mdev1", *b -> "ep81", *c -> ""
 *
 * Input: "mdev1:ep81"
 * Output: *a -> "mdev1", *b -> "ep81", *c == NULL
 */
static int split_string(char *buf, char **a, char **b, char **c)
{
	*a = strsep(&buf, ":");
	if (!*a)
		return -EIO;

	*b = strsep(&buf, ":\n");
	if (!*b)
		return -EIO;

	if (c)
		*c = strsep(&buf, ":\n");

	return 0;
}

/**
 * get_channel_by_name - get pointer to channel object
 * @mdev: name of the device instance
 * @mdev_ch: name of the respective channel
 *
 * This retrieves the pointer to a channel object.
 */
static struct
most_c_obj *get_channel_by_name(char *mdev, char *mdev_ch)
{
	struct most_c_obj *c, *tmp;
	struct most_inst_obj *i, *i_tmp;
	int found = 0;

	list_for_each_entry_safe(i, i_tmp, &instance_list, list) {
		if (!strcmp(kobject_name(&i->kobj), mdev)) {
			found++;
			break;
		}
	}
	if (unlikely(!found))
		return ERR_PTR(-EIO);

	list_for_each_entry_safe(c, tmp, &i->channel_list, list) {
		if (!strcmp(kobject_name(&c->kobj), mdev_ch)) {
			found++;
			break;
		}
	}
	if (unlikely(found < 2))
		return ERR_PTR(-EIO);
	return c;
}

/**
 * store_add_link - store() function for add_link attribute
 * @aim_obj: pointer to AIM object
 * @attr: its attributes
 * @buf: buffer
 * @len: buffer length
 *
 * This parses the string given by buf and splits it into
 * three substrings. Note: third substring is optional. In case a cdev
 * AIM is loaded the optional 3rd substring will make up the name of
 * device node in the /dev directory. If omitted, the device node will
 * inherit the channel's name within sysfs.
 *
 * Searches for a pair of device and channel and probes the AIM
 *
 * Example:
 * (1) echo "mdev0:ch6:my_rxchannel" >add_link
 * (2) echo "mdev1:ep81" >add_link
 *
 * (1) would create the device node /dev/my_rxchannel
 * (2) would create the device node /dev/mdev1-ep81
 */
static ssize_t add_link_store(struct most_aim_obj *aim_obj,
			      struct most_aim_attribute *attr,
			      const char *buf,
			      size_t len)
{
	struct most_c_obj *c;
	struct most_aim **aim_ptr;
	char buffer[STRING_SIZE];
	char *mdev;
	char *mdev_ch;
	char *mdev_devnod;
	char devnod_buf[STRING_SIZE];
	int ret;
	size_t max_len = min_t(size_t, len + 1, STRING_SIZE);

	strlcpy(buffer, buf, max_len);

	ret = split_string(buffer, &mdev, &mdev_ch, &mdev_devnod);
	if (ret)
		return ret;

	if (!mdev_devnod || *mdev_devnod == 0) {
		snprintf(devnod_buf, sizeof(devnod_buf), "%s-%s", mdev,
			 mdev_ch);
		mdev_devnod = devnod_buf;
	}

	c = get_channel_by_name(mdev, mdev_ch);
	if (IS_ERR(c))
		return -ENODEV;

	if (!c->aim0.ptr)
		aim_ptr = &c->aim0.ptr;
	else if (!c->aim1.ptr)
		aim_ptr = &c->aim1.ptr;
	else
		return -ENOSPC;

	*aim_ptr = aim_obj->driver;
	ret = aim_obj->driver->probe_channel(c->iface, c->channel_id,
					     &c->cfg, &c->kobj, mdev_devnod);
	if (ret) {
		*aim_ptr = NULL;
		return ret;
	}

	return len;
}

static struct most_aim_attribute most_aim_attr_add_link =
	__ATTR_RW(add_link);

/**
 * store_remove_link - store function for remove_link attribute
 * @aim_obj: pointer to AIM object
 * @attr: its attributes
 * @buf: buffer
 * @len: buffer length
 *
 * Example:
 * echo "mdev0:ep81" >remove_link
 */
static ssize_t remove_link_store(struct most_aim_obj *aim_obj,
				 struct most_aim_attribute *attr,
				 const char *buf,
				 size_t len)
{
	struct most_c_obj *c;
	char buffer[STRING_SIZE];
	char *mdev;
	char *mdev_ch;
	int ret;
	size_t max_len = min_t(size_t, len + 1, STRING_SIZE);

	strlcpy(buffer, buf, max_len);
	ret = split_string(buffer, &mdev, &mdev_ch, NULL);
	if (ret)
		return ret;

	c = get_channel_by_name(mdev, mdev_ch);
	if (IS_ERR(c))
		return -ENODEV;

	if (aim_obj->driver->disconnect_channel(c->iface, c->channel_id))
		return -EIO;
	if (c->aim0.ptr == aim_obj->driver)
		c->aim0.ptr = NULL;
	if (c->aim1.ptr == aim_obj->driver)
		c->aim1.ptr = NULL;
	return len;
}

static struct most_aim_attribute most_aim_attr_remove_link =
	__ATTR_WO(remove_link);

static struct attribute *most_aim_def_attrs[] = {
	&most_aim_attr_add_link.attr,
	&most_aim_attr_remove_link.attr,
	NULL,
};

static struct kobj_type most_aim_ktype = {
	.sysfs_ops = &most_aim_sysfs_ops,
	.release = most_aim_release,
	.default_attrs = most_aim_def_attrs,
};

static struct kset *most_aim_kset;

/**
 * create_most_aim_obj - creates an AIM object
 * @name: name of the AIM
 *
 * This creates an AIM object assigns the proper kset and registers
 * it with sysfs.
 * Returns a pointer to the object or NULL if something went wrong.
 */
static struct most_aim_obj *create_most_aim_obj(const char *name)
{
	struct most_aim_obj *most_aim;
	int retval;

	most_aim = kzalloc(sizeof(*most_aim), GFP_KERNEL);
	if (!most_aim)
		return NULL;
	most_aim->kobj.kset = most_aim_kset;
	retval = kobject_init_and_add(&most_aim->kobj, &most_aim_ktype,
				      NULL, "%s", name);
	if (retval) {
		kobject_put(&most_aim->kobj);
		return NULL;
	}
	kobject_uevent(&most_aim->kobj, KOBJ_ADD);
	return most_aim;
}

/**
 * destroy_most_aim_obj - AIM release function
 * @p: pointer to AIM object
 *
 * This decrements the reference counter of the AIM object. If the
 * reference count turns zero, its release function will be called.
 */
static void destroy_most_aim_obj(struct most_aim_obj *p)
{
	kobject_put(&p->kobj);
}

/*		     ___       ___
 *		     ___C O R E___
 */

/**
 * Instantiation of the MOST bus
 */
static struct bus_type most_bus = {
	.name = "most",
};

/**
 * Instantiation of the core driver
 */
static struct device_driver mostcore = {
	.name = "mostcore",
	.bus = &most_bus,
};

static inline void trash_mbo(struct mbo *mbo)
{
	unsigned long flags;
	struct most_c_obj *c = mbo->context;

	spin_lock_irqsave(&c->fifo_lock, flags);
	list_add(&mbo->list, &c->trash_fifo);
	spin_unlock_irqrestore(&c->fifo_lock, flags);
}

static bool hdm_mbo_ready(struct most_c_obj *c)
{
	bool empty;

	if (c->enqueue_halt)
		return false;

	spin_lock_irq(&c->fifo_lock);
	empty = list_empty(&c->halt_fifo);
	spin_unlock_irq(&c->fifo_lock);

	return !empty;
}

static void nq_hdm_mbo(struct mbo *mbo)
{
	unsigned long flags;
	struct most_c_obj *c = mbo->context;

	spin_lock_irqsave(&c->fifo_lock, flags);
	list_add_tail(&mbo->list, &c->halt_fifo);
	spin_unlock_irqrestore(&c->fifo_lock, flags);
	wake_up_interruptible(&c->hdm_fifo_wq);
}

static int hdm_enqueue_thread(void *data)
{
	struct most_c_obj *c = data;
	struct mbo *mbo;
	int ret;
	typeof(c->iface->enqueue) enqueue = c->iface->enqueue;

	while (likely(!kthread_should_stop())) {
		wait_event_interruptible(c->hdm_fifo_wq,
					 hdm_mbo_ready(c) ||
					 kthread_should_stop());

		mutex_lock(&c->nq_mutex);
		spin_lock_irq(&c->fifo_lock);
		if (unlikely(c->enqueue_halt || list_empty(&c->halt_fifo))) {
			spin_unlock_irq(&c->fifo_lock);
			mutex_unlock(&c->nq_mutex);
			continue;
		}

		mbo = list_pop_mbo(&c->halt_fifo);
		spin_unlock_irq(&c->fifo_lock);

		if (c->cfg.direction == MOST_CH_RX)
			mbo->buffer_length = c->cfg.buffer_size;

		ret = enqueue(mbo->ifp, mbo->hdm_channel_id, mbo);
		mutex_unlock(&c->nq_mutex);

		if (unlikely(ret)) {
			pr_err("hdm enqueue failed\n");
			nq_hdm_mbo(mbo);
			c->hdm_enqueue_task = NULL;
			return 0;
		}
	}

	return 0;
}

static int run_enqueue_thread(struct most_c_obj *c, int channel_id)
{
	struct task_struct *task =
		kthread_run(hdm_enqueue_thread, c, "hdm_fifo_%d",
			    channel_id);

	if (IS_ERR(task))
		return PTR_ERR(task);

	c->hdm_enqueue_task = task;
	return 0;
}

/**
 * arm_mbo - recycle MBO for further usage
 * @mbo: buffer object
 *
 * This puts an MBO back to the list to have it ready for up coming
 * tx transactions.
 *
 * In case the MBO belongs to a channel that recently has been
 * poisoned, the MBO is scheduled to be trashed.
 * Calls the completion handler of an attached AIM.
 */
static void arm_mbo(struct mbo *mbo)
{
	unsigned long flags;
	struct most_c_obj *c;

	BUG_ON((!mbo) || (!mbo->context));
	c = mbo->context;

	if (c->is_poisoned) {
		trash_mbo(mbo);
		return;
	}

	spin_lock_irqsave(&c->fifo_lock, flags);
	++*mbo->num_buffers_ptr;
	list_add_tail(&mbo->list, &c->fifo);
	spin_unlock_irqrestore(&c->fifo_lock, flags);

	if (c->aim0.refs && c->aim0.ptr->tx_completion)
		c->aim0.ptr->tx_completion(c->iface, c->channel_id);

	if (c->aim1.refs && c->aim1.ptr->tx_completion)
		c->aim1.ptr->tx_completion(c->iface, c->channel_id);
}

/**
 * arm_mbo_chain - helper function that arms an MBO chain for the HDM
 * @c: pointer to interface channel
 * @dir: direction of the channel
 * @compl: pointer to completion function
 *
 * This allocates buffer objects including the containing DMA coherent
 * buffer and puts them in the fifo.
 * Buffers of Rx channels are put in the kthread fifo, hence immediately
 * submitted to the HDM.
 *
 * Returns the number of allocated and enqueued MBOs.
 */
static int arm_mbo_chain(struct most_c_obj *c, int dir,
			 void (*compl)(struct mbo *))
{
	unsigned int i;
	int retval;
	struct mbo *mbo;
	u32 coherent_buf_size = c->cfg.buffer_size + c->cfg.extra_len;

	atomic_set(&c->mbo_nq_level, 0);

	for (i = 0; i < c->cfg.num_buffers; i++) {
		mbo = kzalloc(sizeof(*mbo), GFP_KERNEL);
		if (!mbo) {
			retval = i;
			goto _exit;
		}
		mbo->context = c;
		mbo->ifp = c->iface;
		mbo->hdm_channel_id = c->channel_id;
		mbo->virt_address = dma_alloc_coherent(NULL,
						       coherent_buf_size,
						       &mbo->bus_address,
						       GFP_KERNEL);
		if (!mbo->virt_address) {
			pr_info("WARN: No DMA coherent buffer.\n");
			retval = i;
			goto _error1;
		}
		mbo->complete = compl;
		mbo->num_buffers_ptr = &dummy_num_buffers;
		if (dir == MOST_CH_RX) {
			nq_hdm_mbo(mbo);
			atomic_inc(&c->mbo_nq_level);
		} else {
			arm_mbo(mbo);
		}
	}
	return i;

_error1:
	kfree(mbo);
_exit:
	return retval;
}

/**
 * most_submit_mbo - submits an MBO to fifo
 * @mbo: pointer to the MBO
 */
void most_submit_mbo(struct mbo *mbo)
{
	if (WARN_ONCE(!mbo || !mbo->context,
		      "bad mbo or missing channel reference\n"))
		return;

	nq_hdm_mbo(mbo);
}
EXPORT_SYMBOL_GPL(most_submit_mbo);

/**
 * most_write_completion - write completion handler
 * @mbo: pointer to MBO
 *
 * This recycles the MBO for further usage. In case the channel has been
 * poisoned, the MBO is scheduled to be trashed.
 */
static void most_write_completion(struct mbo *mbo)
{
	struct most_c_obj *c;

	BUG_ON((!mbo) || (!mbo->context));

	c = mbo->context;
	if (mbo->status == MBO_E_INVAL)
		pr_info("WARN: Tx MBO status: invalid\n");
	if (unlikely(c->is_poisoned || (mbo->status == MBO_E_CLOSE)))
		trash_mbo(mbo);
	else
		arm_mbo(mbo);
}

/**
 * get_channel_by_iface - get pointer to channel object
 * @iface: pointer to interface instance
 * @id: channel ID
 *
 * This retrieves a pointer to a channel of the given interface and channel ID.
 */
static struct
most_c_obj *get_channel_by_iface(struct most_interface *iface, int id)
{
	struct most_inst_obj *i;

	if (unlikely(!iface)) {
		pr_err("Bad interface\n");
		return NULL;
	}
	if (unlikely((id < 0) || (id >= iface->num_channels))) {
		pr_err("Channel index (%d) out of range\n", id);
		return NULL;
	}
	i = iface->priv;
	if (unlikely(!i)) {
		pr_err("interface is not registered\n");
		return NULL;
	}
	return i->channel[id];
}

int channel_has_mbo(struct most_interface *iface, int id, struct most_aim *aim)
{
	struct most_c_obj *c = get_channel_by_iface(iface, id);
	unsigned long flags;
	int empty;

	if (unlikely(!c))
		return -EINVAL;

	if (c->aim0.refs && c->aim1.refs &&
	    ((aim == c->aim0.ptr && c->aim0.num_buffers <= 0) ||
	     (aim == c->aim1.ptr && c->aim1.num_buffers <= 0)))
		return 0;

	spin_lock_irqsave(&c->fifo_lock, flags);
	empty = list_empty(&c->fifo);
	spin_unlock_irqrestore(&c->fifo_lock, flags);
	return !empty;
}
EXPORT_SYMBOL_GPL(channel_has_mbo);

/**
 * most_get_mbo - get pointer to an MBO of pool
 * @iface: pointer to interface instance
 * @id: channel ID
 *
 * This attempts to get a free buffer out of the channel fifo.
 * Returns a pointer to MBO on success or NULL otherwise.
 */
struct mbo *most_get_mbo(struct most_interface *iface, int id,
			 struct most_aim *aim)
{
	struct mbo *mbo;
	struct most_c_obj *c;
	unsigned long flags;
	int *num_buffers_ptr;

	c = get_channel_by_iface(iface, id);
	if (unlikely(!c))
		return NULL;

	if (c->aim0.refs && c->aim1.refs &&
	    ((aim == c->aim0.ptr && c->aim0.num_buffers <= 0) ||
	     (aim == c->aim1.ptr && c->aim1.num_buffers <= 0)))
		return NULL;

	if (aim == c->aim0.ptr)
		num_buffers_ptr = &c->aim0.num_buffers;
	else if (aim == c->aim1.ptr)
		num_buffers_ptr = &c->aim1.num_buffers;
	else
		num_buffers_ptr = &dummy_num_buffers;

	spin_lock_irqsave(&c->fifo_lock, flags);
	if (list_empty(&c->fifo)) {
		spin_unlock_irqrestore(&c->fifo_lock, flags);
		return NULL;
	}
	mbo = list_pop_mbo(&c->fifo);
	--*num_buffers_ptr;
	spin_unlock_irqrestore(&c->fifo_lock, flags);

	mbo->num_buffers_ptr = num_buffers_ptr;
	mbo->buffer_length = c->cfg.buffer_size;
	return mbo;
}
EXPORT_SYMBOL_GPL(most_get_mbo);

/**
 * most_put_mbo - return buffer to pool
 * @mbo: buffer object
 */
void most_put_mbo(struct mbo *mbo)
{
	struct most_c_obj *c = mbo->context;

	if (c->cfg.direction == MOST_CH_TX) {
		arm_mbo(mbo);
		return;
	}
	nq_hdm_mbo(mbo);
	atomic_inc(&c->mbo_nq_level);
}
EXPORT_SYMBOL_GPL(most_put_mbo);

/**
 * most_read_completion - read completion handler
 * @mbo: pointer to MBO
 *
 * This function is called by the HDM when data has been received from the
 * hardware and copied to the buffer of the MBO.
 *
 * In case the channel has been poisoned it puts the buffer in the trash queue.
 * Otherwise, it passes the buffer to an AIM for further processing.
 */
static void most_read_completion(struct mbo *mbo)
{
	struct most_c_obj *c = mbo->context;

	if (unlikely(c->is_poisoned || (mbo->status == MBO_E_CLOSE))) {
		trash_mbo(mbo);
		return;
	}

	if (mbo->status == MBO_E_INVAL) {
		nq_hdm_mbo(mbo);
		atomic_inc(&c->mbo_nq_level);
		return;
	}

	if (atomic_sub_and_test(1, &c->mbo_nq_level))
		c->is_starving = 1;

	if (c->aim0.refs && c->aim0.ptr->rx_completion &&
	    c->aim0.ptr->rx_completion(mbo) == 0)
		return;

	if (c->aim1.refs && c->aim1.ptr->rx_completion &&
	    c->aim1.ptr->rx_completion(mbo) == 0)
		return;

	most_put_mbo(mbo);
}

/**
 * most_start_channel - prepares a channel for communication
 * @iface: pointer to interface instance
 * @id: channel ID
 *
 * This prepares the channel for usage. Cross-checks whether the
 * channel's been properly configured.
 *
 * Returns 0 on success or error code otherwise.
 */
int most_start_channel(struct most_interface *iface, int id,
		       struct most_aim *aim)
{
	int num_buffer;
	int ret;
	struct most_c_obj *c = get_channel_by_iface(iface, id);

	if (unlikely(!c))
		return -EINVAL;

	mutex_lock(&c->start_mutex);
	if (c->aim0.refs + c->aim1.refs > 0)
		goto out; /* already started by other aim */

	if (!try_module_get(iface->mod)) {
		pr_info("failed to acquire HDM lock\n");
		mutex_unlock(&c->start_mutex);
		return -ENOLCK;
	}

	c->cfg.extra_len = 0;
	if (c->iface->configure(c->iface, c->channel_id, &c->cfg)) {
		pr_info("channel configuration failed. Go check settings...\n");
		ret = -EINVAL;
		goto error;
	}

	init_waitqueue_head(&c->hdm_fifo_wq);

	if (c->cfg.direction == MOST_CH_RX)
		num_buffer = arm_mbo_chain(c, c->cfg.direction,
					   most_read_completion);
	else
		num_buffer = arm_mbo_chain(c, c->cfg.direction,
					   most_write_completion);
	if (unlikely(!num_buffer)) {
		pr_info("failed to allocate memory\n");
		ret = -ENOMEM;
		goto error;
	}

	ret = run_enqueue_thread(c, id);
	if (ret)
		goto error;

	c->is_starving = 0;
	c->aim0.num_buffers = c->cfg.num_buffers / 2;
	c->aim1.num_buffers = c->cfg.num_buffers - c->aim0.num_buffers;
	atomic_set(&c->mbo_ref, num_buffer);

out:
	if (aim == c->aim0.ptr)
		c->aim0.refs++;
	if (aim == c->aim1.ptr)
		c->aim1.refs++;
	mutex_unlock(&c->start_mutex);
	return 0;

error:
	module_put(iface->mod);
	mutex_unlock(&c->start_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(most_start_channel);

/**
 * most_stop_channel - stops a running channel
 * @iface: pointer to interface instance
 * @id: channel ID
 */
int most_stop_channel(struct most_interface *iface, int id,
		      struct most_aim *aim)
{
	struct most_c_obj *c;

	if (unlikely((!iface) || (id >= iface->num_channels) || (id < 0))) {
		pr_err("Bad interface or index out of range\n");
		return -EINVAL;
	}
	c = get_channel_by_iface(iface, id);
	if (unlikely(!c))
		return -EINVAL;

	mutex_lock(&c->start_mutex);
	if (c->aim0.refs + c->aim1.refs >= 2)
		goto out;

	if (c->hdm_enqueue_task)
		kthread_stop(c->hdm_enqueue_task);
	c->hdm_enqueue_task = NULL;

	if (iface->mod)
		module_put(iface->mod);

	c->is_poisoned = true;
	if (c->iface->poison_channel(c->iface, c->channel_id)) {
		pr_err("Cannot stop channel %d of mdev %s\n", c->channel_id,
		       c->iface->description);
		mutex_unlock(&c->start_mutex);
		return -EAGAIN;
	}
	flush_trash_fifo(c);
	flush_channel_fifos(c);

#ifdef CMPL_INTERRUPTIBLE
	if (wait_for_completion_interruptible(&c->cleanup)) {
		pr_info("Interrupted while clean up ch %d\n", c->channel_id);
		mutex_unlock(&c->start_mutex);
		return -EINTR;
	}
#else
	wait_for_completion(&c->cleanup);
#endif
	c->is_poisoned = false;

out:
	if (aim == c->aim0.ptr)
		c->aim0.refs--;
	if (aim == c->aim1.ptr)
		c->aim1.refs--;
	mutex_unlock(&c->start_mutex);
	return 0;
}
EXPORT_SYMBOL_GPL(most_stop_channel);

/**
 * most_register_aim - registers an AIM (driver) with the core
 * @aim: instance of AIM to be registered
 */
int most_register_aim(struct most_aim *aim)
{
	struct most_aim_obj *aim_obj;

	if (!aim) {
		pr_err("Bad driver\n");
		return -EINVAL;
	}
	aim_obj = create_most_aim_obj(aim->name);
	if (!aim_obj) {
		pr_info("failed to alloc driver object\n");
		return -ENOMEM;
	}
	aim_obj->driver = aim;
	aim->context = aim_obj;
	pr_info("registered new application interfacing module %s\n",
		aim->name);
	list_add_tail(&aim_obj->list, &aim_list);
	return 0;
}
EXPORT_SYMBOL_GPL(most_register_aim);

/**
 * most_deregister_aim - deregisters an AIM (driver) with the core
 * @aim: AIM to be removed
 */
int most_deregister_aim(struct most_aim *aim)
{
	struct most_aim_obj *aim_obj;
	struct most_c_obj *c, *tmp;
	struct most_inst_obj *i, *i_tmp;

	if (!aim) {
		pr_err("Bad driver\n");
		return -EINVAL;
	}

	aim_obj = aim->context;
	if (!aim_obj) {
		pr_info("driver not registered.\n");
		return -EINVAL;
	}
	list_for_each_entry_safe(i, i_tmp, &instance_list, list) {
		list_for_each_entry_safe(c, tmp, &i->channel_list, list) {
			if (c->aim0.ptr == aim || c->aim1.ptr == aim)
				aim->disconnect_channel(
					c->iface, c->channel_id);
			if (c->aim0.ptr == aim)
				c->aim0.ptr = NULL;
			if (c->aim1.ptr == aim)
				c->aim1.ptr = NULL;
		}
	}
	list_del(&aim_obj->list);
	destroy_most_aim_obj(aim_obj);
	pr_info("deregistering application interfacing module %s\n", aim->name);
	return 0;
}
EXPORT_SYMBOL_GPL(most_deregister_aim);

/**
 * most_register_interface - registers an interface with core
 * @iface: pointer to the instance of the interface description.
 *
 * Allocates and initializes a new interface instance and all of its channels.
 * Returns a pointer to kobject or an error pointer.
 */
struct kobject *most_register_interface(struct most_interface *iface)
{
	unsigned int i;
	int id;
	char name[STRING_SIZE];
	char channel_name[STRING_SIZE];
	struct most_c_obj *c;
	struct most_inst_obj *inst;

	if (!iface || !iface->enqueue || !iface->configure ||
	    !iface->poison_channel || (iface->num_channels > MAX_CHANNELS)) {
		pr_err("Bad interface or channel overflow\n");
		return ERR_PTR(-EINVAL);
	}

	id = ida_simple_get(&mdev_id, 0, 0, GFP_KERNEL);
	if (id < 0) {
		pr_info("Failed to alloc mdev ID\n");
		return ERR_PTR(id);
	}
	snprintf(name, STRING_SIZE, "mdev%d", id);

	inst = create_most_inst_obj(name);
	if (!inst) {
		pr_info("Failed to allocate interface instance\n");
		ida_simple_remove(&mdev_id, id);
		return ERR_PTR(-ENOMEM);
	}

	iface->priv = inst;
	INIT_LIST_HEAD(&inst->channel_list);
	inst->iface = iface;
	inst->dev_id = id;
	list_add_tail(&inst->list, &instance_list);

	for (i = 0; i < iface->num_channels; i++) {
		const char *name_suffix = iface->channel_vector[i].name_suffix;

		if (!name_suffix)
			snprintf(channel_name, STRING_SIZE, "ch%d", i);
		else
			snprintf(channel_name, STRING_SIZE, "%s", name_suffix);

		/* this increments the reference count of this instance */
		c = create_most_c_obj(channel_name, &inst->kobj);
		if (!c)
			goto free_instance;
		inst->channel[i] = c;
		c->is_starving = 0;
		c->iface = iface;
		c->inst = inst;
		c->channel_id = i;
		c->keep_mbo = false;
		c->enqueue_halt = false;
		c->is_poisoned = false;
		c->cfg.direction = 0;
		c->cfg.data_type = 0;
		c->cfg.num_buffers = 0;
		c->cfg.buffer_size = 0;
		c->cfg.subbuffer_size = 0;
		c->cfg.packets_per_xact = 0;
		spin_lock_init(&c->fifo_lock);
		INIT_LIST_HEAD(&c->fifo);
		INIT_LIST_HEAD(&c->trash_fifo);
		INIT_LIST_HEAD(&c->halt_fifo);
		init_completion(&c->cleanup);
		atomic_set(&c->mbo_ref, 0);
		mutex_init(&c->start_mutex);
		mutex_init(&c->nq_mutex);
		list_add_tail(&c->list, &inst->channel_list);
	}
	pr_info("registered new MOST device mdev%d (%s)\n",
		inst->dev_id, iface->description);
	return &inst->kobj;

free_instance:
	pr_info("Failed allocate channel(s)\n");
	list_del(&inst->list);
	ida_simple_remove(&mdev_id, id);
	destroy_most_inst_obj(inst);
	return ERR_PTR(-ENOMEM);
}
EXPORT_SYMBOL_GPL(most_register_interface);

/**
 * most_deregister_interface - deregisters an interface with core
 * @iface: pointer to the interface instance description.
 *
 * Before removing an interface instance from the list, all running
 * channels are stopped and poisoned.
 */
void most_deregister_interface(struct most_interface *iface)
{
	struct most_inst_obj *i = iface->priv;
	struct most_c_obj *c;

	if (unlikely(!i)) {
		pr_info("Bad Interface\n");
		return;
	}
	pr_info("deregistering MOST device %s (%s)\n", i->kobj.name,
		iface->description);

	list_for_each_entry(c, &i->channel_list, list) {
		if (c->aim0.ptr)
			c->aim0.ptr->disconnect_channel(c->iface,
							c->channel_id);
		if (c->aim1.ptr)
			c->aim1.ptr->disconnect_channel(c->iface,
							c->channel_id);
		c->aim0.ptr = NULL;
		c->aim1.ptr = NULL;
	}

	ida_simple_remove(&mdev_id, i->dev_id);
	list_del(&i->list);
	destroy_most_inst_obj(i);
}
EXPORT_SYMBOL_GPL(most_deregister_interface);

/**
 * most_stop_enqueue - prevents core from enqueueing MBOs
 * @iface: pointer to interface
 * @id: channel id
 *
 * This is called by an HDM that _cannot_ attend to its duties and
 * is imminent to get run over by the core. The core is not going to
 * enqueue any further packets unless the flagging HDM calls
 * most_resume enqueue().
 */
void most_stop_enqueue(struct most_interface *iface, int id)
{
	struct most_c_obj *c = get_channel_by_iface(iface, id);

	if (!c)
		return;

	mutex_lock(&c->nq_mutex);
	c->enqueue_halt = true;
	mutex_unlock(&c->nq_mutex);
}
EXPORT_SYMBOL_GPL(most_stop_enqueue);

/**
 * most_resume_enqueue - allow core to enqueue MBOs again
 * @iface: pointer to interface
 * @id: channel id
 *
 * This clears the enqueue halt flag and enqueues all MBOs currently
 * sitting in the wait fifo.
 */
void most_resume_enqueue(struct most_interface *iface, int id)
{
	struct most_c_obj *c = get_channel_by_iface(iface, id);

	if (!c)
		return;

	mutex_lock(&c->nq_mutex);
	c->enqueue_halt = false;
	mutex_unlock(&c->nq_mutex);

	wake_up_interruptible(&c->hdm_fifo_wq);
}
EXPORT_SYMBOL_GPL(most_resume_enqueue);

static int __init most_init(void)
{
	int err;

	pr_info("init()\n");
	INIT_LIST_HEAD(&instance_list);
	INIT_LIST_HEAD(&aim_list);
	ida_init(&mdev_id);

	err = bus_register(&most_bus);
	if (err) {
		pr_info("Cannot register most bus\n");
		return err;
	}

	most_class = class_create(THIS_MODULE, "most");
	if (IS_ERR(most_class)) {
		pr_info("No udev support.\n");
		err = PTR_ERR(most_class);
		goto exit_bus;
	}

	err = driver_register(&mostcore);
	if (err) {
		pr_info("Cannot register core driver\n");
		goto exit_class;
	}

	core_dev = device_create(most_class, NULL, 0, NULL, "mostcore");
	if (IS_ERR(core_dev)) {
		err = PTR_ERR(core_dev);
		goto exit_driver;
	}

	most_aim_kset = kset_create_and_add("aims", NULL, &core_dev->kobj);
	if (!most_aim_kset) {
		err = -ENOMEM;
		goto exit_class_container;
	}

	most_inst_kset = kset_create_and_add("devices", NULL, &core_dev->kobj);
	if (!most_inst_kset) {
		err = -ENOMEM;
		goto exit_driver_kset;
	}

	return 0;

exit_driver_kset:
	kset_unregister(most_aim_kset);
exit_class_container:
	device_destroy(most_class, 0);
exit_driver:
	driver_unregister(&mostcore);
exit_class:
	class_destroy(most_class);
exit_bus:
	bus_unregister(&most_bus);
	return err;
}

static void __exit most_exit(void)
{
	struct most_inst_obj *i, *i_tmp;
	struct most_aim_obj *d, *d_tmp;

	pr_info("exit core module\n");
	list_for_each_entry_safe(d, d_tmp, &aim_list, list) {
		destroy_most_aim_obj(d);
	}

	list_for_each_entry_safe(i, i_tmp, &instance_list, list) {
		list_del(&i->list);
		destroy_most_inst_obj(i);
	}
	kset_unregister(most_inst_kset);
	kset_unregister(most_aim_kset);
	device_destroy(most_class, 0);
	driver_unregister(&mostcore);
	class_destroy(most_class);
	bus_unregister(&most_bus);
	ida_destroy(&mdev_id);
}

module_init(most_init);
module_exit(most_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Christian Gromm <christian.gromm@microchip.com>");
MODULE_DESCRIPTION("Core module of stacked MOST Linux driver");
