/*
 * Support for the Broadcom BCM3510 ATSC demodulator (1st generation Air2PC)
 *
 *  Copyright (C) 2001-5, B2C2 inc.
 *
 *  GPL/Linux driver written by Patrick Boettcher <patrick.boettcher@desy.de>
 *
 *  This driver is "hard-coded" to be used with the 1st generation of
 *  Technisat/B2C2's Air2PC ATSC PCI/USB cards/boxes. The pll-programming
 *  (Panasonic CT10S) is located here, which is actually wrong. Unless there is
 *  another device with a BCM3510, this is no problem.
 *
 *  The driver works also with QAM64 DVB-C, but had an unreasonable high
 *  UNC. (Tested with the Air2PC ATSC 1st generation)
 *
 *  You'll need a firmware for this driver in order to get it running. It is
 *  called "dvb-fe-bcm3510-01.fw".
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 675 Mass
 * Ave, Cambridge, MA 02139, USA.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/jiffies.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/mutex.h>

#include "dvb_frontend.h"
#include "bcm3510.h"
#include "bcm3510_priv.h"

/* Max transfer size done by bcm3510_do_hab_cmd() function */
#define MAX_XFER_SIZE	128

struct bcm3510_state {

	struct i2c_adapter* i2c;
	const struct bcm3510_config* config;
	struct dvb_frontend frontend;

	/* demodulator private data */
	struct mutex hab_mutex;
	u8 firmware_loaded:1;

	unsigned long next_status_check;
	unsigned long status_check_interval;
	struct bcm3510_hab_cmd_status1 status1;
	struct bcm3510_hab_cmd_status2 status2;
};

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "set debugging level (1=info,2=i2c (|-able)).");

#define dprintk(level,x...) if (level & debug) printk(x)
#define dbufout(b,l,m) {\
	    int i; \
	    for (i = 0; i < l; i++) \
		m("%02x ",b[i]); \
}
#define deb_info(args...) dprintk(0x01,args)
#define deb_i2c(args...)  dprintk(0x02,args)
#define deb_hab(args...)  dprintk(0x04,args)

/* transfer functions */
static int bcm3510_writebytes (struct bcm3510_state *state, u8 reg, u8 *buf, u8 len)
{
	u8 b[256];
	int err;
	struct i2c_msg msg = { .addr = state->config->demod_address, .flags = 0, .buf = b, .len = len + 1 };

	b[0] = reg;
	memcpy(&b[1],buf,len);

	deb_i2c("i2c wr %02x: ",reg);
	dbufout(buf,len,deb_i2c);
	deb_i2c("\n");

	if ((err = i2c_transfer (state->i2c, &msg, 1)) != 1) {

		deb_info("%s: i2c write error (addr %02x, reg %02x, err == %i)\n",
			__func__, state->config->demod_address, reg,  err);
		return -EREMOTEIO;
	}

	return 0;
}

static int bcm3510_readbytes (struct bcm3510_state *state, u8 reg, u8 *buf, u8 len)
{
	struct i2c_msg msg[] = {
		{ .addr = state->config->demod_address, .flags = 0,        .buf = &reg, .len = 1 },
		{ .addr = state->config->demod_address, .flags = I2C_M_RD, .buf = buf,  .len = len }
	};
	int err;

	memset(buf,0,len);

	if ((err = i2c_transfer (state->i2c, msg, 2)) != 2) {
		deb_info("%s: i2c read error (addr %02x, reg %02x, err == %i)\n",
			__func__, state->config->demod_address, reg,  err);
		return -EREMOTEIO;
	}
	deb_i2c("i2c rd %02x: ",reg);
	dbufout(buf,len,deb_i2c);
	deb_i2c("\n");

	return 0;
}

static int bcm3510_writeB(struct bcm3510_state *state, u8 reg, bcm3510_register_value v)
{
	return bcm3510_writebytes(state,reg,&v.raw,1);
}

