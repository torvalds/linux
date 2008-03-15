/*
	Copyright (C) 2004 - 2008 rt2x00 SourceForge Project
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
	Abstract: rt2x00 queue datastructures and routines
 */

#ifndef RT2X00QUEUE_H
#define RT2X00QUEUE_H

#include <linux/prefetch.h>

/**
 * DOC: Entrie frame size
 *
 * Ralink PCI devices demand the Frame size to be a multiple of 128 bytes,
 * for USB devices this restriction does not apply, but the value of
 * 2432 makes sense since it is big enough to contain the maximum fragment
 * size according to the ieee802.11 specs.
 */
#define DATA_FRAME_SIZE	2432
#define MGMT_FRAME_SIZE	256

/**
 * DOC: Number of entries per queue
 *
 * After research it was concluded that 12 entries in a RX and TX
 * queue would be sufficient. Although this is almost one third of
 * the amount the legacy driver allocated, the queues aren't getting
 * filled to the maximum even when working with the maximum rate.
 */
#define RX_ENTRIES	12
#define TX_ENTRIES	12
#define BEACON_ENTRIES	1
#define ATIM_ENTRIES	1

/**
 * enum data_queue_qid: Queue identification
 */
enum data_queue_qid {
	QID_AC_BE = 0,
	QID_AC_BK = 1,
	QID_AC_VI = 2,
	QID_AC_VO = 3,
	QID_HCCA = 4,
	QID_MGMT = 13,
	QID_RX = 14,
	QID_OTHER = 15,
};

/**
 * enum rt2x00_bcn_queue: Beacon queue index
 *
 * Start counting with a high offset, this because this enumeration
 * supplements &enum ieee80211_tx_queue and we should prevent value
 * conflicts.
 *
 * @RT2X00_BCN_QUEUE_BEACON: Beacon queue
 * @RT2X00_BCN_QUEUE_ATIM: Atim queue (sends frame after beacon)
 */
enum rt2x00_bcn_queue {
	RT2X00_BCN_QUEUE_BEACON = 100,
	RT2X00_BCN_QUEUE_ATIM = 101,
};

/**
 * enum skb_frame_desc_flags: Flags for &struct skb_frame_desc
 *
 * @FRAME_DESC_DRIVER_GENERATED: Frame was generated inside driver
 *	and should not be reported back to mac80211 during txdone.
 */
enum skb_frame_desc_flags {
	FRAME_DESC_DRIVER_GENERATED = 1 << 0,
};

/**
 * struct skb_frame_desc: Descriptor information for the skb buffer
 *
 * This structure is placed over the skb->cb array, this means that
 * this structure should not exceed the size of that array (48 bytes).
 *
 * @flags: Frame flags, see &enum skb_frame_desc_flags.
 * @frame_type: Frame type, see &enum rt2x00_dump_type.
 * @data: Pointer to data part of frame (Start of ieee80211 header).
 * @desc: Pointer to descriptor part of the frame.
 *	Note that this pointer could point to something outside
 *	of the scope of the skb->data pointer.
 * @data_len: Length of the frame data.
 * @desc_len: Length of the frame descriptor.

 * @entry: The entry to which this sk buffer belongs.
 */
struct skb_frame_desc {
	unsigned int flags;

	unsigned int frame_type;

	void *data;
	void *desc;

	unsigned int data_len;
	unsigned int desc_len;

	struct queue_entry *entry;
};

static inline struct skb_frame_desc* get_skb_frame_desc(struct sk_buff *skb)
{
	BUILD_BUG_ON(sizeof(struct skb_frame_desc) > sizeof(skb->cb));
	return (struct skb_frame_desc *)&skb->cb[0];
}

/**
 * enum rxdone_entry_desc_flags: Flags for &struct rxdone_entry_desc
 *
 * @RXDONE_SIGNAL_PLCP: Does the signal field contain the plcp value,
 *	or does it contain the bitrate itself.
 * @RXDONE_MY_BSS: Does this frame originate from device's BSS.
 */
enum rxdone_entry_desc_flags {
	RXDONE_SIGNAL_PLCP = 1 << 0,
	RXDONE_MY_BSS = 1 << 1,
};

/**
 * struct rxdone_entry_desc: RX Entry descriptor
 *
 * Summary of information that has been read from the RX frame descriptor.
 *
 * @signal: Signal of the received frame.
 * @rssi: RSSI of the received frame.
 * @size: Data size of the received frame.
 * @flags: MAC80211 receive flags (See &enum mac80211_rx_flags).
 * @dev_flags: Ralink receive flags (See &enum rxdone_entry_desc_flags).

 */
struct rxdone_entry_desc {
	int signal;
	int rssi;
	int size;
	int flags;
	int dev_flags;
};

/**
 * struct txdone_entry_desc: TX done entry descriptor
 *
 * Summary of information that has been read from the TX frame descriptor
 * after the device is done with transmission.
 *
 * @control: Control structure which was used to transmit the frame.
 * @status: TX status (See &enum tx_status).
 * @retry: Retry count.
 */
