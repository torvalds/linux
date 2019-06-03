/* SPDX-License-Identifier: GPL-2.0-or-later */
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
	u32 start		[ 11];
	u32 getjob_begin	[  4];
	u32 _sms_a10		[  5];
	u32 getjob_end		[  4];
	u32 _sms_a20		[  4];
#ifdef SYM_CONF_TARGET_ROLE_SUPPORT
	u32 select		[  8];
#else
	u32 select		[  6];
#endif
	u32 _sms_a30		[  5];
	u32 wf_sel_done		[  2];
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
	u32 datai_done		[ 11];
	u32 datai_done_wsr	[ 20];
	u32 datao_done		[ 11];
	u32 datao_done_wss	[  6];
	u32 datai_phase		[  5];
	u32 datao_phase		[  5];
	u32 msg_in		[  2];
	u32 msg_in2		[ 10];
#ifdef SYM_CONF_IARB_SUPPORT
	u32 status		[ 14];
#else
	u32 status		[ 10];
#endif
	u32 complete		[  6];
	u32 complete2		[  8];
	u32 _sms_a40		[ 12];
	u32 done		[  5];
	u32 _sms_a50		[  5];
	u32 _sms_a60		[  2];
	u32 done_end		[  4];
	u32 complete_error	[  5];
	u32 save_dp		[ 11];
	u32 restore_dp		[  7];
	u32 disconnect		[ 11];
	u32 disconnect2		[  5];
	u32 _sms_a65		[  3];
#ifdef SYM_CONF_IARB_SUPPORT
	u32 idle		[  4];
#else
	u32 idle		[  2];
#endif
#ifdef SYM_CONF_IARB_SUPPORT
	u32 ungetjob		[  7];
#else
	u32 ungetjob		[  5];
#endif
#ifdef SYM_CONF_TARGET_ROLE_SUPPORT
	u32 reselect		[  4];
#else
	u32 reselect		[  2];
#endif
	u32 reselected		[ 19];
	u32 _sms_a70		[  6];
	u32 _sms_a80		[  4];
	u32 reselected1		[ 25];
	u32 _sms_a90		[  4];
	u32 resel_lun0		[  7];
	u32 _sms_a100		[  4];
	u32 resel_tag		[  8];
#if   SYM_CONF_MAX_TASK*4 > 512
	u32 _sms_a110		[ 23];
#elif SYM_CONF_MAX_TASK*4 > 256
	u32 _sms_a110		[ 17];
#else
	u32 _sms_a110		[ 13];
#endif
	u32 _sms_a120		[  2];
	u32 resel_go		[  4];
	u32 _sms_a130		[  7];
	u32 resel_dsa		[  2];
	u32 resel_dsa1		[  4];
	u32 _sms_a140		[  7];
	u32 resel_no_tag	[  4];
	u32 _sms_a145		[  7];
	u32 data_in		[SYM_CONF_MAX_SG * 2];
	u32 data_in2		[  4];
	u32 data_out		[SYM_CONF_MAX_SG * 2];
	u32 data_out2		[  4];
	u32 pm0_data		[ 12];
	u32 pm0_data_out	[  6];
	u32 pm0_data_end	[  7];
	u32 pm_data_end		[  4];
	u32 _sms_a150		[  4];
	u32 pm1_data		[ 12];
	u32 pm1_data_out	[  6];
	u32 pm1_data_end	[  9];
};

/*
 *  Script fragments which stay in main memory for all chips 
 *  except for chips that support 8K on-chip RAM.
 */
struct SYM_FWB_SCR {
	u32 no_data		[  2];
#ifdef SYM_CONF_TARGET_ROLE_SUPPORT
	u32 sel_for_abort	[ 18];
#else
	u32 sel_for_abort	[ 16];
#endif
	u32 sel_for_abort_1	[  2];
	u32 msg_in_etc		[ 12];
	u32 msg_received	[  5];
	u32 msg_weird_seen	[  5];
	u32 msg_extended	[ 17];
	u32 _sms_b10		[  4];
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
	u32 data_ovrun		[  3];
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
	u32 bad_status		[  7];
	u32 wsr_ma_helper	[  4];

