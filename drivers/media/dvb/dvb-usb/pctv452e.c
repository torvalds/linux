/*
 * PCTV 452e DVB driver
 *
 * Copyright (c) 2006-2008 Dominik Kuhlen <dkuhlen@gmx.net>
 *
 * TT connect S2-3650-CI Common Interface support, MAC readout
 * Copyright (C) 2008 Michael H. Schimek <mschimek@gmx.at>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

/* dvb usb framework */
#define DVB_USB_LOG_PREFIX "pctv452e"
#include "dvb-usb.h"

/* Demodulator */
#include "stb0899_drv.h"
#include "stb0899_reg.h"
#include "stb0899_cfg.h"
/* Tuner */
#include "stb6100.h"
#include "stb6100_cfg.h"
/* FE Power */
#include "lnbp22.h"

#include "dvb_ca_en50221.h"
#include "ttpci-eeprom.h"

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off debugging (default:off).");

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

#define ISOC_INTERFACE_ALTERNATIVE 3

#define SYNC_BYTE_OUT 0xaa
#define SYNC_BYTE_IN  0x55

/* guessed: (copied from ttusb-budget) */
#define PCTV_CMD_RESET 0x15
/* command to poll IR receiver */
#define PCTV_CMD_IR    0x1b
/* command to send I2C  */
#define PCTV_CMD_I2C   0x31

#define I2C_ADDR_STB0899 (0xd0 >> 1)
#define I2C_ADDR_STB6100 (0xc0 >> 1)
#define I2C_ADDR_LNBP22  (0x10 >> 1)
#define I2C_ADDR_24C16   (0xa0 >> 1)
#define I2C_ADDR_24C64   (0xa2 >> 1)


/* pctv452e sends us this amount of data for each issued usb-command */
#define PCTV_ANSWER_LEN 64
/* Wait up to 1000ms for device  */
#define PCTV_TIMEOUT 1000


#define PCTV_LED_GPIO   STB0899_GPIO01
#define PCTV_LED_GREEN  0x82
#define PCTV_LED_ORANGE 0x02

