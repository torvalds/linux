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
#define DRIVER_VERSION "0.8"

#define TIFM_IRQ_ENABLE           0x80000000
#define TIFM_IRQ_SOCKMASK(x)      (x)
#define TIFM_IRQ_CARDMASK(x)      ((x) << 8)
#define TIFM_IRQ_FIFOMASK(x)      ((x) << 16)
#define TIFM_IRQ_SETALL           0xffffffff

static void tifm_7xx1_dummy_eject(struct tifm_adapter *fm,
				  struct tifm_dev *sock)
{
}

static void tifm_7xx1_eject(struct tifm_adapter *fm, struct tifm_dev *sock)
{
	unsigned long flags;

	spin_lock_irqsave(&fm->lock, flags);
	fm->socket_change_set |= 1 << sock->socket_id;
	tifm_queue_work(&fm->media_switcher);
	spin_unlock_irqrestore(&fm->lock, flags);
}

static irqreturn_t tifm_7xx1_isr(int irq, void *dev_id)
{
	struct tifm_adapter *fm = dev_id;
	struct tifm_dev *sock;
	unsigned int irq_status, cnt;

	spin_lock(&fm->lock);
	irq_status = readl(fm->addr + FM_INTERRUPT_STATUS);
	if (irq_status == 0 || irq_status == (~0)) {
		spin_unlock(&fm->lock);
		return IRQ_NONE;
	}

	if (irq_status & TIFM_IRQ_ENABLE) {
		writel(TIFM_IRQ_ENABLE, fm->addr + FM_CLEAR_INTERRUPT_ENABLE);

		for (cnt = 0; cnt < fm->num_sockets; cnt++) {
			sock = fm->sockets[cnt];
			if (sock) {
				if ((irq_status >> cnt) & TIFM_IRQ_FIFOMASK(1))
					sock->data_event(sock);
				if ((irq_status >> cnt) & TIFM_IRQ_CARDMASK(1))
					sock->card_event(sock);
			}
		}

		fm->socket_change_set |= irq_status
					 & ((1 << fm->num_sockets) - 1);
	}
	writel(irq_status, fm->addr + FM_INTERRUPT_STATUS);

	if (fm->finish_me)
		complete_all(fm->finish_me);
	else if (!fm->socket_change_set)
		writel(TIFM_IRQ_ENABLE, fm->addr + FM_SET_INTERRUPT_ENABLE);
	else
		tifm_queue_work(&fm->media_switcher);

	spin_unlock(&fm->lock);
	return IRQ_HANDLED;
}

static unsigned char tifm_7xx1_toggle_sock_power(char __iomem *sock_addr)
{
	unsigned int s_state;
	int cnt;

	writel(0x0e00, sock_addr + SOCK_CONTROL);

	for (cnt = 16; cnt <= 256; cnt <<= 1) {
		if (!(TIFM_SOCK_STATE_POWERED
		      & readl(sock_addr + SOCK_PRESENT_STATE)))
			break;

		msleep(cnt);
	}

	s_state = readl(sock_addr + SOCK_PRESENT_STATE);
	if (!(TIFM_SOCK_STATE_OCCUPIED & s_state))
		return 0;

	writel(readl(sock_addr + SOCK_CONTROL) | TIFM_CTRL_LED,
	       sock_addr + SOCK_CONTROL);

	/* xd needs some extra time before power on */
	if (((readl(sock_addr + SOCK_PRESENT_STATE) >> 4) & 7)
	    == TIFM_TYPE_XD)
		msleep(40);

	writel((s_state & TIFM_CTRL_POWER_MASK) | 0x0c00,
	       sock_addr + SOCK_CONTROL);
	/* wait for power to stabilize */
	msleep(20);
	for (cnt = 16; cnt <= 256; cnt <<= 1) {
		if ((TIFM_SOCK_STATE_POWERED
		     & readl(sock_addr + SOCK_PRESENT_STATE)))
			break;

		msleep(cnt);
	}

	writel(readl(sock_addr + SOCK_CONTROL) & (~TIFM_CTRL_LED),
	       sock_addr + SOCK_CONTROL);

	return (readl(sock_addr + SOCK_PRESENT_STATE) >> 4) & 7;
}

inline static void tifm_7xx1_sock_power_off(char __iomem *sock_addr)
{
	writel((~TIFM_CTRL_POWER_MASK) & readl(sock_addr + SOCK_CONTROL),
	       sock_addr + SOCK_CONTROL);
}

