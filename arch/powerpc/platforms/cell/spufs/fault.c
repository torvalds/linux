/*
 * Low-level SPU handling
 *
 * (C) Copyright IBM Deutschland Entwicklung GmbH 2005
 *
 * Author: Arnd Bergmann <arndb@de.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/module.h>

#include <asm/spu.h>
#include <asm/spu_csa.h>

#include "spufs.h"

/*
 * This ought to be kept in sync with the powerpc specific do_page_fault
 * function. Currently, there are a few corner cases that we haven't had
 * to handle fortunately.
 */
static int spu_handle_mm_fault(struct mm_struct *mm, unsigned long ea,
		unsigned long dsisr, unsigned *flt)
{
	struct vm_area_struct *vma;
	unsigned long is_write;
	int ret;

#if 0
	if (!IS_VALID_EA(ea)) {
		return -EFAULT;
	}
#endif /* XXX */
	if (mm == NULL) {
		return -EFAULT;
	}
	if (mm->pgd == NULL) {
		return -EFAULT;
	}

	down_read(&mm->mmap_sem);
	vma = find_vma(mm, ea);
	if (!vma)
		goto bad_area;
	if (vma->vm_start <= ea)
		goto good_area;
	if (!(vma->vm_flags & VM_GROWSDOWN))
		goto bad_area;
	if (expand_stack(vma, ea))
		goto bad_area;
good_area:
	is_write = dsisr & MFC_DSISR_ACCESS_PUT;
	if (is_write) {
		if (!(vma->vm_flags & VM_WRITE))
			goto bad_area;
	} else {
		if (dsisr & MFC_DSISR_ACCESS_DENIED)
			goto bad_area;
		if (!(vma->vm_flags & (VM_READ | VM_EXEC)))
			goto bad_area;
	}
	ret = 0;
	*flt = handle_mm_fault(mm, vma, ea, is_write);
	if (unlikely(*flt & VM_FAULT_ERROR)) {
		if (*flt & VM_FAULT_OOM) {
			ret = -ENOMEM;
			goto bad_area;
		} else if (*flt & VM_FAULT_SIGBUS) {
			ret = -EFAULT;
			goto bad_area;
		}
		BUG();
	}
	if (*flt & VM_FAULT_MAJOR)
		current->maj_flt++;
	else
		current->min_flt++;
	up_read(&mm->mmap_sem);
	return ret;

bad_area:
	up_read(&mm->mmap_sem);
	return -EFAULT;
}

static void spufs_handle_dma_error(struct spu_context *ctx,
				unsigned long ea, int type)
{
	if (ctx->flags & SPU_CREATE_EVENTS_ENABLED) {
		ctx->event_return |= type;
		wake_up_all(&ctx->stop_wq);
	} else {
		siginfo_t info;
		memset(&info, 0, sizeof(info));

		switch (type) {
		case SPE_EVENT_INVALID_DMA:
			info.si_signo = SIGBUS;
			info.si_code = BUS_OBJERR;
			break;
		case SPE_EVENT_SPE_DATA_STORAGE:
			info.si_signo = SIGBUS;
			info.si_addr = (void __user *)ea;
			info.si_code = BUS_ADRERR;
			break;
		case SPE_EVENT_DMA_ALIGNMENT:
			info.si_signo = SIGBUS;
			/* DAR isn't set for an alignment fault :( */
			info.si_code = BUS_ADRALN;
			break;
		case SPE_EVENT_SPE_ERROR:
			info.si_signo = SIGILL;
			info.si_addr = (void __user *)(unsigned long)
				ctx->ops->npc_read(ctx) - 4;
			info.si_code = ILL_ILLOPC;
			break;
		}
		if (info.si_signo)
			force_sig_info(info.si_signo, &info, current);
	}
}

void spufs_dma_callback(struct spu *spu, int type)
{
	spufs_handle_dma_error(spu->ctx, spu->dar, type);
}
EXPORT_SYMBOL_GPL(spufs_dma_callback);

/*
 * bottom half handler for page faults, we can't do this from
 * interrupt context, since we might need to sleep.
 * we also need to give up the mutex so we can get scheduled
 * out while waiting for the backing store.
 *
 * TODO: try calling hash_page from the interrupt handler first
 *       in order to speed up the easy case.
 */
int spufs_handle_class1(struct spu_context *ctx)
{
	u64 ea, dsisr, access;
	unsigned long flags;
	unsigned flt = 0;
	int ret;

	/*
	 * dar and dsisr get passed from the registers
	 * to the spu_context, to this function, but not
	 * back to the spu if it gets scheduled again.
	 *
	 * if we don't handle the fault for a saved context
	 * in time, we can still expect to get the same fault
	 * the immediately after the context restore.
	 */
	if (ctx->state == SPU_STATE_RUNNABLE) {
		ea = ctx->spu->dar;
		dsisr = ctx->spu->dsisr;
		ctx->spu->dar= ctx->spu->dsisr = 0;
	} else {
		ea = ctx->csa.priv1.mfc_dar_RW;
		dsisr = ctx->csa.priv1.mfc_dsisr_RW;
		ctx->csa.priv1.mfc_dar_RW = 0;
		ctx->csa.priv1.mfc_dsisr_RW = 0;
	}

	if (!(dsisr & (MFC_DSISR_PTE_NOT_FOUND | MFC_DSISR_ACCESS_DENIED)))
		return 0;

	spuctx_switch_state(ctx, SPU_UTIL_IOWAIT);

	pr_debug("ctx %p: ea %016lx, dsisr %016lx state %d\n", ctx, ea,
		dsisr, ctx->state);

	ctx->stats.hash_flt++;
	if (ctx->state == SPU_STATE_RUNNABLE)
		ctx->spu->stats.hash_flt++;

	/* we must not hold the lock when entering spu_handle_mm_fault */
	spu_release(ctx);

	access = (_PAGE_PRESENT | _PAGE_USER);
	access |= (dsisr & MFC_DSISR_ACCESS_PUT) ? _PAGE_RW : 0UL;
	local_irq_save(flags);
	ret = hash_page(ea, access, 0x300);
	local_irq_restore(flags);

	/* hashing failed, so try the actual fault handler */
	if (ret)
		ret = spu_handle_mm_fault(current->mm, ea, dsisr, &flt);

	spu_acquire(ctx);
	/*
	 * If we handled the fault successfully and are in runnable
	 * state, restart the DMA.
	 * In case of unhandled error report the problem to user space.
	 */
	if (!ret) {
		if (flt & VM_FAULT_MAJOR)
			ctx->stats.maj_flt++;
		else
			ctx->stats.min_flt++;
		if (ctx->state == SPU_STATE_RUNNABLE) {
			if (flt & VM_FAULT_MAJOR)
				ctx->spu->stats.maj_flt++;
			else
				ctx->spu->stats.min_flt++;
		}

		if (ctx->spu)
			ctx->ops->restart_dma(ctx);
	} else
		spufs_handle_dma_error(ctx, ea, SPE_EVENT_SPE_DATA_STORAGE);

	spuctx_switch_state(ctx, SPU_UTIL_SYSTEM);
	return ret;
}
EXPORT_SYMBOL_GPL(spufs_handle_class1);
