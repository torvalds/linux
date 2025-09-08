/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_ANR_TYPES_H
#define __IA_CSS_ANR_TYPES_H

/* @file
 * CSS-API header file for Advanced Noise Reduction kernel v1
 */

#include <linux/math.h>

/* Application specific DMA settings  */
#define ANR_BPP                 10
#define ANR_ELEMENT_BITS        round_up(ANR_BPP, 8)

/* Advanced Noise Reduction configuration.
 *  This is also known as Low-Light.
 */
struct ia_css_anr_config {
	s32 threshold; /** Threshold */
	s32 thresholds[4 * 4 * 4];
	s32 factors[3];
};

#endif /* __IA_CSS_ANR_TYPES_H */
