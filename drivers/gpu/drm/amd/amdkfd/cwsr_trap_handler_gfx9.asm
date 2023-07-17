/*
 * Copyright 2016 Advanced Micro Devices, Inc.
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
 * gfx9:
 *   cpp -DASIC_FAMILY=CHIP_VEGAM cwsr_trap_handler_gfx9.asm -P -o gfx9.sp3
 *   sp3 gfx9.sp3 -hex gfx9.hex
 *
 * arcturus:
 *   cpp -DASIC_FAMILY=CHIP_ARCTURUS cwsr_trap_handler_gfx9.asm -P -o arcturus.sp3
 *   sp3 arcturus.sp3 -hex arcturus.hex
 *
 * aldebaran:
 *   cpp -DASIC_FAMILY=CHIP_ALDEBARAN cwsr_trap_handler_gfx9.asm -P -o aldebaran.sp3
 *   sp3 aldebaran.sp3 -hex aldebaran.hex
 *
 * gc_9_4_3:
 *   cpp -DASIC_FAMILY=GC_9_4_3 cwsr_trap_handler_gfx9.asm -P -o gc_9_4_3.sp3
 *   sp3 gc_9_4_3.sp3 -hex gc_9_4_3.hex
 */

#define CHIP_VEGAM 18
#define CHIP_ARCTURUS 23
#define CHIP_ALDEBARAN 25
#define CHIP_GC_9_4_3 26

var ACK_SQC_STORE		    =	1		    //workaround for suspected SQC store bug causing incorrect stores under concurrency
var SAVE_AFTER_XNACK_ERROR	    =	1		    //workaround for TCP store failure after XNACK error when ALLOW_REPLAY=0, for debugger
var SINGLE_STEP_MISSED_WORKAROUND   =	(ASIC_FAMILY <= CHIP_ALDEBARAN)	//workaround for lost MODE.DEBUG_EN exception when SAVECTX raised

/**************************************************************************/
/*			variables					  */
/**************************************************************************/
var SQ_WAVE_STATUS_SPI_PRIO_SHIFT  = 1
var SQ_WAVE_STATUS_SPI_PRIO_MASK   = 0x00000006
var SQ_WAVE_STATUS_HALT_MASK       = 0x2000
var SQ_WAVE_STATUS_PRE_SPI_PRIO_SHIFT   = 0
var SQ_WAVE_STATUS_PRE_SPI_PRIO_SIZE    = 1
var SQ_WAVE_STATUS_POST_SPI_PRIO_SHIFT  = 3
var SQ_WAVE_STATUS_POST_SPI_PRIO_SIZE   = 29
var SQ_WAVE_STATUS_ALLOW_REPLAY_MASK    = 0x400000
var SQ_WAVE_STATUS_ECC_ERR_MASK         = 0x20000

var SQ_WAVE_LDS_ALLOC_LDS_SIZE_SHIFT	= 12
var SQ_WAVE_LDS_ALLOC_LDS_SIZE_SIZE	= 9
var SQ_WAVE_GPR_ALLOC_VGPR_SIZE_SIZE	= 6
var SQ_WAVE_GPR_ALLOC_SGPR_SIZE_SIZE	= 3			//FIXME	 sq.blk still has 4 bits at this time while SQ programming guide has 3 bits
var SQ_WAVE_GPR_ALLOC_SGPR_SIZE_SHIFT	= 24

#if ASIC_FAMILY >= CHIP_ALDEBARAN
var SQ_WAVE_GPR_ALLOC_VGPR_SIZE_SHIFT	= 6
var SQ_WAVE_GPR_ALLOC_ACCV_OFFSET_SHIFT	= 12
var SQ_WAVE_GPR_ALLOC_ACCV_OFFSET_SIZE	= 6
#else
var SQ_WAVE_GPR_ALLOC_VGPR_SIZE_SHIFT	= 8
#endif

var SQ_WAVE_TRAPSTS_SAVECTX_MASK    =	0x400
var SQ_WAVE_TRAPSTS_EXCP_MASK	    =	0x1FF
var SQ_WAVE_TRAPSTS_SAVECTX_SHIFT   =	10
var SQ_WAVE_TRAPSTS_ADDR_WATCH_MASK =	0x80
var SQ_WAVE_TRAPSTS_ADDR_WATCH_SHIFT =	7
var SQ_WAVE_TRAPSTS_MEM_VIOL_MASK   =	0x100
var SQ_WAVE_TRAPSTS_MEM_VIOL_SHIFT  =	8
var SQ_WAVE_TRAPSTS_HOST_TRAP_MASK  =	0x400000
var SQ_WAVE_TRAPSTS_WAVE_BEGIN_MASK =	0x800000
var SQ_WAVE_TRAPSTS_WAVE_END_MASK   =	0x1000000
var SQ_WAVE_TRAPSTS_TRAP_AFTER_INST_MASK =  0x2000000
var SQ_WAVE_TRAPSTS_PRE_SAVECTX_MASK	=   0x3FF
var SQ_WAVE_TRAPSTS_PRE_SAVECTX_SHIFT	=   0x0
var SQ_WAVE_TRAPSTS_PRE_SAVECTX_SIZE	=   10
var SQ_WAVE_TRAPSTS_POST_SAVECTX_MASK	=   0xFFFFF800
var SQ_WAVE_TRAPSTS_POST_SAVECTX_SHIFT	=   11
var SQ_WAVE_TRAPSTS_POST_SAVECTX_SIZE	=   21
var SQ_WAVE_TRAPSTS_ILLEGAL_INST_MASK	=   0x800
var SQ_WAVE_TRAPSTS_EXCP_HI_MASK	=   0x7000
var SQ_WAVE_TRAPSTS_XNACK_ERROR_MASK	=   0x10000000

var SQ_WAVE_MODE_EXCP_EN_SHIFT		=   12
var SQ_WAVE_MODE_EXCP_EN_ADDR_WATCH_SHIFT	= 19

var SQ_WAVE_IB_STS_FIRST_REPLAY_SHIFT	=   15			//FIXME
var SQ_WAVE_IB_STS_RCNT_FIRST_REPLAY_MASK	= 0x1F8000

var SQ_WAVE_MODE_DEBUG_EN_MASK		=   0x800

var TTMP_SAVE_RCNT_FIRST_REPLAY_SHIFT	=   26			// bits [31:26] unused by SPI debug data
var TTMP_SAVE_RCNT_FIRST_REPLAY_MASK	=   0xFC000000
var TTMP_DEBUG_TRAP_ENABLED_SHIFT	=   23
var TTMP_DEBUG_TRAP_ENABLED_MASK	=   0x800000

/*	Save	    */
var S_SAVE_BUF_RSRC_WORD1_STRIDE	=   0x00040000		//stride is 4 bytes
var S_SAVE_BUF_RSRC_WORD3_MISC		=   0x00807FAC		//SQ_SEL_X/Y/Z/W, BUF_NUM_FORMAT_FLOAT, (0 for MUBUF stride[17:14] when ADD_TID_ENABLE and BUF_DATA_FORMAT_32 for MTBUF), ADD_TID_ENABLE
var S_SAVE_PC_HI_TRAP_ID_MASK		=   0x00FF0000
var S_SAVE_PC_HI_HT_MASK		=   0x01000000
var S_SAVE_SPI_INIT_FIRST_WAVE_MASK	=   0x04000000		//bit[26]: FirstWaveInTG
var S_SAVE_SPI_INIT_FIRST_WAVE_SHIFT	=   26

var s_save_spi_init_lo		    =	exec_lo
var s_save_spi_init_hi		    =	exec_hi

var s_save_pc_lo	    =	ttmp0		//{TTMP1, TTMP0} = {3'h0,pc_rewind[3:0], HT[0],trapID[7:0], PC[47:0]}
var s_save_pc_hi	    =	ttmp1
var s_save_exec_lo	    =	ttmp2
var s_save_exec_hi	    =	ttmp3
var s_save_tmp		    =	ttmp14
var s_save_trapsts	    =	ttmp15		//not really used until the end of the SAVE routine
var s_save_xnack_mask_lo    =	ttmp6
var s_save_xnack_mask_hi    =	ttmp7
var s_save_buf_rsrc0	    =	ttmp8
var s_save_buf_rsrc1	    =	ttmp9
var s_save_buf_rsrc2	    =	ttmp10
var s_save_buf_rsrc3	    =	ttmp11
var s_save_status	    =	ttmp12
var s_save_mem_offset	    =	ttmp4
var s_save_alloc_size	    =	s_save_trapsts		//conflict
var s_save_m0		    =	ttmp5
var s_save_ttmps_lo	    =	s_save_tmp		//no conflict
var s_save_ttmps_hi	    =	s_save_trapsts		//no conflict
#if ASIC_FAMILY >= CHIP_GC_9_4_3
var s_save_ib_sts       =	ttmp13
#else
var s_save_ib_sts       =	ttmp11
#endif

