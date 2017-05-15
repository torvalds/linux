/*
 * Copyright 2014 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/workqueue.h>
#include <linux/sched/signal.h>
#include <linux/sched/mm.h>
#include <linux/pid.h>
#include <linux/mm.h>
#include <linux/moduleparam.h>

#undef MODULE_PARAM_PREFIX
#define MODULE_PARAM_PREFIX "cxl" "."
#include <asm/current.h>
#include <asm/copro.h>
#include <asm/mmu.h>

#include "cxl.h"
#include "trace.h"

static bool sste_matches(struct cxl_sste *sste, struct copro_slb *slb)
{
	return ((sste->vsid_data == cpu_to_be64(slb->vsid)) &&
		(sste->esid_data == cpu_to_be64(slb->esid)));
}

/*
 * This finds a free SSTE for the given SLB, or returns NULL if it's already in
 * the segment table.
 */
static struct cxl_sste* find_free_sste(struct cxl_context *ctx,
				       struct copro_slb *slb)
{
	struct cxl_sste *primary, *sste, *ret = NULL;
	unsigned int mask = (ctx->sst_size >> 7) - 1; /* SSTP0[SegTableSize] */
	unsigned int entry;
	unsigned int hash;

	if (slb->vsid & SLB_VSID_B_1T)
		hash = (slb->esid >> SID_SHIFT_1T) & mask;
	else /* 256M */
		hash = (slb->esid >> SID_SHIFT) & mask;

	primary = ctx->sstp + (hash << 3);

	for (entry = 0, sste = primary; entry < 8; entry++, sste++) {
		if (!ret && !(be64_to_cpu(sste->esid_data) & SLB_ESID_V))
			ret = sste;
		if (sste_matches(sste, slb))
			return NULL;
	}
	if (ret)
		return ret;

	/* Nothing free, select an entry to cast out */
	ret = primary + ctx->sst_lru;
	ctx->sst_lru = (ctx->sst_lru + 1) & 0x7;

	return ret;
}

static void cxl_load_segment(struct cxl_context *ctx, struct copro_slb *slb)
{
	/* mask is the group index, we search primary and secondary here. */
	struct cxl_sste *sste;
	unsigned long flags;

	spin_lock_irqsave(&ctx->sste_lock, flags);
	sste = find_free_sste(ctx, slb);
	if (!sste)
		goto out_unlock;

	pr_devel("CXL Populating SST[%li]: %#llx %#llx\n",
			sste - ctx->sstp, slb->vsid, slb->esid);
	trace_cxl_ste_write(ctx, sste - ctx->sstp, slb->esid, slb->vsid);

	sste->vsid_data = cpu_to_be64(slb->vsid);
	sste->esid_data = cpu_to_be64(slb->esid);
out_unlock:
	spin_unlock_irqrestore(&ctx->sste_lock, flags);
}

static int cxl_fault_segment(struct cxl_context *ctx, struct mm_struct *mm,
			     u64 ea)
{
	struct copro_slb slb = {0,0};
	int rc;

	if (!(rc = copro_calculate_slb(mm, ea, &slb))) {
		cxl_load_segment(ctx, &slb);
	}

	return rc;
}

static void cxl_ack_ae(struct cxl_context *ctx)
{
	unsigned long flags;

	cxl_ops->ack_irq(ctx, CXL_PSL_TFC_An_AE, 0);

	spin_lock_irqsave(&ctx->lock, flags);
	ctx->pending_fault = true;
	ctx->fault_addr = ctx->dar;
	ctx->fault_dsisr = ctx->dsisr;
	spin_unlock_irqrestore(&ctx->lock, flags);

	wake_up_all(&ctx->wq);
}

static int cxl_handle_segment_miss(struct cxl_context *ctx,
				   struct mm_struct *mm, u64 ea)
{
	int rc;

	pr_devel("CXL interrupt: Segment fault pe: %i ea: %#llx\n", ctx->pe, ea);
	trace_cxl_ste_miss(ctx, ea);

	if ((rc = cxl_fault_segment(ctx, mm, ea)))
		cxl_ack_ae(ctx);
	else {

		mb(); /* Order seg table write to TFC MMIO write */
		cxl_ops->ack_irq(ctx, CXL_PSL_TFC_An_R, 0);
	}

	return IRQ_HANDLED;
}

static void cxl_handle_page_fault(struct cxl_context *ctx,
				  struct mm_struct *mm, u64 dsisr, u64 dar)
{
	unsigned flt = 0;
	int result;
	unsigned long access, flags, inv_flags = 0;

	trace_cxl_pte_miss(ctx, dsisr, dar);

	if ((result = copro_handle_mm_fault(mm, dar, dsisr, &flt))) {
		pr_devel("copro_handle_mm_fault failed: %#x\n", result);
		return cxl_ack_ae(ctx);
	}

	if (!radix_enabled()) {
		/*
		 * update_mmu_cache() will not have loaded the hash since current->trap
		 * is not a 0x400 or 0x300, so just call hash_page_mm() here.
		 */
		access = _PAGE_PRESENT | _PAGE_READ;
		if (dsisr & CXL_PSL_DSISR_An_S)
			access |= _PAGE_WRITE;

		access |= _PAGE_PRIVILEGED;
		if ((!ctx->kernel) || (REGION_ID(dar) == USER_REGION_ID))
			access &= ~_PAGE_PRIVILEGED;

		if (dsisr & DSISR_NOHPTE)
			inv_flags |= HPTE_NOHPTE_UPDATE;

		local_irq_save(flags);
		hash_page_mm(mm, dar, access, 0x300, inv_flags);
		local_irq_restore(flags);
	}
	pr_devel("Page fault successfully handled for pe: %i!\n", ctx->pe);
	cxl_ops->ack_irq(ctx, CXL_PSL_TFC_An_R, 0);
}

