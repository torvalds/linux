/*
 * PMU support
 *
 * Copyright (C) 2012 ARM Limited
 * Author: Will Deacon <will.deacon@arm.com>
 *
 * This code is based heavily on the ARMv7 perf event code.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <asm/irq_regs.h>
#include <asm/perf_event.h>
#include <asm/virt.h>

#include <linux/of.h>
#include <linux/perf/arm_pmu.h>
#include <linux/platform_device.h>

/*
 * ARMv8 PMUv3 Performance Events handling code.
 * Common event types.
 */

/* Required events. */
#define ARMV8_PMUV3_PERFCTR_PMNC_SW_INCR			0x00
#define ARMV8_PMUV3_PERFCTR_L1_DCACHE_REFILL			0x03
#define ARMV8_PMUV3_PERFCTR_L1_DCACHE_ACCESS			0x04
#define ARMV8_PMUV3_PERFCTR_PC_BRANCH_MIS_PRED			0x10
#define ARMV8_PMUV3_PERFCTR_CLOCK_CYCLES			0x11
#define ARMV8_PMUV3_PERFCTR_PC_BRANCH_PRED			0x12

/* At least one of the following is required. */
#define ARMV8_PMUV3_PERFCTR_INSTR_EXECUTED			0x08
#define ARMV8_PMUV3_PERFCTR_OP_SPEC				0x1B

/* Common architectural events. */
#define ARMV8_PMUV3_PERFCTR_MEM_READ				0x06
#define ARMV8_PMUV3_PERFCTR_MEM_WRITE				0x07
#define ARMV8_PMUV3_PERFCTR_EXC_TAKEN				0x09
#define ARMV8_PMUV3_PERFCTR_EXC_EXECUTED			0x0A
#define ARMV8_PMUV3_PERFCTR_CID_WRITE				0x0B
#define ARMV8_PMUV3_PERFCTR_PC_WRITE				0x0C
#define ARMV8_PMUV3_PERFCTR_PC_IMM_BRANCH			0x0D
#define ARMV8_PMUV3_PERFCTR_PC_PROC_RETURN			0x0E
#define ARMV8_PMUV3_PERFCTR_MEM_UNALIGNED_ACCESS		0x0F
#define ARMV8_PMUV3_PERFCTR_TTBR_WRITE				0x1C
#define ARMV8_PMUV3_PERFCTR_CHAIN				0x1E
#define ARMV8_PMUV3_PERFCTR_BR_RETIRED				0x21

/* Common microarchitectural events. */
#define ARMV8_PMUV3_PERFCTR_L1_ICACHE_REFILL			0x01
#define ARMV8_PMUV3_PERFCTR_ITLB_REFILL				0x02
#define ARMV8_PMUV3_PERFCTR_DTLB_REFILL				0x05
#define ARMV8_PMUV3_PERFCTR_MEM_ACCESS				0x13
#define ARMV8_PMUV3_PERFCTR_L1_ICACHE_ACCESS			0x14
#define ARMV8_PMUV3_PERFCTR_L1_DCACHE_WB			0x15
#define ARMV8_PMUV3_PERFCTR_L2_CACHE_ACCESS			0x16
#define ARMV8_PMUV3_PERFCTR_L2_CACHE_REFILL			0x17
#define ARMV8_PMUV3_PERFCTR_L2_CACHE_WB				0x18
#define ARMV8_PMUV3_PERFCTR_BUS_ACCESS				0x19
#define ARMV8_PMUV3_PERFCTR_MEM_ERROR				0x1A
#define ARMV8_PMUV3_PERFCTR_BUS_CYCLES				0x1D
#define ARMV8_PMUV3_PERFCTR_L1D_CACHE_ALLOCATE			0x1F
#define ARMV8_PMUV3_PERFCTR_L2D_CACHE_ALLOCATE			0x20
#define ARMV8_PMUV3_PERFCTR_BR_MIS_PRED_RETIRED			0x22
#define ARMV8_PMUV3_PERFCTR_STALL_FRONTEND			0x23
#define ARMV8_PMUV3_PERFCTR_STALL_BACKEND			0x24
#define ARMV8_PMUV3_PERFCTR_L1D_TLB				0x25
#define ARMV8_PMUV3_PERFCTR_L1I_TLB				0x26
#define ARMV8_PMUV3_PERFCTR_L2I_CACHE				0x27
#define ARMV8_PMUV3_PERFCTR_L2I_CACHE_REFILL			0x28
#define ARMV8_PMUV3_PERFCTR_L3D_CACHE_ALLOCATE			0x29
#define ARMV8_PMUV3_PERFCTR_L3D_CACHE_REFILL			0x2A
#define ARMV8_PMUV3_PERFCTR_L3D_CACHE				0x2B
#define ARMV8_PMUV3_PERFCTR_L3D_CACHE_WB			0x2C
#define ARMV8_PMUV3_PERFCTR_L2D_TLB_REFILL			0x2D
#define ARMV8_PMUV3_PERFCTR_L21_TLB_REFILL			0x2E
#define ARMV8_PMUV3_PERFCTR_L2D_TLB				0x2F
#define ARMV8_PMUV3_PERFCTR_L21_TLB				0x30

/* ARMv8 implementation defined event types. */
#define ARMV8_IMPDEF_PERFCTR_L1_DCACHE_ACCESS_LD		0x40
#define ARMV8_IMPDEF_PERFCTR_L1_DCACHE_ACCESS_ST		0x41
#define ARMV8_IMPDEF_PERFCTR_L1_DCACHE_REFILL_LD		0x42
#define ARMV8_IMPDEF_PERFCTR_L1_DCACHE_REFILL_ST		0x43
#define ARMV8_IMPDEF_PERFCTR_DTLB_REFILL_LD			0x4C
#define ARMV8_IMPDEF_PERFCTR_DTLB_REFILL_ST			0x4D
#define ARMV8_IMPDEF_PERFCTR_DTLB_ACCESS_LD			0x4E
#define ARMV8_IMPDEF_PERFCTR_DTLB_ACCESS_ST			0x4F

/* ARMv8 Cortex-A53 specific event types. */
#define ARMV8_A53_PERFCTR_PREFETCH_LINEFILL			0xC2

