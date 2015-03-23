/*
 * Driver interface list
 * Copyright (c) 2004-2005, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 */

#include "includes.h"


#ifdef CONFIG_DRIVER_WEXT
extern struct wpa_driver_ops wpa_driver_wext_ops; /* driver_wext.c */
#endif /* CONFIG_DRIVER_WEXT */
#ifdef CONFIG_DRIVER_NL80211
extern struct wpa_driver_ops wpa_driver_nl80211_ops; /* driver_nl80211.c */
#endif /* CONFIG_DRIVER_NL80211 */
#ifdef CONFIG_DRIVER_HOSTAP
extern struct wpa_driver_ops wpa_driver_hostap_ops; /* driver_hostap.c */
#endif /* CONFIG_DRIVER_HOSTAP */
#ifdef CONFIG_DRIVER_MADWIFI
extern struct wpa_driver_ops wpa_driver_madwifi_ops; /* driver_madwifi.c */
#endif /* CONFIG_DRIVER_MADWIFI */
#ifdef CONFIG_DRIVER_BROADCOM
extern struct wpa_driver_ops wpa_driver_broadcom_ops; /* driver_broadcom.c */
#endif /* CONFIG_DRIVER_BROADCOM */
#ifdef CONFIG_DRIVER_BSD
extern struct wpa_driver_ops wpa_driver_bsd_ops; /* driver_bsd.c */
#endif /* CONFIG_DRIVER_BSD */
#ifdef CONFIG_DRIVER_NDIS
extern struct wpa_driver_ops wpa_driver_ndis_ops; /* driver_ndis.c */
#endif /* CONFIG_DRIVER_NDIS */
#ifdef CONFIG_DRIVER_WIRED
extern struct wpa_driver_ops wpa_driver_wired_ops; /* driver_wired.c */
#endif /* CONFIG_DRIVER_WIRED */
#ifdef CONFIG_DRIVER_TEST
extern struct wpa_driver_ops wpa_driver_test_ops; /* driver_test.c */
#endif /* CONFIG_DRIVER_TEST */
#ifdef CONFIG_DRIVER_RALINK
extern struct wpa_driver_ops wpa_driver_ralink_ops; /* driver_ralink.c */
#endif /* CONFIG_DRIVER_RALINK */
#ifdef CONFIG_DRIVER_OSX
extern struct wpa_driver_ops wpa_driver_osx_ops; /* driver_osx.m */
#endif /* CONFIG_DRIVER_OSX */
#ifdef CONFIG_DRIVER_IPHONE
extern struct wpa_driver_ops wpa_driver_iphone_ops; /* driver_iphone.m */
#endif /* CONFIG_DRIVER_IPHONE */
#ifdef CONFIG_DRIVER_ROBOSWITCH
/* driver_roboswitch.c */
extern struct wpa_driver_ops wpa_driver_roboswitch_ops;
#endif /* CONFIG_DRIVER_ROBOSWITCH */
#ifdef CONFIG_DRIVER_ATHEROS
extern struct wpa_driver_ops wpa_driver_atheros_ops; /* driver_atheros.c */
#endif /* CONFIG_DRIVER_ATHEROS */
#ifdef CONFIG_DRIVER_NONE
extern struct wpa_driver_ops wpa_driver_none_ops; /* driver_none.c */
#endif /* CONFIG_DRIVER_NONE */
#ifdef CONFIG_DRIVER_RTW
extern struct wpa_driver_ops wpa_driver_rtw_ops; /* driver_rtw.c */
#endif /* CONFIG_DRIVER_RTW */


struct wpa_driver_ops *wpa_drivers[] =
{
#ifdef CONFIG_DRIVER_WEXT
	&wpa_driver_wext_ops,
#endif /* CONFIG_DRIVER_WEXT */
#ifdef CONFIG_DRIVER_NL80211
	&wpa_driver_nl80211_ops,
#endif /* CONFIG_DRIVER_NL80211 */
#ifdef CONFIG_DRIVER_HOSTAP
	&wpa_driver_hostap_ops,
#endif /* CONFIG_DRIVER_HOSTAP */
#ifdef CONFIG_DRIVER_MADWIFI
	&wpa_driver_madwifi_ops,
#endif /* CONFIG_DRIVER_MADWIFI */
#ifdef CONFIG_DRIVER_BROADCOM
	&wpa_driver_broadcom_ops,
#endif /* CONFIG_DRIVER_BROADCOM */
#ifdef CONFIG_DRIVER_BSD
	&wpa_driver_bsd_ops,
#endif /* CONFIG_DRIVER_BSD */
#ifdef CONFIG_DRIVER_NDIS
	&wpa_driver_ndis_ops,
#endif /* CONFIG_DRIVER_NDIS */
#ifdef CONFIG_DRIVER_WIRED
	&wpa_driver_wired_ops,
#endif /* CONFIG_DRIVER_WIRED */
#ifdef CONFIG_DRIVER_TEST
	&wpa_driver_test_ops,
#endif /* CONFIG_DRIVER_TEST */
#ifdef CONFIG_DRIVER_RALINK
	&wpa_driver_ralink_ops,
#endif /* CONFIG_DRIVER_RALINK */
#ifdef CONFIG_DRIVER_OSX
	&wpa_driver_osx_ops,
#endif /* CONFIG_DRIVER_OSX */
#ifdef CONFIG_DRIVER_IPHONE
	&wpa_driver_iphone_ops,
#endif /* CONFIG_DRIVER_IPHONE */
#ifdef CONFIG_DRIVER_ROBOSWITCH
	&wpa_driver_roboswitch_ops,
#endif /* CONFIG_DRIVER_ROBOSWITCH */
#ifdef CONFIG_DRIVER_ATHEROS
	&wpa_driver_atheros_ops,
#endif /* CONFIG_DRIVER_ATHEROS */
#ifdef CONFIG_DRIVER_NONE
	&wpa_driver_none_ops,
#endif /* CONFIG_DRIVER_NONE */
#ifdef CONFIG_DRIVER_RTW
	&wpa_driver_rtw_ops,
#endif /* CONFIG_DRIVER_RTW */
	NULL
};
