/*
 *  Driver for the Conexant CX25821 PCIe bridge
 *
 *  Copyright (C) 2009 Conexant Systems Inc.
 *  Authors  <hiep.huynh@conexant.com>, <shu.lin@conexant.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "cx25821-video.h"
#include "cx25821-video-upstream.h"

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>

MODULE_DESCRIPTION("v4l2 driver module for cx25821 based TV cards");
MODULE_AUTHOR("Hiep Huynh <hiep.huynh@conexant.com>");
MODULE_LICENSE("GPL");

static int _intr_msk = FLD_VID_SRC_RISC1 | FLD_VID_SRC_UF | FLD_VID_SRC_SYNC |
			FLD_VID_SRC_OPC_ERR;

int cx25821_sram_channel_setup_upstream(struct cx25821_dev *dev,
					const struct sram_channel *ch,
					unsigned int bpl, u32 risc)
{
	unsigned int i, lines;
	u32 cdt;

	if (ch->cmds_start == 0) {
		cx_write(ch->ptr1_reg, 0);
		cx_write(ch->ptr2_reg, 0);
		cx_write(ch->cnt2_reg, 0);
		cx_write(ch->cnt1_reg, 0);
		return 0;
	}

	bpl = (bpl + 7) & ~7;	/* alignment */
	cdt = ch->cdt;
	lines = ch->fifo_size / bpl;

	if (lines > 4)
		lines = 4;

	BUG_ON(lines < 2);

	/* write CDT */
	for (i = 0; i < lines; i++) {
		cx_write(cdt + 16 * i, ch->fifo_start + bpl * i);
		cx_write(cdt + 16 * i + 4, 0);
		cx_write(cdt + 16 * i + 8, 0);
		cx_write(cdt + 16 * i + 12, 0);
	}

	/* write CMDS */
	cx_write(ch->cmds_start + 0, risc);

	cx_write(ch->cmds_start + 4, 0);
	cx_write(ch->cmds_start + 8, cdt);
	cx_write(ch->cmds_start + 12, (lines * 16) >> 3);
	cx_write(ch->cmds_start + 16, ch->ctrl_start);

	cx_write(ch->cmds_start + 20, VID_IQ_SIZE_DW);

	for (i = 24; i < 80; i += 4)
		cx_write(ch->cmds_start + i, 0);

	/* fill registers */
	cx_write(ch->ptr1_reg, ch->fifo_start);
	cx_write(ch->ptr2_reg, cdt);
	cx_write(ch->cnt2_reg, (lines * 16) >> 3);
	cx_write(ch->cnt1_reg, (bpl >> 3) - 1);

	return 0;
}

static __le32 *cx25821_update_riscprogram(struct cx25821_channel *chan,
					  __le32 *rp, unsigned int offset,
					  unsigned int bpl, u32 sync_line,
					  unsigned int lines, int fifo_enable,
					  int field_type)
{
	struct cx25821_video_out_data *out = chan->out;
	unsigned int line, i;
	int dist_betwn_starts = bpl * 2;

	*(rp++) = cpu_to_le32(RISC_RESYNC | sync_line);

	if (USE_RISC_NOOP_VIDEO) {
		for (i = 0; i < NUM_NO_OPS; i++)
			*(rp++) = cpu_to_le32(RISC_NOOP);
	}

	/* scan lines */
	for (line = 0; line < lines; line++) {
		*(rp++) = cpu_to_le32(RISC_READ | RISC_SOL | RISC_EOL | bpl);
		*(rp++) = cpu_to_le32(out->_data_buf_phys_addr + offset);
		*(rp++) = cpu_to_le32(0);	/* bits 63-32 */

		if ((lines <= NTSC_FIELD_HEIGHT)
		    || (line < (NTSC_FIELD_HEIGHT - 1)) || !(out->is_60hz)) {
			offset += dist_betwn_starts;
		}
	}

	return rp;
}

