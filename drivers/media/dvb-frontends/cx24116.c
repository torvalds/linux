/*
    Conexant cx24116/cx24118 - DVBS/S2 Satellite demod/tuner driver

    Copyright (C) 2006-2008 Steven Toth <stoth@hauppauge.com>
    Copyright (C) 2006-2007 Georg Acher
    Copyright (C) 2007-2008 Darron Broad
	March 2007
	    Fixed some bugs.
	    Added diseqc support.
	    Added corrected signal strength support.
	August 2007
	    Sync with legacy version.
	    Some clean ups.
    Copyright (C) 2008 Igor Liplianin
	September, 9th 2008
	    Fixed locking on high symbol rates (>30000).
	    Implement MPEG initialization parameter.
	January, 17th 2009
	    Fill set_voltage with actually control voltage code.
	    Correct set tone to not affect voltage.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/firmware.h>

#include "dvb_frontend.h"
#include "cx24116.h"

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Activates frontend debugging (default:0)");

#define dprintk(args...) \
	do { \
		if (debug) \
			printk(KERN_INFO "cx24116: " args); \
	} while (0)

#define CX24116_DEFAULT_FIRMWARE "dvb-fe-cx24116.fw"
#define CX24116_SEARCH_RANGE_KHZ 5000

/* known registers */
#define CX24116_REG_COMMAND (0x00)      /* command args 0x00..0x1e */
#define CX24116_REG_EXECUTE (0x1f)      /* execute command */
#define CX24116_REG_MAILBOX (0x96)      /* FW or multipurpose mailbox? */
#define CX24116_REG_RESET   (0x20)      /* reset status > 0     */
#define CX24116_REG_SIGNAL  (0x9e)      /* signal low           */
#define CX24116_REG_SSTATUS (0x9d)      /* signal high / status */
#define CX24116_REG_QUALITY8 (0xa3)
#define CX24116_REG_QSTATUS (0xbc)
#define CX24116_REG_QUALITY0 (0xd5)
#define CX24116_REG_BER0    (0xc9)
#define CX24116_REG_BER8    (0xc8)
#define CX24116_REG_BER16   (0xc7)
#define CX24116_REG_BER24   (0xc6)
#define CX24116_REG_UCB0    (0xcb)
#define CX24116_REG_UCB8    (0xca)
#define CX24116_REG_CLKDIV  (0xf3)
#define CX24116_REG_RATEDIV (0xf9)

/* configured fec (not tuned) or actual FEC (tuned) 1=1/2 2=2/3 etc */
#define CX24116_REG_FECSTATUS (0x9c)

/* FECSTATUS bits */
/* mask to determine configured fec (not tuned) or actual fec (tuned) */
#define CX24116_FEC_FECMASK   (0x1f)

/* Select DVB-S demodulator, else DVB-S2 */
#define CX24116_FEC_DVBS      (0x20)
#define CX24116_FEC_UNKNOWN   (0x40)    /* Unknown/unused */

/* Pilot mode requested when tuning else always reset when tuned */
#define CX24116_FEC_PILOT     (0x80)

/* arg buffer size */
#define CX24116_ARGLEN (0x1e)

/* rolloff */
#define CX24116_ROLLOFF_020 (0x00)
#define CX24116_ROLLOFF_025 (0x01)
#define CX24116_ROLLOFF_035 (0x02)

/* pilot bit */
#define CX24116_PILOT_OFF (0x00)
#define CX24116_PILOT_ON (0x40)

/* signal status */
#define CX24116_HAS_SIGNAL   (0x01)
#define CX24116_HAS_CARRIER  (0x02)
#define CX24116_HAS_VITERBI  (0x04)
#define CX24116_HAS_SYNCLOCK (0x08)
#define CX24116_HAS_UNKNOWN1 (0x10)
#define CX24116_HAS_UNKNOWN2 (0x20)
#define CX24116_STATUS_MASK  (0x0f)
#define CX24116_SIGNAL_MASK  (0xc0)

#define CX24116_DISEQC_TONEOFF   (0)    /* toneburst never sent */
#define CX24116_DISEQC_TONECACHE (1)    /* toneburst cached     */
#define CX24116_DISEQC_MESGCACHE (2)    /* message cached       */

/* arg offset for DiSEqC */
#define CX24116_DISEQC_BURST  (1)
#define CX24116_DISEQC_ARG2_2 (2)   /* unknown value=2 */
#define CX24116_DISEQC_ARG3_0 (3)   /* unknown value=0 */
#define CX24116_DISEQC_ARG4_0 (4)   /* unknown value=0 */
#define CX24116_DISEQC_MSGLEN (5)
#define CX24116_DISEQC_MSGOFS (6)

/* DiSEqC burst */
#define CX24116_DISEQC_MINI_A (0)
#define CX24116_DISEQC_MINI_B (1)

/* DiSEqC tone burst */
static int toneburst = 1;
module_param(toneburst, int, 0644);
MODULE_PARM_DESC(toneburst, "DiSEqC toneburst 0=OFF, 1=TONE CACHE, "\
	"2=MESSAGE CACHE (default:1)");

/* SNR measurements */
static int esno_snr;
module_param(esno_snr, int, 0644);
MODULE_PARM_DESC(esno_snr, "SNR return units, 0=PERCENTAGE 0-100, "\
	"1=ESNO(db * 10) (default:0)");

enum cmds {
	CMD_SET_VCO     = 0x10,
	CMD_TUNEREQUEST = 0x11,
	CMD_MPEGCONFIG  = 0x13,
	CMD_TUNERINIT   = 0x14,
	CMD_BANDWIDTH   = 0x15,
	CMD_GETAGC      = 0x19,
	CMD_LNBCONFIG   = 0x20,
	CMD_LNBSEND     = 0x21, /* Formerly CMD_SEND_DISEQC */
	CMD_LNBDCLEVEL  = 0x22,
	CMD_SET_TONE    = 0x23,
	CMD_UPDFWVERS   = 0x35,
	CMD_TUNERSLEEP  = 0x36,
	CMD_AGCCONTROL  = 0x3b, /* Unknown */
};

/* The Demod/Tuner can't easily provide these, we cache them */
struct cx24116_tuning {
	u32 frequency;
	u32 symbol_rate;
	enum fe_spectral_inversion inversion;
	enum fe_code_rate fec;

	enum fe_delivery_system delsys;
	enum fe_modulation modulation;
	enum fe_pilot pilot;
	enum fe_rolloff rolloff;

	/* Demod values */
	u8 fec_val;
	u8 fec_mask;
	u8 inversion_val;
	u8 pilot_val;
	u8 rolloff_val;
};

/* Basic commands that are sent to the firmware */
struct cx24116_cmd {
	u8 len;
	u8 args[CX24116_ARGLEN];
};

struct cx24116_state {
	struct i2c_adapter *i2c;
	const struct cx24116_config *config;

	struct dvb_frontend frontend;

	struct cx24116_tuning dcur;
	struct cx24116_tuning dnxt;

