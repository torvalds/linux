// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Driver for the Integrant ITD1000 "Zero-IF Tuner IC for Direct Broadcast Satellite"
 *
 *  Copyright (c) 2007-8 Patrick Boettcher <pb@linuxtv.org>
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/delay.h>
#include <linux/dvb/frontend.h>
#include <linux/i2c.h>
#include <linux/slab.h>

#include <media/dvb_frontend.h>

#include "itd1000.h"
#include "itd1000_priv.h"

/* Max transfer size done by I2C transfer functions */
#define MAX_XFER_SIZE  64

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off debugging (default:off).");

#define itd_dbg(args...)  do { \
	if (debug) { \
		printk(KERN_DEBUG   "ITD1000: " args);\
	} \
} while (0)

#define itd_warn(args...) do { \
	printk(KERN_WARNING "ITD1000: " args); \
} while (0)

#define itd_info(args...) do { \
	printk(KERN_INFO    "ITD1000: " args); \
} while (0)

/* don't write more than one byte with flexcop behind */
static int itd1000_write_regs(struct itd1000_state *state, u8 reg, u8 v[], u8 len)
{
	u8 buf[MAX_XFER_SIZE];
	struct i2c_msg msg = {
		.addr = state->cfg->i2c_address, .flags = 0, .buf = buf, .len = len+1
	};

	if (1 + len > sizeof(buf)) {
		printk(KERN_WARNING
		       "itd1000: i2c wr reg=%04x: len=%d is too big!\n",
		       reg, len);
		return -EINVAL;
	}

	buf[0] = reg;
	memcpy(&buf[1], v, len);

	/* itd_dbg("wr %02x: %02x\n", reg, v[0]); */

	if (i2c_transfer(state->i2c, &msg, 1) != 1) {
		printk(KERN_WARNING "itd1000 I2C write failed\n");
		return -EREMOTEIO;
	}
	return 0;
}

static int itd1000_read_reg(struct itd1000_state *state, u8 reg)
{
	u8 val;
	struct i2c_msg msg[2] = {
		{ .addr = state->cfg->i2c_address, .flags = 0,        .buf = &reg, .len = 1 },
		{ .addr = state->cfg->i2c_address, .flags = I2C_M_RD, .buf = &val, .len = 1 },
	};

	/* ugly flexcop workaround */
	itd1000_write_regs(state, (reg - 1) & 0xff, &state->shadow[(reg - 1) & 0xff], 1);

	if (i2c_transfer(state->i2c, msg, 2) != 2) {
		itd_warn("itd1000 I2C read failed\n");
		return -EREMOTEIO;
	}
	return val;
}

static inline int itd1000_write_reg(struct itd1000_state *state, u8 r, u8 v)
{
	u8 tmp = v; /* see gcc.gnu.org/bugzilla/show_bug.cgi?id=81715 */
	int ret = itd1000_write_regs(state, r, &tmp, 1);
	state->shadow[r] = tmp;
	return ret;
}


static struct {
	u32 symbol_rate;
	u8  pgaext  : 4; /* PLLFH */
	u8  bbgvmin : 4; /* BBGVMIN */
} itd1000_lpf_pga[] = {
	{        0, 0x8, 0x3 },
	{  5200000, 0x8, 0x3 },
	{ 12200000, 0x4, 0x3 },
	{ 15400000, 0x2, 0x3 },
	{ 19800000, 0x2, 0x3 },
	{ 21500000, 0x2, 0x3 },
	{ 24500000, 0x2, 0x3 },
	{ 28400000, 0x2, 0x3 },
	{ 33400000, 0x2, 0x3 },
	{ 34400000, 0x1, 0x4 },
	{ 34400000, 0x1, 0x4 },
	{ 38400000, 0x1, 0x4 },
	{ 38400000, 0x1, 0x4 },
	{ 40400000, 0x1, 0x4 },
	{ 45400000, 0x1, 0x4 },
};