/* ARMv8 Cavium ThunderX specific event types. */
#define ARMV8_THUNDER_PERFCTR_L1_DCACHE_MISS_ST			0xE9
#define ARMV8_THUNDER_PERFCTR_L1_DCACHE_PREF_ACCESS		0xEA
#define ARMV8_THUNDER_PERFCTR_L1_DCACHE_PREF_MISS		0xEB
#define ARMV8_THUNDER_PERFCTR_L1_ICACHE_PREF_ACCESS		0xEC
#define ARMV8_THUNDER_PERFCTR_L1_ICACHE_PREF_MISS		0xED

/* PMUv3 HW events mapping. */
static const unsigned armv8_pmuv3_perf_map[PERF_COUNT_HW_MAX] = {
	PERF_MAP_ALL_UNSUPPORTED,
	[PERF_COUNT_HW_CPU_CYCLES]		= ARMV8_PMUV3_PERFCTR_CLOCK_CYCLES,
	[PERF_COUNT_HW_INSTRUCTIONS]		= ARMV8_PMUV3_PERFCTR_INSTR_EXECUTED,
	[PERF_COUNT_HW_CACHE_REFERENCES]	= ARMV8_PMUV3_PERFCTR_L1_DCACHE_ACCESS,
	[PERF_COUNT_HW_CACHE_MISSES]		= ARMV8_PMUV3_PERFCTR_L1_DCACHE_REFILL,
	[PERF_COUNT_HW_BRANCH_MISSES]		= ARMV8_PMUV3_PERFCTR_PC_BRANCH_MIS_PRED,
};

/* ARM Cortex-A53 HW events mapping. */
static const unsigned armv8_a53_perf_map[PERF_COUNT_HW_MAX] = {
	PERF_MAP_ALL_UNSUPPORTED,
	[PERF_COUNT_HW_CPU_CYCLES]		= ARMV8_PMUV3_PERFCTR_CLOCK_CYCLES,
	[PERF_COUNT_HW_INSTRUCTIONS]		= ARMV8_PMUV3_PERFCTR_INSTR_EXECUTED,
	[PERF_COUNT_HW_CACHE_REFERENCES]	= ARMV8_PMUV3_PERFCTR_L1_DCACHE_ACCESS,
	[PERF_COUNT_HW_CACHE_MISSES]		= ARMV8_PMUV3_PERFCTR_L1_DCACHE_REFILL,
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS]	= ARMV8_PMUV3_PERFCTR_PC_WRITE,
	[PERF_COUNT_HW_BRANCH_MISSES]		= ARMV8_PMUV3_PERFCTR_PC_BRANCH_MIS_PRED,
	[PERF_COUNT_HW_BUS_CYCLES]		= ARMV8_PMUV3_PERFCTR_BUS_CYCLES,
};

/* ARM Cortex-A57 and Cortex-A72 events mapping. */
static const unsigned armv8_a57_perf_map[PERF_COUNT_HW_MAX] = {
	PERF_MAP_ALL_UNSUPPORTED,
	[PERF_COUNT_HW_CPU_CYCLES]		= ARMV8_PMUV3_PERFCTR_CLOCK_CYCLES,
	[PERF_COUNT_HW_INSTRUCTIONS]		= ARMV8_PMUV3_PERFCTR_INSTR_EXECUTED,
	[PERF_COUNT_HW_CACHE_REFERENCES]	= ARMV8_PMUV3_PERFCTR_L1_DCACHE_ACCESS,
	[PERF_COUNT_HW_CACHE_MISSES]		= ARMV8_PMUV3_PERFCTR_L1_DCACHE_REFILL,
	[PERF_COUNT_HW_BRANCH_MISSES]		= ARMV8_PMUV3_PERFCTR_PC_BRANCH_MIS_PRED,
	[PERF_COUNT_HW_BUS_CYCLES]		= ARMV8_PMUV3_PERFCTR_BUS_CYCLES,
};

static const unsigned armv8_thunder_perf_map[PERF_COUNT_HW_MAX] = {
	PERF_MAP_ALL_UNSUPPORTED,
	[PERF_COUNT_HW_CPU_CYCLES]		= ARMV8_PMUV3_PERFCTR_CLOCK_CYCLES,
	[PERF_COUNT_HW_INSTRUCTIONS]		= ARMV8_PMUV3_PERFCTR_INSTR_EXECUTED,
	[PERF_COUNT_HW_CACHE_REFERENCES]	= ARMV8_PMUV3_PERFCTR_L1_DCACHE_ACCESS,
	[PERF_COUNT_HW_CACHE_MISSES]		= ARMV8_PMUV3_PERFCTR_L1_DCACHE_REFILL,
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS]	= ARMV8_PMUV3_PERFCTR_PC_WRITE,
	[PERF_COUNT_HW_BRANCH_MISSES]		= ARMV8_PMUV3_PERFCTR_PC_BRANCH_MIS_PRED,
	[PERF_COUNT_HW_STALLED_CYCLES_FRONTEND] = ARMV8_PMUV3_PERFCTR_STALL_FRONTEND,
	[PERF_COUNT_HW_STALLED_CYCLES_BACKEND]	= ARMV8_PMUV3_PERFCTR_STALL_BACKEND,
};

static const unsigned armv8_pmuv3_perf_cache_map[PERF_COUNT_HW_CACHE_MAX]
						[PERF_COUNT_HW_CACHE_OP_MAX]
						[PERF_COUNT_HW_CACHE_RESULT_MAX] = {
	PERF_CACHE_MAP_ALL_UNSUPPORTED,

	[C(L1D)][C(OP_READ)][C(RESULT_ACCESS)]	= ARMV8_PMUV3_PERFCTR_L1_DCACHE_ACCESS,
	[C(L1D)][C(OP_READ)][C(RESULT_MISS)]	= ARMV8_PMUV3_PERFCTR_L1_DCACHE_REFILL,
	[C(L1D)][C(OP_WRITE)][C(RESULT_ACCESS)]	= ARMV8_PMUV3_PERFCTR_L1_DCACHE_ACCESS,
	[C(L1D)][C(OP_WRITE)][C(RESULT_MISS)]	= ARMV8_PMUV3_PERFCTR_L1_DCACHE_REFILL,

	[C(BPU)][C(OP_READ)][C(RESULT_ACCESS)]	= ARMV8_PMUV3_PERFCTR_PC_BRANCH_PRED,
	[C(BPU)][C(OP_READ)][C(RESULT_MISS)]	= ARMV8_PMUV3_PERFCTR_PC_BRANCH_MIS_PRED,
	[C(BPU)][C(OP_WRITE)][C(RESULT_ACCESS)]	= ARMV8_PMUV3_PERFCTR_PC_BRANCH_PRED,
	[C(BPU)][C(OP_WRITE)][C(RESULT_MISS)]	= ARMV8_PMUV3_PERFCTR_PC_BRANCH_MIS_PRED,
};

