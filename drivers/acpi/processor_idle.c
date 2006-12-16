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
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/moduleparam.h>
#include <linux/sched.h>	/* need_resched() */
#include <linux/latency.h>

#include <asm/io.h>
#include <asm/uaccess.h>

#include <acpi/acpi_bus.h>
#include <acpi/processor.h>

#define ACPI_PROCESSOR_COMPONENT        0x01000000
#define ACPI_PROCESSOR_CLASS            "processor"
#define ACPI_PROCESSOR_DRIVER_NAME      "ACPI Processor Driver"
#define _COMPONENT              ACPI_PROCESSOR_COMPONENT
ACPI_MODULE_NAME("acpi_processor")
#define ACPI_PROCESSOR_FILE_POWER	"power"
#define US_TO_PM_TIMER_TICKS(t)		((t * (PM_TIMER_FREQUENCY/1000)) / 1000)
#define C2_OVERHEAD			4	/* 1us (3.579 ticks per us) */
#define C3_OVERHEAD			4	/* 1us (3.579 ticks per us) */
static void (*pm_idle_save) (void) __read_mostly;
module_param(max_cstate, uint, 0644);

static unsigned int nocst __read_mostly;
module_param(nocst, uint, 0000);

/*
 * bm_history -- bit-mask with a bit per jiffy of bus-master activity
 * 1000 HZ: 0xFFFFFFFF: 32 jiffies = 32ms
 * 800 HZ: 0xFFFFFFFF: 32 jiffies = 40ms
 * 100 HZ: 0x0000000F: 4 jiffies = 40ms
 * reduce history for more aggressive entry into C3
 */
static unsigned int bm_history __read_mostly =
    (HZ >= 800 ? 0xFFFFFFFF : ((1U << (HZ / 25)) - 1));
module_param(bm_history, uint, 0644);
/* --------------------------------------------------------------------------
                                Power Management
   -------------------------------------------------------------------------- */

/*
 * IBM ThinkPad R40e crashes mysteriously when going into C2 or C3.
 * For now disable this. Probably a bug somewhere else.
 *
 * To skip this limit, boot/load with a large max_cstate limit.
 */
static int set_max_cstate(struct dmi_system_id *id)
{
	if (max_cstate > ACPI_PROCESSOR_MAX_POWER)
		return 0;

	printk(KERN_NOTICE PREFIX "%s detected - limiting to C%ld max_cstate."
	       " Override with \"processor.max_cstate=%d\"\n", id->ident,
	       (long)id->driver_data, ACPI_PROCESSOR_MAX_POWER + 1);

	max_cstate = (long)id->driver_data;

	return 0;
}

/* Actually this shouldn't be __cpuinitdata, would be better to fix the
   callers to only run once -AK */
