// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * processor_idle - idle state submodule to the ACPI processor driver
 *
 *  Copyright (C) 2001, 2002 Andy Grover <andrew.grover@intel.com>
 *  Copyright (C) 2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 *  Copyright (C) 2004, 2005 Dominik Brodowski <linux@brodo.de>
 *  Copyright (C) 2004  Anil S Keshavamurthy <anil.s.keshavamurthy@intel.com>
 *  			- Added processor hotplug support
 *  Copyright (C) 2005  Venkatesh Pallipadi <venkatesh.pallipadi@intel.com>
 *  			- Added support for C3 on SMP
 */
#define pr_fmt(fmt) "ACPI: " fmt

#include <linux/module.h>
#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/sched.h>       /* need_resched() */
#include <linux/sort.h>
#include <linux/tick.h>
#include <linux/cpuidle.h>
#include <linux/cpu.h>
#include <acpi/processor.h>

/*
 * Include the apic definitions for x86 to have the APIC timer related defines
 * available also for UP (on SMP it gets magically included via linux/smp.h).
 * asm/acpi.h is not an option, as it would require more include magic. Also
 * creating an empty asm-ia64/apic.h would just trade pest vs. cholera.
 */
#ifdef CONFIG_X86
#include <asm/apic.h>
#include <asm/cpu.h>
#endif

#define ACPI_PROCESSOR_CLASS            "processor"
#define _COMPONENT              ACPI_PROCESSOR_COMPONENT
ACPI_MODULE_NAME("processor_idle");

#define ACPI_IDLE_STATE_START	(IS_ENABLED(CONFIG_ARCH_HAS_CPU_RELAX) ? 1 : 0)

static unsigned int max_cstate __read_mostly = ACPI_PROCESSOR_MAX_POWER;
module_param(max_cstate, uint, 0000);
static unsigned int nocst __read_mostly;
module_param(nocst, uint, 0000);
static int bm_check_disable __read_mostly;
module_param(bm_check_disable, uint, 0000);

static unsigned int latency_factor __read_mostly = 2;
module_param(latency_factor, uint, 0644);

static DEFINE_PER_CPU(struct cpuidle_device *, acpi_cpuidle_device);

struct cpuidle_driver acpi_idle_driver = {
	.name =		"acpi_idle",
	.owner =	THIS_MODULE,
};

#ifdef CONFIG_ACPI_PROCESSOR_CSTATE
static
DEFINE_PER_CPU(struct acpi_processor_cx * [CPUIDLE_STATE_MAX], acpi_cstate);

static int disabled_by_idle_boot_param(void)
{
	return boot_option_idle_override == IDLE_POLL ||
		boot_option_idle_override == IDLE_HALT;
}

/*
 * IBM ThinkPad R40e crashes mysteriously when going into C2 or C3.
 * For now disable this. Probably a bug somewhere else.
 *
 * To skip this limit, boot/load with a large max_cstate limit.
 */
static int set_max_cstate(const struct dmi_system_id *id)
{
	if (max_cstate > ACPI_PROCESSOR_MAX_POWER)
		return 0;

	pr_notice("%s detected - limiting to C%ld max_cstate."
		  " Override with \"processor.max_cstate=%d\"\n", id->ident,
		  (long)id->driver_data, ACPI_PROCESSOR_MAX_POWER + 1);

	max_cstate = (long)id->driver_data;

	return 0;
}

static const struct dmi_system_id processor_power_dmi_table[] = {
	{ set_max_cstate, "Clevo 5600D", {
	  DMI_MATCH(DMI_BIOS_VENDOR,"Phoenix Technologies LTD"),
	  DMI_MATCH(DMI_BIOS_VERSION,"SHE845M0.86C.0013.D.0302131307")},
	 (void *)2},
	{ set_max_cstate, "Pavilion zv5000", {
	  DMI_MATCH(DMI_SYS_VENDOR, "Hewlett-Packard"),
	  DMI_MATCH(DMI_PRODUCT_NAME,"Pavilion zv5000 (DS502A#ABA)")},
	 (void *)1},
	{ set_max_cstate, "Asus L8400B", {
	  DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK Computer Inc."),
	  DMI_MATCH(DMI_PRODUCT_NAME,"L8400B series Notebook PC")},
	 (void *)1},
	{},
};


/*
 * Callers should disable interrupts before the call and enable
 * interrupts after return.
 */
static void __cpuidle acpi_safe_halt(void)
{
	if (!tif_need_resched()) {
		safe_halt();
		local_irq_disable();
	}
}

#ifdef ARCH_APICTIMER_STOPS_ON_C3

/*
 * Some BIOS implementations switch to C3 in the published C2 state.
 * This seems to be a common problem on AMD boxen, but other vendors
 * are affected too. We pick the most conservative approach: we assume
 * that the local APIC stops in both C2 and C3.
 */
static void lapic_timer_check_state(int state, struct acpi_processor *pr,
				   struct acpi_processor_cx *cx)
{
	struct acpi_processor_power *pwr = &pr->power;
	u8 type = local_apic_timer_c2_ok ? ACPI_STATE_C3 : ACPI_STATE_C2;

	if (cpu_has(&cpu_data(pr->id), X86_FEATURE_ARAT))
		return;

	if (boot_cpu_has_bug(X86_BUG_AMD_APIC_C1E))
		type = ACPI_STATE_C1;

	/*
	 * Check, if one of the previous states already marked the lapic
	 * unstable
	 */
	if (pwr->timer_broadcast_on_state < state)
		return;

	if (cx->type >= type)
		pr->power.timer_broadcast_on_state = state;
}

static void __lapic_timer_propagate_broadcast(void *arg)
{
	struct acpi_processor *pr = (struct acpi_processor *) arg;

	if (pr->power.timer_broadcast_on_state < INT_MAX)
		tick_broadcast_enable();
	else
		tick_broadcast_disable();
}

