/**
 * @file op_model_p4.c
 * P4 model-specific MSR operations
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author Graydon Hoare
 */

#include <linux/oprofile.h>
#include <linux/smp.h>
#include <linux/ptrace.h>
#include <asm/nmi.h>
#include <asm/msr.h>
#include <asm/fixmap.h>
#include <asm/apic.h>


#include "op_x86_model.h"
#include "op_counter.h"

#define NUM_EVENTS 39

#define NUM_COUNTERS_NON_HT 8
#define NUM_ESCRS_NON_HT 45
#define NUM_CCCRS_NON_HT 18
#define NUM_CONTROLS_NON_HT (NUM_ESCRS_NON_HT + NUM_CCCRS_NON_HT)

#define NUM_COUNTERS_HT2 4
#define NUM_ESCRS_HT2 23
#define NUM_CCCRS_HT2 9
#define NUM_CONTROLS_HT2 (NUM_ESCRS_HT2 + NUM_CCCRS_HT2)

#define OP_CTR_OVERFLOW			(1ULL<<31)

static unsigned int num_counters = NUM_COUNTERS_NON_HT;
static unsigned int num_controls = NUM_CONTROLS_NON_HT;

/* this has to be checked dynamically since the
   hyper-threadedness of a chip is discovered at
   kernel boot-time. */
static inline void setup_num_counters(void)
{
#ifdef CONFIG_SMP
	if (smp_num_siblings == 2) {
		num_counters = NUM_COUNTERS_HT2;
		num_controls = NUM_CONTROLS_HT2;
	}
#endif
}

static inline int addr_increment(void)
{
#ifdef CONFIG_SMP
	return smp_num_siblings == 2 ? 2 : 1;
#else
	return 1;
#endif
}


/* tables to simulate simplified hardware view of p4 registers */
struct p4_counter_binding {
	int virt_counter;
	int counter_address;
	int cccr_address;
};

struct p4_event_binding {
	int escr_select;  /* value to put in CCCR */
	int event_select; /* value to put in ESCR */
	struct {
		int virt_counter; /* for this counter... */
		int escr_address; /* use this ESCR       */
	} bindings[2];
};

/* nb: these CTR_* defines are a duplicate of defines in
   event/i386.p4*events. */


#define CTR_BPU_0      (1 << 0)
#define CTR_MS_0       (1 << 1)
#define CTR_FLAME_0    (1 << 2)
#define CTR_IQ_4       (1 << 3)
#define CTR_BPU_2      (1 << 4)
#define CTR_MS_2       (1 << 5)
#define CTR_FLAME_2    (1 << 6)
#define CTR_IQ_5       (1 << 7)

static struct p4_counter_binding p4_counters[NUM_COUNTERS_NON_HT] = {
	{ CTR_BPU_0,   MSR_P4_BPU_PERFCTR0,   MSR_P4_BPU_CCCR0 },
	{ CTR_MS_0,    MSR_P4_MS_PERFCTR0,    MSR_P4_MS_CCCR0 },
	{ CTR_FLAME_0, MSR_P4_FLAME_PERFCTR0, MSR_P4_FLAME_CCCR0 },
	{ CTR_IQ_4,    MSR_P4_IQ_PERFCTR4,    MSR_P4_IQ_CCCR4 },
	{ CTR_BPU_2,   MSR_P4_BPU_PERFCTR2,   MSR_P4_BPU_CCCR2 },
	{ CTR_MS_2,    MSR_P4_MS_PERFCTR2,    MSR_P4_MS_CCCR2 },
	{ CTR_FLAME_2, MSR_P4_FLAME_PERFCTR2, MSR_P4_FLAME_CCCR2 },
	{ CTR_IQ_5,    MSR_P4_IQ_PERFCTR5,    MSR_P4_IQ_CCCR5 }
};

#define NUM_UNUSED_CCCRS (NUM_CCCRS_NON_HT - NUM_COUNTERS_NON_HT)

/* p4 event codes in libop/op_event.h are indices into this table. */

static struct p4_event_binding p4_events[NUM_EVENTS] = {

