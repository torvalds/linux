// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2015-2018, Intel Corporation.
 * Copyright (c) 2021, IBM Corp.
 */

#include <linux/device.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>

#include "kcs_bmc.h"

/* Implement both the device and client interfaces here */
#include "kcs_bmc_device.h"
#include "kcs_bmc_client.h"

/* Record registered devices and drivers */
static DEFINE_MUTEX(kcs_bmc_lock);
static LIST_HEAD(kcs_bmc_devices);
static LIST_HEAD(kcs_bmc_drivers);

/* Consumer data access */

u8 kcs_bmc_read_data(struct kcs_bmc_device *kcs_bmc)
{
	return kcs_bmc->ops->io_inputb(kcs_bmc, kcs_bmc->ioreg.idr);
}
EXPORT_SYMBOL(kcs_bmc_read_data);

void kcs_bmc_write_data(struct kcs_bmc_device *kcs_bmc, u8 data)
{
	kcs_bmc->ops->io_outputb(kcs_bmc, kcs_bmc->ioreg.odr, data);
}
EXPORT_SYMBOL(kcs_bmc_write_data);

u8 kcs_bmc_read_status(struct kcs_bmc_device *kcs_bmc)
{
	return kcs_bmc->ops->io_inputb(kcs_bmc, kcs_bmc->ioreg.str);
}
EXPORT_SYMBOL(kcs_bmc_read_status);

void kcs_bmc_write_status(struct kcs_bmc_device *kcs_bmc, u8 data)
{
	kcs_bmc->ops->io_outputb(kcs_bmc, kcs_bmc->ioreg.str, data);
}
EXPORT_SYMBOL(kcs_bmc_write_status);

void kcs_bmc_update_status(struct kcs_bmc_device *kcs_bmc, u8 mask, u8 val)
{
	kcs_bmc->ops->io_updateb(kcs_bmc, kcs_bmc->ioreg.str, mask, val);
}
EXPORT_SYMBOL(kcs_bmc_update_status);

irqreturn_t kcs_bmc_handle_event(struct kcs_bmc_device *kcs_bmc)
{
	struct kcs_bmc_client *client;
	irqreturn_t rc = IRQ_NONE;
	unsigned long flags;

	spin_lock_irqsave(&kcs_bmc->lock, flags);
	client = kcs_bmc->client;
	if (client)
		rc = client->ops->event(client);
	spin_unlock_irqrestore(&kcs_bmc->lock, flags);

	return rc;
}
EXPORT_SYMBOL(kcs_bmc_handle_event);

int kcs_bmc_enable_device(struct kcs_bmc_device *kcs_bmc, struct kcs_bmc_client *client)
{
	int rc;

	spin_lock_irq(&kcs_bmc->lock);
	if (kcs_bmc->client) {
		rc = -EBUSY;
	} else {
		u8 mask = KCS_BMC_EVENT_TYPE_IBF;

		kcs_bmc->client = client;
		kcs_bmc_update_event_mask(kcs_bmc, mask, mask);
		rc = 0;
	}
	spin_unlock_irq(&kcs_bmc->lock);

	return rc;
}
EXPORT_SYMBOL(kcs_bmc_enable_device);

void kcs_bmc_disable_device(struct kcs_bmc_device *kcs_bmc, struct kcs_bmc_client *client)
{
	spin_lock_irq(&kcs_bmc->lock);
	if (client == kcs_bmc->client) {
		u8 mask = KCS_BMC_EVENT_TYPE_IBF | KCS_BMC_EVENT_TYPE_OBE;

		kcs_bmc_update_event_mask(kcs_bmc, mask, 0);
		kcs_bmc->client = NULL;
	}
	spin_unlock_irq(&kcs_bmc->lock);
}
EXPORT_SYMBOL(kcs_bmc_disable_device);

int kcs_bmc_add_device(struct kcs_bmc_device *kcs_bmc)
{
	struct kcs_bmc_driver *drv;
	int error = 0;
	int rc;

	spin_lock_init(&kcs_bmc->lock);
	kcs_bmc->client = NULL;

	mutex_lock(&kcs_bmc_lock);
	list_add(&kcs_bmc->entry, &kcs_bmc_devices);
	list_for_each_entry(drv, &kcs_bmc_drivers, entry) {
		rc = drv->ops->add_device(kcs_bmc);
		if (!rc)
			continue;

		dev_err(kcs_bmc->dev, "Failed to add chardev for KCS channel %d: %d",
			kcs_bmc->channel, rc);
		error = rc;
	}
	mutex_unlock(&kcs_bmc_lock);

	return error;
}
EXPORT_SYMBOL(kcs_bmc_add_device);

void kcs_bmc_remove_device(struct kcs_bmc_device *kcs_bmc)
{
	struct kcs_bmc_driver *drv;
	int rc;

	mutex_lock(&kcs_bmc_lock);
	list_del(&kcs_bmc->entry);
	list_for_each_entry(drv, &kcs_bmc_drivers, entry) {
		rc = drv->ops->remove_device(kcs_bmc);
		if (rc)
			dev_err(kcs_bmc->dev, "Failed to remove chardev for KCS channel %d: %d",
				kcs_bmc->channel, rc);
	}
	mutex_unlock(&kcs_bmc_lock);
}
EXPORT_SYMBOL(kcs_bmc_remove_device);

void kcs_bmc_register_driver(struct kcs_bmc_driver *drv)
{
	struct kcs_bmc_device *kcs_bmc;
	int rc;

	mutex_lock(&kcs_bmc_lock);
	list_add(&drv->entry, &kcs_bmc_drivers);
	list_for_each_entry(kcs_bmc, &kcs_bmc_devices, entry) {
		rc = drv->ops->add_device(kcs_bmc);
		if (rc)
			dev_err(kcs_bmc->dev, "Failed to add driver for KCS channel %d: %d",
				kcs_bmc->channel, rc);
	}
	mutex_unlock(&kcs_bmc_lock);
}
EXPORT_SYMBOL(kcs_bmc_register_driver);

void kcs_bmc_unregister_driver(struct kcs_bmc_driver *drv)
{
	struct kcs_bmc_device *kcs_bmc;
	int rc;

	mutex_lock(&kcs_bmc_lock);
	list_del(&drv->entry);
	list_for_each_entry(kcs_bmc, &kcs_bmc_devices, entry) {
		rc = drv->ops->remove_device(kcs_bmc);
		if (rc)
			dev_err(kcs_bmc->dev, "Failed to remove driver for KCS channel %d: %d",
				kcs_bmc->channel, rc);
	}
	mutex_unlock(&kcs_bmc_lock);
}
EXPORT_SYMBOL(kcs_bmc_unregister_driver);

void kcs_bmc_update_event_mask(struct kcs_bmc_device *kcs_bmc, u8 mask, u8 events)
{
	kcs_bmc->ops->irq_mask_update(kcs_bmc, mask, events);
}
EXPORT_SYMBOL(kcs_bmc_update_event_mask);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Haiyue Wang <haiyue.wang@linux.intel.com>");
MODULE_AUTHOR("Andrew Jeffery <andrew@aj.id.au>");
MODULE_DESCRIPTION("KCS BMC to handle the IPMI request from system software");
