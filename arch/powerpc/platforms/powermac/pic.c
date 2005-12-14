/*
 *  Support for the interrupt controllers found on Power Macintosh,
 *  currently Apple's "Grand Central" interrupt controller in all
 *  it's incarnations. OpenPIC support used on newer machines is
 *  in a separate file
 *
 *  Copyright (C) 1997 Paul Mackerras (paulus@samba.org)
 *  Copyright (C) 2005 Benjamin Herrenschmidt (benh@kernel.crashing.org)
 *                     IBM, Corp.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */

#include <linux/config.h>
#include <linux/stddef.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/sysdev.h>
#include <linux/adb.h>
#include <linux/pmu.h>
#include <linux/module.h>

#include <asm/sections.h>
#include <asm/io.h>
#include <asm/smp.h>
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#include <asm/time.h>
#include <asm/pmac_feature.h>
#include <asm/mpic.h>

#include "pmac.h"

/*
 * XXX this should be in xmon.h, but putting it there means xmon.h
 * has to include <linux/interrupt.h> (to get irqreturn_t), which
 * causes all sorts of problems.  -- paulus
 */
extern irqreturn_t xmon_irq(int, void *, struct pt_regs *);

#ifdef CONFIG_PPC32
struct pmac_irq_hw {
        unsigned int    event;
        unsigned int    enable;
        unsigned int    ack;
        unsigned int    level;
};

/* Default addresses */
static volatile struct pmac_irq_hw __iomem *pmac_irq_hw[4];

#define GC_LEVEL_MASK		0x3ff00000
#define OHARE_LEVEL_MASK	0x1ff00000
#define HEATHROW_LEVEL_MASK	0x1ff00000

static int max_irqs;
static int max_real_irqs;
static u32 level_mask[4];

static DEFINE_SPINLOCK(pmac_pic_lock);

#define GATWICK_IRQ_POOL_SIZE        10
static struct interrupt_info gatwick_int_pool[GATWICK_IRQ_POOL_SIZE];

#define NR_MASK_WORDS	((NR_IRQS + 31) / 32)
static unsigned long ppc_lost_interrupts[NR_MASK_WORDS];

/*
 * Mark an irq as "lost".  This is only used on the pmac
 * since it can lose interrupts (see pmac_set_irq_mask).
 * -- Cort
 */
void __set_lost(unsigned long irq_nr, int nokick)
{
	if (!test_and_set_bit(irq_nr, ppc_lost_interrupts)) {
		atomic_inc(&ppc_n_lost_interrupts);
		if (!nokick)
			set_dec(1);
	}
}

static void pmac_mask_and_ack_irq(unsigned int irq_nr)
{
        unsigned long bit = 1UL << (irq_nr & 0x1f);
        int i = irq_nr >> 5;
        unsigned long flags;

        if ((unsigned)irq_nr >= max_irqs)
                return;

        clear_bit(irq_nr, ppc_cached_irq_mask);
        if (test_and_clear_bit(irq_nr, ppc_lost_interrupts))
                atomic_dec(&ppc_n_lost_interrupts);
	spin_lock_irqsave(&pmac_pic_lock, flags);
        out_le32(&pmac_irq_hw[i]->enable, ppc_cached_irq_mask[i]);
        out_le32(&pmac_irq_hw[i]->ack, bit);
        do {
                /* make sure ack gets to controller before we enable
                   interrupts */
                mb();
        } while((in_le32(&pmac_irq_hw[i]->enable) & bit)
                != (ppc_cached_irq_mask[i] & bit));
	spin_unlock_irqrestore(&pmac_pic_lock, flags);
}

static void pmac_set_irq_mask(unsigned int irq_nr, int nokicklost)
{
        unsigned long bit = 1UL << (irq_nr & 0x1f);
        int i = irq_nr >> 5;
        unsigned long flags;

        if ((unsigned)irq_nr >= max_irqs)
                return;

	spin_lock_irqsave(&pmac_pic_lock, flags);
        /* enable unmasked interrupts */
        out_le32(&pmac_irq_hw[i]->enable, ppc_cached_irq_mask[i]);

        do {
                /* make sure mask gets to controller before we
                   return to user */
                mb();
        } while((in_le32(&pmac_irq_hw[i]->enable) & bit)
                != (ppc_cached_irq_mask[i] & bit));

        /*
         * Unfortunately, setting the bit in the enable register
         * when the device interrupt is already on *doesn't* set
         * the bit in the flag register or request another interrupt.
         */
        if (bit & ppc_cached_irq_mask[i] & in_le32(&pmac_irq_hw[i]->level))
		__set_lost((ulong)irq_nr, nokicklost);
	spin_unlock_irqrestore(&pmac_pic_lock, flags);
}

