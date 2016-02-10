/*
 * Driver for 93xx46 EEPROMs
 *
 * (C) 2011 DENX Software Engineering, Anatolij Gustschin <agust@denx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/sysfs.h>
#include <linux/eeprom_93xx46.h>

#define OP_START	0x4
#define OP_WRITE	(OP_START | 0x1)
#define OP_READ		(OP_START | 0x2)
#define ADDR_EWDS	0x00
#define ADDR_ERAL	0x20
#define ADDR_EWEN	0x30

struct eeprom_93xx46_devtype_data {
	unsigned int quirks;
};

static const struct eeprom_93xx46_devtype_data atmel_at93c46d_data = {
	.quirks = EEPROM_93XX46_QUIRK_SINGLE_WORD_READ |
		  EEPROM_93XX46_QUIRK_INSTRUCTION_LENGTH,
};

struct eeprom_93xx46_dev {
	struct spi_device *spi;
	struct eeprom_93xx46_platform_data *pdata;
	struct bin_attribute bin;
	struct mutex lock;
	int addrlen;
};

static inline bool has_quirk_single_word_read(struct eeprom_93xx46_dev *edev)
{
	return edev->pdata->quirks & EEPROM_93XX46_QUIRK_SINGLE_WORD_READ;
}

static inline bool has_quirk_instruction_length(struct eeprom_93xx46_dev *edev)
{
	return edev->pdata->quirks & EEPROM_93XX46_QUIRK_INSTRUCTION_LENGTH;
}

static ssize_t
eeprom_93xx46_bin_read(struct file *filp, struct kobject *kobj,
		       struct bin_attribute *bin_attr,
		       char *buf, loff_t off, size_t count)
{
	struct eeprom_93xx46_dev *edev;
	struct device *dev;
	ssize_t ret = 0;

	dev = kobj_to_dev(kobj);
	edev = dev_get_drvdata(dev);

	mutex_lock(&edev->lock);

	if (edev->pdata->prepare)
		edev->pdata->prepare(edev);

	while (count) {
		struct spi_message m;
		struct spi_transfer t[2] = { { 0 } };
		u16 cmd_addr = OP_READ << edev->addrlen;
		size_t nbytes = count;
		int bits;
		int err;

		if (edev->addrlen == 7) {
			cmd_addr |= off & 0x7f;
			bits = 10;
			if (has_quirk_single_word_read(edev))
				nbytes = 1;
		} else {
			cmd_addr |= (off >> 1) & 0x3f;
			bits = 9;
			if (has_quirk_single_word_read(edev))
				nbytes = 2;
		}

		dev_dbg(&edev->spi->dev, "read cmd 0x%x, %d Hz\n",
			cmd_addr, edev->spi->max_speed_hz);

		spi_message_init(&m);

		t[0].tx_buf = (char *)&cmd_addr;
		t[0].len = 2;
		t[0].bits_per_word = bits;
		spi_message_add_tail(&t[0], &m);

		t[1].rx_buf = buf;
		t[1].len = count;
		t[1].bits_per_word = 8;
		spi_message_add_tail(&t[1], &m);

		err = spi_sync(edev->spi, &m);
		/* have to wait at least Tcsl ns */
		ndelay(250);

		if (err) {
			dev_err(&edev->spi->dev, "read %zu bytes at %d: err. %d\n",
				nbytes, (int)off, err);
			ret = err;
			break;
		}

		buf += nbytes;
		off += nbytes;
		count -= nbytes;
		ret += nbytes;
	}

	if (edev->pdata->finish)
		edev->pdata->finish(edev);

	mutex_unlock(&edev->lock);
	return ret;
}

static int eeprom_93xx46_ew(struct eeprom_93xx46_dev *edev, int is_on)
{
	struct spi_message m;
	struct spi_transfer t;
	int bits, ret;
	u16 cmd_addr;

	cmd_addr = OP_START << edev->addrlen;
	if (edev->addrlen == 7) {
		cmd_addr |= (is_on ? ADDR_EWEN : ADDR_EWDS) << 1;
		bits = 10;
	} else {
		cmd_addr |= (is_on ? ADDR_EWEN : ADDR_EWDS);
		bits = 9;
	}

	if (has_quirk_instruction_length(edev)) {
		cmd_addr <<= 2;
		bits += 2;
	}

	dev_dbg(&edev->spi->dev, "ew%s cmd 0x%04x, %d bits\n",
			is_on ? "en" : "ds", cmd_addr, bits);

	spi_message_init(&m);
	memset(&t, 0, sizeof(t));

	t.tx_buf = &cmd_addr;
	t.len = 2;
	t.bits_per_word = bits;
	spi_message_add_tail(&t, &m);

	mutex_lock(&edev->lock);

	if (edev->pdata->prepare)
		edev->pdata->prepare(edev);

	ret = spi_sync(edev->spi, &m);
	/* have to wait at least Tcsl ns */
	ndelay(250);
	if (ret)
		dev_err(&edev->spi->dev, "erase/write %sable error %d\n",
			is_on ? "en" : "dis", ret);

	if (edev->pdata->finish)
		edev->pdata->finish(edev);

	mutex_unlock(&edev->lock);
	return ret;
}

