// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2024 Marvell. */

#include <linux/cleanup.h>
#include <linux/container_of.h>
#include <linux/delay.h>
#include <linux/dev_printk.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/pci_hotplug.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

#define OCTEP_HP_INTR_OFFSET(x) (0x20400 + ((x) << 4))
#define OCTEP_HP_INTR_VECTOR(x) (16 + (x))
#define OCTEP_HP_DRV_NAME "octep_hp"

/*
 * Type of MSI-X interrupts. OCTEP_HP_INTR_VECTOR() and
 * OCTEP_HP_INTR_OFFSET() generate the vector and offset for an interrupt
 * type.
 */
enum octep_hp_intr_type {
	OCTEP_HP_INTR_INVALID = -1,
	OCTEP_HP_INTR_ENA = 0,
	OCTEP_HP_INTR_DIS = 1,
	OCTEP_HP_INTR_MAX = 2,
};

struct octep_hp_cmd {
	struct list_head list;
	enum octep_hp_intr_type intr_type;
	u64 intr_val;
};

struct octep_hp_slot {
	struct list_head list;
	struct hotplug_slot slot;
	u16 slot_number;
	struct pci_dev *hp_pdev;
	unsigned int hp_devfn;
	struct octep_hp_controller *ctrl;
};

struct octep_hp_intr_info {
	enum octep_hp_intr_type type;
	int number;
	char name[16];
};

struct octep_hp_controller {
	void __iomem *base;
	struct pci_dev *pdev;
	struct octep_hp_intr_info intr[OCTEP_HP_INTR_MAX];
	struct work_struct work;
	struct list_head slot_list;
	struct mutex slot_lock; /* Protects slot_list */
	struct list_head hp_cmd_list;
	spinlock_t hp_cmd_lock; /* Protects hp_cmd_list */
};

static void octep_hp_enable_pdev(struct octep_hp_controller *hp_ctrl,
				 struct octep_hp_slot *hp_slot)
{
	guard(mutex)(&hp_ctrl->slot_lock);
	if (hp_slot->hp_pdev) {
		pci_dbg(hp_slot->hp_pdev, "Slot %s is already enabled\n",
			hotplug_slot_name(&hp_slot->slot));
		return;
	}

	/* Scan the device and add it to the bus */
	hp_slot->hp_pdev = pci_scan_single_device(hp_ctrl->pdev->bus,
						  hp_slot->hp_devfn);
	pci_bus_assign_resources(hp_ctrl->pdev->bus);
	pci_bus_add_device(hp_slot->hp_pdev);

	dev_dbg(&hp_slot->hp_pdev->dev, "Enabled slot %s\n",
		hotplug_slot_name(&hp_slot->slot));
}

static void octep_hp_disable_pdev(struct octep_hp_controller *hp_ctrl,
				  struct octep_hp_slot *hp_slot)
{
	guard(mutex)(&hp_ctrl->slot_lock);
	if (!hp_slot->hp_pdev) {
		pci_dbg(hp_ctrl->pdev, "Slot %s is already disabled\n",
			hotplug_slot_name(&hp_slot->slot));
		return;
	}

	pci_dbg(hp_slot->hp_pdev, "Disabling slot %s\n",
		hotplug_slot_name(&hp_slot->slot));

	/* Remove the device from the bus */
	pci_stop_and_remove_bus_device_locked(hp_slot->hp_pdev);
	hp_slot->hp_pdev = NULL;
}

static int octep_hp_enable_slot(struct hotplug_slot *slot)
{
	struct octep_hp_slot *hp_slot =
		container_of(slot, struct octep_hp_slot, slot);

	octep_hp_enable_pdev(hp_slot->ctrl, hp_slot);
	return 0;
}

static int octep_hp_disable_slot(struct hotplug_slot *slot)
{
	struct octep_hp_slot *hp_slot =
		container_of(slot, struct octep_hp_slot, slot);

	octep_hp_disable_pdev(hp_slot->ctrl, hp_slot);
	return 0;
}

static struct hotplug_slot_ops octep_hp_slot_ops = {
	.enable_slot = octep_hp_enable_slot,
	.disable_slot = octep_hp_disable_slot,
};

#define SLOT_NAME_SIZE 16
static struct octep_hp_slot *
octep_hp_register_slot(struct octep_hp_controller *hp_ctrl,
		       struct pci_dev *pdev, u16 slot_number)
{
	char slot_name[SLOT_NAME_SIZE];
	struct octep_hp_slot *hp_slot;
	int ret;

	hp_slot = kzalloc(sizeof(*hp_slot), GFP_KERNEL);
	if (!hp_slot)
		return ERR_PTR(-ENOMEM);

	hp_slot->ctrl = hp_ctrl;
	hp_slot->hp_pdev = pdev;
	hp_slot->hp_devfn = pdev->devfn;
	hp_slot->slot_number = slot_number;
	hp_slot->slot.ops = &octep_hp_slot_ops;

