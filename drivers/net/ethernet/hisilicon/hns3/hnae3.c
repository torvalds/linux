/*
 * Copyright (c) 2016-2017 Hisilicon Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/list.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include "hnae3.h"

static LIST_HEAD(hnae3_ae_algo_list);
static LIST_HEAD(hnae3_client_list);
static LIST_HEAD(hnae3_ae_dev_list);

/* we are keeping things simple and using single lock for all the
 * list. This is a non-critical code so other updations, if happen
 * in parallel, can wait.
 */
static DEFINE_MUTEX(hnae3_common_lock);

static bool hnae3_client_match(enum hnae3_client_type client_type,
			       enum hnae3_dev_type dev_type)
{
	if ((dev_type == HNAE3_DEV_KNIC) && (client_type == HNAE3_CLIENT_KNIC ||
					     client_type == HNAE3_CLIENT_ROCE))
		return true;

	if (dev_type == HNAE3_DEV_UNIC && client_type == HNAE3_CLIENT_UNIC)
		return true;

	return false;
}

static int hnae3_match_n_instantiate(struct hnae3_client *client,
				     struct hnae3_ae_dev *ae_dev, bool is_reg)
{
	int ret;

	/* check if this client matches the type of ae_dev */
	if (!(hnae3_client_match(client->type, ae_dev->dev_type) &&
	      hnae_get_bit(ae_dev->flag, HNAE3_DEV_INITED_B))) {
		return 0;
	}

	/* now, (un-)instantiate client by calling lower layer */
	if (is_reg) {
		ret = ae_dev->ops->init_client_instance(client, ae_dev);
		if (ret) {
			dev_err(&ae_dev->pdev->dev,
				"fail to instantiate client\n");
			return ret;
		}

		hnae_set_bit(ae_dev->flag, HNAE3_CLIENT_INITED_B, 1);
		return 0;
	}

	if (hnae_get_bit(ae_dev->flag, HNAE3_CLIENT_INITED_B)) {
		ae_dev->ops->uninit_client_instance(client, ae_dev);

		hnae_set_bit(ae_dev->flag, HNAE3_CLIENT_INITED_B, 0);
	}

	return 0;
}

int hnae3_register_client(struct hnae3_client *client)
{
	struct hnae3_client *client_tmp;
	struct hnae3_ae_dev *ae_dev;
	int ret = 0;

	mutex_lock(&hnae3_common_lock);
	/* one system should only have one client for every type */
	list_for_each_entry(client_tmp, &hnae3_client_list, node) {
		if (client_tmp->type == client->type)
			goto exit;
	}

	list_add_tail(&client->node, &hnae3_client_list);

	/* initialize the client on every matched port */
	list_for_each_entry(ae_dev, &hnae3_ae_dev_list, node) {
		/* if the client could not be initialized on current port, for
		 * any error reasons, move on to next available port
		 */
		ret = hnae3_match_n_instantiate(client, ae_dev, true);
		if (ret)
			dev_err(&ae_dev->pdev->dev,
				"match and instantiation failed for port\n");
	}

exit:
	mutex_unlock(&hnae3_common_lock);

	return 0;
}
EXPORT_SYMBOL(hnae3_register_client);

void hnae3_unregister_client(struct hnae3_client *client)
{
	struct hnae3_ae_dev *ae_dev;

	mutex_lock(&hnae3_common_lock);
	/* un-initialize the client on every matched port */
	list_for_each_entry(ae_dev, &hnae3_ae_dev_list, node) {
		hnae3_match_n_instantiate(client, ae_dev, false);
	}

	list_del(&client->node);
	mutex_unlock(&hnae3_common_lock);
}
EXPORT_SYMBOL(hnae3_unregister_client);

/* hnae3_register_ae_algo - register a AE algorithm to hnae3 framework
 * @ae_algo: AE algorithm
 * NOTE: the duplicated name will not be checked
 */
void hnae3_register_ae_algo(struct hnae3_ae_algo *ae_algo)
{
	const struct pci_device_id *id;
	struct hnae3_ae_dev *ae_dev;
	struct hnae3_client *client;
	int ret = 0;

	mutex_lock(&hnae3_common_lock);

	list_add_tail(&ae_algo->node, &hnae3_ae_algo_list);

	/* Check if this algo/ops matches the list of ae_devs */
	list_for_each_entry(ae_dev, &hnae3_ae_dev_list, node) {
		id = pci_match_id(ae_algo->pdev_id_table, ae_dev->pdev);
		if (!id)
			continue;

		/* ae_dev init should set flag */
		ae_dev->ops = ae_algo->ops;
		ret = ae_algo->ops->init_ae_dev(ae_dev);
		if (ret) {
			dev_err(&ae_dev->pdev->dev, "init ae_dev error.\n");
			continue;
		}

		hnae_set_bit(ae_dev->flag, HNAE3_DEV_INITED_B, 1);

		/* check the client list for the match with this ae_dev type and
		 * initialize the figure out client instance
		 */
		list_for_each_entry(client, &hnae3_client_list, node) {
			ret = hnae3_match_n_instantiate(client, ae_dev, true);
			if (ret)
				dev_err(&ae_dev->pdev->dev,
					"match and instantiation failed\n");
		}
	}

	mutex_unlock(&hnae3_common_lock);
}
EXPORT_SYMBOL(hnae3_register_ae_algo);