/*	Restore	    */
var S_RESTORE_BUF_RSRC_WORD1_STRIDE	    =	S_SAVE_BUF_RSRC_WORD1_STRIDE
var S_RESTORE_BUF_RSRC_WORD3_MISC	    =	S_SAVE_BUF_RSRC_WORD3_MISC

var S_RESTORE_SPI_INIT_FIRST_WAVE_MASK	    =	0x04000000	    //bit[26]: FirstWaveInTG
var S_RESTORE_SPI_INIT_FIRST_WAVE_SHIFT	    =	26

var s_restore_spi_init_lo		    =	exec_lo
var s_restore_spi_init_hi		    =	exec_hi

var s_restore_mem_offset	=   ttmp12
var s_restore_tmp2		=   ttmp13
var s_restore_alloc_size	=   ttmp3
var s_restore_tmp		=   ttmp2
var s_restore_mem_offset_save	=   s_restore_tmp	//no conflict
var s_restore_accvgpr_offset_save = ttmp7

var s_restore_m0	    =	s_restore_alloc_size	//no conflict

var s_restore_mode	    =	s_restore_accvgpr_offset_save

var s_restore_pc_lo	    =	ttmp0
var s_restore_pc_hi	    =	ttmp1
var s_restore_exec_lo	    =	ttmp4
var s_restore_exec_hi	    = 	ttmp5
var s_restore_status	    =	ttmp14
var s_restore_trapsts	    =	ttmp15
var s_restore_xnack_mask_lo =	xnack_mask_lo
var s_restore_xnack_mask_hi =	xnack_mask_hi
var s_restore_buf_rsrc0	    =	ttmp8
var s_restore_buf_rsrc1	    =	ttmp9
var s_restore_buf_rsrc2	    =	ttmp10
var s_restore_buf_rsrc3	    =	ttmp11
var s_restore_ttmps_lo	    =	s_restore_tmp		//no conflict
var s_restore_ttmps_hi	    =	s_restore_alloc_size	//no conflict

/**************************************************************************/
/*			trap handler entry points			  */
/**************************************************************************/
/* Shader Main*/

shader main
  asic(DEFAULT)
  type(CS)


	s_branch L_SKIP_RESTORE					    //NOT restore. might be a regular trap or save

L_JUMP_TO_RESTORE:
    s_branch L_RESTORE						    //restore

L_SKIP_RESTORE:

    s_getreg_b32    s_save_status, hwreg(HW_REG_STATUS)				    //save STATUS since we will change SCC

    // Clear SPI_PRIO: do not save with elevated priority.
    // Clear ECC_ERR: prevents SQC store and triggers FATAL_HALT if setreg'd.
    s_andn2_b32     s_save_status, s_save_status, SQ_WAVE_STATUS_SPI_PRIO_MASK|SQ_WAVE_STATUS_ECC_ERR_MASK

    s_getreg_b32    s_save_trapsts, hwreg(HW_REG_TRAPSTS)

    s_and_b32       ttmp2, s_save_status, SQ_WAVE_STATUS_HALT_MASK
    s_cbranch_scc0  L_NOT_HALTED

L_HALTED:
    // Host trap may occur while wave is halted.
    s_and_b32       ttmp2, s_save_pc_hi, S_SAVE_PC_HI_TRAP_ID_MASK
    s_cbranch_scc1  L_FETCH_2ND_TRAP

L_CHECK_SAVE:
    s_and_b32       ttmp2, s_save_trapsts, SQ_WAVE_TRAPSTS_SAVECTX_MASK    //check whether this is for save
    s_cbranch_scc1  L_SAVE					//this is the operation for save

    // Wave is halted but neither host trap nor SAVECTX is raised.
    // Caused by instruction fetch memory violation.
    // Spin wait until context saved to prevent interrupt storm.
    s_sleep         0x10
    s_getreg_b32    s_save_trapsts, hwreg(HW_REG_TRAPSTS)
    s_branch        L_CHECK_SAVE

L_NOT_HALTED:
    // Let second-level handle non-SAVECTX exception or trap.
    // Any concurrent SAVECTX will be handled upon re-entry once halted.

    // Check non-maskable exceptions. memory_violation, illegal_instruction
    // and debugger (host trap, wave start/end, trap after instruction)
    // exceptions always cause the wave to enter the trap handler.
    s_and_b32       ttmp2, s_save_trapsts,      \
        SQ_WAVE_TRAPSTS_MEM_VIOL_MASK         | \
        SQ_WAVE_TRAPSTS_ILLEGAL_INST_MASK     | \
        SQ_WAVE_TRAPSTS_HOST_TRAP_MASK        | \
        SQ_WAVE_TRAPSTS_WAVE_BEGIN_MASK       | \
        SQ_WAVE_TRAPSTS_WAVE_END_MASK         | \
        SQ_WAVE_TRAPSTS_TRAP_AFTER_INST_MASK
    s_cbranch_scc1  L_FETCH_2ND_TRAP

    // Check for maskable exceptions in trapsts.excp and trapsts.excp_hi.
    // Maskable exceptions only cause the wave to enter the trap handler if
    // their respective bit in mode.excp_en is set.
    s_and_b32       ttmp2, s_save_trapsts, SQ_WAVE_TRAPSTS_EXCP_MASK|SQ_WAVE_TRAPSTS_EXCP_HI_MASK
    s_cbranch_scc0  L_CHECK_TRAP_ID

    s_and_b32       ttmp3, s_save_trapsts, SQ_WAVE_TRAPSTS_ADDR_WATCH_MASK|SQ_WAVE_TRAPSTS_EXCP_HI_MASK
    s_cbranch_scc0  L_NOT_ADDR_WATCH
    s_bitset1_b32   ttmp2, SQ_WAVE_TRAPSTS_ADDR_WATCH_SHIFT // Check all addr_watch[123] exceptions against excp_en.addr_watch

L_NOT_ADDR_WATCH:
    s_getreg_b32    ttmp3, hwreg(HW_REG_MODE)
    s_lshl_b32      ttmp2, ttmp2, SQ_WAVE_MODE_EXCP_EN_SHIFT
    s_and_b32       ttmp2, ttmp2, ttmp3
    s_cbranch_scc1  L_FETCH_2ND_TRAP

L_CHECK_TRAP_ID:
    // Check trap_id != 0
    s_and_b32       ttmp2, s_save_pc_hi, S_SAVE_PC_HI_TRAP_ID_MASK
    s_cbranch_scc1  L_FETCH_2ND_TRAP

if SINGLE_STEP_MISSED_WORKAROUND
    // Prioritize single step exception over context save.
    // Second-level trap will halt wave and RFE, re-entering for SAVECTX.
    s_getreg_b32    ttmp2, hwreg(HW_REG_MODE)
    s_and_b32       ttmp2, ttmp2, SQ_WAVE_MODE_DEBUG_EN_MASK
    s_cbranch_scc1  L_FETCH_2ND_TRAP
end

    s_and_b32       ttmp2, s_save_trapsts, SQ_WAVE_TRAPSTS_SAVECTX_MASK
    s_cbranch_scc1  L_SAVE

