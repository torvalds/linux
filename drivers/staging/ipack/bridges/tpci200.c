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
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include "tpci200.h"

struct ipack_bus_ops tpci200_bus_ops;

/* TPCI200 controls registers */
static int control_reg[] = {
	TPCI200_CONTROL_A_REG,
	TPCI200_CONTROL_B_REG,
	TPCI200_CONTROL_C_REG,
	TPCI200_CONTROL_D_REG
};

/* Linked list to save the registered devices */
static LIST_HEAD(tpci200_list);

static int tpci200_slot_unregister(struct ipack_device *dev);

static struct tpci200_board *check_slot(struct ipack_device *dev)
{
	struct tpci200_board *tpci200;
	int found = 0;

	if (dev == NULL) {
		pr_info("Slot doesn't exist.\n");
		return NULL;
	}

	list_for_each_entry(tpci200, &tpci200_list, list) {
		if (tpci200->number == dev->bus_nr) {
			found = 1;
			break;
		}
	}

	if (!found) {
		pr_err("Carrier not found\n");
		return NULL;
	}

	if (dev->slot >= TPCI200_NB_SLOT) {
		pr_info("Slot [%s %d:%d] doesn't exist! Last tpci200 slot is %d.\n",
			TPCI200_SHORTNAME, dev->bus_nr, dev->slot,
			TPCI200_NB_SLOT-1);
		return NULL;
	}

	BUG_ON(tpci200->slots == NULL);
	if (tpci200->slots[dev->slot].dev == NULL) {
		pr_info("Slot [%s %d:%d] is not registered !\n",
			TPCI200_SHORTNAME, dev->bus_nr, dev->slot);
		return NULL;
	}

	return tpci200;
}

static inline unsigned char __tpci200_read8(void *address, unsigned long offset)
{
	return ioread8(address + (offset^1));
}

static inline unsigned short __tpci200_read16(void *address,
					      unsigned long offset)
{
	return ioread16(address + offset);
}

static inline unsigned int __tpci200_read32(void *address, unsigned long offset)
{
	return swahw32(ioread32(address + offset));
}

static inline void __tpci200_write8(unsigned char value,
				    void *address, unsigned long offset)
{
	iowrite8(value, address+(offset^1));
}

static inline void __tpci200_write16(unsigned short value, void *address,
				     unsigned long offset)
{
	iowrite16(value, address+offset);
}

static inline void __tpci200_write32(unsigned int value, void *address,
				     unsigned long offset)
{
	iowrite32(swahw32(value), address+offset);
}

static struct ipack_addr_space *get_slot_address_space(struct ipack_device *dev,
						       int space)
{
	struct ipack_addr_space *addr;

	switch (space) {
	case IPACK_IO_SPACE:
		addr = &dev->io_space;
		break;
	case IPACK_ID_SPACE:
		addr = &dev->id_space;
		break;
	case IPACK_MEM_SPACE:
		addr = &dev->mem_space;
		break;
	default:
		pr_err("Slot [%s %d:%d] space number %d doesn't exist !\n",
		       TPCI200_SHORTNAME, dev->bus_nr, dev->slot, space);
		return NULL;
		break;
	}

	if ((addr->size == 0) || (addr->address == NULL)) {
		pr_err("Error, slot space not mapped !\n");
		return NULL;
	}

	return addr;
}

static int tpci200_read8(struct ipack_device *dev, int space,
			 unsigned long offset, unsigned char *value)
{
	struct ipack_addr_space *addr;
	struct tpci200_board *tpci200;

	tpci200 = check_slot(dev);
	if (tpci200 == NULL)
		return -EINVAL;

	addr = get_slot_address_space(dev, space);
	if (addr == NULL)
		return -EINVAL;

	if (offset >= addr->size) {
		pr_err("Error, slot space offset error !\n");
		return -EFAULT;
	}

	*value = __tpci200_read8(addr->address, offset);

	return 0;
}

static int tpci200_read16(struct ipack_device *dev, int space,
			  unsigned long offset, unsigned short *value)
{
	struct ipack_addr_space *addr;
	struct tpci200_board *tpci200;

	tpci200 = check_slot(dev);
	if (tpci200 == NULL)
		return -EINVAL;

	addr = get_slot_address_space(dev, space);
	if (addr == NULL)
		return -EINVAL;

	if ((offset+2) >= addr->size) {
		pr_err("Error, slot space offset error !\n");
		return -EFAULT;
	}
	*value = __tpci200_read16(addr->address, offset);

	return 0;
}

