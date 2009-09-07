/*********************************************************************
 * Author: Cavium Networks
 *
 * Contact: support@caviumnetworks.com
 * This file is part of the OCTEON SDK
 *
 * Copyright (c) 2003-2007 Cavium Networks
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 *
 * This file is distributed in the hope that it will be useful, but
 * AS-IS and WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, TITLE, or
 * NONINFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this file; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 * or visit http://www.gnu.org/licenses/.
 *
 * This file may also be available under a different license from Cavium.
 * Contact Cavium Networks for more information
*********************************************************************/

int cvm_oct_xmit(struct sk_buff *skb, struct net_device *dev);
int cvm_oct_xmit_pow(struct sk_buff *skb, struct net_device *dev);
int cvm_oct_transmit_qos(struct net_device *dev, void *work_queue_entry,
			 int do_free, int qos);
void cvm_oct_tx_shutdown(struct net_device *dev);

/**
 * Free dead transmit skbs.
 *
 * @priv:		The driver data
 * @skb_to_free:	The number of SKBs to free (free none if negative).
 * @qos:		The queue to free from.
 * @take_lock:		If true, acquire the skb list lock.
 */
static inline void cvm_oct_free_tx_skbs(struct octeon_ethernet *priv,
					int skb_to_free,
					int qos, int take_lock)
{
	/* Free skbuffs not in use by the hardware.  */
	if (skb_to_free > 0) {
		if (take_lock)
			spin_lock(&priv->tx_free_list[qos].lock);
		while (skb_to_free > 0) {
			dev_kfree_skb(__skb_dequeue(&priv->tx_free_list[qos]));
			skb_to_free--;
		}
		if (take_lock)
			spin_unlock(&priv->tx_free_list[qos].lock);
	}
}