/*
 * Returns the mm_struct corresponding to the context ctx.
 * mm_users == 0, the context may be in the process of being closed.
 */
static struct mm_struct *get_mem_context(struct cxl_context *ctx)
{
	if (ctx->mm == NULL)
		return NULL;

	if (!atomic_inc_not_zero(&ctx->mm->mm_users))
		return NULL;

	return ctx->mm;
}

static bool cxl_is_segment_miss(struct cxl_context *ctx, u64 dsisr)
{
	if ((cxl_is_psl8(ctx->afu)) && (dsisr & CXL_PSL_DSISR_An_DS))
		return true;

	return false;
}

static bool cxl_is_page_fault(struct cxl_context *ctx, u64 dsisr)
{
	if ((cxl_is_psl8(ctx->afu)) && (dsisr & CXL_PSL_DSISR_An_DM))
		return true;

	if ((cxl_is_psl9(ctx->afu)) &&
	   ((dsisr & CXL_PSL9_DSISR_An_CO_MASK) &
		(CXL_PSL9_DSISR_An_PF_SLR | CXL_PSL9_DSISR_An_PF_RGC |
		 CXL_PSL9_DSISR_An_PF_RGP | CXL_PSL9_DSISR_An_PF_HRH |
		 CXL_PSL9_DSISR_An_PF_STEG)))
		return true;

	return false;
}

void cxl_handle_fault(struct work_struct *fault_work)
{
	struct cxl_context *ctx =
		container_of(fault_work, struct cxl_context, fault_work);
	u64 dsisr = ctx->dsisr;
	u64 dar = ctx->dar;
	struct mm_struct *mm = NULL;

	if (cpu_has_feature(CPU_FTR_HVMODE)) {
		if (cxl_p2n_read(ctx->afu, CXL_PSL_DSISR_An) != dsisr ||
		    cxl_p2n_read(ctx->afu, CXL_PSL_DAR_An) != dar ||
		    cxl_p2n_read(ctx->afu, CXL_PSL_PEHandle_An) != ctx->pe) {
			/* Most likely explanation is harmless - a dedicated
			 * process has detached and these were cleared by the
			 * PSL purge, but warn about it just in case
			 */
			dev_notice(&ctx->afu->dev, "cxl_handle_fault: Translation fault regs changed\n");
			return;
		}
	}

	/* Early return if the context is being / has been detached */
	if (ctx->status == CLOSED) {
		cxl_ack_ae(ctx);
		return;
	}

	pr_devel("CXL BOTTOM HALF handling fault for afu pe: %i. "
		"DSISR: %#llx DAR: %#llx\n", ctx->pe, dsisr, dar);

	if (!ctx->kernel) {

		mm = get_mem_context(ctx);
		if (mm == NULL) {
			pr_devel("%s: unable to get mm for pe=%d pid=%i\n",
				 __func__, ctx->pe, pid_nr(ctx->pid));
			cxl_ack_ae(ctx);
			return;
		} else {
			pr_devel("Handling page fault for pe=%d pid=%i\n",
				 ctx->pe, pid_nr(ctx->pid));
		}
	}

	if (cxl_is_segment_miss(ctx, dsisr))
		cxl_handle_segment_miss(ctx, mm, dar);
	else if (cxl_is_page_fault(ctx, dsisr))
		cxl_handle_page_fault(ctx, mm, dsisr, dar);
	else
		WARN(1, "cxl_handle_fault has nothing to handle\n");

	if (mm)
		mmput(mm);
}

static void cxl_prefault_one(struct cxl_context *ctx, u64 ea)
{
	struct mm_struct *mm;

	mm = get_mem_context(ctx);
	if (mm == NULL) {
		pr_devel("cxl_prefault_one unable to get mm %i\n",
			 pid_nr(ctx->pid));
		return;
	}

	cxl_fault_segment(ctx, mm, ea);

	mmput(mm);
}

static u64 next_segment(u64 ea, u64 vsid)
{
	if (vsid & SLB_VSID_B_1T)
		ea |= (1ULL << 40) - 1;
	else
		ea |= (1ULL << 28) - 1;

	return ea + 1;
}

static void cxl_prefault_vma(struct cxl_context *ctx)
{
	u64 ea, last_esid = 0;
	struct copro_slb slb;
	struct vm_area_struct *vma;
	int rc;
	struct mm_struct *mm;

	mm = get_mem_context(ctx);
	if (mm == NULL) {
		pr_devel("cxl_prefault_vm unable to get mm %i\n",
			 pid_nr(ctx->pid));
		return;
	}

	down_read(&mm->mmap_sem);
	for (vma = mm->mmap; vma; vma = vma->vm_next) {
		for (ea = vma->vm_start; ea < vma->vm_end;
				ea = next_segment(ea, slb.vsid)) {
			rc = copro_calculate_slb(mm, ea, &slb);
			if (rc)
				continue;

			if (last_esid == slb.esid)
				continue;

			cxl_load_segment(ctx, &slb);
			last_esid = slb.esid;
		}
	}
	up_read(&mm->mmap_sem);

	mmput(mm);
}

void cxl_prefault(struct cxl_context *ctx, u64 wed)
{
	switch (ctx->afu->prefault_mode) {
	case CXL_PREFAULT_WED:
		cxl_prefault_one(ctx, wed);
		break;
	case CXL_PREFAULT_ALL:
		cxl_prefault_vma(ctx);
		break;
	default:
		break;
	}
}
