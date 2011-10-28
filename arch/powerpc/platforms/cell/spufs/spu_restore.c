/*
 * spu_restore.c
 *
 * (C) Copyright IBM Corp. 2005
 *
 * SPU-side context restore sequence outlined in
 * Synergistic Processor Element Book IV
 *
 * Author: Mark Nutter <mnutter@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */


#ifndef LS_SIZE
#define LS_SIZE                 0x40000	/* 256K (in bytes) */
#endif

typedef unsigned int u32;
typedef unsigned long long u64;

#include <spu_intrinsics.h>
#include <asm/spu_csa.h>
#include "spu_utils.h"

#define BR_INSTR		0x327fff80	/* br -4         */
#define NOP_INSTR		0x40200000	/* nop           */
#define HEQ_INSTR		0x7b000000	/* heq $0, $0    */
#define STOP_INSTR		0x00000000	/* stop 0x0      */
#define ILLEGAL_INSTR		0x00800000	/* illegal instr */
#define RESTORE_COMPLETE	0x00003ffc	/* stop 0x3ffc   */

static inline void fetch_regs_from_mem(addr64 lscsa_ea)
{
	unsigned int ls = (unsigned int)&regs_spill[0];
	unsigned int size = sizeof(regs_spill);
	unsigned int tag_id = 0;
	unsigned int cmd = 0x40;	/* GET */

	spu_writech(MFC_LSA, ls);
	spu_writech(MFC_EAH, lscsa_ea.ui[0]);
	spu_writech(MFC_EAL, lscsa_ea.ui[1]);
	spu_writech(MFC_Size, size);
	spu_writech(MFC_TagID, tag_id);
	spu_writech(MFC_Cmd, cmd);
}

static inline void restore_upper_240kb(addr64 lscsa_ea)
{
	unsigned int ls = 16384;
	unsigned int list = (unsigned int)&dma_list[0];
	unsigned int size = sizeof(dma_list);
	unsigned int tag_id = 0;
	unsigned int cmd = 0x44;	/* GETL */

	/* Restore, Step 4:
	 *    Enqueue the GETL command (tag 0) to the MFC SPU command
	 *    queue to transfer the upper 240 kb of LS from CSA.
	 */
	spu_writech(MFC_LSA, ls);
	spu_writech(MFC_EAH, lscsa_ea.ui[0]);
	spu_writech(MFC_EAL, list);
	spu_writech(MFC_Size, size);
	spu_writech(MFC_TagID, tag_id);
	spu_writech(MFC_Cmd, cmd);
}

static inline void restore_decr(void)
{
	unsigned int offset;
	unsigned int decr_running;
	unsigned int decr;

	/* Restore, Step 6(moved):
	 *    If the LSCSA "decrementer running" flag is set
	 *    then write the SPU_WrDec channel with the
	 *    decrementer value from LSCSA.
	 */
	offset = LSCSA_QW_OFFSET(decr_status);
	decr_running = regs_spill[offset].slot[0] & SPU_DECR_STATUS_RUNNING;
	if (decr_running) {
		offset = LSCSA_QW_OFFSET(decr);
		decr = regs_spill[offset].slot[0];
		spu_writech(SPU_WrDec, decr);
	}
}

static inline void write_ppu_mb(void)
{
	unsigned int offset;
	unsigned int data;

	/* Restore, Step 11:
	 *    Write the MFC_WrOut_MB channel with the PPU_MB
	 *    data from LSCSA.
	 */
	offset = LSCSA_QW_OFFSET(ppu_mb);
	data = regs_spill[offset].slot[0];
	spu_writech(SPU_WrOutMbox, data);
}

static inline void write_ppuint_mb(void)
{
	unsigned int offset;
	unsigned int data;

	/* Restore, Step 12:
	 *    Write the MFC_WrInt_MB channel with the PPUINT_MB
	 *    data from LSCSA.
	 */
	offset = LSCSA_QW_OFFSET(ppuint_mb);
	data = regs_spill[offset].slot[0];
	spu_writech(SPU_WrOutIntrMbox, data);
}