#define ci_dbg(format, arg...)				\
do {							\
	if (0)						\
		printk(KERN_DEBUG DVB_USB_LOG_PREFIX	\
			": " format "\n" , ## arg);	\
} while (0)

enum {
	TT3650_CMD_CI_TEST = 0x40,
	TT3650_CMD_CI_RD_CTRL,
	TT3650_CMD_CI_WR_CTRL,
	TT3650_CMD_CI_RD_ATTR,
	TT3650_CMD_CI_WR_ATTR,
	TT3650_CMD_CI_RESET,
	TT3650_CMD_CI_SET_VIDEO_PORT
};


static struct stb0899_postproc pctv45e_postproc[] = {
	{ PCTV_LED_GPIO, STB0899_GPIOPULLUP },
	{ 0, 0 }
};

/*
 * stores all private variables for communication with the PCTV452e DVB-S2
 */
struct pctv452e_state {
	struct dvb_ca_en50221 ca;
	struct mutex ca_mutex;

	u8 c;	   /* transaction counter, wraps around...  */
	u8 initialized; /* set to 1 if 0x15 has been sent */
	u16 last_rc_key;
};

static int tt3650_ci_msg(struct dvb_usb_device *d, u8 cmd, u8 *data,
			 unsigned int write_len, unsigned int read_len)
{
	struct pctv452e_state *state = (struct pctv452e_state *)d->priv;
	u8 buf[64];
	u8 id;
	unsigned int rlen;
	int ret;

	BUG_ON(NULL == data && 0 != (write_len | read_len));
	BUG_ON(write_len > 64 - 4);
	BUG_ON(read_len > 64 - 4);

	id = state->c++;

	buf[0] = SYNC_BYTE_OUT;
	buf[1] = id;
	buf[2] = cmd;
	buf[3] = write_len;

	memcpy(buf + 4, data, write_len);

	rlen = (read_len > 0) ? 64 : 0;
	ret = dvb_usb_generic_rw(d, buf, 4 + write_len,
				  buf, rlen, /* delay_ms */ 0);
	if (0 != ret)
		goto failed;

	ret = -EIO;
	if (SYNC_BYTE_IN != buf[0] || id != buf[1])
		goto failed;

	memcpy(data, buf + 4, read_len);

	return 0;

failed:
	err("CI error %d; %02X %02X %02X -> %02X %02X %02X.",
	     ret, SYNC_BYTE_OUT, id, cmd, buf[0], buf[1], buf[2]);

	return ret;
}

static int tt3650_ci_msg_locked(struct dvb_ca_en50221 *ca,
				u8 cmd, u8 *data, unsigned int write_len,
				unsigned int read_len)
{
	struct dvb_usb_device *d = (struct dvb_usb_device *)ca->data;
	struct pctv452e_state *state = (struct pctv452e_state *)d->priv;
	int ret;

	mutex_lock(&state->ca_mutex);
	ret = tt3650_ci_msg(d, cmd, data, write_len, read_len);
	mutex_unlock(&state->ca_mutex);

	return ret;
}

static int tt3650_ci_read_attribute_mem(struct dvb_ca_en50221 *ca,
				 int slot, int address)
{
	u8 buf[3];
	int ret;

	if (0 != slot)
		return -EINVAL;

	buf[0] = (address >> 8) & 0x0F;
	buf[1] = address;

	ret = tt3650_ci_msg_locked(ca, TT3650_CMD_CI_RD_ATTR, buf, 2, 3);

	ci_dbg("%s %04x -> %d 0x%02x",
		__func__, address, ret, buf[2]);

	if (ret < 0)
		return ret;

	return buf[2];
}

static int tt3650_ci_write_attribute_mem(struct dvb_ca_en50221 *ca,
				 int slot, int address, u8 value)
{
	u8 buf[3];

	ci_dbg("%s %d 0x%04x 0x%02x",
		__func__, slot, address, value);

	if (0 != slot)
		return -EINVAL;

	buf[0] = (address >> 8) & 0x0F;
	buf[1] = address;
	buf[2] = value;

	return tt3650_ci_msg_locked(ca, TT3650_CMD_CI_WR_ATTR, buf, 3, 3);
}

static int tt3650_ci_read_cam_control(struct dvb_ca_en50221 *ca,
				 int			slot,
				 u8			address)
{
	u8 buf[2];
	int ret;

	if (0 != slot)
		return -EINVAL;

	buf[0] = address & 3;

	ret = tt3650_ci_msg_locked(ca, TT3650_CMD_CI_RD_CTRL, buf, 1, 2);

	ci_dbg("%s 0x%02x -> %d 0x%02x",
		__func__, address, ret, buf[1]);

	if (ret < 0)
		return ret;

	return buf[1];
}

static int tt3650_ci_write_cam_control(struct dvb_ca_en50221 *ca,
				 int			slot,
				 u8			address,
				 u8			value)
{
	u8 buf[2];

	ci_dbg("%s %d 0x%02x 0x%02x",
		__func__, slot, address, value);

	if (0 != slot)
		return -EINVAL;

	buf[0] = address;
	buf[1] = value;

	return tt3650_ci_msg_locked(ca, TT3650_CMD_CI_WR_CTRL, buf, 2, 2);
}

static int tt3650_ci_set_video_port(struct dvb_ca_en50221 *ca,
				 int			slot,
				 int			enable)
{
	u8 buf[1];
	int ret;

	ci_dbg("%s %d %d", __func__, slot, enable);

	if (0 != slot)
		return -EINVAL;

	enable = !!enable;
	buf[0] = enable;

	ret = tt3650_ci_msg_locked(ca, TT3650_CMD_CI_SET_VIDEO_PORT, buf, 1, 1);
	if (ret < 0)
		return ret;

	if (enable != buf[0]) {
		err("CI not %sabled.", enable ? "en" : "dis");
		return -EIO;
	}

	return 0;
}

static int tt3650_ci_slot_shutdown(struct dvb_ca_en50221 *ca, int slot)
{
	return tt3650_ci_set_video_port(ca, slot, /* enable */ 0);
}

static int tt3650_ci_slot_ts_enable(struct dvb_ca_en50221 *ca, int slot)
{
	return tt3650_ci_set_video_port(ca, slot, /* enable */ 1);
}

static int tt3650_ci_slot_reset(struct dvb_ca_en50221 *ca, int slot)
{
	struct dvb_usb_device *d = (struct dvb_usb_device *)ca->data;
	struct pctv452e_state *state = (struct pctv452e_state *)d->priv;
	u8 buf[1];
	int ret;

	ci_dbg("%s %d", __func__, slot);

	if (0 != slot)
		return -EINVAL;

	buf[0] = 0;

	mutex_lock(&state->ca_mutex);

	ret = tt3650_ci_msg(d, TT3650_CMD_CI_RESET, buf, 1, 1);
	if (0 != ret)
		goto failed;

	msleep(500);

	buf[0] = 1;

	ret = tt3650_ci_msg(d, TT3650_CMD_CI_RESET, buf, 1, 1);
	if (0 != ret)
		goto failed;

	msleep(500);

	buf[0] = 0; /* FTA */

	ret = tt3650_ci_msg(d, TT3650_CMD_CI_SET_VIDEO_PORT, buf, 1, 1);

 failed:
	mutex_unlock(&state->ca_mutex);

	return ret;
}

static int tt3650_ci_poll_slot_status(struct dvb_ca_en50221 *ca,
				 int			slot,
				 int			open)
{
	u8 buf[1];
	int ret;

	if (0 != slot)
		return -EINVAL;

	ret = tt3650_ci_msg_locked(ca, TT3650_CMD_CI_TEST, buf, 0, 1);
	if (0 != ret)
		return ret;

	if (1 == buf[0])
		return DVB_CA_EN50221_POLL_CAM_PRESENT |
			DVB_CA_EN50221_POLL_CAM_READY;

	return 0;

}

static void tt3650_ci_uninit(struct dvb_usb_device *d)
{
	struct pctv452e_state *state;

	ci_dbg("%s", __func__);

	if (NULL == d)
		return;

	state = (struct pctv452e_state *)d->priv;
	if (NULL == state)
		return;

	if (NULL == state->ca.data)
		return;

	/* Error ignored. */
	tt3650_ci_set_video_port(&state->ca, /* slot */ 0, /* enable */ 0);

	dvb_ca_en50221_release(&state->ca);

	memset(&state->ca, 0, sizeof(state->ca));
}

static int tt3650_ci_init(struct dvb_usb_adapter *a)
{
	struct dvb_usb_device *d = a->dev;
	struct pctv452e_state *state = (struct pctv452e_state *)d->priv;
	int ret;

	ci_dbg("%s", __func__);

	mutex_init(&state->ca_mutex);

	state->ca.owner = THIS_MODULE;
	state->ca.read_attribute_mem = tt3650_ci_read_attribute_mem;
	state->ca.write_attribute_mem = tt3650_ci_write_attribute_mem;
	state->ca.read_cam_control = tt3650_ci_read_cam_control;
	state->ca.write_cam_control = tt3650_ci_write_cam_control;
	state->ca.slot_reset = tt3650_ci_slot_reset;
	state->ca.slot_shutdown = tt3650_ci_slot_shutdown;
	state->ca.slot_ts_enable = tt3650_ci_slot_ts_enable;
	state->ca.poll_slot_status = tt3650_ci_poll_slot_status;
	state->ca.data = d;

	ret = dvb_ca_en50221_init(&a->dvb_adap,
				   &state->ca,
				   /* flags */ 0,
				   /* n_slots */ 1);
	if (0 != ret) {
		err("Cannot initialize CI: Error %d.", ret);
		memset(&state->ca, 0, sizeof(state->ca));
		return ret;
	}

	info("CI initialized.");

	return 0;
}

#define CMD_BUFFER_SIZE 0x28
static int pctv452e_i2c_msg(struct dvb_usb_device *d, u8 addr,
				const u8 *snd_buf, u8 snd_len,
				u8 *rcv_buf, u8 rcv_len)
{
	struct pctv452e_state *state = (struct pctv452e_state *)d->priv;
	u8 buf[64];
	u8 id;
	int ret;

	id = state->c++;

	ret = -EINVAL;
	if (snd_len > 64 - 7 || rcv_len > 64 - 7)
		goto failed;

	buf[0] = SYNC_BYTE_OUT;
	buf[1] = id;
	buf[2] = PCTV_CMD_I2C;
	buf[3] = snd_len + 3;
	buf[4] = addr << 1;
	buf[5] = snd_len;
	buf[6] = rcv_len;

	memcpy(buf + 7, snd_buf, snd_len);

	ret = dvb_usb_generic_rw(d, buf, 7 + snd_len,
				  buf, /* rcv_len */ 64,
				  /* delay_ms */ 0);
	if (ret < 0)
		goto failed;

	/* TT USB protocol error. */
	ret = -EIO;
	if (SYNC_BYTE_IN != buf[0] || id != buf[1])
		goto failed;

	/* I2C device didn't respond as expected. */
	ret = -EREMOTEIO;
	if (buf[5] < snd_len || buf[6] < rcv_len)
		goto failed;

	memcpy(rcv_buf, buf + 7, rcv_len);

	return rcv_len;

failed:
	err("I2C error %d; %02X %02X  %02X %02X %02X -> "
	     "%02X %02X  %02X %02X %02X.",
	     ret, SYNC_BYTE_OUT, id, addr << 1, snd_len, rcv_len,
	     buf[0], buf[1], buf[4], buf[5], buf[6]);

	return ret;
}

static int pctv452e_i2c_xfer(struct i2c_adapter *adapter, struct i2c_msg *msg,
				int num)
{
	struct dvb_usb_device *d = i2c_get_adapdata(adapter);
	int i;

	if (mutex_lock_interruptible(&d->i2c_mutex) < 0)
		return -EAGAIN;

	for (i = 0; i < num; i++) {
		u8 addr, snd_len, rcv_len, *snd_buf, *rcv_buf;
		int ret;

		if (msg[i].flags & I2C_M_RD) {
			addr = msg[i].addr;
			snd_buf = NULL;
			snd_len = 0;
			rcv_buf = msg[i].buf;
			rcv_len = msg[i].len;
		} else {
			addr = msg[i].addr;
			snd_buf = msg[i].buf;
			snd_len = msg[i].len;
			rcv_buf = NULL;
			rcv_len = 0;
		}

		ret = pctv452e_i2c_msg(d, addr, snd_buf, snd_len, rcv_buf,
					rcv_len);
		if (ret < rcv_len)
			break;
	}

	mutex_unlock(&d->i2c_mutex);
	return i;
}

static u32 pctv452e_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C;
}

