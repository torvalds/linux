/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2021, Intel Corporation. */

#ifndef _ICE_PTP_H_
#define _ICE_PTP_H_

#include <linux/ptp_clock_kernel.h>
#include <linux/kthread.h>

#include "ice_ptp_hw.h"

/* The ice hardware captures Tx hardware timestamps in the PHY. The timestamp
 * is stored in a buffer of registers. Depending on the specific hardware,
 * this buffer might be shared across multiple PHY ports.
 *
 * On transmit of a packet to be timestamped, software is responsible for
 * selecting an open index. Hardware makes no attempt to lock or prevent
 * re-use of an index for multiple packets.
 *
 * To handle this, timestamp indexes must be tracked by software to ensure
 * that an index is not re-used for multiple transmitted packets. The
 * structures and functions declared in this file track the available Tx
 * register indexes, as well as provide storage for the SKB pointers.
 *
 * To allow multiple ports to access the shared register block independently,
 * the blocks are split up so that indexes are assigned to each port based on
 * hardware logical port number.
 *
 * The timestamp blocks are handled differently for E810- and E822-based
 * devices. In E810 devices, each port has its own block of timestamps, while in
 * E822 there is a need to logically break the block of registers into smaller
 * chunks based on the port number to avoid collisions.
 *
 * Example for port 5 in E810:
 *  +--------+--------+--------+--------+--------+--------+--------+--------+
 *  |register|register|register|register|register|register|register|register|
 *  | block  | block  | block  | block  | block  | block  | block  | block  |
 *  |  for   |  for   |  for   |  for   |  for   |  for   |  for   |  for   |
 *  | port 0 | port 1 | port 2 | port 3 | port 4 | port 5 | port 6 | port 7 |
 *  +--------+--------+--------+--------+--------+--------+--------+--------+
 *                                               ^^
 *                                               ||
 *                                               |---  quad offset is always 0
 *                                               ---- quad number
 *
 * Example for port 5 in E822:
 * +-----------------------------+-----------------------------+
 * |  register block for quad 0  |  register block for quad 1  |
 * |+------+------+------+------+|+------+------+------+------+|
 * ||port 0|port 1|port 2|port 3|||port 0|port 1|port 2|port 3||
 * |+------+------+------+------+|+------+------+------+------+|
 * +-----------------------------+-------^---------------------+
 *                                ^      |
 *                                |      --- quad offset*
 *                                ---- quad number
 *
 *   * PHY port 5 is port 1 in quad 1
 *
 */

/**
 * struct ice_tx_tstamp - Tracking for a single Tx timestamp
 * @skb: pointer to the SKB for this timestamp request
 * @start: jiffies when the timestamp was first requested
 * @cached_tstamp: last read timestamp
 *
 * This structure tracks a single timestamp request. The SKB pointer is
 * provided when initiating a request. The start time is used to ensure that
 * we discard old requests that were not fulfilled within a 2 second time
 * window.
 * Timestamp values in the PHY are read only and do not get cleared except at
 * hardware reset or when a new timestamp value is captured.
 *
 * Some PHY types do not provide a "ready" bitmap indicating which timestamp
 * indexes are valid. In these cases, we use a cached_tstamp to keep track of
 * the last timestamp we read for a given index. If the current timestamp
 * value is the same as the cached value, we assume a new timestamp hasn't
 * been captured. This avoids reporting stale timestamps to the stack. This is
 * only done if the has_ready_bitmap flag is not set in ice_ptp_tx structure.
 */
struct ice_tx_tstamp {
	struct sk_buff *skb;
	unsigned long start;
	u64 cached_tstamp;
};

/**
 * enum ice_tx_tstamp_work - Status of Tx timestamp work function
 * @ICE_TX_TSTAMP_WORK_DONE: Tx timestamp processing is complete
 * @ICE_TX_TSTAMP_WORK_PENDING: More Tx timestamps are pending
 */
enum ice_tx_tstamp_work {
	ICE_TX_TSTAMP_WORK_DONE = 0,
	ICE_TX_TSTAMP_WORK_PENDING,
};

