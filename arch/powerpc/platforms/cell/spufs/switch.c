/*
 * spu_switch.c
 *
 * (C) Copyright IBM Corp. 2005
 *
 * Author: Mark Nutter <mnutter@us.ibm.com>
 *
 * Host-side part of SPU context switch sequence outlined in
 * Synergistic Processor Element, Book IV.
 *
 * A fully premptive switch of an SPE is very expensive in terms
 * of time and system resources.  SPE Book IV indicates that SPE
 * allocation should follow a "serially reusable device" model,
 * in which the SPE is assigned a task until it completes.  When
 * this is not possible, this sequence may be used to premptively
 * save, and then later (optionally) restore the context of a
 * program executing on an SPE.
 *
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
 */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/smp.h>
#include <linux/stddef.h>
#include <linux/unistd.h>

#include <asm/io.h>
#include <asm/spu.h>
#include <asm/spu_priv1.h>
#include <asm/spu_csa.h>
#include <asm/mmu_context.h>

#include "spu_save_dump.h"
#include "spu_restore_dump.h"

#if 0
#define POLL_WHILE_TRUE(_c) {				\
    do {						\
    } while (_c);					\
  }
#else
#define RELAX_SPIN_COUNT				1000
#define POLL_WHILE_TRUE(_c) {				\
    do {						\
	int _i;						\
	for (_i=0; _i<RELAX_SPIN_COUNT && (_c); _i++) { \
	    cpu_relax();				\
	}						\
	if (unlikely(_c)) yield();			\
	else break;					\
    } while (_c);					\
  }
#endif				/* debug */

#define POLL_WHILE_FALSE(_c)	POLL_WHILE_TRUE(!(_c))

static inline void acquire_spu_lock(struct spu *spu)
{
	/* Save, Step 1:
	 * Restore, Step 1:
	 *    Acquire SPU-specific mutual exclusion lock.
	 *    TBD.
	 */
}

static inline void release_spu_lock(struct spu *spu)
{
	/* Restore, Step 76:
	 *    Release SPU-specific mutual exclusion lock.
	 *    TBD.
	 */
}

static inline int check_spu_isolate(struct spu_state *csa, struct spu *spu)
{
	struct spu_problem __iomem *prob = spu->problem;
	u32 isolate_state;

	/* Save, Step 2:
	 * Save, Step 6:
	 *     If SPU_Status[E,L,IS] any field is '1', this
	 *     SPU is in isolate state and cannot be context
	 *     saved at this time.
	 */
	isolate_state = SPU_STATUS_ISOLATED_STATE |
	    SPU_STATUS_ISOLATED_LOAD_STATUS | SPU_STATUS_ISOLATED_EXIT_STATUS;
	return (in_be32(&prob->spu_status_R) & isolate_state) ? 1 : 0;
}

static inline void disable_interrupts(struct spu_state *csa, struct spu *spu)
{
	/* Save, Step 3:
	 * Restore, Step 2:
	 *     Save INT_Mask_class0 in CSA.
	 *     Write INT_MASK_class0 with value of 0.
	 *     Save INT_Mask_class1 in CSA.
	 *     Write INT_MASK_class1 with value of 0.
	 *     Save INT_Mask_class2 in CSA.
	 *     Write INT_MASK_class2 with value of 0.
	 */
	spin_lock_irq(&spu->register_lock);
	if (csa) {
		csa->priv1.int_mask_class0_RW = spu_int_mask_get(spu, 0);
		csa->priv1.int_mask_class1_RW = spu_int_mask_get(spu, 1);
		csa->priv1.int_mask_class2_RW = spu_int_mask_get(spu, 2);
	}
	spu_int_mask_set(spu, 0, 0ul);
	spu_int_mask_set(spu, 1, 0ul);
	spu_int_mask_set(spu, 2, 0ul);
	eieio();
	spin_unlock_irq(&spu->register_lock);
}

static inline void set_watchdog_timer(struct spu_state *csa, struct spu *spu)
{
	/* Save, Step 4:
	 * Restore, Step 25.
	 *    Set a software watchdog timer, which specifies the
	 *    maximum allowable time for a context save sequence.
	 *
	 *    For present, this implementation will not set a global
	 *    watchdog timer, as virtualization & variable system load
	 *    may cause unpredictable execution times.
	 */
}

static inline void inhibit_user_access(struct spu_state *csa, struct spu *spu)
{
	/* Save, Step 5:
	 * Restore, Step 3:
	 *     Inhibit user-space access (if provided) to this
	 *     SPU by unmapping the virtual pages assigned to
	 *     the SPU memory-mapped I/O (MMIO) for problem
	 *     state. TBD.
	 */
}

static inline void set_switch_pending(struct spu_state *csa, struct spu *spu)
{
	/* Save, Step 7:
	 * Restore, Step 5:
	 *     Set a software context switch pending flag.
	 */
	set_bit(SPU_CONTEXT_SWITCH_PENDING, &spu->flags);
	mb();
}

static inline void save_mfc_cntl(struct spu_state *csa, struct spu *spu)
{
	struct spu_priv2 __iomem *priv2 = spu->priv2;

	/* Save, Step 8:
	 *     Suspend DMA and save MFC_CNTL.
	 */
	switch (in_be64(&priv2->mfc_control_RW) &
	       MFC_CNTL_SUSPEND_DMA_STATUS_MASK) {
	case MFC_CNTL_SUSPEND_IN_PROGRESS:
		POLL_WHILE_FALSE((in_be64(&priv2->mfc_control_RW) &
				  MFC_CNTL_SUSPEND_DMA_STATUS_MASK) ==
				 MFC_CNTL_SUSPEND_COMPLETE);
		/* fall through */
	case MFC_CNTL_SUSPEND_COMPLETE:
		if (csa) {
			csa->priv2.mfc_control_RW =
				MFC_CNTL_SUSPEND_MASK |
				MFC_CNTL_SUSPEND_DMA_QUEUE;
		}
		break;
	case MFC_CNTL_NORMAL_DMA_QUEUE_OPERATION:
		out_be64(&priv2->mfc_control_RW, MFC_CNTL_SUSPEND_DMA_QUEUE);
		POLL_WHILE_FALSE((in_be64(&priv2->mfc_control_RW) &
				  MFC_CNTL_SUSPEND_DMA_STATUS_MASK) ==
				 MFC_CNTL_SUSPEND_COMPLETE);
		if (csa) {
			csa->priv2.mfc_control_RW = 0;
		}
		break;
	}
}

static inline void save_spu_runcntl(struct spu_state *csa, struct spu *spu)
{
	struct spu_problem __iomem *prob = spu->problem;

	/* Save, Step 9:
	 *     Save SPU_Runcntl in the CSA.  This value contains
	 *     the "Application Desired State".
	 */
	csa->prob.spu_runcntl_RW = in_be32(&prob->spu_runcntl_RW);
}

static inline void save_mfc_sr1(struct spu_state *csa, struct spu *spu)
{
	/* Save, Step 10:
	 *     Save MFC_SR1 in the CSA.
	 */
	csa->priv1.mfc_sr1_RW = spu_mfc_sr1_get(spu);
}

static inline void save_spu_status(struct spu_state *csa, struct spu *spu)
{
	struct spu_problem __iomem *prob = spu->problem;

	/* Save, Step 11:
	 *     Read SPU_Status[R], and save to CSA.
	 */
	if ((in_be32(&prob->spu_status_R) & SPU_STATUS_RUNNING) == 0) {
		csa->prob.spu_status_R = in_be32(&prob->spu_status_R);
	} else {
		u32 stopped;

		out_be32(&prob->spu_runcntl_RW, SPU_RUNCNTL_STOP);
		eieio();
		POLL_WHILE_TRUE(in_be32(&prob->spu_status_R) &
				SPU_STATUS_RUNNING);
		stopped =
		    SPU_STATUS_INVALID_INSTR | SPU_STATUS_SINGLE_STEP |
		    SPU_STATUS_STOPPED_BY_HALT | SPU_STATUS_STOPPED_BY_STOP;
		if ((in_be32(&prob->spu_status_R) & stopped) == 0)
			csa->prob.spu_status_R = SPU_STATUS_RUNNING;
		else
			csa->prob.spu_status_R = in_be32(&prob->spu_status_R);
	}
}

static inline void save_mfc_decr(struct spu_state *csa, struct spu *spu)
{
	struct spu_priv2 __iomem *priv2 = spu->priv2;

	/* Save, Step 12:
	 *     Read MFC_CNTL[Ds].  Update saved copy of
	 *     CSA.MFC_CNTL[Ds].
	 */
	csa->priv2.mfc_control_RW |=
		in_be64(&priv2->mfc_control_RW) & MFC_CNTL_DECREMENTER_RUNNING;
}

static inline void halt_mfc_decr(struct spu_state *csa, struct spu *spu)
{
	struct spu_priv2 __iomem *priv2 = spu->priv2;

	/* Save, Step 13:
	 *     Write MFC_CNTL[Dh] set to a '1' to halt
	 *     the decrementer.
	 */
	out_be64(&priv2->mfc_control_RW,
		 MFC_CNTL_DECREMENTER_HALTED | MFC_CNTL_SUSPEND_MASK);
	eieio();
}

static inline void save_timebase(struct spu_state *csa, struct spu *spu)
{
	/* Save, Step 14:
	 *    Read PPE Timebase High and Timebase low registers
	 *    and save in CSA.  TBD.
	 */
	csa->suspend_time = get_cycles();
}

static inline void remove_other_spu_access(struct spu_state *csa,
					   struct spu *spu)
{
	/* Save, Step 15:
	 *     Remove other SPU access to this SPU by unmapping
	 *     this SPU's pages from their address space.  TBD.
	 */
}

static inline void do_mfc_mssync(struct spu_state *csa, struct spu *spu)
{
	struct spu_problem __iomem *prob = spu->problem;

	/* Save, Step 16:
	 * Restore, Step 11.
	 *     Write SPU_MSSync register. Poll SPU_MSSync[P]
	 *     for a value of 0.
	 */
	out_be64(&prob->spc_mssync_RW, 1UL);
	POLL_WHILE_TRUE(in_be64(&prob->spc_mssync_RW) & MS_SYNC_PENDING);
}

static inline void issue_mfc_tlbie(struct spu_state *csa, struct spu *spu)
{
	/* Save, Step 17:
	 * Restore, Step 12.
	 * Restore, Step 48.
	 *     Write TLB_Invalidate_Entry[IS,VPN,L,Lp]=0 register.
	 *     Then issue a PPE sync instruction.
	 */
	spu_tlb_invalidate(spu);
	mb();
}

static inline void handle_pending_interrupts(struct spu_state *csa,
					     struct spu *spu)
{
	/* Save, Step 18:
	 *     Handle any pending interrupts from this SPU
	 *     here.  This is OS or hypervisor specific.  One
	 *     option is to re-enable interrupts to handle any
	 *     pending interrupts, with the interrupt handlers
	 *     recognizing the software Context Switch Pending
	 *     flag, to ensure the SPU execution or MFC command
	 *     queue is not restarted.  TBD.
	 */
}

