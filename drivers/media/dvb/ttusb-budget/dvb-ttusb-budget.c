/*
 * TTUSB DVB driver
 *
 * Copyright (c) 2002 Holger Waechtler <holger@convergence.de>
 * Copyright (c) 2003 Felix Domke <tmbinc@elitedvb.net>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation; either version 2 of
 *	the License, or (at your option) any later version.
 */
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/usb.h>
#include <linux/delay.h>
#include <linux/time.h>
#include <linux/errno.h>
#include <linux/jiffies.h>
#include <linux/mutex.h>

#include "dvb_frontend.h"
#include "dmxdev.h"
#include "dvb_demux.h"
#include "dvb_net.h"
#include "ves1820.h"
#include "cx22700.h"
#include "tda1004x.h"
#include "stv0299.h"
#include "tda8083.h"
#include "stv0297.h"
#include "lnbp21.h"

#include <linux/dvb/frontend.h>
#include <linux/dvb/dmx.h>
#include <linux/pci.h>

/*
  TTUSB_HWSECTIONS:
    the DSP supports filtering in hardware, however, since the "muxstream"
    is a bit braindead (no matching channel masks or no matching filter mask),
    we won't support this - yet. it doesn't event support negative filters,
    so the best way is maybe to keep TTUSB_HWSECTIONS undef'd and just
    parse TS data. USB bandwith will be a problem when having large
    datastreams, especially for dvb-net, but hey, that's not my problem.

  TTUSB_DISEQC, TTUSB_TONE:
    let the STC do the diseqc/tone stuff. this isn't supported at least with
    my TTUSB, so let it undef'd unless you want to implement another
    frontend. never tested.

  DEBUG:
    define it to > 3 for really hardcore debugging. you probably don't want
    this unless the device doesn't load at all. > 2 for bandwidth statistics.
*/

static int debug;

module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off debugging (default:off).");

#define dprintk(x...) do { if (debug) printk(KERN_DEBUG x); } while (0)

#define ISO_BUF_COUNT      4
#define FRAMES_PER_ISO_BUF 4
#define ISO_FRAME_SIZE     912
#define TTUSB_MAXCHANNEL   32
#ifdef TTUSB_HWSECTIONS
#define TTUSB_MAXFILTER    16	/* ??? */
#endif

#define TTUSB_REV_2_2	0x22
#define TTUSB_BUDGET_NAME "ttusb_stc_fw"

/**
 *  since we're casting (struct ttusb*) <-> (struct dvb_demux*) around
 *  the dvb_demux field must be the first in struct!!
 */
struct ttusb {
	struct dvb_demux dvb_demux;
	struct dmxdev dmxdev;
	struct dvb_net dvbnet;

	/* and one for USB access. */
	struct mutex semi2c;
	struct mutex semusb;

	struct dvb_adapter adapter;
	struct usb_device *dev;

	struct i2c_adapter i2c_adap;

	int disconnecting;
	int iso_streaming;

	unsigned int bulk_out_pipe;
	unsigned int bulk_in_pipe;
	unsigned int isoc_in_pipe;

	void *iso_buffer;
	dma_addr_t iso_dma_handle;

	struct urb *iso_urb[ISO_BUF_COUNT];

	int running_feed_count;
	int last_channel;
	int last_filter;

	u8 c;			/* transaction counter, wraps around...  */
	fe_sec_tone_mode_t tone;
	fe_sec_voltage_t voltage;

	int mux_state;		// 0..2 - MuxSyncWord, 3 - nMuxPacks,    4 - muxpack
	u8 mux_npacks;
	u8 muxpack[256 + 8];
	int muxpack_ptr, muxpack_len;

	int insync;

	int cc;			/* MuxCounter - will increment on EVERY MUX PACKET */
	/* (including stuffing. yes. really.) */

	u8 last_result[32];

	int revision;

#if 0
	devfs_handle_t stc_devfs_handle;
#endif

	struct dvb_frontend* fe;
};

/* ugly workaround ... don't know why it's neccessary to read */
/* all result codes. */

#define DEBUG 0
static int ttusb_cmd(struct ttusb *ttusb,
	      const u8 * data, int len, int needresult)
{
	int actual_len;
	int err;
#if DEBUG >= 3
	int i;

	printk(">");
	for (i = 0; i < len; ++i)
		printk(" %02x", data[i]);
	printk("\n");
#endif

	if (mutex_lock_interruptible(&ttusb->semusb) < 0)
		return -EAGAIN;

	err = usb_bulk_msg(ttusb->dev, ttusb->bulk_out_pipe,
			   (u8 *) data, len, &actual_len, 1000);
	if (err != 0) {
		dprintk("%s: usb_bulk_msg(send) failed, err == %i!\n",
			__FUNCTION__, err);
		mutex_unlock(&ttusb->semusb);
		return err;
	}
	if (actual_len != len) {
		dprintk("%s: only wrote %d of %d bytes\n", __FUNCTION__,
			actual_len, len);
		mutex_unlock(&ttusb->semusb);
		return -1;
	}

	err = usb_bulk_msg(ttusb->dev, ttusb->bulk_in_pipe,
			   ttusb->last_result, 32, &actual_len, 1000);

	if (err != 0) {
		printk("%s: failed, receive error %d\n", __FUNCTION__,
		       err);
		mutex_unlock(&ttusb->semusb);
		return err;
	}
#if DEBUG >= 3
	actual_len = ttusb->last_result[3] + 4;
	printk("<");
	for (i = 0; i < actual_len; ++i)
		printk(" %02x", ttusb->last_result[i]);
	printk("\n");
#endif
	if (!needresult)
		mutex_unlock(&ttusb->semusb);
	return 0;
}

static int ttusb_result(struct ttusb *ttusb, u8 * data, int len)
{
	memcpy(data, ttusb->last_result, len);
	mutex_unlock(&ttusb->semusb);
	return 0;
}

static int ttusb_i2c_msg(struct ttusb *ttusb,
		  u8 addr, u8 * snd_buf, u8 snd_len, u8 * rcv_buf,
		  u8 rcv_len)
{
	u8 b[0x28];
	u8 id = ++ttusb->c;
	int i, err;

	if (snd_len > 0x28 - 7 || rcv_len > 0x20 - 7)
		return -EINVAL;

	b[0] = 0xaa;
	b[1] = id;
	b[2] = 0x31;
	b[3] = snd_len + 3;
	b[4] = addr << 1;
	b[5] = snd_len;
	b[6] = rcv_len;

	for (i = 0; i < snd_len; i++)
		b[7 + i] = snd_buf[i];

	err = ttusb_cmd(ttusb, b, snd_len + 7, 1);

	if (err)
		return -EREMOTEIO;

	err = ttusb_result(ttusb, b, 0x20);

	/* check if the i2c transaction was successful */
	if ((snd_len != b[5]) || (rcv_len != b[6])) return -EREMOTEIO;

	if (rcv_len > 0) {

		if (err || b[0] != 0x55 || b[1] != id) {
			dprintk
			    ("%s: usb_bulk_msg(recv) failed, err == %i, id == %02x, b == ",
			     __FUNCTION__, err, id);
			return -EREMOTEIO;
		}

		for (i = 0; i < rcv_len; i++)
			rcv_buf[i] = b[7 + i];
	}

	return rcv_len;
}

static int master_xfer(struct i2c_adapter* adapter, struct i2c_msg *msg, int num)
{
	struct ttusb *ttusb = i2c_get_adapdata(adapter);
	int i = 0;
	int inc;

	if (mutex_lock_interruptible(&ttusb->semi2c) < 0)
		return -EAGAIN;

	while (i < num) {
		u8 addr, snd_len, rcv_len, *snd_buf, *rcv_buf;
		int err;

		if (num > i + 1 && (msg[i + 1].flags & I2C_M_RD)) {
			addr = msg[i].addr;
			snd_buf = msg[i].buf;
			snd_len = msg[i].len;
			rcv_buf = msg[i + 1].buf;
			rcv_len = msg[i + 1].len;
			inc = 2;
		} else {
			addr = msg[i].addr;
			snd_buf = msg[i].buf;
			snd_len = msg[i].len;
			rcv_buf = NULL;
			rcv_len = 0;
			inc = 1;
		}

		err = ttusb_i2c_msg(ttusb, addr,
				    snd_buf, snd_len, rcv_buf, rcv_len);

		if (err < rcv_len) {
			dprintk("%s: i == %i\n", __FUNCTION__, i);
			break;
		}

		i += inc;
	}

	mutex_unlock(&ttusb->semi2c);
	return i;
}

