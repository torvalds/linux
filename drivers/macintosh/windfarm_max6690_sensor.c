/*
 * Windfarm PowerMac thermal control.  MAX6690 sensor.
 *
 * Copyright (C) 2005 Paul Mackerras, IBM Corp. <paulus@samba.org>
 *
 * Use and redistribute under the terms of the GNU GPL v2.
 */
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <asm/prom.h>
#include <asm/pmac_low_i2c.h>

#include "windfarm.h"

#define VERSION "0.2"

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

static struct wf_sensor_ops wf_max6690_ops = {
	.get_value	= wf_max6690_get,
	.release	= wf_max6690_release,
	.owner		= THIS_MODULE,
};

static int wf_max6690_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	struct wf_6690_sensor *max;
	int rc;

	max = kzalloc(sizeof(struct wf_6690_sensor), GFP_KERNEL);
	if (max == NULL) {
		printk(KERN_ERR "windfarm: Couldn't create MAX6690 sensor: "
		       "no memory\n");
		return -ENOMEM;
	}

	max->i2c = client;
	max->sens.name = client->dev.platform_data;
	max->sens.ops = &wf_max6690_ops;
	i2c_set_clientdata(client, max);

	rc = wf_register_sensor(&max->sens);
	if (rc) {
		kfree(max);
	}

	return rc;
}

static struct i2c_driver wf_max6690_driver;

static struct i2c_client *wf_max6690_create(struct i2c_adapter *adapter,
					    u8 addr, const char *loc)
{
	struct i2c_board_info info;
	struct i2c_client *client;
	char *name;

	if (!strcmp(loc, "BACKSIDE"))
		name = "backside-temp";
	else if (!strcmp(loc, "NB Ambient"))
		name = "north-bridge-temp";
	else if (!strcmp(loc, "GPU Ambient"))
		name = "gpu-temp";
	else
		goto fail;

	memset(&info, 0, sizeof(struct i2c_board_info));
	info.addr = addr >> 1;
	info.platform_data = name;
	strlcpy(info.type, "wf_max6690", I2C_NAME_SIZE);

	client = i2c_new_device(adapter, &info);
	if (client == NULL) {
		printk(KERN_ERR "windfarm: failed to attach MAX6690 sensor\n");
		goto fail;
	}

	/*
	 * Let i2c-core delete that device on driver removal.
	 * This is safe because i2c-core holds the core_lock mutex for us.
	 */
	list_add_tail(&client->detected, &wf_max6690_driver.clients);
	return client;

 fail:
	return NULL;
}

static int wf_max6690_attach(struct i2c_adapter *adapter)
{
	struct device_node *busnode, *dev = NULL;
	struct pmac_i2c_bus *bus;
	const char *loc;

	bus = pmac_i2c_adapter_to_bus(adapter);
	if (bus == NULL)
		return -ENODEV;
	busnode = pmac_i2c_get_bus_node(bus);

	while ((dev = of_get_next_child(busnode, dev)) != NULL) {
		u8 addr;

		/* We must re-match the adapter in order to properly check
		 * the channel on multibus setups
		 */
		if (!pmac_i2c_match_adapter(dev, adapter))
			continue;
		if (!of_device_is_compatible(dev, "max6690"))
			continue;
		addr = pmac_i2c_get_dev_addr(dev);
		loc = of_get_property(dev, "hwsensor-location", NULL);
		if (loc == NULL || addr == 0)
			continue;
		printk("found max6690, loc=%s addr=0x%02x\n", loc, addr);
		wf_max6690_create(adapter, addr, loc);
	}

	return 0;
}

static int wf_max6690_remove(struct i2c_client *client)
{
	struct wf_6690_sensor *max = i2c_get_clientdata(client);

	max->i2c = NULL;
	wf_unregister_sensor(&max->sens);

	return 0;
}

static const struct i2c_device_id wf_max6690_id[] = {
	{ "wf_max6690", 0 },
	{ }
};

static struct i2c_driver wf_max6690_driver = {
	.driver = {
		.name		= "wf_max6690",
	},
	.attach_adapter	= wf_max6690_attach,
	.probe		= wf_max6690_probe,
	.remove		= wf_max6690_remove,
	.id_table	= wf_max6690_id,
};

static int __init wf_max6690_sensor_init(void)
{
	/* Don't register on old machines that use therm_pm72 for now */
	if (of_machine_is_compatible("PowerMac7,2") ||
	    of_machine_is_compatible("PowerMac7,3") ||
	    of_machine_is_compatible("RackMac3,1"))
		return -ENODEV;
	return i2c_add_driver(&wf_max6690_driver);
}

static void __exit wf_max6690_sensor_exit(void)
{
	i2c_del_driver(&wf_max6690_driver);
}

module_init(wf_max6690_sensor_init);
module_exit(wf_max6690_sensor_exit);

MODULE_AUTHOR("Paul Mackerras <paulus@samba.org>");
MODULE_DESCRIPTION("MAX6690 sensor objects for PowerMac thermal control");
MODULE_LICENSE("GPL");
