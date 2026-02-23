/*
 * Copyright 2018 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

/* To compile this assembly code:
 *
 * gfx12:
 *   cpp -DASIC_FAMILY=CHIP_GFX12 cwsr_trap_handler_gfx12.asm -P -o gfx12.sp3
 *   sp3 gfx12.sp3 -hex gfx12.hex
 */

#define CHIP_GFX12 37
#define CHIP_GC_12_0_3 38

#define HAVE_XNACK (ASIC_FAMILY == CHIP_GC_12_0_3)
#define HAVE_57BIT_ADDRESS (ASIC_FAMILY == CHIP_GC_12_0_3)
#define HAVE_BANKED_VGPRS (ASIC_FAMILY == CHIP_GC_12_0_3)
#define NUM_NAMED_BARRIERS (ASIC_FAMILY == CHIP_GC_12_0_3 ? 0x10 : 0)
#define HAVE_CLUSTER_BARRIER (ASIC_FAMILY == CHIP_GC_12_0_3)
#define CLUSTER_BARRIER_SERIALIZE_WORKAROUND (ASIC_FAMILY == CHIP_GC_12_0_3)
#define RELAXED_SCHEDULING_IN_TRAP (ASIC_FAMILY == CHIP_GFX12)
#define HAVE_INSTRUCTION_FIXUP (ASIC_FAMILY == CHIP_GC_12_0_3)

#define SINGLE_STEP_MISSED_WORKAROUND 1	//workaround for lost TRAP_AFTER_INST exception when SAVECTX raised
#define HAVE_VALU_SGPR_HAZARD (ASIC_FAMILY == CHIP_GFX12)
#define WAVE32_ONLY (ASIC_FAMILY == CHIP_GC_12_0_3)
#define SAVE_TTMPS_IN_SGPR_BLOCK (ASIC_FAMILY >= CHIP_GC_12_0_3)

#if HAVE_XNACK && !WAVE32_ONLY
# error
#endif

#define ADDRESS_HI32_NUM_BITS ((HAVE_57BIT_ADDRESS ? 57 : 48) - 32)
#define ADDRESS_HI32_MASK ((1 << ADDRESS_HI32_NUM_BITS) - 1)

var SQ_WAVE_STATE_PRIV_ALL_BARRIER_COMPLETE_MASK	= 0x4 | (NUM_NAMED_BARRIERS ? 0x8 : 0) | (HAVE_CLUSTER_BARRIER ? 0x10000 : 0)
var SQ_WAVE_STATE_PRIV_SCC_SHIFT		= 9
var SQ_WAVE_STATE_PRIV_SYS_PRIO_MASK		= 0xC00
var SQ_WAVE_STATE_PRIV_HALT_MASK		= 0x4000
var SQ_WAVE_STATE_PRIV_POISON_ERR_MASK		= 0x8000
var SQ_WAVE_STATE_PRIV_POISON_ERR_SHIFT		= 15
var SQ_WAVE_STATUS_WAVE64_SHIFT			= 29
var SQ_WAVE_STATUS_WAVE64_SIZE			= 1
var SQ_WAVE_STATUS_NO_VGPRS_SHIFT		= 24
var SQ_WAVE_STATUS_IN_WG_SHIFT			= 11
var SQ_WAVE_STATE_PRIV_ALWAYS_CLEAR_MASK	= SQ_WAVE_STATE_PRIV_SYS_PRIO_MASK|SQ_WAVE_STATE_PRIV_POISON_ERR_MASK
var S_SAVE_PC_HI_TRAP_ID_MASK			= 0xF0000000

var SQ_WAVE_LDS_ALLOC_LDS_SIZE_SHIFT		= 12
var SQ_WAVE_LDS_ALLOC_LDS_SIZE_SIZE		= 9
var SQ_WAVE_GPR_ALLOC_VGPR_SIZE_SIZE		= 8
var SQ_WAVE_GPR_ALLOC_VGPR_SIZE_SHIFT		= 12

#if ASIC_FAMILY < CHIP_GC_12_0_3
var SQ_WAVE_LDS_ALLOC_GRANULARITY		= 9
#else
var SQ_WAVE_LDS_ALLOC_GRANULARITY		= 10
#endif

var SQ_WAVE_EXCP_FLAG_PRIV_ADDR_WATCH_MASK	= 0xF
var SQ_WAVE_EXCP_FLAG_PRIV_MEM_VIOL_SHIFT	= 4
var SQ_WAVE_EXCP_FLAG_PRIV_MEM_VIOL_MASK	= 0x10
var SQ_WAVE_EXCP_FLAG_PRIV_SAVE_CONTEXT_SHIFT	= 5
var SQ_WAVE_EXCP_FLAG_PRIV_SAVE_CONTEXT_MASK	= 0x20
var SQ_WAVE_EXCP_FLAG_PRIV_ILLEGAL_INST_MASK	= 0x40
var SQ_WAVE_EXCP_FLAG_PRIV_ILLEGAL_INST_SHIFT	= 6
var SQ_WAVE_EXCP_FLAG_PRIV_HOST_TRAP_MASK	= 0x80
var SQ_WAVE_EXCP_FLAG_PRIV_HOST_TRAP_SHIFT	= 7
var SQ_WAVE_EXCP_FLAG_PRIV_WAVE_START_MASK	= 0x100
var SQ_WAVE_EXCP_FLAG_PRIV_WAVE_START_SHIFT	= 8
var SQ_WAVE_EXCP_FLAG_PRIV_WAVE_END_MASK	= 0x200
var SQ_WAVE_EXCP_FLAG_PRIV_TRAP_AFTER_INST_MASK	= 0x800
var SQ_WAVE_TRAP_CTRL_ADDR_WATCH_MASK		= 0x80
var SQ_WAVE_TRAP_CTRL_TRAP_AFTER_INST_MASK	= 0x200

var SQ_WAVE_EXCP_FLAG_PRIV_NON_MASKABLE_EXCP_MASK= SQ_WAVE_EXCP_FLAG_PRIV_MEM_VIOL_MASK		|\
						  SQ_WAVE_EXCP_FLAG_PRIV_ILLEGAL_INST_MASK	|\
						  SQ_WAVE_EXCP_FLAG_PRIV_HOST_TRAP_MASK		|\
						  SQ_WAVE_EXCP_FLAG_PRIV_WAVE_START_MASK	|\
						  SQ_WAVE_EXCP_FLAG_PRIV_WAVE_END_MASK		|\
						  SQ_WAVE_EXCP_FLAG_PRIV_TRAP_AFTER_INST_MASK
var SQ_WAVE_EXCP_FLAG_PRIV_RESTORE_PART_1_SIZE	= SQ_WAVE_EXCP_FLAG_PRIV_SAVE_CONTEXT_SHIFT
var SQ_WAVE_EXCP_FLAG_PRIV_RESTORE_PART_2_SHIFT	= SQ_WAVE_EXCP_FLAG_PRIV_ILLEGAL_INST_SHIFT
var SQ_WAVE_EXCP_FLAG_PRIV_RESTORE_PART_2_SIZE	= SQ_WAVE_EXCP_FLAG_PRIV_HOST_TRAP_SHIFT - SQ_WAVE_EXCP_FLAG_PRIV_ILLEGAL_INST_SHIFT
var SQ_WAVE_EXCP_FLAG_PRIV_RESTORE_PART_3_SHIFT	= SQ_WAVE_EXCP_FLAG_PRIV_WAVE_START_SHIFT
var SQ_WAVE_EXCP_FLAG_PRIV_RESTORE_PART_3_SIZE	= 32 - SQ_WAVE_EXCP_FLAG_PRIV_RESTORE_PART_3_SHIFT

var SQ_WAVE_SCHED_MODE_DEP_MODE_SHIFT		= 0
var SQ_WAVE_SCHED_MODE_DEP_MODE_SIZE		= 2

var BARRIER_STATE_SIGNAL_OFFSET			= 16
var BARRIER_STATE_SIGNAL_SIZE			= 7
var BARRIER_STATE_MEMBER_OFFSET			= 4
var BARRIER_STATE_MEMBER_SIZE			= 7
var BARRIER_STATE_VALID_OFFSET			= 0

#if RELAXED_SCHEDULING_IN_TRAP
var TTMP11_SCHED_MODE_SHIFT			= 26
var TTMP11_SCHED_MODE_SIZE			= 2
var TTMP11_SCHED_MODE_MASK			= 0xC000000
#endif

var NAMED_BARRIERS_SR_OFFSET_FROM_HWREG		= 0x80
var S_BARRIER_INIT_MEMBERCNT_MASK		= 0x7F0000
var S_BARRIER_INIT_MEMBERCNT_SHIFT		= 0x10

var SQ_WAVE_XNACK_STATE_PRIV_FIRST_REPLAY_SHIFT	= 18
var SQ_WAVE_XNACK_STATE_PRIV_FIRST_REPLAY_SIZE	= 1
var SQ_WAVE_XNACK_STATE_PRIV_REPLAY_W64H_SHIFT	= 16
var SQ_WAVE_XNACK_STATE_PRIV_REPLAY_W64H_SIZE	= 1
var SQ_WAVE_XNACK_STATE_PRIV_FXPTR_SHIFT	= 0
var SQ_WAVE_XNACK_STATE_PRIV_FXPTR_SIZE		= 7

#if HAVE_BANKED_VGPRS
var SQ_WAVE_MODE_DST_SRC0_SRC1_VGPR_MSB_SHIFT	= 12
var SQ_WAVE_MODE_DST_SRC0_SRC1_VGPR_MSB_SIZE	= 6
#endif

var TTMP11_SCHED_MODE_SHIFT			= 26
var TTMP11_SCHED_MODE_SIZE			= 2
var TTMP11_SCHED_MODE_MASK			= 0xC000000
var TTMP11_DEBUG_TRAP_ENABLED_SHIFT		= 23
var TTMP11_DEBUG_TRAP_ENABLED_MASK		= 0x800000
var TTMP11_FIRST_REPLAY_SHIFT			= 22
var TTMP11_FIRST_REPLAY_MASK			= 0x400000
var TTMP11_REPLAY_W64H_SHIFT			= 21
var TTMP11_REPLAY_W64H_MASK			= 0x200000
var TTMP11_FXPTR_SHIFT				= 14
var TTMP11_FXPTR_MASK				= 0x1FC000

// SQ_SEL_X/Y/Z/W, BUF_NUM_FORMAT_FLOAT, (0 for MUBUF stride[17:14]
// when ADD_TID_ENABLE and BUF_DATA_FORMAT_32 for MTBUF), ADD_TID_ENABLE
var S_SAVE_BUF_RSRC_WORD1_STRIDE		= 0x00040000
var S_SAVE_BUF_RSRC_WORD3_MISC			= 0x10807FAC
var S_SAVE_SPI_INIT_FIRST_WAVE_MASK		= 0x04000000
var S_SAVE_SPI_INIT_FIRST_WAVE_SHIFT		= 26

var S_SAVE_PC_HI_FIRST_WAVE_MASK		= 0x80000000
var S_SAVE_PC_HI_FIRST_WAVE_SHIFT		= 31

#if HAVE_BANKED_VGPRS
var S_SAVE_PC_HI_DST_SRC0_SRC1_VGPR_MSB_SHIFT	= 25
var S_SAVE_PC_HI_DST_SRC0_SRC1_VGPR_MSB_SIZE	= 6
#endif

var s_sgpr_save_num				= 108

var s_save_spi_init_lo				= exec_lo
var s_save_spi_init_hi				= exec_hi
var s_save_pc_lo				= ttmp0
var s_save_pc_hi				= ttmp1
var s_save_exec_lo				= ttmp2
var s_save_exec_hi				= ttmp3
var s_save_state_priv				= ttmp12
var s_save_excp_flag_priv			= ttmp15
var s_save_xnack_mask				= s_save_exec_hi
var s_wave_size					= ttmp7
var s_save_base_addr_lo				= ttmp8
var s_save_base_addr_hi				= ttmp9
var s_save_addr_lo				= ttmp10
var s_save_addr_hi				= ttmp11
var s_save_mem_offset				= ttmp4
var s_save_alloc_size				= s_save_excp_flag_priv
var s_save_tmp					= ttmp14
var s_save_m0					= ttmp5
var s_save_ttmps_lo				= s_save_tmp
var s_save_ttmps_hi				= s_save_excp_flag_priv

var S_RESTORE_SPI_INIT_FIRST_WAVE_MASK		= 0x04000000
var S_RESTORE_SPI_INIT_FIRST_WAVE_SHIFT		= 26
var S_WAVE_SIZE					= 25

