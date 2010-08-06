/*
 * PISMO memory driver - http://www.pismoworld.org/
 *
 * For ARM Realview and Versatile platforms
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/mtd/physmap.h>
#include <linux/mtd/plat-ram.h>
#include <linux/mtd/pismo.h>

#define PISMO_NUM_CS	5

struct pismo_cs_block {
	u8	type;
	u8	width;
	__le16	access;
	__le32	size;
	u32	reserved[2];
	char	device[32];
} __packed;

struct pismo_eeprom {
	struct pismo_cs_block cs[PISMO_NUM_CS];
	char	board[15];
	u8	sum;
} __packed;

struct pismo_mem {
	phys_addr_t base;
	u32	size;
	u16	access;
	u8	width;
	u8	type;
};

struct pismo_data {
	struct i2c_client	*client;
	void			(*vpp)(void *, int);
	void			*vpp_data;
	struct platform_device	*dev[PISMO_NUM_CS];
};

/* FIXME: set_vpp could do with a better calling convention */
static struct pismo_data *vpp_pismo;
static DEFINE_MUTEX(pismo_mutex);

static int pismo_setvpp_probe_fix(struct pismo_data *pismo)
{
	mutex_lock(&pismo_mutex);
	if (vpp_pismo) {
		mutex_unlock(&pismo_mutex);
		kfree(pismo);
		return -EBUSY;
	}
	vpp_pismo = pismo;
	mutex_unlock(&pismo_mutex);
	return 0;
}

static void pismo_setvpp_remove_fix(struct pismo_data *pismo)
{
	mutex_lock(&pismo_mutex);
	if (vpp_pismo == pismo)
		vpp_pismo = NULL;
	mutex_unlock(&pismo_mutex);
}

static void pismo_set_vpp(struct map_info *map, int on)
{
	struct pismo_data *pismo = vpp_pismo;

	pismo->vpp(pismo->vpp_data, on);
}
/* end of hack */


static unsigned int __devinit pismo_width_to_bytes(unsigned int width)
{
	width &= 15;
	if (width > 2)
		return 0;
	return 1 << width;
}

static int __devinit pismo_eeprom_read(struct i2c_client *client, void *buf,
	u8 addr, size_t size)
{
	int ret;
	struct i2c_msg msg[] = {
		{
			.addr = client->addr,
			.len = sizeof(addr),
			.buf = &addr,
		}, {
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = size,
			.buf = buf,
		},
	};

	ret = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));

	return ret == ARRAY_SIZE(msg) ? size : -EIO;
}

static int __devinit pismo_add_device(struct pismo_data *pismo, int i,
	struct pismo_mem *region, const char *name, void *pdata, size_t psize)
{
	struct platform_device *dev;
	struct resource res = { };
	phys_addr_t base = region->base;
	int ret;

	if (base == ~0)
		return -ENXIO;

	res.start = base;
	res.end = base + region->size - 1;
	res.flags = IORESOURCE_MEM;

	dev = platform_device_alloc(name, i);
	if (!dev)
		return -ENOMEM;
	dev->dev.parent = &pismo->client->dev;

	do {
		ret = platform_device_add_resources(dev, &res, 1);
		if (ret)
			break;

		ret = platform_device_add_data(dev, pdata, psize);
		if (ret)
			break;

		ret = platform_device_add(dev);
		if (ret)
			break;

		pismo->dev[i] = dev;
		return 0;
	} while (0);

	platform_device_put(dev);
	return ret;
}

static int __devinit pismo_add_nor(struct pismo_data *pismo, int i,
	struct pismo_mem *region)
{
	struct physmap_flash_data data = {
		.width = region->width,
	};

	if (pismo->vpp)
		data.set_vpp = pismo_set_vpp;

	return pismo_add_device(pismo, i, region, "physmap-flash",
		&data, sizeof(data));
}

static int __devinit pismo_add_sram(struct pismo_data *pismo, int i,
	struct pismo_mem *region)
{
	struct platdata_mtd_ram data = {
		.bankwidth = region->width,
	};

	return pismo_add_device(pismo, i, region, "mtd-ram",
		&data, sizeof(data));
}

