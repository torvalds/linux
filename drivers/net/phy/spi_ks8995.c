// SPDX-License-Identifier: GPL-2.0
/*
 * SPI driver for Micrel/Kendin KS8995M and KSZ8864RMN ethernet switches
 *
 * Copyright (C) 2008 Gabor Juhos <juhosg at openwrt.org>
 *
 * This file was based on: drivers/spi/at25.c
 *     Copyright (C) 2006 David Brownell
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/of.h>

#include <linux/spi/spi.h>

#define DRV_VERSION		"0.1.1"
#define DRV_DESC		"Micrel KS8995 Ethernet switch SPI driver"

/* ------------------------------------------------------------------------ */

#define KS8995_REG_ID0		0x00    /* Chip ID0 */
#define KS8995_REG_ID1		0x01    /* Chip ID1 */

#define KS8995_REG_GC0		0x02    /* Global Control 0 */
#define KS8995_REG_GC1		0x03    /* Global Control 1 */
#define KS8995_REG_GC2		0x04    /* Global Control 2 */
#define KS8995_REG_GC3		0x05    /* Global Control 3 */
#define KS8995_REG_GC4		0x06    /* Global Control 4 */
#define KS8995_REG_GC5		0x07    /* Global Control 5 */
#define KS8995_REG_GC6		0x08    /* Global Control 6 */
#define KS8995_REG_GC7		0x09    /* Global Control 7 */
#define KS8995_REG_GC8		0x0a    /* Global Control 8 */
#define KS8995_REG_GC9		0x0b    /* Global Control 9 */

#define KS8995_REG_PC(p, r)	((0x10 * p) + r)	 /* Port Control */
#define KS8995_REG_PS(p, r)	((0x10 * p) + r + 0xe)  /* Port Status */

#define KS8995_REG_TPC0		0x60    /* TOS Priority Control 0 */
#define KS8995_REG_TPC1		0x61    /* TOS Priority Control 1 */
#define KS8995_REG_TPC2		0x62    /* TOS Priority Control 2 */
#define KS8995_REG_TPC3		0x63    /* TOS Priority Control 3 */
#define KS8995_REG_TPC4		0x64    /* TOS Priority Control 4 */
#define KS8995_REG_TPC5		0x65    /* TOS Priority Control 5 */
#define KS8995_REG_TPC6		0x66    /* TOS Priority Control 6 */
#define KS8995_REG_TPC7		0x67    /* TOS Priority Control 7 */

#define KS8995_REG_MAC0		0x68    /* MAC address 0 */
#define KS8995_REG_MAC1		0x69    /* MAC address 1 */
#define KS8995_REG_MAC2		0x6a    /* MAC address 2 */
#define KS8995_REG_MAC3		0x6b    /* MAC address 3 */
#define KS8995_REG_MAC4		0x6c    /* MAC address 4 */
#define KS8995_REG_MAC5		0x6d    /* MAC address 5 */

#define KS8995_REG_IAC0		0x6e    /* Indirect Access Control 0 */
#define KS8995_REG_IAC1		0x6f    /* Indirect Access Control 0 */
#define KS8995_REG_IAD7		0x70    /* Indirect Access Data 7 */
#define KS8995_REG_IAD6		0x71    /* Indirect Access Data 6 */
#define KS8995_REG_IAD5		0x72    /* Indirect Access Data 5 */
#define KS8995_REG_IAD4		0x73    /* Indirect Access Data 4 */
#define KS8995_REG_IAD3		0x74    /* Indirect Access Data 3 */
#define KS8995_REG_IAD2		0x75    /* Indirect Access Data 2 */
#define KS8995_REG_IAD1		0x76    /* Indirect Access Data 1 */
#define KS8995_REG_IAD0		0x77    /* Indirect Access Data 0 */

#define KSZ8864_REG_ID1		0xfe	/* Chip ID in bit 7 */

#define KS8995_REGS_SIZE	0x80
#define KSZ8864_REGS_SIZE	0x100
#define KSZ8795_REGS_SIZE	0x100

#define ID1_CHIPID_M		0xf
#define ID1_CHIPID_S		4
#define ID1_REVISION_M		0x7
#define ID1_REVISION_S		1
#define ID1_START_SW		1	/* start the switch */