	/* Data area */
	u32 zero		[  1];
	u32 scratch		[  1];
	u32 scratch1		[  1];
	u32 prev_done		[  1];
	u32 done_pos		[  1];
	u32 nextjob		[  1];
	u32 startpos		[  1];
	u32 targtbl		[  1];
};

/*
 *  Script fragments used at initialisations.
 *  Only runs out of main memory.
 */
struct SYM_FWZ_SCR {
	u32 snooptest		[  9];
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
	SCR_COPY (4),
		PADDR_B (startpos),
		RADDR_1 (scratcha),
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
}/*-------------------------< GETJOB_BEGIN >---------------------*/,{
	/*
	 *  Copy to a fixed location both the next STARTPOS 
	 *  and the current JOB address, using self modifying 
	 *  SCRIPTS.
	 */
	SCR_COPY (4),
		RADDR_1 (scratcha),
		PADDR_A (_sms_a10),
	SCR_COPY (8),
}/*-------------------------< _SMS_A10 >-------------------------*/,{
		0,
		PADDR_B (nextjob),
	/*
	 *  Move the start address to TEMP using self-
	 *  modifying SCRIPTS and jump indirectly to 
	 *  that address.
	 */
	SCR_COPY (4),
		PADDR_B (nextjob),
		RADDR_1 (dsa),
}/*-------------------------< GETJOB_END >-----------------------*/,{
	SCR_COPY (4),
		RADDR_1 (dsa),
		PADDR_A (_sms_a20),
	SCR_COPY (4),
}/*-------------------------< _SMS_A20 >-------------------------*/,{
		0,
		RADDR_1 (temp),
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
	 *  Copy the CCB header to a fixed location 
	 *  in the HCB using self-modifying SCRIPTS.
	 */
	SCR_COPY (4),
		RADDR_1 (dsa),
		PADDR_A (_sms_a30),
	SCR_COPY (sizeof(struct sym_ccbh)),
}/*-------------------------< _SMS_A30 >-------------------------*/,{
		0,
		HADDR_1 (ccb_head),
	/*
	 *  Initialize the status register
	 */
	SCR_COPY (4),
		HADDR_1 (ccb_head.status),
		RADDR_1 (scr0),
}/*-------------------------< WF_SEL_DONE >----------------------*/,{
	SCR_INT ^ IFFALSE (WHEN (SCR_MSG_OUT)),
		SIR_SEL_ATN_NO_MSG_OUT,
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
	SCR_COPY (4),
		RADDR_1 (temp),
		HADDR_1 (ccb_head.lastp),
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
	SCR_COPY (4),
		RADDR_1 (temp),
		HADDR_1 (ccb_head.lastp),
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
	SCR_COPY (4),
		HADDR_1 (ccb_head.lastp),
		RADDR_1 (temp),
	SCR_RETURN,
		0,
}/*-------------------------< DATAO_PHASE >----------------------*/,{
	/*
	 *  Jump to current pointer.
	 */
	SCR_COPY (4),
		HADDR_1 (ccb_head.lastp),
		RADDR_1 (temp),
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
	SCR_COPY (4),
		RADDR_1 (scr0),
		HADDR_1 (ccb_head.status),
	/*
	 *  Move back the CCB header using self-modifying 
	 *  SCRIPTS.
	 */
	SCR_COPY (4),
		RADDR_1 (dsa),
		PADDR_A (_sms_a40),
	SCR_COPY (sizeof(struct sym_ccbh)),
		HADDR_1 (ccb_head),
}/*-------------------------< _SMS_A40 >-------------------------*/,{
		0,
	/*
	 *  Some bridges may reorder DMA writes to memory.
	 *  We donnot want the CPU to deal with completions  
	 *  without all the posted write having been flushed 
	 *  to memory. This DUMMY READ should flush posted 
	 *  buffers prior to the CPU having to deal with 
	 *  completions.
	 */
	SCR_COPY (4),			/* DUMMY READ */
		HADDR_1 (ccb_head.status),
		RADDR_1 (scr0),
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
	SCR_COPY (4),
		PADDR_B (done_pos),
		PADDR_A (_sms_a50),
	SCR_COPY (4),
		RADDR_1 (dsa),
}/*-------------------------< _SMS_A50 >-------------------------*/,{
		0,
	SCR_COPY (4),
		PADDR_B (done_pos),
		PADDR_A (_sms_a60),
	/*
	 *  The instruction below reads the DONE QUEUE next 
	 *  free position from memory.
	 *  In addition it ensures that all PCI posted writes  
	 *  are flushed and so the DSA value of the done 
	 *  CCB is visible by the CPU before INTFLY is raised.
	 */
	SCR_COPY (8),
}/*-------------------------< _SMS_A60 >-------------------------*/,{
		0,
		PADDR_B (prev_done),
}/*-------------------------< DONE_END >-------------------------*/,{
	SCR_INT_FLY,
		0,
	SCR_JUMP,
		PADDR_A (start),
}/*-------------------------< COMPLETE_ERROR >-------------------*/,{
	SCR_COPY (4),
		PADDR_B (startpos),
		RADDR_1 (scratcha),
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
	SCR_COPY (4),
		HADDR_1 (ccb_head.lastp),
		HADDR_1 (ccb_head.savep),
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
	SCR_COPY (4),
		HADDR_1 (ccb_head.savep),
		HADDR_1 (ccb_head.lastp),
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
	SCR_COPY (4),
		RADDR_1 (scr0),
		HADDR_1 (ccb_head.status),
}/*-------------------------< DISCONNECT2 >----------------------*/,{
	/*
	 *  Move back the CCB header using self-modifying 
	 *  SCRIPTS.
	 */
	SCR_COPY (4),
		RADDR_1 (dsa),
		PADDR_A (_sms_a65),
	SCR_COPY (sizeof(struct sym_ccbh)),
		HADDR_1 (ccb_head),
}/*-------------------------< _SMS_A65 >-------------------------*/,{
		0,
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
	SCR_COPY (4),
		RADDR_1 (scratcha),
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
	SCR_COPY (4),
		PADDR_B (targtbl),
		RADDR_1 (dsa),
	SCR_SFBR_REG (dsa, SCR_SHL, 0),
		0,
	SCR_REG_REG (dsa, SCR_SHL, 0),
		0,
	SCR_REG_REG (dsa, SCR_AND, 0x3c),
		0,
	SCR_COPY (4),
		RADDR_1 (dsa),
		PADDR_A (_sms_a70),
	SCR_COPY (4),
}/*-------------------------< _SMS_A70 >-------------------------*/,{
		0,
		RADDR_1 (dsa),
	/*
	 *  Copy the TCB header to a fixed place in 
	 *  the HCB.
	 */
	SCR_COPY (4),
		RADDR_1 (dsa),
		PADDR_A (_sms_a80),
	SCR_COPY (sizeof(struct sym_tcbh)),
}/*-------------------------< _SMS_A80 >-------------------------*/,{
		0,
		HADDR_1 (tcb_head),
	/*
	 *  We expect MESSAGE IN phase.
	 *  If not, get help from the C code.
	 */
	SCR_INT ^ IFFALSE (WHEN (SCR_MSG_IN)),
		SIR_RESEL_NO_MSG_IN,
}/*-------------------------< RESELECTED1 >----------------------*/,{
	/*
	 *  Load the synchronous transfer registers.
	 */
	SCR_COPY (1),
		HADDR_1 (tcb_head.wval),
		RADDR_1 (scntl3),
	SCR_COPY (1),
		HADDR_1 (tcb_head.sval),
		RADDR_1 (sxfer),
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
	SCR_COPY (4),
		HADDR_1 (tcb_head.luntbl_sa),
		RADDR_1 (dsa),
	SCR_SFBR_REG (dsa, SCR_SHL, 0),
		0,
	SCR_REG_REG (dsa, SCR_SHL, 0),
		0,
	SCR_REG_REG (dsa, SCR_AND, 0xfc),
		0,
	SCR_COPY (4),
		RADDR_1 (dsa),
		PADDR_A (_sms_a90),
	SCR_COPY (4),
}/*-------------------------< _SMS_A90 >-------------------------*/,{
		0,
		RADDR_1 (dsa),
	SCR_JUMPR,
		12,
}/*-------------------------< RESEL_LUN0 >-----------------------*/,{
	/*
	 *  LUN 0 special case (but usual one :))
	 */
	SCR_COPY (4),
		HADDR_1 (tcb_head.lun0_sa),
		RADDR_1 (dsa),
	/*
	 *  Jump indirectly to the reselect action for this LUN.
	 *  (lcb.head.resel_sa assumed at offset zero of lcb).
	 */
	SCR_COPY (4),
		RADDR_1 (dsa),
		PADDR_A (_sms_a100),
	SCR_COPY (4),
}/*-------------------------< _SMS_A100 >------------------------*/,{
		0,
		RADDR_1 (temp),
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
	 *  Copy the LCB header to a fixed place in 
	 *  the HCB using self-modifying SCRIPTS.
	 */
	SCR_COPY (4),
		RADDR_1 (dsa),
		PADDR_A (_sms_a110),
	SCR_COPY (sizeof(struct sym_lcbh)),
}/*-------------------------< _SMS_A110 >------------------------*/,{
		0,
		HADDR_1 (lcb_head),
	/*
	 *  Load the pointer to the tagged task 
	 *  table for this LUN.
	 */
	SCR_COPY (4),
		HADDR_1 (lcb_head.itlq_tbl_sa),
		RADDR_1 (dsa),
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
	SCR_COPY (4),
		RADDR_1 (dsa),
		PADDR_A (_sms_a120),
	SCR_COPY (4),
}/*-------------------------< _SMS_A120 >------------------------*/,{
		0,
		RADDR_1 (dsa),
}/*-------------------------< RESEL_GO >-------------------------*/,{
	SCR_COPY (4),
		RADDR_1 (dsa),
		PADDR_A (_sms_a130),
	/*
	 *  Move 'ccb.phys.head.go' action to 
	 *  scratch/scratch1. So scratch1 will 
	 *  contain the 'restart' field of the 
	 *  'go' structure.
	 */
	SCR_COPY (8),
}/*-------------------------< _SMS_A130 >------------------------*/,{
		0,
		PADDR_B (scratch),
	SCR_COPY (4),
		PADDR_B (scratch1), /* phys.head.go.restart */
		RADDR_1 (temp),
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
	 *  Copy the CCB header to a fixed location 
	 *  in the HCB using self-modifying SCRIPTS.
	 */
	SCR_COPY (4),
		RADDR_1 (dsa),
		PADDR_A (_sms_a140),
	SCR_COPY (sizeof(struct sym_ccbh)),
}/*-------------------------< _SMS_A140 >------------------------*/,{
		0,
		HADDR_1 (ccb_head),
	/*
	 *  Initialize the status register
	 */
	SCR_COPY (4),
		HADDR_1 (ccb_head.status),
		RADDR_1 (scr0),
	/*
	 *  Jump to dispatcher.
	 */
	SCR_JUMP,
		PADDR_A (dispatch),
}/*-------------------------< RESEL_NO_TAG >---------------------*/,{
	/*
	 *  Copy the LCB header to a fixed place in 
	 *  the HCB using self-modifying SCRIPTS.
	 */
	SCR_COPY (4),
		RADDR_1 (dsa),
		PADDR_A (_sms_a145),
	SCR_COPY (sizeof(struct sym_lcbh)),
}/*-------------------------< _SMS_A145 >------------------------*/,{
		0,
		HADDR_1 (lcb_head),
	/*
	 *  Load the DSA with the unique ITL task.
	 */
	SCR_COPY (4),
		HADDR_1 (lcb_head.itl_task_sa),
		RADDR_1 (dsa),
	SCR_JUMP,
		PADDR_A (resel_go),
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
	SCR_COPY (4),
		RADDR_1 (dsa),
		RADDR_1 (scratcha),
	SCR_REG_REG (scratcha, SCR_ADD, offsetof (struct sym_ccb,phys.pm0.ret)),
		0,
}/*-------------------------< PM_DATA_END >----------------------*/,{
	SCR_COPY (4),
		RADDR_1 (scratcha),
		PADDR_A (_sms_a150),
	SCR_COPY (4),
}/*-------------------------< _SMS_A150 >------------------------*/,{
		0,
		RADDR_1 (temp),
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
	SCR_COPY (4),
		RADDR_1 (dsa),
		RADDR_1 (scratcha),
	SCR_REG_REG (scratcha, SCR_ADD, offsetof (struct sym_ccb,phys.pm1.ret)),
		0,
	SCR_JUMP,
		PADDR_A (pm_data_end),
}/*--------------------------<>----------------------------------*/
};

