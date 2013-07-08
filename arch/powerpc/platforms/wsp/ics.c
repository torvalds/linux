/*
 * Copyright 2008-2011 IBM Corporation.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */

#include <linux/cpu.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/msi.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/xics.h>

#include "wsp.h"
#include "ics.h"


/* WSP ICS */

struct wsp_ics {
	struct ics ics;
	struct device_node *dn;
	void __iomem *regs;
	spinlock_t lock;
	unsigned long *bitmap;
	u32 chip_id;
	u32 lsi_base;
	u32 lsi_count;
	u64 hwirq_start;
	u64 count;
#ifdef CONFIG_SMP
	int *hwirq_cpu_map;
#endif
};

#define to_wsp_ics(ics)	container_of(ics, struct wsp_ics, ics)

#define INT_SRC_LAYER_BUID_REG(base)	((base) + 0x00)
#define IODA_TBL_ADDR_REG(base)		((base) + 0x18)
#define IODA_TBL_DATA_REG(base)		((base) + 0x20)
#define XIVE_UPDATE_REG(base)		((base) + 0x28)
#define ICS_INT_CAPS_REG(base)		((base) + 0x30)

#define TBL_AUTO_INCREMENT	((1UL << 63) | (1UL << 15))
#define TBL_SELECT_XIST		(1UL << 48)
#define TBL_SELECT_XIVT		(1UL << 49)

#define IODA_IRQ(irq)		((irq) & (0x7FFULL))	/* HRM 5.1.3.4 */

#define XIST_REQUIRED		0x8
#define XIST_REJECTED		0x4
#define XIST_PRESENTED		0x2
#define XIST_PENDING		0x1

#define XIVE_SERVER_SHIFT	42
#define XIVE_SERVER_MASK	0xFFFFULL
#define XIVE_PRIORITY_MASK	0xFFULL
#define XIVE_PRIORITY_SHIFT	32
#define XIVE_WRITE_ENABLE	(1ULL << 63)

/*
 * The docs refer to a 6 bit field called ChipID, which consists of a
 * 3 bit NodeID and a 3 bit ChipID. On WSP the ChipID is always zero
 * so we ignore it, and every where we use "chip id" in this code we
 * mean the NodeID.
 */
#define WSP_ICS_CHIP_SHIFT		17


static struct wsp_ics *ics_list;
static int num_ics;

/* ICS Source controller accessors */

static u64 wsp_ics_get_xive(struct wsp_ics *ics, unsigned int irq)
{
	unsigned long flags;
	u64 xive;

	spin_lock_irqsave(&ics->lock, flags);
	out_be64(IODA_TBL_ADDR_REG(ics->regs), TBL_SELECT_XIVT | IODA_IRQ(irq));
	xive = in_be64(IODA_TBL_DATA_REG(ics->regs));
	spin_unlock_irqrestore(&ics->lock, flags);

	return xive;
}

static void wsp_ics_set_xive(struct wsp_ics *ics, unsigned int irq, u64 xive)
{
	xive &= ~XIVE_ADDR_MASK;
	xive |= (irq & XIVE_ADDR_MASK);
	xive |= XIVE_WRITE_ENABLE;

	out_be64(XIVE_UPDATE_REG(ics->regs), xive);
}

static u64 xive_set_server(u64 xive, unsigned int server)
{
	u64 mask = ~(XIVE_SERVER_MASK << XIVE_SERVER_SHIFT);

	xive &= mask;
	xive |= (server & XIVE_SERVER_MASK) << XIVE_SERVER_SHIFT;

	return xive;
}

static u64 xive_set_priority(u64 xive, unsigned int priority)
{
	u64 mask = ~(XIVE_PRIORITY_MASK << XIVE_PRIORITY_SHIFT);

	xive &= mask;
	xive |= (priority & XIVE_PRIORITY_MASK) << XIVE_PRIORITY_SHIFT;

	return xive;
}


