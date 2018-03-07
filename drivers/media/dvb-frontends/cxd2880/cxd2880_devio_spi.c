// SPDX-License-Identifier: GPL-2.0
/*
 * cxd2880_devio_spi.c
 * Sony CXD2880 DVB-T2/T tuner + demodulator driver
 * I/O interface via SPI
 *
 * Copyright (C) 2016, 2017, 2018 Sony Semiconductor Solutions Corporation
 */

#include "cxd2880_devio_spi.h"

#define BURST_WRITE_MAX 128

static int cxd2880_io_spi_read_reg(struct cxd2880_io *io,
				   enum cxd2880_io_tgt tgt,
				   u8 sub_address, u8 *data,
				   u32 size)
{
	int ret = 0;
	struct cxd2880_spi *spi = NULL;
	u8 send_data[6];
	u8 *read_data_top = data;

	if (!io || !io->if_object || !data)
		return -EINVAL;

	if (sub_address + size > 0x100)
		return -EINVAL;

	spi = io->if_object;

	if (tgt == CXD2880_IO_TGT_SYS)
		send_data[0] = 0x0b;
	else
		send_data[0] = 0x0a;

	send_data[3] = 0;
	send_data[4] = 0;
	send_data[5] = 0;

	while (size > 0) {
		send_data[1] = sub_address;
		if (size > 255)
			send_data[2] = 255;
		else
			send_data[2] = size;

		ret =
		    spi->write_read(spi, send_data, sizeof(send_data),
				    read_data_top, send_data[2]);
		if (ret)
			return ret;

		sub_address += send_data[2];
		read_data_top += send_data[2];
		size -= send_data[2];
	}

	return ret;
}

static int cxd2880_io_spi_write_reg(struct cxd2880_io *io,
				    enum cxd2880_io_tgt tgt,
				    u8 sub_address,
				    const u8 *data, u32 size)
{
	int ret = 0;
	struct cxd2880_spi *spi = NULL;
	u8 send_data[BURST_WRITE_MAX + 4];
	const u8 *write_data_top = data;

	if (!io || !io->if_object || !data)
		return -EINVAL;

	if (size > BURST_WRITE_MAX)
		return -EINVAL;

	if (sub_address + size > 0x100)
		return -EINVAL;

	spi = io->if_object;

	if (tgt == CXD2880_IO_TGT_SYS)
		send_data[0] = 0x0f;
	else
		send_data[0] = 0x0e;

	while (size > 0) {
		send_data[1] = sub_address;
		if (size > 255)
			send_data[2] = 255;
		else
			send_data[2] = size;

		memcpy(&send_data[3], write_data_top, send_data[2]);

		if (tgt == CXD2880_IO_TGT_SYS) {
			send_data[3 + send_data[2]] = 0x00;
			ret = spi->write(spi, send_data, send_data[2] + 4);
		} else {
			ret = spi->write(spi, send_data, send_data[2] + 3);
		}
		if (ret)
			return ret;

		sub_address += send_data[2];
		write_data_top += send_data[2];
		size -= send_data[2];
	}

	return ret;
}

int cxd2880_io_spi_create(struct cxd2880_io *io,
			  struct cxd2880_spi *spi, u8 slave_select)
{
	if (!io || !spi)
		return -EINVAL;

	io->read_regs = cxd2880_io_spi_read_reg;
	io->write_regs = cxd2880_io_spi_write_reg;
	io->write_reg = cxd2880_io_common_write_one_reg;
	io->if_object = spi;
	io->i2c_address_sys = 0;
	io->i2c_address_demod = 0;
	io->slave_select = slave_select;

	return 0;
}
