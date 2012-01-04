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

static struct arm_pmu armv7pmu;

/*
 * Common ARMv7 event types
 *
 * Note: An implementation may not be able to count all of these events
 * but the encodings are considered to be `reserved' in the case that
 * they are not available.
 */
enum armv7_perf_types {
	ARMV7_PERFCTR_PMNC_SW_INCR		= 0x00,
	ARMV7_PERFCTR_IFETCH_MISS		= 0x01,
	ARMV7_PERFCTR_ITLB_MISS			= 0x02,
	ARMV7_PERFCTR_DCACHE_REFILL		= 0x03,	/* L1 */
	ARMV7_PERFCTR_DCACHE_ACCESS		= 0x04,	/* L1 */
	ARMV7_PERFCTR_DTLB_REFILL		= 0x05,
	ARMV7_PERFCTR_DREAD			= 0x06,
	ARMV7_PERFCTR_DWRITE			= 0x07,
	ARMV7_PERFCTR_INSTR_EXECUTED		= 0x08,
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
	ARMV7_PERFCTR_PC_PROC_RETURN		= 0x0E,
	ARMV7_PERFCTR_UNALIGNED_ACCESS		= 0x0F,

	/* These events are defined by the PMUv2 supplement (ARM DDI 0457A). */
	ARMV7_PERFCTR_PC_BRANCH_MIS_PRED	= 0x10,
	ARMV7_PERFCTR_CLOCK_CYCLES		= 0x11,
	ARMV7_PERFCTR_PC_BRANCH_PRED		= 0x12,
	ARMV7_PERFCTR_MEM_ACCESS		= 0x13,
	ARMV7_PERFCTR_L1_ICACHE_ACCESS		= 0x14,
	ARMV7_PERFCTR_L1_DCACHE_WB		= 0x15,
	ARMV7_PERFCTR_L2_DCACHE_ACCESS		= 0x16,
	ARMV7_PERFCTR_L2_DCACHE_REFILL		= 0x17,
	ARMV7_PERFCTR_L2_DCACHE_WB		= 0x18,
	ARMV7_PERFCTR_BUS_ACCESS		= 0x19,
	ARMV7_PERFCTR_MEMORY_ERROR		= 0x1A,
	ARMV7_PERFCTR_INSTR_SPEC		= 0x1B,
	ARMV7_PERFCTR_TTBR_WRITE		= 0x1C,
	ARMV7_PERFCTR_BUS_CYCLES		= 0x1D,

	ARMV7_PERFCTR_CPU_CYCLES		= 0xFF
};

/* ARMv7 Cortex-A8 specific event types */
enum armv7_a8_perf_types {
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

/* ARMv7 Cortex-A5 specific event types */
enum armv7_a5_perf_types {
	ARMV7_PERFCTR_IRQ_TAKEN			= 0x86,
	ARMV7_PERFCTR_FIQ_TAKEN			= 0x87,

	ARMV7_PERFCTR_EXT_MEM_RQST		= 0xc0,
	ARMV7_PERFCTR_NC_EXT_MEM_RQST		= 0xc1,
	ARMV7_PERFCTR_PREFETCH_LINEFILL		= 0xc2,
	ARMV7_PERFCTR_PREFETCH_LINEFILL_DROP	= 0xc3,
	ARMV7_PERFCTR_ENTER_READ_ALLOC		= 0xc4,
	ARMV7_PERFCTR_READ_ALLOC		= 0xc5,

	ARMV7_PERFCTR_STALL_SB_FULL		= 0xc9,
};

/* ARMv7 Cortex-A15 specific event types */
enum armv7_a15_perf_types {
	ARMV7_PERFCTR_L1_DCACHE_READ_ACCESS	= 0x40,
	ARMV7_PERFCTR_L1_DCACHE_WRITE_ACCESS	= 0x41,
	ARMV7_PERFCTR_L1_DCACHE_READ_REFILL	= 0x42,
	ARMV7_PERFCTR_L1_DCACHE_WRITE_REFILL	= 0x43,

