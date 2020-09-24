/*
 *  linux/drivers/clocksource/arm_arch_timer.c
 *
 *  Copyright (C) 2011 ARM Ltd.
 *  All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define pr_fmt(fmt)	"arm_arch_timer: " fmt

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/smp.h>
#include <linux/cpu.h>
#include <linux/cpu_pm.h>
#include <linux/clockchips.h>
#include <linux/clocksource.h>
#include <linux/interrupt.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/sched/clock.h>
#include <linux/sched_clock.h>
#include <linux/acpi.h>

#include <asm/arch_timer.h>
#include <asm/virt.h>

#include <clocksource/arm_arch_timer.h>

#undef pr_fmt
#define pr_fmt(fmt) "arch_timer: " fmt

#define CNTTIDR		0x08
#define CNTTIDR_VIRT(n)	(BIT(1) << ((n) * 4))

#define CNTACR(n)	(0x40 + ((n) * 4))
#define CNTACR_RPCT	BIT(0)
#define CNTACR_RVCT	BIT(1)
#define CNTACR_RFRQ	BIT(2)
#define CNTACR_RVOFF	BIT(3)
#define CNTACR_RWVT	BIT(4)
#define CNTACR_RWPT	BIT(5)

#define CNTVCT_LO	0x08
#define CNTVCT_HI	0x0c
#define CNTFRQ		0x10
#define CNTP_TVAL	0x28
#define CNTP_CTL	0x2c
#define CNTCVAL_LO	0x30
#define CNTCVAL_HI	0x34
#define CNTV_TVAL	0x38
#define CNTV_CTL	0x3c

static unsigned arch_timers_present __initdata;

static void __iomem *arch_counter_base;

struct arch_timer {
	void __iomem *base;
	struct clock_event_device evt;
};

#define to_arch_timer(e) container_of(e, struct arch_timer, evt)

static u32 arch_timer_rate;
static int arch_timer_ppi[ARCH_TIMER_MAX_TIMER_PPI];

static struct clock_event_device __percpu *arch_timer_evt;

static enum arch_timer_ppi_nr arch_timer_uses_ppi = ARCH_TIMER_VIRT_PPI;
static bool arch_timer_c3stop;
static bool arch_timer_mem_use_virtual;
static bool arch_counter_suspend_stop;
static bool vdso_default = true;

static cpumask_t evtstrm_available = CPU_MASK_NONE;
static bool evtstrm_enable = IS_ENABLED(CONFIG_ARM_ARCH_TIMER_EVTSTREAM);

static int __init early_evtstrm_cfg(char *buf)
{
	return strtobool(buf, &evtstrm_enable);
}
early_param("clocksource.arm_arch_timer.evtstrm", early_evtstrm_cfg);

/*
 * Architected system timer support.
 */

static __always_inline
void arch_timer_reg_write(int access, enum arch_timer_reg reg, u32 val,
			  struct clock_event_device *clk)
{
	if (access == ARCH_TIMER_MEM_PHYS_ACCESS) {
		struct arch_timer *timer = to_arch_timer(clk);
		switch (reg) {
		case ARCH_TIMER_REG_CTRL:
			writel_relaxed(val, timer->base + CNTP_CTL);
			break;
		case ARCH_TIMER_REG_TVAL:
			writel_relaxed(val, timer->base + CNTP_TVAL);
			break;
		}
	} else if (access == ARCH_TIMER_MEM_VIRT_ACCESS) {
		struct arch_timer *timer = to_arch_timer(clk);
		switch (reg) {
		case ARCH_TIMER_REG_CTRL:
			writel_relaxed(val, timer->base + CNTV_CTL);
			break;
		case ARCH_TIMER_REG_TVAL:
			writel_relaxed(val, timer->base + CNTV_TVAL);
			break;
		}
	} else {
		arch_timer_reg_write_cp15(access, reg, val);
	}
}

static __always_inline
u32 arch_timer_reg_read(int access, enum arch_timer_reg reg,
			struct clock_event_device *clk)
{
	u32 val;

	if (access == ARCH_TIMER_MEM_PHYS_ACCESS) {
		struct arch_timer *timer = to_arch_timer(clk);
		switch (reg) {
		case ARCH_TIMER_REG_CTRL:
			val = readl_relaxed(timer->base + CNTP_CTL);
			break;
		case ARCH_TIMER_REG_TVAL:
			val = readl_relaxed(timer->base + CNTP_TVAL);
			break;
		}
	} else if (access == ARCH_TIMER_MEM_VIRT_ACCESS) {
		struct arch_timer *timer = to_arch_timer(clk);
		switch (reg) {
		case ARCH_TIMER_REG_CTRL:
			val = readl_relaxed(timer->base + CNTV_CTL);
			break;
		case ARCH_TIMER_REG_TVAL:
			val = readl_relaxed(timer->base + CNTV_TVAL);
			break;
		}
	} else {
		val = arch_timer_reg_read_cp15(access, reg);
	}

	return val;
}

/*
 * Default to cp15 based access because arm64 uses this function for
 * sched_clock() before DT is probed and the cp15 method is guaranteed
 * to exist on arm64. arm doesn't use this before DT is probed so even
 * if we don't have the cp15 accessors we won't have a problem.
 */
u64 (*arch_timer_read_counter)(void) = arch_counter_get_cntvct;
EXPORT_SYMBOL_GPL(arch_timer_read_counter);

static u64 arch_counter_read(struct clocksource *cs)
{
	return arch_timer_read_counter();
}

static u64 arch_counter_read_cc(const struct cyclecounter *cc)
{
	return arch_timer_read_counter();
}

static struct clocksource clocksource_counter = {
	.name	= "arch_sys_counter",
	.rating	= 400,
	.read	= arch_counter_read,
	.mask	= CLOCKSOURCE_MASK(56),
	.flags	= CLOCK_SOURCE_IS_CONTINUOUS,
};

static struct cyclecounter cyclecounter __ro_after_init = {
	.read	= arch_counter_read_cc,
	.mask	= CLOCKSOURCE_MASK(56),
};

struct ate_acpi_oem_info {
	char oem_id[ACPI_OEM_ID_SIZE + 1];
	char oem_table_id[ACPI_OEM_TABLE_ID_SIZE + 1];
	u32 oem_revision;
};

#ifdef CONFIG_FSL_ERRATUM_A008585
/*
 * The number of retries is an arbitrary value well beyond the highest number
 * of iterations the loop has been observed to take.
 */
#define __fsl_a008585_read_reg(reg) ({			\
	u64 _old, _new;					\
	int _retries = 200;				\
							\
	do {						\
		_old = read_sysreg(reg);		\
		_new = read_sysreg(reg);		\
		_retries--;				\
	} while (unlikely(_old != _new) && _retries);	\
							\
	WARN_ON_ONCE(!_retries);			\
	_new;						\
})

static u32 notrace fsl_a008585_read_cntp_tval_el0(void)
{
	return __fsl_a008585_read_reg(cntp_tval_el0);
}

static u32 notrace fsl_a008585_read_cntv_tval_el0(void)
{
	return __fsl_a008585_read_reg(cntv_tval_el0);
}

