/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _DPU_WRITEBACK_H
#define _DPU_WRITEBACK_H

#include <drm/drm_crtc.h>
#include <drm/drm_file.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_writeback.h>

#include "msm_drv.h"
#include "dpu_kms.h"
#include "dpu_encoder_phys.h"

struct dpu_wb_connector {
	struct drm_writeback_connector base;
	struct drm_encoder *wb_enc;
};

static inline struct dpu_wb_connector *to_dpu_wb_conn(struct drm_writeback_connector *conn)
{
	return container_of(conn, struct dpu_wb_connector, base);
}

int dpu_writeback_init(struct drm_device *dev, struct drm_encoder *enc,
		const u32 *format_list, u32 num_formats);

#endif /*_DPU_WRITEBACK_H */
