/*
 *
 *  $Id$
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
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/string.h>
#include <linux/slab.h>
#include "pvrusb2-debugifc.h"
#include "pvrusb2-hdw.h"
#include "pvrusb2-debug.h"
#include "pvrusb2-i2c-core.h"

struct debugifc_mask_item {
	const char *name;
	unsigned long msk;
};

static struct debugifc_mask_item mask_items[] = {
	{"ENC_FIRMWARE",(1<<PVR2_SUBSYS_B_ENC_FIRMWARE)},
	{"ENC_CFG",(1<<PVR2_SUBSYS_B_ENC_CFG)},
	{"DIG_RUN",(1<<PVR2_SUBSYS_B_DIGITIZER_RUN)},
	{"USB_RUN",(1<<PVR2_SUBSYS_B_USBSTREAM_RUN)},
	{"ENC_RUN",(1<<PVR2_SUBSYS_B_ENC_RUN)},
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
	u32 val;
	int ch;
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
		ch = *buf++;
		if ((ch >= '0') && (ch <= '9')) {
			val = ch - '0';
		} else if ((ch >= 'a') && (ch <= 'f')) {
			val = ch - 'a' + 10;
		} else if ((ch >= 'A') && (ch <= 'F')) {
			val = ch - 'A' + 10;
		} else {
			return -EINVAL;
		}
		if (val >= radix) return -EINVAL;
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


static unsigned long debugifc_find_mask(const char *buf,unsigned int count)
{
	struct debugifc_mask_item *mip;
	unsigned int idx;
	for (idx = 0; idx < ARRAY_SIZE(mask_items); idx++) {
		mip = mask_items + idx;
		if (debugifc_match_keyword(buf,count,mip->name)) {
			return mip->msk;
		}
	}
	return 0;
}


static int debugifc_print_mask(char *buf,unsigned int sz,
			       unsigned long msk,unsigned long val)
{
	struct debugifc_mask_item *mip;
	unsigned int idx;
	int bcnt = 0;
	int ccnt;
	for (idx = 0; idx < ARRAY_SIZE(mask_items); idx++) {
		mip = mask_items + idx;
		if (!(mip->msk & msk)) continue;
		ccnt = scnprintf(buf,sz,"%s%c%s",
				 (bcnt ? " " : ""),
				 ((mip->msk & val) ? '+' : '-'),
				 mip->name);
		sz -= ccnt;
		buf += ccnt;
		bcnt += ccnt;
	}
	return bcnt;
}

static unsigned int debugifc_parse_subsys_mask(const char *buf,
					       unsigned int count,
					       unsigned long *mskPtr,
					       unsigned long *valPtr)
{
	const char *wptr;
	unsigned int consume_cnt = 0;
	unsigned int scnt;
	unsigned int wlen;
	int mode;
	unsigned long m1,msk,val;

	msk = 0;
	val = 0;

	while (count) {
		scnt = debugifc_isolate_word(buf,count,&wptr,&wlen);
		if (!scnt) break;
		consume_cnt += scnt; count -= scnt; buf += scnt;
		if (!wptr) break;

		mode = 0;
		if (wlen) switch (wptr[0]) {
		case '+':
			wptr++;
			wlen--;
			break;
		case '-':
			mode = 1;
			wptr++;
			wlen--;
			break;
		}
		if (!wlen) continue;
		m1 = debugifc_find_mask(wptr,wlen);
		if (!m1) break;
		msk |= m1;
		if (!mode) val |= m1;
	}
	*mskPtr = msk;
	*valPtr = val;
	return consume_cnt;
}


int pvr2_debugifc_print_info(struct pvr2_hdw *hdw,char *buf,unsigned int acnt)
{
	int bcnt = 0;
	int ccnt;
	struct pvr2_hdw_debug_info dbg;

	pvr2_hdw_get_debug_info(hdw,&dbg);

	ccnt = scnprintf(buf,acnt,"big lock %s; ctl lock %s",
			 (dbg.big_lock_held ? "held" : "free"),
			 (dbg.ctl_lock_held ? "held" : "free"));
	bcnt += ccnt; acnt -= ccnt; buf += ccnt;
	if (dbg.ctl_lock_held) {
		ccnt = scnprintf(buf,acnt,"; cmd_state=%d cmd_code=%d"
				 " cmd_wlen=%d cmd_rlen=%d"
				 " wpend=%d rpend=%d tmout=%d rstatus=%d"
				 " wstatus=%d",
				 dbg.cmd_debug_state,dbg.cmd_code,
				 dbg.cmd_debug_write_len,
				 dbg.cmd_debug_read_len,
				 dbg.cmd_debug_write_pend,
				 dbg.cmd_debug_read_pend,
				 dbg.cmd_debug_timeout,
				 dbg.cmd_debug_rstatus,
				 dbg.cmd_debug_wstatus);
		bcnt += ccnt; acnt -= ccnt; buf += ccnt;
	}
	ccnt = scnprintf(buf,acnt,"\n");
	bcnt += ccnt; acnt -= ccnt; buf += ccnt;
	ccnt = scnprintf(
		buf,acnt,"driver flags: %s %s %s\n",
		(dbg.flag_init_ok ? "initialized" : "uninitialized"),
		(dbg.flag_ok ? "ok" : "fail"),
		(dbg.flag_disconnected ? "disconnected" : "connected"));
	bcnt += ccnt; acnt -= ccnt; buf += ccnt;
	ccnt = scnprintf(buf,acnt,"Subsystems enabled / configured: ");
	bcnt += ccnt; acnt -= ccnt; buf += ccnt;
	ccnt = debugifc_print_mask(buf,acnt,dbg.subsys_flags,dbg.subsys_flags);
	bcnt += ccnt; acnt -= ccnt; buf += ccnt;
	ccnt = scnprintf(buf,acnt,"\n");
	bcnt += ccnt; acnt -= ccnt; buf += ccnt;
	ccnt = scnprintf(buf,acnt,"Subsystems disabled / unconfigured: ");
	bcnt += ccnt; acnt -= ccnt; buf += ccnt;
	ccnt = debugifc_print_mask(buf,acnt,~dbg.subsys_flags,dbg.subsys_flags);
	bcnt += ccnt; acnt -= ccnt; buf += ccnt;
	ccnt = scnprintf(buf,acnt,"\n");
	bcnt += ccnt; acnt -= ccnt; buf += ccnt;

	ccnt = scnprintf(buf,acnt,"Attached I2C modules:\n");
	bcnt += ccnt; acnt -= ccnt; buf += ccnt;
	ccnt = pvr2_i2c_report(hdw,buf,acnt);
	bcnt += ccnt; acnt -= ccnt; buf += ccnt;

	return bcnt;
}


int pvr2_debugifc_print_status(struct pvr2_hdw *hdw,
			       char *buf,unsigned int acnt)
{
	int bcnt = 0;
	int ccnt;
	unsigned long msk;
	int ret;
	u32 gpio_dir,gpio_in,gpio_out;

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

	msk = pvr2_hdw_subsys_get(hdw);
	ccnt = scnprintf(buf,acnt,"Subsystems enabled / configured: ");
	bcnt += ccnt; acnt -= ccnt; buf += ccnt;
	ccnt = debugifc_print_mask(buf,acnt,msk,msk);
	bcnt += ccnt; acnt -= ccnt; buf += ccnt;
	ccnt = scnprintf(buf,acnt,"\n");
	bcnt += ccnt; acnt -= ccnt; buf += ccnt;
	ccnt = scnprintf(buf,acnt,"Subsystems disabled / unconfigured: ");
	bcnt += ccnt; acnt -= ccnt; buf += ccnt;
	ccnt = debugifc_print_mask(buf,acnt,~msk,msk);
	bcnt += ccnt; acnt -= ccnt; buf += ccnt;
	ccnt = scnprintf(buf,acnt,"\n");
	bcnt += ccnt; acnt -= ccnt; buf += ccnt;

	msk = pvr2_hdw_subsys_stream_get(hdw);
	ccnt = scnprintf(buf,acnt,"Subsystems stopped on stream shutdown: ");
	bcnt += ccnt; acnt -= ccnt; buf += ccnt;
	ccnt = debugifc_print_mask(buf,acnt,msk,msk);
	bcnt += ccnt; acnt -= ccnt; buf += ccnt;
	ccnt = scnprintf(buf,acnt,"\n");
	bcnt += ccnt; acnt -= ccnt; buf += ccnt;

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
		}
		return -EINVAL;
	} else if (debugifc_match_keyword(wptr,wlen,"subsys_flags")) {
		unsigned long msk = 0;
		unsigned long val = 0;
		if (debugifc_parse_subsys_mask(buf,count,&msk,&val) != count) {
			pvr2_trace(PVR2_TRACE_DEBUGIFC,
				   "debugifc parse error on subsys mask");
			return -EINVAL;
		}
		pvr2_hdw_subsys_bit_chg(hdw,msk,val);
		return 0;
	} else if (debugifc_match_keyword(wptr,wlen,"stream_flags")) {
		unsigned long msk = 0;
		unsigned long val = 0;
		if (debugifc_parse_subsys_mask(buf,count,&msk,&val) != count) {
			pvr2_trace(PVR2_TRACE_DEBUGIFC,
				   "debugifc parse error on stream mask");
			return -EINVAL;
		}
		pvr2_hdw_subsys_stream_bit_chg(hdw,msk,val);
		return 0;
	} else if (debugifc_match_keyword(wptr,wlen,"cpufw")) {
		scnt = debugifc_isolate_word(buf,count,&wptr,&wlen);
		if (!scnt) return -EINVAL;
		count -= scnt; buf += scnt;
		if (!wptr) return -EINVAL;
		if (debugifc_match_keyword(wptr,wlen,"fetch")) {
			pvr2_hdw_cpufw_set_enabled(hdw,!0);
			return 0;
		} else if (debugifc_match_keyword(wptr,wlen,"done")) {
			pvr2_hdw_cpufw_set_enabled(hdw,0);
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


/*
  Stuff for Emacs to see, in order to encourage consistent editing style:
  *** Local Variables: ***
  *** mode: c ***
  *** fill-column: 75 ***
  *** tab-width: 8 ***
  *** c-basic-offset: 8 ***
  *** End: ***
  */