#include "dvb-ttusb-dspbootcode.h"

static int ttusb_boot_dsp(struct ttusb *ttusb)
{
	int i, err;
	u8 b[40];

	/* BootBlock */
	b[0] = 0xaa;
	b[2] = 0x13;
	b[3] = 28;

	/* upload dsp code in 32 byte steps (36 didn't work for me ...) */
	/* 32 is max packet size, no messages should be splitted. */
	for (i = 0; i < sizeof(dsp_bootcode); i += 28) {
		memcpy(&b[4], &dsp_bootcode[i], 28);

		b[1] = ++ttusb->c;

		err = ttusb_cmd(ttusb, b, 32, 0);
		if (err)
			goto done;
	}

	/* last block ... */
	b[1] = ++ttusb->c;
	b[2] = 0x13;
	b[3] = 0;

	err = ttusb_cmd(ttusb, b, 4, 0);
	if (err)
		goto done;

	/* BootEnd */
	b[1] = ++ttusb->c;
	b[2] = 0x14;
	b[3] = 0;

	err = ttusb_cmd(ttusb, b, 4, 0);

      done:
	if (err) {
		dprintk("%s: usb_bulk_msg() failed, return value %i!\n",
			__FUNCTION__, err);
	}

	return err;
}

static int ttusb_set_channel(struct ttusb *ttusb, int chan_id, int filter_type,
		      int pid)
{
	int err;
	/* SetChannel */
	u8 b[] = { 0xaa, ++ttusb->c, 0x22, 4, chan_id, filter_type,
		(pid >> 8) & 0xff, pid & 0xff
	};

	err = ttusb_cmd(ttusb, b, sizeof(b), 0);
	return err;
}

static int ttusb_del_channel(struct ttusb *ttusb, int channel_id)
{
	int err;
	/* DelChannel */
	u8 b[] = { 0xaa, ++ttusb->c, 0x23, 1, channel_id };

	err = ttusb_cmd(ttusb, b, sizeof(b), 0);
	return err;
}

#ifdef TTUSB_HWSECTIONS
static int ttusb_set_filter(struct ttusb *ttusb, int filter_id,
		     int associated_chan, u8 filter[8], u8 mask[8])
{
	int err;
	/* SetFilter */
	u8 b[] = { 0xaa, 0, 0x24, 0x1a, filter_id, associated_chan,
		filter[0], filter[1], filter[2], filter[3],
		filter[4], filter[5], filter[6], filter[7],
		filter[8], filter[9], filter[10], filter[11],
		mask[0], mask[1], mask[2], mask[3],
		mask[4], mask[5], mask[6], mask[7],
		mask[8], mask[9], mask[10], mask[11]
	};

	err = ttusb_cmd(ttusb, b, sizeof(b), 0);
	return err;
}

static int ttusb_del_filter(struct ttusb *ttusb, int filter_id)
{
	int err;
	/* DelFilter */
	u8 b[] = { 0xaa, ++ttusb->c, 0x25, 1, filter_id };

	err = ttusb_cmd(ttusb, b, sizeof(b), 0);
	return err;
}
#endif

static int ttusb_init_controller(struct ttusb *ttusb)
{
	u8 b0[] = { 0xaa, ++ttusb->c, 0x15, 1, 0 };
	u8 b1[] = { 0xaa, ++ttusb->c, 0x15, 1, 1 };
	u8 b2[] = { 0xaa, ++ttusb->c, 0x32, 1, 0 };
	/* i2c write read: 5 bytes, addr 0x10, 0x02 bytes write, 1 bytes read. */
	u8 b3[] =
	    { 0xaa, ++ttusb->c, 0x31, 5, 0x10, 0x02, 0x01, 0x00, 0x1e };
	u8 b4[] =
	    { 0x55, ttusb->c, 0x31, 4, 0x10, 0x02, 0x01, 0x00, 0x1e };

	u8 get_version[] = { 0xaa, ++ttusb->c, 0x17, 5, 0, 0, 0, 0, 0 };
	u8 get_dsp_version[0x20] =
	    { 0xaa, ++ttusb->c, 0x26, 28, 0, 0, 0, 0, 0 };
	int err;

	/* reset board */
	if ((err = ttusb_cmd(ttusb, b0, sizeof(b0), 0)))
		return err;

	/* reset board (again?) */
	if ((err = ttusb_cmd(ttusb, b1, sizeof(b1), 0)))
		return err;

	ttusb_boot_dsp(ttusb);

	/* set i2c bit rate */
	if ((err = ttusb_cmd(ttusb, b2, sizeof(b2), 0)))
		return err;

	if ((err = ttusb_cmd(ttusb, b3, sizeof(b3), 1)))
		return err;

	err = ttusb_result(ttusb, b4, sizeof(b4));

	if ((err = ttusb_cmd(ttusb, get_version, sizeof(get_version), 1)))
		return err;

	if ((err = ttusb_result(ttusb, get_version, sizeof(get_version))))
		return err;

	dprintk("%s: stc-version: %c%c%c%c%c\n", __FUNCTION__,
		get_version[4], get_version[5], get_version[6],
		get_version[7], get_version[8]);

	if (memcmp(get_version + 4, "V 0.0", 5) &&
	    memcmp(get_version + 4, "V 1.1", 5) &&
	    memcmp(get_version + 4, "V 2.1", 5) &&
	    memcmp(get_version + 4, "V 2.2", 5)) {
		printk
		    ("%s: unknown STC version %c%c%c%c%c, please report!\n",
		     __FUNCTION__, get_version[4], get_version[5],
		     get_version[6], get_version[7], get_version[8]);
	}

	ttusb->revision = ((get_version[6] - '0') << 4) |
			   (get_version[8] - '0');

	err =
	    ttusb_cmd(ttusb, get_dsp_version, sizeof(get_dsp_version), 1);
	if (err)
		return err;

	err =
	    ttusb_result(ttusb, get_dsp_version, sizeof(get_dsp_version));
	if (err)
		return err;
	printk("%s: dsp-version: %c%c%c\n", __FUNCTION__,
	       get_dsp_version[4], get_dsp_version[5], get_dsp_version[6]);
	return 0;
}

#ifdef TTUSB_DISEQC
static int ttusb_send_diseqc(struct dvb_frontend* fe,
			     const struct dvb_diseqc_master_cmd *cmd)
{
	struct ttusb* ttusb = (struct ttusb*) fe->dvb->priv;
	u8 b[12] = { 0xaa, ++ttusb->c, 0x18 };

	int err;

	b[3] = 4 + 2 + cmd->msg_len;
	b[4] = 0xFF;		/* send diseqc master, not burst */
	b[5] = cmd->msg_len;

	memcpy(b + 5, cmd->msg, cmd->msg_len);

	/* Diseqc */
	if ((err = ttusb_cmd(ttusb, b, 4 + b[3], 0))) {
		dprintk("%s: usb_bulk_msg() failed, return value %i!\n",
			__FUNCTION__, err);
	}

	return err;
}
#endif

static int ttusb_update_lnb(struct ttusb *ttusb)
{
	u8 b[] = { 0xaa, ++ttusb->c, 0x16, 5, /*power: */ 1,
		ttusb->voltage == SEC_VOLTAGE_18 ? 0 : 1,
		ttusb->tone == SEC_TONE_ON ? 1 : 0, 1, 1
	};
	int err;

	/* SetLNB */
	if ((err = ttusb_cmd(ttusb, b, sizeof(b), 0))) {
		dprintk("%s: usb_bulk_msg() failed, return value %i!\n",
			__FUNCTION__, err);
	}

	return err;
}

