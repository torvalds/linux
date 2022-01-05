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

#define MAX_CONNECTOR_NUMBER_PER_SLOT	(16)
#define MAX_BOARD_SLOTS					(4)
#define INVALID_CONNECTOR_INDEX			((unsigned int)(-1))

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

#if defined(CONFIG_DRM_AMD_DC_DCN)
enum tx_ffe_id {
	TX_FFE0 = 0,
	TX_FFE1,
	TX_FFE2,
	TX_FFE3,
	TX_FFE_DeEmphasis_Only,
	TX_FFE_PreShoot_Only,
	TX_FFE_No_FFE,
};
#endif

/* connector sizes in millimeters - from BiosParserTypes.hpp */
#define CONNECTOR_SIZE_DVI			40
#define CONNECTOR_SIZE_VGA			32
#define CONNECTOR_SIZE_HDMI			16
#define CONNECTOR_SIZE_DP			16
#define CONNECTOR_SIZE_MINI_DP			9
#define CONNECTOR_SIZE_UNKNOWN			30

enum connector_layout_type {
	CONNECTOR_LAYOUT_TYPE_UNKNOWN,
	CONNECTOR_LAYOUT_TYPE_DVI_D,
	CONNECTOR_LAYOUT_TYPE_DVI_I,
	CONNECTOR_LAYOUT_TYPE_VGA,
	CONNECTOR_LAYOUT_TYPE_HDMI,
	CONNECTOR_LAYOUT_TYPE_DP,
	CONNECTOR_LAYOUT_TYPE_MINI_DP,
};
struct connector_layout_info {
	struct graphics_object_id connector_id;
	enum connector_layout_type connector_type;
	unsigned int length;
	unsigned int position;  /* offset in mm from right side of the board */
};

/* length and width in mm */
struct slot_layout_info {
	unsigned int length;
	unsigned int width;
	unsigned int num_of_connectors;
	struct connector_layout_info connectors[MAX_CONNECTOR_NUMBER_PER_SLOT];
};

struct board_layout_info {
	unsigned int num_of_slots;

	/* indicates valid information in bracket layout structure. */
	unsigned int is_number_of_slots_valid : 1;
	unsigned int is_slots_size_valid : 1;
	unsigned int is_connector_offsets_valid : 1;
	unsigned int is_connector_lengths_valid : 1;

	struct slot_layout_info slots[MAX_BOARD_SLOTS];
};
#endif
