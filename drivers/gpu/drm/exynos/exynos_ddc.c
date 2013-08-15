/*
 * Copyright (C) 2011 Samsung Electronics Co.Ltd
 * Authors:
 *	Seung-Woo Kim <sw0312.kim@samsung.com>
 *	Inki Dae <inki.dae@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#include <drm/drmP.h>

#include <linux/kernel.h>
#include <linux/i2c.h>


#include "exynos_drm_drv.h"
#include "exynos_hdmi.h"

static int s5p_ddc_probe(struct i2c_client *client,
			const struct i2c_device_id *dev_id)
{
	hdmi_attach_ddc_client(client);

	dev_info(&client->adapter->dev,
		"attached %s into i2c adapter successfully\n",
		client->name);

	return 0;
}

static int s5p_ddc_remove(struct i2c_client *client)
{
	dev_info(&client->adapter->dev,
		"detached %s from i2c adapter successfully\n",
		client->name);

	return 0;
}

static struct i2c_device_id ddc_idtable[] = {
	{"s5p_ddc", 0},
	{"exynos5-hdmiddc", 0},
	{ },
};

#ifdef CONFIG_OF
static struct of_device_id hdmiddc_match_types[] = {
	{
		.compatible = "samsung,exynos5-hdmiddc",
	}, {
		.compatible = "samsung,exynos4210-hdmiddc",
	}, {
		/* end node */
	}
};
#endif

struct i2c_driver ddc_driver = {
	.driver = {
		.name = "exynos-hdmiddc",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(hdmiddc_match_types),
	},
	.id_table	= ddc_idtable,
	.probe		= s5p_ddc_probe,
	.remove		= s5p_ddc_remove,
	.command		= NULL,
};