	{ /* BRANCH_RETIRED */
		0x05, 0x06,
		{ {CTR_IQ_4, MSR_P4_CRU_ESCR2},
		  {CTR_IQ_5, MSR_P4_CRU_ESCR3} }
	},

	{ /* MISPRED_BRANCH_RETIRED */
		0x04, 0x03,
		{ { CTR_IQ_4, MSR_P4_CRU_ESCR0},
		  { CTR_IQ_5, MSR_P4_CRU_ESCR1} }
	},

	{ /* TC_DELIVER_MODE */
		0x01, 0x01,
		{ { CTR_MS_0, MSR_P4_TC_ESCR0},
		  { CTR_MS_2, MSR_P4_TC_ESCR1} }
	},

	{ /* BPU_FETCH_REQUEST */
		0x00, 0x03,
		{ { CTR_BPU_0, MSR_P4_BPU_ESCR0},
		  { CTR_BPU_2, MSR_P4_BPU_ESCR1} }
	},

	{ /* ITLB_REFERENCE */
		0x03, 0x18,
		{ { CTR_BPU_0, MSR_P4_ITLB_ESCR0},
		  { CTR_BPU_2, MSR_P4_ITLB_ESCR1} }
	},

	{ /* MEMORY_CANCEL */
		0x05, 0x02,
		{ { CTR_FLAME_0, MSR_P4_DAC_ESCR0},
		  { CTR_FLAME_2, MSR_P4_DAC_ESCR1} }
	},

	{ /* MEMORY_COMPLETE */
		0x02, 0x08,
		{ { CTR_FLAME_0, MSR_P4_SAAT_ESCR0},
		  { CTR_FLAME_2, MSR_P4_SAAT_ESCR1} }
	},

	{ /* LOAD_PORT_REPLAY */
		0x02, 0x04,
		{ { CTR_FLAME_0, MSR_P4_SAAT_ESCR0},
		  { CTR_FLAME_2, MSR_P4_SAAT_ESCR1} }
	},

	{ /* STORE_PORT_REPLAY */
		0x02, 0x05,
		{ { CTR_FLAME_0, MSR_P4_SAAT_ESCR0},
		  { CTR_FLAME_2, MSR_P4_SAAT_ESCR1} }
	},

	{ /* MOB_LOAD_REPLAY */
		0x02, 0x03,
		{ { CTR_BPU_0, MSR_P4_MOB_ESCR0},
		  { CTR_BPU_2, MSR_P4_MOB_ESCR1} }
	},

	{ /* PAGE_WALK_TYPE */
		0x04, 0x01,
		{ { CTR_BPU_0, MSR_P4_PMH_ESCR0},
		  { CTR_BPU_2, MSR_P4_PMH_ESCR1} }
	},

	{ /* BSQ_CACHE_REFERENCE */
		0x07, 0x0c,
		{ { CTR_BPU_0, MSR_P4_BSU_ESCR0},
		  { CTR_BPU_2, MSR_P4_BSU_ESCR1} }
	},

	{ /* IOQ_ALLOCATION */
		0x06, 0x03,
		{ { CTR_BPU_0, MSR_P4_FSB_ESCR0},
		  { 0, 0 } }
	},

	{ /* IOQ_ACTIVE_ENTRIES */
		0x06, 0x1a,
		{ { CTR_BPU_2, MSR_P4_FSB_ESCR1},
		  { 0, 0 } }
	},

	{ /* FSB_DATA_ACTIVITY */
		0x06, 0x17,
		{ { CTR_BPU_0, MSR_P4_FSB_ESCR0},
		  { CTR_BPU_2, MSR_P4_FSB_ESCR1} }
	},

	{ /* BSQ_ALLOCATION */
		0x07, 0x05,
		{ { CTR_BPU_0, MSR_P4_BSU_ESCR0},
		  { 0, 0 } }
	},

	{ /* BSQ_ACTIVE_ENTRIES */
		0x07, 0x06,
		{ { CTR_BPU_2, MSR_P4_BSU_ESCR1 /* guess */},
		  { 0, 0 } }
	},

