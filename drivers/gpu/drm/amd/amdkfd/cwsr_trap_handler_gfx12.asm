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

#define SINGLE_STEP_MISSED_WORKAROUND 1	//workaround for lost TRAP_AFTER_INST exception when SAVECTX raised

var SQ_WAVE_STATE_PRIV_BARRIER_COMPLETE_MASK	= 0x4
var SQ_WAVE_STATE_PRIV_SCC_SHIFT		= 9
var SQ_WAVE_STATE_PRIV_SYS_PRIO_MASK		= 0xC00
var SQ_WAVE_STATE_PRIV_HALT_MASK		= 0x4000
var SQ_WAVE_STATE_PRIV_POISON_ERR_MASK		= 0x8000
var SQ_WAVE_STATE_PRIV_POISON_ERR_SHIFT		= 15
var SQ_WAVE_STATUS_WAVE64_SHIFT			= 29
var SQ_WAVE_STATUS_WAVE64_SIZE			= 1
var SQ_WAVE_STATUS_NO_VGPRS_SHIFT		= 24
var SQ_WAVE_STATE_PRIV_ALWAYS_CLEAR_MASK	= SQ_WAVE_STATE_PRIV_SYS_PRIO_MASK|SQ_WAVE_STATE_PRIV_POISON_ERR_MASK
var S_SAVE_PC_HI_TRAP_ID_MASK			= 0xF0000000

var SQ_WAVE_LDS_ALLOC_LDS_SIZE_SHIFT		= 12
var SQ_WAVE_LDS_ALLOC_LDS_SIZE_SIZE		= 9
var SQ_WAVE_GPR_ALLOC_VGPR_SIZE_SIZE		= 8
var SQ_WAVE_GPR_ALLOC_VGPR_SIZE_SHIFT		= 12
var SQ_WAVE_LDS_ALLOC_VGPR_SHARED_SIZE_SHIFT	= 24
var SQ_WAVE_LDS_ALLOC_VGPR_SHARED_SIZE_SIZE	= 4
var SQ_WAVE_LDS_ALLOC_GRANULARITY		= 9

var SQ_WAVE_EXCP_FLAG_PRIV_ADDR_WATCH_MASK	= 0xF
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
var BARRIER_STATE_SIGNAL_OFFSET			= 16
var BARRIER_STATE_VALID_OFFSET			= 0

var TTMP11_DEBUG_TRAP_ENABLED_SHIFT		= 23
var TTMP11_DEBUG_TRAP_ENABLED_MASK		= 0x800000

// SQ_SEL_X/Y/Z/W, BUF_NUM_FORMAT_FLOAT, (0 for MUBUF stride[17:14]
// when ADD_TID_ENABLE and BUF_DATA_FORMAT_32 for MTBUF), ADD_TID_ENABLE
var S_SAVE_BUF_RSRC_WORD1_STRIDE		= 0x00040000
var S_SAVE_BUF_RSRC_WORD3_MISC			= 0x10807FAC
var S_SAVE_SPI_INIT_FIRST_WAVE_MASK		= 0x04000000
var S_SAVE_SPI_INIT_FIRST_WAVE_SHIFT		= 26

var S_SAVE_PC_HI_FIRST_WAVE_MASK		= 0x80000000
var S_SAVE_PC_HI_FIRST_WAVE_SHIFT		= 31

var s_sgpr_save_num				= 108

var s_save_spi_init_lo				= exec_lo
var s_save_spi_init_hi				= exec_hi
var s_save_pc_lo				= ttmp0
var s_save_pc_hi				= ttmp1
var s_save_exec_lo				= ttmp2
var s_save_exec_hi				= ttmp3
var s_save_state_priv				= ttmp12
var s_save_excp_flag_priv			= ttmp15
var s_save_xnack_mask				= s_save_excp_flag_priv
var s_wave_size					= ttmp7
var s_save_buf_rsrc0				= ttmp8
var s_save_buf_rsrc1				= ttmp9
var s_save_buf_rsrc2				= ttmp10
var s_save_buf_rsrc3				= ttmp11
var s_save_mem_offset				= ttmp4
var s_save_alloc_size				= s_save_excp_flag_priv
var s_save_tmp					= ttmp14
var s_save_m0					= ttmp5
var s_save_ttmps_lo				= s_save_tmp
var s_save_ttmps_hi				= s_save_excp_flag_priv

var S_RESTORE_BUF_RSRC_WORD1_STRIDE		= S_SAVE_BUF_RSRC_WORD1_STRIDE
var S_RESTORE_BUF_RSRC_WORD3_MISC		= S_SAVE_BUF_RSRC_WORD3_MISC

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
var s_restore_buf_rsrc0				= ttmp8
var s_restore_buf_rsrc1				= ttmp9
var s_restore_buf_rsrc2				= ttmp10
var s_restore_buf_rsrc3				= ttmp11
var s_restore_size				= ttmp6
var s_restore_ttmps_lo				= s_restore_tmp
var s_restore_ttmps_hi				= s_restore_alloc_size
var s_restore_spi_init_hi_save			= s_restore_exec_hi

shader main
	asic(DEFAULT)
	type(CS)
	wave_size(32)

	s_branch	L_SKIP_RESTORE						//NOT restore. might be a regular trap or save

L_JUMP_TO_RESTORE:
	s_branch	L_RESTORE

