/*
 * Copyright 2015-2017 Advanced Micro Devices, Inc.
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
 * PROJECT=vi ./sp3 cwsr_trap_handler_gfx8.asm -hex tmp.hex
 */

/* HW (VI) source code for CWSR trap handler */
/* Version 18 + multiple trap handler */

// this performance-optimal version was originally from Seven Xu at SRDC

// Revison #18   --...
/* Rev History
** #1. Branch from gc dv.   //gfxip/gfx8/main/src/test/suites/block/cs/sr/cs_trap_handler.sp3#1,#50, #51, #52-53(Skip, Already Fixed by PV), #54-56(merged),#57-58(mergerd, skiped-already fixed by PV)
** #4. SR Memory Layout:
**             1. VGPR-SGPR-HWREG-{LDS}
**             2. tba_hi.bits.26 - reconfigured as the first wave in tg bits, for defer Save LDS for a threadgroup.. performance concern..
** #5. Update: 1. Accurate g8sr_ts_save_d timestamp
** #6. Update: 1. Fix s_barrier usage; 2. VGPR s/r using swizzle buffer?(NoNeed, already matched the swizzle pattern, more investigation)
** #7. Update: 1. don't barrier if noLDS
** #8. Branch: 1. Branch to ver#0, which is very similar to gc dv version
**             2. Fix SQ issue by s_sleep 2
** #9. Update: 1. Fix scc restore failed issue, restore wave_status at last
**             2. optimize s_buffer save by burst 16sgprs...
** #10. Update 1. Optimize restore sgpr by busrt 16 sgprs.
** #11. Update 1. Add 2 more timestamp for debug version
** #12. Update 1. Add VGPR SR using DWx4, some case improve and some case drop performance
** #13. Integ  1. Always use MUBUF for PV trap shader...
** #14. Update 1. s_buffer_store soft clause...
** #15. Update 1. PERF - sclar write with glc:0/mtype0 to allow L2 combine. perf improvement a lot.
** #16. Update 1. PRRF - UNROLL LDS_DMA got 2500cycle save in IP tree
** #17. Update 1. FUNC - LDS_DMA has issues while ATC, replace with ds_read/buffer_store for save part[TODO restore part]
**             2. PERF - Save LDS before save VGPR to cover LDS save long latency...
** #18. Update 1. FUNC - Implicitly estore STATUS.VCCZ, which is not writable by s_setreg_b32
**             2. FUNC - Handle non-CWSR traps
*/

var G8SR_WDMEM_HWREG_OFFSET = 0
var G8SR_WDMEM_SGPR_OFFSET  = 128  // in bytes

// Keep definition same as the app shader, These 2 time stamps are part of the app shader... Should before any Save and after restore.

var G8SR_DEBUG_TIMESTAMP = 0
var G8SR_DEBUG_TS_SAVE_D_OFFSET = 40*4  // ts_save_d timestamp offset relative to SGPR_SR_memory_offset
var s_g8sr_ts_save_s    = s[34:35]   // save start
var s_g8sr_ts_sq_save_msg  = s[36:37]   // The save shader send SAVEWAVE msg to spi
var s_g8sr_ts_spi_wrexec   = s[38:39]   // the SPI write the sr address to SQ
var s_g8sr_ts_save_d    = s[40:41]   // save end
var s_g8sr_ts_restore_s = s[42:43]   // restore start
var s_g8sr_ts_restore_d = s[44:45]   // restore end

var G8SR_VGPR_SR_IN_DWX4 = 0
var G8SR_SAVE_BUF_RSRC_WORD1_STRIDE_DWx4 = 0x00100000    // DWx4 stride is 4*4Bytes
var G8SR_RESTORE_BUF_RSRC_WORD1_STRIDE_DWx4  = G8SR_SAVE_BUF_RSRC_WORD1_STRIDE_DWx4


/*************************************************************************/
/*                  control on how to run the shader                     */
/*************************************************************************/
//any hack that needs to be made to run this code in EMU (either because various EMU code are not ready or no compute save & restore in EMU run)
var EMU_RUN_HACK                    =   0
var EMU_RUN_HACK_RESTORE_NORMAL     =   0
var EMU_RUN_HACK_SAVE_NORMAL_EXIT   =   0
var EMU_RUN_HACK_SAVE_SINGLE_WAVE   =   0
var EMU_RUN_HACK_SAVE_FIRST_TIME    =   0                   //for interrupted restore in which the first save is through EMU_RUN_HACK
var EMU_RUN_HACK_SAVE_FIRST_TIME_TBA_LO =   0                   //for interrupted restore in which the first save is through EMU_RUN_HACK
var EMU_RUN_HACK_SAVE_FIRST_TIME_TBA_HI =   0                   //for interrupted restore in which the first save is through EMU_RUN_HACK
var SAVE_LDS                        =   1
var WG_BASE_ADDR_LO                 =   0x9000a000
var WG_BASE_ADDR_HI                 =   0x0
var WAVE_SPACE                      =   0x5000              //memory size that each wave occupies in workgroup state mem
var CTX_SAVE_CONTROL                =   0x0
var CTX_RESTORE_CONTROL             =   CTX_SAVE_CONTROL
var SIM_RUN_HACK                    =   0                   //any hack that needs to be made to run this code in SIM (either because various RTL code are not ready or no compute save & restore in RTL run)
var SGPR_SAVE_USE_SQC               =   1                   //use SQC D$ to do the write
var USE_MTBUF_INSTEAD_OF_MUBUF      =   0                   //because TC EMU currently asserts on 0 of // overload DFMT field to carry 4 more bits of stride for MUBUF opcodes
var SWIZZLE_EN                      =   0                   //whether we use swizzled buffer addressing

/**************************************************************************/
/*                      variables                                         */
/**************************************************************************/
var SQ_WAVE_STATUS_INST_ATC_SHIFT  = 23
var SQ_WAVE_STATUS_INST_ATC_MASK   = 0x00800000
var SQ_WAVE_STATUS_SPI_PRIO_SHIFT  = 1
var SQ_WAVE_STATUS_SPI_PRIO_MASK   = 0x00000006
var SQ_WAVE_STATUS_PRE_SPI_PRIO_SHIFT   = 0
var SQ_WAVE_STATUS_PRE_SPI_PRIO_SIZE    = 1
var SQ_WAVE_STATUS_POST_SPI_PRIO_SHIFT  = 3
var SQ_WAVE_STATUS_POST_SPI_PRIO_SIZE   = 29

var SQ_WAVE_LDS_ALLOC_LDS_SIZE_SHIFT    = 12
var SQ_WAVE_LDS_ALLOC_LDS_SIZE_SIZE     = 9
var SQ_WAVE_GPR_ALLOC_VGPR_SIZE_SHIFT   = 8
var SQ_WAVE_GPR_ALLOC_VGPR_SIZE_SIZE    = 6
var SQ_WAVE_GPR_ALLOC_SGPR_SIZE_SHIFT   = 24
var SQ_WAVE_GPR_ALLOC_SGPR_SIZE_SIZE    = 3                     //FIXME  sq.blk still has 4 bits at this time while SQ programming guide has 3 bits

var SQ_WAVE_TRAPSTS_SAVECTX_MASK    =   0x400
var SQ_WAVE_TRAPSTS_EXCE_MASK       =   0x1FF                   // Exception mask
var SQ_WAVE_TRAPSTS_SAVECTX_SHIFT   =   10
var SQ_WAVE_TRAPSTS_MEM_VIOL_MASK   =   0x100
var SQ_WAVE_TRAPSTS_MEM_VIOL_SHIFT  =   8
var SQ_WAVE_TRAPSTS_PRE_SAVECTX_MASK    =   0x3FF
var SQ_WAVE_TRAPSTS_PRE_SAVECTX_SHIFT   =   0x0
var SQ_WAVE_TRAPSTS_PRE_SAVECTX_SIZE    =   10
var SQ_WAVE_TRAPSTS_POST_SAVECTX_MASK   =   0xFFFFF800
var SQ_WAVE_TRAPSTS_POST_SAVECTX_SHIFT  =   11
var SQ_WAVE_TRAPSTS_POST_SAVECTX_SIZE   =   21

var SQ_WAVE_IB_STS_RCNT_SHIFT           =   16                  //FIXME
var SQ_WAVE_IB_STS_RCNT_SIZE            =   4                   //FIXME
var SQ_WAVE_IB_STS_FIRST_REPLAY_SHIFT   =   15                  //FIXME
var SQ_WAVE_IB_STS_FIRST_REPLAY_SIZE    =   1                   //FIXME
var SQ_WAVE_IB_STS_RCNT_FIRST_REPLAY_MASK_NEG   = 0x00007FFF    //FIXME

var SQ_BUF_RSRC_WORD1_ATC_SHIFT     =   24
var SQ_BUF_RSRC_WORD3_MTYPE_SHIFT   =   27


/*      Save        */
var S_SAVE_BUF_RSRC_WORD1_STRIDE        =   0x00040000          //stride is 4 bytes
var S_SAVE_BUF_RSRC_WORD3_MISC          =   0x00807FAC          //SQ_SEL_X/Y/Z/W, BUF_NUM_FORMAT_FLOAT, (0 for MUBUF stride[17:14] when ADD_TID_ENABLE and BUF_DATA_FORMAT_32 for MTBUF), ADD_TID_ENABLE

