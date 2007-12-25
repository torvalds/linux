/*
    tda18271-priv.h - private header for the NXP TDA18271 silicon tuner

    Copyright (C) 2007 Michael Krufky (mkrufky@linuxtv.org)

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

#ifndef __TDA18271_PRIV_H__
#define __TDA18271_PRIV_H__

#include <linux/kernel.h>
#include <linux/types.h>
#include "tda18271.h"

#define R_ID     0x00	/* ID byte                */
#define R_TM     0x01	/* Thermo byte            */
#define R_PL     0x02	/* Power level byte       */
#define R_EP1    0x03	/* Easy Prog byte 1       */
#define R_EP2    0x04	/* Easy Prog byte 2       */
#define R_EP3    0x05	/* Easy Prog byte 3       */
#define R_EP4    0x06	/* Easy Prog byte 4       */
#define R_EP5    0x07	/* Easy Prog byte 5       */
#define R_CPD    0x08	/* Cal Post-Divider byte  */
#define R_CD1    0x09	/* Cal Divider byte 1     */
#define R_CD2    0x0a	/* Cal Divider byte 2     */
#define R_CD3    0x0b	/* Cal Divider byte 3     */
#define R_MPD    0x0c	/* Main Post-Divider byte */
#define R_MD1    0x0d	/* Main Divider byte 1    */
#define R_MD2    0x0e	/* Main Divider byte 2    */
#define R_MD3    0x0f	/* Main Divider byte 3    */
#define R_EB1    0x10	/* Extended byte 1        */
#define R_EB2    0x11	/* Extended byte 2        */
#define R_EB3    0x12	/* Extended byte 3        */
#define R_EB4    0x13	/* Extended byte 4        */
#define R_EB5    0x14	/* Extended byte 5        */
#define R_EB6    0x15	/* Extended byte 6        */
#define R_EB7    0x16	/* Extended byte 7        */
#define R_EB8    0x17	/* Extended byte 8        */
#define R_EB9    0x18	/* Extended byte 9        */
#define R_EB10   0x19	/* Extended byte 10       */
#define R_EB11   0x1a	/* Extended byte 11       */
#define R_EB12   0x1b	/* Extended byte 12       */
#define R_EB13   0x1c	/* Extended byte 13       */
#define R_EB14   0x1d	/* Extended byte 14       */
#define R_EB15   0x1e	/* Extended byte 15       */
#define R_EB16   0x1f	/* Extended byte 16       */
#define R_EB17   0x20	/* Extended byte 17       */
#define R_EB18   0x21	/* Extended byte 18       */
#define R_EB19   0x22	/* Extended byte 19       */
#define R_EB20   0x23	/* Extended byte 20       */
#define R_EB21   0x24	/* Extended byte 21       */
#define R_EB22   0x25	/* Extended byte 22       */
#define R_EB23   0x26	/* Extended byte 23       */

#define TDA18271_NUM_REGS 39

/*---------------------------------------------------------------------*/

enum tda18271_mode {
	TDA18271_ANALOG,
	TDA18271_DIGITAL,
};

struct tda18271_priv {
	u8 i2c_addr;
	struct i2c_adapter *i2c_adap;
	unsigned char tda18271_regs[TDA18271_NUM_REGS];

	enum tda18271_mode mode;
	enum tda18271_i2c_gate gate;

	u32 frequency;
	u32 bandwidth;
};

/*---------------------------------------------------------------------*/

extern int tda18271_debug;

#define dprintk(level, fmt, arg...) do {\
	if (tda18271_debug & level) \
		printk(KERN_DEBUG "%s: " fmt, __FUNCTION__, ##arg); } while (0)

#define DBG_INFO 1
#define DBG_MAP  2
#define DBG_REG  4

#define dbg_info(fmt, arg...) dprintk(DBG_INFO, fmt, ##arg)
#define dbg_map(fmt, arg...)   dprintk(DBG_MAP, fmt, ##arg)
#define dbg_reg(fmt, arg...)   dprintk(DBG_REG, fmt, ##arg)

/*---------------------------------------------------------------------*/

enum tda18271_map_type {
	/* tda18271_pll_map */
	MAIN_PLL,
	CAL_PLL,
	/* tda18271_map */
	RF_CAL,
	RF_CAL_KMCO,
	BP_FILTER,
	RF_BAND,
	GAIN_TAPER,
	IR_MEASURE,
};

extern int tda18271_lookup_pll_map(enum tda18271_map_type map_type,
				   u32 *freq, u8 *post_div, u8 *div);
extern int tda18271_lookup_map(enum tda18271_map_type map_type,
			       u32 *freq, u8 *val);

#endif /* __TDA18271_PRIV_H__ */

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