static void lapic_timer_propagate_broadcast(struct acpi_processor *pr)
{
	smp_call_function_single(pr->id, __lapic_timer_propagate_broadcast,
				 (void *)pr, 1);
}

/* Power(C) State timer broadcast control */
static bool lapic_timer_needs_broadcast(struct acpi_processor *pr,
					struct acpi_processor_cx *cx)
{
	return cx - pr->power.states >= pr->power.timer_broadcast_on_state;
}

#else

static void lapic_timer_check_state(int state, struct acpi_processor *pr,
				   struct acpi_processor_cx *cstate) { }
static void lapic_timer_propagate_broadcast(struct acpi_processor *pr) { }

static bool lapic_timer_needs_broadcast(struct acpi_processor *pr,
					struct acpi_processor_cx *cx)
{
	return false;
}

#endif

#if defined(CONFIG_X86)
static void tsc_check_state(int state)
{
	switch (boot_cpu_data.x86_vendor) {
	case X86_VENDOR_HYGON:
	case X86_VENDOR_AMD:
	case X86_VENDOR_INTEL:
	case X86_VENDOR_CENTAUR:
	case X86_VENDOR_ZHAOXIN:
		/*
		 * AMD Fam10h TSC will tick in all
		 * C/P/S0/S1 states when this bit is set.
		 */
		if (boot_cpu_has(X86_FEATURE_NONSTOP_TSC))
			return;
		fallthrough;
	default:
		/* TSC could halt in idle, so notify users */
		if (state > ACPI_STATE_C1)
			mark_tsc_unstable("TSC halts in idle");
	}
}
#else
static void tsc_check_state(int state) { return; }
#endif

static int acpi_processor_get_power_info_fadt(struct acpi_processor *pr)
{

	if (!pr->pblk)
		return -ENODEV;

	/* if info is obtained from pblk/fadt, type equals state */
	pr->power.states[ACPI_STATE_C2].type = ACPI_STATE_C2;
	pr->power.states[ACPI_STATE_C3].type = ACPI_STATE_C3;

#ifndef CONFIG_HOTPLUG_CPU
	/*
	 * Check for P_LVL2_UP flag before entering C2 and above on
	 * an SMP system.
	 */
	if ((num_online_cpus() > 1) &&
	    !(acpi_gbl_FADT.flags & ACPI_FADT_C2_MP_SUPPORTED))
		return -ENODEV;
#endif

	/* determine C2 and C3 address from pblk */
	pr->power.states[ACPI_STATE_C2].address = pr->pblk + 4;
	pr->power.states[ACPI_STATE_C3].address = pr->pblk + 5;

	/* determine latencies from FADT */
	pr->power.states[ACPI_STATE_C2].latency = acpi_gbl_FADT.c2_latency;
	pr->power.states[ACPI_STATE_C3].latency = acpi_gbl_FADT.c3_latency;

	/*
	 * FADT specified C2 latency must be less than or equal to
	 * 100 microseconds.
	 */
	if (acpi_gbl_FADT.c2_latency > ACPI_PROCESSOR_MAX_C2_LATENCY) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
			"C2 latency too large [%d]\n", acpi_gbl_FADT.c2_latency));
		/* invalidate C2 */
		pr->power.states[ACPI_STATE_C2].address = 0;
	}

	/*
	 * FADT supplied C3 latency must be less than or equal to
	 * 1000 microseconds.
	 */
	if (acpi_gbl_FADT.c3_latency > ACPI_PROCESSOR_MAX_C3_LATENCY) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
			"C3 latency too large [%d]\n", acpi_gbl_FADT.c3_latency));
		/* invalidate C3 */
		pr->power.states[ACPI_STATE_C3].address = 0;
	}

	ACPI_DEBUG_PRINT((ACPI_DB_INFO,
			  "lvl2[0x%08x] lvl3[0x%08x]\n",
			  pr->power.states[ACPI_STATE_C2].address,
			  pr->power.states[ACPI_STATE_C3].address));

	snprintf(pr->power.states[ACPI_STATE_C2].desc,
			 ACPI_CX_DESC_LEN, "ACPI P_LVL2 IOPORT 0x%x",
			 pr->power.states[ACPI_STATE_C2].address);
	snprintf(pr->power.states[ACPI_STATE_C3].desc,
			 ACPI_CX_DESC_LEN, "ACPI P_LVL3 IOPORT 0x%x",
			 pr->power.states[ACPI_STATE_C3].address);

	return 0;
}

static int acpi_processor_get_power_info_default(struct acpi_processor *pr)
{
	if (!pr->power.states[ACPI_STATE_C1].valid) {
		/* set the first C-State to C1 */
		/* all processors need to support C1 */
		pr->power.states[ACPI_STATE_C1].type = ACPI_STATE_C1;
		pr->power.states[ACPI_STATE_C1].valid = 1;
		pr->power.states[ACPI_STATE_C1].entry_method = ACPI_CSTATE_HALT;

		snprintf(pr->power.states[ACPI_STATE_C1].desc,
			 ACPI_CX_DESC_LEN, "ACPI HLT");
	}
	/* the C0 state only exists as a filler in our array */
	pr->power.states[ACPI_STATE_C0].valid = 1;
	return 0;
}

static int acpi_processor_get_power_info_cst(struct acpi_processor *pr)
{
	int ret;

	if (nocst)
		return -ENODEV;

	ret = acpi_processor_evaluate_cst(pr->handle, pr->id, &pr->power);
	if (ret)
		return ret;

	if (!pr->power.count)
		return -EFAULT;

	pr->flags.has_cst = 1;
	return 0;
}

static void acpi_processor_power_verify_c3(struct acpi_processor *pr,
					   struct acpi_processor_cx *cx)
{
	static int bm_check_flag = -1;
	static int bm_control_flag = -1;


	if (!cx->address)
		return;

