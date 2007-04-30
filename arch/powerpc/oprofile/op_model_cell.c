/*
 * Cell Broadband Engine OProfile Support
 *
 * (C) Copyright IBM Corporation 2006
 *
 * Author: David Erb (djerb@us.ibm.com)
 * Modifications:
 *         Carl Love <carll@us.ibm.com>
 *         Maynard Johnson <maynardj@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/cpufreq.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/kthread.h>
#include <linux/oprofile.h>
#include <linux/percpu.h>
#include <linux/smp.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <asm/cell-pmu.h>
#include <asm/cputable.h>
#include <asm/firmware.h>
#include <asm/io.h>
#include <asm/oprofile_impl.h>
#include <asm/processor.h>
#include <asm/prom.h>
#include <asm/ptrace.h>
#include <asm/reg.h>
#include <asm/rtas.h>
#include <asm/system.h>

#include "../platforms/cell/interrupt.h"
#include "../platforms/cell/cbe_regs.h"

#define PPU_CYCLES_EVENT_NUM 1	/*  event number for CYCLES */
#define PPU_CYCLES_GRP_NUM   1  /* special group number for identifying
                                 * PPU_CYCLES event
                                 */
#define CBE_COUNT_ALL_CYCLES 0x42800000	/* PPU cycle event specifier */

#define NUM_THREADS 2         /* number of physical threads in
			       * physical processor
			       */
#define NUM_TRACE_BUS_WORDS 4
#define NUM_INPUT_BUS_WORDS 2


struct pmc_cntrl_data {
	unsigned long vcntr;
	unsigned long evnts;
	unsigned long masks;
	unsigned long enabled;
};

/*
 * ibm,cbe-perftools rtas parameters
 */

struct pm_signal {
	u16 cpu;		/* Processor to modify */
	u16 sub_unit;		/* hw subunit this applies to (if applicable) */
	short int signal_group;	/* Signal Group to Enable/Disable */
	u8 bus_word;		/* Enable/Disable on this Trace/Trigger/Event
				 * Bus Word(s) (bitmask)
				 */
	u8 bit;			/* Trigger/Event bit (if applicable) */
};

/*
 * rtas call arguments
 */
enum {
	SUBFUNC_RESET = 1,
	SUBFUNC_ACTIVATE = 2,
	SUBFUNC_DEACTIVATE = 3,

	PASSTHRU_IGNORE = 0,
	PASSTHRU_ENABLE = 1,
	PASSTHRU_DISABLE = 2,
};

struct pm_cntrl {
	u16 enable;
	u16 stop_at_max;
	u16 trace_mode;
	u16 freeze;
	u16 count_mode;
};

static struct {
	u32 group_control;
	u32 debug_bus_control;
	struct pm_cntrl pm_cntrl;
	u32 pm07_cntrl[NR_PHYS_CTRS];
} pm_regs;

#define GET_SUB_UNIT(x) ((x & 0x0000f000) >> 12)
#define GET_BUS_WORD(x) ((x & 0x000000f0) >> 4)
#define GET_BUS_TYPE(x) ((x & 0x00000300) >> 8)
#define GET_POLARITY(x) ((x & 0x00000002) >> 1)
#define GET_COUNT_CYCLES(x) (x & 0x00000001)
#define GET_INPUT_CONTROL(x) ((x & 0x00000004) >> 2)

static DEFINE_PER_CPU(unsigned long[NR_PHYS_CTRS], pmc_values);

static struct pmc_cntrl_data pmc_cntrl[NUM_THREADS][NR_PHYS_CTRS];

/* Interpetation of hdw_thread:
 * 0 - even virtual cpus 0, 2, 4,...
 * 1 - odd virtual cpus 1, 3, 5, ...
 */
static u32 hdw_thread;

static u32 virt_cntr_inter_mask;
static struct timer_list timer_virt_cntr;

/* pm_signal needs to be global since it is initialized in
 * cell_reg_setup at the time when the necessary information
 * is available.
 */
static struct pm_signal pm_signal[NR_PHYS_CTRS];
static int pm_rtas_token;

static u32 reset_value[NR_PHYS_CTRS];
static int num_counters;
static int oprofile_running;
static DEFINE_SPINLOCK(virt_cntr_lock);

static u32 ctr_enabled;

static unsigned char trace_bus[NUM_TRACE_BUS_WORDS];
static unsigned char input_bus[NUM_INPUT_BUS_WORDS];

/*
 * Firmware interface functions
 */