L_SKIP_RESTORE:
	s_getreg_b32	s_save_state_priv, hwreg(HW_REG_WAVE_STATE_PRIV)	//save STATUS since we will change SCC

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
	// Read second-level TBA/TMA from first-level TMA and jump if available.
	// ttmp[2:5] and ttmp12 can be used (others hold SPI-initialized debug data)
	// ttmp12 holds SQ_WAVE_STATUS
	s_sendmsg_rtn_b64       [ttmp14, ttmp15], sendmsg(MSG_RTN_GET_TMA)
	s_wait_idle
	s_lshl_b64	[ttmp14, ttmp15], [ttmp14, ttmp15], 0x8

	s_bitcmp1_b32	ttmp15, 0xF
	s_cbranch_scc0	L_NO_SIGN_EXTEND_TMA
	s_or_b32	ttmp15, ttmp15, 0xFFFF0000
L_NO_SIGN_EXTEND_TMA:

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
	s_and_b32	ttmp1, ttmp1, 0xFFFF

	// Restore SQ_WAVE_STATUS.
	s_and_b64	exec, exec, exec					// Restore STATUS.EXECZ, not writable by s_setreg_b32
	s_and_b64	vcc, vcc, vcc						// Restore STATUS.VCCZ, not writable by s_setreg_b32

	// STATE_PRIV.BARRIER_COMPLETE may have changed since we read it.
	// Only restore fields which the trap handler changes.
	s_lshr_b32	s_save_state_priv, s_save_state_priv, SQ_WAVE_STATE_PRIV_SCC_SHIFT
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

	s_and_b32	s_save_pc_hi, s_save_pc_hi, 0x0000ffff			//pc[47:32]
	s_mov_b32	s_save_tmp, 0
	s_setreg_b32	hwreg(HW_REG_WAVE_EXCP_FLAG_PRIV, SQ_WAVE_EXCP_FLAG_PRIV_SAVE_CONTEXT_SHIFT, 1), s_save_tmp	//clear saveCtx bit

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

	// Trap temporaries must be saved via VGPR but all VGPRs are in use.
	// There is no ttmp space to hold the resource constant for VGPR save.
	// Save v0 by itself since it requires only two SGPRs.
	s_mov_b32	s_save_ttmps_lo, exec_lo
	s_and_b32	s_save_ttmps_hi, exec_hi, 0xFFFF
	s_mov_b32	exec_lo, 0xFFFFFFFF
	s_mov_b32	exec_hi, 0xFFFFFFFF
	global_store_dword_addtid	v0, [s_save_ttmps_lo, s_save_ttmps_hi] scope:SCOPE_SYS
	v_mov_b32	v0, 0x0
	s_mov_b32	exec_lo, s_save_ttmps_lo
	s_mov_b32	exec_hi, s_save_ttmps_hi

	// Save trap temporaries 4-11, 13 initialized by SPI debug dispatch logic
	// ttmp SR memory offset : size(VGPR)+size(SVGPR)+size(SGPR)+0x40
	get_wave_size2(s_save_ttmps_hi)
	get_vgpr_size_bytes(s_save_ttmps_lo, s_save_ttmps_hi)
	get_svgpr_size_bytes(s_save_ttmps_hi)
	s_add_u32	s_save_ttmps_lo, s_save_ttmps_lo, s_save_ttmps_hi
	s_and_b32	s_save_ttmps_hi, s_save_spi_init_hi, 0xFFFF
	s_add_u32	s_save_ttmps_lo, s_save_ttmps_lo, get_sgpr_size_bytes()
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
	global_store_dword_addtid	v0, [s_save_ttmps_lo, s_save_ttmps_hi] offset:0x40 scope:SCOPE_SYS
	v_readlane_b32	ttmp14, v0, 0xE
	v_readlane_b32	ttmp15, v0, 0xF
	s_mov_b32	exec_lo, ttmp14
	s_mov_b32	exec_hi, ttmp15

	/* setup Resource Contants */
	s_mov_b32	s_save_buf_rsrc0, s_save_spi_init_lo			//base_addr_lo
	s_and_b32	s_save_buf_rsrc1, s_save_spi_init_hi, 0x0000FFFF	//base_addr_hi
	s_or_b32	s_save_buf_rsrc1, s_save_buf_rsrc1, S_SAVE_BUF_RSRC_WORD1_STRIDE
	s_mov_b32	s_save_buf_rsrc2, 0					//NUM_RECORDS initial value = 0 (in bytes) although not neccessarily inited
	s_mov_b32	s_save_buf_rsrc3, S_SAVE_BUF_RSRC_WORD3_MISC

	s_mov_b32	s_save_m0, m0

	/* global mem offset */
	s_mov_b32	s_save_mem_offset, 0x0
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
	s_mov_b32	s_save_buf_rsrc2, 0x1000000				//NUM_RECORDS in bytes

	// VGPR Allocated in 4-GPR granularity

	buffer_store_dword	v1, v0, s_save_buf_rsrc0, s_save_mem_offset scope:SCOPE_SYS offset:128
	buffer_store_dword	v2, v0, s_save_buf_rsrc0, s_save_mem_offset scope:SCOPE_SYS offset:128*2
	buffer_store_dword	v3, v0, s_save_buf_rsrc0, s_save_mem_offset scope:SCOPE_SYS offset:128*3
	s_branch	L_SAVE_HWREG

