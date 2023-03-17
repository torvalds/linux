// SPDX-License-Identifier: GPL-2.0+
/*
 * Frame Interval Monitor.
 *
 * Copyright (c) 2016 Mentor Graphics Inc.
 */
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>
#include <media/imx.h>
#include "imx-media.h"

enum {
	FIM_CL_ENABLE = 0,
	FIM_CL_NUM,
	FIM_CL_TOLERANCE_MIN,
	FIM_CL_TOLERANCE_MAX,
	FIM_CL_NUM_SKIP,
	FIM_NUM_CONTROLS,
};

enum {
	FIM_CL_ICAP_EDGE = 0,
	FIM_CL_ICAP_CHANNEL,
	FIM_NUM_ICAP_CONTROLS,
};

#define FIM_CL_ENABLE_DEF          0 /* FIM disabled by default */
#define FIM_CL_NUM_DEF             8 /* average 8 frames */
#define FIM_CL_NUM_SKIP_DEF        2 /* skip 2 frames after restart */
#define FIM_CL_TOLERANCE_MIN_DEF  50 /* usec */
#define FIM_CL_TOLERANCE_MAX_DEF   0 /* no max tolerance (unbounded) */

struct imx_media_fim {
	/* the owning subdev of this fim instance */
	struct v4l2_subdev *sd;

	/* FIM's control handler */
	struct v4l2_ctrl_handler ctrl_handler;

	/* control clusters */
	struct v4l2_ctrl  *ctrl[FIM_NUM_CONTROLS];
	struct v4l2_ctrl  *icap_ctrl[FIM_NUM_ICAP_CONTROLS];

	spinlock_t        lock; /* protect control values */

	/* current control values */
	bool              enabled;
	int               num_avg;
	int               num_skip;
	unsigned long     tolerance_min; /* usec */
	unsigned long     tolerance_max; /* usec */
	/* input capture method of measuring FI */
	int               icap_channel;
	int               icap_flags;

	int               counter;
	ktime_t		  last_ts;
	unsigned long     sum;       /* usec */
	unsigned long     nominal;   /* usec */

	struct completion icap_first_event;
	bool              stream_on;
};

static bool icap_enabled(struct imx_media_fim *fim)
{
	return fim->icap_flags != IRQ_TYPE_NONE;
}

static void update_fim_nominal(struct imx_media_fim *fim,
			       const struct v4l2_fract *fi)
{
	if (fi->denominator == 0) {
		dev_dbg(fim->sd->dev, "no frame interval, FIM disabled\n");
		fim->enabled = false;
		return;
	}

	fim->nominal = DIV_ROUND_CLOSEST_ULL(1000000ULL * (u64)fi->numerator,
					     fi->denominator);

	dev_dbg(fim->sd->dev, "FI=%lu usec\n", fim->nominal);
}

static void reset_fim(struct imx_media_fim *fim, bool curval)
{
	struct v4l2_ctrl *icap_chan = fim->icap_ctrl[FIM_CL_ICAP_CHANNEL];
	struct v4l2_ctrl *icap_edge = fim->icap_ctrl[FIM_CL_ICAP_EDGE];
	struct v4l2_ctrl *en = fim->ctrl[FIM_CL_ENABLE];
	struct v4l2_ctrl *num = fim->ctrl[FIM_CL_NUM];
	struct v4l2_ctrl *skip = fim->ctrl[FIM_CL_NUM_SKIP];
	struct v4l2_ctrl *tol_min = fim->ctrl[FIM_CL_TOLERANCE_MIN];
	struct v4l2_ctrl *tol_max = fim->ctrl[FIM_CL_TOLERANCE_MAX];

	if (curval) {
		fim->enabled = en->cur.val;
		fim->icap_flags = icap_edge->cur.val;
		fim->icap_channel = icap_chan->cur.val;
		fim->num_avg = num->cur.val;
		fim->num_skip = skip->cur.val;
		fim->tolerance_min = tol_min->cur.val;
		fim->tolerance_max = tol_max->cur.val;
	} else {
		fim->enabled = en->val;
		fim->icap_flags = icap_edge->val;
		fim->icap_channel = icap_chan->val;
		fim->num_avg = num->val;
		fim->num_skip = skip->val;
		fim->tolerance_min = tol_min->val;
		fim->tolerance_max = tol_max->val;
	}

	/* disable tolerance range if max <= min */
	if (fim->tolerance_max <= fim->tolerance_min)
		fim->tolerance_max = 0;

	/* num_skip must be >= 1 if input capture not used */
	if (!icap_enabled(fim))
		fim->num_skip = max_t(int, fim->num_skip, 1);

	fim->counter = -fim->num_skip;
	fim->sum = 0;
}

