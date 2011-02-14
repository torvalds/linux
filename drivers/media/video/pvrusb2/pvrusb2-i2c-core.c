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
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/i2c.h>
#include <media/ir-kbd-i2c.h>
#include "pvrusb2-i2c-core.h"
#include "pvrusb2-hdw-internal.h"
#include "pvrusb2-debug.h"
#include "pvrusb2-fx2-cmd.h"
#include "pvrusb2.h"

#define trace_i2c(...) pvr2_trace(PVR2_TRACE_I2C,__VA_ARGS__)

/*

  This module attempts to implement a compliant I2C adapter for the pvrusb2
  device.

*/

static unsigned int i2c_scan;
module_param(i2c_scan, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(i2c_scan,"scan i2c bus at insmod time");

static int ir_mode[PVR_NUM] = { [0 ... PVR_NUM-1] = 1 };
module_param_array(ir_mode, int, NULL, 0444);
MODULE_PARM_DESC(ir_mode,"specify: 0=disable IR reception, 1=normal IR");

static int pvr2_disable_ir_video;
module_param_named(disable_autoload_ir_video, pvr2_disable_ir_video,
		   int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(disable_autoload_ir_video,
		 "1=do not try to autoload ir_video IR receiver");

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

static struct i2c_algorithm pvr2_i2c_algo_template = {
	.master_xfer   = pvr2_i2c_xfer,
	.functionality = pvr2_i2c_functionality,
};

static struct i2c_adapter pvr2_i2c_adap_template = {
	.owner         = THIS_MODULE,
	.class	       = 0,
};


/* Return true if device exists at given address */
static int do_i2c_probe(struct pvr2_hdw *hdw, int addr)
{
	struct i2c_msg msg[1];
	int rc;
	msg[0].addr = 0;
	msg[0].flags = I2C_M_RD;
	msg[0].len = 0;
	msg[0].buf = NULL;
	msg[0].addr = addr;
	rc = i2c_transfer(&hdw->i2c_adap, msg, ARRAY_SIZE(msg));
	return rc == 1;
}

static void do_i2c_scan(struct pvr2_hdw *hdw)
{
	int i;
	printk(KERN_INFO "%s: i2c scan beginning\n", hdw->name);
	for (i = 0; i < 128; i++) {
		if (do_i2c_probe(hdw, i)) {
			printk(KERN_INFO "%s: i2c scan: found device @ 0x%x\n",
			       hdw->name, i);
		}
	}
	printk(KERN_INFO "%s: i2c scan done.\n", hdw->name);
}

static void pvr2_i2c_register_ir(struct pvr2_hdw *hdw)
{
	struct i2c_board_info info;
	struct IR_i2c_init_data *init_data = &hdw->ir_init_data;
	if (pvr2_disable_ir_video) {
		pvr2_trace(PVR2_TRACE_INFO,
			   "Automatic binding of ir_video has been disabled.");
		return;
	}
	memset(&info, 0, sizeof(struct i2c_board_info));
	switch (hdw->ir_scheme_active) {
	case PVR2_IR_SCHEME_24XXX: /* FX2-controlled IR */
	case PVR2_IR_SCHEME_29XXX: /* Original 29xxx device */
		init_data->ir_codes              = RC_MAP_HAUPPAUGE_NEW;
		init_data->internal_get_key_func = IR_KBD_GET_KEY_HAUP;
		init_data->type                  = RC_TYPE_RC5;
		init_data->name                  = hdw->hdw_desc->description;
		init_data->polling_interval      = 100; /* ms From ir-kbd-i2c */
		/* IR Receiver */
		info.addr          = 0x18;
		info.platform_data = init_data;
		strlcpy(info.type, "ir_video", I2C_NAME_SIZE);
		pvr2_trace(PVR2_TRACE_INFO, "Binding %s to i2c address 0x%02x.",
			   info.type, info.addr);
		i2c_new_device(&hdw->i2c_adap, &info);
		break;
	case PVR2_IR_SCHEME_ZILOG:     /* HVR-1950 style */
	case PVR2_IR_SCHEME_24XXX_MCE: /* 24xxx MCE device */
		init_data->ir_codes              = RC_MAP_HAUPPAUGE_NEW;
		init_data->internal_get_key_func = IR_KBD_GET_KEY_HAUP_XVR;
		init_data->type                  = RC_TYPE_RC5;
		init_data->name                  = hdw->hdw_desc->description;
		/* IR Receiver */
		info.addr          = 0x71;
		info.platform_data = init_data;
		strlcpy(info.type, "ir_rx_z8f0811_haup", I2C_NAME_SIZE);
		pvr2_trace(PVR2_TRACE_INFO, "Binding %s to i2c address 0x%02x.",
			   info.type, info.addr);
		i2c_new_device(&hdw->i2c_adap, &info);
		/* IR Trasmitter */
		info.addr          = 0x70;
		info.platform_data = init_data;
		strlcpy(info.type, "ir_tx_z8f0811_haup", I2C_NAME_SIZE);
		pvr2_trace(PVR2_TRACE_INFO, "Binding %s to i2c address 0x%02x.",
			   info.type, info.addr);
		i2c_new_device(&hdw->i2c_adap, &info);
		break;
	default:
		/* The device either doesn't support I2C-based IR or we
		   don't know (yet) how to operate IR on the device. */
		break;
	}
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
		if (hdw->ir_scheme_active == PVR2_IR_SCHEME_24XXX) {
			/* Set up translation so that our IR looks like a
			   29xxx device */
			hdw->i2c_func[0x18] = i2c_24xxx_ir;
		}
	}
	if (hdw->hdw_desc->flag_has_cx25840) {
		hdw->i2c_func[0x44] = i2c_hack_cx25840;
	}
	if (hdw->hdw_desc->flag_has_wm8775) {
		hdw->i2c_func[0x1b] = i2c_hack_wm8775;
	}

	// Configure the adapter and set up everything else related to it.
	memcpy(&hdw->i2c_adap,&pvr2_i2c_adap_template,sizeof(hdw->i2c_adap));
	memcpy(&hdw->i2c_algo,&pvr2_i2c_algo_template,sizeof(hdw->i2c_algo));
	strlcpy(hdw->i2c_adap.name,hdw->name,sizeof(hdw->i2c_adap.name));
	hdw->i2c_adap.dev.parent = &hdw->usb_dev->dev;
	hdw->i2c_adap.algo = &hdw->i2c_algo;
	hdw->i2c_adap.algo_data = hdw;
	hdw->i2c_linked = !0;
	i2c_set_adapdata(&hdw->i2c_adap, &hdw->v4l2_dev);
	i2c_add_adapter(&hdw->i2c_adap);
	if (hdw->i2c_func[0x18] == i2c_24xxx_ir) {
		/* Probe for a different type of IR receiver on this
		   device.  This is really the only way to differentiate
		   older 24xxx devices from 24xxx variants that include an
		   IR blaster.  If the IR blaster is present, the IR
		   receiver is part of that chip and thus we must disable
		   the emulated IR receiver. */
		if (do_i2c_probe(hdw, 0x71)) {
			pvr2_trace(PVR2_TRACE_INFO,
				   "Device has newer IR hardware;"
				   " disabling unneeded virtual IR device");
			hdw->i2c_func[0x18] = NULL;
			/* Remember that this is a different device... */
			hdw->ir_scheme_active = PVR2_IR_SCHEME_24XXX_MCE;
		}
	}
	if (i2c_scan) do_i2c_scan(hdw);

	pvr2_i2c_register_ir(hdw);
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
