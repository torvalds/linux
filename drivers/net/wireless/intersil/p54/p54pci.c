// SPDX-License-Identifier: GPL-2.0-only

/*
 * Linux device driver for PCI based Prism54
 *
 * Copyright (c) 2006, Michael Wu <flamingice@sourmilk.net>
 * Copyright (c) 2008, Christian Lamparter <chunkeey@web.de>
 *
 * Based on the islsm (softmac prism54) driver, which is:
 * Copyright 2004-2006 Jean-Baptiste Note <jean-baptiste.note@m4x.org>, et al.
 */

#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/firmware.h>
#include <linux/etherdevice.h>
#include <linux/delay.h>
#include <linux/completion.h>
#include <linux/module.h>
#include <net/mac80211.h>

#include "p54.h"
#include "lmac.h"
#include "p54pci.h"

MODULE_AUTHOR("Michael Wu <flamingice@sourmilk.net>");
MODULE_DESCRIPTION("Prism54 PCI wireless driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("prism54pci");
MODULE_FIRMWARE("isl3886pci");

static const struct pci_device_id p54p_table[] = {
	/* Intersil PRISM Duette/Prism GT Wireless LAN adapter */
	{ PCI_DEVICE(0x1260, 0x3890) },
	/* 3COM 3CRWE154G72 Wireless LAN adapter */
	{ PCI_DEVICE(0x10b7, 0x6001) },
	/* Intersil PRISM Indigo Wireless LAN adapter */
	{ PCI_DEVICE(0x1260, 0x3877) },
	/* Intersil PRISM Javelin/Xbow Wireless LAN adapter */
	{ PCI_DEVICE(0x1260, 0x3886) },
	/* Intersil PRISM Xbow Wireless LAN adapter (Symbol AP-300) */
	{ PCI_DEVICE(0x1260, 0xffff) },
	{ },
};

MODULE_DEVICE_TABLE(pci, p54p_table);

static int p54p_upload_firmware(struct ieee80211_hw *dev)
{
	struct p54p_priv *priv = dev->priv;
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

	/* wait for the firmware to reset properly */
	mdelay(10);

	err = p54_parse_firmware(dev, priv->firmware);
	if (err)
		return err;

	if (priv->common.fw_interface != FW_LM86) {
		dev_err(&priv->pdev->dev, "wrong firmware, "
			"please get a LM86(PCI) firmware a try again.\n");
		return -EINVAL;
	}

	data = (__le32 *) priv->firmware->data;
	remains = priv->firmware->size;
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

	/* wait for the firmware to boot properly */
	mdelay(100);

	return 0;
}

static void p54p_refill_rx_ring(struct ieee80211_hw *dev,
	int ring_index, struct p54p_desc *ring, u32 ring_limit,
	struct sk_buff **rx_buf, u32 index)
{
	struct p54p_priv *priv = dev->priv;
	struct p54p_ring_control *ring_control = priv->ring_control;
	u32 limit, idx, i;

	idx = le32_to_cpu(ring_control->host_idx[ring_index]);
	limit = idx;
	limit -= index;
	limit = ring_limit - limit;

	i = idx % ring_limit;
	while (limit-- > 1) {
		struct p54p_desc *desc = &ring[i];

		if (!desc->host_addr) {
			struct sk_buff *skb;
			dma_addr_t mapping;
			skb = dev_alloc_skb(priv->common.rx_mtu + 32);
			if (!skb)
				break;

			mapping = dma_map_single(&priv->pdev->dev,
						 skb_tail_pointer(skb),
						 priv->common.rx_mtu + 32,
						 DMA_FROM_DEVICE);

			if (dma_mapping_error(&priv->pdev->dev, mapping)) {
				dev_kfree_skb_any(skb);
				dev_err(&priv->pdev->dev,
					"RX DMA Mapping error\n");
				break;
			}

			desc->host_addr = cpu_to_le32(mapping);
			desc->device_addr = 0;	// FIXME: necessary?
			desc->len = cpu_to_le16(priv->common.rx_mtu + 32);
			desc->flags = 0;
			rx_buf[i] = skb;
		}

		i++;
		idx++;
		i %= ring_limit;
	}

	wmb();
	ring_control->host_idx[ring_index] = cpu_to_le32(idx);
}

static void p54p_check_rx_ring(struct ieee80211_hw *dev, u32 *index,
	int ring_index, struct p54p_desc *ring, u32 ring_limit,
	struct sk_buff **rx_buf)
{
	struct p54p_priv *priv = dev->priv;
	struct p54p_ring_control *ring_control = priv->ring_control;
	struct p54p_desc *desc;
	u32 idx, i;

	i = (*index) % ring_limit;
	(*index) = idx = le32_to_cpu(ring_control->device_idx[ring_index]);
	idx %= ring_limit;
	while (i != idx) {
		u16 len;
		struct sk_buff *skb;
		dma_addr_t dma_addr;
		desc = &ring[i];
		len = le16_to_cpu(desc->len);
		skb = rx_buf[i];

		if (!skb) {
			i++;
			i %= ring_limit;
			continue;
		}

		if (unlikely(len > priv->common.rx_mtu)) {
			if (net_ratelimit())
				dev_err(&priv->pdev->dev, "rx'd frame size "
					"exceeds length threshold.\n");

			len = priv->common.rx_mtu;
		}
		dma_addr = le32_to_cpu(desc->host_addr);
		dma_sync_single_for_cpu(&priv->pdev->dev, dma_addr,
					priv->common.rx_mtu + 32,
					DMA_FROM_DEVICE);
		skb_put(skb, len);

		if (p54_rx(dev, skb)) {
			dma_unmap_single(&priv->pdev->dev, dma_addr,
					 priv->common.rx_mtu + 32,
					 DMA_FROM_DEVICE);
			rx_buf[i] = NULL;
			desc->host_addr = cpu_to_le32(0);
		} else {
			skb_trim(skb, 0);
			dma_sync_single_for_device(&priv->pdev->dev, dma_addr,
						   priv->common.rx_mtu + 32,
						   DMA_FROM_DEVICE);
			desc->len = cpu_to_le16(priv->common.rx_mtu + 32);
		}

		i++;
		i %= ring_limit;
	}

	p54p_refill_rx_ring(dev, ring_index, ring, ring_limit, rx_buf, *index);
}

static void p54p_check_tx_ring(struct ieee80211_hw *dev, u32 *index,
	int ring_index, struct p54p_desc *ring, u32 ring_limit,
	struct sk_buff **tx_buf)
{
	struct p54p_priv *priv = dev->priv;
	struct p54p_ring_control *ring_control = priv->ring_control;
	struct p54p_desc *desc;
	struct sk_buff *skb;
	u32 idx, i;

	i = (*index) % ring_limit;
	(*index) = idx = le32_to_cpu(ring_control->device_idx[ring_index]);
	idx %= ring_limit;

	while (i != idx) {
		desc = &ring[i];

		skb = tx_buf[i];
		tx_buf[i] = NULL;

		dma_unmap_single(&priv->pdev->dev,
				 le32_to_cpu(desc->host_addr),
				 le16_to_cpu(desc->len), DMA_TO_DEVICE);

		desc->host_addr = 0;
		desc->device_addr = 0;
		desc->len = 0;
		desc->flags = 0;

		if (skb && FREE_AFTER_TX(skb))
			p54_free_skb(dev, skb);

		i++;
		i %= ring_limit;
	}
}

static void p54p_tasklet(struct tasklet_struct *t)
{
	struct p54p_priv *priv = from_tasklet(priv, t, tasklet);
	struct ieee80211_hw *dev = pci_get_drvdata(priv->pdev);
	struct p54p_ring_control *ring_control = priv->ring_control;

	p54p_check_tx_ring(dev, &priv->tx_idx_mgmt, 3, ring_control->tx_mgmt,
			   ARRAY_SIZE(ring_control->tx_mgmt),
			   priv->tx_buf_mgmt);

	p54p_check_tx_ring(dev, &priv->tx_idx_data, 1, ring_control->tx_data,
			   ARRAY_SIZE(ring_control->tx_data),
			   priv->tx_buf_data);

	p54p_check_rx_ring(dev, &priv->rx_idx_mgmt, 2, ring_control->rx_mgmt,
		ARRAY_SIZE(ring_control->rx_mgmt), priv->rx_buf_mgmt);

	p54p_check_rx_ring(dev, &priv->rx_idx_data, 0, ring_control->rx_data,
		ARRAY_SIZE(ring_control->rx_data), priv->rx_buf_data);

	wmb();
	P54P_WRITE(dev_int, cpu_to_le32(ISL38XX_DEV_INT_UPDATE));
}

static irqreturn_t p54p_interrupt(int irq, void *dev_id)
{
	struct ieee80211_hw *dev = dev_id;
	struct p54p_priv *priv = dev->priv;
	__le32 reg;

	reg = P54P_READ(int_ident);
	if (unlikely(reg == cpu_to_le32(0xFFFFFFFF))) {
		goto out;
	}
	P54P_WRITE(int_ack, reg);

	reg &= P54P_READ(int_enable);

	if (reg & cpu_to_le32(ISL38XX_INT_IDENT_UPDATE))
		tasklet_schedule(&priv->tasklet);
	else if (reg & cpu_to_le32(ISL38XX_INT_IDENT_INIT))
		complete(&priv->boot_comp);

out:
	return reg ? IRQ_HANDLED : IRQ_NONE;
}

static void p54p_tx(struct ieee80211_hw *dev, struct sk_buff *skb)
{
	unsigned long flags;
	struct p54p_priv *priv = dev->priv;
	struct p54p_ring_control *ring_control = priv->ring_control;
	struct p54p_desc *desc;
	dma_addr_t mapping;
	u32 idx, i;
	__le32 device_addr;

	spin_lock_irqsave(&priv->lock, flags);
	idx = le32_to_cpu(ring_control->host_idx[1]);
	i = idx % ARRAY_SIZE(ring_control->tx_data);
	device_addr = ((struct p54_hdr *)skb->data)->req_id;

	mapping = dma_map_single(&priv->pdev->dev, skb->data, skb->len,
				 DMA_TO_DEVICE);
	if (dma_mapping_error(&priv->pdev->dev, mapping)) {
		spin_unlock_irqrestore(&priv->lock, flags);
		p54_free_skb(dev, skb);
		dev_err(&priv->pdev->dev, "TX DMA mapping error\n");
		return ;
	}
	priv->tx_buf_data[i] = skb;

	desc = &ring_control->tx_data[i];
	desc->host_addr = cpu_to_le32(mapping);
	desc->device_addr = device_addr;
	desc->len = cpu_to_le16(skb->len);
	desc->flags = 0;

	wmb();
	ring_control->host_idx[1] = cpu_to_le32(idx + 1);
	spin_unlock_irqrestore(&priv->lock, flags);

	P54P_WRITE(dev_int, cpu_to_le32(ISL38XX_DEV_INT_UPDATE));
	P54P_READ(dev_int);
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

	tasklet_kill(&priv->tasklet);

	P54P_WRITE(dev_int, cpu_to_le32(ISL38XX_DEV_INT_RESET));

	for (i = 0; i < ARRAY_SIZE(priv->rx_buf_data); i++) {
		desc = &ring_control->rx_data[i];
		if (desc->host_addr)
			dma_unmap_single(&priv->pdev->dev,
					 le32_to_cpu(desc->host_addr),
					 priv->common.rx_mtu + 32,
					 DMA_FROM_DEVICE);
		kfree_skb(priv->rx_buf_data[i]);
		priv->rx_buf_data[i] = NULL;
	}

	for (i = 0; i < ARRAY_SIZE(priv->rx_buf_mgmt); i++) {
		desc = &ring_control->rx_mgmt[i];
		if (desc->host_addr)
			dma_unmap_single(&priv->pdev->dev,
					 le32_to_cpu(desc->host_addr),
					 priv->common.rx_mtu + 32,
					 DMA_FROM_DEVICE);
		kfree_skb(priv->rx_buf_mgmt[i]);
		priv->rx_buf_mgmt[i] = NULL;
	}

	for (i = 0; i < ARRAY_SIZE(priv->tx_buf_data); i++) {
		desc = &ring_control->tx_data[i];
		if (desc->host_addr)
			dma_unmap_single(&priv->pdev->dev,
					 le32_to_cpu(desc->host_addr),
					 le16_to_cpu(desc->len),
					 DMA_TO_DEVICE);

		p54_free_skb(dev, priv->tx_buf_data[i]);
		priv->tx_buf_data[i] = NULL;
	}

	for (i = 0; i < ARRAY_SIZE(priv->tx_buf_mgmt); i++) {
		desc = &ring_control->tx_mgmt[i];
		if (desc->host_addr)
			dma_unmap_single(&priv->pdev->dev,
					 le32_to_cpu(desc->host_addr),
					 le16_to_cpu(desc->len),
					 DMA_TO_DEVICE);

		p54_free_skb(dev, priv->tx_buf_mgmt[i]);
		priv->tx_buf_mgmt[i] = NULL;
	}

	memset(ring_control, 0, sizeof(*ring_control));
}

static int p54p_open(struct ieee80211_hw *dev)
{
	struct p54p_priv *priv = dev->priv;
	int err;
	long time_left;

	init_completion(&priv->boot_comp);
	err = request_irq(priv->pdev->irq, p54p_interrupt,
			  IRQF_SHARED, "p54pci", dev);
	if (err) {
		dev_err(&priv->pdev->dev, "failed to register IRQ handler\n");
		return err;
	}

	memset(priv->ring_control, 0, sizeof(*priv->ring_control));
	err = p54p_upload_firmware(dev);
	if (err) {
		free_irq(priv->pdev->irq, dev);
		return err;
	}
	priv->rx_idx_data = priv->tx_idx_data = 0;
	priv->rx_idx_mgmt = priv->tx_idx_mgmt = 0;

	p54p_refill_rx_ring(dev, 0, priv->ring_control->rx_data,
		ARRAY_SIZE(priv->ring_control->rx_data), priv->rx_buf_data, 0);

	p54p_refill_rx_ring(dev, 2, priv->ring_control->rx_mgmt,
		ARRAY_SIZE(priv->ring_control->rx_mgmt), priv->rx_buf_mgmt, 0);

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

	time_left = wait_for_completion_interruptible_timeout(
			&priv->boot_comp, HZ);
	if (time_left <= 0) {
		wiphy_err(dev->wiphy, "Cannot boot firmware!\n");
		p54p_stop(dev);
		return time_left ? -ERESTARTSYS : -ETIMEDOUT;
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

static void p54p_firmware_step2(const struct firmware *fw,
				void *context)
{
	struct p54p_priv *priv = context;
	struct ieee80211_hw *dev = priv->common.hw;
	struct pci_dev *pdev = priv->pdev;
	int err;

	if (!fw) {
		dev_err(&pdev->dev, "Cannot find firmware (isl3886pci)\n");
		err = -ENOENT;
		goto out;
	}

	priv->firmware = fw;

	err = p54p_open(dev);
	if (err)
		goto out;
	err = p54_read_eeprom(dev);
	p54p_stop(dev);
	if (err)
		goto out;

	err = p54_register_common(dev, &pdev->dev);
	if (err)
		goto out;

out:

	complete(&priv->fw_loaded);

	if (err) {
		struct device *parent = pdev->dev.parent;

		if (parent)
			device_lock(parent);

		/*
		 * This will indirectly result in a call to p54p_remove.
		 * Hence, we don't need to bother with freeing any
		 * allocated ressources at all.
		 */
		device_release_driver(&pdev->dev);

		if (parent)
			device_unlock(parent);
	}

	pci_dev_put(pdev);
}

static int p54p_probe(struct pci_dev *pdev,
				const struct pci_device_id *id)
{
	struct p54p_priv *priv;
	struct ieee80211_hw *dev;
	unsigned long mem_addr, mem_len;
	int err;

	pci_dev_get(pdev);
	err = pci_enable_device(pdev);
	if (err) {
		dev_err(&pdev->dev, "Cannot enable new PCI device\n");
		goto err_put;
	}

	mem_addr = pci_resource_start(pdev, 0);
	mem_len = pci_resource_len(pdev, 0);
	if (mem_len < sizeof(struct p54p_csr)) {
		dev_err(&pdev->dev, "Too short PCI resources\n");
		err = -ENODEV;
		goto err_disable_dev;
	}

	err = pci_request_regions(pdev, "p54pci");
	if (err) {
		dev_err(&pdev->dev, "Cannot obtain PCI resources\n");
		goto err_disable_dev;
	}

	err = dma_set_mask(&pdev->dev, DMA_BIT_MASK(32));
	if (!err)
		err = dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(32));
	if (err) {
		dev_err(&pdev->dev, "No suitable DMA available\n");
		goto err_free_reg;
	}

	pci_set_master(pdev);
	pci_try_set_mwi(pdev);

	pci_write_config_byte(pdev, 0x40, 0);
	pci_write_config_byte(pdev, 0x41, 0);

	dev = p54_init_common(sizeof(*priv));
	if (!dev) {
		dev_err(&pdev->dev, "ieee80211 alloc failed\n");
		err = -ENOMEM;
		goto err_free_reg;
	}

	priv = dev->priv;
	priv->pdev = pdev;

	init_completion(&priv->fw_loaded);
	SET_IEEE80211_DEV(dev, &pdev->dev);
	pci_set_drvdata(pdev, dev);

	priv->map = ioremap(mem_addr, mem_len);
	if (!priv->map) {
		dev_err(&pdev->dev, "Cannot map device memory\n");
		err = -ENOMEM;
		goto err_free_dev;
	}

	priv->ring_control = dma_alloc_coherent(&pdev->dev,
						sizeof(*priv->ring_control),
						&priv->ring_control_dma, GFP_KERNEL);
	if (!priv->ring_control) {
		dev_err(&pdev->dev, "Cannot allocate rings\n");
		err = -ENOMEM;
		goto err_iounmap;
	}
	priv->common.open = p54p_open;
	priv->common.stop = p54p_stop;
	priv->common.tx = p54p_tx;

	spin_lock_init(&priv->lock);
	tasklet_setup(&priv->tasklet, p54p_tasklet);

	err = request_firmware_nowait(THIS_MODULE, 1, "isl3886pci",
				      &priv->pdev->dev, GFP_KERNEL,
				      priv, p54p_firmware_step2);
	if (!err)
		return 0;

	dma_free_coherent(&pdev->dev, sizeof(*priv->ring_control),
			  priv->ring_control, priv->ring_control_dma);

 err_iounmap:
	iounmap(priv->map);

 err_free_dev:
	p54_free_common(dev);

 err_free_reg:
	pci_release_regions(pdev);
 err_disable_dev:
	pci_disable_device(pdev);
err_put:
	pci_dev_put(pdev);
	return err;
}

static void p54p_remove(struct pci_dev *pdev)
{
	struct ieee80211_hw *dev = pci_get_drvdata(pdev);
	struct p54p_priv *priv;

	if (!dev)
		return;

	priv = dev->priv;
	wait_for_completion(&priv->fw_loaded);
	p54_unregister_common(dev);
	release_firmware(priv->firmware);
	dma_free_coherent(&pdev->dev, sizeof(*priv->ring_control),
			  priv->ring_control, priv->ring_control_dma);
	iounmap(priv->map);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	p54_free_common(dev);
}

#ifdef CONFIG_PM_SLEEP
static int p54p_suspend(struct device *device)
{
	struct pci_dev *pdev = to_pci_dev(device);

	pci_save_state(pdev);
	pci_set_power_state(pdev, PCI_D3hot);
	pci_disable_device(pdev);
	return 0;
}

static int p54p_resume(struct device *device)
{
	struct pci_dev *pdev = to_pci_dev(device);
	int err;

	err = pci_reenable_device(pdev);
	if (err)
		return err;
	return pci_set_power_state(pdev, PCI_D0);
}

static SIMPLE_DEV_PM_OPS(p54pci_pm_ops, p54p_suspend, p54p_resume);

#define P54P_PM_OPS (&p54pci_pm_ops)
#else
#define P54P_PM_OPS (NULL)
#endif /* CONFIG_PM_SLEEP */

static struct pci_driver p54p_driver = {
	.name		= "p54pci",
	.id_table	= p54p_table,
	.probe		= p54p_probe,
	.remove		= p54p_remove,
	.driver.pm	= P54P_PM_OPS,
};

module_pci_driver(p54p_driver);