	snprintf(slot_name, sizeof(slot_name), "octep_hp_%u", slot_number);
	ret = pci_hp_register(&hp_slot->slot, hp_ctrl->pdev->bus,
			      PCI_SLOT(pdev->devfn), slot_name);
	if (ret) {
		kfree(hp_slot);
		return ERR_PTR(ret);
	}

	pci_info(pdev, "Registered slot %s for device %s\n",
		 slot_name, pci_name(pdev));

	list_add_tail(&hp_slot->list, &hp_ctrl->slot_list);
	octep_hp_disable_pdev(hp_ctrl, hp_slot);

	return hp_slot;
}

static void octep_hp_deregister_slot(void *data)
{
	struct octep_hp_slot *hp_slot = data;
	struct octep_hp_controller *hp_ctrl = hp_slot->ctrl;

	pci_hp_deregister(&hp_slot->slot);
	octep_hp_enable_pdev(hp_ctrl, hp_slot);
	list_del(&hp_slot->list);
	kfree(hp_slot);
}

static const char *octep_hp_cmd_name(enum octep_hp_intr_type type)
{
	switch (type) {
	case OCTEP_HP_INTR_ENA:
		return "hotplug enable";
	case OCTEP_HP_INTR_DIS:
		return "hotplug disable";
	default:
		return "invalid";
	}
}

static void octep_hp_cmd_handler(struct octep_hp_controller *hp_ctrl,
				 struct octep_hp_cmd *hp_cmd)
{
	struct octep_hp_slot *hp_slot;

	/*
	 * Enable or disable the slots based on the slot mask.
	 * intr_val is a bit mask where each bit represents a slot.
	 */
	list_for_each_entry(hp_slot, &hp_ctrl->slot_list, list) {
		if (!(hp_cmd->intr_val & BIT(hp_slot->slot_number)))
			continue;

		pci_info(hp_ctrl->pdev, "Received %s command for slot %s\n",
			 octep_hp_cmd_name(hp_cmd->intr_type),
			 hotplug_slot_name(&hp_slot->slot));

		switch (hp_cmd->intr_type) {
		case OCTEP_HP_INTR_ENA:
			octep_hp_enable_pdev(hp_ctrl, hp_slot);
			break;
		case OCTEP_HP_INTR_DIS:
			octep_hp_disable_pdev(hp_ctrl, hp_slot);
			break;
		default:
			break;
		}
	}
}

static void octep_hp_work_handler(struct work_struct *work)
{
	struct octep_hp_controller *hp_ctrl;
	struct octep_hp_cmd *hp_cmd;
	unsigned long flags;

	hp_ctrl = container_of(work, struct octep_hp_controller, work);

	/* Process all the hotplug commands */
	spin_lock_irqsave(&hp_ctrl->hp_cmd_lock, flags);
	while (!list_empty(&hp_ctrl->hp_cmd_list)) {
		hp_cmd = list_first_entry(&hp_ctrl->hp_cmd_list,
					  struct octep_hp_cmd, list);
		list_del(&hp_cmd->list);
		spin_unlock_irqrestore(&hp_ctrl->hp_cmd_lock, flags);

		octep_hp_cmd_handler(hp_ctrl, hp_cmd);
		kfree(hp_cmd);

		spin_lock_irqsave(&hp_ctrl->hp_cmd_lock, flags);
	}
	spin_unlock_irqrestore(&hp_ctrl->hp_cmd_lock, flags);
}

static enum octep_hp_intr_type octep_hp_intr_type(struct octep_hp_intr_info *intr,
						  int irq)
{
	enum octep_hp_intr_type type;

	for (type = OCTEP_HP_INTR_ENA; type < OCTEP_HP_INTR_MAX; type++) {
		if (intr[type].number == irq)
			return type;
	}

	return OCTEP_HP_INTR_INVALID;
}

static irqreturn_t octep_hp_intr_handler(int irq, void *data)
{
	struct octep_hp_controller *hp_ctrl = data;
	struct pci_dev *pdev = hp_ctrl->pdev;
	enum octep_hp_intr_type type;
	struct octep_hp_cmd *hp_cmd;
	u64 intr_val;

	type = octep_hp_intr_type(hp_ctrl->intr, irq);
	if (type == OCTEP_HP_INTR_INVALID) {
		pci_err(pdev, "Invalid interrupt %d\n", irq);
		return IRQ_HANDLED;
	}

	/* Read and clear the interrupt */
	intr_val = readq(hp_ctrl->base + OCTEP_HP_INTR_OFFSET(type));
	writeq(intr_val, hp_ctrl->base + OCTEP_HP_INTR_OFFSET(type));

	hp_cmd = kzalloc(sizeof(*hp_cmd), GFP_ATOMIC);
	if (!hp_cmd)
		return IRQ_HANDLED;

	hp_cmd->intr_val = intr_val;
	hp_cmd->intr_type = type;

	/* Add the command to the list and schedule the work */
	spin_lock(&hp_ctrl->hp_cmd_lock);
	list_add_tail(&hp_cmd->list, &hp_ctrl->hp_cmd_list);
	spin_unlock(&hp_ctrl->hp_cmd_lock);
	schedule_work(&hp_ctrl->work);

	return IRQ_HANDLED;
}

