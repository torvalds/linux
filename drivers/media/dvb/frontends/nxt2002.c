/*
    Support for B2C2/BBTI Technisat Air2PC - ATSC

    Copyright (C) 2004 Taylor Jacob <rtjacob@earthlink.net>

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

/*
 * This driver needs external firmware. Please use the command
 * "<kerneldir>/Documentation/dvb/get_dvb_firmware nxt2002" to
 * download/extract it, and then copy it to /usr/lib/hotplug/firmware.
 */
#define NXT2002_DEFAULT_FIRMWARE "dvb-fe-nxt2002.fw"
#define CRC_CCIT_MASK 0x1021

#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/string.h>
#include <linux/slab.h>

#include "dvb_frontend.h"
#include "nxt2002.h"

struct nxt2002_state {

	struct i2c_adapter* i2c;
	struct dvb_frontend_ops ops;
	const struct nxt2002_config* config;
	struct dvb_frontend frontend;

	/* demodulator private data */
	u8 initialised:1;
};

static int debug;
#define dprintk(args...) \
	do { \
		if (debug) printk(KERN_DEBUG "nxt2002: " args); \
	} while (0)

static int i2c_writebytes (struct nxt2002_state* state, u8 reg, u8 *buf, u8 len)
{
	/* probbably a much better way or doing this */
	u8 buf2 [256],x;
	int err;
	struct i2c_msg msg = { .addr = state->config->demod_address, .flags = 0, .buf = buf2, .len = len + 1 };

	buf2[0] = reg;
	for (x = 0 ; x < len ; x++)
		buf2[x+1] = buf[x];

	if ((err = i2c_transfer (state->i2c, &msg, 1)) != 1) {
		printk ("%s: i2c write error (addr %02x, err == %i)\n",
			__FUNCTION__, state->config->demod_address, err);
		return -EREMOTEIO;
	}

	return 0;
}

static u8 i2c_readbytes (struct nxt2002_state* state, u8 reg, u8* buf, u8 len)
{
	u8 reg2 [] = { reg };

	struct i2c_msg msg [] = { { .addr = state->config->demod_address, .flags = 0, .buf = reg2, .len = 1 },
			{ .addr = state->config->demod_address, .flags = I2C_M_RD, .buf = buf, .len = len } };

	int err;

	if ((err = i2c_transfer (state->i2c, msg, 2)) != 2) {
		printk ("%s: i2c read error (addr %02x, err == %i)\n",
			__FUNCTION__, state->config->demod_address, err);
		return -EREMOTEIO;
	}

	return 0;
}

static u16 nxt2002_crc(u16 crc, u8 c)
{

	u8 i;
	u16 input = (u16) c & 0xFF;

	input<<=8;
	for(i=0 ;i<8 ;i++) {
		if((crc ^ input) & 0x8000)
			crc=(crc<<1)^CRC_CCIT_MASK;
		else
			crc<<=1;
	input<<=1;
	}
	return crc;
}

static int nxt2002_writereg_multibyte (struct nxt2002_state* state, u8 reg, u8* data, u8 len)
{
	u8 buf;
	dprintk("%s\n", __FUNCTION__);

	/* set multi register length */
	i2c_writebytes(state,0x34,&len,1);

	/* set mutli register register */
	i2c_writebytes(state,0x35,&reg,1);

	/* send the actual data */
	i2c_writebytes(state,0x36,data,len);

	/* toggle the multireg write bit*/
	buf = 0x02;
	i2c_writebytes(state,0x21,&buf,1);

	i2c_readbytes(state,0x21,&buf,1);

	if ((buf & 0x02) == 0)
		return 0;

	dprintk("Error writing multireg register %02X\n",reg);

	return 0;
}

static int nxt2002_readreg_multibyte (struct nxt2002_state* state, u8 reg, u8* data, u8 len)
{
	u8 len2;
	dprintk("%s\n", __FUNCTION__);

	/* set multi register length */
	len2 = len & 0x80;
	i2c_writebytes(state,0x34,&len2,1);

	/* set mutli register register */
	i2c_writebytes(state,0x35,&reg,1);

	/* send the actual data */
	i2c_readbytes(state,reg,data,len);

	return 0;
}

