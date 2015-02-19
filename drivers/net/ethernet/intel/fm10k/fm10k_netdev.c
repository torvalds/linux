/* Intel Ethernet Switch Host Interface Driver
 * Copyright(c) 2013 - 2014 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 */

#include "fm10k.h"
#include <linux/vmalloc.h>
#if IS_ENABLED(CONFIG_FM10K_VXLAN)
#include <net/vxlan.h>
#endif /* CONFIG_FM10K_VXLAN */

/**
 * fm10k_setup_tx_resources - allocate Tx resources (Descriptors)
 * @tx_ring:    tx descriptor ring (for a specific queue) to setup
 *
 * Return 0 on success, negative on failure
 **/
int fm10k_setup_tx_resources(struct fm10k_ring *tx_ring)
{
	struct device *dev = tx_ring->dev;
	int size;

	size = sizeof(struct fm10k_tx_buffer) * tx_ring->count;

	tx_ring->tx_buffer = vzalloc(size);
	if (!tx_ring->tx_buffer)
		goto err;

	u64_stats_init(&tx_ring->syncp);

	/* round up to nearest 4K */
	tx_ring->size = tx_ring->count * sizeof(struct fm10k_tx_desc);
	tx_ring->size = ALIGN(tx_ring->size, 4096);

	tx_ring->desc = dma_alloc_coherent(dev, tx_ring->size,
					   &tx_ring->dma, GFP_KERNEL);
	if (!tx_ring->desc)
		goto err;

	return 0;

err:
	vfree(tx_ring->tx_buffer);
	tx_ring->tx_buffer = NULL;
	return -ENOMEM;
}

/**
 * fm10k_setup_all_tx_resources - allocate all queues Tx resources
 * @interface: board private structure
 *
 * If this function returns with an error, then it's possible one or
 * more of the rings is populated (while the rest are not).  It is the
 * callers duty to clean those orphaned rings.
 *
 * Return 0 on success, negative on failure
 **/
static int fm10k_setup_all_tx_resources(struct fm10k_intfc *interface)
{
	int i, err = 0;

	for (i = 0; i < interface->num_tx_queues; i++) {
		err = fm10k_setup_tx_resources(interface->tx_ring[i]);
		if (!err)
			continue;

		netif_err(interface, probe, interface->netdev,
			  "Allocation for Tx Queue %u failed\n", i);
		goto err_setup_tx;
	}

	return 0;
err_setup_tx:
	/* rewind the index freeing the rings as we go */
	while (i--)
		fm10k_free_tx_resources(interface->tx_ring[i]);
	return err;
}

/**
 * fm10k_setup_rx_resources - allocate Rx resources (Descriptors)
 * @rx_ring:    rx descriptor ring (for a specific queue) to setup
 *
 * Returns 0 on success, negative on failure
 **/
int fm10k_setup_rx_resources(struct fm10k_ring *rx_ring)
{
	struct device *dev = rx_ring->dev;
	int size;

	size = sizeof(struct fm10k_rx_buffer) * rx_ring->count;

	rx_ring->rx_buffer = vzalloc(size);
	if (!rx_ring->rx_buffer)
		goto err;

	u64_stats_init(&rx_ring->syncp);

	/* Round up to nearest 4K */
	rx_ring->size = rx_ring->count * sizeof(union fm10k_rx_desc);
	rx_ring->size = ALIGN(rx_ring->size, 4096);

	rx_ring->desc = dma_alloc_coherent(dev, rx_ring->size,
					   &rx_ring->dma, GFP_KERNEL);
	if (!rx_ring->desc)
		goto err;

	return 0;
err:
	vfree(rx_ring->rx_buffer);
	rx_ring->rx_buffer = NULL;
	return -ENOMEM;
}

/**
 * fm10k_setup_all_rx_resources - allocate all queues Rx resources
 * @interface: board private structure
 *
 * If this function returns with an error, then it's possible one or
 * more of the rings is populated (while the rest are not).  It is the
 * callers duty to clean those orphaned rings.
 *
 * Return 0 on success, negative on failure
 **/
static int fm10k_setup_all_rx_resources(struct fm10k_intfc *interface)
{
	int i, err = 0;

	for (i = 0; i < interface->num_rx_queues; i++) {
		err = fm10k_setup_rx_resources(interface->rx_ring[i]);
		if (!err)
			continue;

		netif_err(interface, probe, interface->netdev,
			  "Allocation for Rx Queue %u failed\n", i);
		goto err_setup_rx;
	}

	return 0;
err_setup_rx:
	/* rewind the index freeing the rings as we go */
	while (i--)
		fm10k_free_rx_resources(interface->rx_ring[i]);
	return err;
}

void fm10k_unmap_and_free_tx_resource(struct fm10k_ring *ring,
				      struct fm10k_tx_buffer *tx_buffer)
{
	if (tx_buffer->skb) {
		dev_kfree_skb_any(tx_buffer->skb);
		if (dma_unmap_len(tx_buffer, len))
			dma_unmap_single(ring->dev,
					 dma_unmap_addr(tx_buffer, dma),
					 dma_unmap_len(tx_buffer, len),
					 DMA_TO_DEVICE);
	} else if (dma_unmap_len(tx_buffer, len)) {
		dma_unmap_page(ring->dev,
			       dma_unmap_addr(tx_buffer, dma),
			       dma_unmap_len(tx_buffer, len),
			       DMA_TO_DEVICE);
	}
	tx_buffer->next_to_watch = NULL;
	tx_buffer->skb = NULL;
	dma_unmap_len_set(tx_buffer, len, 0);
	/* tx_buffer must be completely set up in the transmit path */
}

/**
 * fm10k_clean_tx_ring - Free Tx Buffers
 * @tx_ring: ring to be cleaned
 **/
