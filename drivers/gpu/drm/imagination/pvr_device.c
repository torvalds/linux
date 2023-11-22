// SPDX-License-Identifier: GPL-2.0-only OR MIT
/* Copyright (c) 2023 Imagination Technologies Ltd. */

#include "pvr_device.h"

#include <drm/drm_print.h>

#include <linux/clk.h>
#include <linux/compiler_attributes.h>
#include <linux/compiler_types.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/gfp.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/stddef.h>
#include <linux/types.h>

/**
 * pvr_device_reg_init() - Initialize kernel access to a PowerVR device's
 * control registers.
 * @pvr_dev: Target PowerVR device.
 *
 * Sets struct pvr_device->regs.
 *
 * This method of mapping the device control registers into memory ensures that
 * they are unmapped when the driver is detached (i.e. no explicit cleanup is
 * required).
 *
 * Return:
 *  * 0 on success, or
 *  * Any error returned by devm_platform_ioremap_resource().
 */
static int
pvr_device_reg_init(struct pvr_device *pvr_dev)
{
	struct drm_device *drm_dev = from_pvr_device(pvr_dev);
	struct platform_device *plat_dev = to_platform_device(drm_dev->dev);
	void __iomem *regs;

	pvr_dev->regs = NULL;

	regs = devm_platform_ioremap_resource(plat_dev, 0);
	if (IS_ERR(regs))
		return dev_err_probe(drm_dev->dev, PTR_ERR(regs),
				     "failed to ioremap gpu registers\n");

	pvr_dev->regs = regs;

	return 0;
}

/**
 * pvr_device_clk_init() - Initialize clocks required by a PowerVR device
 * @pvr_dev: Target PowerVR device.
 *
 * Sets struct pvr_device->core_clk, struct pvr_device->sys_clk and
 * struct pvr_device->mem_clk.
 *
 * Three clocks are required by the PowerVR device: core, sys and mem. On
 * return, this function guarantees that the clocks are in one of the following
 * states:
 *
 *  * All successfully initialized,
 *  * Core errored, sys and mem uninitialized,
 *  * Core deinitialized, sys errored, mem uninitialized, or
 *  * Core and sys deinitialized, mem errored.
 *
 * Return:
 *  * 0 on success,
 *  * Any error returned by devm_clk_get(), or
 *  * Any error returned by devm_clk_get_optional().
 */
static int pvr_device_clk_init(struct pvr_device *pvr_dev)
{
	struct drm_device *drm_dev = from_pvr_device(pvr_dev);
	struct clk *core_clk;
	struct clk *sys_clk;
	struct clk *mem_clk;

	core_clk = devm_clk_get(drm_dev->dev, "core");
	if (IS_ERR(core_clk))
		return dev_err_probe(drm_dev->dev, PTR_ERR(core_clk),
				     "failed to get core clock\n");

	sys_clk = devm_clk_get_optional(drm_dev->dev, "sys");
	if (IS_ERR(sys_clk))
		return dev_err_probe(drm_dev->dev, PTR_ERR(core_clk),
				     "failed to get sys clock\n");

	mem_clk = devm_clk_get_optional(drm_dev->dev, "mem");
	if (IS_ERR(mem_clk))
		return dev_err_probe(drm_dev->dev, PTR_ERR(core_clk),
				     "failed to get mem clock\n");

	pvr_dev->core_clk = core_clk;
	pvr_dev->sys_clk = sys_clk;
	pvr_dev->mem_clk = mem_clk;

	return 0;
}

/**
 * pvr_device_init() - Initialize a PowerVR device
 * @pvr_dev: Target PowerVR device.
 *
 * If this function returns successfully, the device will have been fully
 * initialized. Otherwise, any parts of the device initialized before an error
 * occurs will be de-initialized before returning.
 *
 * NOTE: The initialization steps currently taken are the bare minimum required
 *       to read from the control registers. The device is unlikely to function
 *       until further initialization steps are added. [This note should be
 *       removed when that happens.]
 *
 * Return:
 *  * 0 on success,
 *  * Any error returned by pvr_device_reg_init(),
 *  * Any error returned by pvr_device_clk_init(), or
 *  * Any error returned by pvr_device_gpu_init().
 */
int
pvr_device_init(struct pvr_device *pvr_dev)
{
	int err;

	/* Enable and initialize clocks required for the device to operate. */
	err = pvr_device_clk_init(pvr_dev);
	if (err)
		return err;

	/* Map the control registers into memory. */
	return pvr_device_reg_init(pvr_dev);
}

/**
 * pvr_device_fini() - Deinitialize a PowerVR device
 * @pvr_dev: Target PowerVR device.
 */
void
pvr_device_fini(struct pvr_device *pvr_dev)
{
	/*
	 * Deinitialization stages are performed in reverse order compared to
	 * the initialization stages in pvr_device_init().
	 */
}
