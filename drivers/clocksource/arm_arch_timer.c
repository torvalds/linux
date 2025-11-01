// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/drivers/clocksource/arm_arch_timer.c
 *
 *  Copyright (C) 2011 ARM Ltd.
 *  All Rights Reserved
 */

#define pr_fmt(fmt) 	"arch_timer: " fmt

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/smp.h>
#include <linux/cpu.h>
#include <linux/cpu_pm.h>
#include <linux/clockchips.h>
#include <linux/clocksource.h>
#include <linux/clocksource_ids.h>
#include <linux/interrupt.h>
#include <linux/kstrtox.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/sched/clock.h>
#include <linux/sched_clock.h>
#include <linux/acpi.h>
#include <linux/arm-smccc.h>
#include <linux/ptp_kvm.h>

#include <asm/arch_timer.h>
#include <asm/virt.h>

#include <clocksource/arm_arch_timer.h>

/*
 * The minimum amount of time a generic counter is guaranteed to not roll over
 * (40 years)
 */
#define MIN_ROLLOVER_SECS	(40ULL * 365 * 24 * 3600)

static u32 arch_timer_rate __ro_after_init;
static int arch_timer_ppi[ARCH_TIMER_MAX_TIMER_PPI] __ro_after_init;

static const char *arch_timer_ppi_names[ARCH_TIMER_MAX_TIMER_PPI] = {
	[ARCH_TIMER_PHYS_SECURE_PPI]	= "sec-phys",
	[ARCH_TIMER_PHYS_NONSECURE_PPI]	= "phys",
	[ARCH_TIMER_VIRT_PPI]		= "virt",
	[ARCH_TIMER_HYP_PPI]		= "hyp-phys",
	[ARCH_TIMER_HYP_VIRT_PPI]	= "hyp-virt",
};

static struct clock_event_device __percpu *arch_timer_evt;

static enum arch_timer_ppi_nr arch_timer_uses_ppi __ro_after_init = ARCH_TIMER_VIRT_PPI;
static bool arch_timer_c3stop __ro_after_init;
static bool arch_counter_suspend_stop __ro_after_init;
#ifdef CONFIG_GENERIC_GETTIMEOFDAY
static enum vdso_clock_mode vdso_default = VDSO_CLOCKMODE_ARCHTIMER;
#else
static enum vdso_clock_mode vdso_default = VDSO_CLOCKMODE_NONE;
#endif /* CONFIG_GENERIC_GETTIMEOFDAY */

static cpumask_t evtstrm_available = CPU_MASK_NONE;
static bool evtstrm_enable __ro_after_init = IS_ENABLED(CONFIG_ARM_ARCH_TIMER_EVTSTREAM);

static int __init early_evtstrm_cfg(char *buf)
{
	return kstrtobool(buf, &evtstrm_enable);
}
early_param("clocksource.arm_arch_timer.evtstrm", early_evtstrm_cfg);

/*
 * Makes an educated guess at a valid counter width based on the Generic Timer
 * specification. Of note:
 *   1) the system counter is at least 56 bits wide
 *   2) a roll-over time of not less than 40 years
 *
 * See 'ARM DDI 0487G.a D11.1.2 ("The system counter")' for more details.
 */
static int arch_counter_get_width(void)
{
	u64 min_cycles = MIN_ROLLOVER_SECS * arch_timer_rate;

	/* guarantee the returned width is within the valid range */
	return clamp_val(ilog2(min_cycles - 1) + 1, 56, 64);
}

/*
 * Architected system timer support.
 */
static noinstr u64 raw_counter_get_cntpct_stable(void)
{
	return __arch_counter_get_cntpct_stable();
}

static notrace u64 arch_counter_get_cntpct_stable(void)
{
	u64 val;
	preempt_disable_notrace();
	val = __arch_counter_get_cntpct_stable();
	preempt_enable_notrace();
	return val;
}

static noinstr u64 arch_counter_get_cntpct(void)
{
	return __arch_counter_get_cntpct();
}

static noinstr u64 raw_counter_get_cntvct_stable(void)
{
	return __arch_counter_get_cntvct_stable();
}