/* When an irq gets requested for the first client, if it's an
 * edge interrupt, we clear any previous one on the controller
 */
static unsigned int pmac_startup_irq(unsigned int irq_nr)
{
        unsigned long bit = 1UL << (irq_nr & 0x1f);
        int i = irq_nr >> 5;

	if ((irq_desc[irq_nr].status & IRQ_LEVEL) == 0)
		out_le32(&pmac_irq_hw[i]->ack, bit);
        set_bit(irq_nr, ppc_cached_irq_mask);
        pmac_set_irq_mask(irq_nr, 0);

	return 0;
}

static void pmac_mask_irq(unsigned int irq_nr)
{
        clear_bit(irq_nr, ppc_cached_irq_mask);
        pmac_set_irq_mask(irq_nr, 0);
        mb();
}

static void pmac_unmask_irq(unsigned int irq_nr)
{
        set_bit(irq_nr, ppc_cached_irq_mask);
        pmac_set_irq_mask(irq_nr, 0);
}

static void pmac_end_irq(unsigned int irq_nr)
{
	if (!(irq_desc[irq_nr].status & (IRQ_DISABLED|IRQ_INPROGRESS))
	    && irq_desc[irq_nr].action) {
        	set_bit(irq_nr, ppc_cached_irq_mask);
	        pmac_set_irq_mask(irq_nr, 1);
	}
}


struct hw_interrupt_type pmac_pic = {
	.typename	= " PMAC-PIC ",
	.startup	= pmac_startup_irq,
	.enable		= pmac_unmask_irq,
	.disable	= pmac_mask_irq,
	.ack		= pmac_mask_and_ack_irq,
	.end		= pmac_end_irq,
};

struct hw_interrupt_type gatwick_pic = {
	.typename	= " GATWICK  ",
	.startup	= pmac_startup_irq,
	.enable		= pmac_unmask_irq,
	.disable	= pmac_mask_irq,
	.ack		= pmac_mask_and_ack_irq,
	.end		= pmac_end_irq,
};

static irqreturn_t gatwick_action(int cpl, void *dev_id, struct pt_regs *regs)
{
	int irq, bits;

	for (irq = max_irqs; (irq -= 32) >= max_real_irqs; ) {
		int i = irq >> 5;
		bits = in_le32(&pmac_irq_hw[i]->event) | ppc_lost_interrupts[i];
		/* We must read level interrupts from the level register */
		bits |= (in_le32(&pmac_irq_hw[i]->level) & level_mask[i]);
		bits &= ppc_cached_irq_mask[i];
		if (bits == 0)
			continue;
		irq += __ilog2(bits);
		__do_IRQ(irq, regs);
		return IRQ_HANDLED;
	}
	printk("gatwick irq not from gatwick pic\n");
	return IRQ_NONE;
}

static int pmac_get_irq(struct pt_regs *regs)
{
	int irq;
	unsigned long bits = 0;

#ifdef CONFIG_SMP
	void psurge_smp_message_recv(struct pt_regs *);

       	/* IPI's are a hack on the powersurge -- Cort */
       	if ( smp_processor_id() != 0 ) {
		psurge_smp_message_recv(regs);
		return -2;	/* ignore, already handled */
        }
#endif /* CONFIG_SMP */
	for (irq = max_real_irqs; (irq -= 32) >= 0; ) {
		int i = irq >> 5;
		bits = in_le32(&pmac_irq_hw[i]->event) | ppc_lost_interrupts[i];
		/* We must read level interrupts from the level register */
		bits |= (in_le32(&pmac_irq_hw[i]->level) & level_mask[i]);
		bits &= ppc_cached_irq_mask[i];
		if (bits == 0)
			continue;
		irq += __ilog2(bits);
		break;
	}

	return irq;
}

