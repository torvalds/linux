/**
 * tpci200.c
 *
 * driver for the TEWS TPCI-200 device
 * Copyright (c) 2009 Nicolas Serafini, EIC2 SA
 * Copyright (c) 2010,2011 Samuel Iglesias Gonsalvez <siglesia@cern.ch>, CERN
 * Copyright (c) 2012 Samuel Iglesias Gonsalvez <siglesias@igalia.com>, Igalia
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; version 2 of the License.
 */

#include <linux/module.h>
#include "tpci200.h"

static u16 tpci200_status_timeout[] = {
	TPCI200_A_TIMEOUT,
	TPCI200_B_TIMEOUT,
	TPCI200_C_TIMEOUT,
	TPCI200_D_TIMEOUT,
};

static u16 tpci200_status_error[] = {
	TPCI200_A_ERROR,
	TPCI200_B_ERROR,
	TPCI200_C_ERROR,
	TPCI200_D_ERROR,
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
			 dev->bus_nr, dev->slot, TPCI200_NB_SLOT-1);
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
	int i;

	free_irq(tpci200->info->pdev->irq, (void *) tpci200);

	pci_iounmap(tpci200->info->pdev, tpci200->info->interface_regs);
	pci_iounmap(tpci200->info->pdev, tpci200->info->ioidint_space);
	pci_iounmap(tpci200->info->pdev, tpci200->info->mem8_space);
	pci_iounmap(tpci200->info->pdev, tpci200->info->cfg_regs);

	pci_release_region(tpci200->info->pdev, TPCI200_IP_INTERFACE_BAR);
	pci_release_region(tpci200->info->pdev, TPCI200_IO_ID_INT_SPACES_BAR);
	pci_release_region(tpci200->info->pdev, TPCI200_MEM8_SPACE_BAR);
	pci_release_region(tpci200->info->pdev, TPCI200_CFG_MEM_BAR);

	pci_disable_device(tpci200->info->pdev);
	pci_dev_put(tpci200->info->pdev);

	for (i = 0; i < TPCI200_NB_SLOT; i++) {
		tpci200->slots[i].io_phys.address = NULL;
		tpci200->slots[i].io_phys.size = 0;
		tpci200->slots[i].id_phys.address = NULL;
		tpci200->slots[i].id_phys.size = 0;
		tpci200->slots[i].mem_phys.address = NULL;
		tpci200->slots[i].mem_phys.size = 0;
	}
}

static irqreturn_t tpci200_slot_irq(struct slot_irq *slot_irq)
{
	irqreturn_t ret = slot_irq->handler(slot_irq->arg);

	/* Dummy reads */
	readw(slot_irq->holder->io_space.address + 0xC0);
	readw(slot_irq->holder->io_space.address + 0xC2);

	return ret;
}

static irqreturn_t tpci200_interrupt(int irq, void *dev_id)
{
	struct tpci200_board *tpci200 = (struct tpci200_board *) dev_id;
	int i;
	unsigned short status_reg;
	irqreturn_t ret = IRQ_NONE;
	struct slot_irq *slot_irq;

	/* Read status register */
	status_reg = readw(&tpci200->info->interface_regs->status);

	if (status_reg & TPCI200_SLOT_INT_MASK) {
		/* callback to the IRQ handler for the corresponding slot */
		rcu_read_lock();
		for (i = 0; i < TPCI200_NB_SLOT; i++) {
			if (!(status_reg & ((TPCI200_A_INT0 | TPCI200_A_INT1) << (2*i))))
				continue;
			slot_irq = rcu_dereference(tpci200->slots[i].irq);
			if (slot_irq) {
				ret = tpci200_slot_irq(slot_irq);
			} else {
				dev_info(&tpci200->info->pdev->dev,
					 "No registered ISR for slot [%d:%d]!. IRQ will be disabled.\n",
					 tpci200->number, i);
				tpci200_clear_mask(tpci200,
					&tpci200->info->interface_regs->control[i],
					TPCI200_INT0_EN | TPCI200_INT1_EN);
			}
		}
		rcu_read_unlock();
	}

	return ret;
}

