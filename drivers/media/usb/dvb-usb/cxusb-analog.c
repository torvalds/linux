// SPDX-License-Identifier: GPL-2.0+
//
// DVB USB compliant linux driver for Conexant USB reference design -
// (analog part).
//
// Copyright (C) 2011, 2017, 2018
//	Maciej S. Szmigiero (mail@maciej.szmigiero.name)
//
// In case there are new analog / DVB-T hybrid devices released in the market
// using the same general design as Medion MD95700: a CX25840 video decoder
// outputting a BT.656 stream to a USB bridge chip which then forwards it to
// the host in isochronous USB packets this code should be made generic, with
// board specific bits implemented via separate card structures.
//
// This is, however, unlikely as the Medion model was released
// years ago (in 2005).
//
// TODO:
//  * audio support,
//  * finish radio support (requires audio of course),
//  * VBI support,
//  * controls support

#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/ktime.h>
#include <linux/vmalloc.h>
#include <media/drv-intf/cx25840.h>
#include <media/tuner.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-vmalloc.h>

#include "cxusb.h"

static int cxusb_medion_v_queue_setup(struct vb2_queue *q,
				      unsigned int *num_buffers,
				      unsigned int *num_planes,
				      unsigned int sizes[],
				      struct device *alloc_devs[])
{
	struct dvb_usb_device *dvbdev = vb2_get_drv_priv(q);
	struct cxusb_medion_dev *cxdev = dvbdev->priv;
	unsigned int size = cxdev->width * cxdev->height * 2;

	if (*num_planes > 0) {
		if (*num_planes != 1)
			return -EINVAL;

		if (sizes[0] < size)
			return -EINVAL;
	} else {
		*num_planes = 1;
		sizes[0] = size;
	}

	return 0;
}

static int cxusb_medion_v_buf_init(struct vb2_buffer *vb)
{
	struct dvb_usb_device *dvbdev = vb2_get_drv_priv(vb->vb2_queue);
	struct cxusb_medion_dev *cxdev = dvbdev->priv;

	cxusb_vprintk(dvbdev, OPS, "buffer init\n");

	if (vb2_plane_size(vb, 0) < cxdev->width * cxdev->height * 2)
		return -ENOMEM;

	cxusb_vprintk(dvbdev, OPS, "buffer OK\n");

	return 0;
}

static void cxusb_auxbuf_init(struct dvb_usb_device *dvbdev,
			      struct cxusb_medion_auxbuf *auxbuf,
			      u8 *buf, unsigned int len)
{
	cxusb_vprintk(dvbdev, AUXB, "initializing auxbuf of len %u\n", len);

	auxbuf->buf = buf;
	auxbuf->len = len;
	auxbuf->paylen = 0;
}

static void cxusb_auxbuf_head_trim(struct dvb_usb_device *dvbdev,
				   struct cxusb_medion_auxbuf *auxbuf,
				   unsigned int pos)
{
	if (pos == 0)
		return;

	if (WARN_ON(pos > auxbuf->paylen))
		return;

	cxusb_vprintk(dvbdev, AUXB,
		      "trimming auxbuf len by %u to %u\n",
		      pos, auxbuf->paylen - pos);

	memmove(auxbuf->buf, auxbuf->buf + pos, auxbuf->paylen - pos);
	auxbuf->paylen -= pos;
}

static unsigned int cxusb_auxbuf_paylen(struct cxusb_medion_auxbuf *auxbuf)
{
	return auxbuf->paylen;
}

static bool cxusb_auxbuf_make_space(struct dvb_usb_device *dvbdev,
				    struct cxusb_medion_auxbuf *auxbuf,
				    unsigned int howmuch)
{
	unsigned int freespace;

	if (WARN_ON(howmuch >= auxbuf->len))
		howmuch = auxbuf->len - 1;

	freespace = auxbuf->len - cxusb_auxbuf_paylen(auxbuf);

	cxusb_vprintk(dvbdev, AUXB, "freespace is %u\n", freespace);

	if (freespace >= howmuch)
		return true;

	howmuch -= freespace;

	cxusb_vprintk(dvbdev, AUXB, "will overwrite %u bytes of buffer\n",
		      howmuch);

	cxusb_auxbuf_head_trim(dvbdev, auxbuf, howmuch);

	return false;
}

/* returns false if some data was overwritten */
static bool cxusb_auxbuf_append_urb(struct dvb_usb_device *dvbdev,
				    struct cxusb_medion_auxbuf *auxbuf,
				    struct urb *urb)
{
	unsigned long len;
	int i;
	bool ret;

	for (i = 0, len = 0; i < urb->number_of_packets; i++)
		len += urb->iso_frame_desc[i].actual_length;

	ret = cxusb_auxbuf_make_space(dvbdev, auxbuf, len);

	for (i = 0; i < urb->number_of_packets; i++) {
		unsigned int to_copy;

		to_copy = urb->iso_frame_desc[i].actual_length;

		memcpy(auxbuf->buf + auxbuf->paylen, urb->transfer_buffer +
		       urb->iso_frame_desc[i].offset, to_copy);

		auxbuf->paylen += to_copy;
	}

	return ret;
}

static bool cxusb_auxbuf_copy(struct cxusb_medion_auxbuf *auxbuf,
			      unsigned int pos, unsigned char *dest,
			      unsigned int len)
{
	if (pos + len > auxbuf->paylen)
		return false;

	memcpy(dest, auxbuf->buf + pos, len);

	return true;
}

static bool cxusb_medion_cf_refc_fld_chg(struct dvb_usb_device *dvbdev,
					 struct cxusb_bt656_params *bt656,
					 bool firstfield,
					 unsigned int maxlines,
					 unsigned int maxlinesamples,
					 unsigned char buf[4])
{
	bool firstfield_code = (buf[3] & CXUSB_BT656_FIELD_MASK) ==
		CXUSB_BT656_FIELD_1;
	unsigned int remlines;

	if (bt656->line == 0 || firstfield == firstfield_code)
		return false;

	if (bt656->fmode == LINE_SAMPLES) {
		unsigned int remsamples = maxlinesamples -
			bt656->linesamples;

		cxusb_vprintk(dvbdev, BT656,
			      "field %c after line %u field change\n",
			      firstfield ? '1' : '2', bt656->line);

		if (bt656->buf && remsamples > 0) {
			memset(bt656->buf, 0, remsamples);
			bt656->buf += remsamples;

			cxusb_vprintk(dvbdev, BT656,
				      "field %c line %u %u samples still remaining (of %u)\n",
				      firstfield ? '1' : '2',
				      bt656->line, remsamples,
				      maxlinesamples);
		}

		bt656->line++;
	}

	remlines = maxlines - bt656->line;
	if (bt656->buf && remlines > 0) {
		memset(bt656->buf, 0, remlines * maxlinesamples);
		bt656->buf += remlines * maxlinesamples;

		cxusb_vprintk(dvbdev, BT656,
			      "field %c %u lines still remaining (of %u)\n",
			      firstfield ? '1' : '2', remlines,
			      maxlines);
	}

	return true;
}

static void cxusb_medion_cf_refc_start_sch(struct dvb_usb_device *dvbdev,
					   struct cxusb_bt656_params *bt656,
					   bool firstfield,
					   unsigned char buf[4])
{
	bool firstfield_code = (buf[3] & CXUSB_BT656_FIELD_MASK) ==
		CXUSB_BT656_FIELD_1;
	bool sav_code = (buf[3] & CXUSB_BT656_SEAV_MASK) ==
		CXUSB_BT656_SEAV_SAV;
	bool vbi_code = (buf[3] & CXUSB_BT656_VBI_MASK) ==
		CXUSB_BT656_VBI_ON;

	if (!sav_code || firstfield != firstfield_code)
		return;

	if (!vbi_code) {
		cxusb_vprintk(dvbdev, BT656, "line start @ pos %u\n",
			      bt656->pos);

		bt656->linesamples = 0;
		bt656->fmode = LINE_SAMPLES;
	} else {
		cxusb_vprintk(dvbdev, BT656, "VBI start @ pos %u\n",
			      bt656->pos);

		bt656->fmode = VBI_SAMPLES;
	}
}

