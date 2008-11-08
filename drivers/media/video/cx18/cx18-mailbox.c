/*
 *  cx18 mailbox functions
 *
 *  Copyright (C) 2007  Hans Verkuil <hverkuil@xs4all.nl>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *  02111-1307  USA
 */

#include <stdarg.h>

#include "cx18-driver.h"
#include "cx18-io.h"
#include "cx18-scb.h"
#include "cx18-irq.h"
#include "cx18-mailbox.h"

#define API_FAST (1 << 2) /* Short timeout */
#define API_SLOW (1 << 3) /* Additional 300ms timeout */

struct cx18_api_info {
	u32 cmd;
	u8 flags;		/* Flags, see above */
	u8 rpu;			/* Processing unit */
	const char *name; 	/* The name of the command */
};

#define API_ENTRY(rpu, x, f) { (x), (f), (rpu), #x }

static const struct cx18_api_info api_info[] = {
	/* MPEG encoder API */
	API_ENTRY(CPU, CX18_CPU_SET_CHANNEL_TYPE,		0),
	API_ENTRY(CPU, CX18_EPU_DEBUG, 				0),
	API_ENTRY(CPU, CX18_CREATE_TASK, 			0),
	API_ENTRY(CPU, CX18_DESTROY_TASK, 			0),
	API_ENTRY(CPU, CX18_CPU_CAPTURE_START,                  API_SLOW),
	API_ENTRY(CPU, CX18_CPU_CAPTURE_STOP,                   API_SLOW),
	API_ENTRY(CPU, CX18_CPU_CAPTURE_PAUSE,                  0),
	API_ENTRY(CPU, CX18_CPU_CAPTURE_RESUME,                 0),
	API_ENTRY(CPU, CX18_CPU_SET_CHANNEL_TYPE,               0),
	API_ENTRY(CPU, CX18_CPU_SET_STREAM_OUTPUT_TYPE,         0),
	API_ENTRY(CPU, CX18_CPU_SET_VIDEO_IN,                   0),
	API_ENTRY(CPU, CX18_CPU_SET_VIDEO_RATE,                 0),
	API_ENTRY(CPU, CX18_CPU_SET_VIDEO_RESOLUTION,           0),
	API_ENTRY(CPU, CX18_CPU_SET_FILTER_PARAM,               0),
	API_ENTRY(CPU, CX18_CPU_SET_SPATIAL_FILTER_TYPE,        0),
	API_ENTRY(CPU, CX18_CPU_SET_MEDIAN_CORING,              0),
	API_ENTRY(CPU, CX18_CPU_SET_INDEXTABLE,                 0),
	API_ENTRY(CPU, CX18_CPU_SET_AUDIO_PARAMETERS,           0),
	API_ENTRY(CPU, CX18_CPU_SET_VIDEO_MUTE,                 0),
	API_ENTRY(CPU, CX18_CPU_SET_AUDIO_MUTE,                 0),
	API_ENTRY(CPU, CX18_CPU_SET_MISC_PARAMETERS,            0),
	API_ENTRY(CPU, CX18_CPU_SET_RAW_VBI_PARAM,              API_SLOW),
	API_ENTRY(CPU, CX18_CPU_SET_CAPTURE_LINE_NO,            0),
	API_ENTRY(CPU, CX18_CPU_SET_COPYRIGHT,                  0),
	API_ENTRY(CPU, CX18_CPU_SET_AUDIO_PID,                  0),
	API_ENTRY(CPU, CX18_CPU_SET_VIDEO_PID,                  0),
	API_ENTRY(CPU, CX18_CPU_SET_VER_CROP_LINE,              0),
	API_ENTRY(CPU, CX18_CPU_SET_GOP_STRUCTURE,              0),
	API_ENTRY(CPU, CX18_CPU_SET_SCENE_CHANGE_DETECTION,     0),
	API_ENTRY(CPU, CX18_CPU_SET_ASPECT_RATIO,               0),
	API_ENTRY(CPU, CX18_CPU_SET_SKIP_INPUT_FRAME,           0),
	API_ENTRY(CPU, CX18_CPU_SET_SLICED_VBI_PARAM,           0),
	API_ENTRY(CPU, CX18_CPU_SET_USERDATA_PLACE_HOLDER,      0),
	API_ENTRY(CPU, CX18_CPU_GET_ENC_PTS,                    0),
	API_ENTRY(CPU, CX18_CPU_DE_SET_MDL_ACK,			0),
	API_ENTRY(CPU, CX18_CPU_DE_SET_MDL,			API_FAST),
	API_ENTRY(CPU, CX18_APU_RESETAI,			API_FAST),
	API_ENTRY(CPU, CX18_CPU_DE_RELEASE_MDL,			API_SLOW),
	API_ENTRY(0, 0,						0),
};

