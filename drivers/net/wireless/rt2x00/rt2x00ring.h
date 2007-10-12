/*
	Copyright (C) 2004 - 2007 rt2x00 SourceForge Project
	<http://rt2x00.serialmonkey.com>

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the
	Free Software Foundation, Inc.,
	59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/*
	Module: rt2x00
	Abstract: rt2x00 ring datastructures and routines
 */

#ifndef RT2X00RING_H
#define RT2X00RING_H

/*
 * data_desc
 * Each data entry also contains a descriptor which is used by the
 * device to determine what should be done with the packet and
 * what the current status is.
 * This structure is greatly simplified, but the descriptors
 * are basically a list of little endian 32 bit values.
 * Make the array by default 1 word big, this will allow us
 * to use sizeof() correctly.
 */
struct data_desc {
	__le32 word[1];
};

/*
 * rxdata_entry_desc
 * Summary of information that has been read from the
 * RX frame descriptor.
 */
struct rxdata_entry_desc {
	int signal;
	int rssi;
	int ofdm;
	int size;
	int flags;
};

/*
 * txdata_entry_desc
 * Summary of information that should be written into the
 * descriptor for sending a TX frame.
 */
struct txdata_entry_desc {
	unsigned long flags;
#define ENTRY_TXDONE		1
#define ENTRY_TXD_RTS_FRAME	2
#define ENTRY_TXD_OFDM_RATE	3
#define ENTRY_TXD_MORE_FRAG	4
#define ENTRY_TXD_REQ_TIMESTAMP	5
#define ENTRY_TXD_BURST		6

/*
 * Queue ID. ID's 0-4 are data TX rings
 */
	int queue;
#define QUEUE_MGMT		13
#define QUEUE_RX		14
#define QUEUE_OTHER		15

	/*
	 * PLCP values.
	 */
	u16 length_high;
	u16 length_low;
	u16 signal;
	u16 service;

	/*
	 * Timing information
	 */
	int aifs;
	int ifs;
	int cw_min;
	int cw_max;
};

/*
 * data_entry
 * The data ring is a list of data entries.
 * Each entry holds a reference to the descriptor
 * and the data buffer. For TX rings the reference to the
 * sk_buff of the packet being transmitted is also stored here.
 */
struct data_entry {
	/*
	 * Status flags
	 */
	unsigned long flags;
#define ENTRY_OWNER_NIC		1

	/*
	 * Ring we belong to.
	 */
	struct data_ring *ring;

	/*
	 * sk_buff for the packet which is being transmitted
	 * in this entry (Only used with TX related rings).
	 */
	struct sk_buff *skb;

	/*
	 * Store a ieee80211_tx_status structure in each
	 * ring entry, this will optimize the txdone
	 * handler.
	 */
	struct ieee80211_tx_status tx_status;

	/*
	 * private pointer specific to driver.
	 */
	void *priv;

	/*
	 * Data address for this entry.
	 */
	void *data_addr;
	dma_addr_t data_dma;
};

/*
 * data_ring
 * Data rings are used by the device to send and receive packets.
 * The data_addr is the base address of the data memory.
 * To determine at which point in the ring we are,
 * have to use the rt2x00_ring_index_*() functions.
 */
struct data_ring {
	/*
	 * Pointer to main rt2x00dev structure where this
	 * ring belongs to.
	 */
	struct rt2x00_dev *rt2x00dev;

	/*
	 * Base address for the device specific data entries.
	 */
	struct data_entry *entry;

	/*
	 * TX queue statistic info.
	 */
	struct ieee80211_tx_queue_stats_data stats;

	/*
	 * TX Queue parameters.
	 */
	struct ieee80211_tx_queue_params tx_params;

	/*
	 * Base address for data ring.
	 */
	dma_addr_t data_dma;
	void *data_addr;

	/*
	 * Index variables.
	 */
	u16 index;
	u16 index_done;

	/*
	 * Size of packet and descriptor in bytes.
	 */
	u16 data_size;
	u16 desc_size;
};

/*
 * Handlers to determine the address of the current device specific
 * data entry, where either index or index_done points to.
 */
static inline struct data_entry *rt2x00_get_data_entry(struct data_ring *ring)
{
	return &ring->entry[ring->index];
}

static inline struct data_entry *rt2x00_get_data_entry_done(struct data_ring
							    *ring)
{
	return &ring->entry[ring->index_done];
}

/*
 * Total ring memory
 */
static inline int rt2x00_get_ring_size(struct data_ring *ring)
{
	return ring->stats.limit * (ring->desc_size + ring->data_size);
}

/*
 * Ring index manipulation functions.
 */
static inline void rt2x00_ring_index_inc(struct data_ring *ring)
{
	ring->index++;
	if (ring->index >= ring->stats.limit)
		ring->index = 0;
	ring->stats.len++;
}

static inline void rt2x00_ring_index_done_inc(struct data_ring *ring)
{
	ring->index_done++;
	if (ring->index_done >= ring->stats.limit)
		ring->index_done = 0;
	ring->stats.len--;
	ring->stats.count++;
}

static inline void rt2x00_ring_index_clear(struct data_ring *ring)
{
	ring->index = 0;
	ring->index_done = 0;
	ring->stats.len = 0;
	ring->stats.count = 0;
}

static inline int rt2x00_ring_empty(struct data_ring *ring)
{
	return ring->stats.len == 0;
}

static inline int rt2x00_ring_full(struct data_ring *ring)
{
	return ring->stats.len == ring->stats.limit;
}

static inline int rt2x00_ring_free(struct data_ring *ring)
{
	return ring->stats.limit - ring->stats.len;
}

/*
 * TX/RX Descriptor access functions.
 */
static inline void rt2x00_desc_read(struct data_desc *desc,
				    const u8 word, u32 *value)
{
	*value = le32_to_cpu(desc->word[word]);
}

static inline void rt2x00_desc_write(struct data_desc *desc,
				     const u8 word, const u32 value)
{
	desc->word[word] = cpu_to_le32(value);
}

#endif /* RT2X00RING_H */
