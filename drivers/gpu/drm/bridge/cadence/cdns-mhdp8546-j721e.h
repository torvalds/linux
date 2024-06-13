/* SPDX-License-Identifier: GPL-2.0 */
/*
 * TI j721e Cadence MHDP8546 DP wrapper
 *
 * Copyright (C) 2020 Texas Instruments Incorporated - http://www.ti.com/
 * Author: Jyri Sarha <jsarha@ti.com>
 */

#ifndef CDNS_MHDP8546_J721E_H
#define CDNS_MHDP8546_J721E_H

#include "cdns-mhdp8546-core.h"

struct mhdp_platform_ops;

extern const struct mhdp_platform_ops mhdp_ti_j721e_ops;
extern const struct drm_bridge_timings mhdp_ti_j721e_bridge_timings;

#endif /* !CDNS_MHDP8546_J721E_H */
