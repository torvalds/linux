/*
    Frontend-driver for TwinHan DST Frontend

    Copyright (C) 2003 Jamie Honan

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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <asm/div64.h>

#include "dvb_frontend.h"
#include "dst_priv.h"
#include "dst.h"

struct dst_state {

	struct i2c_adapter* i2c;

	struct bt878* bt;

	struct dvb_frontend_ops ops;

	/* configuration settings */
	const struct dst_config* config;

	struct dvb_frontend frontend;

	/* private demodulator data */
	u8 tx_tuna[10];
	u8 rx_tuna[10];
	u8 rxbuffer[10];
	u8 diseq_flags;
	u8 dst_type;
	u32 type_flags;
	u32 frequency;		/* intermediate frequency in kHz for QPSK */
	fe_spectral_inversion_t inversion;
	u32 symbol_rate;	/* symbol rate in Symbols per second */
	fe_code_rate_t fec;
	fe_sec_voltage_t voltage;
	fe_sec_tone_mode_t tone;
	u32 decode_freq;
	u8 decode_lock;
	u16 decode_strength;
	u16 decode_snr;
	unsigned long cur_jiff;
	u8 k22;
	fe_bandwidth_t bandwidth;
};

static unsigned int dst_verbose = 0;
module_param(dst_verbose, int, 0644);
MODULE_PARM_DESC(dst_verbose, "verbose startup messages, default is 1 (yes)");
static unsigned int dst_debug = 0;
module_param(dst_debug, int, 0644);
MODULE_PARM_DESC(dst_debug, "debug messages, default is 0 (no)");

#define dprintk	if (dst_debug) printk

#define DST_TYPE_IS_SAT		0
#define DST_TYPE_IS_TERR	1
#define DST_TYPE_IS_CABLE	2

#define DST_TYPE_HAS_NEWTUNE	1
#define DST_TYPE_HAS_TS204	2
#define DST_TYPE_HAS_SYMDIV	4

#define HAS_LOCK	1
#define ATTEMPT_TUNE	2
#define HAS_POWER	4

static void dst_packsize(struct dst_state* state, int psize)
{
	union dst_gpio_packet bits;

	bits.psize = psize;
	bt878_device_control(state->bt, DST_IG_TS, &bits);
}

static int dst_gpio_outb(struct dst_state* state, u32 mask, u32 enbb, u32 outhigh)
{
	union dst_gpio_packet enb;
	union dst_gpio_packet bits;
	int err;

	enb.enb.mask = mask;
	enb.enb.enable = enbb;
	if ((err = bt878_device_control(state->bt, DST_IG_ENABLE, &enb)) < 0) {
		dprintk("%s: dst_gpio_enb error (err == %i, mask == 0x%02x, enb == 0x%02x)\n", __FUNCTION__, err, mask, enbb);
		return -EREMOTEIO;
	}

	/* because complete disabling means no output, no need to do output packet */
	if (enbb == 0)
		return 0;

	bits.outp.mask = enbb;
	bits.outp.highvals = outhigh;

	if ((err = bt878_device_control(state->bt, DST_IG_WRITE, &bits)) < 0) {
		dprintk("%s: dst_gpio_outb error (err == %i, enbb == 0x%02x, outhigh == 0x%02x)\n", __FUNCTION__, err, enbb, outhigh);
		return -EREMOTEIO;
	}
	return 0;
}

static int dst_gpio_inb(struct dst_state *state, u8 * result)
{
	union dst_gpio_packet rd_packet;
	int err;

	*result = 0;

	if ((err = bt878_device_control(state->bt, DST_IG_READ, &rd_packet)) < 0) {
		dprintk("%s: dst_gpio_inb error (err == %i)\n", __FUNCTION__, err);
		return -EREMOTEIO;
	}

	*result = (u8) rd_packet.rd.value;
	return 0;
}

#define DST_I2C_ENABLE	1
#define DST_8820	2

static int dst_reset8820(struct dst_state *state)
{
	int retval;
	/* pull 8820 gpio pin low, wait, high, wait, then low */
	// dprintk ("%s: reset 8820\n", __FUNCTION__);
	retval = dst_gpio_outb(state, DST_8820, DST_8820, 0);
	if (retval < 0)
		return retval;
	msleep(10);
	retval = dst_gpio_outb(state, DST_8820, DST_8820, DST_8820);
	if (retval < 0)
		return retval;
	/* wait for more feedback on what works here *
	   msleep(10);
	   retval = dst_gpio_outb(dst, DST_8820, DST_8820, 0);
	   if (retval < 0)
	   return retval;
	 */
	return 0;
}

