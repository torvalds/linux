// SPDX-License-Identifier: GPL-2.0-only
/*
 * Windfarm PowerMac thermal control.  MAX6690 sensor.
 *
 * Copyright (C) 2005 Paul Mackerras, IBM Corp. <paulus@samba.org>
 */
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>

#include <asm/pmac_low_i2c.h>

#include "windfarm.h"

#define VERSION "1.0"

/* This currently only exports the external temperature sensor,
   since that's all the control loops need. */

/* Some MAX6690 register numbers */
#define MAX6690_INTERNAL_TEMP	0
#define MAX6690_EXTERNAL_TEMP	1

struct wf_6690_sensor {
	struct i2c_client	*i2c;
	struct wf_sensor	sens;
};

#define wf_to_6690(x)	container_of((x), struct wf_6690_sensor, sens)

static int wf_max6690_get(struct wf_sensor *sr, s32 *value)
{
	struct wf_6690_sensor *max = wf_to_6690(sr);
	s32 data;

	if (max->i2c == NULL)
		return -ENODEV;

	/* chip gets initialized by firmware */
	data = i2c_smbus_read_byte_data(max->i2c, MAX6690_EXTERNAL_TEMP);
	if (data < 0)
		return data;
	*value = data << 16;
	return 0;
}

static void wf_max6690_release(struct wf_sensor *sr)
{
	struct wf_6690_sensor *max = wf_to_6690(sr);

	kfree(max);
}

static const struct wf_sensor_ops wf_max6690_ops = {
	.get_value	= wf_max6690_get,
	.release	= wf_max6690_release,
	.owner		= THIS_MODULE,
};

static int wf_max6690_probe(struct i2c_client *client)
{
	const char *name, *loc;
	struct wf_6690_sensor *max;
	int rc;

	loc = of_get_property(client->dev.of_node, "hwsensor-location", NULL);
	if (!loc) {
		dev_warn(&client->dev, "Missing hwsensor-location property!\n");
		return -ENXIO;
	}

	/*
	 * We only expose the external temperature register for
	 * now as this is all we need for our control loops
	 */
	if (!strcmp(loc, "BACKSIDE") || !strcmp(loc, "SYS CTRLR AMBIENT"))
		name = "backside-temp";
	else if (!strcmp(loc, "NB Ambient"))
		name = "north-bridge-temp";
	else if (!strcmp(loc, "GPU Ambient"))
		name = "gpu-temp";
	else
		return -ENXIO;

	max = kzalloc(sizeof(struct wf_6690_sensor), GFP_KERNEL);
	if (max == NULL) {
		printk(KERN_ERR "windfarm: Couldn't create MAX6690 sensor: "
		       "no memory\n");
		return -ENOMEM;
	}

	max->i2c = client;
	max->sens.name = name;
	max->sens.ops = &wf_max6690_ops;
	i2c_set_clientdata(client, max);

	rc = wf_register_sensor(&max->sens);
	if (rc)
		kfree(max);
	return rc;
}

static void wf_max6690_remove(struct i2c_client *client)
{
	struct wf_6690_sensor *max = i2c_get_clientdata(client);

	max->i2c = NULL;
	wf_unregister_sensor(&max->sens);
}

static const struct i2c_device_id wf_max6690_id[] = {
	{ "MAC,max6690" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wf_max6690_id);

static const struct of_device_id wf_max6690_of_id[] = {
	{ .compatible = "max6690", },
	{ }
};
MODULE_DEVICE_TABLE(of, wf_max6690_of_id);

static struct i2c_driver wf_max6690_driver = {
	.driver = {
		.name		= "wf_max6690",
		.of_match_table = wf_max6690_of_id,
	},
	.probe		= wf_max6690_probe,
	.remove		= wf_max6690_remove,
	.id_table	= wf_max6690_id,
};

module_i2c_driver(wf_max6690_driver);

MODULE_AUTHOR("Paul Mackerras <paulus@samba.org>");
MODULE_DESCRIPTION("MAX6690 sensor objects for PowerMac thermal control");
MODULE_LICENSE("GPL");
