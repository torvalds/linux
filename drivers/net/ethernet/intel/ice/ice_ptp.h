/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2021, Intel Corporation. */

#ifndef _ICE_PTP_H_
#define _ICE_PTP_H_

#include <linux/ptp_clock_kernel.h>
#include <linux/kthread.h>

#include "ice_ptp_hw.h"

enum ice_ptp_pin_e810 {
	GPIO_20 = 0,
	GPIO_21,
	GPIO_22,
	GPIO_23,
	NUM_PTP_PIN_E810
};

enum ice_ptp_pin_e810t {
	GNSS = 0,
	SMA1,
	UFL1,
	SMA2,
	UFL2,
	NUM_PTP_PINS_E810T
};

struct ice_perout_channel {
	bool ena;
	u32 gpio_pin;
	u64 period;
	u64 start_time;
};

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
 * only done if the verify_cached flag is set in ice_ptp_tx structure.
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
 * @verify_cached: if true, verify new timestamp differs from last read value
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
	u8 verify_cached : 1;
};

/* Quad and port information for initializing timestamp blocks */
#define INDEX_PER_QUAD			64
#define INDEX_PER_PORT_E822		16
#define INDEX_PER_PORT_E810		64

/**
 * struct ice_ptp_port - data used to initialize an external port for PTP
 *
 * This structure contains data indicating whether a single external port is
 * ready for PTP functionality. It is used to track the port initialization
 * and determine when the port's PHY offset is valid.
 *
 * @list_member: list member structure of auxiliary device
 * @tx: Tx timestamp tracking for this port
 * @aux_dev: auxiliary device associated with this port
 * @ov_work: delayed work task for tracking when PHY offset is valid
 * @ps_lock: mutex used to protect the overall PTP PHY start procedure
 * @link_up: indicates whether the link is up
 * @tx_fifo_busy_cnt: number of times the Tx FIFO was busy
 * @port_num: the port number this structure represents
 */
