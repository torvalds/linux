/*
 *    Support for NXT2002 and NXT2004 - VSB/QAM
 *
 *    Copyright (C) 2005 Kirk Lapray (kirk.lapray@gmail.com)
 *    based on nxt2002 by Taylor Jacob <rtjacob@earthlink.net>
 *    and nxt2004 by Jean-Francois Thibert (jeanfrancois@sagetv.com)
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
 *                      NOTES ABOUT THIS DRIVER
 *
 * This Linux driver supports:
 *   B2C2/BBTI Technisat Air2PC - ATSC (NXT2002)
 *   AverTVHD MCE A180 (NXT2004)
 *   ATI HDTV Wonder (NXT2004)
 *
 * This driver needs external firmware. Please use the command
 * "<kerneldir>/Documentation/dvb/get_dvb_firmware nxt2002" or
 * "<kerneldir>/Documentation/dvb/get_dvb_firmware nxt2004" to
 * download/extract the appropriate firmware, and then copy it to
 * /usr/lib/hotplug/firmware/ or /lib/firmware/
 * (depending on configuration of firmware hotplug).
 */
#define NXT2002_DEFAULT_FIRMWARE "dvb-fe-nxt2002.fw"
#define NXT2004_DEFAULT_FIRMWARE "dvb-fe-nxt2004.fw"
#define CRC_CCIT_MASK 0x1021

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>

#include "dvb_frontend.h"
#include "dvb-pll.h"
#include "nxt200x.h"

struct nxt200x_state {

	struct i2c_adapter* i2c;
	struct dvb_frontend_ops ops;
	const struct nxt200x_config* config;
	struct dvb_frontend frontend;

	/* demodulator private data */
	nxt_chip_type demod_chip;
	u8 initialised:1;
};

static int debug;
#define dprintk(args...) \
	do { \
		if (debug) printk(KERN_DEBUG "nxt200x: " args); \
	} while (0)

static int i2c_writebytes (struct nxt200x_state* state, u8 addr, u8 *buf, u8 len)
{
	int err;
	struct i2c_msg msg = { .addr = addr, .flags = 0, .buf = buf, .len = len };

	if ((err = i2c_transfer (state->i2c, &msg, 1)) != 1) {
		printk (KERN_WARNING "nxt200x: %s: i2c write error (addr 0x%02x, err == %i)\n",
			__FUNCTION__, addr, err);
		return -EREMOTEIO;
	}
	return 0;
}

static u8 i2c_readbytes (struct nxt200x_state* state, u8 addr, u8* buf, u8 len)
{
	int err;
	struct i2c_msg msg = { .addr = addr, .flags = I2C_M_RD, .buf = buf, .len = len };

	if ((err = i2c_transfer (state->i2c, &msg, 1)) != 1) {
		printk (KERN_WARNING "nxt200x: %s: i2c read error (addr 0x%02x, err == %i)\n",
			__FUNCTION__, addr, err);
		return -EREMOTEIO;
	}
	return 0;
}

static int nxt200x_writebytes (struct nxt200x_state* state, u8 reg, u8 *buf, u8 len)
{
	u8 buf2 [len+1];
	int err;
	struct i2c_msg msg = { .addr = state->config->demod_address, .flags = 0, .buf = buf2, .len = len + 1 };

	buf2[0] = reg;
	memcpy(&buf2[1], buf, len);

	if ((err = i2c_transfer (state->i2c, &msg, 1)) != 1) {
		printk (KERN_WARNING "nxt200x: %s: i2c write error (addr 0x%02x, err == %i)\n",
			__FUNCTION__, state->config->demod_address, err);
		return -EREMOTEIO;
	}
	return 0;
}

static u8 nxt200x_readbytes (struct nxt200x_state* state, u8 reg, u8* buf, u8 len)
{
	u8 reg2 [] = { reg };

	struct i2c_msg msg [] = { { .addr = state->config->demod_address, .flags = 0, .buf = reg2, .len = 1 },
			{ .addr = state->config->demod_address, .flags = I2C_M_RD, .buf = buf, .len = len } };

	int err;

	if ((err = i2c_transfer (state->i2c, msg, 2)) != 2) {
		printk (KERN_WARNING "nxt200x: %s: i2c read error (addr 0x%02x, err == %i)\n",
			__FUNCTION__, state->config->demod_address, err);
		return -EREMOTEIO;
	}
	return 0;
}

static u16 nxt200x_crc(u16 crc, u8 c)
{
	u8 i;
	u16 input = (u16) c & 0xFF;

	input<<=8;
	for(i=0; i<8; i++) {
		if((crc^input) & 0x8000)
			crc=(crc<<1)^CRC_CCIT_MASK;
		else
			crc<<=1;
		input<<=1;
	}
	return crc;
}