static int
rtas_ibm_cbe_perftools(int subfunc, int passthru,
		       void *address, unsigned long length)
{
	u64 paddr = __pa(address);

	return rtas_call(pm_rtas_token, 5, 1, NULL, subfunc, passthru,
			 paddr >> 32, paddr & 0xffffffff, length);
}

static void pm_rtas_reset_signals(u32 node)
{
	int ret;
	struct pm_signal pm_signal_local;

	/*  The debug bus is being set to the passthru disable state.
	 *  However, the FW still expects atleast one legal signal routing
	 *  entry or it will return an error on the arguments.  If we don't
	 *  supply a valid entry, we must ignore all return values.  Ignoring
	 *  all return values means we might miss an error we should be
	 *  concerned about.
	 */

	/*  fw expects physical cpu #. */
	pm_signal_local.cpu = node;
	pm_signal_local.signal_group = 21;
	pm_signal_local.bus_word = 1;
	pm_signal_local.sub_unit = 0;
	pm_signal_local.bit = 0;

	ret = rtas_ibm_cbe_perftools(SUBFUNC_RESET, PASSTHRU_DISABLE,
				     &pm_signal_local,
				     sizeof(struct pm_signal));

	if (ret)
		printk(KERN_WARNING "%s: rtas returned: %d\n",
		       __FUNCTION__, ret);
}

static void pm_rtas_activate_signals(u32 node, u32 count)
{
	int ret;
	int i, j;
	struct pm_signal pm_signal_local[NR_PHYS_CTRS];

	/* There is no debug setup required for the cycles event.
	 * Note that only events in the same group can be used.
	 * Otherwise, there will be conflicts in correctly routing
	 * the signals on the debug bus.  It is the responsiblity
	 * of the OProfile user tool to check the events are in
	 * the same group.
	 */
	i = 0;
	for (j = 0; j < count; j++) {
		if (pm_signal[j].signal_group != PPU_CYCLES_GRP_NUM) {

			/* fw expects physical cpu # */
			pm_signal_local[i].cpu = node;
			pm_signal_local[i].signal_group
				= pm_signal[j].signal_group;
			pm_signal_local[i].bus_word = pm_signal[j].bus_word;
			pm_signal_local[i].sub_unit = pm_signal[j].sub_unit;
			pm_signal_local[i].bit = pm_signal[j].bit;
			i++;
		}
	}

	if (i != 0) {
		ret = rtas_ibm_cbe_perftools(SUBFUNC_ACTIVATE, PASSTHRU_ENABLE,
					     pm_signal_local,
					     i * sizeof(struct pm_signal));

		if (ret)
			printk(KERN_WARNING "%s: rtas returned: %d\n",
			       __FUNCTION__, ret);
	}
}

/*
 * PM Signal functions
 */
static void set_pm_event(u32 ctr, int event, u32 unit_mask)
{
	struct pm_signal *p;
	u32 signal_bit;
	u32 bus_word, bus_type, count_cycles, polarity, input_control;
	int j, i;

	if (event == PPU_CYCLES_EVENT_NUM) {
		/* Special Event: Count all cpu cycles */
		pm_regs.pm07_cntrl[ctr] = CBE_COUNT_ALL_CYCLES;
		p = &(pm_signal[ctr]);
		p->signal_group = PPU_CYCLES_GRP_NUM;
		p->bus_word = 1;
		p->sub_unit = 0;
		p->bit = 0;
		goto out;
	} else {
		pm_regs.pm07_cntrl[ctr] = 0;
	}

	bus_word = GET_BUS_WORD(unit_mask);
	bus_type = GET_BUS_TYPE(unit_mask);
	count_cycles = GET_COUNT_CYCLES(unit_mask);
	polarity = GET_POLARITY(unit_mask);
	input_control = GET_INPUT_CONTROL(unit_mask);
	signal_bit = (event % 100);

	p = &(pm_signal[ctr]);

	p->signal_group = event / 100;
	p->bus_word = bus_word;
	p->sub_unit = (unit_mask & 0x0000f000) >> 12;

	pm_regs.pm07_cntrl[ctr] = 0;
	pm_regs.pm07_cntrl[ctr] |= PM07_CTR_COUNT_CYCLES(count_cycles);
	pm_regs.pm07_cntrl[ctr] |= PM07_CTR_POLARITY(polarity);
	pm_regs.pm07_cntrl[ctr] |= PM07_CTR_INPUT_CONTROL(input_control);

	/* Some of the islands signal selection is based on 64 bit words.
	 * The debug bus words are 32 bits, the input words to the performance
	 * counters are defined as 32 bits.  Need to convert the 64 bit island
	 * specification to the appropriate 32 input bit and bus word for the
	 * performance counter event selection.  See the CELL Performance
	 * monitoring signals manual and the Perf cntr hardware descriptions
	 * for the details.
	 */
	if (input_control == 0) {
		if (signal_bit > 31) {
			signal_bit -= 32;
			if (bus_word == 0x3)
				bus_word = 0x2;
			else if (bus_word == 0xc)
				bus_word = 0x8;
		}

		if ((bus_type == 0) && p->signal_group >= 60)
			bus_type = 2;
		if ((bus_type == 1) && p->signal_group >= 50)
			bus_type = 0;

		pm_regs.pm07_cntrl[ctr] |= PM07_CTR_INPUT_MUX(signal_bit);
	} else {
		pm_regs.pm07_cntrl[ctr] = 0;
		p->bit = signal_bit;
	}

	for (i = 0; i < NUM_TRACE_BUS_WORDS; i++) {
		if (bus_word & (1 << i)) {
			pm_regs.debug_bus_control |=
			    (bus_type << (31 - (2 * i) + 1));

			for (j = 0; j < NUM_INPUT_BUS_WORDS; j++) {
				if (input_bus[j] == 0xff) {
					input_bus[j] = i;
					pm_regs.group_control |=
					    (i << (31 - i));
					break;
				}
			}
		}
	}
out:
	;
}

