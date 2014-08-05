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

#include <asm/cp15.h>
#include <asm/vfp.h>
#include "../vfp/vfpinstr.h"

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

/* ARMv7 Cortex-A12 specific event types */
enum armv7_a12_perf_types {
	ARMV7_A12_PERFCTR_L1_DCACHE_ACCESS_READ		= 0x40,
	ARMV7_A12_PERFCTR_L1_DCACHE_ACCESS_WRITE	= 0x41,

	ARMV7_A12_PERFCTR_L2_CACHE_ACCESS_READ		= 0x50,
	ARMV7_A12_PERFCTR_L2_CACHE_ACCESS_WRITE		= 0x51,

	ARMV7_A12_PERFCTR_PC_WRITE_SPEC			= 0x76,

	ARMV7_A12_PERFCTR_PF_TLB_REFILL			= 0xe7,
};

/* ARMv7 Krait specific event types */
enum krait_perf_types {
	KRAIT_PMRESR0_GROUP0				= 0xcc,
	KRAIT_PMRESR1_GROUP0				= 0xd0,
	KRAIT_PMRESR2_GROUP0				= 0xd4,
	KRAIT_VPMRESR0_GROUP0				= 0xd8,

	KRAIT_PERFCTR_L1_ICACHE_ACCESS			= 0x10011,
	KRAIT_PERFCTR_L1_ICACHE_MISS			= 0x10010,

	KRAIT_PERFCTR_L1_ITLB_ACCESS			= 0x12222,
	KRAIT_PERFCTR_L1_DTLB_ACCESS			= 0x12210,
};

/*
 * Cortex-A8 HW events mapping
 *
 * The hardware events that we support. We do support cache operations but
 * we have harvard caches and no way to combine instruction and data
 * accesses/misses in hardware.
 */
static const unsigned armv7_a8_perf_map[PERF_COUNT_HW_MAX] = {
	PERF_MAP_ALL_UNSUPPORTED,
	[PERF_COUNT_HW_CPU_CYCLES]		= ARMV7_PERFCTR_CPU_CYCLES,
	[PERF_COUNT_HW_INSTRUCTIONS]		= ARMV7_PERFCTR_INSTR_EXECUTED,
	[PERF_COUNT_HW_CACHE_REFERENCES]	= ARMV7_PERFCTR_L1_DCACHE_ACCESS,
	[PERF_COUNT_HW_CACHE_MISSES]		= ARMV7_PERFCTR_L1_DCACHE_REFILL,
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS]	= ARMV7_PERFCTR_PC_WRITE,
	[PERF_COUNT_HW_BRANCH_MISSES]		= ARMV7_PERFCTR_PC_BRANCH_MIS_PRED,
	[PERF_COUNT_HW_STALLED_CYCLES_FRONTEND]	= ARMV7_A8_PERFCTR_STALL_ISIDE,
};

static const unsigned armv7_a8_perf_cache_map[PERF_COUNT_HW_CACHE_MAX]
					  [PERF_COUNT_HW_CACHE_OP_MAX]
					  [PERF_COUNT_HW_CACHE_RESULT_MAX] = {
	PERF_CACHE_MAP_ALL_UNSUPPORTED,

	/*
	 * The performance counters don't differentiate between read and write
	 * accesses/misses so this isn't strictly correct, but it's the best we
	 * can do. Writes and reads get combined.
	 */
	[C(L1D)][C(OP_READ)][C(RESULT_ACCESS)]	= ARMV7_PERFCTR_L1_DCACHE_ACCESS,
	[C(L1D)][C(OP_READ)][C(RESULT_MISS)]	= ARMV7_PERFCTR_L1_DCACHE_REFILL,
	[C(L1D)][C(OP_WRITE)][C(RESULT_ACCESS)]	= ARMV7_PERFCTR_L1_DCACHE_ACCESS,
	[C(L1D)][C(OP_WRITE)][C(RESULT_MISS)]	= ARMV7_PERFCTR_L1_DCACHE_REFILL,

	[C(L1I)][C(OP_READ)][C(RESULT_ACCESS)]	= ARMV7_A8_PERFCTR_L1_ICACHE_ACCESS,
	[C(L1I)][C(OP_READ)][C(RESULT_MISS)]	= ARMV7_PERFCTR_L1_ICACHE_REFILL,

	[C(LL)][C(OP_READ)][C(RESULT_ACCESS)]	= ARMV7_A8_PERFCTR_L2_CACHE_ACCESS,
	[C(LL)][C(OP_READ)][C(RESULT_MISS)]	= ARMV7_A8_PERFCTR_L2_CACHE_REFILL,
	[C(LL)][C(OP_WRITE)][C(RESULT_ACCESS)]	= ARMV7_A8_PERFCTR_L2_CACHE_ACCESS,
	[C(LL)][C(OP_WRITE)][C(RESULT_MISS)]	= ARMV7_A8_PERFCTR_L2_CACHE_REFILL,

	[C(DTLB)][C(OP_READ)][C(RESULT_MISS)]	= ARMV7_PERFCTR_DTLB_REFILL,
	[C(DTLB)][C(OP_WRITE)][C(RESULT_MISS)]	= ARMV7_PERFCTR_DTLB_REFILL,

	[C(ITLB)][C(OP_READ)][C(RESULT_MISS)]	= ARMV7_PERFCTR_ITLB_REFILL,
	[C(ITLB)][C(OP_WRITE)][C(RESULT_MISS)]	= ARMV7_PERFCTR_ITLB_REFILL,

	[C(BPU)][C(OP_READ)][C(RESULT_ACCESS)]	= ARMV7_PERFCTR_PC_BRANCH_PRED,
	[C(BPU)][C(OP_READ)][C(RESULT_MISS)]	= ARMV7_PERFCTR_PC_BRANCH_MIS_PRED,
	[C(BPU)][C(OP_WRITE)][C(RESULT_ACCESS)]	= ARMV7_PERFCTR_PC_BRANCH_PRED,
	[C(BPU)][C(OP_WRITE)][C(RESULT_MISS)]	= ARMV7_PERFCTR_PC_BRANCH_MIS_PRED,
};

