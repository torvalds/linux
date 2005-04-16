/*********************************************************************
 *
 *	sir_dev.c:	irda sir network device
 * 
 *	Copyright (c) 2002 Martin Diehl
 * 
 *	This program is free software; you can redistribute it and/or 
 *	modify it under the terms of the GNU General Public License as 
 *	published by the Free Software Foundation; either version 2 of 
 *	the License, or (at your option) any later version.
 *
 ********************************************************************/    

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/smp_lock.h>
#include <linux/delay.h>

#include <net/irda/irda.h>
#include <net/irda/wrapper.h>
#include <net/irda/irda_device.h>

#include "sir-dev.h"

/***************************************************************************/

void sirdev_enable_rx(struct sir_dev *dev)
{
	if (unlikely(atomic_read(&dev->enable_rx)))
		return;

	/* flush rx-buffer - should also help in case of problems with echo cancelation */
	dev->rx_buff.data = dev->rx_buff.head;
	dev->rx_buff.len = 0;
	dev->rx_buff.in_frame = FALSE;
	dev->rx_buff.state = OUTSIDE_FRAME;
	atomic_set(&dev->enable_rx, 1);
}

static int sirdev_is_receiving(struct sir_dev *dev)
{
	if (!atomic_read(&dev->enable_rx))
		return 0;

	return (dev->rx_buff.state != OUTSIDE_FRAME);
}

int sirdev_set_dongle(struct sir_dev *dev, IRDA_DONGLE type)
{
	int err;

	IRDA_DEBUG(3, "%s : requesting dongle %d.\n", __FUNCTION__, type);

	err = sirdev_schedule_dongle_open(dev, type);
	if (unlikely(err))
		return err;
	down(&dev->fsm.sem);		/* block until config change completed */
	err = dev->fsm.result;
	up(&dev->fsm.sem);
	return err;
}

/* used by dongle drivers for dongle programming */

int sirdev_raw_write(struct sir_dev *dev, const char *buf, int len)
{
	unsigned long flags;
	int ret;

	if (unlikely(len > dev->tx_buff.truesize))
		return -ENOSPC;

	spin_lock_irqsave(&dev->tx_lock, flags);	/* serialize with other tx operations */
	while (dev->tx_buff.len > 0) {			/* wait until tx idle */
		spin_unlock_irqrestore(&dev->tx_lock, flags);
		msleep(10);
		spin_lock_irqsave(&dev->tx_lock, flags);
	}

	dev->tx_buff.data = dev->tx_buff.head;
	memcpy(dev->tx_buff.data, buf, len);	
	dev->tx_buff.len = len;

	ret = dev->drv->do_write(dev, dev->tx_buff.data, dev->tx_buff.len);
	if (ret > 0) {
		IRDA_DEBUG(3, "%s(), raw-tx started\n", __FUNCTION__);

		dev->tx_buff.data += ret;
		dev->tx_buff.len -= ret;
		dev->raw_tx = 1;
		ret = len;		/* all data is going to be sent */
	}
	spin_unlock_irqrestore(&dev->tx_lock, flags);
	return ret;
}

/* seems some dongle drivers may need this */

int sirdev_raw_read(struct sir_dev *dev, char *buf, int len)
{
	int count;

	if (atomic_read(&dev->enable_rx))
		return -EIO;		/* fail if we expect irda-frames */

	count = (len < dev->rx_buff.len) ? len : dev->rx_buff.len;

	if (count > 0) {
		memcpy(buf, dev->rx_buff.data, count);
		dev->rx_buff.data += count;
		dev->rx_buff.len -= count;
	}

	/* remaining stuff gets flushed when re-enabling normal rx */

	return count;
}

int sirdev_set_dtr_rts(struct sir_dev *dev, int dtr, int rts)
{
	int ret = -ENXIO;
	if (dev->drv->set_dtr_rts != 0)
		ret =  dev->drv->set_dtr_rts(dev, dtr, rts);
	return ret;
}
	
/**********************************************************************/

/* called from client driver - likely with bh-context - to indicate
 * it made some progress with transmission. Hence we send the next
 * chunk, if any, or complete the skb otherwise
 */