L_FETCH_2ND_TRAP:
    // Preserve and clear scalar XNACK state before issuing scalar reads.
    save_and_clear_ib_sts(ttmp14)

    // Read second-level TBA/TMA from first-level TMA and jump if available.
    // ttmp[2:5] and ttmp12 can be used (others hold SPI-initialized debug data)
    // ttmp12 holds SQ_WAVE_STATUS
    s_getreg_b32    ttmp14, hwreg(HW_REG_SQ_SHADER_TMA_LO)
    s_getreg_b32    ttmp15, hwreg(HW_REG_SQ_SHADER_TMA_HI)
    s_lshl_b64      [ttmp14, ttmp15], [ttmp14, ttmp15], 0x8

    s_load_dword    ttmp2, [ttmp14, ttmp15], 0x10 glc:1 // debug trap enabled flag
    s_waitcnt       lgkmcnt(0)
    s_lshl_b32      ttmp2, ttmp2, TTMP_DEBUG_TRAP_ENABLED_SHIFT
    s_andn2_b32     s_save_ib_sts, s_save_ib_sts, TTMP_DEBUG_TRAP_ENABLED_MASK
    s_or_b32        s_save_ib_sts, s_save_ib_sts, ttmp2

    s_load_dwordx2  [ttmp2, ttmp3], [ttmp14, ttmp15], 0x0 glc:1 // second-level TBA
    s_waitcnt       lgkmcnt(0)
    s_load_dwordx2  [ttmp14, ttmp15], [ttmp14, ttmp15], 0x8 glc:1 // second-level TMA
    s_waitcnt       lgkmcnt(0)

    s_and_b64       [ttmp2, ttmp3], [ttmp2, ttmp3], [ttmp2, ttmp3]
    s_cbranch_scc0  L_NO_NEXT_TRAP // second-level trap handler not been set
    s_setpc_b64     [ttmp2, ttmp3] // jump to second-level trap handler

L_NO_NEXT_TRAP:
    // If not caused by trap then halt wave to prevent re-entry.
    s_and_b32       ttmp2, s_save_pc_hi, (S_SAVE_PC_HI_TRAP_ID_MASK|S_SAVE_PC_HI_HT_MASK)
    s_cbranch_scc1  L_TRAP_CASE
    s_or_b32        s_save_status, s_save_status, SQ_WAVE_STATUS_HALT_MASK

    // If the PC points to S_ENDPGM then context save will fail if STATUS.HALT is set.
    // Rewind the PC to prevent this from occurring.
    s_sub_u32       ttmp0, ttmp0, 0x8
    s_subb_u32      ttmp1, ttmp1, 0x0

    s_branch        L_EXIT_TRAP

L_TRAP_CASE:
    // Host trap will not cause trap re-entry.
    s_and_b32       ttmp2, s_save_pc_hi, S_SAVE_PC_HI_HT_MASK
    s_cbranch_scc1  L_EXIT_TRAP

    // Advance past trap instruction to prevent re-entry.
    s_add_u32       ttmp0, ttmp0, 0x4
    s_addc_u32      ttmp1, ttmp1, 0x0

L_EXIT_TRAP:
    s_and_b32	ttmp1, ttmp1, 0xFFFF

    restore_ib_sts(ttmp14)

    // Restore SQ_WAVE_STATUS.
    s_and_b64       exec, exec, exec // Restore STATUS.EXECZ, not writable by s_setreg_b32
    s_and_b64       vcc, vcc, vcc    // Restore STATUS.VCCZ, not writable by s_setreg_b32
    set_status_without_spi_prio(s_save_status, ttmp2)

    s_rfe_b64       [ttmp0, ttmp1]

    // *********	End handling of non-CWSR traps	 *******************

/**************************************************************************/
/*			save routine					  */
/**************************************************************************/

L_SAVE:
    s_and_b32	    s_save_pc_hi, s_save_pc_hi, 0x0000ffff    //pc[47:32]

    s_mov_b32	    s_save_tmp, 0							    //clear saveCtx bit
    s_setreg_b32    hwreg(HW_REG_TRAPSTS, SQ_WAVE_TRAPSTS_SAVECTX_SHIFT, 1), s_save_tmp	    //clear saveCtx bit

    save_and_clear_ib_sts(s_save_tmp)

    /*	    inform SPI the readiness and wait for SPI's go signal */
    s_mov_b32	    s_save_exec_lo, exec_lo						    //save EXEC and use EXEC for the go signal from SPI
    s_mov_b32	    s_save_exec_hi, exec_hi
    s_mov_b64	    exec,   0x0								    //clear EXEC to get ready to receive

	s_sendmsg   sendmsg(MSG_SAVEWAVE)  //send SPI a message and wait for SPI's write to EXEC

    // Set SPI_PRIO=2 to avoid starving instruction fetch in the waves we're waiting for.
    s_or_b32 s_save_tmp, s_save_status, (2 << SQ_WAVE_STATUS_SPI_PRIO_SHIFT)
    s_setreg_b32 hwreg(HW_REG_STATUS), s_save_tmp

  L_SLEEP:
    s_sleep 0x2		       // sleep 1 (64clk) is not enough for 8 waves per SIMD, which will cause SQ hang, since the 7,8th wave could not get arbit to exec inst, while other waves are stuck into the sleep-loop and waiting for wrexec!=0

	s_cbranch_execz L_SLEEP

    // Save trap temporaries 4-11, 13 initialized by SPI debug dispatch logic
    // ttmp SR memory offset : size(VGPR)+size(SGPR)+0x40
    get_vgpr_size_bytes(s_save_ttmps_lo)
    get_sgpr_size_bytes(s_save_ttmps_hi)
    s_add_u32	    s_save_ttmps_lo, s_save_ttmps_lo, s_save_ttmps_hi
    s_add_u32	    s_save_ttmps_lo, s_save_ttmps_lo, s_save_spi_init_lo
    s_addc_u32	    s_save_ttmps_hi, s_save_spi_init_hi, 0x0
    s_and_b32	    s_save_ttmps_hi, s_save_ttmps_hi, 0xFFFF
    s_store_dwordx4 [ttmp4, ttmp5, ttmp6, ttmp7], [s_save_ttmps_lo, s_save_ttmps_hi], 0x50 glc:1
    ack_sqc_store_workaround()
    s_store_dwordx4 [ttmp8, ttmp9, ttmp10, ttmp11], [s_save_ttmps_lo, s_save_ttmps_hi], 0x60 glc:1
    ack_sqc_store_workaround()
    s_store_dword   ttmp13, [s_save_ttmps_lo, s_save_ttmps_hi], 0x74 glc:1
    ack_sqc_store_workaround()

    /*	    setup Resource Contants    */
    s_mov_b32	    s_save_buf_rsrc0,	s_save_spi_init_lo							//base_addr_lo
    s_and_b32	    s_save_buf_rsrc1,	s_save_spi_init_hi, 0x0000FFFF						//base_addr_hi
    s_or_b32	    s_save_buf_rsrc1,	s_save_buf_rsrc1,  S_SAVE_BUF_RSRC_WORD1_STRIDE
    s_mov_b32	    s_save_buf_rsrc2,	0									//NUM_RECORDS initial value = 0 (in bytes) although not neccessarily inited
    s_mov_b32	    s_save_buf_rsrc3,	S_SAVE_BUF_RSRC_WORD3_MISC

    //FIXME  right now s_save_m0/s_save_mem_offset use tma_lo/tma_hi  (might need to save them before using them?)
    s_mov_b32	    s_save_m0,		m0								    //save M0

    /*	    global mem offset		*/
    s_mov_b32	    s_save_mem_offset,	0x0									//mem offset initial value = 0




    /*	    save HW registers	*/
    //////////////////////////////

  L_SAVE_HWREG:
	// HWREG SR memory offset : size(VGPR)+size(SGPR)
       get_vgpr_size_bytes(s_save_mem_offset)
       get_sgpr_size_bytes(s_save_tmp)
       s_add_u32 s_save_mem_offset, s_save_mem_offset, s_save_tmp


    s_mov_b32	    s_save_buf_rsrc2, 0x4				//NUM_RECORDS	in bytes
	s_mov_b32	s_save_buf_rsrc2,  0x1000000				    //NUM_RECORDS in bytes


    write_hwreg_to_mem(s_save_m0, s_save_buf_rsrc0, s_save_mem_offset)			//M0
    write_hwreg_to_mem(s_save_pc_lo, s_save_buf_rsrc0, s_save_mem_offset)		    //PC
    write_hwreg_to_mem(s_save_pc_hi, s_save_buf_rsrc0, s_save_mem_offset)
    write_hwreg_to_mem(s_save_exec_lo, s_save_buf_rsrc0, s_save_mem_offset)		//EXEC
    write_hwreg_to_mem(s_save_exec_hi, s_save_buf_rsrc0, s_save_mem_offset)
    write_hwreg_to_mem(s_save_status, s_save_buf_rsrc0, s_save_mem_offset)		//STATUS

    //s_save_trapsts conflicts with s_save_alloc_size
    s_getreg_b32    s_save_trapsts, hwreg(HW_REG_TRAPSTS)
    write_hwreg_to_mem(s_save_trapsts, s_save_buf_rsrc0, s_save_mem_offset)		//TRAPSTS

    write_hwreg_to_mem(xnack_mask_lo, s_save_buf_rsrc0, s_save_mem_offset)	    //XNACK_MASK_LO
    write_hwreg_to_mem(xnack_mask_hi, s_save_buf_rsrc0, s_save_mem_offset)	    //XNACK_MASK_HI

    //use s_save_tmp would introduce conflict here between s_save_tmp and s_save_buf_rsrc2
    s_getreg_b32    s_save_m0, hwreg(HW_REG_MODE)						    //MODE
    write_hwreg_to_mem(s_save_m0, s_save_buf_rsrc0, s_save_mem_offset)



    /*	    the first wave in the threadgroup	 */
    s_and_b32	    s_save_tmp, s_save_spi_init_hi, S_SAVE_SPI_INIT_FIRST_WAVE_MASK	// extract fisrt wave bit
    s_mov_b32	     s_save_exec_hi, 0x0
    s_or_b32	     s_save_exec_hi, s_save_tmp, s_save_exec_hi				 // save first wave bit to s_save_exec_hi.bits[26]


    /*		save SGPRs	*/
	// Save SGPR before LDS save, then the s0 to s4 can be used during LDS save...
    //////////////////////////////

    // SGPR SR memory offset : size(VGPR)
    get_vgpr_size_bytes(s_save_mem_offset)
    // TODO, change RSRC word to rearrange memory layout for SGPRS

    s_getreg_b32    s_save_alloc_size, hwreg(HW_REG_GPR_ALLOC,SQ_WAVE_GPR_ALLOC_SGPR_SIZE_SHIFT,SQ_WAVE_GPR_ALLOC_SGPR_SIZE_SIZE)		//spgr_size
    s_add_u32	    s_save_alloc_size, s_save_alloc_size, 1
    s_lshl_b32	    s_save_alloc_size, s_save_alloc_size, 4			    //Number of SGPRs = (sgpr_size + 1) * 16   (non-zero value)

	s_lshl_b32	s_save_buf_rsrc2,   s_save_alloc_size, 2		    //NUM_RECORDS in bytes

	s_mov_b32	s_save_buf_rsrc2,  0x1000000				    //NUM_RECORDS in bytes


    // backup s_save_buf_rsrc0,1 to s_save_pc_lo/hi, since write_16sgpr_to_mem function will change the rsrc0
    //s_mov_b64 s_save_pc_lo, s_save_buf_rsrc0
    s_mov_b64 s_save_xnack_mask_lo, s_save_buf_rsrc0
    s_add_u32 s_save_buf_rsrc0, s_save_buf_rsrc0, s_save_mem_offset
    s_addc_u32 s_save_buf_rsrc1, s_save_buf_rsrc1, 0

    s_mov_b32	    m0, 0x0			    //SGPR initial index value =0
    s_nop	    0x0				    //Manually inserted wait states
  L_SAVE_SGPR_LOOP:
    // SGPR is allocated in 16 SGPR granularity
    s_movrels_b64   s0, s0     //s0 = s[0+m0], s1 = s[1+m0]
    s_movrels_b64   s2, s2     //s2 = s[2+m0], s3 = s[3+m0]
    s_movrels_b64   s4, s4     //s4 = s[4+m0], s5 = s[5+m0]
    s_movrels_b64   s6, s6     //s6 = s[6+m0], s7 = s[7+m0]
    s_movrels_b64   s8, s8     //s8 = s[8+m0], s9 = s[9+m0]
    s_movrels_b64   s10, s10   //s10 = s[10+m0], s11 = s[11+m0]
    s_movrels_b64   s12, s12   //s12 = s[12+m0], s13 = s[13+m0]
    s_movrels_b64   s14, s14   //s14 = s[14+m0], s15 = s[15+m0]

    write_16sgpr_to_mem(s0, s_save_buf_rsrc0, s_save_mem_offset) //PV: the best performance should be using s_buffer_store_dwordx4
    s_add_u32	    m0, m0, 16							    //next sgpr index
    s_cmp_lt_u32    m0, s_save_alloc_size					    //scc = (m0 < s_save_alloc_size) ? 1 : 0
    s_cbranch_scc1  L_SAVE_SGPR_LOOP					//SGPR save is complete?
    // restore s_save_buf_rsrc0,1
    //s_mov_b64 s_save_buf_rsrc0, s_save_pc_lo
    s_mov_b64 s_save_buf_rsrc0, s_save_xnack_mask_lo




    /*		save first 4 VGPR, then LDS save could use   */
	// each wave will alloc 4 vgprs at least...
    /////////////////////////////////////////////////////////////////////////////////////

    s_mov_b32	    s_save_mem_offset, 0
    s_mov_b32	    exec_lo, 0xFFFFFFFF						    //need every thread from now on
    s_mov_b32	    exec_hi, 0xFFFFFFFF
    s_mov_b32	    xnack_mask_lo, 0x0
    s_mov_b32	    xnack_mask_hi, 0x0

	s_mov_b32	s_save_buf_rsrc2,  0x1000000				    //NUM_RECORDS in bytes


    // VGPR Allocated in 4-GPR granularity