static int bcm3510_readB(struct bcm3510_state *state, u8 reg, bcm3510_register_value *v)
{
	return bcm3510_readbytes(state,reg,&v->raw,1);
}

/* Host Access Buffer transfers */
static int bcm3510_hab_get_response(struct bcm3510_state *st, u8 *buf, int len)
{
	bcm3510_register_value v;
	int ret,i;

	v.HABADR_a6.HABADR = 0;
	if ((ret = bcm3510_writeB(st,0xa6,v)) < 0)
		return ret;

	for (i = 0; i < len; i++) {
		if ((ret = bcm3510_readB(st,0xa7,&v)) < 0)
			return ret;
		buf[i] = v.HABDATA_a7;
	}
	return 0;
}

static int bcm3510_hab_send_request(struct bcm3510_state *st, u8 *buf, int len)
{
	bcm3510_register_value v,hab;
	int ret,i;
	unsigned long t;

/* Check if any previous HAB request still needs to be serviced by the
 * Acquisition Processor before sending new request */
	if ((ret = bcm3510_readB(st,0xa8,&v)) < 0)
		return ret;
	if (v.HABSTAT_a8.HABR) {
		deb_info("HAB is running already - clearing it.\n");
		v.HABSTAT_a8.HABR = 0;
		bcm3510_writeB(st,0xa8,v);
//		return -EBUSY;
	}

/* Send the start HAB Address (automatically incremented after write of
 * HABDATA) and write the HAB Data */
	hab.HABADR_a6.HABADR = 0;
	if ((ret = bcm3510_writeB(st,0xa6,hab)) < 0)
		return ret;

	for (i = 0; i < len; i++) {
		hab.HABDATA_a7 = buf[i];
		if ((ret = bcm3510_writeB(st,0xa7,hab)) < 0)
			return ret;
	}

/* Set the HABR bit to indicate AP request in progress (LBHABR allows HABR to
 * be written) */
	v.raw = 0; v.HABSTAT_a8.HABR = 1; v.HABSTAT_a8.LDHABR = 1;
	if ((ret = bcm3510_writeB(st,0xa8,v)) < 0)
		return ret;

/* Polling method: Wait until the AP finishes processing the HAB request */
	t = jiffies + 1*HZ;
	while (time_before(jiffies, t)) {
		deb_info("waiting for HAB to complete\n");
		msleep(10);
		if ((ret = bcm3510_readB(st,0xa8,&v)) < 0)
			return ret;

		if (!v.HABSTAT_a8.HABR)
			return 0;
	}

	deb_info("send_request execution timed out.\n");
	return -ETIMEDOUT;
}

static int bcm3510_do_hab_cmd(struct bcm3510_state *st, u8 cmd, u8 msgid, u8 *obuf, u8 olen, u8 *ibuf, u8 ilen)
{
	u8 ob[MAX_XFER_SIZE], ib[MAX_XFER_SIZE];
	int ret = 0;

	if (ilen + 2 > sizeof(ib)) {
		deb_hab("do_hab_cmd: ilen=%d is too big!\n", ilen);
		return -EINVAL;
	}

	if (olen + 2 > sizeof(ob)) {
		deb_hab("do_hab_cmd: olen=%d is too big!\n", olen);
		return -EINVAL;
	}

	ob[0] = cmd;
	ob[1] = msgid;
	memcpy(&ob[2],obuf,olen);

	deb_hab("hab snd: ");
	dbufout(ob,olen+2,deb_hab);
	deb_hab("\n");

	if (mutex_lock_interruptible(&st->hab_mutex) < 0)
		return -EAGAIN;

	if ((ret = bcm3510_hab_send_request(st, ob, olen+2)) < 0 ||
		(ret = bcm3510_hab_get_response(st, ib, ilen+2)) < 0)
		goto error;

	deb_hab("hab get: ");
	dbufout(ib,ilen+2,deb_hab);
	deb_hab("\n");

	memcpy(ibuf,&ib[2],ilen);
error:
	mutex_unlock(&st->hab_mutex);
	return ret;
}