static int dst_i2c_enable(struct dst_state *state)
{
	int retval;
	/* pull I2C enable gpio pin low, wait */
	// dprintk ("%s: i2c enable\n", __FUNCTION__);
	retval = dst_gpio_outb(state, ~0, DST_I2C_ENABLE, 0);
	if (retval < 0)
		return retval;
	// dprintk ("%s: i2c enable delay\n", __FUNCTION__);
	msleep(33);
	return 0;
}

static int dst_i2c_disable(struct dst_state *state)
{
	int retval;
	/* release I2C enable gpio pin, wait */
	// dprintk ("%s: i2c disable\n", __FUNCTION__);
	retval = dst_gpio_outb(state, ~0, 0, 0);
	if (retval < 0)
		return retval;
	// dprintk ("%s: i2c disable delay\n", __FUNCTION__);
	msleep(33);
	return 0;
}

static int dst_wait_dst_ready(struct dst_state *state)
{
	u8 reply;
	int retval;
	int i;
	for (i = 0; i < 200; i++) {
		retval = dst_gpio_inb(state, &reply);
		if (retval < 0)
			return retval;
		if ((reply & DST_I2C_ENABLE) == 0) {
			dprintk("%s: dst wait ready after %d\n", __FUNCTION__, i);
			return 1;
		}
		msleep(10);
	}
	dprintk("%s: dst wait NOT ready after %d\n", __FUNCTION__, i);
	return 0;
}

static int write_dst(struct dst_state *state, u8 * data, u8 len)
{
	struct i2c_msg msg = {
		.addr = state->config->demod_address,.flags = 0,.buf = data,.len = len
	};
	int err;
	int cnt;

	if (dst_debug && dst_verbose) {
		u8 i;
		dprintk("%s writing", __FUNCTION__);
		for (i = 0; i < len; i++) {
			dprintk(" 0x%02x", data[i]);
		}
		dprintk("\n");
	}
	msleep(30);
	for (cnt = 0; cnt < 4; cnt++) {
		if ((err = i2c_transfer(state->i2c, &msg, 1)) < 0) {
			dprintk("%s: write_dst error (err == %i, len == 0x%02x, b0 == 0x%02x)\n", __FUNCTION__, err, len, data[0]);
			dst_i2c_disable(state);
			msleep(500);
			dst_i2c_enable(state);
			msleep(500);
			continue;
		} else
			break;
	}
	if (cnt >= 4)
		return -EREMOTEIO;
	return 0;
}

static int read_dst(struct dst_state *state, u8 * ret, u8 len)
{
	struct i2c_msg msg = {.addr = state->config->demod_address,.flags = I2C_M_RD,.buf = ret,.len = len };
	int err;
	int cnt;

	for (cnt = 0; cnt < 4; cnt++) {
		if ((err = i2c_transfer(state->i2c, &msg, 1)) < 0) {
			dprintk("%s: read_dst error (err == %i, len == 0x%02x, b0 == 0x%02x)\n", __FUNCTION__, err, len, ret[0]);
			dst_i2c_disable(state);
			dst_i2c_enable(state);
			continue;
		} else
			break;
	}
	if (cnt >= 4)
		return -EREMOTEIO;
	dprintk("%s reply is 0x%x\n", __FUNCTION__, ret[0]);
	if (dst_debug && dst_verbose) {
		for (err = 1; err < len; err++)
			dprintk(" 0x%x", ret[err]);
		if (err > 1)
			dprintk("\n");
	}
	return 0;
}

static int dst_set_freq(struct dst_state *state, u32 freq)
{
	u8 *val;

	state->frequency = freq;

	// dprintk("%s: set frequency %u\n", __FUNCTION__, freq);
	if (state->dst_type == DST_TYPE_IS_SAT) {
		freq = freq / 1000;
		if (freq < 950 || freq > 2150)
			return -EINVAL;
		val = &state->tx_tuna[0];
		val[2] = (freq >> 8) & 0x7f;
		val[3] = (u8) freq;
		val[4] = 1;
		val[8] &= ~4;
		if (freq < 1531)
			val[8] |= 4;
	} else if (state->dst_type == DST_TYPE_IS_TERR) {
		freq = freq / 1000;
		if (freq < 137000 || freq > 858000)
			return -EINVAL;
		val = &state->tx_tuna[0];
		val[2] = (freq >> 16) & 0xff;
		val[3] = (freq >> 8) & 0xff;
		val[4] = (u8) freq;
		val[5] = 0;
		switch (state->bandwidth) {
		case BANDWIDTH_6_MHZ:
			val[6] = 6;
			break;

		case BANDWIDTH_7_MHZ:
		case BANDWIDTH_AUTO:
			val[6] = 7;
			break;

		case BANDWIDTH_8_MHZ:
			val[6] = 8;
			break;
		}

		val[7] = 0;
		val[8] = 0;
	} else if (state->dst_type == DST_TYPE_IS_CABLE) {
		/* guess till will get one */
		freq = freq / 1000;
		val = &state->tx_tuna[0];
		val[2] = (freq >> 16) & 0xff;
		val[3] = (freq >> 8) & 0xff;
		val[4] = (u8) freq;
	} else
		return -EINVAL;
	return 0;
}