static void nxt2002_microcontroller_stop (struct nxt2002_state* state)
{
	u8 buf[2],counter = 0;
	dprintk("%s\n", __FUNCTION__);

	buf[0] = 0x80;
	i2c_writebytes(state,0x22,buf,1);

	while (counter < 20) {
		i2c_readbytes(state,0x31,buf,1);
		if (buf[0] & 0x40)
			return;
		msleep(10);
		counter++;
	}

	dprintk("Timeout waiting for micro to stop.. This is ok after firmware upload\n");
	return;
}

static void nxt2002_microcontroller_start (struct nxt2002_state* state)
{
	u8 buf;
	dprintk("%s\n", __FUNCTION__);

	buf = 0x00;
	i2c_writebytes(state,0x22,&buf,1);
}

static int nxt2002_writetuner (struct nxt2002_state* state, u8* data)
{
	u8 buf,count = 0;

	dprintk("Tuner Bytes: %02X %02X %02X %02X\n",data[0],data[1],data[2],data[3]);

	dprintk("%s\n", __FUNCTION__);
	/* stop the micro first */
	nxt2002_microcontroller_stop(state);

	/* set the i2c transfer speed to the tuner */
	buf = 0x03;
	i2c_writebytes(state,0x20,&buf,1);

	/* setup to transfer 4 bytes via i2c */
	buf = 0x04;
	i2c_writebytes(state,0x34,&buf,1);

	/* write actual tuner bytes */
	i2c_writebytes(state,0x36,data,4);

	/* set tuner i2c address */
	buf = 0xC2;
	i2c_writebytes(state,0x35,&buf,1);

	/* write UC Opmode to begin transfer */
	buf = 0x80;
	i2c_writebytes(state,0x21,&buf,1);

	while (count < 20) {
		i2c_readbytes(state,0x21,&buf,1);
		if ((buf & 0x80)== 0x00)
			return 0;
		msleep(100);
		count++;
	}

	printk("nxt2002: timeout error writing tuner\n");
	return 0;
}

static void nxt2002_agc_reset(struct nxt2002_state* state)
{
	u8 buf;
	dprintk("%s\n", __FUNCTION__);

	buf = 0x08;
	i2c_writebytes(state,0x08,&buf,1);

	buf = 0x00;
	i2c_writebytes(state,0x08,&buf,1);

	return;
}

static int nxt2002_load_firmware (struct dvb_frontend* fe, const struct firmware *fw)
{

	struct nxt2002_state* state = fe->demodulator_priv;
	u8 buf[256],written = 0,chunkpos = 0;
	u16 rambase,position,crc = 0;

	dprintk("%s\n", __FUNCTION__);
	dprintk("Firmware is %zu bytes\n",fw->size);

	/* Get the RAM base for this nxt2002 */
	i2c_readbytes(state,0x10,buf,1);

	if (buf[0] & 0x10)
		rambase = 0x1000;
	else
		rambase = 0x0000;

	dprintk("rambase on this nxt2002 is %04X\n",rambase);

	/* Hold the micro in reset while loading firmware */
	buf[0] = 0x80;
	i2c_writebytes(state,0x2B,buf,1);

	for (position = 0; position < fw->size ; position++) {
		if (written == 0) {
			crc = 0;
			chunkpos = 0x28;
			buf[0] = ((rambase + position) >> 8);
			buf[1] = (rambase + position) & 0xFF;
			buf[2] = 0x81;
			/* write starting address */
			i2c_writebytes(state,0x29,buf,3);
		}
		written++;
		chunkpos++;

		if ((written % 4) == 0)
			i2c_writebytes(state,chunkpos,&fw->data[position-3],4);

		crc = nxt2002_crc(crc,fw->data[position]);

		if ((written == 255) || (position+1 == fw->size)) {
			/* write remaining bytes of firmware */
			i2c_writebytes(state, chunkpos+4-(written %4),
				&fw->data[position-(written %4) + 1],
				written %4);
			buf[0] = crc << 8;
			buf[1] = crc & 0xFF;

			/* write crc */
			i2c_writebytes(state,0x2C,buf,2);

			/* do a read to stop things */
			i2c_readbytes(state,0x2A,buf,1);

			/* set transfer mode to complete */
			buf[0] = 0x80;
			i2c_writebytes(state,0x2B,buf,1);

			written = 0;
		}
	}

	printk ("done.\n");
	return 0;
};