if SAVE_AFTER_XNACK_ERROR
	check_if_tcp_store_ok()
	s_cbranch_scc1 L_SAVE_FIRST_VGPRS_WITH_TCP

	write_vgprs_to_mem_with_sqc(v0, 4, s_save_buf_rsrc0, s_save_mem_offset)
	s_branch L_SAVE_LDS

L_SAVE_FIRST_VGPRS_WITH_TCP:
end

    write_4vgprs_to_mem(s_save_buf_rsrc0, s_save_mem_offset)

    /*		save LDS	*/
    //////////////////////////////

  L_SAVE_LDS:

	// Change EXEC to all threads...
    s_mov_b32	    exec_lo, 0xFFFFFFFF	  //need every thread from now on
    s_mov_b32	    exec_hi, 0xFFFFFFFF

    s_getreg_b32    s_save_alloc_size, hwreg(HW_REG_LDS_ALLOC,SQ_WAVE_LDS_ALLOC_LDS_SIZE_SHIFT,SQ_WAVE_LDS_ALLOC_LDS_SIZE_SIZE)		    //lds_size
    s_and_b32	    s_save_alloc_size, s_save_alloc_size, 0xFFFFFFFF		    //lds_size is zero?
    s_cbranch_scc0  L_SAVE_LDS_DONE									       //no lds used? jump to L_SAVE_DONE

    s_barrier		    //LDS is used? wait for other waves in the same TG
    s_and_b32	    s_save_tmp, s_save_exec_hi, S_SAVE_SPI_INIT_FIRST_WAVE_MASK		       //exec is still used here
    s_cbranch_scc0  L_SAVE_LDS_DONE

	// first wave do LDS save;

    s_lshl_b32	    s_save_alloc_size, s_save_alloc_size, 6			    //LDS size in dwords = lds_size * 64dw
    s_lshl_b32	    s_save_alloc_size, s_save_alloc_size, 2			    //LDS size in bytes
    s_mov_b32	    s_save_buf_rsrc2,  s_save_alloc_size			    //NUM_RECORDS in bytes

    // LDS at offset: size(VGPR)+SIZE(SGPR)+SIZE(HWREG)
    //
    get_vgpr_size_bytes(s_save_mem_offset)
    get_sgpr_size_bytes(s_save_tmp)
    s_add_u32  s_save_mem_offset, s_save_mem_offset, s_save_tmp
    s_add_u32 s_save_mem_offset, s_save_mem_offset, get_hwreg_size_bytes()


	s_mov_b32	s_save_buf_rsrc2,  0x1000000		      //NUM_RECORDS in bytes

    s_mov_b32	    m0, 0x0						  //lds_offset initial value = 0


      v_mbcnt_lo_u32_b32 v2, 0xffffffff, 0x0
      v_mbcnt_hi_u32_b32 v3, 0xffffffff, v2	// tid

if SAVE_AFTER_XNACK_ERROR
	check_if_tcp_store_ok()
	s_cbranch_scc1 L_SAVE_LDS_WITH_TCP

	v_lshlrev_b32 v2, 2, v3
L_SAVE_LDS_LOOP_SQC:
	ds_read2_b32 v[0:1], v2 offset0:0 offset1:0x40
	s_waitcnt lgkmcnt(0)

	write_vgprs_to_mem_with_sqc(v0, 2, s_save_buf_rsrc0, s_save_mem_offset)

	v_add_u32 v2, 0x200, v2
	v_cmp_lt_u32 vcc[0:1], v2, s_save_alloc_size
	s_cbranch_vccnz L_SAVE_LDS_LOOP_SQC

	s_branch L_SAVE_LDS_DONE