var s_restore_spi_init_lo			= exec_lo
var s_restore_spi_init_hi			= exec_hi
var s_restore_mem_offset			= ttmp12
var s_restore_alloc_size			= ttmp3
var s_restore_tmp				= ttmp2
var s_restore_mem_offset_save			= s_restore_tmp
var s_restore_m0				= s_restore_alloc_size
var s_restore_mode				= ttmp7
var s_restore_flat_scratch			= s_restore_tmp
var s_restore_pc_lo				= ttmp0
var s_restore_pc_hi				= ttmp1
var s_restore_exec_lo				= ttmp4
var s_restore_exec_hi				= ttmp5
var s_restore_state_priv			= ttmp14
var s_restore_excp_flag_priv			= ttmp15
var s_restore_xnack_mask			= ttmp13
var s_restore_base_addr_lo			= ttmp8
var s_restore_base_addr_hi			= ttmp9
var s_restore_addr_lo				= ttmp10
var s_restore_addr_hi				= ttmp11
var s_restore_size				= ttmp6
var s_restore_ttmps_lo				= s_restore_tmp
var s_restore_ttmps_hi				= s_restore_alloc_size
var s_restore_spi_init_hi_save			= s_restore_exec_hi

#if SAVE_TTMPS_IN_SGPR_BLOCK
var TTMP_SR_OFFSET_FROM_HWREG			= -0x40
#else
var TTMP_SR_OFFSET_FROM_HWREG			= 0x40
#endif

shader main
	asic(DEFAULT)
	type(CS)
	wave_size(32)

	s_branch	L_SKIP_RESTORE						//NOT restore. might be a regular trap or save

L_JUMP_TO_RESTORE:
	s_branch	L_RESTORE

L_SKIP_RESTORE:
#if RELAXED_SCHEDULING_IN_TRAP
	// Assume most relaxed scheduling mode is set. Save and revert to normal mode.
	s_getreg_b32	ttmp2, hwreg(HW_REG_WAVE_SCHED_MODE)
	s_wait_alu	0
	s_setreg_imm32_b32	hwreg(HW_REG_WAVE_SCHED_MODE, \
		SQ_WAVE_SCHED_MODE_DEP_MODE_SHIFT, SQ_WAVE_SCHED_MODE_DEP_MODE_SIZE), 0
#endif

	s_getreg_b32	s_save_state_priv, hwreg(HW_REG_WAVE_STATE_PRIV)	//save STATUS since we will change SCC

#if RELAXED_SCHEDULING_IN_TRAP
	// Save SCHED_MODE[1:0] into ttmp11[27:26].
	s_andn2_b32	ttmp11, ttmp11, TTMP11_SCHED_MODE_MASK
	s_lshl_b32	ttmp2, ttmp2, TTMP11_SCHED_MODE_SHIFT
	s_or_b32	ttmp11, ttmp11, ttmp2
#endif

	// Clear SPI_PRIO: do not save with elevated priority.
	// Clear ECC_ERR: prevents SQC store and triggers FATAL_HALT if setreg'd.
	s_andn2_b32	s_save_state_priv, s_save_state_priv, SQ_WAVE_STATE_PRIV_ALWAYS_CLEAR_MASK

	s_getreg_b32	s_save_excp_flag_priv, hwreg(HW_REG_WAVE_EXCP_FLAG_PRIV)

	s_and_b32       ttmp2, s_save_state_priv, SQ_WAVE_STATE_PRIV_HALT_MASK
	s_cbranch_scc0	L_NOT_HALTED

L_HALTED:
	// Host trap may occur while wave is halted.
	s_and_b32	ttmp2, s_save_excp_flag_priv, SQ_WAVE_EXCP_FLAG_PRIV_HOST_TRAP_MASK
	s_cbranch_scc1	L_FETCH_2ND_TRAP

L_CHECK_SAVE:
	s_and_b32	ttmp2, s_save_excp_flag_priv, SQ_WAVE_EXCP_FLAG_PRIV_SAVE_CONTEXT_MASK
	s_cbranch_scc1	L_SAVE

	// Wave is halted but neither host trap nor SAVECTX is raised.
	// Caused by instruction fetch memory violation.
	// Spin wait until context saved to prevent interrupt storm.
	s_sleep		0x10
	s_getreg_b32	s_save_excp_flag_priv, hwreg(HW_REG_WAVE_EXCP_FLAG_PRIV)
	s_branch	L_CHECK_SAVE

L_NOT_HALTED:
	// Let second-level handle non-SAVECTX exception or trap.
	// Any concurrent SAVECTX will be handled upon re-entry once halted.

	// Check non-maskable exceptions. memory_violation, illegal_instruction
	// and xnack_error exceptions always cause the wave to enter the trap
	// handler.
	s_and_b32	ttmp2, s_save_excp_flag_priv, SQ_WAVE_EXCP_FLAG_PRIV_NON_MASKABLE_EXCP_MASK
	s_cbranch_scc1	L_FETCH_2ND_TRAP

	// Check for maskable exceptions in trapsts.excp and trapsts.excp_hi.
	// Maskable exceptions only cause the wave to enter the trap handler if
	// their respective bit in mode.excp_en is set.
	s_getreg_b32	ttmp2, hwreg(HW_REG_WAVE_EXCP_FLAG_USER)
	s_and_b32	ttmp3, s_save_excp_flag_priv, SQ_WAVE_EXCP_FLAG_PRIV_ADDR_WATCH_MASK
	s_cbranch_scc0	L_NOT_ADDR_WATCH
	s_or_b32	ttmp2, ttmp2, SQ_WAVE_TRAP_CTRL_ADDR_WATCH_MASK

L_NOT_ADDR_WATCH:
	s_getreg_b32	ttmp3, hwreg(HW_REG_WAVE_TRAP_CTRL)
	s_and_b32	ttmp2, ttmp3, ttmp2
	s_cbranch_scc1	L_FETCH_2ND_TRAP

L_CHECK_TRAP_ID:
	// Check trap_id != 0
	s_and_b32	ttmp2, s_save_pc_hi, S_SAVE_PC_HI_TRAP_ID_MASK
	s_cbranch_scc1	L_FETCH_2ND_TRAP

#if SINGLE_STEP_MISSED_WORKAROUND
	// Prioritize single step exception over context save.
	// Second-level trap will halt wave and RFE, re-entering for SAVECTX.
	// WAVE_TRAP_CTRL is already in ttmp3.
	s_and_b32	ttmp3, ttmp3, SQ_WAVE_TRAP_CTRL_TRAP_AFTER_INST_MASK
	s_cbranch_scc1	L_FETCH_2ND_TRAP
#endif

	s_and_b32	ttmp2, s_save_excp_flag_priv, SQ_WAVE_EXCP_FLAG_PRIV_SAVE_CONTEXT_MASK
	s_cbranch_scc1	L_SAVE

L_FETCH_2ND_TRAP:
#if HAVE_XNACK
	save_and_clear_xnack_state_priv(ttmp14)
#endif

	// Read second-level TBA/TMA from first-level TMA and jump if available.
	// ttmp[2:5] and ttmp12 can be used (others hold SPI-initialized debug data)
	// ttmp12 holds SQ_WAVE_STATUS
	s_sendmsg_rtn_b64       [ttmp14, ttmp15], sendmsg(MSG_RTN_GET_TMA)
	s_wait_idle
	s_lshl_b64	[ttmp14, ttmp15], [ttmp14, ttmp15], 0x8

	s_bitcmp1_b32	ttmp15, (ADDRESS_HI32_NUM_BITS - 1)
	s_cbranch_scc0	L_NO_SIGN_EXTEND_TMA
	s_or_b32	ttmp15, ttmp15, ~ADDRESS_HI32_MASK
L_NO_SIGN_EXTEND_TMA:
#if RELAXED_SCHEDULING_IN_TRAP
	// Move SCHED_MODE[1:0] from ttmp11 to unused bits in ttmp1[27:26] (return PC_HI).
	// The second-level trap will restore from ttmp1 for backwards compatibility.
	s_and_b32	ttmp2, ttmp11, TTMP11_SCHED_MODE_MASK
	s_andn2_b32	ttmp1, ttmp1, TTMP11_SCHED_MODE_MASK
	s_or_b32	ttmp1, ttmp1, ttmp2
#endif

	s_load_dword    ttmp2, [ttmp14, ttmp15], 0x10 scope:SCOPE_SYS		// debug trap enabled flag
	s_wait_idle
	s_lshl_b32      ttmp2, ttmp2, TTMP11_DEBUG_TRAP_ENABLED_SHIFT
	s_andn2_b32     ttmp11, ttmp11, TTMP11_DEBUG_TRAP_ENABLED_MASK
	s_or_b32        ttmp11, ttmp11, ttmp2

	s_load_dwordx2	[ttmp2, ttmp3], [ttmp14, ttmp15], 0x0 scope:SCOPE_SYS	// second-level TBA
	s_wait_idle
	s_load_dwordx2	[ttmp14, ttmp15], [ttmp14, ttmp15], 0x8 scope:SCOPE_SYS	// second-level TMA
	s_wait_idle

	s_and_b64	[ttmp2, ttmp3], [ttmp2, ttmp3], [ttmp2, ttmp3]
	s_cbranch_scc0	L_NO_NEXT_TRAP						// second-level trap handler not been set
	s_setpc_b64	[ttmp2, ttmp3]						// jump to second-level trap handler

L_NO_NEXT_TRAP:
	// If not caused by trap then halt wave to prevent re-entry.
	s_and_b32	ttmp2, s_save_pc_hi, S_SAVE_PC_HI_TRAP_ID_MASK
	s_cbranch_scc1	L_TRAP_CASE

	// Host trap will not cause trap re-entry.
	s_getreg_b32	ttmp2, hwreg(HW_REG_WAVE_EXCP_FLAG_PRIV)
	s_and_b32	ttmp2, ttmp2, SQ_WAVE_EXCP_FLAG_PRIV_HOST_TRAP_MASK
	s_cbranch_scc1	L_EXIT_TRAP
	s_or_b32	s_save_state_priv, s_save_state_priv, SQ_WAVE_STATE_PRIV_HALT_MASK

	// If the PC points to S_ENDPGM then context save will fail if STATE_PRIV.HALT is set.
	// Rewind the PC to prevent this from occurring.
	s_sub_u32	ttmp0, ttmp0, 0x8
	s_subb_u32	ttmp1, ttmp1, 0x0

	s_branch	L_EXIT_TRAP

L_TRAP_CASE:
	// Advance past trap instruction to prevent re-entry.
	s_add_u32	ttmp0, ttmp0, 0x4
	s_addc_u32	ttmp1, ttmp1, 0x0

L_EXIT_TRAP:
	s_and_b32	ttmp1, ttmp1, ADDRESS_HI32_MASK

#if HAVE_INSTRUCTION_FIXUP
	s_getreg_b32	s_save_excp_flag_priv, hwreg(HW_REG_WAVE_EXCP_FLAG_PRIV)
	fixup_instruction()
#endif

#if HAVE_XNACK
	restore_xnack_state_priv(s_save_tmp)
#endif

	// Restore SQ_WAVE_STATUS.
	s_and_b64	exec, exec, exec					// Restore STATUS.EXECZ, not writable by s_setreg_b32
	s_and_b64	vcc, vcc, vcc						// Restore STATUS.VCCZ, not writable by s_setreg_b32

	// STATE_PRIV.*BARRIER_COMPLETE may have changed since we read it.
	// Only restore fields which the trap handler changes.
	s_lshr_b32	s_save_state_priv, s_save_state_priv, SQ_WAVE_STATE_PRIV_SCC_SHIFT

#if RELAXED_SCHEDULING_IN_TRAP
	// Assume relaxed scheduling mode after this point.
	restore_sched_mode(ttmp2)
#endif

	s_setreg_b32	hwreg(HW_REG_WAVE_STATE_PRIV, SQ_WAVE_STATE_PRIV_SCC_SHIFT, \
		SQ_WAVE_STATE_PRIV_POISON_ERR_SHIFT - SQ_WAVE_STATE_PRIV_SCC_SHIFT + 1), s_save_state_priv

	s_rfe_b64	[ttmp0, ttmp1]

L_SAVE:
	// If VGPRs have been deallocated then terminate the wavefront.
	// It has no remaining program to run and cannot save without VGPRs.
	s_getreg_b32	s_save_tmp, hwreg(HW_REG_WAVE_STATUS)
	s_bitcmp1_b32	s_save_tmp, SQ_WAVE_STATUS_NO_VGPRS_SHIFT
	s_cbranch_scc0	L_HAVE_VGPRS
	s_endpgm
L_HAVE_VGPRS:
	s_and_b32	s_save_pc_hi, s_save_pc_hi, ADDRESS_HI32_MASK
	s_mov_b32	s_save_tmp, 0
	s_setreg_b32	hwreg(HW_REG_WAVE_EXCP_FLAG_PRIV, SQ_WAVE_EXCP_FLAG_PRIV_SAVE_CONTEXT_SHIFT, 1), s_save_tmp	//clear saveCtx bit

#if HAVE_XNACK
	save_and_clear_xnack_state_priv(s_save_tmp)
#endif

#if HAVE_INSTRUCTION_FIXUP
	fixup_instruction()
