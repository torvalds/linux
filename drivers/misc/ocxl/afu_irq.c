// SPDX-License-Identifier: GPL-2.0+
// Copyright 2017 IBM Corp.
#include <linux/interrupt.h>
#include <asm/pnv-ocxl.h>
#include <asm/xive.h>
#include "ocxl_internal.h"
#include "trace.h"

struct afu_irq {
	int id;
	int hw_irq;
	unsigned int virq;
	char *name;
	irqreturn_t (*handler)(void *private);
	void (*free_private)(void *private);
	void *private;
};

int ocxl_irq_offset_to_id(struct ocxl_context *ctx, u64 offset)
{
	return (offset - ctx->afu->irq_base_offset) >> PAGE_SHIFT;
}

u64 ocxl_irq_id_to_offset(struct ocxl_context *ctx, int irq_id)
{
	return ctx->afu->irq_base_offset + (irq_id << PAGE_SHIFT);
}

int ocxl_irq_set_handler(struct ocxl_context *ctx, int irq_id,
		irqreturn_t (*handler)(void *private),
		void (*free_private)(void *private),
		void *private)
{
	struct afu_irq *irq;
	int rc;

	mutex_lock(&ctx->irq_lock);
	irq = idr_find(&ctx->irq_idr, irq_id);
	if (!irq) {
		rc = -EINVAL;
		goto unlock;
	}

	irq->handler = handler;
	irq->private = private;
	irq->free_private = free_private;

	rc = 0;
	// Fall through to unlock

unlock:
	mutex_unlock(&ctx->irq_lock);
	return rc;
}
EXPORT_SYMBOL_GPL(ocxl_irq_set_handler);

static irqreturn_t afu_irq_handler(int virq, void *data)
{
	struct afu_irq *irq = (struct afu_irq *) data;

	trace_ocxl_afu_irq_receive(virq);

	if (irq->handler)
		return irq->handler(irq->private);

	return IRQ_HANDLED; // Just drop it on the ground
}

static int setup_afu_irq(struct ocxl_context *ctx, struct afu_irq *irq)
{
	int rc;

	irq->virq = irq_create_mapping(NULL, irq->hw_irq);
	if (!irq->virq) {
		pr_err("irq_create_mapping failed\n");
		return -ENOMEM;
	}
	pr_debug("hw_irq %d mapped to virq %u\n", irq->hw_irq, irq->virq);

	irq->name = kasprintf(GFP_KERNEL, "ocxl-afu-%u", irq->virq);
	if (!irq->name) {
		irq_dispose_mapping(irq->virq);
		return -ENOMEM;
	}

	rc = request_irq(irq->virq, afu_irq_handler, 0, irq->name, irq);
	if (rc) {
		kfree(irq->name);
		irq->name = NULL;
		irq_dispose_mapping(irq->virq);
		pr_err("request_irq failed: %d\n", rc);
		return rc;
	}
	return 0;
}

static void release_afu_irq(struct afu_irq *irq)
{
	free_irq(irq->virq, irq);
	irq_dispose_mapping(irq->virq);
	kfree(irq->name);
}

int ocxl_afu_irq_alloc(struct ocxl_context *ctx, int *irq_id)
{
	struct afu_irq *irq;
	int rc;

	irq = kzalloc(sizeof(struct afu_irq), GFP_KERNEL);
	if (!irq)
		return -ENOMEM;

	/*
	 * We limit the number of afu irqs per context and per link to
	 * avoid a single process or user depleting the pool of IPIs
	 */

	mutex_lock(&ctx->irq_lock);

	irq->id = idr_alloc(&ctx->irq_idr, irq, 0, MAX_IRQ_PER_CONTEXT,
			GFP_KERNEL);
	if (irq->id < 0) {
		rc = -ENOSPC;
		goto err_unlock;
	}

	rc = ocxl_link_irq_alloc(ctx->afu->fn->link, &irq->hw_irq);
	if (rc)
		goto err_idr;

	rc = setup_afu_irq(ctx, irq);
	if (rc)
		goto err_alloc;

	trace_ocxl_afu_irq_alloc(ctx->pasid, irq->id, irq->virq, irq->hw_irq);
	mutex_unlock(&ctx->irq_lock);

	*irq_id = irq->id;

	return 0;

err_alloc:
	ocxl_link_free_irq(ctx->afu->fn->link, irq->hw_irq);
err_idr:
	idr_remove(&ctx->irq_idr, irq->id);
err_unlock:
	mutex_unlock(&ctx->irq_lock);
	kfree(irq);
	return rc;
}
EXPORT_SYMBOL_GPL(ocxl_afu_irq_alloc);

static void afu_irq_free(struct afu_irq *irq, struct ocxl_context *ctx)
{
	trace_ocxl_afu_irq_free(ctx->pasid, irq->id);
	if (ctx->mapping)
		unmap_mapping_range(ctx->mapping,
				ocxl_irq_id_to_offset(ctx, irq->id),
				1 << PAGE_SHIFT, 1);
	release_afu_irq(irq);
	if (irq->free_private)
		irq->free_private(irq->private);
	ocxl_link_free_irq(ctx->afu->fn->link, irq->hw_irq);
	kfree(irq);
}

int ocxl_afu_irq_free(struct ocxl_context *ctx, int irq_id)
{
	struct afu_irq *irq;

	mutex_lock(&ctx->irq_lock);

	irq = idr_find(&ctx->irq_idr, irq_id);
	if (!irq) {
		mutex_unlock(&ctx->irq_lock);
		return -EINVAL;
	}
	idr_remove(&ctx->irq_idr, irq->id);
	afu_irq_free(irq, ctx);
	mutex_unlock(&ctx->irq_lock);
	return 0;
}
EXPORT_SYMBOL_GPL(ocxl_afu_irq_free);

void ocxl_afu_irq_free_all(struct ocxl_context *ctx)
{
	struct afu_irq *irq;
	int id;

	mutex_lock(&ctx->irq_lock);
	idr_for_each_entry(&ctx->irq_idr, irq, id)
		afu_irq_free(irq, ctx);
	mutex_unlock(&ctx->irq_lock);
}

u64 ocxl_afu_irq_get_addr(struct ocxl_context *ctx, int irq_id)
{
	struct xive_irq_data *xd;
	struct afu_irq *irq;
	u64 addr = 0;

	mutex_lock(&ctx->irq_lock);
	irq = idr_find(&ctx->irq_idr, irq_id);
	if (irq) {
		xd = irq_get_handler_data(irq->virq);
		addr = xd ? xd->trig_page : 0;
	}
	mutex_unlock(&ctx->irq_lock);
	return addr;
}
EXPORT_SYMBOL_GPL(ocxl_afu_irq_get_addr);
