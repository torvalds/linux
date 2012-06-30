/*
 * Loopback IEEE 802.15.4 interface
 *
 * Copyright 2007-2012 Siemens AG
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Written by:
 * Sergey Lapin <slapin@ossfans.org>
 * Dmitry Eremin-Solenikov <dbaryshkov@gmail.com>
 * Alexander Smirnov <alex.bluesman.smirnov@gmail.com>
 */

#include <linux/module.h>
#include <linux/timer.h>
#include <linux/platform_device.h>
#include <linux/netdevice.h>
#include <linux/spinlock.h>
#include <net/mac802154.h>
#include <net/wpan-phy.h>

static int numlbs = 1;

struct fakelb_dev_priv {
	struct ieee802154_dev *dev;

	struct list_head list;
	struct fakelb_priv *fake;

	spinlock_t lock;
	bool working;
};

struct fakelb_priv {
	struct list_head list;
	rwlock_t lock;
};

static int
fakelb_hw_ed(struct ieee802154_dev *dev, u8 *level)
{
	might_sleep();
	BUG_ON(!level);
	*level = 0xbe;

	return 0;
}

static int
fakelb_hw_channel(struct ieee802154_dev *dev, int page, int channel)
{
	pr_debug("set channel to %d\n", channel);

	might_sleep();
	dev->phy->current_page = page;
	dev->phy->current_channel = channel;

	return 0;
}

static void
fakelb_hw_deliver(struct fakelb_dev_priv *priv, struct sk_buff *skb)
{
	struct sk_buff *newskb;

	spin_lock(&priv->lock);
	if (priv->working) {
		newskb = pskb_copy(skb, GFP_ATOMIC);
		ieee802154_rx_irqsafe(priv->dev, newskb, 0xcc);
	}
	spin_unlock(&priv->lock);
}

static int
fakelb_hw_xmit(struct ieee802154_dev *dev, struct sk_buff *skb)
{
	struct fakelb_dev_priv *priv = dev->priv;
	struct fakelb_priv *fake = priv->fake;

	might_sleep();

	read_lock_bh(&fake->lock);
	if (priv->list.next == priv->list.prev) {
		/* we are the only one device */
		fakelb_hw_deliver(priv, skb);
	} else {
		struct fakelb_dev_priv *dp;
		list_for_each_entry(dp, &priv->fake->list, list) {
			if (dp != priv &&
			    (dp->dev->phy->current_channel ==
			     priv->dev->phy->current_channel))
				fakelb_hw_deliver(dp, skb);
		}
	}
	read_unlock_bh(&fake->lock);

	return 0;
}

static int
fakelb_hw_start(struct ieee802154_dev *dev) {
	struct fakelb_dev_priv *priv = dev->priv;
	int ret = 0;

	spin_lock(&priv->lock);
	if (priv->working)
		ret = -EBUSY;
	else
		priv->working = 1;
	spin_unlock(&priv->lock);

	return ret;
}

static void
fakelb_hw_stop(struct ieee802154_dev *dev) {
	struct fakelb_dev_priv *priv = dev->priv;

	spin_lock(&priv->lock);
	priv->working = 0;
	spin_unlock(&priv->lock);
}

static struct ieee802154_ops fakelb_ops = {
	.owner = THIS_MODULE,
	.xmit = fakelb_hw_xmit,
	.ed = fakelb_hw_ed,
	.set_channel = fakelb_hw_channel,
	.start = fakelb_hw_start,
	.stop = fakelb_hw_stop,
};

/* Number of dummy devices to be set up by this module. */
module_param(numlbs, int, 0);
MODULE_PARM_DESC(numlbs, " number of pseudo devices");