	/*
	 * PIIX4 Erratum #18: We don't support C3 when Type-F (fast)
	 * DMA transfers are used by any ISA device to avoid livelock.
	 * Note that we could disable Type-F DMA (as recommended by
	 * the erratum), but this is known to disrupt certain ISA
	 * devices thus we take the conservative approach.
	 */
	else if (errata.piix4.fdma) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "C3 not supported on PIIX4 with Type-F DMA\n"));
		return;
	}

	/* All the logic here assumes flags.bm_check is same across all CPUs */
	if (bm_check_flag == -1) {
		/* Determine whether bm_check is needed based on CPU  */
		acpi_processor_power_init_bm_check(&(pr->flags), pr->id);
		bm_check_flag = pr->flags.bm_check;
		bm_control_flag = pr->flags.bm_control;
	} else {
		pr->flags.bm_check = bm_check_flag;
		pr->flags.bm_control = bm_control_flag;
	}

	if (pr->flags.bm_check) {
		if (!pr->flags.bm_control) {
			if (pr->flags.has_cst != 1) {
				/* bus mastering control is necessary */
				ACPI_DEBUG_PRINT((ACPI_DB_INFO,
					"C3 support requires BM control\n"));
				return;
			} else {
				/* Here we enter C3 without bus mastering */
				ACPI_DEBUG_PRINT((ACPI_DB_INFO,
					"C3 support without BM control\n"));
			}
		}
	} else {
		/*
		 * WBINVD should be set in fadt, for C3 state to be
		 * supported on when bm_check is not required.
		 */
		if (!(acpi_gbl_FADT.flags & ACPI_FADT_WBINVD)) {
			ACPI_DEBUG_PRINT((ACPI_DB_INFO,
					  "Cache invalidation should work properly"
					  " for C3 to be enabled on SMP systems\n"));
			return;
		}
	}

	/*
	 * Otherwise we've met all of our C3 requirements.
	 * Normalize the C3 latency to expidite policy.  Enable
	 * checking of bus mastering status (bm_check) so we can
	 * use this in our C3 policy
	 */
	cx->valid = 1;

	/*
	 * On older chipsets, BM_RLD needs to be set
	 * in order for Bus Master activity to wake the
	 * system from C3.  Newer chipsets handle DMA
	 * during C3 automatically and BM_RLD is a NOP.
	 * In either case, the proper way to
	 * handle BM_RLD is to set it and leave it set.
	 */
	acpi_write_bit_register(ACPI_BITREG_BUS_MASTER_RLD, 1);

	return;
}

static int acpi_cst_latency_cmp(const void *a, const void *b)
{
	const struct acpi_processor_cx *x = a, *y = b;

	if (!(x->valid && y->valid))
		return 0;
	if (x->latency > y->latency)
		return 1;
	if (x->latency < y->latency)
		return -1;
	return 0;
}
static void acpi_cst_latency_swap(void *a, void *b, int n)
{
	struct acpi_processor_cx *x = a, *y = b;
	u32 tmp;

	if (!(x->valid && y->valid))
		return;
	tmp = x->latency;
	x->latency = y->latency;
	y->latency = tmp;
}

static int acpi_processor_power_verify(struct acpi_processor *pr)
{
	unsigned int i;
	unsigned int working = 0;
	unsigned int last_latency = 0;
	unsigned int last_type = 0;
	bool buggy_latency = false;

	pr->power.timer_broadcast_on_state = INT_MAX;

	for (i = 1; i < ACPI_PROCESSOR_MAX_POWER && i <= max_cstate; i++) {
		struct acpi_processor_cx *cx = &pr->power.states[i];

		switch (cx->type) {
		case ACPI_STATE_C1:
			cx->valid = 1;
			break;

		case ACPI_STATE_C2:
			if (!cx->address)
				break;
			cx->valid = 1;
			break;

		case ACPI_STATE_C3:
			acpi_processor_power_verify_c3(pr, cx);
			break;
		}
		if (!cx->valid)
			continue;
		if (cx->type >= last_type && cx->latency < last_latency)
			buggy_latency = true;
		last_latency = cx->latency;
		last_type = cx->type;

		lapic_timer_check_state(i, pr, cx);
		tsc_check_state(cx->type);
		working++;
	}

	if (buggy_latency) {
		pr_notice("FW issue: working around C-state latencies out of order\n");
		sort(&pr->power.states[1], max_cstate,
		     sizeof(struct acpi_processor_cx),
		     acpi_cst_latency_cmp,
		     acpi_cst_latency_swap);
	}

	lapic_timer_propagate_broadcast(pr);

	return (working);
}

static int acpi_processor_get_cstate_info(struct acpi_processor *pr)
{
	unsigned int i;
	int result;


	/* NOTE: the idle thread may not be running while calling
	 * this function */

	/* Zero initialize all the C-states info. */
	memset(pr->power.states, 0, sizeof(pr->power.states));

	result = acpi_processor_get_power_info_cst(pr);
	if (result == -ENODEV)
		result = acpi_processor_get_power_info_fadt(pr);

	if (result)
		return result;

	acpi_processor_get_power_info_default(pr);

	pr->power.count = acpi_processor_power_verify(pr);

	/*
	 * if one state of type C2 or C3 is available, mark this
	 * CPU as being "idle manageable"
	 */
	for (i = 1; i < ACPI_PROCESSOR_MAX_POWER; i++) {
		if (pr->power.states[i].valid) {
			pr->power.count = i;
			pr->flags.power = 1;
		}
	}

	return 0;
}

/**
 * acpi_idle_bm_check - checks if bus master activity was detected
 */
