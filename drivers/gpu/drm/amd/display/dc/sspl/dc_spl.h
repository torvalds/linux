// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#ifndef __DC_SPL_H__
#define __DC_SPL_H__

#include "dc_spl_types.h"
#define BLACK_OFFSET_RGB_Y 0x0
#define BLACK_OFFSET_CBCR  0x8000

#ifndef SPL_PFX_
#define SPL_PFX_
#endif

#define SPL_EXPAND2(a, b)         a##b
#define SPL_EXPAND(a, b)          SPL_EXPAND2(a, b)
#define SPL_NAMESPACE(symbol)     SPL_EXPAND(SPL_PFX_, symbol)


/* SPL interfaces */

bool SPL_NAMESPACE(spl_calculate_scaler_params(struct spl_in *spl_in, struct spl_out *spl_out));

bool SPL_NAMESPACE(spl_get_number_of_taps(struct spl_in *spl_in, struct spl_out *spl_out));

#endif /* __DC_SPL_H__ */
