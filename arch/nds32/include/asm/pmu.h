/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2008-2018 Andes Technology Corporation */

#ifndef __ASM_PMU_H
#define __ASM_PMU_H

#include <linux/interrupt.h>
#include <linux/perf_event.h>
#include <asm/unistd.h>
#include <asm/bitfield.h>

/* Has special meaning for perf core implementation */
#define HW_OP_UNSUPPORTED		0x0
#define C(_x)				PERF_COUNT_HW_CACHE_##_x
#define CACHE_OP_UNSUPPORTED		0x0

/* Enough for both software and hardware defined events */
#define SOFTWARE_EVENT_MASK		0xFF

#define PFM_OFFSET_MAGIC_0		2	/* DO NOT START FROM 0 */
#define PFM_OFFSET_MAGIC_1		(PFM_OFFSET_MAGIC_0 + 36)
#define PFM_OFFSET_MAGIC_2		(PFM_OFFSET_MAGIC_1 + 36)

enum { PFMC0, PFMC1, PFMC2, MAX_COUNTERS };

u32 PFM_CTL_OVF[3] = { PFM_CTL_mskOVF0, PFM_CTL_mskOVF1,
		       PFM_CTL_mskOVF2 };
u32 PFM_CTL_EN[3] = { PFM_CTL_mskEN0, PFM_CTL_mskEN1,
		      PFM_CTL_mskEN2 };
u32 PFM_CTL_OFFSEL[3] = { PFM_CTL_offSEL0, PFM_CTL_offSEL1,
			  PFM_CTL_offSEL2 };
u32 PFM_CTL_IE[3] = { PFM_CTL_mskIE0, PFM_CTL_mskIE1, PFM_CTL_mskIE2 };
u32 PFM_CTL_KS[3] = { PFM_CTL_mskKS0, PFM_CTL_mskKS1, PFM_CTL_mskKS2 };
u32 PFM_CTL_KU[3] = { PFM_CTL_mskKU0, PFM_CTL_mskKU1, PFM_CTL_mskKU2 };
u32 PFM_CTL_SEL[3] = { PFM_CTL_mskSEL0, PFM_CTL_mskSEL1, PFM_CTL_mskSEL2 };
/*
 * Perf Events' indices
 */
#define NDS32_IDX_CYCLE_COUNTER			0
#define NDS32_IDX_COUNTER0			1
#define NDS32_IDX_COUNTER1			2

/* The events for a given PMU register set. */
struct pmu_hw_events {
	/*
	 * The events that are active on the PMU for the given index.
	 */
	struct perf_event *events[MAX_COUNTERS];

	/*
	 * A 1 bit for an index indicates that the counter is being used for
	 * an event. A 0 means that the counter can be used.
	 */
	unsigned long used_mask[BITS_TO_LONGS(MAX_COUNTERS)];

	/*
	 * Hardware lock to serialize accesses to PMU registers. Needed for the
	 * read/modify/write sequences.
	 */
	raw_spinlock_t pmu_lock;
};

struct nds32_pmu {
	struct pmu pmu;
	cpumask_t active_irqs;
	char *name;
	 irqreturn_t (*handle_irq)(int irq_num, void *dev);
	void (*enable)(struct perf_event *event);
	void (*disable)(struct perf_event *event);
	int (*get_event_idx)(struct pmu_hw_events *hw_events,
			     struct perf_event *event);
	int (*set_event_filter)(struct hw_perf_event *evt,
				struct perf_event_attr *attr);
	u32 (*read_counter)(struct perf_event *event);
	void (*write_counter)(struct perf_event *event, u32 val);
	void (*start)(struct nds32_pmu *nds32_pmu);
	void (*stop)(struct nds32_pmu *nds32_pmu);
	void (*reset)(void *data);
	int (*request_irq)(struct nds32_pmu *nds32_pmu, irq_handler_t handler);
	void (*free_irq)(struct nds32_pmu *nds32_pmu);
	int (*map_event)(struct perf_event *event);
	int num_events;
	atomic_t active_events;
	u64 max_period;
	struct platform_device *plat_device;
	struct pmu_hw_events *(*get_hw_events)(void);
};

#define to_nds32_pmu(p)			(container_of(p, struct nds32_pmu, pmu))

int nds32_pmu_register(struct nds32_pmu *nds32_pmu, int type);

u64 nds32_pmu_event_update(struct perf_event *event);

int nds32_pmu_event_set_period(struct perf_event *event);

