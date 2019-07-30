/* SPDX-License-Identifier: GPL-2.0
 * Copyright (c) 2019, Vladimir Oltean <olteanv@gmail.com>
 */
#ifndef _SJA1105_PTP_H
#define _SJA1105_PTP_H

#if IS_ENABLED(CONFIG_NET_DSA_SJA1105_PTP)

int sja1105_ptp_clock_register(struct sja1105_private *priv);

void sja1105_ptp_clock_unregister(struct sja1105_private *priv);

int sja1105_ptpegr_ts_poll(struct sja1105_private *priv, int port, u64 *ts);

int sja1105et_ptp_cmd(const void *ctx, const void *data);

int sja1105pqrs_ptp_cmd(const void *ctx, const void *data);

int sja1105_get_ts_info(struct dsa_switch *ds, int port,
			struct ethtool_ts_info *ts);

u64 sja1105_tstamp_reconstruct(struct sja1105_private *priv, u64 now,
			       u64 ts_partial);

int sja1105_ptp_reset(struct sja1105_private *priv);

#else

static inline int sja1105_ptp_clock_register(struct sja1105_private *priv)
{
	return 0;
}

static inline void sja1105_ptp_clock_unregister(struct sja1105_private *priv)
{
	return;
}

static inline int
sja1105_ptpegr_ts_poll(struct sja1105_private *priv, int port, u64 *ts)
{
	return 0;
}

static inline u64 sja1105_tstamp_reconstruct(struct sja1105_private *priv,
					     u64 now, u64 ts_partial)
{
	return 0;
}

static inline int sja1105_ptp_reset(struct sja1105_private *priv)
{
	return 0;
}

#define sja1105et_ptp_cmd NULL

#define sja1105pqrs_ptp_cmd NULL

#define sja1105_get_ts_info NULL

#endif /* IS_ENABLED(CONFIG_NET_DSA_SJA1105_PTP) */

#endif /* _SJA1105_PTP_H */
