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

#include "cx25821-video.h"
#include "cx25821-audio-upstream.h"

#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/syscalls.h>
#include <linux/file.h>
#include <linux/fcntl.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

MODULE_DESCRIPTION("v4l2 driver module for cx25821 based TV cards");
MODULE_AUTHOR("Hiep Huynh <hiep.huynh@conexant.com>");
MODULE_LICENSE("GPL");

static int _intr_msk =
    FLD_AUD_SRC_RISCI1 | FLD_AUD_SRC_OF | FLD_AUD_SRC_SYNC |
    FLD_AUD_SRC_OPC_ERR;

int cx25821_sram_channel_setup_upstream_audio(struct cx25821_dev *dev,
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

	if (lines > 3)
		lines = 3;

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
	cx_write(ch->cmds_start + 12, AUDIO_CDT_SIZE_QW);
	cx_write(ch->cmds_start + 16, ch->ctrl_start);

	/* IQ size */
	cx_write(ch->cmds_start + 20, AUDIO_IQ_SIZE_DW);

	for (i = 24; i < 80; i += 4)
		cx_write(ch->cmds_start + i, 0);

	/* fill registers */
	cx_write(ch->ptr1_reg, ch->fifo_start);
	cx_write(ch->ptr2_reg, cdt);
	cx_write(ch->cnt2_reg, AUDIO_CDT_SIZE_QW);
	cx_write(ch->cnt1_reg, AUDIO_CLUSTER_SIZE_QW - 1);

	return 0;
}

static __le32 *cx25821_risc_field_upstream_audio(struct cx25821_dev *dev,
						 __le32 *rp,
						 dma_addr_t databuf_phys_addr,
						 unsigned int bpl,
						 int fifo_enable)
{
	unsigned int line;
	struct sram_channel *sram_ch =
	   dev->channels[dev->_audio_upstream_channel_select].sram_channels;
	int offset = 0;

	/* scan lines */
	for (line = 0; line < LINES_PER_AUDIO_BUFFER; line++) {
		*(rp++) = cpu_to_le32(RISC_READ | RISC_SOL | RISC_EOL | bpl);
		*(rp++) = cpu_to_le32(databuf_phys_addr + offset);
		*(rp++) = cpu_to_le32(0);	/* bits 63-32 */

		/* Check if we need to enable the FIFO
		 * after the first 3 lines.
		 * For the upstream audio channel,
		 * the risc engine will enable the FIFO */
		if (fifo_enable && line == 2) {
			*(rp++) = RISC_WRITECR;
			*(rp++) = sram_ch->dma_ctl;
			*(rp++) = sram_ch->fld_aud_fifo_en;
			*(rp++) = 0x00000020;
		}

		offset += AUDIO_LINE_SIZE;
	}

	return rp;
}

int cx25821_risc_buffer_upstream_audio(struct cx25821_dev *dev,
				       struct pci_dev *pci,
				       unsigned int bpl, unsigned int lines)
{
	__le32 *rp;
	int fifo_enable = 0;
	int frame = 0, i = 0;
	int frame_size = AUDIO_DATA_BUF_SZ;
	int databuf_offset = 0;
	int risc_flag = RISC_CNT_INC;
	dma_addr_t risc_phys_jump_addr;

	/* Virtual address of Risc buffer program */
	rp = dev->_risc_virt_addr;

	/* sync instruction */
	*(rp++) = cpu_to_le32(RISC_RESYNC | AUDIO_SYNC_LINE);

	for (frame = 0; frame < NUM_AUDIO_FRAMES; frame++) {
		databuf_offset = frame_size * frame;

		if (frame == 0) {
			fifo_enable = 1;
			risc_flag = RISC_CNT_RESET;
		} else {
			fifo_enable = 0;
			risc_flag = RISC_CNT_INC;
		}

		/* Calculate physical jump address */
		if ((frame + 1) == NUM_AUDIO_FRAMES) {
			risc_phys_jump_addr =
			    dev->_risc_phys_start_addr +
			    RISC_SYNC_INSTRUCTION_SIZE;
		} else {
			risc_phys_jump_addr =
			    dev->_risc_phys_start_addr +
			    RISC_SYNC_INSTRUCTION_SIZE +
			    AUDIO_RISC_DMA_BUF_SIZE * (frame + 1);
		}

		rp = cx25821_risc_field_upstream_audio(dev, rp,
						       dev->
						       _audiodata_buf_phys_addr
						       + databuf_offset, bpl,
						       fifo_enable);

		if (USE_RISC_NOOP_AUDIO) {
			for (i = 0; i < NUM_NO_OPS; i++)
				*(rp++) = cpu_to_le32(RISC_NOOP);
		}

		/* Loop to (Nth)FrameRISC or to Start of Risc program &
		 * generate IRQ */
		*(rp++) = cpu_to_le32(RISC_JUMP | RISC_IRQ1 | risc_flag);
		*(rp++) = cpu_to_le32(risc_phys_jump_addr);
		*(rp++) = cpu_to_le32(0);

		/* Recalculate virtual address based on frame index */
		rp = dev->_risc_virt_addr + RISC_SYNC_INSTRUCTION_SIZE / 4 +
		    (AUDIO_RISC_DMA_BUF_SIZE * (frame + 1) / 4);
	}

	return 0;
}

