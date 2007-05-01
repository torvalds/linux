  /*
     Driver for Philips tda1004xh OFDM Frontend

     (c) 2004 Andrew de Quincey

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

#ifndef TDA1004X_H
#define TDA1004X_H

#include <linux/dvb/frontend.h>
#include <linux/firmware.h>

enum tda10046_xtal {
	TDA10046_XTAL_4M,
	TDA10046_XTAL_16M,
};

enum tda10046_agc {
	TDA10046_AGC_DEFAULT,		/* original configuration */
	TDA10046_AGC_IFO_AUTO_NEG,	/* IF AGC only, automatic, negtive */
	TDA10046_AGC_IFO_AUTO_POS,	/* IF AGC only, automatic, positive */
	TDA10046_AGC_TDA827X,		/* IF AGC only, special setup for tda827x */
};

/* Many (hybrid) boards use GPIO 1 and 3
	GPIO1	analog - dvb switch
	GPIO3	firmware eeprom address switch
*/
enum tda10046_gpio {
	TDA10046_GPTRI  = 0x00,		/* All GPIOs tristate */
	TDA10046_GP00   = 0x40,		/* GPIO3=0, GPIO1=0 */
	TDA10046_GP01   = 0x42,		/* GPIO3=0, GPIO1=1 */
	TDA10046_GP10   = 0x48,		/* GPIO3=1, GPIO1=0 */
	TDA10046_GP11   = 0x4a,		/* GPIO3=1, GPIO1=1 */
	TDA10046_GP00_I = 0x80,		/* GPIO3=0, GPIO1=0, invert in sleep mode*/
	TDA10046_GP01_I = 0x82,		/* GPIO3=0, GPIO1=1, invert in sleep mode */
	TDA10046_GP10_I = 0x88,		/* GPIO3=1, GPIO1=0, invert in sleep mode */
	TDA10046_GP11_I = 0x8a,		/* GPIO3=1, GPIO1=1, invert in sleep mode */
};

enum tda10046_if {
	TDA10046_FREQ_3617,		/* original config, 36,166 MHZ */
	TDA10046_FREQ_3613,		/* 36,13 MHZ */
	TDA10046_FREQ_045,		/* low IF, 4.0, 4.5, or 5.0 MHZ */
	TDA10046_FREQ_052,		/* low IF, 5.1667 MHZ for tda9889 */
};

enum tda10046_tsout {
	TDA10046_TS_PARALLEL  = 0x00,	/* parallel transport stream, default */
	TDA10046_TS_SERIAL    = 0x01,	/* serial transport stream */
};

struct tda1004x_config
{
	/* the demodulator's i2c address */
	u8 demod_address;

	/* does the "inversion" need inverted? */
	u8 invert;

	/* Does the OCLK signal need inverted? */
	u8 invert_oclk;

	/* parallel or serial transport stream */
	enum tda10046_tsout ts_mode;

	/* Xtal frequency, 4 or 16MHz*/
	enum tda10046_xtal xtal_freq;

	/* IF frequency */
	enum tda10046_if if_freq;

	/* AGC configuration */
	enum tda10046_agc agc_config;

	/* setting of GPIO1 and 3 */
	enum tda10046_gpio gpio_config;

	/* slave address and configuration of the tuner */
	u8 tuner_address;
	u8 tuner_config;
	u8 antenna_switch;

	/* if the board uses another I2c Bridge (tda8290), its address */
	u8 i2c_gate;

	/* request firmware for device */
	int (*request_firmware)(struct dvb_frontend* fe, const struct firmware **fw, char* name);
};

enum tda1004x_demod {
	TDA1004X_DEMOD_TDA10045,
	TDA1004X_DEMOD_TDA10046,
};

struct tda1004x_state {
	struct i2c_adapter* i2c;
	const struct tda1004x_config* config;
	struct dvb_frontend frontend;

	/* private demod data */
	enum tda1004x_demod demod_type;
};

#if defined(CONFIG_DVB_TDA1004X) || (defined(CONFIG_DVB_TDA1004X_MODULE) && defined(MODULE))
extern struct dvb_frontend* tda10045_attach(const struct tda1004x_config* config,
					    struct i2c_adapter* i2c);

extern struct dvb_frontend* tda10046_attach(const struct tda1004x_config* config,
					    struct i2c_adapter* i2c);
#else
static inline struct dvb_frontend* tda10045_attach(const struct tda1004x_config* config,
					    struct i2c_adapter* i2c)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __FUNCTION__);
	return NULL;
}
static inline struct dvb_frontend* tda10046_attach(const struct tda1004x_config* config,
					    struct i2c_adapter* i2c)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __FUNCTION__);
	return NULL;
}
#endif // CONFIG_DVB_TDA1004X

static inline int tda1004x_writereg(struct dvb_frontend *fe, u8 reg, u8 val) {
	int r = 0;
	u8 buf[] = {reg, val};
	if (fe->ops.write)
		r = fe->ops.write(fe, buf, 2);
	return r;
}

#endif // TDA1004X_H