/**
 * struct ice_ptp_tx - Tracking structure for all Tx timestamp requests on a port
 * @lock: lock to prevent concurrent access to fields of this struct
 * @tstamps: array of len to store outstanding requests
 * @in_use: bitmap of len to indicate which slots are in use
 * @stale: bitmap of len to indicate slots which have stale timestamps
 * @block: which memory block (quad or port) the timestamps are captured in
 * @offset: offset into timestamp block to get the real index
 * @len: length of the tstamps and in_use fields.
 * @init: if true, the tracker is initialized;
 * @calibrating: if true, the PHY is calibrating the Tx offset. During this
 *               window, timestamps are temporarily disabled.
 * @has_ready_bitmap: if true, the hardware has a valid Tx timestamp ready
 *                    bitmap register. If false, fall back to verifying new
 *                    timestamp values against previously cached copy.
 * @last_ll_ts_idx_read: index of the last LL TS read by the FW
 */
struct ice_ptp_tx {
	spinlock_t lock; /* lock protecting in_use bitmap */
	struct ice_tx_tstamp *tstamps;
	unsigned long *in_use;
	unsigned long *stale;
	u8 block;
	u8 offset;
	u8 len;
	u8 init : 1;
	u8 calibrating : 1;
	u8 has_ready_bitmap : 1;
	s8 last_ll_ts_idx_read;
};

/* Quad and port information for initializing timestamp blocks */
#define INDEX_PER_QUAD			64
#define INDEX_PER_PORT_E82X		16
#define INDEX_PER_PORT			64

/**
 * struct ice_ptp_port - data used to initialize an external port for PTP
 *
 * This structure contains data indicating whether a single external port is
 * ready for PTP functionality. It is used to track the port initialization
 * and determine when the port's PHY offset is valid.
 *
 * @list_node: list member structure
 * @tx: Tx timestamp tracking for this port
 * @ov_work: delayed work task for tracking when PHY offset is valid
 * @ps_lock: mutex used to protect the overall PTP PHY start procedure
 * @link_up: indicates whether the link is up
 * @tx_fifo_busy_cnt: number of times the Tx FIFO was busy
 * @port_num: the port number this structure represents
 */
struct ice_ptp_port {
	struct list_head list_node;
	struct ice_ptp_tx tx;
	struct kthread_delayed_work ov_work;
	struct mutex ps_lock; /* protects overall PTP PHY start procedure */
	bool link_up;
	u8 tx_fifo_busy_cnt;
	u8 port_num;
};

enum ice_ptp_tx_interrupt {
	ICE_PTP_TX_INTERRUPT_NONE = 0,
	ICE_PTP_TX_INTERRUPT_SELF,
	ICE_PTP_TX_INTERRUPT_ALL,
};

#define GLTSYN_TGT_H_IDX_MAX		4

enum ice_ptp_state {
	ICE_PTP_UNINIT = 0,
	ICE_PTP_INITIALIZING,
	ICE_PTP_READY,
	ICE_PTP_RESETTING,
	ICE_PTP_ERROR,
};

enum ice_ptp_pin {
	SDP0 = 0,
	SDP1,
	SDP2,
	SDP3,
	TIME_SYNC,
	ONE_PPS
};

enum ice_ptp_pin_nvm {
	GNSS = 0,
	SMA1,
	UFL1,
	SMA2,
	UFL2,
	NUM_PTP_PINS_NVM,
	GPIO_NA = 9
};

/* Per-channel register definitions */
#define GLTSYN_AUX_OUT(_chan, _idx)	(GLTSYN_AUX_OUT_0(_idx) + ((_chan) * 8))
#define GLTSYN_AUX_IN(_chan, _idx)	(GLTSYN_AUX_IN_0(_idx) + ((_chan) * 8))
#define GLTSYN_CLKO(_chan, _idx)	(GLTSYN_CLKO_0(_idx) + ((_chan) * 8))
#define GLTSYN_TGT_L(_chan, _idx)	(GLTSYN_TGT_L_0(_idx) + ((_chan) * 16))
#define GLTSYN_TGT_H(_chan, _idx)	(GLTSYN_TGT_H_0(_idx) + ((_chan) * 16))
#define GLTSYN_EVNT_L(_chan, _idx)	(GLTSYN_EVNT_L_0(_idx) + ((_chan) * 16))
#define GLTSYN_EVNT_H(_chan, _idx)	(GLTSYN_EVNT_H_0(_idx) + ((_chan) * 16))
#define GLTSYN_EVNT_H_IDX_MAX		3

