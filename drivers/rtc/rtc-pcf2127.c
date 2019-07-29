// SPDX-License-Identifier: GPL-2.0-only
/*
 * An I2C and SPI driver for the NXP PCF2127/29 RTC
 * Copyright 2013 Til-Technologies
 *
 * Author: Renaud Cerrato <r.cerrato@til-technologies.fr>
 *
 * based on the other drivers in this same directory.
 *
 * Datasheet: http://cache.nxp.com/documents/data_sheet/PCF2127.pdf
 */

#include <linux/i2c.h>
#include <linux/spi/spi.h>
#include <linux/bcd.h>
#include <linux/rtc.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regmap.h>

#define PCF2127_REG_CTRL1       (0x00)  /* Control Register 1 */
#define PCF2127_REG_CTRL2       (0x01)  /* Control Register 2 */

#define PCF2127_REG_CTRL3       (0x02)  /* Control Register 3 */
#define PCF2127_REG_CTRL3_BLF		BIT(2)

#define PCF2127_REG_SC          (0x03)  /* datetime */
#define PCF2127_REG_MN          (0x04)
#define PCF2127_REG_HR          (0x05)
#define PCF2127_REG_DM          (0x06)
#define PCF2127_REG_DW          (0x07)
#define PCF2127_REG_MO          (0x08)
#define PCF2127_REG_YR          (0x09)

/* the pcf2127 has 512 bytes nvmem, pcf2129 doesn't */
#define PCF2127_REG_RAM_addr_MSB       0x1a
#define PCF2127_REG_RAM_wrt_cmd        0x1c
#define PCF2127_REG_RAM_rd_cmd         0x1d

#define PCF2127_OSF             BIT(7)  /* Oscillator Fail flag */

struct pcf2127 {
	struct rtc_device *rtc;
	struct regmap *regmap;
};

/*
 * In the routines that deal directly with the pcf2127 hardware, we use
 * rtc_time -- month 0-11, hour 0-23, yr = calendar year-epoch.
 */
static int pcf2127_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct pcf2127 *pcf2127 = dev_get_drvdata(dev);
	unsigned char buf[10];
	int ret;
	int i;

	for (i = 0; i <= PCF2127_REG_CTRL3; i++) {
		ret = regmap_read(pcf2127->regmap, PCF2127_REG_CTRL1 + i,
				  (unsigned int *)(buf + i));
		if (ret) {
			dev_err(dev, "%s: read error\n", __func__);
			return ret;
		}
	}

	ret = regmap_bulk_read(pcf2127->regmap, PCF2127_REG_SC,
			       (buf + PCF2127_REG_SC),
			       ARRAY_SIZE(buf) - PCF2127_REG_SC);
	if (ret) {
		dev_err(dev, "%s: read error\n", __func__);
		return ret;
	}

	if (buf[PCF2127_REG_CTRL3] & PCF2127_REG_CTRL3_BLF)
		dev_info(dev,
			"low voltage detected, check/replace RTC battery.\n");

	if (buf[PCF2127_REG_SC] & PCF2127_OSF) {
		/*
		 * no need clear the flag here,
		 * it will be cleared once the new date is saved
		 */
		dev_warn(dev,
			 "oscillator stop detected, date/time is not reliable\n");
		return -EINVAL;
	}

	dev_dbg(dev,
		"%s: raw data is cr1=%02x, cr2=%02x, cr3=%02x, "
		"sec=%02x, min=%02x, hr=%02x, "
		"mday=%02x, wday=%02x, mon=%02x, year=%02x\n",
		__func__,
		buf[0], buf[1], buf[2],
		buf[3], buf[4], buf[5],
		buf[6], buf[7], buf[8], buf[9]);


	tm->tm_sec = bcd2bin(buf[PCF2127_REG_SC] & 0x7F);
	tm->tm_min = bcd2bin(buf[PCF2127_REG_MN] & 0x7F);
	tm->tm_hour = bcd2bin(buf[PCF2127_REG_HR] & 0x3F); /* rtc hr 0-23 */
	tm->tm_mday = bcd2bin(buf[PCF2127_REG_DM] & 0x3F);
	tm->tm_wday = buf[PCF2127_REG_DW] & 0x07;
	tm->tm_mon = bcd2bin(buf[PCF2127_REG_MO] & 0x1F) - 1; /* rtc mn 1-12 */
	tm->tm_year = bcd2bin(buf[PCF2127_REG_YR]);
	if (tm->tm_year < 70)
		tm->tm_year += 100;	/* assume we are in 1970...2069 */

	dev_dbg(dev, "%s: tm is secs=%d, mins=%d, hours=%d, "
		"mday=%d, mon=%d, year=%d, wday=%d\n",
		__func__,
		tm->tm_sec, tm->tm_min, tm->tm_hour,
		tm->tm_mday, tm->tm_mon, tm->tm_year, tm->tm_wday);

	return 0;
}