inline static char __iomem *
tifm_7xx1_sock_addr(char __iomem *base_addr, unsigned int sock_num)
{
	return base_addr + ((sock_num + 1) << 10);
}

static void tifm_7xx1_switch_media(struct work_struct *work)
{
	struct tifm_adapter *fm = container_of(work, struct tifm_adapter,
					       media_switcher);
	struct tifm_dev *sock;
	char __iomem *sock_addr;
	unsigned long flags;
	unsigned char media_id;
	unsigned int socket_change_set, cnt;

	spin_lock_irqsave(&fm->lock, flags);
	socket_change_set = fm->socket_change_set;
	fm->socket_change_set = 0;

	dev_dbg(fm->cdev.dev, "checking media set %x\n",
		socket_change_set);

	if (!socket_change_set) {
		spin_unlock_irqrestore(&fm->lock, flags);
		return;
	}

	for (cnt = 0; cnt < fm->num_sockets; cnt++) {
		if (!(socket_change_set & (1 << cnt)))
			continue;
		sock = fm->sockets[cnt];
		if (sock) {
			printk(KERN_INFO
			       "%s : demand removing card from socket %u:%u\n",
			       fm->cdev.class_id, fm->id, cnt);
			fm->sockets[cnt] = NULL;
			sock_addr = sock->addr;
			spin_unlock_irqrestore(&fm->lock, flags);
			device_unregister(&sock->dev);
			spin_lock_irqsave(&fm->lock, flags);
			tifm_7xx1_sock_power_off(sock_addr);
			writel(0x0e00, sock_addr + SOCK_CONTROL);
		}

		spin_unlock_irqrestore(&fm->lock, flags);

		media_id = tifm_7xx1_toggle_sock_power(
				tifm_7xx1_sock_addr(fm->addr, cnt));

		// tifm_alloc_device will check if media_id is valid
		sock = tifm_alloc_device(fm, cnt, media_id);
		if (sock) {
			sock->addr = tifm_7xx1_sock_addr(fm->addr, cnt);

			if (!device_register(&sock->dev)) {
				spin_lock_irqsave(&fm->lock, flags);
				if (!fm->sockets[cnt]) {
					fm->sockets[cnt] = sock;
					sock = NULL;
				}
				spin_unlock_irqrestore(&fm->lock, flags);
			}
			if (sock)
				tifm_free_device(&sock->dev);
		}
		spin_lock_irqsave(&fm->lock, flags);
	}

	writel(TIFM_IRQ_FIFOMASK(socket_change_set)
	       | TIFM_IRQ_CARDMASK(socket_change_set),
	       fm->addr + FM_CLEAR_INTERRUPT_ENABLE);

	writel(TIFM_IRQ_FIFOMASK(socket_change_set)
	       | TIFM_IRQ_CARDMASK(socket_change_set),
	       fm->addr + FM_SET_INTERRUPT_ENABLE);

	writel(TIFM_IRQ_ENABLE, fm->addr + FM_SET_INTERRUPT_ENABLE);
	spin_unlock_irqrestore(&fm->lock, flags);
}

#ifdef CONFIG_PM

static int tifm_7xx1_suspend(struct pci_dev *dev, pm_message_t state)
{
	struct tifm_adapter *fm = pci_get_drvdata(dev);
	int cnt;

	dev_dbg(&dev->dev, "suspending host\n");

	for (cnt = 0; cnt < fm->num_sockets; cnt++) {
		if (fm->sockets[cnt])
			tifm_7xx1_sock_power_off(fm->sockets[cnt]->addr);
	}

	pci_save_state(dev);
	pci_enable_wake(dev, pci_choose_state(dev, state), 0);
	pci_disable_device(dev);
	pci_set_power_state(dev, pci_choose_state(dev, state));
	return 0;
}

