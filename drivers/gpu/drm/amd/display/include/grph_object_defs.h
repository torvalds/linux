/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
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
 * Authors: AMD
 *
 */

#ifndef __DAL_GRPH_OBJECT_DEFS_H__
#define __DAL_GRPH_OBJECT_DEFS_H__

#include "grph_object_id.h"

/* ********************************************************************
 * ********************************************************************
 *
 *  These defines shared between All Graphics Objects
 *
 * ********************************************************************
 * ********************************************************************
 */

/* HPD unit id - HW direct translation */
enum hpd_source_id {
	HPD_SOURCEID1 = 0,
	HPD_SOURCEID2,
	HPD_SOURCEID3,
	HPD_SOURCEID4,
	HPD_SOURCEID5,
	HPD_SOURCEID6,

	HPD_SOURCEID_COUNT,
	HPD_SOURCEID_UNKNOWN
};

/* DDC unit id - HW direct translation */
enum channel_id {
	CHANNEL_ID_UNKNOWN = 0,
	CHANNEL_ID_DDC1,
	CHANNEL_ID_DDC2,
	CHANNEL_ID_DDC3,
	CHANNEL_ID_DDC4,
	CHANNEL_ID_DDC5,
	CHANNEL_ID_DDC6,
	CHANNEL_ID_DDC_VGA,
	CHANNEL_ID_I2C_PAD,
	CHANNEL_ID_COUNT
};

#define DECODE_CHANNEL_ID(ch_id) \
	(ch_id) == CHANNEL_ID_DDC1 ? "CHANNEL_ID_DDC1" : \
	(ch_id) == CHANNEL_ID_DDC2 ? "CHANNEL_ID_DDC2" : \
	(ch_id) == CHANNEL_ID_DDC3 ? "CHANNEL_ID_DDC3" : \
	(ch_id) == CHANNEL_ID_DDC4 ? "CHANNEL_ID_DDC4" : \
	(ch_id) == CHANNEL_ID_DDC5 ? "CHANNEL_ID_DDC5" : \
	(ch_id) == CHANNEL_ID_DDC6 ? "CHANNEL_ID_DDC6" : \
	(ch_id) == CHANNEL_ID_DDC_VGA ? "CHANNEL_ID_DDC_VGA" : \
	(ch_id) == CHANNEL_ID_I2C_PAD ? "CHANNEL_ID_I2C_PAD" : "Invalid"

enum transmitter {
	TRANSMITTER_UNKNOWN = (-1L),
	TRANSMITTER_UNIPHY_A,
	TRANSMITTER_UNIPHY_B,
	TRANSMITTER_UNIPHY_C,
	TRANSMITTER_UNIPHY_D,
	TRANSMITTER_UNIPHY_E,
	TRANSMITTER_UNIPHY_F,
	TRANSMITTER_NUTMEG_CRT,
	TRANSMITTER_TRAVIS_CRT,
	TRANSMITTER_TRAVIS_LCD,
	TRANSMITTER_UNIPHY_G,
	TRANSMITTER_COUNT
};

/* Generic source of the synchronisation input/output signal */
/* Can be used for flow control, stereo sync, timing sync, frame sync, etc */
enum sync_source {
	SYNC_SOURCE_NONE = 0,

	/* Source based on controllers */
	SYNC_SOURCE_CONTROLLER0,
	SYNC_SOURCE_CONTROLLER1,
	SYNC_SOURCE_CONTROLLER2,
	SYNC_SOURCE_CONTROLLER3,
	SYNC_SOURCE_CONTROLLER4,
	SYNC_SOURCE_CONTROLLER5,

	/* Source based on GSL group */
	SYNC_SOURCE_GSL_GROUP0,
	SYNC_SOURCE_GSL_GROUP1,
	SYNC_SOURCE_GSL_GROUP2,

	/* Source based on GSL IOs */
	/* These IOs normally used as GSL input/output */
	SYNC_SOURCE_GSL_IO_FIRST,
	SYNC_SOURCE_GSL_IO_GENLOCK_CLOCK = SYNC_SOURCE_GSL_IO_FIRST,
	SYNC_SOURCE_GSL_IO_GENLOCK_VSYNC,
	SYNC_SOURCE_GSL_IO_SWAPLOCK_A,
	SYNC_SOURCE_GSL_IO_SWAPLOCK_B,
	SYNC_SOURCE_GSL_IO_LAST = SYNC_SOURCE_GSL_IO_SWAPLOCK_B,

	/* Source based on regular IOs */
	SYNC_SOURCE_IO_FIRST,
	SYNC_SOURCE_IO_GENERIC_A = SYNC_SOURCE_IO_FIRST,
	SYNC_SOURCE_IO_GENERIC_B,
	SYNC_SOURCE_IO_GENERIC_C,
	SYNC_SOURCE_IO_GENERIC_D,
	SYNC_SOURCE_IO_GENERIC_E,
	SYNC_SOURCE_IO_GENERIC_F,
	SYNC_SOURCE_IO_HPD1,
	SYNC_SOURCE_IO_HPD2,
	SYNC_SOURCE_IO_HSYNC_A,
	SYNC_SOURCE_IO_VSYNC_A,
	SYNC_SOURCE_IO_HSYNC_B,
	SYNC_SOURCE_IO_VSYNC_B,
	SYNC_SOURCE_IO_LAST = SYNC_SOURCE_IO_VSYNC_B,

	/* Misc. flow control sources */
	SYNC_SOURCE_DUAL_GPU_PIN
};


#endif