static __le32 *cx25821_risc_field_upstream(struct cx25821_channel *chan, __le32 *rp,
					   dma_addr_t databuf_phys_addr,
					   unsigned int offset, u32 sync_line,
					   unsigned int bpl, unsigned int lines,
					   int fifo_enable, int field_type)
{
	struct cx25821_video_out_data *out = chan->out;
	unsigned int line, i;
	const struct sram_channel *sram_ch = chan->sram_channels;
	int dist_betwn_starts = bpl * 2;

	/* sync instruction */
	if (sync_line != NO_SYNC_LINE)
		*(rp++) = cpu_to_le32(RISC_RESYNC | sync_line);

	if (USE_RISC_NOOP_VIDEO) {
		for (i = 0; i < NUM_NO_OPS; i++)
			*(rp++) = cpu_to_le32(RISC_NOOP);
	}

	/* scan lines */
	for (line = 0; line < lines; line++) {
		*(rp++) = cpu_to_le32(RISC_READ | RISC_SOL | RISC_EOL | bpl);
		*(rp++) = cpu_to_le32(databuf_phys_addr + offset);
		*(rp++) = cpu_to_le32(0);	/* bits 63-32 */

		if ((lines <= NTSC_FIELD_HEIGHT)
		    || (line < (NTSC_FIELD_HEIGHT - 1)) || !(out->is_60hz))
			/* to skip the other field line */
			offset += dist_betwn_starts;

		/* check if we need to enable the FIFO after the first 4 lines
		 * For the upstream video channel, the risc engine will enable
		 * the FIFO. */
		if (fifo_enable && line == 3) {
			*(rp++) = RISC_WRITECR;
			*(rp++) = sram_ch->dma_ctl;
			*(rp++) = FLD_VID_FIFO_EN;
			*(rp++) = 0x00000001;
		}
	}

	return rp;
}

static int cx25821_risc_buffer_upstream(struct cx25821_channel *chan,
					struct pci_dev *pci,
					unsigned int top_offset,
					unsigned int bpl, unsigned int lines)
{
	struct cx25821_video_out_data *out = chan->out;
	__le32 *rp;
	int fifo_enable = 0;
	/* get line count for single field */
	int singlefield_lines = lines >> 1;
	int odd_num_lines = singlefield_lines;
	int frame = 0;
	int frame_size = 0;
	int databuf_offset = 0;
	int risc_program_size = 0;
	int risc_flag = RISC_CNT_RESET;
	unsigned int bottom_offset = bpl;
	dma_addr_t risc_phys_jump_addr;

	if (out->is_60hz) {
		odd_num_lines = singlefield_lines + 1;
		risc_program_size = FRAME1_VID_PROG_SIZE;
		frame_size = (bpl == Y411_LINE_SZ) ?
			FRAME_SIZE_NTSC_Y411 : FRAME_SIZE_NTSC_Y422;
	} else {
		risc_program_size = PAL_VID_PROG_SIZE;
		frame_size = (bpl == Y411_LINE_SZ) ?
			FRAME_SIZE_PAL_Y411 : FRAME_SIZE_PAL_Y422;
	}

	/* Virtual address of Risc buffer program */
	rp = out->_dma_virt_addr;

	for (frame = 0; frame < NUM_FRAMES; frame++) {
		databuf_offset = frame_size * frame;

		if (UNSET != top_offset) {
			fifo_enable = (frame == 0) ? FIFO_ENABLE : FIFO_DISABLE;
			rp = cx25821_risc_field_upstream(chan, rp,
					out->_data_buf_phys_addr +
					databuf_offset, top_offset, 0, bpl,
					odd_num_lines, fifo_enable, ODD_FIELD);
		}

		fifo_enable = FIFO_DISABLE;

		/* Even Field */
		rp = cx25821_risc_field_upstream(chan, rp,
						 out->_data_buf_phys_addr +
						 databuf_offset, bottom_offset,
						 0x200, bpl, singlefield_lines,
						 fifo_enable, EVEN_FIELD);

		if (frame == 0) {
			risc_flag = RISC_CNT_RESET;
			risc_phys_jump_addr = out->_dma_phys_start_addr +
				risc_program_size;
		} else {
			risc_phys_jump_addr = out->_dma_phys_start_addr;
			risc_flag = RISC_CNT_INC;
		}

		/* Loop to 2ndFrameRISC or to Start of Risc
		 * program & generate IRQ
		 */
		*(rp++) = cpu_to_le32(RISC_JUMP | RISC_IRQ1 | risc_flag);
		*(rp++) = cpu_to_le32(risc_phys_jump_addr);
		*(rp++) = cpu_to_le32(0);
	}

	return 0;
}