static void send_fim_event(struct imx_media_fim *fim, unsigned long error)
{
	static const struct v4l2_event ev = {
		.type = V4L2_EVENT_IMX_FRAME_INTERVAL_ERROR,
	};

	v4l2_subdev_notify_event(fim->sd, &ev);
}

/*
 * Monitor an averaged frame interval. If the average deviates too much
 * from the nominal frame rate, send the frame interval error event. The
 * frame intervals are averaged in order to quiet noise from
 * (presumably random) interrupt latency.
 */
static void frame_interval_monitor(struct imx_media_fim *fim,
				   ktime_t timestamp)
{
	long long interval, error;
	unsigned long error_avg;
	bool send_event = false;

	if (!fim->enabled || ++fim->counter <= 0)
		goto out_update_ts;

	/* max error is less than l00µs, so use 32-bit division or fail */
	interval = ktime_to_ns(ktime_sub(timestamp, fim->last_ts));
	error = abs(interval - NSEC_PER_USEC * (u64)fim->nominal);
	if (error > U32_MAX)
		error = U32_MAX;
	else
		error = abs((u32)error / NSEC_PER_USEC);

	if (fim->tolerance_max && error >= fim->tolerance_max) {
		dev_dbg(fim->sd->dev,
			"FIM: %llu ignored, out of tolerance bounds\n",
			error);
		fim->counter--;
		goto out_update_ts;
	}

	fim->sum += error;

	if (fim->counter == fim->num_avg) {
		error_avg = DIV_ROUND_CLOSEST(fim->sum, fim->num_avg);

		if (error_avg > fim->tolerance_min)
			send_event = true;

		dev_dbg(fim->sd->dev, "FIM: error: %lu usec%s\n",
			error_avg, send_event ? " (!!!)" : "");

		fim->counter = 0;
		fim->sum = 0;
	}

out_update_ts:
	fim->last_ts = timestamp;
	if (send_event)
		send_fim_event(fim, error_avg);
}

/*
 * In case we are monitoring the first frame interval after streamon
 * (when fim->num_skip = 0), we need a valid fim->last_ts before we
 * can begin. This only applies to the input capture method. It is not
 * possible to accurately measure the first FI after streamon using the
 * EOF method, so fim->num_skip minimum is set to 1 in that case, so this
 * function is a noop when the EOF method is used.
 */
static void fim_acquire_first_ts(struct imx_media_fim *fim)
{
	unsigned long ret;

	if (!fim->enabled || fim->num_skip > 0)
		return;

	ret = wait_for_completion_timeout(
		&fim->icap_first_event,
		msecs_to_jiffies(IMX_MEDIA_EOF_TIMEOUT));
	if (ret == 0)
		v4l2_warn(fim->sd, "wait first icap event timeout\n");
}

/* FIM Controls */
static int fim_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct imx_media_fim *fim = container_of(ctrl->handler,
						 struct imx_media_fim,
						 ctrl_handler);
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&fim->lock, flags);

	switch (ctrl->id) {
	case V4L2_CID_IMX_FIM_ENABLE:
		break;
	case V4L2_CID_IMX_FIM_ICAP_EDGE:
		if (fim->stream_on)
			ret = -EBUSY;
		break;
	default:
		ret = -EINVAL;
	}

	if (!ret)
		reset_fim(fim, false);

	spin_unlock_irqrestore(&fim->lock, flags);
	return ret;
}

static const struct v4l2_ctrl_ops fim_ctrl_ops = {
	.s_ctrl = fim_s_ctrl,
};

static const struct v4l2_ctrl_config fim_ctrl[] = {
	[FIM_CL_ENABLE] = {
		.ops = &fim_ctrl_ops,
		.id = V4L2_CID_IMX_FIM_ENABLE,
		.name = "FIM Enable",
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.def = FIM_CL_ENABLE_DEF,
		.min = 0,
		.max = 1,
		.step = 1,
	},
	[FIM_CL_NUM] = {
		.ops = &fim_ctrl_ops,
		.id = V4L2_CID_IMX_FIM_NUM,
		.name = "FIM Num Average",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.def = FIM_CL_NUM_DEF,
		.min =  1, /* no averaging */
		.max = 64, /* average 64 frames */
		.step = 1,
	},
	[FIM_CL_TOLERANCE_MIN] = {
		.ops = &fim_ctrl_ops,
		.id = V4L2_CID_IMX_FIM_TOLERANCE_MIN,
		.name = "FIM Tolerance Min",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.def = FIM_CL_TOLERANCE_MIN_DEF,
		.min =    2,
		.max =  200,
		.step =   1,
	},
	[FIM_CL_TOLERANCE_MAX] = {
		.ops = &fim_ctrl_ops,
		.id = V4L2_CID_IMX_FIM_TOLERANCE_MAX,
		.name = "FIM Tolerance Max",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.def = FIM_CL_TOLERANCE_MAX_DEF,
		.min =    0,
		.max =  500,
		.step =   1,
	},
	[FIM_CL_NUM_SKIP] = {
		.ops = &fim_ctrl_ops,
		.id = V4L2_CID_IMX_FIM_NUM_SKIP,
		.name = "FIM Num Skip",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.def = FIM_CL_NUM_SKIP_DEF,
		.min =   0, /* skip no frames */
		.max = 256, /* skip 256 frames */
		.step =  1,
	},
};

