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
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or (at
 *  your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#include <linux/module.h>
#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/sched.h>       /* need_resched() */
#include <linux/tick.h>
#include <linux/cpuidle.h>
#include <linux/syscore_ops.h>
#include <acpi/processor.h>

/*
 * Include the apic definitions for x86 to have the APIC timer related defines
 * available also for UP (on SMP it gets magically included via linux/smp.h).
 * asm/acpi.h is not an option, as it would require more include magic. Also
 * creating an empty asm-ia64/apic.h would just trade pest vs. cholera.
 */
#ifdef CONFIG_X86
#include <asm/apic.h>
#endif

#define PREFIX "ACPI: "

#define ACPI_PROCESSOR_CLASS            "processor"
#define _COMPONENT              ACPI_PROCESSOR_COMPONENT
ACPI_MODULE_NAME("processor_idle");

static unsigned int max_cstate __read_mostly = ACPI_PROCESSOR_MAX_POWER;
module_param(max_cstate, uint, 0000);
static unsigned int nocst __read_mostly;
module_param(nocst, uint, 0000);
static int bm_check_disable __read_mostly;
module_param(bm_check_disable, uint, 0000);

static unsigned int latency_factor __read_mostly = 2;
module_param(latency_factor, uint, 0644);

static DEFINE_PER_CPU(struct cpuidle_device *, acpi_cpuidle_device);

static DEFINE_PER_CPU(struct acpi_processor_cx * [CPUIDLE_STATE_MAX],
								acpi_cstate);

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

	printk(KERN_NOTICE PREFIX "%s detected - limiting to C%ld max_cstate."
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
static void acpi_safe_halt(void)
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

	if (amd_e400_c1e_detected)
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
static void lapic_timer_state_broadcast(struct acpi_processor *pr,
				       struct acpi_processor_cx *cx,
				       int broadcast)
{
	int state = cx - pr->power.states;

	if (state >= pr->power.timer_broadcast_on_state) {
		if (broadcast)
			tick_broadcast_enter();
		else
			tick_broadcast_exit();
	}
}

#else

static void lapic_timer_check_state(int state, struct acpi_processor *pr,
				   struct acpi_processor_cx *cstate) { }
static void lapic_timer_propagate_broadcast(struct acpi_processor *pr) { }
static void lapic_timer_state_broadcast(struct acpi_processor *pr,
				       struct acpi_processor_cx *cx,
				       int broadcast)
{
}

#endif

#ifdef CONFIG_PM_SLEEP
static u32 saved_bm_rld;

static int acpi_processor_suspend(void)
{
	acpi_read_bit_register(ACPI_BITREG_BUS_MASTER_RLD, &saved_bm_rld);
	return 0;
}

static void acpi_processor_resume(void)
{
	u32 resumed_bm_rld = 0;

	acpi_read_bit_register(ACPI_BITREG_BUS_MASTER_RLD, &resumed_bm_rld);
	if (resumed_bm_rld == saved_bm_rld)
		return;

	acpi_write_bit_register(ACPI_BITREG_BUS_MASTER_RLD, saved_bm_rld);
}

static struct syscore_ops acpi_processor_syscore_ops = {
	.suspend = acpi_processor_suspend,
	.resume = acpi_processor_resume,
};

void acpi_processor_syscore_init(void)
{
	register_syscore_ops(&acpi_processor_syscore_ops);
}

void acpi_processor_syscore_exit(void)
{
	unregister_syscore_ops(&acpi_processor_syscore_ops);
}
#endif /* CONFIG_PM_SLEEP */

#if defined(CONFIG_X86)
static void tsc_check_state(int state)
{
	switch (boot_cpu_data.x86_vendor) {
	case X86_VENDOR_AMD:
	case X86_VENDOR_INTEL:
		/*
		 * AMD Fam10h TSC will tick in all
		 * C/P/S0/S1 states when this bit is set.
		 */
		if (boot_cpu_has(X86_FEATURE_NONSTOP_TSC))
			return;

		/*FALL THROUGH*/
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
	}
	/* the C0 state only exists as a filler in our array */
	pr->power.states[ACPI_STATE_C0].valid = 1;
	return 0;
}

static int acpi_processor_get_power_info_cst(struct acpi_processor *pr)
{
	acpi_status status;
	u64 count;
	int current_count;
	int i, ret = 0;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *cst;


	if (nocst)
		return -ENODEV;

	current_count = 0;

	status = acpi_evaluate_object(pr->handle, "_CST", NULL, &buffer);
	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "No _CST, giving up\n"));
		return -ENODEV;
	}

	cst = buffer.pointer;

	/* There must be at least 2 elements */
	if (!cst || (cst->type != ACPI_TYPE_PACKAGE) || cst->package.count < 2) {
		printk(KERN_ERR PREFIX "not enough elements in _CST\n");
		ret = -EFAULT;
		goto end;
	}

	count = cst->package.elements[0].integer.value;

	/* Validate number of power states. */
	if (count < 1 || count != cst->package.count - 1) {
		printk(KERN_ERR PREFIX "count given by _CST is not valid\n");
		ret = -EFAULT;
		goto end;
	}

	/* Tell driver that at least _CST is supported. */
	pr->flags.has_cst = 1;

	for (i = 1; i <= count; i++) {
		union acpi_object *element;
		union acpi_object *obj;
		struct acpi_power_register *reg;
		struct acpi_processor_cx cx;

		memset(&cx, 0, sizeof(cx));

		element = &(cst->package.elements[i]);
		if (element->type != ACPI_TYPE_PACKAGE)
			continue;

		if (element->package.count != 4)
			continue;

		obj = &(element->package.elements[0]);

		if (obj->type != ACPI_TYPE_BUFFER)
			continue;

		reg = (struct acpi_power_register *)obj->buffer.pointer;

		if (reg->space_id != ACPI_ADR_SPACE_SYSTEM_IO &&
		    (reg->space_id != ACPI_ADR_SPACE_FIXED_HARDWARE))
			continue;

		/* There should be an easy way to extract an integer... */
		obj = &(element->package.elements[1]);
		if (obj->type != ACPI_TYPE_INTEGER)
			continue;

		cx.type = obj->integer.value;
		/*
		 * Some buggy BIOSes won't list C1 in _CST -
		 * Let acpi_processor_get_power_info_default() handle them later
		 */
		if (i == 1 && cx.type != ACPI_STATE_C1)
			current_count++;

		cx.address = reg->address;
		cx.index = current_count + 1;

		cx.entry_method = ACPI_CSTATE_SYSTEMIO;
		if (reg->space_id == ACPI_ADR_SPACE_FIXED_HARDWARE) {
			if (acpi_processor_ffh_cstate_probe
					(pr->id, &cx, reg) == 0) {
				cx.entry_method = ACPI_CSTATE_FFH;
			} else if (cx.type == ACPI_STATE_C1) {
				/*
				 * C1 is a special case where FIXED_HARDWARE
				 * can be handled in non-MWAIT way as well.
				 * In that case, save this _CST entry info.
				 * Otherwise, ignore this info and continue.
				 */
				cx.entry_method = ACPI_CSTATE_HALT;
				snprintf(cx.desc, ACPI_CX_DESC_LEN, "ACPI HLT");
			} else {
				continue;
			}
			if (cx.type == ACPI_STATE_C1 &&
			    (boot_option_idle_override == IDLE_NOMWAIT)) {
				/*
				 * In most cases the C1 space_id obtained from
				 * _CST object is FIXED_HARDWARE access mode.
				 * But when the option of idle=halt is added,
				 * the entry_method type should be changed from
				 * CSTATE_FFH to CSTATE_HALT.
				 * When the option of idle=nomwait is added,
				 * the C1 entry_method type should be
				 * CSTATE_HALT.
				 */
				cx.entry_method = ACPI_CSTATE_HALT;
				snprintf(cx.desc, ACPI_CX_DESC_LEN, "ACPI HLT");
			}
		} else {
			snprintf(cx.desc, ACPI_CX_DESC_LEN, "ACPI IOPORT 0x%x",
				 cx.address);
		}

		if (cx.type == ACPI_STATE_C1) {
			cx.valid = 1;
		}

		obj = &(element->package.elements[2]);
		if (obj->type != ACPI_TYPE_INTEGER)
			continue;

		cx.latency = obj->integer.value;

		obj = &(element->package.elements[3]);
		if (obj->type != ACPI_TYPE_INTEGER)
			continue;

		current_count++;
		memcpy(&(pr->power.states[current_count]), &cx, sizeof(cx));

		/*
		 * We support total ACPI_PROCESSOR_MAX_POWER - 1
		 * (From 1 through ACPI_PROCESSOR_MAX_POWER - 1)
		 */
		if (current_count >= (ACPI_PROCESSOR_MAX_POWER - 1)) {
			printk(KERN_WARNING
			       "Limiting number of power states to max (%d)\n",
			       ACPI_PROCESSOR_MAX_POWER);
			printk(KERN_WARNING
			       "Please increase ACPI_PROCESSOR_MAX_POWER if needed.\n");
			break;
		}
	}

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Found %d power states\n",
			  current_count));

	/* Validate number of power states discovered */
	if (current_count < 2)
		ret = -EFAULT;

      end:
	kfree(buffer.pointer);

	return ret;
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