static int pctv452e_power_ctrl(struct dvb_usb_device *d, int i)
{
	struct pctv452e_state *state = (struct pctv452e_state *)d->priv;
	u8 b0[] = { 0xaa, 0, PCTV_CMD_RESET, 1, 0 };
	u8 rx[PCTV_ANSWER_LEN];
	int ret;

	info("%s: %d\n", __func__, i);

	if (!i)
		return 0;

	if (state->initialized)
		return 0;

	/* hmm where shoud this should go? */
	ret = usb_set_interface(d->udev, 0, ISOC_INTERFACE_ALTERNATIVE);
	if (ret != 0)
		info("%s: Warning set interface returned: %d\n",
			__func__, ret);

	/* this is a one-time initialization, dont know where to put */
	b0[1] = state->c++;
	/* reset board */
	ret = dvb_usb_generic_rw(d, b0, sizeof(b0), rx, PCTV_ANSWER_LEN, 0);
	if (ret)
		return ret;

	b0[1] = state->c++;
	b0[4] = 1;
	/* reset board (again?) */
	ret = dvb_usb_generic_rw(d, b0, sizeof(b0), rx, PCTV_ANSWER_LEN, 0);
	if (ret)
		return ret;

	state->initialized = 1;

	return 0;
}

static int pctv452e_rc_query(struct dvb_usb_device *d)
{
	struct pctv452e_state *state = (struct pctv452e_state *)d->priv;
	u8 b[CMD_BUFFER_SIZE];
	u8 rx[PCTV_ANSWER_LEN];
	int ret, i;
	u8 id = state->c++;

	/* prepare command header  */
	b[0] = SYNC_BYTE_OUT;
	b[1] = id;
	b[2] = PCTV_CMD_IR;
	b[3] = 0;

	/* send ir request */
	ret = dvb_usb_generic_rw(d, b, 4, rx, PCTV_ANSWER_LEN, 0);
	if (ret != 0)
		return ret;

	if (debug > 3) {
		info("%s: read: %2d: %02x %02x %02x: ", __func__,
				ret, rx[0], rx[1], rx[2]);
		for (i = 0; (i < rx[3]) && ((i+3) < PCTV_ANSWER_LEN); i++)
			info(" %02x", rx[i+3]);

		info("\n");
	}

	if ((rx[3] == 9) &&  (rx[12] & 0x01)) {
		/* got a "press" event */
		state->last_rc_key = (rx[7] << 8) | rx[6];
		if (debug > 2)
			info("%s: cmd=0x%02x sys=0x%02x\n",
				__func__, rx[6], rx[7]);

		rc_keydown(d->rc_dev, state->last_rc_key, 0);
	} else if (state->last_rc_key) {
		rc_keyup(d->rc_dev);
		state->last_rc_key = 0;
	}

	return 0;
}