static const unsigned armv8_a53_perf_cache_map[PERF_COUNT_HW_CACHE_MAX]
					      [PERF_COUNT_HW_CACHE_OP_MAX]
					      [PERF_COUNT_HW_CACHE_RESULT_MAX] = {
	PERF_CACHE_MAP_ALL_UNSUPPORTED,

	[C(L1D)][C(OP_READ)][C(RESULT_ACCESS)]	= ARMV8_PMUV3_PERFCTR_L1_DCACHE_ACCESS,
	[C(L1D)][C(OP_READ)][C(RESULT_MISS)]	= ARMV8_PMUV3_PERFCTR_L1_DCACHE_REFILL,
	[C(L1D)][C(OP_WRITE)][C(RESULT_ACCESS)]	= ARMV8_PMUV3_PERFCTR_L1_DCACHE_ACCESS,
	[C(L1D)][C(OP_WRITE)][C(RESULT_MISS)]	= ARMV8_PMUV3_PERFCTR_L1_DCACHE_REFILL,
	[C(L1D)][C(OP_PREFETCH)][C(RESULT_MISS)] = ARMV8_A53_PERFCTR_PREFETCH_LINEFILL,

	[C(L1I)][C(OP_READ)][C(RESULT_ACCESS)]	= ARMV8_PMUV3_PERFCTR_L1_ICACHE_ACCESS,
	[C(L1I)][C(OP_READ)][C(RESULT_MISS)]	= ARMV8_PMUV3_PERFCTR_L1_ICACHE_REFILL,

	[C(ITLB)][C(OP_READ)][C(RESULT_MISS)]	= ARMV8_PMUV3_PERFCTR_ITLB_REFILL,

	[C(BPU)][C(OP_READ)][C(RESULT_ACCESS)]	= ARMV8_PMUV3_PERFCTR_PC_BRANCH_PRED,
	[C(BPU)][C(OP_READ)][C(RESULT_MISS)]	= ARMV8_PMUV3_PERFCTR_PC_BRANCH_MIS_PRED,
	[C(BPU)][C(OP_WRITE)][C(RESULT_ACCESS)]	= ARMV8_PMUV3_PERFCTR_PC_BRANCH_PRED,
	[C(BPU)][C(OP_WRITE)][C(RESULT_MISS)]	= ARMV8_PMUV3_PERFCTR_PC_BRANCH_MIS_PRED,
};

static const unsigned armv8_a57_perf_cache_map[PERF_COUNT_HW_CACHE_MAX]
					      [PERF_COUNT_HW_CACHE_OP_MAX]
					      [PERF_COUNT_HW_CACHE_RESULT_MAX] = {
	PERF_CACHE_MAP_ALL_UNSUPPORTED,

	[C(L1D)][C(OP_READ)][C(RESULT_ACCESS)]	= ARMV8_IMPDEF_PERFCTR_L1_DCACHE_ACCESS_LD,
	[C(L1D)][C(OP_READ)][C(RESULT_MISS)]	= ARMV8_IMPDEF_PERFCTR_L1_DCACHE_REFILL_LD,
	[C(L1D)][C(OP_WRITE)][C(RESULT_ACCESS)]	= ARMV8_IMPDEF_PERFCTR_L1_DCACHE_ACCESS_ST,
	[C(L1D)][C(OP_WRITE)][C(RESULT_MISS)]	= ARMV8_IMPDEF_PERFCTR_L1_DCACHE_REFILL_ST,

	[C(L1I)][C(OP_READ)][C(RESULT_ACCESS)]	= ARMV8_PMUV3_PERFCTR_L1_ICACHE_ACCESS,
	[C(L1I)][C(OP_READ)][C(RESULT_MISS)]	= ARMV8_PMUV3_PERFCTR_L1_ICACHE_REFILL,

	[C(DTLB)][C(OP_READ)][C(RESULT_MISS)]	= ARMV8_IMPDEF_PERFCTR_DTLB_REFILL_LD,
	[C(DTLB)][C(OP_WRITE)][C(RESULT_MISS)]	= ARMV8_IMPDEF_PERFCTR_DTLB_REFILL_ST,

	[C(ITLB)][C(OP_READ)][C(RESULT_MISS)]	= ARMV8_PMUV3_PERFCTR_ITLB_REFILL,

	[C(BPU)][C(OP_READ)][C(RESULT_ACCESS)]	= ARMV8_PMUV3_PERFCTR_PC_BRANCH_PRED,
	[C(BPU)][C(OP_READ)][C(RESULT_MISS)]	= ARMV8_PMUV3_PERFCTR_PC_BRANCH_MIS_PRED,
	[C(BPU)][C(OP_WRITE)][C(RESULT_ACCESS)]	= ARMV8_PMUV3_PERFCTR_PC_BRANCH_PRED,
	[C(BPU)][C(OP_WRITE)][C(RESULT_MISS)]	= ARMV8_PMUV3_PERFCTR_PC_BRANCH_MIS_PRED,
};

