/* zd_netdev.h: Header for net device related functions.
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

#ifndef _ZD_NETDEV_H
#define _ZD_NETDEV_H

#include <linux/usb.h>
#include <linux/netdevice.h>
#include <net/ieee80211.h>

#define ZD_PRIV_SET_REGDOMAIN (SIOCIWFIRSTPRIV)
#define ZD_PRIV_GET_REGDOMAIN (SIOCIWFIRSTPRIV+1)

static inline struct ieee80211_device *zd_netdev_ieee80211(
	struct net_device *ndev)
{
	return netdev_priv(ndev);
}

static inline struct net_device *zd_ieee80211_to_netdev(
	struct ieee80211_device *ieee)
{
	return ieee->dev;
}

struct net_device *zd_netdev_alloc(struct usb_interface *intf);
void zd_netdev_free(struct net_device *netdev);

void zd_netdev_disconnect(struct net_device *netdev);

#endif /* _ZD_NETDEV_H */