static int nxt2002_setup_frontend_parameters (struct dvb_frontend* fe,
					     struct dvb_frontend_parameters *p)
{
	struct nxt2002_state* state = fe->demodulator_priv;
	u32 freq = 0;
	u16 tunerfreq = 0;
	u8 buf[4];

	freq = 44000 + ( p->frequency / 1000 );

	dprintk("freq = %d      p->frequency = %d\n",freq,p->frequency);

	tunerfreq = freq * 24/4000;

	buf[0] = (tunerfreq >> 8) & 0x7F;
	buf[1] = (tunerfreq & 0xFF);

	if (p->frequency <= 214000000) {
		buf[2] = 0x84 + (0x06 << 3);
		buf[3] = (p->frequency <= 172000000) ? 0x01 : 0x02;
	} else if (p->frequency <= 721000000) {
		buf[2] = 0x84 + (0x07 << 3);
		buf[3] = (p->frequency <= 467000000) ? 0x02 : 0x08;
	} else if (p->frequency <= 841000000) {
		buf[2] = 0x84 + (0x0E << 3);
		buf[3] = 0x08;
	} else {
		buf[2] = 0x84 + (0x0F << 3);
		buf[3] = 0x02;
	}

	/* write frequency information */
	nxt2002_writetuner(state,buf);

	/* reset the agc now that tuning has been completed */
	nxt2002_agc_reset(state);

	/* set target power level */
	switch (p->u.vsb.modulation) {
		case QAM_64:
		case QAM_256:
				buf[0] = 0x74;
				break;
		case VSB_8:
				buf[0] = 0x70;
				break;
		default:
				return -EINVAL;
				break;
	}
	i2c_writebytes(state,0x42,buf,1);

	/* configure sdm */
	buf[0] = 0x87;
	i2c_writebytes(state,0x57,buf,1);

	/* write sdm1 input */
	buf[0] = 0x10;
	buf[1] = 0x00;
	nxt2002_writereg_multibyte(state,0x58,buf,2);

	/* write sdmx input */
	switch (p->u.vsb.modulation) {
		case QAM_64:
				buf[0] = 0x68;
				break;
		case QAM_256:
				buf[0] = 0x64;
				break;
		case VSB_8:
				buf[0] = 0x60;
				break;
		default:
				return -EINVAL;
				break;
	}
	buf[1] = 0x00;
	nxt2002_writereg_multibyte(state,0x5C,buf,2);

	/* write adc power lpf fc */
	buf[0] = 0x05;
	i2c_writebytes(state,0x43,buf,1);

	/* write adc power lpf fc */
	buf[0] = 0x05;
	i2c_writebytes(state,0x43,buf,1);

	/* write accumulator2 input */
	buf[0] = 0x80;
	buf[1] = 0x00;
	nxt2002_writereg_multibyte(state,0x4B,buf,2);

	/* write kg1 */
	buf[0] = 0x00;
	i2c_writebytes(state,0x4D,buf,1);

	/* write sdm12 lpf fc */
	buf[0] = 0x44;
	i2c_writebytes(state,0x55,buf,1);

	/* write agc control reg */
	buf[0] = 0x04;
	i2c_writebytes(state,0x41,buf,1);

	/* write agc ucgp0 */
	switch (p->u.vsb.modulation) {
		case QAM_64:
				buf[0] = 0x02;
				break;
		case QAM_256:
				buf[0] = 0x03;
				break;
		case VSB_8:
				buf[0] = 0x00;
				break;
		default:
				return -EINVAL;
				break;
	}
	i2c_writebytes(state,0x30,buf,1);

	/* write agc control reg */
	buf[0] = 0x00;
	i2c_writebytes(state,0x41,buf,1);

	/* write accumulator2 input */
	buf[0] = 0x80;
	buf[1] = 0x00;
	nxt2002_writereg_multibyte(state,0x49,buf,2);
	nxt2002_writereg_multibyte(state,0x4B,buf,2);

	/* write agc control reg */
	buf[0] = 0x04;
	i2c_writebytes(state,0x41,buf,1);

	nxt2002_microcontroller_start(state);

