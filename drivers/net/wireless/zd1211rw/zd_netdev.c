/* zd_netdev.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <net/ieee80211.h>
#include <net/ieee80211softmac.h>
#include <net/ieee80211softmac_wx.h>
#include <net/iw_handler.h>

#include "zd_def.h"
#include "zd_netdev.h"
#include "zd_mac.h"
#include "zd_ieee80211.h"

/* Region 0 means reset regdomain to default. */
static int zd_set_regdomain(struct net_device *netdev,
	                    struct iw_request_info *info,
			    union iwreq_data *req, char *extra)
{
	const u8 *regdomain = (u8 *)req;
	return zd_mac_set_regdomain(zd_netdev_mac(netdev), *regdomain);
}

static int zd_get_regdomain(struct net_device *netdev,
	                    struct iw_request_info *info,
			    union iwreq_data *req, char *extra)
{
	u8 *regdomain = (u8 *)req;
	if (!regdomain)
		return -EINVAL;
	*regdomain = zd_mac_get_regdomain(zd_netdev_mac(netdev));
	return 0;
}

static const struct iw_priv_args zd_priv_args[] = {
	{
		.cmd = ZD_PRIV_SET_REGDOMAIN,
		.set_args = IW_PRIV_TYPE_BYTE | IW_PRIV_SIZE_FIXED | 1,
		.name = "set_regdomain",
	},
	{
		.cmd = ZD_PRIV_GET_REGDOMAIN,
		.get_args = IW_PRIV_TYPE_BYTE | IW_PRIV_SIZE_FIXED | 1,
		.name = "get_regdomain",
	},
};

#define PRIV_OFFSET(x) [(x)-SIOCIWFIRSTPRIV]

static const iw_handler zd_priv_handler[] = {
	PRIV_OFFSET(ZD_PRIV_SET_REGDOMAIN) = zd_set_regdomain,
	PRIV_OFFSET(ZD_PRIV_GET_REGDOMAIN) = zd_get_regdomain,
};

static int iw_get_name(struct net_device *netdev,
	               struct iw_request_info *info,
		       union iwreq_data *req, char *extra)
{
	/* FIXME: check whether 802.11a will also supported */
	strlcpy(req->name, "IEEE 802.11b/g", IFNAMSIZ);
	return 0;
}

static int iw_get_nick(struct net_device *netdev,
	               struct iw_request_info *info,
		       union iwreq_data *req, char *extra)
{
	strcpy(extra, "zd1211");
	req->data.length = strlen(extra);
	req->data.flags = 1;
	return 0;
}

static int iw_set_freq(struct net_device *netdev,
	               struct iw_request_info *info,
		       union iwreq_data *req, char *extra)
{
	int r;
	struct zd_mac *mac = zd_netdev_mac(netdev);
	struct iw_freq *freq = &req->freq;
	u8 channel;

	r = zd_find_channel(&channel, freq);
	if (r < 0)
		return r;
	r = zd_mac_request_channel(mac, channel);
	return r;
}

static int iw_get_freq(struct net_device *netdev,
	           struct iw_request_info *info,
		   union iwreq_data *req, char *extra)
{
	struct zd_mac *mac = zd_netdev_mac(netdev);
	struct iw_freq *freq = &req->freq;

	return zd_channel_to_freq(freq, zd_mac_get_channel(mac));
}

static int iw_set_mode(struct net_device *netdev,
	               struct iw_request_info *info,
		       union iwreq_data *req, char *extra)
{
	return zd_mac_set_mode(zd_netdev_mac(netdev), req->mode);
}

static int iw_get_mode(struct net_device *netdev,
	               struct iw_request_info *info,
		       union iwreq_data *req, char *extra)
{
	return zd_mac_get_mode(zd_netdev_mac(netdev), &req->mode);
}

static int iw_get_range(struct net_device *netdev,
	               struct iw_request_info *info,
		       union iwreq_data *req, char *extra)
{
	struct iw_range *range = (struct iw_range *)extra;

	dev_dbg_f(zd_mac_dev(zd_netdev_mac(netdev)), "\n");
	req->data.length = sizeof(*range);
	return zd_mac_get_range(zd_netdev_mac(netdev), range);
}

static int iw_set_encode(struct net_device *netdev,
			 struct iw_request_info *info,
			 union iwreq_data *data,
			 char *extra)
{
	return ieee80211_wx_set_encode(zd_netdev_ieee80211(netdev), info,
		data, extra);
}

static int iw_get_encode(struct net_device *netdev,
			 struct iw_request_info *info,
			 union iwreq_data *data,
			 char *extra)
{
	return ieee80211_wx_get_encode(zd_netdev_ieee80211(netdev), info,
		data, extra);
}

