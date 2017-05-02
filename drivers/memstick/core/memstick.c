/*
 *  Sony MemoryStick support
 *
 *  Copyright (C) 2007 Alex Dubov <oakad@yahoo.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Special thanks to Carlos Corbacho for providing various MemoryStick cards
 * that made this driver possible.
 *
 */

#include <linux/memstick.h>
#include <linux/idr.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/module.h>

#define DRIVER_NAME "memstick"

static unsigned int cmd_retries = 3;
module_param(cmd_retries, uint, 0644);

static struct workqueue_struct *workqueue;
static DEFINE_IDR(memstick_host_idr);
static DEFINE_SPINLOCK(memstick_host_lock);

static int memstick_dev_match(struct memstick_dev *card,
			      struct memstick_device_id *id)
{
	if (id->match_flags & MEMSTICK_MATCH_ALL) {
		if ((id->type == card->id.type)
		    && (id->category == card->id.category)
		    && (id->class == card->id.class))
			return 1;
	}

	return 0;
}

static int memstick_bus_match(struct device *dev, struct device_driver *drv)
{
	struct memstick_dev *card = container_of(dev, struct memstick_dev,
						 dev);
	struct memstick_driver *ms_drv = container_of(drv,
						      struct memstick_driver,
						      driver);
	struct memstick_device_id *ids = ms_drv->id_table;

	if (ids) {
		while (ids->match_flags) {
			if (memstick_dev_match(card, ids))
				return 1;
			++ids;
		}
	}
	return 0;
}

static int memstick_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	struct memstick_dev *card = container_of(dev, struct memstick_dev,
						  dev);

	if (add_uevent_var(env, "MEMSTICK_TYPE=%02X", card->id.type))
		return -ENOMEM;

	if (add_uevent_var(env, "MEMSTICK_CATEGORY=%02X", card->id.category))
		return -ENOMEM;

	if (add_uevent_var(env, "MEMSTICK_CLASS=%02X", card->id.class))
		return -ENOMEM;

	return 0;
}

static int memstick_device_probe(struct device *dev)
{
	struct memstick_dev *card = container_of(dev, struct memstick_dev,
						 dev);
	struct memstick_driver *drv = container_of(dev->driver,
						   struct memstick_driver,
						   driver);
	int rc = -ENODEV;

	if (dev->driver && drv->probe) {
		rc = drv->probe(card);
		if (!rc)
			get_device(dev);
	}
	return rc;
}

static int memstick_device_remove(struct device *dev)
{
	struct memstick_dev *card = container_of(dev, struct memstick_dev,
						  dev);
	struct memstick_driver *drv = container_of(dev->driver,
						   struct memstick_driver,
						   driver);

	if (dev->driver && drv->remove) {
		drv->remove(card);
		card->dev.driver = NULL;
	}

	put_device(dev);
	return 0;
}

#ifdef CONFIG_PM

static int memstick_device_suspend(struct device *dev, pm_message_t state)
{
	struct memstick_dev *card = container_of(dev, struct memstick_dev,
						  dev);
	struct memstick_driver *drv = container_of(dev->driver,
						   struct memstick_driver,
						   driver);

	if (dev->driver && drv->suspend)
		return drv->suspend(card, state);
	return 0;
}

static int memstick_device_resume(struct device *dev)
{
	struct memstick_dev *card = container_of(dev, struct memstick_dev,
						  dev);
	struct memstick_driver *drv = container_of(dev->driver,
						   struct memstick_driver,
						   driver);

	if (dev->driver && drv->resume)
		return drv->resume(card);
	return 0;
}

#else

#define memstick_device_suspend NULL
#define memstick_device_resume NULL

#endif /* CONFIG_PM */

#define MEMSTICK_ATTR(name, format)                                           \
static ssize_t name##_show(struct device *dev, struct device_attribute *attr, \
			    char *buf)                                        \
{                                                                             \
	struct memstick_dev *card = container_of(dev, struct memstick_dev,    \
						 dev);                        \
	return sprintf(buf, format, card->id.name);                           \
}                                                                             \
static DEVICE_ATTR_RO(name);

MEMSTICK_ATTR(type, "%02X");
MEMSTICK_ATTR(category, "%02X");
MEMSTICK_ATTR(class, "%02X");

static struct attribute *memstick_dev_attrs[] = {
	&dev_attr_type.attr,
	&dev_attr_category.attr,
	&dev_attr_class.attr,
	NULL,
};
ATTRIBUTE_GROUPS(memstick_dev);