	/* adjacent channel detection should be done here, but I don't
	have any stations with this need so I cannot test it */

	return 0;
}

static int nxt2002_read_status(struct dvb_frontend* fe, fe_status_t* status)
{
	struct nxt2002_state* state = fe->demodulator_priv;
	u8 lock;
	i2c_readbytes(state,0x31,&lock,1);

	*status = 0;
	if (lock & 0x20) {
		*status |= FE_HAS_SIGNAL;
		*status |= FE_HAS_CARRIER;
		*status |= FE_HAS_VITERBI;
		*status |= FE_HAS_SYNC;
		*status |= FE_HAS_LOCK;
	}
	return 0;
}

static int nxt2002_read_ber(struct dvb_frontend* fe, u32* ber)
{
	struct nxt2002_state* state = fe->demodulator_priv;
	u8 b[3];

	nxt2002_readreg_multibyte(state,0xE6,b,3);

	*ber = ((b[0] << 8) + b[1]) * 8;

	return 0;
}

static int nxt2002_read_signal_strength(struct dvb_frontend* fe, u16* strength)
{
	struct nxt2002_state* state = fe->demodulator_priv;
	u8 b[2];
	u16 temp = 0;

	/* setup to read cluster variance */
	b[0] = 0x00;
	i2c_writebytes(state,0xA1,b,1);

	/* get multreg val */
	nxt2002_readreg_multibyte(state,0xA6,b,2);

	temp = (b[0] << 8) | b[1];
	*strength = ((0x7FFF - temp) & 0x0FFF) * 16;

	return 0;
}

static int nxt2002_read_snr(struct dvb_frontend* fe, u16* snr)
{

	struct nxt2002_state* state = fe->demodulator_priv;
	u8 b[2];
	u16 temp = 0, temp2;
	u32 snrdb = 0;

	/* setup to read cluster variance */
	b[0] = 0x00;
	i2c_writebytes(state,0xA1,b,1);

	/* get multreg val from 0xA6 */
	nxt2002_readreg_multibyte(state,0xA6,b,2);

	temp = (b[0] << 8) | b[1];
	temp2 = 0x7FFF - temp;

	/* snr will be in db */
	if (temp2 > 0x7F00)
		snrdb = 1000*24 + ( 1000*(30-24) * ( temp2 - 0x7F00 ) / ( 0x7FFF - 0x7F00 ) );
	else if (temp2 > 0x7EC0)
		snrdb = 1000*18 + ( 1000*(24-18) * ( temp2 - 0x7EC0 ) / ( 0x7F00 - 0x7EC0 ) );
	else if (temp2 > 0x7C00)
		snrdb = 1000*12 + ( 1000*(18-12) * ( temp2 - 0x7C00 ) / ( 0x7EC0 - 0x7C00 ) );
	else
		snrdb = 1000*0 + ( 1000*(12-0) * ( temp2 - 0 ) / ( 0x7C00 - 0 ) );

	/* the value reported back from the frontend will be FFFF=32db 0000=0db */

	*snr = snrdb * (0xFFFF/32000);

	return 0;
}

static int nxt2002_read_ucblocks(struct dvb_frontend* fe, u32* ucblocks)
{
	struct nxt2002_state* state = fe->demodulator_priv;
	u8 b[3];

	nxt2002_readreg_multibyte(state,0xE6,b,3);
	*ucblocks = b[2];

	return 0;
}

static int nxt2002_sleep(struct dvb_frontend* fe)
{
	return 0;
}

