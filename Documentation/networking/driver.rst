.. SPDX-License-Identifier: GPL-2.0

=====================
Softnet Driver Issues
=====================

Probing guidelines
==================

Address validation
------------------

Any hardware layer address you obtain for your device should
be verified.  For example, for ethernet check it with
linux/etherdevice.h:is_valid_ether_addr()

Close/stop guidelines
=====================

Quiescence
----------

After the ndo_stop routine has been called, the hardware must
not receive or transmit any data.  All in flight packets must
be aborted. If necessary, poll or wait for completion of
any reset commands.

Auto-close
----------

The ndo_stop routine will be called by unregister_netdevice
if device is still UP.

Transmit path guidelines
========================

Stop queues in advance
----------------------

The ndo_start_xmit method must not return NETDEV_TX_BUSY under
any normal circumstances.  It is considered a hard error unless
there is no way your device can tell ahead of time when its
transmit function will become busy.

Instead it must maintain the queue properly.  For example,
for a driver implementing scatter-gather this means:

.. code-block:: c

	static u32 drv_tx_avail(struct drv_ring *dr)
	{
		u32 used = READ_ONCE(dr->prod) - READ_ONCE(dr->cons);

		return dr->tx_ring_size - (used & bp->tx_ring_mask);
	}

	static netdev_tx_t drv_hard_start_xmit(struct sk_buff *skb,
					       struct net_device *dev)
	{
		struct drv *dp = netdev_priv(dev);
		struct netdev_queue *txq;
		struct drv_ring *dr;
		int idx;

		idx = skb_get_queue_mapping(skb);
		dr = dp->tx_rings[idx];
		txq = netdev_get_tx_queue(dev, idx);

		//...
		/* This should be a very rare race - log it. */
		if (drv_tx_avail(dr) <= skb_shinfo(skb)->nr_frags + 1) {
			netif_stop_queue(dev);
			netdev_warn(dev, "Tx Ring full when queue awake!\n");
			return NETDEV_TX_BUSY;
		}

		//... queue packet to card ...

		netdev_tx_sent_queue(txq, skb->len);

		//... update tx producer index using WRITE_ONCE() ...

		if (!netif_txq_maybe_stop(txq, drv_tx_avail(dr),
					  MAX_SKB_FRAGS + 1, 2 * MAX_SKB_FRAGS))
			dr->stats.stopped++;

		//...
		return NETDEV_TX_OK;
	}

And then at the end of your TX reclamation event handling:

.. code-block:: c

	//... update tx consumer index using WRITE_ONCE() ...

	netif_txq_completed_wake(txq, cmpl_pkts, cmpl_bytes,
				 drv_tx_avail(dr), 2 * MAX_SKB_FRAGS);

Lockless queue stop / wake helper macros
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. kernel-doc:: include/net/netdev_queues.h
   :doc: Lockless queue stopping / waking helpers.

No exclusive ownership
----------------------

An ndo_start_xmit method must not modify the shared parts of a
cloned SKB.

Timely completions
------------------

Do not forget that once you return NETDEV_TX_OK from your
ndo_start_xmit method, it is your driver's responsibility to free
up the SKB and in some finite amount of time.

For example, this means that it is not allowed for your TX
mitigation scheme to let TX packets "hang out" in the TX
ring unreclaimed forever if no new TX packets are sent.
This error can deadlock sockets waiting for send buffer room
to be freed up.

If you return NETDEV_TX_BUSY from the ndo_start_xmit method, you
must not keep any reference to that SKB and you must not attempt
to free it up.
