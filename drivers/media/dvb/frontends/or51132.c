/*
 *    Support for OR51132 (pcHDTV HD-3000) - VSB/QAM
 *
 *    Copyright (C) 2005 Kirk Lapray <kirk_lapray@bigfoot.com>
 *
 *    Based on code from Jack Kelliher (kelliher@xmission.com)
 *                           Copyright (C) 2002 & pcHDTV, inc.
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
*/

/*
 * This driver needs two external firmware files. Please copy
 * "dvb-fe-or51132-vsb.fw" and "dvb-fe-or51132-qam.fw" to
 * /usr/lib/hotplug/firmware/ or /lib/firmware/
 * (depending on configuration of firmware hotplug).
 */
#define OR51132_VSB_FIRMWARE "dvb-fe-or51132-vsb.fw"
#define OR51132_QAM_FIRMWARE "dvb-fe-or51132-qam.fw"

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <asm/byteorder.h>

#include "dvb_frontend.h"
#include "dvb-pll.h"
#include "or51132.h"

static int debug;
#define dprintk(args...) \
	do { \
		if (debug) printk(KERN_DEBUG "or51132: " args); \
	} while (0)


struct or51132_state
{
	struct i2c_adapter* i2c;
	struct dvb_frontend_ops ops;

	/* Configuration settings */
	const struct or51132_config* config;

	struct dvb_frontend frontend;

	/* Demodulator private data */
	fe_modulation_t current_modulation;

	/* Tuner private data */
	u32 current_frequency;
};

static int i2c_writebytes (struct or51132_state* state, u8 reg, u8 *buf, int len)
{
	int err;
	struct i2c_msg msg;
	msg.addr  = reg;
	msg.flags = 0;
	msg.len   = len;
	msg.buf   = buf;

	if ((err = i2c_transfer(state->i2c, &msg, 1)) != 1) {
		printk(KERN_WARNING "or51132: i2c_writebytes error (addr %02x, err == %i)\n", reg, err);
		return -EREMOTEIO;
	}

	return 0;
}

static u8 i2c_readbytes (struct or51132_state* state, u8 reg, u8* buf, int len)
{
	int err;
	struct i2c_msg msg;
	msg.addr   = reg;
	msg.flags = I2C_M_RD;
	msg.len = len;
	msg.buf = buf;

	if ((err = i2c_transfer(state->i2c, &msg, 1)) != 1) {
		printk(KERN_WARNING "or51132: i2c_readbytes error (addr %02x, err == %i)\n", reg, err);
		return -EREMOTEIO;
	}

	return 0;
}