#endif

	/* inform SPI the readiness and wait for SPI's go signal */
	s_mov_b32	s_save_exec_lo, exec_lo					//save EXEC and use EXEC for the go signal from SPI
	s_mov_b32	s_save_exec_hi, exec_hi
	s_mov_b64	exec, 0x0						//clear EXEC to get ready to receive

	s_sendmsg_rtn_b64       [exec_lo, exec_hi], sendmsg(MSG_RTN_SAVE_WAVE)
	s_wait_idle

	// Save first_wave flag so we can clear high bits of save address.
	s_and_b32	s_save_tmp, s_save_spi_init_hi, S_SAVE_SPI_INIT_FIRST_WAVE_MASK
	s_lshl_b32	s_save_tmp, s_save_tmp, (S_SAVE_PC_HI_FIRST_WAVE_SHIFT - S_SAVE_SPI_INIT_FIRST_WAVE_SHIFT)
	s_or_b32	s_save_pc_hi, s_save_pc_hi, s_save_tmp

#if HAVE_XNACK
	s_getreg_b32	s_save_xnack_mask, hwreg(HW_REG_WAVE_XNACK_MASK)
	s_setreg_imm32_b32	hwreg(HW_REG_WAVE_XNACK_MASK), 0
#endif

#if HAVE_BANKED_VGPRS
	// Save and clear shader's DST/SRC0/SRC1 VGPR bank selection so we can use v[0-255].
	s_getreg_b32	s_save_tmp, hwreg(HW_REG_WAVE_MODE, SQ_WAVE_MODE_DST_SRC0_SRC1_VGPR_MSB_SHIFT, SQ_WAVE_MODE_DST_SRC0_SRC1_VGPR_MSB_SIZE)
	s_lshl_b32	s_save_tmp, s_save_tmp, S_SAVE_PC_HI_DST_SRC0_SRC1_VGPR_MSB_SHIFT
	s_or_b32	s_save_pc_hi, s_save_pc_hi, s_save_tmp
	s_mov_b32	s_save_tmp, 0
	s_setreg_b32	hwreg(HW_REG_WAVE_MODE, SQ_WAVE_MODE_DST_SRC0_SRC1_VGPR_MSB_SHIFT, SQ_WAVE_MODE_DST_SRC0_SRC1_VGPR_MSB_SIZE), s_save_tmp
#endif

	// Trap temporaries must be saved via VGPR but all VGPRs are in use.
	// There is no ttmp space to hold the resource constant for VGPR save.
	// Save v0 by itself since it requires only two SGPRs.
	s_mov_b32	s_save_ttmps_lo, exec_lo
	s_and_b32	s_save_ttmps_hi, exec_hi, ADDRESS_HI32_MASK
	s_mov_b32	exec_lo, 0xFFFFFFFF
	s_mov_b32	exec_hi, 0xFFFFFFFF
	global_store_dword_addtid	v0, [s_save_ttmps_lo, s_save_ttmps_hi] scope:SCOPE_SYS
	v_mov_b32	v0, 0x0
	s_mov_b32	exec_lo, s_save_ttmps_lo
	s_mov_b32	exec_hi, s_save_ttmps_hi

	// Save trap temporaries 4-11, 13 initialized by SPI debug dispatch logic
	// ttmp SR memory offset:
	// - gfx12:   size(VGPR)+size(SGPR)+0x40
	// - gfx12.5: size(VGPR)+size(SGPR)-0x40
	get_wave_size2(s_save_ttmps_hi)
	get_vgpr_size_bytes(s_save_ttmps_lo, s_save_ttmps_hi)
	s_and_b32	s_save_ttmps_hi, s_save_spi_init_hi, ADDRESS_HI32_MASK
	s_add_u32	s_save_ttmps_lo, s_save_ttmps_lo, (get_sgpr_size_bytes() + TTMP_SR_OFFSET_FROM_HWREG)
	s_add_u32	s_save_ttmps_lo, s_save_ttmps_lo, s_save_spi_init_lo
	s_addc_u32	s_save_ttmps_hi, s_save_ttmps_hi, 0x0

	v_writelane_b32	v0, ttmp4, 0x4
	v_writelane_b32	v0, ttmp5, 0x5
	v_writelane_b32	v0, ttmp6, 0x6
	v_writelane_b32	v0, ttmp7, 0x7
	v_writelane_b32	v0, ttmp8, 0x8
	v_writelane_b32	v0, ttmp9, 0x9
	v_writelane_b32	v0, ttmp10, 0xA
	v_writelane_b32	v0, ttmp11, 0xB
	v_writelane_b32	v0, ttmp13, 0xD
	v_writelane_b32	v0, exec_lo, 0xE
	v_writelane_b32	v0, exec_hi, 0xF

	s_mov_b32	exec_lo, 0x3FFF
	s_mov_b32	exec_hi, 0x0
	global_store_dword_addtid	v0, [s_save_ttmps_lo, s_save_ttmps_hi] scope:SCOPE_SYS
	v_readlane_b32	ttmp14, v0, 0xE
	v_readlane_b32	ttmp15, v0, 0xF
	s_mov_b32	exec_lo, ttmp14
	s_mov_b32	exec_hi, ttmp15

	s_mov_b32	s_save_base_addr_lo, s_save_spi_init_lo
	s_and_b32	s_save_base_addr_hi, s_save_spi_init_hi, ADDRESS_HI32_MASK
	s_mov_b32	s_save_m0, m0

	get_wave_size2(s_wave_size)

	/* save first 4 VGPRs, needed for SGPR save */
	s_mov_b32	exec_lo, 0xFFFFFFFF					//need every thread from now on
	s_lshr_b32	m0, s_wave_size, S_WAVE_SIZE
	s_and_b32	m0, m0, 1
	s_cmp_eq_u32	m0, 1
	s_cbranch_scc1	L_ENABLE_SAVE_4VGPR_EXEC_HI
	s_mov_b32	exec_hi, 0x00000000
	s_branch	L_SAVE_4VGPR_WAVE32
L_ENABLE_SAVE_4VGPR_EXEC_HI:
	s_mov_b32	exec_hi, 0xFFFFFFFF
	s_branch	L_SAVE_4VGPR_WAVE64
L_SAVE_4VGPR_WAVE32:
	// VGPR Allocated in 4-GPR granularity
	global_store_addtid_b32	v1, [s_save_base_addr_lo, s_save_base_addr_hi] scope:SCOPE_SYS offset:128
	global_store_addtid_b32	v2, [s_save_base_addr_lo, s_save_base_addr_hi] scope:SCOPE_SYS offset:128*2
	global_store_addtid_b32	v3, [s_save_base_addr_lo, s_save_base_addr_hi] scope:SCOPE_SYS offset:128*3
	s_branch	L_SAVE_HWREG

L_SAVE_4VGPR_WAVE64:
	// VGPR Allocated in 4-GPR granularity
	global_store_addtid_b32	v1, [s_save_base_addr_lo, s_save_base_addr_hi] scope:SCOPE_SYS offset:256
	global_store_addtid_b32	v2, [s_save_base_addr_lo, s_save_base_addr_hi] scope:SCOPE_SYS offset:256*2
	global_store_addtid_b32	v3, [s_save_base_addr_lo, s_save_base_addr_hi] scope:SCOPE_SYS offset:256*3

	/* save HW registers */

L_SAVE_HWREG:
	// HWREG SR memory offset : size(VGPR)+size(SGPR)
	get_vgpr_size_bytes(s_save_mem_offset, s_wave_size)
	s_add_u32	s_save_mem_offset, s_save_mem_offset, get_sgpr_size_bytes()

	v_mov_b32	v0, 0x0							//Offset[31:0] from buffer resource
	v_mov_b32	v1, 0x0							//Offset[63:32] from buffer resource
	v_mov_b32	v2, 0x0							//Set of SGPRs for TCP store
	s_mov_b32	m0, 0x0							//Next lane of v2 to write to

	write_hwreg_to_v2(s_save_m0)

	// Ensure no further changes to barrier or LDS state.
	// STATE_PRIV.*BARRIER_COMPLETE may change up to this point.
	wait_trap_barriers(s_save_tmp, s_save_m0, 1)

	// Re-read final state of *BARRIER_COMPLETE fields for save.
	s_getreg_b32	s_save_tmp, hwreg(HW_REG_WAVE_STATE_PRIV)
	s_and_b32	s_save_tmp, s_save_tmp, SQ_WAVE_STATE_PRIV_ALL_BARRIER_COMPLETE_MASK
	s_andn2_b32	s_save_state_priv, s_save_state_priv, SQ_WAVE_STATE_PRIV_ALL_BARRIER_COMPLETE_MASK
	s_or_b32	s_save_state_priv, s_save_state_priv, s_save_tmp

	write_hwreg_to_v2(s_save_pc_lo)
	s_and_b32       s_save_tmp, s_save_pc_hi, ADDRESS_HI32_MASK
	write_hwreg_to_v2(s_save_tmp)
	write_hwreg_to_v2(s_save_exec_lo)
#if WAVE32_ONLY
	s_mov_b32	s_save_tmp, 0
	write_hwreg_to_v2(s_save_tmp)
#else
	write_hwreg_to_v2(s_save_exec_hi)
#endif
	write_hwreg_to_v2(s_save_state_priv)

	s_getreg_b32	s_save_tmp, hwreg(HW_REG_WAVE_EXCP_FLAG_PRIV)
	write_hwreg_to_v2(s_save_tmp)

#if HAVE_XNACK
	write_hwreg_to_v2(s_save_xnack_mask)
#else
	s_mov_b32	s_save_tmp, 0
	write_hwreg_to_v2(s_save_tmp)
#endif

	s_getreg_b32	s_save_m0, hwreg(HW_REG_WAVE_MODE)

#if HAVE_BANKED_VGPRS
	s_bfe_u32	s_save_tmp, s_save_pc_hi, (S_SAVE_PC_HI_DST_SRC0_SRC1_VGPR_MSB_SHIFT | (S_SAVE_PC_HI_DST_SRC0_SRC1_VGPR_MSB_SIZE << 0x10))
	s_lshl_b32	s_save_tmp, s_save_tmp, SQ_WAVE_MODE_DST_SRC0_SRC1_VGPR_MSB_SHIFT
	s_or_b32	s_save_m0, s_save_m0, s_save_tmp
#endif

	write_hwreg_to_v2(s_save_m0)

	s_getreg_b32	s_save_m0, hwreg(HW_REG_WAVE_SCRATCH_BASE_LO)
	write_hwreg_to_v2(s_save_m0)

	s_getreg_b32	s_save_m0, hwreg(HW_REG_WAVE_SCRATCH_BASE_HI)
	write_hwreg_to_v2(s_save_m0)

	s_getreg_b32	s_save_m0, hwreg(HW_REG_WAVE_EXCP_FLAG_USER)
	write_hwreg_to_v2(s_save_m0)

	s_getreg_b32	s_save_m0, hwreg(HW_REG_WAVE_TRAP_CTRL)
	write_hwreg_to_v2(s_save_m0)

	s_getreg_b32	s_save_tmp, hwreg(HW_REG_WAVE_STATUS)
	write_hwreg_to_v2(s_save_tmp)

	s_get_barrier_state s_save_tmp, -1
	s_wait_kmcnt (0)
	write_hwreg_to_v2(s_save_tmp)

#if HAVE_CLUSTER_BARRIER
	s_sendmsg_rtn_b32	s_save_tmp, sendmsg(MSG_RTN_GET_CLUSTER_BARRIER_STATE)
	s_wait_kmcnt	0
	write_hwreg_to_v2(s_save_tmp)
#endif

#if ASIC_FAMILY >= CHIP_GC_12_0_3
	s_getreg_b32	s_save_tmp, hwreg(HW_REG_WAVE_SCHED_MODE)
	write_hwreg_to_v2(s_save_tmp)
#endif

#if ! SAVE_TTMPS_IN_SGPR_BLOCK
	// Write HWREGs with 16 VGPR lanes. TTMPs occupy space after this.
	s_mov_b32       exec_lo, 0xFFFF
#else
	// All 128 bytes are available for HWREGs.
	s_mov_b32       exec_lo, 0xFFFFFFFF
#endif
	s_mov_b32	exec_hi, 0x0
	s_add_u32	s_save_addr_lo, s_save_base_addr_lo, s_save_mem_offset
	s_addc_u32	s_save_addr_hi, s_save_base_addr_hi, 0x0
	global_store_addtid_b32	v2, [s_save_addr_lo, s_save_addr_hi] scope:SCOPE_SYS

	// Write SGPRs with 32 VGPR lanes. This works in wave32 and wave64 mode.
	s_mov_b32       exec_lo, 0xFFFFFFFF

#if NUM_NAMED_BARRIERS
	v_mov_b32	v2, 0

	for var bar_idx = 0; bar_idx < NUM_NAMED_BARRIERS; bar_idx ++
		s_get_barrier_state s_save_tmp, (bar_idx + 1)
		s_wait_kmcnt	0
		v_writelane_b32	v2, s_save_tmp, bar_idx
	end

	global_store_addtid_b32	v2, [s_save_addr_lo, s_save_addr_hi] scope:SCOPE_SYS offset:NAMED_BARRIERS_SR_OFFSET_FROM_HWREG
