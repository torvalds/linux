/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Rockchip Camera Interface (CIF) Driver
 *
 * Abstraction for the DMA part and the ping-pong scheme (a double-buffering
 * mechanism) of the different CIF variants.
 * Each stream is represented as V4L2 device whose corresponding media entity
 * has one sink pad.
 * The sink pad is connected to an instance of the INTERFACE/CROP abstraction
 * in rkcif-interface.c.
 *
 * Copyright (C) 2025 Michael Riesch <michael.riesch@wolfvision.net>
 * Copyright (C) 2025 Collabora, Ltd.
 */

#ifndef _RKCIF_STREAM_H
#define _RKCIF_STREAM_H

#include "rkcif-common.h"

void rkcif_stream_pingpong(struct rkcif_stream *stream);

int rkcif_stream_register(struct rkcif_device *rkcif,
			  struct rkcif_stream *stream);

void rkcif_stream_unregister(struct rkcif_stream *stream);

const struct rkcif_output_fmt *
rkcif_stream_find_output_fmt(struct rkcif_stream *stream, bool ret_def,
			     u32 pixelfmt);

#endif