static void write_pm_cntrl(int cpu)
{
	/* Oprofile will use 32 bit counters, set bits 7:10 to 0
	 * pmregs.pm_cntrl is a global
	 */

	u32 val = 0;
	if (pm_regs.pm_cntrl.enable == 1)
		val |= CBE_PM_ENABLE_PERF_MON;

	if (pm_regs.pm_cntrl.stop_at_max == 1)
		val |= CBE_PM_STOP_AT_MAX;

	if (pm_regs.pm_cntrl.trace_mode == 1)
		val |= CBE_PM_TRACE_MODE_SET(pm_regs.pm_cntrl.trace_mode);

	if (pm_regs.pm_cntrl.freeze == 1)
		val |= CBE_PM_FREEZE_ALL_CTRS;

	/* Routine set_count_mode must be called previously to set
	 * the count mode based on the user selection of user and kernel.
	 */
	val |= CBE_PM_COUNT_MODE_SET(pm_regs.pm_cntrl.count_mode);
	cbe_write_pm(cpu, pm_control, val);
}

static inline void
set_count_mode(u32 kernel, u32 user)
{
	/* The user must specify user and kernel if they want them. If
	 *  neither is specified, OProfile will count in hypervisor mode.
	 *  pm_regs.pm_cntrl is a global
	 */
	if (kernel) {
		if (user)
			pm_regs.pm_cntrl.count_mode = CBE_COUNT_ALL_MODES;
		else
			pm_regs.pm_cntrl.count_mode =
				CBE_COUNT_SUPERVISOR_MODE;
	} else {
		if (user)
			pm_regs.pm_cntrl.count_mode = CBE_COUNT_PROBLEM_MODE;
		else
			pm_regs.pm_cntrl.count_mode =
				CBE_COUNT_HYPERVISOR_MODE;
	}
}

static inline void enable_ctr(u32 cpu, u32 ctr, u32 * pm07_cntrl)
{

	pm07_cntrl[ctr] |= CBE_PM_CTR_ENABLE;
	cbe_write_pm07_control(cpu, ctr, pm07_cntrl[ctr]);
}

/*
 * Oprofile is expected to collect data on all CPUs simultaneously.
 * However, there is one set of performance counters per node.  There are
 * two hardware threads or virtual CPUs on each node.  Hence, OProfile must
 * multiplex in time the performance counter collection on the two virtual
 * CPUs.  The multiplexing of the performance counters is done by this
 * virtual counter routine.
 *
 * The pmc_values used below is defined as 'per-cpu' but its use is
 * more akin to 'per-node'.  We need to store two sets of counter
 * values per node -- one for the previous run and one for the next.
 * The per-cpu[NR_PHYS_CTRS] gives us the storage we need.  Each odd/even
 * pair of per-cpu arrays is used for storing the previous and next
 * pmc values for a given node.
 * NOTE: We use the per-cpu variable to improve cache performance.
 */