/*
 * Cortex-A9 HW events mapping
 */
static const unsigned armv7_a9_perf_map[PERF_COUNT_HW_MAX] = {
	PERF_MAP_ALL_UNSUPPORTED,
	[PERF_COUNT_HW_CPU_CYCLES]		= ARMV7_PERFCTR_CPU_CYCLES,
	[PERF_COUNT_HW_INSTRUCTIONS]		= ARMV7_A9_PERFCTR_INSTR_CORE_RENAME,
	[PERF_COUNT_HW_CACHE_REFERENCES]	= ARMV7_PERFCTR_L1_DCACHE_ACCESS,
	[PERF_COUNT_HW_CACHE_MISSES]		= ARMV7_PERFCTR_L1_DCACHE_REFILL,
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS]	= ARMV7_PERFCTR_PC_WRITE,
	[PERF_COUNT_HW_BRANCH_MISSES]		= ARMV7_PERFCTR_PC_BRANCH_MIS_PRED,
	[PERF_COUNT_HW_STALLED_CYCLES_FRONTEND]	= ARMV7_A9_PERFCTR_STALL_ICACHE,
	[PERF_COUNT_HW_STALLED_CYCLES_BACKEND]	= ARMV7_A9_PERFCTR_STALL_DISPATCH,
};

static const unsigned armv7_a9_perf_cache_map[PERF_COUNT_HW_CACHE_MAX]
					  [PERF_COUNT_HW_CACHE_OP_MAX]
					  [PERF_COUNT_HW_CACHE_RESULT_MAX] = {
	PERF_CACHE_MAP_ALL_UNSUPPORTED,

	/*
	 * The performance counters don't differentiate between read and write
	 * accesses/misses so this isn't strictly correct, but it's the best we
	 * can do. Writes and reads get combined.
	 */
	[C(L1D)][C(OP_READ)][C(RESULT_ACCESS)]	= ARMV7_PERFCTR_L1_DCACHE_ACCESS,
	[C(L1D)][C(OP_READ)][C(RESULT_MISS)]	= ARMV7_PERFCTR_L1_DCACHE_REFILL,
	[C(L1D)][C(OP_WRITE)][C(RESULT_ACCESS)]	= ARMV7_PERFCTR_L1_DCACHE_ACCESS,
	[C(L1D)][C(OP_WRITE)][C(RESULT_MISS)]	= ARMV7_PERFCTR_L1_DCACHE_REFILL,

	[C(L1I)][C(OP_READ)][C(RESULT_MISS)]	= ARMV7_PERFCTR_L1_ICACHE_REFILL,

	[C(DTLB)][C(OP_READ)][C(RESULT_MISS)]	= ARMV7_PERFCTR_DTLB_REFILL,
	[C(DTLB)][C(OP_WRITE)][C(RESULT_MISS)]	= ARMV7_PERFCTR_DTLB_REFILL,

	[C(ITLB)][C(OP_READ)][C(RESULT_MISS)]	= ARMV7_PERFCTR_ITLB_REFILL,
	[C(ITLB)][C(OP_WRITE)][C(RESULT_MISS)]	= ARMV7_PERFCTR_ITLB_REFILL,

	[C(BPU)][C(OP_READ)][C(RESULT_ACCESS)]	= ARMV7_PERFCTR_PC_BRANCH_PRED,
	[C(BPU)][C(OP_READ)][C(RESULT_MISS)]	= ARMV7_PERFCTR_PC_BRANCH_MIS_PRED,
	[C(BPU)][C(OP_WRITE)][C(RESULT_ACCESS)]	= ARMV7_PERFCTR_PC_BRANCH_PRED,
	[C(BPU)][C(OP_WRITE)][C(RESULT_MISS)]	= ARMV7_PERFCTR_PC_BRANCH_MIS_PRED,
};

/*
 * Cortex-A5 HW events mapping
 */
static const unsigned armv7_a5_perf_map[PERF_COUNT_HW_MAX] = {
	PERF_MAP_ALL_UNSUPPORTED,
	[PERF_COUNT_HW_CPU_CYCLES]		= ARMV7_PERFCTR_CPU_CYCLES,
	[PERF_COUNT_HW_INSTRUCTIONS]		= ARMV7_PERFCTR_INSTR_EXECUTED,
	[PERF_COUNT_HW_CACHE_REFERENCES]	= ARMV7_PERFCTR_L1_DCACHE_ACCESS,
	[PERF_COUNT_HW_CACHE_MISSES]		= ARMV7_PERFCTR_L1_DCACHE_REFILL,
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS]	= ARMV7_PERFCTR_PC_WRITE,
	[PERF_COUNT_HW_BRANCH_MISSES]		= ARMV7_PERFCTR_PC_BRANCH_MIS_PRED,
};