/* This routine will fix some missing interrupt values in the device tree
 * on the gatwick mac-io controller used by some PowerBooks
 *
 * Walking of OF nodes could use a bit more fixing up here, but it's not
 * very important as this is all boot time code on static portions of the
 * device-tree.
 *
 * However, the modifications done to "intrs" will have to be removed and
 * replaced with proper updates of the "interrupts" properties or
 * AAPL,interrupts, yet to be decided, once the dynamic parsing is there.
 */
static void __init pmac_fix_gatwick_interrupts(struct device_node *gw,
					       int irq_base)
{
	struct device_node *node;
	int count;

	memset(gatwick_int_pool, 0, sizeof(gatwick_int_pool));
	count = 0;
	for (node = NULL; (node = of_get_next_child(gw, node)) != NULL;) {
		/* Fix SCC */
		if ((strcasecmp(node->name, "escc") == 0) && node->child) {
			if (node->child->n_intrs < 3) {
				node->child->intrs = &gatwick_int_pool[count];
				count += 3;
			}
			node->child->n_intrs = 3;
			node->child->intrs[0].line = 15+irq_base;
			node->child->intrs[1].line =  4+irq_base;
			node->child->intrs[2].line =  5+irq_base;
			printk(KERN_INFO "irq: fixed SCC on gatwick"
			       " (%d,%d,%d)\n",
			       node->child->intrs[0].line,
			       node->child->intrs[1].line,
			       node->child->intrs[2].line);
		}
		/* Fix media-bay & left SWIM */
		if (strcasecmp(node->name, "media-bay") == 0) {
			struct device_node* ya_node;

			if (node->n_intrs == 0)
				node->intrs = &gatwick_int_pool[count++];
			node->n_intrs = 1;
			node->intrs[0].line = 29+irq_base;
			printk(KERN_INFO "irq: fixed media-bay on gatwick"
			       " (%d)\n", node->intrs[0].line);

			ya_node = node->child;
			while(ya_node) {
				if (strcasecmp(ya_node->name, "floppy") == 0) {
					if (ya_node->n_intrs < 2) {
						ya_node->intrs = &gatwick_int_pool[count];
						count += 2;
					}
					ya_node->n_intrs = 2;
					ya_node->intrs[0].line = 19+irq_base;
					ya_node->intrs[1].line =  1+irq_base;
					printk(KERN_INFO "irq: fixed floppy on second controller (%d,%d)\n",
						ya_node->intrs[0].line, ya_node->intrs[1].line);
				}
				if (strcasecmp(ya_node->name, "ata4") == 0) {
					if (ya_node->n_intrs < 2) {
						ya_node->intrs = &gatwick_int_pool[count];
						count += 2;
					}
					ya_node->n_intrs = 2;
					ya_node->intrs[0].line = 14+irq_base;
					ya_node->intrs[1].line =  3+irq_base;
					printk(KERN_INFO "irq: fixed ide on second controller (%d,%d)\n",
						ya_node->intrs[0].line, ya_node->intrs[1].line);
				}
				ya_node = ya_node->sibling;
			}
		}
	}
	if (count > 10) {
		printk("WARNING !! Gatwick interrupt pool overflow\n");
		printk("  GATWICK_IRQ_POOL_SIZE = %d\n", GATWICK_IRQ_POOL_SIZE);
		printk("              requested = %d\n", count);
	}
}

/*
 * The PowerBook 3400/2400/3500 can have a combo ethernet/modem
 * card which includes an ohare chip that acts as a second interrupt
 * controller.  If we find this second ohare, set it up and fix the
 * interrupt value in the device tree for the ethernet chip.
 */
static void __init enable_second_ohare(struct device_node *np)
{
	unsigned char bus, devfn;
	unsigned short cmd;
	struct device_node *ether;

	/* This code doesn't strictly belong here, it could be part of
	 * either the PCI initialisation or the feature code. It's kept
	 * here for historical reasons.
	 */
	if (pci_device_from_OF_node(np, &bus, &devfn) == 0) {
		struct pci_controller* hose =
			pci_find_hose_for_OF_device(np);
		if (!hose) {
			printk(KERN_ERR "Can't find PCI hose for OHare2 !\n");
			return;
		}
		early_read_config_word(hose, bus, devfn, PCI_COMMAND, &cmd);
		cmd |= PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER;
		cmd &= ~PCI_COMMAND_IO;
		early_write_config_word(hose, bus, devfn, PCI_COMMAND, cmd);
	}

	/* Fix interrupt for the modem/ethernet combo controller. The number
	 * in the device tree (27) is bogus (correct for the ethernet-only
	 * board but not the combo ethernet/modem board).
	 * The real interrupt is 28 on the second controller -> 28+32 = 60.
	 */
	ether = of_find_node_by_name(NULL, "pci1011,14");
	if (ether && ether->n_intrs > 0) {
		ether->intrs[0].line = 60;
		printk(KERN_INFO "irq: Fixed ethernet IRQ to %d\n",
		       ether->intrs[0].line);
	}
	of_node_put(ether);
}

