// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * av7110_av.c: audio and video MPEG decoder stuff
 *
 * Copyright (C) 1999-2002 Ralph  Metzler
 *                       & Marcus Metzler for convergence integrated media GmbH
 *
 * originally based on code by:
 * Copyright (C) 1998,1999 Christian Theiss <mistert@rz.fh-augsburg.de>
 *
 * the project's page is at https://linuxtv.org
 */

#include <linux/ethtool.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/fs.h>

#include "av7110.h"
#include "av7110_hw.h"
#include "av7110_av.h"
#include "av7110_ipack.h"

/* MPEG-2 (ISO 13818 / H.222.0) stream types */
#define PROG_STREAM_MAP  0xBC
#define PRIVATE_STREAM1  0xBD
#define PADDING_STREAM	 0xBE
#define PRIVATE_STREAM2  0xBF
#define AUDIO_STREAM_S	 0xC0
#define AUDIO_STREAM_E	 0xDF
#define VIDEO_STREAM_S	 0xE0
#define VIDEO_STREAM_E	 0xEF
#define ECM_STREAM	 0xF0
#define EMM_STREAM	 0xF1
#define DSM_CC_STREAM	 0xF2
#define ISO13522_STREAM  0xF3
#define PROG_STREAM_DIR  0xFF

#define PTS_DTS_FLAGS	 0xC0

//pts_dts flags
#define PTS_ONLY	 0x80
#define PTS_DTS		 0xC0
#define TS_SIZE		 188
#define TRANS_ERROR	 0x80
#define PAY_START	 0x40
#define TRANS_PRIO	 0x20
#define PID_MASK_HI	 0x1F
//flags
#define TRANS_SCRMBL1	 0x80
#define TRANS_SCRMBL2	 0x40
#define ADAPT_FIELD	 0x20
#define PAYLOAD		 0x10
#define COUNT_MASK	 0x0F

// adaptation flags
#define DISCON_IND	 0x80
#define RAND_ACC_IND	 0x40
#define ES_PRI_IND	 0x20
#define PCR_FLAG	 0x10
#define OPCR_FLAG	 0x08
#define SPLICE_FLAG	 0x04
#define TRANS_PRIV	 0x02
#define ADAP_EXT_FLAG	 0x01

// adaptation extension flags
#define LTW_FLAG	 0x80
#define PIECE_RATE	 0x40
#define SEAM_SPLICE	 0x20


static void p_to_t(u8 const *buf, long int length, u16 pid,
		   u8 *counter, struct dvb_demux_feed *feed);
static int write_ts_to_decoder(struct av7110 *av7110, int type, const u8 *buf, size_t len);


int av7110_record_cb(struct dvb_filter_pes2ts *p2t, u8 *buf, size_t len)
{
	struct dvb_demux_feed *dvbdmxfeed = (struct dvb_demux_feed *) p2t->priv;

	if (!(dvbdmxfeed->ts_type & TS_PACKET))
		return 0;
	if (buf[3] == 0xe0)	 // video PES do not have a length in TS
		buf[4] = buf[5] = 0;
	if (dvbdmxfeed->ts_type & TS_PAYLOAD_ONLY)
		return dvbdmxfeed->cb.ts(buf, len, NULL, 0,
					 &dvbdmxfeed->feed.ts, NULL);
	else
		return dvb_filter_pes2ts(p2t, buf, len, 1);
}

static int dvb_filter_pes2ts_cb(void *priv, unsigned char *data)
{
	struct dvb_demux_feed *dvbdmxfeed = (struct dvb_demux_feed *) priv;

	dvbdmxfeed->cb.ts(data, 188, NULL, 0,
			  &dvbdmxfeed->feed.ts, NULL);
	return 0;
}

int av7110_av_start_record(struct av7110 *av7110, int av,
			   struct dvb_demux_feed *dvbdmxfeed)
{
	int ret = 0;
	struct dvb_demux *dvbdmx = dvbdmxfeed->demux;

	dprintk(2, "av7110:%p, , dvb_demux_feed:%p\n", av7110, dvbdmxfeed);

	if (av7110->playing || (av7110->rec_mode & av))
		return -EBUSY;
	av7110_fw_cmd(av7110, COMTYPE_REC_PLAY, __Stop, 0);
	dvbdmx->recording = 1;
	av7110->rec_mode |= av;

	switch (av7110->rec_mode) {
	case RP_AUDIO:
		dvb_filter_pes2ts_init(&av7110->p2t[0],
				       dvbdmx->pesfilter[0]->pid,
				       dvb_filter_pes2ts_cb,
				       (void *) dvbdmx->pesfilter[0]);
		ret = av7110_fw_cmd(av7110, COMTYPE_REC_PLAY, __Record, 2, AudioPES, 0);
		break;

	case RP_VIDEO:
		dvb_filter_pes2ts_init(&av7110->p2t[1],
				       dvbdmx->pesfilter[1]->pid,
				       dvb_filter_pes2ts_cb,
				       (void *) dvbdmx->pesfilter[1]);
		ret = av7110_fw_cmd(av7110, COMTYPE_REC_PLAY, __Record, 2, VideoPES, 0);
		break;

	case RP_AV:
		dvb_filter_pes2ts_init(&av7110->p2t[0],
				       dvbdmx->pesfilter[0]->pid,
				       dvb_filter_pes2ts_cb,
				       (void *) dvbdmx->pesfilter[0]);
		dvb_filter_pes2ts_init(&av7110->p2t[1],
				       dvbdmx->pesfilter[1]->pid,
				       dvb_filter_pes2ts_cb,
				       (void *) dvbdmx->pesfilter[1]);
		ret = av7110_fw_cmd(av7110, COMTYPE_REC_PLAY, __Record, 2, AV_PES, 0);
		break;
	}
	return ret;
}

int av7110_av_start_play(struct av7110 *av7110, int av)
{
	int ret = 0;
	dprintk(2, "av7110:%p, \n", av7110);

	if (av7110->rec_mode)
		return -EBUSY;
	if (av7110->playing & av)
		return -EBUSY;

	av7110_fw_cmd(av7110, COMTYPE_REC_PLAY, __Stop, 0);

	if (av7110->playing == RP_NONE) {
		av7110_ipack_reset(&av7110->ipack[0]);
		av7110_ipack_reset(&av7110->ipack[1]);
	}

	av7110->playing |= av;
	switch (av7110->playing) {
	case RP_AUDIO:
		ret = av7110_fw_cmd(av7110, COMTYPE_REC_PLAY, __Play, 2, AudioPES, 0);
		break;
	case RP_VIDEO:
		ret = av7110_fw_cmd(av7110, COMTYPE_REC_PLAY, __Play, 2, VideoPES, 0);
		av7110->sinfo = 0;
		break;
	case RP_AV:
		av7110->sinfo = 0;
		ret = av7110_fw_cmd(av7110, COMTYPE_REC_PLAY, __Play, 2, AV_PES, 0);
		break;
	}
	return ret;
}

