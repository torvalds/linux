/* interrupt handling
    Copyright (C) 2003-2004  Kevin Thayer <nufan_wfk at yahoo.com>
    Copyright (C) 2004  Chris Kennedy <c@groovy.org>
    Copyright (C) 2005-2007  Hans Verkuil <hverkuil@xs4all.nl>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "ivtv-driver.h"
#include "ivtv-queue.h"
#include "ivtv-udma.h"
#include "ivtv-irq.h"
#include "ivtv-mailbox.h"
#include "ivtv-vbi.h"
#include "ivtv-yuv.h"
#include <media/v4l2-event.h>

#define DMA_MAGIC_COOKIE 0x000001fe

static void ivtv_dma_dec_start(struct ivtv_stream *s);

static const int ivtv_stream_map[] = {
	IVTV_ENC_STREAM_TYPE_MPG,
	IVTV_ENC_STREAM_TYPE_YUV,
	IVTV_ENC_STREAM_TYPE_PCM,
	IVTV_ENC_STREAM_TYPE_VBI,
};

static void ivtv_pcm_work_handler(struct ivtv *itv)
{
	struct ivtv_stream *s = &itv->streams[IVTV_ENC_STREAM_TYPE_PCM];
	struct ivtv_buffer *buf;

	/* Pass the PCM data to ivtv-alsa */

	while (1) {
		/*
		 * Users should not be using both the ALSA and V4L2 PCM audio
		 * capture interfaces at the same time.  If the user is doing
		 * this, there maybe a buffer in q_io to grab, use, and put
		 * back in rotation.
		 */
		buf = ivtv_dequeue(s, &s->q_io);
		if (buf == NULL)
			buf = ivtv_dequeue(s, &s->q_full);
		if (buf == NULL)
			break;

		if (buf->readpos < buf->bytesused)
			itv->pcm_announce_callback(itv->alsa,
				(u8 *)(buf->buf + buf->readpos),
				(size_t)(buf->bytesused - buf->readpos));

		ivtv_enqueue(s, buf, &s->q_free);
	}
}

static void ivtv_pio_work_handler(struct ivtv *itv)
{
	struct ivtv_stream *s = &itv->streams[itv->cur_pio_stream];
	struct ivtv_buffer *buf;
	int i = 0;

	IVTV_DEBUG_HI_DMA("ivtv_pio_work_handler\n");
	if (itv->cur_pio_stream < 0 || itv->cur_pio_stream >= IVTV_MAX_STREAMS ||
			s->vdev == NULL || !ivtv_use_pio(s)) {
		itv->cur_pio_stream = -1;
		/* trigger PIO complete user interrupt */
		write_reg(IVTV_IRQ_ENC_PIO_COMPLETE, 0x44);
		return;
	}
	IVTV_DEBUG_HI_DMA("Process PIO %s\n", s->name);
	list_for_each_entry(buf, &s->q_dma.list, list) {
		u32 size = s->sg_processing[i].size & 0x3ffff;

		/* Copy the data from the card to the buffer */
		if (s->type == IVTV_DEC_STREAM_TYPE_VBI) {
			memcpy_fromio(buf->buf, itv->dec_mem + s->sg_processing[i].src - IVTV_DECODER_OFFSET, size);
		}
		else {
			memcpy_fromio(buf->buf, itv->enc_mem + s->sg_processing[i].src, size);
		}
		i++;
		if (i == s->sg_processing_size)
			break;
	}
	write_reg(IVTV_IRQ_ENC_PIO_COMPLETE, 0x44);
}

void ivtv_irq_work_handler(struct kthread_work *work)
{
	struct ivtv *itv = container_of(work, struct ivtv, irq_work);

	if (test_and_clear_bit(IVTV_F_I_WORK_HANDLER_PIO, &itv->i_flags))
		ivtv_pio_work_handler(itv);

	if (test_and_clear_bit(IVTV_F_I_WORK_HANDLER_VBI, &itv->i_flags))
		ivtv_vbi_work_handler(itv);

	if (test_and_clear_bit(IVTV_F_I_WORK_HANDLER_YUV, &itv->i_flags))
		ivtv_yuv_work_handler(itv);

	if (test_and_clear_bit(IVTV_F_I_WORK_HANDLER_PCM, &itv->i_flags))
		ivtv_pcm_work_handler(itv);
}

/* Determine the required DMA size, setup enough buffers in the predma queue and
   actually copy the data from the card to the buffers in case a PIO transfer is
   required for this stream.
 */
