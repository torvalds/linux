// SPDX-License-Identifier: GPL-2.0
#include "dvb_filter.h"
#include "av7110_ipack.h"
#include <linux/string.h>	/* for memcpy() */
#include <linux/vmalloc.h>


void av7110_ipack_reset(struct ipack *p)
{
	p->found = 0;
	p->cid = 0;
	p->plength = 0;
	p->flag1 = 0;
	p->flag2 = 0;
	p->hlength = 0;
	p->mpeg = 0;
	p->check = 0;
	p->which = 0;
	p->done = 0;
	p->count = 0;
}


int av7110_ipack_init(struct ipack *p, int size,
		      void (*func)(u8 *buf, int size, void *priv))
{
	if (!(p->buf = vmalloc(size))) {
		printk(KERN_WARNING "Couldn't allocate memory for ipack\n");
		return -ENOMEM;
	}
	p->size = size;
	p->func = func;
	p->repack_subids = 0;
	av7110_ipack_reset(p);
	return 0;
}


void av7110_ipack_free(struct ipack *p)
{
	vfree(p->buf);
}


static void send_ipack(struct ipack *p)
{
	int off;
	struct dvb_audio_info ai;
	int ac3_off = 0;
	int streamid = 0;
	int nframes = 0;
	int f = 0;

	switch (p->mpeg) {
	case 2:
		if (p->count < 10)
			return;
		p->buf[3] = p->cid;
		p->buf[4] = (u8)(((p->count - 6) & 0xff00) >> 8);
		p->buf[5] = (u8)((p->count - 6) & 0x00ff);
		if (p->repack_subids && p->cid == PRIVATE_STREAM1) {
			off = 9 + p->buf[8];
			streamid = p->buf[off];
			if ((streamid & 0xf8) == 0x80) {
				ai.off = 0;
				ac3_off = ((p->buf[off + 2] << 8)|
					   p->buf[off + 3]);
				if (ac3_off < p->count)
					f = dvb_filter_get_ac3info(p->buf + off + 3 + ac3_off,
								   p->count - ac3_off, &ai, 0);
				if (!f) {
					nframes = (p->count - off - 3 - ac3_off) /
						ai.framesize + 1;
					p->buf[off + 2] = (ac3_off >> 8) & 0xff;
					p->buf[off + 3] = (ac3_off) & 0xff;
					p->buf[off + 1] = nframes;
					ac3_off +=  nframes * ai.framesize - p->count;
				}
			}
		}
		p->func(p->buf, p->count, p->data);

		p->buf[6] = 0x80;
		p->buf[7] = 0x00;
		p->buf[8] = 0x00;
		p->count = 9;
		if (p->repack_subids && p->cid == PRIVATE_STREAM1
		    && (streamid & 0xf8) == 0x80) {
			p->count += 4;
			p->buf[9] = streamid;
			p->buf[10] = (ac3_off >> 8) & 0xff;
			p->buf[11] = (ac3_off) & 0xff;
			p->buf[12] = 0;
		}
		break;

	case 1:
		if (p->count < 8)
			return;
		p->buf[3] = p->cid;
		p->buf[4] = (u8)(((p->count - 6) & 0xff00) >> 8);
		p->buf[5] = (u8)((p->count - 6) & 0x00ff);
		p->func(p->buf, p->count, p->data);

		p->buf[6] = 0x0f;
		p->count = 7;
		break;
	}
}


void av7110_ipack_flush(struct ipack *p)
{
	if (p->plength != MMAX_PLENGTH - 6 || p->found <= 6)
		return;
	p->plength = p->found - 6;
	p->found = 0;
	send_ipack(p);
	av7110_ipack_reset(p);
}


static void write_ipack(struct ipack *p, const u8 *data, int count)
{
	u8 headr[3] = { 0x00, 0x00, 0x01 };

	if (p->count < 6) {
		memcpy(p->buf, headr, 3);
		p->count = 6;
	}

	if (p->count + count < p->size){
		memcpy(p->buf+p->count, data, count);
		p->count += count;
	} else {
		int rest = p->size - p->count;
		memcpy(p->buf+p->count, data, rest);
		p->count += rest;
		send_ipack(p);
		if (count - rest > 0)
			write_ipack(p, data + rest, count - rest);
	}
}


