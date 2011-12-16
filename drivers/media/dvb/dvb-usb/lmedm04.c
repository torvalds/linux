/* DVB USB compliant linux driver for
 *
 * DM04/QQBOX DVB-S USB BOX	LME2510C + SHARP:BS2F7HZ7395
 *				LME2510C + LG TDQY-P001F
 *				LME2510C + BS2F7HZ0194
 *				LME2510 + LG TDQY-P001F
 *				LME2510 + BS2F7HZ0194
 *
 * MVB7395 (LME2510C+SHARP:BS2F7HZ7395)
 * SHARP:BS2F7HZ7395 = (STV0288+Sharp IX2505V)
 *
 * MV001F (LME2510+LGTDQY-P001F)
 * LG TDQY - P001F =(TDA8263 + TDA10086H)
 *
 * MVB0001F (LME2510C+LGTDQT-P001F)
 *
 * MV0194 (LME2510+SHARP:BS2F7HZ0194)
 * SHARP:BS2F7HZ0194 = (STV0299+IX2410)
 *
 * MVB0194 (LME2510C+SHARP0194)
 *
 * For firmware see Documentation/dvb/lmedm04.txt
 *
 * I2C addresses:
 * 0xd0 - STV0288	- Demodulator
 * 0xc0 - Sharp IX2505V	- Tuner
 * --
 * 0x1c - TDA10086   - Demodulator
 * 0xc0 - TDA8263    - Tuner
 * --
 * 0xd0 - STV0299	- Demodulator
 * 0xc0 - IX2410	- Tuner
 *
 *
 * VID = 3344  PID LME2510=1122 LME2510C=1120
 *
 * Copyright (C) 2010 Malcolm Priestley (tvboxspy@gmail.com)
 * LME2510(C)(C) Leaguerme (Shenzhen) MicroElectronics Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License Version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 * see Documentation/dvb/README.dvb-usb for more information
 *
 * Known Issues :
 *	LME2510: Non Intel USB chipsets fail to maintain High Speed on
 * Boot or Hot Plug.
 *
 * QQbox suffers from noise on LNB voltage.
 *
 *	LME2510: SHARP:BS2F7HZ0194(MV0194) cannot cold reset and share system
 * with other tuners. After a cold reset streaming will not start.
 *
 */
#define DVB_USB_LOG_PREFIX "LME2510(C)"
#include <linux/usb.h>
#include <linux/usb/input.h>
#include <media/rc-core.h>

#include "dvb-usb.h"
#include "lmedm04.h"
#include "tda826x.h"
#include "tda10086.h"
#include "stv0288.h"
#include "ix2505v.h"
#include "stv0299.h"
#include "dvb-pll.h"
#include "z0194a.h"



/* debug */
static int dvb_usb_lme2510_debug;
#define l_dprintk(var, level, args...) do { \
	if ((var >= level)) \
		printk(KERN_DEBUG DVB_USB_LOG_PREFIX ": " args); \
} while (0)

#define deb_info(level, args...) l_dprintk(dvb_usb_lme2510_debug, level, args)
#define debug_data_snipet(level, name, p) \
	 deb_info(level, name" (%02x%02x%02x%02x%02x%02x%02x%02x)", \
		*p, *(p+1), *(p+2), *(p+3), *(p+4), \
			*(p+5), *(p+6), *(p+7));


module_param_named(debug, dvb_usb_lme2510_debug, int, 0644);
MODULE_PARM_DESC(debug, "set debugging level (1=info (or-able))."
			DVB_USB_DEBUG_STATUS);

static int dvb_usb_lme2510_firmware;
module_param_named(firmware, dvb_usb_lme2510_firmware, int, 0644);
MODULE_PARM_DESC(firmware, "set default firmware 0=Sharp7395 1=LG");

static int pid_filter;
module_param_named(pid, pid_filter, int, 0644);
MODULE_PARM_DESC(pid, "set default 0=on 1=off");


DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

#define TUNER_DEFAULT	0x0
#define TUNER_LG	0x1
#define TUNER_S7395	0x2
#define TUNER_S0194	0x3

struct lme2510_state {
	u8 id;
	u8 tuner_config;
	u8 signal_lock;
	u8 signal_level;
	u8 signal_sn;
	u8 time_key;
	u8 i2c_talk_onoff;
	u8 i2c_gate;
	u8 i2c_tuner_gate_w;
	u8 i2c_tuner_gate_r;
	u8 i2c_tuner_addr;
	u8 stream_on;
	u8 pid_size;
	void *buffer;
	struct urb *lme_urb;
	void *usb_buffer;

};

static int lme2510_bulk_write(struct usb_device *dev,
				u8 *snd, int len, u8 pipe)
{
	int ret, actual_l;

	ret = usb_bulk_msg(dev, usb_sndbulkpipe(dev, pipe),
				snd, len , &actual_l, 100);
	return ret;
}

static int lme2510_bulk_read(struct usb_device *dev,
				u8 *rev, int len, u8 pipe)
{
	int ret, actual_l;

	ret = usb_bulk_msg(dev, usb_rcvbulkpipe(dev, pipe),
				 rev, len , &actual_l, 200);
	return ret;
}