static int stream_enc_dma_append(struct ivtv_stream *s, u32 data[CX2341X_MBOX_MAX_DATA])
{
	struct ivtv *itv = s->itv;
	struct ivtv_buffer *buf;
	u32 bytes_needed = 0;
	u32 offset, size;
	u32 UVoffset = 0, UVsize = 0;
	int skip_bufs = s->q_predma.buffers;
	int idx = s->sg_pending_size;
	int rc;

	/* sanity checks */
	if (s->vdev == NULL) {
		IVTV_DEBUG_WARN("Stream %s not started\n", s->name);
		return -1;
	}
	if (!test_bit(IVTV_F_S_CLAIMED, &s->s_flags)) {
		IVTV_DEBUG_WARN("Stream %s not open\n", s->name);
		return -1;
	}

	/* determine offset, size and PTS for the various streams */
	switch (s->type) {
		case IVTV_ENC_STREAM_TYPE_MPG:
			offset = data[1];
			size = data[2];
			s->pending_pts = 0;
			break;

		case IVTV_ENC_STREAM_TYPE_YUV:
			offset = data[1];
			size = data[2];
			UVoffset = data[3];
			UVsize = data[4];
			s->pending_pts = ((u64) data[5] << 32) | data[6];
			break;

		case IVTV_ENC_STREAM_TYPE_PCM:
			offset = data[1] + 12;
			size = data[2] - 12;
			s->pending_pts = read_dec(offset - 8) |
				((u64)(read_dec(offset - 12)) << 32);
			if (itv->has_cx23415)
				offset += IVTV_DECODER_OFFSET;
			break;

		case IVTV_ENC_STREAM_TYPE_VBI:
			size = itv->vbi.enc_size * itv->vbi.fpi;
			offset = read_enc(itv->vbi.enc_start - 4) + 12;
			if (offset == 12) {
				IVTV_DEBUG_INFO("VBI offset == 0\n");
				return -1;
			}
			s->pending_pts = read_enc(offset - 4) | ((u64)read_enc(offset - 8) << 32);
			break;

		case IVTV_DEC_STREAM_TYPE_VBI:
			size = read_dec(itv->vbi.dec_start + 4) + 8;
			offset = read_dec(itv->vbi.dec_start) + itv->vbi.dec_start;
			s->pending_pts = 0;
			offset += IVTV_DECODER_OFFSET;
			break;
		default:
			/* shouldn't happen */
			return -1;
	}

	/* if this is the start of the DMA then fill in the magic cookie */
	if (s->sg_pending_size == 0 && ivtv_use_dma(s)) {
		if (itv->has_cx23415 && (s->type == IVTV_ENC_STREAM_TYPE_PCM ||
		    s->type == IVTV_DEC_STREAM_TYPE_VBI)) {
			s->pending_backup = read_dec(offset - IVTV_DECODER_OFFSET);
			write_dec_sync(DMA_MAGIC_COOKIE, offset - IVTV_DECODER_OFFSET);
		}
		else {
			s->pending_backup = read_enc(offset);
			write_enc_sync(DMA_MAGIC_COOKIE, offset);
		}
		s->pending_offset = offset;
	}

	bytes_needed = size;
	if (s->type == IVTV_ENC_STREAM_TYPE_YUV) {
		/* The size for the Y samples needs to be rounded upwards to a
		   multiple of the buf_size. The UV samples then start in the
		   next buffer. */
		bytes_needed = s->buf_size * ((bytes_needed + s->buf_size - 1) / s->buf_size);
		bytes_needed += UVsize;
	}

	IVTV_DEBUG_HI_DMA("%s %s: 0x%08x bytes at 0x%08x\n",
		ivtv_use_pio(s) ? "PIO" : "DMA", s->name, bytes_needed, offset);

	rc = ivtv_queue_move(s, &s->q_free, &s->q_full, &s->q_predma, bytes_needed);
	if (rc < 0) { /* Insufficient buffers */
		IVTV_DEBUG_WARN("Cannot obtain %d bytes for %s data transfer\n",
				bytes_needed, s->name);
		return -1;
	}
	if (rc && !s->buffers_stolen && test_bit(IVTV_F_S_APPL_IO, &s->s_flags)) {
		IVTV_WARN("All %s stream buffers are full. Dropping data.\n", s->name);
		IVTV_WARN("Cause: the application is not reading fast enough.\n");
	}
	s->buffers_stolen = rc;

	/* got the buffers, now fill in sg_pending */
	buf = list_entry(s->q_predma.list.next, struct ivtv_buffer, list);
	memset(buf->buf, 0, 128);
	list_for_each_entry(buf, &s->q_predma.list, list) {
		if (skip_bufs-- > 0)
			continue;
		s->sg_pending[idx].dst = buf->dma_handle;
		s->sg_pending[idx].src = offset;
		s->sg_pending[idx].size = s->buf_size;
		buf->bytesused = min(size, s->buf_size);
		buf->dma_xfer_cnt = s->dma_xfer_cnt;

		s->q_predma.bytesused += buf->bytesused;
		size -= buf->bytesused;
		offset += s->buf_size;

		/* Sync SG buffers */
		ivtv_buf_sync_for_device(s, buf);

		if (size == 0) {	/* YUV */
			/* process the UV section */
			offset = UVoffset;
			size = UVsize;
		}
		idx++;
	}
	s->sg_pending_size = idx;
	return 0;
}

static void dma_post(struct ivtv_stream *s)
{
	struct ivtv *itv = s->itv;
	struct ivtv_buffer *buf = NULL;
	struct list_head *p;
	u32 offset;
	__le32 *u32buf;
	int x = 0;

	IVTV_DEBUG_HI_DMA("%s %s completed (%x)\n", ivtv_use_pio(s) ? "PIO" : "DMA",
			s->name, s->dma_offset);
	list_for_each(p, &s->q_dma.list) {
		buf = list_entry(p, struct ivtv_buffer, list);
		u32buf = (__le32 *)buf->buf;

		/* Sync Buffer */
		ivtv_buf_sync_for_cpu(s, buf);

		if (x == 0 && ivtv_use_dma(s)) {
			offset = s->dma_last_offset;
			if (le32_to_cpu(u32buf[offset / 4]) != DMA_MAGIC_COOKIE)
			{
				for (offset = 0; offset < 64; offset++)
					if (le32_to_cpu(u32buf[offset]) == DMA_MAGIC_COOKIE)
						break;
				offset *= 4;
				if (offset == 256) {
					IVTV_DEBUG_WARN("%s: Couldn't find start of buffer within the first 256 bytes\n", s->name);
					offset = s->dma_last_offset;
				}
				if (s->dma_last_offset != offset)
					IVTV_DEBUG_WARN("%s: offset %d -> %d\n", s->name, s->dma_last_offset, offset);
				s->dma_last_offset = offset;
			}
			if (itv->has_cx23415 && (s->type == IVTV_ENC_STREAM_TYPE_PCM ||
						s->type == IVTV_DEC_STREAM_TYPE_VBI)) {
				write_dec_sync(0, s->dma_offset - IVTV_DECODER_OFFSET);
			}
			else {
				write_enc_sync(0, s->dma_offset);
			}
			if (offset) {
				buf->bytesused -= offset;
				memcpy(buf->buf, buf->buf + offset, buf->bytesused + offset);
			}
			*u32buf = cpu_to_le32(s->dma_backup);
		}
		x++;
		/* flag byteswap ABCD -> DCBA for MPG & VBI data outside irq */
		if (s->type == IVTV_ENC_STREAM_TYPE_MPG ||
		    s->type == IVTV_ENC_STREAM_TYPE_VBI)
			buf->b_flags |= IVTV_F_B_NEED_BUF_SWAP;
	}
	if (buf)
		buf->bytesused += s->dma_last_offset;
	if (buf && s->type == IVTV_DEC_STREAM_TYPE_VBI) {
		list_for_each_entry(buf, &s->q_dma.list, list) {
			/* Parse and Groom VBI Data */
			s->q_dma.bytesused -= buf->bytesused;
			ivtv_process_vbi_data(itv, buf, 0, s->type);
			s->q_dma.bytesused += buf->bytesused;
		}
		if (s->fh == NULL) {
			ivtv_queue_move(s, &s->q_dma, NULL, &s->q_free, 0);
			return;
		}
	}

	ivtv_queue_move(s, &s->q_dma, NULL, &s->q_full, s->q_dma.bytesused);

	if (s->type == IVTV_ENC_STREAM_TYPE_PCM &&
	    itv->pcm_announce_callback != NULL) {
		/*
		 * Set up the work handler to pass the data to ivtv-alsa.
		 *
		 * We just use q_full and let the work handler race with users
		 * making ivtv-fileops.c calls on the PCM device node.
		 *
		 * Users should not be using both the ALSA and V4L2 PCM audio
		 * capture interfaces at the same time.  If the user does this,
		 * fragments of data will just go out each interface as they
		 * race for PCM data.
		 */
		set_bit(IVTV_F_I_WORK_HANDLER_PCM, &itv->i_flags);
		set_bit(IVTV_F_I_HAVE_WORK, &itv->i_flags);
	}

	if (s->fh)
		wake_up(&s->waitq);
}