void sirdev_write_complete(struct sir_dev *dev)
{
	unsigned long flags;
	struct sk_buff *skb;
	int actual = 0;
	int err;
	
	spin_lock_irqsave(&dev->tx_lock, flags);

	IRDA_DEBUG(3, "%s() - dev->tx_buff.len = %d\n",
		   __FUNCTION__, dev->tx_buff.len);

	if (likely(dev->tx_buff.len > 0))  {
		/* Write data left in transmit buffer */
		actual = dev->drv->do_write(dev, dev->tx_buff.data, dev->tx_buff.len);

		if (likely(actual>0)) {
			dev->tx_buff.data += actual;
			dev->tx_buff.len  -= actual;
		}
		else if (unlikely(actual<0)) {
			/* could be dropped later when we have tx_timeout to recover */
			IRDA_ERROR("%s: drv->do_write failed (%d)\n",
				   __FUNCTION__, actual);
			if ((skb=dev->tx_skb) != NULL) {
				dev->tx_skb = NULL;
				dev_kfree_skb_any(skb);
				dev->stats.tx_errors++;		      
				dev->stats.tx_dropped++;		      
			}
			dev->tx_buff.len = 0;
		}
		if (dev->tx_buff.len > 0)
			goto done;	/* more data to send later */
	}

	if (unlikely(dev->raw_tx != 0)) {
		/* in raw mode we are just done now after the buffer was sent
		 * completely. Since this was requested by some dongle driver
		 * running under the control of the irda-thread we must take
		 * care here not to re-enable the queue. The queue will be
		 * restarted when the irda-thread has completed the request.
		 */

		IRDA_DEBUG(3, "%s(), raw-tx done\n", __FUNCTION__);
		dev->raw_tx = 0;
		goto done;	/* no post-frame handling in raw mode */
	}

	/* we have finished now sending this skb.
	 * update statistics and free the skb.
	 * finally we check and trigger a pending speed change, if any.
	 * if not we switch to rx mode and wake the queue for further
	 * packets.
	 * note the scheduled speed request blocks until the lower
	 * client driver and the corresponding hardware has really
	 * finished sending all data (xmit fifo drained f.e.)
	 * before the speed change gets finally done and the queue
	 * re-activated.
	 */

	IRDA_DEBUG(5, "%s(), finished with frame!\n", __FUNCTION__);
		
	if ((skb=dev->tx_skb) != NULL) {
		dev->tx_skb = NULL;
		dev->stats.tx_packets++;		      
		dev->stats.tx_bytes += skb->len;
		dev_kfree_skb_any(skb);
	}

	if (unlikely(dev->new_speed > 0)) {
		IRDA_DEBUG(5, "%s(), Changing speed!\n", __FUNCTION__);
		err = sirdev_schedule_speed(dev, dev->new_speed);
		if (unlikely(err)) {
			/* should never happen
			 * forget the speed change and hope the stack recovers
			 */
			IRDA_ERROR("%s - schedule speed change failed: %d\n",
				   __FUNCTION__, err);
			netif_wake_queue(dev->netdev);
		}
		/* else: success
		 *	speed change in progress now
		 *	on completion dev->new_speed gets cleared,
		 *	rx-reenabled and the queue restarted
		 */
	}
	else {
		sirdev_enable_rx(dev);
		netif_wake_queue(dev->netdev);
	}

done:
	spin_unlock_irqrestore(&dev->tx_lock, flags);
}

/* called from client driver - likely with bh-context - to give us
 * some more received bytes. We put them into the rx-buffer,
 * normally unwrapping and building LAP-skb's (unless rx disabled)
 */

int sirdev_receive(struct sir_dev *dev, const unsigned char *cp, size_t count) 
{
	if (!dev || !dev->netdev) {
		IRDA_WARNING("%s(), not ready yet!\n", __FUNCTION__);
		return -1;
	}

	if (!dev->irlap) {
		IRDA_WARNING("%s - too early: %p / %zd!\n",
			     __FUNCTION__, cp, count);
		return -1;
	}

	if (cp==NULL) {
		/* error already at lower level receive
		 * just update stats and set media busy
		 */
		irda_device_set_media_busy(dev->netdev, TRUE);
		dev->stats.rx_dropped++;
		IRDA_DEBUG(0, "%s; rx-drop: %zd\n", __FUNCTION__, count);
		return 0;
	}

	/* Read the characters into the buffer */
	if (likely(atomic_read(&dev->enable_rx))) {
		while (count--)
			/* Unwrap and destuff one byte */
			async_unwrap_char(dev->netdev, &dev->stats, 
					  &dev->rx_buff, *cp++);
	} else {
		while (count--) {
			/* rx not enabled: save the raw bytes and never
			 * trigger any netif_rx. The received bytes are flushed
			 * later when we re-enable rx but might be read meanwhile
			 * by the dongle driver.
			 */
			dev->rx_buff.data[dev->rx_buff.len++] = *cp++;

			/* What should we do when the buffer is full? */
			if (unlikely(dev->rx_buff.len == dev->rx_buff.truesize))
				dev->rx_buff.len = 0;
		}
	}

	return 0;
}

