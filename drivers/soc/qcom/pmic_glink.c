// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2019-2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022, Linaro Ltd
 */
#include <linux/auxiliary_bus.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/rpmsg.h>
#include <linux/slab.h>
#include <linux/soc/qcom/pdr.h>
#include <linux/soc/qcom/pmic_glink.h>
#include <linux/spinlock.h>

#define PMIC_GLINK_SEND_TIMEOUT (5 * HZ)

enum {
	PMIC_GLINK_CLIENT_BATT = 0,
	PMIC_GLINK_CLIENT_ALTMODE,
	PMIC_GLINK_CLIENT_UCSI,
};

struct pmic_glink {
	struct device *dev;
	struct pdr_handle *pdr;

	struct rpmsg_endpoint *ept;

	unsigned long client_mask;

	struct auxiliary_device altmode_aux;
	struct auxiliary_device ps_aux;
	struct auxiliary_device ucsi_aux;

	/* serializing client_state and pdr_state updates */
	struct mutex state_lock;
	unsigned int client_state;
	unsigned int pdr_state;

	/* serializing clients list updates */
	spinlock_t client_lock;
	struct list_head clients;
};

static struct pmic_glink *__pmic_glink;
static DEFINE_MUTEX(__pmic_glink_lock);

struct pmic_glink_client {
	struct list_head node;

	struct pmic_glink *pg;
	unsigned int id;

	void (*cb)(const void *data, size_t len, void *priv);
	void (*pdr_notify)(void *priv, int state);
	void *priv;
};

static void _devm_pmic_glink_release_client(struct device *dev, void *res)
{
	struct pmic_glink_client *client = (struct pmic_glink_client *)res;
	struct pmic_glink *pg = client->pg;
	unsigned long flags;

	spin_lock_irqsave(&pg->client_lock, flags);
	list_del(&client->node);
	spin_unlock_irqrestore(&pg->client_lock, flags);
}

struct pmic_glink_client *devm_pmic_glink_client_alloc(struct device *dev,
						       unsigned int id,
						       void (*cb)(const void *, size_t, void *),
						       void (*pdr)(void *, int),
						       void *priv)
{
	struct pmic_glink_client *client;
	struct pmic_glink *pg = dev_get_drvdata(dev->parent);

	client = devres_alloc(_devm_pmic_glink_release_client, sizeof(*client), GFP_KERNEL);
	if (!client)
		return ERR_PTR(-ENOMEM);

	client->pg = pg;
	client->id = id;
	client->cb = cb;
	client->pdr_notify = pdr;
	client->priv = priv;
	INIT_LIST_HEAD(&client->node);

	devres_add(dev, client);

	return client;
}
EXPORT_SYMBOL_GPL(devm_pmic_glink_client_alloc);

void pmic_glink_client_register(struct pmic_glink_client *client)
{
	struct pmic_glink *pg = client->pg;
	unsigned long flags;

	mutex_lock(&pg->state_lock);
	spin_lock_irqsave(&pg->client_lock, flags);

	list_add(&client->node, &pg->clients);
	client->pdr_notify(client->priv, pg->client_state);

	spin_unlock_irqrestore(&pg->client_lock, flags);
	mutex_unlock(&pg->state_lock);

}
EXPORT_SYMBOL_GPL(pmic_glink_client_register);