int av7110_ipack_instant_repack (const u8 *buf, int count, struct ipack *p)
{
	int l;
	int c = 0;

	while (c < count && (p->mpeg == 0 ||
			     (p->mpeg == 1 && p->found < 7) ||
			     (p->mpeg == 2 && p->found < 9))
	       &&  (p->found < 5 || !p->done)) {
		switch (p->found) {
		case 0:
		case 1:
			if (buf[c] == 0x00)
				p->found++;
			else
				p->found = 0;
			c++;
			break;
		case 2:
			if (buf[c] == 0x01)
				p->found++;
			else if (buf[c] == 0)
				p->found = 2;
			else
				p->found = 0;
			c++;
			break;
		case 3:
			p->cid = 0;
			switch (buf[c]) {
			case PROG_STREAM_MAP:
			case PRIVATE_STREAM2:
			case PROG_STREAM_DIR:
			case ECM_STREAM     :
			case EMM_STREAM     :
			case PADDING_STREAM :
			case DSM_CC_STREAM  :
			case ISO13522_STREAM:
				p->done = 1;
				fallthrough;
			case PRIVATE_STREAM1:
			case VIDEO_STREAM_S ... VIDEO_STREAM_E:
			case AUDIO_STREAM_S ... AUDIO_STREAM_E:
				p->found++;
				p->cid = buf[c];
				c++;
				break;
			default:
				p->found = 0;
				break;
			}
			break;

		case 4:
			if (count-c > 1) {
				p->plen[0] = buf[c];
				c++;
				p->plen[1] = buf[c];
				c++;
				p->found += 2;
				p->plength = (p->plen[0] << 8) | p->plen[1];
			} else {
				p->plen[0] = buf[c];
				p->found++;
				return count;
			}
			break;
		case 5:
			p->plen[1] = buf[c];
			c++;
			p->found++;
			p->plength = (p->plen[0] << 8) | p->plen[1];
			break;
		case 6:
			if (!p->done) {
				p->flag1 = buf[c];
				c++;
				p->found++;
				if ((p->flag1 & 0xc0) == 0x80)
					p->mpeg = 2;
				else {
					p->hlength = 0;
					p->which = 0;
					p->mpeg = 1;
					p->flag2 = 0;
				}
			}
			break;

		case 7:
			if (!p->done && p->mpeg == 2) {
				p->flag2 = buf[c];
				c++;
				p->found++;
			}
			break;

		case 8:
			if (!p->done && p->mpeg == 2) {
				p->hlength = buf[c];
				c++;
				p->found++;
			}
			break;
		}
	}

	if (c == count)
		return count;

	if (!p->plength)
		p->plength = MMAX_PLENGTH - 6;

	if (p->done || ((p->mpeg == 2 && p->found >= 9) ||
			(p->mpeg == 1 && p->found >= 7))) {
		switch (p->cid) {
		case AUDIO_STREAM_S ... AUDIO_STREAM_E:
		case VIDEO_STREAM_S ... VIDEO_STREAM_E:
		case PRIVATE_STREAM1:
			if (p->mpeg == 2 && p->found == 9) {
				write_ipack(p, &p->flag1, 1);
				write_ipack(p, &p->flag2, 1);
				write_ipack(p, &p->hlength, 1);
			}

			if (p->mpeg == 1 && p->found == 7)
				write_ipack(p, &p->flag1, 1);

			if (p->mpeg == 2 && (p->flag2 & PTS_ONLY) &&
			    p->found < 14) {
				while (c < count && p->found < 14) {
					p->pts[p->found - 9] = buf[c];
					write_ipack(p, buf + c, 1);
					c++;
					p->found++;
				}
				if (c == count)
					return count;
			}

			if (p->mpeg == 1 && p->which < 2000) {

				if (p->found == 7) {
					p->check = p->flag1;
					p->hlength = 1;
				}

				while (!p->which && c < count &&
				       p->check == 0xff){
					p->check = buf[c];
					write_ipack(p, buf + c, 1);
					c++;
					p->found++;
					p->hlength++;
				}

				if (c == count)
					return count;

				if ((p->check & 0xc0) == 0x40 && !p->which) {
					p->check = buf[c];
					write_ipack(p, buf + c, 1);
					c++;
					p->found++;
					p->hlength++;

					p->which = 1;
					if (c == count)
						return count;
					p->check = buf[c];
					write_ipack(p, buf + c, 1);
					c++;
					p->found++;
					p->hlength++;
					p->which = 2;
					if (c == count)
						return count;
				}

				if (p->which == 1) {
					p->check = buf[c];
					write_ipack(p, buf + c, 1);
					c++;
					p->found++;
					p->hlength++;
					p->which = 2;
					if (c == count)
						return count;
				}

				if ((p->check & 0x30) && p->check != 0xff) {
					p->flag2 = (p->check & 0xf0) << 2;
					p->pts[0] = p->check;
					p->which = 3;
				}

				if (c == count)
					return count;
				if (p->which > 2){
					if ((p->flag2 & PTS_DTS_FLAGS) == PTS_ONLY) {
						while (c < count && p->which < 7) {
							p->pts[p->which - 2] = buf[c];
							write_ipack(p, buf + c, 1);
							c++;
							p->found++;
							p->which++;
							p->hlength++;
						}
						if (c == count)
							return count;
					} else if ((p->flag2 & PTS_DTS_FLAGS) == PTS_DTS) {
						while (c < count && p->which < 12) {
							if (p->which < 7)
								p->pts[p->which - 2] = buf[c];
							write_ipack(p, buf + c, 1);
							c++;
							p->found++;
							p->which++;
							p->hlength++;
						}
						if (c == count)
							return count;
					}
					p->which = 2000;
				}

			}

			while (c < count && p->found < p->plength + 6) {
				l = count - c;
				if (l + p->found > p->plength + 6)
					l = p->plength + 6 - p->found;
				write_ipack(p, buf + c, l);
				p->found += l;
				c += l;
			}
			break;
		}


		if (p->done) {
			if (p->found + count - c < p->plength + 6) {
				p->found += count - c;
				c = count;
			} else {
				c += p->plength + 6 - p->found;
				p->found = p->plength + 6;
			}
		}

		if (p->plength && p->found == p->plength + 6) {
			send_ipack(p);
			av7110_ipack_reset(p);
			if (c < count)
				av7110_ipack_instant_repack(buf + c, count - c, p);
		}
	}
	return count;
}