static void cxusb_medion_cf_refc_line_smpl(struct dvb_usb_device *dvbdev,
					   struct cxusb_bt656_params *bt656,
					   bool firstfield,
					   unsigned int maxlinesamples,
					   unsigned char buf[4])
{
	bool sav_code = (buf[3] & CXUSB_BT656_SEAV_MASK) ==
		CXUSB_BT656_SEAV_SAV;
	unsigned int remsamples;

	if (sav_code)
		cxusb_vprintk(dvbdev, BT656,
			      "SAV in line samples @ line %u, pos %u\n",
			      bt656->line, bt656->pos);

	remsamples = maxlinesamples - bt656->linesamples;
	if (bt656->buf && remsamples > 0) {
		memset(bt656->buf, 0, remsamples);
		bt656->buf += remsamples;

		cxusb_vprintk(dvbdev, BT656,
			      "field %c line %u %u samples still remaining (of %u)\n",
			      firstfield ? '1' : '2', bt656->line, remsamples,
			      maxlinesamples);
	}

	bt656->fmode = START_SEARCH;
	bt656->line++;
}

static void cxusb_medion_cf_refc_vbi_smpl(struct dvb_usb_device *dvbdev,
					  struct cxusb_bt656_params *bt656,
					  unsigned char buf[4])
{
	bool sav_code = (buf[3] & CXUSB_BT656_SEAV_MASK) ==
		CXUSB_BT656_SEAV_SAV;

	if (sav_code)
		cxusb_vprintk(dvbdev, BT656, "SAV in VBI samples @ pos %u\n",
			      bt656->pos);

	bt656->fmode = START_SEARCH;
}

/* returns whether the whole 4-byte code should be skipped in the buffer */
static bool cxusb_medion_cf_ref_code(struct dvb_usb_device *dvbdev,
				     struct cxusb_bt656_params *bt656,
				     bool firstfield,
				     unsigned int maxlines,
				     unsigned int maxlinesamples,
				     unsigned char buf[4])
{
	if (bt656->fmode == START_SEARCH) {
		cxusb_medion_cf_refc_start_sch(dvbdev, bt656, firstfield, buf);
	} else if (bt656->fmode == LINE_SAMPLES) {
		cxusb_medion_cf_refc_line_smpl(dvbdev, bt656, firstfield,
					       maxlinesamples, buf);
		return false;
	} else if (bt656->fmode == VBI_SAMPLES) {
		cxusb_medion_cf_refc_vbi_smpl(dvbdev, bt656, buf);
		return false;
	}

	return true;
}

static bool cxusb_medion_cs_start_sch(struct dvb_usb_device *dvbdev,
				      struct cxusb_medion_auxbuf *auxbuf,
				      struct cxusb_bt656_params *bt656,
				      unsigned int maxlinesamples)
{
	unsigned char buf[64];
	unsigned int idx;
	unsigned int tocheck = clamp_t(size_t, maxlinesamples / 4, 3,
				       sizeof(buf));

	if (!cxusb_auxbuf_copy(auxbuf, bt656->pos + 1, buf, tocheck))
		return false;

	for (idx = 0; idx <= tocheck - 3; idx++)
		if (memcmp(buf + idx, CXUSB_BT656_PREAMBLE, 3) == 0) {
			bt656->pos += (1 + idx);
			return true;
		}

	cxusb_vprintk(dvbdev, BT656, "line %u early start, pos %u\n",
		      bt656->line, bt656->pos);

	bt656->linesamples = 0;
	bt656->fmode = LINE_SAMPLES;

	return true;
}

static void cxusb_medion_cs_line_smpl(struct cxusb_bt656_params *bt656,
				      unsigned int maxlinesamples,
				      unsigned char val)
{
	if (bt656->buf)
		*(bt656->buf++) = val;

	bt656->linesamples++;
	bt656->pos++;

	if (bt656->linesamples >= maxlinesamples) {
		bt656->fmode = START_SEARCH;
		bt656->line++;
	}
}

static bool cxusb_medion_copy_samples(struct dvb_usb_device *dvbdev,
				      struct cxusb_medion_auxbuf *auxbuf,
				      struct cxusb_bt656_params *bt656,
				      unsigned int maxlinesamples,
				      unsigned char val)
{
	if (bt656->fmode == START_SEARCH && bt656->line > 0)
		return cxusb_medion_cs_start_sch(dvbdev, auxbuf, bt656,
						 maxlinesamples);
	else if (bt656->fmode == LINE_SAMPLES)
		cxusb_medion_cs_line_smpl(bt656, maxlinesamples, val);
	else /* TODO: copy VBI samples */
		bt656->pos++;

	return true;
}

static bool cxusb_medion_copy_field(struct dvb_usb_device *dvbdev,
				    struct cxusb_medion_auxbuf *auxbuf,
				    struct cxusb_bt656_params *bt656,
				    bool firstfield,
				    unsigned int maxlines,
				    unsigned int maxlinesmpls)
{
	while (bt656->line < maxlines) {
		unsigned char val;

		if (!cxusb_auxbuf_copy(auxbuf, bt656->pos, &val, 1))
			break;

		if (val == CXUSB_BT656_PREAMBLE[0]) {
			unsigned char buf[4];

			buf[0] = val;
			if (!cxusb_auxbuf_copy(auxbuf, bt656->pos + 1,
					       buf + 1, 3))
				break;

			if (buf[1] == CXUSB_BT656_PREAMBLE[1] &&
			    buf[2] == CXUSB_BT656_PREAMBLE[2]) {
				/*
				 * is this a field change?
				 * if so, terminate copying the current field
				 */
				if (cxusb_medion_cf_refc_fld_chg(dvbdev,
								 bt656,
								 firstfield,
								 maxlines,
								 maxlinesmpls,
								 buf))
					return true;

				if (cxusb_medion_cf_ref_code(dvbdev, bt656,
							     firstfield,
							     maxlines,
							     maxlinesmpls,
							     buf))
					bt656->pos += 4;

				continue;
			}
		}

		if (!cxusb_medion_copy_samples(dvbdev, auxbuf, bt656,
					       maxlinesmpls, val))
			break;
	}

	if (bt656->line < maxlines) {
		cxusb_vprintk(dvbdev, BT656,
			      "end of buffer pos = %u, line = %u\n",
			      bt656->pos, bt656->line);
		return false;
	}

	return true;
}

static bool cxusb_medion_v_process_auxbuf(struct cxusb_medion_dev *cxdev,
					  bool reset)
{
	struct dvb_usb_device *dvbdev = cxdev->dvbdev;
	struct cxusb_bt656_params *bt656 = &cxdev->bt656;

	/*
	 * if this is a new frame
	 * fetch a buffer from list
	 */
	if (bt656->mode == NEW_FRAME) {
		if (!list_empty(&cxdev->buflist)) {
			cxdev->vbuf =
				list_first_entry(&cxdev->buflist,
						 struct cxusb_medion_vbuffer,
						 list);
			list_del(&cxdev->vbuf->list);
		} else {
			dev_warn(&dvbdev->udev->dev, "no free buffers\n");
		}
	}

	if (bt656->mode == NEW_FRAME || reset) {
		cxusb_vprintk(dvbdev, URB, "will copy field 1\n");
		bt656->pos = 0;
		bt656->mode = FIRST_FIELD;
		bt656->fmode = START_SEARCH;
		bt656->line = 0;

		if (cxdev->vbuf) {
			cxdev->vbuf->vb2.vb2_buf.timestamp = ktime_get_ns();
			bt656->buf = vb2_plane_vaddr(&cxdev->vbuf->vb2.vb2_buf,
						     0);
		}
	}

	if (bt656->mode == FIRST_FIELD) {
		if (!cxusb_medion_copy_field(dvbdev, &cxdev->auxbuf, bt656,
					     true, cxdev->height / 2,
					     cxdev->width * 2))
			return false;

		/*
		 * do not trim buffer there in case
		 * we need to reset the search later
		 */

		cxusb_vprintk(dvbdev, URB, "will copy field 2\n");
		bt656->mode = SECOND_FIELD;
		bt656->fmode = START_SEARCH;
		bt656->line = 0;
	}

	if (bt656->mode == SECOND_FIELD) {
		if (!cxusb_medion_copy_field(dvbdev, &cxdev->auxbuf, bt656,
					     false, cxdev->height / 2,
					     cxdev->width * 2))
			return false;

		cxusb_auxbuf_head_trim(dvbdev, &cxdev->auxbuf, bt656->pos);

		bt656->mode = NEW_FRAME;

		if (cxdev->vbuf) {
			vb2_set_plane_payload(&cxdev->vbuf->vb2.vb2_buf, 0,
					      cxdev->width * cxdev->height * 2);

			cxdev->vbuf->vb2.field = cxdev->field_order;
			cxdev->vbuf->vb2.sequence = cxdev->vbuf_sequence++;

			vb2_buffer_done(&cxdev->vbuf->vb2.vb2_buf,
					VB2_BUF_STATE_DONE);

			cxdev->vbuf = NULL;
			cxdev->bt656.buf = NULL;

			cxusb_vprintk(dvbdev, URB, "frame done\n");
		} else {
			cxusb_vprintk(dvbdev, URB, "frame skipped\n");
			cxdev->vbuf_sequence++;
		}
	}

	return true;
}