#endif

	/* save SGPRs */
	// Save SGPR before LDS save, then the s0 to s4 can be used during LDS save...

	// SGPR SR memory offset : size(VGPR)
	get_vgpr_size_bytes(s_save_mem_offset, s_wave_size)

	s_mov_b32	ttmp13, 0x0						//next VGPR lane to copy SGPR into

	s_mov_b32	m0, 0x0							//SGPR initial index value =0
	s_nop		0x0							//Manually inserted wait states
L_SAVE_SGPR_LOOP:
	// SGPR is allocated in 16 SGPR granularity
	s_movrels_b64	s0, s0							//s0 = s[0+m0], s1 = s[1+m0]
	s_movrels_b64	s2, s2							//s2 = s[2+m0], s3 = s[3+m0]
	s_movrels_b64	s4, s4							//s4 = s[4+m0], s5 = s[5+m0]
	s_movrels_b64	s6, s6							//s6 = s[6+m0], s7 = s[7+m0]
	s_movrels_b64	s8, s8							//s8 = s[8+m0], s9 = s[9+m0]
	s_movrels_b64	s10, s10						//s10 = s[10+m0], s11 = s[11+m0]
	s_movrels_b64	s12, s12						//s12 = s[12+m0], s13 = s[13+m0]
	s_movrels_b64	s14, s14						//s14 = s[14+m0], s15 = s[15+m0]

	write_16sgpr_to_v2(s0)

	s_cmp_eq_u32	ttmp13, 0x20						//have 32 VGPR lanes filled?
	s_cbranch_scc0	L_SAVE_SGPR_SKIP_TCP_STORE

	s_add_u32	s_save_addr_lo, s_save_base_addr_lo, s_save_mem_offset
	s_addc_u32	s_save_addr_hi, s_save_base_addr_hi, 0x0
	global_store_addtid_b32	v2, [s_save_addr_lo, s_save_addr_hi] scope:SCOPE_SYS
	s_add_u32	s_save_mem_offset, s_save_mem_offset, 0x80
	s_mov_b32	ttmp13, 0x0
	v_mov_b32	v2, 0x0
L_SAVE_SGPR_SKIP_TCP_STORE:

	s_add_u32	m0, m0, 16						//next sgpr index
	s_cmp_lt_u32	m0, 96							//scc = (m0 < first 96 SGPR) ? 1 : 0
	s_cbranch_scc1	L_SAVE_SGPR_LOOP					//first 96 SGPR save is complete?

	//save the rest 12 SGPR
	s_movrels_b64	s0, s0							//s0 = s[0+m0], s1 = s[1+m0]
	s_movrels_b64	s2, s2							//s2 = s[2+m0], s3 = s[3+m0]
	s_movrels_b64	s4, s4							//s4 = s[4+m0], s5 = s[5+m0]
	s_movrels_b64	s6, s6							//s6 = s[6+m0], s7 = s[7+m0]
	s_movrels_b64	s8, s8							//s8 = s[8+m0], s9 = s[9+m0]
	s_movrels_b64	s10, s10						//s10 = s[10+m0], s11 = s[11+m0]
	write_12sgpr_to_v2(s0)

#if SAVE_TTMPS_IN_SGPR_BLOCK
	// Last 16 dwords of the SGPR block already contain the TTMPS.  Make
	// sure to not override them.
	s_mov_b32	exec_lo, 0xFFFF
#endif
	s_add_u32	s_save_addr_lo, s_save_base_addr_lo, s_save_mem_offset
	s_addc_u32	s_save_addr_hi, s_save_base_addr_hi, 0x0
	global_store_addtid_b32	v2, [s_save_addr_lo, s_save_addr_hi] scope:SCOPE_SYS

	/* save LDS */

L_SAVE_LDS:
	// Change EXEC to all threads...
	s_mov_b32	exec_lo, 0xFFFFFFFF					//need every thread from now on
	s_lshr_b32	m0, s_wave_size, S_WAVE_SIZE
	s_and_b32	m0, m0, 1
	s_cmp_eq_u32	m0, 1
	s_cbranch_scc1	L_ENABLE_SAVE_LDS_EXEC_HI
	s_mov_b32	exec_hi, 0x00000000
	s_branch	L_SAVE_LDS_NORMAL
L_ENABLE_SAVE_LDS_EXEC_HI:
	s_mov_b32	exec_hi, 0xFFFFFFFF
L_SAVE_LDS_NORMAL:
	s_getreg_b32	s_save_alloc_size, hwreg(HW_REG_WAVE_LDS_ALLOC,SQ_WAVE_LDS_ALLOC_LDS_SIZE_SHIFT,SQ_WAVE_LDS_ALLOC_LDS_SIZE_SIZE)
	s_and_b32	s_save_alloc_size, s_save_alloc_size, 0xFFFFFFFF	//lds_size is zero?
	s_cbranch_scc0	L_SAVE_LDS_DONE						//no lds used? jump to L_SAVE_DONE

	s_and_b32	s_save_tmp, s_save_pc_hi, S_SAVE_PC_HI_FIRST_WAVE_MASK
	s_cbranch_scc0	L_SAVE_LDS_DONE

	// first wave do LDS save;

	s_lshl_b32	s_save_alloc_size, s_save_alloc_size, SQ_WAVE_LDS_ALLOC_GRANULARITY

	// LDS at offset: size(VGPR)+SIZE(SGPR)+SIZE(HWREG)
	//
	get_vgpr_size_bytes(s_save_mem_offset, s_wave_size)
	s_add_u32	s_save_mem_offset, s_save_mem_offset, get_sgpr_size_bytes()
	s_add_u32	s_save_mem_offset, s_save_mem_offset, get_hwreg_size_bytes()

	//load 0~63*4(byte address) to vgpr v0
	v_mbcnt_lo_u32_b32	v0, -1, 0
	v_mbcnt_hi_u32_b32	v0, -1, v0
	v_mul_u32_u24	v0, 4, v0

	s_lshr_b32	m0, s_wave_size, S_WAVE_SIZE
	s_and_b32	m0, m0, 1
	s_cmp_eq_u32	m0, 1
	s_mov_b32	m0, 0x0
	s_cbranch_scc1	L_SAVE_LDS_W64

L_SAVE_LDS_W32:
	s_mov_b32	s3, 128
	s_nop		0
	s_nop		0
	s_nop		0
L_SAVE_LDS_LOOP_W32:
	ds_read_b32	v1, v0
	s_wait_idle
	s_add_u32	s_save_addr_lo, s_save_base_addr_lo, s_save_mem_offset
	s_addc_u32	s_save_addr_hi, s_save_base_addr_hi, 0x0
	global_store_addtid_b32	v1, [s_save_addr_lo, s_save_addr_hi] scope:SCOPE_SYS

	s_add_u32	m0, m0, s3						//every buffer_store_lds does 128 bytes
	s_add_u32	s_save_mem_offset, s_save_mem_offset, s3
	v_add_nc_u32	v0, v0, 128						//mem offset increased by 128 bytes
	s_cmp_lt_u32	m0, s_save_alloc_size					//scc=(m0 < s_save_alloc_size) ? 1 : 0
	s_cbranch_scc1	L_SAVE_LDS_LOOP_W32					//LDS save is complete?

	s_branch	L_SAVE_LDS_DONE

L_SAVE_LDS_W64:
	s_mov_b32	s3, 256
	s_nop		0
	s_nop		0
	s_nop		0
L_SAVE_LDS_LOOP_W64:
	ds_read_b32	v1, v0
	s_wait_idle
	s_add_u32	s_save_addr_lo, s_save_base_addr_lo, s_save_mem_offset
	s_addc_u32	s_save_addr_hi, s_save_base_addr_hi, 0x0
	global_store_addtid_b32	v1, [s_save_addr_lo, s_save_addr_hi] scope:SCOPE_SYS

	s_add_u32	m0, m0, s3						//every buffer_store_lds does 256 bytes
	s_add_u32	s_save_mem_offset, s_save_mem_offset, s3
	v_add_nc_u32	v0, v0, 256						//mem offset increased by 256 bytes
	s_cmp_lt_u32	m0, s_save_alloc_size					//scc=(m0 < s_save_alloc_size) ? 1 : 0
	s_cbranch_scc1	L_SAVE_LDS_LOOP_W64					//LDS save is complete?

L_SAVE_LDS_DONE:
	/* save VGPRs  - set the Rest VGPRs */
L_SAVE_VGPR:
	// VGPR SR memory offset: 0
	s_mov_b32	exec_lo, 0xFFFFFFFF					//need every thread from now on
	s_lshr_b32	m0, s_wave_size, S_WAVE_SIZE
	s_and_b32	m0, m0, 1
	s_cmp_eq_u32	m0, 1
	s_cbranch_scc1	L_ENABLE_SAVE_VGPR_EXEC_HI
	s_mov_b32	s_save_mem_offset, (0+128*4)				// for the rest VGPRs
	s_mov_b32	exec_hi, 0x00000000
	s_branch	L_SAVE_VGPR_NORMAL
L_ENABLE_SAVE_VGPR_EXEC_HI:
	s_mov_b32	s_save_mem_offset, (0+256*4)				// for the rest VGPRs
	s_mov_b32	exec_hi, 0xFFFFFFFF
L_SAVE_VGPR_NORMAL:
	s_getreg_b32	s_save_alloc_size, hwreg(HW_REG_WAVE_GPR_ALLOC,SQ_WAVE_GPR_ALLOC_VGPR_SIZE_SHIFT,SQ_WAVE_GPR_ALLOC_VGPR_SIZE_SIZE)
	s_add_u32	s_save_alloc_size, s_save_alloc_size, 1
	s_lshl_b32	s_save_alloc_size, s_save_alloc_size, 2			//Number of VGPRs = (vgpr_size + 1) * 4    (non-zero value)
	//determine it is wave32 or wave64
	s_lshr_b32	m0, s_wave_size, S_WAVE_SIZE
	s_and_b32	m0, m0, 1
	s_cmp_eq_u32	m0, 1
	s_cbranch_scc1	L_SAVE_VGPR_WAVE64

	// VGPR Allocated in 4-GPR granularity

	// VGPR store using dw burst
	s_mov_b32	m0, 0x4							//VGPR initial index value =4
	s_cmp_lt_u32	m0, s_save_alloc_size
	s_cbranch_scc0	L_SAVE_VGPR_END

L_SAVE_VGPR_W32_LOOP:
	v_movrels_b32	v0, v0							//v0 = v[0+m0]
	v_movrels_b32	v1, v1							//v1 = v[1+m0]
	v_movrels_b32	v2, v2							//v2 = v[2+m0]
	v_movrels_b32	v3, v3							//v3 = v[3+m0]

	s_add_u32	s_save_addr_lo, s_save_base_addr_lo, s_save_mem_offset
	s_addc_u32	s_save_addr_hi, s_save_base_addr_hi, 0x0
	global_store_addtid_b32	v0, [s_save_addr_lo, s_save_addr_hi] scope:SCOPE_SYS
	global_store_addtid_b32	v1, [s_save_addr_lo, s_save_addr_hi] scope:SCOPE_SYS offset:128
	global_store_addtid_b32	v2, [s_save_addr_lo, s_save_addr_hi] scope:SCOPE_SYS offset:128*2
	global_store_addtid_b32	v3, [s_save_addr_lo, s_save_addr_hi] scope:SCOPE_SYS offset:128*3

	s_add_u32	m0, m0, 4						//next vgpr index
	s_add_u32	s_save_mem_offset, s_save_mem_offset, 128*4		//every buffer_store_dword does 128 bytes
	s_cmp_lt_u32	m0, s_save_alloc_size					//scc = (m0 < s_save_alloc_size) ? 1 : 0
	s_cbranch_scc1	L_SAVE_VGPR_W32_LOOP					//VGPR save is complete?

	s_branch	L_SAVE_VGPR_END

L_SAVE_VGPR_WAVE64:
	// VGPR store using dw burst
	s_mov_b32	m0, 0x4							//VGPR initial index value =4
	s_cmp_lt_u32	m0, s_save_alloc_size
	s_cbranch_scc0	L_SAVE_VGPR_END

