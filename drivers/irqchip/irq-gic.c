// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Copyright (C) 2002 ARM Limited, All Rights Reserved.
 *
 * Interrupt architecture for the GIC:
 *
 * o There is one Interrupt Distributor, which receives interrupts
 *   from system devices and sends them to the Interrupt Controllers.
 *
 * o There is one CPU Interface per CPU, which sends interrupts sent
 *   by the Distributor, and interrupts generated locally, to the
 *   associated CPU. The base address of the CPU interface is usually
 *   aliased so that the same address points to different chips depending
 *   on the CPU it is accessed from.
 *
 * Note that IRQs 0-31 are special - they are local to each CPU.
 * As such, the enable set/clear, pending set/clear and active bit
 * registers are banked per-cpu for these sources.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kstrtox.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/smp.h>
#include <linux/cpu.h>
#include <linux/cpu_pm.h>
#include <linux/cpumask.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/acpi.h>
#include <linux/irqdomain.h>
#include <linux/interrupt.h>
#include <linux/percpu.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/irqchip.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqchip/arm-gic.h>

#include <asm/cputype.h>
#include <asm/irq.h>
#include <asm/exception.h>
#include <asm/smp_plat.h>
#include <asm/virt.h>

#include "irq-gic-common.h"

#ifdef CONFIG_ARM64
#include <asm/cpufeature.h>

static void gic_check_cpu_features(void)
{
	WARN_TAINT_ONCE(this_cpu_has_cap(ARM64_HAS_GIC_CPUIF_SYSREGS),
			TAINT_CPU_OUT_OF_SPEC,
			"GICv3 system registers enabled, broken firmware!\n");
}
#else
#define gic_check_cpu_features()	do { } while(0)
#endif

union gic_base {
	void __iomem *common_base;
	void __percpu * __iomem *percpu_base;
};

struct gic_chip_data {
	union gic_base dist_base;
	union gic_base cpu_base;
	void __iomem *raw_dist_base;
	void __iomem *raw_cpu_base;
	u32 percpu_offset;
#if defined(CONFIG_CPU_PM) || defined(CONFIG_ARM_GIC_PM)
	u32 saved_spi_enable[DIV_ROUND_UP(1020, 32)];
	u32 saved_spi_active[DIV_ROUND_UP(1020, 32)];
	u32 saved_spi_conf[DIV_ROUND_UP(1020, 16)];
	u32 saved_spi_target[DIV_ROUND_UP(1020, 4)];
	u32 __percpu *saved_ppi_enable;
	u32 __percpu *saved_ppi_active;
	u32 __percpu *saved_ppi_conf;
#endif
	struct irq_domain *domain;
	unsigned int gic_irqs;
};

#ifdef CONFIG_BL_SWITCHER

static DEFINE_RAW_SPINLOCK(cpu_map_lock);

#define gic_lock_irqsave(f)		\
	raw_spin_lock_irqsave(&cpu_map_lock, (f))
#define gic_unlock_irqrestore(f)	\
	raw_spin_unlock_irqrestore(&cpu_map_lock, (f))

#define gic_lock()			raw_spin_lock(&cpu_map_lock)
#define gic_unlock()			raw_spin_unlock(&cpu_map_lock)

#else

#define gic_lock_irqsave(f)		do { (void)(f); } while(0)
#define gic_unlock_irqrestore(f)	do { (void)(f); } while(0)

#define gic_lock()			do { } while(0)
#define gic_unlock()			do { } while(0)

#endif

static DEFINE_STATIC_KEY_FALSE(needs_rmw_access);

/*
 * The GIC mapping of CPU interfaces does not necessarily match
 * the logical CPU numbering.  Let's use a mapping as returned
 * by the GIC itself.
 */
#define NR_GIC_CPU_IF 8
static u8 gic_cpu_map[NR_GIC_CPU_IF] __read_mostly;

static DEFINE_STATIC_KEY_TRUE(supports_deactivate_key);

static struct gic_chip_data gic_data[CONFIG_ARM_GIC_MAX_NR] __read_mostly;

static struct gic_kvm_info gic_v2_kvm_info __initdata;

static DEFINE_PER_CPU(u32, sgi_intid);

#ifdef CONFIG_GIC_NON_BANKED
static DEFINE_STATIC_KEY_FALSE(frankengic_key);

static void enable_frankengic(void)
{
	static_branch_enable(&frankengic_key);
}

static inline void __iomem *__get_base(union gic_base *base)
{
	if (static_branch_unlikely(&frankengic_key))
		return raw_cpu_read(*base->percpu_base);

	return base->common_base;
}

#define gic_data_dist_base(d)	__get_base(&(d)->dist_base)
#define gic_data_cpu_base(d)	__get_base(&(d)->cpu_base)
#else
#define gic_data_dist_base(d)	((d)->dist_base.common_base)
#define gic_data_cpu_base(d)	((d)->cpu_base.common_base)
#define enable_frankengic()	do { } while(0)
#endif

static inline void __iomem *gic_dist_base(struct irq_data *d)
{
	struct gic_chip_data *gic_data = irq_data_get_irq_chip_data(d);
	return gic_data_dist_base(gic_data);
}

static inline void __iomem *gic_cpu_base(struct irq_data *d)
{
	struct gic_chip_data *gic_data = irq_data_get_irq_chip_data(d);
	return gic_data_cpu_base(gic_data);
}

static inline unsigned int gic_irq(struct irq_data *d)
{
	return d->hwirq;
}

static inline bool cascading_gic_irq(struct irq_data *d)
{
	void *data = irq_data_get_irq_handler_data(d);

	/*
	 * If handler_data is set, this is a cascading interrupt, and
	 * it cannot possibly be forwarded.
	 */
	return data != NULL;
}

/*
 * Routines to acknowledge, disable and enable interrupts
 */
static void gic_poke_irq(struct irq_data *d, u32 offset)
{
	u32 mask = 1 << (gic_irq(d) % 32);
	writel_relaxed(mask, gic_dist_base(d) + offset + (gic_irq(d) / 32) * 4);
}

static int gic_peek_irq(struct irq_data *d, u32 offset)
{
	u32 mask = 1 << (gic_irq(d) % 32);
	return !!(readl_relaxed(gic_dist_base(d) + offset + (gic_irq(d) / 32) * 4) & mask);
}

static void gic_mask_irq(struct irq_data *d)
{
	gic_poke_irq(d, GIC_DIST_ENABLE_CLEAR);
}

static void gic_eoimode1_mask_irq(struct irq_data *d)
{
	gic_mask_irq(d);
	/*
	 * When masking a forwarded interrupt, make sure it is
	 * deactivated as well.
	 *
	 * This ensures that an interrupt that is getting
	 * disabled/masked will not get "stuck", because there is
	 * noone to deactivate it (guest is being terminated).
	 */
	if (irqd_is_forwarded_to_vcpu(d))
		gic_poke_irq(d, GIC_DIST_ACTIVE_CLEAR);
}

static void gic_unmask_irq(struct irq_data *d)
{
	gic_poke_irq(d, GIC_DIST_ENABLE_SET);
}

static void gic_eoi_irq(struct irq_data *d)
{
	u32 hwirq = gic_irq(d);

	if (hwirq < 16)
		hwirq = this_cpu_read(sgi_intid);

	writel_relaxed(hwirq, gic_cpu_base(d) + GIC_CPU_EOI);
}

