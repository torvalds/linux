// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include "dvb_filter.h"

static u32 freq[4] = {480, 441, 320, 0};

static unsigned int ac3_bitrates[32] = {
	32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, 448, 512, 576, 640,
	 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static u32 ac3_frames[3][32] = {
	{64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, 448, 512, 640, 768, 896, 1024,
	 1152, 1280, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{69, 87, 104, 121, 139, 174, 208, 243, 278, 348, 417, 487, 557, 696, 835, 975, 1114,
	 1253, 1393, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{96, 120, 144, 168, 192, 240, 288, 336, 384, 480, 576, 672, 768, 960, 1152, 1344,
	 1536, 1728, 1920, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
};

int dvb_filter_get_ac3info(u8 *mbuf, int count, struct dvb_audio_info *ai, int pr)
{
	u8 *headr;
	int found = 0;
	int c = 0;
	u8 frame = 0;
	int fr = 0;

	while (!found  && c < count) {
		u8 *b = mbuf + c;

		if (b[0] == 0x0b &&  b[1] == 0x77)
			found = 1;
		else
			c++;
	}

	if (!found)
		return -1;

	ai->off = c;
	if (c + 5 >= count)
		return -1;

	ai->layer = 0;  // 0 for AC3
	headr = mbuf + c + 2;

	frame = (headr[2] & 0x3f);
	ai->bit_rate = ac3_bitrates[frame >> 1] * 1000;

	ai->frequency = (headr[2] & 0xc0) >> 6;
	fr = (headr[2] & 0xc0) >> 6;
	ai->frequency = freq[fr] * 100;

	ai->framesize = ac3_frames[fr][frame >> 1];
	if ((frame & 1) && (fr == 1))
		ai->framesize++;
	ai->framesize = ai->framesize << 1;

	if (pr)
		pr_info("Audiostream: AC3, BRate: %d kb/s, Freq: %d Hz, Framesize %d\n",
			(int)ai->bit_rate / 1000, (int)ai->frequency, (int)ai->framesize);

	return 0;
}

void dvb_filter_pes2ts_init(struct dvb_filter_pes2ts *p2ts, unsigned short pid,
			    dvb_filter_pes2ts_cb_t *cb, void *priv)
{
	unsigned char *buf = p2ts->buf;

	buf[0] = 0x47;
	buf[1] = (pid >> 8);
	buf[2] = pid & 0xff;
	p2ts->cc = 0;
	p2ts->cb = cb;
	p2ts->priv = priv;
}

int dvb_filter_pes2ts(struct dvb_filter_pes2ts *p2ts, unsigned char *pes,
		      int len, int payload_start)
{
	unsigned char *buf = p2ts->buf;
	int ret = 0, rest;

	//len=6+((pes[4]<<8)|pes[5]);

	if (payload_start)
		buf[1] |= 0x40;
	else
		buf[1] &= ~0x40;
	while (len >= 184) {
		buf[3] = 0x10 | ((p2ts->cc++) & 0x0f);
		memcpy(buf + 4, pes, 184);
		ret = p2ts->cb(p2ts->priv, buf);
		if (ret)
			return ret;
		len -= 184; pes += 184;
		buf[1] &= ~0x40;
	}
	if (!len)
		return 0;
	buf[3] = 0x30 | ((p2ts->cc++) & 0x0f);
	rest = 183 - len;
	if (rest) {
		buf[5] = 0x00;
		if (rest - 1)
			memset(buf + 6, 0xff, rest - 1);
	}
	buf[4] = rest;
	memcpy(buf + 5 + rest, pes, len);
	return p2ts->cb(p2ts->priv, buf);
}
