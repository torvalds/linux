/*
    i2c Support for Apple SMU Controller

    Copyright (c) 2005 Benjamin Herrenschmidt, IBM Corp.
                       <benh@kernel.crashing.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <asm/prom.h>
#include <asm/pmac_low_i2c.h>

MODULE_AUTHOR("Benjamin Herrenschmidt <benh@kernel.crashing.org>");
MODULE_DESCRIPTION("I2C driver for Apple PowerMac");
MODULE_LICENSE("GPL");

/*
 * SMBUS-type transfer entrypoint
 */
static s32 i2c_powermac_smbus_xfer(	struct i2c_adapter*	adap,
					u16			addr,
					unsigned short		flags,
					char			read_write,
					u8			command,
					int			size,
					union i2c_smbus_data*	data)
{
	struct pmac_i2c_bus	*bus = i2c_get_adapdata(adap);
	int			rc = 0;
	int			read = (read_write == I2C_SMBUS_READ);
	int			addrdir = (addr << 1) | read;
	u8			local[2];

	rc = pmac_i2c_open(bus, 0);
	if (rc)
		return rc;

	switch (size) {
        case I2C_SMBUS_QUICK:
		rc = pmac_i2c_setmode(bus, pmac_i2c_mode_std);
		if (rc)
			goto bail;
		rc = pmac_i2c_xfer(bus, addrdir, 0, 0, NULL, 0);
	    	break;
        case I2C_SMBUS_BYTE:
		rc = pmac_i2c_setmode(bus, pmac_i2c_mode_std);
		if (rc)
			goto bail;
		rc = pmac_i2c_xfer(bus, addrdir, 0, 0, &data->byte, 1);
	    	break;
        case I2C_SMBUS_BYTE_DATA:
		rc = pmac_i2c_setmode(bus, read ?
				      pmac_i2c_mode_combined :
				      pmac_i2c_mode_stdsub);
		if (rc)
			goto bail;
		rc = pmac_i2c_xfer(bus, addrdir, 1, command, &data->byte, 1);
	    	break;
        case I2C_SMBUS_WORD_DATA:
		rc = pmac_i2c_setmode(bus, read ?
				      pmac_i2c_mode_combined :
				      pmac_i2c_mode_stdsub);
		if (rc)
			goto bail;
		if (!read) {
			local[0] = data->word & 0xff;
			local[1] = (data->word >> 8) & 0xff;
		}
		rc = pmac_i2c_xfer(bus, addrdir, 1, command, local, 2);
		if (rc == 0 && read) {
			data->word = ((u16)local[1]) << 8;
			data->word |= local[0];
		}
	    	break;

	/* Note that these are broken vs. the expected smbus API where
	 * on reads, the lenght is actually returned from the function,
	 * but I think the current API makes no sense and I don't want
	 * any driver that I haven't verified for correctness to go
	 * anywhere near a pmac i2c bus anyway ...
	 *
	 * I'm also not completely sure what kind of phases to do between
	 * the actual command and the data (what I am _supposed_ to do that
	 * is). For now, I assume writes are a single stream and reads have
	 * a repeat start/addr phase (but not stop in between)
	 */
        case I2C_SMBUS_BLOCK_DATA:
		rc = pmac_i2c_setmode(bus, read ?
				      pmac_i2c_mode_combined :
				      pmac_i2c_mode_stdsub);
		if (rc)
			goto bail;
		rc = pmac_i2c_xfer(bus, addrdir, 1, command, data->block,
				   data->block[0] + 1);

		break;
	case I2C_SMBUS_I2C_BLOCK_DATA:
		rc = pmac_i2c_setmode(bus, read ?
				      pmac_i2c_mode_combined :
				      pmac_i2c_mode_stdsub);
		if (rc)
			goto bail;
		rc = pmac_i2c_xfer(bus, addrdir, 1, command,
				   &data->block[1], data->block[0]);
		break;

        default:
	    	rc = -EINVAL;
	}
 bail:
	pmac_i2c_close(bus);
	return rc;
}

/*
 * Generic i2c master transfer entrypoint. This driver only support single
 * messages (for "lame i2c" transfers). Anything else should use the smbus
 * entry point
 */