static int ttusb_set_voltage(struct dvb_frontend* fe, fe_sec_voltage_t voltage)
{
	struct ttusb* ttusb = (struct ttusb*) fe->dvb->priv;

	ttusb->voltage = voltage;
	return ttusb_update_lnb(ttusb);
}

#ifdef TTUSB_TONE
static int ttusb_set_tone(struct dvb_frontend* fe, fe_sec_tone_mode_t tone)
{
	struct ttusb* ttusb = (struct ttusb*) fe->dvb->priv;

	ttusb->tone = tone;
	return ttusb_update_lnb(ttusb);
}
#endif


#if 0
static void ttusb_set_led_freq(struct ttusb *ttusb, u8 freq)
{
	u8 b[] = { 0xaa, ++ttusb->c, 0x19, 1, freq };
	int err, actual_len;

	err = ttusb_cmd(ttusb, b, sizeof(b), 0);
	if (err) {
		dprintk("%s: usb_bulk_msg() failed, return value %i!\n",
			__FUNCTION__, err);
	}
}
#endif

/*****************************************************************************/

#ifdef TTUSB_HWSECTIONS
static void ttusb_handle_ts_data(struct ttusb_channel *channel,
				 const u8 * data, int len);
static void ttusb_handle_sec_data(struct ttusb_channel *channel,
				  const u8 * data, int len);
#endif

static int numpkt = 0, numts, numstuff, numsec, numinvalid;
static unsigned long lastj;

static void ttusb_process_muxpack(struct ttusb *ttusb, const u8 * muxpack,
			   int len)
{
	u16 csum = 0, cc;
	int i;
	for (i = 0; i < len; i += 2)
		csum ^= le16_to_cpup((u16 *) (muxpack + i));
	if (csum) {
		printk("%s: muxpack with incorrect checksum, ignoring\n",
		       __FUNCTION__);
		numinvalid++;
		return;
	}

	cc = (muxpack[len - 4] << 8) | muxpack[len - 3];
	cc &= 0x7FFF;
	if ((cc != ttusb->cc) && (ttusb->cc != -1))
		printk("%s: cc discontinuity (%d frames missing)\n",
		       __FUNCTION__, (cc - ttusb->cc) & 0x7FFF);
	ttusb->cc = (cc + 1) & 0x7FFF;
	if (muxpack[0] & 0x80) {
#ifdef TTUSB_HWSECTIONS
		/* section data */
		int pusi = muxpack[0] & 0x40;
		int channel = muxpack[0] & 0x1F;
		int payload = muxpack[1];
		const u8 *data = muxpack + 2;
		/* check offset flag */
		if (muxpack[0] & 0x20)
			data++;

		ttusb_handle_sec_data(ttusb->channel + channel, data,
				      payload);
		data += payload;

		if ((!!(ttusb->muxpack[0] & 0x20)) ^
		    !!(ttusb->muxpack[1] & 1))
			data++;
#warning TODO: pusi
		printk("cc: %04x\n", (data[0] << 8) | data[1]);
#endif
		numsec++;
	} else if (muxpack[0] == 0x47) {
#ifdef TTUSB_HWSECTIONS
		/* we have TS data here! */
		int pid = ((muxpack[1] & 0x0F) << 8) | muxpack[2];
		int channel;
		for (channel = 0; channel < TTUSB_MAXCHANNEL; ++channel)
			if (ttusb->channel[channel].active
			    && (pid == ttusb->channel[channel].pid))
				ttusb_handle_ts_data(ttusb->channel +
						     channel, muxpack,
						     188);
#endif
		numts++;
		dvb_dmx_swfilter_packets(&ttusb->dvb_demux, muxpack, 1);
	} else if (muxpack[0] != 0) {
		numinvalid++;
		printk("illegal muxpack type %02x\n", muxpack[0]);
	} else
		numstuff++;
}

static void ttusb_process_frame(struct ttusb *ttusb, u8 * data, int len)
{
	int maxwork = 1024;
	while (len) {
		if (!(maxwork--)) {
			printk("%s: too much work\n", __FUNCTION__);
			break;
		}

		switch (ttusb->mux_state) {
		case 0:
		case 1:
		case 2:
			len--;
			if (*data++ == 0xAA)
				++ttusb->mux_state;
			else {
				ttusb->mux_state = 0;
#if DEBUG > 3
				if (ttusb->insync)
					printk("%02x ", data[-1]);
#else
				if (ttusb->insync) {
					printk("%s: lost sync.\n",
					       __FUNCTION__);
					ttusb->insync = 0;
				}
#endif
			}
			break;
		case 3:
			ttusb->insync = 1;
			len--;
			ttusb->mux_npacks = *data++;
			++ttusb->mux_state;
			ttusb->muxpack_ptr = 0;
			/* maximum bytes, until we know the length */
			ttusb->muxpack_len = 2;
			break;
		case 4:
			{
				int avail;
				avail = len;
				if (avail >
				    (ttusb->muxpack_len -
				     ttusb->muxpack_ptr))
					avail =
					    ttusb->muxpack_len -
					    ttusb->muxpack_ptr;
				memcpy(ttusb->muxpack + ttusb->muxpack_ptr,
				       data, avail);
				ttusb->muxpack_ptr += avail;
				BUG_ON(ttusb->muxpack_ptr > 264);
				data += avail;
				len -= avail;
				/* determine length */
				if (ttusb->muxpack_ptr == 2) {
					if (ttusb->muxpack[0] & 0x80) {
						ttusb->muxpack_len =
						    ttusb->muxpack[1] + 2;
						if (ttusb->
						    muxpack[0] & 0x20)
							ttusb->
							    muxpack_len++;
						if ((!!
						     (ttusb->
						      muxpack[0] & 0x20)) ^
						    !!(ttusb->
						       muxpack[1] & 1))
							ttusb->
							    muxpack_len++;
						ttusb->muxpack_len += 4;
					} else if (ttusb->muxpack[0] ==
						   0x47)
						ttusb->muxpack_len =
						    188 + 4;
					else if (ttusb->muxpack[0] == 0x00)
						ttusb->muxpack_len =
						    ttusb->muxpack[1] + 2 +
						    4;
					else {
						dprintk
						    ("%s: invalid state: first byte is %x\n",
						     __FUNCTION__,
						     ttusb->muxpack[0]);
						ttusb->mux_state = 0;
					}
				}

			/**
			 * if length is valid and we reached the end:
			 * goto next muxpack
			 */
				if ((ttusb->muxpack_ptr >= 2) &&
				    (ttusb->muxpack_ptr ==
				     ttusb->muxpack_len)) {
					ttusb_process_muxpack(ttusb,
							      ttusb->
							      muxpack,
							      ttusb->
							      muxpack_ptr);
					ttusb->muxpack_ptr = 0;
					/* maximum bytes, until we know the length */
					ttusb->muxpack_len = 2;

				/**
				 * no muxpacks left?
				 * return to search-sync state
				 */
					if (!ttusb->mux_npacks--) {
						ttusb->mux_state = 0;
						break;
					}
				}
				break;
			}
		default:
			BUG();
			break;
		}
	}
}