static int nxt200x_writereg_multibyte (struct nxt200x_state* state, u8 reg, u8* data, u8 len)
{
	u8 attr, len2, buf;
	dprintk("%s\n", __FUNCTION__);

	/* set mutli register register */
	nxt200x_writebytes(state, 0x35, &reg, 1);

	/* send the actual data */
	nxt200x_writebytes(state, 0x36, data, len);

	switch (state->demod_chip) {
		case NXT2002:
			len2 = len;
			buf = 0x02;
			break;
		case NXT2004:
			/* probably not right, but gives correct values */
			attr = 0x02;
			if (reg & 0x80) {
				attr = attr << 1;
				if (reg & 0x04)
					attr = attr >> 1;
			}
			/* set write bit */
			len2 = ((attr << 4) | 0x10) | len;
			buf = 0x80;
			break;
		default:
			return -EINVAL;
			break;
	}

	/* set multi register length */
	nxt200x_writebytes(state, 0x34, &len2, 1);

	/* toggle the multireg write bit */
	nxt200x_writebytes(state, 0x21, &buf, 1);

	nxt200x_readbytes(state, 0x21, &buf, 1);

	switch (state->demod_chip) {
		case NXT2002:
			if ((buf & 0x02) == 0)
				return 0;
			break;
		case NXT2004:
			if (buf == 0)
				return 0;
			break;
		default:
			return -EINVAL;
			break;
	}

	printk(KERN_WARNING "nxt200x: Error writing multireg register 0x%02X\n",reg);

	return 0;
}

static int nxt200x_readreg_multibyte (struct nxt200x_state* state, u8 reg, u8* data, u8 len)
{
	int i;
	u8 buf, len2, attr;
	dprintk("%s\n", __FUNCTION__);

	/* set mutli register register */
	nxt200x_writebytes(state, 0x35, &reg, 1);

	switch (state->demod_chip) {
		case NXT2002:
			/* set multi register length */
			len2 = len & 0x80;
			nxt200x_writebytes(state, 0x34, &len2, 1);

			/* read the actual data */
			nxt200x_readbytes(state, reg, data, len);
			return 0;
			break;
		case NXT2004:
			/* probably not right, but gives correct values */
			attr = 0x02;
			if (reg & 0x80) {
				attr = attr << 1;
				if (reg & 0x04)
					attr = attr >> 1;
			}

			/* set multi register length */
			len2 = (attr << 4) | len;
			nxt200x_writebytes(state, 0x34, &len2, 1);

			/* toggle the multireg bit*/
			buf = 0x80;
			nxt200x_writebytes(state, 0x21, &buf, 1);

			/* read the actual data */
			for(i = 0; i < len; i++) {
				nxt200x_readbytes(state, 0x36 + i, &data[i], 1);
			}
			return 0;
			break;
		default:
			return -EINVAL;
			break;
	}
}

static void nxt200x_microcontroller_stop (struct nxt200x_state* state)
{
	u8 buf, stopval, counter = 0;
	dprintk("%s\n", __FUNCTION__);

	/* set correct stop value */
	switch (state->demod_chip) {
		case NXT2002:
			stopval = 0x40;
			break;
		case NXT2004:
			stopval = 0x10;
			break;
		default:
			stopval = 0;
			break;
	}

	buf = 0x80;
	nxt200x_writebytes(state, 0x22, &buf, 1);

	while (counter < 20) {
		nxt200x_readbytes(state, 0x31, &buf, 1);
		if (buf & stopval)
			return;
		msleep(10);
		counter++;
	}

	printk(KERN_WARNING "nxt200x: Timeout waiting for nxt200x to stop. This is ok after firmware upload.\n");
	return;
}

static void nxt200x_microcontroller_start (struct nxt200x_state* state)
{
	u8 buf;
	dprintk("%s\n", __FUNCTION__);

	buf = 0x00;
	nxt200x_writebytes(state, 0x22, &buf, 1);
}

static void nxt2004_microcontroller_init (struct nxt200x_state* state)
{
	u8 buf[9];
	u8 counter = 0;
	dprintk("%s\n", __FUNCTION__);

	buf[0] = 0x00;
	nxt200x_writebytes(state, 0x2b, buf, 1);
	buf[0] = 0x70;
	nxt200x_writebytes(state, 0x34, buf, 1);
	buf[0] = 0x04;
	nxt200x_writebytes(state, 0x35, buf, 1);
	buf[0] = 0x01; buf[1] = 0x23; buf[2] = 0x45; buf[3] = 0x67; buf[4] = 0x89;
	buf[5] = 0xAB; buf[6] = 0xCD; buf[7] = 0xEF; buf[8] = 0xC0;
	nxt200x_writebytes(state, 0x36, buf, 9);
	buf[0] = 0x80;
	nxt200x_writebytes(state, 0x21, buf, 1);

	while (counter < 20) {
		nxt200x_readbytes(state, 0x21, buf, 1);
		if (buf[0] == 0)
			return;
		msleep(10);
		counter++;
	}

	printk(KERN_WARNING "nxt200x: Timeout waiting for nxt2004 to init.\n");

	return;
}

