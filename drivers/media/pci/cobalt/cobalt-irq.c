/*
 *  cobalt interrupt handling
 *
 *  Copyright 2012-2015 Cisco Systems, Inc. and/or its affiliates.
 *  All rights reserved.
 *
 *  This program is free software; you may redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 *  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 *  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 *  ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 *  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 */

#include <media/adv7604.h>

#include "cobalt-driver.h"
#include "cobalt-irq.h"
#include "cobalt-omnitek.h"

static void cobalt_dma_stream_queue_handler(struct cobalt_stream *s)
{
	struct cobalt *cobalt = s->cobalt;
	int rx = s->video_channel;
	struct m00473_freewheel_regmap __iomem *fw =
		COBALT_CVI_FREEWHEEL(s->cobalt, rx);
	struct m00233_video_measure_regmap __iomem *vmr =
		COBALT_CVI_VMR(s->cobalt, rx);
	struct m00389_cvi_regmap __iomem *cvi =
		COBALT_CVI(s->cobalt, rx);
	struct m00479_clk_loss_detector_regmap __iomem *clkloss =
		COBALT_CVI_CLK_LOSS(s->cobalt, rx);
	struct cobalt_buffer *cb;
	bool skip = false;

	spin_lock(&s->irqlock);

	if (list_empty(&s->bufs)) {
		pr_err("no buffers!\n");
		spin_unlock(&s->irqlock);
		return;
	}

	/* Give the fresh filled up buffer to the user.
	 * Note that the interrupt is only sent if the DMA can continue
	 * with a new buffer, so it is always safe to return this buffer
	 * to userspace. */
	cb = list_first_entry(&s->bufs, struct cobalt_buffer, list);
	list_del(&cb->list);
	spin_unlock(&s->irqlock);

	if (s->is_audio || s->is_output)
		goto done;

	if (s->unstable_frame) {
		uint32_t stat = ioread32(&vmr->irq_status);

		iowrite32(stat, &vmr->irq_status);
		if (!(ioread32(&vmr->status) &
		      M00233_STATUS_BITMAP_INIT_DONE_MSK)) {
			cobalt_dbg(1, "!init_done\n");
			if (s->enable_freewheel)
				goto restart_fw;
			goto done;
		}

		if (ioread32(&clkloss->status) &
		    M00479_STATUS_BITMAP_CLOCK_MISSING_MSK) {
			iowrite32(0, &clkloss->ctrl);
			iowrite32(M00479_CTRL_BITMAP_ENABLE_MSK, &clkloss->ctrl);
			cobalt_dbg(1, "no clock\n");
			if (s->enable_freewheel)
				goto restart_fw;
			goto done;
		}
		if ((stat & (M00233_IRQ_STATUS_BITMAP_VACTIVE_AREA_MSK |
			     M00233_IRQ_STATUS_BITMAP_HACTIVE_AREA_MSK)) ||
				ioread32(&vmr->vactive_area) != s->timings.bt.height ||
				ioread32(&vmr->hactive_area) != s->timings.bt.width) {
			cobalt_dbg(1, "unstable\n");
			if (s->enable_freewheel)
				goto restart_fw;
			goto done;
		}
		if (!s->enable_cvi) {
			s->enable_cvi = true;
			iowrite32(M00389_CONTROL_BITMAP_ENABLE_MSK, &cvi->control);
			goto done;
		}
		if (!(ioread32(&cvi->status) & M00389_STATUS_BITMAP_LOCK_MSK)) {
			cobalt_dbg(1, "cvi no lock\n");
			if (s->enable_freewheel)
				goto restart_fw;
			goto done;
		}
		if (!s->enable_freewheel) {
			cobalt_dbg(1, "stable\n");
			s->enable_freewheel = true;
			iowrite32(0, &fw->ctrl);
			goto done;
		}
		cobalt_dbg(1, "enabled fw\n");
		iowrite32(M00233_CONTROL_BITMAP_ENABLE_MEASURE_MSK |
			  M00233_CONTROL_BITMAP_ENABLE_INTERRUPT_MSK,
			  &vmr->control);
		iowrite32(M00473_CTRL_BITMAP_ENABLE_MSK, &fw->ctrl);
		s->enable_freewheel = false;
		s->unstable_frame = false;
		s->skip_first_frames = 2;
		skip = true;
		goto done;
	}
	if (ioread32(&fw->status) & M00473_STATUS_BITMAP_FREEWHEEL_MODE_MSK) {
restart_fw:
		cobalt_dbg(1, "lost lock\n");
		iowrite32(M00233_CONTROL_BITMAP_ENABLE_MEASURE_MSK,
			  &vmr->control);
		iowrite32(M00473_CTRL_BITMAP_ENABLE_MSK |
			  M00473_CTRL_BITMAP_FORCE_FREEWHEEL_MODE_MSK,
			  &fw->ctrl);
		iowrite32(0, &cvi->control);
		s->unstable_frame = true;
		s->enable_freewheel = false;
		s->enable_cvi = false;
	}
done:
	if (s->skip_first_frames) {
		skip = true;
		s->skip_first_frames--;
	}
	v4l2_get_timestamp(&cb->vb.timestamp);
	/* TODO: the sequence number should be read from the FPGA so we
	   also know about dropped frames. */
	cb->vb.sequence = s->sequence++;
	vb2_buffer_done(&cb->vb.vb2_buf,
			(skip || s->unstable_frame) ?
			VB2_BUF_STATE_REQUEUEING : VB2_BUF_STATE_DONE);
}

