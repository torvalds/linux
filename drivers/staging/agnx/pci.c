/**
 * Airgo MIMO wireless driver
 *
 * Copyright (c) 2007 Li YanBo <dreamfly281@gmail.com>

 * Thanks for Jeff Williams <angelbane@gmail.com> do reverse engineer
 * works and published the SPECS at http://airgo.wdwconsulting.net/mymoin

 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/etherdevice.h>
#include <linux/pci.h>
#include <linux/delay.h>

#include "agnx.h"
#include "debug.h"
#include "xmit.h"
#include "phy.h"

MODULE_AUTHOR("Li YanBo <dreamfly281@gmail.com>");
MODULE_DESCRIPTION("Airgo MIMO PCI wireless driver");
MODULE_LICENSE("GPL");

static struct pci_device_id agnx_pci_id_tbl[] __devinitdata = {
	{ PCI_DEVICE(0x17cb, 0x0001) },	/* Beklin F5d8010, Netgear WGM511 etc */
	{ PCI_DEVICE(0x17cb, 0x0002) },	/* Netgear Wpnt511 */
	{ 0 }
};

MODULE_DEVICE_TABLE(pci, agnx_pci_id_tbl);


static inline void agnx_interrupt_ack(struct agnx_priv *priv, u32 *reason)
{
	void __iomem *ctl = priv->ctl;
	u32 reg;

	if ( *reason & AGNX_STAT_RX ) {
		/* Mark complete RX */
		reg = ioread32(ctl + AGNX_CIR_RXCTL);
		reg |= 0x4;
		iowrite32(reg, ctl + AGNX_CIR_RXCTL);
		/* disable Rx interrupt */
	}
	if ( *reason & AGNX_STAT_TX ) {
		reg = ioread32(ctl + AGNX_CIR_TXDCTL);
		if (reg & 0x4) {
			iowrite32(reg, ctl + AGNX_CIR_TXDCTL);
			*reason |= AGNX_STAT_TXD;
		}
 		reg = ioread32(ctl + AGNX_CIR_TXMCTL);
		if (reg & 0x4) {
			iowrite32(reg, ctl + AGNX_CIR_TXMCTL);
			*reason |= AGNX_STAT_TXM;
		}
	}
	if ( *reason & AGNX_STAT_X ) {
/* 		reg = ioread32(ctl + AGNX_INT_STAT); */
/* 		iowrite32(reg, ctl + AGNX_INT_STAT); */
/* 		/\* FIXME reinit interrupt mask *\/ */
/* 		reg = 0xc390bf9 & ~IRQ_TX_BEACON; */
/* 		reg &= ~IRQ_TX_DISABLE; */
/* 		iowrite32(reg, ctl + AGNX_INT_MASK); */
/* 		iowrite32(0x800, ctl + AGNX_CIR_BLKCTL); */
	}
} /* agnx_interrupt_ack */

static irqreturn_t agnx_interrupt_handler(int irq, void *dev_id)
{
	struct ieee80211_hw *dev = dev_id;
	struct agnx_priv *priv = dev->priv;
	void __iomem *ctl = priv->ctl;
	irqreturn_t ret = IRQ_NONE;
	u32 irq_reason;

	spin_lock(&priv->lock);

//	printk(KERN_ERR PFX "Get a interrupt %s\n", __func__);

	if (priv->init_status != AGNX_START)
		goto out;

	/* FiXME  Here has no lock, Is this will lead to race? */
	irq_reason = ioread32(ctl + AGNX_CIR_BLKCTL);
	if (!(irq_reason & 0x7))
		goto out;

	ret = IRQ_HANDLED;
	priv->irq_status = ioread32(ctl + AGNX_INT_STAT);

//	printk(PFX "Interrupt reason is 0x%x\n", irq_reason);
	/* Make sure the txm and txd flags don't conflict with other unknown
	   interrupt flag, maybe is not necessary */
	irq_reason &= 0xF;

	disable_rx_interrupt(priv);
	/* TODO Make sure the card finished initialized */
	agnx_interrupt_ack(priv, &irq_reason);

	if ( irq_reason & AGNX_STAT_RX )
		handle_rx_irq(priv);
	if ( irq_reason & AGNX_STAT_TXD )
		handle_txd_irq(priv);
	if ( irq_reason & AGNX_STAT_TXM )
		handle_txm_irq(priv);
	if ( irq_reason & AGNX_STAT_X )
		handle_other_irq(priv);

	enable_rx_interrupt(priv);
out:
	spin_unlock(&priv->lock);
	return ret;
} /* agnx_interrupt_handler */