static bool cxusb_medion_v_complete_handle_urb(struct cxusb_medion_dev *cxdev,
					       bool *auxbuf_reset)
{
	struct dvb_usb_device *dvbdev = cxdev->dvbdev;
	unsigned int urbn;
	struct urb *urb;
	int ret;

	*auxbuf_reset = false;

	urbn = cxdev->nexturb;
	if (!test_bit(urbn, &cxdev->urbcomplete))
		return false;

	clear_bit(urbn, &cxdev->urbcomplete);

	do {
		cxdev->nexturb++;
		cxdev->nexturb %= CXUSB_VIDEO_URBS;
		urb = cxdev->streamurbs[cxdev->nexturb];
	} while (!urb);

	urb = cxdev->streamurbs[urbn];
	cxusb_vprintk(dvbdev, URB, "URB %u status = %d\n", urbn, urb->status);

	if (urb->status == 0 || urb->status == -EXDEV) {
		int i;
		unsigned long len;

		for (i = 0, len = 0; i < urb->number_of_packets; i++)
			len += urb->iso_frame_desc[i].actual_length;

		cxusb_vprintk(dvbdev, URB, "URB %u data len = %lu\n", urbn,
			      len);

		if (len > 0) {
			cxusb_vprintk(dvbdev, URB, "appending URB\n");

			/*
			 * append new data to auxbuf while
			 * overwriting old data if necessary
			 *
			 * if any overwrite happens then we can no
			 * longer rely on consistency of the whole
			 * data so let's start again the current
			 * auxbuf frame assembling process from
			 * the beginning
			 */
			*auxbuf_reset =
				!cxusb_auxbuf_append_urb(dvbdev,
							 &cxdev->auxbuf,
							 urb);
		}
	}

	cxusb_vprintk(dvbdev, URB, "URB %u resubmit\n", urbn);

	ret = usb_submit_urb(urb, GFP_KERNEL);
	if (ret != 0)
		dev_err(&dvbdev->udev->dev,
			"unable to resubmit URB %u (%d), you'll have to restart streaming\n",
			urbn, ret);

	/* next URB is complete already? reschedule us then to handle it */
	return test_bit(cxdev->nexturb, &cxdev->urbcomplete);
}

static void cxusb_medion_v_complete_work(struct work_struct *work)
{
	struct cxusb_medion_dev *cxdev = container_of(work,
						      struct cxusb_medion_dev,
						      urbwork);
	struct dvb_usb_device *dvbdev = cxdev->dvbdev;
	bool auxbuf_reset;
	bool reschedule;

	mutex_lock(cxdev->videodev->lock);

	cxusb_vprintk(dvbdev, URB, "worker called, stop_streaming = %d\n",
		      (int)cxdev->stop_streaming);

	if (cxdev->stop_streaming)
		goto unlock;

	reschedule = cxusb_medion_v_complete_handle_urb(cxdev, &auxbuf_reset);

	if (cxusb_medion_v_process_auxbuf(cxdev, auxbuf_reset))
		/* reschedule us until auxbuf no longer can produce any frame */
		reschedule = true;

	if (reschedule) {
		cxusb_vprintk(dvbdev, URB, "rescheduling worker\n");
		schedule_work(&cxdev->urbwork);
	}

unlock:
	mutex_unlock(cxdev->videodev->lock);
}

static void cxusb_medion_v_complete(struct urb *u)
{
	struct dvb_usb_device *dvbdev = u->context;
	struct cxusb_medion_dev *cxdev = dvbdev->priv;
	unsigned int i;

	for (i = 0; i < CXUSB_VIDEO_URBS; i++)
		if (cxdev->streamurbs[i] == u)
			break;

	if (i >= CXUSB_VIDEO_URBS) {
		dev_err(&dvbdev->udev->dev,
			"complete on unknown URB\n");
		return;
	}

	cxusb_vprintk(dvbdev, URB, "URB %u complete\n", i);

	set_bit(i, &cxdev->urbcomplete);
	schedule_work(&cxdev->urbwork);
}

static void cxusb_medion_urbs_free(struct cxusb_medion_dev *cxdev)
{
	unsigned int i;

	for (i = 0; i < CXUSB_VIDEO_URBS; i++)
		if (cxdev->streamurbs[i]) {
			kfree(cxdev->streamurbs[i]->transfer_buffer);
			usb_free_urb(cxdev->streamurbs[i]);
			cxdev->streamurbs[i] = NULL;
		}
}

static void cxusb_medion_return_buffers(struct cxusb_medion_dev *cxdev,
					bool requeue)
{
	struct cxusb_medion_vbuffer *vbuf, *vbuf_tmp;

	list_for_each_entry_safe(vbuf, vbuf_tmp, &cxdev->buflist,
				 list) {
		list_del(&vbuf->list);
		vb2_buffer_done(&vbuf->vb2.vb2_buf,
				requeue ? VB2_BUF_STATE_QUEUED :
				VB2_BUF_STATE_ERROR);
	}

	if (cxdev->vbuf) {
		vb2_buffer_done(&cxdev->vbuf->vb2.vb2_buf,
				requeue ? VB2_BUF_STATE_QUEUED :
				VB2_BUF_STATE_ERROR);

		cxdev->vbuf = NULL;
		cxdev->bt656.buf = NULL;
	}
}

static int cxusb_medion_v_ss_auxbuf_alloc(struct cxusb_medion_dev *cxdev,
					  int *npackets)
{
	struct dvb_usb_device *dvbdev = cxdev->dvbdev;
	u8 *buf;
	unsigned int framelen, urblen, auxbuflen;

	framelen = (cxdev->width * 2 + 4 + 4) *
		(cxdev->height + 50 /* VBI lines */);

	/*
	 * try to fit a whole frame into each URB, as long as doing so
	 * does not require very high order memory allocations
	 */
	BUILD_BUG_ON(CXUSB_VIDEO_URB_MAX_SIZE / CXUSB_VIDEO_PKT_SIZE >
		     CXUSB_VIDEO_MAX_FRAME_PKTS);
	*npackets = min_t(int, (framelen + CXUSB_VIDEO_PKT_SIZE - 1) /
			  CXUSB_VIDEO_PKT_SIZE,
			  CXUSB_VIDEO_URB_MAX_SIZE / CXUSB_VIDEO_PKT_SIZE);
	urblen = *npackets * CXUSB_VIDEO_PKT_SIZE;

	cxusb_vprintk(dvbdev, URB,
		      "each URB will have %d packets for total of %u bytes (%u x %u @ %u)\n",
		      *npackets, urblen, (unsigned int)cxdev->width,
		      (unsigned int)cxdev->height, framelen);

	auxbuflen = framelen + urblen;

	buf = vmalloc(auxbuflen);
	if (!buf)
		return -ENOMEM;

	cxusb_auxbuf_init(dvbdev, &cxdev->auxbuf, buf, auxbuflen);

	return 0;
}