void cx25821_stop_upstream_video(struct cx25821_channel *chan)
{
	struct cx25821_video_out_data *out = chan->out;
	struct cx25821_dev *dev = chan->dev;
	const struct sram_channel *sram_ch = chan->sram_channels;
	u32 tmp = 0;

	if (!out->_is_running) {
		pr_info("No video file is currently running so return!\n");
		return;
	}

	/* Set the interrupt mask register, disable irq. */
	cx_set(PCI_INT_MSK, cx_read(PCI_INT_MSK) & ~(1 << sram_ch->irq_bit));

	/* Disable RISC interrupts */
	tmp = cx_read(sram_ch->int_msk);
	cx_write(sram_ch->int_msk, tmp & ~_intr_msk);

	/* Turn OFF risc and fifo enable */
	tmp = cx_read(sram_ch->dma_ctl);
	cx_write(sram_ch->dma_ctl, tmp & ~(FLD_VID_FIFO_EN | FLD_VID_RISC_EN));

	free_irq(dev->pci->irq, chan);

	/* Clear data buffer memory */
	if (out->_data_buf_virt_addr)
		memset(out->_data_buf_virt_addr, 0, out->_data_buf_size);

	out->_is_running = 0;
	out->_is_first_frame = 0;
	out->_frame_count = 0;
	out->_file_status = END_OF_FILE;

	tmp = cx_read(VID_CH_MODE_SEL);
	cx_write(VID_CH_MODE_SEL, tmp & 0xFFFFFE00);
}

void cx25821_free_mem_upstream(struct cx25821_channel *chan)
{
	struct cx25821_video_out_data *out = chan->out;
	struct cx25821_dev *dev = chan->dev;

	if (out->_is_running)
		cx25821_stop_upstream_video(chan);

	if (out->_dma_virt_addr) {
		pci_free_consistent(dev->pci, out->_risc_size,
				    out->_dma_virt_addr, out->_dma_phys_addr);
		out->_dma_virt_addr = NULL;
	}

	if (out->_data_buf_virt_addr) {
		pci_free_consistent(dev->pci, out->_data_buf_size,
				    out->_data_buf_virt_addr,
				    out->_data_buf_phys_addr);
		out->_data_buf_virt_addr = NULL;
	}
}

int cx25821_write_frame(struct cx25821_channel *chan,
		const char __user *data, size_t count)
{
	struct cx25821_video_out_data *out = chan->out;
	int line_size = (out->_pixel_format == PIXEL_FRMT_411) ?
		Y411_LINE_SZ : Y422_LINE_SZ;
	int frame_size = 0;
	int frame_offset = 0;
	int curpos = out->curpos;

	if (out->is_60hz)
		frame_size = (line_size == Y411_LINE_SZ) ?
			FRAME_SIZE_NTSC_Y411 : FRAME_SIZE_NTSC_Y422;
	else
		frame_size = (line_size == Y411_LINE_SZ) ?
			FRAME_SIZE_PAL_Y411 : FRAME_SIZE_PAL_Y422;

	if (curpos == 0) {
		out->cur_frame_index = out->_frame_index;
		if (wait_event_interruptible(out->waitq, out->cur_frame_index != out->_frame_index))
			return -EINTR;
		out->cur_frame_index = out->_frame_index;
	}

	frame_offset = out->cur_frame_index ? frame_size : 0;

	if (frame_size - curpos < count)
		count = frame_size - curpos;
	memcpy((char *)out->_data_buf_virt_addr + frame_offset + curpos,
			data, count);
	curpos += count;
	if (curpos == frame_size) {
		out->_frame_count++;
		curpos = 0;
	}
	out->curpos = curpos;

	return count;
}

