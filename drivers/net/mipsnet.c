/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#define DEBUG

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/netdevice.h>
#include <linux/platform_device.h>
#include <asm/io.h>
#include <asm/mips-boards/simint.h>

#include "mipsnet.h"		/* actual device IO mapping */

#define MIPSNET_VERSION "2005-06-20"

#define mipsnet_reg_address(dev, field) (dev->base_addr + field_offset(field))

struct mipsnet_priv {
	struct net_device_stats stats;
};

static char mipsnet_string[] = "mipsnet";

/*
 * Copy data from the MIPSNET rx data port
 */
static int ioiocpy_frommipsnet(struct net_device *dev, unsigned char *kdata,
			int len)
{
	uint32_t available_len = inl(mipsnet_reg_address(dev, rxDataCount));
	if (available_len < len)
		return -EFAULT;

	for (; len > 0; len--, kdata++) {
		*kdata = inb(mipsnet_reg_address(dev, rxDataBuffer));
	}

	return inl(mipsnet_reg_address(dev, rxDataCount));
}

static inline ssize_t mipsnet_put_todevice(struct net_device *dev,
	struct sk_buff *skb)
{
	int count_to_go = skb->len;
	char *buf_ptr = skb->data;
	struct mipsnet_priv *mp = netdev_priv(dev);

	pr_debug("%s: %s(): telling MIPSNET txDataCount(%d)\n",
	         dev->name, __FUNCTION__, skb->len);

	outl(skb->len, mipsnet_reg_address(dev, txDataCount));

	pr_debug("%s: %s(): sending data to MIPSNET txDataBuffer(%d)\n",
	         dev->name, __FUNCTION__, skb->len);

	for (; count_to_go; buf_ptr++, count_to_go--) {
		outb(*buf_ptr, mipsnet_reg_address(dev, txDataBuffer));
	}

	mp->stats.tx_packets++;
	mp->stats.tx_bytes += skb->len;

	return skb->len;
}

static int mipsnet_xmit(struct sk_buff *skb, struct net_device *dev)
{
	pr_debug("%s:%s(): transmitting %d bytes\n",
	         dev->name, __FUNCTION__, skb->len);

	/* Only one packet at a time. Once TXDONE interrupt is serviced, the
	 * queue will be restarted.
	 */
	netif_stop_queue(dev);
	mipsnet_put_todevice(dev, skb);

	return 0;
}

static inline ssize_t mipsnet_get_fromdev(struct net_device *dev, size_t count)
{
	struct sk_buff *skb;
	size_t len = count;
	struct mipsnet_priv *mp = netdev_priv(dev);

	if (!(skb = alloc_skb(len + 2, GFP_KERNEL))) {
		mp->stats.rx_dropped++;
		return -ENOMEM;
	}

	skb_reserve(skb, 2);
	if (ioiocpy_frommipsnet(dev, skb_put(skb, len), len))
		return -EFAULT;

	skb->protocol = eth_type_trans(skb, dev);
	skb->ip_summed = CHECKSUM_UNNECESSARY;

	pr_debug("%s:%s(): pushing RXed data to kernel\n",
	         dev->name, __FUNCTION__);
	netif_rx(skb);

	mp->stats.rx_packets++;
	mp->stats.rx_bytes += len;

	return count;
}

static irqreturn_t mipsnet_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;

	irqreturn_t retval = IRQ_NONE;
	uint64_t interruptFlags;

	if (irq == dev->irq) {
		pr_debug("%s:%s(): irq %d for device\n",
		         dev->name, __FUNCTION__, irq);

		retval = IRQ_HANDLED;

		interruptFlags =
		    inl(mipsnet_reg_address(dev, interruptControl));
		pr_debug("%s:%s(): intCtl=0x%016llx\n", dev->name,
		         __FUNCTION__, interruptFlags);

		if (interruptFlags & MIPSNET_INTCTL_TXDONE) {
			pr_debug("%s:%s(): got TXDone\n",
			         dev->name, __FUNCTION__);
			outl(MIPSNET_INTCTL_TXDONE,
			     mipsnet_reg_address(dev, interruptControl));
			// only one packet at a time, we are done.
			netif_wake_queue(dev);
		} else if (interruptFlags & MIPSNET_INTCTL_RXDONE) {
			pr_debug("%s:%s(): got RX data\n",
			         dev->name, __FUNCTION__);
			mipsnet_get_fromdev(dev,
			            inl(mipsnet_reg_address(dev, rxDataCount)));
			pr_debug("%s:%s(): clearing RX int\n",
			         dev->name, __FUNCTION__);
			outl(MIPSNET_INTCTL_RXDONE,
			     mipsnet_reg_address(dev, interruptControl));

		} else if (interruptFlags & MIPSNET_INTCTL_TESTBIT) {
			pr_debug("%s:%s(): got test interrupt\n",
			         dev->name, __FUNCTION__);
			// TESTBIT is cleared on read.
			//    And takes effect after a write with 0
			outl(0, mipsnet_reg_address(dev, interruptControl));
		} else {
			pr_debug("%s:%s(): no valid fags 0x%016llx\n",
			         dev->name, __FUNCTION__, interruptFlags);
			// Maybe shared IRQ, just ignore, no clearing.
			retval = IRQ_NONE;
		}

	} else {
		printk(KERN_INFO "%s: %s(): irq %d for unknown device\n",
		       dev->name, __FUNCTION__, irq);
		retval = IRQ_NONE;
	}
	return retval;
}				//mipsnet_interrupt()