/* FIXME */
static int agnx_tx(struct ieee80211_hw *dev, struct sk_buff *skb)
{
	AGNX_TRACE;
	return _agnx_tx(dev->priv, skb);
} /* agnx_tx */


static int agnx_get_mac_address(struct agnx_priv *priv)
{
	void __iomem *ctl = priv->ctl;
	u32 reg;
	AGNX_TRACE;

	/* Attention! directly read the MAC or other date from EEPROM will
	 lead to cardbus(WGM511) lock up when write to PM PLL register */
	reg = agnx_read32(ctl, 0x3544);
	udelay(40);
	reg = agnx_read32(ctl, 0x354c);
	udelay(50);
	/* Get the mac address */
	reg = agnx_read32(ctl, 0x3544);
	udelay(40);

	/* HACK */
	reg = cpu_to_le32(reg);
	priv->mac_addr[0] = ((u8 *)&reg)[2];
	priv->mac_addr[1] = ((u8 *)&reg)[3];
	reg = agnx_read32(ctl, 0x3548);
	udelay(50);
	*((u32 *)(priv->mac_addr + 2)) = cpu_to_le32(reg);

	if (!is_valid_ether_addr(priv->mac_addr)) {
		DECLARE_MAC_BUF(mbuf);
		printk(KERN_WARNING PFX "read mac %s\n", print_mac(mbuf, priv->mac_addr));
		printk(KERN_WARNING PFX "Invalid hwaddr! Using random hwaddr\n");
		random_ether_addr(priv->mac_addr);
	}

	return 0;
} /* agnx_get_mac_address */

static int agnx_alloc_rings(struct agnx_priv *priv)
{
	unsigned int len;
	AGNX_TRACE;

	/* Allocate RX/TXM/TXD rings info */
	priv->rx.size = AGNX_RX_RING_SIZE;
	priv->txm.size = AGNX_TXM_RING_SIZE;
	priv->txd.size = AGNX_TXD_RING_SIZE;

	len = priv->rx.size + priv->txm.size + priv->txd.size;

//	priv->rx.info = kzalloc(sizeof(struct agnx_info) * len, GFP_KERNEL);
	priv->rx.info = kzalloc(sizeof(struct agnx_info) * len, GFP_ATOMIC);
	if (!priv->rx.info)
		return -ENOMEM;
	priv->txm.info = priv->rx.info + priv->rx.size;
	priv->txd.info = priv->txm.info + priv->txm.size;

	/* Allocate RX/TXM/TXD descriptors */
	priv->rx.desc = pci_alloc_consistent(priv->pdev, sizeof(struct agnx_desc) * len,
					     &priv->rx.dma);
	if (!priv->rx.desc) {
		kfree(priv->rx.info);
		return -ENOMEM;
	}

	priv->txm.desc = priv->rx.desc + priv->rx.size;
	priv->txm.dma = priv->rx.dma + sizeof(struct agnx_desc) * priv->rx.size;
	priv->txd.desc = priv->txm.desc + priv->txm.size;
	priv->txd.dma = priv->txm.dma + sizeof(struct agnx_desc) * priv->txm.size;

	return 0;
} /* agnx_alloc_rings */

static void rings_free(struct agnx_priv *priv)
{
	unsigned int len = priv->rx.size + priv->txm.size + priv->txd.size;
	unsigned long flags;
	AGNX_TRACE;

	spin_lock_irqsave(&priv->lock, flags);
	kfree(priv->rx.info);
	pci_free_consistent(priv->pdev, sizeof(struct agnx_desc) * len,
			    priv->rx.desc, priv->rx.dma);
	spin_unlock_irqrestore(&priv->lock, flags);
}

#if 0
static void agnx_periodic_work_handler(struct work_struct *work)
{
	struct agnx_priv *priv = container_of(work, struct agnx_priv,
                                             periodic_work.work);
//	unsigned long flags;
	unsigned long delay;

	/* fixme: using mutex?? */
//	spin_lock_irqsave(&priv->lock, flags);

	/* TODO Recalibrate*/
//	calibrate_oscillator(priv);
//	antenna_calibrate(priv);
//	agnx_send_packet(priv, 997);
	/* FIXME */
/* 	if (debug == 3) */
/*                 delay = msecs_to_jiffies(AGNX_PERIODIC_DELAY); */
/* 	else */
	delay = msecs_to_jiffies(AGNX_PERIODIC_DELAY);
//		delay = round_jiffies(HZ * 15);

	queue_delayed_work(priv->hw->workqueue, &priv->periodic_work, delay);

//	spin_unlock_irqrestore(&priv->lock, flags);
}
#endif