L_SAVE_4VGPR_WAVE64:
	s_mov_b32	s_save_buf_rsrc2, 0x1000000				//NUM_RECORDS in bytes

	// VGPR Allocated in 4-GPR granularity

	buffer_store_dword	v1, v0, s_save_buf_rsrc0, s_save_mem_offset scope:SCOPE_SYS offset:256
	buffer_store_dword	v2, v0, s_save_buf_rsrc0, s_save_mem_offset scope:SCOPE_SYS offset:256*2
	buffer_store_dword	v3, v0, s_save_buf_rsrc0, s_save_mem_offset scope:SCOPE_SYS offset:256*3

	/* save HW registers */

L_SAVE_HWREG:
	// HWREG SR memory offset : size(VGPR)+size(SVGPR)+size(SGPR)
	get_vgpr_size_bytes(s_save_mem_offset, s_wave_size)
	get_svgpr_size_bytes(s_save_tmp)
	s_add_u32	s_save_mem_offset, s_save_mem_offset, s_save_tmp
	s_add_u32	s_save_mem_offset, s_save_mem_offset, get_sgpr_size_bytes()

	s_mov_b32	s_save_buf_rsrc2, 0x1000000				//NUM_RECORDS in bytes

	v_mov_b32	v0, 0x0							//Offset[31:0] from buffer resource
	v_mov_b32	v1, 0x0							//Offset[63:32] from buffer resource
	v_mov_b32	v2, 0x0							//Set of SGPRs for TCP store
	s_mov_b32	m0, 0x0							//Next lane of v2 to write to

	// Ensure no further changes to barrier or LDS state.
	// STATE_PRIV.BARRIER_COMPLETE may change up to this point.
	s_barrier_signal	-2
	s_barrier_wait	-2

	// Re-read final state of BARRIER_COMPLETE field for save.
	s_getreg_b32	s_save_tmp, hwreg(HW_REG_WAVE_STATE_PRIV)
	s_and_b32	s_save_tmp, s_save_tmp, SQ_WAVE_STATE_PRIV_BARRIER_COMPLETE_MASK
	s_andn2_b32	s_save_state_priv, s_save_state_priv, SQ_WAVE_STATE_PRIV_BARRIER_COMPLETE_MASK
	s_or_b32	s_save_state_priv, s_save_state_priv, s_save_tmp

	write_hwreg_to_v2(s_save_m0)
	write_hwreg_to_v2(s_save_pc_lo)
	s_andn2_b32	s_save_tmp, s_save_pc_hi, S_SAVE_PC_HI_FIRST_WAVE_MASK
	write_hwreg_to_v2(s_save_tmp)
	write_hwreg_to_v2(s_save_exec_lo)
	write_hwreg_to_v2(s_save_exec_hi)
	write_hwreg_to_v2(s_save_state_priv)

	s_getreg_b32	s_save_tmp, hwreg(HW_REG_WAVE_EXCP_FLAG_PRIV)
	write_hwreg_to_v2(s_save_tmp)

	write_hwreg_to_v2(s_save_xnack_mask)

	s_getreg_b32	s_save_m0, hwreg(HW_REG_WAVE_MODE)
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

	// Write HWREGs with 16 VGPR lanes. TTMPs occupy space after this.
	s_mov_b32       exec_lo, 0xFFFF
	s_mov_b32	exec_hi, 0x0
	buffer_store_dword	v2, v0, s_save_buf_rsrc0, s_save_mem_offset scope:SCOPE_SYS

	// Write SGPRs with 32 VGPR lanes. This works in wave32 and wave64 mode.
	s_mov_b32       exec_lo, 0xFFFFFFFF

	/* save SGPRs */
	// Save SGPR before LDS save, then the s0 to s4 can be used during LDS save...

	// SGPR SR memory offset : size(VGPR)+size(SVGPR)
	get_vgpr_size_bytes(s_save_mem_offset, s_wave_size)
	get_svgpr_size_bytes(s_save_tmp)
	s_add_u32	s_save_mem_offset, s_save_mem_offset, s_save_tmp
	s_mov_b32	s_save_buf_rsrc2, 0x1000000				//NUM_RECORDS in bytes

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

	buffer_store_dword	v2, v0, s_save_buf_rsrc0, s_save_mem_offset scope:SCOPE_SYS
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

	buffer_store_dword	v2, v0, s_save_buf_rsrc0, s_save_mem_offset scope:SCOPE_SYS

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
	s_mov_b32	s_save_buf_rsrc2, s_save_alloc_size			//NUM_RECORDS in bytes

	// LDS at offset: size(VGPR)+size(SVGPR)+SIZE(SGPR)+SIZE(HWREG)
	//
	get_vgpr_size_bytes(s_save_mem_offset, s_wave_size)
	get_svgpr_size_bytes(s_save_tmp)
	s_add_u32	s_save_mem_offset, s_save_mem_offset, s_save_tmp
	s_add_u32	s_save_mem_offset, s_save_mem_offset, get_sgpr_size_bytes()
	s_add_u32	s_save_mem_offset, s_save_mem_offset, get_hwreg_size_bytes()

	s_mov_b32	s_save_buf_rsrc2, 0x1000000				//NUM_RECORDS in bytes

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
	buffer_store_dword	v1, v0, s_save_buf_rsrc0, s_save_mem_offset scope:SCOPE_SYS

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
	buffer_store_dword	v1, v0, s_save_buf_rsrc0, s_save_mem_offset scope:SCOPE_SYS

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

	s_mov_b32	s_save_buf_rsrc2, 0x1000000				//NUM_RECORDS in bytes

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

	buffer_store_dword	v0, v0, s_save_buf_rsrc0, s_save_mem_offset scope:SCOPE_SYS
	buffer_store_dword	v1, v0, s_save_buf_rsrc0, s_save_mem_offset scope:SCOPE_SYS offset:128
	buffer_store_dword	v2, v0, s_save_buf_rsrc0, s_save_mem_offset scope:SCOPE_SYS offset:128*2
	buffer_store_dword	v3, v0, s_save_buf_rsrc0, s_save_mem_offset scope:SCOPE_SYS offset:128*3

	s_add_u32	m0, m0, 4						//next vgpr index
	s_add_u32	s_save_mem_offset, s_save_mem_offset, 128*4		//every buffer_store_dword does 128 bytes
	s_cmp_lt_u32	m0, s_save_alloc_size					//scc = (m0 < s_save_alloc_size) ? 1 : 0
	s_cbranch_scc1	L_SAVE_VGPR_W32_LOOP					//VGPR save is complete?

	s_branch	L_SAVE_VGPR_END

