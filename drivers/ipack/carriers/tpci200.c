/**
 * tpci200.c
 *
 * driver for the TEWS TPCI-200 device
 *
 * Copyright (C) 2009-2012 CERN (www.cern.ch)
 * Author: Nicolas Serafini, EIC2 SA
 * Author: Samuel Iglesias Gonsalvez <siglesias@igalia.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; version 2 of the License.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include "tpci200.h"

static const u16 tpci200_status_timeout[] = {
	TPCI200_A_TIMEOUT,
	TPCI200_B_TIMEOUT,
	TPCI200_C_TIMEOUT,
	TPCI200_D_TIMEOUT,
};

static const u16 tpci200_status_error[] = {
	TPCI200_A_ERROR,
	TPCI200_B_ERROR,
	TPCI200_C_ERROR,
	TPCI200_D_ERROR,
};

static const size_t tpci200_space_size[IPACK_SPACE_COUNT] = {
	[IPACK_IO_SPACE]    = TPCI200_IO_SPACE_SIZE,
	[IPACK_ID_SPACE]    = TPCI200_ID_SPACE_SIZE,
	[IPACK_INT_SPACE]   = TPCI200_INT_SPACE_SIZE,
	[IPACK_MEM8_SPACE]  = TPCI200_MEM8_SPACE_SIZE,
	[IPACK_MEM16_SPACE] = TPCI200_MEM16_SPACE_SIZE,
};

static const size_t tpci200_space_interval[IPACK_SPACE_COUNT] = {
	[IPACK_IO_SPACE]    = TPCI200_IO_SPACE_INTERVAL,
	[IPACK_ID_SPACE]    = TPCI200_ID_SPACE_INTERVAL,
	[IPACK_INT_SPACE]   = TPCI200_INT_SPACE_INTERVAL,
	[IPACK_MEM8_SPACE]  = TPCI200_MEM8_SPACE_INTERVAL,
	[IPACK_MEM16_SPACE] = TPCI200_MEM16_SPACE_INTERVAL,
};

static struct tpci200_board *check_slot(struct ipack_device *dev)
{
	struct tpci200_board *tpci200;

	if (dev == NULL)
		return NULL;


	tpci200 = dev_get_drvdata(dev->bus->parent);

	if (tpci200 == NULL) {
		dev_info(&dev->dev, "carrier board not found\n");
		return NULL;
	}

	if (dev->slot >= TPCI200_NB_SLOT) {
		dev_info(&dev->dev,
			 "Slot [%d:%d] doesn't exist! Last tpci200 slot is %d.\n",
			 dev->bus->bus_nr, dev->slot, TPCI200_NB_SLOT-1);
		return NULL;
	}

	return tpci200;
}

static void tpci200_clear_mask(struct tpci200_board *tpci200,
			       __le16 __iomem *addr, u16 mask)
{
	unsigned long flags;
	spin_lock_irqsave(&tpci200->regs_lock, flags);
	iowrite16(ioread16(addr) & (~mask), addr);
	spin_unlock_irqrestore(&tpci200->regs_lock, flags);
}

static void tpci200_set_mask(struct tpci200_board *tpci200,
			     __le16 __iomem *addr, u16 mask)
{
	unsigned long flags;
	spin_lock_irqsave(&tpci200->regs_lock, flags);
	iowrite16(ioread16(addr) | mask, addr);
	spin_unlock_irqrestore(&tpci200->regs_lock, flags);
}

static void tpci200_unregister(struct tpci200_board *tpci200)
{
	free_irq(tpci200->info->pdev->irq, (void *) tpci200);

	pci_iounmap(tpci200->info->pdev, tpci200->info->interface_regs);
	pci_iounmap(tpci200->info->pdev, tpci200->info->cfg_regs);

	pci_release_region(tpci200->info->pdev, TPCI200_IP_INTERFACE_BAR);
	pci_release_region(tpci200->info->pdev, TPCI200_IO_ID_INT_SPACES_BAR);
	pci_release_region(tpci200->info->pdev, TPCI200_MEM16_SPACE_BAR);
	pci_release_region(tpci200->info->pdev, TPCI200_MEM8_SPACE_BAR);
	pci_release_region(tpci200->info->pdev, TPCI200_CFG_MEM_BAR);

	pci_disable_device(tpci200->info->pdev);
	pci_dev_put(tpci200->info->pdev);
}

static void tpci200_enable_irq(struct tpci200_board *tpci200,
			       int islot)
{
	tpci200_set_mask(tpci200,
			&tpci200->info->interface_regs->control[islot],
			TPCI200_INT0_EN | TPCI200_INT1_EN);
}

static void tpci200_disable_irq(struct tpci200_board *tpci200,
				int islot)
{
	tpci200_clear_mask(tpci200,
			&tpci200->info->interface_regs->control[islot],
			TPCI200_INT0_EN | TPCI200_INT1_EN);
}

static irqreturn_t tpci200_slot_irq(struct slot_irq *slot_irq)
{
	irqreturn_t ret;

	if (!slot_irq)
		return -ENODEV;
	ret = slot_irq->handler(slot_irq->arg);

	return ret;
}

static irqreturn_t tpci200_interrupt(int irq, void *dev_id)
{
	struct tpci200_board *tpci200 = (struct tpci200_board *) dev_id;
	struct slot_irq *slot_irq;
	irqreturn_t ret;
	u16 status_reg;
	int i;

	/* Read status register */
	status_reg = ioread16(&tpci200->info->interface_regs->status);

	/* Did we cause the interrupt? */
	if (!(status_reg & TPCI200_SLOT_INT_MASK))
		return IRQ_NONE;

	/* callback to the IRQ handler for the corresponding slot */
	rcu_read_lock();
	for (i = 0; i < TPCI200_NB_SLOT; i++) {
		if (!(status_reg & ((TPCI200_A_INT0 | TPCI200_A_INT1) << (2 * i))))
			continue;
		slot_irq = rcu_dereference(tpci200->slots[i].irq);
		ret = tpci200_slot_irq(slot_irq);
		if (ret == -ENODEV) {
			dev_info(&tpci200->info->pdev->dev,
				 "No registered ISR for slot [%d:%d]!. IRQ will be disabled.\n",
				 tpci200->number, i);
			tpci200_disable_irq(tpci200, i);
		}
	}
	rcu_read_unlock();

	return IRQ_HANDLED;
}