#ifdef CONFIG_XMON
static struct irqaction xmon_action = {
	.handler	= xmon_irq,
	.flags		= 0,
	.mask		= CPU_MASK_NONE,
	.name		= "NMI - XMON"
};
#endif

static struct irqaction gatwick_cascade_action = {
	.handler	= gatwick_action,
	.flags		= SA_INTERRUPT,
	.mask		= CPU_MASK_NONE,
	.name		= "cascade",
};

static void __init pmac_pic_probe_oldstyle(void)
{
        int i;
	int irq_cascade = -1;
        struct device_node *master = NULL;
	struct device_node *slave = NULL;
	u8 __iomem *addr;
	struct resource r;

	/* Set our get_irq function */
	ppc_md.get_irq = pmac_get_irq;

	/*
	 * Find the interrupt controller type & node
	 */

	if ((master = of_find_node_by_name(NULL, "gc")) != NULL) {
		max_irqs = max_real_irqs = 32;
		level_mask[0] = GC_LEVEL_MASK;
	} else if ((master = of_find_node_by_name(NULL, "ohare")) != NULL) {
		max_irqs = max_real_irqs = 32;
		level_mask[0] = OHARE_LEVEL_MASK;

		/* We might have a second cascaded ohare */
		slave = of_find_node_by_name(NULL, "pci106b,7");
		if (slave) {
			max_irqs = 64;
			level_mask[1] = OHARE_LEVEL_MASK;
			enable_second_ohare(slave);
		}
	} else if ((master = of_find_node_by_name(NULL, "mac-io")) != NULL) {
		max_irqs = max_real_irqs = 64;
		level_mask[0] = HEATHROW_LEVEL_MASK;
		level_mask[1] = 0;

		/* We might have a second cascaded heathrow */
		slave = of_find_node_by_name(master, "mac-io");

		/* Check ordering of master & slave */
		if (device_is_compatible(master, "gatwick")) {
			struct device_node *tmp;
			BUG_ON(slave == NULL);
			tmp = master;
			master = slave;
			slave = tmp;
		}

		/* We found a slave */
		if (slave) {
			max_irqs = 128;
			level_mask[2] = HEATHROW_LEVEL_MASK;
			level_mask[3] = 0;
			pmac_fix_gatwick_interrupts(slave, max_real_irqs);
		}
	}
	BUG_ON(master == NULL);

	/* Set the handler for the main PIC */
	for ( i = 0; i < max_real_irqs ; i++ )
		irq_desc[i].handler = &pmac_pic;

	/* Get addresses of first controller if we have a node for it */
	BUG_ON(of_address_to_resource(master, 0, &r));

	/* Map interrupts of primary controller */
	addr = (u8 __iomem *) ioremap(r.start, 0x40);
	i = 0;
	pmac_irq_hw[i++] = (volatile struct pmac_irq_hw __iomem *)
		(addr + 0x20);
	if (max_real_irqs > 32)
		pmac_irq_hw[i++] = (volatile struct pmac_irq_hw __iomem *)
			(addr + 0x10);
	of_node_put(master);

	printk(KERN_INFO "irq: Found primary Apple PIC %s for %d irqs\n",
	       master->full_name, max_real_irqs);

	/* Map interrupts of cascaded controller */
	if (slave && !of_address_to_resource(slave, 0, &r)) {
		addr = (u8 __iomem *)ioremap(r.start, 0x40);
		pmac_irq_hw[i++] = (volatile struct pmac_irq_hw __iomem *)
			(addr + 0x20);
		if (max_irqs > 64)
			pmac_irq_hw[i++] =
				(volatile struct pmac_irq_hw __iomem *)
				(addr + 0x10);
		irq_cascade = slave->intrs[0].line;

		printk(KERN_INFO "irq: Found slave Apple PIC %s for %d irqs"
		       " cascade: %d\n", slave->full_name,
		       max_irqs - max_real_irqs, irq_cascade);
	}
	of_node_put(slave);

	/* disable all interrupts in all controllers */
	for (i = 0; i * 32 < max_irqs; ++i)
		out_le32(&pmac_irq_hw[i]->enable, 0);

	/* mark level interrupts */
	for (i = 0; i < max_irqs; i++)
		if (level_mask[i >> 5] & (1UL << (i & 0x1f)))
			irq_desc[i].status = IRQ_LEVEL;

	/* Setup handlers for secondary controller and hook cascade irq*/
	if (slave) {
		for ( i = max_real_irqs ; i < max_irqs ; i++ )
			irq_desc[i].handler = &gatwick_pic;
		setup_irq(irq_cascade, &gatwick_cascade_action);
	}
	printk(KERN_INFO "irq: System has %d possible interrupts\n", max_irqs);
#ifdef CONFIG_XMON
	setup_irq(20, &xmon_action);
#endif
}
#endif /* CONFIG_PPC32 */

