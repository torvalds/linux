/*
 *  cx18 mailbox functions
 *
 *  Copyright (C) 2007  Hans Verkuil <hverkuil@xs4all.nl>
 *  Copyright (C) 2008  Andy Walls <awalls@radix.net>
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
#include "cx18-queue.h"
#include "cx18-streams.h"

static const char *rpu_str[] = { "APU", "CPU", "EPU", "HPU" };

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
	API_ENTRY(CPU, CX18_CPU_DE_RELEASE_MDL,			API_SLOW),
	API_ENTRY(APU, CX18_APU_START,				0),
	API_ENTRY(APU, CX18_APU_STOP,				0),
	API_ENTRY(APU, CX18_APU_RESETAI,			0),
	API_ENTRY(CPU, CX18_CPU_DEBUG_PEEK32,			0),
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

/* Call with buf of n*11+1 bytes */
static char *u32arr2hex(u32 data[], int n, char *buf)
{
	char *p;
	int i;

	for (i = 0, p = buf; i < n; i++, p += 11) {
		/* kernel snprintf() appends '\0' always */
		snprintf(p, 12, " %#010x", data[i]);
	}
	*p = '\0';
	return buf;
}

static void dump_mb(struct cx18 *cx, struct cx18_mailbox *mb, char *name)
{
	char argstr[MAX_MB_ARGUMENTS*11+1];

	if (!(cx18_debug & CX18_DBGFLG_API))
		return;

	CX18_DEBUG_API("%s: req %#010x ack %#010x cmd %#010x err %#010x args%s"
		       "\n", name, mb->request, mb->ack, mb->cmd, mb->error,
		       u32arr2hex(mb->args, MAX_MB_ARGUMENTS, argstr));
}


/*
 * Functions that run in a work_queue work handling context
 */

static void cx18_mdl_send_to_dvb(struct cx18_stream *s, struct cx18_mdl *mdl)
{
	struct cx18_buffer *buf;

	if (!s->dvb.enabled || mdl->bytesused == 0)
		return;

	/* We ignore mdl and buf readpos accounting here - it doesn't matter */

	/* The likely case */
	if (list_is_singular(&mdl->buf_list)) {
		buf = list_first_entry(&mdl->buf_list, struct cx18_buffer,
				       list);
		if (buf->bytesused)
			dvb_dmx_swfilter(&s->dvb.demux,
					 buf->buf, buf->bytesused);
		return;
	}

	list_for_each_entry(buf, &mdl->buf_list, list) {
		if (buf->bytesused == 0)
			break;
		dvb_dmx_swfilter(&s->dvb.demux, buf->buf, buf->bytesused);
	}
}

static void epu_dma_done(struct cx18 *cx, struct cx18_in_work_order *order)
{
	u32 handle, mdl_ack_count, id;
	struct cx18_mailbox *mb;
	struct cx18_mdl_ack *mdl_ack;
	struct cx18_stream *s;
	struct cx18_mdl *mdl;
	int i;

	mb = &order->mb;
	handle = mb->args[0];
	s = cx18_handle_to_stream(cx, handle);

	if (s == NULL) {
		CX18_WARN("Got DMA done notification for unknown/inactive"
			  " handle %d, %s mailbox seq no %d\n", handle,
			  (order->flags & CX18_F_EWO_MB_STALE_UPON_RECEIPT) ?
			  "stale" : "good", mb->request);
		return;
	}

	mdl_ack_count = mb->args[2];
	mdl_ack = order->mdl_ack;
	for (i = 0; i < mdl_ack_count; i++, mdl_ack++) {
		id = mdl_ack->id;
		/*
		 * Simple integrity check for processing a stale (and possibly
		 * inconsistent mailbox): make sure the MDL id is in the
		 * valid range for the stream.
		 *
		 * We go through the trouble of dealing with stale mailboxes
		 * because most of the time, the mailbox data is still valid and
		 * unchanged (and in practice the firmware ping-pongs the
		 * two mdl_ack buffers so mdl_acks are not stale).
		 *
		 * There are occasions when we get a half changed mailbox,
		 * which this check catches for a handle & id mismatch.  If the
		 * handle and id do correspond, the worst case is that we
		 * completely lost the old MDL, but pick up the new MDL
		 * early (but the new mdl_ack is guaranteed to be good in this
		 * case as the firmware wouldn't point us to a new mdl_ack until
		 * it's filled in).
		 *
		 * cx18_queue_get_mdl() will detect the lost MDLs
		 * and send them back to q_free for fw rotation eventually.
		 */
		if ((order->flags & CX18_F_EWO_MB_STALE_UPON_RECEIPT) &&
		    !(id >= s->mdl_base_idx &&
		      id < (s->mdl_base_idx + s->buffers))) {
			CX18_WARN("Fell behind! Ignoring stale mailbox with "
				  " inconsistent data. Lost MDL for mailbox "
				  "seq no %d\n", mb->request);
			break;
		}
		mdl = cx18_queue_get_mdl(s, id, mdl_ack->data_used);

		CX18_DEBUG_HI_DMA("DMA DONE for %s (MDL %d)\n", s->name, id);
		if (mdl == NULL) {
			CX18_WARN("Could not find MDL %d for stream %s\n",
				  id, s->name);
			continue;
		}

		CX18_DEBUG_HI_DMA("%s recv bytesused = %d\n",
				  s->name, mdl->bytesused);

		if (s->type != CX18_ENC_STREAM_TYPE_TS) {
			cx18_enqueue(s, mdl, &s->q_full);
			if (s->type == CX18_ENC_STREAM_TYPE_IDX)
				cx18_stream_rotate_idx_mdls(cx);
		}
		else {
			cx18_mdl_send_to_dvb(s, mdl);
			cx18_enqueue(s, mdl, &s->q_free);
		}
	}
	/* Put as many MDLs as possible back into fw use */
	cx18_stream_load_fw_queue(s);

	wake_up(&cx->dma_waitq);
	if (s->id != -1)
		wake_up(&s->waitq);
}

