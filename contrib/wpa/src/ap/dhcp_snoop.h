/*
 * DHCP snooping for Proxy ARP
 * Copyright (c) 2014, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef DHCP_SNOOP_H
#define DHCP_SNOOP_H

#ifdef CONFIG_PROXYARP

int dhcp_snoop_init(struct hostapd_data *hapd);
void dhcp_snoop_deinit(struct hostapd_data *hapd);

#else /* CONFIG_PROXYARP */

static inline int dhcp_snoop_init(struct hostapd_data *hapd)
{
	return 0;
}

static inline void dhcp_snoop_deinit(struct hostapd_data *hapd)
{
}

#endif /* CONFIG_PROXYARP */

#endif /* DHCP_SNOOP_H */
