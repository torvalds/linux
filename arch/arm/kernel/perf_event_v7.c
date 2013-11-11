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

/*
 * Common ARMv7 event types
 *
 * Note: An implementation may not be able to count all of these events
 * but the encodings are considered to be `reserved' in the case that
 * they are not available.
 */
enum armv7_perf_types {
	ARMV7_PERFCTR_PMNC_SW_INCR			= 0x00,
	ARMV7_PERFCTR_L1_ICACHE_REFILL			= 0x01,
	ARMV7_PERFCTR_ITLB_REFILL			= 0x02,
	ARMV7_PERFCTR_L1_DCACHE_REFILL			= 0x03,
	ARMV7_PERFCTR_L1_DCACHE_ACCESS			= 0x04,
	ARMV7_PERFCTR_DTLB_REFILL			= 0x05,
	ARMV7_PERFCTR_MEM_READ				= 0x06,
	ARMV7_PERFCTR_MEM_WRITE				= 0x07,
	ARMV7_PERFCTR_INSTR_EXECUTED			= 0x08,
	ARMV7_PERFCTR_EXC_TAKEN				= 0x09,
	ARMV7_PERFCTR_EXC_EXECUTED			= 0x0A,
	ARMV7_PERFCTR_CID_WRITE				= 0x0B,

	/*
	 * ARMV7_PERFCTR_PC_WRITE is equivalent to HW_BRANCH_INSTRUCTIONS.
	 * It counts:
	 *  - all (taken) branch instructions,
	 *  - instructions that explicitly write the PC,
	 *  - exception generating instructions.
	 */
	ARMV7_PERFCTR_PC_WRITE				= 0x0C,
	ARMV7_PERFCTR_PC_IMM_BRANCH			= 0x0D,
	ARMV7_PERFCTR_PC_PROC_RETURN			= 0x0E,
	ARMV7_PERFCTR_MEM_UNALIGNED_ACCESS		= 0x0F,
	ARMV7_PERFCTR_PC_BRANCH_MIS_PRED		= 0x10,
	ARMV7_PERFCTR_CLOCK_CYCLES			= 0x11,
	ARMV7_PERFCTR_PC_BRANCH_PRED			= 0x12,

	/* These events are defined by the PMUv2 supplement (ARM DDI 0457A). */
	ARMV7_PERFCTR_MEM_ACCESS			= 0x13,
	ARMV7_PERFCTR_L1_ICACHE_ACCESS			= 0x14,
	ARMV7_PERFCTR_L1_DCACHE_WB			= 0x15,
	ARMV7_PERFCTR_L2_CACHE_ACCESS			= 0x16,
	ARMV7_PERFCTR_L2_CACHE_REFILL			= 0x17,
	ARMV7_PERFCTR_L2_CACHE_WB			= 0x18,
	ARMV7_PERFCTR_BUS_ACCESS			= 0x19,
	ARMV7_PERFCTR_MEM_ERROR				= 0x1A,
	ARMV7_PERFCTR_INSTR_SPEC			= 0x1B,
	ARMV7_PERFCTR_TTBR_WRITE			= 0x1C,
	ARMV7_PERFCTR_BUS_CYCLES			= 0x1D,

	ARMV7_PERFCTR_CPU_CYCLES			= 0xFF
};

/* ARMv7 Cortex-A8 specific event types */
enum armv7_a8_perf_types {
	ARMV7_A8_PERFCTR_L2_CACHE_ACCESS		= 0x43,
	ARMV7_A8_PERFCTR_L2_CACHE_REFILL		= 0x44,
	ARMV7_A8_PERFCTR_L1_ICACHE_ACCESS		= 0x50,
	ARMV7_A8_PERFCTR_STALL_ISIDE			= 0x56,
};

/* ARMv7 Cortex-A9 specific event types */
enum armv7_a9_perf_types {
	ARMV7_A9_PERFCTR_INSTR_CORE_RENAME		= 0x68,
	ARMV7_A9_PERFCTR_STALL_ICACHE			= 0x60,
	ARMV7_A9_PERFCTR_STALL_DISPATCH			= 0x66,
};