static void itd1000_set_lpf_bw(struct itd1000_state *state, u32 symbol_rate)
{
	u8 i;
	u8 con1    = itd1000_read_reg(state, CON1)    & 0xfd;
	u8 pllfh   = itd1000_read_reg(state, PLLFH)   & 0x0f;
	u8 bbgvmin = itd1000_read_reg(state, BBGVMIN) & 0xf0;
	u8 bw      = itd1000_read_reg(state, BW)      & 0xf0;

	itd_dbg("symbol_rate = %d\n", symbol_rate);

	/* not sure what is that ? - starting to download the table */
	itd1000_write_reg(state, CON1, con1 | (1 << 1));

	for (i = 0; i < ARRAY_SIZE(itd1000_lpf_pga); i++)
		if (symbol_rate < itd1000_lpf_pga[i].symbol_rate) {
			itd_dbg("symrate: index: %d pgaext: %x, bbgvmin: %x\n", i, itd1000_lpf_pga[i].pgaext, itd1000_lpf_pga[i].bbgvmin);
			itd1000_write_reg(state, PLLFH,   pllfh | (itd1000_lpf_pga[i].pgaext << 4));
			itd1000_write_reg(state, BBGVMIN, bbgvmin | (itd1000_lpf_pga[i].bbgvmin));
			itd1000_write_reg(state, BW,      bw | (i & 0x0f));
			break;
		}

	itd1000_write_reg(state, CON1, con1 | (0 << 1));
}

static struct {
	u8 vcorg;
	u32 fmax_rg;
} itd1000_vcorg[] = {
	{  1,  920000 },
	{  2,  971000 },
	{  3, 1031000 },
	{  4, 1091000 },
	{  5, 1171000 },
	{  6, 1281000 },
	{  7, 1381000 },
	{  8,  500000 },	/* this is intentional. */
	{  9, 1451000 },
	{ 10, 1531000 },
	{ 11, 1631000 },
	{ 12, 1741000 },
	{ 13, 1891000 },
	{ 14, 2071000 },
	{ 15, 2250000 },
};

static void itd1000_set_vco(struct itd1000_state *state, u32 freq_khz)
{
	u8 i;
	u8 gvbb_i2c     = itd1000_read_reg(state, GVBB_I2C) & 0xbf;
	u8 vco_chp1_i2c = itd1000_read_reg(state, VCO_CHP1_I2C) & 0x0f;
	u8 adcout;

	/* reserved bit again (reset ?) */
	itd1000_write_reg(state, GVBB_I2C, gvbb_i2c | (1 << 6));

	for (i = 0; i < ARRAY_SIZE(itd1000_vcorg); i++) {
		if (freq_khz < itd1000_vcorg[i].fmax_rg) {
			itd1000_write_reg(state, VCO_CHP1_I2C, vco_chp1_i2c | (itd1000_vcorg[i].vcorg << 4));
			msleep(1);

			adcout = itd1000_read_reg(state, PLLLOCK) & 0x0f;

			itd_dbg("VCO: %dkHz: %d -> ADCOUT: %d %02x\n", freq_khz, itd1000_vcorg[i].vcorg, adcout, vco_chp1_i2c);

			if (adcout > 13) {
				if (!(itd1000_vcorg[i].vcorg == 7 || itd1000_vcorg[i].vcorg == 15))
					itd1000_write_reg(state, VCO_CHP1_I2C, vco_chp1_i2c | ((itd1000_vcorg[i].vcorg + 1) << 4));
			} else if (adcout < 2) {
				if (!(itd1000_vcorg[i].vcorg == 1 || itd1000_vcorg[i].vcorg == 9))
					itd1000_write_reg(state, VCO_CHP1_I2C, vco_chp1_i2c | ((itd1000_vcorg[i].vcorg - 1) << 4));
			}
			break;
		}
	}
}