static int cx25821_upstream_buffer_prepare(struct cx25821_channel *chan,
					   const struct sram_channel *sram_ch,
					   int bpl)
{
	struct cx25821_video_out_data *out = chan->out;
	struct cx25821_dev *dev = chan->dev;
	int ret = 0;
	dma_addr_t dma_addr;
	dma_addr_t data_dma_addr;

	if (out->_dma_virt_addr != NULL)
		pci_free_consistent(dev->pci, out->upstream_riscbuf_size,
				out->_dma_virt_addr, out->_dma_phys_addr);

	out->_dma_virt_addr = pci_alloc_consistent(dev->pci,
			out->upstream_riscbuf_size, &dma_addr);
	out->_dma_virt_start_addr = out->_dma_virt_addr;
	out->_dma_phys_start_addr = dma_addr;
	out->_dma_phys_addr = dma_addr;
	out->_risc_size = out->upstream_riscbuf_size;

	if (!out->_dma_virt_addr) {
		pr_err("FAILED to allocate memory for Risc buffer! Returning\n");
		return -ENOMEM;
	}

	/* Clear memory at address */
	memset(out->_dma_virt_addr, 0, out->_risc_size);

	if (out->_data_buf_virt_addr != NULL)
		pci_free_consistent(dev->pci, out->upstream_databuf_size,
				out->_data_buf_virt_addr,
				out->_data_buf_phys_addr);
	/* For Video Data buffer allocation */
	out->_data_buf_virt_addr = pci_alloc_consistent(dev->pci,
			out->upstream_databuf_size, &data_dma_addr);
	out->_data_buf_phys_addr = data_dma_addr;
	out->_data_buf_size = out->upstream_databuf_size;

	if (!out->_data_buf_virt_addr) {
		pr_err("FAILED to allocate memory for data buffer! Returning\n");
		return -ENOMEM;
	}

	/* Clear memory at address */
	memset(out->_data_buf_virt_addr, 0, out->_data_buf_size);

	/* Create RISC programs */
	ret = cx25821_risc_buffer_upstream(chan, dev->pci, 0, bpl,
			out->_lines_count);
	if (ret < 0) {
		pr_info("Failed creating Video Upstream Risc programs!\n");
		goto error;
	}

	return 0;

error:
	return ret;
}

static int cx25821_video_upstream_irq(struct cx25821_channel *chan, u32 status)
{
	struct cx25821_video_out_data *out = chan->out;
	struct cx25821_dev *dev = chan->dev;
	u32 int_msk_tmp;
	const struct sram_channel *channel = chan->sram_channels;
	int singlefield_lines = NTSC_FIELD_HEIGHT;
	int line_size_in_bytes = Y422_LINE_SZ;
	int odd_risc_prog_size = 0;
	dma_addr_t risc_phys_jump_addr;
	__le32 *rp;

	if (status & FLD_VID_SRC_RISC1) {
		/* We should only process one program per call */
		u32 prog_cnt = cx_read(channel->gpcnt);

		/* Since we've identified our IRQ, clear our bits from the
		 * interrupt mask and interrupt status registers */
		int_msk_tmp = cx_read(channel->int_msk);
		cx_write(channel->int_msk, int_msk_tmp & ~_intr_msk);
		cx_write(channel->int_stat, _intr_msk);

		wake_up(&out->waitq);

		spin_lock(&dev->slock);

		out->_frame_index = prog_cnt;

		if (out->_is_first_frame) {
			out->_is_first_frame = 0;

			if (out->is_60hz) {
				singlefield_lines += 1;
				odd_risc_prog_size = ODD_FLD_NTSC_PROG_SIZE;
			} else {
				singlefield_lines = PAL_FIELD_HEIGHT;
				odd_risc_prog_size = ODD_FLD_PAL_PROG_SIZE;
			}

			if (out->_dma_virt_start_addr != NULL) {
				line_size_in_bytes =
				    (out->_pixel_format ==
				     PIXEL_FRMT_411) ? Y411_LINE_SZ :
				    Y422_LINE_SZ;
				risc_phys_jump_addr =
				    out->_dma_phys_start_addr +
				    odd_risc_prog_size;

				rp = cx25821_update_riscprogram(chan,
					out->_dma_virt_start_addr, TOP_OFFSET,
					line_size_in_bytes, 0x0,
					singlefield_lines, FIFO_DISABLE,
					ODD_FIELD);

				/* Jump to Even Risc program of 1st Frame */
				*(rp++) = cpu_to_le32(RISC_JUMP);
				*(rp++) = cpu_to_le32(risc_phys_jump_addr);
				*(rp++) = cpu_to_le32(0);
			}
		}

		spin_unlock(&dev->slock);
	} else {
		if (status & FLD_VID_SRC_UF)
			pr_err("%s(): Video Received Underflow Error Interrupt!\n",
			       __func__);

		if (status & FLD_VID_SRC_SYNC)
			pr_err("%s(): Video Received Sync Error Interrupt!\n",
			       __func__);

		if (status & FLD_VID_SRC_OPC_ERR)
			pr_err("%s(): Video Received OpCode Error Interrupt!\n",
			       __func__);
	}

	if (out->_file_status == END_OF_FILE) {
		pr_err("EOF Channel 1 Framecount = %d\n", out->_frame_count);
		return -1;
	}
	/* ElSE, set the interrupt mask register, re-enable irq. */
	int_msk_tmp = cx_read(channel->int_msk);
	cx_write(channel->int_msk, int_msk_tmp |= _intr_msk);

	return 0;
}