void ivtv_dma_stream_dec_prepare(struct ivtv_stream *s, u32 offset, int lock)
{
	struct ivtv *itv = s->itv;
	struct yuv_playback_info *yi = &itv->yuv_info;
	u8 frame = yi->draw_frame;
	struct yuv_frame_info *f = &yi->new_frame_info[frame];
	struct ivtv_buffer *buf;
	u32 y_size = 720 * ((f->src_h + 31) & ~31);
	u32 uv_offset = offset + IVTV_YUV_BUFFER_UV_OFFSET;
	int y_done = 0;
	int bytes_written = 0;
	int idx = 0;

	IVTV_DEBUG_HI_DMA("DEC PREPARE DMA %s: %08x %08x\n", s->name, s->q_predma.bytesused, offset);

	/* Insert buffer block for YUV if needed */
	if (s->type == IVTV_DEC_STREAM_TYPE_YUV && f->offset_y) {
		if (yi->blanking_dmaptr) {
			s->sg_pending[idx].src = yi->blanking_dmaptr;
			s->sg_pending[idx].dst = offset;
			s->sg_pending[idx].size = 720 * 16;
		}
		offset += 720 * 16;
		idx++;
	}

	list_for_each_entry(buf, &s->q_predma.list, list) {
		/* YUV UV Offset from Y Buffer */
		if (s->type == IVTV_DEC_STREAM_TYPE_YUV && !y_done &&
				(bytes_written + buf->bytesused) >= y_size) {
			s->sg_pending[idx].src = buf->dma_handle;
			s->sg_pending[idx].dst = offset;
			s->sg_pending[idx].size = y_size - bytes_written;
			offset = uv_offset;
			if (s->sg_pending[idx].size != buf->bytesused) {
				idx++;
				s->sg_pending[idx].src =
				  buf->dma_handle + s->sg_pending[idx - 1].size;
				s->sg_pending[idx].dst = offset;
				s->sg_pending[idx].size =
				   buf->bytesused - s->sg_pending[idx - 1].size;
				offset += s->sg_pending[idx].size;
			}
			y_done = 1;
		} else {
			s->sg_pending[idx].src = buf->dma_handle;
			s->sg_pending[idx].dst = offset;
			s->sg_pending[idx].size = buf->bytesused;
			offset += buf->bytesused;
		}
		bytes_written += buf->bytesused;

		/* Sync SG buffers */
		ivtv_buf_sync_for_device(s, buf);
		idx++;
	}
	s->sg_pending_size = idx;

	/* Sync Hardware SG List of buffers */
	ivtv_stream_sync_for_device(s);
	if (lock) {
		unsigned long flags = 0;

		spin_lock_irqsave(&itv->dma_reg_lock, flags);
		if (!test_bit(IVTV_F_I_DMA, &itv->i_flags))
			ivtv_dma_dec_start(s);
		else
			set_bit(IVTV_F_S_DMA_PENDING, &s->s_flags);
		spin_unlock_irqrestore(&itv->dma_reg_lock, flags);
	} else {
		if (!test_bit(IVTV_F_I_DMA, &itv->i_flags))
			ivtv_dma_dec_start(s);
		else
			set_bit(IVTV_F_S_DMA_PENDING, &s->s_flags);
	}
}

static void ivtv_dma_enc_start_xfer(struct ivtv_stream *s)
{
	struct ivtv *itv = s->itv;

	s->sg_dma->src = cpu_to_le32(s->sg_processing[s->sg_processed].src);
	s->sg_dma->dst = cpu_to_le32(s->sg_processing[s->sg_processed].dst);
	s->sg_dma->size = cpu_to_le32(s->sg_processing[s->sg_processed].size | 0x80000000);
	s->sg_processed++;
	/* Sync Hardware SG List of buffers */
	ivtv_stream_sync_for_device(s);
	write_reg(s->sg_handle, IVTV_REG_ENCDMAADDR);
	write_reg_sync(read_reg(IVTV_REG_DMAXFER) | 0x02, IVTV_REG_DMAXFER);
	itv->dma_timer.expires = jiffies + msecs_to_jiffies(300);
	add_timer(&itv->dma_timer);
}