static int tpci200_free_irq(struct ipack_device *dev)
{
	struct slot_irq *slot_irq;
	struct tpci200_board *tpci200;

	tpci200 = check_slot(dev);
	if (tpci200 == NULL)
		return -EINVAL;

	if (mutex_lock_interruptible(&tpci200->mutex))
		return -ERESTARTSYS;

	if (tpci200->slots[dev->slot].irq == NULL) {
		mutex_unlock(&tpci200->mutex);
		return -EINVAL;
	}

	tpci200_disable_irq(tpci200, dev->slot);
	slot_irq = tpci200->slots[dev->slot].irq;
	/* uninstall handler */
	RCU_INIT_POINTER(tpci200->slots[dev->slot].irq, NULL);
	synchronize_rcu();
	kfree(slot_irq);
	mutex_unlock(&tpci200->mutex);
	return 0;
}

static int tpci200_request_irq(struct ipack_device *dev,
			       irqreturn_t (*handler)(void *), void *arg)
{
	int res = 0;
	struct slot_irq *slot_irq;
	struct tpci200_board *tpci200;

	tpci200 = check_slot(dev);
	if (tpci200 == NULL)
		return -EINVAL;

	if (mutex_lock_interruptible(&tpci200->mutex))
		return -ERESTARTSYS;

	if (tpci200->slots[dev->slot].irq != NULL) {
		dev_err(&dev->dev,
			"Slot [%d:%d] IRQ already registered !\n",
			dev->bus->bus_nr,
			dev->slot);
		res = -EINVAL;
		goto out_unlock;
	}

	slot_irq = kzalloc(sizeof(struct slot_irq), GFP_KERNEL);
	if (slot_irq == NULL) {
		dev_err(&dev->dev,
			"Slot [%d:%d] unable to allocate memory for IRQ !\n",
			dev->bus->bus_nr, dev->slot);
		res = -ENOMEM;
		goto out_unlock;
	}

	/*
	 * WARNING: Setup Interrupt Vector in the IndustryPack device
	 * before an IRQ request.
	 * Read the User Manual of your IndustryPack device to know
	 * where to write the vector in memory.
	 */
	slot_irq->handler = handler;
	slot_irq->arg = arg;
	slot_irq->holder = dev;

	rcu_assign_pointer(tpci200->slots[dev->slot].irq, slot_irq);
	tpci200_enable_irq(tpci200, dev->slot);

out_unlock:
	mutex_unlock(&tpci200->mutex);
	return res;
}