irqreturn_t cobalt_irq_handler(int irq, void *dev_id)
{
	struct cobalt *cobalt = (struct cobalt *)dev_id;
	u32 dma_interrupt =
		cobalt_read_bar0(cobalt, DMA_INTERRUPT_STATUS_REG) & 0xffff;
	u32 mask = cobalt_read_bar1(cobalt, COBALT_SYS_STAT_MASK);
	u32 edge = cobalt_read_bar1(cobalt, COBALT_SYS_STAT_EDGE);
	int i;

	/* Clear DMA interrupt */
	cobalt_write_bar0(cobalt, DMA_INTERRUPT_STATUS_REG, dma_interrupt);
	cobalt_write_bar1(cobalt, COBALT_SYS_STAT_MASK, mask & ~edge);
	cobalt_write_bar1(cobalt, COBALT_SYS_STAT_EDGE, edge);

	for (i = 0; i < COBALT_NUM_STREAMS; i++) {
		struct cobalt_stream *s = &cobalt->streams[i];
		unsigned dma_fifo_mask = s->dma_fifo_mask;

		if (dma_interrupt & (1 << s->dma_channel)) {
			cobalt->irq_dma[i]++;
			/* Give fresh buffer to user and chain newly
			 * queued buffers */
			cobalt_dma_stream_queue_handler(s);
			if (!s->is_audio) {
				edge &= ~dma_fifo_mask;
				cobalt_write_bar1(cobalt, COBALT_SYS_STAT_MASK,
						  mask & ~edge);
			}
		}
		if (s->is_audio)
			continue;
		if (edge & s->adv_irq_mask)
			set_bit(COBALT_STREAM_FL_ADV_IRQ, &s->flags);
		if ((edge & mask & dma_fifo_mask) && vb2_is_streaming(&s->q)) {
			cobalt_info("full rx FIFO %d\n", i);
			cobalt->irq_full_fifo++;
		}
	}

	queue_work(cobalt->irq_work_queues, &cobalt->irq_work_queue);

	if (edge & mask & (COBALT_SYSSTAT_VI0_INT1_MSK |
			   COBALT_SYSSTAT_VI1_INT1_MSK |
			   COBALT_SYSSTAT_VI2_INT1_MSK |
			   COBALT_SYSSTAT_VI3_INT1_MSK |
			   COBALT_SYSSTAT_VIHSMA_INT1_MSK |
			   COBALT_SYSSTAT_VOHSMA_INT1_MSK))
		cobalt->irq_adv1++;
	if (edge & mask & (COBALT_SYSSTAT_VI0_INT2_MSK |
			   COBALT_SYSSTAT_VI1_INT2_MSK |
			   COBALT_SYSSTAT_VI2_INT2_MSK |
			   COBALT_SYSSTAT_VI3_INT2_MSK |
			   COBALT_SYSSTAT_VIHSMA_INT2_MSK))
		cobalt->irq_adv2++;
	if (edge & mask & COBALT_SYSSTAT_VOHSMA_INT1_MSK)
		cobalt->irq_advout++;
	if (dma_interrupt)
		cobalt->irq_dma_tot++;
	if (!(edge & mask) && !dma_interrupt)
		cobalt->irq_none++;
	dma_interrupt = cobalt_read_bar0(cobalt, DMA_INTERRUPT_STATUS_REG);

	return IRQ_HANDLED;
}

void cobalt_irq_work_handler(struct work_struct *work)
{
	struct cobalt *cobalt =
		container_of(work, struct cobalt, irq_work_queue);
	int i;

	for (i = 0; i < COBALT_NUM_NODES; i++) {
		struct cobalt_stream *s = &cobalt->streams[i];

		if (test_and_clear_bit(COBALT_STREAM_FL_ADV_IRQ, &s->flags)) {
			u32 mask;

			v4l2_subdev_call(cobalt->streams[i].sd, core,
					interrupt_service_routine, 0, NULL);
			mask = cobalt_read_bar1(cobalt, COBALT_SYS_STAT_MASK);
			cobalt_write_bar1(cobalt, COBALT_SYS_STAT_MASK,
				mask | s->adv_irq_mask);
		}
	}
}

void cobalt_irq_log_status(struct cobalt *cobalt)
{
	u32 mask;
	int i;

	cobalt_info("irq: adv1=%u adv2=%u advout=%u none=%u full=%u\n",
		    cobalt->irq_adv1, cobalt->irq_adv2, cobalt->irq_advout,
		    cobalt->irq_none, cobalt->irq_full_fifo);
	cobalt_info("irq: dma_tot=%u (", cobalt->irq_dma_tot);
	for (i = 0; i < COBALT_NUM_STREAMS; i++)
		pr_cont("%s%u", i ? "/" : "", cobalt->irq_dma[i]);
	pr_cont(")\n");
	cobalt->irq_dma_tot = cobalt->irq_adv1 = cobalt->irq_adv2 = 0;
	cobalt->irq_advout = cobalt->irq_none = cobalt->irq_full_fifo = 0;
	memset(cobalt->irq_dma, 0, sizeof(cobalt->irq_dma));

	mask = cobalt_read_bar1(cobalt, COBALT_SYS_STAT_MASK);
	cobalt_write_bar1(cobalt, COBALT_SYS_STAT_MASK,
			mask |
			COBALT_SYSSTAT_VI0_LOST_DATA_MSK |
			COBALT_SYSSTAT_VI1_LOST_DATA_MSK |
			COBALT_SYSSTAT_VI2_LOST_DATA_MSK |
			COBALT_SYSSTAT_VI3_LOST_DATA_MSK |
			COBALT_SYSSTAT_VIHSMA_LOST_DATA_MSK |
			COBALT_SYSSTAT_VOHSMA_LOST_DATA_MSK |
			COBALT_SYSSTAT_AUD_IN_LOST_DATA_MSK |
			COBALT_SYSSTAT_AUD_OUT_LOST_DATA_MSK);
}