	u8 skip_fw_load;
	u8 burst;
	struct cx24116_cmd dsec_cmd;
};

static int cx24116_writereg(struct cx24116_state *state, int reg, int data)
{
	u8 buf[] = { reg, data };
	struct i2c_msg msg = { .addr = state->config->demod_address,
		.flags = 0, .buf = buf, .len = 2 };
	int err;

	if (debug > 1)
		printk("cx24116: %s: write reg 0x%02x, value 0x%02x\n",
			__func__, reg, data);

	err = i2c_transfer(state->i2c, &msg, 1);
	if (err != 1) {
		printk(KERN_ERR "%s: writereg error(err == %i, reg == 0x%02x, value == 0x%02x)\n",
		       __func__, err, reg, data);
		return -EREMOTEIO;
	}

	return 0;
}

/* Bulk byte writes to a single I2C address, for 32k firmware load */
static int cx24116_writeregN(struct cx24116_state *state, int reg,
			     const u8 *data, u16 len)
{
	int ret = -EREMOTEIO;
	struct i2c_msg msg;
	u8 *buf;

	buf = kmalloc(len + 1, GFP_KERNEL);
	if (buf == NULL) {
		printk("Unable to kmalloc\n");
		ret = -ENOMEM;
		goto error;
	}

	*(buf) = reg;
	memcpy(buf + 1, data, len);

	msg.addr = state->config->demod_address;
	msg.flags = 0;
	msg.buf = buf;
	msg.len = len + 1;

	if (debug > 1)
		printk(KERN_INFO "cx24116: %s:  write regN 0x%02x, len = %d\n",
			__func__, reg, len);

	ret = i2c_transfer(state->i2c, &msg, 1);
	if (ret != 1) {
		printk(KERN_ERR "%s: writereg error(err == %i, reg == 0x%02x\n",
			 __func__, ret, reg);
		ret = -EREMOTEIO;
	}

error:
	kfree(buf);

	return ret;
}

static int cx24116_readreg(struct cx24116_state *state, u8 reg)
{
	int ret;
	u8 b0[] = { reg };
	u8 b1[] = { 0 };
	struct i2c_msg msg[] = {
		{ .addr = state->config->demod_address, .flags = 0,
			.buf = b0, .len = 1 },
		{ .addr = state->config->demod_address, .flags = I2C_M_RD,
			.buf = b1, .len = 1 }
	};

	ret = i2c_transfer(state->i2c, msg, 2);

	if (ret != 2) {
		printk(KERN_ERR "%s: reg=0x%x (error=%d)\n",
			__func__, reg, ret);
		return ret;
	}

	if (debug > 1)
		printk(KERN_INFO "cx24116: read reg 0x%02x, value 0x%02x\n",
			reg, b1[0]);

	return b1[0];
}

static int cx24116_set_inversion(struct cx24116_state *state,
	enum fe_spectral_inversion inversion)
{
	dprintk("%s(%d)\n", __func__, inversion);

	switch (inversion) {
	case INVERSION_OFF:
		state->dnxt.inversion_val = 0x00;
		break;
	case INVERSION_ON:
		state->dnxt.inversion_val = 0x04;
		break;
	case INVERSION_AUTO:
		state->dnxt.inversion_val = 0x0C;
		break;
	default:
		return -EINVAL;
	}

	state->dnxt.inversion = inversion;

	return 0;
}

/*
 * modfec (modulation and FEC)
 * ===========================
 *
 * MOD          FEC             mask/val    standard
 * ----         --------        ----------- --------
 * QPSK         FEC_1_2         0x02 0x02+X DVB-S
 * QPSK         FEC_2_3         0x04 0x02+X DVB-S
 * QPSK         FEC_3_4         0x08 0x02+X DVB-S
 * QPSK         FEC_4_5         0x10 0x02+X DVB-S (?)
 * QPSK         FEC_5_6         0x20 0x02+X DVB-S
 * QPSK         FEC_6_7         0x40 0x02+X DVB-S
 * QPSK         FEC_7_8         0x80 0x02+X DVB-S
 * QPSK         FEC_8_9         0x01 0x02+X DVB-S (?) (NOT SUPPORTED?)
 * QPSK         AUTO            0xff 0x02+X DVB-S
 *
 * For DVB-S high byte probably represents FEC
 * and low byte selects the modulator. The high
 * byte is search range mask. Bit 5 may turn
 * on DVB-S and remaining bits represent some
 * kind of calibration (how/what i do not know).
 *
 * Eg.(2/3) szap "Zone Horror"
 *
 * mask/val = 0x04, 0x20
 * status 1f | signal c3c0 | snr a333 | ber 00000098 | unc 0 | FE_HAS_LOCK
 *
 * mask/val = 0x04, 0x30
 * status 1f | signal c3c0 | snr a333 | ber 00000000 | unc 0 | FE_HAS_LOCK
 *
 * After tuning FECSTATUS contains actual FEC
 * in use numbered 1 through to 8 for 1/2 .. 2/3 etc
 *
 * NBC=NOT/NON BACKWARD COMPATIBLE WITH DVB-S (DVB-S2 only)
 *
 * NBC-QPSK     FEC_1_2         0x00, 0x04      DVB-S2
 * NBC-QPSK     FEC_3_5         0x00, 0x05      DVB-S2
 * NBC-QPSK     FEC_2_3         0x00, 0x06      DVB-S2
 * NBC-QPSK     FEC_3_4         0x00, 0x07      DVB-S2
 * NBC-QPSK     FEC_4_5         0x00, 0x08      DVB-S2
 * NBC-QPSK     FEC_5_6         0x00, 0x09      DVB-S2
 * NBC-QPSK     FEC_8_9         0x00, 0x0a      DVB-S2
 * NBC-QPSK     FEC_9_10        0x00, 0x0b      DVB-S2
 *
 * NBC-8PSK     FEC_3_5         0x00, 0x0c      DVB-S2
 * NBC-8PSK     FEC_2_3         0x00, 0x0d      DVB-S2
 * NBC-8PSK     FEC_3_4         0x00, 0x0e      DVB-S2
 * NBC-8PSK     FEC_5_6         0x00, 0x0f      DVB-S2
 * NBC-8PSK     FEC_8_9         0x00, 0x10      DVB-S2
 * NBC-8PSK     FEC_9_10        0x00, 0x11      DVB-S2
 *
 * For DVB-S2 low bytes selects both modulator
 * and FEC. High byte is meaningless here. To
 * set pilot, bit 6 (0x40) is set. When inspecting
 * FECSTATUS bit 7 (0x80) represents the pilot
 * selection whilst not tuned. When tuned, actual FEC
 * in use is found in FECSTATUS as per above. Pilot
 * value is reset.
 */

/* A table of modulation, fec and configuration bytes for the demod.
 * Not all S2 mmodulation schemes are support and not all rates with
 * a scheme are support. Especially, no auto detect when in S2 mode.
 */