static void cell_virtual_cntr(unsigned long data)
{
	/* This routine will alternate loading the virtual counters for
	 * virtual CPUs
	 */
	int i, prev_hdw_thread, next_hdw_thread;
	u32 cpu;
	unsigned long flags;

	/* Make sure that the interrupt_hander and
	 * the virt counter are not both playing with
	 * the counters on the same node.
	 */

	spin_lock_irqsave(&virt_cntr_lock, flags);

	prev_hdw_thread = hdw_thread;

	/* switch the cpu handling the interrupts */
	hdw_thread = 1 ^ hdw_thread;
	next_hdw_thread = hdw_thread;

	for (i = 0; i < num_counters; i++)
	/* There are some per thread events.  Must do the
	 * set event, for the thread that is being started
	 */
		set_pm_event(i,
			pmc_cntrl[next_hdw_thread][i].evnts,
			pmc_cntrl[next_hdw_thread][i].masks);

	/* The following is done only once per each node, but
	 * we need cpu #, not node #, to pass to the cbe_xxx functions.
	 */
	for_each_online_cpu(cpu) {
		if (cbe_get_hw_thread_id(cpu))
			continue;

		/* stop counters, save counter values, restore counts
		 * for previous thread
		 */
		cbe_disable_pm(cpu);
		cbe_disable_pm_interrupts(cpu);
		for (i = 0; i < num_counters; i++) {
			per_cpu(pmc_values, cpu + prev_hdw_thread)[i]
			    = cbe_read_ctr(cpu, i);

			if (per_cpu(pmc_values, cpu + next_hdw_thread)[i]
			    == 0xFFFFFFFF)
				/* If the cntr value is 0xffffffff, we must
				 * reset that to 0xfffffff0 when the current
				 * thread is restarted.  This will generate a
				 * new interrupt and make sure that we never
				 * restore the counters to the max value.  If
				 * the counters were restored to the max value,
				 * they do not increment and no interrupts are
				 * generated.  Hence no more samples will be
				 * collected on that cpu.
				 */
				cbe_write_ctr(cpu, i, 0xFFFFFFF0);
			else
				cbe_write_ctr(cpu, i,
					      per_cpu(pmc_values,
						      cpu +
						      next_hdw_thread)[i]);
		}

		/* Switch to the other thread. Change the interrupt
		 * and control regs to be scheduled on the CPU
		 * corresponding to the thread to execute.
		 */
		for (i = 0; i < num_counters; i++) {
			if (pmc_cntrl[next_hdw_thread][i].enabled) {
				/* There are some per thread events.
				 * Must do the set event, enable_cntr
				 * for each cpu.
				 */
				enable_ctr(cpu, i,
					   pm_regs.pm07_cntrl);
			} else {
				cbe_write_pm07_control(cpu, i, 0);
			}
		}

		/* Enable interrupts on the CPU thread that is starting */
		cbe_enable_pm_interrupts(cpu, next_hdw_thread,
					 virt_cntr_inter_mask);
		cbe_enable_pm(cpu);
	}

	spin_unlock_irqrestore(&virt_cntr_lock, flags);

	mod_timer(&timer_virt_cntr, jiffies + HZ / 10);
}

static void start_virt_cntrs(void)
{
	init_timer(&timer_virt_cntr);
	timer_virt_cntr.function = cell_virtual_cntr;
	timer_virt_cntr.data = 0UL;
	timer_virt_cntr.expires = jiffies + HZ / 10;
	add_timer(&timer_virt_cntr);
}

/* This function is called once for all cpus combined */
static void
cell_reg_setup(struct op_counter_config *ctr,
	       struct op_system_config *sys, int num_ctrs)
{
	int i, j, cpu;

	pm_rtas_token = rtas_token("ibm,cbe-perftools");
	if (pm_rtas_token == RTAS_UNKNOWN_SERVICE) {
		printk(KERN_WARNING "%s: RTAS_UNKNOWN_SERVICE\n",
		       __FUNCTION__);
		goto out;
	}

	num_counters = num_ctrs;

	pm_regs.group_control = 0;
	pm_regs.debug_bus_control = 0;

	/* setup the pm_control register */
	memset(&pm_regs.pm_cntrl, 0, sizeof(struct pm_cntrl));
	pm_regs.pm_cntrl.stop_at_max = 1;
	pm_regs.pm_cntrl.trace_mode = 0;
	pm_regs.pm_cntrl.freeze = 1;

	set_count_mode(sys->enable_kernel, sys->enable_user);