static int or51132_load_firmware (struct dvb_frontend* fe, const struct firmware *fw)
{
	struct or51132_state* state = fe->demodulator_priv;
	static u8 run_buf[] = {0x7F,0x01};
	u8 rec_buf[8];
	u8 cmd_buf[3];
	u32 firmwareAsize, firmwareBsize;
	int i,ret;

	dprintk("Firmware is %Zd bytes\n",fw->size);

	/* Get size of firmware A and B */
	firmwareAsize = le32_to_cpu(*((u32*)fw->data));
	dprintk("FirmwareA is %i bytes\n",firmwareAsize);
	firmwareBsize = le32_to_cpu(*((u32*)(fw->data+4)));
	dprintk("FirmwareB is %i bytes\n",firmwareBsize);

	/* Upload firmware */
	if ((ret = i2c_writebytes(state,state->config->demod_address,
				 &fw->data[8],firmwareAsize))) {
		printk(KERN_WARNING "or51132: load_firmware error 1\n");
		return ret;
	}
	msleep(1); /* 1ms */
	if ((ret = i2c_writebytes(state,state->config->demod_address,
				 &fw->data[8+firmwareAsize],firmwareBsize))) {
		printk(KERN_WARNING "or51132: load_firmware error 2\n");
		return ret;
	}
	msleep(1); /* 1ms */

	if ((ret = i2c_writebytes(state,state->config->demod_address,
				 run_buf,2))) {
		printk(KERN_WARNING "or51132: load_firmware error 3\n");
		return ret;
	}

	/* Wait at least 5 msec */
	msleep(20); /* 10ms */

	if ((ret = i2c_writebytes(state,state->config->demod_address,
				 run_buf,2))) {
		printk(KERN_WARNING "or51132: load_firmware error 4\n");
		return ret;
	}

	/* 50ms for operation to begin */
	msleep(50);

	/* Read back ucode version to besure we loaded correctly and are really up and running */
	/* Get uCode version */
	cmd_buf[0] = 0x10;
	cmd_buf[1] = 0x10;
	cmd_buf[2] = 0x00;
	msleep(20); /* 20ms */
	if ((ret = i2c_writebytes(state,state->config->demod_address,
				 cmd_buf,3))) {
		printk(KERN_WARNING "or51132: load_firmware error a\n");
		return ret;
	}

	cmd_buf[0] = 0x04;
	cmd_buf[1] = 0x17;
	msleep(20); /* 20ms */
	if ((ret = i2c_writebytes(state,state->config->demod_address,
				 cmd_buf,2))) {
		printk(KERN_WARNING "or51132: load_firmware error b\n");
		return ret;
	}

	cmd_buf[0] = 0x00;
	cmd_buf[1] = 0x00;
	msleep(20); /* 20ms */
	if ((ret = i2c_writebytes(state,state->config->demod_address,
				 cmd_buf,2))) {
		printk(KERN_WARNING "or51132: load_firmware error c\n");
		return ret;
	}

	for(i=0;i<4;i++) {
		msleep(20); /* 20ms */
		/* Once upon a time, this command might have had something
		   to do with getting the firmware version, but it's
		   not used anymore:
		   {0x04,0x00,0x30,0x00,i+1} */
		/* Read 8 bytes, two bytes at a time */
		if ((ret = i2c_readbytes(state,state->config->demod_address,
					&rec_buf[i*2],2))) {
			printk(KERN_WARNING
			       "or51132: load_firmware error d - %d\n",i);
			return ret;
		}
	}

	printk(KERN_WARNING
	       "or51132: Version: %02X%02X%02X%02X-%02X%02X%02X%02X (%02X%01X-%01X-%02X%01X-%01X)\n",
	       rec_buf[1],rec_buf[0],rec_buf[3],rec_buf[2],
	       rec_buf[5],rec_buf[4],rec_buf[7],rec_buf[6],
	       rec_buf[3],rec_buf[2]>>4,rec_buf[2]&0x0f,
	       rec_buf[5],rec_buf[4]>>4,rec_buf[4]&0x0f);

	cmd_buf[0] = 0x10;
	cmd_buf[1] = 0x00;
	cmd_buf[2] = 0x00;
	msleep(20); /* 20ms */
	if ((ret = i2c_writebytes(state,state->config->demod_address,
				 cmd_buf,3))) {
		printk(KERN_WARNING "or51132: load_firmware error e\n");
		return ret;
	}
	return 0;
};

static int or51132_init(struct dvb_frontend* fe)
{
	return 0;
}

static int or51132_read_ber(struct dvb_frontend* fe, u32* ber)
{
	*ber = 0;
	return 0;
}

static int or51132_read_ucblocks(struct dvb_frontend* fe, u32* ucblocks)
{
	*ucblocks = 0;
	return 0;
}

static int or51132_sleep(struct dvb_frontend* fe)
{
	return 0;
}

