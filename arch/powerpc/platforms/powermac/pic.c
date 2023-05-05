// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Support for the interrupt controllers found on Power Macintosh,
 *  currently Apple's "Grand Central" interrupt controller in all
 *  it's incarnations. OpenPIC support used on newer machines is
 *  in a separate file
 *
 *  Copyright (C) 1997 Paul Mackerras (paulus@samba.org)
 *  Copyright (C) 2005 Benjamin Herrenschmidt (benh@kernel.crashing.org)
 *                     IBM, Corp.
 */

#include <linux/stddef.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/syscore_ops.h>
#include <linux/adb.h>
#include <linux/minmax.h>
#include <linux/pmu.h>
#include <linux/irqdomain.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#include <asm/sections.h>
#include <asm/io.h>
#include <asm/smp.h>
#include <asm/pci-bridge.h>
#include <asm/time.h>
#include <asm/pmac_feature.h>
#include <asm/mpic.h>
#include <asm/xmon.h>

#include "pmac.h"

#ifdef CONFIG_PPC32
struct pmac_irq_hw {
        unsigned int    event;
        unsigned int    enable;
        unsigned int    ack;
        unsigned int    level;
};

/* Workaround flags for 32bit powermac machines */
unsigned int of_irq_workarounds;
struct device_node *of_irq_dflt_pic;

/* Default addresses */
static volatile struct pmac_irq_hw __iomem *pmac_irq_hw[4];

static int max_irqs;
static int max_real_irqs;

static DEFINE_RAW_SPINLOCK(pmac_pic_lock);

/* The max irq number this driver deals with is 128; see max_irqs */
static DECLARE_BITMAP(ppc_lost_interrupts, 128);
static DECLARE_BITMAP(ppc_cached_irq_mask, 128);
static int pmac_irq_cascade = -1;
static struct irq_domain *pmac_pic_host;

static void __pmac_retrigger(unsigned int irq_nr)
{
	if (irq_nr >= max_real_irqs && pmac_irq_cascade > 0) {
		__set_bit(irq_nr, ppc_lost_interrupts);
		irq_nr = pmac_irq_cascade;
		mb();
	}
	if (!__test_and_set_bit(irq_nr, ppc_lost_interrupts)) {
		atomic_inc(&ppc_n_lost_interrupts);
		set_dec(1);
	}
}

static void pmac_mask_and_ack_irq(struct irq_data *d)
{
	unsigned int src = irqd_to_hwirq(d);
        unsigned long bit = 1UL << (src & 0x1f);
        int i = src >> 5;
        unsigned long flags;

	raw_spin_lock_irqsave(&pmac_pic_lock, flags);
        __clear_bit(src, ppc_cached_irq_mask);
        if (__test_and_clear_bit(src, ppc_lost_interrupts))
                atomic_dec(&ppc_n_lost_interrupts);
        out_le32(&pmac_irq_hw[i]->enable, ppc_cached_irq_mask[i]);
        out_le32(&pmac_irq_hw[i]->ack, bit);
        do {
                /* make sure ack gets to controller before we enable
                   interrupts */
                mb();
        } while((in_le32(&pmac_irq_hw[i]->enable) & bit)
                != (ppc_cached_irq_mask[i] & bit));
	raw_spin_unlock_irqrestore(&pmac_pic_lock, flags);
}

static void pmac_ack_irq(struct irq_data *d)
{
	unsigned int src = irqd_to_hwirq(d);
        unsigned long bit = 1UL << (src & 0x1f);
        int i = src >> 5;
        unsigned long flags;

	raw_spin_lock_irqsave(&pmac_pic_lock, flags);
	if (__test_and_clear_bit(src, ppc_lost_interrupts))
                atomic_dec(&ppc_n_lost_interrupts);
        out_le32(&pmac_irq_hw[i]->ack, bit);
        (void)in_le32(&pmac_irq_hw[i]->ack);
	raw_spin_unlock_irqrestore(&pmac_pic_lock, flags);
}

static void __pmac_set_irq_mask(unsigned int irq_nr, int nokicklost)
{
        unsigned long bit = 1UL << (irq_nr & 0x1f);
        int i = irq_nr >> 5;

        if ((unsigned)irq_nr >= max_irqs)
                return;

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
		__pmac_retrigger(irq_nr);
}

/* When an irq gets requested for the first client, if it's an
 * edge interrupt, we clear any previous one on the controller
 */
