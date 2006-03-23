  /*
     Driver for Philips tda1004xh OFDM Demodulator

     (c) 2003, 2004 Andrew de Quincey & Robert Schlabbach

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
 * This driver needs external firmware. Please use the commands
 * "<kerneldir>/Documentation/dvb/get_dvb_firmware tda10045",
 * "<kerneldir>/Documentation/dvb/get_dvb_firmware tda10046" to
 * download/extract them, and then copy them to /usr/lib/hotplug/firmware
 * or /lib/firmware (depending on configuration of firmware hotplug).
 */
#define TDA10045_DEFAULT_FIRMWARE "dvb-fe-tda10045.fw"
#define TDA10046_DEFAULT_FIRMWARE "dvb-fe-tda10046.fw"

#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/jiffies.h>
#include <linux/string.h>
#include <linux/slab.h>

#include "dvb_frontend.h"
#include "tda1004x.h"

enum tda1004x_demod {
	TDA1004X_DEMOD_TDA10045,
	TDA1004X_DEMOD_TDA10046,
};

struct tda1004x_state {
	struct i2c_adapter* i2c;
	struct dvb_frontend_ops ops;
	const struct tda1004x_config* config;
	struct dvb_frontend frontend;

	/* private demod data */
	u8 initialised;
	enum tda1004x_demod demod_type;
};

static int debug;
#define dprintk(args...) \
	do { \
		if (debug) printk(KERN_DEBUG "tda1004x: " args); \
	} while (0)

#define TDA1004X_CHIPID		 0x00
#define TDA1004X_AUTO		 0x01
#define TDA1004X_IN_CONF1	 0x02
#define TDA1004X_IN_CONF2	 0x03
#define TDA1004X_OUT_CONF1	 0x04
#define TDA1004X_OUT_CONF2	 0x05
#define TDA1004X_STATUS_CD	 0x06
#define TDA1004X_CONFC4		 0x07
#define TDA1004X_DSSPARE2	 0x0C
#define TDA10045H_CODE_IN	 0x0D
#define TDA10045H_FWPAGE	 0x0E
#define TDA1004X_SCAN_CPT	 0x10
#define TDA1004X_DSP_CMD	 0x11
#define TDA1004X_DSP_ARG	 0x12
#define TDA1004X_DSP_DATA1	 0x13
#define TDA1004X_DSP_DATA2	 0x14
#define TDA1004X_CONFADC1	 0x15
#define TDA1004X_CONFC1		 0x16
#define TDA10045H_S_AGC		 0x1a
#define TDA10046H_AGC_TUN_LEVEL	 0x1a
#define TDA1004X_SNR		 0x1c
#define TDA1004X_CONF_TS1	 0x1e
#define TDA1004X_CONF_TS2	 0x1f
#define TDA1004X_CBER_RESET	 0x20
#define TDA1004X_CBER_MSB	 0x21
#define TDA1004X_CBER_LSB	 0x22
#define TDA1004X_CVBER_LUT	 0x23
#define TDA1004X_VBER_MSB	 0x24
#define TDA1004X_VBER_MID	 0x25
#define TDA1004X_VBER_LSB	 0x26
#define TDA1004X_UNCOR		 0x27

#define TDA10045H_CONFPLL_P	 0x2D
#define TDA10045H_CONFPLL_M_MSB	 0x2E
#define TDA10045H_CONFPLL_M_LSB	 0x2F
#define TDA10045H_CONFPLL_N	 0x30

#define TDA10046H_CONFPLL1	 0x2D
#define TDA10046H_CONFPLL2	 0x2F
#define TDA10046H_CONFPLL3	 0x30
#define TDA10046H_TIME_WREF1	 0x31
#define TDA10046H_TIME_WREF2	 0x32
#define TDA10046H_TIME_WREF3	 0x33
#define TDA10046H_TIME_WREF4	 0x34
#define TDA10046H_TIME_WREF5	 0x35

#define TDA10045H_UNSURW_MSB	 0x31
#define TDA10045H_UNSURW_LSB	 0x32
#define TDA10045H_WREF_MSB	 0x33
#define TDA10045H_WREF_MID	 0x34
#define TDA10045H_WREF_LSB	 0x35
#define TDA10045H_MUXOUT	 0x36
#define TDA1004X_CONFADC2	 0x37

#define TDA10045H_IOFFSET	 0x38

#define TDA10046H_CONF_TRISTATE1 0x3B
#define TDA10046H_CONF_TRISTATE2 0x3C
#define TDA10046H_CONF_POLARITY	 0x3D
#define TDA10046H_FREQ_OFFSET	 0x3E
#define TDA10046H_GPIO_OUT_SEL	 0x41
#define TDA10046H_GPIO_SELECT	 0x42
#define TDA10046H_AGC_CONF	 0x43
#define TDA10046H_AGC_THR	 0x44
#define TDA10046H_AGC_RENORM	 0x45
#define TDA10046H_AGC_GAINS	 0x46
#define TDA10046H_AGC_TUN_MIN	 0x47
#define TDA10046H_AGC_TUN_MAX	 0x48
#define TDA10046H_AGC_IF_MIN	 0x49
#define TDA10046H_AGC_IF_MAX	 0x4A

#define TDA10046H_FREQ_PHY2_MSB	 0x4D
#define TDA10046H_FREQ_PHY2_LSB	 0x4E

#define TDA10046H_CVBER_CTRL	 0x4F
#define TDA10046H_AGC_IF_LEVEL	 0x52
#define TDA10046H_CODE_CPT	 0x57
#define TDA10046H_CODE_IN	 0x58


static int tda1004x_write_byteI(struct tda1004x_state *state, int reg, int data)
{
	int ret;
	u8 buf[] = { reg, data };
	struct i2c_msg msg = { .flags = 0, .buf = buf, .len = 2 };

	dprintk("%s: reg=0x%x, data=0x%x\n", __FUNCTION__, reg, data);

	msg.addr = state->config->demod_address;
	ret = i2c_transfer(state->i2c, &msg, 1);

	if (ret != 1)
		dprintk("%s: error reg=0x%x, data=0x%x, ret=%i\n",
			__FUNCTION__, reg, data, ret);

	dprintk("%s: success reg=0x%x, data=0x%x, ret=%i\n", __FUNCTION__,
		reg, data, ret);
	return (ret != 1) ? -1 : 0;
}

