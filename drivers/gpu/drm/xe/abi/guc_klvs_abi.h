/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef _ABI_GUC_KLVS_ABI_H
#define _ABI_GUC_KLVS_ABI_H

#include <linux/types.h>

/**
 * DOC: GuC KLV
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 | 31:16 | **KEY** - KLV key identifier                                 |
 *  |   |       |   - `GuC Self Config KLVs`_                                  |
 *  |   |       |   - `GuC VGT Policy KLVs`_                                   |
 *  |   |       |   - `GuC VF Configuration KLVs`_                             |
 *  |   |       |                                                              |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  15:0 | **LEN** - length of VALUE (in 32bit dwords)                  |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 |  31:0 | **VALUE** - actual value of the KLV (format depends on KEY)  |
 *  +---+-------+                                                              |
 *  |...|       |                                                              |
 *  +---+-------+                                                              |
 *  | n |  31:0 |                                                              |
 *  +---+-------+--------------------------------------------------------------+
 */

#define GUC_KLV_LEN_MIN				1u
#define GUC_KLV_0_KEY				(0xffffu << 16)
#define GUC_KLV_0_LEN				(0xffffu << 0)
#define GUC_KLV_n_VALUE				(0xffffffffu << 0)

/**
 * DOC: GuC Global Config KLVs
 *
 * `GuC KLV`_ keys available for use with HOST2GUC_SELF_CFG_.
 *
 * _`GUC_KLV_GLOBAL_CFG_GMD_ID` : 0x3000
 *      Refers to 32 bit architecture version as reported by the HW IP.
 *      This key is supported on MTL+ platforms only.
 *      Requires GuC ABI 1.2+.
 */

#define GUC_KLV_GLOBAL_CFG_GMD_ID_KEY			0x3000u
#define GUC_KLV_GLOBAL_CFG_GMD_ID_LEN			1u

/**
 * DOC: GuC Self Config KLVs
 *
 * `GuC KLV`_ keys available for use with HOST2GUC_SELF_CFG_.
 *
 * _`GUC_KLV_SELF_CFG_MEMIRQ_STATUS_ADDR` : 0x0900
 *      Refers to 64 bit Global Gfx address (in bytes) of memory based interrupts
 *      status vector for use by the GuC.
 *
 * _`GUC_KLV_SELF_CFG_MEMIRQ_SOURCE_ADDR` : 0x0901
 *      Refers to 64 bit Global Gfx address (in bytes) of memory based interrupts
 *      source vector for use by the GuC.
 *
 * _`GUC_KLV_SELF_CFG_H2G_CTB_ADDR` : 0x0902
 *      Refers to 64 bit Global Gfx address of H2G `CT Buffer`_.
 *      Should be above WOPCM address but below APIC base address for native mode.
 *
 * _`GUC_KLV_SELF_CFG_H2G_CTB_DESCRIPTOR_ADDR : 0x0903
 *      Refers to 64 bit Global Gfx address of H2G `CTB Descriptor`_.
 *      Should be above WOPCM address but below APIC base address for native mode.
 *
 * _`GUC_KLV_SELF_CFG_H2G_CTB_SIZE : 0x0904
 *      Refers to size of H2G `CT Buffer`_ in bytes.
 *      Should be a multiple of 4K.
 *
 * _`GUC_KLV_SELF_CFG_G2H_CTB_ADDR : 0x0905
 *      Refers to 64 bit Global Gfx address of G2H `CT Buffer`_.
 *      Should be above WOPCM address but below APIC base address for native mode.
 *
 * _GUC_KLV_SELF_CFG_G2H_CTB_DESCRIPTOR_ADDR : 0x0906
 *      Refers to 64 bit Global Gfx address of G2H `CTB Descriptor`_.
 *      Should be above WOPCM address but below APIC base address for native mode.
 *
 * _GUC_KLV_SELF_CFG_G2H_CTB_SIZE : 0x0907
 *      Refers to size of G2H `CT Buffer`_ in bytes.
 *      Should be a multiple of 4K.
 */

