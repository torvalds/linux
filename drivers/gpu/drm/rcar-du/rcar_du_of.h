/* SPDX-License-Identifier: GPL-2.0 */
/*
 * rcar_du_of.h - Legacy DT bindings compatibility
 *
 * Copyright (C) 2018 Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 */
#ifndef __RCAR_DU_OF_H__
#define __RCAR_DU_OF_H__

#include <linux/init.h>

struct of_device_id;

#ifdef CONFIG_DRM_RCAR_LVDS
void __init rcar_du_of_init(const struct of_device_id *of_ids);
#else
static inline void rcar_du_of_init(const struct of_device_id *of_ids) { }
#endif /* CONFIG_DRM_RCAR_LVDS */

#endif /* __RCAR_DU_OF_H__ */