int av7110_av_stop(struct av7110 *av7110, int av)
{
	int ret = 0;
	dprintk(2, "av7110:%p, \n", av7110);

	if (!(av7110->playing & av) && !(av7110->rec_mode & av))
		return 0;
	av7110_fw_cmd(av7110, COMTYPE_REC_PLAY, __Stop, 0);
	if (av7110->playing) {
		av7110->playing &= ~av;
		switch (av7110->playing) {
		case RP_AUDIO:
			ret = av7110_fw_cmd(av7110, COMTYPE_REC_PLAY, __Play, 2, AudioPES, 0);
			break;
		case RP_VIDEO:
			ret = av7110_fw_cmd(av7110, COMTYPE_REC_PLAY, __Play, 2, VideoPES, 0);
			break;
		case RP_NONE:
			ret = av7110_set_vidmode(av7110, av7110->vidmode);
			break;
		}
	} else {
		av7110->rec_mode &= ~av;
		switch (av7110->rec_mode) {
		case RP_AUDIO:
			ret = av7110_fw_cmd(av7110, COMTYPE_REC_PLAY, __Record, 2, AudioPES, 0);
			break;
		case RP_VIDEO:
			ret = av7110_fw_cmd(av7110, COMTYPE_REC_PLAY, __Record, 2, VideoPES, 0);
			break;
		case RP_NONE:
			break;
		}
	}
	return ret;
}


int av7110_pes_play(void *dest, struct dvb_ringbuffer *buf, int dlen)
{
	int len;
	u32 sync;
	u16 blen;

	if (!dlen) {
		wake_up(&buf->queue);
		return -1;
	}
	while (1) {
		len = dvb_ringbuffer_avail(buf);
		if (len < 6) {
			wake_up(&buf->queue);
			return -1;
		}
		sync =  DVB_RINGBUFFER_PEEK(buf, 0) << 24;
		sync |= DVB_RINGBUFFER_PEEK(buf, 1) << 16;
		sync |= DVB_RINGBUFFER_PEEK(buf, 2) << 8;
		sync |= DVB_RINGBUFFER_PEEK(buf, 3);

		if (((sync &~ 0x0f) == 0x000001e0) ||
		    ((sync &~ 0x1f) == 0x000001c0) ||
		    (sync == 0x000001bd))
			break;
		printk("resync\n");
		DVB_RINGBUFFER_SKIP(buf, 1);
	}
	blen =  DVB_RINGBUFFER_PEEK(buf, 4) << 8;
	blen |= DVB_RINGBUFFER_PEEK(buf, 5);
	blen += 6;
	if (len < blen || blen > dlen) {
		//printk("buffer empty - avail %d blen %u dlen %d\n", len, blen, dlen);
		wake_up(&buf->queue);
		return -1;
	}

	dvb_ringbuffer_read(buf, dest, (size_t) blen);

	dprintk(2, "pread=0x%08lx, pwrite=0x%08lx\n",
	       (unsigned long) buf->pread, (unsigned long) buf->pwrite);
	wake_up(&buf->queue);
	return blen;
}


int av7110_set_volume(struct av7110 *av7110, unsigned int volleft,
		      unsigned int volright)
{
	unsigned int vol, val, balance = 0;
	int err;

	dprintk(2, "av7110:%p, \n", av7110);

	av7110->mixer.volume_left = volleft;
	av7110->mixer.volume_right = volright;

	switch (av7110->adac_type) {
	case DVB_ADAC_TI:
		volleft = (volleft * 256) / 1036;
		volright = (volright * 256) / 1036;
		if (volleft > 0x3f)
			volleft = 0x3f;
		if (volright > 0x3f)
			volright = 0x3f;
		if ((err = SendDAC(av7110, 3, 0x80 + volleft)))
			return err;
		return SendDAC(av7110, 4, volright);

	case DVB_ADAC_CRYSTAL:
		volleft = 127 - volleft / 2;
		volright = 127 - volright / 2;
		i2c_writereg(av7110, 0x20, 0x03, volleft);
		i2c_writereg(av7110, 0x20, 0x04, volright);
		return 0;

	case DVB_ADAC_MSP34x0:
		vol  = (volleft > volright) ? volleft : volright;
		val	= (vol * 0x73 / 255) << 8;
		if (vol > 0)
		       balance = ((volright - volleft) * 127) / vol;
		msp_writereg(av7110, MSP_WR_DSP, 0x0001, balance << 8);
		msp_writereg(av7110, MSP_WR_DSP, 0x0000, val); /* loudspeaker */
		msp_writereg(av7110, MSP_WR_DSP, 0x0006, val); /* headphonesr */
		return 0;

	case DVB_ADAC_MSP34x5:
		vol = (volleft > volright) ? volleft : volright;
		val = (vol * 0x73 / 255) << 8;
		if (vol > 0)
			balance = ((volright - volleft) * 127) / vol;
		msp_writereg(av7110, MSP_WR_DSP, 0x0001, balance << 8);
		msp_writereg(av7110, MSP_WR_DSP, 0x0000, val); /* loudspeaker */
		return 0;
	}

	return 0;
}

int av7110_set_vidmode(struct av7110 *av7110, enum av7110_video_mode mode)
{
	int ret;
	dprintk(2, "av7110:%p, \n", av7110);

	ret = av7110_fw_cmd(av7110, COMTYPE_ENCODER, LoadVidCode, 1, mode);

	if (!ret && !av7110->playing) {
		ret = ChangePIDs(av7110, av7110->pids[DMX_PES_VIDEO],
			   av7110->pids[DMX_PES_AUDIO],
			   av7110->pids[DMX_PES_TELETEXT],
			   0, av7110->pids[DMX_PES_PCR]);
		if (!ret)
			ret = av7110_fw_cmd(av7110, COMTYPE_PIDFILTER, Scan, 0);
	}
	return ret;
}


static enum av7110_video_mode sw2mode[16] = {
	AV7110_VIDEO_MODE_PAL, AV7110_VIDEO_MODE_NTSC,
	AV7110_VIDEO_MODE_NTSC, AV7110_VIDEO_MODE_PAL,
	AV7110_VIDEO_MODE_NTSC, AV7110_VIDEO_MODE_NTSC,
	AV7110_VIDEO_MODE_PAL, AV7110_VIDEO_MODE_NTSC,
	AV7110_VIDEO_MODE_PAL, AV7110_VIDEO_MODE_PAL,
	AV7110_VIDEO_MODE_PAL, AV7110_VIDEO_MODE_PAL,
	AV7110_VIDEO_MODE_PAL, AV7110_VIDEO_MODE_PAL,
	AV7110_VIDEO_MODE_PAL, AV7110_VIDEO_MODE_PAL,
};

static int get_video_format(struct av7110 *av7110, u8 *buf, int count)
{
	int i;
	int hsize, vsize;
	int sw;
	u8 *p;
	int ret = 0;

	dprintk(2, "av7110:%p, \n", av7110);

	if (av7110->sinfo)
		return 0;
	for (i = 7; i < count - 10; i++) {
		p = buf + i;
		if (p[0] || p[1] || p[2] != 0x01 || p[3] != 0xb3)
			continue;
		p += 4;
		hsize = ((p[1] &0xF0) >> 4) | (p[0] << 4);
		vsize = ((p[1] &0x0F) << 8) | (p[2]);
		sw = (p[3] & 0x0F);
		ret = av7110_set_vidmode(av7110, sw2mode[sw]);
		if (!ret) {
			dprintk(2, "playback %dx%d fr=%d\n", hsize, vsize, sw);
			av7110->sinfo = 1;
		}
		break;
	}
	return ret;
}


