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
#include <linux/errno.h>
#include <linux/bug.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include "cm2xxx.h"
#include "cm3xxx.h"
#include "cm33xx.h"
#include "cm44xx.h"
#include "clock.h"

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

#define CM_NO_CLOCKS		0x1
#define CM_SINGLE_INSTANCE	0x2

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
 * omap_cm_wait_module_ready - wait for a module to leave idle or standby
 * @part: PRCM partition
 * @prcm_mod: PRCM module offset
 * @idlest_reg: CM_IDLESTx register
 * @idlest_shift: shift of the bit in the CM_IDLEST* register to check
 *
 * Wait for the PRCM to indicate that the module identified by
 * (@prcm_mod, @idlest_id, @idlest_shift) is clocked.  Return 0 upon
 * success, -EBUSY if the module doesn't enable in time, or -EINVAL if
 * no per-SoC wait_module_ready() function pointer has been registered
 * or if the idlest register is unknown on the SoC.
 */
int omap_cm_wait_module_ready(u8 part, s16 prcm_mod, u16 idlest_reg,
			      u8 idlest_shift)
{
	if (!cm_ll_data->wait_module_ready) {
		WARN_ONCE(1, "cm: %s: no low-level function defined\n",
			  __func__);
		return -EINVAL;
	}

	return cm_ll_data->wait_module_ready(part, prcm_mod, idlest_reg,
					     idlest_shift);
}

/**
 * omap_cm_wait_module_idle - wait for a module to enter idle or standby
 * @part: PRCM partition
 * @prcm_mod: PRCM module offset
 * @idlest_reg: CM_IDLESTx register
 * @idlest_shift: shift of the bit in the CM_IDLEST* register to check
 *
 * Wait for the PRCM to indicate that the module identified by
 * (@prcm_mod, @idlest_id, @idlest_shift) is no longer clocked.  Return
 * 0 upon success, -EBUSY if the module doesn't enable in time, or
 * -EINVAL if no per-SoC wait_module_idle() function pointer has been
 * registered or if the idlest register is unknown on the SoC.
 */
int omap_cm_wait_module_idle(u8 part, s16 prcm_mod, u16 idlest_reg,
			     u8 idlest_shift)
{
	if (!cm_ll_data->wait_module_idle) {
		WARN_ONCE(1, "cm: %s: no low-level function defined\n",
			  __func__);
		return -EINVAL;
	}

	return cm_ll_data->wait_module_idle(part, prcm_mod, idlest_reg,
					    idlest_shift);
}

/**
 * omap_cm_module_enable - enable a module
 * @mode: target mode for the module
 * @part: PRCM partition
 * @inst: PRCM instance
 * @clkctrl_offs: CM_CLKCTRL register offset for the module
 *
 * Enables clocks for a module identified by (@part, @inst, @clkctrl_offs)
 * making its IO space accessible. Return 0 upon success, -EINVAL if no
 * per-SoC module_enable() function pointer has been registered.
 */
int omap_cm_module_enable(u8 mode, u8 part, u16 inst, u16 clkctrl_offs)
{
	if (!cm_ll_data->module_enable) {
		WARN_ONCE(1, "cm: %s: no low-level function defined\n",
			  __func__);
		return -EINVAL;
	}

	cm_ll_data->module_enable(mode, part, inst, clkctrl_offs);
	return 0;
}

/**
 * omap_cm_module_disable - disable a module
 * @part: PRCM partition
 * @inst: PRCM instance
 * @clkctrl_offs: CM_CLKCTRL register offset for the module
 *
 * Disables clocks for a module identified by (@part, @inst, @clkctrl_offs)
 * makings its IO space inaccessible. Return 0 upon success, -EINVAL if
 * no per-SoC module_disable() function pointer has been registered.
 */