static int tifm_7xx1_resume(struct pci_dev *dev)
{
	struct tifm_adapter *fm = pci_get_drvdata(dev);
	int rc;
	unsigned int good_sockets = 0, bad_sockets = 0;
	unsigned long flags;
	unsigned char new_ids[fm->num_sockets];
	DECLARE_COMPLETION_ONSTACK(finish_resume);

	pci_set_power_state(dev, PCI_D0);
	pci_restore_state(dev);
	rc = pci_enable_device(dev);
	if (rc)
		return rc;
	pci_set_master(dev);

	dev_dbg(&dev->dev, "resuming host\n");

	for (rc = 0; rc < fm->num_sockets; rc++)
		new_ids[rc] = tifm_7xx1_toggle_sock_power(
					tifm_7xx1_sock_addr(fm->addr, rc));
	spin_lock_irqsave(&fm->lock, flags);
	for (rc = 0; rc < fm->num_sockets; rc++) {
		if (fm->sockets[rc]) {
			if (fm->sockets[rc]->type == new_ids[rc])
				good_sockets |= 1 << rc;
			else
				bad_sockets |= 1 << rc;
		}
	}

	writel(TIFM_IRQ_ENABLE | TIFM_IRQ_SOCKMASK((1 << fm->num_sockets) - 1),
	       fm->addr + FM_SET_INTERRUPT_ENABLE);
	dev_dbg(&dev->dev, "change sets on resume: good %x, bad %x\n",
		good_sockets, bad_sockets);

	fm->socket_change_set = 0;
	if (good_sockets) {
		fm->finish_me = &finish_resume;
		spin_unlock_irqrestore(&fm->lock, flags);
		rc = wait_for_completion_timeout(&finish_resume, HZ);
		dev_dbg(&dev->dev, "wait returned %d\n", rc);
		writel(TIFM_IRQ_FIFOMASK(good_sockets)
		       | TIFM_IRQ_CARDMASK(good_sockets),
		       fm->addr + FM_CLEAR_INTERRUPT_ENABLE);
		writel(TIFM_IRQ_FIFOMASK(good_sockets)
		       | TIFM_IRQ_CARDMASK(good_sockets),
		       fm->addr + FM_SET_INTERRUPT_ENABLE);
		spin_lock_irqsave(&fm->lock, flags);
		fm->finish_me = NULL;
		fm->socket_change_set ^= good_sockets & fm->socket_change_set;
	}

	fm->socket_change_set |= bad_sockets;
	if (fm->socket_change_set)
		tifm_queue_work(&fm->media_switcher);

	spin_unlock_irqrestore(&fm->lock, flags);
	writel(TIFM_IRQ_ENABLE,
	       fm->addr + FM_SET_INTERRUPT_ENABLE);

	return 0;
}

#else

#define tifm_7xx1_suspend NULL
#define tifm_7xx1_resume NULL

#endif /* CONFIG_PM */

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

	fm = tifm_alloc_adapter(dev->device == PCI_DEVICE_ID_TI_XX21_XX11_FM
				? 4 : 2, &dev->dev);
	if (!fm) {
		rc = -ENOMEM;
		goto err_out_int;
	}

	INIT_WORK(&fm->media_switcher, tifm_7xx1_switch_media);
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

	writel(TIFM_IRQ_ENABLE | TIFM_IRQ_SOCKMASK((1 << fm->num_sockets) - 1),
	       fm->addr + FM_SET_INTERRUPT_ENABLE);
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
	int cnt;

	fm->eject = tifm_7xx1_dummy_eject;
	writel(TIFM_IRQ_SETALL, fm->addr + FM_CLEAR_INTERRUPT_ENABLE);
	mmiowb();
	free_irq(dev->irq, fm);

	tifm_remove_adapter(fm);

	for (cnt = 0; cnt < fm->num_sockets; cnt++)
		tifm_7xx1_sock_power_off(tifm_7xx1_sock_addr(fm->addr, cnt));

	pci_set_drvdata(dev, NULL);

	iounmap(fm->addr);
	pci_intx(dev, 0);
	pci_release_regions(dev);

	pci_disable_device(dev);
	tifm_free_adapter(fm);
}

static struct pci_device_id tifm_7xx1_pci_tbl [] = {
	{ PCI_VENDOR_ID_TI, PCI_DEVICE_ID_TI_XX21_XX11_FM, PCI_ANY_ID,
	  PCI_ANY_ID, 0, 0, 0 }, /* xx21 - the one I have */
        { PCI_VENDOR_ID_TI, PCI_DEVICE_ID_TI_XX12_FM, PCI_ANY_ID,
	  PCI_ANY_ID, 0, 0, 0 },
	{ PCI_VENDOR_ID_TI, PCI_DEVICE_ID_TI_XX20_FM, PCI_ANY_ID,
	  PCI_ANY_ID, 0, 0, 0 },
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