static u64 notrace fsl_a008585_read_cntpct_el0(void)
{
	return __fsl_a008585_read_reg(cntpct_el0);
}

static u64 notrace fsl_a008585_read_cntvct_el0(void)
{
	return __fsl_a008585_read_reg(cntvct_el0);
}
#endif

#ifdef CONFIG_HISILICON_ERRATUM_161010101
/*
 * Verify whether the value of the second read is larger than the first by
 * less than 32 is the only way to confirm the value is correct, so clear the
 * lower 5 bits to check whether the difference is greater than 32 or not.
 * Theoretically the erratum should not occur more than twice in succession
 * when reading the system counter, but it is possible that some interrupts
 * may lead to more than twice read errors, triggering the warning, so setting
 * the number of retries far beyond the number of iterations the loop has been
 * observed to take.
 */
#define __hisi_161010101_read_reg(reg) ({				\
	u64 _old, _new;						\
	int _retries = 50;					\
								\
	do {							\
		_old = read_sysreg(reg);			\
		_new = read_sysreg(reg);			\
		_retries--;					\
	} while (unlikely((_new - _old) >> 5) && _retries);	\
								\
	WARN_ON_ONCE(!_retries);				\
	_new;							\
})

static u32 notrace hisi_161010101_read_cntp_tval_el0(void)
{
	return __hisi_161010101_read_reg(cntp_tval_el0);
}

static u32 notrace hisi_161010101_read_cntv_tval_el0(void)
{
	return __hisi_161010101_read_reg(cntv_tval_el0);
}

static u64 notrace hisi_161010101_read_cntpct_el0(void)
{
	return __hisi_161010101_read_reg(cntpct_el0);
}

static u64 notrace hisi_161010101_read_cntvct_el0(void)
{
	return __hisi_161010101_read_reg(cntvct_el0);
}

static struct ate_acpi_oem_info hisi_161010101_oem_info[] = {
	/*
	 * Note that trailing spaces are required to properly match
	 * the OEM table information.
	 */
	{
		.oem_id		= "HISI  ",
		.oem_table_id	= "HIP05   ",
		.oem_revision	= 0,
	},
	{
		.oem_id		= "HISI  ",
		.oem_table_id	= "HIP06   ",
		.oem_revision	= 0,
	},
	{
		.oem_id		= "HISI  ",
		.oem_table_id	= "HIP07   ",
		.oem_revision	= 0,
	},
	{ /* Sentinel indicating the end of the OEM array */ },
};
#endif

#ifdef CONFIG_ARM64_ERRATUM_858921
static u64 notrace arm64_858921_read_cntpct_el0(void)
{
	u64 old, new;

	old = read_sysreg(cntpct_el0);
	new = read_sysreg(cntpct_el0);
	return (((old ^ new) >> 32) & 1) ? old : new;
}

static u64 notrace arm64_858921_read_cntvct_el0(void)
{
	u64 old, new;

	old = read_sysreg(cntvct_el0);
	new = read_sysreg(cntvct_el0);
	return (((old ^ new) >> 32) & 1) ? old : new;
}
#endif

#ifdef CONFIG_SUN50I_ERRATUM_UNKNOWN1
/*
 * The low bits of the counter registers are indeterminate while bit 10 or
 * greater is rolling over. Since the counter value can jump both backward
 * (7ff -> 000 -> 800) and forward (7ff -> fff -> 800), ignore register values
 * with all ones or all zeros in the low bits. Bound the loop by the maximum
 * number of CPU cycles in 3 consecutive 24 MHz counter periods.
 */
#define __sun50i_a64_read_reg(reg) ({					\
	u64 _val;							\
	int _retries = 150;						\
									\
	do {								\
		_val = read_sysreg(reg);				\
		_retries--;						\
	} while (((_val + 1) & GENMASK(9, 0)) <= 1 && _retries);	\
									\
	WARN_ON_ONCE(!_retries);					\
	_val;								\
})

static u64 notrace sun50i_a64_read_cntpct_el0(void)
{
	return __sun50i_a64_read_reg(cntpct_el0);
}

static u64 notrace sun50i_a64_read_cntvct_el0(void)
{
	return __sun50i_a64_read_reg(cntvct_el0);
}

static u32 notrace sun50i_a64_read_cntp_tval_el0(void)
{
	return read_sysreg(cntp_cval_el0) - sun50i_a64_read_cntpct_el0();
}

static u32 notrace sun50i_a64_read_cntv_tval_el0(void)
{
	return read_sysreg(cntv_cval_el0) - sun50i_a64_read_cntvct_el0();
}
#endif

#ifdef CONFIG_ARM_ARCH_TIMER_OOL_WORKAROUND
DEFINE_PER_CPU(const struct arch_timer_erratum_workaround *, timer_unstable_counter_workaround);
EXPORT_SYMBOL_GPL(timer_unstable_counter_workaround);

DEFINE_STATIC_KEY_FALSE(arch_timer_read_ool_enabled);
EXPORT_SYMBOL_GPL(arch_timer_read_ool_enabled);

static void erratum_set_next_event_tval_generic(const int access, unsigned long evt,
						struct clock_event_device *clk)
{
	unsigned long ctrl;
	u64 cval;

	ctrl = arch_timer_reg_read(access, ARCH_TIMER_REG_CTRL, clk);
	ctrl |= ARCH_TIMER_CTRL_ENABLE;
	ctrl &= ~ARCH_TIMER_CTRL_IT_MASK;

	if (access == ARCH_TIMER_PHYS_ACCESS) {
		cval = evt + arch_counter_get_cntpct();
		write_sysreg(cval, cntp_cval_el0);
	} else {
		cval = evt + arch_counter_get_cntvct();
		write_sysreg(cval, cntv_cval_el0);
	}

	arch_timer_reg_write(access, ARCH_TIMER_REG_CTRL, ctrl, clk);
}

static __maybe_unused int erratum_set_next_event_tval_virt(unsigned long evt,
					    struct clock_event_device *clk)
{
	erratum_set_next_event_tval_generic(ARCH_TIMER_VIRT_ACCESS, evt, clk);
	return 0;
}

static __maybe_unused int erratum_set_next_event_tval_phys(unsigned long evt,
					    struct clock_event_device *clk)
{
	erratum_set_next_event_tval_generic(ARCH_TIMER_PHYS_ACCESS, evt, clk);
	return 0;
}