static int lme2510_usb_talk(struct dvb_usb_device *d,
		u8 *wbuf, int wlen, u8 *rbuf, int rlen)
{
	struct lme2510_state *st = d->priv;
	u8 *buff;
	int ret = 0;

	if (st->usb_buffer == NULL) {
		st->usb_buffer = kmalloc(64, GFP_KERNEL);
		if (st->usb_buffer == NULL) {
			info("MEM Error no memory");
			return -ENOMEM;
		}
	}
	buff = st->usb_buffer;

	ret = mutex_lock_interruptible(&d->usb_mutex);

	if (ret < 0)
		return -EAGAIN;

	/* the read/write capped at 64 */
	memcpy(buff, wbuf, (wlen < 64) ? wlen : 64);

	ret |= usb_clear_halt(d->udev, usb_sndbulkpipe(d->udev, 0x01));

	ret |= lme2510_bulk_write(d->udev, buff, wlen , 0x01);

	msleep(10);

	ret |= usb_clear_halt(d->udev, usb_rcvbulkpipe(d->udev, 0x01));

	ret |= lme2510_bulk_read(d->udev, buff, (rlen < 64) ?
			rlen : 64 , 0x01);

	if (rlen > 0)
		memcpy(rbuf, buff, rlen);

	mutex_unlock(&d->usb_mutex);

	return (ret < 0) ? -ENODEV : 0;
}

static int lme2510_stream_restart(struct dvb_usb_device *d)
{
	static u8 stream_on[] = LME_ST_ON_W;
	int ret;
	u8 rbuff[10];
	/*Restart Stream Command*/
	ret = lme2510_usb_talk(d, stream_on, sizeof(stream_on),
			rbuff, sizeof(rbuff));
	return ret;
}

static int lme2510_enable_pid(struct dvb_usb_device *d, u8 index, u16 pid_out)
{
	struct lme2510_state *st = d->priv;
	static u8 pid_buff[] = LME_ZERO_PID;
	static u8 rbuf[1];
	u8 pid_no = index * 2;
	u8 pid_len = pid_no + 2;
	int ret = 0;
	deb_info(1, "PID Setting Pid %04x", pid_out);

	if (st->pid_size == 0)
		ret |= lme2510_stream_restart(d);

	pid_buff[2] = pid_no;
	pid_buff[3] = (u8)pid_out & 0xff;
	pid_buff[4] = pid_no + 1;
	pid_buff[5] = (u8)(pid_out >> 8);

	if (pid_len > st->pid_size)
		st->pid_size = pid_len;
	pid_buff[7] = 0x80 + st->pid_size;

	ret |= lme2510_usb_talk(d, pid_buff ,
		sizeof(pid_buff) , rbuf, sizeof(rbuf));

	if (st->stream_on)
		ret |= lme2510_stream_restart(d);

	return ret;
}

static void lme2510_int_response(struct urb *lme_urb)
{
	struct dvb_usb_adapter *adap = lme_urb->context;
	struct lme2510_state *st = adap->dev->priv;
	static u8 *ibuf, *rbuf;
	int i = 0, offset;
	u32 key;

	switch (lme_urb->status) {
	case 0:
	case -ETIMEDOUT:
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		return;
	default:
		info("Error %x", lme_urb->status);
		break;
	}

	rbuf = (u8 *) lme_urb->transfer_buffer;

	offset = ((lme_urb->actual_length/8) > 4)
			? 4 : (lme_urb->actual_length/8) ;

	for (i = 0; i < offset; ++i) {
		ibuf = (u8 *)&rbuf[i*8];
		deb_info(5, "INT O/S C =%02x C/O=%02x Type =%02x%02x",
		offset, i, ibuf[0], ibuf[1]);

		switch (ibuf[0]) {
		case 0xaa:
			debug_data_snipet(1, "INT Remote data snipet", ibuf);
			if ((ibuf[4] + ibuf[5]) == 0xff) {
				key = ibuf[5];
				key += (ibuf[3] > 0)
					? (ibuf[3] ^ 0xff) << 8 : 0;
				key += (ibuf[2] ^ 0xff) << 16;
				deb_info(1, "INT Key =%08x", key);
				if (adap->dev->rc_dev != NULL)
					rc_keydown(adap->dev->rc_dev, key, 0);
			}
			break;
		case 0xbb:
			switch (st->tuner_config) {
			case TUNER_LG:
				if (ibuf[2] > 0)
					st->signal_lock = ibuf[2];
				st->signal_level = ibuf[4];
				st->signal_sn = ibuf[3];
				st->time_key = ibuf[7];
				break;
			case TUNER_S7395:
			case TUNER_S0194:
				/* Tweak for earlier firmware*/
				if (ibuf[1] == 0x03) {
					if (ibuf[2] > 1)
						st->signal_lock = ibuf[2];
					st->signal_level = ibuf[3];
					st->signal_sn = ibuf[4];
				} else {
					st->signal_level = ibuf[4];
					st->signal_sn = ibuf[5];
					st->signal_lock =
						(st->signal_lock & 0xf7) +
						((ibuf[2] & 0x01) << 0x03);
				}
				break;
			default:
				break;
			}
			debug_data_snipet(5, "INT Remote data snipet in", ibuf);
		break;
		case 0xcc:
			debug_data_snipet(1, "INT Control data snipet", ibuf);
			break;
		default:
			debug_data_snipet(1, "INT Unknown data snipet", ibuf);
		break;
		}
	}
	usb_submit_urb(lme_urb, GFP_ATOMIC);
}

