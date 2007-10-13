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

#include "pvrusb2-i2c-core.h"
#include "pvrusb2-hdw-internal.h"
#include "pvrusb2-debug.h"
#include "pvrusb2-fx2-cmd.h"
#include "pvrusb2.h"

#define trace_i2c(...) pvr2_trace(PVR2_TRACE_I2C,__VA_ARGS__)

/*

  This module attempts to implement a compliant I2C adapter for the pvrusb2
  device.  By doing this we can then make use of existing functionality in
  V4L (e.g. tuner.c) rather than rolling our own.

*/

static unsigned int i2c_scan = 0;
module_param(i2c_scan, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(i2c_scan,"scan i2c bus at insmod time");

static int ir_mode[PVR_NUM] = { [0 ... PVR_NUM-1] = 1 };
module_param_array(ir_mode, int, NULL, 0444);
MODULE_PARM_DESC(ir_mode,"specify: 0=disable IR reception, 1=normal IR");

static unsigned int pvr2_i2c_client_describe(struct pvr2_i2c_client *cp,
					     unsigned int detail,
					     char *buf,unsigned int maxlen);

static int pvr2_i2c_write(struct pvr2_hdw *hdw, /* Context */
			  u8 i2c_addr,      /* I2C address we're talking to */
			  u8 *data,         /* Data to write */
			  u16 length)       /* Size of data to write */
{
	/* Return value - default 0 means success */
	int ret;


	if (!data) length = 0;
	if (length > (sizeof(hdw->cmd_buffer) - 3)) {
		pvr2_trace(PVR2_TRACE_ERROR_LEGS,
			   "Killing an I2C write to %u that is too large"
			   " (desired=%u limit=%u)",
			   i2c_addr,
			   length,(unsigned int)(sizeof(hdw->cmd_buffer) - 3));
		return -ENOTSUPP;
	}

	LOCK_TAKE(hdw->ctl_lock);

	/* Clear the command buffer (likely to be paranoia) */
	memset(hdw->cmd_buffer, 0, sizeof(hdw->cmd_buffer));

	/* Set up command buffer for an I2C write */
	hdw->cmd_buffer[0] = FX2CMD_I2C_WRITE;      /* write prefix */
	hdw->cmd_buffer[1] = i2c_addr;  /* i2c addr of chip */
	hdw->cmd_buffer[2] = length;    /* length of what follows */
	if (length) memcpy(hdw->cmd_buffer + 3, data, length);

	/* Do the operation */
	ret = pvr2_send_request(hdw,
				hdw->cmd_buffer,
				length + 3,
				hdw->cmd_buffer,
				1);
	if (!ret) {
		if (hdw->cmd_buffer[0] != 8) {
			ret = -EIO;
			if (hdw->cmd_buffer[0] != 7) {
				trace_i2c("unexpected status"
					  " from i2_write[%d]: %d",
					  i2c_addr,hdw->cmd_buffer[0]);
			}
		}
	}

	LOCK_GIVE(hdw->ctl_lock);

	return ret;
}

static int pvr2_i2c_read(struct pvr2_hdw *hdw, /* Context */
			 u8 i2c_addr,       /* I2C address we're talking to */
			 u8 *data,          /* Data to write */
			 u16 dlen,          /* Size of data to write */
			 u8 *res,           /* Where to put data we read */
			 u16 rlen)          /* Amount of data to read */
{
	/* Return value - default 0 means success */
	int ret;


	if (!data) dlen = 0;
	if (dlen > (sizeof(hdw->cmd_buffer) - 4)) {
		pvr2_trace(PVR2_TRACE_ERROR_LEGS,
			   "Killing an I2C read to %u that has wlen too large"
			   " (desired=%u limit=%u)",
			   i2c_addr,
			   dlen,(unsigned int)(sizeof(hdw->cmd_buffer) - 4));
		return -ENOTSUPP;
	}
	if (res && (rlen > (sizeof(hdw->cmd_buffer) - 1))) {
		pvr2_trace(PVR2_TRACE_ERROR_LEGS,
			   "Killing an I2C read to %u that has rlen too large"
			   " (desired=%u limit=%u)",
			   i2c_addr,
			   rlen,(unsigned int)(sizeof(hdw->cmd_buffer) - 1));
		return -ENOTSUPP;
	}

	LOCK_TAKE(hdw->ctl_lock);

	/* Clear the command buffer (likely to be paranoia) */
	memset(hdw->cmd_buffer, 0, sizeof(hdw->cmd_buffer));

	/* Set up command buffer for an I2C write followed by a read */
	hdw->cmd_buffer[0] = FX2CMD_I2C_READ;  /* read prefix */
	hdw->cmd_buffer[1] = dlen;  /* arg length */
	hdw->cmd_buffer[2] = rlen;  /* answer length. Device will send one
				       more byte (status). */
	hdw->cmd_buffer[3] = i2c_addr;  /* i2c addr of chip */
	if (dlen) memcpy(hdw->cmd_buffer + 4, data, dlen);

	/* Do the operation */
	ret = pvr2_send_request(hdw,
				hdw->cmd_buffer,
				4 + dlen,
				hdw->cmd_buffer,
				rlen + 1);
	if (!ret) {
		if (hdw->cmd_buffer[0] != 8) {
			ret = -EIO;
			if (hdw->cmd_buffer[0] != 7) {
				trace_i2c("unexpected status"
					  " from i2_read[%d]: %d",
					  i2c_addr,hdw->cmd_buffer[0]);
			}
		}
	}

	/* Copy back the result */
	if (res && rlen) {
		if (ret) {
			/* Error, just blank out the return buffer */
			memset(res, 0, rlen);
		} else {
			memcpy(res, hdw->cmd_buffer + 1, rlen);
		}
	}

	LOCK_GIVE(hdw->ctl_lock);

	return ret;
}

/* This is the common low level entry point for doing I2C operations to the
   hardware. */
static int pvr2_i2c_basic_op(struct pvr2_hdw *hdw,
			     u8 i2c_addr,
			     u8 *wdata,
			     u16 wlen,
			     u8 *rdata,
			     u16 rlen)
{
	if (!rdata) rlen = 0;
	if (!wdata) wlen = 0;
	if (rlen || !wlen) {
		return pvr2_i2c_read(hdw,i2c_addr,wdata,wlen,rdata,rlen);
	} else {
		return pvr2_i2c_write(hdw,i2c_addr,wdata,wlen);
	}
}


/* This is a special entry point for cases of I2C transaction attempts to
   the IR receiver.  The implementation here simulates the IR receiver by
   issuing a command to the FX2 firmware and using that response to return
   what the real I2C receiver would have returned.  We use this for 24xxx
   devices, where the IR receiver chip has been removed and replaced with
   FX2 related logic. */
static int i2c_24xxx_ir(struct pvr2_hdw *hdw,
			u8 i2c_addr,u8 *wdata,u16 wlen,u8 *rdata,u16 rlen)
{
	u8 dat[4];
	unsigned int stat;

	if (!(rlen || wlen)) {
		/* This is a probe attempt.  Just let it succeed. */
		return 0;
	}

	/* We don't understand this kind of transaction */
	if ((wlen != 0) || (rlen == 0)) return -EIO;

	if (rlen < 3) {
		/* Mike Isely <isely@pobox.com> Appears to be a probe
		   attempt from lirc.  Just fill in zeroes and return.  If
		   we try instead to do the full transaction here, then bad
		   things seem to happen within the lirc driver module
		   (version 0.8.0-7 sources from Debian, when run under
		   vanilla 2.6.17.6 kernel) - and I don't have the patience
		   to chase it down. */
		if (rlen > 0) rdata[0] = 0;
		if (rlen > 1) rdata[1] = 0;
		return 0;
	}

	/* Issue a command to the FX2 to read the IR receiver. */
	LOCK_TAKE(hdw->ctl_lock); do {
		hdw->cmd_buffer[0] = FX2CMD_GET_IR_CODE;
		stat = pvr2_send_request(hdw,
					 hdw->cmd_buffer,1,
					 hdw->cmd_buffer,4);
		dat[0] = hdw->cmd_buffer[0];
		dat[1] = hdw->cmd_buffer[1];
		dat[2] = hdw->cmd_buffer[2];
		dat[3] = hdw->cmd_buffer[3];
	} while (0); LOCK_GIVE(hdw->ctl_lock);

	/* Give up if that operation failed. */
	if (stat != 0) return stat;

	/* Mangle the results into something that looks like the real IR
	   receiver. */
	rdata[2] = 0xc1;
	if (dat[0] != 1) {
		/* No code received. */
		rdata[0] = 0;
		rdata[1] = 0;
	} else {
		u16 val;
		/* Mash the FX2 firmware-provided IR code into something
		   that the normal i2c chip-level driver expects. */
		val = dat[1];
		val <<= 8;
		val |= dat[2];
		val >>= 1;
		val &= ~0x0003;
		val |= 0x8000;
		rdata[0] = (val >> 8) & 0xffu;
		rdata[1] = val & 0xffu;
	}

	return 0;
}

/* This is a special entry point that is entered if an I2C operation is
   attempted to a wm8775 chip on model 24xxx hardware.  Autodetect of this
   part doesn't work, but we know it is really there.  So let's look for
   the autodetect attempt and just return success if we see that. */
static int i2c_hack_wm8775(struct pvr2_hdw *hdw,
			   u8 i2c_addr,u8 *wdata,u16 wlen,u8 *rdata,u16 rlen)
{
	if (!(rlen || wlen)) {
		// This is a probe attempt.  Just let it succeed.
		return 0;
	}
	return pvr2_i2c_basic_op(hdw,i2c_addr,wdata,wlen,rdata,rlen);
}

/* This is an entry point designed to always fail any attempt to perform a
   transfer.  We use this to cause certain I2C addresses to not be
   probed. */
static int i2c_black_hole(struct pvr2_hdw *hdw,
			   u8 i2c_addr,u8 *wdata,u16 wlen,u8 *rdata,u16 rlen)
{
	return -EIO;
}

/* This is a special entry point that is entered if an I2C operation is
   attempted to a cx25840 chip on model 24xxx hardware.  This chip can
   sometimes wedge itself.  Worse still, when this happens msp3400 can
   falsely detect this part and then the system gets hosed up after msp3400
   gets confused and dies.  What we want to do here is try to keep msp3400
   away and also try to notice if the chip is wedged and send a warning to
   the system log. */
static int i2c_hack_cx25840(struct pvr2_hdw *hdw,
			    u8 i2c_addr,u8 *wdata,u16 wlen,u8 *rdata,u16 rlen)
{
	int ret;
	unsigned int subaddr;
	u8 wbuf[2];
	int state = hdw->i2c_cx25840_hack_state;

	if (!(rlen || wlen)) {
		// Probe attempt - always just succeed and don't bother the
		// hardware (this helps to make the state machine further
		// down somewhat easier).
		return 0;
	}

	if (state == 3) {
		return pvr2_i2c_basic_op(hdw,i2c_addr,wdata,wlen,rdata,rlen);
	}

	/* We're looking for the exact pattern where the revision register
	   is being read.  The cx25840 module will always look at the
	   revision register first.  Any other pattern of access therefore
	   has to be a probe attempt from somebody else so we'll reject it.
	   Normally we could just let each client just probe the part
	   anyway, but when the cx25840 is wedged, msp3400 will get a false
	   positive and that just screws things up... */

	if (wlen == 0) {
		switch (state) {
		case 1: subaddr = 0x0100; break;
		case 2: subaddr = 0x0101; break;
		default: goto fail;
		}
	} else if (wlen == 2) {
		subaddr = (wdata[0] << 8) | wdata[1];
		switch (subaddr) {
		case 0x0100: state = 1; break;
		case 0x0101: state = 2; break;
		default: goto fail;
		}
	} else {
		goto fail;
	}
	if (!rlen) goto success;
	state = 0;
	if (rlen != 1) goto fail;

	/* If we get to here then we have a legitimate read for one of the
	   two revision bytes, so pass it through. */
	wbuf[0] = subaddr >> 8;
	wbuf[1] = subaddr;
	ret = pvr2_i2c_basic_op(hdw,i2c_addr,wbuf,2,rdata,rlen);

	if ((ret != 0) || (*rdata == 0x04) || (*rdata == 0x0a)) {
		pvr2_trace(PVR2_TRACE_ERROR_LEGS,
			   "WARNING: Detected a wedged cx25840 chip;"
			   " the device will not work.");
		pvr2_trace(PVR2_TRACE_ERROR_LEGS,
			   "WARNING: Try power cycling the pvrusb2 device.");
		pvr2_trace(PVR2_TRACE_ERROR_LEGS,
			   "WARNING: Disabling further access to the device"
			   " to prevent other foul-ups.");
		// This blocks all further communication with the part.
		hdw->i2c_func[0x44] = NULL;
		pvr2_hdw_render_useless(hdw);
		goto fail;
	}

	/* Success! */
	pvr2_trace(PVR2_TRACE_CHIPS,"cx25840 appears to be OK.");
	state = 3;

 success:
	hdw->i2c_cx25840_hack_state = state;
	return 0;

 fail:
	hdw->i2c_cx25840_hack_state = state;
	return -EIO;
}

/* This is a very, very limited I2C adapter implementation.  We can only
   support what we actually know will work on the device... */
static int pvr2_i2c_xfer(struct i2c_adapter *i2c_adap,
			 struct i2c_msg msgs[],
			 int num)
{
	int ret = -ENOTSUPP;
	pvr2_i2c_func funcp = NULL;
	struct pvr2_hdw *hdw = (struct pvr2_hdw *)(i2c_adap->algo_data);

	if (!num) {
		ret = -EINVAL;
		goto done;
	}
	if (msgs[0].addr < PVR2_I2C_FUNC_CNT) {
		funcp = hdw->i2c_func[msgs[0].addr];
	}
	if (!funcp) {
		ret = -EIO;
		goto done;
	}

	if (num == 1) {
		if (msgs[0].flags & I2C_M_RD) {
			/* Simple read */
			u16 tcnt,bcnt,offs;
			if (!msgs[0].len) {
				/* Length == 0 read.  This is a probe. */
				if (funcp(hdw,msgs[0].addr,NULL,0,NULL,0)) {
					ret = -EIO;
					goto done;
				}
				ret = 1;
				goto done;
			}
			/* If the read is short enough we'll do the whole
			   thing atomically.  Otherwise we have no choice
			   but to break apart the reads. */
			tcnt = msgs[0].len;
			offs = 0;
			while (tcnt) {
				bcnt = tcnt;
				if (bcnt > sizeof(hdw->cmd_buffer)-1) {
					bcnt = sizeof(hdw->cmd_buffer)-1;
				}
				if (funcp(hdw,msgs[0].addr,NULL,0,
					  msgs[0].buf+offs,bcnt)) {
					ret = -EIO;
					goto done;
				}
				offs += bcnt;
				tcnt -= bcnt;
			}
			ret = 1;
			goto done;
		} else {
			/* Simple write */
			ret = 1;
			if (funcp(hdw,msgs[0].addr,
				  msgs[0].buf,msgs[0].len,NULL,0)) {
				ret = -EIO;
			}
			goto done;
		}
	} else if (num == 2) {
		if (msgs[0].addr != msgs[1].addr) {
			trace_i2c("i2c refusing 2 phase transfer with"
				  " conflicting target addresses");
			ret = -ENOTSUPP;
			goto done;
		}
		if ((!((msgs[0].flags & I2C_M_RD))) &&
		    (msgs[1].flags & I2C_M_RD)) {
			u16 tcnt,bcnt,wcnt,offs;
			/* Write followed by atomic read.  If the read
			   portion is short enough we'll do the whole thing
			   atomically.  Otherwise we have no choice but to
			   break apart the reads. */
			tcnt = msgs[1].len;
			wcnt = msgs[0].len;
			offs = 0;
			while (tcnt || wcnt) {
				bcnt = tcnt;
				if (bcnt > sizeof(hdw->cmd_buffer)-1) {
					bcnt = sizeof(hdw->cmd_buffer)-1;
				}
				if (funcp(hdw,msgs[0].addr,
					  msgs[0].buf,wcnt,
					  msgs[1].buf+offs,bcnt)) {
					ret = -EIO;
					goto done;
				}
				offs += bcnt;
				tcnt -= bcnt;
				wcnt = 0;
			}
			ret = 2;
			goto done;
		} else {
			trace_i2c("i2c refusing complex transfer"
				  " read0=%d read1=%d",
				  (msgs[0].flags & I2C_M_RD),
				  (msgs[1].flags & I2C_M_RD));
		}
	} else {
		trace_i2c("i2c refusing %d phase transfer",num);
	}

 done:
	if (pvrusb2_debug & PVR2_TRACE_I2C_TRAF) {
		unsigned int idx,offs,cnt;
		for (idx = 0; idx < num; idx++) {
			cnt = msgs[idx].len;
			printk(KERN_INFO
			       "pvrusb2 i2c xfer %u/%u:"
			       " addr=0x%x len=%d %s",
			       idx+1,num,
			       msgs[idx].addr,
			       cnt,
			       (msgs[idx].flags & I2C_M_RD ?
				"read" : "write"));
			if ((ret > 0) || !(msgs[idx].flags & I2C_M_RD)) {
				if (cnt > 8) cnt = 8;
				printk(" [");
				for (offs = 0; offs < (cnt>8?8:cnt); offs++) {
					if (offs) printk(" ");
					printk("%02x",msgs[idx].buf[offs]);
				}
				if (offs < cnt) printk(" ...");
				printk("]");
			}
			if (idx+1 == num) {
				printk(" result=%d",ret);
			}
			printk("\n");
		}
		if (!num) {
			printk(KERN_INFO
			       "pvrusb2 i2c xfer null transfer result=%d\n",
			       ret);
		}
	}
	return ret;
}

static u32 pvr2_i2c_functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_SMBUS_EMUL | I2C_FUNC_I2C;
}

