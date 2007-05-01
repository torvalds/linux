/*
 * Device driver for the SYMBIOS/LSILOGIC 53C8XX and 53C1010 family 
 * of PCI-SCSI IO processors.
 *
 * Copyright (C) 1999-2001  Gerard Roudier <groudier@free.fr>
 *
 * This driver is derived from the Linux sym53c8xx driver.
 * Copyright (C) 1998-2000  Gerard Roudier
 *
 * The sym53c8xx driver is derived from the ncr53c8xx driver that had been 
 * a port of the FreeBSD ncr driver to Linux-1.2.13.
 *
 * The original ncr driver has been written for 386bsd and FreeBSD by
 *         Wolfgang Stanglmeier        <wolf@cologne.de>
 *         Stefan Esser                <se@mi.Uni-Koeln.de>
 * Copyright (C) 1994  Wolfgang Stanglmeier
 *
 * Other major contributions:
 *
 * NVRAM detection and reading.
 * Copyright (C) 1997 Richard Waltham <dormouse@farsrobt.demon.co.uk>
 *
 *-----------------------------------------------------------------------------
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 *  Scripts for SYMBIOS-Processor
 *
 *  We have to know the offsets of all labels before we reach 
 *  them (for forward jumps). Therefore we declare a struct 
 *  here. If you make changes inside the script,
 *
 *  DONT FORGET TO CHANGE THE LENGTHS HERE!
 */

/*
 *  Script fragments which are loaded into the on-chip RAM 
 *  of 825A, 875, 876, 895, 895A, 896 and 1010 chips.
 *  Must not exceed 4K bytes.
 */
struct SYM_FWA_SCR {
	u32 start		[ 14];
	u32 getjob_begin	[  4];
	u32 getjob_end		[  4];
#ifdef SYM_CONF_TARGET_ROLE_SUPPORT
	u32 select		[  6];
#else
	u32 select		[  4];
#endif
#if	SYM_CONF_DMA_ADDRESSING_MODE == 2
	u32 is_dmap_dirty	[  4];
#endif
	u32 wf_sel_done		[  2];
	u32 sel_done		[  2];
	u32 send_ident		[  2];
#ifdef SYM_CONF_IARB_SUPPORT
	u32 select2		[  8];
#else
	u32 select2		[  2];
#endif
	u32 command		[  2];
	u32 dispatch		[ 28];
	u32 sel_no_cmd		[ 10];
	u32 init		[  6];
	u32 clrack		[  4];
	u32 datai_done		[ 10];
	u32 datai_done_wsr	[ 20];
	u32 datao_done		[ 10];
	u32 datao_done_wss	[  6];
	u32 datai_phase		[  4];
	u32 datao_phase		[  6];
	u32 msg_in		[  2];
	u32 msg_in2		[ 10];
#ifdef SYM_CONF_IARB_SUPPORT
	u32 status		[ 14];
#else
	u32 status		[ 10];
#endif
	u32 complete		[  6];
	u32 complete2		[ 12];
	u32 done		[ 14];
	u32 done_end		[  2];
	u32 complete_error	[  4];
	u32 save_dp		[ 12];
	u32 restore_dp		[  8];
	u32 disconnect		[ 12];
#ifdef SYM_CONF_IARB_SUPPORT
	u32 idle		[  4];
#else
	u32 idle		[  2];
#endif
#ifdef SYM_CONF_IARB_SUPPORT
	u32 ungetjob		[  6];
#else
	u32 ungetjob		[  4];
#endif
#ifdef SYM_CONF_TARGET_ROLE_SUPPORT
	u32 reselect		[  4];
#else
	u32 reselect		[  2];
#endif
	u32 reselected		[ 22];
	u32 resel_scntl4	[ 20];
	u32 resel_lun0		[  6];
#if   SYM_CONF_MAX_TASK*4 > 512
	u32 resel_tag		[ 26];
#elif SYM_CONF_MAX_TASK*4 > 256
	u32 resel_tag		[ 20];
#else
	u32 resel_tag		[ 16];
#endif
	u32 resel_dsa		[  2];
	u32 resel_dsa1		[  4];
	u32 resel_no_tag	[  6];
	u32 data_in		[SYM_CONF_MAX_SG * 2];
	u32 data_in2		[  4];
	u32 data_out		[SYM_CONF_MAX_SG * 2];
	u32 data_out2		[  4];
	u32 pm0_data		[ 12];
	u32 pm0_data_out	[  6];
	u32 pm0_data_end	[  6];
	u32 pm1_data		[ 12];
	u32 pm1_data_out	[  6];
	u32 pm1_data_end	[  6];
};

/*
 *  Script fragments which stay in main memory for all chips 
 *  except for chips that support 8K on-chip RAM.
 */
struct SYM_FWB_SCR {
	u32 start64		[  2];
	u32 no_data		[  2];
#ifdef SYM_CONF_TARGET_ROLE_SUPPORT
	u32 sel_for_abort	[ 18];
#else
	u32 sel_for_abort	[ 16];
#endif
	u32 sel_for_abort_1	[  2];
	u32 msg_in_etc		[ 12];
	u32 msg_received	[  4];
	u32 msg_weird_seen	[  4];
	u32 msg_extended	[ 20];
	u32 msg_bad		[  6];
	u32 msg_weird		[  4];
	u32 msg_weird1		[  8];

	u32 wdtr_resp		[  6];
	u32 send_wdtr		[  4];
	u32 sdtr_resp		[  6];
	u32 send_sdtr		[  4];
	u32 ppr_resp		[  6];
	u32 send_ppr		[  4];
	u32 nego_bad_phase	[  4];
	u32 msg_out		[  4];
	u32 msg_out_done	[  4];
	u32 data_ovrun		[  2];
	u32 data_ovrun1		[ 22];
	u32 data_ovrun2		[  8];
	u32 abort_resel		[ 16];
	u32 resend_ident	[  4];
	u32 ident_break		[  4];
	u32 ident_break_atn	[  4];
	u32 sdata_in		[  6];
	u32 resel_bad_lun	[  4];
	u32 bad_i_t_l		[  4];
	u32 bad_i_t_l_q		[  4];
	u32 bad_status		[  6];
	u32 pm_handle		[ 20];
	u32 pm_handle1		[  4];
	u32 pm_save		[  4];
	u32 pm0_save		[ 12];
	u32 pm_save_end		[  4];
	u32 pm1_save		[ 14];

	/* WSR handling */
	u32 pm_wsr_handle	[ 38];
	u32 wsr_ma_helper	[  4];

	/* Data area */
	u32 zero		[  1];
	u32 scratch		[  1];
	u32 pm0_data_addr	[  1];
	u32 pm1_data_addr	[  1];
	u32 done_pos		[  1];
	u32 startpos		[  1];
	u32 targtbl		[  1];
};

/*
 *  Script fragments used at initialisations.
 *  Only runs out of main memory.
 */
struct SYM_FWZ_SCR {
	u32 snooptest		[  6];
	u32 snoopend		[  2];
};