static int tda1004x_read_byte(struct tda1004x_state *state, int reg)
{
	int ret;
	u8 b0[] = { reg };
	u8 b1[] = { 0 };
	struct i2c_msg msg[] = {{ .flags = 0, .buf = b0, .len = 1 },
				{ .flags = I2C_M_RD, .buf = b1, .len = 1 }};

	dprintk("%s: reg=0x%x\n", __FUNCTION__, reg);

	msg[0].addr = state->config->demod_address;
	msg[1].addr = state->config->demod_address;
	ret = i2c_transfer(state->i2c, msg, 2);

	if (ret != 2) {
		dprintk("%s: error reg=0x%x, ret=%i\n", __FUNCTION__, reg,
			ret);
		return -1;
	}

	dprintk("%s: success reg=0x%x, data=0x%x, ret=%i\n", __FUNCTION__,
		reg, b1[0], ret);
	return b1[0];
}

static int tda1004x_write_mask(struct tda1004x_state *state, int reg, int mask, int data)
{
	int val;
	dprintk("%s: reg=0x%x, mask=0x%x, data=0x%x\n", __FUNCTION__, reg,
		mask, data);

	// read a byte and check
	val = tda1004x_read_byte(state, reg);
	if (val < 0)
		return val;

	// mask if off
	val = val & ~mask;
	val |= data & 0xff;

	// write it out again
	return tda1004x_write_byteI(state, reg, val);
}

static int tda1004x_write_buf(struct tda1004x_state *state, int reg, unsigned char *buf, int len)
{
	int i;
	int result;

	dprintk("%s: reg=0x%x, len=0x%x\n", __FUNCTION__, reg, len);

	result = 0;
	for (i = 0; i < len; i++) {
		result = tda1004x_write_byteI(state, reg + i, buf[i]);
		if (result != 0)
			break;
	}

	return result;
}

static int tda1004x_enable_tuner_i2c(struct tda1004x_state *state)
{
	int result;
	dprintk("%s\n", __FUNCTION__);

	result = tda1004x_write_mask(state, TDA1004X_CONFC4, 2, 2);
	msleep(20);
	return result;
}

static int tda1004x_disable_tuner_i2c(struct tda1004x_state *state)
{
	dprintk("%s\n", __FUNCTION__);

	return tda1004x_write_mask(state, TDA1004X_CONFC4, 2, 0);
}

static int tda10045h_set_bandwidth(struct tda1004x_state *state,
				   fe_bandwidth_t bandwidth)
{
	static u8 bandwidth_6mhz[] = { 0x02, 0x00, 0x3d, 0x00, 0x60, 0x1e, 0xa7, 0x45, 0x4f };
	static u8 bandwidth_7mhz[] = { 0x02, 0x00, 0x37, 0x00, 0x4a, 0x2f, 0x6d, 0x76, 0xdb };
	static u8 bandwidth_8mhz[] = { 0x02, 0x00, 0x3d, 0x00, 0x48, 0x17, 0x89, 0xc7, 0x14 };

	switch (bandwidth) {
	case BANDWIDTH_6_MHZ:
		tda1004x_write_buf(state, TDA10045H_CONFPLL_P, bandwidth_6mhz, sizeof(bandwidth_6mhz));
		break;

	case BANDWIDTH_7_MHZ:
		tda1004x_write_buf(state, TDA10045H_CONFPLL_P, bandwidth_7mhz, sizeof(bandwidth_7mhz));
		break;

	case BANDWIDTH_8_MHZ:
		tda1004x_write_buf(state, TDA10045H_CONFPLL_P, bandwidth_8mhz, sizeof(bandwidth_8mhz));
		break;

	default:
		return -EINVAL;
	}

	tda1004x_write_byteI(state, TDA10045H_IOFFSET, 0);

	return 0;
}

