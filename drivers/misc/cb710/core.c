/*
 *  cb710/core.c
 *
 *  Copyright by Michał Mirosław, 2008-2009
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/idr.h>
#include <linux/cb710.h>
#include <linux/gfp.h>

static DEFINE_IDA(cb710_ida);
static DEFINE_SPINLOCK(cb710_ida_lock);

void cb710_pci_update_config_reg(struct pci_dev *pdev,
	int reg, uint32_t mask, uint32_t xor)
{
	u32 rval;

	pci_read_config_dword(pdev, reg, &rval);
	rval = (rval & mask) ^ xor;
	pci_write_config_dword(pdev, reg, rval);
}
EXPORT_SYMBOL_GPL(cb710_pci_update_config_reg);

/* Some magic writes based on Windows driver init code */
static int cb710_pci_configure(struct pci_dev *pdev)
{
	unsigned int devfn = PCI_DEVFN(PCI_SLOT(pdev->devfn), 0);
	struct pci_dev *pdev0;
	u32 val;

	cb710_pci_update_config_reg(pdev, 0x48,
		~0x000000FF, 0x0000003F);

	pci_read_config_dword(pdev, 0x48, &val);
	if (val & 0x80000000)
		return 0;

	pdev0 = pci_get_slot(pdev->bus, devfn);
	if (!pdev0)
		return -ENODEV;

	if (pdev0->vendor == PCI_VENDOR_ID_ENE
	    && pdev0->device == PCI_DEVICE_ID_ENE_720) {
		cb710_pci_update_config_reg(pdev0, 0x8C,
			~0x00F00000, 0x00100000);
		cb710_pci_update_config_reg(pdev0, 0xB0,
			~0x08000000, 0x08000000);
	}

	cb710_pci_update_config_reg(pdev0, 0x8C,
		~0x00000F00, 0x00000200);
	cb710_pci_update_config_reg(pdev0, 0x90,
		~0x00060000, 0x00040000);

	pci_dev_put(pdev0);

	return 0;
}

static irqreturn_t cb710_irq_handler(int irq, void *data)
{
	struct cb710_chip *chip = data;
	struct cb710_slot *slot = &chip->slot[0];
	irqreturn_t handled = IRQ_NONE;
	unsigned nr;

	spin_lock(&chip->irq_lock); /* incl. smp_rmb() */

	for (nr = chip->slots; nr; ++slot, --nr) {
		cb710_irq_handler_t handler_func = slot->irq_handler;
		if (handler_func && handler_func(slot))
			handled = IRQ_HANDLED;
	}

	spin_unlock(&chip->irq_lock);

	return handled;
}

static void cb710_release_slot(struct device *dev)
{
#ifdef CONFIG_CB710_DEBUG_ASSUMPTIONS
	struct cb710_slot *slot = cb710_pdev_to_slot(to_platform_device(dev));
	struct cb710_chip *chip = cb710_slot_to_chip(slot);

	/* slot struct can be freed now */
	atomic_dec(&chip->slot_refs_count);
#endif
}

static int cb710_register_slot(struct cb710_chip *chip,
	unsigned slot_mask, unsigned io_offset, const char *name)
{
	int nr = chip->slots;
	struct cb710_slot *slot = &chip->slot[nr];
	int err;

	dev_dbg(cb710_chip_dev(chip),
		"register: %s.%d; slot %d; mask %d; IO offset: 0x%02X\n",
		name, chip->platform_id, nr, slot_mask, io_offset);

	/* slot->irq_handler == NULL here; this needs to be
	 * seen before platform_device_register() */
	++chip->slots;
	smp_wmb();

	slot->iobase = chip->iobase + io_offset;
	slot->pdev.name = name;
	slot->pdev.id = chip->platform_id;
	slot->pdev.dev.parent = &chip->pdev->dev;
	slot->pdev.dev.release = cb710_release_slot;

	err = platform_device_register(&slot->pdev);

#ifdef CONFIG_CB710_DEBUG_ASSUMPTIONS
	atomic_inc(&chip->slot_refs_count);
#endif

	if (err) {
		/* device_initialize() called from platform_device_register()
		 * wants this on error path */
		platform_device_put(&slot->pdev);

		/* slot->irq_handler == NULL here anyway, so no lock needed */
		--chip->slots;
		return err;
	}

	chip->slot_mask |= slot_mask;

	return 0;
}

static void cb710_unregister_slot(struct cb710_chip *chip,
	unsigned slot_mask)
{
	int nr = chip->slots - 1;

	if (!(chip->slot_mask & slot_mask))
		return;

	platform_device_unregister(&chip->slot[nr].pdev);

	/* complementary to spin_unlock() in cb710_set_irq_handler() */
	smp_rmb();
	BUG_ON(chip->slot[nr].irq_handler != NULL);

	/* slot->irq_handler == NULL here, so no lock needed */
	--chip->slots;
	chip->slot_mask &= ~slot_mask;
}

void cb710_set_irq_handler(struct cb710_slot *slot,
	cb710_irq_handler_t handler)
{
	struct cb710_chip *chip = cb710_slot_to_chip(slot);
	unsigned long flags;

	spin_lock_irqsave(&chip->irq_lock, flags);
	slot->irq_handler = handler;
	spin_unlock_irqrestore(&chip->irq_lock, flags);
}
EXPORT_SYMBOL_GPL(cb710_set_irq_handler);

#ifdef CONFIG_PM

static int cb710_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct cb710_chip *chip = pci_get_drvdata(pdev);

	devm_free_irq(&pdev->dev, pdev->irq, chip);
	pci_save_state(pdev);
	pci_disable_device(pdev);
	if (state.event & PM_EVENT_SLEEP)
		pci_set_power_state(pdev, PCI_D3hot);
	return 0;
}