static struct SYM_FWA_SCR SYM_FWA_SCR = {
/*--------------------------< START >----------------------------*/ {
	/*
	 *  Switch the LED on.
	 *  Will be patched with a NO_OP if LED
	 *  not needed or not desired.
	 */
	SCR_REG_REG (gpreg, SCR_AND, 0xfe),
		0,
	/*
	 *      Clear SIGP.
	 */
	SCR_FROM_REG (ctest2),
		0,
	/*
	 *  Stop here if the C code wants to perform 
	 *  some error recovery procedure manually.
	 *  (Indicate this by setting SEM in ISTAT)
	 */
	SCR_FROM_REG (istat),
		0,
	/*
	 *  Report to the C code the next position in 
	 *  the start queue the SCRIPTS will schedule.
	 *  The C code must not change SCRATCHA.
	 */
	SCR_LOAD_ABS (scratcha, 4),
		PADDR_B (startpos),
	SCR_INT ^ IFTRUE (MASK (SEM, SEM)),
		SIR_SCRIPT_STOPPED,
	/*
	 *  Start the next job.
	 *
	 *  @DSA     = start point for this job.
	 *  SCRATCHA = address of this job in the start queue.
	 *
	 *  We will restore startpos with SCRATCHA if we fails the 
	 *  arbitration or if it is the idle job.
	 *
	 *  The below GETJOB_BEGIN to GETJOB_END section of SCRIPTS 
	 *  is a critical path. If it is partially executed, it then 
	 *  may happen that the job address is not yet in the DSA 
	 *  and the next queue position points to the next JOB.
	 */
	SCR_LOAD_ABS (dsa, 4),
		PADDR_B (startpos),
	SCR_LOAD_REL (temp, 4),
		4,
}/*-------------------------< GETJOB_BEGIN >---------------------*/,{
	SCR_STORE_ABS (temp, 4),
		PADDR_B (startpos),
	SCR_LOAD_REL (dsa, 4),
		0,
}/*-------------------------< GETJOB_END >-----------------------*/,{
	SCR_LOAD_REL (temp, 4),
		0,
	SCR_RETURN,
		0,
}/*-------------------------< SELECT >---------------------------*/,{
	/*
	 *  DSA	contains the address of a scheduled
	 *  	data structure.
	 *
	 *  SCRATCHA contains the address of the start queue  
	 *  	entry which points to the next job.
	 *
	 *  Set Initiator mode.
	 *
	 *  (Target mode is left as an exercise for the reader)
	 */
#ifdef SYM_CONF_TARGET_ROLE_SUPPORT
	SCR_CLR (SCR_TRG),
		0,
#endif
	/*
	 *      And try to select this target.
	 */
	SCR_SEL_TBL_ATN ^ offsetof (struct sym_dsb, select),
		PADDR_A (ungetjob),
	/*
	 *  Now there are 4 possibilities:
	 *
	 *  (1) The chip loses arbitration.
	 *  This is ok, because it will try again,
	 *  when the bus becomes idle.
	 *  (But beware of the timeout function!)
	 *
	 *  (2) The chip is reselected.
	 *  Then the script processor takes the jump
	 *  to the RESELECT label.
	 *
	 *  (3) The chip wins arbitration.
	 *  Then it will execute SCRIPTS instruction until 
	 *  the next instruction that checks SCSI phase.
	 *  Then will stop and wait for selection to be 
	 *  complete or selection time-out to occur.
	 *
	 *  After having won arbitration, the SCRIPTS  
	 *  processor is able to execute instructions while 
	 *  the SCSI core is performing SCSI selection.
	 */
	/*
	 *      Initialize the status registers
	 */
	SCR_LOAD_REL (scr0, 4),
		offsetof (struct sym_ccb, phys.head.status),
	/*
	 *  We may need help from CPU if the DMA segment 
	 *  registers aren't up-to-date for this IO.
	 *  Patched with NOOP for chips that donnot 
	 *  support DAC addressing.
	 */
#if	SYM_CONF_DMA_ADDRESSING_MODE == 2
}/*-------------------------< IS_DMAP_DIRTY >--------------------*/,{
	SCR_FROM_REG (HX_REG),
		0,
	SCR_INT ^ IFTRUE (MASK (HX_DMAP_DIRTY, HX_DMAP_DIRTY)),
		SIR_DMAP_DIRTY,
#endif
}/*-------------------------< WF_SEL_DONE >----------------------*/,{
	SCR_INT ^ IFFALSE (WHEN (SCR_MSG_OUT)),
		SIR_SEL_ATN_NO_MSG_OUT,
}/*-------------------------< SEL_DONE >-------------------------*/,{
	/*
	 *  C1010-33 errata work-around.
	 *  Due to a race, the SCSI core may not have 
	 *  loaded SCNTL3 on SEL_TBL instruction.
	 *  We reload it once phase is stable.
	 *  Patched with a NOOP for other chips.
	 */
	SCR_LOAD_REL (scntl3, 1),
		offsetof(struct sym_dsb, select.sel_scntl3),
}/*-------------------------< SEND_IDENT >-----------------------*/,{
	/*
	 *  Selection complete.
	 *  Send the IDENTIFY and possibly the TAG message 
	 *  and negotiation message if present.
	 */
	SCR_MOVE_TBL ^ SCR_MSG_OUT,
		offsetof (struct sym_dsb, smsg),
}/*-------------------------< SELECT2 >--------------------------*/,{
#ifdef SYM_CONF_IARB_SUPPORT
	/*
	 *  Set IMMEDIATE ARBITRATION if we have been given 
	 *  a hint to do so. (Some job to do after this one).
	 */
	SCR_FROM_REG (HF_REG),
		0,
	SCR_JUMPR ^ IFFALSE (MASK (HF_HINT_IARB, HF_HINT_IARB)),
		8,
	SCR_REG_REG (scntl1, SCR_OR, IARB),
		0,
#endif
	/*
	 *  Anticipate the COMMAND phase.
	 *  This is the PHASE we expect at this point.
	 */
	SCR_JUMP ^ IFFALSE (WHEN (SCR_COMMAND)),
		PADDR_A (sel_no_cmd),
}/*-------------------------< COMMAND >--------------------------*/,{
	/*
	 *  ... and send the command
	 */
	SCR_MOVE_TBL ^ SCR_COMMAND,
		offsetof (struct sym_dsb, cmd),
}/*-------------------------< DISPATCH >-------------------------*/,{
	/*
	 *  MSG_IN is the only phase that shall be 
	 *  entered at least once for each (re)selection.
	 *  So we test it first.
	 */
	SCR_JUMP ^ IFTRUE (WHEN (SCR_MSG_IN)),
		PADDR_A (msg_in),
	SCR_JUMP ^ IFTRUE (IF (SCR_DATA_OUT)),
		PADDR_A (datao_phase),
	SCR_JUMP ^ IFTRUE (IF (SCR_DATA_IN)),
		PADDR_A (datai_phase),
	SCR_JUMP ^ IFTRUE (IF (SCR_STATUS)),
		PADDR_A (status),
	SCR_JUMP ^ IFTRUE (IF (SCR_COMMAND)),
		PADDR_A (command),
	SCR_JUMP ^ IFTRUE (IF (SCR_MSG_OUT)),
		PADDR_B (msg_out),
	/*
	 *  Discard as many illegal phases as 
	 *  required and tell the C code about.
	 */
	SCR_JUMPR ^ IFFALSE (WHEN (SCR_ILG_OUT)),
		16,
	SCR_MOVE_ABS (1) ^ SCR_ILG_OUT,
		HADDR_1 (scratch),
	SCR_JUMPR ^ IFTRUE (WHEN (SCR_ILG_OUT)),
		-16,
	SCR_JUMPR ^ IFFALSE (WHEN (SCR_ILG_IN)),
		16,
	SCR_MOVE_ABS (1) ^ SCR_ILG_IN,
		HADDR_1 (scratch),
	SCR_JUMPR ^ IFTRUE (WHEN (SCR_ILG_IN)),
		-16,
	SCR_INT,
		SIR_BAD_PHASE,
	SCR_JUMP,
		PADDR_A (dispatch),
}/*-------------------------< SEL_NO_CMD >-----------------------*/,{
	/*
	 *  The target does not switch to command 
	 *  phase after IDENTIFY has been sent.
	 *
	 *  If it stays in MSG OUT phase send it 
	 *  the IDENTIFY again.
	 */
	SCR_JUMP ^ IFTRUE (WHEN (SCR_MSG_OUT)),
		PADDR_B (resend_ident),
	/*
	 *  If target does not switch to MSG IN phase 
	 *  and we sent a negotiation, assert the 
	 *  failure immediately.
	 */
	SCR_JUMP ^ IFTRUE (WHEN (SCR_MSG_IN)),
		PADDR_A (dispatch),
	SCR_FROM_REG (HS_REG),
		0,
	SCR_INT ^ IFTRUE (DATA (HS_NEGOTIATE)),
		SIR_NEGO_FAILED,
	/*
	 *  Jump to dispatcher.
	 */
	SCR_JUMP,
		PADDR_A (dispatch),
}/*-------------------------< INIT >-----------------------------*/,{
	/*
	 *  Wait for the SCSI RESET signal to be 
	 *  inactive before restarting operations, 
	 *  since the chip may hang on SEL_ATN 
	 *  if SCSI RESET is active.
	 */
	SCR_FROM_REG (sstat0),
		0,
	SCR_JUMPR ^ IFTRUE (MASK (IRST, IRST)),
		-16,
	SCR_JUMP,
		PADDR_A (start),
}/*-------------------------< CLRACK >---------------------------*/,{
	/*
	 *  Terminate possible pending message phase.
	 */
	SCR_CLR (SCR_ACK),
		0,
	SCR_JUMP,
		PADDR_A (dispatch),
}/*-------------------------< DATAI_DONE >-----------------------*/,{
	/*
	 *  Save current pointer to LASTP.
	 */
	SCR_STORE_REL (temp, 4),
		offsetof (struct sym_ccb, phys.head.lastp),
	/*
	 *  If the SWIDE is not full, jump to dispatcher.
	 *  We anticipate a STATUS phase.
	 */
	SCR_FROM_REG (scntl2),
		0,
	SCR_JUMP ^ IFTRUE (MASK (WSR, WSR)),
		PADDR_A (datai_done_wsr),
	SCR_JUMP ^ IFTRUE (WHEN (SCR_STATUS)),
		PADDR_A (status),
	SCR_JUMP,
		PADDR_A (dispatch),
}/*-------------------------< DATAI_DONE_WSR >-------------------*/,{
	/*
	 *  The SWIDE is full.
	 *  Clear this condition.
	 */
	SCR_REG_REG (scntl2, SCR_OR, WSR),
		0,
	/*
	 *  We are expecting an IGNORE RESIDUE message 
	 *  from the device, otherwise we are in data 
	 *  overrun condition. Check against MSG_IN phase.
	 */
	SCR_INT ^ IFFALSE (WHEN (SCR_MSG_IN)),
		SIR_SWIDE_OVERRUN,
	SCR_JUMP ^ IFFALSE (WHEN (SCR_MSG_IN)),
		PADDR_A (dispatch),
	/*
	 *  We are in MSG_IN phase,
	 *  Read the first byte of the message.
	 *  If it is not an IGNORE RESIDUE message,
	 *  signal overrun and jump to message 
	 *  processing.
	 */
	SCR_MOVE_ABS (1) ^ SCR_MSG_IN,
		HADDR_1 (msgin[0]),
	SCR_INT ^ IFFALSE (DATA (M_IGN_RESIDUE)),
		SIR_SWIDE_OVERRUN,
	SCR_JUMP ^ IFFALSE (DATA (M_IGN_RESIDUE)),
		PADDR_A (msg_in2),
	/*
	 *  We got the message we expected.
	 *  Read the 2nd byte, and jump to dispatcher.
	 */
	SCR_CLR (SCR_ACK),
		0,
	SCR_MOVE_ABS (1) ^ SCR_MSG_IN,
		HADDR_1 (msgin[1]),
	SCR_CLR (SCR_ACK),
		0,
	SCR_JUMP,
		PADDR_A (dispatch),
}/*-------------------------< DATAO_DONE >-----------------------*/,{
	/*
	 *  Save current pointer to LASTP.
	 */
	SCR_STORE_REL (temp, 4),
		offsetof (struct sym_ccb, phys.head.lastp),
	/*
	 *  If the SODL is not full jump to dispatcher.
	 *  We anticipate a STATUS phase.
	 */
	SCR_FROM_REG (scntl2),
		0,
	SCR_JUMP ^ IFTRUE (MASK (WSS, WSS)),
		PADDR_A (datao_done_wss),
	SCR_JUMP ^ IFTRUE (WHEN (SCR_STATUS)),
		PADDR_A (status),
	SCR_JUMP,
		PADDR_A (dispatch),
}/*-------------------------< DATAO_DONE_WSS >-------------------*/,{
	/*
	 *  The SODL is full, clear this condition.
	 */
	SCR_REG_REG (scntl2, SCR_OR, WSS),
		0,
	/*
	 *  And signal a DATA UNDERRUN condition 
	 *  to the C code.
	 */
	SCR_INT,
		SIR_SODL_UNDERRUN,
	SCR_JUMP,
		PADDR_A (dispatch),
}/*-------------------------< DATAI_PHASE >----------------------*/,{
	/*
	 *  Jump to current pointer.
	 */
	SCR_LOAD_REL (temp, 4),
		offsetof (struct sym_ccb, phys.head.lastp),
	SCR_RETURN,
		0,
}/*-------------------------< DATAO_PHASE >----------------------*/,{
	/*
	 *  C1010-66 errata work-around.
	 *  Extra clocks of data hold must be inserted 
	 *  in DATA OUT phase on 33 MHz PCI BUS.
	 *  Patched with a NOOP for other chips.
	 */
	SCR_REG_REG (scntl4, SCR_OR, (XCLKH_DT|XCLKH_ST)),
		0,
	/*
	 *  Jump to current pointer.
	 */
	SCR_LOAD_REL (temp, 4),
		offsetof (struct sym_ccb, phys.head.lastp),
	SCR_RETURN,
		0,
}/*-------------------------< MSG_IN >---------------------------*/,{
	/*
	 *  Get the first byte of the message.
	 *
	 *  The script processor doesn't negate the
	 *  ACK signal after this transfer.
	 */
	SCR_MOVE_ABS (1) ^ SCR_MSG_IN,
		HADDR_1 (msgin[0]),
}/*-------------------------< MSG_IN2 >--------------------------*/,{
	/*
	 *  Check first against 1 byte messages 
	 *  that we handle from SCRIPTS.
	 */
	SCR_JUMP ^ IFTRUE (DATA (M_COMPLETE)),
		PADDR_A (complete),
	SCR_JUMP ^ IFTRUE (DATA (M_DISCONNECT)),
		PADDR_A (disconnect),
	SCR_JUMP ^ IFTRUE (DATA (M_SAVE_DP)),
		PADDR_A (save_dp),
	SCR_JUMP ^ IFTRUE (DATA (M_RESTORE_DP)),
		PADDR_A (restore_dp),
	/*
	 *  We handle all other messages from the 
	 *  C code, so no need to waste on-chip RAM 
	 *  for those ones.
	 */
	SCR_JUMP,
		PADDR_B (msg_in_etc),
}/*-------------------------< STATUS >---------------------------*/,{
	/*
	 *  get the status
	 */
	SCR_MOVE_ABS (1) ^ SCR_STATUS,
		HADDR_1 (scratch),
#ifdef SYM_CONF_IARB_SUPPORT
	/*
	 *  If STATUS is not GOOD, clear IMMEDIATE ARBITRATION, 
	 *  since we may have to tamper the start queue from 
	 *  the C code.
	 */
	SCR_JUMPR ^ IFTRUE (DATA (S_GOOD)),
		8,
	SCR_REG_REG (scntl1, SCR_AND, ~IARB),
		0,
#endif
	/*
	 *  save status to scsi_status.
	 *  mark as complete.
	 */
	SCR_TO_REG (SS_REG),
		0,
	SCR_LOAD_REG (HS_REG, HS_COMPLETE),
		0,
	/*
	 *  Anticipate the MESSAGE PHASE for 
	 *  the TASK COMPLETE message.
	 */
	SCR_JUMP ^ IFTRUE (WHEN (SCR_MSG_IN)),
		PADDR_A (msg_in),
	SCR_JUMP,
		PADDR_A (dispatch),
}/*-------------------------< COMPLETE >-------------------------*/,{
	/*
	 *  Complete message.
	 *
	 *  When we terminate the cycle by clearing ACK,
	 *  the target may disconnect immediately.
	 *
	 *  We don't want to be told of an "unexpected disconnect",
	 *  so we disable this feature.
	 */
	SCR_REG_REG (scntl2, SCR_AND, 0x7f),
		0,
	/*
	 *  Terminate cycle ...
	 */
	SCR_CLR (SCR_ACK|SCR_ATN),
		0,
	/*
	 *  ... and wait for the disconnect.
	 */
	SCR_WAIT_DISC,
		0,
}/*-------------------------< COMPLETE2 >------------------------*/,{
	/*
	 *  Save host status.
	 */
	SCR_STORE_REL (scr0, 4),
		offsetof (struct sym_ccb, phys.head.status),
	/*
	 *  Some bridges may reorder DMA writes to memory.
	 *  We donnot want the CPU to deal with completions  
	 *  without all the posted write having been flushed 
	 *  to memory. This DUMMY READ should flush posted 
	 *  buffers prior to the CPU having to deal with 
	 *  completions.
	 */
	SCR_LOAD_REL (scr0, 4),	/* DUMMY READ */
		offsetof (struct sym_ccb, phys.head.status),

	/*
	 *  If command resulted in not GOOD status,
	 *  call the C code if needed.
	 */
	SCR_FROM_REG (SS_REG),
		0,
	SCR_CALL ^ IFFALSE (DATA (S_GOOD)),
		PADDR_B (bad_status),
	/*
	 *  If we performed an auto-sense, call 
	 *  the C code to synchronyze task aborts 
	 *  with UNIT ATTENTION conditions.
	 */
	SCR_FROM_REG (HF_REG),
		0,
	SCR_JUMP ^ IFFALSE (MASK (0 ,(HF_SENSE|HF_EXT_ERR))),
		PADDR_A (complete_error),
}/*-------------------------< DONE >-----------------------------*/,{
	/*
	 *  Copy the DSA to the DONE QUEUE and 
	 *  signal completion to the host.
	 *  If we are interrupted between DONE 
	 *  and DONE_END, we must reset, otherwise 
	 *  the completed CCB may be lost.
	 */
	SCR_STORE_ABS (dsa, 4),
		PADDR_B (scratch),
	SCR_LOAD_ABS (dsa, 4),
		PADDR_B (done_pos),
	SCR_LOAD_ABS (scratcha, 4),
		PADDR_B (scratch),
	SCR_STORE_REL (scratcha, 4),
		0,
	/*
	 *  The instruction below reads the DONE QUEUE next 
	 *  free position from memory.
	 *  In addition it ensures that all PCI posted writes  
	 *  are flushed and so the DSA value of the done 
	 *  CCB is visible by the CPU before INTFLY is raised.
	 */
	SCR_LOAD_REL (scratcha, 4),
		4,
	SCR_INT_FLY,
		0,
	SCR_STORE_ABS (scratcha, 4),
		PADDR_B (done_pos),
}/*-------------------------< DONE_END >-------------------------*/,{
	SCR_JUMP,
		PADDR_A (start),
}/*-------------------------< COMPLETE_ERROR >-------------------*/,{
	SCR_LOAD_ABS (scratcha, 4),
		PADDR_B (startpos),
	SCR_INT,
		SIR_COMPLETE_ERROR,
}/*-------------------------< SAVE_DP >--------------------------*/,{
	/*
	 *  Clear ACK immediately.
	 *  No need to delay it.
	 */
	SCR_CLR (SCR_ACK),
		0,
	/*
	 *  Keep track we received a SAVE DP, so 
	 *  we will switch to the other PM context 
	 *  on the next PM since the DP may point 
	 *  to the current PM context.
	 */
	SCR_REG_REG (HF_REG, SCR_OR, HF_DP_SAVED),
		0,
	/*
	 *  SAVE_DP message:
	 *  Copy LASTP to SAVEP.
	 */
	SCR_LOAD_REL (scratcha, 4),
		offsetof (struct sym_ccb, phys.head.lastp),
	SCR_STORE_REL (scratcha, 4),
		offsetof (struct sym_ccb, phys.head.savep),
	/*
	 *  Anticipate the MESSAGE PHASE for 
	 *  the DISCONNECT message.
	 */
	SCR_JUMP ^ IFTRUE (WHEN (SCR_MSG_IN)),
		PADDR_A (msg_in),
	SCR_JUMP,
		PADDR_A (dispatch),
}/*-------------------------< RESTORE_DP >-----------------------*/,{
	/*
	 *  Clear ACK immediately.
	 *  No need to delay it.
	 */
	SCR_CLR (SCR_ACK),
		0,
	/*
	 *  Copy SAVEP to LASTP.
	 */
	SCR_LOAD_REL  (scratcha, 4),
		offsetof (struct sym_ccb, phys.head.savep),
	SCR_STORE_REL (scratcha, 4),
		offsetof (struct sym_ccb, phys.head.lastp),
	SCR_JUMP,
		PADDR_A (dispatch),
}/*-------------------------< DISCONNECT >-----------------------*/,{
	/*
	 *  DISCONNECTing  ...
	 *
	 *  disable the "unexpected disconnect" feature,
	 *  and remove the ACK signal.
	 */
	SCR_REG_REG (scntl2, SCR_AND, 0x7f),
		0,
	SCR_CLR (SCR_ACK|SCR_ATN),
		0,
	/*
	 *  Wait for the disconnect.
	 */
	SCR_WAIT_DISC,
		0,
	/*
	 *  Status is: DISCONNECTED.
	 */
	SCR_LOAD_REG (HS_REG, HS_DISCONNECT),
		0,
	/*
	 *  Save host status.
	 */
	SCR_STORE_REL (scr0, 4),
		offsetof (struct sym_ccb, phys.head.status),
	SCR_JUMP,
		PADDR_A (start),
}/*-------------------------< IDLE >-----------------------------*/,{
	/*
	 *  Nothing to do?
	 *  Switch the LED off and wait for reselect.
	 *  Will be patched with a NO_OP if LED
	 *  not needed or not desired.
	 */
	SCR_REG_REG (gpreg, SCR_OR, 0x01),
		0,
#ifdef SYM_CONF_IARB_SUPPORT
	SCR_JUMPR,
		8,
#endif
}/*-------------------------< UNGETJOB >-------------------------*/,{
#ifdef SYM_CONF_IARB_SUPPORT
	/*
	 *  Set IMMEDIATE ARBITRATION, for the next time.
	 *  This will give us better chance to win arbitration 
	 *  for the job we just wanted to do.
	 */
	SCR_REG_REG (scntl1, SCR_OR, IARB),
		0,
#endif
	/*
	 *  We are not able to restart the SCRIPTS if we are 
	 *  interrupted and these instruction haven't been 
	 *  all executed. BTW, this is very unlikely to 
	 *  happen, but we check that from the C code.
	 */
	SCR_LOAD_REG (dsa, 0xff),
		0,
	SCR_STORE_ABS (scratcha, 4),
		PADDR_B (startpos),
}/*-------------------------< RESELECT >-------------------------*/,{
#ifdef SYM_CONF_TARGET_ROLE_SUPPORT
	/*
	 *  Make sure we are in initiator mode.
	 */
	SCR_CLR (SCR_TRG),
		0,
#endif
	/*
	 *  Sleep waiting for a reselection.
	 */
	SCR_WAIT_RESEL,
		PADDR_A(start),
}/*-------------------------< RESELECTED >-----------------------*/,{
	/*
	 *  Switch the LED on.
	 *  Will be patched with a NO_OP if LED
	 *  not needed or not desired.
	 */
	SCR_REG_REG (gpreg, SCR_AND, 0xfe),
		0,
	/*
	 *  load the target id into the sdid
	 */
	SCR_REG_SFBR (ssid, SCR_AND, 0x8F),
		0,
	SCR_TO_REG (sdid),
		0,
	/*
	 *  Load the target control block address
	 */
	SCR_LOAD_ABS (dsa, 4),
		PADDR_B (targtbl),
	SCR_SFBR_REG (dsa, SCR_SHL, 0),
		0,
	SCR_REG_REG (dsa, SCR_SHL, 0),
		0,
	SCR_REG_REG (dsa, SCR_AND, 0x3c),
		0,
	SCR_LOAD_REL (dsa, 4),
		0,
	/*
	 *  We expect MESSAGE IN phase.
	 *  If not, get help from the C code.
	 */
	SCR_INT ^ IFFALSE (WHEN (SCR_MSG_IN)),
		SIR_RESEL_NO_MSG_IN,
	/*
	 *  Load the legacy synchronous transfer registers.
	 */
	SCR_LOAD_REL (scntl3, 1),
		offsetof(struct sym_tcb, head.wval),
	SCR_LOAD_REL (sxfer, 1),
		offsetof(struct sym_tcb, head.sval),
}/*-------------------------< RESEL_SCNTL4 >---------------------*/,{
	/*
	 *  The C1010 uses a new synchronous timing scheme.
	 *  Will be patched with a NO_OP if not a C1010.
	 */
	SCR_LOAD_REL (scntl4, 1),
		offsetof(struct sym_tcb, head.uval),
	/*
	 *  Get the IDENTIFY message.
	 */
	SCR_MOVE_ABS (1) ^ SCR_MSG_IN,
		HADDR_1 (msgin),
	/*
	 *  If IDENTIFY LUN #0, use a faster path 
	 *  to find the LCB structure.
	 */
	SCR_JUMP ^ IFTRUE (MASK (0x80, 0xbf)),
		PADDR_A (resel_lun0),
	/*
	 *  If message isn't an IDENTIFY, 
	 *  tell the C code about.
	 */
	SCR_INT ^ IFFALSE (MASK (0x80, 0x80)),
		SIR_RESEL_NO_IDENTIFY,
	/*
	 *  It is an IDENTIFY message,
	 *  Load the LUN control block address.
	 */
	SCR_LOAD_REL (dsa, 4),
		offsetof(struct sym_tcb, head.luntbl_sa),
	SCR_SFBR_REG (dsa, SCR_SHL, 0),
		0,
	SCR_REG_REG (dsa, SCR_SHL, 0),
		0,
	SCR_REG_REG (dsa, SCR_AND, 0xfc),
		0,
	SCR_LOAD_REL (dsa, 4),
		0,
	SCR_JUMPR,
		8,
}/*-------------------------< RESEL_LUN0 >-----------------------*/,{
	/*
	 *  LUN 0 special case (but usual one :))
	 */
	SCR_LOAD_REL (dsa, 4),
		offsetof(struct sym_tcb, head.lun0_sa),
	/*
	 *  Jump indirectly to the reselect action for this LUN.
	 */
	SCR_LOAD_REL (temp, 4),
		offsetof(struct sym_lcb, head.resel_sa),
	SCR_RETURN,
		0,
	/* In normal situations, we jump to RESEL_TAG or RESEL_NO_TAG */
}/*-------------------------< RESEL_TAG >------------------------*/,{
	/*
	 *  ACK the IDENTIFY previously received.
	 */
	SCR_CLR (SCR_ACK),
		0,
	/*
	 *  It shall be a tagged command.
	 *  Read SIMPLE+TAG.
	 *  The C code will deal with errors.
	 *  Aggressive optimization, isn't it? :)
	 */
	SCR_MOVE_ABS (2) ^ SCR_MSG_IN,
		HADDR_1 (msgin),
	/*
	 *  Load the pointer to the tagged task 
	 *  table for this LUN.
	 */
	SCR_LOAD_REL (dsa, 4),
		offsetof(struct sym_lcb, head.itlq_tbl_sa),
	/*
	 *  The SIDL still contains the TAG value.
	 *  Aggressive optimization, isn't it? :):)
	 */
	SCR_REG_SFBR (sidl, SCR_SHL, 0),
		0,
#if SYM_CONF_MAX_TASK*4 > 512
	SCR_JUMPR ^ IFFALSE (CARRYSET),
		8,
	SCR_REG_REG (dsa1, SCR_OR, 2),
		0,
	SCR_REG_REG (sfbr, SCR_SHL, 0),
		0,
	SCR_JUMPR ^ IFFALSE (CARRYSET),
		8,
	SCR_REG_REG (dsa1, SCR_OR, 1),
		0,
#elif SYM_CONF_MAX_TASK*4 > 256
	SCR_JUMPR ^ IFFALSE (CARRYSET),
		8,
	SCR_REG_REG (dsa1, SCR_OR, 1),
		0,
#endif
	/*
	 *  Retrieve the DSA of this task.
	 *  JUMP indirectly to the restart point of the CCB.
	 */
	SCR_SFBR_REG (dsa, SCR_AND, 0xfc),
		0,
	SCR_LOAD_REL (dsa, 4),
		0,
	SCR_LOAD_REL (temp, 4),
		offsetof(struct sym_ccb, phys.head.go.restart),
	SCR_RETURN,
		0,
	/* In normal situations we branch to RESEL_DSA */
}/*-------------------------< RESEL_DSA >------------------------*/,{
	/*
	 *  ACK the IDENTIFY or TAG previously received.
	 */
	SCR_CLR (SCR_ACK),
		0,
}/*-------------------------< RESEL_DSA1 >-----------------------*/,{
	/*
	 *      Initialize the status registers
	 */
	SCR_LOAD_REL (scr0, 4),
		offsetof (struct sym_ccb, phys.head.status),
	/*
	 *  Jump to dispatcher.
	 */
	SCR_JUMP,
		PADDR_A (dispatch),
}/*-------------------------< RESEL_NO_TAG >---------------------*/,{
	/*
	 *  Load the DSA with the unique ITL task.
	 */
	SCR_LOAD_REL (dsa, 4),
		offsetof(struct sym_lcb, head.itl_task_sa),
	/*
	 *  JUMP indirectly to the restart point of the CCB.
	 */
	SCR_LOAD_REL (temp, 4),
		offsetof(struct sym_ccb, phys.head.go.restart),
	SCR_RETURN,
		0,
	/* In normal situations we branch to RESEL_DSA */
}/*-------------------------< DATA_IN >--------------------------*/,{
/*
 *  Because the size depends on the
 *  #define SYM_CONF_MAX_SG parameter,
 *  it is filled in at runtime.
 *
 *  ##===========< i=0; i<SYM_CONF_MAX_SG >=========
 *  ||	SCR_CHMOV_TBL ^ SCR_DATA_IN,
 *  ||		offsetof (struct sym_dsb, data[ i]),
 *  ##==========================================
 */
0
}/*-------------------------< DATA_IN2 >-------------------------*/,{
	SCR_CALL,
		PADDR_A (datai_done),
	SCR_JUMP,
		PADDR_B (data_ovrun),
}/*-------------------------< DATA_OUT >-------------------------*/,{
/*
 *  Because the size depends on the
 *  #define SYM_CONF_MAX_SG parameter,
 *  it is filled in at runtime.
 *
 *  ##===========< i=0; i<SYM_CONF_MAX_SG >=========
 *  ||	SCR_CHMOV_TBL ^ SCR_DATA_OUT,
 *  ||		offsetof (struct sym_dsb, data[ i]),
 *  ##==========================================
 */
0
}/*-------------------------< DATA_OUT2 >------------------------*/,{
	SCR_CALL,
		PADDR_A (datao_done),
	SCR_JUMP,
		PADDR_B (data_ovrun),
}/*-------------------------< PM0_DATA >-------------------------*/,{
	/*
	 *  Read our host flags to SFBR, so we will be able 
	 *  to check against the data direction we expect.
	 */
	SCR_FROM_REG (HF_REG),
		0,
	/*
	 *  Check against actual DATA PHASE.
	 */
	SCR_JUMP ^ IFFALSE (WHEN (SCR_DATA_IN)),
		PADDR_A (pm0_data_out),
	/*
	 *  Actual phase is DATA IN.
	 *  Check against expected direction.
	 */
	SCR_JUMP ^ IFFALSE (MASK (HF_DATA_IN, HF_DATA_IN)),
		PADDR_B (data_ovrun),
	/*
	 *  Keep track we are moving data from the 
	 *  PM0 DATA mini-script.
	 */
	SCR_REG_REG (HF_REG, SCR_OR, HF_IN_PM0),
		0,
	/*
	 *  Move the data to memory.
	 */
	SCR_CHMOV_TBL ^ SCR_DATA_IN,
		offsetof (struct sym_ccb, phys.pm0.sg),
	SCR_JUMP,
		PADDR_A (pm0_data_end),
}/*-------------------------< PM0_DATA_OUT >---------------------*/,{
	/*
	 *  Actual phase is DATA OUT.
	 *  Check against expected direction.
	 */
	SCR_JUMP ^ IFTRUE (MASK (HF_DATA_IN, HF_DATA_IN)),
		PADDR_B (data_ovrun),
	/*
	 *  Keep track we are moving data from the 
	 *  PM0 DATA mini-script.
	 */
	SCR_REG_REG (HF_REG, SCR_OR, HF_IN_PM0),
		0,
	/*
	 *  Move the data from memory.
	 */
	SCR_CHMOV_TBL ^ SCR_DATA_OUT,
		offsetof (struct sym_ccb, phys.pm0.sg),
}/*-------------------------< PM0_DATA_END >---------------------*/,{
	/*
	 *  Clear the flag that told we were moving  
	 *  data from the PM0 DATA mini-script.
	 */
	SCR_REG_REG (HF_REG, SCR_AND, (~HF_IN_PM0)),
		0,
	/*
	 *  Return to the previous DATA script which 
	 *  is guaranteed by design (if no bug) to be 
	 *  the main DATA script for this transfer.
	 */
	SCR_LOAD_REL (temp, 4),
		offsetof (struct sym_ccb, phys.pm0.ret),
	SCR_RETURN,
		0,
}/*-------------------------< PM1_DATA >-------------------------*/,{
	/*
	 *  Read our host flags to SFBR, so we will be able 
	 *  to check against the data direction we expect.
	 */
	SCR_FROM_REG (HF_REG),
		0,
	/*
	 *  Check against actual DATA PHASE.
	 */
	SCR_JUMP ^ IFFALSE (WHEN (SCR_DATA_IN)),
		PADDR_A (pm1_data_out),
	/*
	 *  Actual phase is DATA IN.
	 *  Check against expected direction.
	 */
	SCR_JUMP ^ IFFALSE (MASK (HF_DATA_IN, HF_DATA_IN)),
		PADDR_B (data_ovrun),
	/*
	 *  Keep track we are moving data from the 
	 *  PM1 DATA mini-script.
	 */
	SCR_REG_REG (HF_REG, SCR_OR, HF_IN_PM1),
		0,
	/*
	 *  Move the data to memory.
	 */
	SCR_CHMOV_TBL ^ SCR_DATA_IN,
		offsetof (struct sym_ccb, phys.pm1.sg),
	SCR_JUMP,
		PADDR_A (pm1_data_end),
}/*-------------------------< PM1_DATA_OUT >---------------------*/,{
	/*
	 *  Actual phase is DATA OUT.
	 *  Check against expected direction.
	 */
	SCR_JUMP ^ IFTRUE (MASK (HF_DATA_IN, HF_DATA_IN)),
		PADDR_B (data_ovrun),
	/*
	 *  Keep track we are moving data from the 
	 *  PM1 DATA mini-script.
	 */
	SCR_REG_REG (HF_REG, SCR_OR, HF_IN_PM1),
		0,
	/*
	 *  Move the data from memory.
	 */
	SCR_CHMOV_TBL ^ SCR_DATA_OUT,
		offsetof (struct sym_ccb, phys.pm1.sg),
}/*-------------------------< PM1_DATA_END >---------------------*/,{
	/*
	 *  Clear the flag that told we were moving  
	 *  data from the PM1 DATA mini-script.
	 */
	SCR_REG_REG (HF_REG, SCR_AND, (~HF_IN_PM1)),
		0,
	/*
	 *  Return to the previous DATA script which 
	 *  is guaranteed by design (if no bug) to be 
	 *  the main DATA script for this transfer.
	 */
	SCR_LOAD_REL (temp, 4),
		offsetof (struct sym_ccb, phys.pm1.ret),
	SCR_RETURN,
		0,
}/*-------------------------<>-----------------------------------*/
};