void cx25821_free_memory_audio(struct cx25821_dev *dev)
{
	if (dev->_risc_virt_addr) {
		pci_free_consistent(dev->pci, dev->_audiorisc_size,
				    dev->_risc_virt_addr, dev->_risc_phys_addr);
		dev->_risc_virt_addr = NULL;
	}

	if (dev->_audiodata_buf_virt_addr) {
		pci_free_consistent(dev->pci, dev->_audiodata_buf_size,
				    dev->_audiodata_buf_virt_addr,
				    dev->_audiodata_buf_phys_addr);
		dev->_audiodata_buf_virt_addr = NULL;
	}
}

void cx25821_stop_upstream_audio(struct cx25821_dev *dev)
{
	struct sram_channel *sram_ch =
	   dev->channels[AUDIO_UPSTREAM_SRAM_CHANNEL_B].sram_channels;
	u32 tmp = 0;

	if (!dev->_audio_is_running) {
		printk(KERN_DEBUG
		    "cx25821: No audio file is currently running so return!\n");
		return;
	}
	/* Disable RISC interrupts */
	cx_write(sram_ch->int_msk, 0);

	/* Turn OFF risc and fifo enable in AUD_DMA_CNTRL */
	tmp = cx_read(sram_ch->dma_ctl);
	cx_write(sram_ch->dma_ctl,
		 tmp & ~(sram_ch->fld_aud_fifo_en | sram_ch->fld_aud_risc_en));

	/* Clear data buffer memory */
	if (dev->_audiodata_buf_virt_addr)
		memset(dev->_audiodata_buf_virt_addr, 0,
		       dev->_audiodata_buf_size);

	dev->_audio_is_running = 0;
	dev->_is_first_audio_frame = 0;
	dev->_audioframe_count = 0;
	dev->_audiofile_status = END_OF_FILE;

	if (dev->_irq_audio_queues) {
		kfree(dev->_irq_audio_queues);
		dev->_irq_audio_queues = NULL;
	}

	if (dev->_audiofilename != NULL)
		kfree(dev->_audiofilename);
}

void cx25821_free_mem_upstream_audio(struct cx25821_dev *dev)
{
	if (dev->_audio_is_running)
		cx25821_stop_upstream_audio(dev);

	cx25821_free_memory_audio(dev);
}

int cx25821_get_audio_data(struct cx25821_dev *dev,
			   struct sram_channel *sram_ch)
{
	struct file *myfile;
	int frame_index_temp = dev->_audioframe_index;
	int i = 0;
	int line_size = AUDIO_LINE_SIZE;
	int frame_size = AUDIO_DATA_BUF_SZ;
	int frame_offset = frame_size * frame_index_temp;
	ssize_t vfs_read_retval = 0;
	char mybuf[line_size];
	loff_t file_offset = dev->_audioframe_count * frame_size;
	loff_t pos;
	mm_segment_t old_fs;

	if (dev->_audiofile_status == END_OF_FILE)
		return 0;

	myfile = filp_open(dev->_audiofilename, O_RDONLY | O_LARGEFILE, 0);