	ARMV7_PERFCTR_L1_DTLB_READ_REFILL	= 0x4C,
	ARMV7_PERFCTR_L1_DTLB_WRITE_REFILL	= 0x4D,

	ARMV7_PERFCTR_L2_DCACHE_READ_ACCESS	= 0x50,
	ARMV7_PERFCTR_L2_DCACHE_WRITE_ACCESS	= 0x51,
	ARMV7_PERFCTR_L2_DCACHE_READ_REFILL	= 0x52,
	ARMV7_PERFCTR_L2_DCACHE_WRITE_REFILL	= 0x53,

	ARMV7_PERFCTR_SPEC_PC_WRITE		= 0x76,
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
	[C(NODE)] = {
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
};

/*
 * Cortex-A9 HW events mapping
 */
static const unsigned armv7_a9_perf_map[PERF_COUNT_HW_MAX] = {
	[PERF_COUNT_HW_CPU_CYCLES]	    = ARMV7_PERFCTR_CPU_CYCLES,
	[PERF_COUNT_HW_INSTRUCTIONS]	    =
					ARMV7_PERFCTR_INST_OUT_OF_RENAME_STAGE,
	[PERF_COUNT_HW_CACHE_REFERENCES]    = ARMV7_PERFCTR_DCACHE_ACCESS,
	[PERF_COUNT_HW_CACHE_MISSES]	    = ARMV7_PERFCTR_DCACHE_REFILL,
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
	[C(NODE)] = {
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
};

/*
 * Cortex-A5 HW events mapping
 */
static const unsigned armv7_a5_perf_map[PERF_COUNT_HW_MAX] = {
	[PERF_COUNT_HW_CPU_CYCLES]	    = ARMV7_PERFCTR_CPU_CYCLES,
	[PERF_COUNT_HW_INSTRUCTIONS]	    = ARMV7_PERFCTR_INSTR_EXECUTED,
	[PERF_COUNT_HW_CACHE_REFERENCES]    = HW_OP_UNSUPPORTED,
	[PERF_COUNT_HW_CACHE_MISSES]	    = HW_OP_UNSUPPORTED,
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS] = ARMV7_PERFCTR_PC_WRITE,
	[PERF_COUNT_HW_BRANCH_MISSES]	    = ARMV7_PERFCTR_PC_BRANCH_MIS_PRED,
	[PERF_COUNT_HW_BUS_CYCLES]	    = HW_OP_UNSUPPORTED,
};

static const unsigned armv7_a5_perf_cache_map[PERF_COUNT_HW_CACHE_MAX]
					[PERF_COUNT_HW_CACHE_OP_MAX]
					[PERF_COUNT_HW_CACHE_RESULT_MAX] = {
	[C(L1D)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]
					= ARMV7_PERFCTR_DCACHE_ACCESS,
			[C(RESULT_MISS)]
					= ARMV7_PERFCTR_DCACHE_REFILL,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]
					= ARMV7_PERFCTR_DCACHE_ACCESS,
			[C(RESULT_MISS)]
					= ARMV7_PERFCTR_DCACHE_REFILL,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]
					= ARMV7_PERFCTR_PREFETCH_LINEFILL,
			[C(RESULT_MISS)]
					= ARMV7_PERFCTR_PREFETCH_LINEFILL_DROP,
		},
	},
	[C(L1I)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= ARMV7_PERFCTR_L1_ICACHE_ACCESS,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_IFETCH_MISS,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= ARMV7_PERFCTR_L1_ICACHE_ACCESS,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_IFETCH_MISS,
		},
		/*
		 * The prefetch counters don't differentiate between the I
		 * side and the D side.
		 */
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]
					= ARMV7_PERFCTR_PREFETCH_LINEFILL,
			[C(RESULT_MISS)]
					= ARMV7_PERFCTR_PREFETCH_LINEFILL_DROP,
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
			[C(RESULT_ACCESS)]	= ARMV7_PERFCTR_PC_BRANCH_PRED,
			[C(RESULT_MISS)]
					= ARMV7_PERFCTR_PC_BRANCH_MIS_PRED,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= ARMV7_PERFCTR_PC_BRANCH_PRED,
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
 * Cortex-A15 HW events mapping
 */
