/* sbus.c: SBus support routines.
 *
 * Copyright (C) 1995, 2006 David S. Miller (davem@davemloft.net)
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/config.h>
#include <linux/init.h>
#include <linux/pci.h>

#include <asm/system.h>
#include <asm/sbus.h>
#include <asm/dma.h>
#include <asm/oplib.h>
#include <asm/bpp.h>
#include <asm/irq.h>

struct sbus_bus *sbus_root;

#ifdef CONFIG_PCI
extern int pcic_present(void);
#endif

static void __init fill_sbus_device(int prom_node, struct sbus_dev *sdev)
{
	unsigned long address, base;
	int len;

	sdev->prom_node = prom_node;
	prom_getstring(prom_node, "name",
		       sdev->prom_name, sizeof(sdev->prom_name));
	address = prom_getint(prom_node, "address");
	len = prom_getproperty(prom_node, "reg",
			       (char *) sdev->reg_addrs,
			       sizeof(sdev->reg_addrs));
	sdev->num_registers = 0;
	if (len != -1) {
		sdev->num_registers =
			len / sizeof(struct linux_prom_registers);
		sdev->ranges_applied = 0;

		base = (unsigned long) sdev->reg_addrs[0].phys_addr;

		/* Compute the slot number. */
		if (base >= SUN_SBUS_BVADDR && sparc_cpu_model == sun4m)
			sdev->slot = sbus_dev_slot(base);
		else
			sdev->slot = sdev->reg_addrs[0].which_io;
	}

	len = prom_getproperty(prom_node, "ranges",
			       (char *)sdev->device_ranges,
			       sizeof(sdev->device_ranges));
	sdev->num_device_ranges = 0;
	if (len != -1)
		sdev->num_device_ranges =
			len / sizeof(struct linux_prom_ranges);

	sbus_fill_device_irq(sdev);
}

/* This routine gets called from whoever needs the sbus first, to scan
 * the SBus device tree.  Currently it just prints out the devices
 * found on the bus and builds trees of SBUS structs and attached
 * devices.
 */

extern void iommu_init(int iommu_node, struct sbus_bus *sbus);
extern void iounit_init(int sbi_node, int iounit_node, struct sbus_bus *sbus);
void sun4_init(void);
#ifdef CONFIG_SUN_AUXIO
extern void auxio_probe(void);
#endif

static void __init sbus_do_child_siblings(int start_node,
					  struct sbus_dev *child,
					  struct sbus_dev *parent,
					  struct sbus_bus *sbus)
{
	struct sbus_dev *this_dev = child;
	int this_node = start_node;

	/* Child already filled in, just need to traverse siblings. */
	child->child = NULL;
	child->parent = parent;
	while((this_node = prom_getsibling(this_node)) != 0) {
		this_dev->next = kmalloc(sizeof(struct sbus_dev), GFP_ATOMIC);
		this_dev = this_dev->next;
		this_dev->next = NULL;
		this_dev->parent = parent;

		this_dev->bus = sbus;
		fill_sbus_device(this_node, this_dev);

		if(prom_getchild(this_node)) {
			this_dev->child = kmalloc(sizeof(struct sbus_dev),
						  GFP_ATOMIC);
			this_dev->child->bus = sbus;
			this_dev->child->next = NULL;
			fill_sbus_device(prom_getchild(this_node), this_dev->child);
			sbus_do_child_siblings(prom_getchild(this_node),
					       this_dev->child, this_dev, sbus);
		} else {
			this_dev->child = NULL;
		}
	}
}

/*
 * XXX This functions appears to be a distorted version of
 * prom_sbus_ranges_init(), with all sun4d stuff cut away.
 * Ask DaveM what is going on here, how is sun4d supposed to work... XXX
 */
/* added back sun4d patch from Thomas Bogendoerfer - should be OK (crn) */