static int tpci200_read32(struct ipack_device *dev, int space,
			  unsigned long offset, unsigned int *value)
{
	struct ipack_addr_space *addr;
	struct tpci200_board *tpci200;

	tpci200 = check_slot(dev);
	if (tpci200 == NULL)
		return -EINVAL;

	addr = get_slot_address_space(dev, space);
	if (addr == NULL)
		return -EINVAL;

	if ((offset+4) >= addr->size) {
		pr_err("Error, slot space offset error !\n");
		return -EFAULT;
	}

	*value = __tpci200_read32(addr->address, offset);

	return 0;
}

static int tpci200_write8(struct ipack_device *dev, int space,
			  unsigned long offset, unsigned char value)
{
	struct ipack_addr_space *addr;
	struct tpci200_board *tpci200;

	tpci200 = check_slot(dev);
	if (tpci200 == NULL)
		return -EINVAL;

	addr = get_slot_address_space(dev, space);
	if (addr == NULL)
		return -EINVAL;

	if (offset >= addr->size) {
		pr_err("Error, slot space offset error !\n");
		return -EFAULT;
	}

	__tpci200_write8(value, addr->address, offset);

	return 0;
}

static int tpci200_write16(struct ipack_device *dev, int space,
			   unsigned long offset, unsigned short value)
{
	struct ipack_addr_space *addr;
	struct tpci200_board *tpci200;

	tpci200 = check_slot(dev);
	if (tpci200 == NULL)
		return -EINVAL;

	addr = get_slot_address_space(dev, space);
	if (addr == NULL)
		return -EINVAL;

	if ((offset+2) >= addr->size) {
		pr_err("Error, slot space offset error !\n");
		return -EFAULT;
	}

	__tpci200_write16(value, addr->address, offset);

	return 0;
}

static int tpci200_write32(struct ipack_device *dev, int space,
			   unsigned long offset, unsigned int value)
{
	struct ipack_addr_space *addr;
	struct tpci200_board *tpci200;

	tpci200 = check_slot(dev);
	if (tpci200 == NULL)
		return -EINVAL;

	addr = get_slot_address_space(dev, space);
	if (addr == NULL)
		return -EINVAL;

	if ((offset+4) >= addr->size) {
		pr_err("Error, slot space offset error !\n");
		return -EFAULT;
	}

	__tpci200_write32(value, addr->address, offset);

	return 0;
}

static void tpci200_unregister(struct tpci200_board *tpci200)
{
	int i;

	free_irq(tpci200->info->pdev->irq, (void *) tpci200);

	pci_iounmap(tpci200->info->pdev, tpci200->info->interface_regs);
	pci_iounmap(tpci200->info->pdev, tpci200->info->ioidint_space);
	pci_iounmap(tpci200->info->pdev, tpci200->info->mem8_space);

	pci_release_region(tpci200->info->pdev, TPCI200_IP_INTERFACE_BAR);
	pci_release_region(tpci200->info->pdev, TPCI200_IO_ID_INT_SPACES_BAR);
	pci_release_region(tpci200->info->pdev, TPCI200_MEM8_SPACE_BAR);

	pci_disable_device(tpci200->info->pdev);
	pci_dev_put(tpci200->info->pdev);

	kfree(tpci200->info);

	for (i = 0; i < TPCI200_NB_SLOT; i++) {
		tpci200->slots[i].io_phys.address = NULL;
		tpci200->slots[i].io_phys.size = 0;
		tpci200->slots[i].id_phys.address = NULL;
		tpci200->slots[i].id_phys.size = 0;
		tpci200->slots[i].mem_phys.address = NULL;
		tpci200->slots[i].mem_phys.size = 0;
	}
}

