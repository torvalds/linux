/* sbus.c: SBus support routines.
 *
 * Copyright (C) 1995, 2006 David S. Miller (davem@davemloft.net)
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/pci.h>

#include <asm/system.h>
#include <asm/sbus.h>
#include <asm/dma.h>
#include <asm/oplib.h>
#include <asm/prom.h>
#include <asm/of_device.h>
#include <asm/bpp.h>
#include <asm/irq.h>

struct sbus_bus *sbus_root;

static void __init fill_sbus_device(struct device_node *dp, struct sbus_dev *sdev)
{
	unsigned long base;
	void *pval;
	int len;

	sdev->prom_node = dp->node;
	strcpy(sdev->prom_name, dp->name);

	pval = of_get_property(dp, "reg", &len);
	sdev->num_registers = 0;
	if (pval) {
		memcpy(sdev->reg_addrs, pval, len);

		sdev->num_registers =
			len / sizeof(struct linux_prom_registers);

		base = (unsigned long) sdev->reg_addrs[0].phys_addr;

		/* Compute the slot number. */
		if (base >= SUN_SBUS_BVADDR && sparc_cpu_model == sun4m)
			sdev->slot = sbus_dev_slot(base);
		else
			sdev->slot = sdev->reg_addrs[0].which_io;
	}

	pval = of_get_property(dp, "ranges", &len);
	sdev->num_device_ranges = 0;
	if (pval) {
		memcpy(sdev->device_ranges, pval, len);
		sdev->num_device_ranges =
			len / sizeof(struct linux_prom_ranges);
	}

	sbus_fill_device_irq(sdev);

	sdev->ofdev.node = dp;
	if (sdev->parent)
		sdev->ofdev.dev.parent = &sdev->parent->ofdev.dev;
	else
		sdev->ofdev.dev.parent = &sdev->bus->ofdev.dev;
	sdev->ofdev.dev.bus = &sbus_bus_type;
	sprintf(sdev->ofdev.dev.bus_id, "sbus[%08x]", dp->node);

	if (of_device_register(&sdev->ofdev) != 0)
		printk(KERN_DEBUG "sbus: device registration error for %s!\n",
		       dp->path_component_name);
}

static void __init sbus_bus_ranges_init(struct device_node *dp, struct sbus_bus *sbus)
{
	void *pval;
	int len;

	pval = of_get_property(dp, "ranges", &len);
	sbus->num_sbus_ranges = 0;
	if (pval) {
		memcpy(sbus->sbus_ranges, pval, len);
		sbus->num_sbus_ranges =
			len / sizeof(struct linux_prom_ranges);

		sbus_arch_bus_ranges_init(dp->parent, sbus);
	}
}

static void __init __apply_ranges_to_regs(struct linux_prom_ranges *ranges,
					  int num_ranges,
					  struct linux_prom_registers *regs,
					  int num_regs)
{
	if (num_ranges) {
		int regnum;

		for (regnum = 0; regnum < num_regs; regnum++) {
			int rngnum;

			for (rngnum = 0; rngnum < num_ranges; rngnum++) {
				if (regs[regnum].which_io == ranges[rngnum].ot_child_space)
					break;
			}
			if (rngnum == num_ranges) {
				/* We used to flag this as an error.  Actually
				 * some devices do not report the regs as we expect.
				 * For example, see SUNW,pln device.  In that case
				 * the reg property is in a format internal to that
				 * node, ie. it is not in the SBUS register space
				 * per se. -DaveM
				 */
				return;
			}
			regs[regnum].which_io = ranges[rngnum].ot_parent_space;
			regs[regnum].phys_addr -= ranges[rngnum].ot_child_base;
			regs[regnum].phys_addr += ranges[rngnum].ot_parent_base;
		}
	}
}

static void __init __fixup_regs_sdev(struct sbus_dev *sdev)
{
	if (sdev->num_registers != 0) {
		struct sbus_dev *parent = sdev->parent;
		int i;

		while (parent != NULL) {
			__apply_ranges_to_regs(parent->device_ranges,
					       parent->num_device_ranges,
					       sdev->reg_addrs,
					       sdev->num_registers);

			parent = parent->parent;
		}

		__apply_ranges_to_regs(sdev->bus->sbus_ranges,
				       sdev->bus->num_sbus_ranges,
				       sdev->reg_addrs,
				       sdev->num_registers);

		for (i = 0; i < sdev->num_registers; i++) {
			struct resource *res = &sdev->resource[i];

			res->start = sdev->reg_addrs[i].phys_addr;
			res->end = (res->start +
				    (unsigned long)sdev->reg_addrs[i].reg_size - 1UL);
			res->flags = IORESOURCE_IO |
				(sdev->reg_addrs[i].which_io & 0xff);
		}
	}
}

