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

var SQ_WAVE_STATUS_INST_ATC_SHIFT		= 23
var SQ_WAVE_STATUS_INST_ATC_MASK		= 0x00800000
var SQ_WAVE_STATUS_SPI_PRIO_MASK		= 0x00000006
var SQ_WAVE_STATUS_HALT_MASK			= 0x2000

var SQ_WAVE_LDS_ALLOC_LDS_SIZE_SHIFT		= 12
var SQ_WAVE_LDS_ALLOC_LDS_SIZE_SIZE		= 9
var SQ_WAVE_GPR_ALLOC_VGPR_SIZE_SHIFT		= 8
var SQ_WAVE_GPR_ALLOC_VGPR_SIZE_SIZE		= 6
var SQ_WAVE_GPR_ALLOC_SGPR_SIZE_SHIFT		= 24
var SQ_WAVE_GPR_ALLOC_SGPR_SIZE_SIZE		= 4
var SQ_WAVE_LDS_ALLOC_VGPR_SHARED_SIZE_SHIFT	= 24
var SQ_WAVE_LDS_ALLOC_VGPR_SHARED_SIZE_SIZE	= 4
var SQ_WAVE_IB_STS2_WAVE64_SHIFT		= 11
var SQ_WAVE_IB_STS2_WAVE64_SIZE			= 1

var SQ_WAVE_TRAPSTS_SAVECTX_MASK		= 0x400
var SQ_WAVE_TRAPSTS_EXCE_MASK			= 0x1FF
var SQ_WAVE_TRAPSTS_SAVECTX_SHIFT		= 10
var SQ_WAVE_TRAPSTS_MEM_VIOL_MASK		= 0x100
var SQ_WAVE_TRAPSTS_MEM_VIOL_SHIFT		= 8
var SQ_WAVE_TRAPSTS_PRE_SAVECTX_MASK		= 0x3FF
var SQ_WAVE_TRAPSTS_PRE_SAVECTX_SHIFT		= 0x0
var SQ_WAVE_TRAPSTS_PRE_SAVECTX_SIZE		= 10
var SQ_WAVE_TRAPSTS_POST_SAVECTX_MASK		= 0xFFFFF800
var SQ_WAVE_TRAPSTS_POST_SAVECTX_SHIFT		= 11
var SQ_WAVE_TRAPSTS_POST_SAVECTX_SIZE		= 21
var SQ_WAVE_TRAPSTS_ILLEGAL_INST_MASK		= 0x800

var SQ_WAVE_IB_STS_RCNT_SHIFT			= 16
var SQ_WAVE_IB_STS_FIRST_REPLAY_SHIFT		= 15
var SQ_WAVE_IB_STS_REPLAY_W64H_SHIFT		= 25
var SQ_WAVE_IB_STS_REPLAY_W64H_SIZE		= 1
var SQ_WAVE_IB_STS_REPLAY_W64H_MASK		= 0x02000000
var SQ_WAVE_IB_STS_FIRST_REPLAY_SIZE		= 1
var SQ_WAVE_IB_STS_RCNT_SIZE			= 6
var SQ_WAVE_IB_STS_RCNT_FIRST_REPLAY_MASK	= 0x003F8000
var SQ_WAVE_IB_STS_RCNT_FIRST_REPLAY_MASK_NEG	= 0x00007FFF

var SQ_BUF_RSRC_WORD1_ATC_SHIFT			= 24
var SQ_BUF_RSRC_WORD3_MTYPE_SHIFT		= 27

// bits [31:24] unused by SPI debug data
var TTMP11_SAVE_REPLAY_W64H_SHIFT		= 31
var TTMP11_SAVE_REPLAY_W64H_MASK		= 0x80000000
var TTMP11_SAVE_RCNT_FIRST_REPLAY_SHIFT		= 24
var TTMP11_SAVE_RCNT_FIRST_REPLAY_MASK		= 0x7F000000

// SQ_SEL_X/Y/Z/W, BUF_NUM_FORMAT_FLOAT, (0 for MUBUF stride[17:14]
// when ADD_TID_ENABLE and BUF_DATA_FORMAT_32 for MTBUF), ADD_TID_ENABLE
var S_SAVE_BUF_RSRC_WORD1_STRIDE		= 0x00040000
var S_SAVE_BUF_RSRC_WORD3_MISC			= 0x10807FAC

var S_SAVE_SPI_INIT_ATC_MASK			= 0x08000000
var S_SAVE_SPI_INIT_ATC_SHIFT			= 27
var S_SAVE_SPI_INIT_MTYPE_MASK			= 0x70000000
var S_SAVE_SPI_INIT_MTYPE_SHIFT			= 28
var S_SAVE_SPI_INIT_FIRST_WAVE_MASK		= 0x04000000
var S_SAVE_SPI_INIT_FIRST_WAVE_SHIFT		= 26

var S_SAVE_PC_HI_RCNT_SHIFT			= 26
var S_SAVE_PC_HI_RCNT_MASK			= 0xFC000000
var S_SAVE_PC_HI_FIRST_REPLAY_SHIFT		= 25
var S_SAVE_PC_HI_FIRST_REPLAY_MASK		= 0x02000000
var S_SAVE_PC_HI_REPLAY_W64H_SHIFT		= 24
var S_SAVE_PC_HI_REPLAY_W64H_MASK		= 0x01000000

var s_sgpr_save_num				= 108

var s_save_spi_init_lo				= exec_lo
var s_save_spi_init_hi				= exec_hi
var s_save_pc_lo				= ttmp0
var s_save_pc_hi				= ttmp1
var s_save_exec_lo				= ttmp2
var s_save_exec_hi				= ttmp3
var s_save_status				= ttmp12
var s_save_trapsts				= ttmp5
var s_save_xnack_mask				= ttmp6
var s_wave_size					= ttmp7
var s_save_buf_rsrc0				= ttmp8
var s_save_buf_rsrc1				= ttmp9
var s_save_buf_rsrc2				= ttmp10
var s_save_buf_rsrc3				= ttmp11
var s_save_mem_offset				= ttmp14
var s_save_alloc_size				= s_save_trapsts
var s_save_tmp					= s_save_buf_rsrc2
var s_save_m0					= ttmp15

var S_RESTORE_BUF_RSRC_WORD1_STRIDE		= S_SAVE_BUF_RSRC_WORD1_STRIDE
var S_RESTORE_BUF_RSRC_WORD3_MISC		= S_SAVE_BUF_RSRC_WORD3_MISC