static inline void save_mfc_queues(struct spu_state *csa, struct spu *spu)
{
	struct spu_priv2 __iomem *priv2 = spu->priv2;
	int i;

	/* Save, Step 19:
	 *     If MFC_Cntl[Se]=0 then save
	 *     MFC command queues.
	 */
	if ((in_be64(&priv2->mfc_control_RW) & MFC_CNTL_DMA_QUEUES_EMPTY) == 0) {
		for (i = 0; i < 8; i++) {
			csa->priv2.puq[i].mfc_cq_data0_RW =
			    in_be64(&priv2->puq[i].mfc_cq_data0_RW);
			csa->priv2.puq[i].mfc_cq_data1_RW =
			    in_be64(&priv2->puq[i].mfc_cq_data1_RW);
			csa->priv2.puq[i].mfc_cq_data2_RW =
			    in_be64(&priv2->puq[i].mfc_cq_data2_RW);
			csa->priv2.puq[i].mfc_cq_data3_RW =
			    in_be64(&priv2->puq[i].mfc_cq_data3_RW);
		}
		for (i = 0; i < 16; i++) {
			csa->priv2.spuq[i].mfc_cq_data0_RW =
			    in_be64(&priv2->spuq[i].mfc_cq_data0_RW);
			csa->priv2.spuq[i].mfc_cq_data1_RW =
			    in_be64(&priv2->spuq[i].mfc_cq_data1_RW);
			csa->priv2.spuq[i].mfc_cq_data2_RW =
			    in_be64(&priv2->spuq[i].mfc_cq_data2_RW);
			csa->priv2.spuq[i].mfc_cq_data3_RW =
			    in_be64(&priv2->spuq[i].mfc_cq_data3_RW);
		}
	}
}

static inline void save_ppu_querymask(struct spu_state *csa, struct spu *spu)
{
	struct spu_problem __iomem *prob = spu->problem;

	/* Save, Step 20:
	 *     Save the PPU_QueryMask register
	 *     in the CSA.
	 */
	csa->prob.dma_querymask_RW = in_be32(&prob->dma_querymask_RW);
}

static inline void save_ppu_querytype(struct spu_state *csa, struct spu *spu)
{
	struct spu_problem __iomem *prob = spu->problem;

	/* Save, Step 21:
	 *     Save the PPU_QueryType register
	 *     in the CSA.
	 */
	csa->prob.dma_querytype_RW = in_be32(&prob->dma_querytype_RW);
}

static inline void save_ppu_tagstatus(struct spu_state *csa, struct spu *spu)
{
	struct spu_problem __iomem *prob = spu->problem;

	/* Save the Prxy_TagStatus register in the CSA.
	 *
	 * It is unnecessary to restore dma_tagstatus_R, however,
	 * dma_tagstatus_R in the CSA is accessed via backing_ops, so
	 * we must save it.
	 */
	csa->prob.dma_tagstatus_R = in_be32(&prob->dma_tagstatus_R);
}

static inline void save_mfc_csr_tsq(struct spu_state *csa, struct spu *spu)
{
	struct spu_priv2 __iomem *priv2 = spu->priv2;

	/* Save, Step 22:
	 *     Save the MFC_CSR_TSQ register
	 *     in the LSCSA.
	 */
	csa->priv2.spu_tag_status_query_RW =
	    in_be64(&priv2->spu_tag_status_query_RW);
}

static inline void save_mfc_csr_cmd(struct spu_state *csa, struct spu *spu)
{
	struct spu_priv2 __iomem *priv2 = spu->priv2;

	/* Save, Step 23:
	 *     Save the MFC_CSR_CMD1 and MFC_CSR_CMD2
	 *     registers in the CSA.
	 */
	csa->priv2.spu_cmd_buf1_RW = in_be64(&priv2->spu_cmd_buf1_RW);
	csa->priv2.spu_cmd_buf2_RW = in_be64(&priv2->spu_cmd_buf2_RW);
}

static inline void save_mfc_csr_ato(struct spu_state *csa, struct spu *spu)
{
	struct spu_priv2 __iomem *priv2 = spu->priv2;

	/* Save, Step 24:
	 *     Save the MFC_CSR_ATO register in
	 *     the CSA.
	 */
	csa->priv2.spu_atomic_status_RW = in_be64(&priv2->spu_atomic_status_RW);
}

static inline void save_mfc_tclass_id(struct spu_state *csa, struct spu *spu)
{
	/* Save, Step 25:
	 *     Save the MFC_TCLASS_ID register in
	 *     the CSA.
	 */
	csa->priv1.mfc_tclass_id_RW = spu_mfc_tclass_id_get(spu);
}

static inline void set_mfc_tclass_id(struct spu_state *csa, struct spu *spu)
{
	/* Save, Step 26:
	 * Restore, Step 23.
	 *     Write the MFC_TCLASS_ID register with
	 *     the value 0x10000000.
	 */
	spu_mfc_tclass_id_set(spu, 0x10000000);
	eieio();
}

static inline void purge_mfc_queue(struct spu_state *csa, struct spu *spu)
{
	struct spu_priv2 __iomem *priv2 = spu->priv2;

	/* Save, Step 27:
	 * Restore, Step 14.
	 *     Write MFC_CNTL[Pc]=1 (purge queue).
	 */
	out_be64(&priv2->mfc_control_RW, MFC_CNTL_PURGE_DMA_REQUEST);
	eieio();
}

static inline void wait_purge_complete(struct spu_state *csa, struct spu *spu)
{
	struct spu_priv2 __iomem *priv2 = spu->priv2;

	/* Save, Step 28:
	 *     Poll MFC_CNTL[Ps] until value '11' is read
	 *     (purge complete).
	 */
	POLL_WHILE_FALSE((in_be64(&priv2->mfc_control_RW) &
			 MFC_CNTL_PURGE_DMA_STATUS_MASK) ==
			 MFC_CNTL_PURGE_DMA_COMPLETE);
}

static inline void setup_mfc_sr1(struct spu_state *csa, struct spu *spu)
{
	/* Save, Step 30:
	 * Restore, Step 18:
	 *     Write MFC_SR1 with MFC_SR1[D=0,S=1] and
	 *     MFC_SR1[TL,R,Pr,T] set correctly for the
	 *     OS specific environment.
	 *
	 *     Implementation note: The SPU-side code
	 *     for save/restore is privileged, so the
	 *     MFC_SR1[Pr] bit is not set.
	 *
	 */
	spu_mfc_sr1_set(spu, (MFC_STATE1_MASTER_RUN_CONTROL_MASK |
			      MFC_STATE1_RELOCATE_MASK |
			      MFC_STATE1_BUS_TLBIE_MASK));
}

static inline void save_spu_npc(struct spu_state *csa, struct spu *spu)
{
	struct spu_problem __iomem *prob = spu->problem;

	/* Save, Step 31:
	 *     Save SPU_NPC in the CSA.
	 */
	csa->prob.spu_npc_RW = in_be32(&prob->spu_npc_RW);
}

static inline void save_spu_privcntl(struct spu_state *csa, struct spu *spu)
{
	struct spu_priv2 __iomem *priv2 = spu->priv2;

	/* Save, Step 32:
	 *     Save SPU_PrivCntl in the CSA.
	 */
	csa->priv2.spu_privcntl_RW = in_be64(&priv2->spu_privcntl_RW);
}

static inline void reset_spu_privcntl(struct spu_state *csa, struct spu *spu)
{
	struct spu_priv2 __iomem *priv2 = spu->priv2;

	/* Save, Step 33:
	 * Restore, Step 16:
	 *     Write SPU_PrivCntl[S,Le,A] fields reset to 0.
	 */
	out_be64(&priv2->spu_privcntl_RW, 0UL);
	eieio();
}

static inline void save_spu_lslr(struct spu_state *csa, struct spu *spu)
{
	struct spu_priv2 __iomem *priv2 = spu->priv2;

	/* Save, Step 34:
	 *     Save SPU_LSLR in the CSA.
	 */
	csa->priv2.spu_lslr_RW = in_be64(&priv2->spu_lslr_RW);
}

static inline void reset_spu_lslr(struct spu_state *csa, struct spu *spu)
{
	struct spu_priv2 __iomem *priv2 = spu->priv2;

	/* Save, Step 35:
	 * Restore, Step 17.
	 *     Reset SPU_LSLR.
	 */
	out_be64(&priv2->spu_lslr_RW, LS_ADDR_MASK);
	eieio();
}

static inline void save_spu_cfg(struct spu_state *csa, struct spu *spu)
{
	struct spu_priv2 __iomem *priv2 = spu->priv2;

	/* Save, Step 36:
	 *     Save SPU_Cfg in the CSA.
	 */
	csa->priv2.spu_cfg_RW = in_be64(&priv2->spu_cfg_RW);
}

static inline void save_pm_trace(struct spu_state *csa, struct spu *spu)
{
	/* Save, Step 37:
	 *     Save PM_Trace_Tag_Wait_Mask in the CSA.
	 *     Not performed by this implementation.
	 */
}

static inline void save_mfc_rag(struct spu_state *csa, struct spu *spu)
{
	/* Save, Step 38:
	 *     Save RA_GROUP_ID register and the
	 *     RA_ENABLE reigster in the CSA.
	 */
	csa->priv1.resource_allocation_groupID_RW =
		spu_resource_allocation_groupID_get(spu);
	csa->priv1.resource_allocation_enable_RW =
		spu_resource_allocation_enable_get(spu);
}

static inline void save_ppu_mb_stat(struct spu_state *csa, struct spu *spu)
{
	struct spu_problem __iomem *prob = spu->problem;

	/* Save, Step 39:
	 *     Save MB_Stat register in the CSA.
	 */
	csa->prob.mb_stat_R = in_be32(&prob->mb_stat_R);
}

static inline void save_ppu_mb(struct spu_state *csa, struct spu *spu)
{
	struct spu_problem __iomem *prob = spu->problem;

	/* Save, Step 40:
	 *     Save the PPU_MB register in the CSA.
	 */
	csa->prob.pu_mb_R = in_be32(&prob->pu_mb_R);
}

static inline void save_ppuint_mb(struct spu_state *csa, struct spu *spu)
{
	struct spu_priv2 __iomem *priv2 = spu->priv2;

	/* Save, Step 41:
	 *     Save the PPUINT_MB register in the CSA.
	 */
	csa->priv2.puint_mb_R = in_be64(&priv2->puint_mb_R);
}

