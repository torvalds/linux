/*
 * TI OMAP4 ISS V4L2 Driver - ISP IPIPEIF module
 *
 * Copyright (C) 2012 Texas Instruments, Inc.
 *
 * Author: Sergio Aguirre <sergio.a.aguirre@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef OMAP4_ISS_IPIPEIF_H
#define OMAP4_ISS_IPIPEIF_H

#include "iss_video.h"

enum ipipeif_input_entity {
	IPIPEIF_INPUT_NONE,
	IPIPEIF_INPUT_CSI2A,
	IPIPEIF_INPUT_CSI2B
};

#define IPIPEIF_OUTPUT_MEMORY			BIT(0)
#define IPIPEIF_OUTPUT_VP			BIT(1)

/* Sink and source IPIPEIF pads */
#define IPIPEIF_PAD_SINK			0
#define IPIPEIF_PAD_SOURCE_ISIF_SF		1
#define IPIPEIF_PAD_SOURCE_VP			2
#define IPIPEIF_PADS_NUM			3

/*
 * struct iss_ipipeif_device - Structure for the IPIPEIF module to store its own
 *			    information
 * @subdev: V4L2 subdevice
 * @pads: Sink and source media entity pads
 * @formats: Active video formats
 * @input: Active input
 * @output: Active outputs
 * @video_out: Output video node
 * @error: A hardware error occurred during capture
 * @alaw: A-law compression enabled (1) or disabled (0)
 * @lpf: Low pass filter enabled (1) or disabled (0)
 * @obclamp: Optical-black clamp enabled (1) or disabled (0)
 * @fpc_en: Faulty pixels correction enabled (1) or disabled (0)
 * @blcomp: Black level compensation configuration
 * @clamp: Optical-black or digital clamp configuration
 * @fpc: Faulty pixels correction configuration
 * @lsc: Lens shading compensation configuration
 * @update: Bitmask of controls to update during the next interrupt
 * @shadow_update: Controls update in progress by userspace
 * @syncif: Interface synchronization configuration
 * @vpcfg: Video port configuration
 * @underrun: A buffer underrun occurred and a new buffer has been queued
 * @state: Streaming state
 * @lock: Serializes shadow_update with interrupt handler
 * @wait: Wait queue used to stop the module
 * @stopping: Stopping state
 * @ioctl_lock: Serializes ioctl calls and LSC requests freeing
 */
struct iss_ipipeif_device {
	struct v4l2_subdev subdev;
	struct media_pad pads[IPIPEIF_PADS_NUM];
	struct v4l2_mbus_framefmt formats[IPIPEIF_PADS_NUM];

	enum ipipeif_input_entity input;
	unsigned int output;
	struct iss_video video_out;
	unsigned int error;

	enum iss_pipeline_stream_state state;
	wait_queue_head_t wait;
	atomic_t stopping;
};

struct iss_device;

int omap4iss_ipipeif_init(struct iss_device *iss);
void omap4iss_ipipeif_cleanup(struct iss_device *iss);
int omap4iss_ipipeif_register_entities(struct iss_ipipeif_device *ipipeif,
				       struct v4l2_device *vdev);
void omap4iss_ipipeif_unregister_entities(struct iss_ipipeif_device *ipipeif);

int omap4iss_ipipeif_busy(struct iss_ipipeif_device *ipipeif);
void omap4iss_ipipeif_isr(struct iss_ipipeif_device *ipipeif, u32 events);
void omap4iss_ipipeif_restore_context(struct iss_device *iss);
void omap4iss_ipipeif_max_rate(struct iss_ipipeif_device *ipipeif,
			       unsigned int *max_rate);

#endif	/* OMAP4_ISS_IPIPEIF_H */
