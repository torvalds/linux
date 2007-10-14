/*
    file operation functions
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
#include "ivtv-fileops.h"
#include "ivtv-i2c.h"
#include "ivtv-queue.h"
#include "ivtv-udma.h"
#include "ivtv-irq.h"
#include "ivtv-vbi.h"
#include "ivtv-mailbox.h"
#include "ivtv-routing.h"
#include "ivtv-streams.h"
#include "ivtv-yuv.h"
#include "ivtv-ioctl.h"
#include "ivtv-cards.h"
#include <media/saa7115.h>

/* This function tries to claim the stream for a specific file descriptor.
   If no one else is using this stream then the stream is claimed and
   associated VBI streams are also automatically claimed.
   Possible error returns: -EBUSY if someone else has claimed
   the stream or 0 on success. */
int ivtv_claim_stream(struct ivtv_open_id *id, int type)
{
	struct ivtv *itv = id->itv;
	struct ivtv_stream *s = &itv->streams[type];
	struct ivtv_stream *s_vbi;
	int vbi_type;

	if (test_and_set_bit(IVTV_F_S_CLAIMED, &s->s_flags)) {
		/* someone already claimed this stream */
		if (s->id == id->open_id) {
			/* yes, this file descriptor did. So that's OK. */
			return 0;
		}
		if (s->id == -1 && (type == IVTV_DEC_STREAM_TYPE_VBI ||
					 type == IVTV_ENC_STREAM_TYPE_VBI)) {
			/* VBI is handled already internally, now also assign
			   the file descriptor to this stream for external
			   reading of the stream. */
			s->id = id->open_id;
			IVTV_DEBUG_INFO("Start Read VBI\n");
			return 0;
		}
		/* someone else is using this stream already */
		IVTV_DEBUG_INFO("Stream %d is busy\n", type);
		return -EBUSY;
	}
	s->id = id->open_id;
	if (type == IVTV_DEC_STREAM_TYPE_VBI) {
		/* Enable reinsertion interrupt */
		ivtv_clear_irq_mask(itv, IVTV_IRQ_DEC_VBI_RE_INSERT);
	}

	/* IVTV_DEC_STREAM_TYPE_MPG needs to claim IVTV_DEC_STREAM_TYPE_VBI,
	   IVTV_ENC_STREAM_TYPE_MPG needs to claim IVTV_ENC_STREAM_TYPE_VBI
	   (provided VBI insertion is on and sliced VBI is selected), for all
	   other streams we're done */
	if (type == IVTV_DEC_STREAM_TYPE_MPG) {
		vbi_type = IVTV_DEC_STREAM_TYPE_VBI;
	} else if (type == IVTV_ENC_STREAM_TYPE_MPG &&
		   itv->vbi.insert_mpeg && itv->vbi.sliced_in->service_set) {
		vbi_type = IVTV_ENC_STREAM_TYPE_VBI;
	} else {
		return 0;
	}
	s_vbi = &itv->streams[vbi_type];

	if (!test_and_set_bit(IVTV_F_S_CLAIMED, &s_vbi->s_flags)) {
		/* Enable reinsertion interrupt */
		if (vbi_type == IVTV_DEC_STREAM_TYPE_VBI)
			ivtv_clear_irq_mask(itv, IVTV_IRQ_DEC_VBI_RE_INSERT);
	}
	/* mark that it is used internally */
	set_bit(IVTV_F_S_INTERNAL_USE, &s_vbi->s_flags);
	return 0;
}

/* This function releases a previously claimed stream. It will take into
   account associated VBI streams. */
void ivtv_release_stream(struct ivtv_stream *s)
{
	struct ivtv *itv = s->itv;
	struct ivtv_stream *s_vbi;

	s->id = -1;
	if ((s->type == IVTV_DEC_STREAM_TYPE_VBI || s->type == IVTV_ENC_STREAM_TYPE_VBI) &&
		test_bit(IVTV_F_S_INTERNAL_USE, &s->s_flags)) {
		/* this stream is still in use internally */
		return;
	}
	if (!test_and_clear_bit(IVTV_F_S_CLAIMED, &s->s_flags)) {
		IVTV_DEBUG_WARN("Release stream %s not in use!\n", s->name);
		return;
	}

	ivtv_flush_queues(s);

	/* disable reinsertion interrupt */
	if (s->type == IVTV_DEC_STREAM_TYPE_VBI)
		ivtv_set_irq_mask(itv, IVTV_IRQ_DEC_VBI_RE_INSERT);

	/* IVTV_DEC_STREAM_TYPE_MPG needs to release IVTV_DEC_STREAM_TYPE_VBI,
	   IVTV_ENC_STREAM_TYPE_MPG needs to release IVTV_ENC_STREAM_TYPE_VBI,
	   for all other streams we're done */
	if (s->type == IVTV_DEC_STREAM_TYPE_MPG)
		s_vbi = &itv->streams[IVTV_DEC_STREAM_TYPE_VBI];
	else if (s->type == IVTV_ENC_STREAM_TYPE_MPG)
		s_vbi = &itv->streams[IVTV_ENC_STREAM_TYPE_VBI];
	else
		return;

	/* clear internal use flag */
	if (!test_and_clear_bit(IVTV_F_S_INTERNAL_USE, &s_vbi->s_flags)) {
		/* was already cleared */
		return;
	}
	if (s_vbi->id != -1) {
		/* VBI stream still claimed by a file descriptor */
		return;
	}
	/* disable reinsertion interrupt */
	if (s_vbi->type == IVTV_DEC_STREAM_TYPE_VBI)
		ivtv_set_irq_mask(itv, IVTV_IRQ_DEC_VBI_RE_INSERT);
	clear_bit(IVTV_F_S_CLAIMED, &s_vbi->s_flags);
	ivtv_flush_queues(s_vbi);
}

