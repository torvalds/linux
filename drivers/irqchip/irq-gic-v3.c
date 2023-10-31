// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013-2017 ARM Limited, All Rights Reserved.
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 */

#define pr_fmt(fmt)	"GICv3: " fmt

#include <linux/acpi.h>
#include <linux/cpu.h>
#include <linux/cpu_pm.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/kstrtox.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/percpu.h>
#include <linux/refcount.h>
#include <linux/slab.h>

#include <linux/irqchip.h>
#include <linux/irqchip/arm-gic-common.h>
#include <linux/irqchip/arm-gic-v3.h>
#include <linux/irqchip/irq-partition-percpu.h>
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/arm-smccc.h>

#include <asm/cputype.h>
#include <asm/exception.h>
#include <asm/smp_plat.h>
#include <asm/virt.h>

#include "irq-gic-common.h"

#define GICD_INT_NMI_PRI	(GICD_INT_DEF_PRI & ~0x80)

#define FLAGS_WORKAROUND_GICR_WAKER_MSM8996	(1ULL << 0)
#define FLAGS_WORKAROUND_CAVIUM_ERRATUM_38539	(1ULL << 1)
#define FLAGS_WORKAROUND_MTK_GICR_SAVE		(1ULL << 2)
#define FLAGS_WORKAROUND_ASR_ERRATUM_8601001	(1ULL << 3)

#define GIC_IRQ_TYPE_PARTITION	(GIC_IRQ_TYPE_LPI + 1)

struct redist_region {
	void __iomem		*redist_base;
	phys_addr_t		phys_base;
	bool			single_redist;
};

struct gic_chip_data {
	struct fwnode_handle	*fwnode;
	phys_addr_t		dist_phys_base;
	void __iomem		*dist_base;
	struct redist_region	*redist_regions;
	struct rdists		rdists;
	struct irq_domain	*domain;
	u64			redist_stride;
	u32			nr_redist_regions;
	u64			flags;
	bool			has_rss;
	unsigned int		ppi_nr;
	struct partition_desc	**ppi_descs;
};

#define T241_CHIPS_MAX		4
static void __iomem *t241_dist_base_alias[T241_CHIPS_MAX] __read_mostly;
static DEFINE_STATIC_KEY_FALSE(gic_nvidia_t241_erratum);

static DEFINE_STATIC_KEY_FALSE(gic_arm64_2941627_erratum);

static struct gic_chip_data gic_data __read_mostly;
static DEFINE_STATIC_KEY_TRUE(supports_deactivate_key);

#define GIC_ID_NR	(1U << GICD_TYPER_ID_BITS(gic_data.rdists.gicd_typer))
#define GIC_LINE_NR	min(GICD_TYPER_SPIS(gic_data.rdists.gicd_typer), 1020U)
#define GIC_ESPI_NR	GICD_TYPER_ESPIS(gic_data.rdists.gicd_typer)

/*
 * The behaviours of RPR and PMR registers differ depending on the value of
 * SCR_EL3.FIQ, and the behaviour of non-secure priority registers of the
 * distributor and redistributors depends on whether security is enabled in the
 * GIC.
 *
 * When security is enabled, non-secure priority values from the (re)distributor
 * are presented to the GIC CPUIF as follow:
 *     (GIC_(R)DIST_PRI[irq] >> 1) | 0x80;
 *
 * If SCR_EL3.FIQ == 1, the values written to/read from PMR and RPR at non-secure
 * EL1 are subject to a similar operation thus matching the priorities presented
 * from the (re)distributor when security is enabled. When SCR_EL3.FIQ == 0,
 * these values are unchanged by the GIC.
 *
 * see GICv3/GICv4 Architecture Specification (IHI0069D):
 * - section 4.8.1 Non-secure accesses to register fields for Secure interrupt
 *   priorities.
 * - Figure 4-7 Secure read of the priority field for a Non-secure Group 1
 *   interrupt.
 */
static DEFINE_STATIC_KEY_FALSE(supports_pseudo_nmis);

DEFINE_STATIC_KEY_FALSE(gic_nonsecure_priorities);
EXPORT_SYMBOL(gic_nonsecure_priorities);

/*
 * When the Non-secure world has access to group 0 interrupts (as a
 * consequence of SCR_EL3.FIQ == 0), reading the ICC_RPR_EL1 register will
 * return the Distributor's view of the interrupt priority.
 *
 * When GIC security is enabled (GICD_CTLR.DS == 0), the interrupt priority
 * written by software is moved to the Non-secure range by the Distributor.
 *
 * If both are true (which is when gic_nonsecure_priorities gets enabled),
 * we need to shift down the priority programmed by software to match it
 * against the value returned by ICC_RPR_EL1.
 */
#define GICD_INT_RPR_PRI(priority)					\
	({								\
		u32 __priority = (priority);				\
		if (static_branch_unlikely(&gic_nonsecure_priorities))	\
			__priority = 0x80 | (__priority >> 1);		\
									\
		__priority;						\
	})

/* ppi_nmi_refs[n] == number of cpus having ppi[n + 16] set as NMI */
static refcount_t *ppi_nmi_refs;

static struct gic_kvm_info gic_v3_kvm_info __initdata;
static DEFINE_PER_CPU(bool, has_rss);

#define MPIDR_RS(mpidr)			(((mpidr) & 0xF0UL) >> 4)
#define gic_data_rdist()		(this_cpu_ptr(gic_data.rdists.rdist))
#define gic_data_rdist_rd_base()	(gic_data_rdist()->rd_base)
#define gic_data_rdist_sgi_base()	(gic_data_rdist_rd_base() + SZ_64K)

/* Our default, arbitrary priority value. Linux only uses one anyway. */
#define DEFAULT_PMR_VALUE	0xf0

enum gic_intid_range {
	SGI_RANGE,
	PPI_RANGE,
	SPI_RANGE,
	EPPI_RANGE,
	ESPI_RANGE,
	LPI_RANGE,
	__INVALID_RANGE__
};

static enum gic_intid_range __get_intid_range(irq_hw_number_t hwirq)
{
	switch (hwirq) {
	case 0 ... 15:
		return SGI_RANGE;
	case 16 ... 31:
		return PPI_RANGE;
	case 32 ... 1019:
		return SPI_RANGE;
	case EPPI_BASE_INTID ... (EPPI_BASE_INTID + 63):
		return EPPI_RANGE;
	case ESPI_BASE_INTID ... (ESPI_BASE_INTID + 1023):
		return ESPI_RANGE;
	case 8192 ... GENMASK(23, 0):
		return LPI_RANGE;
	default:
		return __INVALID_RANGE__;
	}
}

static enum gic_intid_range get_intid_range(struct irq_data *d)
{
	return __get_intid_range(d->hwirq);
}

static inline unsigned int gic_irq(struct irq_data *d)
{
	return d->hwirq;
}

static inline bool gic_irq_in_rdist(struct irq_data *d)
{
	switch (get_intid_range(d)) {
	case SGI_RANGE:
	case PPI_RANGE:
	case EPPI_RANGE:
		return true;
	default:
		return false;
	}
}

static inline void __iomem *gic_dist_base_alias(struct irq_data *d)
{
	if (static_branch_unlikely(&gic_nvidia_t241_erratum)) {
		irq_hw_number_t hwirq = irqd_to_hwirq(d);
		u32 chip;

		/*
		 * For the erratum T241-FABRIC-4, read accesses to GICD_In{E}
		 * registers are directed to the chip that owns the SPI. The
		 * the alias region can also be used for writes to the
		 * GICD_In{E} except GICD_ICENABLERn. Each chip has support
		 * for 320 {E}SPIs. Mappings for all 4 chips:
		 *    Chip0 = 32-351
		 *    Chip1 = 352-671
		 *    Chip2 = 672-991
		 *    Chip3 = 4096-4415
		 */
		switch (__get_intid_range(hwirq)) {
		case SPI_RANGE:
			chip = (hwirq - 32) / 320;
			break;
		case ESPI_RANGE:
			chip = 3;
			break;
		default:
			unreachable();
		}
		return t241_dist_base_alias[chip];
	}

	return gic_data.dist_base;
}

static inline void __iomem *gic_dist_base(struct irq_data *d)
{
	switch (get_intid_range(d)) {
	case SGI_RANGE:
	case PPI_RANGE:
	case EPPI_RANGE:
		/* SGI+PPI -> SGI_base for this CPU */
		return gic_data_rdist_sgi_base();

	case SPI_RANGE:
	case ESPI_RANGE:
		/* SPI -> dist_base */
		return gic_data.dist_base;

	default:
		return NULL;
	}
}

static void gic_do_wait_for_rwp(void __iomem *base, u32 bit)
{
	u32 count = 1000000;	/* 1s! */

	while (readl_relaxed(base + GICD_CTLR) & bit) {
		count--;
		if (!count) {
			pr_err_ratelimited("RWP timeout, gone fishing\n");
			return;
		}
		cpu_relax();
		udelay(1);
	}
}

/* Wait for completion of a distributor change */
static void gic_dist_wait_for_rwp(void)
{
	gic_do_wait_for_rwp(gic_data.dist_base, GICD_CTLR_RWP);
}

/* Wait for completion of a redistributor change */
static void gic_redist_wait_for_rwp(void)
{
	gic_do_wait_for_rwp(gic_data_rdist_rd_base(), GICR_CTLR_RWP);
}

#ifdef CONFIG_ARM64

static u64 __maybe_unused gic_read_iar(void)
{
	if (cpus_have_const_cap(ARM64_WORKAROUND_CAVIUM_23154))
		return gic_read_iar_cavium_thunderx();
	else
		return gic_read_iar_common();
}
#endif

static void gic_enable_redist(bool enable)
{
	void __iomem *rbase;
	u32 count = 1000000;	/* 1s! */
	u32 val;

	if (gic_data.flags & FLAGS_WORKAROUND_GICR_WAKER_MSM8996)
		return;

	rbase = gic_data_rdist_rd_base();

	val = readl_relaxed(rbase + GICR_WAKER);
	if (enable)
		/* Wake up this CPU redistributor */
		val &= ~GICR_WAKER_ProcessorSleep;
	else
		val |= GICR_WAKER_ProcessorSleep;
	writel_relaxed(val, rbase + GICR_WAKER);

	if (!enable) {		/* Check that GICR_WAKER is writeable */
		val = readl_relaxed(rbase + GICR_WAKER);
		if (!(val & GICR_WAKER_ProcessorSleep))
			return;	/* No PM support in this redistributor */
	}

	while (--count) {
		val = readl_relaxed(rbase + GICR_WAKER);
		if (enable ^ (bool)(val & GICR_WAKER_ChildrenAsleep))
			break;
		cpu_relax();
		udelay(1);
	}
	if (!count)
		pr_err_ratelimited("redistributor failed to %s...\n",
				   enable ? "wakeup" : "sleep");
}

/*
 * Routines to disable, enable, EOI and route interrupts
 */