#ifdef CONFIG_SMP
/* Find logical CPUs within mask on a given chip and store result in ret */
void cpus_on_chip(int chip_id, cpumask_t *mask, cpumask_t *ret)
{
	int cpu, chip;
	struct device_node *cpu_dn, *dn;
	const u32 *prop;

	cpumask_clear(ret);
	for_each_cpu(cpu, mask) {
		cpu_dn = of_get_cpu_node(cpu, NULL);
		if (!cpu_dn)
			continue;

		prop = of_get_property(cpu_dn, "at-node", NULL);
		if (!prop) {
			of_node_put(cpu_dn);
			continue;
		}

		dn = of_find_node_by_phandle(*prop);
		of_node_put(cpu_dn);

		chip = wsp_get_chip_id(dn);
		if (chip == chip_id)
			cpumask_set_cpu(cpu, ret);

		of_node_put(dn);
	}
}

/* Store a suitable CPU to handle a hwirq in the ics->hwirq_cpu_map cache */
static int cache_hwirq_map(struct wsp_ics *ics, unsigned int hwirq,
			   const cpumask_t *affinity)
{
	cpumask_var_t avail, newmask;
	int ret = -ENOMEM, cpu, cpu_rover = 0, target;
	int index = hwirq - ics->hwirq_start;
	unsigned int nodeid;

	BUG_ON(index < 0 || index >= ics->count);

	if (!ics->hwirq_cpu_map)
		return -ENOMEM;

	if (!distribute_irqs) {
		ics->hwirq_cpu_map[hwirq - ics->hwirq_start] = xics_default_server;
		return 0;
	}

	/* Allocate needed CPU masks */
	if (!alloc_cpumask_var(&avail, GFP_KERNEL))
		goto ret;
	if (!alloc_cpumask_var(&newmask, GFP_KERNEL))
		goto freeavail;

	/* Find PBus attached to the source of this IRQ */
	nodeid = (hwirq >> WSP_ICS_CHIP_SHIFT) & 0x3; /* 12:14 */

	/* Find CPUs that could handle this IRQ */
	if (affinity)
		cpumask_and(avail, cpu_online_mask, affinity);
	else
		cpumask_copy(avail, cpu_online_mask);

	/* Narrow selection down to logical CPUs on the same chip */
	cpus_on_chip(nodeid, avail, newmask);

	/* Ensure we haven't narrowed it down to 0 */
	if (unlikely(cpumask_empty(newmask))) {
		if (unlikely(cpumask_empty(avail))) {
			ret = -1;
			goto out;
		}
		cpumask_copy(newmask, avail);
	}

	/* Choose a CPU out of those we narrowed it down to in round robin */
	target = hwirq % cpumask_weight(newmask);
	for_each_cpu(cpu, newmask) {
		if (cpu_rover++ >= target) {
			ics->hwirq_cpu_map[index] = get_hard_smp_processor_id(cpu);
			ret = 0;
			goto out;
		}
	}

	/* Shouldn't happen */
	WARN_ON(1);

out:
	free_cpumask_var(newmask);
freeavail:
	free_cpumask_var(avail);
ret:
	if (ret < 0) {
		ics->hwirq_cpu_map[index] = cpumask_first(cpu_online_mask);
		pr_warning("Error, falling hwirq 0x%x routing back to CPU %i\n",
			   hwirq, ics->hwirq_cpu_map[index]);
	}
	return ret;
}

static void alloc_irq_map(struct wsp_ics *ics)
{
	int i;

	ics->hwirq_cpu_map = kmalloc(sizeof(int) * ics->count, GFP_KERNEL);
	if (!ics->hwirq_cpu_map) {
		pr_warning("Allocate hwirq_cpu_map failed, "
			   "IRQ balancing disabled\n");
		return;
	}

	for (i=0; i < ics->count; i++)
		ics->hwirq_cpu_map[i] = xics_default_server;
}

static int get_irq_server(struct wsp_ics *ics, unsigned int hwirq)
{
	int index = hwirq - ics->hwirq_start;

	BUG_ON(index < 0 || index >= ics->count);

	if (!ics->hwirq_cpu_map)
		return xics_default_server;

	return ics->hwirq_cpu_map[index];
}
#else /* !CONFIG_SMP */
static int cache_hwirq_map(struct wsp_ics *ics, unsigned int hwirq,
			   const cpumask_t *affinity)
{
	return 0;
}

