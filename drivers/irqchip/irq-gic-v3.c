/*
 * Copyright (C) 2013, 2014 ARM Limited, All Rights Reserved.
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/cpu.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/percpu.h>
#include <linux/slab.h>

#include <linux/irqchip/arm-gic-v3.h>

#include <asm/cputype.h>
#include <asm/exception.h>
#include <asm/smp_plat.h>

#include "irq-gic-common.h"
#include "irqchip.h"

struct gic_chip_data {
	void __iomem		*dist_base;
	void __iomem		**redist_base;
	void __iomem * __percpu	*rdist;
	struct irq_domain	*domain;
	u64			redist_stride;
	u32			redist_regions;
	unsigned int		irq_nr;
};

static struct gic_chip_data gic_data __read_mostly;

#define gic_data_rdist()		(this_cpu_ptr(gic_data.rdist))
#define gic_data_rdist_rd_base()	(*gic_data_rdist())
#define gic_data_rdist_sgi_base()	(gic_data_rdist_rd_base() + SZ_64K)

/* Our default, arbitrary priority value. Linux only uses one anyway. */
#define DEFAULT_PMR_VALUE	0xf0

static inline unsigned int gic_irq(struct irq_data *d)
{
	return d->hwirq;
}

static inline int gic_irq_in_rdist(struct irq_data *d)
{
	return gic_irq(d) < 32;
}

static inline void __iomem *gic_dist_base(struct irq_data *d)
{
	if (gic_irq_in_rdist(d))	/* SGI+PPI -> SGI_base for this CPU */
		return gic_data_rdist_sgi_base();

	if (d->hwirq <= 1023)		/* SPI -> dist_base */
		return gic_data.dist_base;

	if (d->hwirq >= 8192)
		BUG();		/* LPI Detected!!! */

	return NULL;
}

static void gic_do_wait_for_rwp(void __iomem *base)
{
	u32 count = 1000000;	/* 1s! */

	while (readl_relaxed(base + GICD_CTLR) & GICD_CTLR_RWP) {
		count--;
		if (!count) {
			pr_err_ratelimited("RWP timeout, gone fishing\n");
			return;
		}
		cpu_relax();
		udelay(1);
	};
}

/* Wait for completion of a distributor change */
static void gic_dist_wait_for_rwp(void)
{
	gic_do_wait_for_rwp(gic_data.dist_base);
}

/* Wait for completion of a redistributor change */
static void gic_redist_wait_for_rwp(void)
{
	gic_do_wait_for_rwp(gic_data_rdist_rd_base());
}

/* Low level accessors */
static u64 __maybe_unused gic_read_iar(void)
{
	u64 irqstat;

	asm volatile("mrs_s %0, " __stringify(ICC_IAR1_EL1) : "=r" (irqstat));
	return irqstat;
}

static void __maybe_unused gic_write_pmr(u64 val)
{
	asm volatile("msr_s " __stringify(ICC_PMR_EL1) ", %0" : : "r" (val));
}

static void __maybe_unused gic_write_ctlr(u64 val)
{
	asm volatile("msr_s " __stringify(ICC_CTLR_EL1) ", %0" : : "r" (val));
	isb();
}

static void __maybe_unused gic_write_grpen1(u64 val)
{
	asm volatile("msr_s " __stringify(ICC_GRPEN1_EL1) ", %0" : : "r" (val));
	isb();
}

static void __maybe_unused gic_write_sgi1r(u64 val)
{
	asm volatile("msr_s " __stringify(ICC_SGI1R_EL1) ", %0" : : "r" (val));
}

static void gic_enable_sre(void)
{
	u64 val;

	asm volatile("mrs_s %0, " __stringify(ICC_SRE_EL1) : "=r" (val));
	val |= ICC_SRE_EL1_SRE;
	asm volatile("msr_s " __stringify(ICC_SRE_EL1) ", %0" : : "r" (val));
	isb();

	/*
	 * Need to check that the SRE bit has actually been set. If
	 * not, it means that SRE is disabled at EL2. We're going to
	 * die painfully, and there is nothing we can do about it.
	 *
	 * Kindly inform the luser.
	 */
	asm volatile("mrs_s %0, " __stringify(ICC_SRE_EL1) : "=r" (val));
	if (!(val & ICC_SRE_EL1_SRE))
		pr_err("GIC: unable to set SRE (disabled at EL2), panic ahead\n");
}