static void epu_debug(struct cx18 *cx, struct cx18_in_work_order *order)
{
	char *p;
	char *str = order->str;

	CX18_DEBUG_INFO("%x %s\n", order->mb.args[0], str);
	p = strchr(str, '.');
	if (!test_bit(CX18_F_I_LOADED_FW, &cx->i_flags) && p && p > str)
		CX18_INFO("FW version: %s\n", p - 1);
}

static void epu_cmd(struct cx18 *cx, struct cx18_in_work_order *order)
{
	switch (order->rpu) {
	case CPU:
	{
		switch (order->mb.cmd) {
		case CX18_EPU_DMA_DONE:
			epu_dma_done(cx, order);
			break;
		case CX18_EPU_DEBUG:
			epu_debug(cx, order);
			break;
		default:
			CX18_WARN("Unknown CPU to EPU mailbox command %#0x\n",
				  order->mb.cmd);
			break;
		}
		break;
	}
	case APU:
		CX18_WARN("Unknown APU to EPU mailbox command %#0x\n",
			  order->mb.cmd);
		break;
	default:
		break;
	}
}

static
void free_in_work_order(struct cx18 *cx, struct cx18_in_work_order *order)
{
	atomic_set(&order->pending, 0);
}

void cx18_in_work_handler(struct work_struct *work)
{
	struct cx18_in_work_order *order =
			container_of(work, struct cx18_in_work_order, work);
	struct cx18 *cx = order->cx;
	epu_cmd(cx, order);
	free_in_work_order(cx, order);
}


/*
 * Functions that run in an interrupt handling context
 */

static void mb_ack_irq(struct cx18 *cx, struct cx18_in_work_order *order)
{
	struct cx18_mailbox __iomem *ack_mb;
	u32 ack_irq, req;

	switch (order->rpu) {
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
			  order->rpu, order->mb.cmd);
		return;
	}

	req = order->mb.request;
	/* Don't ack if the RPU has gotten impatient and timed us out */
	if (req != cx18_readl(cx, &ack_mb->request) ||
	    req == cx18_readl(cx, &ack_mb->ack)) {
		CX18_DEBUG_WARN("Possibly falling behind: %s self-ack'ed our "
				"incoming %s to EPU mailbox (sequence no. %u) "
				"while processing\n",
				rpu_str[order->rpu], rpu_str[order->rpu], req);
		order->flags |= CX18_F_EWO_MB_STALE_WHILE_PROC;
		return;
	}
	cx18_writel(cx, req, &ack_mb->ack);
	cx18_write_reg_expect(cx, ack_irq, SW2_INT_SET, ack_irq, ack_irq);
	return;
}