#if 0
/* not needed, we use a semaphore to prevent HAB races */
static int bcm3510_is_ap_ready(struct bcm3510_state *st)
{
	bcm3510_register_value ap,hab;
	int ret;

	if ((ret = bcm3510_readB(st,0xa8,&hab)) < 0 ||
		(ret = bcm3510_readB(st,0xa2,&ap) < 0))
		return ret;

	if (ap.APSTAT1_a2.RESET || ap.APSTAT1_a2.IDLE || ap.APSTAT1_a2.STOP || hab.HABSTAT_a8.HABR) {
		deb_info("AP is busy\n");
		return -EBUSY;
	}

	return 0;
}
#endif

static int bcm3510_bert_reset(struct bcm3510_state *st)
{
	bcm3510_register_value b;
	int ret;

	if ((ret = bcm3510_readB(st,0xfa,&b)) < 0)
		return ret;

	b.BERCTL_fa.RESYNC = 0; bcm3510_writeB(st,0xfa,b);
	b.BERCTL_fa.RESYNC = 1; bcm3510_writeB(st,0xfa,b);
	b.BERCTL_fa.RESYNC = 0; bcm3510_writeB(st,0xfa,b);
	b.BERCTL_fa.CNTCTL = 1; b.BERCTL_fa.BITCNT = 1; bcm3510_writeB(st,0xfa,b);

	/* clear residual bit counter TODO  */
	return 0;
}

static int bcm3510_refresh_state(struct bcm3510_state *st)
{
	if (time_after(jiffies,st->next_status_check)) {
		bcm3510_do_hab_cmd(st, CMD_STATUS, MSGID_STATUS1, NULL,0, (u8 *)&st->status1, sizeof(st->status1));
		bcm3510_do_hab_cmd(st, CMD_STATUS, MSGID_STATUS2, NULL,0, (u8 *)&st->status2, sizeof(st->status2));
		st->next_status_check = jiffies + (st->status_check_interval*HZ)/1000;
	}
	return 0;
}

static int bcm3510_read_status(struct dvb_frontend *fe, fe_status_t *status)
{
	struct bcm3510_state* st = fe->demodulator_priv;
	bcm3510_refresh_state(st);

	*status = 0;
	if (st->status1.STATUS1.RECEIVER_LOCK)
		*status |= FE_HAS_LOCK | FE_HAS_SYNC;

	if (st->status1.STATUS1.FEC_LOCK)
		*status |= FE_HAS_VITERBI;

	if (st->status1.STATUS1.OUT_PLL_LOCK)
		*status |= FE_HAS_SIGNAL | FE_HAS_CARRIER;

	if (*status & FE_HAS_LOCK)
		st->status_check_interval = 1500;
	else /* more frequently checks if no lock has been achieved yet */
		st->status_check_interval = 500;

	deb_info("real_status: %02x\n",*status);
	return 0;
}

static int bcm3510_read_ber(struct dvb_frontend* fe, u32* ber)
{
	struct bcm3510_state* st = fe->demodulator_priv;
	bcm3510_refresh_state(st);

	*ber = (st->status2.LDBER0 << 16) | (st->status2.LDBER1 << 8) | st->status2.LDBER2;
	return 0;
}

static int bcm3510_read_unc(struct dvb_frontend* fe, u32* unc)
{
	struct bcm3510_state* st = fe->demodulator_priv;
	bcm3510_refresh_state(st);
	*unc = (st->status2.LDUERC0 << 8) | st->status2.LDUERC1;
	return 0;
}

static int bcm3510_read_signal_strength(struct dvb_frontend* fe, u16* strength)
{
	struct bcm3510_state* st = fe->demodulator_priv;
	s32 t;

	bcm3510_refresh_state(st);
	t = st->status2.SIGNAL;

	if (t > 190)
		t = 190;
	if (t < 90)
		t = 90;

	t -= 90;
	t = t * 0xff / 100;
	/* normalize if necessary */
	*strength = (t << 8) | t;
	return 0;
}