static void ivtv_dma_dec_start_xfer(struct ivtv_stream *s)
{
	struct ivtv *itv = s->itv;

	s->sg_dma->src = cpu_to_le32(s->sg_processing[s->sg_processed].src);
	s->sg_dma->dst = cpu_to_le32(s->sg_processing[s->sg_processed].dst);
	s->sg_dma->size = cpu_to_le32(s->sg_processing[s->sg_processed].size | 0x80000000);
	s->sg_processed++;
	/* Sync Hardware SG List of buffers */
	ivtv_stream_sync_for_device(s);
	write_reg(s->sg_handle, IVTV_REG_DECDMAADDR);
	write_reg_sync(read_reg(IVTV_REG_DMAXFER) | 0x01, IVTV_REG_DMAXFER);
	itv->dma_timer.expires = jiffies + msecs_to_jiffies(300);
	add_timer(&itv->dma_timer);
}

/* start the encoder DMA */
static void ivtv_dma_enc_start(struct ivtv_stream *s)
{
	struct ivtv *itv = s->itv;
	struct ivtv_stream *s_vbi = &itv->streams[IVTV_ENC_STREAM_TYPE_VBI];
	int i;

	IVTV_DEBUG_HI_DMA("start %s for %s\n", ivtv_use_dma(s) ? "DMA" : "PIO", s->name);

	if (s->q_predma.bytesused)
		ivtv_queue_move(s, &s->q_predma, NULL, &s->q_dma, s->q_predma.bytesused);

	if (ivtv_use_dma(s))
		s->sg_pending[s->sg_pending_size - 1].size += 256;

	/* If this is an MPEG stream, and VBI data is also pending, then append the
	   VBI DMA to the MPEG DMA and transfer both sets of data at once.

	   VBI DMA is a second class citizen compared to MPEG and mixing them together
	   will confuse the firmware (the end of a VBI DMA is seen as the end of a
	   MPEG DMA, thus effectively dropping an MPEG frame). So instead we make
	   sure we only use the MPEG DMA to transfer the VBI DMA if both are in
	   use. This way no conflicts occur. */
	clear_bit(IVTV_F_S_DMA_HAS_VBI, &s->s_flags);
	if (s->type == IVTV_ENC_STREAM_TYPE_MPG && s_vbi->sg_pending_size &&
			s->sg_pending_size + s_vbi->sg_pending_size <= s->buffers) {
		ivtv_queue_move(s_vbi, &s_vbi->q_predma, NULL, &s_vbi->q_dma, s_vbi->q_predma.bytesused);
		if (ivtv_use_dma(s_vbi))
			s_vbi->sg_pending[s_vbi->sg_pending_size - 1].size += 256;
		for (i = 0; i < s_vbi->sg_pending_size; i++) {
			s->sg_pending[s->sg_pending_size++] = s_vbi->sg_pending[i];
		}
		s_vbi->dma_offset = s_vbi->pending_offset;
		s_vbi->sg_pending_size = 0;
		s_vbi->dma_xfer_cnt++;
		set_bit(IVTV_F_S_DMA_HAS_VBI, &s->s_flags);
		IVTV_DEBUG_HI_DMA("include DMA for %s\n", s_vbi->name);
	}

	s->dma_xfer_cnt++;
	memcpy(s->sg_processing, s->sg_pending, sizeof(struct ivtv_sg_host_element) * s->sg_pending_size);
	s->sg_processing_size = s->sg_pending_size;
	s->sg_pending_size = 0;
	s->sg_processed = 0;
	s->dma_offset = s->pending_offset;
	s->dma_backup = s->pending_backup;
	s->dma_pts = s->pending_pts;

	if (ivtv_use_pio(s)) {
		set_bit(IVTV_F_I_WORK_HANDLER_PIO, &itv->i_flags);
		set_bit(IVTV_F_I_HAVE_WORK, &itv->i_flags);
		set_bit(IVTV_F_I_PIO, &itv->i_flags);
		itv->cur_pio_stream = s->type;
	}
	else {
		itv->dma_retries = 0;
		ivtv_dma_enc_start_xfer(s);
		set_bit(IVTV_F_I_DMA, &itv->i_flags);
		itv->cur_dma_stream = s->type;
	}
}

static void ivtv_dma_dec_start(struct ivtv_stream *s)
{
	struct ivtv *itv = s->itv;

	if (s->q_predma.bytesused)
		ivtv_queue_move(s, &s->q_predma, NULL, &s->q_dma, s->q_predma.bytesused);
	s->dma_xfer_cnt++;
	memcpy(s->sg_processing, s->sg_pending, sizeof(struct ivtv_sg_host_element) * s->sg_pending_size);
	s->sg_processing_size = s->sg_pending_size;
	s->sg_pending_size = 0;
	s->sg_processed = 0;

	IVTV_DEBUG_HI_DMA("start DMA for %s\n", s->name);
	itv->dma_retries = 0;
	ivtv_dma_dec_start_xfer(s);
	set_bit(IVTV_F_I_DMA, &itv->i_flags);
	itv->cur_dma_stream = s->type;
}