static int nxt200x_writetuner (struct nxt200x_state* state, u8* data)
{
	u8 buf, count = 0;

	dprintk("%s\n", __FUNCTION__);

	dprintk("Tuner Bytes: %02X %02X %02X %02X\n", data[0], data[1], data[2], data[3]);

	/* if NXT2004, write directly to tuner. if NXT2002, write through NXT chip.
	 * direct write is required for Philips TUV1236D and ALPS TDHU2 */
	switch (state->demod_chip) {
		case NXT2004:
			if (i2c_writebytes(state, state->config->pll_address, data, 4))
	        	        printk(KERN_WARNING "nxt200x: error writing to tuner\n");
			/* wait until we have a lock */
			while (count < 20) {
				i2c_readbytes(state, state->config->pll_address, &buf, 1);
				if (buf & 0x40)
					return 0;
				msleep(100);
				count++;
			}
			printk("nxt2004: timeout waiting for tuner lock\n");
			break;
		case NXT2002:
			/* set the i2c transfer speed to the tuner */
			buf = 0x03;
			nxt200x_writebytes(state, 0x20, &buf, 1);

			/* setup to transfer 4 bytes via i2c */
			buf = 0x04;
			nxt200x_writebytes(state, 0x34, &buf, 1);

			/* write actual tuner bytes */
			nxt200x_writebytes(state, 0x36, data, 4);

			/* set tuner i2c address */
			buf = state->config->pll_address;
			nxt200x_writebytes(state, 0x35, &buf, 1);

			/* write UC Opmode to begin transfer */
			buf = 0x80;
			nxt200x_writebytes(state, 0x21, &buf, 1);

			while (count < 20) {
				nxt200x_readbytes(state, 0x21, &buf, 1);
				if ((buf & 0x80)== 0x00)
					return 0;
				msleep(100);
				count++;
			}
			printk("nxt2002: timeout error writing tuner\n");
			break;
		default:
			return -EINVAL;
			break;
	}
	return 0;
}

static void nxt200x_agc_reset(struct nxt200x_state* state)
{
	u8 buf;
	dprintk("%s\n", __FUNCTION__);

	switch (state->demod_chip) {
		case NXT2002:
			buf = 0x08;
			nxt200x_writebytes(state, 0x08, &buf, 1);
			buf = 0x00;
			nxt200x_writebytes(state, 0x08, &buf, 1);
			break;
		case NXT2004:
			nxt200x_readreg_multibyte(state, 0x08, &buf, 1);
			buf = 0x08;
			nxt200x_writereg_multibyte(state, 0x08, &buf, 1);
			buf = 0x00;
			nxt200x_writereg_multibyte(state, 0x08, &buf, 1);
			break;
		default:
			break;
	}
	return;
}

static int nxt2002_load_firmware (struct dvb_frontend* fe, const struct firmware *fw)
{

	struct nxt200x_state* state = fe->demodulator_priv;
	u8 buf[3], written = 0, chunkpos = 0;
	u16 rambase, position, crc = 0;

	dprintk("%s\n", __FUNCTION__);
	dprintk("Firmware is %zu bytes\n", fw->size);

	/* Get the RAM base for this nxt2002 */
	nxt200x_readbytes(state, 0x10, buf, 1);

	if (buf[0] & 0x10)
		rambase = 0x1000;
	else
		rambase = 0x0000;

	dprintk("rambase on this nxt2002 is %04X\n", rambase);

	/* Hold the micro in reset while loading firmware */
	buf[0] = 0x80;
	nxt200x_writebytes(state, 0x2B, buf, 1);

	for (position = 0; position < fw->size; position++) {
		if (written == 0) {
			crc = 0;
			chunkpos = 0x28;
			buf[0] = ((rambase + position) >> 8);
			buf[1] = (rambase + position) & 0xFF;
			buf[2] = 0x81;
			/* write starting address */
			nxt200x_writebytes(state, 0x29, buf, 3);
		}
		written++;
		chunkpos++;

		if ((written % 4) == 0)
			nxt200x_writebytes(state, chunkpos, &fw->data[position-3], 4);

		crc = nxt200x_crc(crc, fw->data[position]);

		if ((written == 255) || (position+1 == fw->size)) {
			/* write remaining bytes of firmware */
			nxt200x_writebytes(state, chunkpos+4-(written %4),
				&fw->data[position-(written %4) + 1],
				written %4);
			buf[0] = crc << 8;
			buf[1] = crc & 0xFF;

			/* write crc */
			nxt200x_writebytes(state, 0x2C, buf, 2);

			/* do a read to stop things */
			nxt200x_readbytes(state, 0x2A, buf, 1);

			/* set transfer mode to complete */
			buf[0] = 0x80;
			nxt200x_writebytes(state, 0x2B, buf, 1);

			written = 0;
		}
	}

	return 0;
};

