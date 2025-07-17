// SPDX-License-Identifier: GPL-2.0+
/*
 * R-Car Display Unit Color Management Module
 *
 * Copyright (C) 2019 Jacopo Mondi <jacopo+renesas@jmondi.org>
 */

#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include <drm/drm_color_mgmt.h>

#include "rcar_cmm.h"

#define CM2_LUT_CTRL		0x0000
#define CM2_LUT_CTRL_LUT_EN	BIT(0)
#define CM2_LUT_TBL_BASE	0x0600
#define CM2_LUT_TBL(__i)	(CM2_LUT_TBL_BASE + (__i) * 4)

struct rcar_cmm {
	void __iomem *base;

	/*
	 * @lut:		1D-LUT state
	 * @lut.enabled:	1D-LUT enabled flag
	 */
	struct {
		bool enabled;
	} lut;
};

static inline void rcar_cmm_write(struct rcar_cmm *rcmm, u32 reg, u32 data)
{
	iowrite32(data, rcmm->base + reg);
}

/*
 * rcar_cmm_lut_write() - Scale the DRM LUT table entries to hardware precision
 *			  and write to the CMM registers
 * @rcmm: Pointer to the CMM device
 * @drm_lut: Pointer to the DRM LUT table
 */
static void rcar_cmm_lut_write(struct rcar_cmm *rcmm,
			       const struct drm_color_lut *drm_lut)
{
	unsigned int i;

	for (i = 0; i < CM2_LUT_SIZE; ++i) {
		u32 entry = drm_color_lut_extract(drm_lut[i].red, 8) << 16
			  | drm_color_lut_extract(drm_lut[i].green, 8) << 8
			  | drm_color_lut_extract(drm_lut[i].blue, 8);

		rcar_cmm_write(rcmm, CM2_LUT_TBL(i), entry);
	}
}

/*
 * rcar_cmm_setup() - Configure the CMM unit
 * @pdev: The platform device associated with the CMM instance
 * @config: The CMM unit configuration
 *
 * Configure the CMM unit with the given configuration. Currently enabling,
 * disabling and programming of the 1-D LUT unit is supported.
 *
 * As rcar_cmm_setup() accesses the CMM registers the unit should be powered
 * and its functional clock enabled. To guarantee this, before any call to
 * this function is made, the CMM unit has to be enabled by calling
 * rcar_cmm_enable() first.
 *
 * TODO: Add support for LUT double buffer operations to avoid updating the
 * LUT table entries while a frame is being displayed.
 */
int rcar_cmm_setup(struct platform_device *pdev,
		   const struct rcar_cmm_config *config)
{
	struct rcar_cmm *rcmm = platform_get_drvdata(pdev);

	/* Disable LUT if no table is provided. */
	if (!config->lut.table) {
		if (rcmm->lut.enabled) {
			rcar_cmm_write(rcmm, CM2_LUT_CTRL, 0);
			rcmm->lut.enabled = false;
		}

		return 0;
	}

	/* Enable LUT and program the new gamma table values. */
	if (!rcmm->lut.enabled) {
		rcar_cmm_write(rcmm, CM2_LUT_CTRL, CM2_LUT_CTRL_LUT_EN);
		rcmm->lut.enabled = true;
	}

	rcar_cmm_lut_write(rcmm, config->lut.table);

	return 0;
}
EXPORT_SYMBOL_GPL(rcar_cmm_setup);

/*
 * rcar_cmm_enable() - Enable the CMM unit
 * @pdev: The platform device associated with the CMM instance
 *
 * When the output of the corresponding DU channel is routed to the CMM unit,
 * the unit shall be enabled before the DU channel is started, and remain
 * enabled until the channel is stopped. The CMM unit shall be disabled with
 * rcar_cmm_disable().
 *
 * Calls to rcar_cmm_enable() and rcar_cmm_disable() are not reference-counted.
 * It is an error to attempt to enable an already enabled CMM unit, or to
 * attempt to disable a disabled unit.
 */
int rcar_cmm_enable(struct platform_device *pdev)
{
	int ret;

	ret = pm_runtime_resume_and_get(&pdev->dev);
	if (ret < 0)
		return ret;

	return 0;
}
EXPORT_SYMBOL_GPL(rcar_cmm_enable);

/*
 * rcar_cmm_disable() - Disable the CMM unit
 * @pdev: The platform device associated with the CMM instance
 *
 * See rcar_cmm_enable() for usage information.
 *
 * Disabling the CMM unit disable all the internal processing blocks. The CMM
 * state shall thus be restored with rcar_cmm_setup() when re-enabling the CMM
 * unit after the next rcar_cmm_enable() call.
 */
void rcar_cmm_disable(struct platform_device *pdev)
{
	struct rcar_cmm *rcmm = platform_get_drvdata(pdev);

	rcar_cmm_write(rcmm, CM2_LUT_CTRL, 0);
	rcmm->lut.enabled = false;

	pm_runtime_put(&pdev->dev);
}
EXPORT_SYMBOL_GPL(rcar_cmm_disable);

/*
 * rcar_cmm_init() - Initialize the CMM unit
 * @pdev: The platform device associated with the CMM instance
 *
 * Return: 0 on success, -EPROBE_DEFER if the CMM is not available yet,
 *         -ENODEV if the DRM_RCAR_CMM config option is disabled
 */
int rcar_cmm_init(struct platform_device *pdev)
{
	struct rcar_cmm *rcmm = platform_get_drvdata(pdev);

	if (!rcmm)
		return -EPROBE_DEFER;

	return 0;
}
EXPORT_SYMBOL_GPL(rcar_cmm_init);

static int rcar_cmm_probe(struct platform_device *pdev)
{
	struct rcar_cmm *rcmm;

	rcmm = devm_kzalloc(&pdev->dev, sizeof(*rcmm), GFP_KERNEL);
	if (!rcmm)
		return -ENOMEM;
	platform_set_drvdata(pdev, rcmm);

	rcmm->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(rcmm->base))
		return PTR_ERR(rcmm->base);

	pm_runtime_enable(&pdev->dev);

	return 0;
}

static void rcar_cmm_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);
}

static const struct of_device_id rcar_cmm_of_table[] = {
	{ .compatible = "renesas,rcar-gen3-cmm", },
	{ .compatible = "renesas,rcar-gen2-cmm", },
	{ },
};
MODULE_DEVICE_TABLE(of, rcar_cmm_of_table);

static struct platform_driver rcar_cmm_platform_driver = {
	.probe		= rcar_cmm_probe,
	.remove		= rcar_cmm_remove,
	.driver		= {
		.name	= "rcar-cmm",
		.of_match_table = rcar_cmm_of_table,
	},
};

module_platform_driver(rcar_cmm_platform_driver);

MODULE_AUTHOR("Jacopo Mondi <jacopo+renesas@jmondi.org>");
MODULE_DESCRIPTION("Renesas R-Car CMM Driver");
MODULE_LICENSE("GPL v2");
