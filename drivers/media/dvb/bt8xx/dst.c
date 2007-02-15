/*
	Frontend/Card driver for TwinHan DST Frontend
	Copyright (C) 2003 Jamie Honan
	Copyright (C) 2004, 2005 Manu Abraham (manu@kromtek.com)

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
#include "dst_common.h"

static unsigned int verbose = 1;
module_param(verbose, int, 0644);
MODULE_PARM_DESC(verbose, "verbose startup messages, default is 1 (yes)");

static unsigned int dst_addons;
module_param(dst_addons, int, 0644);
MODULE_PARM_DESC(dst_addons, "CA daughterboard, default is 0 (No addons)");

static unsigned int dst_algo;
module_param(dst_algo, int, 0644);
MODULE_PARM_DESC(dst_algo, "tuning algo: default is 0=(SW), 1=(HW)");

#define HAS_LOCK		1
#define ATTEMPT_TUNE		2
#define HAS_POWER		4

#define DST_ERROR		0
#define DST_NOTICE		1
#define DST_INFO		2
#define DST_DEBUG		3

#define dprintk(x, y, z, format, arg...) do {				\
	if (z) {							\
		if	((x > DST_ERROR) && (x > y))			\
			printk(KERN_ERR "dst(%d) %s: " format "\n",	\
				state->bt->nr, __func__ , ##arg);	\
		else if	((x > DST_NOTICE) && (x > y))			\
			printk(KERN_NOTICE "dst(%d) %s: " format "\n",  \
				state->bt->nr, __func__ , ##arg);	\
		else if ((x > DST_INFO) && (x > y))			\
			printk(KERN_INFO "dst(%d) %s: " format "\n",	\
				state->bt->nr, __func__ , ##arg);	\
		else if ((x > DST_DEBUG) && (x > y))			\
			printk(KERN_DEBUG "dst(%d) %s: " format "\n",	\
				state->bt->nr,  __func__ , ##arg);	\
	} else {							\
		if (x > y)						\
			printk(format, ##arg);				\
	}								\
} while(0)


static void dst_packsize(struct dst_state *state, int psize)
{
	union dst_gpio_packet bits;

	bits.psize = psize;
	bt878_device_control(state->bt, DST_IG_TS, &bits);
}

int dst_gpio_outb(struct dst_state *state, u32 mask, u32 enbb, u32 outhigh, int delay)
{
	union dst_gpio_packet enb;
	union dst_gpio_packet bits;
	int err;

	enb.enb.mask = mask;
	enb.enb.enable = enbb;

	dprintk(verbose, DST_INFO, 1, "mask=[%04x], enbb=[%04x], outhigh=[%04x]", mask, enbb, outhigh);
	if ((err = bt878_device_control(state->bt, DST_IG_ENABLE, &enb)) < 0) {
		dprintk(verbose, DST_INFO, 1, "dst_gpio_enb error (err == %i, mask == %02x, enb == %02x)", err, mask, enbb);
		return -EREMOTEIO;
	}
	udelay(1000);
	/* because complete disabling means no output, no need to do output packet */
	if (enbb == 0)
		return 0;
	if (delay)
		msleep(10);
	bits.outp.mask = enbb;
	bits.outp.highvals = outhigh;
	if ((err = bt878_device_control(state->bt, DST_IG_WRITE, &bits)) < 0) {
		dprintk(verbose, DST_INFO, 1, "dst_gpio_outb error (err == %i, enbb == %02x, outhigh == %02x)", err, enbb, outhigh);
		return -EREMOTEIO;
	}

	return 0;
}
EXPORT_SYMBOL(dst_gpio_outb);

int dst_gpio_inb(struct dst_state *state, u8 *result)
{
	union dst_gpio_packet rd_packet;
	int err;

	*result = 0;
	if ((err = bt878_device_control(state->bt, DST_IG_READ, &rd_packet)) < 0) {
		dprintk(verbose, DST_ERROR, 1, "dst_gpio_inb error (err == %i)", err);
		return -EREMOTEIO;
	}
	*result = (u8) rd_packet.rd.value;

	return 0;
}
EXPORT_SYMBOL(dst_gpio_inb);

int rdc_reset_state(struct dst_state *state)
{
	dprintk(verbose, DST_INFO, 1, "Resetting state machine");
	if (dst_gpio_outb(state, RDC_8820_INT, RDC_8820_INT, 0, NO_DELAY) < 0) {
		dprintk(verbose, DST_ERROR, 1, "dst_gpio_outb ERROR !");
		return -1;
	}
	msleep(10);
	if (dst_gpio_outb(state, RDC_8820_INT, RDC_8820_INT, RDC_8820_INT, NO_DELAY) < 0) {
		dprintk(verbose, DST_ERROR, 1, "dst_gpio_outb ERROR !");
		msleep(10);
		return -1;
	}

	return 0;
}
EXPORT_SYMBOL(rdc_reset_state);

int rdc_8820_reset(struct dst_state *state)
{
	dprintk(verbose, DST_DEBUG, 1, "Resetting DST");
	if (dst_gpio_outb(state, RDC_8820_RESET, RDC_8820_RESET, 0, NO_DELAY) < 0) {
		dprintk(verbose, DST_ERROR, 1, "dst_gpio_outb ERROR !");
		return -1;
	}
	udelay(1000);
	if (dst_gpio_outb(state, RDC_8820_RESET, RDC_8820_RESET, RDC_8820_RESET, DELAY) < 0) {
		dprintk(verbose, DST_ERROR, 1, "dst_gpio_outb ERROR !");
		return -1;
	}

	return 0;
}
EXPORT_SYMBOL(rdc_8820_reset);

int dst_pio_enable(struct dst_state *state)
{
	if (dst_gpio_outb(state, ~0, RDC_8820_PIO_0_ENABLE, 0, NO_DELAY) < 0) {
		dprintk(verbose, DST_ERROR, 1, "dst_gpio_outb ERROR !");
		return -1;
	}
	udelay(1000);

	return 0;
}
EXPORT_SYMBOL(dst_pio_enable);

int dst_pio_disable(struct dst_state *state)
{
	if (dst_gpio_outb(state, ~0, RDC_8820_PIO_0_DISABLE, RDC_8820_PIO_0_DISABLE, NO_DELAY) < 0) {
		dprintk(verbose, DST_ERROR, 1, "dst_gpio_outb ERROR !");
		return -1;
	}
	if (state->type_flags & DST_TYPE_HAS_FW_1)
		udelay(1000);

	return 0;
}
EXPORT_SYMBOL(dst_pio_disable);

int dst_wait_dst_ready(struct dst_state *state, u8 delay_mode)
{
	u8 reply;
	int i;

	for (i = 0; i < 200; i++) {
		if (dst_gpio_inb(state, &reply) < 0) {
			dprintk(verbose, DST_ERROR, 1, "dst_gpio_inb ERROR !");
			return -1;
		}
		if ((reply & RDC_8820_PIO_0_ENABLE) == 0) {
			dprintk(verbose, DST_INFO, 1, "dst wait ready after %d", i);
			return 1;
		}
		msleep(10);
	}
	dprintk(verbose, DST_NOTICE, 1, "dst wait NOT ready after %d", i);

	return 0;
}
EXPORT_SYMBOL(dst_wait_dst_ready);

int dst_error_recovery(struct dst_state *state)
{
	dprintk(verbose, DST_NOTICE, 1, "Trying to return from previous errors.");
	dst_pio_disable(state);
	msleep(10);
	dst_pio_enable(state);
	msleep(10);

	return 0;
}
EXPORT_SYMBOL(dst_error_recovery);

int dst_error_bailout(struct dst_state *state)
{
	dprintk(verbose, DST_INFO, 1, "Trying to bailout from previous error.");
	rdc_8820_reset(state);
	dst_pio_disable(state);
	msleep(10);

	return 0;
}
EXPORT_SYMBOL(dst_error_bailout);

int dst_comm_init(struct dst_state *state)
{
	dprintk(verbose, DST_INFO, 1, "Initializing DST.");
	if ((dst_pio_enable(state)) < 0) {
		dprintk(verbose, DST_ERROR, 1, "PIO Enable Failed");
		return -1;
	}
	if ((rdc_reset_state(state)) < 0) {
		dprintk(verbose, DST_ERROR, 1, "RDC 8820 State RESET Failed.");
		return -1;
	}
	if (state->type_flags & DST_TYPE_HAS_FW_1)
		msleep(100);
	else
		msleep(5);

	return 0;
}
EXPORT_SYMBOL(dst_comm_init);

