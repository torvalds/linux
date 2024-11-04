// SPDX-License-Identifier: GPL-2.0+
/*
 * RZ/G2L Display Unit DRM driver
 *
 * Copyright (C) 2023 Renesas Electronics Corporation
 *
 * Based on rcar_du_drv.c
 */

#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_client_setup.h>
#include <drm/drm_drv.h>
#include <drm/drm_fbdev_dma.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_probe_helper.h>

#include "rzg2l_du_drv.h"
#include "rzg2l_du_kms.h"

/* -----------------------------------------------------------------------------
 * Device Information
 */

static const struct rzg2l_du_device_info rzg2l_du_r9a07g043u_info = {
	.channels_mask = BIT(0),
	.routes = {
		[RZG2L_DU_OUTPUT_DPAD0] = {
			.possible_outputs = BIT(0),
			.port = 0,
		},
	},
};

static const struct rzg2l_du_device_info rzg2l_du_r9a07g044_info = {
	.channels_mask = BIT(0),
	.routes = {
		[RZG2L_DU_OUTPUT_DSI0] = {
			.possible_outputs = BIT(0),
			.port = 0,
		},
		[RZG2L_DU_OUTPUT_DPAD0] = {
			.possible_outputs = BIT(0),
			.port = 1,
		}
	}
};

static const struct of_device_id rzg2l_du_of_table[] = {
	{ .compatible = "renesas,r9a07g043u-du", .data = &rzg2l_du_r9a07g043u_info },
	{ .compatible = "renesas,r9a07g044-du", .data = &rzg2l_du_r9a07g044_info },
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, rzg2l_du_of_table);

const char *rzg2l_du_output_name(enum rzg2l_du_output output)
{
	static const char * const names[] = {
		[RZG2L_DU_OUTPUT_DSI0] = "DSI0",
		[RZG2L_DU_OUTPUT_DPAD0] = "DPAD0"
	};

	if (output >= ARRAY_SIZE(names))
		return "UNKNOWN";

	return names[output];
}

/* -----------------------------------------------------------------------------
 * DRM operations
 */

DEFINE_DRM_GEM_DMA_FOPS(rzg2l_du_fops);

static const struct drm_driver rzg2l_du_driver = {
	.driver_features	= DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC,
	.dumb_create		= rzg2l_du_dumb_create,
	DRM_FBDEV_DMA_DRIVER_OPS,
	.fops			= &rzg2l_du_fops,
	.name			= "rzg2l-du",
	.desc			= "Renesas RZ/G2L Display Unit",
	.date			= "20230410",
	.major			= 1,
	.minor			= 0,
};

/* -----------------------------------------------------------------------------
 * Platform driver
 */

static void rzg2l_du_remove(struct platform_device *pdev)
{
	struct rzg2l_du_device *rcdu = platform_get_drvdata(pdev);
	struct drm_device *ddev = &rcdu->ddev;

	drm_dev_unregister(ddev);
	drm_atomic_helper_shutdown(ddev);

	drm_kms_helper_poll_fini(ddev);
}

static void rzg2l_du_shutdown(struct platform_device *pdev)
{
	struct rzg2l_du_device *rcdu = platform_get_drvdata(pdev);

	drm_atomic_helper_shutdown(&rcdu->ddev);
}

static int rzg2l_du_probe(struct platform_device *pdev)
{
	struct rzg2l_du_device *rcdu;
	int ret;

	if (drm_firmware_drivers_only())
		return -ENODEV;

	/* Allocate and initialize the RZ/G2L device structure. */
	rcdu = devm_drm_dev_alloc(&pdev->dev, &rzg2l_du_driver,
				  struct rzg2l_du_device, ddev);
	if (IS_ERR(rcdu))
		return PTR_ERR(rcdu);

	rcdu->dev = &pdev->dev;
	rcdu->info = of_device_get_match_data(rcdu->dev);

	platform_set_drvdata(pdev, rcdu);

	/* I/O resources */
	rcdu->mmio = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(rcdu->mmio))
		return PTR_ERR(rcdu->mmio);

	ret = dma_coerce_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	/* DRM/KMS objects */
	ret = rzg2l_du_modeset_init(rcdu);
	if (ret < 0) {
		/*
		 * Don't use dev_err_probe(), as it would overwrite the probe
		 * deferral reason recorded in rzg2l_du_modeset_init().
		 */
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev,
				"failed to initialize DRM/KMS (%d)\n", ret);
		goto error;
	}

	/*
	 * Register the DRM device with the core and the connectors with
	 * sysfs.
	 */
	ret = drm_dev_register(&rcdu->ddev, 0);
	if (ret)
		goto error;

	drm_info(&rcdu->ddev, "Device %s probed\n", dev_name(&pdev->dev));

	drm_client_setup(&rcdu->ddev, NULL);

	return 0;

error:
	drm_kms_helper_poll_fini(&rcdu->ddev);
	return ret;
}

static struct platform_driver rzg2l_du_platform_driver = {
	.probe		= rzg2l_du_probe,
	.remove_new	= rzg2l_du_remove,
	.shutdown	= rzg2l_du_shutdown,
	.driver		= {
		.name	= "rzg2l-du",
		.of_match_table = rzg2l_du_of_table,
	},
};

module_platform_driver(rzg2l_du_platform_driver);

MODULE_AUTHOR("Biju Das <biju.das.jz@bp.renesas.com>");
MODULE_DESCRIPTION("Renesas RZ/G2L Display Unit DRM Driver");
MODULE_LICENSE("GPL");