static struct SYM_FWB_SCR SYM_FWB_SCR = {
/*--------------------------< START64 >--------------------------*/ {
	/*
	 *  SCRIPT entry point for the 895A, 896 and 1010.
	 *  For now, there is no specific stuff for those 
	 *  chips at this point, but this may come.
	 */
	SCR_JUMP,
		PADDR_A (init),
}/*-------------------------< NO_DATA >--------------------------*/,{
	SCR_JUMP,
		PADDR_B (data_ovrun),
}/*-------------------------< SEL_FOR_ABORT >--------------------*/,{
	/*
	 *  We are jumped here by the C code, if we have 
	 *  some target to reset or some disconnected 
	 *  job to abort. Since error recovery is a serious 
	 *  busyness, we will really reset the SCSI BUS, if 
	 *  case of a SCSI interrupt occurring in this path.
	 */
#ifdef SYM_CONF_TARGET_ROLE_SUPPORT
	/*
	 *  Set initiator mode.
	 */
	SCR_CLR (SCR_TRG),
		0,
#endif
	/*
	 *      And try to select this target.
	 */
	SCR_SEL_TBL_ATN ^ offsetof (struct sym_hcb, abrt_sel),
		PADDR_A (reselect),
	/*
	 *  Wait for the selection to complete or 
	 *  the selection to time out.
	 */
	SCR_JUMPR ^ IFFALSE (WHEN (SCR_MSG_OUT)),
		-8,
	/*
	 *  Call the C code.
	 */
	SCR_INT,
		SIR_TARGET_SELECTED,
	/*
	 *  The C code should let us continue here. 
	 *  Send the 'kiss of death' message.
	 *  We expect an immediate disconnect once 
	 *  the target has eaten the message.
	 */
	SCR_REG_REG (scntl2, SCR_AND, 0x7f),
		0,
	SCR_MOVE_TBL ^ SCR_MSG_OUT,
		offsetof (struct sym_hcb, abrt_tbl),
	SCR_CLR (SCR_ACK|SCR_ATN),
		0,
	SCR_WAIT_DISC,
		0,
	/*
	 *  Tell the C code that we are done.
	 */
	SCR_INT,
		SIR_ABORT_SENT,
}/*-------------------------< SEL_FOR_ABORT_1 >------------------*/,{
	/*
	 *  Jump at scheduler.
	 */
	SCR_JUMP,
		PADDR_A (start),
}/*-------------------------< MSG_IN_ETC >-----------------------*/,{
	/*
	 *  If it is an EXTENDED (variable size message)
	 *  Handle it.
	 */
	SCR_JUMP ^ IFTRUE (DATA (M_EXTENDED)),
		PADDR_B (msg_extended),
	/*
	 *  Let the C code handle any other 
	 *  1 byte message.
	 */
	SCR_JUMP ^ IFTRUE (MASK (0x00, 0xf0)),
		PADDR_B (msg_received),
	SCR_JUMP ^ IFTRUE (MASK (0x10, 0xf0)),
		PADDR_B (msg_received),
	/*
	 *  We donnot handle 2 bytes messages from SCRIPTS.
	 *  So, let the C code deal with these ones too.
	 */
	SCR_JUMP ^ IFFALSE (MASK (0x20, 0xf0)),
		PADDR_B (msg_weird_seen),
	SCR_CLR (SCR_ACK),
		0,
	SCR_MOVE_ABS (1) ^ SCR_MSG_IN,
		HADDR_1 (msgin[1]),
}/*-------------------------< MSG_RECEIVED >---------------------*/,{
	SCR_LOAD_REL (scratcha, 4),	/* DUMMY READ */
		0,
	SCR_INT,
		SIR_MSG_RECEIVED,
}/*-------------------------< MSG_WEIRD_SEEN >-------------------*/,{
	SCR_LOAD_REL (scratcha, 4),	/* DUMMY READ */
		0,
	SCR_INT,
		SIR_MSG_WEIRD,
}/*-------------------------< MSG_EXTENDED >---------------------*/,{
	/*
	 *  Clear ACK and get the next byte 
	 *  assumed to be the message length.
	 */
	SCR_CLR (SCR_ACK),
		0,
	SCR_MOVE_ABS (1) ^ SCR_MSG_IN,
		HADDR_1 (msgin[1]),
	/*
	 *  Try to catch some unlikely situations as 0 length 
	 *  or too large the length.
	 */
	SCR_JUMP ^ IFTRUE (DATA (0)),
		PADDR_B (msg_weird_seen),
	SCR_TO_REG (scratcha),
		0,
	SCR_REG_REG (sfbr, SCR_ADD, (256-8)),
		0,
	SCR_JUMP ^ IFTRUE (CARRYSET),
		PADDR_B (msg_weird_seen),
	/*
	 *  We donnot handle extended messages from SCRIPTS.
	 *  Read the amount of data correponding to the 
	 *  message length and call the C code.
	 */
	SCR_STORE_REL (scratcha, 1),
		offsetof (struct sym_dsb, smsg_ext.size),
	SCR_CLR (SCR_ACK),
		0,
	SCR_MOVE_TBL ^ SCR_MSG_IN,
		offsetof (struct sym_dsb, smsg_ext),
	SCR_JUMP,
		PADDR_B (msg_received),
}/*-------------------------< MSG_BAD >--------------------------*/,{
	/*
	 *  unimplemented message - reject it.
	 */
	SCR_INT,
		SIR_REJECT_TO_SEND,
	SCR_SET (SCR_ATN),
		0,
	SCR_JUMP,
		PADDR_A (clrack),
}/*-------------------------< MSG_WEIRD >------------------------*/,{
	/*
	 *  weird message received
	 *  ignore all MSG IN phases and reject it.
	 */
	SCR_INT,
		SIR_REJECT_TO_SEND,
	SCR_SET (SCR_ATN),
		0,
}/*-------------------------< MSG_WEIRD1 >-----------------------*/,{
	SCR_CLR (SCR_ACK),
		0,
	SCR_JUMP ^ IFFALSE (WHEN (SCR_MSG_IN)),
		PADDR_A (dispatch),
	SCR_MOVE_ABS (1) ^ SCR_MSG_IN,
		HADDR_1 (scratch),
	SCR_JUMP,
		PADDR_B (msg_weird1),
}/*-------------------------< WDTR_RESP >------------------------*/,{
	/*
	 *  let the target fetch our answer.
	 */
	SCR_SET (SCR_ATN),
		0,
	SCR_CLR (SCR_ACK),
		0,
	SCR_JUMP ^ IFFALSE (WHEN (SCR_MSG_OUT)),
		PADDR_B (nego_bad_phase),
}/*-------------------------< SEND_WDTR >------------------------*/,{
	/*
	 *  Send the M_X_WIDE_REQ
	 */
	SCR_MOVE_ABS (4) ^ SCR_MSG_OUT,
		HADDR_1 (msgout),
	SCR_JUMP,
		PADDR_B (msg_out_done),
}/*-------------------------< SDTR_RESP >------------------------*/,{
	/*
	 *  let the target fetch our answer.
	 */
	SCR_SET (SCR_ATN),
		0,
	SCR_CLR (SCR_ACK),
		0,
	SCR_JUMP ^ IFFALSE (WHEN (SCR_MSG_OUT)),
		PADDR_B (nego_bad_phase),
}/*-------------------------< SEND_SDTR >------------------------*/,{
	/*
	 *  Send the M_X_SYNC_REQ
	 */
	SCR_MOVE_ABS (5) ^ SCR_MSG_OUT,
		HADDR_1 (msgout),
	SCR_JUMP,
		PADDR_B (msg_out_done),
}/*-------------------------< PPR_RESP >-------------------------*/,{
	/*
	 *  let the target fetch our answer.
	 */
	SCR_SET (SCR_ATN),
		0,
	SCR_CLR (SCR_ACK),
		0,
	SCR_JUMP ^ IFFALSE (WHEN (SCR_MSG_OUT)),
		PADDR_B (nego_bad_phase),
}/*-------------------------< SEND_PPR >-------------------------*/,{
	/*
	 *  Send the M_X_PPR_REQ
	 */
	SCR_MOVE_ABS (8) ^ SCR_MSG_OUT,
		HADDR_1 (msgout),
	SCR_JUMP,
		PADDR_B (msg_out_done),
}/*-------------------------< NEGO_BAD_PHASE >-------------------*/,{
	SCR_INT,
		SIR_NEGO_PROTO,
	SCR_JUMP,
		PADDR_A (dispatch),
}/*-------------------------< MSG_OUT >--------------------------*/,{
	/*
	 *  The target requests a message.
	 *  We donnot send messages that may 
	 *  require the device to go to bus free.
	 */
	SCR_MOVE_ABS (1) ^ SCR_MSG_OUT,
		HADDR_1 (msgout),
	/*
	 *  ... wait for the next phase
	 *  if it's a message out, send it again, ...
	 */
	SCR_JUMP ^ IFTRUE (WHEN (SCR_MSG_OUT)),
		PADDR_B (msg_out),
}/*-------------------------< MSG_OUT_DONE >---------------------*/,{
	/*
	 *  Let the C code be aware of the 
	 *  sent message and clear the message.
	 */
	SCR_INT,
		SIR_MSG_OUT_DONE,
	/*
	 *  ... and process the next phase
	 */
	SCR_JUMP,
		PADDR_A (dispatch),
}/*-------------------------< DATA_OVRUN >-----------------------*/,{
	/*
	 *  Use scratcha to count the extra bytes.
	 */
	SCR_LOAD_ABS (scratcha, 4),
		PADDR_B (zero),
}/*-------------------------< DATA_OVRUN1 >----------------------*/,{
	/*
	 *  The target may want to transfer too much data.
	 *
	 *  If phase is DATA OUT write 1 byte and count it.
	 */
	SCR_JUMPR ^ IFFALSE (WHEN (SCR_DATA_OUT)),
		16,
	SCR_CHMOV_ABS (1) ^ SCR_DATA_OUT,
		HADDR_1 (scratch),
	SCR_JUMP,
		PADDR_B (data_ovrun2),
	/*
	 *  If WSR is set, clear this condition, and 
	 *  count this byte.
	 */
	SCR_FROM_REG (scntl2),
		0,
	SCR_JUMPR ^ IFFALSE (MASK (WSR, WSR)),
		16,
	SCR_REG_REG (scntl2, SCR_OR, WSR),
		0,
	SCR_JUMP,
		PADDR_B (data_ovrun2),
	/*
	 *  Finally check against DATA IN phase.
	 *  Signal data overrun to the C code 
	 *  and jump to dispatcher if not so.
	 *  Read 1 byte otherwise and count it.
	 */
	SCR_JUMPR ^ IFTRUE (WHEN (SCR_DATA_IN)),
		16,
	SCR_INT,
		SIR_DATA_OVERRUN,
	SCR_JUMP,
		PADDR_A (dispatch),
	SCR_CHMOV_ABS (1) ^ SCR_DATA_IN,
		HADDR_1 (scratch),
}/*-------------------------< DATA_OVRUN2 >----------------------*/,{
	/*
	 *  Count this byte.
	 *  This will allow to return a negative 
	 *  residual to user.
	 */
	SCR_REG_REG (scratcha,  SCR_ADD,  0x01),
		0,
	SCR_REG_REG (scratcha1, SCR_ADDC, 0),
		0,
	SCR_REG_REG (scratcha2, SCR_ADDC, 0),
		0,
	/*
	 *  .. and repeat as required.
	 */
	SCR_JUMP,
		PADDR_B (data_ovrun1),
}/*-------------------------< ABORT_RESEL >----------------------*/,{
	SCR_SET (SCR_ATN),
		0,
	SCR_CLR (SCR_ACK),
		0,
	/*
	 *  send the abort/abortag/reset message
	 *  we expect an immediate disconnect
	 */
	SCR_REG_REG (scntl2, SCR_AND, 0x7f),
		0,
	SCR_MOVE_ABS (1) ^ SCR_MSG_OUT,
		HADDR_1 (msgout),
	SCR_CLR (SCR_ACK|SCR_ATN),
		0,
	SCR_WAIT_DISC,
		0,
	SCR_INT,
		SIR_RESEL_ABORTED,
	SCR_JUMP,
		PADDR_A (start),
}/*-------------------------< RESEND_IDENT >---------------------*/,{
	/*
	 *  The target stays in MSG OUT phase after having acked 
	 *  Identify [+ Tag [+ Extended message ]]. Targets shall
	 *  behave this way on parity error.
	 *  We must send it again all the messages.
	 */
	SCR_SET (SCR_ATN), /* Shall be asserted 2 deskew delays before the  */
		0,         /* 1rst ACK = 90 ns. Hope the chip isn't too fast */
	SCR_JUMP,
		PADDR_A (send_ident),
}/*-------------------------< IDENT_BREAK >----------------------*/,{
	SCR_CLR (SCR_ATN),
		0,
	SCR_JUMP,
		PADDR_A (select2),
}/*-------------------------< IDENT_BREAK_ATN >------------------*/,{
	SCR_SET (SCR_ATN),
		0,
	SCR_JUMP,
		PADDR_A (select2),
}/*-------------------------< SDATA_IN >-------------------------*/,{
	SCR_CHMOV_TBL ^ SCR_DATA_IN,
		offsetof (struct sym_dsb, sense),
	SCR_CALL,
		PADDR_A (datai_done),
	SCR_JUMP,
		PADDR_B (data_ovrun),
}/*-------------------------< RESEL_BAD_LUN >--------------------*/,{
	/*
	 *  Message is an IDENTIFY, but lun is unknown.
	 *  Signal problem to C code for logging the event.
	 *  Send a M_ABORT to clear all pending tasks.
	 */
	SCR_INT,
		SIR_RESEL_BAD_LUN,
	SCR_JUMP,
		PADDR_B (abort_resel),
}/*-------------------------< BAD_I_T_L >------------------------*/,{
	/*
	 *  We donnot have a task for that I_T_L.
	 *  Signal problem to C code for logging the event.
	 *  Send a M_ABORT message.
	 */
	SCR_INT,
		SIR_RESEL_BAD_I_T_L,
	SCR_JUMP,
		PADDR_B (abort_resel),
}/*-------------------------< BAD_I_T_L_Q >----------------------*/,{
	/*
	 *  We donnot have a task that matches the tag.
	 *  Signal problem to C code for logging the event.
	 *  Send a M_ABORTTAG message.
	 */
	SCR_INT,
		SIR_RESEL_BAD_I_T_L_Q,
	SCR_JUMP,
		PADDR_B (abort_resel),
}/*-------------------------< BAD_STATUS >-----------------------*/,{
	/*
	 *  Anything different from INTERMEDIATE 
	 *  CONDITION MET should be a bad SCSI status, 
	 *  given that GOOD status has already been tested.
	 *  Call the C code.
	 */
	SCR_LOAD_ABS (scratcha, 4),
		PADDR_B (startpos),
	SCR_INT ^ IFFALSE (DATA (S_COND_MET)),
		SIR_BAD_SCSI_STATUS,
	SCR_RETURN,
		0,
}/*-------------------------< PM_HANDLE >------------------------*/,{
	/*
	 *  Phase mismatch handling.
	 *
	 *  Since we have to deal with 2 SCSI data pointers  
	 *  (current and saved), we need at least 2 contexts.
	 *  Each context (pm0 and pm1) has a saved area, a 
	 *  SAVE mini-script and a DATA phase mini-script.
	 */
	/*
	 *  Get the PM handling flags.
	 */
	SCR_FROM_REG (HF_REG),
		0,
	/*
	 *  If no flags (1rst PM for example), avoid 
	 *  all the below heavy flags testing.
	 *  This makes the normal case a bit faster.
	 */
	SCR_JUMP ^ IFTRUE (MASK (0, (HF_IN_PM0 | HF_IN_PM1 | HF_DP_SAVED))),
		PADDR_B (pm_handle1),
	/*
	 *  If we received a SAVE DP, switch to the 
	 *  other PM context since the savep may point 
	 *  to the current PM context.
	 */
	SCR_JUMPR ^ IFFALSE (MASK (HF_DP_SAVED, HF_DP_SAVED)),
		8,
	SCR_REG_REG (sfbr, SCR_XOR, HF_ACT_PM),
		0,
	/*
	 *  If we have been interrupt in a PM DATA mini-script,
	 *  we take the return address from the corresponding 
	 *  saved area.
	 *  This ensure the return address always points to the 
	 *  main DATA script for this transfer.
	 */
	SCR_JUMP ^ IFTRUE (MASK (0, (HF_IN_PM0 | HF_IN_PM1))),
		PADDR_B (pm_handle1),
	SCR_JUMPR ^ IFFALSE (MASK (HF_IN_PM0, HF_IN_PM0)),
		16,
	SCR_LOAD_REL (ia, 4),
		offsetof(struct sym_ccb, phys.pm0.ret),
	SCR_JUMP,
		PADDR_B (pm_save),
	SCR_LOAD_REL (ia, 4),
		offsetof(struct sym_ccb, phys.pm1.ret),
	SCR_JUMP,
		PADDR_B (pm_save),
}/*-------------------------< PM_HANDLE1 >-----------------------*/,{
	/*
	 *  Normal case.
	 *  Update the return address so that it 
	 *  will point after the interrupted MOVE.
	 */
	SCR_REG_REG (ia, SCR_ADD, 8),
		0,
	SCR_REG_REG (ia1, SCR_ADDC, 0),
		0,
}/*-------------------------< PM_SAVE >--------------------------*/,{
	/*
	 *  Clear all the flags that told us if we were 
	 *  interrupted in a PM DATA mini-script and/or 
	 *  we received a SAVE DP.
	 */
	SCR_SFBR_REG (HF_REG, SCR_AND, (~(HF_IN_PM0|HF_IN_PM1|HF_DP_SAVED))),
		0,
	/*
	 *  Choose the current PM context.
	 */
	SCR_JUMP ^ IFTRUE (MASK (HF_ACT_PM, HF_ACT_PM)),
		PADDR_B (pm1_save),
}/*-------------------------< PM0_SAVE >-------------------------*/,{
	SCR_STORE_REL (ia, 4),
		offsetof(struct sym_ccb, phys.pm0.ret),
	/*
	 *  If WSR bit is set, either UA and RBC may 
	 *  have to be changed whether the device wants 
	 *  to ignore this residue or not.
	 */
	SCR_FROM_REG (scntl2),
		0,
	SCR_CALL ^ IFTRUE (MASK (WSR, WSR)),
		PADDR_B (pm_wsr_handle),
	/*
	 *  Save the remaining byte count, the updated 
	 *  address and the return address.
	 */
	SCR_STORE_REL (rbc, 4),
		offsetof(struct sym_ccb, phys.pm0.sg.size),
	SCR_STORE_REL (ua, 4),
		offsetof(struct sym_ccb, phys.pm0.sg.addr),
	/*
	 *  Set the current pointer at the PM0 DATA mini-script.
	 */
	SCR_LOAD_ABS (ia, 4),
		PADDR_B (pm0_data_addr),
}/*-------------------------< PM_SAVE_END >----------------------*/,{
	SCR_STORE_REL (ia, 4),
		offsetof(struct sym_ccb, phys.head.lastp),
	SCR_JUMP,
		PADDR_A (dispatch),
}/*-------------------------< PM1_SAVE >-------------------------*/,{
	SCR_STORE_REL (ia, 4),
		offsetof(struct sym_ccb, phys.pm1.ret),
	/*
	 *  If WSR bit is set, either UA and RBC may 
	 *  have to be changed whether the device wants 
	 *  to ignore this residue or not.
	 */
	SCR_FROM_REG (scntl2),
		0,
	SCR_CALL ^ IFTRUE (MASK (WSR, WSR)),
		PADDR_B (pm_wsr_handle),
	/*
	 *  Save the remaining byte count, the updated 
	 *  address and the return address.
	 */
	SCR_STORE_REL (rbc, 4),
		offsetof(struct sym_ccb, phys.pm1.sg.size),
	SCR_STORE_REL (ua, 4),
		offsetof(struct sym_ccb, phys.pm1.sg.addr),
	/*
	 *  Set the current pointer at the PM1 DATA mini-script.
	 */
	SCR_LOAD_ABS (ia, 4),
		PADDR_B (pm1_data_addr),
	SCR_JUMP,
		PADDR_B (pm_save_end),
}/*-------------------------< PM_WSR_HANDLE >--------------------*/,{
	/*
	 *  Phase mismatch handling from SCRIPT with WSR set.
	 *  Such a condition can occur if the chip wants to 
	 *  execute a CHMOV(size > 1) when the WSR bit is 
	 *  set and the target changes PHASE.
	 *
	 *  We must move the residual byte to memory.
	 *
	 *  UA contains bit 0..31 of the address to 
	 *  move the residual byte.
	 *  Move it to the table indirect.
	 */
	SCR_STORE_REL (ua, 4),
		offsetof (struct sym_ccb, phys.wresid.addr),
	/*
	 *  Increment UA (move address to next position).
	 */
	SCR_REG_REG (ua, SCR_ADD, 1),
		0,
	SCR_REG_REG (ua1, SCR_ADDC, 0),
		0,
	SCR_REG_REG (ua2, SCR_ADDC, 0),
		0,
	SCR_REG_REG (ua3, SCR_ADDC, 0),
		0,
	/*
	 *  Compute SCRATCHA as:
	 *  - size to transfer = 1 byte.
	 *  - bit 24..31 = high address bit [32...39].
	 */
	SCR_LOAD_ABS (scratcha, 4),
		PADDR_B (zero),
	SCR_REG_REG (scratcha, SCR_OR, 1),
		0,
	SCR_FROM_REG (rbc3),
		0,
	SCR_TO_REG (scratcha3),
		0,
	/*
	 *  Move this value to the table indirect.
	 */
	SCR_STORE_REL (scratcha, 4),
		offsetof (struct sym_ccb, phys.wresid.size),
	/*
	 *  Wait for a valid phase.
	 *  While testing with bogus QUANTUM drives, the C1010 
	 *  sometimes raised a spurious phase mismatch with 
	 *  WSR and the CHMOV(1) triggered another PM.
	 *  Waiting explicitely for the PHASE seemed to avoid 
	 *  the nested phase mismatch. Btw, this didn't happen 
	 *  using my IBM drives.
	 */
	SCR_JUMPR ^ IFFALSE (WHEN (SCR_DATA_IN)),
		0,
	/*
	 *  Perform the move of the residual byte.
	 */
	SCR_CHMOV_TBL ^ SCR_DATA_IN,
		offsetof (struct sym_ccb, phys.wresid),
	/*
	 *  We can now handle the phase mismatch with UA fixed.
	 *  RBC[0..23]=0 is a special case that does not require 
	 *  a PM context. The C code also checks against this.
	 */
	SCR_FROM_REG (rbc),
		0,
	SCR_RETURN ^ IFFALSE (DATA (0)),
		0,
	SCR_FROM_REG (rbc1),
		0,
	SCR_RETURN ^ IFFALSE (DATA (0)),
		0,
	SCR_FROM_REG (rbc2),
		0,
	SCR_RETURN ^ IFFALSE (DATA (0)),
		0,
	/*
	 *  RBC[0..23]=0.
	 *  Not only we donnot need a PM context, but this would 
	 *  lead to a bogus CHMOV(0). This condition means that 
	 *  the residual was the last byte to move from this CHMOV.
	 *  So, we just have to move the current data script pointer 
	 *  (i.e. TEMP) to the SCRIPTS address following the 
	 *  interrupted CHMOV and jump to dispatcher.
	 *  IA contains the data pointer to save.
	 */
	SCR_JUMP,
		PADDR_B (pm_save_end),
}/*-------------------------< WSR_MA_HELPER >--------------------*/,{
	/*
	 *  Helper for the C code when WSR bit is set.
	 *  Perform the move of the residual byte.
	 */
	SCR_CHMOV_TBL ^ SCR_DATA_IN,
		offsetof (struct sym_ccb, phys.wresid),
	SCR_JUMP,
		PADDR_A (dispatch),

}/*-------------------------< ZERO >-----------------------------*/,{
	SCR_DATA_ZERO,
}/*-------------------------< SCRATCH >--------------------------*/,{
	SCR_DATA_ZERO,
}/*-------------------------< PM0_DATA_ADDR >--------------------*/,{
	SCR_DATA_ZERO,
}/*-------------------------< PM1_DATA_ADDR >--------------------*/,{
	SCR_DATA_ZERO,
}/*-------------------------< DONE_POS >-------------------------*/,{
	SCR_DATA_ZERO,
}/*-------------------------< STARTPOS >-------------------------*/,{
	SCR_DATA_ZERO,
}/*-------------------------< TARGTBL >--------------------------*/,{
	SCR_DATA_ZERO,
}/*-------------------------<>-----------------------------------*/
};

static struct SYM_FWZ_SCR SYM_FWZ_SCR = {
 /*-------------------------< SNOOPTEST >------------------------*/{
	/*
	 *  Read the variable from memory.
	 */
	SCR_LOAD_REL (scratcha, 4),
		offsetof(struct sym_hcb, scratch),
	/*
	 *  Write the variable to memory.
	 */
	SCR_STORE_REL (temp, 4),
		offsetof(struct sym_hcb, scratch),
	/*
	 *  Read back the variable from memory.
	 */
	SCR_LOAD_REL (temp, 4),
		offsetof(struct sym_hcb, scratch),
}/*-------------------------< SNOOPEND >-------------------------*/,{
	/*
	 *  And stop.
	 */
	SCR_INT,
		99,
}/*-------------------------<>-----------------------------------*/
};