static const struct {
	u32 freq;
	u8 values[10]; /* RFTR, RFST1 - RFST9 */
} itd1000_fre_values[] = {
	{ 1075000, { 0x59, 0x1d, 0x1c, 0x17, 0x16, 0x0f, 0x0e, 0x0c, 0x0b, 0x0a } },
	{ 1250000, { 0x89, 0x1e, 0x1d, 0x17, 0x15, 0x0f, 0x0e, 0x0c, 0x0b, 0x0a } },
	{ 1450000, { 0x89, 0x1e, 0x1d, 0x17, 0x15, 0x0f, 0x0e, 0x0c, 0x0b, 0x0a } },
	{ 1650000, { 0x69, 0x1e, 0x1d, 0x17, 0x15, 0x0f, 0x0e, 0x0c, 0x0b, 0x0a } },
	{ 1750000, { 0x69, 0x1e, 0x17, 0x15, 0x14, 0x0f, 0x0e, 0x0c, 0x0b, 0x0a } },
	{ 1850000, { 0x69, 0x1d, 0x17, 0x16, 0x14, 0x0f, 0x0e, 0x0d, 0x0b, 0x0a } },
	{ 1900000, { 0x69, 0x1d, 0x17, 0x15, 0x14, 0x0f, 0x0e, 0x0d, 0x0b, 0x0a } },
	{ 1950000, { 0x69, 0x1d, 0x17, 0x16, 0x14, 0x13, 0x0e, 0x0d, 0x0b, 0x0a } },
	{ 2050000, { 0x69, 0x1e, 0x1d, 0x17, 0x16, 0x14, 0x13, 0x0e, 0x0b, 0x0a } },
	{ 2150000, { 0x69, 0x1d, 0x1c, 0x17, 0x15, 0x14, 0x13, 0x0f, 0x0e, 0x0b } }
};


#define FREF 16

static void itd1000_set_lo(struct itd1000_state *state, u32 freq_khz)
{
	int i, j;
	u32 plln, pllf;
	u64 tmp;

	plln = (freq_khz * 1000) / 2 / FREF;

	/* Compute the factional part times 1000 */
	tmp  = plln % 1000000;
	plln /= 1000000;

	tmp *= 1048576;
	do_div(tmp, 1000000);
	pllf = (u32) tmp;

	state->frequency = ((plln * 1000) + (pllf * 1000)/1048576) * 2*FREF;
	itd_dbg("frequency: %dkHz (wanted) %dkHz (set), PLLF = %d, PLLN = %d\n", freq_khz, state->frequency, pllf, plln);

	itd1000_write_reg(state, PLLNH, 0x80); /* PLLNH */
	itd1000_write_reg(state, PLLNL, plln & 0xff);
	itd1000_write_reg(state, PLLFH, (itd1000_read_reg(state, PLLFH) & 0xf0) | ((pllf >> 16) & 0x0f));
	itd1000_write_reg(state, PLLFM, (pllf >> 8) & 0xff);
	itd1000_write_reg(state, PLLFL, (pllf >> 0) & 0xff);

	for (i = 0; i < ARRAY_SIZE(itd1000_fre_values); i++) {
		if (freq_khz <= itd1000_fre_values[i].freq) {
			itd_dbg("fre_values: %d\n", i);
			itd1000_write_reg(state, RFTR, itd1000_fre_values[i].values[0]);
			for (j = 0; j < 9; j++)
				itd1000_write_reg(state, RFST1+j, itd1000_fre_values[i].values[j+1]);
			break;
		}
	}

	itd1000_set_vco(state, freq_khz);
}

static int itd1000_set_parameters(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct itd1000_state *state = fe->tuner_priv;
	u8 pllcon1;

	itd1000_set_lo(state, c->frequency);
	itd1000_set_lpf_bw(state, c->symbol_rate);

	pllcon1 = itd1000_read_reg(state, PLLCON1) & 0x7f;
	itd1000_write_reg(state, PLLCON1, pllcon1 | (1 << 7));
	itd1000_write_reg(state, PLLCON1, pllcon1);

	return 0;
}

static int itd1000_get_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	struct itd1000_state *state = fe->tuner_priv;
	*frequency = state->frequency;
	return 0;
}

static int itd1000_get_bandwidth(struct dvb_frontend *fe, u32 *bandwidth)
{
	return 0;
}