static int tpci200_register(struct tpci200_board *tpci200)
{
	int i;
	int res;
	unsigned long ioidint_base;
	unsigned long mem_base;
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

	/* Request MEM space (Bar 4) */
	res = pci_request_region(tpci200->info->pdev, TPCI200_MEM8_SPACE_BAR,
				 "Carrier MEM space");
	if (res) {
		dev_err(&tpci200->info->pdev->dev,
			"(bn 0x%X, sn 0x%X) failed to allocate PCI resource for BAR 4!",
			tpci200->info->pdev->bus->number,
			tpci200->info->pdev->devfn);
		goto out_release_ioid_int_space;
	}

	/* Map internal tpci200 driver user space */
	tpci200->info->interface_regs =
		ioremap_nocache(pci_resource_start(tpci200->info->pdev,
					   TPCI200_IP_INTERFACE_BAR),
			TPCI200_IFACE_SIZE);
	tpci200->info->ioidint_space =
		ioremap_nocache(pci_resource_start(tpci200->info->pdev,
					   TPCI200_IO_ID_INT_SPACES_BAR),
			TPCI200_IOIDINT_SIZE);
	tpci200->info->mem8_space =
		ioremap_nocache(pci_resource_start(tpci200->info->pdev,
					   TPCI200_MEM8_SPACE_BAR),
			TPCI200_MEM8_SIZE);

	/* Initialize lock that protects interface_regs */
	spin_lock_init(&tpci200->regs_lock);

	ioidint_base = pci_resource_start(tpci200->info->pdev,
					  TPCI200_IO_ID_INT_SPACES_BAR);
	mem_base = pci_resource_start(tpci200->info->pdev,
				      TPCI200_MEM8_SPACE_BAR);

	/* Set the default parameters of the slot
	 * INT0 disabled, level sensitive
	 * INT1 disabled, level sensitive
	 * error interrupt disabled
	 * timeout interrupt disabled
	 * recover time disabled
	 * clock rate 8 MHz
	 */
	slot_ctrl = 0;

	/* Set all slot physical address space */
	for (i = 0; i < TPCI200_NB_SLOT; i++) {
		tpci200->slots[i].io_phys.address =
			(void __iomem *)ioidint_base +
			TPCI200_IO_SPACE_OFF + TPCI200_IO_SPACE_GAP*i;
		tpci200->slots[i].io_phys.size = TPCI200_IO_SPACE_SIZE;

		tpci200->slots[i].id_phys.address =
			(void __iomem *)ioidint_base +
			TPCI200_ID_SPACE_OFF + TPCI200_ID_SPACE_GAP*i;
		tpci200->slots[i].id_phys.size = TPCI200_ID_SPACE_SIZE;

		tpci200->slots[i].mem_phys.address =
			(void __iomem *)mem_base + TPCI200_MEM8_GAP*i;
		tpci200->slots[i].mem_phys.size = TPCI200_MEM8_SIZE;

		writew(slot_ctrl, &tpci200->info->interface_regs->control[i]);
	}

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

out_release_ioid_int_space:
	pci_release_region(tpci200->info->pdev, TPCI200_IO_ID_INT_SPACES_BAR);
out_release_ip_space:
	pci_release_region(tpci200->info->pdev, TPCI200_IP_INTERFACE_BAR);
out_disable_pci:
	pci_disable_device(tpci200->info->pdev);
	return res;
}

static int __tpci200_request_irq(struct tpci200_board *tpci200,
				 struct ipack_device *dev)
{
	tpci200_set_mask(tpci200,
			&tpci200->info->interface_regs->control[dev->slot],
			TPCI200_INT0_EN | TPCI200_INT1_EN);
	return 0;
}

static void __tpci200_free_irq(struct tpci200_board *tpci200,
			       struct ipack_device *dev)
{
	tpci200_clear_mask(tpci200,
			&tpci200->info->interface_regs->control[dev->slot],
			TPCI200_INT0_EN | TPCI200_INT1_EN);
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

