/* SPDX-License-Identifier: GPL-2.0 */
/* Microchip KSZ PTP Implementation
 *
 * Copyright (C) 2020 ARRI Lighting
 * Copyright (C) 2022 Microchip Technology Inc.
 */

#ifndef _NET_DSA_DRIVERS_KSZ_PTP_H
#define _NET_DSA_DRIVERS_KSZ_PTP_H

#if IS_ENABLED(CONFIG_NET_DSA_MICROCHIP_KSZ_PTP)

#include <linux/ptp_clock_kernel.h>

struct ksz_ptp_data {
	struct ptp_clock_info caps;
	struct ptp_clock *clock;
	/* Serializes all operations on the PTP hardware clock */
	struct mutex lock;
	/* lock for accessing the clock_time */
	spinlock_t clock_lock;
	struct timespec64 clock_time;
};

int ksz_ptp_clock_register(struct dsa_switch *ds);

void ksz_ptp_clock_unregister(struct dsa_switch *ds);

int ksz_get_ts_info(struct dsa_switch *ds, int port,
		    struct ethtool_ts_info *ts);
int ksz_hwtstamp_get(struct dsa_switch *ds, int port, struct ifreq *ifr);
int ksz_hwtstamp_set(struct dsa_switch *ds, int port, struct ifreq *ifr);

#else

struct ksz_ptp_data {
	/* Serializes all operations on the PTP hardware clock */
	struct mutex lock;
};

static inline int ksz_ptp_clock_register(struct dsa_switch *ds)
{
	return 0;
}

static inline void ksz_ptp_clock_unregister(struct dsa_switch *ds) { }

#define ksz_get_ts_info NULL

#define ksz_hwtstamp_get NULL

#define ksz_hwtstamp_set NULL

#endif	/* End of CONFIG_NET_DSA_MICROCHIP_KSZ_PTP */

#endif
