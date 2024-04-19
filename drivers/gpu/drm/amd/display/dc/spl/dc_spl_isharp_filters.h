// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#ifndef __DC_SPL_ISHARP_FILTERS_H__
#define __DC_SPL_ISHARP_FILTERS_H__

#include "dc_spl_types.h"

const uint32_t *spl_get_filter_isharp_1D_lut_0(void);
const uint32_t *spl_get_filter_isharp_1D_lut_0p5x(void);
const uint32_t *spl_get_filter_isharp_1D_lut_1p0x(void);
const uint32_t *spl_get_filter_isharp_1D_lut_1p5x(void);
const uint32_t *spl_get_filter_isharp_1D_lut_2p0x(void);
const uint32_t *spl_get_filter_isharp_bs_4tap_64p(void);
const uint32_t *spl_get_filter_isharp_wide_6tap_64p(void);
#endif /* __DC_SPL_ISHARP_FILTERS_H__ */
