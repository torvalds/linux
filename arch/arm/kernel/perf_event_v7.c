/*
 * ARMv7 Cortex-A8 and Cortex-A9 Performance Events handling code.
 *
 * ARMv7 support: Jean Pihet <jpihet@mvista.com>
 * 2010 (c) MontaVista Software, LLC.
 *
 * Copied from ARMv6 code, with the low level code inspired
 *  by the ARMv7 Oprofile code.
 *
 * Cortex-A8 has up to 4 configurable performance counters and
 *  a single cycle counter.
 * Cortex-A9 has up to 31 configurable performance counters and
 *  a single cycle counter.
 *
 * All counters can be enabled/disabled and IRQ masked separately. The cycle
 *  counter and all 4 performance counters together can be reset separately.
 */

#ifdef CONFIG_CPU_V7
/* Common ARMv7 event types */
enum armv7_perf_types {
	ARMV7_PERFCTR_PMNC_SW_INCR		= 0x00,
	ARMV7_PERFCTR_IFETCH_MISS		= 0x01,
	ARMV7_PERFCTR_ITLB_MISS			= 0x02,
	ARMV7_PERFCTR_DCACHE_REFILL		= 0x03,
	ARMV7_PERFCTR_DCACHE_ACCESS		= 0x04,
	ARMV7_PERFCTR_DTLB_REFILL		= 0x05,
	ARMV7_PERFCTR_DREAD			= 0x06,
	ARMV7_PERFCTR_DWRITE			= 0x07,

	ARMV7_PERFCTR_EXC_TAKEN			= 0x09,
	ARMV7_PERFCTR_EXC_EXECUTED		= 0x0A,
	ARMV7_PERFCTR_CID_WRITE			= 0x0B,
	/* ARMV7_PERFCTR_PC_WRITE is equivalent to HW_BRANCH_INSTRUCTIONS.
	 * It counts:
	 *  - all branch instructions,
	 *  - instructions that explicitly write the PC,
	 *  - exception generating instructions.
	 */
	ARMV7_PERFCTR_PC_WRITE			= 0x0C,
	ARMV7_PERFCTR_PC_IMM_BRANCH		= 0x0D,
	ARMV7_PERFCTR_UNALIGNED_ACCESS		= 0x0F,
	ARMV7_PERFCTR_PC_BRANCH_MIS_PRED	= 0x10,
	ARMV7_PERFCTR_CLOCK_CYCLES		= 0x11,

	ARMV7_PERFCTR_PC_BRANCH_MIS_USED	= 0x12,

	ARMV7_PERFCTR_CPU_CYCLES		= 0xFF
};

/* ARMv7 Cortex-A8 specific event types */
enum armv7_a8_perf_types {
	ARMV7_PERFCTR_INSTR_EXECUTED		= 0x08,

	ARMV7_PERFCTR_PC_PROC_RETURN		= 0x0E,

	ARMV7_PERFCTR_WRITE_BUFFER_FULL		= 0x40,
	ARMV7_PERFCTR_L2_STORE_MERGED		= 0x41,
	ARMV7_PERFCTR_L2_STORE_BUFF		= 0x42,
	ARMV7_PERFCTR_L2_ACCESS			= 0x43,
	ARMV7_PERFCTR_L2_CACH_MISS		= 0x44,
	ARMV7_PERFCTR_AXI_READ_CYCLES		= 0x45,
	ARMV7_PERFCTR_AXI_WRITE_CYCLES		= 0x46,
	ARMV7_PERFCTR_MEMORY_REPLAY		= 0x47,
	ARMV7_PERFCTR_UNALIGNED_ACCESS_REPLAY	= 0x48,
	ARMV7_PERFCTR_L1_DATA_MISS		= 0x49,
	ARMV7_PERFCTR_L1_INST_MISS		= 0x4A,
	ARMV7_PERFCTR_L1_DATA_COLORING		= 0x4B,
	ARMV7_PERFCTR_L1_NEON_DATA		= 0x4C,
	ARMV7_PERFCTR_L1_NEON_CACH_DATA		= 0x4D,
	ARMV7_PERFCTR_L2_NEON			= 0x4E,
	ARMV7_PERFCTR_L2_NEON_HIT		= 0x4F,
	ARMV7_PERFCTR_L1_INST			= 0x50,
	ARMV7_PERFCTR_PC_RETURN_MIS_PRED	= 0x51,
	ARMV7_PERFCTR_PC_BRANCH_FAILED		= 0x52,
	ARMV7_PERFCTR_PC_BRANCH_TAKEN		= 0x53,
	ARMV7_PERFCTR_PC_BRANCH_EXECUTED	= 0x54,
	ARMV7_PERFCTR_OP_EXECUTED		= 0x55,
	ARMV7_PERFCTR_CYCLES_INST_STALL		= 0x56,
	ARMV7_PERFCTR_CYCLES_INST		= 0x57,
	ARMV7_PERFCTR_CYCLES_NEON_DATA_STALL	= 0x58,
	ARMV7_PERFCTR_CYCLES_NEON_INST_STALL	= 0x59,
	ARMV7_PERFCTR_NEON_CYCLES		= 0x5A,