static struct bus_type memstick_bus_type = {
	.name           = "memstick",
	.dev_groups	= memstick_dev_groups,
	.match          = memstick_bus_match,
	.uevent         = memstick_uevent,
	.probe          = memstick_device_probe,
	.remove         = memstick_device_remove,
	.suspend        = memstick_device_suspend,
	.resume         = memstick_device_resume
};

static void memstick_free(struct device *dev)
{
	struct memstick_host *host = container_of(dev, struct memstick_host,
						  dev);
	kfree(host);
}

static struct class memstick_host_class = {
	.name        = "memstick_host",
	.dev_release = memstick_free
};

static void memstick_free_card(struct device *dev)
{
	struct memstick_dev *card = container_of(dev, struct memstick_dev,
						 dev);
	kfree(card);
}

static int memstick_dummy_check(struct memstick_dev *card)
{
	return 0;
}

/**
 * memstick_detect_change - schedule media detection on memstick host
 * @host - host to use
 */
void memstick_detect_change(struct memstick_host *host)
{
	queue_work(workqueue, &host->media_checker);
}
EXPORT_SYMBOL(memstick_detect_change);

/**
 * memstick_next_req - called by host driver to obtain next request to process
 * @host - host to use
 * @mrq - pointer to stick the request to
 *
 * Host calls this function from idle state (*mrq == NULL) or after finishing
 * previous request (*mrq should point to it). If previous request was
 * unsuccessful, it is retried for predetermined number of times. Return value
 * of 0 means that new request was assigned to the host.
 */
int memstick_next_req(struct memstick_host *host, struct memstick_request **mrq)
{
	int rc = -ENXIO;

	if ((*mrq) && (*mrq)->error && host->retries) {
		(*mrq)->error = rc;
		host->retries--;
		return 0;
	}

	if (host->card && host->card->next_request)
		rc = host->card->next_request(host->card, mrq);

	if (!rc)
		host->retries = cmd_retries > 1 ? cmd_retries - 1 : 1;
	else
		*mrq = NULL;

	return rc;
}
EXPORT_SYMBOL(memstick_next_req);

/**
 * memstick_new_req - notify the host that some requests are pending
 * @host - host to use
 */
void memstick_new_req(struct memstick_host *host)
{
	if (host->card) {
		host->retries = cmd_retries;
		reinit_completion(&host->card->mrq_complete);
		host->request(host);
	}
}
EXPORT_SYMBOL(memstick_new_req);

/**
 * memstick_init_req_sg - set request fields needed for bulk data transfer
 * @mrq - request to use
 * @tpc - memstick Transport Protocol Command
 * @sg - TPC argument
 */
void memstick_init_req_sg(struct memstick_request *mrq, unsigned char tpc,
			  const struct scatterlist *sg)
{
	mrq->tpc = tpc;
	if (tpc & 8)
		mrq->data_dir = WRITE;
	else
		mrq->data_dir = READ;

	mrq->sg = *sg;
	mrq->long_data = 1;

	if (tpc == MS_TPC_SET_CMD || tpc == MS_TPC_EX_SET_CMD)
		mrq->need_card_int = 1;
	else
		mrq->need_card_int = 0;
}
EXPORT_SYMBOL(memstick_init_req_sg);

/**
 * memstick_init_req - set request fields needed for short data transfer
 * @mrq - request to use
 * @tpc - memstick Transport Protocol Command
 * @buf - TPC argument buffer
 * @length - TPC argument size
 *
 * The intended use of this function (transfer of data items several bytes
 * in size) allows us to just copy the value between request structure and
 * user supplied buffer.
 */
void memstick_init_req(struct memstick_request *mrq, unsigned char tpc,
		       const void *buf, size_t length)
{
	mrq->tpc = tpc;
	if (tpc & 8)
		mrq->data_dir = WRITE;
	else
		mrq->data_dir = READ;

	mrq->data_len = length > sizeof(mrq->data) ? sizeof(mrq->data) : length;
	if (mrq->data_dir == WRITE)
		memcpy(mrq->data, buf, mrq->data_len);

	mrq->long_data = 0;

	if (tpc == MS_TPC_SET_CMD || tpc == MS_TPC_EX_SET_CMD)
		mrq->need_card_int = 1;
	else
		mrq->need_card_int = 0;
}
EXPORT_SYMBOL(memstick_init_req);

/*
 * Functions prefixed with "h_" are protocol callbacks. They can be called from
 * interrupt context. Return value of 0 means that request processing is still
 * ongoing, while special error value of -EAGAIN means that current request is
 * finished (and request processor should come back some time later).
 */

static int h_memstick_read_dev_id(struct memstick_dev *card,
				  struct memstick_request **mrq)
{
	struct ms_id_register id_reg;