static int pcf2127_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct pcf2127 *pcf2127 = dev_get_drvdata(dev);
	unsigned char buf[7];
	int i = 0, err;

	dev_dbg(dev, "%s: secs=%d, mins=%d, hours=%d, "
		"mday=%d, mon=%d, year=%d, wday=%d\n",
		__func__,
		tm->tm_sec, tm->tm_min, tm->tm_hour,
		tm->tm_mday, tm->tm_mon, tm->tm_year, tm->tm_wday);

	/* hours, minutes and seconds */
	buf[i++] = bin2bcd(tm->tm_sec);	/* this will also clear OSF flag */
	buf[i++] = bin2bcd(tm->tm_min);
	buf[i++] = bin2bcd(tm->tm_hour);
	buf[i++] = bin2bcd(tm->tm_mday);
	buf[i++] = tm->tm_wday & 0x07;

	/* month, 1 - 12 */
	buf[i++] = bin2bcd(tm->tm_mon + 1);

	/* year */
	buf[i++] = bin2bcd(tm->tm_year % 100);

	/* write register's data */
	err = regmap_bulk_write(pcf2127->regmap, PCF2127_REG_SC, buf, i);
	if (err) {
		dev_err(dev,
			"%s: err=%d", __func__, err);
		return err;
	}

	return 0;
}

#ifdef CONFIG_RTC_INTF_DEV
static int pcf2127_rtc_ioctl(struct device *dev,
				unsigned int cmd, unsigned long arg)
{
	struct pcf2127 *pcf2127 = dev_get_drvdata(dev);
	int touser;
	int ret;

	switch (cmd) {
	case RTC_VL_READ:
		ret = regmap_read(pcf2127->regmap, PCF2127_REG_CTRL3, &touser);
		if (ret)
			return ret;

		touser = touser & PCF2127_REG_CTRL3_BLF ? 1 : 0;

		if (copy_to_user((void __user *)arg, &touser, sizeof(int)))
			return -EFAULT;
		return 0;
	default:
		return -ENOIOCTLCMD;
	}
}
#else
#define pcf2127_rtc_ioctl NULL
#endif

static const struct rtc_class_ops pcf2127_rtc_ops = {
	.ioctl		= pcf2127_rtc_ioctl,
	.read_time	= pcf2127_rtc_read_time,
	.set_time	= pcf2127_rtc_set_time,
};

static int pcf2127_nvmem_read(void *priv, unsigned int offset,
			      void *val, size_t bytes)
{
	struct pcf2127 *pcf2127 = priv;
	int ret;
	unsigned char offsetbuf[] = { offset >> 8, offset };

	ret = regmap_bulk_write(pcf2127->regmap, PCF2127_REG_RAM_addr_MSB,
				offsetbuf, 2);
	if (ret)
		return ret;

	ret = regmap_bulk_read(pcf2127->regmap, PCF2127_REG_RAM_rd_cmd,
			       val, bytes);

	return ret ?: bytes;
}