static notrace u64 arch_counter_get_cntvct_stable(void)
{
	u64 val;
	preempt_disable_notrace();
	val = __arch_counter_get_cntvct_stable();
	preempt_enable_notrace();
	return val;
}

static noinstr u64 arch_counter_get_cntvct(void)
{
	return __arch_counter_get_cntvct();
}

/*
 * Default to cp15 based access because arm64 uses this function for
 * sched_clock() before DT is probed and the cp15 method is guaranteed
 * to exist on arm64. arm doesn't use this before DT is probed so even
 * if we don't have the cp15 accessors we won't have a problem.
 */
u64 (*arch_timer_read_counter)(void) __ro_after_init = arch_counter_get_cntvct;
EXPORT_SYMBOL_GPL(arch_timer_read_counter);

static u64 arch_counter_read(struct clocksource *cs)
{
	return arch_timer_read_counter();
}

static u64 arch_counter_read_cc(struct cyclecounter *cc)
{
	return arch_timer_read_counter();
}

static struct clocksource clocksource_counter = {
	.name	= "arch_sys_counter",
	.id	= CSID_ARM_ARCH_COUNTER,
	.rating	= 400,
	.read	= arch_counter_read,
	.flags	= CLOCK_SOURCE_IS_CONTINUOUS,
};

static struct cyclecounter cyclecounter __ro_after_init = {
	.read	= arch_counter_read_cc,
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

static u64 notrace hisi_161010101_read_cntpct_el0(void)
{
	return __hisi_161010101_read_reg(cntpct_el0);
}

static u64 notrace hisi_161010101_read_cntvct_el0(void)
{
	return __hisi_161010101_read_reg(cntvct_el0);
}

static const struct ate_acpi_oem_info hisi_161010101_oem_info[] = {
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
	} while (((_val + 1) & GENMASK(8, 0)) <= 1 && _retries);	\
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
#endif

#ifdef CONFIG_ARM_ARCH_TIMER_OOL_WORKAROUND
DEFINE_PER_CPU(const struct arch_timer_erratum_workaround *, timer_unstable_counter_workaround);
EXPORT_SYMBOL_GPL(timer_unstable_counter_workaround);

static atomic_t timer_unstable_counter_workaround_in_use = ATOMIC_INIT(0);

/*
 * Force the inlining of this function so that the register accesses
 * can be themselves correctly inlined.
 */
static __always_inline
void erratum_set_next_event_generic(const int access, unsigned long evt,
				    struct clock_event_device *clk)
{
	unsigned long ctrl;
	u64 cval;

	ctrl = arch_timer_reg_read_cp15(access, ARCH_TIMER_REG_CTRL);
	ctrl |= ARCH_TIMER_CTRL_ENABLE;
	ctrl &= ~ARCH_TIMER_CTRL_IT_MASK;

	if (access == ARCH_TIMER_PHYS_ACCESS) {
		cval = evt + arch_counter_get_cntpct_stable();
		write_sysreg(cval, cntp_cval_el0);
	} else {
		cval = evt + arch_counter_get_cntvct_stable();
		write_sysreg(cval, cntv_cval_el0);
	}