static struct cx24116_modfec {
	enum fe_delivery_system delivery_system;
	enum fe_modulation modulation;
	enum fe_code_rate fec;
	u8 mask;	/* In DVBS mode this is used to autodetect */
	u8 val;		/* Passed to the firmware to indicate mode selection */
} CX24116_MODFEC_MODES[] = {
 /* QPSK. For unknown rates we set hardware to auto detect 0xfe 0x30 */

 /*mod   fec       mask  val */
 { SYS_DVBS, QPSK, FEC_NONE, 0xfe, 0x30 },
 { SYS_DVBS, QPSK, FEC_1_2,  0x02, 0x2e }, /* 00000010 00101110 */
 { SYS_DVBS, QPSK, FEC_2_3,  0x04, 0x2f }, /* 00000100 00101111 */
 { SYS_DVBS, QPSK, FEC_3_4,  0x08, 0x30 }, /* 00001000 00110000 */
 { SYS_DVBS, QPSK, FEC_4_5,  0xfe, 0x30 }, /* 000?0000 ?        */
 { SYS_DVBS, QPSK, FEC_5_6,  0x20, 0x31 }, /* 00100000 00110001 */
 { SYS_DVBS, QPSK, FEC_6_7,  0xfe, 0x30 }, /* 0?000000 ?        */
 { SYS_DVBS, QPSK, FEC_7_8,  0x80, 0x32 }, /* 10000000 00110010 */
 { SYS_DVBS, QPSK, FEC_8_9,  0xfe, 0x30 }, /* 0000000? ?        */
 { SYS_DVBS, QPSK, FEC_AUTO, 0xfe, 0x30 },
 /* NBC-QPSK */
 { SYS_DVBS2, QPSK, FEC_1_2,  0x00, 0x04 },
 { SYS_DVBS2, QPSK, FEC_3_5,  0x00, 0x05 },
 { SYS_DVBS2, QPSK, FEC_2_3,  0x00, 0x06 },
 { SYS_DVBS2, QPSK, FEC_3_4,  0x00, 0x07 },
 { SYS_DVBS2, QPSK, FEC_4_5,  0x00, 0x08 },
 { SYS_DVBS2, QPSK, FEC_5_6,  0x00, 0x09 },
 { SYS_DVBS2, QPSK, FEC_8_9,  0x00, 0x0a },
 { SYS_DVBS2, QPSK, FEC_9_10, 0x00, 0x0b },
 /* 8PSK */
 { SYS_DVBS2, PSK_8, FEC_3_5,  0x00, 0x0c },
 { SYS_DVBS2, PSK_8, FEC_2_3,  0x00, 0x0d },
 { SYS_DVBS2, PSK_8, FEC_3_4,  0x00, 0x0e },
 { SYS_DVBS2, PSK_8, FEC_5_6,  0x00, 0x0f },
 { SYS_DVBS2, PSK_8, FEC_8_9,  0x00, 0x10 },
 { SYS_DVBS2, PSK_8, FEC_9_10, 0x00, 0x11 },
 /*
  * `val' can be found in the FECSTATUS register when tuning.
  * FECSTATUS will give the actual FEC in use if tuning was successful.
  */
};

static int cx24116_lookup_fecmod(struct cx24116_state *state,
	enum fe_delivery_system d, enum fe_modulation m, enum fe_code_rate f)
{
	int i, ret = -EOPNOTSUPP;

	dprintk("%s(0x%02x,0x%02x)\n", __func__, m, f);

	for (i = 0; i < ARRAY_SIZE(CX24116_MODFEC_MODES); i++) {
		if ((d == CX24116_MODFEC_MODES[i].delivery_system) &&
			(m == CX24116_MODFEC_MODES[i].modulation) &&
			(f == CX24116_MODFEC_MODES[i].fec)) {
				ret = i;
				break;
			}
	}

	return ret;
}

static int cx24116_set_fec(struct cx24116_state *state,
			   enum fe_delivery_system delsys,
			   enum fe_modulation mod,
			   enum fe_code_rate fec)
{
	int ret = 0;

	dprintk("%s(0x%02x,0x%02x)\n", __func__, mod, fec);

	ret = cx24116_lookup_fecmod(state, delsys, mod, fec);

	if (ret < 0)
		return ret;

	state->dnxt.fec = fec;
	state->dnxt.fec_val = CX24116_MODFEC_MODES[ret].val;
	state->dnxt.fec_mask = CX24116_MODFEC_MODES[ret].mask;
	dprintk("%s() mask/val = 0x%02x/0x%02x\n", __func__,
		state->dnxt.fec_mask, state->dnxt.fec_val);

	return 0;
}

static int cx24116_set_symbolrate(struct cx24116_state *state, u32 rate)
{
	dprintk("%s(%d)\n", __func__, rate);

	/*  check if symbol rate is within limits */
	if ((rate > state->frontend.ops.info.symbol_rate_max) ||
	    (rate < state->frontend.ops.info.symbol_rate_min)) {
		dprintk("%s() unsupported symbol_rate = %d\n", __func__, rate);
		return -EOPNOTSUPP;
	}

	state->dnxt.symbol_rate = rate;
	dprintk("%s() symbol_rate = %d\n", __func__, rate);

	return 0;
}

static int cx24116_load_firmware(struct dvb_frontend *fe,
	const struct firmware *fw);

static int cx24116_firmware_ondemand(struct dvb_frontend *fe)
{
	struct cx24116_state *state = fe->demodulator_priv;
	const struct firmware *fw;
	int ret = 0;

	dprintk("%s()\n", __func__);

	if (cx24116_readreg(state, 0x20) > 0) {

		if (state->skip_fw_load)
			return 0;

		/* Load firmware */
		/* request the firmware, this will block until loaded */
		printk(KERN_INFO "%s: Waiting for firmware upload (%s)...\n",
			__func__, CX24116_DEFAULT_FIRMWARE);
		ret = request_firmware(&fw, CX24116_DEFAULT_FIRMWARE,
			state->i2c->dev.parent);
		printk(KERN_INFO "%s: Waiting for firmware upload(2)...\n",
			__func__);
		if (ret) {
			printk(KERN_ERR "%s: No firmware uploaded (timeout or file not found?)\n",
			       __func__);
			return ret;
		}

		/* Make sure we don't recurse back through here
		 * during loading */
		state->skip_fw_load = 1;

		ret = cx24116_load_firmware(fe, fw);
		if (ret)
			printk(KERN_ERR "%s: Writing firmware to device failed\n",
				__func__);

		release_firmware(fw);

		printk(KERN_INFO "%s: Firmware upload %s\n", __func__,
			ret == 0 ? "complete" : "failed");

		/* Ensure firmware is always loaded if required */
		state->skip_fw_load = 0;
	}

	return ret;
}

/* Take a basic firmware command structure, format it
 * and forward it for processing
 */
