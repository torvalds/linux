/*
    tda18271-priv.h - private header for the NXP TDA18271 silicon tuner

    Copyright (C) 2007, 2008 Michael Krufky <mkrufky@linuxtv.org>

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
#include <linux/mutex.h>
#include "tuner-i2c.h"
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

struct tda18271_rf_tracking_filter_cal {
	u32 rfmax;
	u8  rfband;
	u32 rf1_def;
	u32 rf2_def;
	u32 rf3_def;
	u32 rf1;
	u32 rf2;
	u32 rf3;
	int rf_a1;
	int rf_b1;
	int rf_a2;
	int rf_b2;
};

enum tda18271_pll {
	TDA18271_MAIN_PLL,
	TDA18271_CAL_PLL,
};

struct tda18271_map_layout;

enum tda18271_ver {
	TDA18271HDC1,
	TDA18271HDC2,
};

struct tda18271_priv {
	unsigned char tda18271_regs[TDA18271_NUM_REGS];

	struct list_head	hybrid_tuner_instance_list;
	struct tuner_i2c_props	i2c_props;

	enum tda18271_mode mode;
	enum tda18271_role role;
	enum tda18271_i2c_gate gate;
	enum tda18271_ver id;
	enum tda18271_output_options output_opt;

	unsigned int config; /* interface to saa713x / tda829x */
	unsigned int tm_rfcal;
	unsigned int cal_initialized:1;
	unsigned int small_i2c:1;

	struct tda18271_map_layout *maps;
	struct tda18271_std_map std;
	struct tda18271_rf_tracking_filter_cal rf_cal_state[8];

	struct mutex lock;

	u32 frequency;
	u32 bandwidth;
};

/*---------------------------------------------------------------------*/

extern int tda18271_debug;

#define DBG_INFO 1
#define DBG_MAP  2
#define DBG_REG  4
#define DBG_ADV  8
#define DBG_CAL  16

#define tda_printk(kern, fmt, arg...) \
	printk(kern "%s: " fmt, __func__, ##arg)

#define tda_dprintk(lvl, fmt, arg...) do {\
	if (tda18271_debug & lvl) \
		tda_printk(KERN_DEBUG, fmt, ##arg); } while (0)

#define tda_info(fmt, arg...)     printk(KERN_INFO     fmt, ##arg)
#define tda_warn(fmt, arg...) tda_printk(KERN_WARNING, fmt, ##arg)
#define tda_err(fmt, arg...)  tda_printk(KERN_ERR,     fmt, ##arg)
#define tda_dbg(fmt, arg...)  tda_dprintk(DBG_INFO,    fmt, ##arg)
#define tda_map(fmt, arg...)  tda_dprintk(DBG_MAP,     fmt, ##arg)
#define tda_reg(fmt, arg...)  tda_dprintk(DBG_REG,     fmt, ##arg)
#define tda_cal(fmt, arg...)  tda_dprintk(DBG_CAL,     fmt, ##arg)

#define tda_fail(ret)							     \
({									     \
	int __ret;							     \
	__ret = (ret < 0);						     \
	if (__ret)							     \
		tda_printk(KERN_ERR, "error %d on line %d\n", ret, __LINE__);\
	__ret;								     \
})

/*---------------------------------------------------------------------*/

enum tda18271_map_type {
	/* tda18271_pll_map */
	MAIN_PLL,
	CAL_PLL,
	/* tda18271_map */
	RF_CAL,
	RF_CAL_KMCO,
	RF_CAL_DC_OVER_DT,
	BP_FILTER,
	RF_BAND,
	GAIN_TAPER,
	IR_MEASURE,
};

extern int tda18271_lookup_pll_map(struct dvb_frontend *fe,
				   enum tda18271_map_type map_type,
				   u32 *freq, u8 *post_div, u8 *div);
extern int tda18271_lookup_map(struct dvb_frontend *fe,
			       enum tda18271_map_type map_type,
			       u32 *freq, u8 *val);

extern int tda18271_lookup_thermometer(struct dvb_frontend *fe);

extern int tda18271_lookup_rf_band(struct dvb_frontend *fe,
				   u32 *freq, u8 *rf_band);

extern int tda18271_lookup_cid_target(struct dvb_frontend *fe,
				      u32 *freq, u8 *cid_target,
				      u16 *count_limit);

extern int tda18271_assign_map_layout(struct dvb_frontend *fe);

/*---------------------------------------------------------------------*/

extern int tda18271_read_regs(struct dvb_frontend *fe);
extern int tda18271_read_extended(struct dvb_frontend *fe);
extern int tda18271_write_regs(struct dvb_frontend *fe, int idx, int len);
extern int tda18271_init_regs(struct dvb_frontend *fe);

extern int tda18271_charge_pump_source(struct dvb_frontend *fe,
				       enum tda18271_pll pll, int force);
extern int tda18271_set_standby_mode(struct dvb_frontend *fe,
				     int sm, int sm_lt, int sm_xt);

extern int tda18271_calc_main_pll(struct dvb_frontend *fe, u32 freq);
extern int tda18271_calc_cal_pll(struct dvb_frontend *fe, u32 freq);

extern int tda18271_calc_bp_filter(struct dvb_frontend *fe, u32 *freq);
extern int tda18271_calc_km(struct dvb_frontend *fe, u32 *freq);
extern int tda18271_calc_rf_band(struct dvb_frontend *fe, u32 *freq);
extern int tda18271_calc_gain_taper(struct dvb_frontend *fe, u32 *freq);
extern int tda18271_calc_ir_measure(struct dvb_frontend *fe, u32 *freq);
extern int tda18271_calc_rf_cal(struct dvb_frontend *fe, u32 *freq);

#endif /* __TDA18271_PRIV_H__ */

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
