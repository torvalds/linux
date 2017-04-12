/*
 *
 *
 *  Copyright (C) 2005 Mike Isely <isely@pobox.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 */

#include <linux/string.h>
#include "pvrusb2-debugifc.h"
#include "pvrusb2-hdw.h"
#include "pvrusb2-debug.h"

struct debugifc_mask_item {
	const char *name;
	unsigned long msk;
};


static unsigned int debugifc_count_whitespace(const char *buf,
					      unsigned int count)
{
	unsigned int scnt;
	char ch;

	for (scnt = 0; scnt < count; scnt++) {
		ch = buf[scnt];
		if (ch == ' ') continue;
		if (ch == '\t') continue;
		if (ch == '\n') continue;
		break;
	}
	return scnt;
}


static unsigned int debugifc_count_nonwhitespace(const char *buf,
						 unsigned int count)
{
	unsigned int scnt;
	char ch;

	for (scnt = 0; scnt < count; scnt++) {
		ch = buf[scnt];
		if (ch == ' ') break;
		if (ch == '\t') break;
		if (ch == '\n') break;
	}
	return scnt;
}


static unsigned int debugifc_isolate_word(const char *buf,unsigned int count,
					  const char **wstrPtr,
					  unsigned int *wlenPtr)
{
	const char *wptr;
	unsigned int consume_cnt = 0;
	unsigned int wlen;
	unsigned int scnt;

	wptr = NULL;
	wlen = 0;
	scnt = debugifc_count_whitespace(buf,count);
	consume_cnt += scnt; count -= scnt; buf += scnt;
	if (!count) goto done;

	scnt = debugifc_count_nonwhitespace(buf,count);
	if (!scnt) goto done;
	wptr = buf;
	wlen = scnt;
	consume_cnt += scnt; count -= scnt; buf += scnt;

 done:
	*wstrPtr = wptr;
	*wlenPtr = wlen;
	return consume_cnt;
}


static int debugifc_parse_unsigned_number(const char *buf,unsigned int count,
					  u32 *num_ptr)
{
	u32 result = 0;
	int radix = 10;
	if ((count >= 2) && (buf[0] == '0') &&
	    ((buf[1] == 'x') || (buf[1] == 'X'))) {
		radix = 16;
		count -= 2;
		buf += 2;
	} else if ((count >= 1) && (buf[0] == '0')) {
		radix = 8;
	}

	while (count--) {
		int val = hex_to_bin(*buf++);
		if (val < 0 || val >= radix)
			return -EINVAL;
		result *= radix;
		result += val;
	}
	*num_ptr = result;
	return 0;
}


static int debugifc_match_keyword(const char *buf,unsigned int count,
				  const char *keyword)
{
	unsigned int kl;
	if (!keyword) return 0;
	kl = strlen(keyword);
	if (kl != count) return 0;
	return !memcmp(buf,keyword,kl);
}


int pvr2_debugifc_print_info(struct pvr2_hdw *hdw,char *buf,unsigned int acnt)
{
	int bcnt = 0;
	int ccnt;
	ccnt = scnprintf(buf, acnt, "Driver hardware description: %s\n",
			 pvr2_hdw_get_desc(hdw));
	bcnt += ccnt; acnt -= ccnt; buf += ccnt;
	ccnt = scnprintf(buf,acnt,"Driver state info:\n");
	bcnt += ccnt; acnt -= ccnt; buf += ccnt;
	ccnt = pvr2_hdw_state_report(hdw,buf,acnt);
	bcnt += ccnt; acnt -= ccnt; buf += ccnt;

	return bcnt;
}


int pvr2_debugifc_print_status(struct pvr2_hdw *hdw,
			       char *buf,unsigned int acnt)
{
	int bcnt = 0;
	int ccnt;
	int ret;
	u32 gpio_dir,gpio_in,gpio_out;
	struct pvr2_stream_stats stats;
	struct pvr2_stream *sp;

	ret = pvr2_hdw_is_hsm(hdw);
	ccnt = scnprintf(buf,acnt,"USB link speed: %s\n",
			 (ret < 0 ? "FAIL" : (ret ? "high" : "full")));
	bcnt += ccnt; acnt -= ccnt; buf += ccnt;

	gpio_dir = 0; gpio_in = 0; gpio_out = 0;
	pvr2_hdw_gpio_get_dir(hdw,&gpio_dir);
	pvr2_hdw_gpio_get_out(hdw,&gpio_out);
	pvr2_hdw_gpio_get_in(hdw,&gpio_in);
	ccnt = scnprintf(buf,acnt,"GPIO state: dir=0x%x in=0x%x out=0x%x\n",
			 gpio_dir,gpio_in,gpio_out);
	bcnt += ccnt; acnt -= ccnt; buf += ccnt;

	ccnt = scnprintf(buf,acnt,"Streaming is %s\n",
			 pvr2_hdw_get_streaming(hdw) ? "on" : "off");
	bcnt += ccnt; acnt -= ccnt; buf += ccnt;


	sp = pvr2_hdw_get_video_stream(hdw);
	if (sp) {
		pvr2_stream_get_stats(sp, &stats, 0);
		ccnt = scnprintf(
			buf,acnt,
			"Bytes streamed=%u URBs: queued=%u idle=%u ready=%u processed=%u failed=%u\n",
			stats.bytes_processed,
			stats.buffers_in_queue,
			stats.buffers_in_idle,
			stats.buffers_in_ready,
			stats.buffers_processed,
			stats.buffers_failed);
		bcnt += ccnt; acnt -= ccnt; buf += ccnt;
	}

	return bcnt;
}