static void ttusb_iso_irq(struct urb *urb, struct pt_regs *ptregs)
{
	struct ttusb *ttusb = urb->context;

	if (!ttusb->iso_streaming)
		return;

#if 0
	printk("%s: status %d, errcount == %d, length == %i\n",
	       __FUNCTION__,
	       urb->status, urb->error_count, urb->actual_length);
#endif

	if (!urb->status) {
		int i;
		for (i = 0; i < urb->number_of_packets; ++i) {
			struct usb_iso_packet_descriptor *d;
			u8 *data;
			int len;
			numpkt++;
			if (time_after_eq(jiffies, lastj + HZ)) {
#if DEBUG > 2
				printk
				    ("frames/s: %d (ts: %d, stuff %d, sec: %d, invalid: %d, all: %d)\n",
				     numpkt * HZ / (jiffies - lastj),
				     numts, numstuff, numsec, numinvalid,
				     numts + numstuff + numsec +
				     numinvalid);
#endif
				numts = numstuff = numsec = numinvalid = 0;
				lastj = jiffies;
				numpkt = 0;
			}
			d = &urb->iso_frame_desc[i];
			data = urb->transfer_buffer + d->offset;
			len = d->actual_length;
			d->actual_length = 0;
			d->status = 0;
			ttusb_process_frame(ttusb, data, len);
		}
	}
	usb_submit_urb(urb, GFP_ATOMIC);
}

static void ttusb_free_iso_urbs(struct ttusb *ttusb)
{
	int i;

	for (i = 0; i < ISO_BUF_COUNT; i++)
		if (ttusb->iso_urb[i])
			usb_free_urb(ttusb->iso_urb[i]);

	pci_free_consistent(NULL,
			    ISO_FRAME_SIZE * FRAMES_PER_ISO_BUF *
			    ISO_BUF_COUNT, ttusb->iso_buffer,
			    ttusb->iso_dma_handle);
}

static int ttusb_alloc_iso_urbs(struct ttusb *ttusb)
{
	int i;

	ttusb->iso_buffer = pci_alloc_consistent(NULL,
						 ISO_FRAME_SIZE *
						 FRAMES_PER_ISO_BUF *
						 ISO_BUF_COUNT,
						 &ttusb->iso_dma_handle);

	memset(ttusb->iso_buffer, 0,
	       ISO_FRAME_SIZE * FRAMES_PER_ISO_BUF * ISO_BUF_COUNT);

	for (i = 0; i < ISO_BUF_COUNT; i++) {
		struct urb *urb;

		if (!
		    (urb =
		     usb_alloc_urb(FRAMES_PER_ISO_BUF, GFP_ATOMIC))) {
			ttusb_free_iso_urbs(ttusb);
			return -ENOMEM;
		}

		ttusb->iso_urb[i] = urb;
	}

	return 0;
}

static void ttusb_stop_iso_xfer(struct ttusb *ttusb)
{
	int i;

	for (i = 0; i < ISO_BUF_COUNT; i++)
		usb_kill_urb(ttusb->iso_urb[i]);

	ttusb->iso_streaming = 0;
}

static int ttusb_start_iso_xfer(struct ttusb *ttusb)
{
	int i, j, err, buffer_offset = 0;

	if (ttusb->iso_streaming) {
		printk("%s: iso xfer already running!\n", __FUNCTION__);
		return 0;
	}

	ttusb->cc = -1;
	ttusb->insync = 0;
	ttusb->mux_state = 0;

	for (i = 0; i < ISO_BUF_COUNT; i++) {
		int frame_offset = 0;
		struct urb *urb = ttusb->iso_urb[i];

		urb->dev = ttusb->dev;
		urb->context = ttusb;
		urb->complete = ttusb_iso_irq;
		urb->pipe = ttusb->isoc_in_pipe;
		urb->transfer_flags = URB_ISO_ASAP;
		urb->interval = 1;
		urb->number_of_packets = FRAMES_PER_ISO_BUF;
		urb->transfer_buffer_length =
		    ISO_FRAME_SIZE * FRAMES_PER_ISO_BUF;
		urb->transfer_buffer = ttusb->iso_buffer + buffer_offset;
		buffer_offset += ISO_FRAME_SIZE * FRAMES_PER_ISO_BUF;

		for (j = 0; j < FRAMES_PER_ISO_BUF; j++) {
			urb->iso_frame_desc[j].offset = frame_offset;
			urb->iso_frame_desc[j].length = ISO_FRAME_SIZE;
			frame_offset += ISO_FRAME_SIZE;
		}
	}

	for (i = 0; i < ISO_BUF_COUNT; i++) {
		if ((err = usb_submit_urb(ttusb->iso_urb[i], GFP_ATOMIC))) {
			ttusb_stop_iso_xfer(ttusb);
			printk
			    ("%s: failed urb submission (%i: err = %i)!\n",
			     __FUNCTION__, i, err);
			return err;
		}
	}

	ttusb->iso_streaming = 1;

	return 0;
}

#ifdef TTUSB_HWSECTIONS
static void ttusb_handle_ts_data(struct dvb_demux_feed *dvbdmxfeed, const u8 * data,
			  int len)
{
	dvbdmxfeed->cb.ts(data, len, 0, 0, &dvbdmxfeed->feed.ts, 0);
}

static void ttusb_handle_sec_data(struct dvb_demux_feed *dvbdmxfeed, const u8 * data,
			   int len)
{
//      struct dvb_demux_feed *dvbdmxfeed = channel->dvbdmxfeed;
#error TODO: handle ugly stuff
//      dvbdmxfeed->cb.sec(data, len, 0, 0, &dvbdmxfeed->feed.sec, 0);
}
#endif

static int ttusb_start_feed(struct dvb_demux_feed *dvbdmxfeed)
{
	struct ttusb *ttusb = (struct ttusb *) dvbdmxfeed->demux;
	int feed_type = 1;

	dprintk("ttusb_start_feed\n");

	switch (dvbdmxfeed->type) {
	case DMX_TYPE_TS:
		break;
	case DMX_TYPE_SEC:
		break;
	default:
		return -EINVAL;
	}

	if (dvbdmxfeed->type == DMX_TYPE_TS) {
		switch (dvbdmxfeed->pes_type) {
		case DMX_TS_PES_VIDEO:
		case DMX_TS_PES_AUDIO:
		case DMX_TS_PES_TELETEXT:
		case DMX_TS_PES_PCR:
		case DMX_TS_PES_OTHER:
			break;
		default:
			return -EINVAL;
		}
	}

#ifdef TTUSB_HWSECTIONS
#error TODO: allocate filters
	if (dvbdmxfeed->type == DMX_TYPE_TS) {
		feed_type = 1;
	} else if (dvbdmxfeed->type == DMX_TYPE_SEC) {
		feed_type = 2;
	}
#endif

	ttusb_set_channel(ttusb, dvbdmxfeed->index, feed_type, dvbdmxfeed->pid);

	if (0 == ttusb->running_feed_count++)
		ttusb_start_iso_xfer(ttusb);

	return 0;
}

static int ttusb_stop_feed(struct dvb_demux_feed *dvbdmxfeed)
{
	struct ttusb *ttusb = (struct ttusb *) dvbdmxfeed->demux;

	ttusb_del_channel(ttusb, dvbdmxfeed->index);

	if (--ttusb->running_feed_count == 0)
		ttusb_stop_iso_xfer(ttusb);

	return 0;
}

static int ttusb_setup_interfaces(struct ttusb *ttusb)
{
	usb_set_interface(ttusb->dev, 1, 1);

	ttusb->bulk_out_pipe = usb_sndbulkpipe(ttusb->dev, 1);
	ttusb->bulk_in_pipe = usb_rcvbulkpipe(ttusb->dev, 1);
	ttusb->isoc_in_pipe = usb_rcvisocpipe(ttusb->dev, 2);

	return 0;
}

#if 0
static u8 stc_firmware[8192];

static int stc_open(struct inode *inode, struct file *file)
{
	struct ttusb *ttusb = file->private_data;
	int addr;

	for (addr = 0; addr < 8192; addr += 16) {
		u8 snd_buf[2] = { addr >> 8, addr & 0xFF };
		ttusb_i2c_msg(ttusb, 0x50, snd_buf, 2, stc_firmware + addr,
			      16);
	}

	return 0;
}

