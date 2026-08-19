// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Renesas Electronics Corp.
 * Copyright (C) 2026 Ideas on Board Oy
 * Copyright (C) 2026 Ragnatech AB
 *
 * Support library for Dreamchip HDR RPPX1 High Dynamic Range Real-time Pixel
 * Processor.
 */

#include <linux/io.h>
#include <linux/module.h>
#include <linux/slab.h>

#include "rppx1.h"

/* RPP_HDR Base Addresses */
#define RPPX1_HDRREGS_BASE			0x0000
#define RPPX1_HDR_IRQ_BASE			0x0200
#define RPPX1_RPP_OUT_BASE			0x0800
#define RPPX1_RPP_RMAP_BASE			0x0c00
#define RPPX1_RPP_RMAP_MEAS_BASE		0x1000
#define RPPX1_RPP_MAIN_PRE1_BASE		0x2000
#define RPPX1_RPP_MAIN_PRE2_BASE		0x4000
#define RPPX1_RPP_MAIN_POST_BASE		0xa000
#define RPPX1_RPP_MVOUT_BASE			0xc000
#define RPPX1_RPP_FUSA_BASE			0xf000

#define RPPX1_RPP_HDRREGS_VERSION_REG		(RPPX1_HDRREGS_BASE + 0x0000)

#define RPPX1_RPP_HDR_UPD_REG			(RPPX1_HDRREGS_BASE + 0x0004)
#define RPPX1_RPP_HDR_UPD_REGS_GEN_CFG_UPD	BIT(1)
#define RPPX1_RPP_HDR_UPD_REGS_CFG_UPD		BIT(0)

#define RPPX1_RESERVED_3_REG			(RPPX1_HDRREGS_BASE + 0x0008)

#define RPPX1_RPP_HDR_INFORM_ENABLE_REG		(RPPX1_HDRREGS_BASE + 0x000c)
#define RPPX1_RPP_HDR_INFORM_ENABLE_ENABLE	1
#define RPPX1_RPP_HDR_INFORM_ENABLE_DISABLE	0

#define RPPX1_RPP_HDR_OUT_IF_ON_REG		(RPPX1_HDRREGS_BASE + 0x0010)
#define RPPX1_RPP_HDR_OUT_IF_OFF_REG		(RPPX1_HDRREGS_BASE + 0x0014)

#define RPPX1_RPP_HDR_SAFETY_ACCESS_PROTECTION_REG	(RPPX1_HDRREGS_BASE + 0x0018)
#define RPPX1_RPP_HDR_SAFETY_ACCESS_PROTECTION_ENABLE	1
#define RPPX1_RPP_HDR_SAFETY_ACCESS_PROTECTION_DISABLE	0

#define RPPX1_RPP_ISM				(RPPX1_HDR_IRQ_BASE + 0x00)
#define RPPX1_RPP_RIS				(RPPX1_HDR_IRQ_BASE + 0x04)
#define RPPX1_RPP_MIS				(RPPX1_HDR_IRQ_BASE + 0x08)
#define RPPX1_RPP_ISC				(RPPX1_HDR_IRQ_BASE + 0x0c)

/* RPP_OUT/MV_OUT Pipelines - Base Addresses */
#define RPPX1_GAMMA_OUT_BASE			0x0000 /* HV, MV */
#define RPPX1_IS_BASE				0x00c0 /* HV, MV */
#define RPPX1_CSM_BASE				0x0100 /* HV, MV */
#define RPPX1_OUT_IF_BASE			0x0200 /* HV, MV */
#define RPPX1_RPP_OUTREGS_BASE			0x02c0 /* HV, MV */
#define RPPX1_LUV_BASE				0x0300 /* MV */

