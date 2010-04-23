/*
 * This file is part of wl1271
 *
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

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/crc7.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>

#include "wl1271.h"
#include "wl12xx_80211.h"
#include "wl1271_spi.h"


void wl1271_spi_reset(struct wl1271 *wl)
{
	u8 *cmd;
	struct spi_transfer t;
	struct spi_message m;

	cmd = kzalloc(WSPI_INIT_CMD_LEN, GFP_KERNEL);
	if (!cmd) {
		wl1271_error("could not allocate cmd for spi reset");
		return;
	}

	memset(&t, 0, sizeof(t));
	spi_message_init(&m);

	memset(cmd, 0xff, WSPI_INIT_CMD_LEN);

	t.tx_buf = cmd;
	t.len = WSPI_INIT_CMD_LEN;
	spi_message_add_tail(&t, &m);

	spi_sync(wl->spi, &m);

	wl1271_dump(DEBUG_SPI, "spi reset -> ", cmd, WSPI_INIT_CMD_LEN);
}

void wl1271_spi_init(struct wl1271 *wl)
{
	u8 crc[WSPI_INIT_CMD_CRC_LEN], *cmd;
	struct spi_transfer t;
	struct spi_message m;

	cmd = kzalloc(WSPI_INIT_CMD_LEN, GFP_KERNEL);
	if (!cmd) {
		wl1271_error("could not allocate cmd for spi init");
		return;
	}

	memset(crc, 0, sizeof(crc));
	memset(&t, 0, sizeof(t));
	spi_message_init(&m);

	/*
	 * Set WSPI_INIT_COMMAND
	 * the data is being send from the MSB to LSB
	 */
	cmd[2] = 0xff;
	cmd[3] = 0xff;
	cmd[1] = WSPI_INIT_CMD_START | WSPI_INIT_CMD_TX;
	cmd[0] = 0;
	cmd[7] = 0;
	cmd[6] |= HW_ACCESS_WSPI_INIT_CMD_MASK << 3;
	cmd[6] |= HW_ACCESS_WSPI_FIXED_BUSY_LEN & WSPI_INIT_CMD_FIXEDBUSY_LEN;

	if (HW_ACCESS_WSPI_FIXED_BUSY_LEN == 0)
		cmd[5] |=  WSPI_INIT_CMD_DIS_FIXEDBUSY;
	else
		cmd[5] |= WSPI_INIT_CMD_EN_FIXEDBUSY;

	cmd[5] |= WSPI_INIT_CMD_IOD | WSPI_INIT_CMD_IP | WSPI_INIT_CMD_CS
		| WSPI_INIT_CMD_WSPI | WSPI_INIT_CMD_WS;

	crc[0] = cmd[1];
	crc[1] = cmd[0];
	crc[2] = cmd[7];
	crc[3] = cmd[6];
	crc[4] = cmd[5];

	cmd[4] |= crc7(0, crc, WSPI_INIT_CMD_CRC_LEN) << 1;
	cmd[4] |= WSPI_INIT_CMD_END;

	t.tx_buf = cmd;
	t.len = WSPI_INIT_CMD_LEN;
	spi_message_add_tail(&t, &m);

	spi_sync(wl->spi, &m);

	wl1271_dump(DEBUG_SPI, "spi init -> ", cmd, WSPI_INIT_CMD_LEN);
}

#define WL1271_BUSY_WORD_TIMEOUT 1000