static struct dmi_system_id __cpuinitdata processor_power_dmi_table[] = {
	{ set_max_cstate, "IBM ThinkPad R40e", {
	  DMI_MATCH(DMI_BIOS_VENDOR,"IBM"),
	  DMI_MATCH(DMI_BIOS_VERSION,"1SET70WW")}, (void *)1},
	{ set_max_cstate, "IBM ThinkPad R40e", {
	  DMI_MATCH(DMI_BIOS_VENDOR,"IBM"),
	  DMI_MATCH(DMI_BIOS_VERSION,"1SET60WW")}, (void *)1},
	{ set_max_cstate, "IBM ThinkPad R40e", {
	  DMI_MATCH(DMI_BIOS_VENDOR,"IBM"),
	  DMI_MATCH(DMI_BIOS_VERSION,"1SET43WW") }, (void*)1},
	{ set_max_cstate, "IBM ThinkPad R40e", {
	  DMI_MATCH(DMI_BIOS_VENDOR,"IBM"),
	  DMI_MATCH(DMI_BIOS_VERSION,"1SET45WW") }, (void*)1},
	{ set_max_cstate, "IBM ThinkPad R40e", {
	  DMI_MATCH(DMI_BIOS_VENDOR,"IBM"),
	  DMI_MATCH(DMI_BIOS_VERSION,"1SET47WW") }, (void*)1},
	{ set_max_cstate, "IBM ThinkPad R40e", {
	  DMI_MATCH(DMI_BIOS_VENDOR,"IBM"),
	  DMI_MATCH(DMI_BIOS_VERSION,"1SET50WW") }, (void*)1},
	{ set_max_cstate, "IBM ThinkPad R40e", {
	  DMI_MATCH(DMI_BIOS_VENDOR,"IBM"),
	  DMI_MATCH(DMI_BIOS_VERSION,"1SET52WW") }, (void*)1},
	{ set_max_cstate, "IBM ThinkPad R40e", {
	  DMI_MATCH(DMI_BIOS_VENDOR,"IBM"),
	  DMI_MATCH(DMI_BIOS_VERSION,"1SET55WW") }, (void*)1},
	{ set_max_cstate, "IBM ThinkPad R40e", {
	  DMI_MATCH(DMI_BIOS_VENDOR,"IBM"),
	  DMI_MATCH(DMI_BIOS_VERSION,"1SET56WW") }, (void*)1},
	{ set_max_cstate, "IBM ThinkPad R40e", {
	  DMI_MATCH(DMI_BIOS_VENDOR,"IBM"),
	  DMI_MATCH(DMI_BIOS_VERSION,"1SET59WW") }, (void*)1},
	{ set_max_cstate, "IBM ThinkPad R40e", {
	  DMI_MATCH(DMI_BIOS_VENDOR,"IBM"),
	  DMI_MATCH(DMI_BIOS_VERSION,"1SET60WW") }, (void*)1},
	{ set_max_cstate, "IBM ThinkPad R40e", {
	  DMI_MATCH(DMI_BIOS_VENDOR,"IBM"),
	  DMI_MATCH(DMI_BIOS_VERSION,"1SET61WW") }, (void*)1},
	{ set_max_cstate, "IBM ThinkPad R40e", {
	  DMI_MATCH(DMI_BIOS_VENDOR,"IBM"),
	  DMI_MATCH(DMI_BIOS_VERSION,"1SET62WW") }, (void*)1},
	{ set_max_cstate, "IBM ThinkPad R40e", {
	  DMI_MATCH(DMI_BIOS_VENDOR,"IBM"),
	  DMI_MATCH(DMI_BIOS_VERSION,"1SET64WW") }, (void*)1},
	{ set_max_cstate, "IBM ThinkPad R40e", {
	  DMI_MATCH(DMI_BIOS_VENDOR,"IBM"),
	  DMI_MATCH(DMI_BIOS_VERSION,"1SET65WW") }, (void*)1},
	{ set_max_cstate, "IBM ThinkPad R40e", {
	  DMI_MATCH(DMI_BIOS_VENDOR,"IBM"),
	  DMI_MATCH(DMI_BIOS_VERSION,"1SET68WW") }, (void*)1},
	{ set_max_cstate, "Medion 41700", {
	  DMI_MATCH(DMI_BIOS_VENDOR,"Phoenix Technologies LTD"),
	  DMI_MATCH(DMI_BIOS_VERSION,"R01-A1J")}, (void *)1},
	{ set_max_cstate, "Clevo 5600D", {
	  DMI_MATCH(DMI_BIOS_VENDOR,"Phoenix Technologies LTD"),
	  DMI_MATCH(DMI_BIOS_VERSION,"SHE845M0.86C.0013.D.0302131307")},
	 (void *)2},
	{},
};

static inline u32 ticks_elapsed(u32 t1, u32 t2)
{
	if (t2 >= t1)
		return (t2 - t1);
	else if (!acpi_fadt.tmr_val_ext)
		return (((0x00FFFFFF - t1) + t2) & 0x00FFFFFF);
	else
		return ((0xFFFFFFFF - t1) + t2);
}

static void
acpi_processor_power_activate(struct acpi_processor *pr,
			      struct acpi_processor_cx *new)
{
	struct acpi_processor_cx *old;

	if (!pr || !new)
		return;

	old = pr->power.state;

	if (old)
		old->promotion.count = 0;
	new->demotion.count = 0;

	/* Cleanup from old state. */
	if (old) {
		switch (old->type) {
		case ACPI_STATE_C3:
			/* Disable bus master reload */
			if (new->type != ACPI_STATE_C3 && pr->flags.bm_check)
				acpi_set_register(ACPI_BITREG_BUS_MASTER_RLD, 0,
						  ACPI_MTX_DO_NOT_LOCK);
			break;
		}
	}