static int nxt2004_load_firmware (struct dvb_frontend* fe, const struct firmware *fw)
{

	struct nxt200x_state* state = fe->demodulator_priv;
	u8 buf[3];
	u16 rambase, position, crc=0;

	dprintk("%s\n", __FUNCTION__);
	dprintk("Firmware is %zu bytes\n", fw->size);

	/* set rambase */
	rambase = 0x1000;

	/* hold the micro in reset while loading firmware */
	buf[0] = 0x80;
	nxt200x_writebytes(state, 0x2B, buf,1);

	/* calculate firmware CRC */
	for (position = 0; position < fw->size; position++) {
	        crc = nxt200x_crc(crc, fw->data[position]);
	}

	buf[0] = rambase >> 8;
	buf[1] = rambase & 0xFF;
	buf[2] = 0x81;
	/* write starting address */
	nxt200x_writebytes(state,0x29,buf,3);

	for (position = 0; position < fw->size;) {
		nxt200x_writebytes(state, 0x2C, &fw->data[position],
			fw->size-position > 255 ? 255 : fw->size-position);
		position += (fw->size-position > 255 ? 255 : fw->size-position);
	}
	buf[0] = crc >> 8;
	buf[1] = crc & 0xFF;

	dprintk("firmware crc is 0x%02X 0x%02X\n", buf[0], buf[1]);

	/* write crc */
	nxt200x_writebytes(state, 0x2C, buf,2);

	/* do a read to stop things */
	nxt200x_readbytes(state, 0x2C, buf, 1);

	/* set transfer mode to complete */
	buf[0] = 0x80;
	nxt200x_writebytes(state, 0x2B, buf,1);

	return 0;
};

static int nxt200x_setup_frontend_parameters (struct dvb_frontend* fe,
					     struct dvb_frontend_parameters *p)
{
	struct nxt200x_state* state = fe->demodulator_priv;
	u8 buf[4];

	/* stop the micro first */
	nxt200x_microcontroller_stop(state);

	if (state->demod_chip == NXT2004) {
		/* make sure demod is set to digital */
		buf[0] = 0x04;
		nxt200x_writebytes(state, 0x14, buf, 1);
		buf[0] = 0x00;
		nxt200x_writebytes(state, 0x17, buf, 1);
	}

	/* get tuning information */
	dvb_pll_configure(state->config->pll_desc, buf, p->frequency, 0);

	/* set additional params */
	switch (p->u.vsb.modulation) {
		case QAM_64:
		case QAM_256:
			/* Set punctured clock for QAM */
			/* This is just a guess since I am unable to test it */
			if (state->config->set_ts_params)
				state->config->set_ts_params(fe, 1);

			/* set input */
			if (state->config->set_pll_input)
				state->config->set_pll_input(buf, 1);
			break;
		case VSB_8:
			/* Set non-punctured clock for VSB */
			if (state->config->set_ts_params)
				state->config->set_ts_params(fe, 0);

			/* set input */
			if (state->config->set_pll_input)
				state->config->set_pll_input(buf, 0);
			break;
		default:
			return -EINVAL;
			break;
	}

	/* write frequency information */
	nxt200x_writetuner(state, buf);

	/* reset the agc now that tuning has been completed */
	nxt200x_agc_reset(state);

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
	nxt200x_writebytes(state, 0x42, buf, 1);

	/* configure sdm */
	switch (state->demod_chip) {
		case NXT2002:
			buf[0] = 0x87;
			break;
		case NXT2004:
			buf[0] = 0x07;
			break;
		default:
			return -EINVAL;
			break;
	}
	nxt200x_writebytes(state, 0x57, buf, 1);

	/* write sdm1 input */
	buf[0] = 0x10;
	buf[1] = 0x00;
	nxt200x_writebytes(state, 0x58, buf, 2);

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
	nxt200x_writebytes(state, 0x5C, buf, 2);

	/* write adc power lpf fc */
	buf[0] = 0x05;
	nxt200x_writebytes(state, 0x43, buf, 1);

	if (state->demod_chip == NXT2004) {
		/* write ??? */
		buf[0] = 0x00;
		buf[1] = 0x00;
		nxt200x_writebytes(state, 0x46, buf, 2);
	}