static const unsigned armv8_thunder_perf_cache_map[PERF_COUNT_HW_CACHE_MAX]
						   [PERF_COUNT_HW_CACHE_OP_MAX]
						   [PERF_COUNT_HW_CACHE_RESULT_MAX] = {
	PERF_CACHE_MAP_ALL_UNSUPPORTED,

	[C(L1D)][C(OP_READ)][C(RESULT_ACCESS)]	= ARMV8_IMPDEF_PERFCTR_L1_DCACHE_ACCESS_LD,
	[C(L1D)][C(OP_READ)][C(RESULT_MISS)]	= ARMV8_IMPDEF_PERFCTR_L1_DCACHE_REFILL_LD,
	[C(L1D)][C(OP_WRITE)][C(RESULT_ACCESS)]	= ARMV8_IMPDEF_PERFCTR_L1_DCACHE_ACCESS_ST,
	[C(L1D)][C(OP_WRITE)][C(RESULT_MISS)]	= ARMV8_THUNDER_PERFCTR_L1_DCACHE_MISS_ST,
	[C(L1D)][C(OP_PREFETCH)][C(RESULT_ACCESS)] = ARMV8_THUNDER_PERFCTR_L1_DCACHE_PREF_ACCESS,
	[C(L1D)][C(OP_PREFETCH)][C(RESULT_MISS)] = ARMV8_THUNDER_PERFCTR_L1_DCACHE_PREF_MISS,

	[C(L1I)][C(OP_READ)][C(RESULT_ACCESS)]	= ARMV8_PMUV3_PERFCTR_L1_ICACHE_ACCESS,
	[C(L1I)][C(OP_READ)][C(RESULT_MISS)]	= ARMV8_PMUV3_PERFCTR_L1_ICACHE_REFILL,
	[C(L1I)][C(OP_PREFETCH)][C(RESULT_ACCESS)] = ARMV8_THUNDER_PERFCTR_L1_ICACHE_PREF_ACCESS,
	[C(L1I)][C(OP_PREFETCH)][C(RESULT_MISS)] = ARMV8_THUNDER_PERFCTR_L1_ICACHE_PREF_MISS,

	[C(DTLB)][C(OP_READ)][C(RESULT_ACCESS)]	= ARMV8_IMPDEF_PERFCTR_DTLB_ACCESS_LD,
	[C(DTLB)][C(OP_READ)][C(RESULT_MISS)]	= ARMV8_IMPDEF_PERFCTR_DTLB_REFILL_LD,
	[C(DTLB)][C(OP_WRITE)][C(RESULT_ACCESS)] = ARMV8_IMPDEF_PERFCTR_DTLB_ACCESS_ST,
	[C(DTLB)][C(OP_WRITE)][C(RESULT_MISS)]	= ARMV8_IMPDEF_PERFCTR_DTLB_REFILL_ST,

	[C(ITLB)][C(OP_READ)][C(RESULT_MISS)]	= ARMV8_PMUV3_PERFCTR_ITLB_REFILL,

	[C(BPU)][C(OP_READ)][C(RESULT_ACCESS)]	= ARMV8_PMUV3_PERFCTR_PC_BRANCH_PRED,
	[C(BPU)][C(OP_READ)][C(RESULT_MISS)]	= ARMV8_PMUV3_PERFCTR_PC_BRANCH_MIS_PRED,
	[C(BPU)][C(OP_WRITE)][C(RESULT_ACCESS)]	= ARMV8_PMUV3_PERFCTR_PC_BRANCH_PRED,
	[C(BPU)][C(OP_WRITE)][C(RESULT_MISS)]	= ARMV8_PMUV3_PERFCTR_PC_BRANCH_MIS_PRED,
};

#define ARMV8_EVENT_ATTR_RESOLVE(m) #m
#define ARMV8_EVENT_ATTR(name, config) \
	PMU_EVENT_ATTR_STRING(name, armv8_event_attr_##name, \
			      "event=" ARMV8_EVENT_ATTR_RESOLVE(config))

