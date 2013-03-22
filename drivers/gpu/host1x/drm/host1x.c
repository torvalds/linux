/*
 * Copyright (C) 2012 Avionic Design GmbH
 * Copyright (C) 2012 NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include "drm.h"

struct host1x_drm_client {
	struct host1x_client *client;
	struct device_node *np;
	struct list_head list;
};

static int host1x_add_drm_client(struct host1x_drm *host1x,
				 struct device_node *np)
{
	struct host1x_drm_client *client;

	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (!client)
		return -ENOMEM;

	INIT_LIST_HEAD(&client->list);
	client->np = of_node_get(np);

	list_add_tail(&client->list, &host1x->drm_clients);

	return 0;
}

static int host1x_activate_drm_client(struct host1x_drm *host1x,
				      struct host1x_drm_client *drm,
				      struct host1x_client *client)
{
	mutex_lock(&host1x->drm_clients_lock);
	list_del_init(&drm->list);
	list_add_tail(&drm->list, &host1x->drm_active);
	drm->client = client;
	mutex_unlock(&host1x->drm_clients_lock);

	return 0;
}

static int host1x_remove_drm_client(struct host1x_drm *host1x,
				    struct host1x_drm_client *client)
{
	mutex_lock(&host1x->drm_clients_lock);
	list_del_init(&client->list);
	mutex_unlock(&host1x->drm_clients_lock);

	of_node_put(client->np);
	kfree(client);

	return 0;
}

static int host1x_parse_dt(struct host1x_drm *host1x)
{
	static const char * const compat[] = {
		"nvidia,tegra20-dc",
		"nvidia,tegra20-hdmi",
		"nvidia,tegra30-dc",
		"nvidia,tegra30-hdmi",
	};
	unsigned int i;
	int err;

	for (i = 0; i < ARRAY_SIZE(compat); i++) {
		struct device_node *np;

		for_each_child_of_node(host1x->dev->of_node, np) {
			if (of_device_is_compatible(np, compat[i]) &&
			    of_device_is_available(np)) {
				err = host1x_add_drm_client(host1x, np);
				if (err < 0)
					return err;
			}
		}
	}

	return 0;
}

static int tegra_host1x_probe(struct platform_device *pdev)
{
	struct host1x_drm *host1x;
	struct resource *regs;
	int err;

	host1x = devm_kzalloc(&pdev->dev, sizeof(*host1x), GFP_KERNEL);
	if (!host1x)
		return -ENOMEM;

	mutex_init(&host1x->drm_clients_lock);
	INIT_LIST_HEAD(&host1x->drm_clients);
	INIT_LIST_HEAD(&host1x->drm_active);
	mutex_init(&host1x->clients_lock);
	INIT_LIST_HEAD(&host1x->clients);
	host1x->dev = &pdev->dev;

	err = host1x_parse_dt(host1x);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to parse DT: %d\n", err);
		return err;
	}

	host1x->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(host1x->clk))
		return PTR_ERR(host1x->clk);

	err = clk_prepare_enable(host1x->clk);
	if (err < 0)
		return err;

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!regs) {
		err = -ENXIO;
		goto err;
	}

	err = platform_get_irq(pdev, 0);
	if (err < 0)
		goto err;

	host1x->syncpt = err;

	err = platform_get_irq(pdev, 1);
	if (err < 0)
		goto err;

	host1x->irq = err;

	host1x->regs = devm_ioremap_resource(&pdev->dev, regs);
	if (IS_ERR(host1x->regs)) {
		err = PTR_ERR(host1x->regs);
		goto err;
	}

	platform_set_drvdata(pdev, host1x);

	return 0;

err:
	clk_disable_unprepare(host1x->clk);
	return err;
}

static int tegra_host1x_remove(struct platform_device *pdev)
{
	struct host1x_drm *host1x = platform_get_drvdata(pdev);

	clk_disable_unprepare(host1x->clk);

	return 0;
}

int host1x_drm_init(struct host1x_drm *host1x, struct drm_device *drm)
{
	struct host1x_client *client;

	mutex_lock(&host1x->clients_lock);

	list_for_each_entry(client, &host1x->clients, list) {
		if (client->ops && client->ops->drm_init) {
			int err = client->ops->drm_init(client, drm);
			if (err < 0) {
				dev_err(host1x->dev,
					"DRM setup failed for %s: %d\n",
					dev_name(client->dev), err);
				return err;
			}
		}
	}

	mutex_unlock(&host1x->clients_lock);

	return 0;
}

int host1x_drm_exit(struct host1x_drm *host1x)
{
	struct platform_device *pdev = to_platform_device(host1x->dev);
	struct host1x_client *client;

	if (!host1x->drm)
		return 0;

	mutex_lock(&host1x->clients_lock);

	list_for_each_entry_reverse(client, &host1x->clients, list) {
		if (client->ops && client->ops->drm_exit) {
			int err = client->ops->drm_exit(client);
			if (err < 0) {
				dev_err(host1x->dev,
					"DRM cleanup failed for %s: %d\n",
					dev_name(client->dev), err);
				return err;
			}
		}
	}

	mutex_unlock(&host1x->clients_lock);

	drm_platform_exit(&tegra_drm_driver, pdev);
	host1x->drm = NULL;

	return 0;
}

int host1x_register_client(struct host1x_drm *host1x,
			   struct host1x_client *client)
{
	struct host1x_drm_client *drm, *tmp;
	int err;

	mutex_lock(&host1x->clients_lock);
	list_add_tail(&client->list, &host1x->clients);
	mutex_unlock(&host1x->clients_lock);

	list_for_each_entry_safe(drm, tmp, &host1x->drm_clients, list)
		if (drm->np == client->dev->of_node)
			host1x_activate_drm_client(host1x, drm, client);

	if (list_empty(&host1x->drm_clients)) {
		struct platform_device *pdev = to_platform_device(host1x->dev);

		err = drm_platform_init(&tegra_drm_driver, pdev);
		if (err < 0) {
			dev_err(host1x->dev, "drm_platform_init(): %d\n", err);
			return err;
		}
	}

	client->host1x = host1x;

	return 0;
}

int host1x_unregister_client(struct host1x_drm *host1x,
			     struct host1x_client *client)
{
	struct host1x_drm_client *drm, *tmp;
	int err;

	list_for_each_entry_safe(drm, tmp, &host1x->drm_active, list) {
		if (drm->client == client) {
			err = host1x_drm_exit(host1x);
			if (err < 0) {
				dev_err(host1x->dev, "host1x_drm_exit(): %d\n",
					err);
				return err;
			}

			host1x_remove_drm_client(host1x, drm);
			break;
		}
	}

	mutex_lock(&host1x->clients_lock);
	list_del_init(&client->list);
	mutex_unlock(&host1x->clients_lock);

	return 0;
}

static struct of_device_id tegra_host1x_of_match[] = {
	{ .compatible = "nvidia,tegra30-host1x", },
	{ .compatible = "nvidia,tegra20-host1x", },
	{ },
};
MODULE_DEVICE_TABLE(of, tegra_host1x_of_match);

struct platform_driver tegra_host1x_driver = {
	.driver = {
		.name = "tegra-host1x",
		.owner = THIS_MODULE,
		.of_match_table = tegra_host1x_of_match,
	},
	.probe = tegra_host1x_probe,
	.remove = tegra_host1x_remove,
};

static int __init tegra_host1x_init(void)
{
	int err;

	err = platform_driver_register(&tegra_host1x_driver);
	if (err < 0)
		return err;

	err = platform_driver_register(&tegra_dc_driver);
	if (err < 0)
		goto unregister_host1x;

	err = platform_driver_register(&tegra_hdmi_driver);
	if (err < 0)
		goto unregister_dc;

	return 0;

unregister_dc:
	platform_driver_unregister(&tegra_dc_driver);
unregister_host1x:
	platform_driver_unregister(&tegra_host1x_driver);
	return err;
}
module_init(tegra_host1x_init);

static void __exit tegra_host1x_exit(void)
{
	platform_driver_unregister(&tegra_hdmi_driver);
	platform_driver_unregister(&tegra_dc_driver);
	platform_driver_unregister(&tegra_host1x_driver);
}
module_exit(tegra_host1x_exit);

MODULE_AUTHOR("Thierry Reding <thierry.reding@avionic-design.de>");
MODULE_DESCRIPTION("NVIDIA Tegra DRM driver");
MODULE_LICENSE("GPL");