static int pvr2_i2c_core_singleton(struct i2c_client *cp,
				   unsigned int cmd,void *arg)
{
	int stat;
	if (!cp) return -EINVAL;
	if (!(cp->driver)) return -EINVAL;
	if (!(cp->driver->command)) return -EINVAL;
	if (!try_module_get(cp->driver->driver.owner)) return -EAGAIN;
	stat = cp->driver->command(cp,cmd,arg);
	module_put(cp->driver->driver.owner);
	return stat;
}

int pvr2_i2c_client_cmd(struct pvr2_i2c_client *cp,unsigned int cmd,void *arg)
{
	int stat;
	if (pvrusb2_debug & PVR2_TRACE_I2C_CMD) {
		char buf[100];
		unsigned int cnt;
		cnt = pvr2_i2c_client_describe(cp,PVR2_I2C_DETAIL_DEBUG,
					       buf,sizeof(buf));
		pvr2_trace(PVR2_TRACE_I2C_CMD,
			   "i2c COMMAND (code=%u 0x%x) to %.*s",
			   cmd,cmd,cnt,buf);
	}
	stat = pvr2_i2c_core_singleton(cp->client,cmd,arg);
	if (pvrusb2_debug & PVR2_TRACE_I2C_CMD) {
		char buf[100];
		unsigned int cnt;
		cnt = pvr2_i2c_client_describe(cp,PVR2_I2C_DETAIL_DEBUG,
					       buf,sizeof(buf));
		pvr2_trace(PVR2_TRACE_I2C_CMD,
			   "i2c COMMAND to %.*s (ret=%d)",cnt,buf,stat);
	}
	return stat;
}