L_SAVE_LDS_WITH_TCP:
end

      v_mul_i32_i24 v2, v3, 8	// tid*8
      v_mov_b32 v3, 256*2
      s_mov_b32 m0, 0x10000
      s_mov_b32 s0, s_save_buf_rsrc3
      s_and_b32 s_save_buf_rsrc3, s_save_buf_rsrc3, 0xFF7FFFFF	  // disable add_tid
      s_or_b32 s_save_buf_rsrc3, s_save_buf_rsrc3, 0x58000   //DFMT

L_SAVE_LDS_LOOP_VECTOR:
      ds_read_b64 v[0:1], v2	//x =LDS[a], byte address
      s_waitcnt lgkmcnt(0)
      buffer_store_dwordx2  v[0:1], v2, s_save_buf_rsrc0, s_save_mem_offset offen:1  glc:1  slc:1
//	s_waitcnt vmcnt(0)
//	v_add_u32 v2, vcc[0:1], v2, v3
      v_add_u32 v2, v2, v3
      v_cmp_lt_u32 vcc[0:1], v2, s_save_alloc_size
      s_cbranch_vccnz L_SAVE_LDS_LOOP_VECTOR

      // restore rsrc3
      s_mov_b32 s_save_buf_rsrc3, s0

L_SAVE_LDS_DONE:


    /*		save VGPRs  - set the Rest VGPRs	*/
    //////////////////////////////////////////////////////////////////////////////////////
  L_SAVE_VGPR:
    // VGPR SR memory offset: 0
    // TODO rearrange the RSRC words to use swizzle for VGPR save...

    s_mov_b32	    s_save_mem_offset, (0+256*4)				    // for the rest VGPRs
    s_mov_b32	    exec_lo, 0xFFFFFFFF						    //need every thread from now on
    s_mov_b32	    exec_hi, 0xFFFFFFFF

    get_num_arch_vgprs(s_save_alloc_size)
    s_mov_b32	    s_save_buf_rsrc2,  0x1000000				    //NUM_RECORDS in bytes


    // VGPR store using dw burst
    s_mov_b32	      m0, 0x4	//VGPR initial index value =0
    s_cmp_lt_u32      m0, s_save_alloc_size
    s_cbranch_scc0    L_SAVE_VGPR_END


    s_set_gpr_idx_on	m0, 0x1 //M0[7:0] = M0[7:0] and M0[15:12] = 0x1
    s_add_u32	    s_save_alloc_size, s_save_alloc_size, 0x1000		    //add 0x1000 since we compare m0 against it later

if SAVE_AFTER_XNACK_ERROR
	check_if_tcp_store_ok()
	s_cbranch_scc1 L_SAVE_VGPR_LOOP

L_SAVE_VGPR_LOOP_SQC:
	write_vgprs_to_mem_with_sqc(v0, 4, s_save_buf_rsrc0, s_save_mem_offset)

	s_add_u32 m0, m0, 4
	s_cmp_lt_u32 m0, s_save_alloc_size
	s_cbranch_scc1 L_SAVE_VGPR_LOOP_SQC

	s_set_gpr_idx_off
	s_branch L_SAVE_VGPR_END
end

  L_SAVE_VGPR_LOOP:
    v_mov_b32	    v0, v0		//v0 = v[0+m0]
    v_mov_b32	    v1, v1		//v0 = v[0+m0]
    v_mov_b32	    v2, v2		//v0 = v[0+m0]
    v_mov_b32	    v3, v3		//v0 = v[0+m0]

    write_4vgprs_to_mem(s_save_buf_rsrc0, s_save_mem_offset)

    s_add_u32	    m0, m0, 4							    //next vgpr index
    s_add_u32	    s_save_mem_offset, s_save_mem_offset, 256*4			    //every buffer_store_dword does 256 bytes
    s_cmp_lt_u32    m0, s_save_alloc_size					    //scc = (m0 < s_save_alloc_size) ? 1 : 0
    s_cbranch_scc1  L_SAVE_VGPR_LOOP						    //VGPR save is complete?
    s_set_gpr_idx_off

L_SAVE_VGPR_END:

#if ASIC_FAMILY >= CHIP_ARCTURUS
    // Save ACC VGPRs

#if ASIC_FAMILY >= CHIP_ALDEBARAN
    // ACC VGPR count may differ from ARCH VGPR count.
    get_num_acc_vgprs(s_save_alloc_size, s_save_tmp)
    s_and_b32       s_save_alloc_size, s_save_alloc_size, s_save_alloc_size
    s_cbranch_scc0  L_SAVE_ACCVGPR_END
    s_add_u32	    s_save_alloc_size, s_save_alloc_size, 0x1000		    //add 0x1000 since we compare m0 against it later
#endif

    s_mov_b32 m0, 0x0 //VGPR initial index value =0
    s_set_gpr_idx_on m0, 0x1 //M0[7:0] = M0[7:0] and M0[15:12] = 0x1

if SAVE_AFTER_XNACK_ERROR
    check_if_tcp_store_ok()
    s_cbranch_scc1 L_SAVE_ACCVGPR_LOOP

L_SAVE_ACCVGPR_LOOP_SQC:
    for var vgpr = 0; vgpr < 4; ++ vgpr
        v_accvgpr_read v[vgpr], acc[vgpr]  // v[N] = acc[N+m0]
    end

    write_vgprs_to_mem_with_sqc(v0, 4, s_save_buf_rsrc0, s_save_mem_offset)

    s_add_u32 m0, m0, 4
    s_cmp_lt_u32 m0, s_save_alloc_size
    s_cbranch_scc1 L_SAVE_ACCVGPR_LOOP_SQC

    s_set_gpr_idx_off
    s_branch L_SAVE_ACCVGPR_END
end

L_SAVE_ACCVGPR_LOOP:
    for var vgpr = 0; vgpr < 4; ++ vgpr
        v_accvgpr_read v[vgpr], acc[vgpr]  // v[N] = acc[N+m0]
    end

    write_4vgprs_to_mem(s_save_buf_rsrc0, s_save_mem_offset)

    s_add_u32 m0, m0, 4
    s_add_u32 s_save_mem_offset, s_save_mem_offset, 256*4
    s_cmp_lt_u32 m0, s_save_alloc_size
    s_cbranch_scc1 L_SAVE_ACCVGPR_LOOP
    s_set_gpr_idx_off

L_SAVE_ACCVGPR_END:
#endif

    s_branch	L_END_PGM



/**************************************************************************/
/*			restore routine					  */
/**************************************************************************/

L_RESTORE:
    /*	    Setup Resource Contants    */
    s_mov_b32	    s_restore_buf_rsrc0,    s_restore_spi_init_lo							    //base_addr_lo
    s_and_b32	    s_restore_buf_rsrc1,    s_restore_spi_init_hi, 0x0000FFFF						    //base_addr_hi
    s_or_b32	    s_restore_buf_rsrc1,    s_restore_buf_rsrc1,  S_RESTORE_BUF_RSRC_WORD1_STRIDE
    s_mov_b32	    s_restore_buf_rsrc2,    0										    //NUM_RECORDS initial value = 0 (in bytes)
    s_mov_b32	    s_restore_buf_rsrc3,    S_RESTORE_BUF_RSRC_WORD3_MISC

    /*	    global mem offset		*/
