// SPDX-License-Identifier: GPL-2.0-only
/*
 * Broadcom Brahma-B15 CPU read-ahead cache management functions
 *
 * Copyright (C) 2015-2016 Broadcom
 */

#include <linux/err.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/bitops.h>
#include <linux/of_address.h>
#include <linux/notifier.h>
#include <linux/cpu.h>
#include <linux/syscore_ops.h>
#include <linux/reboot.h>

#include <asm/cacheflush.h>
#include <asm/hardware/cache-b15-rac.h>

extern void v7_flush_kern_cache_all(void);

/* RAC register offsets, relative to the HIF_CPU_BIUCTRL register base */
#define RAC_CONFIG0_REG			(0x78)
#define  RACENPREF_MASK			(0x3)
#define  RACPREFINST_SHIFT		(0)
#define  RACENINST_SHIFT		(2)
#define  RACPREFDATA_SHIFT		(4)
#define  RACENDATA_SHIFT		(6)
#define  RAC_CPU_SHIFT			(8)
#define  RACCFG_MASK			(0xff)
#define RAC_CONFIG1_REG			(0x7c)
/* Brahma-B15 is a quad-core only design */
#define B15_RAC_FLUSH_REG		(0x80)
/* Brahma-B53 is an octo-core design */
#define B53_RAC_FLUSH_REG		(0x84)
#define  FLUSH_RAC			(1 << 0)

/* Bitmask to enable instruction and data prefetching with a 256-bytes stride */
#define RAC_DATA_INST_EN_MASK		(1 << RACPREFINST_SHIFT | \
					 RACENPREF_MASK << RACENINST_SHIFT | \
					 1 << RACPREFDATA_SHIFT | \
					 RACENPREF_MASK << RACENDATA_SHIFT)

#define RAC_ENABLED			0
/* Special state where we want to bypass the spinlock and call directly
 * into the v7 cache maintenance operations during suspend/resume
 */
#define RAC_SUSPENDED			1

static void __iomem *b15_rac_base;
static DEFINE_SPINLOCK(rac_lock);

static u32 rac_config0_reg;
static u32 rac_flush_offset;

/* Initialization flag to avoid checking for b15_rac_base, and to prevent
 * multi-platform kernels from crashing here as well.
 */
static unsigned long b15_rac_flags;

static inline u32 __b15_rac_disable(void)
{
	u32 val = __raw_readl(b15_rac_base + RAC_CONFIG0_REG);
	__raw_writel(0, b15_rac_base + RAC_CONFIG0_REG);
	dmb();
	return val;
}

static inline void __b15_rac_flush(void)
{
	u32 reg;

	__raw_writel(FLUSH_RAC, b15_rac_base + rac_flush_offset);
	do {
		/* This dmb() is required to force the Bus Interface Unit
		 * to clean outstanding writes, and forces an idle cycle
		 * to be inserted.
		 */
		dmb();
		reg = __raw_readl(b15_rac_base + rac_flush_offset);
	} while (reg & FLUSH_RAC);
}

static inline u32 b15_rac_disable_and_flush(void)
{
	u32 reg;

	reg = __b15_rac_disable();
	__b15_rac_flush();
	return reg;
}

static inline void __b15_rac_enable(u32 val)
{
	__raw_writel(val, b15_rac_base + RAC_CONFIG0_REG);
	/* dsb() is required here to be consistent with __flush_icache_all() */
	dsb();
}

#define BUILD_RAC_CACHE_OP(name, bar)				\
void b15_flush_##name(void)					\
{								\
	unsigned int do_flush;					\
	u32 val = 0;						\
								\
	if (test_bit(RAC_SUSPENDED, &b15_rac_flags)) {		\
		v7_flush_##name();				\
		bar;						\
		return;						\
	}							\
								\
	spin_lock(&rac_lock);					\
	do_flush = test_bit(RAC_ENABLED, &b15_rac_flags);	\
	if (do_flush)						\
		val = b15_rac_disable_and_flush();		\
	v7_flush_##name();					\
	if (!do_flush)						\
		bar;						\
	else							\
		__b15_rac_enable(val);				\
	spin_unlock(&rac_lock);					\
}

#define nobarrier