static ssize_t stc_read(struct file *file, char *buf, size_t count,
		 loff_t * offset)
{
	int tc = count;

	if ((tc + *offset) > 8192)
		tc = 8192 - *offset;

	if (tc < 0)
		return 0;

	if (copy_to_user(buf, stc_firmware + *offset, tc))
		return -EFAULT;

	*offset += tc;

	return tc;
}

static int stc_release(struct inode *inode, struct file *file)
{
	return 0;
}

static struct file_operations stc_fops = {
	.owner = THIS_MODULE,
	.read = stc_read,
	.open = stc_open,
	.release = stc_release,
};
#endif

static u32 functionality(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C;
}



static int alps_tdmb7_tuner_set_params(struct dvb_frontend* fe, struct dvb_frontend_parameters* params)
{
	struct ttusb* ttusb = (struct ttusb*) fe->dvb->priv;
	u8 data[4];
	struct i2c_msg msg = {.addr=0x61, .flags=0, .buf=data, .len=sizeof(data) };
	u32 div;

	div = (params->frequency + 36166667) / 166667;

	data[0] = (div >> 8) & 0x7f;
	data[1] = div & 0xff;
	data[2] = ((div >> 10) & 0x60) | 0x85;
	data[3] = params->frequency < 592000000 ? 0x40 : 0x80;

	if (fe->ops->i2c_gate_ctrl)
		fe->ops->i2c_gate_ctrl(fe, 1);
	if (i2c_transfer(&ttusb->i2c_adap, &msg, 1) != 1) return -EIO;
	return 0;
}

static struct cx22700_config alps_tdmb7_config = {
	.demod_address = 0x43,
};





static int philips_tdm1316l_tuner_init(struct dvb_frontend* fe)
{
	struct ttusb* ttusb = (struct ttusb*) fe->dvb->priv;
	static u8 td1316_init[] = { 0x0b, 0xf5, 0x85, 0xab };
	static u8 disable_mc44BC374c[] = { 0x1d, 0x74, 0xa0, 0x68 };
	struct i2c_msg tuner_msg = { .addr=0x60, .flags=0, .buf=td1316_init, .len=sizeof(td1316_init) };

	// setup PLL configuration
	if (fe->ops->i2c_gate_ctrl)
		fe->ops->i2c_gate_ctrl(fe, 1);
	if (i2c_transfer(&ttusb->i2c_adap, &tuner_msg, 1) != 1) return -EIO;
	msleep(1);

	// disable the mc44BC374c (do not check for errors)
	tuner_msg.addr = 0x65;
	tuner_msg.buf = disable_mc44BC374c;
	tuner_msg.len = sizeof(disable_mc44BC374c);
	if (fe->ops->i2c_gate_ctrl)
		fe->ops->i2c_gate_ctrl(fe, 1);
	if (i2c_transfer(&ttusb->i2c_adap, &tuner_msg, 1) != 1) {
		i2c_transfer(&ttusb->i2c_adap, &tuner_msg, 1);
	}

	return 0;
}

static int philips_tdm1316l_tuner_set_params(struct dvb_frontend* fe, struct dvb_frontend_parameters* params)
{
	struct ttusb* ttusb = (struct ttusb*) fe->dvb->priv;
	u8 tuner_buf[4];
	struct i2c_msg tuner_msg = {.addr=0x60, .flags=0, .buf=tuner_buf, .len=sizeof(tuner_buf) };
	int tuner_frequency = 0;
	u8 band, cp, filter;

	// determine charge pump
	tuner_frequency = params->frequency + 36130000;
	if (tuner_frequency < 87000000) return -EINVAL;
	else if (tuner_frequency < 130000000) cp = 3;
	else if (tuner_frequency < 160000000) cp = 5;
	else if (tuner_frequency < 200000000) cp = 6;
	else if (tuner_frequency < 290000000) cp = 3;
	else if (tuner_frequency < 420000000) cp = 5;
	else if (tuner_frequency < 480000000) cp = 6;
	else if (tuner_frequency < 620000000) cp = 3;
	else if (tuner_frequency < 830000000) cp = 5;
	else if (tuner_frequency < 895000000) cp = 7;
	else return -EINVAL;

	// determine band
	if (params->frequency < 49000000) return -EINVAL;
	else if (params->frequency < 159000000) band = 1;
	else if (params->frequency < 444000000) band = 2;
	else if (params->frequency < 861000000) band = 4;
	else return -EINVAL;

	// setup PLL filter
	switch (params->u.ofdm.bandwidth) {
	case BANDWIDTH_6_MHZ:
		tda1004x_write_byte(fe, 0x0C, 0);
		filter = 0;
		break;

	case BANDWIDTH_7_MHZ:
		tda1004x_write_byte(fe, 0x0C, 0);
		filter = 0;
		break;

	case BANDWIDTH_8_MHZ:
		tda1004x_write_byte(fe, 0x0C, 0xFF);
		filter = 1;
		break;

	default:
		return -EINVAL;
	}

	// calculate divisor
	// ((36130000+((1000000/6)/2)) + Finput)/(1000000/6)
	tuner_frequency = (((params->frequency / 1000) * 6) + 217280) / 1000;

	// setup tuner buffer
	tuner_buf[0] = tuner_frequency >> 8;
	tuner_buf[1] = tuner_frequency & 0xff;
	tuner_buf[2] = 0xca;
	tuner_buf[3] = (cp << 5) | (filter << 3) | band;

	if (fe->ops->i2c_gate_ctrl)
		fe->ops->i2c_gate_ctrl(fe, 1);
	if (i2c_transfer(&ttusb->i2c_adap, &tuner_msg, 1) != 1)
		return -EIO;

	msleep(1);
	return 0;
}

static int philips_tdm1316l_request_firmware(struct dvb_frontend* fe, const struct firmware **fw, char* name)
{
	struct ttusb* ttusb = (struct ttusb*) fe->dvb->priv;

	return request_firmware(fw, name, &ttusb->dev->dev);
}

static struct tda1004x_config philips_tdm1316l_config = {

	.demod_address = 0x8,
	.invert = 1,
	.invert_oclk = 0,
	.request_firmware = philips_tdm1316l_request_firmware,
};

static u8 alps_bsbe1_inittab[] = {
	0x01, 0x15,
	0x02, 0x30,
	0x03, 0x00,
	0x04, 0x7d,             /* F22FR = 0x7d, F22 = f_VCO / 128 / 0x7d = 22 kHz */
	0x05, 0x35,             /* I2CT = 0, SCLT = 1, SDAT = 1 */
	0x06, 0x40,             /* DAC not used, set to high impendance mode */
	0x07, 0x00,             /* DAC LSB */
	0x08, 0x40,             /* DiSEqC off, LNB power on OP2/LOCK pin on */
	0x09, 0x00,             /* FIFO */
	0x0c, 0x51,             /* OP1 ctl = Normal, OP1 val = 1 (LNB Power ON) */
	0x0d, 0x82,             /* DC offset compensation = ON, beta_agc1 = 2 */
	0x0e, 0x23,             /* alpha_tmg = 2, beta_tmg = 3 */
	0x10, 0x3f,             // AGC2  0x3d
	0x11, 0x84,
	0x12, 0xb9,
	0x15, 0xc9,             // lock detector threshold
	0x16, 0x00,
	0x17, 0x00,
	0x18, 0x00,
	0x19, 0x00,
	0x1a, 0x00,
	0x1f, 0x50,
	0x20, 0x00,
	0x21, 0x00,
	0x22, 0x00,
	0x23, 0x00,
	0x28, 0x00,             // out imp: normal  out type: parallel FEC mode:0
	0x29, 0x1e,             // 1/2 threshold
	0x2a, 0x14,             // 2/3 threshold
	0x2b, 0x0f,             // 3/4 threshold
	0x2c, 0x09,             // 5/6 threshold
	0x2d, 0x05,             // 7/8 threshold
	0x2e, 0x01,
	0x31, 0x1f,             // test all FECs
	0x32, 0x19,             // viterbi and synchro search
	0x33, 0xfc,             // rs control
	0x34, 0x93,             // error control
	0x0f, 0x92,
	0xff, 0xff
};

