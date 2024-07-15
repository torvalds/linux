// SPDX-License-Identifier: GPL-2.0-only
/*
 * AM33XX PRM functions
 *
 * Copyright (C) 2011-2012 Texas Instruments Incorporated - https://www.ti.com/
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/reboot.h>

#include "powerdomain.h"
#include "prm33xx.h"
#include "prm-regbits-33xx.h"

/* Read a register in a PRM instance */
static u32 am33xx_prm_read_reg(s16 inst, u16 idx)
{
	return readl_relaxed(prm_base.va + inst + idx);
}

/* Write into a register in a PRM instance */
static void am33xx_prm_write_reg(u32 val, s16 inst, u16 idx)
{
	writel_relaxed(val, prm_base.va + inst + idx);
}

/* Read-modify-write a register in PRM. Caller must lock */
static u32 am33xx_prm_rmw_reg_bits(u32 mask, u32 bits, s16 inst, s16 idx)
{
	u32 v;

	v = am33xx_prm_read_reg(inst, idx);
	v &= ~mask;
	v |= bits;
	am33xx_prm_write_reg(v, inst, idx);

	return v;
}

/**
 * am33xx_prm_is_hardreset_asserted - read the HW reset line state of
 * submodules contained in the hwmod module
 * @shift: register bit shift corresponding to the reset line to check
 * @part: PRM partition, ignored for AM33xx
 * @inst: CM instance register offset (*_INST macro)
 * @rstctrl_offs: RM_RSTCTRL register address offset for this module
 *
 * Returns 1 if the (sub)module hardreset line is currently asserted,
 * 0 if the (sub)module hardreset line is not currently asserted, or
 * -EINVAL upon parameter error.
 */
static int am33xx_prm_is_hardreset_asserted(u8 shift, u8 part, s16 inst,
					    u16 rstctrl_offs)
{
	u32 v;

	v = am33xx_prm_read_reg(inst, rstctrl_offs);
	v &= 1 << shift;
	v >>= shift;

	return v;
}

/**
 * am33xx_prm_assert_hardreset - assert the HW reset line of a submodule
 * @shift: register bit shift corresponding to the reset line to assert
 * @part: CM partition, ignored for AM33xx
 * @inst: CM instance register offset (*_INST macro)
 * @rstctrl_reg: RM_RSTCTRL register address for this module
 *
 * Some IPs like dsp, ipu or iva contain processors that require an HW
 * reset line to be asserted / deasserted in order to fully enable the
 * IP.  These modules may have multiple hard-reset lines that reset
 * different 'submodules' inside the IP block.  This function will
 * place the submodule into reset.  Returns 0 upon success or -EINVAL
 * upon an argument error.
 */
static int am33xx_prm_assert_hardreset(u8 shift, u8 part, s16 inst,
				       u16 rstctrl_offs)
{
	u32 mask = 1 << shift;

	am33xx_prm_rmw_reg_bits(mask, mask, inst, rstctrl_offs);

	return 0;
}

/**
 * am33xx_prm_deassert_hardreset - deassert a submodule hardreset line and
 * wait
 * @shift: register bit shift corresponding to the reset line to deassert
 * @st_shift: reset status register bit shift corresponding to the reset line
 * @part: PRM partition, not used for AM33xx
 * @inst: CM instance register offset (*_INST macro)
 * @rstctrl_reg: RM_RSTCTRL register address for this module
 * @rstst_reg: RM_RSTST register address for this module
 *
 * Some IPs like dsp, ipu or iva contain processors that require an HW
 * reset line to be asserted / deasserted in order to fully enable the
 * IP.  These modules may have multiple hard-reset lines that reset
 * different 'submodules' inside the IP block.  This function will
 * take the submodule out of reset and wait until the PRCM indicates
 * that the reset has completed before returning.  Returns 0 upon success or
 * -EINVAL upon an argument error, -EEXIST if the submodule was already out
 * of reset, or -EBUSY if the submodule did not exit reset promptly.
 */
static int am33xx_prm_deassert_hardreset(u8 shift, u8 st_shift, u8 part,
					 s16 inst, u16 rstctrl_offs,
					 u16 rstst_offs)
{
	int c;
	u32 mask = 1 << st_shift;

	/* Check the current status to avoid  de-asserting the line twice */
	if (am33xx_prm_is_hardreset_asserted(shift, 0, inst, rstctrl_offs) == 0)
		return -EEXIST;

	/* Clear the reset status by writing 1 to the status bit */
	am33xx_prm_rmw_reg_bits(0xffffffff, mask, inst, rstst_offs);

	/* de-assert the reset control line */
	mask = 1 << shift;

	am33xx_prm_rmw_reg_bits(mask, 0, inst, rstctrl_offs);

	/* wait the status to be set */
	omap_test_timeout(am33xx_prm_is_hardreset_asserted(st_shift, 0, inst,
							   rstst_offs),
			  MAX_MODULE_HARDRESET_WAIT, c);

	return (c == MAX_MODULE_HARDRESET_WAIT) ? -EBUSY : 0;
}