/**********************************************************************/

/* callbacks from network layer */

static struct net_device_stats *sirdev_get_stats(struct net_device *ndev)
{
	struct sir_dev *dev = ndev->priv;

	return (dev) ? &dev->stats : NULL;
}

static int sirdev_hard_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct sir_dev *dev = ndev->priv;
	unsigned long flags;
	int actual = 0;
	int err;
	s32 speed;

	IRDA_ASSERT(dev != NULL, return 0;);

	netif_stop_queue(ndev);

	IRDA_DEBUG(3, "%s(), skb->len = %d\n", __FUNCTION__, skb->len);

	speed = irda_get_next_speed(skb);
	if ((speed != dev->speed) && (speed != -1)) {
		if (!skb->len) {
			err = sirdev_schedule_speed(dev, speed);
			if (unlikely(err == -EWOULDBLOCK)) {
				/* Failed to initiate the speed change, likely the fsm
				 * is still busy (pretty unlikely, but...)
				 * We refuse to accept the skb and return with the queue
				 * stopped so the network layer will retry after the
				 * fsm completes and wakes the queue.
				 */
				 return 1;
			}
			else if (unlikely(err)) {
				/* other fatal error - forget the speed change and
				 * hope the stack will recover somehow
				 */
				 netif_start_queue(ndev);
			}
			/* else: success
			 *	speed change in progress now
			 *	on completion the queue gets restarted
			 */

			dev_kfree_skb_any(skb);
			return 0;
		} else
			dev->new_speed = speed;
	}

	/* Init tx buffer*/
	dev->tx_buff.data = dev->tx_buff.head;

	/* Check problems */
	if(spin_is_locked(&dev->tx_lock)) {
		IRDA_DEBUG(3, "%s(), write not completed\n", __FUNCTION__);
	}

	/* serialize with write completion */
	spin_lock_irqsave(&dev->tx_lock, flags);

        /* Copy skb to tx_buff while wrapping, stuffing and making CRC */
	dev->tx_buff.len = async_wrap_skb(skb, dev->tx_buff.data, dev->tx_buff.truesize); 

	/* transmission will start now - disable receive.
	 * if we are just in the middle of an incoming frame,
	 * treat it as collision. probably it's a good idea to
	 * reset the rx_buf OUTSIDE_FRAME in this case too?
	 */
	atomic_set(&dev->enable_rx, 0);
	if (unlikely(sirdev_is_receiving(dev)))
		dev->stats.collisions++;

	actual = dev->drv->do_write(dev, dev->tx_buff.data, dev->tx_buff.len);

	if (likely(actual > 0)) {
		dev->tx_skb = skb;
		ndev->trans_start = jiffies;
		dev->tx_buff.data += actual;
		dev->tx_buff.len -= actual;
	}
	else if (unlikely(actual < 0)) {
		/* could be dropped later when we have tx_timeout to recover */
		IRDA_ERROR("%s: drv->do_write failed (%d)\n",
			   __FUNCTION__, actual);
		dev_kfree_skb_any(skb);
		dev->stats.tx_errors++;		      
		dev->stats.tx_dropped++;		      
		netif_wake_queue(ndev);
	}
	spin_unlock_irqrestore(&dev->tx_lock, flags);

	return 0;
}

/* called from network layer with rtnl hold */