	ARMV7_PERFCTR_PMU0_EVENTS		= 0x70,
	ARMV7_PERFCTR_PMU1_EVENTS		= 0x71,
	ARMV7_PERFCTR_PMU_EVENTS		= 0x72,
};

/* ARMv7 Cortex-A9 specific event types */
enum armv7_a9_perf_types {
	ARMV7_PERFCTR_JAVA_HW_BYTECODE_EXEC	= 0x40,
	ARMV7_PERFCTR_JAVA_SW_BYTECODE_EXEC	= 0x41,
	ARMV7_PERFCTR_JAZELLE_BRANCH_EXEC	= 0x42,

	ARMV7_PERFCTR_COHERENT_LINE_MISS	= 0x50,
	ARMV7_PERFCTR_COHERENT_LINE_HIT		= 0x51,

	ARMV7_PERFCTR_ICACHE_DEP_STALL_CYCLES	= 0x60,
	ARMV7_PERFCTR_DCACHE_DEP_STALL_CYCLES	= 0x61,
	ARMV7_PERFCTR_TLB_MISS_DEP_STALL_CYCLES	= 0x62,
	ARMV7_PERFCTR_STREX_EXECUTED_PASSED	= 0x63,
	ARMV7_PERFCTR_STREX_EXECUTED_FAILED	= 0x64,
	ARMV7_PERFCTR_DATA_EVICTION		= 0x65,
	ARMV7_PERFCTR_ISSUE_STAGE_NO_INST	= 0x66,
	ARMV7_PERFCTR_ISSUE_STAGE_EMPTY		= 0x67,
	ARMV7_PERFCTR_INST_OUT_OF_RENAME_STAGE	= 0x68,

	ARMV7_PERFCTR_PREDICTABLE_FUNCT_RETURNS	= 0x6E,

	ARMV7_PERFCTR_MAIN_UNIT_EXECUTED_INST	= 0x70,
	ARMV7_PERFCTR_SECOND_UNIT_EXECUTED_INST	= 0x71,
	ARMV7_PERFCTR_LD_ST_UNIT_EXECUTED_INST	= 0x72,
	ARMV7_PERFCTR_FP_EXECUTED_INST		= 0x73,
	ARMV7_PERFCTR_NEON_EXECUTED_INST	= 0x74,

	ARMV7_PERFCTR_PLD_FULL_DEP_STALL_CYCLES	= 0x80,
	ARMV7_PERFCTR_DATA_WR_DEP_STALL_CYCLES	= 0x81,
	ARMV7_PERFCTR_ITLB_MISS_DEP_STALL_CYCLES	= 0x82,
	ARMV7_PERFCTR_DTLB_MISS_DEP_STALL_CYCLES	= 0x83,
	ARMV7_PERFCTR_MICRO_ITLB_MISS_DEP_STALL_CYCLES	= 0x84,
	ARMV7_PERFCTR_MICRO_DTLB_MISS_DEP_STALL_CYCLES	= 0x85,
	ARMV7_PERFCTR_DMB_DEP_STALL_CYCLES	= 0x86,

	ARMV7_PERFCTR_INTGR_CLK_ENABLED_CYCLES	= 0x8A,
	ARMV7_PERFCTR_DATA_ENGINE_CLK_EN_CYCLES	= 0x8B,

