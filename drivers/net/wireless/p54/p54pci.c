
/*
 * Linux device driver for PCI based Prism54
 *
 * Copyright (c) 2006, Michael Wu <flamingice@sourmilk.net>
 *
 * Based on the islsm (softmac prism54) driver, which is:
 * Copyright 2004-2006 Jean-Baptiste Note <jean-baptiste.note@m4x.org>, et al.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/pci.h>
#include <linux/firmware.h>
#include <linux/etherdevice.h>
#include <linux/delay.h>
#include <linux/completion.h>
#include <net/mac80211.h>

#include "p54.h"
#include "p54pci.h"

MODULE_AUTHOR("Michael Wu <flamingice@sourmilk.net>");
MODULE_DESCRIPTION("Prism54 PCI wireless driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("prism54pci");

static struct pci_device_id p54p_table[] __devinitdata = {
	/* Intersil PRISM Duette/Prism GT Wireless LAN adapter */
	{ PCI_DEVICE(0x1260, 0x3890) },
	/* 3COM 3CRWE154G72 Wireless LAN adapter */
	{ PCI_DEVICE(0x10b7, 0x6001) },
	/* Intersil PRISM Indigo Wireless LAN adapter */
	{ PCI_DEVICE(0x1260, 0x3877) },
	/* Intersil PRISM Javelin/Xbow Wireless LAN adapter */
	{ PCI_DEVICE(0x1260, 0x3886) },
	{ },
};

MODULE_DEVICE_TABLE(pci, p54p_table);

static int p54p_upload_firmware(struct ieee80211_hw *dev)
{
	struct p54p_priv *priv = dev->priv;
	const struct firmware *fw_entry = NULL;
	__le32 reg;
	int err;
	__le32 *data;
	u32 remains, left, device_addr;

	P54P_WRITE(int_enable, cpu_to_le32(0));
	P54P_READ(int_enable);
	udelay(10);

	reg = P54P_READ(ctrl_stat);
	reg &= cpu_to_le32(~ISL38XX_CTRL_STAT_RESET);
	reg &= cpu_to_le32(~ISL38XX_CTRL_STAT_RAMBOOT);
	P54P_WRITE(ctrl_stat, reg);
	P54P_READ(ctrl_stat);
	udelay(10);

	reg |= cpu_to_le32(ISL38XX_CTRL_STAT_RESET);
	P54P_WRITE(ctrl_stat, reg);
	wmb();
	udelay(10);

	reg &= cpu_to_le32(~ISL38XX_CTRL_STAT_RESET);
	P54P_WRITE(ctrl_stat, reg);
	wmb();

	mdelay(50);

	err = request_firmware(&fw_entry, "isl3886", &priv->pdev->dev);
	if (err) {
		printk(KERN_ERR "%s (prism54pci): cannot find firmware "
		       "(isl3886)\n", pci_name(priv->pdev));
		return err;
	}

	p54_parse_firmware(dev, fw_entry);

	data = (__le32 *) fw_entry->data;
	remains = fw_entry->size;
	device_addr = ISL38XX_DEV_FIRMWARE_ADDR;
	while (remains) {
		u32 i = 0;
		left = min((u32)0x1000, remains);
		P54P_WRITE(direct_mem_base, cpu_to_le32(device_addr));
		P54P_READ(int_enable);

		device_addr += 0x1000;
		while (i < left) {
			P54P_WRITE(direct_mem_win[i], *data++);
			i += sizeof(u32);
		}

		remains -= left;
		P54P_READ(int_enable);
	}

	release_firmware(fw_entry);

	reg = P54P_READ(ctrl_stat);
	reg &= cpu_to_le32(~ISL38XX_CTRL_STAT_CLKRUN);
	reg &= cpu_to_le32(~ISL38XX_CTRL_STAT_RESET);
	reg |= cpu_to_le32(ISL38XX_CTRL_STAT_RAMBOOT);
	P54P_WRITE(ctrl_stat, reg);
	P54P_READ(ctrl_stat);
	udelay(10);

	reg |= cpu_to_le32(ISL38XX_CTRL_STAT_RESET);
	P54P_WRITE(ctrl_stat, reg);
	wmb();
	udelay(10);

	reg &= cpu_to_le32(~ISL38XX_CTRL_STAT_RESET);
	P54P_WRITE(ctrl_stat, reg);
	wmb();
	udelay(10);

	return 0;
}

