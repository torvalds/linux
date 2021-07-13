/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2021, Intel Corporation. */

#ifndef _ICE_PTP_H_
#define _ICE_PTP_H_

#include <linux/ptp_clock_kernel.h>
#include <linux/kthread.h>

#include "ice_ptp_hw.h"

enum ice_ptp_pin {
	GPIO_20 = 0,
	GPIO_21,
	GPIO_22,
	GPIO_23,
	NUM_ICE_PTP_PIN
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
 */

/**
 * struct ice_tx_tstamp - Tracking for a single Tx timestamp
 * @skb: pointer to the SKB for this timestamp request
 * @start: jiffies when the timestamp was first requested
 *
 * This structure tracks a single timestamp request. The SKB pointer is
 * provided when initiating a request. The start time is used to ensure that
 * we discard old requests that were not fulfilled within a 2 second time
 * window.
 */
struct ice_tx_tstamp {
	struct sk_buff *skb;
	unsigned long start;
};

/**
 * struct ice_ptp_tx - Tracking structure for all Tx timestamp requests on a port
 * @work: work function to handle processing of Tx timestamps
 * @lock: lock to prevent concurrent write to in_use bitmap
 * @tstamps: array of len to store outstanding requests
 * @in_use: bitmap of len to indicate which slots are in use
 * @quad: which quad the timestamps are captured in
 * @quad_offset: offset into timestamp block of the quad to get the real index
 * @len: length of the tstamps and in_use fields.
 * @init: if true, the tracker is initialized;
 */
struct ice_ptp_tx {
	struct kthread_work work;
	spinlock_t lock; /* lock protecting in_use bitmap */
	struct ice_tx_tstamp *tstamps;
	unsigned long *in_use;
	u8 quad;
	u8 quad_offset;
	u8 len;
	u8 init;
};

/* Quad and port information for initializing timestamp blocks */
#define INDEX_PER_QUAD			64
#define INDEX_PER_PORT			(INDEX_PER_QUAD / ICE_PORTS_PER_QUAD)

/**
 * struct ice_ptp_port - data used to initialize an external port for PTP
 *
 * This structure contains PTP data related to the external ports. Currently
 * it is used for tracking the Tx timestamps of a port. In the future this
 * structure will also hold information for the E822 port initialization
 * logic.
 *
 * @tx: Tx timestamp tracking for this port
 */
struct ice_ptp_port {
	struct ice_ptp_tx tx;
};

#define GLTSYN_TGT_H_IDX_MAX		4

/**
 * struct ice_ptp - data used for integrating with CONFIG_PTP_1588_CLOCK
 * @port: data for the PHY port initialization procedure
 * @work: delayed work function for periodic tasks
 * @extts_work: work function for handling external Tx timestamps
 * @cached_phc_time: a cached copy of the PHC time for timestamp extension
 * @ext_ts_chan: the external timestamp channel in use
 * @ext_ts_irq: the external timestamp IRQ in use
 * @kworker: kwork thread for handling periodic work
 * @perout_channels: periodic output data
 * @info: structure defining PTP hardware capabilities
 * @clock: pointer to registered PTP clock device
 * @tstamp_config: hardware timestamping configuration
 */
struct ice_ptp {
	struct ice_ptp_port port;
	struct kthread_delayed_work work;
	struct kthread_work extts_work;
	u64 cached_phc_time;
	u8 ext_ts_chan;
	u8 ext_ts_irq;
	struct kthread_worker *kworker;
	struct ice_perout_channel perout_channels[GLTSYN_TGT_H_IDX_MAX];
	struct ptp_clock_info info;
	struct ptp_clock *clock;
	struct hwtstamp_config tstamp_config;
};

#define __ptp_port_to_ptp(p) \
	container_of((p), struct ice_ptp, port)
#define ptp_port_to_pf(p) \
	container_of(__ptp_port_to_ptp((p)), struct ice_pf, ptp)

#define __ptp_info_to_ptp(i) \
	container_of((i), struct ice_ptp, info)
#define ptp_info_to_pf(i) \
	container_of(__ptp_info_to_ptp((i)), struct ice_pf, ptp)

#define PTP_SHARED_CLK_IDX_VALID	BIT(31)
#define ICE_PTP_TS_VALID		BIT(0)

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
#define E810_N_EXT_TS			3
#define E810_N_PER_OUT			4

#if IS_ENABLED(CONFIG_PTP_1588_CLOCK)
struct ice_pf;
int ice_ptp_set_ts_config(struct ice_pf *pf, struct ifreq *ifr);
int ice_ptp_get_ts_config(struct ice_pf *pf, struct ifreq *ifr);
int ice_get_ptp_clock_index(struct ice_pf *pf);

s8 ice_ptp_request_ts(struct ice_ptp_tx *tx, struct sk_buff *skb);
void ice_ptp_process_ts(struct ice_pf *pf);

void
ice_ptp_rx_hwtstamp(struct ice_ring *rx_ring,
		    union ice_32b_rx_flex_desc *rx_desc, struct sk_buff *skb);
void ice_ptp_init(struct ice_pf *pf);
void ice_ptp_release(struct ice_pf *pf);
#else /* IS_ENABLED(CONFIG_PTP_1588_CLOCK) */
static inline int ice_ptp_set_ts_config(struct ice_pf *pf, struct ifreq *ifr)
{
	return -EOPNOTSUPP;
}

static inline int ice_ptp_get_ts_config(struct ice_pf *pf, struct ifreq *ifr)
{
	return -EOPNOTSUPP;
}

static inline int ice_get_ptp_clock_index(struct ice_pf *pf)
{
	return -1;
}

static inline s8
ice_ptp_request_ts(struct ice_ptp_tx *tx, struct sk_buff *skb)
{
	return -1;
}

static inline void ice_ptp_process_ts(struct ice_pf *pf) { }
static inline void
ice_ptp_rx_hwtstamp(struct ice_ring *rx_ring,
		    union ice_32b_rx_flex_desc *rx_desc, struct sk_buff *skb) { }
static inline void ice_ptp_init(struct ice_pf *pf) { }
static inline void ice_ptp_release(struct ice_pf *pf) { }
#endif /* IS_ENABLED(CONFIG_PTP_1588_CLOCK) */
#endif /* _ICE_PTP_H_ */
