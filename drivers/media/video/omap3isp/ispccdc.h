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
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
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

/*
 * struct ispccdc_syncif - Structure for Sync Interface between sensor and CCDC
 * @ccdc_mastermode: Master mode. 1 - Master, 0 - Slave.
 * @fldstat: Field state. 0 - Odd Field, 1 - Even Field.
 * @datsz: Data size.
 * @fldmode: 0 - Progressive, 1 - Interlaced.
 * @datapol: 0 - Positive, 1 - Negative.
 * @fldpol: 0 - Positive, 1 - Negative.
 * @hdpol: 0 - Positive, 1 - Negative.
 * @vdpol: 0 - Positive, 1 - Negative.
 * @fldout: 0 - Input, 1 - Output.
 * @hs_width: Width of the Horizontal Sync pulse, used for HS/VS Output.
 * @vs_width: Width of the Vertical Sync pulse, used for HS/VS Output.
 * @ppln: Number of pixels per line, used for HS/VS Output.
 * @hlprf: Number of half lines per frame, used for HS/VS Output.
 * @bt_r656_en: 1 - Enable ITU-R BT656 mode, 0 - Sync mode.
 */
struct ispccdc_syncif {
	u8 ccdc_mastermode;
	u8 fldstat;
	u8 datsz;
	u8 fldmode;
	u8 datapol;
	u8 fldpol;
	u8 hdpol;
	u8 vdpol;
	u8 fldout;
	u8 hs_width;
	u8 vs_width;
	u8 ppln;
	u8 hlprf;
	u8 bt_r656_en;
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
	u32 table;
	struct iovm_struct *iovm;
};

/*
 * ispccdc_lsc - CCDC LSC parameters
 * @update_config: Set when user changes config
 * @request_enable: Whether LSC is requested to be enabled
 * @config: LSC config set by user
 * @update_table: Set when user provides a new LSC table to table_new
 * @table_new: LSC table set by user, ISP address
 * @table_inuse: LSC table currently in use, ISP address
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
 * @syncif: Interface synchronization configuration
 * @underrun: A buffer underrun occurred and a new buffer has been queued
 * @state: Streaming state
 * @lock: Serializes shadow_update with interrupt handler
 * @wait: Wait queue used to stop the module
 * @stopping: Stopping state
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
	struct omap3isp_ccdc_fpc fpc;
	struct ispccdc_lsc lsc;
	unsigned int update;
	unsigned int shadow_update;

	struct ispccdc_syncif syncif;

	unsigned int underrun:1;
	enum isp_pipeline_stream_state state;
	spinlock_t lock;
	wait_queue_head_t wait;
	unsigned int stopping;
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
