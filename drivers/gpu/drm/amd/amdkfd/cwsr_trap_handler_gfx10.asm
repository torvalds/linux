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


shader main

asic(DEFAULT)

type(CS)

wave_size(32)
/*************************************************************************/
/*					control on how to run the shader					 */
/*************************************************************************/
//any hack that needs to be made to run this code in EMU (either becasue various EMU code are not ready or no compute save & restore in EMU run)
var EMU_RUN_HACK					=	0
var EMU_RUN_HACK_RESTORE_NORMAL		=	0
var EMU_RUN_HACK_SAVE_NORMAL_EXIT	=	0
var	EMU_RUN_HACK_SAVE_SINGLE_WAVE	=	0
var EMU_RUN_HACK_SAVE_FIRST_TIME	= 	0					//for interrupted restore in which the first save is through EMU_RUN_HACK
var SAVE_LDS						= 	0
var WG_BASE_ADDR_LO					=   0x9000a000
var WG_BASE_ADDR_HI					=	0x0
var WAVE_SPACE						=	0x9000				//memory size that each wave occupies in workgroup state mem, increase from 5000 to 9000 for more SGPR need to be saved
var CTX_SAVE_CONTROL				=	0x0
var CTX_RESTORE_CONTROL				=	CTX_SAVE_CONTROL
var SIM_RUN_HACK					=	0					//any hack that needs to be made to run this code in SIM (either becasue various RTL code are not ready or no compute save & restore in RTL run)
var	SGPR_SAVE_USE_SQC				=	0					//use SQC D$ to do the write
var USE_MTBUF_INSTEAD_OF_MUBUF		=	0					//need to change BUF_DATA_FORMAT in S_SAVE_BUF_RSRC_WORD3_MISC from 0 to BUF_DATA_FORMAT_32 if set to 1 (i.e. 0x00827FAC)
var SWIZZLE_EN						=	0					//whether we use swizzled buffer addressing
var SAVE_RESTORE_HWID_DDID          =   0
var RESTORE_DDID_IN_SGPR18          =   0
/**************************************************************************/
/*                     	variables							              */
/**************************************************************************/
var SQ_WAVE_STATUS_INST_ATC_SHIFT  = 23
var SQ_WAVE_STATUS_INST_ATC_MASK   = 0x00800000
var SQ_WAVE_STATUS_SPI_PRIO_MASK   = 0x00000006

var SQ_WAVE_LDS_ALLOC_LDS_SIZE_SHIFT	= 12
var SQ_WAVE_LDS_ALLOC_LDS_SIZE_SIZE		= 9
var SQ_WAVE_GPR_ALLOC_VGPR_SIZE_SHIFT	= 8
var SQ_WAVE_GPR_ALLOC_VGPR_SIZE_SIZE	= 6
var SQ_WAVE_GPR_ALLOC_SGPR_SIZE_SHIFT	= 24
var SQ_WAVE_GPR_ALLOC_SGPR_SIZE_SIZE	= 4						//FIXME	 sq.blk still has 4 bits at this time while SQ programming guide has 3 bits
var SQ_WAVE_LDS_ALLOC_VGPR_SHARED_SIZE_SHIFT    = 24
var SQ_WAVE_LDS_ALLOC_VGPR_SHARED_SIZE_SIZE     = 4
var SQ_WAVE_IB_STS2_WAVE64_SHIFT        = 11
var SQ_WAVE_IB_STS2_WAVE64_SIZE         = 1

var	SQ_WAVE_TRAPSTS_SAVECTX_MASK	=	0x400
var SQ_WAVE_TRAPSTS_EXCE_MASK       =   0x1FF          			// Exception mask
var	SQ_WAVE_TRAPSTS_SAVECTX_SHIFT	=	10					
var	SQ_WAVE_TRAPSTS_MEM_VIOL_MASK	=	0x100					
var	SQ_WAVE_TRAPSTS_MEM_VIOL_SHIFT	=	8		
var	SQ_WAVE_TRAPSTS_PRE_SAVECTX_MASK 	=	0x3FF
var	SQ_WAVE_TRAPSTS_PRE_SAVECTX_SHIFT 	=	0x0
var	SQ_WAVE_TRAPSTS_PRE_SAVECTX_SIZE 	=	10
var	SQ_WAVE_TRAPSTS_POST_SAVECTX_MASK 	=	0xFFFFF800	
var	SQ_WAVE_TRAPSTS_POST_SAVECTX_SHIFT 	=	11
var	SQ_WAVE_TRAPSTS_POST_SAVECTX_SIZE 	=	21	

var SQ_WAVE_IB_STS_RCNT_SHIFT			=	16					//FIXME
var SQ_WAVE_IB_STS_FIRST_REPLAY_SHIFT	=	15					//FIXME
var SQ_WAVE_IB_STS_FIRST_REPLAY_SIZE    =   1                   //FIXME
var SQ_WAVE_IB_STS_RCNT_SIZE            =   6                   //FIXME
var SQ_WAVE_IB_STS_RCNT_FIRST_REPLAY_MASK_NEG	= 0x00007FFF	//FIXME
 
var	SQ_BUF_RSRC_WORD1_ATC_SHIFT		=	24
var	SQ_BUF_RSRC_WORD3_MTYPE_SHIFT	=	27


/*      Save        */
var	S_SAVE_BUF_RSRC_WORD1_STRIDE		=	0x00040000  		//stride is 4 bytes 
var	S_SAVE_BUF_RSRC_WORD3_MISC			= 	0x00807FAC			//SQ_SEL_X/Y/Z/W, BUF_NUM_FORMAT_FLOAT, (0 for MUBUF stride[17:14] when ADD_TID_ENABLE and BUF_DATA_FORMAT_32 for MTBUF), ADD_TID_ENABLE			

var	S_SAVE_SPI_INIT_ATC_MASK			=	0x08000000			//bit[27]: ATC bit
var	S_SAVE_SPI_INIT_ATC_SHIFT			=	27
var	S_SAVE_SPI_INIT_MTYPE_MASK			=	0x70000000			//bit[30:28]: Mtype
var	S_SAVE_SPI_INIT_MTYPE_SHIFT			=	28
var	S_SAVE_SPI_INIT_FIRST_WAVE_MASK		=	0x04000000			//bit[26]: FirstWaveInTG
var	S_SAVE_SPI_INIT_FIRST_WAVE_SHIFT	=	26

var S_SAVE_PC_HI_RCNT_SHIFT				=	28					//FIXME	 check with Brian to ensure all fields other than PC[47:0] can be used
var S_SAVE_PC_HI_RCNT_MASK				=   0xF0000000			//FIXME
var S_SAVE_PC_HI_FIRST_REPLAY_SHIFT		=	27					//FIXME
var S_SAVE_PC_HI_FIRST_REPLAY_MASK		=	0x08000000			//FIXME

var	s_save_spi_init_lo				=	exec_lo
var s_save_spi_init_hi				=	exec_hi

var	s_save_pc_lo			=	ttmp0			//{TTMP1, TTMP0} = {3¡¯h0,pc_rewind[3:0], HT[0],trapID[7:0], PC[47:0]}
var	s_save_pc_hi			=	ttmp1			
var s_save_exec_lo			=	ttmp2
var s_save_exec_hi			= 	ttmp3			
var	s_save_status			=	ttmp4			
var	s_save_trapsts			=	ttmp5			//not really used until the end of the SAVE routine
var s_wave_size         	=	ttmp6           //ttmp6 is not needed now, since it's only 32bit xnack mask, now use it to determine wave32 or wave64 in EMU_HACK
var s_save_xnack_mask	    =	ttmp7
var	s_save_buf_rsrc0		=	ttmp8
var	s_save_buf_rsrc1		=	ttmp9
var	s_save_buf_rsrc2		=	ttmp10
var	s_save_buf_rsrc3		=	ttmp11

var s_save_mem_offset		= 	ttmp14
var s_sgpr_save_num         =   106                     //in gfx10, all sgpr must be saved
var s_save_alloc_size		=	s_save_trapsts			//conflict
var s_save_tmp              =   s_save_buf_rsrc2       	//shared with s_save_buf_rsrc2  (conflict: should not use mem access with s_save_tmp at the same time)
var s_save_m0				=	ttmp15					

/*      Restore     */
var	S_RESTORE_BUF_RSRC_WORD1_STRIDE			=	S_SAVE_BUF_RSRC_WORD1_STRIDE 
var	S_RESTORE_BUF_RSRC_WORD3_MISC			= 	S_SAVE_BUF_RSRC_WORD3_MISC		 

var	S_RESTORE_SPI_INIT_ATC_MASK			    =	0x08000000			//bit[27]: ATC bit
var	S_RESTORE_SPI_INIT_ATC_SHIFT			=	27
var	S_RESTORE_SPI_INIT_MTYPE_MASK			=	0x70000000			//bit[30:28]: Mtype
var	S_RESTORE_SPI_INIT_MTYPE_SHIFT			=	28
var	S_RESTORE_SPI_INIT_FIRST_WAVE_MASK		=	0x04000000			//bit[26]: FirstWaveInTG
var	S_RESTORE_SPI_INIT_FIRST_WAVE_SHIFT	    =	26

var S_RESTORE_PC_HI_RCNT_SHIFT				=	S_SAVE_PC_HI_RCNT_SHIFT
var S_RESTORE_PC_HI_RCNT_MASK				=   S_SAVE_PC_HI_RCNT_MASK
var S_RESTORE_PC_HI_FIRST_REPLAY_SHIFT		=	S_SAVE_PC_HI_FIRST_REPLAY_SHIFT
var S_RESTORE_PC_HI_FIRST_REPLAY_MASK		=	S_SAVE_PC_HI_FIRST_REPLAY_MASK

var s_restore_spi_init_lo                   =   exec_lo
var s_restore_spi_init_hi                   =   exec_hi

var s_restore_mem_offset		= 	ttmp12
var s_restore_alloc_size		=	ttmp3
var s_restore_tmp           	=   ttmp6
var s_restore_mem_offset_save	= 	s_restore_tmp 		//no conflict

var s_restore_m0			=	s_restore_alloc_size	//no conflict			