static int tpci200_register(struct tpci200_board *tpci200)
{
	int i;
	int res;
	phys_addr_t ioidint_base;
	unsigned short slot_ctrl;

	if (pci_enable_device(tpci200->info->pdev) < 0)
		return -ENODEV;

	/* Request IP interface register (Bar 2) */
	res = pci_request_region(tpci200->info->pdev, TPCI200_IP_INTERFACE_BAR,
				 "Carrier IP interface registers");
	if (res) {
		dev_err(&tpci200->info->pdev->dev,
			"(bn 0x%X, sn 0x%X) failed to allocate PCI resource for BAR 2 !",
			tpci200->info->pdev->bus->number,
			tpci200->info->pdev->devfn);
		goto out_disable_pci;
	}

	/* Request IO ID INT space (Bar 3) */
	res = pci_request_region(tpci200->info->pdev,
				 TPCI200_IO_ID_INT_SPACES_BAR,
				 "Carrier IO ID INT space");
	if (res) {
		dev_err(&tpci200->info->pdev->dev,
			"(bn 0x%X, sn 0x%X) failed to allocate PCI resource for BAR 3 !",
			tpci200->info->pdev->bus->number,
			tpci200->info->pdev->devfn);
		goto out_release_ip_space;
	}

	/* Request MEM8 space (Bar 5) */
	res = pci_request_region(tpci200->info->pdev, TPCI200_MEM8_SPACE_BAR,
				 "Carrier MEM8 space");
	if (res) {
		dev_err(&tpci200->info->pdev->dev,
			"(bn 0x%X, sn 0x%X) failed to allocate PCI resource for BAR 5!",
			tpci200->info->pdev->bus->number,
			tpci200->info->pdev->devfn);
		goto out_release_ioid_int_space;
	}

	/* Request MEM16 space (Bar 4) */
	res = pci_request_region(tpci200->info->pdev, TPCI200_MEM16_SPACE_BAR,
				 "Carrier MEM16 space");
	if (res) {
		dev_err(&tpci200->info->pdev->dev,
			"(bn 0x%X, sn 0x%X) failed to allocate PCI resource for BAR 4!",
			tpci200->info->pdev->bus->number,
			tpci200->info->pdev->devfn);
		goto out_release_mem8_space;
	}

	/* Map internal tpci200 driver user space */
	tpci200->info->interface_regs =
		ioremap_nocache(pci_resource_start(tpci200->info->pdev,
					   TPCI200_IP_INTERFACE_BAR),
			TPCI200_IFACE_SIZE);

	/* Initialize lock that protects interface_regs */
	spin_lock_init(&tpci200->regs_lock);

	ioidint_base = pci_resource_start(tpci200->info->pdev,
					  TPCI200_IO_ID_INT_SPACES_BAR);
	tpci200->mod_mem[IPACK_IO_SPACE] = ioidint_base + TPCI200_IO_SPACE_OFF;
	tpci200->mod_mem[IPACK_ID_SPACE] = ioidint_base + TPCI200_ID_SPACE_OFF;
	tpci200->mod_mem[IPACK_INT_SPACE] =
		ioidint_base + TPCI200_INT_SPACE_OFF;
	tpci200->mod_mem[IPACK_MEM8_SPACE] =
		pci_resource_start(tpci200->info->pdev,
				   TPCI200_MEM8_SPACE_BAR);
	tpci200->mod_mem[IPACK_MEM16_SPACE] =
		pci_resource_start(tpci200->info->pdev,
				   TPCI200_MEM16_SPACE_BAR);

	/* Set the default parameters of the slot
	 * INT0 disabled, level sensitive
	 * INT1 disabled, level sensitive
	 * error interrupt disabled
	 * timeout interrupt disabled
	 * recover time disabled
	 * clock rate 8 MHz
	 */
	slot_ctrl = 0;
	for (i = 0; i < TPCI200_NB_SLOT; i++)
		writew(slot_ctrl, &tpci200->info->interface_regs->control[i]);

	res = request_irq(tpci200->info->pdev->irq,
			  tpci200_interrupt, IRQF_SHARED,
			  KBUILD_MODNAME, (void *) tpci200);
	if (res) {
		dev_err(&tpci200->info->pdev->dev,
			"(bn 0x%X, sn 0x%X) unable to register IRQ !",
			tpci200->info->pdev->bus->number,
			tpci200->info->pdev->devfn);
		goto out_release_ioid_int_space;
	}

	return 0;

out_release_mem8_space:
	pci_release_region(tpci200->info->pdev, TPCI200_MEM8_SPACE_BAR);
out_release_ioid_int_space:
	pci_release_region(tpci200->info->pdev, TPCI200_IO_ID_INT_SPACES_BAR);
out_release_ip_space:
	pci_release_region(tpci200->info->pdev, TPCI200_IP_INTERFACE_BAR);
out_disable_pci:
	pci_disable_device(tpci200->info->pdev);
	return res;
}

