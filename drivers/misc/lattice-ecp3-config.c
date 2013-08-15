/*
 * Copyright (C) 2012 Stefan Roese <sr@denx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/spi/spi.h>
#include <linux/platform_device.h>
#include <linux/delay.h>

#define FIRMWARE_NAME	"lattice-ecp3.bit"

/*
 * The JTAG ID's of the supported FPGA's. The ID is 32bit wide
 * reversed as noted in the manual.
 */
#define ID_ECP3_17	0xc2088080
#define ID_ECP3_35	0xc2048080

/* FPGA commands */
#define FPGA_CMD_READ_ID	0x07	/* plus 24 bits */
#define FPGA_CMD_READ_STATUS	0x09	/* plus 24 bits */
#define FPGA_CMD_CLEAR		0x70
#define FPGA_CMD_REFRESH	0x71
#define FPGA_CMD_WRITE_EN	0x4a	/* plus 2 bits */
#define FPGA_CMD_WRITE_DIS	0x4f	/* plus 8 bits */
#define FPGA_CMD_WRITE_INC	0x41	/* plus 0 bits */

/*
 * The status register is 32bit revered, DONE is bit 17 from the TN1222.pdf
 * (LatticeECP3 Slave SPI Port User's Guide)
 */
#define FPGA_STATUS_DONE	0x00004000
#define FPGA_STATUS_CLEARED	0x00010000

#define FPGA_CLEAR_TIMEOUT	5000	/* max. 5000ms for FPGA clear */
#define FPGA_CLEAR_MSLEEP	10
#define FPGA_CLEAR_LOOP_COUNT	(FPGA_CLEAR_TIMEOUT / FPGA_CLEAR_MSLEEP)

struct fpga_data {
	struct completion fw_loaded;
};

struct ecp3_dev {
	u32 jedec_id;
	char *name;
};

static const struct ecp3_dev ecp3_dev[] = {
	{
		.jedec_id = ID_ECP3_17,
		.name = "Lattice ECP3-17",
	},
	{
		.jedec_id = ID_ECP3_35,
		.name = "Lattice ECP3-35",
	},
};

