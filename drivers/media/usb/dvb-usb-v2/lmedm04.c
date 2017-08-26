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
 * LME2510C + M88RS2000
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
 * M88RS2000 suffers from loss of lock.
 */
#define DVB_USB_LOG_PREFIX "LME2510(C)"
#include <linux/usb.h>
#include <linux/usb/input.h>
#include <media/rc-core.h>

#include "dvb_usb.h"
#include "lmedm04.h"
#include "tda826x.h"
#include "tda10086.h"
#include "stv0288.h"
#include "ix2505v.h"
#include "stv0299.h"
#include "dvb-pll.h"
#include "z0194a.h"
#include "m88rs2000.h"
#include "ts2020.h"


#define LME2510_C_S7395	"dvb-usb-lme2510c-s7395.fw";
#define LME2510_C_LG	"dvb-usb-lme2510c-lg.fw";
#define LME2510_C_S0194	"dvb-usb-lme2510c-s0194.fw";
#define LME2510_C_RS2000 "dvb-usb-lme2510c-rs2000.fw";
#define LME2510_LG	"dvb-usb-lme2510-lg.fw";
#define LME2510_S0194	"dvb-usb-lme2510-s0194.fw";

/* debug */
static int dvb_usb_lme2510_debug;
#define lme_debug(var, level, args...) do { \
	if ((var >= level)) \
		pr_debug(DVB_USB_LOG_PREFIX": " args); \
} while (0)
#define deb_info(level, args...) lme_debug(dvb_usb_lme2510_debug, level, args)
#define debug_data_snipet(level, name, p) \
	 deb_info(level, name" (%8phN)", p);
#define info(args...) pr_info(DVB_USB_LOG_PREFIX": "args)

module_param_named(debug, dvb_usb_lme2510_debug, int, 0644);
MODULE_PARM_DESC(debug, "set debugging level (1=info (or-able)).");

static int dvb_usb_lme2510_firmware;
module_param_named(firmware, dvb_usb_lme2510_firmware, int, 0644);
MODULE_PARM_DESC(firmware, "set default firmware 0=Sharp7395 1=LG");

static int pid_filter;
module_param_named(pid, pid_filter, int, 0644);
MODULE_PARM_DESC(pid, "set default 0=default 1=off 2=on");


DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

#define TUNER_DEFAULT	0x0
#define TUNER_LG	0x1
#define TUNER_S7395	0x2
#define TUNER_S0194	0x3
#define TUNER_RS2000	0x4

struct lme2510_state {
	unsigned long int_urb_due;
	enum fe_status lock_status;
	u8 id;
	u8 tuner_config;
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
	u8 pid_off;
	void *buffer;
	struct urb *lme_urb;
	void *usb_buffer;
	/* Frontend original calls */
	int (*fe_read_status)(struct dvb_frontend *, enum fe_status *);
	int (*fe_read_signal_strength)(struct dvb_frontend *, u16 *);
	int (*fe_read_snr)(struct dvb_frontend *, u16 *);
	int (*fe_read_ber)(struct dvb_frontend *, u32 *);
	int (*fe_read_ucblocks)(struct dvb_frontend *, u32 *);
	int (*fe_set_voltage)(struct dvb_frontend *, enum fe_sec_voltage);
	u8 dvb_usb_lme2510_firmware;
};

static int lme2510_bulk_write(struct usb_device *dev,
				u8 *snd, int len, u8 pipe)
{
	int actual_l;

	return usb_bulk_msg(dev, usb_sndbulkpipe(dev, pipe),
			    snd, len, &actual_l, 100);
}

static int lme2510_bulk_read(struct usb_device *dev,
				u8 *rev, int len, u8 pipe)
{
	int actual_l;

	return usb_bulk_msg(dev, usb_rcvbulkpipe(dev, pipe),
			    rev, len, &actual_l, 200);
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

	ret |= lme2510_bulk_write(d->udev, buff, wlen , 0x01);

	ret |= lme2510_bulk_read(d->udev, buff, (rlen < 64) ?
			rlen : 64 , 0x01);

	if (rlen > 0)
		memcpy(rbuf, buff, rlen);

	mutex_unlock(&d->usb_mutex);

	return (ret < 0) ? -ENODEV : 0;
}

