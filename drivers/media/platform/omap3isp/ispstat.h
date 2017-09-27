/*
 * ispstat.h
 *
 * TI OMAP3 ISP - Statistics core
 *
 * Copyright (C) 2010 Nokia Corporation
 * Copyright (C) 2009 Texas Instruments, Inc
 *
 * Contacts: David Cohen <dacohen@gmail.com>
 *	     Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *	     Sakari Ailus <sakari.ailus@iki.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef OMAP3_ISP_STAT_H
#define OMAP3_ISP_STAT_H

#include <linux/types.h>
#include <linux/omap3isp.h>
#include <media/v4l2-event.h>

#include "isp.h"
#include "ispvideo.h"

#define STAT_MAX_BUFS		5
#define STAT_NEVENTS		8

#define STAT_BUF_DONE		0	/* Buffer is ready */
#define STAT_NO_BUF		1	/* An error has occurred */
#define STAT_BUF_WAITING_DMA	2	/* Histogram only: DMA is running */

struct dma_chan;
struct ispstat;

struct ispstat_buffer {
	struct sg_table sgt;
	void *virt_addr;
	dma_addr_t dma_addr;
	struct timeval ts;
	u32 buf_size;
	u32 frame_number;
	u16 config_counter;
	u8 empty;
};

struct ispstat_ops {
	/*
	 * Validate new params configuration.
	 * new_conf->buf_size value must be changed to the exact buffer size
	 * necessary for the new configuration if it's smaller.
	 */
	int (*validate_params)(struct ispstat *stat, void *new_conf);

	/*
	 * Save new params configuration.
	 * stat->priv->buf_size value must be set to the exact buffer size for
	 * the new configuration.
	 * stat->update is set to 1 if new configuration is different than
	 * current one.
	 */
	void (*set_params)(struct ispstat *stat, void *new_conf);

	/* Apply stored configuration. */
	void (*setup_regs)(struct ispstat *stat, void *priv);

	/* Enable/Disable module. */
	void (*enable)(struct ispstat *stat, int enable);

	/* Verify is module is busy. */
	int (*busy)(struct ispstat *stat);

	/* Used for specific operations during generic buf process task. */
	int (*buf_process)(struct ispstat *stat);
};

enum ispstat_state_t {
	ISPSTAT_DISABLED = 0,
	ISPSTAT_DISABLING,
	ISPSTAT_ENABLED,
	ISPSTAT_ENABLING,
	ISPSTAT_SUSPENDED,
};

struct ispstat {
	struct v4l2_subdev subdev;
	struct media_pad pad;	/* sink pad */

	/* Control */
	unsigned configured:1;
	unsigned update:1;
	unsigned buf_processing:1;
	unsigned sbl_ovl_recover:1;
	u8 inc_config;
	atomic_t buf_err;
	enum ispstat_state_t state;	/* enabling/disabling state */
	struct isp_device *isp;
	void *priv;		/* pointer to priv config struct */
	void *recover_priv;	/* pointer to recover priv configuration */
	struct mutex ioctl_lock; /* serialize private ioctl */

	const struct ispstat_ops *ops;

	/* Buffer */
	u8 wait_acc_frames;
	u16 config_counter;
	u32 frame_number;
	u32 buf_size;
	u32 buf_alloc_size;
	struct dma_chan *dma_ch;
	unsigned long event_type;
	struct ispstat_buffer *buf;
	struct ispstat_buffer *active_buf;
	struct ispstat_buffer *locked_buf;
};

struct ispstat_generic_config {
	/*
	 * Fields must be in the same order as in:
	 *  - omap3isp_h3a_aewb_config
	 *  - omap3isp_h3a_af_config
	 *  - omap3isp_hist_config
	 */
	u32 buf_size;
	u16 config_counter;
};

int omap3isp_stat_config(struct ispstat *stat, void *new_conf);
int omap3isp_stat_request_statistics(struct ispstat *stat,
				     struct omap3isp_stat_data *data);
int omap3isp_stat_init(struct ispstat *stat, const char *name,
		       const struct v4l2_subdev_ops *sd_ops);
void omap3isp_stat_cleanup(struct ispstat *stat);
int omap3isp_stat_subscribe_event(struct v4l2_subdev *subdev,
				  struct v4l2_fh *fh,
				  struct v4l2_event_subscription *sub);
int omap3isp_stat_unsubscribe_event(struct v4l2_subdev *subdev,
				    struct v4l2_fh *fh,
				    struct v4l2_event_subscription *sub);
int omap3isp_stat_s_stream(struct v4l2_subdev *subdev, int enable);

int omap3isp_stat_busy(struct ispstat *stat);
int omap3isp_stat_pcr_busy(struct ispstat *stat);
void omap3isp_stat_suspend(struct ispstat *stat);
void omap3isp_stat_resume(struct ispstat *stat);
int omap3isp_stat_enable(struct ispstat *stat, u8 enable);
void omap3isp_stat_sbl_overflow(struct ispstat *stat);
void omap3isp_stat_isr(struct ispstat *stat);
void omap3isp_stat_isr_frame_sync(struct ispstat *stat);
void omap3isp_stat_dma_isr(struct ispstat *stat);
int omap3isp_stat_register_entities(struct ispstat *stat,
				    struct v4l2_device *vdev);
void omap3isp_stat_unregister_entities(struct ispstat *stat);

#endif /* OMAP3_ISP_STAT_H */