static const struct cx18_api_info *find_api_info(u32 cmd)
{
	int i;

	for (i = 0; api_info[i].cmd; i++)
		if (api_info[i].cmd == cmd)
			return &api_info[i];
	return NULL;
}

long cx18_mb_ack(struct cx18 *cx, const struct cx18_mailbox *mb, int rpu)
{
	struct cx18_mailbox __iomem *ack_mb;
	u32 ack_irq;

	switch (rpu) {
	case APU:
		ack_irq = IRQ_EPU_TO_APU_ACK;
		ack_mb = &cx->scb->apu2epu_mb;
		break;
	case CPU:
		ack_irq = IRQ_EPU_TO_CPU_ACK;
		ack_mb = &cx->scb->cpu2epu_mb;
		break;
	default:
		CX18_WARN("Unhandled RPU (%d) for command %x ack\n",
			  rpu, mb->cmd);
		return -EINVAL;
	}

	cx18_setup_page(cx, SCB_OFFSET);
	cx18_write_sync(cx, mb->request, &ack_mb->ack);
	cx18_write_reg_expect(cx, ack_irq, SW2_INT_SET, ack_irq, ack_irq);
	return 0;
}

static void cx18_api_log_ack_delay(struct cx18 *cx, int msecs)
{
	if (msecs > CX18_MAX_MB_ACK_DELAY)
		msecs = CX18_MAX_MB_ACK_DELAY;
	atomic_inc(&cx->mbox_stats.mb_ack_delay[msecs]);
}

