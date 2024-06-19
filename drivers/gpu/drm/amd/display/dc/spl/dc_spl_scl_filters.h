// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#ifndef __DC_SPL_SCL_FILTERS_H__
#define __DC_SPL_SCL_FILTERS_H__

#include "dc_spl_types.h"

const uint16_t *spl_get_filter_3tap_16p(struct fixed31_32 ratio);
const uint16_t *spl_get_filter_3tap_64p(struct fixed31_32 ratio);
const uint16_t *spl_get_filter_4tap_16p(struct fixed31_32 ratio);
const uint16_t *spl_get_filter_4tap_64p(struct fixed31_32 ratio);
const uint16_t *spl_get_filter_5tap_64p(struct fixed31_32 ratio);
const uint16_t *spl_get_filter_6tap_64p(struct fixed31_32 ratio);
const uint16_t *spl_get_filter_7tap_64p(struct fixed31_32 ratio);
const uint16_t *spl_get_filter_8tap_64p(struct fixed31_32 ratio);
const uint16_t *spl_get_filter_2tap_16p(void);
const uint16_t *spl_get_filter_2tap_64p(void);
const uint16_t *spl_get_filter_3tap_16p_upscale(void);
const uint16_t *spl_get_filter_3tap_16p_116(void);
const uint16_t *spl_get_filter_3tap_16p_149(void);
const uint16_t *spl_get_filter_3tap_16p_183(void);

const uint16_t *spl_get_filter_4tap_16p_upscale(void);
const uint16_t *spl_get_filter_4tap_16p_116(void);
const uint16_t *spl_get_filter_4tap_16p_149(void);
const uint16_t *spl_get_filter_4tap_16p_183(void);

const uint16_t *spl_get_filter_3tap_64p_upscale(void);
const uint16_t *spl_get_filter_3tap_64p_116(void);
const uint16_t *spl_get_filter_3tap_64p_149(void);
const uint16_t *spl_get_filter_3tap_64p_183(void);

const uint16_t *spl_get_filter_4tap_64p_upscale(void);
const uint16_t *spl_get_filter_4tap_64p_116(void);
const uint16_t *spl_get_filter_4tap_64p_149(void);
const uint16_t *spl_get_filter_4tap_64p_183(void);

const uint16_t *spl_get_filter_5tap_64p_upscale(void);
const uint16_t *spl_get_filter_5tap_64p_116(void);
const uint16_t *spl_get_filter_5tap_64p_149(void);
const uint16_t *spl_get_filter_5tap_64p_183(void);

const uint16_t *spl_get_filter_6tap_64p_upscale(void);
const uint16_t *spl_get_filter_6tap_64p_116(void);
const uint16_t *spl_get_filter_6tap_64p_149(void);
const uint16_t *spl_get_filter_6tap_64p_183(void);

const uint16_t *spl_get_filter_7tap_64p_upscale(void);
const uint16_t *spl_get_filter_7tap_64p_116(void);
const uint16_t *spl_get_filter_7tap_64p_149(void);
const uint16_t *spl_get_filter_7tap_64p_183(void);

const uint16_t *spl_get_filter_8tap_64p_upscale(void);
const uint16_t *spl_get_filter_8tap_64p_116(void);
const uint16_t *spl_get_filter_8tap_64p_149(void);
const uint16_t *spl_get_filter_8tap_64p_183(void);
#endif /* __DC_SPL_SCL_FILTERS_H__ */
