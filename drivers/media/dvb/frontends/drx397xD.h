/*
 *  Driver for Micronas DVB-T drx397xD demodulator
 *
 *  Copyright (C) 2007 Henk vergonet <Henk.Vergonet@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.=
 */

#ifndef _DRX397XD_H_INCLUDED
#define _DRX397XD_H_INCLUDED

#include <linux/dvb/frontend.h>

#define DRX_F_STEPSIZE	166667
#define DRX_F_OFFSET	36000000

#define I2C_ADR_C0(x) \
(	cpu_to_le32( \
		(u32)( \
			(((u32)(x) & (u32)0x000000ffUL)      ) | \
			(((u32)(x) & (u32)0x0000ff00UL) << 16) | \
			(((u32)(x) & (u32)0x0fff0000UL) >>  8) | \
			 (	     (u32)0x00c00000UL)          \
		      )) \
)

#define I2C_ADR_E0(x) \
(	cpu_to_le32( \
		(u32)( \
			(((u32)(x) & (u32)0x000000ffUL)      ) | \
			(((u32)(x) & (u32)0x0000ff00UL) << 16) | \
			(((u32)(x) & (u32)0x0fff0000UL) >>  8) | \
			 (	     (u32)0x00e00000UL)          \
		      )) \
)

struct drx397xD_CfgRfAgc	/* 0x7c */
{
	int d00;	/* 2 */
	u16 w04;
	u16 w06;
};

struct drx397xD_CfgIfAgc	/* 0x68 */
{
	int d00;	/* 0 */
	u16 w04;	/* 0 */
	u16 w06;
	u16 w08;
	u16 w0A;
	u16 w0C;
};

struct drx397xD_s20 {
	int d04;
	u32 d18;
	u32 d1C;
	u32 d20;
	u32 d14;
	u32 d24;
	u32 d0C;
	u32 d08;
};

struct drx397xD_config
{
	/* demodulator's I2C address */
	u8	demod_address;		/* 0x0f */

	struct drx397xD_CfgIfAgc  ifagc;  /* 0x68 */
	struct drx397xD_CfgRfAgc  rfagc;  /* 0x7c */
	u32	s20d24;

	/* HI_CfgCommand parameters */
	u16	w50, w52, /* w54, */ w56;

	int	d5C;
	int	d60;
	int	d48;
	int	d28;

	u32	f_if;	/* d14: intermediate frequency [Hz]		*/
			/*	36000000 on Cinergy 2400i DT		*/
			/*	42800000 on Pinnacle Hybrid PRO 330e	*/

	u16	f_osc;	/* s66: 48000 oscillator frequency [kHz]	*/

	u16	w92;	/* 20000 */

	u16	wA0;
	u16	w98;
	u16	w9A;

	u16	w9C;	/* 0xe0 */
	u16	w9E;	/* 0x00 */

	/* used for signal strength calculations in
	   drx397x_read_signal_strength
	*/
	u16	ss78;	// 2200
	u16	ss7A;	// 150
	u16	ss76;	// 820
};

#if defined(CONFIG_DVB_DRX397XD) || (defined(CONFIG_DVB_DRX397XD_MODULE) && defined(MODULE))
extern struct dvb_frontend* drx397xD_attach(const struct drx397xD_config *config,
					   struct i2c_adapter *i2c);
#else
static inline struct dvb_frontend* drx397xD_attach(const struct drx397xD_config *config,
					   struct i2c_adapter *i2c)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif /* CONFIG_DVB_DRX397XD */

#endif /* _DRX397XD_H_INCLUDED */
