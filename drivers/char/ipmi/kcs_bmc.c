// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2015-2018, Intel Corporation.
 * Copyright (c) 2021, IBM Corp.
 */

#include <linux/device.h>
#include <linux/module.h>

#include "kcs_bmc.h"

/* Implement both the device and client interfaces here */
#include "kcs_bmc_device.h"
#include "kcs_bmc_client.h"

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
	irqreturn_t rc;

	spin_lock(&kcs_bmc->lock);
	client = kcs_bmc->client;
	if (client) {
		rc = client->ops->event(client);
	} else {
		u8 status;

		status = kcs_bmc_read_status(kcs_bmc);
		if (status & KCS_BMC_STR_IBF) {
			/* Ack the event by reading the data */
			kcs_bmc_read_data(kcs_bmc);
			rc = IRQ_HANDLED;
		} else {
			rc = IRQ_NONE;
		}
	}
	spin_unlock(&kcs_bmc->lock);

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
		kcs_bmc->client = client;
		rc = 0;
	}
	spin_unlock_irq(&kcs_bmc->lock);

	return rc;
}
EXPORT_SYMBOL(kcs_bmc_enable_device);

void kcs_bmc_disable_device(struct kcs_bmc_device *kcs_bmc, struct kcs_bmc_client *client)
{
	spin_lock_irq(&kcs_bmc->lock);
	if (client == kcs_bmc->client)
		kcs_bmc->client = NULL;
	spin_unlock_irq(&kcs_bmc->lock);
}
EXPORT_SYMBOL(kcs_bmc_disable_device);

int kcs_bmc_ipmi_add_device(struct kcs_bmc_device *kcs_bmc);
int kcs_bmc_add_device(struct kcs_bmc_device *kcs_bmc)
{
	return kcs_bmc_ipmi_add_device(kcs_bmc);
}
EXPORT_SYMBOL(kcs_bmc_add_device);

int kcs_bmc_ipmi_remove_device(struct kcs_bmc_device *kcs_bmc);
void kcs_bmc_remove_device(struct kcs_bmc_device *kcs_bmc)
{
	if (kcs_bmc_ipmi_remove_device(kcs_bmc))
		pr_warn("Failed to remove device for KCS channel %d\n",
			kcs_bmc->channel);
}
EXPORT_SYMBOL(kcs_bmc_remove_device);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Haiyue Wang <haiyue.wang@linux.intel.com>");
MODULE_AUTHOR("Andrew Jeffery <andrew@aj.id.au>");
MODULE_DESCRIPTION("KCS BMC to handle the IPMI request from system software");