static int pmac_u3_cascade(struct pt_regs *regs, void *data)
{
	return mpic_get_one_irq((struct mpic *)data, regs);
}

static void __init pmac_pic_setup_mpic_nmi(struct mpic *mpic)
{
#if defined(CONFIG_XMON) && defined(CONFIG_PPC32)
	struct device_node* pswitch;
	int nmi_irq;

	pswitch = of_find_node_by_name(NULL, "programmer-switch");
	if (pswitch && pswitch->n_intrs) {
		nmi_irq = pswitch->intrs[0].line;
		mpic_irq_set_priority(nmi_irq, 9);
		setup_irq(nmi_irq, &xmon_action);
	}
	of_node_put(pswitch);
#endif	/* defined(CONFIG_XMON) && defined(CONFIG_PPC32) */
}

static struct mpic * __init pmac_setup_one_mpic(struct device_node *np,
						int master)
{
	unsigned char senses[128];
	int offset = master ? 0 : 128;
	int count = master ? 128 : 124;
	const char *name = master ? " MPIC 1   " : " MPIC 2   ";
	struct resource r;
	struct mpic *mpic;
	unsigned int flags = master ? MPIC_PRIMARY : 0;
	int rc;

	rc = of_address_to_resource(np, 0, &r);
	if (rc)
		return NULL;

	pmac_call_feature(PMAC_FTR_ENABLE_MPIC, np, 0, 0);

	prom_get_irq_senses(senses, offset, offset + count);

	flags |= MPIC_WANTS_RESET;
	if (get_property(np, "big-endian", NULL))
		flags |= MPIC_BIG_ENDIAN;

	/* Primary Big Endian means HT interrupts. This is quite dodgy
	 * but works until I find a better way
	 */
	if (master && (flags & MPIC_BIG_ENDIAN))
		flags |= MPIC_BROKEN_U3;

	mpic = mpic_alloc(r.start, flags, 0, offset, count, master ? 252 : 0,
			  senses, count, name);
	if (mpic == NULL)
		return NULL;

	mpic_init(mpic);

	return mpic;
 }

static int __init pmac_pic_probe_mpic(void)
{
	struct mpic *mpic1, *mpic2;
	struct device_node *np, *master = NULL, *slave = NULL;

	/* We can have up to 2 MPICs cascaded */
	for (np = NULL; (np = of_find_node_by_type(np, "open-pic"))
		     != NULL;) {
		if (master == NULL &&
		    get_property(np, "interrupts", NULL) == NULL)
			master = of_node_get(np);
		else if (slave == NULL)
			slave = of_node_get(np);
		if (master && slave)
			break;
	}

	/* Check for bogus setups */
	if (master == NULL && slave != NULL) {
		master = slave;
		slave = NULL;
	}

	/* Not found, default to good old pmac pic */
	if (master == NULL)
		return -ENODEV;

	/* Set master handler */
	ppc_md.get_irq = mpic_get_irq;

	/* Setup master */
	mpic1 = pmac_setup_one_mpic(master, 1);
	BUG_ON(mpic1 == NULL);

	/* Install NMI if any */
	pmac_pic_setup_mpic_nmi(mpic1);

	of_node_put(master);

	/* No slave, let's go out */
	if (slave == NULL || slave->n_intrs < 1)
		return 0;

	mpic2 = pmac_setup_one_mpic(slave, 0);
	if (mpic2 == NULL) {
		printk(KERN_ERR "Failed to setup slave MPIC\n");
		of_node_put(slave);
		return 0;
	}
	mpic_setup_cascade(slave->intrs[0].line, pmac_u3_cascade, mpic2);

	of_node_put(slave);
	return 0;
}


