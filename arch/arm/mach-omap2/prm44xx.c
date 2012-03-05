/*
 * OMAP4 PRM module functions
 *
 * Copyright (C) 2011 Texas Instruments, Inc.
 * Copyright (C) 2010 Nokia Corporation
 * Benoît Cousson
 * Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/io.h>

#include "common.h"
#include <plat/cpu.h>
#include <plat/prcm.h>

#include "vp.h"
#include "prm44xx.h"
#include "prm-regbits-44xx.h"
#include "prcm44xx.h"
#include "prminst44xx.h"

static const struct omap_prcm_irq omap4_prcm_irqs[] = {
	OMAP_PRCM_IRQ("wkup",   0,      0),
	OMAP_PRCM_IRQ("io",     9,      1),
};

static struct omap_prcm_irq_setup omap4_prcm_irq_setup = {
	.ack			= OMAP4_PRM_IRQSTATUS_MPU_OFFSET,
	.mask			= OMAP4_PRM_IRQENABLE_MPU_OFFSET,
	.nr_regs		= 2,
	.irqs			= omap4_prcm_irqs,
	.nr_irqs		= ARRAY_SIZE(omap4_prcm_irqs),
	.irq			= OMAP44XX_IRQ_PRCM,
	.read_pending_irqs	= &omap44xx_prm_read_pending_irqs,
	.ocp_barrier		= &omap44xx_prm_ocp_barrier,
	.save_and_clear_irqen	= &omap44xx_prm_save_and_clear_irqen,
	.restore_irqen		= &omap44xx_prm_restore_irqen,
};

/* PRM low-level functions */

/* Read a register in a CM/PRM instance in the PRM module */
u32 omap4_prm_read_inst_reg(s16 inst, u16 reg)
{
	return __raw_readl(OMAP44XX_PRM_REGADDR(inst, reg));
}

/* Write into a register in a CM/PRM instance in the PRM module */
void omap4_prm_write_inst_reg(u32 val, s16 inst, u16 reg)
{
	__raw_writel(val, OMAP44XX_PRM_REGADDR(inst, reg));
}

/* Read-modify-write a register in a PRM module. Caller must lock */
u32 omap4_prm_rmw_inst_reg_bits(u32 mask, u32 bits, s16 inst, s16 reg)
{
	u32 v;

	v = omap4_prm_read_inst_reg(inst, reg);
	v &= ~mask;
	v |= bits;
	omap4_prm_write_inst_reg(v, inst, reg);

	return v;
}

/* PRM VP */

/*
 * struct omap4_vp - OMAP4 VP register access description.
 * @irqstatus_mpu: offset to IRQSTATUS_MPU register for VP
 * @tranxdone_status: VP_TRANXDONE_ST bitmask in PRM_IRQSTATUS_MPU reg
 */
struct omap4_vp {
	u32 irqstatus_mpu;
	u32 tranxdone_status;
};

static struct omap4_vp omap4_vp[] = {
	[OMAP4_VP_VDD_MPU_ID] = {
		.irqstatus_mpu = OMAP4_PRM_IRQSTATUS_MPU_2_OFFSET,
		.tranxdone_status = OMAP4430_VP_MPU_TRANXDONE_ST_MASK,
	},
	[OMAP4_VP_VDD_IVA_ID] = {
		.irqstatus_mpu = OMAP4_PRM_IRQSTATUS_MPU_OFFSET,
		.tranxdone_status = OMAP4430_VP_IVA_TRANXDONE_ST_MASK,
	},
	[OMAP4_VP_VDD_CORE_ID] = {
		.irqstatus_mpu = OMAP4_PRM_IRQSTATUS_MPU_OFFSET,
		.tranxdone_status = OMAP4430_VP_CORE_TRANXDONE_ST_MASK,
	},
};

u32 omap4_prm_vp_check_txdone(u8 vp_id)
{
	struct omap4_vp *vp = &omap4_vp[vp_id];
	u32 irqstatus;

	irqstatus = omap4_prminst_read_inst_reg(OMAP4430_PRM_PARTITION,
						OMAP4430_PRM_OCP_SOCKET_INST,
						vp->irqstatus_mpu);
	return irqstatus & vp->tranxdone_status;
}

void omap4_prm_vp_clear_txdone(u8 vp_id)
{
	struct omap4_vp *vp = &omap4_vp[vp_id];

	omap4_prminst_write_inst_reg(vp->tranxdone_status,
				     OMAP4430_PRM_PARTITION,
				     OMAP4430_PRM_OCP_SOCKET_INST,
				     vp->irqstatus_mpu);
};

u32 omap4_prm_vcvp_read(u8 offset)
{
	return omap4_prminst_read_inst_reg(OMAP4430_PRM_PARTITION,
					   OMAP4430_PRM_DEVICE_INST, offset);
}

void omap4_prm_vcvp_write(u32 val, u8 offset)
{
	omap4_prminst_write_inst_reg(val, OMAP4430_PRM_PARTITION,
				     OMAP4430_PRM_DEVICE_INST, offset);
}