	ARMV7_PERFCTR_ISB_INST			= 0x90,
	ARMV7_PERFCTR_DSB_INST			= 0x91,
	ARMV7_PERFCTR_DMB_INST			= 0x92,
	ARMV7_PERFCTR_EXT_INTERRUPTS		= 0x93,

	ARMV7_PERFCTR_PLE_CACHE_LINE_RQST_COMPLETED	= 0xA0,
	ARMV7_PERFCTR_PLE_CACHE_LINE_RQST_SKIPPED	= 0xA1,
	ARMV7_PERFCTR_PLE_FIFO_FLUSH		= 0xA2,
	ARMV7_PERFCTR_PLE_RQST_COMPLETED	= 0xA3,
	ARMV7_PERFCTR_PLE_FIFO_OVERFLOW		= 0xA4,
	ARMV7_PERFCTR_PLE_RQST_PROG		= 0xA5
};

/*
 * Cortex-A8 HW events mapping
 *
 * The hardware events that we support. We do support cache operations but
 * we have harvard caches and no way to combine instruction and data
 * accesses/misses in hardware.
 */
static const unsigned armv7_a8_perf_map[PERF_COUNT_HW_MAX] = {
	[PERF_COUNT_HW_CPU_CYCLES]	    = ARMV7_PERFCTR_CPU_CYCLES,
	[PERF_COUNT_HW_INSTRUCTIONS]	    = ARMV7_PERFCTR_INSTR_EXECUTED,
	[PERF_COUNT_HW_CACHE_REFERENCES]    = HW_OP_UNSUPPORTED,
	[PERF_COUNT_HW_CACHE_MISSES]	    = HW_OP_UNSUPPORTED,
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS] = ARMV7_PERFCTR_PC_WRITE,
	[PERF_COUNT_HW_BRANCH_MISSES]	    = ARMV7_PERFCTR_PC_BRANCH_MIS_PRED,
	[PERF_COUNT_HW_BUS_CYCLES]	    = ARMV7_PERFCTR_CLOCK_CYCLES,
};