static void ivtv_dualwatch(struct ivtv *itv)
{
	struct v4l2_tuner vt;
	u16 new_bitmap;
	u16 new_stereo_mode;
	const u16 stereo_mask = 0x0300;
	const u16 dual = 0x0200;

	new_stereo_mode = itv->params.audio_properties & stereo_mask;
	memset(&vt, 0, sizeof(vt));
	ivtv_call_i2c_clients(itv, VIDIOC_G_TUNER, &vt);
	if (vt.audmode == V4L2_TUNER_MODE_LANG1_LANG2 && (vt.rxsubchans & V4L2_TUNER_SUB_LANG2))
		new_stereo_mode = dual;

	if (new_stereo_mode == itv->dualwatch_stereo_mode)
		return;

	new_bitmap = new_stereo_mode | (itv->params.audio_properties & ~stereo_mask);

	IVTV_DEBUG_INFO("dualwatch: change stereo flag from 0x%x to 0x%x. new audio_bitmask=0x%ux\n",
			   itv->dualwatch_stereo_mode, new_stereo_mode, new_bitmap);

	if (ivtv_vapi(itv, CX2341X_ENC_SET_AUDIO_PROPERTIES, 1, new_bitmap) == 0) {
		itv->dualwatch_stereo_mode = new_stereo_mode;
		return;
	}
	IVTV_DEBUG_INFO("dualwatch: changing stereo flag failed\n");
}

static void ivtv_update_pgm_info(struct ivtv *itv)
{
	u32 wr_idx = (read_enc(itv->pgm_info_offset) - itv->pgm_info_offset - 4) / 24;
	int cnt;
	int i = 0;

	if (wr_idx >= itv->pgm_info_num) {
		IVTV_DEBUG_WARN("Invalid PGM index %d (>= %d)\n", wr_idx, itv->pgm_info_num);
		return;
	}
	cnt = (wr_idx + itv->pgm_info_num - itv->pgm_info_write_idx) % itv->pgm_info_num;
	while (i < cnt) {
		int idx = (itv->pgm_info_write_idx + i) % itv->pgm_info_num;
		struct v4l2_enc_idx_entry *e = itv->pgm_info + idx;
		u32 addr = itv->pgm_info_offset + 4 + idx * 24;
		const int mapping[8] = { -1, V4L2_ENC_IDX_FRAME_I, V4L2_ENC_IDX_FRAME_P, -1,
			V4L2_ENC_IDX_FRAME_B, -1, -1, -1 };
					// 1=I, 2=P, 4=B

		e->offset = read_enc(addr + 4) + ((u64)read_enc(addr + 8) << 32);
		if (e->offset > itv->mpg_data_received) {
			break;
		}
		e->offset += itv->vbi_data_inserted;
		e->length = read_enc(addr);
		e->pts = read_enc(addr + 16) + ((u64)(read_enc(addr + 20) & 1) << 32);
		e->flags = mapping[read_enc(addr + 12) & 7];
		i++;
	}
	itv->pgm_info_write_idx = (itv->pgm_info_write_idx + i) % itv->pgm_info_num;
}

static struct ivtv_buffer *ivtv_get_buffer(struct ivtv_stream *s, int non_block, int *err)
{
	struct ivtv *itv = s->itv;
	struct ivtv_stream *s_vbi = &itv->streams[IVTV_ENC_STREAM_TYPE_VBI];
	struct ivtv_buffer *buf;
	DEFINE_WAIT(wait);