static void fm10k_clean_tx_ring(struct fm10k_ring *tx_ring)
{
	struct fm10k_tx_buffer *tx_buffer;
	unsigned long size;
	u16 i;

	/* ring already cleared, nothing to do */
	if (!tx_ring->tx_buffer)
		return;

	/* Free all the Tx ring sk_buffs */
	for (i = 0; i < tx_ring->count; i++) {
		tx_buffer = &tx_ring->tx_buffer[i];
		fm10k_unmap_and_free_tx_resource(tx_ring, tx_buffer);
	}

	/* reset BQL values */
	netdev_tx_reset_queue(txring_txq(tx_ring));

	size = sizeof(struct fm10k_tx_buffer) * tx_ring->count;
	memset(tx_ring->tx_buffer, 0, size);

	/* Zero out the descriptor ring */
	memset(tx_ring->desc, 0, tx_ring->size);
}

/**
 * fm10k_free_tx_resources - Free Tx Resources per Queue
 * @tx_ring: Tx descriptor ring for a specific queue
 *
 * Free all transmit software resources
 **/
void fm10k_free_tx_resources(struct fm10k_ring *tx_ring)
{
	fm10k_clean_tx_ring(tx_ring);

	vfree(tx_ring->tx_buffer);
	tx_ring->tx_buffer = NULL;

	/* if not set, then don't free */
	if (!tx_ring->desc)
		return;

	dma_free_coherent(tx_ring->dev, tx_ring->size,
			  tx_ring->desc, tx_ring->dma);
	tx_ring->desc = NULL;
}

/**
 * fm10k_clean_all_tx_rings - Free Tx Buffers for all queues
 * @interface: board private structure
 **/
void fm10k_clean_all_tx_rings(struct fm10k_intfc *interface)
{
	int i;

	for (i = 0; i < interface->num_tx_queues; i++)
		fm10k_clean_tx_ring(interface->tx_ring[i]);

	/* remove any stale timestamp buffers and free them */
	skb_queue_purge(&interface->ts_tx_skb_queue);
}

/**
 * fm10k_free_all_tx_resources - Free Tx Resources for All Queues
 * @interface: board private structure
 *
 * Free all transmit software resources
 **/
static void fm10k_free_all_tx_resources(struct fm10k_intfc *interface)
{
	int i = interface->num_tx_queues;

	while (i--)
		fm10k_free_tx_resources(interface->tx_ring[i]);
}

/**
 * fm10k_clean_rx_ring - Free Rx Buffers per Queue
 * @rx_ring: ring to free buffers from
 **/
static void fm10k_clean_rx_ring(struct fm10k_ring *rx_ring)
{
	unsigned long size;
	u16 i;

	if (!rx_ring->rx_buffer)
		return;

	if (rx_ring->skb)
		dev_kfree_skb(rx_ring->skb);
	rx_ring->skb = NULL;

	/* Free all the Rx ring sk_buffs */
	for (i = 0; i < rx_ring->count; i++) {
		struct fm10k_rx_buffer *buffer = &rx_ring->rx_buffer[i];
		/* clean-up will only set page pointer to NULL */
		if (!buffer->page)
			continue;

		dma_unmap_page(rx_ring->dev, buffer->dma,
			       PAGE_SIZE, DMA_FROM_DEVICE);
		__free_page(buffer->page);

		buffer->page = NULL;
	}

	size = sizeof(struct fm10k_rx_buffer) * rx_ring->count;
	memset(rx_ring->rx_buffer, 0, size);

	/* Zero out the descriptor ring */
	memset(rx_ring->desc, 0, rx_ring->size);

	rx_ring->next_to_alloc = 0;
	rx_ring->next_to_clean = 0;
	rx_ring->next_to_use = 0;
}

/**
 * fm10k_free_rx_resources - Free Rx Resources
 * @rx_ring: ring to clean the resources from
 *
 * Free all receive software resources
 **/
void fm10k_free_rx_resources(struct fm10k_ring *rx_ring)
{
	fm10k_clean_rx_ring(rx_ring);

	vfree(rx_ring->rx_buffer);
	rx_ring->rx_buffer = NULL;

	/* if not set, then don't free */
	if (!rx_ring->desc)
		return;

	dma_free_coherent(rx_ring->dev, rx_ring->size,
			  rx_ring->desc, rx_ring->dma);

	rx_ring->desc = NULL;
}

/**
 * fm10k_clean_all_rx_rings - Free Rx Buffers for all queues
 * @interface: board private structure
 **/
void fm10k_clean_all_rx_rings(struct fm10k_intfc *interface)
{
	int i;

	for (i = 0; i < interface->num_rx_queues; i++)
		fm10k_clean_rx_ring(interface->rx_ring[i]);
}

/**
 * fm10k_free_all_rx_resources - Free Rx Resources for All Queues
 * @interface: board private structure
 *
 * Free all receive software resources
 **/
static void fm10k_free_all_rx_resources(struct fm10k_intfc *interface)
{
	int i = interface->num_rx_queues;

	while (i--)
		fm10k_free_rx_resources(interface->rx_ring[i]);
}

/**
 * fm10k_request_glort_range - Request GLORTs for use in configuring rules
 * @interface: board private structure
 *
 * This function allocates a range of glorts for this inteface to use.
 **/
static void fm10k_request_glort_range(struct fm10k_intfc *interface)
{
	struct fm10k_hw *hw = &interface->hw;
	u16 mask = (~hw->mac.dglort_map) >> FM10K_DGLORTMAP_MASK_SHIFT;

	/* establish GLORT base */
	interface->glort = hw->mac.dglort_map & FM10K_DGLORTMAP_NONE;
	interface->glort_count = 0;

	/* nothing we can do until mask is allocated */
	if (hw->mac.dglort_map == FM10K_DGLORTMAP_NONE)
		return;

	/* we support 3 possible GLORT configurations.
	 * 1: VFs consume all but the last 1
	 * 2: VFs and PF split glorts with possible gap between
	 * 3: VFs allocated first 64, all others belong to PF
	 */
	if (mask <= hw->iov.total_vfs) {
		interface->glort_count = 1;
		interface->glort += mask;
	} else if (mask < 64) {
		interface->glort_count = (mask + 1) / 2;
		interface->glort += interface->glort_count;
	} else {
		interface->glort_count = mask - 63;
		interface->glort += 64;
	}
}