static void ivtv_irq_dma_read(struct ivtv *itv)
{
	struct ivtv_stream *s = NULL;
	struct ivtv_buffer *buf;
	int hw_stream_type = 0;

	IVTV_DEBUG_HI_IRQ("DEC DMA READ\n");

	del_timer(&itv->dma_timer);

	if (!test_bit(IVTV_F_I_UDMA, &itv->i_flags) && itv->cur_dma_stream < 0)
		return;

	if (!test_bit(IVTV_F_I_UDMA, &itv->i_flags)) {
		s = &itv->streams[itv->cur_dma_stream];
		ivtv_stream_sync_for_cpu(s);

		if (read_reg(IVTV_REG_DMASTATUS) & 0x14) {
			IVTV_DEBUG_WARN("DEC DMA ERROR %x (xfer %d of %d, retry %d)\n",
					read_reg(IVTV_REG_DMASTATUS),
					s->sg_processed, s->sg_processing_size, itv->dma_retries);
			write_reg(read_reg(IVTV_REG_DMASTATUS) & 3, IVTV_REG_DMASTATUS);
			if (itv->dma_retries == 3) {
				/* Too many retries, give up on this frame */
				itv->dma_retries = 0;
				s->sg_processed = s->sg_processing_size;
			}
			else {
				/* Retry, starting with the first xfer segment.
				   Just retrying the current segment is not sufficient. */
				s->sg_processed = 0;
				itv->dma_retries++;
			}
		}
		if (s->sg_processed < s->sg_processing_size) {
			/* DMA next buffer */
			ivtv_dma_dec_start_xfer(s);
			return;
		}
		if (s->type == IVTV_DEC_STREAM_TYPE_YUV)
			hw_stream_type = 2;
		IVTV_DEBUG_HI_DMA("DEC DATA READ %s: %d\n", s->name, s->q_dma.bytesused);

		/* For some reason must kick the firmware, like PIO mode,
		   I think this tells the firmware we are done and the size
		   of the xfer so it can calculate what we need next.
		   I think we can do this part ourselves but would have to
		   fully calculate xfer info ourselves and not use interrupts
		 */
		ivtv_vapi(itv, CX2341X_DEC_SCHED_DMA_FROM_HOST, 3, 0, s->q_dma.bytesused,
				hw_stream_type);

		/* Free last DMA call */
		while ((buf = ivtv_dequeue(s, &s->q_dma)) != NULL) {
			ivtv_buf_sync_for_cpu(s, buf);
			ivtv_enqueue(s, buf, &s->q_free);
		}
		wake_up(&s->waitq);
	}
	clear_bit(IVTV_F_I_UDMA, &itv->i_flags);
	clear_bit(IVTV_F_I_DMA, &itv->i_flags);
	itv->cur_dma_stream = -1;
	wake_up(&itv->dma_waitq);
}

static void ivtv_irq_enc_dma_complete(struct ivtv *itv)
{
	u32 data[CX2341X_MBOX_MAX_DATA];
	struct ivtv_stream *s;

	ivtv_api_get_data(&itv->enc_mbox, IVTV_MBOX_DMA_END, 2, data);
	IVTV_DEBUG_HI_IRQ("ENC DMA COMPLETE %x %d (%d)\n", data[0], data[1], itv->cur_dma_stream);

	del_timer(&itv->dma_timer);

	if (itv->cur_dma_stream < 0)
		return;

	s = &itv->streams[itv->cur_dma_stream];
	ivtv_stream_sync_for_cpu(s);

	if (data[0] & 0x18) {
		IVTV_DEBUG_WARN("ENC DMA ERROR %x (offset %08x, xfer %d of %d, retry %d)\n", data[0],
			s->dma_offset, s->sg_processed, s->sg_processing_size, itv->dma_retries);
		write_reg(read_reg(IVTV_REG_DMASTATUS) & 3, IVTV_REG_DMASTATUS);
		if (itv->dma_retries == 3) {
			/* Too many retries, give up on this frame */
			itv->dma_retries = 0;
			s->sg_processed = s->sg_processing_size;
		}
		else {
			/* Retry, starting with the first xfer segment.
			   Just retrying the current segment is not sufficient. */
			s->sg_processed = 0;
			itv->dma_retries++;
		}
	}
	if (s->sg_processed < s->sg_processing_size) {
		/* DMA next buffer */
		ivtv_dma_enc_start_xfer(s);
		return;
	}
	clear_bit(IVTV_F_I_DMA, &itv->i_flags);
	itv->cur_dma_stream = -1;
	dma_post(s);
	if (test_and_clear_bit(IVTV_F_S_DMA_HAS_VBI, &s->s_flags)) {
		s = &itv->streams[IVTV_ENC_STREAM_TYPE_VBI];
		dma_post(s);
	}
	s->sg_processing_size = 0;
	s->sg_processed = 0;
	wake_up(&itv->dma_waitq);
}

static void ivtv_irq_enc_pio_complete(struct ivtv *itv)
{
	struct ivtv_stream *s;

	if (itv->cur_pio_stream < 0 || itv->cur_pio_stream >= IVTV_MAX_STREAMS) {
		itv->cur_pio_stream = -1;
		return;
	}
	s = &itv->streams[itv->cur_pio_stream];
	IVTV_DEBUG_HI_IRQ("ENC PIO COMPLETE %s\n", s->name);
	clear_bit(IVTV_F_I_PIO, &itv->i_flags);
	itv->cur_pio_stream = -1;
	dma_post(s);
	if (s->type == IVTV_ENC_STREAM_TYPE_MPG)
		ivtv_vapi(itv, CX2341X_ENC_SCHED_DMA_TO_HOST, 3, 0, 0, 0);
	else if (s->type == IVTV_ENC_STREAM_TYPE_YUV)
		ivtv_vapi(itv, CX2341X_ENC_SCHED_DMA_TO_HOST, 3, 0, 0, 1);
	else if (s->type == IVTV_ENC_STREAM_TYPE_PCM)
		ivtv_vapi(itv, CX2341X_ENC_SCHED_DMA_TO_HOST, 3, 0, 0, 2);
	clear_bit(IVTV_F_I_PIO, &itv->i_flags);
	if (test_and_clear_bit(IVTV_F_S_DMA_HAS_VBI, &s->s_flags)) {
		s = &itv->streams[IVTV_ENC_STREAM_TYPE_VBI];
		dma_post(s);
	}
	wake_up(&itv->dma_waitq);
}