static void octep_hp_irq_cleanup(void *data)
{
	struct octep_hp_controller *hp_ctrl = data;

	pci_free_irq_vectors(hp_ctrl->pdev);
	flush_work(&hp_ctrl->work);
}

static int octep_hp_request_irq(struct octep_hp_controller *hp_ctrl,
				enum octep_hp_intr_type type)
{
	struct pci_dev *pdev = hp_ctrl->pdev;
	struct octep_hp_intr_info *intr;
	int irq;

	irq = pci_irq_vector(pdev, OCTEP_HP_INTR_VECTOR(type));
	if (irq < 0)
		return irq;

	intr = &hp_ctrl->intr[type];
	intr->number = irq;
	intr->type = type;
	snprintf(intr->name, sizeof(intr->name), "octep_hp_%d", type);

	return devm_request_irq(&pdev->dev, irq, octep_hp_intr_handler,
				IRQF_SHARED, intr->name, hp_ctrl);
}

static int octep_hp_controller_setup(struct pci_dev *pdev,
				     struct octep_hp_controller *hp_ctrl)
{
	struct device *dev = &pdev->dev;
	enum octep_hp_intr_type type;
	int ret;

	ret = pcim_enable_device(pdev);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to enable PCI device\n");

	hp_ctrl->base = pcim_iomap_region(pdev, 0, OCTEP_HP_DRV_NAME);
	if (IS_ERR(hp_ctrl->base))
		return dev_err_probe(dev, PTR_ERR(hp_ctrl->base),
				     "Failed to map PCI device region\n");

	pci_set_master(pdev);
	pci_set_drvdata(pdev, hp_ctrl);

	INIT_LIST_HEAD(&hp_ctrl->slot_list);
	INIT_LIST_HEAD(&hp_ctrl->hp_cmd_list);
	mutex_init(&hp_ctrl->slot_lock);
	spin_lock_init(&hp_ctrl->hp_cmd_lock);
	INIT_WORK(&hp_ctrl->work, octep_hp_work_handler);
	hp_ctrl->pdev = pdev;

	ret = pci_alloc_irq_vectors(pdev, 1,
				    OCTEP_HP_INTR_VECTOR(OCTEP_HP_INTR_MAX),
				    PCI_IRQ_MSIX);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to alloc MSI-X vectors\n");

	ret = devm_add_action(&pdev->dev, octep_hp_irq_cleanup, hp_ctrl);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "Failed to add IRQ cleanup action\n");

	for (type = OCTEP_HP_INTR_ENA; type < OCTEP_HP_INTR_MAX; type++) {
		ret = octep_hp_request_irq(hp_ctrl, type);
		if (ret)
			return dev_err_probe(dev, ret,
					     "Failed to request IRQ for vector %d\n",
					     OCTEP_HP_INTR_VECTOR(type));
	}

	return 0;
}

static int octep_hp_pci_probe(struct pci_dev *pdev,
			      const struct pci_device_id *id)
{
	struct octep_hp_controller *hp_ctrl;
	struct pci_dev *tmp_pdev, *next;
	struct octep_hp_slot *hp_slot;
	u16 slot_number = 0;
	int ret;

	hp_ctrl = devm_kzalloc(&pdev->dev, sizeof(*hp_ctrl), GFP_KERNEL);
	if (!hp_ctrl)
		return -ENOMEM;

	ret = octep_hp_controller_setup(pdev, hp_ctrl);
	if (ret)
		return ret;

	/*
	 * Register all hotplug slots. Hotplug controller is the first function
	 * of the PCI device. The hotplug slots are the remaining functions of
	 * the PCI device. The hotplug slot functions are logically removed from
	 * the bus during probing and are re-enabled by the driver when a
	 * hotplug event is received.
	 */
	list_for_each_entry_safe(tmp_pdev, next, &pdev->bus->devices, bus_list) {
		if (tmp_pdev == pdev)
			continue;

		hp_slot = octep_hp_register_slot(hp_ctrl, tmp_pdev, slot_number);
		if (IS_ERR(hp_slot))
			return dev_err_probe(&pdev->dev, PTR_ERR(hp_slot),
					     "Failed to register hotplug slot %u\n",
					     slot_number);

		ret = devm_add_action(&pdev->dev, octep_hp_deregister_slot,
				      hp_slot);
		if (ret)
			return dev_err_probe(&pdev->dev, ret,
					     "Failed to add action for deregistering slot %u\n",
					     slot_number);
		slot_number++;
	}

	return 0;
}

#define PCI_DEVICE_ID_CAVIUM_OCTEP_HP_CTLR  0xa0e3
static struct pci_device_id octep_hp_pci_map[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, PCI_DEVICE_ID_CAVIUM_OCTEP_HP_CTLR) },
	{ },
};

static struct pci_driver octep_hp = {
	.name = OCTEP_HP_DRV_NAME,
	.id_table = octep_hp_pci_map,
	.probe = octep_hp_pci_probe,
};

module_pci_driver(octep_hp);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Marvell");
MODULE_DESCRIPTION("Marvell OCTEON PCI Hotplug driver");