/**
 * fm10k_del_vxlan_port_all
 * @interface: board private structure
 *
 * This function frees the entire vxlan_port list
 **/
static void fm10k_del_vxlan_port_all(struct fm10k_intfc *interface)
{
	struct fm10k_vxlan_port *vxlan_port;

	/* flush all entries from list */
	vxlan_port = list_first_entry_or_null(&interface->vxlan_port,
					      struct fm10k_vxlan_port, list);
	while (vxlan_port) {
		list_del(&vxlan_port->list);
		kfree(vxlan_port);
		vxlan_port = list_first_entry_or_null(&interface->vxlan_port,
						      struct fm10k_vxlan_port,
						      list);
	}
}

/**
 * fm10k_restore_vxlan_port
 * @interface: board private structure
 *
 * This function restores the value in the tunnel_cfg register after reset
 **/
static void fm10k_restore_vxlan_port(struct fm10k_intfc *interface)
{
	struct fm10k_hw *hw = &interface->hw;
	struct fm10k_vxlan_port *vxlan_port;

	/* only the PF supports configuring tunnels */
	if (hw->mac.type != fm10k_mac_pf)
		return;

	vxlan_port = list_first_entry_or_null(&interface->vxlan_port,
					      struct fm10k_vxlan_port, list);

	/* restore tunnel configuration register */
	fm10k_write_reg(hw, FM10K_TUNNEL_CFG,
			(vxlan_port ? ntohs(vxlan_port->port) : 0) |
			(ETH_P_TEB << FM10K_TUNNEL_CFG_NVGRE_SHIFT));
}

/**
 * fm10k_add_vxlan_port
 * @netdev: network interface device structure
 * @sa_family: Address family of new port
 * @port: port number used for VXLAN
 *
 * This funciton is called when a new VXLAN interface has added a new port
 * number to the range that is currently in use for VXLAN.  The new port
 * number is always added to the tail so that the port number list should
 * match the order in which the ports were allocated.  The head of the list
 * is always used as the VXLAN port number for offloads.
 **/
static void fm10k_add_vxlan_port(struct net_device *dev,
				 sa_family_t sa_family, __be16 port) {
	struct fm10k_intfc *interface = netdev_priv(dev);
	struct fm10k_vxlan_port *vxlan_port;

	/* only the PF supports configuring tunnels */
	if (interface->hw.mac.type != fm10k_mac_pf)
		return;

	/* existing ports are pulled out so our new entry is always last */
	fm10k_vxlan_port_for_each(vxlan_port, interface) {
		if ((vxlan_port->port == port) &&
		    (vxlan_port->sa_family == sa_family)) {
			list_del(&vxlan_port->list);
			goto insert_tail;
		}
	}

	/* allocate memory to track ports */
	vxlan_port = kmalloc(sizeof(*vxlan_port), GFP_ATOMIC);
	if (!vxlan_port)
		return;
	vxlan_port->port = port;
	vxlan_port->sa_family = sa_family;

insert_tail:
	/* add new port value to list */
	list_add_tail(&vxlan_port->list, &interface->vxlan_port);

	fm10k_restore_vxlan_port(interface);
}

/**
 * fm10k_del_vxlan_port
 * @netdev: network interface device structure
 * @sa_family: Address family of freed port
 * @port: port number used for VXLAN
 *
 * This funciton is called when a new VXLAN interface has freed a port
 * number from the range that is currently in use for VXLAN.  The freed
 * port is removed from the list and the new head is used to determine
 * the port number for offloads.
 **/
static void fm10k_del_vxlan_port(struct net_device *dev,
				 sa_family_t sa_family, __be16 port) {
	struct fm10k_intfc *interface = netdev_priv(dev);
	struct fm10k_vxlan_port *vxlan_port;

	if (interface->hw.mac.type != fm10k_mac_pf)
		return;

	/* find the port in the list and free it */
	fm10k_vxlan_port_for_each(vxlan_port, interface) {
		if ((vxlan_port->port == port) &&
		    (vxlan_port->sa_family == sa_family)) {
			list_del(&vxlan_port->list);
			kfree(vxlan_port);
			break;
		}
	}

	fm10k_restore_vxlan_port(interface);
}

/**
 * fm10k_open - Called when a network interface is made active
 * @netdev: network interface device structure
 *
 * Returns 0 on success, negative value on failure
 *
 * The open entry point is called when a network interface is made
 * active by the system (IFF_UP).  At this point all resources needed
 * for transmit and receive operations are allocated, the interrupt
 * handler is registered with the OS, the watchdog timer is started,
 * and the stack is notified that the interface is ready.
 **/
int fm10k_open(struct net_device *netdev)
{
	struct fm10k_intfc *interface = netdev_priv(netdev);
	int err;

	/* allocate transmit descriptors */
	err = fm10k_setup_all_tx_resources(interface);
	if (err)
		goto err_setup_tx;

	/* allocate receive descriptors */
	err = fm10k_setup_all_rx_resources(interface);
	if (err)
		goto err_setup_rx;

	/* allocate interrupt resources */
	err = fm10k_qv_request_irq(interface);
	if (err)
		goto err_req_irq;

	/* setup GLORT assignment for this port */
	fm10k_request_glort_range(interface);

	/* Notify the stack of the actual queue counts */
	err = netif_set_real_num_tx_queues(netdev,
					   interface->num_tx_queues);
	if (err)
		goto err_set_queues;

	err = netif_set_real_num_rx_queues(netdev,
					   interface->num_rx_queues);
	if (err)
		goto err_set_queues;

#if IS_ENABLED(CONFIG_FM10K_VXLAN)
	/* update VXLAN port configuration */
	vxlan_get_rx_port(netdev);

#endif
	fm10k_up(interface);

	return 0;

err_set_queues:
	fm10k_qv_free_irq(interface);
err_req_irq:
	fm10k_free_all_rx_resources(interface);
err_setup_rx:
	fm10k_free_all_tx_resources(interface);
err_setup_tx:
	return err;
}