static inline void save_ch_part1(struct spu_state *csa, struct spu *spu)
{
	struct spu_priv2 __iomem *priv2 = spu->priv2;
	u64 idx, ch_indices[] = { 0UL, 3UL, 4UL, 24UL, 25UL, 27UL };
	int i;

	/* Save, Step 42:
	 */

	/* Save CH 1, without channel count */
	out_be64(&priv2->spu_chnlcntptr_RW, 1);
	csa->spu_chnldata_RW[1] = in_be64(&priv2->spu_chnldata_RW);

	/* Save the following CH: [0,3,4,24,25,27] */
	for (i = 0; i < ARRAY_SIZE(ch_indices); i++) {
		idx = ch_indices[i];
		out_be64(&priv2->spu_chnlcntptr_RW, idx);
		eieio();
		csa->spu_chnldata_RW[idx] = in_be64(&priv2->spu_chnldata_RW);
		csa->spu_chnlcnt_RW[idx] = in_be64(&priv2->spu_chnlcnt_RW);
		out_be64(&priv2->spu_chnldata_RW, 0UL);
		out_be64(&priv2->spu_chnlcnt_RW, 0UL);
		eieio();
	}
}

static inline void save_spu_mb(struct spu_state *csa, struct spu *spu)
{
	struct spu_priv2 __iomem *priv2 = spu->priv2;
	int i;

	/* Save, Step 43:
	 *     Save SPU Read Mailbox Channel.
	 */
	out_be64(&priv2->spu_chnlcntptr_RW, 29UL);
	eieio();
	csa->spu_chnlcnt_RW[29] = in_be64(&priv2->spu_chnlcnt_RW);
	for (i = 0; i < 4; i++) {
		csa->spu_mailbox_data[i] = in_be64(&priv2->spu_chnldata_RW);
	}
	out_be64(&priv2->spu_chnlcnt_RW, 0UL);
	eieio();
}

static inline void save_mfc_cmd(struct spu_state *csa, struct spu *spu)
{
	struct spu_priv2 __iomem *priv2 = spu->priv2;

	/* Save, Step 44:
	 *     Save MFC_CMD Channel.
	 */
	out_be64(&priv2->spu_chnlcntptr_RW, 21UL);
	eieio();
	csa->spu_chnlcnt_RW[21] = in_be64(&priv2->spu_chnlcnt_RW);
	eieio();
}

static inline void reset_ch(struct spu_state *csa, struct spu *spu)
{
	struct spu_priv2 __iomem *priv2 = spu->priv2;
	u64 ch_indices[4] = { 21UL, 23UL, 28UL, 30UL };
	u64 ch_counts[4] = { 16UL, 1UL, 1UL, 1UL };
	u64 idx;
	int i;

	/* Save, Step 45:
	 *     Reset the following CH: [21, 23, 28, 30]
	 */
	for (i = 0; i < 4; i++) {
		idx = ch_indices[i];
		out_be64(&priv2->spu_chnlcntptr_RW, idx);
		eieio();
		out_be64(&priv2->spu_chnlcnt_RW, ch_counts[i]);
		eieio();
	}
}

static inline void resume_mfc_queue(struct spu_state *csa, struct spu *spu)
{
	struct spu_priv2 __iomem *priv2 = spu->priv2;

	/* Save, Step 46:
	 * Restore, Step 25.
	 *     Write MFC_CNTL[Sc]=0 (resume queue processing).
	 */
	out_be64(&priv2->mfc_control_RW, MFC_CNTL_RESUME_DMA_QUEUE);
}

static inline void get_kernel_slb(u64 ea, u64 slb[2])
{
	u64 llp;

	if (REGION_ID(ea) == KERNEL_REGION_ID)
		llp = mmu_psize_defs[mmu_linear_psize].sllp;
	else
		llp = mmu_psize_defs[mmu_virtual_psize].sllp;
	slb[0] = (get_kernel_vsid(ea) << SLB_VSID_SHIFT) |
		SLB_VSID_KERNEL | llp;
	slb[1] = (ea & ESID_MASK) | SLB_ESID_V;
}

static inline void load_mfc_slb(struct spu *spu, u64 slb[2], int slbe)
{
	struct spu_priv2 __iomem *priv2 = spu->priv2;

	out_be64(&priv2->slb_index_W, slbe);
	eieio();
	out_be64(&priv2->slb_vsid_RW, slb[0]);
	out_be64(&priv2->slb_esid_RW, slb[1]);
	eieio();
}

static inline void setup_mfc_slbs(struct spu_state *csa, struct spu *spu)
{
	u64 code_slb[2];
	u64 lscsa_slb[2];

	/* Save, Step 47:
	 * Restore, Step 30.
	 *     If MFC_SR1[R]=1, write 0 to SLB_Invalidate_All
	 *     register, then initialize SLB_VSID and SLB_ESID
	 *     to provide access to SPU context save code and
	 *     LSCSA.
	 *
	 *     This implementation places both the context
	 *     switch code and LSCSA in kernel address space.
	 *
	 *     Further this implementation assumes that the
	 *     MFC_SR1[R]=1 (in other words, assume that
	 *     translation is desired by OS environment).
	 */
	spu_invalidate_slbs(spu);
	get_kernel_slb((unsigned long)&spu_save_code[0], code_slb);
	get_kernel_slb((unsigned long)csa->lscsa, lscsa_slb);
	load_mfc_slb(spu, code_slb, 0);
	if ((lscsa_slb[0] != code_slb[0]) || (lscsa_slb[1] != code_slb[1]))
		load_mfc_slb(spu, lscsa_slb, 1);
}

static inline void set_switch_active(struct spu_state *csa, struct spu *spu)
{
	/* Save, Step 48:
	 * Restore, Step 23.
	 *     Change the software context switch pending flag
	 *     to context switch active.
	 */
	set_bit(SPU_CONTEXT_SWITCH_ACTIVE, &spu->flags);
	clear_bit(SPU_CONTEXT_SWITCH_PENDING, &spu->flags);
	mb();
}

static inline void enable_interrupts(struct spu_state *csa, struct spu *spu)
{
	unsigned long class1_mask = CLASS1_ENABLE_SEGMENT_FAULT_INTR |
	    CLASS1_ENABLE_STORAGE_FAULT_INTR;

	/* Save, Step 49:
	 * Restore, Step 22:
	 *     Reset and then enable interrupts, as
	 *     needed by OS.
	 *
	 *     This implementation enables only class1
	 *     (translation) interrupts.
	 */
	spin_lock_irq(&spu->register_lock);
	spu_int_stat_clear(spu, 0, ~0ul);
	spu_int_stat_clear(spu, 1, ~0ul);
	spu_int_stat_clear(spu, 2, ~0ul);
	spu_int_mask_set(spu, 0, 0ul);
	spu_int_mask_set(spu, 1, class1_mask);
	spu_int_mask_set(spu, 2, 0ul);
	spin_unlock_irq(&spu->register_lock);
}

static inline int send_mfc_dma(struct spu *spu, unsigned long ea,
			       unsigned int ls_offset, unsigned int size,
			       unsigned int tag, unsigned int rclass,
			       unsigned int cmd)
{
	struct spu_problem __iomem *prob = spu->problem;
	union mfc_tag_size_class_cmd command;
	unsigned int transfer_size;
	volatile unsigned int status = 0x0;

	while (size > 0) {
		transfer_size =
		    (size > MFC_MAX_DMA_SIZE) ? MFC_MAX_DMA_SIZE : size;
		command.u.mfc_size = transfer_size;
		command.u.mfc_tag = tag;
		command.u.mfc_rclassid = rclass;
		command.u.mfc_cmd = cmd;
		do {
			out_be32(&prob->mfc_lsa_W, ls_offset);
			out_be64(&prob->mfc_ea_W, ea);
			out_be64(&prob->mfc_union_W.all64, command.all64);
			status =
			    in_be32(&prob->mfc_union_W.by32.mfc_class_cmd32);
			if (unlikely(status & 0x2)) {
				cpu_relax();
			}
		} while (status & 0x3);
		size -= transfer_size;
		ea += transfer_size;
		ls_offset += transfer_size;
	}
	return 0;
}

static inline void save_ls_16kb(struct spu_state *csa, struct spu *spu)
{
	unsigned long addr = (unsigned long)&csa->lscsa->ls[0];
	unsigned int ls_offset = 0x0;
	unsigned int size = 16384;
	unsigned int tag = 0;
	unsigned int rclass = 0;
	unsigned int cmd = MFC_PUT_CMD;

	/* Save, Step 50:
	 *     Issue a DMA command to copy the first 16K bytes
	 *     of local storage to the CSA.
	 */
	send_mfc_dma(spu, addr, ls_offset, size, tag, rclass, cmd);
}

static inline void set_spu_npc(struct spu_state *csa, struct spu *spu)
{
	struct spu_problem __iomem *prob = spu->problem;

	/* Save, Step 51:
	 * Restore, Step 31.
	 *     Write SPU_NPC[IE]=0 and SPU_NPC[LSA] to entry
	 *     point address of context save code in local
	 *     storage.
	 *
	 *     This implementation uses SPU-side save/restore
	 *     programs with entry points at LSA of 0.
	 */
	out_be32(&prob->spu_npc_RW, 0);
	eieio();
}

static inline void set_signot1(struct spu_state *csa, struct spu *spu)
{
	struct spu_problem __iomem *prob = spu->problem;
	union {
		u64 ull;
		u32 ui[2];
	} addr64;

	/* Save, Step 52:
	 * Restore, Step 32:
	 *    Write SPU_Sig_Notify_1 register with upper 32-bits
	 *    of the CSA.LSCSA effective address.
	 */
	addr64.ull = (u64) csa->lscsa;
	out_be32(&prob->signal_notify1, addr64.ui[0]);
	eieio();
}

static inline void set_signot2(struct spu_state *csa, struct spu *spu)
{
	struct spu_problem __iomem *prob = spu->problem;
	union {
		u64 ull;
		u32 ui[2];
	} addr64;

	/* Save, Step 53:
	 * Restore, Step 33:
	 *    Write SPU_Sig_Notify_2 register with lower 32-bits
	 *    of the CSA.LSCSA effective address.
	 */
	addr64.ull = (u64) csa->lscsa;
	out_be32(&prob->signal_notify2, addr64.ui[1]);
	eieio();
}

static inline void send_save_code(struct spu_state *csa, struct spu *spu)
{
	unsigned long addr = (unsigned long)&spu_save_code[0];
	unsigned int ls_offset = 0x0;
	unsigned int size = sizeof(spu_save_code);
	unsigned int tag = 0;
	unsigned int rclass = 0;
	unsigned int cmd = MFC_GETFS_CMD;

	/* Save, Step 54:
	 *     Issue a DMA command to copy context save code
	 *     to local storage and start SPU.
	 */
	send_mfc_dma(spu, addr, ls_offset, size, tag, rclass, cmd);
}

static inline void set_ppu_querymask(struct spu_state *csa, struct spu *spu)
{
	struct spu_problem __iomem *prob = spu->problem;

	/* Save, Step 55:
	 * Restore, Step 38.
	 *     Write PPU_QueryMask=1 (enable Tag Group 0)
	 *     and issue eieio instruction.
	 */
	out_be32(&prob->dma_querymask_RW, MFC_TAGID_TO_TAGMASK(0));
	eieio();
}