static int epu_dma_done_irq(struct cx18 *cx, struct cx18_in_work_order *order)
{
	u32 handle, mdl_ack_offset, mdl_ack_count;
	struct cx18_mailbox *mb;

	mb = &order->mb;
	handle = mb->args[0];
	mdl_ack_offset = mb->args[1];
	mdl_ack_count = mb->args[2];

	if (handle == CX18_INVALID_TASK_HANDLE ||
	    mdl_ack_count == 0 || mdl_ack_count > CX18_MAX_MDL_ACKS) {
		if ((order->flags & CX18_F_EWO_MB_STALE) == 0)
			mb_ack_irq(cx, order);
		return -1;
	}

	cx18_memcpy_fromio(cx, order->mdl_ack, cx->enc_mem + mdl_ack_offset,
			   sizeof(struct cx18_mdl_ack) * mdl_ack_count);

	if ((order->flags & CX18_F_EWO_MB_STALE) == 0)
		mb_ack_irq(cx, order);
	return 1;
}

static
int epu_debug_irq(struct cx18 *cx, struct cx18_in_work_order *order)
{
	u32 str_offset;
	char *str = order->str;

	str[0] = '\0';
	str_offset = order->mb.args[1];
	if (str_offset) {
		cx18_setup_page(cx, str_offset);
		cx18_memcpy_fromio(cx, str, cx->enc_mem + str_offset, 252);
		str[252] = '\0';
		cx18_setup_page(cx, SCB_OFFSET);
	}

	if ((order->flags & CX18_F_EWO_MB_STALE) == 0)
		mb_ack_irq(cx, order);

	return str_offset ? 1 : 0;
}

static inline
int epu_cmd_irq(struct cx18 *cx, struct cx18_in_work_order *order)
{
	int ret = -1;

	switch (order->rpu) {
	case CPU:
	{
		switch (order->mb.cmd) {
		case CX18_EPU_DMA_DONE:
			ret = epu_dma_done_irq(cx, order);
			break;
		case CX18_EPU_DEBUG:
			ret = epu_debug_irq(cx, order);
			break;
		default:
			CX18_WARN("Unknown CPU to EPU mailbox command %#0x\n",
				  order->mb.cmd);
			break;
		}
		break;
	}
	case APU:
		CX18_WARN("Unknown APU to EPU mailbox command %#0x\n",
			  order->mb.cmd);
		break;
	default:
		break;
	}
	return ret;
}

static inline
struct cx18_in_work_order *alloc_in_work_order_irq(struct cx18 *cx)
{
	int i;
	struct cx18_in_work_order *order = NULL;

	for (i = 0; i < CX18_MAX_IN_WORK_ORDERS; i++) {
		/*
		 * We only need "pending" atomic to inspect its contents,
		 * and need not do a check and set because:
		 * 1. Any work handler thread only clears "pending" and only
		 * on one, particular work order at a time, per handler thread.
		 * 2. "pending" is only set here, and we're serialized because
		 * we're called in an IRQ handler context.
		 */
		if (atomic_read(&cx->in_work_order[i].pending) == 0) {
			order = &cx->in_work_order[i];
			atomic_set(&order->pending, 1);
			break;
		}
	}
	return order;
}

void cx18_api_epu_cmd_irq(struct cx18 *cx, int rpu)
{
	struct cx18_mailbox __iomem *mb;
	struct cx18_mailbox *order_mb;
	struct cx18_in_work_order *order;
	int submit;

	switch (rpu) {
	case CPU:
		mb = &cx->scb->cpu2epu_mb;
		break;
	case APU:
		mb = &cx->scb->apu2epu_mb;
		break;
	default:
		return;
	}

	order = alloc_in_work_order_irq(cx);
	if (order == NULL) {
		CX18_WARN("Unable to find blank work order form to schedule "
			  "incoming mailbox command processing\n");
		return;
	}

	order->flags = 0;
	order->rpu = rpu;
	order_mb = &order->mb;

	/* mb->cmd and mb->args[0] through mb->args[2] */
	cx18_memcpy_fromio(cx, &order_mb->cmd, &mb->cmd, 4 * sizeof(u32));
	/* mb->request and mb->ack.  N.B. we want to read mb->ack last */
	cx18_memcpy_fromio(cx, &order_mb->request, &mb->request,
			   2 * sizeof(u32));

	if (order_mb->request == order_mb->ack) {
		CX18_DEBUG_WARN("Possibly falling behind: %s self-ack'ed our "
				"incoming %s to EPU mailbox (sequence no. %u)"
				"\n",
				rpu_str[rpu], rpu_str[rpu], order_mb->request);
		if (cx18_debug & CX18_DBGFLG_WARN)
			dump_mb(cx, order_mb, "incoming");
		order->flags = CX18_F_EWO_MB_STALE_UPON_RECEIPT;
	}

	/*
	 * Individual EPU command processing is responsible for ack-ing
	 * a non-stale mailbox as soon as possible
	 */
	submit = epu_cmd_irq(cx, order);
	if (submit > 0) {
		queue_work(cx->in_work_queue, &order->work);
	}
}