static int acpi_idle_bm_check(void)
{
	u32 bm_status = 0;

	if (bm_check_disable)
		return 0;

	acpi_read_bit_register(ACPI_BITREG_BUS_MASTER_STATUS, &bm_status);
	if (bm_status)
		acpi_write_bit_register(ACPI_BITREG_BUS_MASTER_STATUS, 1);
	/*
	 * PIIX4 Erratum #18: Note that BM_STS doesn't always reflect
	 * the true state of bus mastering activity; forcing us to
	 * manually check the BMIDEA bit of each IDE channel.
	 */
	else if (errata.piix4.bmisx) {
		if ((inb_p(errata.piix4.bmisx + 0x02) & 0x01)
		    || (inb_p(errata.piix4.bmisx + 0x0A) & 0x01))
			bm_status = 1;
	}
	return bm_status;
}

static void wait_for_freeze(void)
{
#ifdef	CONFIG_X86
	/* No delay is needed if we are in guest */
	if (boot_cpu_has(X86_FEATURE_HYPERVISOR))
		return;
	/*
	 * Modern (>=Nehalem) Intel systems use ACPI via intel_idle,
	 * not this code.  Assume that any Intel systems using this
	 * are ancient and may need the dummy wait.  This also assumes
	 * that the motivating chipset issue was Intel-only.
	 */
	if (boot_cpu_data.x86_vendor != X86_VENDOR_INTEL)
		return;
#endif
	/*
	 * Dummy wait op - must do something useless after P_LVL2 read
	 * because chipsets cannot guarantee that STPCLK# signal gets
	 * asserted in time to freeze execution properly
	 *
	 * This workaround has been in place since the original ACPI
	 * implementation was merged, circa 2002.
	 *
	 * If a profile is pointing to this instruction, please first
	 * consider moving your system to a more modern idle
	 * mechanism.
	 */
	inl(acpi_gbl_FADT.xpm_timer_block.address);
}

/**
 * acpi_idle_do_entry - enter idle state using the appropriate method
 * @cx: cstate data
 *
 * Caller disables interrupt before call and enables interrupt after return.
 */
static void __cpuidle acpi_idle_do_entry(struct acpi_processor_cx *cx)
{
	if (cx->entry_method == ACPI_CSTATE_FFH) {
		/* Call into architectural FFH based C-state */
		acpi_processor_ffh_cstate_enter(cx);
	} else if (cx->entry_method == ACPI_CSTATE_HALT) {
		acpi_safe_halt();
	} else {
		/* IO port based C-state */
		inb(cx->address);
		wait_for_freeze();
	}
}

/**
 * acpi_idle_play_dead - enters an ACPI state for long-term idle (i.e. off-lining)
 * @dev: the target CPU
 * @index: the index of suggested state
 */
static int acpi_idle_play_dead(struct cpuidle_device *dev, int index)
{
	struct acpi_processor_cx *cx = per_cpu(acpi_cstate[index], dev->cpu);

	ACPI_FLUSH_CPU_CACHE();

	while (1) {

		if (cx->entry_method == ACPI_CSTATE_HALT)
			safe_halt();
		else if (cx->entry_method == ACPI_CSTATE_SYSTEMIO) {
			inb(cx->address);
			wait_for_freeze();
		} else
			return -ENODEV;

#if defined(CONFIG_X86) && defined(CONFIG_HOTPLUG_CPU)
		cond_wakeup_cpu0();
#endif
	}

	/* Never reached */
	return 0;
}

static bool acpi_idle_fallback_to_c1(struct acpi_processor *pr)
{
	return IS_ENABLED(CONFIG_HOTPLUG_CPU) && !pr->flags.has_cst &&
		!(acpi_gbl_FADT.flags & ACPI_FADT_C2_MP_SUPPORTED);
}

static int c3_cpu_count;
static DEFINE_RAW_SPINLOCK(c3_lock);

/**
 * acpi_idle_enter_bm - enters C3 with proper BM handling
 * @drv: cpuidle driver
 * @pr: Target processor
 * @cx: Target state context
 * @index: index of target state
 */
static int __cpuidle acpi_idle_enter_bm(struct cpuidle_driver *drv,
			       struct acpi_processor *pr,
			       struct acpi_processor_cx *cx,
			       int index)
{
	static struct acpi_processor_cx safe_cx = {
		.entry_method = ACPI_CSTATE_HALT,
	};

	/*
	 * disable bus master
	 * bm_check implies we need ARB_DIS
	 * bm_control implies whether we can do ARB_DIS
	 *
	 * That leaves a case where bm_check is set and bm_control is not set.
	 * In that case we cannot do much, we enter C3 without doing anything.
	 */
	bool dis_bm = pr->flags.bm_control;

	/* If we can skip BM, demote to a safe state. */
	if (!cx->bm_sts_skip && acpi_idle_bm_check()) {
		dis_bm = false;
		index = drv->safe_state_index;
		if (index >= 0) {
			cx = this_cpu_read(acpi_cstate[index]);
		} else {
			cx = &safe_cx;
			index = -EBUSY;
		}
	}

	if (dis_bm) {
		raw_spin_lock(&c3_lock);
		c3_cpu_count++;
		/* Disable bus master arbitration when all CPUs are in C3 */
		if (c3_cpu_count == num_online_cpus())
			acpi_write_bit_register(ACPI_BITREG_ARB_DISABLE, 1);
		raw_spin_unlock(&c3_lock);
	}

	rcu_idle_enter();

	acpi_idle_do_entry(cx);

	rcu_idle_exit();

	/* Re-enable bus master arbitration */
	if (dis_bm) {
		raw_spin_lock(&c3_lock);
		acpi_write_bit_register(ACPI_BITREG_ARB_DISABLE, 0);
		c3_cpu_count--;
		raw_spin_unlock(&c3_lock);
	}

	return index;
}

static int __cpuidle acpi_idle_enter(struct cpuidle_device *dev,
			   struct cpuidle_driver *drv, int index)
{
	struct acpi_processor_cx *cx = per_cpu(acpi_cstate[index], dev->cpu);
	struct acpi_processor *pr;

	pr = __this_cpu_read(processors);
	if (unlikely(!pr))
		return -EINVAL;

