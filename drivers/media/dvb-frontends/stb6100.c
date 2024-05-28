// SPDX-License-Identifier: GPL-2.0-or-later
/*
	STB6100 Silicon Tuner
	Copyright (C) Manu Abraham (abraham.manu@gmail.com)

	Copyright (C) ST Microelectronics

*/

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>

#include <media/dvb_frontend.h>
#include "stb6100.h"

static unsigned int verbose;
module_param(verbose, int, 0644);

/* Max transfer size done by I2C transfer functions */
#define MAX_XFER_SIZE  64

#define FE_ERROR		0
#define FE_NOTICE		1
#define FE_INFO			2
#define FE_DEBUG		3

#define dprintk(x, y, z, format, arg...) do {						\
	if (z) {									\
		if	((x > FE_ERROR) && (x > y))					\
			printk(KERN_ERR "%s: " format "\n", __func__ , ##arg);		\
		else if	((x > FE_NOTICE) && (x > y))					\
			printk(KERN_NOTICE "%s: " format "\n", __func__ , ##arg);	\
		else if ((x > FE_INFO) && (x > y))					\
			printk(KERN_INFO "%s: " format "\n", __func__ , ##arg);		\
		else if ((x > FE_DEBUG) && (x > y))					\
			printk(KERN_DEBUG "%s: " format "\n", __func__ , ##arg);	\
	} else {									\
		if (x > y)								\
			printk(format, ##arg);						\
	}										\
} while (0)

struct stb6100_lkup {
	u32 val_low;
	u32 val_high;
	u8   reg;
};

static void stb6100_release(struct dvb_frontend *fe);

static const struct stb6100_lkup lkup[] = {
	{       0,  950000, 0x0a },
	{  950000, 1000000, 0x0a },
	{ 1000000, 1075000, 0x0c },
	{ 1075000, 1200000, 0x00 },
	{ 1200000, 1300000, 0x01 },
	{ 1300000, 1370000, 0x02 },
	{ 1370000, 1470000, 0x04 },
	{ 1470000, 1530000, 0x05 },
	{ 1530000, 1650000, 0x06 },
	{ 1650000, 1800000, 0x08 },
	{ 1800000, 1950000, 0x0a },
	{ 1950000, 2150000, 0x0c },
	{ 2150000, 9999999, 0x0c },
	{       0,       0, 0x00 }
};

/* Register names for easy debugging.	*/
static const char *stb6100_regnames[] = {
	[STB6100_LD]		= "LD",
	[STB6100_VCO]		= "VCO",
	[STB6100_NI]		= "NI",
	[STB6100_NF_LSB]	= "NF",
	[STB6100_K]		= "K",
	[STB6100_G]		= "G",
	[STB6100_F]		= "F",
	[STB6100_DLB]		= "DLB",
	[STB6100_TEST1]		= "TEST1",
	[STB6100_FCCK]		= "FCCK",
	[STB6100_LPEN]		= "LPEN",
	[STB6100_TEST3]		= "TEST3",
};

/* Template for normalisation, i.e. setting unused or undocumented
 * bits as required according to the documentation.
 */
struct stb6100_regmask {
	u8 mask;
	u8 set;
};

static const struct stb6100_regmask stb6100_template[] = {
	[STB6100_LD]		= { 0xff, 0x00 },
	[STB6100_VCO]		= { 0xff, 0x00 },
	[STB6100_NI]		= { 0xff, 0x00 },
	[STB6100_NF_LSB]	= { 0xff, 0x00 },
	[STB6100_K]		= { 0xc7, 0x38 },
	[STB6100_G]		= { 0xef, 0x10 },
	[STB6100_F]		= { 0x1f, 0xc0 },
	[STB6100_DLB]		= { 0x38, 0xc4 },
	[STB6100_TEST1]		= { 0x00, 0x8f },
	[STB6100_FCCK]		= { 0x40, 0x0d },
	[STB6100_LPEN]		= { 0xf0, 0x0b },
	[STB6100_TEST3]		= { 0x00, 0xde },
};

/*
 * Currently unused. Some boards might need it in the future
 */
static __always_unused inline void stb6100_normalise_regs(u8 regs[])
{
	int i;

	for (i = 0; i < STB6100_NUMREGS; i++)
		regs[i] = (regs[i] & stb6100_template[i].mask) | stb6100_template[i].set;
}

static int stb6100_read_regs(struct stb6100_state *state, u8 regs[])
{
	int rc;
	struct i2c_msg msg = {
		.addr	= state->config->tuner_address,
		.flags	= I2C_M_RD,
		.buf	= regs,
		.len	= STB6100_NUMREGS
	};

	rc = i2c_transfer(state->i2c, &msg, 1);
	if (unlikely(rc != 1)) {
		dprintk(verbose, FE_ERROR, 1, "Read (0x%x) err, rc=[%d]",
			state->config->tuner_address, rc);

		return -EREMOTEIO;
	}
	if (unlikely(verbose > FE_DEBUG)) {
		int i;

		dprintk(verbose, FE_DEBUG, 1, "    Read from 0x%02x", state->config->tuner_address);
		for (i = 0; i < STB6100_NUMREGS; i++)
			dprintk(verbose, FE_DEBUG, 1, "        %s: 0x%02x", stb6100_regnames[i], regs[i]);
	}
	return 0;
}

static int stb6100_read_reg(struct stb6100_state *state, u8 reg)
{
	u8 regs[STB6100_NUMREGS];

	struct i2c_msg msg = {
		.addr	= state->config->tuner_address + reg,
		.flags	= I2C_M_RD,
		.buf	= regs,
		.len	= 1
	};

	i2c_transfer(state->i2c, &msg, 1);

	if (unlikely(reg >= STB6100_NUMREGS)) {
		dprintk(verbose, FE_ERROR, 1, "Invalid register offset 0x%x", reg);
		return -EINVAL;
	}
	if (unlikely(verbose > FE_DEBUG)) {
		dprintk(verbose, FE_DEBUG, 1, "    Read from 0x%02x", state->config->tuner_address);
		dprintk(verbose, FE_DEBUG, 1, "        %s: 0x%02x", stb6100_regnames[reg], regs[0]);
	}

	return (unsigned int)regs[0];
}

static int stb6100_write_reg_range(struct stb6100_state *state, u8 buf[], int start, int len)
{
	int rc;
	u8 cmdbuf[MAX_XFER_SIZE];
	struct i2c_msg msg = {
		.addr	= state->config->tuner_address,
		.flags	= 0,
		.buf	= cmdbuf,
		.len	= len + 1
	};

	if (1 + len > sizeof(cmdbuf)) {
		printk(KERN_WARNING
		       "%s: i2c wr: len=%d is too big!\n",
		       KBUILD_MODNAME, len);
		return -EINVAL;
	}

	if (unlikely(start < 1 || start + len > STB6100_NUMREGS)) {
		dprintk(verbose, FE_ERROR, 1, "Invalid register range %d:%d",
			start, len);
		return -EINVAL;
	}
	memcpy(&cmdbuf[1], buf, len);
	cmdbuf[0] = start;

	if (unlikely(verbose > FE_DEBUG)) {
		int i;

		dprintk(verbose, FE_DEBUG, 1, "    Write @ 0x%02x: [%d:%d]", state->config->tuner_address, start, len);
		for (i = 0; i < len; i++)
			dprintk(verbose, FE_DEBUG, 1, "        %s: 0x%02x", stb6100_regnames[start + i], buf[i]);
	}
	rc = i2c_transfer(state->i2c, &msg, 1);
	if (unlikely(rc != 1)) {
		dprintk(verbose, FE_ERROR, 1, "(0x%x) write err [%d:%d], rc=[%d]",
			(unsigned int)state->config->tuner_address, start, len,	rc);
		return -EREMOTEIO;
	}
	return 0;
}

static int stb6100_write_reg(struct stb6100_state *state, u8 reg, u8 data)
{
	u8 tmp = data; /* see gcc.gnu.org/bugzilla/show_bug.cgi?id=81715 */

	if (unlikely(reg >= STB6100_NUMREGS)) {
		dprintk(verbose, FE_ERROR, 1, "Invalid register offset 0x%x", reg);
		return -EREMOTEIO;
	}
	tmp = (tmp & stb6100_template[reg].mask) | stb6100_template[reg].set;
	return stb6100_write_reg_range(state, &tmp, reg, 1);
}


static int stb6100_get_status(struct dvb_frontend *fe, u32 *status)
{
	int rc;
	struct stb6100_state *state = fe->tuner_priv;

	rc = stb6100_read_reg(state, STB6100_LD);
	if (rc < 0) {
		dprintk(verbose, FE_ERROR, 1, "%s failed", __func__);
		return rc;
	}
	return (rc & STB6100_LD_LOCK) ? TUNER_STATUS_LOCKED : 0;
}

static int stb6100_get_bandwidth(struct dvb_frontend *fe, u32 *bandwidth)
{
	int rc;
	u8 f;
	u32 bw;
	struct stb6100_state *state = fe->tuner_priv;

	rc = stb6100_read_reg(state, STB6100_F);
	if (rc < 0)
		return rc;
	f = rc & STB6100_F_F;

	bw = (f + 5) * 2000;	/* x2 for ZIF	*/

	*bandwidth = state->bandwidth = bw * 1000;
	dprintk(verbose, FE_DEBUG, 1, "bandwidth = %u Hz", state->bandwidth);
	return 0;
}

static int stb6100_set_bandwidth(struct dvb_frontend *fe, u32 bandwidth)
{
	u32 tmp;
	int rc;
	struct stb6100_state *state = fe->tuner_priv;

	dprintk(verbose, FE_DEBUG, 1, "set bandwidth to %u Hz", bandwidth);

	bandwidth /= 2; /* ZIF */

	if (bandwidth >= 36000000)	/* F[4:0] BW/2 max =31+5=36 mhz for F=31	*/
		tmp = 31;
	else if (bandwidth <= 5000000)	/* bw/2 min = 5Mhz for F=0			*/
		tmp = 0;
	else				/* if 5 < bw/2 < 36				*/
		tmp = (bandwidth + 500000) / 1000000 - 5;

	/* Turn on LPF bandwidth setting clock control,
	 * set bandwidth, wait 10ms, turn off.
	 */
	rc = stb6100_write_reg(state, STB6100_FCCK, 0x0d | STB6100_FCCK_FCCK);
	if (rc < 0)
		return rc;
	rc = stb6100_write_reg(state, STB6100_F, 0xc0 | tmp);
	if (rc < 0)
		return rc;

	msleep(5);  /*  This is dangerous as another (related) thread may start */

	rc = stb6100_write_reg(state, STB6100_FCCK, 0x0d);
	if (rc < 0)
		return rc;

	msleep(10);  /*  This is dangerous as another (related) thread may start */

	return 0;
}

static int stb6100_get_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	int rc;
	u32 nint, nfrac, fvco;
	int psd2, odiv;
	struct stb6100_state *state = fe->tuner_priv;
	u8 regs[STB6100_NUMREGS];

	rc = stb6100_read_regs(state, regs);
	if (rc < 0)
		return rc;

	odiv = (regs[STB6100_VCO] & STB6100_VCO_ODIV) >> STB6100_VCO_ODIV_SHIFT;
	psd2 = (regs[STB6100_K] & STB6100_K_PSD2) >> STB6100_K_PSD2_SHIFT;
	nint = regs[STB6100_NI];
	nfrac = ((regs[STB6100_K] & STB6100_K_NF_MSB) << 8) | regs[STB6100_NF_LSB];
	fvco = (nfrac * state->reference >> (9 - psd2)) + (nint * state->reference << psd2);
	*frequency = state->frequency = fvco >> (odiv + 1);

	dprintk(verbose, FE_DEBUG, 1,
		"frequency = %u kHz, odiv = %u, psd2 = %u, fxtal = %u kHz, fvco = %u kHz, N(I) = %u, N(F) = %u",
		state->frequency, odiv, psd2, state->reference,	fvco, nint, nfrac);
	return 0;
}


static int stb6100_set_frequency(struct dvb_frontend *fe, u32 frequency)
{
	int rc;
	const struct stb6100_lkup *ptr;
	struct stb6100_state *state = fe->tuner_priv;
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;

	u32 srate = 0, fvco, nint, nfrac;
	u8 regs[STB6100_NUMREGS];
	u8 g, psd2, odiv;

	dprintk(verbose, FE_DEBUG, 1, "Version 2010-8-14 13:51");

	if (fe->ops.get_frontend) {
		dprintk(verbose, FE_DEBUG, 1, "Get frontend parameters");
		fe->ops.get_frontend(fe, p);
	}
	srate = p->symbol_rate;

	/* Set up tuner cleanly, LPF calibration on */
	rc = stb6100_write_reg(state, STB6100_FCCK, 0x4d | STB6100_FCCK_FCCK);
	if (rc < 0)
		return rc;  /* allow LPF calibration */

	/* PLL Loop disabled, bias on, VCO on, synth on */
	regs[STB6100_LPEN] = 0xeb;
	rc = stb6100_write_reg(state, STB6100_LPEN, regs[STB6100_LPEN]);
	if (rc < 0)
		return rc;

	/* Program the registers with their data values */

	/* VCO divide ratio (LO divide ratio, VCO prescaler enable).	*/
	if (frequency <= 1075000)
		odiv = 1;
	else
		odiv = 0;

	/* VCO enabled, search clock off as per LL3.7, 3.4.1 */
	regs[STB6100_VCO] = 0xe0 | (odiv << STB6100_VCO_ODIV_SHIFT);

	/* OSM	*/
	for (ptr = lkup;
	     (ptr->val_high != 0) && !CHKRANGE(frequency, ptr->val_low, ptr->val_high);
	     ptr++);

	if (ptr->val_high == 0) {
		printk(KERN_ERR "%s: frequency out of range: %u kHz\n", __func__, frequency);
		return -EINVAL;
	}
	regs[STB6100_VCO] = (regs[STB6100_VCO] & ~STB6100_VCO_OSM) | ptr->reg;
	rc = stb6100_write_reg(state, STB6100_VCO, regs[STB6100_VCO]);
	if (rc < 0)
		return rc;

	if ((frequency > 1075000) && (frequency <= 1325000))
		psd2 = 0;
	else
		psd2 = 1;
	/* F(VCO) = F(LO) * (ODIV == 0 ? 2 : 4)			*/
	fvco = frequency << (1 + odiv);
	/* N(I) = floor(f(VCO) / (f(XTAL) * (PSD2 ? 2 : 1)))	*/
	nint = fvco / (state->reference << psd2);
	/* N(F) = round(f(VCO) / f(XTAL) * (PSD2 ? 2 : 1) - N(I)) * 2 ^ 9	*/
	nfrac = DIV_ROUND_CLOSEST((fvco - (nint * state->reference << psd2))
					 << (9 - psd2), state->reference);

	/* NI */
	regs[STB6100_NI] = nint;
	rc = stb6100_write_reg(state, STB6100_NI, regs[STB6100_NI]);
	if (rc < 0)
		return rc;

	/* NF */
	regs[STB6100_NF_LSB] = nfrac;
	rc = stb6100_write_reg(state, STB6100_NF_LSB, regs[STB6100_NF_LSB]);
	if (rc < 0)
		return rc;

	/* K */
	regs[STB6100_K] = (0x38 & ~STB6100_K_PSD2) | (psd2 << STB6100_K_PSD2_SHIFT);
	regs[STB6100_K] = (regs[STB6100_K] & ~STB6100_K_NF_MSB) | ((nfrac >> 8) & STB6100_K_NF_MSB);
	rc = stb6100_write_reg(state, STB6100_K, regs[STB6100_K]);
	if (rc < 0)
		return rc;

	/* G Baseband gain. */
	if (srate >= 15000000)
		g = 9;  /*  +4 dB */
	else if (srate >= 5000000)
		g = 11; /*  +8 dB */
	else
		g = 14; /* +14 dB */

	regs[STB6100_G] = (0x10 & ~STB6100_G_G) | g;
	regs[STB6100_G] &= ~STB6100_G_GCT; /* mask GCT */
	regs[STB6100_G] |= (1 << 5); /* 2Vp-p Mode */
	rc = stb6100_write_reg(state, STB6100_G, regs[STB6100_G]);
	if (rc < 0)
		return rc;

	/* F we don't write as it is set up in BW set */

	/* DLB set DC servo loop BW to 160Hz (LLA 3.8 / 2.1) */
	regs[STB6100_DLB] = 0xcc;
	rc = stb6100_write_reg(state, STB6100_DLB, regs[STB6100_DLB]);
	if (rc < 0)
		return rc;

	dprintk(verbose, FE_DEBUG, 1,
		"frequency = %u, srate = %u, g = %u, odiv = %u, psd2 = %u, fxtal = %u, osm = %u, fvco = %u, N(I) = %u, N(F) = %u",
		frequency, srate, (unsigned int)g, (unsigned int)odiv,
		(unsigned int)psd2, state->reference,
		ptr->reg, fvco, nint, nfrac);

	/* Set up the test registers */
	regs[STB6100_TEST1] = 0x8f;
	rc = stb6100_write_reg(state, STB6100_TEST1, regs[STB6100_TEST1]);
	if (rc < 0)
		return rc;
	regs[STB6100_TEST3] = 0xde;
	rc = stb6100_write_reg(state, STB6100_TEST3, regs[STB6100_TEST3]);
	if (rc < 0)
		return rc;

	/* Bring up tuner according to LLA 3.7 3.4.1, step 2 */
	regs[STB6100_LPEN] = 0xfb; /* PLL Loop enabled, bias on, VCO on, synth on */
	rc = stb6100_write_reg(state, STB6100_LPEN, regs[STB6100_LPEN]);
	if (rc < 0)
		return rc;

	msleep(2);

	/* Bring up tuner according to LLA 3.7 3.4.1, step 3 */
	regs[STB6100_VCO] &= ~STB6100_VCO_OCK;		/* VCO fast search		*/
	rc = stb6100_write_reg(state, STB6100_VCO, regs[STB6100_VCO]);
	if (rc < 0)
		return rc;

	msleep(10);  /*  This is dangerous as another (related) thread may start */ /* wait for LO to lock */

	regs[STB6100_VCO] &= ~STB6100_VCO_OSCH;		/* vco search disabled		*/
	regs[STB6100_VCO] |= STB6100_VCO_OCK;		/* search clock off		*/
	rc = stb6100_write_reg(state, STB6100_VCO, regs[STB6100_VCO]);
	if (rc < 0)
		return rc;

	rc = stb6100_write_reg(state, STB6100_FCCK, 0x0d);
	if (rc < 0)
		return rc;  /* Stop LPF calibration */

	msleep(10);  /*  This is dangerous as another (related) thread may start */
		     /* wait for stabilisation, (should not be necessary)		*/
	return 0;
}

static int stb6100_sleep(struct dvb_frontend *fe)
{
	/* TODO: power down	*/
	return 0;
}

static int stb6100_init(struct dvb_frontend *fe)
{
	struct stb6100_state *state = fe->tuner_priv;
	int refclk = 27000000; /* Hz */

	/*
	 * iqsense = 1
	 * tunerstep = 125000
	 */
	state->bandwidth        = 36000000;		/* Hz	*/
	state->reference	= refclk / 1000;	/* kHz	*/

	/* Set default bandwidth. Modified, PN 13-May-10	*/
	return 0;
}

static int stb6100_set_params(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;

	if (c->frequency > 0)
		stb6100_set_frequency(fe, c->frequency);

	if (c->bandwidth_hz > 0)
		stb6100_set_bandwidth(fe, c->bandwidth_hz);

	return 0;
}

static const struct dvb_tuner_ops stb6100_ops = {
	.info = {
		.name			= "STB6100 Silicon Tuner",
		.frequency_min_hz	=  950 * MHz,
		.frequency_max_hz	= 2150 * MHz,
	},

	.init		= stb6100_init,
	.sleep          = stb6100_sleep,
	.get_status	= stb6100_get_status,
	.set_params	= stb6100_set_params,
	.get_frequency  = stb6100_get_frequency,
	.get_bandwidth  = stb6100_get_bandwidth,
	.release	= stb6100_release
};

struct dvb_frontend *stb6100_attach(struct dvb_frontend *fe,
				    const struct stb6100_config *config,
				    struct i2c_adapter *i2c)
{
	struct stb6100_state *state = NULL;

	state = kzalloc(sizeof (struct stb6100_state), GFP_KERNEL);
	if (!state)
		return NULL;

	state->config		= config;
	state->i2c		= i2c;
	state->frontend		= fe;
	state->reference	= config->refclock / 1000; /* kHz */
	fe->tuner_priv		= state;
	fe->ops.tuner_ops	= stb6100_ops;

	printk("%s: Attaching STB6100 \n", __func__);
	return fe;
}

static void stb6100_release(struct dvb_frontend *fe)
{
	struct stb6100_state *state = fe->tuner_priv;

	fe->tuner_priv = NULL;
	kfree(state);
}

EXPORT_SYMBOL_GPL(stb6100_attach);
MODULE_PARM_DESC(verbose, "Set Verbosity level");

MODULE_AUTHOR("Manu Abraham");
MODULE_DESCRIPTION("STB6100 Silicon tuner");
MODULE_LICENSE("GPL");