	if (IS_ERR(myfile)) {
		const int open_errno = -PTR_ERR(myfile);
		printk(KERN_ERR "%s(): ERROR opening file(%s) with errno = %d!\n",
		       __func__, dev->_audiofilename, open_errno);
		return PTR_ERR(myfile);
	} else {
		if (!(myfile->f_op)) {
			printk(KERN_ERR "%s: File has no file operations registered!\n",
			       __func__);
			filp_close(myfile, NULL);
			return -EIO;
		}

		if (!myfile->f_op->read) {
			printk(KERN_ERR "%s: File has no READ operations registered!\n",
			       __func__);
			filp_close(myfile, NULL);
			return -EIO;
		}

		pos = myfile->f_pos;
		old_fs = get_fs();
		set_fs(KERNEL_DS);

		for (i = 0; i < dev->_audio_lines_count; i++) {
			pos = file_offset;

			vfs_read_retval =
			    vfs_read(myfile, mybuf, line_size, &pos);

			if (vfs_read_retval > 0 && vfs_read_retval == line_size
			    && dev->_audiodata_buf_virt_addr != NULL) {
				memcpy((void *)(dev->_audiodata_buf_virt_addr +
						frame_offset / 4), mybuf,
				       vfs_read_retval);
			}

			file_offset += vfs_read_retval;
			frame_offset += vfs_read_retval;

			if (vfs_read_retval < line_size) {
				printk(KERN_INFO
				       "Done: exit %s() since no more bytes to read from Audio file.\n",
				       __func__);
				break;
			}
		}

		if (i > 0)
			dev->_audioframe_count++;

		dev->_audiofile_status =
		    (vfs_read_retval == line_size) ? IN_PROGRESS : END_OF_FILE;

		set_fs(old_fs);
		filp_close(myfile, NULL);
	}

	return 0;
}

static void cx25821_audioups_handler(struct work_struct *work)
{
	struct cx25821_dev *dev =
	    container_of(work, struct cx25821_dev, _audio_work_entry);

	if (!dev) {
		printk(KERN_ERR "ERROR %s(): since container_of(work_struct) FAILED!\n",
		       __func__);
		return;
	}

	cx25821_get_audio_data(dev,
			      dev->channels[dev->
				       _audio_upstream_channel_select].
				       sram_channels);
}

int cx25821_openfile_audio(struct cx25821_dev *dev,
			   struct sram_channel *sram_ch)
{
	struct file *myfile;
	int i = 0, j = 0;
	int line_size = AUDIO_LINE_SIZE;
	ssize_t vfs_read_retval = 0;
	char mybuf[line_size];
	loff_t pos;
	loff_t offset = (unsigned long)0;
	mm_segment_t old_fs;

	myfile = filp_open(dev->_audiofilename, O_RDONLY | O_LARGEFILE, 0);

	if (IS_ERR(myfile)) {
		const int open_errno = -PTR_ERR(myfile);
		printk(KERN_ERR "%s(): ERROR opening file(%s) with errno = %d!\n",
		       __func__, dev->_audiofilename, open_errno);
		return PTR_ERR(myfile);
	} else {
		if (!(myfile->f_op)) {
			printk(KERN_ERR "%s: File has no file operations registered!\n",
			       __func__);
			filp_close(myfile, NULL);
			return -EIO;
		}

		if (!myfile->f_op->read) {
			printk(KERN_ERR "%s: File has no READ operations registered!\n",
			       __func__);
			filp_close(myfile, NULL);
			return -EIO;
		}

		pos = myfile->f_pos;
		old_fs = get_fs();
		set_fs(KERNEL_DS);

		for (j = 0; j < NUM_AUDIO_FRAMES; j++) {
			for (i = 0; i < dev->_audio_lines_count; i++) {
				pos = offset;

				vfs_read_retval =
				    vfs_read(myfile, mybuf, line_size, &pos);

				if (vfs_read_retval > 0
				    && vfs_read_retval == line_size
				    && dev->_audiodata_buf_virt_addr != NULL) {
					memcpy((void *)(dev->
							_audiodata_buf_virt_addr
							+ offset / 4), mybuf,
					       vfs_read_retval);
				}

				offset += vfs_read_retval;

				if (vfs_read_retval < line_size) {
					printk(KERN_INFO
					       "Done: exit %s() since no more bytes to read from Audio file.\n",
					       __func__);
					break;
				}
			}

			if (i > 0)
				dev->_audioframe_count++;

			if (vfs_read_retval < line_size)
				break;
		}

		dev->_audiofile_status =
		    (vfs_read_retval == line_size) ? IN_PROGRESS : END_OF_FILE;

		set_fs(old_fs);
		myfile->f_pos = 0;
		filp_close(myfile, NULL);
	}