var S_SAVE_SPI_INIT_ATC_MASK            =   0x08000000          //bit[27]: ATC bit
var S_SAVE_SPI_INIT_ATC_SHIFT           =   27
var S_SAVE_SPI_INIT_MTYPE_MASK          =   0x70000000          //bit[30:28]: Mtype
var S_SAVE_SPI_INIT_MTYPE_SHIFT         =   28
var S_SAVE_SPI_INIT_FIRST_WAVE_MASK     =   0x04000000          //bit[26]: FirstWaveInTG
var S_SAVE_SPI_INIT_FIRST_WAVE_SHIFT    =   26

var S_SAVE_PC_HI_RCNT_SHIFT             =   28                  //FIXME  check with Brian to ensure all fields other than PC[47:0] can be used
var S_SAVE_PC_HI_RCNT_MASK              =   0xF0000000          //FIXME
var S_SAVE_PC_HI_FIRST_REPLAY_SHIFT     =   27                  //FIXME
var S_SAVE_PC_HI_FIRST_REPLAY_MASK      =   0x08000000          //FIXME

var s_save_spi_init_lo              =   exec_lo
var s_save_spi_init_hi              =   exec_hi

                                                //tba_lo and tba_hi need to be saved/restored
var s_save_pc_lo            =   ttmp0           //{TTMP1, TTMP0} = {3'h0,pc_rewind[3:0], HT[0],trapID[7:0], PC[47:0]}
var s_save_pc_hi            =   ttmp1
var s_save_exec_lo          =   ttmp2
var s_save_exec_hi          =   ttmp3
var s_save_status           =   ttmp4
var s_save_trapsts          =   ttmp5           //not really used until the end of the SAVE routine
var s_save_xnack_mask_lo    =   ttmp6
var s_save_xnack_mask_hi    =   ttmp7
var s_save_buf_rsrc0        =   ttmp8
var s_save_buf_rsrc1        =   ttmp9
var s_save_buf_rsrc2        =   ttmp10
var s_save_buf_rsrc3        =   ttmp11

var s_save_mem_offset       =   tma_lo
var s_save_alloc_size       =   s_save_trapsts          //conflict
var s_save_tmp              =   s_save_buf_rsrc2        //shared with s_save_buf_rsrc2  (conflict: should not use mem access with s_save_tmp at the same time)
var s_save_m0               =   tma_hi

/*      Restore     */
var S_RESTORE_BUF_RSRC_WORD1_STRIDE         =   S_SAVE_BUF_RSRC_WORD1_STRIDE
var S_RESTORE_BUF_RSRC_WORD3_MISC           =   S_SAVE_BUF_RSRC_WORD3_MISC

var S_RESTORE_SPI_INIT_ATC_MASK             =   0x08000000          //bit[27]: ATC bit
var S_RESTORE_SPI_INIT_ATC_SHIFT            =   27
var S_RESTORE_SPI_INIT_MTYPE_MASK           =   0x70000000          //bit[30:28]: Mtype
var S_RESTORE_SPI_INIT_MTYPE_SHIFT          =   28
var S_RESTORE_SPI_INIT_FIRST_WAVE_MASK      =   0x04000000          //bit[26]: FirstWaveInTG
var S_RESTORE_SPI_INIT_FIRST_WAVE_SHIFT     =   26

var S_RESTORE_PC_HI_RCNT_SHIFT              =   S_SAVE_PC_HI_RCNT_SHIFT
var S_RESTORE_PC_HI_RCNT_MASK               =   S_SAVE_PC_HI_RCNT_MASK
var S_RESTORE_PC_HI_FIRST_REPLAY_SHIFT      =   S_SAVE_PC_HI_FIRST_REPLAY_SHIFT
var S_RESTORE_PC_HI_FIRST_REPLAY_MASK       =   S_SAVE_PC_HI_FIRST_REPLAY_MASK

var s_restore_spi_init_lo                   =   exec_lo
var s_restore_spi_init_hi                   =   exec_hi

var s_restore_mem_offset        =   ttmp2
var s_restore_alloc_size        =   ttmp3
var s_restore_tmp               =   ttmp6               //tba_lo/hi need to be restored
var s_restore_mem_offset_save   =   s_restore_tmp       //no conflict

var s_restore_m0            =   s_restore_alloc_size    //no conflict

var s_restore_mode          =   ttmp7

var s_restore_pc_lo         =   ttmp0
var s_restore_pc_hi         =   ttmp1
var s_restore_exec_lo       =   tma_lo                  //no conflict
var s_restore_exec_hi       =   tma_hi                  //no conflict
var s_restore_status        =   ttmp4
var s_restore_trapsts       =   ttmp5
var s_restore_xnack_mask_lo =   xnack_mask_lo
var s_restore_xnack_mask_hi =   xnack_mask_hi
var s_restore_buf_rsrc0     =   ttmp8
var s_restore_buf_rsrc1     =   ttmp9
var s_restore_buf_rsrc2     =   ttmp10
var s_restore_buf_rsrc3     =   ttmp11

/**************************************************************************/
/*                      trap handler entry points                         */
/**************************************************************************/
/* Shader Main*/

shader main
  asic(VI)
  type(CS)


    if ((EMU_RUN_HACK) && (!EMU_RUN_HACK_RESTORE_NORMAL))                   //hack to use trap_id for determining save/restore
        //FIXME VCCZ un-init assertion s_getreg_b32     s_save_status, hwreg(HW_REG_STATUS)         //save STATUS since we will change SCC
        s_and_b32 s_save_tmp, s_save_pc_hi, 0xffff0000              //change SCC
        s_cmp_eq_u32 s_save_tmp, 0x007e0000                         //Save: trap_id = 0x7e. Restore: trap_id = 0x7f.
        s_cbranch_scc0 L_JUMP_TO_RESTORE                            //do not need to recover STATUS here  since we are going to RESTORE
        //FIXME  s_setreg_b32   hwreg(HW_REG_STATUS),   s_save_status       //need to recover STATUS since we are going to SAVE
        s_branch L_SKIP_RESTORE                                     //NOT restore, SAVE actually
    else
        s_branch L_SKIP_RESTORE                                     //NOT restore. might be a regular trap or save
    end

L_JUMP_TO_RESTORE:
    s_branch L_RESTORE                                              //restore

L_SKIP_RESTORE:

    s_getreg_b32    s_save_status, hwreg(HW_REG_STATUS)                             //save STATUS since we will change SCC
    s_andn2_b32     s_save_status, s_save_status, SQ_WAVE_STATUS_SPI_PRIO_MASK      //check whether this is for save
    s_getreg_b32    s_save_trapsts, hwreg(HW_REG_TRAPSTS)
    s_and_b32       s_save_trapsts, s_save_trapsts, SQ_WAVE_TRAPSTS_SAVECTX_MASK    //check whether this is for save
    s_cbranch_scc1  L_SAVE                                      //this is the operation for save

    // *********    Handle non-CWSR traps       *******************
if (!EMU_RUN_HACK)
    /* read tba and tma for next level trap handler, ttmp4 is used as s_save_status */
    s_load_dwordx4  [ttmp8,ttmp9,ttmp10, ttmp11], [tma_lo,tma_hi], 0
    s_waitcnt lgkmcnt(0)
    s_or_b32        ttmp7, ttmp8, ttmp9
    s_cbranch_scc0  L_NO_NEXT_TRAP //next level trap handler not been set
    set_status_without_spi_prio(s_save_status, ttmp2) //restore HW status(SCC)
    s_setpc_b64     [ttmp8,ttmp9] //jump to next level trap handler

L_NO_NEXT_TRAP:
    s_getreg_b32    s_save_trapsts, hwreg(HW_REG_TRAPSTS)
    s_and_b32       s_save_trapsts, s_save_trapsts, SQ_WAVE_TRAPSTS_EXCE_MASK // Check whether it is an exception
    s_cbranch_scc1  L_EXCP_CASE   // Exception, jump back to the shader program directly.
    s_add_u32       ttmp0, ttmp0, 4   // S_TRAP case, add 4 to ttmp0
    s_addc_u32  ttmp1, ttmp1, 0
L_EXCP_CASE:
    s_and_b32   ttmp1, ttmp1, 0xFFFF
    set_status_without_spi_prio(s_save_status, ttmp2) //restore HW status(SCC)
    s_rfe_b64       [ttmp0, ttmp1]
end
    // *********        End handling of non-CWSR traps   *******************

/**************************************************************************/
/*                      save routine                                      */
/**************************************************************************/

L_SAVE:

if G8SR_DEBUG_TIMESTAMP
        s_memrealtime   s_g8sr_ts_save_s
        s_waitcnt lgkmcnt(0)         //FIXME, will cause xnack??
end

    //check whether there is mem_viol
    s_getreg_b32    s_save_trapsts, hwreg(HW_REG_TRAPSTS)
    s_and_b32   s_save_trapsts, s_save_trapsts, SQ_WAVE_TRAPSTS_MEM_VIOL_MASK
    s_cbranch_scc0  L_NO_PC_REWIND

    //if so, need rewind PC assuming GDS operation gets NACKed
    s_mov_b32       s_save_tmp, 0                                                           //clear mem_viol bit
    s_setreg_b32    hwreg(HW_REG_TRAPSTS, SQ_WAVE_TRAPSTS_MEM_VIOL_SHIFT, 1), s_save_tmp    //clear mem_viol bit
    s_and_b32       s_save_pc_hi, s_save_pc_hi, 0x0000ffff    //pc[47:32]
    s_sub_u32       s_save_pc_lo, s_save_pc_lo, 8             //pc[31:0]-8
    s_subb_u32      s_save_pc_hi, s_save_pc_hi, 0x0           // -scc