static const struct v4l2_ctrl_config fim_icap_ctrl[] = {
	[FIM_CL_ICAP_EDGE] = {
		.ops = &fim_ctrl_ops,
		.id = V4L2_CID_IMX_FIM_ICAP_EDGE,
		.name = "FIM Input Capture Edge",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.def =  IRQ_TYPE_NONE, /* input capture disabled by default */
		.min =  IRQ_TYPE_NONE,
		.max =  IRQ_TYPE_EDGE_BOTH,
		.step = 1,
	},
	[FIM_CL_ICAP_CHANNEL] = {
		.ops = &fim_ctrl_ops,
		.id = V4L2_CID_IMX_FIM_ICAP_CHANNEL,
		.name = "FIM Input Capture Channel",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.def =  0,
		.min =  0,
		.max =  1,
		.step = 1,
	},
};

static int init_fim_controls(struct imx_media_fim *fim)
{
	struct v4l2_ctrl_handler *hdlr = &fim->ctrl_handler;
	int i, ret;

	v4l2_ctrl_handler_init(hdlr, FIM_NUM_CONTROLS + FIM_NUM_ICAP_CONTROLS);

	for (i = 0; i < FIM_NUM_CONTROLS; i++)
		fim->ctrl[i] = v4l2_ctrl_new_custom(hdlr,
						    &fim_ctrl[i],
						    NULL);
	for (i = 0; i < FIM_NUM_ICAP_CONTROLS; i++)
		fim->icap_ctrl[i] = v4l2_ctrl_new_custom(hdlr,
							 &fim_icap_ctrl[i],
							 NULL);
	if (hdlr->error) {
		ret = hdlr->error;
		goto err_free;
	}

	v4l2_ctrl_cluster(FIM_NUM_CONTROLS, fim->ctrl);
	v4l2_ctrl_cluster(FIM_NUM_ICAP_CONTROLS, fim->icap_ctrl);

	return 0;
err_free:
	v4l2_ctrl_handler_free(hdlr);
	return ret;
}

/*
 * Monitor frame intervals via EOF interrupt. This method is
 * subject to uncertainty errors introduced by interrupt latency.
 *
 * This is a noop if the Input Capture method is being used, since
 * the frame_interval_monitor() is called by the input capture event
 * callback handler in that case.
 */
void imx_media_fim_eof_monitor(struct imx_media_fim *fim, ktime_t timestamp)
{
	unsigned long flags;

	spin_lock_irqsave(&fim->lock, flags);

	if (!icap_enabled(fim))
		frame_interval_monitor(fim, timestamp);

	spin_unlock_irqrestore(&fim->lock, flags);
}

/* Called by the subdev in its s_stream callback */
void imx_media_fim_set_stream(struct imx_media_fim *fim,
			      const struct v4l2_fract *fi,
			      bool on)
{
	unsigned long flags;

	v4l2_ctrl_lock(fim->ctrl[FIM_CL_ENABLE]);

	if (fim->stream_on == on)
		goto out;

	if (on) {
		spin_lock_irqsave(&fim->lock, flags);
		reset_fim(fim, true);
		update_fim_nominal(fim, fi);
		spin_unlock_irqrestore(&fim->lock, flags);

		if (icap_enabled(fim))
			fim_acquire_first_ts(fim);
	}

	fim->stream_on = on;
out:
	v4l2_ctrl_unlock(fim->ctrl[FIM_CL_ENABLE]);
}

int imx_media_fim_add_controls(struct imx_media_fim *fim)
{
	/* add the FIM controls to the calling subdev ctrl handler */
	return v4l2_ctrl_add_handler(fim->sd->ctrl_handler,
				     &fim->ctrl_handler, NULL, false);
}

/* Called by the subdev in its subdev registered callback */
struct imx_media_fim *imx_media_fim_init(struct v4l2_subdev *sd)
{
	struct imx_media_fim *fim;
	int ret;

	fim = devm_kzalloc(sd->dev, sizeof(*fim), GFP_KERNEL);
	if (!fim)
		return ERR_PTR(-ENOMEM);

	fim->sd = sd;

	spin_lock_init(&fim->lock);

	ret = init_fim_controls(fim);
	if (ret)
		return ERR_PTR(ret);

	return fim;
}

void imx_media_fim_free(struct imx_media_fim *fim)
{
	v4l2_ctrl_handler_free(&fim->ctrl_handler);
}
