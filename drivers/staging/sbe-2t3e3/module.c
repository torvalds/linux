/*
 * SBE 2T3E3 synchronous serial card driver for Linux
 *
 * Copyright (C) 2009-2010 Krzysztof Halasa <khc@pm.waw.pl>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
 *
 * This code is based on a driver written by SBE Inc.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/pci.h>
#include <linux/hdlc.h>
#include <linux/if_arp.h>
#include <linux/interrupt.h>
#include "2t3e3.h"

static void check_leds(unsigned long arg)
{
	struct card *card = (struct card *)arg;
	struct channel *channel0 = &card->channels[0];
	static int blinker;

	update_led(channel0, ++blinker);
	if (has_two_ports(channel0->pdev))
		update_led(&card->channels[1], blinker);

	card->timer.expires = jiffies + HZ / 10;
	add_timer(&card->timer);
}

static void t3e3_remove_channel(struct channel *channel)
{
	struct pci_dev *pdev = channel->pdev;
	struct net_device *dev = channel->dev;

	/* system hangs if board asserts irq while module is unloaded */
	cpld_stop_intr(channel);
	free_irq(dev->irq, dev);
	dc_drop_descriptor_list(channel);
	unregister_hdlc_device(dev);
	free_netdev(dev);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
}

static int __devinit t3e3_init_channel(struct channel *channel, struct pci_dev *pdev, struct card *card)
{
	struct net_device *dev;
	unsigned int val;
	int err;

	err = pci_enable_device(pdev);
	if (err)
		return err;

	err = pci_request_regions(pdev, "SBE 2T3E3");
	if (err)
		goto disable;

	dev = alloc_hdlcdev(channel);
	if (!dev) {
		printk(KERN_ERR "SBE 2T3E3" ": Out of memory\n");
		err = -ENOMEM;
		goto free_regions;
	}

	t3e3_sc_init(channel);
	dev_to_priv(dev) = channel;

	channel->pdev = pdev;
	channel->dev = dev;
	channel->card = card;
	channel->addr = pci_resource_start(pdev, 0);
	if (pdev->subsystem_device == PCI_SUBDEVICE_ID_SBE_2T3E3_P1)
		channel->h.slot = 1;
	else
		channel->h.slot = 0;

	err = setup_device(dev, channel);
	if (err)
		goto free_dev;

	pci_read_config_dword(channel->pdev, 0x40, &val); /* mask sleep mode */
	pci_write_config_dword(channel->pdev, 0x40, val & 0x3FFFFFFF);

	pci_read_config_byte(channel->pdev, PCI_CACHE_LINE_SIZE, &channel->h.cache_size);
	pci_read_config_dword(channel->pdev, PCI_COMMAND, &channel->h.command);
	t3e3_init(channel);

	err = request_irq(dev->irq, &t3e3_intr, IRQF_SHARED, dev->name, dev);
	if (err) {
		printk(KERN_WARNING "%s: could not get irq: %d\n", dev->name, dev->irq);
		goto unregister_dev;
	}

	pci_set_drvdata(pdev, channel);
	return 0;

unregister_dev:
	unregister_hdlc_device(dev);
free_dev:
	free_netdev(dev);
free_regions:
	pci_release_regions(pdev);
disable:
	pci_disable_device(pdev);
	return err;
}

static void __devexit t3e3_remove_card(struct pci_dev *pdev)
{
	struct channel *channel0 = pci_get_drvdata(pdev);
	struct card *card = channel0->card;

	del_timer(&card->timer);
	if (has_two_ports(channel0->pdev)) {
		t3e3_remove_channel(&card->channels[1]);
		pci_dev_put(card->channels[1].pdev);
	}
	t3e3_remove_channel(channel0);
	kfree(card);
}

static int __devinit t3e3_init_card(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	/* pdev points to channel #0 */
	struct pci_dev *pdev1 = NULL;
	struct card *card;
	int channels = 1, err;

	if (has_two_ports(pdev)) {
		while ((pdev1 = pci_get_subsys(PCI_VENDOR_ID_DEC, PCI_DEVICE_ID_DEC_21142,
					       PCI_VENDOR_ID_SBE, PCI_SUBDEVICE_ID_SBE_2T3E3_P1,
					       pdev1)))
			if (pdev1->bus == pdev->bus &&
			    pdev1->devfn == pdev->devfn + 8 /* next device on the same bus */)
				break; /* found the second channel */

		if (!pdev1) {
			printk(KERN_ERR "SBE 2T3E3" ": Can't find the second channel\n");
			return -EFAULT;
		}
		channels = 2;
		/* holds the reference for pdev1 */
	}

	card = kzalloc(sizeof(struct card) + channels * sizeof(struct channel), GFP_KERNEL);
	if (!card) {
		printk(KERN_ERR "SBE 2T3E3" ": Out of memory\n");
		return -ENOBUFS;
	}

	spin_lock_init(&card->bootrom_lock);
	card->bootrom_addr = pci_resource_start(pdev, 0);

	err = t3e3_init_channel(&card->channels[0], pdev, card);
	if (err)
		goto free_card;

	if (channels == 2) {
		err = t3e3_init_channel(&card->channels[1], pdev1, card);
		if (err) {
			t3e3_remove_channel(&card->channels[0]);
			goto free_card;
		}
	}

	/* start LED timer */
	init_timer(&card->timer);
	card->timer.function = check_leds;
	card->timer.expires = jiffies + HZ / 10;
	card->timer.data = (unsigned long)card;
	add_timer(&card->timer);
	return 0;

free_card:
	kfree(card);
	return err;
}

static struct pci_device_id t3e3_pci_tbl[] __devinitdata = {
	{ PCI_VENDOR_ID_DEC, PCI_DEVICE_ID_DEC_21142,
	  PCI_VENDOR_ID_SBE, PCI_SUBDEVICE_ID_SBE_T3E3, 0, 0, 0 },
	{ PCI_VENDOR_ID_DEC, PCI_DEVICE_ID_DEC_21142,
	  PCI_VENDOR_ID_SBE, PCI_SUBDEVICE_ID_SBE_2T3E3_P0, 0, 0, 0 },
	/* channel 1 will be initialized after channel 0 */
	{ 0, }
};

static struct pci_driver t3e3_pci_driver = {
	.name     = "SBE T3E3",
	.id_table = t3e3_pci_tbl,
	.probe    = t3e3_init_card,
	.remove   = t3e3_remove_card,
};

module_pci_driver(t3e3_pci_driver);
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, t3e3_pci_tbl);