var S_RESTORE_SPI_INIT_ATC_MASK			= 0x08000000
var S_RESTORE_SPI_INIT_ATC_SHIFT		= 27
var S_RESTORE_SPI_INIT_MTYPE_MASK		= 0x70000000
var S_RESTORE_SPI_INIT_MTYPE_SHIFT		= 28
var S_RESTORE_SPI_INIT_FIRST_WAVE_MASK		= 0x04000000
var S_RESTORE_SPI_INIT_FIRST_WAVE_SHIFT		= 26
var S_WAVE_SIZE					= 25

var S_RESTORE_PC_HI_RCNT_SHIFT			= S_SAVE_PC_HI_RCNT_SHIFT
var S_RESTORE_PC_HI_RCNT_MASK			= S_SAVE_PC_HI_RCNT_MASK
var S_RESTORE_PC_HI_FIRST_REPLAY_SHIFT		= S_SAVE_PC_HI_FIRST_REPLAY_SHIFT
var S_RESTORE_PC_HI_FIRST_REPLAY_MASK		= S_SAVE_PC_HI_FIRST_REPLAY_MASK

var s_restore_spi_init_lo			= exec_lo
var s_restore_spi_init_hi			= exec_hi
var s_restore_mem_offset			= ttmp12
var s_restore_alloc_size			= ttmp3
var s_restore_tmp				= ttmp6
var s_restore_mem_offset_save			= s_restore_tmp
var s_restore_m0				= s_restore_alloc_size
var s_restore_mode				= ttmp7
var s_restore_flat_scratch			= ttmp2
var s_restore_pc_lo				= ttmp0
var s_restore_pc_hi				= ttmp1
var s_restore_exec_lo				= ttmp14
var s_restore_exec_hi				= ttmp15
var s_restore_status				= ttmp4
var s_restore_trapsts				= ttmp5
var s_restore_xnack_mask			= ttmp13
var s_restore_buf_rsrc0				= ttmp8
var s_restore_buf_rsrc1				= ttmp9
var s_restore_buf_rsrc2				= ttmp10
var s_restore_buf_rsrc3				= ttmp11
var s_restore_size				= ttmp7

shader main
	asic(DEFAULT)
	type(CS)
	wave_size(32)

	s_branch	L_SKIP_RESTORE						//NOT restore. might be a regular trap or save

L_JUMP_TO_RESTORE:
	s_branch	L_RESTORE

L_SKIP_RESTORE:
	s_getreg_b32	s_save_status, hwreg(HW_REG_STATUS)			//save STATUS since we will change SCC
	s_andn2_b32	s_save_status, s_save_status, SQ_WAVE_STATUS_SPI_PRIO_MASK
	s_getreg_b32	s_save_trapsts, hwreg(HW_REG_TRAPSTS)
	s_and_b32	ttmp2, s_save_trapsts, SQ_WAVE_TRAPSTS_SAVECTX_MASK	//check whether this is for save
	s_cbranch_scc1	L_SAVE

	// If STATUS.MEM_VIOL is asserted then halt the wave to prevent
	// the exception raising again and blocking context save.
	s_and_b32	ttmp2, s_save_trapsts, SQ_WAVE_TRAPSTS_MEM_VIOL_MASK
	s_cbranch_scc0	L_FETCH_2ND_TRAP
	s_or_b32	s_save_status, s_save_status, SQ_WAVE_STATUS_HALT_MASK

L_FETCH_2ND_TRAP:
	// Preserve and clear scalar XNACK state before issuing scalar loads.
	// Save IB_STS.REPLAY_W64H[25], RCNT[21:16], FIRST_REPLAY[15] into
	// unused space ttmp11[31:24].
	s_andn2_b32	ttmp11, ttmp11, (TTMP11_SAVE_REPLAY_W64H_MASK | TTMP11_SAVE_RCNT_FIRST_REPLAY_MASK)
	s_getreg_b32	ttmp2, hwreg(HW_REG_IB_STS)
	s_and_b32	ttmp3, ttmp2, SQ_WAVE_IB_STS_REPLAY_W64H_MASK
	s_lshl_b32	ttmp3, ttmp3, (TTMP11_SAVE_REPLAY_W64H_SHIFT - SQ_WAVE_IB_STS_REPLAY_W64H_SHIFT)
	s_or_b32	ttmp11, ttmp11, ttmp3
	s_and_b32	ttmp3, ttmp2, SQ_WAVE_IB_STS_RCNT_FIRST_REPLAY_MASK
	s_lshl_b32	ttmp3, ttmp3, (TTMP11_SAVE_RCNT_FIRST_REPLAY_SHIFT - SQ_WAVE_IB_STS_FIRST_REPLAY_SHIFT)
	s_or_b32	ttmp11, ttmp11, ttmp3
	s_andn2_b32	ttmp2, ttmp2, (SQ_WAVE_IB_STS_REPLAY_W64H_MASK | SQ_WAVE_IB_STS_RCNT_FIRST_REPLAY_MASK)
	s_setreg_b32	hwreg(HW_REG_IB_STS), ttmp2

	// Read second-level TBA/TMA from first-level TMA and jump if available.
	// ttmp[2:5] and ttmp12 can be used (others hold SPI-initialized debug data)
	// ttmp12 holds SQ_WAVE_STATUS
	s_getreg_b32	ttmp14, hwreg(HW_REG_SHADER_TMA_LO)
	s_getreg_b32	ttmp15, hwreg(HW_REG_SHADER_TMA_HI)
	s_lshl_b64	[ttmp14, ttmp15], [ttmp14, ttmp15], 0x8
	s_load_dwordx2	[ttmp2, ttmp3], [ttmp14, ttmp15], 0x0 glc:1		// second-level TBA
	s_waitcnt	lgkmcnt(0)
	s_load_dwordx2	[ttmp14, ttmp15], [ttmp14, ttmp15], 0x8 glc:1		// second-level TMA
	s_waitcnt	lgkmcnt(0)
	s_and_b64	[ttmp2, ttmp3], [ttmp2, ttmp3], [ttmp2, ttmp3]
	s_cbranch_scc0	L_NO_NEXT_TRAP						// second-level trap handler not been set
	s_setpc_b64	[ttmp2, ttmp3]						// jump to second-level trap handler

L_NO_NEXT_TRAP:
	s_getreg_b32	s_save_trapsts, hwreg(HW_REG_TRAPSTS)
	s_and_b32	s_save_trapsts, s_save_trapsts, SQ_WAVE_TRAPSTS_EXCE_MASK
	s_cbranch_scc1	L_EXCP_CASE						// Exception, jump back to the shader program directly.
	s_add_u32	ttmp0, ttmp0, 4						// S_TRAP case, add 4 to ttmp0
	s_addc_u32	ttmp1, ttmp1, 0