static u8 alps_bsru6_inittab[] = {
	0x01, 0x15,
	0x02, 0x30,
	0x03, 0x00,
	0x04, 0x7d,		/* F22FR = 0x7d, F22 = f_VCO / 128 / 0x7d = 22 kHz */
	0x05, 0x35,		/* I2CT = 0, SCLT = 1, SDAT = 1 */
	0x06, 0x40,		/* DAC not used, set to high impendance mode */
	0x07, 0x00,		/* DAC LSB */
	0x08, 0x40,		/* DiSEqC off, LNB power on OP2/LOCK pin on */
	0x09, 0x00,		/* FIFO */
	0x0c, 0x51,		/* OP1 ctl = Normal, OP1 val = 1 (LNB Power ON) */
	0x0d, 0x82,		/* DC offset compensation = ON, beta_agc1 = 2 */
	0x0e, 0x23,		/* alpha_tmg = 2, beta_tmg = 3 */
	0x10, 0x3f,		// AGC2  0x3d
	0x11, 0x84,
	0x12, 0xb9,
	0x15, 0xc9,		// lock detector threshold
	0x16, 0x00,
	0x17, 0x00,
	0x18, 0x00,
	0x19, 0x00,
	0x1a, 0x00,
	0x1f, 0x50,
	0x20, 0x00,
	0x21, 0x00,
	0x22, 0x00,
	0x23, 0x00,
	0x28, 0x00,		// out imp: normal  out type: parallel FEC mode:0
	0x29, 0x1e,		// 1/2 threshold
	0x2a, 0x14,		// 2/3 threshold
	0x2b, 0x0f,		// 3/4 threshold
	0x2c, 0x09,		// 5/6 threshold
	0x2d, 0x05,		// 7/8 threshold
	0x2e, 0x01,
	0x31, 0x1f,		// test all FECs
	0x32, 0x19,		// viterbi and synchro search
	0x33, 0xfc,		// rs control
	0x34, 0x93,		// error control
	0x0f, 0x52,
	0xff, 0xff
};

static int alps_stv0299_set_symbol_rate(struct dvb_frontend *fe, u32 srate, u32 ratio)
{
	u8 aclk = 0;
	u8 bclk = 0;

	if (srate < 1500000) {
		aclk = 0xb7;
		bclk = 0x47;
	} else if (srate < 3000000) {
		aclk = 0xb7;
		bclk = 0x4b;
	} else if (srate < 7000000) {
		aclk = 0xb7;
		bclk = 0x4f;
	} else if (srate < 14000000) {
		aclk = 0xb7;
		bclk = 0x53;
	} else if (srate < 30000000) {
		aclk = 0xb6;
		bclk = 0x53;
	} else if (srate < 45000000) {
		aclk = 0xb4;
		bclk = 0x51;
	}

	stv0299_writereg(fe, 0x13, aclk);
	stv0299_writereg(fe, 0x14, bclk);
	stv0299_writereg(fe, 0x1f, (ratio >> 16) & 0xff);
	stv0299_writereg(fe, 0x20, (ratio >> 8) & 0xff);
	stv0299_writereg(fe, 0x21, (ratio) & 0xf0);

	return 0;
}

static int philips_tsa5059_tuner_set_params(struct dvb_frontend *fe, struct dvb_frontend_parameters *params)
{
	struct ttusb* ttusb = (struct ttusb*) fe->dvb->priv;
	u8 buf[4];
	u32 div;
	struct i2c_msg msg = {.addr = 0x61,.flags = 0,.buf = buf,.len = sizeof(buf) };

	if ((params->frequency < 950000) || (params->frequency > 2150000))
		return -EINVAL;

	div = (params->frequency + (125 - 1)) / 125;	// round correctly
	buf[0] = (div >> 8) & 0x7f;
	buf[1] = div & 0xff;
	buf[2] = 0x80 | ((div & 0x18000) >> 10) | 4;
	buf[3] = 0xC4;

	if (params->frequency > 1530000)
		buf[3] = 0xC0;

	/* BSBE1 wants XCE bit set */
	if (ttusb->revision == TTUSB_REV_2_2)
		buf[3] |= 0x20;

	if (fe->ops->i2c_gate_ctrl)
		fe->ops->i2c_gate_ctrl(fe, 1);
	if (i2c_transfer(&ttusb->i2c_adap, &msg, 1) != 1)
		return -EIO;

	return 0;
}

static struct stv0299_config alps_stv0299_config = {
	.demod_address = 0x68,
	.inittab = alps_bsru6_inittab,
	.mclk = 88000000UL,
	.invert = 1,
	.skip_reinit = 0,
	.lock_output = STV0229_LOCKOUTPUT_1,
	.volt13_op0_op1 = STV0299_VOLT13_OP1,
	.min_delay_ms = 100,
	.set_symbol_rate = alps_stv0299_set_symbol_rate,
};

static int ttusb_novas_grundig_29504_491_tuner_set_params(struct dvb_frontend *fe, struct dvb_frontend_parameters *params)
{
	struct ttusb* ttusb = (struct ttusb*) fe->dvb->priv;
	u8 buf[4];
	u32 div;
	struct i2c_msg msg = {.addr = 0x61,.flags = 0,.buf = buf,.len = sizeof(buf) };

	div = params->frequency / 125;

	buf[0] = (div >> 8) & 0x7f;
	buf[1] = div & 0xff;
	buf[2] = 0x8e;
	buf[3] = 0x00;

	if (fe->ops->i2c_gate_ctrl)
		fe->ops->i2c_gate_ctrl(fe, 1);
	if (i2c_transfer(&ttusb->i2c_adap, &msg, 1) != 1)
		return -EIO;

	return 0;
}

static struct tda8083_config ttusb_novas_grundig_29504_491_config = {

	.demod_address = 0x68,
};

static int alps_tdbe2_tuner_set_params(struct dvb_frontend* fe, struct dvb_frontend_parameters* params)
{
	struct ttusb* ttusb = fe->dvb->priv;
	u32 div;
	u8 data[4];
	struct i2c_msg msg = { .addr = 0x62, .flags = 0, .buf = data, .len = sizeof(data) };

	div = (params->frequency + 35937500 + 31250) / 62500;

	data[0] = (div >> 8) & 0x7f;
	data[1] = div & 0xff;
	data[2] = 0x85 | ((div >> 10) & 0x60);
	data[3] = (params->frequency < 174000000 ? 0x88 : params->frequency < 470000000 ? 0x84 : 0x81);

	if (fe->ops->i2c_gate_ctrl)
		fe->ops->i2c_gate_ctrl(fe, 1);
	if (i2c_transfer (&ttusb->i2c_adap, &msg, 1) != 1)
		return -EIO;

	return 0;
}


static struct ves1820_config alps_tdbe2_config = {
	.demod_address = 0x09,
	.xin = 57840000UL,
	.invert = 1,
	.selagc = VES1820_SELAGC_SIGNAMPERR,
};

static u8 read_pwm(struct ttusb* ttusb)
{
	u8 b = 0xff;
	u8 pwm;
	struct i2c_msg msg[] = { { .addr = 0x50,.flags = 0,.buf = &b,.len = 1 },
				{ .addr = 0x50,.flags = I2C_M_RD,.buf = &pwm,.len = 1} };

	if ((i2c_transfer(&ttusb->i2c_adap, msg, 2) != 2) || (pwm == 0xff))
		pwm = 0x48;

	return pwm;
}