static const struct arch_timer_erratum_workaround ool_workarounds[] = {
#ifdef CONFIG_FSL_ERRATUM_A008585
	{
		.match_type = ate_match_dt,
		.id = "fsl,erratum-a008585",
		.desc = "Freescale erratum a005858",
		.read_cntp_tval_el0 = fsl_a008585_read_cntp_tval_el0,
		.read_cntv_tval_el0 = fsl_a008585_read_cntv_tval_el0,
		.read_cntpct_el0 = fsl_a008585_read_cntpct_el0,
		.read_cntvct_el0 = fsl_a008585_read_cntvct_el0,
		.set_next_event_phys = erratum_set_next_event_tval_phys,
		.set_next_event_virt = erratum_set_next_event_tval_virt,
	},
#endif
#ifdef CONFIG_HISILICON_ERRATUM_161010101
	{
		.match_type = ate_match_dt,
		.id = "hisilicon,erratum-161010101",
		.desc = "HiSilicon erratum 161010101",
		.read_cntp_tval_el0 = hisi_161010101_read_cntp_tval_el0,
		.read_cntv_tval_el0 = hisi_161010101_read_cntv_tval_el0,
		.read_cntpct_el0 = hisi_161010101_read_cntpct_el0,
		.read_cntvct_el0 = hisi_161010101_read_cntvct_el0,
		.set_next_event_phys = erratum_set_next_event_tval_phys,
		.set_next_event_virt = erratum_set_next_event_tval_virt,
	},
	{
		.match_type = ate_match_acpi_oem_info,
		.id = hisi_161010101_oem_info,
		.desc = "HiSilicon erratum 161010101",
		.read_cntp_tval_el0 = hisi_161010101_read_cntp_tval_el0,
		.read_cntv_tval_el0 = hisi_161010101_read_cntv_tval_el0,
		.read_cntpct_el0 = hisi_161010101_read_cntpct_el0,
		.read_cntvct_el0 = hisi_161010101_read_cntvct_el0,
		.set_next_event_phys = erratum_set_next_event_tval_phys,
		.set_next_event_virt = erratum_set_next_event_tval_virt,
	},
#endif
#ifdef CONFIG_ARM64_ERRATUM_858921
	{
		.match_type = ate_match_local_cap_id,
		.id = (void *)ARM64_WORKAROUND_858921,
		.desc = "ARM erratum 858921",
		.read_cntpct_el0 = arm64_858921_read_cntpct_el0,
		.read_cntvct_el0 = arm64_858921_read_cntvct_el0,
	},
#endif
#ifdef CONFIG_SUN50I_ERRATUM_UNKNOWN1
	{
		.match_type = ate_match_dt,
		.id = "allwinner,erratum-unknown1",
		.desc = "Allwinner erratum UNKNOWN1",
		.read_cntp_tval_el0 = sun50i_a64_read_cntp_tval_el0,
		.read_cntv_tval_el0 = sun50i_a64_read_cntv_tval_el0,
		.read_cntpct_el0 = sun50i_a64_read_cntpct_el0,
		.read_cntvct_el0 = sun50i_a64_read_cntvct_el0,
		.set_next_event_phys = erratum_set_next_event_tval_phys,
		.set_next_event_virt = erratum_set_next_event_tval_virt,
	},
#endif
};

typedef bool (*ate_match_fn_t)(const struct arch_timer_erratum_workaround *,
			       const void *);

static
bool arch_timer_check_dt_erratum(const struct arch_timer_erratum_workaround *wa,
				 const void *arg)
{
	const struct device_node *np = arg;

	return of_property_read_bool(np, wa->id);
}

static
bool arch_timer_check_local_cap_erratum(const struct arch_timer_erratum_workaround *wa,
					const void *arg)
{
	return this_cpu_has_cap((uintptr_t)wa->id);
}


static
bool arch_timer_check_acpi_oem_erratum(const struct arch_timer_erratum_workaround *wa,
				       const void *arg)
{
	static const struct ate_acpi_oem_info empty_oem_info = {};
	const struct ate_acpi_oem_info *info = wa->id;
	const struct acpi_table_header *table = arg;

	/* Iterate over the ACPI OEM info array, looking for a match */
	while (memcmp(info, &empty_oem_info, sizeof(*info))) {
		if (!memcmp(info->oem_id, table->oem_id, ACPI_OEM_ID_SIZE) &&
		    !memcmp(info->oem_table_id, table->oem_table_id, ACPI_OEM_TABLE_ID_SIZE) &&
		    info->oem_revision == table->oem_revision)
			return true;

		info++;
	}

	return false;
}

static const struct arch_timer_erratum_workaround *
arch_timer_iterate_errata(enum arch_timer_erratum_match_type type,
			  ate_match_fn_t match_fn,
			  void *arg)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ool_workarounds); i++) {
		if (ool_workarounds[i].match_type != type)
			continue;

		if (match_fn(&ool_workarounds[i], arg))
			return &ool_workarounds[i];
	}

	return NULL;
}

static
void arch_timer_enable_workaround(const struct arch_timer_erratum_workaround *wa,
				  bool local)
{
	int i;

	if (local) {
		__this_cpu_write(timer_unstable_counter_workaround, wa);
	} else {
		for_each_possible_cpu(i)
			per_cpu(timer_unstable_counter_workaround, i) = wa;
	}

	/*
	 * Use the locked version, as we're called from the CPU
	 * hotplug framework. Otherwise, we end-up in deadlock-land.
	 */
	static_branch_enable_cpuslocked(&arch_timer_read_ool_enabled);

	/*
	 * Don't use the vdso fastpath if errata require using the
	 * out-of-line counter accessor. We may change our mind pretty
	 * late in the game (with a per-CPU erratum, for example), so
	 * change both the default value and the vdso itself.
	 */
	if (wa->read_cntvct_el0) {
		clocksource_counter.archdata.vdso_direct = false;
		vdso_default = false;
	}
}

static void arch_timer_check_ool_workaround(enum arch_timer_erratum_match_type type,
					    void *arg)
{
	const struct arch_timer_erratum_workaround *wa;
	ate_match_fn_t match_fn = NULL;
	bool local = false;

	switch (type) {
	case ate_match_dt:
		match_fn = arch_timer_check_dt_erratum;
		break;
	case ate_match_local_cap_id:
		match_fn = arch_timer_check_local_cap_erratum;
		local = true;
		break;
	case ate_match_acpi_oem_info:
		match_fn = arch_timer_check_acpi_oem_erratum;
		break;
	default:
		WARN_ON(1);
		return;
	}

	wa = arch_timer_iterate_errata(type, match_fn, arg);
	if (!wa)
		return;

	if (needs_unstable_timer_counter_workaround()) {
		const struct arch_timer_erratum_workaround *__wa;
		__wa = __this_cpu_read(timer_unstable_counter_workaround);
		if (__wa && wa != __wa)
			pr_warn("Can't enable workaround for %s (clashes with %s\n)",
				wa->desc, __wa->desc);

		if (__wa)
			return;
	}

	arch_timer_enable_workaround(wa, local);
	pr_info("Enabling %s workaround for %s\n",
		local ? "local" : "global", wa->desc);
}

#define erratum_handler(fn, r, ...)					\
({									\
	bool __val;							\
	if (needs_unstable_timer_counter_workaround()) {		\
		const struct arch_timer_erratum_workaround *__wa;	\
		__wa = __this_cpu_read(timer_unstable_counter_workaround); \
		if (__wa && __wa->fn) {					\
			r = __wa->fn(__VA_ARGS__);			\
			__val = true;					\
		} else {						\
			__val = false;					\
		}							\
	} else {							\
		__val = false;						\
	}								\
	__val;								\
})