static void gic_enable_redist(void)
{
	void __iomem *rbase;
	u32 count = 1000000;	/* 1s! */
	u32 val;

	rbase = gic_data_rdist_rd_base();

	/* Wake up this CPU redistributor */
	val = readl_relaxed(rbase + GICR_WAKER);
	val &= ~GICR_WAKER_ProcessorSleep;
	writel_relaxed(val, rbase + GICR_WAKER);

	while (readl_relaxed(rbase + GICR_WAKER) & GICR_WAKER_ChildrenAsleep) {
		count--;
		if (!count) {
			pr_err_ratelimited("redist didn't wake up...\n");
			return;
		}
		cpu_relax();
		udelay(1);
	};
}

/*
 * Routines to disable, enable, EOI and route interrupts
 */
static void gic_poke_irq(struct irq_data *d, u32 offset)
{
	u32 mask = 1 << (gic_irq(d) % 32);
	void (*rwp_wait)(void);
	void __iomem *base;

	if (gic_irq_in_rdist(d)) {
		base = gic_data_rdist_sgi_base();
		rwp_wait = gic_redist_wait_for_rwp;
	} else {
		base = gic_data.dist_base;
		rwp_wait = gic_dist_wait_for_rwp;
	}

	writel_relaxed(mask, base + offset + (gic_irq(d) / 32) * 4);
	rwp_wait();
}

static void gic_mask_irq(struct irq_data *d)
{
	gic_poke_irq(d, GICD_ICENABLER);
}

static void gic_unmask_irq(struct irq_data *d)
{
	gic_poke_irq(d, GICD_ISENABLER);
}

static void gic_eoi_irq(struct irq_data *d)
{
	gic_write_eoir(gic_irq(d));
}

static int gic_set_type(struct irq_data *d, unsigned int type)
{
	unsigned int irq = gic_irq(d);
	void (*rwp_wait)(void);
	void __iomem *base;

	/* Interrupt configuration for SGIs can't be changed */
	if (irq < 16)
		return -EINVAL;

	if (type != IRQ_TYPE_LEVEL_HIGH && type != IRQ_TYPE_EDGE_RISING)
		return -EINVAL;

	if (gic_irq_in_rdist(d)) {
		base = gic_data_rdist_sgi_base();
		rwp_wait = gic_redist_wait_for_rwp;
	} else {
		base = gic_data.dist_base;
		rwp_wait = gic_dist_wait_for_rwp;
	}

	gic_configure_irq(irq, type, base, rwp_wait);

	return 0;
}

static u64 gic_mpidr_to_affinity(u64 mpidr)
{
	u64 aff;

	aff = (MPIDR_AFFINITY_LEVEL(mpidr, 3) << 32 |
	       MPIDR_AFFINITY_LEVEL(mpidr, 2) << 16 |
	       MPIDR_AFFINITY_LEVEL(mpidr, 1) << 8  |
	       MPIDR_AFFINITY_LEVEL(mpidr, 0));

	return aff;
}

static asmlinkage void __exception_irq_entry gic_handle_irq(struct pt_regs *regs)
{
	u64 irqnr;

	do {
		irqnr = gic_read_iar();

		if (likely(irqnr > 15 && irqnr < 1020)) {
			u64 irq = irq_find_mapping(gic_data.domain, irqnr);
			if (likely(irq)) {
				handle_IRQ(irq, regs);
				continue;
			}

			WARN_ONCE(true, "Unexpected SPI received!\n");
			gic_write_eoir(irqnr);
		}
		if (irqnr < 16) {
			gic_write_eoir(irqnr);
#ifdef CONFIG_SMP
			handle_IPI(irqnr, regs);
#else
			WARN_ONCE(true, "Unexpected SGI received!\n");
#endif
			continue;
		}
	} while (irqnr != ICC_IAR1_EL1_SPURIOUS);
}