L_SAVE_VGPR_W64_LOOP:
	v_movrels_b32	v0, v0							//v0 = v[0+m0]
	v_movrels_b32	v1, v1							//v1 = v[1+m0]
	v_movrels_b32	v2, v2							//v2 = v[2+m0]
	v_movrels_b32	v3, v3							//v3 = v[3+m0]

	s_add_u32	s_save_addr_lo, s_save_base_addr_lo, s_save_mem_offset
	s_addc_u32	s_save_addr_hi, s_save_base_addr_hi, 0x0
	global_store_addtid_b32	v0, [s_save_addr_lo, s_save_addr_hi] scope:SCOPE_SYS
	global_store_addtid_b32	v1, [s_save_addr_lo, s_save_addr_hi] scope:SCOPE_SYS offset:256
	global_store_addtid_b32	v2, [s_save_addr_lo, s_save_addr_hi] scope:SCOPE_SYS offset:256*2
	global_store_addtid_b32	v3, [s_save_addr_lo, s_save_addr_hi] scope:SCOPE_SYS offset:256*3

	s_add_u32	m0, m0, 4						//next vgpr index
	s_add_u32	s_save_mem_offset, s_save_mem_offset, 256*4		//every buffer_store_dword does 256 bytes
	s_cmp_lt_u32	m0, s_save_alloc_size					//scc = (m0 < s_save_alloc_size) ? 1 : 0
	s_cbranch_scc1	L_SAVE_VGPR_W64_LOOP					//VGPR save is complete?

L_SAVE_VGPR_END:
	s_branch	L_END_PGM

L_RESTORE:
	s_mov_b32	s_restore_base_addr_lo, s_restore_spi_init_lo
	s_and_b32	s_restore_base_addr_hi, s_restore_spi_init_hi, ADDRESS_HI32_MASK

	// Save s_restore_spi_init_hi for later use.
	s_mov_b32 s_restore_spi_init_hi_save, s_restore_spi_init_hi

	//determine it is wave32 or wave64
	get_wave_size2(s_restore_size)

	s_and_b32	s_restore_tmp, s_restore_spi_init_hi, S_RESTORE_SPI_INIT_FIRST_WAVE_MASK
	s_cbranch_scc0	L_RESTORE_VGPR

	/* restore LDS */
L_RESTORE_LDS:
	s_mov_b32	exec_lo, 0xFFFFFFFF					//need every thread from now on
	s_lshr_b32	m0, s_restore_size, S_WAVE_SIZE
	s_and_b32	m0, m0, 1
	s_cmp_eq_u32	m0, 1
	s_cbranch_scc1	L_ENABLE_RESTORE_LDS_EXEC_HI
	s_mov_b32	exec_hi, 0x00000000
	s_branch	L_RESTORE_LDS_NORMAL
L_ENABLE_RESTORE_LDS_EXEC_HI:
	s_mov_b32	exec_hi, 0xFFFFFFFF
L_RESTORE_LDS_NORMAL:
	s_getreg_b32	s_restore_alloc_size, hwreg(HW_REG_WAVE_LDS_ALLOC,SQ_WAVE_LDS_ALLOC_LDS_SIZE_SHIFT,SQ_WAVE_LDS_ALLOC_LDS_SIZE_SIZE)
	s_and_b32	s_restore_alloc_size, s_restore_alloc_size, 0xFFFFFFFF	//lds_size is zero?
	s_cbranch_scc0	L_RESTORE_VGPR						//no lds used? jump to L_RESTORE_VGPR
	s_lshl_b32	s_restore_alloc_size, s_restore_alloc_size, SQ_WAVE_LDS_ALLOC_GRANULARITY

	// LDS at offset: size(VGPR)+SIZE(SGPR)+SIZE(HWREG)
	//
	get_vgpr_size_bytes(s_restore_mem_offset, s_restore_size)
	s_add_u32	s_restore_mem_offset, s_restore_mem_offset, get_sgpr_size_bytes()
	s_add_u32	s_restore_mem_offset, s_restore_mem_offset, get_hwreg_size_bytes()

	s_lshr_b32	m0, s_restore_size, S_WAVE_SIZE
	s_and_b32	m0, m0, 1
	s_cmp_eq_u32	m0, 1
	s_mov_b32	m0, 0x0

	v_mbcnt_lo_u32_b32	v1, -1, 0
	v_mbcnt_hi_u32_b32	v1, -1, v1
	v_lshlrev_b32		v1, 2, v1					// 0, 4, 8, ... 124 (W32) or 252 (W64)

	s_cbranch_scc1	L_RESTORE_LDS_LOOP_W64

L_RESTORE_LDS_LOOP_W32:
	s_add_u32	s_restore_addr_lo, s_restore_base_addr_lo, s_restore_mem_offset
	s_addc_u32	s_restore_addr_hi, s_restore_base_addr_hi, 0x0
	global_load_addtid_b32	v0, [s_restore_addr_lo, s_restore_addr_hi] scope:SCOPE_SYS
	s_wait_idle
	ds_store_b32	v1, v0
	v_add_nc_u32	v1, v1, 128
	s_add_u32	m0, m0, 128						// 128 DW
	s_add_u32	s_restore_mem_offset, s_restore_mem_offset, 128		//mem offset increased by 128DW
	s_cmp_lt_u32	m0, s_restore_alloc_size				//scc=(m0 < s_restore_alloc_size) ? 1 : 0
	s_cbranch_scc1	L_RESTORE_LDS_LOOP_W32					//LDS restore is complete?
	s_branch	L_RESTORE_VGPR

L_RESTORE_LDS_LOOP_W64:
	s_add_u32	s_restore_addr_lo, s_restore_base_addr_lo, s_restore_mem_offset
	s_addc_u32	s_restore_addr_hi, s_restore_base_addr_hi, 0x0
	global_load_addtid_b32	v0, [s_restore_addr_lo, s_restore_addr_hi] scope:SCOPE_SYS
	s_wait_idle
	ds_store_b32	v1, v0
	v_add_nc_u32	v1, v1, 256
	s_add_u32	m0, m0, 256						// 256 DW
	s_add_u32	s_restore_mem_offset, s_restore_mem_offset, 256		//mem offset increased by 256DW
	s_cmp_lt_u32	m0, s_restore_alloc_size				//scc=(m0 < s_restore_alloc_size) ? 1 : 0
	s_cbranch_scc1	L_RESTORE_LDS_LOOP_W64					//LDS restore is complete?

	/* restore VGPRs */
L_RESTORE_VGPR:
	// VGPR SR memory offset : 0
	s_mov_b32	s_restore_mem_offset, 0x0
	s_mov_b32	exec_lo, 0xFFFFFFFF					//need every thread from now on
	s_lshr_b32	m0, s_restore_size, S_WAVE_SIZE
	s_and_b32	m0, m0, 1
	s_cmp_eq_u32	m0, 1
	s_cbranch_scc1	L_ENABLE_RESTORE_VGPR_EXEC_HI
	s_mov_b32	exec_hi, 0x00000000
	s_branch	L_RESTORE_VGPR_NORMAL
L_ENABLE_RESTORE_VGPR_EXEC_HI:
	s_mov_b32	exec_hi, 0xFFFFFFFF
L_RESTORE_VGPR_NORMAL:
	s_getreg_b32	s_restore_alloc_size, hwreg(HW_REG_WAVE_GPR_ALLOC,SQ_WAVE_GPR_ALLOC_VGPR_SIZE_SHIFT,SQ_WAVE_GPR_ALLOC_VGPR_SIZE_SIZE)
	s_add_u32	s_restore_alloc_size, s_restore_alloc_size, 1
	s_lshl_b32	s_restore_alloc_size, s_restore_alloc_size, 2		//Number of VGPRs = (vgpr_size + 1) * 4    (non-zero value)
	//determine it is wave32 or wave64
	s_lshr_b32	m0, s_restore_size, S_WAVE_SIZE
	s_and_b32	m0, m0, 1
	s_cmp_eq_u32	m0, 1
	s_cbranch_scc1	L_RESTORE_VGPR_WAVE64

	// VGPR load using dw burst
	s_mov_b32	s_restore_mem_offset_save, s_restore_mem_offset		// restore start with v1, v0 will be the last
	s_add_u32	s_restore_mem_offset, s_restore_mem_offset, 128*4
	s_mov_b32	m0, 4							//VGPR initial index value = 4

L_RESTORE_VGPR_WAVE32_LOOP:
	s_add_u32	s_restore_addr_lo, s_restore_base_addr_lo, s_restore_mem_offset
	s_addc_u32	s_restore_addr_hi, s_restore_base_addr_hi, 0x0
	global_load_addtid_b32	v0, [s_restore_addr_lo, s_restore_addr_hi] scope:SCOPE_SYS
	global_load_addtid_b32	v1, [s_restore_addr_lo, s_restore_addr_hi] scope:SCOPE_SYS offset:128
	global_load_addtid_b32	v2, [s_restore_addr_lo, s_restore_addr_hi] scope:SCOPE_SYS offset:128*2
	global_load_addtid_b32	v3, [s_restore_addr_lo, s_restore_addr_hi] scope:SCOPE_SYS offset:128*3
	s_wait_idle
	v_movreld_b32	v0, v0							//v[0+m0] = v0
	v_movreld_b32	v1, v1
	v_movreld_b32	v2, v2
	v_movreld_b32	v3, v3
	s_add_u32	m0, m0, 4						//next vgpr index
	s_add_u32	s_restore_mem_offset, s_restore_mem_offset, 128*4	//every buffer_load_dword does 128 bytes
	s_cmp_lt_u32	m0, s_restore_alloc_size				//scc = (m0 < s_restore_alloc_size) ? 1 : 0
	s_cbranch_scc1	L_RESTORE_VGPR_WAVE32_LOOP				//VGPR restore (except v0) is complete?

	/* VGPR restore on v0 */
	s_add_u32	s_restore_addr_lo, s_restore_base_addr_lo, s_restore_mem_offset_save
	s_addc_u32	s_restore_addr_hi, s_restore_base_addr_hi, 0x0
	global_load_addtid_b32	v0, [s_restore_addr_lo, s_restore_addr_hi] scope:SCOPE_SYS
	global_load_addtid_b32	v1, [s_restore_addr_lo, s_restore_addr_hi] scope:SCOPE_SYS offset:128
	global_load_addtid_b32	v2, [s_restore_addr_lo, s_restore_addr_hi] scope:SCOPE_SYS offset:128*2
	global_load_addtid_b32	v3, [s_restore_addr_lo, s_restore_addr_hi] scope:SCOPE_SYS offset:128*3
	s_wait_idle

	s_branch	L_RESTORE_SGPR

L_RESTORE_VGPR_WAVE64:
	// VGPR load using dw burst
	s_mov_b32	s_restore_mem_offset_save, s_restore_mem_offset		// restore start with v4, v0 will be the last
	s_add_u32	s_restore_mem_offset, s_restore_mem_offset, 256*4
	s_mov_b32	m0, 4							//VGPR initial index value = 4
	s_cmp_lt_u32	m0, s_restore_alloc_size
	s_cbranch_scc0	L_RESTORE_V0

L_RESTORE_VGPR_WAVE64_LOOP:
	s_add_u32	s_restore_addr_lo, s_restore_base_addr_lo, s_restore_mem_offset
	s_addc_u32	s_restore_addr_hi, s_restore_base_addr_hi, 0x0
	global_load_addtid_b32	v0, [s_restore_addr_lo, s_restore_addr_hi] scope:SCOPE_SYS
	global_load_addtid_b32	v1, [s_restore_addr_lo, s_restore_addr_hi] scope:SCOPE_SYS offset:256
	global_load_addtid_b32	v2, [s_restore_addr_lo, s_restore_addr_hi] scope:SCOPE_SYS offset:256*2
	global_load_addtid_b32	v3, [s_restore_addr_lo, s_restore_addr_hi] scope:SCOPE_SYS offset:256*3
	s_wait_idle
	v_movreld_b32	v0, v0							//v[0+m0] = v0
	v_movreld_b32	v1, v1
	v_movreld_b32	v2, v2
	v_movreld_b32	v3, v3
	s_add_u32	m0, m0, 4						//next vgpr index
	s_add_u32	s_restore_mem_offset, s_restore_mem_offset, 256*4	//every buffer_load_dword does 256 bytes
	s_cmp_lt_u32	m0, s_restore_alloc_size				//scc = (m0 < s_restore_alloc_size) ? 1 : 0
	s_cbranch_scc1	L_RESTORE_VGPR_WAVE64_LOOP				//VGPR restore (except v0) is complete?

	/* VGPR restore on v0 */
L_RESTORE_V0:
	s_add_u32	s_restore_addr_lo, s_restore_base_addr_lo, s_restore_mem_offset_save
	s_addc_u32	s_restore_addr_hi, s_restore_base_addr_hi, 0x0
	global_load_addtid_b32	v0, [s_restore_addr_lo, s_restore_addr_hi] scope:SCOPE_SYS
	global_load_addtid_b32	v1, [s_restore_addr_lo, s_restore_addr_hi] scope:SCOPE_SYS offset:256
	global_load_addtid_b32	v2, [s_restore_addr_lo, s_restore_addr_hi] scope:SCOPE_SYS offset:256*2
	global_load_addtid_b32	v3, [s_restore_addr_lo, s_restore_addr_hi] scope:SCOPE_SYS offset:256*3
	s_wait_idle

	/* restore SGPRs */
	//will be 2+8+16*6
	// SGPR SR memory offset : size(VGPR)