	*err = 0;
	while (1) {
		if (s->type == IVTV_ENC_STREAM_TYPE_MPG) {
			/* Process pending program info updates and pending VBI data */
			ivtv_update_pgm_info(itv);

			if (jiffies - itv->dualwatch_jiffies > msecs_to_jiffies(1000)) {
				itv->dualwatch_jiffies = jiffies;
				ivtv_dualwatch(itv);
			}

			if (test_bit(IVTV_F_S_INTERNAL_USE, &s_vbi->s_flags) &&
			    !test_bit(IVTV_F_S_APPL_IO, &s_vbi->s_flags)) {
				while ((buf = ivtv_dequeue(s_vbi, &s_vbi->q_full))) {
					/* byteswap and process VBI data */
					ivtv_process_vbi_data(itv, buf, s_vbi->dma_pts, s_vbi->type);
					ivtv_enqueue(s_vbi, buf, &s_vbi->q_free);
				}
			}
			buf = &itv->vbi.sliced_mpeg_buf;
			if (buf->readpos != buf->bytesused) {
				return buf;
			}
		}

		/* do we have leftover data? */
		buf = ivtv_dequeue(s, &s->q_io);
		if (buf)
			return buf;

		/* do we have new data? */
		buf = ivtv_dequeue(s, &s->q_full);
		if (buf) {
			if ((buf->b_flags & IVTV_F_B_NEED_BUF_SWAP) == 0)
				return buf;
			buf->b_flags &= ~IVTV_F_B_NEED_BUF_SWAP;
			if (s->type == IVTV_ENC_STREAM_TYPE_MPG)
				/* byteswap MPG data */
				ivtv_buf_swap(buf);
			else if (s->type != IVTV_DEC_STREAM_TYPE_VBI) {
				/* byteswap and process VBI data */
				ivtv_process_vbi_data(itv, buf, s->dma_pts, s->type);
			}
			return buf;
		}

		/* return if end of stream */
		if (s->type != IVTV_DEC_STREAM_TYPE_VBI && !test_bit(IVTV_F_S_STREAMING, &s->s_flags)) {
			IVTV_DEBUG_INFO("EOS %s\n", s->name);
			return NULL;
		}

		/* return if file was opened with O_NONBLOCK */
		if (non_block) {
			*err = -EAGAIN;
			return NULL;
		}

		/* wait for more data to arrive */
		prepare_to_wait(&s->waitq, &wait, TASK_INTERRUPTIBLE);
		/* New buffers might have become available before we were added to the waitqueue */
		if (!s->q_full.buffers)
			schedule();
		finish_wait(&s->waitq, &wait);
		if (signal_pending(current)) {
			/* return if a signal was received */
			IVTV_DEBUG_INFO("User stopped %s\n", s->name);
			*err = -EINTR;
			return NULL;
		}
	}
}

static void ivtv_setup_sliced_vbi_buf(struct ivtv *itv)
{
	int idx = itv->vbi.inserted_frame % IVTV_VBI_FRAMES;

	itv->vbi.sliced_mpeg_buf.buf = itv->vbi.sliced_mpeg_data[idx];
	itv->vbi.sliced_mpeg_buf.bytesused = itv->vbi.sliced_mpeg_size[idx];
	itv->vbi.sliced_mpeg_buf.readpos = 0;
}

static size_t ivtv_copy_buf_to_user(struct ivtv_stream *s, struct ivtv_buffer *buf,
		char __user *ubuf, size_t ucount)
{
	struct ivtv *itv = s->itv;
	size_t len = buf->bytesused - buf->readpos;

	if (len > ucount) len = ucount;
	if (itv->vbi.insert_mpeg && s->type == IVTV_ENC_STREAM_TYPE_MPG &&
	    itv->vbi.sliced_in->service_set && buf != &itv->vbi.sliced_mpeg_buf) {
		const char *start = buf->buf + buf->readpos;
		const char *p = start + 1;
		const u8 *q;
		u8 ch = itv->search_pack_header ? 0xba : 0xe0;
		int stuffing, i;

		while (start + len > p && (q = memchr(p, 0, start + len - p))) {
			p = q + 1;
			if ((char *)q + 15 >= buf->buf + buf->bytesused ||
			    q[1] != 0 || q[2] != 1 || q[3] != ch) {
				continue;
			}
			if (!itv->search_pack_header) {
				if ((q[6] & 0xc0) != 0x80)
					continue;
				if (((q[7] & 0xc0) == 0x80 && (q[9] & 0xf0) == 0x20) ||
				    ((q[7] & 0xc0) == 0xc0 && (q[9] & 0xf0) == 0x30)) {
					ch = 0xba;
					itv->search_pack_header = 1;
					p = q + 9;
				}
				continue;
			}
			stuffing = q[13] & 7;
			/* all stuffing bytes must be 0xff */
			for (i = 0; i < stuffing; i++)
				if (q[14 + i] != 0xff)
					break;
			if (i == stuffing && (q[4] & 0xc4) == 0x44 && (q[12] & 3) == 3 &&
					q[14 + stuffing] == 0 && q[15 + stuffing] == 0 &&
					q[16 + stuffing] == 1) {
				itv->search_pack_header = 0;
				len = (char *)q - start;
				ivtv_setup_sliced_vbi_buf(itv);
				break;
			}
		}
	}
	if (copy_to_user(ubuf, (u8 *)buf->buf + buf->readpos, len)) {
		IVTV_DEBUG_WARN("copy %zd bytes to user failed for %s\n", len, s->name);
		return -EFAULT;
	}
	/*IVTV_INFO("copied %lld %d %d %d %d %d vbi %d\n", itv->mpg_data_received, len, ucount,
			buf->readpos, buf->bytesused, buf->bytesused - buf->readpos - len,
			buf == &itv->vbi.sliced_mpeg_buf); */
	buf->readpos += len;
	if (s->type == IVTV_ENC_STREAM_TYPE_MPG && buf != &itv->vbi.sliced_mpeg_buf)
		itv->mpg_data_received += len;
	return len;
}