static struct SYM_FWB_SCR SYM_FWB_SCR = {
/*-------------------------< NO_DATA >--------------------------*/ {
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
	SCR_COPY (4),			/* DUMMY READ */
		HADDR_1 (scratch),
		RADDR_1 (scratcha),
	SCR_INT,
		SIR_MSG_RECEIVED,
}/*-------------------------< MSG_WEIRD_SEEN >-------------------*/,{
	SCR_COPY (4),			/* DUMMY READ */
		HADDR_1 (scratch),
		RADDR_1 (scratcha),
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
	 *  Read the amount of data corresponding to the 
	 *  message length and call the C code.
	 */
	SCR_COPY (1),
		RADDR_1 (scratcha),
		PADDR_B (_sms_b10),
	SCR_CLR (SCR_ACK),
		0,
}/*-------------------------< _SMS_B10 >-------------------------*/,{
	SCR_MOVE_ABS (0) ^ SCR_MSG_IN,
		HADDR_1 (msgin[2]),
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
	 *  Zero scratcha that will count the 
	 *  extras bytes.
	 */
	SCR_COPY (4),
		PADDR_B (zero),
		RADDR_1 (scratcha),
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
	SCR_COPY (4),
		PADDR_B (startpos),
		RADDR_1 (scratcha),
	SCR_INT ^ IFFALSE (DATA (S_COND_MET)),
		SIR_BAD_SCSI_STATUS,
	SCR_RETURN,
		0,
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
	SCR_DATA_ZERO, /* MUST BE BEFORE SCRATCH1 */
}/*-------------------------< SCRATCH1 >-------------------------*/,{
	SCR_DATA_ZERO,
}/*-------------------------< PREV_DONE >------------------------*/,{
	SCR_DATA_ZERO, /* MUST BE BEFORE DONE_POS ! */
}/*-------------------------< DONE_POS >-------------------------*/,{
	SCR_DATA_ZERO,
}/*-------------------------< NEXTJOB >--------------------------*/,{
	SCR_DATA_ZERO, /* MUST BE BEFORE STARTPOS ! */
}/*-------------------------< STARTPOS >-------------------------*/,{
	SCR_DATA_ZERO,
}/*-------------------------< TARGTBL >--------------------------*/,{
	SCR_DATA_ZERO,
}/*--------------------------<>----------------------------------*/
};

static struct SYM_FWZ_SCR SYM_FWZ_SCR = {
 /*-------------------------< SNOOPTEST >------------------------*/{
	/*
	 *  Read the variable.
	 */
	SCR_COPY (4),
		HADDR_1 (scratch),
		RADDR_1 (scratcha),
	/*
	 *  Write the variable.
	 */
	SCR_COPY (4),
		RADDR_1 (temp),
		HADDR_1 (scratch),
	/*
	 *  Read back the variable.
	 */
	SCR_COPY (4),
		HADDR_1 (scratch),
		RADDR_1 (temp),
}/*-------------------------< SNOOPEND >-------------------------*/,{
	/*
	 *  And stop.
	 */
	SCR_INT,
		99,
}/*--------------------------<>----------------------------------*/
};