static int cx24116_cmd_execute(struct dvb_frontend *fe, struct cx24116_cmd *cmd)
{
	struct cx24116_state *state = fe->demodulator_priv;
	int i, ret;

	dprintk("%s()\n", __func__);

	/* Load the firmware if required */
	ret = cx24116_firmware_ondemand(fe);
	if (ret != 0) {
		printk(KERN_ERR "%s(): Unable initialise the firmware\n",
			__func__);
		return ret;
	}

	/* Write the command */
	for (i = 0; i < cmd->len ; i++) {
		dprintk("%s: 0x%02x == 0x%02x\n", __func__, i, cmd->args[i]);
		cx24116_writereg(state, i, cmd->args[i]);
	}

	/* Start execution and wait for cmd to terminate */
	cx24116_writereg(state, CX24116_REG_EXECUTE, 0x01);
	while (cx24116_readreg(state, CX24116_REG_EXECUTE)) {
		msleep(10);
		if (i++ > 64) {
			/* Avoid looping forever if the firmware does
				not respond */
			printk(KERN_WARNING "%s() Firmware not responding\n",
				__func__);
			return -EREMOTEIO;
		}
	}
	return 0;
}

static int cx24116_load_firmware(struct dvb_frontend *fe,
	const struct firmware *fw)
{
	struct cx24116_state *state = fe->demodulator_priv;
	struct cx24116_cmd cmd;
	int i, ret, len, max, remaining;
	unsigned char vers[4];

	dprintk("%s\n", __func__);
	dprintk("Firmware is %zu bytes (%02x %02x .. %02x %02x)\n",
			fw->size,
			fw->data[0],
			fw->data[1],
			fw->data[fw->size-2],
			fw->data[fw->size-1]);

	/* Toggle 88x SRST pin to reset demod */
	if (state->config->reset_device)
		state->config->reset_device(fe);

	/* Begin the firmware load process */
	/* Prepare the demod, load the firmware, cleanup after load */

	/* Init PLL */
	cx24116_writereg(state, 0xE5, 0x00);
	cx24116_writereg(state, 0xF1, 0x08);
	cx24116_writereg(state, 0xF2, 0x13);

	/* Start PLL */
	cx24116_writereg(state, 0xe0, 0x03);
	cx24116_writereg(state, 0xe0, 0x00);

	/* Unknown */
	cx24116_writereg(state, CX24116_REG_CLKDIV, 0x46);
	cx24116_writereg(state, CX24116_REG_RATEDIV, 0x00);

	/* Unknown */
	cx24116_writereg(state, 0xF0, 0x03);
	cx24116_writereg(state, 0xF4, 0x81);
	cx24116_writereg(state, 0xF5, 0x00);
	cx24116_writereg(state, 0xF6, 0x00);

	/* Split firmware to the max I2C write len and write.
	 * Writes whole firmware as one write when i2c_wr_max is set to 0. */
	if (state->config->i2c_wr_max)
		max = state->config->i2c_wr_max;
	else
		max = INT_MAX; /* enough for 32k firmware */

	for (remaining = fw->size; remaining > 0; remaining -= max - 1) {
		len = remaining;
		if (len > max - 1)
			len = max - 1;

		cx24116_writeregN(state, 0xF7, &fw->data[fw->size - remaining],
			len);
	}

	cx24116_writereg(state, 0xF4, 0x10);
	cx24116_writereg(state, 0xF0, 0x00);
	cx24116_writereg(state, 0xF8, 0x06);

	/* Firmware CMD 10: VCO config */
	cmd.args[0x00] = CMD_SET_VCO;
	cmd.args[0x01] = 0x05;
	cmd.args[0x02] = 0xdc;
	cmd.args[0x03] = 0xda;
	cmd.args[0x04] = 0xae;
	cmd.args[0x05] = 0xaa;
	cmd.args[0x06] = 0x04;
	cmd.args[0x07] = 0x9d;
	cmd.args[0x08] = 0xfc;
	cmd.args[0x09] = 0x06;
	cmd.len = 0x0a;
	ret = cx24116_cmd_execute(fe, &cmd);
	if (ret != 0)
		return ret;

	cx24116_writereg(state, CX24116_REG_SSTATUS, 0x00);

	/* Firmware CMD 14: Tuner config */
	cmd.args[0x00] = CMD_TUNERINIT;
	cmd.args[0x01] = 0x00;
	cmd.args[0x02] = 0x00;
	cmd.len = 0x03;
	ret = cx24116_cmd_execute(fe, &cmd);
	if (ret != 0)
		return ret;

	cx24116_writereg(state, 0xe5, 0x00);

	/* Firmware CMD 13: MPEG config */
	cmd.args[0x00] = CMD_MPEGCONFIG;
	cmd.args[0x01] = 0x01;
	cmd.args[0x02] = 0x75;
	cmd.args[0x03] = 0x00;
	if (state->config->mpg_clk_pos_pol)
		cmd.args[0x04] = state->config->mpg_clk_pos_pol;
	else
		cmd.args[0x04] = 0x02;
	cmd.args[0x05] = 0x00;
	cmd.len = 0x06;
	ret = cx24116_cmd_execute(fe, &cmd);
	if (ret != 0)
		return ret;

	/* Firmware CMD 35: Get firmware version */
	cmd.args[0x00] = CMD_UPDFWVERS;
	cmd.len = 0x02;
	for (i = 0; i < 4; i++) {
		cmd.args[0x01] = i;
		ret = cx24116_cmd_execute(fe, &cmd);
		if (ret != 0)
			return ret;
		vers[i] = cx24116_readreg(state, CX24116_REG_MAILBOX);
	}
	printk(KERN_INFO "%s: FW version %i.%i.%i.%i\n", __func__,
		vers[0], vers[1], vers[2], vers[3]);

	return 0;
}

static int cx24116_read_status(struct dvb_frontend *fe, enum fe_status *status)
{
	struct cx24116_state *state = fe->demodulator_priv;

	int lock = cx24116_readreg(state, CX24116_REG_SSTATUS) &
		CX24116_STATUS_MASK;

	dprintk("%s: status = 0x%02x\n", __func__, lock);

	*status = 0;

	if (lock & CX24116_HAS_SIGNAL)
		*status |= FE_HAS_SIGNAL;
	if (lock & CX24116_HAS_CARRIER)
		*status |= FE_HAS_CARRIER;
	if (lock & CX24116_HAS_VITERBI)
		*status |= FE_HAS_VITERBI;
	if (lock & CX24116_HAS_SYNCLOCK)
		*status |= FE_HAS_SYNC | FE_HAS_LOCK;

	return 0;
}

static int cx24116_read_ber(struct dvb_frontend *fe, u32 *ber)
{
	struct cx24116_state *state = fe->demodulator_priv;

	dprintk("%s()\n", __func__);

	*ber =  (cx24116_readreg(state, CX24116_REG_BER24) << 24) |
		(cx24116_readreg(state, CX24116_REG_BER16) << 16) |
		(cx24116_readreg(state, CX24116_REG_BER8)  << 8)  |
		 cx24116_readreg(state, CX24116_REG_BER0);

	return 0;
}