static int dvbc_philips_tdm1316l_tuner_set_params(struct dvb_frontend *fe, struct dvb_frontend_parameters *params)
{
	struct ttusb *ttusb = (struct ttusb *) fe->dvb->priv;
	u8 tuner_buf[5];
	struct i2c_msg tuner_msg = {.addr = 0x60,
				    .flags = 0,
				    .buf = tuner_buf,
				    .len = sizeof(tuner_buf) };
	int tuner_frequency = 0;
	u8 band, cp, filter;

	// determine charge pump
	tuner_frequency = params->frequency;
	if      (tuner_frequency <  87000000) {return -EINVAL;}
	else if (tuner_frequency < 130000000) {cp = 3; band = 1;}
	else if (tuner_frequency < 160000000) {cp = 5; band = 1;}
	else if (tuner_frequency < 200000000) {cp = 6; band = 1;}
	else if (tuner_frequency < 290000000) {cp = 3; band = 2;}
	else if (tuner_frequency < 420000000) {cp = 5; band = 2;}
	else if (tuner_frequency < 480000000) {cp = 6; band = 2;}
	else if (tuner_frequency < 620000000) {cp = 3; band = 4;}
	else if (tuner_frequency < 830000000) {cp = 5; band = 4;}
	else if (tuner_frequency < 895000000) {cp = 7; band = 4;}
	else {return -EINVAL;}

	// assume PLL filter should always be 8MHz for the moment.
	filter = 1;

	// calculate divisor
	// (Finput + Fif)/Fref; Fif = 36125000 Hz, Fref = 62500 Hz
	tuner_frequency = ((params->frequency + 36125000) / 62500);

	// setup tuner buffer
	tuner_buf[0] = tuner_frequency >> 8;
	tuner_buf[1] = tuner_frequency & 0xff;
	tuner_buf[2] = 0xc8;
	tuner_buf[3] = (cp << 5) | (filter << 3) | band;
	tuner_buf[4] = 0x80;

	if (fe->ops->i2c_gate_ctrl)
		fe->ops->i2c_gate_ctrl(fe, 1);
	if (i2c_transfer(&ttusb->i2c_adap, &tuner_msg, 1) != 1) {
		printk("dvb-ttusb-budget: dvbc_philips_tdm1316l_pll_set Error 1\n");
		return -EIO;
	}

	msleep(50);

	if (fe->ops->i2c_gate_ctrl)
		fe->ops->i2c_gate_ctrl(fe, 1);
	if (i2c_transfer(&ttusb->i2c_adap, &tuner_msg, 1) != 1) {
		printk("dvb-ttusb-budget: dvbc_philips_tdm1316l_pll_set Error 2\n");
		return -EIO;
	}

	msleep(1);

	return 0;
}

static u8 dvbc_philips_tdm1316l_inittab[] = {
	0x80, 0x21,
	0x80, 0x20,
	0x81, 0x01,
	0x81, 0x00,
	0x00, 0x09,
	0x01, 0x69,
	0x03, 0x00,
	0x04, 0x00,
	0x07, 0x00,
	0x08, 0x00,
	0x20, 0x00,
	0x21, 0x40,
	0x22, 0x00,
	0x23, 0x00,
	0x24, 0x40,
	0x25, 0x88,
	0x30, 0xff,
	0x31, 0x00,
	0x32, 0xff,
	0x33, 0x00,
	0x34, 0x50,
	0x35, 0x7f,
	0x36, 0x00,
	0x37, 0x20,
	0x38, 0x00,
	0x40, 0x1c,
	0x41, 0xff,
	0x42, 0x29,
	0x43, 0x20,
	0x44, 0xff,
	0x45, 0x00,
	0x46, 0x00,
	0x49, 0x04,
	0x4a, 0xff,
	0x4b, 0x7f,
	0x52, 0x30,
	0x55, 0xae,
	0x56, 0x47,
	0x57, 0xe1,
	0x58, 0x3a,
	0x5a, 0x1e,
	0x5b, 0x34,
	0x60, 0x00,
	0x63, 0x00,
	0x64, 0x00,
	0x65, 0x00,
	0x66, 0x00,
	0x67, 0x00,
	0x68, 0x00,
	0x69, 0x00,
	0x6a, 0x02,
	0x6b, 0x00,
	0x70, 0xff,
	0x71, 0x00,
	0x72, 0x00,
	0x73, 0x00,
	0x74, 0x0c,
	0x80, 0x00,
	0x81, 0x00,
	0x82, 0x00,
	0x83, 0x00,
	0x84, 0x04,
	0x85, 0x80,
	0x86, 0x24,
	0x87, 0x78,
	0x88, 0x00,
	0x89, 0x00,
	0x90, 0x01,
	0x91, 0x01,
	0xa0, 0x00,
	0xa1, 0x00,
	0xa2, 0x00,
	0xb0, 0x91,
	0xb1, 0x0b,
	0xc0, 0x4b,
	0xc1, 0x00,
	0xc2, 0x00,
	0xd0, 0x00,
	0xd1, 0x00,
	0xd2, 0x00,
	0xd3, 0x00,
	0xd4, 0x00,
	0xd5, 0x00,
	0xde, 0x00,
	0xdf, 0x00,
	0x61, 0x38,
	0x62, 0x0a,
	0x53, 0x13,
	0x59, 0x08,
	0x55, 0x00,
	0x56, 0x40,
	0x57, 0x08,
	0x58, 0x3d,
	0x88, 0x10,
	0xa0, 0x00,
	0xa0, 0x00,
	0xa0, 0x00,
	0xa0, 0x04,
	0xff, 0xff,
};

static struct stv0297_config dvbc_philips_tdm1316l_config = {
	.demod_address = 0x1c,
	.inittab = dvbc_philips_tdm1316l_inittab,
	.invert = 0,
};

static void frontend_init(struct ttusb* ttusb)
{
	switch(le16_to_cpu(ttusb->dev->descriptor.idProduct)) {
	case 0x1003: // Hauppauge/TT Nova-USB-S budget (stv0299/ALPS BSRU6|BSBE1(tsa5059))
		// try the stv0299 based first
		ttusb->fe = stv0299_attach(&alps_stv0299_config, &ttusb->i2c_adap);
		if (ttusb->fe != NULL) {
			ttusb->fe->ops->tuner_ops.set_params = philips_tsa5059_tuner_set_params;

			if(ttusb->revision == TTUSB_REV_2_2) { // ALPS BSBE1
				alps_stv0299_config.inittab = alps_bsbe1_inittab;
				lnbp21_attach(ttusb->fe, &ttusb->i2c_adap, 0, 0);
			} else { // ALPS BSRU6
				ttusb->fe->ops->set_voltage = ttusb_set_voltage;
			}
			break;
		}

		// Grundig 29504-491
		ttusb->fe = tda8083_attach(&ttusb_novas_grundig_29504_491_config, &ttusb->i2c_adap);
		if (ttusb->fe != NULL) {
			ttusb->fe->ops->tuner_ops.set_params = ttusb_novas_grundig_29504_491_tuner_set_params;
			ttusb->fe->ops->set_voltage = ttusb_set_voltage;
			break;
		}
		break;

	case 0x1004: // Hauppauge/TT DVB-C budget (ves1820/ALPS TDBE2(sp5659))
		ttusb->fe = ves1820_attach(&alps_tdbe2_config, &ttusb->i2c_adap, read_pwm(ttusb));
		if (ttusb->fe != NULL) {
			ttusb->fe->ops->tuner_ops.set_params = alps_tdbe2_tuner_set_params;
			break;
		}

		ttusb->fe = stv0297_attach(&dvbc_philips_tdm1316l_config, &ttusb->i2c_adap);
		if (ttusb->fe != NULL) {
			ttusb->fe->ops->tuner_ops.set_params = dvbc_philips_tdm1316l_tuner_set_params;
			break;
		}
		break;

	case 0x1005: // Hauppauge/TT Nova-USB-t budget (tda10046/Philips td1316(tda6651tt) OR cx22700/ALPS TDMB7(??))
		// try the ALPS TDMB7 first
		ttusb->fe = cx22700_attach(&alps_tdmb7_config, &ttusb->i2c_adap);
		if (ttusb->fe != NULL) {
			ttusb->fe->ops->tuner_ops.set_params = alps_tdmb7_tuner_set_params;
			break;
		}

		// Philips td1316
		ttusb->fe = tda10046_attach(&philips_tdm1316l_config, &ttusb->i2c_adap);
		if (ttusb->fe != NULL) {
			ttusb->fe->ops->tuner_ops.init = philips_tdm1316l_tuner_init;
			ttusb->fe->ops->tuner_ops.set_params = philips_tdm1316l_tuner_set_params;
			break;
		}
		break;
	}

	if (ttusb->fe == NULL) {
		printk("dvb-ttusb-budget: A frontend driver was not found for device %04x/%04x\n",
		       le16_to_cpu(ttusb->dev->descriptor.idVendor),
		       le16_to_cpu(ttusb->dev->descriptor.idProduct));
	} else {
		if (dvb_register_frontend(&ttusb->adapter, ttusb->fe)) {
			printk("dvb-ttusb-budget: Frontend registration failed!\n");
			if (ttusb->fe->ops->release)
				ttusb->fe->ops->release(ttusb->fe);
			ttusb->fe = NULL;
		}
	}
}