L_SAVE_VGPR_WAVE64:
	s_mov_b32	s_save_buf_rsrc2, 0x1000000				//NUM_RECORDS in bytes

	// VGPR store using dw burst
	s_mov_b32	m0, 0x4							//VGPR initial index value =4
	s_cmp_lt_u32	m0, s_save_alloc_size
	s_cbranch_scc0	L_SAVE_SHARED_VGPR

L_SAVE_VGPR_W64_LOOP:
	v_movrels_b32	v0, v0							//v0 = v[0+m0]
	v_movrels_b32	v1, v1							//v1 = v[1+m0]
	v_movrels_b32	v2, v2							//v2 = v[2+m0]
	v_movrels_b32	v3, v3							//v3 = v[3+m0]

	buffer_store_dword	v0, v0, s_save_buf_rsrc0, s_save_mem_offset scope:SCOPE_SYS
	buffer_store_dword	v1, v0, s_save_buf_rsrc0, s_save_mem_offset scope:SCOPE_SYS offset:256
	buffer_store_dword	v2, v0, s_save_buf_rsrc0, s_save_mem_offset scope:SCOPE_SYS offset:256*2
	buffer_store_dword	v3, v0, s_save_buf_rsrc0, s_save_mem_offset scope:SCOPE_SYS offset:256*3

	s_add_u32	m0, m0, 4						//next vgpr index
	s_add_u32	s_save_mem_offset, s_save_mem_offset, 256*4		//every buffer_store_dword does 256 bytes
	s_cmp_lt_u32	m0, s_save_alloc_size					//scc = (m0 < s_save_alloc_size) ? 1 : 0
	s_cbranch_scc1	L_SAVE_VGPR_W64_LOOP					//VGPR save is complete?

L_SAVE_SHARED_VGPR:
	s_getreg_b32	s_save_alloc_size, hwreg(HW_REG_WAVE_LDS_ALLOC,SQ_WAVE_LDS_ALLOC_VGPR_SHARED_SIZE_SHIFT,SQ_WAVE_LDS_ALLOC_VGPR_SHARED_SIZE_SIZE)
	s_and_b32	s_save_alloc_size, s_save_alloc_size, 0xFFFFFFFF	//shared_vgpr_size is zero?
	s_cbranch_scc0	L_SAVE_VGPR_END						//no shared_vgpr used? jump to L_SAVE_LDS
	s_lshl_b32	s_save_alloc_size, s_save_alloc_size, 3			//Number of SHARED_VGPRs = shared_vgpr_size * 8    (non-zero value)
	//m0 now has the value of normal vgpr count, just add the m0 with shared_vgpr count to get the total count.
	//save shared_vgpr will start from the index of m0
	s_add_u32	s_save_alloc_size, s_save_alloc_size, m0
	s_mov_b32	exec_lo, 0xFFFFFFFF
	s_mov_b32	exec_hi, 0x00000000

L_SAVE_SHARED_VGPR_WAVE64_LOOP:
	v_movrels_b32	v0, v0							//v0 = v[0+m0]
	buffer_store_dword	v0, v0, s_save_buf_rsrc0, s_save_mem_offset scope:SCOPE_SYS
	s_add_u32	m0, m0, 1						//next vgpr index
	s_add_u32	s_save_mem_offset, s_save_mem_offset, 128
	s_cmp_lt_u32	m0, s_save_alloc_size					//scc = (m0 < s_save_alloc_size) ? 1 : 0
	s_cbranch_scc1	L_SAVE_SHARED_VGPR_WAVE64_LOOP				//SHARED_VGPR save is complete?

L_SAVE_VGPR_END:
	s_branch	L_END_PGM