static const unsigned armv7_a15_perf_map[PERF_COUNT_HW_MAX] = {
	[PERF_COUNT_HW_CPU_CYCLES]	    = ARMV7_PERFCTR_CPU_CYCLES,
	[PERF_COUNT_HW_INSTRUCTIONS]	    = ARMV7_PERFCTR_INSTR_EXECUTED,
	[PERF_COUNT_HW_CACHE_REFERENCES]    = HW_OP_UNSUPPORTED,
	[PERF_COUNT_HW_CACHE_MISSES]	    = HW_OP_UNSUPPORTED,
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS] = ARMV7_PERFCTR_SPEC_PC_WRITE,
	[PERF_COUNT_HW_BRANCH_MISSES]	    = ARMV7_PERFCTR_PC_BRANCH_MIS_PRED,
	[PERF_COUNT_HW_BUS_CYCLES]	    = ARMV7_PERFCTR_BUS_CYCLES,
};

static const unsigned armv7_a15_perf_cache_map[PERF_COUNT_HW_CACHE_MAX]
					[PERF_COUNT_HW_CACHE_OP_MAX]
					[PERF_COUNT_HW_CACHE_RESULT_MAX] = {
	[C(L1D)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]
					= ARMV7_PERFCTR_L1_DCACHE_READ_ACCESS,
			[C(RESULT_MISS)]
					= ARMV7_PERFCTR_L1_DCACHE_READ_REFILL,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]
					= ARMV7_PERFCTR_L1_DCACHE_WRITE_ACCESS,
			[C(RESULT_MISS)]
					= ARMV7_PERFCTR_L1_DCACHE_WRITE_REFILL,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
	[C(L1I)] = {
		/*
		 * Not all performance counters differentiate between read
		 * and write accesses/misses so we're not always strictly
		 * correct, but it's the best we can do. Writes and reads get
		 * combined in these cases.
		 */
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= ARMV7_PERFCTR_L1_ICACHE_ACCESS,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_IFETCH_MISS,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= ARMV7_PERFCTR_L1_ICACHE_ACCESS,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_IFETCH_MISS,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
	[C(LL)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]
					= ARMV7_PERFCTR_L2_DCACHE_READ_ACCESS,
			[C(RESULT_MISS)]
					= ARMV7_PERFCTR_L2_DCACHE_READ_REFILL,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]
					= ARMV7_PERFCTR_L2_DCACHE_WRITE_ACCESS,
			[C(RESULT_MISS)]
					= ARMV7_PERFCTR_L2_DCACHE_WRITE_REFILL,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
	[C(DTLB)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]
					= ARMV7_PERFCTR_L1_DTLB_READ_REFILL,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]
					= ARMV7_PERFCTR_L1_DTLB_WRITE_REFILL,
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
			[C(RESULT_ACCESS)]	= ARMV7_PERFCTR_PC_BRANCH_PRED,
			[C(RESULT_MISS)]
					= ARMV7_PERFCTR_PC_BRANCH_MIS_PRED,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= ARMV7_PERFCTR_PC_BRANCH_PRED,
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
 * Perf Events' indices
 */
#define	ARMV7_IDX_CYCLE_COUNTER	0
#define	ARMV7_IDX_COUNTER0	1
#define	ARMV7_IDX_COUNTER_LAST	(ARMV7_IDX_CYCLE_COUNTER + cpu_pmu->num_events - 1)

#define	ARMV7_MAX_COUNTERS	32
#define	ARMV7_COUNTER_MASK	(ARMV7_MAX_COUNTERS - 1)

