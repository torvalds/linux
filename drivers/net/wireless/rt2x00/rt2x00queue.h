/*
	Copyright (C) 2004 - 2010 Ivo van Doorn <IvDoorn@gmail.com>
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
 * DOC: Entry frame size
 *
 * Ralink PCI devices demand the Frame size to be a multiple of 128 bytes,
 * for USB devices this restriction does not apply, but the value of
 * 2432 makes sense since it is big enough to contain the maximum fragment
 * size according to the ieee802.11 specs.
 * The aggregation size depends on support from the driver, but should
 * be something around 3840 bytes.
 */
#define DATA_FRAME_SIZE		2432
#define MGMT_FRAME_SIZE		256
#define AGGREGATION_SIZE	3840

/**
 * enum data_queue_qid: Queue identification
 *
 * @QID_AC_BE: AC BE queue
 * @QID_AC_BK: AC BK queue
 * @QID_AC_VI: AC VI queue
 * @QID_AC_VO: AC VO queue
 * @QID_HCCA: HCCA queue
 * @QID_MGMT: MGMT queue (prio queue)
 * @QID_RX: RX queue
 * @QID_OTHER: None of the above (don't use, only present for completeness)
 * @QID_BEACON: Beacon queue (value unspecified, don't send it to device)
 * @QID_ATIM: Atim queue (value unspeficied, don't send it to device)
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
	QID_BEACON,
	QID_ATIM,
};

/**
 * enum skb_frame_desc_flags: Flags for &struct skb_frame_desc
 *
 * @SKBDESC_DMA_MAPPED_RX: &skb_dma field has been mapped for RX
 * @SKBDESC_DMA_MAPPED_TX: &skb_dma field has been mapped for TX
 * @SKBDESC_IV_STRIPPED: Frame contained a IV/EIV provided by
 *	mac80211 but was stripped for processing by the driver.
 * @SKBDESC_NOT_MAC80211: Frame didn't originate from mac80211,
 *	don't try to pass it back.
 * @SKBDESC_DESC_IN_SKB: The descriptor is at the start of the
 *	skb, instead of in the desc field.
 */
enum skb_frame_desc_flags {
	SKBDESC_DMA_MAPPED_RX = 1 << 0,
	SKBDESC_DMA_MAPPED_TX = 1 << 1,
	SKBDESC_IV_STRIPPED = 1 << 2,
	SKBDESC_NOT_MAC80211 = 1 << 3,
	SKBDESC_DESC_IN_SKB = 1 << 4,
};

/**
 * struct skb_frame_desc: Descriptor information for the skb buffer
 *
 * This structure is placed over the driver_data array, this means that
 * this structure should not exceed the size of that array (40 bytes).
 *
 * @flags: Frame flags, see &enum skb_frame_desc_flags.
 * @desc_len: Length of the frame descriptor.
 * @tx_rate_idx: the index of the TX rate, used for TX status reporting
 * @tx_rate_flags: the TX rate flags, used for TX status reporting
 * @desc: Pointer to descriptor part of the frame.
 *	Note that this pointer could point to something outside
 *	of the scope of the skb->data pointer.
 * @iv: IV/EIV data used during encryption/decryption.
 * @skb_dma: (PCI-only) the DMA address associated with the sk buffer.
 * @entry: The entry to which this sk buffer belongs.
 */
struct skb_frame_desc {
	u8 flags;

	u8 desc_len;
	u8 tx_rate_idx;
	u8 tx_rate_flags;

	void *desc;

	__le32 iv[2];

	dma_addr_t skb_dma;

	struct queue_entry *entry;
};

/**
 * get_skb_frame_desc - Obtain the rt2x00 frame descriptor from a sk_buff.
 * @skb: &struct sk_buff from where we obtain the &struct skb_frame_desc
 */
static inline struct skb_frame_desc* get_skb_frame_desc(struct sk_buff *skb)
{
	BUILD_BUG_ON(sizeof(struct skb_frame_desc) >
		     IEEE80211_TX_INFO_DRIVER_DATA_SIZE);
	return (struct skb_frame_desc *)&IEEE80211_SKB_CB(skb)->driver_data;
}