L_RESTORE:
	/* Setup Resource Contants */
	s_mov_b32	s_restore_buf_rsrc0, s_restore_spi_init_lo		//base_addr_lo
	s_and_b32	s_restore_buf_rsrc1, s_restore_spi_init_hi, 0x0000FFFF	//base_addr_hi
	s_or_b32	s_restore_buf_rsrc1, s_restore_buf_rsrc1, S_RESTORE_BUF_RSRC_WORD1_STRIDE
	s_mov_b32	s_restore_buf_rsrc2, 0					//NUM_RECORDS initial value = 0 (in bytes)
	s_mov_b32	s_restore_buf_rsrc3, S_RESTORE_BUF_RSRC_WORD3_MISC

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
	s_mov_b32	s_restore_buf_rsrc2, s_restore_alloc_size		//NUM_RECORDS in bytes

	// LDS at offset: size(VGPR)+size(SVGPR)+SIZE(SGPR)+SIZE(HWREG)
	//
	get_vgpr_size_bytes(s_restore_mem_offset, s_restore_size)
	get_svgpr_size_bytes(s_restore_tmp)
	s_add_u32	s_restore_mem_offset, s_restore_mem_offset, s_restore_tmp
	s_add_u32	s_restore_mem_offset, s_restore_mem_offset, get_sgpr_size_bytes()
	s_add_u32	s_restore_mem_offset, s_restore_mem_offset, get_hwreg_size_bytes()

	s_mov_b32	s_restore_buf_rsrc2, 0x1000000				//NUM_RECORDS in bytes

	s_lshr_b32	m0, s_restore_size, S_WAVE_SIZE
	s_and_b32	m0, m0, 1
	s_cmp_eq_u32	m0, 1
	s_mov_b32	m0, 0x0
	s_cbranch_scc1	L_RESTORE_LDS_LOOP_W64

L_RESTORE_LDS_LOOP_W32:
	buffer_load_dword       v0, v0, s_restore_buf_rsrc0, s_restore_mem_offset
	s_wait_idle
	ds_store_addtid_b32     v0
	s_add_u32	m0, m0, 128						// 128 DW
	s_add_u32	s_restore_mem_offset, s_restore_mem_offset, 128		//mem offset increased by 128DW
	s_cmp_lt_u32	m0, s_restore_alloc_size				//scc=(m0 < s_restore_alloc_size) ? 1 : 0
	s_cbranch_scc1	L_RESTORE_LDS_LOOP_W32					//LDS restore is complete?
	s_branch	L_RESTORE_VGPR

L_RESTORE_LDS_LOOP_W64:
	buffer_load_dword       v0, v0, s_restore_buf_rsrc0, s_restore_mem_offset
	s_wait_idle
	ds_store_addtid_b32     v0
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

	s_mov_b32	s_restore_buf_rsrc2, 0x1000000				//NUM_RECORDS in bytes

	// VGPR load using dw burst
	s_mov_b32	s_restore_mem_offset_save, s_restore_mem_offset		// restore start with v1, v0 will be the last
	s_add_u32	s_restore_mem_offset, s_restore_mem_offset, 128*4
	s_mov_b32	m0, 4							//VGPR initial index value = 4
	s_cmp_lt_u32	m0, s_restore_alloc_size
	s_cbranch_scc0	L_RESTORE_SGPR

L_RESTORE_VGPR_WAVE32_LOOP:
	buffer_load_dword	v0, v0, s_restore_buf_rsrc0, s_restore_mem_offset scope:SCOPE_SYS
	buffer_load_dword	v1, v0, s_restore_buf_rsrc0, s_restore_mem_offset scope:SCOPE_SYS offset:128
	buffer_load_dword	v2, v0, s_restore_buf_rsrc0, s_restore_mem_offset scope:SCOPE_SYS offset:128*2
	buffer_load_dword	v3, v0, s_restore_buf_rsrc0, s_restore_mem_offset scope:SCOPE_SYS offset:128*3
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
	buffer_load_dword	v0, v0, s_restore_buf_rsrc0, s_restore_mem_offset_save scope:SCOPE_SYS
	buffer_load_dword	v1, v0, s_restore_buf_rsrc0, s_restore_mem_offset_save scope:SCOPE_SYS offset:128
	buffer_load_dword	v2, v0, s_restore_buf_rsrc0, s_restore_mem_offset_save scope:SCOPE_SYS offset:128*2
	buffer_load_dword	v3, v0, s_restore_buf_rsrc0, s_restore_mem_offset_save scope:SCOPE_SYS offset:128*3
	s_wait_idle

	s_branch	L_RESTORE_SGPR

L_RESTORE_VGPR_WAVE64:
	s_mov_b32	s_restore_buf_rsrc2, 0x1000000				//NUM_RECORDS in bytes

	// VGPR load using dw burst
	s_mov_b32	s_restore_mem_offset_save, s_restore_mem_offset		// restore start with v4, v0 will be the last
	s_add_u32	s_restore_mem_offset, s_restore_mem_offset, 256*4
	s_mov_b32	m0, 4							//VGPR initial index value = 4
	s_cmp_lt_u32	m0, s_restore_alloc_size
	s_cbranch_scc0	L_RESTORE_SHARED_VGPR

