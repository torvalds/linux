/*
 * bsru6.h - ALPS BSRU6 tuner support (moved from budget-ci.c)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 * Or, point your browser to http://www.gnu.org/copyleft/gpl.html
 *
 *
 * the project's page is at http://www.linuxtv.org
 */

#ifndef BSRU6_H
#define BSRU6_H

static u8 alps_bsru6_inittab[] = {
	0x01, 0x15,
	0x02, 0x00,
	0x03, 0x00,
	0x04, 0x7d,   /* F22FR = 0x7d, F22 = f_VCO / 128 / 0x7d = 22 kHz */
	0x05, 0x35,   /* I2CT = 0, SCLT = 1, SDAT = 1 */
	0x06, 0x40,   /* DAC not used, set to high impendance mode */
	0x07, 0x00,   /* DAC LSB */
	0x08, 0x40,   /* DiSEqC off, LNB power on OP2/LOCK pin on */
	0x09, 0x00,   /* FIFO */
	0x0c, 0x51,   /* OP1 ctl = Normal, OP1 val = 1 (LNB Power ON) */
	0x0d, 0x82,   /* DC offset compensation = ON, beta_agc1 = 2 */
	0x0e, 0x23,   /* alpha_tmg = 2, beta_tmg = 3 */
	0x10, 0x3f,   // AGC2  0x3d
	0x11, 0x84,
	0x12, 0xb9,
	0x15, 0xc9,   // lock detector threshold
	0x16, 0x00,
	0x17, 0x00,
	0x18, 0x00,
	0x19, 0x00,
	0x1a, 0x00,
	0x1f, 0x50,
	0x20, 0x00,
	0x21, 0x00,
	0x22, 0x00,
	0x23, 0x00,
	0x28, 0x00,  // out imp: normal  out type: parallel FEC mode:0
	0x29, 0x1e,  // 1/2 threshold
	0x2a, 0x14,  // 2/3 threshold
	0x2b, 0x0f,  // 3/4 threshold
	0x2c, 0x09,  // 5/6 threshold
	0x2d, 0x05,  // 7/8 threshold
	0x2e, 0x01,
	0x31, 0x1f,  // test all FECs
	0x32, 0x19,  // viterbi and synchro search
	0x33, 0xfc,  // rs control
	0x34, 0x93,  // error control
	0x0f, 0x52,
	0xff, 0xff
};

static int alps_bsru6_set_symbol_rate(struct dvb_frontend *fe, u32 srate, u32 ratio)
{
	u8 aclk = 0;
	u8 bclk = 0;

	if (srate < 1500000) {
		aclk = 0xb7;
		bclk = 0x47;
	} else if (srate < 3000000) {
		aclk = 0xb7;
		bclk = 0x4b;
	} else if (srate < 7000000) {
		aclk = 0xb7;
		bclk = 0x4f;
	} else if (srate < 14000000) {
		aclk = 0xb7;
		bclk = 0x53;
	} else if (srate < 30000000) {
		aclk = 0xb6;
		bclk = 0x53;
	} else if (srate < 45000000) {
		aclk = 0xb4;
		bclk = 0x51;
	}

	stv0299_writereg(fe, 0x13, aclk);
	stv0299_writereg(fe, 0x14, bclk);
	stv0299_writereg(fe, 0x1f, (ratio >> 16) & 0xff);
	stv0299_writereg(fe, 0x20, (ratio >> 8) & 0xff);
	stv0299_writereg(fe, 0x21, ratio & 0xf0);

	return 0;
}

static int alps_bsru6_tuner_set_params(struct dvb_frontend *fe, struct dvb_frontend_parameters *params)
{
	u8 buf[4];
	u32 div;
	struct i2c_msg msg = { .addr = 0x61, .flags = 0, .buf = buf, .len = sizeof(buf) };
	struct i2c_adapter *i2c = fe->tuner_priv;

	if ((params->frequency < 950000) || (params->frequency > 2150000))
		return -EINVAL;

	div = (params->frequency + (125 - 1)) / 125;	// round correctly
	buf[0] = (div >> 8) & 0x7f;
	buf[1] = div & 0xff;
	buf[2] = 0x80 | ((div & 0x18000) >> 10) | 4;
	buf[3] = 0xC4;

	if (params->frequency > 1530000)
		buf[3] = 0xc0;

	if (i2c_transfer(i2c, &msg, 1) != 1)
		return -EIO;
	return 0;
}

static struct stv0299_config alps_bsru6_config = {
	.demod_address = 0x68,
	.inittab = alps_bsru6_inittab,
	.mclk = 88000000UL,
	.invert = 1,
	.skip_reinit = 0,
	.lock_output = STV0229_LOCKOUTPUT_1,
	.volt13_op0_op1 = STV0299_VOLT13_OP1,
	.min_delay_ms = 100,
	.set_symbol_rate = alps_bsru6_set_symbol_rate,
};

#endif