int write_dst(struct dst_state *state, u8 *data, u8 len)
{
	struct i2c_msg msg = {
		.addr = state->config->demod_address,
		.flags = 0,
		.buf = data,
		.len = len
	};

	int err;
	u8 cnt, i;

	dprintk(verbose, DST_NOTICE, 0, "writing [ ");
	for (i = 0; i < len; i++)
		dprintk(verbose, DST_NOTICE, 0, "%02x ", data[i]);
	dprintk(verbose, DST_NOTICE, 0, "]\n");

	for (cnt = 0; cnt < 2; cnt++) {
		if ((err = i2c_transfer(state->i2c, &msg, 1)) < 0) {
			dprintk(verbose, DST_INFO, 1, "_write_dst error (err == %i, len == 0x%02x, b0 == 0x%02x)", err, len, data[0]);
			dst_error_recovery(state);
			continue;
		} else
			break;
	}
	if (cnt >= 2) {
		dprintk(verbose, DST_INFO, 1, "RDC 8820 RESET");
		dst_error_bailout(state);

		return -1;
	}

	return 0;
}
EXPORT_SYMBOL(write_dst);

int read_dst(struct dst_state *state, u8 *ret, u8 len)
{
	struct i2c_msg msg = {
		.addr = state->config->demod_address,
		.flags = I2C_M_RD,
		.buf = ret,
		.len = len
	};

	int err;
	int cnt;

	for (cnt = 0; cnt < 2; cnt++) {
		if ((err = i2c_transfer(state->i2c, &msg, 1)) < 0) {
			dprintk(verbose, DST_INFO, 1, "read_dst error (err == %i, len == 0x%02x, b0 == 0x%02x)", err, len, ret[0]);
			dst_error_recovery(state);
			continue;
		} else
			break;
	}
	if (cnt >= 2) {
		dprintk(verbose, DST_INFO, 1, "RDC 8820 RESET");
		dst_error_bailout(state);

		return -1;
	}
	dprintk(verbose, DST_DEBUG, 1, "reply is 0x%x", ret[0]);
	for (err = 1; err < len; err++)
		dprintk(verbose, DST_DEBUG, 0, " 0x%x", ret[err]);
	if (err > 1)
		dprintk(verbose, DST_DEBUG, 0, "\n");

	return 0;
}
EXPORT_SYMBOL(read_dst);

static int dst_set_polarization(struct dst_state *state)
{
	switch (state->voltage) {
	case SEC_VOLTAGE_13:	/*	Vertical	*/
		dprintk(verbose, DST_INFO, 1, "Polarization=[Vertical]");
		state->tx_tuna[8] &= ~0x40;
		break;
	case SEC_VOLTAGE_18:	/*	Horizontal	*/
		dprintk(verbose, DST_INFO, 1, "Polarization=[Horizontal]");
		state->tx_tuna[8] |= 0x40;
		break;
	case SEC_VOLTAGE_OFF:
		break;
	}

	return 0;
}

static int dst_set_freq(struct dst_state *state, u32 freq)
{
	state->frequency = freq;
	dprintk(verbose, DST_INFO, 1, "set Frequency %u", freq);

	if (state->dst_type == DST_TYPE_IS_SAT) {
		freq = freq / 1000;
		if (freq < 950 || freq > 2150)
			return -EINVAL;
		state->tx_tuna[2] = (freq >> 8);
		state->tx_tuna[3] = (u8) freq;
		state->tx_tuna[4] = 0x01;
		state->tx_tuna[8] &= ~0x04;
		if (state->type_flags & DST_TYPE_HAS_OBS_REGS) {
			if (freq < 1531)
				state->tx_tuna[8] |= 0x04;
		}
	} else if (state->dst_type == DST_TYPE_IS_TERR) {
		freq = freq / 1000;
		if (freq < 137000 || freq > 858000)
			return -EINVAL;
		state->tx_tuna[2] = (freq >> 16) & 0xff;
		state->tx_tuna[3] = (freq >> 8) & 0xff;
		state->tx_tuna[4] = (u8) freq;
	} else if (state->dst_type == DST_TYPE_IS_CABLE) {
		freq = freq / 1000;
		state->tx_tuna[2] = (freq >> 16) & 0xff;
		state->tx_tuna[3] = (freq >> 8) & 0xff;
		state->tx_tuna[4] = (u8) freq;
	} else if (state->dst_type == DST_TYPE_IS_ATSC) {
		freq = freq / 1000;
		if (freq < 51000 || freq > 858000)
			return -EINVAL;
		state->tx_tuna[2] = (freq >> 16) & 0xff;
		state->tx_tuna[3] = (freq >>  8) & 0xff;
		state->tx_tuna[4] = (u8) freq;
		state->tx_tuna[5] = 0x00;		/*	ATSC	*/
		state->tx_tuna[6] = 0x00;
		if (state->dst_hw_cap & DST_TYPE_HAS_ANALOG)
			state->tx_tuna[7] = 0x00;	/*	Digital	*/
	} else
		return -EINVAL;

	return 0;
}