static const unsigned armv7_a5_perf_cache_map[PERF_COUNT_HW_CACHE_MAX]
					[PERF_COUNT_HW_CACHE_OP_MAX]
					[PERF_COUNT_HW_CACHE_RESULT_MAX] = {
	PERF_CACHE_MAP_ALL_UNSUPPORTED,

	[C(L1D)][C(OP_READ)][C(RESULT_ACCESS)]	= ARMV7_PERFCTR_L1_DCACHE_ACCESS,
	[C(L1D)][C(OP_READ)][C(RESULT_MISS)]	= ARMV7_PERFCTR_L1_DCACHE_REFILL,
	[C(L1D)][C(OP_WRITE)][C(RESULT_ACCESS)]	= ARMV7_PERFCTR_L1_DCACHE_ACCESS,
	[C(L1D)][C(OP_WRITE)][C(RESULT_MISS)]	= ARMV7_PERFCTR_L1_DCACHE_REFILL,
	[C(L1D)][C(OP_PREFETCH)][C(RESULT_ACCESS)]	= ARMV7_A5_PERFCTR_PREFETCH_LINEFILL,
	[C(L1D)][C(OP_PREFETCH)][C(RESULT_MISS)]	= ARMV7_A5_PERFCTR_PREFETCH_LINEFILL_DROP,

	[C(L1I)][C(OP_READ)][C(RESULT_ACCESS)]	= ARMV7_PERFCTR_L1_ICACHE_ACCESS,
	[C(L1I)][C(OP_READ)][C(RESULT_MISS)]	= ARMV7_PERFCTR_L1_ICACHE_REFILL,
	/*
	 * The prefetch counters don't differentiate between the I side and the
	 * D side.
	 */
	[C(L1I)][C(OP_PREFETCH)][C(RESULT_ACCESS)]	= ARMV7_A5_PERFCTR_PREFETCH_LINEFILL,
	[C(L1I)][C(OP_PREFETCH)][C(RESULT_MISS)]	= ARMV7_A5_PERFCTR_PREFETCH_LINEFILL_DROP,

	[C(DTLB)][C(OP_READ)][C(RESULT_MISS)]	= ARMV7_PERFCTR_DTLB_REFILL,
	[C(DTLB)][C(OP_WRITE)][C(RESULT_MISS)]	= ARMV7_PERFCTR_DTLB_REFILL,

	[C(ITLB)][C(OP_READ)][C(RESULT_MISS)]	= ARMV7_PERFCTR_ITLB_REFILL,
	[C(ITLB)][C(OP_WRITE)][C(RESULT_MISS)]	= ARMV7_PERFCTR_ITLB_REFILL,

	[C(BPU)][C(OP_READ)][C(RESULT_ACCESS)]	= ARMV7_PERFCTR_PC_BRANCH_PRED,
	[C(BPU)][C(OP_READ)][C(RESULT_MISS)]	= ARMV7_PERFCTR_PC_BRANCH_MIS_PRED,
	[C(BPU)][C(OP_WRITE)][C(RESULT_ACCESS)]	= ARMV7_PERFCTR_PC_BRANCH_PRED,
	[C(BPU)][C(OP_WRITE)][C(RESULT_MISS)]	= ARMV7_PERFCTR_PC_BRANCH_MIS_PRED,
};

/*
 * Cortex-A15 HW events mapping
 */
static const unsigned armv7_a15_perf_map[PERF_COUNT_HW_MAX] = {
	PERF_MAP_ALL_UNSUPPORTED,
	[PERF_COUNT_HW_CPU_CYCLES]		= ARMV7_PERFCTR_CPU_CYCLES,
	[PERF_COUNT_HW_INSTRUCTIONS]		= ARMV7_PERFCTR_INSTR_EXECUTED,
	[PERF_COUNT_HW_CACHE_REFERENCES]	= ARMV7_PERFCTR_L1_DCACHE_ACCESS,
	[PERF_COUNT_HW_CACHE_MISSES]		= ARMV7_PERFCTR_L1_DCACHE_REFILL,
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS]	= ARMV7_A15_PERFCTR_PC_WRITE_SPEC,
	[PERF_COUNT_HW_BRANCH_MISSES]		= ARMV7_PERFCTR_PC_BRANCH_MIS_PRED,
	[PERF_COUNT_HW_BUS_CYCLES]		= ARMV7_PERFCTR_BUS_CYCLES,
};

static const unsigned armv7_a15_perf_cache_map[PERF_COUNT_HW_CACHE_MAX]
					[PERF_COUNT_HW_CACHE_OP_MAX]
					[PERF_COUNT_HW_CACHE_RESULT_MAX] = {
	PERF_CACHE_MAP_ALL_UNSUPPORTED,

	[C(L1D)][C(OP_READ)][C(RESULT_ACCESS)]	= ARMV7_A15_PERFCTR_L1_DCACHE_ACCESS_READ,
	[C(L1D)][C(OP_READ)][C(RESULT_MISS)]	= ARMV7_A15_PERFCTR_L1_DCACHE_REFILL_READ,
	[C(L1D)][C(OP_WRITE)][C(RESULT_ACCESS)]	= ARMV7_A15_PERFCTR_L1_DCACHE_ACCESS_WRITE,
	[C(L1D)][C(OP_WRITE)][C(RESULT_MISS)]	= ARMV7_A15_PERFCTR_L1_DCACHE_REFILL_WRITE,

	/*
	 * Not all performance counters differentiate between read and write
	 * accesses/misses so we're not always strictly correct, but it's the
	 * best we can do. Writes and reads get combined in these cases.
	 */
	[C(L1I)][C(OP_READ)][C(RESULT_ACCESS)]	= ARMV7_PERFCTR_L1_ICACHE_ACCESS,
	[C(L1I)][C(OP_READ)][C(RESULT_MISS)]	= ARMV7_PERFCTR_L1_ICACHE_REFILL,

	[C(LL)][C(OP_READ)][C(RESULT_ACCESS)]	= ARMV7_A15_PERFCTR_L2_CACHE_ACCESS_READ,
	[C(LL)][C(OP_READ)][C(RESULT_MISS)]	= ARMV7_A15_PERFCTR_L2_CACHE_REFILL_READ,
	[C(LL)][C(OP_WRITE)][C(RESULT_ACCESS)]	= ARMV7_A15_PERFCTR_L2_CACHE_ACCESS_WRITE,
	[C(LL)][C(OP_WRITE)][C(RESULT_MISS)]	= ARMV7_A15_PERFCTR_L2_CACHE_REFILL_WRITE,

	[C(DTLB)][C(OP_READ)][C(RESULT_MISS)]	= ARMV7_A15_PERFCTR_DTLB_REFILL_L1_READ,
	[C(DTLB)][C(OP_WRITE)][C(RESULT_MISS)]	= ARMV7_A15_PERFCTR_DTLB_REFILL_L1_WRITE,

	[C(ITLB)][C(OP_READ)][C(RESULT_MISS)]	= ARMV7_PERFCTR_ITLB_REFILL,
	[C(ITLB)][C(OP_WRITE)][C(RESULT_MISS)]	= ARMV7_PERFCTR_ITLB_REFILL,

	[C(BPU)][C(OP_READ)][C(RESULT_ACCESS)]	= ARMV7_PERFCTR_PC_BRANCH_PRED,
	[C(BPU)][C(OP_READ)][C(RESULT_MISS)]	= ARMV7_PERFCTR_PC_BRANCH_MIS_PRED,
	[C(BPU)][C(OP_WRITE)][C(RESULT_ACCESS)]	= ARMV7_PERFCTR_PC_BRANCH_PRED,
	[C(BPU)][C(OP_WRITE)][C(RESULT_MISS)]	= ARMV7_PERFCTR_PC_BRANCH_MIS_PRED,
};

