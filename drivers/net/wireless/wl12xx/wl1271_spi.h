/*
 * This file is part of wl1271
 *
 * Copyright (C) 1998-2009 Texas Instruments. All rights reserved.
 * Copyright (C) 2008-2009 Nokia Corporation
 *
 * Contact: Luciano Coelho <luciano.coelho@nokia.com>
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

#ifndef __WL1271_SPI_H__
#define __WL1271_SPI_H__

#include "wl1271_reg.h"

#define HW_ACCESS_MEMORY_MAX_RANGE		0x1FFC0

#define HW_PARTITION_REGISTERS_ADDR         0x1ffc0
#define HW_PART0_SIZE_ADDR                  (HW_PARTITION_REGISTERS_ADDR)
#define HW_PART0_START_ADDR                 (HW_PARTITION_REGISTERS_ADDR + 4)
#define HW_PART1_SIZE_ADDR                  (HW_PARTITION_REGISTERS_ADDR + 8)
#define HW_PART1_START_ADDR                 (HW_PARTITION_REGISTERS_ADDR + 12)
#define HW_PART2_SIZE_ADDR                  (HW_PARTITION_REGISTERS_ADDR + 16)
#define HW_PART2_START_ADDR                 (HW_PARTITION_REGISTERS_ADDR + 20)
#define HW_PART3_START_ADDR                 (HW_PARTITION_REGISTERS_ADDR + 24)

#define HW_ACCESS_REGISTER_SIZE             4

#define HW_ACCESS_PRAM_MAX_RANGE		0x3c000

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
		((WL1271_BUSY_WORD_LEN - 4) / sizeof(u32))
#define HW_ACCESS_WSPI_INIT_CMD_MASK  0

#define OCP_CMD_LOOP  32

#define OCP_CMD_WRITE 0x1
#define OCP_CMD_READ  0x2

#define OCP_READY_MASK  BIT(18)
#define OCP_STATUS_MASK (BIT(16) | BIT(17))

#define OCP_STATUS_NO_RESP    0x00000
#define OCP_STATUS_OK         0x10000
#define OCP_STATUS_REQ_FAILED 0x20000
#define OCP_STATUS_RESP_ERROR 0x30000

/* Raw target IO, address is not translated */
void wl1271_spi_raw_write(struct wl1271 *wl, int addr, void *buf,
		      size_t len, bool fixed);
void wl1271_spi_raw_read(struct wl1271 *wl, int addr, void *buf,
		     size_t len, bool fixed);

/* INIT and RESET words */
void wl1271_spi_reset(struct wl1271 *wl);
void wl1271_spi_init(struct wl1271 *wl);
#endif /* __WL1271_SPI_H__ */