static irqreturn_t tpci200_interrupt(int irq, void *dev_id)
{
	struct tpci200_board *tpci200 = (struct tpci200_board *) dev_id;
	int i;
	unsigned long flags;
	unsigned short status_reg, reg_value;
	unsigned short unhandled_ints = 0;
	irqreturn_t ret = IRQ_NONE;

	spin_lock_irqsave(&tpci200->info->access_lock, flags);

	/* Read status register */
	status_reg = readw((unsigned short *) (tpci200->info->interface_regs +
					       TPCI200_STATUS_REG));

	if (status_reg & TPCI200_SLOT_INT_MASK) {
		unhandled_ints = status_reg & TPCI200_SLOT_INT_MASK;
		/* callback to the IRQ handler for the corresponding slot */
		for (i = 0; i < TPCI200_NB_SLOT; i++) {
			if ((tpci200->slots[i].irq != NULL) &&
			    (status_reg & ((TPCI200_A_INT0 | TPCI200_A_INT1) << (2*i)))) {

				ret = tpci200->slots[i].irq->handler(tpci200->slots[i].irq->arg);

				/* Dummy reads */
				readw((unsigned short *)
				      (tpci200->slots[i].dev->io_space.address +
				       0xC0));
				readw((unsigned short *)
				      (tpci200->slots[i].dev->io_space.address +
				       0xC2));

				unhandled_ints &= ~(((TPCI200_A_INT0 | TPCI200_A_INT1) << (2*i)));
			}
		}
	}
	/* Interrupt not handled are disabled */
	if (unhandled_ints) {
		for (i = 0; i < TPCI200_NB_SLOT; i++) {
			if (unhandled_ints & ((TPCI200_INT0_EN | TPCI200_INT1_EN) << (2*i))) {
				pr_info("No registered ISR for slot [%s %d:%d]!. IRQ will be disabled.\n",
					TPCI200_SHORTNAME,
					tpci200->number, i);
				reg_value = readw((unsigned short *)(tpci200->info->interface_regs +
								     control_reg[i]));
				reg_value &=
					~(TPCI200_INT0_EN | TPCI200_INT1_EN);
				writew(reg_value, (unsigned short *)(tpci200->info->interface_regs +
								     control_reg[i]));
			}
		}
	}

	spin_unlock_irqrestore(&tpci200->info->access_lock, flags);
	return ret;
}

#ifdef CONFIG_SYSFS

static struct ipack_device *tpci200_slot_register(const char *board_name,
						  int size,
						  unsigned int tpci200_number,
						  unsigned int slot_position)
{
	int found = 0;
	struct ipack_device  *dev;
	struct tpci200_board *tpci200;

	list_for_each_entry(tpci200, &tpci200_list, list) {
		if (tpci200->number == tpci200_number) {
			found = 1;
			break;
		}
	}

	if (!found) {
		pr_err("carrier board not found for the device\n");
		return NULL;
	}

	if (slot_position >= TPCI200_NB_SLOT) {
		pr_info("Slot [%s %d:%d] doesn't exist!\n",
			TPCI200_SHORTNAME, tpci200_number, slot_position);
		return NULL;
	}

	if (mutex_lock_interruptible(&tpci200->mutex))
		return NULL;

	if (tpci200->slots[slot_position].dev != NULL) {
		pr_err("Slot [%s %d:%d] already installed !\n",
		       TPCI200_SHORTNAME, tpci200_number, slot_position);
		goto err_unlock;
	}

	dev = kzalloc(sizeof(struct ipack_device), GFP_KERNEL);
	if (dev == NULL) {
		pr_info("Slot [%s %d:%d] Unable to allocate memory for new slot !\n",
			TPCI200_SHORTNAME,
			tpci200_number, slot_position);
		goto err_unlock;
	}

	if (size > IPACK_BOARD_NAME_SIZE) {
		pr_warning("Slot [%s %d:%d] name (%s) too long (%d > %d). Will be truncated!\n",
			   TPCI200_SHORTNAME, tpci200_number, slot_position,
			   board_name, (int)strlen(board_name),
			   IPACK_BOARD_NAME_SIZE);

		size = IPACK_BOARD_NAME_SIZE;
	}

	strncpy(dev->board_name, board_name, size-1);
	dev->board_name[size-1] = '\0';
	dev->bus_nr = tpci200->info->drv.bus_nr;
	dev->slot = slot_position;
	/*
	 * Give the same IRQ number as the slot number.
	 * The TPCI200 has assigned his own two IRQ by PCI bus driver
	 */
	dev->irq = slot_position;

	dev->id_space.address = NULL;
	dev->id_space.size = 0;
	dev->io_space.address = NULL;
	dev->io_space.size = 0;
	dev->mem_space.address = NULL;
	dev->mem_space.size = 0;

	/* Give the operations structure */
	dev->ops = &tpci200_bus_ops;
	tpci200->slots[slot_position].dev = dev;

	if (ipack_device_register(dev) < 0)
		goto err_unregister;

	mutex_unlock(&tpci200->mutex);
	return dev;

err_unregister:
	tpci200_slot_unregister(dev);
	kfree(dev);
err_unlock:
	mutex_unlock(&tpci200->mutex);
	return NULL;
}

static ssize_t tpci200_store_board(struct device *pdev, const char *buf,
				   size_t count, int slot)
{
	struct tpci200_board *card = dev_get_drvdata(pdev);
	struct ipack_device *dev = card->slots[slot].dev;

	if (dev != NULL)
		return -EBUSY;

	dev = tpci200_slot_register(buf, count, card->number, slot);
	if (dev == NULL)
		return -ENODEV;

	return count;
}