	/* Prepare to use new state. */
	switch (new->type) {
	case ACPI_STATE_C3:
		/* Enable bus master reload */
		if (old->type != ACPI_STATE_C3 && pr->flags.bm_check)
			acpi_set_register(ACPI_BITREG_BUS_MASTER_RLD, 1,
					  ACPI_MTX_DO_NOT_LOCK);
		break;
	}

	pr->power.state = new;

	return;
}

static void acpi_safe_halt(void)
{
	current_thread_info()->status &= ~TS_POLLING;
	smp_mb__after_clear_bit();
	if (!need_resched())
		safe_halt();
	current_thread_info()->status |= TS_POLLING;
}

static atomic_t c3_cpu_count;

/* Common C-state entry for C2, C3, .. */
static void acpi_cstate_enter(struct acpi_processor_cx *cstate)
{
	if (cstate->space_id == ACPI_CSTATE_FFH) {
		/* Call into architectural FFH based C-state */
		acpi_processor_ffh_cstate_enter(cstate);
	} else {
		int unused;
		/* IO port based C-state */
		inb(cstate->address);
		/* Dummy wait op - must do something useless after P_LVL2 read
		   because chipsets cannot guarantee that STPCLK# signal
		   gets asserted in time to freeze execution properly. */
		unused = inl(acpi_fadt.xpm_tmr_blk.address);
	}
}