static int bcm3510_read_snr(struct dvb_frontend* fe, u16* snr)
{
	struct bcm3510_state* st = fe->demodulator_priv;
	bcm3510_refresh_state(st);

	*snr = st->status1.SNR_EST0*1000 + ((st->status1.SNR_EST1*1000) >> 8);
	return 0;
}

/* tuner frontend programming */
static int bcm3510_tuner_cmd(struct bcm3510_state* st,u8 bc, u16 n, u8 a)
{
	struct bcm3510_hab_cmd_tune c;
	memset(&c,0,sizeof(struct bcm3510_hab_cmd_tune));

/* I2C Mode disabled,  set 16 control / Data pairs */
	c.length = 0x10;
	c.clock_width = 0;
/* CS1, CS0, DATA, CLK bits control the tuner RF_AGC_SEL pin is set to
 * logic high (as Configuration) */
	c.misc = 0x10;
/* Set duration of the initial state of TUNCTL = 3.34 micro Sec */
	c.TUNCTL_state = 0x40;

/* PRESCALER DIVIDE RATIO | BC1_2_3_4; (band switch), 1stosc REFERENCE COUNTER REF_S12 and REF_S11 */
	c.ctl_dat[0].ctrl.size = BITS_8;
	c.ctl_dat[0].data      = 0x80 | bc;

/* Control DATA pin, 1stosc REFERENCE COUNTER REF_S10 to REF_S3 */
	c.ctl_dat[1].ctrl.size = BITS_8;
	c.ctl_dat[1].data      = 4;

/* set CONTROL BIT 1 to 1, 1stosc REFERENCE COUNTER REF_S2 to REF_S1 */
	c.ctl_dat[2].ctrl.size = BITS_3;
	c.ctl_dat[2].data      = 0x20;

/* control CS0 pin, pulse byte ? */
	c.ctl_dat[3].ctrl.size = BITS_3;
	c.ctl_dat[3].ctrl.clk_off = 1;
	c.ctl_dat[3].ctrl.cs0  = 1;
	c.ctl_dat[3].data      = 0x40;

/* PGM_S18 to PGM_S11 */
	c.ctl_dat[4].ctrl.size = BITS_8;
	c.ctl_dat[4].data      = n >> 3;

/* PGM_S10 to PGM_S8, SWL_S7 to SWL_S3 */
	c.ctl_dat[5].ctrl.size = BITS_8;
	c.ctl_dat[5].data      = ((n & 0x7) << 5) | (a >> 2);

/* SWL_S2 and SWL_S1, set CONTROL BIT 2 to 0 */
	c.ctl_dat[6].ctrl.size = BITS_3;
	c.ctl_dat[6].data      = (a << 6) & 0xdf;

/* control CS0 pin, pulse byte ? */
	c.ctl_dat[7].ctrl.size = BITS_3;
	c.ctl_dat[7].ctrl.clk_off = 1;
	c.ctl_dat[7].ctrl.cs0  = 1;
	c.ctl_dat[7].data      = 0x40;

/* PRESCALER DIVIDE RATIO, 2ndosc REFERENCE COUNTER REF_S12 and REF_S11 */
	c.ctl_dat[8].ctrl.size = BITS_8;
	c.ctl_dat[8].data      = 0x80;

/* 2ndosc REFERENCE COUNTER REF_S10 to REF_S3 */
	c.ctl_dat[9].ctrl.size = BITS_8;
	c.ctl_dat[9].data      = 0x10;

/* set CONTROL BIT 1 to 1, 2ndosc REFERENCE COUNTER REF_S2 to REF_S1 */
	c.ctl_dat[10].ctrl.size = BITS_3;
	c.ctl_dat[10].data      = 0x20;

/* pulse byte */
	c.ctl_dat[11].ctrl.size = BITS_3;
	c.ctl_dat[11].ctrl.clk_off = 1;
	c.ctl_dat[11].ctrl.cs1  = 1;
	c.ctl_dat[11].data      = 0x40;

/* PGM_S18 to PGM_S11 */
	c.ctl_dat[12].ctrl.size = BITS_8;
	c.ctl_dat[12].data      = 0x2a;

/* PGM_S10 to PGM_S8 and SWL_S7 to SWL_S3 */
	c.ctl_dat[13].ctrl.size = BITS_8;
	c.ctl_dat[13].data      = 0x8e;

/* SWL_S2 and SWL_S1 and set CONTROL BIT 2 to 0 */
	c.ctl_dat[14].ctrl.size = BITS_3;
	c.ctl_dat[14].data      = 0;

/* Pulse Byte */
	c.ctl_dat[15].ctrl.size = BITS_3;
	c.ctl_dat[15].ctrl.clk_off = 1;
	c.ctl_dat[15].ctrl.cs1  = 1;
	c.ctl_dat[15].data      = 0x40;

	return bcm3510_do_hab_cmd(st,CMD_TUNE, MSGID_TUNE,(u8 *) &c,sizeof(c), NULL, 0);
}