static irqreturn_t cx25821_upstream_irq(int irq, void *dev_id)
{
	struct cx25821_channel *chan = dev_id;
	struct cx25821_dev *dev = chan->dev;
	u32 vid_status;
	int handled = 0;
	const struct sram_channel *sram_ch;

	if (!dev)
		return -1;

	sram_ch = chan->sram_channels;

	vid_status = cx_read(sram_ch->int_stat);

	/* Only deal with our interrupt */
	if (vid_status)
		handled = cx25821_video_upstream_irq(chan, vid_status);

	return IRQ_RETVAL(handled);
}

static void cx25821_set_pixelengine(struct cx25821_channel *chan,
				    const struct sram_channel *ch,
				    int pix_format)
{
	struct cx25821_video_out_data *out = chan->out;
	struct cx25821_dev *dev = chan->dev;
	int width = WIDTH_D1;
	int height = out->_lines_count;
	int num_lines, odd_num_lines;
	u32 value;
	int vip_mode = OUTPUT_FRMT_656;

	value = ((pix_format & 0x3) << 12) | (vip_mode & 0x7);
	value &= 0xFFFFFFEF;
	value |= out->is_60hz ? 0 : 0x10;
	cx_write(ch->vid_fmt_ctl, value);

	/* set number of active pixels in each line.
	 * Default is 720 pixels in both NTSC and PAL format */
	cx_write(ch->vid_active_ctl1, width);

	num_lines = (height / 2) & 0x3FF;
	odd_num_lines = num_lines;

	if (out->is_60hz)
		odd_num_lines += 1;

	value = (num_lines << 16) | odd_num_lines;

	/* set number of active lines in field 0 (top) and field 1 (bottom) */
	cx_write(ch->vid_active_ctl2, value);

	cx_write(ch->vid_cdt_size, VID_CDT_SIZE >> 3);
}