static ssize_t tpci200_show_board(struct device *pdev, char *buf, int slot)
{
	struct tpci200_board *card = dev_get_drvdata(pdev);
	struct ipack_device *dev = card->slots[slot].dev;

	if (dev != NULL)
		return snprintf(buf, PAGE_SIZE, "%s\n", dev->board_name);
	else
		return snprintf(buf, PAGE_SIZE, "none\n");
}

static ssize_t tpci200_show_description(struct device *pdev,
					struct device_attribute *attr,
					char *buf)
{
	return snprintf(buf, PAGE_SIZE,
			"TEWS tpci200 carrier PCI for Industry-pack mezzanines.\n");
}

static ssize_t tpci200_show_board_slot0(struct device *pdev,
					struct device_attribute *attr,
					char *buf)
{
	return tpci200_show_board(pdev, buf, 0);
}

static ssize_t tpci200_store_board_slot0(struct device *pdev,
					 struct device_attribute *attr,
					 const char *buf, size_t count)
{
	return tpci200_store_board(pdev, buf, count, 0);
}

static ssize_t tpci200_show_board_slot1(struct device *pdev,
					struct device_attribute *attr,
					char *buf)
{
	return tpci200_show_board(pdev, buf, 1);
}

static ssize_t tpci200_store_board_slot1(struct device *pdev,
					 struct device_attribute *attr,
					 const char *buf, size_t count)
{
	return tpci200_store_board(pdev, buf, count, 1);
}

static ssize_t tpci200_show_board_slot2(struct device *pdev,
					struct device_attribute *attr,
					char *buf)
{
	return tpci200_show_board(pdev, buf, 2);
}

static ssize_t tpci200_store_board_slot2(struct device *pdev,
					 struct device_attribute *attr,
					 const char *buf, size_t count)
{
	return tpci200_store_board(pdev, buf, count, 2);
}


static ssize_t tpci200_show_board_slot3(struct device *pdev,
					struct device_attribute *attr,
					char *buf)
{
	return tpci200_show_board(pdev, buf, 3);
}

static ssize_t tpci200_store_board_slot3(struct device *pdev,
					 struct device_attribute *attr,
					 const char *buf, size_t count)
{
	return tpci200_store_board(pdev, buf, count, 3);
}

/* Declaration of the device attributes for the TPCI200 */
static DEVICE_ATTR(description, S_IRUGO,
		   tpci200_show_description, NULL);
static DEVICE_ATTR(board_slot0, S_IRUGO | S_IWUSR,
		   tpci200_show_board_slot0, tpci200_store_board_slot0);
static DEVICE_ATTR(board_slot1, S_IRUGO | S_IWUSR,
		   tpci200_show_board_slot1, tpci200_store_board_slot1);
static DEVICE_ATTR(board_slot2, S_IRUGO | S_IWUSR,
		   tpci200_show_board_slot2, tpci200_store_board_slot2);
static DEVICE_ATTR(board_slot3, S_IRUGO | S_IWUSR,
		   tpci200_show_board_slot3, tpci200_store_board_slot3);

static struct attribute *tpci200_attrs[] = {
	&dev_attr_description.attr,
	&dev_attr_board_slot0.attr,
	&dev_attr_board_slot1.attr,
	&dev_attr_board_slot2.attr,
	&dev_attr_board_slot3.attr,
	NULL,
};

static struct attribute_group tpci200_attr_group = {
	.attrs = tpci200_attrs,
};

static int tpci200_create_sysfs_files(struct tpci200_board *card)
{
	return sysfs_create_group(&card->info->pdev->dev.kobj,
				  &tpci200_attr_group);
}

static void tpci200_remove_sysfs_files(struct tpci200_board *card)
{
	sysfs_remove_group(&card->info->pdev->dev.kobj, &tpci200_attr_group);
}

#else

static int tpci200_create_sysfs_files(struct tpci200_board *card)
{
	return 0;
}

static void tpci200_remove_sysfs_files(struct tpci200_board *card)
{
}

#endif /* CONFIG_SYSFS */