#define GUC_KLV_SELF_CFG_MEMIRQ_STATUS_ADDR_KEY		0x0900
#define GUC_KLV_SELF_CFG_MEMIRQ_STATUS_ADDR_LEN		2u

#define GUC_KLV_SELF_CFG_MEMIRQ_SOURCE_ADDR_KEY		0x0901
#define GUC_KLV_SELF_CFG_MEMIRQ_SOURCE_ADDR_LEN		2u

#define GUC_KLV_SELF_CFG_H2G_CTB_ADDR_KEY		0x0902
#define GUC_KLV_SELF_CFG_H2G_CTB_ADDR_LEN		2u

#define GUC_KLV_SELF_CFG_H2G_CTB_DESCRIPTOR_ADDR_KEY	0x0903
#define GUC_KLV_SELF_CFG_H2G_CTB_DESCRIPTOR_ADDR_LEN	2u

#define GUC_KLV_SELF_CFG_H2G_CTB_SIZE_KEY		0x0904
#define GUC_KLV_SELF_CFG_H2G_CTB_SIZE_LEN		1u

#define GUC_KLV_SELF_CFG_G2H_CTB_ADDR_KEY		0x0905
#define GUC_KLV_SELF_CFG_G2H_CTB_ADDR_LEN		2u

#define GUC_KLV_SELF_CFG_G2H_CTB_DESCRIPTOR_ADDR_KEY	0x0906
#define GUC_KLV_SELF_CFG_G2H_CTB_DESCRIPTOR_ADDR_LEN	2u

#define GUC_KLV_SELF_CFG_G2H_CTB_SIZE_KEY		0x0907
#define GUC_KLV_SELF_CFG_G2H_CTB_SIZE_LEN		1u

/*
 * Per context scheduling policy update keys.
 */
enum  {
	GUC_CONTEXT_POLICIES_KLV_ID_EXECUTION_QUANTUM			= 0x2001,
	GUC_CONTEXT_POLICIES_KLV_ID_PREEMPTION_TIMEOUT			= 0x2002,
	GUC_CONTEXT_POLICIES_KLV_ID_SCHEDULING_PRIORITY			= 0x2003,
	GUC_CONTEXT_POLICIES_KLV_ID_PREEMPT_TO_IDLE_ON_QUANTUM_EXPIRY	= 0x2004,
	GUC_CONTEXT_POLICIES_KLV_ID_SLPM_GT_FREQUENCY			= 0x2005,

	GUC_CONTEXT_POLICIES_KLV_NUM_IDS = 5,
};

/**
 * DOC: GuC VGT Policy KLVs
 *
 * `GuC KLV`_ keys available for use with PF2GUC_UPDATE_VGT_POLICY.
 *
 * _`GUC_KLV_VGT_POLICY_SCHED_IF_IDLE` : 0x8001
 *      This config sets whether strict scheduling is enabled whereby any VF
 *      that doesn’t have work to submit is still allocated a fixed execution
 *      time-slice to ensure active VFs execution is always consistent even
 *      during other VF reprovisiong / rebooting events. Changing this KLV
 *      impacts all VFs and takes effect on the next VF-Switch event.
 *
 *      :0: don't schedule idle (default)
 *      :1: schedule if idle
 *
 * _`GUC_KLV_VGT_POLICY_ADVERSE_SAMPLE_PERIOD` : 0x8002
 *      This config sets the sample period for tracking adverse event counters.
 *       A sample period is the period in millisecs during which events are counted.
 *       This is applicable for all the VFs.
 *
 *      :0: adverse events are not counted (default)
 *      :n: sample period in milliseconds
 *
 * _`GUC_KLV_VGT_POLICY_RESET_AFTER_VF_SWITCH` : 0x8D00
 *      This enum is to reset utilized HW engine after VF Switch (i.e to clean
 *      up Stale HW register left behind by previous VF)
 *
 *      :0: don't reset (default)
 *      :1: reset
 */

#define GUC_KLV_VGT_POLICY_SCHED_IF_IDLE_KEY		0x8001
#define GUC_KLV_VGT_POLICY_SCHED_IF_IDLE_LEN		1u