/*
 * Common NDS32 SPAv3 event types
 *
 * Note: An implementation may not be able to count all of these events
 * but the encodings are considered to be `reserved' in the case that
 * they are not available.
 *
 * SEL_TOTAL_CYCLES will add an offset is due to ZERO is defined as
 * NOT_SUPPORTED EVENT mapping in generic perf code.
 * You will need to deal it in the event writing implementation.
 */
enum spav3_counter_0_perf_types {
	SPAV3_0_SEL_BASE = -1 + PFM_OFFSET_MAGIC_0,	/* counting symbol */
	SPAV3_0_SEL_TOTAL_CYCLES = 0 + PFM_OFFSET_MAGIC_0,
	SPAV3_0_SEL_COMPLETED_INSTRUCTION = 1 + PFM_OFFSET_MAGIC_0,
	SPAV3_0_SEL_LAST	/* counting symbol */
};

enum spav3_counter_1_perf_types {
	SPAV3_1_SEL_BASE = -1 + PFM_OFFSET_MAGIC_1,	/* counting symbol */
	SPAV3_1_SEL_TOTAL_CYCLES = 0 + PFM_OFFSET_MAGIC_1,
	SPAV3_1_SEL_COMPLETED_INSTRUCTION = 1 + PFM_OFFSET_MAGIC_1,
	SPAV3_1_SEL_CONDITIONAL_BRANCH = 2 + PFM_OFFSET_MAGIC_1,
	SPAV3_1_SEL_TAKEN_CONDITIONAL_BRANCH = 3 + PFM_OFFSET_MAGIC_1,
	SPAV3_1_SEL_PREFETCH_INSTRUCTION = 4 + PFM_OFFSET_MAGIC_1,
	SPAV3_1_SEL_RET_INST = 5 + PFM_OFFSET_MAGIC_1,
	SPAV3_1_SEL_JR_INST = 6 + PFM_OFFSET_MAGIC_1,
	SPAV3_1_SEL_JAL_JRAL_INST = 7 + PFM_OFFSET_MAGIC_1,
	SPAV3_1_SEL_NOP_INST = 8 + PFM_OFFSET_MAGIC_1,
	SPAV3_1_SEL_SCW_INST = 9 + PFM_OFFSET_MAGIC_1,
	SPAV3_1_SEL_ISB_DSB_INST = 10 + PFM_OFFSET_MAGIC_1,
	SPAV3_1_SEL_CCTL_INST = 11 + PFM_OFFSET_MAGIC_1,
	SPAV3_1_SEL_TAKEN_INTERRUPTS = 12 + PFM_OFFSET_MAGIC_1,
	SPAV3_1_SEL_LOADS_COMPLETED = 13 + PFM_OFFSET_MAGIC_1,
	SPAV3_1_SEL_UITLB_ACCESS = 14 + PFM_OFFSET_MAGIC_1,
	SPAV3_1_SEL_UDTLB_ACCESS = 15 + PFM_OFFSET_MAGIC_1,
	SPAV3_1_SEL_MTLB_ACCESS = 16 + PFM_OFFSET_MAGIC_1,
	SPAV3_1_SEL_CODE_CACHE_ACCESS = 17 + PFM_OFFSET_MAGIC_1,
	SPAV3_1_SEL_DATA_DEPENDENCY_STALL_CYCLES = 18 + PFM_OFFSET_MAGIC_1,
	SPAV3_1_SEL_DATA_CACHE_MISS_STALL_CYCLES = 19 + PFM_OFFSET_MAGIC_1,
	SPAV3_1_SEL_DATA_CACHE_ACCESS = 20 + PFM_OFFSET_MAGIC_1,
	SPAV3_1_SEL_DATA_CACHE_MISS = 21 + PFM_OFFSET_MAGIC_1,
	SPAV3_1_SEL_LOAD_DATA_CACHE_ACCESS = 22 + PFM_OFFSET_MAGIC_1,
	SPAV3_1_SEL_STORE_DATA_CACHE_ACCESS = 23 + PFM_OFFSET_MAGIC_1,
	SPAV3_1_SEL_ILM_ACCESS = 24 + PFM_OFFSET_MAGIC_1,
	SPAV3_1_SEL_LSU_BIU_CYCLES = 25 + PFM_OFFSET_MAGIC_1,
	SPAV3_1_SEL_HPTWK_BIU_CYCLES = 26 + PFM_OFFSET_MAGIC_1,
	SPAV3_1_SEL_DMA_BIU_CYCLES = 27 + PFM_OFFSET_MAGIC_1,
	SPAV3_1_SEL_CODE_CACHE_FILL_BIU_CYCLES = 28 + PFM_OFFSET_MAGIC_1,
	SPAV3_1_SEL_LEGAL_UNALIGN_DCACHE_ACCESS = 29 + PFM_OFFSET_MAGIC_1,
	SPAV3_1_SEL_PUSH25 = 30 + PFM_OFFSET_MAGIC_1,
	SPAV3_1_SEL_SYSCALLS_INST = 31 + PFM_OFFSET_MAGIC_1,
	SPAV3_1_SEL_LAST	/* counting symbol */
};