static int bcm3510_set_freq(struct bcm3510_state* st,u32 freq)
{
	u8 bc,a;
	u16 n;
	s32 YIntercept,Tfvco1;

	freq /= 1000;

	deb_info("%dkHz:",freq);
	/* set Band Switch */
	if (freq <= 168000)
		bc = 0x1c;
	else if (freq <= 378000)
		bc = 0x2c;
	else
		bc = 0x30;

	if (freq >= 470000) {
		freq -= 470001;
		YIntercept = 18805;
	} else if (freq >= 90000) {
		freq -= 90001;
		YIntercept = 15005;
	} else if (freq >= 76000){
		freq -= 76001;
		YIntercept = 14865;
	} else {
		freq -= 54001;
		YIntercept = 14645;
	}

	Tfvco1 = (((freq/6000)*60 + YIntercept)*4)/10;

	n = Tfvco1 >> 6;
	a = Tfvco1 & 0x3f;

	deb_info(" BC1_2_3_4: %x, N: %x A: %x\n", bc, n, a);
	if (n >= 16 && n <= 2047)
		return bcm3510_tuner_cmd(st,bc,n,a);

	return -EINVAL;
}

static int bcm3510_set_frontend(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct bcm3510_state* st = fe->demodulator_priv;
	struct bcm3510_hab_cmd_ext_acquire cmd;
	struct bcm3510_hab_cmd_bert_control bert;
	int ret;

	memset(&cmd,0,sizeof(cmd));
	switch (c->modulation) {
		case QAM_256:
			cmd.ACQUIRE0.MODE = 0x1;
			cmd.ACQUIRE1.SYM_RATE = 0x1;
			cmd.ACQUIRE1.IF_FREQ = 0x1;
			break;
		case QAM_64:
			cmd.ACQUIRE0.MODE = 0x2;
			cmd.ACQUIRE1.SYM_RATE = 0x2;
			cmd.ACQUIRE1.IF_FREQ = 0x1;
			break;
#if 0
		case QAM_256:
			cmd.ACQUIRE0.MODE = 0x3;
			break;
		case QAM_128:
			cmd.ACQUIRE0.MODE = 0x4;
			break;
		case QAM_64:
			cmd.ACQUIRE0.MODE = 0x5;
			break;
		case QAM_32:
			cmd.ACQUIRE0.MODE = 0x6;
			break;
		case QAM_16:
			cmd.ACQUIRE0.MODE = 0x7;
			break;
#endif
		case VSB_8:
			cmd.ACQUIRE0.MODE = 0x8;
			cmd.ACQUIRE1.SYM_RATE = 0x0;
			cmd.ACQUIRE1.IF_FREQ = 0x0;
			break;
		case VSB_16:
			cmd.ACQUIRE0.MODE = 0x9;
			cmd.ACQUIRE1.SYM_RATE = 0x0;
			cmd.ACQUIRE1.IF_FREQ = 0x0;
		default:
			return -EINVAL;
	}
	cmd.ACQUIRE0.OFFSET = 0;
	cmd.ACQUIRE0.NTSCSWEEP = 1;
	cmd.ACQUIRE0.FA = 1;
	cmd.ACQUIRE0.BW = 0;

/*	if (enableOffset) {
		cmd.IF_OFFSET0 = xx;
		cmd.IF_OFFSET1 = xx;

		cmd.SYM_OFFSET0 = xx;
		cmd.SYM_OFFSET1 = xx;
		if (enableNtscSweep) {
			cmd.NTSC_OFFSET0;
			cmd.NTSC_OFFSET1;
		}
	} */
	bcm3510_do_hab_cmd(st, CMD_ACQUIRE, MSGID_EXT_TUNER_ACQUIRE, (u8 *) &cmd, sizeof(cmd), NULL, 0);

/* doing it with different MSGIDs, data book and source differs */
	bert.BE = 0;
	bert.unused = 0;
	bcm3510_do_hab_cmd(st, CMD_STATE_CONTROL, MSGID_BERT_CONTROL, (u8 *) &bert, sizeof(bert), NULL, 0);
	bcm3510_do_hab_cmd(st, CMD_STATE_CONTROL, MSGID_BERT_SET, (u8 *) &bert, sizeof(bert), NULL, 0);

	bcm3510_bert_reset(st);

	ret = bcm3510_set_freq(st, c->frequency);
	if (ret < 0)
		return ret;

	memset(&st->status1,0,sizeof(st->status1));
	memset(&st->status2,0,sizeof(st->status2));
	st->status_check_interval = 500;

/* Give the AP some time */
	msleep(200);

	return 0;
}