static u32 cxusb_medion_norm2field_order(v4l2_std_id norm)
{
	bool is625 = norm & V4L2_STD_625_50;
	bool is525 = norm & V4L2_STD_525_60;

	if (!is625 && !is525)
		return V4L2_FIELD_NONE;

	if (is625 && is525)
		return V4L2_FIELD_NONE;

	if (is625)
		return V4L2_FIELD_SEQ_TB;
	else /* is525 */
		return V4L2_FIELD_SEQ_BT;
}

static u32 cxusb_medion_field_order(struct cxusb_medion_dev *cxdev)
{
	struct dvb_usb_device *dvbdev = cxdev->dvbdev;
	u32 field;
	int ret;
	v4l2_std_id norm;

	/* TV tuner is PAL-only so it is always TB */
	if (cxdev->input == 0)
		return V4L2_FIELD_SEQ_TB;

	field = cxusb_medion_norm2field_order(cxdev->norm);
	if (field != V4L2_FIELD_NONE)
		return field;

	ret = v4l2_subdev_call(cxdev->cx25840, video, g_std, &norm);
	if (ret != 0) {
		cxusb_vprintk(dvbdev, OPS,
			      "cannot get current standard for input %u\n",
			      (unsigned int)cxdev->input);
	} else {
		field = cxusb_medion_norm2field_order(norm);
		if (field != V4L2_FIELD_NONE)
			return field;
	}

	dev_warn(&dvbdev->udev->dev,
		 "cannot determine field order for the current standard setup and received signal, using TB\n");
	return V4L2_FIELD_SEQ_TB;
}

static int cxusb_medion_v_start_streaming(struct vb2_queue *q,
					  unsigned int count)
{
	struct dvb_usb_device *dvbdev = vb2_get_drv_priv(q);
	struct cxusb_medion_dev *cxdev = dvbdev->priv;
	u8 streamon_params[2] = { 0x03, 0x00 };
	int npackets, i;
	int ret;

	cxusb_vprintk(dvbdev, OPS, "should start streaming\n");

	if (cxdev->stop_streaming) {
		/* stream is being stopped */
		ret = -EBUSY;
		goto ret_retbufs;
	}

	cxdev->field_order = cxusb_medion_field_order(cxdev);

	ret = v4l2_subdev_call(cxdev->cx25840, video, s_stream, 1);
	if (ret != 0) {
		dev_err(&dvbdev->udev->dev,
			"unable to start stream (%d)\n", ret);
		goto ret_retbufs;
	}

	ret = cxusb_ctrl_msg(dvbdev, CMD_STREAMING_ON, streamon_params, 2,
			     NULL, 0);
	if (ret != 0) {
		dev_err(&dvbdev->udev->dev,
			"unable to start streaming (%d)\n", ret);
		goto ret_unstream_cx;
	}

	ret = cxusb_medion_v_ss_auxbuf_alloc(cxdev, &npackets);
	if (ret != 0)
		goto ret_unstream_md;

	for (i = 0; i < CXUSB_VIDEO_URBS; i++) {
		int framen;
		u8 *streambuf;
		struct urb *surb;

		/*
		 * TODO: change this to an array of single pages to avoid
		 * doing a large continuous allocation when (if)
		 * s-g isochronous USB transfers are supported
		 */
		streambuf = kmalloc_array(npackets, CXUSB_VIDEO_PKT_SIZE,
					  GFP_KERNEL);
		if (!streambuf) {
			if (i < 2) {
				ret = -ENOMEM;
				goto ret_freeab;
			}
			break;
		}

		surb = usb_alloc_urb(npackets, GFP_KERNEL);
		if (!surb) {
			kfree(streambuf);
			ret = -ENOMEM;
			goto ret_freeu;
		}

		cxdev->streamurbs[i] = surb;
		surb->dev = dvbdev->udev;
		surb->context = dvbdev;
		surb->pipe = usb_rcvisocpipe(dvbdev->udev, 2);

		surb->interval = 1;
		surb->transfer_flags = URB_ISO_ASAP;

		surb->transfer_buffer = streambuf;

		surb->complete = cxusb_medion_v_complete;
		surb->number_of_packets = npackets;
		surb->transfer_buffer_length = npackets * CXUSB_VIDEO_PKT_SIZE;

		for (framen = 0; framen < npackets; framen++) {
			surb->iso_frame_desc[framen].offset =
				CXUSB_VIDEO_PKT_SIZE * framen;

			surb->iso_frame_desc[framen].length =
				CXUSB_VIDEO_PKT_SIZE;
		}
	}

	cxdev->urbcomplete = 0;
	cxdev->nexturb = 0;
	cxdev->vbuf_sequence = 0;

	cxdev->vbuf = NULL;
	cxdev->bt656.mode = NEW_FRAME;
	cxdev->bt656.buf = NULL;

	for (i = 0; i < CXUSB_VIDEO_URBS; i++)
		if (cxdev->streamurbs[i]) {
			ret = usb_submit_urb(cxdev->streamurbs[i],
					     GFP_KERNEL);
			if (ret != 0)
				dev_err(&dvbdev->udev->dev,
					"URB %d submission failed (%d)\n", i,
					ret);
		}

	return 0;

ret_freeu:
	cxusb_medion_urbs_free(cxdev);

ret_freeab:
	vfree(cxdev->auxbuf.buf);

ret_unstream_md:
	cxusb_ctrl_msg(dvbdev, CMD_STREAMING_OFF, NULL, 0, NULL, 0);

ret_unstream_cx:
	v4l2_subdev_call(cxdev->cx25840, video, s_stream, 0);

ret_retbufs:
	cxusb_medion_return_buffers(cxdev, true);

	return ret;
}

static void cxusb_medion_v_stop_streaming(struct vb2_queue *q)
{
	struct dvb_usb_device *dvbdev = vb2_get_drv_priv(q);
	struct cxusb_medion_dev *cxdev = dvbdev->priv;
	int ret;
	unsigned int i;

	cxusb_vprintk(dvbdev, OPS, "should stop streaming\n");

	if (WARN_ON(cxdev->stop_streaming))
		return;

	cxdev->stop_streaming = true;

	cxusb_ctrl_msg(dvbdev, CMD_STREAMING_OFF, NULL, 0, NULL, 0);

	ret = v4l2_subdev_call(cxdev->cx25840, video, s_stream, 0);
	if (ret != 0)
		dev_err(&dvbdev->udev->dev, "unable to stop stream (%d)\n",
			ret);

	/* let URB completion run */
	mutex_unlock(cxdev->videodev->lock);

	for (i = 0; i < CXUSB_VIDEO_URBS; i++)
		if (cxdev->streamurbs[i])
			usb_kill_urb(cxdev->streamurbs[i]);

	flush_work(&cxdev->urbwork);

	mutex_lock(cxdev->videodev->lock);

	/* free transfer buffer and URB */
	vfree(cxdev->auxbuf.buf);

	cxusb_medion_urbs_free(cxdev);

	cxusb_medion_return_buffers(cxdev, false);

	cxdev->stop_streaming = false;
}

static void cxusub_medion_v_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *v4l2buf = to_vb2_v4l2_buffer(vb);
	struct cxusb_medion_vbuffer *vbuf =
		container_of(v4l2buf, struct cxusb_medion_vbuffer, vb2);
	struct dvb_usb_device *dvbdev = vb2_get_drv_priv(vb->vb2_queue);
	struct cxusb_medion_dev *cxdev = dvbdev->priv;

	/* cxusb_vprintk(dvbdev, OPS, "mmmm.. a fresh buffer...\n"); */

	list_add_tail(&vbuf->list, &cxdev->buflist);
}

static const struct vb2_ops cxdev_video_qops = {
	.queue_setup = cxusb_medion_v_queue_setup,
	.buf_init = cxusb_medion_v_buf_init,
	.start_streaming = cxusb_medion_v_start_streaming,
	.stop_streaming = cxusb_medion_v_stop_streaming,
	.buf_queue = cxusub_medion_v_buf_queue,
};

