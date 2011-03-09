/*
 *  cx18 file operation functions
 *
 *  Derived from ivtv-fileops.c
 *
 *  Copyright (C) 2007  Hans Verkuil <hverkuil@xs4all.nl>
 *  Copyright (C) 2008  Andy Walls <awalls@md.metrocast.net>
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

#include "cx18-driver.h"
#include "cx18-fileops.h"
#include "cx18-i2c.h"
#include "cx18-queue.h"
#include "cx18-vbi.h"
#include "cx18-audio.h"
#include "cx18-mailbox.h"
#include "cx18-scb.h"
#include "cx18-streams.h"
#include "cx18-controls.h"
#include "cx18-ioctl.h"
#include "cx18-cards.h"

/* This function tries to claim the stream for a specific file descriptor.
   If no one else is using this stream then the stream is claimed and
   associated VBI and IDX streams are also automatically claimed.
   Possible error returns: -EBUSY if someone else has claimed
   the stream or 0 on success. */
int cx18_claim_stream(struct cx18_open_id *id, int type)
{
	struct cx18 *cx = id->cx;
	struct cx18_stream *s = &cx->streams[type];
	struct cx18_stream *s_assoc;

	/* Nothing should ever try to directly claim the IDX stream */
	if (type == CX18_ENC_STREAM_TYPE_IDX) {
		CX18_WARN("MPEG Index stream cannot be claimed "
			  "directly, but something tried.\n");
		return -EINVAL;
	}

	if (test_and_set_bit(CX18_F_S_CLAIMED, &s->s_flags)) {
		/* someone already claimed this stream */
		if (s->id == id->open_id) {
			/* yes, this file descriptor did. So that's OK. */
			return 0;
		}
		if (s->id == -1 && type == CX18_ENC_STREAM_TYPE_VBI) {
			/* VBI is handled already internally, now also assign
			   the file descriptor to this stream for external
			   reading of the stream. */
			s->id = id->open_id;
			CX18_DEBUG_INFO("Start Read VBI\n");
			return 0;
		}
		/* someone else is using this stream already */
		CX18_DEBUG_INFO("Stream %d is busy\n", type);
		return -EBUSY;
	}
	s->id = id->open_id;

	/*
	 * CX18_ENC_STREAM_TYPE_MPG needs to claim:
	 * CX18_ENC_STREAM_TYPE_VBI, if VBI insertion is on for sliced VBI, or
	 * CX18_ENC_STREAM_TYPE_IDX, if VBI insertion is off for sliced VBI
	 * (We don't yet fix up MPEG Index entries for our inserted packets).
	 *
	 * For all other streams we're done.
	 */
	if (type != CX18_ENC_STREAM_TYPE_MPG)
		return 0;

	s_assoc = &cx->streams[CX18_ENC_STREAM_TYPE_IDX];
	if (cx->vbi.insert_mpeg && !cx18_raw_vbi(cx))
		s_assoc = &cx->streams[CX18_ENC_STREAM_TYPE_VBI];
	else if (!cx18_stream_enabled(s_assoc))
		return 0;

	set_bit(CX18_F_S_CLAIMED, &s_assoc->s_flags);

	/* mark that it is used internally */
	set_bit(CX18_F_S_INTERNAL_USE, &s_assoc->s_flags);
	return 0;
}
EXPORT_SYMBOL(cx18_claim_stream);

/* This function releases a previously claimed stream. It will take into
   account associated VBI streams. */