static void __init sbus_bus_ranges_init(int parent_node, struct sbus_bus *sbus)
{
	int len;

	len = prom_getproperty(sbus->prom_node, "ranges",
			       (char *) sbus->sbus_ranges,
			       sizeof(sbus->sbus_ranges));
	if (len == -1 || len == 0) {
		sbus->num_sbus_ranges = 0;
		return;
	}
	sbus->num_sbus_ranges = len / sizeof(struct linux_prom_ranges);
#ifdef CONFIG_SPARC32
	if (sparc_cpu_model == sun4d) {
		struct linux_prom_ranges iounit_ranges[PROMREG_MAX];
		int num_iounit_ranges;

		len = prom_getproperty(parent_node, "ranges",
				       (char *) iounit_ranges,
				       sizeof (iounit_ranges));
		if (len != -1) {
			num_iounit_ranges = (len/sizeof(struct linux_prom_ranges));
			prom_adjust_ranges (sbus->sbus_ranges, sbus->num_sbus_ranges, iounit_ranges, num_iounit_ranges);
		}
	}
#endif
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

extern void register_proc_sparc_ioport(void);
extern void firetruck_init(void);

#ifdef CONFIG_SUN4
extern void sun4_dvma_init(void);
#endif

static int __init sbus_init(void)
{
	int nd, this_sbus, sbus_devs, topnd, iommund;
	unsigned int sbus_clock;
	struct sbus_bus *sbus;
	struct sbus_dev *this_dev;
	int num_sbus = 0;  /* How many did we find? */

#ifdef CONFIG_SPARC32
	register_proc_sparc_ioport();
#endif

#ifdef CONFIG_SUN4
	sun4_dvma_init();
	return 0;
#endif

	topnd = prom_getchild(prom_root_node);
	
	/* Finding the first sbus is a special case... */
	iommund = 0;
	if(sparc_cpu_model == sun4u) {
		nd = prom_searchsiblings(topnd, "sbus");
		if(nd == 0) {
#ifdef CONFIG_PCI
			if (!pcic_present()) {
				prom_printf("Neither SBUS nor PCI found.\n");
				prom_halt();
			} else {
#ifdef CONFIG_SPARC64
				firetruck_init();
#endif
			}
			return 0;
#else
			prom_printf("YEEE, UltraSparc sbus not found\n");
			prom_halt();
#endif
		}
	} else if(sparc_cpu_model == sun4d) {
		if((iommund = prom_searchsiblings(topnd, "io-unit")) == 0 ||
		   (nd = prom_getchild(iommund)) == 0 ||
		   (nd = prom_searchsiblings(nd, "sbi")) == 0) {
		   	panic("sbi not found");
		}
	} else if((nd = prom_searchsiblings(topnd, "sbus")) == 0) {
		if((iommund = prom_searchsiblings(topnd, "iommu")) == 0 ||
		   (nd = prom_getchild(iommund)) == 0 ||
		   (nd = prom_searchsiblings(nd, "sbus")) == 0) {
#ifdef CONFIG_PCI
                        if (!pcic_present()) {
                                prom_printf("Neither SBUS nor PCI found.\n");
                                prom_halt();
                        }
                        return 0;
#else
			/* No reason to run further - the data access trap will occur. */
			panic("sbus not found");
#endif
		}
	}

	/* Ok, we've found the first one, allocate first SBus struct
	 * and place in chain.
	 */
	sbus = sbus_root = kmalloc(sizeof(struct sbus_bus), GFP_ATOMIC);
	sbus->next = NULL;
	sbus->prom_node = nd;
	this_sbus = nd;

	if(iommund && sparc_cpu_model != sun4u && sparc_cpu_model != sun4d)
		iommu_init(iommund, sbus);

	/* Loop until we find no more SBUS's */
	while(this_sbus) {
#ifdef CONFIG_SPARC64
		/* IOMMU hides inside SBUS/SYSIO prom node on Ultra. */
		if(sparc_cpu_model == sun4u) {
			extern void sbus_iommu_init(int prom_node, struct sbus_bus *sbus);

			sbus_iommu_init(this_sbus, sbus);
		}
#endif /* CONFIG_SPARC64 */

#ifdef CONFIG_SPARC32
		if (sparc_cpu_model == sun4d)
			iounit_init(this_sbus, iommund, sbus);
#endif /* CONFIG_SPARC32 */
		printk("sbus%d: ", num_sbus);
		sbus_clock = prom_getint(this_sbus, "clock-frequency");
		if(sbus_clock == -1)
			sbus_clock = (25*1000*1000);
		printk("Clock %d.%d MHz\n", (int) ((sbus_clock/1000)/1000),
		       (int) (((sbus_clock/1000)%1000 != 0) ? 
			      (((sbus_clock/1000)%1000) + 1000) : 0));

		prom_getstring(this_sbus, "name",
			       sbus->prom_name, sizeof(sbus->prom_name));
		sbus->clock_freq = sbus_clock;
#ifdef CONFIG_SPARC32
		if (sparc_cpu_model == sun4d) {
			sbus->devid = prom_getint(iommund, "device-id");
			sbus->board = prom_getint(iommund, "board#");
		}
#endif
		
		sbus_bus_ranges_init(iommund, sbus);

		sbus_devs = prom_getchild(this_sbus);
		if (!sbus_devs) {
			sbus->devices = NULL;
			goto next_bus;
		}

		sbus->devices = kmalloc(sizeof(struct sbus_dev), GFP_ATOMIC);

		this_dev = sbus->devices;
		this_dev->next = NULL;

		this_dev->bus = sbus;
		this_dev->parent = NULL;
		fill_sbus_device(sbus_devs, this_dev);

		/* Should we traverse for children? */
		if(prom_getchild(sbus_devs)) {
			/* Allocate device node */
			this_dev->child = kmalloc(sizeof(struct sbus_dev),
						  GFP_ATOMIC);
			/* Fill it */
			this_dev->child->bus = sbus;
			this_dev->child->next = NULL;
			fill_sbus_device(prom_getchild(sbus_devs),
					 this_dev->child);
			sbus_do_child_siblings(prom_getchild(sbus_devs),
					       this_dev->child,
					       this_dev,
					       sbus);
		} else {
			this_dev->child = NULL;
		}

		while((sbus_devs = prom_getsibling(sbus_devs)) != 0) {
			/* Allocate device node */
			this_dev->next = kmalloc(sizeof(struct sbus_dev),
						 GFP_ATOMIC);
			this_dev = this_dev->next;
			this_dev->next = NULL;

			/* Fill it */
			this_dev->bus = sbus;
			this_dev->parent = NULL;
			fill_sbus_device(sbus_devs, this_dev);

			/* Is there a child node hanging off of us? */
			if(prom_getchild(sbus_devs)) {
				/* Get new device struct */
				this_dev->child = kmalloc(sizeof(struct sbus_dev),
							  GFP_ATOMIC);
				/* Fill it */
				this_dev->child->bus = sbus;
				this_dev->child->next = NULL;
				fill_sbus_device(prom_getchild(sbus_devs),
						 this_dev->child);
				sbus_do_child_siblings(prom_getchild(sbus_devs),
						       this_dev->child,
						       this_dev,
						       sbus);
			} else {
				this_dev->child = NULL;
			}
		}

		/* Walk all devices and apply parent ranges. */
		sbus_fixup_all_regs(sbus->devices);

		dvma_init(sbus);
	next_bus:
		num_sbus++;
		if(sparc_cpu_model == sun4u) {
			this_sbus = prom_getsibling(this_sbus);
			if(!this_sbus)
				break;
			this_sbus = prom_searchsiblings(this_sbus, "sbus");
		} else if(sparc_cpu_model == sun4d) {
			iommund = prom_getsibling(iommund);
			if(!iommund)
				break;
			iommund = prom_searchsiblings(iommund, "io-unit");
			if(!iommund)
				break;
			this_sbus = prom_searchsiblings(prom_getchild(iommund), "sbi");
		} else {
			this_sbus = prom_getsibling(this_sbus);
			if(!this_sbus)
				break;
			this_sbus = prom_searchsiblings(this_sbus, "sbus");
		}
		if(this_sbus) {
			sbus->next = kmalloc(sizeof(struct sbus_bus), GFP_ATOMIC);
			sbus = sbus->next;
			sbus->next = NULL;
			sbus->prom_node = this_sbus;
		} else {
			break;
		}
	} /* while(this_sbus) */

	if (sparc_cpu_model == sun4d) {
		extern void sun4d_init_sbi_irq(void);
		sun4d_init_sbi_irq();
	}
	
#ifdef CONFIG_SPARC64
	if (sparc_cpu_model == sun4u) {
		firetruck_init();
	}
#endif
#ifdef CONFIG_SUN_AUXIO
	if (sparc_cpu_model == sun4u)
		auxio_probe ();
#endif
#ifdef CONFIG_SPARC64
	if (sparc_cpu_model == sun4u) {
		extern void clock_probe(void);

		clock_probe();
	}
#endif

	return 0;
}

subsys_initcall(sbus_init);