static struct i2c_algorithm ttusb_dec_algo = {
	.master_xfer	= master_xfer,
	.functionality	= functionality,
};

static int ttusb_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device *udev;
	struct ttusb *ttusb;
	int result;

	dprintk("%s: TTUSB DVB connected\n", __FUNCTION__);

	udev = interface_to_usbdev(intf);

	if (intf->altsetting->desc.bInterfaceNumber != 1) return -ENODEV;

	if (!(ttusb = kzalloc(sizeof(struct ttusb), GFP_KERNEL)))
		return -ENOMEM;

	ttusb->dev = udev;
	ttusb->c = 0;
	ttusb->mux_state = 0;
	mutex_init(&ttusb->semi2c);

	mutex_lock(&ttusb->semi2c);

	mutex_init(&ttusb->semusb);

	ttusb_setup_interfaces(ttusb);

	ttusb_alloc_iso_urbs(ttusb);
	if (ttusb_init_controller(ttusb))
		printk("ttusb_init_controller: error\n");

	mutex_unlock(&ttusb->semi2c);

	if ((result = dvb_register_adapter(&ttusb->adapter, "Technotrend/Hauppauge Nova-USB", THIS_MODULE, &udev->dev)) < 0) {
		ttusb_free_iso_urbs(ttusb);
		kfree(ttusb);
		return result;
	}
	ttusb->adapter.priv = ttusb;

	/* i2c */
	memset(&ttusb->i2c_adap, 0, sizeof(struct i2c_adapter));
	strcpy(ttusb->i2c_adap.name, "TTUSB DEC");

	i2c_set_adapdata(&ttusb->i2c_adap, ttusb);

#ifdef I2C_ADAP_CLASS_TV_DIGITAL
	ttusb->i2c_adap.class		  = I2C_ADAP_CLASS_TV_DIGITAL;
#else
	ttusb->i2c_adap.class		  = I2C_CLASS_TV_DIGITAL;
#endif
	ttusb->i2c_adap.algo              = &ttusb_dec_algo;
	ttusb->i2c_adap.algo_data         = NULL;

	result = i2c_add_adapter(&ttusb->i2c_adap);
	if (result) {
		dvb_unregister_adapter (&ttusb->adapter);
		return result;
	}

	memset(&ttusb->dvb_demux, 0, sizeof(ttusb->dvb_demux));

	ttusb->dvb_demux.dmx.capabilities =
	    DMX_TS_FILTERING | DMX_SECTION_FILTERING;
	ttusb->dvb_demux.priv = NULL;
#ifdef TTUSB_HWSECTIONS
	ttusb->dvb_demux.filternum = TTUSB_MAXFILTER;
#else
	ttusb->dvb_demux.filternum = 32;
#endif
	ttusb->dvb_demux.feednum = TTUSB_MAXCHANNEL;
	ttusb->dvb_demux.start_feed = ttusb_start_feed;
	ttusb->dvb_demux.stop_feed = ttusb_stop_feed;
	ttusb->dvb_demux.write_to_decoder = NULL;

	if ((result = dvb_dmx_init(&ttusb->dvb_demux)) < 0) {
		printk("ttusb_dvb: dvb_dmx_init failed (errno = %d)\n", result);
		i2c_del_adapter(&ttusb->i2c_adap);
		dvb_unregister_adapter (&ttusb->adapter);
		return -ENODEV;
	}
//FIXME dmxdev (nur WAS?)
	ttusb->dmxdev.filternum = ttusb->dvb_demux.filternum;
	ttusb->dmxdev.demux = &ttusb->dvb_demux.dmx;
	ttusb->dmxdev.capabilities = 0;

	if ((result = dvb_dmxdev_init(&ttusb->dmxdev, &ttusb->adapter)) < 0) {
		printk("ttusb_dvb: dvb_dmxdev_init failed (errno = %d)\n",
		       result);
		dvb_dmx_release(&ttusb->dvb_demux);
		i2c_del_adapter(&ttusb->i2c_adap);
		dvb_unregister_adapter (&ttusb->adapter);
		return -ENODEV;
	}

	if (dvb_net_init(&ttusb->adapter, &ttusb->dvbnet, &ttusb->dvb_demux.dmx)) {
		printk("ttusb_dvb: dvb_net_init failed!\n");
		dvb_dmxdev_release(&ttusb->dmxdev);
		dvb_dmx_release(&ttusb->dvb_demux);
		i2c_del_adapter(&ttusb->i2c_adap);
		dvb_unregister_adapter (&ttusb->adapter);
		return -ENODEV;
	}

#if 0
	ttusb->stc_devfs_handle =
	    devfs_register(ttusb->adapter->devfs_handle, TTUSB_BUDGET_NAME,
			   DEVFS_FL_DEFAULT, 0, 192,
			   S_IFCHR | S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP
			   | S_IROTH | S_IWOTH, &stc_fops, ttusb);
#endif
	usb_set_intfdata(intf, (void *) ttusb);

	frontend_init(ttusb);

	return 0;
}

static void ttusb_disconnect(struct usb_interface *intf)
{
	struct ttusb *ttusb = usb_get_intfdata(intf);

	usb_set_intfdata(intf, NULL);

	ttusb->disconnecting = 1;

	ttusb_stop_iso_xfer(ttusb);

	ttusb->dvb_demux.dmx.close(&ttusb->dvb_demux.dmx);
	dvb_net_release(&ttusb->dvbnet);
	dvb_dmxdev_release(&ttusb->dmxdev);
	dvb_dmx_release(&ttusb->dvb_demux);
	if (ttusb->fe != NULL) dvb_unregister_frontend(ttusb->fe);
	i2c_del_adapter(&ttusb->i2c_adap);
	dvb_unregister_adapter(&ttusb->adapter);

	ttusb_free_iso_urbs(ttusb);

	kfree(ttusb);

	dprintk("%s: TTUSB DVB disconnected\n", __FUNCTION__);
}

static struct usb_device_id ttusb_table[] = {
	{USB_DEVICE(0xb48, 0x1003)},
	{USB_DEVICE(0xb48, 0x1004)},
	{USB_DEVICE(0xb48, 0x1005)},
	{}
};

MODULE_DEVICE_TABLE(usb, ttusb_table);

static struct usb_driver ttusb_driver = {
      .name		= "ttusb",
      .probe		= ttusb_probe,
      .disconnect	= ttusb_disconnect,
      .id_table		= ttusb_table,
};

static int __init ttusb_init(void)
{
	int err;

	if ((err = usb_register(&ttusb_driver)) < 0) {
		printk("%s: usb_register failed! Error number %d",
		       __FILE__, err);
		return err;
	}

	return 0;
}

static void __exit ttusb_exit(void)
{
	usb_deregister(&ttusb_driver);
}

module_init(ttusb_init);
module_exit(ttusb_exit);

MODULE_AUTHOR("Holger Waechtler <holger@convergence.de>");
MODULE_DESCRIPTION("TTUSB DVB Driver");
MODULE_LICENSE("GPL");