static int tpci200_get_clockrate(struct ipack_device *dev)
{
	struct tpci200_board *tpci200 = check_slot(dev);
	__le16 __iomem *addr;

	if (!tpci200)
		return -ENODEV;

	addr = &tpci200->info->interface_regs->control[dev->slot];
	return (ioread16(addr) & TPCI200_CLK32) ? 32 : 8;
}

static int tpci200_set_clockrate(struct ipack_device *dev, int mherz)
{
	struct tpci200_board *tpci200 = check_slot(dev);
	__le16 __iomem *addr;

	if (!tpci200)
		return -ENODEV;

	addr = &tpci200->info->interface_regs->control[dev->slot];

	switch (mherz) {
	case 8:
		tpci200_clear_mask(tpci200, addr, TPCI200_CLK32);
		break;
	case 32:
		tpci200_set_mask(tpci200, addr, TPCI200_CLK32);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int tpci200_get_error(struct ipack_device *dev)
{
	struct tpci200_board *tpci200 = check_slot(dev);
	__le16 __iomem *addr;
	u16 mask;

	if (!tpci200)
		return -ENODEV;

	addr = &tpci200->info->interface_regs->status;
	mask = tpci200_status_error[dev->slot];
	return (ioread16(addr) & mask) ? 1 : 0;
}

static int tpci200_get_timeout(struct ipack_device *dev)
{
	struct tpci200_board *tpci200 = check_slot(dev);
	__le16 __iomem *addr;
	u16 mask;

	if (!tpci200)
		return -ENODEV;

	addr = &tpci200->info->interface_regs->status;
	mask = tpci200_status_timeout[dev->slot];

	return (ioread16(addr) & mask) ? 1 : 0;
}

static int tpci200_reset_timeout(struct ipack_device *dev)
{
	struct tpci200_board *tpci200 = check_slot(dev);
	__le16 __iomem *addr;
	u16 mask;

	if (!tpci200)
		return -ENODEV;

	addr = &tpci200->info->interface_regs->status;
	mask = tpci200_status_timeout[dev->slot];

	iowrite16(mask, addr);
	return 0;
}

static void tpci200_uninstall(struct tpci200_board *tpci200)
{
	tpci200_unregister(tpci200);
	kfree(tpci200->slots);
}

static const struct ipack_bus_ops tpci200_bus_ops = {
	.request_irq = tpci200_request_irq,
	.free_irq = tpci200_free_irq,
	.get_clockrate = tpci200_get_clockrate,
	.set_clockrate = tpci200_set_clockrate,
	.get_error     = tpci200_get_error,
	.get_timeout   = tpci200_get_timeout,
	.reset_timeout = tpci200_reset_timeout,
};

static int tpci200_install(struct tpci200_board *tpci200)
{
	int res;

	tpci200->slots = kzalloc(
		TPCI200_NB_SLOT * sizeof(struct tpci200_slot), GFP_KERNEL);
	if (tpci200->slots == NULL)
		return -ENOMEM;

	res = tpci200_register(tpci200);
	if (res) {
		kfree(tpci200->slots);
		tpci200->slots = NULL;
		return res;
	}

	mutex_init(&tpci200->mutex);
	return 0;
}

static void tpci200_release_device(struct ipack_device *dev)
{
	kfree(dev);
}

static int tpci200_create_device(struct tpci200_board *tpci200, int i)
{
	enum ipack_space space;
	struct ipack_device *dev =
		kzalloc(sizeof(struct ipack_device), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;
	dev->slot = i;
	dev->bus = tpci200->info->ipack_bus;
	dev->release = tpci200_release_device;

	for (space = 0; space < IPACK_SPACE_COUNT; space++) {
		dev->region[space].start =
			tpci200->mod_mem[space]
			+ tpci200_space_interval[space] * i;
		dev->region[space].size = tpci200_space_size[space];
	}
	return ipack_device_register(dev);
}

static int tpci200_pci_probe(struct pci_dev *pdev,
			     const struct pci_device_id *id)
{
	int ret, i;
	struct tpci200_board *tpci200;
	u32 reg32;

	tpci200 = kzalloc(sizeof(struct tpci200_board), GFP_KERNEL);
	if (!tpci200)
		return -ENOMEM;

	tpci200->info = kzalloc(sizeof(struct tpci200_infos), GFP_KERNEL);
	if (!tpci200->info) {
		ret = -ENOMEM;
		goto out_err_info;
	}

	pci_dev_get(pdev);

	/* Obtain a mapping of the carrier's PCI configuration registers */
	ret = pci_request_region(pdev, TPCI200_CFG_MEM_BAR,
				 KBUILD_MODNAME " Configuration Memory");
	if (ret) {
		dev_err(&pdev->dev, "Failed to allocate PCI Configuration Memory");
		ret = -EBUSY;
		goto out_err_pci_request;
	}
	tpci200->info->cfg_regs = ioremap_nocache(
			pci_resource_start(pdev, TPCI200_CFG_MEM_BAR),
			pci_resource_len(pdev, TPCI200_CFG_MEM_BAR));
	if (!tpci200->info->cfg_regs) {
		dev_err(&pdev->dev, "Failed to map PCI Configuration Memory");
		ret = -EFAULT;
		goto out_err_ioremap;
	}

	/* Disable byte swapping for 16 bit IP module access. This will ensure
	 * that the Industrypack big endian byte order is preserved by the
	 * carrier. */
	reg32 = ioread32(tpci200->info->cfg_regs + LAS1_DESC);
	reg32 |= 1 << LAS_BIT_BIGENDIAN;
	iowrite32(reg32, tpci200->info->cfg_regs + LAS1_DESC);

	reg32 = ioread32(tpci200->info->cfg_regs + LAS2_DESC);
	reg32 |= 1 << LAS_BIT_BIGENDIAN;
	iowrite32(reg32, tpci200->info->cfg_regs + LAS2_DESC);

	/* Save struct pci_dev pointer */
	tpci200->info->pdev = pdev;
	tpci200->info->id_table = (struct pci_device_id *)id;

	/* register the device and initialize it */
	ret = tpci200_install(tpci200);
	if (ret) {
		dev_err(&pdev->dev, "error during tpci200 install\n");
		ret = -ENODEV;
		goto out_err_install;
	}

	/* Register the carrier in the industry pack bus driver */
	tpci200->info->ipack_bus = ipack_bus_register(&pdev->dev,
						      TPCI200_NB_SLOT,
						      &tpci200_bus_ops);
	if (!tpci200->info->ipack_bus) {
		dev_err(&pdev->dev,
			"error registering the carrier on ipack driver\n");
		ret = -EFAULT;
		goto out_err_bus_register;
	}

	/* save the bus number given by ipack to logging purpose */
	tpci200->number = tpci200->info->ipack_bus->bus_nr;
	dev_set_drvdata(&pdev->dev, tpci200);

	for (i = 0; i < TPCI200_NB_SLOT; i++)
		tpci200_create_device(tpci200, i);
	return 0;

out_err_bus_register:
	tpci200_uninstall(tpci200);
out_err_install:
	iounmap(tpci200->info->cfg_regs);
out_err_ioremap:
	pci_release_region(pdev, TPCI200_CFG_MEM_BAR);
out_err_pci_request:
	pci_dev_put(pdev);
	kfree(tpci200->info);
out_err_info:
	kfree(tpci200);
	return ret;
}

static void __tpci200_pci_remove(struct tpci200_board *tpci200)
{
	ipack_bus_unregister(tpci200->info->ipack_bus);
	tpci200_uninstall(tpci200);

	kfree(tpci200->info);
	kfree(tpci200);
}

static void __devexit tpci200_pci_remove(struct pci_dev *dev)
{
	struct tpci200_board *tpci200 = pci_get_drvdata(dev);

	__tpci200_pci_remove(tpci200);
}

static DEFINE_PCI_DEVICE_TABLE(tpci200_idtable) = {
	{ TPCI200_VENDOR_ID, TPCI200_DEVICE_ID, TPCI200_SUBVENDOR_ID,
	  TPCI200_SUBDEVICE_ID },
	{ 0, },
};

MODULE_DEVICE_TABLE(pci, tpci200_idtable);

static struct pci_driver tpci200_pci_drv = {
	.name = "tpci200",
	.id_table = tpci200_idtable,
	.probe = tpci200_pci_probe,
	.remove = __devexit_p(tpci200_pci_remove),
};

module_pci_driver(tpci200_pci_drv);

MODULE_DESCRIPTION("TEWS TPCI-200 device driver");
MODULE_LICENSE("GPL");