L_RESTORE_VGPR_WAVE64_LOOP:
	buffer_load_dword	v0, v0, s_restore_buf_rsrc0, s_restore_mem_offset scope:SCOPE_SYS
	buffer_load_dword	v1, v0, s_restore_buf_rsrc0, s_restore_mem_offset scope:SCOPE_SYS offset:256
	buffer_load_dword	v2, v0, s_restore_buf_rsrc0, s_restore_mem_offset scope:SCOPE_SYS offset:256*2
	buffer_load_dword	v3, v0, s_restore_buf_rsrc0, s_restore_mem_offset scope:SCOPE_SYS offset:256*3
	s_wait_idle
	v_movreld_b32	v0, v0							//v[0+m0] = v0
	v_movreld_b32	v1, v1
	v_movreld_b32	v2, v2
	v_movreld_b32	v3, v3
	s_add_u32	m0, m0, 4						//next vgpr index
	s_add_u32	s_restore_mem_offset, s_restore_mem_offset, 256*4	//every buffer_load_dword does 256 bytes
	s_cmp_lt_u32	m0, s_restore_alloc_size				//scc = (m0 < s_restore_alloc_size) ? 1 : 0
	s_cbranch_scc1	L_RESTORE_VGPR_WAVE64_LOOP				//VGPR restore (except v0) is complete?

L_RESTORE_SHARED_VGPR:
	s_getreg_b32	s_restore_alloc_size, hwreg(HW_REG_WAVE_LDS_ALLOC,SQ_WAVE_LDS_ALLOC_VGPR_SHARED_SIZE_SHIFT,SQ_WAVE_LDS_ALLOC_VGPR_SHARED_SIZE_SIZE)	//shared_vgpr_size
	s_and_b32	s_restore_alloc_size, s_restore_alloc_size, 0xFFFFFFFF	//shared_vgpr_size is zero?
	s_cbranch_scc0	L_RESTORE_V0						//no shared_vgpr used?
	s_lshl_b32	s_restore_alloc_size, s_restore_alloc_size, 3		//Number of SHARED_VGPRs = shared_vgpr_size * 8    (non-zero value)
	//m0 now has the value of normal vgpr count, just add the m0 with shared_vgpr count to get the total count.
	//restore shared_vgpr will start from the index of m0
	s_add_u32	s_restore_alloc_size, s_restore_alloc_size, m0
	s_mov_b32	exec_lo, 0xFFFFFFFF
	s_mov_b32	exec_hi, 0x00000000
L_RESTORE_SHARED_VGPR_WAVE64_LOOP:
	buffer_load_dword	v0, v0, s_restore_buf_rsrc0, s_restore_mem_offset scope:SCOPE_SYS
	s_wait_idle
	v_movreld_b32	v0, v0							//v[0+m0] = v0
	s_add_u32	m0, m0, 1						//next vgpr index
	s_add_u32	s_restore_mem_offset, s_restore_mem_offset, 128
	s_cmp_lt_u32	m0, s_restore_alloc_size				//scc = (m0 < s_restore_alloc_size) ? 1 : 0
	s_cbranch_scc1	L_RESTORE_SHARED_VGPR_WAVE64_LOOP			//VGPR restore (except v0) is complete?

	s_mov_b32	exec_hi, 0xFFFFFFFF					//restore back exec_hi before restoring V0!!

	/* VGPR restore on v0 */
L_RESTORE_V0:
	buffer_load_dword	v0, v0, s_restore_buf_rsrc0, s_restore_mem_offset_save scope:SCOPE_SYS
	buffer_load_dword	v1, v0, s_restore_buf_rsrc0, s_restore_mem_offset_save scope:SCOPE_SYS offset:256
	buffer_load_dword	v2, v0, s_restore_buf_rsrc0, s_restore_mem_offset_save scope:SCOPE_SYS offset:256*2
	buffer_load_dword	v3, v0, s_restore_buf_rsrc0, s_restore_mem_offset_save scope:SCOPE_SYS offset:256*3
	s_wait_idle

	/* restore SGPRs */
	//will be 2+8+16*6
	// SGPR SR memory offset : size(VGPR)+size(SVGPR)