static unsigned int pmac_startup_irq(struct irq_data *d)
{
	unsigned long flags;
	unsigned int src = irqd_to_hwirq(d);
        unsigned long bit = 1UL << (src & 0x1f);
        int i = src >> 5;

	raw_spin_lock_irqsave(&pmac_pic_lock, flags);
	if (!irqd_is_level_type(d))
		out_le32(&pmac_irq_hw[i]->ack, bit);
        __set_bit(src, ppc_cached_irq_mask);
        __pmac_set_irq_mask(src, 0);
	raw_spin_unlock_irqrestore(&pmac_pic_lock, flags);

	return 0;
}

static void pmac_mask_irq(struct irq_data *d)
{
	unsigned long flags;
	unsigned int src = irqd_to_hwirq(d);

	raw_spin_lock_irqsave(&pmac_pic_lock, flags);
        __clear_bit(src, ppc_cached_irq_mask);
        __pmac_set_irq_mask(src, 1);
	raw_spin_unlock_irqrestore(&pmac_pic_lock, flags);
}

static void pmac_unmask_irq(struct irq_data *d)
{
	unsigned long flags;
	unsigned int src = irqd_to_hwirq(d);

	raw_spin_lock_irqsave(&pmac_pic_lock, flags);
	__set_bit(src, ppc_cached_irq_mask);
        __pmac_set_irq_mask(src, 0);
	raw_spin_unlock_irqrestore(&pmac_pic_lock, flags);
}

static int pmac_retrigger(struct irq_data *d)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&pmac_pic_lock, flags);
	__pmac_retrigger(irqd_to_hwirq(d));
	raw_spin_unlock_irqrestore(&pmac_pic_lock, flags);
	return 1;
}

static struct irq_chip pmac_pic = {
	.name		= "PMAC-PIC",
	.irq_startup	= pmac_startup_irq,
	.irq_mask	= pmac_mask_irq,
	.irq_ack	= pmac_ack_irq,
	.irq_mask_ack	= pmac_mask_and_ack_irq,
	.irq_unmask	= pmac_unmask_irq,
	.irq_retrigger	= pmac_retrigger,
};

static irqreturn_t gatwick_action(int cpl, void *dev_id)
{
	unsigned long flags;
	int irq, bits;
	int rc = IRQ_NONE;

	raw_spin_lock_irqsave(&pmac_pic_lock, flags);
	for (irq = max_irqs; (irq -= 32) >= max_real_irqs; ) {
		int i = irq >> 5;
		bits = in_le32(&pmac_irq_hw[i]->event) | ppc_lost_interrupts[i];
		bits |= in_le32(&pmac_irq_hw[i]->level);
		bits &= ppc_cached_irq_mask[i];
		if (bits == 0)
			continue;
		irq += __ilog2(bits);
		raw_spin_unlock_irqrestore(&pmac_pic_lock, flags);
		generic_handle_irq(irq);
		raw_spin_lock_irqsave(&pmac_pic_lock, flags);
		rc = IRQ_HANDLED;
	}
	raw_spin_unlock_irqrestore(&pmac_pic_lock, flags);
	return rc;
}

static unsigned int pmac_pic_get_irq(void)
{
	int irq;
	unsigned long bits = 0;
	unsigned long flags;

#ifdef CONFIG_PPC_PMAC32_PSURGE
	/* IPI's are a hack on the powersurge -- Cort */
	if (smp_processor_id() != 0) {
		return  psurge_secondary_virq;
        }
#endif /* CONFIG_PPC_PMAC32_PSURGE */
	raw_spin_lock_irqsave(&pmac_pic_lock, flags);
	for (irq = max_real_irqs; (irq -= 32) >= 0; ) {
		int i = irq >> 5;
		bits = in_le32(&pmac_irq_hw[i]->event) | ppc_lost_interrupts[i];
		bits |= in_le32(&pmac_irq_hw[i]->level);
		bits &= ppc_cached_irq_mask[i];
		if (bits == 0)
			continue;
		irq += __ilog2(bits);
		break;
	}
	raw_spin_unlock_irqrestore(&pmac_pic_lock, flags);
	if (unlikely(irq < 0))
		return 0;
	return irq_linear_revmap(pmac_pic_host, irq);
}

static int pmac_pic_host_match(struct irq_domain *h, struct device_node *node,
			       enum irq_domain_bus_token bus_token)
{
	/* We match all, we don't always have a node anyway */
	return 1;
}

static int pmac_pic_host_map(struct irq_domain *h, unsigned int virq,
			     irq_hw_number_t hw)
{
	if (hw >= max_irqs)
		return -EINVAL;

	/* Mark level interrupts, set delayed disable for edge ones and set
	 * handlers
	 */
	irq_set_status_flags(virq, IRQ_LEVEL);
	irq_set_chip_and_handler(virq, &pmac_pic, handle_level_irq);
	return 0;
}