static void acpi_processor_idle(void)
{
	struct acpi_processor *pr = NULL;
	struct acpi_processor_cx *cx = NULL;
	struct acpi_processor_cx *next_state = NULL;
	int sleep_ticks = 0;
	u32 t1, t2 = 0;

	pr = processors[smp_processor_id()];
	if (!pr)
		return;

	/*
	 * Interrupts must be disabled during bus mastering calculations and
	 * for C2/C3 transitions.
	 */
	local_irq_disable();

	/*
	 * Check whether we truly need to go idle, or should
	 * reschedule:
	 */
	if (unlikely(need_resched())) {
		local_irq_enable();
		return;
	}

	cx = pr->power.state;
	if (!cx) {
		if (pm_idle_save)
			pm_idle_save();
		else
			acpi_safe_halt();
		return;
	}

	/*
	 * Check BM Activity
	 * -----------------
	 * Check for bus mastering activity (if required), record, and check
	 * for demotion.
	 */
	if (pr->flags.bm_check) {
		u32 bm_status = 0;
		unsigned long diff = jiffies - pr->power.bm_check_timestamp;

		if (diff > 31)
			diff = 31;

		pr->power.bm_activity <<= diff;

		acpi_get_register(ACPI_BITREG_BUS_MASTER_STATUS,
				  &bm_status, ACPI_MTX_DO_NOT_LOCK);
		if (bm_status) {
			pr->power.bm_activity |= 0x1;
			acpi_set_register(ACPI_BITREG_BUS_MASTER_STATUS,
					  1, ACPI_MTX_DO_NOT_LOCK);
		}
		/*
		 * PIIX4 Erratum #18: Note that BM_STS doesn't always reflect
		 * the true state of bus mastering activity; forcing us to
		 * manually check the BMIDEA bit of each IDE channel.
		 */
		else if (errata.piix4.bmisx) {
			if ((inb_p(errata.piix4.bmisx + 0x02) & 0x01)
			    || (inb_p(errata.piix4.bmisx + 0x0A) & 0x01))
				pr->power.bm_activity |= 0x1;
		}

		pr->power.bm_check_timestamp = jiffies;

		/*
		 * If bus mastering is or was active this jiffy, demote
		 * to avoid a faulty transition.  Note that the processor
		 * won't enter a low-power state during this call (to this
		 * function) but should upon the next.
		 *
		 * TBD: A better policy might be to fallback to the demotion
		 *      state (use it for this quantum only) istead of
		 *      demoting -- and rely on duration as our sole demotion
		 *      qualification.  This may, however, introduce DMA
		 *      issues (e.g. floppy DMA transfer overrun/underrun).
		 */
		if ((pr->power.bm_activity & 0x1) &&
		    cx->demotion.threshold.bm) {
			local_irq_enable();
			next_state = cx->demotion.state;
			goto end;
		}
	}

#ifdef CONFIG_HOTPLUG_CPU
	/*
	 * Check for P_LVL2_UP flag before entering C2 and above on
	 * an SMP system. We do it here instead of doing it at _CST/P_LVL
	 * detection phase, to work cleanly with logical CPU hotplug.
	 */
	if ((cx->type != ACPI_STATE_C1) && (num_online_cpus() > 1) && 
	    !pr->flags.has_cst && !acpi_fadt.plvl2_up)
		cx = &pr->power.states[ACPI_STATE_C1];
#endif

	/*
	 * Sleep:
	 * ------
	 * Invoke the current Cx state to put the processor to sleep.
	 */
	if (cx->type == ACPI_STATE_C2 || cx->type == ACPI_STATE_C3) {
		current_thread_info()->status &= ~TS_POLLING;
		smp_mb__after_clear_bit();
		if (need_resched()) {
			current_thread_info()->status |= TS_POLLING;
			local_irq_enable();
			return;
		}
	}

	switch (cx->type) {

	case ACPI_STATE_C1:
		/*
		 * Invoke C1.
		 * Use the appropriate idle routine, the one that would
		 * be used without acpi C-states.
		 */
		if (pm_idle_save)
			pm_idle_save();
		else
			acpi_safe_halt();

		/*
		 * TBD: Can't get time duration while in C1, as resumes
		 *      go to an ISR rather than here.  Need to instrument
		 *      base interrupt handler.
		 */
		sleep_ticks = 0xFFFFFFFF;
		break;

	case ACPI_STATE_C2:
		/* Get start time (ticks) */
		t1 = inl(acpi_fadt.xpm_tmr_blk.address);
		/* Invoke C2 */
		acpi_cstate_enter(cx);
		/* Get end time (ticks) */
		t2 = inl(acpi_fadt.xpm_tmr_blk.address);

#ifdef CONFIG_GENERIC_TIME
		/* TSC halts in C2, so notify users */
		mark_tsc_unstable();
#endif
		/* Re-enable interrupts */
		local_irq_enable();
		current_thread_info()->status |= TS_POLLING;
		/* Compute time (ticks) that we were actually asleep */
		sleep_ticks =
		    ticks_elapsed(t1, t2) - cx->latency_ticks - C2_OVERHEAD;
		break;

	case ACPI_STATE_C3:

		if (pr->flags.bm_check) {
			if (atomic_inc_return(&c3_cpu_count) ==
			    num_online_cpus()) {
				/*
				 * All CPUs are trying to go to C3
				 * Disable bus master arbitration
				 */
				acpi_set_register(ACPI_BITREG_ARB_DISABLE, 1,
						  ACPI_MTX_DO_NOT_LOCK);
			}
		} else {
			/* SMP with no shared cache... Invalidate cache  */
			ACPI_FLUSH_CPU_CACHE();
		}

		/* Get start time (ticks) */
		t1 = inl(acpi_fadt.xpm_tmr_blk.address);
		/* Invoke C3 */
		acpi_cstate_enter(cx);
		/* Get end time (ticks) */
		t2 = inl(acpi_fadt.xpm_tmr_blk.address);
		if (pr->flags.bm_check) {
			/* Enable bus master arbitration */
			atomic_dec(&c3_cpu_count);
			acpi_set_register(ACPI_BITREG_ARB_DISABLE, 0,
					  ACPI_MTX_DO_NOT_LOCK);
		}

#ifdef CONFIG_GENERIC_TIME
		/* TSC halts in C3, so notify users */
		mark_tsc_unstable();
#endif
		/* Re-enable interrupts */
		local_irq_enable();
		current_thread_info()->status |= TS_POLLING;
		/* Compute time (ticks) that we were actually asleep */
		sleep_ticks =
		    ticks_elapsed(t1, t2) - cx->latency_ticks - C3_OVERHEAD;
		break;

	default:
		local_irq_enable();
		return;
	}
	cx->usage++;
	if ((cx->type != ACPI_STATE_C1) && (sleep_ticks > 0))
		cx->time += sleep_ticks;

	next_state = pr->power.state;

#ifdef CONFIG_HOTPLUG_CPU
	/* Don't do promotion/demotion */
	if ((cx->type == ACPI_STATE_C1) && (num_online_cpus() > 1) &&
	    !pr->flags.has_cst && !acpi_fadt.plvl2_up) {
		next_state = cx;
		goto end;
	}
#endif

	/*
	 * Promotion?
	 * ----------
	 * Track the number of longs (time asleep is greater than threshold)
	 * and promote when the count threshold is reached.  Note that bus
	 * mastering activity may prevent promotions.
	 * Do not promote above max_cstate.
	 */
	if (cx->promotion.state &&
	    ((cx->promotion.state - pr->power.states) <= max_cstate)) {
		if (sleep_ticks > cx->promotion.threshold.ticks &&
		  cx->promotion.state->latency <= system_latency_constraint()) {
			cx->promotion.count++;
			cx->demotion.count = 0;
			if (cx->promotion.count >=
			    cx->promotion.threshold.count) {
				if (pr->flags.bm_check) {
					if (!
					    (pr->power.bm_activity & cx->
					     promotion.threshold.bm)) {
						next_state =
						    cx->promotion.state;
						goto end;
					}
				} else {
					next_state = cx->promotion.state;
					goto end;
				}
			}
		}
	}

	/*
	 * Demotion?
	 * ---------
	 * Track the number of shorts (time asleep is less than time threshold)
	 * and demote when the usage threshold is reached.
	 */
	if (cx->demotion.state) {
		if (sleep_ticks < cx->demotion.threshold.ticks) {
			cx->demotion.count++;
			cx->promotion.count = 0;
			if (cx->demotion.count >= cx->demotion.threshold.count) {
				next_state = cx->demotion.state;
				goto end;
			}
		}
	}

      end:
	/*
	 * Demote if current state exceeds max_cstate
	 * or if the latency of the current state is unacceptable
	 */
	if ((pr->power.state - pr->power.states) > max_cstate ||
		pr->power.state->latency > system_latency_constraint()) {
		if (cx->demotion.state)
			next_state = cx->demotion.state;
	}

	/*
	 * New Cx State?
	 * -------------
	 * If we're going to start using a new Cx state we must clean up
	 * from the previous and prepare to use the new.
	 */
	if (next_state != pr->power.state)
		acpi_processor_power_activate(pr, next_state);
}

