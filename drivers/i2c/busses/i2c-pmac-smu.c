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

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/completion.h>
#include <linux/device.h>
#include <asm/prom.h>
#include <asm/of_device.h>
#include <asm/smu.h>

static int probe;

MODULE_AUTHOR("Benjamin Herrenschmidt <benh@kernel.crashing.org>");
MODULE_DESCRIPTION("I2C driver for Apple's SMU");
MODULE_LICENSE("GPL");
module_param(probe, bool, 0);


/* Physical interface */
struct smu_iface
{
	struct i2c_adapter	adapter;
	struct completion	complete;
	u32			busid;
};

static void smu_i2c_done(struct smu_i2c_cmd *cmd, void *misc)
{
	struct smu_iface	*iface = misc;
	complete(&iface->complete);
}

/*
 * SMBUS-type transfer entrypoint
 */
static s32 smu_smbus_xfer(	struct i2c_adapter*	adap,
				u16			addr,
				unsigned short		flags,
				char			read_write,
				u8			command,
				int			size,
				union i2c_smbus_data*	data)
{
	struct smu_iface	*iface = i2c_get_adapdata(adap);
	struct smu_i2c_cmd	cmd;
	int			rc = 0;
	int			read = (read_write == I2C_SMBUS_READ);

	cmd.info.bus = iface->busid;
	cmd.info.devaddr = (addr << 1) | (read ? 0x01 : 0x00);

	/* Prepare datas & select mode */
	switch (size) {
        case I2C_SMBUS_QUICK:
		cmd.info.type = SMU_I2C_TRANSFER_SIMPLE;
		cmd.info.datalen = 0;
	    	break;
        case I2C_SMBUS_BYTE:
		cmd.info.type = SMU_I2C_TRANSFER_SIMPLE;
		cmd.info.datalen = 1;
		if (!read)
			cmd.info.data[0] = data->byte;
	    	break;
        case I2C_SMBUS_BYTE_DATA:
		cmd.info.type = SMU_I2C_TRANSFER_STDSUB;
		cmd.info.datalen = 1;
		cmd.info.sublen = 1;
		cmd.info.subaddr[0] = command;
		cmd.info.subaddr[1] = 0;
		cmd.info.subaddr[2] = 0;
		if (!read)
			cmd.info.data[0] = data->byte;
	    	break;
        case I2C_SMBUS_WORD_DATA:
		cmd.info.type = SMU_I2C_TRANSFER_STDSUB;
		cmd.info.datalen = 2;
		cmd.info.sublen = 1;
		cmd.info.subaddr[0] = command;
		cmd.info.subaddr[1] = 0;
		cmd.info.subaddr[2] = 0;
		if (!read) {
			cmd.info.data[0] = data->byte & 0xff;
			cmd.info.data[1] = (data->byte >> 8) & 0xff;
		}
		break;
	/* Note that these are broken vs. the expected smbus API where
	 * on reads, the lenght is actually returned from the function,
	 * but I think the current API makes no sense and I don't want
	 * any driver that I haven't verified for correctness to go
	 * anywhere near a pmac i2c bus anyway ...
	 */
        case I2C_SMBUS_BLOCK_DATA:
		cmd.info.type = SMU_I2C_TRANSFER_STDSUB;
		cmd.info.datalen = data->block[0] + 1;
		if (cmd.info.datalen > 6)
			return -EINVAL;
		if (!read)
			memcpy(cmd.info.data, data->block, cmd.info.datalen);
		cmd.info.sublen = 1;
		cmd.info.subaddr[0] = command;
		cmd.info.subaddr[1] = 0;
		cmd.info.subaddr[2] = 0;
		break;
	case I2C_SMBUS_I2C_BLOCK_DATA:
		cmd.info.type = SMU_I2C_TRANSFER_STDSUB;
		cmd.info.datalen = data->block[0];
		if (cmd.info.datalen > 7)
			return -EINVAL;
		if (!read)
			memcpy(cmd.info.data, &data->block[1],
			       cmd.info.datalen);
		cmd.info.sublen = 1;
		cmd.info.subaddr[0] = command;
		cmd.info.subaddr[1] = 0;
		cmd.info.subaddr[2] = 0;
		break;

        default:
	    	return -EINVAL;
	}

	/* Turn a standardsub read into a combined mode access */
 	if (read_write == I2C_SMBUS_READ &&
	    cmd.info.type == SMU_I2C_TRANSFER_STDSUB)
		cmd.info.type = SMU_I2C_TRANSFER_COMBINED;

	/* Finish filling command and submit it */
	cmd.done = smu_i2c_done;
	cmd.misc = iface;
	rc = smu_queue_i2c(&cmd);
	if (rc < 0)
		return rc;
	wait_for_completion(&iface->complete);
	rc = cmd.status;

	if (!read || rc < 0)
		return rc;

