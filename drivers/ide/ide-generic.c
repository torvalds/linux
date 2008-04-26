/*
 * generic/default IDE host driver
 *
 * Copyright (C) 2004, 2008 Bartlomiej Zolnierkiewicz
 * This code was split off from ide.c.  See it for original copyrights.
 *
 * May be copied or modified under the terms of the GNU General Public License.
 */

/*
 * For special cases new interfaces may be added using sysfs, i.e.
 *
 *	echo -n "0x168:0x36e:10" > /sys/class/ide_generic/add
 *
 * will add an interface using I/O ports 0x168-0x16f/0x36e and IRQ 10.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/ide.h>

#define DRV_NAME	"ide_generic"

static ssize_t store_add(struct class *cls, const char *buf, size_t n)
{
	ide_hwif_t *hwif;
	unsigned int base, ctl;
	int irq;
	hw_regs_t hw;
	u8 idx[] = { 0xff, 0xff, 0xff, 0xff };

	if (sscanf(buf, "%x:%x:%d", &base, &ctl, &irq) != 3)
		return -EINVAL;

	hwif = ide_find_port();
	if (hwif == NULL)
		return -ENOENT;

	memset(&hw, 0, sizeof(hw));
	ide_std_init_ports(&hw, base, ctl);
	hw.irq = irq;
	hw.chipset = ide_generic;

	ide_init_port_hw(hwif, &hw);

	idx[0] = hwif->index;

	ide_device_add(idx, NULL);

	return n;
};

static struct class_attribute ide_generic_class_attrs[] = {
	__ATTR(add, S_IWUSR, NULL, store_add),
	__ATTR_NULL
};

static void ide_generic_class_release(struct class *cls)
{
	kfree(cls);
}

static int __init ide_generic_sysfs_init(void)
{
	struct class *cls;
	int rc;

	cls = kzalloc(sizeof(*cls), GFP_KERNEL);
	if (!cls)
		return -ENOMEM;

	cls->name = DRV_NAME;
	cls->owner = THIS_MODULE;
	cls->class_release = ide_generic_class_release;
	cls->class_attrs = ide_generic_class_attrs;

	rc = class_register(cls);
	if (rc) {
		kfree(cls);
		return rc;
	}

	return 0;
}

static int __init ide_generic_init(void)
{
	u8 idx[MAX_HWIFS];
	int i;

	for (i = 0; i < MAX_HWIFS; i++) {
		ide_hwif_t *hwif;
		unsigned long io_addr = ide_default_io_base(i);
		hw_regs_t hw;

		idx[i] = 0xff;

		if (io_addr) {
			if (!request_region(io_addr, 8, DRV_NAME)) {
				printk(KERN_ERR "%s: I/O resource 0x%lX-0x%lX "
						"not free.\n",
						DRV_NAME, io_addr, io_addr + 7);
				continue;
			}

			if (!request_region(io_addr + 0x206, 1, DRV_NAME)) {
				printk(KERN_ERR "%s: I/O resource 0x%lX "
						"not free.\n",
						DRV_NAME, io_addr + 0x206);
				release_region(io_addr, 8);
				continue;
			}

			/*
			 * Skip probing if the corresponding
			 * slot is already occupied.
			 */
			hwif = ide_find_port();
			if (hwif == NULL || hwif->index != i) {
				idx[i] = 0xff;
				continue;
			}

			memset(&hw, 0, sizeof(hw));
			ide_std_init_ports(&hw, io_addr, io_addr + 0x206);
			hw.irq = ide_default_irq(io_addr);
			ide_init_port_hw(hwif, &hw);

			idx[i] = i;
		}
	}

	ide_device_add_all(idx, NULL);

	if (ide_generic_sysfs_init())
		printk(KERN_ERR DRV_NAME ": failed to create ide_generic "
					 "class\n");

	return 0;
}

module_init(ide_generic_init);

MODULE_LICENSE("GPL");