int omap_cm_module_disable(u8 part, u16 inst, u16 clkctrl_offs)
{
	if (!cm_ll_data->module_disable) {
		WARN_ONCE(1, "cm: %s: no low-level function defined\n",
			  __func__);
		return -EINVAL;
	}

	cm_ll_data->module_disable(part, inst, clkctrl_offs);
	return 0;
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

#if defined(CONFIG_ARCH_OMAP4) || defined(CONFIG_SOC_OMAP5) || \
	defined(CONFIG_SOC_DRA7XX)
static struct omap_prcm_init_data cm_data __initdata = {
	.index = TI_CLKM_CM,
	.init = omap4_cm_init,
};

static struct omap_prcm_init_data cm2_data __initdata = {
	.index = TI_CLKM_CM2,
	.init = omap4_cm_init,
};
#endif

#ifdef CONFIG_ARCH_OMAP2
static struct omap_prcm_init_data omap2_prcm_data __initdata = {
	.index = TI_CLKM_CM,
	.init = omap2xxx_cm_init,
	.flags = CM_NO_CLOCKS | CM_SINGLE_INSTANCE,
};
#endif

#ifdef CONFIG_ARCH_OMAP3
static struct omap_prcm_init_data omap3_cm_data __initdata = {
	.index = TI_CLKM_CM,
	.init = omap3xxx_cm_init,
	.flags = CM_SINGLE_INSTANCE,

	/*
	 * IVA2 offset is a negative value, must offset the cm_base address
	 * by this to get it to positive side on the iomap
	 */
	.offset = -OMAP3430_IVA2_MOD,
};
#endif

#if defined(CONFIG_SOC_AM33XX) || defined(CONFIG_SOC_TI81XX)
static struct omap_prcm_init_data am3_prcm_data __initdata = {
	.index = TI_CLKM_CM,
	.flags = CM_NO_CLOCKS | CM_SINGLE_INSTANCE,
	.init = am33xx_cm_init,
};
#endif

#ifdef CONFIG_SOC_AM43XX
static struct omap_prcm_init_data am4_prcm_data __initdata = {
	.index = TI_CLKM_CM,
	.flags = CM_NO_CLOCKS | CM_SINGLE_INSTANCE,
	.init = omap4_cm_init,
};
#endif

static const struct of_device_id omap_cm_dt_match_table[] __initconst = {
#ifdef CONFIG_ARCH_OMAP2
	{ .compatible = "ti,omap2-prcm", .data = &omap2_prcm_data },
#endif
#ifdef CONFIG_ARCH_OMAP3
	{ .compatible = "ti,omap3-cm", .data = &omap3_cm_data },
#endif
#ifdef CONFIG_ARCH_OMAP4
	{ .compatible = "ti,omap4-cm1", .data = &cm_data },
	{ .compatible = "ti,omap4-cm2", .data = &cm2_data },
#endif
#ifdef CONFIG_SOC_OMAP5
	{ .compatible = "ti,omap5-cm-core-aon", .data = &cm_data },
	{ .compatible = "ti,omap5-cm-core", .data = &cm2_data },
#endif
#ifdef CONFIG_SOC_DRA7XX
	{ .compatible = "ti,dra7-cm-core-aon", .data = &cm_data },
	{ .compatible = "ti,dra7-cm-core", .data = &cm2_data },
#endif
#ifdef CONFIG_SOC_AM33XX
	{ .compatible = "ti,am3-prcm", .data = &am3_prcm_data },
#endif
#ifdef CONFIG_SOC_AM43XX
	{ .compatible = "ti,am4-prcm", .data = &am4_prcm_data },
#endif
#ifdef CONFIG_SOC_TI81XX
	{ .compatible = "ti,dm814-prcm", .data = &am3_prcm_data },
	{ .compatible = "ti,dm816-prcm", .data = &am3_prcm_data },
#endif
	{ }
};

/**
 * omap2_cm_base_init - initialize iomappings for the CM drivers
 *
 * Detects and initializes the iomappings for the CM driver, based
 * on the DT data. Returns 0 in success, negative error value
 * otherwise.
 */
int __init omap2_cm_base_init(void)
{
	struct device_node *np;
	const struct of_device_id *match;
	struct omap_prcm_init_data *data;
	void __iomem *mem;

	for_each_matching_node_and_match(np, omap_cm_dt_match_table, &match) {
		data = (struct omap_prcm_init_data *)match->data;

		mem = of_iomap(np, 0);
		if (!mem)
			return -ENOMEM;

		if (data->index == TI_CLKM_CM)
			cm_base = mem + data->offset;

		if (data->index == TI_CLKM_CM2)
			cm2_base = mem + data->offset;

		data->mem = mem;

		data->np = np;

		if (data->init && (data->flags & CM_SINGLE_INSTANCE ||
				   (cm_base && cm2_base)))
			data->init(data);
	}

	return 0;
}

/**
 * omap_cm_init - low level init for the CM drivers
 *
 * Initializes the low level clock infrastructure for CM drivers.
 * Returns 0 in success, negative error value in failure.
 */
int __init omap_cm_init(void)
{
	struct device_node *np;
	const struct of_device_id *match;
	const struct omap_prcm_init_data *data;
	int ret;

	for_each_matching_node_and_match(np, omap_cm_dt_match_table, &match) {
		data = match->data;

		if (data->flags & CM_NO_CLOCKS)
			continue;

		ret = omap2_clk_provider_init(np, data->index, NULL, data->mem);
		if (ret)
			return ret;
	}

	return 0;
}
