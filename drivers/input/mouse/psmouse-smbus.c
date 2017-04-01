/*
 * Copyright (c) 2017 Red Hat, Inc
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#define pr_fmt(fmt)		KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/libps2.h>
#include <linux/i2c.h>
#include <linux/serio.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include "psmouse.h"

struct psmouse_smbus_dev {
	struct i2c_board_info board;
	struct psmouse *psmouse;
	struct i2c_client *client;
	struct list_head node;
	bool dead;
};

static LIST_HEAD(psmouse_smbus_list);
static DEFINE_MUTEX(psmouse_smbus_mutex);

static void psmouse_smbus_check_adapter(struct i2c_adapter *adapter)
{
	struct psmouse_smbus_dev *smbdev;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_HOST_NOTIFY))
		return;

	mutex_lock(&psmouse_smbus_mutex);

	list_for_each_entry(smbdev, &psmouse_smbus_list, node) {
		if (smbdev->dead)
			continue;

		if (smbdev->client)
			continue;

		/*
		 * Here would be a good place to check if device is actually
		 * present, but it seems that SMBus will not respond unless we
		 * fully reset PS/2 connection.  So cross our fingers, and try
		 * to switch over, hopefully our system will not have too many
		 * "host notify" I2C adapters.
		 */
		psmouse_dbg(smbdev->psmouse,
			    "SMBus candidate adapter appeared, triggering rescan\n");
		serio_rescan(smbdev->psmouse->ps2dev.serio);
	}

	mutex_unlock(&psmouse_smbus_mutex);
}

static void psmouse_smbus_detach_i2c_client(struct i2c_client *client)
{
	struct psmouse_smbus_dev *smbdev, *tmp;

	mutex_lock(&psmouse_smbus_mutex);

	list_for_each_entry_safe(smbdev, tmp, &psmouse_smbus_list, node) {
		if (smbdev->client != client)
			continue;

		kfree(client->dev.platform_data);
		client->dev.platform_data = NULL;

		if (!smbdev->dead) {
			psmouse_dbg(smbdev->psmouse,
				    "Marking SMBus companion %s as gone\n",
				    dev_name(&smbdev->client->dev));
			smbdev->dead = true;
			serio_rescan(smbdev->psmouse->ps2dev.serio);
		} else {
			list_del(&smbdev->node);
			kfree(smbdev);
		}
	}

	mutex_unlock(&psmouse_smbus_mutex);
}

static int psmouse_smbus_notifier_call(struct notifier_block *nb,
				       unsigned long action, void *data)
{
	struct device *dev = data;

	switch (action) {
	case BUS_NOTIFY_ADD_DEVICE:
		if (dev->type == &i2c_adapter_type)
			psmouse_smbus_check_adapter(to_i2c_adapter(dev));
		break;

	case BUS_NOTIFY_REMOVED_DEVICE:
		if (dev->type != &i2c_adapter_type)
			psmouse_smbus_detach_i2c_client(to_i2c_client(dev));
		break;
	}

	return 0;
}

static struct notifier_block psmouse_smbus_notifier = {
	.notifier_call = psmouse_smbus_notifier_call,
};

static psmouse_ret_t psmouse_smbus_process_byte(struct psmouse *psmouse)
{
	return PSMOUSE_FULL_PACKET;
}

static int psmouse_smbus_reconnect(struct psmouse *psmouse)
{
	psmouse_deactivate(psmouse);

	return 0;
}

struct psmouse_smbus_removal_work {
	struct work_struct work;
	struct i2c_client *client;
};

static void psmouse_smbus_remove_i2c_device(struct work_struct *work)
{
	struct psmouse_smbus_removal_work *rwork =
		container_of(work, struct psmouse_smbus_removal_work, work);

	dev_dbg(&rwork->client->dev, "destroying SMBus companion device\n");
	i2c_unregister_device(rwork->client);

	kfree(rwork);
}

/*
 * This schedules removal of SMBus companion device. We have to do
 * it in a separate tread to avoid deadlocking on psmouse_mutex in
 * case the device has a trackstick (which is also driven by psmouse).
 *
 * Note that this may be racing with i2c adapter removal, but we
 * can't do anything about that: i2c automatically destroys clients
 * attached to an adapter that is being removed. This has to be
 * fixed in i2c core.
 */