enum spav3_counter_2_perf_types {
	SPAV3_2_SEL_BASE = -1 + PFM_OFFSET_MAGIC_2,	/* counting symbol */
	SPAV3_2_SEL_TOTAL_CYCLES = 0 + PFM_OFFSET_MAGIC_2,
	SPAV3_2_SEL_COMPLETED_INSTRUCTION = 1 + PFM_OFFSET_MAGIC_2,
	SPAV3_2_SEL_CONDITIONAL_BRANCH_MISPREDICT = 2 + PFM_OFFSET_MAGIC_2,
	SPAV3_2_SEL_TAKEN_CONDITIONAL_BRANCH_MISPREDICT =
	    3 + PFM_OFFSET_MAGIC_2,
	SPAV3_2_SEL_PREFETCH_INSTRUCTION_CACHE_HIT = 4 + PFM_OFFSET_MAGIC_2,
	SPAV3_1_SEL_RET_MISPREDICT = 5 + PFM_OFFSET_MAGIC_2,
	SPAV3_1_SEL_IMMEDIATE_J_INST = 6 + PFM_OFFSET_MAGIC_2,
	SPAV3_1_SEL_MULTIPLY_INST = 7 + PFM_OFFSET_MAGIC_2,
	SPAV3_1_SEL_16_BIT_INST = 8 + PFM_OFFSET_MAGIC_2,
	SPAV3_1_SEL_FAILED_SCW_INST = 9 + PFM_OFFSET_MAGIC_2,
	SPAV3_1_SEL_LD_AFTER_ST_CONFLICT_REPLAYS = 10 + PFM_OFFSET_MAGIC_2,
	SPAV3_1_SEL_TAKEN_EXCEPTIONS = 12 + PFM_OFFSET_MAGIC_2,
	SPAV3_1_SEL_STORES_COMPLETED = 13 + PFM_OFFSET_MAGIC_2,
	SPAV3_2_SEL_UITLB_MISS = 14 + PFM_OFFSET_MAGIC_2,
	SPAV3_2_SEL_UDTLB_MISS = 15 + PFM_OFFSET_MAGIC_2,
	SPAV3_2_SEL_MTLB_MISS = 16 + PFM_OFFSET_MAGIC_2,
	SPAV3_2_SEL_CODE_CACHE_MISS = 17 + PFM_OFFSET_MAGIC_2,
	SPAV3_1_SEL_EMPTY_INST_QUEUE_STALL_CYCLES = 18 + PFM_OFFSET_MAGIC_2,
	SPAV3_1_SEL_DATA_WRITE_BACK = 19 + PFM_OFFSET_MAGIC_2,
	SPAV3_2_SEL_DATA_CACHE_MISS = 21 + PFM_OFFSET_MAGIC_2,
	SPAV3_2_SEL_LOAD_DATA_CACHE_MISS = 22 + PFM_OFFSET_MAGIC_2,
	SPAV3_2_SEL_STORE_DATA_CACHE_MISS = 23 + PFM_OFFSET_MAGIC_2,
	SPAV3_1_SEL_DLM_ACCESS = 24 + PFM_OFFSET_MAGIC_2,
	SPAV3_1_SEL_LSU_BIU_REQUEST = 25 + PFM_OFFSET_MAGIC_2,
	SPAV3_1_SEL_HPTWK_BIU_REQUEST = 26 + PFM_OFFSET_MAGIC_2,
	SPAV3_1_SEL_DMA_BIU_REQUEST = 27 + PFM_OFFSET_MAGIC_2,
	SPAV3_1_SEL_CODE_CACHE_FILL_BIU_REQUEST = 28 + PFM_OFFSET_MAGIC_2,
	SPAV3_1_SEL_EXTERNAL_EVENTS = 29 + PFM_OFFSET_MAGIC_2,
	SPAV3_1_SEL_POP25 = 30 + PFM_OFFSET_MAGIC_2,
	SPAV3_2_SEL_LAST	/* counting symbol */
};