void cx18_release_stream(struct cx18_stream *s)
{
	struct cx18 *cx = s->cx;
	struct cx18_stream *s_assoc;

	s->id = -1;
	if (s->type == CX18_ENC_STREAM_TYPE_IDX) {
		/*
		 * The IDX stream is only used internally, and can
		 * only be indirectly unclaimed by unclaiming the MPG stream.
		 */
		return;
	}

	if (s->type == CX18_ENC_STREAM_TYPE_VBI &&
		test_bit(CX18_F_S_INTERNAL_USE, &s->s_flags)) {
		/* this stream is still in use internally */
		return;
	}
	if (!test_and_clear_bit(CX18_F_S_CLAIMED, &s->s_flags)) {
		CX18_DEBUG_WARN("Release stream %s not in use!\n", s->name);
		return;
	}

	cx18_flush_queues(s);

	/*
	 * CX18_ENC_STREAM_TYPE_MPG needs to release the
	 * CX18_ENC_STREAM_TYPE_VBI and/or CX18_ENC_STREAM_TYPE_IDX streams.
	 *
	 * For all other streams we're done.
	 */
	if (s->type != CX18_ENC_STREAM_TYPE_MPG)
		return;

	/* Unclaim the associated MPEG Index stream */
	s_assoc = &cx->streams[CX18_ENC_STREAM_TYPE_IDX];
	if (test_and_clear_bit(CX18_F_S_INTERNAL_USE, &s_assoc->s_flags)) {
		clear_bit(CX18_F_S_CLAIMED, &s_assoc->s_flags);
		cx18_flush_queues(s_assoc);
	}

	/* Unclaim the associated VBI stream */
	s_assoc = &cx->streams[CX18_ENC_STREAM_TYPE_VBI];
	if (test_and_clear_bit(CX18_F_S_INTERNAL_USE, &s_assoc->s_flags)) {
		if (s_assoc->id == -1) {
			/*
			 * The VBI stream is not still claimed by a file
			 * descriptor, so completely unclaim it.
			 */
			clear_bit(CX18_F_S_CLAIMED, &s_assoc->s_flags);
			cx18_flush_queues(s_assoc);
		}
	}
}
EXPORT_SYMBOL(cx18_release_stream);

static void cx18_dualwatch(struct cx18 *cx)
{
	struct v4l2_tuner vt;
	u32 new_bitmap;
	u32 new_stereo_mode;
	const u32 stereo_mask = 0x0300;
	const u32 dual = 0x0200;
	u32 h;

	new_stereo_mode = cx->params.audio_properties & stereo_mask;
	memset(&vt, 0, sizeof(vt));
	cx18_call_all(cx, tuner, g_tuner, &vt);
	if (vt.audmode == V4L2_TUNER_MODE_LANG1_LANG2 &&
			(vt.rxsubchans & V4L2_TUNER_SUB_LANG2))
		new_stereo_mode = dual;

	if (new_stereo_mode == cx->dualwatch_stereo_mode)
		return;

	new_bitmap = new_stereo_mode
			| (cx->params.audio_properties & ~stereo_mask);

	CX18_DEBUG_INFO("dualwatch: change stereo flag from 0x%x to 0x%x. "
			"new audio_bitmask=0x%ux\n",
			cx->dualwatch_stereo_mode, new_stereo_mode, new_bitmap);

	h = cx18_find_handle(cx);
	if (h == CX18_INVALID_TASK_HANDLE) {
		CX18_DEBUG_INFO("dualwatch: can't find valid task handle\n");
		return;
	}

	if (cx18_vapi(cx,
		      CX18_CPU_SET_AUDIO_PARAMETERS, 2, h, new_bitmap) == 0) {
		cx->dualwatch_stereo_mode = new_stereo_mode;
		return;
	}
	CX18_DEBUG_INFO("dualwatch: changing stereo flag failed\n");
}