static ssize_t
eeprom_93xx46_write_word(struct eeprom_93xx46_dev *edev,
			 const char *buf, unsigned off)
{
	struct spi_message m;
	struct spi_transfer t[2];
	int bits, data_len, ret;
	u16 cmd_addr;

	cmd_addr = OP_WRITE << edev->addrlen;

	if (edev->addrlen == 7) {
		cmd_addr |= off & 0x7f;
		bits = 10;
		data_len = 1;
	} else {
		cmd_addr |= (off >> 1) & 0x3f;
		bits = 9;
		data_len = 2;
	}

	dev_dbg(&edev->spi->dev, "write cmd 0x%x\n", cmd_addr);

	spi_message_init(&m);
	memset(t, 0, sizeof(t));

	t[0].tx_buf = (char *)&cmd_addr;
	t[0].len = 2;
	t[0].bits_per_word = bits;
	spi_message_add_tail(&t[0], &m);

	t[1].tx_buf = buf;
	t[1].len = data_len;
	t[1].bits_per_word = 8;
	spi_message_add_tail(&t[1], &m);

	ret = spi_sync(edev->spi, &m);
	/* have to wait program cycle time Twc ms */
	mdelay(6);
	return ret;
}

static ssize_t
eeprom_93xx46_bin_write(struct file *filp, struct kobject *kobj,
			struct bin_attribute *bin_attr,
			char *buf, loff_t off, size_t count)
{
	struct eeprom_93xx46_dev *edev;
	struct device *dev;
	int i, ret, step = 1;

	dev = kobj_to_dev(kobj);
	edev = dev_get_drvdata(dev);

	/* only write even number of bytes on 16-bit devices */
	if (edev->addrlen == 6) {
		step = 2;
		count &= ~1;
	}

	/* erase/write enable */
	ret = eeprom_93xx46_ew(edev, 1);
	if (ret)
		return ret;

	mutex_lock(&edev->lock);

	if (edev->pdata->prepare)
		edev->pdata->prepare(edev);

	for (i = 0; i < count; i += step) {
		ret = eeprom_93xx46_write_word(edev, &buf[i], off + i);
		if (ret) {
			dev_err(&edev->spi->dev, "write failed at %d: %d\n",
				(int)off + i, ret);
			break;
		}
	}

	if (edev->pdata->finish)
		edev->pdata->finish(edev);

	mutex_unlock(&edev->lock);

	/* erase/write disable */
	eeprom_93xx46_ew(edev, 0);
	return ret ? : count;
}

static int eeprom_93xx46_eral(struct eeprom_93xx46_dev *edev)
{
	struct eeprom_93xx46_platform_data *pd = edev->pdata;
	struct spi_message m;
	struct spi_transfer t;
	int bits, ret;
	u16 cmd_addr;

	cmd_addr = OP_START << edev->addrlen;
	if (edev->addrlen == 7) {
		cmd_addr |= ADDR_ERAL << 1;
		bits = 10;
	} else {
		cmd_addr |= ADDR_ERAL;
		bits = 9;
	}

	if (has_quirk_instruction_length(edev)) {
		cmd_addr <<= 2;
		bits += 2;
	}

	dev_dbg(&edev->spi->dev, "eral cmd 0x%04x, %d bits\n", cmd_addr, bits);

	spi_message_init(&m);
	memset(&t, 0, sizeof(t));

	t.tx_buf = &cmd_addr;
	t.len = 2;
	t.bits_per_word = bits;
	spi_message_add_tail(&t, &m);

	mutex_lock(&edev->lock);

	if (edev->pdata->prepare)
		edev->pdata->prepare(edev);

	ret = spi_sync(edev->spi, &m);
	if (ret)
		dev_err(&edev->spi->dev, "erase error %d\n", ret);
	/* have to wait erase cycle time Tec ms */
	mdelay(6);

	if (pd->finish)
		pd->finish(edev);

	mutex_unlock(&edev->lock);
	return ret;
}

static ssize_t eeprom_93xx46_store_erase(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t count)
{
	struct eeprom_93xx46_dev *edev = dev_get_drvdata(dev);
	int erase = 0, ret;

	sscanf(buf, "%d", &erase);
	if (erase) {
		ret = eeprom_93xx46_ew(edev, 1);
		if (ret)
			return ret;
		ret = eeprom_93xx46_eral(edev);
		if (ret)
			return ret;
		ret = eeprom_93xx46_ew(edev, 0);
		if (ret)
			return ret;
	}
	return count;
}
static DEVICE_ATTR(erase, S_IWUSR, NULL, eeprom_93xx46_store_erase);