struct txdone_entry_desc {
	struct ieee80211_tx_control *control;
	int status;
	int retry;
};

/**
 * enum txentry_desc_flags: Status flags for TX entry descriptor
 *
 * @ENTRY_TXD_RTS_FRAME: This frame is a RTS frame.
 * @ENTRY_TXD_OFDM_RATE: This frame is send out with an OFDM rate.
 * @ENTRY_TXD_MORE_FRAG: This frame is followed by another fragment.
 * @ENTRY_TXD_REQ_TIMESTAMP: Require timestamp to be inserted.
 * @ENTRY_TXD_BURST: This frame belongs to the same burst event.
 * @ENTRY_TXD_ACK: An ACK is required for this frame.
 */
enum txentry_desc_flags {
	ENTRY_TXD_RTS_FRAME,
	ENTRY_TXD_OFDM_RATE,
	ENTRY_TXD_MORE_FRAG,
	ENTRY_TXD_REQ_TIMESTAMP,
	ENTRY_TXD_BURST,
	ENTRY_TXD_ACK,
};

/**
 * struct txentry_desc: TX Entry descriptor
 *
 * Summary of information for the frame descriptor before sending a TX frame.
 *
 * @flags: Descriptor flags (See &enum queue_entry_flags).
 * @queue: Queue identification (See &enum data_queue_qid).
 * @length_high: PLCP length high word.
 * @length_low: PLCP length low word.
 * @signal: PLCP signal.
 * @service: PLCP service.
 * @aifs: AIFS value.
 * @ifs: IFS value.
 * @cw_min: cwmin value.
 * @cw_max: cwmax value.
 */
struct txentry_desc {
	unsigned long flags;

	enum data_queue_qid queue;

	u16 length_high;
	u16 length_low;
	u16 signal;
	u16 service;

	int aifs;
	int ifs;
	int cw_min;
	int cw_max;
};

/**
 * enum queue_entry_flags: Status flags for queue entry
 *
 * @ENTRY_BCN_ASSIGNED: This entry has been assigned to an interface.
 *	As long as this bit is set, this entry may only be touched
 *	through the interface structure.
 * @ENTRY_OWNER_DEVICE_DATA: This entry is owned by the device for data
 *	transfer (either TX or RX depending on the queue). The entry should
 *	only be touched after the device has signaled it is done with it.
 * @ENTRY_OWNER_DEVICE_CRYPTO: This entry is owned by the device for data
 *	encryption or decryption. The entry should only be touched after
 *	the device has signaled it is done with it.
 */

enum queue_entry_flags {
	ENTRY_BCN_ASSIGNED,
	ENTRY_OWNER_DEVICE_DATA,
	ENTRY_OWNER_DEVICE_CRYPTO,
};

/**
 * struct queue_entry: Entry inside the &struct data_queue
 *
 * @flags: Entry flags, see &enum queue_entry_flags.
 * @queue: The data queue (&struct data_queue) to which this entry belongs.
 * @skb: The buffer which is currently being transmitted (for TX queue),
 *	or used to directly recieve data in (for RX queue).
 * @entry_idx: The entry index number.
 * @priv_data: Private data belonging to this queue entry. The pointer
 *	points to data specific to a particular driver and queue type.
 */
struct queue_entry {
	unsigned long flags;

	struct data_queue *queue;

	struct sk_buff *skb;

	unsigned int entry_idx;

	void *priv_data;
};

/**
 * enum queue_index: Queue index type
 *
 * @Q_INDEX: Index pointer to the current entry in the queue, if this entry is
 *	owned by the hardware then the queue is considered to be full.
 * @Q_INDEX_DONE: Index pointer to the next entry which will be completed by
 *	the hardware and for which we need to run the txdone handler. If this
 *	entry is not owned by the hardware the queue is considered to be empty.
 * @Q_INDEX_CRYPTO: Index pointer to the next entry which encryption/decription
 *	will be completed by the hardware next.
 * @Q_INDEX_MAX: Keep last, used in &struct data_queue to determine the size
 *	of the index array.
 */
enum queue_index {
	Q_INDEX,
	Q_INDEX_DONE,
	Q_INDEX_CRYPTO,
	Q_INDEX_MAX,
};

/**
 * struct data_queue: Data queue
 *
 * @rt2x00dev: Pointer to main &struct rt2x00dev where this queue belongs to.
 * @entries: Base address of the &struct queue_entry which are
 *	part of this queue.
 * @qid: The queue identification, see &enum data_queue_qid.
 * @lock: Spinlock to protect index handling. Whenever @index, @index_done or
 *	@index_crypt needs to be changed this lock should be grabbed to prevent
 *	index corruption due to concurrency.
 * @count: Number of frames handled in the queue.
 * @limit: Maximum number of entries in the queue.
 * @length: Number of frames in queue.
 * @index: Index pointers to entry positions in the queue,
 *	use &enum queue_index to get a specific index field.
 * @aifs: The aifs value for outgoing frames (field ignored in RX queue).
 * @cw_min: The cw min value for outgoing frames (field ignored in RX queue).
 * @cw_max: The cw max value for outgoing frames (field ignored in RX queue).
 * @data_size: Maximum data size for the frames in this queue.
 * @desc_size: Hardware descriptor size for the data in this queue.
 */