	return 0;
}

static int cx25821_audio_upstream_buffer_prepare(struct cx25821_dev *dev,
						 struct sram_channel *sram_ch,
						 int bpl)
{
	int ret = 0;
	dma_addr_t dma_addr;
	dma_addr_t data_dma_addr;

	cx25821_free_memory_audio(dev);

	dev->_risc_virt_addr =
	    pci_alloc_consistent(dev->pci, dev->audio_upstream_riscbuf_size,
				 &dma_addr);
	dev->_risc_virt_start_addr = dev->_risc_virt_addr;
	dev->_risc_phys_start_addr = dma_addr;
	dev->_risc_phys_addr = dma_addr;
	dev->_audiorisc_size = dev->audio_upstream_riscbuf_size;

	if (!dev->_risc_virt_addr) {
		printk(KERN_DEBUG
			"cx25821 ERROR: pci_alloc_consistent() FAILED to allocate memory for RISC program! Returning.\n");
		return -ENOMEM;
	}
	/* Clear out memory at address */
	memset(dev->_risc_virt_addr, 0, dev->_audiorisc_size);

	/* For Audio Data buffer allocation */
	dev->_audiodata_buf_virt_addr =
	    pci_alloc_consistent(dev->pci, dev->audio_upstream_databuf_size,
				 &data_dma_addr);
	dev->_audiodata_buf_phys_addr = data_dma_addr;
	dev->_audiodata_buf_size = dev->audio_upstream_databuf_size;

	if (!dev->_audiodata_buf_virt_addr) {
		printk(KERN_DEBUG
			"cx25821 ERROR: pci_alloc_consistent() FAILED to allocate memory for data buffer! Returning.\n");
		return -ENOMEM;
	}
	/* Clear out memory at address */
	memset(dev->_audiodata_buf_virt_addr, 0, dev->_audiodata_buf_size);

	ret = cx25821_openfile_audio(dev, sram_ch);
	if (ret < 0)
		return ret;

	/* Creating RISC programs */
	ret =
	    cx25821_risc_buffer_upstream_audio(dev, dev->pci, bpl,
					       dev->_audio_lines_count);
	if (ret < 0) {
		printk(KERN_DEBUG
		      "cx25821 ERROR creating audio upstream RISC programs!\n");
		goto error;
	}

	return 0;

error:
	return ret;
}

int cx25821_audio_upstream_irq(struct cx25821_dev *dev, int chan_num,
			       u32 status)
{
	int i = 0;
	u32 int_msk_tmp;
       struct sram_channel *channel = dev->channels[chan_num].sram_channels;
	dma_addr_t risc_phys_jump_addr;
	__le32 *rp;

	if (status & FLD_AUD_SRC_RISCI1) {
		/* Get interrupt_index of the program that interrupted */
		u32 prog_cnt = cx_read(channel->gpcnt);

		/* Since we've identified our IRQ, clear our bits from the
		 * interrupt mask and interrupt status registers */
		cx_write(channel->int_msk, 0);
		cx_write(channel->int_stat, cx_read(channel->int_stat));

		spin_lock(&dev->slock);

		while (prog_cnt != dev->_last_index_irq) {
			/* Update _last_index_irq */
			if (dev->_last_index_irq < (NUMBER_OF_PROGRAMS - 1))
				dev->_last_index_irq++;
			else
				dev->_last_index_irq = 0;

			dev->_audioframe_index = dev->_last_index_irq;

			queue_work(dev->_irq_audio_queues,
				   &dev->_audio_work_entry);
		}

		if (dev->_is_first_audio_frame) {
			dev->_is_first_audio_frame = 0;

			if (dev->_risc_virt_start_addr != NULL) {
				risc_phys_jump_addr =
				    dev->_risc_phys_start_addr +
				    RISC_SYNC_INSTRUCTION_SIZE +
				    AUDIO_RISC_DMA_BUF_SIZE;

				rp = cx25821_risc_field_upstream_audio(dev,
								       dev->
								       _risc_virt_start_addr
								       + 1,
								       dev->
								       _audiodata_buf_phys_addr,
								       AUDIO_LINE_SIZE,
								       FIFO_DISABLE);

				if (USE_RISC_NOOP_AUDIO) {
					for (i = 0; i < NUM_NO_OPS; i++) {
						*(rp++) =
						    cpu_to_le32(RISC_NOOP);
					}
				}
				/* Jump to 2nd Audio Frame */
				*(rp++) =
				    cpu_to_le32(RISC_JUMP | RISC_IRQ1 |
						RISC_CNT_RESET);
				*(rp++) = cpu_to_le32(risc_phys_jump_addr);
				*(rp++) = cpu_to_le32(0);
			}
		}

		spin_unlock(&dev->slock);
	} else {
		if (status & FLD_AUD_SRC_OF)
			printk(KERN_WARNING "%s: Audio Received Overflow Error Interrupt!\n",
			       __func__);

		if (status & FLD_AUD_SRC_SYNC)
			printk(KERN_WARNING "%s: Audio Received Sync Error Interrupt!\n",
			       __func__);

		if (status & FLD_AUD_SRC_OPC_ERR)
			printk(KERN_WARNING "%s: Audio Received OpCode Error Interrupt!\n",
			       __func__);

		/* Read and write back the interrupt status register to clear
		 * our bits */
		cx_write(channel->int_stat, cx_read(channel->int_stat));
	}

	if (dev->_audiofile_status == END_OF_FILE) {
		printk(KERN_WARNING "cx25821: EOF Channel Audio Framecount = %d\n",
		       dev->_audioframe_count);
		return -1;
	}
	/* ElSE, set the interrupt mask register, re-enable irq. */
	int_msk_tmp = cx_read(channel->int_msk);
	cx_write(channel->int_msk, int_msk_tmp |= _intr_msk);

	return 0;
}