static irqreturn_t p54p_simple_interrupt(int irq, void *dev_id)
{
	struct p54p_priv *priv = (struct p54p_priv *) dev_id;
	__le32 reg;

	reg = P54P_READ(int_ident);
	P54P_WRITE(int_ack, reg);

	if (reg & P54P_READ(int_enable))
		complete(&priv->boot_comp);

	return IRQ_HANDLED;
}

static int p54p_read_eeprom(struct ieee80211_hw *dev)
{
	struct p54p_priv *priv = dev->priv;
	struct p54p_ring_control *ring_control = priv->ring_control;
	int err;
	struct p54_control_hdr *hdr;
	void *eeprom;
	dma_addr_t rx_mapping, tx_mapping;
	u16 alen;

	init_completion(&priv->boot_comp);
	err = request_irq(priv->pdev->irq, &p54p_simple_interrupt,
			  IRQF_SHARED, "prism54pci", priv);
	if (err) {
		printk(KERN_ERR "%s (prism54pci): failed to register IRQ handler\n",
		       pci_name(priv->pdev));
		return err;
	}

	eeprom = kmalloc(0x2010 + EEPROM_READBACK_LEN, GFP_KERNEL);
	if (!eeprom) {
		printk(KERN_ERR "%s (prism54pci): no memory for eeprom!\n",
		       pci_name(priv->pdev));
		err = -ENOMEM;
		goto out;
	}

	memset(ring_control, 0, sizeof(*ring_control));
	P54P_WRITE(ring_control_base, cpu_to_le32(priv->ring_control_dma));
	P54P_READ(ring_control_base);
	udelay(10);

	P54P_WRITE(int_enable, cpu_to_le32(ISL38XX_INT_IDENT_INIT));
	P54P_READ(int_enable);
	udelay(10);

	P54P_WRITE(dev_int, cpu_to_le32(ISL38XX_DEV_INT_RESET));

	if (!wait_for_completion_interruptible_timeout(&priv->boot_comp, HZ)) {
		printk(KERN_ERR "%s (prism54pci): Cannot boot firmware!\n",
		       pci_name(priv->pdev));
		err = -EINVAL;
		goto out;
	}

	P54P_WRITE(int_enable, cpu_to_le32(ISL38XX_INT_IDENT_UPDATE));
	P54P_READ(int_enable);

	hdr = eeprom + 0x2010;
	p54_fill_eeprom_readback(hdr);
	hdr->req_id = cpu_to_le32(priv->common.rx_start);

	rx_mapping = pci_map_single(priv->pdev, eeprom,
				    0x2010, PCI_DMA_FROMDEVICE);
	tx_mapping = pci_map_single(priv->pdev, (void *)hdr,
				    EEPROM_READBACK_LEN, PCI_DMA_TODEVICE);

	ring_control->rx_mgmt[0].host_addr = cpu_to_le32(rx_mapping);
	ring_control->rx_mgmt[0].len = cpu_to_le16(0x2010);
	ring_control->tx_data[0].host_addr = cpu_to_le32(tx_mapping);
	ring_control->tx_data[0].device_addr = hdr->req_id;
	ring_control->tx_data[0].len = cpu_to_le16(EEPROM_READBACK_LEN);

	ring_control->host_idx[2] = cpu_to_le32(1);
	ring_control->host_idx[1] = cpu_to_le32(1);

	wmb();
	mdelay(100);
	P54P_WRITE(dev_int, cpu_to_le32(ISL38XX_DEV_INT_UPDATE));

	wait_for_completion_interruptible_timeout(&priv->boot_comp, HZ);
	wait_for_completion_interruptible_timeout(&priv->boot_comp, HZ);

	pci_unmap_single(priv->pdev, tx_mapping,
			 EEPROM_READBACK_LEN, PCI_DMA_TODEVICE);
	pci_unmap_single(priv->pdev, rx_mapping,
			 0x2010, PCI_DMA_FROMDEVICE);

	alen = le16_to_cpu(ring_control->rx_mgmt[0].len);
	if (le32_to_cpu(ring_control->device_idx[2]) != 1 ||
	    alen < 0x10) {
		printk(KERN_ERR "%s (prism54pci): Cannot read eeprom!\n",
		       pci_name(priv->pdev));
		err = -EINVAL;
		goto out;
	}

	p54_parse_eeprom(dev, (u8 *)eeprom + 0x10, alen - 0x10);

 out:
	kfree(eeprom);
	P54P_WRITE(int_enable, cpu_to_le32(0));
	P54P_READ(int_enable);
	udelay(10);
	free_irq(priv->pdev->irq, priv);
	P54P_WRITE(dev_int, cpu_to_le32(ISL38XX_DEV_INT_RESET));
	return err;
}