	if (!(*mrq)) {
		memstick_init_req(&card->current_mrq, MS_TPC_READ_REG, &id_reg,
				  sizeof(struct ms_id_register));
		*mrq = &card->current_mrq;
		return 0;
	} else {
		if (!(*mrq)->error) {
			memcpy(&id_reg, (*mrq)->data, sizeof(id_reg));
			card->id.match_flags = MEMSTICK_MATCH_ALL;
			card->id.type = id_reg.type;
			card->id.category = id_reg.category;
			card->id.class = id_reg.class;
			dev_dbg(&card->dev, "if_mode = %02x\n", id_reg.if_mode);
		}
		complete(&card->mrq_complete);
		return -EAGAIN;
	}
}

static int h_memstick_set_rw_addr(struct memstick_dev *card,
				  struct memstick_request **mrq)
{
	if (!(*mrq)) {
		memstick_init_req(&card->current_mrq, MS_TPC_SET_RW_REG_ADRS,
				  (char *)&card->reg_addr,
				  sizeof(card->reg_addr));
		*mrq = &card->current_mrq;
		return 0;
	} else {
		complete(&card->mrq_complete);
		return -EAGAIN;
	}
}

/**
 * memstick_set_rw_addr - issue SET_RW_REG_ADDR request and wait for it to
 *                        complete
 * @card - media device to use
 */
int memstick_set_rw_addr(struct memstick_dev *card)
{
	card->next_request = h_memstick_set_rw_addr;
	memstick_new_req(card->host);
	wait_for_completion(&card->mrq_complete);

	return card->current_mrq.error;
}
EXPORT_SYMBOL(memstick_set_rw_addr);

static struct memstick_dev *memstick_alloc_card(struct memstick_host *host)
{
	struct memstick_dev *card = kzalloc(sizeof(struct memstick_dev),
					    GFP_KERNEL);
	struct memstick_dev *old_card = host->card;
	struct ms_id_register id_reg;

	if (card) {
		card->host = host;
		dev_set_name(&card->dev, "%s", dev_name(&host->dev));
		card->dev.parent = &host->dev;
		card->dev.bus = &memstick_bus_type;
		card->dev.release = memstick_free_card;
		card->check = memstick_dummy_check;

		card->reg_addr.r_offset = offsetof(struct ms_register, id);
		card->reg_addr.r_length = sizeof(id_reg);
		card->reg_addr.w_offset = offsetof(struct ms_register, id);
		card->reg_addr.w_length = sizeof(id_reg);

		init_completion(&card->mrq_complete);

		host->card = card;
		if (memstick_set_rw_addr(card))
			goto err_out;

		card->next_request = h_memstick_read_dev_id;
		memstick_new_req(host);
		wait_for_completion(&card->mrq_complete);

		if (card->current_mrq.error)
			goto err_out;
	}
	host->card = old_card;
	return card;
err_out:
	host->card = old_card;
	kfree(card);
	return NULL;
}

static int memstick_power_on(struct memstick_host *host)
{
	int rc = host->set_param(host, MEMSTICK_POWER, MEMSTICK_POWER_ON);

	if (!rc)
		rc = host->set_param(host, MEMSTICK_INTERFACE, MEMSTICK_SERIAL);

	return rc;
}

static void memstick_check(struct work_struct *work)
{
	struct memstick_host *host = container_of(work, struct memstick_host,
						  media_checker);
	struct memstick_dev *card;

	dev_dbg(&host->dev, "memstick_check started\n");
	mutex_lock(&host->lock);
	if (!host->card) {
		if (memstick_power_on(host))
			goto out_power_off;
	} else if (host->card->stop)
		host->card->stop(host->card);

	card = memstick_alloc_card(host);

	if (!card) {
		if (host->card) {
			device_unregister(&host->card->dev);
			host->card = NULL;
		}
	} else {
		dev_dbg(&host->dev, "new card %02x, %02x, %02x\n",
			card->id.type, card->id.category, card->id.class);
		if (host->card) {
			if (memstick_set_rw_addr(host->card)
			    || !memstick_dev_match(host->card, &card->id)
			    || !(host->card->check(host->card))) {
				device_unregister(&host->card->dev);
				host->card = NULL;
			} else if (host->card->start)
				host->card->start(host->card);
		}

		if (!host->card) {
			host->card = card;
			if (device_register(&card->dev)) {
				put_device(&card->dev);
				kfree(host->card);
				host->card = NULL;
			}
		} else
			kfree(card);
	}

out_power_off:
	if (!host->card)
		host->set_param(host, MEMSTICK_POWER, MEMSTICK_POWER_OFF);

	mutex_unlock(&host->lock);
	dev_dbg(&host->dev, "memstick_check finished\n");
}