L_RESTORE_SGPR:
	get_vgpr_size_bytes(s_restore_mem_offset, s_restore_size)
	s_add_u32	s_restore_mem_offset, s_restore_mem_offset, get_sgpr_size_bytes()
	s_sub_u32	s_restore_mem_offset, s_restore_mem_offset, 24*4	// s[104:107]
	s_add_u32	s_restore_addr_lo, s_restore_base_addr_lo, s_restore_mem_offset
	s_addc_u32	s_restore_addr_hi, s_restore_base_addr_hi, 0x0

	s_mov_b32	m0, s_sgpr_save_num

	s_load_b128	s0, [s_restore_addr_lo, s_restore_addr_hi], 0x0 scope:SCOPE_SYS
	s_wait_idle

	s_sub_u32	m0, m0, 4						// Restore from S[0] to S[104]
	s_nop		0							// hazard SALU M0=> S_MOVREL

	s_movreld_b64	s0, s0							//s[0+m0] = s0
	s_movreld_b64	s2, s2

	s_sub_co_u32	s_restore_addr_lo, s_restore_addr_lo, 8*4		// s[96:103]
	s_sub_co_ci_u32	s_restore_addr_hi, s_restore_addr_hi, 0
	s_load_b256	s0, [s_restore_addr_lo, s_restore_addr_hi], 0x0 scope:SCOPE_SYS
	s_wait_idle

	s_sub_u32	m0, m0, 8						// Restore from S[0] to S[96]
	s_nop		0							// hazard SALU M0=> S_MOVREL

	s_movreld_b64	s0, s0							//s[0+m0] = s0
	s_movreld_b64	s2, s2
	s_movreld_b64	s4, s4
	s_movreld_b64	s6, s6

 L_RESTORE_SGPR_LOOP:
	s_sub_co_u32	s_restore_addr_lo, s_restore_addr_lo, 16*4		// s[0,16,32,48,64,80]
	s_sub_co_ci_u32	s_restore_addr_hi, s_restore_addr_hi, 0
	s_load_b512	s0, [s_restore_addr_lo, s_restore_addr_hi], 0x0 scope:SCOPE_SYS
	s_wait_idle

	s_sub_u32	m0, m0, 16						// Restore from S[n] to S[0]
	s_nop		0							// hazard SALU M0=> S_MOVREL

	s_movreld_b64	s0, s0							//s[0+m0] = s0
	s_movreld_b64	s2, s2
	s_movreld_b64	s4, s4
	s_movreld_b64	s6, s6
	s_movreld_b64	s8, s8
	s_movreld_b64	s10, s10
	s_movreld_b64	s12, s12
	s_movreld_b64	s14, s14

	s_cmp_eq_u32	m0, 0							//scc = (m0 < s_sgpr_save_num) ? 1 : 0
	s_cbranch_scc0	L_RESTORE_SGPR_LOOP

	// s_barrier with STATE_PRIV.TRAP_AFTER_INST=1, STATUS.PRIV=1 incorrectly asserts debug exception.
	// Clear DEBUG_EN before and restore MODE after the barrier.
	s_setreg_imm32_b32	hwreg(HW_REG_WAVE_MODE), 0

	/* restore HW registers */
L_RESTORE_HWREG:
	// HWREG SR memory offset : size(VGPR)+size(SGPR)
	get_vgpr_size_bytes(s_restore_mem_offset, s_restore_size)
	s_add_u32	s_restore_mem_offset, s_restore_mem_offset, get_sgpr_size_bytes()
	s_add_u32	s_restore_addr_lo, s_restore_base_addr_lo, s_restore_mem_offset
	s_addc_u32	s_restore_addr_hi, s_restore_base_addr_hi, 0x0

	// Restore s_restore_spi_init_hi before the saved value gets clobbered.
	s_mov_b32 s_restore_spi_init_hi, s_restore_spi_init_hi_save

	s_load_b32	s_restore_m0, [s_restore_addr_lo, s_restore_addr_hi], null scope:SCOPE_SYS
	s_load_b32	s_restore_pc_lo, [s_restore_addr_lo, s_restore_addr_hi], null scope:SCOPE_SYS offset:0x4
	s_load_b32	s_restore_pc_hi, [s_restore_addr_lo, s_restore_addr_hi], null scope:SCOPE_SYS offset:0x8
	s_load_b32	s_restore_exec_lo, [s_restore_addr_lo, s_restore_addr_hi], null scope:SCOPE_SYS offset:0xC
	s_load_b32	s_restore_exec_hi, [s_restore_addr_lo, s_restore_addr_hi], null scope:SCOPE_SYS offset:0x10
	s_load_b32	s_restore_state_priv, [s_restore_addr_lo, s_restore_addr_hi], null scope:SCOPE_SYS offset:0x14
	s_load_b32	s_restore_excp_flag_priv, [s_restore_addr_lo, s_restore_addr_hi], null scope:SCOPE_SYS offset:0x18
	s_load_b32	s_restore_xnack_mask, [s_restore_addr_lo, s_restore_addr_hi], null scope:SCOPE_SYS offset:0x1C
	s_load_b32	s_restore_mode, [s_restore_addr_lo, s_restore_addr_hi], null scope:SCOPE_SYS offset:0x20
	s_load_b32	s_restore_flat_scratch, [s_restore_addr_lo, s_restore_addr_hi], null scope:SCOPE_SYS offset:0x24
	s_wait_idle

	s_setreg_b32	hwreg(HW_REG_WAVE_SCRATCH_BASE_LO), s_restore_flat_scratch

	s_load_b32	s_restore_flat_scratch, [s_restore_addr_lo, s_restore_addr_hi], null scope:SCOPE_SYS offset:0x28
	s_wait_idle

	s_setreg_b32	hwreg(HW_REG_WAVE_SCRATCH_BASE_HI), s_restore_flat_scratch

	s_load_b32	s_restore_tmp, [s_restore_addr_lo, s_restore_addr_hi], null scope:SCOPE_SYS offset:0x2C
	s_wait_idle
	s_setreg_b32	hwreg(HW_REG_WAVE_EXCP_FLAG_USER), s_restore_tmp

	s_load_b32	s_restore_tmp, [s_restore_addr_lo, s_restore_addr_hi], null scope:SCOPE_SYS offset:0x30
	s_wait_idle
	s_setreg_b32	hwreg(HW_REG_WAVE_TRAP_CTRL), s_restore_tmp

	// Only the first wave needs to restore group barriers.
	s_and_b32	s_restore_tmp, s_restore_spi_init_hi, S_RESTORE_SPI_INIT_FIRST_WAVE_MASK
	s_cbranch_scc0	L_SKIP_GROUP_BARRIER_RESTORE

	// Skip over WAVE_STATUS, since there is no state to restore from it

	s_load_b32	s_restore_tmp, [s_restore_addr_lo, s_restore_addr_hi], null scope:SCOPE_SYS offset:0x38
	s_wait_idle

	// Skip group barriers if wave is not part of a group.
	s_bitcmp1_b32	s_restore_tmp, BARRIER_STATE_VALID_OFFSET
	s_cbranch_scc0	L_SKIP_GROUP_BARRIER_RESTORE

	// Restore workgroup barrier signal count.
	restore_barrier_signal_count(-1)

#if NUM_NAMED_BARRIERS
	s_mov_b32	s_restore_mem_offset, NAMED_BARRIERS_SR_OFFSET_FROM_HWREG
	s_mov_b32	m0, 1

L_RESTORE_NAMED_BARRIER_LOOP:
	s_load_b32	s_restore_tmp, [s_restore_addr_lo, s_restore_addr_hi], s_restore_mem_offset scope:SCOPE_SYS
	s_wait_kmcnt	0
	s_add_u32	s_restore_mem_offset, s_restore_mem_offset, 0x4

	// Restore named barrier member count.
	s_bfe_u32	exec_lo, s_restore_tmp, (BARRIER_STATE_MEMBER_OFFSET | (BARRIER_STATE_MEMBER_SIZE << 16))
	s_lshl_b32	exec_lo, exec_lo, S_BARRIER_INIT_MEMBERCNT_SHIFT
	s_or_b32	m0, m0, exec_lo
	s_barrier_init	m0
	s_andn2_b32	m0, m0, S_BARRIER_INIT_MEMBERCNT_MASK

	// Restore named barrier signal count.
	restore_barrier_signal_count(m0)

	s_add_u32	m0, m0, 1
	s_cmp_gt_u32	m0, NUM_NAMED_BARRIERS
	s_cbranch_scc0	L_RESTORE_NAMED_BARRIER_LOOP
#endif

L_SKIP_GROUP_BARRIER_RESTORE:
#if HAVE_CLUSTER_BARRIER
	s_load_b32	s_restore_tmp, [s_restore_addr_lo, s_restore_addr_hi], null scope:SCOPE_SYS offset:0x3C
	s_wait_kmcnt	0

	// Skip cluster barrier restore if wave is not part of a cluster.
	s_bitcmp1_b32	s_restore_tmp, BARRIER_STATE_VALID_OFFSET
	s_cbranch_scc0	L_SKIP_CLUSTER_BARRIER_RESTORE

	// Only the first wave in the group signals the trap cluster barrier.
	s_bitcmp1_b32	s_restore_spi_init_hi, S_RESTORE_SPI_INIT_FIRST_WAVE_SHIFT
	s_cbranch_scc0	L_SKIP_TRAP_CLUSTER_BARRIER_SIGNAL

	// Clear SCC: s_barrier_signal_isfirst -4 writes SCC=>1 but not SCC=>0.
	s_cmp_eq_u32	0, 1
	s_barrier_signal_isfirst	-4
L_SKIP_TRAP_CLUSTER_BARRIER_SIGNAL:
	s_barrier_wait	-4

	// Only the first wave in the cluster restores the barrier.
	s_cbranch_scc0	L_SKIP_CLUSTER_BARRIER_RESTORE

	// Restore cluster barrier signal count.
	restore_barrier_signal_count(-3)
L_SKIP_CLUSTER_BARRIER_RESTORE:
#endif

#if ASIC_FAMILY >= CHIP_GC_12_0_3
	s_load_b32	s_restore_tmp, [s_restore_addr_lo, s_restore_addr_hi], null scope:SCOPE_SYS offset:0x40
	s_wait_kmcnt	0
	s_setreg_b32	hwreg(HW_REG_WAVE_SCHED_MODE), s_restore_tmp
#endif

	s_mov_b32	m0, s_restore_m0
	s_mov_b32	exec_lo, s_restore_exec_lo
	s_mov_b32	exec_hi, s_restore_exec_hi

#if HAVE_XNACK
	s_setreg_b32	hwreg(HW_REG_WAVE_XNACK_MASK), s_restore_xnack_mask
#endif

	// EXCP_FLAG_PRIV.SAVE_CONTEXT and HOST_TRAP may have changed.
	// Only restore the other fields to avoid clobbering them.
	s_setreg_b32	hwreg(HW_REG_WAVE_EXCP_FLAG_PRIV, 0, SQ_WAVE_EXCP_FLAG_PRIV_RESTORE_PART_1_SIZE), s_restore_excp_flag_priv
	s_lshr_b32	s_restore_excp_flag_priv, s_restore_excp_flag_priv, SQ_WAVE_EXCP_FLAG_PRIV_RESTORE_PART_2_SHIFT
	s_setreg_b32	hwreg(HW_REG_WAVE_EXCP_FLAG_PRIV, SQ_WAVE_EXCP_FLAG_PRIV_RESTORE_PART_2_SHIFT, SQ_WAVE_EXCP_FLAG_PRIV_RESTORE_PART_2_SIZE), s_restore_excp_flag_priv
	s_lshr_b32	s_restore_excp_flag_priv, s_restore_excp_flag_priv, SQ_WAVE_EXCP_FLAG_PRIV_RESTORE_PART_3_SHIFT - SQ_WAVE_EXCP_FLAG_PRIV_RESTORE_PART_2_SHIFT
	s_setreg_b32	hwreg(HW_REG_WAVE_EXCP_FLAG_PRIV, SQ_WAVE_EXCP_FLAG_PRIV_RESTORE_PART_3_SHIFT, SQ_WAVE_EXCP_FLAG_PRIV_RESTORE_PART_3_SIZE), s_restore_excp_flag_priv

	s_setreg_b32	hwreg(HW_REG_WAVE_MODE), s_restore_mode

	// Restore trap temporaries 4-11, 13 initialized by SPI debug dispatch logic
	// ttmp SR memory offset :
	// - gfx12:   size(VGPR)+size(SGPR)+0x40
	// - gfx12.5: size(VGPR)+size(SGPR)-0x40
	get_vgpr_size_bytes(s_restore_ttmps_lo, s_restore_size)
	s_add_u32	s_restore_ttmps_lo, s_restore_ttmps_lo, (get_sgpr_size_bytes() + TTMP_SR_OFFSET_FROM_HWREG)
	s_add_u32	s_restore_ttmps_lo, s_restore_ttmps_lo, s_restore_base_addr_lo
	s_addc_u32	s_restore_ttmps_hi, s_restore_base_addr_hi, 0x0
	s_load_dwordx4	[ttmp4, ttmp5, ttmp6, ttmp7], [s_restore_ttmps_lo, s_restore_ttmps_hi], 0x10 scope:SCOPE_SYS
	s_load_dwordx4	[ttmp8, ttmp9, ttmp10, ttmp11], [s_restore_ttmps_lo, s_restore_ttmps_hi], 0x20 scope:SCOPE_SYS
	s_load_dword	ttmp13, [s_restore_ttmps_lo, s_restore_ttmps_hi], 0x34 scope:SCOPE_SYS
	s_wait_idle