L_EXCP_CASE:
	s_and_b32	ttmp1, ttmp1, 0xFFFF

	// Restore SQ_WAVE_IB_STS.
	s_lshr_b32	ttmp2, ttmp11, (TTMP11_SAVE_RCNT_FIRST_REPLAY_SHIFT - SQ_WAVE_IB_STS_FIRST_REPLAY_SHIFT)
	s_and_b32	ttmp3, ttmp2, SQ_WAVE_IB_STS_RCNT_FIRST_REPLAY_MASK
	s_lshr_b32	ttmp2, ttmp11, (TTMP11_SAVE_REPLAY_W64H_SHIFT - SQ_WAVE_IB_STS_REPLAY_W64H_SHIFT)
	s_and_b32	ttmp2, ttmp2, SQ_WAVE_IB_STS_REPLAY_W64H_MASK
	s_or_b32	ttmp2, ttmp2, ttmp3
	s_setreg_b32	hwreg(HW_REG_IB_STS), ttmp2

	// Restore SQ_WAVE_STATUS.
	s_and_b64	exec, exec, exec					// Restore STATUS.EXECZ, not writable by s_setreg_b32
	s_and_b64	vcc, vcc, vcc						// Restore STATUS.VCCZ, not writable by s_setreg_b32
	s_setreg_b32	hwreg(HW_REG_STATUS), s_save_status

	s_rfe_b64	[ttmp0, ttmp1]

L_SAVE:
	//check whether there is mem_viol
	s_getreg_b32	s_save_trapsts, hwreg(HW_REG_TRAPSTS)
	s_and_b32	s_save_trapsts, s_save_trapsts, SQ_WAVE_TRAPSTS_MEM_VIOL_MASK
	s_cbranch_scc0	L_NO_PC_REWIND

	//if so, need rewind PC assuming GDS operation gets NACKed
	s_mov_b32	s_save_tmp, 0
	s_setreg_b32	hwreg(HW_REG_TRAPSTS, SQ_WAVE_TRAPSTS_MEM_VIOL_SHIFT, 1), s_save_tmp	//clear mem_viol bit
	s_and_b32	s_save_pc_hi, s_save_pc_hi, 0x0000ffff			//pc[47:32]
	s_sub_u32	s_save_pc_lo, s_save_pc_lo, 8				//pc[31:0]-8
	s_subb_u32	s_save_pc_hi, s_save_pc_hi, 0x0

L_NO_PC_REWIND:
	s_mov_b32	s_save_tmp, 0
	s_setreg_b32	hwreg(HW_REG_TRAPSTS, SQ_WAVE_TRAPSTS_SAVECTX_SHIFT, 1), s_save_tmp	//clear saveCtx bit

	s_getreg_b32	s_save_xnack_mask, hwreg(HW_REG_SHADER_XNACK_MASK)
	s_getreg_b32	s_save_tmp, hwreg(HW_REG_IB_STS, SQ_WAVE_IB_STS_RCNT_SHIFT, SQ_WAVE_IB_STS_RCNT_SIZE)
	s_lshl_b32	s_save_tmp, s_save_tmp, S_SAVE_PC_HI_RCNT_SHIFT
	s_or_b32	s_save_pc_hi, s_save_pc_hi, s_save_tmp
	s_getreg_b32	s_save_tmp, hwreg(HW_REG_IB_STS, SQ_WAVE_IB_STS_FIRST_REPLAY_SHIFT, SQ_WAVE_IB_STS_FIRST_REPLAY_SIZE)
	s_lshl_b32	s_save_tmp, s_save_tmp, S_SAVE_PC_HI_FIRST_REPLAY_SHIFT
	s_or_b32	s_save_pc_hi, s_save_pc_hi, s_save_tmp
	s_getreg_b32	s_save_tmp, hwreg(HW_REG_IB_STS, SQ_WAVE_IB_STS_REPLAY_W64H_SHIFT, SQ_WAVE_IB_STS_REPLAY_W64H_SIZE)
	s_lshl_b32	s_save_tmp, s_save_tmp, S_SAVE_PC_HI_REPLAY_W64H_SHIFT
	s_or_b32	s_save_pc_hi, s_save_pc_hi, s_save_tmp
	s_getreg_b32	s_save_tmp, hwreg(HW_REG_IB_STS)			//clear RCNT and FIRST_REPLAY and REPLAY_W64H in IB_STS
	s_and_b32	s_save_tmp, s_save_tmp, SQ_WAVE_IB_STS_RCNT_FIRST_REPLAY_MASK_NEG

	s_setreg_b32	hwreg(HW_REG_IB_STS), s_save_tmp

	/* inform SPI the readiness and wait for SPI's go signal */
	s_mov_b32	s_save_exec_lo, exec_lo					//save EXEC and use EXEC for the go signal from SPI
	s_mov_b32	s_save_exec_hi, exec_hi
	s_mov_b64	exec, 0x0						//clear EXEC to get ready to receive

	s_sendmsg	sendmsg(MSG_SAVEWAVE)					//send SPI a message and wait for SPI's write to EXEC

L_SLEEP:
	// sleep 1 (64clk) is not enough for 8 waves per SIMD, which will cause
	// SQ hang, since the 7,8th wave could not get arbit to exec inst, while
	// other waves are stuck into the sleep-loop and waiting for wrexec!=0
	s_sleep		0x2
	s_cbranch_execz	L_SLEEP

	/* setup Resource Contants */
	s_mov_b32	s_save_buf_rsrc0, s_save_spi_init_lo			//base_addr_lo
	s_and_b32	s_save_buf_rsrc1, s_save_spi_init_hi, 0x0000FFFF	//base_addr_hi
	s_or_b32	s_save_buf_rsrc1, s_save_buf_rsrc1, S_SAVE_BUF_RSRC_WORD1_STRIDE
	s_mov_b32	s_save_buf_rsrc2, 0					//NUM_RECORDS initial value = 0 (in bytes) although not neccessarily inited
	s_mov_b32	s_save_buf_rsrc3, S_SAVE_BUF_RSRC_WORD3_MISC
	s_and_b32	s_save_tmp, s_save_spi_init_hi, S_SAVE_SPI_INIT_ATC_MASK
	s_lshr_b32	s_save_tmp, s_save_tmp, (S_SAVE_SPI_INIT_ATC_SHIFT-SQ_BUF_RSRC_WORD1_ATC_SHIFT)
	s_or_b32	s_save_buf_rsrc3, s_save_buf_rsrc3, s_save_tmp		//or ATC
	s_and_b32	s_save_tmp, s_save_spi_init_hi, S_SAVE_SPI_INIT_MTYPE_MASK
	s_lshr_b32	s_save_tmp, s_save_tmp, (S_SAVE_SPI_INIT_MTYPE_SHIFT-SQ_BUF_RSRC_WORD3_MTYPE_SHIFT)
	s_or_b32	s_save_buf_rsrc3, s_save_buf_rsrc3, s_save_tmp		//or MTYPE

	s_mov_b32	s_save_m0, m0

	/* global mem offset */
	s_mov_b32	s_save_mem_offset, 0x0
	s_getreg_b32	s_wave_size, hwreg(HW_REG_IB_STS2,SQ_WAVE_IB_STS2_WAVE64_SHIFT,SQ_WAVE_IB_STS2_WAVE64_SIZE)
	s_lshl_b32	s_wave_size, s_wave_size, S_WAVE_SIZE
	s_or_b32	s_wave_size, s_save_spi_init_hi, s_wave_size		//share s_wave_size with exec_hi, it's at bit25

	/* save HW registers */

