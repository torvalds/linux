// SPDX-License-Identifier: GPL-2.0+
// Copyright (c) 2016-2017 Hisilicon Limited.

#include <linux/list.h>
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

static bool hnae3_client_match(enum hnae3_client_type client_type)
{
	if (client_type == HNAE3_CLIENT_KNIC ||
	    client_type == HNAE3_CLIENT_ROCE)
		return true;

	return false;
}

void hnae3_set_client_init_flag(struct hnae3_client *client,
				struct hnae3_ae_dev *ae_dev,
				unsigned int inited)
{
	if (!client || !ae_dev)
		return;

	switch (client->type) {
	case HNAE3_CLIENT_KNIC:
		hnae3_set_bit(ae_dev->flag, HNAE3_KNIC_CLIENT_INITED_B, inited);
		break;
	case HNAE3_CLIENT_ROCE:
		hnae3_set_bit(ae_dev->flag, HNAE3_ROCE_CLIENT_INITED_B, inited);
		break;
	default:
		break;
	}
}
EXPORT_SYMBOL(hnae3_set_client_init_flag);

static int hnae3_get_client_init_flag(struct hnae3_client *client,
				      struct hnae3_ae_dev *ae_dev)
{
	int inited = 0;

	switch (client->type) {
	case HNAE3_CLIENT_KNIC:
		inited = hnae3_get_bit(ae_dev->flag,
				       HNAE3_KNIC_CLIENT_INITED_B);
		break;
	case HNAE3_CLIENT_ROCE:
		inited = hnae3_get_bit(ae_dev->flag,
				       HNAE3_ROCE_CLIENT_INITED_B);
		break;
	default:
		break;
	}

	return inited;
}

static int hnae3_init_client_instance(struct hnae3_client *client,
				      struct hnae3_ae_dev *ae_dev)
{
	int ret;

	/* check if this client matches the type of ae_dev */
	if (!(hnae3_client_match(client->type) &&
	      hnae3_get_bit(ae_dev->flag, HNAE3_DEV_INITED_B))) {
		return 0;
	}

	ret = ae_dev->ops->init_client_instance(client, ae_dev);
	if (ret)
		dev_err(&ae_dev->pdev->dev,
			"fail to instantiate client, ret = %d\n", ret);

	return ret;
}

static void hnae3_uninit_client_instance(struct hnae3_client *client,
					 struct hnae3_ae_dev *ae_dev)
{
	/* check if this client matches the type of ae_dev */
	if (!(hnae3_client_match(client->type) &&
	      hnae3_get_bit(ae_dev->flag, HNAE3_DEV_INITED_B)))
		return;

	if (hnae3_get_client_init_flag(client, ae_dev)) {
		ae_dev->ops->uninit_client_instance(client, ae_dev);

		hnae3_set_client_init_flag(client, ae_dev, 0);
	}
}

int hnae3_register_client(struct hnae3_client *client)
{
	struct hnae3_client *client_tmp;
	struct hnae3_ae_dev *ae_dev;

	if (!client)
		return -ENODEV;

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
		int ret = hnae3_init_client_instance(client, ae_dev);
		if (ret)
			dev_err(&ae_dev->pdev->dev,
				"match and instantiation failed for port, ret = %d\n",
				ret);
	}

exit:
	mutex_unlock(&hnae3_common_lock);

	return 0;
}
EXPORT_SYMBOL(hnae3_register_client);

