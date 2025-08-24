// SPDX-License-Identifier: GPL-2.0
/* Marvell RVU Admin Function Devlink
 *
 * Copyright (C) 2020 Marvell.
 *
 */

#include <linux/bitfield.h>

#include "rvu.h"
#include "rvu_reg.h"
#include "rvu_struct.h"
#include "rvu_npc_hash.h"

#define DRV_NAME "octeontx2-af"

static void rvu_report_pair_start(struct devlink_fmsg *fmsg, const char *name)
{
	devlink_fmsg_pair_nest_start(fmsg, name);
	devlink_fmsg_obj_nest_start(fmsg);
}

static void rvu_report_pair_end(struct devlink_fmsg *fmsg)
{
	devlink_fmsg_obj_nest_end(fmsg);
	devlink_fmsg_pair_nest_end(fmsg);
}

static bool rvu_common_request_irq(struct rvu *rvu, int offset,
				   const char *name, irq_handler_t fn)
{
	struct rvu_devlink *rvu_dl = rvu->rvu_dl;
	int rc;

	sprintf(&rvu->irq_name[offset * NAME_SIZE], "%s", name);
	rc = request_irq(pci_irq_vector(rvu->pdev, offset), fn, 0,
			 &rvu->irq_name[offset * NAME_SIZE], rvu_dl);
	if (rc)
		dev_warn(rvu->dev, "Failed to register %s irq\n", name);
	else
		rvu->irq_allocated[offset] = true;

	return rvu->irq_allocated[offset];
}

static void rvu_nix_intr_work(struct work_struct *work)
{
	struct rvu_nix_health_reporters *rvu_nix_health_reporter;

	rvu_nix_health_reporter = container_of(work, struct rvu_nix_health_reporters, intr_work);
	devlink_health_report(rvu_nix_health_reporter->rvu_hw_nix_intr_reporter,
			      "NIX_AF_RVU Error",
			      rvu_nix_health_reporter->nix_event_ctx);
}

static irqreturn_t rvu_nix_af_rvu_intr_handler(int irq, void *rvu_irq)
{
	struct rvu_nix_event_ctx *nix_event_context;
	struct rvu_devlink *rvu_dl = rvu_irq;
	struct rvu *rvu;
	int blkaddr;
	u64 intr;

	rvu = rvu_dl->rvu;
	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NIX, 0);
	if (blkaddr < 0)
		return IRQ_NONE;

	nix_event_context = rvu_dl->rvu_nix_health_reporter->nix_event_ctx;
	intr = rvu_read64(rvu, blkaddr, NIX_AF_RVU_INT);
	nix_event_context->nix_af_rvu_int = intr;

	/* Clear interrupts */
	rvu_write64(rvu, blkaddr, NIX_AF_RVU_INT, intr);
	rvu_write64(rvu, blkaddr, NIX_AF_RVU_INT_ENA_W1C, ~0ULL);
	queue_work(rvu_dl->devlink_wq, &rvu_dl->rvu_nix_health_reporter->intr_work);

	return IRQ_HANDLED;
}

static void rvu_nix_gen_work(struct work_struct *work)
{
	struct rvu_nix_health_reporters *rvu_nix_health_reporter;

	rvu_nix_health_reporter = container_of(work, struct rvu_nix_health_reporters, gen_work);
	devlink_health_report(rvu_nix_health_reporter->rvu_hw_nix_gen_reporter,
			      "NIX_AF_GEN Error",
			      rvu_nix_health_reporter->nix_event_ctx);
}

static irqreturn_t rvu_nix_af_rvu_gen_handler(int irq, void *rvu_irq)
{
	struct rvu_nix_event_ctx *nix_event_context;
	struct rvu_devlink *rvu_dl = rvu_irq;
	struct rvu *rvu;
	int blkaddr;
	u64 intr;

	rvu = rvu_dl->rvu;
	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NIX, 0);
	if (blkaddr < 0)
		return IRQ_NONE;

	nix_event_context = rvu_dl->rvu_nix_health_reporter->nix_event_ctx;
	intr = rvu_read64(rvu, blkaddr, NIX_AF_GEN_INT);
	nix_event_context->nix_af_rvu_gen = intr;

	/* Clear interrupts */
	rvu_write64(rvu, blkaddr, NIX_AF_GEN_INT, intr);
	rvu_write64(rvu, blkaddr, NIX_AF_GEN_INT_ENA_W1C, ~0ULL);
	queue_work(rvu_dl->devlink_wq, &rvu_dl->rvu_nix_health_reporter->gen_work);

	return IRQ_HANDLED;
}

static void rvu_nix_err_work(struct work_struct *work)
{
	struct rvu_nix_health_reporters *rvu_nix_health_reporter;

	rvu_nix_health_reporter = container_of(work, struct rvu_nix_health_reporters, err_work);
	devlink_health_report(rvu_nix_health_reporter->rvu_hw_nix_err_reporter,
			      "NIX_AF_ERR Error",
			      rvu_nix_health_reporter->nix_event_ctx);
}

static irqreturn_t rvu_nix_af_rvu_err_handler(int irq, void *rvu_irq)
{
	struct rvu_nix_event_ctx *nix_event_context;
	struct rvu_devlink *rvu_dl = rvu_irq;
	struct rvu *rvu;
	int blkaddr;
	u64 intr;

	rvu = rvu_dl->rvu;
	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NIX, 0);
	if (blkaddr < 0)
		return IRQ_NONE;

	nix_event_context = rvu_dl->rvu_nix_health_reporter->nix_event_ctx;
	intr = rvu_read64(rvu, blkaddr, NIX_AF_ERR_INT);
	nix_event_context->nix_af_rvu_err = intr;

	/* Clear interrupts */
	rvu_write64(rvu, blkaddr, NIX_AF_ERR_INT, intr);
	rvu_write64(rvu, blkaddr, NIX_AF_ERR_INT_ENA_W1C, ~0ULL);
	queue_work(rvu_dl->devlink_wq, &rvu_dl->rvu_nix_health_reporter->err_work);

	return IRQ_HANDLED;
}

static void rvu_nix_ras_work(struct work_struct *work)
{
	struct rvu_nix_health_reporters *rvu_nix_health_reporter;

	rvu_nix_health_reporter = container_of(work, struct rvu_nix_health_reporters, ras_work);
	devlink_health_report(rvu_nix_health_reporter->rvu_hw_nix_ras_reporter,
			      "NIX_AF_RAS Error",
			      rvu_nix_health_reporter->nix_event_ctx);
}

static irqreturn_t rvu_nix_af_rvu_ras_handler(int irq, void *rvu_irq)
{
	struct rvu_nix_event_ctx *nix_event_context;
	struct rvu_devlink *rvu_dl = rvu_irq;
	struct rvu *rvu;
	int blkaddr;
	u64 intr;

	rvu = rvu_dl->rvu;
	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NIX, 0);
	if (blkaddr < 0)
		return IRQ_NONE;

	nix_event_context = rvu_dl->rvu_nix_health_reporter->nix_event_ctx;
	intr = rvu_read64(rvu, blkaddr, NIX_AF_ERR_INT);
	nix_event_context->nix_af_rvu_ras = intr;

	/* Clear interrupts */
	rvu_write64(rvu, blkaddr, NIX_AF_RAS, intr);
	rvu_write64(rvu, blkaddr, NIX_AF_RAS_ENA_W1C, ~0ULL);
	queue_work(rvu_dl->devlink_wq, &rvu_dl->rvu_nix_health_reporter->ras_work);

	return IRQ_HANDLED;
}

static void rvu_nix_unregister_interrupts(struct rvu *rvu)
{
	struct rvu_devlink *rvu_dl = rvu->rvu_dl;
	int offs, i, blkaddr;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NIX, 0);
	if (blkaddr < 0)
		return;

	offs = rvu_read64(rvu, blkaddr, NIX_PRIV_AF_INT_CFG) & 0x3ff;
	if (!offs)
		return;

	rvu_write64(rvu, blkaddr, NIX_AF_RVU_INT_ENA_W1C, ~0ULL);
	rvu_write64(rvu, blkaddr, NIX_AF_GEN_INT_ENA_W1C, ~0ULL);
	rvu_write64(rvu, blkaddr, NIX_AF_ERR_INT_ENA_W1C, ~0ULL);
	rvu_write64(rvu, blkaddr, NIX_AF_RAS_ENA_W1C, ~0ULL);

	if (rvu->irq_allocated[offs + NIX_AF_INT_VEC_RVU]) {
		free_irq(pci_irq_vector(rvu->pdev, offs + NIX_AF_INT_VEC_RVU),
			 rvu_dl);
		rvu->irq_allocated[offs + NIX_AF_INT_VEC_RVU] = false;
	}

	for (i = NIX_AF_INT_VEC_GEN; i < NIX_AF_INT_VEC_CNT; i++)
		if (rvu->irq_allocated[offs + i]) {
			free_irq(pci_irq_vector(rvu->pdev, offs + i), rvu_dl);
			rvu->irq_allocated[offs + i] = false;
		}
}