#define FAMILY_KS8995		0x95
#define FAMILY_KSZ8795		0x87
#define CHIPID_M		0
#define KS8995_CHIP_ID		0x00
#define KSZ8864_CHIP_ID		0x01
#define KSZ8795_CHIP_ID		0x09

#define KS8995_CMD_WRITE	0x02U
#define KS8995_CMD_READ		0x03U

#define KS8995_RESET_DELAY	10 /* usec */

enum ks8995_chip_variant {
	ks8995,
	ksz8864,
	ksz8795,
	max_variant
};

struct ks8995_chip_params {
	char *name;
	int family_id;
	int chip_id;
	int regs_size;
	int addr_width;
	int addr_shift;
};

static const struct ks8995_chip_params ks8995_chip[] = {
	[ks8995] = {
		.name = "KS8995MA",
		.family_id = FAMILY_KS8995,
		.chip_id = KS8995_CHIP_ID,
		.regs_size = KS8995_REGS_SIZE,
		.addr_width = 8,
		.addr_shift = 0,
	},
	[ksz8864] = {
		.name = "KSZ8864RMN",
		.family_id = FAMILY_KS8995,
		.chip_id = KSZ8864_CHIP_ID,
		.regs_size = KSZ8864_REGS_SIZE,
		.addr_width = 8,
		.addr_shift = 0,
	},
	[ksz8795] = {
		.name = "KSZ8795CLX",
		.family_id = FAMILY_KSZ8795,
		.chip_id = KSZ8795_CHIP_ID,
		.regs_size = KSZ8795_REGS_SIZE,
		.addr_width = 12,
		.addr_shift = 1,
	},
};

struct ks8995_switch {
	struct spi_device	*spi;
	struct mutex		lock;
	struct gpio_desc	*reset_gpio;
	struct bin_attribute	regs_attr;
	const struct ks8995_chip_params	*chip;
	int			revision_id;
};

static const struct spi_device_id ks8995_id[] = {
	{"ks8995", ks8995},
	{"ksz8864", ksz8864},
	{"ksz8795", ksz8795},
	{ }
};
MODULE_DEVICE_TABLE(spi, ks8995_id);

static const struct of_device_id ks8895_spi_of_match[] = {
	{ .compatible = "micrel,ks8995" },
	{ .compatible = "micrel,ksz8864" },
	{ .compatible = "micrel,ksz8795" },
	{ },
};
MODULE_DEVICE_TABLE(of, ks8895_spi_of_match);

static inline u8 get_chip_id(u8 val)
{
	return (val >> ID1_CHIPID_S) & ID1_CHIPID_M;
}

static inline u8 get_chip_rev(u8 val)
{
	return (val >> ID1_REVISION_S) & ID1_REVISION_M;
}

/* create_spi_cmd - create a chip specific SPI command header
 * @ks: pointer to switch instance
 * @cmd: SPI command for switch
 * @address: register address for command
 *
 * Different chip families use different bit pattern to address the switches
 * registers:
 *
 * KS8995: 8bit command + 8bit address
 * KSZ8795: 3bit command + 12bit address + 1bit TR (?)
 */
static inline __be16 create_spi_cmd(struct ks8995_switch *ks, int cmd,
				    unsigned address)
{
	u16 result = cmd;

	/* make room for address (incl. address shift) */
	result <<= ks->chip->addr_width + ks->chip->addr_shift;
	/* add address */
	result |= address << ks->chip->addr_shift;
	/* SPI protocol needs big endian */
	return cpu_to_be16(result);
}
/* ------------------------------------------------------------------------ */
static int ks8995_read(struct ks8995_switch *ks, char *buf,
		 unsigned offset, size_t count)
{
	__be16 cmd;
	struct spi_transfer t[2];
	struct spi_message m;
	int err;

	cmd = create_spi_cmd(ks, KS8995_CMD_READ, offset);
	spi_message_init(&m);

	memset(&t, 0, sizeof(t));

	t[0].tx_buf = &cmd;
	t[0].len = sizeof(cmd);
	spi_message_add_tail(&t[0], &m);

	t[1].rx_buf = buf;
	t[1].len = count;
	spi_message_add_tail(&t[1], &m);

	mutex_lock(&ks->lock);
	err = spi_sync(ks->spi, &m);
	mutex_unlock(&ks->lock);

	return err ? err : count;
}

