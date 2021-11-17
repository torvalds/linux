// SPDX-License-Identifier: GPL-2.0
/*
 * core.c - Implementation of core module of MOST Linux driver stack
 *
 * Copyright (C) 2013-2020 Microchip Technology Germany II GmbH & Co. KG
 */

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
#include <linux/most.h>

#define MAX_CHANNELS	64
#define STRING_SIZE	80

static struct ida mdev_id;
static int dummy_num_buffers;
static struct list_head comp_list;

struct pipe {
	struct most_component *comp;
	int refs;
	int num_buffers;
};

struct most_channel {
	struct device dev;
	struct completion cleanup;
	atomic_t mbo_ref;
	atomic_t mbo_nq_level;
	u16 channel_id;
	char name[STRING_SIZE];
	bool is_poisoned;
	struct mutex start_mutex; /* channel activation synchronization */
	struct mutex nq_mutex; /* nq thread synchronization */
	int is_starving;
	struct most_interface *iface;
	struct most_channel_config cfg;
	bool keep_mbo;
	bool enqueue_halt;
	struct list_head fifo;
	spinlock_t fifo_lock; /* fifo access synchronization */
	struct list_head halt_fifo;
	struct list_head list;
	struct pipe pipe0;
	struct pipe pipe1;
	struct list_head trash_fifo;
	struct task_struct *hdm_enqueue_task;
	wait_queue_head_t hdm_fifo_wq;

};

#define to_channel(d) container_of(d, struct most_channel, dev)

struct interface_private {
	int dev_id;
	char name[STRING_SIZE];
	struct most_channel *channel[MAX_CHANNELS];
	struct list_head channel_list;
};

static const struct {
	int most_ch_data_type;
	const char *name;
} ch_data_type[] = {
	{ MOST_CH_CONTROL, "control" },
	{ MOST_CH_ASYNC, "async" },
	{ MOST_CH_SYNC, "sync" },
	{ MOST_CH_ISOC, "isoc"},
	{ MOST_CH_ISOC, "isoc_avp"},
};

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

/**
 * most_free_mbo_coherent - free an MBO and its coherent buffer
 * @mbo: most buffer
 */
static void most_free_mbo_coherent(struct mbo *mbo)
{
	struct most_channel *c = mbo->context;
	u16 const coherent_buf_size = c->cfg.buffer_size + c->cfg.extra_len;

	if (c->iface->dma_free)
		c->iface->dma_free(mbo, coherent_buf_size);
	else
		kfree(mbo->virt_address);
	kfree(mbo);
	if (atomic_sub_and_test(1, &c->mbo_ref))
		complete(&c->cleanup);
}

/**
 * flush_channel_fifos - clear the channel fifos
 * @c: pointer to channel object
 */
static void flush_channel_fifos(struct most_channel *c)
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
		dev_warn(&c->dev, "Channel or trash fifo not empty\n");
}

/**
 * flush_trash_fifo - clear the trash fifo
 * @c: pointer to channel object
 */
static int flush_trash_fifo(struct most_channel *c)
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

static ssize_t available_directions_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct most_channel *c = to_channel(dev);
	unsigned int i = c->channel_id;

	strcpy(buf, "");
	if (c->iface->channel_vector[i].direction & MOST_CH_RX)
		strcat(buf, "rx ");
	if (c->iface->channel_vector[i].direction & MOST_CH_TX)
		strcat(buf, "tx ");
	strcat(buf, "\n");
	return strlen(buf);
}

static ssize_t available_datatypes_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct most_channel *c = to_channel(dev);
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

static ssize_t number_of_packet_buffers_show(struct device *dev,
					     struct device_attribute *attr,
					     char *buf)
{
	struct most_channel *c = to_channel(dev);
	unsigned int i = c->channel_id;

	return snprintf(buf, PAGE_SIZE, "%d\n",
			c->iface->channel_vector[i].num_buffers_packet);
}

static ssize_t number_of_stream_buffers_show(struct device *dev,
					     struct device_attribute *attr,
					     char *buf)
{
	struct most_channel *c = to_channel(dev);
	unsigned int i = c->channel_id;

	return snprintf(buf, PAGE_SIZE, "%d\n",
			c->iface->channel_vector[i].num_buffers_streaming);
}

