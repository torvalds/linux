/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * RZ/G2L Display Unit DRM driver
 *
 * Copyright (C) 2023 Renesas Electronics Corporation
 *
 * Based on rcar_du_drv.h
 */

#ifndef __RZG2L_DU_DRV_H__
#define __RZG2L_DU_DRV_H__

#include <linux/kernel.h>

#include <drm/drm_device.h>

#include "rzg2l_du_crtc.h"
#include "rzg2l_du_vsp.h"

struct device;
struct drm_property;

enum rzg2l_du_output {
	RZG2L_DU_OUTPUT_DSI0,
	RZG2L_DU_OUTPUT_DPAD0,
	RZG2L_DU_OUTPUT_MAX,
};

/*
 * struct rzg2l_du_output_routing - Output routing specification
 * @possible_outputs: bitmask of possible outputs
 * @port: device tree port number corresponding to this output route
 *
 * The DU has 2 possible outputs (DPAD0, DSI0). Output routing data
 * specify the valid SoC outputs, which CRTC can drive the output, and the type
 * of in-SoC encoder for the output.
 */
struct rzg2l_du_output_routing {
	unsigned int possible_outputs;
	unsigned int port;
};

/*
 * struct rzg2l_du_device_info - DU model-specific information
 * @channels_mask: bit mask of available DU channels
 * @routes: array of CRTC to output routes, indexed by output (RZG2L_DU_OUTPUT_*)
 */
struct rzg2l_du_device_info {
	unsigned int channels_mask;
	struct rzg2l_du_output_routing routes[RZG2L_DU_OUTPUT_MAX];
};

#define RZG2L_DU_MAX_CRTCS		1
#define RZG2L_DU_MAX_VSPS		1
#define RZG2L_DU_MAX_DSI		1

struct rzg2l_du_device {
	struct device *dev;
	const struct rzg2l_du_device_info *info;

	void __iomem *mmio;

	struct drm_device ddev;

	struct rzg2l_du_crtc crtcs[RZG2L_DU_MAX_CRTCS];
	unsigned int num_crtcs;

	struct rzg2l_du_vsp vsps[RZG2L_DU_MAX_VSPS];
};

static inline struct rzg2l_du_device *to_rzg2l_du_device(struct drm_device *dev)
{
	return container_of(dev, struct rzg2l_du_device, ddev);
}

const char *rzg2l_du_output_name(enum rzg2l_du_output output);

#endif /* __RZG2L_DU_DRV_H__ */