/* The readahead cache present in the Brahma-B15 CPU is a special piece of
 * hardware after the integrated L2 cache of the B15 CPU complex whose purpose
 * is to prefetch instruction and/or data with a line size of either 64 bytes
 * or 256 bytes. The rationale is that the data-bus of the CPU interface is
 * optimized for 256-bytes transactions, and enabling the readahead cache
 * provides a significant performance boost we want it enabled (typically
 * twice the performance for a memcpy benchmark application).
 *
 * The readahead cache is transparent for Modified Virtual Addresses
 * cache maintenance operations: ICIMVAU, DCIMVAC, DCCMVAC, DCCMVAU and
 * DCCIMVAC.
 *
 * It is however not transparent for the following cache maintenance
 * operations: DCISW, DCCSW, DCCISW, ICIALLUIS and ICIALLU which is precisely
 * what we are patching here with our BUILD_RAC_CACHE_OP here.
 */
BUILD_RAC_CACHE_OP(kern_cache_all, nobarrier);

static void b15_rac_enable(void)
{
	unsigned int cpu;
	u32 enable = 0;

	for_each_possible_cpu(cpu)
		enable |= (RAC_DATA_INST_EN_MASK << (cpu * RAC_CPU_SHIFT));

	b15_rac_disable_and_flush();
	__b15_rac_enable(enable);
}

static int b15_rac_reboot_notifier(struct notifier_block *nb,
				   unsigned long action,
				   void *data)
{
	/* During kexec, we are not yet migrated on the boot CPU, so we need to
	 * make sure we are SMP safe here. Once the RAC is disabled, flag it as
	 * suspended such that the hotplug notifier returns early.
	 */
	if (action == SYS_RESTART) {
		spin_lock(&rac_lock);
		b15_rac_disable_and_flush();
		clear_bit(RAC_ENABLED, &b15_rac_flags);
		set_bit(RAC_SUSPENDED, &b15_rac_flags);
		spin_unlock(&rac_lock);
	}

	return NOTIFY_DONE;
}

static struct notifier_block b15_rac_reboot_nb = {
	.notifier_call	= b15_rac_reboot_notifier,
};

/* The CPU hotplug case is the most interesting one, we basically need to make
 * sure that the RAC is disabled for the entire system prior to having a CPU
 * die, in particular prior to this dying CPU having exited the coherency
 * domain.
 *
 * Once this CPU is marked dead, we can safely re-enable the RAC for the
 * remaining CPUs in the system which are still online.
 *
 * Offlining a CPU is the problematic case, onlining a CPU is not much of an
 * issue since the CPU and its cache-level hierarchy will start filling with
 * the RAC disabled, so L1 and L2 only.
 *
 * In this function, we should NOT have to verify any unsafe setting/condition
 * b15_rac_base:
 *
 *   It is protected by the RAC_ENABLED flag which is cleared by default, and
 *   being cleared when initial procedure is done. b15_rac_base had been set at
 *   that time.
 *
 * RAC_ENABLED:
 *   There is a small timing windows, in b15_rac_init(), between
 *      cpuhp_setup_state_*()
 *      ...
 *      set RAC_ENABLED
 *   However, there is no hotplug activity based on the Linux booting procedure.
 *
 * Since we have to disable RAC for all cores, we keep RAC on as long as as
 * possible (disable it as late as possible) to gain the cache benefit.
 *
 * Thus, dying/dead states are chosen here
 *
 * We are choosing not do disable the RAC on a per-CPU basis, here, if we did
 * we would want to consider disabling it as early as possible to benefit the
 * other active CPUs.
 */

/* Running on the dying CPU */
static int b15_rac_dying_cpu(unsigned int cpu)
{
	/* During kexec/reboot, the RAC is disabled via the reboot notifier
	 * return early here.
	 */
	if (test_bit(RAC_SUSPENDED, &b15_rac_flags))
		return 0;

	spin_lock(&rac_lock);

	/* Indicate that we are starting a hotplug procedure */
	__clear_bit(RAC_ENABLED, &b15_rac_flags);

	/* Disable the readahead cache and save its value to a global */
	rac_config0_reg = b15_rac_disable_and_flush();

	spin_unlock(&rac_lock);

	return 0;
}

/* Running on a non-dying CPU */
static int b15_rac_dead_cpu(unsigned int cpu)
{
	/* During kexec/reboot, the RAC is disabled via the reboot notifier
	 * return early here.
	 */
	if (test_bit(RAC_SUSPENDED, &b15_rac_flags))
		return 0;

	spin_lock(&rac_lock);

	/* And enable it */
	__b15_rac_enable(rac_config0_reg);
	__set_bit(RAC_ENABLED, &b15_rac_flags);

	spin_unlock(&rac_lock);

	return 0;
}

