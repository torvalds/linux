/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2013--2024 Intel Corporation */

#ifndef IPU6_ISYS_CSI2_H
#define IPU6_ISYS_CSI2_H

#include <linux/container_of.h>

#include "ipu6-isys-subdev.h"
#include "ipu6-isys-video.h"

struct media_entity;
struct v4l2_mbus_frame_desc_entry;

struct ipu6_isys_video;
struct ipu6_isys;
struct ipu6_isys_csi2_pdata;
struct ipu6_isys_stream;

#define NR_OF_CSI2_VC		16
#define INVALID_VC_ID		-1
#define NR_OF_CSI2_SINK_PADS	1
#define CSI2_PAD_SINK		0
#define NR_OF_CSI2_SRC_PADS	8
#define CSI2_PAD_SRC		1
#define NR_OF_CSI2_PADS		(NR_OF_CSI2_SINK_PADS + NR_OF_CSI2_SRC_PADS)

#define CSI2_CSI_RX_DLY_CNT_TERMEN_CLANE_A		0
#define CSI2_CSI_RX_DLY_CNT_TERMEN_CLANE_B		0
#define CSI2_CSI_RX_DLY_CNT_SETTLE_CLANE_A		95
#define CSI2_CSI_RX_DLY_CNT_SETTLE_CLANE_B		-8

#define CSI2_CSI_RX_DLY_CNT_TERMEN_DLANE_A		0
#define CSI2_CSI_RX_DLY_CNT_TERMEN_DLANE_B		0
#define CSI2_CSI_RX_DLY_CNT_SETTLE_DLANE_A		85
#define CSI2_CSI_RX_DLY_CNT_SETTLE_DLANE_B		-2

struct ipu6_isys_csi2 {
	struct ipu6_isys_subdev asd;
	struct ipu6_isys_csi2_pdata *pdata;
	struct ipu6_isys *isys;
	struct ipu6_isys_video av[NR_OF_CSI2_SRC_PADS];

	void __iomem *base;
	u32 receiver_errors;
	unsigned int nlanes;
	unsigned int port;
	unsigned int stream_count;
};

struct ipu6_isys_csi2_timing {
	u32 ctermen;
	u32 csettle;
	u32 dtermen;
	u32 dsettle;
};

struct ipu6_csi2_error {
	const char *error_string;
	bool is_info_only;
};

#define ipu6_isys_subdev_to_csi2(__sd) \
	container_of(__sd, struct ipu6_isys_csi2, asd)

#define to_ipu6_isys_csi2(__asd) container_of(__asd, struct ipu6_isys_csi2, asd)

s64 ipu6_isys_csi2_get_link_freq(struct ipu6_isys_csi2 *csi2);
int ipu6_isys_csi2_init(struct ipu6_isys_csi2 *csi2, struct ipu6_isys *isys,
			void __iomem *base, unsigned int index);
void ipu6_isys_csi2_cleanup(struct ipu6_isys_csi2 *csi2);
void ipu6_isys_csi2_sof_event_by_stream(struct ipu6_isys_stream *stream);
void ipu6_isys_csi2_eof_event_by_stream(struct ipu6_isys_stream *stream);
void ipu6_isys_register_errors(struct ipu6_isys_csi2 *csi2);
void ipu6_isys_csi2_error(struct ipu6_isys_csi2 *csi2);
int ipu6_isys_csi2_get_remote_desc(u32 source_stream,
				   struct ipu6_isys_csi2 *csi2,
				   struct media_entity *source_entity,
				   struct v4l2_mbus_frame_desc_entry *entry);
void ipu6_isys_set_csi2_streams_status(struct ipu6_isys_video *av, bool status);

#endif /* IPU6_ISYS_CSI2_H */