static const struct irq_domain_ops pmac_pic_host_ops = {
	.match = pmac_pic_host_match,
	.map = pmac_pic_host_map,
	.xlate = irq_domain_xlate_onecell,
};

static void __init pmac_pic_probe_oldstyle(void)
{
        int i;
        struct device_node *master = NULL;
	struct device_node *slave = NULL;
	u8 __iomem *addr;
	struct resource r;

	/* Set our get_irq function */
	ppc_md.get_irq = pmac_pic_get_irq;

	/*
	 * Find the interrupt controller type & node
	 */

	if ((master = of_find_node_by_name(NULL, "gc")) != NULL) {
		max_irqs = max_real_irqs = 32;
	} else if ((master = of_find_node_by_name(NULL, "ohare")) != NULL) {
		max_irqs = max_real_irqs = 32;
		/* We might have a second cascaded ohare */
		slave = of_find_node_by_name(NULL, "pci106b,7");
		if (slave)
			max_irqs = 64;
	} else if ((master = of_find_node_by_name(NULL, "mac-io")) != NULL) {
		max_irqs = max_real_irqs = 64;

		/* We might have a second cascaded heathrow */

		/* Compensate for of_node_put() in of_find_node_by_name() */
		of_node_get(master);
		slave = of_find_node_by_name(master, "mac-io");

		/* Check ordering of master & slave */
		if (of_device_is_compatible(master, "gatwick")) {
			BUG_ON(slave == NULL);
			swap(master, slave);
		}

		/* We found a slave */
		if (slave)
			max_irqs = 128;
	}
	BUG_ON(master == NULL);

	/*
	 * Allocate an irq host
	 */
	pmac_pic_host = irq_domain_add_linear(master, max_irqs,
					      &pmac_pic_host_ops, NULL);
	BUG_ON(pmac_pic_host == NULL);
	irq_set_default_host(pmac_pic_host);

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

	printk(KERN_INFO "irq: Found primary Apple PIC %pOF for %d irqs\n",
	       master, max_real_irqs);

	/* Map interrupts of cascaded controller */
	if (slave && !of_address_to_resource(slave, 0, &r)) {
		addr = (u8 __iomem *)ioremap(r.start, 0x40);
		pmac_irq_hw[i++] = (volatile struct pmac_irq_hw __iomem *)
			(addr + 0x20);
		if (max_irqs > 64)
			pmac_irq_hw[i++] =
				(volatile struct pmac_irq_hw __iomem *)
				(addr + 0x10);
		pmac_irq_cascade = irq_of_parse_and_map(slave, 0);

		printk(KERN_INFO "irq: Found slave Apple PIC %pOF for %d irqs"
		       " cascade: %d\n", slave,
		       max_irqs - max_real_irqs, pmac_irq_cascade);
	}
	of_node_put(slave);

	/* Disable all interrupts in all controllers */
	for (i = 0; i * 32 < max_irqs; ++i)
		out_le32(&pmac_irq_hw[i]->enable, 0);

	/* Hookup cascade irq */
	if (slave && pmac_irq_cascade) {
		if (request_irq(pmac_irq_cascade, gatwick_action,
				IRQF_NO_THREAD, "cascade", NULL))
			pr_err("Failed to register cascade interrupt\n");
	}

	printk(KERN_INFO "irq: System has %d possible interrupts\n", max_irqs);
#ifdef CONFIG_XMON
	i = irq_create_mapping(NULL, 20);
	if (request_irq(i, xmon_irq, IRQF_NO_THREAD, "NMI - XMON", NULL))
		pr_err("Failed to register NMI-XMON interrupt\n");
#endif
}

int of_irq_parse_oldworld(const struct device_node *device, int index,
			struct of_phandle_args *out_irq)
{
	const u32 *ints = NULL;
	int intlen;

	/*
	 * Old machines just have a list of interrupt numbers
	 * and no interrupt-controller nodes. We also have dodgy
	 * cases where the APPL,interrupts property is completely
	 * missing behind pci-pci bridges and we have to get it
	 * from the parent (the bridge itself, as apple just wired
	 * everything together on these)
	 */
	while (device) {
		ints = of_get_property(device, "AAPL,interrupts", &intlen);
		if (ints != NULL)
			break;
		device = device->parent;
		if (!of_node_is_type(device, "pci"))
			break;
	}
	if (ints == NULL)
		return -EINVAL;
	intlen /= sizeof(u32);

	if (index >= intlen)
		return -EINVAL;

	out_irq->np = NULL;
	out_irq->args[0] = ints[index];
	out_irq->args_count = 1;

	return 0;
}
#endif /* CONFIG_PPC32 */