//  s_mov_b32	    s_restore_mem_offset, 0x0				    //mem offset initial value = 0

    /*	    the first wave in the threadgroup	 */
    s_and_b32	    s_restore_tmp, s_restore_spi_init_hi, S_RESTORE_SPI_INIT_FIRST_WAVE_MASK
    s_cbranch_scc0  L_RESTORE_VGPR

    /*		restore LDS	*/
    //////////////////////////////
  L_RESTORE_LDS:

    s_mov_b32	    exec_lo, 0xFFFFFFFF							    //need every thread from now on   //be consistent with SAVE although can be moved ahead
    s_mov_b32	    exec_hi, 0xFFFFFFFF

    s_getreg_b32    s_restore_alloc_size, hwreg(HW_REG_LDS_ALLOC,SQ_WAVE_LDS_ALLOC_LDS_SIZE_SHIFT,SQ_WAVE_LDS_ALLOC_LDS_SIZE_SIZE)		//lds_size
    s_and_b32	    s_restore_alloc_size, s_restore_alloc_size, 0xFFFFFFFF		    //lds_size is zero?
    s_cbranch_scc0  L_RESTORE_VGPR							    //no lds used? jump to L_RESTORE_VGPR
    s_lshl_b32	    s_restore_alloc_size, s_restore_alloc_size, 6			    //LDS size in dwords = lds_size * 64dw
    s_lshl_b32	    s_restore_alloc_size, s_restore_alloc_size, 2			    //LDS size in bytes
    s_mov_b32	    s_restore_buf_rsrc2,    s_restore_alloc_size			    //NUM_RECORDS in bytes

    // LDS at offset: size(VGPR)+SIZE(SGPR)+SIZE(HWREG)
    //
    get_vgpr_size_bytes(s_restore_mem_offset)
    get_sgpr_size_bytes(s_restore_tmp)
    s_add_u32  s_restore_mem_offset, s_restore_mem_offset, s_restore_tmp
    s_add_u32  s_restore_mem_offset, s_restore_mem_offset, get_hwreg_size_bytes()	     //FIXME, Check if offset overflow???


	s_mov_b32	s_restore_buf_rsrc2,  0x1000000					    //NUM_RECORDS in bytes
    s_mov_b32	    m0, 0x0								    //lds_offset initial value = 0

  L_RESTORE_LDS_LOOP:
	buffer_load_dword   v0, v0, s_restore_buf_rsrc0, s_restore_mem_offset lds:1		       // first 64DW
	buffer_load_dword   v0, v0, s_restore_buf_rsrc0, s_restore_mem_offset lds:1 offset:256	       // second 64DW
    s_add_u32	    m0, m0, 256*2						// 128 DW
    s_add_u32	    s_restore_mem_offset, s_restore_mem_offset, 256*2		//mem offset increased by 128DW
    s_cmp_lt_u32    m0, s_restore_alloc_size					//scc=(m0 < s_restore_alloc_size) ? 1 : 0
    s_cbranch_scc1  L_RESTORE_LDS_LOOP							    //LDS restore is complete?


    /*		restore VGPRs	    */
    //////////////////////////////
  L_RESTORE_VGPR:
    s_mov_b32	    exec_lo, 0xFFFFFFFF							    //need every thread from now on   //be consistent with SAVE although can be moved ahead
    s_mov_b32	    exec_hi, 0xFFFFFFFF
    s_mov_b32	    s_restore_buf_rsrc2,  0x1000000					    //NUM_RECORDS in bytes

    // Save ARCH VGPRs 4-N, then all ACC VGPRs, then ARCH VGPRs 0-3.
    get_num_arch_vgprs(s_restore_alloc_size)
    s_add_u32	    s_restore_alloc_size, s_restore_alloc_size, 0x8000			    //add 0x8000 since we compare m0 against it later

    // ARCH VGPRs at offset: 0
    s_mov_b32	    s_restore_mem_offset, 0x0
    s_mov_b32	    s_restore_mem_offset_save, s_restore_mem_offset	// restore start with v1, v0 will be the last
    s_add_u32	    s_restore_mem_offset, s_restore_mem_offset, 256*4
    s_mov_b32	    m0, 4				//VGPR initial index value = 1
    s_set_gpr_idx_on	m0, 0x8								    //M0[7:0] = M0[7:0] and M0[15:12] = 0x8

  L_RESTORE_VGPR_LOOP:
    read_4vgprs_from_mem(s_restore_buf_rsrc0, s_restore_mem_offset)
    v_mov_b32	    v0, v0								    //v[0+m0] = v0
    v_mov_b32	    v1, v1
    v_mov_b32	    v2, v2
    v_mov_b32	    v3, v3
    s_add_u32	    m0, m0, 4								    //next vgpr index
    s_add_u32	    s_restore_mem_offset, s_restore_mem_offset, 256*4				//every buffer_load_dword does 256 bytes
    s_cmp_lt_u32    m0, s_restore_alloc_size						    //scc = (m0 < s_restore_alloc_size) ? 1 : 0
    s_cbranch_scc1  L_RESTORE_VGPR_LOOP							    //VGPR restore (except v0) is complete?

#if ASIC_FAMILY >= CHIP_ALDEBARAN
    // ACC VGPR count may differ from ARCH VGPR count.
    get_num_acc_vgprs(s_restore_alloc_size, s_restore_tmp2)
    s_and_b32       s_restore_alloc_size, s_restore_alloc_size, s_restore_alloc_size
    s_cbranch_scc0  L_RESTORE_ACCVGPR_END
    s_add_u32	    s_restore_alloc_size, s_restore_alloc_size, 0x8000			    //add 0x8000 since we compare m0 against it later
#endif

#if ASIC_FAMILY >= CHIP_ARCTURUS
    // ACC VGPRs at offset: size(ARCH VGPRs)
    s_mov_b32	    m0, 0
    s_set_gpr_idx_on	m0, 0x8								    //M0[7:0] = M0[7:0] and M0[15:12] = 0x8

  L_RESTORE_ACCVGPR_LOOP:
    read_4vgprs_from_mem(s_restore_buf_rsrc0, s_restore_mem_offset)

    for var vgpr = 0; vgpr < 4; ++ vgpr
        v_accvgpr_write acc[vgpr], v[vgpr]
    end

    s_add_u32	    m0, m0, 4								    //next vgpr index
    s_add_u32	    s_restore_mem_offset, s_restore_mem_offset, 256*4			    //every buffer_load_dword does 256 bytes
    s_cmp_lt_u32    m0, s_restore_alloc_size						    //scc = (m0 < s_restore_alloc_size) ? 1 : 0
    s_cbranch_scc1  L_RESTORE_ACCVGPR_LOOP						    //VGPR restore (except v0) is complete?
  L_RESTORE_ACCVGPR_END:
