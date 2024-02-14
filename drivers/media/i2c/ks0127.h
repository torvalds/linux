/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Video Capture Driver ( Video for Linux 1/2 )
 * for the Matrox Marvel G200,G400 and Rainbow Runner-G series
 *
 * This module is an interface to the KS0127 video decoder chip.
 *
 * Copyright (C) 1999  Ryan Drake <stiletto@mediaone.net>
 */

#ifndef KS0127_H
#define KS0127_H

/* input channels */
#define KS_INPUT_COMPOSITE_1    0
#define KS_INPUT_COMPOSITE_2    1
#define KS_INPUT_COMPOSITE_3    2
#define KS_INPUT_COMPOSITE_4    4
#define KS_INPUT_COMPOSITE_5    5
#define KS_INPUT_COMPOSITE_6    6

#define KS_INPUT_SVIDEO_1       8
#define KS_INPUT_SVIDEO_2       9
#define KS_INPUT_SVIDEO_3       10

#define KS_INPUT_YUV656		15
#define KS_INPUT_COUNT          10

/* output channels */
#define KS_OUTPUT_YUV656E       0
#define KS_OUTPUT_EXV           1

/* video standards */
#define KS_STD_NTSC_N           112       /* 50 Hz NTSC */
#define KS_STD_PAL_M            113       /* 60 Hz PAL  */

#endif /* KS0127_H */