int pvr2_i2c_core_cmd(struct pvr2_hdw *hdw,unsigned int cmd,void *arg)
{
	struct pvr2_i2c_client *cp, *ncp;
	int stat = -EINVAL;

	if (!hdw) return stat;

	mutex_lock(&hdw->i2c_list_lock);
	list_for_each_entry_safe(cp, ncp, &hdw->i2c_clients, list) {
		if (!cp->recv_enable) continue;
		mutex_unlock(&hdw->i2c_list_lock);
		stat = pvr2_i2c_client_cmd(cp,cmd,arg);
		mutex_lock(&hdw->i2c_list_lock);
	}
	mutex_unlock(&hdw->i2c_list_lock);
	return stat;
}


static int handler_check(struct pvr2_i2c_client *cp)
{
	struct pvr2_i2c_handler *hp = cp->handler;
	if (!hp) return 0;
	if (!hp->func_table->check) return 0;
	return hp->func_table->check(hp->func_data) != 0;
}

#define BUFSIZE 500


void pvr2_i2c_core_status_poll(struct pvr2_hdw *hdw)
{
	struct pvr2_i2c_client *cp;
	mutex_lock(&hdw->i2c_list_lock); do {
		struct v4l2_tuner *vtp = &hdw->tuner_signal_info;
		memset(vtp,0,sizeof(*vtp));
		list_for_each_entry(cp, &hdw->i2c_clients, list) {
			if (!cp->detected_flag) continue;
			if (!cp->status_poll) continue;
			cp->status_poll(cp);
		}
		hdw->tuner_signal_stale = 0;
		pvr2_trace(PVR2_TRACE_CHIPS,"i2c status poll"
			   " type=%u strength=%u audio=0x%x cap=0x%x"
			   " low=%u hi=%u",
			   vtp->type,
			   vtp->signal,vtp->rxsubchans,vtp->capability,
			   vtp->rangelow,vtp->rangehigh);
	} while (0); mutex_unlock(&hdw->i2c_list_lock);
}