static struct cx18_mdl *cx18_get_mdl(struct cx18_stream *s, int non_block,
				     int *err)
{
	struct cx18 *cx = s->cx;
	struct cx18_stream *s_vbi = &cx->streams[CX18_ENC_STREAM_TYPE_VBI];
	struct cx18_mdl *mdl;
	DEFINE_WAIT(wait);

	*err = 0;
	while (1) {
		if (s->type == CX18_ENC_STREAM_TYPE_MPG) {
			/* Process pending program updates and VBI data */
			if (time_after(jiffies, cx->dualwatch_jiffies + msecs_to_jiffies(1000))) {
				cx->dualwatch_jiffies = jiffies;
				cx18_dualwatch(cx);
			}
			if (test_bit(CX18_F_S_INTERNAL_USE, &s_vbi->s_flags) &&
			    !test_bit(CX18_F_S_APPL_IO, &s_vbi->s_flags)) {
				while ((mdl = cx18_dequeue(s_vbi,
							   &s_vbi->q_full))) {
					/* byteswap and process VBI data */
					cx18_process_vbi_data(cx, mdl,
							      s_vbi->type);
					cx18_stream_put_mdl_fw(s_vbi, mdl);
				}
			}
			mdl = &cx->vbi.sliced_mpeg_mdl;
			if (mdl->readpos != mdl->bytesused)
				return mdl;
		}

		/* do we have new data? */
		mdl = cx18_dequeue(s, &s->q_full);
		if (mdl) {
			if (!test_and_clear_bit(CX18_F_M_NEED_SWAP,
						&mdl->m_flags))
				return mdl;
			if (s->type == CX18_ENC_STREAM_TYPE_MPG)
				/* byteswap MPG data */
				cx18_mdl_swap(mdl);
			else {
				/* byteswap and process VBI data */
				cx18_process_vbi_data(cx, mdl, s->type);
			}
			return mdl;
		}

		/* return if end of stream */
		if (!test_bit(CX18_F_S_STREAMING, &s->s_flags)) {
			CX18_DEBUG_INFO("EOS %s\n", s->name);
			return NULL;
		}

		/* return if file was opened with O_NONBLOCK */
		if (non_block) {
			*err = -EAGAIN;
			return NULL;
		}

		/* wait for more data to arrive */
		prepare_to_wait(&s->waitq, &wait, TASK_INTERRUPTIBLE);
		/* New buffers might have become available before we were added
		   to the waitqueue */
		if (!atomic_read(&s->q_full.depth))
			schedule();
		finish_wait(&s->waitq, &wait);
		if (signal_pending(current)) {
			/* return if a signal was received */
			CX18_DEBUG_INFO("User stopped %s\n", s->name);
			*err = -EINTR;
			return NULL;
		}
	}
}

static void cx18_setup_sliced_vbi_mdl(struct cx18 *cx)
{
	struct cx18_mdl *mdl = &cx->vbi.sliced_mpeg_mdl;
	struct cx18_buffer *buf = &cx->vbi.sliced_mpeg_buf;
	int idx = cx->vbi.inserted_frame % CX18_VBI_FRAMES;

	buf->buf = cx->vbi.sliced_mpeg_data[idx];
	buf->bytesused = cx->vbi.sliced_mpeg_size[idx];
	buf->readpos = 0;

	mdl->curr_buf = NULL;
	mdl->bytesused = cx->vbi.sliced_mpeg_size[idx];
	mdl->readpos = 0;
}

static size_t cx18_copy_buf_to_user(struct cx18_stream *s,
	struct cx18_buffer *buf, char __user *ubuf, size_t ucount, bool *stop)
{
	struct cx18 *cx = s->cx;
	size_t len = buf->bytesused - buf->readpos;