static int pcf2127_nvmem_write(void *priv, unsigned int offset,
			       void *val, size_t bytes)
{
	struct pcf2127 *pcf2127 = priv;
	int ret;
	unsigned char offsetbuf[] = { offset >> 8, offset };

	ret = regmap_bulk_write(pcf2127->regmap, PCF2127_REG_RAM_addr_MSB,
				offsetbuf, 2);
	if (ret)
		return ret;

	ret = regmap_bulk_write(pcf2127->regmap, PCF2127_REG_RAM_wrt_cmd,
				val, bytes);

	return ret ?: bytes;
}

static int pcf2127_probe(struct device *dev, struct regmap *regmap,
			const char *name, bool has_nvmem)
{
	struct pcf2127 *pcf2127;
	int ret = 0;

	dev_dbg(dev, "%s\n", __func__);

	pcf2127 = devm_kzalloc(dev, sizeof(*pcf2127), GFP_KERNEL);
	if (!pcf2127)
		return -ENOMEM;

	pcf2127->regmap = regmap;

	dev_set_drvdata(dev, pcf2127);

	pcf2127->rtc = devm_rtc_device_register(dev, name, &pcf2127_rtc_ops,
						THIS_MODULE);
	if (IS_ERR(pcf2127->rtc))
		return PTR_ERR(pcf2127->rtc);

	if (has_nvmem) {
		struct nvmem_config nvmem_cfg = {
			.priv = pcf2127,
			.reg_read = pcf2127_nvmem_read,
			.reg_write = pcf2127_nvmem_write,
			.size = 512,
		};

		ret = rtc_nvmem_register(pcf2127->rtc, &nvmem_cfg);
	}

	return ret;
}

#ifdef CONFIG_OF
static const struct of_device_id pcf2127_of_match[] = {
	{ .compatible = "nxp,pcf2127" },
	{ .compatible = "nxp,pcf2129" },
	{}
};
MODULE_DEVICE_TABLE(of, pcf2127_of_match);
#endif

#if IS_ENABLED(CONFIG_I2C)

static int pcf2127_i2c_write(void *context, const void *data, size_t count)
{
	struct device *dev = context;
	struct i2c_client *client = to_i2c_client(dev);
	int ret;

	ret = i2c_master_send(client, data, count);
	if (ret != count)
		return ret < 0 ? ret : -EIO;

	return 0;
}

static int pcf2127_i2c_gather_write(void *context,
				const void *reg, size_t reg_size,
				const void *val, size_t val_size)
{
	struct device *dev = context;
	struct i2c_client *client = to_i2c_client(dev);
	int ret;
	void *buf;

	if (WARN_ON(reg_size != 1))
		return -EINVAL;

	buf = kmalloc(val_size + 1, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	memcpy(buf, reg, 1);
	memcpy(buf + 1, val, val_size);

	ret = i2c_master_send(client, buf, val_size + 1);

	kfree(buf);

	if (ret != val_size + 1)
		return ret < 0 ? ret : -EIO;

	return 0;
}

static int pcf2127_i2c_read(void *context, const void *reg, size_t reg_size,
				void *val, size_t val_size)
{
	struct device *dev = context;
	struct i2c_client *client = to_i2c_client(dev);
	int ret;

	if (WARN_ON(reg_size != 1))
		return -EINVAL;

	ret = i2c_master_send(client, reg, 1);
	if (ret != 1)
		return ret < 0 ? ret : -EIO;

	ret = i2c_master_recv(client, val, val_size);
	if (ret != val_size)
		return ret < 0 ? ret : -EIO;

	return 0;
}

/*
 * The reason we need this custom regmap_bus instead of using regmap_init_i2c()
 * is that the STOP condition is required between set register address and
 * read register data when reading from registers.
 */
static const struct regmap_bus pcf2127_i2c_regmap = {
	.write = pcf2127_i2c_write,
	.gather_write = pcf2127_i2c_gather_write,
	.read = pcf2127_i2c_read,
};

static struct i2c_driver pcf2127_i2c_driver;

static int pcf2127_i2c_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	struct regmap *regmap;
	static const struct regmap_config config = {
		.reg_bits = 8,
		.val_bits = 8,
	};

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -ENODEV;

	regmap = devm_regmap_init(&client->dev, &pcf2127_i2c_regmap,
					&client->dev, &config);
	if (IS_ERR(regmap)) {
		dev_err(&client->dev, "%s: regmap allocation failed: %ld\n",
			__func__, PTR_ERR(regmap));
		return PTR_ERR(regmap);
	}

	return pcf2127_probe(&client->dev, regmap,
			     pcf2127_i2c_driver.driver.name, id->driver_data);
}