/*
 * ARMv7 low level PMNC access
 */

/*
 * Perf Event to low level counters mapping
 */
#define	ARMV7_IDX_TO_COUNTER(x)	\
	(((x) - ARMV7_IDX_COUNTER0) & ARMV7_COUNTER_MASK)

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
 * FLAG: counters overflow flag status reg
 */
#define	ARMV7_FLAG_MASK		0xffffffff	/* Mask for writable bits */
#define	ARMV7_OVERFLOWED_MASK	ARMV7_FLAG_MASK

/*
 * PMXEVTYPER: Event selection reg
 */
#define	ARMV7_EVTYPE_MASK	0xc00000ff	/* Mask for writable bits */
#define	ARMV7_EVTYPE_EVENT	0xff		/* Mask for EVENT bits */

/*
 * Event filters for PMUv2
 */
#define	ARMV7_EXCLUDE_PL1	(1 << 31)
#define	ARMV7_EXCLUDE_USER	(1 << 30)
#define	ARMV7_INCLUDE_HYP	(1 << 27)

static inline u32 armv7_pmnc_read(void)
{
	u32 val;
	asm volatile("mrc p15, 0, %0, c9, c12, 0" : "=r"(val));
	return val;
}

static inline void armv7_pmnc_write(u32 val)
{
	val &= ARMV7_PMNC_MASK;
	isb();
	asm volatile("mcr p15, 0, %0, c9, c12, 0" : : "r"(val));
}

static inline int armv7_pmnc_has_overflowed(u32 pmnc)
{
	return pmnc & ARMV7_OVERFLOWED_MASK;
}

static inline int armv7_pmnc_counter_valid(int idx)
{
	return idx >= ARMV7_IDX_CYCLE_COUNTER && idx <= ARMV7_IDX_COUNTER_LAST;
}

static inline int armv7_pmnc_counter_has_overflowed(u32 pmnc, int idx)
{
	int ret = 0;
	u32 counter;

	if (!armv7_pmnc_counter_valid(idx)) {
		pr_err("CPU%u checking wrong counter %d overflow status\n",
			smp_processor_id(), idx);
	} else {
		counter = ARMV7_IDX_TO_COUNTER(idx);
		ret = pmnc & BIT(counter);
	}

	return ret;
}

static inline int armv7_pmnc_select_counter(int idx)
{
	u32 counter;

	if (!armv7_pmnc_counter_valid(idx)) {
		pr_err("CPU%u selecting wrong PMNC counter %d\n",
			smp_processor_id(), idx);
		return -EINVAL;
	}

	counter = ARMV7_IDX_TO_COUNTER(idx);
	asm volatile("mcr p15, 0, %0, c9, c12, 5" : : "r" (counter));
	isb();

	return idx;
}

static inline u32 armv7pmu_read_counter(int idx)
{
	u32 value = 0;

	if (!armv7_pmnc_counter_valid(idx))
		pr_err("CPU%u reading wrong counter %d\n",
			smp_processor_id(), idx);
	else if (idx == ARMV7_IDX_CYCLE_COUNTER)
		asm volatile("mrc p15, 0, %0, c9, c13, 0" : "=r" (value));
	else if (armv7_pmnc_select_counter(idx) == idx)
		asm volatile("mrc p15, 0, %0, c9, c13, 2" : "=r" (value));

	return value;
}

static inline void armv7pmu_write_counter(int idx, u32 value)
{
	if (!armv7_pmnc_counter_valid(idx))
		pr_err("CPU%u writing wrong counter %d\n",
			smp_processor_id(), idx);
	else if (idx == ARMV7_IDX_CYCLE_COUNTER)
		asm volatile("mcr p15, 0, %0, c9, c13, 0" : : "r" (value));
	else if (armv7_pmnc_select_counter(idx) == idx)
		asm volatile("mcr p15, 0, %0, c9, c13, 2" : : "r" (value));
}