	*stop = false;
	if (len > ucount)
		len = ucount;
	if (cx->vbi.insert_mpeg && s->type == CX18_ENC_STREAM_TYPE_MPG &&
	    !cx18_raw_vbi(cx) && buf != &cx->vbi.sliced_mpeg_buf) {
		/*
		 * Try to find a good splice point in the PS, just before
		 * an MPEG-2 Program Pack start code, and provide only
		 * up to that point to the user, so it's easy to insert VBI data
		 * the next time around.
		 *
		 * This will not work for an MPEG-2 TS and has only been
		 * verified by analysis to work for an MPEG-2 PS.  Helen Buus
		 * pointed out this works for the CX23416 MPEG-2 DVD compatible
		 * stream, and research indicates both the MPEG 2 SVCD and DVD
		 * stream types use an MPEG-2 PS container.
		 */
		/*
		 * An MPEG-2 Program Stream (PS) is a series of
		 * MPEG-2 Program Packs terminated by an
		 * MPEG Program End Code after the last Program Pack.
		 * A Program Pack may hold a PS System Header packet and any
		 * number of Program Elementary Stream (PES) Packets
		 */
		const char *start = buf->buf + buf->readpos;
		const char *p = start + 1;
		const u8 *q;
		u8 ch = cx->search_pack_header ? 0xba : 0xe0;
		int stuffing, i;

		while (start + len > p) {
			/* Scan for a 0 to find a potential MPEG-2 start code */
			q = memchr(p, 0, start + len - p);
			if (q == NULL)
				break;
			p = q + 1;
			/*
			 * Keep looking if not a
			 * MPEG-2 Pack header start code:  0x00 0x00 0x01 0xba
			 * or MPEG-2 video PES start code: 0x00 0x00 0x01 0xe0
			 */
			if ((char *)q + 15 >= buf->buf + buf->bytesused ||
			    q[1] != 0 || q[2] != 1 || q[3] != ch)
				continue;

			/* If expecting the primary video PES */
			if (!cx->search_pack_header) {
				/* Continue if it couldn't be a PES packet */
				if ((q[6] & 0xc0) != 0x80)
					continue;
				/* Check if a PTS or PTS & DTS follow */
				if (((q[7] & 0xc0) == 0x80 &&  /* PTS only */
				     (q[9] & 0xf0) == 0x20) || /* PTS only */
				    ((q[7] & 0xc0) == 0xc0 &&  /* PTS & DTS */
				     (q[9] & 0xf0) == 0x30)) { /* DTS follows */
					/* Assume we found the video PES hdr */
					ch = 0xba; /* next want a Program Pack*/
					cx->search_pack_header = 1;
					p = q + 9; /* Skip this video PES hdr */
				}
				continue;
			}

			/* We may have found a Program Pack start code */

			/* Get the count of stuffing bytes & verify them */
			stuffing = q[13] & 7;
			/* all stuffing bytes must be 0xff */
			for (i = 0; i < stuffing; i++)
				if (q[14 + i] != 0xff)
					break;
			if (i == stuffing && /* right number of stuffing bytes*/
			    (q[4] & 0xc4) == 0x44 && /* marker check */
			    (q[12] & 3) == 3 &&  /* marker check */
			    q[14 + stuffing] == 0 && /* PES Pack or Sys Hdr */
			    q[15 + stuffing] == 0 &&
			    q[16 + stuffing] == 1) {
				/* We declare we actually found a Program Pack*/
				cx->search_pack_header = 0; /* expect vid PES */
				len = (char *)q - start;
				cx18_setup_sliced_vbi_mdl(cx);
				*stop = true;
				break;
			}
		}
	}
	if (copy_to_user(ubuf, (u8 *)buf->buf + buf->readpos, len)) {
		CX18_DEBUG_WARN("copy %zd bytes to user failed for %s\n",
				len, s->name);
		return -EFAULT;
	}
	buf->readpos += len;
	if (s->type == CX18_ENC_STREAM_TYPE_MPG &&
	    buf != &cx->vbi.sliced_mpeg_buf)
		cx->mpg_data_received += len;
	return len;
}

static size_t cx18_copy_mdl_to_user(struct cx18_stream *s,
		struct cx18_mdl *mdl, char __user *ubuf, size_t ucount)
{
	size_t tot_written = 0;
	int rc;
	bool stop = false;

	if (mdl->curr_buf == NULL)
		mdl->curr_buf = list_first_entry(&mdl->buf_list,
						 struct cx18_buffer, list);

	if (list_entry_is_past_end(mdl->curr_buf, &mdl->buf_list, list)) {
		/*
		 * For some reason we've exhausted the buffers, but the MDL
		 * object still said some data was unread.
		 * Fix that and bail out.
		 */
		mdl->readpos = mdl->bytesused;
		return 0;
	}