static int acpi_processor_power_verify(struct acpi_processor *pr)
{
	unsigned int i;
	unsigned int working = 0;

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

		lapic_timer_check_state(i, pr, cx);
		tsc_check_state(cx->type);
		working++;
	}

	lapic_timer_propagate_broadcast(pr);

	return (working);
}

static int acpi_processor_get_power_info(struct acpi_processor *pr)
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
			if (pr->power.states[i].type >= ACPI_STATE_C2)
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

/**
 * acpi_idle_do_entry - enter idle state using the appropriate method
 * @cx: cstate data
 *
 * Caller disables interrupt before call and enables interrupt after return.
 */
static void acpi_idle_do_entry(struct acpi_processor_cx *cx)
{
	if (cx->entry_method == ACPI_CSTATE_FFH) {
		/* Call into architectural FFH based C-state */
		acpi_processor_ffh_cstate_enter(cx);
	} else if (cx->entry_method == ACPI_CSTATE_HALT) {
		acpi_safe_halt();
	} else {
		/* IO port based C-state */
		inb(cx->address);
		/* Dummy wait op - must do something useless after P_LVL2 read
		   because chipsets cannot guarantee that STPCLK# signal
		   gets asserted in time to freeze execution properly. */
		inl(acpi_gbl_FADT.xpm_timer_block.address);
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
			/* See comment in acpi_idle_do_entry() */
			inl(acpi_gbl_FADT.xpm_timer_block.address);
		} else
			return -ENODEV;
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
 * @pr: Target processor
 * @cx: Target state context
 * @timer_bc: Whether or not to change timer mode to broadcast
 */
static void acpi_idle_enter_bm(struct acpi_processor *pr,
			       struct acpi_processor_cx *cx, bool timer_bc)
{
	acpi_unlazy_tlb(smp_processor_id());

	/*
	 * Must be done before busmaster disable as we might need to
	 * access HPET !
	 */
	if (timer_bc)
		lapic_timer_state_broadcast(pr, cx, 1);

	/*
	 * disable bus master
	 * bm_check implies we need ARB_DIS
	 * bm_control implies whether we can do ARB_DIS
	 *
	 * That leaves a case where bm_check is set and bm_control is
	 * not set. In that case we cannot do much, we enter C3
	 * without doing anything.
	 */
	if (pr->flags.bm_control) {
		raw_spin_lock(&c3_lock);
		c3_cpu_count++;
		/* Disable bus master arbitration when all CPUs are in C3 */
		if (c3_cpu_count == num_online_cpus())
			acpi_write_bit_register(ACPI_BITREG_ARB_DISABLE, 1);
		raw_spin_unlock(&c3_lock);
	}

	acpi_idle_do_entry(cx);

	/* Re-enable bus master arbitration */
	if (pr->flags.bm_control) {
		raw_spin_lock(&c3_lock);
		acpi_write_bit_register(ACPI_BITREG_ARB_DISABLE, 0);
		c3_cpu_count--;
		raw_spin_unlock(&c3_lock);
	}

	if (timer_bc)
		lapic_timer_state_broadcast(pr, cx, 0);
}

static int acpi_idle_enter(struct cpuidle_device *dev,
			   struct cpuidle_driver *drv, int index)
{
	struct acpi_processor_cx *cx = per_cpu(acpi_cstate[index], dev->cpu);
	struct acpi_processor *pr;

	pr = __this_cpu_read(processors);
	if (unlikely(!pr))
		return -EINVAL;

	if (cx->type != ACPI_STATE_C1) {
		if (acpi_idle_fallback_to_c1(pr) && num_online_cpus() > 1) {
			index = CPUIDLE_DRIVER_STATE_START;
			cx = per_cpu(acpi_cstate[index], dev->cpu);
		} else if (cx->type == ACPI_STATE_C3 && pr->flags.bm_check) {
			if (cx->bm_sts_skip || !acpi_idle_bm_check()) {
				acpi_idle_enter_bm(pr, cx, true);
				return index;
			} else if (drv->safe_state_index >= 0) {
				index = drv->safe_state_index;
				cx = per_cpu(acpi_cstate[index], dev->cpu);
			} else {
				acpi_safe_halt();
				return -EBUSY;
			}
		}
	}

	lapic_timer_state_broadcast(pr, cx, 1);

	if (cx->type == ACPI_STATE_C3)
		ACPI_FLUSH_CPU_CACHE();

	acpi_idle_do_entry(cx);

	lapic_timer_state_broadcast(pr, cx, 0);

	return index;
}

static void acpi_idle_enter_freeze(struct cpuidle_device *dev,
				   struct cpuidle_driver *drv, int index)
{
	struct acpi_processor_cx *cx = per_cpu(acpi_cstate[index], dev->cpu);

	if (cx->type == ACPI_STATE_C3) {
		struct acpi_processor *pr = __this_cpu_read(processors);

		if (unlikely(!pr))
			return;

		if (pr->flags.bm_check) {
			acpi_idle_enter_bm(pr, cx, false);
			return;
		} else {
			ACPI_FLUSH_CPU_CACHE();
		}
	}
	acpi_idle_do_entry(cx);
}

struct cpuidle_driver acpi_idle_driver = {
	.name =		"acpi_idle",
	.owner =	THIS_MODULE,
};

/**
 * acpi_processor_setup_cpuidle_cx - prepares and configures CPUIDLE
 * device i.e. per-cpu data
 *
 * @pr: the ACPI processor
 * @dev : the cpuidle device
 */
static int acpi_processor_setup_cpuidle_cx(struct acpi_processor *pr,
					   struct cpuidle_device *dev)
{
	int i, count = CPUIDLE_DRIVER_STATE_START;
	struct acpi_processor_cx *cx;

	if (!pr->flags.power_setup_done)
		return -EINVAL;

	if (pr->flags.power == 0) {
		return -EINVAL;
	}

	if (!dev)
		return -EINVAL;

	dev->cpu = pr->id;

	if (max_cstate == 0)
		max_cstate = 1;

	for (i = 1; i < ACPI_PROCESSOR_MAX_POWER && i <= max_cstate; i++) {
		cx = &pr->power.states[i];

		if (!cx->valid)
			continue;

		per_cpu(acpi_cstate[count], dev->cpu) = cx;

		count++;
		if (count == CPUIDLE_STATE_MAX)
			break;
	}

	if (!count)
		return -EINVAL;

	return 0;
}

/**
 * acpi_processor_setup_cpuidle states- prepares and configures cpuidle
 * global state data i.e. idle routines
 *
 * @pr: the ACPI processor
 */
static int acpi_processor_setup_cpuidle_states(struct acpi_processor *pr)
{
	int i, count = CPUIDLE_DRIVER_STATE_START;
	struct acpi_processor_cx *cx;
	struct cpuidle_state *state;
	struct cpuidle_driver *drv = &acpi_idle_driver;

	if (!pr->flags.power_setup_done)
		return -EINVAL;

	if (pr->flags.power == 0)
		return -EINVAL;

	drv->safe_state_index = -1;
	for (i = CPUIDLE_DRIVER_STATE_START; i < CPUIDLE_STATE_MAX; i++) {
		drv->states[i].name[0] = '\0';
		drv->states[i].desc[0] = '\0';
	}

	if (max_cstate == 0)
		max_cstate = 1;

	for (i = 1; i < ACPI_PROCESSOR_MAX_POWER && i <= max_cstate; i++) {
		cx = &pr->power.states[i];

		if (!cx->valid)
			continue;

		state = &drv->states[count];
		snprintf(state->name, CPUIDLE_NAME_LEN, "C%d", i);
		strncpy(state->desc, cx->desc, CPUIDLE_DESC_LEN);
		state->exit_latency = cx->latency;
		state->target_residency = cx->latency * latency_factor;
		state->enter = acpi_idle_enter;

		state->flags = 0;
		if (cx->type == ACPI_STATE_C1 || cx->type == ACPI_STATE_C2) {
			state->enter_dead = acpi_idle_play_dead;
			drv->safe_state_index = count;
		}
		/*
		 * Halt-induced C1 is not good for ->enter_freeze, because it
		 * re-enables interrupts on exit.  Moreover, C1 is generally not
		 * particularly interesting from the suspend-to-idle angle, so
		 * avoid C1 and the situations in which we may need to fall back
		 * to it altogether.
		 */
		if (cx->type != ACPI_STATE_C1 && !acpi_idle_fallback_to_c1(pr))
			state->enter_freeze = acpi_idle_enter_freeze;

		count++;
		if (count == CPUIDLE_STATE_MAX)
			break;
	}

	drv->state_count = count;

	if (!count)
		return -EINVAL;

	return 0;
}

int acpi_processor_hotplug(struct acpi_processor *pr)
{
	int ret = 0;
	struct cpuidle_device *dev;

	if (disabled_by_idle_boot_param())
		return 0;

	if (nocst)
		return -ENODEV;

	if (!pr->flags.power_setup_done)
		return -ENODEV;

	dev = per_cpu(acpi_cpuidle_device, pr->id);
	cpuidle_pause_and_lock();
	cpuidle_disable_device(dev);
	acpi_processor_get_power_info(pr);
	if (pr->flags.power) {
		acpi_processor_setup_cpuidle_cx(pr, dev);
		ret = cpuidle_enable_device(dev);
	}
	cpuidle_resume_and_unlock();

	return ret;
}

int acpi_processor_cst_has_changed(struct acpi_processor *pr)
{
	int cpu;
	struct acpi_processor *_pr;
	struct cpuidle_device *dev;

	if (disabled_by_idle_boot_param())
		return 0;

	if (nocst)
		return -ENODEV;

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
				acpi_processor_setup_cpuidle_cx(_pr, dev);
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
	acpi_status status;
	int retval;
	struct cpuidle_device *dev;
	static int first_run;

	if (disabled_by_idle_boot_param())
		return 0;

	if (!first_run) {
		dmi_check_system(processor_power_dmi_table);
		max_cstate = acpi_processor_cstate_check(max_cstate);
		if (max_cstate < ACPI_C_STATES_MAX)
			printk(KERN_NOTICE
			       "ACPI: processor limited to max C-state %d\n",
			       max_cstate);
		first_run++;
	}

	if (acpi_gbl_FADT.cst_control && !nocst) {
		status =
		    acpi_os_write_port(acpi_gbl_FADT.smi_command, acpi_gbl_FADT.cst_control, 8);
		if (ACPI_FAILURE(status)) {
			ACPI_EXCEPTION((AE_INFO, status,
					"Notifying BIOS of _CST ability failed"));
		}
	}

	acpi_processor_get_power_info(pr);
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
			printk(KERN_DEBUG "ACPI: %s registered with cpuidle\n",
					acpi_idle_driver.name);
		}

		dev = kzalloc(sizeof(*dev), GFP_KERNEL);
		if (!dev)
			return -ENOMEM;
		per_cpu(acpi_cpuidle_device, pr->id) = dev;

		acpi_processor_setup_cpuidle_cx(pr, dev);

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