struct data_queue {
	struct rt2x00_dev *rt2x00dev;
	struct queue_entry *entries;

	enum data_queue_qid qid;

	spinlock_t lock;
	unsigned int count;
	unsigned short limit;
	unsigned short length;
	unsigned short index[Q_INDEX_MAX];

	unsigned short aifs;
	unsigned short cw_min;
	unsigned short cw_max;

	unsigned short data_size;
	unsigned short desc_size;
};

/**
 * struct data_queue_desc: Data queue description
 *
 * The information in this structure is used by drivers
 * to inform rt2x00lib about the creation of the data queue.
 *
 * @entry_num: Maximum number of entries for a queue.
 * @data_size: Maximum data size for the frames in this queue.
 * @desc_size: Hardware descriptor size for the data in this queue.
 * @priv_size: Size of per-queue_entry private data.
 */
struct data_queue_desc {
	unsigned short entry_num;
	unsigned short data_size;
	unsigned short desc_size;
	unsigned short priv_size;
};

/**
 * queue_end - Return pointer to the last queue (HELPER MACRO).
 * @__dev: Pointer to &struct rt2x00_dev
 *
 * Using the base rx pointer and the maximum number of available queues,
 * this macro will return the address of 1 position beyond  the end of the
 * queues array.
 */
#define queue_end(__dev) \
	&(__dev)->rx[(__dev)->data_queues]

/**
 * tx_queue_end - Return pointer to the last TX queue (HELPER MACRO).
 * @__dev: Pointer to &struct rt2x00_dev
 *
 * Using the base tx pointer and the maximum number of available TX
 * queues, this macro will return the address of 1 position beyond
 * the end of the TX queue array.
 */
#define tx_queue_end(__dev) \
	&(__dev)->tx[(__dev)->hw->queues]

/**
 * queue_loop - Loop through the queues within a specific range (HELPER MACRO).
 * @__entry: Pointer where the current queue entry will be stored in.
 * @__start: Start queue pointer.
 * @__end: End queue pointer.
 *
 * This macro will loop through all queues between &__start and &__end.
 */
#define queue_loop(__entry, __start, __end)			\
	for ((__entry) = (__start);				\
	     prefetch(&(__entry)[1]), (__entry) != (__end);	\
	     (__entry) = &(__entry)[1])

/**
 * queue_for_each - Loop through all queues
 * @__dev: Pointer to &struct rt2x00_dev
 * @__entry: Pointer where the current queue entry will be stored in.
 *
 * This macro will loop through all available queues.
 */
#define queue_for_each(__dev, __entry) \
	queue_loop(__entry, (__dev)->rx, queue_end(__dev))

/**
 * tx_queue_for_each - Loop through the TX queues
 * @__dev: Pointer to &struct rt2x00_dev
 * @__entry: Pointer where the current queue entry will be stored in.
 *
 * This macro will loop through all TX related queues excluding
 * the Beacon and Atim queues.
 */
#define tx_queue_for_each(__dev, __entry) \
	queue_loop(__entry, (__dev)->tx, tx_queue_end(__dev))

/**
 * txall_queue_for_each - Loop through all TX related queues
 * @__dev: Pointer to &struct rt2x00_dev
 * @__entry: Pointer where the current queue entry will be stored in.
 *
 * This macro will loop through all TX related queues including
 * the Beacon and Atim queues.
 */
#define txall_queue_for_each(__dev, __entry) \
	queue_loop(__entry, (__dev)->tx, queue_end(__dev))

/**
 * rt2x00queue_empty - Check if the queue is empty.
 * @queue: Queue to check if empty.
 */
static inline int rt2x00queue_empty(struct data_queue *queue)
{
	return queue->length == 0;
}

/**
 * rt2x00queue_full - Check if the queue is full.
 * @queue: Queue to check if full.
 */
static inline int rt2x00queue_full(struct data_queue *queue)
{
	return queue->length == queue->limit;
}

/**
 * rt2x00queue_free - Check the number of available entries in queue.
 * @queue: Queue to check.
 */
static inline int rt2x00queue_available(struct data_queue *queue)
{
	return queue->limit - queue->length;
}

/**
 * rt2x00_desc_read - Read a word from the hardware descriptor.
 * @desc: Base descriptor address
 * @word: Word index from where the descriptor should be read.
 * @value: Address where the descriptor value should be written into.
 */
static inline void rt2x00_desc_read(__le32 *desc, const u8 word, u32 *value)
{
	*value = le32_to_cpu(desc[word]);
}

/**
 * rt2x00_desc_write - wrote a word to the hardware descriptor.
 * @desc: Base descriptor address
 * @word: Word index from where the descriptor should be written.
 * @value: Value that should be written into the descriptor.
 */
static inline void rt2x00_desc_write(__le32 *desc, const u8 word, u32 value)
{
	desc[word] = cpu_to_le32(value);
}

#endif /* RT2X00QUEUE_H */
