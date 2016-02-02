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

#ifndef FIJI_DYN_DEFAULTS_H
#define FIJI_DYN_DEFAULTS_H

/** \file
* Volcanic Islands Dynamic default parameters.
*/

enum FIJIdpm_TrendDetection
{
    FIJIAdpm_TrendDetection_AUTO,
    FIJIAdpm_TrendDetection_UP,
    FIJIAdpm_TrendDetection_DOWN
};
typedef enum FIJIdpm_TrendDetection FIJIdpm_TrendDetection;

/* We need to fill in the default values!!!!!!!!!!!!!!!!!!!!!!! */

/* Bit vector representing same fields as hardware register. */
#define PPFIJI_VOTINGRIGHTSCLIENTS_DFLT0    0x3FFFC102  /* CP_Gfx_busy ????
                                                         * HDP_busy
                                                         * IH_busy
                                                         * UVD_busy
                                                         * VCE_busy
                                                         * ACP_busy
                                                         * SAMU_busy
                                                         * SDMA enabled */
#define PPFIJI_VOTINGRIGHTSCLIENTS_DFLT1    0x000400  /* FE_Gfx_busy  - Intended for primary usage.   Rest are for flexibility. ????
                                                       * SH_Gfx_busy
                                                       * RB_Gfx_busy
                                                       * VCE_busy */

#define PPFIJI_VOTINGRIGHTSCLIENTS_DFLT2    0xC00080  /* SH_Gfx_busy - Intended for primary usage.   Rest are for flexibility.
                                                       * FE_Gfx_busy
                                                       * RB_Gfx_busy
                                                       * ACP_busy */

#define PPFIJI_VOTINGRIGHTSCLIENTS_DFLT3    0xC00200  /* RB_Gfx_busy - Intended for primary usage.   Rest are for flexibility.
                                                       * FE_Gfx_busy
                                                       * SH_Gfx_busy
                                                       * UVD_busy */

#define PPFIJI_VOTINGRIGHTSCLIENTS_DFLT4    0xC01680  /* UVD_busy
                                                       * VCE_busy
                                                       * ACP_busy
                                                       * SAMU_busy */

#define PPFIJI_VOTINGRIGHTSCLIENTS_DFLT5    0xC00033  /* GFX, HDP */
#define PPFIJI_VOTINGRIGHTSCLIENTS_DFLT6    0xC00033  /* GFX, HDP */
#define PPFIJI_VOTINGRIGHTSCLIENTS_DFLT7    0x3FFFC000  /* GFX, HDP */


/* thermal protection counter (units). */
#define PPFIJI_THERMALPROTECTCOUNTER_DFLT            0x200 /* ~19us */

/* static screen threshold unit */
#define PPFIJI_STATICSCREENTHRESHOLDUNIT_DFLT    0

/* static screen threshold */
#define PPFIJI_STATICSCREENTHRESHOLD_DFLT        0x00C8

/* gfx idle clock stop threshold */
#define PPFIJI_GFXIDLECLOCKSTOPTHRESHOLD_DFLT        0x200 /* ~19us with static screen threshold unit of 0 */

/* Fixed reference divider to use when building baby stepping tables. */
#define PPFIJI_REFERENCEDIVIDER_DFLT                  4

/* ULV voltage change delay time
 * Used to be delay_vreg in N.I. split for S.I.
 * Using N.I. delay_vreg value as default
 * ReferenceClock = 2700
 * VoltageResponseTime = 1000
 * VDDCDelayTime = (VoltageResponseTime * ReferenceClock) / 1600 = 1687
 */
#define PPFIJI_ULVVOLTAGECHANGEDELAY_DFLT             1687

#define PPFIJI_CGULVPARAMETER_DFLT			0x00040035
#define PPFIJI_CGULVCONTROL_DFLT			0x00007450
#define PPFIJI_TARGETACTIVITY_DFLT			30 /* 30%*/
#define PPFIJI_MCLK_TARGETACTIVITY_DFLT		10 /* 10% */

#endif