static int pctv452e_read_mac_address(struct dvb_usb_device *d, u8 mac[6])
{
	const u8 mem_addr[] = { 0x1f, 0xcc };
	u8 encoded_mac[20];
	int ret;

	ret = -EAGAIN;
	if (mutex_lock_interruptible(&d->i2c_mutex) < 0)
		goto failed;

	ret = pctv452e_i2c_msg(d, I2C_ADDR_24C16,
				mem_addr + 1, /* snd_len */ 1,
				encoded_mac, /* rcv_len */ 20);
	if (-EREMOTEIO == ret)
		/* Caution! A 24C16 interprets 0xA2 0x1F 0xCC as a
		   byte write if /WC is low. */
		ret = pctv452e_i2c_msg(d, I2C_ADDR_24C64,
					mem_addr, 2,
					encoded_mac, 20);

	mutex_unlock(&d->i2c_mutex);

	if (20 != ret)
		goto failed;

	ret = ttpci_eeprom_decode_mac(mac, encoded_mac);
	if (0 != ret)
		goto failed;

	return 0;

failed:
	memset(mac, 0, 6);

	return ret;
}

static const struct stb0899_s1_reg pctv452e_init_dev[] = {
	{ STB0899_DISCNTRL1,	0x26 },
	{ STB0899_DISCNTRL2,	0x80 },
	{ STB0899_DISRX_ST0,	0x04 },
	{ STB0899_DISRX_ST1,	0x20 },
	{ STB0899_DISPARITY,	0x00 },
	{ STB0899_DISFIFO,	0x00 },
	{ STB0899_DISF22,	0x99 },
	{ STB0899_DISF22RX,	0x85 }, /* 0xa8 */
	{ STB0899_ACRPRESC,	0x11 },
	{ STB0899_ACRDIV1,	0x0a },
	{ STB0899_ACRDIV2,	0x05 },
	{ STB0899_DACR1	,	0x00 },
	{ STB0899_DACR2	,	0x00 },
	{ STB0899_OUTCFG,	0x00 },
	{ STB0899_MODECFG,	0x00 }, /* Inversion */
	{ STB0899_IRQMSK_3,	0xf3 },
	{ STB0899_IRQMSK_2,	0xfc },
	{ STB0899_IRQMSK_1,	0xff },
	{ STB0899_IRQMSK_0,	0xff },
	{ STB0899_I2CCFG,	0x88 },
	{ STB0899_I2CRPT,	0x58 },
	{ STB0899_GPIO00CFG,	0x82 },
	{ STB0899_GPIO01CFG,	0x82 }, /* LED: 0x02 green, 0x82 orange */
	{ STB0899_GPIO02CFG,	0x82 },
	{ STB0899_GPIO03CFG,	0x82 },
	{ STB0899_GPIO04CFG,	0x82 },
	{ STB0899_GPIO05CFG,	0x82 },
	{ STB0899_GPIO06CFG,	0x82 },
	{ STB0899_GPIO07CFG,	0x82 },
	{ STB0899_GPIO08CFG,	0x82 },
	{ STB0899_GPIO09CFG,	0x82 },
	{ STB0899_GPIO10CFG,	0x82 },
	{ STB0899_GPIO11CFG,	0x82 },
	{ STB0899_GPIO12CFG,	0x82 },
	{ STB0899_GPIO13CFG,	0x82 },
	{ STB0899_GPIO14CFG,	0x82 },
	{ STB0899_GPIO15CFG,	0x82 },
	{ STB0899_GPIO16CFG,	0x82 },
	{ STB0899_GPIO17CFG,	0x82 },
	{ STB0899_GPIO18CFG,	0x82 },
	{ STB0899_GPIO19CFG,	0x82 },
	{ STB0899_GPIO20CFG,	0x82 },
	{ STB0899_SDATCFG,	0xb8 },
	{ STB0899_SCLTCFG,	0xba },
	{ STB0899_AGCRFCFG,	0x1c }, /* 0x11 DVB-S; 0x1c DVB-S2 (1c, rjkm) */
	{ STB0899_GPIO22,	0x82 },
	{ STB0899_GPIO21,	0x91 },
	{ STB0899_DIRCLKCFG,	0x82 },
	{ STB0899_CLKOUT27CFG,	0x7e },
	{ STB0899_STDBYCFG,	0x82 },
	{ STB0899_CS0CFG,	0x82 },
	{ STB0899_CS1CFG,	0x82 },
	{ STB0899_DISEQCOCFG,	0x20 },
	{ STB0899_NCOARSE,	0x15 }, /* 0x15 27Mhz, F/3 198MHz, F/6 108MHz */
	{ STB0899_SYNTCTRL,	0x00 }, /* 0x00 CLKI, 0x02 XTALI */
	{ STB0899_FILTCTRL,	0x00 },
	{ STB0899_SYSCTRL,	0x00 },
	{ STB0899_STOPCLK1,	0x20 }, /* orig: 0x00 budget-ci: 0x20 */
	{ STB0899_STOPCLK2,	0x00 },
	{ STB0899_INTBUFCTRL,	0x0a },
	{ STB0899_AGC2I1,	0x00 },
	{ STB0899_AGC2I2,	0x00 },
	{ STB0899_AGCIQIN,	0x00 },
	{ STB0899_TSTRES,	0x40 }, /* rjkm */
	{ 0xffff,		0xff },
};

