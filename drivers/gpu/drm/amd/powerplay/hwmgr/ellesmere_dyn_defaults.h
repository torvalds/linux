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

#ifndef ELLESMERE_DYN_DEFAULTS_H
#define ELLESMERE_DYN_DEFAULTS_H


enum Ellesmeredpm_TrendDetection {
	EllesmereAdpm_TrendDetection_AUTO,
	EllesmereAdpm_TrendDetection_UP,
	EllesmereAdpm_TrendDetection_DOWN
};
typedef enum Ellesmeredpm_TrendDetection Ellesmeredpm_TrendDetection;

/*  We need to fill in the default values */


#define PPELLESMERE_VOTINGRIGHTSCLIENTS_DFLT0              0x3FFFC102
#define PPELLESMERE_VOTINGRIGHTSCLIENTS_DFLT1              0x000400
#define PPELLESMERE_VOTINGRIGHTSCLIENTS_DFLT2              0xC00080
#define PPELLESMERE_VOTINGRIGHTSCLIENTS_DFLT3              0xC00200
#define PPELLESMERE_VOTINGRIGHTSCLIENTS_DFLT4              0xC01680
#define PPELLESMERE_VOTINGRIGHTSCLIENTS_DFLT5              0xC00033
#define PPELLESMERE_VOTINGRIGHTSCLIENTS_DFLT6              0xC00033
#define PPELLESMERE_VOTINGRIGHTSCLIENTS_DFLT7              0x3FFFC000


#define PPELLESMERE_THERMALPROTECTCOUNTER_DFLT            0x200
#define PPELLESMERE_STATICSCREENTHRESHOLDUNIT_DFLT        0
#define PPELLESMERE_STATICSCREENTHRESHOLD_DFLT            0x00C8
#define PPELLESMERE_GFXIDLECLOCKSTOPTHRESHOLD_DFLT        0x200
#define PPELLESMERE_REFERENCEDIVIDER_DFLT                  4

#define PPELLESMERE_ULVVOLTAGECHANGEDELAY_DFLT             1687

#define PPELLESMERE_CGULVPARAMETER_DFLT                    0x00040035
#define PPELLESMERE_CGULVCONTROL_DFLT                      0x00007450
#define PPELLESMERE_TARGETACTIVITY_DFLT                     50
#define PPELLESMERE_MCLK_TARGETACTIVITY_DFLT                10

#endif