/* PRE1/PRE2/POST Pipelines - Base Addresses */
#define RPPX1_ACQ_BASE				0x0080 /* PRE1, PRE2 */
#define RPPX1_BLS_BASE				0x0100 /* PRE1, PRE2 */
#define RPPX1_GAMMA_IN_BASE			0x0200 /* PRE1, PRE2 */
#define RPPX1_LSC_BASE				0x0400 /* PRE1, PRE2 */
#define RPPX1_AWB_GAIN_BASE			0x0500 /* PRE1, PRE2, POST */
#define RPPX1_DPCC_BASE				0x0600 /* PRE1, PRE2 */
#define RPPX1_DPF_BASE				0x0700 /* PRE1, PRE2 */
#define RPPX1_FILT_BASE				0x0800 /* POST */
#define RPPX1_CAC_BASE				0x0880 /* POST */
#define RPPX1_CCOR_BASE				0x0900 /* POST */
#define RPPX1_HIST_BASE				0x0a00 /* PRE1, PRE2, POST */
#define RPPX1_HIST256_BASE			0x0b00 /* PRE1 */
#define RPPX1_EXM_BASE				0x0c00 /* PRE1, PRE2 */
#define RPPX1_LTM_BASE				0x1000 /* POST */
#define RPPX1_LTM_MEAS_BASE			0x1200 /* POST */
#define RPPX1_WBMEAS_BASE			0x1700 /* POST */
#define RPPX1_BDRGB_BASE			0x1800 /* POST */
#define RPPX1_SHRP_BASE				0x1a00 /* POST */

/* Functional Safety Module Base Addresses */
#define RPPX1_FMU_BASE				0x0100

#define RPPX1_RPP_HDR_FMU_FSM_REG		(RPPX1_RPP_FUSA_BASE + RPPX1_FMU_BASE + 0x00)
#define RPPX1_RPP_HDR_FMU_FSM_FSM_IRQM_FAULT	BIT(23)
#define RPPX1_RPP_HDR_FMU_FSM_PRE2_SIZE_FAULT	BIT(20)
#define RPPX1_RPP_HDR_FMU_FSM_PRE2_TIME_FAULT	BIT(19)
#define RPPX1_RPP_HDR_FMU_FSM_PRE1_SIZE_FAULT	BIT(18)
#define RPPX1_RPP_HDR_FMU_FSM_PRE1_TIME_FAULT	BIT(17)
#define RPPX1_RPP_HDR_FMU_FSM_SIZE_FAULT	BIT(16)
#define RPPX1_RPP_HDR_FMU_FSM_TIME_FAULT	BIT(15)
#define RPPX1_RPP_HDR_FMU_FSM_MV_OUT_SIZE_FAULT	BIT(14)
#define RPPX1_RPP_HDR_FMU_FSM_MV_OUT_TIME_FAULT	BIT(13)
#define RPPX1_RPP_HDR_FMU_FSM_HV_OUT_SIZE_FAULT	BIT(12)
#define RPPX1_RPP_HDR_FMU_FSM_HV_OUT_TIME_FAULT	BIT(11)
#define RPPX1_RPP_HDR_FMU_FSM_MV_OUT_SIZE_ERR	BIT(10)
#define RPPX1_RPP_HDR_FMU_FSM_IS_OUT_SIZE_ERR	BIT(9)
#define RPPX1_RPP_HDR_FMU_FSM_PRE2_FIFO_OVFLW	BIT(7)
#define RPPX1_RPP_HDR_FMU_FSM_PRE1_FIFO_OVFLW	BIT(6)
#define RPPX1_RPP_HDR_FMU_FSM_PRE1_INFORM_SIZE	BIT(5)
#define RPPX1_RPP_HDR_FMU_FSM_PRE1_OUTFORM_SIZE	BIT(4)
#define RPPX1_RPP_HDR_FMU_FSM_PRE2_INFORM_SIZE	BIT(3)
#define RPPX1_RPP_HDR_FMU_FSM_PRE2_OUTFORM_SIZE	BIT(2)

#define RPPX1_RPP_HDR_FMU_RFS_REG		(RPPX1_RPP_FUSA_BASE + RPPX1_FMU_BASE + 0x04)
#define RPPX1_RPP_HDR_FMU_MFS_REG		(RPPX1_RPP_FUSA_BASE + RPPX1_FMU_BASE + 0x08)
#define RPPX1_RPP_HDR_FMU_FSC_REG		(RPPX1_RPP_FUSA_BASE + RPPX1_FMU_BASE + 0x0c)

void rppx1_write(struct rppx1 *rpp, u32 offset, u32 value)
{
	iowrite32(value, rpp->base + offset);
}

u32 rppx1_read(struct rppx1 *rpp, u32 offset)
{
	return ioread32(rpp->base + offset);
}