	{ /* X87_ASSIST */
		0x05, 0x03,
		{ { CTR_IQ_4, MSR_P4_CRU_ESCR2},
		  { CTR_IQ_5, MSR_P4_CRU_ESCR3} }
	},

	{ /* SSE_INPUT_ASSIST */
		0x01, 0x34,
		{ { CTR_FLAME_0, MSR_P4_FIRM_ESCR0},
		  { CTR_FLAME_2, MSR_P4_FIRM_ESCR1} }
	},

	{ /* PACKED_SP_UOP */
		0x01, 0x08,
		{ { CTR_FLAME_0, MSR_P4_FIRM_ESCR0},
		  { CTR_FLAME_2, MSR_P4_FIRM_ESCR1} }
	},

	{ /* PACKED_DP_UOP */
		0x01, 0x0c,
		{ { CTR_FLAME_0, MSR_P4_FIRM_ESCR0},
		  { CTR_FLAME_2, MSR_P4_FIRM_ESCR1} }
	},

	{ /* SCALAR_SP_UOP */
		0x01, 0x0a,
		{ { CTR_FLAME_0, MSR_P4_FIRM_ESCR0},
		  { CTR_FLAME_2, MSR_P4_FIRM_ESCR1} }
	},

	{ /* SCALAR_DP_UOP */
		0x01, 0x0e,
		{ { CTR_FLAME_0, MSR_P4_FIRM_ESCR0},
		  { CTR_FLAME_2, MSR_P4_FIRM_ESCR1} }
	},

	{ /* 64BIT_MMX_UOP */
		0x01, 0x02,
		{ { CTR_FLAME_0, MSR_P4_FIRM_ESCR0},
		  { CTR_FLAME_2, MSR_P4_FIRM_ESCR1} }
	},

	{ /* 128BIT_MMX_UOP */
		0x01, 0x1a,
		{ { CTR_FLAME_0, MSR_P4_FIRM_ESCR0},
		  { CTR_FLAME_2, MSR_P4_FIRM_ESCR1} }
	},

	{ /* X87_FP_UOP */
		0x01, 0x04,
		{ { CTR_FLAME_0, MSR_P4_FIRM_ESCR0},
		  { CTR_FLAME_2, MSR_P4_FIRM_ESCR1} }
	},

	{ /* X87_SIMD_MOVES_UOP */
		0x01, 0x2e,
		{ { CTR_FLAME_0, MSR_P4_FIRM_ESCR0},
		  { CTR_FLAME_2, MSR_P4_FIRM_ESCR1} }
	},

	{ /* MACHINE_CLEAR */
		0x05, 0x02,
		{ { CTR_IQ_4, MSR_P4_CRU_ESCR2},
		  { CTR_IQ_5, MSR_P4_CRU_ESCR3} }
	},

	{ /* GLOBAL_POWER_EVENTS */
		0x06, 0x13 /* older manual says 0x05, newer 0x13 */,
		{ { CTR_BPU_0, MSR_P4_FSB_ESCR0},
		  { CTR_BPU_2, MSR_P4_FSB_ESCR1} }
	},

	{ /* TC_MS_XFER */
		0x00, 0x05,
		{ { CTR_MS_0, MSR_P4_MS_ESCR0},
		  { CTR_MS_2, MSR_P4_MS_ESCR1} }
	},

	{ /* UOP_QUEUE_WRITES */
		0x00, 0x09,
		{ { CTR_MS_0, MSR_P4_MS_ESCR0},
		  { CTR_MS_2, MSR_P4_MS_ESCR1} }
	},

	{ /* FRONT_END_EVENT */
		0x05, 0x08,
		{ { CTR_IQ_4, MSR_P4_CRU_ESCR2},
		  { CTR_IQ_5, MSR_P4_CRU_ESCR3} }
	},

	{ /* EXECUTION_EVENT */
		0x05, 0x0c,
		{ { CTR_IQ_4, MSR_P4_CRU_ESCR2},
		  { CTR_IQ_5, MSR_P4_CRU_ESCR3} }
	},