static int bcm3510_sleep(struct dvb_frontend* fe)
{
	return 0;
}

static int bcm3510_get_tune_settings(struct dvb_frontend *fe, struct dvb_frontend_tune_settings *s)
{
	s->min_delay_ms = 1000;
	s->step_size = 0;
	s->max_drift = 0;
	return 0;
}

static void bcm3510_release(struct dvb_frontend* fe)
{
	struct bcm3510_state* state = fe->demodulator_priv;
	kfree(state);
}

/* firmware download:
 * firmware file is build up like this:
 * 16bit addr, 16bit length, 8byte of length
 */
#define BCM3510_DEFAULT_FIRMWARE "dvb-fe-bcm3510-01.fw"

static int bcm3510_write_ram(struct bcm3510_state *st, u16 addr, const u8 *b,
			     u16 len)
{
	int ret = 0,i;
	bcm3510_register_value vH, vL,vD;

	vH.MADRH_a9 = addr >> 8;
	vL.MADRL_aa = addr;
	if ((ret = bcm3510_writeB(st,0xa9,vH)) < 0) return ret;
	if ((ret = bcm3510_writeB(st,0xaa,vL)) < 0) return ret;

	for (i = 0; i < len; i++) {
		vD.MDATA_ab = b[i];
		if ((ret = bcm3510_writeB(st,0xab,vD)) < 0)
			return ret;
	}

	return 0;
}

static int bcm3510_download_firmware(struct dvb_frontend* fe)
{
	struct bcm3510_state* st = fe->demodulator_priv;
	const struct firmware *fw;
	u16 addr,len;
	const u8 *b;
	int ret,i;

	deb_info("requesting firmware\n");
	if ((ret = st->config->request_firmware(fe, &fw, BCM3510_DEFAULT_FIRMWARE)) < 0) {
		err("could not load firmware (%s): %d",BCM3510_DEFAULT_FIRMWARE,ret);
		return ret;
	}
	deb_info("got firmware: %zu\n", fw->size);

	b = fw->data;
	for (i = 0; i < fw->size;) {
		addr = le16_to_cpu(*((__le16 *)&b[i]));
		len  = le16_to_cpu(*((__le16 *)&b[i+2]));
		deb_info("firmware chunk, addr: 0x%04x, len: 0x%04x, total length: 0x%04zx\n",addr,len,fw->size);
		if ((ret = bcm3510_write_ram(st,addr,&b[i+4],len)) < 0) {
			err("firmware download failed: %d\n",ret);
			return ret;
		}
		i += 4 + len;
	}
	release_firmware(fw);
	deb_info("firmware download successfully completed\n");
	return 0;
}

