/*
 * Windfarm PowerMac thermal control. LM75 sensor
 *
 * (c) Copyright 2005 Benjamin Herrenschmidt, IBM Corp.
 *                    <benh@kernel.crashing.org>
 *
 * Released under the term of the GNU GPL v2.
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
#include <asm/system.h>
#include <asm/sections.h>
#include <asm/pmac_low_i2c.h>

#include "windfarm.h"

#define VERSION "0.2"

#undef DEBUG

#ifdef DEBUG
#define DBG(args...)	printk(args)
#else
#define DBG(args...)	do { } while(0)
#endif

struct wf_lm75_sensor {
	int			ds1775 : 1;
	int			inited : 1;
	struct 	i2c_client	*i2c;
	struct 	wf_sensor	sens;
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

static struct wf_sensor_ops wf_lm75_ops = {
	.get_value	= wf_lm75_get,
	.release	= wf_lm75_release,
	.owner		= THIS_MODULE,
};

static int wf_lm75_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct wf_lm75_sensor *lm;
	int rc;

	lm = kzalloc(sizeof(struct wf_lm75_sensor), GFP_KERNEL);
	if (lm == NULL)
		return -ENODEV;

	lm->inited = 0;
	lm->ds1775 = id->driver_data;
	lm->i2c = client;
	lm->sens.name = client->dev.platform_data;
	lm->sens.ops = &wf_lm75_ops;
	i2c_set_clientdata(client, lm);

	rc = wf_register_sensor(&lm->sens);
	if (rc) {
		i2c_set_clientdata(client, NULL);
		kfree(lm);
	}

	return rc;
}

static struct i2c_driver wf_lm75_driver;

static struct i2c_client *wf_lm75_create(struct i2c_adapter *adapter,
					     u8 addr, int ds1775,
					     const char *loc)
{
	struct i2c_board_info info;
	struct i2c_client *client;
	char *name;

	DBG("wf_lm75: creating  %s device at address 0x%02x\n",
	    ds1775 ? "ds1775" : "lm75", addr);

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
	else
		goto fail;

	memset(&info, 0, sizeof(struct i2c_board_info));
	info.addr = (addr >> 1) & 0x7f;
	info.platform_data = name;
	strlcpy(info.type, ds1775 ? "wf_ds1775" : "wf_lm75", I2C_NAME_SIZE);

	client = i2c_new_device(adapter, &info);
	if (client == NULL) {
		printk(KERN_ERR "windfarm: failed to attach %s %s to i2c\n",
		       ds1775 ? "ds1775" : "lm75", name);
		goto fail;
	}

	/*
	 * Let i2c-core delete that device on driver removal.
	 * This is safe because i2c-core holds the core_lock mutex for us.
	 */
	list_add_tail(&client->detected, &wf_lm75_driver.clients);
	return client;
 fail:
	return NULL;
}

static int wf_lm75_attach(struct i2c_adapter *adapter)
{
	struct device_node *busnode, *dev;
	struct pmac_i2c_bus *bus;

	DBG("wf_lm75: adapter %s detected\n", adapter->name);

	bus = pmac_i2c_adapter_to_bus(adapter);
	if (bus == NULL)
		return -ENODEV;
	busnode = pmac_i2c_get_bus_node(bus);

	DBG("wf_lm75: bus found, looking for device...\n");

	/* Now look for lm75(s) in there */
	for (dev = NULL;
	     (dev = of_get_next_child(busnode, dev)) != NULL;) {
		const char *loc =
			of_get_property(dev, "hwsensor-location", NULL);
		u8 addr;

		/* We must re-match the adapter in order to properly check
		 * the channel on multibus setups
		 */
		if (!pmac_i2c_match_adapter(dev, adapter))
			continue;
		addr = pmac_i2c_get_dev_addr(dev);
		if (loc == NULL || addr == 0)
			continue;
		/* real lm75 */
		if (of_device_is_compatible(dev, "lm75"))
			wf_lm75_create(adapter, addr, 0, loc);
		/* ds1775 (compatible, better resolution */
		else if (of_device_is_compatible(dev, "ds1775"))
			wf_lm75_create(adapter, addr, 1, loc);
	}
	return 0;
}

static int wf_lm75_remove(struct i2c_client *client)
{
	struct wf_lm75_sensor *lm = i2c_get_clientdata(client);

	DBG("wf_lm75: i2c detatch called for %s\n", lm->sens.name);

	/* Mark client detached */
	lm->i2c = NULL;

	/* release sensor */
	wf_unregister_sensor(&lm->sens);

	i2c_set_clientdata(client, NULL);
	return 0;
}

static const struct i2c_device_id wf_lm75_id[] = {
	{ "wf_lm75", 0 },
	{ "wf_ds1775", 1 },
	{ }
};

static struct i2c_driver wf_lm75_driver = {
	.driver = {
		.name	= "wf_lm75",
	},
	.attach_adapter	= wf_lm75_attach,
	.probe		= wf_lm75_probe,
	.remove		= wf_lm75_remove,
	.id_table	= wf_lm75_id,
};

static int __init wf_lm75_sensor_init(void)
{
	/* Don't register on old machines that use therm_pm72 for now */
	if (machine_is_compatible("PowerMac7,2") ||
	    machine_is_compatible("PowerMac7,3") ||
	    machine_is_compatible("RackMac3,1"))
		return -ENODEV;
	return i2c_add_driver(&wf_lm75_driver);
}

static void __exit wf_lm75_sensor_exit(void)
{
	i2c_del_driver(&wf_lm75_driver);
}


module_init(wf_lm75_sensor_init);
module_exit(wf_lm75_sensor_exit);

MODULE_AUTHOR("Benjamin Herrenschmidt <benh@kernel.crashing.org>");
MODULE_DESCRIPTION("LM75 sensor objects for PowerMacs thermal control");
MODULE_LICENSE("GPL");

