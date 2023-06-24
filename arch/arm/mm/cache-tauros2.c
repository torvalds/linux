// SPDX-License-Identifier: GPL-2.0-only
/*
 * arch/arm/mm/cache-tauros2.c - Tauros2 L2 cache controller support
 *
 * Copyright (C) 2008 Marvell Semiconductor
 *
 * References:
 * - PJ1 CPU Core Datasheet,
 *   Document ID MV-S104837-01, Rev 0.7, January 24 2008.
 * - PJ4 CPU Core Datasheet,
 *   Document ID MV-S105190-00, Rev 0.7, March 14 2008.
 */

#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <asm/cacheflush.h>
#include <asm/cp15.h>
#include <asm/cputype.h>
#include <asm/hardware/cache-tauros2.h>

/* CP15 PJ4 Control configuration register */
#define CCR_L2C_PREFETCH_DISABLE	BIT(24)
#define CCR_L2C_ECC_ENABLE		BIT(23)
#define CCR_L2C_WAY7_4_DISABLE		BIT(21)
#define CCR_L2C_BURST8_ENABLE		BIT(20)

/*
 * When Tauros2 is used on a CPU that supports the v7 hierarchical
 * cache operations, the cache handling code in proc-v7.S takes care
 * of everything, including handling DMA coherency.
 *
 * So, we only need to register outer cache operations here if we're
 * being used on a pre-v7 CPU, and we only need to build support for
 * outer cache operations into the kernel image if the kernel has been
 * configured to support a pre-v7 CPU.
 */
#ifdef CONFIG_CPU_32v5
/*
 * Low-level cache maintenance operations.
 */
static inline void tauros2_clean_pa(unsigned long addr)
{
	__asm__("mcr p15, 1, %0, c7, c11, 3" : : "r" (addr));
}

static inline void tauros2_clean_inv_pa(unsigned long addr)
{
	__asm__("mcr p15, 1, %0, c7, c15, 3" : : "r" (addr));
}

static inline void tauros2_inv_pa(unsigned long addr)
{
	__asm__("mcr p15, 1, %0, c7, c7, 3" : : "r" (addr));
}


/*
 * Linux primitives.
 *
 * Note that the end addresses passed to Linux primitives are
 * noninclusive.
 */
#define CACHE_LINE_SIZE		32

static void tauros2_inv_range(unsigned long start, unsigned long end)
{
	/*
	 * Clean and invalidate partial first cache line.
	 */
	if (start & (CACHE_LINE_SIZE - 1)) {
		tauros2_clean_inv_pa(start & ~(CACHE_LINE_SIZE - 1));
		start = (start | (CACHE_LINE_SIZE - 1)) + 1;
	}

	/*
	 * Clean and invalidate partial last cache line.
	 */
	if (end & (CACHE_LINE_SIZE - 1)) {
		tauros2_clean_inv_pa(end & ~(CACHE_LINE_SIZE - 1));
		end &= ~(CACHE_LINE_SIZE - 1);
	}

	/*
	 * Invalidate all full cache lines between 'start' and 'end'.
	 */
	while (start < end) {
		tauros2_inv_pa(start);
		start += CACHE_LINE_SIZE;
	}

	dsb();
}

static void tauros2_clean_range(unsigned long start, unsigned long end)
{
	start &= ~(CACHE_LINE_SIZE - 1);
	while (start < end) {
		tauros2_clean_pa(start);
		start += CACHE_LINE_SIZE;
	}

	dsb();
}

static void tauros2_flush_range(unsigned long start, unsigned long end)
{
	start &= ~(CACHE_LINE_SIZE - 1);
	while (start < end) {
		tauros2_clean_inv_pa(start);
		start += CACHE_LINE_SIZE;
	}

	dsb();
}

static void tauros2_disable(void)
{
	__asm__ __volatile__ (
	"mcr	p15, 1, %0, c7, c11, 0 @L2 Cache Clean All\n\t"
	"mrc	p15, 0, %0, c1, c0, 0\n\t"
	"bic	%0, %0, #(1 << 26)\n\t"
	"mcr	p15, 0, %0, c1, c0, 0  @Disable L2 Cache\n\t"
	: : "r" (0x0));
}

static void tauros2_resume(void)
{
	__asm__ __volatile__ (
	"mcr	p15, 1, %0, c7, c7, 0 @L2 Cache Invalidate All\n\t"
	"mrc	p15, 0, %0, c1, c0, 0\n\t"
	"orr	%0, %0, #(1 << 26)\n\t"
	"mcr	p15, 0, %0, c1, c0, 0 @Enable L2 Cache\n\t"
	: : "r" (0x0));
}
#endif

static inline u32 __init read_extra_features(void)
{
	u32 u;

	__asm__("mrc p15, 1, %0, c15, c1, 0" : "=r" (u));

	return u;
}