static ssize_t ivtv_read(struct ivtv_stream *s, char __user *ubuf, size_t tot_count, int non_block)
{
	struct ivtv *itv = s->itv;
	size_t tot_written = 0;
	int single_frame = 0;

	if (atomic_read(&itv->capturing) == 0 && s->id == -1) {
		/* shouldn't happen */
		IVTV_DEBUG_WARN("Stream %s not initialized before read\n", s->name);
		return -EIO;
	}

	/* Each VBI buffer is one frame, the v4l2 API says that for VBI the frames should
	   arrive one-by-one, so make sure we never output more than one VBI frame at a time */
	if (s->type == IVTV_DEC_STREAM_TYPE_VBI ||
			(s->type == IVTV_ENC_STREAM_TYPE_VBI && itv->vbi.sliced_in->service_set))
		single_frame = 1;

	for (;;) {
		struct ivtv_buffer *buf;
		int rc;

		buf = ivtv_get_buffer(s, non_block, &rc);
		/* if there is no data available... */
		if (buf == NULL) {
			/* if we got data, then return that regardless */
			if (tot_written)
				break;
			/* EOS condition */
			if (rc == 0) {
				clear_bit(IVTV_F_S_STREAMOFF, &s->s_flags);
				clear_bit(IVTV_F_S_APPL_IO, &s->s_flags);
				ivtv_release_stream(s);
			}
			/* set errno */
			return rc;
		}
		rc = ivtv_copy_buf_to_user(s, buf, ubuf + tot_written, tot_count - tot_written);
		if (buf != &itv->vbi.sliced_mpeg_buf) {
			ivtv_enqueue(s, buf, (buf->readpos == buf->bytesused) ? &s->q_free : &s->q_io);
		}
		else if (buf->readpos == buf->bytesused) {
			int idx = itv->vbi.inserted_frame % IVTV_VBI_FRAMES;
			itv->vbi.sliced_mpeg_size[idx] = 0;
			itv->vbi.inserted_frame++;
			itv->vbi_data_inserted += buf->bytesused;
		}
		if (rc < 0)
			return rc;
		tot_written += rc;

		if (tot_written == tot_count || single_frame)
			break;
	}
	return tot_written;
}

static ssize_t ivtv_read_pos(struct ivtv_stream *s, char __user *ubuf, size_t count,
			loff_t *pos, int non_block)
{
	ssize_t rc = count ? ivtv_read(s, ubuf, count, non_block) : 0;
	struct ivtv *itv = s->itv;

	IVTV_DEBUG_HI_FILE("read %zd from %s, got %zd\n", count, s->name, rc);
	if (rc > 0)
		pos += rc;
	return rc;
}

int ivtv_start_capture(struct ivtv_open_id *id)
{
	struct ivtv *itv = id->itv;
	struct ivtv_stream *s = &itv->streams[id->type];
	struct ivtv_stream *s_vbi;

	if (s->type == IVTV_ENC_STREAM_TYPE_RAD ||
	    s->type == IVTV_DEC_STREAM_TYPE_MPG ||
	    s->type == IVTV_DEC_STREAM_TYPE_YUV ||
	    s->type == IVTV_DEC_STREAM_TYPE_VOUT) {
		/* you cannot read from these stream types. */
		return -EPERM;
	}

	/* Try to claim this stream. */
	if (ivtv_claim_stream(id, s->type))
		return -EBUSY;

	/* This stream does not need to start capturing */
	if (s->type == IVTV_DEC_STREAM_TYPE_VBI) {
		set_bit(IVTV_F_S_APPL_IO, &s->s_flags);
		return 0;
	}

	/* If capture is already in progress, then we also have to
	   do nothing extra. */
	if (test_bit(IVTV_F_S_STREAMOFF, &s->s_flags) || test_and_set_bit(IVTV_F_S_STREAMING, &s->s_flags)) {
		set_bit(IVTV_F_S_APPL_IO, &s->s_flags);
		return 0;
	}

	/* Start VBI capture if required */
	s_vbi = &itv->streams[IVTV_ENC_STREAM_TYPE_VBI];
	if (s->type == IVTV_ENC_STREAM_TYPE_MPG &&
	    test_bit(IVTV_F_S_INTERNAL_USE, &s_vbi->s_flags) &&
	    !test_and_set_bit(IVTV_F_S_STREAMING, &s_vbi->s_flags)) {
		/* Note: the IVTV_ENC_STREAM_TYPE_VBI is claimed
		   automatically when the MPG stream is claimed.
		   We only need to start the VBI capturing. */
		if (ivtv_start_v4l2_encode_stream(s_vbi)) {
			IVTV_DEBUG_WARN("VBI capture start failed\n");

			/* Failure, clean up and return an error */
			clear_bit(IVTV_F_S_STREAMING, &s_vbi->s_flags);
			clear_bit(IVTV_F_S_STREAMING, &s->s_flags);
			/* also releases the associated VBI stream */
			ivtv_release_stream(s);
			return -EIO;
		}
		IVTV_DEBUG_INFO("VBI insertion started\n");
	}

	/* Tell the card to start capturing */
	if (!ivtv_start_v4l2_encode_stream(s)) {
		/* We're done */
		set_bit(IVTV_F_S_APPL_IO, &s->s_flags);
		/* Resume a possibly paused encoder */
		if (test_and_clear_bit(IVTV_F_I_ENC_PAUSED, &itv->i_flags))
			ivtv_vapi(itv, CX2341X_ENC_PAUSE_ENCODER, 1, 1);
		return 0;
	}

	/* failure, clean up */
	IVTV_DEBUG_WARN("Failed to start capturing for stream %s\n", s->name);

	/* Note: the IVTV_ENC_STREAM_TYPE_VBI is released
	   automatically when the MPG stream is released.
	   We only need to stop the VBI capturing. */
	if (s->type == IVTV_ENC_STREAM_TYPE_MPG &&
	    test_bit(IVTV_F_S_STREAMING, &s_vbi->s_flags)) {
		ivtv_stop_v4l2_encode_stream(s_vbi, 0);
		clear_bit(IVTV_F_S_STREAMING, &s_vbi->s_flags);
	}
	clear_bit(IVTV_F_S_STREAMING, &s->s_flags);
	ivtv_release_stream(s);
	return -EIO;
}