/*
 * Cortex-A7 HW events mapping
 */
static const unsigned armv7_a7_perf_map[PERF_COUNT_HW_MAX] = {
	PERF_MAP_ALL_UNSUPPORTED,
	[PERF_COUNT_HW_CPU_CYCLES]		= ARMV7_PERFCTR_CPU_CYCLES,
	[PERF_COUNT_HW_INSTRUCTIONS]		= ARMV7_PERFCTR_INSTR_EXECUTED,
	[PERF_COUNT_HW_CACHE_REFERENCES]	= ARMV7_PERFCTR_L1_DCACHE_ACCESS,
	[PERF_COUNT_HW_CACHE_MISSES]		= ARMV7_PERFCTR_L1_DCACHE_REFILL,
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS]	= ARMV7_PERFCTR_PC_WRITE,
	[PERF_COUNT_HW_BRANCH_MISSES]		= ARMV7_PERFCTR_PC_BRANCH_MIS_PRED,
	[PERF_COUNT_HW_BUS_CYCLES]		= ARMV7_PERFCTR_BUS_CYCLES,
};

static const unsigned armv7_a7_perf_cache_map[PERF_COUNT_HW_CACHE_MAX]
					[PERF_COUNT_HW_CACHE_OP_MAX]
					[PERF_COUNT_HW_CACHE_RESULT_MAX] = {
	PERF_CACHE_MAP_ALL_UNSUPPORTED,

	/*
	 * The performance counters don't differentiate between read and write
	 * accesses/misses so this isn't strictly correct, but it's the best we
	 * can do. Writes and reads get combined.
	 */
	[C(L1D)][C(OP_READ)][C(RESULT_ACCESS)]	= ARMV7_PERFCTR_L1_DCACHE_ACCESS,
	[C(L1D)][C(OP_READ)][C(RESULT_MISS)]	= ARMV7_PERFCTR_L1_DCACHE_REFILL,
	[C(L1D)][C(OP_WRITE)][C(RESULT_ACCESS)]	= ARMV7_PERFCTR_L1_DCACHE_ACCESS,
	[C(L1D)][C(OP_WRITE)][C(RESULT_MISS)]	= ARMV7_PERFCTR_L1_DCACHE_REFILL,

	[C(L1I)][C(OP_READ)][C(RESULT_ACCESS)]	= ARMV7_PERFCTR_L1_ICACHE_ACCESS,
	[C(L1I)][C(OP_READ)][C(RESULT_MISS)]	= ARMV7_PERFCTR_L1_ICACHE_REFILL,

	[C(LL)][C(OP_READ)][C(RESULT_ACCESS)]	= ARMV7_PERFCTR_L2_CACHE_ACCESS,
	[C(LL)][C(OP_READ)][C(RESULT_MISS)]	= ARMV7_PERFCTR_L2_CACHE_REFILL,
	[C(LL)][C(OP_WRITE)][C(RESULT_ACCESS)]	= ARMV7_PERFCTR_L2_CACHE_ACCESS,
	[C(LL)][C(OP_WRITE)][C(RESULT_MISS)]	= ARMV7_PERFCTR_L2_CACHE_REFILL,

	[C(DTLB)][C(OP_READ)][C(RESULT_MISS)]	= ARMV7_PERFCTR_DTLB_REFILL,
	[C(DTLB)][C(OP_WRITE)][C(RESULT_MISS)]	= ARMV7_PERFCTR_DTLB_REFILL,

	[C(ITLB)][C(OP_READ)][C(RESULT_MISS)]	= ARMV7_PERFCTR_ITLB_REFILL,
	[C(ITLB)][C(OP_WRITE)][C(RESULT_MISS)]	= ARMV7_PERFCTR_ITLB_REFILL,

	[C(BPU)][C(OP_READ)][C(RESULT_ACCESS)]	= ARMV7_PERFCTR_PC_BRANCH_PRED,
	[C(BPU)][C(OP_READ)][C(RESULT_MISS)]	= ARMV7_PERFCTR_PC_BRANCH_MIS_PRED,
	[C(BPU)][C(OP_WRITE)][C(RESULT_ACCESS)]	= ARMV7_PERFCTR_PC_BRANCH_PRED,
	[C(BPU)][C(OP_WRITE)][C(RESULT_MISS)]	= ARMV7_PERFCTR_PC_BRANCH_MIS_PRED,
};

/*
 * Cortex-A12 HW events mapping
 */
static const unsigned armv7_a12_perf_map[PERF_COUNT_HW_MAX] = {
	PERF_MAP_ALL_UNSUPPORTED,
	[PERF_COUNT_HW_CPU_CYCLES]		= ARMV7_PERFCTR_CPU_CYCLES,
	[PERF_COUNT_HW_INSTRUCTIONS]		= ARMV7_PERFCTR_INSTR_EXECUTED,
	[PERF_COUNT_HW_CACHE_REFERENCES]	= ARMV7_PERFCTR_L1_DCACHE_ACCESS,
	[PERF_COUNT_HW_CACHE_MISSES]		= ARMV7_PERFCTR_L1_DCACHE_REFILL,
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS]	= ARMV7_A12_PERFCTR_PC_WRITE_SPEC,
	[PERF_COUNT_HW_BRANCH_MISSES]		= ARMV7_PERFCTR_PC_BRANCH_MIS_PRED,
	[PERF_COUNT_HW_BUS_CYCLES]		= ARMV7_PERFCTR_BUS_CYCLES,
};