/****************************************************************************
 * I/O buffer management and control
 ****************************************************************************/

static inline long aux_ring_buffer_write(struct dvb_ringbuffer *rbuf,
					 const u8 *buf, unsigned long count)
{
	unsigned long todo = count;
	int free;

	while (todo > 0) {
		if (dvb_ringbuffer_free(rbuf) < 2048) {
			if (wait_event_interruptible(rbuf->queue,
						     (dvb_ringbuffer_free(rbuf) >= 2048)))
				return count - todo;
		}
		free = dvb_ringbuffer_free(rbuf);
		if (free > todo)
			free = todo;
		dvb_ringbuffer_write(rbuf, buf, free);
		todo -= free;
		buf += free;
	}

	return count - todo;
}

static void play_video_cb(u8 *buf, int count, void *priv)
{
	struct av7110 *av7110 = (struct av7110 *) priv;
	dprintk(2, "av7110:%p, \n", av7110);

	if ((buf[3] & 0xe0) == 0xe0) {
		get_video_format(av7110, buf, count);
		aux_ring_buffer_write(&av7110->avout, buf, count);
	} else
		aux_ring_buffer_write(&av7110->aout, buf, count);
}

static void play_audio_cb(u8 *buf, int count, void *priv)
{
	struct av7110 *av7110 = (struct av7110 *) priv;
	dprintk(2, "av7110:%p, \n", av7110);

	aux_ring_buffer_write(&av7110->aout, buf, count);
}


#define FREE_COND_TS (dvb_ringbuffer_free(rb) >= 4096)

static ssize_t ts_play(struct av7110 *av7110, const char __user *buf,
		       unsigned long count, int nonblock, int type)
{
	struct dvb_ringbuffer *rb;
	u8 *kb;
	unsigned long todo = count;

	dprintk(2, "%s: type %d cnt %lu\n", __func__, type, count);

	rb = (type) ? &av7110->avout : &av7110->aout;
	kb = av7110->kbuf[type];

	if (!kb)
		return -ENOBUFS;

	if (nonblock && !FREE_COND_TS)
		return -EWOULDBLOCK;

	while (todo >= TS_SIZE) {
		if (!FREE_COND_TS) {
			if (nonblock)
				return count - todo;
			if (wait_event_interruptible(rb->queue, FREE_COND_TS))
				return count - todo;
		}
		if (copy_from_user(kb, buf, TS_SIZE))
			return -EFAULT;
		write_ts_to_decoder(av7110, type, kb, TS_SIZE);
		todo -= TS_SIZE;
		buf += TS_SIZE;
	}

	return count - todo;
}


#define FREE_COND (dvb_ringbuffer_free(&av7110->avout) >= 20 * 1024 && \
		   dvb_ringbuffer_free(&av7110->aout) >= 20 * 1024)

static ssize_t dvb_play(struct av7110 *av7110, const char __user *buf,
			unsigned long count, int nonblock, int type)
{
	unsigned long todo = count, n;
	dprintk(2, "av7110:%p, \n", av7110);

	if (!av7110->kbuf[type])
		return -ENOBUFS;

	if (nonblock && !FREE_COND)
		return -EWOULDBLOCK;

	while (todo > 0) {
		if (!FREE_COND) {
			if (nonblock)
				return count - todo;
			if (wait_event_interruptible(av7110->avout.queue,
						     FREE_COND))
				return count - todo;
		}
		n = todo;
		if (n > IPACKS * 2)
			n = IPACKS * 2;
		if (copy_from_user(av7110->kbuf[type], buf, n))
			return -EFAULT;
		av7110_ipack_instant_repack(av7110->kbuf[type], n,
					    &av7110->ipack[type]);
		todo -= n;
		buf += n;
	}
	return count - todo;
}

static ssize_t dvb_play_kernel(struct av7110 *av7110, const u8 *buf,
			unsigned long count, int nonblock, int type)
{
	unsigned long todo = count, n;
	dprintk(2, "av7110:%p, \n", av7110);

	if (!av7110->kbuf[type])
		return -ENOBUFS;

	if (nonblock && !FREE_COND)
		return -EWOULDBLOCK;

	while (todo > 0) {
		if (!FREE_COND) {
			if (nonblock)
				return count - todo;
			if (wait_event_interruptible(av7110->avout.queue,
						     FREE_COND))
				return count - todo;
		}
		n = todo;
		if (n > IPACKS * 2)
			n = IPACKS * 2;
		av7110_ipack_instant_repack(buf, n, &av7110->ipack[type]);
		todo -= n;
		buf += n;
	}
	return count - todo;
}

static ssize_t dvb_aplay(struct av7110 *av7110, const char __user *buf,
			 unsigned long count, int nonblock, int type)
{
	unsigned long todo = count, n;
	dprintk(2, "av7110:%p, \n", av7110);

	if (!av7110->kbuf[type])
		return -ENOBUFS;
	if (nonblock && dvb_ringbuffer_free(&av7110->aout) < 20 * 1024)
		return -EWOULDBLOCK;

	while (todo > 0) {
		if (dvb_ringbuffer_free(&av7110->aout) < 20 * 1024) {
			if (nonblock)
				return count - todo;
			if (wait_event_interruptible(av7110->aout.queue,
					(dvb_ringbuffer_free(&av7110->aout) >= 20 * 1024)))
				return count-todo;
		}
		n = todo;
		if (n > IPACKS * 2)
			n = IPACKS * 2;
		if (copy_from_user(av7110->kbuf[type], buf, n))
			return -EFAULT;
		av7110_ipack_instant_repack(av7110->kbuf[type], n,
					    &av7110->ipack[type]);
		todo -= n;
		buf += n;
	}
	return count - todo;
}

void av7110_p2t_init(struct av7110_p2t *p, struct dvb_demux_feed *feed)
{
	memset(p->pes, 0, TS_SIZE);
	p->counter = 0;
	p->pos = 0;
	p->frags = 0;
	if (feed)
		p->feed = feed;
}

static void clear_p2t(struct av7110_p2t *p)
{
	memset(p->pes, 0, TS_SIZE);
//	p->counter = 0;
	p->pos = 0;
	p->frags = 0;
}


static int find_pes_header(u8 const *buf, long int length, int *frags)
{
	int c = 0;
	int found = 0;

	*frags = 0;

	while (c < length - 3 && !found) {
		if (buf[c] == 0x00 && buf[c + 1] == 0x00 &&
		    buf[c + 2] == 0x01) {
			switch ( buf[c + 3] ) {
			case PROG_STREAM_MAP:
			case PRIVATE_STREAM2:
			case PROG_STREAM_DIR:
			case ECM_STREAM     :
			case EMM_STREAM     :
			case PADDING_STREAM :
			case DSM_CC_STREAM  :
			case ISO13522_STREAM:
			case PRIVATE_STREAM1:
			case AUDIO_STREAM_S ... AUDIO_STREAM_E:
			case VIDEO_STREAM_S ... VIDEO_STREAM_E:
				found = 1;
				break;

			default:
				c++;
				break;
			}
		} else
			c++;
	}
	if (c == length - 3 && !found) {
		if (buf[length - 1] == 0x00)
			*frags = 1;
		if (buf[length - 2] == 0x00 &&
		    buf[length - 1] == 0x00)
			*frags = 2;
		if (buf[length - 3] == 0x00 &&
		    buf[length - 2] == 0x00 &&
		    buf[length - 1] == 0x01)
			*frags = 3;
		return -1;
	}

	return c;
}