static const unsigned armv7_a8_perf_cache_map[PERF_COUNT_HW_CACHE_MAX]
					  [PERF_COUNT_HW_CACHE_OP_MAX]
					  [PERF_COUNT_HW_CACHE_RESULT_MAX] = {
	[C(L1D)] = {
		/*
		 * The performance counters don't differentiate between read
		 * and write accesses/misses so this isn't strictly correct,
		 * but it's the best we can do. Writes and reads get
		 * combined.
		 */
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= ARMV7_PERFCTR_DCACHE_ACCESS,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_DCACHE_REFILL,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= ARMV7_PERFCTR_DCACHE_ACCESS,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_DCACHE_REFILL,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
	[C(L1I)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= ARMV7_PERFCTR_L1_INST,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_L1_INST_MISS,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= ARMV7_PERFCTR_L1_INST,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_L1_INST_MISS,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
	[C(LL)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= ARMV7_PERFCTR_L2_ACCESS,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_L2_CACH_MISS,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= ARMV7_PERFCTR_L2_ACCESS,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_L2_CACH_MISS,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
	[C(DTLB)] = {
		/*
		 * Only ITLB misses and DTLB refills are supported.
		 * If users want the DTLB refills misses a raw counter
		 * must be used.
		 */
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_DTLB_REFILL,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_DTLB_REFILL,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
	[C(ITLB)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_ITLB_MISS,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_ITLB_MISS,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
	[C(BPU)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= ARMV7_PERFCTR_PC_WRITE,
			[C(RESULT_MISS)]
					= ARMV7_PERFCTR_PC_BRANCH_MIS_PRED,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= ARMV7_PERFCTR_PC_WRITE,
			[C(RESULT_MISS)]
					= ARMV7_PERFCTR_PC_BRANCH_MIS_PRED,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
};

/*
 * Cortex-A9 HW events mapping
 */
static const unsigned armv7_a9_perf_map[PERF_COUNT_HW_MAX] = {
	[PERF_COUNT_HW_CPU_CYCLES]	    = ARMV7_PERFCTR_CPU_CYCLES,
	[PERF_COUNT_HW_INSTRUCTIONS]	    =
					ARMV7_PERFCTR_INST_OUT_OF_RENAME_STAGE,
	[PERF_COUNT_HW_CACHE_REFERENCES]    = ARMV7_PERFCTR_COHERENT_LINE_HIT,
	[PERF_COUNT_HW_CACHE_MISSES]	    = ARMV7_PERFCTR_COHERENT_LINE_MISS,
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS] = ARMV7_PERFCTR_PC_WRITE,
	[PERF_COUNT_HW_BRANCH_MISSES]	    = ARMV7_PERFCTR_PC_BRANCH_MIS_PRED,
	[PERF_COUNT_HW_BUS_CYCLES]	    = ARMV7_PERFCTR_CLOCK_CYCLES,
};

static const unsigned armv7_a9_perf_cache_map[PERF_COUNT_HW_CACHE_MAX]
					  [PERF_COUNT_HW_CACHE_OP_MAX]
					  [PERF_COUNT_HW_CACHE_RESULT_MAX] = {
	[C(L1D)] = {
		/*
		 * The performance counters don't differentiate between read
		 * and write accesses/misses so this isn't strictly correct,
		 * but it's the best we can do. Writes and reads get
		 * combined.
		 */
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= ARMV7_PERFCTR_DCACHE_ACCESS,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_DCACHE_REFILL,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= ARMV7_PERFCTR_DCACHE_ACCESS,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_DCACHE_REFILL,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
	[C(L1I)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_IFETCH_MISS,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_IFETCH_MISS,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
	[C(LL)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
	[C(DTLB)] = {
		/*
		 * Only ITLB misses and DTLB refills are supported.
		 * If users want the DTLB refills misses a raw counter
		 * must be used.
		 */
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_DTLB_REFILL,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_DTLB_REFILL,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
	[C(ITLB)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_ITLB_MISS,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_ITLB_MISS,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
	[C(BPU)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= ARMV7_PERFCTR_PC_WRITE,
			[C(RESULT_MISS)]
					= ARMV7_PERFCTR_PC_BRANCH_MIS_PRED,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= ARMV7_PERFCTR_PC_WRITE,
			[C(RESULT_MISS)]
					= ARMV7_PERFCTR_PC_BRANCH_MIS_PRED,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
};

/*
 * Perf Events counters
 */
enum armv7_counters {
	ARMV7_CYCLE_COUNTER		= 1,	/* Cycle counter */
	ARMV7_COUNTER0			= 2,	/* First event counter */
};

/*
 * The cycle counter is ARMV7_CYCLE_COUNTER.
 * The first event counter is ARMV7_COUNTER0.
 * The last event counter is (ARMV7_COUNTER0 + armpmu->num_events - 1).
 */
#define	ARMV7_COUNTER_LAST	(ARMV7_COUNTER0 + armpmu->num_events - 1)

/*
 * ARMv7 low level PMNC access
 */

/*
 * Per-CPU PMNC: config reg
 */
#define ARMV7_PMNC_E		(1 << 0) /* Enable all counters */
#define ARMV7_PMNC_P		(1 << 1) /* Reset all counters */
#define ARMV7_PMNC_C		(1 << 2) /* Cycle counter reset */
#define ARMV7_PMNC_D		(1 << 3) /* CCNT counts every 64th cpu cycle */
#define ARMV7_PMNC_X		(1 << 4) /* Export to ETM */
#define ARMV7_PMNC_DP		(1 << 5) /* Disable CCNT if non-invasive debug*/
#define	ARMV7_PMNC_N_SHIFT	11	 /* Number of counters supported */
#define	ARMV7_PMNC_N_MASK	0x1f
#define	ARMV7_PMNC_MASK		0x3f	 /* Mask for writable bits */

/*
 * Available counters
 */
#define ARMV7_CNT0		0	/* First event counter */
#define ARMV7_CCNT		31	/* Cycle counter */

/* Perf Event to low level counters mapping */
#define ARMV7_EVENT_CNT_TO_CNTx	(ARMV7_COUNTER0 - ARMV7_CNT0)

/*
 * CNTENS: counters enable reg
 */
#define ARMV7_CNTENS_P(idx)	(1 << (idx - ARMV7_EVENT_CNT_TO_CNTx))
#define ARMV7_CNTENS_C		(1 << ARMV7_CCNT)

/*
 * CNTENC: counters disable reg
 */
#define ARMV7_CNTENC_P(idx)	(1 << (idx - ARMV7_EVENT_CNT_TO_CNTx))
#define ARMV7_CNTENC_C		(1 << ARMV7_CCNT)

/*
 * INTENS: counters overflow interrupt enable reg
 */
#define ARMV7_INTENS_P(idx)	(1 << (idx - ARMV7_EVENT_CNT_TO_CNTx))
#define ARMV7_INTENS_C		(1 << ARMV7_CCNT)

/*
 * INTENC: counters overflow interrupt disable reg
 */
#define ARMV7_INTENC_P(idx)	(1 << (idx - ARMV7_EVENT_CNT_TO_CNTx))
#define ARMV7_INTENC_C		(1 << ARMV7_CCNT)

/*
 * EVTSEL: Event selection reg
 */
#define	ARMV7_EVTSEL_MASK	0xff		/* Mask for writable bits */

/*
 * SELECT: Counter selection reg
 */
#define	ARMV7_SELECT_MASK	0x1f		/* Mask for writable bits */

/*
 * FLAG: counters overflow flag status reg
 */
#define ARMV7_FLAG_P(idx)	(1 << (idx - ARMV7_EVENT_CNT_TO_CNTx))
#define ARMV7_FLAG_C		(1 << ARMV7_CCNT)
#define	ARMV7_FLAG_MASK		0xffffffff	/* Mask for writable bits */
#define	ARMV7_OVERFLOWED_MASK	ARMV7_FLAG_MASK

static inline unsigned long armv7_pmnc_read(void)
{
	u32 val;
	asm volatile("mrc p15, 0, %0, c9, c12, 0" : "=r"(val));
	return val;
}

static inline void armv7_pmnc_write(unsigned long val)
{
	val &= ARMV7_PMNC_MASK;
	asm volatile("mcr p15, 0, %0, c9, c12, 0" : : "r"(val));
}

static inline int armv7_pmnc_has_overflowed(unsigned long pmnc)
{
	return pmnc & ARMV7_OVERFLOWED_MASK;
}

static inline int armv7_pmnc_counter_has_overflowed(unsigned long pmnc,
					enum armv7_counters counter)
{
	int ret = 0;

	if (counter == ARMV7_CYCLE_COUNTER)
		ret = pmnc & ARMV7_FLAG_C;
	else if ((counter >= ARMV7_COUNTER0) && (counter <= ARMV7_COUNTER_LAST))
		ret = pmnc & ARMV7_FLAG_P(counter);
	else
		pr_err("CPU%u checking wrong counter %d overflow status\n",
			smp_processor_id(), counter);

	return ret;
}

static inline int armv7_pmnc_select_counter(unsigned int idx)
{
	u32 val;

	if ((idx < ARMV7_COUNTER0) || (idx > ARMV7_COUNTER_LAST)) {
		pr_err("CPU%u selecting wrong PMNC counter"
			" %d\n", smp_processor_id(), idx);
		return -1;
	}

	val = (idx - ARMV7_EVENT_CNT_TO_CNTx) & ARMV7_SELECT_MASK;
	asm volatile("mcr p15, 0, %0, c9, c12, 5" : : "r" (val));

	return idx;
}

static inline u32 armv7pmu_read_counter(int idx)
{
	unsigned long value = 0;

	if (idx == ARMV7_CYCLE_COUNTER)
		asm volatile("mrc p15, 0, %0, c9, c13, 0" : "=r" (value));
	else if ((idx >= ARMV7_COUNTER0) && (idx <= ARMV7_COUNTER_LAST)) {
		if (armv7_pmnc_select_counter(idx) == idx)
			asm volatile("mrc p15, 0, %0, c9, c13, 2"
				     : "=r" (value));
	} else
		pr_err("CPU%u reading wrong counter %d\n",
			smp_processor_id(), idx);

	return value;
}

static inline void armv7pmu_write_counter(int idx, u32 value)
{
	if (idx == ARMV7_CYCLE_COUNTER)
		asm volatile("mcr p15, 0, %0, c9, c13, 0" : : "r" (value));
	else if ((idx >= ARMV7_COUNTER0) && (idx <= ARMV7_COUNTER_LAST)) {
		if (armv7_pmnc_select_counter(idx) == idx)
			asm volatile("mcr p15, 0, %0, c9, c13, 2"
				     : : "r" (value));
	} else
		pr_err("CPU%u writing wrong counter %d\n",
			smp_processor_id(), idx);
}

static inline void armv7_pmnc_write_evtsel(unsigned int idx, u32 val)
{
	if (armv7_pmnc_select_counter(idx) == idx) {
		val &= ARMV7_EVTSEL_MASK;
		asm volatile("mcr p15, 0, %0, c9, c13, 1" : : "r" (val));
	}
}

static inline u32 armv7_pmnc_enable_counter(unsigned int idx)
{
	u32 val;

	if ((idx != ARMV7_CYCLE_COUNTER) &&
	    ((idx < ARMV7_COUNTER0) || (idx > ARMV7_COUNTER_LAST))) {
		pr_err("CPU%u enabling wrong PMNC counter"
			" %d\n", smp_processor_id(), idx);
		return -1;
	}

	if (idx == ARMV7_CYCLE_COUNTER)
		val = ARMV7_CNTENS_C;
	else
		val = ARMV7_CNTENS_P(idx);

	asm volatile("mcr p15, 0, %0, c9, c12, 1" : : "r" (val));

	return idx;
}

static inline u32 armv7_pmnc_disable_counter(unsigned int idx)
{
	u32 val;


	if ((idx != ARMV7_CYCLE_COUNTER) &&
	    ((idx < ARMV7_COUNTER0) || (idx > ARMV7_COUNTER_LAST))) {
		pr_err("CPU%u disabling wrong PMNC counter"
			" %d\n", smp_processor_id(), idx);
		return -1;
	}

	if (idx == ARMV7_CYCLE_COUNTER)
		val = ARMV7_CNTENC_C;
	else
		val = ARMV7_CNTENC_P(idx);

	asm volatile("mcr p15, 0, %0, c9, c12, 2" : : "r" (val));

	return idx;
}

static inline u32 armv7_pmnc_enable_intens(unsigned int idx)
{
	u32 val;

	if ((idx != ARMV7_CYCLE_COUNTER) &&
	    ((idx < ARMV7_COUNTER0) || (idx > ARMV7_COUNTER_LAST))) {
		pr_err("CPU%u enabling wrong PMNC counter"
			" interrupt enable %d\n", smp_processor_id(), idx);
		return -1;
	}

	if (idx == ARMV7_CYCLE_COUNTER)
		val = ARMV7_INTENS_C;
	else
		val = ARMV7_INTENS_P(idx);

	asm volatile("mcr p15, 0, %0, c9, c14, 1" : : "r" (val));

	return idx;
}

static inline u32 armv7_pmnc_disable_intens(unsigned int idx)
{
	u32 val;

	if ((idx != ARMV7_CYCLE_COUNTER) &&
	    ((idx < ARMV7_COUNTER0) || (idx > ARMV7_COUNTER_LAST))) {
		pr_err("CPU%u disabling wrong PMNC counter"
			" interrupt enable %d\n", smp_processor_id(), idx);
		return -1;
	}

	if (idx == ARMV7_CYCLE_COUNTER)
		val = ARMV7_INTENC_C;
	else
		val = ARMV7_INTENC_P(idx);

	asm volatile("mcr p15, 0, %0, c9, c14, 2" : : "r" (val));

	return idx;
}

static inline u32 armv7_pmnc_getreset_flags(void)
{
	u32 val;

	/* Read */
	asm volatile("mrc p15, 0, %0, c9, c12, 3" : "=r" (val));

	/* Write to clear flags */
	val &= ARMV7_FLAG_MASK;
	asm volatile("mcr p15, 0, %0, c9, c12, 3" : : "r" (val));

	return val;
}

#ifdef DEBUG
static void armv7_pmnc_dump_regs(void)
{
	u32 val;
	unsigned int cnt;

	printk(KERN_INFO "PMNC registers dump:\n");

	asm volatile("mrc p15, 0, %0, c9, c12, 0" : "=r" (val));
	printk(KERN_INFO "PMNC  =0x%08x\n", val);

	asm volatile("mrc p15, 0, %0, c9, c12, 1" : "=r" (val));
	printk(KERN_INFO "CNTENS=0x%08x\n", val);

	asm volatile("mrc p15, 0, %0, c9, c14, 1" : "=r" (val));
	printk(KERN_INFO "INTENS=0x%08x\n", val);

	asm volatile("mrc p15, 0, %0, c9, c12, 3" : "=r" (val));
	printk(KERN_INFO "FLAGS =0x%08x\n", val);

	asm volatile("mrc p15, 0, %0, c9, c12, 5" : "=r" (val));
	printk(KERN_INFO "SELECT=0x%08x\n", val);

	asm volatile("mrc p15, 0, %0, c9, c13, 0" : "=r" (val));
	printk(KERN_INFO "CCNT  =0x%08x\n", val);

	for (cnt = ARMV7_COUNTER0; cnt < ARMV7_COUNTER_LAST; cnt++) {
		armv7_pmnc_select_counter(cnt);
		asm volatile("mrc p15, 0, %0, c9, c13, 2" : "=r" (val));
		printk(KERN_INFO "CNT[%d] count =0x%08x\n",
			cnt-ARMV7_EVENT_CNT_TO_CNTx, val);
		asm volatile("mrc p15, 0, %0, c9, c13, 1" : "=r" (val));
		printk(KERN_INFO "CNT[%d] evtsel=0x%08x\n",
			cnt-ARMV7_EVENT_CNT_TO_CNTx, val);
	}
}
#endif

static void armv7pmu_enable_event(struct hw_perf_event *hwc, int idx)
{
	unsigned long flags;

	/*
	 * Enable counter and interrupt, and set the counter to count
	 * the event that we're interested in.
	 */
	spin_lock_irqsave(&pmu_lock, flags);

	/*
	 * Disable counter
	 */
	armv7_pmnc_disable_counter(idx);

	/*
	 * Set event (if destined for PMNx counters)
	 * We don't need to set the event if it's a cycle count
	 */
	if (idx != ARMV7_CYCLE_COUNTER)
		armv7_pmnc_write_evtsel(idx, hwc->config_base);

	/*
	 * Enable interrupt for this counter
	 */
	armv7_pmnc_enable_intens(idx);

	/*
	 * Enable counter
	 */
	armv7_pmnc_enable_counter(idx);

	spin_unlock_irqrestore(&pmu_lock, flags);
}

static void armv7pmu_disable_event(struct hw_perf_event *hwc, int idx)
{
	unsigned long flags;

	/*
	 * Disable counter and interrupt
	 */
	spin_lock_irqsave(&pmu_lock, flags);

	/*
	 * Disable counter
	 */
	armv7_pmnc_disable_counter(idx);

	/*
	 * Disable interrupt for this counter
	 */
	armv7_pmnc_disable_intens(idx);

	spin_unlock_irqrestore(&pmu_lock, flags);
}

static irqreturn_t armv7pmu_handle_irq(int irq_num, void *dev)
{
	unsigned long pmnc;
	struct perf_sample_data data;
	struct cpu_hw_events *cpuc;
	struct pt_regs *regs;
	int idx;

	/*
	 * Get and reset the IRQ flags
	 */
	pmnc = armv7_pmnc_getreset_flags();

	/*
	 * Did an overflow occur?
	 */
	if (!armv7_pmnc_has_overflowed(pmnc))
		return IRQ_NONE;

	/*
	 * Handle the counter(s) overflow(s)
	 */
	regs = get_irq_regs();

	perf_sample_data_init(&data, 0);

	cpuc = &__get_cpu_var(cpu_hw_events);
	for (idx = 0; idx <= armpmu->num_events; ++idx) {
		struct perf_event *event = cpuc->events[idx];
		struct hw_perf_event *hwc;

		if (!test_bit(idx, cpuc->active_mask))
			continue;

		/*
		 * We have a single interrupt for all counters. Check that
		 * each counter has overflowed before we process it.
		 */
		if (!armv7_pmnc_counter_has_overflowed(pmnc, idx))
			continue;

		hwc = &event->hw;
		armpmu_event_update(event, hwc, idx);
		data.period = event->hw.last_period;
		if (!armpmu_event_set_period(event, hwc, idx))
			continue;

		if (perf_event_overflow(event, 0, &data, regs))
			armpmu->disable(hwc, idx);
	}

	/*
	 * Handle the pending perf events.
	 *
	 * Note: this call *must* be run with interrupts disabled. For
	 * platforms that can have the PMU interrupts raised as an NMI, this
	 * will not work.
	 */
	irq_work_run();

	return IRQ_HANDLED;
}

static void armv7pmu_start(void)
{
	unsigned long flags;

	spin_lock_irqsave(&pmu_lock, flags);
	/* Enable all counters */
	armv7_pmnc_write(armv7_pmnc_read() | ARMV7_PMNC_E);
	spin_unlock_irqrestore(&pmu_lock, flags);
}

static void armv7pmu_stop(void)
{
	unsigned long flags;

	spin_lock_irqsave(&pmu_lock, flags);
	/* Disable all counters */
	armv7_pmnc_write(armv7_pmnc_read() & ~ARMV7_PMNC_E);
	spin_unlock_irqrestore(&pmu_lock, flags);
}

static int armv7pmu_get_event_idx(struct cpu_hw_events *cpuc,
				  struct hw_perf_event *event)
{
	int idx;

	/* Always place a cycle counter into the cycle counter. */
	if (event->config_base == ARMV7_PERFCTR_CPU_CYCLES) {
		if (test_and_set_bit(ARMV7_CYCLE_COUNTER, cpuc->used_mask))
			return -EAGAIN;

		return ARMV7_CYCLE_COUNTER;
	} else {
		/*
		 * For anything other than a cycle counter, try and use
		 * the events counters
		 */
		for (idx = ARMV7_COUNTER0; idx <= armpmu->num_events; ++idx) {
			if (!test_and_set_bit(idx, cpuc->used_mask))
				return idx;
		}

		/* The counters are all in use. */
		return -EAGAIN;
	}
}

static struct arm_pmu armv7pmu = {
	.handle_irq		= armv7pmu_handle_irq,
	.enable			= armv7pmu_enable_event,
	.disable		= armv7pmu_disable_event,
	.read_counter		= armv7pmu_read_counter,
	.write_counter		= armv7pmu_write_counter,
	.get_event_idx		= armv7pmu_get_event_idx,
	.start			= armv7pmu_start,
	.stop			= armv7pmu_stop,
	.raw_event_mask		= 0xFF,
	.max_period		= (1LLU << 32) - 1,
};

static u32 __init armv7_reset_read_pmnc(void)
{
	u32 nb_cnt;

	/* Initialize & Reset PMNC: C and P bits */
	armv7_pmnc_write(ARMV7_PMNC_P | ARMV7_PMNC_C);

	/* Read the nb of CNTx counters supported from PMNC */
	nb_cnt = (armv7_pmnc_read() >> ARMV7_PMNC_N_SHIFT) & ARMV7_PMNC_N_MASK;

	/* Add the CPU cycles counter and return */
	return nb_cnt + 1;
}

static const struct arm_pmu *__init armv7_a8_pmu_init(void)
{
	armv7pmu.id		= ARM_PERF_PMU_ID_CA8;
	armv7pmu.name		= "ARMv7 Cortex-A8";
	armv7pmu.cache_map	= &armv7_a8_perf_cache_map;
	armv7pmu.event_map	= &armv7_a8_perf_map;
	armv7pmu.num_events	= armv7_reset_read_pmnc();
	return &armv7pmu;
}

static const struct arm_pmu *__init armv7_a9_pmu_init(void)
{
	armv7pmu.id		= ARM_PERF_PMU_ID_CA9;
	armv7pmu.name		= "ARMv7 Cortex-A9";
	armv7pmu.cache_map	= &armv7_a9_perf_cache_map;
	armv7pmu.event_map	= &armv7_a9_perf_map;
	armv7pmu.num_events	= armv7_reset_read_pmnc();
	return &armv7pmu;
}
#else
static const struct arm_pmu *__init armv7_a8_pmu_init(void)
{
	return NULL;
}

static const struct arm_pmu *__init armv7_a9_pmu_init(void)
{
	return NULL;
}
#endif	/* CONFIG_CPU_V7 */
