// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#ifndef __DC_SPL_SCL_FILTERS_H__
#define __DC_SPL_SCL_FILTERS_H__

#include "dc_spl_types.h"

/* public API */
const uint16_t *spl_dscl_get_filter_coeffs_64p(int taps, struct spl_fixed31_32 ratio);

#endif /* __DC_SPL_SCL_FILTERS_H__ */