static int or51132_setmode(struct dvb_frontend* fe)
{
	struct or51132_state* state = fe->demodulator_priv;
	unsigned char cmd_buf[3];

	dprintk("setmode %d\n",(int)state->current_modulation);
	/* set operation mode in Receiver 1 register; */
	cmd_buf[0] = 0x04;
	cmd_buf[1] = 0x01;
	switch (state->current_modulation) {
	case QAM_256:
	case QAM_64:
	case QAM_AUTO:
		/* Auto-deinterleave; MPEG ser, MPEG2tr, phase noise-high*/
		cmd_buf[2] = 0x5F;
		break;
	case VSB_8:
		/* Auto CH, Auto NTSC rej, MPEGser, MPEG2tr, phase noise-high*/
		cmd_buf[2] = 0x50;
		break;
	default:
		printk("setmode:Modulation set to unsupported value\n");
	};
	if (i2c_writebytes(state,state->config->demod_address,
			   cmd_buf,3)) {
		printk(KERN_WARNING "or51132: set_mode error 1\n");
		return -1;
	}
	dprintk("or51132: set #1 to %02x\n", cmd_buf[2]);

	/* Set operation mode in Receiver 6 register */
	cmd_buf[0] = 0x1C;
	switch (state->current_modulation) {
	case QAM_AUTO:
		/* REC MODE Normal Carrier Lock */
		cmd_buf[1] = 0x00;
		/* Channel MODE Auto QAM64/256 */
		cmd_buf[2] = 0x4f;
		break;
	case QAM_256:
		/* REC MODE Normal Carrier Lock */
		cmd_buf[1] = 0x00;
		/* Channel MODE QAM256 */
		cmd_buf[2] = 0x45;
		break;
	case QAM_64:
		/* REC MODE Normal Carrier Lock */
		cmd_buf[1] = 0x00;
		/* Channel MODE QAM64 */
		cmd_buf[2] = 0x43;
		break;
	case VSB_8:
		 /* REC MODE inv IF spectrum, Normal */
		cmd_buf[1] = 0x03;
		/* Channel MODE ATSC/VSB8 */
		cmd_buf[2] = 0x06;
		break;
	default:
		printk("setmode: Modulation set to unsupported value\n");
	};
	msleep(20); /* 20ms */
	if (i2c_writebytes(state,state->config->demod_address,
			   cmd_buf,3)) {
		printk(KERN_WARNING "or51132: set_mode error 2\n");
		return -1;
	}
	dprintk("or51132: set #6 to 0x%02x%02x\n", cmd_buf[1], cmd_buf[2]);

	return 0;
}

/* Some modulations use the same firmware.  This classifies modulations
   by the firmware they use. */
#define MOD_FWCLASS_UNKNOWN	0
#define MOD_FWCLASS_VSB		1
#define MOD_FWCLASS_QAM		2
static int modulation_fw_class(fe_modulation_t modulation)
{
	switch(modulation) {
	case VSB_8:
		return MOD_FWCLASS_VSB;
	case QAM_AUTO:
	case QAM_64:
	case QAM_256:
		return MOD_FWCLASS_QAM;
	default:
		return MOD_FWCLASS_UNKNOWN;
	}
}

static int or51132_set_parameters(struct dvb_frontend* fe,
				  struct dvb_frontend_parameters *param)
{
	int ret;
	struct or51132_state* state = fe->demodulator_priv;
	const struct firmware *fw;
	const char *fwname;
	int clock_mode;

	/* Upload new firmware only if we need a different one */
	if (modulation_fw_class(state->current_modulation) !=
	    modulation_fw_class(param->u.vsb.modulation)) {
		switch(modulation_fw_class(param->u.vsb.modulation)) {
		case MOD_FWCLASS_VSB:
			dprintk("set_parameters VSB MODE\n");
			fwname = OR51132_VSB_FIRMWARE;

			/* Set non-punctured clock for VSB */
			clock_mode = 0;
			break;
		case MOD_FWCLASS_QAM:
			dprintk("set_parameters QAM MODE\n");
			fwname = OR51132_QAM_FIRMWARE;

			/* Set punctured clock for QAM */
			clock_mode = 1;
			break;
		default:
			printk("or51132: Modulation type(%d) UNSUPPORTED\n",
			       param->u.vsb.modulation);
			return -1;
		}
		printk("or51132: Waiting for firmware upload(%s)...\n",
		       fwname);
		ret = request_firmware(&fw, fwname, &state->i2c->dev);
		if (ret) {
			printk(KERN_WARNING "or51132: No firmware up"
			       "loaded(timeout or file not found?)\n");
			return ret;
		}
		ret = or51132_load_firmware(fe, fw);
		release_firmware(fw);
		if (ret) {
			printk(KERN_WARNING "or51132: Writing firmware to "
			       "device failed!\n");
			return ret;
		}
		printk("or51132: Firmware upload complete.\n");
		state->config->set_ts_params(fe, clock_mode);
	}
	/* Change only if we are actually changing the modulation */
	if (state->current_modulation != param->u.vsb.modulation) {
		state->current_modulation = param->u.vsb.modulation;
		or51132_setmode(fe);
	}