/* Issue various I2C operations to bring chip-level drivers into sync with
   state stored in this driver. */
void pvr2_i2c_core_sync(struct pvr2_hdw *hdw)
{
	unsigned long msk;
	unsigned int idx;
	struct pvr2_i2c_client *cp, *ncp;

	if (!hdw->i2c_linked) return;
	if (!(hdw->i2c_pend_types & PVR2_I2C_PEND_ALL)) {
		return;
	}
	mutex_lock(&hdw->i2c_list_lock); do {
		pvr2_trace(PVR2_TRACE_I2C_CORE,"i2c: core_sync BEGIN");
		if (hdw->i2c_pend_types & PVR2_I2C_PEND_DETECT) {
			/* One or more I2C clients have attached since we
			   last synced.  So scan the list and identify the
			   new clients. */
			char *buf;
			unsigned int cnt;
			unsigned long amask = 0;
			buf = kmalloc(BUFSIZE,GFP_KERNEL);
			pvr2_trace(PVR2_TRACE_I2C_CORE,"i2c: PEND_DETECT");
			hdw->i2c_pend_types &= ~PVR2_I2C_PEND_DETECT;
			list_for_each_entry(cp, &hdw->i2c_clients, list) {
				if (!cp->detected_flag) {
					cp->ctl_mask = 0;
					pvr2_i2c_probe(hdw,cp);
					cp->detected_flag = !0;
					msk = cp->ctl_mask;
					cnt = 0;
					if (buf) {
						cnt = pvr2_i2c_client_describe(
							cp,
							PVR2_I2C_DETAIL_ALL,
							buf,BUFSIZE);
					}
					trace_i2c("Probed: %.*s",cnt,buf);
					if (handler_check(cp)) {
						hdw->i2c_pend_types |=
							PVR2_I2C_PEND_CLIENT;
					}
					cp->pend_mask = msk;
					hdw->i2c_pend_mask |= msk;
					hdw->i2c_pend_types |=
						PVR2_I2C_PEND_REFRESH;
				}
				amask |= cp->ctl_mask;
			}
			hdw->i2c_active_mask = amask;
			if (buf) kfree(buf);
		}
		if (hdw->i2c_pend_types & PVR2_I2C_PEND_STALE) {
			/* Need to do one or more global updates.  Arrange
			   for this to happen. */
			unsigned long m2;
			pvr2_trace(PVR2_TRACE_I2C_CORE,
				   "i2c: PEND_STALE (0x%lx)",
				   hdw->i2c_stale_mask);
			hdw->i2c_pend_types &= ~PVR2_I2C_PEND_STALE;
			list_for_each_entry(cp, &hdw->i2c_clients, list) {
				m2 = hdw->i2c_stale_mask;
				m2 &= cp->ctl_mask;
				m2 &= ~cp->pend_mask;
				if (m2) {
					pvr2_trace(PVR2_TRACE_I2C_CORE,
						   "i2c: cp=%p setting 0x%lx",
						   cp,m2);
					cp->pend_mask |= m2;
				}
			}
			hdw->i2c_pend_mask |= hdw->i2c_stale_mask;
			hdw->i2c_stale_mask = 0;
			hdw->i2c_pend_types |= PVR2_I2C_PEND_REFRESH;
		}
		if (hdw->i2c_pend_types & PVR2_I2C_PEND_CLIENT) {
			/* One or more client handlers are asking for an
			   update.  Run through the list of known clients
			   and update each one. */
			pvr2_trace(PVR2_TRACE_I2C_CORE,"i2c: PEND_CLIENT");
			hdw->i2c_pend_types &= ~PVR2_I2C_PEND_CLIENT;
			list_for_each_entry_safe(cp, ncp, &hdw->i2c_clients,
						 list) {
				if (!cp->handler) continue;
				if (!cp->handler->func_table->update) continue;
				pvr2_trace(PVR2_TRACE_I2C_CORE,
					   "i2c: cp=%p update",cp);
				mutex_unlock(&hdw->i2c_list_lock);
				cp->handler->func_table->update(
					cp->handler->func_data);
				mutex_lock(&hdw->i2c_list_lock);
				/* If client's update function set some
				   additional pending bits, account for that
				   here. */
				if (cp->pend_mask & ~hdw->i2c_pend_mask) {
					hdw->i2c_pend_mask |= cp->pend_mask;
					hdw->i2c_pend_types |=
						PVR2_I2C_PEND_REFRESH;
				}
			}
		}
		if (hdw->i2c_pend_types & PVR2_I2C_PEND_REFRESH) {
			const struct pvr2_i2c_op *opf;
			unsigned long pm;
			/* Some actual updates are pending.  Walk through
			   each update type and perform it. */
			pvr2_trace(PVR2_TRACE_I2C_CORE,"i2c: PEND_REFRESH"
				   " (0x%lx)",hdw->i2c_pend_mask);
			hdw->i2c_pend_types &= ~PVR2_I2C_PEND_REFRESH;
			pm = hdw->i2c_pend_mask;
			hdw->i2c_pend_mask = 0;
			for (idx = 0, msk = 1; pm; idx++, msk <<= 1) {
				if (!(pm & msk)) continue;
				pm &= ~msk;
				list_for_each_entry(cp, &hdw->i2c_clients,
						    list) {
					if (cp->pend_mask & msk) {
						cp->pend_mask &= ~msk;
						cp->recv_enable = !0;
					} else {
						cp->recv_enable = 0;
					}
				}
				opf = pvr2_i2c_get_op(idx);
				if (!opf) continue;
				mutex_unlock(&hdw->i2c_list_lock);
				opf->update(hdw);
				mutex_lock(&hdw->i2c_list_lock);
			}
		}
		pvr2_trace(PVR2_TRACE_I2C_CORE,"i2c: core_sync END");
	} while (0); mutex_unlock(&hdw->i2c_list_lock);
}