/**
 * enum rxdone_entry_desc_flags: Flags for &struct rxdone_entry_desc
 *
 * @RXDONE_SIGNAL_PLCP: Signal field contains the plcp value.
 * @RXDONE_SIGNAL_BITRATE: Signal field contains the bitrate value.
 * @RXDONE_SIGNAL_MCS: Signal field contains the mcs value.
 * @RXDONE_MY_BSS: Does this frame originate from device's BSS.
 * @RXDONE_CRYPTO_IV: Driver provided IV/EIV data.
 * @RXDONE_CRYPTO_ICV: Driver provided ICV data.
 * @RXDONE_L2PAD: 802.11 payload has been padded to 4-byte boundary.
 */
enum rxdone_entry_desc_flags {
	RXDONE_SIGNAL_PLCP = BIT(0),
	RXDONE_SIGNAL_BITRATE = BIT(1),
	RXDONE_SIGNAL_MCS = BIT(2),
	RXDONE_MY_BSS = BIT(3),
	RXDONE_CRYPTO_IV = BIT(4),
	RXDONE_CRYPTO_ICV = BIT(5),
	RXDONE_L2PAD = BIT(6),
};

/**
 * RXDONE_SIGNAL_MASK - Define to mask off all &rxdone_entry_desc_flags flags
 * except for the RXDONE_SIGNAL_* flags. This is useful to convert the dev_flags
 * from &rxdone_entry_desc to a signal value type.
 */
#define RXDONE_SIGNAL_MASK \
	( RXDONE_SIGNAL_PLCP | RXDONE_SIGNAL_BITRATE | RXDONE_SIGNAL_MCS )

/**
 * struct rxdone_entry_desc: RX Entry descriptor
 *
 * Summary of information that has been read from the RX frame descriptor.
 *
 * @timestamp: RX Timestamp
 * @signal: Signal of the received frame.
 * @rssi: RSSI of the received frame.
 * @size: Data size of the received frame.
 * @flags: MAC80211 receive flags (See &enum mac80211_rx_flags).
 * @dev_flags: Ralink receive flags (See &enum rxdone_entry_desc_flags).
 * @rate_mode: Rate mode (See @enum rate_modulation).
 * @cipher: Cipher type used during decryption.
 * @cipher_status: Decryption status.
 * @iv: IV/EIV data used during decryption.
 * @icv: ICV data used during decryption.
 */
struct rxdone_entry_desc {
	u64 timestamp;
	int signal;
	int rssi;
	int size;
	int flags;
	int dev_flags;
	u16 rate_mode;
	u8 cipher;
	u8 cipher_status;

	__le32 iv[2];
	__le32 icv;
};

/**
 * enum txdone_entry_desc_flags: Flags for &struct txdone_entry_desc
 *
 * Every txdone report has to contain the basic result of the
 * transmission, either &TXDONE_UNKNOWN, &TXDONE_SUCCESS or
 * &TXDONE_FAILURE. The flag &TXDONE_FALLBACK can be used in
 * conjunction with all of these flags but should only be set
 * if retires > 0. The flag &TXDONE_EXCESSIVE_RETRY can only be used
 * in conjunction with &TXDONE_FAILURE.
 *
 * @TXDONE_UNKNOWN: Hardware could not determine success of transmission.
 * @TXDONE_SUCCESS: Frame was successfully send
 * @TXDONE_FALLBACK: Hardware used fallback rates for retries
 * @TXDONE_FAILURE: Frame was not successfully send
 * @TXDONE_EXCESSIVE_RETRY: In addition to &TXDONE_FAILURE, the
 *	frame transmission failed due to excessive retries.
 */
enum txdone_entry_desc_flags {
	TXDONE_UNKNOWN,
	TXDONE_SUCCESS,
	TXDONE_FALLBACK,
	TXDONE_FAILURE,
	TXDONE_EXCESSIVE_RETRY,
};