static u32 convert_offset_index(struct irq_data *d, u32 offset, u32 *index)
{
	switch (get_intid_range(d)) {
	case SGI_RANGE:
	case PPI_RANGE:
	case SPI_RANGE:
		*index = d->hwirq;
		return offset;
	case EPPI_RANGE:
		/*
		 * Contrary to the ESPI range, the EPPI range is contiguous
		 * to the PPI range in the registers, so let's adjust the
		 * displacement accordingly. Consistency is overrated.
		 */
		*index = d->hwirq - EPPI_BASE_INTID + 32;
		return offset;
	case ESPI_RANGE:
		*index = d->hwirq - ESPI_BASE_INTID;
		switch (offset) {
		case GICD_ISENABLER:
			return GICD_ISENABLERnE;
		case GICD_ICENABLER:
			return GICD_ICENABLERnE;
		case GICD_ISPENDR:
			return GICD_ISPENDRnE;
		case GICD_ICPENDR:
			return GICD_ICPENDRnE;
		case GICD_ISACTIVER:
			return GICD_ISACTIVERnE;
		case GICD_ICACTIVER:
			return GICD_ICACTIVERnE;
		case GICD_IPRIORITYR:
			return GICD_IPRIORITYRnE;
		case GICD_ICFGR:
			return GICD_ICFGRnE;
		case GICD_IROUTER:
			return GICD_IROUTERnE;
		default:
			break;
		}
		break;
	default:
		break;
	}

	WARN_ON(1);
	*index = d->hwirq;
	return offset;
}

static int gic_peek_irq(struct irq_data *d, u32 offset)
{
	void __iomem *base;
	u32 index, mask;

	offset = convert_offset_index(d, offset, &index);
	mask = 1 << (index % 32);

	if (gic_irq_in_rdist(d))
		base = gic_data_rdist_sgi_base();
	else
		base = gic_dist_base_alias(d);

	return !!(readl_relaxed(base + offset + (index / 32) * 4) & mask);
}

static void gic_poke_irq(struct irq_data *d, u32 offset)
{
	void __iomem *base;
	u32 index, mask;

	offset = convert_offset_index(d, offset, &index);
	mask = 1 << (index % 32);

	if (gic_irq_in_rdist(d))
		base = gic_data_rdist_sgi_base();
	else
		base = gic_data.dist_base;

	writel_relaxed(mask, base + offset + (index / 32) * 4);
}

static void gic_mask_irq(struct irq_data *d)
{
	gic_poke_irq(d, GICD_ICENABLER);
	if (gic_irq_in_rdist(d))
		gic_redist_wait_for_rwp();
	else
		gic_dist_wait_for_rwp();
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
		gic_poke_irq(d, GICD_ICACTIVER);
}

static void gic_unmask_irq(struct irq_data *d)
{
	gic_poke_irq(d, GICD_ISENABLER);
}

static inline bool gic_supports_nmi(void)
{
	return IS_ENABLED(CONFIG_ARM64_PSEUDO_NMI) &&
	       static_branch_likely(&supports_pseudo_nmis);
}

static int gic_irq_set_irqchip_state(struct irq_data *d,
				     enum irqchip_irq_state which, bool val)
{
	u32 reg;

	if (d->hwirq >= 8192) /* SGI/PPI/SPI only */
		return -EINVAL;