	/* write accumulator2 input */
	buf[0] = 0x80;
	buf[1] = 0x00;
	nxt200x_writebytes(state, 0x4B, buf, 2);

	/* write kg1 */
	buf[0] = 0x00;
	nxt200x_writebytes(state, 0x4D, buf, 1);

	/* write sdm12 lpf fc */
	buf[0] = 0x44;
	nxt200x_writebytes(state, 0x55, buf, 1);

	/* write agc control reg */
	buf[0] = 0x04;
	nxt200x_writebytes(state, 0x41, buf, 1);

	if (state->demod_chip == NXT2004) {
		nxt200x_readreg_multibyte(state, 0x80, buf, 1);
		buf[0] = 0x24;
		nxt200x_writereg_multibyte(state, 0x80, buf, 1);

		/* soft reset? */
		nxt200x_readreg_multibyte(state, 0x08, buf, 1);
		buf[0] = 0x10;
		nxt200x_writereg_multibyte(state, 0x08, buf, 1);
		nxt200x_readreg_multibyte(state, 0x08, buf, 1);
		buf[0] = 0x00;
		nxt200x_writereg_multibyte(state, 0x08, buf, 1);

		nxt200x_readreg_multibyte(state, 0x80, buf, 1);
		buf[0] = 0x04;
		nxt200x_writereg_multibyte(state, 0x80, buf, 1);
		buf[0] = 0x00;
		nxt200x_writereg_multibyte(state, 0x81, buf, 1);
		buf[0] = 0x80; buf[1] = 0x00; buf[2] = 0x00;
		nxt200x_writereg_multibyte(state, 0x82, buf, 3);
		nxt200x_readreg_multibyte(state, 0x88, buf, 1);
		buf[0] = 0x11;
		nxt200x_writereg_multibyte(state, 0x88, buf, 1);
		nxt200x_readreg_multibyte(state, 0x80, buf, 1);
		buf[0] = 0x44;
		nxt200x_writereg_multibyte(state, 0x80, buf, 1);
	}

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
	nxt200x_writebytes(state, 0x30, buf, 1);

	/* write agc control reg */
	buf[0] = 0x00;
	nxt200x_writebytes(state, 0x41, buf, 1);

	/* write accumulator2 input */
	buf[0] = 0x80;
	buf[1] = 0x00;
	nxt200x_writebytes(state, 0x49, buf,2);
	nxt200x_writebytes(state, 0x4B, buf,2);

	/* write agc control reg */
	buf[0] = 0x04;
	nxt200x_writebytes(state, 0x41, buf, 1);

	nxt200x_microcontroller_start(state);

	if (state->demod_chip == NXT2004) {
		nxt2004_microcontroller_init(state);

		/* ???? */
		buf[0] = 0xF0;
		buf[1] = 0x00;
		nxt200x_writebytes(state, 0x5C, buf, 2);
	}

	/* adjacent channel detection should be done here, but I don't
	have any stations with this need so I cannot test it */

	return 0;
}