L_SAVE_HWREG:
	// HWREG SR memory offset : size(VGPR)+size(SVGPR)+size(SGPR)
	get_vgpr_size_bytes(s_save_mem_offset, s_wave_size)
	get_svgpr_size_bytes(s_save_tmp)
	s_add_u32	s_save_mem_offset, s_save_mem_offset, s_save_tmp
	s_add_u32	s_save_mem_offset, s_save_mem_offset, get_sgpr_size_bytes()

	s_mov_b32	s_save_buf_rsrc2, 0x1000000				//NUM_RECORDS in bytes

	write_hwreg_to_mem(s_save_m0, s_save_buf_rsrc0, s_save_mem_offset)
	write_hwreg_to_mem(s_save_pc_lo, s_save_buf_rsrc0, s_save_mem_offset)
	write_hwreg_to_mem(s_save_pc_hi, s_save_buf_rsrc0, s_save_mem_offset)
	write_hwreg_to_mem(s_save_exec_lo, s_save_buf_rsrc0, s_save_mem_offset)
	write_hwreg_to_mem(s_save_exec_hi, s_save_buf_rsrc0, s_save_mem_offset)
	write_hwreg_to_mem(s_save_status, s_save_buf_rsrc0, s_save_mem_offset)

	s_getreg_b32	s_save_trapsts, hwreg(HW_REG_TRAPSTS)
	write_hwreg_to_mem(s_save_trapsts, s_save_buf_rsrc0, s_save_mem_offset)
	write_hwreg_to_mem(s_save_xnack_mask, s_save_buf_rsrc0, s_save_mem_offset)

	s_getreg_b32	s_save_m0, hwreg(HW_REG_MODE)
	write_hwreg_to_mem(s_save_m0, s_save_buf_rsrc0, s_save_mem_offset)

	s_getreg_b32	s_save_m0, hwreg(HW_REG_SHADER_FLAT_SCRATCH_LO)
	write_hwreg_to_mem(s_save_m0, s_save_buf_rsrc0, s_save_mem_offset)

	s_getreg_b32	s_save_m0, hwreg(HW_REG_SHADER_FLAT_SCRATCH_HI)
	write_hwreg_to_mem(s_save_m0, s_save_buf_rsrc0, s_save_mem_offset)

	/* the first wave in the threadgroup */
	s_and_b32	s_save_tmp, s_save_spi_init_hi, S_SAVE_SPI_INIT_FIRST_WAVE_MASK
	s_mov_b32	s_save_exec_hi, 0x0
	s_or_b32	s_save_exec_hi, s_save_tmp, s_save_exec_hi		// save first wave bit to s_save_exec_hi.bits[26]

	/* save SGPRs */
	// Save SGPR before LDS save, then the s0 to s4 can be used during LDS save...

	// SGPR SR memory offset : size(VGPR)+size(SVGPR)
	get_vgpr_size_bytes(s_save_mem_offset, s_wave_size)
	get_svgpr_size_bytes(s_save_tmp)
	s_add_u32	s_save_mem_offset, s_save_mem_offset, s_save_tmp
	s_mov_b32	s_save_buf_rsrc2, 0x1000000				//NUM_RECORDS in bytes

	// backup s_save_buf_rsrc0,1 to s_save_pc_lo/hi, since write_16sgpr_to_mem function will change the rsrc0
	s_mov_b32	s_save_xnack_mask, s_save_buf_rsrc0
	s_add_u32	s_save_buf_rsrc0, s_save_buf_rsrc0, s_save_mem_offset
	s_addc_u32	s_save_buf_rsrc1, s_save_buf_rsrc1, 0

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

	write_16sgpr_to_mem(s0, s_save_buf_rsrc0, s_save_mem_offset)
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
	write_12sgpr_to_mem(s0, s_save_buf_rsrc0, s_save_mem_offset)

	// restore s_save_buf_rsrc0,1
	s_mov_b32	s_save_buf_rsrc0, s_save_xnack_mask

	/* save first 4 VGPR, then LDS save could use   */
	// each wave will alloc 4 vgprs at least...

	s_mov_b32	s_save_mem_offset, 0
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

	buffer_store_dword	v0, v0, s_save_buf_rsrc0, s_save_mem_offset slc:1 glc:1
	buffer_store_dword	v1, v0, s_save_buf_rsrc0, s_save_mem_offset slc:1 glc:1 offset:128
	buffer_store_dword	v2, v0, s_save_buf_rsrc0, s_save_mem_offset slc:1 glc:1 offset:128*2
	buffer_store_dword	v3, v0, s_save_buf_rsrc0, s_save_mem_offset slc:1 glc:1 offset:128*3
	s_branch	L_SAVE_LDS