/* Get converted event counter index */
static inline int get_converted_event_idx(unsigned long event)
{
	int idx;

	if ((event) > SPAV3_0_SEL_BASE && event < SPAV3_0_SEL_LAST) {
		idx = 0;
	} else if ((event) > SPAV3_1_SEL_BASE && event < SPAV3_1_SEL_LAST) {
		idx = 1;
	} else if ((event) > SPAV3_2_SEL_BASE && event < SPAV3_2_SEL_LAST) {
		idx = 2;
	} else {
		pr_err("GET_CONVERTED_EVENT_IDX PFM counter range error\n");
		return -EPERM;
	}

	return idx;
}

/* Get converted hardware event number */
static inline u32 get_converted_evet_hw_num(u32 event)
{
	if (event > SPAV3_0_SEL_BASE && event < SPAV3_0_SEL_LAST)
		event -= PFM_OFFSET_MAGIC_0;
	else if (event > SPAV3_1_SEL_BASE && event < SPAV3_1_SEL_LAST)
		event -= PFM_OFFSET_MAGIC_1;
	else if (event > SPAV3_2_SEL_BASE && event < SPAV3_2_SEL_LAST)
		event -= PFM_OFFSET_MAGIC_2;
	else if (event != 0)
		pr_err("GET_CONVERTED_EVENT_HW_NUM PFM counter range error\n");

	return event;
}

/*
 * NDS32 HW events mapping
 *
 * The hardware events that we support. We do support cache operations but
 * we have harvard caches and no way to combine instruction and data
 * accesses/misses in hardware.
 */
static const unsigned int nds32_pfm_perf_map[PERF_COUNT_HW_MAX] = {
	[PERF_COUNT_HW_CPU_CYCLES] = SPAV3_0_SEL_TOTAL_CYCLES,
	[PERF_COUNT_HW_INSTRUCTIONS] = SPAV3_1_SEL_COMPLETED_INSTRUCTION,
	[PERF_COUNT_HW_CACHE_REFERENCES] = SPAV3_1_SEL_DATA_CACHE_ACCESS,
	[PERF_COUNT_HW_CACHE_MISSES] = SPAV3_2_SEL_DATA_CACHE_MISS,
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS] = HW_OP_UNSUPPORTED,
	[PERF_COUNT_HW_BRANCH_MISSES] = HW_OP_UNSUPPORTED,
	[PERF_COUNT_HW_BUS_CYCLES] = HW_OP_UNSUPPORTED,
	[PERF_COUNT_HW_STALLED_CYCLES_FRONTEND] = HW_OP_UNSUPPORTED,
	[PERF_COUNT_HW_STALLED_CYCLES_BACKEND] = HW_OP_UNSUPPORTED,
	[PERF_COUNT_HW_REF_CPU_CYCLES] = HW_OP_UNSUPPORTED
};