#define GUC_KLV_VGT_POLICY_ADVERSE_SAMPLE_PERIOD_KEY	0x8002
#define GUC_KLV_VGT_POLICY_ADVERSE_SAMPLE_PERIOD_LEN	1u

#define GUC_KLV_VGT_POLICY_RESET_AFTER_VF_SWITCH_KEY	0x8D00
#define GUC_KLV_VGT_POLICY_RESET_AFTER_VF_SWITCH_LEN	1u

/**
 * DOC: GuC VF Configuration KLVs
 *
 * `GuC KLV`_ keys available for use with PF2GUC_UPDATE_VF_CFG.
 *
 * _`GUC_KLV_VF_CFG_GGTT_START` : 0x0001
 *      A 4K aligned start GTT address/offset assigned to VF.
 *      Value is 64 bits.
 *
 * _`GUC_KLV_VF_CFG_GGTT_SIZE` : 0x0002
 *      A 4K aligned size of GGTT assigned to VF.
 *      Value is 64 bits.
 *
 * _`GUC_KLV_VF_CFG_LMEM_SIZE` : 0x0003
 *      A 2M aligned size of local memory assigned to VF.
 *      Value is 64 bits.
 *
 * _`GUC_KLV_VF_CFG_NUM_CONTEXTS` : 0x0004
 *      Refers to the number of contexts allocated to this VF.
 *
 *      :0: no contexts (default)
 *      :1-65535: number of contexts (Gen12)
 *
 * _`GUC_KLV_VF_CFG_TILE_MASK` : 0x0005
 *      For multi-tiled products, this field contains the bitwise-OR of tiles
 *      assigned to the VF. Bit-0-set means VF has access to Tile-0,
 *      Bit-31-set means VF has access to Tile-31, and etc.
 *      At least one tile will always be allocated.
 *      If all bits are zero, VF KMD should treat this as a fatal error.
 *      For, single-tile products this KLV config is ignored.
 *
 * _`GUC_KLV_VF_CFG_NUM_DOORBELLS` : 0x0006
 *      Refers to the number of doorbells allocated to this VF.
 *
 *      :0: no doorbells (default)
 *      :1-255: number of doorbells (Gen12)
 *
 * _`GUC_KLV_VF_CFG_EXEC_QUANTUM` : 0x8A01
 *      This config sets the VFs-execution-quantum in milliseconds.
 *      GUC will attempt to obey the maximum values as much as HW is capable
 *      of and this will never be perfectly-exact (accumulated nano-second
 *      granularity) since the GPUs clock time runs off a different crystal
 *      from the CPUs clock. Changing this KLV on a VF that is currently
 *      running a context won't take effect until a new context is scheduled in.
 *      That said, when the PF is changing this value from 0x0 to
 *      a non-zero value, it might never take effect if the VF is running an
 *      infinitely long compute or shader kernel. In such a scenario, the
 *      PF would need to trigger a VM PAUSE and then change the KLV to force
 *      it to take effect. Such cases might typically happen on a 1PF+1VF
 *      Virtualization config enabled for heavier workloads like AI/ML.
 *
 *      The max value for this KLV is 100 seconds, anything exceeding that
 *      will be clamped to the max.
 *
 *      :0: infinite exec quantum (default)
 *      :100000: maximum exec quantum (100000ms == 100s)
 *
 * _`GUC_KLV_VF_CFG_PREEMPT_TIMEOUT` : 0x8A02
 *      This config sets the VF-preemption-timeout in microseconds.
 *      GUC will attempt to obey the minimum and maximum values as much as
 *      HW is capable and this will never be perfectly-exact (accumulated
 *      nano-second granularity) since the GPUs clock time runs off a
 *      different crystal from the CPUs clock. Changing this KLV on a VF
 *      that is currently running a context won't take effect until a new
 *      context is scheduled in.
 *      That said, when the PF is changing this value from 0x0 to
 *      a non-zero value, it might never take effect if the VF is running an
 *      infinitely long compute or shader kernel.
 *      In this case, the PF would need to trigger a VM PAUSE and then change
 *      the KLV to force it to take effect. Such cases might typically happen
 *      on a 1PF+1VF Virtualization config enabled for heavier workloads like
 *      AI/ML.
 *
 *      The max value for this KLV is 100 seconds, anything exceeding that
 *      will be clamped to the max.
 *
 *      :0: no preemption timeout (default)
 *      :100000000: maximum preemption timeout (100000000us == 100s)
 *
 * _`GUC_KLV_VF_CFG_THRESHOLD_CAT_ERR` : 0x8A03
 *      This config sets threshold for CAT errors caused by the VF.
 *
 *      :0: adverse events or error will not be reported (default)
 *      :n: event occurrence count per sampling interval
 *
 * _`GUC_KLV_VF_CFG_THRESHOLD_ENGINE_RESET` : 0x8A04
 *      This config sets threshold for engine reset caused by the VF.
 *
 *      :0: adverse events or error will not be reported (default)
 *      :n: event occurrence count per sampling interval
 *
 * _`GUC_KLV_VF_CFG_THRESHOLD_PAGE_FAULT` : 0x8A05
 *      This config sets threshold for page fault errors caused by the VF.
 *
 *      :0: adverse events or error will not be reported (default)
 *      :n: event occurrence count per sampling interval
 *
 * _`GUC_KLV_VF_CFG_THRESHOLD_H2G_STORM` : 0x8A06
 *      This config sets threshold for H2G interrupts triggered by the VF.
 *
 *      :0: adverse events or error will not be reported (default)
 *      :n: time (us) per sampling interval
 *
 * _`GUC_KLV_VF_CFG_THRESHOLD_IRQ_STORM` : 0x8A07
 *      This config sets threshold for GT interrupts triggered by the VF's
 *      workloads.
 *
 *      :0: adverse events or error will not be reported (default)
 *      :n: time (us) per sampling interval
 *
 * _`GUC_KLV_VF_CFG_THRESHOLD_DOORBELL_STORM` : 0x8A08
 *      This config sets threshold for doorbell's ring triggered by the VF.
 *
 *      :0: adverse events or error will not be reported (default)
 *      :n: time (us) per sampling interval
 *
 * _`GUC_KLV_VF_CFG_BEGIN_DOORBELL_ID` : 0x8A0A
 *      Refers to the start index of doorbell assigned to this VF.
 *
 *      :0: (default)
 *      :1-255: number of doorbells (Gen12)
 *
 * _`GUC_KLV_VF_CFG_BEGIN_CONTEXT_ID` : 0x8A0B
 *      Refers to the start index in context array allocated to this VF’s use.
 *
 *      :0: (default)
 *      :1-65535: number of contexts (Gen12)
 *
 * _`GUC_KLV_VF_CFG_SCHED_PRIORITY` : 0x8A0C
 *      This config controls VF’s scheduling priority.
 *
 *      :0: LOW = schedule VF only if it has active work (default)
 *      :1: NORMAL = schedule VF always, irrespective of whether it has work or not
 *      :2: HIGH = schedule VF in the next time-slice after current active
 *          time-slice completes if it has active work
 */