/**
 * struct txdone_entry_desc: TX done entry descriptor
 *
 * Summary of information that has been read from the TX frame descriptor
 * after the device is done with transmission.
 *
 * @flags: TX done flags (See &enum txdone_entry_desc_flags).
 * @retry: Retry count.
 */
struct txdone_entry_desc {
	unsigned long flags;
	int retry;
};

/**
 * enum txentry_desc_flags: Status flags for TX entry descriptor
 *
 * @ENTRY_TXD_RTS_FRAME: This frame is a RTS frame.
 * @ENTRY_TXD_CTS_FRAME: This frame is a CTS-to-self frame.
 * @ENTRY_TXD_GENERATE_SEQ: This frame requires sequence counter.
 * @ENTRY_TXD_FIRST_FRAGMENT: This is the first frame.
 * @ENTRY_TXD_MORE_FRAG: This frame is followed by another fragment.
 * @ENTRY_TXD_REQ_TIMESTAMP: Require timestamp to be inserted.
 * @ENTRY_TXD_BURST: This frame belongs to the same burst event.
 * @ENTRY_TXD_ACK: An ACK is required for this frame.
 * @ENTRY_TXD_RETRY_MODE: When set, the long retry count is used.
 * @ENTRY_TXD_ENCRYPT: This frame should be encrypted.
 * @ENTRY_TXD_ENCRYPT_PAIRWISE: Use pairwise key table (instead of shared).
 * @ENTRY_TXD_ENCRYPT_IV: Generate IV/EIV in hardware.
 * @ENTRY_TXD_ENCRYPT_MMIC: Generate MIC in hardware.
 * @ENTRY_TXD_HT_AMPDU: This frame is part of an AMPDU.
 * @ENTRY_TXD_HT_BW_40: Use 40MHz Bandwidth.
 * @ENTRY_TXD_HT_SHORT_GI: Use short GI.
 * @ENTRY_TXD_HT_MIMO_PS: The receiving STA is in dynamic SM PS mode.
 */
enum txentry_desc_flags {
	ENTRY_TXD_RTS_FRAME,
	ENTRY_TXD_CTS_FRAME,
	ENTRY_TXD_GENERATE_SEQ,
	ENTRY_TXD_FIRST_FRAGMENT,
	ENTRY_TXD_MORE_FRAG,
	ENTRY_TXD_REQ_TIMESTAMP,
	ENTRY_TXD_BURST,
	ENTRY_TXD_ACK,
	ENTRY_TXD_RETRY_MODE,
	ENTRY_TXD_ENCRYPT,
	ENTRY_TXD_ENCRYPT_PAIRWISE,
	ENTRY_TXD_ENCRYPT_IV,
	ENTRY_TXD_ENCRYPT_MMIC,
	ENTRY_TXD_HT_AMPDU,
	ENTRY_TXD_HT_BW_40,
	ENTRY_TXD_HT_SHORT_GI,
	ENTRY_TXD_HT_MIMO_PS,
};

/**
 * struct txentry_desc: TX Entry descriptor
 *
 * Summary of information for the frame descriptor before sending a TX frame.
 *
 * @flags: Descriptor flags (See &enum queue_entry_flags).
 * @length: Length of the entire frame.
 * @header_length: Length of 802.11 header.
 * @length_high: PLCP length high word.
 * @length_low: PLCP length low word.
 * @signal: PLCP signal.
 * @service: PLCP service.
 * @msc: MCS.
 * @stbc: STBC.
 * @ba_size: BA size.
 * @rate_mode: Rate mode (See @enum rate_modulation).
 * @mpdu_density: MDPU density.
 * @retry_limit: Max number of retries.
 * @ifs: IFS value.
 * @txop: IFS value for 11n capable chips.
 * @cipher: Cipher type used for encryption.
 * @key_idx: Key index used for encryption.
 * @iv_offset: Position where IV should be inserted by hardware.
 * @iv_len: Length of IV data.
 */
struct txentry_desc {
	unsigned long flags;

	u16 length;
	u16 header_length;

	u16 length_high;
	u16 length_low;
	u16 signal;
	u16 service;