var s_restore_mode			=  	ttmp13
var s_restore_hwid1         =  ttmp2
var s_restore_ddid          =  s_restore_hwid1
var	s_restore_pc_lo		    =	ttmp0			
var	s_restore_pc_hi		    =	ttmp1
var s_restore_exec_lo		=	ttmp14
var s_restore_exec_hi		= 	ttmp15
var	s_restore_status	    =	ttmp4			
var	s_restore_trapsts	    =	ttmp5
//var s_restore_xnack_mask_lo	=	xnack_mask_lo
//var s_restore_xnack_mask_hi	=	xnack_mask_hi
var s_restore_xnack_mask    =   ttmp7
var	s_restore_buf_rsrc0		=	ttmp8
var	s_restore_buf_rsrc1		=	ttmp9
var	s_restore_buf_rsrc2		=	ttmp10
var	s_restore_buf_rsrc3		=	ttmp11
var s_restore_size         	=	ttmp13                  //ttmp13 has no conflict

/**************************************************************************/
/*                     	trap handler entry points			              */
/**************************************************************************/
    if ((EMU_RUN_HACK) && (!EMU_RUN_HACK_RESTORE_NORMAL)) 					//hack to use trap_id for determining save/restore
		//FIXME VCCZ un-init assertion s_getreg_b32  	s_save_status, hwreg(HW_REG_STATUS)			//save STATUS since we will change SCC
		s_and_b32 s_save_tmp, s_save_pc_hi, 0xffff0000 				//change SCC
    	s_cmp_eq_u32 s_save_tmp, 0x007e0000  						//Save: trap_id = 0x7e. Restore: trap_id = 0x7f.  
    	s_cbranch_scc0 L_JUMP_TO_RESTORE							//do not need to recover STATUS here  since we are going to RESTORE
		//FIXME  s_setreg_b32 	hwreg(HW_REG_STATUS), 	s_save_status		//need to recover STATUS since we are going to SAVE	
		s_branch L_SKIP_RESTORE 									//NOT restore, SAVE actually
	else	
		s_branch L_SKIP_RESTORE 									//NOT restore. might be a regular trap or save
    end

L_JUMP_TO_RESTORE:
    s_branch L_RESTORE												//restore

L_SKIP_RESTORE:
	
	s_getreg_b32  	s_save_status, hwreg(HW_REG_STATUS)								//save STATUS since we will change SCC
    s_andn2_b32		s_save_status, s_save_status, SQ_WAVE_STATUS_SPI_PRIO_MASK      //check whether this is for save
	s_getreg_b32  	s_save_trapsts, hwreg(HW_REG_TRAPSTS)    		 				
	s_and_b32		s_save_trapsts, s_save_trapsts, SQ_WAVE_TRAPSTS_SAVECTX_MASK	//check whether this is for save  
	s_cbranch_scc1	L_SAVE															//this is the operation for save

    // *********    Handle non-CWSR traps       *******************
    if (!EMU_RUN_HACK)
		s_getreg_b32     s_save_trapsts, hwreg(HW_REG_TRAPSTS)
		s_and_b32        s_save_trapsts, s_save_trapsts, SQ_WAVE_TRAPSTS_EXCE_MASK // Check whether it is an exception
		s_cbranch_scc1  L_EXCP_CASE   // Exception, jump back to the shader program directly.
		s_add_u32    ttmp0, ttmp0, 4   // S_TRAP case, add 4 to ttmp0 
		
		L_EXCP_CASE:
		s_and_b32    ttmp1, ttmp1, 0xFFFF
		s_rfe_b64    [ttmp0, ttmp1]
	end
    // *********        End handling of non-CWSR traps   *******************

/**************************************************************************/
/*                     	save routine						              */
/**************************************************************************/

L_SAVE:	
	
	//check whether there is mem_viol
	s_getreg_b32  	s_save_trapsts, hwreg(HW_REG_TRAPSTS)    		 				
	s_and_b32		s_save_trapsts, s_save_trapsts, SQ_WAVE_TRAPSTS_MEM_VIOL_MASK			
	s_cbranch_scc0	L_NO_PC_REWIND
    
	//if so, need rewind PC assuming GDS operation gets NACKed
	s_mov_b32       s_save_tmp, 0															//clear mem_viol bit
	s_setreg_b32	hwreg(HW_REG_TRAPSTS, SQ_WAVE_TRAPSTS_MEM_VIOL_SHIFT, 1), s_save_tmp	//clear mem_viol bit 
	s_and_b32 		s_save_pc_hi, s_save_pc_hi, 0x0000ffff    //pc[47:32]
	s_sub_u32 		s_save_pc_lo, s_save_pc_lo, 8             //pc[31:0]-8
	s_subb_u32 		s_save_pc_hi, s_save_pc_hi, 0x0			  // -scc