static int bcm3510_check_firmware_version(struct bcm3510_state *st)
{
	struct bcm3510_hab_cmd_get_version_info ver;
	bcm3510_do_hab_cmd(st,CMD_GET_VERSION_INFO,MSGID_GET_VERSION_INFO,NULL,0,(u8*)&ver,sizeof(ver));

	deb_info("Version information: 0x%02x 0x%02x 0x%02x 0x%02x\n",
		ver.microcode_version, ver.script_version, ver.config_version, ver.demod_version);

	if (ver.script_version == BCM3510_DEF_SCRIPT_VERSION &&
		ver.config_version == BCM3510_DEF_CONFIG_VERSION &&
		ver.demod_version  == BCM3510_DEF_DEMOD_VERSION)
		return 0;

	deb_info("version check failed\n");
	return -ENODEV;
}

/* (un)resetting the AP */
static int bcm3510_reset(struct bcm3510_state *st)
{
	int ret;
	unsigned long  t;
	bcm3510_register_value v;

	bcm3510_readB(st,0xa0,&v); v.HCTL1_a0.RESET = 1;
	if ((ret = bcm3510_writeB(st,0xa0,v)) < 0)
		return ret;

    t = jiffies + 3*HZ;
	while (time_before(jiffies, t)) {
		msleep(10);
		if ((ret = bcm3510_readB(st,0xa2,&v)) < 0)
			return ret;

		if (v.APSTAT1_a2.RESET)
			return 0;
	}
	deb_info("reset timed out\n");
	return -ETIMEDOUT;
}

static int bcm3510_clear_reset(struct bcm3510_state *st)
{
	bcm3510_register_value v;
	int ret;
	unsigned long t;

	v.raw = 0;
	if ((ret = bcm3510_writeB(st,0xa0,v)) < 0)
		return ret;

    t = jiffies + 3*HZ;
	while (time_before(jiffies, t)) {
		msleep(10);
		if ((ret = bcm3510_readB(st,0xa2,&v)) < 0)
			return ret;

		/* verify that reset is cleared */
		if (!v.APSTAT1_a2.RESET)
			return 0;
	}
	deb_info("reset clear timed out\n");
	return -ETIMEDOUT;
}

static int bcm3510_init_cold(struct bcm3510_state *st)
{
	int ret;
	bcm3510_register_value v;

	/* read Acquisation Processor status register and check it is not in RUN mode */
	if ((ret = bcm3510_readB(st,0xa2,&v)) < 0)
		return ret;
	if (v.APSTAT1_a2.RUN) {
		deb_info("AP is already running - firmware already loaded.\n");
		return 0;
	}

	deb_info("reset?\n");
	if ((ret = bcm3510_reset(st)) < 0)
		return ret;

	deb_info("tristate?\n");
	/* tri-state */
	v.TSTCTL_2e.CTL = 0;
	if ((ret = bcm3510_writeB(st,0x2e,v)) < 0)
		return ret;

	deb_info("firmware?\n");
	if ((ret = bcm3510_download_firmware(&st->frontend)) < 0 ||
		(ret = bcm3510_clear_reset(st)) < 0)
		return ret;

	/* anything left here to Let the acquisition processor begin execution at program counter 0000 ??? */

	return 0;
}