static int dst_set_bandwidth(struct dst_state* state, fe_bandwidth_t bandwidth)
{
	u8 *val;

	state->bandwidth = bandwidth;

	if (state->dst_type != DST_TYPE_IS_TERR)
		return 0;

	val = &state->tx_tuna[0];
	switch (bandwidth) {
	case BANDWIDTH_6_MHZ:
		val[6] = 6;
		break;

	case BANDWIDTH_7_MHZ:
		val[6] = 7;
		break;

	case BANDWIDTH_8_MHZ:
		val[6] = 8;
		break;

	default:
		return -EINVAL;
	}
	return 0;
}

static int dst_set_inversion(struct dst_state* state, fe_spectral_inversion_t inversion)
{
	u8 *val;

	state->inversion = inversion;

	val = &state->tx_tuna[0];

	val[8] &= ~0x80;

	switch (inversion) {
	case INVERSION_OFF:
		break;
	case INVERSION_ON:
		val[8] |= 0x80;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int dst_set_fec(struct dst_state* state, fe_code_rate_t fec)
{
	state->fec = fec;
	return 0;
}

static fe_code_rate_t dst_get_fec(struct dst_state* state)
{
	return state->fec;
}

static int dst_set_symbolrate(struct dst_state* state, u32 srate)
{
	u8 *val;
	u32 symcalc;
	u64 sval;

	state->symbol_rate = srate;

	if (state->dst_type == DST_TYPE_IS_TERR) {
		return 0;
	}
	// dprintk("%s: set srate %u\n", __FUNCTION__, srate);
	srate /= 1000;
	val = &state->tx_tuna[0];

	if (state->type_flags & DST_TYPE_HAS_SYMDIV) {
		sval = srate;
		sval <<= 20;
		do_div(sval, 88000);
		symcalc = (u32) sval;
		// dprintk("%s: set symcalc %u\n", __FUNCTION__, symcalc);
		val[5] = (u8) (symcalc >> 12);
		val[6] = (u8) (symcalc >> 4);
		val[7] = (u8) (symcalc << 4);
	} else {
		val[5] = (u8) (srate >> 16) & 0x7f;
		val[6] = (u8) (srate >> 8);
		val[7] = (u8) srate;
	}
	val[8] &= ~0x20;
	if (srate > 8000)
		val[8] |= 0x20;
	return 0;
}

static u8 dst_check_sum(u8 * buf, u32 len)
{
	u32 i;
	u8 val = 0;
	if (!len)
		return 0;
	for (i = 0; i < len; i++) {
		val += buf[i];
	}
	return ((~val) + 1);
}

struct dst_types {
	char *mstr;
	int offs;
	u8 dst_type;
	u32 type_flags;
};

static struct dst_types dst_tlist[] = {
	{"DST-020", 0, DST_TYPE_IS_SAT, DST_TYPE_HAS_SYMDIV},
	{"DST-030", 0, DST_TYPE_IS_SAT, DST_TYPE_HAS_TS204 | DST_TYPE_HAS_NEWTUNE},
	{"DST-03T", 0, DST_TYPE_IS_SAT, DST_TYPE_HAS_SYMDIV | DST_TYPE_HAS_TS204},
	{"DST-MOT", 0, DST_TYPE_IS_SAT, DST_TYPE_HAS_SYMDIV},
	{"DST-CI",  1, DST_TYPE_IS_SAT, DST_TYPE_HAS_TS204 | DST_TYPE_HAS_NEWTUNE},
	{"DSTMCI",  1, DST_TYPE_IS_SAT, DST_TYPE_HAS_NEWTUNE},
	{"DSTFCI",  1, DST_TYPE_IS_SAT, DST_TYPE_HAS_NEWTUNE},
	{"DCTNEW",  1, DST_TYPE_IS_CABLE, DST_TYPE_HAS_NEWTUNE},
	{"DCT-CI",  1, DST_TYPE_IS_CABLE, DST_TYPE_HAS_NEWTUNE | DST_TYPE_HAS_TS204},
	{"DTTDIG",  1, DST_TYPE_IS_TERR, 0}
};

/* DCTNEW and DCT-CI are guesses */

static void dst_type_flags_print(u32 type_flags)
{
	printk("DST type flags :");
	if (type_flags & DST_TYPE_HAS_NEWTUNE)
		printk(" 0x%x newtuner", DST_TYPE_HAS_NEWTUNE);
	if (type_flags & DST_TYPE_HAS_TS204)
		printk(" 0x%x ts204", DST_TYPE_HAS_TS204);
	if (type_flags & DST_TYPE_HAS_SYMDIV)
		printk(" 0x%x symdiv", DST_TYPE_HAS_SYMDIV);
	printk("\n");
}

static int dst_type_print(u8 type)
{
	char *otype;
	switch (type) {
	case DST_TYPE_IS_SAT:
		otype = "satellite";
		break;
	case DST_TYPE_IS_TERR:
		otype = "terrestrial";
		break;
	case DST_TYPE_IS_CABLE:
		otype = "cable";
		break;
	default:
		printk("%s: invalid dst type %d\n", __FUNCTION__, type);
		return -EINVAL;
	}
	printk("DST type : %s\n", otype);
	return 0;
}

static int dst_check_ci(struct dst_state *state)
{
	u8 txbuf[8];
	u8 rxbuf[8];
	int retval;
	int i;
	struct dst_types *dsp;
	u8 use_dst_type;
	u32 use_type_flags;

	memset(txbuf, 0, sizeof(txbuf));
	txbuf[1] = 6;
	txbuf[7] = dst_check_sum(txbuf, 7);

	dst_i2c_enable(state);
	dst_reset8820(state);
	retval = write_dst(state, txbuf, 8);
	if (retval < 0) {
		dst_i2c_disable(state);
		dprintk("%s: write not successful, maybe no card?\n", __FUNCTION__);
		return retval;
	}
	msleep(3);
	retval = read_dst(state, rxbuf, 1);
	dst_i2c_disable(state);
	if (retval < 0) {
		dprintk("%s: read not successful, maybe no card?\n", __FUNCTION__);
		return retval;
	}
	if (rxbuf[0] != 0xff) {
		dprintk("%s: write reply not 0xff, not ci (%02x)\n", __FUNCTION__, rxbuf[0]);
		return retval;
	}
	if (!dst_wait_dst_ready(state))
		return 0;
	// dst_i2c_enable(i2c); Dimitri
	retval = read_dst(state, rxbuf, 8);
	dst_i2c_disable(state);
	if (retval < 0) {
		dprintk("%s: read not successful\n", __FUNCTION__);
		return retval;
	}
	if (rxbuf[7] != dst_check_sum(rxbuf, 7)) {
		dprintk("%s: checksum failure\n", __FUNCTION__);
		return retval;
	}
	rxbuf[7] = '\0';
	for (i = 0, dsp = &dst_tlist[0]; i < sizeof(dst_tlist) / sizeof(dst_tlist[0]); i++, dsp++) {
		if (!strncmp(&rxbuf[dsp->offs], dsp->mstr, strlen(dsp->mstr))) {
			use_type_flags = dsp->type_flags;
			use_dst_type = dsp->dst_type;
			printk("%s: recognize %s\n", __FUNCTION__, dsp->mstr);
			break;
		}
	}
	if (i >= sizeof(dst_tlist) / sizeof(dst_tlist[0])) {
		printk("%s: unable to recognize %s or %s\n", __FUNCTION__, &rxbuf[0], &rxbuf[1]);
		printk("%s please email linux-dvb@linuxtv.org with this type in\n", __FUNCTION__);
		use_dst_type = DST_TYPE_IS_SAT;
		use_type_flags = DST_TYPE_HAS_SYMDIV;
	}
	dst_type_print(use_dst_type);

	state->type_flags = use_type_flags;
	state->dst_type = use_dst_type;
	dst_type_flags_print(state->type_flags);

	if (state->type_flags & DST_TYPE_HAS_TS204) {
		dst_packsize(state, 204);
	}
	return 0;
}

static int dst_command(struct dst_state* state, u8 * data, u8 len)
{
	int retval;
	u8 reply;

	dst_i2c_enable(state);
	dst_reset8820(state);
	retval = write_dst(state, data, len);
	if (retval < 0) {
		dst_i2c_disable(state);
		dprintk("%s: write not successful\n", __FUNCTION__);
		return retval;
	}
	msleep(33);
	retval = read_dst(state, &reply, 1);
	dst_i2c_disable(state);
	if (retval < 0) {
		dprintk("%s: read verify  not successful\n", __FUNCTION__);
		return retval;
	}
	if (reply != 0xff) {
		dprintk("%s: write reply not 0xff 0x%02x \n", __FUNCTION__, reply);
		return 0;
	}
	if (len >= 2 && data[0] == 0 && (data[1] == 1 || data[1] == 3))
		return 0;
	if (!dst_wait_dst_ready(state))
		return 0;
	// dst_i2c_enable(i2c); Per dimitri
	retval = read_dst(state, state->rxbuffer, 8);
	dst_i2c_disable(state);
	if (retval < 0) {
		dprintk("%s: read not successful\n", __FUNCTION__);
		return 0;
	}
	if (state->rxbuffer[7] != dst_check_sum(state->rxbuffer, 7)) {
		dprintk("%s: checksum failure\n", __FUNCTION__);
		return 0;
	}
	return 0;
}

static int dst_get_signal(struct dst_state* state)
{
	int retval;
	u8 get_signal[] = { 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfb };

	if ((state->diseq_flags & ATTEMPT_TUNE) == 0) {
		state->decode_lock = state->decode_strength = state->decode_snr = 0;
		return 0;
	}
	if (0 == (state->diseq_flags & HAS_LOCK)) {
		state->decode_lock = state->decode_strength = state->decode_snr = 0;
		return 0;
	}
	if (time_after_eq(jiffies, state->cur_jiff + (HZ / 5))) {
		retval = dst_command(state, get_signal, 8);
		if (retval < 0)
			return retval;
		if (state->dst_type == DST_TYPE_IS_SAT) {
			state->decode_lock = ((state->rxbuffer[6] & 0x10) == 0) ? 1 : 0;
			state->decode_strength = state->rxbuffer[5] << 8;
			state->decode_snr = state->rxbuffer[2] << 8 | state->rxbuffer[3];
		} else if ((state->dst_type == DST_TYPE_IS_TERR) || (state->dst_type == DST_TYPE_IS_CABLE)) {
			state->decode_lock = (state->rxbuffer[1]) ? 1 : 0;
			state->decode_strength = state->rxbuffer[4] << 8;
			state->decode_snr = state->rxbuffer[3] << 8;
		}
		state->cur_jiff = jiffies;
	}
	return 0;
}

static int dst_tone_power_cmd(struct dst_state* state)
{
	u8 paket[8] = { 0x00, 0x09, 0xff, 0xff, 0x01, 0x00, 0x00, 0x00 };

	if (state->dst_type == DST_TYPE_IS_TERR)
		return 0;

	if (state->voltage == SEC_VOLTAGE_OFF)
		paket[4] = 0;
	else
		paket[4] = 1;
	if (state->tone == SEC_TONE_ON)
		paket[2] = state->k22;
	else
		paket[2] = 0;
	paket[7] = dst_check_sum(&paket[0], 7);
	dst_command(state, paket, 8);
	return 0;
}

static int dst_get_tuna(struct dst_state* state)
{
	int retval;
	if ((state->diseq_flags & ATTEMPT_TUNE) == 0)
		return 0;
	state->diseq_flags &= ~(HAS_LOCK);
	if (!dst_wait_dst_ready(state))
		return 0;
	if (state->type_flags & DST_TYPE_HAS_NEWTUNE) {
		/* how to get variable length reply ???? */
		retval = read_dst(state, state->rx_tuna, 10);
	} else {
		retval = read_dst(state, &state->rx_tuna[2], 8);
	}
	if (retval < 0) {
		dprintk("%s: read not successful\n", __FUNCTION__);
		return 0;
	}
	if (state->type_flags & DST_TYPE_HAS_NEWTUNE) {
		if (state->rx_tuna[9] != dst_check_sum(&state->rx_tuna[0], 9)) {
			dprintk("%s: checksum failure?\n", __FUNCTION__);
			return 0;
		}
	} else {
		if (state->rx_tuna[9] != dst_check_sum(&state->rx_tuna[2], 7)) {
			dprintk("%s: checksum failure?\n", __FUNCTION__);
			return 0;
		}
	}
	if (state->rx_tuna[2] == 0 && state->rx_tuna[3] == 0)
		return 0;
	state->decode_freq = ((state->rx_tuna[2] & 0x7f) << 8) + state->rx_tuna[3];

	state->decode_lock = 1;
	/*
	   dst->decode_n1 = (dst->rx_tuna[4] << 8) +
	   (dst->rx_tuna[5]);

	   dst->decode_n2 = (dst->rx_tuna[8] << 8) +
	   (dst->rx_tuna[7]);
	 */
	state->diseq_flags |= HAS_LOCK;
	/* dst->cur_jiff = jiffies; */
	return 1;
}

static int dst_set_voltage(struct dvb_frontend* fe, fe_sec_voltage_t voltage);

static int dst_write_tuna(struct dvb_frontend* fe)
{
	struct dst_state* state = (struct dst_state*) fe->demodulator_priv;
	int retval;
	u8 reply;

	dprintk("%s: type_flags 0x%x \n", __FUNCTION__, state->type_flags);
	state->decode_freq = 0;
	state->decode_lock = state->decode_strength = state->decode_snr = 0;
	if (state->dst_type == DST_TYPE_IS_SAT) {
		if (!(state->diseq_flags & HAS_POWER))
			dst_set_voltage(fe, SEC_VOLTAGE_13);
	}
	state->diseq_flags &= ~(HAS_LOCK | ATTEMPT_TUNE);
	dst_i2c_enable(state);
	if (state->type_flags & DST_TYPE_HAS_NEWTUNE) {
		dst_reset8820(state);
		state->tx_tuna[9] = dst_check_sum(&state->tx_tuna[0], 9);
		retval = write_dst(state, &state->tx_tuna[0], 10);
	} else {
		state->tx_tuna[9] = dst_check_sum(&state->tx_tuna[2], 7);
		retval = write_dst(state, &state->tx_tuna[2], 8);
	}
	if (retval < 0) {
		dst_i2c_disable(state);
		dprintk("%s: write not successful\n", __FUNCTION__);
		return retval;
	}
	msleep(3);
	retval = read_dst(state, &reply, 1);
	dst_i2c_disable(state);
	if (retval < 0) {
		dprintk("%s: read verify  not successful\n", __FUNCTION__);
		return retval;
	}
	if (reply != 0xff) {
		dprintk("%s: write reply not 0xff 0x%02x \n", __FUNCTION__, reply);
		return 0;
	}
	state->diseq_flags |= ATTEMPT_TUNE;
	return dst_get_tuna(state);
}

/*
 * line22k0    0x00, 0x09, 0x00, 0xff, 0x01, 0x00, 0x00, 0x00
 * line22k1    0x00, 0x09, 0x01, 0xff, 0x01, 0x00, 0x00, 0x00
 * line22k2    0x00, 0x09, 0x02, 0xff, 0x01, 0x00, 0x00, 0x00
 * tone        0x00, 0x09, 0xff, 0x00, 0x01, 0x00, 0x00, 0x00
 * data        0x00, 0x09, 0xff, 0x01, 0x01, 0x00, 0x00, 0x00
 * power_off   0x00, 0x09, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00
 * power_on    0x00, 0x09, 0xff, 0xff, 0x01, 0x00, 0x00, 0x00
 * Diseqc 1    0x00, 0x08, 0x04, 0xe0, 0x10, 0x38, 0xf0, 0xec
 * Diseqc 2    0x00, 0x08, 0x04, 0xe0, 0x10, 0x38, 0xf4, 0xe8
 * Diseqc 3    0x00, 0x08, 0x04, 0xe0, 0x10, 0x38, 0xf8, 0xe4
 * Diseqc 4    0x00, 0x08, 0x04, 0xe0, 0x10, 0x38, 0xfc, 0xe0
 */

static int dst_set_diseqc(struct dvb_frontend* fe, struct dvb_diseqc_master_cmd* cmd)
{
	struct dst_state* state = (struct dst_state*) fe->demodulator_priv;
	u8 paket[8] = { 0x00, 0x08, 0x04, 0xe0, 0x10, 0x38, 0xf0, 0xec };

	if (state->dst_type == DST_TYPE_IS_TERR)
		return 0;

	if (cmd->msg_len == 0 || cmd->msg_len > 4)
		return -EINVAL;
	memcpy(&paket[3], cmd->msg, cmd->msg_len);
	paket[7] = dst_check_sum(&paket[0], 7);
	dst_command(state, paket, 8);
	return 0;
}

static int dst_set_voltage(struct dvb_frontend* fe, fe_sec_voltage_t voltage)
{
	u8 *val;
	int need_cmd;
	struct dst_state* state = (struct dst_state*) fe->demodulator_priv;

	state->voltage = voltage;

	if (state->dst_type == DST_TYPE_IS_TERR)
		return 0;

	need_cmd = 0;
	val = &state->tx_tuna[0];
	val[8] &= ~0x40;
	switch (voltage) {
	case SEC_VOLTAGE_13:
		if ((state->diseq_flags & HAS_POWER) == 0)
			need_cmd = 1;
		state->diseq_flags |= HAS_POWER;
		break;
	case SEC_VOLTAGE_18:
		if ((state->diseq_flags & HAS_POWER) == 0)
			need_cmd = 1;
		state->diseq_flags |= HAS_POWER;
		val[8] |= 0x40;
		break;
	case SEC_VOLTAGE_OFF:
		need_cmd = 1;
		state->diseq_flags &= ~(HAS_POWER | HAS_LOCK | ATTEMPT_TUNE);
		break;
	default:
		return -EINVAL;
	}
	if (need_cmd) {
		dst_tone_power_cmd(state);
	}
	return 0;
}

static int dst_set_tone(struct dvb_frontend* fe, fe_sec_tone_mode_t tone)
{
	u8 *val;
	struct dst_state* state = (struct dst_state*) fe->demodulator_priv;

	state->tone = tone;

	if (state->dst_type == DST_TYPE_IS_TERR)
		return 0;

	val = &state->tx_tuna[0];

	val[8] &= ~0x1;

	switch (tone) {
	case SEC_TONE_OFF:
		break;
	case SEC_TONE_ON:
		val[8] |= 1;
		break;
	default:
		return -EINVAL;
	}
	dst_tone_power_cmd(state);
	return 0;
}

static int dst_init(struct dvb_frontend* fe)
{
	struct dst_state* state = (struct dst_state*) fe->demodulator_priv;
	static u8 ini_satci_tuna[] = { 9, 0, 3, 0xb6, 1, 0, 0x73, 0x21, 0, 0 };
	static u8 ini_satfta_tuna[] = { 0, 0, 3, 0xb6, 1, 0x55, 0xbd, 0x50, 0, 0 };
	static u8 ini_tvfta_tuna[] = { 0, 0, 3, 0xb6, 1, 7, 0x0, 0x0, 0, 0 };
	static u8 ini_tvci_tuna[] = { 9, 0, 3, 0xb6, 1, 7, 0x0, 0x0, 0, 0 };
	static u8 ini_cabfta_tuna[] = { 0, 0, 3, 0xb6, 1, 7, 0x0, 0x0, 0, 0 };
	static u8 ini_cabci_tuna[] = { 9, 0, 3, 0xb6, 1, 7, 0x0, 0x0, 0, 0 };
	state->inversion = INVERSION_ON;
	state->voltage = SEC_VOLTAGE_13;
	state->tone = SEC_TONE_OFF;
	state->symbol_rate = 29473000;
	state->fec = FEC_AUTO;
	state->diseq_flags = 0;
	state->k22 = 0x02;
	state->bandwidth = BANDWIDTH_7_MHZ;
	state->cur_jiff = jiffies;
	if (state->dst_type == DST_TYPE_IS_SAT) {
		state->frequency = 950000;
		memcpy(state->tx_tuna, ((state->type_flags & DST_TYPE_HAS_NEWTUNE) ? ini_satci_tuna : ini_satfta_tuna), sizeof(ini_satfta_tuna));
	} else if (state->dst_type == DST_TYPE_IS_TERR) {
		state->frequency = 137000000;
		memcpy(state->tx_tuna, ((state->type_flags & DST_TYPE_HAS_NEWTUNE) ? ini_tvci_tuna : ini_tvfta_tuna), sizeof(ini_tvfta_tuna));
	} else if (state->dst_type == DST_TYPE_IS_CABLE) {
		state->frequency = 51000000;
		memcpy(state->tx_tuna, ((state->type_flags & DST_TYPE_HAS_NEWTUNE) ? ini_cabci_tuna : ini_cabfta_tuna), sizeof(ini_cabfta_tuna));
	}

	return 0;
}

static int dst_read_status(struct dvb_frontend* fe, fe_status_t* status)
{
	struct dst_state* state = (struct dst_state*) fe->demodulator_priv;

	*status = 0;
	if (state->diseq_flags & HAS_LOCK) {
		dst_get_signal(state);
		if (state->decode_lock)
			*status |= FE_HAS_LOCK | FE_HAS_SIGNAL | FE_HAS_CARRIER | FE_HAS_SYNC | FE_HAS_VITERBI;
	}

	return 0;
}

static int dst_read_signal_strength(struct dvb_frontend* fe, u16* strength)
{
	struct dst_state* state = (struct dst_state*) fe->demodulator_priv;

	dst_get_signal(state);
	*strength = state->decode_strength;

	return 0;
}

static int dst_read_snr(struct dvb_frontend* fe, u16* snr)
{
	struct dst_state* state = (struct dst_state*) fe->demodulator_priv;

	dst_get_signal(state);
	*snr = state->decode_snr;

	return 0;
}

static int dst_set_frontend(struct dvb_frontend* fe, struct dvb_frontend_parameters *p)
{
	struct dst_state* state = (struct dst_state*) fe->demodulator_priv;

	dst_set_freq(state, p->frequency);
	dst_set_inversion(state, p->inversion);
	if (state->dst_type == DST_TYPE_IS_SAT) {
		dst_set_fec(state, p->u.qpsk.fec_inner);
		dst_set_symbolrate(state, p->u.qpsk.symbol_rate);
	} else if (state->dst_type == DST_TYPE_IS_TERR) {
		dst_set_bandwidth(state, p->u.ofdm.bandwidth);
	} else if (state->dst_type == DST_TYPE_IS_CABLE) {
		dst_set_fec(state, p->u.qam.fec_inner);
		dst_set_symbolrate(state, p->u.qam.symbol_rate);
	}
	dst_write_tuna(fe);

	return 0;
}

static int dst_get_frontend(struct dvb_frontend* fe, struct dvb_frontend_parameters *p)
{
	struct dst_state* state = (struct dst_state*) fe->demodulator_priv;

	p->frequency = state->decode_freq;
	p->inversion = state->inversion;
	if (state->dst_type == DST_TYPE_IS_SAT) {
		p->u.qpsk.symbol_rate = state->symbol_rate;
		p->u.qpsk.fec_inner = dst_get_fec(state);
	} else if (state->dst_type == DST_TYPE_IS_TERR) {
		p->u.ofdm.bandwidth = state->bandwidth;
	} else if (state->dst_type == DST_TYPE_IS_CABLE) {
		p->u.qam.symbol_rate = state->symbol_rate;
		p->u.qam.fec_inner = dst_get_fec(state);
		p->u.qam.modulation = QAM_AUTO;
	}

	return 0;
}

static void dst_release(struct dvb_frontend* fe)
{
	struct dst_state* state = (struct dst_state*) fe->demodulator_priv;
	kfree(state);
}

static struct dvb_frontend_ops dst_dvbt_ops;
static struct dvb_frontend_ops dst_dvbs_ops;
static struct dvb_frontend_ops dst_dvbc_ops;

struct dvb_frontend* dst_attach(const struct dst_config* config,
				struct i2c_adapter* i2c,
				struct bt878 *bt)
{
	struct dst_state* state = NULL;

	/* allocate memory for the internal state */
	state = (struct dst_state*) kmalloc(sizeof(struct dst_state), GFP_KERNEL);
	if (state == NULL) goto error;

	/* setup the state */
	state->config = config;
	state->i2c = i2c;
	state->bt = bt;

	/* check if the demod is there */
	if (dst_check_ci(state) < 0) goto error;

	/* determine settings based on type */
	switch (state->dst_type) {
	case DST_TYPE_IS_TERR:
		memcpy(&state->ops, &dst_dvbt_ops, sizeof(struct dvb_frontend_ops));
		break;
	case DST_TYPE_IS_CABLE:
		memcpy(&state->ops, &dst_dvbc_ops, sizeof(struct dvb_frontend_ops));
		break;
	case DST_TYPE_IS_SAT:
		memcpy(&state->ops, &dst_dvbs_ops, sizeof(struct dvb_frontend_ops));
		break;
	default:
		printk("dst: unknown frontend type. please report to the LinuxTV.org DVB mailinglist.\n");
		goto error;
	}

	/* create dvb_frontend */
	state->frontend.ops = &state->ops;
	state->frontend.demodulator_priv = state;
	return &state->frontend;

error:
	kfree(state);
	return NULL;
}

static struct dvb_frontend_ops dst_dvbt_ops = {

	.info = {
		.name = "DST DVB-T",
		.type = FE_OFDM,
		.frequency_min = 137000000,
		.frequency_max = 858000000,
		.frequency_stepsize = 166667,
		.caps = FE_CAN_FEC_AUTO | FE_CAN_QAM_AUTO | FE_CAN_TRANSMISSION_MODE_AUTO | FE_CAN_GUARD_INTERVAL_AUTO
	},

	.release = dst_release,

	.init = dst_init,

	.set_frontend = dst_set_frontend,
	.get_frontend = dst_get_frontend,

	.read_status = dst_read_status,
	.read_signal_strength = dst_read_signal_strength,
	.read_snr = dst_read_snr,
};

static struct dvb_frontend_ops dst_dvbs_ops = {

	.info = {
		.name = "DST DVB-S",
		.type = FE_QPSK,
		.frequency_min = 950000,
		.frequency_max = 2150000,
		.frequency_stepsize = 1000,	/* kHz for QPSK frontends */
		.frequency_tolerance = 29500,
		.symbol_rate_min = 1000000,
		.symbol_rate_max = 45000000,
	/*     . symbol_rate_tolerance	=	???,*/
		.caps = FE_CAN_FEC_AUTO | FE_CAN_QPSK
	},

	.release = dst_release,

	.init = dst_init,

	.set_frontend = dst_set_frontend,
	.get_frontend = dst_get_frontend,

	.read_status = dst_read_status,
	.read_signal_strength = dst_read_signal_strength,
	.read_snr = dst_read_snr,

	.diseqc_send_master_cmd = dst_set_diseqc,
	.set_voltage = dst_set_voltage,
	.set_tone = dst_set_tone,
};

static struct dvb_frontend_ops dst_dvbc_ops = {

	.info = {
		.name = "DST DVB-C",
		.type = FE_QAM,
		.frequency_stepsize = 62500,
		.frequency_min = 51000000,
		.frequency_max = 858000000,
		.symbol_rate_min = 1000000,
		.symbol_rate_max = 45000000,
	/*     . symbol_rate_tolerance	=	???,*/
		.caps = FE_CAN_FEC_AUTO | FE_CAN_QAM_AUTO
	},

	.release = dst_release,

	.init = dst_init,

	.set_frontend = dst_set_frontend,
	.get_frontend = dst_get_frontend,

	.read_status = dst_read_status,
	.read_signal_strength = dst_read_signal_strength,
	.read_snr = dst_read_snr,
};

MODULE_DESCRIPTION("DST DVB-S/T/C Combo Frontend driver");
MODULE_AUTHOR("Jamie Honan");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL(dst_attach);
