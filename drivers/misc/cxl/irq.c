// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2014 IBM Corp.
 */

#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/pid.h>
#include <asm/cputable.h>
#include <misc/cxl-base.h>

#include "cxl.h"
#include "trace.h"

static int afu_irq_range_start(void)
{
	if (cpu_has_feature(CPU_FTR_HVMODE))
		return 1;
	return 0;
}

static irqreturn_t schedule_cxl_fault(struct cxl_context *ctx, u64 dsisr, u64 dar)
{
	ctx->dsisr = dsisr;
	ctx->dar = dar;
	schedule_work(&ctx->fault_work);
	return IRQ_HANDLED;
}

irqreturn_t cxl_irq_psl9(int irq, struct cxl_context *ctx, struct cxl_irq_info *irq_info)
{
	u64 dsisr, dar;

	dsisr = irq_info->dsisr;
	dar = irq_info->dar;

	trace_cxl_psl9_irq(ctx, irq, dsisr, dar);

	pr_devel("CXL interrupt %i for afu pe: %i DSISR: %#llx DAR: %#llx\n", irq, ctx->pe, dsisr, dar);

	if (dsisr & CXL_PSL9_DSISR_An_TF) {
		pr_devel("CXL interrupt: Scheduling translation fault handling for later (pe: %i)\n", ctx->pe);
		return schedule_cxl_fault(ctx, dsisr, dar);
	}

	if (dsisr & CXL_PSL9_DSISR_An_PE)
		return cxl_ops->handle_psl_slice_error(ctx, dsisr,
						irq_info->errstat);
	if (dsisr & CXL_PSL9_DSISR_An_AE) {
		pr_devel("CXL interrupt: AFU Error 0x%016llx\n", irq_info->afu_err);

		if (ctx->pending_afu_err) {
			/*
			 * This shouldn't happen - the PSL treats these errors
			 * as fatal and will have reset the AFU, so there's not
			 * much point buffering multiple AFU errors.
			 * OTOH if we DO ever see a storm of these come in it's
			 * probably best that we log them somewhere:
			 */
			dev_err_ratelimited(&ctx->afu->dev, "CXL AFU Error undelivered to pe %i: 0x%016llx\n",
					    ctx->pe, irq_info->afu_err);
		} else {
			spin_lock(&ctx->lock);
			ctx->afu_err = irq_info->afu_err;
			ctx->pending_afu_err = 1;
			spin_unlock(&ctx->lock);

			wake_up_all(&ctx->wq);
		}

		cxl_ops->ack_irq(ctx, CXL_PSL_TFC_An_A, 0);
		return IRQ_HANDLED;
	}
	if (dsisr & CXL_PSL9_DSISR_An_OC)
		pr_devel("CXL interrupt: OS Context Warning\n");

	WARN(1, "Unhandled CXL PSL IRQ\n");
	return IRQ_HANDLED;
}