static const __u32 videocaps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_TUNER |
	V4L2_CAP_READWRITE | V4L2_CAP_STREAMING;
static const __u32 radiocaps = V4L2_CAP_TUNER | V4L2_CAP_RADIO;

static int cxusb_medion_v_querycap(struct file *file, void *fh,
				   struct v4l2_capability *cap)
{
	struct dvb_usb_device *dvbdev = video_drvdata(file);

	strscpy(cap->driver, dvbdev->udev->dev.driver->name,
		sizeof(cap->driver));
	strscpy(cap->card, "Medion 95700", sizeof(cap->card));
	usb_make_path(dvbdev->udev, cap->bus_info, sizeof(cap->bus_info));

	cap->capabilities = videocaps | radiocaps | V4L2_CAP_DEVICE_CAPS;

	return 0;
}

static int cxusb_medion_v_enum_fmt_vid_cap(struct file *file, void *fh,
					   struct v4l2_fmtdesc *f)
{
	if (f->index != 0)
		return -EINVAL;

	f->pixelformat = V4L2_PIX_FMT_UYVY;

	return 0;
}

static int cxusb_medion_g_fmt_vid_cap(struct file *file, void *fh,
				      struct v4l2_format *f)
{
	struct dvb_usb_device *dvbdev = video_drvdata(file);
	struct cxusb_medion_dev *cxdev = dvbdev->priv;

	f->fmt.pix.width = cxdev->width;
	f->fmt.pix.height = cxdev->height;
	f->fmt.pix.pixelformat = V4L2_PIX_FMT_UYVY;
	f->fmt.pix.field = vb2_start_streaming_called(&cxdev->videoqueue) ?
		cxdev->field_order : cxusb_medion_field_order(cxdev);
	f->fmt.pix.bytesperline = cxdev->width * 2;
	f->fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;
	f->fmt.pix.sizeimage = f->fmt.pix.bytesperline * f->fmt.pix.height;

	return 0;
}

static int cxusb_medion_try_s_fmt_vid_cap(struct file *file,
					  struct v4l2_format *f,
					  bool isset)
{
	struct dvb_usb_device *dvbdev = video_drvdata(file);
	struct cxusb_medion_dev *cxdev = dvbdev->priv;
	struct v4l2_subdev_format subfmt = {
		.which = isset ? V4L2_SUBDEV_FORMAT_ACTIVE :
			 V4L2_SUBDEV_FORMAT_TRY,
	};
	u32 field;
	int ret;

	if (isset && vb2_is_busy(&cxdev->videoqueue))
		return -EBUSY;

	field = vb2_start_streaming_called(&cxdev->videoqueue) ?
		cxdev->field_order : cxusb_medion_field_order(cxdev);

	subfmt.format.width = f->fmt.pix.width & ~1;
	subfmt.format.height = f->fmt.pix.height & ~1;
	subfmt.format.code = MEDIA_BUS_FMT_FIXED;
	subfmt.format.field = field;
	subfmt.format.colorspace = V4L2_COLORSPACE_SMPTE170M;

	ret = v4l2_subdev_call(cxdev->cx25840, pad, set_fmt, NULL, &subfmt);
	if (ret != 0)
		return ret;

	f->fmt.pix.width = subfmt.format.width;
	f->fmt.pix.height = subfmt.format.height;
	f->fmt.pix.pixelformat = V4L2_PIX_FMT_UYVY;
	f->fmt.pix.field = field;
	f->fmt.pix.bytesperline = f->fmt.pix.width * 2;
	f->fmt.pix.sizeimage = f->fmt.pix.bytesperline * f->fmt.pix.height;
	f->fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;

	if (isset) {
		cxdev->width = f->fmt.pix.width;
		cxdev->height = f->fmt.pix.height;
	}

	return 0;
}

static int cxusb_medion_try_fmt_vid_cap(struct file *file, void *fh,
					struct v4l2_format *f)
{
	return cxusb_medion_try_s_fmt_vid_cap(file, f, false);
}

static int cxusb_medion_s_fmt_vid_cap(struct file *file, void *fh,
				      struct v4l2_format *f)
{
	return cxusb_medion_try_s_fmt_vid_cap(file, f, true);
}

static const struct {
	struct v4l2_input input;
	u32 inputcfg;
} cxusb_medion_inputs[] = {
	{ .input = { .name = "TV tuner", .type = V4L2_INPUT_TYPE_TUNER,
		     .tuner = 0, .std = V4L2_STD_PAL },
	  .inputcfg = CX25840_COMPOSITE2, },

	{  .input = { .name = "Composite", .type = V4L2_INPUT_TYPE_CAMERA,
		     .std = V4L2_STD_ALL },
	   .inputcfg = CX25840_COMPOSITE1, },

	{  .input = { .name = "S-Video", .type = V4L2_INPUT_TYPE_CAMERA,
		      .std = V4L2_STD_ALL },
	   .inputcfg = CX25840_SVIDEO_LUMA3 | CX25840_SVIDEO_CHROMA4 }
};

#define CXUSB_INPUT_CNT ARRAY_SIZE(cxusb_medion_inputs)

static int cxusb_medion_enum_input(struct file *file, void *fh,
				   struct v4l2_input *inp)
{
	struct dvb_usb_device *dvbdev = video_drvdata(file);
	struct cxusb_medion_dev *cxdev = dvbdev->priv;
	u32 index = inp->index;

	if (index >= CXUSB_INPUT_CNT)
		return -EINVAL;

	*inp = cxusb_medion_inputs[index].input;
	inp->index = index;
	inp->capabilities |= V4L2_IN_CAP_STD;

	if (index == cxdev->input) {
		int ret;
		u32 status = 0;

		ret = v4l2_subdev_call(cxdev->cx25840, video, g_input_status,
				       &status);
		if (ret != 0)
			dev_warn(&dvbdev->udev->dev,
				 "cx25840 input status query failed (%d)\n",
				 ret);
		else
			inp->status = status;
	}

	return 0;
}

static int cxusb_medion_g_input(struct file *file, void *fh,
				unsigned int *i)
{
	struct dvb_usb_device *dvbdev = video_drvdata(file);
	struct cxusb_medion_dev *cxdev = dvbdev->priv;

	*i = cxdev->input;

	return 0;
}

static int cxusb_medion_set_norm(struct cxusb_medion_dev *cxdev,
				 v4l2_std_id norm)
{
	struct dvb_usb_device *dvbdev = cxdev->dvbdev;
	int ret;

	cxusb_vprintk(dvbdev, OPS,
		      "trying to set standard for input %u to %lx\n",
		      (unsigned int)cxdev->input,
		      (unsigned long)norm);

	/* no autodetection support */
	if (norm == V4L2_STD_UNKNOWN)
		return -EINVAL;

	/* on composite or S-Video any std is acceptable */
	if (cxdev->input != 0) {
		ret = v4l2_subdev_call(cxdev->cx25840, video, s_std, norm);
		if (ret)
			return ret;

		goto ret_savenorm;
	}

	/* TV tuner is only able to demodulate PAL */
	if ((norm & ~V4L2_STD_PAL) != 0)
		return -EINVAL;

	ret = v4l2_subdev_call(cxdev->tda9887, video, s_std, norm);
	if (ret != 0) {
		dev_err(&dvbdev->udev->dev,
			"tda9887 norm setup failed (%d)\n",
			ret);
		return ret;
	}

	ret = v4l2_subdev_call(cxdev->tuner, video, s_std, norm);
	if (ret != 0) {
		dev_err(&dvbdev->udev->dev,
			"tuner norm setup failed (%d)\n",
			ret);
		return ret;
	}

	ret = v4l2_subdev_call(cxdev->cx25840, video, s_std, norm);
	if (ret != 0) {
		dev_err(&dvbdev->udev->dev,
			"cx25840 norm setup failed (%d)\n",
			ret);
		return ret;
	}

ret_savenorm:
	cxdev->norm = norm;

	return 0;
}