static int tpci200_register(struct tpci200_board *tpci200)
{
	int  i;
	int res;
	unsigned long ioidint_base;
	unsigned long mem_base;
	unsigned short slot_ctrl;

	if (pci_enable_device(tpci200->info->pdev) < 0)
		return -ENODEV;

	if (tpci200_create_sysfs_files(tpci200) < 0) {
		pr_err("failed creating sysfs files\n");
		res = -EFAULT;
		goto out_disable_pci;
	}

	/* Request IP interface register (Bar 2) */
	res = pci_request_region(tpci200->info->pdev, TPCI200_IP_INTERFACE_BAR,
				 "Carrier IP interface registers");
	if (res) {
		pr_err("(bn 0x%X, sn 0x%X) failed to allocate PCI resource for BAR 2 !",
		       tpci200->info->pdev->bus->number,
		       tpci200->info->pdev->devfn);
		goto out_remove_sysfs;
	}

	/* Request IO ID INT space (Bar 3) */
	res = pci_request_region(tpci200->info->pdev,
				 TPCI200_IO_ID_INT_SPACES_BAR,
				 "Carrier IO ID INT space");
	if (res) {
		pr_err("(bn 0x%X, sn 0x%X) failed to allocate PCI resource for BAR 3 !",
		       tpci200->info->pdev->bus->number,
		       tpci200->info->pdev->devfn);
		goto out_release_ip_space;
	}

	/* Request MEM space (Bar 4) */
	res = pci_request_region(tpci200->info->pdev, TPCI200_MEM8_SPACE_BAR,
				 "Carrier MEM space");
	if (res) {
		pr_err("(bn 0x%X, sn 0x%X) failed to allocate PCI resource for BAR 4!",
		       tpci200->info->pdev->bus->number,
		       tpci200->info->pdev->devfn);
		goto out_release_ioid_int_space;
	}

	/* Map internal tpci200 driver user space */
	tpci200->info->interface_regs =
		ioremap(pci_resource_start(tpci200->info->pdev,
					   TPCI200_IP_INTERFACE_BAR),
			TPCI200_IFACE_SIZE);
	tpci200->info->ioidint_space =
		ioremap(pci_resource_start(tpci200->info->pdev,
					   TPCI200_IO_ID_INT_SPACES_BAR),
			TPCI200_IOIDINT_SIZE);
	tpci200->info->mem8_space =
		ioremap(pci_resource_start(tpci200->info->pdev,
					   TPCI200_MEM8_SPACE_BAR),
			TPCI200_MEM8_SIZE);

	spin_lock_init(&tpci200->info->access_lock);
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
			(void *)ioidint_base +
			TPCI200_IO_SPACE_OFF + TPCI200_IO_SPACE_GAP*i;
		tpci200->slots[i].io_phys.size = TPCI200_IO_SPACE_SIZE;

		tpci200->slots[i].id_phys.address =
			(void *)ioidint_base +
			TPCI200_ID_SPACE_OFF + TPCI200_ID_SPACE_GAP*i;
		tpci200->slots[i].id_phys.size = TPCI200_ID_SPACE_SIZE;

		tpci200->slots[i].mem_phys.address =
			(void *)mem_base + TPCI200_MEM8_GAP*i;
		tpci200->slots[i].mem_phys.size = TPCI200_MEM8_SIZE;

		writew(slot_ctrl,
		       (unsigned short *)(tpci200->info->interface_regs +
					  control_reg[i]));
	}

	res = request_irq(tpci200->info->pdev->irq,
			  tpci200_interrupt, IRQF_SHARED,
			  TPCI200_SHORTNAME, (void *) tpci200);
	if (res) {
		pr_err("(bn 0x%X, sn 0x%X) unable to register IRQ !",
		       tpci200->info->pdev->bus->number,
		       tpci200->info->pdev->devfn);
		tpci200_unregister(tpci200);
		goto out_err;
	}

	return 0;

out_release_ioid_int_space:
	pci_release_region(tpci200->info->pdev, TPCI200_IO_ID_INT_SPACES_BAR);
out_release_ip_space:
	pci_release_region(tpci200->info->pdev, TPCI200_IP_INTERFACE_BAR);
out_remove_sysfs:
	tpci200_remove_sysfs_files(tpci200);
out_disable_pci:
	pci_disable_device(tpci200->info->pdev);
out_err:
	return res;
}

static int __tpci200_request_irq(struct tpci200_board *tpci200,
				 struct ipack_device *dev)
{
	unsigned short slot_ctrl;

	/* Set the default parameters of the slot
	 * INT0 enabled, level sensitive
	 * INT1 enabled, level sensitive
	 * error interrupt disabled
	 * timeout interrupt disabled
	 * recover time disabled
	 * clock rate 8 MHz
	 */
	slot_ctrl = TPCI200_INT0_EN | TPCI200_INT1_EN;
	writew(slot_ctrl, (unsigned short *)(tpci200->info->interface_regs +
					     control_reg[dev->slot]));

	return 0;
}