static inline void wait_tag_complete(struct spu_state *csa, struct spu *spu)
{
	struct spu_problem __iomem *prob = spu->problem;
	u32 mask = MFC_TAGID_TO_TAGMASK(0);
	unsigned long flags;

	/* Save, Step 56:
	 * Restore, Step 39.
	 * Restore, Step 39.
	 * Restore, Step 46.
	 *     Poll PPU_TagStatus[gn] until 01 (Tag group 0 complete)
	 *     or write PPU_QueryType[TS]=01 and wait for Tag Group
	 *     Complete Interrupt.  Write INT_Stat_Class0 or
	 *     INT_Stat_Class2 with value of 'handled'.
	 */
	POLL_WHILE_FALSE(in_be32(&prob->dma_tagstatus_R) & mask);

	local_irq_save(flags);
	spu_int_stat_clear(spu, 0, ~(0ul));
	spu_int_stat_clear(spu, 2, ~(0ul));
	local_irq_restore(flags);
}

static inline void wait_spu_stopped(struct spu_state *csa, struct spu *spu)
{
	struct spu_problem __iomem *prob = spu->problem;
	unsigned long flags;

	/* Save, Step 57:
	 * Restore, Step 40.
	 *     Poll until SPU_Status[R]=0 or wait for SPU Class 0
	 *     or SPU Class 2 interrupt.  Write INT_Stat_class0
	 *     or INT_Stat_class2 with value of handled.
	 */
	POLL_WHILE_TRUE(in_be32(&prob->spu_status_R) & SPU_STATUS_RUNNING);

	local_irq_save(flags);
	spu_int_stat_clear(spu, 0, ~(0ul));
	spu_int_stat_clear(spu, 2, ~(0ul));
	local_irq_restore(flags);
}

static inline int check_save_status(struct spu_state *csa, struct spu *spu)
{
	struct spu_problem __iomem *prob = spu->problem;
	u32 complete;

	/* Save, Step 54:
	 *     If SPU_Status[P]=1 and SPU_Status[SC] = "success",
	 *     context save succeeded, otherwise context save
	 *     failed.
	 */
	complete = ((SPU_SAVE_COMPLETE << SPU_STOP_STATUS_SHIFT) |
		    SPU_STATUS_STOPPED_BY_STOP);
	return (in_be32(&prob->spu_status_R) != complete) ? 1 : 0;
}

static inline void terminate_spu_app(struct spu_state *csa, struct spu *spu)
{
	/* Restore, Step 4:
	 *    If required, notify the "using application" that
	 *    the SPU task has been terminated.  TBD.
	 */
}

static inline void suspend_mfc_and_halt_decr(struct spu_state *csa,
		struct spu *spu)
{
	struct spu_priv2 __iomem *priv2 = spu->priv2;

	/* Restore, Step 7:
	 *     Write MFC_Cntl[Dh,Sc,Sm]='1','1','0' to suspend
	 *     the queue and halt the decrementer.
	 */
	out_be64(&priv2->mfc_control_RW, MFC_CNTL_SUSPEND_DMA_QUEUE |
		 MFC_CNTL_DECREMENTER_HALTED);
	eieio();
}

static inline void wait_suspend_mfc_complete(struct spu_state *csa,
					     struct spu *spu)
{
	struct spu_priv2 __iomem *priv2 = spu->priv2;

	/* Restore, Step 8:
	 * Restore, Step 47.
	 *     Poll MFC_CNTL[Ss] until 11 is returned.
	 */
	POLL_WHILE_FALSE((in_be64(&priv2->mfc_control_RW) &
			 MFC_CNTL_SUSPEND_DMA_STATUS_MASK) ==
			 MFC_CNTL_SUSPEND_COMPLETE);
}

static inline int suspend_spe(struct spu_state *csa, struct spu *spu)
{
	struct spu_problem __iomem *prob = spu->problem;

	/* Restore, Step 9:
	 *    If SPU_Status[R]=1, stop SPU execution
	 *    and wait for stop to complete.
	 *
	 *    Returns       1 if SPU_Status[R]=1 on entry.
	 *                  0 otherwise
	 */
	if (in_be32(&prob->spu_status_R) & SPU_STATUS_RUNNING) {
		if (in_be32(&prob->spu_status_R) &
		    SPU_STATUS_ISOLATED_EXIT_STATUS) {
			POLL_WHILE_TRUE(in_be32(&prob->spu_status_R) &
					SPU_STATUS_RUNNING);
		}
		if ((in_be32(&prob->spu_status_R) &
		     SPU_STATUS_ISOLATED_LOAD_STATUS)
		    || (in_be32(&prob->spu_status_R) &
			SPU_STATUS_ISOLATED_STATE)) {
			out_be32(&prob->spu_runcntl_RW, SPU_RUNCNTL_STOP);
			eieio();
			POLL_WHILE_TRUE(in_be32(&prob->spu_status_R) &
					SPU_STATUS_RUNNING);
			out_be32(&prob->spu_runcntl_RW, 0x2);
			eieio();
			POLL_WHILE_TRUE(in_be32(&prob->spu_status_R) &
					SPU_STATUS_RUNNING);
		}
		if (in_be32(&prob->spu_status_R) &
		    SPU_STATUS_WAITING_FOR_CHANNEL) {
			out_be32(&prob->spu_runcntl_RW, SPU_RUNCNTL_STOP);
			eieio();
			POLL_WHILE_TRUE(in_be32(&prob->spu_status_R) &
					SPU_STATUS_RUNNING);
		}
		return 1;
	}
	return 0;
}

static inline void clear_spu_status(struct spu_state *csa, struct spu *spu)
{
	struct spu_problem __iomem *prob = spu->problem;

	/* Restore, Step 10:
	 *    If SPU_Status[R]=0 and SPU_Status[E,L,IS]=1,
	 *    release SPU from isolate state.
	 */
	if (!(in_be32(&prob->spu_status_R) & SPU_STATUS_RUNNING)) {
		if (in_be32(&prob->spu_status_R) &
		    SPU_STATUS_ISOLATED_EXIT_STATUS) {
			spu_mfc_sr1_set(spu,
					MFC_STATE1_MASTER_RUN_CONTROL_MASK);
			eieio();
			out_be32(&prob->spu_runcntl_RW, SPU_RUNCNTL_RUNNABLE);
			eieio();
			POLL_WHILE_TRUE(in_be32(&prob->spu_status_R) &
					SPU_STATUS_RUNNING);
		}
		if ((in_be32(&prob->spu_status_R) &
		     SPU_STATUS_ISOLATED_LOAD_STATUS)
		    || (in_be32(&prob->spu_status_R) &
			SPU_STATUS_ISOLATED_STATE)) {
			spu_mfc_sr1_set(spu,
					MFC_STATE1_MASTER_RUN_CONTROL_MASK);
			eieio();
			out_be32(&prob->spu_runcntl_RW, 0x2);
			eieio();
			POLL_WHILE_TRUE(in_be32(&prob->spu_status_R) &
					SPU_STATUS_RUNNING);
		}
	}
}

static inline void reset_ch_part1(struct spu_state *csa, struct spu *spu)
{
	struct spu_priv2 __iomem *priv2 = spu->priv2;
	u64 ch_indices[] = { 0UL, 3UL, 4UL, 24UL, 25UL, 27UL };
	u64 idx;
	int i;

	/* Restore, Step 20:
	 */

	/* Reset CH 1 */
	out_be64(&priv2->spu_chnlcntptr_RW, 1);
	out_be64(&priv2->spu_chnldata_RW, 0UL);

	/* Reset the following CH: [0,3,4,24,25,27] */
	for (i = 0; i < ARRAY_SIZE(ch_indices); i++) {
		idx = ch_indices[i];
		out_be64(&priv2->spu_chnlcntptr_RW, idx);
		eieio();
		out_be64(&priv2->spu_chnldata_RW, 0UL);
		out_be64(&priv2->spu_chnlcnt_RW, 0UL);
		eieio();
	}
}

static inline void reset_ch_part2(struct spu_state *csa, struct spu *spu)
{
	struct spu_priv2 __iomem *priv2 = spu->priv2;
	u64 ch_indices[5] = { 21UL, 23UL, 28UL, 29UL, 30UL };
	u64 ch_counts[5] = { 16UL, 1UL, 1UL, 0UL, 1UL };
	u64 idx;
	int i;

	/* Restore, Step 21:
	 *     Reset the following CH: [21, 23, 28, 29, 30]
	 */
	for (i = 0; i < 5; i++) {
		idx = ch_indices[i];
		out_be64(&priv2->spu_chnlcntptr_RW, idx);
		eieio();
		out_be64(&priv2->spu_chnlcnt_RW, ch_counts[i]);
		eieio();
	}
}

static inline void setup_spu_status_part1(struct spu_state *csa,
					  struct spu *spu)
{
	u32 status_P = SPU_STATUS_STOPPED_BY_STOP;
	u32 status_I = SPU_STATUS_INVALID_INSTR;
	u32 status_H = SPU_STATUS_STOPPED_BY_HALT;
	u32 status_S = SPU_STATUS_SINGLE_STEP;
	u32 status_S_I = SPU_STATUS_SINGLE_STEP | SPU_STATUS_INVALID_INSTR;
	u32 status_S_P = SPU_STATUS_SINGLE_STEP | SPU_STATUS_STOPPED_BY_STOP;
	u32 status_P_H = SPU_STATUS_STOPPED_BY_HALT |SPU_STATUS_STOPPED_BY_STOP;
	u32 status_P_I = SPU_STATUS_STOPPED_BY_STOP |SPU_STATUS_INVALID_INSTR;
	u32 status_code;

	/* Restore, Step 27:
	 *     If the CSA.SPU_Status[I,S,H,P]=1 then add the correct
	 *     instruction sequence to the end of the SPU based restore
	 *     code (after the "context restored" stop and signal) to
	 *     restore the correct SPU status.
	 *
	 *     NOTE: Rather than modifying the SPU executable, we
	 *     instead add a new 'stopped_status' field to the
	 *     LSCSA.  The SPU-side restore reads this field and
	 *     takes the appropriate action when exiting.
	 */

