/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#define DEBUG

#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/platform_device.h>
#include <asm/mips-boards/simint.h>

#include "mipsnet.h"		/* actual device IO mapping */

#define MIPSNET_VERSION "2005-06-20"

#define mipsnet_reg_address(dev, field) (dev->base_addr + field_offset(field))

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

	for (; len > 0; len--, kdata++)
		*kdata = inb(mipsnet_reg_address(dev, rxDataBuffer));

	return inl(mipsnet_reg_address(dev, rxDataCount));
}

static inline ssize_t mipsnet_put_todevice(struct net_device *dev,
	struct sk_buff *skb)
{
	int count_to_go = skb->len;
	char *buf_ptr = skb->data;

	outl(skb->len, mipsnet_reg_address(dev, txDataCount));

	for (; count_to_go; buf_ptr++, count_to_go--)
		outb(*buf_ptr, mipsnet_reg_address(dev, txDataBuffer));

	dev->stats.tx_packets++;
	dev->stats.tx_bytes += skb->len;

	return skb->len;
}

static int mipsnet_xmit(struct sk_buff *skb, struct net_device *dev)
{
	/*
	 * Only one packet at a time. Once TXDONE interrupt is serviced, the
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

	skb = alloc_skb(len + 2, GFP_KERNEL);
	if (!skb) {
		dev->stats.rx_dropped++;
		return -ENOMEM;
	}

	skb_reserve(skb, 2);
	if (ioiocpy_frommipsnet(dev, skb_put(skb, len), len))
		return -EFAULT;

	skb->protocol = eth_type_trans(skb, dev);
	skb->ip_summed = CHECKSUM_UNNECESSARY;

	netif_rx(skb);

	dev->stats.rx_packets++;
	dev->stats.rx_bytes += len;

	return count;
}

static irqreturn_t mipsnet_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;

	irqreturn_t retval = IRQ_NONE;
	uint64_t interruptFlags;

	if (irq == dev->irq) {
		retval = IRQ_HANDLED;

		interruptFlags =
		    inl(mipsnet_reg_address(dev, interruptControl));

		if (interruptFlags & MIPSNET_INTCTL_TXDONE) {
			outl(MIPSNET_INTCTL_TXDONE,
			     mipsnet_reg_address(dev, interruptControl));
			/* only one packet at a time, we are done. */
			netif_wake_queue(dev);
		} else if (interruptFlags & MIPSNET_INTCTL_RXDONE) {
			mipsnet_get_fromdev(dev,
				    inl(mipsnet_reg_address(dev, rxDataCount)));
			outl(MIPSNET_INTCTL_RXDONE,
			     mipsnet_reg_address(dev, interruptControl));

		} else if (interruptFlags & MIPSNET_INTCTL_TESTBIT) {
			/*
			 * TESTBIT is cleared on read.
			 * And takes effect after a write with 0
			 */
			outl(0, mipsnet_reg_address(dev, interruptControl));
		} else {
			/* Maybe shared IRQ, just ignore, no clearing. */
			retval = IRQ_NONE;
		}

	} else {
		printk(KERN_INFO "%s: %s(): irq %d for unknown device\n",
		       dev->name, __FUNCTION__, irq);
		retval = IRQ_NONE;
	}
	return retval;
}

static int mipsnet_open(struct net_device *dev)
{
	int err;

	err = request_irq(dev->irq, &mipsnet_interrupt,
			  IRQF_SHARED, dev->name, (void *) dev);

	if (err) {
		release_region(dev->base_addr, MIPSNET_IO_EXTENT);
		return err;
	}

	netif_start_queue(dev);

	/* test interrupt handler */
	outl(MIPSNET_INTCTL_TESTBIT,
	     mipsnet_reg_address(dev, interruptControl));


	return 0;
}

static int mipsnet_close(struct net_device *dev)
{
	netif_stop_queue(dev);

	return 0;
}

static void mipsnet_set_mclist(struct net_device *dev)
{
}

static int __init mipsnet_probe(struct device *dev)
{
	struct net_device *netdev;
	int err;

	netdev = alloc_etherdev(0);
	if (!netdev) {
		err = -ENOMEM;
		goto out;
	}

	dev_set_drvdata(dev, netdev);

	netdev->open			= mipsnet_open;
	netdev->stop			= mipsnet_close;
	netdev->hard_start_xmit		= mipsnet_xmit;
	netdev->set_multicast_list	= mipsnet_set_mclist;

	/*
	 * TODO: probe for these or load them from PARAM
	 */
	netdev->base_addr = 0x4200;
	netdev->irq = MIPS_CPU_IRQ_BASE + MIPSCPU_INT_MB0 +
		      inl(mipsnet_reg_address(netdev, interruptInfo));

	/* Get the io region now, get irq on open() */
	if (!request_region(netdev->base_addr, MIPSNET_IO_EXTENT, "mipsnet")) {
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
	driver_unregister(&mipsnet_driver);
}

module_init(mipsnet_init_module);
module_exit(mipsnet_exit_module);