irqreturn_t cxl_irq_psl8(int irq, struct cxl_context *ctx, struct cxl_irq_info *irq_info)
{
	u64 dsisr, dar;

	dsisr = irq_info->dsisr;
	dar = irq_info->dar;

	trace_cxl_psl_irq(ctx, irq, dsisr, dar);

	pr_devel("CXL interrupt %i for afu pe: %i DSISR: %#llx DAR: %#llx\n", irq, ctx->pe, dsisr, dar);

	if (dsisr & CXL_PSL_DSISR_An_DS) {
		/*
		 * We don't inherently need to sleep to handle this, but we do
		 * need to get a ref to the task's mm, which we can't do from
		 * irq context without the potential for a deadlock since it
		 * takes the task_lock. An alternate option would be to keep a
		 * reference to the task's mm the entire time it has cxl open,
		 * but to do that we need to solve the issue where we hold a
		 * ref to the mm, but the mm can hold a ref to the fd after an
		 * mmap preventing anything from being cleaned up.
		 */
		pr_devel("Scheduling segment miss handling for later pe: %i\n", ctx->pe);
		return schedule_cxl_fault(ctx, dsisr, dar);
	}

	if (dsisr & CXL_PSL_DSISR_An_M)
		pr_devel("CXL interrupt: PTE not found\n");
	if (dsisr & CXL_PSL_DSISR_An_P)
		pr_devel("CXL interrupt: Storage protection violation\n");
	if (dsisr & CXL_PSL_DSISR_An_A)
		pr_devel("CXL interrupt: AFU lock access to write through or cache inhibited storage\n");
	if (dsisr & CXL_PSL_DSISR_An_S)
		pr_devel("CXL interrupt: Access was afu_wr or afu_zero\n");
	if (dsisr & CXL_PSL_DSISR_An_K)
		pr_devel("CXL interrupt: Access not permitted by virtual page class key protection\n");

	if (dsisr & CXL_PSL_DSISR_An_DM) {
		/*
		 * In some cases we might be able to handle the fault
		 * immediately if hash_page would succeed, but we still need
		 * the task's mm, which as above we can't get without a lock
		 */
		pr_devel("Scheduling page fault handling for later pe: %i\n", ctx->pe);
		return schedule_cxl_fault(ctx, dsisr, dar);
	}
	if (dsisr & CXL_PSL_DSISR_An_ST)
		WARN(1, "CXL interrupt: Segment Table PTE not found\n");
	if (dsisr & CXL_PSL_DSISR_An_UR)
		pr_devel("CXL interrupt: AURP PTE not found\n");
	if (dsisr & CXL_PSL_DSISR_An_PE)
		return cxl_ops->handle_psl_slice_error(ctx, dsisr,
						irq_info->errstat);
	if (dsisr & CXL_PSL_DSISR_An_AE) {
		pr_devel("CXL interrupt: AFU Error 0x%016llx\n", irq_info->afu_err);

		if (ctx->pending_afu_err) {
			/*
			 * This shouldn't happen - the PSL treats these errors
			 * as fatal and will have reset the AFU, so there's not
			 * much point buffering multiple AFU errors.
			 * OTOH if we DO ever see a storm of these come in it's
			 * probably best that we log them somewhere:
			 */
			dev_err_ratelimited(&ctx->afu->dev, "CXL AFU Error "
					    "undelivered to pe %i: 0x%016llx\n",
					    ctx->pe, irq_info->afu_err);
		} else {
			spin_lock(&ctx->lock);
			ctx->afu_err = irq_info->afu_err;
			ctx->pending_afu_err = true;
			spin_unlock(&ctx->lock);

			wake_up_all(&ctx->wq);
		}

		cxl_ops->ack_irq(ctx, CXL_PSL_TFC_An_A, 0);
		return IRQ_HANDLED;
	}
	if (dsisr & CXL_PSL_DSISR_An_OC)
		pr_devel("CXL interrupt: OS Context Warning\n");

	WARN(1, "Unhandled CXL PSL IRQ\n");
	return IRQ_HANDLED;
}

static irqreturn_t cxl_irq_afu(int irq, void *data)
{
	struct cxl_context *ctx = data;
	irq_hw_number_t hwirq = irqd_to_hwirq(irq_get_irq_data(irq));
	int irq_off, afu_irq = 0;
	__u16 range;
	int r;

	/*
	 * Look for the interrupt number.
	 * On bare-metal, we know range 0 only contains the PSL
	 * interrupt so we could start counting at range 1 and initialize
	 * afu_irq at 1.
	 * In a guest, range 0 also contains AFU interrupts, so it must
	 * be counted for. Therefore we initialize afu_irq at 0 to take into
	 * account the PSL interrupt.
	 *
	 * For code-readability, it just seems easier to go over all
	 * the ranges on bare-metal and guest. The end result is the same.
	 */
	for (r = 0; r < CXL_IRQ_RANGES; r++) {
		irq_off = hwirq - ctx->irqs.offset[r];
		range = ctx->irqs.range[r];
		if (irq_off >= 0 && irq_off < range) {
			afu_irq += irq_off;
			break;
		}
		afu_irq += range;
	}
	if (unlikely(r >= CXL_IRQ_RANGES)) {
		WARN(1, "Received AFU IRQ out of range for pe %i (virq %i hwirq %lx)\n",
		     ctx->pe, irq, hwirq);
		return IRQ_HANDLED;
	}

	trace_cxl_afu_irq(ctx, afu_irq, irq, hwirq);
	pr_devel("Received AFU interrupt %i for pe: %i (virq %i hwirq %lx)\n",
	       afu_irq, ctx->pe, irq, hwirq);

	if (unlikely(!ctx->irq_bitmap)) {
		WARN(1, "Received AFU IRQ for context with no IRQ bitmap\n");
		return IRQ_HANDLED;
	}
	spin_lock(&ctx->lock);
	set_bit(afu_irq - 1, ctx->irq_bitmap);
	ctx->pending_irq = true;
	spin_unlock(&ctx->lock);

	wake_up_all(&ctx->wq);

	return IRQ_HANDLED;
}