static int acpi_processor_set_power_policy(struct acpi_processor *pr)
{
	unsigned int i;
	unsigned int state_is_set = 0;
	struct acpi_processor_cx *lower = NULL;
	struct acpi_processor_cx *higher = NULL;
	struct acpi_processor_cx *cx;


	if (!pr)
		return -EINVAL;

	/*
	 * This function sets the default Cx state policy (OS idle handler).
	 * Our scheme is to promote quickly to C2 but more conservatively
	 * to C3.  We're favoring C2  for its characteristics of low latency
	 * (quick response), good power savings, and ability to allow bus
	 * mastering activity.  Note that the Cx state policy is completely
	 * customizable and can be altered dynamically.
	 */

	/* startup state */
	for (i = 1; i < ACPI_PROCESSOR_MAX_POWER; i++) {
		cx = &pr->power.states[i];
		if (!cx->valid)
			continue;

		if (!state_is_set)
			pr->power.state = cx;
		state_is_set++;
		break;
	}

	if (!state_is_set)
		return -ENODEV;

	/* demotion */
	for (i = 1; i < ACPI_PROCESSOR_MAX_POWER; i++) {
		cx = &pr->power.states[i];
		if (!cx->valid)
			continue;

		if (lower) {
			cx->demotion.state = lower;
			cx->demotion.threshold.ticks = cx->latency_ticks;
			cx->demotion.threshold.count = 1;
			if (cx->type == ACPI_STATE_C3)
				cx->demotion.threshold.bm = bm_history;
		}

		lower = cx;
	}

	/* promotion */
	for (i = (ACPI_PROCESSOR_MAX_POWER - 1); i > 0; i--) {
		cx = &pr->power.states[i];
		if (!cx->valid)
			continue;

		if (higher) {
			cx->promotion.state = higher;
			cx->promotion.threshold.ticks = cx->latency_ticks;
			if (cx->type >= ACPI_STATE_C2)
				cx->promotion.threshold.count = 4;
			else
				cx->promotion.threshold.count = 10;
			if (higher->type == ACPI_STATE_C3)
				cx->promotion.threshold.bm = bm_history;
		}

		higher = cx;
	}

	return 0;
}