/* Pin definitions for PTP */
#define ICE_N_PINS_MAX			6
#define ICE_SMA_PINS_NUM		4
#define ICE_PIN_DESC_ARR_LEN(_arr)	(sizeof(_arr) / \
					 sizeof(struct ice_ptp_pin_desc))

/**
 * struct ice_ptp_pin_desc - hardware pin description data
 * @name_idx: index of the name of pin in ice_pin_names
 * @gpio: the associated GPIO input and output pins
 * @delay: input and output signal delays in nanoseconds
 *
 * Structure describing a PTP-capable GPIO pin that extends ptp_pin_desc array
 * for the device. Device families have separate sets of available pins with
 * varying restrictions.
 */
struct ice_ptp_pin_desc {
	int name_idx;
	int gpio[2];
	unsigned int delay[2];
};

/**
 * struct ice_ptp - data used for integrating with CONFIG_PTP_1588_CLOCK
 * @state: current state of PTP state machine
 * @tx_interrupt_mode: the TX interrupt mode for the PTP clock
 * @port: data for the PHY port initialization procedure
 * @work: delayed work function for periodic tasks
 * @cached_phc_time: a cached copy of the PHC time for timestamp extension
 * @cached_phc_jiffies: jiffies when cached_phc_time was last updated
 * @kworker: kwork thread for handling periodic work
 * @ext_ts_irq: the external timestamp IRQ in use
 * @pin_desc: structure defining pins
 * @ice_pin_desc: internal structure describing pin relations
 * @perout_rqs: cached periodic output requests
 * @extts_rqs: cached external timestamp requests
 * @info: structure defining PTP hardware capabilities
 * @clock: pointer to registered PTP clock device
 * @tstamp_config: hardware timestamping configuration
 * @reset_time: kernel time after clock stop on reset
 * @tx_hwtstamp_skipped: number of Tx time stamp requests skipped
 * @tx_hwtstamp_timeouts: number of Tx skbs discarded with no time stamp
 * @tx_hwtstamp_flushed: number of Tx skbs flushed due to interface closed
 * @tx_hwtstamp_discarded: number of Tx skbs discarded due to cached PHC time
 *                         being too old to correctly extend timestamp
 * @late_cached_phc_updates: number of times cached PHC update is late
 */
struct ice_ptp {
	enum ice_ptp_state state;
	enum ice_ptp_tx_interrupt tx_interrupt_mode;
	struct ice_ptp_port port;
	struct kthread_delayed_work work;
	u64 cached_phc_time;
	unsigned long cached_phc_jiffies;
	struct kthread_worker *kworker;
	u8 ext_ts_irq;
	struct ptp_pin_desc pin_desc[ICE_N_PINS_MAX];
	const struct ice_ptp_pin_desc *ice_pin_desc;
	struct ptp_perout_request perout_rqs[GLTSYN_TGT_H_IDX_MAX];
	struct ptp_extts_request extts_rqs[GLTSYN_EVNT_H_IDX_MAX];
	struct ptp_clock_info info;
	struct ptp_clock *clock;
	struct hwtstamp_config tstamp_config;
	u64 reset_time;
	u32 tx_hwtstamp_skipped;
	u32 tx_hwtstamp_timeouts;
	u32 tx_hwtstamp_flushed;
	u32 tx_hwtstamp_discarded;
	u32 late_cached_phc_updates;
};

#define __ptp_port_to_ptp(p) \
	container_of((p), struct ice_ptp, port)
#define ptp_port_to_pf(p) \
	container_of(__ptp_port_to_ptp((p)), struct ice_pf, ptp)

#define __ptp_info_to_ptp(i) \
	container_of((i), struct ice_ptp, info)
#define ptp_info_to_pf(i) \
	container_of(__ptp_info_to_ptp((i)), struct ice_pf, ptp)

#define PFTSYN_SEM_BYTES		4
#define PTP_SHARED_CLK_IDX_VALID	BIT(31)
#define TS_CMD_MASK			0xF
#define SYNC_EXEC_CMD			0x3
#define ICE_PTP_TS_VALID		BIT(0)