static int pvr2_debugifc_do1cmd(struct pvr2_hdw *hdw,const char *buf,
				unsigned int count)
{
	const char *wptr;
	unsigned int wlen;
	unsigned int scnt;

	scnt = debugifc_isolate_word(buf,count,&wptr,&wlen);
	if (!scnt) return 0;
	count -= scnt; buf += scnt;
	if (!wptr) return 0;

	pvr2_trace(PVR2_TRACE_DEBUGIFC,"debugifc cmd: \"%.*s\"",wlen,wptr);
	if (debugifc_match_keyword(wptr,wlen,"reset")) {
		scnt = debugifc_isolate_word(buf,count,&wptr,&wlen);
		if (!scnt) return -EINVAL;
		count -= scnt; buf += scnt;
		if (!wptr) return -EINVAL;
		if (debugifc_match_keyword(wptr,wlen,"cpu")) {
			pvr2_hdw_cpureset_assert(hdw,!0);
			pvr2_hdw_cpureset_assert(hdw,0);
			return 0;
		} else if (debugifc_match_keyword(wptr,wlen,"bus")) {
			pvr2_hdw_device_reset(hdw);
		} else if (debugifc_match_keyword(wptr,wlen,"soft")) {
			return pvr2_hdw_cmd_powerup(hdw);
		} else if (debugifc_match_keyword(wptr,wlen,"deep")) {
			return pvr2_hdw_cmd_deep_reset(hdw);
		} else if (debugifc_match_keyword(wptr,wlen,"firmware")) {
			return pvr2_upload_firmware2(hdw);
		} else if (debugifc_match_keyword(wptr,wlen,"decoder")) {
			return pvr2_hdw_cmd_decoder_reset(hdw);
		} else if (debugifc_match_keyword(wptr,wlen,"worker")) {
			return pvr2_hdw_untrip(hdw);
		} else if (debugifc_match_keyword(wptr,wlen,"usbstats")) {
			pvr2_stream_get_stats(pvr2_hdw_get_video_stream(hdw),
					      NULL, !0);
			return 0;
		}
		return -EINVAL;
	} else if (debugifc_match_keyword(wptr,wlen,"cpufw")) {
		scnt = debugifc_isolate_word(buf,count,&wptr,&wlen);
		if (!scnt) return -EINVAL;
		count -= scnt; buf += scnt;
		if (!wptr) return -EINVAL;
		if (debugifc_match_keyword(wptr,wlen,"fetch")) {
			scnt = debugifc_isolate_word(buf,count,&wptr,&wlen);
			if (scnt && wptr) {
				count -= scnt; buf += scnt;
				if (debugifc_match_keyword(wptr, wlen,
							   "prom")) {
					pvr2_hdw_cpufw_set_enabled(hdw, 2, !0);
				} else if (debugifc_match_keyword(wptr, wlen,
								  "ram8k")) {
					pvr2_hdw_cpufw_set_enabled(hdw, 0, !0);
				} else if (debugifc_match_keyword(wptr, wlen,
								  "ram16k")) {
					pvr2_hdw_cpufw_set_enabled(hdw, 1, !0);
				} else {
					return -EINVAL;
				}
			}
			pvr2_hdw_cpufw_set_enabled(hdw,0,!0);
			return 0;
		} else if (debugifc_match_keyword(wptr,wlen,"done")) {
			pvr2_hdw_cpufw_set_enabled(hdw,0,0);
			return 0;
		} else {
			return -EINVAL;
		}
	} else if (debugifc_match_keyword(wptr,wlen,"gpio")) {
		int dir_fl = 0;
		int ret;
		u32 msk,val;
		scnt = debugifc_isolate_word(buf,count,&wptr,&wlen);
		if (!scnt) return -EINVAL;
		count -= scnt; buf += scnt;
		if (!wptr) return -EINVAL;
		if (debugifc_match_keyword(wptr,wlen,"dir")) {
			dir_fl = !0;
		} else if (!debugifc_match_keyword(wptr,wlen,"out")) {
			return -EINVAL;
		}
		scnt = debugifc_isolate_word(buf,count,&wptr,&wlen);
		if (!scnt) return -EINVAL;
		count -= scnt; buf += scnt;
		if (!wptr) return -EINVAL;
		ret = debugifc_parse_unsigned_number(wptr,wlen,&msk);
		if (ret) return ret;
		scnt = debugifc_isolate_word(buf,count,&wptr,&wlen);
		if (wptr) {
			ret = debugifc_parse_unsigned_number(wptr,wlen,&val);
			if (ret) return ret;
		} else {
			val = msk;
			msk = 0xffffffff;
		}
		if (dir_fl) {
			ret = pvr2_hdw_gpio_chg_dir(hdw,msk,val);
		} else {
			ret = pvr2_hdw_gpio_chg_out(hdw,msk,val);
		}
		return ret;
	}
	pvr2_trace(PVR2_TRACE_DEBUGIFC,
		   "debugifc failed to recognize cmd: \"%.*s\"",wlen,wptr);
	return -EINVAL;
}


int pvr2_debugifc_docmd(struct pvr2_hdw *hdw,const char *buf,
			unsigned int count)
{
	unsigned int bcnt = 0;
	int ret;

	while (count) {
		for (bcnt = 0; bcnt < count; bcnt++) {
			if (buf[bcnt] == '\n') break;
		}

		ret = pvr2_debugifc_do1cmd(hdw,buf,bcnt);
		if (ret < 0) return ret;
		if (bcnt < count) bcnt++;
		buf += bcnt;
		count -= bcnt;
	}

	return 0;
}