void av7110_p2t_write(u8 const *buf, long int length, u16 pid, struct av7110_p2t *p)
{
	int c, c2, l, add;
	int check, rest;

	c = 0;
	c2 = 0;
	if (p->frags){
		check = 0;
		switch(p->frags) {
		case 1:
			if (buf[c] == 0x00 && buf[c + 1] == 0x01) {
				check = 1;
				c += 2;
			}
			break;
		case 2:
			if (buf[c] == 0x01) {
				check = 1;
				c++;
			}
			break;
		case 3:
			check = 1;
		}
		if (check) {
			switch (buf[c]) {
			case PROG_STREAM_MAP:
			case PRIVATE_STREAM2:
			case PROG_STREAM_DIR:
			case ECM_STREAM     :
			case EMM_STREAM     :
			case PADDING_STREAM :
			case DSM_CC_STREAM  :
			case ISO13522_STREAM:
			case PRIVATE_STREAM1:
			case AUDIO_STREAM_S ... AUDIO_STREAM_E:
			case VIDEO_STREAM_S ... VIDEO_STREAM_E:
				p->pes[0] = 0x00;
				p->pes[1] = 0x00;
				p->pes[2] = 0x01;
				p->pes[3] = buf[c];
				p->pos = 4;
				memcpy(p->pes + p->pos, buf + c, (TS_SIZE - 4) - p->pos);
				c += (TS_SIZE - 4) - p->pos;
				p_to_t(p->pes, (TS_SIZE - 4), pid, &p->counter, p->feed);
				clear_p2t(p);
				break;

			default:
				c = 0;
				break;
			}
		}
		p->frags = 0;
	}

	if (p->pos) {
		c2 = find_pes_header(buf + c, length - c, &p->frags);
		if (c2 >= 0 && c2 < (TS_SIZE - 4) - p->pos)
			l = c2+c;
		else
			l = (TS_SIZE - 4) - p->pos;
		memcpy(p->pes + p->pos, buf, l);
		c += l;
		p->pos += l;
		p_to_t(p->pes, p->pos, pid, &p->counter, p->feed);
		clear_p2t(p);
	}

	add = 0;
	while (c < length) {
		c2 = find_pes_header(buf + c + add, length - c - add, &p->frags);
		if (c2 >= 0) {
			c2 += c + add;
			if (c2 > c){
				p_to_t(buf + c, c2 - c, pid, &p->counter, p->feed);
				c = c2;
				clear_p2t(p);
				add = 0;
			} else
				add = 1;
		} else {
			l = length - c;
			rest = l % (TS_SIZE - 4);
			l -= rest;
			p_to_t(buf + c, l, pid, &p->counter, p->feed);
			memcpy(p->pes, buf + c + l, rest);
			p->pos = rest;
			c = length;
		}
	}
}


static int write_ts_header2(u16 pid, u8 *counter, int pes_start, u8 *buf, u8 length)
{
	int i;
	int c = 0;
	int fill;
	u8 tshead[4] = { 0x47, 0x00, 0x00, 0x10 };

	fill = (TS_SIZE - 4) - length;
	if (pes_start)
		tshead[1] = 0x40;
	if (fill)
		tshead[3] = 0x30;
	tshead[1] |= (u8)((pid & 0x1F00) >> 8);
	tshead[2] |= (u8)(pid & 0x00FF);
	tshead[3] |= ((*counter)++ & 0x0F);
	memcpy(buf, tshead, 4);
	c += 4;

	if (fill) {
		buf[4] = fill - 1;
		c++;
		if (fill > 1) {
			buf[5] = 0x00;
			c++;
		}
		for (i = 6; i < fill + 4; i++) {
			buf[i] = 0xFF;
			c++;
		}
	}

	return c;
}


static void p_to_t(u8 const *buf, long int length, u16 pid, u8 *counter,
		   struct dvb_demux_feed *feed)
{
	int l, pes_start;
	u8 obuf[TS_SIZE];
	long c = 0;

	pes_start = 0;
	if (length > 3 &&
	     buf[0] == 0x00 && buf[1] == 0x00 && buf[2] == 0x01)
		switch (buf[3]) {
			case PROG_STREAM_MAP:
			case PRIVATE_STREAM2:
			case PROG_STREAM_DIR:
			case ECM_STREAM     :
			case EMM_STREAM     :
			case PADDING_STREAM :
			case DSM_CC_STREAM  :
			case ISO13522_STREAM:
			case PRIVATE_STREAM1:
			case AUDIO_STREAM_S ... AUDIO_STREAM_E:
			case VIDEO_STREAM_S ... VIDEO_STREAM_E:
				pes_start = 1;
				break;

			default:
				break;
		}

	while (c < length) {
		memset(obuf, 0, TS_SIZE);
		if (length - c >= (TS_SIZE - 4)){
			l = write_ts_header2(pid, counter, pes_start,
					     obuf, (TS_SIZE - 4));
			memcpy(obuf + l, buf + c, TS_SIZE - l);
			c += TS_SIZE - l;
		} else {
			l = write_ts_header2(pid, counter, pes_start,
					     obuf, length - c);
			memcpy(obuf + l, buf + c, TS_SIZE - l);
			c = length;
		}
		feed->cb.ts(obuf, 188, NULL, 0, &feed->feed.ts, NULL);
		pes_start = 0;
	}
}


static int write_ts_to_decoder(struct av7110 *av7110, int type, const u8 *buf, size_t len)
{
	struct ipack *ipack = &av7110->ipack[type];

	if (buf[1] & TRANS_ERROR) {
		av7110_ipack_reset(ipack);
		return -1;
	}

	if (!(buf[3] & PAYLOAD))
		return -1;

	if (buf[1] & PAY_START)
		av7110_ipack_flush(ipack);

	if (buf[3] & ADAPT_FIELD) {
		len -= buf[4] + 1;
		buf += buf[4] + 1;
		if (!len)
			return 0;
	}

	av7110_ipack_instant_repack(buf + 4, len - 4, ipack);
	return 0;
}


int av7110_write_to_decoder(struct dvb_demux_feed *feed, const u8 *buf, size_t len)
{
	struct dvb_demux *demux = feed->demux;
	struct av7110 *av7110 = (struct av7110 *) demux->priv;

	dprintk(2, "av7110:%p, \n", av7110);

	if (av7110->full_ts && demux->dmx.frontend->source != DMX_MEMORY_FE)
		return 0;

	switch (feed->pes_type) {
	case 0:
		if (av7110->audiostate.stream_source == AUDIO_SOURCE_MEMORY)
			return -EINVAL;
		break;
	case 1:
		if (av7110->videostate.stream_source == VIDEO_SOURCE_MEMORY)
			return -EINVAL;
		break;
	default:
		return -1;
	}

	return write_ts_to_decoder(av7110, feed->pes_type, buf, len);
}



/******************************************************************************
 * Video MPEG decoder events
 ******************************************************************************/