static inline void restore_fpcr(void)
{
	unsigned int offset;
	vector unsigned int fpcr;

	/* Restore, Step 13:
	 *    Restore the floating-point status and control
	 *    register from the LSCSA.
	 */
	offset = LSCSA_QW_OFFSET(fpcr);
	fpcr = regs_spill[offset].v;
	spu_mtfpscr(fpcr);
}

static inline void restore_srr0(void)
{
	unsigned int offset;
	unsigned int srr0;

	/* Restore, Step 14:
	 *    Restore the SPU SRR0 data from the LSCSA.
	 */
	offset = LSCSA_QW_OFFSET(srr0);
	srr0 = regs_spill[offset].slot[0];
	spu_writech(SPU_WrSRR0, srr0);
}

static inline void restore_event_mask(void)
{
	unsigned int offset;
	unsigned int event_mask;

	/* Restore, Step 15:
	 *    Restore the SPU_RdEventMsk data from the LSCSA.
	 */
	offset = LSCSA_QW_OFFSET(event_mask);
	event_mask = regs_spill[offset].slot[0];
	spu_writech(SPU_WrEventMask, event_mask);
}

static inline void restore_tag_mask(void)
{
	unsigned int offset;
	unsigned int tag_mask;

	/* Restore, Step 16:
	 *    Restore the SPU_RdTagMsk data from the LSCSA.
	 */
	offset = LSCSA_QW_OFFSET(tag_mask);
	tag_mask = regs_spill[offset].slot[0];
	spu_writech(MFC_WrTagMask, tag_mask);
}

static inline void restore_complete(void)
{
	extern void exit_fini(void);
	unsigned int *exit_instrs = (unsigned int *)exit_fini;
	unsigned int offset;
	unsigned int stopped_status;
	unsigned int stopped_code;

	/* Restore, Step 18:
	 *    Issue a stop-and-signal instruction with
	 *    "good context restore" signal value.
	 *
	 * Restore, Step 19:
	 *    There may be additional instructions placed
	 *    here by the PPE Sequence for SPU Context
	 *    Restore in order to restore the correct
	 *    "stopped state".
	 *
	 *    This step is handled here by analyzing the
	 *    LSCSA.stopped_status and then modifying the
	 *    exit() function to behave appropriately.
	 */

	offset = LSCSA_QW_OFFSET(stopped_status);
	stopped_status = regs_spill[offset].slot[0];
	stopped_code = regs_spill[offset].slot[1];

	switch (stopped_status) {
	case SPU_STOPPED_STATUS_P_I:
		/* SPU_Status[P,I]=1.  Add illegal instruction
		 * followed by stop-and-signal instruction after
		 * end of restore code.
		 */
		exit_instrs[0] = RESTORE_COMPLETE;
		exit_instrs[1] = ILLEGAL_INSTR;
		exit_instrs[2] = STOP_INSTR | stopped_code;
		break;
	case SPU_STOPPED_STATUS_P_H:
		/* SPU_Status[P,H]=1.  Add 'heq $0, $0' followed
		 * by stop-and-signal instruction after end of
		 * restore code.
		 */
		exit_instrs[0] = RESTORE_COMPLETE;
		exit_instrs[1] = HEQ_INSTR;
		exit_instrs[2] = STOP_INSTR | stopped_code;
		break;
	case SPU_STOPPED_STATUS_S_P:
		/* SPU_Status[S,P]=1.  Add nop instruction
		 * followed by 'br -4' after end of restore
		 * code.
		 */
		exit_instrs[0] = RESTORE_COMPLETE;
		exit_instrs[1] = STOP_INSTR | stopped_code;
		exit_instrs[2] = NOP_INSTR;
		exit_instrs[3] = BR_INSTR;
		break;
	case SPU_STOPPED_STATUS_S_I:
		/* SPU_Status[S,I]=1.  Add  illegal instruction
		 * followed by 'br -4' after end of restore code.
		 */
		exit_instrs[0] = RESTORE_COMPLETE;
		exit_instrs[1] = ILLEGAL_INSTR;
		exit_instrs[2] = NOP_INSTR;
		exit_instrs[3] = BR_INSTR;
		break;
	case SPU_STOPPED_STATUS_I:
		/* SPU_Status[I]=1. Add illegal instruction followed
		 * by infinite loop after end of restore sequence.
		 */
		exit_instrs[0] = RESTORE_COMPLETE;
		exit_instrs[1] = ILLEGAL_INSTR;
		exit_instrs[2] = NOP_INSTR;
		exit_instrs[3] = BR_INSTR;
		break;
	case SPU_STOPPED_STATUS_S:
		/* SPU_Status[S]=1. Add two 'nop' instructions. */
		exit_instrs[0] = RESTORE_COMPLETE;
		exit_instrs[1] = NOP_INSTR;
		exit_instrs[2] = NOP_INSTR;
		exit_instrs[3] = BR_INSTR;
		break;
	case SPU_STOPPED_STATUS_H:
		/* SPU_Status[H]=1. Add 'heq $0, $0' instruction
		 * after end of restore code.
		 */
		exit_instrs[0] = RESTORE_COMPLETE;
		exit_instrs[1] = HEQ_INSTR;
		exit_instrs[2] = NOP_INSTR;
		exit_instrs[3] = BR_INSTR;
		break;
	case SPU_STOPPED_STATUS_P:
		/* SPU_Status[P]=1. Add stop-and-signal instruction
		 * after end of restore code.
		 */
		exit_instrs[0] = RESTORE_COMPLETE;
		exit_instrs[1] = STOP_INSTR | stopped_code;
		break;
	case SPU_STOPPED_STATUS_R:
		/* SPU_Status[I,S,H,P,R]=0. Add infinite loop. */
		exit_instrs[0] = RESTORE_COMPLETE;
		exit_instrs[1] = NOP_INSTR;
		exit_instrs[2] = NOP_INSTR;
		exit_instrs[3] = BR_INSTR;
		break;
	default:
		/* SPU_Status[R]=1. No additional instructions. */
		break;
	}
	spu_sync();
}