	{ /* REPLAY_EVENT */
		0x05, 0x09,
		{ { CTR_IQ_4, MSR_P4_CRU_ESCR2},
		  { CTR_IQ_5, MSR_P4_CRU_ESCR3} }
	},

	{ /* INSTR_RETIRED */
		0x04, 0x02,
		{ { CTR_IQ_4, MSR_P4_CRU_ESCR0},
		  { CTR_IQ_5, MSR_P4_CRU_ESCR1} }
	},

	{ /* UOPS_RETIRED */
		0x04, 0x01,
		{ { CTR_IQ_4, MSR_P4_CRU_ESCR0},
		  { CTR_IQ_5, MSR_P4_CRU_ESCR1} }
	},

	{ /* UOP_TYPE */
		0x02, 0x02,
		{ { CTR_IQ_4, MSR_P4_RAT_ESCR0},
		  { CTR_IQ_5, MSR_P4_RAT_ESCR1} }
	},

	{ /* RETIRED_MISPRED_BRANCH_TYPE */
		0x02, 0x05,
		{ { CTR_MS_0, MSR_P4_TBPU_ESCR0},
		  { CTR_MS_2, MSR_P4_TBPU_ESCR1} }
	},

	{ /* RETIRED_BRANCH_TYPE */
		0x02, 0x04,
		{ { CTR_MS_0, MSR_P4_TBPU_ESCR0},
		  { CTR_MS_2, MSR_P4_TBPU_ESCR1} }
	}
};


#define MISC_PMC_ENABLED_P(x) ((x) & 1 << 7)

#define ESCR_RESERVED_BITS 0x80000003
#define ESCR_CLEAR(escr) ((escr) &= ESCR_RESERVED_BITS)
#define ESCR_SET_USR_0(escr, usr) ((escr) |= (((usr) & 1) << 2))
#define ESCR_SET_OS_0(escr, os) ((escr) |= (((os) & 1) << 3))
#define ESCR_SET_USR_1(escr, usr) ((escr) |= (((usr) & 1)))
#define ESCR_SET_OS_1(escr, os) ((escr) |= (((os) & 1) << 1))
#define ESCR_SET_EVENT_SELECT(escr, sel) ((escr) |= (((sel) & 0x3f) << 25))
#define ESCR_SET_EVENT_MASK(escr, mask) ((escr) |= (((mask) & 0xffff) << 9))

#define CCCR_RESERVED_BITS 0x38030FFF
#define CCCR_CLEAR(cccr) ((cccr) &= CCCR_RESERVED_BITS)
#define CCCR_SET_REQUIRED_BITS(cccr) ((cccr) |= 0x00030000)
#define CCCR_SET_ESCR_SELECT(cccr, sel) ((cccr) |= (((sel) & 0x07) << 13))
#define CCCR_SET_PMI_OVF_0(cccr) ((cccr) |= (1<<26))
#define CCCR_SET_PMI_OVF_1(cccr) ((cccr) |= (1<<27))
#define CCCR_SET_ENABLE(cccr) ((cccr) |= (1<<12))
#define CCCR_SET_DISABLE(cccr) ((cccr) &= ~(1<<12))
#define CCCR_OVF_P(cccr) ((cccr) & (1U<<31))
#define CCCR_CLEAR_OVF(cccr) ((cccr) &= (~(1U<<31)))


/* this assigns a "stagger" to the current CPU, which is used throughout
   the code in this module as an extra array offset, to select the "even"
   or "odd" part of all the divided resources. */
static unsigned int get_stagger(void)
{
#ifdef CONFIG_SMP
	int cpu = smp_processor_id();
	return cpu != cpumask_first(this_cpu_cpumask_var_ptr(cpu_sibling_map));
#endif
	return 0;
}


/* finally, mediate access to a real hardware counter
   by passing a "virtual" counter numer to this macro,
   along with your stagger setting. */
#define VIRT_CTR(stagger, i) ((i) + ((num_counters) * (stagger)))

static unsigned long reset_value[NUM_COUNTERS_NON_HT];