static const unsigned armv7_a12_perf_cache_map[PERF_COUNT_HW_CACHE_MAX]
					[PERF_COUNT_HW_CACHE_OP_MAX]
					[PERF_COUNT_HW_CACHE_RESULT_MAX] = {
	PERF_CACHE_MAP_ALL_UNSUPPORTED,

	[C(L1D)][C(OP_READ)][C(RESULT_ACCESS)]	= ARMV7_A12_PERFCTR_L1_DCACHE_ACCESS_READ,
	[C(L1D)][C(OP_READ)][C(RESULT_MISS)]	= ARMV7_PERFCTR_L1_DCACHE_REFILL,
	[C(L1D)][C(OP_WRITE)][C(RESULT_ACCESS)]	= ARMV7_A12_PERFCTR_L1_DCACHE_ACCESS_WRITE,
	[C(L1D)][C(OP_WRITE)][C(RESULT_MISS)]	= ARMV7_PERFCTR_L1_DCACHE_REFILL,

	/*
	 * Not all performance counters differentiate between read and write
	 * accesses/misses so we're not always strictly correct, but it's the
	 * best we can do. Writes and reads get combined in these cases.
	 */
	[C(L1I)][C(OP_READ)][C(RESULT_ACCESS)]	= ARMV7_PERFCTR_L1_ICACHE_ACCESS,
	[C(L1I)][C(OP_READ)][C(RESULT_MISS)]	= ARMV7_PERFCTR_L1_ICACHE_REFILL,

	[C(LL)][C(OP_READ)][C(RESULT_ACCESS)]	= ARMV7_A12_PERFCTR_L2_CACHE_ACCESS_READ,
	[C(LL)][C(OP_READ)][C(RESULT_MISS)]	= ARMV7_PERFCTR_L2_CACHE_REFILL,
	[C(LL)][C(OP_WRITE)][C(RESULT_ACCESS)]	= ARMV7_A12_PERFCTR_L2_CACHE_ACCESS_WRITE,
	[C(LL)][C(OP_WRITE)][C(RESULT_MISS)]	= ARMV7_PERFCTR_L2_CACHE_REFILL,

	[C(DTLB)][C(OP_READ)][C(RESULT_MISS)]	= ARMV7_PERFCTR_DTLB_REFILL,
	[C(DTLB)][C(OP_WRITE)][C(RESULT_MISS)]	= ARMV7_PERFCTR_DTLB_REFILL,
	[C(DTLB)][C(OP_PREFETCH)][C(RESULT_MISS)]	= ARMV7_A12_PERFCTR_PF_TLB_REFILL,

	[C(ITLB)][C(OP_READ)][C(RESULT_MISS)]	= ARMV7_PERFCTR_ITLB_REFILL,
	[C(ITLB)][C(OP_WRITE)][C(RESULT_MISS)]	= ARMV7_PERFCTR_ITLB_REFILL,

	[C(BPU)][C(OP_READ)][C(RESULT_ACCESS)]	= ARMV7_PERFCTR_PC_BRANCH_PRED,
	[C(BPU)][C(OP_READ)][C(RESULT_MISS)]	= ARMV7_PERFCTR_PC_BRANCH_MIS_PRED,
	[C(BPU)][C(OP_WRITE)][C(RESULT_ACCESS)]	= ARMV7_PERFCTR_PC_BRANCH_PRED,
	[C(BPU)][C(OP_WRITE)][C(RESULT_MISS)]	= ARMV7_PERFCTR_PC_BRANCH_MIS_PRED,
};

/*
 * Krait HW events mapping
 */
static const unsigned krait_perf_map[PERF_COUNT_HW_MAX] = {
	PERF_MAP_ALL_UNSUPPORTED,
	[PERF_COUNT_HW_CPU_CYCLES]	    = ARMV7_PERFCTR_CPU_CYCLES,
	[PERF_COUNT_HW_INSTRUCTIONS]	    = ARMV7_PERFCTR_INSTR_EXECUTED,
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS] = ARMV7_PERFCTR_PC_WRITE,
	[PERF_COUNT_HW_BRANCH_MISSES]	    = ARMV7_PERFCTR_PC_BRANCH_MIS_PRED,
	[PERF_COUNT_HW_BUS_CYCLES]	    = ARMV7_PERFCTR_CLOCK_CYCLES,
};

static const unsigned krait_perf_map_no_branch[PERF_COUNT_HW_MAX] = {
	PERF_MAP_ALL_UNSUPPORTED,
	[PERF_COUNT_HW_CPU_CYCLES]	    = ARMV7_PERFCTR_CPU_CYCLES,
	[PERF_COUNT_HW_INSTRUCTIONS]	    = ARMV7_PERFCTR_INSTR_EXECUTED,
	[PERF_COUNT_HW_BRANCH_MISSES]	    = ARMV7_PERFCTR_PC_BRANCH_MIS_PRED,
	[PERF_COUNT_HW_BUS_CYCLES]	    = ARMV7_PERFCTR_CLOCK_CYCLES,
};