	list_for_each_entry_from(mdl->curr_buf, &mdl->buf_list, list) {

		if (mdl->curr_buf->readpos >= mdl->curr_buf->bytesused)
			continue;

		rc = cx18_copy_buf_to_user(s, mdl->curr_buf, ubuf + tot_written,
					   ucount - tot_written, &stop);
		if (rc < 0)
			return rc;
		mdl->readpos += rc;
		tot_written += rc;

		if (stop ||	/* Forced stopping point for VBI insertion */
		    tot_written >= ucount ||	/* Reader request statisfied */
		    mdl->curr_buf->readpos < mdl->curr_buf->bytesused ||
		    mdl->readpos >= mdl->bytesused) /* MDL buffers drained */
			break;
	}
	return tot_written;
}

static ssize_t cx18_read(struct cx18_stream *s, char __user *ubuf,
		size_t tot_count, int non_block)
{
	struct cx18 *cx = s->cx;
	size_t tot_written = 0;
	int single_frame = 0;

	if (atomic_read(&cx->ana_capturing) == 0 && s->id == -1) {
		/* shouldn't happen */
		CX18_DEBUG_WARN("Stream %s not initialized before read\n",
				s->name);
		return -EIO;
	}

	/* Each VBI buffer is one frame, the v4l2 API says that for VBI the
	   frames should arrive one-by-one, so make sure we never output more
	   than one VBI frame at a time */
	if (s->type == CX18_ENC_STREAM_TYPE_VBI && !cx18_raw_vbi(cx))
		single_frame = 1;

	for (;;) {
		struct cx18_mdl *mdl;
		int rc;

		mdl = cx18_get_mdl(s, non_block, &rc);
		/* if there is no data available... */
		if (mdl == NULL) {
			/* if we got data, then return that regardless */
			if (tot_written)
				break;
			/* EOS condition */
			if (rc == 0) {
				clear_bit(CX18_F_S_STREAMOFF, &s->s_flags);
				clear_bit(CX18_F_S_APPL_IO, &s->s_flags);
				cx18_release_stream(s);
			}
			/* set errno */
			return rc;
		}

		rc = cx18_copy_mdl_to_user(s, mdl, ubuf + tot_written,
				tot_count - tot_written);

		if (mdl != &cx->vbi.sliced_mpeg_mdl) {
			if (mdl->readpos == mdl->bytesused)
				cx18_stream_put_mdl_fw(s, mdl);
			else
				cx18_push(s, mdl, &s->q_full);
		} else if (mdl->readpos == mdl->bytesused) {
			int idx = cx->vbi.inserted_frame % CX18_VBI_FRAMES;

			cx->vbi.sliced_mpeg_size[idx] = 0;
			cx->vbi.inserted_frame++;
			cx->vbi_data_inserted += mdl->bytesused;
		}
		if (rc < 0)
			return rc;
		tot_written += rc;

		if (tot_written == tot_count || single_frame)
			break;
	}
	return tot_written;
}

static ssize_t cx18_read_pos(struct cx18_stream *s, char __user *ubuf,
		size_t count, loff_t *pos, int non_block)
{
	ssize_t rc = count ? cx18_read(s, ubuf, count, non_block) : 0;
	struct cx18 *cx = s->cx;

	CX18_DEBUG_HI_FILE("read %zd from %s, got %zd\n", count, s->name, rc);
	if (rc > 0)
		pos += rc;
	return rc;
}