static void gic_eoimode1_eoi_irq(struct irq_data *d)
{
	u32 hwirq = gic_irq(d);

	/* Do not deactivate an IRQ forwarded to a vcpu. */
	if (irqd_is_forwarded_to_vcpu(d))
		return;

	if (hwirq < 16)
		hwirq = this_cpu_read(sgi_intid);

	writel_relaxed(hwirq, gic_cpu_base(d) + GIC_CPU_DEACTIVATE);
}

static int gic_irq_set_irqchip_state(struct irq_data *d,
				     enum irqchip_irq_state which, bool val)
{
	u32 reg;

	switch (which) {
	case IRQCHIP_STATE_PENDING:
		reg = val ? GIC_DIST_PENDING_SET : GIC_DIST_PENDING_CLEAR;
		break;

	case IRQCHIP_STATE_ACTIVE:
		reg = val ? GIC_DIST_ACTIVE_SET : GIC_DIST_ACTIVE_CLEAR;
		break;

	case IRQCHIP_STATE_MASKED:
		reg = val ? GIC_DIST_ENABLE_CLEAR : GIC_DIST_ENABLE_SET;
		break;

	default:
		return -EINVAL;
	}

	gic_poke_irq(d, reg);
	return 0;
}

static int gic_irq_get_irqchip_state(struct irq_data *d,
				      enum irqchip_irq_state which, bool *val)
{
	switch (which) {
	case IRQCHIP_STATE_PENDING:
		*val = gic_peek_irq(d, GIC_DIST_PENDING_SET);
		break;

	case IRQCHIP_STATE_ACTIVE:
		*val = gic_peek_irq(d, GIC_DIST_ACTIVE_SET);
		break;

	case IRQCHIP_STATE_MASKED:
		*val = !gic_peek_irq(d, GIC_DIST_ENABLE_SET);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int gic_set_type(struct irq_data *d, unsigned int type)
{
	void __iomem *base = gic_dist_base(d);
	unsigned int gicirq = gic_irq(d);
	int ret;

	/* Interrupt configuration for SGIs can't be changed */
	if (gicirq < 16)
		return type != IRQ_TYPE_EDGE_RISING ? -EINVAL : 0;

	/* SPIs have restrictions on the supported types */
	if (gicirq >= 32 && type != IRQ_TYPE_LEVEL_HIGH &&
			    type != IRQ_TYPE_EDGE_RISING)
		return -EINVAL;

	ret = gic_configure_irq(gicirq, type, base + GIC_DIST_CONFIG, NULL);
	if (ret && gicirq < 32) {
		/* Misconfigured PPIs are usually not fatal */
		pr_warn("GIC: PPI%d is secure or misconfigured\n", gicirq - 16);
		ret = 0;
	}

	return ret;
}

static int gic_irq_set_vcpu_affinity(struct irq_data *d, void *vcpu)
{
	/* Only interrupts on the primary GIC can be forwarded to a vcpu. */
	if (cascading_gic_irq(d) || gic_irq(d) < 16)
		return -EINVAL;

	if (vcpu)
		irqd_set_forwarded_to_vcpu(d);
	else
		irqd_clr_forwarded_to_vcpu(d);
	return 0;
}

static int gic_retrigger(struct irq_data *data)
{
	return !gic_irq_set_irqchip_state(data, IRQCHIP_STATE_PENDING, true);
}

static void __exception_irq_entry gic_handle_irq(struct pt_regs *regs)
{
	u32 irqstat, irqnr;
	struct gic_chip_data *gic = &gic_data[0];
	void __iomem *cpu_base = gic_data_cpu_base(gic);

	do {
		irqstat = readl_relaxed(cpu_base + GIC_CPU_INTACK);
		irqnr = irqstat & GICC_IAR_INT_ID_MASK;

		if (unlikely(irqnr >= 1020))
			break;

		if (static_branch_likely(&supports_deactivate_key))
			writel_relaxed(irqstat, cpu_base + GIC_CPU_EOI);
		isb();

		/*
		 * Ensure any shared data written by the CPU sending the IPI
		 * is read after we've read the ACK register on the GIC.
		 *
		 * Pairs with the write barrier in gic_ipi_send_mask
		 */
		if (irqnr <= 15) {
			smp_rmb();

			/*
			 * The GIC encodes the source CPU in GICC_IAR,
			 * leading to the deactivation to fail if not
			 * written back as is to GICC_EOI.  Stash the INTID
			 * away for gic_eoi_irq() to write back.  This only
			 * works because we don't nest SGIs...
			 */
			this_cpu_write(sgi_intid, irqstat);
		}

		generic_handle_domain_irq(gic->domain, irqnr);
	} while (1);
}

static void gic_handle_cascade_irq(struct irq_desc *desc)
{
	struct gic_chip_data *chip_data = irq_desc_get_handler_data(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	unsigned int gic_irq;
	unsigned long status;
	int ret;

	chained_irq_enter(chip, desc);

	status = readl_relaxed(gic_data_cpu_base(chip_data) + GIC_CPU_INTACK);

	gic_irq = (status & GICC_IAR_INT_ID_MASK);
	if (gic_irq == GICC_INT_SPURIOUS)
		goto out;

	isb();
	ret = generic_handle_domain_irq(chip_data->domain, gic_irq);
	if (unlikely(ret))
		handle_bad_irq(desc);
 out:
	chained_irq_exit(chip, desc);
}

static void gic_irq_print_chip(struct irq_data *d, struct seq_file *p)
{
	struct gic_chip_data *gic = irq_data_get_irq_chip_data(d);

	if (gic->domain->pm_dev)
		seq_printf(p, gic->domain->pm_dev->of_node->name);
	else
		seq_printf(p, "GIC-%d", (int)(gic - &gic_data[0]));
}

void __init gic_cascade_irq(unsigned int gic_nr, unsigned int irq)
{
	BUG_ON(gic_nr >= CONFIG_ARM_GIC_MAX_NR);
	irq_set_chained_handler_and_data(irq, gic_handle_cascade_irq,
					 &gic_data[gic_nr]);
}

static u8 gic_get_cpumask(struct gic_chip_data *gic)
{
	void __iomem *base = gic_data_dist_base(gic);
	u32 mask, i;

	for (i = mask = 0; i < 32; i += 4) {
		mask = readl_relaxed(base + GIC_DIST_TARGET + i);
		mask |= mask >> 16;
		mask |= mask >> 8;
		if (mask)
			break;
	}

	if (!mask && num_possible_cpus() > 1)
		pr_crit("GIC CPU mask not found - kernel will fail to boot.\n");

	return mask;
}

static bool gic_check_gicv2(void __iomem *base)
{
	u32 val = readl_relaxed(base + GIC_CPU_IDENT);
	return (val & 0xff0fff) == 0x02043B;
}

static void gic_cpu_if_up(struct gic_chip_data *gic)
{
	void __iomem *cpu_base = gic_data_cpu_base(gic);
	u32 bypass = 0;
	u32 mode = 0;
	int i;

	if (gic == &gic_data[0] && static_branch_likely(&supports_deactivate_key))
		mode = GIC_CPU_CTRL_EOImodeNS;

	if (gic_check_gicv2(cpu_base))
		for (i = 0; i < 4; i++)
			writel_relaxed(0, cpu_base + GIC_CPU_ACTIVEPRIO + i * 4);

	/*
	* Preserve bypass disable bits to be written back later
	*/
	bypass = readl(cpu_base + GIC_CPU_CTRL);
	bypass &= GICC_DIS_BYPASS_MASK;

	writel_relaxed(bypass | mode | GICC_ENABLE, cpu_base + GIC_CPU_CTRL);
}


static void gic_dist_init(struct gic_chip_data *gic)
{
	unsigned int i;
	u32 cpumask;
	unsigned int gic_irqs = gic->gic_irqs;
	void __iomem *base = gic_data_dist_base(gic);

	writel_relaxed(GICD_DISABLE, base + GIC_DIST_CTRL);

	/*
	 * Set all global interrupts to this CPU only.
	 */
	cpumask = gic_get_cpumask(gic);
	cpumask |= cpumask << 8;
	cpumask |= cpumask << 16;
	for (i = 32; i < gic_irqs; i += 4)
		writel_relaxed(cpumask, base + GIC_DIST_TARGET + i * 4 / 4);

	gic_dist_config(base, gic_irqs, NULL);

	writel_relaxed(GICD_ENABLE, base + GIC_DIST_CTRL);
}

static int gic_cpu_init(struct gic_chip_data *gic)
{
	void __iomem *dist_base = gic_data_dist_base(gic);
	void __iomem *base = gic_data_cpu_base(gic);
	unsigned int cpu_mask, cpu = smp_processor_id();
	int i;

	/*
	 * Setting up the CPU map is only relevant for the primary GIC
	 * because any nested/secondary GICs do not directly interface
	 * with the CPU(s).
	 */
	if (gic == &gic_data[0]) {
		/*
		 * Get what the GIC says our CPU mask is.
		 */
		if (WARN_ON(cpu >= NR_GIC_CPU_IF))
			return -EINVAL;

		gic_check_cpu_features();
		cpu_mask = gic_get_cpumask(gic);
		gic_cpu_map[cpu] = cpu_mask;

		/*
		 * Clear our mask from the other map entries in case they're
		 * still undefined.
		 */
		for (i = 0; i < NR_GIC_CPU_IF; i++)
			if (i != cpu)
				gic_cpu_map[i] &= ~cpu_mask;
	}

	gic_cpu_config(dist_base, 32, NULL);

	writel_relaxed(GICC_INT_PRI_THRESHOLD, base + GIC_CPU_PRIMASK);
	gic_cpu_if_up(gic);

	return 0;
}

int gic_cpu_if_down(unsigned int gic_nr)
{
	void __iomem *cpu_base;
	u32 val = 0;

	if (gic_nr >= CONFIG_ARM_GIC_MAX_NR)
		return -EINVAL;

	cpu_base = gic_data_cpu_base(&gic_data[gic_nr]);
	val = readl(cpu_base + GIC_CPU_CTRL);
	val &= ~GICC_ENABLE;
	writel_relaxed(val, cpu_base + GIC_CPU_CTRL);

	return 0;
}

#if defined(CONFIG_CPU_PM) || defined(CONFIG_ARM_GIC_PM)
/*
 * Saves the GIC distributor registers during suspend or idle.  Must be called
 * with interrupts disabled but before powering down the GIC.  After calling
 * this function, no interrupts will be delivered by the GIC, and another
 * platform-specific wakeup source must be enabled.
 */
void gic_dist_save(struct gic_chip_data *gic)
{
	unsigned int gic_irqs;
	void __iomem *dist_base;
	int i;

	if (WARN_ON(!gic))
		return;

	gic_irqs = gic->gic_irqs;
	dist_base = gic_data_dist_base(gic);

	if (!dist_base)
		return;

	for (i = 0; i < DIV_ROUND_UP(gic_irqs, 16); i++)
		gic->saved_spi_conf[i] =
			readl_relaxed(dist_base + GIC_DIST_CONFIG + i * 4);

	for (i = 0; i < DIV_ROUND_UP(gic_irqs, 4); i++)
		gic->saved_spi_target[i] =
			readl_relaxed(dist_base + GIC_DIST_TARGET + i * 4);

	for (i = 0; i < DIV_ROUND_UP(gic_irqs, 32); i++)
		gic->saved_spi_enable[i] =
			readl_relaxed(dist_base + GIC_DIST_ENABLE_SET + i * 4);

	for (i = 0; i < DIV_ROUND_UP(gic_irqs, 32); i++)
		gic->saved_spi_active[i] =
			readl_relaxed(dist_base + GIC_DIST_ACTIVE_SET + i * 4);
}

/*
 * Restores the GIC distributor registers during resume or when coming out of
 * idle.  Must be called before enabling interrupts.  If a level interrupt
 * that occurred while the GIC was suspended is still present, it will be
 * handled normally, but any edge interrupts that occurred will not be seen by
 * the GIC and need to be handled by the platform-specific wakeup source.
 */
void gic_dist_restore(struct gic_chip_data *gic)
{
	unsigned int gic_irqs;
	unsigned int i;
	void __iomem *dist_base;

	if (WARN_ON(!gic))
		return;

	gic_irqs = gic->gic_irqs;
	dist_base = gic_data_dist_base(gic);

	if (!dist_base)
		return;

	writel_relaxed(GICD_DISABLE, dist_base + GIC_DIST_CTRL);

	for (i = 0; i < DIV_ROUND_UP(gic_irqs, 16); i++)
		writel_relaxed(gic->saved_spi_conf[i],
			dist_base + GIC_DIST_CONFIG + i * 4);

	for (i = 0; i < DIV_ROUND_UP(gic_irqs, 4); i++)
		writel_relaxed(GICD_INT_DEF_PRI_X4,
			dist_base + GIC_DIST_PRI + i * 4);

	for (i = 0; i < DIV_ROUND_UP(gic_irqs, 4); i++)
		writel_relaxed(gic->saved_spi_target[i],
			dist_base + GIC_DIST_TARGET + i * 4);

	for (i = 0; i < DIV_ROUND_UP(gic_irqs, 32); i++) {
		writel_relaxed(GICD_INT_EN_CLR_X32,
			dist_base + GIC_DIST_ENABLE_CLEAR + i * 4);
		writel_relaxed(gic->saved_spi_enable[i],
			dist_base + GIC_DIST_ENABLE_SET + i * 4);
	}

	for (i = 0; i < DIV_ROUND_UP(gic_irqs, 32); i++) {
		writel_relaxed(GICD_INT_EN_CLR_X32,
			dist_base + GIC_DIST_ACTIVE_CLEAR + i * 4);
		writel_relaxed(gic->saved_spi_active[i],
			dist_base + GIC_DIST_ACTIVE_SET + i * 4);
	}

	writel_relaxed(GICD_ENABLE, dist_base + GIC_DIST_CTRL);
}

void gic_cpu_save(struct gic_chip_data *gic)
{
	int i;
	u32 *ptr;
	void __iomem *dist_base;
	void __iomem *cpu_base;

	if (WARN_ON(!gic))
		return;

	dist_base = gic_data_dist_base(gic);
	cpu_base = gic_data_cpu_base(gic);

	if (!dist_base || !cpu_base)
		return;

	ptr = raw_cpu_ptr(gic->saved_ppi_enable);
	for (i = 0; i < DIV_ROUND_UP(32, 32); i++)
		ptr[i] = readl_relaxed(dist_base + GIC_DIST_ENABLE_SET + i * 4);

	ptr = raw_cpu_ptr(gic->saved_ppi_active);
	for (i = 0; i < DIV_ROUND_UP(32, 32); i++)
		ptr[i] = readl_relaxed(dist_base + GIC_DIST_ACTIVE_SET + i * 4);

	ptr = raw_cpu_ptr(gic->saved_ppi_conf);
	for (i = 0; i < DIV_ROUND_UP(32, 16); i++)
		ptr[i] = readl_relaxed(dist_base + GIC_DIST_CONFIG + i * 4);

}

void gic_cpu_restore(struct gic_chip_data *gic)
{
	int i;
	u32 *ptr;
	void __iomem *dist_base;
	void __iomem *cpu_base;

	if (WARN_ON(!gic))
		return;

	dist_base = gic_data_dist_base(gic);
	cpu_base = gic_data_cpu_base(gic);

	if (!dist_base || !cpu_base)
		return;

	ptr = raw_cpu_ptr(gic->saved_ppi_enable);
	for (i = 0; i < DIV_ROUND_UP(32, 32); i++) {
		writel_relaxed(GICD_INT_EN_CLR_X32,
			       dist_base + GIC_DIST_ENABLE_CLEAR + i * 4);
		writel_relaxed(ptr[i], dist_base + GIC_DIST_ENABLE_SET + i * 4);
	}

	ptr = raw_cpu_ptr(gic->saved_ppi_active);
	for (i = 0; i < DIV_ROUND_UP(32, 32); i++) {
		writel_relaxed(GICD_INT_EN_CLR_X32,
			       dist_base + GIC_DIST_ACTIVE_CLEAR + i * 4);
		writel_relaxed(ptr[i], dist_base + GIC_DIST_ACTIVE_SET + i * 4);
	}

	ptr = raw_cpu_ptr(gic->saved_ppi_conf);
	for (i = 0; i < DIV_ROUND_UP(32, 16); i++)
		writel_relaxed(ptr[i], dist_base + GIC_DIST_CONFIG + i * 4);

	for (i = 0; i < DIV_ROUND_UP(32, 4); i++)
		writel_relaxed(GICD_INT_DEF_PRI_X4,
					dist_base + GIC_DIST_PRI + i * 4);

	writel_relaxed(GICC_INT_PRI_THRESHOLD, cpu_base + GIC_CPU_PRIMASK);
	gic_cpu_if_up(gic);
}

static int gic_notifier(struct notifier_block *self, unsigned long cmd,	void *v)
{
	int i;

	for (i = 0; i < CONFIG_ARM_GIC_MAX_NR; i++) {
		switch (cmd) {
		case CPU_PM_ENTER:
			gic_cpu_save(&gic_data[i]);
			break;
		case CPU_PM_ENTER_FAILED:
		case CPU_PM_EXIT:
			gic_cpu_restore(&gic_data[i]);
			break;
		case CPU_CLUSTER_PM_ENTER:
			gic_dist_save(&gic_data[i]);
			break;
		case CPU_CLUSTER_PM_ENTER_FAILED:
		case CPU_CLUSTER_PM_EXIT:
			gic_dist_restore(&gic_data[i]);
			break;
		}
	}

	return NOTIFY_OK;
}

static struct notifier_block gic_notifier_block = {
	.notifier_call = gic_notifier,
};

static int gic_pm_init(struct gic_chip_data *gic)
{
	gic->saved_ppi_enable = __alloc_percpu(DIV_ROUND_UP(32, 32) * 4,
		sizeof(u32));
	if (WARN_ON(!gic->saved_ppi_enable))
		return -ENOMEM;

	gic->saved_ppi_active = __alloc_percpu(DIV_ROUND_UP(32, 32) * 4,
		sizeof(u32));
	if (WARN_ON(!gic->saved_ppi_active))
		goto free_ppi_enable;

	gic->saved_ppi_conf = __alloc_percpu(DIV_ROUND_UP(32, 16) * 4,
		sizeof(u32));
	if (WARN_ON(!gic->saved_ppi_conf))
		goto free_ppi_active;

	if (gic == &gic_data[0])
		cpu_pm_register_notifier(&gic_notifier_block);

	return 0;

free_ppi_active:
	free_percpu(gic->saved_ppi_active);
free_ppi_enable:
	free_percpu(gic->saved_ppi_enable);

	return -ENOMEM;
}
#else
static int gic_pm_init(struct gic_chip_data *gic)
{
	return 0;
}
#endif

#ifdef CONFIG_SMP
static void rmw_writeb(u8 bval, void __iomem *addr)
{
	static DEFINE_RAW_SPINLOCK(rmw_lock);
	unsigned long offset = (unsigned long)addr & 3UL;
	unsigned long shift = offset * 8;
	unsigned long flags;
	u32 val;

	raw_spin_lock_irqsave(&rmw_lock, flags);

	addr -= offset;
	val = readl_relaxed(addr);
	val &= ~GENMASK(shift + 7, shift);
	val |= bval << shift;
	writel_relaxed(val, addr);

	raw_spin_unlock_irqrestore(&rmw_lock, flags);
}

static int gic_set_affinity(struct irq_data *d, const struct cpumask *mask_val,
			    bool force)
{
	void __iomem *reg = gic_dist_base(d) + GIC_DIST_TARGET + gic_irq(d);
	struct gic_chip_data *gic = irq_data_get_irq_chip_data(d);
	unsigned int cpu;

	if (unlikely(gic != &gic_data[0]))
		return -EINVAL;

	if (!force)
		cpu = cpumask_any_and(mask_val, cpu_online_mask);
	else
		cpu = cpumask_first(mask_val);

	if (cpu >= NR_GIC_CPU_IF || cpu >= nr_cpu_ids)
		return -EINVAL;

	if (static_branch_unlikely(&needs_rmw_access))
		rmw_writeb(gic_cpu_map[cpu], reg);
	else
		writeb_relaxed(gic_cpu_map[cpu], reg);
	irq_data_update_effective_affinity(d, cpumask_of(cpu));

	return IRQ_SET_MASK_OK_DONE;
}

static void gic_ipi_send_mask(struct irq_data *d, const struct cpumask *mask)
{
	int cpu;
	unsigned long flags, map = 0;

	if (unlikely(nr_cpu_ids == 1)) {
		/* Only one CPU? let's do a self-IPI... */
		writel_relaxed(2 << 24 | d->hwirq,
			       gic_data_dist_base(&gic_data[0]) + GIC_DIST_SOFTINT);
		return;
	}

	gic_lock_irqsave(flags);

	/* Convert our logical CPU mask into a physical one. */
	for_each_cpu(cpu, mask)
		map |= gic_cpu_map[cpu];

	/*
	 * Ensure that stores to Normal memory are visible to the
	 * other CPUs before they observe us issuing the IPI.
	 */
	dmb(ishst);

	/* this always happens on GIC0 */
	writel_relaxed(map << 16 | d->hwirq, gic_data_dist_base(&gic_data[0]) + GIC_DIST_SOFTINT);

	gic_unlock_irqrestore(flags);
}

static int gic_starting_cpu(unsigned int cpu)
{
	gic_cpu_init(&gic_data[0]);
	return 0;
}

static __init void gic_smp_init(void)
{
	struct irq_fwspec sgi_fwspec = {
		.fwnode		= gic_data[0].domain->fwnode,
		.param_count	= 1,
	};
	int base_sgi;

	cpuhp_setup_state_nocalls(CPUHP_AP_IRQ_GIC_STARTING,
				  "irqchip/arm/gic:starting",
				  gic_starting_cpu, NULL);

	base_sgi = irq_domain_alloc_irqs(gic_data[0].domain, 8, NUMA_NO_NODE, &sgi_fwspec);
	if (WARN_ON(base_sgi <= 0))
		return;

	set_smp_ipi_range(base_sgi, 8);
}
#else
#define gic_smp_init()		do { } while(0)
#define gic_set_affinity	NULL
#define gic_ipi_send_mask	NULL
#endif

static const struct irq_chip gic_chip = {
	.irq_mask		= gic_mask_irq,
	.irq_unmask		= gic_unmask_irq,
	.irq_eoi		= gic_eoi_irq,
	.irq_set_type		= gic_set_type,
	.irq_retrigger          = gic_retrigger,
	.irq_set_affinity	= gic_set_affinity,
	.ipi_send_mask		= gic_ipi_send_mask,
	.irq_get_irqchip_state	= gic_irq_get_irqchip_state,
	.irq_set_irqchip_state	= gic_irq_set_irqchip_state,
	.irq_print_chip		= gic_irq_print_chip,
	.flags			= IRQCHIP_SET_TYPE_MASKED |
				  IRQCHIP_SKIP_SET_WAKE |
				  IRQCHIP_MASK_ON_SUSPEND,
};

static const struct irq_chip gic_chip_mode1 = {
	.name			= "GICv2",
	.irq_mask		= gic_eoimode1_mask_irq,
	.irq_unmask		= gic_unmask_irq,
	.irq_eoi		= gic_eoimode1_eoi_irq,
	.irq_set_type		= gic_set_type,
	.irq_retrigger          = gic_retrigger,
	.irq_set_affinity	= gic_set_affinity,
	.ipi_send_mask		= gic_ipi_send_mask,
	.irq_get_irqchip_state	= gic_irq_get_irqchip_state,
	.irq_set_irqchip_state	= gic_irq_set_irqchip_state,
	.irq_set_vcpu_affinity	= gic_irq_set_vcpu_affinity,
	.flags			= IRQCHIP_SET_TYPE_MASKED |
				  IRQCHIP_SKIP_SET_WAKE |
				  IRQCHIP_MASK_ON_SUSPEND,
};

#ifdef CONFIG_BL_SWITCHER
/*
 * gic_send_sgi - send a SGI directly to given CPU interface number
 *
 * cpu_id: the ID for the destination CPU interface
 * irq: the IPI number to send a SGI for
 */
void gic_send_sgi(unsigned int cpu_id, unsigned int irq)
{
	BUG_ON(cpu_id >= NR_GIC_CPU_IF);
	cpu_id = 1 << cpu_id;
	/* this always happens on GIC0 */
	writel_relaxed((cpu_id << 16) | irq, gic_data_dist_base(&gic_data[0]) + GIC_DIST_SOFTINT);
}

/*
 * gic_get_cpu_id - get the CPU interface ID for the specified CPU
 *
 * @cpu: the logical CPU number to get the GIC ID for.
 *
 * Return the CPU interface ID for the given logical CPU number,
 * or -1 if the CPU number is too large or the interface ID is
 * unknown (more than one bit set).
 */
int gic_get_cpu_id(unsigned int cpu)
{
	unsigned int cpu_bit;

	if (cpu >= NR_GIC_CPU_IF)
		return -1;
	cpu_bit = gic_cpu_map[cpu];
	if (cpu_bit & (cpu_bit - 1))
		return -1;
	return __ffs(cpu_bit);
}

/*
 * gic_migrate_target - migrate IRQs to another CPU interface
 *
 * @new_cpu_id: the CPU target ID to migrate IRQs to
 *
 * Migrate all peripheral interrupts with a target matching the current CPU
 * to the interface corresponding to @new_cpu_id.  The CPU interface mapping
 * is also updated.  Targets to other CPU interfaces are unchanged.
 * This must be called with IRQs locally disabled.
 */
void gic_migrate_target(unsigned int new_cpu_id)
{
	unsigned int cur_cpu_id, gic_irqs, gic_nr = 0;
	void __iomem *dist_base;
	int i, ror_val, cpu = smp_processor_id();
	u32 val, cur_target_mask, active_mask;

	BUG_ON(gic_nr >= CONFIG_ARM_GIC_MAX_NR);

	dist_base = gic_data_dist_base(&gic_data[gic_nr]);
	if (!dist_base)
		return;
	gic_irqs = gic_data[gic_nr].gic_irqs;

	cur_cpu_id = __ffs(gic_cpu_map[cpu]);
	cur_target_mask = 0x01010101 << cur_cpu_id;
	ror_val = (cur_cpu_id - new_cpu_id) & 31;

	gic_lock();

	/* Update the target interface for this logical CPU */
	gic_cpu_map[cpu] = 1 << new_cpu_id;

	/*
	 * Find all the peripheral interrupts targeting the current
	 * CPU interface and migrate them to the new CPU interface.
	 * We skip DIST_TARGET 0 to 7 as they are read-only.
	 */
	for (i = 8; i < DIV_ROUND_UP(gic_irqs, 4); i++) {
		val = readl_relaxed(dist_base + GIC_DIST_TARGET + i * 4);
		active_mask = val & cur_target_mask;
		if (active_mask) {
			val &= ~active_mask;
			val |= ror32(active_mask, ror_val);
			writel_relaxed(val, dist_base + GIC_DIST_TARGET + i*4);
		}
	}

	gic_unlock();

	/*
	 * Now let's migrate and clear any potential SGIs that might be
	 * pending for us (cur_cpu_id).  Since GIC_DIST_SGI_PENDING_SET
	 * is a banked register, we can only forward the SGI using
	 * GIC_DIST_SOFTINT.  The original SGI source is lost but Linux
	 * doesn't use that information anyway.
	 *
	 * For the same reason we do not adjust SGI source information
	 * for previously sent SGIs by us to other CPUs either.
	 */
	for (i = 0; i < 16; i += 4) {
		int j;
		val = readl_relaxed(dist_base + GIC_DIST_SGI_PENDING_SET + i);
		if (!val)
			continue;
		writel_relaxed(val, dist_base + GIC_DIST_SGI_PENDING_CLEAR + i);
		for (j = i; j < i + 4; j++) {
			if (val & 0xff)
				writel_relaxed((1 << (new_cpu_id + 16)) | j,
						dist_base + GIC_DIST_SOFTINT);
			val >>= 8;
		}
	}
}

/*
 * gic_get_sgir_physaddr - get the physical address for the SGI register
 *
 * Return the physical address of the SGI register to be used
 * by some early assembly code when the kernel is not yet available.
 */
static unsigned long gic_dist_physaddr;

unsigned long gic_get_sgir_physaddr(void)
{
	if (!gic_dist_physaddr)
		return 0;
	return gic_dist_physaddr + GIC_DIST_SOFTINT;
}

static void __init gic_init_physaddr(struct device_node *node)
{
	struct resource res;
	if (of_address_to_resource(node, 0, &res) == 0) {
		gic_dist_physaddr = res.start;
		pr_info("GIC physical location is %#lx\n", gic_dist_physaddr);
	}
}

#else
#define gic_init_physaddr(node)  do { } while (0)
#endif

static int gic_irq_domain_map(struct irq_domain *d, unsigned int irq,
				irq_hw_number_t hw)
{
	struct gic_chip_data *gic = d->host_data;
	struct irq_data *irqd = irq_desc_get_irq_data(irq_to_desc(irq));
	const struct irq_chip *chip;

	chip = (static_branch_likely(&supports_deactivate_key) &&
		gic == &gic_data[0]) ? &gic_chip_mode1 : &gic_chip;

	switch (hw) {
	case 0 ... 31:
		irq_set_percpu_devid(irq);
		irq_domain_set_info(d, irq, hw, chip, d->host_data,
				    handle_percpu_devid_irq, NULL, NULL);
		break;
	default:
		irq_domain_set_info(d, irq, hw, chip, d->host_data,
				    handle_fasteoi_irq, NULL, NULL);
		irq_set_probe(irq);
		irqd_set_single_target(irqd);
		break;
	}

	/* Prevents SW retriggers which mess up the ACK/EOI ordering */
	irqd_set_handle_enforce_irqctx(irqd);
	return 0;
}

static int gic_irq_domain_translate(struct irq_domain *d,
				    struct irq_fwspec *fwspec,
				    unsigned long *hwirq,
				    unsigned int *type)
{
	if (fwspec->param_count == 1 && fwspec->param[0] < 16) {
		*hwirq = fwspec->param[0];
		*type = IRQ_TYPE_EDGE_RISING;
		return 0;
	}

	if (is_of_node(fwspec->fwnode)) {
		if (fwspec->param_count < 3)
			return -EINVAL;

		switch (fwspec->param[0]) {
		case 0:			/* SPI */
			*hwirq = fwspec->param[1] + 32;
			break;
		case 1:			/* PPI */
			*hwirq = fwspec->param[1] + 16;
			break;
		default:
			return -EINVAL;
		}

		*type = fwspec->param[2] & IRQ_TYPE_SENSE_MASK;

		/* Make it clear that broken DTs are... broken */
		WARN(*type == IRQ_TYPE_NONE,
		     "HW irq %ld has invalid type\n", *hwirq);
		return 0;
	}

	if (is_fwnode_irqchip(fwspec->fwnode)) {
		if(fwspec->param_count != 2)
			return -EINVAL;

		if (fwspec->param[0] < 16) {
			pr_err(FW_BUG "Illegal GSI%d translation request\n",
			       fwspec->param[0]);
			return -EINVAL;
		}

		*hwirq = fwspec->param[0];
		*type = fwspec->param[1];

		WARN(*type == IRQ_TYPE_NONE,
		     "HW irq %ld has invalid type\n", *hwirq);
		return 0;
	}

	return -EINVAL;
}

static int gic_irq_domain_alloc(struct irq_domain *domain, unsigned int virq,
				unsigned int nr_irqs, void *arg)
{
	int i, ret;
	irq_hw_number_t hwirq;
	unsigned int type = IRQ_TYPE_NONE;
	struct irq_fwspec *fwspec = arg;

	ret = gic_irq_domain_translate(domain, fwspec, &hwirq, &type);
	if (ret)
		return ret;

	for (i = 0; i < nr_irqs; i++) {
		ret = gic_irq_domain_map(domain, virq + i, hwirq + i);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct irq_domain_ops gic_irq_domain_hierarchy_ops = {
	.translate = gic_irq_domain_translate,
	.alloc = gic_irq_domain_alloc,
	.free = irq_domain_free_irqs_top,
};

static int gic_init_bases(struct gic_chip_data *gic,
			  struct fwnode_handle *handle)
{
	int gic_irqs, ret;

	if (IS_ENABLED(CONFIG_GIC_NON_BANKED) && gic->percpu_offset) {
		/* Frankein-GIC without banked registers... */
		unsigned int cpu;

		gic->dist_base.percpu_base = alloc_percpu(void __iomem *);
		gic->cpu_base.percpu_base = alloc_percpu(void __iomem *);
		if (WARN_ON(!gic->dist_base.percpu_base ||
			    !gic->cpu_base.percpu_base)) {
			ret = -ENOMEM;
			goto error;
		}

		for_each_possible_cpu(cpu) {
			u32 mpidr = cpu_logical_map(cpu);
			u32 core_id = MPIDR_AFFINITY_LEVEL(mpidr, 0);
			unsigned long offset = gic->percpu_offset * core_id;
			*per_cpu_ptr(gic->dist_base.percpu_base, cpu) =
				gic->raw_dist_base + offset;
			*per_cpu_ptr(gic->cpu_base.percpu_base, cpu) =
				gic->raw_cpu_base + offset;
		}

		enable_frankengic();
	} else {
		/* Normal, sane GIC... */
		WARN(gic->percpu_offset,
		     "GIC_NON_BANKED not enabled, ignoring %08x offset!",
		     gic->percpu_offset);
		gic->dist_base.common_base = gic->raw_dist_base;
		gic->cpu_base.common_base = gic->raw_cpu_base;
	}

	/*
	 * Find out how many interrupts are supported.
	 * The GIC only supports up to 1020 interrupt sources.
	 */
	gic_irqs = readl_relaxed(gic_data_dist_base(gic) + GIC_DIST_CTR) & 0x1f;
	gic_irqs = (gic_irqs + 1) * 32;
	if (gic_irqs > 1020)
		gic_irqs = 1020;
	gic->gic_irqs = gic_irqs;

	gic->domain = irq_domain_create_linear(handle, gic_irqs,
					       &gic_irq_domain_hierarchy_ops,
					       gic);
	if (WARN_ON(!gic->domain)) {
		ret = -ENODEV;
		goto error;
	}

	gic_dist_init(gic);
	ret = gic_cpu_init(gic);
	if (ret)
		goto error;

	ret = gic_pm_init(gic);
	if (ret)
		goto error;

	return 0;

error:
	if (IS_ENABLED(CONFIG_GIC_NON_BANKED) && gic->percpu_offset) {
		free_percpu(gic->dist_base.percpu_base);
		free_percpu(gic->cpu_base.percpu_base);
	}

	return ret;
}

static int __init __gic_init_bases(struct gic_chip_data *gic,
				   struct fwnode_handle *handle)
{
	int i, ret;

	if (WARN_ON(!gic || gic->domain))
		return -EINVAL;

	if (gic == &gic_data[0]) {
		/*
		 * Initialize the CPU interface map to all CPUs.
		 * It will be refined as each CPU probes its ID.
		 * This is only necessary for the primary GIC.
		 */
		for (i = 0; i < NR_GIC_CPU_IF; i++)
			gic_cpu_map[i] = 0xff;

		set_handle_irq(gic_handle_irq);
		if (static_branch_likely(&supports_deactivate_key))
			pr_info("GIC: Using split EOI/Deactivate mode\n");
	}

	ret = gic_init_bases(gic, handle);
	if (gic == &gic_data[0])
		gic_smp_init();

	return ret;
}

static void gic_teardown(struct gic_chip_data *gic)
{
	if (WARN_ON(!gic))
		return;

	if (gic->raw_dist_base)
		iounmap(gic->raw_dist_base);
	if (gic->raw_cpu_base)
		iounmap(gic->raw_cpu_base);
}

static int gic_cnt __initdata;
static bool gicv2_force_probe;

static int __init gicv2_force_probe_cfg(char *buf)
{
	return kstrtobool(buf, &gicv2_force_probe);
}
early_param("irqchip.gicv2_force_probe", gicv2_force_probe_cfg);

static bool gic_check_eoimode(struct device_node *node, void __iomem **base)
{
	struct resource cpuif_res;

	of_address_to_resource(node, 1, &cpuif_res);

	if (!is_hyp_mode_available())
		return false;
	if (resource_size(&cpuif_res) < SZ_8K) {
		void __iomem *alt;
		/*
		 * Check for a stupid firmware that only exposes the
		 * first page of a GICv2.
		 */
		if (!gic_check_gicv2(*base))
			return false;

		if (!gicv2_force_probe) {
			pr_warn("GIC: GICv2 detected, but range too small and irqchip.gicv2_force_probe not set\n");
			return false;
		}

		alt = ioremap(cpuif_res.start, SZ_8K);
		if (!alt)
			return false;
		if (!gic_check_gicv2(alt + SZ_4K)) {
			/*
			 * The first page was that of a GICv2, and
			 * the second was *something*. Let's trust it
			 * to be a GICv2, and update the mapping.
			 */
			pr_warn("GIC: GICv2 at %pa, but range is too small (broken DT?), assuming 8kB\n",
				&cpuif_res.start);
			iounmap(*base);
			*base = alt;
			return true;
		}

		/*
		 * We detected *two* initial GICv2 pages in a
		 * row. Could be a GICv2 aliased over two 64kB
		 * pages. Update the resource, map the iospace, and
		 * pray.
		 */
		iounmap(alt);
		alt = ioremap(cpuif_res.start, SZ_128K);
		if (!alt)
			return false;
		pr_warn("GIC: Aliased GICv2 at %pa, trying to find the canonical range over 128kB\n",
			&cpuif_res.start);
		cpuif_res.end = cpuif_res.start + SZ_128K -1;
		iounmap(*base);
		*base = alt;
	}
	if (resource_size(&cpuif_res) == SZ_128K) {
		/*
		 * Verify that we have the first 4kB of a GICv2
		 * aliased over the first 64kB by checking the
		 * GICC_IIDR register on both ends.
		 */
		if (!gic_check_gicv2(*base) ||
		    !gic_check_gicv2(*base + 0xf000))
			return false;

		/*
		 * Move the base up by 60kB, so that we have a 8kB
		 * contiguous region, which allows us to use GICC_DIR
		 * at its normal offset. Please pass me that bucket.
		 */
		*base += 0xf000;
		cpuif_res.start += 0xf000;
		pr_warn("GIC: Adjusting CPU interface base to %pa\n",
			&cpuif_res.start);
	}

	return true;
}

static bool gic_enable_rmw_access(void *data)
{
	/*
	 * The EMEV2 class of machines has a broken interconnect, and
	 * locks up on accesses that are less than 32bit. So far, only
	 * the affinity setting requires it.
	 */
	if (of_machine_is_compatible("renesas,emev2")) {
		static_branch_enable(&needs_rmw_access);
		return true;
	}

	return false;
}

static const struct gic_quirk gic_quirks[] = {
	{
		.desc		= "broken byte access",
		.compatible	= "arm,pl390",
		.init		= gic_enable_rmw_access,
	},
	{ },
};

static int gic_of_setup(struct gic_chip_data *gic, struct device_node *node)
{
	if (!gic || !node)
		return -EINVAL;

	gic->raw_dist_base = of_iomap(node, 0);
	if (WARN(!gic->raw_dist_base, "unable to map gic dist registers\n"))
		goto error;

	gic->raw_cpu_base = of_iomap(node, 1);
	if (WARN(!gic->raw_cpu_base, "unable to map gic cpu registers\n"))
		goto error;

	if (of_property_read_u32(node, "cpu-offset", &gic->percpu_offset))
		gic->percpu_offset = 0;

	gic_enable_of_quirks(node, gic_quirks, gic);

	return 0;

error:
	gic_teardown(gic);

	return -ENOMEM;
}

int gic_of_init_child(struct device *dev, struct gic_chip_data **gic, int irq)
{
	int ret;

	if (!dev || !dev->of_node || !gic || !irq)
		return -EINVAL;

	*gic = devm_kzalloc(dev, sizeof(**gic), GFP_KERNEL);
	if (!*gic)
		return -ENOMEM;

	ret = gic_of_setup(*gic, dev->of_node);
	if (ret)
		return ret;

	ret = gic_init_bases(*gic, &dev->of_node->fwnode);
	if (ret) {
		gic_teardown(*gic);
		return ret;
	}

	irq_domain_set_pm_device((*gic)->domain, dev);
	irq_set_chained_handler_and_data(irq, gic_handle_cascade_irq, *gic);

	return 0;
}

static void __init gic_of_setup_kvm_info(struct device_node *node)
{
	int ret;
	struct resource *vctrl_res = &gic_v2_kvm_info.vctrl;
	struct resource *vcpu_res = &gic_v2_kvm_info.vcpu;

	gic_v2_kvm_info.type = GIC_V2;

	gic_v2_kvm_info.maint_irq = irq_of_parse_and_map(node, 0);
	if (!gic_v2_kvm_info.maint_irq)
		return;

	ret = of_address_to_resource(node, 2, vctrl_res);
	if (ret)
		return;

	ret = of_address_to_resource(node, 3, vcpu_res);
	if (ret)
		return;

	if (static_branch_likely(&supports_deactivate_key))
		vgic_set_kvm_info(&gic_v2_kvm_info);
}

int __init
gic_of_init(struct device_node *node, struct device_node *parent)
{
	struct gic_chip_data *gic;
	int irq, ret;

	if (WARN_ON(!node))
		return -ENODEV;

	if (WARN_ON(gic_cnt >= CONFIG_ARM_GIC_MAX_NR))
		return -EINVAL;

	gic = &gic_data[gic_cnt];

	ret = gic_of_setup(gic, node);
	if (ret)
		return ret;

	/*
	 * Disable split EOI/Deactivate if either HYP is not available
	 * or the CPU interface is too small.
	 */
	if (gic_cnt == 0 && !gic_check_eoimode(node, &gic->raw_cpu_base))
		static_branch_disable(&supports_deactivate_key);

	ret = __gic_init_bases(gic, &node->fwnode);
	if (ret) {
		gic_teardown(gic);
		return ret;
	}

	if (!gic_cnt) {
		gic_init_physaddr(node);
		gic_of_setup_kvm_info(node);
	}

	if (parent) {
		irq = irq_of_parse_and_map(node, 0);
		gic_cascade_irq(gic_cnt, irq);
	}

	if (IS_ENABLED(CONFIG_ARM_GIC_V2M))
		gicv2m_init(&node->fwnode, gic_data[gic_cnt].domain);

	gic_cnt++;
	return 0;
}
IRQCHIP_DECLARE(gic_400, "arm,gic-400", gic_of_init);
IRQCHIP_DECLARE(arm11mp_gic, "arm,arm11mp-gic", gic_of_init);
IRQCHIP_DECLARE(arm1176jzf_dc_gic, "arm,arm1176jzf-devchip-gic", gic_of_init);
IRQCHIP_DECLARE(cortex_a15_gic, "arm,cortex-a15-gic", gic_of_init);
IRQCHIP_DECLARE(cortex_a9_gic, "arm,cortex-a9-gic", gic_of_init);
IRQCHIP_DECLARE(cortex_a7_gic, "arm,cortex-a7-gic", gic_of_init);
IRQCHIP_DECLARE(msm_8660_qgic, "qcom,msm-8660-qgic", gic_of_init);
IRQCHIP_DECLARE(msm_qgic2, "qcom,msm-qgic2", gic_of_init);
IRQCHIP_DECLARE(pl390, "arm,pl390", gic_of_init);

#ifdef CONFIG_ACPI
static struct
{
	phys_addr_t cpu_phys_base;
	u32 maint_irq;
	int maint_irq_mode;
	phys_addr_t vctrl_base;
	phys_addr_t vcpu_base;
} acpi_data __initdata;

static int __init
gic_acpi_parse_madt_cpu(union acpi_subtable_headers *header,
			const unsigned long end)
{
	struct acpi_madt_generic_interrupt *processor;
	phys_addr_t gic_cpu_base;
	static int cpu_base_assigned;

	processor = (struct acpi_madt_generic_interrupt *)header;

	if (BAD_MADT_GICC_ENTRY(processor, end))
		return -EINVAL;

	/*
	 * There is no support for non-banked GICv1/2 register in ACPI spec.
	 * All CPU interface addresses have to be the same.
	 */
	gic_cpu_base = processor->base_address;
	if (cpu_base_assigned && gic_cpu_base != acpi_data.cpu_phys_base)
		return -EINVAL;

	acpi_data.cpu_phys_base = gic_cpu_base;
	acpi_data.maint_irq = processor->vgic_interrupt;
	acpi_data.maint_irq_mode = (processor->flags & ACPI_MADT_VGIC_IRQ_MODE) ?
				    ACPI_EDGE_SENSITIVE : ACPI_LEVEL_SENSITIVE;
	acpi_data.vctrl_base = processor->gich_base_address;
	acpi_data.vcpu_base = processor->gicv_base_address;

	cpu_base_assigned = 1;
	return 0;
}

/* The things you have to do to just *count* something... */
static int __init acpi_dummy_func(union acpi_subtable_headers *header,
				  const unsigned long end)
{
	return 0;
}

static bool __init acpi_gic_redist_is_present(void)
{
	return acpi_table_parse_madt(ACPI_MADT_TYPE_GENERIC_REDISTRIBUTOR,
				     acpi_dummy_func, 0) > 0;
}

static bool __init gic_validate_dist(struct acpi_subtable_header *header,
				     struct acpi_probe_entry *ape)
{
	struct acpi_madt_generic_distributor *dist;
	dist = (struct acpi_madt_generic_distributor *)header;

	return (dist->version == ape->driver_data &&
		(dist->version != ACPI_MADT_GIC_VERSION_NONE ||
		 !acpi_gic_redist_is_present()));
}

#define ACPI_GICV2_DIST_MEM_SIZE	(SZ_4K)
#define ACPI_GIC_CPU_IF_MEM_SIZE	(SZ_8K)
#define ACPI_GICV2_VCTRL_MEM_SIZE	(SZ_4K)
#define ACPI_GICV2_VCPU_MEM_SIZE	(SZ_8K)

static void __init gic_acpi_setup_kvm_info(void)
{
	int irq;
	struct resource *vctrl_res = &gic_v2_kvm_info.vctrl;
	struct resource *vcpu_res = &gic_v2_kvm_info.vcpu;

	gic_v2_kvm_info.type = GIC_V2;

	if (!acpi_data.vctrl_base)
		return;

	vctrl_res->flags = IORESOURCE_MEM;
	vctrl_res->start = acpi_data.vctrl_base;
	vctrl_res->end = vctrl_res->start + ACPI_GICV2_VCTRL_MEM_SIZE - 1;

	if (!acpi_data.vcpu_base)
		return;

	vcpu_res->flags = IORESOURCE_MEM;
	vcpu_res->start = acpi_data.vcpu_base;
	vcpu_res->end = vcpu_res->start + ACPI_GICV2_VCPU_MEM_SIZE - 1;

	irq = acpi_register_gsi(NULL, acpi_data.maint_irq,
				acpi_data.maint_irq_mode,
				ACPI_ACTIVE_HIGH);
	if (irq <= 0)
		return;

	gic_v2_kvm_info.maint_irq = irq;

	vgic_set_kvm_info(&gic_v2_kvm_info);
}

static struct fwnode_handle *gsi_domain_handle;

static struct fwnode_handle *gic_v2_get_gsi_domain_id(u32 gsi)
{
	return gsi_domain_handle;
}

static int __init gic_v2_acpi_init(union acpi_subtable_headers *header,
				   const unsigned long end)
{
	struct acpi_madt_generic_distributor *dist;
	struct gic_chip_data *gic = &gic_data[0];
	int count, ret;

	/* Collect CPU base addresses */
	count = acpi_table_parse_madt(ACPI_MADT_TYPE_GENERIC_INTERRUPT,
				      gic_acpi_parse_madt_cpu, 0);
	if (count <= 0) {
		pr_err("No valid GICC entries exist\n");
		return -EINVAL;
	}

	gic->raw_cpu_base = ioremap(acpi_data.cpu_phys_base, ACPI_GIC_CPU_IF_MEM_SIZE);
	if (!gic->raw_cpu_base) {
		pr_err("Unable to map GICC registers\n");
		return -ENOMEM;
	}

	dist = (struct acpi_madt_generic_distributor *)header;
	gic->raw_dist_base = ioremap(dist->base_address,
				     ACPI_GICV2_DIST_MEM_SIZE);
	if (!gic->raw_dist_base) {
		pr_err("Unable to map GICD registers\n");
		gic_teardown(gic);
		return -ENOMEM;
	}

	/*
	 * Disable split EOI/Deactivate if HYP is not available. ACPI
	 * guarantees that we'll always have a GICv2, so the CPU
	 * interface will always be the right size.
	 */
	if (!is_hyp_mode_available())
		static_branch_disable(&supports_deactivate_key);

	/*
	 * Initialize GIC instance zero (no multi-GIC support).
	 */
	gsi_domain_handle = irq_domain_alloc_fwnode(&dist->base_address);
	if (!gsi_domain_handle) {
		pr_err("Unable to allocate domain handle\n");
		gic_teardown(gic);
		return -ENOMEM;
	}

	ret = __gic_init_bases(gic, gsi_domain_handle);
	if (ret) {
		pr_err("Failed to initialise GIC\n");
		irq_domain_free_fwnode(gsi_domain_handle);
		gic_teardown(gic);
		return ret;
	}

	acpi_set_irq_model(ACPI_IRQ_MODEL_GIC, gic_v2_get_gsi_domain_id);

	if (IS_ENABLED(CONFIG_ARM_GIC_V2M))
		gicv2m_init(NULL, gic_data[0].domain);

	if (static_branch_likely(&supports_deactivate_key))
		gic_acpi_setup_kvm_info();

	return 0;
}
IRQCHIP_ACPI_DECLARE(gic_v2, ACPI_MADT_TYPE_GENERIC_DISTRIBUTOR,
		     gic_validate_dist, ACPI_MADT_GIC_VERSION_V2,
		     gic_v2_acpi_init);
IRQCHIP_ACPI_DECLARE(gic_v2_maybe, ACPI_MADT_TYPE_GENERIC_DISTRIBUTOR,
		     gic_validate_dist, ACPI_MADT_GIC_VERSION_NONE,
		     gic_v2_acpi_init);
#endif