L_NO_PC_REWIND:
    s_mov_b32       s_save_tmp, 0                                                           //clear saveCtx bit
    s_setreg_b32    hwreg(HW_REG_TRAPSTS, SQ_WAVE_TRAPSTS_SAVECTX_SHIFT, 1), s_save_tmp     //clear saveCtx bit

    s_mov_b32       s_save_xnack_mask_lo,   xnack_mask_lo                                   //save XNACK_MASK
    s_mov_b32       s_save_xnack_mask_hi,   xnack_mask_hi    //save XNACK must before any memory operation
    s_getreg_b32    s_save_tmp, hwreg(HW_REG_IB_STS, SQ_WAVE_IB_STS_RCNT_SHIFT, SQ_WAVE_IB_STS_RCNT_SIZE)                   //save RCNT
    s_lshl_b32      s_save_tmp, s_save_tmp, S_SAVE_PC_HI_RCNT_SHIFT
    s_or_b32        s_save_pc_hi, s_save_pc_hi, s_save_tmp
    s_getreg_b32    s_save_tmp, hwreg(HW_REG_IB_STS, SQ_WAVE_IB_STS_FIRST_REPLAY_SHIFT, SQ_WAVE_IB_STS_FIRST_REPLAY_SIZE)   //save FIRST_REPLAY
    s_lshl_b32      s_save_tmp, s_save_tmp, S_SAVE_PC_HI_FIRST_REPLAY_SHIFT
    s_or_b32        s_save_pc_hi, s_save_pc_hi, s_save_tmp
    s_getreg_b32    s_save_tmp, hwreg(HW_REG_IB_STS)                                        //clear RCNT and FIRST_REPLAY in IB_STS
    s_and_b32       s_save_tmp, s_save_tmp, SQ_WAVE_IB_STS_RCNT_FIRST_REPLAY_MASK_NEG

    s_setreg_b32    hwreg(HW_REG_IB_STS), s_save_tmp

    /*      inform SPI the readiness and wait for SPI's go signal */
    s_mov_b32       s_save_exec_lo, exec_lo                                                 //save EXEC and use EXEC for the go signal from SPI
    s_mov_b32       s_save_exec_hi, exec_hi
    s_mov_b64       exec,   0x0                                                             //clear EXEC to get ready to receive

if G8SR_DEBUG_TIMESTAMP
        s_memrealtime  s_g8sr_ts_sq_save_msg
        s_waitcnt lgkmcnt(0)
end

    if (EMU_RUN_HACK)

    else
        s_sendmsg   sendmsg(MSG_SAVEWAVE)  //send SPI a message and wait for SPI's write to EXEC
    end

    // Set SPI_PRIO=2 to avoid starving instruction fetch in the waves we're waiting for.
    s_or_b32 s_save_tmp, s_save_status, (2 << SQ_WAVE_STATUS_SPI_PRIO_SHIFT)
    s_setreg_b32 hwreg(HW_REG_STATUS), s_save_tmp

  L_SLEEP:
    s_sleep 0x2                // sleep 1 (64clk) is not enough for 8 waves per SIMD, which will cause SQ hang, since the 7,8th wave could not get arbit to exec inst, while other waves are stuck into the sleep-loop and waiting for wrexec!=0

    if (EMU_RUN_HACK)

    else
        s_cbranch_execz L_SLEEP
    end

if G8SR_DEBUG_TIMESTAMP
        s_memrealtime  s_g8sr_ts_spi_wrexec
        s_waitcnt lgkmcnt(0)
end

    /*      setup Resource Contants    */
    if ((EMU_RUN_HACK) && (!EMU_RUN_HACK_SAVE_SINGLE_WAVE))
        //calculate wd_addr using absolute thread id
        v_readlane_b32 s_save_tmp, v9, 0
        s_lshr_b32 s_save_tmp, s_save_tmp, 6
        s_mul_i32 s_save_tmp, s_save_tmp, WAVE_SPACE
        s_add_i32 s_save_spi_init_lo, s_save_tmp, WG_BASE_ADDR_LO
        s_mov_b32 s_save_spi_init_hi, WG_BASE_ADDR_HI
        s_and_b32 s_save_spi_init_hi, s_save_spi_init_hi, CTX_SAVE_CONTROL
    else
    end
    if ((EMU_RUN_HACK) && (EMU_RUN_HACK_SAVE_SINGLE_WAVE))
        s_add_i32 s_save_spi_init_lo, s_save_tmp, WG_BASE_ADDR_LO
        s_mov_b32 s_save_spi_init_hi, WG_BASE_ADDR_HI
        s_and_b32 s_save_spi_init_hi, s_save_spi_init_hi, CTX_SAVE_CONTROL
    else
    end


    s_mov_b32       s_save_buf_rsrc0,   s_save_spi_init_lo                                                      //base_addr_lo
    s_and_b32       s_save_buf_rsrc1,   s_save_spi_init_hi, 0x0000FFFF                                          //base_addr_hi
    s_or_b32        s_save_buf_rsrc1,   s_save_buf_rsrc1,  S_SAVE_BUF_RSRC_WORD1_STRIDE
    s_mov_b32       s_save_buf_rsrc2,   0                                                                       //NUM_RECORDS initial value = 0 (in bytes) although not neccessarily inited
    s_mov_b32       s_save_buf_rsrc3,   S_SAVE_BUF_RSRC_WORD3_MISC
    s_and_b32       s_save_tmp,         s_save_spi_init_hi, S_SAVE_SPI_INIT_ATC_MASK
    s_lshr_b32      s_save_tmp,         s_save_tmp, (S_SAVE_SPI_INIT_ATC_SHIFT-SQ_BUF_RSRC_WORD1_ATC_SHIFT)         //get ATC bit into position
    s_or_b32        s_save_buf_rsrc3,   s_save_buf_rsrc3,  s_save_tmp                                           //or ATC
    s_and_b32       s_save_tmp,         s_save_spi_init_hi, S_SAVE_SPI_INIT_MTYPE_MASK
    s_lshr_b32      s_save_tmp,         s_save_tmp, (S_SAVE_SPI_INIT_MTYPE_SHIFT-SQ_BUF_RSRC_WORD3_MTYPE_SHIFT)     //get MTYPE bits into position
    s_or_b32        s_save_buf_rsrc3,   s_save_buf_rsrc3,  s_save_tmp                                           //or MTYPE

    //FIXME  right now s_save_m0/s_save_mem_offset use tma_lo/tma_hi  (might need to save them before using them?)
    s_mov_b32       s_save_m0,          m0                                                                  //save M0

    /*      global mem offset           */
    s_mov_b32       s_save_mem_offset,  0x0                                                                     //mem offset initial value = 0




    /*      save HW registers   */
    //////////////////////////////

  L_SAVE_HWREG:
        // HWREG SR memory offset : size(VGPR)+size(SGPR)
       get_vgpr_size_bytes(s_save_mem_offset)
       get_sgpr_size_bytes(s_save_tmp)
       s_add_u32 s_save_mem_offset, s_save_mem_offset, s_save_tmp


    s_mov_b32       s_save_buf_rsrc2, 0x4                               //NUM_RECORDS   in bytes
    if (SWIZZLE_EN)
        s_add_u32       s_save_buf_rsrc2, s_save_buf_rsrc2, 0x0                     //FIXME need to use swizzle to enable bounds checking?
    else
        s_mov_b32       s_save_buf_rsrc2,  0x1000000                                //NUM_RECORDS in bytes
    end


    write_hwreg_to_mem(s_save_m0, s_save_buf_rsrc0, s_save_mem_offset)                  //M0

    if ((EMU_RUN_HACK) && (EMU_RUN_HACK_SAVE_FIRST_TIME))
        s_add_u32 s_save_pc_lo, s_save_pc_lo, 4             //pc[31:0]+4
        s_addc_u32 s_save_pc_hi, s_save_pc_hi, 0x0          //carry bit over
        s_mov_b32   tba_lo, EMU_RUN_HACK_SAVE_FIRST_TIME_TBA_LO
        s_mov_b32   tba_hi, EMU_RUN_HACK_SAVE_FIRST_TIME_TBA_HI
    end

    write_hwreg_to_mem(s_save_pc_lo, s_save_buf_rsrc0, s_save_mem_offset)                   //PC
    write_hwreg_to_mem(s_save_pc_hi, s_save_buf_rsrc0, s_save_mem_offset)
    write_hwreg_to_mem(s_save_exec_lo, s_save_buf_rsrc0, s_save_mem_offset)             //EXEC
    write_hwreg_to_mem(s_save_exec_hi, s_save_buf_rsrc0, s_save_mem_offset)
    write_hwreg_to_mem(s_save_status, s_save_buf_rsrc0, s_save_mem_offset)              //STATUS

    //s_save_trapsts conflicts with s_save_alloc_size
    s_getreg_b32    s_save_trapsts, hwreg(HW_REG_TRAPSTS)
    write_hwreg_to_mem(s_save_trapsts, s_save_buf_rsrc0, s_save_mem_offset)             //TRAPSTS

    write_hwreg_to_mem(s_save_xnack_mask_lo, s_save_buf_rsrc0, s_save_mem_offset)           //XNACK_MASK_LO
    write_hwreg_to_mem(s_save_xnack_mask_hi, s_save_buf_rsrc0, s_save_mem_offset)           //XNACK_MASK_HI

    //use s_save_tmp would introduce conflict here between s_save_tmp and s_save_buf_rsrc2
    s_getreg_b32    s_save_m0, hwreg(HW_REG_MODE)                                                   //MODE
    write_hwreg_to_mem(s_save_m0, s_save_buf_rsrc0, s_save_mem_offset)
    write_hwreg_to_mem(tba_lo, s_save_buf_rsrc0, s_save_mem_offset)                     //TBA_LO
    write_hwreg_to_mem(tba_hi, s_save_buf_rsrc0, s_save_mem_offset)                     //TBA_HI



    /*      the first wave in the threadgroup    */
        // save fist_wave bits in tba_hi unused bit.26
    s_and_b32       s_save_tmp, s_save_spi_init_hi, S_SAVE_SPI_INIT_FIRST_WAVE_MASK     // extract fisrt wave bit
    //s_or_b32        tba_hi, s_save_tmp, tba_hi                                        // save first wave bit to tba_hi.bits[26]
    s_mov_b32        s_save_exec_hi, 0x0
    s_or_b32         s_save_exec_hi, s_save_tmp, s_save_exec_hi                          // save first wave bit to s_save_exec_hi.bits[26]


    /*          save SGPRs      */
        // Save SGPR before LDS save, then the s0 to s4 can be used during LDS save...
    //////////////////////////////

    // SGPR SR memory offset : size(VGPR)
    get_vgpr_size_bytes(s_save_mem_offset)
    // TODO, change RSRC word to rearrange memory layout for SGPRS

    s_getreg_b32    s_save_alloc_size, hwreg(HW_REG_GPR_ALLOC,SQ_WAVE_GPR_ALLOC_SGPR_SIZE_SHIFT,SQ_WAVE_GPR_ALLOC_SGPR_SIZE_SIZE)               //spgr_size
    s_add_u32       s_save_alloc_size, s_save_alloc_size, 1
    s_lshl_b32      s_save_alloc_size, s_save_alloc_size, 4                         //Number of SGPRs = (sgpr_size + 1) * 16   (non-zero value)

    if (SGPR_SAVE_USE_SQC)
        s_lshl_b32      s_save_buf_rsrc2,   s_save_alloc_size, 2                    //NUM_RECORDS in bytes
    else
        s_lshl_b32      s_save_buf_rsrc2,   s_save_alloc_size, 8                    //NUM_RECORDS in bytes (64 threads)
    end

    if (SWIZZLE_EN)
        s_add_u32       s_save_buf_rsrc2, s_save_buf_rsrc2, 0x0                     //FIXME need to use swizzle to enable bounds checking?
    else
        s_mov_b32       s_save_buf_rsrc2,  0x1000000                                //NUM_RECORDS in bytes
    end


    // backup s_save_buf_rsrc0,1 to s_save_pc_lo/hi, since write_16sgpr_to_mem function will change the rsrc0
    //s_mov_b64 s_save_pc_lo, s_save_buf_rsrc0
    s_mov_b64 s_save_xnack_mask_lo, s_save_buf_rsrc0
    s_add_u32 s_save_buf_rsrc0, s_save_buf_rsrc0, s_save_mem_offset
    s_addc_u32 s_save_buf_rsrc1, s_save_buf_rsrc1, 0

    s_mov_b32       m0, 0x0                         //SGPR initial index value =0
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
    s_add_u32       m0, m0, 16                                                      //next sgpr index
    s_cmp_lt_u32    m0, s_save_alloc_size                                           //scc = (m0 < s_save_alloc_size) ? 1 : 0
    s_cbranch_scc1  L_SAVE_SGPR_LOOP                                    //SGPR save is complete?
    // restore s_save_buf_rsrc0,1
    //s_mov_b64 s_save_buf_rsrc0, s_save_pc_lo
    s_mov_b64 s_save_buf_rsrc0, s_save_xnack_mask_lo




    /*          save first 4 VGPR, then LDS save could use   */
        // each wave will alloc 4 vgprs at least...
    /////////////////////////////////////////////////////////////////////////////////////

    s_mov_b32       s_save_mem_offset, 0
    s_mov_b32       exec_lo, 0xFFFFFFFF                                             //need every thread from now on
    s_mov_b32       exec_hi, 0xFFFFFFFF

    if (SWIZZLE_EN)
        s_add_u32       s_save_buf_rsrc2, s_save_buf_rsrc2, 0x0                     //FIXME need to use swizzle to enable bounds checking?
    else
        s_mov_b32       s_save_buf_rsrc2,  0x1000000                                //NUM_RECORDS in bytes
    end


    // VGPR Allocated in 4-GPR granularity