L_NO_PC_REWIND:
    s_mov_b32       s_save_tmp, 0															//clear saveCtx bit
	s_setreg_b32	hwreg(HW_REG_TRAPSTS, SQ_WAVE_TRAPSTS_SAVECTX_SHIFT, 1), s_save_tmp		//clear saveCtx bit   

	//s_mov_b32		s_save_xnack_mask_lo,	xnack_mask_lo									//save XNACK_MASK  
	//s_mov_b32		s_save_xnack_mask_hi,	xnack_mask_hi
    s_getreg_b32	s_save_xnack_mask,  hwreg(HW_REG_SHADER_XNACK_MASK)  
	s_getreg_b32	s_save_tmp, hwreg(HW_REG_IB_STS, SQ_WAVE_IB_STS_RCNT_SHIFT, SQ_WAVE_IB_STS_RCNT_SIZE)					//save RCNT
	s_lshl_b32		s_save_tmp, s_save_tmp, S_SAVE_PC_HI_RCNT_SHIFT
	s_or_b32		s_save_pc_hi, s_save_pc_hi, s_save_tmp
	s_getreg_b32	s_save_tmp, hwreg(HW_REG_IB_STS, SQ_WAVE_IB_STS_FIRST_REPLAY_SHIFT, SQ_WAVE_IB_STS_FIRST_REPLAY_SIZE)	//save FIRST_REPLAY
	s_lshl_b32		s_save_tmp, s_save_tmp, S_SAVE_PC_HI_FIRST_REPLAY_SHIFT
	s_or_b32		s_save_pc_hi, s_save_pc_hi, s_save_tmp
	s_getreg_b32	s_save_tmp, hwreg(HW_REG_IB_STS)										//clear RCNT and FIRST_REPLAY in IB_STS
	s_and_b32		s_save_tmp, s_save_tmp, SQ_WAVE_IB_STS_RCNT_FIRST_REPLAY_MASK_NEG

	s_setreg_b32	hwreg(HW_REG_IB_STS), s_save_tmp
    
	/*		inform SPI the readiness and wait for SPI's go signal */
	s_mov_b32		s_save_exec_lo,	exec_lo													//save EXEC and use EXEC for the go signal from SPI
	s_mov_b32		s_save_exec_hi,	exec_hi
	s_mov_b64		exec, 	0x0																//clear EXEC to get ready to receive
	if (EMU_RUN_HACK)
	
	else
		s_sendmsg	sendmsg(MSG_SAVEWAVE)													//send SPI a message and wait for SPI's write to EXEC  
	end

  L_SLEEP:		
	s_sleep 0x2
	
	if (EMU_RUN_HACK)
																							
	else
		s_cbranch_execz	L_SLEEP                                                         
	end


	/*      setup Resource Contants    */
	if ((EMU_RUN_HACK) && (!EMU_RUN_HACK_SAVE_SINGLE_WAVE))	
		//calculate wd_addr using absolute thread id 
		v_readlane_b32 s_save_tmp, v9, 0
        //determine it is wave32 or wave64
        s_getreg_b32 	s_wave_size, hwreg(HW_REG_IB_STS2,SQ_WAVE_IB_STS2_WAVE64_SHIFT,SQ_WAVE_IB_STS2_WAVE64_SIZE)
        s_cmp_eq_u32    s_wave_size, 0
        s_cbranch_scc1  L_SAVE_WAVE32
        s_lshr_b32 s_save_tmp, s_save_tmp, 6 //SAVE WAVE64
        s_branch    L_SAVE_CON
    L_SAVE_WAVE32:
        s_lshr_b32 s_save_tmp, s_save_tmp, 5 //SAVE WAVE32
    L_SAVE_CON:
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
	
	
	s_mov_b32		s_save_buf_rsrc0, 	s_save_spi_init_lo														//base_addr_lo
	s_and_b32		s_save_buf_rsrc1, 	s_save_spi_init_hi, 0x0000FFFF											//base_addr_hi
	s_or_b32		s_save_buf_rsrc1, 	s_save_buf_rsrc1,  S_SAVE_BUF_RSRC_WORD1_STRIDE
    s_mov_b32       s_save_buf_rsrc2,   0                                               						//NUM_RECORDS initial value = 0 (in bytes) although not neccessarily inited
	s_mov_b32		s_save_buf_rsrc3, 	S_SAVE_BUF_RSRC_WORD3_MISC
	s_and_b32		s_save_tmp,         s_save_spi_init_hi, S_SAVE_SPI_INIT_ATC_MASK		
	s_lshr_b32		s_save_tmp,  		s_save_tmp, (S_SAVE_SPI_INIT_ATC_SHIFT-SQ_BUF_RSRC_WORD1_ATC_SHIFT)			//get ATC bit into position
	s_or_b32		s_save_buf_rsrc3, 	s_save_buf_rsrc3,  s_save_tmp											//or ATC
	s_and_b32		s_save_tmp,         s_save_spi_init_hi, S_SAVE_SPI_INIT_MTYPE_MASK		
	s_lshr_b32		s_save_tmp,  		s_save_tmp, (S_SAVE_SPI_INIT_MTYPE_SHIFT-SQ_BUF_RSRC_WORD3_MTYPE_SHIFT)		//get MTYPE bits into position
	s_or_b32		s_save_buf_rsrc3, 	s_save_buf_rsrc3,  s_save_tmp											//or MTYPE	
	
	s_mov_b32		s_save_m0,			m0																	//save M0
	
	/* 		global mem offset			*/
	s_mov_b32		s_save_mem_offset, 	0x0																		//mem offset initial value = 0
    s_getreg_b32 	s_wave_size, hwreg(HW_REG_IB_STS2,SQ_WAVE_IB_STS2_WAVE64_SHIFT,SQ_WAVE_IB_STS2_WAVE64_SIZE) //get wave_save_size
    s_or_b32        s_wave_size, s_save_spi_init_hi,    s_wave_size                                             //share s_wave_size with exec_hi

    /*      	save VGPRs	    */
	//////////////////////////////
  L_SAVE_VGPR:
  
 	s_mov_b32		exec_lo, 0xFFFFFFFF 											//need every thread from now on
    s_and_b32       m0, s_wave_size, 1
    s_cmp_eq_u32    m0, 1  
    s_cbranch_scc1  L_ENABLE_SAVE_VGPR_EXEC_HI   
    s_mov_b32		exec_hi, 0x00000000
    s_branch        L_SAVE_VGPR_NORMAL
  L_ENABLE_SAVE_VGPR_EXEC_HI:
	s_mov_b32		exec_hi, 0xFFFFFFFF
  L_SAVE_VGPR_NORMAL:	
	s_getreg_b32 	s_save_alloc_size, hwreg(HW_REG_GPR_ALLOC,SQ_WAVE_GPR_ALLOC_VGPR_SIZE_SHIFT,SQ_WAVE_GPR_ALLOC_VGPR_SIZE_SIZE) 					//vpgr_size
	//for wave32 and wave64, the num of vgpr function is the same?
    s_add_u32 		s_save_alloc_size, s_save_alloc_size, 1
	s_lshl_b32 		s_save_alloc_size, s_save_alloc_size, 2 						//Number of VGPRs = (vgpr_size + 1) * 4    (non-zero value)   //FIXME for GFX, zero is possible
    //determine it is wave32 or wave64
    s_and_b32       m0, s_wave_size, 1
    s_cmp_eq_u32    m0, 1
    s_cbranch_scc1  L_SAVE_VGPR_WAVE64

    //zhenxu added it for save vgpr for wave32
	s_lshl_b32		s_save_buf_rsrc2,  s_save_alloc_size, 7							//NUM_RECORDS in bytes (32 threads*4)
	if (SWIZZLE_EN)
		s_add_u32		s_save_buf_rsrc2, s_save_buf_rsrc2, 0x0						//FIXME need to use swizzle to enable bounds checking?
	else
		s_mov_b32		s_save_buf_rsrc2,  0x1000000								//NUM_RECORDS in bytes
	end
	
    s_mov_b32 		m0, 0x0 														//VGPR initial index value =0
	//s_set_gpr_idx_on  m0, 0x1														//M0[7:0] = M0[7:0] and M0[15:12] = 0x1
    //s_add_u32		s_save_alloc_size, s_save_alloc_size, 0x1000					//add 0x1000 since we compare m0 against it later, doesn't need this in gfx10

  L_SAVE_VGPR_WAVE32_LOOP: 										
	v_movrels_b32 		v0, v0															//v0 = v[0+m0]	
	    
    if(USE_MTBUF_INSTEAD_OF_MUBUF)       
		tbuffer_store_format_x v0, v0, s_save_buf_rsrc0, s_save_mem_offset format:BUF_NUM_FORMAT_FLOAT format: BUF_DATA_FORMAT_32 slc:1 glc:1
    else
		buffer_store_dword v0, v0, s_save_buf_rsrc0, s_save_mem_offset slc:1 glc:1
	end

    s_add_u32		m0, m0, 1														//next vgpr index
	s_add_u32		s_save_mem_offset, s_save_mem_offset, 128						//every buffer_store_dword does 128 bytes
	s_cmp_lt_u32 	m0,	s_save_alloc_size 											//scc = (m0 < s_save_alloc_size) ? 1 : 0
	s_cbranch_scc1 	L_SAVE_VGPR_WAVE32_LOOP												//VGPR save is complete?
    s_branch    L_SAVE_LDS
    //save vgpr for wave32 ends

  L_SAVE_VGPR_WAVE64:
	s_lshl_b32		s_save_buf_rsrc2,  s_save_alloc_size, 8							//NUM_RECORDS in bytes (64 threads*4)
	if (SWIZZLE_EN)
		s_add_u32		s_save_buf_rsrc2, s_save_buf_rsrc2, 0x0						//FIXME need to use swizzle to enable bounds checking?
	else
		s_mov_b32		s_save_buf_rsrc2,  0x1000000								//NUM_RECORDS in bytes
	end
	
    s_mov_b32 		m0, 0x0 														//VGPR initial index value =0
	//s_set_gpr_idx_on  m0, 0x1														//M0[7:0] = M0[7:0] and M0[15:12] = 0x1
    //s_add_u32		s_save_alloc_size, s_save_alloc_size, 0x1000					//add 0x1000 since we compare m0 against it later, doesn't need this in gfx10

  L_SAVE_VGPR_WAVE64_LOOP: 										
	v_movrels_b32 		v0, v0															//v0 = v[0+m0]	
	    
    if(USE_MTBUF_INSTEAD_OF_MUBUF)       
		tbuffer_store_format_x v0, v0, s_save_buf_rsrc0, s_save_mem_offset format:BUF_NUM_FORMAT_FLOAT format: BUF_DATA_FORMAT_32 slc:1 glc:1
    else
		buffer_store_dword v0, v0, s_save_buf_rsrc0, s_save_mem_offset slc:1 glc:1
	end

    s_add_u32		m0, m0, 1														//next vgpr index
	s_add_u32		s_save_mem_offset, s_save_mem_offset, 256						//every buffer_store_dword does 256 bytes
	s_cmp_lt_u32 	m0,	s_save_alloc_size 											//scc = (m0 < s_save_alloc_size) ? 1 : 0
	s_cbranch_scc1 	L_SAVE_VGPR_WAVE64_LOOP												//VGPR save is complete?
	//s_set_gpr_idx_off
    //
    //Below part will be the save shared vgpr part (new for gfx10)
    s_getreg_b32 	s_save_alloc_size, hwreg(HW_REG_LDS_ALLOC,SQ_WAVE_LDS_ALLOC_VGPR_SHARED_SIZE_SHIFT,SQ_WAVE_LDS_ALLOC_VGPR_SHARED_SIZE_SIZE) 			//shared_vgpr_size
    s_and_b32		s_save_alloc_size, s_save_alloc_size, 0xFFFFFFFF				//shared_vgpr_size is zero?
    s_cbranch_scc0	L_SAVE_LDS													    //no shared_vgpr used? jump to L_SAVE_LDS
    s_lshl_b32 		s_save_alloc_size, s_save_alloc_size, 3 						//Number of SHARED_VGPRs = shared_vgpr_size * 8    (non-zero value)
    //m0 now has the value of normal vgpr count, just add the m0 with shared_vgpr count to get the total count.
    //save shared_vgpr will start from the index of m0
    s_add_u32       s_save_alloc_size, s_save_alloc_size, m0
    s_mov_b32		exec_lo, 0xFFFFFFFF
    s_mov_b32		exec_hi, 0x00000000
    L_SAVE_SHARED_VGPR_WAVE64_LOOP: 										
	v_movrels_b32 		v0, v0															//v0 = v[0+m0]	
	buffer_store_dword v0, v0, s_save_buf_rsrc0, s_save_mem_offset slc:1 glc:1
    s_add_u32		m0, m0, 1														//next vgpr index
	s_add_u32		s_save_mem_offset, s_save_mem_offset, 128						//every buffer_store_dword does 256 bytes
	s_cmp_lt_u32 	m0,	s_save_alloc_size 											//scc = (m0 < s_save_alloc_size) ? 1 : 0
	s_cbranch_scc1 	L_SAVE_SHARED_VGPR_WAVE64_LOOP									//SHARED_VGPR save is complete?
    
	/*      	save LDS	    */
	//////////////////////////////
  L_SAVE_LDS:

    //Only check the first wave need LDS
	/*      the first wave in the threadgroup    */
	s_barrier																		//FIXME  not performance-optimal "LDS is used? wait for other waves in the same TG" 
	s_and_b32		s_save_tmp, s_wave_size, S_SAVE_SPI_INIT_FIRST_WAVE_MASK								//exec is still used here
	s_cbranch_scc0	L_SAVE_SGPR
	
	s_mov_b32		exec_lo, 0xFFFFFFFF 											//need every thread from now on
    s_and_b32       m0, s_wave_size, 1
    s_cmp_eq_u32    m0, 1
    s_cbranch_scc1  L_ENABLE_SAVE_LDS_EXEC_HI   
    s_mov_b32		exec_hi, 0x00000000
    s_branch        L_SAVE_LDS_NORMAL
  L_ENABLE_SAVE_LDS_EXEC_HI:
	s_mov_b32		exec_hi, 0xFFFFFFFF
  L_SAVE_LDS_NORMAL:	
	s_getreg_b32 	s_save_alloc_size, hwreg(HW_REG_LDS_ALLOC,SQ_WAVE_LDS_ALLOC_LDS_SIZE_SHIFT,SQ_WAVE_LDS_ALLOC_LDS_SIZE_SIZE) 			//lds_size
	s_and_b32		s_save_alloc_size, s_save_alloc_size, 0xFFFFFFFF				//lds_size is zero?
	s_cbranch_scc0	L_SAVE_SGPR														//no lds used? jump to L_SAVE_VGPR
	s_lshl_b32 		s_save_alloc_size, s_save_alloc_size, 6 						//LDS size in dwords = lds_size * 64dw
	s_lshl_b32 		s_save_alloc_size, s_save_alloc_size, 2 						//LDS size in bytes
	s_mov_b32		s_save_buf_rsrc2,  s_save_alloc_size  							//NUM_RECORDS in bytes
	if (SWIZZLE_EN)
		s_add_u32		s_save_buf_rsrc2, s_save_buf_rsrc2, 0x0						//FIXME need to use swizzle to enable bounds checking?
	else
		s_mov_b32		s_save_buf_rsrc2,  0x1000000								//NUM_RECORDS in bytes
	end

    //load 0~63*4(byte address) to vgpr v15
    v_mbcnt_lo_u32_b32 v0, -1, 0
    v_mbcnt_hi_u32_b32 v0, -1, v0
    v_mul_u32_u24 v0, 4, v0

    s_and_b32       m0, s_wave_size, 1
    s_cmp_eq_u32    m0, 1
    s_mov_b32 		m0, 0x0
    s_cbranch_scc1  L_SAVE_LDS_LOOP_W64

  L_SAVE_LDS_LOOP_W32:									
	if (SAVE_LDS)
    ds_read_b32 v1, v0
    s_waitcnt 0														    //ensure data ready
    buffer_store_dword v1, v0, s_save_buf_rsrc0, s_save_mem_offset slc:1 glc:1
	//buffer_store_lds_dword	s_save_buf_rsrc0, s_save_mem_offset lds:1               //save lds to memory doesn't exist in 10
	end
	s_add_u32		m0, m0, 128															//every buffer_store_lds does 128 bytes
	s_add_u32		s_save_mem_offset, s_save_mem_offset, 128							//mem offset increased by 128 bytes
    v_add_nc_u32    v0, v0, 128
	s_cmp_lt_u32	m0, s_save_alloc_size												//scc=(m0 < s_save_alloc_size) ? 1 : 0
	s_cbranch_scc1  L_SAVE_LDS_LOOP_W32													//LDS save is complete?
    s_branch        L_SAVE_SGPR

  L_SAVE_LDS_LOOP_W64:									
	if (SAVE_LDS)
    ds_read_b32 v1, v0
    s_waitcnt 0														    //ensure data ready
    buffer_store_dword v1, v0, s_save_buf_rsrc0, s_save_mem_offset slc:1 glc:1
	//buffer_store_lds_dword	s_save_buf_rsrc0, s_save_mem_offset lds:1               //save lds to memory doesn't exist in 10
	end
	s_add_u32		m0, m0, 256															//every buffer_store_lds does 256 bytes
	s_add_u32		s_save_mem_offset, s_save_mem_offset, 256							//mem offset increased by 256 bytes
    v_add_nc_u32    v0, v0, 256
	s_cmp_lt_u32	m0, s_save_alloc_size												//scc=(m0 < s_save_alloc_size) ? 1 : 0
	s_cbranch_scc1  L_SAVE_LDS_LOOP_W64													//LDS save is complete?
   
	
	/*      	save SGPRs	    */
	//////////////////////////////
	//s_getreg_b32 	s_save_alloc_size, hwreg(HW_REG_GPR_ALLOC,SQ_WAVE_GPR_ALLOC_SGPR_SIZE_SHIFT,SQ_WAVE_GPR_ALLOC_SGPR_SIZE_SIZE) 				//spgr_size
	//s_add_u32 		s_save_alloc_size, s_save_alloc_size, 1
	//s_lshl_b32 		s_save_alloc_size, s_save_alloc_size, 4 						//Number of SGPRs = (sgpr_size + 1) * 16   (non-zero value) 
	//s_lshl_b32 		s_save_alloc_size, s_save_alloc_size, 3 						//In gfx10, Number of SGPRs = (sgpr_size + 1) * 8   (non-zero value) 
  L_SAVE_SGPR:
    //need to look at it is wave32 or wave64
    s_and_b32       m0, s_wave_size, 1
    s_cmp_eq_u32    m0, 1
    s_cbranch_scc1  L_SAVE_SGPR_VMEM_WAVE64
    if (SGPR_SAVE_USE_SQC)
		s_lshl_b32		s_save_buf_rsrc2,	s_sgpr_save_num, 2					//NUM_RECORDS in bytes
    else
        s_lshl_b32		s_save_buf_rsrc2,	s_sgpr_save_num, 7					//NUM_RECORDS in bytes (32 threads)
    end
    s_branch    L_SAVE_SGPR_CONT    
  L_SAVE_SGPR_VMEM_WAVE64:
	if (SGPR_SAVE_USE_SQC)
		s_lshl_b32		s_save_buf_rsrc2,	s_sgpr_save_num, 2					//NUM_RECORDS in bytes 
	else
		s_lshl_b32		s_save_buf_rsrc2,	s_sgpr_save_num, 8					//NUM_RECORDS in bytes (64 threads)
	end
  L_SAVE_SGPR_CONT:
	if (SWIZZLE_EN)
		s_add_u32		s_save_buf_rsrc2, s_save_buf_rsrc2, 0x0						//FIXME need to use swizzle to enable bounds checking?
	else
		s_mov_b32		s_save_buf_rsrc2,  0x1000000								//NUM_RECORDS in bytes
	end
	
	//s_mov_b32 		m0, 0x0 														//SGPR initial index value =0		
    //s_nop           0x0                                                             //Manually inserted wait states
	
    s_and_b32       m0, s_wave_size, 1
    s_cmp_eq_u32    m0, 1
    
    s_mov_b32 		m0, 0x0 														//SGPR initial index value =0		
    s_nop           0x0                                                             //Manually inserted wait states

    s_cbranch_scc1  L_SAVE_SGPR_LOOP_WAVE64

  L_SAVE_SGPR_LOOP_WAVE32: 										
	s_movrels_b32 	s0, s0 															//s0 = s[0+m0]
    //zhenxu, adding one more argument to save sgpr function, this is only for vmem, using sqc is not change    
	write_sgpr_to_mem_wave32(s0, s_save_buf_rsrc0, s_save_mem_offset, SGPR_SAVE_USE_SQC, USE_MTBUF_INSTEAD_OF_MUBUF)							//PV: the best performance should be using s_buffer_store_dwordx4
	s_add_u32		m0, m0, 1														//next sgpr index
	s_cmp_lt_u32 	m0, s_sgpr_save_num 											//scc = (m0 < s_sgpr_save_num) ? 1 : 0
	s_cbranch_scc1 	L_SAVE_SGPR_LOOP_WAVE32												//SGPR save is complete?
    s_branch    L_SAVE_HWREG

  L_SAVE_SGPR_LOOP_WAVE64: 										
	s_movrels_b32 	s0, s0 															//s0 = s[0+m0]
    //zhenxu, adding one more argument to save sgpr function, this is only for vmem, using sqc is not change    
	write_sgpr_to_mem_wave64(s0, s_save_buf_rsrc0, s_save_mem_offset, SGPR_SAVE_USE_SQC, USE_MTBUF_INSTEAD_OF_MUBUF)							//PV: the best performance should be using s_buffer_store_dwordx4
	s_add_u32		m0, m0, 1														//next sgpr index
	s_cmp_lt_u32 	m0, s_sgpr_save_num 											//scc = (m0 < s_sgpr_save_num) ? 1 : 0
	s_cbranch_scc1 	L_SAVE_SGPR_LOOP_WAVE64												//SGPR save is complete?

	
	/* 		save HW registers	*/
	//////////////////////////////
  L_SAVE_HWREG:
    s_mov_b32		s_save_buf_rsrc2, 0x4								//NUM_RECORDS	in bytes
	if (SWIZZLE_EN)
		s_add_u32		s_save_buf_rsrc2, s_save_buf_rsrc2, 0x0						//FIXME need to use swizzle to enable bounds checking?
	else
		s_mov_b32		s_save_buf_rsrc2,  0x1000000								//NUM_RECORDS in bytes
	end

    s_and_b32       m0, s_wave_size, 1
    s_cmp_eq_u32    m0, 1
    s_cbranch_scc1  L_SAVE_HWREG_WAVE64
	
	write_sgpr_to_mem_wave32(s_save_m0, s_save_buf_rsrc0, s_save_mem_offset, SGPR_SAVE_USE_SQC, USE_MTBUF_INSTEAD_OF_MUBUF)					//M0

	if ((EMU_RUN_HACK) && (EMU_RUN_HACK_SAVE_FIRST_TIME))      
		s_add_u32 s_save_pc_lo, s_save_pc_lo, 4             //pc[31:0]+4
		s_addc_u32 s_save_pc_hi, s_save_pc_hi, 0x0			//carry bit over
	end

	write_sgpr_to_mem_wave32(s_save_pc_lo, s_save_buf_rsrc0, s_save_mem_offset, SGPR_SAVE_USE_SQC, USE_MTBUF_INSTEAD_OF_MUBUF)					//PC
	write_sgpr_to_mem_wave32(s_save_pc_hi, s_save_buf_rsrc0, s_save_mem_offset, SGPR_SAVE_USE_SQC, USE_MTBUF_INSTEAD_OF_MUBUF)
	write_sgpr_to_mem_wave32(s_save_exec_lo, s_save_buf_rsrc0, s_save_mem_offset, SGPR_SAVE_USE_SQC, USE_MTBUF_INSTEAD_OF_MUBUF)				//EXEC
	write_sgpr_to_mem_wave32(s_save_exec_hi, s_save_buf_rsrc0, s_save_mem_offset, SGPR_SAVE_USE_SQC, USE_MTBUF_INSTEAD_OF_MUBUF)
	write_sgpr_to_mem_wave32(s_save_status, s_save_buf_rsrc0, s_save_mem_offset, SGPR_SAVE_USE_SQC, USE_MTBUF_INSTEAD_OF_MUBUF)				//STATUS 
	
	//s_save_trapsts conflicts with s_save_alloc_size
	s_getreg_b32    s_save_trapsts, hwreg(HW_REG_TRAPSTS)
	write_sgpr_to_mem_wave32(s_save_trapsts, s_save_buf_rsrc0, s_save_mem_offset, SGPR_SAVE_USE_SQC, USE_MTBUF_INSTEAD_OF_MUBUF)				//TRAPSTS
	
	//write_sgpr_to_mem_wave32(s_save_xnack_mask_lo, s_save_buf_rsrc0, s_save_mem_offset, SGPR_SAVE_USE_SQC, USE_MTBUF_INSTEAD_OF_MUBUF)			//XNACK_MASK_LO
	write_sgpr_to_mem_wave32(s_save_xnack_mask, s_save_buf_rsrc0, s_save_mem_offset, SGPR_SAVE_USE_SQC, USE_MTBUF_INSTEAD_OF_MUBUF)			//XNACK_MASK_HI
	
	//use s_save_tmp would introduce conflict here between s_save_tmp and s_save_buf_rsrc2
	s_getreg_b32 	s_save_m0, hwreg(HW_REG_MODE)																						//MODE
	write_sgpr_to_mem_wave32(s_save_m0, s_save_buf_rsrc0, s_save_mem_offset, SGPR_SAVE_USE_SQC, USE_MTBUF_INSTEAD_OF_MUBUF)
    if(SAVE_RESTORE_HWID_DDID)
    s_getreg_b32 	s_save_m0, hwreg(HW_REG_HW_ID1)																						//HW_ID1, handler records the SE/SA/WGP/SIMD/wave of the original wave
    write_sgpr_to_mem_wave32(s_save_m0, s_save_buf_rsrc0, s_save_mem_offset, SGPR_SAVE_USE_SQC, USE_MTBUF_INSTEAD_OF_MUBUF)
    end
    s_branch   L_S_PGM_END_SAVED

  L_SAVE_HWREG_WAVE64:
    write_sgpr_to_mem_wave64(s_save_m0, s_save_buf_rsrc0, s_save_mem_offset, SGPR_SAVE_USE_SQC, USE_MTBUF_INSTEAD_OF_MUBUF)					//M0

	if ((EMU_RUN_HACK) && (EMU_RUN_HACK_SAVE_FIRST_TIME))      
		s_add_u32 s_save_pc_lo, s_save_pc_lo, 4             //pc[31:0]+4
		s_addc_u32 s_save_pc_hi, s_save_pc_hi, 0x0			//carry bit over
	end

	write_sgpr_to_mem_wave64(s_save_pc_lo, s_save_buf_rsrc0, s_save_mem_offset, SGPR_SAVE_USE_SQC, USE_MTBUF_INSTEAD_OF_MUBUF)					//PC
	write_sgpr_to_mem_wave64(s_save_pc_hi, s_save_buf_rsrc0, s_save_mem_offset, SGPR_SAVE_USE_SQC, USE_MTBUF_INSTEAD_OF_MUBUF)
	write_sgpr_to_mem_wave64(s_save_exec_lo, s_save_buf_rsrc0, s_save_mem_offset, SGPR_SAVE_USE_SQC, USE_MTBUF_INSTEAD_OF_MUBUF)				//EXEC
	write_sgpr_to_mem_wave64(s_save_exec_hi, s_save_buf_rsrc0, s_save_mem_offset, SGPR_SAVE_USE_SQC, USE_MTBUF_INSTEAD_OF_MUBUF)
	write_sgpr_to_mem_wave64(s_save_status, s_save_buf_rsrc0, s_save_mem_offset, SGPR_SAVE_USE_SQC, USE_MTBUF_INSTEAD_OF_MUBUF)				//STATUS 
	
	//s_save_trapsts conflicts with s_save_alloc_size
	s_getreg_b32    s_save_trapsts, hwreg(HW_REG_TRAPSTS)
	write_sgpr_to_mem_wave64(s_save_trapsts, s_save_buf_rsrc0, s_save_mem_offset, SGPR_SAVE_USE_SQC, USE_MTBUF_INSTEAD_OF_MUBUF)				//TRAPSTS
	
	//write_sgpr_to_mem_wave64(s_save_xnack_mask_lo, s_save_buf_rsrc0, s_save_mem_offset, SGPR_SAVE_USE_SQC, USE_MTBUF_INSTEAD_OF_MUBUF)			//XNACK_MASK_LO
	write_sgpr_to_mem_wave64(s_save_xnack_mask, s_save_buf_rsrc0, s_save_mem_offset, SGPR_SAVE_USE_SQC, USE_MTBUF_INSTEAD_OF_MUBUF)			//XNACK_MASK_HI
	
	//use s_save_tmp would introduce conflict here between s_save_tmp and s_save_buf_rsrc2
	s_getreg_b32 	s_save_m0, hwreg(HW_REG_MODE)																						//MODE
	write_sgpr_to_mem_wave64(s_save_m0, s_save_buf_rsrc0, s_save_mem_offset, SGPR_SAVE_USE_SQC, USE_MTBUF_INSTEAD_OF_MUBUF)


    if(SAVE_RESTORE_HWID_DDID)
    s_getreg_b32 	s_save_m0, hwreg(HW_REG_HW_ID1)																						//HW_ID1, handler records the SE/SA/WGP/SIMD/wave of the original wave
    write_sgpr_to_mem_wave64(s_save_m0, s_save_buf_rsrc0, s_save_mem_offset, SGPR_SAVE_USE_SQC, USE_MTBUF_INSTEAD_OF_MUBUF)

	/* 		save DDID	*/
	//////////////////////////////
  L_SAVE_DDID:
    //EXEC has been saved, no vector inst following
    s_mov_b32	exec_lo, 0x80000000    //Set MSB to 1. Cleared when draw index is returned
    s_sendmsg sendmsg(MSG_GET_DDID)

  L_WAIT_DDID_LOOP:    
    s_nop		7			// sleep a bit
    s_bitcmp0_b32 exec_lo, 31	// test to see if MSB is cleared, meaning done
    s_cbranch_scc0	L_WAIT_DDID_LOOP

    s_mov_b32	s_save_m0, exec_lo


    s_mov_b32		s_save_buf_rsrc2, 0x4								//NUM_RECORDS	in bytes
	if (SWIZZLE_EN)
		s_add_u32		s_save_buf_rsrc2, s_save_buf_rsrc2, 0x0						//FIXME need to use swizzle to enable bounds checking?
	else
		s_mov_b32		s_save_buf_rsrc2,  0x1000000								//NUM_RECORDS in bytes
	end
    s_and_b32       m0, s_wave_size, 1
    s_cmp_eq_u32    m0, 1
    s_cbranch_scc1  L_SAVE_DDID_WAVE64

    write_sgpr_to_mem_wave32(s_save_m0, s_save_buf_rsrc0, s_save_mem_offset, SGPR_SAVE_USE_SQC, USE_MTBUF_INSTEAD_OF_MUBUF) 

  L_SAVE_DDID_WAVE64:
    write_sgpr_to_mem_wave64(s_save_m0, s_save_buf_rsrc0, s_save_mem_offset, SGPR_SAVE_USE_SQC, USE_MTBUF_INSTEAD_OF_MUBUF) 

    end
   
  L_S_PGM_END_SAVED:
	/*     S_PGM_END_SAVED  */    							//FIXME  graphics ONLY
	if ((EMU_RUN_HACK) && (!EMU_RUN_HACK_SAVE_NORMAL_EXIT))	
		s_and_b32 s_save_pc_hi, s_save_pc_hi, 0x0000ffff    //pc[47:32]
		s_add_u32 s_save_pc_lo, s_save_pc_lo, 4             //pc[31:0]+4
		s_addc_u32 s_save_pc_hi, s_save_pc_hi, 0x0			//carry bit over
		s_rfe_b64 s_save_pc_lo                              //Return to the main shader program
	else
	end

	
    s_branch	L_END_PGM
	

				