ssize_t ivtv_v4l2_read(struct file * filp, char __user *buf, size_t count, loff_t * pos)
{
	struct ivtv_open_id *id = filp->private_data;
	struct ivtv *itv = id->itv;
	struct ivtv_stream *s = &itv->streams[id->type];
	int rc;

	IVTV_DEBUG_HI_FILE("read %zd bytes from %s\n", count, s->name);

	mutex_lock(&itv->serialize_lock);
	rc = ivtv_start_capture(id);
	mutex_unlock(&itv->serialize_lock);
	if (rc)
		return rc;
	return ivtv_read_pos(s, buf, count, pos, filp->f_flags & O_NONBLOCK);
}

int ivtv_start_decoding(struct ivtv_open_id *id, int speed)
{
	struct ivtv *itv = id->itv;
	struct ivtv_stream *s = &itv->streams[id->type];

	if (atomic_read(&itv->decoding) == 0) {
		if (ivtv_claim_stream(id, s->type)) {
			/* someone else is using this stream already */
			IVTV_DEBUG_WARN("start decode, stream already claimed\n");
			return -EBUSY;
		}
		ivtv_start_v4l2_decode_stream(s, 0);
	}
	if (s->type == IVTV_DEC_STREAM_TYPE_MPG)
		return ivtv_set_speed(itv, speed);
	return 0;
}

ssize_t ivtv_v4l2_write(struct file *filp, const char __user *user_buf, size_t count, loff_t *pos)
{
	struct ivtv_open_id *id = filp->private_data;
	struct ivtv *itv = id->itv;
	struct ivtv_stream *s = &itv->streams[id->type];
	struct ivtv_buffer *buf;
	struct ivtv_queue q;
	int bytes_written = 0;
	int mode;
	int rc;
	DEFINE_WAIT(wait);

	IVTV_DEBUG_HI_FILE("write %zd bytes to %s\n", count, s->name);

	if (s->type != IVTV_DEC_STREAM_TYPE_MPG &&
	    s->type != IVTV_DEC_STREAM_TYPE_YUV &&
	    s->type != IVTV_DEC_STREAM_TYPE_VOUT)
		/* not decoder streams */
		return -EPERM;

	/* Try to claim this stream */
	if (ivtv_claim_stream(id, s->type))
		return -EBUSY;

	/* This stream does not need to start any decoding */
	if (s->type == IVTV_DEC_STREAM_TYPE_VOUT) {
		int elems = count / sizeof(struct v4l2_sliced_vbi_data);

		set_bit(IVTV_F_S_APPL_IO, &s->s_flags);
		ivtv_write_vbi(itv, (const struct v4l2_sliced_vbi_data *)user_buf, elems);
		return elems * sizeof(struct v4l2_sliced_vbi_data);
	}

	mode = s->type == IVTV_DEC_STREAM_TYPE_MPG ? OUT_MPG : OUT_YUV;

	if (ivtv_set_output_mode(itv, mode) != mode) {
	    ivtv_release_stream(s);
	    return -EBUSY;
	}
	ivtv_queue_init(&q);
	set_bit(IVTV_F_S_APPL_IO, &s->s_flags);

retry:
	for (;;) {
		/* Gather buffers */
		while (q.length - q.bytesused < count && (buf = ivtv_dequeue(s, &s->q_io)))
			ivtv_enqueue(s, buf, &q);
		while (q.length - q.bytesused < count && (buf = ivtv_dequeue(s, &s->q_free))) {
			ivtv_enqueue(s, buf, &q);
		}
		if (q.buffers)
			break;
		if (filp->f_flags & O_NONBLOCK)
			return -EAGAIN;
		prepare_to_wait(&s->waitq, &wait, TASK_INTERRUPTIBLE);
		/* New buffers might have become free before we were added to the waitqueue */
		if (!s->q_free.buffers)
			schedule();
		finish_wait(&s->waitq, &wait);
		if (signal_pending(current)) {
			IVTV_DEBUG_INFO("User stopped %s\n", s->name);
			return -EINTR;
		}
	}

	/* copy user data into buffers */
	while ((buf = ivtv_dequeue(s, &q))) {
		/* Make sure we really got all the user data */
		rc = ivtv_buf_copy_from_user(s, buf, user_buf, count);

		if (rc < 0) {
			ivtv_queue_move(s, &q, NULL, &s->q_free, 0);
			return rc;
		}
		user_buf += rc;
		count -= rc;
		bytes_written += rc;

		if (buf->bytesused != s->buf_size) {
			/* incomplete, leave in q_io for next time */
			ivtv_enqueue(s, buf, &s->q_io);
			break;
		}
		/* Byteswap MPEG buffer */
		if (s->type == IVTV_DEC_STREAM_TYPE_MPG)
			ivtv_buf_swap(buf);
		ivtv_enqueue(s, buf, &s->q_full);
	}

	/* Start decoder (returns 0 if already started) */
	mutex_lock(&itv->serialize_lock);
	rc = ivtv_start_decoding(id, itv->speed);
	mutex_unlock(&itv->serialize_lock);
	if (rc) {
		IVTV_DEBUG_WARN("Failed start decode stream %s\n", s->name);

		/* failure, clean up */
		clear_bit(IVTV_F_S_STREAMING, &s->s_flags);
		clear_bit(IVTV_F_S_APPL_IO, &s->s_flags);
		return rc;
	}
	if (test_bit(IVTV_F_S_NEEDS_DATA, &s->s_flags)) {
		if (s->q_full.length >= itv->dma_data_req_size) {
			int got_sig;

			prepare_to_wait(&itv->dma_waitq, &wait, TASK_INTERRUPTIBLE);
			while (!(got_sig = signal_pending(current)) &&
					test_bit(IVTV_F_S_DMA_PENDING, &s->s_flags)) {
				schedule();
			}
			finish_wait(&itv->dma_waitq, &wait);
			if (got_sig) {
				IVTV_DEBUG_INFO("User interrupted %s\n", s->name);
				return -EINTR;
			}

			clear_bit(IVTV_F_S_NEEDS_DATA, &s->s_flags);
			ivtv_queue_move(s, &s->q_full, NULL, &s->q_predma, itv->dma_data_req_size);
			ivtv_dma_stream_dec_prepare(s, itv->dma_data_req_offset + IVTV_DECODER_OFFSET, 1);
		}
	}
	/* more user data is available, wait until buffers become free
	   to transfer the rest. */
	if (count && !(filp->f_flags & O_NONBLOCK))
		goto retry;
	IVTV_DEBUG_HI_FILE("Wrote %d bytes to %s (%d)\n", bytes_written, s->name, s->q_full.bytesused);
	return bytes_written;
}

