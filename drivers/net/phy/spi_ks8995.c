/*
 * SPI driver for Micrel/Kendin KS8995M and KSZ8864RMN ethernet switches
 *
 * Copyright (C) 2008 Gabor Juhos <juhosg at openwrt.org>
 *
 * This file was based on: drivers/spi/at25.c
 *     Copyright (C) 2006 David Brownell
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/device.h>

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

#define ID1_CHIPID_M		0xf
#define ID1_CHIPID_S		4
#define ID1_REVISION_M		0x7
#define ID1_REVISION_S		1
#define ID1_START_SW		1	/* start the switch */

#define FAMILY_KS8995		0x95
#define CHIPID_M		0

#define KS8995_CMD_WRITE	0x02U
#define KS8995_CMD_READ		0x03U

#define KS8995_RESET_DELAY	10 /* usec */

struct ks8995_pdata {
	/* not yet implemented */
};

struct ks8995_switch {
	struct spi_device	*spi;
	struct mutex		lock;
	struct ks8995_pdata	*pdata;
	struct bin_attribute	regs_attr;
};

static inline u8 get_chip_id(u8 val)
{
	return (val >> ID1_CHIPID_S) & ID1_CHIPID_M;
}

static inline u8 get_chip_rev(u8 val)
{
	return (val >> ID1_REVISION_S) & ID1_REVISION_M;
}

/* ------------------------------------------------------------------------ */
static int ks8995_read(struct ks8995_switch *ks, char *buf,
		 unsigned offset, size_t count)
{
	u8 cmd[2];
	struct spi_transfer t[2];
	struct spi_message m;
	int err;

	spi_message_init(&m);

	memset(&t, 0, sizeof(t));

	t[0].tx_buf = cmd;
	t[0].len = sizeof(cmd);
	spi_message_add_tail(&t[0], &m);

	t[1].rx_buf = buf;
	t[1].len = count;
	spi_message_add_tail(&t[1], &m);

	cmd[0] = KS8995_CMD_READ;
	cmd[1] = offset;

	mutex_lock(&ks->lock);
	err = spi_sync(ks->spi, &m);
	mutex_unlock(&ks->lock);

	return err ? err : count;
}