	if (cx->type != ACPI_STATE_C1) {
		if (cx->type == ACPI_STATE_C3 && pr->flags.bm_check)
			return acpi_idle_enter_bm(drv, pr, cx, index);

		/* C2 to C1 demotion. */
		if (acpi_idle_fallback_to_c1(pr) && num_online_cpus() > 1) {
			index = ACPI_IDLE_STATE_START;
			cx = per_cpu(acpi_cstate[index], dev->cpu);
		}
	}

	if (cx->type == ACPI_STATE_C3)
		ACPI_FLUSH_CPU_CACHE();

	acpi_idle_do_entry(cx);

	return index;
}

static int __cpuidle acpi_idle_enter_s2idle(struct cpuidle_device *dev,
				  struct cpuidle_driver *drv, int index)
{
	struct acpi_processor_cx *cx = per_cpu(acpi_cstate[index], dev->cpu);

	if (cx->type == ACPI_STATE_C3) {
		struct acpi_processor *pr = __this_cpu_read(processors);

		if (unlikely(!pr))
			return 0;

		if (pr->flags.bm_check) {
			u8 bm_sts_skip = cx->bm_sts_skip;

			/* Don't check BM_STS, do an unconditional ARB_DIS for S2IDLE */
			cx->bm_sts_skip = 1;
			acpi_idle_enter_bm(drv, pr, cx, index);
			cx->bm_sts_skip = bm_sts_skip;

			return 0;
		} else {
			ACPI_FLUSH_CPU_CACHE();
		}
	}
	acpi_idle_do_entry(cx);

	return 0;
}

static int acpi_processor_setup_cpuidle_cx(struct acpi_processor *pr,
					   struct cpuidle_device *dev)
{
	int i, count = ACPI_IDLE_STATE_START;
	struct acpi_processor_cx *cx;
	struct cpuidle_state *state;

	if (max_cstate == 0)
		max_cstate = 1;

	for (i = 1; i < ACPI_PROCESSOR_MAX_POWER && i <= max_cstate; i++) {
		state = &acpi_idle_driver.states[count];
		cx = &pr->power.states[i];

		if (!cx->valid)
			continue;

		per_cpu(acpi_cstate[count], dev->cpu) = cx;

		if (lapic_timer_needs_broadcast(pr, cx))
			state->flags |= CPUIDLE_FLAG_TIMER_STOP;

		if (cx->type == ACPI_STATE_C3) {
			state->flags |= CPUIDLE_FLAG_TLB_FLUSHED;
			if (pr->flags.bm_check)
				state->flags |= CPUIDLE_FLAG_RCU_IDLE;
		}

		count++;
		if (count == CPUIDLE_STATE_MAX)
			break;
	}

	if (!count)
		return -EINVAL;

	return 0;
}

static int acpi_processor_setup_cstates(struct acpi_processor *pr)
{
	int i, count;
	struct acpi_processor_cx *cx;
	struct cpuidle_state *state;
	struct cpuidle_driver *drv = &acpi_idle_driver;

	if (max_cstate == 0)
		max_cstate = 1;

	if (IS_ENABLED(CONFIG_ARCH_HAS_CPU_RELAX)) {
		cpuidle_poll_state_init(drv);
		count = 1;
	} else {
		count = 0;
	}

	for (i = 1; i < ACPI_PROCESSOR_MAX_POWER && i <= max_cstate; i++) {
		cx = &pr->power.states[i];

		if (!cx->valid)
			continue;

		state = &drv->states[count];
		snprintf(state->name, CPUIDLE_NAME_LEN, "C%d", i);
		strlcpy(state->desc, cx->desc, CPUIDLE_DESC_LEN);
		state->exit_latency = cx->latency;
		state->target_residency = cx->latency * latency_factor;
		state->enter = acpi_idle_enter;

		state->flags = 0;
		if (cx->type == ACPI_STATE_C1 || cx->type == ACPI_STATE_C2) {
			state->enter_dead = acpi_idle_play_dead;
			drv->safe_state_index = count;
		}
		/*
		 * Halt-induced C1 is not good for ->enter_s2idle, because it
		 * re-enables interrupts on exit.  Moreover, C1 is generally not
		 * particularly interesting from the suspend-to-idle angle, so
		 * avoid C1 and the situations in which we may need to fall back
		 * to it altogether.
		 */
		if (cx->type != ACPI_STATE_C1 && !acpi_idle_fallback_to_c1(pr))
			state->enter_s2idle = acpi_idle_enter_s2idle;

		count++;
		if (count == CPUIDLE_STATE_MAX)
			break;
	}

	drv->state_count = count;

	if (!count)
		return -EINVAL;

	return 0;
}

static inline void acpi_processor_cstate_first_run_checks(void)
{
	static int first_run;

	if (first_run)
		return;
	dmi_check_system(processor_power_dmi_table);
	max_cstate = acpi_processor_cstate_check(max_cstate);
	if (max_cstate < ACPI_C_STATES_MAX)
		pr_notice("ACPI: processor limited to max C-state %d\n",
			  max_cstate);
	first_run++;

	if (nocst)
		return;

	acpi_processor_claim_cst_control();
}
#else

static inline int disabled_by_idle_boot_param(void) { return 0; }
static inline void acpi_processor_cstate_first_run_checks(void) { }
static int acpi_processor_get_cstate_info(struct acpi_processor *pr)
{
	return -ENODEV;
}

static int acpi_processor_setup_cpuidle_cx(struct acpi_processor *pr,
					   struct cpuidle_device *dev)
{
	return -EINVAL;
}

static int acpi_processor_setup_cstates(struct acpi_processor *pr)
{
	return -EINVAL;
}

#endif /* CONFIG_ACPI_PROCESSOR_CSTATE */

struct acpi_lpi_states_array {
	unsigned int size;
	unsigned int composite_states_size;
	struct acpi_lpi_state *entries;
	struct acpi_lpi_state *composite_states[ACPI_PROCESSOR_MAX_POWER];
};

