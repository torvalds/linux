/*
 * Copyright 2014 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/workqueue.h>
#include <linux/sched.h>
#include <linux/pid.h>
#include <linux/mm.h>
#include <linux/moduleparam.h>

#undef MODULE_PARAM_PREFIX
#define MODULE_PARAM_PREFIX "cxl" "."
#include <asm/current.h>
#include <asm/copro.h>
#include <asm/mmu.h>

#include "cxl.h"

static struct cxl_sste* find_free_sste(struct cxl_sste *primary_group,
				       bool sec_hash,
				       struct cxl_sste *secondary_group,
				       unsigned int *lru)
{
	unsigned int i, entry;
	struct cxl_sste *sste, *group = primary_group;

	for (i = 0; i < 2; i++) {
		for (entry = 0; entry < 8; entry++) {
			sste = group + entry;
			if (!(be64_to_cpu(sste->esid_data) & SLB_ESID_V))
				return sste;
		}
		if (!sec_hash)
			break;
		group = secondary_group;
	}
	/* Nothing free, select an entry to cast out */
	if (sec_hash && (*lru & 0x8))
		sste = secondary_group + (*lru & 0x7);
	else
		sste = primary_group + (*lru & 0x7);
	*lru = (*lru + 1) & 0xf;

	return sste;
}

static void cxl_load_segment(struct cxl_context *ctx, struct copro_slb *slb)
{
	/* mask is the group index, we search primary and secondary here. */
	unsigned int mask = (ctx->sst_size >> 7)-1; /* SSTP0[SegTableSize] */
	bool sec_hash = 1;
	struct cxl_sste *sste;
	unsigned int hash;
	unsigned long flags;


	sec_hash = !!(cxl_p1n_read(ctx->afu, CXL_PSL_SR_An) & CXL_PSL_SR_An_SC);

	if (slb->vsid & SLB_VSID_B_1T)
		hash = (slb->esid >> SID_SHIFT_1T) & mask;
	else /* 256M */
		hash = (slb->esid >> SID_SHIFT) & mask;

	spin_lock_irqsave(&ctx->sste_lock, flags);
	sste = find_free_sste(ctx->sstp + (hash << 3), sec_hash,
			      ctx->sstp + ((~hash & mask) << 3), &ctx->sst_lru);

	pr_devel("CXL Populating SST[%li]: %#llx %#llx\n",
			sste - ctx->sstp, slb->vsid, slb->esid);

	sste->vsid_data = cpu_to_be64(slb->vsid);
	sste->esid_data = cpu_to_be64(slb->esid);
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

	cxl_ack_irq(ctx, CXL_PSL_TFC_An_AE, 0);

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

	if ((rc = cxl_fault_segment(ctx, mm, ea)))
		cxl_ack_ae(ctx);
	else {

		mb(); /* Order seg table write to TFC MMIO write */
		cxl_ack_irq(ctx, CXL_PSL_TFC_An_R, 0);
	}

	return IRQ_HANDLED;
}

static void cxl_handle_page_fault(struct cxl_context *ctx,
				  struct mm_struct *mm, u64 dsisr, u64 dar)
{
	unsigned flt = 0;
	int result;
	unsigned long access, flags;

	if ((result = copro_handle_mm_fault(mm, dar, dsisr, &flt))) {
		pr_devel("copro_handle_mm_fault failed: %#x\n", result);
		return cxl_ack_ae(ctx);
	}

	/*
	 * update_mmu_cache() will not have loaded the hash since current->trap
	 * is not a 0x400 or 0x300, so just call hash_page_mm() here.
	 */
	access = _PAGE_PRESENT;
	if (dsisr & CXL_PSL_DSISR_An_S)
		access |= _PAGE_RW;
	if ((!ctx->kernel) || ~(dar & (1ULL << 63)))
		access |= _PAGE_USER;
	local_irq_save(flags);
	hash_page_mm(mm, dar, access, 0x300);
	local_irq_restore(flags);

	pr_devel("Page fault successfully handled for pe: %i!\n", ctx->pe);
	cxl_ack_irq(ctx, CXL_PSL_TFC_An_R, 0);
}

void cxl_handle_fault(struct work_struct *fault_work)
{
	struct cxl_context *ctx =
		container_of(fault_work, struct cxl_context, fault_work);
	u64 dsisr = ctx->dsisr;
	u64 dar = ctx->dar;
	struct task_struct *task;
	struct mm_struct *mm;

	if (cxl_p2n_read(ctx->afu, CXL_PSL_DSISR_An) != dsisr ||
	    cxl_p2n_read(ctx->afu, CXL_PSL_DAR_An) != dar ||
	    cxl_p2n_read(ctx->afu, CXL_PSL_PEHandle_An) != ctx->pe) {
		/* Most likely explanation is harmless - a dedicated process
		 * has detached and these were cleared by the PSL purge, but
		 * warn about it just in case */
		dev_notice(&ctx->afu->dev, "cxl_handle_fault: Translation fault regs changed\n");
		return;
	}

	pr_devel("CXL BOTTOM HALF handling fault for afu pe: %i. "
		"DSISR: %#llx DAR: %#llx\n", ctx->pe, dsisr, dar);

	if (!(task = get_pid_task(ctx->pid, PIDTYPE_PID))) {
		pr_devel("cxl_handle_fault unable to get task %i\n",
			 pid_nr(ctx->pid));
		cxl_ack_ae(ctx);
		return;
	}
	if (!(mm = get_task_mm(task))) {
		pr_devel("cxl_handle_fault unable to get mm %i\n",
			 pid_nr(ctx->pid));
		cxl_ack_ae(ctx);
		goto out;
	}

	if (dsisr & CXL_PSL_DSISR_An_DS)
		cxl_handle_segment_miss(ctx, mm, dar);
	else if (dsisr & CXL_PSL_DSISR_An_DM)
		cxl_handle_page_fault(ctx, mm, dsisr, dar);
	else
		WARN(1, "cxl_handle_fault has nothing to handle\n");

	mmput(mm);
out:
	put_task_struct(task);
}

static void cxl_prefault_one(struct cxl_context *ctx, u64 ea)
{
	int rc;
	struct task_struct *task;
	struct mm_struct *mm;

	if (!(task = get_pid_task(ctx->pid, PIDTYPE_PID))) {
		pr_devel("cxl_prefault_one unable to get task %i\n",
			 pid_nr(ctx->pid));
		return;
	}
	if (!(mm = get_task_mm(task))) {
		pr_devel("cxl_prefault_one unable to get mm %i\n",
			 pid_nr(ctx->pid));
		put_task_struct(task);
		return;
	}

	rc = cxl_fault_segment(ctx, mm, ea);

	mmput(mm);
	put_task_struct(task);
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
	struct task_struct *task;
	struct mm_struct *mm;

	if (!(task = get_pid_task(ctx->pid, PIDTYPE_PID))) {
		pr_devel("cxl_prefault_vma unable to get task %i\n",
			 pid_nr(ctx->pid));
		return;
	}
	if (!(mm = get_task_mm(task))) {
		pr_devel("cxl_prefault_vm unable to get mm %i\n",
			 pid_nr(ctx->pid));
		goto out1;
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
out1:
	put_task_struct(task);
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