static void __init gic_dist_init(void)
{
	unsigned int i;
	u64 affinity;
	void __iomem *base = gic_data.dist_base;

	/* Disable the distributor */
	writel_relaxed(0, base + GICD_CTLR);
	gic_dist_wait_for_rwp();

	gic_dist_config(base, gic_data.irq_nr, gic_dist_wait_for_rwp);

	/* Enable distributor with ARE, Group1 */
	writel_relaxed(GICD_CTLR_ARE_NS | GICD_CTLR_ENABLE_G1A | GICD_CTLR_ENABLE_G1,
		       base + GICD_CTLR);

	/*
	 * Set all global interrupts to the boot CPU only. ARE must be
	 * enabled.
	 */
	affinity = gic_mpidr_to_affinity(cpu_logical_map(smp_processor_id()));
	for (i = 32; i < gic_data.irq_nr; i++)
		writeq_relaxed(affinity, base + GICD_IROUTER + i * 8);
}

static int gic_populate_rdist(void)
{
	u64 mpidr = cpu_logical_map(smp_processor_id());
	u64 typer;
	u32 aff;
	int i;

	/*
	 * Convert affinity to a 32bit value that can be matched to
	 * GICR_TYPER bits [63:32].
	 */
	aff = (MPIDR_AFFINITY_LEVEL(mpidr, 3) << 24 |
	       MPIDR_AFFINITY_LEVEL(mpidr, 2) << 16 |
	       MPIDR_AFFINITY_LEVEL(mpidr, 1) << 8 |
	       MPIDR_AFFINITY_LEVEL(mpidr, 0));

	for (i = 0; i < gic_data.redist_regions; i++) {
		void __iomem *ptr = gic_data.redist_base[i];
		u32 reg;

		reg = readl_relaxed(ptr + GICR_PIDR2) & GIC_PIDR2_ARCH_MASK;
		if (reg != GIC_PIDR2_ARCH_GICv3 &&
		    reg != GIC_PIDR2_ARCH_GICv4) { /* We're in trouble... */
			pr_warn("No redistributor present @%p\n", ptr);
			break;
		}

		do {
			typer = readq_relaxed(ptr + GICR_TYPER);
			if ((typer >> 32) == aff) {
				gic_data_rdist_rd_base() = ptr;
				pr_info("CPU%d: found redistributor %llx @%p\n",
					smp_processor_id(),
					(unsigned long long)mpidr, ptr);
				return 0;
			}

			if (gic_data.redist_stride) {
				ptr += gic_data.redist_stride;
			} else {
				ptr += SZ_64K * 2; /* Skip RD_base + SGI_base */
				if (typer & GICR_TYPER_VLPIS)
					ptr += SZ_64K * 2; /* Skip VLPI_base + reserved page */
			}
		} while (!(typer & GICR_TYPER_LAST));
	}

	/* We couldn't even deal with ourselves... */
	WARN(true, "CPU%d: mpidr %llx has no re-distributor!\n",
	     smp_processor_id(), (unsigned long long)mpidr);
	return -ENODEV;
}

static void gic_cpu_init(void)
{
	void __iomem *rbase;

	/* Register ourselves with the rest of the world */
	if (gic_populate_rdist())
		return;

	gic_enable_redist();

	rbase = gic_data_rdist_sgi_base();

	gic_cpu_config(rbase, gic_redist_wait_for_rwp);

	/* Enable system registers */
	gic_enable_sre();

	/* Set priority mask register */
	gic_write_pmr(DEFAULT_PMR_VALUE);

	/* EOI deactivates interrupt too (mode 0) */
	gic_write_ctlr(ICC_CTLR_EL1_EOImode_drop_dir);

	/* ... and let's hit the road... */
	gic_write_grpen1(1);
}

#ifdef CONFIG_SMP
static int gic_peek_irq(struct irq_data *d, u32 offset)
{
	u32 mask = 1 << (gic_irq(d) % 32);
	void __iomem *base;

	if (gic_irq_in_rdist(d))
		base = gic_data_rdist_sgi_base();
	else
		base = gic_data.dist_base;

	return !!(readl_relaxed(base + offset + (gic_irq(d) / 32) * 4) & mask);
}

static int gic_secondary_init(struct notifier_block *nfb,
			      unsigned long action, void *hcpu)
{
	if (action == CPU_STARTING || action == CPU_STARTING_FROZEN)
		gic_cpu_init();
	return NOTIFY_OK;
}

/*
 * Notifier for enabling the GIC CPU interface. Set an arbitrarily high
 * priority because the GIC needs to be up before the ARM generic timers.
 */
static struct notifier_block gic_cpu_notifier = {
	.notifier_call = gic_secondary_init,
	.priority = 100,
};