	status_code =
	    (csa->prob.spu_status_R >> SPU_STOP_STATUS_SHIFT) & 0xFFFF;
	if ((csa->prob.spu_status_R & status_P_I) == status_P_I) {

		/* SPU_Status[P,I]=1 - Illegal Instruction followed
		 * by Stop and Signal instruction, followed by 'br -4'.
		 *
		 */
		csa->lscsa->stopped_status.slot[0] = SPU_STOPPED_STATUS_P_I;
		csa->lscsa->stopped_status.slot[1] = status_code;

	} else if ((csa->prob.spu_status_R & status_P_H) == status_P_H) {

		/* SPU_Status[P,H]=1 - Halt Conditional, followed
		 * by Stop and Signal instruction, followed by
		 * 'br -4'.
		 */
		csa->lscsa->stopped_status.slot[0] = SPU_STOPPED_STATUS_P_H;
		csa->lscsa->stopped_status.slot[1] = status_code;

	} else if ((csa->prob.spu_status_R & status_S_P) == status_S_P) {

		/* SPU_Status[S,P]=1 - Stop and Signal instruction
		 * followed by 'br -4'.
		 */
		csa->lscsa->stopped_status.slot[0] = SPU_STOPPED_STATUS_S_P;
		csa->lscsa->stopped_status.slot[1] = status_code;

	} else if ((csa->prob.spu_status_R & status_S_I) == status_S_I) {

		/* SPU_Status[S,I]=1 - Illegal instruction followed
		 * by 'br -4'.
		 */
		csa->lscsa->stopped_status.slot[0] = SPU_STOPPED_STATUS_S_I;
		csa->lscsa->stopped_status.slot[1] = status_code;

	} else if ((csa->prob.spu_status_R & status_P) == status_P) {

		/* SPU_Status[P]=1 - Stop and Signal instruction
		 * followed by 'br -4'.
		 */
		csa->lscsa->stopped_status.slot[0] = SPU_STOPPED_STATUS_P;
		csa->lscsa->stopped_status.slot[1] = status_code;

	} else if ((csa->prob.spu_status_R & status_H) == status_H) {

		/* SPU_Status[H]=1 - Halt Conditional, followed
		 * by 'br -4'.
		 */
		csa->lscsa->stopped_status.slot[0] = SPU_STOPPED_STATUS_H;

	} else if ((csa->prob.spu_status_R & status_S) == status_S) {

		/* SPU_Status[S]=1 - Two nop instructions.
		 */
		csa->lscsa->stopped_status.slot[0] = SPU_STOPPED_STATUS_S;

	} else if ((csa->prob.spu_status_R & status_I) == status_I) {

		/* SPU_Status[I]=1 - Illegal instruction followed
		 * by 'br -4'.
		 */
		csa->lscsa->stopped_status.slot[0] = SPU_STOPPED_STATUS_I;

	}
}

static inline void setup_spu_status_part2(struct spu_state *csa,
					  struct spu *spu)
{
	u32 mask;

	/* Restore, Step 28:
	 *     If the CSA.SPU_Status[I,S,H,P,R]=0 then
	 *     add a 'br *' instruction to the end of
	 *     the SPU based restore code.
	 *
	 *     NOTE: Rather than modifying the SPU executable, we
	 *     instead add a new 'stopped_status' field to the
	 *     LSCSA.  The SPU-side restore reads this field and
	 *     takes the appropriate action when exiting.
	 */
	mask = SPU_STATUS_INVALID_INSTR |
	    SPU_STATUS_SINGLE_STEP |
	    SPU_STATUS_STOPPED_BY_HALT |
	    SPU_STATUS_STOPPED_BY_STOP | SPU_STATUS_RUNNING;
	if (!(csa->prob.spu_status_R & mask)) {
		csa->lscsa->stopped_status.slot[0] = SPU_STOPPED_STATUS_R;
	}
}

static inline void restore_mfc_rag(struct spu_state *csa, struct spu *spu)
{
	/* Restore, Step 29:
	 *     Restore RA_GROUP_ID register and the
	 *     RA_ENABLE reigster from the CSA.
	 */
	spu_resource_allocation_groupID_set(spu,
			csa->priv1.resource_allocation_groupID_RW);
	spu_resource_allocation_enable_set(spu,
			csa->priv1.resource_allocation_enable_RW);
}

static inline void send_restore_code(struct spu_state *csa, struct spu *spu)
{
	unsigned long addr = (unsigned long)&spu_restore_code[0];
	unsigned int ls_offset = 0x0;
	unsigned int size = sizeof(spu_restore_code);
	unsigned int tag = 0;
	unsigned int rclass = 0;
	unsigned int cmd = MFC_GETFS_CMD;

	/* Restore, Step 37:
	 *     Issue MFC DMA command to copy context
	 *     restore code to local storage.
	 */
	send_mfc_dma(spu, addr, ls_offset, size, tag, rclass, cmd);
}

static inline void setup_decr(struct spu_state *csa, struct spu *spu)
{
	/* Restore, Step 34:
	 *     If CSA.MFC_CNTL[Ds]=1 (decrementer was
	 *     running) then adjust decrementer, set
	 *     decrementer running status in LSCSA,
	 *     and set decrementer "wrapped" status
	 *     in LSCSA.
	 */
	if (csa->priv2.mfc_control_RW & MFC_CNTL_DECREMENTER_RUNNING) {
		cycles_t resume_time = get_cycles();
		cycles_t delta_time = resume_time - csa->suspend_time;

		csa->lscsa->decr_status.slot[0] = SPU_DECR_STATUS_RUNNING;
		if (csa->lscsa->decr.slot[0] < delta_time) {
			csa->lscsa->decr_status.slot[0] |=
				 SPU_DECR_STATUS_WRAPPED;
		}

		csa->lscsa->decr.slot[0] -= delta_time;
	} else {
		csa->lscsa->decr_status.slot[0] = 0;
	}
}

static inline void setup_ppu_mb(struct spu_state *csa, struct spu *spu)
{
	/* Restore, Step 35:
	 *     Copy the CSA.PU_MB data into the LSCSA.
	 */
	csa->lscsa->ppu_mb.slot[0] = csa->prob.pu_mb_R;
}

static inline void setup_ppuint_mb(struct spu_state *csa, struct spu *spu)
{
	/* Restore, Step 36:
	 *     Copy the CSA.PUINT_MB data into the LSCSA.
	 */
	csa->lscsa->ppuint_mb.slot[0] = csa->priv2.puint_mb_R;
}

static inline int check_restore_status(struct spu_state *csa, struct spu *spu)
{
	struct spu_problem __iomem *prob = spu->problem;
	u32 complete;

	/* Restore, Step 40:
	 *     If SPU_Status[P]=1 and SPU_Status[SC] = "success",
	 *     context restore succeeded, otherwise context restore
	 *     failed.
	 */
	complete = ((SPU_RESTORE_COMPLETE << SPU_STOP_STATUS_SHIFT) |
		    SPU_STATUS_STOPPED_BY_STOP);
	return (in_be32(&prob->spu_status_R) != complete) ? 1 : 0;
}

static inline void restore_spu_privcntl(struct spu_state *csa, struct spu *spu)
{
	struct spu_priv2 __iomem *priv2 = spu->priv2;

	/* Restore, Step 41:
	 *     Restore SPU_PrivCntl from the CSA.
	 */
	out_be64(&priv2->spu_privcntl_RW, csa->priv2.spu_privcntl_RW);
	eieio();
}

static inline void restore_status_part1(struct spu_state *csa, struct spu *spu)
{
	struct spu_problem __iomem *prob = spu->problem;
	u32 mask;

	/* Restore, Step 42:
	 *     If any CSA.SPU_Status[I,S,H,P]=1, then
	 *     restore the error or single step state.
	 */
	mask = SPU_STATUS_INVALID_INSTR |
	    SPU_STATUS_SINGLE_STEP |
	    SPU_STATUS_STOPPED_BY_HALT | SPU_STATUS_STOPPED_BY_STOP;
	if (csa->prob.spu_status_R & mask) {
		out_be32(&prob->spu_runcntl_RW, SPU_RUNCNTL_RUNNABLE);
		eieio();
		POLL_WHILE_TRUE(in_be32(&prob->spu_status_R) &
				SPU_STATUS_RUNNING);
	}
}

static inline void restore_status_part2(struct spu_state *csa, struct spu *spu)
{
	struct spu_problem __iomem *prob = spu->problem;
	u32 mask;

	/* Restore, Step 43:
	 *     If all CSA.SPU_Status[I,S,H,P,R]=0 then write
	 *     SPU_RunCntl[R0R1]='01', wait for SPU_Status[R]=1,
	 *     then write '00' to SPU_RunCntl[R0R1] and wait
	 *     for SPU_Status[R]=0.
	 */
	mask = SPU_STATUS_INVALID_INSTR |
	    SPU_STATUS_SINGLE_STEP |
	    SPU_STATUS_STOPPED_BY_HALT |
	    SPU_STATUS_STOPPED_BY_STOP | SPU_STATUS_RUNNING;
	if (!(csa->prob.spu_status_R & mask)) {
		out_be32(&prob->spu_runcntl_RW, SPU_RUNCNTL_RUNNABLE);
		eieio();
		POLL_WHILE_FALSE(in_be32(&prob->spu_status_R) &
				 SPU_STATUS_RUNNING);
		out_be32(&prob->spu_runcntl_RW, SPU_RUNCNTL_STOP);
		eieio();
		POLL_WHILE_TRUE(in_be32(&prob->spu_status_R) &
				SPU_STATUS_RUNNING);
	}
}

static inline void restore_ls_16kb(struct spu_state *csa, struct spu *spu)
{
	unsigned long addr = (unsigned long)&csa->lscsa->ls[0];
	unsigned int ls_offset = 0x0;
	unsigned int size = 16384;
	unsigned int tag = 0;
	unsigned int rclass = 0;
	unsigned int cmd = MFC_GET_CMD;

	/* Restore, Step 44:
	 *     Issue a DMA command to restore the first
	 *     16kb of local storage from CSA.
	 */
	send_mfc_dma(spu, addr, ls_offset, size, tag, rclass, cmd);
}

static inline void suspend_mfc(struct spu_state *csa, struct spu *spu)
{
	struct spu_priv2 __iomem *priv2 = spu->priv2;

	/* Restore, Step 47.
	 *     Write MFC_Cntl[Sc,Sm]='1','0' to suspend
	 *     the queue.
	 */
	out_be64(&priv2->mfc_control_RW, MFC_CNTL_SUSPEND_DMA_QUEUE);
	eieio();
}

static inline void clear_interrupts(struct spu_state *csa, struct spu *spu)
{
	/* Restore, Step 49:
	 *     Write INT_MASK_class0 with value of 0.
	 *     Write INT_MASK_class1 with value of 0.
	 *     Write INT_MASK_class2 with value of 0.
	 *     Write INT_STAT_class0 with value of -1.
	 *     Write INT_STAT_class1 with value of -1.
	 *     Write INT_STAT_class2 with value of -1.
	 */
	spin_lock_irq(&spu->register_lock);
	spu_int_mask_set(spu, 0, 0ul);
	spu_int_mask_set(spu, 1, 0ul);
	spu_int_mask_set(spu, 2, 0ul);
	spu_int_stat_clear(spu, 0, ~0ul);
	spu_int_stat_clear(spu, 1, ~0ul);
	spu_int_stat_clear(spu, 2, ~0ul);
	spin_unlock_irq(&spu->register_lock);
}