static ssize_t size_of_packet_buffer_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct most_channel *c = to_channel(dev);
	unsigned int i = c->channel_id;

	return snprintf(buf, PAGE_SIZE, "%d\n",
			c->iface->channel_vector[i].buffer_size_packet);
}

static ssize_t size_of_stream_buffer_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct most_channel *c = to_channel(dev);
	unsigned int i = c->channel_id;

	return snprintf(buf, PAGE_SIZE, "%d\n",
			c->iface->channel_vector[i].buffer_size_streaming);
}

static ssize_t channel_starving_show(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct most_channel *c = to_channel(dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", c->is_starving);
}

static ssize_t set_number_of_buffers_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct most_channel *c = to_channel(dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", c->cfg.num_buffers);
}

static ssize_t set_buffer_size_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct most_channel *c = to_channel(dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", c->cfg.buffer_size);
}

static ssize_t set_direction_show(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	struct most_channel *c = to_channel(dev);

	if (c->cfg.direction & MOST_CH_TX)
		return snprintf(buf, PAGE_SIZE, "tx\n");
	else if (c->cfg.direction & MOST_CH_RX)
		return snprintf(buf, PAGE_SIZE, "rx\n");
	return snprintf(buf, PAGE_SIZE, "unconfigured\n");
}

static ssize_t set_datatype_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	int i;
	struct most_channel *c = to_channel(dev);

	for (i = 0; i < ARRAY_SIZE(ch_data_type); i++) {
		if (c->cfg.data_type & ch_data_type[i].most_ch_data_type)
			return snprintf(buf, PAGE_SIZE, "%s",
					ch_data_type[i].name);
	}
	return snprintf(buf, PAGE_SIZE, "unconfigured\n");
}

static ssize_t set_subbuffer_size_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct most_channel *c = to_channel(dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", c->cfg.subbuffer_size);
}

static ssize_t set_packets_per_xact_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct most_channel *c = to_channel(dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", c->cfg.packets_per_xact);
}

static ssize_t set_dbr_size_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct most_channel *c = to_channel(dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", c->cfg.dbr_size);
}

#define to_dev_attr(a) container_of(a, struct device_attribute, attr)
static umode_t channel_attr_is_visible(struct kobject *kobj,
				       struct attribute *attr, int index)
{
	struct device_attribute *dev_attr = to_dev_attr(attr);
	struct device *dev = kobj_to_dev(kobj);
	struct most_channel *c = to_channel(dev);

	if (!strcmp(dev_attr->attr.name, "set_dbr_size") &&
	    (c->iface->interface != ITYPE_MEDIALB_DIM2))
		return 0;
	if (!strcmp(dev_attr->attr.name, "set_packets_per_xact") &&
	    (c->iface->interface != ITYPE_USB))
		return 0;

	return attr->mode;
}

#define DEV_ATTR(_name)  (&dev_attr_##_name.attr)

static DEVICE_ATTR_RO(available_directions);
static DEVICE_ATTR_RO(available_datatypes);
static DEVICE_ATTR_RO(number_of_packet_buffers);
static DEVICE_ATTR_RO(number_of_stream_buffers);
static DEVICE_ATTR_RO(size_of_stream_buffer);
static DEVICE_ATTR_RO(size_of_packet_buffer);
static DEVICE_ATTR_RO(channel_starving);
static DEVICE_ATTR_RO(set_buffer_size);
static DEVICE_ATTR_RO(set_number_of_buffers);
static DEVICE_ATTR_RO(set_direction);
static DEVICE_ATTR_RO(set_datatype);
static DEVICE_ATTR_RO(set_subbuffer_size);
static DEVICE_ATTR_RO(set_packets_per_xact);
static DEVICE_ATTR_RO(set_dbr_size);

static struct attribute *channel_attrs[] = {
	DEV_ATTR(available_directions),
	DEV_ATTR(available_datatypes),
	DEV_ATTR(number_of_packet_buffers),
	DEV_ATTR(number_of_stream_buffers),
	DEV_ATTR(size_of_stream_buffer),
	DEV_ATTR(size_of_packet_buffer),
	DEV_ATTR(channel_starving),
	DEV_ATTR(set_buffer_size),
	DEV_ATTR(set_number_of_buffers),
	DEV_ATTR(set_direction),
	DEV_ATTR(set_datatype),
	DEV_ATTR(set_subbuffer_size),
	DEV_ATTR(set_packets_per_xact),
	DEV_ATTR(set_dbr_size),
	NULL,
};