	u16 mcs;
	u16 stbc;
	u16 ba_size;
	u16 rate_mode;
	u16 mpdu_density;

	short retry_limit;
	short ifs;
	short txop;

	enum cipher cipher;
	u16 key_idx;
	u16 iv_offset;
	u16 iv_len;
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
 * @ENTRY_DATA_PENDING: This entry contains a valid frame and is waiting
 *	for the signal to start sending.
 * @ENTRY_DATA_IO_FAILED: Hardware indicated that an IO error occured
 *	while transfering the data to the hardware. No TX status report will
 *	be expected from the hardware.
 */
enum queue_entry_flags {
	ENTRY_BCN_ASSIGNED,
	ENTRY_OWNER_DEVICE_DATA,
	ENTRY_DATA_PENDING,
	ENTRY_DATA_IO_FAILED
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
 * @Q_INDEX_DMA_DONE: Index pointer for the next entry which will have been
 *	transfered to the hardware.
 * @Q_INDEX_DONE: Index pointer to the next entry which will be completed by
 *	the hardware and for which we need to run the txdone handler. If this
 *	entry is not owned by the hardware the queue is considered to be empty.
 * @Q_INDEX_MAX: Keep last, used in &struct data_queue to determine the size
 *	of the index array.
 */
enum queue_index {
	Q_INDEX,
	Q_INDEX_DMA_DONE,
	Q_INDEX_DONE,
	Q_INDEX_MAX,
};

/**
 * enum data_queue_flags: Status flags for data queues
 *
 * @QUEUE_STARTED: The queue has been started. Fox RX queues this means the
 *	device might be DMA'ing skbuffers. TX queues will accept skbuffers to
 *	be transmitted and beacon queues will start beaconing the configured
 *	beacons.
 * @QUEUE_PAUSED: The queue has been started but is currently paused.
 *	When this bit is set, the queue has been stopped in mac80211,
 *	preventing new frames to be enqueued. However, a few frames
 *	might still appear shortly after the pausing...
 */
enum data_queue_flags {
	QUEUE_STARTED,
	QUEUE_PAUSED,
};

/**
 * struct data_queue: Data queue
 *
 * @rt2x00dev: Pointer to main &struct rt2x00dev where this queue belongs to.
 * @entries: Base address of the &struct queue_entry which are
 *	part of this queue.
 * @qid: The queue identification, see &enum data_queue_qid.
 * @flags: Entry flags, see &enum queue_entry_flags.
 * @status_lock: The mutex for protecting the start/stop/flush
 *	handling on this queue.
 * @index_lock: Spinlock to protect index handling. Whenever @index, @index_done or
 *	@index_crypt needs to be changed this lock should be grabbed to prevent
 *	index corruption due to concurrency.
 * @count: Number of frames handled in the queue.
 * @limit: Maximum number of entries in the queue.
 * @threshold: Minimum number of free entries before queue is kicked by force.
 * @length: Number of frames in queue.
 * @index: Index pointers to entry positions in the queue,
 *	use &enum queue_index to get a specific index field.
 * @txop: maximum burst time.
 * @aifs: The aifs value for outgoing frames (field ignored in RX queue).
 * @cw_min: The cw min value for outgoing frames (field ignored in RX queue).
 * @cw_max: The cw max value for outgoing frames (field ignored in RX queue).
 * @data_size: Maximum data size for the frames in this queue.
 * @desc_size: Hardware descriptor size for the data in this queue.
 * @usb_endpoint: Device endpoint used for communication (USB only)
 * @usb_maxpacket: Max packet size for given endpoint (USB only)
 */
struct data_queue {
	struct rt2x00_dev *rt2x00dev;
	struct queue_entry *entries;

	enum data_queue_qid qid;
	unsigned long flags;

	struct mutex status_lock;
	spinlock_t index_lock;

	unsigned int count;
	unsigned short limit;
	unsigned short threshold;
	unsigned short length;
	unsigned short index[Q_INDEX_MAX];
	unsigned long last_action[Q_INDEX_MAX];

	unsigned short txop;
	unsigned short aifs;
	unsigned short cw_min;
	unsigned short cw_max;