static void psmouse_smbus_schedule_remove(struct i2c_client *client)
{
	struct psmouse_smbus_removal_work *rwork;

	rwork = kzalloc(sizeof(*rwork), GFP_KERNEL);
	if (rwork) {
		INIT_WORK(&rwork->work, psmouse_smbus_remove_i2c_device);
		rwork->client = client;

		schedule_work(&rwork->work);
	}
}

static void psmouse_smbus_disconnect(struct psmouse *psmouse)
{
	struct psmouse_smbus_dev *smbdev = psmouse->private;

	mutex_lock(&psmouse_smbus_mutex);

	if (smbdev->dead) {
		list_del(&smbdev->node);
		kfree(smbdev);
	} else {
		smbdev->dead = true;
		psmouse_dbg(smbdev->psmouse,
			    "posting removal request for SMBus companion %s\n",
			    dev_name(&smbdev->client->dev));
		psmouse_smbus_schedule_remove(smbdev->client);
	}

	mutex_unlock(&psmouse_smbus_mutex);

	psmouse->private = NULL;
}

static int psmouse_smbus_create_companion(struct device *dev, void *data)
{
	struct psmouse_smbus_dev *smbdev = data;
	unsigned short addr_list[] = { smbdev->board.addr, I2C_CLIENT_END };
	struct i2c_adapter *adapter;

	adapter = i2c_verify_adapter(dev);
	if (!adapter)
		return 0;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_HOST_NOTIFY))
		return 0;

	smbdev->client = i2c_new_probed_device(adapter, &smbdev->board,
					       addr_list, NULL);
	if (!smbdev->client)
		return 0;

	/* We have our(?) device, stop iterating i2c bus. */
	return 1;
}

void psmouse_smbus_cleanup(struct psmouse *psmouse)
{
	struct psmouse_smbus_dev *smbdev, *tmp;

	mutex_lock(&psmouse_smbus_mutex);

	list_for_each_entry_safe(smbdev, tmp, &psmouse_smbus_list, node) {
		if (psmouse == smbdev->psmouse) {
			list_del(&smbdev->node);
			kfree(smbdev);
		}
	}

	mutex_unlock(&psmouse_smbus_mutex);
}

int psmouse_smbus_init(struct psmouse *psmouse,
		       const struct i2c_board_info *board,
		       const void *pdata, size_t pdata_size,
		       bool leave_breadcrumbs)
{
	struct psmouse_smbus_dev *smbdev;
	int error;

	smbdev = kzalloc(sizeof(*smbdev), GFP_KERNEL);
	if (!smbdev)
		return -ENOMEM;

	smbdev->psmouse = psmouse;
	smbdev->board = *board;

	smbdev->board.platform_data = kmemdup(pdata, pdata_size, GFP_KERNEL);
	if (!smbdev->board.platform_data) {
		kfree(smbdev);
		return -ENOMEM;
	}

	psmouse->private = smbdev;
	psmouse->protocol_handler = psmouse_smbus_process_byte;
	psmouse->reconnect = psmouse_smbus_reconnect;
	psmouse->fast_reconnect = psmouse_smbus_reconnect;
	psmouse->disconnect = psmouse_smbus_disconnect;
	psmouse->resync_time = 0;

	psmouse_deactivate(psmouse);

	mutex_lock(&psmouse_smbus_mutex);
	list_add_tail(&smbdev->node, &psmouse_smbus_list);
	mutex_unlock(&psmouse_smbus_mutex);

	/* Bind to already existing adapters right away */
	error = i2c_for_each_dev(smbdev, psmouse_smbus_create_companion);

	if (smbdev->client) {
		/* We have our companion device */
		return 0;
	}

	/*
	 * If we did not create i2c device we will not need platform
	 * data even if we are leaving breadcrumbs.
	 */
	kfree(smbdev->board.platform_data);
	smbdev->board.platform_data = NULL;

	if (error < 0 || !leave_breadcrumbs) {
		mutex_lock(&psmouse_smbus_mutex);
		list_del(&smbdev->node);
		mutex_unlock(&psmouse_smbus_mutex);

		kfree(smbdev);
	}

	return error < 0 ? error : -EAGAIN;
}

int __init psmouse_smbus_module_init(void)
{
	int error;

	error = bus_register_notifier(&i2c_bus_type, &psmouse_smbus_notifier);
	if (error) {
		pr_err("failed to register i2c bus notifier: %d\n", error);
		return error;
	}

	return 0;
}

void psmouse_smbus_module_exit(void)
{
	bus_unregister_notifier(&i2c_bus_type, &psmouse_smbus_notifier);
	flush_scheduled_work();
}