static inline void armv7_pmnc_write_evtsel(int idx, u32 val)
{
	if (armv7_pmnc_select_counter(idx) == idx) {
		val &= ARMV7_EVTYPE_MASK;
		asm volatile("mcr p15, 0, %0, c9, c13, 1" : : "r" (val));
	}
}

static inline int armv7_pmnc_enable_counter(int idx)
{
	u32 counter;

	if (!armv7_pmnc_counter_valid(idx)) {
		pr_err("CPU%u enabling wrong PMNC counter %d\n",
			smp_processor_id(), idx);
		return -EINVAL;
	}

	counter = ARMV7_IDX_TO_COUNTER(idx);
	asm volatile("mcr p15, 0, %0, c9, c12, 1" : : "r" (BIT(counter)));
	return idx;
}

static inline int armv7_pmnc_disable_counter(int idx)
{
	u32 counter;

	if (!armv7_pmnc_counter_valid(idx)) {
		pr_err("CPU%u disabling wrong PMNC counter %d\n",
			smp_processor_id(), idx);
		return -EINVAL;
	}

	counter = ARMV7_IDX_TO_COUNTER(idx);
	asm volatile("mcr p15, 0, %0, c9, c12, 2" : : "r" (BIT(counter)));
	return idx;
}

static inline int armv7_pmnc_enable_intens(int idx)
{
	u32 counter;

	if (!armv7_pmnc_counter_valid(idx)) {
		pr_err("CPU%u enabling wrong PMNC counter IRQ enable %d\n",
			smp_processor_id(), idx);
		return -EINVAL;
	}

	counter = ARMV7_IDX_TO_COUNTER(idx);
	asm volatile("mcr p15, 0, %0, c9, c14, 1" : : "r" (BIT(counter)));
	return idx;
}

static inline int armv7_pmnc_disable_intens(int idx)
{
	u32 counter;

	if (!armv7_pmnc_counter_valid(idx)) {
		pr_err("CPU%u disabling wrong PMNC counter IRQ enable %d\n",
			smp_processor_id(), idx);
		return -EINVAL;
	}

	counter = ARMV7_IDX_TO_COUNTER(idx);
	asm volatile("mcr p15, 0, %0, c9, c14, 2" : : "r" (BIT(counter)));
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

	for (cnt = ARMV7_IDX_COUNTER0; cnt <= ARMV7_IDX_COUNTER_LAST; cnt++) {
		armv7_pmnc_select_counter(cnt);
		asm volatile("mrc p15, 0, %0, c9, c13, 2" : "=r" (val));
		printk(KERN_INFO "CNT[%d] count =0x%08x\n",
			ARMV7_IDX_TO_COUNTER(cnt), val);
		asm volatile("mrc p15, 0, %0, c9, c13, 1" : "=r" (val));
		printk(KERN_INFO "CNT[%d] evtsel=0x%08x\n",
			ARMV7_IDX_TO_COUNTER(cnt), val);
	}
}
#endif

static void armv7pmu_enable_event(struct hw_perf_event *hwc, int idx)
{
	unsigned long flags;
	struct pmu_hw_events *events = cpu_pmu->get_hw_events();

	/*
	 * Enable counter and interrupt, and set the counter to count
	 * the event that we're interested in.
	 */
	raw_spin_lock_irqsave(&events->pmu_lock, flags);

	/*
	 * Disable counter
	 */
	armv7_pmnc_disable_counter(idx);

	/*
	 * Set event (if destined for PMNx counters)
	 * We only need to set the event for the cycle counter if we
	 * have the ability to perform event filtering.
	 */
	if (armv7pmu.set_event_filter || idx != ARMV7_IDX_CYCLE_COUNTER)
		armv7_pmnc_write_evtsel(idx, hwc->config_base);

	/*
	 * Enable interrupt for this counter
	 */
	armv7_pmnc_enable_intens(idx);

	/*
	 * Enable counter
	 */
	armv7_pmnc_enable_counter(idx);

	raw_spin_unlock_irqrestore(&events->pmu_lock, flags);
}

