/*
 * This file is part of stlc45xx
 *
 * Copyright (C) 2008 Nokia Corporation and/or its subsidiary(-ies).
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

#include <linux/mutex.h>
#include <linux/list.h>
#include <net/mac80211.h>

#include "stlc45xx_lmac.h"

#define DRIVER_NAME "stlc45xx"
#define DRIVER_VERSION "0.1.3"

#define DRIVER_PREFIX DRIVER_NAME ": "

enum {
	DEBUG_NONE = 0,
	DEBUG_FUNC = 1 << 0,
	DEBUG_IRQ = 1 << 1,
	DEBUG_BH = 1 << 2,
	DEBUG_RX = 1 << 3,
	DEBUG_RX_CONTENT = 1 << 5,
	DEBUG_TX = 1 << 6,
	DEBUG_TX_CONTENT = 1 << 8,
	DEBUG_TXBUFFER = 1 << 9,
	DEBUG_QUEUE = 1 << 10,
	DEBUG_BOOT = 1 << 11,
	DEBUG_PSM = 1 << 12,
	DEBUG_ALL = ~0,
};

#define DEBUG_LEVEL DEBUG_NONE
/* #define DEBUG_LEVEL DEBUG_ALL */
/* #define DEBUG_LEVEL (DEBUG_TX | DEBUG_RX | DEBUG_IRQ) */
/* #define DEBUG_LEVEL (DEBUG_TX | DEBUG_MEMREGION | DEBUG_QUEUE) */
/* #define DEBUG_LEVEL (DEBUG_MEMREGION | DEBUG_QUEUE) */

#define stlc45xx_error(fmt, arg...) \
	printk(KERN_ERR DRIVER_PREFIX "ERROR " fmt "\n", ##arg)

#define stlc45xx_warning(fmt, arg...) \
	printk(KERN_WARNING DRIVER_PREFIX "WARNING " fmt "\n", ##arg)

#define stlc45xx_info(fmt, arg...) \
	printk(KERN_INFO DRIVER_PREFIX fmt "\n", ##arg)

#define stlc45xx_debug(level, fmt, arg...) \
	do { \
		if (level & DEBUG_LEVEL) \
			printk(KERN_DEBUG DRIVER_PREFIX fmt "\n", ##arg); \
	} while (0)

#define stlc45xx_dump(level, buf, len)		\
	do { \
		if (level & DEBUG_LEVEL) \
			print_hex_dump(KERN_DEBUG, "", DUMP_PREFIX_OFFSET, \
				       16, 1, buf, len, 1);		\
	} while (0)

#define MAC2STR(a) ((a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5])
#define MACSTR "%02x:%02x:%02x:%02x:%02x:%02x"

/* Bit 15 is read/write bit; ON = READ, OFF = WRITE */
#define ADDR_READ_BIT_15  0x8000

#define SPI_ADRS_ARM_INTERRUPTS     0x00
#define SPI_ADRS_ARM_INT_EN	    0x04

#define SPI_ADRS_HOST_INTERRUPTS    0x08
#define SPI_ADRS_HOST_INT_EN	    0x0c
#define SPI_ADRS_HOST_INT_ACK	    0x10

#define SPI_ADRS_GEN_PURP_1   	    0x14
#define SPI_ADRS_GEN_PURP_2   	    0x18

/* high word */
#define SPI_ADRS_DEV_CTRL_STAT      0x26

#define SPI_ADRS_DMA_DATA      	    0x28

#define SPI_ADRS_DMA_WRITE_CTRL     0x2c
#define SPI_ADRS_DMA_WRITE_LEN      0x2e
#define SPI_ADRS_DMA_WRITE_BASE     0x30

#define SPI_ADRS_DMA_READ_CTRL      0x34
#define SPI_ADRS_DMA_READ_LEN       0x36
#define SPI_ADRS_DMA_READ_BASE      0x38

#define SPI_CTRL_STAT_HOST_OVERRIDE 0x8000
#define SPI_CTRL_STAT_START_HALTED  0x4000
#define SPI_CTRL_STAT_RAM_BOOT      0x2000
#define SPI_CTRL_STAT_HOST_RESET    0x1000
#define SPI_CTRL_STAT_HOST_CPU_EN   0x0800

#define SPI_DMA_WRITE_CTRL_ENABLE   0x0001
#define SPI_DMA_READ_CTRL_ENABLE    0x0001
#define HOST_ALLOWED                (1 << 7)

#define FIRMWARE_ADDRESS                        0x20000

#define SPI_TIMEOUT                             100         /* msec */

#define SPI_MAX_TX_PACKETS                      32

#define SPI_MAX_PACKET_SIZE                     32767

#define SPI_TARGET_INT_WAKEUP                   0x00000001
#define SPI_TARGET_INT_SLEEP                    0x00000002
#define SPI_TARGET_INT_RDDONE                   0x00000004

#define SPI_TARGET_INT_CTS                      0x00004000
#define SPI_TARGET_INT_DR                       0x00008000

#define SPI_HOST_INT_READY                      0x00000001
#define SPI_HOST_INT_WR_READY                   0x00000002
#define SPI_HOST_INT_SW_UPDATE                  0x00000004
#define SPI_HOST_INT_UPDATE                     0x10000000

/* clear to send */
#define SPI_HOST_INT_CTS	                0x00004000

/* data ready */
#define SPI_HOST_INT_DR	                        0x00008000

#define SPI_HOST_INTS_DEFAULT \
	(SPI_HOST_INT_READY | SPI_HOST_INT_UPDATE | SPI_HOST_INT_SW_UPDATE)

#define TARGET_BOOT_SLEEP 50

/* The firmware buffer is divided into three areas:
 *
 * o config area (for control commands)
 * o tx buffer
 * o rx buffer
 */
#define FIRMWARE_BUFFER_START 0x20200
#define FIRMWARE_BUFFER_END 0x27c60
#define FIRMWARE_BUFFER_LEN (FIRMWARE_BUFFER_END - FIRMWARE_BUFFER_START)
#define FIRMWARE_MTU 3240
#define FIRMWARE_CONFIG_PAYLOAD_LEN 1024
#define FIRMWARE_CONFIG_START FIRMWARE_BUFFER_START
#define FIRMWARE_CONFIG_LEN (sizeof(struct s_lm_control) + \
			     FIRMWARE_CONFIG_PAYLOAD_LEN)
#define FIRMWARE_CONFIG_END (FIRMWARE_CONFIG_START + FIRMWARE_CONFIG_LEN - 1)
#define FIRMWARE_RXBUFFER_LEN (5 * FIRMWARE_MTU + 1024)
#define FIRMWARE_RXBUFFER_START (FIRMWARE_BUFFER_END - FIRMWARE_RXBUFFER_LEN)
#define FIRMWARE_RXBUFFER_END (FIRMWARE_RXBUFFER_START + \
			       FIRMWARE_RXBUFFER_LEN - 1)
#define FIRMWARE_TXBUFFER_START (FIRMWARE_BUFFER_START + FIRMWARE_CONFIG_LEN)
#define FIRMWARE_TXBUFFER_LEN (FIRMWARE_BUFFER_LEN - FIRMWARE_CONFIG_LEN - \
			       FIRMWARE_RXBUFFER_LEN)
#define FIRMWARE_TXBUFFER_END (FIRMWARE_TXBUFFER_START + \
			       FIRMWARE_TXBUFFER_LEN - 1)

