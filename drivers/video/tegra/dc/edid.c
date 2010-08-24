/*
 * drivers/video/tegra/dc/edid.c
 *
 * Copyright (C) 2010 Google, Inc.
 * Author: Erik Gilling <konkers@android.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/i2c.h>
#include <linux/fb.h>

#include "edid.h"

int tegra_edid_get_monspecs(struct tegra_edid *edid, struct fb_monspecs *specs)
{
	u8 data[256];
	int i;
	int ret;

	for (i = 0; i < 256; i++) {
		ret = i2c_smbus_read_byte_data(edid->client, i);
		if (ret < 0)
			break;

		data[i] = ret;
	}

	if (i != 128 && i != 256)
		return ret;

	fb_edid_to_monspecs(data, specs);
	if (i == 256)
		fb_edid_add_monspecs(data + 128, specs);

	return 0;
}

struct tegra_edid *tegra_edid_create(int bus)
{
	struct tegra_edid *edid;
	struct i2c_adapter *adapter;

	edid = kzalloc(sizeof(struct tegra_edid), GFP_KERNEL);
	if (!edid)
		return ERR_PTR(-ENOMEM);

	strlcpy(edid->info.type, "tegra_edid", sizeof(edid->info.type));
	edid->info.addr = 0x50;
	edid->info.platform_data = edid;
	init_waitqueue_head(&edid->wq);

	adapter = i2c_get_adapter(bus);
	if (!adapter) {
		pr_err("can't get adpater for bus %d\n", bus);
		return NULL;
	}
	edid->client = i2c_new_device(adapter, &edid->info);

	i2c_put_adapter(adapter);

	if (!edid->client) {
		pr_err("can't create new device\n");
		return NULL;
	}

	return edid;
}

void tegra_edid_destroy(struct tegra_edid *edid)
{
	i2c_release_client(edid->client);
	kfree(edid);
}

static const struct i2c_device_id tegra_edid_id[] = {
        { "tegra_edid", 0 },
        { }
};

MODULE_DEVICE_TABLE(i2c, tegra_edid_id);

static struct i2c_driver tegra_edid_driver = {
        .id_table = tegra_edid_id,
        .driver = {
                .name = "tegra_edid",
        },
};

static int __init tegra_edid_init(void)
{
        return i2c_add_driver(&tegra_edid_driver);
}

static void __exit tegra_edid_exit(void)
{
        i2c_del_driver(&tegra_edid_driver);
}

module_init(tegra_edid_init);
module_exit(tegra_edid_exit);