static int sirdev_ioctl(struct net_device *ndev, struct ifreq *rq, int cmd)
{
	struct if_irda_req *irq = (struct if_irda_req *) rq;
	struct sir_dev *dev = ndev->priv;
	int ret = 0;

	IRDA_ASSERT(dev != NULL, return -1;);

	IRDA_DEBUG(3, "%s(), %s, (cmd=0x%X)\n", __FUNCTION__, ndev->name, cmd);
	
	switch (cmd) {
	case SIOCSBANDWIDTH: /* Set bandwidth */
		if (!capable(CAP_NET_ADMIN))
			ret = -EPERM;
		else
			ret = sirdev_schedule_speed(dev, irq->ifr_baudrate);
		/* cannot sleep here for completion
		 * we are called from network layer with rtnl hold
		 */
		break;

	case SIOCSDONGLE: /* Set dongle */
		if (!capable(CAP_NET_ADMIN))
			ret = -EPERM;
		else
			ret = sirdev_schedule_dongle_open(dev, irq->ifr_dongle);
		/* cannot sleep here for completion
		 * we are called from network layer with rtnl hold
		 */
		break;

	case SIOCSMEDIABUSY: /* Set media busy */
		if (!capable(CAP_NET_ADMIN))
			ret = -EPERM;
		else
			irda_device_set_media_busy(dev->netdev, TRUE);
		break;

	case SIOCGRECEIVING: /* Check if we are receiving right now */
		irq->ifr_receiving = sirdev_is_receiving(dev);
		break;

	case SIOCSDTRRTS:
		if (!capable(CAP_NET_ADMIN))
			ret = -EPERM;
		else
			ret = sirdev_schedule_dtr_rts(dev, irq->ifr_dtr, irq->ifr_rts);
		/* cannot sleep here for completion
		 * we are called from network layer with rtnl hold
		 */
		break;

	case SIOCSMODE:
#if 0
		if (!capable(CAP_NET_ADMIN))
			ret = -EPERM;
		else
			ret = sirdev_schedule_mode(dev, irq->ifr_mode);
		/* cannot sleep here for completion
		 * we are called from network layer with rtnl hold
		 */
		break;
#endif
	default:
		ret = -EOPNOTSUPP;
	}
	
	return ret;
}

/* ----------------------------------------------------------------------------- */

#define SIRBUF_ALLOCSIZE 4269	/* worst case size of a wrapped IrLAP frame */

static int sirdev_alloc_buffers(struct sir_dev *dev)
{
	dev->tx_buff.truesize = SIRBUF_ALLOCSIZE;
	dev->rx_buff.truesize = IRDA_SKB_MAX_MTU; 

	/* Bootstrap ZeroCopy Rx */
	dev->rx_buff.skb = __dev_alloc_skb(dev->rx_buff.truesize, GFP_KERNEL);
	if (dev->rx_buff.skb == NULL)
		return -ENOMEM;
	skb_reserve(dev->rx_buff.skb, 1);
	dev->rx_buff.head = dev->rx_buff.skb->data;

	dev->tx_buff.head = kmalloc(dev->tx_buff.truesize, GFP_KERNEL);
	if (dev->tx_buff.head == NULL) {
		kfree_skb(dev->rx_buff.skb);
		dev->rx_buff.skb = NULL;
		dev->rx_buff.head = NULL;
		return -ENOMEM;
	}

	dev->tx_buff.data = dev->tx_buff.head;
	dev->rx_buff.data = dev->rx_buff.head;
	dev->tx_buff.len = 0;
	dev->rx_buff.len = 0;

	dev->rx_buff.in_frame = FALSE;
	dev->rx_buff.state = OUTSIDE_FRAME;
	return 0;
};

static void sirdev_free_buffers(struct sir_dev *dev)
{
	if (dev->rx_buff.skb)
		kfree_skb(dev->rx_buff.skb);
	if (dev->tx_buff.head)
		kfree(dev->tx_buff.head);
	dev->rx_buff.head = dev->tx_buff.head = NULL;
	dev->rx_buff.skb = NULL;
}

static int sirdev_open(struct net_device *ndev)
{
	struct sir_dev *dev = ndev->priv;
	const struct sir_driver *drv = dev->drv;

	if (!drv)
		return -ENODEV;

	/* increase the reference count of the driver module before doing serious stuff */
	if (!try_module_get(drv->owner))
		return -ESTALE;

	IRDA_DEBUG(2, "%s()\n", __FUNCTION__);

	if (sirdev_alloc_buffers(dev))
		goto errout_dec;

	if (!dev->drv->start_dev  ||  dev->drv->start_dev(dev))
		goto errout_free;

	sirdev_enable_rx(dev);
	dev->raw_tx = 0;

	netif_start_queue(ndev);
	dev->irlap = irlap_open(ndev, &dev->qos, dev->hwname);
	if (!dev->irlap)
		goto errout_stop;

	netif_wake_queue(ndev);

	IRDA_DEBUG(2, "%s - done, speed = %d\n", __FUNCTION__, dev->speed);

	return 0;

errout_stop:
	atomic_set(&dev->enable_rx, 0);
	if (dev->drv->stop_dev)
		dev->drv->stop_dev(dev);
errout_free:
	sirdev_free_buffers(dev);
errout_dec:
	module_put(drv->owner);
	return -EAGAIN;
}