void hnae3_unregister_client(struct hnae3_client *client)
{
	struct hnae3_client *client_tmp;
	struct hnae3_ae_dev *ae_dev;
	bool existed = false;

	if (!client)
		return;

	mutex_lock(&hnae3_common_lock);
	/* one system should only have one client for every type */
	list_for_each_entry(client_tmp, &hnae3_client_list, node) {
		if (client_tmp->type == client->type) {
			existed = true;
			break;
		}
	}

	if (!existed) {
		mutex_unlock(&hnae3_common_lock);
		pr_err("client %s does not exist!\n", client->name);
		return;
	}

	/* un-initialize the client on every matched port */
	list_for_each_entry(ae_dev, &hnae3_ae_dev_list, node) {
		hnae3_uninit_client_instance(client, ae_dev);
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
	int ret;

	if (!ae_algo)
		return;

	mutex_lock(&hnae3_common_lock);

	list_add_tail(&ae_algo->node, &hnae3_ae_algo_list);

	/* Check if this algo/ops matches the list of ae_devs */
	list_for_each_entry(ae_dev, &hnae3_ae_dev_list, node) {
		id = pci_match_id(ae_algo->pdev_id_table, ae_dev->pdev);
		if (!id)
			continue;

		if (!ae_algo->ops) {
			dev_err(&ae_dev->pdev->dev, "ae_algo ops are null\n");
			continue;
		}
		ae_dev->ops = ae_algo->ops;

		ret = ae_algo->ops->init_ae_dev(ae_dev);
		if (ret) {
			dev_err(&ae_dev->pdev->dev,
				"init ae_dev error, ret = %d\n", ret);
			continue;
		}

		/* ae_dev init should set flag */
		hnae3_set_bit(ae_dev->flag, HNAE3_DEV_INITED_B, 1);

		/* check the client list for the match with this ae_dev type and
		 * initialize the figure out client instance
		 */
		list_for_each_entry(client, &hnae3_client_list, node) {
			ret = hnae3_init_client_instance(client, ae_dev);
			if (ret)
				dev_err(&ae_dev->pdev->dev,
					"match and instantiation failed, ret = %d\n",
					ret);
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

	if (!ae_algo)
		return;

	mutex_lock(&hnae3_common_lock);
	/* Check if there are matched ae_dev */
	list_for_each_entry(ae_dev, &hnae3_ae_dev_list, node) {
		if (!hnae3_get_bit(ae_dev->flag, HNAE3_DEV_INITED_B))
			continue;

		id = pci_match_id(ae_algo->pdev_id_table, ae_dev->pdev);
		if (!id)
			continue;

		/* check the client list for the match with this ae_dev type and
		 * un-initialize the figure out client instance
		 */
		list_for_each_entry(client, &hnae3_client_list, node)
			hnae3_uninit_client_instance(client, ae_dev);

		ae_algo->ops->uninit_ae_dev(ae_dev);
		hnae3_set_bit(ae_dev->flag, HNAE3_DEV_INITED_B, 0);
		ae_dev->ops = NULL;
	}

	list_del(&ae_algo->node);
	mutex_unlock(&hnae3_common_lock);
}
EXPORT_SYMBOL(hnae3_unregister_ae_algo);

/* hnae3_register_ae_dev - registers a AE device to hnae3 framework
 * @ae_dev: the AE device
 * NOTE: the duplicated name will not be checked
 */
int hnae3_register_ae_dev(struct hnae3_ae_dev *ae_dev)
{
	const struct pci_device_id *id;
	struct hnae3_ae_algo *ae_algo;
	struct hnae3_client *client;
	int ret;

	if (!ae_dev)
		return -ENODEV;

	mutex_lock(&hnae3_common_lock);

	list_add_tail(&ae_dev->node, &hnae3_ae_dev_list);

	/* Check if there are matched ae_algo */
	list_for_each_entry(ae_algo, &hnae3_ae_algo_list, node) {
		id = pci_match_id(ae_algo->pdev_id_table, ae_dev->pdev);
		if (!id)
			continue;

		if (!ae_algo->ops) {
			dev_err(&ae_dev->pdev->dev, "ae_algo ops are null\n");
			ret = -EOPNOTSUPP;
			goto out_err;
		}
		ae_dev->ops = ae_algo->ops;

		ret = ae_dev->ops->init_ae_dev(ae_dev);
		if (ret) {
			dev_err(&ae_dev->pdev->dev,
				"init ae_dev error, ret = %d\n", ret);
			goto out_err;
		}

		/* ae_dev init should set flag */
		hnae3_set_bit(ae_dev->flag, HNAE3_DEV_INITED_B, 1);
		break;
	}

	/* check the client list for the match with this ae_dev type and
	 * initialize the figure out client instance
	 */
	list_for_each_entry(client, &hnae3_client_list, node) {
		ret = hnae3_init_client_instance(client, ae_dev);
		if (ret)
			dev_err(&ae_dev->pdev->dev,
				"match and instantiation failed, ret = %d\n",
				ret);
	}

	mutex_unlock(&hnae3_common_lock);

	return 0;

out_err:
	list_del(&ae_dev->node);
	mutex_unlock(&hnae3_common_lock);

	return ret;
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

	if (!ae_dev)
		return;

	mutex_lock(&hnae3_common_lock);
	/* Check if there are matched ae_algo */
	list_for_each_entry(ae_algo, &hnae3_ae_algo_list, node) {
		if (!hnae3_get_bit(ae_dev->flag, HNAE3_DEV_INITED_B))
			continue;

		id = pci_match_id(ae_algo->pdev_id_table, ae_dev->pdev);
		if (!id)
			continue;

		list_for_each_entry(client, &hnae3_client_list, node)
			hnae3_uninit_client_instance(client, ae_dev);

		ae_algo->ops->uninit_ae_dev(ae_dev);
		hnae3_set_bit(ae_dev->flag, HNAE3_DEV_INITED_B, 0);
		ae_dev->ops = NULL;
	}

	list_del(&ae_dev->node);
	mutex_unlock(&hnae3_common_lock);
}
EXPORT_SYMBOL(hnae3_unregister_ae_dev);

MODULE_AUTHOR("Huawei Tech. Co., Ltd.");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("HNAE3(Hisilicon Network Acceleration Engine) Framework");
MODULE_VERSION(HNAE3_MOD_VERSION);