static int get_irq_server(struct wsp_ics *ics, unsigned int hwirq)
{
	return xics_default_server;
}

static void alloc_irq_map(struct wsp_ics *ics) { }
#endif

static void wsp_chip_unmask_irq(struct irq_data *d)
{
	unsigned int hw_irq = (unsigned int)irqd_to_hwirq(d);
	struct wsp_ics *ics;
	int server;
	u64 xive;

	if (hw_irq == XICS_IPI || hw_irq == XICS_IRQ_SPURIOUS)
		return;

	ics = d->chip_data;
	if (WARN_ON(!ics))
		return;

	server = get_irq_server(ics, hw_irq);

	xive = wsp_ics_get_xive(ics, hw_irq);
	xive = xive_set_server(xive, server);
	xive = xive_set_priority(xive, DEFAULT_PRIORITY);
	wsp_ics_set_xive(ics, hw_irq, xive);
}

static unsigned int wsp_chip_startup(struct irq_data *d)
{
	/* unmask it */
	wsp_chip_unmask_irq(d);
	return 0;
}

static void wsp_mask_real_irq(unsigned int hw_irq, struct wsp_ics *ics)
{
	u64 xive;

	if (hw_irq == XICS_IPI)
		return;

	if (WARN_ON(!ics))
		return;
	xive = wsp_ics_get_xive(ics, hw_irq);
	xive = xive_set_server(xive, xics_default_server);
	xive = xive_set_priority(xive, LOWEST_PRIORITY);
	wsp_ics_set_xive(ics, hw_irq, xive);
}

static void wsp_chip_mask_irq(struct irq_data *d)
{
	unsigned int hw_irq = (unsigned int)irqd_to_hwirq(d);
	struct wsp_ics *ics = d->chip_data;

	if (hw_irq == XICS_IPI || hw_irq == XICS_IRQ_SPURIOUS)
		return;

	wsp_mask_real_irq(hw_irq, ics);
}

static int wsp_chip_set_affinity(struct irq_data *d,
				 const struct cpumask *cpumask, bool force)
{
	unsigned int hw_irq = (unsigned int)irqd_to_hwirq(d);
	struct wsp_ics *ics;
	int ret;
	u64 xive;

	if (hw_irq == XICS_IPI || hw_irq == XICS_IRQ_SPURIOUS)
		return -1;

	ics = d->chip_data;
	if (WARN_ON(!ics))
		return -1;
	xive = wsp_ics_get_xive(ics, hw_irq);

	/*
	 * For the moment only implement delivery to all cpus or one cpu.
	 * Get current irq_server for the given irq
	 */
	ret = cache_hwirq_map(ics, hw_irq, cpumask);
	if (ret == -1) {
		char cpulist[128];
		cpumask_scnprintf(cpulist, sizeof(cpulist), cpumask);
		pr_warning("%s: No online cpus in the mask %s for irq %d\n",
			   __func__, cpulist, d->irq);
		return -1;
	} else if (ret == -ENOMEM) {
		pr_warning("%s: Out of memory\n", __func__);
		return -1;
	}

	xive = xive_set_server(xive, get_irq_server(ics, hw_irq));
	wsp_ics_set_xive(ics, hw_irq, xive);

	return IRQ_SET_MASK_OK;
}

static struct irq_chip wsp_irq_chip = {
	.name = "WSP ICS",
	.irq_startup		= wsp_chip_startup,
	.irq_mask		= wsp_chip_mask_irq,
	.irq_unmask		= wsp_chip_unmask_irq,
	.irq_set_affinity	= wsp_chip_set_affinity
};

static int wsp_ics_host_match(struct ics *ics, struct device_node *dn)
{
	/* All ICSs in the system implement a global irq number space,
	 * so match against them all. */
	return of_device_is_compatible(dn, "ibm,ppc-xics");
}

static int wsp_ics_match_hwirq(struct wsp_ics *wsp_ics, unsigned int hwirq)
{
	if (hwirq >= wsp_ics->hwirq_start &&
	    hwirq <  wsp_ics->hwirq_start + wsp_ics->count)
		return 1;

	return 0;
}

