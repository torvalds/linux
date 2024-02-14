/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/* Copyright (c) 2023 Imagination Technologies Ltd. */

#ifndef PVR_STREAM_DEFS_H
#define PVR_STREAM_DEFS_H

#include "pvr_stream.h"

extern const struct pvr_stream_cmd_defs pvr_cmd_geom_stream;
extern const struct pvr_stream_cmd_defs pvr_cmd_frag_stream;
extern const struct pvr_stream_cmd_defs pvr_cmd_compute_stream;
extern const struct pvr_stream_cmd_defs pvr_cmd_transfer_stream;
extern const struct pvr_stream_cmd_defs pvr_static_render_context_state_stream;
extern const struct pvr_stream_cmd_defs pvr_static_compute_context_state_stream;

#endif /* PVR_STREAM_DEFS_H */