static int ks8995_write(struct ks8995_switch *ks, char *buf,
		 unsigned offset, size_t count)
{
	u8 cmd[2];
	struct spi_transfer t[2];
	struct spi_message m;
	int err;

	spi_message_init(&m);

	memset(&t, 0, sizeof(t));

	t[0].tx_buf = cmd;
	t[0].len = sizeof(cmd);
	spi_message_add_tail(&t[0], &m);

	t[1].tx_buf = buf;
	t[1].len = count;
	spi_message_add_tail(&t[1], &m);

	cmd[0] = KS8995_CMD_WRITE;
	cmd[1] = offset;

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

/* ------------------------------------------------------------------------ */

static ssize_t ks8995_registers_read(struct file *filp, struct kobject *kobj,
	struct bin_attribute *bin_attr, char *buf, loff_t off, size_t count)
{
	struct device *dev;
	struct ks8995_switch *ks8995;

	dev = container_of(kobj, struct device, kobj);
	ks8995 = dev_get_drvdata(dev);

	if (unlikely(off > ks8995->regs_attr.size))
		return 0;

	if ((off + count) > ks8995->regs_attr.size)
		count = ks8995->regs_attr.size - off;

	if (unlikely(!count))
		return count;

	return ks8995_read(ks8995, buf, off, count);
}


static ssize_t ks8995_registers_write(struct file *filp, struct kobject *kobj,
	struct bin_attribute *bin_attr, char *buf, loff_t off, size_t count)
{
	struct device *dev;
	struct ks8995_switch *ks8995;

	dev = container_of(kobj, struct device, kobj);
	ks8995 = dev_get_drvdata(dev);

	if (unlikely(off >= ks8995->regs_attr.size))
		return -EFBIG;

	if ((off + count) > ks8995->regs_attr.size)
		count = ks8995->regs_attr.size - off;

	if (unlikely(!count))
		return count;

	return ks8995_write(ks8995, buf, off, count);
}


static const struct bin_attribute ks8995_registers_attr = {
	.attr = {
		.name   = "registers",
		.mode   = S_IRUSR | S_IWUSR,
	},
	.size   = KS8995_REGS_SIZE,
	.read   = ks8995_registers_read,
	.write  = ks8995_registers_write,
};

/* ------------------------------------------------------------------------ */

static int ks8995_probe(struct spi_device *spi)
{
	struct ks8995_switch    *ks;
	struct ks8995_pdata     *pdata;
	u8      ids[2];
	int     err;

	/* Chip description */
	pdata = spi->dev.platform_data;

	ks = devm_kzalloc(&spi->dev, sizeof(*ks), GFP_KERNEL);
	if (!ks)
		return -ENOMEM;

	mutex_init(&ks->lock);
	ks->pdata = pdata;
	ks->spi = spi_dev_get(spi);
	spi_set_drvdata(spi, ks);

	spi->mode = SPI_MODE_0;
	spi->bits_per_word = 8;
	err = spi_setup(spi);
	if (err) {
		dev_err(&spi->dev, "spi_setup failed, err=%d\n", err);
		return err;
	}

	err = ks8995_read(ks, ids, KS8995_REG_ID0, sizeof(ids));
	if (err < 0) {
		dev_err(&spi->dev, "unable to read id registers, err=%d\n",
				err);
		return err;
	}

	switch (ids[0]) {
	case FAMILY_KS8995:
		break;
	default:
		dev_err(&spi->dev, "unknown family id:%02x\n", ids[0]);
		return -ENODEV;
	}

	memcpy(&ks->regs_attr, &ks8995_registers_attr, sizeof(ks->regs_attr));
	if (get_chip_id(ids[1]) != CHIPID_M) {
		u8 val;

		/* Check if this is a KSZ8864RMN */
		err = ks8995_read(ks, &val, KSZ8864_REG_ID1, sizeof(val));
		if (err < 0) {
			dev_err(&spi->dev,
				"unable to read chip id register, err=%d\n",
				err);
			return err;
		}
		if ((val & 0x80) == 0) {
			dev_err(&spi->dev, "unknown chip:%02x,0\n", ids[1]);
			return err;
		}
		ks->regs_attr.size = KSZ8864_REGS_SIZE;
	}

	err = ks8995_reset(ks);
	if (err)
		return err;

	err = sysfs_create_bin_file(&spi->dev.kobj, &ks->regs_attr);
	if (err) {
		dev_err(&spi->dev, "unable to create sysfs file, err=%d\n",
				    err);
		return err;
	}

	if (get_chip_id(ids[1]) == CHIPID_M) {
		dev_info(&spi->dev,
			 "KS8995 device found, Chip ID:%x, Revision:%x\n",
			 get_chip_id(ids[1]), get_chip_rev(ids[1]));
	} else {
		dev_info(&spi->dev, "KSZ8864 device found, Revision:%x\n",
			 get_chip_rev(ids[1]));
	}

	return 0;
}

static int ks8995_remove(struct spi_device *spi)
{
	struct ks8995_switch *ks = spi_get_drvdata(spi);

	sysfs_remove_bin_file(&spi->dev.kobj, &ks->regs_attr);

	return 0;
}

/* ------------------------------------------------------------------------ */

static struct spi_driver ks8995_driver = {
	.driver = {
		.name	    = "spi-ks8995",
		.owner	   = THIS_MODULE,
	},
	.probe	  = ks8995_probe,
	.remove	  = ks8995_remove,
};

module_spi_driver(ks8995_driver);

MODULE_DESCRIPTION(DRV_DESC);
MODULE_VERSION(DRV_VERSION);
MODULE_AUTHOR("Gabor Juhos <juhosg at openwrt.org>");
MODULE_LICENSE("GPL v2");