static int rvu_nix_register_interrupts(struct rvu *rvu)
{
	int blkaddr, base;
	bool rc;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NIX, 0);
	if (blkaddr < 0)
		return blkaddr;

	/* Get NIX AF MSIX vectors offset. */
	base = rvu_read64(rvu, blkaddr, NIX_PRIV_AF_INT_CFG) & 0x3ff;
	if (!base) {
		dev_warn(rvu->dev,
			 "Failed to get NIX%d NIX_AF_INT vector offsets\n",
			 blkaddr - BLKADDR_NIX0);
		return 0;
	}
	/* Register and enable NIX_AF_RVU_INT interrupt */
	rc = rvu_common_request_irq(rvu, base +  NIX_AF_INT_VEC_RVU,
				    "NIX_AF_RVU_INT",
				    rvu_nix_af_rvu_intr_handler);
	if (!rc)
		goto err;
	rvu_write64(rvu, blkaddr, NIX_AF_RVU_INT_ENA_W1S, ~0ULL);

	/* Register and enable NIX_AF_GEN_INT interrupt */
	rc = rvu_common_request_irq(rvu, base +  NIX_AF_INT_VEC_GEN,
				    "NIX_AF_GEN_INT",
				    rvu_nix_af_rvu_gen_handler);
	if (!rc)
		goto err;
	rvu_write64(rvu, blkaddr, NIX_AF_GEN_INT_ENA_W1S, ~0ULL);

	/* Register and enable NIX_AF_ERR_INT interrupt */
	rc = rvu_common_request_irq(rvu, base + NIX_AF_INT_VEC_AF_ERR,
				    "NIX_AF_ERR_INT",
				    rvu_nix_af_rvu_err_handler);
	if (!rc)
		goto err;
	rvu_write64(rvu, blkaddr, NIX_AF_ERR_INT_ENA_W1S, ~0ULL);

	/* Register and enable NIX_AF_RAS interrupt */
	rc = rvu_common_request_irq(rvu, base + NIX_AF_INT_VEC_POISON,
				    "NIX_AF_RAS",
				    rvu_nix_af_rvu_ras_handler);
	if (!rc)
		goto err;
	rvu_write64(rvu, blkaddr, NIX_AF_RAS_ENA_W1S, ~0ULL);

	return 0;
err:
	rvu_nix_unregister_interrupts(rvu);
	return rc;
}

static int rvu_nix_report_show(struct devlink_fmsg *fmsg, void *ctx,
			       enum nix_af_rvu_health health_reporter)
{
	struct rvu_nix_event_ctx *nix_event_context;
	u64 intr_val;

	nix_event_context = ctx;
	switch (health_reporter) {
	case NIX_AF_RVU_INTR:
		intr_val = nix_event_context->nix_af_rvu_int;
		rvu_report_pair_start(fmsg, "NIX_AF_RVU");
		devlink_fmsg_u64_pair_put(fmsg, "\tNIX RVU Interrupt Reg ",
					  nix_event_context->nix_af_rvu_int);
		if (intr_val & BIT_ULL(0))
			devlink_fmsg_string_put(fmsg, "\n\tUnmap Slot Error");
		rvu_report_pair_end(fmsg);
		break;
	case NIX_AF_RVU_GEN:
		intr_val = nix_event_context->nix_af_rvu_gen;
		rvu_report_pair_start(fmsg, "NIX_AF_GENERAL");
		devlink_fmsg_u64_pair_put(fmsg, "\tNIX General Interrupt Reg ",
					  nix_event_context->nix_af_rvu_gen);
		if (intr_val & BIT_ULL(0))
			devlink_fmsg_string_put(fmsg, "\n\tRx multicast pkt drop");
		if (intr_val & BIT_ULL(1))
			devlink_fmsg_string_put(fmsg, "\n\tRx mirror pkt drop");
		if (intr_val & BIT_ULL(4))
			devlink_fmsg_string_put(fmsg, "\n\tSMQ flush done");
		rvu_report_pair_end(fmsg);
		break;
	case NIX_AF_RVU_ERR:
		intr_val = nix_event_context->nix_af_rvu_err;
		rvu_report_pair_start(fmsg, "NIX_AF_ERR");
		devlink_fmsg_u64_pair_put(fmsg, "\tNIX Error Interrupt Reg ",
					  nix_event_context->nix_af_rvu_err);
		if (intr_val & BIT_ULL(14))
			devlink_fmsg_string_put(fmsg, "\n\tFault on NIX_AQ_INST_S read");
		if (intr_val & BIT_ULL(13))
			devlink_fmsg_string_put(fmsg, "\n\tFault on NIX_AQ_RES_S write");
		if (intr_val & BIT_ULL(12))
			devlink_fmsg_string_put(fmsg, "\n\tAQ Doorbell Error");
		if (intr_val & BIT_ULL(6))
			devlink_fmsg_string_put(fmsg, "\n\tRx on unmapped PF_FUNC");
		if (intr_val & BIT_ULL(5))
			devlink_fmsg_string_put(fmsg, "\n\tRx multicast replication error");
		if (intr_val & BIT_ULL(4))
			devlink_fmsg_string_put(fmsg, "\n\tFault on NIX_RX_MCE_S read");
		if (intr_val & BIT_ULL(3))
			devlink_fmsg_string_put(fmsg, "\n\tFault on multicast WQE read");
		if (intr_val & BIT_ULL(2))
			devlink_fmsg_string_put(fmsg, "\n\tFault on mirror WQE read");
		if (intr_val & BIT_ULL(1))
			devlink_fmsg_string_put(fmsg, "\n\tFault on mirror pkt write");
		if (intr_val & BIT_ULL(0))
			devlink_fmsg_string_put(fmsg, "\n\tFault on multicast pkt write");
		rvu_report_pair_end(fmsg);
		break;
	case NIX_AF_RVU_RAS:
		intr_val = nix_event_context->nix_af_rvu_err;
		rvu_report_pair_start(fmsg, "NIX_AF_RAS");
		devlink_fmsg_u64_pair_put(fmsg, "\tNIX RAS Interrupt Reg ",
					  nix_event_context->nix_af_rvu_err);
		devlink_fmsg_string_put(fmsg, "\n\tPoison Data on:");
		if (intr_val & BIT_ULL(34))
			devlink_fmsg_string_put(fmsg, "\n\tNIX_AQ_INST_S");
		if (intr_val & BIT_ULL(33))
			devlink_fmsg_string_put(fmsg, "\n\tNIX_AQ_RES_S");
		if (intr_val & BIT_ULL(32))
			devlink_fmsg_string_put(fmsg, "\n\tHW ctx");
		if (intr_val & BIT_ULL(4))
			devlink_fmsg_string_put(fmsg, "\n\tPacket from mirror buffer");
		if (intr_val & BIT_ULL(3))
			devlink_fmsg_string_put(fmsg, "\n\tPacket from multicast buffer");
		if (intr_val & BIT_ULL(2))
			devlink_fmsg_string_put(fmsg, "\n\tWQE read from mirror buffer");
		if (intr_val & BIT_ULL(1))
			devlink_fmsg_string_put(fmsg, "\n\tWQE read from multicast buffer");
		if (intr_val & BIT_ULL(0))
			devlink_fmsg_string_put(fmsg, "\n\tNIX_RX_MCE_S read");
		rvu_report_pair_end(fmsg);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int rvu_hw_nix_intr_dump(struct devlink_health_reporter *reporter,
				struct devlink_fmsg *fmsg, void *ctx,
				struct netlink_ext_ack *netlink_extack)
{
	struct rvu *rvu = devlink_health_reporter_priv(reporter);
	struct rvu_devlink *rvu_dl = rvu->rvu_dl;
	struct rvu_nix_event_ctx *nix_ctx;

	nix_ctx = rvu_dl->rvu_nix_health_reporter->nix_event_ctx;

	return ctx ? rvu_nix_report_show(fmsg, ctx, NIX_AF_RVU_INTR) :
		     rvu_nix_report_show(fmsg, nix_ctx, NIX_AF_RVU_INTR);
}

static int rvu_hw_nix_intr_recover(struct devlink_health_reporter *reporter,
				   void *ctx, struct netlink_ext_ack *netlink_extack)
{
	struct rvu *rvu = devlink_health_reporter_priv(reporter);
	struct rvu_nix_event_ctx *nix_event_ctx = ctx;
	int blkaddr;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NIX, 0);
	if (blkaddr < 0)
		return blkaddr;

	if (nix_event_ctx->nix_af_rvu_int)
		rvu_write64(rvu, blkaddr, NIX_AF_RVU_INT_ENA_W1S, ~0ULL);

	return 0;
}

static int rvu_hw_nix_gen_dump(struct devlink_health_reporter *reporter,
			       struct devlink_fmsg *fmsg, void *ctx,
			       struct netlink_ext_ack *netlink_extack)
{
	struct rvu *rvu = devlink_health_reporter_priv(reporter);
	struct rvu_devlink *rvu_dl = rvu->rvu_dl;
	struct rvu_nix_event_ctx *nix_ctx;