/**************************************************************************/
/*                     	restore routine						              */
/**************************************************************************/

L_RESTORE:
    /*      Setup Resource Contants    */
    if ((EMU_RUN_HACK) && (!EMU_RUN_HACK_RESTORE_NORMAL))
		//calculate wd_addr using absolute thread id
		v_readlane_b32 s_restore_tmp, v9, 0
        //determine it is wave32 or wave64
        s_getreg_b32 	s_restore_size, hwreg(HW_REG_IB_STS2,SQ_WAVE_IB_STS2_WAVE64_SHIFT,SQ_WAVE_IB_STS2_WAVE64_SIZE) //change to ttmp13
        s_cmp_eq_u32    s_restore_size, 0
        s_cbranch_scc1  L_RESTORE_WAVE32
        s_lshr_b32 s_restore_tmp, s_restore_tmp, 6 //SAVE WAVE64
        s_branch    L_RESTORE_CON
    L_RESTORE_WAVE32:
        s_lshr_b32 s_restore_tmp, s_restore_tmp, 5 //SAVE WAVE32
    L_RESTORE_CON:
		s_mul_i32 s_restore_tmp, s_restore_tmp, WAVE_SPACE
		s_add_i32 s_restore_spi_init_lo, s_restore_tmp, WG_BASE_ADDR_LO
		s_mov_b32 s_restore_spi_init_hi, WG_BASE_ADDR_HI
		s_and_b32 s_restore_spi_init_hi, s_restore_spi_init_hi, CTX_RESTORE_CONTROL	
	else
	end
	
    s_mov_b32		s_restore_buf_rsrc0, 	s_restore_spi_init_lo															//base_addr_lo
	s_and_b32		s_restore_buf_rsrc1, 	s_restore_spi_init_hi, 0x0000FFFF												//base_addr_hi
	s_or_b32		s_restore_buf_rsrc1, 	s_restore_buf_rsrc1,  S_RESTORE_BUF_RSRC_WORD1_STRIDE
    s_mov_b32       s_restore_buf_rsrc2,   	0                                               								//NUM_RECORDS initial value = 0 (in bytes)
	s_mov_b32		s_restore_buf_rsrc3, 	S_RESTORE_BUF_RSRC_WORD3_MISC
	s_and_b32		s_restore_tmp,         	s_restore_spi_init_hi, S_RESTORE_SPI_INIT_ATC_MASK		
	s_lshr_b32		s_restore_tmp,  		s_restore_tmp, (S_RESTORE_SPI_INIT_ATC_SHIFT-SQ_BUF_RSRC_WORD1_ATC_SHIFT)		//get ATC bit into position
	s_or_b32		s_restore_buf_rsrc3, 	s_restore_buf_rsrc3,  s_restore_tmp												//or ATC
	s_and_b32		s_restore_tmp,         	s_restore_spi_init_hi, S_RESTORE_SPI_INIT_MTYPE_MASK		
	s_lshr_b32		s_restore_tmp,  		s_restore_tmp, (S_RESTORE_SPI_INIT_MTYPE_SHIFT-SQ_BUF_RSRC_WORD3_MTYPE_SHIFT)	//get MTYPE bits into position
	s_or_b32		s_restore_buf_rsrc3, 	s_restore_buf_rsrc3,  s_restore_tmp												//or MTYPE
    //determine it is wave32 or wave64
    s_getreg_b32 	s_restore_size, hwreg(HW_REG_IB_STS2,SQ_WAVE_IB_STS2_WAVE64_SHIFT,SQ_WAVE_IB_STS2_WAVE64_SIZE)
    s_or_b32        s_restore_size, s_restore_spi_init_hi,    s_restore_size                                             //share s_wave_size with exec_hi
	
	/* 		global mem offset			*/
	s_mov_b32		s_restore_mem_offset, 0x0								//mem offset initial value = 0

        /*      	restore VGPRs	    */
	//////////////////////////////
  L_RESTORE_VGPR:
  
 	s_mov_b32		exec_lo, 0xFFFFFFFF 													//need every thread from now on   //be consistent with SAVE although can be moved ahead
    s_and_b32       m0, s_restore_size, 1
    s_cmp_eq_u32    m0, 1
    s_cbranch_scc1  L_ENABLE_RESTORE_VGPR_EXEC_HI   
    s_mov_b32		exec_hi, 0x00000000
    s_branch        L_RESTORE_VGPR_NORMAL
  L_ENABLE_RESTORE_VGPR_EXEC_HI:
	s_mov_b32		exec_hi, 0xFFFFFFFF
  L_RESTORE_VGPR_NORMAL:	
	s_getreg_b32 	s_restore_alloc_size, hwreg(HW_REG_GPR_ALLOC,SQ_WAVE_GPR_ALLOC_VGPR_SIZE_SHIFT,SQ_WAVE_GPR_ALLOC_VGPR_SIZE_SIZE) 	//vpgr_size
	s_add_u32 		s_restore_alloc_size, s_restore_alloc_size, 1
	s_lshl_b32 		s_restore_alloc_size, s_restore_alloc_size, 2 							//Number of VGPRs = (vgpr_size + 1) * 4    (non-zero value)
    //determine it is wave32 or wave64
    s_and_b32       m0, s_restore_size, 1
    s_cmp_eq_u32    m0, 1
    s_cbranch_scc1  L_RESTORE_VGPR_WAVE64

    s_lshl_b32		s_restore_buf_rsrc2,  s_restore_alloc_size, 7						    //NUM_RECORDS in bytes (32 threads*4)
	if (SWIZZLE_EN)
		s_add_u32		s_restore_buf_rsrc2, s_restore_buf_rsrc2, 0x0						//FIXME need to use swizzle to enable bounds checking?
	else
		s_mov_b32		s_restore_buf_rsrc2,  0x1000000										//NUM_RECORDS in bytes
	end	

	s_mov_b32		s_restore_mem_offset_save, s_restore_mem_offset							// restore start with v1, v0 will be the last
	s_add_u32		s_restore_mem_offset, s_restore_mem_offset, 128
    s_mov_b32 		m0, 1 																	//VGPR initial index value = 1
	//s_set_gpr_idx_on  m0, 0x8																//M0[7:0] = M0[7:0] and M0[15:12] = 0x8
    //s_add_u32		s_restore_alloc_size, s_restore_alloc_size, 0x8000						//add 0x8000 since we compare m0 against it later, might not need this in gfx10	

  L_RESTORE_VGPR_WAVE32_LOOP: 										
    if(USE_MTBUF_INSTEAD_OF_MUBUF)       
		tbuffer_load_format_x v0, v0, s_restore_buf_rsrc0, s_restore_mem_offset format:BUF_NUM_FORMAT_FLOAT format: BUF_DATA_FORMAT_32 slc:1 glc:1
    else
		buffer_load_dword v0, v0, s_restore_buf_rsrc0, s_restore_mem_offset	slc:1 glc:1	
	end
	s_waitcnt		vmcnt(0)																//ensure data ready
	v_movreld_b32		v0, v0																	//v[0+m0] = v0
    s_add_u32		m0, m0, 1																//next vgpr index
	s_add_u32		s_restore_mem_offset, s_restore_mem_offset, 128							//every buffer_load_dword does 128 bytes
	s_cmp_lt_u32 	m0,	s_restore_alloc_size 												//scc = (m0 < s_restore_alloc_size) ? 1 : 0
	s_cbranch_scc1 	L_RESTORE_VGPR_WAVE32_LOOP														//VGPR restore (except v0) is complete?
	//s_set_gpr_idx_off
																							/* VGPR restore on v0 */
    if(USE_MTBUF_INSTEAD_OF_MUBUF)       
		tbuffer_load_format_x v0, v0, s_restore_buf_rsrc0, s_restore_mem_offset_save format:BUF_NUM_FORMAT_FLOAT format: BUF_DATA_FORMAT_32 slc:1 glc:1
    else
		buffer_load_dword v0, v0, s_restore_buf_rsrc0, s_restore_mem_offset_save	slc:1 glc:1	
	end

    s_branch    L_RESTORE_LDS

  L_RESTORE_VGPR_WAVE64:
    s_lshl_b32		s_restore_buf_rsrc2,  s_restore_alloc_size, 8						    //NUM_RECORDS in bytes (64 threads*4)
	if (SWIZZLE_EN)
		s_add_u32		s_restore_buf_rsrc2, s_restore_buf_rsrc2, 0x0						//FIXME need to use swizzle to enable bounds checking?
	else
		s_mov_b32		s_restore_buf_rsrc2,  0x1000000										//NUM_RECORDS in bytes
	end	

	s_mov_b32		s_restore_mem_offset_save, s_restore_mem_offset							// restore start with v1, v0 will be the last
	s_add_u32		s_restore_mem_offset, s_restore_mem_offset, 256
    s_mov_b32 		m0, 1 																	//VGPR initial index value = 1
  L_RESTORE_VGPR_WAVE64_LOOP: 										
    if(USE_MTBUF_INSTEAD_OF_MUBUF)       
		tbuffer_load_format_x v0, v0, s_restore_buf_rsrc0, s_restore_mem_offset format:BUF_NUM_FORMAT_FLOAT format: BUF_DATA_FORMAT_32 slc:1 glc:1
    else
		buffer_load_dword v0, v0, s_restore_buf_rsrc0, s_restore_mem_offset	slc:1 glc:1	
	end
	s_waitcnt		vmcnt(0)																//ensure data ready
	v_movreld_b32		v0, v0																	//v[0+m0] = v0
    s_add_u32		m0, m0, 1																//next vgpr index
	s_add_u32		s_restore_mem_offset, s_restore_mem_offset, 256							//every buffer_load_dword does 256 bytes
	s_cmp_lt_u32 	m0,	s_restore_alloc_size 												//scc = (m0 < s_restore_alloc_size) ? 1 : 0
	s_cbranch_scc1 	L_RESTORE_VGPR_WAVE64_LOOP														//VGPR restore (except v0) is complete?
	//s_set_gpr_idx_off
    //
    //Below part will be the restore shared vgpr part (new for gfx10)
    s_getreg_b32 	s_restore_alloc_size, hwreg(HW_REG_LDS_ALLOC,SQ_WAVE_LDS_ALLOC_VGPR_SHARED_SIZE_SHIFT,SQ_WAVE_LDS_ALLOC_VGPR_SHARED_SIZE_SIZE) 			//shared_vgpr_size
    s_and_b32		s_restore_alloc_size, s_restore_alloc_size, 0xFFFFFFFF				//shared_vgpr_size is zero?
    s_cbranch_scc0	L_RESTORE_V0													    //no shared_vgpr used? jump to L_SAVE_LDS
    s_lshl_b32 		s_restore_alloc_size, s_restore_alloc_size, 3 						//Number of SHARED_VGPRs = shared_vgpr_size * 8    (non-zero value)
    //m0 now has the value of normal vgpr count, just add the m0 with shared_vgpr count to get the total count.
    //restore shared_vgpr will start from the index of m0
    s_add_u32       s_restore_alloc_size, s_restore_alloc_size, m0
    s_mov_b32		exec_lo, 0xFFFFFFFF
    s_mov_b32		exec_hi, 0x00000000
    L_RESTORE_SHARED_VGPR_WAVE64_LOOP: 
    buffer_load_dword v0, v0, s_restore_buf_rsrc0, s_restore_mem_offset	slc:1 glc:1
    s_waitcnt		vmcnt(0)																//ensure data ready
	v_movreld_b32		v0, v0																	//v[0+m0] = v0
    s_add_u32		m0, m0, 1																//next vgpr index
	s_add_u32		s_restore_mem_offset, s_restore_mem_offset, 128							//every buffer_load_dword does 256 bytes
	s_cmp_lt_u32 	m0,	s_restore_alloc_size 												//scc = (m0 < s_restore_alloc_size) ? 1 : 0
	s_cbranch_scc1 	L_RESTORE_SHARED_VGPR_WAVE64_LOOP														//VGPR restore (except v0) is complete?

    s_mov_b32 exec_hi, 0xFFFFFFFF                                                           //restore back exec_hi before restoring V0!!
	
    /* VGPR restore on v0 */
  L_RESTORE_V0:
    if(USE_MTBUF_INSTEAD_OF_MUBUF)       
		tbuffer_load_format_x v0, v0, s_restore_buf_rsrc0, s_restore_mem_offset_save format:BUF_NUM_FORMAT_FLOAT format: BUF_DATA_FORMAT_32 slc:1 glc:1
    else
		buffer_load_dword v0, v0, s_restore_buf_rsrc0, s_restore_mem_offset_save	slc:1 glc:1	
	end


    /*      	restore LDS	    */
	//////////////////////////////
  L_RESTORE_LDS:

    //Only need to check the first wave    
	/*      the first wave in the threadgroup    */
	s_and_b32		s_restore_tmp, s_restore_size, S_RESTORE_SPI_INIT_FIRST_WAVE_MASK			
	s_cbranch_scc0	L_RESTORE_SGPR
	
    s_mov_b32		exec_lo, 0xFFFFFFFF 													//need every thread from now on   //be consistent with SAVE although can be moved ahead
    s_and_b32       m0, s_restore_size, 1
    s_cmp_eq_u32    m0, 1
    s_cbranch_scc1  L_ENABLE_RESTORE_LDS_EXEC_HI   
    s_mov_b32		exec_hi, 0x00000000
    s_branch        L_RESTORE_LDS_NORMAL
  L_ENABLE_RESTORE_LDS_EXEC_HI:
	s_mov_b32		exec_hi, 0xFFFFFFFF
  L_RESTORE_LDS_NORMAL:	
	s_getreg_b32 	s_restore_alloc_size, hwreg(HW_REG_LDS_ALLOC,SQ_WAVE_LDS_ALLOC_LDS_SIZE_SHIFT,SQ_WAVE_LDS_ALLOC_LDS_SIZE_SIZE) 				//lds_size
	s_and_b32		s_restore_alloc_size, s_restore_alloc_size, 0xFFFFFFFF					//lds_size is zero?
	s_cbranch_scc0	L_RESTORE_SGPR															//no lds used? jump to L_RESTORE_VGPR
	s_lshl_b32 		s_restore_alloc_size, s_restore_alloc_size, 6 							//LDS size in dwords = lds_size * 64dw
	s_lshl_b32 		s_restore_alloc_size, s_restore_alloc_size, 2 							//LDS size in bytes
	s_mov_b32		s_restore_buf_rsrc2,	s_restore_alloc_size							//NUM_RECORDS in bytes
	if (SWIZZLE_EN)
		s_add_u32		s_restore_buf_rsrc2, s_restore_buf_rsrc2, 0x0						//FIXME need to use swizzle to enable bounds checking?
	else
		s_mov_b32		s_restore_buf_rsrc2,  0x1000000										//NUM_RECORDS in bytes
	end

    s_and_b32       m0, s_wave_size, 1
    s_cmp_eq_u32    m0, 1
    s_mov_b32 		m0, 0x0
    s_cbranch_scc1  L_RESTORE_LDS_LOOP_W64

  L_RESTORE_LDS_LOOP_W32:									
	if (SAVE_LDS)
	buffer_load_dword	v0, v0, s_restore_buf_rsrc0, s_restore_mem_offset lds:1
    s_waitcnt 0
	end
    s_add_u32		m0, m0, 128																//every buffer_load_dword does 256 bytes
	s_add_u32		s_restore_mem_offset, s_restore_mem_offset, 128						//mem offset increased by 256 bytes
	s_cmp_lt_u32	m0, s_restore_alloc_size												//scc=(m0 < s_restore_alloc_size) ? 1 : 0
	s_cbranch_scc1  L_RESTORE_LDS_LOOP_W32														//LDS restore is complete?
    s_branch        L_RESTORE_SGPR

  L_RESTORE_LDS_LOOP_W64:									
	if (SAVE_LDS)
	buffer_load_dword	v0, v0, s_restore_buf_rsrc0, s_restore_mem_offset lds:1
    s_waitcnt 0
	end
    s_add_u32		m0, m0, 256																//every buffer_load_dword does 256 bytes
	s_add_u32		s_restore_mem_offset, s_restore_mem_offset, 256							//mem offset increased by 256 bytes
	s_cmp_lt_u32	m0, s_restore_alloc_size												//scc=(m0 < s_restore_alloc_size) ? 1 : 0
	s_cbranch_scc1  L_RESTORE_LDS_LOOP_W64														//LDS restore is complete?

	
    /*      	restore SGPRs	    */
	//////////////////////////////
	//s_getreg_b32 	s_restore_alloc_size, hwreg(HW_REG_GPR_ALLOC,SQ_WAVE_GPR_ALLOC_SGPR_SIZE_SHIFT,SQ_WAVE_GPR_ALLOC_SGPR_SIZE_SIZE) 				//spgr_size
	//s_add_u32 		s_restore_alloc_size, s_restore_alloc_size, 1
	//s_lshl_b32 		s_restore_alloc_size, s_restore_alloc_size, 4 							//Number of SGPRs = (sgpr_size + 1) * 16   (non-zero value)
	//s_lshl_b32 		s_restore_alloc_size, s_restore_alloc_size, 3 							//Number of SGPRs = (sgpr_size + 1) * 8   (non-zero value)
  L_RESTORE_SGPR:
    //need to look at it is wave32 or wave64
    s_and_b32       m0, s_restore_size, 1
    s_cmp_eq_u32    m0, 1
    s_cbranch_scc1  L_RESTORE_SGPR_VMEM_WAVE64
	if (SGPR_SAVE_USE_SQC)
		s_lshl_b32		s_restore_buf_rsrc2,	s_sgpr_save_num, 2						//NUM_RECORDS in bytes 
	else
        s_lshl_b32		s_restore_buf_rsrc2,	s_sgpr_save_num, 7						//NUM_RECORDS in bytes (32 threads)
    end
    s_branch        L_RESTORE_SGPR_CONT
  L_RESTORE_SGPR_VMEM_WAVE64:
    if (SGPR_SAVE_USE_SQC)
		s_lshl_b32		s_restore_buf_rsrc2,	s_sgpr_save_num, 2						//NUM_RECORDS in bytes 
	else
		s_lshl_b32		s_restore_buf_rsrc2,	s_sgpr_save_num, 8						//NUM_RECORDS in bytes (64 threads)
	end

  L_RESTORE_SGPR_CONT:
	if (SWIZZLE_EN)
		s_add_u32		s_restore_buf_rsrc2, s_restore_buf_rsrc2, 0x0						//FIXME need to use swizzle to enable bounds checking?
	else
		s_mov_b32		s_restore_buf_rsrc2,  0x1000000										//NUM_RECORDS in bytes
	end

    s_and_b32       m0, s_restore_size, 1
    s_cmp_eq_u32    m0, 1
    s_cbranch_scc1  L_RESTORE_SGPR_WAVE64

    read_sgpr_from_mem_wave32(s_restore_tmp, s_restore_buf_rsrc0, s_restore_mem_offset, SGPR_SAVE_USE_SQC)		//save s0 to s_restore_tmp
	s_mov_b32 		m0, 0x1

  L_RESTORE_SGPR_LOOP_WAVE32:
    read_sgpr_from_mem_wave32(s0, s_restore_buf_rsrc0, s_restore_mem_offset, SGPR_SAVE_USE_SQC)															//PV: further performance improvement can be made
	s_waitcnt		lgkmcnt(0)																//ensure data ready
	s_movreld_b32 	s0, s0                                                                  //s[0+m0] = s0
    s_nop 0                                                                                 // hazard SALU M0=> S_MOVREL
	s_add_u32		m0, m0, 1																//next sgpr index
	s_cmp_lt_u32 	m0, s_sgpr_save_num												//scc = (m0 < s_restore_alloc_size) ? 1 : 0
	s_cbranch_scc1 	L_RESTORE_SGPR_LOOP_WAVE32														//SGPR restore (except s0) is complete?
	s_mov_b32		s0, s_restore_tmp															/* SGPR restore on s0 */
    s_branch        L_RESTORE_HWREG
  
  L_RESTORE_SGPR_WAVE64:
	read_sgpr_from_mem_wave64(s_restore_tmp, s_restore_buf_rsrc0, s_restore_mem_offset, SGPR_SAVE_USE_SQC)		//save s0 to s_restore_tmp
	s_mov_b32 		m0, 0x1																				//SGPR initial index value =1	//go on with with s1
	
  L_RESTORE_SGPR_LOOP_WAVE64: 																					
	read_sgpr_from_mem_wave64(s0, s_restore_buf_rsrc0, s_restore_mem_offset, SGPR_SAVE_USE_SQC)															//PV: further performance improvement can be made
	s_waitcnt		lgkmcnt(0)																//ensure data ready
	s_movreld_b32 	s0, s0                                                                  //s[0+m0] = s0
    s_nop 0                                                                                 // hazard SALU M0=> S_MOVREL
	s_add_u32		m0, m0, 1																//next sgpr index
	s_cmp_lt_u32 	m0, s_sgpr_save_num												//scc = (m0 < s_restore_alloc_size) ? 1 : 0
	s_cbranch_scc1 	L_RESTORE_SGPR_LOOP_WAVE64														//SGPR restore (except s0) is complete?
	s_mov_b32		s0, s_restore_tmp															/* SGPR restore on s0 */

	
    /* 		restore HW registers	*/
	//////////////////////////////
  L_RESTORE_HWREG:
    s_mov_b32		s_restore_buf_rsrc2, 0x4												//NUM_RECORDS	in bytes
	if (SWIZZLE_EN)
		s_add_u32		s_restore_buf_rsrc2, s_restore_buf_rsrc2, 0x0						//FIXME need to use swizzle to enable bounds checking?
	else
		s_mov_b32		s_restore_buf_rsrc2,  0x1000000										//NUM_RECORDS in bytes
	end

    s_and_b32       m0, s_restore_size, 1
    s_cmp_eq_u32    m0, 1
    s_cbranch_scc1  L_RESTORE_HWREG_WAVE64

    read_sgpr_from_mem_wave32(s_restore_m0, s_restore_buf_rsrc0, s_restore_mem_offset, SGPR_SAVE_USE_SQC)					//M0
	read_sgpr_from_mem_wave32(s_restore_pc_lo, s_restore_buf_rsrc0, s_restore_mem_offset, SGPR_SAVE_USE_SQC)				//PC
	read_sgpr_from_mem_wave32(s_restore_pc_hi, s_restore_buf_rsrc0, s_restore_mem_offset, SGPR_SAVE_USE_SQC)
	read_sgpr_from_mem_wave32(s_restore_exec_lo, s_restore_buf_rsrc0, s_restore_mem_offset, SGPR_SAVE_USE_SQC)				//EXEC
	read_sgpr_from_mem_wave32(s_restore_exec_hi, s_restore_buf_rsrc0, s_restore_mem_offset, SGPR_SAVE_USE_SQC)
	read_sgpr_from_mem_wave32(s_restore_status, s_restore_buf_rsrc0, s_restore_mem_offset, SGPR_SAVE_USE_SQC)				//STATUS
	read_sgpr_from_mem_wave32(s_restore_trapsts, s_restore_buf_rsrc0, s_restore_mem_offset, SGPR_SAVE_USE_SQC)				//TRAPSTS
    //read_sgpr_from_mem_wave32(xnack_mask_lo, s_restore_buf_rsrc0, s_restore_mem_offset, SGPR_SAVE_USE_SQC)					//XNACK_MASK_LO
	//read_sgpr_from_mem_wave32(xnack_mask_hi, s_restore_buf_rsrc0, s_restore_mem_offset, SGPR_SAVE_USE_SQC)					//XNACK_MASK_HI
    read_sgpr_from_mem_wave32(s_restore_xnack_mask, s_restore_buf_rsrc0, s_restore_mem_offset, SGPR_SAVE_USE_SQC)					//XNACK_MASK
	read_sgpr_from_mem_wave32(s_restore_mode, s_restore_buf_rsrc0, s_restore_mem_offset, SGPR_SAVE_USE_SQC)				//MODE
    if(SAVE_RESTORE_HWID_DDID)
    read_sgpr_from_mem_wave32(s_restore_hwid1, s_restore_buf_rsrc0, s_restore_mem_offset, SGPR_SAVE_USE_SQC)				//HW_ID1
    end
    s_branch        L_RESTORE_HWREG_FINISH

  L_RESTORE_HWREG_WAVE64:
	read_sgpr_from_mem_wave64(s_restore_m0, s_restore_buf_rsrc0, s_restore_mem_offset, SGPR_SAVE_USE_SQC)					//M0
	read_sgpr_from_mem_wave64(s_restore_pc_lo, s_restore_buf_rsrc0, s_restore_mem_offset, SGPR_SAVE_USE_SQC)				//PC
	read_sgpr_from_mem_wave64(s_restore_pc_hi, s_restore_buf_rsrc0, s_restore_mem_offset, SGPR_SAVE_USE_SQC)
	read_sgpr_from_mem_wave64(s_restore_exec_lo, s_restore_buf_rsrc0, s_restore_mem_offset, SGPR_SAVE_USE_SQC)				//EXEC
	read_sgpr_from_mem_wave64(s_restore_exec_hi, s_restore_buf_rsrc0, s_restore_mem_offset, SGPR_SAVE_USE_SQC)
	read_sgpr_from_mem_wave64(s_restore_status, s_restore_buf_rsrc0, s_restore_mem_offset, SGPR_SAVE_USE_SQC)				//STATUS
	read_sgpr_from_mem_wave64(s_restore_trapsts, s_restore_buf_rsrc0, s_restore_mem_offset, SGPR_SAVE_USE_SQC)				//TRAPSTS
    //read_sgpr_from_mem_wave64(xnack_mask_lo, s_restore_buf_rsrc0, s_restore_mem_offset, SGPR_SAVE_USE_SQC)					//XNACK_MASK_LO
	//read_sgpr_from_mem_wave64(xnack_mask_hi, s_restore_buf_rsrc0, s_restore_mem_offset, SGPR_SAVE_USE_SQC)					//XNACK_MASK_HI
    read_sgpr_from_mem_wave64(s_restore_xnack_mask, s_restore_buf_rsrc0, s_restore_mem_offset, SGPR_SAVE_USE_SQC)					//XNACK_MASK
	read_sgpr_from_mem_wave64(s_restore_mode, s_restore_buf_rsrc0, s_restore_mem_offset, SGPR_SAVE_USE_SQC)				//MODE
    if(SAVE_RESTORE_HWID_DDID)
    read_sgpr_from_mem_wave64(s_restore_hwid1, s_restore_buf_rsrc0, s_restore_mem_offset, SGPR_SAVE_USE_SQC)				//HW_ID1
    end
  L_RESTORE_HWREG_FINISH:
	s_waitcnt		lgkmcnt(0)																						//from now on, it is safe to restore STATUS and IB_STS
  


    if(SAVE_RESTORE_HWID_DDID)
  L_RESTORE_DDID:
    s_mov_b32      m0, s_restore_hwid1                                                      //virture ttrace support: The save-context handler records the SE/SA/WGP/SIMD/wave of the original wave
    s_ttracedata                                                                            //and then can output it as SHADER_DATA to ttrace on restore to provide a correlation across the save-restore
                                    
    s_mov_b32		s_restore_buf_rsrc2, 0x4												//NUM_RECORDS	in bytes
	if (SWIZZLE_EN)
		s_add_u32		s_restore_buf_rsrc2, s_restore_buf_rsrc2, 0x0						//FIXME need to use swizzle to enable bounds checking?
	else
		s_mov_b32		s_restore_buf_rsrc2,  0x1000000										//NUM_RECORDS in bytes
	end

    s_and_b32       m0, s_restore_size, 1
    s_cmp_eq_u32    m0, 1
    s_cbranch_scc1  L_RESTORE_DDID_WAVE64

    read_sgpr_from_mem_wave32(s_restore_ddid, s_restore_buf_rsrc0, s_restore_mem_offset, SGPR_SAVE_USE_SQC)	
    s_branch        L_RESTORE_DDID_FINISH
  L_RESTORE_DDID_WAVE64:
    read_sgpr_from_mem_wave64(s_restore_ddid, s_restore_buf_rsrc0, s_restore_mem_offset, SGPR_SAVE_USE_SQC)	

  L_RESTORE_DDID_FINISH:
    s_waitcnt		lgkmcnt(0)
    //s_mov_b32      m0, s_restore_ddid
    //s_ttracedata   
    if (RESTORE_DDID_IN_SGPR18)
        s_mov_b32   s18, s_restore_ddid
	end	
    
    end   

	s_and_b32 s_restore_pc_hi, s_restore_pc_hi, 0x0000ffff    	//pc[47:32]        //Do it here in order not to affect STATUS

	//for normal save & restore, the saved PC points to the next inst to execute, no adjustment needs to be made, otherwise:
	if ((EMU_RUN_HACK) && (!EMU_RUN_HACK_RESTORE_NORMAL))
		s_add_u32 s_restore_pc_lo, s_restore_pc_lo, 8            //pc[31:0]+8	  //two back-to-back s_trap are used (first for save and second for restore)
		s_addc_u32	s_restore_pc_hi, s_restore_pc_hi, 0x0		 //carry bit over
	end	
	if ((EMU_RUN_HACK) && (EMU_RUN_HACK_RESTORE_NORMAL))	      
		s_add_u32 s_restore_pc_lo, s_restore_pc_lo, 4            //pc[31:0]+4     // save is hack through s_trap but restore is normal
		s_addc_u32	s_restore_pc_hi, s_restore_pc_hi, 0x0		 //carry bit over
	end
	
	s_mov_b32 		m0, 		s_restore_m0
	s_mov_b32 		exec_lo, 	s_restore_exec_lo
	s_mov_b32 		exec_hi, 	s_restore_exec_hi
	
	s_and_b32		s_restore_m0, SQ_WAVE_TRAPSTS_PRE_SAVECTX_MASK, s_restore_trapsts
	s_setreg_b32	hwreg(HW_REG_TRAPSTS, SQ_WAVE_TRAPSTS_PRE_SAVECTX_SHIFT, SQ_WAVE_TRAPSTS_PRE_SAVECTX_SIZE), s_restore_m0
    s_setreg_b32    hwreg(HW_REG_SHADER_XNACK_MASK), s_restore_xnack_mask         //restore xnack_mask
	s_and_b32		s_restore_m0, SQ_WAVE_TRAPSTS_POST_SAVECTX_MASK, s_restore_trapsts
	s_lshr_b32		s_restore_m0, s_restore_m0, SQ_WAVE_TRAPSTS_POST_SAVECTX_SHIFT
	s_setreg_b32	hwreg(HW_REG_TRAPSTS, SQ_WAVE_TRAPSTS_POST_SAVECTX_SHIFT, SQ_WAVE_TRAPSTS_POST_SAVECTX_SIZE), s_restore_m0
	//s_setreg_b32 	hwreg(HW_REG_TRAPSTS), 	s_restore_trapsts      //don't overwrite SAVECTX bit as it may be set through external SAVECTX during restore
	s_setreg_b32 	hwreg(HW_REG_MODE), 	s_restore_mode
	//reuse s_restore_m0 as a temp register
	s_and_b32		s_restore_m0, s_restore_pc_hi, S_SAVE_PC_HI_RCNT_MASK
	s_lshr_b32		s_restore_m0, s_restore_m0, S_SAVE_PC_HI_RCNT_SHIFT
	s_lshl_b32		s_restore_m0, s_restore_m0, SQ_WAVE_IB_STS_RCNT_SHIFT
	s_mov_b32		s_restore_tmp, 0x0																				//IB_STS is zero
	s_or_b32		s_restore_tmp, s_restore_tmp, s_restore_m0
	s_and_b32		s_restore_m0, s_restore_pc_hi, S_SAVE_PC_HI_FIRST_REPLAY_MASK
	s_lshr_b32		s_restore_m0, s_restore_m0, S_SAVE_PC_HI_FIRST_REPLAY_SHIFT
	s_lshl_b32		s_restore_m0, s_restore_m0, SQ_WAVE_IB_STS_FIRST_REPLAY_SHIFT
	s_or_b32		s_restore_tmp, s_restore_tmp, s_restore_m0
    s_and_b32       s_restore_m0, s_restore_status, SQ_WAVE_STATUS_INST_ATC_MASK 
    s_lshr_b32		s_restore_m0, s_restore_m0, SQ_WAVE_STATUS_INST_ATC_SHIFT
	s_setreg_b32 	hwreg(HW_REG_IB_STS), 	s_restore_tmp
	s_setreg_b32 	hwreg(HW_REG_STATUS), 	s_restore_status

	s_barrier													//barrier to ensure the readiness of LDS before access attemps from any other wave in the same TG //FIXME not performance-optimal at this time
	
	