static const unsigned int nds32_pfm_perf_cache_map[PERF_COUNT_HW_CACHE_MAX]
	[PERF_COUNT_HW_CACHE_OP_MAX]
	[PERF_COUNT_HW_CACHE_RESULT_MAX] = {
	[C(L1D)] = {
		    [C(OP_READ)] = {
				    [C(RESULT_ACCESS)] =
				    SPAV3_1_SEL_LOAD_DATA_CACHE_ACCESS,
				    [C(RESULT_MISS)] =
				    SPAV3_2_SEL_LOAD_DATA_CACHE_MISS,
				    },
		    [C(OP_WRITE)] = {
				     [C(RESULT_ACCESS)] =
				     SPAV3_1_SEL_STORE_DATA_CACHE_ACCESS,
				     [C(RESULT_MISS)] =
				     SPAV3_2_SEL_STORE_DATA_CACHE_MISS,
				     },
		    [C(OP_PREFETCH)] = {
					[C(RESULT_ACCESS)] =
						CACHE_OP_UNSUPPORTED,
					[C(RESULT_MISS)] =
						CACHE_OP_UNSUPPORTED,
					},
		    },
	[C(L1I)] = {
		    [C(OP_READ)] = {
				    [C(RESULT_ACCESS)] =
				    SPAV3_1_SEL_CODE_CACHE_ACCESS,
				    [C(RESULT_MISS)] =
				    SPAV3_2_SEL_CODE_CACHE_MISS,
				    },
		    [C(OP_WRITE)] = {
				     [C(RESULT_ACCESS)] =
				     SPAV3_1_SEL_CODE_CACHE_ACCESS,
				     [C(RESULT_MISS)] =
				     SPAV3_2_SEL_CODE_CACHE_MISS,
				     },
		    [C(OP_PREFETCH)] = {
					[C(RESULT_ACCESS)] =
					CACHE_OP_UNSUPPORTED,
					[C(RESULT_MISS)] = CACHE_OP_UNSUPPORTED,
					},
		    },
	/* TODO: L2CC */
	[C(LL)] = {
		   [C(OP_READ)] = {
				   [C(RESULT_ACCESS)] = CACHE_OP_UNSUPPORTED,
				   [C(RESULT_MISS)] = CACHE_OP_UNSUPPORTED,
				   },
		   [C(OP_WRITE)] = {
				    [C(RESULT_ACCESS)] = CACHE_OP_UNSUPPORTED,
				    [C(RESULT_MISS)] = CACHE_OP_UNSUPPORTED,
				    },
		   [C(OP_PREFETCH)] = {
				       [C(RESULT_ACCESS)] =
				       CACHE_OP_UNSUPPORTED,
				       [C(RESULT_MISS)] = CACHE_OP_UNSUPPORTED,
				       },
		   },
	/* NDS32 PMU does not support TLB read/write hit/miss,
	 * However, it can count access/miss, which mixed with read and write.
	 * Therefore, only READ counter will use it.
	 * We do as possible as we can.
	 */
	[C(DTLB)] = {
		     [C(OP_READ)] = {
				     [C(RESULT_ACCESS)] =
					SPAV3_1_SEL_UDTLB_ACCESS,
				     [C(RESULT_MISS)] =
					SPAV3_2_SEL_UDTLB_MISS,
				     },
		     [C(OP_WRITE)] = {
				      [C(RESULT_ACCESS)] = CACHE_OP_UNSUPPORTED,
				      [C(RESULT_MISS)] = CACHE_OP_UNSUPPORTED,
				      },
		     [C(OP_PREFETCH)] = {
					 [C(RESULT_ACCESS)] =
					 CACHE_OP_UNSUPPORTED,
					 [C(RESULT_MISS)] =
					 CACHE_OP_UNSUPPORTED,
					 },
		     },
	[C(ITLB)] = {
		     [C(OP_READ)] = {
				     [C(RESULT_ACCESS)] =
					SPAV3_1_SEL_UITLB_ACCESS,
				     [C(RESULT_MISS)] =
					SPAV3_2_SEL_UITLB_MISS,
				     },
		     [C(OP_WRITE)] = {
				      [C(RESULT_ACCESS)] =
					CACHE_OP_UNSUPPORTED,
				      [C(RESULT_MISS)] =
					CACHE_OP_UNSUPPORTED,
				      },
		     [C(OP_PREFETCH)] = {
					 [C(RESULT_ACCESS)] =
						CACHE_OP_UNSUPPORTED,
					 [C(RESULT_MISS)] =
						CACHE_OP_UNSUPPORTED,
					 },
		     },
	[C(BPU)] = {		/* What is BPU? */
		    [C(OP_READ)] = {
				    [C(RESULT_ACCESS)] =
					CACHE_OP_UNSUPPORTED,
				    [C(RESULT_MISS)] =
					CACHE_OP_UNSUPPORTED,
				    },
		    [C(OP_WRITE)] = {
				     [C(RESULT_ACCESS)] =
					CACHE_OP_UNSUPPORTED,
				     [C(RESULT_MISS)] =
					CACHE_OP_UNSUPPORTED,
				     },
		    [C(OP_PREFETCH)] = {
					[C(RESULT_ACCESS)] =
						CACHE_OP_UNSUPPORTED,
					[C(RESULT_MISS)] =
						CACHE_OP_UNSUPPORTED,
					},
		    },
	[C(NODE)] = {		/* What is NODE? */
		     [C(OP_READ)] = {
				     [C(RESULT_ACCESS)] = CACHE_OP_UNSUPPORTED,
				     [C(RESULT_MISS)] = CACHE_OP_UNSUPPORTED,
				     },
		     [C(OP_WRITE)] = {
				      [C(RESULT_ACCESS)] = CACHE_OP_UNSUPPORTED,
				      [C(RESULT_MISS)] = CACHE_OP_UNSUPPORTED,
				      },
		     [C(OP_PREFETCH)] = {
					 [C(RESULT_ACCESS)] =
						CACHE_OP_UNSUPPORTED,
					 [C(RESULT_MISS)] =
						CACHE_OP_UNSUPPORTED,
					 },
		     },
};

int nds32_pmu_map_event(struct perf_event *event,
			const unsigned int (*event_map)[PERF_COUNT_HW_MAX],
			const unsigned int (*cache_map)[PERF_COUNT_HW_CACHE_MAX]
			[PERF_COUNT_HW_CACHE_OP_MAX]
			[PERF_COUNT_HW_CACHE_RESULT_MAX], u32 raw_event_mask);

#endif /* __ASM_PMU_H */