static u16 gic_compute_target_list(int *base_cpu, const struct cpumask *mask,
				   u64 cluster_id)
{
	int cpu = *base_cpu;
	u64 mpidr = cpu_logical_map(cpu);
	u16 tlist = 0;

	while (cpu < nr_cpu_ids) {
		/*
		 * If we ever get a cluster of more than 16 CPUs, just
		 * scream and skip that CPU.
		 */
		if (WARN_ON((mpidr & 0xff) >= 16))
			goto out;

		tlist |= 1 << (mpidr & 0xf);

		cpu = cpumask_next(cpu, mask);
		if (cpu == nr_cpu_ids)
			goto out;

		mpidr = cpu_logical_map(cpu);

		if (cluster_id != (mpidr & ~0xffUL)) {
			cpu--;
			goto out;
		}
	}
out:
	*base_cpu = cpu;
	return tlist;
}

static void gic_send_sgi(u64 cluster_id, u16 tlist, unsigned int irq)
{
	u64 val;

	val = (MPIDR_AFFINITY_LEVEL(cluster_id, 3) << 48	|
	       MPIDR_AFFINITY_LEVEL(cluster_id, 2) << 32	|
	       irq << 24			    		|
	       MPIDR_AFFINITY_LEVEL(cluster_id, 1) << 16	|
	       tlist);

	pr_debug("CPU%d: ICC_SGI1R_EL1 %llx\n", smp_processor_id(), val);
	gic_write_sgi1r(val);
}

static void gic_raise_softirq(const struct cpumask *mask, unsigned int irq)
{
	int cpu;

	if (WARN_ON(irq >= 16))
		return;

	/*
	 * Ensure that stores to Normal memory are visible to the
	 * other CPUs before issuing the IPI.
	 */
	smp_wmb();

	for_each_cpu_mask(cpu, *mask) {
		u64 cluster_id = cpu_logical_map(cpu) & ~0xffUL;
		u16 tlist;

		tlist = gic_compute_target_list(&cpu, mask, cluster_id);
		gic_send_sgi(cluster_id, tlist, irq);
	}

	/* Force the above writes to ICC_SGI1R_EL1 to be executed */
	isb();
}

static void gic_smp_init(void)
{
	set_smp_cross_call(gic_raise_softirq);
	register_cpu_notifier(&gic_cpu_notifier);
}

static int gic_set_affinity(struct irq_data *d, const struct cpumask *mask_val,
			    bool force)
{
	unsigned int cpu = cpumask_any_and(mask_val, cpu_online_mask);
	void __iomem *reg;
	int enabled;
	u64 val;

	if (gic_irq_in_rdist(d))
		return -EINVAL;

	/* If interrupt was enabled, disable it first */
	enabled = gic_peek_irq(d, GICD_ISENABLER);
	if (enabled)
		gic_mask_irq(d);

	reg = gic_dist_base(d) + GICD_IROUTER + (gic_irq(d) * 8);
	val = gic_mpidr_to_affinity(cpu_logical_map(cpu));

	writeq_relaxed(val, reg);

	/*
	 * If the interrupt was enabled, enabled it again. Otherwise,
	 * just wait for the distributor to have digested our changes.
	 */
	if (enabled)
		gic_unmask_irq(d);
	else
		gic_dist_wait_for_rwp();

	return IRQ_SET_MASK_OK;
}
#else
#define gic_set_affinity	NULL
#define gic_smp_init()		do { } while(0)
#endif

static struct irq_chip gic_chip = {
	.name			= "GICv3",
	.irq_mask		= gic_mask_irq,
	.irq_unmask		= gic_unmask_irq,
	.irq_eoi		= gic_eoi_irq,
	.irq_set_type		= gic_set_type,
	.irq_set_affinity	= gic_set_affinity,
};

static int gic_irq_domain_map(struct irq_domain *d, unsigned int irq,
			      irq_hw_number_t hw)
{
	/* SGIs are private to the core kernel */
	if (hw < 16)
		return -EPERM;
	/* PPIs */
	if (hw < 32) {
		irq_set_percpu_devid(irq);
		irq_set_chip_and_handler(irq, &gic_chip,
					 handle_percpu_devid_irq);
		set_irq_flags(irq, IRQF_VALID | IRQF_NOAUTOEN);
	}
	/* SPIs */
	if (hw >= 32 && hw < gic_data.irq_nr) {
		irq_set_chip_and_handler(irq, &gic_chip,
					 handle_fasteoi_irq);
		set_irq_flags(irq, IRQF_VALID | IRQF_PROBE);
	}
	irq_set_chip_data(irq, d->host_data);
	return 0;
}