#if HAVE_XNACK
	restore_xnack_state_priv(s_restore_tmp)
#endif

	s_and_b32	s_restore_pc_hi, s_restore_pc_hi, ADDRESS_HI32_MASK	//Do it here in order not to affect STATUS
	s_and_b64	exec, exec, exec					// Restore STATUS.EXECZ, not writable by s_setreg_b32
	s_and_b64	vcc, vcc, vcc						// Restore STATUS.VCCZ, not writable by s_setreg_b32

#if RELAXED_SCHEDULING_IN_TRAP
	// Assume relaxed scheduling mode after this point.
	restore_sched_mode(s_restore_tmp)
#endif

	s_setreg_b32	hwreg(HW_REG_WAVE_STATE_PRIV), s_restore_state_priv	// SCC is included, which is changed by previous salu

	// Make barrier and LDS state visible to all waves in the group/cluster.
	// STATE_PRIV.*BARRIER_COMPLETE may change after this point.
	wait_trap_barriers(s_restore_tmp, 0, 0)

#if HAVE_CLUSTER_BARRIER
	// SCC is changed by wait_trap_barriers, restore it separately.
	s_lshr_b32	s_restore_state_priv, s_restore_state_priv, SQ_WAVE_STATE_PRIV_SCC_SHIFT
	s_setreg_b32	hwreg(HW_REG_WAVE_STATE_PRIV, SQ_WAVE_STATE_PRIV_SCC_SHIFT, 1), s_restore_state_priv
#endif

	s_rfe_b64	s_restore_pc_lo						//Return to the main shader program and resume execution

L_END_PGM:
	// Make sure that no wave of the group/cluster can exit the trap handler
	// before the group/cluster barrier state is saved.
	wait_trap_barriers(s_restore_tmp, 0, 0)

	s_endpgm_saved
end

function write_hwreg_to_v2(s)
	// Copy into VGPR for later TCP store.
	v_writelane_b32	v2, s, m0
	s_add_u32	m0, m0, 0x1
end


function write_16sgpr_to_v2(s)
	// Copy into VGPR for later TCP store.
	for var sgpr_idx = 0; sgpr_idx < 16; sgpr_idx ++
		v_writelane_b32	v2, s[sgpr_idx], ttmp13
		s_add_u32	ttmp13, ttmp13, 0x1
	end
end

function write_12sgpr_to_v2(s)
	// Copy into VGPR for later TCP store.
	for var sgpr_idx = 0; sgpr_idx < 12; sgpr_idx ++
		v_writelane_b32	v2, s[sgpr_idx], ttmp13
		s_add_u32	ttmp13, ttmp13, 0x1
	end
end

function get_vgpr_size_bytes(s_vgpr_size_byte, s_size)
	s_getreg_b32	s_vgpr_size_byte, hwreg(HW_REG_WAVE_GPR_ALLOC,SQ_WAVE_GPR_ALLOC_VGPR_SIZE_SHIFT,SQ_WAVE_GPR_ALLOC_VGPR_SIZE_SIZE)
	s_add_u32	s_vgpr_size_byte, s_vgpr_size_byte, 1
	s_bitcmp1_b32	s_size, S_WAVE_SIZE
	s_cbranch_scc1	L_ENABLE_SHIFT_W64
	s_lshl_b32	s_vgpr_size_byte, s_vgpr_size_byte, (2+7)		//Number of VGPRs = (vgpr_size + 1) * 4 * 32 * 4   (non-zero value)
	s_branch	L_SHIFT_DONE
L_ENABLE_SHIFT_W64:
	s_lshl_b32	s_vgpr_size_byte, s_vgpr_size_byte, (2+8)		//Number of VGPRs = (vgpr_size + 1) * 4 * 64 * 4   (non-zero value)
L_SHIFT_DONE:
end

function get_sgpr_size_bytes
	return 512
end

function get_hwreg_size_bytes
#if ASIC_FAMILY >= CHIP_GC_12_0_3
	return 512
#else
	return 128
#endif
end

function get_wave_size2(s_reg)
	s_getreg_b32	s_reg, hwreg(HW_REG_WAVE_STATUS,SQ_WAVE_STATUS_WAVE64_SHIFT,SQ_WAVE_STATUS_WAVE64_SIZE)
	s_lshl_b32	s_reg, s_reg, S_WAVE_SIZE
end

#if HAVE_XNACK
function save_and_clear_xnack_state_priv(s_tmp)
	// Preserve and clear XNACK state before issuing further translations.
	// Save XNACK_STATE_PRIV.{FIRST_REPLAY, REPLAY_W64H, FXPTR} into ttmp11[22:14].
	s_andn2_b32	ttmp11, ttmp11, (TTMP11_FIRST_REPLAY_MASK | TTMP11_REPLAY_W64H_MASK | TTMP11_FXPTR_MASK)

	s_getreg_b32	s_tmp, hwreg(HW_REG_WAVE_XNACK_STATE_PRIV, SQ_WAVE_XNACK_STATE_PRIV_FIRST_REPLAY_SHIFT, SQ_WAVE_XNACK_STATE_PRIV_FIRST_REPLAY_SIZE)
	s_lshl_b32	s_tmp, s_tmp, TTMP11_FIRST_REPLAY_SHIFT
	s_or_b32	ttmp11, ttmp11, s_tmp

	s_getreg_b32	s_tmp, hwreg(HW_REG_WAVE_XNACK_STATE_PRIV, SQ_WAVE_XNACK_STATE_PRIV_REPLAY_W64H_SHIFT, SQ_WAVE_XNACK_STATE_PRIV_REPLAY_W64H_SIZE)
	s_lshl_b32	s_tmp, s_tmp, TTMP11_REPLAY_W64H_SHIFT
	s_or_b32	ttmp11, ttmp11, s_tmp

	s_getreg_b32	s_tmp, hwreg(HW_REG_WAVE_XNACK_STATE_PRIV, SQ_WAVE_XNACK_STATE_PRIV_FXPTR_SHIFT, SQ_WAVE_XNACK_STATE_PRIV_FXPTR_SIZE)
	s_lshl_b32	s_tmp, s_tmp, TTMP11_FXPTR_SHIFT
	s_or_b32	ttmp11, ttmp11, s_tmp

	s_setreg_imm32_b32	hwreg(HW_REG_WAVE_XNACK_STATE_PRIV), 0
end

function restore_xnack_state_priv(s_tmp)
	s_lshr_b32	s_tmp, ttmp11, TTMP11_FIRST_REPLAY_SHIFT
	s_setreg_b32	hwreg(HW_REG_WAVE_XNACK_STATE_PRIV, SQ_WAVE_XNACK_STATE_PRIV_FIRST_REPLAY_SHIFT, SQ_WAVE_XNACK_STATE_PRIV_FIRST_REPLAY_SIZE), s_tmp

	s_lshr_b32	s_tmp, ttmp11, TTMP11_REPLAY_W64H_SHIFT
	s_setreg_b32	hwreg(HW_REG_WAVE_XNACK_STATE_PRIV, SQ_WAVE_XNACK_STATE_PRIV_REPLAY_W64H_SHIFT, SQ_WAVE_XNACK_STATE_PRIV_REPLAY_W64H_SIZE), s_tmp

	s_lshr_b32	s_tmp, ttmp11, TTMP11_FXPTR_SHIFT
	s_setreg_b32	hwreg(HW_REG_WAVE_XNACK_STATE_PRIV, SQ_WAVE_XNACK_STATE_PRIV_FXPTR_SHIFT, SQ_WAVE_XNACK_STATE_PRIV_FXPTR_SIZE), s_tmp
end
#endif

function wait_trap_barriers(s_tmp1, s_tmp2, serialize_wa)
#if HAVE_CLUSTER_BARRIER
	// If not in a WG then wave cannot use s_barrier_signal_isfirst.
	s_getreg_b32	s_tmp1, hwreg(HW_REG_WAVE_STATUS)
	s_bitcmp0_b32	s_tmp1, SQ_WAVE_STATUS_IN_WG_SHIFT
	s_cbranch_scc1	L_TRAP_CLUSTER_BARRIER_SIGNAL

	s_barrier_signal_isfirst	-2
	s_barrier_wait	-2

	// Only the first wave in the group signals the trap cluster barrier.
	s_cbranch_scc0	L_SKIP_TRAP_CLUSTER_BARRIER_SIGNAL

L_TRAP_CLUSTER_BARRIER_SIGNAL:
	s_barrier_signal	-4

L_SKIP_TRAP_CLUSTER_BARRIER_SIGNAL:
	s_barrier_wait	-4

#if CLUSTER_BARRIER_SERIALIZE_WORKAROUND
if serialize_wa
	// Trap cluster barrier may complete with a user cluster barrier in-flight.
	// This is indicated if user cluster member count and signal count are equal.
L_WAIT_USER_CLUSTER_BARRIER_COMPLETE:
	s_sendmsg_rtn_b32	s_tmp1, sendmsg(MSG_RTN_GET_CLUSTER_BARRIER_STATE)
	s_wait_kmcnt	0
	s_bitcmp0_b32	s_tmp1, BARRIER_STATE_VALID_OFFSET
	s_cbranch_scc1	L_NOT_IN_CLUSTER

	s_bfe_u32	s_tmp2, s_tmp1, (BARRIER_STATE_MEMBER_OFFSET | (BARRIER_STATE_MEMBER_SIZE << 0x10))
	s_bfe_u32	s_tmp1, s_tmp1, (BARRIER_STATE_SIGNAL_OFFSET | (BARRIER_STATE_SIGNAL_SIZE << 0x10))
	s_cmp_eq_u32	s_tmp1, s_tmp2
	s_cbranch_scc1	L_WAIT_USER_CLUSTER_BARRIER_COMPLETE
end
L_NOT_IN_CLUSTER:
#endif

#else
	s_barrier_signal	-2
	s_barrier_wait	-2
#endif
end

#if RELAXED_SCHEDULING_IN_TRAP
function restore_sched_mode(s_tmp)
	s_bfe_u32	s_tmp, ttmp11, (TTMP11_SCHED_MODE_SHIFT | (TTMP11_SCHED_MODE_SIZE << 0x10))
	s_setreg_b32	hwreg(HW_REG_WAVE_SCHED_MODE), s_tmp
end
#endif

function restore_barrier_signal_count(barrier_id)
	// extract the saved signal count from s_restore_tmp
	s_lshr_b32	s_restore_tmp, s_restore_tmp, BARRIER_STATE_SIGNAL_OFFSET

	// We need to call s_barrier_signal repeatedly to restore the signal count
	// of the group/cluster barrier. The member count is already initialized.
L_BARRIER_RESTORE_LOOP:
	s_and_b32	s_restore_tmp, s_restore_tmp, s_restore_tmp
	s_cbranch_scc0	L_BARRIER_RESTORE_DONE
	s_barrier_signal	barrier_id
	s_add_i32	s_restore_tmp, s_restore_tmp, -1
	s_branch	L_BARRIER_RESTORE_LOOP

L_BARRIER_RESTORE_DONE:
end