static int dst_set_bandwidth(struct dst_state *state, fe_bandwidth_t bandwidth)
{
	state->bandwidth = bandwidth;

	if (state->dst_type != DST_TYPE_IS_TERR)
		return -EOPNOTSUPP;

	switch (bandwidth) {
	case BANDWIDTH_6_MHZ:
		if (state->dst_hw_cap & DST_TYPE_HAS_CA)
			state->tx_tuna[7] = 0x06;
		else {
			state->tx_tuna[6] = 0x06;
			state->tx_tuna[7] = 0x00;
		}
		break;
	case BANDWIDTH_7_MHZ:
		if (state->dst_hw_cap & DST_TYPE_HAS_CA)
			state->tx_tuna[7] = 0x07;
		else {
			state->tx_tuna[6] = 0x07;
			state->tx_tuna[7] = 0x00;
		}
		break;
	case BANDWIDTH_8_MHZ:
		if (state->dst_hw_cap & DST_TYPE_HAS_CA)
			state->tx_tuna[7] = 0x08;
		else {
			state->tx_tuna[6] = 0x08;
			state->tx_tuna[7] = 0x00;
		}
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int dst_set_inversion(struct dst_state *state, fe_spectral_inversion_t inversion)
{
	state->inversion = inversion;
	switch (inversion) {
	case INVERSION_OFF:	/*	Inversion = Normal	*/
		state->tx_tuna[8] &= ~0x80;
		break;
	case INVERSION_ON:
		state->tx_tuna[8] |= 0x80;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int dst_set_fec(struct dst_state *state, fe_code_rate_t fec)
{
	state->fec = fec;
	return 0;
}

static fe_code_rate_t dst_get_fec(struct dst_state *state)
{
	return state->fec;
}

static int dst_set_symbolrate(struct dst_state *state, u32 srate)
{
	u32 symcalc;
	u64 sval;

	state->symbol_rate = srate;
	if (state->dst_type == DST_TYPE_IS_TERR) {
		return -EOPNOTSUPP;
	}
	dprintk(verbose, DST_INFO, 1, "set symrate %u", srate);
	srate /= 1000;
	if (state->dst_type == DST_TYPE_IS_SAT) {
		if (state->type_flags & DST_TYPE_HAS_SYMDIV) {
			sval = srate;
			sval <<= 20;
			do_div(sval, 88000);
			symcalc = (u32) sval;
			dprintk(verbose, DST_INFO, 1, "set symcalc %u", symcalc);
			state->tx_tuna[5] = (u8) (symcalc >> 12);
			state->tx_tuna[6] = (u8) (symcalc >> 4);
			state->tx_tuna[7] = (u8) (symcalc << 4);
		} else {
			state->tx_tuna[5] = (u8) (srate >> 16) & 0x7f;
			state->tx_tuna[6] = (u8) (srate >> 8);
			state->tx_tuna[7] = (u8) srate;
		}
		state->tx_tuna[8] &= ~0x20;
		if (state->type_flags & DST_TYPE_HAS_OBS_REGS) {
			if (srate > 8000)
				state->tx_tuna[8] |= 0x20;
		}
	} else if (state->dst_type == DST_TYPE_IS_CABLE) {
		dprintk(verbose, DST_DEBUG, 1, "%s", state->fw_name);
		if (!strncmp(state->fw_name, "DCTNEW", 6)) {
			state->tx_tuna[5] = (u8) (srate >> 8);
			state->tx_tuna[6] = (u8) srate;
			state->tx_tuna[7] = 0x00;
		} else if (!strncmp(state->fw_name, "DCT-CI", 6)) {
			state->tx_tuna[5] = 0x00;
			state->tx_tuna[6] = (u8) (srate >> 8);
			state->tx_tuna[7] = (u8) srate;
		}
	}
	return 0;
}

static int dst_set_modulation(struct dst_state *state, fe_modulation_t modulation)
{
	if (state->dst_type != DST_TYPE_IS_CABLE)
		return -EOPNOTSUPP;

	state->modulation = modulation;
	switch (modulation) {
	case QAM_16:
		state->tx_tuna[8] = 0x10;
		break;
	case QAM_32:
		state->tx_tuna[8] = 0x20;
		break;
	case QAM_64:
		state->tx_tuna[8] = 0x40;
		break;
	case QAM_128:
		state->tx_tuna[8] = 0x80;
		break;
	case QAM_256:
		if (!strncmp(state->fw_name, "DCTNEW", 6))
			state->tx_tuna[8] = 0xff;
		else if (!strncmp(state->fw_name, "DCT-CI", 6))
			state->tx_tuna[8] = 0x00;
		break;
	case QPSK:
	case QAM_AUTO:
	case VSB_8:
	case VSB_16:
	default:
		return -EINVAL;

	}

	return 0;
}

static fe_modulation_t dst_get_modulation(struct dst_state *state)
{
	return state->modulation;
}


u8 dst_check_sum(u8 *buf, u32 len)
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
EXPORT_SYMBOL(dst_check_sum);

static void dst_type_flags_print(struct dst_state *state)
{
	u32 type_flags = state->type_flags;

	dprintk(verbose, DST_ERROR, 0, "DST type flags :");
	if (type_flags & DST_TYPE_HAS_TS188)
		dprintk(verbose, DST_ERROR, 0, " 0x%x newtuner", DST_TYPE_HAS_TS188);
	if (type_flags & DST_TYPE_HAS_NEWTUNE_2)
		dprintk(verbose, DST_ERROR, 0, " 0x%x newtuner 2", DST_TYPE_HAS_NEWTUNE_2);
	if (type_flags & DST_TYPE_HAS_TS204)
		dprintk(verbose, DST_ERROR, 0, " 0x%x ts204", DST_TYPE_HAS_TS204);
	if (type_flags & DST_TYPE_HAS_VLF)
		dprintk(verbose, DST_ERROR, 0, " 0x%x VLF", DST_TYPE_HAS_VLF);
	if (type_flags & DST_TYPE_HAS_SYMDIV)
		dprintk(verbose, DST_ERROR, 0, " 0x%x symdiv", DST_TYPE_HAS_SYMDIV);
	if (type_flags & DST_TYPE_HAS_FW_1)
		dprintk(verbose, DST_ERROR, 0, " 0x%x firmware version = 1", DST_TYPE_HAS_FW_1);
	if (type_flags & DST_TYPE_HAS_FW_2)
		dprintk(verbose, DST_ERROR, 0, " 0x%x firmware version = 2", DST_TYPE_HAS_FW_2);
	if (type_flags & DST_TYPE_HAS_FW_3)
		dprintk(verbose, DST_ERROR, 0, " 0x%x firmware version = 3", DST_TYPE_HAS_FW_3);
	dprintk(verbose, DST_ERROR, 0, "\n");
}


static int dst_type_print(struct dst_state *state, u8 type)
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

	case DST_TYPE_IS_ATSC:
		otype = "atsc";
		break;

	default:
		dprintk(verbose, DST_INFO, 1, "invalid dst type %d", type);
		return -EINVAL;
	}
	dprintk(verbose, DST_INFO, 1, "DST type: %s", otype);

	return 0;
}

struct tuner_types tuner_list[] = {
	{
		.tuner_type = TUNER_TYPE_L64724,
		.tuner_name = "L 64724",
		.board_name = "UNKNOWN",
		.fw_name    = "UNKNOWN"
	},

	{
		.tuner_type = TUNER_TYPE_STV0299,
		.tuner_name = "STV 0299",
		.board_name = "VP1020",
		.fw_name    = "DST-MOT"
	},

	{
		.tuner_type = TUNER_TYPE_STV0299,
		.tuner_name = "STV 0299",
		.board_name = "VP1020",
		.fw_name    = "DST-03T"
	},

	{
		.tuner_type = TUNER_TYPE_MB86A15,
		.tuner_name = "MB 86A15",
		.board_name = "VP1022",
		.fw_name    = "DST-03T"
	},

	{
		.tuner_type = TUNER_TYPE_MB86A15,
		.tuner_name = "MB 86A15",
		.board_name = "VP1025",
		.fw_name    = "DST-03T"
	},

	{
		.tuner_type = TUNER_TYPE_STV0299,
		.tuner_name = "STV 0299",
		.board_name = "VP1030",
		.fw_name    = "DST-CI"
	},

	{
		.tuner_type = TUNER_TYPE_STV0299,
		.tuner_name = "STV 0299",
		.board_name = "VP1030",
		.fw_name    = "DSTMCI"
	},

	{
		.tuner_type = TUNER_TYPE_UNKNOWN,
		.tuner_name = "UNKNOWN",
		.board_name = "VP2021",
		.fw_name    = "DCTNEW"
	},

	{
		.tuner_type = TUNER_TYPE_UNKNOWN,
		.tuner_name = "UNKNOWN",
		.board_name = "VP2030",
		.fw_name    = "DCT-CI"
	},

	{
		.tuner_type = TUNER_TYPE_UNKNOWN,
		.tuner_name = "UNKNOWN",
		.board_name = "VP2031",
		.fw_name    = "DCT-CI"
	},

	{
		.tuner_type = TUNER_TYPE_UNKNOWN,
		.tuner_name = "UNKNOWN",
		.board_name = "VP2040",
		.fw_name    = "DCT-CI"
	},

	{
		.tuner_type = TUNER_TYPE_UNKNOWN,
		.tuner_name = "UNKNOWN",
		.board_name = "VP3020",
		.fw_name    = "DTTFTA"
	},

	{
		.tuner_type = TUNER_TYPE_UNKNOWN,
		.tuner_name = "UNKNOWN",
		.board_name = "VP3021",
		.fw_name    = "DTTFTA"
	},

	{
		.tuner_type = TUNER_TYPE_TDA10046,
		.tuner_name = "TDA10046",
		.board_name = "VP3040",
		.fw_name    = "DTT-CI"
	},

	{
		.tuner_type = TUNER_TYPE_UNKNOWN,
		.tuner_name = "UNKNOWN",
		.board_name = "VP3051",
		.fw_name    = "DTTNXT"
	},

	{
		.tuner_type = TUNER_TYPE_NXT200x,
		.tuner_name = "NXT200x",
		.board_name = "VP3220",
		.fw_name    = "ATSCDI"
	},

	{
		.tuner_type = TUNER_TYPE_NXT200x,
		.tuner_name = "NXT200x",
		.board_name = "VP3250",
		.fw_name    = "ATSCAD"
	},
};

/*
	Known cards list
	Satellite
	-------------------
		  200103A
	VP-1020   DST-MOT	LG(old), TS=188

	VP-1020   DST-03T	LG(new), TS=204
	VP-1022   DST-03T	LG(new), TS=204
	VP-1025   DST-03T	LG(new), TS=204

	VP-1030   DSTMCI,	LG(new), TS=188
	VP-1032   DSTMCI,	LG(new), TS=188

	Cable
	-------------------
	VP-2030   DCT-CI,	Samsung, TS=204
	VP-2021   DCT-CI,	Unknown, TS=204
	VP-2031   DCT-CI,	Philips, TS=188
	VP-2040   DCT-CI,	Philips, TS=188, with CA daughter board
	VP-2040   DCT-CI,	Philips, TS=204, without CA daughter board

	Terrestrial
	-------------------
	VP-3050  DTTNXT			 TS=188
	VP-3040  DTT-CI,	Philips, TS=188
	VP-3040  DTT-CI,	Philips, TS=204

	ATSC
	-------------------
	VP-3220  ATSCDI,		 TS=188
	VP-3250  ATSCAD,		 TS=188

*/

static struct dst_types dst_tlist[] = {
	{
		.device_id = "200103A",
		.offset = 0,
		.dst_type =  DST_TYPE_IS_SAT,
		.type_flags = DST_TYPE_HAS_SYMDIV | DST_TYPE_HAS_FW_1 | DST_TYPE_HAS_OBS_REGS,
		.dst_feature = 0,
		.tuner_type = 0
	},	/*	obsolete	*/

	{
		.device_id = "DST-020",
		.offset = 0,
		.dst_type =  DST_TYPE_IS_SAT,
		.type_flags = DST_TYPE_HAS_SYMDIV | DST_TYPE_HAS_FW_1,
		.dst_feature = 0,
		.tuner_type = 0
	},	/*	obsolete	*/

	{
		.device_id = "DST-030",
		.offset =  0,
		.dst_type = DST_TYPE_IS_SAT,
		.type_flags = DST_TYPE_HAS_TS204 | DST_TYPE_HAS_TS188 | DST_TYPE_HAS_FW_1,
		.dst_feature = 0,
		.tuner_type = 0
	},	/*	obsolete	*/

	{
		.device_id = "DST-03T",
		.offset = 0,
		.dst_type = DST_TYPE_IS_SAT,
		.type_flags = DST_TYPE_HAS_SYMDIV | DST_TYPE_HAS_TS204 | DST_TYPE_HAS_FW_2,
		.dst_feature = DST_TYPE_HAS_DISEQC3 | DST_TYPE_HAS_DISEQC4 | DST_TYPE_HAS_DISEQC5
							 | DST_TYPE_HAS_MAC | DST_TYPE_HAS_MOTO,
		.tuner_type = TUNER_TYPE_MULTI
	 },

	{
		.device_id = "DST-MOT",
		.offset =  0,
		.dst_type = DST_TYPE_IS_SAT,
		.type_flags = DST_TYPE_HAS_SYMDIV | DST_TYPE_HAS_FW_1,
		.dst_feature = 0,
		.tuner_type = 0
	},	/*	obsolete	*/

	{
		.device_id = "DST-CI",
		.offset = 1,
		.dst_type = DST_TYPE_IS_SAT,
		.type_flags = DST_TYPE_HAS_TS204 | DST_TYPE_HAS_FW_1,
		.dst_feature = DST_TYPE_HAS_CA,
		.tuner_type = 0
	},	/*	An OEM board	*/

	{
		.device_id = "DSTMCI",
		.offset = 1,
		.dst_type = DST_TYPE_IS_SAT,
		.type_flags = DST_TYPE_HAS_TS188 | DST_TYPE_HAS_FW_2 | DST_TYPE_HAS_FW_BUILD | DST_TYPE_HAS_INC_COUNT | DST_TYPE_HAS_VLF,
		.dst_feature = DST_TYPE_HAS_CA | DST_TYPE_HAS_DISEQC3 | DST_TYPE_HAS_DISEQC4
							| DST_TYPE_HAS_MOTO | DST_TYPE_HAS_MAC,
		.tuner_type = TUNER_TYPE_MULTI
	},

	{
		.device_id = "DSTFCI",
		.offset = 1,
		.dst_type = DST_TYPE_IS_SAT,
		.type_flags = DST_TYPE_HAS_TS188 | DST_TYPE_HAS_FW_1,
		.dst_feature = 0,
		.tuner_type = 0
	},	/* unknown to vendor	*/

	{
		.device_id = "DCT-CI",
		.offset = 1,
		.dst_type = DST_TYPE_IS_CABLE,
		.type_flags = DST_TYPE_HAS_MULTI_FE | DST_TYPE_HAS_FW_1	| DST_TYPE_HAS_FW_2 | DST_TYPE_HAS_VLF,
		.dst_feature = DST_TYPE_HAS_CA,
		.tuner_type = 0
	},

	{
		.device_id = "DCTNEW",
		.offset = 1,
		.dst_type = DST_TYPE_IS_CABLE,
		.type_flags = DST_TYPE_HAS_TS188 | DST_TYPE_HAS_FW_3 | DST_TYPE_HAS_FW_BUILD | DST_TYPE_HAS_MULTI_FE,
		.dst_feature = 0,
		.tuner_type = 0
	},

	{
		.device_id = "DTT-CI",
		.offset = 1,
		.dst_type = DST_TYPE_IS_TERR,
		.type_flags = DST_TYPE_HAS_FW_2 | DST_TYPE_HAS_MULTI_FE | DST_TYPE_HAS_VLF,
		.dst_feature = DST_TYPE_HAS_CA,
		.tuner_type = 0
	},

	{
		.device_id = "DTTDIG",
		.offset = 1,
		.dst_type = DST_TYPE_IS_TERR,
		.type_flags = DST_TYPE_HAS_FW_2,
		.dst_feature = 0,
		.tuner_type = 0
	},

	{
		.device_id = "DTTNXT",
		.offset = 1,
		.dst_type = DST_TYPE_IS_TERR,
		.type_flags = DST_TYPE_HAS_FW_2,
		.dst_feature = DST_TYPE_HAS_ANALOG,
		.tuner_type = 0
	},

	{
		.device_id = "ATSCDI",
		.offset = 1,
		.dst_type = DST_TYPE_IS_ATSC,
		.type_flags = DST_TYPE_HAS_FW_2,
		.dst_feature = 0,
		.tuner_type = 0
	},

	{
		.device_id = "ATSCAD",
		.offset = 1,
		.dst_type = DST_TYPE_IS_ATSC,
		.type_flags = DST_TYPE_HAS_MULTI_FE | DST_TYPE_HAS_FW_2 | DST_TYPE_HAS_FW_BUILD,
		.dst_feature = DST_TYPE_HAS_MAC | DST_TYPE_HAS_ANALOG,
		.tuner_type = 0
	},

	{ }

};

static int dst_get_mac(struct dst_state *state)
{
	u8 get_mac[] = { 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	get_mac[7] = dst_check_sum(get_mac, 7);
	if (dst_command(state, get_mac, 8) < 0) {
		dprintk(verbose, DST_INFO, 1, "Unsupported Command");
		return -1;
	}
	memset(&state->mac_address, '\0', 8);
	memcpy(&state->mac_address, &state->rxbuffer, 6);
	dprintk(verbose, DST_ERROR, 1, "MAC Address=[%02x:%02x:%02x:%02x:%02x:%02x]",
		state->mac_address[0], state->mac_address[1], state->mac_address[2],
		state->mac_address[4], state->mac_address[5], state->mac_address[6]);

	return 0;
}

static int dst_fw_ver(struct dst_state *state)
{
	u8 get_ver[] = { 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	get_ver[7] = dst_check_sum(get_ver, 7);
	if (dst_command(state, get_ver, 8) < 0) {
		dprintk(verbose, DST_INFO, 1, "Unsupported Command");
		return -1;
	}
	memset(&state->fw_version, '\0', 8);
	memcpy(&state->fw_version, &state->rxbuffer, 8);
	dprintk(verbose, DST_ERROR, 1, "Firmware Ver = %x.%x Build = %02x, on %x:%x, %x-%x-20%02x",
		state->fw_version[0] >> 4, state->fw_version[0] & 0x0f,
		state->fw_version[1],
		state->fw_version[5], state->fw_version[6],
		state->fw_version[4], state->fw_version[3], state->fw_version[2]);

	return 0;
}

static int dst_card_type(struct dst_state *state)
{
	int j;
	struct tuner_types *p_tuner_list = NULL;

	u8 get_type[] = { 0x00, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	get_type[7] = dst_check_sum(get_type, 7);
	if (dst_command(state, get_type, 8) < 0) {
		dprintk(verbose, DST_INFO, 1, "Unsupported Command");
		return -1;
	}
	memset(&state->card_info, '\0', 8);
	memcpy(&state->card_info, &state->rxbuffer, 7);
	dprintk(verbose, DST_ERROR, 1, "Device Model=[%s]", &state->card_info[0]);

	for (j = 0, p_tuner_list = tuner_list; j < ARRAY_SIZE(tuner_list); j++, p_tuner_list++) {
		if (!strcmp(&state->card_info[0], p_tuner_list->board_name)) {
			state->tuner_type = p_tuner_list->tuner_type;
			dprintk(verbose, DST_ERROR, 1, "DST has [%s] tuner, tuner type=[%d]",
				p_tuner_list->tuner_name, p_tuner_list->tuner_type);
		}
	}

	return 0;
}

static int dst_get_vendor(struct dst_state *state)
{
	u8 get_vendor[] = { 0x00, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	get_vendor[7] = dst_check_sum(get_vendor, 7);
	if (dst_command(state, get_vendor, 8) < 0) {
		dprintk(verbose, DST_INFO, 1, "Unsupported Command");
		return -1;
	}
	memset(&state->vendor, '\0', 8);
	memcpy(&state->vendor, &state->rxbuffer, 7);
	dprintk(verbose, DST_ERROR, 1, "Vendor=[%s]", &state->vendor[0]);

	return 0;
}

static void debug_dst_buffer(struct dst_state *state)
{
	int i;

	if (verbose > 2) {
		printk("%s: [", __func__);
		for (i = 0; i < 8; i++)
			printk(" %02x", state->rxbuffer[i]);
		printk("]\n");
	}
}

static int dst_check_stv0299(struct dst_state *state)
{
	u8 check_stv0299[] = { 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

	check_stv0299[7] = dst_check_sum(check_stv0299, 7);
	if (dst_command(state, check_stv0299, 8) < 0) {
		dprintk(verbose, DST_ERROR, 1, "Cmd=[0x04] failed");
		return -1;
	}
	debug_dst_buffer(state);

	if (memcmp(&check_stv0299, &state->rxbuffer, 8)) {
		dprintk(verbose, DST_ERROR, 1, "Found a STV0299 NIM");
		state->tuner_type = TUNER_TYPE_STV0299;
		return 0;
	}

	return -1;
}

static int dst_check_mb86a15(struct dst_state *state)
{
	u8 check_mb86a15[] = { 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

	check_mb86a15[7] = dst_check_sum(check_mb86a15, 7);
	if (dst_command(state, check_mb86a15, 8) < 0) {
		dprintk(verbose, DST_ERROR, 1, "Cmd=[0x10], failed");
		return -1;
	}
	debug_dst_buffer(state);

	if (memcmp(&check_mb86a15, &state->rxbuffer, 8) < 0) {
		dprintk(verbose, DST_ERROR, 1, "Found a MB86A15 NIM");
		state->tuner_type = TUNER_TYPE_MB86A15;
		return 0;
	}

	return -1;
}

static int dst_get_tuner_info(struct dst_state *state)
{
	u8 get_tuner_1[] = { 0x00, 0x13, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	u8 get_tuner_2[] = { 0x00, 0x0b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

	get_tuner_1[7] = dst_check_sum(get_tuner_1, 7);
	get_tuner_2[7] = dst_check_sum(get_tuner_2, 7);
	dprintk(verbose, DST_ERROR, 1, "DST TYpe = MULTI FE");
	if (state->type_flags & DST_TYPE_HAS_MULTI_FE) {
		if (dst_command(state, get_tuner_1, 8) < 0) {
			dprintk(verbose, DST_INFO, 1, "Cmd=[0x13], Unsupported");
			goto force;
		}
	} else {
		if (dst_command(state, get_tuner_2, 8) < 0) {
			dprintk(verbose, DST_INFO, 1, "Cmd=[0xb], Unsupported");
			goto force;
		}
	}
	memset(&state->board_info, '\0', 8);
	memcpy(&state->board_info, &state->rxbuffer, 8);
	if (state->type_flags & DST_TYPE_HAS_MULTI_FE) {
		dprintk(verbose, DST_ERROR, 1, "DST type has TS=188");
	}
	if (state->board_info[0] == 0xbc) {
		if (state->type_flags != DST_TYPE_IS_ATSC)
			state->type_flags |= DST_TYPE_HAS_TS188;
		else
			state->type_flags |= DST_TYPE_HAS_NEWTUNE_2;

		if (state->board_info[1] == 0x01) {
			state->dst_hw_cap |= DST_TYPE_HAS_DBOARD;
			dprintk(verbose, DST_ERROR, 1, "DST has Daughterboard");
		}
	}

	return 0;
force:
	if (!strncmp(state->fw_name, "DCT-CI", 6)) {
		state->type_flags |= DST_TYPE_HAS_TS204;
		dprintk(verbose, DST_ERROR, 1, "Forcing [%s] to TS188", state->fw_name);
	}

	return -1;
}

static int dst_get_device_id(struct dst_state *state)
{
	u8 reply;

	int i, j;
	struct dst_types *p_dst_type = NULL;
	struct tuner_types *p_tuner_list = NULL;

	u8 use_dst_type = 0;
	u32 use_type_flags = 0;

	static u8 device_type[8] = {0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff};

	state->tuner_type = 0;
	device_type[7] = dst_check_sum(device_type, 7);

	if (write_dst(state, device_type, FIXED_COMM))
		return -1;		/*	Write failed		*/
	if ((dst_pio_disable(state)) < 0)
		return -1;
	if (read_dst(state, &reply, GET_ACK))
		return -1;		/*	Read failure		*/
	if (reply != ACK) {
		dprintk(verbose, DST_INFO, 1, "Write not Acknowledged! [Reply=0x%02x]", reply);
		return -1;		/*	Unack'd write		*/
	}
	if (!dst_wait_dst_ready(state, DEVICE_INIT))
		return -1;		/*	DST not ready yet	*/
	if (read_dst(state, state->rxbuffer, FIXED_COMM))
		return -1;

	dst_pio_disable(state);
	if (state->rxbuffer[7] != dst_check_sum(state->rxbuffer, 7)) {
		dprintk(verbose, DST_INFO, 1, "Checksum failure!");
		return -1;		/*	Checksum failure	*/
	}
	state->rxbuffer[7] = '\0';

	for (i = 0, p_dst_type = dst_tlist; i < ARRAY_SIZE(dst_tlist); i++, p_dst_type++) {
		if (!strncmp (&state->rxbuffer[p_dst_type->offset], p_dst_type->device_id, strlen (p_dst_type->device_id))) {
			use_type_flags = p_dst_type->type_flags;
			use_dst_type = p_dst_type->dst_type;

			/*	Card capabilities	*/
			state->dst_hw_cap = p_dst_type->dst_feature;
			dprintk(verbose, DST_ERROR, 1, "Recognise [%s]", p_dst_type->device_id);
			strncpy(&state->fw_name[0], p_dst_type->device_id, 6);
			/*	Multiple tuners		*/
			if (p_dst_type->tuner_type & TUNER_TYPE_MULTI) {
				switch (use_dst_type) {
				case DST_TYPE_IS_SAT:
					/*	STV0299 check	*/
					if (dst_check_stv0299(state) < 0) {
						dprintk(verbose, DST_ERROR, 1, "Unsupported");
						state->tuner_type = TUNER_TYPE_MB86A15;
					}
					break;
				default:
					break;
				}
				if (dst_check_mb86a15(state) < 0)
					dprintk(verbose, DST_ERROR, 1, "Unsupported");
			/*	Single tuner		*/
			} else {
				state->tuner_type = p_dst_type->tuner_type;
			}
			for (j = 0, p_tuner_list = tuner_list; j < ARRAY_SIZE(tuner_list); j++, p_tuner_list++) {
				if (!(strncmp(p_dst_type->device_id, p_tuner_list->fw_name, 7)) &&
					p_tuner_list->tuner_type == state->tuner_type) {
					dprintk(verbose, DST_ERROR, 1, "[%s] has a [%s]",
						p_dst_type->device_id, p_tuner_list->tuner_name);
				}
			}
			break;
		}
	}

	if (i >= ARRAY_SIZE(dst_tlist)) {
		dprintk(verbose, DST_ERROR, 1, "Unable to recognize %s or %s", &state->rxbuffer[0], &state->rxbuffer[1]);
		dprintk(verbose, DST_ERROR, 1, "please email linux-dvb@linuxtv.org with this type in");
		use_dst_type = DST_TYPE_IS_SAT;
		use_type_flags = DST_TYPE_HAS_SYMDIV;
	}
	dst_type_print(state, use_dst_type);
	state->type_flags = use_type_flags;
	state->dst_type = use_dst_type;
	dst_type_flags_print(state);

	return 0;
}

static int dst_probe(struct dst_state *state)
{
	mutex_init(&state->dst_mutex);
	if (dst_addons & DST_TYPE_HAS_CA) {
		if ((rdc_8820_reset(state)) < 0) {
			dprintk(verbose, DST_ERROR, 1, "RDC 8820 RESET Failed.");
			return -1;
		}
		msleep(4000);
	} else {
		msleep(100);
	}
	if ((dst_comm_init(state)) < 0) {
		dprintk(verbose, DST_ERROR, 1, "DST Initialization Failed.");
		return -1;
	}
	msleep(100);
	if (dst_get_device_id(state) < 0) {
		dprintk(verbose, DST_ERROR, 1, "unknown device.");
		return -1;
	}
	if (dst_get_mac(state) < 0) {
		dprintk(verbose, DST_INFO, 1, "MAC: Unsupported command");
	}
	if ((state->type_flags & DST_TYPE_HAS_MULTI_FE) || (state->type_flags & DST_TYPE_HAS_FW_BUILD)) {
		if (dst_get_tuner_info(state) < 0)
			dprintk(verbose, DST_INFO, 1, "Tuner: Unsupported command");
	}
	if (state->type_flags & DST_TYPE_HAS_TS204) {
		dst_packsize(state, 204);
	}
	if (state->type_flags & DST_TYPE_HAS_FW_BUILD) {
		if (dst_fw_ver(state) < 0) {
			dprintk(verbose, DST_INFO, 1, "FW: Unsupported command");
			return 0;
		}
		if (dst_card_type(state) < 0) {
			dprintk(verbose, DST_INFO, 1, "Card: Unsupported command");
			return 0;
		}
		if (dst_get_vendor(state) < 0) {
			dprintk(verbose, DST_INFO, 1, "Vendor: Unsupported command");
			return 0;
		}
	}

	return 0;
}

int dst_command(struct dst_state *state, u8 *data, u8 len)
{
	u8 reply;

	mutex_lock(&state->dst_mutex);
	if ((dst_comm_init(state)) < 0) {
		dprintk(verbose, DST_NOTICE, 1, "DST Communication Initialization Failed.");
		goto error;
	}
	if (write_dst(state, data, len)) {
		dprintk(verbose, DST_INFO, 1, "Trying to recover.. ");
		if ((dst_error_recovery(state)) < 0) {
			dprintk(verbose, DST_ERROR, 1, "Recovery Failed.");
			goto error;
		}
		goto error;
	}
	if ((dst_pio_disable(state)) < 0) {
		dprintk(verbose, DST_ERROR, 1, "PIO Disable Failed.");
		goto error;
	}
	if (state->type_flags & DST_TYPE_HAS_FW_1)
		udelay(3000);
	if (read_dst(state, &reply, GET_ACK)) {
		dprintk(verbose, DST_DEBUG, 1, "Trying to recover.. ");
		if ((dst_error_recovery(state)) < 0) {
			dprintk(verbose, DST_INFO, 1, "Recovery Failed.");
			goto error;
		}
		goto error;
	}
	if (reply != ACK) {
		dprintk(verbose, DST_INFO, 1, "write not acknowledged 0x%02x ", reply);
		goto error;
	}
	if (len >= 2 && data[0] == 0 && (data[1] == 1 || data[1] == 3))
		goto error;
	if (state->type_flags & DST_TYPE_HAS_FW_1)
		udelay(3000);
	else
		udelay(2000);
	if (!dst_wait_dst_ready(state, NO_DELAY))
		goto error;
	if (read_dst(state, state->rxbuffer, FIXED_COMM)) {
		dprintk(verbose, DST_DEBUG, 1, "Trying to recover.. ");
		if ((dst_error_recovery(state)) < 0) {
			dprintk(verbose, DST_INFO, 1, "Recovery failed.");
			goto error;
		}
		goto error;
	}
	if (state->rxbuffer[7] != dst_check_sum(state->rxbuffer, 7)) {
		dprintk(verbose, DST_INFO, 1, "checksum failure");
		goto error;
	}
	mutex_unlock(&state->dst_mutex);
	return 0;

error:
	mutex_unlock(&state->dst_mutex);
	return -EIO;

}
EXPORT_SYMBOL(dst_command);

static int dst_get_signal(struct dst_state *state)
{
	int retval;
	u8 get_signal[] = { 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfb };
	//dprintk("%s: Getting Signal strength and other parameters\n", __FUNCTION__);
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
		} else if (state->dst_type == DST_TYPE_IS_ATSC) {
			state->decode_lock = (state->rxbuffer[6] == 0x00) ? 1 : 0;
			state->decode_strength = state->rxbuffer[4] << 8;
			state->decode_snr = state->rxbuffer[2] << 8 | state->rxbuffer[3];
		}
		state->cur_jiff = jiffies;
	}
	return 0;
}

static int dst_tone_power_cmd(struct dst_state *state)
{
	u8 paket[8] = { 0x00, 0x09, 0xff, 0xff, 0x01, 0x00, 0x00, 0x00 };

	if (state->dst_type != DST_TYPE_IS_SAT)
		return -EOPNOTSUPP;
	paket[4] = state->tx_tuna[4];
	paket[2] = state->tx_tuna[2];
	paket[3] = state->tx_tuna[3];
	paket[7] = dst_check_sum (paket, 7);
	return dst_command(state, paket, 8);
}

static int dst_get_tuna(struct dst_state *state)
{
	int retval;

	if ((state->diseq_flags & ATTEMPT_TUNE) == 0)
		return 0;
	state->diseq_flags &= ~(HAS_LOCK);
	if (!dst_wait_dst_ready(state, NO_DELAY))
		return -EIO;
	if ((state->type_flags & DST_TYPE_HAS_VLF) &&
		!(state->dst_type == DST_TYPE_IS_ATSC))

		retval = read_dst(state, state->rx_tuna, 10);
	else
		retval = read_dst(state, &state->rx_tuna[2], FIXED_COMM);
	if (retval < 0) {
		dprintk(verbose, DST_DEBUG, 1, "read not successful");
		return retval;
	}
	if ((state->type_flags & DST_TYPE_HAS_VLF) &&
		!(state->dst_type == DST_TYPE_IS_CABLE) &&
		!(state->dst_type == DST_TYPE_IS_ATSC)) {

		if (state->rx_tuna[9] != dst_check_sum(&state->rx_tuna[0], 9)) {
			dprintk(verbose, DST_INFO, 1, "checksum failure ? ");
			return -EIO;
		}
	} else {
		if (state->rx_tuna[9] != dst_check_sum(&state->rx_tuna[2], 7)) {
			dprintk(verbose, DST_INFO, 1, "checksum failure? ");
			return -EIO;
		}
	}
	if (state->rx_tuna[2] == 0 && state->rx_tuna[3] == 0)
		return 0;
	if (state->dst_type == DST_TYPE_IS_SAT) {
		state->decode_freq = ((state->rx_tuna[2] & 0x7f) << 8) + state->rx_tuna[3];
	} else {
		state->decode_freq = ((state->rx_tuna[2] & 0x7f) << 16) + (state->rx_tuna[3] << 8) + state->rx_tuna[4];
	}
	state->decode_freq = state->decode_freq * 1000;
	state->decode_lock = 1;
	state->diseq_flags |= HAS_LOCK;

	return 1;
}

static int dst_set_voltage(struct dvb_frontend *fe, fe_sec_voltage_t voltage);

static int dst_write_tuna(struct dvb_frontend *fe)
{
	struct dst_state *state = fe->demodulator_priv;
	int retval;
	u8 reply;

	dprintk(verbose, DST_INFO, 1, "type_flags 0x%x ", state->type_flags);
	state->decode_freq = 0;
	state->decode_lock = state->decode_strength = state->decode_snr = 0;
	if (state->dst_type == DST_TYPE_IS_SAT) {
		if (!(state->diseq_flags & HAS_POWER))
			dst_set_voltage(fe, SEC_VOLTAGE_13);
	}
	state->diseq_flags &= ~(HAS_LOCK | ATTEMPT_TUNE);
	mutex_lock(&state->dst_mutex);
	if ((dst_comm_init(state)) < 0) {
		dprintk(verbose, DST_DEBUG, 1, "DST Communication initialization failed.");
		goto error;
	}
//	if (state->type_flags & DST_TYPE_HAS_NEWTUNE) {
	if ((state->type_flags & DST_TYPE_HAS_VLF) &&
		(!(state->dst_type == DST_TYPE_IS_ATSC))) {

		state->tx_tuna[9] = dst_check_sum(&state->tx_tuna[0], 9);
		retval = write_dst(state, &state->tx_tuna[0], 10);
	} else {
		state->tx_tuna[9] = dst_check_sum(&state->tx_tuna[2], 7);
		retval = write_dst(state, &state->tx_tuna[2], FIXED_COMM);
	}
	if (retval < 0) {
		dst_pio_disable(state);
		dprintk(verbose, DST_DEBUG, 1, "write not successful");
		goto werr;
	}
	if ((dst_pio_disable(state)) < 0) {
		dprintk(verbose, DST_DEBUG, 1, "DST PIO disable failed !");
		goto error;
	}
	if ((read_dst(state, &reply, GET_ACK) < 0)) {
		dprintk(verbose, DST_DEBUG, 1, "read verify not successful.");
		goto error;
	}
	if (reply != ACK) {
		dprintk(verbose, DST_DEBUG, 1, "write not acknowledged 0x%02x ", reply);
		goto error;
	}
	state->diseq_flags |= ATTEMPT_TUNE;
	retval = dst_get_tuna(state);
werr:
	mutex_unlock(&state->dst_mutex);
	return retval;

error:
	mutex_unlock(&state->dst_mutex);
	return -EIO;
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

static int dst_set_diseqc(struct dvb_frontend *fe, struct dvb_diseqc_master_cmd *cmd)
{
	struct dst_state *state = fe->demodulator_priv;
	u8 paket[8] = { 0x00, 0x08, 0x04, 0xe0, 0x10, 0x38, 0xf0, 0xec };

	if (state->dst_type != DST_TYPE_IS_SAT)
		return -EOPNOTSUPP;
	if (cmd->msg_len > 0 && cmd->msg_len < 5)
		memcpy(&paket[3], cmd->msg, cmd->msg_len);
	else if (cmd->msg_len == 5 && state->dst_hw_cap & DST_TYPE_HAS_DISEQC5)
		memcpy(&paket[2], cmd->msg, cmd->msg_len);
	else
		return -EINVAL;
	paket[7] = dst_check_sum(&paket[0], 7);
	return dst_command(state, paket, 8);
}

static int dst_set_voltage(struct dvb_frontend *fe, fe_sec_voltage_t voltage)
{
	int need_cmd, retval = 0;
	struct dst_state *state = fe->demodulator_priv;

	state->voltage = voltage;
	if (state->dst_type != DST_TYPE_IS_SAT)
		return -EOPNOTSUPP;

	need_cmd = 0;

	switch (voltage) {
	case SEC_VOLTAGE_13:
	case SEC_VOLTAGE_18:
		if ((state->diseq_flags & HAS_POWER) == 0)
			need_cmd = 1;
		state->diseq_flags |= HAS_POWER;
		state->tx_tuna[4] = 0x01;
		break;
	case SEC_VOLTAGE_OFF:
		need_cmd = 1;
		state->diseq_flags &= ~(HAS_POWER | HAS_LOCK | ATTEMPT_TUNE);
		state->tx_tuna[4] = 0x00;
		break;
	default:
		return -EINVAL;
	}

	if (need_cmd)
		retval = dst_tone_power_cmd(state);

	return retval;
}

static int dst_set_tone(struct dvb_frontend *fe, fe_sec_tone_mode_t tone)
{
	struct dst_state *state = fe->demodulator_priv;

	state->tone = tone;
	if (state->dst_type != DST_TYPE_IS_SAT)
		return -EOPNOTSUPP;

	switch (tone) {
	case SEC_TONE_OFF:
		if (state->type_flags & DST_TYPE_HAS_OBS_REGS)
		    state->tx_tuna[2] = 0x00;
		else
		    state->tx_tuna[2] = 0xff;
		break;

	case SEC_TONE_ON:
		state->tx_tuna[2] = 0x02;
		break;
	default:
		return -EINVAL;
	}
	return dst_tone_power_cmd(state);
}

static int dst_send_burst(struct dvb_frontend *fe, fe_sec_mini_cmd_t minicmd)
{
	struct dst_state *state = fe->demodulator_priv;

	if (state->dst_type != DST_TYPE_IS_SAT)
		return -EOPNOTSUPP;
	state->minicmd = minicmd;
	switch (minicmd) {
	case SEC_MINI_A:
		state->tx_tuna[3] = 0x02;
		break;
	case SEC_MINI_B:
		state->tx_tuna[3] = 0xff;
		break;
	}
	return dst_tone_power_cmd(state);
}


static int dst_init(struct dvb_frontend *fe)
{
	struct dst_state *state = fe->demodulator_priv;

	static u8 sat_tuna_188[] = { 0x09, 0x00, 0x03, 0xb6, 0x01, 0x00, 0x73, 0x21, 0x00, 0x00 };
	static u8 sat_tuna_204[] = { 0x00, 0x00, 0x03, 0xb6, 0x01, 0x55, 0xbd, 0x50, 0x00, 0x00 };
	static u8 ter_tuna_188[] = { 0x09, 0x00, 0x03, 0xb6, 0x01, 0x07, 0x00, 0x00, 0x00, 0x00 };
	static u8 ter_tuna_204[] = { 0x00, 0x00, 0x03, 0xb6, 0x01, 0x07, 0x00, 0x00, 0x00, 0x00 };
	static u8 cab_tuna_188[] = { 0x09, 0x00, 0x03, 0xb6, 0x01, 0x07, 0x00, 0x00, 0x00, 0x00 };
	static u8 cab_tuna_204[] = { 0x00, 0x00, 0x03, 0xb6, 0x01, 0x07, 0x00, 0x00, 0x00, 0x00 };
	static u8 atsc_tuner[] = { 0x00, 0x00, 0x03, 0xb6, 0x01, 0x07, 0x00, 0x00, 0x00, 0x00 };

	state->inversion = INVERSION_OFF;
	state->voltage = SEC_VOLTAGE_13;
	state->tone = SEC_TONE_OFF;
	state->diseq_flags = 0;
	state->k22 = 0x02;
	state->bandwidth = BANDWIDTH_7_MHZ;
	state->cur_jiff = jiffies;
	if (state->dst_type == DST_TYPE_IS_SAT)
		memcpy(state->tx_tuna, ((state->type_flags & DST_TYPE_HAS_VLF) ? sat_tuna_188 : sat_tuna_204), sizeof (sat_tuna_204));
	else if (state->dst_type == DST_TYPE_IS_TERR)
		memcpy(state->tx_tuna, ((state->type_flags & DST_TYPE_HAS_VLF) ? ter_tuna_188 : ter_tuna_204), sizeof (ter_tuna_204));
	else if (state->dst_type == DST_TYPE_IS_CABLE)
		memcpy(state->tx_tuna, ((state->type_flags & DST_TYPE_HAS_VLF) ? cab_tuna_188 : cab_tuna_204), sizeof (cab_tuna_204));
	else if (state->dst_type == DST_TYPE_IS_ATSC)
		memcpy(state->tx_tuna, atsc_tuner, sizeof (atsc_tuner));

	return 0;
}

static int dst_read_status(struct dvb_frontend *fe, fe_status_t *status)
{
	struct dst_state *state = fe->demodulator_priv;

	*status = 0;
	if (state->diseq_flags & HAS_LOCK) {
//		dst_get_signal(state);	// don't require(?) to ask MCU
		if (state->decode_lock)
			*status |= FE_HAS_LOCK | FE_HAS_SIGNAL | FE_HAS_CARRIER | FE_HAS_SYNC | FE_HAS_VITERBI;
	}

	return 0;
}

static int dst_read_signal_strength(struct dvb_frontend *fe, u16 *strength)
{
	struct dst_state *state = fe->demodulator_priv;

	int retval = dst_get_signal(state);
	*strength = state->decode_strength;

	return retval;
}

static int dst_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	struct dst_state *state = fe->demodulator_priv;

	int retval = dst_get_signal(state);
	*snr = state->decode_snr;

	return retval;
}

static int dst_set_frontend(struct dvb_frontend *fe, struct dvb_frontend_parameters *p)
{
	int retval = -EINVAL;
	struct dst_state *state = fe->demodulator_priv;

	if (p != NULL) {
		retval = dst_set_freq(state, p->frequency);
		if(retval != 0)
			return retval;
		dprintk(verbose, DST_DEBUG, 1, "Set Frequency=[%d]", p->frequency);

		if (state->dst_type == DST_TYPE_IS_SAT) {
			if (state->type_flags & DST_TYPE_HAS_OBS_REGS)
				dst_set_inversion(state, p->inversion);
			dst_set_fec(state, p->u.qpsk.fec_inner);
			dst_set_symbolrate(state, p->u.qpsk.symbol_rate);
			dst_set_polarization(state);
			dprintk(verbose, DST_DEBUG, 1, "Set Symbolrate=[%d]", p->u.qpsk.symbol_rate);

		} else if (state->dst_type == DST_TYPE_IS_TERR)
			dst_set_bandwidth(state, p->u.ofdm.bandwidth);
		else if (state->dst_type == DST_TYPE_IS_CABLE) {
			dst_set_fec(state, p->u.qam.fec_inner);
			dst_set_symbolrate(state, p->u.qam.symbol_rate);
			dst_set_modulation(state, p->u.qam.modulation);
		}
		retval = dst_write_tuna(fe);
	}

	return retval;
}

static int dst_tune_frontend(struct dvb_frontend* fe,
			    struct dvb_frontend_parameters* p,
			    unsigned int mode_flags,
			    int *delay,
			    fe_status_t *status)
{
	struct dst_state *state = fe->demodulator_priv;

	if (p != NULL) {
		dst_set_freq(state, p->frequency);
		dprintk(verbose, DST_DEBUG, 1, "Set Frequency=[%d]", p->frequency);

		if (state->dst_type == DST_TYPE_IS_SAT) {
			if (state->type_flags & DST_TYPE_HAS_OBS_REGS)
				dst_set_inversion(state, p->inversion);
			dst_set_fec(state, p->u.qpsk.fec_inner);
			dst_set_symbolrate(state, p->u.qpsk.symbol_rate);
			dst_set_polarization(state);
			dprintk(verbose, DST_DEBUG, 1, "Set Symbolrate=[%d]", p->u.qpsk.symbol_rate);

		} else if (state->dst_type == DST_TYPE_IS_TERR)
			dst_set_bandwidth(state, p->u.ofdm.bandwidth);
		else if (state->dst_type == DST_TYPE_IS_CABLE) {
			dst_set_fec(state, p->u.qam.fec_inner);
			dst_set_symbolrate(state, p->u.qam.symbol_rate);
			dst_set_modulation(state, p->u.qam.modulation);
		}
		dst_write_tuna(fe);
	}

	if (!(mode_flags & FE_TUNE_MODE_ONESHOT))
		dst_read_status(fe, status);

	*delay = HZ/10;
	return 0;
}

static int dst_get_tuning_algo(struct dvb_frontend *fe)
{
	return dst_algo;
}

static int dst_get_frontend(struct dvb_frontend *fe, struct dvb_frontend_parameters *p)
{
	struct dst_state *state = fe->demodulator_priv;

	p->frequency = state->decode_freq;
	if (state->dst_type == DST_TYPE_IS_SAT) {
		if (state->type_flags & DST_TYPE_HAS_OBS_REGS)
			p->inversion = state->inversion;
		p->u.qpsk.symbol_rate = state->symbol_rate;
		p->u.qpsk.fec_inner = dst_get_fec(state);
	} else if (state->dst_type == DST_TYPE_IS_TERR) {
		p->u.ofdm.bandwidth = state->bandwidth;
	} else if (state->dst_type == DST_TYPE_IS_CABLE) {
		p->u.qam.symbol_rate = state->symbol_rate;
		p->u.qam.fec_inner = dst_get_fec(state);
		p->u.qam.modulation = dst_get_modulation(state);
	}

	return 0;
}

static void dst_release(struct dvb_frontend *fe)
{
	struct dst_state *state = fe->demodulator_priv;
	if (state->dst_ca) {
		dvb_unregister_device(state->dst_ca);
#ifdef CONFIG_DVB_CORE_ATTACH
		symbol_put(dst_ca_attach);
#endif
	}
#ifdef CONFIG_DVB_CORE_ATTACH
	symbol_put(dst_attach);
#endif
	kfree(state);
}

static struct dvb_frontend_ops dst_dvbt_ops;
static struct dvb_frontend_ops dst_dvbs_ops;
static struct dvb_frontend_ops dst_dvbc_ops;
static struct dvb_frontend_ops dst_atsc_ops;

struct dst_state *dst_attach(struct dst_state *state, struct dvb_adapter *dvb_adapter)
{
	/* check if the ASIC is there */
	if (dst_probe(state) < 0) {
		kfree(state);
		return NULL;
	}
	/* determine settings based on type */
	/* create dvb_frontend */
	switch (state->dst_type) {
	case DST_TYPE_IS_TERR:
		memcpy(&state->frontend.ops, &dst_dvbt_ops, sizeof(struct dvb_frontend_ops));
		break;
	case DST_TYPE_IS_CABLE:
		memcpy(&state->frontend.ops, &dst_dvbc_ops, sizeof(struct dvb_frontend_ops));
		break;
	case DST_TYPE_IS_SAT:
		memcpy(&state->frontend.ops, &dst_dvbs_ops, sizeof(struct dvb_frontend_ops));
		break;
	case DST_TYPE_IS_ATSC:
		memcpy(&state->frontend.ops, &dst_atsc_ops, sizeof(struct dvb_frontend_ops));
		break;
	default:
		dprintk(verbose, DST_ERROR, 1, "unknown DST type. please report to the LinuxTV.org DVB mailinglist.");
		kfree(state);
		return NULL;
	}
	state->frontend.demodulator_priv = state;

	return state;				/*	Manu (DST is a card not a frontend)	*/
}

EXPORT_SYMBOL(dst_attach);

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
	.tune = dst_tune_frontend,
	.set_frontend = dst_set_frontend,
	.get_frontend = dst_get_frontend,
	.get_frontend_algo = dst_get_tuning_algo,
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
	.tune = dst_tune_frontend,
	.set_frontend = dst_set_frontend,
	.get_frontend = dst_get_frontend,
	.get_frontend_algo = dst_get_tuning_algo,
	.read_status = dst_read_status,
	.read_signal_strength = dst_read_signal_strength,
	.read_snr = dst_read_snr,
	.diseqc_send_burst = dst_send_burst,
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
	.tune = dst_tune_frontend,
	.set_frontend = dst_set_frontend,
	.get_frontend = dst_get_frontend,
	.get_frontend_algo = dst_get_tuning_algo,
	.read_status = dst_read_status,
	.read_signal_strength = dst_read_signal_strength,
	.read_snr = dst_read_snr,
};

static struct dvb_frontend_ops dst_atsc_ops = {
	.info = {
		.name = "DST ATSC",
		.type = FE_ATSC,
		.frequency_stepsize = 62500,
		.frequency_min = 510000000,
		.frequency_max = 858000000,
		.symbol_rate_min = 1000000,
		.symbol_rate_max = 45000000,
		.caps = FE_CAN_FEC_AUTO | FE_CAN_QAM_AUTO | FE_CAN_QAM_64 | FE_CAN_QAM_256 | FE_CAN_8VSB
	},

	.release = dst_release,
	.init = dst_init,
	.tune = dst_tune_frontend,
	.set_frontend = dst_set_frontend,
	.get_frontend = dst_get_frontend,
	.get_frontend_algo = dst_get_tuning_algo,
	.read_status = dst_read_status,
	.read_signal_strength = dst_read_signal_strength,
	.read_snr = dst_read_snr,
};

MODULE_DESCRIPTION("DST DVB-S/T/C/ATSC Combo Frontend driver");
MODULE_AUTHOR("Jamie Honan, Manu Abraham");
MODULE_LICENSE("GPL");