#define GUC_KLV_VF_CFG_GGTT_START_KEY		0x0001
#define GUC_KLV_VF_CFG_GGTT_START_LEN		2u

#define GUC_KLV_VF_CFG_GGTT_SIZE_KEY		0x0002
#define GUC_KLV_VF_CFG_GGTT_SIZE_LEN		2u

#define GUC_KLV_VF_CFG_LMEM_SIZE_KEY		0x0003
#define GUC_KLV_VF_CFG_LMEM_SIZE_LEN		2u

#define GUC_KLV_VF_CFG_NUM_CONTEXTS_KEY		0x0004
#define GUC_KLV_VF_CFG_NUM_CONTEXTS_LEN		1u

#define GUC_KLV_VF_CFG_TILE_MASK_KEY		0x0005
#define GUC_KLV_VF_CFG_TILE_MASK_LEN		1u

#define GUC_KLV_VF_CFG_NUM_DOORBELLS_KEY	0x0006
#define GUC_KLV_VF_CFG_NUM_DOORBELLS_LEN	1u

#define GUC_KLV_VF_CFG_EXEC_QUANTUM_KEY		0x8a01
#define GUC_KLV_VF_CFG_EXEC_QUANTUM_LEN		1u
#define GUC_KLV_VF_CFG_EXEC_QUANTUM_MAX_VALUE	100000u