static int sirdev_close(struct net_device *ndev)
{
	struct sir_dev *dev = ndev->priv;
	const struct sir_driver *drv;

//	IRDA_DEBUG(0, "%s\n", __FUNCTION__);

	netif_stop_queue(ndev);

	down(&dev->fsm.sem);		/* block on pending config completion */

	atomic_set(&dev->enable_rx, 0);

	if (unlikely(!dev->irlap))
		goto out;
	irlap_close(dev->irlap);
	dev->irlap = NULL;

	drv = dev->drv;
	if (unlikely(!drv  ||  !dev->priv))
		goto out;

	if (drv->stop_dev)
		drv->stop_dev(dev);

	sirdev_free_buffers(dev);
	module_put(drv->owner);

out:
	dev->speed = 0;
	up(&dev->fsm.sem);
	return 0;
}

/* ----------------------------------------------------------------------------- */

struct sir_dev * sirdev_get_instance(const struct sir_driver *drv, const char *name)
{
	struct net_device *ndev;
	struct sir_dev *dev;

	IRDA_DEBUG(0, "%s - %s\n", __FUNCTION__, name);

	/* instead of adding tests to protect against drv->do_write==NULL
	 * at several places we refuse to create a sir_dev instance for
	 * drivers which don't implement do_write.
	 */
	if (!drv ||  !drv->do_write)
		return NULL;

	/*
	 *  Allocate new instance of the device
	 */
	ndev = alloc_irdadev(sizeof(*dev));
	if (ndev == NULL) {
		IRDA_ERROR("%s - Can't allocate memory for IrDA control block!\n", __FUNCTION__);
		goto out;
	}
	dev = ndev->priv;

	irda_init_max_qos_capabilies(&dev->qos);
	dev->qos.baud_rate.bits = IR_9600|IR_19200|IR_38400|IR_57600|IR_115200;
	dev->qos.min_turn_time.bits = drv->qos_mtt_bits;
	irda_qos_bits_to_value(&dev->qos);

	strncpy(dev->hwname, name, sizeof(dev->hwname)-1);

	atomic_set(&dev->enable_rx, 0);
	dev->tx_skb = NULL;

	spin_lock_init(&dev->tx_lock);
	init_MUTEX(&dev->fsm.sem);

	INIT_LIST_HEAD(&dev->fsm.rq.lh_request);
	dev->fsm.rq.pending = 0;
	init_timer(&dev->fsm.rq.timer);

	dev->drv = drv;
	dev->netdev = ndev;

	SET_MODULE_OWNER(ndev);

	/* Override the network functions we need to use */
	ndev->hard_start_xmit = sirdev_hard_xmit;
	ndev->open = sirdev_open;
	ndev->stop = sirdev_close;
	ndev->get_stats = sirdev_get_stats;
	ndev->do_ioctl = sirdev_ioctl;

	if (register_netdev(ndev)) {
		IRDA_ERROR("%s(), register_netdev() failed!\n", __FUNCTION__);
		goto out_freenetdev;
	}

	return dev;

out_freenetdev:
	free_netdev(ndev);
out:
	return NULL;
}

int sirdev_put_instance(struct sir_dev *dev)
{
	int err = 0;

	IRDA_DEBUG(0, "%s\n", __FUNCTION__);

	atomic_set(&dev->enable_rx, 0);

	netif_carrier_off(dev->netdev);
	netif_device_detach(dev->netdev);

	if (dev->dongle_drv)
		err = sirdev_schedule_dongle_close(dev);
	if (err)
		IRDA_ERROR("%s - error %d\n", __FUNCTION__, err);

	sirdev_close(dev->netdev);

	down(&dev->fsm.sem);
	dev->fsm.state = SIRDEV_STATE_DEAD;	/* mark staled */
	dev->dongle_drv = NULL;
	dev->priv = NULL;
	up(&dev->fsm.sem);

	/* Remove netdevice */
	unregister_netdev(dev->netdev);

	free_netdev(dev->netdev);

	return 0;
}

