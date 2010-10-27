/*
 * Copyright (C) 2008 Christian Lamparter <chunkeey@web.de>
 *
 * This driver is a port from stlc45xx:
 *	Copyright (C) 2008 Nokia Corporation and/or its subsidiary(-ies).
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
 */

#ifndef P54SPI_H
#define P54SPI_H

#include <linux/mutex.h>
#include <linux/list.h>
#include <net/mac80211.h>

#include "p54.h"

/* Bit 15 is read/write bit; ON = READ, OFF = WRITE */
#define SPI_ADRS_READ_BIT_15		0x8000

#define SPI_ADRS_ARM_INTERRUPTS		0x00
#define SPI_ADRS_ARM_INT_EN		0x04

#define SPI_ADRS_HOST_INTERRUPTS	0x08
#define SPI_ADRS_HOST_INT_EN		0x0c
#define SPI_ADRS_HOST_INT_ACK		0x10

#define SPI_ADRS_GEN_PURP_1		0x14
#define SPI_ADRS_GEN_PURP_2		0x18

#define SPI_ADRS_DEV_CTRL_STAT		0x26    /* high word */

#define SPI_ADRS_DMA_DATA		0x28

#define SPI_ADRS_DMA_WRITE_CTRL		0x2c
#define SPI_ADRS_DMA_WRITE_LEN		0x2e
#define SPI_ADRS_DMA_WRITE_BASE		0x30

#define SPI_ADRS_DMA_READ_CTRL		0x34
#define SPI_ADRS_DMA_READ_LEN		0x36
#define SPI_ADRS_DMA_READ_BASE		0x38

#define SPI_CTRL_STAT_HOST_OVERRIDE	0x8000
#define SPI_CTRL_STAT_START_HALTED	0x4000
#define SPI_CTRL_STAT_RAM_BOOT		0x2000
#define SPI_CTRL_STAT_HOST_RESET	0x1000
#define SPI_CTRL_STAT_HOST_CPU_EN	0x0800

#define SPI_DMA_WRITE_CTRL_ENABLE	0x0001
#define SPI_DMA_READ_CTRL_ENABLE	0x0001
#define HOST_ALLOWED			(1 << 7)

#define SPI_TIMEOUT			100         /* msec */

#define SPI_MAX_TX_PACKETS		32

#define SPI_MAX_PACKET_SIZE		32767

#define SPI_TARGET_INT_WAKEUP		0x00000001
#define SPI_TARGET_INT_SLEEP		0x00000002
#define SPI_TARGET_INT_RDDONE		0x00000004

#define SPI_TARGET_INT_CTS		0x00004000
#define SPI_TARGET_INT_DR		0x00008000

#define SPI_HOST_INT_READY		0x00000001
#define SPI_HOST_INT_WR_READY		0x00000002
#define SPI_HOST_INT_SW_UPDATE		0x00000004
#define SPI_HOST_INT_UPDATE		0x10000000

/* clear to send */
#define SPI_HOST_INT_CR			0x00004000

/* data ready */
#define SPI_HOST_INT_DR			0x00008000

#define SPI_HOST_INTS_DEFAULT 						    \
	(SPI_HOST_INT_READY | SPI_HOST_INT_UPDATE | SPI_HOST_INT_SW_UPDATE)

#define TARGET_BOOT_SLEEP 50

struct p54s_dma_regs {
	__le16 cmd;
	__le16 len;
	__le32 addr;
} __packed;

struct p54s_tx_info {
	struct list_head tx_list;
};

struct p54s_priv {
	/* p54_common has to be the first entry */
	struct p54_common common;
	struct ieee80211_hw *hw;
	struct spi_device *spi;

	struct work_struct work;

	struct mutex mutex;
	struct completion fw_comp;

	spinlock_t tx_lock;

	/* protected by tx_lock */
	struct list_head tx_pending;

	enum fw_state fw_state;
	const struct firmware *firmware;
};

#endif /* P54SPI_H */