static const struct attribute_group channel_attr_group = {
	.attrs = channel_attrs,
	.is_visible = channel_attr_is_visible,
};

static const struct attribute_group *channel_attr_groups[] = {
	&channel_attr_group,
	NULL,
};

static ssize_t description_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct most_interface *iface = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%s\n", iface->description);
}

static ssize_t interface_show(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	struct most_interface *iface = dev_get_drvdata(dev);

	switch (iface->interface) {
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

static DEVICE_ATTR_RO(description);
static DEVICE_ATTR_RO(interface);

static struct attribute *interface_attrs[] = {
	DEV_ATTR(description),
	DEV_ATTR(interface),
	NULL,
};

static const struct attribute_group interface_attr_group = {
	.attrs = interface_attrs,
};

static const struct attribute_group *interface_attr_groups[] = {
	&interface_attr_group,
	NULL,
};

static struct most_component *match_component(char *name)
{
	struct most_component *comp;

	list_for_each_entry(comp, &comp_list, list) {
		if (!strcmp(comp->name, name))
			return comp;
	}
	return NULL;
}

struct show_links_data {
	int offs;
	char *buf;
};

static int print_links(struct device *dev, void *data)
{
	struct show_links_data *d = data;
	int offs = d->offs;
	char *buf = d->buf;
	struct most_channel *c;
	struct most_interface *iface = dev_get_drvdata(dev);

	list_for_each_entry(c, &iface->p->channel_list, list) {
		if (c->pipe0.comp) {
			offs += scnprintf(buf + offs,
					 PAGE_SIZE - offs,
					 "%s:%s:%s\n",
					 c->pipe0.comp->name,
					 dev_name(iface->dev),
					 dev_name(&c->dev));
		}
		if (c->pipe1.comp) {
			offs += scnprintf(buf + offs,
					 PAGE_SIZE - offs,
					 "%s:%s:%s\n",
					 c->pipe1.comp->name,
					 dev_name(iface->dev),
					 dev_name(&c->dev));
		}
	}
	d->offs = offs;
	return 0;
}

static int most_match(struct device *dev, struct device_driver *drv)
{
	if (!strcmp(dev_name(dev), "most"))
		return 0;
	else
		return 1;
}

static struct bus_type mostbus = {
	.name = "most",
	.match = most_match,
};

static ssize_t links_show(struct device_driver *drv, char *buf)
{
	struct show_links_data d = { .buf = buf };

	bus_for_each_dev(&mostbus, NULL, &d, print_links);
	return d.offs;
}

static ssize_t components_show(struct device_driver *drv, char *buf)
{
	struct most_component *comp;
	int offs = 0;

	list_for_each_entry(comp, &comp_list, list) {
		offs += scnprintf(buf + offs, PAGE_SIZE - offs, "%s\n",
				 comp->name);
	}
	return offs;
}

/**
 * get_channel - get pointer to channel
 * @mdev: name of the device interface
 * @mdev_ch: name of channel
 */
static struct most_channel *get_channel(char *mdev, char *mdev_ch)
{
	struct device *dev = NULL;
	struct most_interface *iface;
	struct most_channel *c, *tmp;

	dev = bus_find_device_by_name(&mostbus, NULL, mdev);
	if (!dev)
		return NULL;
	put_device(dev);
	iface = dev_get_drvdata(dev);
	list_for_each_entry_safe(c, tmp, &iface->p->channel_list, list) {
		if (!strcmp(dev_name(&c->dev), mdev_ch))
			return c;
	}
	return NULL;
}

static
inline int link_channel_to_component(struct most_channel *c,
				     struct most_component *comp,
				     char *name,
				     char *comp_param)
{
	int ret;
	struct most_component **comp_ptr;

	if (!c->pipe0.comp)
		comp_ptr = &c->pipe0.comp;
	else if (!c->pipe1.comp)
		comp_ptr = &c->pipe1.comp;
	else
		return -ENOSPC;

	*comp_ptr = comp;
	ret = comp->probe_channel(c->iface, c->channel_id, &c->cfg, name,
				  comp_param);
	if (ret) {
		*comp_ptr = NULL;
		return ret;
	}
	return 0;
}

int most_set_cfg_buffer_size(char *mdev, char *mdev_ch, u16 val)
{
	struct most_channel *c = get_channel(mdev, mdev_ch);

	if (!c)
		return -ENODEV;
	c->cfg.buffer_size = val;
	return 0;
}

int most_set_cfg_subbuffer_size(char *mdev, char *mdev_ch, u16 val)
{
	struct most_channel *c = get_channel(mdev, mdev_ch);

	if (!c)
		return -ENODEV;
	c->cfg.subbuffer_size = val;
	return 0;
}

int most_set_cfg_dbr_size(char *mdev, char *mdev_ch, u16 val)
{
	struct most_channel *c = get_channel(mdev, mdev_ch);

	if (!c)
		return -ENODEV;
	c->cfg.dbr_size = val;
	return 0;
}

int most_set_cfg_num_buffers(char *mdev, char *mdev_ch, u16 val)
{
	struct most_channel *c = get_channel(mdev, mdev_ch);

	if (!c)
		return -ENODEV;
	c->cfg.num_buffers = val;
	return 0;
}

int most_set_cfg_datatype(char *mdev, char *mdev_ch, char *buf)
{
	int i;
	struct most_channel *c = get_channel(mdev, mdev_ch);

	if (!c)
		return -ENODEV;
	for (i = 0; i < ARRAY_SIZE(ch_data_type); i++) {
		if (!strcmp(buf, ch_data_type[i].name)) {
			c->cfg.data_type = ch_data_type[i].most_ch_data_type;
			break;
		}
	}

	if (i == ARRAY_SIZE(ch_data_type))
		dev_warn(&c->dev, "Invalid attribute settings\n");
	return 0;
}

int most_set_cfg_direction(char *mdev, char *mdev_ch, char *buf)
{
	struct most_channel *c = get_channel(mdev, mdev_ch);

	if (!c)
		return -ENODEV;
	if (!strcmp(buf, "dir_rx")) {
		c->cfg.direction = MOST_CH_RX;
	} else if (!strcmp(buf, "rx")) {
		c->cfg.direction = MOST_CH_RX;
	} else if (!strcmp(buf, "dir_tx")) {
		c->cfg.direction = MOST_CH_TX;
	} else if (!strcmp(buf, "tx")) {
		c->cfg.direction = MOST_CH_TX;
	} else {
		dev_err(&c->dev, "Invalid direction\n");
		return -ENODATA;
	}
	return 0;
}

int most_set_cfg_packets_xact(char *mdev, char *mdev_ch, u16 val)
{
	struct most_channel *c = get_channel(mdev, mdev_ch);

	if (!c)
		return -ENODEV;
	c->cfg.packets_per_xact = val;
	return 0;
}

int most_cfg_complete(char *comp_name)
{
	struct most_component *comp;

	comp = match_component(comp_name);
	if (!comp)
		return -ENODEV;

	return comp->cfg_complete();
}

int most_add_link(char *mdev, char *mdev_ch, char *comp_name, char *link_name,
		  char *comp_param)
{
	struct most_channel *c = get_channel(mdev, mdev_ch);
	struct most_component *comp = match_component(comp_name);

	if (!c || !comp)
		return -ENODEV;

	return link_channel_to_component(c, comp, link_name, comp_param);
}

int most_remove_link(char *mdev, char *mdev_ch, char *comp_name)
{
	struct most_channel *c;
	struct most_component *comp;

	comp = match_component(comp_name);
	if (!comp)
		return -ENODEV;
	c = get_channel(mdev, mdev_ch);
	if (!c)
		return -ENODEV;

	if (comp->disconnect_channel(c->iface, c->channel_id))
		return -EIO;
	if (c->pipe0.comp == comp)
		c->pipe0.comp = NULL;
	if (c->pipe1.comp == comp)
		c->pipe1.comp = NULL;
	return 0;
}

#define DRV_ATTR(_name)  (&driver_attr_##_name.attr)

static DRIVER_ATTR_RO(links);
static DRIVER_ATTR_RO(components);

static struct attribute *mc_attrs[] = {
	DRV_ATTR(links),
	DRV_ATTR(components),
	NULL,
};

static const struct attribute_group mc_attr_group = {
	.attrs = mc_attrs,
};

static const struct attribute_group *mc_attr_groups[] = {
	&mc_attr_group,
	NULL,
};

static struct device_driver mostbus_driver = {
	.name = "most_core",
	.bus = &mostbus,
	.groups = mc_attr_groups,
};

static inline void trash_mbo(struct mbo *mbo)
{
	unsigned long flags;
	struct most_channel *c = mbo->context;

	spin_lock_irqsave(&c->fifo_lock, flags);
	list_add(&mbo->list, &c->trash_fifo);
	spin_unlock_irqrestore(&c->fifo_lock, flags);
}

static bool hdm_mbo_ready(struct most_channel *c)
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
	struct most_channel *c = mbo->context;

	spin_lock_irqsave(&c->fifo_lock, flags);
	list_add_tail(&mbo->list, &c->halt_fifo);
	spin_unlock_irqrestore(&c->fifo_lock, flags);
	wake_up_interruptible(&c->hdm_fifo_wq);
}

static int hdm_enqueue_thread(void *data)
{
	struct most_channel *c = data;
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
			dev_err(&c->dev, "Buffer enqueue failed\n");
			nq_hdm_mbo(mbo);
			c->hdm_enqueue_task = NULL;
			return 0;
		}
	}

	return 0;
}

