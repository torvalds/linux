/*
 * descriptions + helper functions for simple dvb plls.
 */

#ifndef __DVB_PLL_H__
#define __DVB_PLL_H__

#include <linux/i2c.h>
#include "dvb_frontend.h"

#define DVB_PLL_UNDEFINED               0
#define DVB_PLL_THOMSON_DTT7579         1
#define DVB_PLL_THOMSON_DTT759X         2
#define DVB_PLL_THOMSON_DTT7610         3
#define DVB_PLL_LG_Z201                 4
#define DVB_PLL_MICROTUNE_4042          5
#define DVB_PLL_THOMSON_DTT761X         6
#define DVB_PLL_UNKNOWN_1               7
#define DVB_PLL_TUA6010XS               8
#define DVB_PLL_ENV57H1XD5              9
#define DVB_PLL_TUA6034                10
#define DVB_PLL_LG_TDVS_H06XF          11
#define DVB_PLL_TDA665X                12
#define DVB_PLL_FMD1216ME              13
#define DVB_PLL_TDED4                  14
#define DVB_PLL_TUV1236D               15
#define DVB_PLL_TDHU2                  16
#define DVB_PLL_SAMSUNG_TBMV           17
#define DVB_PLL_PHILIPS_SD1878_TDA8261 18
#define DVB_PLL_PHILIPS_TD1316         19
#define DVB_PLL_THOMSON_FE6600         20
#define DVB_PLL_OPERA1                 21
#define DVB_PLL_FCV1236D               22

/**
 * Attach a dvb-pll to the supplied frontend structure.
 *
 * @param fe Frontend to attach to.
 * @param pll_addr i2c address of the PLL (if used).
 * @param i2c i2c adapter to use (set to NULL if not used).
 * @param pll_desc_id dvb_pll_desc to use.
 * @return Frontend pointer on success, NULL on failure
 */
#if defined(CONFIG_DVB_PLL) || (defined(CONFIG_DVB_PLL_MODULE) && defined(MODULE))
extern struct dvb_frontend *dvb_pll_attach(struct dvb_frontend *fe,
					   int pll_addr,
					   struct i2c_adapter *i2c,
					   unsigned int pll_desc_id);
#else
static inline struct dvb_frontend *dvb_pll_attach(struct dvb_frontend *fe,
					   int pll_addr,
					   struct i2c_adapter *i2c,
					   unsigned int pll_desc_id)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __FUNCTION__);
	return NULL;
}
#endif

#endif