static const struct stb0899_s1_reg pctv452e_init_s1_demod[] = {
	{ STB0899_DEMOD,	0x00 },
	{ STB0899_RCOMPC,	0xc9 },
	{ STB0899_AGC1CN,	0x01 },
	{ STB0899_AGC1REF,	0x10 },
	{ STB0899_RTC,		0x23 },
	{ STB0899_TMGCFG,	0x4e },
	{ STB0899_AGC2REF,	0x34 },
	{ STB0899_TLSR,		0x84 },
	{ STB0899_CFD,		0xf7 },
	{ STB0899_ACLC,		0x87 },
	{ STB0899_BCLC,		0x94 },
	{ STB0899_EQON,		0x41 },
	{ STB0899_LDT,		0xf1 },
	{ STB0899_LDT2,		0xe3 },
	{ STB0899_EQUALREF,	0xb4 },
	{ STB0899_TMGRAMP,	0x10 },
	{ STB0899_TMGTHD,	0x30 },
	{ STB0899_IDCCOMP,	0xfd },
	{ STB0899_QDCCOMP,	0xff },
	{ STB0899_POWERI,	0x0c },
	{ STB0899_POWERQ,	0x0f },
	{ STB0899_RCOMP,	0x6c },
	{ STB0899_AGCIQIN,	0x80 },
	{ STB0899_AGC2I1,	0x06 },
	{ STB0899_AGC2I2,	0x00 },
	{ STB0899_TLIR,		0x30 },
	{ STB0899_RTF,		0x7f },
	{ STB0899_DSTATUS,	0x00 },
	{ STB0899_LDI,		0xbc },
	{ STB0899_CFRM,		0xea },
	{ STB0899_CFRL,		0x31 },
	{ STB0899_NIRM,		0x2b },
	{ STB0899_NIRL,		0x80 },
	{ STB0899_ISYMB,	0x1d },
	{ STB0899_QSYMB,	0xa6 },
	{ STB0899_SFRH,		0x2f },
	{ STB0899_SFRM,		0x68 },
	{ STB0899_SFRL,		0x40 },
	{ STB0899_SFRUPH,	0x2f },
	{ STB0899_SFRUPM,	0x68 },
	{ STB0899_SFRUPL,	0x40 },
	{ STB0899_EQUAI1,	0x02 },
	{ STB0899_EQUAQ1,	0xff },
	{ STB0899_EQUAI2,	0x04 },
	{ STB0899_EQUAQ2,	0x05 },
	{ STB0899_EQUAI3,	0x02 },
	{ STB0899_EQUAQ3,	0xfd },
	{ STB0899_EQUAI4,	0x03 },
	{ STB0899_EQUAQ4,	0x07 },
	{ STB0899_EQUAI5,	0x08 },
	{ STB0899_EQUAQ5,	0xf5 },
	{ STB0899_DSTATUS2,	0x00 },
	{ STB0899_VSTATUS,	0x00 },
	{ STB0899_VERROR,	0x86 },
	{ STB0899_IQSWAP,	0x2a },
	{ STB0899_ECNT1M,	0x00 },
	{ STB0899_ECNT1L,	0x00 },
	{ STB0899_ECNT2M,	0x00 },
	{ STB0899_ECNT2L,	0x00 },
	{ STB0899_ECNT3M,	0x0a },
	{ STB0899_ECNT3L,	0xad },
	{ STB0899_FECAUTO1,	0x06 },
	{ STB0899_FECM,		0x01 },
	{ STB0899_VTH12,	0xb0 },
	{ STB0899_VTH23,	0x7a },
	{ STB0899_VTH34,	0x58 },
	{ STB0899_VTH56,	0x38 },
	{ STB0899_VTH67,	0x34 },
	{ STB0899_VTH78,	0x24 },
	{ STB0899_PRVIT,	0xff },
	{ STB0899_VITSYNC,	0x19 },
	{ STB0899_RSULC,	0xb1 }, /* DVB = 0xb1, DSS = 0xa1 */
	{ STB0899_TSULC,	0x42 },
	{ STB0899_RSLLC,	0x41 },
	{ STB0899_TSLPL,	0x12 },
	{ STB0899_TSCFGH,	0x0c },
	{ STB0899_TSCFGM,	0x00 },
	{ STB0899_TSCFGL,	0x00 },
	{ STB0899_TSOUT,	0x69 }, /* 0x0d for CAM */
	{ STB0899_RSSYNCDEL,	0x00 },
	{ STB0899_TSINHDELH,	0x02 },
	{ STB0899_TSINHDELM,	0x00 },
	{ STB0899_TSINHDELL,	0x00 },
	{ STB0899_TSLLSTKM,	0x1b },
	{ STB0899_TSLLSTKL,	0xb3 },
	{ STB0899_TSULSTKM,	0x00 },
	{ STB0899_TSULSTKL,	0x00 },
	{ STB0899_PCKLENUL,	0xbc },
	{ STB0899_PCKLENLL,	0xcc },
	{ STB0899_RSPCKLEN,	0xbd },
	{ STB0899_TSSTATUS,	0x90 },
	{ STB0899_ERRCTRL1,	0xb6 },
	{ STB0899_ERRCTRL2,	0x95 },
	{ STB0899_ERRCTRL3,	0x8d },
	{ STB0899_DMONMSK1,	0x27 },
	{ STB0899_DMONMSK0,	0x03 },
	{ STB0899_DEMAPVIT,	0x5c },
	{ STB0899_PLPARM,	0x19 },
	{ STB0899_PDELCTRL,	0x48 },
	{ STB0899_PDELCTRL2,	0x00 },
	{ STB0899_BBHCTRL1,	0x00 },
	{ STB0899_BBHCTRL2,	0x00 },
	{ STB0899_HYSTTHRESH,	0x77 },
	{ STB0899_MATCSTM,	0x00 },
	{ STB0899_MATCSTL,	0x00 },
	{ STB0899_UPLCSTM,	0x00 },
	{ STB0899_UPLCSTL,	0x00 },
	{ STB0899_DFLCSTM,	0x00 },
	{ STB0899_DFLCSTL,	0x00 },
	{ STB0899_SYNCCST,	0x00 },
	{ STB0899_SYNCDCSTM,	0x00 },
	{ STB0899_SYNCDCSTL,	0x00 },
	{ STB0899_ISI_ENTRY,	0x00 },
	{ STB0899_ISI_BIT_EN,	0x00 },
	{ STB0899_MATSTRM,	0xf0 },
	{ STB0899_MATSTRL,	0x02 },
	{ STB0899_UPLSTRM,	0x45 },
	{ STB0899_UPLSTRL,	0x60 },
	{ STB0899_DFLSTRM,	0xe3 },
	{ STB0899_DFLSTRL,	0x00 },
	{ STB0899_SYNCSTR,	0x47 },
	{ STB0899_SYNCDSTRM,	0x05 },
	{ STB0899_SYNCDSTRL,	0x18 },
	{ STB0899_CFGPDELSTATUS1, 0x19 },
	{ STB0899_CFGPDELSTATUS2, 0x2b },
	{ STB0899_BBFERRORM,	0x00 },
	{ STB0899_BBFERRORL,	0x01 },
	{ STB0899_UPKTERRORM,	0x00 },
	{ STB0899_UPKTERRORL,	0x00 },
	{ 0xffff,		0xff },
};