static int cb710_resume(struct pci_dev *pdev)
{
	struct cb710_chip *chip = pci_get_drvdata(pdev);
	int err;

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);
	err = pcim_enable_device(pdev);
	if (err)
		return err;

	return devm_request_irq(&pdev->dev, pdev->irq,
		cb710_irq_handler, IRQF_SHARED, KBUILD_MODNAME, chip);
}

#endif /* CONFIG_PM */

static int cb710_probe(struct pci_dev *pdev,
	const struct pci_device_id *ent)
{
	struct cb710_chip *chip;
	unsigned long flags;
	u32 val;
	int err;
	int n = 0;

	err = cb710_pci_configure(pdev);
	if (err)
		return err;

	/* this is actually magic... */
	pci_read_config_dword(pdev, 0x48, &val);
	if (!(val & 0x80000000)) {
		pci_write_config_dword(pdev, 0x48, val|0x71000000);
		pci_read_config_dword(pdev, 0x48, &val);
	}

	dev_dbg(&pdev->dev, "PCI config[0x48] = 0x%08X\n", val);
	if (!(val & 0x70000000))
		return -ENODEV;
	val = (val >> 28) & 7;
	if (val & CB710_SLOT_MMC)
		++n;
	if (val & CB710_SLOT_MS)
		++n;
	if (val & CB710_SLOT_SM)
		++n;

	chip = devm_kzalloc(&pdev->dev, struct_size(chip, slot, n),
			    GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	err = pcim_enable_device(pdev);
	if (err)
		return err;

	err = pcim_iomap_regions(pdev, 0x0001, KBUILD_MODNAME);
	if (err)
		return err;

	spin_lock_init(&chip->irq_lock);
	chip->pdev = pdev;
	chip->iobase = pcim_iomap_table(pdev)[0];

	pci_set_drvdata(pdev, chip);

	err = devm_request_irq(&pdev->dev, pdev->irq,
		cb710_irq_handler, IRQF_SHARED, KBUILD_MODNAME, chip);
	if (err)
		return err;

	do {
		if (!ida_pre_get(&cb710_ida, GFP_KERNEL))
			return -ENOMEM;

		spin_lock_irqsave(&cb710_ida_lock, flags);
		err = ida_get_new(&cb710_ida, &chip->platform_id);
		spin_unlock_irqrestore(&cb710_ida_lock, flags);

		if (err && err != -EAGAIN)
			return err;
	} while (err);


	dev_info(&pdev->dev, "id %d, IO 0x%p, IRQ %d\n",
		chip->platform_id, chip->iobase, pdev->irq);

	if (val & CB710_SLOT_MMC) {	/* MMC/SD slot */
		err = cb710_register_slot(chip,
			CB710_SLOT_MMC, 0x00, "cb710-mmc");
		if (err)
			return err;
	}

	if (val & CB710_SLOT_MS) {	/* MemoryStick slot */
		err = cb710_register_slot(chip,
			CB710_SLOT_MS, 0x40, "cb710-ms");
		if (err)
			goto unreg_mmc;
	}

	if (val & CB710_SLOT_SM) {	/* SmartMedia slot */
		err = cb710_register_slot(chip,
			CB710_SLOT_SM, 0x60, "cb710-sm");
		if (err)
			goto unreg_ms;
	}

	return 0;
unreg_ms:
	cb710_unregister_slot(chip, CB710_SLOT_MS);
unreg_mmc:
	cb710_unregister_slot(chip, CB710_SLOT_MMC);

#ifdef CONFIG_CB710_DEBUG_ASSUMPTIONS
	BUG_ON(atomic_read(&chip->slot_refs_count) != 0);
#endif
	return err;
}

static void cb710_remove_one(struct pci_dev *pdev)
{
	struct cb710_chip *chip = pci_get_drvdata(pdev);
	unsigned long flags;

	cb710_unregister_slot(chip, CB710_SLOT_SM);
	cb710_unregister_slot(chip, CB710_SLOT_MS);
	cb710_unregister_slot(chip, CB710_SLOT_MMC);
#ifdef CONFIG_CB710_DEBUG_ASSUMPTIONS
	BUG_ON(atomic_read(&chip->slot_refs_count) != 0);
#endif

	spin_lock_irqsave(&cb710_ida_lock, flags);
	ida_remove(&cb710_ida, chip->platform_id);
	spin_unlock_irqrestore(&cb710_ida_lock, flags);
}

static const struct pci_device_id cb710_pci_tbl[] = {
	{ PCI_VENDOR_ID_ENE, PCI_DEVICE_ID_ENE_CB710_FLASH,
		PCI_ANY_ID, PCI_ANY_ID, },
	{ 0, }
};

static struct pci_driver cb710_driver = {
	.name = KBUILD_MODNAME,
	.id_table = cb710_pci_tbl,
	.probe = cb710_probe,
	.remove = cb710_remove_one,
#ifdef CONFIG_PM
	.suspend = cb710_suspend,
	.resume = cb710_resume,
#endif
};

static int __init cb710_init_module(void)
{
	return pci_register_driver(&cb710_driver);
}

static void __exit cb710_cleanup_module(void)
{
	pci_unregister_driver(&cb710_driver);
	ida_destroy(&cb710_ida);
}

module_init(cb710_init_module);
module_exit(cb710_cleanup_module);

MODULE_AUTHOR("Michał Mirosław <mirq-linux@rere.qmqm.pl>");
MODULE_DESCRIPTION("ENE CB710 memory card reader driver");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, cb710_pci_tbl);
