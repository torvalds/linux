/*
 * OMAP4 PRM module functions
 *
 * Copyright (C) 2011-2012 Texas Instruments, Inc.
 * Copyright (C) 2010 Nokia Corporation
 * Benoît Cousson
 * Paul Walmsley
 * Rajendra Nayak <rnayak@ti.com>
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
#include <linux/of_irq.h>
#include <linux/of.h>

#include "soc.h"
#include "iomap.h"
#include "common.h"
#include "vp.h"
#include "prm44xx.h"
#include "prcm43xx.h"
#include "prm-regbits-44xx.h"
#include "prcm44xx.h"
#include "prminst44xx.h"
#include "powerdomain.h"

/* Static data */

static void omap44xx_prm_read_pending_irqs(unsigned long *events);
static void omap44xx_prm_ocp_barrier(void);
static void omap44xx_prm_save_and_clear_irqen(u32 *saved_mask);
static void omap44xx_prm_restore_irqen(u32 *saved_mask);
static void omap44xx_prm_reconfigure_io_chain(void);

static const struct omap_prcm_irq omap4_prcm_irqs[] = {
	OMAP_PRCM_IRQ("io",     9,      1),
};

static struct omap_prcm_irq_setup omap4_prcm_irq_setup = {
	.ack			= OMAP4_PRM_IRQSTATUS_MPU_OFFSET,
	.mask			= OMAP4_PRM_IRQENABLE_MPU_OFFSET,
	.pm_ctrl		= OMAP4_PRM_IO_PMCTRL_OFFSET,
	.nr_regs		= 2,
	.irqs			= omap4_prcm_irqs,
	.nr_irqs		= ARRAY_SIZE(omap4_prcm_irqs),
	.irq			= 11 + OMAP44XX_IRQ_GIC_START,
	.xlate_irq		= omap4_xlate_irq,
	.read_pending_irqs	= &omap44xx_prm_read_pending_irqs,
	.ocp_barrier		= &omap44xx_prm_ocp_barrier,
	.save_and_clear_irqen	= &omap44xx_prm_save_and_clear_irqen,
	.restore_irqen		= &omap44xx_prm_restore_irqen,
	.reconfigure_io_chain	= &omap44xx_prm_reconfigure_io_chain,
};

/*
 * omap44xx_prm_reset_src_map - map from bits in the PRM_RSTST
 *   hardware register (which are specific to OMAP44xx SoCs) to reset
 *   source ID bit shifts (which is an OMAP SoC-independent
 *   enumeration)
 */
static struct prm_reset_src_map omap44xx_prm_reset_src_map[] = {
	{ OMAP4430_GLOBAL_WARM_SW_RST_SHIFT,
	  OMAP_GLOBAL_WARM_RST_SRC_ID_SHIFT },
	{ OMAP4430_GLOBAL_COLD_RST_SHIFT,
	  OMAP_GLOBAL_COLD_RST_SRC_ID_SHIFT },
	{ OMAP4430_MPU_SECURITY_VIOL_RST_SHIFT,
	  OMAP_SECU_VIOL_RST_SRC_ID_SHIFT },
	{ OMAP4430_MPU_WDT_RST_SHIFT, OMAP_MPU_WD_RST_SRC_ID_SHIFT },
	{ OMAP4430_SECURE_WDT_RST_SHIFT, OMAP_SECU_WD_RST_SRC_ID_SHIFT },
	{ OMAP4430_EXTERNAL_WARM_RST_SHIFT, OMAP_EXTWARM_RST_SRC_ID_SHIFT },
	{ OMAP4430_VDD_MPU_VOLT_MGR_RST_SHIFT,
	  OMAP_VDD_MPU_VM_RST_SRC_ID_SHIFT },
	{ OMAP4430_VDD_IVA_VOLT_MGR_RST_SHIFT,
	  OMAP_VDD_IVA_VM_RST_SRC_ID_SHIFT },
	{ OMAP4430_VDD_CORE_VOLT_MGR_RST_SHIFT,
	  OMAP_VDD_CORE_VM_RST_SRC_ID_SHIFT },
	{ OMAP4430_ICEPICK_RST_SHIFT, OMAP_ICEPICK_RST_SRC_ID_SHIFT },
	{ OMAP4430_C2C_RST_SHIFT, OMAP_C2C_RST_SRC_ID_SHIFT },
	{ -1, -1 },
};