static void ivtv_irq_dma_err(struct ivtv *itv)
{
	u32 data[CX2341X_MBOX_MAX_DATA];
	u32 status;

	del_timer(&itv->dma_timer);

	ivtv_api_get_data(&itv->enc_mbox, IVTV_MBOX_DMA_END, 2, data);
	status = read_reg(IVTV_REG_DMASTATUS);
	IVTV_DEBUG_WARN("DMA ERROR %08x %08x %08x %d\n", data[0], data[1],
				status, itv->cur_dma_stream);
	/*
	 * We do *not* write back to the IVTV_REG_DMASTATUS register to
	 * clear the error status, if either the encoder write (0x02) or
	 * decoder read (0x01) bus master DMA operation do not indicate
	 * completed.  We can race with the DMA engine, which may have
	 * transitioned to completed status *after* we read the register.
	 * Setting a IVTV_REG_DMASTATUS flag back to "busy" status, after the
	 * DMA engine has completed, will cause the DMA engine to stop working.
	 */
	status &= 0x3;
	if (status == 0x3)
		write_reg(status, IVTV_REG_DMASTATUS);

	if (!test_bit(IVTV_F_I_UDMA, &itv->i_flags) &&
	    itv->cur_dma_stream >= 0 && itv->cur_dma_stream < IVTV_MAX_STREAMS) {
		struct ivtv_stream *s = &itv->streams[itv->cur_dma_stream];

		if (s->type >= IVTV_DEC_STREAM_TYPE_MPG) {
			/* retry */
			/*
			 * FIXME - handle cases of DMA error similar to
			 * encoder below, except conditioned on status & 0x1
			 */
			ivtv_dma_dec_start(s);
			return;
		} else {
			if ((status & 0x2) == 0) {
				/*
				 * CX2341x Bus Master DMA write is ongoing.
				 * Reset the timer and let it complete.
				 */
				itv->dma_timer.expires =
						jiffies + msecs_to_jiffies(600);
				add_timer(&itv->dma_timer);
				return;
			}

			if (itv->dma_retries < 3) {
				/*
				 * CX2341x Bus Master DMA write has ended.
				 * Retry the write, starting with the first
				 * xfer segment. Just retrying the current
				 * segment is not sufficient.
				 */
				s->sg_processed = 0;
				itv->dma_retries++;
				ivtv_dma_enc_start_xfer(s);
				return;
			}
			/* Too many retries, give up on this one */
		}

	}
	if (test_bit(IVTV_F_I_UDMA, &itv->i_flags)) {
		ivtv_udma_start(itv);
		return;
	}
	clear_bit(IVTV_F_I_UDMA, &itv->i_flags);
	clear_bit(IVTV_F_I_DMA, &itv->i_flags);
	itv->cur_dma_stream = -1;
	wake_up(&itv->dma_waitq);
}

static void ivtv_irq_enc_start_cap(struct ivtv *itv)
{
	u32 data[CX2341X_MBOX_MAX_DATA];
	struct ivtv_stream *s;

	/* Get DMA destination and size arguments from card */
	ivtv_api_get_data(&itv->enc_mbox, IVTV_MBOX_DMA, 7, data);
	IVTV_DEBUG_HI_IRQ("ENC START CAP %d: %08x %08x\n", data[0], data[1], data[2]);

	if (data[0] > 2 || data[1] == 0 || data[2] == 0) {
		IVTV_DEBUG_WARN("Unknown input: %08x %08x %08x\n",
				data[0], data[1], data[2]);
		return;
	}
	s = &itv->streams[ivtv_stream_map[data[0]]];
	if (!stream_enc_dma_append(s, data)) {
		set_bit(ivtv_use_pio(s) ? IVTV_F_S_PIO_PENDING : IVTV_F_S_DMA_PENDING, &s->s_flags);
	}
}

static void ivtv_irq_enc_vbi_cap(struct ivtv *itv)
{
	u32 data[CX2341X_MBOX_MAX_DATA];
	struct ivtv_stream *s;

	IVTV_DEBUG_HI_IRQ("ENC START VBI CAP\n");
	s = &itv->streams[IVTV_ENC_STREAM_TYPE_VBI];

	if (!stream_enc_dma_append(s, data))
		set_bit(ivtv_use_pio(s) ? IVTV_F_S_PIO_PENDING : IVTV_F_S_DMA_PENDING, &s->s_flags);
}

static void ivtv_irq_dec_vbi_reinsert(struct ivtv *itv)
{
	u32 data[CX2341X_MBOX_MAX_DATA];
	struct ivtv_stream *s = &itv->streams[IVTV_DEC_STREAM_TYPE_VBI];

	IVTV_DEBUG_HI_IRQ("DEC VBI REINSERT\n");
	if (test_bit(IVTV_F_S_CLAIMED, &s->s_flags) &&
			!stream_enc_dma_append(s, data)) {
		set_bit(IVTV_F_S_PIO_PENDING, &s->s_flags);
	}
}

static void ivtv_irq_dec_data_req(struct ivtv *itv)
{
	u32 data[CX2341X_MBOX_MAX_DATA];
	struct ivtv_stream *s;

	/* YUV or MPG */

	if (test_bit(IVTV_F_I_DEC_YUV, &itv->i_flags)) {
		ivtv_api_get_data(&itv->dec_mbox, IVTV_MBOX_DMA, 2, data);
		itv->dma_data_req_size =
				 1080 * ((itv->yuv_info.v4l2_src_h + 31) & ~31);
		itv->dma_data_req_offset = data[1];
		if (atomic_read(&itv->yuv_info.next_dma_frame) >= 0)
			ivtv_yuv_frame_complete(itv);
		s = &itv->streams[IVTV_DEC_STREAM_TYPE_YUV];
	}
	else {
		ivtv_api_get_data(&itv->dec_mbox, IVTV_MBOX_DMA, 3, data);
		itv->dma_data_req_size = min_t(u32, data[2], 0x10000);
		itv->dma_data_req_offset = data[1];
		s = &itv->streams[IVTV_DEC_STREAM_TYPE_MPG];
	}
	IVTV_DEBUG_HI_IRQ("DEC DATA REQ %s: %d %08x %u\n", s->name, s->q_full.bytesused,
		       itv->dma_data_req_offset, itv->dma_data_req_size);
	if (itv->dma_data_req_size == 0 || s->q_full.bytesused < itv->dma_data_req_size) {
		set_bit(IVTV_F_S_NEEDS_DATA, &s->s_flags);
	}
	else {
		if (test_bit(IVTV_F_I_DEC_YUV, &itv->i_flags))
			ivtv_yuv_setup_stream_frame(itv);
		clear_bit(IVTV_F_S_NEEDS_DATA, &s->s_flags);
		ivtv_queue_move(s, &s->q_full, NULL, &s->q_predma, itv->dma_data_req_size);
		ivtv_dma_stream_dec_prepare(s, itv->dma_data_req_offset + IVTV_DECODER_OFFSET, 0);
	}
}

