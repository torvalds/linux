/*
 * spu_save.c
 *
 * (C) Copyright IBM Corp. 2005
 *
 * SPU-side context save sequence outlined in
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

static inline void save_event_mask(void)
{
	unsigned int offset;

	/* Save, Step 2:
	 *    Read the SPU_RdEventMsk channel and save to the LSCSA.
	 */
	offset = LSCSA_QW_OFFSET(event_mask);
	regs_spill[offset].slot[0] = spu_readch(SPU_RdEventStatMask);
}

static inline void save_tag_mask(void)
{
	unsigned int offset;

	/* Save, Step 3:
	 *    Read the SPU_RdTagMsk channel and save to the LSCSA.
	 */
	offset = LSCSA_QW_OFFSET(tag_mask);
	regs_spill[offset].slot[0] = spu_readch(MFC_RdTagMask);
}

static inline void save_upper_240kb(addr64 lscsa_ea)
{
	unsigned int ls = 16384;
	unsigned int list = (unsigned int)&dma_list[0];
	unsigned int size = sizeof(dma_list);
	unsigned int tag_id = 0;
	unsigned int cmd = 0x24;	/* PUTL */

	/* Save, Step 7:
	 *    Enqueue the PUTL command (tag 0) to the MFC SPU command
	 *    queue to transfer the remaining 240 kb of LS to CSA.
	 */
	spu_writech(MFC_LSA, ls);
	spu_writech(MFC_EAH, lscsa_ea.ui[0]);
	spu_writech(MFC_EAL, list);
	spu_writech(MFC_Size, size);
	spu_writech(MFC_TagID, tag_id);
	spu_writech(MFC_Cmd, cmd);
}

static inline void save_fpcr(void)
{
	// vector unsigned int fpcr;
	unsigned int offset;

	/* Save, Step 9:
	 *    Issue the floating-point status and control register
	 *    read instruction, and save to the LSCSA.
	 */
	offset = LSCSA_QW_OFFSET(fpcr);
	regs_spill[offset].v = spu_mffpscr();
}

static inline void save_decr(void)
{
	unsigned int offset;

	/* Save, Step 10:
	 *    Read and save the SPU_RdDec channel data to
	 *    the LSCSA.
	 */
	offset = LSCSA_QW_OFFSET(decr);
	regs_spill[offset].slot[0] = spu_readch(SPU_RdDec);
}

static inline void save_srr0(void)
{
	unsigned int offset;

	/* Save, Step 11:
	 *    Read and save the SPU_WSRR0 channel data to
	 *    the LSCSA.
	 */
	offset = LSCSA_QW_OFFSET(srr0);
	regs_spill[offset].slot[0] = spu_readch(SPU_RdSRR0);
}

static inline void spill_regs_to_mem(addr64 lscsa_ea)
{
	unsigned int ls = (unsigned int)&regs_spill[0];
	unsigned int size = sizeof(regs_spill);
	unsigned int tag_id = 0;
	unsigned int cmd = 0x20;	/* PUT */

	/* Save, Step 13:
	 *    Enqueue a PUT command (tag 0) to send the LSCSA
	 *    to the CSA.
	 */
	spu_writech(MFC_LSA, ls);
	spu_writech(MFC_EAH, lscsa_ea.ui[0]);
	spu_writech(MFC_EAL, lscsa_ea.ui[1]);
	spu_writech(MFC_Size, size);
	spu_writech(MFC_TagID, tag_id);
	spu_writech(MFC_Cmd, cmd);
}

static inline void enqueue_sync(addr64 lscsa_ea)
{
	unsigned int tag_id = 0;
	unsigned int cmd = 0xCC;

	/* Save, Step 14:
	 *    Enqueue an MFC_SYNC command (tag 0).
	 */
	spu_writech(MFC_TagID, tag_id);
	spu_writech(MFC_Cmd, cmd);
}

static inline void save_complete(void)
{
	/* Save, Step 18:
	 *    Issue a stop-and-signal instruction indicating
	 *    "save complete".  Note: This function will not
	 *    return!!
	 */
	spu_stop(SPU_SAVE_COMPLETE);
}

/**
 * main - entry point for SPU-side context save.
 *
 * This code deviates from the documented sequence as follows:
 *
 *      1. The EA for LSCSA is passed from PPE in the
 *         signal notification channels.
 *      2. All 128 registers are saved by crt0.o.
 */
int main()
{
	addr64 lscsa_ea;

	lscsa_ea.ui[0] = spu_readch(SPU_RdSigNotify1);
	lscsa_ea.ui[1] = spu_readch(SPU_RdSigNotify2);

	/* Step 1: done by exit(). */
	save_event_mask();	/* Step 2.  */
	save_tag_mask();	/* Step 3.  */
	set_event_mask();	/* Step 4.  */
	set_tag_mask();		/* Step 5.  */
	build_dma_list(lscsa_ea);	/* Step 6.  */
	save_upper_240kb(lscsa_ea);	/* Step 7.  */
	/* Step 8: done by exit(). */
	save_fpcr();		/* Step 9.  */
	save_decr();		/* Step 10. */
	save_srr0();		/* Step 11. */
	enqueue_putllc(lscsa_ea);	/* Step 12. */
	spill_regs_to_mem(lscsa_ea);	/* Step 13. */
	enqueue_sync(lscsa_ea);	/* Step 14. */
	set_tag_update();	/* Step 15. */
	read_tag_status();	/* Step 16. */
	read_llar_status();	/* Step 17. */
	save_complete();	/* Step 18. */

	return 0;
}