/* TODO Determine function and scale appropriately */
static int cx24116_read_signal_strength(struct dvb_frontend *fe,
	u16 *signal_strength)
{
	struct cx24116_state *state = fe->demodulator_priv;
	struct cx24116_cmd cmd;
	int ret;
	u16 sig_reading;

	dprintk("%s()\n", __func__);

	/* Firmware CMD 19: Get AGC */
	cmd.args[0x00] = CMD_GETAGC;
	cmd.len = 0x01;
	ret = cx24116_cmd_execute(fe, &cmd);
	if (ret != 0)
		return ret;

	sig_reading =
		(cx24116_readreg(state,
			CX24116_REG_SSTATUS) & CX24116_SIGNAL_MASK) |
		(cx24116_readreg(state, CX24116_REG_SIGNAL) << 6);
	*signal_strength = 0 - sig_reading;

	dprintk("%s: raw / cooked = 0x%04x / 0x%04x\n",
		__func__, sig_reading, *signal_strength);

	return 0;
}

/* SNR (0..100)% = (sig & 0xf0) * 10 + (sig & 0x0f) * 10 / 16 */
static int cx24116_read_snr_pct(struct dvb_frontend *fe, u16 *snr)
{
	struct cx24116_state *state = fe->demodulator_priv;
	u8 snr_reading;
	static const u32 snr_tab[] = { /* 10 x Table (rounded up) */
		0x00000, 0x0199A, 0x03333, 0x04ccD, 0x06667,
		0x08000, 0x0999A, 0x0b333, 0x0cccD, 0x0e667,
		0x10000, 0x1199A, 0x13333, 0x14ccD, 0x16667,
		0x18000 };

	dprintk("%s()\n", __func__);

	snr_reading = cx24116_readreg(state, CX24116_REG_QUALITY0);

	if (snr_reading >= 0xa0 /* 100% */)
		*snr = 0xffff;
	else
		*snr = snr_tab[(snr_reading & 0xf0) >> 4] +
			(snr_tab[(snr_reading & 0x0f)] >> 4);

	dprintk("%s: raw / cooked = 0x%02x / 0x%04x\n", __func__,
		snr_reading, *snr);

	return 0;
}

/* The reelbox patches show the value in the registers represents
 * ESNO, from 0->30db (values 0->300). We provide this value by
 * default.
 */
static int cx24116_read_snr_esno(struct dvb_frontend *fe, u16 *snr)
{
	struct cx24116_state *state = fe->demodulator_priv;

	dprintk("%s()\n", __func__);

	*snr = cx24116_readreg(state, CX24116_REG_QUALITY8) << 8 |
		cx24116_readreg(state, CX24116_REG_QUALITY0);

	dprintk("%s: raw 0x%04x\n", __func__, *snr);

	return 0;
}

static int cx24116_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	if (esno_snr == 1)
		return cx24116_read_snr_esno(fe, snr);
	else
		return cx24116_read_snr_pct(fe, snr);
}

static int cx24116_read_ucblocks(struct dvb_frontend *fe, u32 *ucblocks)
{
	struct cx24116_state *state = fe->demodulator_priv;

	dprintk("%s()\n", __func__);

	*ucblocks = (cx24116_readreg(state, CX24116_REG_UCB8) << 8) |
		cx24116_readreg(state, CX24116_REG_UCB0);

	return 0;
}

/* Overwrite the current tuning params, we are about to tune */
static void cx24116_clone_params(struct dvb_frontend *fe)
{
	struct cx24116_state *state = fe->demodulator_priv;
	state->dcur = state->dnxt;
}

/* Wait for LNB */
static int cx24116_wait_for_lnb(struct dvb_frontend *fe)
{
	struct cx24116_state *state = fe->demodulator_priv;
	int i;

	dprintk("%s() qstatus = 0x%02x\n", __func__,
		cx24116_readreg(state, CX24116_REG_QSTATUS));

	/* Wait for up to 300 ms */
	for (i = 0; i < 30 ; i++) {
		if (cx24116_readreg(state, CX24116_REG_QSTATUS) & 0x20)
			return 0;
		msleep(10);
	}

	dprintk("%s(): LNB not ready\n", __func__);

	return -ETIMEDOUT; /* -EBUSY ? */
}

static int cx24116_set_voltage(struct dvb_frontend *fe,
	enum fe_sec_voltage voltage)
{
	struct cx24116_cmd cmd;
	int ret;

	dprintk("%s: %s\n", __func__,
		voltage == SEC_VOLTAGE_13 ? "SEC_VOLTAGE_13" :
		voltage == SEC_VOLTAGE_18 ? "SEC_VOLTAGE_18" : "??");

	/* Wait for LNB ready */
	ret = cx24116_wait_for_lnb(fe);
	if (ret != 0)
		return ret;

	/* Wait for voltage/min repeat delay */
	msleep(100);

	cmd.args[0x00] = CMD_LNBDCLEVEL;
	cmd.args[0x01] = (voltage == SEC_VOLTAGE_18 ? 0x01 : 0x00);
	cmd.len = 0x02;

	/* Min delay time before DiSEqC send */
	msleep(15);

	return cx24116_cmd_execute(fe, &cmd);
}

static int cx24116_set_tone(struct dvb_frontend *fe,
	enum fe_sec_tone_mode tone)
{
	struct cx24116_cmd cmd;
	int ret;

	dprintk("%s(%d)\n", __func__, tone);
	if ((tone != SEC_TONE_ON) && (tone != SEC_TONE_OFF)) {
		printk(KERN_ERR "%s: Invalid, tone=%d\n", __func__, tone);
		return -EINVAL;
	}

	/* Wait for LNB ready */
	ret = cx24116_wait_for_lnb(fe);
	if (ret != 0)
		return ret;

	/* Min delay time after DiSEqC send */
	msleep(15); /* XXX determine is FW does this, see send_diseqc/burst */

	/* Now we set the tone */
	cmd.args[0x00] = CMD_SET_TONE;
	cmd.args[0x01] = 0x00;
	cmd.args[0x02] = 0x00;

	switch (tone) {
	case SEC_TONE_ON:
		dprintk("%s: setting tone on\n", __func__);
		cmd.args[0x03] = 0x01;
		break;
	case SEC_TONE_OFF:
		dprintk("%s: setting tone off\n", __func__);
		cmd.args[0x03] = 0x00;
		break;
	}
	cmd.len = 0x04;

	/* Min delay time before DiSEqC send */
	msleep(15); /* XXX determine is FW does this, see send_diseqc/burst */

	return cx24116_cmd_execute(fe, &cmd);
}