#define FIRMWARE_TXBUFFER_HEADER 100
#define FIRMWARE_TXBUFFER_TRAILER 4

/* FIXME: come up with a proper value */
#define MAX_FRAME_LEN 2500

/* unit is ms */
#define TX_FRAME_LIFETIME 2000
#define TX_TIMEOUT 4000

#define SUPPORTED_CHANNELS 13

/* FIXME */
/* #define CHANNEL_CAL_LEN offsetof(struct s_lmo_scan, bratemask) - \ */
/* 	offsetof(struct s_lmo_scan, channel) */
#define CHANNEL_CAL_LEN 292
#define CHANNEL_CAL_ARRAY_LEN (SUPPORTED_CHANNELS * CHANNEL_CAL_LEN)
/* FIXME */
/* #define RSSI_CAL_LEN sizeof(struct s_lmo_scan) - \ */
/* 	offsetof(struct s_lmo_scan, rssical) */
#define RSSI_CAL_LEN 8
#define RSSI_CAL_ARRAY_LEN (SUPPORTED_CHANNELS * RSSI_CAL_LEN)

struct s_dma_regs {
	unsigned short cmd;
	unsigned short len;
	unsigned long addr;
};

struct stlc45xx_ie_tim {
	u8 dtim_count;
	u8 dtim_period;
	u8 bmap_control;
	u8 pvbmap[251];
};

struct txbuffer {
	/* can be removed when switched to skb queue */
	struct list_head tx_list;

	struct list_head buffer_list;

	int start;
	int frame_start;
	int end;

	struct sk_buff *skb;
	u32 handle;

	bool status_needed;

	int header_len;

	/* unit jiffies */
	unsigned long lifetime;
};

enum fw_state {
	FW_STATE_OFF,
	FW_STATE_BOOTING,
	FW_STATE_READY,
	FW_STATE_RESET,
	FW_STATE_RESETTING,
};

struct stlc45xx {
	struct ieee80211_hw *hw;
	struct spi_device *spi;
	struct work_struct work;
	struct work_struct work_reset;
	struct delayed_work work_tx_timeout;
	struct mutex mutex;
	struct completion fw_comp;


	u8 bssid[ETH_ALEN];
	u8 mac_addr[ETH_ALEN];
	int channel;

	u8 *cal_rssi;
	u8 *cal_channels;

	enum fw_state fw_state;

	spinlock_t tx_lock;

	/* protected by tx_lock */
	struct list_head txbuffer;

	/* protected by tx_lock */
	struct list_head tx_pending;

	/* protected by tx_lock */
	int tx_queue_stopped;

	/* protected by mutex */
	struct list_head tx_sent;

	int tx_frames;

	u8 *fw;
	int fw_len;

	bool psm;
	bool associated;
	int aid;
	bool pspolling;
};