/**
 * fm10k_close - Disables a network interface
 * @netdev: network interface device structure
 *
 * Returns 0, this is not allowed to fail
 *
 * The close entry point is called when an interface is de-activated
 * by the OS.  The hardware is still under the drivers control, but
 * needs to be disabled.  A global MAC reset is issued to stop the
 * hardware, and all transmit and receive resources are freed.
 **/
int fm10k_close(struct net_device *netdev)
{
	struct fm10k_intfc *interface = netdev_priv(netdev);

	fm10k_down(interface);

	fm10k_qv_free_irq(interface);

	fm10k_del_vxlan_port_all(interface);

	fm10k_free_all_tx_resources(interface);
	fm10k_free_all_rx_resources(interface);

	return 0;
}

static netdev_tx_t fm10k_xmit_frame(struct sk_buff *skb, struct net_device *dev)
{
	struct fm10k_intfc *interface = netdev_priv(dev);
	unsigned int r_idx = skb->queue_mapping;
	int err;

	if ((skb->protocol ==  htons(ETH_P_8021Q)) &&
	    !skb_vlan_tag_present(skb)) {
		/* FM10K only supports hardware tagging, any tags in frame
		 * are considered 2nd level or "outer" tags
		 */
		struct vlan_hdr *vhdr;
		__be16 proto;

		/* make sure skb is not shared */
		skb = skb_share_check(skb, GFP_ATOMIC);
		if (!skb)
			return NETDEV_TX_OK;

		/* make sure there is enough room to move the ethernet header */
		if (unlikely(!pskb_may_pull(skb, VLAN_ETH_HLEN)))
			return NETDEV_TX_OK;

		/* verify the skb head is not shared */
		err = skb_cow_head(skb, 0);
		if (err)
			return NETDEV_TX_OK;

		/* locate vlan header */
		vhdr = (struct vlan_hdr *)(skb->data + ETH_HLEN);

		/* pull the 2 key pieces of data out of it */
		__vlan_hwaccel_put_tag(skb,
				       htons(ETH_P_8021Q),
				       ntohs(vhdr->h_vlan_TCI));
		proto = vhdr->h_vlan_encapsulated_proto;
		skb->protocol = (ntohs(proto) >= 1536) ? proto :
							 htons(ETH_P_802_2);

		/* squash it by moving the ethernet addresses up 4 bytes */
		memmove(skb->data + VLAN_HLEN, skb->data, 12);
		__skb_pull(skb, VLAN_HLEN);
		skb_reset_mac_header(skb);
	}

	/* The minimum packet size for a single buffer is 17B so pad the skb
	 * in order to meet this minimum size requirement.
	 */
	if (unlikely(skb->len < 17)) {
		int pad_len = 17 - skb->len;

		if (skb_pad(skb, pad_len))
			return NETDEV_TX_OK;
		__skb_put(skb, pad_len);
	}

	/* prepare packet for hardware time stamping */
	if (unlikely(skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP))
		fm10k_ts_tx_enqueue(interface, skb);

	if (r_idx >= interface->num_tx_queues)
		r_idx %= interface->num_tx_queues;

	err = fm10k_xmit_frame_ring(skb, interface->tx_ring[r_idx]);

	return err;
}

static int fm10k_change_mtu(struct net_device *dev, int new_mtu)
{
	if (new_mtu < 68 || new_mtu > FM10K_MAX_JUMBO_FRAME_SIZE)
		return -EINVAL;

	dev->mtu = new_mtu;

	return 0;
}

/**
 * fm10k_tx_timeout - Respond to a Tx Hang
 * @netdev: network interface device structure
 **/
static void fm10k_tx_timeout(struct net_device *netdev)
{
	struct fm10k_intfc *interface = netdev_priv(netdev);
	bool real_tx_hang = false;
	int i;

#define TX_TIMEO_LIMIT 16000
	for (i = 0; i < interface->num_tx_queues; i++) {
		struct fm10k_ring *tx_ring = interface->tx_ring[i];

		if (check_for_tx_hang(tx_ring) && fm10k_check_tx_hang(tx_ring))
			real_tx_hang = true;
	}

	if (real_tx_hang) {
		fm10k_tx_timeout_reset(interface);
	} else {
		netif_info(interface, drv, netdev,
			   "Fake Tx hang detected with timeout of %d seconds\n",
			   netdev->watchdog_timeo/HZ);

		/* fake Tx hang - increase the kernel timeout */
		if (netdev->watchdog_timeo < TX_TIMEO_LIMIT)
			netdev->watchdog_timeo *= 2;
	}
}

static int fm10k_uc_vlan_unsync(struct net_device *netdev,
				const unsigned char *uc_addr)
{
	struct fm10k_intfc *interface = netdev_priv(netdev);
	struct fm10k_hw *hw = &interface->hw;
	u16 glort = interface->glort;
	u16 vid = interface->vid;
	bool set = !!(vid / VLAN_N_VID);
	int err;

	/* drop any leading bits on the VLAN ID */
	vid &= VLAN_N_VID - 1;

	err = hw->mac.ops.update_uc_addr(hw, glort, uc_addr, vid, set, 0);
	if (err)
		return err;

	/* return non-zero value as we are only doing a partial sync/unsync */
	return 1;
}

static int fm10k_mc_vlan_unsync(struct net_device *netdev,
				const unsigned char *mc_addr)
{
	struct fm10k_intfc *interface = netdev_priv(netdev);
	struct fm10k_hw *hw = &interface->hw;
	u16 glort = interface->glort;
	u16 vid = interface->vid;
	bool set = !!(vid / VLAN_N_VID);
	int err;

	/* drop any leading bits on the VLAN ID */
	vid &= VLAN_N_VID - 1;

	err = hw->mac.ops.update_mc_addr(hw, glort, mc_addr, vid, set);
	if (err)
		return err;

	/* return non-zero value as we are only doing a partial sync/unsync */
	return 1;
}