if G8SR_VGPR_SR_IN_DWX4
        // the const stride for DWx4 is 4*4 bytes
        s_and_b32 s_save_buf_rsrc1, s_save_buf_rsrc1, 0x0000FFFF   // reset const stride to 0
        s_or_b32  s_save_buf_rsrc1, s_save_buf_rsrc1, G8SR_SAVE_BUF_RSRC_WORD1_STRIDE_DWx4  // const stride to 4*4 bytes

        buffer_store_dwordx4 v0, v0, s_save_buf_rsrc0, s_save_mem_offset slc:1 glc:1

        s_and_b32 s_save_buf_rsrc1, s_save_buf_rsrc1, 0x0000FFFF   // reset const stride to 0
        s_or_b32  s_save_buf_rsrc1, s_save_buf_rsrc1, S_SAVE_BUF_RSRC_WORD1_STRIDE  // reset const stride to 4 bytes
else
        buffer_store_dword v0, v0, s_save_buf_rsrc0, s_save_mem_offset slc:1 glc:1
        buffer_store_dword v1, v0, s_save_buf_rsrc0, s_save_mem_offset slc:1 glc:1  offset:256
        buffer_store_dword v2, v0, s_save_buf_rsrc0, s_save_mem_offset slc:1 glc:1  offset:256*2
        buffer_store_dword v3, v0, s_save_buf_rsrc0, s_save_mem_offset slc:1 glc:1  offset:256*3
end



    /*          save LDS        */
    //////////////////////////////

  L_SAVE_LDS:

        // Change EXEC to all threads...
    s_mov_b32       exec_lo, 0xFFFFFFFF   //need every thread from now on
    s_mov_b32       exec_hi, 0xFFFFFFFF

    s_getreg_b32    s_save_alloc_size, hwreg(HW_REG_LDS_ALLOC,SQ_WAVE_LDS_ALLOC_LDS_SIZE_SHIFT,SQ_WAVE_LDS_ALLOC_LDS_SIZE_SIZE)             //lds_size
    s_and_b32       s_save_alloc_size, s_save_alloc_size, 0xFFFFFFFF                //lds_size is zero?
    s_cbranch_scc0  L_SAVE_LDS_DONE                                                                            //no lds used? jump to L_SAVE_DONE

    s_barrier               //LDS is used? wait for other waves in the same TG
    //s_and_b32     s_save_tmp, tba_hi, S_SAVE_SPI_INIT_FIRST_WAVE_MASK                //exec is still used here
    s_and_b32       s_save_tmp, s_save_exec_hi, S_SAVE_SPI_INIT_FIRST_WAVE_MASK                //exec is still used here
    s_cbranch_scc0  L_SAVE_LDS_DONE

        // first wave do LDS save;

    s_lshl_b32      s_save_alloc_size, s_save_alloc_size, 6                         //LDS size in dwords = lds_size * 64dw
    s_lshl_b32      s_save_alloc_size, s_save_alloc_size, 2                         //LDS size in bytes
    s_mov_b32       s_save_buf_rsrc2,  s_save_alloc_size                            //NUM_RECORDS in bytes

    // LDS at offset: size(VGPR)+SIZE(SGPR)+SIZE(HWREG)
    //
    get_vgpr_size_bytes(s_save_mem_offset)
    get_sgpr_size_bytes(s_save_tmp)
    s_add_u32  s_save_mem_offset, s_save_mem_offset, s_save_tmp
    s_add_u32 s_save_mem_offset, s_save_mem_offset, get_hwreg_size_bytes()


    if (SWIZZLE_EN)
        s_add_u32       s_save_buf_rsrc2, s_save_buf_rsrc2, 0x0       //FIXME need to use swizzle to enable bounds checking?
    else
        s_mov_b32       s_save_buf_rsrc2,  0x1000000                  //NUM_RECORDS in bytes
    end

    s_mov_b32       m0, 0x0                                               //lds_offset initial value = 0


var LDS_DMA_ENABLE = 0
var UNROLL = 0
if UNROLL==0 && LDS_DMA_ENABLE==1
        s_mov_b32  s3, 256*2
        s_nop 0
        s_nop 0
        s_nop 0
  L_SAVE_LDS_LOOP:
        //TODO: looks the 2 buffer_store/load clause for s/r will hurt performance.???
    if (SAVE_LDS)     //SPI always alloc LDS space in 128DW granularity
            buffer_store_lds_dword s_save_buf_rsrc0, s_save_mem_offset lds:1            // first 64DW
            buffer_store_lds_dword s_save_buf_rsrc0, s_save_mem_offset lds:1 offset:256 // second 64DW
    end

    s_add_u32       m0, m0, s3                                          //every buffer_store_lds does 256 bytes
    s_add_u32       s_save_mem_offset, s_save_mem_offset, s3                            //mem offset increased by 256 bytes
    s_cmp_lt_u32    m0, s_save_alloc_size                                               //scc=(m0 < s_save_alloc_size) ? 1 : 0
    s_cbranch_scc1  L_SAVE_LDS_LOOP                                                     //LDS save is complete?