static int obj_get_integer(union acpi_object *obj, u32 *value)
{
	if (obj->type != ACPI_TYPE_INTEGER)
		return -EINVAL;

	*value = obj->integer.value;
	return 0;
}

static int acpi_processor_evaluate_lpi(acpi_handle handle,
				       struct acpi_lpi_states_array *info)
{
	acpi_status status;
	int ret = 0;
	int pkg_count, state_idx = 1, loop;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *lpi_data;
	struct acpi_lpi_state *lpi_state;

	status = acpi_evaluate_object(handle, "_LPI", NULL, &buffer);
	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "No _LPI, giving up\n"));
		return -ENODEV;
	}

	lpi_data = buffer.pointer;

	/* There must be at least 4 elements = 3 elements + 1 package */
	if (!lpi_data || lpi_data->type != ACPI_TYPE_PACKAGE ||
	    lpi_data->package.count < 4) {
		pr_debug("not enough elements in _LPI\n");
		ret = -ENODATA;
		goto end;
	}

	pkg_count = lpi_data->package.elements[2].integer.value;

	/* Validate number of power states. */
	if (pkg_count < 1 || pkg_count != lpi_data->package.count - 3) {
		pr_debug("count given by _LPI is not valid\n");
		ret = -ENODATA;
		goto end;
	}

	lpi_state = kcalloc(pkg_count, sizeof(*lpi_state), GFP_KERNEL);
	if (!lpi_state) {
		ret = -ENOMEM;
		goto end;
	}

	info->size = pkg_count;
	info->entries = lpi_state;

	/* LPI States start at index 3 */
	for (loop = 3; state_idx <= pkg_count; loop++, state_idx++, lpi_state++) {
		union acpi_object *element, *pkg_elem, *obj;

		element = &lpi_data->package.elements[loop];
		if (element->type != ACPI_TYPE_PACKAGE || element->package.count < 7)
			continue;

		pkg_elem = element->package.elements;

		obj = pkg_elem + 6;
		if (obj->type == ACPI_TYPE_BUFFER) {
			struct acpi_power_register *reg;

			reg = (struct acpi_power_register *)obj->buffer.pointer;
			if (reg->space_id != ACPI_ADR_SPACE_SYSTEM_IO &&
			    reg->space_id != ACPI_ADR_SPACE_FIXED_HARDWARE)
				continue;

			lpi_state->address = reg->address;
			lpi_state->entry_method =
				reg->space_id == ACPI_ADR_SPACE_FIXED_HARDWARE ?
				ACPI_CSTATE_FFH : ACPI_CSTATE_SYSTEMIO;
		} else if (obj->type == ACPI_TYPE_INTEGER) {
			lpi_state->entry_method = ACPI_CSTATE_INTEGER;
			lpi_state->address = obj->integer.value;
		} else {
			continue;
		}

		/* elements[7,8] skipped for now i.e. Residency/Usage counter*/

		obj = pkg_elem + 9;
		if (obj->type == ACPI_TYPE_STRING)
			strlcpy(lpi_state->desc, obj->string.pointer,
				ACPI_CX_DESC_LEN);

		lpi_state->index = state_idx;
		if (obj_get_integer(pkg_elem + 0, &lpi_state->min_residency)) {
			pr_debug("No min. residency found, assuming 10 us\n");
			lpi_state->min_residency = 10;
		}

		if (obj_get_integer(pkg_elem + 1, &lpi_state->wake_latency)) {
			pr_debug("No wakeup residency found, assuming 10 us\n");
			lpi_state->wake_latency = 10;
		}

		if (obj_get_integer(pkg_elem + 2, &lpi_state->flags))
			lpi_state->flags = 0;

		if (obj_get_integer(pkg_elem + 3, &lpi_state->arch_flags))
			lpi_state->arch_flags = 0;

		if (obj_get_integer(pkg_elem + 4, &lpi_state->res_cnt_freq))
			lpi_state->res_cnt_freq = 1;

		if (obj_get_integer(pkg_elem + 5, &lpi_state->enable_parent_state))
			lpi_state->enable_parent_state = 0;
	}

	acpi_handle_debug(handle, "Found %d power states\n", state_idx);
end:
	kfree(buffer.pointer);
	return ret;
}

/*
 * flat_state_cnt - the number of composite LPI states after the process of flattening
 */
static int flat_state_cnt;

/**
 * combine_lpi_states - combine local and parent LPI states to form a composite LPI state
 *
 * @local: local LPI state
 * @parent: parent LPI state
 * @result: composite LPI state
 */
static bool combine_lpi_states(struct acpi_lpi_state *local,
			       struct acpi_lpi_state *parent,
			       struct acpi_lpi_state *result)
{
	if (parent->entry_method == ACPI_CSTATE_INTEGER) {
		if (!parent->address) /* 0 means autopromotable */
			return false;
		result->address = local->address + parent->address;
	} else {
		result->address = parent->address;
	}

	result->min_residency = max(local->min_residency, parent->min_residency);
	result->wake_latency = local->wake_latency + parent->wake_latency;
	result->enable_parent_state = parent->enable_parent_state;
	result->entry_method = local->entry_method;

	result->flags = parent->flags;
	result->arch_flags = parent->arch_flags;
	result->index = parent->index;

	strlcpy(result->desc, local->desc, ACPI_CX_DESC_LEN);
	strlcat(result->desc, "+", ACPI_CX_DESC_LEN);
	strlcat(result->desc, parent->desc, ACPI_CX_DESC_LEN);
	return true;
}

#define ACPI_LPI_STATE_FLAGS_ENABLED			BIT(0)

static void stash_composite_state(struct acpi_lpi_states_array *curr_level,
				  struct acpi_lpi_state *t)
{
	curr_level->composite_states[curr_level->composite_states_size++] = t;
}