#endif

    s_set_gpr_idx_off

    // Restore VGPRs 0-3 last, no longer needed.
    read_4vgprs_from_mem(s_restore_buf_rsrc0, s_restore_mem_offset_save)

    /*		restore SGPRs	    */
    //////////////////////////////

    // SGPR SR memory offset : size(VGPR)
    get_vgpr_size_bytes(s_restore_mem_offset)
    get_sgpr_size_bytes(s_restore_tmp)
    s_add_u32 s_restore_mem_offset, s_restore_mem_offset, s_restore_tmp
    s_sub_u32 s_restore_mem_offset, s_restore_mem_offset, 16*4	   // restore SGPR from S[n] to S[0], by 16 sgprs group
    // TODO, change RSRC word to rearrange memory layout for SGPRS

    s_getreg_b32    s_restore_alloc_size, hwreg(HW_REG_GPR_ALLOC,SQ_WAVE_GPR_ALLOC_SGPR_SIZE_SHIFT,SQ_WAVE_GPR_ALLOC_SGPR_SIZE_SIZE)		    //spgr_size
    s_add_u32	    s_restore_alloc_size, s_restore_alloc_size, 1
    s_lshl_b32	    s_restore_alloc_size, s_restore_alloc_size, 4			    //Number of SGPRs = (sgpr_size + 1) * 16   (non-zero value)

	s_lshl_b32	s_restore_buf_rsrc2,	s_restore_alloc_size, 2			    //NUM_RECORDS in bytes
	s_mov_b32	s_restore_buf_rsrc2,  0x1000000					    //NUM_RECORDS in bytes

    s_mov_b32 m0, s_restore_alloc_size

 L_RESTORE_SGPR_LOOP:
    read_16sgpr_from_mem(s0, s_restore_buf_rsrc0, s_restore_mem_offset)	 //PV: further performance improvement can be made
    s_waitcnt	    lgkmcnt(0)								    //ensure data ready

    s_sub_u32 m0, m0, 16    // Restore from S[n] to S[0]
    s_nop 0 // hazard SALU M0=> S_MOVREL

    s_movreld_b64   s0, s0	//s[0+m0] = s0
    s_movreld_b64   s2, s2
    s_movreld_b64   s4, s4
    s_movreld_b64   s6, s6
    s_movreld_b64   s8, s8
    s_movreld_b64   s10, s10
    s_movreld_b64   s12, s12
    s_movreld_b64   s14, s14

    s_cmp_eq_u32    m0, 0		//scc = (m0 < s_restore_alloc_size) ? 1 : 0
    s_cbranch_scc0  L_RESTORE_SGPR_LOOP		    //SGPR restore (except s0) is complete?

    /*	    restore HW registers    */
    //////////////////////////////
  L_RESTORE_HWREG:


    // HWREG SR memory offset : size(VGPR)+size(SGPR)
    get_vgpr_size_bytes(s_restore_mem_offset)
    get_sgpr_size_bytes(s_restore_tmp)
    s_add_u32 s_restore_mem_offset, s_restore_mem_offset, s_restore_tmp


    s_mov_b32	    s_restore_buf_rsrc2, 0x4						    //NUM_RECORDS   in bytes
	s_mov_b32	s_restore_buf_rsrc2,  0x1000000					    //NUM_RECORDS in bytes

    read_hwreg_from_mem(s_restore_m0, s_restore_buf_rsrc0, s_restore_mem_offset)		    //M0
    read_hwreg_from_mem(s_restore_pc_lo, s_restore_buf_rsrc0, s_restore_mem_offset)		//PC
    read_hwreg_from_mem(s_restore_pc_hi, s_restore_buf_rsrc0, s_restore_mem_offset)
    read_hwreg_from_mem(s_restore_exec_lo, s_restore_buf_rsrc0, s_restore_mem_offset)		    //EXEC
    read_hwreg_from_mem(s_restore_exec_hi, s_restore_buf_rsrc0, s_restore_mem_offset)
    read_hwreg_from_mem(s_restore_status, s_restore_buf_rsrc0, s_restore_mem_offset)		    //STATUS
    read_hwreg_from_mem(s_restore_trapsts, s_restore_buf_rsrc0, s_restore_mem_offset)		    //TRAPSTS
    read_hwreg_from_mem(xnack_mask_lo, s_restore_buf_rsrc0, s_restore_mem_offset)		    //XNACK_MASK_LO
    read_hwreg_from_mem(xnack_mask_hi, s_restore_buf_rsrc0, s_restore_mem_offset)		    //XNACK_MASK_HI
    read_hwreg_from_mem(s_restore_mode, s_restore_buf_rsrc0, s_restore_mem_offset)		//MODE

    s_waitcnt	    lgkmcnt(0)											    //from now on, it is safe to restore STATUS and IB_STS

    s_mov_b32	    m0,		s_restore_m0
    s_mov_b32	    exec_lo,	s_restore_exec_lo
    s_mov_b32	    exec_hi,	s_restore_exec_hi

    s_and_b32	    s_restore_m0, SQ_WAVE_TRAPSTS_PRE_SAVECTX_MASK, s_restore_trapsts
    s_setreg_b32    hwreg(HW_REG_TRAPSTS, SQ_WAVE_TRAPSTS_PRE_SAVECTX_SHIFT, SQ_WAVE_TRAPSTS_PRE_SAVECTX_SIZE), s_restore_m0
    s_and_b32	    s_restore_m0, SQ_WAVE_TRAPSTS_POST_SAVECTX_MASK, s_restore_trapsts
    s_lshr_b32	    s_restore_m0, s_restore_m0, SQ_WAVE_TRAPSTS_POST_SAVECTX_SHIFT
    s_setreg_b32    hwreg(HW_REG_TRAPSTS, SQ_WAVE_TRAPSTS_POST_SAVECTX_SHIFT, SQ_WAVE_TRAPSTS_POST_SAVECTX_SIZE), s_restore_m0
    //s_setreg_b32  hwreg(HW_REG_TRAPSTS),  s_restore_trapsts	   //don't overwrite SAVECTX bit as it may be set through external SAVECTX during restore
    s_setreg_b32    hwreg(HW_REG_MODE),	    s_restore_mode

    // Restore trap temporaries 4-11, 13 initialized by SPI debug dispatch logic
    // ttmp SR memory offset : size(VGPR)+size(SGPR)+0x40
    get_vgpr_size_bytes(s_restore_ttmps_lo)
    get_sgpr_size_bytes(s_restore_ttmps_hi)
    s_add_u32	    s_restore_ttmps_lo, s_restore_ttmps_lo, s_restore_ttmps_hi
    s_add_u32	    s_restore_ttmps_lo, s_restore_ttmps_lo, s_restore_buf_rsrc0
    s_addc_u32	    s_restore_ttmps_hi, s_restore_buf_rsrc1, 0x0
    s_and_b32	    s_restore_ttmps_hi, s_restore_ttmps_hi, 0xFFFF
    s_load_dwordx4  [ttmp4, ttmp5, ttmp6, ttmp7], [s_restore_ttmps_lo, s_restore_ttmps_hi], 0x50 glc:1
    s_load_dwordx4  [ttmp8, ttmp9, ttmp10, ttmp11], [s_restore_ttmps_lo, s_restore_ttmps_hi], 0x60 glc:1
    s_load_dword    ttmp13, [s_restore_ttmps_lo, s_restore_ttmps_hi], 0x74 glc:1
    s_waitcnt	    lgkmcnt(0)

    restore_ib_sts(s_restore_tmp)

    s_and_b32 s_restore_pc_hi, s_restore_pc_hi, 0x0000ffff	//pc[47:32]	   //Do it here in order not to affect STATUS
    s_and_b64	 exec, exec, exec  // Restore STATUS.EXECZ, not writable by s_setreg_b32
    s_and_b64	 vcc, vcc, vcc	// Restore STATUS.VCCZ, not writable by s_setreg_b32
    set_status_without_spi_prio(s_restore_status, s_restore_tmp) // SCC is included, which is changed by previous salu

    s_barrier							//barrier to ensure the readiness of LDS before access attempts from any other wave in the same TG //FIXME not performance-optimal at this time

    s_rfe_b64 s_restore_pc_lo					//Return to the main shader program and resume execution


/**************************************************************************/
/*			the END						  */
/**************************************************************************/
L_END_PGM:
    s_endpgm

end


/**************************************************************************/
/*			the helper functions				  */
/**************************************************************************/

//Only for save hwreg to mem
function write_hwreg_to_mem(s, s_rsrc, s_mem_offset)
	s_mov_b32 exec_lo, m0			//assuming exec_lo is not needed anymore from this point on
	s_mov_b32 m0, s_mem_offset
	s_buffer_store_dword s, s_rsrc, m0	glc:1
	ack_sqc_store_workaround()
	s_add_u32	s_mem_offset, s_mem_offset, 4
	s_mov_b32   m0, exec_lo
end


// HWREG are saved before SGPRs, so all HWREG could be use.
function write_16sgpr_to_mem(s, s_rsrc, s_mem_offset)

	s_buffer_store_dwordx4 s[0], s_rsrc, 0	glc:1
	ack_sqc_store_workaround()
	s_buffer_store_dwordx4 s[4], s_rsrc, 16	 glc:1
	ack_sqc_store_workaround()
	s_buffer_store_dwordx4 s[8], s_rsrc, 32	 glc:1
	ack_sqc_store_workaround()
	s_buffer_store_dwordx4 s[12], s_rsrc, 48 glc:1
	ack_sqc_store_workaround()
	s_add_u32	s_rsrc[0], s_rsrc[0], 4*16
	s_addc_u32	s_rsrc[1], s_rsrc[1], 0x0	      // +scc
end


function read_hwreg_from_mem(s, s_rsrc, s_mem_offset)
    s_buffer_load_dword s, s_rsrc, s_mem_offset	    glc:1
    s_add_u32	    s_mem_offset, s_mem_offset, 4
end

function read_16sgpr_from_mem(s, s_rsrc, s_mem_offset)
    s_buffer_load_dwordx16 s, s_rsrc, s_mem_offset	glc:1
    s_sub_u32	    s_mem_offset, s_mem_offset, 4*16
end

function check_if_tcp_store_ok
	// If STATUS.ALLOW_REPLAY=0 and TRAPSTS.XNACK_ERROR=1 then TCP stores will fail.
	s_and_b32 s_save_tmp, s_save_status, SQ_WAVE_STATUS_ALLOW_REPLAY_MASK
	s_cbranch_scc1 L_TCP_STORE_CHECK_DONE

	s_getreg_b32 s_save_tmp, hwreg(HW_REG_TRAPSTS)
	s_andn2_b32 s_save_tmp, SQ_WAVE_TRAPSTS_XNACK_ERROR_MASK, s_save_tmp

L_TCP_STORE_CHECK_DONE:
end