static int gic_irq_domain_xlate(struct irq_domain *d,
				struct device_node *controller,
				const u32 *intspec, unsigned int intsize,
				unsigned long *out_hwirq, unsigned int *out_type)
{
	if (d->of_node != controller)
		return -EINVAL;
	if (intsize < 3)
		return -EINVAL;

	switch(intspec[0]) {
	case 0:			/* SPI */
		*out_hwirq = intspec[1] + 32;
		break;
	case 1:			/* PPI */
		*out_hwirq = intspec[1] + 16;
		break;
	default:
		return -EINVAL;
	}

	*out_type = intspec[2] & IRQ_TYPE_SENSE_MASK;
	return 0;
}

static const struct irq_domain_ops gic_irq_domain_ops = {
	.map = gic_irq_domain_map,
	.xlate = gic_irq_domain_xlate,
};

static int __init gic_of_init(struct device_node *node, struct device_node *parent)
{
	void __iomem *dist_base;
	void __iomem **redist_base;
	u64 redist_stride;
	u32 redist_regions;
	u32 reg;
	int gic_irqs;
	int err;
	int i;

	dist_base = of_iomap(node, 0);
	if (!dist_base) {
		pr_err("%s: unable to map gic dist registers\n",
			node->full_name);
		return -ENXIO;
	}

	reg = readl_relaxed(dist_base + GICD_PIDR2) & GIC_PIDR2_ARCH_MASK;
	if (reg != GIC_PIDR2_ARCH_GICv3 && reg != GIC_PIDR2_ARCH_GICv4) {
		pr_err("%s: no distributor detected, giving up\n",
			node->full_name);
		err = -ENODEV;
		goto out_unmap_dist;
	}

	if (of_property_read_u32(node, "#redistributor-regions", &redist_regions))
		redist_regions = 1;

	redist_base = kzalloc(sizeof(*redist_base) * redist_regions, GFP_KERNEL);
	if (!redist_base) {
		err = -ENOMEM;
		goto out_unmap_dist;
	}

	for (i = 0; i < redist_regions; i++) {
		redist_base[i] = of_iomap(node, 1 + i);
		if (!redist_base[i]) {
			pr_err("%s: couldn't map region %d\n",
			       node->full_name, i);
			err = -ENODEV;
			goto out_unmap_rdist;
		}
	}

	if (of_property_read_u64(node, "redistributor-stride", &redist_stride))
		redist_stride = 0;

	gic_data.dist_base = dist_base;
	gic_data.redist_base = redist_base;
	gic_data.redist_regions = redist_regions;
	gic_data.redist_stride = redist_stride;

	/*
	 * Find out how many interrupts are supported.
	 * The GIC only supports up to 1020 interrupt sources (SGI+PPI+SPI)
	 */
	gic_irqs = readl_relaxed(gic_data.dist_base + GICD_TYPER) & 0x1f;
	gic_irqs = (gic_irqs + 1) * 32;
	if (gic_irqs > 1020)
		gic_irqs = 1020;
	gic_data.irq_nr = gic_irqs;

	gic_data.domain = irq_domain_add_tree(node, &gic_irq_domain_ops,
					      &gic_data);
	gic_data.rdist = alloc_percpu(typeof(*gic_data.rdist));

	if (WARN_ON(!gic_data.domain) || WARN_ON(!gic_data.rdist)) {
		err = -ENOMEM;
		goto out_free;
	}

	set_handle_irq(gic_handle_irq);

	gic_smp_init();
	gic_dist_init();
	gic_cpu_init();

	return 0;

out_free:
	if (gic_data.domain)
		irq_domain_remove(gic_data.domain);
	free_percpu(gic_data.rdist);
out_unmap_rdist:
	for (i = 0; i < redist_regions; i++)
		if (redist_base[i])
			iounmap(redist_base[i]);
	kfree(redist_base);
out_unmap_dist:
	iounmap(dist_base);
	return err;
}

IRQCHIP_DECLARE(gic_v3, "arm,gic-v3", gic_of_init);