unsigned int ivtv_v4l2_dec_poll(struct file *filp, poll_table *wait)
{
	struct ivtv_open_id *id = filp->private_data;
	struct ivtv *itv = id->itv;
	struct ivtv_stream *s = &itv->streams[id->type];
	int res = 0;

	/* add stream's waitq to the poll list */
	IVTV_DEBUG_HI_FILE("Decoder poll\n");
	poll_wait(filp, &s->waitq, wait);

	set_bit(IVTV_F_I_EV_VSYNC_ENABLED, &itv->i_flags);
	if (test_bit(IVTV_F_I_EV_VSYNC, &itv->i_flags) ||
	    test_bit(IVTV_F_I_EV_DEC_STOPPED, &itv->i_flags))
		res = POLLPRI;

	/* Allow write if buffers are available for writing */
	if (s->q_free.buffers)
		res |= POLLOUT | POLLWRNORM;
	return res;
}

unsigned int ivtv_v4l2_enc_poll(struct file *filp, poll_table * wait)
{
	struct ivtv_open_id *id = filp->private_data;
	struct ivtv *itv = id->itv;
	struct ivtv_stream *s = &itv->streams[id->type];
	int eof = test_bit(IVTV_F_S_STREAMOFF, &s->s_flags);

	/* Start a capture if there is none */
	if (!eof && !test_bit(IVTV_F_S_STREAMING, &s->s_flags)) {
		int rc;

		mutex_lock(&itv->serialize_lock);
		rc = ivtv_start_capture(id);
		mutex_unlock(&itv->serialize_lock);
		if (rc) {
			IVTV_DEBUG_INFO("Could not start capture for %s (%d)\n",
					s->name, rc);
			return POLLERR;
		}
		IVTV_DEBUG_FILE("Encoder poll started capture\n");
	}

	/* add stream's waitq to the poll list */
	IVTV_DEBUG_HI_FILE("Encoder poll\n");
	poll_wait(filp, &s->waitq, wait);

	if (eof || s->q_full.length)
		return POLLIN | POLLRDNORM;
	return 0;
}