L_SAVE_4VGPR_WAVE64:
	s_mov_b32	s_save_buf_rsrc2, 0x1000000				//NUM_RECORDS in bytes

	// VGPR Allocated in 4-GPR granularity

	buffer_store_dword	v0, v0, s_save_buf_rsrc0, s_save_mem_offset slc:1 glc:1
	buffer_store_dword	v1, v0, s_save_buf_rsrc0, s_save_mem_offset slc:1 glc:1 offset:256
	buffer_store_dword	v2, v0, s_save_buf_rsrc0, s_save_mem_offset slc:1 glc:1 offset:256*2
	buffer_store_dword	v3, v0, s_save_buf_rsrc0, s_save_mem_offset slc:1 glc:1 offset:256*3

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
	s_getreg_b32	s_save_alloc_size, hwreg(HW_REG_LDS_ALLOC,SQ_WAVE_LDS_ALLOC_LDS_SIZE_SHIFT,SQ_WAVE_LDS_ALLOC_LDS_SIZE_SIZE)
	s_and_b32	s_save_alloc_size, s_save_alloc_size, 0xFFFFFFFF	//lds_size is zero?
	s_cbranch_scc0	L_SAVE_LDS_DONE						//no lds used? jump to L_SAVE_DONE

	s_barrier								//LDS is used? wait for other waves in the same TG
	s_and_b32	s_save_tmp, s_save_exec_hi, S_SAVE_SPI_INIT_FIRST_WAVE_MASK
	s_cbranch_scc0	L_SAVE_LDS_DONE

	// first wave do LDS save;

	s_lshl_b32	s_save_alloc_size, s_save_alloc_size, 6			//LDS size in dwords = lds_size * 64dw
	s_lshl_b32	s_save_alloc_size, s_save_alloc_size, 2			//LDS size in bytes
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
	s_waitcnt	0
	buffer_store_dword	v1, v0, s_save_buf_rsrc0, s_save_mem_offset slc:1 glc:1

	s_add_u32	m0, m0, s3						//every buffer_store_lds does 256 bytes
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
	s_waitcnt	0
	buffer_store_dword	v1, v0, s_save_buf_rsrc0, s_save_mem_offset slc:1 glc:1

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
	s_getreg_b32	s_save_alloc_size, hwreg(HW_REG_GPR_ALLOC,SQ_WAVE_GPR_ALLOC_VGPR_SIZE_SHIFT,SQ_WAVE_GPR_ALLOC_VGPR_SIZE_SIZE)
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

	buffer_store_dword	v0, v0, s_save_buf_rsrc0, s_save_mem_offset slc:1 glc:1
	buffer_store_dword	v1, v0, s_save_buf_rsrc0, s_save_mem_offset slc:1 glc:1 offset:128
	buffer_store_dword	v2, v0, s_save_buf_rsrc0, s_save_mem_offset slc:1 glc:1 offset:128*2
	buffer_store_dword	v3, v0, s_save_buf_rsrc0, s_save_mem_offset slc:1 glc:1 offset:128*3

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
	s_cbranch_scc0	L_SAVE_VGPR_END

L_SAVE_VGPR_W64_LOOP:
	v_movrels_b32	v0, v0							//v0 = v[0+m0]
	v_movrels_b32	v1, v1							//v1 = v[1+m0]
	v_movrels_b32	v2, v2							//v2 = v[2+m0]
	v_movrels_b32	v3, v3							//v3 = v[3+m0]

	buffer_store_dword	v0, v0, s_save_buf_rsrc0, s_save_mem_offset slc:1 glc:1
	buffer_store_dword	v1, v0, s_save_buf_rsrc0, s_save_mem_offset slc:1 glc:1 offset:256
	buffer_store_dword	v2, v0, s_save_buf_rsrc0, s_save_mem_offset slc:1 glc:1 offset:256*2
	buffer_store_dword	v3, v0, s_save_buf_rsrc0, s_save_mem_offset slc:1 glc:1 offset:256*3

	s_add_u32	m0, m0, 4						//next vgpr index
	s_add_u32	s_save_mem_offset, s_save_mem_offset, 256*4		//every buffer_store_dword does 256 bytes
	s_cmp_lt_u32	m0, s_save_alloc_size					//scc = (m0 < s_save_alloc_size) ? 1 : 0
	s_cbranch_scc1	L_SAVE_VGPR_W64_LOOP					//VGPR save is complete?

	//Below part will be the save shared vgpr part (new for gfx10)
	s_getreg_b32	s_save_alloc_size, hwreg(HW_REG_LDS_ALLOC,SQ_WAVE_LDS_ALLOC_VGPR_SHARED_SIZE_SHIFT,SQ_WAVE_LDS_ALLOC_VGPR_SHARED_SIZE_SIZE)
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
	buffer_store_dword	v0, v0, s_save_buf_rsrc0, s_save_mem_offset slc:1 glc:1
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
	s_and_b32	s_restore_tmp, s_restore_spi_init_hi, S_RESTORE_SPI_INIT_ATC_MASK
	s_lshr_b32	s_restore_tmp, s_restore_tmp, (S_RESTORE_SPI_INIT_ATC_SHIFT-SQ_BUF_RSRC_WORD1_ATC_SHIFT)
	s_or_b32	s_restore_buf_rsrc3, s_restore_buf_rsrc3, s_restore_tmp	//or ATC
	s_and_b32	s_restore_tmp, s_restore_spi_init_hi, S_RESTORE_SPI_INIT_MTYPE_MASK
	s_lshr_b32	s_restore_tmp, s_restore_tmp, (S_RESTORE_SPI_INIT_MTYPE_SHIFT-SQ_BUF_RSRC_WORD3_MTYPE_SHIFT)
	s_or_b32	s_restore_buf_rsrc3, s_restore_buf_rsrc3, s_restore_tmp	//or MTYPE
	//determine it is wave32 or wave64
	s_getreg_b32	s_restore_size, hwreg(HW_REG_IB_STS2,SQ_WAVE_IB_STS2_WAVE64_SHIFT,SQ_WAVE_IB_STS2_WAVE64_SIZE)
	s_lshl_b32	s_restore_size, s_restore_size, S_WAVE_SIZE
	s_or_b32	s_restore_size, s_restore_spi_init_hi, s_restore_size

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
	s_getreg_b32	s_restore_alloc_size, hwreg(HW_REG_LDS_ALLOC,SQ_WAVE_LDS_ALLOC_LDS_SIZE_SHIFT,SQ_WAVE_LDS_ALLOC_LDS_SIZE_SIZE)
	s_and_b32	s_restore_alloc_size, s_restore_alloc_size, 0xFFFFFFFF	//lds_size is zero?
	s_cbranch_scc0	L_RESTORE_VGPR						//no lds used? jump to L_RESTORE_VGPR
	s_lshl_b32	s_restore_alloc_size, s_restore_alloc_size, 6		//LDS size in dwords = lds_size * 64dw
	s_lshl_b32	s_restore_alloc_size, s_restore_alloc_size, 2		//LDS size in bytes
	s_mov_b32	s_restore_buf_rsrc2, s_restore_alloc_size		//NUM_RECORDS in bytes

	// LDS at offset: size(VGPR)+size(SVGPR)+SIZE(SGPR)+SIZE(HWREG)
	//
	get_vgpr_size_bytes(s_restore_mem_offset, s_restore_size)
	get_svgpr_size_bytes(s_restore_tmp)
	s_add_u32	s_restore_mem_offset, s_restore_mem_offset, s_restore_tmp
	s_add_u32	s_restore_mem_offset, s_restore_mem_offset, get_sgpr_size_bytes()
	s_add_u32	s_restore_mem_offset, s_restore_mem_offset, get_hwreg_size_bytes()

	s_mov_b32	s_restore_buf_rsrc2, 0x1000000				//NUM_RECORDS in bytes

	s_lshr_b32	m0, s_wave_size, S_WAVE_SIZE
	s_and_b32	m0, m0, 1
	s_cmp_eq_u32	m0, 1
	s_mov_b32	m0, 0x0
	s_cbranch_scc1	L_RESTORE_LDS_LOOP_W64