static int lme2510_int_read(struct dvb_usb_adapter *adap)
{
	struct lme2510_state *lme_int = adap->dev->priv;

	lme_int->lme_urb = usb_alloc_urb(0, GFP_ATOMIC);

	if (lme_int->lme_urb == NULL)
			return -ENOMEM;

	lme_int->buffer = usb_alloc_coherent(adap->dev->udev, 128, GFP_ATOMIC,
					&lme_int->lme_urb->transfer_dma);

	if (lme_int->buffer == NULL)
			return -ENOMEM;

	usb_fill_int_urb(lme_int->lme_urb,
				adap->dev->udev,
				usb_rcvintpipe(adap->dev->udev, 0xa),
				lme_int->buffer,
				128,
				lme2510_int_response,
				adap,
				8);

	lme_int->lme_urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	usb_submit_urb(lme_int->lme_urb, GFP_ATOMIC);
	info("INT Interrupt Service Started");

	return 0;
}

static int lme2510_pid_filter_ctrl(struct dvb_usb_adapter *adap, int onoff)
{
	struct lme2510_state *st = adap->dev->priv;
	static u8 clear_pid_reg[] = LME_CLEAR_PID;
	static u8 rbuf[1];
	int ret;

	deb_info(1, "PID Clearing Filter");

	ret = mutex_lock_interruptible(&adap->dev->i2c_mutex);
	if (ret < 0)
		return -EAGAIN;

	if (!onoff)
		ret |= lme2510_usb_talk(adap->dev, clear_pid_reg,
			sizeof(clear_pid_reg), rbuf, sizeof(rbuf));

	st->pid_size = 0;

	mutex_unlock(&adap->dev->i2c_mutex);

	return 0;
}

static int lme2510_pid_filter(struct dvb_usb_adapter *adap, int index, u16 pid,
	int onoff)
{
	int ret = 0;

	deb_info(3, "%s PID=%04x Index=%04x onoff=%02x", __func__,
		pid, index, onoff);

	if (onoff)
		if (!pid_filter) {
			ret = mutex_lock_interruptible(&adap->dev->i2c_mutex);
			if (ret < 0)
				return -EAGAIN;
			ret |= lme2510_enable_pid(adap->dev, index, pid);
			mutex_unlock(&adap->dev->i2c_mutex);
	}


	return ret;
}


static int lme2510_return_status(struct usb_device *dev)
{
	int ret = 0;
	u8 *data;

	data = kzalloc(10, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	ret |= usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
			0x06, 0x80, 0x0302, 0x00, data, 0x0006, 200);
	info("Firmware Status: %x (%x)", ret , data[2]);

	ret = (ret < 0) ? -ENODEV : data[2];
	kfree(data);
	return ret;
}

static int lme2510_msg(struct dvb_usb_device *d,
		u8 *wbuf, int wlen, u8 *rbuf, int rlen)
{
	int ret = 0;
	struct lme2510_state *st = d->priv;

	if (mutex_lock_interruptible(&d->i2c_mutex) < 0)
			return -EAGAIN;

	if (st->i2c_talk_onoff == 1) {

		ret = lme2510_usb_talk(d, wbuf, wlen, rbuf, rlen);

		switch (st->tuner_config) {
		case TUNER_LG:
			if (wbuf[2] == 0x1c) {
				if (wbuf[3] == 0x0e) {
					st->signal_lock = rbuf[1];
					if ((st->stream_on & 1) &&
						(st->signal_lock & 0x10)) {
						lme2510_stream_restart(d);
						st->i2c_talk_onoff = 0;
					}
					msleep(80);
				}
			}
			break;
		case TUNER_S7395:
			if (wbuf[2] == 0xd0) {
				if (wbuf[3] == 0x24) {
					st->signal_lock = rbuf[1];
					if ((st->stream_on & 1) &&
						(st->signal_lock & 0x8)) {
						lme2510_stream_restart(d);
						st->i2c_talk_onoff = 0;
					}
				}
				if ((wbuf[3] != 0x6) & (wbuf[3] != 0x5))
					msleep(5);
			}
			break;
		case TUNER_S0194:
			if (wbuf[2] == 0xd0) {
				if (wbuf[3] == 0x1b) {
					st->signal_lock = rbuf[1];
					if ((st->stream_on & 1) &&
						(st->signal_lock & 0x8)) {
						lme2510_stream_restart(d);
						st->i2c_talk_onoff = 0;
					}
				}
			}
			break;
		default:
			break;
		}
	} else {
		switch (st->tuner_config) {
		case TUNER_LG:
			switch (wbuf[3]) {
			case 0x0e:
				rbuf[0] = 0x55;
				rbuf[1] = st->signal_lock;
				break;
			case 0x43:
				rbuf[0] = 0x55;
				rbuf[1] = st->signal_level;
				break;
			case 0x1c:
				rbuf[0] = 0x55;
				rbuf[1] = st->signal_sn;
				break;
			case 0x15:
			case 0x16:
			case 0x17:
			case 0x18:
				rbuf[0] = 0x55;
				rbuf[1] = 0x00;
				break;
			default:
				lme2510_usb_talk(d, wbuf, wlen, rbuf, rlen);
				st->i2c_talk_onoff = 1;
				break;
			}
			break;
		case TUNER_S7395:
			switch (wbuf[3]) {
			case 0x10:
				rbuf[0] = 0x55;
				rbuf[1] = (st->signal_level & 0x80)
						? 0 : (st->signal_level * 2);
				break;
			case 0x2d:
				rbuf[0] = 0x55;
				rbuf[1] = st->signal_sn;
				break;
			case 0x24:
				rbuf[0] = 0x55;
				rbuf[1] = st->signal_lock;
				break;
			case 0x2e:
			case 0x26:
			case 0x27:
				rbuf[0] = 0x55;
				rbuf[1] = 0x00;
				break;
			default:
				lme2510_usb_talk(d, wbuf, wlen, rbuf, rlen);
				st->i2c_talk_onoff = 1;
				break;
			}
			break;
		case TUNER_S0194:
			switch (wbuf[3]) {
			case 0x18:
				rbuf[0] = 0x55;
				rbuf[1] = (st->signal_level & 0x80)
						? 0 : (st->signal_level * 2);
				break;
			case 0x24:
				rbuf[0] = 0x55;
				rbuf[1] = st->signal_sn;
				break;
			case 0x1b:
				rbuf[0] = 0x55;
				rbuf[1] = st->signal_lock;
				break;
			case 0x19:
			case 0x25:
			case 0x1e:
			case 0x1d:
				rbuf[0] = 0x55;
				rbuf[1] = 0x00;
				break;
			default:
				lme2510_usb_talk(d, wbuf, wlen, rbuf, rlen);
				st->i2c_talk_onoff = 1;
				break;
			}
			break;
		default:
			break;
		}

		deb_info(4, "I2C From Interrupt Message out(%02x) in(%02x)",
				wbuf[3], rbuf[1]);

	}

	mutex_unlock(&d->i2c_mutex);

	return ret;
}