/* ARMv7 Cortex-A5 specific event types */
enum armv7_a5_perf_types {
	ARMV7_A5_PERFCTR_PREFETCH_LINEFILL		= 0xc2,
	ARMV7_A5_PERFCTR_PREFETCH_LINEFILL_DROP		= 0xc3,
};

/* ARMv7 Cortex-A15 specific event types */
enum armv7_a15_perf_types {
	ARMV7_A15_PERFCTR_L1_DCACHE_ACCESS_READ		= 0x40,
	ARMV7_A15_PERFCTR_L1_DCACHE_ACCESS_WRITE	= 0x41,
	ARMV7_A15_PERFCTR_L1_DCACHE_REFILL_READ		= 0x42,
	ARMV7_A15_PERFCTR_L1_DCACHE_REFILL_WRITE	= 0x43,

	ARMV7_A15_PERFCTR_DTLB_REFILL_L1_READ		= 0x4C,
	ARMV7_A15_PERFCTR_DTLB_REFILL_L1_WRITE		= 0x4D,

	ARMV7_A15_PERFCTR_L2_CACHE_ACCESS_READ		= 0x50,
	ARMV7_A15_PERFCTR_L2_CACHE_ACCESS_WRITE		= 0x51,
	ARMV7_A15_PERFCTR_L2_CACHE_REFILL_READ		= 0x52,
	ARMV7_A15_PERFCTR_L2_CACHE_REFILL_WRITE		= 0x53,

	ARMV7_A15_PERFCTR_PC_WRITE_SPEC			= 0x76,
};

/*
 * Cortex-A8 HW events mapping
 *
 * The hardware events that we support. We do support cache operations but
 * we have harvard caches and no way to combine instruction and data
 * accesses/misses in hardware.
 */