	unsigned short data_size;
	unsigned short desc_size;

	unsigned short usb_endpoint;
	unsigned short usb_maxpacket;
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
	&(__dev)->tx[(__dev)->ops->tx_queues]

/**
 * queue_next - Return pointer to next queue in list (HELPER MACRO).
 * @__queue: Current queue for which we need the next queue
 *
 * Using the current queue address we take the address directly
 * after the queue to take the next queue. Note that this macro
 * should be used carefully since it does not protect against
 * moving past the end of the list. (See macros &queue_end and
 * &tx_queue_end for determining the end of the queue).
 */
#define queue_next(__queue) \
	&(__queue)[1]

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
	     prefetch(queue_next(__entry)), (__entry) != (__end);\
	     (__entry) = queue_next(__entry))

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
 * rt2x00queue_for_each_entry - Loop through all entries in the queue
 * @queue: Pointer to @data_queue
 * @start: &enum queue_index Pointer to start index
 * @end: &enum queue_index Pointer to end index
 * @fn: The function to call for each &struct queue_entry
 *
 * This will walk through all entries in the queue, in chronological
 * order. This means it will start at the current @start pointer
 * and will walk through the queue until it reaches the @end pointer.
 */
void rt2x00queue_for_each_entry(struct data_queue *queue,
				enum queue_index start,
				enum queue_index end,
				void (*fn)(struct queue_entry *entry));

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
 * rt2x00queue_threshold - Check if the queue is below threshold
 * @queue: Queue to check.
 */
static inline int rt2x00queue_threshold(struct data_queue *queue)
{
	return rt2x00queue_available(queue) < queue->threshold;
}

/**
 * rt2x00queue_status_timeout - Check if a timeout occured for STATUS reports
 * @queue: Queue to check.
 */
static inline int rt2x00queue_status_timeout(struct data_queue *queue)
{
	return time_after(queue->last_action[Q_INDEX_DMA_DONE],
			  queue->last_action[Q_INDEX_DONE] + (HZ / 10));
}

/**
 * rt2x00queue_timeout - Check if a timeout occured for DMA transfers
 * @queue: Queue to check.
 */
static inline int rt2x00queue_dma_timeout(struct data_queue *queue)
{
	return time_after(queue->last_action[Q_INDEX],
			  queue->last_action[Q_INDEX_DMA_DONE] + (HZ / 10));
}

/**
 * _rt2x00_desc_read - Read a word from the hardware descriptor.
 * @desc: Base descriptor address
 * @word: Word index from where the descriptor should be read.
 * @value: Address where the descriptor value should be written into.
 */
static inline void _rt2x00_desc_read(__le32 *desc, const u8 word, __le32 *value)
{
	*value = desc[word];
}

/**
 * rt2x00_desc_read - Read a word from the hardware descriptor, this
 * function will take care of the byte ordering.
 * @desc: Base descriptor address
 * @word: Word index from where the descriptor should be read.
 * @value: Address where the descriptor value should be written into.
 */
static inline void rt2x00_desc_read(__le32 *desc, const u8 word, u32 *value)
{
	__le32 tmp;
	_rt2x00_desc_read(desc, word, &tmp);
	*value = le32_to_cpu(tmp);
}

/**
 * rt2x00_desc_write - write a word to the hardware descriptor, this
 * function will take care of the byte ordering.
 * @desc: Base descriptor address
 * @word: Word index from where the descriptor should be written.
 * @value: Value that should be written into the descriptor.
 */
static inline void _rt2x00_desc_write(__le32 *desc, const u8 word, __le32 value)
{
	desc[word] = value;
}

/**
 * rt2x00_desc_write - write a word to the hardware descriptor.
 * @desc: Base descriptor address
 * @word: Word index from where the descriptor should be written.
 * @value: Value that should be written into the descriptor.
 */
static inline void rt2x00_desc_write(__le32 *desc, const u8 word, u32 value)
{
	_rt2x00_desc_write(desc, word, cpu_to_le32(value));
}

#endif /* RT2X00QUEUE_H */