static void p54p_refill_rx_ring(struct ieee80211_hw *dev)
{
	struct p54p_priv *priv = dev->priv;
	struct p54p_ring_control *ring_control = priv->ring_control;
	u32 limit, host_idx, idx;

	host_idx = le32_to_cpu(ring_control->host_idx[0]);
	limit = host_idx;
	limit -= le32_to_cpu(ring_control->device_idx[0]);
	limit = ARRAY_SIZE(ring_control->rx_data) - limit;

	idx = host_idx % ARRAY_SIZE(ring_control->rx_data);
	while (limit-- > 1) {
		struct p54p_desc *desc = &ring_control->rx_data[idx];

		if (!desc->host_addr) {
			struct sk_buff *skb;
			dma_addr_t mapping;
			skb = dev_alloc_skb(MAX_RX_SIZE);
			if (!skb)
				break;

			mapping = pci_map_single(priv->pdev,
						 skb_tail_pointer(skb),
						 MAX_RX_SIZE,
						 PCI_DMA_FROMDEVICE);
			desc->host_addr = cpu_to_le32(mapping);
			desc->device_addr = 0;	// FIXME: necessary?
			desc->len = cpu_to_le16(MAX_RX_SIZE);
			desc->flags = 0;
			priv->rx_buf[idx] = skb;
		}

		idx++;
		host_idx++;
		idx %= ARRAY_SIZE(ring_control->rx_data);
	}

	wmb();
	ring_control->host_idx[0] = cpu_to_le32(host_idx);
}

static irqreturn_t p54p_interrupt(int irq, void *dev_id)
{
	struct ieee80211_hw *dev = dev_id;
	struct p54p_priv *priv = dev->priv;
	struct p54p_ring_control *ring_control = priv->ring_control;
	__le32 reg;

	spin_lock(&priv->lock);
	reg = P54P_READ(int_ident);
	if (unlikely(reg == cpu_to_le32(0xFFFFFFFF))) {
		spin_unlock(&priv->lock);
		return IRQ_HANDLED;
	}

	P54P_WRITE(int_ack, reg);

	reg &= P54P_READ(int_enable);

	if (reg & cpu_to_le32(ISL38XX_INT_IDENT_UPDATE)) {
		struct p54p_desc *desc;
		u32 idx, i;
		i = priv->tx_idx;
		i %= ARRAY_SIZE(ring_control->tx_data);
		priv->tx_idx = idx = le32_to_cpu(ring_control->device_idx[1]);
		idx %= ARRAY_SIZE(ring_control->tx_data);

		while (i != idx) {
			desc = &ring_control->tx_data[i];
			if (priv->tx_buf[i]) {
				kfree(priv->tx_buf[i]);
				priv->tx_buf[i] = NULL;
			}

			pci_unmap_single(priv->pdev, le32_to_cpu(desc->host_addr),
					 le16_to_cpu(desc->len), PCI_DMA_TODEVICE);

			desc->host_addr = 0;
			desc->device_addr = 0;
			desc->len = 0;
			desc->flags = 0;

			i++;
			i %= ARRAY_SIZE(ring_control->tx_data);
		}

		i = priv->rx_idx;
		i %= ARRAY_SIZE(ring_control->rx_data);
		priv->rx_idx = idx = le32_to_cpu(ring_control->device_idx[0]);
		idx %= ARRAY_SIZE(ring_control->rx_data);
		while (i != idx) {
			u16 len;
			struct sk_buff *skb;
			desc = &ring_control->rx_data[i];
			len = le16_to_cpu(desc->len);
			skb = priv->rx_buf[i];

			skb_put(skb, len);

			if (p54_rx(dev, skb)) {
				pci_unmap_single(priv->pdev,
						 le32_to_cpu(desc->host_addr),
						 MAX_RX_SIZE, PCI_DMA_FROMDEVICE);

				priv->rx_buf[i] = NULL;
				desc->host_addr = 0;
			} else {
				skb_trim(skb, 0);
				desc->len = cpu_to_le16(MAX_RX_SIZE);
			}

			i++;
			i %= ARRAY_SIZE(ring_control->rx_data);
		}

		p54p_refill_rx_ring(dev);

		wmb();
		P54P_WRITE(dev_int, cpu_to_le32(ISL38XX_DEV_INT_UPDATE));
	} else if (reg & cpu_to_le32(ISL38XX_INT_IDENT_INIT))
		complete(&priv->boot_comp);

	spin_unlock(&priv->lock);

	return reg ? IRQ_HANDLED : IRQ_NONE;
}