L_RESTORE_LDS_LOOP_W32:
	buffer_load_dword	v0, v0, s_restore_buf_rsrc0, s_restore_mem_offset lds:1	// first 64DW
	s_add_u32	m0, m0, 128						// 128 DW
	s_add_u32	s_restore_mem_offset, s_restore_mem_offset, 128		//mem offset increased by 128DW
	s_cmp_lt_u32	m0, s_restore_alloc_size				//scc=(m0 < s_restore_alloc_size) ? 1 : 0
	s_cbranch_scc1	L_RESTORE_LDS_LOOP_W32					//LDS restore is complete?
	s_branch	L_RESTORE_VGPR

L_RESTORE_LDS_LOOP_W64:
	buffer_load_dword	v0, v0, s_restore_buf_rsrc0, s_restore_mem_offset lds:1	// first 64DW
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
	s_getreg_b32	s_restore_alloc_size, hwreg(HW_REG_GPR_ALLOC,SQ_WAVE_GPR_ALLOC_VGPR_SIZE_SHIFT,SQ_WAVE_GPR_ALLOC_VGPR_SIZE_SIZE)
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

L_RESTORE_VGPR_WAVE32_LOOP:
	buffer_load_dword	v0, v0, s_restore_buf_rsrc0, s_restore_mem_offset slc:1 glc:1
	buffer_load_dword	v1, v0, s_restore_buf_rsrc0, s_restore_mem_offset slc:1 glc:1 offset:128
	buffer_load_dword	v2, v0, s_restore_buf_rsrc0, s_restore_mem_offset slc:1 glc:1 offset:128*2
	buffer_load_dword	v3, v0, s_restore_buf_rsrc0, s_restore_mem_offset slc:1 glc:1 offset:128*3
	s_waitcnt	vmcnt(0)
	v_movreld_b32	v0, v0							//v[0+m0] = v0
	v_movreld_b32	v1, v1
	v_movreld_b32	v2, v2
	v_movreld_b32	v3, v3
	s_add_u32	m0, m0, 4						//next vgpr index
	s_add_u32	s_restore_mem_offset, s_restore_mem_offset, 128*4	//every buffer_load_dword does 128 bytes
	s_cmp_lt_u32	m0, s_restore_alloc_size				//scc = (m0 < s_restore_alloc_size) ? 1 : 0
	s_cbranch_scc1	L_RESTORE_VGPR_WAVE32_LOOP				//VGPR restore (except v0) is complete?

	/* VGPR restore on v0 */
	buffer_load_dword	v0, v0, s_restore_buf_rsrc0, s_restore_mem_offset_save slc:1 glc:1
	buffer_load_dword	v1, v0, s_restore_buf_rsrc0, s_restore_mem_offset_save slc:1 glc:1 offset:128
	buffer_load_dword	v2, v0, s_restore_buf_rsrc0, s_restore_mem_offset_save slc:1 glc:1 offset:128*2
	buffer_load_dword	v3, v0, s_restore_buf_rsrc0, s_restore_mem_offset_save slc:1 glc:1 offset:128*3

	s_branch	L_RESTORE_SGPR

L_RESTORE_VGPR_WAVE64:
	s_mov_b32	s_restore_buf_rsrc2, 0x1000000				//NUM_RECORDS in bytes

	// VGPR load using dw burst
	s_mov_b32	s_restore_mem_offset_save, s_restore_mem_offset		// restore start with v4, v0 will be the last
	s_add_u32	s_restore_mem_offset, s_restore_mem_offset, 256*4
	s_mov_b32	m0, 4							//VGPR initial index value = 4

L_RESTORE_VGPR_WAVE64_LOOP:
	buffer_load_dword	v0, v0, s_restore_buf_rsrc0, s_restore_mem_offset slc:1 glc:1
	buffer_load_dword	v1, v0, s_restore_buf_rsrc0, s_restore_mem_offset slc:1 glc:1 offset:256
	buffer_load_dword	v2, v0, s_restore_buf_rsrc0, s_restore_mem_offset slc:1 glc:1 offset:256*2
	buffer_load_dword	v3, v0, s_restore_buf_rsrc0, s_restore_mem_offset slc:1 glc:1 offset:256*3
	s_waitcnt	vmcnt(0)
	v_movreld_b32	v0, v0							//v[0+m0] = v0
	v_movreld_b32	v1, v1
	v_movreld_b32	v2, v2
	v_movreld_b32	v3, v3
	s_add_u32	m0, m0, 4						//next vgpr index
	s_add_u32	s_restore_mem_offset, s_restore_mem_offset, 256*4	//every buffer_load_dword does 256 bytes
	s_cmp_lt_u32	m0, s_restore_alloc_size				//scc = (m0 < s_restore_alloc_size) ? 1 : 0
	s_cbranch_scc1	L_RESTORE_VGPR_WAVE64_LOOP				//VGPR restore (except v0) is complete?

	//Below part will be the restore shared vgpr part (new for gfx10)
	s_getreg_b32	s_restore_alloc_size, hwreg(HW_REG_LDS_ALLOC,SQ_WAVE_LDS_ALLOC_VGPR_SHARED_SIZE_SHIFT,SQ_WAVE_LDS_ALLOC_VGPR_SHARED_SIZE_SIZE)	//shared_vgpr_size
	s_and_b32	s_restore_alloc_size, s_restore_alloc_size, 0xFFFFFFFF	//shared_vgpr_size is zero?
	s_cbranch_scc0	L_RESTORE_V0						//no shared_vgpr used?
	s_lshl_b32	s_restore_alloc_size, s_restore_alloc_size, 3		//Number of SHARED_VGPRs = shared_vgpr_size * 8    (non-zero value)
	//m0 now has the value of normal vgpr count, just add the m0 with shared_vgpr count to get the total count.
	//restore shared_vgpr will start from the index of m0
	s_add_u32	s_restore_alloc_size, s_restore_alloc_size, m0
	s_mov_b32	exec_lo, 0xFFFFFFFF
	s_mov_b32	exec_hi, 0x00000000