	nix_ctx = rvu_dl->rvu_nix_health_reporter->nix_event_ctx;

	return ctx ? rvu_nix_report_show(fmsg, ctx, NIX_AF_RVU_GEN) :
		     rvu_nix_report_show(fmsg, nix_ctx, NIX_AF_RVU_GEN);
}

static int rvu_hw_nix_gen_recover(struct devlink_health_reporter *reporter,
				  void *ctx, struct netlink_ext_ack *netlink_extack)
{
	struct rvu *rvu = devlink_health_reporter_priv(reporter);
	struct rvu_nix_event_ctx *nix_event_ctx = ctx;
	int blkaddr;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NIX, 0);
	if (blkaddr < 0)
		return blkaddr;

	if (nix_event_ctx->nix_af_rvu_gen)
		rvu_write64(rvu, blkaddr, NIX_AF_GEN_INT_ENA_W1S, ~0ULL);

	return 0;
}

static int rvu_hw_nix_err_dump(struct devlink_health_reporter *reporter,
			       struct devlink_fmsg *fmsg, void *ctx,
			       struct netlink_ext_ack *netlink_extack)
{
	struct rvu *rvu = devlink_health_reporter_priv(reporter);
	struct rvu_devlink *rvu_dl = rvu->rvu_dl;
	struct rvu_nix_event_ctx *nix_ctx;

	nix_ctx = rvu_dl->rvu_nix_health_reporter->nix_event_ctx;

	return ctx ? rvu_nix_report_show(fmsg, ctx, NIX_AF_RVU_ERR) :
		     rvu_nix_report_show(fmsg, nix_ctx, NIX_AF_RVU_ERR);
}

static int rvu_hw_nix_err_recover(struct devlink_health_reporter *reporter,
				  void *ctx, struct netlink_ext_ack *netlink_extack)
{
	struct rvu *rvu = devlink_health_reporter_priv(reporter);
	struct rvu_nix_event_ctx *nix_event_ctx = ctx;
	int blkaddr;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NIX, 0);
	if (blkaddr < 0)
		return blkaddr;

	if (nix_event_ctx->nix_af_rvu_err)
		rvu_write64(rvu, blkaddr, NIX_AF_ERR_INT_ENA_W1S, ~0ULL);

	return 0;
}

static int rvu_hw_nix_ras_dump(struct devlink_health_reporter *reporter,
			       struct devlink_fmsg *fmsg, void *ctx,
			       struct netlink_ext_ack *netlink_extack)
{
	struct rvu *rvu = devlink_health_reporter_priv(reporter);
	struct rvu_devlink *rvu_dl = rvu->rvu_dl;
	struct rvu_nix_event_ctx *nix_ctx;

	nix_ctx = rvu_dl->rvu_nix_health_reporter->nix_event_ctx;

	return ctx ? rvu_nix_report_show(fmsg, ctx, NIX_AF_RVU_RAS) :
		     rvu_nix_report_show(fmsg, nix_ctx, NIX_AF_RVU_RAS);
}

static int rvu_hw_nix_ras_recover(struct devlink_health_reporter *reporter,
				  void *ctx, struct netlink_ext_ack *netlink_extack)
{
	struct rvu *rvu = devlink_health_reporter_priv(reporter);
	struct rvu_nix_event_ctx *nix_event_ctx = ctx;
	int blkaddr;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NIX, 0);
	if (blkaddr < 0)
		return blkaddr;

	if (nix_event_ctx->nix_af_rvu_int)
		rvu_write64(rvu, blkaddr, NIX_AF_RAS_ENA_W1S, ~0ULL);

	return 0;
}

RVU_REPORTERS(hw_nix_intr);
RVU_REPORTERS(hw_nix_gen);
RVU_REPORTERS(hw_nix_err);
RVU_REPORTERS(hw_nix_ras);

static void rvu_nix_health_reporters_destroy(struct rvu_devlink *rvu_dl);

static int rvu_nix_register_reporters(struct rvu_devlink *rvu_dl)
{
	struct rvu_nix_health_reporters *rvu_reporters;
	struct rvu_nix_event_ctx *nix_event_context;
	struct rvu *rvu = rvu_dl->rvu;

	rvu_reporters = kzalloc(sizeof(*rvu_reporters), GFP_KERNEL);
	if (!rvu_reporters)
		return -ENOMEM;

	rvu_dl->rvu_nix_health_reporter = rvu_reporters;
	nix_event_context = kzalloc(sizeof(*nix_event_context), GFP_KERNEL);
	if (!nix_event_context)
		return -ENOMEM;

	rvu_reporters->nix_event_ctx = nix_event_context;
	rvu_reporters->rvu_hw_nix_intr_reporter =
		devlink_health_reporter_create(rvu_dl->dl,
					       &rvu_hw_nix_intr_reporter_ops,
					       rvu);
	if (IS_ERR(rvu_reporters->rvu_hw_nix_intr_reporter)) {
		dev_warn(rvu->dev, "Failed to create hw_nix_intr reporter, err=%ld\n",
			 PTR_ERR(rvu_reporters->rvu_hw_nix_intr_reporter));
		return PTR_ERR(rvu_reporters->rvu_hw_nix_intr_reporter);
	}

	rvu_reporters->rvu_hw_nix_gen_reporter =
		devlink_health_reporter_create(rvu_dl->dl,
					       &rvu_hw_nix_gen_reporter_ops,
					       rvu);
	if (IS_ERR(rvu_reporters->rvu_hw_nix_gen_reporter)) {
		dev_warn(rvu->dev, "Failed to create hw_nix_gen reporter, err=%ld\n",
			 PTR_ERR(rvu_reporters->rvu_hw_nix_gen_reporter));
		return PTR_ERR(rvu_reporters->rvu_hw_nix_gen_reporter);
	}

	rvu_reporters->rvu_hw_nix_err_reporter =
		devlink_health_reporter_create(rvu_dl->dl,
					       &rvu_hw_nix_err_reporter_ops,
					       rvu);
	if (IS_ERR(rvu_reporters->rvu_hw_nix_err_reporter)) {
		dev_warn(rvu->dev, "Failed to create hw_nix_err reporter, err=%ld\n",
			 PTR_ERR(rvu_reporters->rvu_hw_nix_err_reporter));
		return PTR_ERR(rvu_reporters->rvu_hw_nix_err_reporter);
	}

	rvu_reporters->rvu_hw_nix_ras_reporter =
		devlink_health_reporter_create(rvu_dl->dl,
					       &rvu_hw_nix_ras_reporter_ops,
					       rvu);
	if (IS_ERR(rvu_reporters->rvu_hw_nix_ras_reporter)) {
		dev_warn(rvu->dev, "Failed to create hw_nix_ras reporter, err=%ld\n",
			 PTR_ERR(rvu_reporters->rvu_hw_nix_ras_reporter));
		return PTR_ERR(rvu_reporters->rvu_hw_nix_ras_reporter);
	}

	rvu_dl->devlink_wq = create_workqueue("rvu_devlink_wq");
	if (!rvu_dl->devlink_wq)
		return -ENOMEM;

	INIT_WORK(&rvu_reporters->intr_work, rvu_nix_intr_work);
	INIT_WORK(&rvu_reporters->gen_work, rvu_nix_gen_work);
	INIT_WORK(&rvu_reporters->err_work, rvu_nix_err_work);
	INIT_WORK(&rvu_reporters->ras_work, rvu_nix_ras_work);

	return 0;
}

static int rvu_nix_health_reporters_create(struct rvu_devlink *rvu_dl)
{
	struct rvu *rvu = rvu_dl->rvu;
	int err;

	err = rvu_nix_register_reporters(rvu_dl);
	if (err) {
		dev_warn(rvu->dev, "Failed to create nix reporter, err =%d\n",
			 err);
		return err;
	}
	rvu_nix_register_interrupts(rvu);

	return 0;
}

static void rvu_nix_health_reporters_destroy(struct rvu_devlink *rvu_dl)
{
	struct rvu_nix_health_reporters *nix_reporters;
	struct rvu *rvu = rvu_dl->rvu;

	nix_reporters = rvu_dl->rvu_nix_health_reporter;

	if (!nix_reporters->rvu_hw_nix_ras_reporter)
		return;
	if (!IS_ERR_OR_NULL(nix_reporters->rvu_hw_nix_intr_reporter))
		devlink_health_reporter_destroy(nix_reporters->rvu_hw_nix_intr_reporter);

	if (!IS_ERR_OR_NULL(nix_reporters->rvu_hw_nix_gen_reporter))
		devlink_health_reporter_destroy(nix_reporters->rvu_hw_nix_gen_reporter);

	if (!IS_ERR_OR_NULL(nix_reporters->rvu_hw_nix_err_reporter))
		devlink_health_reporter_destroy(nix_reporters->rvu_hw_nix_err_reporter);

	if (!IS_ERR_OR_NULL(nix_reporters->rvu_hw_nix_ras_reporter))
		devlink_health_reporter_destroy(nix_reporters->rvu_hw_nix_ras_reporter);

	rvu_nix_unregister_interrupts(rvu);
	kfree(rvu_dl->rvu_nix_health_reporter->nix_event_ctx);
	kfree(rvu_dl->rvu_nix_health_reporter);
}