#if HAVE_INSTRUCTION_FIXUP
function fixup_instruction
	// PC read may fault if memory violation has been asserted.
	// In this case no further progress is expected so fixup is not needed.
	s_bitcmp1_b32	s_save_excp_flag_priv, SQ_WAVE_EXCP_FLAG_PRIV_MEM_VIOL_SHIFT
	s_cbranch_scc1	L_FIXUP_DONE

	// ttmp[0:1]: {7b'0} PC[56:0]
	// ttmp2, 3, 10, 13, 14, 15: free
	s_load_b64	[ttmp14, ttmp15], [ttmp0, ttmp1], 0 scope:SCOPE_CU	// Load the 2 instruction DW we are returning to
	s_wait_kmcnt	0
	s_load_b64	[ttmp2, ttmp3], [ttmp0, ttmp1], 8 scope:SCOPE_CU	// Load the next 2 instruction DW, just in case
	s_and_b32	ttmp10, ttmp14, 0x80000000				// Check bit 31 in the first DWORD
										// SCC set if ttmp10 is != 0, i.e. if bit 31 == 1
	s_cbranch_scc1	L_FIXUP_NOT_VOP12C					// If bit 31 is 1, we are not VOP1, VOP2, or VOP3C
	// Fall through here means bit 31 == 0, meaning we are VOP1, VOP2, or VOPC
	// Size of instruction depends on Opcode or SRC0_9
	// Check for VOP2 opcode
	s_bfe_u32	ttmp10, ttmp14, (25 | (6 << 0x10))			// Check bits 30:25 for VOP2 Opcode
	// VOP2 V_FMAMK_F64 of V_FMAAK_F64 has implied 64-bit literature, 3 DW
	s_sub_co_i32	ttmp13, ttmp10, 0x23					// V_FMAMK_F64 is 0x23, V_FMAAK_F64 is 0x24
	s_cmp_le_u32	ttmp13, 0x1						// 0==0x23, 1==0x24
	s_cbranch_scc1	L_FIXUP_THREE_DWORD					// If either, this is 3 DWORD inst
	// VOP2 V_FMAMK_F32, V_FMAAK_F32, V_FMAMK_F16, V_FMAAK_F16, 2 DW
	s_sub_co_i32	ttmp13, ttmp10, 0x2c					// V_FMAMK_F32 is 0x2c, V_FMAAK_F32 is 0x2d
	s_cmp_le_u32	ttmp13, 0x1						// 0==0x2c, 1==0x2d
	s_cbranch_scc1	L_FIXUP_TWO_DWORD					// If either, this is 2 DWORD inst
	s_sub_co_i32	ttmp13, ttmp10, 0x37					// V_FMAMK_F16 is 0x37, V_FMAAK_F16 is 0x38
	s_cmp_le_u32	ttmp13, 0x1						// 0==0x37, 1==0x38
	s_cbranch_scc1	L_FIXUP_TWO_DWORD					// If either, this is 2 DWORD inst
	// Check SRC0_9 for VOP1, VOP2, and VOPC
	s_and_b32	ttmp10, ttmp14, 0x1ff					// Check bits 8:0 for SRC0_9
	// Literal constant 64 is 3 DWORDs
	s_cmp_eq_u32	ttmp10, 0xfe						// 0xfe == 254 == Literal constant64
	s_cbranch_scc1	L_FIXUP_THREE_DWORD					// 3 DWORD inst
	// Literal constant 32, DPP16, DPP8, and DPP8FI are 2 DWORDs
	s_cmp_eq_u32	ttmp10, 0xff						// 0xff == 255 = Literal constant32
	s_cbranch_scc1	L_FIXUP_TWO_DWORD					// 2 DWORD inst
	s_cmp_eq_u32	ttmp10, 0xfa						// 0xfa == 250 = DPP16
	s_cbranch_scc1	L_FIXUP_TWO_DWORD					// 2 DWORD inst
	s_sub_co_i32	ttmp13, ttmp10, 0xe9					// DPP8 is 0xe9, DPP8FI is 0xea
	s_cmp_le_u32	ttmp13, 0x1						// 0==0xe9, 1==0xea
	s_cbranch_scc1	L_FIXUP_TWO_DWORD					// If either, this is 2 DWORD inst
	// Instruction is 1 DWORD otherwise

L_FIXUP_ONE_DWORD:
	// Check if TTMP15 contains the value for S_SET_VGPR_MSB instruction
	s_and_b32	ttmp10, ttmp15, 0xffff0000				// Check encoding in upper 16 bits
	s_cmp_eq_u32	ttmp10, 0xbf860000					// Check if SOPP (9b'10_1111111) and S_SET_VGPR_MSB (7b'0000110)
	s_cbranch_scc0	L_FIXUP_DONE						// No problem, no fixup needed
	// VALU op followed by a S_SET_VGPR_MSB. Need to pull SIMM[15:8] to fix up MODE.*_VGPR_MSB
	s_bfe_u32	ttmp10, ttmp15, (14 | (2 << 0x10))			// Shift SIMM[15:14] over to 1:0, Dst
	s_and_b32	ttmp13, ttmp15, 0x3f00					// Mask to get SIMM[13:8] only
	s_lshr_b32	ttmp13, ttmp13, 6					// Shift SIMM[13:8] into 7:2, Src2, Src1, Src0
	s_or_b32	ttmp10, ttmp10, ttmp13					// Src2, Src1, Src0, Dst --> format in MODE register
	s_setreg_b32	hwreg(HW_REG_WAVE_MODE, 12, 8), ttmp10			// Write value into MODE[19:12]
	s_branch	L_FIXUP_DONE

L_FIXUP_NOT_VOP12C:
	// ttmp[0:1]: {7b'0} PC[56:0]
	// ttmp2: PC+2 value (not waitcnt'ed yet)
	// ttmp3: PC+3 value (not waitcnt'ed yet)
	// ttmp10, ttmp13: free
	// ttmp14: PC+O value
	// ttmp15: PC+1 value
	// Not VOP1, VOP2, or VOPC.
	// Check if we are VOP3 or VOP3SD
	s_and_b32	ttmp10, ttmp14, 0xfc000000				// Bits 31:26
	s_cmp_eq_u32	ttmp10, 0xd4000000					// If 31:26 = 0x35, this is VOP3 or VOP3SD
	s_cbranch_scc1	L_FIXUP_CHECK_VOP3					// If VOP3 or VOP3SD, need to check SRC2_9, SRC1_9, SRC0_9
	// Not VOP1, VOP2, VOPC, VOP3, or VOP3SD.
	// Check for VOPD
	s_cmp_eq_u32	ttmp10, 0xc8000000					// If 31:26 = 0x32, this is VOPD
	s_cbranch_scc1	L_FIXUP_CHECK_VOPD					// If VOPD, need to check OpX, OpY, SRCX0 and SRCY0
	// Not VOP1, VOP2, VOPC, VOP3, VOP3SD, VOPD.
	// Check if we are VOPD3
	s_and_b32	ttmp10, ttmp14, 0xff000000				// Bits 31:24
	s_cmp_eq_u32	ttmp10, 0xcf000000					// If 31:24 = 0xcf, this is VOPD3
	s_cbranch_scc1	L_FIXUP_THREE_DWORD					// If VOPD3, 3 DWORD inst
	// Not VOP1, VOP2, VOPC, VOP3, VOP3SD, VOPD, or VOPD3.
	// Check if we are in the middle of VOP3PX.
	s_and_b32	ttmp13, ttmp14, 0xffff0000				// Bits 31:16
	s_cmp_eq_u32	ttmp13, 0xcc330000					// If 31:16 = 0xcc33, this is 8 bytes past VOP3PX
	s_cbranch_scc1	L_FIXUP_VOP3PX_MIDDLE
	s_cmp_eq_u32	ttmp13, 0xcc880000					// If 31:16 = 0xcc88, this is 8 bytes past VOP3PX
	s_cbranch_scc1	L_FIXUP_VOP3PX_MIDDLE
	// Might be in VOP3P, but we must ensure we are not VOP3PX2
	s_cmp_eq_u32	ttmp13, 0xcc350000					// If 31:16 = 0xcc35, this is VOP3PX2
	s_cbranch_scc1	L_FIXUP_DONE						// If VOP3PX2, no fixup needed
	s_cmp_eq_u32	ttmp13, 0xcc3a0000					// If 31:16 = 0xcc3a, this is VOP3PX2
	s_cbranch_scc1	L_FIXUP_DONE						// If VOP3PX2, no fixup needed
	// Check if we are VOP3P
	s_cmp_eq_u32	ttmp10, 0xcc000000					// If 31:24 = 0xcc, this is VOP3P
	s_cbranch_scc0	L_FIXUP_DONE						// Not in VOP3P, so instruction is not VOP1, VOP2,
										// VOPC, VOP3, VOP3SD, VOP3P, VOPD, or VOPD3
										// No fixup needed.
	// Fall-through if we are in VOP3P to check SRC2_9, SRC1_9, and SRC0_9
L_FIXUP_CHECK_VOP3:
	// Start with Src0, which is in bits 8:0 of second instruction DW, ttmp15
	s_and_b32	ttmp10, ttmp15, 0x1ff					// Mask out unused bits
	// Src0_9 == Literal constant 32, DPP16, DPP8, and DPP8FI means 3 DWORDs
	s_cmp_eq_u32	ttmp10, 0xff						// 0xff == 255 = Literal constant32
	s_cbranch_scc1	L_FIXUP_THREE_DWORD					// 3 DWORD inst
	s_cmp_eq_u32	ttmp10, 0xfa						// 0xfa == 250 = DPP16
	s_cbranch_scc1	L_FIXUP_THREE_DWORD					// 3 DWORD inst
	s_sub_co_i32	ttmp10, ttmp10, 0xe9					// DPP8 is 0xe9, DPP8FI is 0xea
	s_cmp_le_u32	ttmp10, 0x1						// 0==0xe9, 1==0xea
	s_cbranch_scc1	L_FIXUP_THREE_DWORD					// If either, this is 3 DWORD inst
	s_and_b32	ttmp10, ttmp15, 0x3fe00					// Next is Src1, which is in 17:9
	s_cmp_eq_u32	ttmp10, 0x1fe00						// 0xff == 255 = Literal constant32
	s_cbranch_scc1	L_FIXUP_THREE_DWORD					// 3 DWORD inst
	s_and_b32	ttmp10, ttmp15, 0x7fc0000				// Next is Src2, which is in 26:18
	s_cmp_eq_u32	ttmp10, 0x3fc0000					// 0xff == 255 = Literal constant32
	s_cbranch_scc1	L_FIXUP_THREE_DWORD					// 3 DWORD inst
	s_branch	L_FIXUP_TWO_DWORD					// No special encodings, VOP3* is 2 Dword

L_FIXUP_CHECK_VOPD:
	// OpX being V_DUAL_FMA*K_F32 means 3 DWORDs
	s_bfe_u32	ttmp10, ttmp14, (22 | (4 << 0x10))			// OPX is bits 25:22
	s_sub_co_i32	ttmp10, ttmp10, 0x1					// V_DUAL_FMAAK_F32 is 0x1, V_DUAL_FMAMK_F32 is 0x2
	s_cmp_le_u32	ttmp10, 0x1						// 0==0x1, 1==0x2
	s_cbranch_scc1	L_FIXUP_THREE_DWORD					// If either, this is 3 DWORD inst
	// OpY being V_DUAL_FMA*K_F32 means 3 DWORDs
	s_bfe_u32	ttmp10, ttmp14, (17 | (5 << 0x10))			// OPX is bits 21:17
	s_sub_co_i32	ttmp10, ttmp10, 0x1					// V_DUAL_FMAAK_F32 is 0x1, V_DUAL_FMAMK_F32 is 0x2
	s_cmp_le_u32	ttmp10, 0x1						// 0==0x1, 1==0x2
	s_cbranch_scc1	L_FIXUP_THREE_DWORD					// If either, this is 3 DWORD inst
	// SRCX0 == Literal constant 32 means 3 DWORDs
	s_and_b32	ttmp10, ttmp14, 0x1ff					// SRCX0 is in bits 8:0 of 1st DWORD
	s_cmp_eq_u32	ttmp10, 0xff						// 0xff == 255 = Literal constant32
	s_cbranch_scc1	L_FIXUP_THREE_DWORD					// 3 DWORD inst
	// SRCY0 == Literal constant 32 means 3 DWORDs
	s_and_b32	ttmp10, ttmp15, 0x1ff					// SRCY0 is in bits 8:0 of 2nd DWORD
	s_cmp_eq_u32	ttmp10, 0xff						// 0xff == 255 = Literal constant32
	s_cbranch_scc1	L_FIXUP_THREE_DWORD					// 3 DWORD inst
										// If otherwise, no special encodings. Default VOPD is 2 Dword
										// Fall-thru if true, because this is a 2 DWORD inst
L_FIXUP_TWO_DWORD:
	s_wait_kmcnt	0							// Wait for PC+2 and PC+3 to arrive in ttmp2 and ttmp3
	s_mov_b32	ttmp15, ttmp2						// Move possible S_SET_VGPR_MSB into ttmp15
	s_branch	L_FIXUP_ONE_DWORD					// Go to common logic that checks if it is S_SET_VGPR_MSB

L_FIXUP_THREE_DWORD:
	s_wait_kmcnt	0							// Wait for PC+2 and PC+3 to arrive in ttmp2 and ttmp3
	s_mov_b32	ttmp15, ttmp3						// Move possible S_SET_VGPR_MSB into ttmp15
	s_branch	L_FIXUP_ONE_DWORD					// Go to common logic that checks if it is S_SET_VGPR_MSB

L_FIXUP_VOP3PX_MIDDLE:
	s_sub_co_u32	ttmp0, ttmp0, 8						// Rewind PC 8 bytes to beginning of instruction
	s_sub_co_ci_u32	ttmp1, ttmp1, 0
	s_branch	L_FIXUP_TWO_DWORD					// 2 DWORD inst (2nd half of a 4 DWORD inst)

L_FIXUP_DONE:
	s_wait_kmcnt	0							// Ensure load of ttmp2 and ttmp3 is done
end
#endif