unsigned int cxl_map_irq(struct cxl *adapter, irq_hw_number_t hwirq,
			 irq_handler_t handler, void *cookie, const char *name)
{
	unsigned int virq;
	int result;

	/* IRQ Domain? */
	virq = irq_create_mapping(NULL, hwirq);
	if (!virq) {
		dev_warn(&adapter->dev, "cxl_map_irq: irq_create_mapping failed\n");
		return 0;
	}

	if (cxl_ops->setup_irq)
		cxl_ops->setup_irq(adapter, hwirq, virq);

	pr_devel("hwirq %#lx mapped to virq %u\n", hwirq, virq);

	result = request_irq(virq, handler, 0, name, cookie);
	if (result) {
		dev_warn(&adapter->dev, "cxl_map_irq: request_irq failed: %i\n", result);
		return 0;
	}

	return virq;
}

void cxl_unmap_irq(unsigned int virq, void *cookie)
{
	free_irq(virq, cookie);
}

int cxl_register_one_irq(struct cxl *adapter,
			irq_handler_t handler,
			void *cookie,
			irq_hw_number_t *dest_hwirq,
			unsigned int *dest_virq,
			const char *name)
{
	int hwirq, virq;

	if ((hwirq = cxl_ops->alloc_one_irq(adapter)) < 0)
		return hwirq;

	if (!(virq = cxl_map_irq(adapter, hwirq, handler, cookie, name)))
		goto err;

	*dest_hwirq = hwirq;
	*dest_virq = virq;

	return 0;

err:
	cxl_ops->release_one_irq(adapter, hwirq);
	return -ENOMEM;
}

void afu_irq_name_free(struct cxl_context *ctx)
{
	struct cxl_irq_name *irq_name, *tmp;

	list_for_each_entry_safe(irq_name, tmp, &ctx->irq_names, list) {
		kfree(irq_name->name);
		list_del(&irq_name->list);
		kfree(irq_name);
	}
}

int afu_allocate_irqs(struct cxl_context *ctx, u32 count)
{
	int rc, r, i, j = 1;
	struct cxl_irq_name *irq_name;
	int alloc_count;

	/*
	 * In native mode, range 0 is reserved for the multiplexed
	 * PSL interrupt. It has been allocated when the AFU was initialized.
	 *
	 * In a guest, the PSL interrupt is not mutliplexed, but per-context,
	 * and is the first interrupt from range 0. It still needs to be
	 * allocated, so bump the count by one.
	 */
	if (cpu_has_feature(CPU_FTR_HVMODE))
		alloc_count = count;
	else
		alloc_count = count + 1;

	if ((rc = cxl_ops->alloc_irq_ranges(&ctx->irqs, ctx->afu->adapter,
							alloc_count)))
		return rc;

	if (cpu_has_feature(CPU_FTR_HVMODE)) {
		/* Multiplexed PSL Interrupt */
		ctx->irqs.offset[0] = ctx->afu->native->psl_hwirq;
		ctx->irqs.range[0] = 1;
	}

	ctx->irq_count = count;
	ctx->irq_bitmap = kcalloc(BITS_TO_LONGS(count),
				  sizeof(*ctx->irq_bitmap), GFP_KERNEL);
	if (!ctx->irq_bitmap)
		goto out;

	/*
	 * Allocate names first.  If any fail, bail out before allocating
	 * actual hardware IRQs.
	 */
	for (r = afu_irq_range_start(); r < CXL_IRQ_RANGES; r++) {
		for (i = 0; i < ctx->irqs.range[r]; i++) {
			irq_name = kmalloc(sizeof(struct cxl_irq_name),
					   GFP_KERNEL);
			if (!irq_name)
				goto out;
			irq_name->name = kasprintf(GFP_KERNEL, "cxl-%s-pe%i-%i",
						   dev_name(&ctx->afu->dev),
						   ctx->pe, j);
			if (!irq_name->name) {
				kfree(irq_name);
				goto out;
			}
			/* Add to tail so next look get the correct order */
			list_add_tail(&irq_name->list, &ctx->irq_names);
			j++;
		}
	}
	return 0;

out:
	cxl_ops->release_irq_ranges(&ctx->irqs, ctx->afu->adapter);
	afu_irq_name_free(ctx);
	return -ENOMEM;
}