/* hnae3_unregister_ae_algo - unregisters a AE algorithm
 * @ae_algo: the AE algorithm to unregister
 */
void hnae3_unregister_ae_algo(struct hnae3_ae_algo *ae_algo)
{
	const struct pci_device_id *id;
	struct hnae3_ae_dev *ae_dev;
	struct hnae3_client *client;

	mutex_lock(&hnae3_common_lock);
	/* Check if there are matched ae_dev */
	list_for_each_entry(ae_dev, &hnae3_ae_dev_list, node) {
		if (!hnae_get_bit(ae_dev->flag, HNAE3_DEV_INITED_B))
			continue;

		id = pci_match_id(ae_algo->pdev_id_table, ae_dev->pdev);
		if (!id)
			continue;

		/* check the client list for the match with this ae_dev type and
		 * un-initialize the figure out client instance
		 */
		list_for_each_entry(client, &hnae3_client_list, node)
			hnae3_match_n_instantiate(client, ae_dev, false);

		ae_algo->ops->uninit_ae_dev(ae_dev);
		hnae_set_bit(ae_dev->flag, HNAE3_DEV_INITED_B, 0);
	}

	list_del(&ae_algo->node);
	mutex_unlock(&hnae3_common_lock);
}
EXPORT_SYMBOL(hnae3_unregister_ae_algo);

/* hnae3_register_ae_dev - registers a AE device to hnae3 framework
 * @ae_dev: the AE device
 * NOTE: the duplicated name will not be checked
 */
void hnae3_register_ae_dev(struct hnae3_ae_dev *ae_dev)
{
	const struct pci_device_id *id;
	struct hnae3_ae_algo *ae_algo;
	struct hnae3_client *client;
	int ret = 0;

	mutex_lock(&hnae3_common_lock);

	list_add_tail(&ae_dev->node, &hnae3_ae_dev_list);

	/* Check if there are matched ae_algo */
	list_for_each_entry(ae_algo, &hnae3_ae_algo_list, node) {
		id = pci_match_id(ae_algo->pdev_id_table, ae_dev->pdev);
		if (!id)
			continue;

		ae_dev->ops = ae_algo->ops;

		if (!ae_dev->ops) {
			dev_err(&ae_dev->pdev->dev, "ae_dev ops are null\n");
			goto out_err;
		}

		/* ae_dev init should set flag */
		ret = ae_dev->ops->init_ae_dev(ae_dev);
		if (ret) {
			dev_err(&ae_dev->pdev->dev, "init ae_dev error\n");
			goto out_err;
		}

		hnae_set_bit(ae_dev->flag, HNAE3_DEV_INITED_B, 1);
		break;
	}

	/* check the client list for the match with this ae_dev type and
	 * initialize the figure out client instance
	 */
	list_for_each_entry(client, &hnae3_client_list, node) {
		ret = hnae3_match_n_instantiate(client, ae_dev, true);
		if (ret)
			dev_err(&ae_dev->pdev->dev,
				"match and instantiation failed\n");
	}

out_err:
	mutex_unlock(&hnae3_common_lock);
}
EXPORT_SYMBOL(hnae3_register_ae_dev);

/* hnae3_unregister_ae_dev - unregisters a AE device
 * @ae_dev: the AE device to unregister
 */
void hnae3_unregister_ae_dev(struct hnae3_ae_dev *ae_dev)
{
	const struct pci_device_id *id;
	struct hnae3_ae_algo *ae_algo;
	struct hnae3_client *client;

	mutex_lock(&hnae3_common_lock);
	/* Check if there are matched ae_algo */
	list_for_each_entry(ae_algo, &hnae3_ae_algo_list, node) {
		if (!hnae_get_bit(ae_dev->flag, HNAE3_DEV_INITED_B))
			continue;

		id = pci_match_id(ae_algo->pdev_id_table, ae_dev->pdev);
		if (!id)
			continue;

		list_for_each_entry(client, &hnae3_client_list, node)
			hnae3_match_n_instantiate(client, ae_dev, false);

		ae_algo->ops->uninit_ae_dev(ae_dev);
		hnae_set_bit(ae_dev->flag, HNAE3_DEV_INITED_B, 0);
	}

	list_del(&ae_dev->node);
	mutex_unlock(&hnae3_common_lock);
}
EXPORT_SYMBOL(hnae3_unregister_ae_dev);

MODULE_AUTHOR("Huawei Tech. Co., Ltd.");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("HNAE3(Hisilicon Network Acceleration Engine) Framework");
MODULE_VERSION(HNAE3_MOD_VERSION);