static int acpi_processor_get_power_info_fadt(struct acpi_processor *pr)
{

	if (!pr)
		return -EINVAL;

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
	if ((num_online_cpus() > 1) && !acpi_fadt.plvl2_up)
		return -ENODEV;
#endif

	/* determine C2 and C3 address from pblk */
	pr->power.states[ACPI_STATE_C2].address = pr->pblk + 4;
	pr->power.states[ACPI_STATE_C3].address = pr->pblk + 5;

	/* determine latencies from FADT */
	pr->power.states[ACPI_STATE_C2].latency = acpi_fadt.plvl2_lat;
	pr->power.states[ACPI_STATE_C3].latency = acpi_fadt.plvl3_lat;

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
	}
	/* the C0 state only exists as a filler in our array */
	pr->power.states[ACPI_STATE_C0].valid = 1;
	return 0;
}

static int acpi_processor_get_power_info_cst(struct acpi_processor *pr)
{
	acpi_status status = 0;
	acpi_integer count;
	int current_count;
	int i;
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
		status = -EFAULT;
		goto end;
	}

	count = cst->package.elements[0].integer.value;

	/* Validate number of power states. */
	if (count < 1 || count != cst->package.count - 1) {
		printk(KERN_ERR PREFIX "count given by _CST is not valid\n");
		status = -EFAULT;
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

		cx.space_id = ACPI_CSTATE_SYSTEMIO;
		if (reg->space_id == ACPI_ADR_SPACE_FIXED_HARDWARE) {
			if (acpi_processor_ffh_cstate_probe
					(pr->id, &cx, reg) == 0) {
				cx.space_id = ACPI_CSTATE_FFH;
			} else if (cx.type != ACPI_STATE_C1) {
				/*
				 * C1 is a special case where FIXED_HARDWARE
				 * can be handled in non-MWAIT way as well.
				 * In that case, save this _CST entry info.
				 * That is, we retain space_id of SYSTEM_IO for
				 * halt based C1.
				 * Otherwise, ignore this info and continue.
				 */
				continue;
			}
		}

		obj = &(element->package.elements[2]);
		if (obj->type != ACPI_TYPE_INTEGER)
			continue;

		cx.latency = obj->integer.value;

		obj = &(element->package.elements[3]);
		if (obj->type != ACPI_TYPE_INTEGER)
			continue;

		cx.power = obj->integer.value;

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
		status = -EFAULT;

      end:
	kfree(buffer.pointer);

	return status;
}

static void acpi_processor_power_verify_c2(struct acpi_processor_cx *cx)
{

	if (!cx->address)
		return;

	/*
	 * C2 latency must be less than or equal to 100
	 * microseconds.
	 */
	else if (cx->latency > ACPI_PROCESSOR_MAX_C2_LATENCY) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "latency too large [%d]\n", cx->latency));
		return;
	}

	/*
	 * Otherwise we've met all of our C2 requirements.
	 * Normalize the C2 latency to expidite policy
	 */
	cx->valid = 1;
	cx->latency_ticks = US_TO_PM_TIMER_TICKS(cx->latency);

	return;
}

static void acpi_processor_power_verify_c3(struct acpi_processor *pr,
					   struct acpi_processor_cx *cx)
{
	static int bm_check_flag;


	if (!cx->address)
		return;

	/*
	 * C3 latency must be less than or equal to 1000
	 * microseconds.
	 */
	else if (cx->latency > ACPI_PROCESSOR_MAX_C3_LATENCY) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "latency too large [%d]\n", cx->latency));
		return;
	}

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
	if (!bm_check_flag) {
		/* Determine whether bm_check is needed based on CPU  */
		acpi_processor_power_init_bm_check(&(pr->flags), pr->id);
		bm_check_flag = pr->flags.bm_check;
	} else {
		pr->flags.bm_check = bm_check_flag;
	}

	if (pr->flags.bm_check) {
		/* bus mastering control is necessary */
		if (!pr->flags.bm_control) {
			ACPI_DEBUG_PRINT((ACPI_DB_INFO,
					  "C3 support requires bus mastering control\n"));
			return;
		}
	} else {
		/*
		 * WBINVD should be set in fadt, for C3 state to be
		 * supported on when bm_check is not required.
		 */
		if (acpi_fadt.wb_invd != 1) {
			ACPI_DEBUG_PRINT((ACPI_DB_INFO,
					  "Cache invalidation should work properly"
					  " for C3 to be enabled on SMP systems\n"));
			return;
		}
		acpi_set_register(ACPI_BITREG_BUS_MASTER_RLD,
				  0, ACPI_MTX_DO_NOT_LOCK);
	}

	/*
	 * Otherwise we've met all of our C3 requirements.
	 * Normalize the C3 latency to expidite policy.  Enable
	 * checking of bus mastering status (bm_check) so we can
	 * use this in our C3 policy
	 */
	cx->valid = 1;
	cx->latency_ticks = US_TO_PM_TIMER_TICKS(cx->latency);

	return;
}

