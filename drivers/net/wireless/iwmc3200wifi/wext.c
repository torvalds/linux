/*
 * Intel Wireless Multicomm 3200 WiFi driver
 *
 * Copyright (C) 2009 Intel Corporation <ilw@linux.intel.com>
 * Samuel Ortiz <samuel.ortiz@intel.com>
 * Zhu Yi <yi.zhu@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */

#include <linux/wireless.h>
#include <net/cfg80211.h>

#include "iwm.h"
#include "commands.h"

static int iwm_wext_siwessid(struct net_device *dev,
			     struct iw_request_info *info,
			     struct iw_point *data, char *ssid)
{
	struct iwm_priv *iwm = ndev_to_iwm(dev);

	switch (iwm->conf.mode) {
	case UMAC_MODE_IBSS:
		return cfg80211_ibss_wext_siwessid(dev, info, data, ssid);
	case UMAC_MODE_BSS:
		return cfg80211_mgd_wext_siwessid(dev, info, data, ssid);
	default:
		return -EOPNOTSUPP;
	}
}

static int iwm_wext_giwessid(struct net_device *dev,
			     struct iw_request_info *info,
			     struct iw_point *data, char *ssid)
{
	struct iwm_priv *iwm = ndev_to_iwm(dev);

	switch (iwm->conf.mode) {
	case UMAC_MODE_IBSS:
		return cfg80211_ibss_wext_giwessid(dev, info, data, ssid);
	case UMAC_MODE_BSS:
		return cfg80211_mgd_wext_giwessid(dev, info, data, ssid);
	default:
		return -EOPNOTSUPP;
	}
}

static const iw_handler iwm_handlers[] =
{
	(iw_handler) NULL,				/* SIOCSIWCOMMIT */
	(iw_handler) cfg80211_wext_giwname,		/* SIOCGIWNAME */
	(iw_handler) NULL,				/* SIOCSIWNWID */
	(iw_handler) NULL,				/* SIOCGIWNWID */
	(iw_handler) cfg80211_wext_siwfreq,		/* SIOCSIWFREQ */
	(iw_handler) cfg80211_wext_giwfreq,		/* SIOCGIWFREQ */
	(iw_handler) cfg80211_wext_siwmode,		/* SIOCSIWMODE */
	(iw_handler) cfg80211_wext_giwmode,		/* SIOCGIWMODE */
	(iw_handler) NULL,				/* SIOCSIWSENS */
	(iw_handler) NULL,				/* SIOCGIWSENS */
	(iw_handler) NULL /* not used */,		/* SIOCSIWRANGE */
	(iw_handler) cfg80211_wext_giwrange,		/* SIOCGIWRANGE */
	(iw_handler) NULL /* not used */,		/* SIOCSIWPRIV */
	(iw_handler) NULL /* kernel code */,		/* SIOCGIWPRIV */
	(iw_handler) NULL /* not used */,		/* SIOCSIWSTATS */
	(iw_handler) NULL /* kernel code */,		/* SIOCGIWSTATS */
	(iw_handler) NULL,				/* SIOCSIWSPY */
	(iw_handler) NULL,				/* SIOCGIWSPY */
	(iw_handler) NULL,				/* SIOCSIWTHRSPY */
	(iw_handler) NULL,				/* SIOCGIWTHRSPY */
	(iw_handler) cfg80211_wext_siwap,		/* SIOCSIWAP */
	(iw_handler) cfg80211_wext_giwap,		/* SIOCGIWAP */
	(iw_handler) NULL,			        /* SIOCSIWMLME */
	(iw_handler) NULL,				/* SIOCGIWAPLIST */
	(iw_handler) cfg80211_wext_siwscan,		/* SIOCSIWSCAN */
	(iw_handler) cfg80211_wext_giwscan,		/* SIOCGIWSCAN */
	(iw_handler) iwm_wext_siwessid,			/* SIOCSIWESSID */
	(iw_handler) iwm_wext_giwessid,			/* SIOCGIWESSID */
	(iw_handler) NULL,				/* SIOCSIWNICKN */
	(iw_handler) NULL,				/* SIOCGIWNICKN */
	(iw_handler) NULL,				/* -- hole -- */
	(iw_handler) NULL,				/* -- hole -- */
	(iw_handler) NULL,				/* SIOCSIWRATE */
	(iw_handler) cfg80211_wext_giwrate,		/* SIOCGIWRATE */
	(iw_handler) cfg80211_wext_siwrts,		/* SIOCSIWRTS */
	(iw_handler) cfg80211_wext_giwrts,		/* SIOCGIWRTS */
	(iw_handler) cfg80211_wext_siwfrag,	        /* SIOCSIWFRAG */
	(iw_handler) cfg80211_wext_giwfrag,		/* SIOCGIWFRAG */
	(iw_handler) cfg80211_wext_siwtxpower,		/* SIOCSIWTXPOW */
	(iw_handler) cfg80211_wext_giwtxpower,		/* SIOCGIWTXPOW */
	(iw_handler) NULL,				/* SIOCSIWRETRY */
	(iw_handler) NULL,				/* SIOCGIWRETRY */
	(iw_handler) cfg80211_wext_siwencode,		/* SIOCSIWENCODE */
	(iw_handler) cfg80211_wext_giwencode,		/* SIOCGIWENCODE */
	(iw_handler) cfg80211_wext_siwpower,		/* SIOCSIWPOWER */
	(iw_handler) cfg80211_wext_giwpower,		/* SIOCGIWPOWER */
	(iw_handler) NULL,				/* -- hole -- */
	(iw_handler) NULL,				/* -- hole -- */
	(iw_handler) cfg80211_wext_siwgenie,            /* SIOCSIWGENIE */
	(iw_handler) NULL,				/* SIOCGIWGENIE */
	(iw_handler) cfg80211_wext_siwauth,		/* SIOCSIWAUTH */
	(iw_handler) cfg80211_wext_giwauth,		/* SIOCGIWAUTH */
	(iw_handler) cfg80211_wext_siwencodeext,	/* SIOCSIWENCODEEXT */
	(iw_handler) NULL,				/* SIOCGIWENCODEEXT */
	(iw_handler) NULL,				/* SIOCSIWPMKSA */
	(iw_handler) NULL,				/* -- hole -- */
};

const struct iw_handler_def iwm_iw_handler_def = {
	.num_standard	= ARRAY_SIZE(iwm_handlers),
	.standard	= (iw_handler *) iwm_handlers,
	.get_wireless_stats = cfg80211_wireless_stats,
};