static const unsigned krait_perf_cache_map[PERF_COUNT_HW_CACHE_MAX]
					  [PERF_COUNT_HW_CACHE_OP_MAX]
					  [PERF_COUNT_HW_CACHE_RESULT_MAX] = {
	PERF_CACHE_MAP_ALL_UNSUPPORTED,

	/*
	 * The performance counters don't differentiate between read and write
	 * accesses/misses so this isn't strictly correct, but it's the best we
	 * can do. Writes and reads get combined.
	 */
	[C(L1D)][C(OP_READ)][C(RESULT_ACCESS)]	= ARMV7_PERFCTR_L1_DCACHE_ACCESS,
	[C(L1D)][C(OP_READ)][C(RESULT_MISS)]	= ARMV7_PERFCTR_L1_DCACHE_REFILL,
	[C(L1D)][C(OP_WRITE)][C(RESULT_ACCESS)]	= ARMV7_PERFCTR_L1_DCACHE_ACCESS,
	[C(L1D)][C(OP_WRITE)][C(RESULT_MISS)]	= ARMV7_PERFCTR_L1_DCACHE_REFILL,

	[C(L1I)][C(OP_READ)][C(RESULT_ACCESS)]	= KRAIT_PERFCTR_L1_ICACHE_ACCESS,
	[C(L1I)][C(OP_READ)][C(RESULT_MISS)]	= KRAIT_PERFCTR_L1_ICACHE_MISS,

	[C(DTLB)][C(OP_READ)][C(RESULT_ACCESS)]	= KRAIT_PERFCTR_L1_DTLB_ACCESS,
	[C(DTLB)][C(OP_WRITE)][C(RESULT_ACCESS)]	= KRAIT_PERFCTR_L1_DTLB_ACCESS,

	[C(ITLB)][C(OP_READ)][C(RESULT_ACCESS)]	= KRAIT_PERFCTR_L1_ITLB_ACCESS,
	[C(ITLB)][C(OP_WRITE)][C(RESULT_ACCESS)]	= KRAIT_PERFCTR_L1_ITLB_ACCESS,

	[C(BPU)][C(OP_READ)][C(RESULT_ACCESS)]	= ARMV7_PERFCTR_PC_BRANCH_PRED,
	[C(BPU)][C(OP_READ)][C(RESULT_MISS)]	= ARMV7_PERFCTR_PC_BRANCH_MIS_PRED,
	[C(BPU)][C(OP_WRITE)][C(RESULT_ACCESS)]	= ARMV7_PERFCTR_PC_BRANCH_PRED,
	[C(BPU)][C(OP_WRITE)][C(RESULT_MISS)]	= ARMV7_PERFCTR_PC_BRANCH_MIS_PRED,
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

static int armv7_a12_map_event(struct perf_event *event)
{
	return armpmu_map_event(event, &armv7_a12_perf_map,
				&armv7_a12_perf_cache_map, 0xFF);
}

static int krait_map_event(struct perf_event *event)
{
	return armpmu_map_event(event, &krait_perf_map,
				&krait_perf_cache_map, 0xFFFFF);
}

static int krait_map_event_no_branch(struct perf_event *event)
{
	return armpmu_map_event(event, &krait_perf_map_no_branch,
				&krait_perf_cache_map, 0xFFFFF);
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
	cpu_pmu->name		= "armv7_cortex_a8";
	cpu_pmu->map_event	= armv7_a8_map_event;
	cpu_pmu->num_events	= armv7_read_num_pmnc_events();
	return 0;
}

static int armv7_a9_pmu_init(struct arm_pmu *cpu_pmu)
{
	armv7pmu_init(cpu_pmu);
	cpu_pmu->name		= "armv7_cortex_a9";
	cpu_pmu->map_event	= armv7_a9_map_event;
	cpu_pmu->num_events	= armv7_read_num_pmnc_events();
	return 0;
}

static int armv7_a5_pmu_init(struct arm_pmu *cpu_pmu)
{
	armv7pmu_init(cpu_pmu);
	cpu_pmu->name		= "armv7_cortex_a5";
	cpu_pmu->map_event	= armv7_a5_map_event;
	cpu_pmu->num_events	= armv7_read_num_pmnc_events();
	return 0;
}

static int armv7_a15_pmu_init(struct arm_pmu *cpu_pmu)
{
	armv7pmu_init(cpu_pmu);
	cpu_pmu->name		= "armv7_cortex_a15";
	cpu_pmu->map_event	= armv7_a15_map_event;
	cpu_pmu->num_events	= armv7_read_num_pmnc_events();
	cpu_pmu->set_event_filter = armv7pmu_set_event_filter;
	return 0;
}

static int armv7_a7_pmu_init(struct arm_pmu *cpu_pmu)
{
	armv7pmu_init(cpu_pmu);
	cpu_pmu->name		= "armv7_cortex_a7";
	cpu_pmu->map_event	= armv7_a7_map_event;
	cpu_pmu->num_events	= armv7_read_num_pmnc_events();
	cpu_pmu->set_event_filter = armv7pmu_set_event_filter;
	return 0;
}

static int armv7_a12_pmu_init(struct arm_pmu *cpu_pmu)
{
	armv7pmu_init(cpu_pmu);
	cpu_pmu->name		= "armv7_cortex_a12";
	cpu_pmu->map_event	= armv7_a12_map_event;
	cpu_pmu->num_events	= armv7_read_num_pmnc_events();
	cpu_pmu->set_event_filter = armv7pmu_set_event_filter;
	return 0;
}

static int armv7_a17_pmu_init(struct arm_pmu *cpu_pmu)
{
	armv7_a12_pmu_init(cpu_pmu);
	cpu_pmu->name = "armv7_cortex_a17";
	return 0;
}

/*
 * Krait Performance Monitor Region Event Selection Register (PMRESRn)
 *
 *            31   30     24     16     8      0
 *            +--------------------------------+
 *  PMRESR0   | EN |  CC  |  CC  |  CC  |  CC  |   N = 1, R = 0
 *            +--------------------------------+
 *  PMRESR1   | EN |  CC  |  CC  |  CC  |  CC  |   N = 1, R = 1
 *            +--------------------------------+
 *  PMRESR2   | EN |  CC  |  CC  |  CC  |  CC  |   N = 1, R = 2
 *            +--------------------------------+
 *  VPMRESR0  | EN |  CC  |  CC  |  CC  |  CC  |   N = 2, R = ?
 *            +--------------------------------+
 *              EN | G=3  | G=2  | G=1  | G=0
 *
 *  Event Encoding:
 *
 *      hwc->config_base = 0xNRCCG
 *
 *      N  = prefix, 1 for Krait CPU (PMRESRn), 2 for Venum VFP (VPMRESR)
 *      R  = region register
 *      CC = class of events the group G is choosing from
 *      G  = group or particular event
 *
 *  Example: 0x12021 is a Krait CPU event in PMRESR2's group 1 with code 2
 *
 *  A region (R) corresponds to a piece of the CPU (execution unit, instruction
 *  unit, etc.) while the event code (CC) corresponds to a particular class of
 *  events (interrupts for example). An event code is broken down into
 *  groups (G) that can be mapped into the PMU (irq, fiqs, and irq+fiqs for
 *  example).
 */

#define KRAIT_EVENT		(1 << 16)
#define VENUM_EVENT		(2 << 16)
#define KRAIT_EVENT_MASK	(KRAIT_EVENT | VENUM_EVENT)
#define PMRESRn_EN		BIT(31)

static u32 krait_read_pmresrn(int n)
{
	u32 val;

	switch (n) {
	case 0:
		asm volatile("mrc p15, 1, %0, c9, c15, 0" : "=r" (val));
		break;
	case 1:
		asm volatile("mrc p15, 1, %0, c9, c15, 1" : "=r" (val));
		break;
	case 2:
		asm volatile("mrc p15, 1, %0, c9, c15, 2" : "=r" (val));
		break;
	default:
		BUG(); /* Should be validated in krait_pmu_get_event_idx() */
	}

	return val;
}

static void krait_write_pmresrn(int n, u32 val)
{
	switch (n) {
	case 0:
		asm volatile("mcr p15, 1, %0, c9, c15, 0" : : "r" (val));
		break;
	case 1:
		asm volatile("mcr p15, 1, %0, c9, c15, 1" : : "r" (val));
		break;
	case 2:
		asm volatile("mcr p15, 1, %0, c9, c15, 2" : : "r" (val));
		break;
	default:
		BUG(); /* Should be validated in krait_pmu_get_event_idx() */
	}
}

static u32 krait_read_vpmresr0(void)
{
	u32 val;
	asm volatile("mrc p10, 7, %0, c11, c0, 0" : "=r" (val));
	return val;
}

static void krait_write_vpmresr0(u32 val)
{
	asm volatile("mcr p10, 7, %0, c11, c0, 0" : : "r" (val));
}

static void krait_pre_vpmresr0(u32 *venum_orig_val, u32 *fp_orig_val)
{
	u32 venum_new_val;
	u32 fp_new_val;

	BUG_ON(preemptible());
	/* CPACR Enable CP10 and CP11 access */
	*venum_orig_val = get_copro_access();
	venum_new_val = *venum_orig_val | CPACC_SVC(10) | CPACC_SVC(11);
	set_copro_access(venum_new_val);

	/* Enable FPEXC */
	*fp_orig_val = fmrx(FPEXC);
	fp_new_val = *fp_orig_val | FPEXC_EN;
	fmxr(FPEXC, fp_new_val);
}

static void krait_post_vpmresr0(u32 venum_orig_val, u32 fp_orig_val)
{
	BUG_ON(preemptible());
	/* Restore FPEXC */
	fmxr(FPEXC, fp_orig_val);
	isb();
	/* Restore CPACR */
	set_copro_access(venum_orig_val);
}

static u32 krait_get_pmresrn_event(unsigned int region)
{
	static const u32 pmresrn_table[] = { KRAIT_PMRESR0_GROUP0,
					     KRAIT_PMRESR1_GROUP0,
					     KRAIT_PMRESR2_GROUP0 };
	return pmresrn_table[region];
}

static void krait_evt_setup(int idx, u32 config_base)
{
	u32 val;
	u32 mask;
	u32 vval, fval;
	unsigned int region;
	unsigned int group;
	unsigned int code;
	unsigned int group_shift;
	bool venum_event;

	venum_event = !!(config_base & VENUM_EVENT);
	region = (config_base >> 12) & 0xf;
	code   = (config_base >> 4) & 0xff;
	group  = (config_base >> 0)  & 0xf;

	group_shift = group * 8;
	mask = 0xff << group_shift;

	/* Configure evtsel for the region and group */
	if (venum_event)
		val = KRAIT_VPMRESR0_GROUP0;
	else
		val = krait_get_pmresrn_event(region);
	val += group;
	/* Mix in mode-exclusion bits */
	val |= config_base & (ARMV7_EXCLUDE_USER | ARMV7_EXCLUDE_PL1);
	armv7_pmnc_write_evtsel(idx, val);

	asm volatile("mcr p15, 0, %0, c9, c15, 0" : : "r" (0));

	if (venum_event) {
		krait_pre_vpmresr0(&vval, &fval);
		val = krait_read_vpmresr0();
		val &= ~mask;
		val |= code << group_shift;
		val |= PMRESRn_EN;
		krait_write_vpmresr0(val);
		krait_post_vpmresr0(vval, fval);
	} else {
		val = krait_read_pmresrn(region);
		val &= ~mask;
		val |= code << group_shift;
		val |= PMRESRn_EN;
		krait_write_pmresrn(region, val);
	}
}

static u32 krait_clear_pmresrn_group(u32 val, int group)
{
	u32 mask;
	int group_shift;

	group_shift = group * 8;
	mask = 0xff << group_shift;
	val &= ~mask;

	/* Don't clear enable bit if entire region isn't disabled */
	if (val & ~PMRESRn_EN)
		return val |= PMRESRn_EN;

	return 0;
}

static void krait_clearpmu(u32 config_base)
{
	u32 val;
	u32 vval, fval;
	unsigned int region;
	unsigned int group;
	bool venum_event;

	venum_event = !!(config_base & VENUM_EVENT);
	region = (config_base >> 12) & 0xf;
	group  = (config_base >> 0)  & 0xf;

	if (venum_event) {
		krait_pre_vpmresr0(&vval, &fval);
		val = krait_read_vpmresr0();
		val = krait_clear_pmresrn_group(val, group);
		krait_write_vpmresr0(val);
		krait_post_vpmresr0(vval, fval);
	} else {
		val = krait_read_pmresrn(region);
		val = krait_clear_pmresrn_group(val, group);
		krait_write_pmresrn(region, val);
	}
}

static void krait_pmu_disable_event(struct perf_event *event)
{
	unsigned long flags;
	struct hw_perf_event *hwc = &event->hw;
	int idx = hwc->idx;
	struct arm_pmu *cpu_pmu = to_arm_pmu(event->pmu);
	struct pmu_hw_events *events = cpu_pmu->get_hw_events();

	/* Disable counter and interrupt */
	raw_spin_lock_irqsave(&events->pmu_lock, flags);

	/* Disable counter */
	armv7_pmnc_disable_counter(idx);

	/*
	 * Clear pmresr code (if destined for PMNx counters)
	 */
	if (hwc->config_base & KRAIT_EVENT_MASK)
		krait_clearpmu(hwc->config_base);

	/* Disable interrupt for this counter */
	armv7_pmnc_disable_intens(idx);

	raw_spin_unlock_irqrestore(&events->pmu_lock, flags);
}

static void krait_pmu_enable_event(struct perf_event *event)
{
	unsigned long flags;
	struct hw_perf_event *hwc = &event->hw;
	int idx = hwc->idx;
	struct arm_pmu *cpu_pmu = to_arm_pmu(event->pmu);
	struct pmu_hw_events *events = cpu_pmu->get_hw_events();

	/*
	 * Enable counter and interrupt, and set the counter to count
	 * the event that we're interested in.
	 */
	raw_spin_lock_irqsave(&events->pmu_lock, flags);

	/* Disable counter */
	armv7_pmnc_disable_counter(idx);

	/*
	 * Set event (if destined for PMNx counters)
	 * We set the event for the cycle counter because we
	 * have the ability to perform event filtering.
	 */
	if (hwc->config_base & KRAIT_EVENT_MASK)
		krait_evt_setup(idx, hwc->config_base);
	else
		armv7_pmnc_write_evtsel(idx, hwc->config_base);

	/* Enable interrupt for this counter */
	armv7_pmnc_enable_intens(idx);

	/* Enable counter */
	armv7_pmnc_enable_counter(idx);

	raw_spin_unlock_irqrestore(&events->pmu_lock, flags);
}

static void krait_pmu_reset(void *info)
{
	u32 vval, fval;

	armv7pmu_reset(info);

	/* Clear all pmresrs */
	krait_write_pmresrn(0, 0);
	krait_write_pmresrn(1, 0);
	krait_write_pmresrn(2, 0);

	krait_pre_vpmresr0(&vval, &fval);
	krait_write_vpmresr0(0);
	krait_post_vpmresr0(vval, fval);
}

static int krait_event_to_bit(struct perf_event *event, unsigned int region,
			      unsigned int group)
{
	int bit;
	struct hw_perf_event *hwc = &event->hw;
	struct arm_pmu *cpu_pmu = to_arm_pmu(event->pmu);

	if (hwc->config_base & VENUM_EVENT)
		bit = KRAIT_VPMRESR0_GROUP0;
	else
		bit = krait_get_pmresrn_event(region);
	bit -= krait_get_pmresrn_event(0);
	bit += group;
	/*
	 * Lower bits are reserved for use by the counters (see
	 * armv7pmu_get_event_idx() for more info)
	 */
	bit += ARMV7_IDX_COUNTER_LAST(cpu_pmu) + 1;

	return bit;
}

/*
 * We check for column exclusion constraints here.
 * Two events cant use the same group within a pmresr register.
 */
static int krait_pmu_get_event_idx(struct pmu_hw_events *cpuc,
				   struct perf_event *event)
{
	int idx;
	int bit = -1;
	unsigned int prefix;
	unsigned int region;
	unsigned int code;
	unsigned int group;
	bool krait_event;
	struct hw_perf_event *hwc = &event->hw;

	region = (hwc->config_base >> 12) & 0xf;
	code   = (hwc->config_base >> 4) & 0xff;
	group  = (hwc->config_base >> 0) & 0xf;
	krait_event = !!(hwc->config_base & KRAIT_EVENT_MASK);

	if (krait_event) {
		/* Ignore invalid events */
		if (group > 3 || region > 2)
			return -EINVAL;
		prefix = hwc->config_base & KRAIT_EVENT_MASK;
		if (prefix != KRAIT_EVENT && prefix != VENUM_EVENT)
			return -EINVAL;
		if (prefix == VENUM_EVENT && (code & 0xe0))
			return -EINVAL;

		bit = krait_event_to_bit(event, region, group);
		if (test_and_set_bit(bit, cpuc->used_mask))
			return -EAGAIN;
	}

	idx = armv7pmu_get_event_idx(cpuc, event);
	if (idx < 0 && bit >= 0)
		clear_bit(bit, cpuc->used_mask);

	return idx;
}

static void krait_pmu_clear_event_idx(struct pmu_hw_events *cpuc,
				      struct perf_event *event)
{
	int bit;
	struct hw_perf_event *hwc = &event->hw;
	unsigned int region;
	unsigned int group;
	bool krait_event;

	region = (hwc->config_base >> 12) & 0xf;
	group  = (hwc->config_base >> 0) & 0xf;
	krait_event = !!(hwc->config_base & KRAIT_EVENT_MASK);

	if (krait_event) {
		bit = krait_event_to_bit(event, region, group);
		clear_bit(bit, cpuc->used_mask);
	}
}

static int krait_pmu_init(struct arm_pmu *cpu_pmu)
{
	armv7pmu_init(cpu_pmu);
	cpu_pmu->name		= "armv7_krait";
	/* Some early versions of Krait don't support PC write events */
	if (of_property_read_bool(cpu_pmu->plat_device->dev.of_node,
				  "qcom,no-pc-write"))
		cpu_pmu->map_event = krait_map_event_no_branch;
	else
		cpu_pmu->map_event = krait_map_event;
	cpu_pmu->num_events	= armv7_read_num_pmnc_events();
	cpu_pmu->set_event_filter = armv7pmu_set_event_filter;
	cpu_pmu->reset		= krait_pmu_reset;
	cpu_pmu->enable		= krait_pmu_enable_event;
	cpu_pmu->disable	= krait_pmu_disable_event;
	cpu_pmu->get_event_idx	= krait_pmu_get_event_idx;
	cpu_pmu->clear_event_idx = krait_pmu_clear_event_idx;
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

static inline int armv7_a12_pmu_init(struct arm_pmu *cpu_pmu)
{
	return -ENODEV;
}

static inline int armv7_a17_pmu_init(struct arm_pmu *cpu_pmu)
{
	return -ENODEV;
}

static inline int krait_pmu_init(struct arm_pmu *cpu_pmu)
{
	return -ENODEV;
}
#endif	/* CONFIG_CPU_V7 */