static void afu_register_hwirqs(struct cxl_context *ctx)
{
	irq_hw_number_t hwirq;
	struct cxl_irq_name *irq_name;
	int r, i;
	irqreturn_t (*handler)(int irq, void *data);

	/* We've allocated all memory now, so let's do the irq allocations */
	irq_name = list_first_entry(&ctx->irq_names, struct cxl_irq_name, list);
	for (r = afu_irq_range_start(); r < CXL_IRQ_RANGES; r++) {
		hwirq = ctx->irqs.offset[r];
		for (i = 0; i < ctx->irqs.range[r]; hwirq++, i++) {
			if (r == 0 && i == 0)
				/*
				 * The very first interrupt of range 0 is
				 * always the PSL interrupt, but we only
				 * need to connect a handler for guests,
				 * because there's one PSL interrupt per
				 * context.
				 * On bare-metal, the PSL interrupt is
				 * multiplexed and was setup when the AFU
				 * was configured.
				 */
				handler = cxl_ops->psl_interrupt;
			else
				handler = cxl_irq_afu;
			cxl_map_irq(ctx->afu->adapter, hwirq, handler, ctx,
				irq_name->name);
			irq_name = list_next_entry(irq_name, list);
		}
	}
}

int afu_register_irqs(struct cxl_context *ctx, u32 count)
{
	int rc;

	rc = afu_allocate_irqs(ctx, count);
	if (rc)
		return rc;

	afu_register_hwirqs(ctx);
	return 0;
}

void afu_release_irqs(struct cxl_context *ctx, void *cookie)
{
	irq_hw_number_t hwirq;
	unsigned int virq;
	int r, i;

	for (r = afu_irq_range_start(); r < CXL_IRQ_RANGES; r++) {
		hwirq = ctx->irqs.offset[r];
		for (i = 0; i < ctx->irqs.range[r]; hwirq++, i++) {
			virq = irq_find_mapping(NULL, hwirq);
			if (virq)
				cxl_unmap_irq(virq, cookie);
		}
	}

	afu_irq_name_free(ctx);
	cxl_ops->release_irq_ranges(&ctx->irqs, ctx->afu->adapter);

	ctx->irq_count = 0;
}

void cxl_afu_decode_psl_serr(struct cxl_afu *afu, u64 serr)
{
	dev_crit(&afu->dev,
		 "PSL Slice error received. Check AFU for root cause.\n");
	dev_crit(&afu->dev, "PSL_SERR_An: 0x%016llx\n", serr);
	if (serr & CXL_PSL_SERR_An_afuto)
		dev_crit(&afu->dev, "AFU MMIO Timeout\n");
	if (serr & CXL_PSL_SERR_An_afudis)
		dev_crit(&afu->dev,
			 "MMIO targeted Accelerator that was not enabled\n");
	if (serr & CXL_PSL_SERR_An_afuov)
		dev_crit(&afu->dev, "AFU CTAG Overflow\n");
	if (serr & CXL_PSL_SERR_An_badsrc)
		dev_crit(&afu->dev, "Bad Interrupt Source\n");
	if (serr & CXL_PSL_SERR_An_badctx)
		dev_crit(&afu->dev, "Bad Context Handle\n");
	if (serr & CXL_PSL_SERR_An_llcmdis)
		dev_crit(&afu->dev, "LLCMD to Disabled AFU\n");
	if (serr & CXL_PSL_SERR_An_llcmdto)
		dev_crit(&afu->dev, "LLCMD Timeout to AFU\n");
	if (serr & CXL_PSL_SERR_An_afupar)
		dev_crit(&afu->dev, "AFU MMIO Parity Error\n");
	if (serr & CXL_PSL_SERR_An_afudup)
		dev_crit(&afu->dev, "AFU MMIO Duplicate CTAG Error\n");
	if (serr & CXL_PSL_SERR_An_AE)
		dev_crit(&afu->dev,
			 "AFU asserted JDONE with JERROR in AFU Directed Mode\n");
}
