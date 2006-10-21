/*
 *  tifm_7xx1.c - TI FlashMedia driver
 *
 *  Copyright (C) 2006 Alex Dubov <oakad@yahoo.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/tifm.h>
#include <linux/dma-mapping.h>

#define DRIVER_NAME "tifm_7xx1"
#define DRIVER_VERSION "0.6"

static void tifm_7xx1_eject(struct tifm_adapter *fm, struct tifm_dev *sock)
{
	int cnt;
	unsigned long flags;

	spin_lock_irqsave(&fm->lock, flags);
	if (!fm->inhibit_new_cards) {
		for (cnt = 0; cnt < fm->max_sockets; cnt++) {
			if (fm->sockets[cnt] == sock) {
				fm->remove_mask |= (1 << cnt);
				queue_work(fm->wq, &fm->media_remover);
				break;
			}
		}
	}
	spin_unlock_irqrestore(&fm->lock, flags);
}

static void tifm_7xx1_remove_media(void *adapter)
{
	struct tifm_adapter *fm = adapter;
	unsigned long flags;
	int cnt;
	struct tifm_dev *sock;

	if (!class_device_get(&fm->cdev))
		return;
	spin_lock_irqsave(&fm->lock, flags);
	for (cnt = 0; cnt < fm->max_sockets; cnt++) {
		if (fm->sockets[cnt] && (fm->remove_mask & (1 << cnt))) {
			printk(KERN_INFO DRIVER_NAME
			       ": demand removing card from socket %d\n", cnt);
			sock = fm->sockets[cnt];
			fm->sockets[cnt] = NULL;
			fm->remove_mask &= ~(1 << cnt);

			writel(0x0e00, sock->addr + SOCK_CONTROL);

			writel((TIFM_IRQ_FIFOMASK | TIFM_IRQ_CARDMASK) << cnt,
				fm->addr + FM_CLEAR_INTERRUPT_ENABLE);
			writel((TIFM_IRQ_FIFOMASK | TIFM_IRQ_CARDMASK) << cnt,
				fm->addr + FM_SET_INTERRUPT_ENABLE);

			spin_unlock_irqrestore(&fm->lock, flags);
			device_unregister(&sock->dev);
			spin_lock_irqsave(&fm->lock, flags);
		}
	}
	spin_unlock_irqrestore(&fm->lock, flags);
	class_device_put(&fm->cdev);
}

static irqreturn_t tifm_7xx1_isr(int irq, void *dev_id)
{
	struct tifm_adapter *fm = dev_id;
	unsigned int irq_status;
	unsigned int sock_irq_status, cnt;

	spin_lock(&fm->lock);
	irq_status = readl(fm->addr + FM_INTERRUPT_STATUS);
	if (irq_status == 0 || irq_status == (~0)) {
		spin_unlock(&fm->lock);
		return IRQ_NONE;
	}

	if (irq_status & TIFM_IRQ_ENABLE) {
		writel(TIFM_IRQ_ENABLE, fm->addr + FM_CLEAR_INTERRUPT_ENABLE);

		for (cnt = 0; cnt <  fm->max_sockets; cnt++) {
			sock_irq_status = (irq_status >> cnt) &
					(TIFM_IRQ_FIFOMASK | TIFM_IRQ_CARDMASK);

			if (fm->sockets[cnt]) {
				if (sock_irq_status &&
						fm->sockets[cnt]->signal_irq)
					sock_irq_status = fm->sockets[cnt]->
						signal_irq(fm->sockets[cnt],
							sock_irq_status);

				if (irq_status & (1 << cnt))
					fm->remove_mask |= 1 << cnt;
			} else {
				if (irq_status & (1 << cnt))
					fm->insert_mask |= 1 << cnt;
			}
		}
	}
	writel(irq_status, fm->addr + FM_INTERRUPT_STATUS);

	if (!fm->inhibit_new_cards) {
		if (!fm->remove_mask && !fm->insert_mask) {
			writel(TIFM_IRQ_ENABLE,
				fm->addr + FM_SET_INTERRUPT_ENABLE);
		} else {
			queue_work(fm->wq, &fm->media_remover);
			queue_work(fm->wq, &fm->media_inserter);
		}
	}

	spin_unlock(&fm->lock);
	return IRQ_HANDLED;
}

static tifm_media_id tifm_7xx1_toggle_sock_power(char __iomem *sock_addr, int is_x2)
{
	unsigned int s_state;
	int cnt;

	writel(0x0e00, sock_addr + SOCK_CONTROL);

	for (cnt = 0; cnt < 100; cnt++) {
		if (!(TIFM_SOCK_STATE_POWERED &
				readl(sock_addr + SOCK_PRESENT_STATE)))
			break;
		msleep(10);
	}

	s_state = readl(sock_addr + SOCK_PRESENT_STATE);
	if (!(TIFM_SOCK_STATE_OCCUPIED & s_state))
		return FM_NULL;

	if (is_x2) {
		writel((s_state & 7) | 0x0c00, sock_addr + SOCK_CONTROL);
	} else {
		// SmartMedia cards need extra 40 msec
		if (((readl(sock_addr + SOCK_PRESENT_STATE) >> 4) & 7) == 1)
			msleep(40);
		writel(readl(sock_addr + SOCK_CONTROL) | TIFM_CTRL_LED,
		       sock_addr + SOCK_CONTROL);
		msleep(10);
		writel((s_state & 0x7) | 0x0c00 | TIFM_CTRL_LED,
			sock_addr + SOCK_CONTROL);
	}

	for (cnt = 0; cnt < 100; cnt++) {
		if ((TIFM_SOCK_STATE_POWERED &
				readl(sock_addr + SOCK_PRESENT_STATE)))
			break;
		msleep(10);
	}

	if (!is_x2)
		writel(readl(sock_addr + SOCK_CONTROL) & (~TIFM_CTRL_LED),
		       sock_addr + SOCK_CONTROL);

	return (readl(sock_addr + SOCK_PRESENT_STATE) >> 4) & 7;
}

inline static char __iomem *
tifm_7xx1_sock_addr(char __iomem *base_addr, unsigned int sock_num)
{
	return base_addr + ((sock_num + 1) << 10);
}

static void tifm_7xx1_insert_media(void *adapter)
{
	struct tifm_adapter *fm = adapter;
	unsigned long flags;
	tifm_media_id media_id;
	char *card_name = "xx";
	int cnt, ok_to_register;
	unsigned int insert_mask;
	struct tifm_dev *new_sock = NULL;

	if (!class_device_get(&fm->cdev))
		return;
	spin_lock_irqsave(&fm->lock, flags);
	insert_mask = fm->insert_mask;
	fm->insert_mask = 0;
	if (fm->inhibit_new_cards) {
		spin_unlock_irqrestore(&fm->lock, flags);
		class_device_put(&fm->cdev);
		return;
	}
	spin_unlock_irqrestore(&fm->lock, flags);

	for (cnt = 0; cnt < fm->max_sockets; cnt++) {
		if (!(insert_mask & (1 << cnt)))
			continue;

		media_id = tifm_7xx1_toggle_sock_power(tifm_7xx1_sock_addr(fm->addr, cnt),
						       fm->max_sockets == 2);
		if (media_id) {
			ok_to_register = 0;
			new_sock = tifm_alloc_device(fm, cnt);
			if (new_sock) {
				new_sock->addr = tifm_7xx1_sock_addr(fm->addr,
									cnt);
				new_sock->media_id = media_id;
				switch (media_id) {
				case 1:
					card_name = "xd";
					break;
				case 2:
					card_name = "ms";
					break;
				case 3:
					card_name = "sd";
					break;
				default:
					break;
				}
				snprintf(new_sock->dev.bus_id, BUS_ID_SIZE,
					"tifm_%s%u:%u", card_name, fm->id, cnt);
				printk(KERN_INFO DRIVER_NAME
					": %s card detected in socket %d\n",
					card_name, cnt);
				spin_lock_irqsave(&fm->lock, flags);
				if (!fm->sockets[cnt]) {
					fm->sockets[cnt] = new_sock;
					ok_to_register = 1;
				}
				spin_unlock_irqrestore(&fm->lock, flags);
				if (!ok_to_register ||
					    device_register(&new_sock->dev)) {
					spin_lock_irqsave(&fm->lock, flags);
					fm->sockets[cnt] = NULL;
					spin_unlock_irqrestore(&fm->lock,
								flags);
					tifm_free_device(&new_sock->dev);
				}
			}
		}
		writel((TIFM_IRQ_FIFOMASK | TIFM_IRQ_CARDMASK) << cnt,
		       fm->addr + FM_CLEAR_INTERRUPT_ENABLE);
		writel((TIFM_IRQ_FIFOMASK | TIFM_IRQ_CARDMASK) << cnt,
		       fm->addr + FM_SET_INTERRUPT_ENABLE);
	}

	writel(TIFM_IRQ_ENABLE, fm->addr + FM_SET_INTERRUPT_ENABLE);
	class_device_put(&fm->cdev);
}

static int tifm_7xx1_suspend(struct pci_dev *dev, pm_message_t state)
{
	struct tifm_adapter *fm = pci_get_drvdata(dev);
	unsigned long flags;

	spin_lock_irqsave(&fm->lock, flags);
	fm->inhibit_new_cards = 1;
	fm->remove_mask = 0xf;
	fm->insert_mask = 0;
	writel(TIFM_IRQ_ENABLE, fm->addr + FM_CLEAR_INTERRUPT_ENABLE);
	spin_unlock_irqrestore(&fm->lock, flags);
	flush_workqueue(fm->wq);

	tifm_7xx1_remove_media(fm);

	pci_set_power_state(dev, PCI_D3hot);
        pci_disable_device(dev);
        pci_save_state(dev);
	return 0;
}

static int tifm_7xx1_resume(struct pci_dev *dev)
{
	struct tifm_adapter *fm = pci_get_drvdata(dev);
	unsigned long flags;

	pci_restore_state(dev);
        pci_enable_device(dev);
        pci_set_power_state(dev, PCI_D0);
        pci_set_master(dev);

	spin_lock_irqsave(&fm->lock, flags);
	fm->inhibit_new_cards = 0;
	writel(TIFM_IRQ_SETALL, fm->addr + FM_INTERRUPT_STATUS);
	writel(TIFM_IRQ_SETALL, fm->addr + FM_CLEAR_INTERRUPT_ENABLE);
	writel(TIFM_IRQ_ENABLE | TIFM_IRQ_SETALLSOCK,
		fm->addr + FM_SET_INTERRUPT_ENABLE);
	fm->insert_mask = 0xf;
	spin_unlock_irqrestore(&fm->lock, flags);
	return 0;
}

static int tifm_7xx1_probe(struct pci_dev *dev,
			const struct pci_device_id *dev_id)
{
	struct tifm_adapter *fm;
	int pci_dev_busy = 0;
	int rc;

	rc = pci_set_dma_mask(dev, DMA_32BIT_MASK);
	if (rc)
		return rc;

	rc = pci_enable_device(dev);
	if (rc)
		return rc;

	pci_set_master(dev);

	rc = pci_request_regions(dev, DRIVER_NAME);
	if (rc) {
		pci_dev_busy = 1;
		goto err_out;
	}

	pci_intx(dev, 1);

	fm = tifm_alloc_adapter();
	if (!fm) {
		rc = -ENOMEM;
		goto err_out_int;
	}

	fm->dev = &dev->dev;
	fm->max_sockets = (dev->device == 0x803B) ? 2 : 4;
	fm->sockets = kzalloc(sizeof(struct tifm_dev*) * fm->max_sockets,
				GFP_KERNEL);
	if (!fm->sockets)
		goto err_out_free;

	INIT_WORK(&fm->media_inserter, tifm_7xx1_insert_media, fm);
	INIT_WORK(&fm->media_remover, tifm_7xx1_remove_media, fm);
	fm->eject = tifm_7xx1_eject;
	pci_set_drvdata(dev, fm);

	fm->addr = ioremap(pci_resource_start(dev, 0),
				pci_resource_len(dev, 0));
	if (!fm->addr)
		goto err_out_free;

	rc = request_irq(dev->irq, tifm_7xx1_isr, SA_SHIRQ, DRIVER_NAME, fm);
	if (rc)
		goto err_out_unmap;

	rc = tifm_add_adapter(fm);
	if (rc)
		goto err_out_irq;

	writel(TIFM_IRQ_SETALL, fm->addr + FM_CLEAR_INTERRUPT_ENABLE);
	writel(TIFM_IRQ_ENABLE | TIFM_IRQ_SETALLSOCK,
		fm->addr + FM_SET_INTERRUPT_ENABLE);

	fm->insert_mask = 0xf;

	return 0;

err_out_irq:
	free_irq(dev->irq, fm);
err_out_unmap:
	iounmap(fm->addr);
err_out_free:
	pci_set_drvdata(dev, NULL);
	tifm_free_adapter(fm);
err_out_int:
	pci_intx(dev, 0);
	pci_release_regions(dev);
err_out:
	if (!pci_dev_busy)
		pci_disable_device(dev);
	return rc;
}

static void tifm_7xx1_remove(struct pci_dev *dev)
{
	struct tifm_adapter *fm = pci_get_drvdata(dev);
	unsigned long flags;

	spin_lock_irqsave(&fm->lock, flags);
	fm->inhibit_new_cards = 1;
	fm->remove_mask = 0xf;
	fm->insert_mask = 0;
	writel(TIFM_IRQ_ENABLE, fm->addr + FM_CLEAR_INTERRUPT_ENABLE);
	spin_unlock_irqrestore(&fm->lock, flags);

	flush_workqueue(fm->wq);

	tifm_7xx1_remove_media(fm);

	writel(TIFM_IRQ_SETALL, fm->addr + FM_CLEAR_INTERRUPT_ENABLE);
	free_irq(dev->irq, fm);

	tifm_remove_adapter(fm);

	pci_set_drvdata(dev, NULL);

	iounmap(fm->addr);
	pci_intx(dev, 0);
	pci_release_regions(dev);

	pci_disable_device(dev);
	tifm_free_adapter(fm);
}

static struct pci_device_id tifm_7xx1_pci_tbl [] = {
	{ PCI_VENDOR_ID_TI, 0x8033, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	  0 }, /* xx21 - the one I have */
        { PCI_VENDOR_ID_TI, 0x803B, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	  0 }, /* xx12 - should be also supported */
	{ }
};

static struct pci_driver tifm_7xx1_driver = {
	.name = DRIVER_NAME,
	.id_table = tifm_7xx1_pci_tbl,
	.probe = tifm_7xx1_probe,
	.remove = tifm_7xx1_remove,
	.suspend = tifm_7xx1_suspend,
	.resume = tifm_7xx1_resume,
};

static int __init tifm_7xx1_init(void)
{
	return pci_register_driver(&tifm_7xx1_driver);
}

static void __exit tifm_7xx1_exit(void)
{
	pci_unregister_driver(&tifm_7xx1_driver);
}

MODULE_AUTHOR("Alex Dubov");
MODULE_DESCRIPTION("TI FlashMedia host driver");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, tifm_7xx1_pci_tbl);
MODULE_VERSION(DRIVER_VERSION);

module_init(tifm_7xx1_init);
module_exit(tifm_7xx1_exit);