struct ice_ptp_port {
	struct list_head list_member;
	struct ice_ptp_tx tx;
	struct auxiliary_device aux_dev;
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

/**
 * struct ice_ptp_port_owner - data used to handle the PTP clock owner info
 *
 * This structure contains data necessary for the PTP clock owner to correctly
 * handle the timestamping feature for all attached ports.
 *
 * @aux_driver: the structure carring the auxiliary driver information
 * @ports: list of porst handled by this port owner
 * @lock: protect access to ports list
 */
struct ice_ptp_port_owner {
	struct auxiliary_driver aux_driver;
	struct list_head ports;
	struct mutex lock;
};

#define GLTSYN_TGT_H_IDX_MAX		4

/**
 * struct ice_ptp - data used for integrating with CONFIG_PTP_1588_CLOCK
 * @tx_interrupt_mode: the TX interrupt mode for the PTP clock
 * @port: data for the PHY port initialization procedure
 * @ports_owner: data for the auxiliary driver owner
 * @work: delayed work function for periodic tasks
 * @cached_phc_time: a cached copy of the PHC time for timestamp extension
 * @cached_phc_jiffies: jiffies when cached_phc_time was last updated
 * @ext_ts_chan: the external timestamp channel in use
 * @ext_ts_irq: the external timestamp IRQ in use
 * @kworker: kwork thread for handling periodic work
 * @perout_channels: periodic output data
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
	enum ice_ptp_tx_interrupt tx_interrupt_mode;
	struct ice_ptp_port port;
	struct ice_ptp_port_owner ports_owner;
	struct kthread_delayed_work work;
	u64 cached_phc_time;
	unsigned long cached_phc_jiffies;
	u8 ext_ts_chan;
	u8 ext_ts_irq;
	struct kthread_worker *kworker;
	struct ice_perout_channel perout_channels[GLTSYN_TGT_H_IDX_MAX];
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
/* Per-channel register definitions */
#define GLTSYN_AUX_OUT(_chan, _idx)	(GLTSYN_AUX_OUT_0(_idx) + ((_chan) * 8))
#define GLTSYN_AUX_IN(_chan, _idx)	(GLTSYN_AUX_IN_0(_idx) + ((_chan) * 8))
#define GLTSYN_CLKO(_chan, _idx)	(GLTSYN_CLKO_0(_idx) + ((_chan) * 8))
#define GLTSYN_TGT_L(_chan, _idx)	(GLTSYN_TGT_L_0(_idx) + ((_chan) * 16))
#define GLTSYN_TGT_H(_chan, _idx)	(GLTSYN_TGT_H_0(_idx) + ((_chan) * 16))
#define GLTSYN_EVNT_L(_chan, _idx)	(GLTSYN_EVNT_L_0(_idx) + ((_chan) * 16))
#define GLTSYN_EVNT_H(_chan, _idx)	(GLTSYN_EVNT_H_0(_idx) + ((_chan) * 16))
#define GLTSYN_EVNT_H_IDX_MAX		3

/* Pin definitions for PTP PPS out */
#define PPS_CLK_GEN_CHAN		3
#define PPS_CLK_SRC_CHAN		2
#define PPS_PIN_INDEX			5
#define TIME_SYNC_PIN_INDEX		4
#define N_EXT_TS_E810			3
#define N_PER_OUT_E810			4
#define N_PER_OUT_E810T			3
#define N_PER_OUT_NO_SMA_E810T		2
#define N_EXT_TS_NO_SMA_E810T		2
#define ETH_GLTSYN_ENA(_i)		(0x03000348 + ((_i) * 4))

#if IS_ENABLED(CONFIG_PTP_1588_CLOCK)
int ice_ptp_clock_index(struct ice_pf *pf);
struct ice_pf;
int ice_ptp_set_ts_config(struct ice_pf *pf, struct ifreq *ifr);
int ice_ptp_get_ts_config(struct ice_pf *pf, struct ifreq *ifr);
void ice_ptp_cfg_timestamp(struct ice_pf *pf, bool ena);

void ice_ptp_extts_event(struct ice_pf *pf);
s8 ice_ptp_request_ts(struct ice_ptp_tx *tx, struct sk_buff *skb);
enum ice_tx_tstamp_work ice_ptp_process_ts(struct ice_pf *pf);

void
ice_ptp_rx_hwtstamp(struct ice_rx_ring *rx_ring,
		    union ice_32b_rx_flex_desc *rx_desc, struct sk_buff *skb);
void ice_ptp_reset(struct ice_pf *pf);
void ice_ptp_prepare_for_reset(struct ice_pf *pf);
void ice_ptp_init(struct ice_pf *pf);
void ice_ptp_release(struct ice_pf *pf);
void ice_ptp_link_change(struct ice_pf *pf, u8 port, bool linkup);
#else /* IS_ENABLED(CONFIG_PTP_1588_CLOCK) */
static inline int ice_ptp_set_ts_config(struct ice_pf *pf, struct ifreq *ifr)
{
	return -EOPNOTSUPP;
}

static inline int ice_ptp_get_ts_config(struct ice_pf *pf, struct ifreq *ifr)
{
	return -EOPNOTSUPP;
}

static inline void ice_ptp_cfg_timestamp(struct ice_pf *pf, bool ena) { }

static inline void ice_ptp_extts_event(struct ice_pf *pf) { }
static inline s8
ice_ptp_request_ts(struct ice_ptp_tx *tx, struct sk_buff *skb)
{
	return -1;
}

static inline bool ice_ptp_process_ts(struct ice_pf *pf)
{
	return true;
}
static inline void
ice_ptp_rx_hwtstamp(struct ice_rx_ring *rx_ring,
		    union ice_32b_rx_flex_desc *rx_desc, struct sk_buff *skb) { }
static inline void ice_ptp_reset(struct ice_pf *pf) { }
static inline void ice_ptp_prepare_for_reset(struct ice_pf *pf) { }
static inline void ice_ptp_init(struct ice_pf *pf) { }
static inline void ice_ptp_release(struct ice_pf *pf) { }
static inline void ice_ptp_link_change(struct ice_pf *pf, u8 port, bool linkup)
{
}

static inline int ice_ptp_clock_index(struct ice_pf *pf)
{
	return -1;
}
#endif /* IS_ENABLED(CONFIG_PTP_1588_CLOCK) */
#endif /* _ICE_PTP_H_ */