static void ivtv_irq_vsync(struct ivtv *itv)
{
	/* The vsync interrupt is unusual in that it won't clear until
	 * the end of the first line for the current field, at which
	 * point it clears itself. This can result in repeated vsync
	 * interrupts, or a missed vsync. Read some of the registers
	 * to determine the line being displayed and ensure we handle
	 * one vsync per frame.
	 */
	unsigned int frame = read_reg(IVTV_REG_DEC_LINE_FIELD) & 1;
	struct yuv_playback_info *yi = &itv->yuv_info;
	int last_dma_frame = atomic_read(&yi->next_dma_frame);
	struct yuv_frame_info *f = &yi->new_frame_info[last_dma_frame];

	if (0) IVTV_DEBUG_IRQ("DEC VSYNC\n");

	if (((frame ^ f->sync_field) == 0 &&
		((itv->last_vsync_field & 1) ^ f->sync_field)) ||
			(frame != (itv->last_vsync_field & 1) && !f->interlaced)) {
		int next_dma_frame = last_dma_frame;

		if (!(f->interlaced && f->delay && yi->fields_lapsed < 1)) {
			if (next_dma_frame >= 0 && next_dma_frame != atomic_read(&yi->next_fill_frame)) {
				write_reg(yuv_offset[next_dma_frame] >> 4, 0x82c);
				write_reg((yuv_offset[next_dma_frame] + IVTV_YUV_BUFFER_UV_OFFSET) >> 4, 0x830);
				write_reg(yuv_offset[next_dma_frame] >> 4, 0x834);
				write_reg((yuv_offset[next_dma_frame] + IVTV_YUV_BUFFER_UV_OFFSET) >> 4, 0x838);
				next_dma_frame = (next_dma_frame + 1) % IVTV_YUV_BUFFERS;
				atomic_set(&yi->next_dma_frame, next_dma_frame);
				yi->fields_lapsed = -1;
				yi->running = 1;
			}
		}
	}
	if (frame != (itv->last_vsync_field & 1)) {
		static const struct v4l2_event evtop = {
			.type = V4L2_EVENT_VSYNC,
			.u.vsync.field = V4L2_FIELD_TOP,
		};
		static const struct v4l2_event evbottom = {
			.type = V4L2_EVENT_VSYNC,
			.u.vsync.field = V4L2_FIELD_BOTTOM,
		};
		struct ivtv_stream *s = ivtv_get_output_stream(itv);

		itv->last_vsync_field += 1;
		if (frame == 0) {
			clear_bit(IVTV_F_I_VALID_DEC_TIMINGS, &itv->i_flags);
			clear_bit(IVTV_F_I_EV_VSYNC_FIELD, &itv->i_flags);
		}
		else {
			set_bit(IVTV_F_I_EV_VSYNC_FIELD, &itv->i_flags);
		}
		if (test_bit(IVTV_F_I_EV_VSYNC_ENABLED, &itv->i_flags)) {
			set_bit(IVTV_F_I_EV_VSYNC, &itv->i_flags);
			wake_up(&itv->event_waitq);
			if (s)
				wake_up(&s->waitq);
		}
		if (s && s->vdev)
			v4l2_event_queue(s->vdev, frame ? &evtop : &evbottom);
		wake_up(&itv->vsync_waitq);

		/* Send VBI to saa7127 */
		if (frame && (itv->output_mode == OUT_PASSTHROUGH ||
			test_bit(IVTV_F_I_UPDATE_WSS, &itv->i_flags) ||
			test_bit(IVTV_F_I_UPDATE_VPS, &itv->i_flags) ||
			test_bit(IVTV_F_I_UPDATE_CC, &itv->i_flags))) {
			set_bit(IVTV_F_I_WORK_HANDLER_VBI, &itv->i_flags);
			set_bit(IVTV_F_I_HAVE_WORK, &itv->i_flags);
		}

		/* Check if we need to update the yuv registers */
		if (yi->running && (yi->yuv_forced_update || f->update)) {
			if (!f->update) {
				last_dma_frame =
					(u8)(atomic_read(&yi->next_dma_frame) -
						 1) % IVTV_YUV_BUFFERS;
				f = &yi->new_frame_info[last_dma_frame];
			}

			if (f->src_w) {
				yi->update_frame = last_dma_frame;
				f->update = 0;
				yi->yuv_forced_update = 0;
				set_bit(IVTV_F_I_WORK_HANDLER_YUV, &itv->i_flags);
				set_bit(IVTV_F_I_HAVE_WORK, &itv->i_flags);
			}
		}

		yi->fields_lapsed++;
	}
}

#define IVTV_IRQ_DMA (IVTV_IRQ_DMA_READ | IVTV_IRQ_ENC_DMA_COMPLETE | IVTV_IRQ_DMA_ERR | IVTV_IRQ_ENC_START_CAP | IVTV_IRQ_ENC_VBI_CAP | IVTV_IRQ_DEC_DATA_REQ | IVTV_IRQ_DEC_VBI_RE_INSERT)