static int lme2510_stream_restart(struct dvb_usb_device *d)
{
	struct lme2510_state *st = d->priv;
	u8 all_pids[] = LME_ALL_PIDS;
	u8 stream_on[] = LME_ST_ON_W;
	u8 rbuff[1];
	if (st->pid_off)
		lme2510_usb_talk(d, all_pids, sizeof(all_pids),
				 rbuff, sizeof(rbuff));
	/*Restart Stream Command*/
	return lme2510_usb_talk(d, stream_on, sizeof(stream_on),
				rbuff, sizeof(rbuff));
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

/* Convert range from 0x00-0xff to 0x0000-0xffff */
#define reg_to_16bits(x)	((x) | ((x) << 8))

static void lme2510_update_stats(struct dvb_usb_adapter *adap)
{
	struct lme2510_state *st = adap_to_priv(adap);
	struct dvb_frontend *fe = adap->fe[0];
	struct dtv_frontend_properties *c;
	u32 s_tmp = 0, c_tmp = 0;

	if (!fe)
		return;

	c = &fe->dtv_property_cache;

	c->block_count.len = 1;
	c->block_count.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	c->block_error.len = 1;
	c->block_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	c->post_bit_count.len = 1;
	c->post_bit_count.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	c->post_bit_error.len = 1;
	c->post_bit_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;

	if (st->i2c_talk_onoff) {
		c->strength.len = 1;
		c->strength.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
		c->cnr.len = 1;
		c->cnr.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
		return;
	}

	switch (st->tuner_config) {
	case TUNER_LG:
		s_tmp = reg_to_16bits(0xff - st->signal_level);
		c_tmp = reg_to_16bits(0xff - st->signal_sn);
		break;
	case TUNER_S7395:
	case TUNER_S0194:
		s_tmp = 0xffff - (((st->signal_level * 2) << 8) * 5 / 4);
		c_tmp = reg_to_16bits((0xff - st->signal_sn - 0xa1) * 3);
		break;
	case TUNER_RS2000:
		s_tmp = reg_to_16bits(st->signal_level);
		c_tmp = reg_to_16bits(st->signal_sn);
	}

	c->strength.len = 1;
	c->strength.stat[0].scale = FE_SCALE_RELATIVE;
	c->strength.stat[0].uvalue = (u64)s_tmp;

	c->cnr.len = 1;
	c->cnr.stat[0].scale = FE_SCALE_RELATIVE;
	c->cnr.stat[0].uvalue = (u64)c_tmp;
}

static void lme2510_int_response(struct urb *lme_urb)
{
	struct dvb_usb_adapter *adap = lme_urb->context;
	struct lme2510_state *st = adap_to_priv(adap);
	u8 *ibuf, *rbuf;
	int i = 0, offset;
	u32 key;
	u8 signal_lock = 0;

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
			if (!adap_to_d(adap)->rc_dev)
				break;

			key = RC_SCANCODE_NEC32(ibuf[2] << 24 |
						ibuf[3] << 16 |
						ibuf[4] << 8  |
						ibuf[5]);

			deb_info(1, "INT Key = 0x%08x", key);
			rc_keydown(adap_to_d(adap)->rc_dev, RC_TYPE_NEC32, key,
									0);
			break;
		case 0xbb:
			switch (st->tuner_config) {
			case TUNER_LG:
				signal_lock = ibuf[2] & BIT(5);
				st->signal_level = ibuf[4];
				st->signal_sn = ibuf[3];
				st->time_key = ibuf[7];
				break;
			case TUNER_S7395:
			case TUNER_S0194:
				/* Tweak for earlier firmware*/
				if (ibuf[1] == 0x03) {
					signal_lock = ibuf[2] & BIT(4);
					st->signal_level = ibuf[3];
					st->signal_sn = ibuf[4];
				} else {
					st->signal_level = ibuf[4];
					st->signal_sn = ibuf[5];
				}
				break;
			case TUNER_RS2000:
				signal_lock = ibuf[2] & 0xee;
				st->signal_level = ibuf[5];
				st->signal_sn = ibuf[4];
				st->time_key = ibuf[7];
			default:
				break;
			}

			/* Interrupt will also throw just BIT 0 as lock */
			signal_lock |= ibuf[2] & BIT(0);

			if (!signal_lock)
				st->lock_status &= ~FE_HAS_LOCK;

			lme2510_update_stats(adap);

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

	/* Interrupt urb is due every 48 msecs while streaming the buffer
	 * stores up to 4 periods if missed. Allow 200 msec for next interrupt.
	 */
	st->int_urb_due = jiffies + msecs_to_jiffies(200);
}

static int lme2510_int_read(struct dvb_usb_adapter *adap)
{
	struct dvb_usb_device *d = adap_to_d(adap);
	struct lme2510_state *lme_int = adap_to_priv(adap);
	struct usb_host_endpoint *ep;

	lme_int->lme_urb = usb_alloc_urb(0, GFP_ATOMIC);

	if (lme_int->lme_urb == NULL)
			return -ENOMEM;

	lme_int->buffer = usb_alloc_coherent(d->udev, 128, GFP_ATOMIC,
					&lme_int->lme_urb->transfer_dma);

	if (lme_int->buffer == NULL)
			return -ENOMEM;

	usb_fill_int_urb(lme_int->lme_urb,
				d->udev,
				usb_rcvintpipe(d->udev, 0xa),
				lme_int->buffer,
				128,
				lme2510_int_response,
				adap,
				8);

	/* Quirk of pipe reporting PIPE_BULK but behaves as interrupt */
	ep = usb_pipe_endpoint(d->udev, lme_int->lme_urb->pipe);

	if (usb_endpoint_type(&ep->desc) == USB_ENDPOINT_XFER_BULK)
		lme_int->lme_urb->pipe = usb_rcvbulkpipe(d->udev, 0xa),

	lme_int->lme_urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	usb_submit_urb(lme_int->lme_urb, GFP_ATOMIC);
	info("INT Interrupt Service Started");

	return 0;
}

static int lme2510_pid_filter_ctrl(struct dvb_usb_adapter *adap, int onoff)
{
	struct dvb_usb_device *d = adap_to_d(adap);
	struct lme2510_state *st = adap_to_priv(adap);
	static u8 clear_pid_reg[] = LME_ALL_PIDS;
	static u8 rbuf[1];
	int ret = 0;

	deb_info(1, "PID Clearing Filter");

	mutex_lock(&d->i2c_mutex);

	if (!onoff) {
		ret |= lme2510_usb_talk(d, clear_pid_reg,
			sizeof(clear_pid_reg), rbuf, sizeof(rbuf));
		st->pid_off = true;
	} else
		st->pid_off = false;

	st->pid_size = 0;

	mutex_unlock(&d->i2c_mutex);

	return 0;
}

static int lme2510_pid_filter(struct dvb_usb_adapter *adap, int index, u16 pid,
	int onoff)
{
	struct dvb_usb_device *d = adap_to_d(adap);
	int ret = 0;

	deb_info(3, "%s PID=%04x Index=%04x onoff=%02x", __func__,
		pid, index, onoff);

	if (onoff) {
		mutex_lock(&d->i2c_mutex);
		ret |= lme2510_enable_pid(d, index, pid);
		mutex_unlock(&d->i2c_mutex);
	}


	return ret;
}


static int lme2510_return_status(struct dvb_usb_device *d)
{
	int ret = 0;
	u8 *data;

	data = kzalloc(10, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	ret |= usb_control_msg(d->udev, usb_rcvctrlpipe(d->udev, 0),
			0x06, 0x80, 0x0302, 0x00, data, 0x0006, 200);
	info("Firmware Status: %x (%x)", ret , data[2]);

	ret = (ret < 0) ? -ENODEV : data[2];
	kfree(data);
	return ret;
}

static int lme2510_msg(struct dvb_usb_device *d,
		u8 *wbuf, int wlen, u8 *rbuf, int rlen)
{
	struct lme2510_state *st = d->priv;

	st->i2c_talk_onoff = 1;

	return lme2510_usb_talk(d, wbuf, wlen, rbuf, rlen);
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

	mutex_lock(&d->i2c_mutex);

	if (gate == 0)
		gate = 5;

	for (i = 0; i < num; i++) {
		read_o = msg[i].flags & I2C_M_RD;
		read = i + 1 < num && msg[i + 1].flags & I2C_M_RD;
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

		obuf[2] = msg[i].addr << 1;

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
			mutex_unlock(&d->i2c_mutex);
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

	mutex_unlock(&d->i2c_mutex);
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

static int lme2510_streaming_ctrl(struct dvb_frontend *fe, int onoff)
{
	struct dvb_usb_adapter *adap = fe_to_adap(fe);
	struct dvb_usb_device *d = adap_to_d(adap);
	struct lme2510_state *st = adap_to_priv(adap);
	static u8 clear_reg_3[] = LME_ALL_PIDS;
	static u8 rbuf[1];
	int ret = 0, rlen = sizeof(rbuf);

	deb_info(1, "STM  (%02x)", onoff);

	/* Streaming is started by FE_HAS_LOCK */
	if (onoff == 1)
		st->stream_on = 1;
	else {
		deb_info(1, "STM Steam Off");
		/* mutex is here only to avoid collision with I2C */
		mutex_lock(&d->i2c_mutex);

		ret = lme2510_usb_talk(d, clear_reg_3,
				sizeof(clear_reg_3), rbuf, rlen);
		st->stream_on = 0;
		st->i2c_talk_onoff = 1;

		mutex_unlock(&d->i2c_mutex);
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

static int lme2510_download_firmware(struct dvb_usb_device *d,
					const struct firmware *fw)
{
	int ret = 0;
	u8 *data;
	u16 j, wlen, len_in, start, end;
	u8 packet_size, dlen, i;
	u8 *fw_data;

	packet_size = 0x31;
	len_in = 1;

	data = kzalloc(128, GFP_KERNEL);
	if (!data) {
		info("FRM Could not start Firmware Download"\
			"(Buffer allocation failed)");
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
			lme2510_usb_talk(d, data, wlen, data, len_in);
			ret |= (data[0] == 0x88) ? 0 : -1;
		}
	}

	data[0] = 0x8a;
	len_in = 1;
	msleep(2000);
	lme2510_usb_talk(d, data, len_in, data, len_in);
	msleep(400);

	if (ret < 0)
		info("FRM Firmware Download Failed (%04x)" , ret);
	else
		info("FRM Firmware Download Completed - Resetting Device");

	kfree(data);
	return RECONNECTS_USB;
}

static void lme_coldreset(struct dvb_usb_device *d)
{
	u8 data[1] = {0};
	data[0] = 0x0a;
	info("FRM Firmware Cold Reset");

	lme2510_usb_talk(d, data, sizeof(data), data, sizeof(data));

	return;
}

static const char fw_c_s7395[] = LME2510_C_S7395;
static const char fw_c_lg[] = LME2510_C_LG;
static const char fw_c_s0194[] = LME2510_C_S0194;
static const char fw_c_rs2000[] = LME2510_C_RS2000;
static const char fw_lg[] = LME2510_LG;
static const char fw_s0194[] = LME2510_S0194;

static const char *lme_firmware_switch(struct dvb_usb_device *d, int cold)
{
	struct lme2510_state *st = d->priv;
	struct usb_device *udev = d->udev;
	const struct firmware *fw = NULL;
	const char *fw_lme;
	int ret = 0;

	cold = (cold > 0) ? (cold & 1) : 0;

	switch (le16_to_cpu(udev->descriptor.idProduct)) {
	case 0x1122:
		switch (st->dvb_usb_lme2510_firmware) {
		default:
		case TUNER_S0194:
			fw_lme = fw_s0194;
			ret = request_firmware(&fw, fw_lme, &udev->dev);
			if (ret == 0) {
				st->dvb_usb_lme2510_firmware = TUNER_S0194;
				cold = 0;
				break;
			}
			/* fall through */
		case TUNER_LG:
			fw_lme = fw_lg;
			ret = request_firmware(&fw, fw_lme, &udev->dev);
			if (ret == 0) {
				st->dvb_usb_lme2510_firmware = TUNER_LG;
				break;
			}
			st->dvb_usb_lme2510_firmware = TUNER_DEFAULT;
			break;
		}
		break;
	case 0x1120:
		switch (st->dvb_usb_lme2510_firmware) {
		default:
		case TUNER_S7395:
			fw_lme = fw_c_s7395;
			ret = request_firmware(&fw, fw_lme, &udev->dev);
			if (ret == 0) {
				st->dvb_usb_lme2510_firmware = TUNER_S7395;
				cold = 0;
				break;
			}
			/* fall through */
		case TUNER_LG:
			fw_lme = fw_c_lg;
			ret = request_firmware(&fw, fw_lme, &udev->dev);
			if (ret == 0) {
				st->dvb_usb_lme2510_firmware = TUNER_LG;
				break;
			}
			/* fall through */
		case TUNER_S0194:
			fw_lme = fw_c_s0194;
			ret = request_firmware(&fw, fw_lme, &udev->dev);
			if (ret == 0) {
				st->dvb_usb_lme2510_firmware = TUNER_S0194;
				break;
			}
			st->dvb_usb_lme2510_firmware = TUNER_DEFAULT;
			cold = 0;
			break;
		}
		break;
	case 0x22f0:
		fw_lme = fw_c_rs2000;
		st->dvb_usb_lme2510_firmware = TUNER_RS2000;
		break;
	default:
		fw_lme = fw_c_s7395;
	}

	release_firmware(fw);

	if (cold) {
		dvb_usb_lme2510_firmware = st->dvb_usb_lme2510_firmware;
		info("FRM Changing to %s firmware", fw_lme);
		lme_coldreset(d);
		return NULL;
	}

	return fw_lme;
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
	.demod_address = 0x0e,
	.invert = 0,
	.diseqc_tone = 1,
	.xtal_freq = TDA10086_XTAL_16M,
};

static struct stv0288_config lme_config = {
	.demod_address = 0x68,
	.min_delay_ms = 15,
	.inittab = s7395_inittab,
};

static struct ix2505v_config lme_tuner = {
	.tuner_address = 0x60,
	.min_delay_ms = 100,
	.tuner_gain = 0x0,
	.tuner_chargepump = 0x3,
};

static struct stv0299_config sharp_z0194_config = {
	.demod_address = 0x68,
	.inittab = sharp_z0194a_inittab,
	.mclk = 88000000UL,
	.invert = 0,
	.skip_reinit = 0,
	.lock_output = STV0299_LOCKOUTPUT_1,
	.volt13_op0_op1 = STV0299_VOLT13_OP1,
	.min_delay_ms = 100,
	.set_symbol_rate = sharp_z0194a_set_symbol_rate,
};

static struct m88rs2000_config m88rs2000_config = {
	.demod_addr = 0x68
};

static struct ts2020_config ts2020_config = {
	.tuner_address = 0x60,
	.clk_out_div = 7,
	.dont_poll = true
};

static int dm04_lme2510_set_voltage(struct dvb_frontend *fe,
				    enum fe_sec_voltage voltage)
{
	struct dvb_usb_device *d = fe_to_d(fe);
	struct lme2510_state *st = fe_to_priv(fe);
	static u8 voltage_low[] = LME_VOLTAGE_L;
	static u8 voltage_high[] = LME_VOLTAGE_H;
	static u8 rbuf[1];
	int ret = 0, len = 3, rlen = 1;

	mutex_lock(&d->i2c_mutex);

	switch (voltage) {
	case SEC_VOLTAGE_18:
		ret |= lme2510_usb_talk(d,
			voltage_high, len, rbuf, rlen);
		break;

	case SEC_VOLTAGE_OFF:
	case SEC_VOLTAGE_13:
	default:
		ret |= lme2510_usb_talk(d,
				voltage_low, len, rbuf, rlen);
		break;
	}

	mutex_unlock(&d->i2c_mutex);

	if (st->tuner_config == TUNER_RS2000)
		if (st->fe_set_voltage)
			st->fe_set_voltage(fe, voltage);


	return (ret < 0) ? -ENODEV : 0;
}

static int dm04_read_status(struct dvb_frontend *fe, enum fe_status *status)
{
	struct dvb_usb_device *d = fe_to_d(fe);
	struct lme2510_state *st = d->priv;
	int ret = 0;

	if (st->i2c_talk_onoff) {
		if (st->fe_read_status) {
			ret = st->fe_read_status(fe, status);
			if (ret < 0)
				return ret;
		}

		st->lock_status = *status;

		if (*status & FE_HAS_LOCK && st->stream_on) {
			mutex_lock(&d->i2c_mutex);

			st->i2c_talk_onoff = 0;
			ret = lme2510_stream_restart(d);

			mutex_unlock(&d->i2c_mutex);
		}

		return ret;
	}

	/* Timeout of interrupt reached on RS2000 */
	if (st->tuner_config == TUNER_RS2000 &&
	    time_after(jiffies, st->int_urb_due))
		st->lock_status &= ~FE_HAS_LOCK;

	*status = st->lock_status;

	if (!(*status & FE_HAS_LOCK)) {
		struct dvb_usb_adapter *adap = fe_to_adap(fe);

		st->i2c_talk_onoff = 1;

		lme2510_update_stats(adap);
	}

	return ret;
}

static int dm04_read_signal_strength(struct dvb_frontend *fe, u16 *strength)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct lme2510_state *st = fe_to_priv(fe);

	if (st->fe_read_signal_strength && !st->stream_on)
		return st->fe_read_signal_strength(fe, strength);

	if (c->strength.stat[0].scale == FE_SCALE_RELATIVE)
		*strength = (u16)c->strength.stat[0].uvalue;
	else
		*strength = 0;

	return 0;
}

static int dm04_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct lme2510_state *st = fe_to_priv(fe);

	if (st->fe_read_snr && !st->stream_on)
		return st->fe_read_snr(fe, snr);

	if (c->cnr.stat[0].scale == FE_SCALE_RELATIVE)
		*snr = (u16)c->cnr.stat[0].uvalue;
	else
		*snr = 0;

	return 0;
}

static int dm04_read_ber(struct dvb_frontend *fe, u32 *ber)
{
	struct lme2510_state *st = fe_to_priv(fe);

	if (st->fe_read_ber && !st->stream_on)
		return st->fe_read_ber(fe, ber);

	*ber = 0;

	return 0;
}

static int dm04_read_ucblocks(struct dvb_frontend *fe, u32 *ucblocks)
{
	struct lme2510_state *st = fe_to_priv(fe);

	if (st->fe_read_ucblocks && !st->stream_on)
		return st->fe_read_ucblocks(fe, ucblocks);

	*ucblocks = 0;

	return 0;
}

static int lme_name(struct dvb_usb_adapter *adap)
{
	struct dvb_usb_device *d = adap_to_d(adap);
	struct lme2510_state *st = adap_to_priv(adap);
	const char *desc = d->name;
	static const char * const fe_name[] = {
		"", " LG TDQY-P001F", " SHARP:BS2F7HZ7395",
		" SHARP:BS2F7HZ0194", " RS2000"};
	char *name = adap->fe[0]->ops.info.name;

	strlcpy(name, desc, 128);
	strlcat(name, fe_name[st->tuner_config], 128);

	return 0;
}

static int dm04_lme2510_frontend_attach(struct dvb_usb_adapter *adap)
{
	struct dvb_usb_device *d = adap_to_d(adap);
	struct lme2510_state *st = d->priv;
	int ret = 0;

	st->i2c_talk_onoff = 1;
	switch (le16_to_cpu(d->udev->descriptor.idProduct)) {
	case 0x1122:
	case 0x1120:
		st->i2c_gate = 4;
		adap->fe[0] = dvb_attach(tda10086_attach,
			&tda10086_config, &d->i2c_adap);
		if (adap->fe[0]) {
			info("TUN Found Frontend TDA10086");
			st->i2c_tuner_gate_w = 4;
			st->i2c_tuner_gate_r = 4;
			st->i2c_tuner_addr = 0x60;
			st->tuner_config = TUNER_LG;
			if (st->dvb_usb_lme2510_firmware != TUNER_LG) {
				st->dvb_usb_lme2510_firmware = TUNER_LG;
				ret = lme_firmware_switch(d, 1) ? 0 : -ENODEV;
			}
			break;
		}

		st->i2c_gate = 4;
		adap->fe[0] = dvb_attach(stv0299_attach,
				&sharp_z0194_config, &d->i2c_adap);
		if (adap->fe[0]) {
			info("FE Found Stv0299");
			st->i2c_tuner_gate_w = 4;
			st->i2c_tuner_gate_r = 5;
			st->i2c_tuner_addr = 0x60;
			st->tuner_config = TUNER_S0194;
			if (st->dvb_usb_lme2510_firmware != TUNER_S0194) {
				st->dvb_usb_lme2510_firmware = TUNER_S0194;
				ret = lme_firmware_switch(d, 1) ? 0 : -ENODEV;
			}
			break;
		}

		st->i2c_gate = 5;
		adap->fe[0] = dvb_attach(stv0288_attach, &lme_config,
			&d->i2c_adap);

		if (adap->fe[0]) {
			info("FE Found Stv0288");
			st->i2c_tuner_gate_w = 4;
			st->i2c_tuner_gate_r = 5;
			st->i2c_tuner_addr = 0x60;
			st->tuner_config = TUNER_S7395;
			if (st->dvb_usb_lme2510_firmware != TUNER_S7395) {
				st->dvb_usb_lme2510_firmware = TUNER_S7395;
				ret = lme_firmware_switch(d, 1) ? 0 : -ENODEV;
			}
			break;
		}
		/* fall through */
	case 0x22f0:
		st->i2c_gate = 5;
		adap->fe[0] = dvb_attach(m88rs2000_attach,
			&m88rs2000_config, &d->i2c_adap);

		if (adap->fe[0]) {
			info("FE Found M88RS2000");
			dvb_attach(ts2020_attach, adap->fe[0], &ts2020_config,
					&d->i2c_adap);
			st->i2c_tuner_gate_w = 5;
			st->i2c_tuner_gate_r = 5;
			st->i2c_tuner_addr = 0x60;
			st->tuner_config = TUNER_RS2000;
			st->fe_set_voltage =
				adap->fe[0]->ops.set_voltage;
		}
		break;
	}

	if (adap->fe[0] == NULL) {
		info("DM04/QQBOX Not Powered up or not Supported");
		return -ENODEV;
	}

	if (ret) {
		if (adap->fe[0]) {
			dvb_frontend_detach(adap->fe[0]);
			adap->fe[0] = NULL;
		}
		d->rc_map = NULL;
		return -ENODEV;
	}

	st->fe_read_status = adap->fe[0]->ops.read_status;
	st->fe_read_signal_strength = adap->fe[0]->ops.read_signal_strength;
	st->fe_read_snr = adap->fe[0]->ops.read_snr;
	st->fe_read_ber = adap->fe[0]->ops.read_ber;
	st->fe_read_ucblocks = adap->fe[0]->ops.read_ucblocks;

	adap->fe[0]->ops.read_status = dm04_read_status;
	adap->fe[0]->ops.read_signal_strength = dm04_read_signal_strength;
	adap->fe[0]->ops.read_snr = dm04_read_snr;
	adap->fe[0]->ops.read_ber = dm04_read_ber;
	adap->fe[0]->ops.read_ucblocks = dm04_read_ucblocks;
	adap->fe[0]->ops.set_voltage = dm04_lme2510_set_voltage;

	ret = lme_name(adap);
	return ret;
}

static int dm04_lme2510_tuner(struct dvb_usb_adapter *adap)
{
	struct dvb_usb_device *d = adap_to_d(adap);
	struct lme2510_state *st = adap_to_priv(adap);
	static const char * const tun_msg[] = {"", "TDA8263", "IX2505V", "DVB_PLL_OPERA", "RS2000"};
	int ret = 0;

	switch (st->tuner_config) {
	case TUNER_LG:
		if (dvb_attach(tda826x_attach, adap->fe[0], 0x60,
			&d->i2c_adap, 1))
			ret = st->tuner_config;
		break;
	case TUNER_S7395:
		if (dvb_attach(ix2505v_attach , adap->fe[0], &lme_tuner,
			&d->i2c_adap))
			ret = st->tuner_config;
		break;
	case TUNER_S0194:
		if (dvb_attach(dvb_pll_attach , adap->fe[0], 0x60,
			&d->i2c_adap, DVB_PLL_OPERA1))
			ret = st->tuner_config;
		break;
	case TUNER_RS2000:
		ret = st->tuner_config;
		break;
	default:
		break;
	}

	if (ret)
		info("TUN Found %s tuner", tun_msg[ret]);
	else {
		info("TUN No tuner found --- resetting device");
		lme_coldreset(d);
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
	int ret = 0, len = 3, rlen = 1;

	mutex_lock(&d->i2c_mutex);

	ret = lme2510_usb_talk(d, onoff ? lnb_on : lnb_off, len, rbuf, rlen);

	st->i2c_talk_onoff = 1;

	mutex_unlock(&d->i2c_mutex);

	return ret;
}

static int lme2510_get_adapter_count(struct dvb_usb_device *d)
{
	return 1;
}

static int lme2510_identify_state(struct dvb_usb_device *d, const char **name)
{
	struct lme2510_state *st = d->priv;

	usb_reset_configuration(d->udev);

	usb_set_interface(d->udev,
		d->props->bInterfaceNumber, 1);

	st->dvb_usb_lme2510_firmware = dvb_usb_lme2510_firmware;

	if (lme2510_return_status(d) == 0x44) {
		*name = lme_firmware_switch(d, 0);
		return COLD;
	}

	return 0;
}

static int lme2510_get_stream_config(struct dvb_frontend *fe, u8 *ts_type,
		struct usb_data_stream_properties *stream)
{
	struct dvb_usb_adapter *adap = fe_to_adap(fe);
	struct dvb_usb_device *d;

	if (adap == NULL)
		return 0;

	d = adap_to_d(adap);

	/* Turn PID filter on the fly by module option */
	if (pid_filter == 2) {
		adap->pid_filtering  = true;
		adap->max_feed_count = 15;
	}

	if (!(le16_to_cpu(d->udev->descriptor.idProduct)
		== 0x1122))
		stream->endpoint = 0x8;

	return 0;
}

static int lme2510_get_rc_config(struct dvb_usb_device *d,
	struct dvb_usb_rc *rc)
{
	rc->allowed_protos = RC_BIT_NEC32;
	return 0;
}

static void *lme2510_exit_int(struct dvb_usb_device *d)
{
	struct lme2510_state *st = d->priv;
	struct dvb_usb_adapter *adap = &d->adapter[0];
	void *buffer = NULL;

	if (adap != NULL) {
		lme2510_kill_urb(&adap->stream);
	}

	if (st->usb_buffer != NULL) {
		st->i2c_talk_onoff = 1;
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

static void lme2510_exit(struct dvb_usb_device *d)
{
	void *usb_buffer;

	if (d != NULL) {
		usb_buffer = lme2510_exit_int(d);
		kfree(usb_buffer);
	}
}

static struct dvb_usb_device_properties lme2510_props = {
	.driver_name = KBUILD_MODNAME,
	.owner = THIS_MODULE,
	.bInterfaceNumber = 0,
	.adapter_nr = adapter_nr,
	.size_of_priv = sizeof(struct lme2510_state),

	.download_firmware = lme2510_download_firmware,

	.power_ctrl       = lme2510_powerup,
	.identify_state   = lme2510_identify_state,
	.i2c_algo         = &lme2510_i2c_algo,

	.frontend_attach  = dm04_lme2510_frontend_attach,
	.tuner_attach = dm04_lme2510_tuner,
	.get_stream_config = lme2510_get_stream_config,
	.get_adapter_count = lme2510_get_adapter_count,
	.streaming_ctrl   = lme2510_streaming_ctrl,

	.get_rc_config = lme2510_get_rc_config,

	.exit = lme2510_exit,
	.adapter = {
		{
			.caps = DVB_USB_ADAP_HAS_PID_FILTER|
				DVB_USB_ADAP_PID_FILTER_CAN_BE_TURNED_OFF,
			.pid_filter_count = 15,
			.pid_filter = lme2510_pid_filter,
			.pid_filter_ctrl  = lme2510_pid_filter_ctrl,
			.stream =
			DVB_USB_STREAM_BULK(0x86, 10, 4096),
		},
		{
		}
	},
};

static const struct usb_device_id lme2510_id_table[] = {
	{	DVB_USB_DEVICE(0x3344, 0x1122, &lme2510_props,
		"DM04_LME2510_DVB-S", RC_MAP_LME2510)	},
	{	DVB_USB_DEVICE(0x3344, 0x1120, &lme2510_props,
		"DM04_LME2510C_DVB-S", RC_MAP_LME2510)	},
	{	DVB_USB_DEVICE(0x3344, 0x22f0, &lme2510_props,
		"DM04_LME2510C_DVB-S RS2000", RC_MAP_LME2510)	},
	{}		/* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, lme2510_id_table);

static struct usb_driver lme2510_driver = {
	.name		= KBUILD_MODNAME,
	.probe		= dvb_usbv2_probe,
	.disconnect	= dvb_usbv2_disconnect,
	.id_table	= lme2510_id_table,
	.no_dynamic_id = 1,
	.soft_unbind = 1,
};

module_usb_driver(lme2510_driver);

MODULE_AUTHOR("Malcolm Priestley <tvboxspy@gmail.com>");
MODULE_DESCRIPTION("LME2510(C) DVB-S USB2.0");
MODULE_VERSION("2.07");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE(LME2510_C_S7395);
MODULE_FIRMWARE(LME2510_C_LG);
MODULE_FIRMWARE(LME2510_C_S0194);
MODULE_FIRMWARE(LME2510_C_RS2000);
MODULE_FIRMWARE(LME2510_LG);
MODULE_FIRMWARE(LME2510_S0194);