/**
 * memstick_alloc_host - allocate a memstick_host structure
 * @extra: size of the user private data to allocate
 * @dev: parent device of the host
 */
struct memstick_host *memstick_alloc_host(unsigned int extra,
					  struct device *dev)
{
	struct memstick_host *host;

	host = kzalloc(sizeof(struct memstick_host) + extra, GFP_KERNEL);
	if (host) {
		mutex_init(&host->lock);
		INIT_WORK(&host->media_checker, memstick_check);
		host->dev.class = &memstick_host_class;
		host->dev.parent = dev;
		device_initialize(&host->dev);
	}
	return host;
}
EXPORT_SYMBOL(memstick_alloc_host);

/**
 * memstick_add_host - start request processing on memstick host
 * @host - host to use
 */
int memstick_add_host(struct memstick_host *host)
{
	int rc;

	idr_preload(GFP_KERNEL);
	spin_lock(&memstick_host_lock);

	rc = idr_alloc(&memstick_host_idr, host, 0, 0, GFP_NOWAIT);
	if (rc >= 0)
		host->id = rc;

	spin_unlock(&memstick_host_lock);
	idr_preload_end();
	if (rc < 0)
		return rc;

	dev_set_name(&host->dev, "memstick%u", host->id);

	rc = device_add(&host->dev);
	if (rc) {
		spin_lock(&memstick_host_lock);
		idr_remove(&memstick_host_idr, host->id);
		spin_unlock(&memstick_host_lock);
		return rc;
	}

	host->set_param(host, MEMSTICK_POWER, MEMSTICK_POWER_OFF);
	memstick_detect_change(host);
	return 0;
}
EXPORT_SYMBOL(memstick_add_host);

/**
 * memstick_remove_host - stop request processing on memstick host
 * @host - host to use
 */
void memstick_remove_host(struct memstick_host *host)
{
	flush_workqueue(workqueue);
	mutex_lock(&host->lock);
	if (host->card)
		device_unregister(&host->card->dev);
	host->card = NULL;
	host->set_param(host, MEMSTICK_POWER, MEMSTICK_POWER_OFF);
	mutex_unlock(&host->lock);

	spin_lock(&memstick_host_lock);
	idr_remove(&memstick_host_idr, host->id);
	spin_unlock(&memstick_host_lock);
	device_del(&host->dev);
}
EXPORT_SYMBOL(memstick_remove_host);

/**
 * memstick_free_host - free memstick host
 * @host - host to use
 */
void memstick_free_host(struct memstick_host *host)
{
	mutex_destroy(&host->lock);
	put_device(&host->dev);
}
EXPORT_SYMBOL(memstick_free_host);

/**
 * memstick_suspend_host - notify bus driver of host suspension
 * @host - host to use
 */
void memstick_suspend_host(struct memstick_host *host)
{
	mutex_lock(&host->lock);
	host->set_param(host, MEMSTICK_POWER, MEMSTICK_POWER_OFF);
	mutex_unlock(&host->lock);
}
EXPORT_SYMBOL(memstick_suspend_host);

/**
 * memstick_resume_host - notify bus driver of host resumption
 * @host - host to use
 */
void memstick_resume_host(struct memstick_host *host)
{
	int rc = 0;

	mutex_lock(&host->lock);
	if (host->card)
		rc = memstick_power_on(host);
	mutex_unlock(&host->lock);

	if (!rc)
		memstick_detect_change(host);
}
EXPORT_SYMBOL(memstick_resume_host);

int memstick_register_driver(struct memstick_driver *drv)
{
	drv->driver.bus = &memstick_bus_type;

	return driver_register(&drv->driver);
}
EXPORT_SYMBOL(memstick_register_driver);

void memstick_unregister_driver(struct memstick_driver *drv)
{
	driver_unregister(&drv->driver);
}
EXPORT_SYMBOL(memstick_unregister_driver);


static int __init memstick_init(void)
{
	int rc;

	workqueue = create_freezable_workqueue("kmemstick");
	if (!workqueue)
		return -ENOMEM;

	rc = bus_register(&memstick_bus_type);
	if (!rc)
		rc = class_register(&memstick_host_class);

	if (!rc)
		return 0;

	bus_unregister(&memstick_bus_type);
	destroy_workqueue(workqueue);

	return rc;
}

static void __exit memstick_exit(void)
{
	class_unregister(&memstick_host_class);
	bus_unregister(&memstick_bus_type);
	destroy_workqueue(workqueue);
	idr_destroy(&memstick_host_idr);
}

module_init(memstick_init);
module_exit(memstick_exit);

MODULE_AUTHOR("Alex Dubov");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Sony MemoryStick core driver");