static int cxusb_medion_s_input(struct file *file, void *fh,
				unsigned int i)
{
	struct dvb_usb_device *dvbdev = video_drvdata(file);
	struct cxusb_medion_dev *cxdev = dvbdev->priv;
	int ret;
	v4l2_std_id norm;

	if (i >= CXUSB_INPUT_CNT)
		return -EINVAL;

	ret = v4l2_subdev_call(cxdev->cx25840, video, s_routing,
			       cxusb_medion_inputs[i].inputcfg, 0, 0);
	if (ret != 0)
		return ret;

	cxdev->input = i;
	cxdev->videodev->tvnorms = cxusb_medion_inputs[i].input.std;

	norm = cxdev->norm & cxusb_medion_inputs[i].input.std;
	if (norm == 0)
		norm = cxusb_medion_inputs[i].input.std;

	cxusb_medion_set_norm(cxdev, norm);

	return 0;
}

static int cxusb_medion_g_tuner(struct file *file, void *fh,
				struct v4l2_tuner *tuner)
{
	struct dvb_usb_device *dvbdev = video_drvdata(file);
	struct cxusb_medion_dev *cxdev = dvbdev->priv;
	struct video_device *vdev = video_devdata(file);
	int ret;

	if (tuner->index != 0)
		return -EINVAL;

	if (vdev->vfl_type == VFL_TYPE_VIDEO)
		tuner->type = V4L2_TUNER_ANALOG_TV;
	else
		tuner->type = V4L2_TUNER_RADIO;

	tuner->capability = 0;
	tuner->afc = 0;

	/*
	 * fills:
	 * always: capability (static), rangelow (static), rangehigh (static)
	 * radio mode: afc (may fail silently), rxsubchans (static), audmode
	 */
	ret = v4l2_subdev_call(cxdev->tda9887, tuner, g_tuner, tuner);
	if (ret != 0)
		return ret;

	/*
	 * fills:
	 * always: capability (static), rangelow (static), rangehigh (static)
	 * radio mode: rxsubchans (always stereo), audmode,
	 * signal (might be wrong)
	 */
	ret = v4l2_subdev_call(cxdev->tuner, tuner, g_tuner, tuner);
	if (ret != 0)
		return ret;

	tuner->signal = 0;

	/*
	 * fills: TV mode: capability, rxsubchans, audmode, signal
	 */
	ret = v4l2_subdev_call(cxdev->cx25840, tuner, g_tuner, tuner);
	if (ret != 0)
		return ret;

	if (vdev->vfl_type == VFL_TYPE_VIDEO)
		strscpy(tuner->name, "TV tuner", sizeof(tuner->name));
	else
		strscpy(tuner->name, "Radio tuner", sizeof(tuner->name));

	memset(tuner->reserved, 0, sizeof(tuner->reserved));

	return 0;
}

static int cxusb_medion_s_tuner(struct file *file, void *fh,
				const struct v4l2_tuner *tuner)
{
	struct dvb_usb_device *dvbdev = video_drvdata(file);
	struct cxusb_medion_dev *cxdev = dvbdev->priv;
	struct video_device *vdev = video_devdata(file);
	int ret;

	if (tuner->index != 0)
		return -EINVAL;

	ret = v4l2_subdev_call(cxdev->tda9887, tuner, s_tuner, tuner);
	if (ret != 0)
		return ret;

	ret = v4l2_subdev_call(cxdev->tuner, tuner, s_tuner, tuner);
	if (ret != 0)
		return ret;

	/*
	 * make sure that cx25840 is in a correct TV / radio mode,
	 * since calls above may have changed it for tuner / IF demod
	 */
	if (vdev->vfl_type == VFL_TYPE_VIDEO)
		v4l2_subdev_call(cxdev->cx25840, video, s_std, cxdev->norm);
	else
		v4l2_subdev_call(cxdev->cx25840, tuner, s_radio);

	return v4l2_subdev_call(cxdev->cx25840, tuner, s_tuner, tuner);
}

static int cxusb_medion_g_frequency(struct file *file, void *fh,
				    struct v4l2_frequency *freq)
{
	struct dvb_usb_device *dvbdev = video_drvdata(file);
	struct cxusb_medion_dev *cxdev = dvbdev->priv;

	if (freq->tuner != 0)
		return -EINVAL;

	return v4l2_subdev_call(cxdev->tuner, tuner, g_frequency, freq);
}

static int cxusb_medion_s_frequency(struct file *file, void *fh,
				    const struct v4l2_frequency *freq)
{
	struct dvb_usb_device *dvbdev = video_drvdata(file);
	struct cxusb_medion_dev *cxdev = dvbdev->priv;
	struct video_device *vdev = video_devdata(file);
	int ret;

	if (freq->tuner != 0)
		return -EINVAL;

	ret = v4l2_subdev_call(cxdev->tda9887, tuner, s_frequency, freq);
	if (ret != 0)
		return ret;

	ret = v4l2_subdev_call(cxdev->tuner, tuner, s_frequency, freq);
	if (ret != 0)
		return ret;

	/*
	 * make sure that cx25840 is in a correct TV / radio mode,
	 * since calls above may have changed it for tuner / IF demod
	 */
	if (vdev->vfl_type == VFL_TYPE_VIDEO)
		v4l2_subdev_call(cxdev->cx25840, video, s_std, cxdev->norm);
	else
		v4l2_subdev_call(cxdev->cx25840, tuner, s_radio);

	return v4l2_subdev_call(cxdev->cx25840, tuner, s_frequency, freq);
}

static int cxusb_medion_g_std(struct file *file, void *fh,
			      v4l2_std_id *norm)
{
	struct dvb_usb_device *dvbdev = video_drvdata(file);
	struct cxusb_medion_dev *cxdev = dvbdev->priv;

	*norm = cxdev->norm;

	if (*norm == V4L2_STD_UNKNOWN)
		return -ENODATA;

	return 0;
}

static int cxusb_medion_s_std(struct file *file, void *fh,
			      v4l2_std_id norm)
{
	struct dvb_usb_device *dvbdev = video_drvdata(file);
	struct cxusb_medion_dev *cxdev = dvbdev->priv;

	return cxusb_medion_set_norm(cxdev, norm);
}

static int cxusb_medion_querystd(struct file *file, void *fh,
				 v4l2_std_id *norm)
{
	struct dvb_usb_device *dvbdev = video_drvdata(file);
	struct cxusb_medion_dev *cxdev = dvbdev->priv;
	v4l2_std_id norm_mask;
	int ret;

	/*
	 * make sure we don't have improper std bits set for the TV tuner
	 * (could happen when no signal was present yet after reset)
	 */
	if (cxdev->input == 0)
		norm_mask = V4L2_STD_PAL;
	else
		norm_mask = V4L2_STD_ALL;

	ret = v4l2_subdev_call(cxdev->cx25840, video, querystd, norm);
	if (ret != 0) {
		cxusb_vprintk(dvbdev, OPS,
			      "cannot get detected standard for input %u\n",
			      (unsigned int)cxdev->input);
		return ret;
	}

	cxusb_vprintk(dvbdev, OPS, "input %u detected standard is %lx\n",
		      (unsigned int)cxdev->input, (unsigned long)*norm);
	*norm &= norm_mask;

	return 0;
}

static int cxusb_medion_log_status(struct file *file, void *fh)
{
	struct dvb_usb_device *dvbdev = video_drvdata(file);
	struct cxusb_medion_dev *cxdev = dvbdev->priv;

	v4l2_device_call_all(&cxdev->v4l2dev, 0, core, log_status);

	return 0;
}