static int fm10k_update_vid(struct net_device *netdev, u16 vid, bool set)
{
	struct fm10k_intfc *interface = netdev_priv(netdev);
	struct fm10k_hw *hw = &interface->hw;
	s32 err;

	/* updates do not apply to VLAN 0 */
	if (!vid)
		return 0;

	if (vid >= VLAN_N_VID)
		return -EINVAL;

	/* Verify we have permission to add VLANs */
	if (hw->mac.vlan_override)
		return -EACCES;

	/* if default VLAN is already present do nothing */
	if (vid == hw->mac.default_vid)
		return -EBUSY;

	/* update active_vlans bitmask */
	set_bit(vid, interface->active_vlans);
	if (!set)
		clear_bit(vid, interface->active_vlans);

	fm10k_mbx_lock(interface);

	/* only need to update the VLAN if not in promiscous mode */
	if (!(netdev->flags & IFF_PROMISC)) {
		err = hw->mac.ops.update_vlan(hw, vid, 0, set);
		if (err)
			goto err_out;
	}

	/* update our base MAC address */
	err = hw->mac.ops.update_uc_addr(hw, interface->glort, hw->mac.addr,
					 vid, set, 0);
	if (err)
		goto err_out;

	/* set vid prior to syncing/unsyncing the VLAN */
	interface->vid = vid + (set ? VLAN_N_VID : 0);

	/* Update the unicast and multicast address list to add/drop VLAN */
	__dev_uc_unsync(netdev, fm10k_uc_vlan_unsync);
	__dev_mc_unsync(netdev, fm10k_mc_vlan_unsync);

err_out:
	fm10k_mbx_unlock(interface);

	return err;
}

static int fm10k_vlan_rx_add_vid(struct net_device *netdev,
				 __always_unused __be16 proto, u16 vid)
{
	/* update VLAN and address table based on changes */
	return fm10k_update_vid(netdev, vid, true);
}

static int fm10k_vlan_rx_kill_vid(struct net_device *netdev,
				  __always_unused __be16 proto, u16 vid)
{
	/* update VLAN and address table based on changes */
	return fm10k_update_vid(netdev, vid, false);
}

static u16 fm10k_find_next_vlan(struct fm10k_intfc *interface, u16 vid)
{
	struct fm10k_hw *hw = &interface->hw;
	u16 default_vid = hw->mac.default_vid;
	u16 vid_limit = vid < default_vid ? default_vid : VLAN_N_VID;

	vid = find_next_bit(interface->active_vlans, vid_limit, ++vid);

	return vid;
}

static void fm10k_clear_unused_vlans(struct fm10k_intfc *interface)
{
	struct fm10k_hw *hw = &interface->hw;
	u32 vid, prev_vid;

	/* loop through and find any gaps in the table */
	for (vid = 0, prev_vid = 0;
	     prev_vid < VLAN_N_VID;
	     prev_vid = vid + 1, vid = fm10k_find_next_vlan(interface, vid)) {
		if (prev_vid == vid)
			continue;

		/* send request to clear multiple bits at a time */
		prev_vid += (vid - prev_vid - 1) << FM10K_VLAN_LENGTH_SHIFT;
		hw->mac.ops.update_vlan(hw, prev_vid, 0, false);
	}
}

static int __fm10k_uc_sync(struct net_device *dev,
			   const unsigned char *addr, bool sync)
{
	struct fm10k_intfc *interface = netdev_priv(dev);
	struct fm10k_hw *hw = &interface->hw;
	u16 vid, glort = interface->glort;
	s32 err;

	if (!is_valid_ether_addr(addr))
		return -EADDRNOTAVAIL;

	/* update table with current entries */
	for (vid = hw->mac.default_vid ? fm10k_find_next_vlan(interface, 0) : 0;
	     vid < VLAN_N_VID;
	     vid = fm10k_find_next_vlan(interface, vid)) {
		err = hw->mac.ops.update_uc_addr(hw, glort, addr,
						  vid, sync, 0);
		if (err)
			return err;
	}

	return 0;
}

static int fm10k_uc_sync(struct net_device *dev,
			 const unsigned char *addr)
{
	return __fm10k_uc_sync(dev, addr, true);
}

static int fm10k_uc_unsync(struct net_device *dev,
			   const unsigned char *addr)
{
	return __fm10k_uc_sync(dev, addr, false);
}

static int fm10k_set_mac(struct net_device *dev, void *p)
{
	struct fm10k_intfc *interface = netdev_priv(dev);
	struct fm10k_hw *hw = &interface->hw;
	struct sockaddr *addr = p;
	s32 err = 0;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	if (dev->flags & IFF_UP) {
		/* setting MAC address requires mailbox */
		fm10k_mbx_lock(interface);

		err = fm10k_uc_sync(dev, addr->sa_data);
		if (!err)
			fm10k_uc_unsync(dev, hw->mac.addr);

		fm10k_mbx_unlock(interface);
	}

	if (!err) {
		ether_addr_copy(dev->dev_addr, addr->sa_data);
		ether_addr_copy(hw->mac.addr, addr->sa_data);
		dev->addr_assign_type &= ~NET_ADDR_RANDOM;
	}

	/* if we had a mailbox error suggest trying again */
	return err ? -EAGAIN : 0;
}

static int __fm10k_mc_sync(struct net_device *dev,
			   const unsigned char *addr, bool sync)
{
	struct fm10k_intfc *interface = netdev_priv(dev);
	struct fm10k_hw *hw = &interface->hw;
	u16 vid, glort = interface->glort;
	s32 err;

	if (!is_multicast_ether_addr(addr))
		return -EADDRNOTAVAIL;

	/* update table with current entries */
	for (vid = hw->mac.default_vid ? fm10k_find_next_vlan(interface, 0) : 0;
	     vid < VLAN_N_VID;
	     vid = fm10k_find_next_vlan(interface, vid)) {
		err = hw->mac.ops.update_mc_addr(hw, glort, addr, vid, sync);
		if (err)
			return err;
	}

	return 0;
}

static int fm10k_mc_sync(struct net_device *dev,
			 const unsigned char *addr)
{
	return __fm10k_mc_sync(dev, addr, true);
}

static int fm10k_mc_unsync(struct net_device *dev,
			   const unsigned char *addr)
{
	return __fm10k_mc_sync(dev, addr, false);
}