static void __tpci200_free_irq(struct tpci200_board *tpci200,
			       struct ipack_device *dev)
{
	unsigned short slot_ctrl;

	/* Set the default parameters of the slot
	 * INT0 disabled, level sensitive
	 * INT1 disabled, level sensitive
	 * error interrupt disabled
	 * timeout interrupt disabled
	 * recover time disabled
	 * clock rate 8 MHz
	 */
	slot_ctrl = 0;
	writew(slot_ctrl, (unsigned short *)(tpci200->info->interface_regs +
					     control_reg[dev->slot]));
}

static int tpci200_free_irq(struct ipack_device *dev)
{
	int res;
	struct slot_irq *slot_irq;
	struct tpci200_board *tpci200;

	tpci200 = check_slot(dev);
	if (tpci200 == NULL) {
		res = -EINVAL;
		goto out;
	}

	if (mutex_lock_interruptible(&tpci200->mutex)) {
		res = -ERESTARTSYS;
		goto out;
	}

	if (tpci200->slots[dev->slot].irq == NULL) {
		res = -EINVAL;
		goto out_unlock;
	}

	__tpci200_free_irq(tpci200, dev);
	slot_irq = tpci200->slots[dev->slot].irq;
	tpci200->slots[dev->slot].irq = NULL;
	kfree(slot_irq);

out_unlock:
	mutex_unlock(&tpci200->mutex);
out:
	return res;
}

static int tpci200_slot_unmap_space(struct ipack_device *dev, int space)
{
	int res;
	struct ipack_addr_space *virt_addr_space;
	struct tpci200_board *tpci200;

	tpci200 = check_slot(dev);
	if (tpci200 == NULL) {
		res = -EINVAL;
		goto out;
	}

	if (mutex_lock_interruptible(&tpci200->mutex)) {
		res = -ERESTARTSYS;
		goto out;
	}

	switch (space) {
	case IPACK_IO_SPACE:
		if (dev->io_space.address == NULL) {
			pr_info("Slot [%s %d:%d] IO space not mapped !\n",
				TPCI200_SHORTNAME, dev->bus_nr, dev->slot);
			goto out_unlock;
		}
		virt_addr_space = &dev->io_space;
		break;
	case IPACK_ID_SPACE:
		if (dev->id_space.address == NULL) {
			pr_info("Slot [%s %d:%d] ID space not mapped !\n",
				TPCI200_SHORTNAME, dev->bus_nr, dev->slot);
			goto out_unlock;
		}
		virt_addr_space = &dev->id_space;
		break;
	case IPACK_MEM_SPACE:
		if (dev->mem_space.address == NULL) {
			pr_info("Slot [%s %d:%d] MEM space not mapped !\n",
				TPCI200_SHORTNAME, dev->bus_nr, dev->slot);
		goto out_unlock;
		}
		virt_addr_space = &dev->mem_space;
		break;
	default:
		pr_err("Slot [%s %d:%d] space number %d doesn't exist !\n",
		       TPCI200_SHORTNAME, dev->bus_nr, dev->slot, space);
		res = -EINVAL;
		goto out_unlock;
		break;
	}

	iounmap(virt_addr_space->address);

	virt_addr_space->address = NULL;
	virt_addr_space->size = 0;
out_unlock:
	mutex_unlock(&tpci200->mutex);
out:
	return res;
}

static int tpci200_slot_unregister(struct ipack_device *dev)
{
	struct tpci200_board *tpci200;

	if (dev == NULL)
		return -ENODEV;

	tpci200 = check_slot(dev);
	if (tpci200 == NULL)
		return -EINVAL;

	tpci200_free_irq(dev);

	if (mutex_lock_interruptible(&tpci200->mutex))
		return -ERESTARTSYS;

	ipack_device_unregister(dev);
	tpci200->slots[dev->slot].dev = NULL;
	kfree(dev);
	mutex_unlock(&tpci200->mutex);

	return 0;
}

