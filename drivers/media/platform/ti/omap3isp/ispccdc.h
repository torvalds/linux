/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * ispccdc.h
 *
 * TI OMAP3 ISP - CCDC module
 *
 * Copyright (C) 2009-2010 Nokia Corporation
 * Copyright (C) 2009 Texas Instruments, Inc.
 *
 * Contacts: Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *	     Sakari Ailus <sakari.ailus@iki.fi>
 */

#ifndef OMAP3_ISP_CCDC_H
#define OMAP3_ISP_CCDC_H

#include <linux/omap3isp.h>
#include <linux/workqueue.h>

#include "ispvideo.h"

enum ccdc_input_entity {
	CCDC_INPUT_NONE,
	CCDC_INPUT_PARALLEL,
	CCDC_INPUT_CSI2A,
	CCDC_INPUT_CCP2B,
	CCDC_INPUT_CSI2C
};

#define CCDC_OUTPUT_MEMORY	(1 << 0)
#define CCDC_OUTPUT_PREVIEW	(1 << 1)
#define CCDC_OUTPUT_RESIZER	(1 << 2)

#define	OMAP3ISP_CCDC_NEVENTS	16

struct ispccdc_fpc {
	void *addr;
	dma_addr_t dma;
	unsigned int fpnum;
};

enum ispccdc_lsc_state {
	LSC_STATE_STOPPED = 0,
	LSC_STATE_STOPPING = 1,
	LSC_STATE_RUNNING = 2,
	LSC_STATE_RECONFIG = 3,
};

struct ispccdc_lsc_config_req {
	struct list_head list;
	struct omap3isp_ccdc_lsc_config config;
	unsigned char enable;

	struct {
		void *addr;
		dma_addr_t dma;
		struct sg_table sgt;
	} table;
};

/*
 * ispccdc_lsc - CCDC LSC parameters
 */
struct ispccdc_lsc {
	enum ispccdc_lsc_state state;
	struct work_struct table_work;

	/* LSC queue of configurations */
	spinlock_t req_lock;
	struct ispccdc_lsc_config_req *request;	/* requested configuration */
	struct ispccdc_lsc_config_req *active;	/* active configuration */
	struct list_head free_queue;	/* configurations for freeing */
};

#define CCDC_STOP_NOT_REQUESTED		0x00
#define CCDC_STOP_REQUEST		0x01
#define CCDC_STOP_EXECUTED		(0x02 | CCDC_STOP_REQUEST)
#define CCDC_STOP_CCDC_FINISHED		0x04
#define CCDC_STOP_LSC_FINISHED		0x08
#define CCDC_STOP_FINISHED		\
	(CCDC_STOP_EXECUTED | CCDC_STOP_CCDC_FINISHED | CCDC_STOP_LSC_FINISHED)

#define CCDC_EVENT_VD1			0x10
#define CCDC_EVENT_VD0			0x20
#define CCDC_EVENT_LSC_DONE		0x40

/* Sink and source CCDC pads */
#define CCDC_PAD_SINK			0
#define CCDC_PAD_SOURCE_OF		1
#define CCDC_PAD_SOURCE_VP		2
#define CCDC_PADS_NUM			3

#define CCDC_FIELD_TOP			1
#define CCDC_FIELD_BOTTOM		2
#define CCDC_FIELD_BOTH			3

/*
 * struct isp_ccdc_device - Structure for the CCDC module to store its own
 *			    information
 * @subdev: V4L2 subdevice
 * @pads: Sink and source media entity pads
 * @formats: Active video formats
 * @crop: Active crop rectangle on the OF source pad
 * @input: Active input
 * @output: Active outputs
 * @video_out: Output video node
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
 * @bt656: Whether the input interface uses BT.656 synchronization
 * @fields: The fields (CCDC_FIELD_*) stored in the current buffer
 * @underrun: A buffer underrun occurred and a new buffer has been queued
 * @state: Streaming state
 * @lock: Serializes shadow_update with interrupt handler
 * @wait: Wait queue used to stop the module
 * @stopping: Stopping state
 * @running: Is the CCDC hardware running
 * @ioctl_lock: Serializes ioctl calls and LSC requests freeing
 */
struct isp_ccdc_device {
	struct v4l2_subdev subdev;
	struct media_pad pads[CCDC_PADS_NUM];
	struct v4l2_mbus_framefmt formats[CCDC_PADS_NUM];
	struct v4l2_rect crop;

	enum ccdc_input_entity input;
	unsigned int output;
	struct isp_video video_out;

	unsigned int alaw:1,
		     lpf:1,
		     obclamp:1,
		     fpc_en:1;
	struct omap3isp_ccdc_blcomp blcomp;
	struct omap3isp_ccdc_bclamp clamp;
	struct ispccdc_fpc fpc;
	struct ispccdc_lsc lsc;
	unsigned int update;
	unsigned int shadow_update;

	bool bt656;
	unsigned int fields;

	unsigned int underrun:1;
	enum isp_pipeline_stream_state state;
	spinlock_t lock;
	wait_queue_head_t wait;
	unsigned int stopping;
	bool running;
	struct mutex ioctl_lock;
};

struct isp_device;

int omap3isp_ccdc_init(struct isp_device *isp);
void omap3isp_ccdc_cleanup(struct isp_device *isp);
int omap3isp_ccdc_register_entities(struct isp_ccdc_device *ccdc,
	struct v4l2_device *vdev);
void omap3isp_ccdc_unregister_entities(struct isp_ccdc_device *ccdc);

int omap3isp_ccdc_busy(struct isp_ccdc_device *isp_ccdc);
int omap3isp_ccdc_isr(struct isp_ccdc_device *isp_ccdc, u32 events);
void omap3isp_ccdc_restore_context(struct isp_device *isp);
void omap3isp_ccdc_max_rate(struct isp_ccdc_device *ccdc,
	unsigned int *max_rate);

#endif	/* OMAP3_ISP_CCDC_H */