elsif LDS_DMA_ENABLE==1 && UNROLL==1 // UNROOL  , has ichace miss
      // store from higest LDS address to lowest
      s_mov_b32  s3, 256*2
      s_sub_u32  m0, s_save_alloc_size, s3
      s_add_u32 s_save_mem_offset, s_save_mem_offset, m0
      s_lshr_b32 s_save_alloc_size, s_save_alloc_size, 9   // how many 128 trunks...
      s_sub_u32 s_save_alloc_size, 128, s_save_alloc_size   // store from higheset addr to lowest
      s_mul_i32 s_save_alloc_size, s_save_alloc_size, 6*4   // PC offset increment,  each LDS save block cost 6*4 Bytes instruction
      s_add_u32 s_save_alloc_size, s_save_alloc_size, 3*4   //2is the below 2 inst...//s_addc and s_setpc
      s_nop 0
      s_nop 0
      s_nop 0   //pad 3 dw to let LDS_DMA align with 64Bytes
      s_getpc_b64 s[0:1]                              // reuse s[0:1], since s[0:1] already saved
      s_add_u32   s0, s0,s_save_alloc_size
      s_addc_u32  s1, s1, 0
      s_setpc_b64 s[0:1]


       for var i =0; i< 128; i++
            // be careful to make here a 64Byte aligned address, which could improve performance...
            buffer_store_lds_dword s_save_buf_rsrc0, s_save_mem_offset lds:1 offset:0           // first 64DW
            buffer_store_lds_dword s_save_buf_rsrc0, s_save_mem_offset lds:1 offset:256           // second 64DW

        if i!=127
        s_sub_u32  m0, m0, s3      // use a sgpr to shrink 2DW-inst to 1DW inst to improve performance , i.e.  pack more LDS_DMA inst to one Cacheline
            s_sub_u32  s_save_mem_offset, s_save_mem_offset,  s3
            end
       end

else   // BUFFER_STORE
      v_mbcnt_lo_u32_b32 v2, 0xffffffff, 0x0
      v_mbcnt_hi_u32_b32 v3, 0xffffffff, v2     // tid
      v_mul_i32_i24 v2, v3, 8   // tid*8
      v_mov_b32 v3, 256*2
      s_mov_b32 m0, 0x10000
      s_mov_b32 s0, s_save_buf_rsrc3
      s_and_b32 s_save_buf_rsrc3, s_save_buf_rsrc3, 0xFF7FFFFF    // disable add_tid
      s_or_b32 s_save_buf_rsrc3, s_save_buf_rsrc3, 0x58000   //DFMT

L_SAVE_LDS_LOOP_VECTOR:
      ds_read_b64 v[0:1], v2    //x =LDS[a], byte address
      s_waitcnt lgkmcnt(0)
      buffer_store_dwordx2  v[0:1], v2, s_save_buf_rsrc0, s_save_mem_offset offen:1  glc:1  slc:1
//      s_waitcnt vmcnt(0)
      v_add_u32 v2, vcc[0:1], v2, v3
      v_cmp_lt_u32 vcc[0:1], v2, s_save_alloc_size
      s_cbranch_vccnz L_SAVE_LDS_LOOP_VECTOR

      // restore rsrc3
      s_mov_b32 s_save_buf_rsrc3, s0

end

L_SAVE_LDS_DONE:


    /*          save VGPRs  - set the Rest VGPRs        */
    //////////////////////////////////////////////////////////////////////////////////////
  L_SAVE_VGPR:
    // VGPR SR memory offset: 0
    // TODO rearrange the RSRC words to use swizzle for VGPR save...

    s_mov_b32       s_save_mem_offset, (0+256*4)                                    // for the rest VGPRs
    s_mov_b32       exec_lo, 0xFFFFFFFF                                             //need every thread from now on
    s_mov_b32       exec_hi, 0xFFFFFFFF

    s_getreg_b32    s_save_alloc_size, hwreg(HW_REG_GPR_ALLOC,SQ_WAVE_GPR_ALLOC_VGPR_SIZE_SHIFT,SQ_WAVE_GPR_ALLOC_VGPR_SIZE_SIZE)                   //vpgr_size
    s_add_u32       s_save_alloc_size, s_save_alloc_size, 1
    s_lshl_b32      s_save_alloc_size, s_save_alloc_size, 2                         //Number of VGPRs = (vgpr_size + 1) * 4    (non-zero value)   //FIXME for GFX, zero is possible
    s_lshl_b32      s_save_buf_rsrc2,  s_save_alloc_size, 8                         //NUM_RECORDS in bytes (64 threads*4)
    if (SWIZZLE_EN)
        s_add_u32       s_save_buf_rsrc2, s_save_buf_rsrc2, 0x0                     //FIXME need to use swizzle to enable bounds checking?
    else
        s_mov_b32       s_save_buf_rsrc2,  0x1000000                                //NUM_RECORDS in bytes
    end


    // VGPR Allocated in 4-GPR granularity

if G8SR_VGPR_SR_IN_DWX4
        // the const stride for DWx4 is 4*4 bytes
        s_and_b32 s_save_buf_rsrc1, s_save_buf_rsrc1, 0x0000FFFF   // reset const stride to 0
        s_or_b32  s_save_buf_rsrc1, s_save_buf_rsrc1, G8SR_SAVE_BUF_RSRC_WORD1_STRIDE_DWx4  // const stride to 4*4 bytes

        s_mov_b32         m0, 4     // skip first 4 VGPRs
        s_cmp_lt_u32      m0, s_save_alloc_size
        s_cbranch_scc0    L_SAVE_VGPR_LOOP_END      // no more vgprs

        s_set_gpr_idx_on  m0, 0x1   // This will change M0
        s_add_u32         s_save_alloc_size, s_save_alloc_size, 0x1000  // because above inst change m0
L_SAVE_VGPR_LOOP:
        v_mov_b32         v0, v0   // v0 = v[0+m0]
        v_mov_b32         v1, v1
        v_mov_b32         v2, v2
        v_mov_b32         v3, v3


        buffer_store_dwordx4 v0, v0, s_save_buf_rsrc0, s_save_mem_offset slc:1 glc:1
        s_add_u32         m0, m0, 4
        s_add_u32         s_save_mem_offset, s_save_mem_offset, 256*4
        s_cmp_lt_u32      m0, s_save_alloc_size
    s_cbranch_scc1  L_SAVE_VGPR_LOOP                                                //VGPR save is complete?
    s_set_gpr_idx_off
L_SAVE_VGPR_LOOP_END:

        s_and_b32 s_save_buf_rsrc1, s_save_buf_rsrc1, 0x0000FFFF   // reset const stride to 0
        s_or_b32  s_save_buf_rsrc1, s_save_buf_rsrc1, S_SAVE_BUF_RSRC_WORD1_STRIDE  // reset const stride to 4 bytes
else
    // VGPR store using dw burst
    s_mov_b32         m0, 0x4   //VGPR initial index value =0
    s_cmp_lt_u32      m0, s_save_alloc_size
    s_cbranch_scc0    L_SAVE_VGPR_END


    s_set_gpr_idx_on    m0, 0x1 //M0[7:0] = M0[7:0] and M0[15:12] = 0x1
    s_add_u32       s_save_alloc_size, s_save_alloc_size, 0x1000                    //add 0x1000 since we compare m0 against it later

  L_SAVE_VGPR_LOOP:
    v_mov_b32       v0, v0              //v0 = v[0+m0]
    v_mov_b32       v1, v1              //v0 = v[0+m0]
    v_mov_b32       v2, v2              //v0 = v[0+m0]
    v_mov_b32       v3, v3              //v0 = v[0+m0]

    if(USE_MTBUF_INSTEAD_OF_MUBUF)
        tbuffer_store_format_x v0, v0, s_save_buf_rsrc0, s_save_mem_offset format:BUF_NUM_FORMAT_FLOAT format: BUF_DATA_FORMAT_32 slc:1 glc:1
    else
        buffer_store_dword v0, v0, s_save_buf_rsrc0, s_save_mem_offset slc:1 glc:1
        buffer_store_dword v1, v0, s_save_buf_rsrc0, s_save_mem_offset slc:1 glc:1  offset:256
        buffer_store_dword v2, v0, s_save_buf_rsrc0, s_save_mem_offset slc:1 glc:1  offset:256*2
        buffer_store_dword v3, v0, s_save_buf_rsrc0, s_save_mem_offset slc:1 glc:1  offset:256*3
    end

    s_add_u32       m0, m0, 4                                                       //next vgpr index
    s_add_u32       s_save_mem_offset, s_save_mem_offset, 256*4                     //every buffer_store_dword does 256 bytes
    s_cmp_lt_u32    m0, s_save_alloc_size                                           //scc = (m0 < s_save_alloc_size) ? 1 : 0
    s_cbranch_scc1  L_SAVE_VGPR_LOOP                                                //VGPR save is complete?
    s_set_gpr_idx_off
end

L_SAVE_VGPR_END:






    /*     S_PGM_END_SAVED  */                              //FIXME  graphics ONLY
    if ((EMU_RUN_HACK) && (!EMU_RUN_HACK_SAVE_NORMAL_EXIT))
        s_and_b32 s_save_pc_hi, s_save_pc_hi, 0x0000ffff    //pc[47:32]
        s_add_u32 s_save_pc_lo, s_save_pc_lo, 4             //pc[31:0]+4
        s_addc_u32 s_save_pc_hi, s_save_pc_hi, 0x0          //carry bit over
        s_rfe_b64 s_save_pc_lo                              //Return to the main shader program
    else
    end