int cx18_start_capture(struct cx18_open_id *id)
{
	struct cx18 *cx = id->cx;
	struct cx18_stream *s = &cx->streams[id->type];
	struct cx18_stream *s_vbi;
	struct cx18_stream *s_idx;

	if (s->type == CX18_ENC_STREAM_TYPE_RAD) {
		/* you cannot read from these stream types. */
		return -EPERM;
	}

	/* Try to claim this stream. */
	if (cx18_claim_stream(id, s->type))
		return -EBUSY;

	/* If capture is already in progress, then we also have to
	   do nothing extra. */
	if (test_bit(CX18_F_S_STREAMOFF, &s->s_flags) ||
	    test_and_set_bit(CX18_F_S_STREAMING, &s->s_flags)) {
		set_bit(CX18_F_S_APPL_IO, &s->s_flags);
		return 0;
	}

	/* Start associated VBI or IDX stream capture if required */
	s_vbi = &cx->streams[CX18_ENC_STREAM_TYPE_VBI];
	s_idx = &cx->streams[CX18_ENC_STREAM_TYPE_IDX];
	if (s->type == CX18_ENC_STREAM_TYPE_MPG) {
		/*
		 * The VBI and IDX streams should have been claimed
		 * automatically, if for internal use, when the MPG stream was
		 * claimed.  We only need to start these streams capturing.
		 */
		if (test_bit(CX18_F_S_INTERNAL_USE, &s_idx->s_flags) &&
		    !test_and_set_bit(CX18_F_S_STREAMING, &s_idx->s_flags)) {
			if (cx18_start_v4l2_encode_stream(s_idx)) {
				CX18_DEBUG_WARN("IDX capture start failed\n");
				clear_bit(CX18_F_S_STREAMING, &s_idx->s_flags);
				goto start_failed;
			}
			CX18_DEBUG_INFO("IDX capture started\n");
		}
		if (test_bit(CX18_F_S_INTERNAL_USE, &s_vbi->s_flags) &&
		    !test_and_set_bit(CX18_F_S_STREAMING, &s_vbi->s_flags)) {
			if (cx18_start_v4l2_encode_stream(s_vbi)) {
				CX18_DEBUG_WARN("VBI capture start failed\n");
				clear_bit(CX18_F_S_STREAMING, &s_vbi->s_flags);
				goto start_failed;
			}
			CX18_DEBUG_INFO("VBI insertion started\n");
		}
	}

	/* Tell the card to start capturing */
	if (!cx18_start_v4l2_encode_stream(s)) {
		/* We're done */
		set_bit(CX18_F_S_APPL_IO, &s->s_flags);
		/* Resume a possibly paused encoder */
		if (test_and_clear_bit(CX18_F_I_ENC_PAUSED, &cx->i_flags))
			cx18_vapi(cx, CX18_CPU_CAPTURE_PAUSE, 1, s->handle);
		return 0;
	}

start_failed:
	CX18_DEBUG_WARN("Failed to start capturing for stream %s\n", s->name);

	/*
	 * The associated VBI and IDX streams for internal use are released
	 * automatically when the MPG stream is released.  We only need to stop
	 * the associated stream.
	 */
	if (s->type == CX18_ENC_STREAM_TYPE_MPG) {
		/* Stop the IDX stream which is always for internal use */
		if (test_bit(CX18_F_S_STREAMING, &s_idx->s_flags)) {
			cx18_stop_v4l2_encode_stream(s_idx, 0);
			clear_bit(CX18_F_S_STREAMING, &s_idx->s_flags);
		}
		/* Stop the VBI stream, if only running for internal use */
		if (test_bit(CX18_F_S_STREAMING, &s_vbi->s_flags) &&
		    !test_bit(CX18_F_S_APPL_IO, &s_vbi->s_flags)) {
			cx18_stop_v4l2_encode_stream(s_vbi, 0);
			clear_bit(CX18_F_S_STREAMING, &s_vbi->s_flags);
		}
	}
	clear_bit(CX18_F_S_STREAMING, &s->s_flags);
	cx18_release_stream(s); /* Also releases associated streams */
	return -EIO;
}

ssize_t cx18_v4l2_read(struct file *filp, char __user *buf, size_t count,
		loff_t *pos)
{
	struct cx18_open_id *id = filp->private_data;
	struct cx18 *cx = id->cx;
	struct cx18_stream *s = &cx->streams[id->type];
	int rc;

	CX18_DEBUG_HI_FILE("read %zd bytes from %s\n", count, s->name);

	mutex_lock(&cx->serialize_lock);
	rc = cx18_start_capture(id);
	mutex_unlock(&cx->serialize_lock);
	if (rc)
		return rc;
	return cx18_read_pos(s, buf, count, pos, filp->f_flags & O_NONBLOCK);
}