static const struct i2c_device_id pcf2127_i2c_id[] = {
	{ "pcf2127", 1 },
	{ "pcf2129", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, pcf2127_i2c_id);

static struct i2c_driver pcf2127_i2c_driver = {
	.driver		= {
		.name	= "rtc-pcf2127-i2c",
		.of_match_table = of_match_ptr(pcf2127_of_match),
	},
	.probe		= pcf2127_i2c_probe,
	.id_table	= pcf2127_i2c_id,
};

static int pcf2127_i2c_register_driver(void)
{
	return i2c_add_driver(&pcf2127_i2c_driver);
}

static void pcf2127_i2c_unregister_driver(void)
{
	i2c_del_driver(&pcf2127_i2c_driver);
}

#else

static int pcf2127_i2c_register_driver(void)
{
	return 0;
}

static void pcf2127_i2c_unregister_driver(void)
{
}

#endif

#if IS_ENABLED(CONFIG_SPI_MASTER)

static struct spi_driver pcf2127_spi_driver;

static int pcf2127_spi_probe(struct spi_device *spi)
{
	static const struct regmap_config config = {
		.reg_bits = 8,
		.val_bits = 8,
		.read_flag_mask = 0xa0,
		.write_flag_mask = 0x20,
	};
	struct regmap *regmap;

	regmap = devm_regmap_init_spi(spi, &config);
	if (IS_ERR(regmap)) {
		dev_err(&spi->dev, "%s: regmap allocation failed: %ld\n",
			__func__, PTR_ERR(regmap));
		return PTR_ERR(regmap);
	}

	return pcf2127_probe(&spi->dev, regmap, pcf2127_spi_driver.driver.name,
			     spi_get_device_id(spi)->driver_data);
}

static const struct spi_device_id pcf2127_spi_id[] = {
	{ "pcf2127", 1 },
	{ "pcf2129", 0 },
	{ }
};
MODULE_DEVICE_TABLE(spi, pcf2127_spi_id);

static struct spi_driver pcf2127_spi_driver = {
	.driver		= {
		.name	= "rtc-pcf2127-spi",
		.of_match_table = of_match_ptr(pcf2127_of_match),
	},
	.probe		= pcf2127_spi_probe,
	.id_table	= pcf2127_spi_id,
};

static int pcf2127_spi_register_driver(void)
{
	return spi_register_driver(&pcf2127_spi_driver);
}

static void pcf2127_spi_unregister_driver(void)
{
	spi_unregister_driver(&pcf2127_spi_driver);
}

#else

static int pcf2127_spi_register_driver(void)
{
	return 0;
}

static void pcf2127_spi_unregister_driver(void)
{
}

#endif

static int __init pcf2127_init(void)
{
	int ret;

	ret = pcf2127_i2c_register_driver();
	if (ret) {
		pr_err("Failed to register pcf2127 i2c driver: %d\n", ret);
		return ret;
	}

	ret = pcf2127_spi_register_driver();
	if (ret) {
		pr_err("Failed to register pcf2127 spi driver: %d\n", ret);
		pcf2127_i2c_unregister_driver();
	}

	return ret;
}
module_init(pcf2127_init)

static void __exit pcf2127_exit(void)
{
	pcf2127_spi_unregister_driver();
	pcf2127_i2c_unregister_driver();
}
module_exit(pcf2127_exit)

MODULE_AUTHOR("Renaud Cerrato <r.cerrato@til-technologies.fr>");
MODULE_DESCRIPTION("NXP PCF2127/29 RTC driver");
MODULE_LICENSE("GPL v2");
