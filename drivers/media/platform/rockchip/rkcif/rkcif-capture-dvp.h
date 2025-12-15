/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Rockchip Camera Interface (CIF) Driver
 *
 * Copyright (C) 2018 Rockchip Electronics Co., Ltd.
 * Copyright (C) 2023 Mehdi Djait <mehdi.djait@bootlin.com>
 * Copyright (C) 2025 Michael Riesch <michael.riesch@wolfvision.net>
 * Copyright (C) 2025 Collabora, Ltd.
 */

#ifndef _RKCIF_CAPTURE_DVP_H
#define _RKCIF_CAPTURE_DVP_H

#include "rkcif-common.h"

extern const struct rkcif_dvp_match_data rkcif_px30_vip_dvp_match_data;
extern const struct rkcif_dvp_match_data rkcif_rk3568_vicap_dvp_match_data;

int rkcif_dvp_register(struct rkcif_device *rkcif);

void rkcif_dvp_unregister(struct rkcif_device *rkcif);

irqreturn_t rkcif_dvp_isr(int irq, void *ctx);

#endif