static int nxt2002_init(struct dvb_frontend* fe)
{
	struct nxt2002_state* state = fe->demodulator_priv;
	const struct firmware *fw;
	int ret;
	u8 buf[2];

	if (!state->initialised) {
		/* request the firmware, this will block until someone uploads it */
		printk("nxt2002: Waiting for firmware upload (%s)...\n", NXT2002_DEFAULT_FIRMWARE);
		ret = state->config->request_firmware(fe, &fw, NXT2002_DEFAULT_FIRMWARE);
		printk("nxt2002: Waiting for firmware upload(2)...\n");
		if (ret) {
			printk("nxt2002: no firmware upload (timeout or file not found?)\n");
			return ret;
		}

		ret = nxt2002_load_firmware(fe, fw);
		if (ret) {
			printk("nxt2002: writing firmware to device failed\n");
			release_firmware(fw);
			return ret;
		}
		printk("nxt2002: firmware upload complete\n");

		/* Put the micro into reset */
		nxt2002_microcontroller_stop(state);

		/* ensure transfer is complete */
		buf[0]=0;
		i2c_writebytes(state,0x2B,buf,1);

		/* Put the micro into reset for real this time */
		nxt2002_microcontroller_stop(state);

		/* soft reset everything (agc,frontend,eq,fec)*/
		buf[0] = 0x0F;
		i2c_writebytes(state,0x08,buf,1);
		buf[0] = 0x00;
		i2c_writebytes(state,0x08,buf,1);

		/* write agc sdm configure */
		buf[0] = 0xF1;
		i2c_writebytes(state,0x57,buf,1);

		/* write mod output format */
		buf[0] = 0x20;
		i2c_writebytes(state,0x09,buf,1);

		/* write fec mpeg mode */
		buf[0] = 0x7E;
		buf[1] = 0x00;
		i2c_writebytes(state,0xE9,buf,2);

		/* write mux selection */
		buf[0] = 0x00;
		i2c_writebytes(state,0xCC,buf,1);

		state->initialised = 1;
	}

	return 0;
}

static int nxt2002_get_tune_settings(struct dvb_frontend* fe, struct dvb_frontend_tune_settings* fesettings)
{
	fesettings->min_delay_ms = 500;
	fesettings->step_size = 0;
	fesettings->max_drift = 0;
	return 0;
}

static void nxt2002_release(struct dvb_frontend* fe)
{
	struct nxt2002_state* state = fe->demodulator_priv;
	kfree(state);
}

static struct dvb_frontend_ops nxt2002_ops;

struct dvb_frontend* nxt2002_attach(const struct nxt2002_config* config,
				   struct i2c_adapter* i2c)
{
	struct nxt2002_state* state = NULL;
	u8 buf [] = {0,0,0,0,0};

	/* allocate memory for the internal state */
	state = kmalloc(sizeof(struct nxt2002_state), GFP_KERNEL);
	if (state == NULL) goto error;

	/* setup the state */
	state->config = config;
	state->i2c = i2c;
	memcpy(&state->ops, &nxt2002_ops, sizeof(struct dvb_frontend_ops));
	state->initialised = 0;

	/* Check the first 5 registers to ensure this a revision we can handle */

	i2c_readbytes(state, 0x00, buf, 5);
	if (buf[0] != 0x04) goto error;		/* device id */
	if (buf[1] != 0x02) goto error;		/* fab id */
	if (buf[2] != 0x11) goto error;		/* month */
	if (buf[3] != 0x20) goto error;		/* year msb */
	if (buf[4] != 0x00) goto error;		/* year lsb */

	/* create dvb_frontend */
	state->frontend.ops = &state->ops;
	state->frontend.demodulator_priv = state;
	return &state->frontend;

error:
	kfree(state);
	return NULL;
}

static struct dvb_frontend_ops nxt2002_ops = {

	.info = {
		.name = "Nextwave nxt2002 VSB/QAM frontend",
		.type = FE_ATSC,
		.frequency_min =  54000000,
		.frequency_max = 860000000,
		/* stepsize is just a guess */
		.frequency_stepsize = 166666,
		.caps = FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
			FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
			FE_CAN_8VSB | FE_CAN_QAM_64 | FE_CAN_QAM_256
	},

	.release = nxt2002_release,

	.init = nxt2002_init,
	.sleep = nxt2002_sleep,

	.set_frontend = nxt2002_setup_frontend_parameters,
	.get_tune_settings = nxt2002_get_tune_settings,

	.read_status = nxt2002_read_status,
	.read_ber = nxt2002_read_ber,
	.read_signal_strength = nxt2002_read_signal_strength,
	.read_snr = nxt2002_read_snr,
	.read_ucblocks = nxt2002_read_ucblocks,

};

module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off frontend debugging (default:off).");

MODULE_DESCRIPTION("NXT2002 ATSC (8VSB & ITU J83 AnnexB FEC QAM64/256) demodulator driver");
MODULE_AUTHOR("Taylor Jacob");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL(nxt2002_attach);