static const struct v4l2_ioctl_ops cxusb_video_ioctl = {
	.vidioc_querycap = cxusb_medion_v_querycap,
	.vidioc_enum_fmt_vid_cap = cxusb_medion_v_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap = cxusb_medion_g_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap = cxusb_medion_s_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap = cxusb_medion_try_fmt_vid_cap,
	.vidioc_enum_input = cxusb_medion_enum_input,
	.vidioc_g_input = cxusb_medion_g_input,
	.vidioc_s_input = cxusb_medion_s_input,
	.vidioc_g_tuner = cxusb_medion_g_tuner,
	.vidioc_s_tuner = cxusb_medion_s_tuner,
	.vidioc_g_frequency = cxusb_medion_g_frequency,
	.vidioc_s_frequency = cxusb_medion_s_frequency,
	.vidioc_g_std = cxusb_medion_g_std,
	.vidioc_s_std = cxusb_medion_s_std,
	.vidioc_querystd = cxusb_medion_querystd,
	.vidioc_log_status = cxusb_medion_log_status,
	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,
	.vidioc_create_bufs = vb2_ioctl_create_bufs,
	.vidioc_prepare_buf = vb2_ioctl_prepare_buf,
	.vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_streamoff = vb2_ioctl_streamoff
};

static const struct v4l2_ioctl_ops cxusb_radio_ioctl = {
	.vidioc_querycap = cxusb_medion_v_querycap,
	.vidioc_g_tuner = cxusb_medion_g_tuner,
	.vidioc_s_tuner = cxusb_medion_s_tuner,
	.vidioc_g_frequency = cxusb_medion_g_frequency,
	.vidioc_s_frequency = cxusb_medion_s_frequency,
	.vidioc_log_status = cxusb_medion_log_status
};

/*
 * in principle, this should be const, but s_io_pin_config is declared
 * to take non-const, and gcc complains
 */
static struct v4l2_subdev_io_pin_config cxusub_medion_pin_config[] = {
	{ .pin = CX25840_PIN_DVALID_PRGM0, .function = CX25840_PAD_DEFAULT,
	  .strength = CX25840_PIN_DRIVE_MEDIUM },
	{ .pin = CX25840_PIN_PLL_CLK_PRGM7, .function = CX25840_PAD_AUX_PLL },
	{ .pin = CX25840_PIN_HRESET_PRGM2, .function = CX25840_PAD_ACTIVE,
	  .strength = CX25840_PIN_DRIVE_MEDIUM }
};

int cxusb_medion_analog_init(struct dvb_usb_device *dvbdev)
{
	struct cxusb_medion_dev *cxdev = dvbdev->priv;
	u8 tuner_analog_msg_data[] = { 0x9c, 0x60, 0x85, 0x54 };
	struct i2c_msg tuner_analog_msg = { .addr = 0x61, .flags = 0,
					    .buf = tuner_analog_msg_data,
					    .len =
					    sizeof(tuner_analog_msg_data) };
	struct v4l2_subdev_format subfmt = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};
	int ret;

	/* switch tuner to analog mode so IF demod will become accessible */
	ret = i2c_transfer(&dvbdev->i2c_adap, &tuner_analog_msg, 1);
	if (ret != 1)
		dev_warn(&dvbdev->udev->dev,
			 "tuner analog switch failed (%d)\n", ret);

	/*
	 * cx25840 might have lost power during mode switching so we need
	 * to set it again
	 */
	ret = v4l2_subdev_call(cxdev->cx25840, core, reset, 0);
	if (ret != 0)
		dev_warn(&dvbdev->udev->dev,
			 "cx25840 reset failed (%d)\n", ret);

	ret = v4l2_subdev_call(cxdev->cx25840, video, s_routing,
			       CX25840_COMPOSITE1, 0, 0);
	if (ret != 0)
		dev_warn(&dvbdev->udev->dev,
			 "cx25840 initial input setting failed (%d)\n", ret);

	/* composite */
	cxdev->input = 1;
	cxdev->videodev->tvnorms = V4L2_STD_ALL;
	cxdev->norm = V4L2_STD_PAL;

	/* TODO: setup audio samples insertion */

	ret = v4l2_subdev_call(cxdev->cx25840, core, s_io_pin_config,
			       ARRAY_SIZE(cxusub_medion_pin_config),
			       cxusub_medion_pin_config);
	if (ret != 0)
		dev_warn(&dvbdev->udev->dev,
			 "cx25840 pin config failed (%d)\n", ret);

	/* make sure that we aren't in radio mode */
	v4l2_subdev_call(cxdev->tda9887, video, s_std, cxdev->norm);
	v4l2_subdev_call(cxdev->tuner, video, s_std, cxdev->norm);
	v4l2_subdev_call(cxdev->cx25840, video, s_std, cxdev->norm);

	subfmt.format.width = cxdev->width;
	subfmt.format.height = cxdev->height;
	subfmt.format.code = MEDIA_BUS_FMT_FIXED;
	subfmt.format.field = V4L2_FIELD_SEQ_TB;
	subfmt.format.colorspace = V4L2_COLORSPACE_SMPTE170M;

	ret = v4l2_subdev_call(cxdev->cx25840, pad, set_fmt, NULL, &subfmt);
	if (ret != 0)
		dev_warn(&dvbdev->udev->dev,
			 "cx25840 format set failed (%d)\n", ret);

	if (ret == 0) {
		cxdev->width = subfmt.format.width;
		cxdev->height = subfmt.format.height;
	}

	return 0;
}

static int cxusb_videoradio_open(struct file *f)
{
	struct dvb_usb_device *dvbdev = video_drvdata(f);
	int ret;

	/*
	 * no locking needed since this call only modifies analog
	 * state if there are no other analog handles currenly
	 * opened so ops done via them cannot create a conflict
	 */
	ret = cxusb_medion_get(dvbdev, CXUSB_OPEN_ANALOG);
	if (ret != 0)
		return ret;

	ret = v4l2_fh_open(f);
	if (ret != 0)
		goto ret_release;

	cxusb_vprintk(dvbdev, OPS, "got open\n");

	return 0;

ret_release:
	cxusb_medion_put(dvbdev);

	return ret;
}

static int cxusb_videoradio_release(struct file *f)
{
	struct video_device *vdev = video_devdata(f);
	struct dvb_usb_device *dvbdev = video_drvdata(f);
	int ret;

	cxusb_vprintk(dvbdev, OPS, "got release\n");

	if (vdev->vfl_type == VFL_TYPE_VIDEO)
		ret = vb2_fop_release(f);
	else
		ret = v4l2_fh_release(f);

	cxusb_medion_put(dvbdev);

	return ret;
}

static const struct v4l2_file_operations cxusb_video_fops = {
	.owner = THIS_MODULE,
	.read = vb2_fop_read,
	.poll = vb2_fop_poll,
	.unlocked_ioctl = video_ioctl2,
	.mmap = vb2_fop_mmap,
	.open = cxusb_videoradio_open,
	.release = cxusb_videoradio_release
};

static const struct v4l2_file_operations cxusb_radio_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = video_ioctl2,
	.open = cxusb_videoradio_open,
	.release = cxusb_videoradio_release
};

static void cxusb_medion_v4l2_release(struct v4l2_device *v4l2_dev)
{
	struct cxusb_medion_dev *cxdev =
		container_of(v4l2_dev, struct cxusb_medion_dev, v4l2dev);
	struct dvb_usb_device *dvbdev = cxdev->dvbdev;

	cxusb_vprintk(dvbdev, OPS, "v4l2 device release\n");

	v4l2_device_unregister(&cxdev->v4l2dev);

	mutex_destroy(&cxdev->dev_lock);

	while (completion_done(&cxdev->v4l2_release))
		schedule();

	complete(&cxdev->v4l2_release);
}

static void cxusb_medion_videodev_release(struct video_device *vdev)
{
	struct dvb_usb_device *dvbdev = video_get_drvdata(vdev);

	cxusb_vprintk(dvbdev, OPS, "video device release\n");

	video_device_release(vdev);
}

