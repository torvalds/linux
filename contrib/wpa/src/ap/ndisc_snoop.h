/*
 * Neighbor Discovery snooping for Proxy ARP
 * Copyright (c) 2014, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef NDISC_SNOOP_H
#define NDISC_SNOOP_H

#if defined(CONFIG_PROXYARP) && defined(CONFIG_IPV6)

int ndisc_snoop_init(struct hostapd_data *hapd);
void ndisc_snoop_deinit(struct hostapd_data *hapd);
void sta_ip6addr_del(struct hostapd_data *hapd, struct sta_info *sta);

#else /* CONFIG_PROXYARP && CONFIG_IPV6 */

static inline int ndisc_snoop_init(struct hostapd_data *hapd)
{
	return 0;
}

static inline void ndisc_snoop_deinit(struct hostapd_data *hapd)
{
}

static inline void sta_ip6addr_del(struct hostapd_data *hapd,
				   struct sta_info *sta)
{
}

#endif /* CONFIG_PROXYARP && CONFIG_IPV6 */

#endif /* NDISC_SNOOP_H */
