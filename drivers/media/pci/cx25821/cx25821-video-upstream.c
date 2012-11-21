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

#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/syscalls.h>
#include <linux/file.h>
#include <linux/fcntl.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

MODULE_DESCRIPTION("v4l2 driver module for cx25821 based TV cards");
MODULE_AUTHOR("Hiep Huynh <hiep.huynh@conexant.com>");
MODULE_LICENSE("GPL");

static int _intr_msk = FLD_VID_SRC_RISC1 | FLD_VID_SRC_UF | FLD_VID_SRC_SYNC |
			FLD_VID_SRC_OPC_ERR;

int cx25821_sram_channel_setup_upstream(struct cx25821_dev *dev,
					struct sram_channel *ch,
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

static __le32 *cx25821_update_riscprogram(struct cx25821_dev *dev,
					  __le32 *rp, unsigned int offset,
					  unsigned int bpl, u32 sync_line,
					  unsigned int lines, int fifo_enable,
					  int field_type)
{
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
		*(rp++) = cpu_to_le32(dev->_data_buf_phys_addr + offset);
		*(rp++) = cpu_to_le32(0);	/* bits 63-32 */

		if ((lines <= NTSC_FIELD_HEIGHT)
		    || (line < (NTSC_FIELD_HEIGHT - 1)) || !(dev->_isNTSC)) {
			offset += dist_betwn_starts;
		}
	}

	return rp;
}

static __le32 *cx25821_risc_field_upstream(struct cx25821_dev *dev, __le32 * rp,
					   dma_addr_t databuf_phys_addr,
					   unsigned int offset, u32 sync_line,
					   unsigned int bpl, unsigned int lines,
					   int fifo_enable, int field_type)
{
	unsigned int line, i;
	struct sram_channel *sram_ch =
		dev->channels[dev->_channel_upstream_select].sram_channels;
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
		    || (line < (NTSC_FIELD_HEIGHT - 1)) || !(dev->_isNTSC))
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

