/*
 * OMAP2+ common Clock Management (CM) IP block functions
 *
 * Copyright (C) 2012 Texas Instruments, Inc.
 * Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * XXX This code should eventually be moved to a CM driver.
 */

#include <linux/kernel.h>
#include <linux/init.h>

#include "cm2xxx.h"
#include "cm3xxx.h"
#include "cm44xx.h"
#include "common.h"

/*
 * cm_ll_data: function pointers to SoC-specific implementations of
 * common CM functions
 */
static struct cm_ll_data null_cm_ll_data;
static struct cm_ll_data *cm_ll_data = &null_cm_ll_data;

/* cm_base: base virtual address of the CM IP block */
void __iomem *cm_base;

/* cm2_base: base virtual address of the CM2 IP block (OMAP44xx only) */
void __iomem *cm2_base;

/**
 * omap2_set_globals_cm - set the CM/CM2 base addresses (for early use)
 * @cm: CM base virtual address
 * @cm2: CM2 base virtual address (if present on the booted SoC)
 *
 * XXX Will be replaced when the PRM/CM drivers are completed.
 */
void __init omap2_set_globals_cm(void __iomem *cm, void __iomem *cm2)
{
	cm_base = cm;
	cm2_base = cm2;
}

/**
 * cm_split_idlest_reg - split CM_IDLEST reg addr into its components
 * @idlest_reg: CM_IDLEST* virtual address
 * @prcm_inst: pointer to an s16 to return the PRCM instance offset
 * @idlest_reg_id: pointer to a u8 to return the CM_IDLESTx register ID
 *
 * Given an absolute CM_IDLEST register address @idlest_reg, passes
 * the PRCM instance offset and IDLEST register ID back to the caller
 * via the @prcm_inst and @idlest_reg_id.  Returns -EINVAL upon error,
 * or 0 upon success.  XXX This function is only needed until absolute
 * register addresses are removed from the OMAP struct clk records.
 */
int cm_split_idlest_reg(void __iomem *idlest_reg, s16 *prcm_inst,
			u8 *idlest_reg_id)
{
	if (!cm_ll_data->split_idlest_reg) {
		WARN_ONCE(1, "cm: %s: no low-level function defined\n",
			  __func__);
		return -EINVAL;
	}

	return cm_ll_data->split_idlest_reg(idlest_reg, prcm_inst,
					   idlest_reg_id);
}

/**
 * cm_wait_module_ready - wait for a module to leave idle or standby
 * @prcm_mod: PRCM module offset
 * @idlest_id: CM_IDLESTx register ID (i.e., x = 1, 2, 3)
 * @idlest_shift: shift of the bit in the CM_IDLEST* register to check
 *
 * Wait for the PRCM to indicate that the module identified by
 * (@prcm_mod, @idlest_id, @idlest_shift) is clocked.  Return 0 upon
 * success, -EBUSY if the module doesn't enable in time, or -EINVAL if
 * no per-SoC wait_module_ready() function pointer has been registered
 * or if the idlest register is unknown on the SoC.
 */
int cm_wait_module_ready(s16 prcm_mod, u8 idlest_id, u8 idlest_shift)
{
	if (!cm_ll_data->wait_module_ready) {
		WARN_ONCE(1, "cm: %s: no low-level function defined\n",
			  __func__);
		return -EINVAL;
	}

	return cm_ll_data->wait_module_ready(prcm_mod, idlest_id, idlest_shift);
}

/**
 * cm_register - register per-SoC low-level data with the CM
 * @cld: low-level per-SoC OMAP CM data & function pointers to register
 *
 * Register per-SoC low-level OMAP CM data and function pointers with
 * the OMAP CM common interface.  The caller must keep the data
 * pointed to by @cld valid until it calls cm_unregister() and
 * it returns successfully.  Returns 0 upon success, -EINVAL if @cld
 * is NULL, or -EEXIST if cm_register() has already been called
 * without an intervening cm_unregister().
 */
int cm_register(struct cm_ll_data *cld)
{
	if (!cld)
		return -EINVAL;

	if (cm_ll_data != &null_cm_ll_data)
		return -EEXIST;

	cm_ll_data = cld;

	return 0;
}

/**
 * cm_unregister - unregister per-SoC low-level data & function pointers
 * @cld: low-level per-SoC OMAP CM data & function pointers to unregister
 *
 * Unregister per-SoC low-level OMAP CM data and function pointers
 * that were previously registered with cm_register().  The
 * caller may not destroy any of the data pointed to by @cld until
 * this function returns successfully.  Returns 0 upon success, or
 * -EINVAL if @cld is NULL or if @cld does not match the struct
 * cm_ll_data * previously registered by cm_register().
 */
int cm_unregister(struct cm_ll_data *cld)
{
	if (!cld || cm_ll_data != cld)
		return -EINVAL;

	cm_ll_data = &null_cm_ll_data;

	return 0;
}