/*
 * Functions called from a non-interrupt, non work_queue context
 */

static int cx18_api_call(struct cx18 *cx, u32 cmd, int args, u32 data[])
{
	const struct cx18_api_info *info = find_api_info(cmd);
	u32 state, irq, req, ack, err;
	struct cx18_mailbox __iomem *mb;
	u32 __iomem *xpu_state;
	wait_queue_head_t *waitq;
	struct mutex *mb_lock;
	unsigned long int t0, timeout, ret;
	int i;
	char argstr[MAX_MB_ARGUMENTS*11+1];
	DEFINE_WAIT(w);

	if (info == NULL) {
		CX18_WARN("unknown cmd %x\n", cmd);
		return -EINVAL;
	}

	if (cx18_debug & CX18_DBGFLG_API) { /* only call u32arr2hex if needed */
		if (cmd == CX18_CPU_DE_SET_MDL) {
			if (cx18_debug & CX18_DBGFLG_HIGHVOL)
				CX18_DEBUG_HI_API("%s\tcmd %#010x args%s\n",
						info->name, cmd,
						u32arr2hex(data, args, argstr));
		} else
			CX18_DEBUG_API("%s\tcmd %#010x args%s\n",
				       info->name, cmd,
				       u32arr2hex(data, args, argstr));
	}

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
	timeout = msecs_to_jiffies(10);
	ret = wait_event_timeout(*waitq,
				 (ack = cx18_readl(cx, &mb->ack)) == req,
				 timeout);
	if (req != ack) {
		/* waited long enough, make the mbox "not busy" from our end */
		cx18_writel(cx, req, &mb->ack);
		CX18_ERR("mbox was found stuck busy when setting up for %s; "
			 "clearing busy and trying to proceed\n", info->name);
	} else if (ret != timeout)
		CX18_DEBUG_API("waited %u msecs for busy mbox to be acked\n",
			       jiffies_to_msecs(timeout-ret));

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
	 */
	timeout = msecs_to_jiffies((info->flags & API_FAST) ? 10 : 20);

	CX18_DEBUG_HI_IRQ("sending interrupt SW1: %x to send %s\n",
			  irq, info->name);

	/* So we don't miss the wakeup, prepare to wait before notifying fw */
	prepare_to_wait(waitq, &w, TASK_UNINTERRUPTIBLE);
	cx18_write_reg_expect(cx, irq, SW1_INT_SET, irq, irq);

	t0 = jiffies;
	ack = cx18_readl(cx, &mb->ack);
	if (ack != req) {
		schedule_timeout(timeout);
		ret = jiffies - t0;
		ack = cx18_readl(cx, &mb->ack);
	} else {
		ret = jiffies - t0;
	}

	finish_wait(waitq, &w);

	if (req != ack) {
		mutex_unlock(mb_lock);
		if (ret >= timeout) {
			/* Timed out */
			CX18_DEBUG_WARN("sending %s timed out waiting %d msecs "
					"for RPU acknowledgement\n",
					info->name, jiffies_to_msecs(ret));
		} else {
			CX18_DEBUG_WARN("woken up before mailbox ack was ready "
					"after submitting %s to RPU.  only "
					"waited %d msecs on req %u but awakened"
					" with unmatched ack %u\n",
					info->name,
					jiffies_to_msecs(ret),
					req, ack);
		}
		return -EINVAL;
	}

	if (ret >= timeout)
		CX18_DEBUG_WARN("failed to be awakened upon RPU acknowledgment "
				"sending %s; timed out waiting %d msecs\n",
				info->name, jiffies_to_msecs(ret));
	else
		CX18_DEBUG_HI_API("waited %u msecs for %s to be acked\n",
				  jiffies_to_msecs(ret), info->name);

	/* Collect data returned by the XPU */
	for (i = 0; i < MAX_MB_ARGUMENTS; i++)
		data[i] = cx18_readl(cx, &mb->args[i]);
	err = cx18_readl(cx, &mb->error);
	mutex_unlock(mb_lock);

	/*
	 * Wait for XPU to perform extra actions for the caller in some cases.
	 * e.g. CX18_CPU_DE_RELEASE_MDL will cause the CPU to send all MDLs
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
	struct cx18_api_func_private *api_priv = priv;
	struct cx18 *cx = api_priv->cx;
	struct cx18_stream *s = api_priv->s;

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