static void p54p_tx(struct ieee80211_hw *dev, struct p54_control_hdr *data,
		    size_t len, int free_on_tx)
{
	struct p54p_priv *priv = dev->priv;
	struct p54p_ring_control *ring_control = priv->ring_control;
	unsigned long flags;
	struct p54p_desc *desc;
	dma_addr_t mapping;
	u32 device_idx, idx, i;

	spin_lock_irqsave(&priv->lock, flags);

	device_idx = le32_to_cpu(ring_control->device_idx[1]);
	idx = le32_to_cpu(ring_control->host_idx[1]);
	i = idx % ARRAY_SIZE(ring_control->tx_data);

	mapping = pci_map_single(priv->pdev, data, len, PCI_DMA_TODEVICE);
	desc = &ring_control->tx_data[i];
	desc->host_addr = cpu_to_le32(mapping);
	desc->device_addr = data->req_id;
	desc->len = cpu_to_le16(len);
	desc->flags = 0;

	wmb();
	ring_control->host_idx[1] = cpu_to_le32(idx + 1);

	if (free_on_tx)
		priv->tx_buf[i] = data;

	spin_unlock_irqrestore(&priv->lock, flags);

	P54P_WRITE(dev_int, cpu_to_le32(ISL38XX_DEV_INT_UPDATE));
	P54P_READ(dev_int);

	/* FIXME: unlikely to happen because the device usually runs out of
	   memory before we fill the ring up, but we can make it impossible */
	if (idx - device_idx > ARRAY_SIZE(ring_control->tx_data) - 2)
		printk(KERN_INFO "%s: tx overflow.\n", wiphy_name(dev->wiphy));
}

static int p54p_open(struct ieee80211_hw *dev)
{
	struct p54p_priv *priv = dev->priv;
	int err;

	init_completion(&priv->boot_comp);
	err = request_irq(priv->pdev->irq, &p54p_interrupt,
			  IRQF_SHARED, "prism54pci", dev);
	if (err) {
		printk(KERN_ERR "%s: failed to register IRQ handler\n",
		       wiphy_name(dev->wiphy));
		return err;
	}

	memset(priv->ring_control, 0, sizeof(*priv->ring_control));
	priv->rx_idx = priv->tx_idx = 0;
	p54p_refill_rx_ring(dev);

	p54p_upload_firmware(dev);

	P54P_WRITE(ring_control_base, cpu_to_le32(priv->ring_control_dma));
	P54P_READ(ring_control_base);
	wmb();
	udelay(10);

	P54P_WRITE(int_enable, cpu_to_le32(ISL38XX_INT_IDENT_INIT));
	P54P_READ(int_enable);
	wmb();
	udelay(10);

	P54P_WRITE(dev_int, cpu_to_le32(ISL38XX_DEV_INT_RESET));
	P54P_READ(dev_int);

	if (!wait_for_completion_interruptible_timeout(&priv->boot_comp, HZ)) {
		printk(KERN_ERR "%s: Cannot boot firmware!\n",
		       wiphy_name(dev->wiphy));
		free_irq(priv->pdev->irq, dev);
		return -ETIMEDOUT;
	}

	P54P_WRITE(int_enable, cpu_to_le32(ISL38XX_INT_IDENT_UPDATE));
	P54P_READ(int_enable);
	wmb();
	udelay(10);

	P54P_WRITE(dev_int, cpu_to_le32(ISL38XX_DEV_INT_UPDATE));
	P54P_READ(dev_int);
	wmb();
	udelay(10);

	return 0;
}