static u8 itd1000_init_tab[][2] = {
	{ PLLCON1,       0x65 }, /* Register does not change */
	{ PLLNH,         0x80 }, /* Bits [7:6] do not change */
	{ RESERVED_0X6D, 0x3b },
	{ VCO_CHP2_I2C,  0x12 },
	{ 0x72,          0xf9 }, /* No such regsister defined */
	{ RESERVED_0X73, 0xff },
	{ RESERVED_0X74, 0xb2 },
	{ RESERVED_0X75, 0xc7 },
	{ EXTGVBBRF,     0xf0 },
	{ DIVAGCCK,      0x80 },
	{ BBTR,          0xa0 },
	{ RESERVED_0X7E, 0x4f },
	{ 0x82,          0x88 }, /* No such regsister defined */
	{ 0x83,          0x80 }, /* No such regsister defined */
	{ 0x84,          0x80 }, /* No such regsister defined */
	{ RESERVED_0X85, 0x74 },
	{ RESERVED_0X86, 0xff },
	{ RESERVED_0X88, 0x02 },
	{ RESERVED_0X89, 0x16 },
	{ RFST0,         0x1f },
	{ RESERVED_0X94, 0x66 },
	{ RESERVED_0X95, 0x66 },
	{ RESERVED_0X96, 0x77 },
	{ RESERVED_0X97, 0x99 },
	{ RESERVED_0X98, 0xff },
	{ RESERVED_0X99, 0xfc },
	{ RESERVED_0X9A, 0xba },
	{ RESERVED_0X9B, 0xaa },
};

static u8 itd1000_reinit_tab[][2] = {
	{ VCO_CHP1_I2C, 0x8a },
	{ BW,           0x87 },
	{ GVBB_I2C,     0x03 },
	{ BBGVMIN,      0x03 },
	{ CON1,         0x2e },
};


static int itd1000_init(struct dvb_frontend *fe)
{
	struct itd1000_state *state = fe->tuner_priv;
	int i;

	for (i = 0; i < ARRAY_SIZE(itd1000_init_tab); i++)
		itd1000_write_reg(state, itd1000_init_tab[i][0], itd1000_init_tab[i][1]);

	for (i = 0; i < ARRAY_SIZE(itd1000_reinit_tab); i++)
		itd1000_write_reg(state, itd1000_reinit_tab[i][0], itd1000_reinit_tab[i][1]);

	return 0;
}

static int itd1000_sleep(struct dvb_frontend *fe)
{
	return 0;
}

static void itd1000_release(struct dvb_frontend *fe)
{
	kfree(fe->tuner_priv);
	fe->tuner_priv = NULL;
}

static const struct dvb_tuner_ops itd1000_tuner_ops = {
	.info = {
		.name              = "Integrant ITD1000",
		.frequency_min_hz  =  950 * MHz,
		.frequency_max_hz  = 2150 * MHz,
		.frequency_step_hz =  125 * kHz,
	},

	.release       = itd1000_release,

	.init          = itd1000_init,
	.sleep         = itd1000_sleep,

	.set_params    = itd1000_set_parameters,
	.get_frequency = itd1000_get_frequency,
	.get_bandwidth = itd1000_get_bandwidth
};


struct dvb_frontend *itd1000_attach(struct dvb_frontend *fe, struct i2c_adapter *i2c, struct itd1000_config *cfg)
{
	struct itd1000_state *state = NULL;
	u8 i = 0;

	state = kzalloc(sizeof(struct itd1000_state), GFP_KERNEL);
	if (state == NULL)
		return NULL;

	state->cfg = cfg;
	state->i2c = i2c;

	i = itd1000_read_reg(state, 0);
	if (i != 0) {
		kfree(state);
		return NULL;
	}
	itd_info("successfully identified (ID: %d)\n", i);

	memset(state->shadow, 0xff, sizeof(state->shadow));
	for (i = 0x65; i < 0x9c; i++)
		state->shadow[i] = itd1000_read_reg(state, i);

	memcpy(&fe->ops.tuner_ops, &itd1000_tuner_ops, sizeof(struct dvb_tuner_ops));

	fe->tuner_priv = state;

	return fe;
}
EXPORT_SYMBOL_GPL(itd1000_attach);

MODULE_AUTHOR("Patrick Boettcher <pb@linuxtv.org>");
MODULE_DESCRIPTION("Integrant ITD1000 driver");
MODULE_LICENSE("GPL");