void dvb_video_add_event(struct av7110 *av7110, struct video_event *event)
{
	struct dvb_video_events *events = &av7110->video_events;
	int wp;

	spin_lock_bh(&events->lock);

	wp = (events->eventw + 1) % MAX_VIDEO_EVENT;
	if (wp == events->eventr) {
		events->overflow = 1;
		events->eventr = (events->eventr + 1) % MAX_VIDEO_EVENT;
	}

	//FIXME: timestamp?
	memcpy(&events->events[events->eventw], event, sizeof(struct video_event));
	events->eventw = wp;

	spin_unlock_bh(&events->lock);

	wake_up_interruptible(&events->wait_queue);
}


static int dvb_video_get_event (struct av7110 *av7110, struct video_event *event, int flags)
{
	struct dvb_video_events *events = &av7110->video_events;

	if (events->overflow) {
		events->overflow = 0;
		return -EOVERFLOW;
	}
	if (events->eventw == events->eventr) {
		int ret;

		if (flags & O_NONBLOCK)
			return -EWOULDBLOCK;

		ret = wait_event_interruptible(events->wait_queue,
					       events->eventw != events->eventr);
		if (ret < 0)
			return ret;
	}

	spin_lock_bh(&events->lock);

	memcpy(event, &events->events[events->eventr],
	       sizeof(struct video_event));
	events->eventr = (events->eventr + 1) % MAX_VIDEO_EVENT;

	spin_unlock_bh(&events->lock);

	return 0;
}

/******************************************************************************
 * DVB device file operations
 ******************************************************************************/

static __poll_t dvb_video_poll(struct file *file, poll_table *wait)
{
	struct dvb_device *dvbdev = file->private_data;
	struct av7110 *av7110 = dvbdev->priv;
	__poll_t mask = 0;

	dprintk(2, "av7110:%p, \n", av7110);

	if ((file->f_flags & O_ACCMODE) != O_RDONLY)
		poll_wait(file, &av7110->avout.queue, wait);

	poll_wait(file, &av7110->video_events.wait_queue, wait);

	if (av7110->video_events.eventw != av7110->video_events.eventr)
		mask = EPOLLPRI;

	if ((file->f_flags & O_ACCMODE) != O_RDONLY) {
		if (av7110->playing) {
			if (FREE_COND)
				mask |= (EPOLLOUT | EPOLLWRNORM);
		} else {
			/* if not playing: may play if asked for */
			mask |= (EPOLLOUT | EPOLLWRNORM);
		}
	}

	return mask;
}

static ssize_t dvb_video_write(struct file *file, const char __user *buf,
			       size_t count, loff_t *ppos)
{
	struct dvb_device *dvbdev = file->private_data;
	struct av7110 *av7110 = dvbdev->priv;
	unsigned char c;

	dprintk(2, "av7110:%p, \n", av7110);

	if ((file->f_flags & O_ACCMODE) == O_RDONLY)
		return -EPERM;

	if (av7110->videostate.stream_source != VIDEO_SOURCE_MEMORY)
		return -EPERM;

	if (get_user(c, buf))
		return -EFAULT;
	if (c == 0x47 && count % TS_SIZE == 0)
		return ts_play(av7110, buf, count, file->f_flags & O_NONBLOCK, 1);
	else
		return dvb_play(av7110, buf, count, file->f_flags & O_NONBLOCK, 1);
}

static __poll_t dvb_audio_poll(struct file *file, poll_table *wait)
{
	struct dvb_device *dvbdev = file->private_data;
	struct av7110 *av7110 = dvbdev->priv;
	__poll_t mask = 0;

	dprintk(2, "av7110:%p, \n", av7110);

	poll_wait(file, &av7110->aout.queue, wait);

	if (av7110->playing) {
		if (dvb_ringbuffer_free(&av7110->aout) >= 20 * 1024)
			mask |= (EPOLLOUT | EPOLLWRNORM);
	} else /* if not playing: may play if asked for */
		mask = (EPOLLOUT | EPOLLWRNORM);

	return mask;
}

static ssize_t dvb_audio_write(struct file *file, const char __user *buf,
			       size_t count, loff_t *ppos)
{
	struct dvb_device *dvbdev = file->private_data;
	struct av7110 *av7110 = dvbdev->priv;
	unsigned char c;

	dprintk(2, "av7110:%p, \n", av7110);

	if (av7110->audiostate.stream_source != AUDIO_SOURCE_MEMORY) {
		printk(KERN_ERR "not audio source memory\n");
		return -EPERM;
	}

	if (get_user(c, buf))
		return -EFAULT;
	if (c == 0x47 && count % TS_SIZE == 0)
		return ts_play(av7110, buf, count, file->f_flags & O_NONBLOCK, 0);
	else
		return dvb_aplay(av7110, buf, count, file->f_flags & O_NONBLOCK, 0);
}

static u8 iframe_header[] = { 0x00, 0x00, 0x01, 0xe0, 0x00, 0x00, 0x80, 0x00, 0x00 };

#define MIN_IFRAME 400000

static int play_iframe(struct av7110 *av7110, char __user *buf, unsigned int len, int nonblock)
{
	unsigned i, n;
	int progressive = 0;
	int match = 0;

	dprintk(2, "av7110:%p, \n", av7110);

	if (len == 0)
		return 0;

	if (!(av7110->playing & RP_VIDEO)) {
		if (av7110_av_start_play(av7110, RP_VIDEO) < 0)
			return -EBUSY;
	}

	/* search in buf for instances of 00 00 01 b5 1? */
	for (i = 0; i < len; i++) {
		unsigned char c;
		if (get_user(c, buf + i))
			return -EFAULT;
		if (match == 5) {
			progressive = c & 0x08;
			match = 0;
		}
		if (c == 0x00) {
			match = (match == 1 || match == 2) ? 2 : 1;
			continue;
		}
		switch (match++) {
		case 2: if (c == 0x01)
				continue;
			break;
		case 3: if (c == 0xb5)
				continue;
			break;
		case 4: if ((c & 0xf0) == 0x10)
				continue;
			break;
		}
		match = 0;
	}

	/* setting n always > 1, fixes problems when playing stillframes
	   consisting of I- and P-Frames */
	n = MIN_IFRAME / len + 1;

	/* FIXME: nonblock? */
	dvb_play_kernel(av7110, iframe_header, sizeof(iframe_header), 0, 1);

	for (i = 0; i < n; i++)
		dvb_play(av7110, buf, len, 0, 1);

	av7110_ipack_flush(&av7110->ipack[1]);

	if (progressive)
		return vidcom(av7110, AV_VIDEO_CMD_FREEZE, 1);
	else
		return 0;
}

#ifdef CONFIG_COMPAT
struct compat_video_still_picture {
	compat_uptr_t iFrame;
	int32_t size;
};
#define VIDEO_STILLPICTURE32 _IOW('o', 30, struct compat_video_still_picture)

struct compat_video_event {
	__s32 type;
	/* unused, make sure to use atomic time for y2038 if it ever gets used */
	compat_long_t timestamp;
	union {
		video_size_t size;
		unsigned int frame_rate;        /* in frames per 1000sec */
		unsigned char vsync_field;      /* unknown/odd/even/progressive */
	} u;
};
#define VIDEO_GET_EVENT32 _IOR('o', 28, struct compat_video_event)