int pvr2_i2c_core_check_stale(struct pvr2_hdw *hdw)
{
	unsigned long msk,sm,pm;
	unsigned int idx;
	const struct pvr2_i2c_op *opf;
	struct pvr2_i2c_client *cp;
	unsigned int pt = 0;

	pvr2_trace(PVR2_TRACE_I2C_CORE,"pvr2_i2c_core_check_stale BEGIN");

	pm = hdw->i2c_active_mask;
	sm = 0;
	for (idx = 0, msk = 1; pm; idx++, msk <<= 1) {
		if (!(msk & pm)) continue;
		pm &= ~msk;
		opf = pvr2_i2c_get_op(idx);
		if (!opf) continue;
		if (opf->check(hdw)) {
			sm |= msk;
		}
	}
	if (sm) pt |= PVR2_I2C_PEND_STALE;

	list_for_each_entry(cp, &hdw->i2c_clients, list)
		if (handler_check(cp))
			pt |= PVR2_I2C_PEND_CLIENT;

	if (pt) {
		mutex_lock(&hdw->i2c_list_lock); do {
			hdw->i2c_pend_types |= pt;
			hdw->i2c_stale_mask |= sm;
			hdw->i2c_pend_mask |= hdw->i2c_stale_mask;
		} while (0); mutex_unlock(&hdw->i2c_list_lock);
	}

	pvr2_trace(PVR2_TRACE_I2C_CORE,
		   "i2c: types=0x%x stale=0x%lx pend=0x%lx",
		   hdw->i2c_pend_types,
		   hdw->i2c_stale_mask,
		   hdw->i2c_pend_mask);
	pvr2_trace(PVR2_TRACE_I2C_CORE,"pvr2_i2c_core_check_stale END");

	return (hdw->i2c_pend_types & PVR2_I2C_PEND_ALL) != 0;
}