void ivtv_stop_capture(struct ivtv_open_id *id, int gop_end)
{
	struct ivtv *itv = id->itv;
	struct ivtv_stream *s = &itv->streams[id->type];

	IVTV_DEBUG_FILE("close() of %s\n", s->name);

	/* 'Unclaim' this stream */

	/* Stop capturing */
	if (test_bit(IVTV_F_S_STREAMING, &s->s_flags)) {
		struct ivtv_stream *s_vbi = &itv->streams[IVTV_ENC_STREAM_TYPE_VBI];

		IVTV_DEBUG_INFO("close stopping capture\n");
		/* Special case: a running VBI capture for VBI insertion
		   in the mpeg stream. Need to stop that too. */
		if (id->type == IVTV_ENC_STREAM_TYPE_MPG &&
		    test_bit(IVTV_F_S_STREAMING, &s_vbi->s_flags) &&
		    !test_bit(IVTV_F_S_APPL_IO, &s_vbi->s_flags)) {
			IVTV_DEBUG_INFO("close stopping embedded VBI capture\n");
			ivtv_stop_v4l2_encode_stream(s_vbi, 0);
		}
		if ((id->type == IVTV_DEC_STREAM_TYPE_VBI ||
		     id->type == IVTV_ENC_STREAM_TYPE_VBI) &&
		    test_bit(IVTV_F_S_INTERNAL_USE, &s->s_flags)) {
			/* Also used internally, don't stop capturing */
			s->id = -1;
		}
		else {
			ivtv_stop_v4l2_encode_stream(s, gop_end);
		}
	}
	if (!gop_end) {
		clear_bit(IVTV_F_S_APPL_IO, &s->s_flags);
		clear_bit(IVTV_F_S_STREAMOFF, &s->s_flags);
		ivtv_release_stream(s);
	}
}

static void ivtv_stop_decoding(struct ivtv_open_id *id, int flags, u64 pts)
{
	struct ivtv *itv = id->itv;
	struct ivtv_stream *s = &itv->streams[id->type];

	IVTV_DEBUG_FILE("close() of %s\n", s->name);

	/* Stop decoding */
	if (test_bit(IVTV_F_S_STREAMING, &s->s_flags)) {
		IVTV_DEBUG_INFO("close stopping decode\n");

		ivtv_stop_v4l2_decode_stream(s, flags, pts);
		itv->output_mode = OUT_NONE;
	}
	clear_bit(IVTV_F_S_APPL_IO, &s->s_flags);
	clear_bit(IVTV_F_S_STREAMOFF, &s->s_flags);
	if (id->type == IVTV_DEC_STREAM_TYPE_YUV && test_bit(IVTV_F_I_DECODING_YUV, &itv->i_flags)) {
		/* Restore registers we've changed & clean up any mess we've made */
		ivtv_yuv_close(itv);
	}
	if (itv->output_mode == OUT_UDMA_YUV && id->yuv_frames)
		itv->output_mode = OUT_NONE;

	itv->speed = 0;
	clear_bit(IVTV_F_I_DEC_PAUSED, &itv->i_flags);
	ivtv_release_stream(s);
}

int ivtv_v4l2_close(struct inode *inode, struct file *filp)
{
	struct ivtv_open_id *id = filp->private_data;
	struct ivtv *itv = id->itv;
	struct ivtv_stream *s = &itv->streams[id->type];

	IVTV_DEBUG_FILE("close %s\n", s->name);

	v4l2_prio_close(&itv->prio, &id->prio);

	/* Easy case first: this stream was never claimed by us */
	if (s->id != id->open_id) {
		kfree(id);
		return 0;
	}

	/* 'Unclaim' this stream */

	/* Stop radio */
	mutex_lock(&itv->serialize_lock);
	if (id->type == IVTV_ENC_STREAM_TYPE_RAD) {
		/* Closing radio device, return to TV mode */
		ivtv_mute(itv);
		/* Mark that the radio is no longer in use */
		clear_bit(IVTV_F_I_RADIO_USER, &itv->i_flags);
		/* Switch tuner to TV */
		ivtv_call_i2c_clients(itv, VIDIOC_S_STD, &itv->std);
		/* Select correct audio input (i.e. TV tuner or Line in) */
		ivtv_audio_set_io(itv);
		if (itv->hw_flags & IVTV_HW_SAA711X)
		{
			struct v4l2_crystal_freq crystal_freq;
			crystal_freq.freq = SAA7115_FREQ_32_11_MHZ;
			crystal_freq.flags = 0;
			ivtv_saa7115(itv, VIDIOC_INT_S_CRYSTAL_FREQ, &crystal_freq);
		}
		if (atomic_read(&itv->capturing) > 0) {
			/* Undo video mute */
			ivtv_vapi(itv, CX2341X_ENC_MUTE_VIDEO, 1,
				itv->params.video_mute | (itv->params.video_mute_yuv << 8));
		}
		/* Done! Unmute and continue. */
		ivtv_unmute(itv);
		ivtv_release_stream(s);
	} else if (s->type >= IVTV_DEC_STREAM_TYPE_MPG) {
		struct ivtv_stream *s_vout = &itv->streams[IVTV_DEC_STREAM_TYPE_VOUT];

		ivtv_stop_decoding(id, VIDEO_CMD_STOP_TO_BLACK | VIDEO_CMD_STOP_IMMEDIATELY, 0);

		/* If all output streams are closed, and if the user doesn't have
		   IVTV_DEC_STREAM_TYPE_VOUT open, then disable CC on TV-out. */
		if (itv->output_mode == OUT_NONE && !test_bit(IVTV_F_S_APPL_IO, &s_vout->s_flags)) {
			/* disable CC on TV-out */
			ivtv_disable_cc(itv);
		}
	} else {
		ivtv_stop_capture(id, 0);
	}
	kfree(id);
	mutex_unlock(&itv->serialize_lock);
	return 0;
}