#define GUC_KLV_VF_CFG_PREEMPT_TIMEOUT_KEY		0x8a02
#define GUC_KLV_VF_CFG_PREEMPT_TIMEOUT_LEN		1u
#define GUC_KLV_VF_CFG_PREEMPT_TIMEOUT_MAX_VALUE	100000000u

#define GUC_KLV_VF_CFG_THRESHOLD_CAT_ERR_KEY		0x8a03
#define GUC_KLV_VF_CFG_THRESHOLD_CAT_ERR_LEN		1u

#define GUC_KLV_VF_CFG_THRESHOLD_ENGINE_RESET_KEY	0x8a04
#define GUC_KLV_VF_CFG_THRESHOLD_ENGINE_RESET_LEN	1u

#define GUC_KLV_VF_CFG_THRESHOLD_PAGE_FAULT_KEY		0x8a05
#define GUC_KLV_VF_CFG_THRESHOLD_PAGE_FAULT_LEN		1u

#define GUC_KLV_VF_CFG_THRESHOLD_H2G_STORM_KEY		0x8a06
#define GUC_KLV_VF_CFG_THRESHOLD_H2G_STORM_LEN		1u

#define GUC_KLV_VF_CFG_THRESHOLD_IRQ_STORM_KEY		0x8a07
#define GUC_KLV_VF_CFG_THRESHOLD_IRQ_STORM_LEN		1u

#define GUC_KLV_VF_CFG_THRESHOLD_DOORBELL_STORM_KEY	0x8a08
#define GUC_KLV_VF_CFG_THRESHOLD_DOORBELL_STORM_LEN	1u

#define GUC_KLV_VF_CFG_BEGIN_DOORBELL_ID_KEY	0x8a0a
#define GUC_KLV_VF_CFG_BEGIN_DOORBELL_ID_LEN	1u

#define GUC_KLV_VF_CFG_BEGIN_CONTEXT_ID_KEY	0x8a0b
#define GUC_KLV_VF_CFG_BEGIN_CONTEXT_ID_LEN	1u

#define GUC_KLV_VF_CFG_SCHED_PRIORITY_KEY	0x8a0c
#define GUC_KLV_VF_CFG_SCHED_PRIORITY_LEN	1u
#define   GUC_SCHED_PRIORITY_LOW		0u
#define   GUC_SCHED_PRIORITY_NORMAL		1u
#define   GUC_SCHED_PRIORITY_HIGH		2u

/*
 * Workaround keys:
 */
enum xe_guc_klv_ids {
	GUC_WORKAROUND_KLV_BLOCK_INTERRUPTS_WHEN_MGSR_BLOCKED				= 0x9002,
	GUC_WORKAROUND_KLV_ID_GAM_PFQ_SHADOW_TAIL_POLLING				= 0x9005,
	GUC_WORKAROUND_KLV_ID_DISABLE_MTP_DURING_ASYNC_COMPUTE				= 0x9007,
	GUC_WA_KLV_NP_RD_WRITE_TO_CLEAR_RCSM_AT_CGP_LATE_RESTORE			= 0x9008,
	GUC_WORKAROUND_KLV_ID_BACK_TO_BACK_RCS_ENGINE_RESET				= 0x9009,
	GUC_WA_KLV_WAKE_POWER_DOMAINS_FOR_OUTBOUND_MMIO					= 0x900a,
	GUC_WA_KLV_RESET_BB_STACK_PTR_ON_VF_SWITCH					= 0x900b,
};

#endif