	/* Setup the thread 0 events */
	for (i = 0; i < num_ctrs; ++i) {

		pmc_cntrl[0][i].evnts = ctr[i].event;
		pmc_cntrl[0][i].masks = ctr[i].unit_mask;
		pmc_cntrl[0][i].enabled = ctr[i].enabled;
		pmc_cntrl[0][i].vcntr = i;

		for_each_possible_cpu(j)
			per_cpu(pmc_values, j)[i] = 0;
	}

	/* Setup the thread 1 events, map the thread 0 event to the
	 * equivalent thread 1 event.
	 */
	for (i = 0; i < num_ctrs; ++i) {
		if ((ctr[i].event >= 2100) && (ctr[i].event <= 2111))
			pmc_cntrl[1][i].evnts = ctr[i].event + 19;
		else if (ctr[i].event == 2203)
			pmc_cntrl[1][i].evnts = ctr[i].event;
		else if ((ctr[i].event >= 2200) && (ctr[i].event <= 2215))
			pmc_cntrl[1][i].evnts = ctr[i].event + 16;
		else
			pmc_cntrl[1][i].evnts = ctr[i].event;

		pmc_cntrl[1][i].masks = ctr[i].unit_mask;
		pmc_cntrl[1][i].enabled = ctr[i].enabled;
		pmc_cntrl[1][i].vcntr = i;
	}

	for (i = 0; i < NUM_TRACE_BUS_WORDS; i++)
		trace_bus[i] = 0xff;

	for (i = 0; i < NUM_INPUT_BUS_WORDS; i++)
		input_bus[i] = 0xff;

	/* Our counters count up, and "count" refers to
	 * how much before the next interrupt, and we interrupt
	 * on overflow.  So we calculate the starting value
	 * which will give us "count" until overflow.
	 * Then we set the events on the enabled counters.
	 */
	for (i = 0; i < num_counters; ++i) {
		/* start with virtual counter set 0 */
		if (pmc_cntrl[0][i].enabled) {
			/* Using 32bit counters, reset max - count */
			reset_value[i] = 0xFFFFFFFF - ctr[i].count;
			set_pm_event(i,
				     pmc_cntrl[0][i].evnts,
				     pmc_cntrl[0][i].masks);

			/* global, used by cell_cpu_setup */
			ctr_enabled |= (1 << i);
		}
	}

	/* initialize the previous counts for the virtual cntrs */
	for_each_online_cpu(cpu)
		for (i = 0; i < num_counters; ++i) {
			per_cpu(pmc_values, cpu)[i] = reset_value[i];
		}
out:
	;
}

/* This function is called once for each cpu */
static void cell_cpu_setup(struct op_counter_config *cntr)
{
	u32 cpu = smp_processor_id();
	u32 num_enabled = 0;
	int i;

	/* There is one performance monitor per processor chip (i.e. node),
	 * so we only need to perform this function once per node.
	 */
	if (cbe_get_hw_thread_id(cpu))
		goto out;

	if (pm_rtas_token == RTAS_UNKNOWN_SERVICE) {
		printk(KERN_WARNING "%s: RTAS_UNKNOWN_SERVICE\n",
		       __FUNCTION__);
		goto out;
	}

	/* Stop all counters */
	cbe_disable_pm(cpu);
	cbe_disable_pm_interrupts(cpu);

	cbe_write_pm(cpu, pm_interval, 0);
	cbe_write_pm(cpu, pm_start_stop, 0);
	cbe_write_pm(cpu, group_control, pm_regs.group_control);
	cbe_write_pm(cpu, debug_bus_control, pm_regs.debug_bus_control);
	write_pm_cntrl(cpu);

	for (i = 0; i < num_counters; ++i) {
		if (ctr_enabled & (1 << i)) {
			pm_signal[num_enabled].cpu = cbe_cpu_to_node(cpu);
			num_enabled++;
		}
	}

	pm_rtas_activate_signals(cbe_cpu_to_node(cpu), num_enabled);
out:
	;
}