static int cxusb_medion_register_analog_video(struct dvb_usb_device *dvbdev)
{
	struct cxusb_medion_dev *cxdev = dvbdev->priv;
	int ret;

	cxdev->videoqueue.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	cxdev->videoqueue.io_modes = VB2_MMAP | VB2_USERPTR | VB2_READ |
		VB2_DMABUF;
	cxdev->videoqueue.ops = &cxdev_video_qops;
	cxdev->videoqueue.mem_ops = &vb2_vmalloc_memops;
	cxdev->videoqueue.drv_priv = dvbdev;
	cxdev->videoqueue.buf_struct_size =
		sizeof(struct cxusb_medion_vbuffer);
	cxdev->videoqueue.timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	cxdev->videoqueue.min_queued_buffers = 6;
	cxdev->videoqueue.lock = &cxdev->dev_lock;

	ret = vb2_queue_init(&cxdev->videoqueue);
	if (ret) {
		dev_err(&dvbdev->udev->dev,
			"video queue init failed, ret = %d\n", ret);
		return ret;
	}

	cxdev->videodev = video_device_alloc();
	if (!cxdev->videodev) {
		dev_err(&dvbdev->udev->dev, "video device alloc failed\n");
		return -ENOMEM;
	}

	cxdev->videodev->device_caps = videocaps;
	cxdev->videodev->fops = &cxusb_video_fops;
	cxdev->videodev->v4l2_dev = &cxdev->v4l2dev;
	cxdev->videodev->queue = &cxdev->videoqueue;
	strscpy(cxdev->videodev->name, "cxusb", sizeof(cxdev->videodev->name));
	cxdev->videodev->vfl_dir = VFL_DIR_RX;
	cxdev->videodev->ioctl_ops = &cxusb_video_ioctl;
	cxdev->videodev->tvnorms = V4L2_STD_ALL;
	cxdev->videodev->release = cxusb_medion_videodev_release;
	cxdev->videodev->lock = &cxdev->dev_lock;
	video_set_drvdata(cxdev->videodev, dvbdev);

	ret = video_register_device(cxdev->videodev, VFL_TYPE_VIDEO, -1);
	if (ret) {
		dev_err(&dvbdev->udev->dev,
			"video device register failed, ret = %d\n", ret);
		goto ret_vrelease;
	}

	return 0;

ret_vrelease:
	video_device_release(cxdev->videodev);
	return ret;
}

static int cxusb_medion_register_analog_radio(struct dvb_usb_device *dvbdev)
{
	struct cxusb_medion_dev *cxdev = dvbdev->priv;
	int ret;

	cxdev->radiodev = video_device_alloc();
	if (!cxdev->radiodev) {
		dev_err(&dvbdev->udev->dev, "radio device alloc failed\n");
		return -ENOMEM;
	}

	cxdev->radiodev->device_caps = radiocaps;
	cxdev->radiodev->fops = &cxusb_radio_fops;
	cxdev->radiodev->v4l2_dev = &cxdev->v4l2dev;
	strscpy(cxdev->radiodev->name, "cxusb", sizeof(cxdev->radiodev->name));
	cxdev->radiodev->vfl_dir = VFL_DIR_RX;
	cxdev->radiodev->ioctl_ops = &cxusb_radio_ioctl;
	cxdev->radiodev->release = video_device_release;
	cxdev->radiodev->lock = &cxdev->dev_lock;
	video_set_drvdata(cxdev->radiodev, dvbdev);

	ret = video_register_device(cxdev->radiodev, VFL_TYPE_RADIO, -1);
	if (ret) {
		dev_err(&dvbdev->udev->dev,
			"radio device register failed, ret = %d\n", ret);
		video_device_release(cxdev->radiodev);
		return ret;
	}

	return 0;
}

static int cxusb_medion_register_analog_subdevs(struct dvb_usb_device *dvbdev)
{
	struct cxusb_medion_dev *cxdev = dvbdev->priv;
	int ret;
	struct tuner_setup tun_setup;

	/* attach cx25840 capture chip */
	cxdev->cx25840 = v4l2_i2c_new_subdev(&cxdev->v4l2dev,
					     &dvbdev->i2c_adap,
					     "cx25840", 0x44, NULL);
	if (!cxdev->cx25840) {
		dev_err(&dvbdev->udev->dev, "cx25840 not found\n");
		return -ENODEV;
	}

	/*
	 * Initialize cx25840 chip by calling its subdevice init core op.
	 *
	 * This switches it into the generic mode that disables some of
	 * ivtv-related hacks in the cx25840 driver while allowing setting
	 * of the chip video output configuration (passed in the call below
	 * as the last argument).
	 */
	ret = v4l2_subdev_call(cxdev->cx25840, core, init,
			       CX25840_VCONFIG_FMT_BT656 |
			       CX25840_VCONFIG_RES_8BIT |
			       CX25840_VCONFIG_VBIRAW_DISABLED |
			       CX25840_VCONFIG_ANCDATA_DISABLED |
			       CX25840_VCONFIG_ACTIVE_COMPOSITE |
			       CX25840_VCONFIG_VALID_ANDACTIVE |
			       CX25840_VCONFIG_HRESETW_NORMAL |
			       CX25840_VCONFIG_CLKGATE_NONE |
			       CX25840_VCONFIG_DCMODE_DWORDS);
	if (ret != 0) {
		dev_err(&dvbdev->udev->dev,
			"cx25840 init failed (%d)\n", ret);
		return ret;
	}

	/* attach analog tuner */
	cxdev->tuner = v4l2_i2c_new_subdev(&cxdev->v4l2dev,
					   &dvbdev->i2c_adap,
					   "tuner", 0x61, NULL);
	if (!cxdev->tuner) {
		dev_err(&dvbdev->udev->dev, "tuner not found\n");
		return -ENODEV;
	}

	/* configure it */
	memset(&tun_setup, 0, sizeof(tun_setup));
	tun_setup.addr = 0x61;
	tun_setup.type = TUNER_PHILIPS_FMD1216ME_MK3;
	tun_setup.mode_mask = T_RADIO | T_ANALOG_TV;
	v4l2_subdev_call(cxdev->tuner, tuner, s_type_addr, &tun_setup);

	/* attach IF demod */
	cxdev->tda9887 = v4l2_i2c_new_subdev(&cxdev->v4l2dev,
					     &dvbdev->i2c_adap,
					     "tuner", 0x43, NULL);
	if (!cxdev->tda9887) {
		dev_err(&dvbdev->udev->dev, "tda9887 not found\n");
		return -ENODEV;
	}

	return 0;
}

int cxusb_medion_register_analog(struct dvb_usb_device *dvbdev)
{
	struct cxusb_medion_dev *cxdev = dvbdev->priv;
	int ret;

	mutex_init(&cxdev->dev_lock);

	init_completion(&cxdev->v4l2_release);

	cxdev->v4l2dev.release = cxusb_medion_v4l2_release;

	ret = v4l2_device_register(&dvbdev->udev->dev, &cxdev->v4l2dev);
	if (ret != 0) {
		dev_err(&dvbdev->udev->dev,
			"V4L2 device registration failed, ret = %d\n", ret);
		mutex_destroy(&cxdev->dev_lock);
		return ret;
	}

	ret = cxusb_medion_register_analog_subdevs(dvbdev);
	if (ret)
		goto ret_unregister;

	INIT_WORK(&cxdev->urbwork, cxusb_medion_v_complete_work);
	INIT_LIST_HEAD(&cxdev->buflist);

	cxdev->width = 320;
	cxdev->height = 240;

	ret = cxusb_medion_register_analog_video(dvbdev);
	if (ret)
		goto ret_unregister;

	ret = cxusb_medion_register_analog_radio(dvbdev);
	if (ret)
		goto ret_vunreg;

	return 0;

ret_vunreg:
	vb2_video_unregister_device(cxdev->videodev);

ret_unregister:
	v4l2_device_put(&cxdev->v4l2dev);
	wait_for_completion(&cxdev->v4l2_release);

	return ret;
}

void cxusb_medion_unregister_analog(struct dvb_usb_device *dvbdev)
{
	struct cxusb_medion_dev *cxdev = dvbdev->priv;

	cxusb_vprintk(dvbdev, OPS, "unregistering analog\n");

	video_unregister_device(cxdev->radiodev);
	vb2_video_unregister_device(cxdev->videodev);

	v4l2_device_put(&cxdev->v4l2dev);
	wait_for_completion(&cxdev->v4l2_release);

	cxusb_vprintk(dvbdev, OPS, "analog unregistered\n");
}