ARMV8_EVENT_ATTR(sw_incr, ARMV8_PMUV3_PERFCTR_PMNC_SW_INCR);
ARMV8_EVENT_ATTR(l1i_cache_refill, ARMV8_PMUV3_PERFCTR_L1_ICACHE_REFILL);
ARMV8_EVENT_ATTR(l1i_tlb_refill, ARMV8_PMUV3_PERFCTR_ITLB_REFILL);
ARMV8_EVENT_ATTR(l1d_cache_refill, ARMV8_PMUV3_PERFCTR_L1_DCACHE_REFILL);
ARMV8_EVENT_ATTR(l1d_cache, ARMV8_PMUV3_PERFCTR_L1_DCACHE_ACCESS);
ARMV8_EVENT_ATTR(l1d_tlb_refill, ARMV8_PMUV3_PERFCTR_DTLB_REFILL);
ARMV8_EVENT_ATTR(ld_retired, ARMV8_PMUV3_PERFCTR_MEM_READ);
ARMV8_EVENT_ATTR(st_retired, ARMV8_PMUV3_PERFCTR_MEM_WRITE);
ARMV8_EVENT_ATTR(inst_retired, ARMV8_PMUV3_PERFCTR_INSTR_EXECUTED);
ARMV8_EVENT_ATTR(exc_taken, ARMV8_PMUV3_PERFCTR_EXC_TAKEN);
ARMV8_EVENT_ATTR(exc_return, ARMV8_PMUV3_PERFCTR_EXC_EXECUTED);
ARMV8_EVENT_ATTR(cid_write_retired, ARMV8_PMUV3_PERFCTR_CID_WRITE);
ARMV8_EVENT_ATTR(pc_write_retired, ARMV8_PMUV3_PERFCTR_PC_WRITE);
ARMV8_EVENT_ATTR(br_immed_retired, ARMV8_PMUV3_PERFCTR_PC_IMM_BRANCH);
ARMV8_EVENT_ATTR(br_return_retired, ARMV8_PMUV3_PERFCTR_PC_PROC_RETURN);
ARMV8_EVENT_ATTR(unaligned_ldst_retired, ARMV8_PMUV3_PERFCTR_MEM_UNALIGNED_ACCESS);
ARMV8_EVENT_ATTR(br_mis_pred, ARMV8_PMUV3_PERFCTR_PC_BRANCH_MIS_PRED);
ARMV8_EVENT_ATTR(cpu_cycles, ARMV8_PMUV3_PERFCTR_CLOCK_CYCLES);
ARMV8_EVENT_ATTR(br_pred, ARMV8_PMUV3_PERFCTR_PC_BRANCH_PRED);
ARMV8_EVENT_ATTR(mem_access, ARMV8_PMUV3_PERFCTR_MEM_ACCESS);
ARMV8_EVENT_ATTR(l1i_cache, ARMV8_PMUV3_PERFCTR_L1_ICACHE_ACCESS);
ARMV8_EVENT_ATTR(l1d_cache_wb, ARMV8_PMUV3_PERFCTR_L1_DCACHE_WB);
ARMV8_EVENT_ATTR(l2d_cache, ARMV8_PMUV3_PERFCTR_L2_CACHE_ACCESS);
ARMV8_EVENT_ATTR(l2d_cache_refill, ARMV8_PMUV3_PERFCTR_L2_CACHE_REFILL);
ARMV8_EVENT_ATTR(l2d_cache_wb, ARMV8_PMUV3_PERFCTR_L2_CACHE_WB);
ARMV8_EVENT_ATTR(bus_access, ARMV8_PMUV3_PERFCTR_BUS_ACCESS);
ARMV8_EVENT_ATTR(memory_error, ARMV8_PMUV3_PERFCTR_MEM_ERROR);
ARMV8_EVENT_ATTR(inst_spec, ARMV8_PMUV3_PERFCTR_OP_SPEC);
ARMV8_EVENT_ATTR(ttbr_write_retired, ARMV8_PMUV3_PERFCTR_TTBR_WRITE);
ARMV8_EVENT_ATTR(bus_cycles, ARMV8_PMUV3_PERFCTR_BUS_CYCLES);
ARMV8_EVENT_ATTR(chain, ARMV8_PMUV3_PERFCTR_CHAIN);
ARMV8_EVENT_ATTR(l1d_cache_allocate, ARMV8_PMUV3_PERFCTR_L1D_CACHE_ALLOCATE);
ARMV8_EVENT_ATTR(l2d_cache_allocate, ARMV8_PMUV3_PERFCTR_L2D_CACHE_ALLOCATE);
ARMV8_EVENT_ATTR(br_retired, ARMV8_PMUV3_PERFCTR_BR_RETIRED);
ARMV8_EVENT_ATTR(br_mis_pred_retired, ARMV8_PMUV3_PERFCTR_BR_MIS_PRED_RETIRED);
ARMV8_EVENT_ATTR(stall_frontend, ARMV8_PMUV3_PERFCTR_STALL_FRONTEND);
ARMV8_EVENT_ATTR(stall_backend, ARMV8_PMUV3_PERFCTR_STALL_BACKEND);
ARMV8_EVENT_ATTR(l1d_tlb, ARMV8_PMUV3_PERFCTR_L1D_TLB);
ARMV8_EVENT_ATTR(l1i_tlb, ARMV8_PMUV3_PERFCTR_L1I_TLB);
ARMV8_EVENT_ATTR(l2i_cache, ARMV8_PMUV3_PERFCTR_L2I_CACHE);
ARMV8_EVENT_ATTR(l2i_cache_refill, ARMV8_PMUV3_PERFCTR_L2I_CACHE_REFILL);
ARMV8_EVENT_ATTR(l3d_cache_allocate, ARMV8_PMUV3_PERFCTR_L3D_CACHE_ALLOCATE);
ARMV8_EVENT_ATTR(l3d_cache_refill, ARMV8_PMUV3_PERFCTR_L3D_CACHE_REFILL);
ARMV8_EVENT_ATTR(l3d_cache, ARMV8_PMUV3_PERFCTR_L3D_CACHE);
ARMV8_EVENT_ATTR(l3d_cache_wb, ARMV8_PMUV3_PERFCTR_L3D_CACHE_WB);
ARMV8_EVENT_ATTR(l2d_tlb_refill, ARMV8_PMUV3_PERFCTR_L2D_TLB_REFILL);
ARMV8_EVENT_ATTR(l21_tlb_refill, ARMV8_PMUV3_PERFCTR_L21_TLB_REFILL);
ARMV8_EVENT_ATTR(l2d_tlb, ARMV8_PMUV3_PERFCTR_L2D_TLB);
ARMV8_EVENT_ATTR(l21_tlb, ARMV8_PMUV3_PERFCTR_L21_TLB);

static struct attribute *armv8_pmuv3_event_attrs[] = {
	&armv8_event_attr_sw_incr.attr.attr,
	&armv8_event_attr_l1i_cache_refill.attr.attr,
	&armv8_event_attr_l1i_tlb_refill.attr.attr,
	&armv8_event_attr_l1d_cache_refill.attr.attr,
	&armv8_event_attr_l1d_cache.attr.attr,
	&armv8_event_attr_l1d_tlb_refill.attr.attr,
	&armv8_event_attr_ld_retired.attr.attr,
	&armv8_event_attr_st_retired.attr.attr,
	&armv8_event_attr_inst_retired.attr.attr,
	&armv8_event_attr_exc_taken.attr.attr,
	&armv8_event_attr_exc_return.attr.attr,
	&armv8_event_attr_cid_write_retired.attr.attr,
	&armv8_event_attr_pc_write_retired.attr.attr,
	&armv8_event_attr_br_immed_retired.attr.attr,
	&armv8_event_attr_br_return_retired.attr.attr,
	&armv8_event_attr_unaligned_ldst_retired.attr.attr,
	&armv8_event_attr_br_mis_pred.attr.attr,
	&armv8_event_attr_cpu_cycles.attr.attr,
	&armv8_event_attr_br_pred.attr.attr,
	&armv8_event_attr_mem_access.attr.attr,
	&armv8_event_attr_l1i_cache.attr.attr,
	&armv8_event_attr_l1d_cache_wb.attr.attr,
	&armv8_event_attr_l2d_cache.attr.attr,
	&armv8_event_attr_l2d_cache_refill.attr.attr,
	&armv8_event_attr_l2d_cache_wb.attr.attr,
	&armv8_event_attr_bus_access.attr.attr,
	&armv8_event_attr_memory_error.attr.attr,
	&armv8_event_attr_inst_spec.attr.attr,
	&armv8_event_attr_ttbr_write_retired.attr.attr,
	&armv8_event_attr_bus_cycles.attr.attr,
	&armv8_event_attr_chain.attr.attr,
	&armv8_event_attr_l1d_cache_allocate.attr.attr,
	&armv8_event_attr_l2d_cache_allocate.attr.attr,
	&armv8_event_attr_br_retired.attr.attr,
	&armv8_event_attr_br_mis_pred_retired.attr.attr,
	&armv8_event_attr_stall_frontend.attr.attr,
	&armv8_event_attr_stall_backend.attr.attr,
	&armv8_event_attr_l1d_tlb.attr.attr,
	&armv8_event_attr_l1i_tlb.attr.attr,
	&armv8_event_attr_l2i_cache.attr.attr,
	&armv8_event_attr_l2i_cache_refill.attr.attr,
	&armv8_event_attr_l3d_cache_allocate.attr.attr,
	&armv8_event_attr_l3d_cache_refill.attr.attr,
	&armv8_event_attr_l3d_cache.attr.attr,
	&armv8_event_attr_l3d_cache_wb.attr.attr,
	&armv8_event_attr_l2d_tlb_refill.attr.attr,
	&armv8_event_attr_l21_tlb_refill.attr.attr,
	&armv8_event_attr_l2d_tlb.attr.attr,
	&armv8_event_attr_l21_tlb.attr.attr,
	NULL,
};