static struct stb0899_config stb0899_config = {
	.init_dev	= pctv452e_init_dev,
	.init_s2_demod	= stb0899_s2_init_2,
	.init_s1_demod	= pctv452e_init_s1_demod,
	.init_s2_fec	= stb0899_s2_init_4,
	.init_tst	= stb0899_s1_init_5,

	.demod_address   = I2C_ADDR_STB0899, /* I2C Address */
	.block_sync_mode = STB0899_SYNC_FORCED, /* ? */

	.xtal_freq       = 27000000,	 /* Assume Hz ? */
	.inversion       = IQ_SWAP_ON,       /* ? */

	.lo_clk	  = 76500000,
	.hi_clk	  = 99000000,

	.ts_output_mode  = 0,	/* Use parallel mode */
	.clock_polarity  = 0,
	.data_clk_parity = 0,
	.fec_mode	= 0,

	.esno_ave	    = STB0899_DVBS2_ESNO_AVE,
	.esno_quant	  = STB0899_DVBS2_ESNO_QUANT,
	.avframes_coarse     = STB0899_DVBS2_AVFRAMES_COARSE,
	.avframes_fine       = STB0899_DVBS2_AVFRAMES_FINE,
	.miss_threshold      = STB0899_DVBS2_MISS_THRESHOLD,
	.uwp_threshold_acq   = STB0899_DVBS2_UWP_THRESHOLD_ACQ,
	.uwp_threshold_track = STB0899_DVBS2_UWP_THRESHOLD_TRACK,
	.uwp_threshold_sof   = STB0899_DVBS2_UWP_THRESHOLD_SOF,
	.sof_search_timeout  = STB0899_DVBS2_SOF_SEARCH_TIMEOUT,