static int fakelb_add_one(struct device *dev, struct fakelb_priv *fake)
{
	struct fakelb_dev_priv *priv;
	int err;
	struct ieee802154_dev *ieee;

	ieee = ieee802154_alloc_device(sizeof(*priv), &fakelb_ops);
	if (!ieee)
		return -ENOMEM;

	priv = ieee->priv;
	priv->dev = ieee;

	/* 868 MHz BPSK	802.15.4-2003 */
	ieee->phy->channels_supported[0] |= 1;
	/* 915 MHz BPSK	802.15.4-2003 */
	ieee->phy->channels_supported[0] |= 0x7fe;
	/* 2.4 GHz O-QPSK 802.15.4-2003 */
	ieee->phy->channels_supported[0] |= 0x7FFF800;
	/* 868 MHz ASK 802.15.4-2006 */
	ieee->phy->channels_supported[1] |= 1;
	/* 915 MHz ASK 802.15.4-2006 */
	ieee->phy->channels_supported[1] |= 0x7fe;
	/* 868 MHz O-QPSK 802.15.4-2006 */
	ieee->phy->channels_supported[2] |= 1;
	/* 915 MHz O-QPSK 802.15.4-2006 */
	ieee->phy->channels_supported[2] |= 0x7fe;
	/* 2.4 GHz CSS 802.15.4a-2007 */
	ieee->phy->channels_supported[3] |= 0x3fff;
	/* UWB Sub-gigahertz 802.15.4a-2007 */
	ieee->phy->channels_supported[4] |= 1;
	/* UWB Low band 802.15.4a-2007 */
	ieee->phy->channels_supported[4] |= 0x1e;
	/* UWB High band 802.15.4a-2007 */
	ieee->phy->channels_supported[4] |= 0xffe0;
	/* 750 MHz O-QPSK 802.15.4c-2009 */
	ieee->phy->channels_supported[5] |= 0xf;
	/* 750 MHz MPSK 802.15.4c-2009 */
	ieee->phy->channels_supported[5] |= 0xf0;
	/* 950 MHz BPSK 802.15.4d-2009 */
	ieee->phy->channels_supported[6] |= 0x3ff;
	/* 950 MHz GFSK 802.15.4d-2009 */
	ieee->phy->channels_supported[6] |= 0x3ffc00;

	INIT_LIST_HEAD(&priv->list);
	priv->fake = fake;

	spin_lock_init(&priv->lock);

	ieee->parent = dev;

	err = ieee802154_register_device(ieee);
	if (err)
		goto err_reg;

	write_lock_bh(&fake->lock);
	list_add_tail(&priv->list, &fake->list);
	write_unlock_bh(&fake->lock);

	return 0;

err_reg:
	ieee802154_free_device(priv->dev);
	return err;
}

static void fakelb_del(struct fakelb_dev_priv *priv)
{
	write_lock_bh(&priv->fake->lock);
	list_del(&priv->list);
	write_unlock_bh(&priv->fake->lock);

	ieee802154_unregister_device(priv->dev);
	ieee802154_free_device(priv->dev);
}

static int __devinit fakelb_probe(struct platform_device *pdev)
{
	struct fakelb_priv *priv;
	struct fakelb_dev_priv *dp;
	int err = -ENOMEM;
	int i;

	priv = kzalloc(sizeof(struct fakelb_priv), GFP_KERNEL);
	if (!priv)
		goto err_alloc;

	INIT_LIST_HEAD(&priv->list);
	rwlock_init(&priv->lock);

	for (i = 0; i < numlbs; i++) {
		err = fakelb_add_one(&pdev->dev, priv);
		if (err < 0)
			goto err_slave;
	}

	platform_set_drvdata(pdev, priv);
	dev_info(&pdev->dev, "added ieee802154 hardware\n");
	return 0;

err_slave:
	list_for_each_entry(dp, &priv->list, list)
		fakelb_del(dp);
	kfree(priv);
err_alloc:
	return err;
}

static int __devexit fakelb_remove(struct platform_device *pdev)
{
	struct fakelb_priv *priv = platform_get_drvdata(pdev);
	struct fakelb_dev_priv *dp, *temp;

	list_for_each_entry_safe(dp, temp, &priv->list, list)
		fakelb_del(dp);
	kfree(priv);

	return 0;
}

static struct platform_device *ieee802154fake_dev;

static struct platform_driver ieee802154fake_driver = {
	.probe = fakelb_probe,
	.remove = __devexit_p(fakelb_remove),
	.driver = {
			.name = "ieee802154fakelb",
			.owner = THIS_MODULE,
	},
};

static __init int fakelb_init_module(void)
{
	ieee802154fake_dev = platform_device_register_simple(
			     "ieee802154fakelb", -1, NULL, 0);
	return platform_driver_register(&ieee802154fake_driver);
}

static __exit void fake_remove_module(void)
{
	platform_driver_unregister(&ieee802154fake_driver);
	platform_device_unregister(ieee802154fake_dev);
}

module_init(fakelb_init_module);
module_exit(fake_remove_module);
MODULE_LICENSE("GPL");