static int agnx_start(struct ieee80211_hw *dev)
{
	struct agnx_priv *priv = dev->priv;
	/* unsigned long delay; */
	int err = 0;
	AGNX_TRACE;

	err = agnx_alloc_rings(priv);
	if (err) {
		printk(KERN_ERR PFX "Can't alloc RX/TXM/TXD rings\n");
		goto out;
	}
	err = request_irq(priv->pdev->irq, &agnx_interrupt_handler,
			  IRQF_SHARED, "agnx_pci", dev);
	if (err) {
		printk(KERN_ERR PFX "Failed to register IRQ handler\n");
		rings_free(priv);
		goto out;
	}

//	mdelay(500);

	might_sleep();
	agnx_hw_init(priv);

//	mdelay(500);
	might_sleep();

	priv->init_status = AGNX_START;
/*         INIT_DELAYED_WORK(&priv->periodic_work, agnx_periodic_work_handler); */
/* 	delay = msecs_to_jiffies(AGNX_PERIODIC_DELAY); */
/*         queue_delayed_work(priv->hw->workqueue, &priv->periodic_work, delay); */
out:
	return err;
} /* agnx_start */

static void agnx_stop(struct ieee80211_hw *dev)
{
	struct agnx_priv *priv = dev->priv;
	AGNX_TRACE;

	priv->init_status = AGNX_STOP;
	/* make sure hardware will not generate irq */
	agnx_hw_reset(priv);
	free_irq(priv->pdev->irq, dev);
        flush_workqueue(priv->hw->workqueue);
//	cancel_delayed_work_sync(&priv->periodic_work);
	unfill_rings(priv);
	rings_free(priv);
}

static int agnx_config(struct ieee80211_hw *dev,
		       struct ieee80211_conf *conf)
{
	struct agnx_priv *priv = dev->priv;
	int channel = ieee80211_frequency_to_channel(conf->channel->center_freq);
	AGNX_TRACE;

	spin_lock(&priv->lock);
	/* FIXME need priv lock? */
	if (channel != priv->channel) {
		priv->channel = channel;
		agnx_set_channel(priv, priv->channel);
	}

	spin_unlock(&priv->lock);
	return 0;
}

static int agnx_config_interface(struct ieee80211_hw *dev,
				 struct ieee80211_vif *vif,
				 struct ieee80211_if_conf *conf)
{
	struct agnx_priv *priv = dev->priv;
	void __iomem *ctl = priv->ctl;
	AGNX_TRACE;

	spin_lock(&priv->lock);

	if (memcmp(conf->bssid, priv->bssid, ETH_ALEN)) {
//		u32 reghi, reglo;
		agnx_set_bssid(priv, conf->bssid);
		memcpy(priv->bssid, conf->bssid, ETH_ALEN);
		hash_write(priv, conf->bssid, BSSID_STAID);
		sta_init(priv, BSSID_STAID);
		/* FIXME needed? */
		sta_power_init(priv, BSSID_STAID);
		agnx_write32(ctl, AGNX_BM_MTSM, 0xff & ~0x1);
	}
	spin_unlock(&priv->lock);
	return 0;
} /* agnx_config_interface */


static void agnx_configure_filter(struct ieee80211_hw *dev,
				  unsigned int changed_flags,
				  unsigned int *total_flags,
				  int mc_count, struct dev_mc_list *mclist)
{
	unsigned int new_flags = 0;

	*total_flags = new_flags;
	/* TODO */
}

static int agnx_add_interface(struct ieee80211_hw *dev,
			      struct ieee80211_if_init_conf *conf)
{
	struct agnx_priv *priv = dev->priv;
	AGNX_TRACE;

	spin_lock(&priv->lock);
	/* FIXME */
	if (priv->mode != NL80211_IFTYPE_MONITOR)
		return -EOPNOTSUPP;

	switch (conf->type) {
	case NL80211_IFTYPE_STATION:
		priv->mode = conf->type;
		break;
	default:
		return -EOPNOTSUPP;
	}

	spin_unlock(&priv->lock);

	return 0;
}

static void agnx_remove_interface(struct ieee80211_hw *dev,
				  struct ieee80211_if_init_conf *conf)
{
	struct agnx_priv *priv = dev->priv;
	AGNX_TRACE;

	/* TODO */
	priv->mode = NL80211_IFTYPE_MONITOR;
}