static int am33xx_pwrdm_set_next_pwrst(struct powerdomain *pwrdm, u8 pwrst)
{
	am33xx_prm_rmw_reg_bits(OMAP_POWERSTATE_MASK,
				(pwrst << OMAP_POWERSTATE_SHIFT),
				pwrdm->prcm_offs, pwrdm->pwrstctrl_offs);
	return 0;
}

static int am33xx_pwrdm_read_next_pwrst(struct powerdomain *pwrdm)
{
	u32 v;

	v = am33xx_prm_read_reg(pwrdm->prcm_offs,  pwrdm->pwrstctrl_offs);
	v &= OMAP_POWERSTATE_MASK;
	v >>= OMAP_POWERSTATE_SHIFT;

	return v;
}

static int am33xx_pwrdm_read_pwrst(struct powerdomain *pwrdm)
{
	u32 v;

	v = am33xx_prm_read_reg(pwrdm->prcm_offs, pwrdm->pwrstst_offs);
	v &= OMAP_POWERSTATEST_MASK;
	v >>= OMAP_POWERSTATEST_SHIFT;

	return v;
}

static int am33xx_pwrdm_set_lowpwrstchange(struct powerdomain *pwrdm)
{
	am33xx_prm_rmw_reg_bits(AM33XX_LOWPOWERSTATECHANGE_MASK,
				(1 << AM33XX_LOWPOWERSTATECHANGE_SHIFT),
				pwrdm->prcm_offs, pwrdm->pwrstctrl_offs);
	return 0;
}

static int am33xx_pwrdm_clear_all_prev_pwrst(struct powerdomain *pwrdm)
{
	am33xx_prm_rmw_reg_bits(AM33XX_LASTPOWERSTATEENTERED_MASK,
				AM33XX_LASTPOWERSTATEENTERED_MASK,
				pwrdm->prcm_offs, pwrdm->pwrstst_offs);
	return 0;
}

static int am33xx_pwrdm_set_logic_retst(struct powerdomain *pwrdm, u8 pwrst)
{
	u32 m;

	m = pwrdm->logicretstate_mask;
	if (!m)
		return -EINVAL;

	am33xx_prm_rmw_reg_bits(m, (pwrst << __ffs(m)),
				pwrdm->prcm_offs, pwrdm->pwrstctrl_offs);

	return 0;
}

static int am33xx_pwrdm_read_logic_pwrst(struct powerdomain *pwrdm)
{
	u32 v;

	v = am33xx_prm_read_reg(pwrdm->prcm_offs, pwrdm->pwrstst_offs);
	v &= AM33XX_LOGICSTATEST_MASK;
	v >>= AM33XX_LOGICSTATEST_SHIFT;

	return v;
}

static int am33xx_pwrdm_read_logic_retst(struct powerdomain *pwrdm)
{
	u32 v, m;

	m = pwrdm->logicretstate_mask;
	if (!m)
		return -EINVAL;

	v = am33xx_prm_read_reg(pwrdm->prcm_offs, pwrdm->pwrstctrl_offs);
	v &= m;
	v >>= __ffs(m);

	return v;
}

static int am33xx_pwrdm_set_mem_onst(struct powerdomain *pwrdm, u8 bank,
		u8 pwrst)
{
	u32 m;

	m = pwrdm->mem_on_mask[bank];
	if (!m)
		return -EINVAL;

	am33xx_prm_rmw_reg_bits(m, (pwrst << __ffs(m)),
				pwrdm->prcm_offs, pwrdm->pwrstctrl_offs);

	return 0;
}

static int am33xx_pwrdm_set_mem_retst(struct powerdomain *pwrdm, u8 bank,
					u8 pwrst)
{
	u32 m;

	m = pwrdm->mem_ret_mask[bank];
	if (!m)
		return -EINVAL;

	am33xx_prm_rmw_reg_bits(m, (pwrst << __ffs(m)),
				pwrdm->prcm_offs, pwrdm->pwrstctrl_offs);

	return 0;
}

static int am33xx_pwrdm_read_mem_pwrst(struct powerdomain *pwrdm, u8 bank)
{
	u32 m, v;

	m = pwrdm->mem_pwrst_mask[bank];
	if (!m)
		return -EINVAL;

	v = am33xx_prm_read_reg(pwrdm->prcm_offs, pwrdm->pwrstst_offs);
	v &= m;
	v >>= __ffs(m);

	return v;
}

static int am33xx_pwrdm_read_mem_retst(struct powerdomain *pwrdm, u8 bank)
{
	u32 m, v;

	m = pwrdm->mem_retst_mask[bank];
	if (!m)
		return -EINVAL;

	v = am33xx_prm_read_reg(pwrdm->prcm_offs, pwrdm->pwrstctrl_offs);
	v &= m;
	v >>= __ffs(m);

	return v;
}