/* PRM low-level functions */

/* Read a register in a CM/PRM instance in the PRM module */
static u32 omap4_prm_read_inst_reg(s16 inst, u16 reg)
{
	return readl_relaxed(prm_base.va + inst + reg);
}

/* Write into a register in a CM/PRM instance in the PRM module */
static void omap4_prm_write_inst_reg(u32 val, s16 inst, u16 reg)
{
	writel_relaxed(val, prm_base.va + inst + reg);
}

/* Read-modify-write a register in a PRM module. Caller must lock */
static u32 omap4_prm_rmw_inst_reg_bits(u32 mask, u32 bits, s16 inst, s16 reg)
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

static u32 omap4_prm_vp_check_txdone(u8 vp_id)
{
	struct omap4_vp *vp = &omap4_vp[vp_id];
	u32 irqstatus;

	irqstatus = omap4_prminst_read_inst_reg(OMAP4430_PRM_PARTITION,
						OMAP4430_PRM_OCP_SOCKET_INST,
						vp->irqstatus_mpu);
	return irqstatus & vp->tranxdone_status;
}

static void omap4_prm_vp_clear_txdone(u8 vp_id)
{
	struct omap4_vp *vp = &omap4_vp[vp_id];

	omap4_prminst_write_inst_reg(vp->tranxdone_status,
				     OMAP4430_PRM_PARTITION,
				     OMAP4430_PRM_OCP_SOCKET_INST,
				     vp->irqstatus_mpu);
};

u32 omap4_prm_vcvp_read(u8 offset)
{
	s32 inst = omap4_prmst_get_prm_dev_inst();

	if (inst == PRM_INSTANCE_UNKNOWN)
		return 0;

	return omap4_prminst_read_inst_reg(OMAP4430_PRM_PARTITION,
					   inst, offset);
}

void omap4_prm_vcvp_write(u32 val, u8 offset)
{
	s32 inst = omap4_prmst_get_prm_dev_inst();

	if (inst == PRM_INSTANCE_UNKNOWN)
		return;

	omap4_prminst_write_inst_reg(val, OMAP4430_PRM_PARTITION,
				     inst, offset);
}

u32 omap4_prm_vcvp_rmw(u32 mask, u32 bits, u8 offset)
{
	s32 inst = omap4_prmst_get_prm_dev_inst();

	if (inst == PRM_INSTANCE_UNKNOWN)
		return 0;

	return omap4_prminst_rmw_inst_reg_bits(mask, bits,
					       OMAP4430_PRM_PARTITION,
					       inst,
					       offset);
}