static void rvu_npa_intr_work(struct work_struct *work)
{
	struct rvu_npa_health_reporters *rvu_npa_health_reporter;

	rvu_npa_health_reporter = container_of(work, struct rvu_npa_health_reporters, intr_work);
	devlink_health_report(rvu_npa_health_reporter->rvu_hw_npa_intr_reporter,
			      "NPA_AF_RVU Error",
			      rvu_npa_health_reporter->npa_event_ctx);
}

static irqreturn_t rvu_npa_af_rvu_intr_handler(int irq, void *rvu_irq)
{
	struct rvu_npa_event_ctx *npa_event_context;
	struct rvu_devlink *rvu_dl = rvu_irq;
	struct rvu *rvu;
	int blkaddr;
	u64 intr;

	rvu = rvu_dl->rvu;
	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPA, 0);
	if (blkaddr < 0)
		return IRQ_NONE;

	npa_event_context = rvu_dl->rvu_npa_health_reporter->npa_event_ctx;
	intr = rvu_read64(rvu, blkaddr, NPA_AF_RVU_INT);
	npa_event_context->npa_af_rvu_int = intr;

	/* Clear interrupts */
	rvu_write64(rvu, blkaddr, NPA_AF_RVU_INT, intr);
	rvu_write64(rvu, blkaddr, NPA_AF_RVU_INT_ENA_W1C, ~0ULL);
	queue_work(rvu_dl->devlink_wq, &rvu_dl->rvu_npa_health_reporter->intr_work);

	return IRQ_HANDLED;
}

static void rvu_npa_gen_work(struct work_struct *work)
{
	struct rvu_npa_health_reporters *rvu_npa_health_reporter;

	rvu_npa_health_reporter = container_of(work, struct rvu_npa_health_reporters, gen_work);
	devlink_health_report(rvu_npa_health_reporter->rvu_hw_npa_gen_reporter,
			      "NPA_AF_GEN Error",
			      rvu_npa_health_reporter->npa_event_ctx);
}

static irqreturn_t rvu_npa_af_gen_intr_handler(int irq, void *rvu_irq)
{
	struct rvu_npa_event_ctx *npa_event_context;
	struct rvu_devlink *rvu_dl = rvu_irq;
	struct rvu *rvu;
	int blkaddr;
	u64 intr;

	rvu = rvu_dl->rvu;
	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPA, 0);
	if (blkaddr < 0)
		return IRQ_NONE;

	npa_event_context = rvu_dl->rvu_npa_health_reporter->npa_event_ctx;
	intr = rvu_read64(rvu, blkaddr, NPA_AF_GEN_INT);
	npa_event_context->npa_af_rvu_gen = intr;

	/* Clear interrupts */
	rvu_write64(rvu, blkaddr, NPA_AF_GEN_INT, intr);
	rvu_write64(rvu, blkaddr, NPA_AF_GEN_INT_ENA_W1C, ~0ULL);
	queue_work(rvu_dl->devlink_wq, &rvu_dl->rvu_npa_health_reporter->gen_work);

	return IRQ_HANDLED;
}

static void rvu_npa_err_work(struct work_struct *work)
{
	struct rvu_npa_health_reporters *rvu_npa_health_reporter;

	rvu_npa_health_reporter = container_of(work, struct rvu_npa_health_reporters, err_work);
	devlink_health_report(rvu_npa_health_reporter->rvu_hw_npa_err_reporter,
			      "NPA_AF_ERR Error",
			      rvu_npa_health_reporter->npa_event_ctx);
}

static irqreturn_t rvu_npa_af_err_intr_handler(int irq, void *rvu_irq)
{
	struct rvu_npa_event_ctx *npa_event_context;
	struct rvu_devlink *rvu_dl = rvu_irq;
	struct rvu *rvu;
	int blkaddr;
	u64 intr;

	rvu = rvu_dl->rvu;
	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPA, 0);
	if (blkaddr < 0)
		return IRQ_NONE;
	npa_event_context = rvu_dl->rvu_npa_health_reporter->npa_event_ctx;
	intr = rvu_read64(rvu, blkaddr, NPA_AF_ERR_INT);
	npa_event_context->npa_af_rvu_err = intr;

	/* Clear interrupts */
	rvu_write64(rvu, blkaddr, NPA_AF_ERR_INT, intr);
	rvu_write64(rvu, blkaddr, NPA_AF_ERR_INT_ENA_W1C, ~0ULL);
	queue_work(rvu_dl->devlink_wq, &rvu_dl->rvu_npa_health_reporter->err_work);

	return IRQ_HANDLED;
}

static void rvu_npa_ras_work(struct work_struct *work)
{
	struct rvu_npa_health_reporters *rvu_npa_health_reporter;

	rvu_npa_health_reporter = container_of(work, struct rvu_npa_health_reporters, ras_work);
	devlink_health_report(rvu_npa_health_reporter->rvu_hw_npa_ras_reporter,
			      "HW NPA_AF_RAS Error reported",
			      rvu_npa_health_reporter->npa_event_ctx);
}

static irqreturn_t rvu_npa_af_ras_intr_handler(int irq, void *rvu_irq)
{
	struct rvu_npa_event_ctx *npa_event_context;
	struct rvu_devlink *rvu_dl = rvu_irq;
	struct rvu *rvu;
	int blkaddr;
	u64 intr;

	rvu = rvu_dl->rvu;
	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPA, 0);
	if (blkaddr < 0)
		return IRQ_NONE;

	npa_event_context = rvu_dl->rvu_npa_health_reporter->npa_event_ctx;
	intr = rvu_read64(rvu, blkaddr, NPA_AF_RAS);
	npa_event_context->npa_af_rvu_ras = intr;

	/* Clear interrupts */
	rvu_write64(rvu, blkaddr, NPA_AF_RAS, intr);
	rvu_write64(rvu, blkaddr, NPA_AF_RAS_ENA_W1C, ~0ULL);
	queue_work(rvu_dl->devlink_wq, &rvu_dl->rvu_npa_health_reporter->ras_work);

	return IRQ_HANDLED;
}

static void rvu_npa_unregister_interrupts(struct rvu *rvu)
{
	struct rvu_devlink *rvu_dl = rvu->rvu_dl;
	int i, offs, blkaddr;
	u64 reg;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPA, 0);
	if (blkaddr < 0)
		return;

	reg = rvu_read64(rvu, blkaddr, NPA_PRIV_AF_INT_CFG);
	offs = reg & 0x3FF;

	rvu_write64(rvu, blkaddr, NPA_AF_RVU_INT_ENA_W1C, ~0ULL);
	rvu_write64(rvu, blkaddr, NPA_AF_GEN_INT_ENA_W1C, ~0ULL);
	rvu_write64(rvu, blkaddr, NPA_AF_ERR_INT_ENA_W1C, ~0ULL);
	rvu_write64(rvu, blkaddr, NPA_AF_RAS_ENA_W1C, ~0ULL);

	for (i = 0; i < NPA_AF_INT_VEC_CNT; i++)
		if (rvu->irq_allocated[offs + i]) {
			free_irq(pci_irq_vector(rvu->pdev, offs + i), rvu_dl);
			rvu->irq_allocated[offs + i] = false;
		}
}

static int rvu_npa_register_interrupts(struct rvu *rvu)
{
	int blkaddr, base;
	bool rc;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPA, 0);
	if (blkaddr < 0)
		return blkaddr;

	/* Get NPA AF MSIX vectors offset. */
	base = rvu_read64(rvu, blkaddr, NPA_PRIV_AF_INT_CFG) & 0x3ff;
	if (!base) {
		dev_warn(rvu->dev,
			 "Failed to get NPA_AF_INT vector offsets\n");
		return 0;
	}

	/* Register and enable NPA_AF_RVU_INT interrupt */
	rc = rvu_common_request_irq(rvu, base +  NPA_AF_INT_VEC_RVU,
				    "NPA_AF_RVU_INT",
				    rvu_npa_af_rvu_intr_handler);
	if (!rc)
		goto err;
	rvu_write64(rvu, blkaddr, NPA_AF_RVU_INT_ENA_W1S, ~0ULL);

	/* Register and enable NPA_AF_GEN_INT interrupt */
	rc = rvu_common_request_irq(rvu, base + NPA_AF_INT_VEC_GEN,
				    "NPA_AF_RVU_GEN",
				    rvu_npa_af_gen_intr_handler);
	if (!rc)
		goto err;
	rvu_write64(rvu, blkaddr, NPA_AF_GEN_INT_ENA_W1S, ~0ULL);

	/* Register and enable NPA_AF_ERR_INT interrupt */
	rc = rvu_common_request_irq(rvu, base + NPA_AF_INT_VEC_AF_ERR,
				    "NPA_AF_ERR_INT",
				    rvu_npa_af_err_intr_handler);
	if (!rc)
		goto err;
	rvu_write64(rvu, blkaddr, NPA_AF_ERR_INT_ENA_W1S, ~0ULL);

	/* Register and enable NPA_AF_RAS interrupt */
	rc = rvu_common_request_irq(rvu, base + NPA_AF_INT_VEC_POISON,
				    "NPA_AF_RAS",
				    rvu_npa_af_ras_intr_handler);
	if (!rc)
		goto err;
	rvu_write64(rvu, blkaddr, NPA_AF_RAS_ENA_W1S, ~0ULL);

	return 0;
