/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019, Vladimir Oltean <olteanv@gmail.com>
 */
#ifndef _SJA1105_PTP_H
#define _SJA1105_PTP_H

#include <linux/timer.h>

#if IS_ENABLED(CONFIG_NET_DSA_SJA1105_PTP)

/* Timestamps are in units of 8 ns clock ticks (equivalent to
 * a fixed 125 MHz clock).
 */
#define SJA1105_TICK_NS			8

static inline s64 ns_to_sja1105_ticks(s64 ns)
{
	return ns / SJA1105_TICK_NS;
}

static inline s64 sja1105_ticks_to_ns(s64 ticks)
{
	return ticks * SJA1105_TICK_NS;
}

/* Calculate the first base_time in the future that satisfies this
 * relationship:
 *
 * future_base_time = base_time + N x cycle_time >= now, or
 *
 *      now - base_time
 * N >= ---------------
 *         cycle_time
 *
 * Because N is an integer, the ceiling value of the above "a / b" ratio
 * is in fact precisely the floor value of "(a + b - 1) / b", which is
 * easier to calculate only having integer division tools.
 */
static inline s64 future_base_time(s64 base_time, s64 cycle_time, s64 now)
{
	s64 a, b, n;

	if (base_time >= now)
		return base_time;

	a = now - base_time;
	b = cycle_time;
	n = div_s64(a + b - 1, b);

	return base_time + n * cycle_time;
}

/* This is not a preprocessor macro because the "ns" argument may or may not be
 * s64 at caller side. This ensures it is properly type-cast before div_s64.
 */
static inline s64 ns_to_sja1105_delta(s64 ns)
{
	return div_s64(ns, 200);
}

static inline s64 sja1105_delta_to_ns(s64 delta)
{
	return delta * 200;
}

struct sja1105_ptp_cmd {
	u64 startptpcp;		/* start toggling PTP_CLK pin */
	u64 stopptpcp;		/* stop toggling PTP_CLK pin */
	u64 ptpstrtsch;		/* start schedule */
	u64 ptpstopsch;		/* stop schedule */
	u64 resptp;		/* reset */
	u64 corrclk4ts;		/* use the corrected clock for timestamps */
	u64 ptpclkadd;		/* enum sja1105_ptp_clk_mode */
};

struct sja1105_ptp_data {
	struct timer_list extts_timer;
	/* Used only on SJA1105 to reconstruct partial timestamps */
	struct sk_buff_head skb_rxtstamp_queue;
	/* Used on SJA1110 where meta frames are generated only for
	 * 2-step TX timestamps
	 */
	struct sk_buff_head skb_txtstamp_queue;
	struct ptp_clock_info caps;
	struct ptp_clock *clock;
	struct sja1105_ptp_cmd cmd;
	/* Serializes all operations on the PTP hardware clock */
	struct mutex lock;
	bool extts_enabled;
	u64 ptpsyncts;
};

int sja1105_ptp_clock_register(struct dsa_switch *ds);

void sja1105_ptp_clock_unregister(struct dsa_switch *ds);

void sja1105et_ptp_cmd_packing(u8 *buf, struct sja1105_ptp_cmd *cmd,
			       enum packing_op op);

void sja1105pqrs_ptp_cmd_packing(u8 *buf, struct sja1105_ptp_cmd *cmd,
				 enum packing_op op);

int sja1105_get_ts_info(struct dsa_switch *ds, int port,
			struct ethtool_ts_info *ts);

void sja1105_ptp_txtstamp_skb(struct dsa_switch *ds, int slot,
			      struct sk_buff *clone);

bool sja1105_port_rxtstamp(struct dsa_switch *ds, int port,
			   struct sk_buff *skb, unsigned int type);

void sja1105_port_txtstamp(struct dsa_switch *ds, int port,
			   struct sk_buff *skb);

int sja1105_hwtstamp_get(struct dsa_switch *ds, int port, struct ifreq *ifr);

int sja1105_hwtstamp_set(struct dsa_switch *ds, int port, struct ifreq *ifr);

int __sja1105_ptp_gettimex(struct dsa_switch *ds, u64 *ns,
			   struct ptp_system_timestamp *sts);

int __sja1105_ptp_settime(struct dsa_switch *ds, u64 ns,
			  struct ptp_system_timestamp *ptp_sts);

int __sja1105_ptp_adjtime(struct dsa_switch *ds, s64 delta);

int sja1105_ptp_commit(struct dsa_switch *ds, struct sja1105_ptp_cmd *cmd,
		       sja1105_spi_rw_mode_t rw);

bool sja1105_rxtstamp(struct dsa_switch *ds, int port, struct sk_buff *skb);
bool sja1110_rxtstamp(struct dsa_switch *ds, int port, struct sk_buff *skb);
void sja1110_txtstamp(struct dsa_switch *ds, int port, struct sk_buff *skb);

void sja1110_process_meta_tstamp(struct dsa_switch *ds, int port, u8 ts_id,
				 enum sja1110_meta_tstamp dir, u64 tstamp);

#else

struct sja1105_ptp_cmd;

/* Structures cannot be empty in C. Bah!
 * Keep the mutex as the only element, which is a bit more difficult to
 * refactor out of sja1105_main.c anyway.
 */
struct sja1105_ptp_data {
	struct mutex lock;
};

static inline int sja1105_ptp_clock_register(struct dsa_switch *ds)
{
	return 0;
}

static inline void sja1105_ptp_clock_unregister(struct dsa_switch *ds) { }

static inline void sja1105_ptp_txtstamp_skb(struct dsa_switch *ds, int slot,
					    struct sk_buff *clone)
{
}

static inline int __sja1105_ptp_gettimex(struct dsa_switch *ds, u64 *ns,
					 struct ptp_system_timestamp *sts)
{
	return 0;
}

static inline int __sja1105_ptp_settime(struct dsa_switch *ds, u64 ns,
					struct ptp_system_timestamp *ptp_sts)
{
	return 0;
}

static inline int __sja1105_ptp_adjtime(struct dsa_switch *ds, s64 delta)
{
	return 0;
}

static inline int sja1105_ptp_commit(struct dsa_switch *ds,
				     struct sja1105_ptp_cmd *cmd,
				     sja1105_spi_rw_mode_t rw)
{
	return 0;
}

#define sja1105et_ptp_cmd_packing NULL

#define sja1105pqrs_ptp_cmd_packing NULL

#define sja1105_get_ts_info NULL

#define sja1105_port_rxtstamp NULL

#define sja1105_port_txtstamp NULL

#define sja1105_hwtstamp_get NULL

#define sja1105_hwtstamp_set NULL

#define sja1105_rxtstamp NULL
#define sja1110_rxtstamp NULL
#define sja1110_txtstamp NULL

#define sja1110_process_meta_tstamp NULL

#endif /* IS_ENABLED(CONFIG_NET_DSA_SJA1105_PTP) */

#endif /* _SJA1105_PTP_H */