static void p4_shutdown(struct op_msrs const * const msrs)
{
	int i;

	for (i = 0; i < num_counters; ++i) {
		if (msrs->counters[i].addr)
			release_perfctr_nmi(msrs->counters[i].addr);
	}
	/*
	 * some of the control registers are specially reserved in
	 * conjunction with the counter registers (hence the starting offset).
	 * This saves a few bits.
	 */
	for (i = num_counters; i < num_controls; ++i) {
		if (msrs->controls[i].addr)
			release_evntsel_nmi(msrs->controls[i].addr);
	}
}

static int p4_fill_in_addresses(struct op_msrs * const msrs)
{
	unsigned int i;
	unsigned int addr, cccraddr, stag;

	setup_num_counters();
	stag = get_stagger();

	/* the counter & cccr registers we pay attention to */
	for (i = 0; i < num_counters; ++i) {
		addr = p4_counters[VIRT_CTR(stag, i)].counter_address;
		cccraddr = p4_counters[VIRT_CTR(stag, i)].cccr_address;
		if (reserve_perfctr_nmi(addr)) {
			msrs->counters[i].addr = addr;
			msrs->controls[i].addr = cccraddr;
		}
	}

	/* 43 ESCR registers in three or four discontiguous group */
	for (addr = MSR_P4_BSU_ESCR0 + stag;
	     addr < MSR_P4_IQ_ESCR0; ++i, addr += addr_increment()) {
		if (reserve_evntsel_nmi(addr))
			msrs->controls[i].addr = addr;
	}

	/* no IQ_ESCR0/1 on some models, we save a seconde time BSU_ESCR0/1
	 * to avoid special case in nmi_{save|restore}_registers() */
	if (boot_cpu_data.x86_model >= 0x3) {
		for (addr = MSR_P4_BSU_ESCR0 + stag;
		     addr <= MSR_P4_BSU_ESCR1; ++i, addr += addr_increment()) {
			if (reserve_evntsel_nmi(addr))
				msrs->controls[i].addr = addr;
		}
	} else {
		for (addr = MSR_P4_IQ_ESCR0 + stag;
		     addr <= MSR_P4_IQ_ESCR1; ++i, addr += addr_increment()) {
			if (reserve_evntsel_nmi(addr))
				msrs->controls[i].addr = addr;
		}
	}

	for (addr = MSR_P4_RAT_ESCR0 + stag;
	     addr <= MSR_P4_SSU_ESCR0; ++i, addr += addr_increment()) {
		if (reserve_evntsel_nmi(addr))
			msrs->controls[i].addr = addr;
	}

	for (addr = MSR_P4_MS_ESCR0 + stag;
	     addr <= MSR_P4_TC_ESCR1; ++i, addr += addr_increment()) {
		if (reserve_evntsel_nmi(addr))
			msrs->controls[i].addr = addr;
	}

	for (addr = MSR_P4_IX_ESCR0 + stag;
	     addr <= MSR_P4_CRU_ESCR3; ++i, addr += addr_increment()) {
		if (reserve_evntsel_nmi(addr))
			msrs->controls[i].addr = addr;
	}

	/* there are 2 remaining non-contiguously located ESCRs */

	if (num_counters == NUM_COUNTERS_NON_HT) {
		/* standard non-HT CPUs handle both remaining ESCRs*/
		if (reserve_evntsel_nmi(MSR_P4_CRU_ESCR5))
			msrs->controls[i++].addr = MSR_P4_CRU_ESCR5;
		if (reserve_evntsel_nmi(MSR_P4_CRU_ESCR4))
			msrs->controls[i++].addr = MSR_P4_CRU_ESCR4;

	} else if (stag == 0) {
		/* HT CPUs give the first remainder to the even thread, as
		   the 32nd control register */
		if (reserve_evntsel_nmi(MSR_P4_CRU_ESCR4))
			msrs->controls[i++].addr = MSR_P4_CRU_ESCR4;

	} else {
		/* and two copies of the second to the odd thread,
		   for the 22st and 23nd control registers */
		if (reserve_evntsel_nmi(MSR_P4_CRU_ESCR5)) {
			msrs->controls[i++].addr = MSR_P4_CRU_ESCR5;
			msrs->controls[i++].addr = MSR_P4_CRU_ESCR5;
		}
	}

	for (i = 0; i < num_counters; ++i) {
		if (!counter_config[i].enabled)
			continue;
		if (msrs->controls[i].addr)
			continue;
		op_x86_warn_reserved(i);
		p4_shutdown(msrs);
		return -EBUSY;
	}

	return 0;
}