// Save Done timestamp
if G8SR_DEBUG_TIMESTAMP
        s_memrealtime   s_g8sr_ts_save_d
        // SGPR SR memory offset : size(VGPR)
        get_vgpr_size_bytes(s_save_mem_offset)
        s_add_u32 s_save_mem_offset, s_save_mem_offset, G8SR_DEBUG_TS_SAVE_D_OFFSET
        s_waitcnt lgkmcnt(0)         //FIXME, will cause xnack??
        // Need reset rsrc2??
        s_mov_b32 m0, s_save_mem_offset
        s_mov_b32 s_save_buf_rsrc2,  0x1000000                                  //NUM_RECORDS in bytes
        s_buffer_store_dwordx2 s_g8sr_ts_save_d, s_save_buf_rsrc0, m0       glc:1
end


    s_branch    L_END_PGM



/**************************************************************************/
/*                      restore routine                                   */
/**************************************************************************/

L_RESTORE:
    /*      Setup Resource Contants    */
    if ((EMU_RUN_HACK) && (!EMU_RUN_HACK_RESTORE_NORMAL))
        //calculate wd_addr using absolute thread id
        v_readlane_b32 s_restore_tmp, v9, 0
        s_lshr_b32 s_restore_tmp, s_restore_tmp, 6
        s_mul_i32 s_restore_tmp, s_restore_tmp, WAVE_SPACE
        s_add_i32 s_restore_spi_init_lo, s_restore_tmp, WG_BASE_ADDR_LO
        s_mov_b32 s_restore_spi_init_hi, WG_BASE_ADDR_HI
        s_and_b32 s_restore_spi_init_hi, s_restore_spi_init_hi, CTX_RESTORE_CONTROL
    else
    end

if G8SR_DEBUG_TIMESTAMP
        s_memrealtime   s_g8sr_ts_restore_s
        s_waitcnt lgkmcnt(0)         //FIXME, will cause xnack??
        // tma_lo/hi are sgpr 110, 111, which will not used for 112 SGPR allocated case...
        s_mov_b32 s_restore_pc_lo, s_g8sr_ts_restore_s[0]
        s_mov_b32 s_restore_pc_hi, s_g8sr_ts_restore_s[1]   //backup ts to ttmp0/1, sicne exec will be finally restored..
end



    s_mov_b32       s_restore_buf_rsrc0,    s_restore_spi_init_lo                                                           //base_addr_lo
    s_and_b32       s_restore_buf_rsrc1,    s_restore_spi_init_hi, 0x0000FFFF                                               //base_addr_hi
    s_or_b32        s_restore_buf_rsrc1,    s_restore_buf_rsrc1,  S_RESTORE_BUF_RSRC_WORD1_STRIDE
    s_mov_b32       s_restore_buf_rsrc2,    0                                                                               //NUM_RECORDS initial value = 0 (in bytes)
    s_mov_b32       s_restore_buf_rsrc3,    S_RESTORE_BUF_RSRC_WORD3_MISC
    s_and_b32       s_restore_tmp,          s_restore_spi_init_hi, S_RESTORE_SPI_INIT_ATC_MASK
    s_lshr_b32      s_restore_tmp,          s_restore_tmp, (S_RESTORE_SPI_INIT_ATC_SHIFT-SQ_BUF_RSRC_WORD1_ATC_SHIFT)       //get ATC bit into position
    s_or_b32        s_restore_buf_rsrc3,    s_restore_buf_rsrc3,  s_restore_tmp                                             //or ATC
    s_and_b32       s_restore_tmp,          s_restore_spi_init_hi, S_RESTORE_SPI_INIT_MTYPE_MASK
    s_lshr_b32      s_restore_tmp,          s_restore_tmp, (S_RESTORE_SPI_INIT_MTYPE_SHIFT-SQ_BUF_RSRC_WORD3_MTYPE_SHIFT)   //get MTYPE bits into position
    s_or_b32        s_restore_buf_rsrc3,    s_restore_buf_rsrc3,  s_restore_tmp                                             //or MTYPE

    /*      global mem offset           */
//  s_mov_b32       s_restore_mem_offset, 0x0                               //mem offset initial value = 0

    /*      the first wave in the threadgroup    */
    s_and_b32       s_restore_tmp, s_restore_spi_init_hi, S_RESTORE_SPI_INIT_FIRST_WAVE_MASK
    s_cbranch_scc0  L_RESTORE_VGPR

    /*          restore LDS     */
    //////////////////////////////
  L_RESTORE_LDS:

    s_mov_b32       exec_lo, 0xFFFFFFFF                                                     //need every thread from now on   //be consistent with SAVE although can be moved ahead
    s_mov_b32       exec_hi, 0xFFFFFFFF

    s_getreg_b32    s_restore_alloc_size, hwreg(HW_REG_LDS_ALLOC,SQ_WAVE_LDS_ALLOC_LDS_SIZE_SHIFT,SQ_WAVE_LDS_ALLOC_LDS_SIZE_SIZE)              //lds_size
    s_and_b32       s_restore_alloc_size, s_restore_alloc_size, 0xFFFFFFFF                  //lds_size is zero?
    s_cbranch_scc0  L_RESTORE_VGPR                                                          //no lds used? jump to L_RESTORE_VGPR
    s_lshl_b32      s_restore_alloc_size, s_restore_alloc_size, 6                           //LDS size in dwords = lds_size * 64dw
    s_lshl_b32      s_restore_alloc_size, s_restore_alloc_size, 2                           //LDS size in bytes
    s_mov_b32       s_restore_buf_rsrc2,    s_restore_alloc_size                            //NUM_RECORDS in bytes

    // LDS at offset: size(VGPR)+SIZE(SGPR)+SIZE(HWREG)
    //
    get_vgpr_size_bytes(s_restore_mem_offset)
    get_sgpr_size_bytes(s_restore_tmp)
    s_add_u32  s_restore_mem_offset, s_restore_mem_offset, s_restore_tmp
    s_add_u32  s_restore_mem_offset, s_restore_mem_offset, get_hwreg_size_bytes()            //FIXME, Check if offset overflow???


    if (SWIZZLE_EN)
        s_add_u32       s_restore_buf_rsrc2, s_restore_buf_rsrc2, 0x0                       //FIXME need to use swizzle to enable bounds checking?
    else
        s_mov_b32       s_restore_buf_rsrc2,  0x1000000                                     //NUM_RECORDS in bytes
    end
    s_mov_b32       m0, 0x0                                                                 //lds_offset initial value = 0

  L_RESTORE_LDS_LOOP:
    if (SAVE_LDS)
        buffer_load_dword   v0, v0, s_restore_buf_rsrc0, s_restore_mem_offset lds:1                    // first 64DW
        buffer_load_dword   v0, v0, s_restore_buf_rsrc0, s_restore_mem_offset lds:1 offset:256         // second 64DW
    end
    s_add_u32       m0, m0, 256*2                                               // 128 DW
    s_add_u32       s_restore_mem_offset, s_restore_mem_offset, 256*2           //mem offset increased by 128DW
    s_cmp_lt_u32    m0, s_restore_alloc_size                                    //scc=(m0 < s_restore_alloc_size) ? 1 : 0
    s_cbranch_scc1  L_RESTORE_LDS_LOOP                                                      //LDS restore is complete?


    /*          restore VGPRs       */
    //////////////////////////////
  L_RESTORE_VGPR:
        // VGPR SR memory offset : 0
    s_mov_b32       s_restore_mem_offset, 0x0
    s_mov_b32       exec_lo, 0xFFFFFFFF                                                     //need every thread from now on   //be consistent with SAVE although can be moved ahead
    s_mov_b32       exec_hi, 0xFFFFFFFF

    s_getreg_b32    s_restore_alloc_size, hwreg(HW_REG_GPR_ALLOC,SQ_WAVE_GPR_ALLOC_VGPR_SIZE_SHIFT,SQ_WAVE_GPR_ALLOC_VGPR_SIZE_SIZE)    //vpgr_size
    s_add_u32       s_restore_alloc_size, s_restore_alloc_size, 1
    s_lshl_b32      s_restore_alloc_size, s_restore_alloc_size, 2                           //Number of VGPRs = (vgpr_size + 1) * 4    (non-zero value)
    s_lshl_b32      s_restore_buf_rsrc2,  s_restore_alloc_size, 8                           //NUM_RECORDS in bytes (64 threads*4)
    if (SWIZZLE_EN)
        s_add_u32       s_restore_buf_rsrc2, s_restore_buf_rsrc2, 0x0                       //FIXME need to use swizzle to enable bounds checking?
    else
        s_mov_b32       s_restore_buf_rsrc2,  0x1000000                                     //NUM_RECORDS in bytes
    end

if G8SR_VGPR_SR_IN_DWX4
     get_vgpr_size_bytes(s_restore_mem_offset)
     s_sub_u32         s_restore_mem_offset, s_restore_mem_offset, 256*4

     // the const stride for DWx4 is 4*4 bytes
     s_and_b32 s_restore_buf_rsrc1, s_restore_buf_rsrc1, 0x0000FFFF   // reset const stride to 0
     s_or_b32  s_restore_buf_rsrc1, s_restore_buf_rsrc1, G8SR_RESTORE_BUF_RSRC_WORD1_STRIDE_DWx4  // const stride to 4*4 bytes

     s_mov_b32         m0, s_restore_alloc_size
     s_set_gpr_idx_on  m0, 0x8    // Note.. This will change m0