static irqreturn_t cx25821_upstream_irq_audio(int irq, void *dev_id)
{
	struct cx25821_dev *dev = dev_id;
	u32 msk_stat, audio_status;
	int handled = 0;
	struct sram_channel *sram_ch;

	if (!dev)
		return -1;

       sram_ch = dev->channels[dev->_audio_upstream_channel_select].
				       sram_channels;

	msk_stat = cx_read(sram_ch->int_mstat);
	audio_status = cx_read(sram_ch->int_stat);

	/* Only deal with our interrupt */
	if (audio_status) {
		handled =
		    cx25821_audio_upstream_irq(dev,
					       dev->
					       _audio_upstream_channel_select,
					       audio_status);
	}

	if (handled < 0)
		cx25821_stop_upstream_audio(dev);
	else
		handled += handled;

	return IRQ_RETVAL(handled);
}

static void cx25821_wait_fifo_enable(struct cx25821_dev *dev,
				     struct sram_channel *sram_ch)
{
	int count = 0;
	u32 tmp;

	do {
		/* Wait 10 microsecond before checking to see if the FIFO is
		 * turned ON. */
		udelay(10);

		tmp = cx_read(sram_ch->dma_ctl);

		/* 10 millisecond timeout */
		if (count++ > 1000) {
			printk(KERN_ERR
			       "cx25821 ERROR: %s() fifo is NOT turned on. Timeout!\n",
			     __func__);
			return;
		}

	} while (!(tmp & sram_ch->fld_aud_fifo_en));

}

int cx25821_start_audio_dma_upstream(struct cx25821_dev *dev,
				     struct sram_channel *sram_ch)
{
	u32 tmp = 0;
	int err = 0;

	/* Set the physical start address of the RISC program in the initial
	 * program counter(IPC) member of the CMDS. */
	cx_write(sram_ch->cmds_start + 0, dev->_risc_phys_addr);
	/* Risc IPC High 64 bits 63-32 */
	cx_write(sram_ch->cmds_start + 4, 0);

	/* reset counter */
	cx_write(sram_ch->gpcnt_ctl, 3);

	/* Set the line length       (It looks like we do not need to set the
	 * line length) */
	cx_write(sram_ch->aud_length, AUDIO_LINE_SIZE & FLD_AUD_DST_LN_LNGTH);

	/* Set the input mode to 16-bit */
	tmp = cx_read(sram_ch->aud_cfg);
	tmp |=
	    FLD_AUD_SRC_ENABLE | FLD_AUD_DST_PK_MODE | FLD_AUD_CLK_ENABLE |
	    FLD_AUD_MASTER_MODE | FLD_AUD_CLK_SELECT_PLL_D | FLD_AUD_SONY_MODE;
	cx_write(sram_ch->aud_cfg, tmp);