static struct attribute_group armv8_pmuv3_events_attr_group = {
	.name = "events",
	.attrs = armv8_pmuv3_event_attrs,
};

PMU_FORMAT_ATTR(event, "config:0-9");

static struct attribute *armv8_pmuv3_format_attrs[] = {
	&format_attr_event.attr,
	NULL,
};

static struct attribute_group armv8_pmuv3_format_attr_group = {
	.name = "format",
	.attrs = armv8_pmuv3_format_attrs,
};

static const struct attribute_group *armv8_pmuv3_attr_groups[] = {
	&armv8_pmuv3_events_attr_group,
	&armv8_pmuv3_format_attr_group,
	NULL,
};

/*
 * Perf Events' indices
 */
#define	ARMV8_IDX_CYCLE_COUNTER	0
#define	ARMV8_IDX_COUNTER0	1
#define	ARMV8_IDX_COUNTER_LAST(cpu_pmu) \
	(ARMV8_IDX_CYCLE_COUNTER + cpu_pmu->num_events - 1)

/*
 * ARMv8 low level PMU access
 */

/*
 * Perf Event to low level counters mapping
 */
#define	ARMV8_IDX_TO_COUNTER(x)	\
	(((x) - ARMV8_IDX_COUNTER0) & ARMV8_PMU_COUNTER_MASK)

static inline u32 armv8pmu_pmcr_read(void)
{
	u32 val;
	asm volatile("mrs %0, pmcr_el0" : "=r" (val));
	return val;
}

static inline void armv8pmu_pmcr_write(u32 val)
{
	val &= ARMV8_PMU_PMCR_MASK;
	isb();
	asm volatile("msr pmcr_el0, %0" :: "r" (val));
}

static inline int armv8pmu_has_overflowed(u32 pmovsr)
{
	return pmovsr & ARMV8_PMU_OVERFLOWED_MASK;
}

static inline int armv8pmu_counter_valid(struct arm_pmu *cpu_pmu, int idx)
{
	return idx >= ARMV8_IDX_CYCLE_COUNTER &&
		idx <= ARMV8_IDX_COUNTER_LAST(cpu_pmu);
}

static inline int armv8pmu_counter_has_overflowed(u32 pmnc, int idx)
{
	return pmnc & BIT(ARMV8_IDX_TO_COUNTER(idx));
}

static inline int armv8pmu_select_counter(int idx)
{
	u32 counter = ARMV8_IDX_TO_COUNTER(idx);
	asm volatile("msr pmselr_el0, %0" :: "r" (counter));
	isb();

	return idx;
}

static inline u32 armv8pmu_read_counter(struct perf_event *event)
{
	struct arm_pmu *cpu_pmu = to_arm_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	int idx = hwc->idx;
	u32 value = 0;

	if (!armv8pmu_counter_valid(cpu_pmu, idx))
		pr_err("CPU%u reading wrong counter %d\n",
			smp_processor_id(), idx);
	else if (idx == ARMV8_IDX_CYCLE_COUNTER)
		asm volatile("mrs %0, pmccntr_el0" : "=r" (value));
	else if (armv8pmu_select_counter(idx) == idx)
		asm volatile("mrs %0, pmxevcntr_el0" : "=r" (value));

	return value;
}

static inline void armv8pmu_write_counter(struct perf_event *event, u32 value)
{
	struct arm_pmu *cpu_pmu = to_arm_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	int idx = hwc->idx;

	if (!armv8pmu_counter_valid(cpu_pmu, idx))
		pr_err("CPU%u writing wrong counter %d\n",
			smp_processor_id(), idx);
	else if (idx == ARMV8_IDX_CYCLE_COUNTER) {
		/*
		 * Set the upper 32bits as this is a 64bit counter but we only
		 * count using the lower 32bits and we want an interrupt when
		 * it overflows.
		 */
		u64 value64 = 0xffffffff00000000ULL | value;

		asm volatile("msr pmccntr_el0, %0" :: "r" (value64));
	} else if (armv8pmu_select_counter(idx) == idx)
		asm volatile("msr pmxevcntr_el0, %0" :: "r" (value));
}

static inline void armv8pmu_write_evtype(int idx, u32 val)
{
	if (armv8pmu_select_counter(idx) == idx) {
		val &= ARMV8_PMU_EVTYPE_MASK;
		asm volatile("msr pmxevtyper_el0, %0" :: "r" (val));
	}
}

static inline int armv8pmu_enable_counter(int idx)
{
	u32 counter = ARMV8_IDX_TO_COUNTER(idx);
	asm volatile("msr pmcntenset_el0, %0" :: "r" (BIT(counter)));
	return idx;
}

static inline int armv8pmu_disable_counter(int idx)
{
	u32 counter = ARMV8_IDX_TO_COUNTER(idx);
	asm volatile("msr pmcntenclr_el0, %0" :: "r" (BIT(counter)));
	return idx;
}

static inline int armv8pmu_enable_intens(int idx)
{
	u32 counter = ARMV8_IDX_TO_COUNTER(idx);
	asm volatile("msr pmintenset_el1, %0" :: "r" (BIT(counter)));
	return idx;
}

static inline int armv8pmu_disable_intens(int idx)
{
	u32 counter = ARMV8_IDX_TO_COUNTER(idx);
	asm volatile("msr pmintenclr_el1, %0" :: "r" (BIT(counter)));
	isb();
	/* Clear the overflow flag in case an interrupt is pending. */
	asm volatile("msr pmovsclr_el0, %0" :: "r" (BIT(counter)));
	isb();

	return idx;
}

