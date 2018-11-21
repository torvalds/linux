/* SPDX-License-Identifier: GPL-2.0 */
/*
 * cxd2880_devio_spi.h
 * Sony CXD2880 DVB-T2/T tuner + demodulator driver
 * I/O interface via SPI
 *
 * Copyright (C) 2016, 2017, 2018 Sony Semiconductor Solutions Corporation
 */

#ifndef CXD2880_DEVIO_SPI_H
#define CXD2880_DEVIO_SPI_H

#include "cxd2880_common.h"
#include "cxd2880_io.h"
#include "cxd2880_spi.h"

#include "cxd2880_tnrdmd.h"

int cxd2880_io_spi_create(struct cxd2880_io *io,
			  struct cxd2880_spi *spi,
			  u8 slave_select);

#endif