static void p54p_stop(struct ieee80211_hw *dev)
{
	struct p54p_priv *priv = dev->priv;
	struct p54p_ring_control *ring_control = priv->ring_control;
	unsigned int i;
	struct p54p_desc *desc;

	P54P_WRITE(int_enable, cpu_to_le32(0));
	P54P_READ(int_enable);
	udelay(10);

	free_irq(priv->pdev->irq, dev);

	P54P_WRITE(dev_int, cpu_to_le32(ISL38XX_DEV_INT_RESET));

	for (i = 0; i < ARRAY_SIZE(priv->rx_buf); i++) {
		desc = &ring_control->rx_data[i];
		if (desc->host_addr)
			pci_unmap_single(priv->pdev, le32_to_cpu(desc->host_addr),
					 MAX_RX_SIZE, PCI_DMA_FROMDEVICE);
		kfree_skb(priv->rx_buf[i]);
		priv->rx_buf[i] = NULL;
	}

	for (i = 0; i < ARRAY_SIZE(priv->tx_buf); i++) {
		desc = &ring_control->tx_data[i];
		if (desc->host_addr)
			pci_unmap_single(priv->pdev, le32_to_cpu(desc->host_addr),
					 le16_to_cpu(desc->len), PCI_DMA_TODEVICE);

		kfree(priv->tx_buf[i]);
		priv->tx_buf[i] = NULL;
	}

	memset(ring_control, 0, sizeof(ring_control));
}