static int flatten_lpi_states(struct acpi_processor *pr,
			      struct acpi_lpi_states_array *curr_level,
			      struct acpi_lpi_states_array *prev_level)
{
	int i, j, state_count = curr_level->size;
	struct acpi_lpi_state *p, *t = curr_level->entries;

	curr_level->composite_states_size = 0;
	for (j = 0; j < state_count; j++, t++) {
		struct acpi_lpi_state *flpi;

		if (!(t->flags & ACPI_LPI_STATE_FLAGS_ENABLED))
			continue;

		if (flat_state_cnt >= ACPI_PROCESSOR_MAX_POWER) {
			pr_warn("Limiting number of LPI states to max (%d)\n",
				ACPI_PROCESSOR_MAX_POWER);
			pr_warn("Please increase ACPI_PROCESSOR_MAX_POWER if needed.\n");
			break;
		}

		flpi = &pr->power.lpi_states[flat_state_cnt];

		if (!prev_level) { /* leaf/processor node */
			memcpy(flpi, t, sizeof(*t));
			stash_composite_state(curr_level, flpi);
			flat_state_cnt++;
			continue;
		}

		for (i = 0; i < prev_level->composite_states_size; i++) {
			p = prev_level->composite_states[i];
			if (t->index <= p->enable_parent_state &&
			    combine_lpi_states(p, t, flpi)) {
				stash_composite_state(curr_level, flpi);
				flat_state_cnt++;
				flpi++;
			}
		}
	}

	kfree(curr_level->entries);
	return 0;
}

int __weak acpi_processor_ffh_lpi_probe(unsigned int cpu)
{
	return -EOPNOTSUPP;
}

static int acpi_processor_get_lpi_info(struct acpi_processor *pr)
{
	int ret, i;
	acpi_status status;
	acpi_handle handle = pr->handle, pr_ahandle;
	struct acpi_device *d = NULL;
	struct acpi_lpi_states_array info[2], *tmp, *prev, *curr;

	/* make sure our architecture has support */
	ret = acpi_processor_ffh_lpi_probe(pr->id);
	if (ret == -EOPNOTSUPP)
		return ret;

	if (!osc_pc_lpi_support_confirmed)
		return -EOPNOTSUPP;

	if (!acpi_has_method(handle, "_LPI"))
		return -EINVAL;

	flat_state_cnt = 0;
	prev = &info[0];
	curr = &info[1];
	handle = pr->handle;
	ret = acpi_processor_evaluate_lpi(handle, prev);
	if (ret)
		return ret;
	flatten_lpi_states(pr, prev, NULL);

	status = acpi_get_parent(handle, &pr_ahandle);
	while (ACPI_SUCCESS(status)) {
		acpi_bus_get_device(pr_ahandle, &d);
		handle = pr_ahandle;

		if (strcmp(acpi_device_hid(d), ACPI_PROCESSOR_CONTAINER_HID))
			break;

		/* can be optional ? */
		if (!acpi_has_method(handle, "_LPI"))
			break;

		ret = acpi_processor_evaluate_lpi(handle, curr);
		if (ret)
			break;

		/* flatten all the LPI states in this level of hierarchy */
		flatten_lpi_states(pr, curr, prev);

		tmp = prev, prev = curr, curr = tmp;

		status = acpi_get_parent(handle, &pr_ahandle);
	}

	pr->power.count = flat_state_cnt;
	/* reset the index after flattening */
	for (i = 0; i < pr->power.count; i++)
		pr->power.lpi_states[i].index = i;

	/* Tell driver that _LPI is supported. */
	pr->flags.has_lpi = 1;
	pr->flags.power = 1;

	return 0;
}

int __weak acpi_processor_ffh_lpi_enter(struct acpi_lpi_state *lpi)
{
	return -ENODEV;
}

/**
 * acpi_idle_lpi_enter - enters an ACPI any LPI state
 * @dev: the target CPU
 * @drv: cpuidle driver containing cpuidle state info
 * @index: index of target state
 *
 * Return: 0 for success or negative value for error
 */
static int acpi_idle_lpi_enter(struct cpuidle_device *dev,
			       struct cpuidle_driver *drv, int index)
{
	struct acpi_processor *pr;
	struct acpi_lpi_state *lpi;

	pr = __this_cpu_read(processors);

	if (unlikely(!pr))
		return -EINVAL;

	lpi = &pr->power.lpi_states[index];
	if (lpi->entry_method == ACPI_CSTATE_FFH)
		return acpi_processor_ffh_lpi_enter(lpi);

	return -EINVAL;
}

static int acpi_processor_setup_lpi_states(struct acpi_processor *pr)
{
	int i;
	struct acpi_lpi_state *lpi;
	struct cpuidle_state *state;
	struct cpuidle_driver *drv = &acpi_idle_driver;

	if (!pr->flags.has_lpi)
		return -EOPNOTSUPP;

	for (i = 0; i < pr->power.count && i < CPUIDLE_STATE_MAX; i++) {
		lpi = &pr->power.lpi_states[i];

		state = &drv->states[i];
		snprintf(state->name, CPUIDLE_NAME_LEN, "LPI-%d", i);
		strlcpy(state->desc, lpi->desc, CPUIDLE_DESC_LEN);
		state->exit_latency = lpi->wake_latency;
		state->target_residency = lpi->min_residency;
		if (lpi->arch_flags)
			state->flags |= CPUIDLE_FLAG_TIMER_STOP;
		state->enter = acpi_idle_lpi_enter;
		drv->safe_state_index = i;
	}

	drv->state_count = i;

	return 0;
}

/**
 * acpi_processor_setup_cpuidle_states- prepares and configures cpuidle
 * global state data i.e. idle routines
 *
 * @pr: the ACPI processor
 */
