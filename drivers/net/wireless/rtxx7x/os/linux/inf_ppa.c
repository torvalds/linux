/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2010, Ralink Technology, Inc.
 *
 * This program is free software; you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation; either version 2 of the License, or     *
 * (at your option) any later version.                                   *
 *                                                                       *
 * This program is distributed in the hope that it will be useful,       *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 * GNU General Public License for more details.                          *
 *                                                                       *
 * You should have received a copy of the GNU General Public License     *
 * along with this program; if not, write to the                         *
 * Free Software Foundation, Inc.,                                       *
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 *                                                                       *
 *************************************************************************/


#ifdef INF_PPA_SUPPORT

#include "rt_config.h"
#include <linux/skbuff.h>
#include <linux/netdevice.h>

extern INT rt28xx_send_packets(
	IN struct sk_buff		*skb_p,
	IN struct net_device	*net_dev);

int ifx_ra_start_xmit(
	struct net_device *rx_dev,
	struct net_device *tx_dev,
	struct sk_buff *skb, int len)
{
	if(tx_dev != NULL)
	{
		SET_OS_PKT_NETDEV(skb, tx_dev);
		rt28xx_send_packets(skb, tx_dev);
	}
	else if(rx_dev != NULL)
	{
		skb->protocol = eth_type_trans(skb, skb->dev);
		netif_rx(skb);
	}
	else
	{
		dev_kfree_skb_any(skb);
	}
	return 0;
}
#endif /* INF_PPA_SUPPORT */