static int wsp_ics_map(struct ics *ics, unsigned int virq)
{
	struct wsp_ics *wsp_ics = to_wsp_ics(ics);
	unsigned int hw_irq = virq_to_hw(virq);
	unsigned long flags;

	if (!wsp_ics_match_hwirq(wsp_ics, hw_irq))
		return -ENOENT;

	irq_set_chip_and_handler(virq, &wsp_irq_chip, handle_fasteoi_irq);

	irq_set_chip_data(virq, wsp_ics);

	spin_lock_irqsave(&wsp_ics->lock, flags);
	bitmap_allocate_region(wsp_ics->bitmap, hw_irq - wsp_ics->hwirq_start, 0);
	spin_unlock_irqrestore(&wsp_ics->lock, flags);

	return 0;
}

static void wsp_ics_mask_unknown(struct ics *ics, unsigned long hw_irq)
{
	struct wsp_ics *wsp_ics = to_wsp_ics(ics);

	if (!wsp_ics_match_hwirq(wsp_ics, hw_irq))
		return;

	pr_err("%s: IRQ %lu (real) is invalid, disabling it.\n", __func__, hw_irq);
	wsp_mask_real_irq(hw_irq, wsp_ics);
}

static long wsp_ics_get_server(struct ics *ics, unsigned long hw_irq)
{
	struct wsp_ics *wsp_ics = to_wsp_ics(ics);

	if (!wsp_ics_match_hwirq(wsp_ics, hw_irq))
		return -ENOENT;

	return get_irq_server(wsp_ics, hw_irq);
}

/* HW Number allocation API */

static struct wsp_ics *wsp_ics_find_dn_ics(struct device_node *dn)
{
	struct device_node *iparent;
	int i;

	iparent = of_irq_find_parent(dn);
	if (!iparent) {
		pr_err("wsp_ics: Failed to find interrupt parent!\n");
		return NULL;
	}

	for(i = 0; i < num_ics; i++) {
		if(ics_list[i].dn == iparent)
			break;
	}

	if (i >= num_ics) {
		pr_err("wsp_ics: Unable to find parent bitmap!\n");
		return NULL;
	}

	return &ics_list[i];
}

int wsp_ics_alloc_irq(struct device_node *dn, int num)
{
	struct wsp_ics *ics;
	int order, offset;

	ics = wsp_ics_find_dn_ics(dn);
	if (!ics)
		return -ENODEV;

	/* Fast, but overly strict if num isn't a power of two */
	order = get_count_order(num);

	spin_lock_irq(&ics->lock);
	offset = bitmap_find_free_region(ics->bitmap, ics->count, order);
	spin_unlock_irq(&ics->lock);

	if (offset < 0)
		return offset;

	return offset + ics->hwirq_start;
}

void wsp_ics_free_irq(struct device_node *dn, unsigned int irq)
{
	struct wsp_ics *ics;

	ics = wsp_ics_find_dn_ics(dn);
	if (WARN_ON(!ics))
		return;

	spin_lock_irq(&ics->lock);
	bitmap_release_region(ics->bitmap, irq, 0);
	spin_unlock_irq(&ics->lock);
}

/* Initialisation */

static int __init wsp_ics_bitmap_setup(struct wsp_ics *ics,
				      struct device_node *dn)
{
	int len, i, j, size;
	u32 start, count;
	const u32 *p;

	size = BITS_TO_LONGS(ics->count) * sizeof(long);
	ics->bitmap = kzalloc(size, GFP_KERNEL);
	if (!ics->bitmap) {
		pr_err("wsp_ics: ENOMEM allocating IRQ bitmap!\n");
		return -ENOMEM;
	}

	spin_lock_init(&ics->lock);

	p = of_get_property(dn, "available-ranges", &len);
	if (!p || !len) {
		/* FIXME this should be a WARN() once mambo is updated */
		pr_err("wsp_ics: No available-ranges defined for %s\n",
			dn->full_name);
		return 0;
	}