	if (fe->ops->tuner_ops.set_params) {
		fe->ops->tuner_ops.set_params(fe, param);
		if (fe->ops->i2c_gate_ctrl) fe->ops->i2c_gate_ctrl(fe, 0);
	}

	/* Set to current mode */
	or51132_setmode(fe);

	/* Update current frequency */
	state->current_frequency = param->frequency;
	return 0;
}

static int or51132_get_parameters(struct dvb_frontend* fe,
				  struct dvb_frontend_parameters *param)
{
	struct or51132_state* state = fe->demodulator_priv;
	u8 buf[2];

	/* Receiver Status */
	buf[0]=0x04;
	buf[1]=0x00;
	msleep(30); /* 30ms */
	if (i2c_writebytes(state,state->config->demod_address,buf,2)) {
		printk(KERN_WARNING "or51132: get_parameters write error\n");
		return -EREMOTEIO;
	}
	msleep(30); /* 30ms */
	if (i2c_readbytes(state,state->config->demod_address,buf,2)) {
		printk(KERN_WARNING "or51132: get_parameters read error\n");
		return -EREMOTEIO;
	}
	switch(buf[0]) {
		case 0x06: param->u.vsb.modulation = VSB_8; break;
		case 0x43: param->u.vsb.modulation = QAM_64; break;
		case 0x45: param->u.vsb.modulation = QAM_256; break;
		default:
			printk(KERN_WARNING "or51132: unknown status 0x%02x\n",
			       buf[0]);
			return -EREMOTEIO;
	}

	/* FIXME: Read frequency from frontend, take AFC into account */
	param->frequency = state->current_frequency;

	/* FIXME: How to read inversion setting? Receiver 6 register? */
	param->inversion = INVERSION_AUTO;

	return 0;
}

static int or51132_read_status(struct dvb_frontend* fe, fe_status_t* status)
{
	struct or51132_state* state = fe->demodulator_priv;
	unsigned char rec_buf[2];
	unsigned char snd_buf[2];
	*status = 0;

	/* Receiver Status */
	snd_buf[0]=0x04;
	snd_buf[1]=0x00;
	msleep(30); /* 30ms */
	if (i2c_writebytes(state,state->config->demod_address,snd_buf,2)) {
		printk(KERN_WARNING "or51132: read_status write error\n");
		return -1;
	}
	msleep(30); /* 30ms */
	if (i2c_readbytes(state,state->config->demod_address,rec_buf,2)) {
		printk(KERN_WARNING "or51132: read_status read error\n");
		return -1;
	}
	dprintk("read_status %x %x\n",rec_buf[0],rec_buf[1]);

	if (rec_buf[1] & 0x01) { /* Receiver Lock */
		*status |= FE_HAS_SIGNAL;
		*status |= FE_HAS_CARRIER;
		*status |= FE_HAS_VITERBI;
		*status |= FE_HAS_SYNC;
		*status |= FE_HAS_LOCK;
	}
	return 0;
}