static int lme2510_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg msg[],
				 int num)
{
	struct dvb_usb_device *d = i2c_get_adapdata(adap);
	struct lme2510_state *st = d->priv;
	static u8 obuf[64], ibuf[64];
	int i, read, read_o;
	u16 len;
	u8 gate = st->i2c_gate;

	if (gate == 0)
		gate = 5;

	if (num > 2)
		warn("more than 2 i2c messages"
			"at a time is not handled yet.	TODO.");

	for (i = 0; i < num; i++) {
		read_o = 1 & (msg[i].flags & I2C_M_RD);
		read = i+1 < num && (msg[i+1].flags & I2C_M_RD);
		read |= read_o;
		gate = (msg[i].addr == st->i2c_tuner_addr)
			? (read)	? st->i2c_tuner_gate_r
					: st->i2c_tuner_gate_w
			: st->i2c_gate;
		obuf[0] = gate | (read << 7);

		if (gate == 5)
			obuf[1] = (read) ? 2 : msg[i].len + 1;
		else
			obuf[1] = msg[i].len + read + 1;

		obuf[2] = msg[i].addr;
		if (read) {
			if (read_o)
				len = 3;
			else {
				memcpy(&obuf[3], msg[i].buf, msg[i].len);
				obuf[msg[i].len+3] = msg[i+1].len;
				len = msg[i].len+4;
			}
		} else {
			memcpy(&obuf[3], msg[i].buf, msg[i].len);
			len = msg[i].len+3;
		}

		if (lme2510_msg(d, obuf, len, ibuf, 64) < 0) {
			deb_info(1, "i2c transfer failed.");
			return -EAGAIN;
		}

		if (read) {
			if (read_o)
				memcpy(msg[i].buf, &ibuf[1], msg[i].len);
			else {
				memcpy(msg[i+1].buf, &ibuf[1], msg[i+1].len);
				i++;
			}
		}
	}
	return i;
}

static u32 lme2510_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C;
}

static struct i2c_algorithm lme2510_i2c_algo = {
	.master_xfer   = lme2510_i2c_xfer,
	.functionality = lme2510_i2c_func,
};

/* Callbacks for DVB USB */
static int lme2510_identify_state(struct usb_device *udev,
		struct dvb_usb_device_properties *props,
		struct dvb_usb_device_description **desc,
		int *cold)
{
	*cold = 0;
	return 0;
}

static int lme2510_streaming_ctrl(struct dvb_usb_adapter *adap, int onoff)
{
	struct lme2510_state *st = adap->dev->priv;
	static u8 clear_reg_3[] = LME_CLEAR_PID;
	static u8 rbuf[1];
	int ret = 0, rlen = sizeof(rbuf);

	deb_info(1, "STM  (%02x)", onoff);

	/* Streaming is started by FE_HAS_LOCK */
	if (onoff == 1)
		st->stream_on = 1;
	else {
		deb_info(1, "STM Steam Off");
		/* mutex is here only to avoid collision with I2C */
		if (mutex_lock_interruptible(&adap->dev->i2c_mutex) < 0)
			return -EAGAIN;

		ret = lme2510_usb_talk(adap->dev, clear_reg_3,
				sizeof(clear_reg_3), rbuf, rlen);
		st->stream_on = 0;
		st->i2c_talk_onoff = 1;

		mutex_unlock(&adap->dev->i2c_mutex);
	}

	return (ret < 0) ? -ENODEV : 0;
}