static void cell_global_start(struct op_counter_config *ctr)
{
	u32 cpu;
	u32 interrupt_mask = 0;
	u32 i;

	/* This routine gets called once for the system.
	 * There is one performance monitor per node, so we
	 * only need to perform this function once per node.
	 */
	for_each_online_cpu(cpu) {
		if (cbe_get_hw_thread_id(cpu))
			continue;

		interrupt_mask = 0;

		for (i = 0; i < num_counters; ++i) {
			if (ctr_enabled & (1 << i)) {
				cbe_write_ctr(cpu, i, reset_value[i]);
				enable_ctr(cpu, i, pm_regs.pm07_cntrl);
				interrupt_mask |=
				    CBE_PM_CTR_OVERFLOW_INTR(i);
			} else {
				/* Disable counter */
				cbe_write_pm07_control(cpu, i, 0);
			}
		}

		cbe_get_and_clear_pm_interrupts(cpu);
		cbe_enable_pm_interrupts(cpu, hdw_thread, interrupt_mask);
		cbe_enable_pm(cpu);
	}

	virt_cntr_inter_mask = interrupt_mask;
	oprofile_running = 1;
	smp_wmb();

	/* NOTE: start_virt_cntrs will result in cell_virtual_cntr() being
	 * executed which manipulates the PMU.  We start the "virtual counter"
	 * here so that we do not need to synchronize access to the PMU in
	 * the above for-loop.
	 */
	start_virt_cntrs();
}

static void cell_global_stop(void)
{
	int cpu;

	/* This routine will be called once for the system.
	 * There is one performance monitor per node, so we
	 * only need to perform this function once per node.
	 */
	del_timer_sync(&timer_virt_cntr);
	oprofile_running = 0;
	smp_wmb();

	for_each_online_cpu(cpu) {
		if (cbe_get_hw_thread_id(cpu))
			continue;

		cbe_sync_irq(cbe_cpu_to_node(cpu));
		/* Stop the counters */
		cbe_disable_pm(cpu);

		/* Deactivate the signals */
		pm_rtas_reset_signals(cbe_cpu_to_node(cpu));

		/* Deactivate interrupts */
		cbe_disable_pm_interrupts(cpu);
	}
}

static void
cell_handle_interrupt(struct pt_regs *regs, struct op_counter_config *ctr)
{
	u32 cpu;
	u64 pc;
	int is_kernel;
	unsigned long flags = 0;
	u32 interrupt_mask;
	int i;

	cpu = smp_processor_id();

	/* Need to make sure the interrupt handler and the virt counter
	 * routine are not running at the same time. See the
	 * cell_virtual_cntr() routine for additional comments.
	 */
	spin_lock_irqsave(&virt_cntr_lock, flags);

	/* Need to disable and reenable the performance counters
	 * to get the desired behavior from the hardware.  This
	 * is hardware specific.
	 */

	cbe_disable_pm(cpu);

	interrupt_mask = cbe_get_and_clear_pm_interrupts(cpu);

	/* If the interrupt mask has been cleared, then the virt cntr
	 * has cleared the interrupt.  When the thread that generated
	 * the interrupt is restored, the data count will be restored to
	 * 0xffffff0 to cause the interrupt to be regenerated.
	 */

	if ((oprofile_running == 1) && (interrupt_mask != 0)) {
		pc = regs->nip;
		is_kernel = is_kernel_addr(pc);

		for (i = 0; i < num_counters; ++i) {
			if ((interrupt_mask & CBE_PM_CTR_OVERFLOW_INTR(i))
			    && ctr[i].enabled) {
				oprofile_add_pc(pc, is_kernel, i);
				cbe_write_ctr(cpu, i, reset_value[i]);
			}
		}

		/* The counters were frozen by the interrupt.
		 * Reenable the interrupt and restart the counters.
		 * If there was a race between the interrupt handler and
		 * the virtual counter routine.  The virutal counter
		 * routine may have cleared the interrupts.  Hence must
		 * use the virt_cntr_inter_mask to re-enable the interrupts.
		 */
		cbe_enable_pm_interrupts(cpu, hdw_thread,
					 virt_cntr_inter_mask);

		/* The writes to the various performance counters only writes
		 * to a latch.  The new values (interrupt setting bits, reset
		 * counter value etc.) are not copied to the actual registers
		 * until the performance monitor is enabled.  In order to get
		 * this to work as desired, the permormance monitor needs to
		 * be disabled while writting to the latches.  This is a
		 * HW design issue.
		 */
		cbe_enable_pm(cpu);
	}
	spin_unlock_irqrestore(&virt_cntr_lock, flags);
}

struct op_powerpc_model op_model_cell = {
	.reg_setup = cell_reg_setup,
	.cpu_setup = cell_cpu_setup,
	.global_start = cell_global_start,
	.global_stop = cell_global_stop,
	.handle_interrupt = cell_handle_interrupt,
};