static int acpi_processor_setup_cpuidle_states(struct acpi_processor *pr)
{
	int i;
	struct cpuidle_driver *drv = &acpi_idle_driver;

	if (!pr->flags.power_setup_done || !pr->flags.power)
		return -EINVAL;

	drv->safe_state_index = -1;
	for (i = ACPI_IDLE_STATE_START; i < CPUIDLE_STATE_MAX; i++) {
		drv->states[i].name[0] = '\0';
		drv->states[i].desc[0] = '\0';
	}

	if (pr->flags.has_lpi)
		return acpi_processor_setup_lpi_states(pr);

	return acpi_processor_setup_cstates(pr);
}

/**
 * acpi_processor_setup_cpuidle_dev - prepares and configures CPUIDLE
 * device i.e. per-cpu data
 *
 * @pr: the ACPI processor
 * @dev : the cpuidle device
 */
static int acpi_processor_setup_cpuidle_dev(struct acpi_processor *pr,
					    struct cpuidle_device *dev)
{
	if (!pr->flags.power_setup_done || !pr->flags.power || !dev)
		return -EINVAL;

	dev->cpu = pr->id;
	if (pr->flags.has_lpi)
		return acpi_processor_ffh_lpi_probe(pr->id);

	return acpi_processor_setup_cpuidle_cx(pr, dev);
}

static int acpi_processor_get_power_info(struct acpi_processor *pr)
{
	int ret;

	ret = acpi_processor_get_lpi_info(pr);
	if (ret)
		ret = acpi_processor_get_cstate_info(pr);

	return ret;
}

int acpi_processor_hotplug(struct acpi_processor *pr)
{
	int ret = 0;
	struct cpuidle_device *dev;

	if (disabled_by_idle_boot_param())
		return 0;

	if (!pr->flags.power_setup_done)
		return -ENODEV;

	dev = per_cpu(acpi_cpuidle_device, pr->id);
	cpuidle_pause_and_lock();
	cpuidle_disable_device(dev);
	ret = acpi_processor_get_power_info(pr);
	if (!ret && pr->flags.power) {
		acpi_processor_setup_cpuidle_dev(pr, dev);
		ret = cpuidle_enable_device(dev);
	}
	cpuidle_resume_and_unlock();

	return ret;
}

int acpi_processor_power_state_has_changed(struct acpi_processor *pr)
{
	int cpu;
	struct acpi_processor *_pr;
	struct cpuidle_device *dev;

	if (disabled_by_idle_boot_param())
		return 0;

	if (!pr->flags.power_setup_done)
		return -ENODEV;

	/*
	 * FIXME:  Design the ACPI notification to make it once per
	 * system instead of once per-cpu.  This condition is a hack
	 * to make the code that updates C-States be called once.
	 */

	if (pr->id == 0 && cpuidle_get_driver() == &acpi_idle_driver) {

		/* Protect against cpu-hotplug */
		get_online_cpus();
		cpuidle_pause_and_lock();

		/* Disable all cpuidle devices */
		for_each_online_cpu(cpu) {
			_pr = per_cpu(processors, cpu);
			if (!_pr || !_pr->flags.power_setup_done)
				continue;
			dev = per_cpu(acpi_cpuidle_device, cpu);
			cpuidle_disable_device(dev);
		}

		/* Populate Updated C-state information */
		acpi_processor_get_power_info(pr);
		acpi_processor_setup_cpuidle_states(pr);

		/* Enable all cpuidle devices */
		for_each_online_cpu(cpu) {
			_pr = per_cpu(processors, cpu);
			if (!_pr || !_pr->flags.power_setup_done)
				continue;
			acpi_processor_get_power_info(_pr);
			if (_pr->flags.power) {
				dev = per_cpu(acpi_cpuidle_device, cpu);
				acpi_processor_setup_cpuidle_dev(_pr, dev);
				cpuidle_enable_device(dev);
			}
		}
		cpuidle_resume_and_unlock();
		put_online_cpus();
	}

	return 0;
}

static int acpi_processor_registered;

int acpi_processor_power_init(struct acpi_processor *pr)
{
	int retval;
	struct cpuidle_device *dev;

	if (disabled_by_idle_boot_param())
		return 0;

	acpi_processor_cstate_first_run_checks();

	if (!acpi_processor_get_power_info(pr))
		pr->flags.power_setup_done = 1;

	/*
	 * Install the idle handler if processor power management is supported.
	 * Note that we use previously set idle handler will be used on
	 * platforms that only support C1.
	 */
	if (pr->flags.power) {
		/* Register acpi_idle_driver if not already registered */
		if (!acpi_processor_registered) {
			acpi_processor_setup_cpuidle_states(pr);
			retval = cpuidle_register_driver(&acpi_idle_driver);
			if (retval)
				return retval;
			pr_debug("%s registered with cpuidle\n",
				 acpi_idle_driver.name);
		}

		dev = kzalloc(sizeof(*dev), GFP_KERNEL);
		if (!dev)
			return -ENOMEM;
		per_cpu(acpi_cpuidle_device, pr->id) = dev;

		acpi_processor_setup_cpuidle_dev(pr, dev);

		/* Register per-cpu cpuidle_device. Cpuidle driver
		 * must already be registered before registering device
		 */
		retval = cpuidle_register_device(dev);
		if (retval) {
			if (acpi_processor_registered == 0)
				cpuidle_unregister_driver(&acpi_idle_driver);
			return retval;
		}
		acpi_processor_registered++;
	}
	return 0;
}

int acpi_processor_power_exit(struct acpi_processor *pr)
{
	struct cpuidle_device *dev = per_cpu(acpi_cpuidle_device, pr->id);

	if (disabled_by_idle_boot_param())
		return 0;

	if (pr->flags.power) {
		cpuidle_unregister_device(dev);
		acpi_processor_registered--;
		if (acpi_processor_registered == 0)
			cpuidle_unregister_driver(&acpi_idle_driver);
	}

	pr->flags.power_setup_done = 0;
	return 0;
}
