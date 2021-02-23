// SPDX-License-Identifier: GPL-2.0
/*
 * HMS Profinet Client Driver
 *
 * Copyright (C) 2018 Arcx Inc
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>

/* move to <linux/fieldbus_dev.h> when taking this out of staging */
#include "../fieldbus_dev.h"

/* move to <linux/anybuss-client.h> when taking this out of staging */
#include "anybuss-client.h"

#define PROFI_DPRAM_SIZE	512

/*
 * ---------------------------------------------------------------
 * Anybus Profinet mailbox messages - definitions
 * ---------------------------------------------------------------
 * note that we're depending on the layout of these structures being
 * exactly as advertised.
 */

struct msg_mac_addr {
	u8 addr[6];
};

struct profi_priv {
	struct fieldbus_dev fbdev;
	struct anybuss_client *client;
	struct mutex enable_lock; /* serializes card enable */
	bool power_on;
};

static ssize_t
profi_read_area(struct fieldbus_dev *fbdev, char __user *buf, size_t size,
		loff_t *offset)
{
	struct profi_priv *priv = container_of(fbdev, struct profi_priv, fbdev);

	return anybuss_read_output(priv->client, buf, size, offset);
}

static ssize_t
profi_write_area(struct fieldbus_dev *fbdev, const char __user *buf,
		 size_t size, loff_t *offset)
{
	struct profi_priv *priv = container_of(fbdev, struct profi_priv, fbdev);

	return anybuss_write_input(priv->client, buf, size, offset);
}

static int profi_id_get(struct fieldbus_dev *fbdev, char *buf,
			size_t max_size)
{
	struct profi_priv *priv = container_of(fbdev, struct profi_priv, fbdev);
	struct msg_mac_addr response;
	int ret;

	ret = anybuss_recv_msg(priv->client, 0x0010, &response,
			       sizeof(response));
	if (ret < 0)
		return ret;
	return snprintf(buf, max_size, "%pM\n", response.addr);
}

static bool profi_enable_get(struct fieldbus_dev *fbdev)
{
	struct profi_priv *priv = container_of(fbdev, struct profi_priv, fbdev);
	bool power_on;

	mutex_lock(&priv->enable_lock);
	power_on = priv->power_on;
	mutex_unlock(&priv->enable_lock);

	return power_on;
}

static int __profi_enable(struct profi_priv *priv)
{
	int ret;
	struct anybuss_client *client = priv->client;
	/* Initialization Sequence, Generic Anybus Mode */
	const struct anybuss_memcfg mem_cfg = {
		.input_io = 220,
		.input_dpram = PROFI_DPRAM_SIZE,
		.input_total = PROFI_DPRAM_SIZE,
		.output_io = 220,
		.output_dpram = PROFI_DPRAM_SIZE,
		.output_total = PROFI_DPRAM_SIZE,
		.offl_mode = FIELDBUS_DEV_OFFL_MODE_CLEAR,
	};

	/*
	 * switch anybus off then on, this ensures we can do a complete
	 * configuration cycle in case anybus was already on.
	 */
	anybuss_set_power(client, false);
	ret = anybuss_set_power(client, true);
	if (ret)
		goto err;
	ret = anybuss_start_init(client, &mem_cfg);
	if (ret)
		goto err;
	ret = anybuss_finish_init(client);
	if (ret)
		goto err;
	priv->power_on = true;
	return 0;

err:
	anybuss_set_power(client, false);
	priv->power_on = false;
	return ret;
}

static int __profi_disable(struct profi_priv *priv)
{
	struct anybuss_client *client = priv->client;

	anybuss_set_power(client, false);
	priv->power_on = false;
	return 0;
}

static int profi_simple_enable(struct fieldbus_dev *fbdev, bool enable)
{
	int ret;
	struct profi_priv *priv = container_of(fbdev, struct profi_priv, fbdev);

	mutex_lock(&priv->enable_lock);
	if (enable)
		ret = __profi_enable(priv);
	else
		ret = __profi_disable(priv);
	mutex_unlock(&priv->enable_lock);

	return ret;
}

static void profi_on_area_updated(struct anybuss_client *client)
{
	struct profi_priv *priv = anybuss_get_drvdata(client);

	fieldbus_dev_area_updated(&priv->fbdev);
}

static void profi_on_online_changed(struct anybuss_client *client, bool online)
{
	struct profi_priv *priv = anybuss_get_drvdata(client);

	fieldbus_dev_online_changed(&priv->fbdev, online);
}

static int profinet_probe(struct anybuss_client *client)
{
	struct profi_priv *priv;
	struct device *dev = &client->dev;
	int err;

	client->on_area_updated = profi_on_area_updated;
	client->on_online_changed = profi_on_online_changed;
	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	mutex_init(&priv->enable_lock);
	priv->client = client;
	priv->fbdev.read_area_sz = PROFI_DPRAM_SIZE;
	priv->fbdev.write_area_sz = PROFI_DPRAM_SIZE;
	priv->fbdev.card_name = "HMS Profinet IRT (Anybus-S)";
	priv->fbdev.fieldbus_type = FIELDBUS_DEV_TYPE_PROFINET;
	priv->fbdev.read_area = profi_read_area;
	priv->fbdev.write_area = profi_write_area;
	priv->fbdev.fieldbus_id_get = profi_id_get;
	priv->fbdev.enable_get = profi_enable_get;
	priv->fbdev.simple_enable_set = profi_simple_enable;
	priv->fbdev.parent = dev;
	err = fieldbus_dev_register(&priv->fbdev);
	if (err < 0)
		return err;
	dev_info(dev, "card detected, registered as %s",
		 dev_name(priv->fbdev.dev));
	anybuss_set_drvdata(client, priv);

	return 0;
}

static int profinet_remove(struct anybuss_client *client)
{
	struct profi_priv *priv = anybuss_get_drvdata(client);

	fieldbus_dev_unregister(&priv->fbdev);
	return 0;
}

static struct anybuss_client_driver profinet_driver = {
	.probe = profinet_probe,
	.remove = profinet_remove,
	.driver		= {
		.name   = "hms-profinet",
		.owner	= THIS_MODULE,
	},
	.anybus_id = 0x0089,
};

static int __init profinet_init(void)
{
	return anybuss_client_driver_register(&profinet_driver);
}
module_init(profinet_init);

static void __exit profinet_exit(void)
{
	return anybuss_client_driver_unregister(&profinet_driver);
}
module_exit(profinet_exit);

MODULE_AUTHOR("Sven Van Asbroeck <TheSven73@gmail.com>");
MODULE_DESCRIPTION("HMS Profinet IRT Driver (Anybus-S)");
MODULE_LICENSE("GPL v2");