static void __devinit pismo_add_one(struct pismo_data *pismo, int i,
	const struct pismo_cs_block *cs, phys_addr_t base)
{
	struct device *dev = &pismo->client->dev;
	struct pismo_mem region;

	region.base = base;
	region.type = cs->type;
	region.width = pismo_width_to_bytes(cs->width);
	region.access = le16_to_cpu(cs->access);
	region.size = le32_to_cpu(cs->size);

	if (region.width == 0) {
		dev_err(dev, "cs%u: bad width: %02x, ignoring\n", i, cs->width);
		return;
	}

	/*
	 * FIXME: may need to the platforms memory controller here, but at
	 * the moment we assume that it has already been correctly setup.
	 * The memory controller can also tell us the base address as well.
	 */

	dev_info(dev, "cs%u: %.32s: type %02x access %u00ps size %uK\n",
		i, cs->device, region.type, region.access, region.size / 1024);

	switch (region.type) {
	case 0:
		break;
	case 1:
		/* static DOC */
		break;
	case 2:
		/* static NOR */
		pismo_add_nor(pismo, i, &region);
		break;
	case 3:
		/* static RAM */
		pismo_add_sram(pismo, i, &region);
		break;
	}
}

static int __devexit pismo_remove(struct i2c_client *client)
{
	struct pismo_data *pismo = i2c_get_clientdata(client);
	int i;

	for (i = 0; i < ARRAY_SIZE(pismo->dev); i++)
		platform_device_unregister(pismo->dev[i]);

	/* FIXME: set_vpp needs saner arguments */
	pismo_setvpp_remove_fix(pismo);

	kfree(pismo);

	return 0;
}

static int __devinit pismo_probe(struct i2c_client *client,
				 const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct pismo_pdata *pdata = client->dev.platform_data;
	struct pismo_eeprom eeprom;
	struct pismo_data *pismo;
	int ret, i;

	if (!i2c_check_functionality(adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "functionality mismatch\n");
		return -EIO;
	}

	pismo = kzalloc(sizeof(*pismo), GFP_KERNEL);
	if (!pismo)
		return -ENOMEM;

	/* FIXME: set_vpp needs saner arguments */
	ret = pismo_setvpp_probe_fix(pismo);
	if (ret)
		return ret;

	pismo->client = client;
	if (pdata) {
		pismo->vpp = pdata->set_vpp;
		pismo->vpp_data = pdata->vpp_data;
	}
	i2c_set_clientdata(client, pismo);

	ret = pismo_eeprom_read(client, &eeprom, 0, sizeof(eeprom));
	if (ret < 0) {
		dev_err(&client->dev, "error reading EEPROM: %d\n", ret);
		goto exit_free;
	}

	dev_info(&client->dev, "%.15s board found\n", eeprom.board);

	for (i = 0; i < ARRAY_SIZE(eeprom.cs); i++)
		if (eeprom.cs[i].type != 0xff)
			pismo_add_one(pismo, i, &eeprom.cs[i],
				      pdata->cs_addrs[i]);

	return 0;

 exit_free:
	kfree(pismo);
	return ret;
}

static const struct i2c_device_id pismo_id[] = {
	{ "pismo" },
	{ },
};
MODULE_DEVICE_TABLE(i2c, pismo_id);

static struct i2c_driver pismo_driver = {
	.driver	= {
		.name	= "pismo",
		.owner	= THIS_MODULE,
	},
	.probe		= pismo_probe,
	.remove		= __devexit_p(pismo_remove),
	.id_table	= pismo_id,
};

static int __init pismo_init(void)
{
	BUILD_BUG_ON(sizeof(struct pismo_cs_block) != 48);
	BUILD_BUG_ON(sizeof(struct pismo_eeprom) != 256);

	return i2c_add_driver(&pismo_driver);
}
module_init(pismo_init);

static void __exit pismo_exit(void)
{
	i2c_del_driver(&pismo_driver);
}
module_exit(pismo_exit);

MODULE_AUTHOR("Russell King <linux@arm.linux.org.uk>");
MODULE_DESCRIPTION("PISMO memory driver");
MODULE_LICENSE("GPL");
