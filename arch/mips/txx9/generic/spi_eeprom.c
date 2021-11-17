/*
 * spi_eeprom.c
 * Copyright (C) 2000-2001 Toshiba Corporation
 *
 * 2003-2005 (c) MontaVista Software, Inc. This file is licensed under the
 * terms of the GNU General Public License version 2. This program is
 * licensed "as is" without any warranty of any kind, whether express
 * or implied.
 *
 * Support for TX4938 in 2.6 - Manish Lachwani (mlachwani@mvista.com)
 */
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/device.h>
#include <linux/spi/spi.h>
#include <linux/spi/eeprom.h>
#include <asm/txx9/spi.h>

#define AT250X0_PAGE_SIZE	8

/* register board information for at25 driver */
int __init spi_eeprom_register(int busid, int chipid, int size)
{
	struct spi_board_info info = {
		.modalias = "at25",
		.max_speed_hz = 1500000,	/* 1.5Mbps */
		.bus_num = busid,
		.chip_select = chipid,
		/* Mode 0: High-Active, Sample-Then-Shift */
	};
	struct spi_eeprom *eeprom;
	eeprom = kzalloc(sizeof(*eeprom), GFP_KERNEL);
	if (!eeprom)
		return -ENOMEM;
	strcpy(eeprom->name, "at250x0");
	eeprom->byte_len = size;
	eeprom->page_size = AT250X0_PAGE_SIZE;
	eeprom->flags = EE_ADDR1;
	info.platform_data = eeprom;
	return spi_register_board_info(&info, 1);
}

/* simple temporary spi driver to provide early access to seeprom. */

static struct read_param {
	int busid;
	int chipid;
	int address;
	unsigned char *buf;
	int len;
} *read_param;

static int __init early_seeprom_probe(struct spi_device *spi)
{
	int stat = 0;
	u8 cmd[2];
	int len = read_param->len;
	char *buf = read_param->buf;
	int address = read_param->address;

	dev_info(&spi->dev, "spiclk %u KHz.\n",
		 (spi->max_speed_hz + 500) / 1000);
	if (read_param->busid != spi->master->bus_num ||
	    read_param->chipid != spi->chip_select)
		return -ENODEV;
	while (len > 0) {
		/* spi_write_then_read can only work with small chunk */
		int c = len < AT250X0_PAGE_SIZE ? len : AT250X0_PAGE_SIZE;
		cmd[0] = 0x03;	/* AT25_READ */
		cmd[1] = address;
		stat = spi_write_then_read(spi, cmd, sizeof(cmd), buf, c);
		buf += c;
		len -= c;
		address += c;
	}
	return stat;
}

static struct spi_driver early_seeprom_driver __initdata = {
	.driver = {
		.name	= "at25",
	},
	.probe	= early_seeprom_probe,
};

int __init spi_eeprom_read(int busid, int chipid, int address,
			   unsigned char *buf, int len)
{
	int ret;
	struct read_param param = {
		.busid = busid,
		.chipid = chipid,
		.address = address,
		.buf = buf,
		.len = len
	};

	read_param = &param;
	ret = spi_register_driver(&early_seeprom_driver);
	if (!ret)
		spi_unregister_driver(&early_seeprom_driver);
	return ret;
}