static int i2c_powermac_master_xfer(	struct i2c_adapter *adap,
					struct i2c_msg *msgs,
					int num)
{
	struct pmac_i2c_bus	*bus = i2c_get_adapdata(adap);
	int			rc = 0;
	int			read;
	int			addrdir;

	if (msgs->flags & I2C_M_TEN)
		return -EINVAL;
	read = (msgs->flags & I2C_M_RD) != 0;
	addrdir = (msgs->addr << 1) | read;
	if (msgs->flags & I2C_M_REV_DIR_ADDR)
		addrdir ^= 1;

	rc = pmac_i2c_open(bus, 0);
	if (rc)
		return rc;
	rc = pmac_i2c_setmode(bus, pmac_i2c_mode_std);
	if (rc)
		goto bail;
	rc = pmac_i2c_xfer(bus, addrdir, 0, 0, msgs->buf, msgs->len);
 bail:
	pmac_i2c_close(bus);
	return rc < 0 ? rc : 1;
}

static u32 i2c_powermac_func(struct i2c_adapter * adapter)
{
	return I2C_FUNC_SMBUS_QUICK | I2C_FUNC_SMBUS_BYTE |
		I2C_FUNC_SMBUS_BYTE_DATA | I2C_FUNC_SMBUS_WORD_DATA |
		I2C_FUNC_SMBUS_BLOCK_DATA | I2C_FUNC_I2C;
}

/* For now, we only handle smbus */
static const struct i2c_algorithm i2c_powermac_algorithm = {
	.smbus_xfer	= i2c_powermac_smbus_xfer,
	.master_xfer	= i2c_powermac_master_xfer,
	.functionality	= i2c_powermac_func,
};


static int i2c_powermac_remove(struct platform_device *dev)
{
	struct i2c_adapter	*adapter = platform_get_drvdata(dev);
	struct pmac_i2c_bus	*bus = i2c_get_adapdata(adapter);
	int			rc;

	rc = i2c_del_adapter(adapter);
	pmac_i2c_detach_adapter(bus, adapter);
	i2c_set_adapdata(adapter, NULL);
	/* We aren't that prepared to deal with this... */
	if (rc)
		printk("i2c-powermac.c: Failed to remove bus %s !\n",
		       adapter->name);
	platform_set_drvdata(dev, NULL);
	kfree(adapter);

	return 0;
}


static int __devexit i2c_powermac_probe(struct platform_device *dev)
{
	struct pmac_i2c_bus *bus = dev->dev.platform_data;
	struct device_node *parent = NULL;
	struct i2c_adapter *adapter;
	char name[32];
	const char *basename;
	int rc;

	if (bus == NULL)
		return -EINVAL;

	/* Ok, now we need to make up a name for the interface that will
	 * match what we used to do in the past, that is basically the
	 * controller's parent device node for keywest. PMU didn't have a
	 * naming convention and SMU has a different one
	 */
	switch(pmac_i2c_get_type(bus)) {
	case pmac_i2c_bus_keywest:
		parent = of_get_parent(pmac_i2c_get_controller(bus));
		if (parent == NULL)
			return -EINVAL;
		basename = parent->name;
		break;
	case pmac_i2c_bus_pmu:
		basename = "pmu";
		break;
	case pmac_i2c_bus_smu:
		/* This is not what we used to do but I'm fixing drivers at
		 * the same time as this change
		 */
		basename = "smu";
		break;
	default:
		return -EINVAL;
	}
	snprintf(name, 32, "%s %d", basename, pmac_i2c_get_channel(bus));
	of_node_put(parent);

	adapter = kzalloc(sizeof(struct i2c_adapter), GFP_KERNEL);
	if (adapter == NULL) {
		printk(KERN_ERR "i2c-powermac: can't allocate inteface !\n");
		return -ENOMEM;
	}
	platform_set_drvdata(dev, adapter);
	strcpy(adapter->name, name);
	adapter->algo = &i2c_powermac_algorithm;
	i2c_set_adapdata(adapter, bus);
	adapter->dev.parent = &dev->dev;
	pmac_i2c_attach_adapter(bus, adapter);
	rc = i2c_add_adapter(adapter);
	if (rc) {
		printk(KERN_ERR "i2c-powermac: Adapter %s registration "
		       "failed\n", name);
		i2c_set_adapdata(adapter, NULL);
		pmac_i2c_detach_adapter(bus, adapter);
	}

	printk(KERN_INFO "PowerMac i2c bus %s registered\n", name);
	return rc;
}


static struct platform_driver i2c_powermac_driver = {
	.probe = i2c_powermac_probe,
	.remove = __devexit_p(i2c_powermac_remove),
	.driver = {
		.name = "i2c-powermac",
		.bus = &platform_bus_type,
	},
};

static int __init i2c_powermac_init(void)
{
	platform_driver_register(&i2c_powermac_driver);
	return 0;
}


static void __exit i2c_powermac_cleanup(void)
{
	platform_driver_unregister(&i2c_powermac_driver);
}

module_init(i2c_powermac_init);
module_exit(i2c_powermac_cleanup);