static void pmc_setup_one_p4_counter(unsigned int ctr)
{
	int i;
	int const maxbind = 2;
	unsigned int cccr = 0;
	unsigned int escr = 0;
	unsigned int high = 0;
	unsigned int counter_bit;
	struct p4_event_binding *ev = NULL;
	unsigned int stag;

	stag = get_stagger();

	/* convert from counter *number* to counter *bit* */
	counter_bit = 1 << VIRT_CTR(stag, ctr);

	/* find our event binding structure. */
	if (counter_config[ctr].event <= 0 || counter_config[ctr].event > NUM_EVENTS) {
		printk(KERN_ERR
		       "oprofile: P4 event code 0x%lx out of range\n",
		       counter_config[ctr].event);
		return;
	}

	ev = &(p4_events[counter_config[ctr].event - 1]);

	for (i = 0; i < maxbind; i++) {
		if (ev->bindings[i].virt_counter & counter_bit) {

			/* modify ESCR */
			rdmsr(ev->bindings[i].escr_address, escr, high);
			ESCR_CLEAR(escr);
			if (stag == 0) {
				ESCR_SET_USR_0(escr, counter_config[ctr].user);
				ESCR_SET_OS_0(escr, counter_config[ctr].kernel);
			} else {
				ESCR_SET_USR_1(escr, counter_config[ctr].user);
				ESCR_SET_OS_1(escr, counter_config[ctr].kernel);
			}
			ESCR_SET_EVENT_SELECT(escr, ev->event_select);
			ESCR_SET_EVENT_MASK(escr, counter_config[ctr].unit_mask);
			wrmsr(ev->bindings[i].escr_address, escr, high);

			/* modify CCCR */
			rdmsr(p4_counters[VIRT_CTR(stag, ctr)].cccr_address,
			      cccr, high);
			CCCR_CLEAR(cccr);
			CCCR_SET_REQUIRED_BITS(cccr);
			CCCR_SET_ESCR_SELECT(cccr, ev->escr_select);
			if (stag == 0)
				CCCR_SET_PMI_OVF_0(cccr);
			else
				CCCR_SET_PMI_OVF_1(cccr);
			wrmsr(p4_counters[VIRT_CTR(stag, ctr)].cccr_address,
			      cccr, high);
			return;
		}
	}

	printk(KERN_ERR
	       "oprofile: P4 event code 0x%lx no binding, stag %d ctr %d\n",
	       counter_config[ctr].event, stag, ctr);
}


static void p4_setup_ctrs(struct op_x86_model_spec const *model,
			  struct op_msrs const * const msrs)
{
	unsigned int i;
	unsigned int low, high;
	unsigned int stag;

	stag = get_stagger();

	rdmsr(MSR_IA32_MISC_ENABLE, low, high);
	if (!MISC_PMC_ENABLED_P(low)) {
		printk(KERN_ERR "oprofile: P4 PMC not available\n");
		return;
	}

	/* clear the cccrs we will use */
	for (i = 0; i < num_counters; i++) {
		if (unlikely(!msrs->controls[i].addr))
			continue;
		rdmsr(p4_counters[VIRT_CTR(stag, i)].cccr_address, low, high);
		CCCR_CLEAR(low);
		CCCR_SET_REQUIRED_BITS(low);
		wrmsr(p4_counters[VIRT_CTR(stag, i)].cccr_address, low, high);
	}

	/* clear all escrs (including those outside our concern) */
	for (i = num_counters; i < num_controls; i++) {
		if (unlikely(!msrs->controls[i].addr))
			continue;
		wrmsr(msrs->controls[i].addr, 0, 0);
	}

