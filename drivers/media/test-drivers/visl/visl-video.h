/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Contains the driver implementation for the V4L2 stateless interface.
 */

#ifndef _VISL_VIDEO_H_
#define _VISL_VIDEO_H_
#include <media/v4l2-mem2mem.h>

#include "visl.h"

extern const struct v4l2_ioctl_ops visl_ioctl_ops;

extern const struct visl_ctrls visl_fwht_ctrls;
extern const struct visl_ctrls visl_mpeg2_ctrls;
extern const struct visl_ctrls visl_vp8_ctrls;
extern const struct visl_ctrls visl_vp9_ctrls;
extern const struct visl_ctrls visl_h264_ctrls;
extern const struct visl_ctrls visl_hevc_ctrls;
extern const struct visl_ctrls visl_av1_ctrls;

int visl_queue_init(void *priv, struct vb2_queue *src_vq,
		    struct vb2_queue *dst_vq);

int visl_set_default_format(struct visl_ctx *ctx);
int visl_request_validate(struct media_request *req);

#endif /* _VISL_VIDEO_H_ */