static unsigned int pvr2_i2c_client_describe(struct pvr2_i2c_client *cp,
					     unsigned int detail,
					     char *buf,unsigned int maxlen)
{
	unsigned int ccnt,bcnt;
	int spcfl = 0;
	const struct pvr2_i2c_op *opf;

	ccnt = 0;
	if (detail & PVR2_I2C_DETAIL_DEBUG) {
		bcnt = scnprintf(buf,maxlen,
				 "ctxt=%p ctl_mask=0x%lx",
				 cp,cp->ctl_mask);
		ccnt += bcnt; buf += bcnt; maxlen -= bcnt;
		spcfl = !0;
	}
	bcnt = scnprintf(buf,maxlen,
			 "%s%s @ 0x%x",
			 (spcfl ? " " : ""),
			 cp->client->name,
			 cp->client->addr);
	ccnt += bcnt; buf += bcnt; maxlen -= bcnt;
	if ((detail & PVR2_I2C_DETAIL_HANDLER) &&
	    cp->handler && cp->handler->func_table->describe) {
		bcnt = scnprintf(buf,maxlen," (");
		ccnt += bcnt; buf += bcnt; maxlen -= bcnt;
		bcnt = cp->handler->func_table->describe(
			cp->handler->func_data,buf,maxlen);
		ccnt += bcnt; buf += bcnt; maxlen -= bcnt;
		bcnt = scnprintf(buf,maxlen,")");
		ccnt += bcnt; buf += bcnt; maxlen -= bcnt;
	}
	if ((detail & PVR2_I2C_DETAIL_CTLMASK) && cp->ctl_mask) {
		unsigned int idx;
		unsigned long msk,sm;
		int spcfl;
		bcnt = scnprintf(buf,maxlen," [");
		ccnt += bcnt; buf += bcnt; maxlen -= bcnt;
		sm = 0;
		spcfl = 0;
		for (idx = 0, msk = 1; msk; idx++, msk <<= 1) {
			if (!(cp->ctl_mask & msk)) continue;
			opf = pvr2_i2c_get_op(idx);
			if (opf) {
				bcnt = scnprintf(buf,maxlen,"%s%s",
						 spcfl ? " " : "",
						 opf->name);
				ccnt += bcnt; buf += bcnt; maxlen -= bcnt;
				spcfl = !0;
			} else {
				sm |= msk;
			}
		}
		if (sm) {
			bcnt = scnprintf(buf,maxlen,"%s%lx",
					 idx != 0 ? " " : "",sm);
			ccnt += bcnt; buf += bcnt; maxlen -= bcnt;
		}
		bcnt = scnprintf(buf,maxlen,"]");
		ccnt += bcnt; buf += bcnt; maxlen -= bcnt;
	}
	return ccnt;
}