bool rppx1_interrupt(struct rppx1 *rpp, u32 *isc)
{
	u32 status, raw, fault;

	fault = rppx1_read(rpp, RPPX1_RPP_HDR_FMU_MFS_REG);
	if (fault) {
		dev_err(rpp->dev, "%s: fault 0x%08x\n", __func__, fault);
		rppx1_write(rpp, RPPX1_RPP_HDR_FMU_FSC_REG, fault);
	}

	/* Read raw interrupt status. */
	raw = rppx1_read(rpp, RPPX1_RPP_RIS);
	status = rppx1_read(rpp, RPPX1_RPP_MIS);

	/* Propagate the isc status. */
	if (isc)
		*isc = status | raw;

	/* Clear enabled interrupts */
	rppx1_write(rpp, RPPX1_RPP_ISC, status);

	return !!(status & RPPX1_IRQ_ID_OUT_FRAME);
}
EXPORT_SYMBOL_GPL(rppx1_interrupt);

void rppx1_destroy(struct rppx1 *rpp)
{
	kfree(rpp);
}
EXPORT_SYMBOL_GPL(rppx1_destroy);

/*
 * Allocate the private data structure and verify the hardware is present.
 */
struct rppx1 *rppx1_create(void __iomem *base, struct device *dev)
{
	struct rppx1 *rpp;
	u32 reg;

	/* Allocate library structure */
	rpp = kzalloc_obj(*rpp);
	if (!rpp)
		return NULL;

	rpp->base = base;
	rpp->dev = dev;

	/* Check communication with RPP and verify it truly is a X1. */
	reg = rppx1_read(rpp, RPPX1_RPP_HDRREGS_VERSION_REG);
	if (reg != 3) {
		dev_err(rpp->dev, "Unsupported HDR version (%u)\n", reg);
		rppx1_destroy(rpp);
		return NULL;
	}

	/* Probe the PRE1 pipeline. */
	if (rpp_module_probe(&rpp->pre1.acq, rpp, &rppx1_acq_ops,
			     RPPX1_RPP_MAIN_PRE1_BASE + RPPX1_ACQ_BASE) ||
	    rpp_module_probe(&rpp->pre1.bls, rpp, &rppx1_bls_ops,
			     RPPX1_RPP_MAIN_PRE1_BASE + RPPX1_BLS_BASE) ||
	    rpp_module_probe(&rpp->pre1.lin, rpp, &rppx1_lin_ops,
			     RPPX1_RPP_MAIN_PRE1_BASE + RPPX1_GAMMA_IN_BASE) ||
	    rpp_module_probe(&rpp->pre1.lsc, rpp, &rppx1_lsc_ops,
			     RPPX1_RPP_MAIN_PRE1_BASE + RPPX1_LSC_BASE) ||
	    rpp_module_probe(&rpp->pre1.awbg, rpp, &rppx1_awbg_ops,
			     RPPX1_RPP_MAIN_PRE1_BASE + RPPX1_AWB_GAIN_BASE) ||
	    rpp_module_probe(&rpp->pre1.dpcc, rpp, &rppx1_dpcc_ops,
			     RPPX1_RPP_MAIN_PRE1_BASE + RPPX1_DPCC_BASE) ||
	    rpp_module_probe(&rpp->pre1.bd, rpp, &rppx1_bd_ops,
			     RPPX1_RPP_MAIN_PRE1_BASE + RPPX1_DPF_BASE) ||
	    rpp_module_probe(&rpp->pre1.hist, rpp, &rppx1_hist_ops,
			     RPPX1_RPP_MAIN_PRE1_BASE + RPPX1_HIST_BASE) ||
	    rpp_module_probe(&rpp->pre1.hist256, rpp, &rppx1_hist256_ops,
			     RPPX1_RPP_MAIN_PRE1_BASE + RPPX1_HIST256_BASE) ||
	    rpp_module_probe(&rpp->pre1.exm, rpp, &rppx1_exm_ops,
			     RPPX1_RPP_MAIN_PRE1_BASE + RPPX1_EXM_BASE))
		goto err;

