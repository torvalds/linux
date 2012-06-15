/*
 * Windfarm PowerMac thermal control. LM87 sensor
 *
 * Copyright 2012 Benjamin Herrenschmidt, IBM Corp.
 *
 * Released under the term of the GNU GPL v2.
 *
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/wait.h>
#include <linux/i2c.h>
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

struct wf_lm87_sensor {
	struct i2c_client	*i2c;
	struct wf_sensor	sens;
};
#define wf_to_lm87(c) container_of(c, struct wf_lm87_sensor, sens)


static int wf_lm87_read_reg(struct i2c_client *chip, int reg)
{
	int rc, tries = 0;
	u8 buf;

	for (;;) {
		/* Set address */
		buf = (u8)reg;
		rc = i2c_master_send(chip, &buf, 1);
		if (rc <= 0)
			goto error;
		rc = i2c_master_recv(chip, &buf, 1);
		if (rc <= 0)
			goto error;
		return (int)buf;
	error:
		DBG("wf_lm87: Error reading LM87, retrying...\n");
		if (++tries > 10) {
			printk(KERN_ERR "wf_lm87: Error reading LM87 !\n");
			return -EIO;
		}
		msleep(10);
	}
}

static int wf_lm87_get(struct wf_sensor *sr, s32 *value)
{
	struct wf_lm87_sensor *lm = sr->priv;
	s32 temp;

	if (lm->i2c == NULL)
		return -ENODEV;

#define LM87_INT_TEMP		0x27

	/* Read temperature register */
	temp = wf_lm87_read_reg(lm->i2c, LM87_INT_TEMP);
	if (temp < 0)
		return temp;
	*value = temp << 16;

	return 0;
}

static void wf_lm87_release(struct wf_sensor *sr)
{
	struct wf_lm87_sensor *lm = wf_to_lm87(sr);

	kfree(lm);
}

static struct wf_sensor_ops wf_lm87_ops = {
	.get_value	= wf_lm87_get,
	.release	= wf_lm87_release,
	.owner		= THIS_MODULE,
};

static int wf_lm87_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{	
	struct wf_lm87_sensor *lm;
	const char *name = NULL, *loc;
	struct device_node *np = NULL;
	int rc;

	/*
	 * The lm87 contains a whole pile of sensors, additionally,
	 * the Xserve G5 has several lm87's. However, for now we only
	 * care about the internal temperature sensor
	 */
	while ((np = of_get_next_child(client->dev.of_node, np)) != NULL) {
		if (strcmp(np->name, "int-temp"))
			continue;
		loc = of_get_property(np, "location", NULL);
		if (!loc)
			continue;
		if (strstr(loc, "DIMM"))
			name = "dimms-temp";
		else if (strstr(loc, "Processors"))
			name = "between-cpus-temp";
		if (name) {
			of_node_put(np);
			break;
		}
	}
	if (!name) {
		pr_warning("wf_lm87: Unsupported sensor %s\n",
			   client->dev.of_node->full_name);
		return -ENODEV;
	}

	lm = kzalloc(sizeof(struct wf_lm87_sensor), GFP_KERNEL);
	if (lm == NULL)
		return -ENODEV;

	lm->i2c = client;
	lm->sens.name = name;
	lm->sens.ops = &wf_lm87_ops;
	lm->sens.priv = lm;
	i2c_set_clientdata(client, lm);

	rc = wf_register_sensor(&lm->sens);
	if (rc)
		kfree(lm);
	return rc;
}

static int wf_lm87_remove(struct i2c_client *client)
{
	struct wf_lm87_sensor *lm = i2c_get_clientdata(client);

	DBG("wf_lm87: i2c detatch called for %s\n", lm->sens.name);

	/* Mark client detached */
	lm->i2c = NULL;

	/* release sensor */
	wf_unregister_sensor(&lm->sens);

	return 0;
}

static const struct i2c_device_id wf_lm87_id[] = {
	{ "MAC,lm87cimt", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wf_lm87_id);

static struct i2c_driver wf_lm87_driver = {
	.driver = {
		.name	= "wf_lm87",
	},
	.probe		= wf_lm87_probe,
	.remove		= wf_lm87_remove,
	.id_table	= wf_lm87_id,
};

static int __init wf_lm87_sensor_init(void)
{
	/* We only support this on the Xserve */
	if (!of_machine_is_compatible("RackMac3,1"))
		return -ENODEV;

	return i2c_add_driver(&wf_lm87_driver);
}

static void __exit wf_lm87_sensor_exit(void)
{
	i2c_del_driver(&wf_lm87_driver);
}


module_init(wf_lm87_sensor_init);
module_exit(wf_lm87_sensor_exit);

MODULE_AUTHOR("Benjamin Herrenschmidt <benh@kernel.crashing.org>");
MODULE_DESCRIPTION("LM87 sensor objects for PowerMacs thermal control");
MODULE_LICENSE("GPL");