static int acpi_processor_power_verify(struct acpi_processor *pr)
{
	unsigned int i;
	unsigned int working = 0;

#ifdef ARCH_APICTIMER_STOPS_ON_C3
	int timer_broadcast = 0;
	cpumask_t mask = cpumask_of_cpu(pr->id);
	on_each_cpu(switch_ipi_to_APIC_timer, &mask, 1, 1);
#endif

	for (i = 1; i < ACPI_PROCESSOR_MAX_POWER; i++) {
		struct acpi_processor_cx *cx = &pr->power.states[i];

		switch (cx->type) {
		case ACPI_STATE_C1:
			cx->valid = 1;
			break;

		case ACPI_STATE_C2:
			acpi_processor_power_verify_c2(cx);
#ifdef ARCH_APICTIMER_STOPS_ON_C3
			/* Some AMD systems fake C3 as C2, but still
			   have timer troubles */
			if (cx->valid && 
				boot_cpu_data.x86_vendor == X86_VENDOR_AMD)
				timer_broadcast++;
#endif
			break;

		case ACPI_STATE_C3:
			acpi_processor_power_verify_c3(pr, cx);
#ifdef ARCH_APICTIMER_STOPS_ON_C3
			if (cx->valid)
				timer_broadcast++;
#endif
			break;
		}

		if (cx->valid)
			working++;
	}

#ifdef ARCH_APICTIMER_STOPS_ON_C3
	if (timer_broadcast)
		on_each_cpu(switch_APIC_timer_to_ipi, &mask, 1, 1);
#endif

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
	 * Set Default Policy
	 * ------------------
	 * Now that we know which states are supported, set the default
	 * policy.  Note that this policy can be changed dynamically
	 * (e.g. encourage deeper sleeps to conserve battery life when
	 * not on AC).
	 */
	result = acpi_processor_set_power_policy(pr);
	if (result)
		return result;

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

int acpi_processor_cst_has_changed(struct acpi_processor *pr)
{
	int result = 0;


	if (!pr)
		return -EINVAL;

	if (nocst) {
		return -ENODEV;
	}

	if (!pr->flags.power_setup_done)
		return -ENODEV;

	/* Fall back to the default idle loop */
	pm_idle = pm_idle_save;
	synchronize_sched();	/* Relies on interrupts forcing exit from idle. */

	pr->flags.power = 0;
	result = acpi_processor_get_power_info(pr);
	if ((pr->flags.power == 1) && (pr->flags.power_setup_done))
		pm_idle = acpi_processor_idle;

	return result;
}

/* proc interface */

static int acpi_processor_power_seq_show(struct seq_file *seq, void *offset)
{
	struct acpi_processor *pr = seq->private;
	unsigned int i;


	if (!pr)
		goto end;

	seq_printf(seq, "active state:            C%zd\n"
		   "max_cstate:              C%d\n"
		   "bus master activity:     %08x\n"
		   "maximum allowed latency: %d usec\n",
		   pr->power.state ? pr->power.state - pr->power.states : 0,
		   max_cstate, (unsigned)pr->power.bm_activity,
		   system_latency_constraint());

	seq_puts(seq, "states:\n");

	for (i = 1; i <= pr->power.count; i++) {
		seq_printf(seq, "   %cC%d:                  ",
			   (&pr->power.states[i] ==
			    pr->power.state ? '*' : ' '), i);

		if (!pr->power.states[i].valid) {
			seq_puts(seq, "<not supported>\n");
			continue;
		}

		switch (pr->power.states[i].type) {
		case ACPI_STATE_C1:
			seq_printf(seq, "type[C1] ");
			break;
		case ACPI_STATE_C2:
			seq_printf(seq, "type[C2] ");
			break;
		case ACPI_STATE_C3:
			seq_printf(seq, "type[C3] ");
			break;
		default:
			seq_printf(seq, "type[--] ");
			break;
		}

		if (pr->power.states[i].promotion.state)
			seq_printf(seq, "promotion[C%zd] ",
				   (pr->power.states[i].promotion.state -
				    pr->power.states));
		else
			seq_puts(seq, "promotion[--] ");

		if (pr->power.states[i].demotion.state)
			seq_printf(seq, "demotion[C%zd] ",
				   (pr->power.states[i].demotion.state -
				    pr->power.states));
		else
			seq_puts(seq, "demotion[--] ");

		seq_printf(seq, "latency[%03d] usage[%08d] duration[%020llu]\n",
			   pr->power.states[i].latency,
			   pr->power.states[i].usage,
			   pr->power.states[i].time);
	}

      end:
	return 0;
}

static int acpi_processor_power_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, acpi_processor_power_seq_show,
			   PDE(inode)->data);
}