err:
	rvu_npa_unregister_interrupts(rvu);
	return rc;
}

static int rvu_npa_report_show(struct devlink_fmsg *fmsg, void *ctx,
			       enum npa_af_rvu_health health_reporter)
{
	struct rvu_npa_event_ctx *npa_event_context;
	unsigned int alloc_dis, free_dis;
	u64 intr_val;

	npa_event_context = ctx;
	switch (health_reporter) {
	case NPA_AF_RVU_GEN:
		intr_val = npa_event_context->npa_af_rvu_gen;
		rvu_report_pair_start(fmsg, "NPA_AF_GENERAL");
		devlink_fmsg_u64_pair_put(fmsg, "\tNPA General Interrupt Reg ",
					  npa_event_context->npa_af_rvu_gen);
		if (intr_val & BIT_ULL(32))
			devlink_fmsg_string_put(fmsg, "\n\tUnmap PF Error");

		free_dis = FIELD_GET(GENMASK(15, 0), intr_val);
		if (free_dis & BIT(NPA_INPQ_NIX0_RX))
			devlink_fmsg_string_put(fmsg, "\n\tNIX0: free disabled RX");
		if (free_dis & BIT(NPA_INPQ_NIX0_TX))
			devlink_fmsg_string_put(fmsg, "\n\tNIX0:free disabled TX");
		if (free_dis & BIT(NPA_INPQ_NIX1_RX))
			devlink_fmsg_string_put(fmsg, "\n\tNIX1: free disabled RX");
		if (free_dis & BIT(NPA_INPQ_NIX1_TX))
			devlink_fmsg_string_put(fmsg, "\n\tNIX1:free disabled TX");
		if (free_dis & BIT(NPA_INPQ_SSO))
			devlink_fmsg_string_put(fmsg, "\n\tFree Disabled for SSO");
		if (free_dis & BIT(NPA_INPQ_TIM))
			devlink_fmsg_string_put(fmsg, "\n\tFree Disabled for TIM");
		if (free_dis & BIT(NPA_INPQ_DPI))
			devlink_fmsg_string_put(fmsg, "\n\tFree Disabled for DPI");
		if (free_dis & BIT(NPA_INPQ_AURA_OP))
			devlink_fmsg_string_put(fmsg, "\n\tFree Disabled for AURA");

		alloc_dis = FIELD_GET(GENMASK(31, 16), intr_val);
		if (alloc_dis & BIT(NPA_INPQ_NIX0_RX))
			devlink_fmsg_string_put(fmsg, "\n\tNIX0: alloc disabled RX");
		if (alloc_dis & BIT(NPA_INPQ_NIX0_TX))
			devlink_fmsg_string_put(fmsg, "\n\tNIX0:alloc disabled TX");
		if (alloc_dis & BIT(NPA_INPQ_NIX1_RX))
			devlink_fmsg_string_put(fmsg, "\n\tNIX1: alloc disabled RX");
		if (alloc_dis & BIT(NPA_INPQ_NIX1_TX))
			devlink_fmsg_string_put(fmsg, "\n\tNIX1:alloc disabled TX");
		if (alloc_dis & BIT(NPA_INPQ_SSO))
			devlink_fmsg_string_put(fmsg, "\n\tAlloc Disabled for SSO");
		if (alloc_dis & BIT(NPA_INPQ_TIM))
			devlink_fmsg_string_put(fmsg, "\n\tAlloc Disabled for TIM");
		if (alloc_dis & BIT(NPA_INPQ_DPI))
			devlink_fmsg_string_put(fmsg, "\n\tAlloc Disabled for DPI");
		if (alloc_dis & BIT(NPA_INPQ_AURA_OP))
			devlink_fmsg_string_put(fmsg, "\n\tAlloc Disabled for AURA");

		rvu_report_pair_end(fmsg);
		break;
	case NPA_AF_RVU_ERR:
		rvu_report_pair_start(fmsg, "NPA_AF_ERR");
		devlink_fmsg_u64_pair_put(fmsg, "\tNPA Error Interrupt Reg ",
					  npa_event_context->npa_af_rvu_err);
		if (npa_event_context->npa_af_rvu_err & BIT_ULL(14))
			devlink_fmsg_string_put(fmsg, "\n\tFault on NPA_AQ_INST_S read");
		if (npa_event_context->npa_af_rvu_err & BIT_ULL(13))
			devlink_fmsg_string_put(fmsg, "\n\tFault on NPA_AQ_RES_S write");
		if (npa_event_context->npa_af_rvu_err & BIT_ULL(12))
			devlink_fmsg_string_put(fmsg, "\n\tAQ Doorbell Error");
		rvu_report_pair_end(fmsg);
		break;
	case NPA_AF_RVU_RAS:
		rvu_report_pair_start(fmsg, "NPA_AF_RVU_RAS");
		devlink_fmsg_u64_pair_put(fmsg, "\tNPA RAS Interrupt Reg ",
					  npa_event_context->npa_af_rvu_ras);
		if (npa_event_context->npa_af_rvu_ras & BIT_ULL(34))
			devlink_fmsg_string_put(fmsg, "\n\tPoison data on NPA_AQ_INST_S");
		if (npa_event_context->npa_af_rvu_ras & BIT_ULL(33))
			devlink_fmsg_string_put(fmsg, "\n\tPoison data on NPA_AQ_RES_S");
		if (npa_event_context->npa_af_rvu_ras & BIT_ULL(32))
			devlink_fmsg_string_put(fmsg, "\n\tPoison data on HW context");
		rvu_report_pair_end(fmsg);
		break;
	case NPA_AF_RVU_INTR:
		rvu_report_pair_start(fmsg, "NPA_AF_RVU");
		devlink_fmsg_u64_pair_put(fmsg, "\tNPA RVU Interrupt Reg ",
					  npa_event_context->npa_af_rvu_int);
		if (npa_event_context->npa_af_rvu_int & BIT_ULL(0))
			devlink_fmsg_string_put(fmsg, "\n\tUnmap Slot Error");
		rvu_report_pair_end(fmsg);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int rvu_hw_npa_intr_dump(struct devlink_health_reporter *reporter,
				struct devlink_fmsg *fmsg, void *ctx,
				struct netlink_ext_ack *netlink_extack)
{
	struct rvu *rvu = devlink_health_reporter_priv(reporter);
	struct rvu_devlink *rvu_dl = rvu->rvu_dl;
	struct rvu_npa_event_ctx *npa_ctx;

	npa_ctx = rvu_dl->rvu_npa_health_reporter->npa_event_ctx;

	return ctx ? rvu_npa_report_show(fmsg, ctx, NPA_AF_RVU_INTR) :
		     rvu_npa_report_show(fmsg, npa_ctx, NPA_AF_RVU_INTR);
}

static int rvu_hw_npa_intr_recover(struct devlink_health_reporter *reporter,
				   void *ctx, struct netlink_ext_ack *netlink_extack)
{
	struct rvu *rvu = devlink_health_reporter_priv(reporter);
	struct rvu_npa_event_ctx *npa_event_ctx = ctx;
	int blkaddr;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPA, 0);
	if (blkaddr < 0)
		return blkaddr;

	if (npa_event_ctx->npa_af_rvu_int)
		rvu_write64(rvu, blkaddr, NPA_AF_RVU_INT_ENA_W1S, ~0ULL);

	return 0;
}

static int rvu_hw_npa_gen_dump(struct devlink_health_reporter *reporter,
			       struct devlink_fmsg *fmsg, void *ctx,
			       struct netlink_ext_ack *netlink_extack)
{
	struct rvu *rvu = devlink_health_reporter_priv(reporter);
	struct rvu_devlink *rvu_dl = rvu->rvu_dl;
	struct rvu_npa_event_ctx *npa_ctx;

	npa_ctx = rvu_dl->rvu_npa_health_reporter->npa_event_ctx;

	return ctx ? rvu_npa_report_show(fmsg, ctx, NPA_AF_RVU_GEN) :
		     rvu_npa_report_show(fmsg, npa_ctx, NPA_AF_RVU_GEN);
}

static int rvu_hw_npa_gen_recover(struct devlink_health_reporter *reporter,
				  void *ctx, struct netlink_ext_ack *netlink_extack)
{
	struct rvu *rvu = devlink_health_reporter_priv(reporter);
	struct rvu_npa_event_ctx *npa_event_ctx = ctx;
	int blkaddr;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPA, 0);
	if (blkaddr < 0)
		return blkaddr;

