/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Rockchip Camera Interface (CIF) Driver
 *
 * Copyright (C) 2018 Rockchip Electronics Co., Ltd.
 * Copyright (C) 2025 Michael Riesch <michael.riesch@wolfvision.net>
 * Copyright (C) 2025 Collabora, Ltd.
 */

#ifndef _RKCIF_CAPTURE_MIPI_H
#define _RKCIF_CAPTURE_MIPI_H

#include "rkcif-common.h"

extern const struct rkcif_mipi_match_data rkcif_rk3568_vicap_mipi_match_data;

int rkcif_mipi_register(struct rkcif_device *rkcif);

void rkcif_mipi_unregister(struct rkcif_device *rkcif);

irqreturn_t rkcif_mipi_isr(int irq, void *ctx);

#endif