static int __devinit p54p_probe(struct pci_dev *pdev,
				const struct pci_device_id *id)
{
	struct p54p_priv *priv;
	struct ieee80211_hw *dev;
	unsigned long mem_addr, mem_len;
	int err;
	DECLARE_MAC_BUF(mac);

	err = pci_enable_device(pdev);
	if (err) {
		printk(KERN_ERR "%s (prism54pci): Cannot enable new PCI device\n",
		       pci_name(pdev));
		return err;
	}

	mem_addr = pci_resource_start(pdev, 0);
	mem_len = pci_resource_len(pdev, 0);
	if (mem_len < sizeof(struct p54p_csr)) {
		printk(KERN_ERR "%s (prism54pci): Too short PCI resources\n",
		       pci_name(pdev));
		pci_disable_device(pdev);
		return err;
	}

	err = pci_request_regions(pdev, "prism54pci");
	if (err) {
		printk(KERN_ERR "%s (prism54pci): Cannot obtain PCI resources\n",
		       pci_name(pdev));
		return err;
	}

	if (pci_set_dma_mask(pdev, DMA_32BIT_MASK) ||
	    pci_set_consistent_dma_mask(pdev, DMA_32BIT_MASK)) {
		printk(KERN_ERR "%s (prism54pci): No suitable DMA available\n",
		       pci_name(pdev));
		goto err_free_reg;
	}

	pci_set_master(pdev);
	pci_try_set_mwi(pdev);

	pci_write_config_byte(pdev, 0x40, 0);
	pci_write_config_byte(pdev, 0x41, 0);

	dev = p54_init_common(sizeof(*priv));
	if (!dev) {
		printk(KERN_ERR "%s (prism54pci): ieee80211 alloc failed\n",
		       pci_name(pdev));
		err = -ENOMEM;
		goto err_free_reg;
	}

	priv = dev->priv;
	priv->pdev = pdev;

	SET_IEEE80211_DEV(dev, &pdev->dev);
	pci_set_drvdata(pdev, dev);

	priv->map = ioremap(mem_addr, mem_len);
	if (!priv->map) {
		printk(KERN_ERR "%s (prism54pci): Cannot map device memory\n",
		       pci_name(pdev));
		err = -EINVAL;	// TODO: use a better error code?
		goto err_free_dev;
	}

	priv->ring_control = pci_alloc_consistent(pdev, sizeof(*priv->ring_control),
						  &priv->ring_control_dma);
	if (!priv->ring_control) {
		printk(KERN_ERR "%s (prism54pci): Cannot allocate rings\n",
		       pci_name(pdev));
		err = -ENOMEM;
		goto err_iounmap;
	}
	memset(priv->ring_control, 0, sizeof(*priv->ring_control));

	err = p54p_upload_firmware(dev);
	if (err)
		goto err_free_desc;

	err = p54p_read_eeprom(dev);
	if (err)
		goto err_free_desc;

	priv->common.open = p54p_open;
	priv->common.stop = p54p_stop;
	priv->common.tx = p54p_tx;

	spin_lock_init(&priv->lock);

	err = ieee80211_register_hw(dev);
	if (err) {
		printk(KERN_ERR "%s (prism54pci): Cannot register netdevice\n",
		       pci_name(pdev));
		goto err_free_common;
	}

	printk(KERN_INFO "%s: hwaddr %s, isl38%02x\n",
	       wiphy_name(dev->wiphy),
	       print_mac(mac, dev->wiphy->perm_addr),
	       priv->common.version);

	return 0;

 err_free_common:
	p54_free_common(dev);

 err_free_desc:
	pci_free_consistent(pdev, sizeof(*priv->ring_control),
			    priv->ring_control, priv->ring_control_dma);

 err_iounmap:
	iounmap(priv->map);

 err_free_dev:
	pci_set_drvdata(pdev, NULL);
	ieee80211_free_hw(dev);

 err_free_reg:
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	return err;
}

static void __devexit p54p_remove(struct pci_dev *pdev)
{
	struct ieee80211_hw *dev = pci_get_drvdata(pdev);
	struct p54p_priv *priv;

	if (!dev)
		return;

	ieee80211_unregister_hw(dev);
	priv = dev->priv;
	pci_free_consistent(pdev, sizeof(*priv->ring_control),
			    priv->ring_control, priv->ring_control_dma);
	p54_free_common(dev);
	iounmap(priv->map);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	ieee80211_free_hw(dev);
}

#ifdef CONFIG_PM
static int p54p_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct ieee80211_hw *dev = pci_get_drvdata(pdev);
	struct p54p_priv *priv = dev->priv;

	if (priv->common.mode != IEEE80211_IF_TYPE_INVALID) {
		ieee80211_stop_queues(dev);
		p54p_stop(dev);
	}

	pci_save_state(pdev);
	pci_set_power_state(pdev, pci_choose_state(pdev, state));
	return 0;
}

static int p54p_resume(struct pci_dev *pdev)
{
	struct ieee80211_hw *dev = pci_get_drvdata(pdev);
	struct p54p_priv *priv = dev->priv;

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);

	if (priv->common.mode != IEEE80211_IF_TYPE_INVALID) {
		p54p_open(dev);
		ieee80211_start_queues(dev);
	}

	return 0;
}
#endif /* CONFIG_PM */

static struct pci_driver p54p_driver = {
	.name		= "prism54pci",
	.id_table	= p54p_table,
	.probe		= p54p_probe,
	.remove		= __devexit_p(p54p_remove),
#ifdef CONFIG_PM
	.suspend	= p54p_suspend,
	.resume		= p54p_resume,
#endif /* CONFIG_PM */
};

static int __init p54p_init(void)
{
	return pci_register_driver(&p54p_driver);
}

static void __exit p54p_exit(void)
{
	pci_unregister_driver(&p54p_driver);
}

module_init(p54p_init);
module_exit(p54p_exit);