	/* Probe the PRE2 pipeline. */
	if (rpp_module_probe(&rpp->pre2.acq, rpp, &rppx1_acq_ops,
			     RPPX1_RPP_MAIN_PRE2_BASE + RPPX1_ACQ_BASE) ||
	    rpp_module_probe(&rpp->pre2.bls, rpp, &rppx1_bls_ops,
			     RPPX1_RPP_MAIN_PRE2_BASE + RPPX1_BLS_BASE) ||
	    rpp_module_probe(&rpp->pre2.lin, rpp, &rppx1_lin_ops,
			     RPPX1_RPP_MAIN_PRE2_BASE + RPPX1_GAMMA_IN_BASE) ||
	    rpp_module_probe(&rpp->pre2.lsc, rpp, &rppx1_lsc_ops,
			     RPPX1_RPP_MAIN_PRE2_BASE + RPPX1_LSC_BASE) ||
	    rpp_module_probe(&rpp->pre2.awbg, rpp, &rppx1_awbg_ops,
			     RPPX1_RPP_MAIN_PRE2_BASE + RPPX1_AWB_GAIN_BASE) ||
	    rpp_module_probe(&rpp->pre2.dpcc, rpp, &rppx1_dpcc_ops,
			     RPPX1_RPP_MAIN_PRE2_BASE + RPPX1_DPCC_BASE) ||
	    rpp_module_probe(&rpp->pre2.bd, rpp, &rppx1_bd_ops,
			     RPPX1_RPP_MAIN_PRE2_BASE + RPPX1_DPF_BASE) ||
	    rpp_module_probe(&rpp->pre2.hist, rpp, &rppx1_hist_ops,
			     RPPX1_RPP_MAIN_PRE2_BASE + RPPX1_HIST_BASE) ||
	    rpp_module_probe(&rpp->pre2.exm, rpp, &rppx1_exm_ops,
			     RPPX1_RPP_MAIN_PRE2_BASE + RPPX1_EXM_BASE))
		goto err;

	/* Probe the POST pipeline. */
	if (rpp_module_probe(&rpp->post.awbg, rpp, &rppx1_awbg_ops,
			     RPPX1_RPP_MAIN_POST_BASE + RPPX1_AWB_GAIN_BASE) ||
	    rpp_module_probe(&rpp->post.ccor, rpp, &rppx1_ccor_ops,
			     RPPX1_RPP_MAIN_POST_BASE + RPPX1_CCOR_BASE) ||
	    rpp_module_probe(&rpp->post.hist, rpp, &rppx1_hist_ops,
			     RPPX1_RPP_MAIN_POST_BASE + RPPX1_HIST_BASE) ||
	    rpp_module_probe(&rpp->post.db, rpp, &rppx1_db_ops,
			     RPPX1_RPP_MAIN_POST_BASE + RPPX1_FILT_BASE) ||
	    rpp_module_probe(&rpp->post.cac, rpp, &rppx1_cac_ops,
			     RPPX1_RPP_MAIN_POST_BASE + RPPX1_CAC_BASE) ||
	    rpp_module_probe(&rpp->post.ltm, rpp, &rppx1_ltm_ops,
			     RPPX1_RPP_MAIN_POST_BASE + RPPX1_LTM_BASE) ||
	    rpp_module_probe(&rpp->post.ltmmeas, rpp, &rppx1_ltmmeas_ops,
			     RPPX1_RPP_MAIN_POST_BASE + RPPX1_LTM_MEAS_BASE) ||
	    rpp_module_probe(&rpp->post.wbmeas, rpp, &rppx1_wbmeas_ops,
			     RPPX1_RPP_MAIN_POST_BASE + RPPX1_WBMEAS_BASE) ||
	    rpp_module_probe(&rpp->post.bdrgb, rpp, &rppx1_bdrgb_ops,
			     RPPX1_RPP_MAIN_POST_BASE + RPPX1_BDRGB_BASE) ||
	    rpp_module_probe(&rpp->post.shrp, rpp, &rppx1_shrp_ops,
			     RPPX1_RPP_MAIN_POST_BASE + RPPX1_SHRP_BASE))
		goto err;