static int tpci200_slot_map_space(struct ipack_device *dev,
				  unsigned int memory_size, int space)
{
	int res;
	unsigned int size_to_map;
	void *phys_address;
	struct ipack_addr_space *virt_addr_space;
	struct tpci200_board *tpci200;

	tpci200 = check_slot(dev);
	if (tpci200 == NULL) {
		res = -EINVAL;
		goto out;
	}

	if (mutex_lock_interruptible(&tpci200->mutex)) {
		res = -ERESTARTSYS;
		goto out;
	}

	switch (space) {
	case IPACK_IO_SPACE:
		if (dev->io_space.address != NULL) {
			pr_err("Slot [%s %d:%d] IO space already mapped !\n",
			       TPCI200_SHORTNAME, tpci200->number, dev->slot);
			res = -EINVAL;
			goto out_unlock;
		}
		virt_addr_space = &dev->io_space;

		phys_address = tpci200->slots[dev->slot].io_phys.address;
		size_to_map = tpci200->slots[dev->slot].io_phys.size;
		break;
	case IPACK_ID_SPACE:
		if (dev->id_space.address != NULL) {
			pr_err("Slot [%s %d:%d] ID space already mapped !\n",
			       TPCI200_SHORTNAME, tpci200->number, dev->slot);
			res = -EINVAL;
			goto out_unlock;
		}
		virt_addr_space = &dev->id_space;

		phys_address = tpci200->slots[dev->slot].id_phys.address;
		size_to_map = tpci200->slots[dev->slot].id_phys.size;
		break;
	case IPACK_MEM_SPACE:
		if (dev->mem_space.address != NULL) {
			pr_err("Slot [%s %d:%d] MEM space already mapped !\n",
			       TPCI200_SHORTNAME,
			       tpci200->number, dev->slot);
			res = -EINVAL;
			goto out_unlock;
		}
		virt_addr_space = &dev->mem_space;

		if (memory_size > tpci200->slots[dev->slot].mem_phys.size) {
			pr_err("Slot [%s %d:%d] request is 0x%X memory, only 0x%X available !\n",
			       TPCI200_SHORTNAME, dev->bus_nr, dev->slot,
			       memory_size, tpci200->slots[dev->slot].mem_phys.size);
			res = -EINVAL;
			goto out_unlock;
		}

		phys_address = tpci200->slots[dev->slot].mem_phys.address;
		size_to_map = memory_size;
		break;
	default:
		pr_err("Slot [%s %d:%d] space %d doesn't exist !\n",
		       TPCI200_SHORTNAME,
		       tpci200->number, dev->slot, space);
		res = -EINVAL;
		goto out_unlock;
		break;
	}

	virt_addr_space->size = size_to_map;
	virt_addr_space->address =
		ioremap((unsigned long)phys_address, size_to_map);

out_unlock:
	mutex_unlock(&tpci200->mutex);
out:
	return res;
}

