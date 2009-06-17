/*
 * This file is part of wl12xx
 *
 * Copyright (c) 1998-2007 Texas Instruments Incorporated
 * Copyright (C) 2008 Nokia Corporation
 *
 * Contact: Kalle Valo <kalle.valo@nokia.com>
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

#ifndef __WL12XX_SPI_H__
#define __WL12XX_SPI_H__

#include "cmd.h"
#include "acx.h"
#include "reg.h"

#define HW_ACCESS_MEMORY_MAX_RANGE		0x1FFC0

#define HW_ACCESS_PART0_SIZE_ADDR           0x1FFC0
#define HW_ACCESS_PART0_START_ADDR          0x1FFC4
#define HW_ACCESS_PART1_SIZE_ADDR           0x1FFC8
#define HW_ACCESS_PART1_START_ADDR          0x1FFCC

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

#define TNETWIF_READ_OFFSET_BYTES  8
#define HW_ACCESS_WSPI_FIXED_BUSY_LEN \
		((TNETWIF_READ_OFFSET_BYTES - 4) / sizeof(u32))
#define HW_ACCESS_WSPI_INIT_CMD_MASK  0


/* Raw target IO, address is not translated */
void wl12xx_spi_read(struct wl12xx *wl, int addr, void *buf, size_t len);
void wl12xx_spi_write(struct wl12xx *wl, int addr, void *buf, size_t len);

/* Memory target IO, address is tranlated to partition 0 */
void wl12xx_spi_mem_read(struct wl12xx *wl, int addr, void *buf, size_t len);
void wl12xx_spi_mem_write(struct wl12xx *wl, int addr, void *buf, size_t len);
u32 wl12xx_mem_read32(struct wl12xx *wl, int addr);
void wl12xx_mem_write32(struct wl12xx *wl, int addr, u32 val);

/* Registers IO */
u32 wl12xx_reg_read32(struct wl12xx *wl, int addr);
void wl12xx_reg_write32(struct wl12xx *wl, int addr, u32 val);

/* INIT and RESET words */
void wl12xx_spi_reset(struct wl12xx *wl);
void wl12xx_spi_init(struct wl12xx *wl);
void wl12xx_set_partition(struct wl12xx *wl,
			  u32 part_start, u32 part_size,
			  u32 reg_start,  u32 reg_size);

static inline u32 wl12xx_read32(struct wl12xx *wl, int addr)
{
	u32 response;

	wl12xx_spi_read(wl, addr, &response, sizeof(u32));

	return response;
}

static inline void wl12xx_write32(struct wl12xx *wl, int addr, u32 val)
{
	wl12xx_spi_write(wl, addr, &val, sizeof(u32));
}

#endif /* __WL12XX_SPI_H__ */