L_RESTORE_SGPR:
	get_vgpr_size_bytes(s_restore_mem_offset, s_restore_size)
	get_svgpr_size_bytes(s_restore_tmp)
	s_add_u32	s_restore_mem_offset, s_restore_mem_offset, s_restore_tmp
	s_add_u32	s_restore_mem_offset, s_restore_mem_offset, get_sgpr_size_bytes()
	s_sub_u32	s_restore_mem_offset, s_restore_mem_offset, 20*4	//s108~s127 is not saved

	s_mov_b32	s_restore_buf_rsrc2, 0x1000000				//NUM_RECORDS in bytes

	s_mov_b32	m0, s_sgpr_save_num

	read_4sgpr_from_mem(s0, s_restore_buf_rsrc0, s_restore_mem_offset)
	s_wait_idle

	s_sub_u32	m0, m0, 4						// Restore from S[0] to S[104]
	s_nop		0							// hazard SALU M0=> S_MOVREL

	s_movreld_b64	s0, s0							//s[0+m0] = s0
	s_movreld_b64	s2, s2

	read_8sgpr_from_mem(s0, s_restore_buf_rsrc0, s_restore_mem_offset)
	s_wait_idle

	s_sub_u32	m0, m0, 8						// Restore from S[0] to S[96]
	s_nop		0							// hazard SALU M0=> S_MOVREL

	s_movreld_b64	s0, s0							//s[0+m0] = s0
	s_movreld_b64	s2, s2
	s_movreld_b64	s4, s4
	s_movreld_b64	s6, s6

 L_RESTORE_SGPR_LOOP:
	read_16sgpr_from_mem(s0, s_restore_buf_rsrc0, s_restore_mem_offset)
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
	// HWREG SR memory offset : size(VGPR)+size(SVGPR)+size(SGPR)
	get_vgpr_size_bytes(s_restore_mem_offset, s_restore_size)
	get_svgpr_size_bytes(s_restore_tmp)
	s_add_u32	s_restore_mem_offset, s_restore_mem_offset, s_restore_tmp
	s_add_u32	s_restore_mem_offset, s_restore_mem_offset, get_sgpr_size_bytes()

	s_mov_b32	s_restore_buf_rsrc2, 0x1000000				//NUM_RECORDS in bytes

	// Restore s_restore_spi_init_hi before the saved value gets clobbered.
	s_mov_b32 s_restore_spi_init_hi, s_restore_spi_init_hi_save

	read_hwreg_from_mem(s_restore_m0, s_restore_buf_rsrc0, s_restore_mem_offset)
	read_hwreg_from_mem(s_restore_pc_lo, s_restore_buf_rsrc0, s_restore_mem_offset)
	read_hwreg_from_mem(s_restore_pc_hi, s_restore_buf_rsrc0, s_restore_mem_offset)
	read_hwreg_from_mem(s_restore_exec_lo, s_restore_buf_rsrc0, s_restore_mem_offset)
	read_hwreg_from_mem(s_restore_exec_hi, s_restore_buf_rsrc0, s_restore_mem_offset)
	read_hwreg_from_mem(s_restore_state_priv, s_restore_buf_rsrc0, s_restore_mem_offset)
	read_hwreg_from_mem(s_restore_excp_flag_priv, s_restore_buf_rsrc0, s_restore_mem_offset)
	read_hwreg_from_mem(s_restore_xnack_mask, s_restore_buf_rsrc0, s_restore_mem_offset)
	read_hwreg_from_mem(s_restore_mode, s_restore_buf_rsrc0, s_restore_mem_offset)
	read_hwreg_from_mem(s_restore_flat_scratch, s_restore_buf_rsrc0, s_restore_mem_offset)
	s_wait_idle

	s_setreg_b32	hwreg(HW_REG_WAVE_SCRATCH_BASE_LO), s_restore_flat_scratch

	read_hwreg_from_mem(s_restore_flat_scratch, s_restore_buf_rsrc0, s_restore_mem_offset)
	s_wait_idle

	s_setreg_b32	hwreg(HW_REG_WAVE_SCRATCH_BASE_HI), s_restore_flat_scratch

	read_hwreg_from_mem(s_restore_tmp, s_restore_buf_rsrc0, s_restore_mem_offset)
	s_wait_idle
	s_setreg_b32	hwreg(HW_REG_WAVE_EXCP_FLAG_USER), s_restore_tmp

	read_hwreg_from_mem(s_restore_tmp, s_restore_buf_rsrc0, s_restore_mem_offset)
	s_wait_idle
	s_setreg_b32	hwreg(HW_REG_WAVE_TRAP_CTRL), s_restore_tmp

	// Only the first wave needs to restore the workgroup barrier.
	s_and_b32	s_restore_tmp, s_restore_spi_init_hi, S_RESTORE_SPI_INIT_FIRST_WAVE_MASK
	s_cbranch_scc0	L_SKIP_BARRIER_RESTORE

	// Skip over WAVE_STATUS, since there is no state to restore from it
	s_add_u32	s_restore_mem_offset, s_restore_mem_offset, 4

	read_hwreg_from_mem(s_restore_tmp, s_restore_buf_rsrc0, s_restore_mem_offset)
	s_wait_idle

	s_bitcmp1_b32	s_restore_tmp, BARRIER_STATE_VALID_OFFSET
	s_cbranch_scc0	L_SKIP_BARRIER_RESTORE

	// extract the saved signal count from s_restore_tmp
	s_lshr_b32	s_restore_tmp, s_restore_tmp, BARRIER_STATE_SIGNAL_OFFSET

	// We need to call s_barrier_signal repeatedly to restore the signal
	// count of the work group barrier.  The member count is already
	// initialized with the number of waves in the work group.
L_BARRIER_RESTORE_LOOP:
	s_and_b32	s_restore_tmp, s_restore_tmp, s_restore_tmp
	s_cbranch_scc0	L_SKIP_BARRIER_RESTORE
	s_barrier_signal	-1
	s_add_i32	s_restore_tmp, s_restore_tmp, -1
	s_branch	L_BARRIER_RESTORE_LOOP