//	s_rfe_b64 s_restore_pc_lo                              		//Return to the main shader program and resume execution
    s_rfe_b64  s_restore_pc_lo            // s_restore_m0[0] is used to set STATUS.inst_atc 


/**************************************************************************/
/*                     	the END								              */
/**************************************************************************/	
L_END_PGM:	
	s_endpgm
	
end	


/**************************************************************************/
/*                     	the helper functions							  */
/**************************************************************************/
function write_sgpr_to_mem_wave32(s, s_rsrc, s_mem_offset, use_sqc, use_mtbuf)
	if (use_sqc)
		s_mov_b32 exec_lo, m0					//assuming exec_lo is not needed anymore from this point on
		s_mov_b32 m0, s_mem_offset
		s_buffer_store_dword s, s_rsrc, m0		glc:1	
		s_add_u32		s_mem_offset, s_mem_offset, 4
		s_mov_b32	m0, exec_lo
    elsif (use_mtbuf)
        v_mov_b32	v0,	s
        tbuffer_store_format_x v0, v0, s_rsrc, s_mem_offset format:BUF_NUM_FORMAT_FLOAT format: BUF_DATA_FORMAT_32 slc:1 glc:1
		s_add_u32		s_mem_offset, s_mem_offset, 128
    else 
        v_mov_b32	v0,	s
		buffer_store_dword	v0, v0, s_rsrc, s_mem_offset	slc:1 glc:1
        s_add_u32		s_mem_offset, s_mem_offset, 128
	end