	if (len % (2 * sizeof(u32)) != 0) {
		/* FIXME this should be a WARN() once mambo is updated */
		pr_err("wsp_ics: Invalid available-ranges for %s\n",
			dn->full_name);
		return 0;
	}

	bitmap_fill(ics->bitmap, ics->count);

	for (i = 0; i < len / sizeof(u32); i += 2) {
		start = of_read_number(p + i, 1);
		count = of_read_number(p + i + 1, 1);

		pr_devel("%s: start: %d count: %d\n", __func__, start, count);

		if ((start + count) > (ics->hwirq_start + ics->count) ||
		     start < ics->hwirq_start) {
			pr_err("wsp_ics: Invalid range! -> %d to %d\n",
					start, start + count);
			break;
		}

		for (j = 0; j < count; j++)
			bitmap_release_region(ics->bitmap,
				(start + j) - ics->hwirq_start, 0);
	}

	/* Ensure LSIs are not available for allocation */
	bitmap_allocate_region(ics->bitmap, ics->lsi_base,
			       get_count_order(ics->lsi_count));

	return 0;
}

static int __init wsp_ics_setup(struct wsp_ics *ics, struct device_node *dn)
{
	u32 lsi_buid, msi_buid, msi_base, msi_count;
	void __iomem *regs;
	const u32 *p;
	int rc, len, i;
	u64 caps, buid;

	p = of_get_property(dn, "interrupt-ranges", &len);
	if (!p || len < (2 * sizeof(u32))) {
		pr_err("wsp_ics: No/bad interrupt-ranges found on %s\n",
			dn->full_name);
		return -ENOENT;
	}

	if (len > (2 * sizeof(u32))) {
		pr_err("wsp_ics: Multiple ics ranges not supported.\n");
		return -EINVAL;
	}

	regs = of_iomap(dn, 0);
	if (!regs) {
		pr_err("wsp_ics: of_iomap(%s) failed\n", dn->full_name);
		return -ENXIO;
	}

	ics->hwirq_start = of_read_number(p, 1);
	ics->count = of_read_number(p + 1, 1);
	ics->regs = regs;

	ics->chip_id = wsp_get_chip_id(dn);
	if (WARN_ON(ics->chip_id < 0))
		ics->chip_id = 0;

	/* Get some informations about the critter */
	caps = in_be64(ICS_INT_CAPS_REG(ics->regs));
	buid = in_be64(INT_SRC_LAYER_BUID_REG(ics->regs));
	ics->lsi_count = caps >> 56;
	msi_count = (caps >> 44) & 0x7ff;

	/* Note: LSI BUID is 9 bits, but really only 3 are BUID and the
	 * rest is mixed in the interrupt number. We store the whole
	 * thing though
	 */
	lsi_buid = (buid >> 48) & 0x1ff;
	ics->lsi_base = (ics->chip_id << WSP_ICS_CHIP_SHIFT) | lsi_buid << 5;
	msi_buid = (buid >> 37) & 0x7;
	msi_base = (ics->chip_id << WSP_ICS_CHIP_SHIFT) | msi_buid << 11;

	pr_info("wsp_ics: Found %s\n", dn->full_name);
	pr_info("wsp_ics:    irq range : 0x%06llx..0x%06llx\n",
		ics->hwirq_start, ics->hwirq_start + ics->count - 1);
	pr_info("wsp_ics:    %4d LSIs : 0x%06x..0x%06x\n",
		ics->lsi_count, ics->lsi_base,
		ics->lsi_base + ics->lsi_count - 1);
	pr_info("wsp_ics:    %4d MSIs : 0x%06x..0x%06x\n",
		msi_count, msi_base,
		msi_base + msi_count - 1);

	/* Let's check the HW config is sane */
	if (ics->lsi_base < ics->hwirq_start ||
	    (ics->lsi_base + ics->lsi_count) > (ics->hwirq_start + ics->count))
		pr_warning("wsp_ics: WARNING ! LSIs out of interrupt-ranges !\n");
	if (msi_base < ics->hwirq_start ||
	    (msi_base + msi_count) > (ics->hwirq_start + ics->count))
		pr_warning("wsp_ics: WARNING ! MSIs out of interrupt-ranges !\n");

	/* We don't check for overlap between LSI and MSI, which will happen
	 * if we use the same BUID, I'm not sure yet how legit that is.
	 */

	rc = wsp_ics_bitmap_setup(ics, dn);
	if (rc) {
		iounmap(regs);
		return rc;
	}

	ics->dn = of_node_get(dn);
	alloc_irq_map(ics);

	for(i = 0; i < ics->count; i++)
		wsp_mask_real_irq(ics->hwirq_start + i, ics);

	ics->ics.map = wsp_ics_map;
	ics->ics.mask_unknown = wsp_ics_mask_unknown;
	ics->ics.get_server = wsp_ics_get_server;
	ics->ics.host_match = wsp_ics_host_match;

	xics_register_ics(&ics->ics);

	return 0;
}