L_SKIP_BARRIER_RESTORE:

	s_mov_b32	m0, s_restore_m0
	s_mov_b32	exec_lo, s_restore_exec_lo
	s_mov_b32	exec_hi, s_restore_exec_hi

	// EXCP_FLAG_PRIV.SAVE_CONTEXT and HOST_TRAP may have changed.
	// Only restore the other fields to avoid clobbering them.
	s_setreg_b32	hwreg(HW_REG_WAVE_EXCP_FLAG_PRIV, 0, SQ_WAVE_EXCP_FLAG_PRIV_RESTORE_PART_1_SIZE), s_restore_excp_flag_priv
	s_lshr_b32	s_restore_excp_flag_priv, s_restore_excp_flag_priv, SQ_WAVE_EXCP_FLAG_PRIV_RESTORE_PART_2_SHIFT
	s_setreg_b32	hwreg(HW_REG_WAVE_EXCP_FLAG_PRIV, SQ_WAVE_EXCP_FLAG_PRIV_RESTORE_PART_2_SHIFT, SQ_WAVE_EXCP_FLAG_PRIV_RESTORE_PART_2_SIZE), s_restore_excp_flag_priv
	s_lshr_b32	s_restore_excp_flag_priv, s_restore_excp_flag_priv, SQ_WAVE_EXCP_FLAG_PRIV_RESTORE_PART_3_SHIFT - SQ_WAVE_EXCP_FLAG_PRIV_RESTORE_PART_2_SHIFT
	s_setreg_b32	hwreg(HW_REG_WAVE_EXCP_FLAG_PRIV, SQ_WAVE_EXCP_FLAG_PRIV_RESTORE_PART_3_SHIFT, SQ_WAVE_EXCP_FLAG_PRIV_RESTORE_PART_3_SIZE), s_restore_excp_flag_priv

	s_setreg_b32	hwreg(HW_REG_WAVE_MODE), s_restore_mode

	// Restore trap temporaries 4-11, 13 initialized by SPI debug dispatch logic
	// ttmp SR memory offset : size(VGPR)+size(SVGPR)+size(SGPR)+0x40
	get_vgpr_size_bytes(s_restore_ttmps_lo, s_restore_size)
	get_svgpr_size_bytes(s_restore_ttmps_hi)
	s_add_u32	s_restore_ttmps_lo, s_restore_ttmps_lo, s_restore_ttmps_hi
	s_add_u32	s_restore_ttmps_lo, s_restore_ttmps_lo, get_sgpr_size_bytes()
	s_add_u32	s_restore_ttmps_lo, s_restore_ttmps_lo, s_restore_buf_rsrc0
	s_addc_u32	s_restore_ttmps_hi, s_restore_buf_rsrc1, 0x0
	s_and_b32	s_restore_ttmps_hi, s_restore_ttmps_hi, 0xFFFF
	s_load_dwordx4	[ttmp4, ttmp5, ttmp6, ttmp7], [s_restore_ttmps_lo, s_restore_ttmps_hi], 0x50 scope:SCOPE_SYS
	s_load_dwordx4	[ttmp8, ttmp9, ttmp10, ttmp11], [s_restore_ttmps_lo, s_restore_ttmps_hi], 0x60 scope:SCOPE_SYS
	s_load_dword	ttmp13, [s_restore_ttmps_lo, s_restore_ttmps_hi], 0x74 scope:SCOPE_SYS
	s_wait_idle

	s_and_b32	s_restore_pc_hi, s_restore_pc_hi, 0x0000ffff		//pc[47:32] //Do it here in order not to affect STATUS
	s_and_b64	exec, exec, exec					// Restore STATUS.EXECZ, not writable by s_setreg_b32
	s_and_b64	vcc, vcc, vcc						// Restore STATUS.VCCZ, not writable by s_setreg_b32

	s_setreg_b32	hwreg(HW_REG_WAVE_STATE_PRIV), s_restore_state_priv	// SCC is included, which is changed by previous salu

	// Make barrier and LDS state visible to all waves in the group.
	// STATE_PRIV.BARRIER_COMPLETE may change after this point.
	s_barrier_signal	-2
	s_barrier_wait	-2

	s_rfe_b64	s_restore_pc_lo						//Return to the main shader program and resume execution

L_END_PGM:
	// Make sure that no wave of the workgroup can exit the trap handler
	// before the workgroup barrier state is saved.
	s_barrier_signal	-2
	s_barrier_wait	-2
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

function read_hwreg_from_mem(s, s_rsrc, s_mem_offset)
	s_buffer_load_dword	s, s_rsrc, s_mem_offset scope:SCOPE_SYS
	s_add_u32	s_mem_offset, s_mem_offset, 4
end

function read_16sgpr_from_mem(s, s_rsrc, s_mem_offset)
	s_sub_u32	s_mem_offset, s_mem_offset, 4*16
	s_buffer_load_dwordx16	s, s_rsrc, s_mem_offset scope:SCOPE_SYS
end

function read_8sgpr_from_mem(s, s_rsrc, s_mem_offset)
	s_sub_u32	s_mem_offset, s_mem_offset, 4*8
	s_buffer_load_dwordx8	s, s_rsrc, s_mem_offset scope:SCOPE_SYS
end

function read_4sgpr_from_mem(s, s_rsrc, s_mem_offset)
	s_sub_u32	s_mem_offset, s_mem_offset, 4*4
	s_buffer_load_dwordx4	s, s_rsrc, s_mem_offset scope:SCOPE_SYS
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

function get_svgpr_size_bytes(s_svgpr_size_byte)
	s_getreg_b32	s_svgpr_size_byte, hwreg(HW_REG_WAVE_LDS_ALLOC,SQ_WAVE_LDS_ALLOC_VGPR_SHARED_SIZE_SHIFT,SQ_WAVE_LDS_ALLOC_VGPR_SHARED_SIZE_SIZE)
	s_lshl_b32	s_svgpr_size_byte, s_svgpr_size_byte, (3+7)
end

function get_sgpr_size_bytes
	return 512
end

function get_hwreg_size_bytes
	return 128
end

function get_wave_size2(s_reg)
	s_getreg_b32	s_reg, hwreg(HW_REG_WAVE_STATUS,SQ_WAVE_STATUS_WAVE64_SHIFT,SQ_WAVE_STATUS_WAVE64_SIZE)
	s_lshl_b32	s_reg, s_reg, S_WAVE_SIZE
end