	.btr_nco_bits	  = STB0899_DVBS2_BTR_NCO_BITS,
	.btr_gain_shift_offset = STB0899_DVBS2_BTR_GAIN_SHIFT_OFFSET,
	.crl_nco_bits	  = STB0899_DVBS2_CRL_NCO_BITS,
	.ldpc_max_iter	 = STB0899_DVBS2_LDPC_MAX_ITER,

	.tuner_get_frequency	= stb6100_get_frequency,
	.tuner_set_frequency	= stb6100_set_frequency,
	.tuner_set_bandwidth	= stb6100_set_bandwidth,
	.tuner_get_bandwidth	= stb6100_get_bandwidth,
	.tuner_set_rfsiggain	= NULL,

	/* helper for switching LED green/orange */
	.postproc = pctv45e_postproc
};

static struct stb6100_config stb6100_config = {
	.tuner_address = I2C_ADDR_STB6100,
	.refclock      = 27000000
};


static struct i2c_algorithm pctv452e_i2c_algo = {
	.master_xfer   = pctv452e_i2c_xfer,
	.functionality = pctv452e_i2c_func
};

static int pctv452e_frontend_attach(struct dvb_usb_adapter *a)
{
	struct usb_device_id *id;

	a->fe_adap[0].fe = dvb_attach(stb0899_attach, &stb0899_config,
						&a->dev->i2c_adap);
	if (!a->fe_adap[0].fe)
		return -ENODEV;
	if ((dvb_attach(lnbp22_attach, a->fe_adap[0].fe,
					&a->dev->i2c_adap)) == 0)
		err("Cannot attach lnbp22\n");

	id = a->dev->desc->warm_ids[0];
	if (USB_VID_TECHNOTREND == id->idVendor
	    && USB_PID_TECHNOTREND_CONNECT_S2_3650_CI == id->idProduct)
		/* Error ignored. */
		tt3650_ci_init(a);

	return 0;
}

static int pctv452e_tuner_attach(struct dvb_usb_adapter *a)
{
	if (!a->fe_adap[0].fe)
		return -ENODEV;
	if (dvb_attach(stb6100_attach, a->fe_adap[0].fe, &stb6100_config,
					&a->dev->i2c_adap) == 0) {
		err("%s failed\n", __func__);
		return -ENODEV;
	}

	return 0;
}

static struct usb_device_id pctv452e_usb_table[] = {
	{USB_DEVICE(USB_VID_PINNACLE, USB_PID_PCTV_452E)},
	{USB_DEVICE(USB_VID_TECHNOTREND, USB_PID_TECHNOTREND_CONNECT_S2_3600)},
	{USB_DEVICE(USB_VID_TECHNOTREND,
				USB_PID_TECHNOTREND_CONNECT_S2_3650_CI)},
	{}
};
MODULE_DEVICE_TABLE(usb, pctv452e_usb_table);