/* log10-1 table at .5 increments from 1 to 100.5 */
static unsigned int i100x20log10[] = {
     0,  352,  602,  795,  954, 1088, 1204, 1306, 1397, 1480,
  1556, 1625, 1690, 1750, 1806, 1858, 1908, 1955, 2000, 2042,
  2082, 2121, 2158, 2193, 2227, 2260, 2292, 2322, 2352, 2380,
  2408, 2434, 2460, 2486, 2510, 2534, 2557, 2580, 2602, 2623,
  2644, 2664, 2684, 2704, 2723, 2742, 2760, 2778, 2795, 2813,
  2829, 2846, 2862, 2878, 2894, 2909, 2924, 2939, 2954, 2968,
  2982, 2996, 3010, 3023, 3037, 3050, 3062, 3075, 3088, 3100,
  3112, 3124, 3136, 3148, 3159, 3170, 3182, 3193, 3204, 3214,
  3225, 3236, 3246, 3256, 3266, 3276, 3286, 3296, 3306, 3316,
  3325, 3334, 3344, 3353, 3362, 3371, 3380, 3389, 3397, 3406,
  3415, 3423, 3432, 3440, 3448, 3456, 3464, 3472, 3480, 3488,
  3496, 3504, 3511, 3519, 3526, 3534, 3541, 3549, 3556, 3563,
  3570, 3577, 3584, 3591, 3598, 3605, 3612, 3619, 3625, 3632,
  3639, 3645, 3652, 3658, 3665, 3671, 3677, 3683, 3690, 3696,
  3702, 3708, 3714, 3720, 3726, 3732, 3738, 3744, 3750, 3755,
  3761, 3767, 3772, 3778, 3784, 3789, 3795, 3800, 3806, 3811,
  3816, 3822, 3827, 3832, 3838, 3843, 3848, 3853, 3858, 3863,
  3868, 3874, 3879, 3884, 3888, 3893, 3898, 3903, 3908, 3913,
  3918, 3922, 3927, 3932, 3936, 3941, 3946, 3950, 3955, 3960,
  3964, 3969, 3973, 3978, 3982, 3986, 3991, 3995, 4000, 4004,
};

static unsigned int denom[] = {1,1,100,1000,10000,100000,1000000,10000000,100000000};

static unsigned int i20Log10(unsigned short val)
{
	unsigned int rntval = 100;
	unsigned int tmp = val;
	unsigned int exp = 1;

	while(tmp > 100) {tmp /= 100; exp++;}

	val = (2 * val)/denom[exp];
	if (exp > 1) rntval = 2000*exp;

	rntval += i100x20log10[val];
	return rntval;
}

static int or51132_read_signal_strength(struct dvb_frontend* fe, u16* strength)
{
	struct or51132_state* state = fe->demodulator_priv;
	unsigned char rec_buf[2];
	unsigned char snd_buf[2];
	u8 rcvr_stat;
	u16 snr_equ;
	u32 signal_strength;
	int usK;

	snd_buf[0]=0x04;
	snd_buf[1]=0x02; /* SNR after Equalizer */
	msleep(30); /* 30ms */
	if (i2c_writebytes(state,state->config->demod_address,snd_buf,2)) {
		printk(KERN_WARNING "or51132: read_status write error\n");
		return -1;
	}
	msleep(30); /* 30ms */
	if (i2c_readbytes(state,state->config->demod_address,rec_buf,2)) {
		printk(KERN_WARNING "or51132: read_status read error\n");
		return -1;
	}
	snr_equ = rec_buf[0] | (rec_buf[1] << 8);
	dprintk("read_signal_strength snr_equ %x %x (%i)\n",rec_buf[0],rec_buf[1],snr_equ);

	/* Receiver Status */
	snd_buf[0]=0x04;
	snd_buf[1]=0x00;
	msleep(30); /* 30ms */
	if (i2c_writebytes(state,state->config->demod_address,snd_buf,2)) {
		printk(KERN_WARNING "or51132: read_signal_strength read_status write error\n");
		return -1;
	}
	msleep(30); /* 30ms */
	if (i2c_readbytes(state,state->config->demod_address,rec_buf,2)) {
		printk(KERN_WARNING "or51132: read_signal_strength read_status read error\n");
		return -1;
	}
	dprintk("read_signal_strength read_status %x %x\n",rec_buf[0],rec_buf[1]);
	rcvr_stat = rec_buf[1];
	usK = (rcvr_stat & 0x10) ? 3 : 0;

	/* The value reported back from the frontend will be FFFF=100% 0000=0% */
	signal_strength = (((8952 - i20Log10(snr_equ) - usK*100)/3+5)*65535)/1000;
	if (signal_strength > 0xffff)
		*strength = 0xffff;
	else
		*strength = signal_strength;
	dprintk("read_signal_strength %i\n",*strength);

	return 0;
}