static void __init sbus_fixup_all_regs(struct sbus_dev *first_sdev)
{
	struct sbus_dev *sdev;

	for (sdev = first_sdev; sdev; sdev = sdev->next) {
		if (sdev->child)
			sbus_fixup_all_regs(sdev->child);
		__fixup_regs_sdev(sdev);
	}
}

/* We preserve the "probe order" of these bus and device lists to give
 * the same ordering as the old code.
 */
static void __init sbus_insert(struct sbus_bus *sbus, struct sbus_bus **root)
{
	while (*root)
		root = &(*root)->next;
	*root = sbus;
	sbus->next = NULL;
}

static void __init sdev_insert(struct sbus_dev *sdev, struct sbus_dev **root)
{
	while (*root)
		root = &(*root)->next;
	*root = sdev;
	sdev->next = NULL;
}

static void __init walk_children(struct device_node *dp, struct sbus_dev *parent, struct sbus_bus *sbus)
{
	dp = dp->child;
	while (dp) {
		struct sbus_dev *sdev;

		sdev = kzalloc(sizeof(struct sbus_dev), GFP_ATOMIC);
		if (sdev) {
			sdev_insert(sdev, &parent->child);

			sdev->bus = sbus;
			sdev->parent = parent;

			fill_sbus_device(dp, sdev);

			walk_children(dp, sdev, sbus);
		}
		dp = dp->sibling;
	}
}

static void __init build_one_sbus(struct device_node *dp, int num_sbus)
{
	struct sbus_bus *sbus;
	unsigned int sbus_clock;
	struct device_node *dev_dp;

	sbus = kzalloc(sizeof(struct sbus_bus), GFP_ATOMIC);
	if (!sbus)
		return;

	sbus_insert(sbus, &sbus_root);
	sbus->prom_node = dp->node;

	sbus_setup_iommu(sbus, dp);

	printk("sbus%d: ", num_sbus);

	sbus_clock = of_getintprop_default(dp, "clock-frequency",
					   (25*1000*1000));
	sbus->clock_freq = sbus_clock;

	printk("Clock %d.%d MHz\n", (int) ((sbus_clock/1000)/1000),
	       (int) (((sbus_clock/1000)%1000 != 0) ? 
		      (((sbus_clock/1000)%1000) + 1000) : 0));

	strcpy(sbus->prom_name, dp->name);

	sbus_setup_arch_props(sbus, dp);

	sbus_bus_ranges_init(dp, sbus);

	sbus->ofdev.node = dp;
	sbus->ofdev.dev.parent = NULL;
	sbus->ofdev.dev.bus = &sbus_bus_type;
	sprintf(sbus->ofdev.dev.bus_id, "sbus%d", num_sbus);

	if (of_device_register(&sbus->ofdev) != 0)
		printk(KERN_DEBUG "sbus: device registration error for %s!\n",
		       sbus->ofdev.dev.bus_id);

	dev_dp = dp->child;
	while (dev_dp) {
		struct sbus_dev *sdev;

		sdev = kzalloc(sizeof(struct sbus_dev), GFP_ATOMIC);
		if (sdev) {
			sdev_insert(sdev, &sbus->devices);

			sdev->bus = sbus;
			sdev->parent = NULL;
			fill_sbus_device(dev_dp, sdev);

			walk_children(dev_dp, sdev, sbus);
		}
		dev_dp = dev_dp->sibling;
	}

	sbus_fixup_all_regs(sbus->devices);

	dvma_init(sbus);
}

static int __init sbus_init(void)
{
	struct device_node *dp;
	const char *sbus_name = "sbus";
	int num_sbus = 0;

	if (sbus_arch_preinit())
		return 0;

	if (sparc_cpu_model == sun4d)
		sbus_name = "sbi";

	for_each_node_by_name(dp, sbus_name) {
		build_one_sbus(dp, num_sbus);
		num_sbus++;

	}

	sbus_arch_postinit();

	return 0;
}

subsys_initcall(sbus_init);