L_RESTORE_SHARED_VGPR_WAVE64_LOOP:
	buffer_load_dword	v0, v0, s_restore_buf_rsrc0, s_restore_mem_offset slc:1 glc:1
	s_waitcnt	vmcnt(0)
	v_movreld_b32	v0, v0							//v[0+m0] = v0
	s_add_u32	m0, m0, 1						//next vgpr index
	s_add_u32	s_restore_mem_offset, s_restore_mem_offset, 128
	s_cmp_lt_u32	m0, s_restore_alloc_size				//scc = (m0 < s_restore_alloc_size) ? 1 : 0
	s_cbranch_scc1	L_RESTORE_SHARED_VGPR_WAVE64_LOOP			//VGPR restore (except v0) is complete?

	s_mov_b32	exec_hi, 0xFFFFFFFF					//restore back exec_hi before restoring V0!!

	/* VGPR restore on v0 */
L_RESTORE_V0:
	buffer_load_dword	v0, v0, s_restore_buf_rsrc0, s_restore_mem_offset_save slc:1 glc:1
	buffer_load_dword	v1, v0, s_restore_buf_rsrc0, s_restore_mem_offset_save slc:1 glc:1 offset:256
	buffer_load_dword	v2, v0, s_restore_buf_rsrc0, s_restore_mem_offset_save slc:1 glc:1 offset:256*2
	buffer_load_dword	v3, v0, s_restore_buf_rsrc0, s_restore_mem_offset_save slc:1 glc:1 offset:256*3
	s_waitcnt	vmcnt(0)

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
	s_waitcnt	lgkmcnt(0)

	s_sub_u32	m0, m0, 4						// Restore from S[0] to S[104]
	s_nop		0							// hazard SALU M0=> S_MOVREL

	s_movreld_b64	s0, s0							//s[0+m0] = s0
	s_movreld_b64	s2, s2

	read_8sgpr_from_mem(s0, s_restore_buf_rsrc0, s_restore_mem_offset)
	s_waitcnt	lgkmcnt(0)

	s_sub_u32	m0, m0, 8						// Restore from S[0] to S[96]
	s_nop		0							// hazard SALU M0=> S_MOVREL

	s_movreld_b64	s0, s0							//s[0+m0] = s0
	s_movreld_b64	s2, s2
	s_movreld_b64	s4, s4
	s_movreld_b64	s6, s6

 L_RESTORE_SGPR_LOOP:
	read_16sgpr_from_mem(s0, s_restore_buf_rsrc0, s_restore_mem_offset)
	s_waitcnt	lgkmcnt(0)

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

	/* restore HW registers */
L_RESTORE_HWREG:
	// HWREG SR memory offset : size(VGPR)+size(SVGPR)+size(SGPR)
	get_vgpr_size_bytes(s_restore_mem_offset, s_restore_size)
	get_svgpr_size_bytes(s_restore_tmp)
	s_add_u32	s_restore_mem_offset, s_restore_mem_offset, s_restore_tmp
	s_add_u32	s_restore_mem_offset, s_restore_mem_offset, get_sgpr_size_bytes()

	s_mov_b32	s_restore_buf_rsrc2, 0x1000000				//NUM_RECORDS in bytes

	read_hwreg_from_mem(s_restore_m0, s_restore_buf_rsrc0, s_restore_mem_offset)
	read_hwreg_from_mem(s_restore_pc_lo, s_restore_buf_rsrc0, s_restore_mem_offset)
	read_hwreg_from_mem(s_restore_pc_hi, s_restore_buf_rsrc0, s_restore_mem_offset)
	read_hwreg_from_mem(s_restore_exec_lo, s_restore_buf_rsrc0, s_restore_mem_offset)
	read_hwreg_from_mem(s_restore_exec_hi, s_restore_buf_rsrc0, s_restore_mem_offset)
	read_hwreg_from_mem(s_restore_status, s_restore_buf_rsrc0, s_restore_mem_offset)
	read_hwreg_from_mem(s_restore_trapsts, s_restore_buf_rsrc0, s_restore_mem_offset)
	read_hwreg_from_mem(s_restore_xnack_mask, s_restore_buf_rsrc0, s_restore_mem_offset)
	read_hwreg_from_mem(s_restore_mode, s_restore_buf_rsrc0, s_restore_mem_offset)
	read_hwreg_from_mem(s_restore_flat_scratch, s_restore_buf_rsrc0, s_restore_mem_offset)
	s_waitcnt	lgkmcnt(0)

	s_setreg_b32	hwreg(HW_REG_SHADER_FLAT_SCRATCH_LO), s_restore_flat_scratch

	read_hwreg_from_mem(s_restore_flat_scratch, s_restore_buf_rsrc0, s_restore_mem_offset)
	s_waitcnt	lgkmcnt(0)						//from now on, it is safe to restore STATUS and IB_STS

	s_setreg_b32	hwreg(HW_REG_SHADER_FLAT_SCRATCH_HI), s_restore_flat_scratch

	s_mov_b32	s_restore_tmp, s_restore_pc_hi
	s_and_b32	s_restore_pc_hi, s_restore_tmp, 0x0000ffff		//pc[47:32] //Do it here in order not to affect STATUS

	s_mov_b32	m0, s_restore_m0
	s_mov_b32	exec_lo, s_restore_exec_lo
	s_mov_b32	exec_hi, s_restore_exec_hi

	s_and_b32	s_restore_m0, SQ_WAVE_TRAPSTS_PRE_SAVECTX_MASK, s_restore_trapsts
	s_setreg_b32	hwreg(HW_REG_TRAPSTS, SQ_WAVE_TRAPSTS_PRE_SAVECTX_SHIFT, SQ_WAVE_TRAPSTS_PRE_SAVECTX_SIZE), s_restore_m0
	s_setreg_b32	hwreg(HW_REG_SHADER_XNACK_MASK), s_restore_xnack_mask
	s_and_b32	s_restore_m0, SQ_WAVE_TRAPSTS_POST_SAVECTX_MASK, s_restore_trapsts
	s_lshr_b32	s_restore_m0, s_restore_m0, SQ_WAVE_TRAPSTS_POST_SAVECTX_SHIFT
	s_setreg_b32	hwreg(HW_REG_TRAPSTS, SQ_WAVE_TRAPSTS_POST_SAVECTX_SHIFT, SQ_WAVE_TRAPSTS_POST_SAVECTX_SIZE), s_restore_m0
	s_setreg_b32	hwreg(HW_REG_MODE), s_restore_mode
	s_and_b32	s_restore_m0, s_restore_tmp, S_SAVE_PC_HI_RCNT_MASK
	s_lshr_b32	s_restore_m0, s_restore_m0, S_SAVE_PC_HI_RCNT_SHIFT
	s_lshl_b32	s_restore_m0, s_restore_m0, SQ_WAVE_IB_STS_RCNT_SHIFT
	s_mov_b32	s_restore_mode, 0x0
	s_or_b32	s_restore_mode, s_restore_mode, s_restore_m0
	s_and_b32	s_restore_m0, s_restore_tmp, S_SAVE_PC_HI_FIRST_REPLAY_MASK
	s_lshr_b32	s_restore_m0, s_restore_m0, S_SAVE_PC_HI_FIRST_REPLAY_SHIFT
	s_lshl_b32	s_restore_m0, s_restore_m0, SQ_WAVE_IB_STS_FIRST_REPLAY_SHIFT
	s_or_b32	s_restore_mode, s_restore_mode, s_restore_m0
	s_and_b32	s_restore_m0, s_restore_tmp, S_SAVE_PC_HI_REPLAY_W64H_MASK
	s_lshr_b32	s_restore_m0, s_restore_m0, S_SAVE_PC_HI_REPLAY_W64H_SHIFT
	s_lshl_b32	s_restore_m0, s_restore_m0, SQ_WAVE_IB_STS_REPLAY_W64H_SHIFT
	s_or_b32	s_restore_mode, s_restore_mode, s_restore_m0

	s_and_b32	s_restore_m0, s_restore_status, SQ_WAVE_STATUS_INST_ATC_MASK
	s_lshr_b32	s_restore_m0, s_restore_m0, SQ_WAVE_STATUS_INST_ATC_SHIFT
	s_setreg_b32 	hwreg(HW_REG_IB_STS), s_restore_mode

	s_and_b64	exec, exec, exec					// Restore STATUS.EXECZ, not writable by s_setreg_b32
	s_and_b64	vcc, vcc, vcc						// Restore STATUS.VCCZ, not writable by s_setreg_b32
	s_setreg_b32	hwreg(HW_REG_STATUS), s_restore_status			// SCC is included, which is changed by previous salu

	s_barrier								//barrier to ensure the readiness of LDS before access attemps from any other wave in the same TG

	s_rfe_b64	s_restore_pc_lo						//Return to the main shader program and resume execution