	if (npa_event_ctx->npa_af_rvu_gen)
		rvu_write64(rvu, blkaddr, NPA_AF_GEN_INT_ENA_W1S, ~0ULL);

	return 0;
}

static int rvu_hw_npa_err_dump(struct devlink_health_reporter *reporter,
			       struct devlink_fmsg *fmsg, void *ctx,
			       struct netlink_ext_ack *netlink_extack)
{
	struct rvu *rvu = devlink_health_reporter_priv(reporter);
	struct rvu_devlink *rvu_dl = rvu->rvu_dl;
	struct rvu_npa_event_ctx *npa_ctx;

	npa_ctx = rvu_dl->rvu_npa_health_reporter->npa_event_ctx;

	return ctx ? rvu_npa_report_show(fmsg, ctx, NPA_AF_RVU_ERR) :
		     rvu_npa_report_show(fmsg, npa_ctx, NPA_AF_RVU_ERR);
}

static int rvu_hw_npa_err_recover(struct devlink_health_reporter *reporter,
				  void *ctx, struct netlink_ext_ack *netlink_extack)
{
	struct rvu *rvu = devlink_health_reporter_priv(reporter);
	struct rvu_npa_event_ctx *npa_event_ctx = ctx;
	int blkaddr;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPA, 0);
	if (blkaddr < 0)
		return blkaddr;

	if (npa_event_ctx->npa_af_rvu_err)
		rvu_write64(rvu, blkaddr, NPA_AF_ERR_INT_ENA_W1S, ~0ULL);

	return 0;
}

static int rvu_hw_npa_ras_dump(struct devlink_health_reporter *reporter,
			       struct devlink_fmsg *fmsg, void *ctx,
			       struct netlink_ext_ack *netlink_extack)
{
	struct rvu *rvu = devlink_health_reporter_priv(reporter);
	struct rvu_devlink *rvu_dl = rvu->rvu_dl;
	struct rvu_npa_event_ctx *npa_ctx;

	npa_ctx = rvu_dl->rvu_npa_health_reporter->npa_event_ctx;

	return ctx ? rvu_npa_report_show(fmsg, ctx, NPA_AF_RVU_RAS) :
		     rvu_npa_report_show(fmsg, npa_ctx, NPA_AF_RVU_RAS);
}

static int rvu_hw_npa_ras_recover(struct devlink_health_reporter *reporter,
				  void *ctx, struct netlink_ext_ack *netlink_extack)
{
	struct rvu *rvu = devlink_health_reporter_priv(reporter);
	struct rvu_npa_event_ctx *npa_event_ctx = ctx;
	int blkaddr;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPA, 0);
	if (blkaddr < 0)
		return blkaddr;

	if (npa_event_ctx->npa_af_rvu_ras)
		rvu_write64(rvu, blkaddr, NPA_AF_RAS_ENA_W1S, ~0ULL);

	return 0;
}

RVU_REPORTERS(hw_npa_intr);
RVU_REPORTERS(hw_npa_gen);
RVU_REPORTERS(hw_npa_err);
RVU_REPORTERS(hw_npa_ras);

static void rvu_npa_health_reporters_destroy(struct rvu_devlink *rvu_dl);

static int rvu_npa_register_reporters(struct rvu_devlink *rvu_dl)
{
	struct rvu_npa_health_reporters *rvu_reporters;
	struct rvu_npa_event_ctx *npa_event_context;
	struct rvu *rvu = rvu_dl->rvu;

	rvu_reporters = kzalloc(sizeof(*rvu_reporters), GFP_KERNEL);
	if (!rvu_reporters)
		return -ENOMEM;

	rvu_dl->rvu_npa_health_reporter = rvu_reporters;
	npa_event_context = kzalloc(sizeof(*npa_event_context), GFP_KERNEL);
	if (!npa_event_context)
		return -ENOMEM;

	rvu_reporters->npa_event_ctx = npa_event_context;
	rvu_reporters->rvu_hw_npa_intr_reporter =
		devlink_health_reporter_create(rvu_dl->dl,
					       &rvu_hw_npa_intr_reporter_ops,
					       rvu);
	if (IS_ERR(rvu_reporters->rvu_hw_npa_intr_reporter)) {
		dev_warn(rvu->dev, "Failed to create hw_npa_intr reporter, err=%ld\n",
			 PTR_ERR(rvu_reporters->rvu_hw_npa_intr_reporter));
		return PTR_ERR(rvu_reporters->rvu_hw_npa_intr_reporter);
	}

	rvu_reporters->rvu_hw_npa_gen_reporter =
		devlink_health_reporter_create(rvu_dl->dl,
					       &rvu_hw_npa_gen_reporter_ops,
					       rvu);
	if (IS_ERR(rvu_reporters->rvu_hw_npa_gen_reporter)) {
		dev_warn(rvu->dev, "Failed to create hw_npa_gen reporter, err=%ld\n",
			 PTR_ERR(rvu_reporters->rvu_hw_npa_gen_reporter));
		return PTR_ERR(rvu_reporters->rvu_hw_npa_gen_reporter);
	}

	rvu_reporters->rvu_hw_npa_err_reporter =
		devlink_health_reporter_create(rvu_dl->dl,
					       &rvu_hw_npa_err_reporter_ops,
					       rvu);
	if (IS_ERR(rvu_reporters->rvu_hw_npa_err_reporter)) {
		dev_warn(rvu->dev, "Failed to create hw_npa_err reporter, err=%ld\n",
			 PTR_ERR(rvu_reporters->rvu_hw_npa_err_reporter));
		return PTR_ERR(rvu_reporters->rvu_hw_npa_err_reporter);
	}

	rvu_reporters->rvu_hw_npa_ras_reporter =
		devlink_health_reporter_create(rvu_dl->dl,
					       &rvu_hw_npa_ras_reporter_ops,
					       rvu);
	if (IS_ERR(rvu_reporters->rvu_hw_npa_ras_reporter)) {
		dev_warn(rvu->dev, "Failed to create hw_npa_ras reporter, err=%ld\n",
			 PTR_ERR(rvu_reporters->rvu_hw_npa_ras_reporter));
		return PTR_ERR(rvu_reporters->rvu_hw_npa_ras_reporter);
	}

	rvu_dl->devlink_wq = create_workqueue("rvu_devlink_wq");
	if (!rvu_dl->devlink_wq)
		return -ENOMEM;

	INIT_WORK(&rvu_reporters->intr_work, rvu_npa_intr_work);
	INIT_WORK(&rvu_reporters->err_work, rvu_npa_err_work);
	INIT_WORK(&rvu_reporters->gen_work, rvu_npa_gen_work);
	INIT_WORK(&rvu_reporters->ras_work, rvu_npa_ras_work);

	return 0;
}

static int rvu_npa_health_reporters_create(struct rvu_devlink *rvu_dl)
{
	struct rvu *rvu = rvu_dl->rvu;
	int err;

	err = rvu_npa_register_reporters(rvu_dl);
	if (err) {
		dev_warn(rvu->dev, "Failed to create npa reporter, err =%d\n",
			 err);
		return err;
	}
	rvu_npa_register_interrupts(rvu);

	return 0;
}

static void rvu_npa_health_reporters_destroy(struct rvu_devlink *rvu_dl)
{
	struct rvu_npa_health_reporters *npa_reporters;
	struct rvu *rvu = rvu_dl->rvu;

	npa_reporters = rvu_dl->rvu_npa_health_reporter;

	if (!npa_reporters->rvu_hw_npa_ras_reporter)
		return;
	if (!IS_ERR_OR_NULL(npa_reporters->rvu_hw_npa_intr_reporter))
		devlink_health_reporter_destroy(npa_reporters->rvu_hw_npa_intr_reporter);

	if (!IS_ERR_OR_NULL(npa_reporters->rvu_hw_npa_gen_reporter))
		devlink_health_reporter_destroy(npa_reporters->rvu_hw_npa_gen_reporter);

	if (!IS_ERR_OR_NULL(npa_reporters->rvu_hw_npa_err_reporter))
		devlink_health_reporter_destroy(npa_reporters->rvu_hw_npa_err_reporter);

	if (!IS_ERR_OR_NULL(npa_reporters->rvu_hw_npa_ras_reporter))
		devlink_health_reporter_destroy(npa_reporters->rvu_hw_npa_ras_reporter);

	rvu_npa_unregister_interrupts(rvu);
	kfree(rvu_dl->rvu_npa_health_reporter->npa_event_ctx);
	kfree(rvu_dl->rvu_npa_health_reporter);
}

static int rvu_health_reporters_create(struct rvu *rvu)
{
	struct rvu_devlink *rvu_dl;
	int err;

	rvu_dl = rvu->rvu_dl;
	err = rvu_npa_health_reporters_create(rvu_dl);
	if (err)
		return err;

	return rvu_nix_health_reporters_create(rvu_dl);
}