static int b15_rac_suspend(void)
{
	/* Suspend the read-ahead cache oeprations, forcing our cache
	 * implementation to fallback to the regular ARMv7 calls.
	 *
	 * We are guaranteed to be running on the boot CPU at this point and
	 * with every other CPU quiesced, so setting RAC_SUSPENDED is not racy
	 * here.
	 */
	rac_config0_reg = b15_rac_disable_and_flush();
	set_bit(RAC_SUSPENDED, &b15_rac_flags);

	return 0;
}

static void b15_rac_resume(void)
{
	/* Coming out of a S3 suspend/resume cycle, the read-ahead cache
	 * register RAC_CONFIG0_REG will be restored to its default value, make
	 * sure we re-enable it and set the enable flag, we are also guaranteed
	 * to run on the boot CPU, so not racy again.
	 */
	__b15_rac_enable(rac_config0_reg);
	clear_bit(RAC_SUSPENDED, &b15_rac_flags);
}

static struct syscore_ops b15_rac_syscore_ops = {
	.suspend	= b15_rac_suspend,
	.resume		= b15_rac_resume,
};

static int __init b15_rac_init(void)
{
	struct device_node *dn, *cpu_dn;
	int ret = 0, cpu;
	u32 reg, en_mask = 0;

	dn = of_find_compatible_node(NULL, NULL, "brcm,brcmstb-cpu-biu-ctrl");
	if (!dn)
		return -ENODEV;

	if (WARN(num_possible_cpus() > 4, "RAC only supports 4 CPUs\n"))
		goto out;

	b15_rac_base = of_iomap(dn, 0);
	if (!b15_rac_base) {
		pr_err("failed to remap BIU control base\n");
		ret = -ENOMEM;
		goto out;
	}

	cpu_dn = of_get_cpu_node(0, NULL);
	if (!cpu_dn) {
		ret = -ENODEV;
		goto out;
	}

	if (of_device_is_compatible(cpu_dn, "brcm,brahma-b15"))
		rac_flush_offset = B15_RAC_FLUSH_REG;
	else if (of_device_is_compatible(cpu_dn, "brcm,brahma-b53"))
		rac_flush_offset = B53_RAC_FLUSH_REG;
	else {
		pr_err("Unsupported CPU\n");
		of_node_put(cpu_dn);
		ret = -EINVAL;
		goto out;
	}
	of_node_put(cpu_dn);

	ret = register_reboot_notifier(&b15_rac_reboot_nb);
	if (ret) {
		pr_err("failed to register reboot notifier\n");
		iounmap(b15_rac_base);
		goto out;
	}

	if (IS_ENABLED(CONFIG_HOTPLUG_CPU)) {
		ret = cpuhp_setup_state_nocalls(CPUHP_AP_ARM_CACHE_B15_RAC_DEAD,
					"arm/cache-b15-rac:dead",
					NULL, b15_rac_dead_cpu);
		if (ret)
			goto out_unmap;

		ret = cpuhp_setup_state_nocalls(CPUHP_AP_ARM_CACHE_B15_RAC_DYING,
					"arm/cache-b15-rac:dying",
					NULL, b15_rac_dying_cpu);
		if (ret)
			goto out_cpu_dead;
	}

	if (IS_ENABLED(CONFIG_PM_SLEEP))
		register_syscore_ops(&b15_rac_syscore_ops);

	spin_lock(&rac_lock);
	reg = __raw_readl(b15_rac_base + RAC_CONFIG0_REG);
	for_each_possible_cpu(cpu)
		en_mask |= ((1 << RACPREFDATA_SHIFT) << (cpu * RAC_CPU_SHIFT));
	WARN(reg & en_mask, "Read-ahead cache not previously disabled\n");

	b15_rac_enable();
	set_bit(RAC_ENABLED, &b15_rac_flags);
	spin_unlock(&rac_lock);

	pr_info("%pOF: Broadcom Brahma-B15 readahead cache\n", dn);

	goto out;

out_cpu_dead:
	cpuhp_remove_state_nocalls(CPUHP_AP_ARM_CACHE_B15_RAC_DYING);
out_unmap:
	unregister_reboot_notifier(&b15_rac_reboot_nb);
	iounmap(b15_rac_base);
out:
	of_node_put(dn);
	return ret;
}
arch_initcall(b15_rac_init);