static inline void restore_mfc_queues(struct spu_state *csa, struct spu *spu)
{
	struct spu_priv2 __iomem *priv2 = spu->priv2;
	int i;

	/* Restore, Step 50:
	 *     If MFC_Cntl[Se]!=0 then restore
	 *     MFC command queues.
	 */
	if ((csa->priv2.mfc_control_RW & MFC_CNTL_DMA_QUEUES_EMPTY_MASK) == 0) {
		for (i = 0; i < 8; i++) {
			out_be64(&priv2->puq[i].mfc_cq_data0_RW,
				 csa->priv2.puq[i].mfc_cq_data0_RW);
			out_be64(&priv2->puq[i].mfc_cq_data1_RW,
				 csa->priv2.puq[i].mfc_cq_data1_RW);
			out_be64(&priv2->puq[i].mfc_cq_data2_RW,
				 csa->priv2.puq[i].mfc_cq_data2_RW);
			out_be64(&priv2->puq[i].mfc_cq_data3_RW,
				 csa->priv2.puq[i].mfc_cq_data3_RW);
		}
		for (i = 0; i < 16; i++) {
			out_be64(&priv2->spuq[i].mfc_cq_data0_RW,
				 csa->priv2.spuq[i].mfc_cq_data0_RW);
			out_be64(&priv2->spuq[i].mfc_cq_data1_RW,
				 csa->priv2.spuq[i].mfc_cq_data1_RW);
			out_be64(&priv2->spuq[i].mfc_cq_data2_RW,
				 csa->priv2.spuq[i].mfc_cq_data2_RW);
			out_be64(&priv2->spuq[i].mfc_cq_data3_RW,
				 csa->priv2.spuq[i].mfc_cq_data3_RW);
		}
	}
	eieio();
}

static inline void restore_ppu_querymask(struct spu_state *csa, struct spu *spu)
{
	struct spu_problem __iomem *prob = spu->problem;

	/* Restore, Step 51:
	 *     Restore the PPU_QueryMask register from CSA.
	 */
	out_be32(&prob->dma_querymask_RW, csa->prob.dma_querymask_RW);
	eieio();
}

static inline void restore_ppu_querytype(struct spu_state *csa, struct spu *spu)
{
	struct spu_problem __iomem *prob = spu->problem;

	/* Restore, Step 52:
	 *     Restore the PPU_QueryType register from CSA.
	 */
	out_be32(&prob->dma_querytype_RW, csa->prob.dma_querytype_RW);
	eieio();
}

static inline void restore_mfc_csr_tsq(struct spu_state *csa, struct spu *spu)
{
	struct spu_priv2 __iomem *priv2 = spu->priv2;

	/* Restore, Step 53:
	 *     Restore the MFC_CSR_TSQ register from CSA.
	 */
	out_be64(&priv2->spu_tag_status_query_RW,
		 csa->priv2.spu_tag_status_query_RW);
	eieio();
}

static inline void restore_mfc_csr_cmd(struct spu_state *csa, struct spu *spu)
{
	struct spu_priv2 __iomem *priv2 = spu->priv2;

	/* Restore, Step 54:
	 *     Restore the MFC_CSR_CMD1 and MFC_CSR_CMD2
	 *     registers from CSA.
	 */
	out_be64(&priv2->spu_cmd_buf1_RW, csa->priv2.spu_cmd_buf1_RW);
	out_be64(&priv2->spu_cmd_buf2_RW, csa->priv2.spu_cmd_buf2_RW);
	eieio();
}

static inline void restore_mfc_csr_ato(struct spu_state *csa, struct spu *spu)
{
	struct spu_priv2 __iomem *priv2 = spu->priv2;

	/* Restore, Step 55:
	 *     Restore the MFC_CSR_ATO register from CSA.
	 */
	out_be64(&priv2->spu_atomic_status_RW, csa->priv2.spu_atomic_status_RW);
}

static inline void restore_mfc_tclass_id(struct spu_state *csa, struct spu *spu)
{
	/* Restore, Step 56:
	 *     Restore the MFC_TCLASS_ID register from CSA.
	 */
	spu_mfc_tclass_id_set(spu, csa->priv1.mfc_tclass_id_RW);
	eieio();
}

static inline void set_llr_event(struct spu_state *csa, struct spu *spu)
{
	u64 ch0_cnt, ch0_data;
	u64 ch1_data;

	/* Restore, Step 57:
	 *    Set the Lock Line Reservation Lost Event by:
	 *      1. OR CSA.SPU_Event_Status with bit 21 (Lr) set to 1.
	 *      2. If CSA.SPU_Channel_0_Count=0 and
	 *         CSA.SPU_Wr_Event_Mask[Lr]=1 and
	 *         CSA.SPU_Event_Status[Lr]=0 then set
	 *         CSA.SPU_Event_Status_Count=1.
	 */
	ch0_cnt = csa->spu_chnlcnt_RW[0];
	ch0_data = csa->spu_chnldata_RW[0];
	ch1_data = csa->spu_chnldata_RW[1];
	csa->spu_chnldata_RW[0] |= MFC_LLR_LOST_EVENT;
	if ((ch0_cnt == 0) && !(ch0_data & MFC_LLR_LOST_EVENT) &&
	    (ch1_data & MFC_LLR_LOST_EVENT)) {
		csa->spu_chnlcnt_RW[0] = 1;
	}
}

static inline void restore_decr_wrapped(struct spu_state *csa, struct spu *spu)
{
	/* Restore, Step 58:
	 *     If the status of the CSA software decrementer
	 *     "wrapped" flag is set, OR in a '1' to
	 *     CSA.SPU_Event_Status[Tm].
	 */
	if (csa->lscsa->decr_status.slot[0] & SPU_DECR_STATUS_WRAPPED) {
		csa->spu_chnldata_RW[0] |= 0x20;
	}
	if ((csa->lscsa->decr_status.slot[0] & SPU_DECR_STATUS_WRAPPED) &&
	    (csa->spu_chnlcnt_RW[0] == 0 &&
	     ((csa->spu_chnldata_RW[2] & 0x20) == 0x0) &&
	     ((csa->spu_chnldata_RW[0] & 0x20) != 0x1))) {
		csa->spu_chnlcnt_RW[0] = 1;
	}
}

static inline void restore_ch_part1(struct spu_state *csa, struct spu *spu)
{
	struct spu_priv2 __iomem *priv2 = spu->priv2;
	u64 idx, ch_indices[] = { 0UL, 3UL, 4UL, 24UL, 25UL, 27UL };
	int i;

	/* Restore, Step 59:
	 *	Restore the following CH: [0,3,4,24,25,27]
	 */
	for (i = 0; i < ARRAY_SIZE(ch_indices); i++) {
		idx = ch_indices[i];
		out_be64(&priv2->spu_chnlcntptr_RW, idx);
		eieio();
		out_be64(&priv2->spu_chnldata_RW, csa->spu_chnldata_RW[idx]);
		out_be64(&priv2->spu_chnlcnt_RW, csa->spu_chnlcnt_RW[idx]);
		eieio();
	}
}

static inline void restore_ch_part2(struct spu_state *csa, struct spu *spu)
{
	struct spu_priv2 __iomem *priv2 = spu->priv2;
	u64 ch_indices[3] = { 9UL, 21UL, 23UL };
	u64 ch_counts[3] = { 1UL, 16UL, 1UL };
	u64 idx;
	int i;

	/* Restore, Step 60:
	 *     Restore the following CH: [9,21,23].
	 */
	ch_counts[0] = 1UL;
	ch_counts[1] = csa->spu_chnlcnt_RW[21];
	ch_counts[2] = 1UL;
	for (i = 0; i < 3; i++) {
		idx = ch_indices[i];
		out_be64(&priv2->spu_chnlcntptr_RW, idx);
		eieio();
		out_be64(&priv2->spu_chnlcnt_RW, ch_counts[i]);
		eieio();
	}
}

static inline void restore_spu_lslr(struct spu_state *csa, struct spu *spu)
{
	struct spu_priv2 __iomem *priv2 = spu->priv2;

	/* Restore, Step 61:
	 *     Restore the SPU_LSLR register from CSA.
	 */
	out_be64(&priv2->spu_lslr_RW, csa->priv2.spu_lslr_RW);
	eieio();
}

static inline void restore_spu_cfg(struct spu_state *csa, struct spu *spu)
{
	struct spu_priv2 __iomem *priv2 = spu->priv2;

	/* Restore, Step 62:
	 *     Restore the SPU_Cfg register from CSA.
	 */
	out_be64(&priv2->spu_cfg_RW, csa->priv2.spu_cfg_RW);
	eieio();
}

static inline void restore_pm_trace(struct spu_state *csa, struct spu *spu)
{
	/* Restore, Step 63:
	 *     Restore PM_Trace_Tag_Wait_Mask from CSA.
	 *     Not performed by this implementation.
	 */
}

static inline void restore_spu_npc(struct spu_state *csa, struct spu *spu)
{
	struct spu_problem __iomem *prob = spu->problem;

	/* Restore, Step 64:
	 *     Restore SPU_NPC from CSA.
	 */
	out_be32(&prob->spu_npc_RW, csa->prob.spu_npc_RW);
	eieio();
}

static inline void restore_spu_mb(struct spu_state *csa, struct spu *spu)
{
	struct spu_priv2 __iomem *priv2 = spu->priv2;
	int i;

	/* Restore, Step 65:
	 *     Restore MFC_RdSPU_MB from CSA.
	 */
	out_be64(&priv2->spu_chnlcntptr_RW, 29UL);
	eieio();
	out_be64(&priv2->spu_chnlcnt_RW, csa->spu_chnlcnt_RW[29]);
	for (i = 0; i < 4; i++) {
		out_be64(&priv2->spu_chnldata_RW, csa->spu_mailbox_data[i]);
	}
	eieio();
}

static inline void check_ppu_mb_stat(struct spu_state *csa, struct spu *spu)
{
	struct spu_problem __iomem *prob = spu->problem;
	u32 dummy = 0;

	/* Restore, Step 66:
	 *     If CSA.MB_Stat[P]=0 (mailbox empty) then
	 *     read from the PPU_MB register.
	 */
	if ((csa->prob.mb_stat_R & 0xFF) == 0) {
		dummy = in_be32(&prob->pu_mb_R);
		eieio();
	}
}

static inline void check_ppuint_mb_stat(struct spu_state *csa, struct spu *spu)
{
	struct spu_priv2 __iomem *priv2 = spu->priv2;
	u64 dummy = 0UL;

	/* Restore, Step 66:
	 *     If CSA.MB_Stat[I]=0 (mailbox empty) then
	 *     read from the PPUINT_MB register.
	 */
	if ((csa->prob.mb_stat_R & 0xFF0000) == 0) {
		dummy = in_be64(&priv2->puint_mb_R);
		eieio();
		spu_int_stat_clear(spu, 2, CLASS2_ENABLE_MAILBOX_INTR);
		eieio();
	}
}