static void __init wsp_ics_set_default_server(void)
{
	struct device_node *np;
	u32 hwid;

	/* Find the server number for the boot cpu. */
	np = of_get_cpu_node(boot_cpuid, NULL);
	BUG_ON(!np);

	hwid = get_hard_smp_processor_id(boot_cpuid);

	pr_info("wsp_ics: default server is %#x, CPU %s\n", hwid, np->full_name);
	xics_default_server = hwid;

	of_node_put(np);
}

static int __init wsp_ics_init(void)
{
	struct device_node *dn;
	struct wsp_ics *ics;
	int rc, found;

	wsp_ics_set_default_server();

	found = 0;
	for_each_compatible_node(dn, NULL, "ibm,ppc-xics")
		found++;

	if (found == 0) {
		pr_err("wsp_ics: No ICS's found!\n");
		return -ENODEV;
	}

	ics_list = kmalloc(sizeof(*ics) * found, GFP_KERNEL);
	if (!ics_list) {
		pr_err("wsp_ics: No memory for structs.\n");
		return -ENOMEM;
	}

	num_ics = 0;
	ics = ics_list;
	for_each_compatible_node(dn, NULL, "ibm,wsp-xics") {
		rc = wsp_ics_setup(ics, dn);
		if (rc == 0) {
			ics++;
			num_ics++;
		}
	}

	if (found != num_ics) {
		pr_err("wsp_ics: Failed setting up %d ICS's\n",
			found - num_ics);
		return -1;
	}

	return 0;
}

void __init wsp_init_irq(void)
{
	wsp_ics_init();
	xics_init();

	/* We need to patch our irq chip's EOI to point to the right ICP */
	wsp_irq_chip.irq_eoi = icp_ops->eoi;
}

#ifdef CONFIG_PCI_MSI
static void wsp_ics_msi_unmask_irq(struct irq_data *d)
{
	wsp_chip_unmask_irq(d);
	unmask_msi_irq(d);
}

static unsigned int wsp_ics_msi_startup(struct irq_data *d)
{
	wsp_ics_msi_unmask_irq(d);
	return 0;
}

static void wsp_ics_msi_mask_irq(struct irq_data *d)
{
	mask_msi_irq(d);
	wsp_chip_mask_irq(d);
}

/*
 * we do it this way because we reassinge default EOI handling in
 * irq_init() above
 */
static void wsp_ics_eoi(struct irq_data *data)
{
	wsp_irq_chip.irq_eoi(data);
}

static struct irq_chip wsp_ics_msi = {
	.name = "WSP ICS MSI",
	.irq_startup = wsp_ics_msi_startup,
	.irq_mask = wsp_ics_msi_mask_irq,
	.irq_unmask = wsp_ics_msi_unmask_irq,
	.irq_eoi = wsp_ics_eoi,
	.irq_set_affinity = wsp_chip_set_affinity
};

void wsp_ics_set_msi_chip(unsigned int irq)
{
	irq_set_chip(irq, &wsp_ics_msi);
}

void wsp_ics_set_std_chip(unsigned int irq)
{
	irq_set_chip(irq, &wsp_irq_chip);
}
#endif /* CONFIG_PCI_MSI */
