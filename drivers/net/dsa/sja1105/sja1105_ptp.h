/* SPDX-License-Identifier: GPL-2.0
 * Copyright (c) 2019, Vladimir Oltean <olteanv@gmail.com>
 */
#ifndef _SJA1105_PTP_H
#define _SJA1105_PTP_H

#if IS_ENABLED(CONFIG_NET_DSA_SJA1105_PTP)

struct sja1105_ptp_data {
	struct ptp_clock_info caps;
	struct ptp_clock *clock;
	/* The cycle counter translates the PTP timestamps (based on
	 * a free-running counter) into a software time domain.
	 */
	struct cyclecounter tstamp_cc;
	struct timecounter tstamp_tc;
	struct delayed_work refresh_work;
	/* Serializes all operations on the cycle counter */
	struct mutex lock;
};

int sja1105_ptp_clock_register(struct dsa_switch *ds);

void sja1105_ptp_clock_unregister(struct dsa_switch *ds);

int sja1105et_ptp_cmd(const struct dsa_switch *ds, const void *data);

int sja1105pqrs_ptp_cmd(const struct dsa_switch *ds, const void *data);

int sja1105_get_ts_info(struct dsa_switch *ds, int port,
			struct ethtool_ts_info *ts);

void sja1105_ptp_txtstamp_skb(struct dsa_switch *ds, int slot,
			      struct sk_buff *clone);

int sja1105_ptp_reset(struct dsa_switch *ds);

bool sja1105_port_rxtstamp(struct dsa_switch *ds, int port,
			   struct sk_buff *skb, unsigned int type);

bool sja1105_port_txtstamp(struct dsa_switch *ds, int port,
			   struct sk_buff *skb, unsigned int type);

int sja1105_hwtstamp_get(struct dsa_switch *ds, int port, struct ifreq *ifr);

int sja1105_hwtstamp_set(struct dsa_switch *ds, int port, struct ifreq *ifr);

#else

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

static inline int sja1105_ptp_reset(struct dsa_switch *ds)
{
	return 0;
}

#define sja1105et_ptp_cmd NULL

#define sja1105pqrs_ptp_cmd NULL

#define sja1105_get_ts_info NULL

#define sja1105_port_rxtstamp NULL

#define sja1105_port_txtstamp NULL

#define sja1105_hwtstamp_get NULL

#define sja1105_hwtstamp_set NULL

#endif /* IS_ENABLED(CONFIG_NET_DSA_SJA1105_PTP) */

#endif /* _SJA1105_PTP_H */