static int tpci200_request_irq(struct ipack_device *dev, int vector,
			       int (*handler)(void *), void *arg)
{
	int res;
	struct slot_irq *slot_irq;
	struct tpci200_board *tpci200;

	tpci200 = check_slot(dev);
	if (tpci200 == NULL) {
		res = -EINVAL;
		goto out;
	}

	if (mutex_lock_interruptible(&tpci200->mutex)) {
		res = -ERESTARTSYS;
		goto out;
	}

	if (tpci200->slots[dev->slot].irq != NULL) {
		pr_err("Slot [%s %d:%d] IRQ already registered !\n",
		       TPCI200_SHORTNAME, dev->bus_nr, dev->slot);
		res = -EINVAL;
		goto out_unlock;
	}

	slot_irq = kzalloc(sizeof(struct slot_irq), GFP_KERNEL);
	if (slot_irq == NULL) {
		pr_err("Slot [%s %d:%d] unable to allocate memory for IRQ !\n",
		       TPCI200_SHORTNAME, dev->bus_nr, dev->slot);
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
	if (dev->board_name) {
		if (strlen(dev->board_name) > IPACK_IRQ_NAME_SIZE) {
			pr_warning("Slot [%s %d:%d] IRQ name too long (%d char > %d char MAX). Will be truncated!\n",
				   TPCI200_SHORTNAME, dev->bus_nr, dev->slot,
				   (int)strlen(dev->board_name),
				   IPACK_IRQ_NAME_SIZE);
		}
		strncpy(slot_irq->name, dev->board_name, IPACK_IRQ_NAME_SIZE-1);
	} else {
		strcpy(slot_irq->name, "Unknown");
	}

	tpci200->slots[dev->slot].irq = slot_irq;
	res = __tpci200_request_irq(tpci200, dev);

out_unlock:
	mutex_unlock(&tpci200->mutex);
out:
	return res;
}

static void tpci200_slot_remove(struct tpci200_slot *slot)
{
	if ((slot->dev == NULL) ||
	    (slot->dev->driver->ops->remove == NULL))
		return;

	slot->dev->driver->ops->remove(slot->dev);
}

static void tpci200_uninstall(struct tpci200_board *tpci200)
{
	int i;

	for (i = 0; i < TPCI200_NB_SLOT; i++)
		tpci200_slot_remove(&tpci200->slots[i]);

	tpci200_unregister(tpci200);
	kfree(tpci200->slots);
}

struct ipack_bus_ops tpci200_bus_ops = {
	.map_space = tpci200_slot_map_space,
	.unmap_space = tpci200_slot_unmap_space,
	.request_irq = tpci200_request_irq,
	.free_irq = tpci200_free_irq,
	.read8 = tpci200_read8,
	.read16 = tpci200_read16,
	.read32 = tpci200_read32,
	.write8 = tpci200_write8,
	.write16 = tpci200_write16,
	.write32 = tpci200_write32,
	.remove_device = tpci200_slot_unregister,
};

static int tpci200_install(struct tpci200_board *tpci200)
{
	int res;

	tpci200->slots = kzalloc(
		TPCI200_NB_SLOT * sizeof(struct tpci200_slot), GFP_KERNEL);
	if (tpci200->slots == NULL) {
		res = -ENOMEM;
		goto out_err;
	}

	res = tpci200_register(tpci200);
	if (res)
		goto out_free;

	mutex_init(&tpci200->mutex);
	return 0;

out_free:
	kfree(tpci200->slots);
	tpci200->slots = NULL;
out_err:
	return res;
}

static int tpci200_pciprobe(struct pci_dev *pdev,
			    const struct pci_device_id *id)
{
	int ret;
	struct tpci200_board *tpci200;

	tpci200 = kzalloc(sizeof(struct tpci200_board), GFP_KERNEL);
	if (!tpci200)
		return -ENOMEM;

	tpci200->info = kzalloc(sizeof(struct tpci200_infos), GFP_KERNEL);
	if (!tpci200->info) {
		kfree(tpci200);
		return -ENOMEM;
	}

	/* Save struct pci_dev pointer */
	tpci200->info->pdev = pdev;
	tpci200->info->id_table = (struct pci_device_id *)id;

	/* register the device and initialize it */
	ret = tpci200_install(tpci200);
	if (ret) {
		pr_err("Error during tpci200 install !\n");
		kfree(tpci200->info);
		kfree(tpci200);
		return -ENODEV;
	}

	tpci200->info->drv.dev = &pdev->dev;
	tpci200->info->drv.slots = TPCI200_NB_SLOT;

	/* Register the bus in the industry pack driver */
	ret = ipack_bus_register(&tpci200->info->drv);
	if (ret < 0) {
		pr_err("error registering the carrier on ipack driver\n");
		tpci200_uninstall(tpci200);
		kfree(tpci200->info);
		kfree(tpci200);
		return -EFAULT;
	}
	/* save the bus number given by ipack to logging purpose */
	tpci200->number = tpci200->info->drv.bus_nr;
	dev_set_drvdata(&pdev->dev, tpci200);
	/* add the registered device in an internal linked list */
	list_add_tail(&tpci200->list, &tpci200_list);
	return ret;
}

static void __tpci200_pci_remove(struct tpci200_board *tpci200)
{
	tpci200_uninstall(tpci200);
	tpci200_remove_sysfs_files(tpci200);
	list_del(&tpci200->list);
	ipack_bus_unregister(&tpci200->info->drv);
	kfree(tpci200);
}

static void __devexit tpci200_pci_remove(struct pci_dev *dev)
{
	struct tpci200_board *tpci200, *next;

	/* Search the registered device to uninstall it */
	list_for_each_entry_safe(tpci200, next, &tpci200_list, list) {
		if (tpci200->info->pdev == dev) {
			__tpci200_pci_remove(tpci200);
			break;
		}
	}
}

static struct pci_device_id tpci200_idtable[2] = {
	{ TPCI200_VENDOR_ID, TPCI200_DEVICE_ID, TPCI200_SUBVENDOR_ID,
	  TPCI200_SUBDEVICE_ID },
	{ 0, },
};

static struct pci_driver tpci200_pci_drv = {
	.name = "tpci200",
	.id_table = tpci200_idtable,
	.probe = tpci200_pciprobe,
	.remove = __devexit_p(tpci200_pci_remove),
};

static int __init tpci200_drvr_init_module(void)
{
	return pci_register_driver(&tpci200_pci_drv);
}

static void __exit tpci200_drvr_exit_module(void)
{
	struct tpci200_board *tpci200, *next;

	list_for_each_entry_safe(tpci200, next, &tpci200_list, list)
		__tpci200_pci_remove(tpci200);

	pci_unregister_driver(&tpci200_pci_drv);
}

MODULE_DESCRIPTION("TEWS TPCI-200 device driver");
MODULE_LICENSE("GPL");
module_init(tpci200_drvr_init_module);
module_exit(tpci200_drvr_exit_module);