L_END_PGM:
	s_endpgm
end

function write_hwreg_to_mem(s, s_rsrc, s_mem_offset)
	s_mov_b32	exec_lo, m0
	s_mov_b32	m0, s_mem_offset
	s_buffer_store_dword	s, s_rsrc, m0 glc:1
	s_add_u32	s_mem_offset, s_mem_offset, 4
	s_mov_b32	m0, exec_lo
end


function write_16sgpr_to_mem(s, s_rsrc, s_mem_offset)
	s_buffer_store_dwordx4	s[0], s_rsrc, 0 glc:1
	s_buffer_store_dwordx4	s[4], s_rsrc, 16 glc:1
	s_buffer_store_dwordx4	s[8], s_rsrc, 32 glc:1
	s_buffer_store_dwordx4	s[12], s_rsrc, 48 glc:1
	s_add_u32	s_rsrc[0], s_rsrc[0], 4*16
	s_addc_u32	s_rsrc[1], s_rsrc[1], 0x0
end

function write_12sgpr_to_mem(s, s_rsrc, s_mem_offset)
	s_buffer_store_dwordx4	s[0], s_rsrc, 0 glc:1
	s_buffer_store_dwordx4	s[4], s_rsrc, 16 glc:1
	s_buffer_store_dwordx4	s[8], s_rsrc, 32 glc:1
	s_add_u32	s_rsrc[0], s_rsrc[0], 4*12
	s_addc_u32	s_rsrc[1], s_rsrc[1], 0x0
end


function read_hwreg_from_mem(s, s_rsrc, s_mem_offset)
	s_buffer_load_dword	s, s_rsrc, s_mem_offset glc:1
	s_add_u32	s_mem_offset, s_mem_offset, 4
end

function read_16sgpr_from_mem(s, s_rsrc, s_mem_offset)
	s_sub_u32	s_mem_offset, s_mem_offset, 4*16
	s_buffer_load_dwordx16	s, s_rsrc, s_mem_offset glc:1
end

function read_8sgpr_from_mem(s, s_rsrc, s_mem_offset)
	s_sub_u32	s_mem_offset, s_mem_offset, 4*8
	s_buffer_load_dwordx8	s, s_rsrc, s_mem_offset glc:1
end

function read_4sgpr_from_mem(s, s_rsrc, s_mem_offset)
	s_sub_u32	s_mem_offset, s_mem_offset, 4*4
	s_buffer_load_dwordx4	s, s_rsrc, s_mem_offset glc:1
end


function get_lds_size_bytes(s_lds_size_byte)
	s_getreg_b32	s_lds_size_byte, hwreg(HW_REG_LDS_ALLOC, SQ_WAVE_LDS_ALLOC_LDS_SIZE_SHIFT, SQ_WAVE_LDS_ALLOC_LDS_SIZE_SIZE)
	s_lshl_b32	s_lds_size_byte, s_lds_size_byte, 8			//LDS size in dwords = lds_size * 64 *4Bytes // granularity 64DW
end

function get_vgpr_size_bytes(s_vgpr_size_byte, s_size)
	s_getreg_b32	s_vgpr_size_byte, hwreg(HW_REG_GPR_ALLOC,SQ_WAVE_GPR_ALLOC_VGPR_SIZE_SHIFT,SQ_WAVE_GPR_ALLOC_VGPR_SIZE_SIZE)
	s_add_u32	s_vgpr_size_byte, s_vgpr_size_byte, 1
	s_lshr_b32	m0, s_size, S_WAVE_SIZE
	s_and_b32	m0, m0, 1
	s_cmp_eq_u32	m0, 1
	s_cbranch_scc1	L_ENABLE_SHIFT_W64
	s_lshl_b32	s_vgpr_size_byte, s_vgpr_size_byte, (2+7)		//Number of VGPRs = (vgpr_size + 1) * 4 * 32 * 4   (non-zero value)
	s_branch	L_SHIFT_DONE
L_ENABLE_SHIFT_W64:
	s_lshl_b32	s_vgpr_size_byte, s_vgpr_size_byte, (2+8)		//Number of VGPRs = (vgpr_size + 1) * 4 * 64 * 4   (non-zero value)
L_SHIFT_DONE:
end

function get_svgpr_size_bytes(s_svgpr_size_byte)
	s_getreg_b32	s_svgpr_size_byte, hwreg(HW_REG_LDS_ALLOC,SQ_WAVE_LDS_ALLOC_VGPR_SHARED_SIZE_SHIFT,SQ_WAVE_LDS_ALLOC_VGPR_SHARED_SIZE_SIZE)
	s_lshl_b32	s_svgpr_size_byte, s_svgpr_size_byte, (3+7)
end

function get_sgpr_size_bytes
	return 512
end

function get_hwreg_size_bytes
	return 128
end