function write_4vgprs_to_mem(s_rsrc, s_mem_offset)
	buffer_store_dword v0, v0, s_rsrc, s_mem_offset slc:1 glc:1
	buffer_store_dword v1, v0, s_rsrc, s_mem_offset slc:1 glc:1  offset:256
	buffer_store_dword v2, v0, s_rsrc, s_mem_offset slc:1 glc:1  offset:256*2
	buffer_store_dword v3, v0, s_rsrc, s_mem_offset slc:1 glc:1  offset:256*3
end

function read_4vgprs_from_mem(s_rsrc, s_mem_offset)
	buffer_load_dword v0, v0, s_rsrc, s_mem_offset slc:1 glc:1
	buffer_load_dword v1, v0, s_rsrc, s_mem_offset slc:1 glc:1 offset:256
	buffer_load_dword v2, v0, s_rsrc, s_mem_offset slc:1 glc:1 offset:256*2
	buffer_load_dword v3, v0, s_rsrc, s_mem_offset slc:1 glc:1 offset:256*3
	s_waitcnt vmcnt(0)
end

function write_vgpr_to_mem_with_sqc(v, s_rsrc, s_mem_offset)
	s_mov_b32 s4, 0

L_WRITE_VGPR_LANE_LOOP:
	for var lane = 0; lane < 4; ++ lane
		v_readlane_b32 s[lane], v, s4
		s_add_u32 s4, s4, 1
	end

	s_buffer_store_dwordx4 s[0:3], s_rsrc, s_mem_offset glc:1
	ack_sqc_store_workaround()

	s_add_u32 s_mem_offset, s_mem_offset, 0x10
	s_cmp_eq_u32 s4, 0x40
	s_cbranch_scc0 L_WRITE_VGPR_LANE_LOOP
end

function write_vgprs_to_mem_with_sqc(v, n_vgprs, s_rsrc, s_mem_offset)
	for var vgpr = 0; vgpr < n_vgprs; ++ vgpr
		write_vgpr_to_mem_with_sqc(v[vgpr], s_rsrc, s_mem_offset)
	end
end

function get_lds_size_bytes(s_lds_size_byte)
    // SQ LDS granularity is 64DW, while PGM_RSRC2.lds_size is in granularity 128DW
    s_getreg_b32   s_lds_size_byte, hwreg(HW_REG_LDS_ALLOC, SQ_WAVE_LDS_ALLOC_LDS_SIZE_SHIFT, SQ_WAVE_LDS_ALLOC_LDS_SIZE_SIZE)		// lds_size
    s_lshl_b32	   s_lds_size_byte, s_lds_size_byte, 8			    //LDS size in dwords = lds_size * 64 *4Bytes    // granularity 64DW
end

function get_vgpr_size_bytes(s_vgpr_size_byte)
    s_getreg_b32   s_vgpr_size_byte, hwreg(HW_REG_GPR_ALLOC,SQ_WAVE_GPR_ALLOC_VGPR_SIZE_SHIFT,SQ_WAVE_GPR_ALLOC_VGPR_SIZE_SIZE)	 //vpgr_size
    s_add_u32	   s_vgpr_size_byte, s_vgpr_size_byte, 1
    s_lshl_b32	   s_vgpr_size_byte, s_vgpr_size_byte, (2+8) //Number of VGPRs = (vgpr_size + 1) * 4 * 64 * 4	(non-zero value)   //FIXME for GFX, zero is possible

#if ASIC_FAMILY >= CHIP_ARCTURUS
    s_lshl_b32     s_vgpr_size_byte, s_vgpr_size_byte, 1  // Double size for ACC VGPRs
#endif
end

function get_sgpr_size_bytes(s_sgpr_size_byte)
    s_getreg_b32   s_sgpr_size_byte, hwreg(HW_REG_GPR_ALLOC,SQ_WAVE_GPR_ALLOC_SGPR_SIZE_SHIFT,SQ_WAVE_GPR_ALLOC_SGPR_SIZE_SIZE)	 //spgr_size
    s_add_u32	   s_sgpr_size_byte, s_sgpr_size_byte, 1
    s_lshl_b32	   s_sgpr_size_byte, s_sgpr_size_byte, 6 //Number of SGPRs = (sgpr_size + 1) * 16 *4   (non-zero value)
end

function get_hwreg_size_bytes
    return 128 //HWREG size 128 bytes
end

function get_num_arch_vgprs(s_num_arch_vgprs)
#if ASIC_FAMILY >= CHIP_ALDEBARAN
    // VGPR count includes ACC VGPRs, use ACC VGPR offset for ARCH VGPR count.
    s_getreg_b32    s_num_arch_vgprs, hwreg(HW_REG_GPR_ALLOC,SQ_WAVE_GPR_ALLOC_ACCV_OFFSET_SHIFT,SQ_WAVE_GPR_ALLOC_ACCV_OFFSET_SIZE)
#else
    s_getreg_b32    s_num_arch_vgprs, hwreg(HW_REG_GPR_ALLOC,SQ_WAVE_GPR_ALLOC_VGPR_SIZE_SHIFT,SQ_WAVE_GPR_ALLOC_VGPR_SIZE_SIZE)
#endif

    // Number of VGPRs = (vgpr_size + 1) * 4
    s_add_u32	    s_num_arch_vgprs, s_num_arch_vgprs, 1
    s_lshl_b32	    s_num_arch_vgprs, s_num_arch_vgprs, 2
end

#if ASIC_FAMILY >= CHIP_ALDEBARAN
function get_num_acc_vgprs(s_num_acc_vgprs, s_tmp)
    // VGPR count = (GPR_ALLOC.VGPR_SIZE + 1) * 8
    s_getreg_b32    s_num_acc_vgprs, hwreg(HW_REG_GPR_ALLOC,SQ_WAVE_GPR_ALLOC_VGPR_SIZE_SHIFT,SQ_WAVE_GPR_ALLOC_VGPR_SIZE_SIZE)
    s_add_u32	    s_num_acc_vgprs, s_num_acc_vgprs, 1
    s_lshl_b32	    s_num_acc_vgprs, s_num_acc_vgprs, 3

    // ACC VGPR count = VGPR count - ARCH VGPR count.
    get_num_arch_vgprs(s_tmp)
    s_sub_u32	    s_num_acc_vgprs, s_num_acc_vgprs, s_tmp
end
#endif

function ack_sqc_store_workaround
    if ACK_SQC_STORE
        s_waitcnt lgkmcnt(0)
    end
end

function set_status_without_spi_prio(status, tmp)
    // Do not restore STATUS.SPI_PRIO since scheduler may have raised it.
    s_lshr_b32      tmp, status, SQ_WAVE_STATUS_POST_SPI_PRIO_SHIFT
    s_setreg_b32    hwreg(HW_REG_STATUS, SQ_WAVE_STATUS_POST_SPI_PRIO_SHIFT, SQ_WAVE_STATUS_POST_SPI_PRIO_SIZE), tmp
    s_nop           0x2 // avoid S_SETREG => S_SETREG hazard
    s_setreg_b32    hwreg(HW_REG_STATUS, SQ_WAVE_STATUS_PRE_SPI_PRIO_SHIFT, SQ_WAVE_STATUS_PRE_SPI_PRIO_SIZE), status
end

function save_and_clear_ib_sts(tmp)
    // Save IB_STS.FIRST_REPLAY[15] and IB_STS.RCNT[20:16] into unused space s_save_ib_sts[31:26].
    s_getreg_b32    tmp, hwreg(HW_REG_IB_STS)
    s_and_b32       tmp, tmp, SQ_WAVE_IB_STS_RCNT_FIRST_REPLAY_MASK
    s_lshl_b32      tmp, tmp, (TTMP_SAVE_RCNT_FIRST_REPLAY_SHIFT - SQ_WAVE_IB_STS_FIRST_REPLAY_SHIFT)
    s_andn2_b32     s_save_ib_sts, s_save_ib_sts, TTMP_SAVE_RCNT_FIRST_REPLAY_MASK
    s_or_b32        s_save_ib_sts, s_save_ib_sts, tmp
    s_setreg_imm32_b32 hwreg(HW_REG_IB_STS), 0x0
end

function restore_ib_sts(tmp)
    s_lshr_b32      tmp, s_save_ib_sts, (TTMP_SAVE_RCNT_FIRST_REPLAY_SHIFT - SQ_WAVE_IB_STS_FIRST_REPLAY_SHIFT)
    s_and_b32       tmp, tmp, SQ_WAVE_IB_STS_RCNT_FIRST_REPLAY_MASK
    s_setreg_b32    hwreg(HW_REG_IB_STS), tmp
end