static struct dvb_usb_device_properties pctv452e_properties = {
	.caps = DVB_USB_IS_AN_I2C_ADAPTER, /* more ? */
	.usb_ctrl = DEVICE_SPECIFIC,

	.size_of_priv     = sizeof(struct pctv452e_state),

	.power_ctrl       = pctv452e_power_ctrl,

	.rc.core = {
		.rc_codes	= RC_MAP_DIB0700_RC5_TABLE,
		.allowed_protos	= RC_TYPE_UNKNOWN,
		.rc_query	= pctv452e_rc_query,
		.rc_interval	= 100,
	},

	.num_adapters     = 1,
	.adapter = {{
		.num_frontends = 1,
		.fe = {{
			.frontend_attach  = pctv452e_frontend_attach,
			.tuner_attach     = pctv452e_tuner_attach,

			/* parameter for the MPEG2-data transfer */
			.stream = {
				.type     = USB_ISOC,
				.count    = 4,
				.endpoint = 0x02,
				.u = {
					.isoc = {
						.framesperurb = 4,
						.framesize    = 940,
						.interval     = 1
					}
				}
			},
		} },
	} },

	.i2c_algo = &pctv452e_i2c_algo,

	.generic_bulk_ctrl_endpoint = 1, /* allow generice rw function */

	.num_device_descs = 1,
	.devices = {
		{ .name = "PCTV HDTV USB",
		  .cold_ids = { NULL, NULL }, /* this is a warm only device */
		  .warm_ids = { &pctv452e_usb_table[0], NULL }
		},
		{ 0 },
	}
};

static struct dvb_usb_device_properties tt_connect_s2_3600_properties = {
	.caps = DVB_USB_IS_AN_I2C_ADAPTER, /* more ? */
	.usb_ctrl = DEVICE_SPECIFIC,

	.size_of_priv		= sizeof(struct pctv452e_state),

	.power_ctrl		= pctv452e_power_ctrl,
	.read_mac_address	= pctv452e_read_mac_address,

	.rc.core = {
		.rc_codes	= RC_MAP_TT_1500,
		.allowed_protos	= RC_TYPE_UNKNOWN,
		.rc_query	= pctv452e_rc_query,
		.rc_interval	= 100,
	},

	.num_adapters		= 1,
	.adapter = {{
		.num_frontends = 1,
		.fe = {{
			.frontend_attach = pctv452e_frontend_attach,
			.tuner_attach = pctv452e_tuner_attach,

			/* parameter for the MPEG2-data transfer */
			.stream = {
				.type = USB_ISOC,
				.count = 7,
				.endpoint = 0x02,
				.u = {
					.isoc = {
						.framesperurb = 4,
						.framesize = 940,
						.interval = 1
					}
				}
			},

		} },
	} },

	.i2c_algo = &pctv452e_i2c_algo,

	.generic_bulk_ctrl_endpoint = 1, /* allow generic rw function*/

	.num_device_descs = 2,
	.devices = {
		{ .name = "Technotrend TT Connect S2-3600",
		  .cold_ids = { NULL, NULL }, /* this is a warm only device */
		  .warm_ids = { &pctv452e_usb_table[1], NULL }
		},
		{ .name = "Technotrend TT Connect S2-3650-CI",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &pctv452e_usb_table[2], NULL }
		},
		{ 0 },
	}
};

static void pctv452e_usb_disconnect(struct usb_interface *intf)
{
	struct dvb_usb_device *d = usb_get_intfdata(intf);

	tt3650_ci_uninit(d);
	dvb_usb_device_exit(intf);
}

static int pctv452e_usb_probe(struct usb_interface *intf,
				const struct usb_device_id *id)
{
	if (0 == dvb_usb_device_init(intf, &pctv452e_properties,
					THIS_MODULE, NULL, adapter_nr) ||
	    0 == dvb_usb_device_init(intf, &tt_connect_s2_3600_properties,
					THIS_MODULE, NULL, adapter_nr))
		return 0;

	return -ENODEV;
}

static struct usb_driver pctv452e_usb_driver = {
	.name       = "pctv452e",
	.probe      = pctv452e_usb_probe,
	.disconnect = pctv452e_usb_disconnect,
	.id_table   = pctv452e_usb_table,
};

module_usb_driver(pctv452e_usb_driver);

MODULE_AUTHOR("Dominik Kuhlen <dkuhlen@gmx.net>");
MODULE_AUTHOR("Andre Weidemann <Andre.Weidemann@web.de>");
MODULE_AUTHOR("Michael H. Schimek <mschimek@gmx.at>");
MODULE_DESCRIPTION("Pinnacle PCTV HDTV USB DVB / TT connect S2-3600 Driver");
MODULE_LICENSE("GPL");
