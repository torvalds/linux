/*
 * This file is part of wl1251
 *
 * Copyright (c) 1998-2007 Texas Instruments Incorporated
 * Copyright (C) 2008 Nokia Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifndef __WL1251_SPI_H__
#define __WL1251_SPI_H__

#include "cmd.h"
#include "acx.h"
#include "reg.h"

#define WSPI_CMD_READ                 0x40000000
#define WSPI_CMD_WRITE                0x00000000
#define WSPI_CMD_FIXED                0x20000000
#define WSPI_CMD_BYTE_LENGTH          0x1FFE0000
#define WSPI_CMD_BYTE_LENGTH_OFFSET   17
#define WSPI_CMD_BYTE_ADDR            0x0001FFFF

#define WSPI_INIT_CMD_CRC_LEN       5

#define WSPI_INIT_CMD_START         0x00
#define WSPI_INIT_CMD_TX            0x40
/* the extra bypass bit is sampled by the TNET as '1' */
#define WSPI_INIT_CMD_BYPASS_BIT    0x80
#define WSPI_INIT_CMD_FIXEDBUSY_LEN 0x07
#define WSPI_INIT_CMD_EN_FIXEDBUSY  0x80
#define WSPI_INIT_CMD_DIS_FIXEDBUSY 0x00
#define WSPI_INIT_CMD_IOD           0x40
#define WSPI_INIT_CMD_IP            0x20
#define WSPI_INIT_CMD_CS            0x10
#define WSPI_INIT_CMD_WS            0x08
#define WSPI_INIT_CMD_WSPI          0x01
#define WSPI_INIT_CMD_END           0x01

#define WSPI_INIT_CMD_LEN           8

#define HW_ACCESS_WSPI_FIXED_BUSY_LEN \
		((WL1251_BUSY_WORD_LEN - 4) / sizeof(u32))
#define HW_ACCESS_WSPI_INIT_CMD_MASK  0

#endif /* __WL1251_SPI_H__ */