	switch (which) {
	case IRQCHIP_STATE_PENDING:
		reg = val ? GICD_ISPENDR : GICD_ICPENDR;
		break;

	case IRQCHIP_STATE_ACTIVE:
		reg = val ? GICD_ISACTIVER : GICD_ICACTIVER;
		break;

	case IRQCHIP_STATE_MASKED:
		if (val) {
			gic_mask_irq(d);
			return 0;
		}
		reg = GICD_ISENABLER;
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
	if (d->hwirq >= 8192) /* PPI/SPI only */
		return -EINVAL;

	switch (which) {
	case IRQCHIP_STATE_PENDING:
		*val = gic_peek_irq(d, GICD_ISPENDR);
		break;

	case IRQCHIP_STATE_ACTIVE:
		*val = gic_peek_irq(d, GICD_ISACTIVER);
		break;

	case IRQCHIP_STATE_MASKED:
		*val = !gic_peek_irq(d, GICD_ISENABLER);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static void gic_irq_set_prio(struct irq_data *d, u8 prio)
{
	void __iomem *base = gic_dist_base(d);
	u32 offset, index;

	offset = convert_offset_index(d, GICD_IPRIORITYR, &index);

	writeb_relaxed(prio, base + offset + index);
}

static u32 __gic_get_ppi_index(irq_hw_number_t hwirq)
{
	switch (__get_intid_range(hwirq)) {
	case PPI_RANGE:
		return hwirq - 16;
	case EPPI_RANGE:
		return hwirq - EPPI_BASE_INTID + 16;
	default:
		unreachable();
	}
}

static u32 gic_get_ppi_index(struct irq_data *d)
{
	return __gic_get_ppi_index(d->hwirq);
}

static int gic_irq_nmi_setup(struct irq_data *d)
{
	struct irq_desc *desc = irq_to_desc(d->irq);

	if (!gic_supports_nmi())
		return -EINVAL;

	if (gic_peek_irq(d, GICD_ISENABLER)) {
		pr_err("Cannot set NMI property of enabled IRQ %u\n", d->irq);
		return -EINVAL;
	}

	/*
	 * A secondary irq_chip should be in charge of LPI request,
	 * it should not be possible to get there
	 */
	if (WARN_ON(gic_irq(d) >= 8192))
		return -EINVAL;

	/* desc lock should already be held */
	if (gic_irq_in_rdist(d)) {
		u32 idx = gic_get_ppi_index(d);

		/* Setting up PPI as NMI, only switch handler for first NMI */
		if (!refcount_inc_not_zero(&ppi_nmi_refs[idx])) {
			refcount_set(&ppi_nmi_refs[idx], 1);
			desc->handle_irq = handle_percpu_devid_fasteoi_nmi;
		}
	} else {
		desc->handle_irq = handle_fasteoi_nmi;
	}

	gic_irq_set_prio(d, GICD_INT_NMI_PRI);

	return 0;
}

static void gic_irq_nmi_teardown(struct irq_data *d)
{
	struct irq_desc *desc = irq_to_desc(d->irq);

	if (WARN_ON(!gic_supports_nmi()))
		return;

	if (gic_peek_irq(d, GICD_ISENABLER)) {
		pr_err("Cannot set NMI property of enabled IRQ %u\n", d->irq);
		return;
	}

	/*
	 * A secondary irq_chip should be in charge of LPI request,
	 * it should not be possible to get there
	 */
	if (WARN_ON(gic_irq(d) >= 8192))
		return;

	/* desc lock should already be held */
	if (gic_irq_in_rdist(d)) {
		u32 idx = gic_get_ppi_index(d);

		/* Tearing down NMI, only switch handler for last NMI */
		if (refcount_dec_and_test(&ppi_nmi_refs[idx]))
			desc->handle_irq = handle_percpu_devid_irq;
	} else {
		desc->handle_irq = handle_fasteoi_irq;
	}

	gic_irq_set_prio(d, GICD_INT_DEF_PRI);
}

static bool gic_arm64_erratum_2941627_needed(struct irq_data *d)
{
	enum gic_intid_range range;

	if (!static_branch_unlikely(&gic_arm64_2941627_erratum))
		return false;

	range = get_intid_range(d);

	/*
	 * The workaround is needed if the IRQ is an SPI and
	 * the target cpu is different from the one we are
	 * executing on.
	 */
	return (range == SPI_RANGE || range == ESPI_RANGE) &&
		!cpumask_test_cpu(raw_smp_processor_id(),
				  irq_data_get_effective_affinity_mask(d));
}

static void gic_eoi_irq(struct irq_data *d)
{
	write_gicreg(gic_irq(d), ICC_EOIR1_EL1);
	isb();

	if (gic_arm64_erratum_2941627_needed(d)) {
		/*
		 * Make sure the GIC stream deactivate packet
		 * issued by ICC_EOIR1_EL1 has completed before
		 * deactivating through GICD_IACTIVER.
		 */
		dsb(sy);
		gic_poke_irq(d, GICD_ICACTIVER);
	}
}

static void gic_eoimode1_eoi_irq(struct irq_data *d)
{
	/*
	 * No need to deactivate an LPI, or an interrupt that
	 * is is getting forwarded to a vcpu.
	 */
	if (gic_irq(d) >= 8192 || irqd_is_forwarded_to_vcpu(d))
		return;

	if (!gic_arm64_erratum_2941627_needed(d))
		gic_write_dir(gic_irq(d));
	else
		gic_poke_irq(d, GICD_ICACTIVER);
}

static int gic_set_type(struct irq_data *d, unsigned int type)
{
	enum gic_intid_range range;
	unsigned int irq = gic_irq(d);
	void __iomem *base;
	u32 offset, index;
	int ret;

	range = get_intid_range(d);

	/* Interrupt configuration for SGIs can't be changed */
	if (range == SGI_RANGE)
		return type != IRQ_TYPE_EDGE_RISING ? -EINVAL : 0;

	/* SPIs have restrictions on the supported types */
	if ((range == SPI_RANGE || range == ESPI_RANGE) &&
	    type != IRQ_TYPE_LEVEL_HIGH && type != IRQ_TYPE_EDGE_RISING)
		return -EINVAL;

	if (gic_irq_in_rdist(d))
		base = gic_data_rdist_sgi_base();
	else
		base = gic_dist_base_alias(d);

	offset = convert_offset_index(d, GICD_ICFGR, &index);

	ret = gic_configure_irq(index, type, base + offset, NULL);
	if (ret && (range == PPI_RANGE || range == EPPI_RANGE)) {
		/* Misconfigured PPIs are usually not fatal */
		pr_warn("GIC: PPI INTID%d is secure or misconfigured\n", irq);
		ret = 0;
	}

	return ret;
}

static int gic_irq_set_vcpu_affinity(struct irq_data *d, void *vcpu)
{
	if (get_intid_range(d) == SGI_RANGE)
		return -EINVAL;

	if (vcpu)
		irqd_set_forwarded_to_vcpu(d);
	else
		irqd_clr_forwarded_to_vcpu(d);
	return 0;
}

static u64 gic_cpu_to_affinity(int cpu)
{
	u64 mpidr = cpu_logical_map(cpu);
	u64 aff;

	/* ASR8601 needs to have its affinities shifted down... */
	if (unlikely(gic_data.flags & FLAGS_WORKAROUND_ASR_ERRATUM_8601001))
		mpidr = (MPIDR_AFFINITY_LEVEL(mpidr, 1)	|
			 (MPIDR_AFFINITY_LEVEL(mpidr, 2) << 8));

	aff = ((u64)MPIDR_AFFINITY_LEVEL(mpidr, 3) << 32 |
	       MPIDR_AFFINITY_LEVEL(mpidr, 2) << 16 |
	       MPIDR_AFFINITY_LEVEL(mpidr, 1) << 8  |
	       MPIDR_AFFINITY_LEVEL(mpidr, 0));

	return aff;
}

static void gic_deactivate_unhandled(u32 irqnr)
{
	if (static_branch_likely(&supports_deactivate_key)) {
		if (irqnr < 8192)
			gic_write_dir(irqnr);
	} else {
		write_gicreg(irqnr, ICC_EOIR1_EL1);
		isb();
	}
}

/*
 * Follow a read of the IAR with any HW maintenance that needs to happen prior
 * to invoking the relevant IRQ handler. We must do two things:
 *
 * (1) Ensure instruction ordering between a read of IAR and subsequent
 *     instructions in the IRQ handler using an ISB.
 *
 *     It is possible for the IAR to report an IRQ which was signalled *after*
 *     the CPU took an IRQ exception as multiple interrupts can race to be
 *     recognized by the GIC, earlier interrupts could be withdrawn, and/or
 *     later interrupts could be prioritized by the GIC.
 *
 *     For devices which are tightly coupled to the CPU, such as PMUs, a
 *     context synchronization event is necessary to ensure that system
 *     register state is not stale, as these may have been indirectly written
 *     *after* exception entry.
 *
 * (2) Deactivate the interrupt when EOI mode 1 is in use.
 */
static inline void gic_complete_ack(u32 irqnr)
{
	if (static_branch_likely(&supports_deactivate_key))
		write_gicreg(irqnr, ICC_EOIR1_EL1);

	isb();
}

static bool gic_rpr_is_nmi_prio(void)
{
	if (!gic_supports_nmi())
		return false;

	return unlikely(gic_read_rpr() == GICD_INT_RPR_PRI(GICD_INT_NMI_PRI));
}

static bool gic_irqnr_is_special(u32 irqnr)
{
	return irqnr >= 1020 && irqnr <= 1023;
}

static void __gic_handle_irq(u32 irqnr, struct pt_regs *regs)
{
	if (gic_irqnr_is_special(irqnr))
		return;

	gic_complete_ack(irqnr);

	if (generic_handle_domain_irq(gic_data.domain, irqnr)) {
		WARN_ONCE(true, "Unexpected interrupt (irqnr %u)\n", irqnr);
		gic_deactivate_unhandled(irqnr);
	}
}

static void __gic_handle_nmi(u32 irqnr, struct pt_regs *regs)
{
	if (gic_irqnr_is_special(irqnr))
		return;

	gic_complete_ack(irqnr);

	if (generic_handle_domain_nmi(gic_data.domain, irqnr)) {
		WARN_ONCE(true, "Unexpected pseudo-NMI (irqnr %u)\n", irqnr);
		gic_deactivate_unhandled(irqnr);
	}
}

/*
 * An exception has been taken from a context with IRQs enabled, and this could
 * be an IRQ or an NMI.
 *
 * The entry code called us with DAIF.IF set to keep NMIs masked. We must clear
 * DAIF.IF (and update ICC_PMR_EL1 to mask regular IRQs) prior to returning,
 * after handling any NMI but before handling any IRQ.
 *
 * The entry code has performed IRQ entry, and if an NMI is detected we must
 * perform NMI entry/exit around invoking the handler.
 */
static void __gic_handle_irq_from_irqson(struct pt_regs *regs)
{
	bool is_nmi;
	u32 irqnr;

	irqnr = gic_read_iar();

	is_nmi = gic_rpr_is_nmi_prio();

	if (is_nmi) {
		nmi_enter();
		__gic_handle_nmi(irqnr, regs);
		nmi_exit();
	}

	if (gic_prio_masking_enabled()) {
		gic_pmr_mask_irqs();
		gic_arch_enable_irqs();
	}

	if (!is_nmi)
		__gic_handle_irq(irqnr, regs);
}

/*
 * An exception has been taken from a context with IRQs disabled, which can only
 * be an NMI.
 *
 * The entry code called us with DAIF.IF set to keep NMIs masked. We must leave
 * DAIF.IF (and ICC_PMR_EL1) unchanged.
 *
 * The entry code has performed NMI entry.
 */
static void __gic_handle_irq_from_irqsoff(struct pt_regs *regs)
{
	u64 pmr;
	u32 irqnr;

	/*
	 * We were in a context with IRQs disabled. However, the
	 * entry code has set PMR to a value that allows any
	 * interrupt to be acknowledged, and not just NMIs. This can
	 * lead to surprising effects if the NMI has been retired in
	 * the meantime, and that there is an IRQ pending. The IRQ
	 * would then be taken in NMI context, something that nobody
	 * wants to debug twice.
	 *
	 * Until we sort this, drop PMR again to a level that will
	 * actually only allow NMIs before reading IAR, and then
	 * restore it to what it was.
	 */
	pmr = gic_read_pmr();
	gic_pmr_mask_irqs();
	isb();
	irqnr = gic_read_iar();
	gic_write_pmr(pmr);

	__gic_handle_nmi(irqnr, regs);
}

static asmlinkage void __exception_irq_entry gic_handle_irq(struct pt_regs *regs)
{
	if (unlikely(gic_supports_nmi() && !interrupts_enabled(regs)))
		__gic_handle_irq_from_irqsoff(regs);
	else
		__gic_handle_irq_from_irqson(regs);
}

static u32 gic_get_pribits(void)
{
	u32 pribits;

	pribits = gic_read_ctlr();
	pribits &= ICC_CTLR_EL1_PRI_BITS_MASK;
	pribits >>= ICC_CTLR_EL1_PRI_BITS_SHIFT;
	pribits++;

	return pribits;
}

static bool gic_has_group0(void)
{
	u32 val;
	u32 old_pmr;

	old_pmr = gic_read_pmr();

	/*
	 * Let's find out if Group0 is under control of EL3 or not by
	 * setting the highest possible, non-zero priority in PMR.
	 *
	 * If SCR_EL3.FIQ is set, the priority gets shifted down in
	 * order for the CPU interface to set bit 7, and keep the
	 * actual priority in the non-secure range. In the process, it
	 * looses the least significant bit and the actual priority
	 * becomes 0x80. Reading it back returns 0, indicating that
	 * we're don't have access to Group0.
	 */
	gic_write_pmr(BIT(8 - gic_get_pribits()));
	val = gic_read_pmr();

	gic_write_pmr(old_pmr);

	return val != 0;
}

static void __init gic_dist_init(void)
{
	unsigned int i;
	u64 affinity;
	void __iomem *base = gic_data.dist_base;
	u32 val;

	/* Disable the distributor */
	writel_relaxed(0, base + GICD_CTLR);
	gic_dist_wait_for_rwp();

	/*
	 * Configure SPIs as non-secure Group-1. This will only matter
	 * if the GIC only has a single security state. This will not
	 * do the right thing if the kernel is running in secure mode,
	 * but that's not the intended use case anyway.
	 */
	for (i = 32; i < GIC_LINE_NR; i += 32)
		writel_relaxed(~0, base + GICD_IGROUPR + i / 8);

	/* Extended SPI range, not handled by the GICv2/GICv3 common code */
	for (i = 0; i < GIC_ESPI_NR; i += 32) {
		writel_relaxed(~0U, base + GICD_ICENABLERnE + i / 8);
		writel_relaxed(~0U, base + GICD_ICACTIVERnE + i / 8);
	}

	for (i = 0; i < GIC_ESPI_NR; i += 32)
		writel_relaxed(~0U, base + GICD_IGROUPRnE + i / 8);

	for (i = 0; i < GIC_ESPI_NR; i += 16)
		writel_relaxed(0, base + GICD_ICFGRnE + i / 4);

	for (i = 0; i < GIC_ESPI_NR; i += 4)
		writel_relaxed(GICD_INT_DEF_PRI_X4, base + GICD_IPRIORITYRnE + i);

	/* Now do the common stuff */
	gic_dist_config(base, GIC_LINE_NR, NULL);

	val = GICD_CTLR_ARE_NS | GICD_CTLR_ENABLE_G1A | GICD_CTLR_ENABLE_G1;
	if (gic_data.rdists.gicd_typer2 & GICD_TYPER2_nASSGIcap) {
		pr_info("Enabling SGIs without active state\n");
		val |= GICD_CTLR_nASSGIreq;
	}

	/* Enable distributor with ARE, Group1, and wait for it to drain */
	writel_relaxed(val, base + GICD_CTLR);
	gic_dist_wait_for_rwp();

	/*
	 * Set all global interrupts to the boot CPU only. ARE must be
	 * enabled.
	 */
	affinity = gic_cpu_to_affinity(smp_processor_id());
	for (i = 32; i < GIC_LINE_NR; i++)
		gic_write_irouter(affinity, base + GICD_IROUTER + i * 8);

	for (i = 0; i < GIC_ESPI_NR; i++)
		gic_write_irouter(affinity, base + GICD_IROUTERnE + i * 8);
}

static int gic_iterate_rdists(int (*fn)(struct redist_region *, void __iomem *))
{
	int ret = -ENODEV;
	int i;

	for (i = 0; i < gic_data.nr_redist_regions; i++) {
		void __iomem *ptr = gic_data.redist_regions[i].redist_base;
		u64 typer;
		u32 reg;

		reg = readl_relaxed(ptr + GICR_PIDR2) & GIC_PIDR2_ARCH_MASK;
		if (reg != GIC_PIDR2_ARCH_GICv3 &&
		    reg != GIC_PIDR2_ARCH_GICv4) { /* We're in trouble... */
			pr_warn("No redistributor present @%p\n", ptr);
			break;
		}

		do {
			typer = gic_read_typer(ptr + GICR_TYPER);
			ret = fn(gic_data.redist_regions + i, ptr);
			if (!ret)
				return 0;

			if (gic_data.redist_regions[i].single_redist)
				break;

			if (gic_data.redist_stride) {
				ptr += gic_data.redist_stride;
			} else {
				ptr += SZ_64K * 2; /* Skip RD_base + SGI_base */
				if (typer & GICR_TYPER_VLPIS)
					ptr += SZ_64K * 2; /* Skip VLPI_base + reserved page */
			}
		} while (!(typer & GICR_TYPER_LAST));
	}

	return ret ? -ENODEV : 0;
}

static int __gic_populate_rdist(struct redist_region *region, void __iomem *ptr)
{
	unsigned long mpidr;
	u64 typer;
	u32 aff;

	/*
	 * Convert affinity to a 32bit value that can be matched to
	 * GICR_TYPER bits [63:32].
	 */
	mpidr = gic_cpu_to_affinity(smp_processor_id());

	aff = (MPIDR_AFFINITY_LEVEL(mpidr, 3) << 24 |
	       MPIDR_AFFINITY_LEVEL(mpidr, 2) << 16 |
	       MPIDR_AFFINITY_LEVEL(mpidr, 1) << 8 |
	       MPIDR_AFFINITY_LEVEL(mpidr, 0));

	typer = gic_read_typer(ptr + GICR_TYPER);
	if ((typer >> 32) == aff) {
		u64 offset = ptr - region->redist_base;
		raw_spin_lock_init(&gic_data_rdist()->rd_lock);
		gic_data_rdist_rd_base() = ptr;
		gic_data_rdist()->phys_base = region->phys_base + offset;

		pr_info("CPU%d: found redistributor %lx region %d:%pa\n",
			smp_processor_id(), mpidr,
			(int)(region - gic_data.redist_regions),
			&gic_data_rdist()->phys_base);
		return 0;
	}

	/* Try next one */
	return 1;
}

static int gic_populate_rdist(void)
{
	if (gic_iterate_rdists(__gic_populate_rdist) == 0)
		return 0;

	/* We couldn't even deal with ourselves... */
	WARN(true, "CPU%d: mpidr %lx has no re-distributor!\n",
	     smp_processor_id(),
	     (unsigned long)cpu_logical_map(smp_processor_id()));
	return -ENODEV;
}

static int __gic_update_rdist_properties(struct redist_region *region,
					 void __iomem *ptr)
{
	u64 typer = gic_read_typer(ptr + GICR_TYPER);
	u32 ctlr = readl_relaxed(ptr + GICR_CTLR);

	/* Boot-time cleanup */
	if ((typer & GICR_TYPER_VLPIS) && (typer & GICR_TYPER_RVPEID)) {
		u64 val;

		/* Deactivate any present vPE */
		val = gicr_read_vpendbaser(ptr + SZ_128K + GICR_VPENDBASER);
		if (val & GICR_VPENDBASER_Valid)
			gicr_write_vpendbaser(GICR_VPENDBASER_PendingLast,
					      ptr + SZ_128K + GICR_VPENDBASER);

		/* Mark the VPE table as invalid */
		val = gicr_read_vpropbaser(ptr + SZ_128K + GICR_VPROPBASER);
		val &= ~GICR_VPROPBASER_4_1_VALID;
		gicr_write_vpropbaser(val, ptr + SZ_128K + GICR_VPROPBASER);
	}

	gic_data.rdists.has_vlpis &= !!(typer & GICR_TYPER_VLPIS);

	/*
	 * TYPER.RVPEID implies some form of DirectLPI, no matter what the
	 * doc says... :-/ And CTLR.IR implies another subset of DirectLPI
	 * that the ITS driver can make use of for LPIs (and not VLPIs).
	 *
	 * These are 3 different ways to express the same thing, depending
	 * on the revision of the architecture and its relaxations over
	 * time. Just group them under the 'direct_lpi' banner.
	 */
	gic_data.rdists.has_rvpeid &= !!(typer & GICR_TYPER_RVPEID);
	gic_data.rdists.has_direct_lpi &= (!!(typer & GICR_TYPER_DirectLPIS) |
					   !!(ctlr & GICR_CTLR_IR) |
					   gic_data.rdists.has_rvpeid);
	gic_data.rdists.has_vpend_valid_dirty &= !!(typer & GICR_TYPER_DIRTY);

	/* Detect non-sensical configurations */
	if (WARN_ON_ONCE(gic_data.rdists.has_rvpeid && !gic_data.rdists.has_vlpis)) {
		gic_data.rdists.has_direct_lpi = false;
		gic_data.rdists.has_vlpis = false;
		gic_data.rdists.has_rvpeid = false;
	}

	gic_data.ppi_nr = min(GICR_TYPER_NR_PPIS(typer), gic_data.ppi_nr);

	return 1;
}

static void gic_update_rdist_properties(void)
{
	gic_data.ppi_nr = UINT_MAX;
	gic_iterate_rdists(__gic_update_rdist_properties);
	if (WARN_ON(gic_data.ppi_nr == UINT_MAX))
		gic_data.ppi_nr = 0;
	pr_info("GICv3 features: %d PPIs%s%s\n",
		gic_data.ppi_nr,
		gic_data.has_rss ? ", RSS" : "",
		gic_data.rdists.has_direct_lpi ? ", DirectLPI" : "");

	if (gic_data.rdists.has_vlpis)
		pr_info("GICv4 features: %s%s%s\n",
			gic_data.rdists.has_direct_lpi ? "DirectLPI " : "",
			gic_data.rdists.has_rvpeid ? "RVPEID " : "",
			gic_data.rdists.has_vpend_valid_dirty ? "Valid+Dirty " : "");
}

/* Check whether it's single security state view */
static inline bool gic_dist_security_disabled(void)
{
	return readl_relaxed(gic_data.dist_base + GICD_CTLR) & GICD_CTLR_DS;
}

static void gic_cpu_sys_reg_init(void)
{
	int i, cpu = smp_processor_id();
	u64 mpidr = gic_cpu_to_affinity(cpu);
	u64 need_rss = MPIDR_RS(mpidr);
	bool group0;
	u32 pribits;

	/*
	 * Need to check that the SRE bit has actually been set. If
	 * not, it means that SRE is disabled at EL2. We're going to
	 * die painfully, and there is nothing we can do about it.
	 *
	 * Kindly inform the luser.
	 */
	if (!gic_enable_sre())
		pr_err("GIC: unable to set SRE (disabled at EL2), panic ahead\n");

	pribits = gic_get_pribits();

	group0 = gic_has_group0();

	/* Set priority mask register */
	if (!gic_prio_masking_enabled()) {
		write_gicreg(DEFAULT_PMR_VALUE, ICC_PMR_EL1);
	} else if (gic_supports_nmi()) {
		/*
		 * Mismatch configuration with boot CPU, the system is likely
		 * to die as interrupt masking will not work properly on all
		 * CPUs
		 *
		 * The boot CPU calls this function before enabling NMI support,
		 * and as a result we'll never see this warning in the boot path
		 * for that CPU.
		 */
		if (static_branch_unlikely(&gic_nonsecure_priorities))
			WARN_ON(!group0 || gic_dist_security_disabled());
		else
			WARN_ON(group0 && !gic_dist_security_disabled());
	}

	/*
	 * Some firmwares hand over to the kernel with the BPR changed from
	 * its reset value (and with a value large enough to prevent
	 * any pre-emptive interrupts from working at all). Writing a zero
	 * to BPR restores is reset value.
	 */
	gic_write_bpr1(0);

	if (static_branch_likely(&supports_deactivate_key)) {
		/* EOI drops priority only (mode 1) */
		gic_write_ctlr(ICC_CTLR_EL1_EOImode_drop);
	} else {
		/* EOI deactivates interrupt too (mode 0) */
		gic_write_ctlr(ICC_CTLR_EL1_EOImode_drop_dir);
	}

	/* Always whack Group0 before Group1 */
	if (group0) {
		switch(pribits) {
		case 8:
		case 7:
			write_gicreg(0, ICC_AP0R3_EL1);
			write_gicreg(0, ICC_AP0R2_EL1);
			fallthrough;
		case 6:
			write_gicreg(0, ICC_AP0R1_EL1);
			fallthrough;
		case 5:
		case 4:
			write_gicreg(0, ICC_AP0R0_EL1);
		}

		isb();
	}

	switch(pribits) {
	case 8:
	case 7:
		write_gicreg(0, ICC_AP1R3_EL1);
		write_gicreg(0, ICC_AP1R2_EL1);
		fallthrough;
	case 6:
		write_gicreg(0, ICC_AP1R1_EL1);
		fallthrough;
	case 5:
	case 4:
		write_gicreg(0, ICC_AP1R0_EL1);
	}

	isb();

	/* ... and let's hit the road... */
	gic_write_grpen1(1);

	/* Keep the RSS capability status in per_cpu variable */
	per_cpu(has_rss, cpu) = !!(gic_read_ctlr() & ICC_CTLR_EL1_RSS);

	/* Check all the CPUs have capable of sending SGIs to other CPUs */
	for_each_online_cpu(i) {
		bool have_rss = per_cpu(has_rss, i) && per_cpu(has_rss, cpu);

		need_rss |= MPIDR_RS(gic_cpu_to_affinity(i));
		if (need_rss && (!have_rss))
			pr_crit("CPU%d (%lx) can't SGI CPU%d (%lx), no RSS\n",
				cpu, (unsigned long)mpidr,
				i, (unsigned long)gic_cpu_to_affinity(i));
	}

	/**
	 * GIC spec says, when ICC_CTLR_EL1.RSS==1 and GICD_TYPER.RSS==0,
	 * writing ICC_ASGI1R_EL1 register with RS != 0 is a CONSTRAINED
	 * UNPREDICTABLE choice of :
	 *   - The write is ignored.
	 *   - The RS field is treated as 0.
	 */
	if (need_rss && (!gic_data.has_rss))
		pr_crit_once("RSS is required but GICD doesn't support it\n");
}

static bool gicv3_nolpi;

static int __init gicv3_nolpi_cfg(char *buf)
{
	return kstrtobool(buf, &gicv3_nolpi);
}
early_param("irqchip.gicv3_nolpi", gicv3_nolpi_cfg);

static int gic_dist_supports_lpis(void)
{
	return (IS_ENABLED(CONFIG_ARM_GIC_V3_ITS) &&
		!!(readl_relaxed(gic_data.dist_base + GICD_TYPER) & GICD_TYPER_LPIS) &&
		!gicv3_nolpi);
}

static void gic_cpu_init(void)
{
	void __iomem *rbase;
	int i;

	/* Register ourselves with the rest of the world */
	if (gic_populate_rdist())
		return;

	gic_enable_redist(true);

	WARN((gic_data.ppi_nr > 16 || GIC_ESPI_NR != 0) &&
	     !(gic_read_ctlr() & ICC_CTLR_EL1_ExtRange),
	     "Distributor has extended ranges, but CPU%d doesn't\n",
	     smp_processor_id());

	rbase = gic_data_rdist_sgi_base();

	/* Configure SGIs/PPIs as non-secure Group-1 */
	for (i = 0; i < gic_data.ppi_nr + 16; i += 32)
		writel_relaxed(~0, rbase + GICR_IGROUPR0 + i / 8);

	gic_cpu_config(rbase, gic_data.ppi_nr + 16, gic_redist_wait_for_rwp);

	/* initialise system registers */
	gic_cpu_sys_reg_init();
}

#ifdef CONFIG_SMP

#define MPIDR_TO_SGI_RS(mpidr)	(MPIDR_RS(mpidr) << ICC_SGI1R_RS_SHIFT)
#define MPIDR_TO_SGI_CLUSTER_ID(mpidr)	((mpidr) & ~0xFUL)

static int gic_starting_cpu(unsigned int cpu)
{
	gic_cpu_init();

	if (gic_dist_supports_lpis())
		its_cpu_init();

	return 0;
}

static u16 gic_compute_target_list(int *base_cpu, const struct cpumask *mask,
				   unsigned long cluster_id)
{
	int next_cpu, cpu = *base_cpu;
	unsigned long mpidr;
	u16 tlist = 0;

	mpidr = gic_cpu_to_affinity(cpu);

	while (cpu < nr_cpu_ids) {
		tlist |= 1 << (mpidr & 0xf);

		next_cpu = cpumask_next(cpu, mask);
		if (next_cpu >= nr_cpu_ids)
			goto out;
		cpu = next_cpu;

		mpidr = gic_cpu_to_affinity(cpu);

		if (cluster_id != MPIDR_TO_SGI_CLUSTER_ID(mpidr)) {
			cpu--;
			goto out;
		}
	}
out:
	*base_cpu = cpu;
	return tlist;
}

#define MPIDR_TO_SGI_AFFINITY(cluster_id, level) \
	(MPIDR_AFFINITY_LEVEL(cluster_id, level) \
		<< ICC_SGI1R_AFFINITY_## level ##_SHIFT)

static void gic_send_sgi(u64 cluster_id, u16 tlist, unsigned int irq)
{
	u64 val;

	val = (MPIDR_TO_SGI_AFFINITY(cluster_id, 3)	|
	       MPIDR_TO_SGI_AFFINITY(cluster_id, 2)	|
	       irq << ICC_SGI1R_SGI_ID_SHIFT		|
	       MPIDR_TO_SGI_AFFINITY(cluster_id, 1)	|
	       MPIDR_TO_SGI_RS(cluster_id)		|
	       tlist << ICC_SGI1R_TARGET_LIST_SHIFT);

	pr_devel("CPU%d: ICC_SGI1R_EL1 %llx\n", smp_processor_id(), val);
	gic_write_sgi1r(val);
}

static void gic_ipi_send_mask(struct irq_data *d, const struct cpumask *mask)
{
	int cpu;

	if (WARN_ON(d->hwirq >= 16))
		return;

	/*
	 * Ensure that stores to Normal memory are visible to the
	 * other CPUs before issuing the IPI.
	 */
	dsb(ishst);

	for_each_cpu(cpu, mask) {
		u64 cluster_id = MPIDR_TO_SGI_CLUSTER_ID(gic_cpu_to_affinity(cpu));
		u16 tlist;

		tlist = gic_compute_target_list(&cpu, mask, cluster_id);
		gic_send_sgi(cluster_id, tlist, d->hwirq);
	}

	/* Force the above writes to ICC_SGI1R_EL1 to be executed */
	isb();
}

static void __init gic_smp_init(void)
{
	struct irq_fwspec sgi_fwspec = {
		.fwnode		= gic_data.fwnode,
		.param_count	= 1,
	};
	int base_sgi;

	cpuhp_setup_state_nocalls(CPUHP_AP_IRQ_GIC_STARTING,
				  "irqchip/arm/gicv3:starting",
				  gic_starting_cpu, NULL);

	/* Register all 8 non-secure SGIs */
	base_sgi = irq_domain_alloc_irqs(gic_data.domain, 8, NUMA_NO_NODE, &sgi_fwspec);
	if (WARN_ON(base_sgi <= 0))
		return;

	set_smp_ipi_range(base_sgi, 8);
}

static int gic_set_affinity(struct irq_data *d, const struct cpumask *mask_val,
			    bool force)
{
	unsigned int cpu;
	u32 offset, index;
	void __iomem *reg;
	int enabled;
	u64 val;

	if (force)
		cpu = cpumask_first(mask_val);
	else
		cpu = cpumask_any_and(mask_val, cpu_online_mask);

	if (cpu >= nr_cpu_ids)
		return -EINVAL;

	if (gic_irq_in_rdist(d))
		return -EINVAL;

	/* If interrupt was enabled, disable it first */
	enabled = gic_peek_irq(d, GICD_ISENABLER);
	if (enabled)
		gic_mask_irq(d);

	offset = convert_offset_index(d, GICD_IROUTER, &index);
	reg = gic_dist_base(d) + offset + (index * 8);
	val = gic_cpu_to_affinity(cpu);

	gic_write_irouter(val, reg);

	/*
	 * If the interrupt was enabled, enabled it again. Otherwise,
	 * just wait for the distributor to have digested our changes.
	 */
	if (enabled)
		gic_unmask_irq(d);

	irq_data_update_effective_affinity(d, cpumask_of(cpu));

	return IRQ_SET_MASK_OK_DONE;
}
#else
#define gic_set_affinity	NULL
#define gic_ipi_send_mask	NULL
#define gic_smp_init()		do { } while(0)
#endif

static int gic_retrigger(struct irq_data *data)
{
	return !gic_irq_set_irqchip_state(data, IRQCHIP_STATE_PENDING, true);
}

#ifdef CONFIG_CPU_PM
static int gic_cpu_pm_notifier(struct notifier_block *self,
			       unsigned long cmd, void *v)
{
	if (cmd == CPU_PM_EXIT) {
		if (gic_dist_security_disabled())
			gic_enable_redist(true);
		gic_cpu_sys_reg_init();
	} else if (cmd == CPU_PM_ENTER && gic_dist_security_disabled()) {
		gic_write_grpen1(0);
		gic_enable_redist(false);
	}
	return NOTIFY_OK;
}

static struct notifier_block gic_cpu_pm_notifier_block = {
	.notifier_call = gic_cpu_pm_notifier,
};

static void gic_cpu_pm_init(void)
{
	cpu_pm_register_notifier(&gic_cpu_pm_notifier_block);
}

#else
static inline void gic_cpu_pm_init(void) { }
#endif /* CONFIG_CPU_PM */

static struct irq_chip gic_chip = {
	.name			= "GICv3",
	.irq_mask		= gic_mask_irq,
	.irq_unmask		= gic_unmask_irq,
	.irq_eoi		= gic_eoi_irq,
	.irq_set_type		= gic_set_type,
	.irq_set_affinity	= gic_set_affinity,
	.irq_retrigger          = gic_retrigger,
	.irq_get_irqchip_state	= gic_irq_get_irqchip_state,
	.irq_set_irqchip_state	= gic_irq_set_irqchip_state,
	.irq_nmi_setup		= gic_irq_nmi_setup,
	.irq_nmi_teardown	= gic_irq_nmi_teardown,
	.ipi_send_mask		= gic_ipi_send_mask,
	.flags			= IRQCHIP_SET_TYPE_MASKED |
				  IRQCHIP_SKIP_SET_WAKE |
				  IRQCHIP_MASK_ON_SUSPEND,
};

static struct irq_chip gic_eoimode1_chip = {
	.name			= "GICv3",
	.irq_mask		= gic_eoimode1_mask_irq,
	.irq_unmask		= gic_unmask_irq,
	.irq_eoi		= gic_eoimode1_eoi_irq,
	.irq_set_type		= gic_set_type,
	.irq_set_affinity	= gic_set_affinity,
	.irq_retrigger          = gic_retrigger,
	.irq_get_irqchip_state	= gic_irq_get_irqchip_state,
	.irq_set_irqchip_state	= gic_irq_set_irqchip_state,
	.irq_set_vcpu_affinity	= gic_irq_set_vcpu_affinity,
	.irq_nmi_setup		= gic_irq_nmi_setup,
	.irq_nmi_teardown	= gic_irq_nmi_teardown,
	.ipi_send_mask		= gic_ipi_send_mask,
	.flags			= IRQCHIP_SET_TYPE_MASKED |
				  IRQCHIP_SKIP_SET_WAKE |
				  IRQCHIP_MASK_ON_SUSPEND,
};

static int gic_irq_domain_map(struct irq_domain *d, unsigned int irq,
			      irq_hw_number_t hw)
{
	struct irq_chip *chip = &gic_chip;
	struct irq_data *irqd = irq_desc_get_irq_data(irq_to_desc(irq));

	if (static_branch_likely(&supports_deactivate_key))
		chip = &gic_eoimode1_chip;

	switch (__get_intid_range(hw)) {
	case SGI_RANGE:
	case PPI_RANGE:
	case EPPI_RANGE:
		irq_set_percpu_devid(irq);
		irq_domain_set_info(d, irq, hw, chip, d->host_data,
				    handle_percpu_devid_irq, NULL, NULL);
		break;

	case SPI_RANGE:
	case ESPI_RANGE:
		irq_domain_set_info(d, irq, hw, chip, d->host_data,
				    handle_fasteoi_irq, NULL, NULL);
		irq_set_probe(irq);
		irqd_set_single_target(irqd);
		break;

	case LPI_RANGE:
		if (!gic_dist_supports_lpis())
			return -EPERM;
		irq_domain_set_info(d, irq, hw, chip, d->host_data,
				    handle_fasteoi_irq, NULL, NULL);
		break;

	default:
		return -EPERM;
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
		case 2:			/* ESPI */
			*hwirq = fwspec->param[1] + ESPI_BASE_INTID;
			break;
		case 3:			/* EPPI */
			*hwirq = fwspec->param[1] + EPPI_BASE_INTID;
			break;
		case GIC_IRQ_TYPE_LPI:	/* LPI */
			*hwirq = fwspec->param[1];
			break;
		case GIC_IRQ_TYPE_PARTITION:
			*hwirq = fwspec->param[1];
			if (fwspec->param[1] >= 16)
				*hwirq += EPPI_BASE_INTID - 16;
			else
				*hwirq += 16;
			break;
		default:
			return -EINVAL;
		}

		*type = fwspec->param[2] & IRQ_TYPE_SENSE_MASK;

		/*
		 * Make it clear that broken DTs are... broken.
		 * Partitioned PPIs are an unfortunate exception.
		 */
		WARN_ON(*type == IRQ_TYPE_NONE &&
			fwspec->param[0] != GIC_IRQ_TYPE_PARTITION);
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

		WARN_ON(*type == IRQ_TYPE_NONE);
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

static void gic_irq_domain_free(struct irq_domain *domain, unsigned int virq,
				unsigned int nr_irqs)
{
	int i;

	for (i = 0; i < nr_irqs; i++) {
		struct irq_data *d = irq_domain_get_irq_data(domain, virq + i);
		irq_set_handler(virq + i, NULL);
		irq_domain_reset_irq_data(d);
	}
}

static bool fwspec_is_partitioned_ppi(struct irq_fwspec *fwspec,
				      irq_hw_number_t hwirq)
{
	enum gic_intid_range range;

	if (!gic_data.ppi_descs)
		return false;

	if (!is_of_node(fwspec->fwnode))
		return false;

	if (fwspec->param_count < 4 || !fwspec->param[3])
		return false;

	range = __get_intid_range(hwirq);
	if (range != PPI_RANGE && range != EPPI_RANGE)
		return false;

	return true;
}

static int gic_irq_domain_select(struct irq_domain *d,
				 struct irq_fwspec *fwspec,
				 enum irq_domain_bus_token bus_token)
{
	unsigned int type, ret, ppi_idx;
	irq_hw_number_t hwirq;

	/* Not for us */
        if (fwspec->fwnode != d->fwnode)
		return 0;

	/* If this is not DT, then we have a single domain */
	if (!is_of_node(fwspec->fwnode))
		return 1;

	ret = gic_irq_domain_translate(d, fwspec, &hwirq, &type);
	if (WARN_ON_ONCE(ret))
		return 0;

	if (!fwspec_is_partitioned_ppi(fwspec, hwirq))
		return d == gic_data.domain;

	/*
	 * If this is a PPI and we have a 4th (non-null) parameter,
	 * then we need to match the partition domain.
	 */
	ppi_idx = __gic_get_ppi_index(hwirq);
	return d == partition_get_domain(gic_data.ppi_descs[ppi_idx]);
}

static const struct irq_domain_ops gic_irq_domain_ops = {
	.translate = gic_irq_domain_translate,
	.alloc = gic_irq_domain_alloc,
	.free = gic_irq_domain_free,
	.select = gic_irq_domain_select,
};

static int partition_domain_translate(struct irq_domain *d,
				      struct irq_fwspec *fwspec,
				      unsigned long *hwirq,
				      unsigned int *type)
{
	unsigned long ppi_intid;
	struct device_node *np;
	unsigned int ppi_idx;
	int ret;

	if (!gic_data.ppi_descs)
		return -ENOMEM;

	np = of_find_node_by_phandle(fwspec->param[3]);
	if (WARN_ON(!np))
		return -EINVAL;

	ret = gic_irq_domain_translate(d, fwspec, &ppi_intid, type);
	if (WARN_ON_ONCE(ret))
		return 0;

	ppi_idx = __gic_get_ppi_index(ppi_intid);
	ret = partition_translate_id(gic_data.ppi_descs[ppi_idx],
				     of_node_to_fwnode(np));
	if (ret < 0)
		return ret;

	*hwirq = ret;
	*type = fwspec->param[2] & IRQ_TYPE_SENSE_MASK;

	return 0;
}

static const struct irq_domain_ops partition_domain_ops = {
	.translate = partition_domain_translate,
	.select = gic_irq_domain_select,
};

static bool gic_enable_quirk_msm8996(void *data)
{
	struct gic_chip_data *d = data;

	d->flags |= FLAGS_WORKAROUND_GICR_WAKER_MSM8996;

	return true;
}

static bool gic_enable_quirk_mtk_gicr(void *data)
{
	struct gic_chip_data *d = data;

	d->flags |= FLAGS_WORKAROUND_MTK_GICR_SAVE;

	return true;
}

static bool gic_enable_quirk_cavium_38539(void *data)
{
	struct gic_chip_data *d = data;

	d->flags |= FLAGS_WORKAROUND_CAVIUM_ERRATUM_38539;

	return true;
}

static bool gic_enable_quirk_hip06_07(void *data)
{
	struct gic_chip_data *d = data;

	/*
	 * HIP06 GICD_IIDR clashes with GIC-600 product number (despite
	 * not being an actual ARM implementation). The saving grace is
	 * that GIC-600 doesn't have ESPI, so nothing to do in that case.
	 * HIP07 doesn't even have a proper IIDR, and still pretends to
	 * have ESPI. In both cases, put them right.
	 */
	if (d->rdists.gicd_typer & GICD_TYPER_ESPI) {
		/* Zero both ESPI and the RES0 field next to it... */
		d->rdists.gicd_typer &= ~GENMASK(9, 8);
		return true;
	}

	return false;
}

#define T241_CHIPN_MASK		GENMASK_ULL(45, 44)
#define T241_CHIP_GICDA_OFFSET	0x1580000
#define SMCCC_SOC_ID_T241	0x036b0241

static bool gic_enable_quirk_nvidia_t241(void *data)
{
	s32 soc_id = arm_smccc_get_soc_id_version();
	unsigned long chip_bmask = 0;
	phys_addr_t phys;
	u32 i;

	/* Check JEP106 code for NVIDIA T241 chip (036b:0241) */
	if ((soc_id < 0) || (soc_id != SMCCC_SOC_ID_T241))
		return false;

	/* Find the chips based on GICR regions PHYS addr */
	for (i = 0; i < gic_data.nr_redist_regions; i++) {
		chip_bmask |= BIT(FIELD_GET(T241_CHIPN_MASK,
				  (u64)gic_data.redist_regions[i].phys_base));
	}

	if (hweight32(chip_bmask) < 3)
		return false;

	/* Setup GICD alias regions */
	for (i = 0; i < ARRAY_SIZE(t241_dist_base_alias); i++) {
		if (chip_bmask & BIT(i)) {
			phys = gic_data.dist_phys_base + T241_CHIP_GICDA_OFFSET;
			phys |= FIELD_PREP(T241_CHIPN_MASK, i);
			t241_dist_base_alias[i] = ioremap(phys, SZ_64K);
			WARN_ON_ONCE(!t241_dist_base_alias[i]);
		}
	}
	static_branch_enable(&gic_nvidia_t241_erratum);
	return true;
}

static bool gic_enable_quirk_asr8601(void *data)
{
	struct gic_chip_data *d = data;

	d->flags |= FLAGS_WORKAROUND_ASR_ERRATUM_8601001;

	return true;
}

static bool gic_enable_quirk_arm64_2941627(void *data)
{
	static_branch_enable(&gic_arm64_2941627_erratum);
	return true;
}

static bool rd_set_non_coherent(void *data)
{
	struct gic_chip_data *d = data;

	d->rdists.flags |= RDIST_FLAGS_FORCE_NON_SHAREABLE;
	return true;
}

static const struct gic_quirk gic_quirks[] = {
	{
		.desc	= "GICv3: Qualcomm MSM8996 broken firmware",
		.compatible = "qcom,msm8996-gic-v3",
		.init	= gic_enable_quirk_msm8996,
	},
	{
		.desc	= "GICv3: ASR erratum 8601001",
		.compatible = "asr,asr8601-gic-v3",
		.init	= gic_enable_quirk_asr8601,
	},
	{
		.desc	= "GICv3: Mediatek Chromebook GICR save problem",
		.property = "mediatek,broken-save-restore-fw",
		.init	= gic_enable_quirk_mtk_gicr,
	},
	{
		.desc	= "GICv3: HIP06 erratum 161010803",
		.iidr	= 0x0204043b,
		.mask	= 0xffffffff,
		.init	= gic_enable_quirk_hip06_07,
	},
	{
		.desc	= "GICv3: HIP07 erratum 161010803",
		.iidr	= 0x00000000,
		.mask	= 0xffffffff,
		.init	= gic_enable_quirk_hip06_07,
	},
	{
		/*
		 * Reserved register accesses generate a Synchronous
		 * External Abort. This erratum applies to:
		 * - ThunderX: CN88xx
		 * - OCTEON TX: CN83xx, CN81xx
		 * - OCTEON TX2: CN93xx, CN96xx, CN98xx, CNF95xx*
		 */
		.desc	= "GICv3: Cavium erratum 38539",
		.iidr	= 0xa000034c,
		.mask	= 0xe8f00fff,
		.init	= gic_enable_quirk_cavium_38539,
	},
	{
		.desc	= "GICv3: NVIDIA erratum T241-FABRIC-4",
		.iidr	= 0x0402043b,
		.mask	= 0xffffffff,
		.init	= gic_enable_quirk_nvidia_t241,
	},
	{
		/*
		 * GIC-700: 2941627 workaround - IP variant [0,1]
		 *
		 */
		.desc	= "GICv3: ARM64 erratum 2941627",
		.iidr	= 0x0400043b,
		.mask	= 0xff0e0fff,
		.init	= gic_enable_quirk_arm64_2941627,
	},
	{
		/*
		 * GIC-700: 2941627 workaround - IP variant [2]
		 */
		.desc	= "GICv3: ARM64 erratum 2941627",
		.iidr	= 0x0402043b,
		.mask	= 0xff0f0fff,
		.init	= gic_enable_quirk_arm64_2941627,
	},
	{
		.desc   = "GICv3: non-coherent attribute",
		.property = "dma-noncoherent",
		.init   = rd_set_non_coherent,
	},
	{
	}
};

static void gic_enable_nmi_support(void)
{
	int i;

	if (!gic_prio_masking_enabled())
		return;

	if (gic_data.flags & FLAGS_WORKAROUND_MTK_GICR_SAVE) {
		pr_warn("Skipping NMI enable due to firmware issues\n");
		return;
	}

	ppi_nmi_refs = kcalloc(gic_data.ppi_nr, sizeof(*ppi_nmi_refs), GFP_KERNEL);
	if (!ppi_nmi_refs)
		return;

	for (i = 0; i < gic_data.ppi_nr; i++)
		refcount_set(&ppi_nmi_refs[i], 0);

	pr_info("Pseudo-NMIs enabled using %s ICC_PMR_EL1 synchronisation\n",
		gic_has_relaxed_pmr_sync() ? "relaxed" : "forced");

	/*
	 * How priority values are used by the GIC depends on two things:
	 * the security state of the GIC (controlled by the GICD_CTRL.DS bit)
	 * and if Group 0 interrupts can be delivered to Linux in the non-secure
	 * world as FIQs (controlled by the SCR_EL3.FIQ bit). These affect the
	 * ICC_PMR_EL1 register and the priority that software assigns to
	 * interrupts:
	 *
	 * GICD_CTRL.DS | SCR_EL3.FIQ | ICC_PMR_EL1 | Group 1 priority
	 * -----------------------------------------------------------
	 *      1       |      -      |  unchanged  |    unchanged
	 * -----------------------------------------------------------
	 *      0       |      1      |  non-secure |    non-secure
	 * -----------------------------------------------------------
	 *      0       |      0      |  unchanged  |    non-secure
	 *
	 * where non-secure means that the value is right-shifted by one and the
	 * MSB bit set, to make it fit in the non-secure priority range.
	 *
	 * In the first two cases, where ICC_PMR_EL1 and the interrupt priority
	 * are both either modified or unchanged, we can use the same set of
	 * priorities.
	 *
	 * In the last case, where only the interrupt priorities are modified to
	 * be in the non-secure range, we use a different PMR value to mask IRQs
	 * and the rest of the values that we use remain unchanged.
	 */
	if (gic_has_group0() && !gic_dist_security_disabled())
		static_branch_enable(&gic_nonsecure_priorities);

	static_branch_enable(&supports_pseudo_nmis);

	if (static_branch_likely(&supports_deactivate_key))
		gic_eoimode1_chip.flags |= IRQCHIP_SUPPORTS_NMI;
	else
		gic_chip.flags |= IRQCHIP_SUPPORTS_NMI;
}

static int __init gic_init_bases(phys_addr_t dist_phys_base,
				 void __iomem *dist_base,
				 struct redist_region *rdist_regs,
				 u32 nr_redist_regions,
				 u64 redist_stride,
				 struct fwnode_handle *handle)
{
	u32 typer;
	int err;

	if (!is_hyp_mode_available())
		static_branch_disable(&supports_deactivate_key);

	if (static_branch_likely(&supports_deactivate_key))
		pr_info("GIC: Using split EOI/Deactivate mode\n");

	gic_data.fwnode = handle;
	gic_data.dist_phys_base = dist_phys_base;
	gic_data.dist_base = dist_base;
	gic_data.redist_regions = rdist_regs;
	gic_data.nr_redist_regions = nr_redist_regions;
	gic_data.redist_stride = redist_stride;

	/*
	 * Find out how many interrupts are supported.
	 */
	typer = readl_relaxed(gic_data.dist_base + GICD_TYPER);
	gic_data.rdists.gicd_typer = typer;

	gic_enable_quirks(readl_relaxed(gic_data.dist_base + GICD_IIDR),
			  gic_quirks, &gic_data);

	pr_info("%d SPIs implemented\n", GIC_LINE_NR - 32);
	pr_info("%d Extended SPIs implemented\n", GIC_ESPI_NR);

	/*
	 * ThunderX1 explodes on reading GICD_TYPER2, in violation of the
	 * architecture spec (which says that reserved registers are RES0).
	 */
	if (!(gic_data.flags & FLAGS_WORKAROUND_CAVIUM_ERRATUM_38539))
		gic_data.rdists.gicd_typer2 = readl_relaxed(gic_data.dist_base + GICD_TYPER2);

	gic_data.domain = irq_domain_create_tree(handle, &gic_irq_domain_ops,
						 &gic_data);
	gic_data.rdists.rdist = alloc_percpu(typeof(*gic_data.rdists.rdist));
	if (!static_branch_unlikely(&gic_nvidia_t241_erratum)) {
		/* Disable GICv4.x features for the erratum T241-FABRIC-4 */
		gic_data.rdists.has_rvpeid = true;
		gic_data.rdists.has_vlpis = true;
		gic_data.rdists.has_direct_lpi = true;
		gic_data.rdists.has_vpend_valid_dirty = true;
	}

	if (WARN_ON(!gic_data.domain) || WARN_ON(!gic_data.rdists.rdist)) {
		err = -ENOMEM;
		goto out_free;
	}

	irq_domain_update_bus_token(gic_data.domain, DOMAIN_BUS_WIRED);

	gic_data.has_rss = !!(typer & GICD_TYPER_RSS);

	if (typer & GICD_TYPER_MBIS) {
		err = mbi_init(handle, gic_data.domain);
		if (err)
			pr_err("Failed to initialize MBIs\n");
	}

	set_handle_irq(gic_handle_irq);

	gic_update_rdist_properties();

	gic_dist_init();
	gic_cpu_init();
	gic_smp_init();
	gic_cpu_pm_init();

	if (gic_dist_supports_lpis()) {
		its_init(handle, &gic_data.rdists, gic_data.domain);
		its_cpu_init();
		its_lpi_memreserve_init();
	} else {
		if (IS_ENABLED(CONFIG_ARM_GIC_V2M))
			gicv2m_init(handle, gic_data.domain);
	}

	gic_enable_nmi_support();

	return 0;

out_free:
	if (gic_data.domain)
		irq_domain_remove(gic_data.domain);
	free_percpu(gic_data.rdists.rdist);
	return err;
}

static int __init gic_validate_dist_version(void __iomem *dist_base)
{
	u32 reg = readl_relaxed(dist_base + GICD_PIDR2) & GIC_PIDR2_ARCH_MASK;

	if (reg != GIC_PIDR2_ARCH_GICv3 && reg != GIC_PIDR2_ARCH_GICv4)
		return -ENODEV;

	return 0;
}

/* Create all possible partitions at boot time */
static void __init gic_populate_ppi_partitions(struct device_node *gic_node)
{
	struct device_node *parts_node, *child_part;
	int part_idx = 0, i;
	int nr_parts;
	struct partition_affinity *parts;

	parts_node = of_get_child_by_name(gic_node, "ppi-partitions");
	if (!parts_node)
		return;

	gic_data.ppi_descs = kcalloc(gic_data.ppi_nr, sizeof(*gic_data.ppi_descs), GFP_KERNEL);
	if (!gic_data.ppi_descs)
		goto out_put_node;

	nr_parts = of_get_child_count(parts_node);

	if (!nr_parts)
		goto out_put_node;

	parts = kcalloc(nr_parts, sizeof(*parts), GFP_KERNEL);
	if (WARN_ON(!parts))
		goto out_put_node;

	for_each_child_of_node(parts_node, child_part) {
		struct partition_affinity *part;
		int n;

		part = &parts[part_idx];

		part->partition_id = of_node_to_fwnode(child_part);

		pr_info("GIC: PPI partition %pOFn[%d] { ",
			child_part, part_idx);

		n = of_property_count_elems_of_size(child_part, "affinity",
						    sizeof(u32));
		WARN_ON(n <= 0);

		for (i = 0; i < n; i++) {
			int err, cpu;
			u32 cpu_phandle;
			struct device_node *cpu_node;

			err = of_property_read_u32_index(child_part, "affinity",
							 i, &cpu_phandle);
			if (WARN_ON(err))
				continue;

			cpu_node = of_find_node_by_phandle(cpu_phandle);
			if (WARN_ON(!cpu_node))
				continue;

			cpu = of_cpu_node_to_id(cpu_node);
			if (WARN_ON(cpu < 0)) {
				of_node_put(cpu_node);
				continue;
			}

			pr_cont("%pOF[%d] ", cpu_node, cpu);

			cpumask_set_cpu(cpu, &part->mask);
			of_node_put(cpu_node);
		}

		pr_cont("}\n");
		part_idx++;
	}

	for (i = 0; i < gic_data.ppi_nr; i++) {
		unsigned int irq;
		struct partition_desc *desc;
		struct irq_fwspec ppi_fwspec = {
			.fwnode		= gic_data.fwnode,
			.param_count	= 3,
			.param		= {
				[0]	= GIC_IRQ_TYPE_PARTITION,
				[1]	= i,
				[2]	= IRQ_TYPE_NONE,
			},
		};

		irq = irq_create_fwspec_mapping(&ppi_fwspec);
		if (WARN_ON(!irq))
			continue;
		desc = partition_create_desc(gic_data.fwnode, parts, nr_parts,
					     irq, &partition_domain_ops);
		if (WARN_ON(!desc))
			continue;

		gic_data.ppi_descs[i] = desc;
	}

out_put_node:
	of_node_put(parts_node);
}

static void __init gic_of_setup_kvm_info(struct device_node *node)
{
	int ret;
	struct resource r;
	u32 gicv_idx;

	gic_v3_kvm_info.type = GIC_V3;

	gic_v3_kvm_info.maint_irq = irq_of_parse_and_map(node, 0);
	if (!gic_v3_kvm_info.maint_irq)
		return;

	if (of_property_read_u32(node, "#redistributor-regions",
				 &gicv_idx))
		gicv_idx = 1;

	gicv_idx += 3;	/* Also skip GICD, GICC, GICH */
	ret = of_address_to_resource(node, gicv_idx, &r);
	if (!ret)
		gic_v3_kvm_info.vcpu = r;

	gic_v3_kvm_info.has_v4 = gic_data.rdists.has_vlpis;
	gic_v3_kvm_info.has_v4_1 = gic_data.rdists.has_rvpeid;
	vgic_set_kvm_info(&gic_v3_kvm_info);
}

static void gic_request_region(resource_size_t base, resource_size_t size,
			       const char *name)
{
	if (!request_mem_region(base, size, name))
		pr_warn_once(FW_BUG "%s region %pa has overlapping address\n",
			     name, &base);
}

static void __iomem *gic_of_iomap(struct device_node *node, int idx,
				  const char *name, struct resource *res)
{
	void __iomem *base;
	int ret;

	ret = of_address_to_resource(node, idx, res);
	if (ret)
		return IOMEM_ERR_PTR(ret);

	gic_request_region(res->start, resource_size(res), name);
	base = of_iomap(node, idx);

	return base ?: IOMEM_ERR_PTR(-ENOMEM);
}

static int __init gic_of_init(struct device_node *node, struct device_node *parent)
{
	phys_addr_t dist_phys_base;
	void __iomem *dist_base;
	struct redist_region *rdist_regs;
	struct resource res;
	u64 redist_stride;
	u32 nr_redist_regions;
	int err, i;

	dist_base = gic_of_iomap(node, 0, "GICD", &res);
	if (IS_ERR(dist_base)) {
		pr_err("%pOF: unable to map gic dist registers\n", node);
		return PTR_ERR(dist_base);
	}

	dist_phys_base = res.start;

	err = gic_validate_dist_version(dist_base);
	if (err) {
		pr_err("%pOF: no distributor detected, giving up\n", node);
		goto out_unmap_dist;
	}

	if (of_property_read_u32(node, "#redistributor-regions", &nr_redist_regions))
		nr_redist_regions = 1;

	rdist_regs = kcalloc(nr_redist_regions, sizeof(*rdist_regs),
			     GFP_KERNEL);
	if (!rdist_regs) {
		err = -ENOMEM;
		goto out_unmap_dist;
	}

	for (i = 0; i < nr_redist_regions; i++) {
		rdist_regs[i].redist_base = gic_of_iomap(node, 1 + i, "GICR", &res);
		if (IS_ERR(rdist_regs[i].redist_base)) {
			pr_err("%pOF: couldn't map region %d\n", node, i);
			err = -ENODEV;
			goto out_unmap_rdist;
		}
		rdist_regs[i].phys_base = res.start;
	}

	if (of_property_read_u64(node, "redistributor-stride", &redist_stride))
		redist_stride = 0;

	gic_enable_of_quirks(node, gic_quirks, &gic_data);

	err = gic_init_bases(dist_phys_base, dist_base, rdist_regs,
			     nr_redist_regions, redist_stride, &node->fwnode);
	if (err)
		goto out_unmap_rdist;

	gic_populate_ppi_partitions(node);

	if (static_branch_likely(&supports_deactivate_key))
		gic_of_setup_kvm_info(node);
	return 0;

out_unmap_rdist:
	for (i = 0; i < nr_redist_regions; i++)
		if (rdist_regs[i].redist_base && !IS_ERR(rdist_regs[i].redist_base))
			iounmap(rdist_regs[i].redist_base);
	kfree(rdist_regs);
out_unmap_dist:
	iounmap(dist_base);
	return err;
}

IRQCHIP_DECLARE(gic_v3, "arm,gic-v3", gic_of_init);

#ifdef CONFIG_ACPI
static struct
{
	void __iomem *dist_base;
	struct redist_region *redist_regs;
	u32 nr_redist_regions;
	bool single_redist;
	int enabled_rdists;
	u32 maint_irq;
	int maint_irq_mode;
	phys_addr_t vcpu_base;
} acpi_data __initdata;

static void __init
gic_acpi_register_redist(phys_addr_t phys_base, void __iomem *redist_base)
{
	static int count = 0;

	acpi_data.redist_regs[count].phys_base = phys_base;
	acpi_data.redist_regs[count].redist_base = redist_base;
	acpi_data.redist_regs[count].single_redist = acpi_data.single_redist;
	count++;
}

static int __init
gic_acpi_parse_madt_redist(union acpi_subtable_headers *header,
			   const unsigned long end)
{
	struct acpi_madt_generic_redistributor *redist =
			(struct acpi_madt_generic_redistributor *)header;
	void __iomem *redist_base;

	redist_base = ioremap(redist->base_address, redist->length);
	if (!redist_base) {
		pr_err("Couldn't map GICR region @%llx\n", redist->base_address);
		return -ENOMEM;
	}
	gic_request_region(redist->base_address, redist->length, "GICR");

	gic_acpi_register_redist(redist->base_address, redist_base);
	return 0;
}

static int __init
gic_acpi_parse_madt_gicc(union acpi_subtable_headers *header,
			 const unsigned long end)
{
	struct acpi_madt_generic_interrupt *gicc =
				(struct acpi_madt_generic_interrupt *)header;
	u32 reg = readl_relaxed(acpi_data.dist_base + GICD_PIDR2) & GIC_PIDR2_ARCH_MASK;
	u32 size = reg == GIC_PIDR2_ARCH_GICv4 ? SZ_64K * 4 : SZ_64K * 2;
	void __iomem *redist_base;

	/* GICC entry which has !ACPI_MADT_ENABLED is not unusable so skip */
	if (!(gicc->flags & ACPI_MADT_ENABLED))
		return 0;

	redist_base = ioremap(gicc->gicr_base_address, size);
	if (!redist_base)
		return -ENOMEM;
	gic_request_region(gicc->gicr_base_address, size, "GICR");

	gic_acpi_register_redist(gicc->gicr_base_address, redist_base);
	return 0;
}

static int __init gic_acpi_collect_gicr_base(void)
{
	acpi_tbl_entry_handler redist_parser;
	enum acpi_madt_type type;

	if (acpi_data.single_redist) {
		type = ACPI_MADT_TYPE_GENERIC_INTERRUPT;
		redist_parser = gic_acpi_parse_madt_gicc;
	} else {
		type = ACPI_MADT_TYPE_GENERIC_REDISTRIBUTOR;
		redist_parser = gic_acpi_parse_madt_redist;
	}

	/* Collect redistributor base addresses in GICR entries */
	if (acpi_table_parse_madt(type, redist_parser, 0) > 0)
		return 0;

	pr_info("No valid GICR entries exist\n");
	return -ENODEV;
}

static int __init gic_acpi_match_gicr(union acpi_subtable_headers *header,
				  const unsigned long end)
{
	/* Subtable presence means that redist exists, that's it */
	return 0;
}

static int __init gic_acpi_match_gicc(union acpi_subtable_headers *header,
				      const unsigned long end)
{
	struct acpi_madt_generic_interrupt *gicc =
				(struct acpi_madt_generic_interrupt *)header;

	/*
	 * If GICC is enabled and has valid gicr base address, then it means
	 * GICR base is presented via GICC
	 */
	if ((gicc->flags & ACPI_MADT_ENABLED) && gicc->gicr_base_address) {
		acpi_data.enabled_rdists++;
		return 0;
	}

	/*
	 * It's perfectly valid firmware can pass disabled GICC entry, driver
	 * should not treat as errors, skip the entry instead of probe fail.
	 */
	if (!(gicc->flags & ACPI_MADT_ENABLED))
		return 0;

	return -ENODEV;
}

static int __init gic_acpi_count_gicr_regions(void)
{
	int count;

	/*
	 * Count how many redistributor regions we have. It is not allowed
	 * to mix redistributor description, GICR and GICC subtables have to be
	 * mutually exclusive.
	 */
	count = acpi_table_parse_madt(ACPI_MADT_TYPE_GENERIC_REDISTRIBUTOR,
				      gic_acpi_match_gicr, 0);
	if (count > 0) {
		acpi_data.single_redist = false;
		return count;
	}

	count = acpi_table_parse_madt(ACPI_MADT_TYPE_GENERIC_INTERRUPT,
				      gic_acpi_match_gicc, 0);
	if (count > 0) {
		acpi_data.single_redist = true;
		count = acpi_data.enabled_rdists;
	}

	return count;
}

static bool __init acpi_validate_gic_table(struct acpi_subtable_header *header,
					   struct acpi_probe_entry *ape)
{
	struct acpi_madt_generic_distributor *dist;
	int count;

	dist = (struct acpi_madt_generic_distributor *)header;
	if (dist->version != ape->driver_data)
		return false;

	/* We need to do that exercise anyway, the sooner the better */
	count = gic_acpi_count_gicr_regions();
	if (count <= 0)
		return false;

	acpi_data.nr_redist_regions = count;
	return true;
}

static int __init gic_acpi_parse_virt_madt_gicc(union acpi_subtable_headers *header,
						const unsigned long end)
{
	struct acpi_madt_generic_interrupt *gicc =
		(struct acpi_madt_generic_interrupt *)header;
	int maint_irq_mode;
	static int first_madt = true;

	/* Skip unusable CPUs */
	if (!(gicc->flags & ACPI_MADT_ENABLED))
		return 0;

	maint_irq_mode = (gicc->flags & ACPI_MADT_VGIC_IRQ_MODE) ?
		ACPI_EDGE_SENSITIVE : ACPI_LEVEL_SENSITIVE;

	if (first_madt) {
		first_madt = false;

		acpi_data.maint_irq = gicc->vgic_interrupt;
		acpi_data.maint_irq_mode = maint_irq_mode;
		acpi_data.vcpu_base = gicc->gicv_base_address;

		return 0;
	}

	/*
	 * The maintenance interrupt and GICV should be the same for every CPU
	 */
	if ((acpi_data.maint_irq != gicc->vgic_interrupt) ||
	    (acpi_data.maint_irq_mode != maint_irq_mode) ||
	    (acpi_data.vcpu_base != gicc->gicv_base_address))
		return -EINVAL;

	return 0;
}

static bool __init gic_acpi_collect_virt_info(void)
{
	int count;

	count = acpi_table_parse_madt(ACPI_MADT_TYPE_GENERIC_INTERRUPT,
				      gic_acpi_parse_virt_madt_gicc, 0);

	return (count > 0);
}

#define ACPI_GICV3_DIST_MEM_SIZE (SZ_64K)
#define ACPI_GICV2_VCTRL_MEM_SIZE	(SZ_4K)
#define ACPI_GICV2_VCPU_MEM_SIZE	(SZ_8K)

static void __init gic_acpi_setup_kvm_info(void)
{
	int irq;

	if (!gic_acpi_collect_virt_info()) {
		pr_warn("Unable to get hardware information used for virtualization\n");
		return;
	}

	gic_v3_kvm_info.type = GIC_V3;

	irq = acpi_register_gsi(NULL, acpi_data.maint_irq,
				acpi_data.maint_irq_mode,
				ACPI_ACTIVE_HIGH);
	if (irq <= 0)
		return;

	gic_v3_kvm_info.maint_irq = irq;

	if (acpi_data.vcpu_base) {
		struct resource *vcpu = &gic_v3_kvm_info.vcpu;

		vcpu->flags = IORESOURCE_MEM;
		vcpu->start = acpi_data.vcpu_base;
		vcpu->end = vcpu->start + ACPI_GICV2_VCPU_MEM_SIZE - 1;
	}

	gic_v3_kvm_info.has_v4 = gic_data.rdists.has_vlpis;
	gic_v3_kvm_info.has_v4_1 = gic_data.rdists.has_rvpeid;
	vgic_set_kvm_info(&gic_v3_kvm_info);
}

static struct fwnode_handle *gsi_domain_handle;

static struct fwnode_handle *gic_v3_get_gsi_domain_id(u32 gsi)
{
	return gsi_domain_handle;
}

static int __init
gic_acpi_init(union acpi_subtable_headers *header, const unsigned long end)
{
	struct acpi_madt_generic_distributor *dist;
	size_t size;
	int i, err;

	/* Get distributor base address */
	dist = (struct acpi_madt_generic_distributor *)header;
	acpi_data.dist_base = ioremap(dist->base_address,
				      ACPI_GICV3_DIST_MEM_SIZE);
	if (!acpi_data.dist_base) {
		pr_err("Unable to map GICD registers\n");
		return -ENOMEM;
	}
	gic_request_region(dist->base_address, ACPI_GICV3_DIST_MEM_SIZE, "GICD");

	err = gic_validate_dist_version(acpi_data.dist_base);
	if (err) {
		pr_err("No distributor detected at @%p, giving up\n",
		       acpi_data.dist_base);
		goto out_dist_unmap;
	}

	size = sizeof(*acpi_data.redist_regs) * acpi_data.nr_redist_regions;
	acpi_data.redist_regs = kzalloc(size, GFP_KERNEL);
	if (!acpi_data.redist_regs) {
		err = -ENOMEM;
		goto out_dist_unmap;
	}

	err = gic_acpi_collect_gicr_base();
	if (err)
		goto out_redist_unmap;

	gsi_domain_handle = irq_domain_alloc_fwnode(&dist->base_address);
	if (!gsi_domain_handle) {
		err = -ENOMEM;
		goto out_redist_unmap;
	}

	err = gic_init_bases(dist->base_address, acpi_data.dist_base,
			     acpi_data.redist_regs, acpi_data.nr_redist_regions,
			     0, gsi_domain_handle);
	if (err)
		goto out_fwhandle_free;

	acpi_set_irq_model(ACPI_IRQ_MODEL_GIC, gic_v3_get_gsi_domain_id);

	if (static_branch_likely(&supports_deactivate_key))
		gic_acpi_setup_kvm_info();

	return 0;

out_fwhandle_free:
	irq_domain_free_fwnode(gsi_domain_handle);
out_redist_unmap:
	for (i = 0; i < acpi_data.nr_redist_regions; i++)
		if (acpi_data.redist_regs[i].redist_base)
			iounmap(acpi_data.redist_regs[i].redist_base);
	kfree(acpi_data.redist_regs);
out_dist_unmap:
	iounmap(acpi_data.dist_base);
	return err;
}
IRQCHIP_ACPI_DECLARE(gic_v3, ACPI_MADT_TYPE_GENERIC_DISTRIBUTOR,
		     acpi_validate_gic_table, ACPI_MADT_GIC_VERSION_V3,
		     gic_acpi_init);
IRQCHIP_ACPI_DECLARE(gic_v4, ACPI_MADT_TYPE_GENERIC_DISTRIBUTOR,
		     acpi_validate_gic_table, ACPI_MADT_GIC_VERSION_V4,
		     gic_acpi_init);
IRQCHIP_ACPI_DECLARE(gic_v3_or_v4, ACPI_MADT_TYPE_GENERIC_DISTRIBUTOR,
		     acpi_validate_gic_table, ACPI_MADT_GIC_VERSION_NONE,
		     gic_acpi_init);
#endif