/* Initialise DiSEqC */
static int cx24116_diseqc_init(struct dvb_frontend *fe)
{
	struct cx24116_state *state = fe->demodulator_priv;
	struct cx24116_cmd cmd;
	int ret;

	/* Firmware CMD 20: LNB/DiSEqC config */
	cmd.args[0x00] = CMD_LNBCONFIG;
	cmd.args[0x01] = 0x00;
	cmd.args[0x02] = 0x10;
	cmd.args[0x03] = 0x00;
	cmd.args[0x04] = 0x8f;
	cmd.args[0x05] = 0x28;
	cmd.args[0x06] = (toneburst == CX24116_DISEQC_TONEOFF) ? 0x00 : 0x01;
	cmd.args[0x07] = 0x01;
	cmd.len = 0x08;
	ret = cx24116_cmd_execute(fe, &cmd);
	if (ret != 0)
		return ret;

	/* Prepare a DiSEqC command */
	state->dsec_cmd.args[0x00] = CMD_LNBSEND;

	/* DiSEqC burst */
	state->dsec_cmd.args[CX24116_DISEQC_BURST]  = CX24116_DISEQC_MINI_A;

	/* Unknown */
	state->dsec_cmd.args[CX24116_DISEQC_ARG2_2] = 0x02;
	state->dsec_cmd.args[CX24116_DISEQC_ARG3_0] = 0x00;
	/* Continuation flag? */
	state->dsec_cmd.args[CX24116_DISEQC_ARG4_0] = 0x00;

	/* DiSEqC message length */
	state->dsec_cmd.args[CX24116_DISEQC_MSGLEN] = 0x00;

	/* Command length */
	state->dsec_cmd.len = CX24116_DISEQC_MSGOFS;

	return 0;
}

/* Send DiSEqC message with derived burst (hack) || previous burst */
static int cx24116_send_diseqc_msg(struct dvb_frontend *fe,
	struct dvb_diseqc_master_cmd *d)
{
	struct cx24116_state *state = fe->demodulator_priv;
	int i, ret;

	/* Validate length */
	if (d->msg_len > sizeof(d->msg))
                return -EINVAL;

	/* Dump DiSEqC message */
	if (debug) {
		printk(KERN_INFO "cx24116: %s(", __func__);
		for (i = 0 ; i < d->msg_len ;) {
			printk(KERN_INFO "0x%02x", d->msg[i]);
			if (++i < d->msg_len)
				printk(KERN_INFO ", ");
		}
		printk(") toneburst=%d\n", toneburst);
	}

	/* DiSEqC message */
	for (i = 0; i < d->msg_len; i++)
		state->dsec_cmd.args[CX24116_DISEQC_MSGOFS + i] = d->msg[i];

	/* DiSEqC message length */
	state->dsec_cmd.args[CX24116_DISEQC_MSGLEN] = d->msg_len;

	/* Command length */
	state->dsec_cmd.len = CX24116_DISEQC_MSGOFS +
		state->dsec_cmd.args[CX24116_DISEQC_MSGLEN];

	/* DiSEqC toneburst */
	if (toneburst == CX24116_DISEQC_MESGCACHE)
		/* Message is cached */
		return 0;

	else if (toneburst == CX24116_DISEQC_TONEOFF)
		/* Message is sent without burst */
		state->dsec_cmd.args[CX24116_DISEQC_BURST] = 0;

	else if (toneburst == CX24116_DISEQC_TONECACHE) {
		/*
		 * Message is sent with derived else cached burst
		 *
		 * WRITE PORT GROUP COMMAND 38
		 *
		 * 0/A/A: E0 10 38 F0..F3
		 * 1/B/B: E0 10 38 F4..F7
		 * 2/C/A: E0 10 38 F8..FB
		 * 3/D/B: E0 10 38 FC..FF
		 *
		 * databyte[3]= 8421:8421
		 *              ABCD:WXYZ
		 *              CLR :SET
		 *
		 *              WX= PORT SELECT 0..3    (X=TONEBURST)
		 *              Y = VOLTAGE             (0=13V, 1=18V)
		 *              Z = BAND                (0=LOW, 1=HIGH(22K))
		 */
		if (d->msg_len >= 4 && d->msg[2] == 0x38)
			state->dsec_cmd.args[CX24116_DISEQC_BURST] =
				((d->msg[3] & 4) >> 2);
		if (debug)
			dprintk("%s burst=%d\n", __func__,
				state->dsec_cmd.args[CX24116_DISEQC_BURST]);
	}

	/* Wait for LNB ready */
	ret = cx24116_wait_for_lnb(fe);
	if (ret != 0)
		return ret;

	/* Wait for voltage/min repeat delay */
	msleep(100);

	/* Command */
	ret = cx24116_cmd_execute(fe, &state->dsec_cmd);
	if (ret != 0)
		return ret;
	/*
	 * Wait for send
	 *
	 * Eutelsat spec:
	 * >15ms delay          + (XXX determine if FW does this, see set_tone)
	 *  13.5ms per byte     +
	 * >15ms delay          +
	 *  12.5ms burst        +
	 * >15ms delay            (XXX determine if FW does this, see set_tone)
	 */
	msleep((state->dsec_cmd.args[CX24116_DISEQC_MSGLEN] << 4) +
		((toneburst == CX24116_DISEQC_TONEOFF) ? 30 : 60));

	return 0;
}

/* Send DiSEqC burst */
static int cx24116_diseqc_send_burst(struct dvb_frontend *fe,
	enum fe_sec_mini_cmd burst)
{
	struct cx24116_state *state = fe->demodulator_priv;
	int ret;

	dprintk("%s(%d) toneburst=%d\n", __func__, burst, toneburst);

	/* DiSEqC burst */
	if (burst == SEC_MINI_A)
		state->dsec_cmd.args[CX24116_DISEQC_BURST] =
			CX24116_DISEQC_MINI_A;
	else if (burst == SEC_MINI_B)
		state->dsec_cmd.args[CX24116_DISEQC_BURST] =
			CX24116_DISEQC_MINI_B;
	else
		return -EINVAL;

	/* DiSEqC toneburst */
	if (toneburst != CX24116_DISEQC_MESGCACHE)
		/* Burst is cached */
		return 0;

	/* Burst is to be sent with cached message */

	/* Wait for LNB ready */
	ret = cx24116_wait_for_lnb(fe);
	if (ret != 0)
		return ret;

	/* Wait for voltage/min repeat delay */
	msleep(100);

	/* Command */
	ret = cx24116_cmd_execute(fe, &state->dsec_cmd);
	if (ret != 0)
		return ret;

	/*
	 * Wait for send
	 *
	 * Eutelsat spec:
	 * >15ms delay          + (XXX determine if FW does this, see set_tone)
	 *  13.5ms per byte     +
	 * >15ms delay          +
	 *  12.5ms burst        +
	 * >15ms delay            (XXX determine if FW does this, see set_tone)
	 */
	msleep((state->dsec_cmd.args[CX24116_DISEQC_MSGLEN] << 4) + 60);

	return 0;
}

static void cx24116_release(struct dvb_frontend *fe)
{
	struct cx24116_state *state = fe->demodulator_priv;
	dprintk("%s\n", __func__);
	kfree(state);
}

static const struct dvb_frontend_ops cx24116_ops;

struct dvb_frontend *cx24116_attach(const struct cx24116_config *config,
	struct i2c_adapter *i2c)
{
	struct cx24116_state *state = NULL;
	int ret;

	dprintk("%s\n", __func__);

	/* allocate memory for the internal state */
	state = kzalloc(sizeof(struct cx24116_state), GFP_KERNEL);
	if (state == NULL)
		goto error1;

	state->config = config;
	state->i2c = i2c;