static void __init pmac_pic_setup_mpic_nmi(struct mpic *mpic)
{
#if defined(CONFIG_XMON) && defined(CONFIG_PPC32)
	struct device_node* pswitch;
	int nmi_irq;

	pswitch = of_find_node_by_name(NULL, "programmer-switch");
	if (pswitch) {
		nmi_irq = irq_of_parse_and_map(pswitch, 0);
		if (nmi_irq) {
			mpic_irq_set_priority(nmi_irq, 9);
			if (request_irq(nmi_irq, xmon_irq, IRQF_NO_THREAD,
					"NMI - XMON", NULL))
				pr_err("Failed to register NMI-XMON interrupt\n");
		}
		of_node_put(pswitch);
	}
#endif	/* defined(CONFIG_XMON) && defined(CONFIG_PPC32) */
}

static struct mpic * __init pmac_setup_one_mpic(struct device_node *np,
						int master)
{
	const char *name = master ? " MPIC 1   " : " MPIC 2   ";
	struct mpic *mpic;
	unsigned int flags = master ? 0 : MPIC_SECONDARY;

	pmac_call_feature(PMAC_FTR_ENABLE_MPIC, np, 0, 0);

	if (of_property_read_bool(np, "big-endian"))
		flags |= MPIC_BIG_ENDIAN;

	/* Primary Big Endian means HT interrupts. This is quite dodgy
	 * but works until I find a better way
	 */
	if (master && (flags & MPIC_BIG_ENDIAN))
		flags |= MPIC_U3_HT_IRQS;

	mpic = mpic_alloc(np, 0, flags, 0, 0, name);
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
	for_each_node_by_type(np, "open-pic") {
		if (master == NULL && !of_property_present(np, "interrupts"))
			master = of_node_get(np);
		else if (slave == NULL)
			slave = of_node_get(np);
		if (master && slave) {
			of_node_put(np);
			break;
		}
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

	/* Set up a cascaded controller, if present */
	if (slave) {
		mpic2 = pmac_setup_one_mpic(slave, 0);
		if (mpic2 == NULL)
			printk(KERN_ERR "Failed to setup slave MPIC\n");
		of_node_put(slave);
	}

	return 0;
}


void __init pmac_pic_init(void)
{
	/* We configure the OF parsing based on our oldworld vs. newworld
	 * platform type and whether we were booted by BootX.
	 */
#ifdef CONFIG_PPC32
	if (!pmac_newworld)
		of_irq_workarounds |= OF_IMAP_OLDWORLD_MAC;
	if (of_property_read_bool(of_chosen, "linux,bootx"))
		of_irq_workarounds |= OF_IMAP_NO_PHANDLE;

	/* If we don't have phandles on a newworld, then try to locate a
	 * default interrupt controller (happens when booting with BootX).
	 * We do a first match here, hopefully, that only ever happens on
	 * machines with one controller.
	 */
	if (pmac_newworld && (of_irq_workarounds & OF_IMAP_NO_PHANDLE)) {
		struct device_node *np;

		for_each_node_with_property(np, "interrupt-controller") {
			/* Skip /chosen/interrupt-controller */
			if (of_node_name_eq(np, "chosen"))
				continue;
			/* It seems like at least one person wants
			 * to use BootX on a machine with an AppleKiwi
			 * controller which happens to pretend to be an
			 * interrupt controller too. */
			if (of_node_name_eq(np, "AppleKiwi"))
				continue;
			/* I think we found one ! */
			of_irq_dflt_pic = np;
			break;
		}
	}
#endif /* CONFIG_PPC32 */

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
 * We really want to do things differently though...
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
	viaint = irq_of_parse_and_map(np, 0);
	of_node_put(np);

not_found:
#endif /* CONFIG_ADB_PMU */
	return viaint;
}

static int pmacpic_suspend(void)
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

static void pmacpic_resume(void)
{
	int i;

	out_le32(&pmac_irq_hw[0]->enable, 0);
	if (max_real_irqs > 32)
		out_le32(&pmac_irq_hw[1]->enable, 0);
	mb();
	for (i = 0; i < max_real_irqs; ++i)
		if (test_bit(i, sleep_save_mask))
			pmac_unmask_irq(irq_get_irq_data(i));
}

static struct syscore_ops pmacpic_syscore_ops = {
	.suspend	= pmacpic_suspend,
	.resume		= pmacpic_resume,
};

static int __init init_pmacpic_syscore(void)
{
	if (pmac_irq_hw[0])
		register_syscore_ops(&pmacpic_syscore_ops);
	return 0;
}

machine_subsys_initcall(powermac, init_pmacpic_syscore);

#endif /* CONFIG_PM && CONFIG_PPC32 */