static int agnx_get_stats(struct ieee80211_hw *dev,
			  struct ieee80211_low_level_stats *stats)
{
	struct agnx_priv *priv = dev->priv;
	AGNX_TRACE;
	spin_lock(&priv->lock);
	/* TODO !! */
	memcpy(stats, &priv->stats, sizeof(*stats));
	spin_unlock(&priv->lock);

	return 0;
}

static u64 agnx_get_tsft(struct ieee80211_hw *dev)
{
	void __iomem *ctl = ((struct agnx_priv *)dev->priv)->ctl;
	u32 tsftl;
	u64 tsft;
	AGNX_TRACE;

	/* FIXME */
	tsftl = ioread32(ctl + AGNX_TXM_TIMESTAMPLO);
	tsft = ioread32(ctl + AGNX_TXM_TIMESTAMPHI);
	tsft <<= 32;
	tsft |= tsftl;

	return tsft;
}

static int agnx_get_tx_stats(struct ieee80211_hw *dev,
			     struct ieee80211_tx_queue_stats *stats)
{
	struct agnx_priv *priv = dev->priv;
	AGNX_TRACE;

	/* FIXME now we just using txd queue, but should using txm queue too */
	stats[0].len = (priv->txd.idx - priv->txd.idx_sent) / 2;
	stats[0].limit = priv->txd.size - 2;
	stats[0].count = priv->txd.idx / 2;

	return 0;
}

static struct ieee80211_ops agnx_ops = {
	.tx			= agnx_tx,
	.start			= agnx_start,
	.stop			= agnx_stop,
	.add_interface		= agnx_add_interface,
	.remove_interface	= agnx_remove_interface,
	.config			= agnx_config,
	.config_interface	= agnx_config_interface,
 	.configure_filter	= agnx_configure_filter,
	.get_stats		= agnx_get_stats,
	.get_tx_stats		= agnx_get_tx_stats,
	.get_tsf		= agnx_get_tsft
};

static void __devexit agnx_pci_remove(struct pci_dev *pdev)
{
	struct ieee80211_hw *dev = pci_get_drvdata(pdev);
	struct agnx_priv *priv = dev->priv;
	AGNX_TRACE;

	if (!dev)
		return;
	ieee80211_unregister_hw(dev);
	pci_iounmap(pdev, priv->ctl);
	pci_iounmap(pdev, priv->data);
	pci_release_regions(pdev);
	pci_disable_device(pdev);

	ieee80211_free_hw(dev);
}