	/* check if the demod is present */
	ret = (cx24116_readreg(state, 0xFF) << 8) |
		cx24116_readreg(state, 0xFE);
	if (ret != 0x0501) {
		printk(KERN_INFO "Invalid probe, probably not a CX24116 device\n");
		goto error2;
	}

	/* create dvb_frontend */
	memcpy(&state->frontend.ops, &cx24116_ops,
		sizeof(struct dvb_frontend_ops));
	state->frontend.demodulator_priv = state;
	return &state->frontend;

error2: kfree(state);
error1: return NULL;
}
EXPORT_SYMBOL(cx24116_attach);

/*
 * Initialise or wake up device
 *
 * Power config will reset and load initial firmware if required
 */
static int cx24116_initfe(struct dvb_frontend *fe)
{
	struct cx24116_state *state = fe->demodulator_priv;
	struct cx24116_cmd cmd;
	int ret;

	dprintk("%s()\n", __func__);

	/* Power on */
	cx24116_writereg(state, 0xe0, 0);
	cx24116_writereg(state, 0xe1, 0);
	cx24116_writereg(state, 0xea, 0);

	/* Firmware CMD 36: Power config */
	cmd.args[0x00] = CMD_TUNERSLEEP;
	cmd.args[0x01] = 0;
	cmd.len = 0x02;
	ret = cx24116_cmd_execute(fe, &cmd);
	if (ret != 0)
		return ret;

	ret = cx24116_diseqc_init(fe);
	if (ret != 0)
		return ret;

	/* HVR-4000 needs this */
	return cx24116_set_voltage(fe, SEC_VOLTAGE_13);
}

/*
 * Put device to sleep
 */
static int cx24116_sleep(struct dvb_frontend *fe)
{
	struct cx24116_state *state = fe->demodulator_priv;
	struct cx24116_cmd cmd;
	int ret;

	dprintk("%s()\n", __func__);

	/* Firmware CMD 36: Power config */
	cmd.args[0x00] = CMD_TUNERSLEEP;
	cmd.args[0x01] = 1;
	cmd.len = 0x02;
	ret = cx24116_cmd_execute(fe, &cmd);
	if (ret != 0)
		return ret;

	/* Power off (Shutdown clocks) */
	cx24116_writereg(state, 0xea, 0xff);
	cx24116_writereg(state, 0xe1, 1);
	cx24116_writereg(state, 0xe0, 1);

	return 0;
}

/* dvb-core told us to tune, the tv property cache will be complete,
 * it's safe for is to pull values and use them for tuning purposes.
 */
static int cx24116_set_frontend(struct dvb_frontend *fe)
{
	struct cx24116_state *state = fe->demodulator_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct cx24116_cmd cmd;
	enum fe_status tunerstat;
	int i, status, ret, retune = 1;

	dprintk("%s()\n", __func__);

	switch (c->delivery_system) {
	case SYS_DVBS:
		dprintk("%s: DVB-S delivery system selected\n", __func__);

		/* Only QPSK is supported for DVB-S */
		if (c->modulation != QPSK) {
			dprintk("%s: unsupported modulation selected (%d)\n",
				__func__, c->modulation);
			return -EOPNOTSUPP;
		}

		/* Pilot doesn't exist in DVB-S, turn bit off */
		state->dnxt.pilot_val = CX24116_PILOT_OFF;

		/* DVB-S only supports 0.35 */
		if (c->rolloff != ROLLOFF_35) {
			dprintk("%s: unsupported rolloff selected (%d)\n",
				__func__, c->rolloff);
			return -EOPNOTSUPP;
		}
		state->dnxt.rolloff_val = CX24116_ROLLOFF_035;
		break;

	case SYS_DVBS2:
		dprintk("%s: DVB-S2 delivery system selected\n", __func__);

		/*
		 * NBC 8PSK/QPSK with DVB-S is supported for DVB-S2,
		 * but not hardware auto detection
		 */
		if (c->modulation != PSK_8 && c->modulation != QPSK) {
			dprintk("%s: unsupported modulation selected (%d)\n",
				__func__, c->modulation);
			return -EOPNOTSUPP;
		}

		switch (c->pilot) {
		case PILOT_AUTO:	/* Not supported but emulated */
			state->dnxt.pilot_val = (c->modulation == QPSK)
				? CX24116_PILOT_OFF : CX24116_PILOT_ON;
			retune++;
			break;
		case PILOT_OFF:
			state->dnxt.pilot_val = CX24116_PILOT_OFF;
			break;
		case PILOT_ON:
			state->dnxt.pilot_val = CX24116_PILOT_ON;
			break;
		default:
			dprintk("%s: unsupported pilot mode selected (%d)\n",
				__func__, c->pilot);
			return -EOPNOTSUPP;
		}

		switch (c->rolloff) {
		case ROLLOFF_20:
			state->dnxt.rolloff_val = CX24116_ROLLOFF_020;
			break;
		case ROLLOFF_25:
			state->dnxt.rolloff_val = CX24116_ROLLOFF_025;
			break;
		case ROLLOFF_35:
			state->dnxt.rolloff_val = CX24116_ROLLOFF_035;
			break;
		case ROLLOFF_AUTO:	/* Rolloff must be explicit */
		default:
			dprintk("%s: unsupported rolloff selected (%d)\n",
				__func__, c->rolloff);
			return -EOPNOTSUPP;
		}
		break;

	default:
		dprintk("%s: unsupported delivery system selected (%d)\n",
			__func__, c->delivery_system);
		return -EOPNOTSUPP;
	}
	state->dnxt.delsys = c->delivery_system;
	state->dnxt.modulation = c->modulation;
	state->dnxt.frequency = c->frequency;
	state->dnxt.pilot = c->pilot;
	state->dnxt.rolloff = c->rolloff;

	ret = cx24116_set_inversion(state, c->inversion);
	if (ret !=  0)
		return ret;

	/* FEC_NONE/AUTO for DVB-S2 is not supported and detected here */
	ret = cx24116_set_fec(state, c->delivery_system, c->modulation, c->fec_inner);
	if (ret !=  0)
		return ret;

	ret = cx24116_set_symbolrate(state, c->symbol_rate);
	if (ret !=  0)
		return ret;

	/* discard the 'current' tuning parameters and prepare to tune */
	cx24116_clone_params(fe);

	dprintk("%s:   delsys      = %d\n", __func__, state->dcur.delsys);
	dprintk("%s:   modulation  = %d\n", __func__, state->dcur.modulation);
	dprintk("%s:   frequency   = %d\n", __func__, state->dcur.frequency);
	dprintk("%s:   pilot       = %d (val = 0x%02x)\n", __func__,
		state->dcur.pilot, state->dcur.pilot_val);
	dprintk("%s:   retune      = %d\n", __func__, retune);
	dprintk("%s:   rolloff     = %d (val = 0x%02x)\n", __func__,
		state->dcur.rolloff, state->dcur.rolloff_val);
	dprintk("%s:   symbol_rate = %d\n", __func__, state->dcur.symbol_rate);
	dprintk("%s:   FEC         = %d (mask/val = 0x%02x/0x%02x)\n", __func__,
		state->dcur.fec, state->dcur.fec_mask, state->dcur.fec_val);
	dprintk("%s:   Inversion   = %d (val = 0x%02x)\n", __func__,
		state->dcur.inversion, state->dcur.inversion_val);

	/* This is also done in advise/acquire on HVR4000 but not on LITE */
	if (state->config->set_ts_params)
		state->config->set_ts_params(fe, 0);

	/* Set/Reset B/W */
	cmd.args[0x00] = CMD_BANDWIDTH;
	cmd.args[0x01] = 0x01;
	cmd.len = 0x02;
	ret = cx24116_cmd_execute(fe, &cmd);
	if (ret != 0)
		return ret;

	/* Prepare a tune request */
	cmd.args[0x00] = CMD_TUNEREQUEST;

	/* Frequency */
	cmd.args[0x01] = (state->dcur.frequency & 0xff0000) >> 16;
	cmd.args[0x02] = (state->dcur.frequency & 0x00ff00) >> 8;
	cmd.args[0x03] = (state->dcur.frequency & 0x0000ff);

	/* Symbol Rate */
	cmd.args[0x04] = ((state->dcur.symbol_rate / 1000) & 0xff00) >> 8;
	cmd.args[0x05] = ((state->dcur.symbol_rate / 1000) & 0x00ff);

	/* Automatic Inversion */
	cmd.args[0x06] = state->dcur.inversion_val;

	/* Modulation / FEC / Pilot */
	cmd.args[0x07] = state->dcur.fec_val | state->dcur.pilot_val;

	cmd.args[0x08] = CX24116_SEARCH_RANGE_KHZ >> 8;
	cmd.args[0x09] = CX24116_SEARCH_RANGE_KHZ & 0xff;
	cmd.args[0x0a] = 0x00;
	cmd.args[0x0b] = 0x00;
	cmd.args[0x0c] = state->dcur.rolloff_val;
	cmd.args[0x0d] = state->dcur.fec_mask;

	if (state->dcur.symbol_rate > 30000000) {
		cmd.args[0x0e] = 0x04;
		cmd.args[0x0f] = 0x00;
		cmd.args[0x10] = 0x01;
		cmd.args[0x11] = 0x77;
		cmd.args[0x12] = 0x36;
		cx24116_writereg(state, CX24116_REG_CLKDIV, 0x44);
		cx24116_writereg(state, CX24116_REG_RATEDIV, 0x01);
	} else {
		cmd.args[0x0e] = 0x06;
		cmd.args[0x0f] = 0x00;
		cmd.args[0x10] = 0x00;
		cmd.args[0x11] = 0xFA;
		cmd.args[0x12] = 0x24;
		cx24116_writereg(state, CX24116_REG_CLKDIV, 0x46);
		cx24116_writereg(state, CX24116_REG_RATEDIV, 0x00);
	}

	cmd.len = 0x13;

	/* We need to support pilot and non-pilot tuning in the
	 * driver automatically. This is a workaround for because
	 * the demod does not support autodetect.
	 */
	do {
		/* Reset status register */
		status = cx24116_readreg(state, CX24116_REG_SSTATUS)
			& CX24116_SIGNAL_MASK;
		cx24116_writereg(state, CX24116_REG_SSTATUS, status);

		/* Tune */
		ret = cx24116_cmd_execute(fe, &cmd);
		if (ret != 0)
			break;

		/*
		 * Wait for up to 500 ms before retrying
		 *
		 * If we are able to tune then generally it occurs within 100ms.
		 * If it takes longer, try a different toneburst setting.
		 */
		for (i = 0; i < 50 ; i++) {
			cx24116_read_status(fe, &tunerstat);
			status = tunerstat & (FE_HAS_SIGNAL | FE_HAS_SYNC);
			if (status == (FE_HAS_SIGNAL | FE_HAS_SYNC)) {
				dprintk("%s: Tuned\n", __func__);
				goto tuned;
			}
			msleep(10);
		}

		dprintk("%s: Not tuned\n", __func__);

		/* Toggle pilot bit when in auto-pilot */
		if (state->dcur.pilot == PILOT_AUTO)
			cmd.args[0x07] ^= CX24116_PILOT_ON;
	} while (--retune);

tuned:  /* Set/Reset B/W */
	cmd.args[0x00] = CMD_BANDWIDTH;
	cmd.args[0x01] = 0x00;
	cmd.len = 0x02;
	return cx24116_cmd_execute(fe, &cmd);
}