static int tda10046h_set_bandwidth(struct tda1004x_state *state,
				   fe_bandwidth_t bandwidth)
{
	static u8 bandwidth_6mhz_53M[] = { 0x7b, 0x2e, 0x11, 0xf0, 0xd2 };
	static u8 bandwidth_7mhz_53M[] = { 0x6a, 0x02, 0x6a, 0x43, 0x9f };
	static u8 bandwidth_8mhz_53M[] = { 0x5c, 0x32, 0xc2, 0x96, 0x6d };

	static u8 bandwidth_6mhz_48M[] = { 0x70, 0x02, 0x49, 0x24, 0x92 };
	static u8 bandwidth_7mhz_48M[] = { 0x60, 0x02, 0xaa, 0xaa, 0xab };
	static u8 bandwidth_8mhz_48M[] = { 0x54, 0x03, 0x0c, 0x30, 0xc3 };
	int tda10046_clk53m;

	if ((state->config->if_freq == TDA10046_FREQ_045) ||
	    (state->config->if_freq == TDA10046_FREQ_052))
		tda10046_clk53m = 0;
	else
		tda10046_clk53m = 1;
	switch (bandwidth) {
	case BANDWIDTH_6_MHZ:
		if (tda10046_clk53m)
			tda1004x_write_buf(state, TDA10046H_TIME_WREF1, bandwidth_6mhz_53M,
						  sizeof(bandwidth_6mhz_53M));
		else
			tda1004x_write_buf(state, TDA10046H_TIME_WREF1, bandwidth_6mhz_48M,
						  sizeof(bandwidth_6mhz_48M));
		if (state->config->if_freq == TDA10046_FREQ_045) {
			tda1004x_write_byteI(state, TDA10046H_FREQ_PHY2_MSB, 0x0a);
			tda1004x_write_byteI(state, TDA10046H_FREQ_PHY2_LSB, 0xab);
		}
		break;

	case BANDWIDTH_7_MHZ:
		if (tda10046_clk53m)
			tda1004x_write_buf(state, TDA10046H_TIME_WREF1, bandwidth_7mhz_53M,
						  sizeof(bandwidth_7mhz_53M));
		else
			tda1004x_write_buf(state, TDA10046H_TIME_WREF1, bandwidth_7mhz_48M,
						  sizeof(bandwidth_7mhz_48M));
		if (state->config->if_freq == TDA10046_FREQ_045) {
			tda1004x_write_byteI(state, TDA10046H_FREQ_PHY2_MSB, 0x0c);
			tda1004x_write_byteI(state, TDA10046H_FREQ_PHY2_LSB, 0x00);
		}
		break;

	case BANDWIDTH_8_MHZ:
		if (tda10046_clk53m)
			tda1004x_write_buf(state, TDA10046H_TIME_WREF1, bandwidth_8mhz_53M,
						  sizeof(bandwidth_8mhz_53M));
		else
			tda1004x_write_buf(state, TDA10046H_TIME_WREF1, bandwidth_8mhz_48M,
						  sizeof(bandwidth_8mhz_48M));
		if (state->config->if_freq == TDA10046_FREQ_045) {
			tda1004x_write_byteI(state, TDA10046H_FREQ_PHY2_MSB, 0x0d);
			tda1004x_write_byteI(state, TDA10046H_FREQ_PHY2_LSB, 0x55);
		}
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int tda1004x_do_upload(struct tda1004x_state *state,
			      unsigned char *mem, unsigned int len,
			      u8 dspCodeCounterReg, u8 dspCodeInReg)
{
	u8 buf[65];
	struct i2c_msg fw_msg = { .flags = 0, .buf = buf, .len = 0 };
	int tx_size;
	int pos = 0;

	/* clear code counter */
	tda1004x_write_byteI(state, dspCodeCounterReg, 0);
	fw_msg.addr = state->config->demod_address;

	buf[0] = dspCodeInReg;
	while (pos != len) {
		// work out how much to send this time
		tx_size = len - pos;
		if (tx_size > 0x10)
			tx_size = 0x10;

		// send the chunk
		memcpy(buf + 1, mem + pos, tx_size);
		fw_msg.len = tx_size + 1;
		if (i2c_transfer(state->i2c, &fw_msg, 1) != 1) {
			printk(KERN_ERR "tda1004x: Error during firmware upload\n");
			return -EIO;
		}
		pos += tx_size;

		dprintk("%s: fw_pos=0x%x\n", __FUNCTION__, pos);
	}
	// give the DSP a chance to settle 03/10/05 Hac
	msleep(100);

	return 0;
}

static int tda1004x_check_upload_ok(struct tda1004x_state *state)
{
	u8 data1, data2;
	unsigned long timeout;

	if (state->demod_type == TDA1004X_DEMOD_TDA10046) {
		timeout = jiffies + 2 * HZ;
		while(!(tda1004x_read_byte(state, TDA1004X_STATUS_CD) & 0x20)) {
			if (time_after(jiffies, timeout)) {
				printk(KERN_ERR "tda1004x: timeout waiting for DSP ready\n");
				break;
			}
			msleep(1);
		}
	} else
		msleep(100);

	// check upload was OK
	tda1004x_write_mask(state, TDA1004X_CONFC4, 0x10, 0); // we want to read from the DSP
	tda1004x_write_byteI(state, TDA1004X_DSP_CMD, 0x67);

	data1 = tda1004x_read_byte(state, TDA1004X_DSP_DATA1);
	data2 = tda1004x_read_byte(state, TDA1004X_DSP_DATA2);
	if (data1 != 0x67 || data2 < 0x20 || data2 > 0x2e) {
		printk(KERN_INFO "tda1004x: found firmware revision %x -- invalid\n", data2);
		return -EIO;
	}
	printk(KERN_INFO "tda1004x: found firmware revision %x -- ok\n", data2);
	return 0;
}

static int tda10045_fwupload(struct dvb_frontend* fe)
{
	struct tda1004x_state* state = fe->demodulator_priv;
	int ret;
	const struct firmware *fw;

	/* don't re-upload unless necessary */
	if (tda1004x_check_upload_ok(state) == 0)
		return 0;

	/* request the firmware, this will block until someone uploads it */
	printk(KERN_INFO "tda1004x: waiting for firmware upload (%s)...\n", TDA10045_DEFAULT_FIRMWARE);
	ret = state->config->request_firmware(fe, &fw, TDA10045_DEFAULT_FIRMWARE);
	if (ret) {
		printk(KERN_ERR "tda1004x: no firmware upload (timeout or file not found?)\n");
		return ret;
	}

	/* reset chip */
	tda1004x_write_mask(state, TDA1004X_CONFC4, 0x10, 0);
	tda1004x_write_mask(state, TDA1004X_CONFC4, 8, 8);
	tda1004x_write_mask(state, TDA1004X_CONFC4, 8, 0);
	msleep(10);

	/* set parameters */
	tda10045h_set_bandwidth(state, BANDWIDTH_8_MHZ);

	ret = tda1004x_do_upload(state, fw->data, fw->size, TDA10045H_FWPAGE, TDA10045H_CODE_IN);
	release_firmware(fw);
	if (ret)
		return ret;
	printk(KERN_INFO "tda1004x: firmware upload complete\n");

	/* wait for DSP to initialise */
	/* DSPREADY doesn't seem to work on the TDA10045H */
	msleep(100);

	return tda1004x_check_upload_ok(state);
}

static void tda10046_init_plls(struct dvb_frontend* fe)
{
	struct tda1004x_state* state = fe->demodulator_priv;
	int tda10046_clk53m;

	if ((state->config->if_freq == TDA10046_FREQ_045) ||
	    (state->config->if_freq == TDA10046_FREQ_052))
		tda10046_clk53m = 0;
	else
		tda10046_clk53m = 1;

	tda1004x_write_byteI(state, TDA10046H_CONFPLL1, 0xf0);
	if(tda10046_clk53m) {
		printk(KERN_INFO "tda1004x: setting up plls for 53MHz sampling clock\n");
		tda1004x_write_byteI(state, TDA10046H_CONFPLL2, 0x08); // PLL M = 8
	} else {
		printk(KERN_INFO "tda1004x: setting up plls for 48MHz sampling clock\n");
		tda1004x_write_byteI(state, TDA10046H_CONFPLL2, 0x03); // PLL M = 3
	}
	if (state->config->xtal_freq == TDA10046_XTAL_4M ) {
		dprintk("%s: setting up PLLs for a 4 MHz Xtal\n", __FUNCTION__);
		tda1004x_write_byteI(state, TDA10046H_CONFPLL3, 0); // PLL P = N = 0
	} else {
		dprintk("%s: setting up PLLs for a 16 MHz Xtal\n", __FUNCTION__);
		tda1004x_write_byteI(state, TDA10046H_CONFPLL3, 3); // PLL P = 0, N = 3
	}
	if(tda10046_clk53m)
		tda1004x_write_byteI(state, TDA10046H_FREQ_OFFSET, 0x67);
	else
		tda1004x_write_byteI(state, TDA10046H_FREQ_OFFSET, 0x72);
	/* Note clock frequency is handled implicitly */
	switch (state->config->if_freq) {
	case TDA10046_FREQ_045:
		tda1004x_write_byteI(state, TDA10046H_FREQ_PHY2_MSB, 0x0c);
		tda1004x_write_byteI(state, TDA10046H_FREQ_PHY2_LSB, 0x00);
		break;
	case TDA10046_FREQ_052:
		tda1004x_write_byteI(state, TDA10046H_FREQ_PHY2_MSB, 0x0d);
		tda1004x_write_byteI(state, TDA10046H_FREQ_PHY2_LSB, 0xc7);
		break;
	case TDA10046_FREQ_3617:
		tda1004x_write_byteI(state, TDA10046H_FREQ_PHY2_MSB, 0xd7);
		tda1004x_write_byteI(state, TDA10046H_FREQ_PHY2_LSB, 0x59);
		break;
	case TDA10046_FREQ_3613:
		tda1004x_write_byteI(state, TDA10046H_FREQ_PHY2_MSB, 0xd7);
		tda1004x_write_byteI(state, TDA10046H_FREQ_PHY2_LSB, 0x3f);
		break;
	}
	tda10046h_set_bandwidth(state, BANDWIDTH_8_MHZ); // default bandwidth 8 MHz
	/* let the PLLs settle */
	msleep(120);
}

static int tda10046_fwupload(struct dvb_frontend* fe)
{
	struct tda1004x_state* state = fe->demodulator_priv;
	int ret;
	const struct firmware *fw;

	/* reset + wake up chip */
	if (state->config->xtal_freq == TDA10046_XTAL_4M) {
		tda1004x_write_byteI(state, TDA1004X_CONFC4, 0);
	} else {
		dprintk("%s: 16MHz Xtal, reducing I2C speed\n", __FUNCTION__);
		tda1004x_write_byteI(state, TDA1004X_CONFC4, 0x80);
	}
	tda1004x_write_mask(state, TDA10046H_CONF_TRISTATE1, 1, 0);
	/* let the clocks recover from sleep */
	msleep(5);

	/* The PLLs need to be reprogrammed after sleep */
	tda10046_init_plls(fe);

	/* don't re-upload unless necessary */
	if (tda1004x_check_upload_ok(state) == 0)
		return 0;

	if (state->config->request_firmware != NULL) {
		/* request the firmware, this will block until someone uploads it */
		printk(KERN_INFO "tda1004x: waiting for firmware upload...\n");
		ret = state->config->request_firmware(fe, &fw, TDA10046_DEFAULT_FIRMWARE);
		if (ret) {
			printk(KERN_ERR "tda1004x: no firmware upload (timeout or file not found?)\n");
			return ret;
		}
		tda1004x_write_mask(state, TDA1004X_CONFC4, 8, 8); // going to boot from HOST
		ret = tda1004x_do_upload(state, fw->data, fw->size, TDA10046H_CODE_CPT, TDA10046H_CODE_IN);
		release_firmware(fw);
		if (ret)
			return ret;
	} else {
		/* boot from firmware eeprom */
		printk(KERN_INFO "tda1004x: booting from eeprom\n");
		tda1004x_write_mask(state, TDA1004X_CONFC4, 4, 4);
		msleep(300);
	}
	return tda1004x_check_upload_ok(state);
}

static int tda1004x_encode_fec(int fec)
{
	// convert known FEC values
	switch (fec) {
	case FEC_1_2:
		return 0;
	case FEC_2_3:
		return 1;
	case FEC_3_4:
		return 2;
	case FEC_5_6:
		return 3;
	case FEC_7_8:
		return 4;
	}

	// unsupported
	return -EINVAL;
}

static int tda1004x_decode_fec(int tdafec)
{
	// convert known FEC values
	switch (tdafec) {
	case 0:
		return FEC_1_2;
	case 1:
		return FEC_2_3;
	case 2:
		return FEC_3_4;
	case 3:
		return FEC_5_6;
	case 4:
		return FEC_7_8;
	}

	// unsupported
	return -1;
}

int tda1004x_write_byte(struct dvb_frontend* fe, int reg, int data)
{
	struct tda1004x_state* state = fe->demodulator_priv;

	return tda1004x_write_byteI(state, reg, data);
}

static int tda10045_init(struct dvb_frontend* fe)
{
	struct tda1004x_state* state = fe->demodulator_priv;

	dprintk("%s\n", __FUNCTION__);

	if (state->initialised)
		return 0;

	if (tda10045_fwupload(fe)) {
		printk("tda1004x: firmware upload failed\n");
		return -EIO;
	}

	tda1004x_write_mask(state, TDA1004X_CONFADC1, 0x10, 0); // wake up the ADC

	// Init the PLL
	if (state->config->pll_init) {
		tda1004x_enable_tuner_i2c(state);
		state->config->pll_init(fe);
		tda1004x_disable_tuner_i2c(state);
	}

	// tda setup
	tda1004x_write_mask(state, TDA1004X_CONFC4, 0x20, 0); // disable DSP watchdog timer
	tda1004x_write_mask(state, TDA1004X_AUTO, 8, 0); // select HP stream
	tda1004x_write_mask(state, TDA1004X_CONFC1, 0x40, 0); // set polarity of VAGC signal
	tda1004x_write_mask(state, TDA1004X_CONFC1, 0x80, 0x80); // enable pulse killer
	tda1004x_write_mask(state, TDA1004X_AUTO, 0x10, 0x10); // enable auto offset
	tda1004x_write_mask(state, TDA1004X_IN_CONF2, 0xC0, 0x0); // no frequency offset
	tda1004x_write_byteI(state, TDA1004X_CONF_TS1, 0); // setup MPEG2 TS interface
	tda1004x_write_byteI(state, TDA1004X_CONF_TS2, 0); // setup MPEG2 TS interface
	tda1004x_write_mask(state, TDA1004X_VBER_MSB, 0xe0, 0xa0); // 10^6 VBER measurement bits
	tda1004x_write_mask(state, TDA1004X_CONFC1, 0x10, 0); // VAGC polarity
	tda1004x_write_byteI(state, TDA1004X_CONFADC1, 0x2e);

	tda1004x_write_mask(state, 0x1f, 0x01, state->config->invert_oclk);

	state->initialised = 1;
	return 0;
}

static int tda10046_init(struct dvb_frontend* fe)
{
	struct tda1004x_state* state = fe->demodulator_priv;
	dprintk("%s\n", __FUNCTION__);

	if (state->initialised)
		return 0;

	if (tda10046_fwupload(fe)) {
		printk("tda1004x: firmware upload failed\n");
			return -EIO;
	}

	// Init the tuner PLL
	if (state->config->pll_init) {
		tda1004x_enable_tuner_i2c(state);
		if (state->config->pll_init(fe)) {
			printk(KERN_ERR "tda1004x: pll init failed\n");
			return 	-EIO;
		}
		tda1004x_disable_tuner_i2c(state);
	}

	// tda setup
	tda1004x_write_mask(state, TDA1004X_CONFC4, 0x20, 0); // disable DSP watchdog timer
	tda1004x_write_byteI(state, TDA1004X_AUTO, 0x87);    // 100 ppm crystal, select HP stream
	tda1004x_write_byteI(state, TDA1004X_CONFC1, 0x88);      // enable pulse killer

	switch (state->config->agc_config) {
	case TDA10046_AGC_DEFAULT:
		tda1004x_write_byteI(state, TDA10046H_AGC_CONF, 0x00); // AGC setup
		tda1004x_write_byteI(state, TDA10046H_CONF_POLARITY, 0x60); // set AGC polarities
		break;
	case TDA10046_AGC_IFO_AUTO_NEG:
		tda1004x_write_byteI(state, TDA10046H_AGC_CONF, 0x0a); // AGC setup
		tda1004x_write_byteI(state, TDA10046H_CONF_POLARITY, 0x60); // set AGC polarities
		break;
	case TDA10046_AGC_IFO_AUTO_POS:
		tda1004x_write_byteI(state, TDA10046H_AGC_CONF, 0x0a); // AGC setup
		tda1004x_write_byteI(state, TDA10046H_CONF_POLARITY, 0x00); // set AGC polarities
		break;
	case TDA10046_AGC_TDA827X:
		tda1004x_write_byteI(state, TDA10046H_AGC_CONF, 0x02);   // AGC setup
		tda1004x_write_byteI(state, TDA10046H_AGC_THR, 0x70);    // AGC Threshold
		tda1004x_write_byteI(state, TDA10046H_AGC_RENORM, 0x08); // Gain Renormalize
		tda1004x_write_byteI(state, TDA10046H_CONF_POLARITY, 0x6a); // set AGC polarities
		break;
	case TDA10046_AGC_TDA827X_GPL:
		tda1004x_write_byteI(state, TDA10046H_AGC_CONF, 0x02);   // AGC setup
		tda1004x_write_byteI(state, TDA10046H_AGC_THR, 0x70);    // AGC Threshold
		tda1004x_write_byteI(state, TDA10046H_AGC_RENORM, 0x08); // Gain Renormalize
		tda1004x_write_byteI(state, TDA10046H_CONF_POLARITY, 0x60); // set AGC polarities
		break;
	}
	tda1004x_write_byteI(state, TDA1004X_CONFADC2, 0x38);
	tda1004x_write_byteI(state, TDA10046H_CONF_TRISTATE1, 0x61); // Turn both AGC outputs on
	tda1004x_write_byteI(state, TDA10046H_AGC_TUN_MIN, 0);	  // }
	tda1004x_write_byteI(state, TDA10046H_AGC_TUN_MAX, 0xff); // } AGC min/max values
	tda1004x_write_byteI(state, TDA10046H_AGC_IF_MIN, 0);	  // }
	tda1004x_write_byteI(state, TDA10046H_AGC_IF_MAX, 0xff);  // }
	tda1004x_write_byteI(state, TDA10046H_AGC_GAINS, 0x12); // IF gain 2, TUN gain 1
	tda1004x_write_byteI(state, TDA10046H_CVBER_CTRL, 0x1a); // 10^6 VBER measurement bits
	tda1004x_write_byteI(state, TDA1004X_CONF_TS1, 7); // MPEG2 interface config
	tda1004x_write_byteI(state, TDA1004X_CONF_TS2, 0xc0); // MPEG2 interface config
	// tda1004x_write_mask(state, 0x50, 0x80, 0x80);         // handle out of guard echoes
	tda1004x_write_mask(state, 0x3a, 0x80, state->config->invert_oclk << 7);

	state->initialised = 1;
	return 0;
}

static int tda1004x_set_fe(struct dvb_frontend* fe,
			   struct dvb_frontend_parameters *fe_params)
{
	struct tda1004x_state* state = fe->demodulator_priv;
	int tmp;
	int inversion;

	dprintk("%s\n", __FUNCTION__);

	if (state->demod_type == TDA1004X_DEMOD_TDA10046) {
		// setup auto offset
		tda1004x_write_mask(state, TDA1004X_AUTO, 0x10, 0x10);
		tda1004x_write_mask(state, TDA1004X_IN_CONF1, 0x80, 0);
		tda1004x_write_mask(state, TDA1004X_IN_CONF2, 0xC0, 0);

		// disable agc_conf[2]
		tda1004x_write_mask(state, TDA10046H_AGC_CONF, 4, 0);
	}

	// set frequency
	tda1004x_enable_tuner_i2c(state);
	if (state->config->pll_set(fe, fe_params)) {
		printk(KERN_ERR "tda1004x: pll set failed\n");
		return 	-EIO;
	}
	tda1004x_disable_tuner_i2c(state);

	// Hardcoded to use auto as much as possible on the TDA10045 as it
	// is very unreliable if AUTO mode is _not_ used.
	if (state->demod_type == TDA1004X_DEMOD_TDA10045) {
		fe_params->u.ofdm.code_rate_HP = FEC_AUTO;
		fe_params->u.ofdm.guard_interval = GUARD_INTERVAL_AUTO;
		fe_params->u.ofdm.transmission_mode = TRANSMISSION_MODE_AUTO;
	}

	// Set standard params.. or put them to auto
	if ((fe_params->u.ofdm.code_rate_HP == FEC_AUTO) ||
		(fe_params->u.ofdm.code_rate_LP == FEC_AUTO) ||
		(fe_params->u.ofdm.constellation == QAM_AUTO) ||
		(fe_params->u.ofdm.hierarchy_information == HIERARCHY_AUTO)) {
		tda1004x_write_mask(state, TDA1004X_AUTO, 1, 1);	// enable auto
		tda1004x_write_mask(state, TDA1004X_IN_CONF1, 0x03, 0);	// turn off constellation bits
		tda1004x_write_mask(state, TDA1004X_IN_CONF1, 0x60, 0);	// turn off hierarchy bits
		tda1004x_write_mask(state, TDA1004X_IN_CONF2, 0x3f, 0);	// turn off FEC bits
	} else {
		tda1004x_write_mask(state, TDA1004X_AUTO, 1, 0);	// disable auto

		// set HP FEC
		tmp = tda1004x_encode_fec(fe_params->u.ofdm.code_rate_HP);
		if (tmp < 0)
			return tmp;
		tda1004x_write_mask(state, TDA1004X_IN_CONF2, 7, tmp);

		// set LP FEC
		tmp = tda1004x_encode_fec(fe_params->u.ofdm.code_rate_LP);
		if (tmp < 0)
			return tmp;
		tda1004x_write_mask(state, TDA1004X_IN_CONF2, 0x38, tmp << 3);

		// set constellation
		switch (fe_params->u.ofdm.constellation) {
		case QPSK:
			tda1004x_write_mask(state, TDA1004X_IN_CONF1, 3, 0);
			break;

		case QAM_16:
			tda1004x_write_mask(state, TDA1004X_IN_CONF1, 3, 1);
			break;

		case QAM_64:
			tda1004x_write_mask(state, TDA1004X_IN_CONF1, 3, 2);
			break;

		default:
			return -EINVAL;
		}

		// set hierarchy
		switch (fe_params->u.ofdm.hierarchy_information) {
		case HIERARCHY_NONE:
			tda1004x_write_mask(state, TDA1004X_IN_CONF1, 0x60, 0 << 5);
			break;

		case HIERARCHY_1:
			tda1004x_write_mask(state, TDA1004X_IN_CONF1, 0x60, 1 << 5);
			break;

		case HIERARCHY_2:
			tda1004x_write_mask(state, TDA1004X_IN_CONF1, 0x60, 2 << 5);
			break;

		case HIERARCHY_4:
			tda1004x_write_mask(state, TDA1004X_IN_CONF1, 0x60, 3 << 5);
			break;

		default:
			return -EINVAL;
		}
	}

	// set bandwidth
	switch (state->demod_type) {
	case TDA1004X_DEMOD_TDA10045:
		tda10045h_set_bandwidth(state, fe_params->u.ofdm.bandwidth);
		break;

	case TDA1004X_DEMOD_TDA10046:
		tda10046h_set_bandwidth(state, fe_params->u.ofdm.bandwidth);
		break;
	}

	// set inversion
	inversion = fe_params->inversion;
	if (state->config->invert)
		inversion = inversion ? INVERSION_OFF : INVERSION_ON;
	switch (inversion) {
	case INVERSION_OFF:
		tda1004x_write_mask(state, TDA1004X_CONFC1, 0x20, 0);
		break;

	case INVERSION_ON:
		tda1004x_write_mask(state, TDA1004X_CONFC1, 0x20, 0x20);
		break;

	default:
		return -EINVAL;
	}

	// set guard interval
	switch (fe_params->u.ofdm.guard_interval) {
	case GUARD_INTERVAL_1_32:
		tda1004x_write_mask(state, TDA1004X_AUTO, 2, 0);
		tda1004x_write_mask(state, TDA1004X_IN_CONF1, 0x0c, 0 << 2);
		break;

	case GUARD_INTERVAL_1_16:
		tda1004x_write_mask(state, TDA1004X_AUTO, 2, 0);
		tda1004x_write_mask(state, TDA1004X_IN_CONF1, 0x0c, 1 << 2);
		break;

	case GUARD_INTERVAL_1_8:
		tda1004x_write_mask(state, TDA1004X_AUTO, 2, 0);
		tda1004x_write_mask(state, TDA1004X_IN_CONF1, 0x0c, 2 << 2);
		break;

	case GUARD_INTERVAL_1_4:
		tda1004x_write_mask(state, TDA1004X_AUTO, 2, 0);
		tda1004x_write_mask(state, TDA1004X_IN_CONF1, 0x0c, 3 << 2);
		break;

	case GUARD_INTERVAL_AUTO:
		tda1004x_write_mask(state, TDA1004X_AUTO, 2, 2);
		tda1004x_write_mask(state, TDA1004X_IN_CONF1, 0x0c, 0 << 2);
		break;

	default:
		return -EINVAL;
	}

	// set transmission mode
	switch (fe_params->u.ofdm.transmission_mode) {
	case TRANSMISSION_MODE_2K:
		tda1004x_write_mask(state, TDA1004X_AUTO, 4, 0);
		tda1004x_write_mask(state, TDA1004X_IN_CONF1, 0x10, 0 << 4);
		break;

	case TRANSMISSION_MODE_8K:
		tda1004x_write_mask(state, TDA1004X_AUTO, 4, 0);
		tda1004x_write_mask(state, TDA1004X_IN_CONF1, 0x10, 1 << 4);
		break;

	case TRANSMISSION_MODE_AUTO:
		tda1004x_write_mask(state, TDA1004X_AUTO, 4, 4);
		tda1004x_write_mask(state, TDA1004X_IN_CONF1, 0x10, 0);
		break;

	default:
		return -EINVAL;
	}

	// start the lock
	switch (state->demod_type) {
	case TDA1004X_DEMOD_TDA10045:
		tda1004x_write_mask(state, TDA1004X_CONFC4, 8, 8);
		tda1004x_write_mask(state, TDA1004X_CONFC4, 8, 0);
		break;

	case TDA1004X_DEMOD_TDA10046:
		tda1004x_write_mask(state, TDA1004X_AUTO, 0x40, 0x40);
		msleep(1);
		tda1004x_write_mask(state, TDA10046H_AGC_CONF, 4, 1);
		break;
	}

	msleep(10);

	return 0;
}

static int tda1004x_get_fe(struct dvb_frontend* fe, struct dvb_frontend_parameters *fe_params)
{
	struct tda1004x_state* state = fe->demodulator_priv;

	dprintk("%s\n", __FUNCTION__);

	// inversion status
	fe_params->inversion = INVERSION_OFF;
	if (tda1004x_read_byte(state, TDA1004X_CONFC1) & 0x20)
		fe_params->inversion = INVERSION_ON;
	if (state->config->invert)
		fe_params->inversion = fe_params->inversion ? INVERSION_OFF : INVERSION_ON;

	// bandwidth
	switch (state->demod_type) {
	case TDA1004X_DEMOD_TDA10045:
		switch (tda1004x_read_byte(state, TDA10045H_WREF_LSB)) {
		case 0x14:
			fe_params->u.ofdm.bandwidth = BANDWIDTH_8_MHZ;
			break;
		case 0xdb:
			fe_params->u.ofdm.bandwidth = BANDWIDTH_7_MHZ;
			break;
		case 0x4f:
			fe_params->u.ofdm.bandwidth = BANDWIDTH_6_MHZ;
			break;
		}
		break;
	case TDA1004X_DEMOD_TDA10046:
		switch (tda1004x_read_byte(state, TDA10046H_TIME_WREF1)) {
		case 0x5c:
		case 0x54:
			fe_params->u.ofdm.bandwidth = BANDWIDTH_8_MHZ;
			break;
		case 0x6a:
		case 0x60:
			fe_params->u.ofdm.bandwidth = BANDWIDTH_7_MHZ;
			break;
		case 0x7b:
		case 0x70:
			fe_params->u.ofdm.bandwidth = BANDWIDTH_6_MHZ;
			break;
		}
		break;
	}

	// FEC
	fe_params->u.ofdm.code_rate_HP =
	    tda1004x_decode_fec(tda1004x_read_byte(state, TDA1004X_OUT_CONF2) & 7);
	fe_params->u.ofdm.code_rate_LP =
	    tda1004x_decode_fec((tda1004x_read_byte(state, TDA1004X_OUT_CONF2) >> 3) & 7);

	// constellation
	switch (tda1004x_read_byte(state, TDA1004X_OUT_CONF1) & 3) {
	case 0:
		fe_params->u.ofdm.constellation = QPSK;
		break;
	case 1:
		fe_params->u.ofdm.constellation = QAM_16;
		break;
	case 2:
		fe_params->u.ofdm.constellation = QAM_64;
		break;
	}

	// transmission mode
	fe_params->u.ofdm.transmission_mode = TRANSMISSION_MODE_2K;
	if (tda1004x_read_byte(state, TDA1004X_OUT_CONF1) & 0x10)
		fe_params->u.ofdm.transmission_mode = TRANSMISSION_MODE_8K;

	// guard interval
	switch ((tda1004x_read_byte(state, TDA1004X_OUT_CONF1) & 0x0c) >> 2) {
	case 0:
		fe_params->u.ofdm.guard_interval = GUARD_INTERVAL_1_32;
		break;
	case 1:
		fe_params->u.ofdm.guard_interval = GUARD_INTERVAL_1_16;
		break;
	case 2:
		fe_params->u.ofdm.guard_interval = GUARD_INTERVAL_1_8;
		break;
	case 3:
		fe_params->u.ofdm.guard_interval = GUARD_INTERVAL_1_4;
		break;
	}

	// hierarchy
	switch ((tda1004x_read_byte(state, TDA1004X_OUT_CONF1) & 0x60) >> 5) {
	case 0:
		fe_params->u.ofdm.hierarchy_information = HIERARCHY_NONE;
		break;
	case 1:
		fe_params->u.ofdm.hierarchy_information = HIERARCHY_1;
		break;
	case 2:
		fe_params->u.ofdm.hierarchy_information = HIERARCHY_2;
		break;
	case 3:
		fe_params->u.ofdm.hierarchy_information = HIERARCHY_4;
		break;
	}

	return 0;
}

static int tda1004x_read_status(struct dvb_frontend* fe, fe_status_t * fe_status)
{
	struct tda1004x_state* state = fe->demodulator_priv;
	int status;
	int cber;
	int vber;

	dprintk("%s\n", __FUNCTION__);

	// read status
	status = tda1004x_read_byte(state, TDA1004X_STATUS_CD);
	if (status == -1)
		return -EIO;

	// decode
	*fe_status = 0;
	if (status & 4)
		*fe_status |= FE_HAS_SIGNAL;
	if (status & 2)
		*fe_status |= FE_HAS_CARRIER;
	if (status & 8)
		*fe_status |= FE_HAS_VITERBI | FE_HAS_SYNC | FE_HAS_LOCK;

	// if we don't already have VITERBI (i.e. not LOCKED), see if the viterbi
	// is getting anything valid
	if (!(*fe_status & FE_HAS_VITERBI)) {
		// read the CBER
		cber = tda1004x_read_byte(state, TDA1004X_CBER_LSB);
		if (cber == -1)
			return -EIO;
		status = tda1004x_read_byte(state, TDA1004X_CBER_MSB);
		if (status == -1)
			return -EIO;
		cber |= (status << 8);
		// The address 0x20 should be read to cope with a TDA10046 bug
		tda1004x_read_byte(state, TDA1004X_CBER_RESET);

		if (cber != 65535)
			*fe_status |= FE_HAS_VITERBI;
	}

	// if we DO have some valid VITERBI output, but don't already have SYNC
	// bytes (i.e. not LOCKED), see if the RS decoder is getting anything valid.
	if ((*fe_status & FE_HAS_VITERBI) && (!(*fe_status & FE_HAS_SYNC))) {
		// read the VBER
		vber = tda1004x_read_byte(state, TDA1004X_VBER_LSB);
		if (vber == -1)
			return -EIO;
		status = tda1004x_read_byte(state, TDA1004X_VBER_MID);
		if (status == -1)
			return -EIO;
		vber |= (status << 8);
		status = tda1004x_read_byte(state, TDA1004X_VBER_MSB);
		if (status == -1)
			return -EIO;
		vber |= (status & 0x0f) << 16;
		// The CVBER_LUT should be read to cope with TDA10046 hardware bug
		tda1004x_read_byte(state, TDA1004X_CVBER_LUT);

		// if RS has passed some valid TS packets, then we must be
		// getting some SYNC bytes
		if (vber < 16632)
			*fe_status |= FE_HAS_SYNC;
	}

	// success
	dprintk("%s: fe_status=0x%x\n", __FUNCTION__, *fe_status);
	return 0;
}

static int tda1004x_read_signal_strength(struct dvb_frontend* fe, u16 * signal)
{
	struct tda1004x_state* state = fe->demodulator_priv;
	int tmp;
	int reg = 0;

	dprintk("%s\n", __FUNCTION__);

	// determine the register to use
	switch (state->demod_type) {
	case TDA1004X_DEMOD_TDA10045:
		reg = TDA10045H_S_AGC;
		break;

	case TDA1004X_DEMOD_TDA10046:
		reg = TDA10046H_AGC_IF_LEVEL;
		break;
	}

	// read it
	tmp = tda1004x_read_byte(state, reg);
	if (tmp < 0)
		return -EIO;

	*signal = (tmp << 8) | tmp;
	dprintk("%s: signal=0x%x\n", __FUNCTION__, *signal);
	return 0;
}

static int tda1004x_read_snr(struct dvb_frontend* fe, u16 * snr)
{
	struct tda1004x_state* state = fe->demodulator_priv;
	int tmp;

	dprintk("%s\n", __FUNCTION__);

	// read it
	tmp = tda1004x_read_byte(state, TDA1004X_SNR);
	if (tmp < 0)
		return -EIO;
	tmp = 255 - tmp;

	*snr = ((tmp << 8) | tmp);
	dprintk("%s: snr=0x%x\n", __FUNCTION__, *snr);
	return 0;
}

static int tda1004x_read_ucblocks(struct dvb_frontend* fe, u32* ucblocks)
{
	struct tda1004x_state* state = fe->demodulator_priv;
	int tmp;
	int tmp2;
	int counter;

	dprintk("%s\n", __FUNCTION__);

	// read the UCBLOCKS and reset
	counter = 0;
	tmp = tda1004x_read_byte(state, TDA1004X_UNCOR);
	if (tmp < 0)
		return -EIO;
	tmp &= 0x7f;
	while (counter++ < 5) {
		tda1004x_write_mask(state, TDA1004X_UNCOR, 0x80, 0);
		tda1004x_write_mask(state, TDA1004X_UNCOR, 0x80, 0);
		tda1004x_write_mask(state, TDA1004X_UNCOR, 0x80, 0);

		tmp2 = tda1004x_read_byte(state, TDA1004X_UNCOR);
		if (tmp2 < 0)
			return -EIO;
		tmp2 &= 0x7f;
		if ((tmp2 < tmp) || (tmp2 == 0))
			break;
	}

	if (tmp != 0x7f)
		*ucblocks = tmp;
	else
		*ucblocks = 0xffffffff;

	dprintk("%s: ucblocks=0x%x\n", __FUNCTION__, *ucblocks);
	return 0;
}

static int tda1004x_read_ber(struct dvb_frontend* fe, u32* ber)
{
	struct tda1004x_state* state = fe->demodulator_priv;
	int tmp;

	dprintk("%s\n", __FUNCTION__);

	// read it in
	tmp = tda1004x_read_byte(state, TDA1004X_CBER_LSB);
	if (tmp < 0)
		return -EIO;
	*ber = tmp << 1;
	tmp = tda1004x_read_byte(state, TDA1004X_CBER_MSB);
	if (tmp < 0)
		return -EIO;
	*ber |= (tmp << 9);
	// The address 0x20 should be read to cope with a TDA10046 bug
	tda1004x_read_byte(state, TDA1004X_CBER_RESET);

	dprintk("%s: ber=0x%x\n", __FUNCTION__, *ber);
	return 0;
}

static int tda1004x_sleep(struct dvb_frontend* fe)
{
	struct tda1004x_state* state = fe->demodulator_priv;

	switch (state->demod_type) {
	case TDA1004X_DEMOD_TDA10045:
		tda1004x_write_mask(state, TDA1004X_CONFADC1, 0x10, 0x10);
		break;

	case TDA1004X_DEMOD_TDA10046:
		if (state->config->pll_sleep != NULL) {
			tda1004x_enable_tuner_i2c(state);
			state->config->pll_sleep(fe);
			if (state->config->if_freq != TDA10046_FREQ_052) {
				/* special hack for Philips EUROPA Based boards:
				 * keep the I2c bridge open for tuner access in analog mode
				 */
				tda1004x_disable_tuner_i2c(state);
			}
		}
		/* set outputs to tristate */
		tda1004x_write_byteI(state, TDA10046H_CONF_TRISTATE1, 0xff);
		tda1004x_write_mask(state, TDA1004X_CONFC4, 1, 1);
		break;
	}
	state->initialised = 0;

	return 0;
}

static int tda1004x_get_tune_settings(struct dvb_frontend* fe, struct dvb_frontend_tune_settings* fesettings)
{
	fesettings->min_delay_ms = 800;
	/* Drift compensation makes no sense for DVB-T */
	fesettings->step_size = 0;
	fesettings->max_drift = 0;
	return 0;
}

static void tda1004x_release(struct dvb_frontend* fe)
{
	struct tda1004x_state *state = fe->demodulator_priv;
	kfree(state);
}

static struct dvb_frontend_ops tda10045_ops = {
	.info = {
		.name = "Philips TDA10045H DVB-T",
		.type = FE_OFDM,
		.frequency_min = 51000000,
		.frequency_max = 858000000,
		.frequency_stepsize = 166667,
		.caps =
		    FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
		    FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
		    FE_CAN_QPSK | FE_CAN_QAM_16 | FE_CAN_QAM_64 | FE_CAN_QAM_AUTO |
		    FE_CAN_TRANSMISSION_MODE_AUTO | FE_CAN_GUARD_INTERVAL_AUTO
	},

	.release = tda1004x_release,

	.init = tda10045_init,
	.sleep = tda1004x_sleep,

	.set_frontend = tda1004x_set_fe,
	.get_frontend = tda1004x_get_fe,
	.get_tune_settings = tda1004x_get_tune_settings,

	.read_status = tda1004x_read_status,
	.read_ber = tda1004x_read_ber,
	.read_signal_strength = tda1004x_read_signal_strength,
	.read_snr = tda1004x_read_snr,
	.read_ucblocks = tda1004x_read_ucblocks,
};

struct dvb_frontend* tda10045_attach(const struct tda1004x_config* config,
				     struct i2c_adapter* i2c)
{
	struct tda1004x_state *state;

	/* allocate memory for the internal state */
	state = kmalloc(sizeof(struct tda1004x_state), GFP_KERNEL);
	if (!state)
		return NULL;

	/* setup the state */
	state->config = config;
	state->i2c = i2c;
	memcpy(&state->ops, &tda10045_ops, sizeof(struct dvb_frontend_ops));
	state->initialised = 0;
	state->demod_type = TDA1004X_DEMOD_TDA10045;

	/* check if the demod is there */
	if (tda1004x_read_byte(state, TDA1004X_CHIPID) != 0x25) {
		kfree(state);
		return NULL;
	}

	/* create dvb_frontend */
	state->frontend.ops = &state->ops;
	state->frontend.demodulator_priv = state;
	return &state->frontend;
}

static struct dvb_frontend_ops tda10046_ops = {
	.info = {
		.name = "Philips TDA10046H DVB-T",
		.type = FE_OFDM,
		.frequency_min = 51000000,
		.frequency_max = 858000000,
		.frequency_stepsize = 166667,
		.caps =
		    FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
		    FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
		    FE_CAN_QPSK | FE_CAN_QAM_16 | FE_CAN_QAM_64 | FE_CAN_QAM_AUTO |
		    FE_CAN_TRANSMISSION_MODE_AUTO | FE_CAN_GUARD_INTERVAL_AUTO
	},

	.release = tda1004x_release,

	.init = tda10046_init,
	.sleep = tda1004x_sleep,

	.set_frontend = tda1004x_set_fe,
	.get_frontend = tda1004x_get_fe,
	.get_tune_settings = tda1004x_get_tune_settings,

	.read_status = tda1004x_read_status,
	.read_ber = tda1004x_read_ber,
	.read_signal_strength = tda1004x_read_signal_strength,
	.read_snr = tda1004x_read_snr,
	.read_ucblocks = tda1004x_read_ucblocks,
};

struct dvb_frontend* tda10046_attach(const struct tda1004x_config* config,
				     struct i2c_adapter* i2c)
{
	struct tda1004x_state *state;

	/* allocate memory for the internal state */
	state = kmalloc(sizeof(struct tda1004x_state), GFP_KERNEL);
	if (!state)
		return NULL;

	/* setup the state */
	state->config = config;
	state->i2c = i2c;
	memcpy(&state->ops, &tda10046_ops, sizeof(struct dvb_frontend_ops));
	state->initialised = 0;
	state->demod_type = TDA1004X_DEMOD_TDA10046;

	/* check if the demod is there */
	if (tda1004x_read_byte(state, TDA1004X_CHIPID) != 0x46) {
		kfree(state);
		return NULL;
	}

	/* create dvb_frontend */
	state->frontend.ops = &state->ops;
	state->frontend.demodulator_priv = state;
	return &state->frontend;
}

module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off frontend debugging (default:off).");

MODULE_DESCRIPTION("Philips TDA10045H & TDA10046H DVB-T Demodulator");
MODULE_AUTHOR("Andrew de Quincey & Robert Schlabbach");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL(tda10045_attach);
EXPORT_SYMBOL(tda10046_attach);
EXPORT_SYMBOL(tda1004x_write_byte);