static int ks8995_write(struct ks8995_switch *ks, char *buf,
		 unsigned offset, size_t count)
{
	__be16 cmd;
	struct spi_transfer t[2];
	struct spi_message m;
	int err;

	cmd = create_spi_cmd(ks, KS8995_CMD_WRITE, offset);
	spi_message_init(&m);

	memset(&t, 0, sizeof(t));

	t[0].tx_buf = &cmd;
	t[0].len = sizeof(cmd);
	spi_message_add_tail(&t[0], &m);

	t[1].tx_buf = buf;
	t[1].len = count;
	spi_message_add_tail(&t[1], &m);

	mutex_lock(&ks->lock);
	err = spi_sync(ks->spi, &m);
	mutex_unlock(&ks->lock);

	return err ? err : count;
}

static inline int ks8995_read_reg(struct ks8995_switch *ks, u8 addr, u8 *buf)
{
	return ks8995_read(ks, buf, addr, 1) != 1;
}

static inline int ks8995_write_reg(struct ks8995_switch *ks, u8 addr, u8 val)
{
	char buf = val;

	return ks8995_write(ks, &buf, addr, 1) != 1;
}

/* ------------------------------------------------------------------------ */

static int ks8995_stop(struct ks8995_switch *ks)
{
	return ks8995_write_reg(ks, KS8995_REG_ID1, 0);
}

static int ks8995_start(struct ks8995_switch *ks)
{
	return ks8995_write_reg(ks, KS8995_REG_ID1, 1);
}

static int ks8995_reset(struct ks8995_switch *ks)
{
	int err;

	err = ks8995_stop(ks);
	if (err)
		return err;

	udelay(KS8995_RESET_DELAY);

	return ks8995_start(ks);
}

static ssize_t ks8995_registers_read(struct file *filp, struct kobject *kobj,
	struct bin_attribute *bin_attr, char *buf, loff_t off, size_t count)
{
	struct device *dev;
	struct ks8995_switch *ks8995;

	dev = kobj_to_dev(kobj);
	ks8995 = dev_get_drvdata(dev);

	return ks8995_read(ks8995, buf, off, count);
}

static ssize_t ks8995_registers_write(struct file *filp, struct kobject *kobj,
	struct bin_attribute *bin_attr, char *buf, loff_t off, size_t count)
{
	struct device *dev;
	struct ks8995_switch *ks8995;

	dev = kobj_to_dev(kobj);
	ks8995 = dev_get_drvdata(dev);

	return ks8995_write(ks8995, buf, off, count);
}

/* ks8995_get_revision - get chip revision
 * @ks: pointer to switch instance
 *
 * Verify chip family and id and get chip revision.
 */
static int ks8995_get_revision(struct ks8995_switch *ks)
{
	int err;
	u8 id0, id1, ksz8864_id;

	/* read family id */
	err = ks8995_read_reg(ks, KS8995_REG_ID0, &id0);
	if (err) {
		err = -EIO;
		goto err_out;
	}

	/* verify family id */
	if (id0 != ks->chip->family_id) {
		dev_err(&ks->spi->dev, "chip family id mismatch: expected 0x%02x but 0x%02x read\n",
			ks->chip->family_id, id0);
		err = -ENODEV;
		goto err_out;
	}

	switch (ks->chip->family_id) {
	case FAMILY_KS8995:
		/* try reading chip id at CHIP ID1 */
		err = ks8995_read_reg(ks, KS8995_REG_ID1, &id1);
		if (err) {
			err = -EIO;
			goto err_out;
		}

		/* verify chip id */
		if ((get_chip_id(id1) == CHIPID_M) &&
		    (get_chip_id(id1) == ks->chip->chip_id)) {
			/* KS8995MA */
			ks->revision_id = get_chip_rev(id1);
		} else if (get_chip_id(id1) != CHIPID_M) {
			/* KSZ8864RMN */
			err = ks8995_read_reg(ks, KS8995_REG_ID1, &ksz8864_id);
			if (err) {
				err = -EIO;
				goto err_out;
			}

			if ((ksz8864_id & 0x80) &&
			    (ks->chip->chip_id == KSZ8864_CHIP_ID)) {
				ks->revision_id = get_chip_rev(id1);
			}

		} else {
			dev_err(&ks->spi->dev, "unsupported chip id for KS8995 family: 0x%02x\n",
				id1);
			err = -ENODEV;
		}
		break;
	case FAMILY_KSZ8795:
		/* try reading chip id at CHIP ID1 */
		err = ks8995_read_reg(ks, KS8995_REG_ID1, &id1);
		if (err) {
			err = -EIO;
			goto err_out;
		}

		if (get_chip_id(id1) == ks->chip->chip_id) {
			ks->revision_id = get_chip_rev(id1);
		} else {
			dev_err(&ks->spi->dev, "unsupported chip id for KSZ8795 family: 0x%02x\n",
				id1);
			err = -ENODEV;
		}
		break;
	default:
		dev_err(&ks->spi->dev, "unsupported family id: 0x%02x\n", id0);
		err = -ENODEV;
		break;
	}
err_out:
	return err;
}

