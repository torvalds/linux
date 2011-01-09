/*
 * MFD driver for wl1273 FM radio and audio codec submodules.
 *
 * Copyright (C) 2010 Nokia Corporation
 * Author: Matti Aaltonen <matti.j.aaltonen@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/mfd/wl1273-core.h>
#include <linux/slab.h>

#define DRIVER_DESC "WL1273 FM Radio Core"

static struct i2c_device_id wl1273_driver_id_table[] = {
	{ WL1273_FM_DRIVER_NAME, 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wl1273_driver_id_table);

static int wl1273_core_remove(struct i2c_client *client)
{
	struct wl1273_core *core = i2c_get_clientdata(client);

	dev_dbg(&client->dev, "%s\n", __func__);

	mfd_remove_devices(&client->dev);
	i2c_set_clientdata(client, NULL);
	kfree(core);

	return 0;
}

static int __devinit wl1273_core_probe(struct i2c_client *client,
				       const struct i2c_device_id *id)
{
	struct wl1273_fm_platform_data *pdata = client->dev.platform_data;
	struct wl1273_core *core;
	struct mfd_cell *cell;
	int children = 0;
	int r = 0;

	dev_dbg(&client->dev, "%s\n", __func__);

	if (!pdata) {
		dev_err(&client->dev, "No platform data.\n");
		return -EINVAL;
	}

	if (!(pdata->children & WL1273_RADIO_CHILD)) {
		dev_err(&client->dev, "Cannot function without radio child.\n");
		return -EINVAL;
	}

	core = kzalloc(sizeof(*core), GFP_KERNEL);
	if (!core)
		return -ENOMEM;

	core->pdata = pdata;
	core->client = client;
	mutex_init(&core->lock);

	i2c_set_clientdata(client, core);

	dev_dbg(&client->dev, "%s: Have V4L2.\n", __func__);

	cell = &core->cells[children];
	cell->name = "wl1273_fm_radio";
	cell->platform_data = &core;
	cell->data_size = sizeof(core);
	children++;

	if (pdata->children & WL1273_CODEC_CHILD) {
		cell = &core->cells[children];

		dev_dbg(&client->dev, "%s: Have codec.\n", __func__);
		cell->name = "wl1273-codec";
		cell->platform_data = &core;
		cell->data_size = sizeof(core);
		children++;
	}

	dev_dbg(&client->dev, "%s: number of children: %d.\n",
		__func__, children);

	r = mfd_add_devices(&client->dev, -1, core->cells,
			    children, NULL, 0);
	if (r)
		goto err;

	return 0;

err:
	i2c_set_clientdata(client, NULL);
	pdata->free_resources();
	kfree(core);

	dev_dbg(&client->dev, "%s\n", __func__);

	return r;
}

static struct i2c_driver wl1273_core_driver = {
	.driver = {
		.name = WL1273_FM_DRIVER_NAME,
	},
	.probe = wl1273_core_probe,
	.id_table = wl1273_driver_id_table,
	.remove = __devexit_p(wl1273_core_remove),
};

static int __init wl1273_core_init(void)
{
	int r;

	r = i2c_add_driver(&wl1273_core_driver);
	if (r) {
		pr_err(WL1273_FM_DRIVER_NAME
		       ": driver registration failed\n");
		return r;
	}

	return r;
}

static void __exit wl1273_core_exit(void)
{
	i2c_del_driver(&wl1273_core_driver);
}
late_initcall(wl1273_core_init);
module_exit(wl1273_core_exit);

MODULE_AUTHOR("Matti Aaltonen <matti.j.aaltonen@nokia.com>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