static int cx18_api_call(struct cx18 *cx, u32 cmd, int args, u32 data[])
{
	const struct cx18_api_info *info = find_api_info(cmd);
	u32 state, irq, req, ack, err;
	struct cx18_mailbox __iomem *mb;
	u32 __iomem *xpu_state;
	wait_queue_head_t *waitq;
	struct mutex *mb_lock;
	long int timeout, ret;
	int i;

	if (info == NULL) {
		CX18_WARN("unknown cmd %x\n", cmd);
		return -EINVAL;
	}

	if (cmd == CX18_CPU_DE_SET_MDL)
		CX18_DEBUG_HI_API("%s\n", info->name);
	else
		CX18_DEBUG_API("%s\n", info->name);

	switch (info->rpu) {
	case APU:
		waitq = &cx->mb_apu_waitq;
		mb_lock = &cx->epu2apu_mb_lock;
		irq = IRQ_EPU_TO_APU;
		mb = &cx->scb->epu2apu_mb;
		xpu_state = &cx->scb->apu_state;
		break;
	case CPU:
		waitq = &cx->mb_cpu_waitq;
		mb_lock = &cx->epu2cpu_mb_lock;
		irq = IRQ_EPU_TO_CPU;
		mb = &cx->scb->epu2cpu_mb;
		xpu_state = &cx->scb->cpu_state;
		break;
	default:
		CX18_WARN("Unknown RPU (%d) for API call\n", info->rpu);
		return -EINVAL;
	}

	mutex_lock(mb_lock);
	cx18_setup_page(cx, SCB_OFFSET);

	/*
	 * Wait for an in-use mailbox to complete
	 *
	 * If the XPU is responding with Ack's, the mailbox shouldn't be in
	 * a busy state, since we serialize access to it on our end.
	 *
	 * If the wait for ack after sending a previous command was interrupted
	 * by a signal, we may get here and find a busy mailbox.  After waiting,
	 * mark it "not busy" from our end, if the XPU hasn't ack'ed it still.
	 */
	state = cx18_readl(cx, xpu_state);
	req = cx18_readl(cx, &mb->request);
	timeout = msecs_to_jiffies(20); /* 1 field at 50 Hz vertical refresh */
	ret = wait_event_timeout(*waitq,
				 (ack = cx18_readl(cx, &mb->ack)) == req,
				 timeout);
	if (req != ack) {
		/* waited long enough, make the mbox "not busy" from our end */
		cx18_writel(cx, req, &mb->ack);
		CX18_ERR("mbox was found stuck busy when setting up for %s; "
			 "clearing busy and trying to proceed\n", info->name);
	} else if (ret != timeout)
		CX18_DEBUG_API("waited %u usecs for busy mbox to be acked\n",
			       jiffies_to_usecs(timeout-ret));

	/* Build the outgoing mailbox */
	req = ((req & 0xfffffffe) == 0xfffffffe) ? 1 : req + 1;

	cx18_writel(cx, cmd, &mb->cmd);
	for (i = 0; i < args; i++)
		cx18_writel(cx, data[i], &mb->args[i]);
	cx18_writel(cx, 0, &mb->error);
	cx18_writel(cx, req, &mb->request);
	cx18_writel(cx, req - 1, &mb->ack); /* ensure ack & req are distinct */

	/*
	 * Notify the XPU and wait for it to send an Ack back
	 * 21 ms = ~ 0.5 frames at a frame rate of 24 fps
	 * 42 ms = ~ 1 frame at a frame rate of 24 fps
	 */
	timeout = msecs_to_jiffies((info->flags & API_FAST) ? 21 : 42);

	CX18_DEBUG_HI_IRQ("sending interrupt SW1: %x to send %s\n",
			  irq, info->name);
	cx18_write_reg_expect(cx, irq, SW1_INT_SET, irq, irq);

	ret = wait_event_timeout(
		       *waitq,
		       cx18_readl(cx, &mb->ack) == cx18_readl(cx, &mb->request),
		       timeout);
	if (ret == 0) {
		/* Timed out */
		mutex_unlock(mb_lock);
		i = jiffies_to_msecs(timeout);
		cx18_api_log_ack_delay(cx, i);
		CX18_WARN("sending %s timed out waiting %d msecs for RPU "
			  "acknowledgement\n", info->name, i);
		return -EINVAL;
	} else if (ret < 0) {
		/* Interrupted */
		mutex_unlock(mb_lock);
		CX18_WARN("sending %s was interrupted waiting for RPU"
			  "acknowledgement\n", info->name);
		return -EINTR;
	}

	i = jiffies_to_msecs(timeout-ret);
	cx18_api_log_ack_delay(cx, i);
	if (ret != timeout)
		CX18_DEBUG_HI_API("waited %u msecs for %s to be acked\n",
				  i, info->name);

	/* Collect data returned by the XPU */
	for (i = 0; i < MAX_MB_ARGUMENTS; i++)
		data[i] = cx18_readl(cx, &mb->args[i]);
	err = cx18_readl(cx, &mb->error);
	mutex_unlock(mb_lock);

	/*
	 * Wait for XPU to perform extra actions for the caller in some cases.
	 * e.g. CX18_CPU_DE_RELEASE_MDL will cause the CPU to send all buffers
	 * back in a burst shortly thereafter
	 */
	if (info->flags & API_SLOW)
		cx18_msleep_timeout(300, 0);

	if (err)
		CX18_DEBUG_API("mailbox error %08x for command %s\n", err,
				info->name);
	return err ? -EIO : 0;
}

int cx18_api(struct cx18 *cx, u32 cmd, int args, u32 data[])
{
	return cx18_api_call(cx, cmd, args, data);
}

static int cx18_set_filter_param(struct cx18_stream *s)
{
	struct cx18 *cx = s->cx;
	u32 mode;
	int ret;

	mode = (cx->filter_mode & 1) ? 2 : (cx->spatial_strength ? 1 : 0);
	ret = cx18_vapi(cx, CX18_CPU_SET_FILTER_PARAM, 4,
			s->handle, 1, mode, cx->spatial_strength);
	mode = (cx->filter_mode & 2) ? 2 : (cx->temporal_strength ? 1 : 0);
	ret = ret ? ret : cx18_vapi(cx, CX18_CPU_SET_FILTER_PARAM, 4,
			s->handle, 0, mode, cx->temporal_strength);
	ret = ret ? ret : cx18_vapi(cx, CX18_CPU_SET_FILTER_PARAM, 4,
			s->handle, 2, cx->filter_mode >> 2, 0);
	return ret;
}