static inline u32 armv8pmu_getreset_flags(void)
{
	u32 value;

	/* Read */
	asm volatile("mrs %0, pmovsclr_el0" : "=r" (value));

	/* Write to clear flags */
	value &= ARMV8_PMU_OVSR_MASK;
	asm volatile("msr pmovsclr_el0, %0" :: "r" (value));

	return value;
}

static void armv8pmu_enable_event(struct perf_event *event)
{
	unsigned long flags;
	struct hw_perf_event *hwc = &event->hw;
	struct arm_pmu *cpu_pmu = to_arm_pmu(event->pmu);
	struct pmu_hw_events *events = this_cpu_ptr(cpu_pmu->hw_events);
	int idx = hwc->idx;

	/*
	 * Enable counter and interrupt, and set the counter to count
	 * the event that we're interested in.
	 */
	raw_spin_lock_irqsave(&events->pmu_lock, flags);

	/*
	 * Disable counter
	 */
	armv8pmu_disable_counter(idx);

	/*
	 * Set event (if destined for PMNx counters).
	 */
	armv8pmu_write_evtype(idx, hwc->config_base);

	/*
	 * Enable interrupt for this counter
	 */
	armv8pmu_enable_intens(idx);

	/*
	 * Enable counter
	 */
	armv8pmu_enable_counter(idx);

	raw_spin_unlock_irqrestore(&events->pmu_lock, flags);
}

static void armv8pmu_disable_event(struct perf_event *event)
{
	unsigned long flags;
	struct hw_perf_event *hwc = &event->hw;
	struct arm_pmu *cpu_pmu = to_arm_pmu(event->pmu);
	struct pmu_hw_events *events = this_cpu_ptr(cpu_pmu->hw_events);
	int idx = hwc->idx;

	/*
	 * Disable counter and interrupt
	 */
	raw_spin_lock_irqsave(&events->pmu_lock, flags);

	/*
	 * Disable counter
	 */
	armv8pmu_disable_counter(idx);

	/*
	 * Disable interrupt for this counter
	 */
	armv8pmu_disable_intens(idx);

	raw_spin_unlock_irqrestore(&events->pmu_lock, flags);
}