unsigned int pvr2_i2c_report(struct pvr2_hdw *hdw,
			     char *buf,unsigned int maxlen)
{
	unsigned int ccnt,bcnt;
	struct pvr2_i2c_client *cp;
	ccnt = 0;
	mutex_lock(&hdw->i2c_list_lock); do {
		list_for_each_entry(cp, &hdw->i2c_clients, list) {
			bcnt = pvr2_i2c_client_describe(
				cp,
				(PVR2_I2C_DETAIL_HANDLER|
				 PVR2_I2C_DETAIL_CTLMASK),
				buf,maxlen);
			ccnt += bcnt; buf += bcnt; maxlen -= bcnt;
			bcnt = scnprintf(buf,maxlen,"\n");
			ccnt += bcnt; buf += bcnt; maxlen -= bcnt;
		}
	} while (0); mutex_unlock(&hdw->i2c_list_lock);
	return ccnt;
}

static int pvr2_i2c_attach_inform(struct i2c_client *client)
{
	struct pvr2_hdw *hdw = (struct pvr2_hdw *)(client->adapter->algo_data);
	struct pvr2_i2c_client *cp;
	int fl = !(hdw->i2c_pend_types & PVR2_I2C_PEND_ALL);
	cp = kzalloc(sizeof(*cp),GFP_KERNEL);
	trace_i2c("i2c_attach [client=%s @ 0x%x ctxt=%p]",
		  client->name,
		  client->addr,cp);
	if (!cp) return -ENOMEM;
	cp->hdw = hdw;
	INIT_LIST_HEAD(&cp->list);
	cp->client = client;
	mutex_lock(&hdw->i2c_list_lock); do {
		list_add_tail(&cp->list,&hdw->i2c_clients);
		hdw->i2c_pend_types |= PVR2_I2C_PEND_DETECT;
	} while (0); mutex_unlock(&hdw->i2c_list_lock);
	if (fl) pvr2_hdw_poll_trigger_unlocked(hdw);
	return 0;
}