static const struct bin_attribute ks8995_registers_attr = {
	.attr = {
		.name   = "registers",
		.mode   = 0600,
	},
	.size   = KS8995_REGS_SIZE,
	.read   = ks8995_registers_read,
	.write  = ks8995_registers_write,
};

/* ------------------------------------------------------------------------ */
static int ks8995_probe(struct spi_device *spi)
{
	struct ks8995_switch *ks;
	int err;
	int variant = spi_get_device_id(spi)->driver_data;

	if (variant >= max_variant) {
		dev_err(&spi->dev, "bad chip variant %d\n", variant);
		return -ENODEV;
	}

	ks = devm_kzalloc(&spi->dev, sizeof(*ks), GFP_KERNEL);
	if (!ks)
		return -ENOMEM;

	mutex_init(&ks->lock);
	ks->spi = spi;
	ks->chip = &ks8995_chip[variant];

	ks->reset_gpio = devm_gpiod_get_optional(&spi->dev, "reset",
						 GPIOD_OUT_HIGH);
	err = PTR_ERR_OR_ZERO(ks->reset_gpio);
	if (err) {
		dev_err(&spi->dev,
			"failed to get reset gpio: %d\n", err);
		return err;
	}

	err = gpiod_set_consumer_name(ks->reset_gpio, "switch-reset");
	if (err)
		return err;

	/* de-assert switch reset */
	/* FIXME: this likely requires a delay */
	gpiod_set_value_cansleep(ks->reset_gpio, 0);

	spi_set_drvdata(spi, ks);

	spi->mode = SPI_MODE_0;
	spi->bits_per_word = 8;
	err = spi_setup(spi);
	if (err) {
		dev_err(&spi->dev, "spi_setup failed, err=%d\n", err);
		return err;
	}

	err = ks8995_get_revision(ks);
	if (err)
		return err;

	memcpy(&ks->regs_attr, &ks8995_registers_attr, sizeof(ks->regs_attr));
	ks->regs_attr.size = ks->chip->regs_size;

	err = ks8995_reset(ks);
	if (err)
		return err;

	sysfs_attr_init(&ks->regs_attr.attr);
	err = sysfs_create_bin_file(&spi->dev.kobj, &ks->regs_attr);
	if (err) {
		dev_err(&spi->dev, "unable to create sysfs file, err=%d\n",
				    err);
		return err;
	}

	dev_info(&spi->dev, "%s device found, Chip ID:%x, Revision:%x\n",
		 ks->chip->name, ks->chip->chip_id, ks->revision_id);

	return 0;
}

static void ks8995_remove(struct spi_device *spi)
{
	struct ks8995_switch *ks = spi_get_drvdata(spi);

	sysfs_remove_bin_file(&spi->dev.kobj, &ks->regs_attr);

	/* assert reset */
	gpiod_set_value_cansleep(ks->reset_gpio, 1);
}

/* ------------------------------------------------------------------------ */
static struct spi_driver ks8995_driver = {
	.driver = {
		.name	    = "spi-ks8995",
		.of_match_table = of_match_ptr(ks8895_spi_of_match),
	},
	.probe	  = ks8995_probe,
	.remove	  = ks8995_remove,
	.id_table = ks8995_id,
};

module_spi_driver(ks8995_driver);

MODULE_DESCRIPTION(DRV_DESC);
MODULE_VERSION(DRV_VERSION);
MODULE_AUTHOR("Gabor Juhos <juhosg at openwrt.org>");
MODULE_LICENSE("GPL v2");
