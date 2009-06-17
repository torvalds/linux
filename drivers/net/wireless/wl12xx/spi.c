/*
 * This file is part of wl12xx
 *
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

#include <linux/module.h>
#include <linux/crc7.h>
#include <linux/spi/spi.h>

#include "wl12xx.h"
#include "wl12xx_80211.h"
#include "reg.h"
#include "spi.h"
#include "ps.h"

static int wl12xx_translate_reg_addr(struct wl12xx *wl, int addr)
{
	/* If the address is lower than REGISTERS_BASE, it means that this is
	 * a chip-specific register address, so look it up in the registers
	 * table */
	if (addr < REGISTERS_BASE) {
		/* Make sure we don't go over the table */
		if (addr >= ACX_REG_TABLE_LEN) {
			wl12xx_error("address out of range (%d)", addr);
			return -EINVAL;
		}
		addr = wl->chip.acx_reg_table[addr];
	}

	return addr - wl->physical_reg_addr + wl->virtual_reg_addr;
}

static int wl12xx_translate_mem_addr(struct wl12xx *wl, int addr)
{
	return addr - wl->physical_mem_addr + wl->virtual_mem_addr;
}


void wl12xx_spi_reset(struct wl12xx *wl)
{
	u8 *cmd;
	struct spi_transfer t;
	struct spi_message m;

	cmd = kzalloc(WSPI_INIT_CMD_LEN, GFP_KERNEL);
	if (!cmd) {
		wl12xx_error("could not allocate cmd for spi reset");
		return;
	}

	memset(&t, 0, sizeof(t));
	spi_message_init(&m);

	memset(cmd, 0xff, WSPI_INIT_CMD_LEN);

	t.tx_buf = cmd;
	t.len = WSPI_INIT_CMD_LEN;
	spi_message_add_tail(&t, &m);

	spi_sync(wl->spi, &m);

	wl12xx_dump(DEBUG_SPI, "spi reset -> ", cmd, WSPI_INIT_CMD_LEN);
}