end

function write_sgpr_to_mem_wave64(s, s_rsrc, s_mem_offset, use_sqc, use_mtbuf)
	if (use_sqc)
		s_mov_b32 exec_lo, m0					//assuming exec_lo is not needed anymore from this point on
		s_mov_b32 m0, s_mem_offset
		s_buffer_store_dword s, s_rsrc, m0		glc:1	
		s_add_u32		s_mem_offset, s_mem_offset, 4
		s_mov_b32	m0, exec_lo
    elsif (use_mtbuf)
        v_mov_b32	v0,	s
        tbuffer_store_format_x v0, v0, s_rsrc, s_mem_offset format:BUF_NUM_FORMAT_FLOAT format: BUF_DATA_FORMAT_32 slc:1 glc:1
		s_add_u32		s_mem_offset, s_mem_offset, 256
    else 
        v_mov_b32	v0,	s
		buffer_store_dword	v0, v0, s_rsrc, s_mem_offset	slc:1 glc:1
        s_add_u32		s_mem_offset, s_mem_offset, 256
	end
end

function read_sgpr_from_mem_wave32(s, s_rsrc, s_mem_offset, use_sqc)
	s_buffer_load_dword s, s_rsrc, s_mem_offset		glc:1
	if (use_sqc)
		s_add_u32		s_mem_offset, s_mem_offset, 4
	else
        s_add_u32		s_mem_offset, s_mem_offset, 128
	end
end

function read_sgpr_from_mem_wave64(s, s_rsrc, s_mem_offset, use_sqc)
	s_buffer_load_dword s, s_rsrc, s_mem_offset		glc:1
	if (use_sqc)
		s_add_u32		s_mem_offset, s_mem_offset, 4
	else
        s_add_u32		s_mem_offset, s_mem_offset, 256
	end
end