static int bcm3510_init(struct dvb_frontend* fe)
{
	struct bcm3510_state* st = fe->demodulator_priv;
	bcm3510_register_value j;
	struct bcm3510_hab_cmd_set_agc c;
	int ret;

	if ((ret = bcm3510_readB(st,0xca,&j)) < 0)
		return ret;

	deb_info("JDEC: %02x\n",j.raw);

	switch (j.JDEC_ca.JDEC) {
		case JDEC_WAIT_AT_RAM:
			deb_info("attempting to download firmware\n");
			if ((ret = bcm3510_init_cold(st)) < 0)
				return ret;
		case JDEC_EEPROM_LOAD_WAIT: /* fall-through is wanted */
			deb_info("firmware is loaded\n");
			bcm3510_check_firmware_version(st);
			break;
		default:
			return -ENODEV;
	}

	memset(&c,0,1);
	c.SEL = 1;
	bcm3510_do_hab_cmd(st,CMD_AUTO_PARAM,MSGID_SET_RF_AGC_SEL,(u8 *)&c,sizeof(c),NULL,0);

	return 0;
}


static struct dvb_frontend_ops bcm3510_ops;

struct dvb_frontend* bcm3510_attach(const struct bcm3510_config *config,
				   struct i2c_adapter *i2c)
{
	struct bcm3510_state* state = NULL;
	int ret;
	bcm3510_register_value v;

	/* allocate memory for the internal state */
	state = kzalloc(sizeof(struct bcm3510_state), GFP_KERNEL);
	if (state == NULL)
		goto error;

	/* setup the state */

	state->config = config;
	state->i2c = i2c;

	/* create dvb_frontend */
	memcpy(&state->frontend.ops, &bcm3510_ops, sizeof(struct dvb_frontend_ops));
	state->frontend.demodulator_priv = state;

	mutex_init(&state->hab_mutex);

	if ((ret = bcm3510_readB(state,0xe0,&v)) < 0)
		goto error;

	deb_info("Revision: 0x%1x, Layer: 0x%1x.\n",v.REVID_e0.REV,v.REVID_e0.LAYER);

	if ((v.REVID_e0.REV != 0x1 && v.REVID_e0.LAYER != 0xb) && /* cold */
		(v.REVID_e0.REV != 0x8 && v.REVID_e0.LAYER != 0x0))   /* warm */
		goto error;

	info("Revision: 0x%1x, Layer: 0x%1x.",v.REVID_e0.REV,v.REVID_e0.LAYER);

	bcm3510_reset(state);

	return &state->frontend;

error:
	kfree(state);
	return NULL;
}
EXPORT_SYMBOL(bcm3510_attach);

static struct dvb_frontend_ops bcm3510_ops = {
	.delsys = { SYS_ATSC, SYS_DVBC_ANNEX_B },
	.info = {
		.name = "Broadcom BCM3510 VSB/QAM frontend",
		.frequency_min =  54000000,
		.frequency_max = 803000000,
		/* stepsize is just a guess */
		.frequency_stepsize = 0,
		.caps =
			FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
			FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
			FE_CAN_8VSB | FE_CAN_16VSB |
			FE_CAN_QAM_16 | FE_CAN_QAM_64 | FE_CAN_QAM_128 | FE_CAN_QAM_256
	},

	.release = bcm3510_release,

	.init = bcm3510_init,
	.sleep = bcm3510_sleep,

	.set_frontend = bcm3510_set_frontend,
	.get_tune_settings = bcm3510_get_tune_settings,

	.read_status = bcm3510_read_status,
	.read_ber = bcm3510_read_ber,
	.read_signal_strength = bcm3510_read_signal_strength,
	.read_snr = bcm3510_read_snr,
	.read_ucblocks = bcm3510_read_unc,
};

MODULE_DESCRIPTION("Broadcom BCM3510 ATSC (8VSB/16VSB & ITU J83 AnnexB FEC QAM64/256) demodulator driver");
MODULE_AUTHOR("Patrick Boettcher <patrick.boettcher@desy.de>");
MODULE_LICENSE("GPL");