static int ivtv_serialized_open(struct ivtv_stream *s, struct file *filp)
{
	struct ivtv *itv = s->itv;
	struct ivtv_open_id *item;

	IVTV_DEBUG_FILE("open %s\n", s->name);

	if (s->type == IVTV_DEC_STREAM_TYPE_MPG &&
		test_bit(IVTV_F_S_CLAIMED, &itv->streams[IVTV_DEC_STREAM_TYPE_YUV].s_flags))
		return -EBUSY;

	if (s->type == IVTV_DEC_STREAM_TYPE_YUV &&
		test_bit(IVTV_F_S_CLAIMED, &itv->streams[IVTV_DEC_STREAM_TYPE_MPG].s_flags))
		return -EBUSY;

	if (s->type == IVTV_DEC_STREAM_TYPE_YUV) {
		if (read_reg(0x82c) == 0) {
			IVTV_ERR("Tried to open YUV output device but need to send data to mpeg decoder before it can be used\n");
			/* return -ENODEV; */
		}
		ivtv_udma_alloc(itv);
	}

	/* Allocate memory */
	item = kmalloc(sizeof(struct ivtv_open_id), GFP_KERNEL);
	if (NULL == item) {
		IVTV_DEBUG_WARN("nomem on v4l2 open\n");
		return -ENOMEM;
	}
	item->itv = itv;
	item->type = s->type;
	v4l2_prio_open(&itv->prio, &item->prio);

	item->open_id = itv->open_id++;
	filp->private_data = item;

	if (item->type == IVTV_ENC_STREAM_TYPE_RAD) {
		/* Try to claim this stream */
		if (ivtv_claim_stream(item, item->type)) {
			/* No, it's already in use */
			kfree(item);
			return -EBUSY;
		}

		if (!test_bit(IVTV_F_I_RADIO_USER, &itv->i_flags)) {
			if (atomic_read(&itv->capturing) > 0) {
				/* switching to radio while capture is
				   in progress is not polite */
				ivtv_release_stream(s);
				kfree(item);
				return -EBUSY;
			}
		}
		/* Mark that the radio is being used. */
		set_bit(IVTV_F_I_RADIO_USER, &itv->i_flags);
		/* We have the radio */
		ivtv_mute(itv);
		/* Switch tuner to radio */
		ivtv_call_i2c_clients(itv, AUDC_SET_RADIO, NULL);
		/* Select the correct audio input (i.e. radio tuner) */
		ivtv_audio_set_io(itv);
		if (itv->hw_flags & IVTV_HW_SAA711X)
		{
			struct v4l2_crystal_freq crystal_freq;
			crystal_freq.freq = SAA7115_FREQ_32_11_MHZ;
			crystal_freq.flags = SAA7115_FREQ_FL_APLL;
			ivtv_saa7115(itv, VIDIOC_INT_S_CRYSTAL_FREQ, &crystal_freq);
		}
		/* Done! Unmute and continue. */
		ivtv_unmute(itv);
	}

	/* YUV or MPG Decoding Mode? */
	if (s->type == IVTV_DEC_STREAM_TYPE_MPG)
		clear_bit(IVTV_F_I_DEC_YUV, &itv->i_flags);
	else if (s->type == IVTV_DEC_STREAM_TYPE_YUV)
		set_bit(IVTV_F_I_DEC_YUV, &itv->i_flags);
	return 0;
}

int ivtv_v4l2_open(struct inode *inode, struct file *filp)
{
	int res, x, y = 0;
	struct ivtv *itv = NULL;
	struct ivtv_stream *s = NULL;
	int minor = iminor(inode);

	/* Find which card this open was on */
	spin_lock(&ivtv_cards_lock);
	for (x = 0; itv == NULL && x < ivtv_cards_active; x++) {
		/* find out which stream this open was on */
		for (y = 0; y < IVTV_MAX_STREAMS; y++) {
			s = &ivtv_cards[x]->streams[y];
			if (s->v4l2dev && s->v4l2dev->minor == minor) {
				itv = ivtv_cards[x];
				break;
			}
		}
	}
	spin_unlock(&ivtv_cards_lock);

	if (itv == NULL) {
		/* Couldn't find a device registered
		   on that minor, shouldn't happen! */
		printk(KERN_WARNING "No ivtv device found on minor %d\n", minor);
		return -ENXIO;
	}

	mutex_lock(&itv->serialize_lock);
	if (ivtv_init_on_first_open(itv)) {
		IVTV_ERR("Failed to initialize on minor %d\n", minor);
		mutex_unlock(&itv->serialize_lock);
		return -ENXIO;
	}
	res = ivtv_serialized_open(s, filp);
	mutex_unlock(&itv->serialize_lock);
	return res;
}

void ivtv_mute(struct ivtv *itv)
{
	if (atomic_read(&itv->capturing))
		ivtv_vapi(itv, CX2341X_ENC_MUTE_AUDIO, 1, 1);
	IVTV_DEBUG_INFO("Mute\n");
}

void ivtv_unmute(struct ivtv *itv)
{
	if (atomic_read(&itv->capturing)) {
		ivtv_msleep_timeout(100, 0);
		ivtv_vapi(itv, CX2341X_ENC_MISC, 1, 12);
		ivtv_vapi(itv, CX2341X_ENC_MUTE_AUDIO, 1, 0);
	}
	IVTV_DEBUG_INFO("Unmute\n");
}