u32 omap4_prm_vcvp_rmw(u32 mask, u32 bits, u8 offset)
{
	return omap4_prminst_rmw_inst_reg_bits(mask, bits,
					       OMAP4430_PRM_PARTITION,
					       OMAP4430_PRM_DEVICE_INST,
					       offset);
}

static inline u32 _read_pending_irq_reg(u16 irqen_offs, u16 irqst_offs)
{
	u32 mask, st;

	/* XXX read mask from RAM? */
	mask = omap4_prm_read_inst_reg(OMAP4430_PRM_DEVICE_INST, irqen_offs);
	st = omap4_prm_read_inst_reg(OMAP4430_PRM_DEVICE_INST, irqst_offs);

	return mask & st;
}

/**
 * omap44xx_prm_read_pending_irqs - read pending PRM MPU IRQs into @events
 * @events: ptr to two consecutive u32s, preallocated by caller
 *
 * Read PRM_IRQSTATUS_MPU* bits, AND'ed with the currently-enabled PRM
 * MPU IRQs, and store the result into the two u32s pointed to by @events.
 * No return value.
 */
void omap44xx_prm_read_pending_irqs(unsigned long *events)
{
	events[0] = _read_pending_irq_reg(OMAP4_PRM_IRQENABLE_MPU_OFFSET,
					  OMAP4_PRM_IRQSTATUS_MPU_OFFSET);

	events[1] = _read_pending_irq_reg(OMAP4_PRM_IRQENABLE_MPU_2_OFFSET,
					  OMAP4_PRM_IRQSTATUS_MPU_2_OFFSET);
}

/**
 * omap44xx_prm_ocp_barrier - force buffered MPU writes to the PRM to complete
 *
 * Force any buffered writes to the PRM IP block to complete.  Needed
 * by the PRM IRQ handler, which reads and writes directly to the IP
 * block, to avoid race conditions after acknowledging or clearing IRQ
 * bits.  No return value.
 */
void omap44xx_prm_ocp_barrier(void)
{
	omap4_prm_read_inst_reg(OMAP4430_PRM_DEVICE_INST,
				OMAP4_REVISION_PRM_OFFSET);
}

/**
 * omap44xx_prm_save_and_clear_irqen - save/clear PRM_IRQENABLE_MPU* regs
 * @saved_mask: ptr to a u32 array to save IRQENABLE bits
 *
 * Save the PRM_IRQENABLE_MPU and PRM_IRQENABLE_MPU_2 registers to
 * @saved_mask.  @saved_mask must be allocated by the caller.
 * Intended to be used in the PRM interrupt handler suspend callback.
 * The OCP barrier is needed to ensure the write to disable PRM
 * interrupts reaches the PRM before returning; otherwise, spurious
 * interrupts might occur.  No return value.
 */
void omap44xx_prm_save_and_clear_irqen(u32 *saved_mask)
{
	saved_mask[0] =
		omap4_prm_read_inst_reg(OMAP4430_PRM_DEVICE_INST,
					OMAP4_PRM_IRQSTATUS_MPU_OFFSET);
	saved_mask[1] =
		omap4_prm_read_inst_reg(OMAP4430_PRM_DEVICE_INST,
					OMAP4_PRM_IRQSTATUS_MPU_2_OFFSET);

	omap4_prm_write_inst_reg(0, OMAP4430_PRM_DEVICE_INST,
				 OMAP4_PRM_IRQENABLE_MPU_OFFSET);
	omap4_prm_write_inst_reg(0, OMAP4430_PRM_DEVICE_INST,
				 OMAP4_PRM_IRQENABLE_MPU_2_OFFSET);

	/* OCP barrier */
	omap4_prm_read_inst_reg(OMAP4430_PRM_DEVICE_INST,
				OMAP4_REVISION_PRM_OFFSET);
}

/**
 * omap44xx_prm_restore_irqen - set PRM_IRQENABLE_MPU* registers from args
 * @saved_mask: ptr to a u32 array of IRQENABLE bits saved previously
 *
 * Restore the PRM_IRQENABLE_MPU and PRM_IRQENABLE_MPU_2 registers from
 * @saved_mask.  Intended to be used in the PRM interrupt handler resume
 * callback to restore values saved by omap44xx_prm_save_and_clear_irqen().
 * No OCP barrier should be needed here; any pending PRM interrupts will fire
 * once the writes reach the PRM.  No return value.
 */
void omap44xx_prm_restore_irqen(u32 *saved_mask)
{
	omap4_prm_write_inst_reg(saved_mask[0], OMAP4430_PRM_DEVICE_INST,
				 OMAP4_PRM_IRQENABLE_MPU_OFFSET);
	omap4_prm_write_inst_reg(saved_mask[1], OMAP4430_PRM_DEVICE_INST,
				 OMAP4_PRM_IRQENABLE_MPU_2_OFFSET);
}

static int __init omap4xxx_prcm_init(void)
{
	if (cpu_is_omap44xx())
		return omap_prcm_register_chain_handler(&omap4_prcm_irq_setup);
	return 0;
}
subsys_initcall(omap4xxx_prcm_init);