static void armv7pmu_disable_event(struct hw_perf_event *hwc, int idx)
{
	unsigned long flags;
	struct pmu_hw_events *events = cpu_pmu->get_hw_events();

	/*
	 * Disable counter and interrupt
	 */
	raw_spin_lock_irqsave(&events->pmu_lock, flags);

	/*
	 * Disable counter
	 */
	armv7_pmnc_disable_counter(idx);

	/*
	 * Disable interrupt for this counter
	 */
	armv7_pmnc_disable_intens(idx);

	raw_spin_unlock_irqrestore(&events->pmu_lock, flags);
}

static irqreturn_t armv7pmu_handle_irq(int irq_num, void *dev)
{
	u32 pmnc;
	struct perf_sample_data data;
	struct pmu_hw_events *cpuc;
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
	for (idx = 0; idx < cpu_pmu->num_events; ++idx) {
		struct perf_event *event = cpuc->events[idx];
		struct hw_perf_event *hwc;

		/*
		 * We have a single interrupt for all counters. Check that
		 * each counter has overflowed before we process it.
		 */
		if (!armv7_pmnc_counter_has_overflowed(pmnc, idx))
			continue;

		hwc = &event->hw;
		armpmu_event_update(event, hwc, idx, 1);
		data.period = event->hw.last_period;
		if (!armpmu_event_set_period(event, hwc, idx))
			continue;

		if (perf_event_overflow(event, &data, regs))
			cpu_pmu->disable(hwc, idx);
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
	struct pmu_hw_events *events = cpu_pmu->get_hw_events();

	raw_spin_lock_irqsave(&events->pmu_lock, flags);
	/* Enable all counters */
	armv7_pmnc_write(armv7_pmnc_read() | ARMV7_PMNC_E);
	raw_spin_unlock_irqrestore(&events->pmu_lock, flags);
}

static void armv7pmu_stop(void)
{
	unsigned long flags;
	struct pmu_hw_events *events = cpu_pmu->get_hw_events();

	raw_spin_lock_irqsave(&events->pmu_lock, flags);
	/* Disable all counters */
	armv7_pmnc_write(armv7_pmnc_read() & ~ARMV7_PMNC_E);
	raw_spin_unlock_irqrestore(&events->pmu_lock, flags);
}

static int armv7pmu_get_event_idx(struct pmu_hw_events *cpuc,
				  struct hw_perf_event *event)
{
	int idx;
	unsigned long evtype = event->config_base & ARMV7_EVTYPE_EVENT;

	/* Always place a cycle counter into the cycle counter. */
	if (evtype == ARMV7_PERFCTR_CPU_CYCLES) {
		if (test_and_set_bit(ARMV7_IDX_CYCLE_COUNTER, cpuc->used_mask))
			return -EAGAIN;

		return ARMV7_IDX_CYCLE_COUNTER;
	}

	/*
	 * For anything other than a cycle counter, try and use
	 * the events counters
	 */
	for (idx = ARMV7_IDX_COUNTER0; idx < cpu_pmu->num_events; ++idx) {
		if (!test_and_set_bit(idx, cpuc->used_mask))
			return idx;
	}

	/* The counters are all in use. */
	return -EAGAIN;
}

/*
 * Add an event filter to a given event. This will only work for PMUv2 PMUs.
 */
static int armv7pmu_set_event_filter(struct hw_perf_event *event,
				     struct perf_event_attr *attr)
{
	unsigned long config_base = 0;

	if (attr->exclude_idle)
		return -EPERM;
	if (attr->exclude_user)
		config_base |= ARMV7_EXCLUDE_USER;
	if (attr->exclude_kernel)
		config_base |= ARMV7_EXCLUDE_PL1;
	if (!attr->exclude_hv)
		config_base |= ARMV7_INCLUDE_HYP;

	/*
	 * Install the filter into config_base as this is used to
	 * construct the event type.
	 */
	event->config_base = config_base;

	return 0;
}

static void armv7pmu_reset(void *info)
{
	u32 idx, nb_cnt = cpu_pmu->num_events;

	/* The counter and interrupt enable registers are unknown at reset. */
	for (idx = ARMV7_IDX_CYCLE_COUNTER; idx < nb_cnt; ++idx)
		armv7pmu_disable_event(NULL, idx);

	/* Initialize & Reset PMNC: C and P bits */
	armv7_pmnc_write(ARMV7_PMNC_P | ARMV7_PMNC_C);
}

static int armv7_a8_map_event(struct perf_event *event)
{
	return map_cpu_event(event, &armv7_a8_perf_map,
				&armv7_a8_perf_cache_map, 0xFF);
}

static int armv7_a9_map_event(struct perf_event *event)
{
	return map_cpu_event(event, &armv7_a9_perf_map,
				&armv7_a9_perf_cache_map, 0xFF);
}

static int armv7_a5_map_event(struct perf_event *event)
{
	return map_cpu_event(event, &armv7_a5_perf_map,
				&armv7_a5_perf_cache_map, 0xFF);
}

static int armv7_a15_map_event(struct perf_event *event)
{
	return map_cpu_event(event, &armv7_a15_perf_map,
				&armv7_a15_perf_cache_map, 0xFF);
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
	.reset			= armv7pmu_reset,
	.max_period		= (1LLU << 32) - 1,
};

static u32 __init armv7_read_num_pmnc_events(void)
{
	u32 nb_cnt;

	/* Read the nb of CNTx counters supported from PMNC */
	nb_cnt = (armv7_pmnc_read() >> ARMV7_PMNC_N_SHIFT) & ARMV7_PMNC_N_MASK;

	/* Add the CPU cycles counter and return */
	return nb_cnt + 1;
}

static struct arm_pmu *__init armv7_a8_pmu_init(void)
{
	armv7pmu.id		= ARM_PERF_PMU_ID_CA8;
	armv7pmu.name		= "ARMv7 Cortex-A8";
	armv7pmu.map_event	= armv7_a8_map_event;
	armv7pmu.num_events	= armv7_read_num_pmnc_events();
	return &armv7pmu;
}

static struct arm_pmu *__init armv7_a9_pmu_init(void)
{
	armv7pmu.id		= ARM_PERF_PMU_ID_CA9;
	armv7pmu.name		= "ARMv7 Cortex-A9";
	armv7pmu.map_event	= armv7_a9_map_event;
	armv7pmu.num_events	= armv7_read_num_pmnc_events();
	return &armv7pmu;
}

static struct arm_pmu *__init armv7_a5_pmu_init(void)
{
	armv7pmu.id		= ARM_PERF_PMU_ID_CA5;
	armv7pmu.name		= "ARMv7 Cortex-A5";
	armv7pmu.map_event	= armv7_a5_map_event;
	armv7pmu.num_events	= armv7_read_num_pmnc_events();
	return &armv7pmu;
}

static struct arm_pmu *__init armv7_a15_pmu_init(void)
{
	armv7pmu.id		= ARM_PERF_PMU_ID_CA15;
	armv7pmu.name		= "ARMv7 Cortex-A15";
	armv7pmu.map_event	= armv7_a15_map_event;
	armv7pmu.num_events	= armv7_read_num_pmnc_events();
	armv7pmu.set_event_filter = armv7pmu_set_event_filter;
	return &armv7pmu;
}
#else
static struct arm_pmu *__init armv7_a8_pmu_init(void)
{
	return NULL;
}

static struct arm_pmu *__init armv7_a9_pmu_init(void)
{
	return NULL;
}

static struct arm_pmu *__init armv7_a5_pmu_init(void)
{
	return NULL;
}

static struct arm_pmu *__init armv7_a15_pmu_init(void)
{
	return NULL;
}
#endif	/* CONFIG_CPU_V7 */