static inline u32 _read_pending_irq_reg(u16 irqen_offs, u16 irqst_offs)
{
	u32 mask, st;

	/* XXX read mask from RAM? */
	mask = omap4_prm_read_inst_reg(OMAP4430_PRM_OCP_SOCKET_INST,
				       irqen_offs);
	st = omap4_prm_read_inst_reg(OMAP4430_PRM_OCP_SOCKET_INST, irqst_offs);

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
static void omap44xx_prm_read_pending_irqs(unsigned long *events)
{
	int i;

	for (i = 0; i < omap4_prcm_irq_setup.nr_regs; i++)
		events[i] = _read_pending_irq_reg(omap4_prcm_irq_setup.mask +
				i * 4, omap4_prcm_irq_setup.ack + i * 4);
}

/**
 * omap44xx_prm_ocp_barrier - force buffered MPU writes to the PRM to complete
 *
 * Force any buffered writes to the PRM IP block to complete.  Needed
 * by the PRM IRQ handler, which reads and writes directly to the IP
 * block, to avoid race conditions after acknowledging or clearing IRQ
 * bits.  No return value.
 */
static void omap44xx_prm_ocp_barrier(void)
{
	omap4_prm_read_inst_reg(OMAP4430_PRM_OCP_SOCKET_INST,
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
static void omap44xx_prm_save_and_clear_irqen(u32 *saved_mask)
{
	int i;
	u16 reg;

	for (i = 0; i < omap4_prcm_irq_setup.nr_regs; i++) {
		reg = omap4_prcm_irq_setup.mask + i * 4;

		saved_mask[i] =
			omap4_prm_read_inst_reg(OMAP4430_PRM_OCP_SOCKET_INST,
						reg);
		omap4_prm_write_inst_reg(0, OMAP4430_PRM_OCP_SOCKET_INST, reg);
	}

	/* OCP barrier */
	omap4_prm_read_inst_reg(OMAP4430_PRM_OCP_SOCKET_INST,
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
static void omap44xx_prm_restore_irqen(u32 *saved_mask)
{
	int i;

	for (i = 0; i < omap4_prcm_irq_setup.nr_regs; i++)
		omap4_prm_write_inst_reg(saved_mask[i],
					 OMAP4430_PRM_OCP_SOCKET_INST,
					 omap4_prcm_irq_setup.mask + i * 4);
}

/**
 * omap44xx_prm_reconfigure_io_chain - clear latches and reconfigure I/O chain
 *
 * Clear any previously-latched I/O wakeup events and ensure that the
 * I/O wakeup gates are aligned with the current mux settings.  Works
 * by asserting WUCLKIN, waiting for WUCLKOUT to be asserted, and then
 * deasserting WUCLKIN and waiting for WUCLKOUT to be deasserted.
 * No return value. XXX Are the final two steps necessary?
 */
static void omap44xx_prm_reconfigure_io_chain(void)
{
	int i = 0;
	s32 inst = omap4_prmst_get_prm_dev_inst();

	if (inst == PRM_INSTANCE_UNKNOWN)
		return;

	/* Trigger WUCLKIN enable */
	omap4_prm_rmw_inst_reg_bits(OMAP4430_WUCLK_CTRL_MASK,
				    OMAP4430_WUCLK_CTRL_MASK,
				    inst,
				    omap4_prcm_irq_setup.pm_ctrl);
	omap_test_timeout(
		(((omap4_prm_read_inst_reg(inst,
					   omap4_prcm_irq_setup.pm_ctrl) &
		   OMAP4430_WUCLK_STATUS_MASK) >>
		  OMAP4430_WUCLK_STATUS_SHIFT) == 1),
		MAX_IOPAD_LATCH_TIME, i);
	if (i == MAX_IOPAD_LATCH_TIME)
		pr_warn("PRM: I/O chain clock line assertion timed out\n");

	/* Trigger WUCLKIN disable */
	omap4_prm_rmw_inst_reg_bits(OMAP4430_WUCLK_CTRL_MASK, 0x0,
				    inst,
				    omap4_prcm_irq_setup.pm_ctrl);
	omap_test_timeout(
		(((omap4_prm_read_inst_reg(inst,
					   omap4_prcm_irq_setup.pm_ctrl) &
		   OMAP4430_WUCLK_STATUS_MASK) >>
		  OMAP4430_WUCLK_STATUS_SHIFT) == 0),
		MAX_IOPAD_LATCH_TIME, i);
	if (i == MAX_IOPAD_LATCH_TIME)
		pr_warn("PRM: I/O chain clock line deassertion timed out\n");

	return;
}

/**
 * omap44xx_prm_enable_io_wakeup - enable wakeup events from I/O wakeup latches
 *
 * Activates the I/O wakeup event latches and allows events logged by
 * those latches to signal a wakeup event to the PRCM.  For I/O wakeups
 * to occur, WAKEUPENABLE bits must be set in the pad mux registers, and
 * omap44xx_prm_reconfigure_io_chain() must be called.  No return value.
 */
static void __init omap44xx_prm_enable_io_wakeup(void)
{
	s32 inst = omap4_prmst_get_prm_dev_inst();

	if (inst == PRM_INSTANCE_UNKNOWN)
		return;

	omap4_prm_rmw_inst_reg_bits(OMAP4430_GLOBAL_WUEN_MASK,
				    OMAP4430_GLOBAL_WUEN_MASK,
				    inst,
				    omap4_prcm_irq_setup.pm_ctrl);
}

/**
 * omap44xx_prm_read_reset_sources - return the last SoC reset source
 *
 * Return a u32 representing the last reset sources of the SoC.  The
 * returned reset source bits are standardized across OMAP SoCs.
 */
static u32 omap44xx_prm_read_reset_sources(void)
{
	struct prm_reset_src_map *p;
	u32 r = 0;
	u32 v;
	s32 inst = omap4_prmst_get_prm_dev_inst();

	if (inst == PRM_INSTANCE_UNKNOWN)
		return 0;


	v = omap4_prm_read_inst_reg(inst,
				    OMAP4_RM_RSTST);

	p = omap44xx_prm_reset_src_map;
	while (p->reg_shift >= 0 && p->std_shift >= 0) {
		if (v & (1 << p->reg_shift))
			r |= 1 << p->std_shift;
		p++;
	}

	return r;
}

/**
 * omap44xx_prm_was_any_context_lost_old - was module hardware context lost?
 * @part: PRM partition ID (e.g., OMAP4430_PRM_PARTITION)
 * @inst: PRM instance offset (e.g., OMAP4430_PRM_MPU_INST)
 * @idx: CONTEXT register offset
 *
 * Return 1 if any bits were set in the *_CONTEXT_* register
 * identified by (@part, @inst, @idx), which means that some context
 * was lost for that module; otherwise, return 0.
 */
static bool omap44xx_prm_was_any_context_lost_old(u8 part, s16 inst, u16 idx)
{
	return (omap4_prminst_read_inst_reg(part, inst, idx)) ? 1 : 0;
}

/**
 * omap44xx_prm_clear_context_lost_flags_old - clear context loss flags
 * @part: PRM partition ID (e.g., OMAP4430_PRM_PARTITION)
 * @inst: PRM instance offset (e.g., OMAP4430_PRM_MPU_INST)
 * @idx: CONTEXT register offset
 *
 * Clear hardware context loss bits for the module identified by
 * (@part, @inst, @idx).  No return value.  XXX Writes to reserved bits;
 * is there a way to avoid this?
 */
static void omap44xx_prm_clear_context_loss_flags_old(u8 part, s16 inst,
						      u16 idx)
{
	omap4_prminst_write_inst_reg(0xffffffff, part, inst, idx);
}

/* Powerdomain low-level functions */

static int omap4_pwrdm_set_next_pwrst(struct powerdomain *pwrdm, u8 pwrst)
{
	omap4_prminst_rmw_inst_reg_bits(OMAP_POWERSTATE_MASK,
					(pwrst << OMAP_POWERSTATE_SHIFT),
					pwrdm->prcm_partition,
					pwrdm->prcm_offs, OMAP4_PM_PWSTCTRL);
	return 0;
}

static int omap4_pwrdm_read_next_pwrst(struct powerdomain *pwrdm)
{
	u32 v;

	v = omap4_prminst_read_inst_reg(pwrdm->prcm_partition, pwrdm->prcm_offs,
					OMAP4_PM_PWSTCTRL);
	v &= OMAP_POWERSTATE_MASK;
	v >>= OMAP_POWERSTATE_SHIFT;

	return v;
}

static int omap4_pwrdm_read_pwrst(struct powerdomain *pwrdm)
{
	u32 v;

	v = omap4_prminst_read_inst_reg(pwrdm->prcm_partition, pwrdm->prcm_offs,
					OMAP4_PM_PWSTST);
	v &= OMAP_POWERSTATEST_MASK;
	v >>= OMAP_POWERSTATEST_SHIFT;

	return v;
}

static int omap4_pwrdm_read_prev_pwrst(struct powerdomain *pwrdm)
{
	u32 v;

	v = omap4_prminst_read_inst_reg(pwrdm->prcm_partition, pwrdm->prcm_offs,
					OMAP4_PM_PWSTST);
	v &= OMAP4430_LASTPOWERSTATEENTERED_MASK;
	v >>= OMAP4430_LASTPOWERSTATEENTERED_SHIFT;

	return v;
}

static int omap4_pwrdm_set_lowpwrstchange(struct powerdomain *pwrdm)
{
	omap4_prminst_rmw_inst_reg_bits(OMAP4430_LOWPOWERSTATECHANGE_MASK,
					(1 << OMAP4430_LOWPOWERSTATECHANGE_SHIFT),
					pwrdm->prcm_partition,
					pwrdm->prcm_offs, OMAP4_PM_PWSTCTRL);
	return 0;
}

static int omap4_pwrdm_clear_all_prev_pwrst(struct powerdomain *pwrdm)
{
	omap4_prminst_rmw_inst_reg_bits(OMAP4430_LASTPOWERSTATEENTERED_MASK,
					OMAP4430_LASTPOWERSTATEENTERED_MASK,
					pwrdm->prcm_partition,
					pwrdm->prcm_offs, OMAP4_PM_PWSTST);
	return 0;
}

static int omap4_pwrdm_set_logic_retst(struct powerdomain *pwrdm, u8 pwrst)
{
	u32 v;

	v = pwrst << __ffs(OMAP4430_LOGICRETSTATE_MASK);
	omap4_prminst_rmw_inst_reg_bits(OMAP4430_LOGICRETSTATE_MASK, v,
					pwrdm->prcm_partition, pwrdm->prcm_offs,
					OMAP4_PM_PWSTCTRL);

	return 0;
}

static int omap4_pwrdm_set_mem_onst(struct powerdomain *pwrdm, u8 bank,
				    u8 pwrst)
{
	u32 m;

	m = omap2_pwrdm_get_mem_bank_onstate_mask(bank);

	omap4_prminst_rmw_inst_reg_bits(m, (pwrst << __ffs(m)),
					pwrdm->prcm_partition, pwrdm->prcm_offs,
					OMAP4_PM_PWSTCTRL);

	return 0;
}

static int omap4_pwrdm_set_mem_retst(struct powerdomain *pwrdm, u8 bank,
				     u8 pwrst)
{
	u32 m;

	m = omap2_pwrdm_get_mem_bank_retst_mask(bank);

	omap4_prminst_rmw_inst_reg_bits(m, (pwrst << __ffs(m)),
					pwrdm->prcm_partition, pwrdm->prcm_offs,
					OMAP4_PM_PWSTCTRL);

	return 0;
}

static int omap4_pwrdm_read_logic_pwrst(struct powerdomain *pwrdm)
{
	u32 v;

	v = omap4_prminst_read_inst_reg(pwrdm->prcm_partition, pwrdm->prcm_offs,
					OMAP4_PM_PWSTST);
	v &= OMAP4430_LOGICSTATEST_MASK;
	v >>= OMAP4430_LOGICSTATEST_SHIFT;

	return v;
}

static int omap4_pwrdm_read_logic_retst(struct powerdomain *pwrdm)
{
	u32 v;

	v = omap4_prminst_read_inst_reg(pwrdm->prcm_partition, pwrdm->prcm_offs,
					OMAP4_PM_PWSTCTRL);
	v &= OMAP4430_LOGICRETSTATE_MASK;
	v >>= OMAP4430_LOGICRETSTATE_SHIFT;

	return v;
}

/**
 * omap4_pwrdm_read_prev_logic_pwrst - read the previous logic powerstate
 * @pwrdm: struct powerdomain * to read the state for
 *
 * Reads the previous logic powerstate for a powerdomain. This
 * function must determine the previous logic powerstate by first
 * checking the previous powerstate for the domain. If that was OFF,
 * then logic has been lost. If previous state was RETENTION, the
 * function reads the setting for the next retention logic state to
 * see the actual value.  In every other case, the logic is
 * retained. Returns either PWRDM_POWER_OFF or PWRDM_POWER_RET
 * depending whether the logic was retained or not.
 */
static int omap4_pwrdm_read_prev_logic_pwrst(struct powerdomain *pwrdm)
{
	int state;

	state = omap4_pwrdm_read_prev_pwrst(pwrdm);

	if (state == PWRDM_POWER_OFF)
		return PWRDM_POWER_OFF;

	if (state != PWRDM_POWER_RET)
		return PWRDM_POWER_RET;

	return omap4_pwrdm_read_logic_retst(pwrdm);
}

static int omap4_pwrdm_read_mem_pwrst(struct powerdomain *pwrdm, u8 bank)
{
	u32 m, v;

	m = omap2_pwrdm_get_mem_bank_stst_mask(bank);

	v = omap4_prminst_read_inst_reg(pwrdm->prcm_partition, pwrdm->prcm_offs,
					OMAP4_PM_PWSTST);
	v &= m;
	v >>= __ffs(m);

	return v;
}

static int omap4_pwrdm_read_mem_retst(struct powerdomain *pwrdm, u8 bank)
{
	u32 m, v;

	m = omap2_pwrdm_get_mem_bank_retst_mask(bank);

	v = omap4_prminst_read_inst_reg(pwrdm->prcm_partition, pwrdm->prcm_offs,
					OMAP4_PM_PWSTCTRL);
	v &= m;
	v >>= __ffs(m);

	return v;
}

/**
 * omap4_pwrdm_read_prev_mem_pwrst - reads the previous memory powerstate
 * @pwrdm: struct powerdomain * to read mem powerstate for
 * @bank: memory bank index
 *
 * Reads the previous memory powerstate for a powerdomain. This
 * function must determine the previous memory powerstate by first
 * checking the previous powerstate for the domain. If that was OFF,
 * then logic has been lost. If previous state was RETENTION, the
 * function reads the setting for the next memory retention state to
 * see the actual value.  In every other case, the logic is
 * retained. Returns either PWRDM_POWER_OFF or PWRDM_POWER_RET
 * depending whether logic was retained or not.
 */
static int omap4_pwrdm_read_prev_mem_pwrst(struct powerdomain *pwrdm, u8 bank)
{
	int state;

	state = omap4_pwrdm_read_prev_pwrst(pwrdm);

	if (state == PWRDM_POWER_OFF)
		return PWRDM_POWER_OFF;

	if (state != PWRDM_POWER_RET)
		return PWRDM_POWER_RET;

	return omap4_pwrdm_read_mem_retst(pwrdm, bank);
}

static int omap4_pwrdm_wait_transition(struct powerdomain *pwrdm)
{
	u32 c = 0;

	/*
	 * REVISIT: pwrdm_wait_transition() may be better implemented
	 * via a callback and a periodic timer check -- how long do we expect
	 * powerdomain transitions to take?
	 */

	/* XXX Is this udelay() value meaningful? */
	while ((omap4_prminst_read_inst_reg(pwrdm->prcm_partition,
					    pwrdm->prcm_offs,
					    OMAP4_PM_PWSTST) &
		OMAP_INTRANSITION_MASK) &&
	       (c++ < PWRDM_TRANSITION_BAILOUT))
		udelay(1);

	if (c > PWRDM_TRANSITION_BAILOUT) {
		pr_err("powerdomain: %s: waited too long to complete transition\n",
		       pwrdm->name);
		return -EAGAIN;
	}

	pr_debug("powerdomain: completed transition in %d loops\n", c);

	return 0;
}

static int omap4_check_vcvp(void)
{
	if (prm_features & PRM_HAS_VOLTAGE)
		return 1;

	return 0;
}

struct pwrdm_ops omap4_pwrdm_operations = {
	.pwrdm_set_next_pwrst	= omap4_pwrdm_set_next_pwrst,
	.pwrdm_read_next_pwrst	= omap4_pwrdm_read_next_pwrst,
	.pwrdm_read_pwrst	= omap4_pwrdm_read_pwrst,
	.pwrdm_read_prev_pwrst	= omap4_pwrdm_read_prev_pwrst,
	.pwrdm_set_lowpwrstchange	= omap4_pwrdm_set_lowpwrstchange,
	.pwrdm_clear_all_prev_pwrst	= omap4_pwrdm_clear_all_prev_pwrst,
	.pwrdm_set_logic_retst	= omap4_pwrdm_set_logic_retst,
	.pwrdm_read_logic_pwrst	= omap4_pwrdm_read_logic_pwrst,
	.pwrdm_read_prev_logic_pwrst	= omap4_pwrdm_read_prev_logic_pwrst,
	.pwrdm_read_logic_retst	= omap4_pwrdm_read_logic_retst,
	.pwrdm_read_mem_pwrst	= omap4_pwrdm_read_mem_pwrst,
	.pwrdm_read_mem_retst	= omap4_pwrdm_read_mem_retst,
	.pwrdm_read_prev_mem_pwrst	= omap4_pwrdm_read_prev_mem_pwrst,
	.pwrdm_set_mem_onst	= omap4_pwrdm_set_mem_onst,
	.pwrdm_set_mem_retst	= omap4_pwrdm_set_mem_retst,
	.pwrdm_wait_transition	= omap4_pwrdm_wait_transition,
	.pwrdm_has_voltdm	= omap4_check_vcvp,
};

static int omap44xx_prm_late_init(void);

/*
 * XXX document
 */
static struct prm_ll_data omap44xx_prm_ll_data = {
	.read_reset_sources = &omap44xx_prm_read_reset_sources,
	.was_any_context_lost_old = &omap44xx_prm_was_any_context_lost_old,
	.clear_context_loss_flags_old = &omap44xx_prm_clear_context_loss_flags_old,
	.late_init = &omap44xx_prm_late_init,
	.assert_hardreset	= omap4_prminst_assert_hardreset,
	.deassert_hardreset	= omap4_prminst_deassert_hardreset,
	.is_hardreset_asserted	= omap4_prminst_is_hardreset_asserted,
	.reset_system		= omap4_prminst_global_warm_sw_reset,
	.vp_check_txdone	= omap4_prm_vp_check_txdone,
	.vp_clear_txdone	= omap4_prm_vp_clear_txdone,
};

static const struct omap_prcm_init_data *prm_init_data;

int __init omap44xx_prm_init(const struct omap_prcm_init_data *data)
{
	omap_prm_base_init();

	prm_init_data = data;

	if (data->flags & PRM_HAS_IO_WAKEUP)
		prm_features |= PRM_HAS_IO_WAKEUP;

	if (data->flags & PRM_HAS_VOLTAGE)
		prm_features |= PRM_HAS_VOLTAGE;

	omap4_prminst_set_prm_dev_inst(data->device_inst_offset);

	/* Add AM437X specific differences */
	if (of_device_is_compatible(data->np, "ti,am4-prcm")) {
		omap4_prcm_irq_setup.nr_irqs = 1;
		omap4_prcm_irq_setup.nr_regs = 1;
		omap4_prcm_irq_setup.pm_ctrl = AM43XX_PRM_IO_PMCTRL_OFFSET;
		omap4_prcm_irq_setup.ack = AM43XX_PRM_IRQSTATUS_MPU_OFFSET;
		omap4_prcm_irq_setup.mask = AM43XX_PRM_IRQENABLE_MPU_OFFSET;
	}

	return prm_register(&omap44xx_prm_ll_data);
}

static int omap44xx_prm_late_init(void)
{
	int irq_num;

	if (!(prm_features & PRM_HAS_IO_WAKEUP))
		return 0;

	/* OMAP4+ is DT only now */
	if (!of_have_populated_dt())
		return 0;

	irq_num = of_irq_get(prm_init_data->np, 0);
	/*
	 * Already have OMAP4 IRQ num. For all other platforms, we need
	 * IRQ numbers from DT
	 */
	if (irq_num < 0 && !(prm_init_data->flags & PRM_IRQ_DEFAULT)) {
		if (irq_num == -EPROBE_DEFER)
			return irq_num;

		/* Have nothing to do */
		return 0;
	}

	/* Once OMAP4 DT is filled as well */
	if (irq_num >= 0) {
		omap4_prcm_irq_setup.irq = irq_num;
		omap4_prcm_irq_setup.xlate_irq = NULL;
	}

	omap44xx_prm_enable_io_wakeup();

	return omap_prcm_register_chain_handler(&omap4_prcm_irq_setup);
}

static void __exit omap44xx_prm_exit(void)
{
	prm_unregister(&omap44xx_prm_ll_data);
}
__exitcall(omap44xx_prm_exit);
