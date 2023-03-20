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

#define KSZ_PTP_N_GPIO		2

enum ksz_ptp_tou_mode {
	KSZ_PTP_TOU_IDLE,
	KSZ_PTP_TOU_PEROUT,
};

struct ksz_ptp_data {
	struct ptp_clock_info caps;
	struct ptp_clock *clock;
	struct ptp_pin_desc pin_config[KSZ_PTP_N_GPIO];
	/* Serializes all operations on the PTP hardware clock */
	struct mutex lock;
	/* lock for accessing the clock_time */
	spinlock_t clock_lock;
	struct timespec64 clock_time;
	enum ksz_ptp_tou_mode tou_mode;
	struct timespec64 perout_target_time_first;  /* start of first pulse */
	struct timespec64 perout_period;
};

int ksz_ptp_clock_register(struct dsa_switch *ds);

void ksz_ptp_clock_unregister(struct dsa_switch *ds);

int ksz_get_ts_info(struct dsa_switch *ds, int port,
		    struct ethtool_ts_info *ts);
int ksz_hwtstamp_get(struct dsa_switch *ds, int port, struct ifreq *ifr);
int ksz_hwtstamp_set(struct dsa_switch *ds, int port, struct ifreq *ifr);
void ksz_port_txtstamp(struct dsa_switch *ds, int port, struct sk_buff *skb);
void ksz_port_deferred_xmit(struct kthread_work *work);
bool ksz_port_rxtstamp(struct dsa_switch *ds, int port, struct sk_buff *skb,
		       unsigned int type);
int ksz_ptp_irq_setup(struct dsa_switch *ds, u8 p);
void ksz_ptp_irq_free(struct dsa_switch *ds, u8 p);

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

static inline int ksz_ptp_irq_setup(struct dsa_switch *ds, u8 p)
{
	return 0;
}

static inline void ksz_ptp_irq_free(struct dsa_switch *ds, u8 p) {}

#define ksz_get_ts_info NULL

#define ksz_hwtstamp_get NULL

#define ksz_hwtstamp_set NULL

#define ksz_port_rxtstamp NULL

#define ksz_port_txtstamp NULL

#define ksz_port_deferred_xmit NULL

#endif	/* End of CONFIG_NET_DSA_MICROCHIP_KSZ_PTP */

#endif
