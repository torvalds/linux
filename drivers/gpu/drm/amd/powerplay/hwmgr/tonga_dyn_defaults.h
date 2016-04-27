/*
 * Copyright 2015 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */
#ifndef TONGA_DYN_DEFAULTS_H
#define TONGA_DYN_DEFAULTS_H


/** \file
 * Volcanic Islands Dynamic default parameters.
 */

enum TONGAdpm_TrendDetection {
	TONGAdpm_TrendDetection_AUTO,
	TONGAdpm_TrendDetection_UP,
	TONGAdpm_TrendDetection_DOWN
};
typedef enum TONGAdpm_TrendDetection TONGAdpm_TrendDetection;

/* Bit vector representing same fields as hardware register. */
#define PPTONGA_VOTINGRIGHTSCLIENTS_DFLT0              0x3FFFC102  /* CP_Gfx_busy */
/* HDP_busy */
/* IH_busy */
/* DRM_busy */
/* DRMDMA_busy */
/* UVD_busy */
/* VCE_busy */
/* ACP_busy */
/* SAMU_busy */
/* AVP_busy  */
/* SDMA enabled */
#define PPTONGA_VOTINGRIGHTSCLIENTS_DFLT1              0x000400  /* FE_Gfx_busy  - Intended for primary usage.   Rest are for flexibility. */
/* SH_Gfx_busy */
/* RB_Gfx_busy */
/* VCE_busy */

#define PPTONGA_VOTINGRIGHTSCLIENTS_DFLT2              0xC00080  /* SH_Gfx_busy - Intended for primary usage.   Rest are for flexibility. */
/* FE_Gfx_busy */
/* RB_Gfx_busy */
/* ACP_busy */

#define PPTONGA_VOTINGRIGHTSCLIENTS_DFLT3              0xC00200  /* RB_Gfx_busy - Intended for primary usage.   Rest are for flexibility. */
/* FE_Gfx_busy */
/* SH_Gfx_busy */
/* UVD_busy */

#define PPTONGA_VOTINGRIGHTSCLIENTS_DFLT4              0xC01680  /* UVD_busy */
/* VCE_busy */
/* ACP_busy */
/* SAMU_busy */

#define PPTONGA_VOTINGRIGHTSCLIENTS_DFLT5              0xC00033  /* GFX, HDP, DRMDMA */
#define PPTONGA_VOTINGRIGHTSCLIENTS_DFLT6              0xC00033  /* GFX, HDP, DRMDMA */
#define PPTONGA_VOTINGRIGHTSCLIENTS_DFLT7              0x3FFFC000  /* GFX, HDP, DRMDMA */


/* thermal protection counter (units).*/
#define PPTONGA_THERMALPROTECTCOUNTER_DFLT            0x200 /* ~19us */

/* static screen threshold unit */
#define PPTONGA_STATICSCREENTHRESHOLDUNIT_DFLT        0

/* static screen threshold */
#define PPTONGA_STATICSCREENTHRESHOLD_DFLT            0x00C8

/* gfx idle clock stop threshold */
#define PPTONGA_GFXIDLECLOCKSTOPTHRESHOLD_DFLT        0x200 /* ~19us with static screen threshold unit of 0 */

/* Fixed reference divider to use when building baby stepping tables. */
#define PPTONGA_REFERENCEDIVIDER_DFLT                  4

/*
 * ULV voltage change delay time
 * Used to be delay_vreg in N.I. split for S.I.
 * Using N.I. delay_vreg value as default
 * ReferenceClock = 2700
 * VoltageResponseTime = 1000
 * VDDCDelayTime = (VoltageResponseTime * ReferenceClock) / 1600 = 1687
 */

#define PPTONGA_ULVVOLTAGECHANGEDELAY_DFLT             1687

#define PPTONGA_CGULVPARAMETER_DFLT                    0x00040035
#define PPTONGA_CGULVCONTROL_DFLT                      0x00007450
#define PPTONGA_TARGETACTIVITY_DFLT                     30  /*30%  */
#define PPTONGA_MCLK_TARGETACTIVITY_DFLT                10  /*10%  */

#endif