static void rvu_health_reporters_destroy(struct rvu *rvu)
{
	struct rvu_devlink *rvu_dl;

	if (!rvu->rvu_dl)
		return;

	rvu_dl = rvu->rvu_dl;
	rvu_npa_health_reporters_destroy(rvu_dl);
	rvu_nix_health_reporters_destroy(rvu_dl);
}

/* Devlink Params APIs */
static int rvu_af_dl_dwrr_mtu_validate(struct devlink *devlink, u32 id,
				       union devlink_param_value val,
				       struct netlink_ext_ack *extack)
{
	struct rvu_devlink *rvu_dl = devlink_priv(devlink);
	struct rvu *rvu = rvu_dl->rvu;
	int dwrr_mtu = val.vu32;
	struct nix_txsch *txsch;
	struct nix_hw *nix_hw;

	if (!rvu->hw->cap.nix_common_dwrr_mtu) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Setting DWRR_MTU is not supported on this silicon");
		return -EOPNOTSUPP;
	}

	if ((dwrr_mtu > 65536 || !is_power_of_2(dwrr_mtu)) &&
	    (dwrr_mtu != 9728 && dwrr_mtu != 10240)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Invalid, supported MTUs are 0,2,4,8.16,32,64....4K,8K,32K,64K and 9728, 10240");
		return -EINVAL;
	}

	nix_hw = get_nix_hw(rvu->hw, BLKADDR_NIX0);
	if (!nix_hw)
		return -ENODEV;

	txsch = &nix_hw->txsch[NIX_TXSCH_LVL_SMQ];
	if (rvu_rsrc_free_count(&txsch->schq) != txsch->schq.max) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Changing DWRR MTU is not supported when there are active NIXLFs");
		NL_SET_ERR_MSG_MOD(extack,
				   "Make sure none of the PF/VF interfaces are initialized and retry");
		return -EOPNOTSUPP;
	}

	return 0;
}

static int rvu_af_dl_dwrr_mtu_set(struct devlink *devlink, u32 id,
				  struct devlink_param_gset_ctx *ctx,
				  struct netlink_ext_ack *extack)
{
	struct rvu_devlink *rvu_dl = devlink_priv(devlink);
	struct rvu *rvu = rvu_dl->rvu;
	u64 dwrr_mtu;

	dwrr_mtu = convert_bytes_to_dwrr_mtu(ctx->val.vu32);
	rvu_write64(rvu, BLKADDR_NIX0,
		    nix_get_dwrr_mtu_reg(rvu->hw, SMQ_LINK_TYPE_RPM), dwrr_mtu);

	return 0;
}

static int rvu_af_dl_dwrr_mtu_get(struct devlink *devlink, u32 id,
				  struct devlink_param_gset_ctx *ctx)
{
	struct rvu_devlink *rvu_dl = devlink_priv(devlink);
	struct rvu *rvu = rvu_dl->rvu;
	u64 dwrr_mtu;

	if (!rvu->hw->cap.nix_common_dwrr_mtu)
		return -EOPNOTSUPP;

	dwrr_mtu = rvu_read64(rvu, BLKADDR_NIX0,
			      nix_get_dwrr_mtu_reg(rvu->hw, SMQ_LINK_TYPE_RPM));
	ctx->val.vu32 = convert_dwrr_mtu_to_bytes(dwrr_mtu);

	return 0;
}

enum rvu_af_dl_param_id {
	RVU_AF_DEVLINK_PARAM_ID_BASE = DEVLINK_PARAM_GENERIC_ID_MAX,
	RVU_AF_DEVLINK_PARAM_ID_DWRR_MTU,
	RVU_AF_DEVLINK_PARAM_ID_NPC_MCAM_ZONE_PERCENT,
	RVU_AF_DEVLINK_PARAM_ID_NPC_EXACT_FEATURE_DISABLE,
	RVU_AF_DEVLINK_PARAM_ID_NPC_DEF_RULE_CNTR_ENABLE,
	RVU_AF_DEVLINK_PARAM_ID_NIX_MAXLF,
};

static int rvu_af_npc_exact_feature_get(struct devlink *devlink, u32 id,
					struct devlink_param_gset_ctx *ctx)
{
	struct rvu_devlink *rvu_dl = devlink_priv(devlink);
	struct rvu *rvu = rvu_dl->rvu;
	bool enabled;

	enabled = rvu_npc_exact_has_match_table(rvu);

	snprintf(ctx->val.vstr, sizeof(ctx->val.vstr), "%s",
		 enabled ? "enabled" : "disabled");

	return 0;
}

static int rvu_af_npc_exact_feature_disable(struct devlink *devlink, u32 id,
					    struct devlink_param_gset_ctx *ctx,
					    struct netlink_ext_ack *extack)
{
	struct rvu_devlink *rvu_dl = devlink_priv(devlink);
	struct rvu *rvu = rvu_dl->rvu;

	rvu_npc_exact_disable_feature(rvu);

	return 0;
}

static int rvu_af_npc_exact_feature_validate(struct devlink *devlink, u32 id,
					     union devlink_param_value val,
					     struct netlink_ext_ack *extack)
{
	struct rvu_devlink *rvu_dl = devlink_priv(devlink);
	struct rvu *rvu = rvu_dl->rvu;
	u64 enable;

	if (kstrtoull(val.vstr, 10, &enable)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Only 1 value is supported");
		return -EINVAL;
	}

	if (enable != 1) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Only disabling exact match feature is supported");
		return -EINVAL;
	}

	if (rvu_npc_exact_can_disable_feature(rvu))
		return 0;

	NL_SET_ERR_MSG_MOD(extack,
			   "Can't disable exact match feature; Please try before any configuration");
	return -EFAULT;
}

static int rvu_af_dl_npc_mcam_high_zone_percent_get(struct devlink *devlink, u32 id,
						    struct devlink_param_gset_ctx *ctx)
{
	struct rvu_devlink *rvu_dl = devlink_priv(devlink);
	struct rvu *rvu = rvu_dl->rvu;
	struct npc_mcam *mcam;
	u32 percent;

	mcam = &rvu->hw->mcam;
	percent = (mcam->hprio_count * 100) / mcam->bmap_entries;
	ctx->val.vu8 = (u8)percent;

	return 0;
}

static int rvu_af_dl_npc_mcam_high_zone_percent_set(struct devlink *devlink, u32 id,
						    struct devlink_param_gset_ctx *ctx,
						    struct netlink_ext_ack *extack)
{
	struct rvu_devlink *rvu_dl = devlink_priv(devlink);
	struct rvu *rvu = rvu_dl->rvu;
	struct npc_mcam *mcam;
	u32 percent;

	percent = ctx->val.vu8;
	mcam = &rvu->hw->mcam;
	mcam->hprio_count = (mcam->bmap_entries * percent) / 100;
	mcam->hprio_end = mcam->hprio_count;
	mcam->lprio_count = (mcam->bmap_entries - mcam->hprio_count) / 2;
	mcam->lprio_start = mcam->bmap_entries - mcam->lprio_count;

	return 0;
}

static int rvu_af_dl_npc_mcam_high_zone_percent_validate(struct devlink *devlink, u32 id,
							 union devlink_param_value val,
							 struct netlink_ext_ack *extack)
{
	struct rvu_devlink *rvu_dl = devlink_priv(devlink);
	struct rvu *rvu = rvu_dl->rvu;
	struct npc_mcam *mcam;

	/* The percent of high prio zone must range from 12% to 100% of unreserved mcam space */
	if (val.vu8 < 12 || val.vu8 > 100) {
		NL_SET_ERR_MSG_MOD(extack,
				   "mcam high zone percent must be between 12% to 100%");
		return -EINVAL;
	}

	/* Do not allow user to modify the high priority zone entries while mcam entries
	 * have already been assigned.
	 */
	mcam = &rvu->hw->mcam;
	if (mcam->bmap_fcnt < mcam->bmap_entries) {
		NL_SET_ERR_MSG_MOD(extack,
				   "mcam entries have already been assigned, can't resize");
		return -EPERM;
	}

	return 0;
}

static int rvu_af_dl_npc_def_rule_cntr_get(struct devlink *devlink, u32 id,
					   struct devlink_param_gset_ctx *ctx)
{
	struct rvu_devlink *rvu_dl = devlink_priv(devlink);
	struct rvu *rvu = rvu_dl->rvu;

	ctx->val.vbool = rvu->def_rule_cntr_en;

	return 0;
}

static int rvu_af_dl_npc_def_rule_cntr_set(struct devlink *devlink, u32 id,
					   struct devlink_param_gset_ctx *ctx,
					   struct netlink_ext_ack *extack)
{
	struct rvu_devlink *rvu_dl = devlink_priv(devlink);
	struct rvu *rvu = rvu_dl->rvu;
	int err;

	err = npc_config_cntr_default_entries(rvu, ctx->val.vbool);
	if (!err)
		rvu->def_rule_cntr_en = ctx->val.vbool;

	return err;
}

static int rvu_af_dl_nix_maxlf_get(struct devlink *devlink, u32 id,
				   struct devlink_param_gset_ctx *ctx)
{
	struct rvu_devlink *rvu_dl = devlink_priv(devlink);
	struct rvu *rvu = rvu_dl->rvu;