static int or51132_read_snr(struct dvb_frontend* fe, u16* snr)
{
	struct or51132_state* state = fe->demodulator_priv;
	unsigned char rec_buf[2];
	unsigned char snd_buf[2];
	u16 snr_equ;

	snd_buf[0]=0x04;
	snd_buf[1]=0x02; /* SNR after Equalizer */
	msleep(30); /* 30ms */
	if (i2c_writebytes(state,state->config->demod_address,snd_buf,2)) {
		printk(KERN_WARNING "or51132: read_snr write error\n");
		return -1;
	}
	msleep(30); /* 30ms */
	if (i2c_readbytes(state,state->config->demod_address,rec_buf,2)) {
		printk(KERN_WARNING "or51132: read_snr dvr read error\n");
		return -1;
	}
	snr_equ = rec_buf[0] | (rec_buf[1] << 8);
	dprintk("read_snr snr_equ %x %x (%i)\n",rec_buf[0],rec_buf[1],snr_equ);

	*snr = 0xFFFF - snr_equ;
	dprintk("read_snr %i\n",*snr);

	return 0;
}

static int or51132_get_tune_settings(struct dvb_frontend* fe, struct dvb_frontend_tune_settings* fe_tune_settings)
{
	fe_tune_settings->min_delay_ms = 500;
	fe_tune_settings->step_size = 0;
	fe_tune_settings->max_drift = 0;

	return 0;
}

static void or51132_release(struct dvb_frontend* fe)
{
	struct or51132_state* state = fe->demodulator_priv;
	kfree(state);
}

static struct dvb_frontend_ops or51132_ops;

struct dvb_frontend* or51132_attach(const struct or51132_config* config,
				    struct i2c_adapter* i2c)
{
	struct or51132_state* state = NULL;

	/* Allocate memory for the internal state */
	state = kmalloc(sizeof(struct or51132_state), GFP_KERNEL);
	if (state == NULL)
		goto error;

	/* Setup the state */
	state->config = config;
	state->i2c = i2c;
	memcpy(&state->ops, &or51132_ops, sizeof(struct dvb_frontend_ops));
	state->current_frequency = -1;
	state->current_modulation = -1;

	/* Create dvb_frontend */
	state->frontend.ops = &state->ops;
	state->frontend.demodulator_priv = state;
	return &state->frontend;

error:
	kfree(state);
	return NULL;
}

static struct dvb_frontend_ops or51132_ops = {

	.info = {
		.name			= "Oren OR51132 VSB/QAM Frontend",
		.type 			= FE_ATSC,
		.frequency_min		= 44000000,
		.frequency_max		= 958000000,
		.frequency_stepsize	= 166666,
		.caps = FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
			FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
			FE_CAN_QAM_64 | FE_CAN_QAM_256 | FE_CAN_QAM_AUTO |
			FE_CAN_8VSB
	},

	.release = or51132_release,

	.init = or51132_init,
	.sleep = or51132_sleep,

	.set_frontend = or51132_set_parameters,
	.get_frontend = or51132_get_parameters,
	.get_tune_settings = or51132_get_tune_settings,

	.read_status = or51132_read_status,
	.read_ber = or51132_read_ber,
	.read_signal_strength = or51132_read_signal_strength,
	.read_snr = or51132_read_snr,
	.read_ucblocks = or51132_read_ucblocks,
};

module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off frontend debugging (default:off).");

MODULE_DESCRIPTION("OR51132 ATSC [pcHDTV HD-3000] (8VSB & ITU J83 AnnexB FEC QAM64/256) Demodulator Driver");
MODULE_AUTHOR("Kirk Lapray");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL(or51132_attach);

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