static int cx24116_tune(struct dvb_frontend *fe, bool re_tune,
	unsigned int mode_flags, unsigned int *delay, enum fe_status *status)
{
	/*
	 * It is safe to discard "params" here, as the DVB core will sync
	 * fe->dtv_property_cache with fepriv->parameters_in, where the
	 * DVBv3 params are stored. The only practical usage for it indicate
	 * that re-tuning is needed, e. g. (fepriv->state & FESTATE_RETUNE) is
	 * true.
	 */

	*delay = HZ / 5;
	if (re_tune) {
		int ret = cx24116_set_frontend(fe);
		if (ret)
			return ret;
	}
	return cx24116_read_status(fe, status);
}

static int cx24116_get_algo(struct dvb_frontend *fe)
{
	return DVBFE_ALGO_HW;
}

static const struct dvb_frontend_ops cx24116_ops = {
	.delsys = { SYS_DVBS, SYS_DVBS2 },
	.info = {
		.name = "Conexant CX24116/CX24118",
		.frequency_min = 950000,
		.frequency_max = 2150000,
		.frequency_stepsize = 1011, /* kHz for QPSK frontends */
		.frequency_tolerance = 5000,
		.symbol_rate_min = 1000000,
		.symbol_rate_max = 45000000,
		.caps = FE_CAN_INVERSION_AUTO |
			FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
			FE_CAN_FEC_4_5 | FE_CAN_FEC_5_6 | FE_CAN_FEC_6_7 |
			FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
			FE_CAN_2G_MODULATION |
			FE_CAN_QPSK | FE_CAN_RECOVER
	},

	.release = cx24116_release,

	.init = cx24116_initfe,
	.sleep = cx24116_sleep,
	.read_status = cx24116_read_status,
	.read_ber = cx24116_read_ber,
	.read_signal_strength = cx24116_read_signal_strength,
	.read_snr = cx24116_read_snr,
	.read_ucblocks = cx24116_read_ucblocks,
	.set_tone = cx24116_set_tone,
	.set_voltage = cx24116_set_voltage,
	.diseqc_send_master_cmd = cx24116_send_diseqc_msg,
	.diseqc_send_burst = cx24116_diseqc_send_burst,
	.get_frontend_algo = cx24116_get_algo,
	.tune = cx24116_tune,

	.set_frontend = cx24116_set_frontend,
};

MODULE_DESCRIPTION("DVB Frontend module for Conexant cx24116/cx24118 hardware");
MODULE_AUTHOR("Steven Toth");
MODULE_LICENSE("GPL");