static int am33xx_pwrdm_wait_transition(struct powerdomain *pwrdm)
{
	u32 c = 0;

	/*
	 * REVISIT: pwrdm_wait_transition() may be better implemented
	 * via a callback and a periodic timer check -- how long do we expect
	 * powerdomain transitions to take?
	 */

	/* XXX Is this udelay() value meaningful? */
	while ((am33xx_prm_read_reg(pwrdm->prcm_offs, pwrdm->pwrstst_offs)
			& OMAP_INTRANSITION_MASK) &&
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

static int am33xx_check_vcvp(void)
{
	/* No VC/VP on am33xx devices */
	return 0;
}

/**
 * am33xx_prm_global_warm_sw_reset - reboot the device via warm reset
 *
 * Immediately reboots the device through warm reset.
 */
static void am33xx_prm_global_sw_reset(void)
{
	/*
	 * Historically AM33xx performed warm reset for all requested reboot_mode.
	 * Keep this behaviour unchanged for all except newly added REBOOT_COLD.
	 */
	u32 mask = AM33XX_RST_GLOBAL_WARM_SW_MASK;

	if (prm_reboot_mode == REBOOT_COLD)
		mask = AM33XX_RST_GLOBAL_COLD_SW_MASK;

	am33xx_prm_rmw_reg_bits(mask,
				mask,
				AM33XX_PRM_DEVICE_MOD,
				AM33XX_PRM_RSTCTRL_OFFSET);

	/* OCP barrier */
	(void)am33xx_prm_read_reg(AM33XX_PRM_DEVICE_MOD,
				  AM33XX_PRM_RSTCTRL_OFFSET);
}

static void am33xx_pwrdm_save_context(struct powerdomain *pwrdm)
{
	pwrdm->context = am33xx_prm_read_reg(pwrdm->prcm_offs,
						pwrdm->pwrstctrl_offs);
	/*
	 * Do not save LOWPOWERSTATECHANGE, writing a 1 indicates a request,
	 * reading back a 1 indicates a request in progress.
	 */
	pwrdm->context &= ~AM33XX_LOWPOWERSTATECHANGE_MASK;
}

static void am33xx_pwrdm_restore_context(struct powerdomain *pwrdm)
{
	int st, ctrl;

	st = am33xx_prm_read_reg(pwrdm->prcm_offs,
				 pwrdm->pwrstst_offs);

	am33xx_prm_write_reg(pwrdm->context, pwrdm->prcm_offs,
			     pwrdm->pwrstctrl_offs);

	/* Make sure we only wait for a transition if there is one */
	st &= OMAP_POWERSTATEST_MASK;
	ctrl = OMAP_POWERSTATEST_MASK & pwrdm->context;

	if (st != ctrl)
		am33xx_pwrdm_wait_transition(pwrdm);
}

struct pwrdm_ops am33xx_pwrdm_operations = {
	.pwrdm_set_next_pwrst		= am33xx_pwrdm_set_next_pwrst,
	.pwrdm_read_next_pwrst		= am33xx_pwrdm_read_next_pwrst,
	.pwrdm_read_pwrst		= am33xx_pwrdm_read_pwrst,
	.pwrdm_set_logic_retst		= am33xx_pwrdm_set_logic_retst,
	.pwrdm_read_logic_pwrst		= am33xx_pwrdm_read_logic_pwrst,
	.pwrdm_read_logic_retst		= am33xx_pwrdm_read_logic_retst,
	.pwrdm_clear_all_prev_pwrst	= am33xx_pwrdm_clear_all_prev_pwrst,
	.pwrdm_set_lowpwrstchange	= am33xx_pwrdm_set_lowpwrstchange,
	.pwrdm_read_mem_pwrst		= am33xx_pwrdm_read_mem_pwrst,
	.pwrdm_read_mem_retst		= am33xx_pwrdm_read_mem_retst,
	.pwrdm_set_mem_onst		= am33xx_pwrdm_set_mem_onst,
	.pwrdm_set_mem_retst		= am33xx_pwrdm_set_mem_retst,
	.pwrdm_wait_transition		= am33xx_pwrdm_wait_transition,
	.pwrdm_has_voltdm		= am33xx_check_vcvp,
	.pwrdm_save_context		= am33xx_pwrdm_save_context,
	.pwrdm_restore_context		= am33xx_pwrdm_restore_context,
};

static struct prm_ll_data am33xx_prm_ll_data = {
	.assert_hardreset		= am33xx_prm_assert_hardreset,
	.deassert_hardreset		= am33xx_prm_deassert_hardreset,
	.is_hardreset_asserted		= am33xx_prm_is_hardreset_asserted,
	.reset_system			= am33xx_prm_global_sw_reset,
};

int __init am33xx_prm_init(const struct omap_prcm_init_data *data)
{
	return prm_register(&am33xx_prm_ll_data);
}

static void __exit am33xx_prm_exit(void)
{
	prm_unregister(&am33xx_prm_ll_data);
}
__exitcall(am33xx_prm_exit);