static u8 check_sum(u8 *p, u8 len)
{
	u8 sum = 0;
	while (len--)
		sum += *p++;
	return sum;
}

static int lme2510_download_firmware(struct usb_device *dev,
					const struct firmware *fw)
{
	int ret = 0;
	u8 *data;
	u16 j, wlen, len_in, start, end;
	u8 packet_size, dlen, i;
	u8 *fw_data;

	packet_size = 0x31;
	len_in = 1;

	data = kzalloc(512, GFP_KERNEL);
	if (!data) {
		info("FRM Could not start Firmware Download (Buffer allocation failed)");
		return -ENOMEM;
	}

	info("FRM Starting Firmware Download");

	for (i = 1; i < 3; i++) {
		start = (i == 1) ? 0 : 512;
		end = (i == 1) ? 512 : fw->size;
		for (j = start; j < end; j += (packet_size+1)) {
			fw_data = (u8 *)(fw->data + j);
			if ((end - j) > packet_size) {
				data[0] = i;
				dlen = packet_size;
			} else {
				data[0] = i | 0x80;
				dlen = (u8)(end - j)-1;
			}
			data[1] = dlen;
			memcpy(&data[2], fw_data, dlen+1);
			wlen = (u8) dlen + 4;
			data[wlen-1] = check_sum(fw_data, dlen+1);
			deb_info(1, "Data S=%02x:E=%02x CS= %02x", data[3],
				data[dlen+2], data[dlen+3]);
			ret |= lme2510_bulk_write(dev, data,  wlen, 1);
			ret |= lme2510_bulk_read(dev, data, len_in , 1);
			ret |= (data[0] == 0x88) ? 0 : -1;
		}
	}

	usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
			0x06, 0x80, 0x0200, 0x00, data, 0x0109, 1000);


	data[0] = 0x8a;
	len_in = 1;
	msleep(2000);
	ret |= lme2510_bulk_write(dev, data , len_in, 1); /*Resetting*/
	ret |= lme2510_bulk_read(dev, data, len_in, 1);
	msleep(400);

	if (ret < 0)
		info("FRM Firmware Download Failed (%04x)" , ret);
	else
		info("FRM Firmware Download Completed - Resetting Device");

	kfree(data);
	return (ret < 0) ? -ENODEV : 0;
}

static void lme_coldreset(struct usb_device *dev)
{
	int ret = 0, len_in;
	u8 data[512] = {0};

	data[0] = 0x0a;
	len_in = 1;
	info("FRM Firmware Cold Reset");
	ret |= lme2510_bulk_write(dev, data , len_in, 1); /*Cold Resetting*/
	ret |= lme2510_bulk_read(dev, data, len_in, 1);

	return;
}

static int lme_firmware_switch(struct usb_device *udev, int cold)
{
	const struct firmware *fw = NULL;
	const char fw_c_s7395[] = "dvb-usb-lme2510c-s7395.fw";
	const char fw_c_lg[] = "dvb-usb-lme2510c-lg.fw";
	const char fw_c_s0194[] = "dvb-usb-lme2510c-s0194.fw";
	const char fw_lg[] = "dvb-usb-lme2510-lg.fw";
	const char fw_s0194[] = "dvb-usb-lme2510-s0194.fw";
	const char *fw_lme;
	int ret, cold_fw;

	cold = (cold > 0) ? (cold & 1) : 0;

	cold_fw = !cold;

	if (le16_to_cpu(udev->descriptor.idProduct) == 0x1122) {
		switch (dvb_usb_lme2510_firmware) {
		default:
			dvb_usb_lme2510_firmware = TUNER_S0194;
		case TUNER_S0194:
			fw_lme = fw_s0194;
			ret = request_firmware(&fw, fw_lme, &udev->dev);
			if (ret == 0) {
				cold = 0;
				break;
			}
			dvb_usb_lme2510_firmware = TUNER_LG;
		case TUNER_LG:
			fw_lme = fw_lg;
			ret = request_firmware(&fw, fw_lme, &udev->dev);
			if (ret == 0)
				break;
			info("FRM No Firmware Found - please install");
			dvb_usb_lme2510_firmware = TUNER_DEFAULT;
			cold = 0;
			cold_fw = 0;
			break;
		}
	} else {
		switch (dvb_usb_lme2510_firmware) {
		default:
			dvb_usb_lme2510_firmware = TUNER_S7395;
		case TUNER_S7395:
			fw_lme = fw_c_s7395;
			ret = request_firmware(&fw, fw_lme, &udev->dev);
			if (ret == 0) {
				cold = 0;
				break;
			}
			dvb_usb_lme2510_firmware = TUNER_LG;
		case TUNER_LG:
			fw_lme = fw_c_lg;
			ret = request_firmware(&fw, fw_lme, &udev->dev);
			if (ret == 0)
				break;
			dvb_usb_lme2510_firmware = TUNER_S0194;
		case TUNER_S0194:
			fw_lme = fw_c_s0194;
			ret = request_firmware(&fw, fw_lme, &udev->dev);
			if (ret == 0)
				break;
			info("FRM No Firmware Found - please install");
			dvb_usb_lme2510_firmware = TUNER_DEFAULT;
			cold = 0;
			cold_fw = 0;
			break;
		}
	}

	if (cold_fw) {
		info("FRM Loading %s file", fw_lme);
		ret = lme2510_download_firmware(udev, fw);
	}

	release_firmware(fw);

	if (cold) {
		info("FRM Changing to %s firmware", fw_lme);
		lme_coldreset(udev);
		return -ENODEV;
	}

	return ret;
}