static int nxt200x_read_status(struct dvb_frontend* fe, fe_status_t* status)
{
	struct nxt200x_state* state = fe->demodulator_priv;
	u8 lock;
	nxt200x_readbytes(state, 0x31, &lock, 1);

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

static int nxt200x_read_ber(struct dvb_frontend* fe, u32* ber)
{
	struct nxt200x_state* state = fe->demodulator_priv;
	u8 b[3];

	nxt200x_readreg_multibyte(state, 0xE6, b, 3);

	*ber = ((b[0] << 8) + b[1]) * 8;

	return 0;
}

static int nxt200x_read_signal_strength(struct dvb_frontend* fe, u16* strength)
{
	struct nxt200x_state* state = fe->demodulator_priv;
	u8 b[2];
	u16 temp = 0;

	/* setup to read cluster variance */
	b[0] = 0x00;
	nxt200x_writebytes(state, 0xA1, b, 1);

	/* get multreg val */
	nxt200x_readreg_multibyte(state, 0xA6, b, 2);

	temp = (b[0] << 8) | b[1];
	*strength = ((0x7FFF - temp) & 0x0FFF) * 16;

	return 0;
}

static int nxt200x_read_snr(struct dvb_frontend* fe, u16* snr)
{

	struct nxt200x_state* state = fe->demodulator_priv;
	u8 b[2];
	u16 temp = 0, temp2;
	u32 snrdb = 0;

	/* setup to read cluster variance */
	b[0] = 0x00;
	nxt200x_writebytes(state, 0xA1, b, 1);

	/* get multreg val from 0xA6 */
	nxt200x_readreg_multibyte(state, 0xA6, b, 2);

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

static int nxt200x_read_ucblocks(struct dvb_frontend* fe, u32* ucblocks)
{
	struct nxt200x_state* state = fe->demodulator_priv;
	u8 b[3];

	nxt200x_readreg_multibyte(state, 0xE6, b, 3);
	*ucblocks = b[2];

	return 0;
}

static int nxt200x_sleep(struct dvb_frontend* fe)
{
	return 0;
}

static int nxt2002_init(struct dvb_frontend* fe)
{
	struct nxt200x_state* state = fe->demodulator_priv;
	const struct firmware *fw;
	int ret;
	u8 buf[2];

	/* request the firmware, this will block until someone uploads it */
	printk("nxt2002: Waiting for firmware upload (%s)...\n", NXT2002_DEFAULT_FIRMWARE);
	ret = request_firmware(&fw, NXT2002_DEFAULT_FIRMWARE, &state->i2c->dev);
	printk("nxt2002: Waiting for firmware upload(2)...\n");
	if (ret) {
		printk("nxt2002: No firmware uploaded (timeout or file not found?)\n");
		return ret;
	}

	ret = nxt2002_load_firmware(fe, fw);
	if (ret) {
		printk("nxt2002: Writing firmware to device failed\n");
		release_firmware(fw);
		return ret;
	}
	printk("nxt2002: Firmware upload complete\n");

	/* Put the micro into reset */
	nxt200x_microcontroller_stop(state);

	/* ensure transfer is complete */
	buf[0]=0x00;
	nxt200x_writebytes(state, 0x2B, buf, 1);

	/* Put the micro into reset for real this time */
	nxt200x_microcontroller_stop(state);

	/* soft reset everything (agc,frontend,eq,fec)*/
	buf[0] = 0x0F;
	nxt200x_writebytes(state, 0x08, buf, 1);
	buf[0] = 0x00;
	nxt200x_writebytes(state, 0x08, buf, 1);

	/* write agc sdm configure */
	buf[0] = 0xF1;
	nxt200x_writebytes(state, 0x57, buf, 1);

	/* write mod output format */
	buf[0] = 0x20;
	nxt200x_writebytes(state, 0x09, buf, 1);

	/* write fec mpeg mode */
	buf[0] = 0x7E;
	buf[1] = 0x00;
	nxt200x_writebytes(state, 0xE9, buf, 2);

	/* write mux selection */
	buf[0] = 0x00;
	nxt200x_writebytes(state, 0xCC, buf, 1);

	return 0;
}

static int nxt2004_init(struct dvb_frontend* fe)
{
	struct nxt200x_state* state = fe->demodulator_priv;
	const struct firmware *fw;
	int ret;
	u8 buf[3];

	/* ??? */
	buf[0]=0x00;
	nxt200x_writebytes(state, 0x1E, buf, 1);

	/* request the firmware, this will block until someone uploads it */
	printk("nxt2004: Waiting for firmware upload (%s)...\n", NXT2004_DEFAULT_FIRMWARE);
	ret = request_firmware(&fw, NXT2004_DEFAULT_FIRMWARE, &state->i2c->dev);
	printk("nxt2004: Waiting for firmware upload(2)...\n");
	if (ret) {
		printk("nxt2004: No firmware uploaded (timeout or file not found?)\n");
		return ret;
	}

	ret = nxt2004_load_firmware(fe, fw);
	if (ret) {
		printk("nxt2004: Writing firmware to device failed\n");
		release_firmware(fw);
		return ret;
	}
	printk("nxt2004: Firmware upload complete\n");

	/* ensure transfer is complete */
	buf[0] = 0x01;
	nxt200x_writebytes(state, 0x19, buf, 1);

	nxt2004_microcontroller_init(state);
	nxt200x_microcontroller_stop(state);
	nxt200x_microcontroller_stop(state);
	nxt2004_microcontroller_init(state);
	nxt200x_microcontroller_stop(state);

	/* soft reset everything (agc,frontend,eq,fec)*/
	buf[0] = 0xFF;
	nxt200x_writereg_multibyte(state, 0x08, buf, 1);
	buf[0] = 0x00;
	nxt200x_writereg_multibyte(state, 0x08, buf, 1);

	/* write agc sdm configure */
	buf[0] = 0xD7;
	nxt200x_writebytes(state, 0x57, buf, 1);

	/* ???*/
	buf[0] = 0x07;
	buf[1] = 0xfe;
	nxt200x_writebytes(state, 0x35, buf, 2);
	buf[0] = 0x12;
	nxt200x_writebytes(state, 0x34, buf, 1);
	buf[0] = 0x80;
	nxt200x_writebytes(state, 0x21, buf, 1);

	/* ???*/
	buf[0] = 0x21;
	nxt200x_writebytes(state, 0x0A, buf, 1);

	/* ???*/
	buf[0] = 0x01;
	nxt200x_writereg_multibyte(state, 0x80, buf, 1);

	/* write fec mpeg mode */
	buf[0] = 0x7E;
	buf[1] = 0x00;
	nxt200x_writebytes(state, 0xE9, buf, 2);

	/* write mux selection */
	buf[0] = 0x00;
	nxt200x_writebytes(state, 0xCC, buf, 1);

	/* ???*/
	nxt200x_readreg_multibyte(state, 0x80, buf, 1);
	buf[0] = 0x00;
	nxt200x_writereg_multibyte(state, 0x80, buf, 1);

	/* soft reset? */
	nxt200x_readreg_multibyte(state, 0x08, buf, 1);
	buf[0] = 0x10;
	nxt200x_writereg_multibyte(state, 0x08, buf, 1);
	nxt200x_readreg_multibyte(state, 0x08, buf, 1);
	buf[0] = 0x00;
	nxt200x_writereg_multibyte(state, 0x08, buf, 1);

	/* ???*/
	nxt200x_readreg_multibyte(state, 0x80, buf, 1);
	buf[0] = 0x01;
	nxt200x_writereg_multibyte(state, 0x80, buf, 1);
	buf[0] = 0x70;
	nxt200x_writereg_multibyte(state, 0x81, buf, 1);
	buf[0] = 0x31; buf[1] = 0x5E; buf[2] = 0x66;
	nxt200x_writereg_multibyte(state, 0x82, buf, 3);

	nxt200x_readreg_multibyte(state, 0x88, buf, 1);
	buf[0] = 0x11;
	nxt200x_writereg_multibyte(state, 0x88, buf, 1);
	nxt200x_readreg_multibyte(state, 0x80, buf, 1);
	buf[0] = 0x40;
	nxt200x_writereg_multibyte(state, 0x80, buf, 1);

	nxt200x_readbytes(state, 0x10, buf, 1);
	buf[0] = 0x10;
	nxt200x_writebytes(state, 0x10, buf, 1);
	nxt200x_readbytes(state, 0x0A, buf, 1);
	buf[0] = 0x21;
	nxt200x_writebytes(state, 0x0A, buf, 1);

	nxt2004_microcontroller_init(state);

	buf[0] = 0x21;
	nxt200x_writebytes(state, 0x0A, buf, 1);
	buf[0] = 0x7E;
	nxt200x_writebytes(state, 0xE9, buf, 1);
	buf[0] = 0x00;
	nxt200x_writebytes(state, 0xEA, buf, 1);

	nxt200x_readreg_multibyte(state, 0x80, buf, 1);
	buf[0] = 0x00;
	nxt200x_writereg_multibyte(state, 0x80, buf, 1);
	nxt200x_readreg_multibyte(state, 0x80, buf, 1);
	buf[0] = 0x00;
	nxt200x_writereg_multibyte(state, 0x80, buf, 1);

	/* soft reset? */
	nxt200x_readreg_multibyte(state, 0x08, buf, 1);
	buf[0] = 0x10;
	nxt200x_writereg_multibyte(state, 0x08, buf, 1);
	nxt200x_readreg_multibyte(state, 0x08, buf, 1);
	buf[0] = 0x00;
	nxt200x_writereg_multibyte(state, 0x08, buf, 1);

	nxt200x_readreg_multibyte(state, 0x80, buf, 1);
	buf[0] = 0x04;
	nxt200x_writereg_multibyte(state, 0x80, buf, 1);
	buf[0] = 0x00;
	nxt200x_writereg_multibyte(state, 0x81, buf, 1);
	buf[0] = 0x80; buf[1] = 0x00; buf[2] = 0x00;
	nxt200x_writereg_multibyte(state, 0x82, buf, 3);

	nxt200x_readreg_multibyte(state, 0x88, buf, 1);
	buf[0] = 0x11;
	nxt200x_writereg_multibyte(state, 0x88, buf, 1);

	nxt200x_readreg_multibyte(state, 0x80, buf, 1);
	buf[0] = 0x44;
	nxt200x_writereg_multibyte(state, 0x80, buf, 1);

	/* initialize tuner */
	nxt200x_readbytes(state, 0x10, buf, 1);
	buf[0] = 0x12;
	nxt200x_writebytes(state, 0x10, buf, 1);
	buf[0] = 0x04;
	nxt200x_writebytes(state, 0x13, buf, 1);
	buf[0] = 0x00;
	nxt200x_writebytes(state, 0x16, buf, 1);
	buf[0] = 0x04;
	nxt200x_writebytes(state, 0x14, buf, 1);
	buf[0] = 0x00;
	nxt200x_writebytes(state, 0x14, buf, 1);
	nxt200x_writebytes(state, 0x17, buf, 1);
	nxt200x_writebytes(state, 0x14, buf, 1);
	nxt200x_writebytes(state, 0x17, buf, 1);

	return 0;
}

static int nxt200x_init(struct dvb_frontend* fe)
{
	struct nxt200x_state* state = fe->demodulator_priv;
	int ret = 0;

	if (!state->initialised) {
		switch (state->demod_chip) {
			case NXT2002:
				ret = nxt2002_init(fe);
				break;
			case NXT2004:
				ret = nxt2004_init(fe);
				break;
			default:
				return -EINVAL;
				break;
		}
		state->initialised = 1;
	}
	return ret;
}

static int nxt200x_get_tune_settings(struct dvb_frontend* fe, struct dvb_frontend_tune_settings* fesettings)
{
	fesettings->min_delay_ms = 500;
	fesettings->step_size = 0;
	fesettings->max_drift = 0;
	return 0;
}

static void nxt200x_release(struct dvb_frontend* fe)
{
	struct nxt200x_state* state = fe->demodulator_priv;
	kfree(state);
}

static struct dvb_frontend_ops nxt200x_ops;

struct dvb_frontend* nxt200x_attach(const struct nxt200x_config* config,
				   struct i2c_adapter* i2c)
{
	struct nxt200x_state* state = NULL;
	u8 buf [] = {0,0,0,0,0};

	/* allocate memory for the internal state */
	state = (struct nxt200x_state*) kmalloc(sizeof(struct nxt200x_state), GFP_KERNEL);
	if (state == NULL)
		goto error;
	memset(state,0,sizeof(*state));

	/* setup the state */
	state->config = config;
	state->i2c = i2c;
	memcpy(&state->ops, &nxt200x_ops, sizeof(struct dvb_frontend_ops));
	state->initialised = 0;

	/* read card id */
	nxt200x_readbytes(state, 0x00, buf, 5);
	dprintk("NXT info: %02X %02X %02X %02X %02X\n",
		buf[0], buf[1], buf[2],	buf[3], buf[4]);

	/* set demod chip */
	switch (buf[0]) {
		case 0x04:
			state->demod_chip = NXT2002;
			printk("nxt200x: NXT2002 Detected\n");
			break;
		case 0x05:
			state->demod_chip = NXT2004;
			printk("nxt200x: NXT2004 Detected\n");
			break;
		default:
			goto error;
	}

	/* make sure demod chip is supported */
	switch (state->demod_chip) {
		case NXT2002:
			if (buf[0] != 0x04) goto error;		/* device id */
			if (buf[1] != 0x02) goto error;		/* fab id */
			if (buf[2] != 0x11) goto error;		/* month */
			if (buf[3] != 0x20) goto error;		/* year msb */
			if (buf[4] != 0x00) goto error;		/* year lsb */
			break;
		case NXT2004:
			if (buf[0] != 0x05) goto error;		/* device id */
			break;
		default:
			goto error;
	}

	/* create dvb_frontend */
	state->frontend.ops = &state->ops;
	state->frontend.demodulator_priv = state;
	return &state->frontend;

error:
	kfree(state);
	printk("Unknown/Unsupported NXT chip: %02X %02X %02X %02X %02X\n",
		buf[0], buf[1], buf[2], buf[3], buf[4]);
	return NULL;
}

static struct dvb_frontend_ops nxt200x_ops = {

	.info = {
		.name = "Nextwave NXT200X VSB/QAM frontend",
		.type = FE_ATSC,
		.frequency_min =  54000000,
		.frequency_max = 860000000,
		.frequency_stepsize = 166666,	/* stepsize is just a guess */
		.caps = FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
			FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
			FE_CAN_8VSB | FE_CAN_QAM_64 | FE_CAN_QAM_256
	},

	.release = nxt200x_release,

	.init = nxt200x_init,
	.sleep = nxt200x_sleep,

	.set_frontend = nxt200x_setup_frontend_parameters,
	.get_tune_settings = nxt200x_get_tune_settings,

	.read_status = nxt200x_read_status,
	.read_ber = nxt200x_read_ber,
	.read_signal_strength = nxt200x_read_signal_strength,
	.read_snr = nxt200x_read_snr,
	.read_ucblocks = nxt200x_read_ucblocks,
};

module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off frontend debugging (default:off).");

MODULE_DESCRIPTION("NXT200X (ATSC 8VSB & ITU-T J.83 AnnexB 64/256 QAM) Demodulator Driver");
MODULE_AUTHOR("Kirk Lapray, Jean-Francois Thibert, and Taylor Jacob");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL(nxt200x_attach);