static bool arch_timer_this_cpu_has_cntvct_wa(void)
{
	const struct arch_timer_erratum_workaround *wa;

	wa = __this_cpu_read(timer_unstable_counter_workaround);
	return wa && wa->read_cntvct_el0;
}
#else
#define arch_timer_check_ool_workaround(t,a)		do { } while(0)
#define erratum_set_next_event_tval_virt(...)		({BUG(); 0;})
#define erratum_set_next_event_tval_phys(...)		({BUG(); 0;})
#define erratum_handler(fn, r, ...)			({false;})
#define arch_timer_this_cpu_has_cntvct_wa()		({false;})
#endif /* CONFIG_ARM_ARCH_TIMER_OOL_WORKAROUND */

static __always_inline irqreturn_t timer_handler(const int access,
					struct clock_event_device *evt)
{
	unsigned long ctrl;

	ctrl = arch_timer_reg_read(access, ARCH_TIMER_REG_CTRL, evt);
	if (ctrl & ARCH_TIMER_CTRL_IT_STAT) {
		ctrl |= ARCH_TIMER_CTRL_IT_MASK;
		arch_timer_reg_write(access, ARCH_TIMER_REG_CTRL, ctrl, evt);
		evt->event_handler(evt);
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static irqreturn_t arch_timer_handler_virt(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;

	return timer_handler(ARCH_TIMER_VIRT_ACCESS, evt);
}

static irqreturn_t arch_timer_handler_phys(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;

	return timer_handler(ARCH_TIMER_PHYS_ACCESS, evt);
}

static irqreturn_t arch_timer_handler_phys_mem(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;

	return timer_handler(ARCH_TIMER_MEM_PHYS_ACCESS, evt);
}

static irqreturn_t arch_timer_handler_virt_mem(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;

	return timer_handler(ARCH_TIMER_MEM_VIRT_ACCESS, evt);
}

static __always_inline int timer_shutdown(const int access,
					  struct clock_event_device *clk)
{
	unsigned long ctrl;

	ctrl = arch_timer_reg_read(access, ARCH_TIMER_REG_CTRL, clk);
	ctrl &= ~ARCH_TIMER_CTRL_ENABLE;
	arch_timer_reg_write(access, ARCH_TIMER_REG_CTRL, ctrl, clk);

	return 0;
}

static int arch_timer_shutdown_virt(struct clock_event_device *clk)
{
	return timer_shutdown(ARCH_TIMER_VIRT_ACCESS, clk);
}

static int arch_timer_shutdown_phys(struct clock_event_device *clk)
{
	return timer_shutdown(ARCH_TIMER_PHYS_ACCESS, clk);
}

static int arch_timer_shutdown_virt_mem(struct clock_event_device *clk)
{
	return timer_shutdown(ARCH_TIMER_MEM_VIRT_ACCESS, clk);
}

static int arch_timer_shutdown_phys_mem(struct clock_event_device *clk)
{
	return timer_shutdown(ARCH_TIMER_MEM_PHYS_ACCESS, clk);
}

static __always_inline void set_next_event(const int access, unsigned long evt,
					   struct clock_event_device *clk)
{
	unsigned long ctrl;
	ctrl = arch_timer_reg_read(access, ARCH_TIMER_REG_CTRL, clk);
	ctrl |= ARCH_TIMER_CTRL_ENABLE;
	ctrl &= ~ARCH_TIMER_CTRL_IT_MASK;
	arch_timer_reg_write(access, ARCH_TIMER_REG_TVAL, evt, clk);
	arch_timer_reg_write(access, ARCH_TIMER_REG_CTRL, ctrl, clk);
}

static int arch_timer_set_next_event_virt(unsigned long evt,
					  struct clock_event_device *clk)
{
	int ret;

	if (erratum_handler(set_next_event_virt, ret, evt, clk))
		return ret;

	set_next_event(ARCH_TIMER_VIRT_ACCESS, evt, clk);
	return 0;
}

static int arch_timer_set_next_event_phys(unsigned long evt,
					  struct clock_event_device *clk)
{
	int ret;

	if (erratum_handler(set_next_event_phys, ret, evt, clk))
		return ret;

	set_next_event(ARCH_TIMER_PHYS_ACCESS, evt, clk);
	return 0;
}

static int arch_timer_set_next_event_virt_mem(unsigned long evt,
					      struct clock_event_device *clk)
{
	set_next_event(ARCH_TIMER_MEM_VIRT_ACCESS, evt, clk);
	return 0;
}

static int arch_timer_set_next_event_phys_mem(unsigned long evt,
					      struct clock_event_device *clk)
{
	set_next_event(ARCH_TIMER_MEM_PHYS_ACCESS, evt, clk);
	return 0;
}

static void __arch_timer_setup(unsigned type,
			       struct clock_event_device *clk)
{
	clk->features = CLOCK_EVT_FEAT_ONESHOT;

	if (type == ARCH_TIMER_TYPE_CP15) {
		if (arch_timer_c3stop)
			clk->features |= CLOCK_EVT_FEAT_C3STOP;
		clk->name = "arch_sys_timer";
		clk->rating = 450;
		clk->cpumask = cpumask_of(smp_processor_id());
		clk->irq = arch_timer_ppi[arch_timer_uses_ppi];
		switch (arch_timer_uses_ppi) {
		case ARCH_TIMER_VIRT_PPI:
			clk->set_state_shutdown = arch_timer_shutdown_virt;
			clk->set_state_oneshot_stopped = arch_timer_shutdown_virt;
			clk->set_next_event = arch_timer_set_next_event_virt;
			break;
		case ARCH_TIMER_PHYS_SECURE_PPI:
		case ARCH_TIMER_PHYS_NONSECURE_PPI:
		case ARCH_TIMER_HYP_PPI:
			clk->set_state_shutdown = arch_timer_shutdown_phys;
			clk->set_state_oneshot_stopped = arch_timer_shutdown_phys;
			clk->set_next_event = arch_timer_set_next_event_phys;
			break;
		default:
			BUG();
		}

		arch_timer_check_ool_workaround(ate_match_local_cap_id, NULL);
	} else {
		clk->features |= CLOCK_EVT_FEAT_DYNIRQ;
		clk->name = "arch_mem_timer";
		clk->rating = 400;
		clk->cpumask = cpu_possible_mask;
		if (arch_timer_mem_use_virtual) {
			clk->set_state_shutdown = arch_timer_shutdown_virt_mem;
			clk->set_state_oneshot_stopped = arch_timer_shutdown_virt_mem;
			clk->set_next_event =
				arch_timer_set_next_event_virt_mem;
		} else {
			clk->set_state_shutdown = arch_timer_shutdown_phys_mem;
			clk->set_state_oneshot_stopped = arch_timer_shutdown_phys_mem;
			clk->set_next_event =
				arch_timer_set_next_event_phys_mem;
		}
	}

	clk->set_state_shutdown(clk);

	clockevents_config_and_register(clk, arch_timer_rate, 0xf, 0x7fffffff);
}

static void arch_timer_evtstrm_enable(int divider)
{
	u32 cntkctl = arch_timer_get_cntkctl();

	cntkctl &= ~ARCH_TIMER_EVT_TRIGGER_MASK;
	/* Set the divider and enable virtual event stream */
	cntkctl |= (divider << ARCH_TIMER_EVT_TRIGGER_SHIFT)
			| ARCH_TIMER_VIRT_EVT_EN;
	arch_timer_set_cntkctl(cntkctl);
	elf_hwcap |= HWCAP_EVTSTRM;
#ifdef CONFIG_COMPAT
	compat_elf_hwcap |= COMPAT_HWCAP_EVTSTRM;
#endif
	cpumask_set_cpu(smp_processor_id(), &evtstrm_available);
}

static void arch_timer_configure_evtstream(void)
{
	int evt_stream_div, pos;

	/* Find the closest power of two to the divisor */
	evt_stream_div = arch_timer_rate / ARCH_TIMER_EVT_STREAM_FREQ;
	pos = fls(evt_stream_div);
	if (pos > 1 && !(evt_stream_div & (1 << (pos - 2))))
		pos--;
	/* enable event stream */
	arch_timer_evtstrm_enable(min(pos, 15));
}

static void arch_counter_set_user_access(void)
{
	u32 cntkctl = arch_timer_get_cntkctl();

	/* Disable user access to the timers and both counters */
	/* Also disable virtual event stream */
	cntkctl &= ~(ARCH_TIMER_USR_PT_ACCESS_EN
			| ARCH_TIMER_USR_VT_ACCESS_EN
		        | ARCH_TIMER_USR_VCT_ACCESS_EN
			| ARCH_TIMER_VIRT_EVT_EN
			| ARCH_TIMER_USR_PCT_ACCESS_EN);

	/*
	 * Enable user access to the virtual counter if it doesn't
	 * need to be workaround. The vdso may have been already
	 * disabled though.
	 */
	if (arch_timer_this_cpu_has_cntvct_wa())
		pr_info("CPU%d: Trapping CNTVCT access\n", smp_processor_id());
	else
		cntkctl |= ARCH_TIMER_USR_VCT_ACCESS_EN;

	arch_timer_set_cntkctl(cntkctl);
}

static bool arch_timer_has_nonsecure_ppi(void)
{
	return (arch_timer_uses_ppi == ARCH_TIMER_PHYS_SECURE_PPI &&
		arch_timer_ppi[ARCH_TIMER_PHYS_NONSECURE_PPI]);
}

static u32 check_ppi_trigger(int irq)
{
	u32 flags = irq_get_trigger_type(irq);

	if (flags != IRQF_TRIGGER_HIGH && flags != IRQF_TRIGGER_LOW) {
		pr_warn("WARNING: Invalid trigger for IRQ%d, assuming level low\n", irq);
		pr_warn("WARNING: Please fix your firmware\n");
		flags = IRQF_TRIGGER_LOW;
	}

	return flags;
}

static int arch_timer_starting_cpu(unsigned int cpu)
{
	struct clock_event_device *clk = this_cpu_ptr(arch_timer_evt);
	u32 flags;

	__arch_timer_setup(ARCH_TIMER_TYPE_CP15, clk);

	flags = check_ppi_trigger(arch_timer_ppi[arch_timer_uses_ppi]);
	enable_percpu_irq(arch_timer_ppi[arch_timer_uses_ppi], flags);

	if (arch_timer_has_nonsecure_ppi()) {
		flags = check_ppi_trigger(arch_timer_ppi[ARCH_TIMER_PHYS_NONSECURE_PPI]);
		enable_percpu_irq(arch_timer_ppi[ARCH_TIMER_PHYS_NONSECURE_PPI],
				  flags);
	}

	arch_counter_set_user_access();
	if (evtstrm_enable)
		arch_timer_configure_evtstream();

	return 0;
}

/*
 * For historical reasons, when probing with DT we use whichever (non-zero)
 * rate was probed first, and don't verify that others match. If the first node
 * probed has a clock-frequency property, this overrides the HW register.
 */
static void arch_timer_of_configure_rate(u32 rate, struct device_node *np)
{
	/* Who has more than one independent system counter? */
	if (arch_timer_rate)
		return;

	if (of_property_read_u32(np, "clock-frequency", &arch_timer_rate))
		arch_timer_rate = rate;

	/* Check the timer frequency. */
	if (arch_timer_rate == 0)
		pr_warn("frequency not available\n");
}

static void arch_timer_banner(unsigned type)
{
	pr_info("%s%s%s timer(s) running at %lu.%02luMHz (%s%s%s).\n",
		type & ARCH_TIMER_TYPE_CP15 ? "cp15" : "",
		type == (ARCH_TIMER_TYPE_CP15 | ARCH_TIMER_TYPE_MEM) ?
			" and " : "",
		type & ARCH_TIMER_TYPE_MEM ? "mmio" : "",
		(unsigned long)arch_timer_rate / 1000000,
		(unsigned long)(arch_timer_rate / 10000) % 100,
		type & ARCH_TIMER_TYPE_CP15 ?
			(arch_timer_uses_ppi == ARCH_TIMER_VIRT_PPI) ? "virt" : "phys" :
			"",
		type == (ARCH_TIMER_TYPE_CP15 | ARCH_TIMER_TYPE_MEM) ? "/" : "",
		type & ARCH_TIMER_TYPE_MEM ?
			arch_timer_mem_use_virtual ? "virt" : "phys" :
			"");
}

u32 arch_timer_get_rate(void)
{
	return arch_timer_rate;
}
EXPORT_SYMBOL_GPL(arch_timer_get_rate);

bool arch_timer_evtstrm_available(void)
{
	/*
	 * We might get called from a preemptible context. This is fine
	 * because availability of the event stream should be always the same
	 * for a preemptible context and context where we might resume a task.
	 */
	return cpumask_test_cpu(raw_smp_processor_id(), &evtstrm_available);
}

void arch_timer_mem_get_cval(u32 *lo, u32 *hi)
{
	u32 ctrl;

	*lo = *hi = ~0U;

	if (!arch_counter_base)
		return;

	ctrl = readl_relaxed(arch_counter_base + CNTV_CTL);

	if (ctrl & ARCH_TIMER_CTRL_ENABLE) {
		*lo = readl_relaxed(arch_counter_base + CNTCVAL_LO);
		*hi = readl_relaxed(arch_counter_base + CNTCVAL_HI);
	}
}
EXPORT_SYMBOL_GPL(arch_timer_mem_get_cval);

static u64 arch_counter_get_cntvct_mem(void)
{
	u32 vct_lo, vct_hi, tmp_hi;

	do {
		vct_hi = readl_relaxed(arch_counter_base + CNTVCT_HI);
		vct_lo = readl_relaxed(arch_counter_base + CNTVCT_LO);
		tmp_hi = readl_relaxed(arch_counter_base + CNTVCT_HI);
	} while (vct_hi != tmp_hi);

	return ((u64) vct_hi << 32) | vct_lo;
}

static struct arch_timer_kvm_info arch_timer_kvm_info;

struct arch_timer_kvm_info *arch_timer_get_kvm_info(void)
{
	return &arch_timer_kvm_info;
}

static void __init arch_counter_register(unsigned type)
{
	u64 start_count;

	/* Register the CP15 based counter if we have one */
	if (type & ARCH_TIMER_TYPE_CP15) {
		if ((IS_ENABLED(CONFIG_ARM64) && !is_hyp_mode_available()) ||
		    arch_timer_uses_ppi == ARCH_TIMER_VIRT_PPI)
			arch_timer_read_counter = arch_counter_get_cntvct;
		else
			arch_timer_read_counter = arch_counter_get_cntpct;

		clocksource_counter.archdata.vdso_direct = vdso_default;
	} else {
		arch_timer_read_counter = arch_counter_get_cntvct_mem;
	}

	if (!arch_counter_suspend_stop)
		clocksource_counter.flags |= CLOCK_SOURCE_SUSPEND_NONSTOP;
	start_count = arch_timer_read_counter();
	clocksource_register_hz(&clocksource_counter, arch_timer_rate);
	cyclecounter.mult = clocksource_counter.mult;
	cyclecounter.shift = clocksource_counter.shift;
	timecounter_init(&arch_timer_kvm_info.timecounter,
			 &cyclecounter, start_count);

	/* 56 bits minimum, so we assume worst case rollover */
	sched_clock_register(arch_timer_read_counter, 56, arch_timer_rate);
}

static void arch_timer_stop(struct clock_event_device *clk)
{
	pr_debug("disable IRQ%d cpu #%d\n", clk->irq, smp_processor_id());

	disable_percpu_irq(arch_timer_ppi[arch_timer_uses_ppi]);
	if (arch_timer_has_nonsecure_ppi())
		disable_percpu_irq(arch_timer_ppi[ARCH_TIMER_PHYS_NONSECURE_PPI]);

	clk->set_state_shutdown(clk);
}

static int arch_timer_dying_cpu(unsigned int cpu)
{
	struct clock_event_device *clk = this_cpu_ptr(arch_timer_evt);

	cpumask_clear_cpu(smp_processor_id(), &evtstrm_available);

	arch_timer_stop(clk);
	return 0;
}

#ifdef CONFIG_CPU_PM
static DEFINE_PER_CPU(unsigned long, saved_cntkctl);
static int arch_timer_cpu_pm_notify(struct notifier_block *self,
				    unsigned long action, void *hcpu)
{
	if (action == CPU_PM_ENTER) {
		__this_cpu_write(saved_cntkctl, arch_timer_get_cntkctl());

		cpumask_clear_cpu(smp_processor_id(), &evtstrm_available);
	} else if (action == CPU_PM_ENTER_FAILED || action == CPU_PM_EXIT) {
		arch_timer_set_cntkctl(__this_cpu_read(saved_cntkctl));

		if (elf_hwcap & HWCAP_EVTSTRM)
			cpumask_set_cpu(smp_processor_id(), &evtstrm_available);
	}
	return NOTIFY_OK;
}

static struct notifier_block arch_timer_cpu_pm_notifier = {
	.notifier_call = arch_timer_cpu_pm_notify,
};

static int __init arch_timer_cpu_pm_init(void)
{
	return cpu_pm_register_notifier(&arch_timer_cpu_pm_notifier);
}

static void __init arch_timer_cpu_pm_deinit(void)
{
	WARN_ON(cpu_pm_unregister_notifier(&arch_timer_cpu_pm_notifier));
}

#else
static int __init arch_timer_cpu_pm_init(void)
{
	return 0;
}

static void __init arch_timer_cpu_pm_deinit(void)
{
}
#endif

static int __init arch_timer_register(void)
{
	int err;
	int ppi;

	arch_timer_evt = alloc_percpu(struct clock_event_device);
	if (!arch_timer_evt) {
		err = -ENOMEM;
		goto out;
	}

	ppi = arch_timer_ppi[arch_timer_uses_ppi];
	switch (arch_timer_uses_ppi) {
	case ARCH_TIMER_VIRT_PPI:
		err = request_percpu_irq(ppi, arch_timer_handler_virt,
					 "arch_timer", arch_timer_evt);
		break;
	case ARCH_TIMER_PHYS_SECURE_PPI:
	case ARCH_TIMER_PHYS_NONSECURE_PPI:
		err = request_percpu_irq(ppi, arch_timer_handler_phys,
					 "arch_timer", arch_timer_evt);
		if (!err && arch_timer_has_nonsecure_ppi()) {
			ppi = arch_timer_ppi[ARCH_TIMER_PHYS_NONSECURE_PPI];
			err = request_percpu_irq(ppi, arch_timer_handler_phys,
						 "arch_timer", arch_timer_evt);
			if (err)
				free_percpu_irq(arch_timer_ppi[ARCH_TIMER_PHYS_SECURE_PPI],
						arch_timer_evt);
		}
		break;
	case ARCH_TIMER_HYP_PPI:
		err = request_percpu_irq(ppi, arch_timer_handler_phys,
					 "arch_timer", arch_timer_evt);
		break;
	default:
		BUG();
	}

	if (err) {
		pr_err("can't register interrupt %d (%d)\n", ppi, err);
		goto out_free;
	}

	err = arch_timer_cpu_pm_init();
	if (err)
		goto out_unreg_notify;

	/* Register and immediately configure the timer on the boot CPU */
	err = cpuhp_setup_state(CPUHP_AP_ARM_ARCH_TIMER_STARTING,
				"clockevents/arm/arch_timer:starting",
				arch_timer_starting_cpu, arch_timer_dying_cpu);
	if (err)
		goto out_unreg_cpupm;
	return 0;

out_unreg_cpupm:
	arch_timer_cpu_pm_deinit();

out_unreg_notify:
	free_percpu_irq(arch_timer_ppi[arch_timer_uses_ppi], arch_timer_evt);
	if (arch_timer_has_nonsecure_ppi())
		free_percpu_irq(arch_timer_ppi[ARCH_TIMER_PHYS_NONSECURE_PPI],
				arch_timer_evt);

out_free:
	free_percpu(arch_timer_evt);
out:
	return err;
}

static int __init arch_timer_mem_register(void __iomem *base, unsigned int irq)
{
	int ret;
	irq_handler_t func;
	struct arch_timer *t;

	t = kzalloc(sizeof(*t), GFP_KERNEL);
	if (!t)
		return -ENOMEM;

	t->base = base;
	t->evt.irq = irq;
	__arch_timer_setup(ARCH_TIMER_TYPE_MEM, &t->evt);

	if (arch_timer_mem_use_virtual)
		func = arch_timer_handler_virt_mem;
	else
		func = arch_timer_handler_phys_mem;

	ret = request_irq(irq, func, IRQF_TIMER, "arch_mem_timer", &t->evt);
	if (ret) {
		pr_err("Failed to request mem timer irq\n");
		kfree(t);
	}

	return ret;
}

static const struct of_device_id arch_timer_of_match[] __initconst = {
	{ .compatible   = "arm,armv7-timer",    },
	{ .compatible   = "arm,armv8-timer",    },
	{},
};

static const struct of_device_id arch_timer_mem_of_match[] __initconst = {
	{ .compatible   = "arm,armv7-timer-mem", },
	{},
};

static bool __init arch_timer_needs_of_probing(void)
{
	struct device_node *dn;
	bool needs_probing = false;
	unsigned int mask = ARCH_TIMER_TYPE_CP15 | ARCH_TIMER_TYPE_MEM;

	/* We have two timers, and both device-tree nodes are probed. */
	if ((arch_timers_present & mask) == mask)
		return false;

	/*
	 * Only one type of timer is probed,
	 * check if we have another type of timer node in device-tree.
	 */
	if (arch_timers_present & ARCH_TIMER_TYPE_CP15)
		dn = of_find_matching_node(NULL, arch_timer_mem_of_match);
	else
		dn = of_find_matching_node(NULL, arch_timer_of_match);

	if (dn && of_device_is_available(dn))
		needs_probing = true;

	of_node_put(dn);

	return needs_probing;
}

static int __init arch_timer_common_init(void)
{
	arch_timer_banner(arch_timers_present);
	arch_counter_register(arch_timers_present);
	return arch_timer_arch_init();
}

/**
 * arch_timer_select_ppi() - Select suitable PPI for the current system.
 *
 * If HYP mode is available, we know that the physical timer
 * has been configured to be accessible from PL1. Use it, so
 * that a guest can use the virtual timer instead.
 *
 * On ARMv8.1 with VH extensions, the kernel runs in HYP. VHE
 * accesses to CNTP_*_EL1 registers are silently redirected to
 * their CNTHP_*_EL2 counterparts, and use a different PPI
 * number.
 *
 * If no interrupt provided for virtual timer, we'll have to
 * stick to the physical timer. It'd better be accessible...
 * For arm64 we never use the secure interrupt.
 *
 * Return: a suitable PPI type for the current system.
 */
static enum arch_timer_ppi_nr __init arch_timer_select_ppi(void)
{
	if (is_kernel_in_hyp_mode())
		return ARCH_TIMER_HYP_PPI;

	if (!is_hyp_mode_available() && arch_timer_ppi[ARCH_TIMER_VIRT_PPI])
		return ARCH_TIMER_VIRT_PPI;

	if (IS_ENABLED(CONFIG_ARM64))
		return ARCH_TIMER_PHYS_NONSECURE_PPI;

	return ARCH_TIMER_PHYS_SECURE_PPI;
}

static int __init arch_timer_of_init(struct device_node *np)
{
	int i, ret;
	u32 rate;

	if (arch_timers_present & ARCH_TIMER_TYPE_CP15) {
		pr_warn("multiple nodes in dt, skipping\n");
		return 0;
	}

	arch_timers_present |= ARCH_TIMER_TYPE_CP15;
	for (i = ARCH_TIMER_PHYS_SECURE_PPI; i < ARCH_TIMER_MAX_TIMER_PPI; i++)
		arch_timer_ppi[i] = irq_of_parse_and_map(np, i);

	arch_timer_kvm_info.virtual_irq = arch_timer_ppi[ARCH_TIMER_VIRT_PPI];

	rate = arch_timer_get_cntfrq();
	arch_timer_of_configure_rate(rate, np);

	arch_timer_c3stop = !of_property_read_bool(np, "always-on");

	/* Check for globally applicable workarounds */
	arch_timer_check_ool_workaround(ate_match_dt, np);

	/*
	 * If we cannot rely on firmware initializing the timer registers then
	 * we should use the physical timers instead.
	 */
	if (IS_ENABLED(CONFIG_ARM) &&
	    of_property_read_bool(np, "arm,cpu-registers-not-fw-configured"))
		arch_timer_uses_ppi = ARCH_TIMER_PHYS_SECURE_PPI;
	else
		arch_timer_uses_ppi = arch_timer_select_ppi();

	if (!arch_timer_ppi[arch_timer_uses_ppi]) {
		pr_err("No interrupt available, giving up\n");
		return -EINVAL;
	}

	/* On some systems, the counter stops ticking when in suspend. */
	arch_counter_suspend_stop = of_property_read_bool(np,
							 "arm,no-tick-in-suspend");

	ret = arch_timer_register();
	if (ret)
		return ret;

	if (arch_timer_needs_of_probing())
		return 0;

	return arch_timer_common_init();
}
TIMER_OF_DECLARE(armv7_arch_timer, "arm,armv7-timer", arch_timer_of_init);
TIMER_OF_DECLARE(armv8_arch_timer, "arm,armv8-timer", arch_timer_of_init);

static u32 __init
arch_timer_mem_frame_get_cntfrq(struct arch_timer_mem_frame *frame)
{
	void __iomem *base;
	u32 rate;

	base = ioremap(frame->cntbase, frame->size);
	if (!base) {
		pr_err("Unable to map frame @ %pa\n", &frame->cntbase);
		return 0;
	}

	rate = readl_relaxed(base + CNTFRQ);

	iounmap(base);

	return rate;
}

static struct arch_timer_mem_frame * __init
arch_timer_mem_find_best_frame(struct arch_timer_mem *timer_mem)
{
	struct arch_timer_mem_frame *frame, *best_frame = NULL;
	void __iomem *cntctlbase;
	u32 cnttidr;
	int i;

	cntctlbase = ioremap(timer_mem->cntctlbase, timer_mem->size);
	if (!cntctlbase) {
		pr_err("Can't map CNTCTLBase @ %pa\n",
			&timer_mem->cntctlbase);
		return NULL;
	}

	cnttidr = readl_relaxed(cntctlbase + CNTTIDR);

	/*
	 * Try to find a virtual capable frame. Otherwise fall back to a
	 * physical capable frame.
	 */
	for (i = 0; i < ARCH_TIMER_MEM_MAX_FRAMES; i++) {
		u32 cntacr = CNTACR_RFRQ | CNTACR_RWPT | CNTACR_RPCT |
			     CNTACR_RWVT | CNTACR_RVOFF | CNTACR_RVCT;

		frame = &timer_mem->frame[i];
		if (!frame->valid)
			continue;

		/* Try enabling everything, and see what sticks */
		writel_relaxed(cntacr, cntctlbase + CNTACR(i));
		cntacr = readl_relaxed(cntctlbase + CNTACR(i));

		if ((cnttidr & CNTTIDR_VIRT(i)) &&
		    !(~cntacr & (CNTACR_RWVT | CNTACR_RVCT))) {
			best_frame = frame;
			arch_timer_mem_use_virtual = true;
			break;
		}

		if (~cntacr & (CNTACR_RWPT | CNTACR_RPCT))
			continue;

		best_frame = frame;
	}

	iounmap(cntctlbase);

	return best_frame;
}

static int __init
arch_timer_mem_frame_register(struct arch_timer_mem_frame *frame)
{
	void __iomem *base;
	int ret, irq = 0;

	if (arch_timer_mem_use_virtual)
		irq = frame->virt_irq;
	else
		irq = frame->phys_irq;

	if (!irq) {
		pr_err("Frame missing %s irq.\n",
		       arch_timer_mem_use_virtual ? "virt" : "phys");
		return -EINVAL;
	}

	if (!request_mem_region(frame->cntbase, frame->size,
				"arch_mem_timer"))
		return -EBUSY;

	base = ioremap(frame->cntbase, frame->size);
	if (!base) {
		pr_err("Can't map frame's registers\n");
		return -ENXIO;
	}

	ret = arch_timer_mem_register(base, irq);
	if (ret) {
		iounmap(base);
		return ret;
	}

	arch_counter_base = base;
	arch_timers_present |= ARCH_TIMER_TYPE_MEM;

	return 0;
}

static int __init arch_timer_mem_of_init(struct device_node *np)
{
	struct arch_timer_mem *timer_mem;
	struct arch_timer_mem_frame *frame;
	struct device_node *frame_node;
	struct resource res;
	int ret = -EINVAL;
	u32 rate;

	timer_mem = kzalloc(sizeof(*timer_mem), GFP_KERNEL);
	if (!timer_mem)
		return -ENOMEM;

	if (of_address_to_resource(np, 0, &res))
		goto out;
	timer_mem->cntctlbase = res.start;
	timer_mem->size = resource_size(&res);

	for_each_available_child_of_node(np, frame_node) {
		u32 n;
		struct arch_timer_mem_frame *frame;

		if (of_property_read_u32(frame_node, "frame-number", &n)) {
			pr_err(FW_BUG "Missing frame-number.\n");
			of_node_put(frame_node);
			goto out;
		}
		if (n >= ARCH_TIMER_MEM_MAX_FRAMES) {
			pr_err(FW_BUG "Wrong frame-number, only 0-%u are permitted.\n",
			       ARCH_TIMER_MEM_MAX_FRAMES - 1);
			of_node_put(frame_node);
			goto out;
		}
		frame = &timer_mem->frame[n];

		if (frame->valid) {
			pr_err(FW_BUG "Duplicated frame-number.\n");
			of_node_put(frame_node);
			goto out;
		}

		if (of_address_to_resource(frame_node, 0, &res)) {
			of_node_put(frame_node);
			goto out;
		}
		frame->cntbase = res.start;
		frame->size = resource_size(&res);

		frame->virt_irq = irq_of_parse_and_map(frame_node,
						       ARCH_TIMER_VIRT_SPI);
		frame->phys_irq = irq_of_parse_and_map(frame_node,
						       ARCH_TIMER_PHYS_SPI);

		frame->valid = true;
	}

	frame = arch_timer_mem_find_best_frame(timer_mem);
	if (!frame) {
		pr_err("Unable to find a suitable frame in timer @ %pa\n",
			&timer_mem->cntctlbase);
		ret = -EINVAL;
		goto out;
	}

	rate = arch_timer_mem_frame_get_cntfrq(frame);
	arch_timer_of_configure_rate(rate, np);

	ret = arch_timer_mem_frame_register(frame);
	if (!ret && !arch_timer_needs_of_probing())
		ret = arch_timer_common_init();
out:
	kfree(timer_mem);
	return ret;
}
TIMER_OF_DECLARE(armv7_arch_timer_mem, "arm,armv7-timer-mem",
		       arch_timer_mem_of_init);

#ifdef CONFIG_ACPI_GTDT
static int __init
arch_timer_mem_verify_cntfrq(struct arch_timer_mem *timer_mem)
{
	struct arch_timer_mem_frame *frame;
	u32 rate;
	int i;

	for (i = 0; i < ARCH_TIMER_MEM_MAX_FRAMES; i++) {
		frame = &timer_mem->frame[i];

		if (!frame->valid)
			continue;

		rate = arch_timer_mem_frame_get_cntfrq(frame);
		if (rate == arch_timer_rate)
			continue;

		pr_err(FW_BUG "CNTFRQ mismatch: frame @ %pa: (0x%08lx), CPU: (0x%08lx)\n",
			&frame->cntbase,
			(unsigned long)rate, (unsigned long)arch_timer_rate);

		return -EINVAL;
	}

	return 0;
}

static int __init arch_timer_mem_acpi_init(int platform_timer_count)
{
	struct arch_timer_mem *timers, *timer;
	struct arch_timer_mem_frame *frame, *best_frame = NULL;
	int timer_count, i, ret = 0;

	timers = kcalloc(platform_timer_count, sizeof(*timers),
			    GFP_KERNEL);
	if (!timers)
		return -ENOMEM;

	ret = acpi_arch_timer_mem_init(timers, &timer_count);
	if (ret || !timer_count)
		goto out;

	/*
	 * While unlikely, it's theoretically possible that none of the frames
	 * in a timer expose the combination of feature we want.
	 */
	for (i = 0; i < timer_count; i++) {
		timer = &timers[i];

		frame = arch_timer_mem_find_best_frame(timer);
		if (!best_frame)
			best_frame = frame;

		ret = arch_timer_mem_verify_cntfrq(timer);
		if (ret) {
			pr_err("Disabling MMIO timers due to CNTFRQ mismatch\n");
			goto out;
		}

		if (!best_frame) /* implies !frame */
			/*
			 * Only complain about missing suitable frames if we
			 * haven't already found one in a previous iteration.
			 */
			pr_err("Unable to find a suitable frame in timer @ %pa\n",
				&timer->cntctlbase);
	}

	if (best_frame)
		ret = arch_timer_mem_frame_register(best_frame);
out:
	kfree(timers);
	return ret;
}

/* Initialize per-processor generic timer and memory-mapped timer(if present) */
static int __init arch_timer_acpi_init(struct acpi_table_header *table)
{
	int ret, platform_timer_count;

	if (arch_timers_present & ARCH_TIMER_TYPE_CP15) {
		pr_warn("already initialized, skipping\n");
		return -EINVAL;
	}

	arch_timers_present |= ARCH_TIMER_TYPE_CP15;

	ret = acpi_gtdt_init(table, &platform_timer_count);
	if (ret) {
		pr_err("Failed to init GTDT table.\n");
		return ret;
	}

	arch_timer_ppi[ARCH_TIMER_PHYS_NONSECURE_PPI] =
		acpi_gtdt_map_ppi(ARCH_TIMER_PHYS_NONSECURE_PPI);

	arch_timer_ppi[ARCH_TIMER_VIRT_PPI] =
		acpi_gtdt_map_ppi(ARCH_TIMER_VIRT_PPI);

	arch_timer_ppi[ARCH_TIMER_HYP_PPI] =
		acpi_gtdt_map_ppi(ARCH_TIMER_HYP_PPI);

	arch_timer_kvm_info.virtual_irq = arch_timer_ppi[ARCH_TIMER_VIRT_PPI];

	/*
	 * When probing via ACPI, we have no mechanism to override the sysreg
	 * CNTFRQ value. This *must* be correct.
	 */
	arch_timer_rate = arch_timer_get_cntfrq();
	if (!arch_timer_rate) {
		pr_err(FW_BUG "frequency not available.\n");
		return -EINVAL;
	}

	arch_timer_uses_ppi = arch_timer_select_ppi();
	if (!arch_timer_ppi[arch_timer_uses_ppi]) {
		pr_err("No interrupt available, giving up\n");
		return -EINVAL;
	}

	/* Always-on capability */
	arch_timer_c3stop = acpi_gtdt_c3stop(arch_timer_uses_ppi);

	/* Check for globally applicable workarounds */
	arch_timer_check_ool_workaround(ate_match_acpi_oem_info, table);

	ret = arch_timer_register();
	if (ret)
		return ret;

	if (platform_timer_count &&
	    arch_timer_mem_acpi_init(platform_timer_count))
		pr_err("Failed to initialize memory-mapped timer.\n");

	return arch_timer_common_init();
}
TIMER_ACPI_DECLARE(arch_timer, ACPI_SIG_GTDT, arch_timer_acpi_init);
#endif