static int lme2510_kill_urb(struct usb_data_stream *stream)
{
	int i;

	for (i = 0; i < stream->urbs_submitted; i++) {
		deb_info(3, "killing URB no. %d.", i);
		/* stop the URB */
		usb_kill_urb(stream->urb_list[i]);
	}
	stream->urbs_submitted = 0;

	return 0;
}

static struct tda10086_config tda10086_config = {
	.demod_address = 0x1c,
	.invert = 0,
	.diseqc_tone = 1,
	.xtal_freq = TDA10086_XTAL_16M,
};

static struct stv0288_config lme_config = {
	.demod_address = 0xd0,
	.min_delay_ms = 15,
	.inittab = s7395_inittab,
};

static struct ix2505v_config lme_tuner = {
	.tuner_address = 0xc0,
	.min_delay_ms = 100,
	.tuner_gain = 0x0,
	.tuner_chargepump = 0x3,
};

static struct stv0299_config sharp_z0194_config = {
	.demod_address = 0xd0,
	.inittab = sharp_z0194a_inittab,
	.mclk = 88000000UL,
	.invert = 0,
	.skip_reinit = 0,
	.lock_output = STV0299_LOCKOUTPUT_1,
	.volt13_op0_op1 = STV0299_VOLT13_OP1,
	.min_delay_ms = 100,
	.set_symbol_rate = sharp_z0194a_set_symbol_rate,
};

static int dm04_lme2510_set_voltage(struct dvb_frontend *fe,
					fe_sec_voltage_t voltage)
{
	struct dvb_usb_adapter *adap = fe->dvb->priv;
	static u8 voltage_low[]	= LME_VOLTAGE_L;
	static u8 voltage_high[] = LME_VOLTAGE_H;
	static u8 rbuf[1];
	int ret = 0, len = 3, rlen = 1;

	if (mutex_lock_interruptible(&adap->dev->i2c_mutex) < 0)
			return -EAGAIN;

	switch (voltage) {
	case SEC_VOLTAGE_18:
		ret |= lme2510_usb_talk(adap->dev,
			voltage_high, len, rbuf, rlen);
		break;

	case SEC_VOLTAGE_OFF:
	case SEC_VOLTAGE_13:
	default:
		ret |= lme2510_usb_talk(adap->dev,
				voltage_low, len, rbuf, rlen);
		break;
	}

	mutex_unlock(&adap->dev->i2c_mutex);

	return (ret < 0) ? -ENODEV : 0;
}

static int lme_name(struct dvb_usb_adapter *adap)
{
	struct lme2510_state *st = adap->dev->priv;
	const char *desc = adap->dev->desc->name;
	char *fe_name[] = {"", " LG TDQY-P001F", " SHARP:BS2F7HZ7395",
				" SHARP:BS2F7HZ0194"};
	char *name = adap->fe_adap[0].fe->ops.info.name;

	strlcpy(name, desc, 128);
	strlcat(name, fe_name[st->tuner_config], 128);

	return 0;
}

static int dm04_lme2510_frontend_attach(struct dvb_usb_adapter *adap)
{
	struct lme2510_state *st = adap->dev->priv;

	int ret = 0;

	st->i2c_talk_onoff = 1;

	st->i2c_gate = 4;
	adap->fe_adap[0].fe = dvb_attach(tda10086_attach, &tda10086_config,
		&adap->dev->i2c_adap);

	if (adap->fe_adap[0].fe) {
		info("TUN Found Frontend TDA10086");
		st->i2c_tuner_gate_w = 4;
		st->i2c_tuner_gate_r = 4;
		st->i2c_tuner_addr = 0xc0;
		st->tuner_config = TUNER_LG;
		if (dvb_usb_lme2510_firmware != TUNER_LG) {
			dvb_usb_lme2510_firmware = TUNER_LG;
			ret = lme_firmware_switch(adap->dev->udev, 1);
		}
		goto end;
	}

	st->i2c_gate = 4;
	adap->fe_adap[0].fe = dvb_attach(stv0299_attach, &sharp_z0194_config,
			&adap->dev->i2c_adap);
	if (adap->fe_adap[0].fe) {
		info("FE Found Stv0299");
		st->i2c_tuner_gate_w = 4;
		st->i2c_tuner_gate_r = 5;
		st->i2c_tuner_addr = 0xc0;
		st->tuner_config = TUNER_S0194;
		if (dvb_usb_lme2510_firmware != TUNER_S0194) {
			dvb_usb_lme2510_firmware = TUNER_S0194;
			ret = lme_firmware_switch(adap->dev->udev, 1);
		}
		goto end;
	}

	st->i2c_gate = 5;
	adap->fe_adap[0].fe = dvb_attach(stv0288_attach, &lme_config,
			&adap->dev->i2c_adap);
	if (adap->fe_adap[0].fe) {
		info("FE Found Stv0288");
		st->i2c_tuner_gate_w = 4;
		st->i2c_tuner_gate_r = 5;
		st->i2c_tuner_addr = 0xc0;
		st->tuner_config = TUNER_S7395;
		if (dvb_usb_lme2510_firmware != TUNER_S7395) {
			dvb_usb_lme2510_firmware = TUNER_S7395;
			ret = lme_firmware_switch(adap->dev->udev, 1);
		}
	} else {
		info("DM04 Not Supported");
		return -ENODEV;
	}


end:	if (ret) {
		if (adap->fe_adap[0].fe) {
			dvb_frontend_detach(adap->fe_adap[0].fe);
			adap->fe_adap[0].fe = NULL;
		}
		adap->dev->props.rc.core.rc_codes = NULL;
		return -ENODEV;
	}

	adap->fe_adap[0].fe->ops.set_voltage = dm04_lme2510_set_voltage;
	ret = lme_name(adap);
	return ret;
}