static int dvb_compat_video_get_event(struct av7110 *av7110,
				      struct compat_video_event *event, int flags)
{
	struct video_event ev;
	int ret;

	ret = dvb_video_get_event(av7110, &ev, flags);

	*event = (struct compat_video_event) {
		.type = ev.type,
		.timestamp = ev.timestamp,
		.u.size = ev.u.size,
	};

	return ret;
}
#endif

static int dvb_video_ioctl(struct file *file,
			   unsigned int cmd, void *parg)
{
	struct dvb_device *dvbdev = file->private_data;
	struct av7110 *av7110 = dvbdev->priv;
	unsigned long arg = (unsigned long) parg;
	int ret = 0;

	dprintk(1, "av7110:%p, cmd=%04x\n", av7110,cmd);

	if ((file->f_flags & O_ACCMODE) == O_RDONLY) {
		if ( cmd != VIDEO_GET_STATUS && cmd != VIDEO_GET_EVENT &&
		     cmd != VIDEO_GET_SIZE ) {
			return -EPERM;
		}
	}

	if (mutex_lock_interruptible(&av7110->ioctl_mutex))
		return -ERESTARTSYS;

	switch (cmd) {
	case VIDEO_STOP:
		av7110->videostate.play_state = VIDEO_STOPPED;
		if (av7110->videostate.stream_source == VIDEO_SOURCE_MEMORY)
			ret = av7110_av_stop(av7110, RP_VIDEO);
		else
			ret = vidcom(av7110, AV_VIDEO_CMD_STOP,
			       av7110->videostate.video_blank ? 0 : 1);
		if (!ret)
			av7110->trickmode = TRICK_NONE;
		break;

	case VIDEO_PLAY:
		av7110->trickmode = TRICK_NONE;
		if (av7110->videostate.play_state == VIDEO_FREEZED) {
			av7110->videostate.play_state = VIDEO_PLAYING;
			ret = vidcom(av7110, AV_VIDEO_CMD_PLAY, 0);
			if (ret)
				break;
		}
		if (av7110->videostate.stream_source == VIDEO_SOURCE_MEMORY) {
			if (av7110->playing == RP_AV) {
				ret = av7110_fw_cmd(av7110, COMTYPE_REC_PLAY, __Stop, 0);
				if (ret)
					break;
				av7110->playing &= ~RP_VIDEO;
			}
			ret = av7110_av_start_play(av7110, RP_VIDEO);
		}
		if (!ret)
			ret = vidcom(av7110, AV_VIDEO_CMD_PLAY, 0);
		if (!ret)
			av7110->videostate.play_state = VIDEO_PLAYING;
		break;

	case VIDEO_FREEZE:
		av7110->videostate.play_state = VIDEO_FREEZED;
		if (av7110->playing & RP_VIDEO)
			ret = av7110_fw_cmd(av7110, COMTYPE_REC_PLAY, __Pause, 0);
		else
			ret = vidcom(av7110, AV_VIDEO_CMD_FREEZE, 1);
		if (!ret)
			av7110->trickmode = TRICK_FREEZE;
		break;

	case VIDEO_CONTINUE:
		if (av7110->playing & RP_VIDEO)
			ret = av7110_fw_cmd(av7110, COMTYPE_REC_PLAY, __Continue, 0);
		if (!ret)
			ret = vidcom(av7110, AV_VIDEO_CMD_PLAY, 0);
		if (!ret) {
			av7110->videostate.play_state = VIDEO_PLAYING;
			av7110->trickmode = TRICK_NONE;
		}
		break;

	case VIDEO_SELECT_SOURCE:
		av7110->videostate.stream_source = (video_stream_source_t) arg;
		break;

	case VIDEO_SET_BLANK:
		av7110->videostate.video_blank = (int) arg;
		break;

	case VIDEO_GET_STATUS:
		memcpy(parg, &av7110->videostate, sizeof(struct video_status));
		break;

#ifdef CONFIG_COMPAT
	case VIDEO_GET_EVENT32:
		ret = dvb_compat_video_get_event(av7110, parg, file->f_flags);
		break;
#endif

	case VIDEO_GET_EVENT:
		ret = dvb_video_get_event(av7110, parg, file->f_flags);
		break;

	case VIDEO_GET_SIZE:
		memcpy(parg, &av7110->video_size, sizeof(video_size_t));
		break;

	case VIDEO_SET_DISPLAY_FORMAT:
	{
		video_displayformat_t format = (video_displayformat_t) arg;
		switch (format) {
		case VIDEO_PAN_SCAN:
			av7110->display_panscan = VID_PAN_SCAN_PREF;
			break;
		case VIDEO_LETTER_BOX:
			av7110->display_panscan = VID_VC_AND_PS_PREF;
			break;
		case VIDEO_CENTER_CUT_OUT:
			av7110->display_panscan = VID_CENTRE_CUT_PREF;
			break;
		default:
			ret = -EINVAL;
		}
		if (ret < 0)
			break;
		av7110->videostate.display_format = format;
		ret = av7110_fw_cmd(av7110, COMTYPE_ENCODER, SetPanScanType,
				    1, av7110->display_panscan);
		break;
	}

	case VIDEO_SET_FORMAT:
		if (arg > 1) {
			ret = -EINVAL;
			break;
		}
		av7110->display_ar = arg;
		ret = av7110_fw_cmd(av7110, COMTYPE_ENCODER, SetMonitorType,
				    1, (u16) arg);
		break;

#ifdef CONFIG_COMPAT
	case VIDEO_STILLPICTURE32:
	{
		struct compat_video_still_picture *pic =
			(struct compat_video_still_picture *) parg;
		av7110->videostate.stream_source = VIDEO_SOURCE_MEMORY;
		dvb_ringbuffer_flush_spinlock_wakeup(&av7110->avout);
		ret = play_iframe(av7110, compat_ptr(pic->iFrame),
				  pic->size, file->f_flags & O_NONBLOCK);
		break;
	}
#endif

	case VIDEO_STILLPICTURE:
	{
		struct video_still_picture *pic =
			(struct video_still_picture *) parg;
		av7110->videostate.stream_source = VIDEO_SOURCE_MEMORY;
		dvb_ringbuffer_flush_spinlock_wakeup(&av7110->avout);
		ret = play_iframe(av7110, pic->iFrame, pic->size,
				  file->f_flags & O_NONBLOCK);
		break;
	}

	case VIDEO_FAST_FORWARD:
		//note: arg is ignored by firmware
		if (av7110->playing & RP_VIDEO)
			ret = av7110_fw_cmd(av7110, COMTYPE_REC_PLAY,
					    __Scan_I, 2, AV_PES, 0);
		else
			ret = vidcom(av7110, AV_VIDEO_CMD_FFWD, arg);
		if (!ret) {
			av7110->trickmode = TRICK_FAST;
			av7110->videostate.play_state = VIDEO_PLAYING;
		}
		break;

	case VIDEO_SLOWMOTION:
		if (av7110->playing&RP_VIDEO) {
			if (av7110->trickmode != TRICK_SLOW)
				ret = av7110_fw_cmd(av7110, COMTYPE_REC_PLAY, __Slow, 2, 0, 0);
			if (!ret)
				ret = vidcom(av7110, AV_VIDEO_CMD_SLOW, arg);
		} else {
			ret = vidcom(av7110, AV_VIDEO_CMD_PLAY, 0);
			if (!ret)
				ret = vidcom(av7110, AV_VIDEO_CMD_STOP, 0);
			if (!ret)
				ret = vidcom(av7110, AV_VIDEO_CMD_SLOW, arg);
		}
		if (!ret) {
			av7110->trickmode = TRICK_SLOW;
			av7110->videostate.play_state = VIDEO_PLAYING;
		}
		break;

	case VIDEO_GET_CAPABILITIES:
		*(int *)parg = VIDEO_CAP_MPEG1 | VIDEO_CAP_MPEG2 |
			VIDEO_CAP_SYS | VIDEO_CAP_PROG;
		break;

	case VIDEO_CLEAR_BUFFER:
		dvb_ringbuffer_flush_spinlock_wakeup(&av7110->avout);
		av7110_ipack_reset(&av7110->ipack[1]);
		if (av7110->playing == RP_AV) {
			ret = av7110_fw_cmd(av7110, COMTYPE_REC_PLAY,
					    __Play, 2, AV_PES, 0);
			if (ret)
				break;
			if (av7110->trickmode == TRICK_FAST)
				ret = av7110_fw_cmd(av7110, COMTYPE_REC_PLAY,
						    __Scan_I, 2, AV_PES, 0);
			if (av7110->trickmode == TRICK_SLOW) {
				ret = av7110_fw_cmd(av7110, COMTYPE_REC_PLAY,
						    __Slow, 2, 0, 0);
				if (!ret)
					ret = vidcom(av7110, AV_VIDEO_CMD_SLOW, arg);
			}
			if (av7110->trickmode == TRICK_FREEZE)
				ret = vidcom(av7110, AV_VIDEO_CMD_STOP, 1);
		}
		break;

	case VIDEO_SET_STREAMTYPE:
		break;

	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	mutex_unlock(&av7110->ioctl_mutex);
	return ret;
}

static int dvb_audio_ioctl(struct file *file,
			   unsigned int cmd, void *parg)
{
	struct dvb_device *dvbdev = file->private_data;
	struct av7110 *av7110 = dvbdev->priv;
	unsigned long arg = (unsigned long) parg;
	int ret = 0;

	dprintk(1, "av7110:%p, cmd=%04x\n", av7110,cmd);

	if (((file->f_flags & O_ACCMODE) == O_RDONLY) &&
	    (cmd != AUDIO_GET_STATUS))
		return -EPERM;

	if (mutex_lock_interruptible(&av7110->ioctl_mutex))
		return -ERESTARTSYS;

	switch (cmd) {
	case AUDIO_STOP:
		if (av7110->audiostate.stream_source == AUDIO_SOURCE_MEMORY)
			ret = av7110_av_stop(av7110, RP_AUDIO);
		else
			ret = audcom(av7110, AUDIO_CMD_MUTE);
		if (!ret)
			av7110->audiostate.play_state = AUDIO_STOPPED;
		break;

	case AUDIO_PLAY:
		if (av7110->audiostate.stream_source == AUDIO_SOURCE_MEMORY)
			ret = av7110_av_start_play(av7110, RP_AUDIO);
		if (!ret)
			ret = audcom(av7110, AUDIO_CMD_UNMUTE);
		if (!ret)
			av7110->audiostate.play_state = AUDIO_PLAYING;
		break;

	case AUDIO_PAUSE:
		ret = audcom(av7110, AUDIO_CMD_MUTE);
		if (!ret)
			av7110->audiostate.play_state = AUDIO_PAUSED;
		break;

	case AUDIO_CONTINUE:
		if (av7110->audiostate.play_state == AUDIO_PAUSED) {
			av7110->audiostate.play_state = AUDIO_PLAYING;
			ret = audcom(av7110, AUDIO_CMD_UNMUTE | AUDIO_CMD_PCM16);
		}
		break;

	case AUDIO_SELECT_SOURCE:
		av7110->audiostate.stream_source = (audio_stream_source_t) arg;
		break;

	case AUDIO_SET_MUTE:
	{
		ret = audcom(av7110, arg ? AUDIO_CMD_MUTE : AUDIO_CMD_UNMUTE);
		if (!ret)
			av7110->audiostate.mute_state = (int) arg;
		break;
	}

	case AUDIO_SET_AV_SYNC:
		av7110->audiostate.AV_sync_state = (int) arg;
		ret = audcom(av7110, arg ? AUDIO_CMD_SYNC_ON : AUDIO_CMD_SYNC_OFF);
		break;

	case AUDIO_SET_BYPASS_MODE:
		if (FW_VERSION(av7110->arm_app) < 0x2621)
			ret = -EINVAL;
		av7110->audiostate.bypass_mode = (int)arg;
		break;

	case AUDIO_CHANNEL_SELECT:
		av7110->audiostate.channel_select = (audio_channel_select_t) arg;
		switch(av7110->audiostate.channel_select) {
		case AUDIO_STEREO:
			ret = audcom(av7110, AUDIO_CMD_STEREO);
			if (!ret) {
				if (av7110->adac_type == DVB_ADAC_CRYSTAL)
					i2c_writereg(av7110, 0x20, 0x02, 0x49);
				else if (av7110->adac_type == DVB_ADAC_MSP34x5)
					msp_writereg(av7110, MSP_WR_DSP, 0x0008, 0x0220);
			}
			break;
		case AUDIO_MONO_LEFT:
			ret = audcom(av7110, AUDIO_CMD_MONO_L);
			if (!ret) {
				if (av7110->adac_type == DVB_ADAC_CRYSTAL)
					i2c_writereg(av7110, 0x20, 0x02, 0x4a);
				else if (av7110->adac_type == DVB_ADAC_MSP34x5)
					msp_writereg(av7110, MSP_WR_DSP, 0x0008, 0x0200);
			}
			break;
		case AUDIO_MONO_RIGHT:
			ret = audcom(av7110, AUDIO_CMD_MONO_R);
			if (!ret) {
				if (av7110->adac_type == DVB_ADAC_CRYSTAL)
					i2c_writereg(av7110, 0x20, 0x02, 0x45);
				else if (av7110->adac_type == DVB_ADAC_MSP34x5)
					msp_writereg(av7110, MSP_WR_DSP, 0x0008, 0x0210);
			}
			break;
		default:
			ret = -EINVAL;
			break;
		}
		break;

	case AUDIO_GET_STATUS:
		memcpy(parg, &av7110->audiostate, sizeof(struct audio_status));
		break;

	case AUDIO_GET_CAPABILITIES:
		if (FW_VERSION(av7110->arm_app) < 0x2621)
			*(unsigned int *)parg = AUDIO_CAP_LPCM | AUDIO_CAP_MP1 | AUDIO_CAP_MP2;
		else
			*(unsigned int *)parg = AUDIO_CAP_LPCM | AUDIO_CAP_DTS | AUDIO_CAP_AC3 |
						AUDIO_CAP_MP1 | AUDIO_CAP_MP2;
		break;

	case AUDIO_CLEAR_BUFFER:
		dvb_ringbuffer_flush_spinlock_wakeup(&av7110->aout);
		av7110_ipack_reset(&av7110->ipack[0]);
		if (av7110->playing == RP_AV)
			ret = av7110_fw_cmd(av7110, COMTYPE_REC_PLAY,
					    __Play, 2, AV_PES, 0);
		break;

	case AUDIO_SET_ID:
		break;

	case AUDIO_SET_MIXER:
	{
		struct audio_mixer *amix = (struct audio_mixer *)parg;
		ret = av7110_set_volume(av7110, amix->volume_left, amix->volume_right);
		break;
	}

	case AUDIO_SET_STREAMTYPE:
		break;

	default:
		ret = -ENOIOCTLCMD;
	}

	mutex_unlock(&av7110->ioctl_mutex);
	return ret;
}


static int dvb_video_open(struct inode *inode, struct file *file)
{
	struct dvb_device *dvbdev = file->private_data;
	struct av7110 *av7110 = dvbdev->priv;
	int err;

	dprintk(2, "av7110:%p, \n", av7110);

	if ((err = dvb_generic_open(inode, file)) < 0)
		return err;

	if ((file->f_flags & O_ACCMODE) != O_RDONLY) {
		dvb_ringbuffer_flush_spinlock_wakeup(&av7110->aout);
		dvb_ringbuffer_flush_spinlock_wakeup(&av7110->avout);
		av7110->video_blank = 1;
		av7110->audiostate.AV_sync_state = 1;
		av7110->videostate.stream_source = VIDEO_SOURCE_DEMUX;

		/*  empty event queue */
		av7110->video_events.eventr = av7110->video_events.eventw = 0;
	}

	return 0;
}

static int dvb_video_release(struct inode *inode, struct file *file)
{
	struct dvb_device *dvbdev = file->private_data;
	struct av7110 *av7110 = dvbdev->priv;

	dprintk(2, "av7110:%p, \n", av7110);

	if ((file->f_flags & O_ACCMODE) != O_RDONLY) {
		av7110_av_stop(av7110, RP_VIDEO);
	}

	return dvb_generic_release(inode, file);
}

static int dvb_audio_open(struct inode *inode, struct file *file)
{
	struct dvb_device *dvbdev = file->private_data;
	struct av7110 *av7110 = dvbdev->priv;
	int err = dvb_generic_open(inode, file);

	dprintk(2, "av7110:%p, \n", av7110);

	if (err < 0)
		return err;
	dvb_ringbuffer_flush_spinlock_wakeup(&av7110->aout);
	av7110->audiostate.stream_source = AUDIO_SOURCE_DEMUX;
	return 0;
}

static int dvb_audio_release(struct inode *inode, struct file *file)
{
	struct dvb_device *dvbdev = file->private_data;
	struct av7110 *av7110 = dvbdev->priv;

	dprintk(2, "av7110:%p, \n", av7110);

	av7110_av_stop(av7110, RP_AUDIO);
	return dvb_generic_release(inode, file);
}



/******************************************************************************
 * driver registration
 ******************************************************************************/

static const struct file_operations dvb_video_fops = {
	.owner		= THIS_MODULE,
	.write		= dvb_video_write,
	.unlocked_ioctl	= dvb_generic_ioctl,
	.compat_ioctl	= dvb_generic_ioctl,
	.open		= dvb_video_open,
	.release	= dvb_video_release,
	.poll		= dvb_video_poll,
	.llseek		= noop_llseek,
};

static struct dvb_device dvbdev_video = {
	.priv		= NULL,
	.users		= 6,
	.readers	= 5,	/* arbitrary */
	.writers	= 1,
	.fops		= &dvb_video_fops,
	.kernel_ioctl	= dvb_video_ioctl,
};

static const struct file_operations dvb_audio_fops = {
	.owner		= THIS_MODULE,
	.write		= dvb_audio_write,
	.unlocked_ioctl	= dvb_generic_ioctl,
	.compat_ioctl	= dvb_generic_ioctl,
	.open		= dvb_audio_open,
	.release	= dvb_audio_release,
	.poll		= dvb_audio_poll,
	.llseek		= noop_llseek,
};

static struct dvb_device dvbdev_audio = {
	.priv		= NULL,
	.users		= 1,
	.writers	= 1,
	.fops		= &dvb_audio_fops,
	.kernel_ioctl	= dvb_audio_ioctl,
};


int av7110_av_register(struct av7110 *av7110)
{
	av7110->audiostate.AV_sync_state = 0;
	av7110->audiostate.mute_state = 0;
	av7110->audiostate.play_state = AUDIO_STOPPED;
	av7110->audiostate.stream_source = AUDIO_SOURCE_DEMUX;
	av7110->audiostate.channel_select = AUDIO_STEREO;
	av7110->audiostate.bypass_mode = 0;

	av7110->videostate.video_blank = 0;
	av7110->videostate.play_state = VIDEO_STOPPED;
	av7110->videostate.stream_source = VIDEO_SOURCE_DEMUX;
	av7110->videostate.video_format = VIDEO_FORMAT_4_3;
	av7110->videostate.display_format = VIDEO_LETTER_BOX;
	av7110->display_ar = VIDEO_FORMAT_4_3;
	av7110->display_panscan = VID_VC_AND_PS_PREF;

	init_waitqueue_head(&av7110->video_events.wait_queue);
	spin_lock_init(&av7110->video_events.lock);
	av7110->video_events.eventw = av7110->video_events.eventr = 0;
	av7110->video_events.overflow = 0;
	memset(&av7110->video_size, 0, sizeof (video_size_t));

	dvb_register_device(&av7110->dvb_adapter, &av7110->video_dev,
			    &dvbdev_video, av7110, DVB_DEVICE_VIDEO, 0);

	dvb_register_device(&av7110->dvb_adapter, &av7110->audio_dev,
			    &dvbdev_audio, av7110, DVB_DEVICE_AUDIO, 0);

	return 0;
}

void av7110_av_unregister(struct av7110 *av7110)
{
	dvb_unregister_device(av7110->audio_dev);
	dvb_unregister_device(av7110->video_dev);
}

int av7110_av_init(struct av7110 *av7110)
{
	void (*play[])(u8 *, int, void *) = { play_audio_cb, play_video_cb };
	int i, ret;

	for (i = 0; i < 2; i++) {
		struct ipack *ipack = av7110->ipack + i;

		ret = av7110_ipack_init(ipack, IPACKS, play[i]);
		if (ret < 0) {
			if (i)
				av7110_ipack_free(--ipack);
			goto out;
		}
		ipack->data = av7110;
	}

	dvb_ringbuffer_init(&av7110->avout, av7110->iobuf, AVOUTLEN);
	dvb_ringbuffer_init(&av7110->aout, av7110->iobuf + AVOUTLEN, AOUTLEN);

	av7110->kbuf[0] = (u8 *)(av7110->iobuf + AVOUTLEN + AOUTLEN + BMPLEN);
	av7110->kbuf[1] = av7110->kbuf[0] + 2 * IPACKS;
out:
	return ret;
}

void av7110_av_exit(struct av7110 *av7110)
{
	av7110_ipack_free(&av7110->ipack[0]);
	av7110_ipack_free(&av7110->ipack[1]);
}