int pmic_glink_send(struct pmic_glink_client *client, void *data, size_t len)
{
	struct pmic_glink *pg = client->pg;
	bool timeout_reached = false;
	unsigned long start;
	int ret;

	mutex_lock(&pg->state_lock);
	if (!pg->ept) {
		ret = -ECONNRESET;
	} else {
		start = jiffies;
		for (;;) {
			ret = rpmsg_send(pg->ept, data, len);
			if (ret != -EAGAIN)
				break;

			if (timeout_reached) {
				ret = -ETIMEDOUT;
				break;
			}

			usleep_range(1000, 5000);
			timeout_reached = time_after(jiffies, start + PMIC_GLINK_SEND_TIMEOUT);
		}
	}
	mutex_unlock(&pg->state_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(pmic_glink_send);

static int pmic_glink_rpmsg_callback(struct rpmsg_device *rpdev, void *data,
				     int len, void *priv, u32 addr)
{
	struct pmic_glink_client *client;
	struct pmic_glink_hdr *hdr;
	struct pmic_glink *pg = dev_get_drvdata(&rpdev->dev);
	unsigned long flags;

	if (len < sizeof(*hdr)) {
		dev_warn(pg->dev, "ignoring truncated message\n");
		return 0;
	}

	hdr = data;

	spin_lock_irqsave(&pg->client_lock, flags);
	list_for_each_entry(client, &pg->clients, node) {
		if (client->id == le32_to_cpu(hdr->owner))
			client->cb(data, len, client->priv);
	}
	spin_unlock_irqrestore(&pg->client_lock, flags);

	return 0;
}

static void pmic_glink_aux_release(struct device *dev) {}

static int pmic_glink_add_aux_device(struct pmic_glink *pg,
				     struct auxiliary_device *aux,
				     const char *name)
{
	struct device *parent = pg->dev;
	int ret;

	aux->name = name;
	aux->dev.parent = parent;
	aux->dev.release = pmic_glink_aux_release;
	device_set_of_node_from_dev(&aux->dev, parent);
	ret = auxiliary_device_init(aux);
	if (ret)
		return ret;

	ret = auxiliary_device_add(aux);
	if (ret)
		auxiliary_device_uninit(aux);

	return ret;
}

static void pmic_glink_del_aux_device(struct pmic_glink *pg,
				      struct auxiliary_device *aux)
{
	auxiliary_device_delete(aux);
	auxiliary_device_uninit(aux);
}

static void pmic_glink_state_notify_clients(struct pmic_glink *pg)
{
	struct pmic_glink_client *client;
	unsigned int new_state = pg->client_state;
	unsigned long flags;

	if (pg->client_state != SERVREG_SERVICE_STATE_UP) {
		if (pg->pdr_state == SERVREG_SERVICE_STATE_UP && pg->ept)
			new_state = SERVREG_SERVICE_STATE_UP;
	} else {
		if (pg->pdr_state == SERVREG_SERVICE_STATE_DOWN || !pg->ept)
			new_state = SERVREG_SERVICE_STATE_DOWN;
	}

	if (new_state != pg->client_state) {
		spin_lock_irqsave(&pg->client_lock, flags);
		list_for_each_entry(client, &pg->clients, node)
			client->pdr_notify(client->priv, new_state);
		spin_unlock_irqrestore(&pg->client_lock, flags);
		pg->client_state = new_state;
	}
}

static void pmic_glink_pdr_callback(int state, char *svc_path, void *priv)
{
	struct pmic_glink *pg = priv;

	mutex_lock(&pg->state_lock);
	pg->pdr_state = state;

	pmic_glink_state_notify_clients(pg);
	mutex_unlock(&pg->state_lock);
}

static int pmic_glink_rpmsg_probe(struct rpmsg_device *rpdev)
{
	struct pmic_glink *pg = __pmic_glink;
	int ret = 0;

	mutex_lock(&__pmic_glink_lock);
	if (!pg) {
		ret = dev_err_probe(&rpdev->dev, -ENODEV, "no pmic_glink device to attach to\n");
		goto out_unlock;
	}

	dev_set_drvdata(&rpdev->dev, pg);

	mutex_lock(&pg->state_lock);
	pg->ept = rpdev->ept;
	pmic_glink_state_notify_clients(pg);
	mutex_unlock(&pg->state_lock);

out_unlock:
	mutex_unlock(&__pmic_glink_lock);
	return ret;
}

static void pmic_glink_rpmsg_remove(struct rpmsg_device *rpdev)
{
	struct pmic_glink *pg;

	mutex_lock(&__pmic_glink_lock);
	pg = __pmic_glink;
	if (!pg)
		goto out_unlock;

	mutex_lock(&pg->state_lock);
	pg->ept = NULL;
	pmic_glink_state_notify_clients(pg);
	mutex_unlock(&pg->state_lock);
out_unlock:
	mutex_unlock(&__pmic_glink_lock);
}

static const struct rpmsg_device_id pmic_glink_rpmsg_id_match[] = {
	{ "PMIC_RTR_ADSP_APPS" },
	{}
};

static struct rpmsg_driver pmic_glink_rpmsg_driver = {
	.probe = pmic_glink_rpmsg_probe,
	.remove = pmic_glink_rpmsg_remove,
	.callback = pmic_glink_rpmsg_callback,
	.id_table = pmic_glink_rpmsg_id_match,
	.drv  = {
		.name  = "qcom_pmic_glink_rpmsg",
	},
};

static int pmic_glink_probe(struct platform_device *pdev)
{
	const unsigned long *match_data;
	struct pdr_service *service;
	struct pmic_glink *pg;
	int ret;

	pg = devm_kzalloc(&pdev->dev, sizeof(*pg), GFP_KERNEL);
	if (!pg)
		return -ENOMEM;

	dev_set_drvdata(&pdev->dev, pg);

	pg->dev = &pdev->dev;

	INIT_LIST_HEAD(&pg->clients);
	spin_lock_init(&pg->client_lock);
	mutex_init(&pg->state_lock);

	match_data = (unsigned long *)of_device_get_match_data(&pdev->dev);
	if (!match_data)
		return -EINVAL;

	pg->client_mask = *match_data;

	pg->pdr = pdr_handle_alloc(pmic_glink_pdr_callback, pg);
	if (IS_ERR(pg->pdr)) {
		ret = dev_err_probe(&pdev->dev, PTR_ERR(pg->pdr),
				    "failed to initialize pdr\n");
		return ret;
	}

	if (pg->client_mask & BIT(PMIC_GLINK_CLIENT_UCSI)) {
		ret = pmic_glink_add_aux_device(pg, &pg->ucsi_aux, "ucsi");
		if (ret)
			goto out_release_pdr_handle;
	}
	if (pg->client_mask & BIT(PMIC_GLINK_CLIENT_ALTMODE)) {
		ret = pmic_glink_add_aux_device(pg, &pg->altmode_aux, "altmode");
		if (ret)
			goto out_release_ucsi_aux;
	}
	if (pg->client_mask & BIT(PMIC_GLINK_CLIENT_BATT)) {
		ret = pmic_glink_add_aux_device(pg, &pg->ps_aux, "power-supply");
		if (ret)
			goto out_release_altmode_aux;
	}

	service = pdr_add_lookup(pg->pdr, "tms/servreg", "msm/adsp/charger_pd");
	if (IS_ERR(service)) {
		ret = dev_err_probe(&pdev->dev, PTR_ERR(service),
				    "failed adding pdr lookup for charger_pd\n");
		goto out_release_aux_devices;
	}

	mutex_lock(&__pmic_glink_lock);
	__pmic_glink = pg;
	mutex_unlock(&__pmic_glink_lock);

	return 0;

out_release_aux_devices:
	if (pg->client_mask & BIT(PMIC_GLINK_CLIENT_BATT))
		pmic_glink_del_aux_device(pg, &pg->ps_aux);
out_release_altmode_aux:
	if (pg->client_mask & BIT(PMIC_GLINK_CLIENT_ALTMODE))
		pmic_glink_del_aux_device(pg, &pg->altmode_aux);
out_release_ucsi_aux:
	if (pg->client_mask & BIT(PMIC_GLINK_CLIENT_UCSI))
		pmic_glink_del_aux_device(pg, &pg->ucsi_aux);
out_release_pdr_handle:
	pdr_handle_release(pg->pdr);

	return ret;
}

static void pmic_glink_remove(struct platform_device *pdev)
{
	struct pmic_glink *pg = dev_get_drvdata(&pdev->dev);

	pdr_handle_release(pg->pdr);

	if (pg->client_mask & BIT(PMIC_GLINK_CLIENT_BATT))
		pmic_glink_del_aux_device(pg, &pg->ps_aux);
	if (pg->client_mask & BIT(PMIC_GLINK_CLIENT_ALTMODE))
		pmic_glink_del_aux_device(pg, &pg->altmode_aux);
	if (pg->client_mask & BIT(PMIC_GLINK_CLIENT_UCSI))
		pmic_glink_del_aux_device(pg, &pg->ucsi_aux);

	mutex_lock(&__pmic_glink_lock);
	__pmic_glink = NULL;
	mutex_unlock(&__pmic_glink_lock);
}

static const unsigned long pmic_glink_sc8280xp_client_mask = BIT(PMIC_GLINK_CLIENT_BATT) |
							     BIT(PMIC_GLINK_CLIENT_ALTMODE);

static const unsigned long pmic_glink_sm8450_client_mask = BIT(PMIC_GLINK_CLIENT_BATT) |
							   BIT(PMIC_GLINK_CLIENT_ALTMODE) |
							   BIT(PMIC_GLINK_CLIENT_UCSI);

static const struct of_device_id pmic_glink_of_match[] = {
	{ .compatible = "qcom,sc8280xp-pmic-glink", .data = &pmic_glink_sc8280xp_client_mask },
	{ .compatible = "qcom,pmic-glink", .data = &pmic_glink_sm8450_client_mask },
	{}
};
MODULE_DEVICE_TABLE(of, pmic_glink_of_match);

static struct platform_driver pmic_glink_driver = {
	.probe = pmic_glink_probe,
	.remove_new = pmic_glink_remove,
	.driver = {
		.name = "qcom_pmic_glink",
		.of_match_table = pmic_glink_of_match,
	},
};

static int pmic_glink_init(void)
{
	int ret;

	ret = platform_driver_register(&pmic_glink_driver);
	if (ret < 0)
		return ret;

	ret = register_rpmsg_driver(&pmic_glink_rpmsg_driver);
	if (ret < 0) {
		platform_driver_unregister(&pmic_glink_driver);
		return ret;
	}

	return 0;
}
module_init(pmic_glink_init);

static void pmic_glink_exit(void)
{
	unregister_rpmsg_driver(&pmic_glink_rpmsg_driver);
	platform_driver_unregister(&pmic_glink_driver);
}
module_exit(pmic_glink_exit);

MODULE_DESCRIPTION("Qualcomm PMIC GLINK driver");
MODULE_LICENSE("GPL");