static int dm04_lme2510_tuner(struct dvb_usb_adapter *adap)
{
	struct lme2510_state *st = adap->dev->priv;
	char *tun_msg[] = {"", "TDA8263", "IX2505V", "DVB_PLL_OPERA"};
	int ret = 0;

	switch (st->tuner_config) {
	case TUNER_LG:
		if (dvb_attach(tda826x_attach, adap->fe_adap[0].fe, 0xc0,
			&adap->dev->i2c_adap, 1))
			ret = st->tuner_config;
		break;
	case TUNER_S7395:
		if (dvb_attach(ix2505v_attach , adap->fe_adap[0].fe, &lme_tuner,
			&adap->dev->i2c_adap))
			ret = st->tuner_config;
		break;
	case TUNER_S0194:
		if (dvb_attach(dvb_pll_attach , adap->fe_adap[0].fe, 0xc0,
			&adap->dev->i2c_adap, DVB_PLL_OPERA1))
			ret = st->tuner_config;
		break;
	default:
		break;
	}

	if (ret)
		info("TUN Found %s tuner", tun_msg[ret]);
	else {
		info("TUN No tuner found --- reseting device");
		lme_coldreset(adap->dev->udev);
		return -ENODEV;
	}

	/* Start the Interrupt*/
	ret = lme2510_int_read(adap);
	if (ret < 0) {
		info("INT Unable to start Interrupt Service");
		return -ENODEV;
	}

	return ret;
}

static int lme2510_powerup(struct dvb_usb_device *d, int onoff)
{
	struct lme2510_state *st = d->priv;
	static u8 lnb_on[] = LNB_ON;
	static u8 lnb_off[] = LNB_OFF;
	static u8 rbuf[1];
	int ret, len = 3, rlen = 1;

	if (mutex_lock_interruptible(&d->i2c_mutex) < 0)
		return -EAGAIN;

	if (onoff)
		ret = lme2510_usb_talk(d, lnb_on, len, rbuf, rlen);
	else
		ret = lme2510_usb_talk(d, lnb_off, len, rbuf, rlen);

	st->i2c_talk_onoff = 1;

	mutex_unlock(&d->i2c_mutex);

	return ret;
}

/* DVB USB Driver stuff */
static struct dvb_usb_device_properties lme2510_properties;
static struct dvb_usb_device_properties lme2510c_properties;

static int lme2510_probe(struct usb_interface *intf,
		const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	int ret = 0;

	usb_reset_configuration(udev);

	usb_set_interface(udev, intf->cur_altsetting->desc.bInterfaceNumber, 1);

	if (udev->speed != USB_SPEED_HIGH) {
		ret = usb_reset_device(udev);
		info("DEV Failed to connect in HIGH SPEED mode");
		return -ENODEV;
	}

	if (lme2510_return_status(udev) == 0x44) {
		lme_firmware_switch(udev, 0);
		return -ENODEV;
	}

	if (0 == dvb_usb_device_init(intf, &lme2510_properties,
				     THIS_MODULE, NULL, adapter_nr)) {
		info("DEV registering device driver");
		return 0;
	}
	if (0 == dvb_usb_device_init(intf, &lme2510c_properties,
				     THIS_MODULE, NULL, adapter_nr)) {
		info("DEV registering device driver");
		return 0;
	}

	info("DEV lme2510 Error");
	return -ENODEV;

}