static int run_enqueue_thread(struct most_channel *c, int channel_id)
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
 * @mbo: most buffer
 *
 * This puts an MBO back to the list to have it ready for up coming
 * tx transactions.
 *
 * In case the MBO belongs to a channel that recently has been
 * poisoned, the MBO is scheduled to be trashed.
 * Calls the completion handler of an attached component.
 */
static void arm_mbo(struct mbo *mbo)
{
	unsigned long flags;
	struct most_channel *c;

	c = mbo->context;

	if (c->is_poisoned) {
		trash_mbo(mbo);
		return;
	}

	spin_lock_irqsave(&c->fifo_lock, flags);
	++*mbo->num_buffers_ptr;
	list_add_tail(&mbo->list, &c->fifo);
	spin_unlock_irqrestore(&c->fifo_lock, flags);

	if (c->pipe0.refs && c->pipe0.comp->tx_completion)
		c->pipe0.comp->tx_completion(c->iface, c->channel_id);

	if (c->pipe1.refs && c->pipe1.comp->tx_completion)
		c->pipe1.comp->tx_completion(c->iface, c->channel_id);
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
static int arm_mbo_chain(struct most_channel *c, int dir,
			 void (*compl)(struct mbo *))
{
	unsigned int i;
	struct mbo *mbo;
	unsigned long flags;
	u32 coherent_buf_size = c->cfg.buffer_size + c->cfg.extra_len;

	atomic_set(&c->mbo_nq_level, 0);

	for (i = 0; i < c->cfg.num_buffers; i++) {
		mbo = kzalloc(sizeof(*mbo), GFP_KERNEL);
		if (!mbo)
			goto flush_fifos;

		mbo->context = c;
		mbo->ifp = c->iface;
		mbo->hdm_channel_id = c->channel_id;
		if (c->iface->dma_alloc) {
			mbo->virt_address =
				c->iface->dma_alloc(mbo, coherent_buf_size);
		} else {
			mbo->virt_address =
				kzalloc(coherent_buf_size, GFP_KERNEL);
		}
		if (!mbo->virt_address)
			goto release_mbo;

		mbo->complete = compl;
		mbo->num_buffers_ptr = &dummy_num_buffers;
		if (dir == MOST_CH_RX) {
			nq_hdm_mbo(mbo);
			atomic_inc(&c->mbo_nq_level);
		} else {
			spin_lock_irqsave(&c->fifo_lock, flags);
			list_add_tail(&mbo->list, &c->fifo);
			spin_unlock_irqrestore(&c->fifo_lock, flags);
		}
	}
	return c->cfg.num_buffers;

release_mbo:
	kfree(mbo);

flush_fifos:
	flush_channel_fifos(c);
	return 0;
}

/**
 * most_submit_mbo - submits an MBO to fifo
 * @mbo: most buffer
 */
void most_submit_mbo(struct mbo *mbo)
{
	if (WARN_ONCE(!mbo || !mbo->context,
		      "Bad buffer or missing channel reference\n"))
		return;

	nq_hdm_mbo(mbo);
}
EXPORT_SYMBOL_GPL(most_submit_mbo);

/**
 * most_write_completion - write completion handler
 * @mbo: most buffer
 *
 * This recycles the MBO for further usage. In case the channel has been
 * poisoned, the MBO is scheduled to be trashed.
 */
static void most_write_completion(struct mbo *mbo)
{
	struct most_channel *c;

	c = mbo->context;
	if (unlikely(c->is_poisoned || (mbo->status == MBO_E_CLOSE)))
		trash_mbo(mbo);
	else
		arm_mbo(mbo);
}

int channel_has_mbo(struct most_interface *iface, int id,
		    struct most_component *comp)
{
	struct most_channel *c = iface->p->channel[id];
	unsigned long flags;
	int empty;

	if (unlikely(!c))
		return -EINVAL;

	if (c->pipe0.refs && c->pipe1.refs &&
	    ((comp == c->pipe0.comp && c->pipe0.num_buffers <= 0) ||
	     (comp == c->pipe1.comp && c->pipe1.num_buffers <= 0)))
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
 * @comp: driver component
 *
 * This attempts to get a free buffer out of the channel fifo.
 * Returns a pointer to MBO on success or NULL otherwise.
 */
struct mbo *most_get_mbo(struct most_interface *iface, int id,
			 struct most_component *comp)
{
	struct mbo *mbo;
	struct most_channel *c;
	unsigned long flags;
	int *num_buffers_ptr;

	c = iface->p->channel[id];
	if (unlikely(!c))
		return NULL;

	if (c->pipe0.refs && c->pipe1.refs &&
	    ((comp == c->pipe0.comp && c->pipe0.num_buffers <= 0) ||
	     (comp == c->pipe1.comp && c->pipe1.num_buffers <= 0)))
		return NULL;

	if (comp == c->pipe0.comp)
		num_buffers_ptr = &c->pipe0.num_buffers;
	else if (comp == c->pipe1.comp)
		num_buffers_ptr = &c->pipe1.num_buffers;
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
 * @mbo: most buffer
 */
void most_put_mbo(struct mbo *mbo)
{
	struct most_channel *c = mbo->context;

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
 * @mbo: most buffer
 *
 * This function is called by the HDM when data has been received from the
 * hardware and copied to the buffer of the MBO.
 *
 * In case the channel has been poisoned it puts the buffer in the trash queue.
 * Otherwise, it passes the buffer to an component for further processing.
 */
static void most_read_completion(struct mbo *mbo)
{
	struct most_channel *c = mbo->context;

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

	if (c->pipe0.refs && c->pipe0.comp->rx_completion &&
	    c->pipe0.comp->rx_completion(mbo) == 0)
		return;

	if (c->pipe1.refs && c->pipe1.comp->rx_completion &&
	    c->pipe1.comp->rx_completion(mbo) == 0)
		return;

	most_put_mbo(mbo);
}

/**
 * most_start_channel - prepares a channel for communication
 * @iface: pointer to interface instance
 * @id: channel ID
 * @comp: driver component
 *
 * This prepares the channel for usage. Cross-checks whether the
 * channel's been properly configured.
 *
 * Returns 0 on success or error code otherwise.
 */
int most_start_channel(struct most_interface *iface, int id,
		       struct most_component *comp)
{
	int num_buffer;
	int ret;
	struct most_channel *c = iface->p->channel[id];

	if (unlikely(!c))
		return -EINVAL;

	mutex_lock(&c->start_mutex);
	if (c->pipe0.refs + c->pipe1.refs > 0)
		goto out; /* already started by another component */

	if (!try_module_get(iface->mod)) {
		dev_err(&c->dev, "Failed to acquire HDM lock\n");
		mutex_unlock(&c->start_mutex);
		return -ENOLCK;
	}

	c->cfg.extra_len = 0;
	if (c->iface->configure(c->iface, c->channel_id, &c->cfg)) {
		dev_err(&c->dev, "Channel configuration failed. Go check settings...\n");
		ret = -EINVAL;
		goto err_put_module;
	}

	init_waitqueue_head(&c->hdm_fifo_wq);

	if (c->cfg.direction == MOST_CH_RX)
		num_buffer = arm_mbo_chain(c, c->cfg.direction,
					   most_read_completion);
	else
		num_buffer = arm_mbo_chain(c, c->cfg.direction,
					   most_write_completion);
	if (unlikely(!num_buffer)) {
		ret = -ENOMEM;
		goto err_put_module;
	}

	ret = run_enqueue_thread(c, id);
	if (ret)
		goto err_put_module;

	c->is_starving = 0;
	c->pipe0.num_buffers = c->cfg.num_buffers / 2;
	c->pipe1.num_buffers = c->cfg.num_buffers - c->pipe0.num_buffers;
	atomic_set(&c->mbo_ref, num_buffer);

out:
	if (comp == c->pipe0.comp)
		c->pipe0.refs++;
	if (comp == c->pipe1.comp)
		c->pipe1.refs++;
	mutex_unlock(&c->start_mutex);
	return 0;

err_put_module:
	module_put(iface->mod);
	mutex_unlock(&c->start_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(most_start_channel);

/**
 * most_stop_channel - stops a running channel
 * @iface: pointer to interface instance
 * @id: channel ID
 * @comp: driver component
 */
int most_stop_channel(struct most_interface *iface, int id,
		      struct most_component *comp)
{
	struct most_channel *c;

	if (unlikely((!iface) || (id >= iface->num_channels) || (id < 0))) {
		pr_err("Bad interface or index out of range\n");
		return -EINVAL;
	}
	c = iface->p->channel[id];
	if (unlikely(!c))
		return -EINVAL;

	mutex_lock(&c->start_mutex);
	if (c->pipe0.refs + c->pipe1.refs >= 2)
		goto out;

	if (c->hdm_enqueue_task)
		kthread_stop(c->hdm_enqueue_task);
	c->hdm_enqueue_task = NULL;

	if (iface->mod)
		module_put(iface->mod);

	c->is_poisoned = true;
	if (c->iface->poison_channel(c->iface, c->channel_id)) {
		dev_err(&c->dev, "Failed to stop channel %d of interface %s\n", c->channel_id,
			c->iface->description);
		mutex_unlock(&c->start_mutex);
		return -EAGAIN;
	}
	flush_trash_fifo(c);
	flush_channel_fifos(c);

#ifdef CMPL_INTERRUPTIBLE
	if (wait_for_completion_interruptible(&c->cleanup)) {
		dev_err(&c->dev, "Interrupted while cleaning up channel %d\n", c->channel_id);
		mutex_unlock(&c->start_mutex);
		return -EINTR;
	}
#else
	wait_for_completion(&c->cleanup);
#endif
	c->is_poisoned = false;

out:
	if (comp == c->pipe0.comp)
		c->pipe0.refs--;
	if (comp == c->pipe1.comp)
		c->pipe1.refs--;
	mutex_unlock(&c->start_mutex);
	return 0;
}
EXPORT_SYMBOL_GPL(most_stop_channel);

/**
 * most_register_component - registers a driver component with the core
 * @comp: driver component
 */
int most_register_component(struct most_component *comp)
{
	if (!comp) {
		pr_err("Bad component\n");
		return -EINVAL;
	}
	list_add_tail(&comp->list, &comp_list);
	return 0;
}
EXPORT_SYMBOL_GPL(most_register_component);

static int disconnect_channels(struct device *dev, void *data)
{
	struct most_interface *iface;
	struct most_channel *c, *tmp;
	struct most_component *comp = data;

	iface = dev_get_drvdata(dev);
	list_for_each_entry_safe(c, tmp, &iface->p->channel_list, list) {
		if (c->pipe0.comp == comp || c->pipe1.comp == comp)
			comp->disconnect_channel(c->iface, c->channel_id);
		if (c->pipe0.comp == comp)
			c->pipe0.comp = NULL;
		if (c->pipe1.comp == comp)
			c->pipe1.comp = NULL;
	}
	return 0;
}

/**
 * most_deregister_component - deregisters a driver component with the core
 * @comp: driver component
 */
int most_deregister_component(struct most_component *comp)
{
	if (!comp) {
		pr_err("Bad component\n");
		return -EINVAL;
	}

	bus_for_each_dev(&mostbus, NULL, comp, disconnect_channels);
	list_del(&comp->list);
	return 0;
}
EXPORT_SYMBOL_GPL(most_deregister_component);

static void release_channel(struct device *dev)
{
	struct most_channel *c = to_channel(dev);

	kfree(c);
}

/**
 * most_register_interface - registers an interface with core
 * @iface: device interface
 *
 * Allocates and initializes a new interface instance and all of its channels.
 * Returns a pointer to kobject or an error pointer.
 */
int most_register_interface(struct most_interface *iface)
{
	unsigned int i;
	int id;
	struct most_channel *c;

	if (!iface || !iface->enqueue || !iface->configure ||
	    !iface->poison_channel || (iface->num_channels > MAX_CHANNELS))
		return -EINVAL;

	id = ida_simple_get(&mdev_id, 0, 0, GFP_KERNEL);
	if (id < 0) {
		dev_err(iface->dev, "Failed to allocate device ID\n");
		return id;
	}

	iface->p = kzalloc(sizeof(*iface->p), GFP_KERNEL);
	if (!iface->p) {
		ida_simple_remove(&mdev_id, id);
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&iface->p->channel_list);
	iface->p->dev_id = id;
	strscpy(iface->p->name, iface->description, sizeof(iface->p->name));
	iface->dev->bus = &mostbus;
	iface->dev->groups = interface_attr_groups;
	dev_set_drvdata(iface->dev, iface);
	if (device_register(iface->dev)) {
		dev_err(iface->dev, "Failed to register interface device\n");
		kfree(iface->p);
		put_device(iface->dev);
		ida_simple_remove(&mdev_id, id);
		return -ENOMEM;
	}

	for (i = 0; i < iface->num_channels; i++) {
		const char *name_suffix = iface->channel_vector[i].name_suffix;

		c = kzalloc(sizeof(*c), GFP_KERNEL);
		if (!c)
			goto err_free_resources;
		if (!name_suffix)
			snprintf(c->name, STRING_SIZE, "ch%d", i);
		else
			snprintf(c->name, STRING_SIZE, "%s", name_suffix);
		c->dev.init_name = c->name;
		c->dev.parent = iface->dev;
		c->dev.groups = channel_attr_groups;
		c->dev.release = release_channel;
		iface->p->channel[i] = c;
		c->is_starving = 0;
		c->iface = iface;
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
		list_add_tail(&c->list, &iface->p->channel_list);
		if (device_register(&c->dev)) {
			dev_err(&c->dev, "Failed to register channel device\n");
			goto err_free_most_channel;
		}
	}
	most_interface_register_notify(iface->description);
	return 0;

err_free_most_channel:
	put_device(&c->dev);

err_free_resources:
	while (i > 0) {
		c = iface->p->channel[--i];
		device_unregister(&c->dev);
	}
	kfree(iface->p);
	device_unregister(iface->dev);
	ida_simple_remove(&mdev_id, id);
	return -ENOMEM;
}
EXPORT_SYMBOL_GPL(most_register_interface);

/**
 * most_deregister_interface - deregisters an interface with core
 * @iface: device interface
 *
 * Before removing an interface instance from the list, all running
 * channels are stopped and poisoned.
 */
void most_deregister_interface(struct most_interface *iface)
{
	int i;
	struct most_channel *c;

	for (i = 0; i < iface->num_channels; i++) {
		c = iface->p->channel[i];
		if (c->pipe0.comp)
			c->pipe0.comp->disconnect_channel(c->iface,
							c->channel_id);
		if (c->pipe1.comp)
			c->pipe1.comp->disconnect_channel(c->iface,
							c->channel_id);
		c->pipe0.comp = NULL;
		c->pipe1.comp = NULL;
		list_del(&c->list);
		device_unregister(&c->dev);
	}

	ida_simple_remove(&mdev_id, iface->p->dev_id);
	kfree(iface->p);
	device_unregister(iface->dev);
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
	struct most_channel *c = iface->p->channel[id];

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
	struct most_channel *c = iface->p->channel[id];

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

	INIT_LIST_HEAD(&comp_list);
	ida_init(&mdev_id);

	err = bus_register(&mostbus);
	if (err) {
		pr_err("Failed to register most bus\n");
		return err;
	}
	err = driver_register(&mostbus_driver);
	if (err) {
		pr_err("Failed to register core driver\n");
		goto err_unregister_bus;
	}
	configfs_init();
	return 0;

err_unregister_bus:
	bus_unregister(&mostbus);
	return err;
}

static void __exit most_exit(void)
{
	driver_unregister(&mostbus_driver);
	bus_unregister(&mostbus);
	ida_destroy(&mdev_id);
}

subsys_initcall(most_init);
module_exit(most_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Christian Gromm <christian.gromm@microchip.com>");
MODULE_DESCRIPTION("Core module of stacked MOST Linux driver");