static void firmware_load(const struct firmware *fw, void *context)
{
	struct spi_device *spi = (struct spi_device *)context;
	struct fpga_data *data = spi_get_drvdata(spi);
	u8 *buffer;
	int ret;
	u8 txbuf[8];
	u8 rxbuf[8];
	int rx_len = 8;
	int i;
	u32 jedec_id;
	u32 status;

	if (fw->size == 0) {
		dev_err(&spi->dev, "Error: Firmware size is 0!\n");
		return;
	}

	/* Fill dummy data (24 stuffing bits for commands) */
	txbuf[1] = 0x00;
	txbuf[2] = 0x00;
	txbuf[3] = 0x00;

	/* Trying to speak with the FPGA via SPI... */
	txbuf[0] = FPGA_CMD_READ_ID;
	ret = spi_write_then_read(spi, txbuf, 8, rxbuf, rx_len);
	dev_dbg(&spi->dev, "FPGA JTAG ID=%08x\n", *(u32 *)&rxbuf[4]);
	jedec_id = *(u32 *)&rxbuf[4];

	for (i = 0; i < ARRAY_SIZE(ecp3_dev); i++) {
		if (jedec_id == ecp3_dev[i].jedec_id)
			break;
	}
	if (i == ARRAY_SIZE(ecp3_dev)) {
		dev_err(&spi->dev,
			"Error: No supported FPGA detected (JEDEC_ID=%08x)!\n",
			jedec_id);
		return;
	}

	dev_info(&spi->dev, "FPGA %s detected\n", ecp3_dev[i].name);

	txbuf[0] = FPGA_CMD_READ_STATUS;
	ret = spi_write_then_read(spi, txbuf, 8, rxbuf, rx_len);
	dev_dbg(&spi->dev, "FPGA Status=%08x\n", *(u32 *)&rxbuf[4]);

	buffer = kzalloc(fw->size + 8, GFP_KERNEL);
	if (!buffer) {
		dev_err(&spi->dev, "Error: Can't allocate memory!\n");
		return;
	}

	/*
	 * Insert WRITE_INC command into stream (one SPI frame)
	 */
	buffer[0] = FPGA_CMD_WRITE_INC;
	buffer[1] = 0xff;
	buffer[2] = 0xff;
	buffer[3] = 0xff;
	memcpy(buffer + 4, fw->data, fw->size);

	txbuf[0] = FPGA_CMD_REFRESH;
	ret = spi_write(spi, txbuf, 4);

	txbuf[0] = FPGA_CMD_WRITE_EN;
	ret = spi_write(spi, txbuf, 4);

	txbuf[0] = FPGA_CMD_CLEAR;
	ret = spi_write(spi, txbuf, 4);

	/*
	 * Wait for FPGA memory to become cleared
	 */
	for (i = 0; i < FPGA_CLEAR_LOOP_COUNT; i++) {
		txbuf[0] = FPGA_CMD_READ_STATUS;
		ret = spi_write_then_read(spi, txbuf, 8, rxbuf, rx_len);
		status = *(u32 *)&rxbuf[4];
		if (status == FPGA_STATUS_CLEARED)
			break;

		msleep(FPGA_CLEAR_MSLEEP);
	}

	if (i == FPGA_CLEAR_LOOP_COUNT) {
		dev_err(&spi->dev,
			"Error: Timeout waiting for FPGA to clear (status=%08x)!\n",
			status);
		kfree(buffer);
		return;
	}

	dev_info(&spi->dev, "Configuring the FPGA...\n");
	ret = spi_write(spi, buffer, fw->size + 8);

	txbuf[0] = FPGA_CMD_WRITE_DIS;
	ret = spi_write(spi, txbuf, 4);

	txbuf[0] = FPGA_CMD_READ_STATUS;
	ret = spi_write_then_read(spi, txbuf, 8, rxbuf, rx_len);
	dev_dbg(&spi->dev, "FPGA Status=%08x\n", *(u32 *)&rxbuf[4]);
	status = *(u32 *)&rxbuf[4];

	/* Check result */
	if (status & FPGA_STATUS_DONE)
		dev_info(&spi->dev, "FPGA successfully configured!\n");
	else
		dev_info(&spi->dev, "FPGA not configured (DONE not set)\n");

	/*
	 * Don't forget to release the firmware again
	 */
	release_firmware(fw);

	kfree(buffer);

	complete(&data->fw_loaded);
}

static int lattice_ecp3_probe(struct spi_device *spi)
{
	struct fpga_data *data;
	int err;

	data = devm_kzalloc(&spi->dev, sizeof(*data), GFP_KERNEL);
	if (!data) {
		dev_err(&spi->dev, "Memory allocation for fpga_data failed\n");
		return -ENOMEM;
	}
	spi_set_drvdata(spi, data);

	init_completion(&data->fw_loaded);
	err = request_firmware_nowait(THIS_MODULE, FW_ACTION_NOHOTPLUG,
				      FIRMWARE_NAME, &spi->dev,
				      GFP_KERNEL, spi, firmware_load);
	if (err) {
		dev_err(&spi->dev, "Firmware loading failed with %d!\n", err);
		return err;
	}

	dev_info(&spi->dev, "FPGA bitstream configuration driver registered\n");

	return 0;
}

static int lattice_ecp3_remove(struct spi_device *spi)
{
	struct fpga_data *data = spi_get_drvdata(spi);

	wait_for_completion(&data->fw_loaded);

	return 0;
}

static const struct spi_device_id lattice_ecp3_id[] = {
	{ "ecp3-17", 0 },
	{ "ecp3-35", 0 },
	{ }
};
MODULE_DEVICE_TABLE(spi, lattice_ecp3_id);

static struct spi_driver lattice_ecp3_driver = {
	.driver = {
		.name = "lattice-ecp3",
		.owner = THIS_MODULE,
	},
	.probe = lattice_ecp3_probe,
	.remove = lattice_ecp3_remove,
	.id_table = lattice_ecp3_id,
};

module_spi_driver(lattice_ecp3_driver);

MODULE_AUTHOR("Stefan Roese <sr@denx.de>");
MODULE_DESCRIPTION("Lattice ECP3 FPGA configuration via SPI");
MODULE_LICENSE("GPL");
