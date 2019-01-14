/*	Copyright (C) 2009 - 2010 Ivo van Doorn <IvDoorn@gmail.com>
 *	Copyright (C) 2009 Alban Browaeys <prahal@yahoo.com>
 *	Copyright (C) 2009 Felix Fietkau <nbd@openwrt.org>
 *	Copyright (C) 2009 Luis Correia <luis.f.correia@gmail.com>
 *	Copyright (C) 2009 Mattias Nissler <mattias.nissler@gmx.de>
 *	Copyright (C) 2009 Mark Asselstine <asselsm@gmail.com>
 *	Copyright (C) 2009 Xose Vazquez Perez <xose.vazquez@gmail.com>
 *	Copyright (C) 2009 Bart Zolnierkiewicz <bzolnier@gmail.com>
 *	<http://rt2x00.serialmonkey.com>
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/*	Module: rt2800mmio
 *	Abstract: forward declarations for the rt2800mmio module.
 */

#ifndef RT2800MMIO_H
#define RT2800MMIO_H

/*
 * Queue register offset macros
 */
#define TX_QUEUE_REG_OFFSET	0x10
#define TX_BASE_PTR(__x)	(TX_BASE_PTR0 + ((__x) * TX_QUEUE_REG_OFFSET))
#define TX_MAX_CNT(__x)		(TX_MAX_CNT0 + ((__x) * TX_QUEUE_REG_OFFSET))
#define TX_CTX_IDX(__x)		(TX_CTX_IDX0 + ((__x) * TX_QUEUE_REG_OFFSET))
#define TX_DTX_IDX(__x)		(TX_DTX_IDX0 + ((__x) * TX_QUEUE_REG_OFFSET))

/*
 * DMA descriptor defines.
 */
#define TXD_DESC_SIZE			(4 * sizeof(__le32))
#define RXD_DESC_SIZE			(4 * sizeof(__le32))

/*
 * TX descriptor format for TX, PRIO and Beacon Ring.
 */

/*
 * Word0
 */
#define TXD_W0_SD_PTR0			FIELD32(0xffffffff)

/*
 * Word1
 */
#define TXD_W1_SD_LEN1			FIELD32(0x00003fff)
#define TXD_W1_LAST_SEC1		FIELD32(0x00004000)
#define TXD_W1_BURST			FIELD32(0x00008000)
#define TXD_W1_SD_LEN0			FIELD32(0x3fff0000)
#define TXD_W1_LAST_SEC0		FIELD32(0x40000000)
#define TXD_W1_DMA_DONE			FIELD32(0x80000000)

/*
 * Word2
 */
#define TXD_W2_SD_PTR1			FIELD32(0xffffffff)

/*
 * Word3
 * WIV: Wireless Info Valid. 1: Driver filled WI, 0: DMA needs to copy WI
 * QSEL: Select on-chip FIFO ID for 2nd-stage output scheduler.
 *       0:MGMT, 1:HCCA 2:EDCA
 */
#define TXD_W3_WIV			FIELD32(0x01000000)
#define TXD_W3_QSEL			FIELD32(0x06000000)
#define TXD_W3_TCO			FIELD32(0x20000000)
#define TXD_W3_UCO			FIELD32(0x40000000)
#define TXD_W3_ICO			FIELD32(0x80000000)

/*
 * RX descriptor format for RX Ring.
 */

/*
 * Word0
 */
#define RXD_W0_SDP0			FIELD32(0xffffffff)

/*
 * Word1
 */
#define RXD_W1_SDL1			FIELD32(0x00003fff)
#define RXD_W1_SDL0			FIELD32(0x3fff0000)
#define RXD_W1_LS0			FIELD32(0x40000000)
#define RXD_W1_DMA_DONE			FIELD32(0x80000000)

/*
 * Word2
 */
#define RXD_W2_SDP1			FIELD32(0xffffffff)

/*
 * Word3
 * AMSDU: RX with 802.3 header, not 802.11 header.
 * DECRYPTED: This frame is being decrypted.
 */
#define RXD_W3_BA			FIELD32(0x00000001)
#define RXD_W3_DATA			FIELD32(0x00000002)
#define RXD_W3_NULLDATA			FIELD32(0x00000004)
#define RXD_W3_FRAG			FIELD32(0x00000008)
#define RXD_W3_UNICAST_TO_ME		FIELD32(0x00000010)
#define RXD_W3_MULTICAST		FIELD32(0x00000020)
#define RXD_W3_BROADCAST		FIELD32(0x00000040)
#define RXD_W3_MY_BSS			FIELD32(0x00000080)
#define RXD_W3_CRC_ERROR		FIELD32(0x00000100)
#define RXD_W3_CIPHER_ERROR		FIELD32(0x00000600)
#define RXD_W3_AMSDU			FIELD32(0x00000800)
#define RXD_W3_HTC			FIELD32(0x00001000)
#define RXD_W3_RSSI			FIELD32(0x00002000)
#define RXD_W3_L2PAD			FIELD32(0x00004000)
#define RXD_W3_AMPDU			FIELD32(0x00008000)
#define RXD_W3_DECRYPTED		FIELD32(0x00010000)
#define RXD_W3_PLCP_SIGNAL		FIELD32(0x00020000)
#define RXD_W3_PLCP_RSSI		FIELD32(0x00040000)

/* TX descriptor initialization */
__le32 *rt2800mmio_get_txwi(struct queue_entry *entry);
void rt2800mmio_write_tx_desc(struct queue_entry *entry,
			      struct txentry_desc *txdesc);

/* RX control handlers */
void rt2800mmio_fill_rxdone(struct queue_entry *entry,
			    struct rxdone_entry_desc *rxdesc);

/* Interrupt functions */
void rt2800mmio_txstatus_tasklet(unsigned long data);
void rt2800mmio_pretbtt_tasklet(unsigned long data);
void rt2800mmio_tbtt_tasklet(unsigned long data);
void rt2800mmio_rxdone_tasklet(unsigned long data);
void rt2800mmio_autowake_tasklet(unsigned long data);
irqreturn_t rt2800mmio_interrupt(int irq, void *dev_instance);
void rt2800mmio_toggle_irq(struct rt2x00_dev *rt2x00dev,
			   enum dev_state state);

/* Queue handlers */
void rt2800mmio_start_queue(struct data_queue *queue);
void rt2800mmio_kick_queue(struct data_queue *queue);
void rt2800mmio_flush_queue(struct data_queue *queue, bool drop);
void rt2800mmio_stop_queue(struct data_queue *queue);
void rt2800mmio_queue_init(struct data_queue *queue);

/* Initialization functions */
bool rt2800mmio_get_entry_state(struct queue_entry *entry);
void rt2800mmio_clear_entry(struct queue_entry *entry);
int rt2800mmio_init_queues(struct rt2x00_dev *rt2x00dev);
int rt2800mmio_init_registers(struct rt2x00_dev *rt2x00dev);

/* Device state switch handlers. */
int rt2800mmio_enable_radio(struct rt2x00_dev *rt2x00dev);

#endif /* RT2800MMIO_H */