static inline void restore_mfc_sr1(struct spu_state *csa, struct spu *spu)
{
	/* Restore, Step 69:
	 *     Restore the MFC_SR1 register from CSA.
	 */
	spu_mfc_sr1_set(spu, csa->priv1.mfc_sr1_RW);
	eieio();
}

static inline void restore_other_spu_access(struct spu_state *csa,
					    struct spu *spu)
{
	/* Restore, Step 70:
	 *     Restore other SPU mappings to this SPU. TBD.
	 */
}

static inline void restore_spu_runcntl(struct spu_state *csa, struct spu *spu)
{
	struct spu_problem __iomem *prob = spu->problem;

	/* Restore, Step 71:
	 *     If CSA.SPU_Status[R]=1 then write
	 *     SPU_RunCntl[R0R1]='01'.
	 */
	if (csa->prob.spu_status_R & SPU_STATUS_RUNNING) {
		out_be32(&prob->spu_runcntl_RW, SPU_RUNCNTL_RUNNABLE);
		eieio();
	}
}

static inline void restore_mfc_cntl(struct spu_state *csa, struct spu *spu)
{
	struct spu_priv2 __iomem *priv2 = spu->priv2;

	/* Restore, Step 72:
	 *    Restore the MFC_CNTL register for the CSA.
	 */
	out_be64(&priv2->mfc_control_RW, csa->priv2.mfc_control_RW);
	eieio();
	/*
	 * FIXME: this is to restart a DMA that we were processing
	 *        before the save. better remember the fault information
	 *        in the csa instead.
	 */
	if ((csa->priv2.mfc_control_RW & MFC_CNTL_SUSPEND_DMA_QUEUE_MASK)) {
		out_be64(&priv2->mfc_control_RW, MFC_CNTL_RESTART_DMA_COMMAND);
		eieio();
	}
}

static inline void enable_user_access(struct spu_state *csa, struct spu *spu)
{
	/* Restore, Step 73:
	 *     Enable user-space access (if provided) to this
	 *     SPU by mapping the virtual pages assigned to
	 *     the SPU memory-mapped I/O (MMIO) for problem
	 *     state. TBD.
	 */
}

static inline void reset_switch_active(struct spu_state *csa, struct spu *spu)
{
	/* Restore, Step 74:
	 *     Reset the "context switch active" flag.
	 */
	clear_bit(SPU_CONTEXT_SWITCH_ACTIVE, &spu->flags);
	mb();
}

static inline void reenable_interrupts(struct spu_state *csa, struct spu *spu)
{
	/* Restore, Step 75:
	 *     Re-enable SPU interrupts.
	 */
	spin_lock_irq(&spu->register_lock);
	spu_int_mask_set(spu, 0, csa->priv1.int_mask_class0_RW);
	spu_int_mask_set(spu, 1, csa->priv1.int_mask_class1_RW);
	spu_int_mask_set(spu, 2, csa->priv1.int_mask_class2_RW);
	spin_unlock_irq(&spu->register_lock);
}

static int quiece_spu(struct spu_state *prev, struct spu *spu)
{
	/*
	 * Combined steps 2-18 of SPU context save sequence, which
	 * quiesce the SPU state (disable SPU execution, MFC command
	 * queues, decrementer, SPU interrupts, etc.).
	 *
	 * Returns      0 on success.
	 *              2 if failed step 2.
	 *              6 if failed step 6.
	 */

	if (check_spu_isolate(prev, spu)) {	/* Step 2. */
		return 2;
	}
	disable_interrupts(prev, spu);	        /* Step 3. */
	set_watchdog_timer(prev, spu);	        /* Step 4. */
	inhibit_user_access(prev, spu);	        /* Step 5. */
	if (check_spu_isolate(prev, spu)) {	/* Step 6. */
		return 6;
	}
	set_switch_pending(prev, spu);	        /* Step 7. */
	save_mfc_cntl(prev, spu);		/* Step 8. */
	save_spu_runcntl(prev, spu);	        /* Step 9. */
	save_mfc_sr1(prev, spu);	        /* Step 10. */
	save_spu_status(prev, spu);	        /* Step 11. */
	save_mfc_decr(prev, spu);	        /* Step 12. */
	halt_mfc_decr(prev, spu);	        /* Step 13. */
	save_timebase(prev, spu);		/* Step 14. */
	remove_other_spu_access(prev, spu);	/* Step 15. */
	do_mfc_mssync(prev, spu);	        /* Step 16. */
	issue_mfc_tlbie(prev, spu);	        /* Step 17. */
	handle_pending_interrupts(prev, spu);	/* Step 18. */

	return 0;
}

static void save_csa(struct spu_state *prev, struct spu *spu)
{
	/*
	 * Combine steps 19-44 of SPU context save sequence, which
	 * save regions of the privileged & problem state areas.
	 */

	save_mfc_queues(prev, spu);	/* Step 19. */
	save_ppu_querymask(prev, spu);	/* Step 20. */
	save_ppu_querytype(prev, spu);	/* Step 21. */
	save_ppu_tagstatus(prev, spu);  /* NEW.     */
	save_mfc_csr_tsq(prev, spu);	/* Step 22. */
	save_mfc_csr_cmd(prev, spu);	/* Step 23. */
	save_mfc_csr_ato(prev, spu);	/* Step 24. */
	save_mfc_tclass_id(prev, spu);	/* Step 25. */
	set_mfc_tclass_id(prev, spu);	/* Step 26. */
	purge_mfc_queue(prev, spu);	/* Step 27. */
	wait_purge_complete(prev, spu);	/* Step 28. */
	setup_mfc_sr1(prev, spu);	/* Step 30. */
	save_spu_npc(prev, spu);	/* Step 31. */
	save_spu_privcntl(prev, spu);	/* Step 32. */
	reset_spu_privcntl(prev, spu);	/* Step 33. */
	save_spu_lslr(prev, spu);	/* Step 34. */
	reset_spu_lslr(prev, spu);	/* Step 35. */
	save_spu_cfg(prev, spu);	/* Step 36. */
	save_pm_trace(prev, spu);	/* Step 37. */
	save_mfc_rag(prev, spu);	/* Step 38. */
	save_ppu_mb_stat(prev, spu);	/* Step 39. */
	save_ppu_mb(prev, spu);	        /* Step 40. */
	save_ppuint_mb(prev, spu);	/* Step 41. */
	save_ch_part1(prev, spu);	/* Step 42. */
	save_spu_mb(prev, spu);	        /* Step 43. */
	save_mfc_cmd(prev, spu);	/* Step 44. */
	reset_ch(prev, spu);	        /* Step 45. */
}

static void save_lscsa(struct spu_state *prev, struct spu *spu)
{
	/*
	 * Perform steps 46-57 of SPU context save sequence,
	 * which save regions of the local store and register
	 * file.
	 */

	resume_mfc_queue(prev, spu);	/* Step 46. */
	setup_mfc_slbs(prev, spu);	/* Step 47. */
	set_switch_active(prev, spu);	/* Step 48. */
	enable_interrupts(prev, spu);	/* Step 49. */
	save_ls_16kb(prev, spu);	/* Step 50. */
	set_spu_npc(prev, spu);	        /* Step 51. */
	set_signot1(prev, spu);		/* Step 52. */
	set_signot2(prev, spu);		/* Step 53. */
	send_save_code(prev, spu);	/* Step 54. */
	set_ppu_querymask(prev, spu);	/* Step 55. */
	wait_tag_complete(prev, spu);	/* Step 56. */
	wait_spu_stopped(prev, spu);	/* Step 57. */
}

static void force_spu_isolate_exit(struct spu *spu)
{
	struct spu_problem __iomem *prob = spu->problem;
	struct spu_priv2 __iomem *priv2 = spu->priv2;

	/* Stop SPE execution and wait for completion. */
	out_be32(&prob->spu_runcntl_RW, SPU_RUNCNTL_STOP);
	iobarrier_rw();
	POLL_WHILE_TRUE(in_be32(&prob->spu_status_R) & SPU_STATUS_RUNNING);

	/* Restart SPE master runcntl. */
	spu_mfc_sr1_set(spu, MFC_STATE1_MASTER_RUN_CONTROL_MASK);
	iobarrier_w();

	/* Initiate isolate exit request and wait for completion. */
	out_be64(&priv2->spu_privcntl_RW, 4LL);
	iobarrier_w();
	out_be32(&prob->spu_runcntl_RW, 2);
	iobarrier_rw();
	POLL_WHILE_FALSE((in_be32(&prob->spu_status_R)
				& SPU_STATUS_STOPPED_BY_STOP));

	/* Reset load request to normal. */
	out_be64(&priv2->spu_privcntl_RW, SPU_PRIVCNT_LOAD_REQUEST_NORMAL);
	iobarrier_w();
}

/**
 * stop_spu_isolate
 *	Check SPU run-control state and force isolated
 *	exit function as necessary.
 */
static void stop_spu_isolate(struct spu *spu)
{
	struct spu_problem __iomem *prob = spu->problem;

	if (in_be32(&prob->spu_status_R) & SPU_STATUS_ISOLATED_STATE) {
		/* The SPU is in isolated state; the only way
		 * to get it out is to perform an isolated
		 * exit (clean) operation.
		 */
		force_spu_isolate_exit(spu);
	}
}

static void harvest(struct spu_state *prev, struct spu *spu)
{
	/*
	 * Perform steps 2-25 of SPU context restore sequence,
	 * which resets an SPU either after a failed save, or
	 * when using SPU for first time.
	 */

	disable_interrupts(prev, spu);	        /* Step 2.  */
	inhibit_user_access(prev, spu);	        /* Step 3.  */
	terminate_spu_app(prev, spu);	        /* Step 4.  */
	set_switch_pending(prev, spu);	        /* Step 5.  */
	stop_spu_isolate(spu);			/* NEW.     */
	remove_other_spu_access(prev, spu);	/* Step 6.  */
	suspend_mfc_and_halt_decr(prev, spu);	/* Step 7.  */
	wait_suspend_mfc_complete(prev, spu);	/* Step 8.  */
	if (!suspend_spe(prev, spu))	        /* Step 9.  */
		clear_spu_status(prev, spu);	/* Step 10. */
	do_mfc_mssync(prev, spu);	        /* Step 11. */
	issue_mfc_tlbie(prev, spu);	        /* Step 12. */
	handle_pending_interrupts(prev, spu);	/* Step 13. */
	purge_mfc_queue(prev, spu);	        /* Step 14. */
	wait_purge_complete(prev, spu);	        /* Step 15. */
	reset_spu_privcntl(prev, spu);	        /* Step 16. */
	reset_spu_lslr(prev, spu);              /* Step 17. */
	setup_mfc_sr1(prev, spu);	        /* Step 18. */
	spu_invalidate_slbs(spu);		/* Step 19. */
	reset_ch_part1(prev, spu);	        /* Step 20. */
	reset_ch_part2(prev, spu);	        /* Step 21. */
	enable_interrupts(prev, spu);	        /* Step 22. */
	set_switch_active(prev, spu);	        /* Step 23. */
	set_mfc_tclass_id(prev, spu);	        /* Step 24. */
	resume_mfc_queue(prev, spu);	        /* Step 25. */
}