void wl12xx_spi_init(struct wl12xx *wl)
{
	u8 crc[WSPI_INIT_CMD_CRC_LEN], *cmd;
	struct spi_transfer t;
	struct spi_message m;

	cmd = kzalloc(WSPI_INIT_CMD_LEN, GFP_KERNEL);
	if (!cmd) {
		wl12xx_error("could not allocate cmd for spi init");
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

	wl12xx_dump(DEBUG_SPI, "spi init -> ", cmd, WSPI_INIT_CMD_LEN);
}

/* Set the SPI partitions to access the chip addresses
 *
 * There are two VIRTUAL (SPI) partitions (the memory partition and the
 * registers partition), which are mapped to two different areas of the
 * PHYSICAL (hardware) memory.  This function also makes other checks to
 * ensure that the partitions are not overlapping.  In the diagram below, the
 * memory partition comes before the register partition, but the opposite is
 * also supported.
 *
 *                               PHYSICAL address
 *                                     space
 *
 *                                    |    |
 *                                 ...+----+--> mem_start
 *          VIRTUAL address     ...   |    |
 *               space       ...      |    | [PART_0]
 *                        ...         |    |
 * 0x00000000 <--+----+...         ...+----+--> mem_start + mem_size
 *               |    |         ...   |    |
 *               |MEM |      ...      |    |
 *               |    |   ...         |    |
 *  part_size <--+----+...            |    | {unused area)
 *               |    |   ...         |    |
 *               |REG |      ...      |    |
 *  part_size    |    |         ...   |    |
 *      +     <--+----+...         ...+----+--> reg_start
 *  reg_size              ...         |    |
 *                           ...      |    | [PART_1]
 *                              ...   |    |
 *                                 ...+----+--> reg_start + reg_size
 *                                    |    |
 *
 */
void wl12xx_set_partition(struct wl12xx *wl,
			  u32 mem_start, u32 mem_size,
			  u32 reg_start, u32 reg_size)
{
	u8 tx_buf[sizeof(u32) + 2 * sizeof(struct wl12xx_partition)];
	struct wl12xx_partition *partition;
	struct spi_transfer t;
	struct spi_message m;
	u32 *cmd;
	size_t len;
	int addr;

	spi_message_init(&m);
	memset(&t, 0, sizeof(t));
	memset(tx_buf, 0, sizeof(tx_buf));

	cmd = (u32 *) tx_buf;
	partition = (struct wl12xx_partition *) (tx_buf + sizeof(u32));
	addr = HW_ACCESS_PART0_SIZE_ADDR;
	len = 2 * sizeof(struct wl12xx_partition);

	*cmd |= WSPI_CMD_WRITE;
	*cmd |= (len << WSPI_CMD_BYTE_LENGTH_OFFSET) & WSPI_CMD_BYTE_LENGTH;
	*cmd |= addr & WSPI_CMD_BYTE_ADDR;

	wl12xx_debug(DEBUG_SPI, "mem_start %08X mem_size %08X",
		     mem_start, mem_size);
	wl12xx_debug(DEBUG_SPI, "reg_start %08X reg_size %08X",
		     reg_start, reg_size);

	/* Make sure that the two partitions together don't exceed the
	 * address range */
	if ((mem_size + reg_size) > HW_ACCESS_MEMORY_MAX_RANGE) {
		wl12xx_debug(DEBUG_SPI, "Total size exceeds maximum virtual"
			     " address range.  Truncating partition[0].");
		mem_size = HW_ACCESS_MEMORY_MAX_RANGE - reg_size;
		wl12xx_debug(DEBUG_SPI, "mem_start %08X mem_size %08X",
			     mem_start, mem_size);
		wl12xx_debug(DEBUG_SPI, "reg_start %08X reg_size %08X",
			     reg_start, reg_size);
	}

	if ((mem_start < reg_start) &&
	    ((mem_start + mem_size) > reg_start)) {
		/* Guarantee that the memory partition doesn't overlap the
		 * registers partition */
		wl12xx_debug(DEBUG_SPI, "End of partition[0] is "
			     "overlapping partition[1].  Adjusted.");
		mem_size = reg_start - mem_start;
		wl12xx_debug(DEBUG_SPI, "mem_start %08X mem_size %08X",
			     mem_start, mem_size);
		wl12xx_debug(DEBUG_SPI, "reg_start %08X reg_size %08X",
			     reg_start, reg_size);
	} else if ((reg_start < mem_start) &&
		   ((reg_start + reg_size) > mem_start)) {
		/* Guarantee that the register partition doesn't overlap the
		 * memory partition */
		wl12xx_debug(DEBUG_SPI, "End of partition[1] is"
			     " overlapping partition[0].  Adjusted.");
		reg_size = mem_start - reg_start;
		wl12xx_debug(DEBUG_SPI, "mem_start %08X mem_size %08X",
			     mem_start, mem_size);
		wl12xx_debug(DEBUG_SPI, "reg_start %08X reg_size %08X",
			     reg_start, reg_size);
	}

	partition[0].start = mem_start;
	partition[0].size  = mem_size;
	partition[1].start = reg_start;
	partition[1].size  = reg_size;

	wl->physical_mem_addr = mem_start;
	wl->physical_reg_addr = reg_start;

	wl->virtual_mem_addr = 0;
	wl->virtual_reg_addr = mem_size;

	t.tx_buf = tx_buf;
	t.len = sizeof(tx_buf);
	spi_message_add_tail(&t, &m);

	spi_sync(wl->spi, &m);
}

void wl12xx_spi_read(struct wl12xx *wl, int addr, void *buf,
		     size_t len)
{
	struct spi_transfer t[3];
	struct spi_message m;
	char busy_buf[TNETWIF_READ_OFFSET_BYTES];
	u32 cmd;

	cmd = 0;
	cmd |= WSPI_CMD_READ;
	cmd |= (len << WSPI_CMD_BYTE_LENGTH_OFFSET) & WSPI_CMD_BYTE_LENGTH;
	cmd |= addr & WSPI_CMD_BYTE_ADDR;

	spi_message_init(&m);
	memset(t, 0, sizeof(t));

	t[0].tx_buf = &cmd;
	t[0].len = 4;
	spi_message_add_tail(&t[0], &m);

	/* Busy and non busy words read */
	t[1].rx_buf = busy_buf;
	t[1].len = TNETWIF_READ_OFFSET_BYTES;
	spi_message_add_tail(&t[1], &m);

	t[2].rx_buf = buf;
	t[2].len = len;
	spi_message_add_tail(&t[2], &m);

	spi_sync(wl->spi, &m);

	/* FIXME: check busy words */

	wl12xx_dump(DEBUG_SPI, "spi_read cmd -> ", &cmd, sizeof(cmd));
	wl12xx_dump(DEBUG_SPI, "spi_read buf <- ", buf, len);
}

void wl12xx_spi_write(struct wl12xx *wl, int addr, void *buf,
		      size_t len)
{
	struct spi_transfer t[2];
	struct spi_message m;
	u32 cmd;

	cmd = 0;
	cmd |= WSPI_CMD_WRITE;
	cmd |= (len << WSPI_CMD_BYTE_LENGTH_OFFSET) & WSPI_CMD_BYTE_LENGTH;
	cmd |= addr & WSPI_CMD_BYTE_ADDR;

	spi_message_init(&m);
	memset(t, 0, sizeof(t));

	t[0].tx_buf = &cmd;
	t[0].len = sizeof(cmd);
	spi_message_add_tail(&t[0], &m);

	t[1].tx_buf = buf;
	t[1].len = len;
	spi_message_add_tail(&t[1], &m);

	spi_sync(wl->spi, &m);

	wl12xx_dump(DEBUG_SPI, "spi_write cmd -> ", &cmd, sizeof(cmd));
	wl12xx_dump(DEBUG_SPI, "spi_write buf -> ", buf, len);
}

void wl12xx_spi_mem_read(struct wl12xx *wl, int addr, void *buf,
			 size_t len)
{
	int physical;

	physical = wl12xx_translate_mem_addr(wl, addr);

	wl12xx_spi_read(wl, physical, buf, len);
}

void wl12xx_spi_mem_write(struct wl12xx *wl, int addr, void *buf,
			  size_t len)
{
	int physical;

	physical = wl12xx_translate_mem_addr(wl, addr);

	wl12xx_spi_write(wl, physical, buf, len);
}

u32 wl12xx_mem_read32(struct wl12xx *wl, int addr)
{
	return wl12xx_read32(wl, wl12xx_translate_mem_addr(wl, addr));
}

void wl12xx_mem_write32(struct wl12xx *wl, int addr, u32 val)
{
	wl12xx_write32(wl, wl12xx_translate_mem_addr(wl, addr), val);
}

u32 wl12xx_reg_read32(struct wl12xx *wl, int addr)
{
	return wl12xx_read32(wl, wl12xx_translate_reg_addr(wl, addr));
}

void wl12xx_reg_write32(struct wl12xx *wl, int addr, u32 val)
{
	wl12xx_write32(wl, wl12xx_translate_reg_addr(wl, addr), val);
}
