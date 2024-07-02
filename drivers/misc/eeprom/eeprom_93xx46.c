// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for 93xx46 EEPROMs
 *
 * (C) 2011 DENX Software Engineering, Anatolij Gustschin <agust@denx.de>
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/kernel.h>
#include <linux/log2.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/nvmem-provider.h>
#include <linux/eeprom_93xx46.h>

#define OP_START	0x4
#define OP_WRITE	(OP_START | 0x1)
#define OP_READ		(OP_START | 0x2)
#define ADDR_EWDS	0x00
#define ADDR_ERAL	0x20
#define ADDR_EWEN	0x30

struct eeprom_93xx46_devtype_data {
	unsigned int quirks;
	unsigned char flags;
};

static const struct eeprom_93xx46_devtype_data at93c46_data = {
	.flags = EE_SIZE1K,
};

static const struct eeprom_93xx46_devtype_data at93c56_data = {
	.flags = EE_SIZE2K,
};

static const struct eeprom_93xx46_devtype_data at93c66_data = {
	.flags = EE_SIZE4K,
};

static const struct eeprom_93xx46_devtype_data atmel_at93c46d_data = {
	.flags = EE_SIZE1K,
	.quirks = EEPROM_93XX46_QUIRK_SINGLE_WORD_READ |
		  EEPROM_93XX46_QUIRK_INSTRUCTION_LENGTH,
};

static const struct eeprom_93xx46_devtype_data microchip_93lc46b_data = {
	.flags = EE_SIZE1K,
	.quirks = EEPROM_93XX46_QUIRK_EXTRA_READ_CYCLE,
};

struct eeprom_93xx46_dev {
	struct spi_device *spi;
	struct eeprom_93xx46_platform_data *pdata;
	struct mutex lock;
	struct nvmem_config nvmem_config;
	struct nvmem_device *nvmem;
	int addrlen;
	int size;
};

static inline bool has_quirk_single_word_read(struct eeprom_93xx46_dev *edev)
{
	return edev->pdata->quirks & EEPROM_93XX46_QUIRK_SINGLE_WORD_READ;
}

static inline bool has_quirk_instruction_length(struct eeprom_93xx46_dev *edev)
{
	return edev->pdata->quirks & EEPROM_93XX46_QUIRK_INSTRUCTION_LENGTH;
}

static inline bool has_quirk_extra_read_cycle(struct eeprom_93xx46_dev *edev)
{
	return edev->pdata->quirks & EEPROM_93XX46_QUIRK_EXTRA_READ_CYCLE;
}

static int eeprom_93xx46_read(void *priv, unsigned int off,
			      void *val, size_t count)
{
	struct eeprom_93xx46_dev *edev = priv;
	char *buf = val;
	int err = 0;
	int bits;

	if (unlikely(off >= edev->size))
		return 0;
	if ((off + count) > edev->size)
		count = edev->size - off;
	if (unlikely(!count))
		return count;

	mutex_lock(&edev->lock);

	if (edev->pdata->prepare)
		edev->pdata->prepare(edev);

	/* The opcode in front of the address is three bits. */
	bits = edev->addrlen + 3;

	while (count) {
		struct spi_message m;
		struct spi_transfer t[2] = { { 0 } };
		u16 cmd_addr = OP_READ << edev->addrlen;
		size_t nbytes = count;

		if (edev->pdata->flags & EE_ADDR8) {
			cmd_addr |= off;
			if (has_quirk_single_word_read(edev))
				nbytes = 1;
		} else {
			cmd_addr |= (off >> 1);
			if (has_quirk_single_word_read(edev))
				nbytes = 2;
		}

		dev_dbg(&edev->spi->dev, "read cmd 0x%x, %d Hz\n",
			cmd_addr, edev->spi->max_speed_hz);

		if (has_quirk_extra_read_cycle(edev)) {
			cmd_addr <<= 1;
			bits += 1;
		}

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
			break;
		}

		buf += nbytes;
		off += nbytes;
		count -= nbytes;
	}

	if (edev->pdata->finish)
		edev->pdata->finish(edev);

	mutex_unlock(&edev->lock);

	return err;
}