static int pvr2_i2c_detach_inform(struct i2c_client *client)
{
	struct pvr2_hdw *hdw = (struct pvr2_hdw *)(client->adapter->algo_data);
	struct pvr2_i2c_client *cp, *ncp;
	unsigned long amask = 0;
	int foundfl = 0;
	mutex_lock(&hdw->i2c_list_lock); do {
		list_for_each_entry_safe(cp, ncp, &hdw->i2c_clients, list) {
			if (cp->client == client) {
				trace_i2c("pvr2_i2c_detach"
					  " [client=%s @ 0x%x ctxt=%p]",
					  client->name,
					  client->addr,cp);
				if (cp->handler &&
				    cp->handler->func_table->detach) {
					cp->handler->func_table->detach(
						cp->handler->func_data);
				}
				list_del(&cp->list);
				kfree(cp);
				foundfl = !0;
				continue;
			}
			amask |= cp->ctl_mask;
		}
		hdw->i2c_active_mask = amask;
	} while (0); mutex_unlock(&hdw->i2c_list_lock);
	if (!foundfl) {
		trace_i2c("pvr2_i2c_detach [client=%s @ 0x%x ctxt=<unknown>]",
			  client->name,
			  client->addr);
	}
	return 0;
}

static struct i2c_algorithm pvr2_i2c_algo_template = {
	.master_xfer   = pvr2_i2c_xfer,
	.functionality = pvr2_i2c_functionality,
};

static struct i2c_adapter pvr2_i2c_adap_template = {
	.owner         = THIS_MODULE,
	.class	   = I2C_CLASS_TV_ANALOG,
	.id            = I2C_HW_B_BT848,
	.client_register = pvr2_i2c_attach_inform,
	.client_unregister = pvr2_i2c_detach_inform,
};

static void do_i2c_scan(struct pvr2_hdw *hdw)
{
	struct i2c_msg msg[1];
	int i,rc;
	msg[0].addr = 0;
	msg[0].flags = I2C_M_RD;
	msg[0].len = 0;
	msg[0].buf = NULL;
	printk("%s: i2c scan beginning\n",hdw->name);
	for (i = 0; i < 128; i++) {
		msg[0].addr = i;
		rc = i2c_transfer(&hdw->i2c_adap,msg, ARRAY_SIZE(msg));
		if (rc != 1) continue;
		printk("%s: i2c scan: found device @ 0x%x\n",hdw->name,i);
	}
	printk("%s: i2c scan done.\n",hdw->name);
}

void pvr2_i2c_core_init(struct pvr2_hdw *hdw)
{
	unsigned int idx;

	/* The default action for all possible I2C addresses is just to do
	   the transfer normally. */
	for (idx = 0; idx < PVR2_I2C_FUNC_CNT; idx++) {
		hdw->i2c_func[idx] = pvr2_i2c_basic_op;
	}

	/* However, deal with various special cases for 24xxx hardware. */
	if (ir_mode[hdw->unit_number] == 0) {
		printk(KERN_INFO "%s: IR disabled\n",hdw->name);
		hdw->i2c_func[0x18] = i2c_black_hole;
	} else if (ir_mode[hdw->unit_number] == 1) {
		if (hdw->hdw_type == PVR2_HDW_TYPE_24XXX) {
			hdw->i2c_func[0x18] = i2c_24xxx_ir;
		}
	}
	if (hdw->hdw_type == PVR2_HDW_TYPE_24XXX) {
		hdw->i2c_func[0x1b] = i2c_hack_wm8775;
		hdw->i2c_func[0x44] = i2c_hack_cx25840;
	}

	// Configure the adapter and set up everything else related to it.
	memcpy(&hdw->i2c_adap,&pvr2_i2c_adap_template,sizeof(hdw->i2c_adap));
	memcpy(&hdw->i2c_algo,&pvr2_i2c_algo_template,sizeof(hdw->i2c_algo));
	strlcpy(hdw->i2c_adap.name,hdw->name,sizeof(hdw->i2c_adap.name));
	hdw->i2c_adap.dev.parent = &hdw->usb_dev->dev;
	hdw->i2c_adap.algo = &hdw->i2c_algo;
	hdw->i2c_adap.algo_data = hdw;
	hdw->i2c_pend_mask = 0;
	hdw->i2c_stale_mask = 0;
	hdw->i2c_active_mask = 0;
	INIT_LIST_HEAD(&hdw->i2c_clients);
	mutex_init(&hdw->i2c_list_lock);
	hdw->i2c_linked = !0;
	i2c_add_adapter(&hdw->i2c_adap);
	if (i2c_scan) do_i2c_scan(hdw);
}

void pvr2_i2c_core_done(struct pvr2_hdw *hdw)
{
	if (hdw->i2c_linked) {
		i2c_del_adapter(&hdw->i2c_adap);
		hdw->i2c_linked = 0;
	}
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