	/* Probe the Human Vision pipeline. */
	if (rpp_module_probe(&rpp->hv.ga, rpp, &rppx1_ga_ops,
			     RPPX1_RPP_OUT_BASE + RPPX1_GAMMA_OUT_BASE) ||
	    rpp_module_probe(&rpp->hv.is, rpp, &rppx1_is_ops,
			     RPPX1_RPP_OUT_BASE + RPPX1_IS_BASE) ||
	    rpp_module_probe(&rpp->hv.ccor, rpp, &rppx1_ccor_csm_ops,
			     RPPX1_RPP_OUT_BASE + RPPX1_CSM_BASE) ||
	    rpp_module_probe(&rpp->hv.outif, rpp, &rppx1_outif_ops,
			     RPPX1_RPP_OUT_BASE + RPPX1_OUT_IF_BASE) ||
	    rpp_module_probe(&rpp->hv.outregs, rpp, &rppx1_outregs_ops,
			     RPPX1_RPP_OUT_BASE + RPPX1_RPP_OUTREGS_BASE))
		goto err;

	/* Probe the Machine Vision pipeline. */
	if (rpp_module_probe(&rpp->mv.ga, rpp, &rppx1_ga_ops,
			     RPPX1_RPP_MVOUT_BASE + RPPX1_GAMMA_OUT_BASE) ||
	    rpp_module_probe(&rpp->mv.is, rpp, &rppx1_is_ops,
			     RPPX1_RPP_MVOUT_BASE + RPPX1_IS_BASE) ||
	    rpp_module_probe(&rpp->mv.ccor, rpp, &rppx1_ccor_csm_ops,
			     RPPX1_RPP_MVOUT_BASE + RPPX1_CSM_BASE) ||
	    rpp_module_probe(&rpp->mv.outif, rpp, &rppx1_outif_ops,
			     RPPX1_RPP_MVOUT_BASE + RPPX1_OUT_IF_BASE) ||
	    rpp_module_probe(&rpp->mv.outregs, rpp, &rppx1_outregs_ops,
			     RPPX1_RPP_MVOUT_BASE + RPPX1_RPP_OUTREGS_BASE) ||
	    rpp_module_probe(&rpp->mv.xyz2luv, rpp, &rppx1_xyz2luv_ops,
			     RPPX1_RPP_MVOUT_BASE + RPPX1_LUV_BASE))
		goto err;

	/* Probe the standalone Radiance Mapping modules. */
	if (rpp_module_probe(&rpp->rmap, rpp, &rppx1_rmap_ops,
			     RPPX1_RPP_RMAP_BASE) ||
	    rpp_module_probe(&rpp->rmapmeas, rpp, &rppx1_rmapmeas_ops,
			     RPPX1_RPP_RMAP_MEAS_BASE))
		goto err;

	return rpp;
err:
	rppx1_destroy(rpp);

	return NULL;
}
EXPORT_SYMBOL_GPL(rppx1_create);