static struct usb_device_id lme2510_table[] = {
	{ USB_DEVICE(0x3344, 0x1122) },  /* LME2510 */
	{ USB_DEVICE(0x3344, 0x1120) },  /* LME2510C */
	{}		/* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, lme2510_table);

static struct dvb_usb_device_properties lme2510_properties = {
	.caps = DVB_USB_IS_AN_I2C_ADAPTER,
	.size_of_priv = sizeof(struct lme2510_state),
	.num_adapters = 1,
	.adapter = {
		{
		.num_frontends = 1,
		.fe = {{
			.caps = DVB_USB_ADAP_HAS_PID_FILTER|
				DVB_USB_ADAP_NEED_PID_FILTERING|
				DVB_USB_ADAP_PID_FILTER_CAN_BE_TURNED_OFF,
			.streaming_ctrl   = lme2510_streaming_ctrl,
			.pid_filter_count = 15,
			.pid_filter = lme2510_pid_filter,
			.pid_filter_ctrl  = lme2510_pid_filter_ctrl,
			.frontend_attach  = dm04_lme2510_frontend_attach,
			.tuner_attach = dm04_lme2510_tuner,
			/* parameter for the MPEG2-data transfer */
			.stream = {
				.type = USB_BULK,
				.count = 10,
				.endpoint = 0x06,
				.u = {
					.bulk = {
						.buffersize = 4096,

					}
				}
			}
		}},
		}
	},
	.rc.core = {
		.protocol	= RC_TYPE_NEC,
		.module_name	= "LME2510 Remote Control",
		.allowed_protos	= RC_TYPE_NEC,
		.rc_codes	= RC_MAP_LME2510,
	},
	.power_ctrl       = lme2510_powerup,
	.identify_state   = lme2510_identify_state,
	.i2c_algo         = &lme2510_i2c_algo,
	.generic_bulk_ctrl_endpoint = 0,
	.num_device_descs = 1,
	.devices = {
		{   "DM04_LME2510_DVB-S",
			{ &lme2510_table[0], NULL },
			},

	}
};

static struct dvb_usb_device_properties lme2510c_properties = {
	.caps = DVB_USB_IS_AN_I2C_ADAPTER,
	.size_of_priv = sizeof(struct lme2510_state),
	.num_adapters = 1,
	.adapter = {
		{
		.num_frontends = 1,
		.fe = {{
			.caps = DVB_USB_ADAP_HAS_PID_FILTER|
				DVB_USB_ADAP_NEED_PID_FILTERING|
				DVB_USB_ADAP_PID_FILTER_CAN_BE_TURNED_OFF,
			.streaming_ctrl   = lme2510_streaming_ctrl,
			.pid_filter_count = 15,
			.pid_filter = lme2510_pid_filter,
			.pid_filter_ctrl  = lme2510_pid_filter_ctrl,
			.frontend_attach  = dm04_lme2510_frontend_attach,
			.tuner_attach = dm04_lme2510_tuner,
			/* parameter for the MPEG2-data transfer */
			.stream = {
				.type = USB_BULK,
				.count = 10,
				.endpoint = 0x8,
				.u = {
					.bulk = {
						.buffersize = 4096,

					}
				}
			}
		}},
		}
	},
	.rc.core = {
		.protocol	= RC_TYPE_NEC,
		.module_name	= "LME2510 Remote Control",
		.allowed_protos	= RC_TYPE_NEC,
		.rc_codes	= RC_MAP_LME2510,
	},
	.power_ctrl       = lme2510_powerup,
	.identify_state   = lme2510_identify_state,
	.i2c_algo         = &lme2510_i2c_algo,
	.generic_bulk_ctrl_endpoint = 0,
	.num_device_descs = 1,
	.devices = {
		{   "DM04_LME2510C_DVB-S",
			{ &lme2510_table[1], NULL },
			},
	}
};

static void *lme2510_exit_int(struct dvb_usb_device *d)
{
	struct lme2510_state *st = d->priv;
	struct dvb_usb_adapter *adap = &d->adapter[0];
	void *buffer = NULL;

	if (adap != NULL) {
		lme2510_kill_urb(&adap->fe_adap[0].stream);
		adap->feedcount = 0;
	}

	if (st->usb_buffer != NULL) {
		st->i2c_talk_onoff = 1;
		st->signal_lock = 0;
		st->signal_level = 0;
		st->signal_sn = 0;
		buffer = st->usb_buffer;
	}

	if (st->lme_urb != NULL) {
		usb_kill_urb(st->lme_urb);
		usb_free_coherent(d->udev, 128, st->buffer,
				  st->lme_urb->transfer_dma);
		info("Interrupt Service Stopped");
	}

	return buffer;
}

static void lme2510_exit(struct usb_interface *intf)
{
	struct dvb_usb_device *d = usb_get_intfdata(intf);
	void *usb_buffer;

	if (d != NULL) {
		usb_buffer = lme2510_exit_int(d);
		dvb_usb_device_exit(intf);
		if (usb_buffer != NULL)
			kfree(usb_buffer);
	}
}

static struct usb_driver lme2510_driver = {
	.name		= "LME2510C_DVB-S",
	.probe		= lme2510_probe,
	.disconnect	= lme2510_exit,
	.id_table	= lme2510_table,
};

/* module stuff */
static int __init lme2510_module_init(void)
{
	int result = usb_register(&lme2510_driver);
	if (result) {
		err("usb_register failed. Error number %d", result);
		return result;
	}

	return 0;
}

static void __exit lme2510_module_exit(void)
{
	/* deregister this driver from the USB subsystem */
	usb_deregister(&lme2510_driver);
}

module_init(lme2510_module_init);
module_exit(lme2510_module_exit);

MODULE_AUTHOR("Malcolm Priestley <tvboxspy@gmail.com>");
MODULE_DESCRIPTION("LME2510(C) DVB-S USB2.0");
MODULE_VERSION("1.90");
MODULE_LICENSE("GPL");