static int mipsnet_open(struct net_device *dev)
{
	int err;
	pr_debug("%s: mipsnet_open\n", dev->name);

	err = request_irq(dev->irq, &mipsnet_interrupt,
			  IRQF_SHARED, dev->name, (void *) dev);

	if (err) {
		pr_debug("%s: %s(): can't get irq %d\n",
		         dev->name, __FUNCTION__, dev->irq);
		release_region(dev->base_addr, MIPSNET_IO_EXTENT);
		return err;
	}

	pr_debug("%s: %s(): got IO region at 0x%04lx and irq %d for dev.\n",
	         dev->name, __FUNCTION__, dev->base_addr, dev->irq);


	netif_start_queue(dev);

	// test interrupt handler
	outl(MIPSNET_INTCTL_TESTBIT,
	     mipsnet_reg_address(dev, interruptControl));


	return 0;
}

static int mipsnet_close(struct net_device *dev)
{
	pr_debug("%s: %s()\n", dev->name, __FUNCTION__);
	netif_stop_queue(dev);
	return 0;
}

static struct net_device_stats *mipsnet_get_stats(struct net_device *dev)
{
	struct mipsnet_priv *mp = netdev_priv(dev);

	return &mp->stats;
}

static void mipsnet_set_mclist(struct net_device *dev)
{
	// we don't do anything
	return;
}

static int __init mipsnet_probe(struct device *dev)
{
	struct net_device *netdev;
	int err;

	netdev = alloc_etherdev(sizeof(struct mipsnet_priv));
	if (!netdev) {
		err = -ENOMEM;
		goto out;
	}

	dev_set_drvdata(dev, netdev);

	netdev->open			= mipsnet_open;
	netdev->stop			= mipsnet_close;
	netdev->hard_start_xmit		= mipsnet_xmit;
	netdev->get_stats		= mipsnet_get_stats;
	netdev->set_multicast_list	= mipsnet_set_mclist;

	/*
	 * TODO: probe for these or load them from PARAM
	 */
	netdev->base_addr = 0x4200;
	netdev->irq = MIPSCPU_INT_BASE + MIPSCPU_INT_MB0 +
	              inl(mipsnet_reg_address(netdev, interruptInfo));

	// Get the io region now, get irq on open()
	if (!request_region(netdev->base_addr, MIPSNET_IO_EXTENT, "mipsnet")) {
		pr_debug("%s: %s(): IO region {start: 0x%04lux, len: %d} "
		         "for dev is not availble.\n", netdev->name,
		         __FUNCTION__, netdev->base_addr, MIPSNET_IO_EXTENT);
		err = -EBUSY;
		goto out_free_netdev;
	}

	/*
	 * Lacking any better mechanism to allocate a MAC address we use a
	 * random one ...
	 */
	random_ether_addr(netdev->dev_addr);

	err = register_netdev(netdev);
	if (err) {
		printk(KERN_ERR "MIPSNet: failed to register netdev.\n");
		goto out_free_region;
	}

	return 0;

out_free_region:
	release_region(netdev->base_addr, MIPSNET_IO_EXTENT);

out_free_netdev:
	free_netdev(netdev);

out:
	return err;
}

static int __devexit mipsnet_device_remove(struct device *device)
{
	struct net_device *dev = dev_get_drvdata(device);

	unregister_netdev(dev);
	release_region(dev->base_addr, MIPSNET_IO_EXTENT);
	free_netdev(dev);
	dev_set_drvdata(device, NULL);

	return 0;
}

static struct device_driver mipsnet_driver = {
	.name	= mipsnet_string,
	.bus	= &platform_bus_type,
	.probe	= mipsnet_probe,
	.remove	= __devexit_p(mipsnet_device_remove),
};

static int __init mipsnet_init_module(void)
{
	int err;

	printk(KERN_INFO "MIPSNet Ethernet driver. Version: %s. "
	       "(c)2005 MIPS Technologies, Inc.\n", MIPSNET_VERSION);

	err = driver_register(&mipsnet_driver);
	if (err)
		printk(KERN_ERR "Driver registration failed\n");

	return err;
}

static void __exit mipsnet_exit_module(void)
{
	pr_debug("MIPSNet Ethernet driver exiting\n");

	driver_unregister(&mipsnet_driver);
}

module_init(mipsnet_init_module);
module_exit(mipsnet_exit_module);