static void fm10k_set_rx_mode(struct net_device *dev)
{
	struct fm10k_intfc *interface = netdev_priv(dev);
	struct fm10k_hw *hw = &interface->hw;
	int xcast_mode;

	/* no need to update the harwdare if we are not running */
	if (!(dev->flags & IFF_UP))
		return;

	/* determine new mode based on flags */
	xcast_mode = (dev->flags & IFF_PROMISC) ? FM10K_XCAST_MODE_PROMISC :
		     (dev->flags & IFF_ALLMULTI) ? FM10K_XCAST_MODE_ALLMULTI :
		     (dev->flags & (IFF_BROADCAST | IFF_MULTICAST)) ?
		     FM10K_XCAST_MODE_MULTI : FM10K_XCAST_MODE_NONE;

	fm10k_mbx_lock(interface);

	/* syncronize all of the addresses */
	if (xcast_mode != FM10K_XCAST_MODE_PROMISC) {
		__dev_uc_sync(dev, fm10k_uc_sync, fm10k_uc_unsync);
		if (xcast_mode != FM10K_XCAST_MODE_ALLMULTI)
			__dev_mc_sync(dev, fm10k_mc_sync, fm10k_mc_unsync);
	}

	/* if we aren't changing modes there is nothing to do */
	if (interface->xcast_mode != xcast_mode) {
		/* update VLAN table */
		if (xcast_mode == FM10K_XCAST_MODE_PROMISC)
			hw->mac.ops.update_vlan(hw, FM10K_VLAN_ALL, 0, true);
		if (interface->xcast_mode == FM10K_XCAST_MODE_PROMISC)
			fm10k_clear_unused_vlans(interface);

		/* update xcast mode */
		hw->mac.ops.update_xcast_mode(hw, interface->glort, xcast_mode);

		/* record updated xcast mode state */
		interface->xcast_mode = xcast_mode;
	}

	fm10k_mbx_unlock(interface);
}

void fm10k_restore_rx_state(struct fm10k_intfc *interface)
{
	struct net_device *netdev = interface->netdev;
	struct fm10k_hw *hw = &interface->hw;
	int xcast_mode;
	u16 vid, glort;

	/* restore our address if perm_addr is set */
	if (hw->mac.type == fm10k_mac_vf) {
		if (is_valid_ether_addr(hw->mac.perm_addr)) {
			ether_addr_copy(hw->mac.addr, hw->mac.perm_addr);
			ether_addr_copy(netdev->perm_addr, hw->mac.perm_addr);
			ether_addr_copy(netdev->dev_addr, hw->mac.perm_addr);
			netdev->addr_assign_type &= ~NET_ADDR_RANDOM;
		}

		if (hw->mac.vlan_override)
			netdev->features &= ~NETIF_F_HW_VLAN_CTAG_RX;
		else
			netdev->features |= NETIF_F_HW_VLAN_CTAG_RX;
	}

	/* record glort for this interface */
	glort = interface->glort;

	/* convert interface flags to xcast mode */
	if (netdev->flags & IFF_PROMISC)
		xcast_mode = FM10K_XCAST_MODE_PROMISC;
	else if (netdev->flags & IFF_ALLMULTI)
		xcast_mode = FM10K_XCAST_MODE_ALLMULTI;
	else if (netdev->flags & (IFF_BROADCAST | IFF_MULTICAST))
		xcast_mode = FM10K_XCAST_MODE_MULTI;
	else
		xcast_mode = FM10K_XCAST_MODE_NONE;

	fm10k_mbx_lock(interface);

	/* Enable logical port */
	hw->mac.ops.update_lport_state(hw, glort, interface->glort_count, true);

	/* update VLAN table */
	hw->mac.ops.update_vlan(hw, FM10K_VLAN_ALL, 0,
				xcast_mode == FM10K_XCAST_MODE_PROMISC);

	/* Add filter for VLAN 0 */
	hw->mac.ops.update_vlan(hw, 0, 0, true);

	/* update table with current entries */
	for (vid = hw->mac.default_vid ? fm10k_find_next_vlan(interface, 0) : 0;
	     vid < VLAN_N_VID;
	     vid = fm10k_find_next_vlan(interface, vid)) {
		hw->mac.ops.update_vlan(hw, vid, 0, true);
		hw->mac.ops.update_uc_addr(hw, glort, hw->mac.addr,
					   vid, true, 0);
	}

	/* syncronize all of the addresses */
	if (xcast_mode != FM10K_XCAST_MODE_PROMISC) {
		__dev_uc_sync(netdev, fm10k_uc_sync, fm10k_uc_unsync);
		if (xcast_mode != FM10K_XCAST_MODE_ALLMULTI)
			__dev_mc_sync(netdev, fm10k_mc_sync, fm10k_mc_unsync);
	}

	/* update xcast mode */
	hw->mac.ops.update_xcast_mode(hw, glort, xcast_mode);

	fm10k_mbx_unlock(interface);

	/* record updated xcast mode state */
	interface->xcast_mode = xcast_mode;

	/* Restore tunnel configuration */
	fm10k_restore_vxlan_port(interface);
}

void fm10k_reset_rx_state(struct fm10k_intfc *interface)
{
	struct net_device *netdev = interface->netdev;
	struct fm10k_hw *hw = &interface->hw;

	fm10k_mbx_lock(interface);

	/* clear the logical port state on lower device */
	hw->mac.ops.update_lport_state(hw, interface->glort,
				       interface->glort_count, false);

	fm10k_mbx_unlock(interface);

	/* reset flags to default state */
	interface->xcast_mode = FM10K_XCAST_MODE_NONE;

	/* clear the sync flag since the lport has been dropped */
	__dev_uc_unsync(netdev, NULL);
	__dev_mc_unsync(netdev, NULL);
}

/**
 * fm10k_get_stats64 - Get System Network Statistics
 * @netdev: network interface device structure
 * @stats: storage space for 64bit statistics
 *
 * Returns 64bit statistics, for use in the ndo_get_stats64 callback. This
 * function replaces fm10k_get_stats for kernels which support it.
 */