unsigned int cx18_v4l2_enc_poll(struct file *filp, poll_table *wait)
{
	struct cx18_open_id *id = filp->private_data;
	struct cx18 *cx = id->cx;
	struct cx18_stream *s = &cx->streams[id->type];
	int eof = test_bit(CX18_F_S_STREAMOFF, &s->s_flags);

	/* Start a capture if there is none */
	if (!eof && !test_bit(CX18_F_S_STREAMING, &s->s_flags)) {
		int rc;

		mutex_lock(&cx->serialize_lock);
		rc = cx18_start_capture(id);
		mutex_unlock(&cx->serialize_lock);
		if (rc) {
			CX18_DEBUG_INFO("Could not start capture for %s (%d)\n",
					s->name, rc);
			return POLLERR;
		}
		CX18_DEBUG_FILE("Encoder poll started capture\n");
	}

	/* add stream's waitq to the poll list */
	CX18_DEBUG_HI_FILE("Encoder poll\n");
	poll_wait(filp, &s->waitq, wait);

	if (atomic_read(&s->q_full.depth))
		return POLLIN | POLLRDNORM;
	if (eof)
		return POLLHUP;
	return 0;
}

void cx18_stop_capture(struct cx18_open_id *id, int gop_end)
{
	struct cx18 *cx = id->cx;
	struct cx18_stream *s = &cx->streams[id->type];
	struct cx18_stream *s_vbi = &cx->streams[CX18_ENC_STREAM_TYPE_VBI];
	struct cx18_stream *s_idx = &cx->streams[CX18_ENC_STREAM_TYPE_IDX];

	CX18_DEBUG_IOCTL("close() of %s\n", s->name);

	/* 'Unclaim' this stream */

	/* Stop capturing */
	if (test_bit(CX18_F_S_STREAMING, &s->s_flags)) {
		CX18_DEBUG_INFO("close stopping capture\n");
		if (id->type == CX18_ENC_STREAM_TYPE_MPG) {
			/* Stop internal use associated VBI and IDX streams */
			if (test_bit(CX18_F_S_STREAMING, &s_vbi->s_flags) &&
			    !test_bit(CX18_F_S_APPL_IO, &s_vbi->s_flags)) {
				CX18_DEBUG_INFO("close stopping embedded VBI "
						"capture\n");
				cx18_stop_v4l2_encode_stream(s_vbi, 0);
			}
			if (test_bit(CX18_F_S_STREAMING, &s_idx->s_flags)) {
				CX18_DEBUG_INFO("close stopping IDX capture\n");
				cx18_stop_v4l2_encode_stream(s_idx, 0);
			}
		}
		if (id->type == CX18_ENC_STREAM_TYPE_VBI &&
		    test_bit(CX18_F_S_INTERNAL_USE, &s->s_flags))
			/* Also used internally, don't stop capturing */
			s->id = -1;
		else
			cx18_stop_v4l2_encode_stream(s, gop_end);
	}
	if (!gop_end) {
		clear_bit(CX18_F_S_APPL_IO, &s->s_flags);
		clear_bit(CX18_F_S_STREAMOFF, &s->s_flags);
		cx18_release_stream(s);
	}
}

int cx18_v4l2_close(struct file *filp)
{
	struct cx18_open_id *id = filp->private_data;
	struct cx18 *cx = id->cx;
	struct cx18_stream *s = &cx->streams[id->type];

	CX18_DEBUG_IOCTL("close() of %s\n", s->name);

	v4l2_prio_close(&cx->prio, id->prio);

	/* Easy case first: this stream was never claimed by us */
	if (s->id != id->open_id) {
		kfree(id);
		return 0;
	}

	/* 'Unclaim' this stream */

	/* Stop radio */
	mutex_lock(&cx->serialize_lock);
	if (id->type == CX18_ENC_STREAM_TYPE_RAD) {
		/* Closing radio device, return to TV mode */
		cx18_mute(cx);
		/* Mark that the radio is no longer in use */
		clear_bit(CX18_F_I_RADIO_USER, &cx->i_flags);
		/* Switch tuner to TV */
		cx18_call_all(cx, core, s_std, cx->std);
		/* Select correct audio input (i.e. TV tuner or Line in) */
		cx18_audio_set_io(cx);
		if (atomic_read(&cx->ana_capturing) > 0) {
			/* Undo video mute */
			cx18_vapi(cx, CX18_CPU_SET_VIDEO_MUTE, 2, s->handle,
				cx->params.video_mute |
					(cx->params.video_mute_yuv << 8));
		}
		/* Done! Unmute and continue. */
		cx18_unmute(cx);
		cx18_release_stream(s);
	} else {
		cx18_stop_capture(id, 0);
	}
	kfree(id);
	mutex_unlock(&cx->serialize_lock);
	return 0;
}