int cx25821_risc_buffer_upstream(struct cx25821_dev *dev,
				 struct pci_dev *pci,
				 unsigned int top_offset,
				 unsigned int bpl, unsigned int lines)
{
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

	if (dev->_isNTSC) {
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
	rp = dev->_dma_virt_addr;

	for (frame = 0; frame < NUM_FRAMES; frame++) {
		databuf_offset = frame_size * frame;

		if (UNSET != top_offset) {
			fifo_enable = (frame == 0) ? FIFO_ENABLE : FIFO_DISABLE;
			rp = cx25821_risc_field_upstream(dev, rp,
					dev->_data_buf_phys_addr +
					databuf_offset, top_offset, 0, bpl,
					odd_num_lines, fifo_enable, ODD_FIELD);
		}

		fifo_enable = FIFO_DISABLE;

		/* Even Field */
		rp = cx25821_risc_field_upstream(dev, rp,
						 dev->_data_buf_phys_addr +
						 databuf_offset, bottom_offset,
						 0x200, bpl, singlefield_lines,
						 fifo_enable, EVEN_FIELD);

		if (frame == 0) {
			risc_flag = RISC_CNT_RESET;
			risc_phys_jump_addr = dev->_dma_phys_start_addr +
				risc_program_size;
		} else {
			risc_phys_jump_addr = dev->_dma_phys_start_addr;
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

void cx25821_stop_upstream_video_ch1(struct cx25821_dev *dev)
{
	struct sram_channel *sram_ch =
		dev->channels[VID_UPSTREAM_SRAM_CHANNEL_I].sram_channels;
	u32 tmp = 0;

	if (!dev->_is_running) {
		pr_info("No video file is currently running so return!\n");
		return;
	}
	/* Disable RISC interrupts */
	tmp = cx_read(sram_ch->int_msk);
	cx_write(sram_ch->int_msk, tmp & ~_intr_msk);

	/* Turn OFF risc and fifo enable */
	tmp = cx_read(sram_ch->dma_ctl);
	cx_write(sram_ch->dma_ctl, tmp & ~(FLD_VID_FIFO_EN | FLD_VID_RISC_EN));

	/* Clear data buffer memory */
	if (dev->_data_buf_virt_addr)
		memset(dev->_data_buf_virt_addr, 0, dev->_data_buf_size);

	dev->_is_running = 0;
	dev->_is_first_frame = 0;
	dev->_frame_count = 0;
	dev->_file_status = END_OF_FILE;

	kfree(dev->_irq_queues);
	dev->_irq_queues = NULL;

	kfree(dev->_filename);

	tmp = cx_read(VID_CH_MODE_SEL);
	cx_write(VID_CH_MODE_SEL, tmp & 0xFFFFFE00);
}

void cx25821_free_mem_upstream_ch1(struct cx25821_dev *dev)
{
	if (dev->_is_running)
		cx25821_stop_upstream_video_ch1(dev);

	if (dev->_dma_virt_addr) {
		pci_free_consistent(dev->pci, dev->_risc_size,
				    dev->_dma_virt_addr, dev->_dma_phys_addr);
		dev->_dma_virt_addr = NULL;
	}

	if (dev->_data_buf_virt_addr) {
		pci_free_consistent(dev->pci, dev->_data_buf_size,
				    dev->_data_buf_virt_addr,
				    dev->_data_buf_phys_addr);
		dev->_data_buf_virt_addr = NULL;
	}
}

int cx25821_get_frame(struct cx25821_dev *dev, struct sram_channel *sram_ch)
{
	struct file *myfile;
	int frame_index_temp = dev->_frame_index;
	int i = 0;
	int line_size = (dev->_pixel_format == PIXEL_FRMT_411) ?
		Y411_LINE_SZ : Y422_LINE_SZ;
	int frame_size = 0;
	int frame_offset = 0;
	ssize_t vfs_read_retval = 0;
	char mybuf[line_size];
	loff_t file_offset;
	loff_t pos;
	mm_segment_t old_fs;

	if (dev->_file_status == END_OF_FILE)
		return 0;

	if (dev->_isNTSC)
		frame_size = (line_size == Y411_LINE_SZ) ?
			FRAME_SIZE_NTSC_Y411 : FRAME_SIZE_NTSC_Y422;
	else
		frame_size = (line_size == Y411_LINE_SZ) ?
			FRAME_SIZE_PAL_Y411 : FRAME_SIZE_PAL_Y422;

	frame_offset = (frame_index_temp > 0) ? frame_size : 0;
	file_offset = dev->_frame_count * frame_size;

	myfile = filp_open(dev->_filename, O_RDONLY | O_LARGEFILE, 0);

	if (IS_ERR(myfile)) {
		const int open_errno = -PTR_ERR(myfile);
		pr_err("%s(): ERROR opening file(%s) with errno = %d!\n",
		       __func__, dev->_filename, open_errno);
		return PTR_ERR(myfile);
	} else {
		if (!(myfile->f_op)) {
			pr_err("%s(): File has no file operations registered!\n",
			       __func__);
			filp_close(myfile, NULL);
			return -EIO;
		}

		if (!myfile->f_op->read) {
			pr_err("%s(): File has no READ operations registered!\n",
			       __func__);
			filp_close(myfile, NULL);
			return -EIO;
		}

		pos = myfile->f_pos;
		old_fs = get_fs();
		set_fs(KERNEL_DS);

		for (i = 0; i < dev->_lines_count; i++) {
			pos = file_offset;

			vfs_read_retval = vfs_read(myfile, mybuf, line_size,
					&pos);

			if (vfs_read_retval > 0 && vfs_read_retval == line_size
			    && dev->_data_buf_virt_addr != NULL) {
				memcpy((void *)(dev->_data_buf_virt_addr +
						frame_offset / 4), mybuf,
				       vfs_read_retval);
			}

			file_offset += vfs_read_retval;
			frame_offset += vfs_read_retval;

			if (vfs_read_retval < line_size) {
				pr_info("Done: exit %s() since no more bytes to read from Video file\n",
					__func__);
				break;
			}
		}

		if (i > 0)
			dev->_frame_count++;

		dev->_file_status = (vfs_read_retval == line_size) ?
			IN_PROGRESS : END_OF_FILE;

		set_fs(old_fs);
		filp_close(myfile, NULL);
	}

	return 0;
}

static void cx25821_vidups_handler(struct work_struct *work)
{
	struct cx25821_dev *dev = container_of(work, struct cx25821_dev,
			_irq_work_entry);

	if (!dev) {
		pr_err("ERROR %s(): since container_of(work_struct) FAILED!\n",
		       __func__);
		return;
	}

	cx25821_get_frame(dev, dev->channels[dev->_channel_upstream_select].
			sram_channels);
}

int cx25821_openfile(struct cx25821_dev *dev, struct sram_channel *sram_ch)
{
	struct file *myfile;
	int i = 0, j = 0;
	int line_size = (dev->_pixel_format == PIXEL_FRMT_411) ?
		Y411_LINE_SZ : Y422_LINE_SZ;
	ssize_t vfs_read_retval = 0;
	char mybuf[line_size];
	loff_t pos;
	loff_t offset = (unsigned long)0;
	mm_segment_t old_fs;

	myfile = filp_open(dev->_filename, O_RDONLY | O_LARGEFILE, 0);

	if (IS_ERR(myfile)) {
		const int open_errno = -PTR_ERR(myfile);
		pr_err("%s(): ERROR opening file(%s) with errno = %d!\n",
		       __func__, dev->_filename, open_errno);
		return PTR_ERR(myfile);
	} else {
		if (!(myfile->f_op)) {
			pr_err("%s(): File has no file operations registered!\n",
			       __func__);
			filp_close(myfile, NULL);
			return -EIO;
		}

		if (!myfile->f_op->read) {
			pr_err("%s(): File has no READ operations registered!  Returning\n",
			       __func__);
			filp_close(myfile, NULL);
			return -EIO;
		}

		pos = myfile->f_pos;
		old_fs = get_fs();
		set_fs(KERNEL_DS);

		for (j = 0; j < NUM_FRAMES; j++) {
			for (i = 0; i < dev->_lines_count; i++) {
				pos = offset;

				vfs_read_retval = vfs_read(myfile, mybuf,
						line_size, &pos);

				if (vfs_read_retval > 0
				    && vfs_read_retval == line_size
				    && dev->_data_buf_virt_addr != NULL) {
					memcpy((void *)(dev->
							_data_buf_virt_addr +
							offset / 4), mybuf,
					       vfs_read_retval);
				}

				offset += vfs_read_retval;

				if (vfs_read_retval < line_size) {
					pr_info("Done: exit %s() since no more bytes to read from Video file\n",
						__func__);
					break;
				}
			}

			if (i > 0)
				dev->_frame_count++;

			if (vfs_read_retval < line_size)
				break;
		}

		dev->_file_status = (vfs_read_retval == line_size) ?
			IN_PROGRESS : END_OF_FILE;

		set_fs(old_fs);
		myfile->f_pos = 0;
		filp_close(myfile, NULL);
	}

	return 0;
}

int cx25821_upstream_buffer_prepare(struct cx25821_dev *dev,
				    struct sram_channel *sram_ch, int bpl)
{
	int ret = 0;
	dma_addr_t dma_addr;
	dma_addr_t data_dma_addr;

	if (dev->_dma_virt_addr != NULL)
		pci_free_consistent(dev->pci, dev->upstream_riscbuf_size,
				dev->_dma_virt_addr, dev->_dma_phys_addr);

	dev->_dma_virt_addr = pci_alloc_consistent(dev->pci,
			dev->upstream_riscbuf_size, &dma_addr);
	dev->_dma_virt_start_addr = dev->_dma_virt_addr;
	dev->_dma_phys_start_addr = dma_addr;
	dev->_dma_phys_addr = dma_addr;
	dev->_risc_size = dev->upstream_riscbuf_size;

	if (!dev->_dma_virt_addr) {
		pr_err("FAILED to allocate memory for Risc buffer! Returning\n");
		return -ENOMEM;
	}

	/* Clear memory at address */
	memset(dev->_dma_virt_addr, 0, dev->_risc_size);

	if (dev->_data_buf_virt_addr != NULL)
		pci_free_consistent(dev->pci, dev->upstream_databuf_size,
				dev->_data_buf_virt_addr,
				dev->_data_buf_phys_addr);
	/* For Video Data buffer allocation */
	dev->_data_buf_virt_addr = pci_alloc_consistent(dev->pci,
			dev->upstream_databuf_size, &data_dma_addr);
	dev->_data_buf_phys_addr = data_dma_addr;
	dev->_data_buf_size = dev->upstream_databuf_size;

	if (!dev->_data_buf_virt_addr) {
		pr_err("FAILED to allocate memory for data buffer! Returning\n");
		return -ENOMEM;
	}

	/* Clear memory at address */
	memset(dev->_data_buf_virt_addr, 0, dev->_data_buf_size);

	ret = cx25821_openfile(dev, sram_ch);
	if (ret < 0)
		return ret;

	/* Create RISC programs */
	ret = cx25821_risc_buffer_upstream(dev, dev->pci, 0, bpl,
			dev->_lines_count);
	if (ret < 0) {
		pr_info("Failed creating Video Upstream Risc programs!\n");
		goto error;
	}

	return 0;

error:
	return ret;
}

int cx25821_video_upstream_irq(struct cx25821_dev *dev, int chan_num,
			       u32 status)
{
	u32 int_msk_tmp;
	struct sram_channel *channel = dev->channels[chan_num].sram_channels;
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

		spin_lock(&dev->slock);

		dev->_frame_index = prog_cnt;

		queue_work(dev->_irq_queues, &dev->_irq_work_entry);

		if (dev->_is_first_frame) {
			dev->_is_first_frame = 0;

			if (dev->_isNTSC) {
				singlefield_lines += 1;
				odd_risc_prog_size = ODD_FLD_NTSC_PROG_SIZE;
			} else {
				singlefield_lines = PAL_FIELD_HEIGHT;
				odd_risc_prog_size = ODD_FLD_PAL_PROG_SIZE;
			}

			if (dev->_dma_virt_start_addr != NULL) {
				line_size_in_bytes =
				    (dev->_pixel_format ==
				     PIXEL_FRMT_411) ? Y411_LINE_SZ :
				    Y422_LINE_SZ;
				risc_phys_jump_addr =
				    dev->_dma_phys_start_addr +
				    odd_risc_prog_size;

				rp = cx25821_update_riscprogram(dev,
					dev->_dma_virt_start_addr, TOP_OFFSET,
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

	if (dev->_file_status == END_OF_FILE) {
		pr_err("EOF Channel 1 Framecount = %d\n", dev->_frame_count);
		return -1;
	}
	/* ElSE, set the interrupt mask register, re-enable irq. */
	int_msk_tmp = cx_read(channel->int_msk);
	cx_write(channel->int_msk, int_msk_tmp |= _intr_msk);

	return 0;
}

static irqreturn_t cx25821_upstream_irq(int irq, void *dev_id)
{
	struct cx25821_dev *dev = dev_id;
	u32 vid_status;
	int handled = 0;
	int channel_num = 0;
	struct sram_channel *sram_ch;

	if (!dev)
		return -1;

	channel_num = VID_UPSTREAM_SRAM_CHANNEL_I;

	sram_ch = dev->channels[channel_num].sram_channels;

	vid_status = cx_read(sram_ch->int_stat);

	/* Only deal with our interrupt */
	if (vid_status)
		handled = cx25821_video_upstream_irq(dev, channel_num,
				vid_status);

	if (handled < 0)
		cx25821_stop_upstream_video_ch1(dev);
	else
		handled += handled;

	return IRQ_RETVAL(handled);
}

void cx25821_set_pixelengine(struct cx25821_dev *dev, struct sram_channel *ch,
			     int pix_format)
{
	int width = WIDTH_D1;
	int height = dev->_lines_count;
	int num_lines, odd_num_lines;
	u32 value;
	int vip_mode = OUTPUT_FRMT_656;

	value = ((pix_format & 0x3) << 12) | (vip_mode & 0x7);
	value &= 0xFFFFFFEF;
	value |= dev->_isNTSC ? 0 : 0x10;
	cx_write(ch->vid_fmt_ctl, value);

	/* set number of active pixels in each line.
	 * Default is 720 pixels in both NTSC and PAL format */
	cx_write(ch->vid_active_ctl1, width);

	num_lines = (height / 2) & 0x3FF;
	odd_num_lines = num_lines;

	if (dev->_isNTSC)
		odd_num_lines += 1;

	value = (num_lines << 16) | odd_num_lines;

	/* set number of active lines in field 0 (top) and field 1 (bottom) */
	cx_write(ch->vid_active_ctl2, value);

	cx_write(ch->vid_cdt_size, VID_CDT_SIZE >> 3);
}

int cx25821_start_video_dma_upstream(struct cx25821_dev *dev,
				     struct sram_channel *sram_ch)
{
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
	cx_write(sram_ch->cmds_start + 0, dev->_dma_phys_addr);
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
			IRQF_SHARED, dev->name, dev);
	if (err < 0) {
		pr_err("%s: can't get upstream IRQ %d\n",
		       dev->name, dev->pci->irq);
		goto fail_irq;
	}

	/* Start the DMA  engine */
	tmp = cx_read(sram_ch->dma_ctl);
	cx_set(sram_ch->dma_ctl, tmp | FLD_VID_RISC_EN);

	dev->_is_running = 1;
	dev->_is_first_frame = 1;

	return 0;

fail_irq:
	cx25821_dev_unregister(dev);
	return err;
}

int cx25821_vidupstream_init_ch1(struct cx25821_dev *dev, int channel_select,
				 int pixel_format)
{
	struct sram_channel *sram_ch;
	u32 tmp;
	int retval = 0;
	int err = 0;
	int data_frame_size = 0;
	int risc_buffer_size = 0;
	int str_length = 0;

	if (dev->_is_running) {
		pr_info("Video Channel is still running so return!\n");
		return 0;
	}

	dev->_channel_upstream_select = channel_select;
	sram_ch = dev->channels[channel_select].sram_channels;

	INIT_WORK(&dev->_irq_work_entry, cx25821_vidups_handler);
	dev->_irq_queues = create_singlethread_workqueue("cx25821_workqueue");

	if (!dev->_irq_queues) {
		pr_err("create_singlethread_workqueue() for Video FAILED!\n");
		return -ENOMEM;
	}
	/* 656/VIP SRC Upstream Channel I & J and 7 - Host Bus Interface for
	 * channel A-C
	 */
	tmp = cx_read(VID_CH_MODE_SEL);
	cx_write(VID_CH_MODE_SEL, tmp | 0x1B0001FF);

	dev->_is_running = 0;
	dev->_frame_count = 0;
	dev->_file_status = RESET_STATUS;
	dev->_lines_count = dev->_isNTSC ? 480 : 576;
	dev->_pixel_format = pixel_format;
	dev->_line_size = (dev->_pixel_format == PIXEL_FRMT_422) ?
		(WIDTH_D1 * 2) : (WIDTH_D1 * 3) / 2;
	data_frame_size = dev->_isNTSC ? NTSC_DATA_BUF_SZ : PAL_DATA_BUF_SZ;
	risc_buffer_size = dev->_isNTSC ?
		NTSC_RISC_BUF_SIZE : PAL_RISC_BUF_SIZE;

	if (dev->input_filename) {
		str_length = strlen(dev->input_filename);
		dev->_filename = kmemdup(dev->input_filename, str_length + 1,
					 GFP_KERNEL);

		if (!dev->_filename)
			goto error;
	} else {
		str_length = strlen(dev->_defaultname);
		dev->_filename = kmemdup(dev->_defaultname, str_length + 1,
					 GFP_KERNEL);

		if (!dev->_filename)
			goto error;
	}

	/* Default if filename is empty string */
	if (strcmp(dev->_filename, "") == 0) {
		if (dev->_isNTSC) {
			dev->_filename =
				(dev->_pixel_format == PIXEL_FRMT_411) ?
				"/root/vid411.yuv" : "/root/vidtest.yuv";
		} else {
			dev->_filename =
				(dev->_pixel_format == PIXEL_FRMT_411) ?
				"/root/pal411.yuv" : "/root/pal422.yuv";
		}
	}

	dev->_is_running = 0;
	dev->_frame_count = 0;
	dev->_file_status = RESET_STATUS;
	dev->_lines_count = dev->_isNTSC ? 480 : 576;
	dev->_pixel_format = pixel_format;
	dev->_line_size = (dev->_pixel_format == PIXEL_FRMT_422) ?
		(WIDTH_D1 * 2) : (WIDTH_D1 * 3) / 2;

	retval = cx25821_sram_channel_setup_upstream(dev, sram_ch,
			dev->_line_size, 0);

	/* setup fifo + format */
	cx25821_set_pixelengine(dev, sram_ch, dev->_pixel_format);

	dev->upstream_riscbuf_size = risc_buffer_size * 2;
	dev->upstream_databuf_size = data_frame_size * 2;

	/* Allocating buffers and prepare RISC program */
	retval = cx25821_upstream_buffer_prepare(dev, sram_ch, dev->_line_size);
	if (retval < 0) {
		pr_err("%s: Failed to set up Video upstream buffers!\n",
		       dev->name);
		goto error;
	}

	cx25821_start_video_dma_upstream(dev, sram_ch);

	return 0;

error:
	cx25821_dev_unregister(dev);

	return err;
}