#define FIFO_EMPTY			BIT(2)
#define FIFO_OK				0xFF
#define ICE_PTP_FIFO_NUM_CHECKS		5

#if IS_ENABLED(CONFIG_PTP_1588_CLOCK)
int ice_ptp_clock_index(struct ice_pf *pf);
struct ice_pf;
int ice_ptp_set_ts_config(struct ice_pf *pf, struct ifreq *ifr);
int ice_ptp_get_ts_config(struct ice_pf *pf, struct ifreq *ifr);
void ice_ptp_restore_timestamp_mode(struct ice_pf *pf);

void ice_ptp_extts_event(struct ice_pf *pf);
s8 ice_ptp_request_ts(struct ice_ptp_tx *tx, struct sk_buff *skb);
void ice_ptp_req_tx_single_tstamp(struct ice_ptp_tx *tx, u8 idx);
void ice_ptp_complete_tx_single_tstamp(struct ice_ptp_tx *tx);
enum ice_tx_tstamp_work ice_ptp_process_ts(struct ice_pf *pf);
irqreturn_t ice_ptp_ts_irq(struct ice_pf *pf);
u64 ice_ptp_read_src_clk_reg(struct ice_pf *pf,
			     struct ptp_system_timestamp *sts);

u64 ice_ptp_get_rx_hwts(const union ice_32b_rx_flex_desc *rx_desc,
			const struct ice_pkt_ctx *pkt_ctx);
void ice_ptp_rebuild(struct ice_pf *pf, enum ice_reset_req reset_type);
void ice_ptp_prepare_for_reset(struct ice_pf *pf,
			       enum ice_reset_req reset_type);
void ice_ptp_init(struct ice_pf *pf);
void ice_ptp_release(struct ice_pf *pf);
void ice_ptp_link_change(struct ice_pf *pf, bool linkup);
#else /* IS_ENABLED(CONFIG_PTP_1588_CLOCK) */
static inline int ice_ptp_set_ts_config(struct ice_pf *pf, struct ifreq *ifr)
{
	return -EOPNOTSUPP;
}

static inline int ice_ptp_get_ts_config(struct ice_pf *pf, struct ifreq *ifr)
{
	return -EOPNOTSUPP;
}

static inline void ice_ptp_restore_timestamp_mode(struct ice_pf *pf) { }
static inline void ice_ptp_extts_event(struct ice_pf *pf) { }
static inline s8
ice_ptp_request_ts(struct ice_ptp_tx *tx, struct sk_buff *skb)
{
	return -1;
}

static inline void ice_ptp_req_tx_single_tstamp(struct ice_ptp_tx *tx, u8 idx)
{ }

static inline void ice_ptp_complete_tx_single_tstamp(struct ice_ptp_tx *tx) { }

static inline bool ice_ptp_process_ts(struct ice_pf *pf)
{
	return true;
}

static inline irqreturn_t ice_ptp_ts_irq(struct ice_pf *pf)
{
	return IRQ_HANDLED;
}

static inline u64 ice_ptp_read_src_clk_reg(struct ice_pf *pf,
					   struct ptp_system_timestamp *sts)
{
	return 0;
}

static inline u64
ice_ptp_get_rx_hwts(const union ice_32b_rx_flex_desc *rx_desc,
		    const struct ice_pkt_ctx *pkt_ctx)
{
	return 0;
}

static inline void ice_ptp_rebuild(struct ice_pf *pf,
				   enum ice_reset_req reset_type)
{
}

static inline void ice_ptp_prepare_for_reset(struct ice_pf *pf,
					     enum ice_reset_req reset_type)
{
}
static inline void ice_ptp_init(struct ice_pf *pf) { }
static inline void ice_ptp_release(struct ice_pf *pf) { }
static inline void ice_ptp_link_change(struct ice_pf *pf, bool linkup)
{
}

static inline int ice_ptp_clock_index(struct ice_pf *pf)
{
	return -1;
}
#endif /* IS_ENABLED(CONFIG_PTP_1588_CLOCK) */
#endif /* _ICE_PTP_H_ */