/* FIXME: Check busy words, removed due to SPI bug */
#if 0
static void wl1271_spi_read_busy(struct wl1271 *wl, void *buf, size_t len)
{
	struct spi_transfer t[1];
	struct spi_message m;
	u32 *busy_buf;
	int num_busy_bytes = 0;

	wl1271_info("spi read BUSY!");

	/*
	 * Look for the non-busy word in the read buffer, and if found,
	 * read in the remaining data into the buffer.
	 */
	busy_buf = (u32 *)buf;
	for (; (u32)busy_buf < (u32)buf + len; busy_buf++) {
		num_busy_bytes += sizeof(u32);
		if (*busy_buf & 0x1) {
			spi_message_init(&m);
			memset(t, 0, sizeof(t));
			memmove(buf, busy_buf, len - num_busy_bytes);
			t[0].rx_buf = buf + (len - num_busy_bytes);
			t[0].len = num_busy_bytes;
			spi_message_add_tail(&t[0], &m);
			spi_sync(wl->spi, &m);
			return;
		}
	}

	/*
	 * Read further busy words from SPI until a non-busy word is
	 * encountered, then read the data itself into the buffer.
	 */
	wl1271_info("spi read BUSY-polling needed!");

	num_busy_bytes = WL1271_BUSY_WORD_TIMEOUT;
	busy_buf = wl->buffer_busyword;
	while (num_busy_bytes) {
		num_busy_bytes--;
		spi_message_init(&m);
		memset(t, 0, sizeof(t));
		t[0].rx_buf = busy_buf;
		t[0].len = sizeof(u32);
		spi_message_add_tail(&t[0], &m);
		spi_sync(wl->spi, &m);

		if (*busy_buf & 0x1) {
			spi_message_init(&m);
			memset(t, 0, sizeof(t));
			t[0].rx_buf = buf;
			t[0].len = len;
			spi_message_add_tail(&t[0], &m);
			spi_sync(wl->spi, &m);
			return;
		}
	}

	/* The SPI bus is unresponsive, the read failed. */
	memset(buf, 0, len);
	wl1271_error("SPI read busy-word timeout!\n");
}
#endif

void wl1271_spi_raw_read(struct wl1271 *wl, int addr, void *buf,
			 size_t len, bool fixed)
{
	struct spi_transfer t[3];
	struct spi_message m;
	u32 *busy_buf;
	u32 *cmd;

	cmd = &wl->buffer_cmd;
	busy_buf = wl->buffer_busyword;

	*cmd = 0;
	*cmd |= WSPI_CMD_READ;
	*cmd |= (len << WSPI_CMD_BYTE_LENGTH_OFFSET) & WSPI_CMD_BYTE_LENGTH;
	*cmd |= addr & WSPI_CMD_BYTE_ADDR;

	if (fixed)
		*cmd |= WSPI_CMD_FIXED;

	spi_message_init(&m);
	memset(t, 0, sizeof(t));

	t[0].tx_buf = cmd;
	t[0].len = 4;
	spi_message_add_tail(&t[0], &m);

	/* Busy and non busy words read */
	t[1].rx_buf = busy_buf;
	t[1].len = WL1271_BUSY_WORD_LEN;
	spi_message_add_tail(&t[1], &m);

	t[2].rx_buf = buf;
	t[2].len = len;
	spi_message_add_tail(&t[2], &m);

	spi_sync(wl->spi, &m);

	/* FIXME: Check busy words, removed due to SPI bug */
	/* if (!(busy_buf[WL1271_BUSY_WORD_CNT - 1] & 0x1))
	   wl1271_spi_read_busy(wl, buf, len); */

	wl1271_dump(DEBUG_SPI, "spi_read cmd -> ", cmd, sizeof(*cmd));
	wl1271_dump(DEBUG_SPI, "spi_read buf <- ", buf, len);
}

void wl1271_spi_raw_write(struct wl1271 *wl, int addr, void *buf,
			  size_t len, bool fixed)
{
	struct spi_transfer t[2];
	struct spi_message m;
	u32 *cmd;

	cmd = &wl->buffer_cmd;

	*cmd = 0;
	*cmd |= WSPI_CMD_WRITE;
	*cmd |= (len << WSPI_CMD_BYTE_LENGTH_OFFSET) & WSPI_CMD_BYTE_LENGTH;
	*cmd |= addr & WSPI_CMD_BYTE_ADDR;

	if (fixed)
		*cmd |= WSPI_CMD_FIXED;

	spi_message_init(&m);
	memset(t, 0, sizeof(t));

	t[0].tx_buf = cmd;
	t[0].len = sizeof(*cmd);
	spi_message_add_tail(&t[0], &m);

	t[1].tx_buf = buf;
	t[1].len = len;
	spi_message_add_tail(&t[1], &m);

	spi_sync(wl->spi, &m);

	wl1271_dump(DEBUG_SPI, "spi_write cmd -> ", cmd, sizeof(*cmd));
	wl1271_dump(DEBUG_SPI, "spi_write buf -> ", buf, len);
}