static void restore_lscsa(struct spu_state *next, struct spu *spu)
{
	/*
	 * Perform steps 26-40 of SPU context restore sequence,
	 * which restores regions of the local store and register
	 * file.
	 */

	set_watchdog_timer(next, spu);	        /* Step 26. */
	setup_spu_status_part1(next, spu);	/* Step 27. */
	setup_spu_status_part2(next, spu);	/* Step 28. */
	restore_mfc_rag(next, spu);	        /* Step 29. */
	setup_mfc_slbs(next, spu);	        /* Step 30. */
	set_spu_npc(next, spu);	                /* Step 31. */
	set_signot1(next, spu);	                /* Step 32. */
	set_signot2(next, spu);	                /* Step 33. */
	setup_decr(next, spu);	                /* Step 34. */
	setup_ppu_mb(next, spu);	        /* Step 35. */
	setup_ppuint_mb(next, spu);	        /* Step 36. */
	send_restore_code(next, spu);	        /* Step 37. */
	set_ppu_querymask(next, spu);	        /* Step 38. */
	wait_tag_complete(next, spu);	        /* Step 39. */
	wait_spu_stopped(next, spu);	        /* Step 40. */
}

static void restore_csa(struct spu_state *next, struct spu *spu)
{
	/*
	 * Combine steps 41-76 of SPU context restore sequence, which
	 * restore regions of the privileged & problem state areas.
	 */

	restore_spu_privcntl(next, spu);	/* Step 41. */
	restore_status_part1(next, spu);	/* Step 42. */
	restore_status_part2(next, spu);	/* Step 43. */
	restore_ls_16kb(next, spu);	        /* Step 44. */
	wait_tag_complete(next, spu);	        /* Step 45. */
	suspend_mfc(next, spu);	                /* Step 46. */
	wait_suspend_mfc_complete(next, spu);	/* Step 47. */
	issue_mfc_tlbie(next, spu);	        /* Step 48. */
	clear_interrupts(next, spu);	        /* Step 49. */
	restore_mfc_queues(next, spu);	        /* Step 50. */
	restore_ppu_querymask(next, spu);	/* Step 51. */
	restore_ppu_querytype(next, spu);	/* Step 52. */
	restore_mfc_csr_tsq(next, spu);	        /* Step 53. */
	restore_mfc_csr_cmd(next, spu);	        /* Step 54. */
	restore_mfc_csr_ato(next, spu);	        /* Step 55. */
	restore_mfc_tclass_id(next, spu);	/* Step 56. */
	set_llr_event(next, spu);	        /* Step 57. */
	restore_decr_wrapped(next, spu);	/* Step 58. */
	restore_ch_part1(next, spu);	        /* Step 59. */
	restore_ch_part2(next, spu);	        /* Step 60. */
	restore_spu_lslr(next, spu);	        /* Step 61. */
	restore_spu_cfg(next, spu);	        /* Step 62. */
	restore_pm_trace(next, spu);	        /* Step 63. */
	restore_spu_npc(next, spu);	        /* Step 64. */
	restore_spu_mb(next, spu);	        /* Step 65. */
	check_ppu_mb_stat(next, spu);	        /* Step 66. */
	check_ppuint_mb_stat(next, spu);	/* Step 67. */
	spu_invalidate_slbs(spu);		/* Modified Step 68. */
	restore_mfc_sr1(next, spu);	        /* Step 69. */
	restore_other_spu_access(next, spu);	/* Step 70. */
	restore_spu_runcntl(next, spu);	        /* Step 71. */
	restore_mfc_cntl(next, spu);	        /* Step 72. */
	enable_user_access(next, spu);	        /* Step 73. */
	reset_switch_active(next, spu);	        /* Step 74. */
	reenable_interrupts(next, spu);	        /* Step 75. */
}

static int __do_spu_save(struct spu_state *prev, struct spu *spu)
{
	int rc;

	/*
	 * SPU context save can be broken into three phases:
	 *
	 *     (a) quiesce [steps 2-16].
	 *     (b) save of CSA, performed by PPE [steps 17-42]
	 *     (c) save of LSCSA, mostly performed by SPU [steps 43-52].
	 *
	 * Returns      0 on success.
	 *              2,6 if failed to quiece SPU
	 *              53 if SPU-side of save failed.
	 */

	rc = quiece_spu(prev, spu);	        /* Steps 2-16. */
	switch (rc) {
	default:
	case 2:
	case 6:
		harvest(prev, spu);
		return rc;
		break;
	case 0:
		break;
	}
	save_csa(prev, spu);	                /* Steps 17-43. */
	save_lscsa(prev, spu);	                /* Steps 44-53. */
	return check_save_status(prev, spu);	/* Step 54.     */
}

static int __do_spu_restore(struct spu_state *next, struct spu *spu)
{
	int rc;

	/*
	 * SPU context restore can be broken into three phases:
	 *
	 *    (a) harvest (or reset) SPU [steps 2-24].
	 *    (b) restore LSCSA [steps 25-40], mostly performed by SPU.
	 *    (c) restore CSA [steps 41-76], performed by PPE.
	 *
	 * The 'harvest' step is not performed here, but rather
	 * as needed below.
	 */

	restore_lscsa(next, spu);	        /* Steps 24-39. */
	rc = check_restore_status(next, spu);	/* Step 40.     */
	switch (rc) {
	default:
		/* Failed. Return now. */
		return rc;
		break;
	case 0:
		/* Fall through to next step. */
		break;
	}
	restore_csa(next, spu);

	return 0;
}

/**
 * spu_save - SPU context save, with locking.
 * @prev: pointer to SPU context save area, to be saved.
 * @spu: pointer to SPU iomem structure.
 *
 * Acquire locks, perform the save operation then return.
 */
int spu_save(struct spu_state *prev, struct spu *spu)
{
	int rc;

	acquire_spu_lock(spu);	        /* Step 1.     */
	prev->dar = spu->dar;
	prev->dsisr = spu->dsisr;
	spu->dar = 0;
	spu->dsisr = 0;
	rc = __do_spu_save(prev, spu);	/* Steps 2-53. */
	release_spu_lock(spu);
	if (rc != 0 && rc != 2 && rc != 6) {
		panic("%s failed on SPU[%d], rc=%d.\n",
		      __func__, spu->number, rc);
	}
	return 0;
}
EXPORT_SYMBOL_GPL(spu_save);

/**
 * spu_restore - SPU context restore, with harvest and locking.
 * @new: pointer to SPU context save area, to be restored.
 * @spu: pointer to SPU iomem structure.
 *
 * Perform harvest + restore, as we may not be coming
 * from a previous successful save operation, and the
 * hardware state is unknown.
 */
int spu_restore(struct spu_state *new, struct spu *spu)
{
	int rc;

	acquire_spu_lock(spu);
	harvest(NULL, spu);
	spu->slb_replace = 0;
	new->dar = 0;
	new->dsisr = 0;
	spu->class_0_pending = 0;
	rc = __do_spu_restore(new, spu);
	release_spu_lock(spu);
	if (rc) {
		panic("%s failed on SPU[%d] rc=%d.\n",
		       __func__, spu->number, rc);
	}
	return rc;
}
EXPORT_SYMBOL_GPL(spu_restore);

/**
 * spu_harvest - SPU harvest (reset) operation
 * @spu: pointer to SPU iomem structure.
 *
 * Perform SPU harvest (reset) operation.
 */
void spu_harvest(struct spu *spu)
{
	acquire_spu_lock(spu);
	harvest(NULL, spu);
	release_spu_lock(spu);
}

static void init_prob(struct spu_state *csa)
{
	csa->spu_chnlcnt_RW[9] = 1;
	csa->spu_chnlcnt_RW[21] = 16;
	csa->spu_chnlcnt_RW[23] = 1;
	csa->spu_chnlcnt_RW[28] = 1;
	csa->spu_chnlcnt_RW[30] = 1;
	csa->prob.spu_runcntl_RW = SPU_RUNCNTL_STOP;
	csa->prob.mb_stat_R = 0x000400;
}

static void init_priv1(struct spu_state *csa)
{
	/* Enable decode, relocate, tlbie response, master runcntl. */
	csa->priv1.mfc_sr1_RW = MFC_STATE1_LOCAL_STORAGE_DECODE_MASK |
	    MFC_STATE1_MASTER_RUN_CONTROL_MASK |
	    MFC_STATE1_PROBLEM_STATE_MASK |
	    MFC_STATE1_RELOCATE_MASK | MFC_STATE1_BUS_TLBIE_MASK;

	/* Enable OS-specific set of interrupts. */
	csa->priv1.int_mask_class0_RW = CLASS0_ENABLE_DMA_ALIGNMENT_INTR |
	    CLASS0_ENABLE_INVALID_DMA_COMMAND_INTR |
	    CLASS0_ENABLE_SPU_ERROR_INTR;
	csa->priv1.int_mask_class1_RW = CLASS1_ENABLE_SEGMENT_FAULT_INTR |
	    CLASS1_ENABLE_STORAGE_FAULT_INTR;
	csa->priv1.int_mask_class2_RW = CLASS2_ENABLE_SPU_STOP_INTR |
	    CLASS2_ENABLE_SPU_HALT_INTR |
	    CLASS2_ENABLE_SPU_DMA_TAG_GROUP_COMPLETE_INTR;
}

static void init_priv2(struct spu_state *csa)
{
	csa->priv2.spu_lslr_RW = LS_ADDR_MASK;
	csa->priv2.mfc_control_RW = MFC_CNTL_RESUME_DMA_QUEUE |
	    MFC_CNTL_NORMAL_DMA_QUEUE_OPERATION |
	    MFC_CNTL_DMA_QUEUES_EMPTY_MASK;
}

/**
 * spu_alloc_csa - allocate and initialize an SPU context save area.
 *
 * Allocate and initialize the contents of an SPU context save area.
 * This includes enabling address translation, interrupt masks, etc.,
 * as appropriate for the given OS environment.
 *
 * Note that storage for the 'lscsa' is allocated separately,
 * as it is by far the largest of the context save regions,
 * and may need to be pinned or otherwise specially aligned.
 */
int spu_init_csa(struct spu_state *csa)
{
	int rc;

	if (!csa)
		return -EINVAL;
	memset(csa, 0, sizeof(struct spu_state));

	rc = spu_alloc_lscsa(csa);
	if (rc)
		return rc;

	spin_lock_init(&csa->register_lock);

	init_prob(csa);
	init_priv1(csa);
	init_priv2(csa);

	return 0;
}
EXPORT_SYMBOL_GPL(spu_init_csa);

void spu_fini_csa(struct spu_state *csa)
{
	spu_free_lscsa(csa);
}
EXPORT_SYMBOL_GPL(spu_fini_csa);