	/* Read and write back the interrupt status register to clear it */
	tmp = cx_read(sram_ch->int_stat);
	cx_write(sram_ch->int_stat, tmp);

	/* Clear our bits from the interrupt status register. */
	cx_write(sram_ch->int_stat, _intr_msk);

	/* Set the interrupt mask register, enable irq. */
	cx_set(PCI_INT_MSK, cx_read(PCI_INT_MSK) | (1 << sram_ch->irq_bit));
	tmp = cx_read(sram_ch->int_msk);
	cx_write(sram_ch->int_msk, tmp |= _intr_msk);

	err =
	    request_irq(dev->pci->irq, cx25821_upstream_irq_audio,
			IRQF_SHARED | IRQF_DISABLED, dev->name, dev);
	if (err < 0) {
		printk(KERN_ERR "%s: can't get upstream IRQ %d\n", dev->name,
		       dev->pci->irq);
		goto fail_irq;
	}

	/* Start the DMA  engine */
	tmp = cx_read(sram_ch->dma_ctl);
	cx_set(sram_ch->dma_ctl, tmp | sram_ch->fld_aud_risc_en);

	dev->_audio_is_running = 1;
	dev->_is_first_audio_frame = 1;

	/* The fifo_en bit turns on by the first Risc program */
	cx25821_wait_fifo_enable(dev, sram_ch);

	return 0;

fail_irq:
	cx25821_dev_unregister(dev);
	return err;
}

int cx25821_audio_upstream_init(struct cx25821_dev *dev, int channel_select)
{
	struct sram_channel *sram_ch;
	int retval = 0;
	int err = 0;
	int str_length = 0;

	if (dev->_audio_is_running) {
		printk(KERN_WARNING "Audio Channel is still running so return!\n");
		return 0;
	}

	dev->_audio_upstream_channel_select = channel_select;
       sram_ch = dev->channels[channel_select].sram_channels;

	/* Work queue */
	INIT_WORK(&dev->_audio_work_entry, cx25821_audioups_handler);
	dev->_irq_audio_queues =
	    create_singlethread_workqueue("cx25821_audioworkqueue");

	if (!dev->_irq_audio_queues) {
		printk(KERN_DEBUG
			"cx25821 ERROR: create_singlethread_workqueue() for Audio FAILED!\n");
		return -ENOMEM;
	}

	dev->_last_index_irq = 0;
	dev->_audio_is_running = 0;
	dev->_audioframe_count = 0;
	dev->_audiofile_status = RESET_STATUS;
	dev->_audio_lines_count = LINES_PER_AUDIO_BUFFER;
	_line_size = AUDIO_LINE_SIZE;

	if (dev->input_audiofilename) {
		str_length = strlen(dev->input_audiofilename);
		dev->_audiofilename = kmalloc(str_length + 1, GFP_KERNEL);

		if (!dev->_audiofilename)
			goto error;

		memcpy(dev->_audiofilename, dev->input_audiofilename,
		       str_length + 1);

		/* Default if filename is empty string */
		if (strcmp(dev->input_audiofilename, "") == 0) {
			dev->_audiofilename = "/root/audioGOOD.wav";
		}
	} else {
		str_length = strlen(_defaultAudioName);
		dev->_audiofilename = kmalloc(str_length + 1, GFP_KERNEL);

		if (!dev->_audiofilename)
			goto error;

		memcpy(dev->_audiofilename, _defaultAudioName, str_length + 1);
	}

	retval =
	    cx25821_sram_channel_setup_upstream_audio(dev, sram_ch, _line_size,
						      0);

	dev->audio_upstream_riscbuf_size =
	    AUDIO_RISC_DMA_BUF_SIZE * NUM_AUDIO_PROGS +
	    RISC_SYNC_INSTRUCTION_SIZE;
	dev->audio_upstream_databuf_size = AUDIO_DATA_BUF_SZ * NUM_AUDIO_PROGS;

	/* Allocating buffers and prepare RISC program */
	retval =
	    cx25821_audio_upstream_buffer_prepare(dev, sram_ch, _line_size);
	if (retval < 0) {
		printk(KERN_ERR
		       "%s: Failed to set up Audio upstream buffers!\n",
		       dev->name);
		goto error;
	}
	/* Start RISC engine */
	cx25821_start_audio_dma_upstream(dev, sram_ch);

	return 0;

error:
	cx25821_dev_unregister(dev);

	return err;
}