void __init pmac_pic_init(void)
{
	/* We first try to detect Apple's new Core99 chipset, since mac-io
	 * is quite different on those machines and contains an IBM MPIC2.
	 */
	if (pmac_pic_probe_mpic() == 0)
		return;

#ifdef CONFIG_PPC32
	pmac_pic_probe_oldstyle();
#endif
}

#if defined(CONFIG_PM) && defined(CONFIG_PPC32)
/*
 * These procedures are used in implementing sleep on the powerbooks.
 * sleep_save_intrs() saves the states of all interrupt enables
 * and disables all interrupts except for the nominated one.
 * sleep_restore_intrs() restores the states of all interrupt enables.
 */
unsigned long sleep_save_mask[2];

/* This used to be passed by the PMU driver but that link got
 * broken with the new driver model. We use this tweak for now...
 */
static int pmacpic_find_viaint(void)
{
	int viaint = -1;

#ifdef CONFIG_ADB_PMU
	struct device_node *np;

	if (pmu_get_model() != PMU_OHARE_BASED)
		goto not_found;
	np = of_find_node_by_name(NULL, "via-pmu");
	if (np == NULL)
		goto not_found;
	viaint = np->intrs[0].line;
#endif /* CONFIG_ADB_PMU */

not_found:
	return viaint;
}

static int pmacpic_suspend(struct sys_device *sysdev, pm_message_t state)
{
	int viaint = pmacpic_find_viaint();

	sleep_save_mask[0] = ppc_cached_irq_mask[0];
	sleep_save_mask[1] = ppc_cached_irq_mask[1];
	ppc_cached_irq_mask[0] = 0;
	ppc_cached_irq_mask[1] = 0;
	if (viaint > 0)
		set_bit(viaint, ppc_cached_irq_mask);
	out_le32(&pmac_irq_hw[0]->enable, ppc_cached_irq_mask[0]);
	if (max_real_irqs > 32)
		out_le32(&pmac_irq_hw[1]->enable, ppc_cached_irq_mask[1]);
	(void)in_le32(&pmac_irq_hw[0]->event);
	/* make sure mask gets to controller before we return to caller */
	mb();
        (void)in_le32(&pmac_irq_hw[0]->enable);

        return 0;
}

static int pmacpic_resume(struct sys_device *sysdev)
{
	int i;

	out_le32(&pmac_irq_hw[0]->enable, 0);
	if (max_real_irqs > 32)
		out_le32(&pmac_irq_hw[1]->enable, 0);
	mb();
	for (i = 0; i < max_real_irqs; ++i)
		if (test_bit(i, sleep_save_mask))
			pmac_unmask_irq(i);

	return 0;
}

#endif /* CONFIG_PM && CONFIG_PPC32 */

static struct sysdev_class pmacpic_sysclass = {
	set_kset_name("pmac_pic"),
};

static struct sys_device device_pmacpic = {
	.id		= 0,
	.cls		= &pmacpic_sysclass,
};

static struct sysdev_driver driver_pmacpic = {
#if defined(CONFIG_PM) && defined(CONFIG_PPC32)
	.suspend	= &pmacpic_suspend,
	.resume		= &pmacpic_resume,
#endif /* CONFIG_PM && CONFIG_PPC32 */
};

static int __init init_pmacpic_sysfs(void)
{
#ifdef CONFIG_PPC32
	if (max_irqs == 0)
		return -ENODEV;
#endif
	printk(KERN_DEBUG "Registering pmac pic with sysfs...\n");
	sysdev_class_register(&pmacpic_sysclass);
	sysdev_register(&device_pmacpic);
	sysdev_driver_register(&pmacpic_sysclass, &driver_pmacpic);
	return 0;
}

subsys_initcall(init_pmacpic_sysfs);