static int cx18_serialized_open(struct cx18_stream *s, struct file *filp)
{
	struct cx18 *cx = s->cx;
	struct cx18_open_id *item;

	CX18_DEBUG_FILE("open %s\n", s->name);

	/* Allocate memory */
	item = kmalloc(sizeof(struct cx18_open_id), GFP_KERNEL);
	if (NULL == item) {
		CX18_DEBUG_WARN("nomem on v4l2 open\n");
		return -ENOMEM;
	}
	item->cx = cx;
	item->type = s->type;
	v4l2_prio_open(&cx->prio, &item->prio);

	item->open_id = cx->open_id++;
	filp->private_data = item;

	if (item->type == CX18_ENC_STREAM_TYPE_RAD) {
		/* Try to claim this stream */
		if (cx18_claim_stream(item, item->type)) {
			/* No, it's already in use */
			kfree(item);
			return -EBUSY;
		}

		if (!test_bit(CX18_F_I_RADIO_USER, &cx->i_flags)) {
			if (atomic_read(&cx->ana_capturing) > 0) {
				/* switching to radio while capture is
				   in progress is not polite */
				cx18_release_stream(s);
				kfree(item);
				return -EBUSY;
			}
		}

		/* Mark that the radio is being used. */
		set_bit(CX18_F_I_RADIO_USER, &cx->i_flags);
		/* We have the radio */
		cx18_mute(cx);
		/* Switch tuner to radio */
		cx18_call_all(cx, tuner, s_radio);
		/* Select the correct audio input (i.e. radio tuner) */
		cx18_audio_set_io(cx);
		/* Done! Unmute and continue. */
		cx18_unmute(cx);
	}
	return 0;
}

int cx18_v4l2_open(struct file *filp)
{
	int res;
	struct video_device *video_dev = video_devdata(filp);
	struct cx18_stream *s = video_get_drvdata(video_dev);
	struct cx18 *cx = s->cx;

	mutex_lock(&cx->serialize_lock);
	if (cx18_init_on_first_open(cx)) {
		CX18_ERR("Failed to initialize on %s\n",
			 video_device_node_name(video_dev));
		mutex_unlock(&cx->serialize_lock);
		return -ENXIO;
	}
	res = cx18_serialized_open(s, filp);
	mutex_unlock(&cx->serialize_lock);
	return res;
}

void cx18_mute(struct cx18 *cx)
{
	u32 h;
	if (atomic_read(&cx->ana_capturing)) {
		h = cx18_find_handle(cx);
		if (h != CX18_INVALID_TASK_HANDLE)
			cx18_vapi(cx, CX18_CPU_SET_AUDIO_MUTE, 2, h, 1);
		else
			CX18_ERR("Can't find valid task handle for mute\n");
	}
	CX18_DEBUG_INFO("Mute\n");
}

void cx18_unmute(struct cx18 *cx)
{
	u32 h;
	if (atomic_read(&cx->ana_capturing)) {
		h = cx18_find_handle(cx);
		if (h != CX18_INVALID_TASK_HANDLE) {
			cx18_msleep_timeout(100, 0);
			cx18_vapi(cx, CX18_CPU_SET_MISC_PARAMETERS, 2, h, 12);
			cx18_vapi(cx, CX18_CPU_SET_AUDIO_MUTE, 2, h, 0);
		} else
			CX18_ERR("Can't find valid task handle for unmute\n");
	}
	CX18_DEBUG_INFO("Unmute\n");
}
