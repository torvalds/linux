/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Rockchip Video Decoder CABAC tables
 *
 * Copyright (C) 2023 Collabora, Ltd.
 *	Sebastian Fricke <sebastian.fricke@collabora.com>
 * Copyright (C) 2019 Collabora, Ltd.
 *	Boris Brezillon <boris.brezillon@collabora.com>
 */

#ifndef RKVDEC_CABAC_H_
#define RKVDEC_CABAC_H_

#include <linux/types.h>

#define RKV_HEVC_CABAC_TABLE_SIZE		27456

extern const s8 rkvdec_h264_cabac_table[4][464][2];
extern const u8 rkvdec_hevc_cabac_table[RKV_HEVC_CABAC_TABLE_SIZE];

#endif /* RKVDEC_CABAC_H_ */