L_RESTORE_VGPR_LOOP:
     buffer_load_dwordx4 v0, v0, s_restore_buf_rsrc0, s_restore_mem_offset slc:1 glc:1
     s_waitcnt vmcnt(0)
     s_sub_u32         m0, m0, 4
     v_mov_b32         v0, v0   // v[0+m0] = v0
     v_mov_b32         v1, v1
     v_mov_b32         v2, v2
     v_mov_b32         v3, v3
     s_sub_u32         s_restore_mem_offset, s_restore_mem_offset, 256*4
     s_cmp_eq_u32      m0, 0x8000
     s_cbranch_scc0    L_RESTORE_VGPR_LOOP
     s_set_gpr_idx_off

     s_and_b32 s_restore_buf_rsrc1, s_restore_buf_rsrc1, 0x0000FFFF   // reset const stride to 0
     s_or_b32  s_restore_buf_rsrc1, s_restore_buf_rsrc1, S_RESTORE_BUF_RSRC_WORD1_STRIDE  // const stride to 4*4 bytes

else
    // VGPR load using dw burst
    s_mov_b32       s_restore_mem_offset_save, s_restore_mem_offset     // restore start with v1, v0 will be the last
    s_add_u32       s_restore_mem_offset, s_restore_mem_offset, 256*4
    s_mov_b32       m0, 4                               //VGPR initial index value = 1
    s_set_gpr_idx_on  m0, 0x8                       //M0[7:0] = M0[7:0] and M0[15:12] = 0x8
    s_add_u32       s_restore_alloc_size, s_restore_alloc_size, 0x8000                      //add 0x8000 since we compare m0 against it later

  L_RESTORE_VGPR_LOOP:
    if(USE_MTBUF_INSTEAD_OF_MUBUF)
        tbuffer_load_format_x v0, v0, s_restore_buf_rsrc0, s_restore_mem_offset format:BUF_NUM_FORMAT_FLOAT format: BUF_DATA_FORMAT_32 slc:1 glc:1
    else
        buffer_load_dword v0, v0, s_restore_buf_rsrc0, s_restore_mem_offset slc:1 glc:1
        buffer_load_dword v1, v0, s_restore_buf_rsrc0, s_restore_mem_offset slc:1 glc:1 offset:256
        buffer_load_dword v2, v0, s_restore_buf_rsrc0, s_restore_mem_offset slc:1 glc:1 offset:256*2
        buffer_load_dword v3, v0, s_restore_buf_rsrc0, s_restore_mem_offset slc:1 glc:1 offset:256*3
    end
    s_waitcnt       vmcnt(0)                                                                //ensure data ready
    v_mov_b32       v0, v0                                                                  //v[0+m0] = v0
    v_mov_b32       v1, v1
    v_mov_b32       v2, v2
    v_mov_b32       v3, v3
    s_add_u32       m0, m0, 4                                                               //next vgpr index
    s_add_u32       s_restore_mem_offset, s_restore_mem_offset, 256*4                           //every buffer_load_dword does 256 bytes
    s_cmp_lt_u32    m0, s_restore_alloc_size                                                //scc = (m0 < s_restore_alloc_size) ? 1 : 0
    s_cbranch_scc1  L_RESTORE_VGPR_LOOP                                                     //VGPR restore (except v0) is complete?
    s_set_gpr_idx_off
                                                                                            /* VGPR restore on v0 */
    if(USE_MTBUF_INSTEAD_OF_MUBUF)
        tbuffer_load_format_x v0, v0, s_restore_buf_rsrc0, s_restore_mem_offset_save format:BUF_NUM_FORMAT_FLOAT format: BUF_DATA_FORMAT_32 slc:1 glc:1
    else
        buffer_load_dword v0, v0, s_restore_buf_rsrc0, s_restore_mem_offset_save    slc:1 glc:1
        buffer_load_dword v1, v0, s_restore_buf_rsrc0, s_restore_mem_offset_save    slc:1 glc:1 offset:256
        buffer_load_dword v2, v0, s_restore_buf_rsrc0, s_restore_mem_offset_save    slc:1 glc:1 offset:256*2
        buffer_load_dword v3, v0, s_restore_buf_rsrc0, s_restore_mem_offset_save    slc:1 glc:1 offset:256*3
    end

end

    /*          restore SGPRs       */
    //////////////////////////////

    // SGPR SR memory offset : size(VGPR)
    get_vgpr_size_bytes(s_restore_mem_offset)
    get_sgpr_size_bytes(s_restore_tmp)
    s_add_u32 s_restore_mem_offset, s_restore_mem_offset, s_restore_tmp
    s_sub_u32 s_restore_mem_offset, s_restore_mem_offset, 16*4     // restore SGPR from S[n] to S[0], by 16 sgprs group
    // TODO, change RSRC word to rearrange memory layout for SGPRS

    s_getreg_b32    s_restore_alloc_size, hwreg(HW_REG_GPR_ALLOC,SQ_WAVE_GPR_ALLOC_SGPR_SIZE_SHIFT,SQ_WAVE_GPR_ALLOC_SGPR_SIZE_SIZE)                //spgr_size
    s_add_u32       s_restore_alloc_size, s_restore_alloc_size, 1
    s_lshl_b32      s_restore_alloc_size, s_restore_alloc_size, 4                           //Number of SGPRs = (sgpr_size + 1) * 16   (non-zero value)

    if (SGPR_SAVE_USE_SQC)
        s_lshl_b32      s_restore_buf_rsrc2,    s_restore_alloc_size, 2                     //NUM_RECORDS in bytes
    else
        s_lshl_b32      s_restore_buf_rsrc2,    s_restore_alloc_size, 8                     //NUM_RECORDS in bytes (64 threads)
    end
    if (SWIZZLE_EN)
        s_add_u32       s_restore_buf_rsrc2, s_restore_buf_rsrc2, 0x0                       //FIXME need to use swizzle to enable bounds checking?
    else
        s_mov_b32       s_restore_buf_rsrc2,  0x1000000                                     //NUM_RECORDS in bytes
    end

    /* If 112 SGPRs ar allocated, 4 sgprs are not used TBA(108,109),TMA(110,111),
       However, we are safe to restore these 4 SGPRs anyway, since TBA,TMA will later be restored by HWREG
    */
    s_mov_b32 m0, s_restore_alloc_size

 L_RESTORE_SGPR_LOOP:
    read_16sgpr_from_mem(s0, s_restore_buf_rsrc0, s_restore_mem_offset)  //PV: further performance improvement can be made
    s_waitcnt       lgkmcnt(0)                                                              //ensure data ready

    s_sub_u32 m0, m0, 16    // Restore from S[n] to S[0]

    s_movreld_b64   s0, s0      //s[0+m0] = s0
    s_movreld_b64   s2, s2
    s_movreld_b64   s4, s4
    s_movreld_b64   s6, s6
    s_movreld_b64   s8, s8
    s_movreld_b64   s10, s10
    s_movreld_b64   s12, s12
    s_movreld_b64   s14, s14

    s_cmp_eq_u32    m0, 0               //scc = (m0 < s_restore_alloc_size) ? 1 : 0
    s_cbranch_scc0  L_RESTORE_SGPR_LOOP             //SGPR restore (except s0) is complete?

    /*      restore HW registers    */
    //////////////////////////////
  L_RESTORE_HWREG:


if G8SR_DEBUG_TIMESTAMP
      s_mov_b32 s_g8sr_ts_restore_s[0], s_restore_pc_lo
      s_mov_b32 s_g8sr_ts_restore_s[1], s_restore_pc_hi
