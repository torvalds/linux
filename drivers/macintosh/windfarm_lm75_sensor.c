// SPDX-License-Identifier: GPL-2.0-only
/*
 * Windfarm PowerMac thermal control. LM75 sensor
 *
 * (c) Copyright 2005 Benjamin Herrenschmidt, IBM Corp.
 *                    <benh@kernel.crashing.org>
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/wait.h>
#include <linux/i2c.h>
#include <linux/of_device.h>
#include <asm/prom.h>
#include <asm/machdep.h>
#include <asm/io.h>
#include <asm/sections.h>
#include <asm/pmac_low_i2c.h>

#include "windfarm.h"

#define VERSION "1.0"

#undef DEBUG

#ifdef DEBUG
#define DBG(args...)	printk(args)
#else
#define DBG(args...)	do { } while(0)
#endif

struct wf_lm75_sensor {
	int			ds1775 : 1;
	int			inited : 1;
	struct i2c_client	*i2c;
	struct wf_sensor	sens;
};
#define wf_to_lm75(c) container_of(c, struct wf_lm75_sensor, sens)

static int wf_lm75_get(struct wf_sensor *sr, s32 *value)
{
	struct wf_lm75_sensor *lm = wf_to_lm75(sr);
	s32 data;

	if (lm->i2c == NULL)
		return -ENODEV;

	/* Init chip if necessary */
	if (!lm->inited) {
		u8 cfg_new, cfg = (u8)i2c_smbus_read_byte_data(lm->i2c, 1);

		DBG("wf_lm75: Initializing %s, cfg was: %02x\n",
		    sr->name, cfg);

		/* clear shutdown bit, keep other settings as left by
		 * the firmware for now
		 */
		cfg_new = cfg & ~0x01;
		i2c_smbus_write_byte_data(lm->i2c, 1, cfg_new);
		lm->inited = 1;

		/* If we just powered it up, let's wait 200 ms */
		msleep(200);
	}

	/* Read temperature register */
	data = (s32)le16_to_cpu(i2c_smbus_read_word_data(lm->i2c, 0));
	data <<= 8;
	*value = data;

	return 0;
}

static void wf_lm75_release(struct wf_sensor *sr)
{
	struct wf_lm75_sensor *lm = wf_to_lm75(sr);

	kfree(lm);
}

static const struct wf_sensor_ops wf_lm75_ops = {
	.get_value	= wf_lm75_get,
	.release	= wf_lm75_release,
	.owner		= THIS_MODULE,
};

static int wf_lm75_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{	
	struct wf_lm75_sensor *lm;
	int rc, ds1775;
	const char *name, *loc;

	if (id)
		ds1775 = id->driver_data;
	else
		ds1775 = !!of_device_get_match_data(&client->dev);

	DBG("wf_lm75: creating  %s device at address 0x%02x\n",
	    ds1775 ? "ds1775" : "lm75", client->addr);

	loc = of_get_property(client->dev.of_node, "hwsensor-location", NULL);
	if (!loc) {
		dev_warn(&client->dev, "Missing hwsensor-location property!\n");
		return -ENXIO;
	}

	/* Usual rant about sensor names not beeing very consistent in
	 * the device-tree, oh well ...
	 * Add more entries below as you deal with more setups
	 */
	if (!strcmp(loc, "Hard drive") || !strcmp(loc, "DRIVE BAY"))
		name = "hd-temp";
	else if (!strcmp(loc, "Incoming Air Temp"))
		name = "incoming-air-temp";
	else if (!strcmp(loc, "ODD Temp"))
		name = "optical-drive-temp";
	else if (!strcmp(loc, "HD Temp"))
		name = "hard-drive-temp";
	else if (!strcmp(loc, "PCI SLOTS"))
		name = "slots-temp";
	else if (!strcmp(loc, "CPU A INLET"))
		name = "cpu-inlet-temp-0";
	else if (!strcmp(loc, "CPU B INLET"))
		name = "cpu-inlet-temp-1";
	else
		return -ENXIO;
 	

	lm = kzalloc(sizeof(struct wf_lm75_sensor), GFP_KERNEL);
	if (lm == NULL)
		return -ENODEV;

	lm->inited = 0;
	lm->ds1775 = ds1775;
	lm->i2c = client;
	lm->sens.name = name;
	lm->sens.ops = &wf_lm75_ops;
	i2c_set_clientdata(client, lm);

	rc = wf_register_sensor(&lm->sens);
	if (rc)
		kfree(lm);
	return rc;
}

static int wf_lm75_remove(struct i2c_client *client)
{
	struct wf_lm75_sensor *lm = i2c_get_clientdata(client);

	/* Mark client detached */
	lm->i2c = NULL;

	/* release sensor */
	wf_unregister_sensor(&lm->sens);

	return 0;
}

static const struct i2c_device_id wf_lm75_id[] = {
	{ "MAC,lm75", 0 },
	{ "MAC,ds1775", 1 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wf_lm75_id);

static const struct of_device_id wf_lm75_of_id[] = {
	{ .compatible = "lm75", .data = (void *)0},
	{ .compatible = "ds1775", .data = (void *)1 },
	{ }
};
MODULE_DEVICE_TABLE(of, wf_lm75_of_id);

static struct i2c_driver wf_lm75_driver = {
	.driver = {
		.name	= "wf_lm75",
		.of_match_table = wf_lm75_of_id,
	},
	.probe		= wf_lm75_probe,
	.remove		= wf_lm75_remove,
	.id_table	= wf_lm75_id,
};

module_i2c_driver(wf_lm75_driver);

MODULE_AUTHOR("Benjamin Herrenschmidt <benh@kernel.crashing.org>");
MODULE_DESCRIPTION("LM75 sensor objects for PowerMacs thermal control");
MODULE_LICENSE("GPL");