static inline void __init write_extra_features(u32 u)
{
	__asm__("mcr p15, 1, %0, c15, c1, 0" : : "r" (u));
}

static inline int __init cpuid_scheme(void)
{
	return !!((processor_id & 0x000f0000) == 0x000f0000);
}

static inline u32 __init read_mmfr3(void)
{
	u32 mmfr3;

	__asm__("mrc p15, 0, %0, c0, c1, 7\n" : "=r" (mmfr3));

	return mmfr3;
}

static inline u32 __init read_actlr(void)
{
	u32 actlr;

	__asm__("mrc p15, 0, %0, c1, c0, 1\n" : "=r" (actlr));

	return actlr;
}

static inline void __init write_actlr(u32 actlr)
{
	__asm__("mcr p15, 0, %0, c1, c0, 1\n" : : "r" (actlr));
}

static void enable_extra_feature(unsigned int features)
{
	u32 u;

	u = read_extra_features();

	if (features & CACHE_TAUROS2_PREFETCH_ON)
		u &= ~CCR_L2C_PREFETCH_DISABLE;
	else
		u |= CCR_L2C_PREFETCH_DISABLE;
	pr_info("Tauros2: %s L2 prefetch.\n",
			(features & CACHE_TAUROS2_PREFETCH_ON)
			? "Enabling" : "Disabling");

	if (features & CACHE_TAUROS2_LINEFILL_BURST8)
		u |= CCR_L2C_BURST8_ENABLE;
	else
		u &= ~CCR_L2C_BURST8_ENABLE;
	pr_info("Tauros2: %s burst8 line fill.\n",
			(features & CACHE_TAUROS2_LINEFILL_BURST8)
			? "Enabling" : "Disabling");

	write_extra_features(u);
}

static void __init tauros2_internal_init(unsigned int features)
{
	char *mode = NULL;

	enable_extra_feature(features);

#ifdef CONFIG_CPU_32v5
	if ((processor_id & 0xff0f0000) == 0x56050000) {
		u32 feat;

		/*
		 * v5 CPUs with Tauros2 have the L2 cache enable bit
		 * located in the CPU Extra Features register.
		 */
		feat = read_extra_features();
		if (!(feat & 0x00400000)) {
			pr_info("Tauros2: Enabling L2 cache.\n");
			write_extra_features(feat | 0x00400000);
		}

		mode = "ARMv5";
		outer_cache.inv_range = tauros2_inv_range;
		outer_cache.clean_range = tauros2_clean_range;
		outer_cache.flush_range = tauros2_flush_range;
		outer_cache.disable = tauros2_disable;
		outer_cache.resume = tauros2_resume;
	}
#endif

#ifdef CONFIG_CPU_32v7
	/*
	 * Check whether this CPU has support for the v7 hierarchical
	 * cache ops.  (PJ4 is in its v7 personality mode if the MMFR3
	 * register indicates support for the v7 hierarchical cache
	 * ops.)
	 *
	 * (Although strictly speaking there may exist CPUs that
	 * implement the v7 cache ops but are only ARMv6 CPUs (due to
	 * not complying with all of the other ARMv7 requirements),
	 * there are no real-life examples of Tauros2 being used on
	 * such CPUs as of yet.)
	 */
	if (cpuid_scheme() && (read_mmfr3() & 0xf) == 1) {
		u32 actlr;

		/*
		 * When Tauros2 is used in an ARMv7 system, the L2
		 * enable bit is located in the Auxiliary System Control
		 * Register (which is the only register allowed by the
		 * ARMv7 spec to contain fine-grained cache control bits).
		 */
		actlr = read_actlr();
		if (!(actlr & 0x00000002)) {
			pr_info("Tauros2: Enabling L2 cache.\n");
			write_actlr(actlr | 0x00000002);
		}

		mode = "ARMv7";
	}
#endif

	if (mode == NULL) {
		pr_crit("Tauros2: Unable to detect CPU mode.\n");
		return;
	}

	pr_info("Tauros2: L2 cache support initialised "
			 "in %s mode.\n", mode);
}

#ifdef CONFIG_OF
static const struct of_device_id tauros2_ids[] __initconst = {
	{ .compatible = "marvell,tauros2-cache"},
	{}
};
#endif

void __init tauros2_init(unsigned int features)
{
#ifdef CONFIG_OF
	struct device_node *node;
	int ret;
	unsigned int f;

	node = of_find_matching_node(NULL, tauros2_ids);
	if (!node) {
		pr_info("Not found marvell,tauros2-cache, disable it\n");
	} else {
		ret = of_property_read_u32(node, "marvell,tauros2-cache-features", &f);
		if (ret) {
			pr_info("Not found marvell,tauros-cache-features property, "
				"disable extra features\n");
			features = 0;
		} else
			features = f;
	}
#endif
	tauros2_internal_init(features);
}