int cx18_api_func(void *priv, u32 cmd, int in, int out,
		u32 data[CX2341X_MBOX_MAX_DATA])
{
	struct cx18 *cx = priv;
	struct cx18_stream *s = &cx->streams[CX18_ENC_STREAM_TYPE_MPG];

	switch (cmd) {
	case CX2341X_ENC_SET_OUTPUT_PORT:
		return 0;
	case CX2341X_ENC_SET_FRAME_RATE:
		return cx18_vapi(cx, CX18_CPU_SET_VIDEO_IN, 6,
				s->handle, 0, 0, 0, 0, data[0]);
	case CX2341X_ENC_SET_FRAME_SIZE:
		return cx18_vapi(cx, CX18_CPU_SET_VIDEO_RESOLUTION, 3,
				s->handle, data[1], data[0]);
	case CX2341X_ENC_SET_STREAM_TYPE:
		return cx18_vapi(cx, CX18_CPU_SET_STREAM_OUTPUT_TYPE, 2,
				s->handle, data[0]);
	case CX2341X_ENC_SET_ASPECT_RATIO:
		return cx18_vapi(cx, CX18_CPU_SET_ASPECT_RATIO, 2,
				s->handle, data[0]);

	case CX2341X_ENC_SET_GOP_PROPERTIES:
		return cx18_vapi(cx, CX18_CPU_SET_GOP_STRUCTURE, 3,
				s->handle, data[0], data[1]);
	case CX2341X_ENC_SET_GOP_CLOSURE:
		return 0;
	case CX2341X_ENC_SET_AUDIO_PROPERTIES:
		return cx18_vapi(cx, CX18_CPU_SET_AUDIO_PARAMETERS, 2,
				s->handle, data[0]);
	case CX2341X_ENC_MUTE_AUDIO:
		return cx18_vapi(cx, CX18_CPU_SET_AUDIO_MUTE, 2,
				s->handle, data[0]);
	case CX2341X_ENC_SET_BIT_RATE:
		return cx18_vapi(cx, CX18_CPU_SET_VIDEO_RATE, 5,
				s->handle, data[0], data[1], data[2], data[3]);
	case CX2341X_ENC_MUTE_VIDEO:
		return cx18_vapi(cx, CX18_CPU_SET_VIDEO_MUTE, 2,
				s->handle, data[0]);
	case CX2341X_ENC_SET_FRAME_DROP_RATE:
		return cx18_vapi(cx, CX18_CPU_SET_SKIP_INPUT_FRAME, 2,
				s->handle, data[0]);
	case CX2341X_ENC_MISC:
		return cx18_vapi(cx, CX18_CPU_SET_MISC_PARAMETERS, 4,
				s->handle, data[0], data[1], data[2]);
	case CX2341X_ENC_SET_DNR_FILTER_MODE:
		cx->filter_mode = (data[0] & 3) | (data[1] << 2);
		return cx18_set_filter_param(s);
	case CX2341X_ENC_SET_DNR_FILTER_PROPS:
		cx->spatial_strength = data[0];
		cx->temporal_strength = data[1];
		return cx18_set_filter_param(s);
	case CX2341X_ENC_SET_SPATIAL_FILTER_TYPE:
		return cx18_vapi(cx, CX18_CPU_SET_SPATIAL_FILTER_TYPE, 3,
				s->handle, data[0], data[1]);
	case CX2341X_ENC_SET_CORING_LEVELS:
		return cx18_vapi(cx, CX18_CPU_SET_MEDIAN_CORING, 5,
				s->handle, data[0], data[1], data[2], data[3]);
	}
	CX18_WARN("Unknown cmd %x\n", cmd);
	return 0;
}

int cx18_vapi_result(struct cx18 *cx, u32 data[MAX_MB_ARGUMENTS],
		u32 cmd, int args, ...)
{
	va_list ap;
	int i;

	va_start(ap, args);
	for (i = 0; i < args; i++)
		data[i] = va_arg(ap, u32);
	va_end(ap);
	return cx18_api(cx, cmd, args, data);
}

int cx18_vapi(struct cx18 *cx, u32 cmd, int args, ...)
{
	u32 data[MAX_MB_ARGUMENTS];
	va_list ap;
	int i;

	if (cx == NULL) {
		CX18_ERR("cx == NULL (cmd=%x)\n", cmd);
		return 0;
	}
	if (args > MAX_MB_ARGUMENTS) {
		CX18_ERR("args too big (cmd=%x)\n", cmd);
		args = MAX_MB_ARGUMENTS;
	}
	va_start(ap, args);
	for (i = 0; i < args; i++)
		data[i] = va_arg(ap, u32);
	va_end(ap);
	return cx18_api(cx, cmd, args, data);
}