	__tpci200_free_irq(tpci200, dev);
	slot_irq = tpci200->slots[dev->slot].irq;
	RCU_INIT_POINTER(tpci200->slots[dev->slot].irq, NULL);
	synchronize_rcu();
	kfree(slot_irq);
	mutex_unlock(&tpci200->mutex);
	return 0;
}

static int tpci200_slot_unmap_space(struct ipack_device *dev, int space)
{
	struct ipack_addr_space *virt_addr_space;
	struct tpci200_board *tpci200;

	tpci200 = check_slot(dev);
	if (tpci200 == NULL)
		return -EINVAL;

	if (mutex_lock_interruptible(&tpci200->mutex))
		return -ERESTARTSYS;

	switch (space) {
	case IPACK_IO_SPACE:
		if (dev->io_space.address == NULL) {
			dev_info(&dev->dev,
				 "Slot [%d:%d] IO space not mapped !\n",
				 dev->bus_nr, dev->slot);
			goto out_unlock;
		}
		virt_addr_space = &dev->io_space;
		break;
	case IPACK_ID_SPACE:
		if (dev->id_space.address == NULL) {
			dev_info(&dev->dev,
				 "Slot [%d:%d] ID space not mapped !\n",
				 dev->bus_nr, dev->slot);
			goto out_unlock;
		}
		virt_addr_space = &dev->id_space;
		break;
	case IPACK_MEM_SPACE:
		if (dev->mem_space.address == NULL) {
			dev_info(&dev->dev,
				 "Slot [%d:%d] MEM space not mapped !\n",
				 dev->bus_nr, dev->slot);
			goto out_unlock;
		}
		virt_addr_space = &dev->mem_space;
		break;
	default:
		dev_err(&dev->dev,
			"Slot [%d:%d] space number %d doesn't exist !\n",
			dev->bus_nr, dev->slot, space);
		mutex_unlock(&tpci200->mutex);
		return -EINVAL;
	}

	iounmap(virt_addr_space->address);

	virt_addr_space->address = NULL;
	virt_addr_space->size = 0;
out_unlock:
	mutex_unlock(&tpci200->mutex);
	return 0;
}

static int tpci200_slot_map_space(struct ipack_device *dev,
				  unsigned int memory_size, int space)
{
	int res = 0;
	unsigned int size_to_map;
	void __iomem *phys_address;
	struct ipack_addr_space *virt_addr_space;
	struct tpci200_board *tpci200;

	tpci200 = check_slot(dev);
	if (tpci200 == NULL)
		return -EINVAL;

	if (mutex_lock_interruptible(&tpci200->mutex))
		return -ERESTARTSYS;

	switch (space) {
	case IPACK_IO_SPACE:
		if (dev->io_space.address != NULL) {
			dev_err(&dev->dev,
				"Slot [%d:%d] IO space already mapped !\n",
				tpci200->number, dev->slot);
			res = -EINVAL;
			goto out_unlock;
		}
		virt_addr_space = &dev->io_space;

		phys_address = tpci200->slots[dev->slot].io_phys.address;
		size_to_map = tpci200->slots[dev->slot].io_phys.size;
		break;
	case IPACK_ID_SPACE:
		if (dev->id_space.address != NULL) {
			dev_err(&dev->dev,
				"Slot [%d:%d] ID space already mapped !\n",
				tpci200->number, dev->slot);
			res = -EINVAL;
			goto out_unlock;
		}
		virt_addr_space = &dev->id_space;

		phys_address = tpci200->slots[dev->slot].id_phys.address;
		size_to_map = tpci200->slots[dev->slot].id_phys.size;
		break;
	case IPACK_MEM_SPACE:
		if (dev->mem_space.address != NULL) {
			dev_err(&dev->dev,
				"Slot [%d:%d] MEM space already mapped !\n",
				tpci200->number, dev->slot);
			res = -EINVAL;
			goto out_unlock;
		}
		virt_addr_space = &dev->mem_space;

		if (memory_size > tpci200->slots[dev->slot].mem_phys.size) {
			dev_err(&dev->dev,
				"Slot [%d:%d] request is 0x%X memory, only 0x%X available !\n",
				dev->bus_nr, dev->slot, memory_size,
				tpci200->slots[dev->slot].mem_phys.size);
			res = -EINVAL;
			goto out_unlock;
		}

		phys_address = tpci200->slots[dev->slot].mem_phys.address;
		size_to_map = memory_size;
		break;
	default:
		dev_err(&dev->dev, "Slot [%d:%d] space %d doesn't exist !\n",
			tpci200->number, dev->slot, space);
		res = -EINVAL;
		goto out_unlock;
	}

	virt_addr_space->size = size_to_map;
	virt_addr_space->address =
		ioremap_nocache((unsigned long)phys_address, size_to_map);

out_unlock:
	mutex_unlock(&tpci200->mutex);
	return res;
}