irqreturn_t ivtv_irq_handler(int irq, void *dev_id)
{
	struct ivtv *itv = (struct ivtv *)dev_id;
	u32 combo;
	u32 stat;
	int i;
	u8 vsync_force = 0;

	spin_lock(&itv->dma_reg_lock);
	/* get contents of irq status register */
	stat = read_reg(IVTV_REG_IRQSTATUS);

	combo = ~itv->irqmask & stat;

	/* Clear out IRQ */
	if (combo) write_reg(combo, IVTV_REG_IRQSTATUS);

	if (0 == combo) {
		/* The vsync interrupt is unusual and clears itself. If we
		 * took too long, we may have missed it. Do some checks
		 */
		if (~itv->irqmask & IVTV_IRQ_DEC_VSYNC) {
			/* vsync is enabled, see if we're in a new field */
			if ((itv->last_vsync_field & 1) !=
			    (read_reg(IVTV_REG_DEC_LINE_FIELD) & 1)) {
				/* New field, looks like we missed it */
				IVTV_DEBUG_YUV("VSync interrupt missed %d\n",
				       read_reg(IVTV_REG_DEC_LINE_FIELD) >> 16);
				vsync_force = 1;
			}
		}

		if (!vsync_force) {
			/* No Vsync expected, wasn't for us */
			spin_unlock(&itv->dma_reg_lock);
			return IRQ_NONE;
		}
	}

	/* Exclude interrupts noted below from the output, otherwise the log is flooded with
	   these messages */
	if (combo & ~0xff6d0400)
		IVTV_DEBUG_HI_IRQ("======= valid IRQ bits: 0x%08x ======\n", combo);

	if (combo & IVTV_IRQ_DEC_DMA_COMPLETE) {
		IVTV_DEBUG_HI_IRQ("DEC DMA COMPLETE\n");
	}

	if (combo & IVTV_IRQ_DMA_READ) {
		ivtv_irq_dma_read(itv);
	}

	if (combo & IVTV_IRQ_ENC_DMA_COMPLETE) {
		ivtv_irq_enc_dma_complete(itv);
	}

	if (combo & IVTV_IRQ_ENC_PIO_COMPLETE) {
		ivtv_irq_enc_pio_complete(itv);
	}

	if (combo & IVTV_IRQ_DMA_ERR) {
		ivtv_irq_dma_err(itv);
	}

	if (combo & IVTV_IRQ_ENC_START_CAP) {
		ivtv_irq_enc_start_cap(itv);
	}

	if (combo & IVTV_IRQ_ENC_VBI_CAP) {
		ivtv_irq_enc_vbi_cap(itv);
	}

	if (combo & IVTV_IRQ_DEC_VBI_RE_INSERT) {
		ivtv_irq_dec_vbi_reinsert(itv);
	}

	if (combo & IVTV_IRQ_ENC_EOS) {
		IVTV_DEBUG_IRQ("ENC EOS\n");
		set_bit(IVTV_F_I_EOS, &itv->i_flags);
		wake_up(&itv->eos_waitq);
	}

	if (combo & IVTV_IRQ_DEC_DATA_REQ) {
		ivtv_irq_dec_data_req(itv);
	}

	/* Decoder Vertical Sync - We can't rely on 'combo', so check if vsync enabled */
	if (~itv->irqmask & IVTV_IRQ_DEC_VSYNC) {
		ivtv_irq_vsync(itv);
	}

	if (combo & IVTV_IRQ_ENC_VIM_RST) {
		IVTV_DEBUG_IRQ("VIM RST\n");
		/*ivtv_vapi(itv, CX2341X_ENC_REFRESH_INPUT, 0); */
	}

	if (combo & IVTV_IRQ_DEC_AUD_MODE_CHG) {
		IVTV_DEBUG_INFO("Stereo mode changed\n");
	}

	if ((combo & IVTV_IRQ_DMA) && !test_bit(IVTV_F_I_DMA, &itv->i_flags)) {
		itv->irq_rr_idx++;
		for (i = 0; i < IVTV_MAX_STREAMS; i++) {
			int idx = (i + itv->irq_rr_idx) % IVTV_MAX_STREAMS;
			struct ivtv_stream *s = &itv->streams[idx];

			if (!test_and_clear_bit(IVTV_F_S_DMA_PENDING, &s->s_flags))
				continue;
			if (s->type >= IVTV_DEC_STREAM_TYPE_MPG)
				ivtv_dma_dec_start(s);
			else
				ivtv_dma_enc_start(s);
			break;
		}

		if (i == IVTV_MAX_STREAMS &&
		    test_bit(IVTV_F_I_UDMA_PENDING, &itv->i_flags))
			ivtv_udma_start(itv);
	}

	if ((combo & IVTV_IRQ_DMA) && !test_bit(IVTV_F_I_PIO, &itv->i_flags)) {
		itv->irq_rr_idx++;
		for (i = 0; i < IVTV_MAX_STREAMS; i++) {
			int idx = (i + itv->irq_rr_idx) % IVTV_MAX_STREAMS;
			struct ivtv_stream *s = &itv->streams[idx];

			if (!test_and_clear_bit(IVTV_F_S_PIO_PENDING, &s->s_flags))
				continue;
			if (s->type == IVTV_DEC_STREAM_TYPE_VBI || s->type < IVTV_DEC_STREAM_TYPE_MPG)
				ivtv_dma_enc_start(s);
			break;
		}
	}

	if (test_and_clear_bit(IVTV_F_I_HAVE_WORK, &itv->i_flags)) {
		queue_kthread_work(&itv->irq_worker, &itv->irq_work);
	}

	spin_unlock(&itv->dma_reg_lock);

	/* If we've just handled a 'forced' vsync, it's safest to say it
	 * wasn't ours. Another device may have triggered it at just
	 * the right time.
	 */
	return vsync_force ? IRQ_NONE : IRQ_HANDLED;
}

void ivtv_unfinished_dma(unsigned long arg)
{
	struct ivtv *itv = (struct ivtv *)arg;

	if (!test_bit(IVTV_F_I_DMA, &itv->i_flags))
		return;
	IVTV_ERR("DMA TIMEOUT %08x %d\n", read_reg(IVTV_REG_DMASTATUS), itv->cur_dma_stream);

	write_reg(read_reg(IVTV_REG_DMASTATUS) & 3, IVTV_REG_DMASTATUS);
	clear_bit(IVTV_F_I_UDMA, &itv->i_flags);
	clear_bit(IVTV_F_I_DMA, &itv->i_flags);
	itv->cur_dma_stream = -1;
	wake_up(&itv->dma_waitq);
}