/**
 * main - entry point for SPU-side context restore.
 *
 * This code deviates from the documented sequence in the
 * following aspects:
 *
 *	1. The EA for LSCSA is passed from PPE in the
 *	   signal notification channels.
 *	2. The register spill area is pulled by SPU
 *	   into LS, rather than pushed by PPE.
 *	3. All 128 registers are restored by exit().
 *	4. The exit() function is modified at run
 *	   time in order to properly restore the
 *	   SPU_Status register.
 */
int main()
{
	addr64 lscsa_ea;

	lscsa_ea.ui[0] = spu_readch(SPU_RdSigNotify1);
	lscsa_ea.ui[1] = spu_readch(SPU_RdSigNotify2);
	fetch_regs_from_mem(lscsa_ea);

	set_event_mask();		/* Step 1.  */
	set_tag_mask();			/* Step 2.  */
	build_dma_list(lscsa_ea);	/* Step 3.  */
	restore_upper_240kb(lscsa_ea);	/* Step 4.  */
					/* Step 5: done by 'exit'. */
	enqueue_putllc(lscsa_ea);	/* Step 7. */
	set_tag_update();		/* Step 8. */
	read_tag_status();		/* Step 9. */
	restore_decr();			/* moved Step 6. */
	read_llar_status();		/* Step 10. */
	write_ppu_mb();			/* Step 11. */
	write_ppuint_mb();		/* Step 12. */
	restore_fpcr();			/* Step 13. */
	restore_srr0();			/* Step 14. */
	restore_event_mask();		/* Step 15. */
	restore_tag_mask();		/* Step 16. */
					/* Step 17. done by 'exit'. */
	restore_complete();		/* Step 18. */

	return 0;
}