static const unsigned armv7_a8_perf_map[PERF_COUNT_HW_MAX] = {
	[PERF_COUNT_HW_CPU_CYCLES]		= ARMV7_PERFCTR_CPU_CYCLES,
	[PERF_COUNT_HW_INSTRUCTIONS]		= ARMV7_PERFCTR_INSTR_EXECUTED,
	[PERF_COUNT_HW_CACHE_REFERENCES]	= ARMV7_PERFCTR_L1_DCACHE_ACCESS,
	[PERF_COUNT_HW_CACHE_MISSES]		= ARMV7_PERFCTR_L1_DCACHE_REFILL,
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS]	= ARMV7_PERFCTR_PC_WRITE,
	[PERF_COUNT_HW_BRANCH_MISSES]		= ARMV7_PERFCTR_PC_BRANCH_MIS_PRED,
	[PERF_COUNT_HW_BUS_CYCLES]		= HW_OP_UNSUPPORTED,
	[PERF_COUNT_HW_STALLED_CYCLES_FRONTEND]	= ARMV7_A8_PERFCTR_STALL_ISIDE,
	[PERF_COUNT_HW_STALLED_CYCLES_BACKEND]	= HW_OP_UNSUPPORTED,
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
			[C(RESULT_ACCESS)]	= ARMV7_PERFCTR_L1_DCACHE_ACCESS,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_L1_DCACHE_REFILL,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= ARMV7_PERFCTR_L1_DCACHE_ACCESS,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_L1_DCACHE_REFILL,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
	[C(L1I)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= ARMV7_A8_PERFCTR_L1_ICACHE_ACCESS,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_L1_ICACHE_REFILL,
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
	[C(LL)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= ARMV7_A8_PERFCTR_L2_CACHE_ACCESS,
			[C(RESULT_MISS)]	= ARMV7_A8_PERFCTR_L2_CACHE_REFILL,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= ARMV7_A8_PERFCTR_L2_CACHE_ACCESS,
			[C(RESULT_MISS)]	= ARMV7_A8_PERFCTR_L2_CACHE_REFILL,
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
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_ITLB_REFILL,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_ITLB_REFILL,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
	[C(BPU)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= ARMV7_PERFCTR_PC_BRANCH_PRED,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_PC_BRANCH_MIS_PRED,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= ARMV7_PERFCTR_PC_BRANCH_PRED,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_PC_BRANCH_MIS_PRED,
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
	[PERF_COUNT_HW_CPU_CYCLES]		= ARMV7_PERFCTR_CPU_CYCLES,
	[PERF_COUNT_HW_INSTRUCTIONS]		= ARMV7_A9_PERFCTR_INSTR_CORE_RENAME,
	[PERF_COUNT_HW_CACHE_REFERENCES]	= ARMV7_PERFCTR_L1_DCACHE_ACCESS,
	[PERF_COUNT_HW_CACHE_MISSES]		= ARMV7_PERFCTR_L1_DCACHE_REFILL,
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS]	= ARMV7_PERFCTR_PC_WRITE,
	[PERF_COUNT_HW_BRANCH_MISSES]		= ARMV7_PERFCTR_PC_BRANCH_MIS_PRED,
	[PERF_COUNT_HW_BUS_CYCLES]		= HW_OP_UNSUPPORTED,
	[PERF_COUNT_HW_STALLED_CYCLES_FRONTEND]	= ARMV7_A9_PERFCTR_STALL_ICACHE,
	[PERF_COUNT_HW_STALLED_CYCLES_BACKEND]	= ARMV7_A9_PERFCTR_STALL_DISPATCH,
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
			[C(RESULT_ACCESS)]	= ARMV7_PERFCTR_L1_DCACHE_ACCESS,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_L1_DCACHE_REFILL,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= ARMV7_PERFCTR_L1_DCACHE_ACCESS,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_L1_DCACHE_REFILL,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
	[C(L1I)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_L1_ICACHE_REFILL,
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
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_ITLB_REFILL,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_ITLB_REFILL,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
	[C(BPU)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= ARMV7_PERFCTR_PC_BRANCH_PRED,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_PC_BRANCH_MIS_PRED,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= ARMV7_PERFCTR_PC_BRANCH_PRED,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_PC_BRANCH_MIS_PRED,
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
	[PERF_COUNT_HW_CPU_CYCLES]		= ARMV7_PERFCTR_CPU_CYCLES,
	[PERF_COUNT_HW_INSTRUCTIONS]		= ARMV7_PERFCTR_INSTR_EXECUTED,
	[PERF_COUNT_HW_CACHE_REFERENCES]	= ARMV7_PERFCTR_L1_DCACHE_ACCESS,
	[PERF_COUNT_HW_CACHE_MISSES]		= ARMV7_PERFCTR_L1_DCACHE_REFILL,
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS]	= ARMV7_PERFCTR_PC_WRITE,
	[PERF_COUNT_HW_BRANCH_MISSES]		= ARMV7_PERFCTR_PC_BRANCH_MIS_PRED,
	[PERF_COUNT_HW_BUS_CYCLES]		= HW_OP_UNSUPPORTED,
	[PERF_COUNT_HW_STALLED_CYCLES_FRONTEND]	= HW_OP_UNSUPPORTED,
	[PERF_COUNT_HW_STALLED_CYCLES_BACKEND]	= HW_OP_UNSUPPORTED,
};

static const unsigned armv7_a5_perf_cache_map[PERF_COUNT_HW_CACHE_MAX]
					[PERF_COUNT_HW_CACHE_OP_MAX]
					[PERF_COUNT_HW_CACHE_RESULT_MAX] = {
	[C(L1D)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= ARMV7_PERFCTR_L1_DCACHE_ACCESS,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_L1_DCACHE_REFILL,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= ARMV7_PERFCTR_L1_DCACHE_ACCESS,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_L1_DCACHE_REFILL,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= ARMV7_A5_PERFCTR_PREFETCH_LINEFILL,
			[C(RESULT_MISS)]	= ARMV7_A5_PERFCTR_PREFETCH_LINEFILL_DROP,
		},
	},
	[C(L1I)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= ARMV7_PERFCTR_L1_ICACHE_ACCESS,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_L1_ICACHE_REFILL,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
		/*
		 * The prefetch counters don't differentiate between the I
		 * side and the D side.
		 */
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= ARMV7_A5_PERFCTR_PREFETCH_LINEFILL,
			[C(RESULT_MISS)]	= ARMV7_A5_PERFCTR_PREFETCH_LINEFILL_DROP,
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
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_ITLB_REFILL,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_ITLB_REFILL,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
	[C(BPU)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= ARMV7_PERFCTR_PC_BRANCH_PRED,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_PC_BRANCH_MIS_PRED,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= ARMV7_PERFCTR_PC_BRANCH_PRED,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_PC_BRANCH_MIS_PRED,
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
 * Cortex-A15 HW events mapping
 */
static const unsigned armv7_a15_perf_map[PERF_COUNT_HW_MAX] = {
	[PERF_COUNT_HW_CPU_CYCLES]		= ARMV7_PERFCTR_CPU_CYCLES,
	[PERF_COUNT_HW_INSTRUCTIONS]		= ARMV7_PERFCTR_INSTR_EXECUTED,
	[PERF_COUNT_HW_CACHE_REFERENCES]	= ARMV7_PERFCTR_L1_DCACHE_ACCESS,
	[PERF_COUNT_HW_CACHE_MISSES]		= ARMV7_PERFCTR_L1_DCACHE_REFILL,
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS]	= ARMV7_A15_PERFCTR_PC_WRITE_SPEC,
	[PERF_COUNT_HW_BRANCH_MISSES]		= ARMV7_PERFCTR_PC_BRANCH_MIS_PRED,
	[PERF_COUNT_HW_BUS_CYCLES]		= ARMV7_PERFCTR_BUS_CYCLES,
	[PERF_COUNT_HW_STALLED_CYCLES_FRONTEND]	= HW_OP_UNSUPPORTED,
	[PERF_COUNT_HW_STALLED_CYCLES_BACKEND]	= HW_OP_UNSUPPORTED,
};

static const unsigned armv7_a15_perf_cache_map[PERF_COUNT_HW_CACHE_MAX]
					[PERF_COUNT_HW_CACHE_OP_MAX]
					[PERF_COUNT_HW_CACHE_RESULT_MAX] = {
	[C(L1D)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= ARMV7_A15_PERFCTR_L1_DCACHE_ACCESS_READ,
			[C(RESULT_MISS)]	= ARMV7_A15_PERFCTR_L1_DCACHE_REFILL_READ,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= ARMV7_A15_PERFCTR_L1_DCACHE_ACCESS_WRITE,
			[C(RESULT_MISS)]	= ARMV7_A15_PERFCTR_L1_DCACHE_REFILL_WRITE,
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
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_L1_ICACHE_REFILL,
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
	[C(LL)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= ARMV7_A15_PERFCTR_L2_CACHE_ACCESS_READ,
			[C(RESULT_MISS)]	= ARMV7_A15_PERFCTR_L2_CACHE_REFILL_READ,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= ARMV7_A15_PERFCTR_L2_CACHE_ACCESS_WRITE,
			[C(RESULT_MISS)]	= ARMV7_A15_PERFCTR_L2_CACHE_REFILL_WRITE,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
	[C(DTLB)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= ARMV7_A15_PERFCTR_DTLB_REFILL_L1_READ,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= ARMV7_A15_PERFCTR_DTLB_REFILL_L1_WRITE,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
	[C(ITLB)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_ITLB_REFILL,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_ITLB_REFILL,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
	[C(BPU)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= ARMV7_PERFCTR_PC_BRANCH_PRED,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_PC_BRANCH_MIS_PRED,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= ARMV7_PERFCTR_PC_BRANCH_PRED,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_PC_BRANCH_MIS_PRED,
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
 * Cortex-A7 HW events mapping
 */
static const unsigned armv7_a7_perf_map[PERF_COUNT_HW_MAX] = {
	[PERF_COUNT_HW_CPU_CYCLES]		= ARMV7_PERFCTR_CPU_CYCLES,
	[PERF_COUNT_HW_INSTRUCTIONS]		= ARMV7_PERFCTR_INSTR_EXECUTED,
	[PERF_COUNT_HW_CACHE_REFERENCES]	= ARMV7_PERFCTR_L1_DCACHE_ACCESS,
	[PERF_COUNT_HW_CACHE_MISSES]		= ARMV7_PERFCTR_L1_DCACHE_REFILL,
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS]	= ARMV7_PERFCTR_PC_WRITE,
	[PERF_COUNT_HW_BRANCH_MISSES]		= ARMV7_PERFCTR_PC_BRANCH_MIS_PRED,
	[PERF_COUNT_HW_BUS_CYCLES]		= ARMV7_PERFCTR_BUS_CYCLES,
	[PERF_COUNT_HW_STALLED_CYCLES_FRONTEND]	= HW_OP_UNSUPPORTED,
	[PERF_COUNT_HW_STALLED_CYCLES_BACKEND]	= HW_OP_UNSUPPORTED,
};

static const unsigned armv7_a7_perf_cache_map[PERF_COUNT_HW_CACHE_MAX]
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
			[C(RESULT_ACCESS)]	= ARMV7_PERFCTR_L1_DCACHE_ACCESS,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_L1_DCACHE_REFILL,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= ARMV7_PERFCTR_L1_DCACHE_ACCESS,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_L1_DCACHE_REFILL,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
	[C(L1I)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= ARMV7_PERFCTR_L1_ICACHE_ACCESS,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_L1_ICACHE_REFILL,
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
	[C(LL)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= ARMV7_PERFCTR_L2_CACHE_ACCESS,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_L2_CACHE_REFILL,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= ARMV7_PERFCTR_L2_CACHE_ACCESS,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_L2_CACHE_REFILL,
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
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_ITLB_REFILL,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_ITLB_REFILL,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
	[C(BPU)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= ARMV7_PERFCTR_PC_BRANCH_PRED,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_PC_BRANCH_MIS_PRED,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= ARMV7_PERFCTR_PC_BRANCH_PRED,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_PC_BRANCH_MIS_PRED,
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
 * Perf Events' indices
 */
#define	ARMV7_IDX_CYCLE_COUNTER	0
#define	ARMV7_IDX_COUNTER0	1
#define	ARMV7_IDX_COUNTER_LAST(cpu_pmu) \
	(ARMV7_IDX_CYCLE_COUNTER + cpu_pmu->num_events - 1)

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
#define	ARMV7_EVTYPE_MASK	0xc80000ff	/* Mask for writable bits */
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

static inline int armv7_pmnc_counter_valid(struct arm_pmu *cpu_pmu, int idx)
{
	return idx >= ARMV7_IDX_CYCLE_COUNTER &&
		idx <= ARMV7_IDX_COUNTER_LAST(cpu_pmu);
}

static inline int armv7_pmnc_counter_has_overflowed(u32 pmnc, int idx)
{
	return pmnc & BIT(ARMV7_IDX_TO_COUNTER(idx));
}

static inline int armv7_pmnc_select_counter(int idx)
{
	u32 counter = ARMV7_IDX_TO_COUNTER(idx);
	asm volatile("mcr p15, 0, %0, c9, c12, 5" : : "r" (counter));
	isb();

	return idx;
}

static inline u32 armv7pmu_read_counter(struct perf_event *event)
{
	struct arm_pmu *cpu_pmu = to_arm_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	int idx = hwc->idx;
	u32 value = 0;

	if (!armv7_pmnc_counter_valid(cpu_pmu, idx))
		pr_err("CPU%u reading wrong counter %d\n",
			smp_processor_id(), idx);
	else if (idx == ARMV7_IDX_CYCLE_COUNTER)
		asm volatile("mrc p15, 0, %0, c9, c13, 0" : "=r" (value));
	else if (armv7_pmnc_select_counter(idx) == idx)
		asm volatile("mrc p15, 0, %0, c9, c13, 2" : "=r" (value));

	return value;
}

static inline void armv7pmu_write_counter(struct perf_event *event, u32 value)
{
	struct arm_pmu *cpu_pmu = to_arm_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	int idx = hwc->idx;

	if (!armv7_pmnc_counter_valid(cpu_pmu, idx))
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
	u32 counter = ARMV7_IDX_TO_COUNTER(idx);
	asm volatile("mcr p15, 0, %0, c9, c12, 1" : : "r" (BIT(counter)));
	return idx;
}

static inline int armv7_pmnc_disable_counter(int idx)
{
	u32 counter = ARMV7_IDX_TO_COUNTER(idx);
	asm volatile("mcr p15, 0, %0, c9, c12, 2" : : "r" (BIT(counter)));
	return idx;
}

static inline int armv7_pmnc_enable_intens(int idx)
{
	u32 counter = ARMV7_IDX_TO_COUNTER(idx);
	asm volatile("mcr p15, 0, %0, c9, c14, 1" : : "r" (BIT(counter)));
	return idx;
}

static inline int armv7_pmnc_disable_intens(int idx)
{
	u32 counter = ARMV7_IDX_TO_COUNTER(idx);
	asm volatile("mcr p15, 0, %0, c9, c14, 2" : : "r" (BIT(counter)));
	isb();
	/* Clear the overflow flag in case an interrupt is pending. */
	asm volatile("mcr p15, 0, %0, c9, c12, 3" : : "r" (BIT(counter)));
	isb();

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
static void armv7_pmnc_dump_regs(struct arm_pmu *cpu_pmu)
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

	for (cnt = ARMV7_IDX_COUNTER0;
			cnt <= ARMV7_IDX_COUNTER_LAST(cpu_pmu); cnt++) {
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

static void armv7pmu_save_regs(struct arm_pmu *cpu_pmu,
					struct cpupmu_regs *regs)
{
	unsigned int cnt;
	asm volatile("mrc p15, 0, %0, c9, c12, 0" : "=r" (regs->pmc));
	if (!(regs->pmc & ARMV7_PMNC_E))
		return;

	asm volatile("mrc p15, 0, %0, c9, c12, 1" : "=r" (regs->pmcntenset));
	asm volatile("mrc p15, 0, %0, c9, c14, 0" : "=r" (regs->pmuseren));
	asm volatile("mrc p15, 0, %0, c9, c14, 1" : "=r" (regs->pmintenset));
	asm volatile("mrc p15, 0, %0, c9, c13, 0" : "=r" (regs->pmxevtcnt[0]));
	for (cnt = ARMV7_IDX_COUNTER0;
			cnt <= ARMV7_IDX_COUNTER_LAST(cpu_pmu); cnt++) {
		armv7_pmnc_select_counter(cnt);
		asm volatile("mrc p15, 0, %0, c9, c13, 1"
					: "=r"(regs->pmxevttype[cnt]));
		asm volatile("mrc p15, 0, %0, c9, c13, 2"
					: "=r"(regs->pmxevtcnt[cnt]));
	}
	return;
}

static void armv7pmu_restore_regs(struct arm_pmu *cpu_pmu,
					struct cpupmu_regs *regs)
{
	unsigned int cnt;
	if (!(regs->pmc & ARMV7_PMNC_E))
		return;

	asm volatile("mcr p15, 0, %0, c9, c12, 1" : : "r" (regs->pmcntenset));
	asm volatile("mcr p15, 0, %0, c9, c14, 0" : : "r" (regs->pmuseren));
	asm volatile("mcr p15, 0, %0, c9, c14, 1" : : "r" (regs->pmintenset));
	asm volatile("mcr p15, 0, %0, c9, c13, 0" : : "r" (regs->pmxevtcnt[0]));
	for (cnt = ARMV7_IDX_COUNTER0;
			cnt <= ARMV7_IDX_COUNTER_LAST(cpu_pmu); cnt++) {
		armv7_pmnc_select_counter(cnt);
		asm volatile("mcr p15, 0, %0, c9, c13, 1"
					: : "r"(regs->pmxevttype[cnt]));
		asm volatile("mcr p15, 0, %0, c9, c13, 2"
					: : "r"(regs->pmxevtcnt[cnt]));
	}
	asm volatile("mcr p15, 0, %0, c9, c12, 0" : : "r" (regs->pmc));
}

static void armv7pmu_enable_event(struct perf_event *event)
{
	unsigned long flags;
	struct hw_perf_event *hwc = &event->hw;
	struct arm_pmu *cpu_pmu = to_arm_pmu(event->pmu);
	struct pmu_hw_events *events = cpu_pmu->get_hw_events();
	int idx = hwc->idx;

	if (!armv7_pmnc_counter_valid(cpu_pmu, idx)) {
		pr_err("CPU%u enabling wrong PMNC counter IRQ enable %d\n",
			smp_processor_id(), idx);
		return;
	}

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
	if (cpu_pmu->set_event_filter || idx != ARMV7_IDX_CYCLE_COUNTER)
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

static void armv7pmu_disable_event(struct perf_event *event)
{
	unsigned long flags;
	struct hw_perf_event *hwc = &event->hw;
	struct arm_pmu *cpu_pmu = to_arm_pmu(event->pmu);
	struct pmu_hw_events *events = cpu_pmu->get_hw_events();
	int idx = hwc->idx;

	if (!armv7_pmnc_counter_valid(cpu_pmu, idx)) {
		pr_err("CPU%u disabling wrong PMNC counter IRQ enable %d\n",
			smp_processor_id(), idx);
		return;
	}

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
	struct arm_pmu *cpu_pmu = (struct arm_pmu *)dev;
	struct pmu_hw_events *cpuc = cpu_pmu->get_hw_events();
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

	for (idx = 0; idx < cpu_pmu->num_events; ++idx) {
		struct perf_event *event = cpuc->events[idx];
		struct hw_perf_event *hwc;

		/* Ignore if we don't have an event. */
		if (!event)
			continue;

		/*
		 * We have a single interrupt for all counters. Check that
		 * each counter has overflowed before we process it.
		 */
		if (!armv7_pmnc_counter_has_overflowed(pmnc, idx))
			continue;

		hwc = &event->hw;
		armpmu_event_update(event);
		perf_sample_data_init(&data, 0, hwc->last_period);
		if (!armpmu_event_set_period(event))
			continue;

		if (perf_event_overflow(event, &data, regs))
			cpu_pmu->disable(event);
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

static void armv7pmu_start(struct arm_pmu *cpu_pmu)
{
	unsigned long flags;
	struct pmu_hw_events *events = cpu_pmu->get_hw_events();

	raw_spin_lock_irqsave(&events->pmu_lock, flags);
	/* Enable all counters */
	armv7_pmnc_write(armv7_pmnc_read() | ARMV7_PMNC_E);
	raw_spin_unlock_irqrestore(&events->pmu_lock, flags);
}

static void armv7pmu_stop(struct arm_pmu *cpu_pmu)
{
	unsigned long flags;
	struct pmu_hw_events *events = cpu_pmu->get_hw_events();

	raw_spin_lock_irqsave(&events->pmu_lock, flags);
	/* Disable all counters */
	armv7_pmnc_write(armv7_pmnc_read() & ~ARMV7_PMNC_E);
	raw_spin_unlock_irqrestore(&events->pmu_lock, flags);
}

static int armv7pmu_get_event_idx(struct pmu_hw_events *cpuc,
				  struct perf_event *event)
{
	int idx;
	struct arm_pmu *cpu_pmu = to_arm_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	unsigned long evtype = hwc->config_base & ARMV7_EVTYPE_EVENT;

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
	struct arm_pmu *cpu_pmu = (struct arm_pmu *)info;
	u32 idx, nb_cnt = cpu_pmu->num_events;

	/* The counter and interrupt enable registers are unknown at reset. */
	for (idx = ARMV7_IDX_CYCLE_COUNTER; idx < nb_cnt; ++idx) {
		armv7_pmnc_disable_counter(idx);
		armv7_pmnc_disable_intens(idx);
	}

	/* Initialize & Reset PMNC: C and P bits */
	armv7_pmnc_write(ARMV7_PMNC_P | ARMV7_PMNC_C);
}

static int armv7_a8_map_event(struct perf_event *event)
{
	return armpmu_map_event(event, &armv7_a8_perf_map,
				&armv7_a8_perf_cache_map, 0xFF);
}

static int armv7_a9_map_event(struct perf_event *event)
{
	return armpmu_map_event(event, &armv7_a9_perf_map,
				&armv7_a9_perf_cache_map, 0xFF);
}

static int armv7_a5_map_event(struct perf_event *event)
{
	return armpmu_map_event(event, &armv7_a5_perf_map,
				&armv7_a5_perf_cache_map, 0xFF);
}

static int armv7_a15_map_event(struct perf_event *event)
{
	return armpmu_map_event(event, &armv7_a15_perf_map,
				&armv7_a15_perf_cache_map, 0xFF);
}

static int armv7_a7_map_event(struct perf_event *event)
{
	return armpmu_map_event(event, &armv7_a7_perf_map,
				&armv7_a7_perf_cache_map, 0xFF);
}

static void armv7pmu_init(struct arm_pmu *cpu_pmu)
{
	cpu_pmu->handle_irq	= armv7pmu_handle_irq;
	cpu_pmu->enable		= armv7pmu_enable_event;
	cpu_pmu->disable	= armv7pmu_disable_event;
	cpu_pmu->read_counter	= armv7pmu_read_counter;
	cpu_pmu->write_counter	= armv7pmu_write_counter;
	cpu_pmu->get_event_idx	= armv7pmu_get_event_idx;
	cpu_pmu->start		= armv7pmu_start;
	cpu_pmu->stop		= armv7pmu_stop;
	cpu_pmu->reset		= armv7pmu_reset;
	cpu_pmu->save_regs	= armv7pmu_save_regs;
	cpu_pmu->restore_regs	= armv7pmu_restore_regs;
	cpu_pmu->max_period	= (1LLU << 32) - 1;
};

static u32 armv7_read_num_pmnc_events(void)
{
	u32 nb_cnt;

	/* Read the nb of CNTx counters supported from PMNC */
	nb_cnt = (armv7_pmnc_read() >> ARMV7_PMNC_N_SHIFT) & ARMV7_PMNC_N_MASK;

	/* Add the CPU cycles counter and return */
	return nb_cnt + 1;
}

static int armv7_a8_pmu_init(struct arm_pmu *cpu_pmu)
{
	armv7pmu_init(cpu_pmu);
	cpu_pmu->name		= "ARMv7_Cortex_A8";
	cpu_pmu->map_event	= armv7_a8_map_event;
	cpu_pmu->num_events	= armv7_read_num_pmnc_events();
	return 0;
}

static int armv7_a9_pmu_init(struct arm_pmu *cpu_pmu)
{
	armv7pmu_init(cpu_pmu);
	cpu_pmu->name		= "ARMv7_Cortex_A9";
	cpu_pmu->map_event	= armv7_a9_map_event;
	cpu_pmu->num_events	= armv7_read_num_pmnc_events();
	return 0;
}

static int armv7_a5_pmu_init(struct arm_pmu *cpu_pmu)
{
	armv7pmu_init(cpu_pmu);
	cpu_pmu->name		= "ARMv7_Cortex_A5";
	cpu_pmu->map_event	= armv7_a5_map_event;
	cpu_pmu->num_events	= armv7_read_num_pmnc_events();
	return 0;
}

static int armv7_a15_pmu_init(struct arm_pmu *cpu_pmu)
{
	armv7pmu_init(cpu_pmu);
	cpu_pmu->name		= "ARMv7_Cortex_A15";
	cpu_pmu->map_event	= armv7_a15_map_event;
	cpu_pmu->num_events	= armv7_read_num_pmnc_events();
	cpu_pmu->set_event_filter = armv7pmu_set_event_filter;
	return 0;
}

static int armv7_a7_pmu_init(struct arm_pmu *cpu_pmu)
{
	armv7pmu_init(cpu_pmu);
	cpu_pmu->name		= "ARMv7_Cortex_A7";
	cpu_pmu->map_event	= armv7_a7_map_event;
	cpu_pmu->num_events	= armv7_read_num_pmnc_events();
	cpu_pmu->set_event_filter = armv7pmu_set_event_filter;
	return 0;
}
#else
static inline int armv7_a8_pmu_init(struct arm_pmu *cpu_pmu)
{
	return -ENODEV;
}

static inline int armv7_a9_pmu_init(struct arm_pmu *cpu_pmu)
{
	return -ENODEV;
}

static inline int armv7_a5_pmu_init(struct arm_pmu *cpu_pmu)
{
	return -ENODEV;
}

static inline int armv7_a15_pmu_init(struct arm_pmu *cpu_pmu)
{
	return -ENODEV;
}

static inline int armv7_a7_pmu_init(struct arm_pmu *cpu_pmu)
{
	return -ENODEV;
}
#endif	/* CONFIG_CPU_V7 */