static int cx25821_start_video_dma_upstream(struct cx25821_channel *chan,
					    const struct sram_channel *sram_ch)
{
	struct cx25821_video_out_data *out = chan->out;
	struct cx25821_dev *dev = chan->dev;
	u32 tmp = 0;
	int err = 0;

	/* 656/VIP SRC Upstream Channel I & J and 7 - Host Bus Interface for
	 * channel A-C
	 */
	tmp = cx_read(VID_CH_MODE_SEL);
	cx_write(VID_CH_MODE_SEL, tmp | 0x1B0001FF);

	/* Set the physical start address of the RISC program in the initial
	 * program counter(IPC) member of the cmds.
	 */
	cx_write(sram_ch->cmds_start + 0, out->_dma_phys_addr);
	/* Risc IPC High 64 bits 63-32 */
	cx_write(sram_ch->cmds_start + 4, 0);

	/* reset counter */
	cx_write(sram_ch->gpcnt_ctl, 3);

	/* Clear our bits from the interrupt status register. */
	cx_write(sram_ch->int_stat, _intr_msk);

	/* Set the interrupt mask register, enable irq. */
	cx_set(PCI_INT_MSK, cx_read(PCI_INT_MSK) | (1 << sram_ch->irq_bit));
	tmp = cx_read(sram_ch->int_msk);
	cx_write(sram_ch->int_msk, tmp |= _intr_msk);

	err = request_irq(dev->pci->irq, cx25821_upstream_irq,
			IRQF_SHARED, dev->name, chan);
	if (err < 0) {
		pr_err("%s: can't get upstream IRQ %d\n",
		       dev->name, dev->pci->irq);
		goto fail_irq;
	}

	/* Start the DMA  engine */
	tmp = cx_read(sram_ch->dma_ctl);
	cx_set(sram_ch->dma_ctl, tmp | FLD_VID_RISC_EN);

	out->_is_running = 1;
	out->_is_first_frame = 1;

	return 0;

fail_irq:
	cx25821_dev_unregister(dev);
	return err;
}

int cx25821_vidupstream_init(struct cx25821_channel *chan,
				 int pixel_format)
{
	struct cx25821_video_out_data *out = chan->out;
	struct cx25821_dev *dev = chan->dev;
	const struct sram_channel *sram_ch;
	u32 tmp;
	int err = 0;
	int data_frame_size = 0;
	int risc_buffer_size = 0;

	if (out->_is_running) {
		pr_info("Video Channel is still running so return!\n");
		return 0;
	}

	sram_ch = chan->sram_channels;

	out->is_60hz = dev->tvnorm & V4L2_STD_525_60;

	/* 656/VIP SRC Upstream Channel I & J and 7 - Host Bus Interface for
	 * channel A-C
	 */
	tmp = cx_read(VID_CH_MODE_SEL);
	cx_write(VID_CH_MODE_SEL, tmp | 0x1B0001FF);

	out->_is_running = 0;
	out->_frame_count = 0;
	out->_file_status = RESET_STATUS;
	out->_lines_count = out->is_60hz ? 480 : 576;
	out->_pixel_format = pixel_format;
	out->_line_size = (out->_pixel_format == PIXEL_FRMT_422) ?
		(WIDTH_D1 * 2) : (WIDTH_D1 * 3) / 2;
	data_frame_size = out->is_60hz ? NTSC_DATA_BUF_SZ : PAL_DATA_BUF_SZ;
	risc_buffer_size = out->is_60hz ?
		NTSC_RISC_BUF_SIZE : PAL_RISC_BUF_SIZE;

	out->_is_running = 0;
	out->_frame_count = 0;
	out->_file_status = RESET_STATUS;
	out->_lines_count = out->is_60hz ? 480 : 576;
	out->_pixel_format = pixel_format;
	out->_line_size = (out->_pixel_format == PIXEL_FRMT_422) ?
		(WIDTH_D1 * 2) : (WIDTH_D1 * 3) / 2;
	out->curpos = 0;
	init_waitqueue_head(&out->waitq);

	err = cx25821_sram_channel_setup_upstream(dev, sram_ch,
			out->_line_size, 0);

	/* setup fifo + format */
	cx25821_set_pixelengine(chan, sram_ch, out->_pixel_format);

	out->upstream_riscbuf_size = risc_buffer_size * 2;
	out->upstream_databuf_size = data_frame_size * 2;

	/* Allocating buffers and prepare RISC program */
	err = cx25821_upstream_buffer_prepare(chan, sram_ch, out->_line_size);
	if (err < 0) {
		pr_err("%s: Failed to set up Video upstream buffers!\n",
		       dev->name);
		goto error;
	}

	cx25821_start_video_dma_upstream(chan, sram_ch);

	return 0;

error:
	cx25821_dev_unregister(dev);

	return err;
}