static irqreturn_t armv8pmu_handle_irq(int irq_num, void *dev)
{
	u32 pmovsr;
	struct perf_sample_data data;
	struct arm_pmu *cpu_pmu = (struct arm_pmu *)dev;
	struct pmu_hw_events *cpuc = this_cpu_ptr(cpu_pmu->hw_events);
	struct pt_regs *regs;
	int idx;

	/*
	 * Get and reset the IRQ flags
	 */
	pmovsr = armv8pmu_getreset_flags();

	/*
	 * Did an overflow occur?
	 */
	if (!armv8pmu_has_overflowed(pmovsr))
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
		if (!armv8pmu_counter_has_overflowed(pmovsr, idx))
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

static void armv8pmu_start(struct arm_pmu *cpu_pmu)
{
	unsigned long flags;
	struct pmu_hw_events *events = this_cpu_ptr(cpu_pmu->hw_events);

	raw_spin_lock_irqsave(&events->pmu_lock, flags);
	/* Enable all counters */
	armv8pmu_pmcr_write(armv8pmu_pmcr_read() | ARMV8_PMU_PMCR_E);
	raw_spin_unlock_irqrestore(&events->pmu_lock, flags);
}

static void armv8pmu_stop(struct arm_pmu *cpu_pmu)
{
	unsigned long flags;
	struct pmu_hw_events *events = this_cpu_ptr(cpu_pmu->hw_events);

	raw_spin_lock_irqsave(&events->pmu_lock, flags);
	/* Disable all counters */
	armv8pmu_pmcr_write(armv8pmu_pmcr_read() & ~ARMV8_PMU_PMCR_E);
	raw_spin_unlock_irqrestore(&events->pmu_lock, flags);
}

static int armv8pmu_get_event_idx(struct pmu_hw_events *cpuc,
				  struct perf_event *event)
{
	int idx;
	struct arm_pmu *cpu_pmu = to_arm_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	unsigned long evtype = hwc->config_base & ARMV8_PMU_EVTYPE_EVENT;

	/* Always place a cycle counter into the cycle counter. */
	if (evtype == ARMV8_PMUV3_PERFCTR_CLOCK_CYCLES) {
		if (test_and_set_bit(ARMV8_IDX_CYCLE_COUNTER, cpuc->used_mask))
			return -EAGAIN;

		return ARMV8_IDX_CYCLE_COUNTER;
	}

	/*
	 * For anything other than a cycle counter, try and use
	 * the events counters
	 */
	for (idx = ARMV8_IDX_COUNTER0; idx < cpu_pmu->num_events; ++idx) {
		if (!test_and_set_bit(idx, cpuc->used_mask))
			return idx;
	}

	/* The counters are all in use. */
	return -EAGAIN;
}

/*
 * Add an event filter to a given event. This will only work for PMUv2 PMUs.
 */
static int armv8pmu_set_event_filter(struct hw_perf_event *event,
				     struct perf_event_attr *attr)
{
	unsigned long config_base = 0;

	if (attr->exclude_idle)
		return -EPERM;
	if (is_kernel_in_hyp_mode() &&
	    attr->exclude_kernel != attr->exclude_hv)
		return -EINVAL;
	if (attr->exclude_user)
		config_base |= ARMV8_PMU_EXCLUDE_EL0;
	if (!is_kernel_in_hyp_mode() && attr->exclude_kernel)
		config_base |= ARMV8_PMU_EXCLUDE_EL1;
	if (!attr->exclude_hv)
		config_base |= ARMV8_PMU_INCLUDE_EL2;

	/*
	 * Install the filter into config_base as this is used to
	 * construct the event type.
	 */
	event->config_base = config_base;

	return 0;
}

static void armv8pmu_reset(void *info)
{
	struct arm_pmu *cpu_pmu = (struct arm_pmu *)info;
	u32 idx, nb_cnt = cpu_pmu->num_events;

	/* The counter and interrupt enable registers are unknown at reset. */
	for (idx = ARMV8_IDX_CYCLE_COUNTER; idx < nb_cnt; ++idx) {
		armv8pmu_disable_counter(idx);
		armv8pmu_disable_intens(idx);
	}

	/*
	 * Initialize & Reset PMNC. Request overflow interrupt for
	 * 64 bit cycle counter but cheat in armv8pmu_write_counter().
	 */
	armv8pmu_pmcr_write(ARMV8_PMU_PMCR_P | ARMV8_PMU_PMCR_C |
			    ARMV8_PMU_PMCR_LC);
}

static int armv8_pmuv3_map_event(struct perf_event *event)
{
	return armpmu_map_event(event, &armv8_pmuv3_perf_map,
				&armv8_pmuv3_perf_cache_map,
				ARMV8_PMU_EVTYPE_EVENT);
}

static int armv8_a53_map_event(struct perf_event *event)
{
	return armpmu_map_event(event, &armv8_a53_perf_map,
				&armv8_a53_perf_cache_map,
				ARMV8_PMU_EVTYPE_EVENT);
}

static int armv8_a57_map_event(struct perf_event *event)
{
	return armpmu_map_event(event, &armv8_a57_perf_map,
				&armv8_a57_perf_cache_map,
				ARMV8_PMU_EVTYPE_EVENT);
}

static int armv8_thunder_map_event(struct perf_event *event)
{
	return armpmu_map_event(event, &armv8_thunder_perf_map,
				&armv8_thunder_perf_cache_map,
				ARMV8_PMU_EVTYPE_EVENT);
}

static void armv8pmu_read_num_pmnc_events(void *info)
{
	int *nb_cnt = info;

	/* Read the nb of CNTx counters supported from PMNC */
	*nb_cnt = (armv8pmu_pmcr_read() >> ARMV8_PMU_PMCR_N_SHIFT) & ARMV8_PMU_PMCR_N_MASK;

	/* Add the CPU cycles counter */
	*nb_cnt += 1;
}

static int armv8pmu_probe_num_events(struct arm_pmu *arm_pmu)
{
	return smp_call_function_any(&arm_pmu->supported_cpus,
				    armv8pmu_read_num_pmnc_events,
				    &arm_pmu->num_events, 1);
}

static void armv8_pmu_init(struct arm_pmu *cpu_pmu)
{
	cpu_pmu->handle_irq		= armv8pmu_handle_irq,
	cpu_pmu->enable			= armv8pmu_enable_event,
	cpu_pmu->disable		= armv8pmu_disable_event,
	cpu_pmu->read_counter		= armv8pmu_read_counter,
	cpu_pmu->write_counter		= armv8pmu_write_counter,
	cpu_pmu->get_event_idx		= armv8pmu_get_event_idx,
	cpu_pmu->start			= armv8pmu_start,
	cpu_pmu->stop			= armv8pmu_stop,
	cpu_pmu->reset			= armv8pmu_reset,
	cpu_pmu->max_period		= (1LLU << 32) - 1,
	cpu_pmu->set_event_filter	= armv8pmu_set_event_filter;
}

static int armv8_pmuv3_init(struct arm_pmu *cpu_pmu)
{
	armv8_pmu_init(cpu_pmu);
	cpu_pmu->name			= "armv8_pmuv3";
	cpu_pmu->map_event		= armv8_pmuv3_map_event;
	return armv8pmu_probe_num_events(cpu_pmu);
}

static int armv8_a53_pmu_init(struct arm_pmu *cpu_pmu)
{
	armv8_pmu_init(cpu_pmu);
	cpu_pmu->name			= "armv8_cortex_a53";
	cpu_pmu->map_event		= armv8_a53_map_event;
	cpu_pmu->pmu.attr_groups	= armv8_pmuv3_attr_groups;
	return armv8pmu_probe_num_events(cpu_pmu);
}

static int armv8_a57_pmu_init(struct arm_pmu *cpu_pmu)
{
	armv8_pmu_init(cpu_pmu);
	cpu_pmu->name			= "armv8_cortex_a57";
	cpu_pmu->map_event		= armv8_a57_map_event;
	cpu_pmu->pmu.attr_groups	= armv8_pmuv3_attr_groups;
	return armv8pmu_probe_num_events(cpu_pmu);
}

static int armv8_a72_pmu_init(struct arm_pmu *cpu_pmu)
{
	armv8_pmu_init(cpu_pmu);
	cpu_pmu->name			= "armv8_cortex_a72";
	cpu_pmu->map_event		= armv8_a57_map_event;
	cpu_pmu->pmu.attr_groups	= armv8_pmuv3_attr_groups;
	return armv8pmu_probe_num_events(cpu_pmu);
}

static int armv8_thunder_pmu_init(struct arm_pmu *cpu_pmu)
{
	armv8_pmu_init(cpu_pmu);
	cpu_pmu->name			= "armv8_cavium_thunder";
	cpu_pmu->map_event		= armv8_thunder_map_event;
	cpu_pmu->pmu.attr_groups	= armv8_pmuv3_attr_groups;
	return armv8pmu_probe_num_events(cpu_pmu);
}

static const struct of_device_id armv8_pmu_of_device_ids[] = {
	{.compatible = "arm,armv8-pmuv3",	.data = armv8_pmuv3_init},
	{.compatible = "arm,cortex-a53-pmu",	.data = armv8_a53_pmu_init},
	{.compatible = "arm,cortex-a57-pmu",	.data = armv8_a57_pmu_init},
	{.compatible = "arm,cortex-a72-pmu",	.data = armv8_a72_pmu_init},
	{.compatible = "cavium,thunder-pmu",	.data = armv8_thunder_pmu_init},
	{},
};

static int armv8_pmu_device_probe(struct platform_device *pdev)
{
	return arm_pmu_device_probe(pdev, armv8_pmu_of_device_ids, NULL);
}

static struct platform_driver armv8_pmu_driver = {
	.driver		= {
		.name	= "armv8-pmu",
		.of_match_table = armv8_pmu_of_device_ids,
	},
	.probe		= armv8_pmu_device_probe,
};

static int __init register_armv8_pmu_driver(void)
{
	return platform_driver_register(&armv8_pmu_driver);
}
device_initcall(register_armv8_pmu_driver);