static const struct of_device_id eeprom_93xx46_of_table[] = {
	{ .compatible = "eeprom-93xx46", },
	{ .compatible = "atmel,at93c46d", .data = &atmel_at93c46d_data, },
	{}
};
MODULE_DEVICE_TABLE(of, eeprom_93xx46_of_table);

static int eeprom_93xx46_probe_dt(struct spi_device *spi)
{
	const struct of_device_id *of_id =
		of_match_device(eeprom_93xx46_of_table, &spi->dev);
	struct device_node *np = spi->dev.of_node;
	struct eeprom_93xx46_platform_data *pd;
	u32 tmp;
	int ret;

	pd = devm_kzalloc(&spi->dev, sizeof(*pd), GFP_KERNEL);
	if (!pd)
		return -ENOMEM;

	ret = of_property_read_u32(np, "data-size", &tmp);
	if (ret < 0) {
		dev_err(&spi->dev, "data-size property not found\n");
		return ret;
	}

	if (tmp == 8) {
		pd->flags |= EE_ADDR8;
	} else if (tmp == 16) {
		pd->flags |= EE_ADDR16;
	} else {
		dev_err(&spi->dev, "invalid data-size (%d)\n", tmp);
		return -EINVAL;
	}

	if (of_property_read_bool(np, "read-only"))
		pd->flags |= EE_READONLY;

	if (of_id->data) {
		const struct eeprom_93xx46_devtype_data *data = of_id->data;

		pd->quirks = data->quirks;
	}

	spi->dev.platform_data = pd;

	return 0;
}

static int eeprom_93xx46_probe(struct spi_device *spi)
{
	struct eeprom_93xx46_platform_data *pd;
	struct eeprom_93xx46_dev *edev;
	int err;

	if (spi->dev.of_node) {
		err = eeprom_93xx46_probe_dt(spi);
		if (err < 0)
			return err;
	}

	pd = spi->dev.platform_data;
	if (!pd) {
		dev_err(&spi->dev, "missing platform data\n");
		return -ENODEV;
	}

	edev = kzalloc(sizeof(*edev), GFP_KERNEL);
	if (!edev)
		return -ENOMEM;

	if (pd->flags & EE_ADDR8)
		edev->addrlen = 7;
	else if (pd->flags & EE_ADDR16)
		edev->addrlen = 6;
	else {
		dev_err(&spi->dev, "unspecified address type\n");
		err = -EINVAL;
		goto fail;
	}

	mutex_init(&edev->lock);

	edev->spi = spi_dev_get(spi);
	edev->pdata = pd;

	sysfs_bin_attr_init(&edev->bin);
	edev->bin.attr.name = "eeprom";
	edev->bin.attr.mode = S_IRUSR;
	edev->bin.read = eeprom_93xx46_bin_read;
	edev->bin.size = 128;
	if (!(pd->flags & EE_READONLY)) {
		edev->bin.write = eeprom_93xx46_bin_write;
		edev->bin.attr.mode |= S_IWUSR;
	}

	err = sysfs_create_bin_file(&spi->dev.kobj, &edev->bin);
	if (err)
		goto fail;

	dev_info(&spi->dev, "%d-bit eeprom %s\n",
		(pd->flags & EE_ADDR8) ? 8 : 16,
		(pd->flags & EE_READONLY) ? "(readonly)" : "");

	if (!(pd->flags & EE_READONLY)) {
		if (device_create_file(&spi->dev, &dev_attr_erase))
			dev_err(&spi->dev, "can't create erase interface\n");
	}

	spi_set_drvdata(spi, edev);
	return 0;
fail:
	kfree(edev);
	return err;
}

static int eeprom_93xx46_remove(struct spi_device *spi)
{
	struct eeprom_93xx46_dev *edev = spi_get_drvdata(spi);

	if (!(edev->pdata->flags & EE_READONLY))
		device_remove_file(&spi->dev, &dev_attr_erase);

	sysfs_remove_bin_file(&spi->dev.kobj, &edev->bin);
	kfree(edev);
	return 0;
}

static struct spi_driver eeprom_93xx46_driver = {
	.driver = {
		.name	= "93xx46",
		.of_match_table = of_match_ptr(eeprom_93xx46_of_table),
	},
	.probe		= eeprom_93xx46_probe,
	.remove		= eeprom_93xx46_remove,
};

module_spi_driver(eeprom_93xx46_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Driver for 93xx46 EEPROMs");
MODULE_AUTHOR("Anatolij Gustschin <agust@denx.de>");
MODULE_ALIAS("spi:93xx46");