	arch_timer_reg_write_cp15(access, ARCH_TIMER_REG_CTRL, ctrl);
}

static __maybe_unused int erratum_set_next_event_virt(unsigned long evt,
					    struct clock_event_device *clk)
{
	erratum_set_next_event_generic(ARCH_TIMER_VIRT_ACCESS, evt, clk);
	return 0;
}

static __maybe_unused int erratum_set_next_event_phys(unsigned long evt,
					    struct clock_event_device *clk)
{
	erratum_set_next_event_generic(ARCH_TIMER_PHYS_ACCESS, evt, clk);
	return 0;
}

static const struct arch_timer_erratum_workaround ool_workarounds[] = {
#ifdef CONFIG_FSL_ERRATUM_A008585
	{
		.match_type = ate_match_dt,
		.id = "fsl,erratum-a008585",
		.desc = "Freescale erratum a005858",
		.read_cntpct_el0 = fsl_a008585_read_cntpct_el0,
		.read_cntvct_el0 = fsl_a008585_read_cntvct_el0,
		.set_next_event_phys = erratum_set_next_event_phys,
		.set_next_event_virt = erratum_set_next_event_virt,
	},
#endif
#ifdef CONFIG_HISILICON_ERRATUM_161010101
	{
		.match_type = ate_match_dt,
		.id = "hisilicon,erratum-161010101",
		.desc = "HiSilicon erratum 161010101",
		.read_cntpct_el0 = hisi_161010101_read_cntpct_el0,
		.read_cntvct_el0 = hisi_161010101_read_cntvct_el0,
		.set_next_event_phys = erratum_set_next_event_phys,
		.set_next_event_virt = erratum_set_next_event_virt,
	},
	{
		.match_type = ate_match_acpi_oem_info,
		.id = hisi_161010101_oem_info,
		.desc = "HiSilicon erratum 161010101",
		.read_cntpct_el0 = hisi_161010101_read_cntpct_el0,
		.read_cntvct_el0 = hisi_161010101_read_cntvct_el0,
		.set_next_event_phys = erratum_set_next_event_phys,
		.set_next_event_virt = erratum_set_next_event_virt,
	},
#endif
#ifdef CONFIG_ARM64_ERRATUM_858921
	{
		.match_type = ate_match_local_cap_id,
		.id = (void *)ARM64_WORKAROUND_858921,
		.desc = "ARM erratum 858921",
		.read_cntpct_el0 = arm64_858921_read_cntpct_el0,
		.read_cntvct_el0 = arm64_858921_read_cntvct_el0,
		.set_next_event_phys = erratum_set_next_event_phys,
		.set_next_event_virt = erratum_set_next_event_virt,
	},
#endif
#ifdef CONFIG_SUN50I_ERRATUM_UNKNOWN1
	{
		.match_type = ate_match_dt,
		.id = "allwinner,erratum-unknown1",
		.desc = "Allwinner erratum UNKNOWN1",
		.read_cntpct_el0 = sun50i_a64_read_cntpct_el0,
		.read_cntvct_el0 = sun50i_a64_read_cntvct_el0,
		.set_next_event_phys = erratum_set_next_event_phys,
		.set_next_event_virt = erratum_set_next_event_virt,
	},
#endif
#ifdef CONFIG_ARM64_ERRATUM_1418040
	{
		.match_type = ate_match_local_cap_id,
		.id = (void *)ARM64_WORKAROUND_1418040,
		.desc = "ARM erratum 1418040",
		.disable_compat_vdso = true,
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

	if (wa->read_cntvct_el0 || wa->read_cntpct_el0)
		atomic_set(&timer_unstable_counter_workaround_in_use, 1);

	/*
	 * Don't use the vdso fastpath if errata require using the
	 * out-of-line counter accessor. We may change our mind pretty
	 * late in the game (with a per-CPU erratum, for example), so
	 * change both the default value and the vdso itself.
	 */
	if (wa->read_cntvct_el0) {
		clocksource_counter.vdso_clock_mode = VDSO_CLOCKMODE_NONE;
		vdso_default = VDSO_CLOCKMODE_NONE;
	} else if (wa->disable_compat_vdso && vdso_default != VDSO_CLOCKMODE_NONE) {
		vdso_default = VDSO_CLOCKMODE_ARCHTIMER_NOCOMPAT;
		clocksource_counter.vdso_clock_mode = vdso_default;
	}
}

static void arch_timer_check_ool_workaround(enum arch_timer_erratum_match_type type,
					    void *arg)
{
	const struct arch_timer_erratum_workaround *wa, *__wa;
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

	__wa = __this_cpu_read(timer_unstable_counter_workaround);
	if (__wa && wa != __wa)
		pr_warn("Can't enable workaround for %s (clashes with %s\n)",
			wa->desc, __wa->desc);

	if (__wa)
		return;

	arch_timer_enable_workaround(wa, local);
	pr_info("Enabling %s workaround for %s\n",
		local ? "local" : "global", wa->desc);
}

static bool arch_timer_this_cpu_has_cntvct_wa(void)
{
	return has_erratum_handler(read_cntvct_el0);
}

static bool arch_timer_counter_has_wa(void)
{
	return atomic_read(&timer_unstable_counter_workaround_in_use);
}
#else
#define arch_timer_check_ool_workaround(t,a)		do { } while(0)
#define arch_timer_this_cpu_has_cntvct_wa()		({false;})
#define arch_timer_counter_has_wa()			({false;})
#endif /* CONFIG_ARM_ARCH_TIMER_OOL_WORKAROUND */

static __always_inline irqreturn_t timer_handler(const int access,
					struct clock_event_device *evt)
{
	unsigned long ctrl;

	ctrl = arch_timer_reg_read_cp15(access, ARCH_TIMER_REG_CTRL);
	if (ctrl & ARCH_TIMER_CTRL_IT_STAT) {
		ctrl |= ARCH_TIMER_CTRL_IT_MASK;
		arch_timer_reg_write_cp15(access, ARCH_TIMER_REG_CTRL, ctrl);
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

static __always_inline int arch_timer_shutdown(const int access,
					       struct clock_event_device *clk)
{
	unsigned long ctrl;

	ctrl = arch_timer_reg_read_cp15(access, ARCH_TIMER_REG_CTRL);
	ctrl &= ~ARCH_TIMER_CTRL_ENABLE;
	arch_timer_reg_write_cp15(access, ARCH_TIMER_REG_CTRL, ctrl);

	return 0;
}

static int arch_timer_shutdown_virt(struct clock_event_device *clk)
{
	return arch_timer_shutdown(ARCH_TIMER_VIRT_ACCESS, clk);
}

static int arch_timer_shutdown_phys(struct clock_event_device *clk)
{
	return arch_timer_shutdown(ARCH_TIMER_PHYS_ACCESS, clk);
}

static __always_inline void set_next_event(const int access, unsigned long evt,
					   struct clock_event_device *clk)
{
	unsigned long ctrl;
	u64 cnt;

	ctrl = arch_timer_reg_read_cp15(access, ARCH_TIMER_REG_CTRL);
	ctrl |= ARCH_TIMER_CTRL_ENABLE;
	ctrl &= ~ARCH_TIMER_CTRL_IT_MASK;

	if (access == ARCH_TIMER_PHYS_ACCESS)
		cnt = __arch_counter_get_cntpct();
	else
		cnt = __arch_counter_get_cntvct();

	arch_timer_reg_write_cp15(access, ARCH_TIMER_REG_CVAL, evt + cnt);
	arch_timer_reg_write_cp15(access, ARCH_TIMER_REG_CTRL, ctrl);
}

static int arch_timer_set_next_event_virt(unsigned long evt,
					  struct clock_event_device *clk)
{
	set_next_event(ARCH_TIMER_VIRT_ACCESS, evt, clk);
	return 0;
}

static int arch_timer_set_next_event_phys(unsigned long evt,
					  struct clock_event_device *clk)
{
	set_next_event(ARCH_TIMER_PHYS_ACCESS, evt, clk);
	return 0;
}

static u64 __arch_timer_check_delta(void)
{
#ifdef CONFIG_ARM64
	const struct midr_range broken_cval_midrs[] = {
		/*
		 * XGene-1 implements CVAL in terms of TVAL, meaning
		 * that the maximum timer range is 32bit. Shame on them.
		 *
		 * Note that TVAL is signed, thus has only 31 of its
		 * 32 bits to express magnitude.
		 */
		MIDR_REV_RANGE(MIDR_CPU_MODEL(ARM_CPU_IMP_APM,
					      APM_CPU_PART_XGENE),
			       APM_CPU_VAR_POTENZA, 0x0, 0xf),
		{},
	};

	if (is_midr_in_range_list(broken_cval_midrs)) {
		pr_warn_once("Broken CNTx_CVAL_EL1, using 31 bit TVAL instead.\n");
		return CLOCKSOURCE_MASK(31);
	}
#endif
	return CLOCKSOURCE_MASK(arch_counter_get_width());
}

static void __arch_timer_setup(struct clock_event_device *clk)
{
	typeof(clk->set_next_event) sne;
	u64 max_delta;

	clk->features = CLOCK_EVT_FEAT_ONESHOT;

	arch_timer_check_ool_workaround(ate_match_local_cap_id, NULL);

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
		sne = erratum_handler(set_next_event_virt);
		break;
	case ARCH_TIMER_PHYS_SECURE_PPI:
	case ARCH_TIMER_PHYS_NONSECURE_PPI:
	case ARCH_TIMER_HYP_PPI:
		clk->set_state_shutdown = arch_timer_shutdown_phys;
		clk->set_state_oneshot_stopped = arch_timer_shutdown_phys;
		sne = erratum_handler(set_next_event_phys);
		break;
	default:
		BUG();
	}

	clk->set_next_event = sne;
	max_delta = __arch_timer_check_delta();

	clk->set_state_shutdown(clk);

	clockevents_config_and_register(clk, arch_timer_rate, 0xf, max_delta);
}

static void arch_timer_evtstrm_enable(unsigned int divider)
{
	u32 cntkctl = arch_timer_get_cntkctl();

#ifdef CONFIG_ARM64
	/* ECV is likely to require a large divider. Use the EVNTIS flag. */
	if (cpus_have_final_cap(ARM64_HAS_ECV) && divider > 15) {
		cntkctl |= ARCH_TIMER_EVT_INTERVAL_SCALE;
		divider -= 8;
	}
#endif

	divider = min(divider, 15U);
	cntkctl &= ~ARCH_TIMER_EVT_TRIGGER_MASK;
	/* Set the divider and enable virtual event stream */
	cntkctl |= (divider << ARCH_TIMER_EVT_TRIGGER_SHIFT)
			| ARCH_TIMER_VIRT_EVT_EN;
	arch_timer_set_cntkctl(cntkctl);
	arch_timer_set_evtstrm_feature();
	cpumask_set_cpu(smp_processor_id(), &evtstrm_available);
}

static void arch_timer_configure_evtstream(void)
{
	int evt_stream_div, lsb;

	/*
	 * As the event stream can at most be generated at half the frequency
	 * of the counter, use half the frequency when computing the divider.
	 */
	evt_stream_div = arch_timer_rate / ARCH_TIMER_EVT_STREAM_FREQ / 2;

	/*
	 * Find the closest power of two to the divisor. If the adjacent bit
	 * of lsb (last set bit, starts from 0) is set, then we use (lsb + 1).
	 */
	lsb = fls(evt_stream_div) - 1;
	if (lsb > 0 && (evt_stream_div & BIT(lsb - 1)))
		lsb++;

	/* enable event stream */
	arch_timer_evtstrm_enable(max(0, lsb));
}

static int arch_timer_evtstrm_starting_cpu(unsigned int cpu)
{
	arch_timer_configure_evtstream();
	return 0;
}

static int arch_timer_evtstrm_dying_cpu(unsigned int cpu)
{
	cpumask_clear_cpu(smp_processor_id(), &evtstrm_available);
	return 0;
}

static int __init arch_timer_evtstrm_register(void)
{
	if (!arch_timer_evt || !evtstrm_enable)
		return 0;

	return cpuhp_setup_state(CPUHP_AP_ARM_ARCH_TIMER_EVTSTRM_STARTING,
				 "clockevents/arm/arch_timer_evtstrm:starting",
				 arch_timer_evtstrm_starting_cpu,
				 arch_timer_evtstrm_dying_cpu);
}
core_initcall(arch_timer_evtstrm_register);

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

	__arch_timer_setup(clk);

	flags = check_ppi_trigger(arch_timer_ppi[arch_timer_uses_ppi]);
	enable_percpu_irq(arch_timer_ppi[arch_timer_uses_ppi], flags);

	if (arch_timer_has_nonsecure_ppi()) {
		flags = check_ppi_trigger(arch_timer_ppi[ARCH_TIMER_PHYS_NONSECURE_PPI]);
		enable_percpu_irq(arch_timer_ppi[ARCH_TIMER_PHYS_NONSECURE_PPI],
				  flags);
	}

	arch_counter_set_user_access();

	return 0;
}

static int validate_timer_rate(void)
{
	if (!arch_timer_rate)
		return -EINVAL;

	/* Arch timer frequency < 1MHz can cause trouble */
	WARN_ON(arch_timer_rate < 1000000);

	return 0;
}

/*
 * For historical reasons, when probing with DT we use whichever (non-zero)
 * rate was probed first, and don't verify that others match. If the first node
 * probed has a clock-frequency property, this overrides the HW register.
 */
static void __init arch_timer_of_configure_rate(u32 rate, struct device_node *np)
{
	/* Who has more than one independent system counter? */
	if (arch_timer_rate)
		return;

	if (of_property_read_u32(np, "clock-frequency", &arch_timer_rate))
		arch_timer_rate = rate;

	/* Check the timer frequency. */
	if (validate_timer_rate())
		pr_warn("frequency not available\n");
}

static void __init arch_timer_banner(void)
{
	pr_info("cp15 timer running at %lu.%02luMHz (%s).\n",
		(unsigned long)arch_timer_rate / 1000000,
		(unsigned long)(arch_timer_rate / 10000) % 100,
		(arch_timer_uses_ppi == ARCH_TIMER_VIRT_PPI) ? "virt" : "phys");
}

u32 arch_timer_get_rate(void)
{
	return arch_timer_rate;
}

bool arch_timer_evtstrm_available(void)
{
	/*
	 * We might get called from a preemptible context. This is fine
	 * because availability of the event stream should be always the same
	 * for a preemptible context and context where we might resume a task.
	 */
	return cpumask_test_cpu(raw_smp_processor_id(), &evtstrm_available);
}

static struct arch_timer_kvm_info arch_timer_kvm_info;

struct arch_timer_kvm_info *arch_timer_get_kvm_info(void)
{
	return &arch_timer_kvm_info;
}

static void __init arch_counter_register(void)
{
	u64 (*scr)(void);
	u64 (*rd)(void);
	u64 start_count;
	int width;

	if ((IS_ENABLED(CONFIG_ARM64) && !is_hyp_mode_available()) ||
	    arch_timer_uses_ppi == ARCH_TIMER_VIRT_PPI) {
		if (arch_timer_counter_has_wa()) {
			rd = arch_counter_get_cntvct_stable;
			scr = raw_counter_get_cntvct_stable;
		} else {
			rd = arch_counter_get_cntvct;
			scr = arch_counter_get_cntvct;
		}
	} else {
		if (arch_timer_counter_has_wa()) {
			rd = arch_counter_get_cntpct_stable;
			scr = raw_counter_get_cntpct_stable;
		} else {
			rd = arch_counter_get_cntpct;
			scr = arch_counter_get_cntpct;
		}
	}

	arch_timer_read_counter = rd;
	clocksource_counter.vdso_clock_mode = vdso_default;

	width = arch_counter_get_width();
	clocksource_counter.mask = CLOCKSOURCE_MASK(width);
	cyclecounter.mask = CLOCKSOURCE_MASK(width);

	if (!arch_counter_suspend_stop)
		clocksource_counter.flags |= CLOCK_SOURCE_SUSPEND_NONSTOP;
	start_count = arch_timer_read_counter();
	clocksource_register_hz(&clocksource_counter, arch_timer_rate);
	cyclecounter.mult = clocksource_counter.mult;
	cyclecounter.shift = clocksource_counter.shift;
	timecounter_init(&arch_timer_kvm_info.timecounter,
			 &cyclecounter, start_count);

	sched_clock_register(scr, width, arch_timer_rate);
}

static void arch_timer_stop(struct clock_event_device *clk)
{
	pr_debug("disable IRQ%d cpu #%d\n", clk->irq, smp_processor_id());

	disable_percpu_irq(arch_timer_ppi[arch_timer_uses_ppi]);
	if (arch_timer_has_nonsecure_ppi())
		disable_percpu_irq(arch_timer_ppi[ARCH_TIMER_PHYS_NONSECURE_PPI]);
}

static int arch_timer_dying_cpu(unsigned int cpu)
{
	struct clock_event_device *clk = this_cpu_ptr(arch_timer_evt);

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

		if (arch_timer_have_evtstrm_feature())
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
	arch_timer_evt = NULL;
out:
	return err;
}

static int __init arch_timer_common_init(void)
{
	arch_timer_banner();
	arch_counter_register();
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

static void __init arch_timer_populate_kvm_info(void)
{
	arch_timer_kvm_info.virtual_irq = arch_timer_ppi[ARCH_TIMER_VIRT_PPI];
	if (is_kernel_in_hyp_mode())
		arch_timer_kvm_info.physical_irq = arch_timer_ppi[ARCH_TIMER_PHYS_NONSECURE_PPI];
}

static int __init arch_timer_of_init(struct device_node *np)
{
	int i, irq, ret;
	u32 rate;
	bool has_names;

	if (arch_timer_evt) {
		pr_warn("multiple nodes in dt, skipping\n");
		return 0;
	}

	has_names = of_property_present(np, "interrupt-names");

	for (i = ARCH_TIMER_PHYS_SECURE_PPI; i < ARCH_TIMER_MAX_TIMER_PPI; i++) {
		if (has_names)
			irq = of_irq_get_byname(np, arch_timer_ppi_names[i]);
		else
			irq = of_irq_get(np, i);
		if (irq > 0)
			arch_timer_ppi[i] = irq;
	}

	arch_timer_populate_kvm_info();

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

	return arch_timer_common_init();
}
TIMER_OF_DECLARE(armv7_arch_timer, "arm,armv7-timer", arch_timer_of_init);
TIMER_OF_DECLARE(armv8_arch_timer, "arm,armv8-timer", arch_timer_of_init);

#ifdef CONFIG_ACPI_GTDT
static int __init arch_timer_acpi_init(struct acpi_table_header *table)
{
	int ret;

	if (arch_timer_evt) {
		pr_warn("already initialized, skipping\n");
		return -EINVAL;
	}

	ret = acpi_gtdt_init(table, NULL);
	if (ret)
		return ret;

	arch_timer_ppi[ARCH_TIMER_PHYS_NONSECURE_PPI] =
		acpi_gtdt_map_ppi(ARCH_TIMER_PHYS_NONSECURE_PPI);

	arch_timer_ppi[ARCH_TIMER_VIRT_PPI] =
		acpi_gtdt_map_ppi(ARCH_TIMER_VIRT_PPI);

	arch_timer_ppi[ARCH_TIMER_HYP_PPI] =
		acpi_gtdt_map_ppi(ARCH_TIMER_HYP_PPI);

	arch_timer_populate_kvm_info();

	/*
	 * When probing via ACPI, we have no mechanism to override the sysreg
	 * CNTFRQ value. This *must* be correct.
	 */
	arch_timer_rate = arch_timer_get_cntfrq();
	ret = validate_timer_rate();
	if (ret) {
		pr_err(FW_BUG "frequency not available.\n");
		return ret;
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

	return arch_timer_common_init();
}
TIMER_ACPI_DECLARE(arch_timer, ACPI_SIG_GTDT, arch_timer_acpi_init);
#endif

int kvm_arch_ptp_get_crosststamp(u64 *cycle, struct timespec64 *ts,
				 enum clocksource_ids *cs_id)
{
	struct arm_smccc_res hvc_res;
	u32 ptp_counter;
	ktime_t ktime;

	if (!IS_ENABLED(CONFIG_HAVE_ARM_SMCCC_DISCOVERY))
		return -EOPNOTSUPP;

	if (arch_timer_uses_ppi == ARCH_TIMER_VIRT_PPI)
		ptp_counter = KVM_PTP_VIRT_COUNTER;
	else
		ptp_counter = KVM_PTP_PHYS_COUNTER;

	arm_smccc_1_1_invoke(ARM_SMCCC_VENDOR_HYP_KVM_PTP_FUNC_ID,
			     ptp_counter, &hvc_res);

	if ((int)(hvc_res.a0) < 0)
		return -EOPNOTSUPP;

	ktime = (u64)hvc_res.a0 << 32 | hvc_res.a1;
	*ts = ktime_to_timespec64(ktime);
	if (cycle)
		*cycle = (u64)hvc_res.a2 << 32 | hvc_res.a3;
	if (cs_id)
		*cs_id = CSID_ARM_ARCH_COUNTER;

	return 0;
}
EXPORT_SYMBOL_GPL(kvm_arch_ptp_get_crosststamp);