	ctx->val.vu16 = (u16)rvu_get_nixlf_count(rvu);

	return 0;
}

static int rvu_af_dl_nix_maxlf_set(struct devlink *devlink, u32 id,
				   struct devlink_param_gset_ctx *ctx,
				   struct netlink_ext_ack *extack)
{
	struct rvu_devlink *rvu_dl = devlink_priv(devlink);
	struct rvu *rvu = rvu_dl->rvu;
	struct rvu_block *block;
	int blkaddr = 0;

	npc_mcam_rsrcs_deinit(rvu);
	blkaddr = rvu_get_next_nix_blkaddr(rvu, blkaddr);
	while (blkaddr) {
		block = &rvu->hw->block[blkaddr];
		block->lf.max = ctx->val.vu16;
		blkaddr = rvu_get_next_nix_blkaddr(rvu, blkaddr);
	}

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPC, 0);
	npc_mcam_rsrcs_init(rvu, blkaddr);

	return 0;
}

static int rvu_af_dl_nix_maxlf_validate(struct devlink *devlink, u32 id,
					union devlink_param_value val,
					struct netlink_ext_ack *extack)
{
	struct rvu_devlink *rvu_dl = devlink_priv(devlink);
	struct rvu *rvu = rvu_dl->rvu;
	u16 max_nix0_lf, max_nix1_lf;
	struct npc_mcam *mcam;
	u64 cfg;

	cfg = rvu_read64(rvu, BLKADDR_NIX0, NIX_AF_CONST2);
	max_nix0_lf = cfg & 0xFFF;
	cfg = rvu_read64(rvu, BLKADDR_NIX1, NIX_AF_CONST2);
	max_nix1_lf = cfg & 0xFFF;

	/* Do not allow user to modify maximum NIX LFs while mcam entries
	 * have already been assigned.
	 */
	mcam = &rvu->hw->mcam;
	if (mcam->bmap_fcnt < mcam->bmap_entries) {
		NL_SET_ERR_MSG_MOD(extack,
				   "mcam entries have already been assigned, can't resize");
		return -EPERM;
	}

	if (max_nix0_lf && val.vu16 > max_nix0_lf) {
		NL_SET_ERR_MSG_MOD(extack,
				   "requested nixlf is greater than the max supported nix0_lf");
		return -EPERM;
	}

	if (max_nix1_lf && val.vu16 > max_nix1_lf) {
		NL_SET_ERR_MSG_MOD(extack,
				   "requested nixlf is greater than the max supported nix1_lf");
		return -EINVAL;
	}

	return 0;
}

static const struct devlink_param rvu_af_dl_params[] = {
	DEVLINK_PARAM_DRIVER(RVU_AF_DEVLINK_PARAM_ID_DWRR_MTU,
			     "dwrr_mtu", DEVLINK_PARAM_TYPE_U32,
			     BIT(DEVLINK_PARAM_CMODE_RUNTIME),
			     rvu_af_dl_dwrr_mtu_get, rvu_af_dl_dwrr_mtu_set,
			     rvu_af_dl_dwrr_mtu_validate),
	DEVLINK_PARAM_DRIVER(RVU_AF_DEVLINK_PARAM_ID_NPC_MCAM_ZONE_PERCENT,
			     "npc_mcam_high_zone_percent", DEVLINK_PARAM_TYPE_U8,
			     BIT(DEVLINK_PARAM_CMODE_RUNTIME),
			     rvu_af_dl_npc_mcam_high_zone_percent_get,
			     rvu_af_dl_npc_mcam_high_zone_percent_set,
			     rvu_af_dl_npc_mcam_high_zone_percent_validate),
	DEVLINK_PARAM_DRIVER(RVU_AF_DEVLINK_PARAM_ID_NPC_DEF_RULE_CNTR_ENABLE,
			     "npc_def_rule_cntr", DEVLINK_PARAM_TYPE_BOOL,
			     BIT(DEVLINK_PARAM_CMODE_RUNTIME),
			     rvu_af_dl_npc_def_rule_cntr_get,
			     rvu_af_dl_npc_def_rule_cntr_set, NULL),
	DEVLINK_PARAM_DRIVER(RVU_AF_DEVLINK_PARAM_ID_NIX_MAXLF,
			     "nix_maxlf", DEVLINK_PARAM_TYPE_U16,
			     BIT(DEVLINK_PARAM_CMODE_RUNTIME),
			     rvu_af_dl_nix_maxlf_get,
			     rvu_af_dl_nix_maxlf_set,
			     rvu_af_dl_nix_maxlf_validate),
};

static const struct devlink_param rvu_af_dl_param_exact_match[] = {
	DEVLINK_PARAM_DRIVER(RVU_AF_DEVLINK_PARAM_ID_NPC_EXACT_FEATURE_DISABLE,
			     "npc_exact_feature_disable", DEVLINK_PARAM_TYPE_STRING,
			     BIT(DEVLINK_PARAM_CMODE_RUNTIME),
			     rvu_af_npc_exact_feature_get,
			     rvu_af_npc_exact_feature_disable,
			     rvu_af_npc_exact_feature_validate),
};

/* Devlink switch mode */
static int rvu_devlink_eswitch_mode_get(struct devlink *devlink, u16 *mode)
{
	struct rvu_devlink *rvu_dl = devlink_priv(devlink);
	struct rvu *rvu = rvu_dl->rvu;
	struct rvu_switch *rswitch;

	if (rvu->rep_mode)
		return -EOPNOTSUPP;

	rswitch = &rvu->rswitch;
	*mode = rswitch->mode;

	return 0;
}

static int rvu_devlink_eswitch_mode_set(struct devlink *devlink, u16 mode,
					struct netlink_ext_ack *extack)
{
	struct rvu_devlink *rvu_dl = devlink_priv(devlink);
	struct rvu *rvu = rvu_dl->rvu;
	struct rvu_switch *rswitch;

	rswitch = &rvu->rswitch;
	switch (mode) {
	case DEVLINK_ESWITCH_MODE_LEGACY:
	case DEVLINK_ESWITCH_MODE_SWITCHDEV:
		if (rswitch->mode == mode)
			return 0;
		rswitch->mode = mode;
		if (mode == DEVLINK_ESWITCH_MODE_SWITCHDEV)
			rvu_switch_enable(rvu);
		else
			rvu_switch_disable(rvu);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct devlink_ops rvu_devlink_ops = {
	.eswitch_mode_get = rvu_devlink_eswitch_mode_get,
	.eswitch_mode_set = rvu_devlink_eswitch_mode_set,
};

int rvu_register_dl(struct rvu *rvu)
{
	struct rvu_devlink *rvu_dl;
	struct devlink *dl;
	int err;

	dl = devlink_alloc(&rvu_devlink_ops, sizeof(struct rvu_devlink),
			   rvu->dev);
	if (!dl) {
		dev_warn(rvu->dev, "devlink_alloc failed\n");
		return -ENOMEM;
	}

	rvu_dl = devlink_priv(dl);
	rvu_dl->dl = dl;
	rvu_dl->rvu = rvu;
	rvu->rvu_dl = rvu_dl;

	err = rvu_health_reporters_create(rvu);
	if (err) {
		dev_err(rvu->dev,
			"devlink health reporter creation failed with error %d\n", err);
		goto err_dl_health;
	}

	err = devlink_params_register(dl, rvu_af_dl_params, ARRAY_SIZE(rvu_af_dl_params));
	if (err) {
		dev_err(rvu->dev,
			"devlink params register failed with error %d", err);
		goto err_dl_health;
	}

	/* Register exact match devlink only for CN10K-B */
	if (!rvu_npc_exact_has_match_table(rvu))
		goto done;

	err = devlink_params_register(dl, rvu_af_dl_param_exact_match,
				      ARRAY_SIZE(rvu_af_dl_param_exact_match));
	if (err) {
		dev_err(rvu->dev,
			"devlink exact match params register failed with error %d", err);
		goto err_dl_exact_match;
	}

done:
	devlink_register(dl);
	return 0;

err_dl_exact_match:
	devlink_params_unregister(dl, rvu_af_dl_params, ARRAY_SIZE(rvu_af_dl_params));

err_dl_health:
	rvu_health_reporters_destroy(rvu);
	devlink_free(dl);
	return err;
}

void rvu_unregister_dl(struct rvu *rvu)
{
	struct rvu_devlink *rvu_dl = rvu->rvu_dl;
	struct devlink *dl = rvu_dl->dl;

	devlink_unregister(dl);

	devlink_params_unregister(dl, rvu_af_dl_params, ARRAY_SIZE(rvu_af_dl_params));

	/* Unregister exact match devlink only for CN10K-B */
	if (rvu_npc_exact_has_match_table(rvu))
		devlink_params_unregister(dl, rvu_af_dl_param_exact_match,
					  ARRAY_SIZE(rvu_af_dl_param_exact_match));

	rvu_health_reporters_destroy(rvu);
	devlink_free(dl);
}