static struct rtnl_link_stats64 *fm10k_get_stats64(struct net_device *netdev,
						   struct rtnl_link_stats64 *stats)
{
	struct fm10k_intfc *interface = netdev_priv(netdev);
	struct fm10k_ring *ring;
	unsigned int start, i;
	u64 bytes, packets;

	rcu_read_lock();

	for (i = 0; i < interface->num_rx_queues; i++) {
		ring = ACCESS_ONCE(interface->rx_ring[i]);

		if (!ring)
			continue;

		do {
			start = u64_stats_fetch_begin_irq(&ring->syncp);
			packets = ring->stats.packets;
			bytes   = ring->stats.bytes;
		} while (u64_stats_fetch_retry_irq(&ring->syncp, start));

		stats->rx_packets += packets;
		stats->rx_bytes   += bytes;
	}

	for (i = 0; i < interface->num_tx_queues; i++) {
		ring = ACCESS_ONCE(interface->rx_ring[i]);

		if (!ring)
			continue;

		do {
			start = u64_stats_fetch_begin_irq(&ring->syncp);
			packets = ring->stats.packets;
			bytes   = ring->stats.bytes;
		} while (u64_stats_fetch_retry_irq(&ring->syncp, start));

		stats->tx_packets += packets;
		stats->tx_bytes   += bytes;
	}

	rcu_read_unlock();

	/* following stats updated by fm10k_service_task() */
	stats->rx_missed_errors	= netdev->stats.rx_missed_errors;

	return stats;
}

int fm10k_setup_tc(struct net_device *dev, u8 tc)
{
	struct fm10k_intfc *interface = netdev_priv(dev);

	/* Currently only the PF supports priority classes */
	if (tc && (interface->hw.mac.type != fm10k_mac_pf))
		return -EINVAL;

	/* Hardware supports up to 8 traffic classes */
	if (tc > 8)
		return -EINVAL;

	/* Hardware has to reinitialize queues to match packet
	 * buffer alignment. Unfortunately, the hardware is not
	 * flexible enough to do this dynamically.
	 */
	if (netif_running(dev))
		fm10k_close(dev);

	fm10k_mbx_free_irq(interface);

	fm10k_clear_queueing_scheme(interface);

	/* we expect the prio_tc map to be repopulated later */
	netdev_reset_tc(dev);
	netdev_set_num_tc(dev, tc);

	fm10k_init_queueing_scheme(interface);

	fm10k_mbx_request_irq(interface);

	if (netif_running(dev))
		fm10k_open(dev);

	/* flag to indicate SWPRI has yet to be updated */
	interface->flags |= FM10K_FLAG_SWPRI_CONFIG;

	return 0;
}

static int fm10k_ioctl(struct net_device *netdev, struct ifreq *ifr, int cmd)
{
	switch (cmd) {
	case SIOCGHWTSTAMP:
		return fm10k_get_ts_config(netdev, ifr);
	case SIOCSHWTSTAMP:
		return fm10k_set_ts_config(netdev, ifr);
	default:
		return -EOPNOTSUPP;
	}
}

static void fm10k_assign_l2_accel(struct fm10k_intfc *interface,
				  struct fm10k_l2_accel *l2_accel)
{
	struct fm10k_ring *ring;
	int i;

	for (i = 0; i < interface->num_rx_queues; i++) {
		ring = interface->rx_ring[i];
		rcu_assign_pointer(ring->l2_accel, l2_accel);
	}

	interface->l2_accel = l2_accel;
}

static void *fm10k_dfwd_add_station(struct net_device *dev,
				    struct net_device *sdev)
{
	struct fm10k_intfc *interface = netdev_priv(dev);
	struct fm10k_l2_accel *l2_accel = interface->l2_accel;
	struct fm10k_l2_accel *old_l2_accel = NULL;
	struct fm10k_dglort_cfg dglort = { 0 };
	struct fm10k_hw *hw = &interface->hw;
	int size = 0, i;
	u16 glort;

	/* allocate l2 accel structure if it is not available */
	if (!l2_accel) {
		/* verify there is enough free GLORTs to support l2_accel */
		if (interface->glort_count < 7)
			return ERR_PTR(-EBUSY);

		size = offsetof(struct fm10k_l2_accel, macvlan[7]);
		l2_accel = kzalloc(size, GFP_KERNEL);
		if (!l2_accel)
			return ERR_PTR(-ENOMEM);

		l2_accel->size = 7;
		l2_accel->dglort = interface->glort;

		/* update pointers */
		fm10k_assign_l2_accel(interface, l2_accel);
	/* do not expand if we are at our limit */
	} else if ((l2_accel->count == FM10K_MAX_STATIONS) ||
		   (l2_accel->count == (interface->glort_count - 1))) {
		return ERR_PTR(-EBUSY);
	/* expand if we have hit the size limit */
	} else if (l2_accel->count == l2_accel->size) {
		old_l2_accel = l2_accel;
		size = offsetof(struct fm10k_l2_accel,
				macvlan[(l2_accel->size * 2) + 1]);
		l2_accel = kzalloc(size, GFP_KERNEL);
		if (!l2_accel)
			return ERR_PTR(-ENOMEM);

		memcpy(l2_accel, old_l2_accel,
		       offsetof(struct fm10k_l2_accel,
				macvlan[old_l2_accel->size]));

		l2_accel->size = (old_l2_accel->size * 2) + 1;

		/* update pointers */
		fm10k_assign_l2_accel(interface, l2_accel);
		kfree_rcu(old_l2_accel, rcu);
	}

	/* add macvlan to accel table, and record GLORT for position */
	for (i = 0; i < l2_accel->size; i++) {
		if (!l2_accel->macvlan[i])
			break;
	}

	/* record station */
	l2_accel->macvlan[i] = sdev;
	l2_accel->count++;

	/* configure default DGLORT mapping for RSS/DCB */
	dglort.idx = fm10k_dglort_pf_rss;
	dglort.inner_rss = 1;
	dglort.rss_l = fls(interface->ring_feature[RING_F_RSS].mask);
	dglort.pc_l = fls(interface->ring_feature[RING_F_QOS].mask);
	dglort.glort = interface->glort;
	dglort.shared_l = fls(l2_accel->size);
	hw->mac.ops.configure_dglort_map(hw, &dglort);