static int tpci200_request_irq(struct ipack_device *dev, int vector,
			       int (*handler)(void *), void *arg)
{
	int res;
	struct slot_irq *slot_irq;
	struct tpci200_board *tpci200;

	tpci200 = check_slot(dev);
	if (tpci200 == NULL)
		return -EINVAL;

	if (mutex_lock_interruptible(&tpci200->mutex))
		return -ERESTARTSYS;

	if (tpci200->slots[dev->slot].irq != NULL) {
		dev_err(&dev->dev,
			"Slot [%d:%d] IRQ already registered !\n", dev->bus_nr,
			dev->slot);
		res = -EINVAL;
		goto out_unlock;
	}

	slot_irq = kzalloc(sizeof(struct slot_irq), GFP_KERNEL);
	if (slot_irq == NULL) {
		dev_err(&dev->dev,
			"Slot [%d:%d] unable to allocate memory for IRQ !\n",
			dev->bus_nr, dev->slot);
		res = -ENOMEM;
		goto out_unlock;
	}

	/*
	 * WARNING: Setup Interrupt Vector in the IndustryPack device
	 * before an IRQ request.
	 * Read the User Manual of your IndustryPack device to know
	 * where to write the vector in memory.
	 */
	slot_irq->vector = vector;
	slot_irq->handler = handler;
	slot_irq->arg = arg;
	slot_irq->holder = dev;

	rcu_assign_pointer(tpci200->slots[dev->slot].irq, slot_irq);
	res = __tpci200_request_irq(tpci200, dev);

out_unlock:
	mutex_unlock(&tpci200->mutex);
	return res;
}

static int tpci200_get_clockrate(struct ipack_device *dev)
{
	struct tpci200_board *tpci200 = check_slot(dev);
	u16 __iomem *addr;

	if (!tpci200)
		return -ENODEV;

	addr = &tpci200->info->interface_regs->control[dev->slot];
	return (ioread16(addr) & TPCI200_CLK32) ? 32 : 8;
}

static int tpci200_set_clockrate(struct ipack_device *dev, int mherz)
{
	struct tpci200_board *tpci200 = check_slot(dev);
	u16 __iomem *addr;

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
	u16 __iomem *addr;
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
	u16 __iomem *addr;
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
	u16 __iomem *addr;
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
	.map_space = tpci200_slot_map_space,
	.unmap_space = tpci200_slot_unmap_space,
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

	/*
	 * Give the same IRQ number as the slot number.
	 * The TPCI200 has assigned his own two IRQ by PCI bus driver
	 */
	for (i = 0; i < TPCI200_NB_SLOT; i++)
		ipack_device_register(tpci200->info->ipack_bus, i, i);
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

static int __init tpci200_drvr_init_module(void)
{
	return pci_register_driver(&tpci200_pci_drv);
}

static void __exit tpci200_drvr_exit_module(void)
{
	pci_unregister_driver(&tpci200_pci_drv);
}

MODULE_DESCRIPTION("TEWS TPCI-200 device driver");
MODULE_LICENSE("GPL");
module_init(tpci200_drvr_init_module);
module_exit(tpci200_drvr_exit_module);