static int __devinit agnx_pci_probe(struct pci_dev *pdev,
				    const struct pci_device_id *id)
{
	struct ieee80211_hw *dev;
	struct agnx_priv *priv;
	u32 mem_addr0, mem_len0;
	u32 mem_addr1, mem_len1;
	int err;
	DECLARE_MAC_BUF(mac);

	err = pci_enable_device(pdev);
	if (err) {
		printk(KERN_ERR PFX "Can't enable new PCI device\n");
		return err;
	}

	/* get pci resource */
	mem_addr0 = pci_resource_start(pdev, 0);
	mem_len0 = pci_resource_len(pdev, 0);
	mem_addr1 = pci_resource_start(pdev, 1);
	mem_len1 = pci_resource_len(pdev, 1);
	printk(KERN_DEBUG PFX "Memaddr0 is %x, length is %x\n", mem_addr0, mem_len0);
	printk(KERN_DEBUG PFX "Memaddr1 is %x, length is %x\n", mem_addr1, mem_len1);

	err = pci_request_regions(pdev, "agnx-pci");
	if (err) {
		printk(KERN_ERR PFX "Can't obtain PCI resource\n");
		return err;
	}

	if (pci_set_dma_mask(pdev, DMA_32BIT_MASK) ||
	    pci_set_consistent_dma_mask(pdev, DMA_32BIT_MASK)) {
		printk(KERN_ERR PFX "No suitable DMA available\n");
		goto err_free_reg;
	}

	pci_set_master(pdev);
	printk(KERN_DEBUG PFX "pdev->irq is %d\n", pdev->irq);

	dev = ieee80211_alloc_hw(sizeof(*priv), &agnx_ops);
	if (!dev) {
		printk(KERN_ERR PFX "ieee80211 alloc failed\n");
		err = -ENOMEM;
		goto err_free_reg;
	}
	/* init priv  */
	priv = dev->priv;
	memset(priv, 0, sizeof(*priv));
	priv->mode = NL80211_IFTYPE_MONITOR;
	priv->pdev = pdev;
	priv->hw = dev;
	spin_lock_init(&priv->lock);
	priv->init_status = AGNX_UNINIT;

	/* Map mem #1 and #2 */
	priv->ctl = pci_iomap(pdev, 0, mem_len0);
//	printk(KERN_DEBUG PFX"MEM1 mapped address is 0x%p\n", priv->ctl);
	if (!priv->ctl) {
		printk(KERN_ERR PFX "Can't map device memory\n");
		goto err_free_dev;
	}
	priv->data = pci_iomap(pdev, 1, mem_len1);
	printk(KERN_DEBUG PFX "MEM2 mapped address is 0x%p\n", priv->data);
	if (!priv->data) {
		printk(KERN_ERR PFX "Can't map device memory\n");
		goto err_iounmap2;
	}

	pci_read_config_byte(pdev, PCI_REVISION_ID, &priv->revid);

	priv->band.channels   = (struct ieee80211_channel *)agnx_channels;
	priv->band.n_channels = ARRAY_SIZE(agnx_channels);
	priv->band.bitrates   = (struct ieee80211_rate *)agnx_rates_80211g;
	priv->band.n_bitrates = ARRAY_SIZE(agnx_rates_80211g);

	/* Init ieee802.11 dev  */
	SET_IEEE80211_DEV(dev, &pdev->dev);
	pci_set_drvdata(pdev, dev);
	dev->extra_tx_headroom = sizeof(struct agnx_hdr);

	/* FIXME It only include FCS in promious mode but not manage mode */
/*      dev->flags =  IEEE80211_HW_RX_INCLUDES_FCS; */
	dev->channel_change_time = 5000;
	dev->max_signal = 100;
	/* FIXME */
	dev->queues = 1;

	agnx_get_mac_address(priv);

	SET_IEEE80211_PERM_ADDR(dev, priv->mac_addr);

/* 	/\* FIXME *\/ */
/* 	for (i = 1; i < NUM_DRIVE_MODES; i++) { */
/* 		err = ieee80211_register_hwmode(dev, &priv->modes[i]); */
/* 		if (err) { */
/* 			printk(KERN_ERR PFX "Can't register hwmode\n"); */
/* 			goto  err_iounmap; */
/* 		} */
/* 	} */

	priv->channel = 1;
	dev->wiphy->bands[IEEE80211_BAND_2GHZ] = &priv->band;

	err = ieee80211_register_hw(dev);
	if (err) {
		printk(KERN_ERR PFX "Can't register hardware\n");
		goto err_iounmap;
	}

	agnx_hw_reset(priv);


	printk(PFX "%s: hwaddr %s, Rev 0x%02x\n", wiphy_name(dev->wiphy),
	       print_mac(mac, dev->wiphy->perm_addr), priv->revid);
	return 0;

 err_iounmap:
	pci_iounmap(pdev, priv->data);

 err_iounmap2:
	pci_iounmap(pdev, priv->ctl);

 err_free_dev:
	pci_set_drvdata(pdev, NULL);
	ieee80211_free_hw(dev);

 err_free_reg:
	pci_release_regions(pdev);

	pci_disable_device(pdev);
	return err;
} /* agnx_pci_probe*/

#ifdef CONFIG_PM

static int agnx_pci_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct ieee80211_hw *dev = pci_get_drvdata(pdev);
	AGNX_TRACE;

	ieee80211_stop_queues(dev);
	agnx_stop(dev);

	pci_save_state(pdev);
	pci_set_power_state(pdev, pci_choose_state(pdev, state));
	return 0;
}

static int agnx_pci_resume(struct pci_dev *pdev)
{
	struct ieee80211_hw *dev = pci_get_drvdata(pdev);
	AGNX_TRACE;

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);

	agnx_start(dev);
	ieee80211_wake_queues(dev);

	return 0;
}

#else

#define agnx_pci_suspend NULL
#define agnx_pci_resume NULL

#endif /* CONFIG_PM */


static struct pci_driver agnx_pci_driver = {
	.name		= "agnx-pci",
	.id_table	= agnx_pci_id_tbl,
	.probe		= agnx_pci_probe,
	.remove		= __devexit_p(agnx_pci_remove),
	.suspend	= agnx_pci_suspend,
	.resume		= agnx_pci_resume,
};

static int __init agnx_pci_init(void)
{
	AGNX_TRACE;
	return pci_register_driver(&agnx_pci_driver);
}

static void __exit agnx_pci_exit(void)
{
	AGNX_TRACE;
	pci_unregister_driver(&agnx_pci_driver);
}


module_init(agnx_pci_init);
module_exit(agnx_pci_exit);