	switch (size) {
        case I2C_SMBUS_BYTE:
        case I2C_SMBUS_BYTE_DATA:
		data->byte = cmd.info.data[0];
	    	break;
        case I2C_SMBUS_WORD_DATA:
		data->word = ((u16)cmd.info.data[1]) << 8;
		data->word |= cmd.info.data[0];
		break;
	/* Note that these are broken vs. the expected smbus API where
	 * on reads, the lenght is actually returned from the function,
	 * but I think the current API makes no sense and I don't want
	 * any driver that I haven't verified for correctness to go
	 * anywhere near a pmac i2c bus anyway ...
	 */
        case I2C_SMBUS_BLOCK_DATA:
	case I2C_SMBUS_I2C_BLOCK_DATA:
		memcpy(&data->block[0], cmd.info.data, cmd.info.datalen);
		break;
	}

	return rc;
}

static u32
smu_smbus_func(struct i2c_adapter * adapter)
{
	return I2C_FUNC_SMBUS_QUICK | I2C_FUNC_SMBUS_BYTE |
	       I2C_FUNC_SMBUS_BYTE_DATA | I2C_FUNC_SMBUS_WORD_DATA |
	       I2C_FUNC_SMBUS_BLOCK_DATA;
}

/* For now, we only handle combined mode (smbus) */
static struct i2c_algorithm smu_algorithm = {
	.smbus_xfer	= smu_smbus_xfer,
	.functionality	= smu_smbus_func,
};

static int create_iface(struct device_node *np, struct device *dev)
{
	struct smu_iface* iface;
	u32 *reg, busid;
	int rc;

	reg = (u32 *)get_property(np, "reg", NULL);
	if (reg == NULL) {
		printk(KERN_ERR "i2c-pmac-smu: can't find bus number !\n");
		return -ENXIO;
	}
	busid = *reg;

	iface = kzalloc(sizeof(struct smu_iface), GFP_KERNEL);
	if (iface == NULL) {
		printk(KERN_ERR "i2c-pmac-smu: can't allocate inteface !\n");
		return -ENOMEM;
	}
	init_completion(&iface->complete);
	iface->busid = busid;

	dev_set_drvdata(dev, iface);

	sprintf(iface->adapter.name, "smu-i2c-%02x", busid);
	iface->adapter.algo = &smu_algorithm;
	iface->adapter.algo_data = NULL;
	iface->adapter.client_register = NULL;
	iface->adapter.client_unregister = NULL;
	i2c_set_adapdata(&iface->adapter, iface);
	iface->adapter.dev.parent = dev;

	rc = i2c_add_adapter(&iface->adapter);
	if (rc) {
		printk(KERN_ERR "i2c-pamc-smu.c: Adapter %s registration "
		       "failed\n", iface->adapter.name);
		i2c_set_adapdata(&iface->adapter, NULL);
	}

	if (probe) {
		unsigned char addr;
		printk("Probe: ");
		for (addr = 0x00; addr <= 0x7f; addr++) {
			if (i2c_smbus_xfer(&iface->adapter,addr,
					   0,0,0,I2C_SMBUS_QUICK,NULL) >= 0)
				printk("%02x ", addr);
		}
		printk("\n");
	}

	printk(KERN_INFO "SMU i2c bus %x registered\n", busid);

	return 0;
}

static int dispose_iface(struct device *dev)
{
	struct smu_iface *iface = dev_get_drvdata(dev);
	int rc;

	rc = i2c_del_adapter(&iface->adapter);
	i2c_set_adapdata(&iface->adapter, NULL);
	/* We aren't that prepared to deal with this... */
	if (rc)
		printk("i2c-pmac-smu.c: Failed to remove bus %s !\n",
		       iface->adapter.name);
	dev_set_drvdata(dev, NULL);
	kfree(iface);

	return 0;
}


static int create_iface_of_platform(struct of_device* dev,
				    const struct of_device_id *match)
{
	return create_iface(dev->node, &dev->dev);
}


static int dispose_iface_of_platform(struct of_device* dev)
{
	return dispose_iface(&dev->dev);
}


static struct of_device_id i2c_smu_match[] =
{
	{
		.compatible	= "smu-i2c",
	},
	{},
};
static struct of_platform_driver i2c_smu_of_platform_driver =
{
	.name 		= "i2c-smu",
	.match_table	= i2c_smu_match,
	.probe		= create_iface_of_platform,
	.remove		= dispose_iface_of_platform
};


static int __init i2c_pmac_smu_init(void)
{
	of_register_driver(&i2c_smu_of_platform_driver);
	return 0;
}


static void __exit i2c_pmac_smu_cleanup(void)
{
	of_unregister_driver(&i2c_smu_of_platform_driver);
}

module_init(i2c_pmac_smu_init);
module_exit(i2c_pmac_smu_cleanup);