static const struct file_operations acpi_processor_power_fops = {
	.open = acpi_processor_power_open_fs,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

#ifdef CONFIG_SMP
static void smp_callback(void *v)
{
	/* we already woke the CPU up, nothing more to do */
}

/*
 * This function gets called when a part of the kernel has a new latency
 * requirement.  This means we need to get all processors out of their C-state,
 * and then recalculate a new suitable C-state. Just do a cross-cpu IPI; that
 * wakes them all right up.
 */
static int acpi_processor_latency_notify(struct notifier_block *b,
		unsigned long l, void *v)
{
	smp_call_function(smp_callback, NULL, 0, 1);
	return NOTIFY_OK;
}

static struct notifier_block acpi_processor_latency_notifier = {
	.notifier_call = acpi_processor_latency_notify,
};
#endif

int __cpuinit acpi_processor_power_init(struct acpi_processor *pr,
			      struct acpi_device *device)
{
	acpi_status status = 0;
	static int first_run;
	struct proc_dir_entry *entry = NULL;
	unsigned int i;


	if (!first_run) {
		dmi_check_system(processor_power_dmi_table);
		if (max_cstate < ACPI_C_STATES_MAX)
			printk(KERN_NOTICE
			       "ACPI: processor limited to max C-state %d\n",
			       max_cstate);
		first_run++;
#ifdef CONFIG_SMP
		register_latency_notifier(&acpi_processor_latency_notifier);
#endif
	}

	if (!pr)
		return -EINVAL;

	if (acpi_fadt.cst_cnt && !nocst) {
		status =
		    acpi_os_write_port(acpi_fadt.smi_cmd, acpi_fadt.cst_cnt, 8);
		if (ACPI_FAILURE(status)) {
			ACPI_EXCEPTION((AE_INFO, status,
					"Notifying BIOS of _CST ability failed"));
		}
	}

	acpi_processor_get_power_info(pr);

	/*
	 * Install the idle handler if processor power management is supported.
	 * Note that we use previously set idle handler will be used on
	 * platforms that only support C1.
	 */
	if ((pr->flags.power) && (!boot_option_idle_override)) {
		printk(KERN_INFO PREFIX "CPU%d (power states:", pr->id);
		for (i = 1; i <= pr->power.count; i++)
			if (pr->power.states[i].valid)
				printk(" C%d[C%d]", i,
				       pr->power.states[i].type);
		printk(")\n");

		if (pr->id == 0) {
			pm_idle_save = pm_idle;
			pm_idle = acpi_processor_idle;
		}
	}

	/* 'power' [R] */
	entry = create_proc_entry(ACPI_PROCESSOR_FILE_POWER,
				  S_IRUGO, acpi_device_dir(device));
	if (!entry)
		return -EIO;
	else {
		entry->proc_fops = &acpi_processor_power_fops;
		entry->data = acpi_driver_data(device);
		entry->owner = THIS_MODULE;
	}

	pr->flags.power_setup_done = 1;

	return 0;
}

int acpi_processor_power_exit(struct acpi_processor *pr,
			      struct acpi_device *device)
{

	pr->flags.power_setup_done = 0;

	if (acpi_device_dir(device))
		remove_proc_entry(ACPI_PROCESSOR_FILE_POWER,
				  acpi_device_dir(device));

	/* Unregister the idle handler when processor #0 is removed. */
	if (pr->id == 0) {
		pm_idle = pm_idle_save;

		/*
		 * We are about to unload the current idle thread pm callback
		 * (pm_idle), Wait for all processors to update cached/local
		 * copies of pm_idle before proceeding.
		 */
		cpu_idle_wait();
#ifdef CONFIG_SMP
		unregister_latency_notifier(&acpi_processor_latency_notifier);
#endif
	}

	return 0;
}