end

    // HWREG SR memory offset : size(VGPR)+size(SGPR)
    get_vgpr_size_bytes(s_restore_mem_offset)
    get_sgpr_size_bytes(s_restore_tmp)
    s_add_u32 s_restore_mem_offset, s_restore_mem_offset, s_restore_tmp


    s_mov_b32       s_restore_buf_rsrc2, 0x4                                                //NUM_RECORDS   in bytes
    if (SWIZZLE_EN)
        s_add_u32       s_restore_buf_rsrc2, s_restore_buf_rsrc2, 0x0                       //FIXME need to use swizzle to enable bounds checking?
    else
        s_mov_b32       s_restore_buf_rsrc2,  0x1000000                                     //NUM_RECORDS in bytes
    end

    read_hwreg_from_mem(s_restore_m0, s_restore_buf_rsrc0, s_restore_mem_offset)                    //M0
    read_hwreg_from_mem(s_restore_pc_lo, s_restore_buf_rsrc0, s_restore_mem_offset)             //PC
    read_hwreg_from_mem(s_restore_pc_hi, s_restore_buf_rsrc0, s_restore_mem_offset)
    read_hwreg_from_mem(s_restore_exec_lo, s_restore_buf_rsrc0, s_restore_mem_offset)               //EXEC
    read_hwreg_from_mem(s_restore_exec_hi, s_restore_buf_rsrc0, s_restore_mem_offset)
    read_hwreg_from_mem(s_restore_status, s_restore_buf_rsrc0, s_restore_mem_offset)                //STATUS
    read_hwreg_from_mem(s_restore_trapsts, s_restore_buf_rsrc0, s_restore_mem_offset)               //TRAPSTS
    read_hwreg_from_mem(xnack_mask_lo, s_restore_buf_rsrc0, s_restore_mem_offset)                   //XNACK_MASK_LO
    read_hwreg_from_mem(xnack_mask_hi, s_restore_buf_rsrc0, s_restore_mem_offset)                   //XNACK_MASK_HI
    read_hwreg_from_mem(s_restore_mode, s_restore_buf_rsrc0, s_restore_mem_offset)              //MODE
    read_hwreg_from_mem(tba_lo, s_restore_buf_rsrc0, s_restore_mem_offset)                      //TBA_LO
    read_hwreg_from_mem(tba_hi, s_restore_buf_rsrc0, s_restore_mem_offset)                      //TBA_HI

    s_waitcnt       lgkmcnt(0)                                                                                      //from now on, it is safe to restore STATUS and IB_STS

    //for normal save & restore, the saved PC points to the next inst to execute, no adjustment needs to be made, otherwise:
    if ((EMU_RUN_HACK) && (!EMU_RUN_HACK_RESTORE_NORMAL))
        s_add_u32 s_restore_pc_lo, s_restore_pc_lo, 8            //pc[31:0]+8     //two back-to-back s_trap are used (first for save and second for restore)
        s_addc_u32  s_restore_pc_hi, s_restore_pc_hi, 0x0        //carry bit over
    end
    if ((EMU_RUN_HACK) && (EMU_RUN_HACK_RESTORE_NORMAL))
        s_add_u32 s_restore_pc_lo, s_restore_pc_lo, 4            //pc[31:0]+4     // save is hack through s_trap but restore is normal
        s_addc_u32  s_restore_pc_hi, s_restore_pc_hi, 0x0        //carry bit over
    end

    s_mov_b32       m0,         s_restore_m0
    s_mov_b32       exec_lo,    s_restore_exec_lo
    s_mov_b32       exec_hi,    s_restore_exec_hi

    s_and_b32       s_restore_m0, SQ_WAVE_TRAPSTS_PRE_SAVECTX_MASK, s_restore_trapsts
    s_setreg_b32    hwreg(HW_REG_TRAPSTS, SQ_WAVE_TRAPSTS_PRE_SAVECTX_SHIFT, SQ_WAVE_TRAPSTS_PRE_SAVECTX_SIZE), s_restore_m0
    s_and_b32       s_restore_m0, SQ_WAVE_TRAPSTS_POST_SAVECTX_MASK, s_restore_trapsts
    s_lshr_b32      s_restore_m0, s_restore_m0, SQ_WAVE_TRAPSTS_POST_SAVECTX_SHIFT
    s_setreg_b32    hwreg(HW_REG_TRAPSTS, SQ_WAVE_TRAPSTS_POST_SAVECTX_SHIFT, SQ_WAVE_TRAPSTS_POST_SAVECTX_SIZE), s_restore_m0
    //s_setreg_b32  hwreg(HW_REG_TRAPSTS),  s_restore_trapsts      //don't overwrite SAVECTX bit as it may be set through external SAVECTX during restore
    s_setreg_b32    hwreg(HW_REG_MODE),     s_restore_mode
    //reuse s_restore_m0 as a temp register
    s_and_b32       s_restore_m0, s_restore_pc_hi, S_SAVE_PC_HI_RCNT_MASK
    s_lshr_b32      s_restore_m0, s_restore_m0, S_SAVE_PC_HI_RCNT_SHIFT
    s_lshl_b32      s_restore_m0, s_restore_m0, SQ_WAVE_IB_STS_RCNT_SHIFT
    s_mov_b32       s_restore_tmp, 0x0                                                                              //IB_STS is zero
    s_or_b32        s_restore_tmp, s_restore_tmp, s_restore_m0
    s_and_b32       s_restore_m0, s_restore_pc_hi, S_SAVE_PC_HI_FIRST_REPLAY_MASK
    s_lshr_b32      s_restore_m0, s_restore_m0, S_SAVE_PC_HI_FIRST_REPLAY_SHIFT
    s_lshl_b32      s_restore_m0, s_restore_m0, SQ_WAVE_IB_STS_FIRST_REPLAY_SHIFT
    s_or_b32        s_restore_tmp, s_restore_tmp, s_restore_m0
    s_and_b32       s_restore_m0, s_restore_status, SQ_WAVE_STATUS_INST_ATC_MASK
    s_lshr_b32      s_restore_m0, s_restore_m0, SQ_WAVE_STATUS_INST_ATC_SHIFT
    s_setreg_b32    hwreg(HW_REG_IB_STS),   s_restore_tmp

    s_and_b32 s_restore_pc_hi, s_restore_pc_hi, 0x0000ffff      //pc[47:32]        //Do it here in order not to affect STATUS
    s_and_b64    exec, exec, exec  // Restore STATUS.EXECZ, not writable by s_setreg_b32
    s_and_b64    vcc, vcc, vcc  // Restore STATUS.VCCZ, not writable by s_setreg_b32
    set_status_without_spi_prio(s_restore_status, s_restore_tmp) // SCC is included, which is changed by previous salu

    s_barrier                                                   //barrier to ensure the readiness of LDS before access attempts from any other wave in the same TG //FIXME not performance-optimal at this time

if G8SR_DEBUG_TIMESTAMP
    s_memrealtime s_g8sr_ts_restore_d
    s_waitcnt lgkmcnt(0)
end

//  s_rfe_b64 s_restore_pc_lo                                   //Return to the main shader program and resume execution
    s_rfe_restore_b64  s_restore_pc_lo, s_restore_m0            // s_restore_m0[0] is used to set STATUS.inst_atc


/**************************************************************************/
/*                      the END                                           */
/**************************************************************************/
L_END_PGM:
    s_endpgm

end


/**************************************************************************/
/*                      the helper functions                              */
/**************************************************************************/

//Only for save hwreg to mem
function write_hwreg_to_mem(s, s_rsrc, s_mem_offset)
        s_mov_b32 exec_lo, m0                   //assuming exec_lo is not needed anymore from this point on
        s_mov_b32 m0, s_mem_offset
        s_buffer_store_dword s, s_rsrc, m0      glc:1
        s_add_u32       s_mem_offset, s_mem_offset, 4
        s_mov_b32   m0, exec_lo
end


// HWREG are saved before SGPRs, so all HWREG could be use.
function write_16sgpr_to_mem(s, s_rsrc, s_mem_offset)

        s_buffer_store_dwordx4 s[0], s_rsrc, 0  glc:1
        s_buffer_store_dwordx4 s[4], s_rsrc, 16  glc:1
        s_buffer_store_dwordx4 s[8], s_rsrc, 32  glc:1
        s_buffer_store_dwordx4 s[12], s_rsrc, 48 glc:1
        s_add_u32       s_rsrc[0], s_rsrc[0], 4*16
        s_addc_u32      s_rsrc[1], s_rsrc[1], 0x0             // +scc
end


function read_hwreg_from_mem(s, s_rsrc, s_mem_offset)
    s_buffer_load_dword s, s_rsrc, s_mem_offset     glc:1
    s_add_u32       s_mem_offset, s_mem_offset, 4
end

function read_16sgpr_from_mem(s, s_rsrc, s_mem_offset)
    s_buffer_load_dwordx16 s, s_rsrc, s_mem_offset      glc:1
    s_sub_u32       s_mem_offset, s_mem_offset, 4*16
end



function get_lds_size_bytes(s_lds_size_byte)
    // SQ LDS granularity is 64DW, while PGM_RSRC2.lds_size is in granularity 128DW
    s_getreg_b32   s_lds_size_byte, hwreg(HW_REG_LDS_ALLOC, SQ_WAVE_LDS_ALLOC_LDS_SIZE_SHIFT, SQ_WAVE_LDS_ALLOC_LDS_SIZE_SIZE)          // lds_size
    s_lshl_b32     s_lds_size_byte, s_lds_size_byte, 8                      //LDS size in dwords = lds_size * 64 *4Bytes    // granularity 64DW
end

function get_vgpr_size_bytes(s_vgpr_size_byte)
    s_getreg_b32   s_vgpr_size_byte, hwreg(HW_REG_GPR_ALLOC,SQ_WAVE_GPR_ALLOC_VGPR_SIZE_SHIFT,SQ_WAVE_GPR_ALLOC_VGPR_SIZE_SIZE)  //vpgr_size
    s_add_u32      s_vgpr_size_byte, s_vgpr_size_byte, 1
    s_lshl_b32     s_vgpr_size_byte, s_vgpr_size_byte, (2+8) //Number of VGPRs = (vgpr_size + 1) * 4 * 64 * 4   (non-zero value)   //FIXME for GFX, zero is possible
end

function get_sgpr_size_bytes(s_sgpr_size_byte)
    s_getreg_b32   s_sgpr_size_byte, hwreg(HW_REG_GPR_ALLOC,SQ_WAVE_GPR_ALLOC_SGPR_SIZE_SHIFT,SQ_WAVE_GPR_ALLOC_SGPR_SIZE_SIZE)  //spgr_size
    s_add_u32      s_sgpr_size_byte, s_sgpr_size_byte, 1
    s_lshl_b32     s_sgpr_size_byte, s_sgpr_size_byte, 6 //Number of SGPRs = (sgpr_size + 1) * 16 *4   (non-zero value)
end

function get_hwreg_size_bytes
    return 128 //HWREG size 128 bytes
end

function set_status_without_spi_prio(status, tmp)
    // Do not restore STATUS.SPI_PRIO since scheduler may have raised it.
    s_lshr_b32      tmp, status, SQ_WAVE_STATUS_POST_SPI_PRIO_SHIFT
    s_setreg_b32    hwreg(HW_REG_STATUS, SQ_WAVE_STATUS_POST_SPI_PRIO_SHIFT, SQ_WAVE_STATUS_POST_SPI_PRIO_SIZE), tmp
    s_nop           0x2 // avoid S_SETREG => S_SETREG hazard
    s_setreg_b32    hwreg(HW_REG_STATUS, SQ_WAVE_STATUS_PRE_SPI_PRIO_SHIFT, SQ_WAVE_STATUS_PRE_SPI_PRIO_SIZE), status
end