static int eeprom_93xx46_ew(struct eeprom_93xx46_dev *edev, int is_on)
{
	struct spi_message m;
	struct spi_transfer t;
	int bits, ret;
	u16 cmd_addr;

	/* The opcode in front of the address is three bits. */
	bits = edev->addrlen + 3;

	cmd_addr = OP_START << edev->addrlen;
	if (edev->pdata->flags & EE_ADDR8)
		cmd_addr |= (is_on ? ADDR_EWEN : ADDR_EWDS) << 1;
	else
		cmd_addr |= (is_on ? ADDR_EWEN : ADDR_EWDS);

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

	if (unlikely(off >= edev->size))
		return -EINVAL;

	/* The opcode in front of the address is three bits. */
	bits = edev->addrlen + 3;

	cmd_addr = OP_WRITE << edev->addrlen;

	if (edev->pdata->flags & EE_ADDR8) {
		cmd_addr |= off;
		data_len = 1;
	} else {
		cmd_addr |= (off >> 1);
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

static int eeprom_93xx46_write(void *priv, unsigned int off,
				   void *val, size_t count)
{
	struct eeprom_93xx46_dev *edev = priv;
	char *buf = val;
	int i, ret, step = 1;

	if (unlikely(off >= edev->size))
		return -EFBIG;
	if ((off + count) > edev->size)
		count = edev->size - off;
	if (unlikely(!count))
		return count;

	/* only write even number of bytes on 16-bit devices */
	if (edev->pdata->flags & EE_ADDR16) {
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
	return ret;
}

static int eeprom_93xx46_eral(struct eeprom_93xx46_dev *edev)
{
	struct eeprom_93xx46_platform_data *pd = edev->pdata;
	struct spi_message m;
	struct spi_transfer t;
	int bits, ret;
	u16 cmd_addr;

	/* The opcode in front of the address is three bits. */
	bits = edev->addrlen + 3;

	cmd_addr = OP_START << edev->addrlen;
	if (edev->pdata->flags & EE_ADDR8)
		cmd_addr |= ADDR_ERAL << 1;
	else
		cmd_addr |= ADDR_ERAL;

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

static void select_assert(void *context)
{
	struct eeprom_93xx46_dev *edev = context;

	gpiod_set_value_cansleep(edev->pdata->select, 1);
}

static void select_deassert(void *context)
{
	struct eeprom_93xx46_dev *edev = context;

	gpiod_set_value_cansleep(edev->pdata->select, 0);
}

static const struct of_device_id eeprom_93xx46_of_table[] = {
	{ .compatible = "eeprom-93xx46", .data = &at93c46_data, },
	{ .compatible = "atmel,at93c46", .data = &at93c46_data, },
	{ .compatible = "atmel,at93c46d", .data = &atmel_at93c46d_data, },
	{ .compatible = "atmel,at93c56", .data = &at93c56_data, },
	{ .compatible = "atmel,at93c66", .data = &at93c66_data, },
	{ .compatible = "microchip,93lc46b", .data = &microchip_93lc46b_data, },
	{}
};
MODULE_DEVICE_TABLE(of, eeprom_93xx46_of_table);

static const struct spi_device_id eeprom_93xx46_spi_ids[] = {
	{ .name = "eeprom-93xx46",
	  .driver_data = (kernel_ulong_t)&at93c46_data, },
	{ .name = "at93c46",
	  .driver_data = (kernel_ulong_t)&at93c46_data, },
	{ .name = "at93c46d",
	  .driver_data = (kernel_ulong_t)&atmel_at93c46d_data, },
	{ .name = "at93c56",
	  .driver_data = (kernel_ulong_t)&at93c56_data, },
	{ .name = "at93c66",
	  .driver_data = (kernel_ulong_t)&at93c66_data, },
	{ .name = "93lc46b",
	  .driver_data = (kernel_ulong_t)&microchip_93lc46b_data, },
	{}
};
MODULE_DEVICE_TABLE(spi, eeprom_93xx46_spi_ids);

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

	pd->select = devm_gpiod_get_optional(&spi->dev, "select",
					     GPIOD_OUT_LOW);
	if (IS_ERR(pd->select))
		return PTR_ERR(pd->select);

	pd->prepare = select_assert;
	pd->finish = select_deassert;
	gpiod_direction_output(pd->select, 0);

	if (of_id->data) {
		const struct eeprom_93xx46_devtype_data *data = of_id->data;

		pd->quirks = data->quirks;
		pd->flags |= data->flags;
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

	edev = devm_kzalloc(&spi->dev, sizeof(*edev), GFP_KERNEL);
	if (!edev)
		return -ENOMEM;

	if (pd->flags & EE_SIZE1K)
		edev->size = 128;
	else if (pd->flags & EE_SIZE2K)
		edev->size = 256;
	else if (pd->flags & EE_SIZE4K)
		edev->size = 512;
	else {
		dev_err(&spi->dev, "unspecified size\n");
		return -EINVAL;
	}

	if (pd->flags & EE_ADDR8)
		edev->addrlen = ilog2(edev->size);
	else if (pd->flags & EE_ADDR16)
		edev->addrlen = ilog2(edev->size) - 1;
	else {
		dev_err(&spi->dev, "unspecified address type\n");
		return -EINVAL;
	}

	mutex_init(&edev->lock);

	edev->spi = spi;
	edev->pdata = pd;

	edev->nvmem_config.type = NVMEM_TYPE_EEPROM;
	edev->nvmem_config.name = dev_name(&spi->dev);
	edev->nvmem_config.dev = &spi->dev;
	edev->nvmem_config.read_only = pd->flags & EE_READONLY;
	edev->nvmem_config.root_only = true;
	edev->nvmem_config.owner = THIS_MODULE;
	edev->nvmem_config.compat = true;
	edev->nvmem_config.base_dev = &spi->dev;
	edev->nvmem_config.reg_read = eeprom_93xx46_read;
	edev->nvmem_config.reg_write = eeprom_93xx46_write;
	edev->nvmem_config.priv = edev;
	edev->nvmem_config.stride = 4;
	edev->nvmem_config.word_size = 1;
	edev->nvmem_config.size = edev->size;

	edev->nvmem = devm_nvmem_register(&spi->dev, &edev->nvmem_config);
	if (IS_ERR(edev->nvmem))
		return PTR_ERR(edev->nvmem);

	dev_info(&spi->dev, "%d-bit eeprom containing %d bytes %s\n",
		(pd->flags & EE_ADDR8) ? 8 : 16,
		edev->size,
		(pd->flags & EE_READONLY) ? "(readonly)" : "");

	if (!(pd->flags & EE_READONLY)) {
		if (device_create_file(&spi->dev, &dev_attr_erase))
			dev_err(&spi->dev, "can't create erase interface\n");
	}

	spi_set_drvdata(spi, edev);
	return 0;
}

static void eeprom_93xx46_remove(struct spi_device *spi)
{
	struct eeprom_93xx46_dev *edev = spi_get_drvdata(spi);

	if (!(edev->pdata->flags & EE_READONLY))
		device_remove_file(&spi->dev, &dev_attr_erase);
}

static struct spi_driver eeprom_93xx46_driver = {
	.driver = {
		.name	= "93xx46",
		.of_match_table = of_match_ptr(eeprom_93xx46_of_table),
	},
	.probe		= eeprom_93xx46_probe,
	.remove		= eeprom_93xx46_remove,
	.id_table	= eeprom_93xx46_spi_ids,
};

module_spi_driver(eeprom_93xx46_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Driver for 93xx46 EEPROMs");
MODULE_AUTHOR("Anatolij Gustschin <agust@denx.de>");
MODULE_ALIAS("spi:93xx46");
MODULE_ALIAS("spi:eeprom-93xx46");
MODULE_ALIAS("spi:93lc46b");