int rppx1_start(struct rppx1 *rpp,
		const struct v4l2_mbus_framefmt *input,
		const struct v4l2_mbus_framefmt *hv,
		const struct v4l2_mbus_framefmt *mv)
{
	if (rpp_module_call(&rpp->pre1.acq, start, input) ||
	    rpp_module_call(&rpp->pre1.bls, start, input) ||
	    rpp_module_call(&rpp->pre1.lin, start, input) ||
	    rpp_module_call(&rpp->pre1.lsc, start, input) ||
	    rpp_module_call(&rpp->pre1.awbg, start, input) ||
	    rpp_module_call(&rpp->pre1.dpcc, start, input) ||
	    rpp_module_call(&rpp->pre1.bd, start, input) ||
	    rpp_module_call(&rpp->pre1.hist, start, input) ||
	    rpp_module_call(&rpp->pre1.exm, start, input) ||
	    rpp_module_call(&rpp->pre1.hist256, start, input))
		return -EINVAL;

	if (rpp_module_call(&rpp->rmap, start, NULL) ||
	    rpp_module_call(&rpp->rmapmeas, start, NULL))
		return -EINVAL;

	if (rpp_module_call(&rpp->post.awbg, start, input) ||
	    rpp_module_call(&rpp->post.db, start, input) ||
	    rpp_module_call(&rpp->post.cac, start, input) ||
	    rpp_module_call(&rpp->post.ccor, start, input) ||
	    rpp_module_call(&rpp->post.ltm, start, input) ||
	    rpp_module_call(&rpp->post.bdrgb, start, input) ||
	    rpp_module_call(&rpp->post.shrp, start, input) ||
	    rpp_module_call(&rpp->post.ltmmeas, start, input) ||
	    rpp_module_call(&rpp->post.wbmeas, start, input) ||
	    rpp_module_call(&rpp->post.hist, start, input))
		return -EINVAL;

	if (hv && (rpp_module_call(&rpp->hv.ga, start, hv) ||
		   rpp_module_call(&rpp->hv.ccor, start, hv) ||
		   rpp_module_call(&rpp->hv.outregs, start, hv) ||
		   rpp_module_call(&rpp->hv.is, start, hv) ||
		   rpp_module_call(&rpp->hv.outif, start, hv)))
		return -EINVAL;

	if (mv && (rpp_module_call(&rpp->mv.ga, start, mv) ||
		   rpp_module_call(&rpp->mv.ccor, start, mv) ||
		   rpp_module_call(&rpp->mv.xyz2luv, start, mv) ||
		   rpp_module_call(&rpp->mv.outregs, start, mv) ||
		   rpp_module_call(&rpp->mv.is, start, mv) ||
		   rpp_module_call(&rpp->mv.outif, start, mv)))
		return -EINVAL;

	/* Immediate update for shadows. */
	rppx1_write(rpp, RPPX1_RPP_HDR_UPD_REG, RPPX1_RPP_HDR_UPD_REGS_CFG_UPD);

	/* Clear fault interrupts. */
	rppx1_write(rpp, RPPX1_RPP_HDR_SAFETY_ACCESS_PROTECTION_REG,
		    RPPX1_RPP_HDR_SAFETY_ACCESS_PROTECTION_ENABLE);
	rppx1_write(rpp, RPPX1_RPP_HDR_FMU_FSM_REG,
		    RPPX1_RPP_HDR_FMU_FSM_PRE2_FIFO_OVFLW |
		    RPPX1_RPP_HDR_FMU_FSM_PRE1_FIFO_OVFLW);
	rppx1_write(rpp, RPPX1_RPP_HDR_FMU_FSC_REG,
		    rppx1_read(rpp, RPPX1_RPP_HDR_FMU_MFS_REG));
	rppx1_write(rpp, RPPX1_RPP_HDR_SAFETY_ACCESS_PROTECTION_REG,
		    RPPX1_RPP_HDR_SAFETY_ACCESS_PROTECTION_DISABLE);

	/* Set interrupt mask. */
	rppx1_write(rpp, RPPX1_RPP_ISM, RPPX1_IRQ_ID_OUT_FRAME);

	/* Immediate commit update for shadows. */
	rppx1_write(rpp, RPPX1_RPP_HDR_UPD_REG, RPPX1_RPP_HDR_UPD_REGS_CFG_UPD);

	/* Then for operation update shadows with picture synchronization. */
	rppx1_write(rpp, RPPX1_RPP_HDR_UPD_REG, RPPX1_RPP_HDR_UPD_REGS_GEN_CFG_UPD);

	/* Clear any pending interrupts. */
	rppx1_interrupt(rpp, NULL);

	/* Enable input formatters. */
	rppx1_write(rpp, RPPX1_RPP_HDR_INFORM_ENABLE_REG,
		    RPPX1_RPP_HDR_INFORM_ENABLE_ENABLE);

	return 0;
}
EXPORT_SYMBOL_GPL(rppx1_start);

int rppx1_stop(struct rppx1 *rpp)
{
	/* Disable input formatters. */
	rppx1_write(rpp, RPPX1_RPP_HDR_INFORM_ENABLE_REG,
		    RPPX1_RPP_HDR_INFORM_ENABLE_DISABLE);

	/* Clear any pending interrupts. */
	rppx1_interrupt(rpp, NULL);

	return 0;
}
EXPORT_SYMBOL_GPL(rppx1_stop);

MODULE_AUTHOR("Niklas Söderlund <niklas.soderlund@ragnatech.se>");
MODULE_DESCRIPTION("Dreamchip HDR RPPX1 support library");
MODULE_LICENSE("GPL");