	/* Add rules for this specific dglort to the switch */
	fm10k_mbx_lock(interface);

	glort = l2_accel->dglort + 1 + i;
	hw->mac.ops.update_xcast_mode(hw, glort, FM10K_XCAST_MODE_MULTI);
	hw->mac.ops.update_uc_addr(hw, glort, sdev->dev_addr, 0, true, 0);

	fm10k_mbx_unlock(interface);

	return sdev;
}

static void fm10k_dfwd_del_station(struct net_device *dev, void *priv)
{
	struct fm10k_intfc *interface = netdev_priv(dev);
	struct fm10k_l2_accel *l2_accel = ACCESS_ONCE(interface->l2_accel);
	struct fm10k_dglort_cfg dglort = { 0 };
	struct fm10k_hw *hw = &interface->hw;
	struct net_device *sdev = priv;
	int i;
	u16 glort;

	if (!l2_accel)
		return;

	/* search table for matching interface */
	for (i = 0; i < l2_accel->size; i++) {
		if (l2_accel->macvlan[i] == sdev)
			break;
	}

	/* exit if macvlan not found */
	if (i == l2_accel->size)
		return;

	/* Remove any rules specific to this dglort */
	fm10k_mbx_lock(interface);

	glort = l2_accel->dglort + 1 + i;
	hw->mac.ops.update_xcast_mode(hw, glort, FM10K_XCAST_MODE_NONE);
	hw->mac.ops.update_uc_addr(hw, glort, sdev->dev_addr, 0, false, 0);

	fm10k_mbx_unlock(interface);

	/* record removal */
	l2_accel->macvlan[i] = NULL;
	l2_accel->count--;

	/* configure default DGLORT mapping for RSS/DCB */
	dglort.idx = fm10k_dglort_pf_rss;
	dglort.inner_rss = 1;
	dglort.rss_l = fls(interface->ring_feature[RING_F_RSS].mask);
	dglort.pc_l = fls(interface->ring_feature[RING_F_QOS].mask);
	dglort.glort = interface->glort;
	if (l2_accel)
		dglort.shared_l = fls(l2_accel->size);
	hw->mac.ops.configure_dglort_map(hw, &dglort);

	/* If table is empty remove it */
	if (l2_accel->count == 0) {
		fm10k_assign_l2_accel(interface, NULL);
		kfree_rcu(l2_accel, rcu);
	}
}

static const struct net_device_ops fm10k_netdev_ops = {
	.ndo_open		= fm10k_open,
	.ndo_stop		= fm10k_close,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_start_xmit		= fm10k_xmit_frame,
	.ndo_set_mac_address	= fm10k_set_mac,
	.ndo_change_mtu		= fm10k_change_mtu,
	.ndo_tx_timeout		= fm10k_tx_timeout,
	.ndo_vlan_rx_add_vid	= fm10k_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid	= fm10k_vlan_rx_kill_vid,
	.ndo_set_rx_mode	= fm10k_set_rx_mode,
	.ndo_get_stats64	= fm10k_get_stats64,
	.ndo_setup_tc		= fm10k_setup_tc,
	.ndo_set_vf_mac		= fm10k_ndo_set_vf_mac,
	.ndo_set_vf_vlan	= fm10k_ndo_set_vf_vlan,
	.ndo_set_vf_rate	= fm10k_ndo_set_vf_bw,
	.ndo_get_vf_config	= fm10k_ndo_get_vf_config,
	.ndo_add_vxlan_port	= fm10k_add_vxlan_port,
	.ndo_del_vxlan_port	= fm10k_del_vxlan_port,
	.ndo_do_ioctl		= fm10k_ioctl,
	.ndo_dfwd_add_station	= fm10k_dfwd_add_station,
	.ndo_dfwd_del_station	= fm10k_dfwd_del_station,
};

#define DEFAULT_DEBUG_LEVEL_SHIFT 3

struct net_device *fm10k_alloc_netdev(void)
{
	struct fm10k_intfc *interface;
	struct net_device *dev;

	dev = alloc_etherdev_mq(sizeof(struct fm10k_intfc), MAX_QUEUES);
	if (!dev)
		return NULL;

	/* set net device and ethtool ops */
	dev->netdev_ops = &fm10k_netdev_ops;
	fm10k_set_ethtool_ops(dev);

	/* configure default debug level */
	interface = netdev_priv(dev);
	interface->msg_enable = (1 << DEFAULT_DEBUG_LEVEL_SHIFT) - 1;

	/* configure default features */
	dev->features |= NETIF_F_IP_CSUM |
			 NETIF_F_IPV6_CSUM |
			 NETIF_F_SG |
			 NETIF_F_TSO |
			 NETIF_F_TSO6 |
			 NETIF_F_TSO_ECN |
			 NETIF_F_GSO_UDP_TUNNEL |
			 NETIF_F_RXHASH |
			 NETIF_F_RXCSUM;

	/* all features defined to this point should be changeable */
	dev->hw_features |= dev->features;

	/* allow user to enable L2 forwarding acceleration */
	dev->hw_features |= NETIF_F_HW_L2FW_DOFFLOAD;

	/* configure VLAN features */
	dev->vlan_features |= dev->features;

	/* configure tunnel offloads */
	dev->hw_enc_features |= NETIF_F_IP_CSUM |
				NETIF_F_TSO |
				NETIF_F_TSO6 |
				NETIF_F_TSO_ECN |
				NETIF_F_GSO_UDP_TUNNEL |
				NETIF_F_IPV6_CSUM;

	/* we want to leave these both on as we cannot disable VLAN tag
	 * insertion or stripping on the hardware since it is contained
	 * in the FTAG and not in the frame itself.
	 */
	dev->features |= NETIF_F_HW_VLAN_CTAG_TX |
			 NETIF_F_HW_VLAN_CTAG_RX |
			 NETIF_F_HW_VLAN_CTAG_FILTER;

	dev->priv_flags |= IFF_UNICAST_FLT;

	return dev;
}