	/* setup all counters */
	for (i = 0; i < num_counters; ++i) {
		if (counter_config[i].enabled && msrs->controls[i].addr) {
			reset_value[i] = counter_config[i].count;
			pmc_setup_one_p4_counter(i);
			wrmsrl(p4_counters[VIRT_CTR(stag, i)].counter_address,
			       -(u64)counter_config[i].count);
		} else {
			reset_value[i] = 0;
		}
	}
}


static int p4_check_ctrs(struct pt_regs * const regs,
			 struct op_msrs const * const msrs)
{
	unsigned long ctr, low, high, stag, real;
	int i;

	stag = get_stagger();

	for (i = 0; i < num_counters; ++i) {

		if (!reset_value[i])
			continue;

		/*
		 * there is some eccentricity in the hardware which
		 * requires that we perform 2 extra corrections:
		 *
		 * - check both the CCCR:OVF flag for overflow and the
		 *   counter high bit for un-flagged overflows.
		 *
		 * - write the counter back twice to ensure it gets
		 *   updated properly.
		 *
		 * the former seems to be related to extra NMIs happening
		 * during the current NMI; the latter is reported as errata
		 * N15 in intel doc 249199-029, pentium 4 specification
		 * update, though their suggested work-around does not
		 * appear to solve the problem.
		 */

		real = VIRT_CTR(stag, i);

		rdmsr(p4_counters[real].cccr_address, low, high);
		rdmsr(p4_counters[real].counter_address, ctr, high);
		if (CCCR_OVF_P(low) || !(ctr & OP_CTR_OVERFLOW)) {
			oprofile_add_sample(regs, i);
			wrmsrl(p4_counters[real].counter_address,
			       -(u64)reset_value[i]);
			CCCR_CLEAR_OVF(low);
			wrmsr(p4_counters[real].cccr_address, low, high);
			wrmsrl(p4_counters[real].counter_address,
			       -(u64)reset_value[i]);
		}
	}

	/* P4 quirk: you have to re-unmask the apic vector */
	apic_write(APIC_LVTPC, apic_read(APIC_LVTPC) & ~APIC_LVT_MASKED);

	/* See op_model_ppro.c */
	return 1;
}


static void p4_start(struct op_msrs const * const msrs)
{
	unsigned int low, high, stag;
	int i;

	stag = get_stagger();

	for (i = 0; i < num_counters; ++i) {
		if (!reset_value[i])
			continue;
		rdmsr(p4_counters[VIRT_CTR(stag, i)].cccr_address, low, high);
		CCCR_SET_ENABLE(low);
		wrmsr(p4_counters[VIRT_CTR(stag, i)].cccr_address, low, high);
	}
}


static void p4_stop(struct op_msrs const * const msrs)
{
	unsigned int low, high, stag;
	int i;

	stag = get_stagger();

	for (i = 0; i < num_counters; ++i) {
		if (!reset_value[i])
			continue;
		rdmsr(p4_counters[VIRT_CTR(stag, i)].cccr_address, low, high);
		CCCR_SET_DISABLE(low);
		wrmsr(p4_counters[VIRT_CTR(stag, i)].cccr_address, low, high);
	}
}

#ifdef CONFIG_SMP
struct op_x86_model_spec op_p4_ht2_spec = {
	.num_counters		= NUM_COUNTERS_HT2,
	.num_controls		= NUM_CONTROLS_HT2,
	.fill_in_addresses	= &p4_fill_in_addresses,
	.setup_ctrs		= &p4_setup_ctrs,
	.check_ctrs		= &p4_check_ctrs,
	.start			= &p4_start,
	.stop			= &p4_stop,
	.shutdown		= &p4_shutdown
};
#endif

struct op_x86_model_spec op_p4_spec = {
	.num_counters		= NUM_COUNTERS_NON_HT,
	.num_controls		= NUM_CONTROLS_NON_HT,
	.fill_in_addresses	= &p4_fill_in_addresses,
	.setup_ctrs		= &p4_setup_ctrs,
	.check_ctrs		= &p4_check_ctrs,
	.start			= &p4_start,
	.stop			= &p4_stop,
	.shutdown		= &p4_shutdown
};