static int iw_set_encodeext(struct net_device *netdev,
			 struct iw_request_info *info,
			 union iwreq_data *data,
			 char *extra)
{
	return ieee80211_wx_set_encodeext(zd_netdev_ieee80211(netdev), info,
		data, extra);
}

static int iw_get_encodeext(struct net_device *netdev,
			 struct iw_request_info *info,
			 union iwreq_data *data,
			 char *extra)
{
	return ieee80211_wx_get_encodeext(zd_netdev_ieee80211(netdev), info,
		data, extra);
}

#define WX(x) [(x)-SIOCIWFIRST]

static const iw_handler zd_standard_iw_handlers[] = {
	WX(SIOCGIWNAME)		= iw_get_name,
	WX(SIOCGIWNICKN)	= iw_get_nick,
	WX(SIOCSIWFREQ)		= iw_set_freq,
	WX(SIOCGIWFREQ)		= iw_get_freq,
	WX(SIOCSIWMODE)		= iw_set_mode,
	WX(SIOCGIWMODE)		= iw_get_mode,
	WX(SIOCGIWRANGE)	= iw_get_range,
	WX(SIOCSIWENCODE)	= iw_set_encode,
	WX(SIOCGIWENCODE)	= iw_get_encode,
	WX(SIOCSIWENCODEEXT)	= iw_set_encodeext,
	WX(SIOCGIWENCODEEXT)	= iw_get_encodeext,
	WX(SIOCSIWAUTH)		= ieee80211_wx_set_auth,
	WX(SIOCGIWAUTH)		= ieee80211_wx_get_auth,
	WX(SIOCSIWSCAN)		= ieee80211softmac_wx_trigger_scan,
	WX(SIOCGIWSCAN)		= ieee80211softmac_wx_get_scan_results,
	WX(SIOCSIWESSID)	= ieee80211softmac_wx_set_essid,
	WX(SIOCGIWESSID)	= ieee80211softmac_wx_get_essid,
	WX(SIOCSIWAP)		= ieee80211softmac_wx_set_wap,
	WX(SIOCGIWAP)		= ieee80211softmac_wx_get_wap,
	WX(SIOCSIWRATE)		= ieee80211softmac_wx_set_rate,
	WX(SIOCGIWRATE)		= ieee80211softmac_wx_get_rate,
	WX(SIOCSIWGENIE)	= ieee80211softmac_wx_set_genie,
	WX(SIOCGIWGENIE)	= ieee80211softmac_wx_get_genie,
	WX(SIOCSIWMLME)		= ieee80211softmac_wx_set_mlme,
};

static const struct iw_handler_def iw_handler_def = {
	.standard		= zd_standard_iw_handlers,
	.num_standard		= ARRAY_SIZE(zd_standard_iw_handlers),
	.private		= zd_priv_handler,
	.num_private		= ARRAY_SIZE(zd_priv_handler),
	.private_args		= zd_priv_args,
	.num_private_args	= ARRAY_SIZE(zd_priv_args),
	.get_wireless_stats	= zd_mac_get_wireless_stats,
};

struct net_device *zd_netdev_alloc(struct usb_interface *intf)
{
	int r;
	struct net_device *netdev;
	struct zd_mac *mac;

	netdev = alloc_ieee80211softmac(sizeof(struct zd_mac));
	if (!netdev) {
		dev_dbg_f(&intf->dev, "out of memory\n");
		return NULL;
	}

	mac = zd_netdev_mac(netdev);
	r = zd_mac_init(mac, netdev, intf);
	if (r) {
		usb_set_intfdata(intf, NULL);
		free_ieee80211(netdev);
		return NULL;
	}

	SET_MODULE_OWNER(netdev);
	SET_NETDEV_DEV(netdev, &intf->dev);

	dev_dbg_f(&intf->dev, "netdev->flags %#06hx\n", netdev->flags);
	dev_dbg_f(&intf->dev, "netdev->features %#010lx\n", netdev->features);

	netdev->open = zd_mac_open;
	netdev->stop = zd_mac_stop;
	/* netdev->get_stats = */
	/* netdev->set_multicast_list = */
	netdev->set_mac_address = zd_mac_set_mac_address;
	netdev->wireless_handlers = &iw_handler_def;
	/* netdev->ethtool_ops = */

	return netdev;
}

void zd_netdev_free(struct net_device *netdev)
{
	if (!netdev)
		return;

	zd_mac_clear(zd_netdev_mac(netdev));
	free_ieee80211(netdev);
}

void zd_netdev_disconnect(struct net_device *netdev)
{
	unregister_netdev(netdev);
}
