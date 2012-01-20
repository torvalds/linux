/*
 * linux/drivers/video/omap2/dss/rfbi.c
 *
 * Copyright (C) 2009 Nokia Corporation
 * Author: Tomi Valkeinen <tomi.valkeinen@nokia.com>
 *
 * Some code and ideas taken from drivers/video/omap/ driver
 * by Imre Deak.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define DSS_SUBSYS_NAME "RFBI"

#include <linux/kernel.h>
#include <linux/dma-mapping.h>
#include <linux/export.h>
#include <linux/vmalloc.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/kfifo.h>
#include <linux/ktime.h>
#include <linux/hrtimer.h>
#include <linux/seq_file.h>
#include <linux/semaphore.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include <video/omapdss.h>
#include "dss.h"

struct rfbi_reg { u16 idx; };

#define RFBI_REG(idx)		((const struct rfbi_reg) { idx })

#define RFBI_REVISION		RFBI_REG(0x0000)
#define RFBI_SYSCONFIG		RFBI_REG(0x0010)
#define RFBI_SYSSTATUS		RFBI_REG(0x0014)
#define RFBI_CONTROL		RFBI_REG(0x0040)
#define RFBI_PIXEL_CNT		RFBI_REG(0x0044)
#define RFBI_LINE_NUMBER	RFBI_REG(0x0048)
#define RFBI_CMD		RFBI_REG(0x004c)
#define RFBI_PARAM		RFBI_REG(0x0050)
#define RFBI_DATA		RFBI_REG(0x0054)
#define RFBI_READ		RFBI_REG(0x0058)
#define RFBI_STATUS		RFBI_REG(0x005c)

#define RFBI_CONFIG(n)		RFBI_REG(0x0060 + (n)*0x18)
#define RFBI_ONOFF_TIME(n)	RFBI_REG(0x0064 + (n)*0x18)
#define RFBI_CYCLE_TIME(n)	RFBI_REG(0x0068 + (n)*0x18)
#define RFBI_DATA_CYCLE1(n)	RFBI_REG(0x006c + (n)*0x18)
#define RFBI_DATA_CYCLE2(n)	RFBI_REG(0x0070 + (n)*0x18)
#define RFBI_DATA_CYCLE3(n)	RFBI_REG(0x0074 + (n)*0x18)

#define RFBI_VSYNC_WIDTH	RFBI_REG(0x0090)
#define RFBI_HSYNC_WIDTH	RFBI_REG(0x0094)

#define REG_FLD_MOD(idx, val, start, end) \
	rfbi_write_reg(idx, FLD_MOD(rfbi_read_reg(idx), val, start, end))

enum omap_rfbi_cycleformat {
	OMAP_DSS_RFBI_CYCLEFORMAT_1_1 = 0,
	OMAP_DSS_RFBI_CYCLEFORMAT_2_1 = 1,
	OMAP_DSS_RFBI_CYCLEFORMAT_3_1 = 2,
	OMAP_DSS_RFBI_CYCLEFORMAT_3_2 = 3,
};

enum omap_rfbi_datatype {
	OMAP_DSS_RFBI_DATATYPE_12 = 0,
	OMAP_DSS_RFBI_DATATYPE_16 = 1,
	OMAP_DSS_RFBI_DATATYPE_18 = 2,
	OMAP_DSS_RFBI_DATATYPE_24 = 3,
};

enum omap_rfbi_parallelmode {
	OMAP_DSS_RFBI_PARALLELMODE_8 = 0,
	OMAP_DSS_RFBI_PARALLELMODE_9 = 1,
	OMAP_DSS_RFBI_PARALLELMODE_12 = 2,
	OMAP_DSS_RFBI_PARALLELMODE_16 = 3,
};

static int rfbi_convert_timings(struct rfbi_timings *t);
static void rfbi_get_clk_info(u32 *clk_period, u32 *max_clk_div);

static struct {
	struct platform_device *pdev;
	void __iomem	*base;

	unsigned long	l4_khz;

	enum omap_rfbi_datatype datatype;
	enum omap_rfbi_parallelmode parallelmode;

	enum omap_rfbi_te_mode te_mode;
	int te_enabled;

	void (*framedone_callback)(void *data);
	void *framedone_callback_data;

	struct omap_dss_device *dssdev[2];

	struct semaphore bus_lock;
} rfbi;

static inline void rfbi_write_reg(const struct rfbi_reg idx, u32 val)
{
	__raw_writel(val, rfbi.base + idx.idx);
}

static inline u32 rfbi_read_reg(const struct rfbi_reg idx)
{
	return __raw_readl(rfbi.base + idx.idx);
}

static int rfbi_runtime_get(void)
{
	int r;

	DSSDBG("rfbi_runtime_get\n");

	r = pm_runtime_get_sync(&rfbi.pdev->dev);
	WARN_ON(r < 0);
	return r < 0 ? r : 0;
}

static void rfbi_runtime_put(void)
{
	int r;

	DSSDBG("rfbi_runtime_put\n");

	r = pm_runtime_put(&rfbi.pdev->dev);
	WARN_ON(r < 0);
}

void rfbi_bus_lock(void)
{
	down(&rfbi.bus_lock);
}
EXPORT_SYMBOL(rfbi_bus_lock);

void rfbi_bus_unlock(void)
{
	up(&rfbi.bus_lock);
}
EXPORT_SYMBOL(rfbi_bus_unlock);

void omap_rfbi_write_command(const void *buf, u32 len)
{
	switch (rfbi.parallelmode) {
	case OMAP_DSS_RFBI_PARALLELMODE_8:
	{
		const u8 *b = buf;
		for (; len; len--)
			rfbi_write_reg(RFBI_CMD, *b++);
		break;
	}

	case OMAP_DSS_RFBI_PARALLELMODE_16:
	{
		const u16 *w = buf;
		BUG_ON(len & 1);
		for (; len; len -= 2)
			rfbi_write_reg(RFBI_CMD, *w++);
		break;
	}

	case OMAP_DSS_RFBI_PARALLELMODE_9:
	case OMAP_DSS_RFBI_PARALLELMODE_12:
	default:
		BUG();
	}
}
EXPORT_SYMBOL(omap_rfbi_write_command);

void omap_rfbi_read_data(void *buf, u32 len)
{
	switch (rfbi.parallelmode) {
	case OMAP_DSS_RFBI_PARALLELMODE_8:
	{
		u8 *b = buf;
		for (; len; len--) {
			rfbi_write_reg(RFBI_READ, 0);
			*b++ = rfbi_read_reg(RFBI_READ);
		}
		break;
	}

	case OMAP_DSS_RFBI_PARALLELMODE_16:
	{
		u16 *w = buf;
		BUG_ON(len & ~1);
		for (; len; len -= 2) {
			rfbi_write_reg(RFBI_READ, 0);
			*w++ = rfbi_read_reg(RFBI_READ);
		}
		break;
	}

	case OMAP_DSS_RFBI_PARALLELMODE_9:
	case OMAP_DSS_RFBI_PARALLELMODE_12:
	default:
		BUG();
	}
}
EXPORT_SYMBOL(omap_rfbi_read_data);

void omap_rfbi_write_data(const void *buf, u32 len)
{
	switch (rfbi.parallelmode) {
	case OMAP_DSS_RFBI_PARALLELMODE_8:
	{
		const u8 *b = buf;
		for (; len; len--)
			rfbi_write_reg(RFBI_PARAM, *b++);
		break;
	}

	case OMAP_DSS_RFBI_PARALLELMODE_16:
	{
		const u16 *w = buf;
		BUG_ON(len & 1);
		for (; len; len -= 2)
			rfbi_write_reg(RFBI_PARAM, *w++);
		break;
	}

	case OMAP_DSS_RFBI_PARALLELMODE_9:
	case OMAP_DSS_RFBI_PARALLELMODE_12:
	default:
		BUG();

	}
}
EXPORT_SYMBOL(omap_rfbi_write_data);

void omap_rfbi_write_pixels(const void __iomem *buf, int scr_width,
		u16 x, u16 y,
		u16 w, u16 h)
{
	int start_offset = scr_width * y + x;
	int horiz_offset = scr_width - w;
	int i;

	if (rfbi.datatype == OMAP_DSS_RFBI_DATATYPE_16 &&
	   rfbi.parallelmode == OMAP_DSS_RFBI_PARALLELMODE_8) {
		const u16 __iomem *pd = buf;
		pd += start_offset;

		for (; h; --h) {
			for (i = 0; i < w; ++i) {
				const u8 __iomem *b = (const u8 __iomem *)pd;
				rfbi_write_reg(RFBI_PARAM, __raw_readb(b+1));
				rfbi_write_reg(RFBI_PARAM, __raw_readb(b+0));
				++pd;
			}
			pd += horiz_offset;
		}
	} else if (rfbi.datatype == OMAP_DSS_RFBI_DATATYPE_24 &&
	   rfbi.parallelmode == OMAP_DSS_RFBI_PARALLELMODE_8) {
		const u32 __iomem *pd = buf;
		pd += start_offset;

		for (; h; --h) {
			for (i = 0; i < w; ++i) {
				const u8 __iomem *b = (const u8 __iomem *)pd;
				rfbi_write_reg(RFBI_PARAM, __raw_readb(b+2));
				rfbi_write_reg(RFBI_PARAM, __raw_readb(b+1));
				rfbi_write_reg(RFBI_PARAM, __raw_readb(b+0));
				++pd;
			}
			pd += horiz_offset;
		}
	} else if (rfbi.datatype == OMAP_DSS_RFBI_DATATYPE_16 &&
	   rfbi.parallelmode == OMAP_DSS_RFBI_PARALLELMODE_16) {
		const u16 __iomem *pd = buf;
		pd += start_offset;

		for (; h; --h) {
			for (i = 0; i < w; ++i) {
				rfbi_write_reg(RFBI_PARAM, __raw_readw(pd));
				++pd;
			}
			pd += horiz_offset;
		}
	} else {
		BUG();
	}
}
EXPORT_SYMBOL(omap_rfbi_write_pixels);

static void rfbi_transfer_area(struct omap_dss_device *dssdev, u16 width,
		u16 height, void (*callback)(void *data), void *data)
{
	u32 l;

	/*BUG_ON(callback == 0);*/
	BUG_ON(rfbi.framedone_callback != NULL);

	DSSDBG("rfbi_transfer_area %dx%d\n", width, height);

	dispc_mgr_set_lcd_size(dssdev->manager->id, width, height);

	dispc_mgr_enable(dssdev->manager->id, true);

	rfbi.framedone_callback = callback;
	rfbi.framedone_callback_data = data;

	rfbi_write_reg(RFBI_PIXEL_CNT, width * height);

	l = rfbi_read_reg(RFBI_CONTROL);
	l = FLD_MOD(l, 1, 0, 0); /* enable */
	if (!rfbi.te_enabled)
		l = FLD_MOD(l, 1, 4, 4); /* ITE */

	rfbi_write_reg(RFBI_CONTROL, l);
}

static void framedone_callback(void *data, u32 mask)
{
	void (*callback)(void *data);

	DSSDBG("FRAMEDONE\n");

	REG_FLD_MOD(RFBI_CONTROL, 0, 0, 0);

	callback = rfbi.framedone_callback;
	rfbi.framedone_callback = NULL;

	if (callback != NULL)
		callback(rfbi.framedone_callback_data);
}

#if 1 /* VERBOSE */
static void rfbi_print_timings(void)
{
	u32 l;
	u32 time;

	l = rfbi_read_reg(RFBI_CONFIG(0));
	time = 1000000000 / rfbi.l4_khz;
	if (l & (1 << 4))
		time *= 2;

	DSSDBG("Tick time %u ps\n", time);
	l = rfbi_read_reg(RFBI_ONOFF_TIME(0));
	DSSDBG("CSONTIME %d, CSOFFTIME %d, WEONTIME %d, WEOFFTIME %d, "
		"REONTIME %d, REOFFTIME %d\n",
		l & 0x0f, (l >> 4) & 0x3f, (l >> 10) & 0x0f, (l >> 14) & 0x3f,
		(l >> 20) & 0x0f, (l >> 24) & 0x3f);

	l = rfbi_read_reg(RFBI_CYCLE_TIME(0));
	DSSDBG("WECYCLETIME %d, RECYCLETIME %d, CSPULSEWIDTH %d, "
		"ACCESSTIME %d\n",
		(l & 0x3f), (l >> 6) & 0x3f, (l >> 12) & 0x3f,
		(l >> 22) & 0x3f);
}
#else
static void rfbi_print_timings(void) {}
#endif




static u32 extif_clk_period;

static inline unsigned long round_to_extif_ticks(unsigned long ps, int div)
{
	int bus_tick = extif_clk_period * div;
	return (ps + bus_tick - 1) / bus_tick * bus_tick;
}

static int calc_reg_timing(struct rfbi_timings *t, int div)
{
	t->clk_div = div;

	t->cs_on_time = round_to_extif_ticks(t->cs_on_time, div);

	t->we_on_time = round_to_extif_ticks(t->we_on_time, div);
	t->we_off_time = round_to_extif_ticks(t->we_off_time, div);
	t->we_cycle_time = round_to_extif_ticks(t->we_cycle_time, div);

	t->re_on_time = round_to_extif_ticks(t->re_on_time, div);
	t->re_off_time = round_to_extif_ticks(t->re_off_time, div);
	t->re_cycle_time = round_to_extif_ticks(t->re_cycle_time, div);

	t->access_time = round_to_extif_ticks(t->access_time, div);
	t->cs_off_time = round_to_extif_ticks(t->cs_off_time, div);
	t->cs_pulse_width = round_to_extif_ticks(t->cs_pulse_width, div);

	DSSDBG("[reg]cson %d csoff %d reon %d reoff %d\n",
	       t->cs_on_time, t->cs_off_time, t->re_on_time, t->re_off_time);
	DSSDBG("[reg]weon %d weoff %d recyc %d wecyc %d\n",
	       t->we_on_time, t->we_off_time, t->re_cycle_time,
	       t->we_cycle_time);
	DSSDBG("[reg]rdaccess %d cspulse %d\n",
	       t->access_time, t->cs_pulse_width);

	return rfbi_convert_timings(t);
}

static int calc_extif_timings(struct rfbi_timings *t)
{
	u32 max_clk_div;
	int div;

	rfbi_get_clk_info(&extif_clk_period, &max_clk_div);
	for (div = 1; div <= max_clk_div; div++) {
		if (calc_reg_timing(t, div) == 0)
			break;
	}

	if (div <= max_clk_div)
		return 0;

	DSSERR("can't setup timings\n");
	return -1;
}


static void rfbi_set_timings(int rfbi_module, struct rfbi_timings *t)
{
	int r;

	if (!t->converted) {
		r = calc_extif_timings(t);
		if (r < 0)
			DSSERR("Failed to calc timings\n");
	}

	BUG_ON(!t->converted);

	rfbi_write_reg(RFBI_ONOFF_TIME(rfbi_module), t->tim[0]);
	rfbi_write_reg(RFBI_CYCLE_TIME(rfbi_module), t->tim[1]);

	/* TIMEGRANULARITY */
	REG_FLD_MOD(RFBI_CONFIG(rfbi_module),
		    (t->tim[2] ? 1 : 0), 4, 4);

	rfbi_print_timings();
}

static int ps_to_rfbi_ticks(int time, int div)
{
	unsigned long tick_ps;
	int ret;

	/* Calculate in picosecs to yield more exact results */
	tick_ps = 1000000000 / (rfbi.l4_khz) * div;

	ret = (time + tick_ps - 1) / tick_ps;

	return ret;
}

static void rfbi_get_clk_info(u32 *clk_period, u32 *max_clk_div)
{
	*clk_period = 1000000000 / rfbi.l4_khz;
	*max_clk_div = 2;
}

static int rfbi_convert_timings(struct rfbi_timings *t)
{
	u32 l;
	int reon, reoff, weon, weoff, cson, csoff, cs_pulse;
	int actim, recyc, wecyc;
	int div = t->clk_div;

	if (div <= 0 || div > 2)
		return -1;

	/* Make sure that after conversion it still holds that:
	 * weoff > weon, reoff > reon, recyc >= reoff, wecyc >= weoff,
	 * csoff > cson, csoff >= max(weoff, reoff), actim > reon
	 */
	weon = ps_to_rfbi_ticks(t->we_on_time, div);
	weoff = ps_to_rfbi_ticks(t->we_off_time, div);
	if (weoff <= weon)
		weoff = weon + 1;
	if (weon > 0x0f)
		return -1;
	if (weoff > 0x3f)
		return -1;

	reon = ps_to_rfbi_ticks(t->re_on_time, div);
	reoff = ps_to_rfbi_ticks(t->re_off_time, div);
	if (reoff <= reon)
		reoff = reon + 1;
	if (reon > 0x0f)
		return -1;
	if (reoff > 0x3f)
		return -1;

	cson = ps_to_rfbi_ticks(t->cs_on_time, div);
	csoff = ps_to_rfbi_ticks(t->cs_off_time, div);
	if (csoff <= cson)
		csoff = cson + 1;
	if (csoff < max(weoff, reoff))
		csoff = max(weoff, reoff);
	if (cson > 0x0f)
		return -1;
	if (csoff > 0x3f)
		return -1;

	l =  cson;
	l |= csoff << 4;
	l |= weon  << 10;
	l |= weoff << 14;
	l |= reon  << 20;
	l |= reoff << 24;

	t->tim[0] = l;

	actim = ps_to_rfbi_ticks(t->access_time, div);
	if (actim <= reon)
		actim = reon + 1;
	if (actim > 0x3f)
		return -1;

	wecyc = ps_to_rfbi_ticks(t->we_cycle_time, div);
	if (wecyc < weoff)
		wecyc = weoff;
	if (wecyc > 0x3f)
		return -1;

	recyc = ps_to_rfbi_ticks(t->re_cycle_time, div);
	if (recyc < reoff)
		recyc = reoff;
	if (recyc > 0x3f)
		return -1;

	cs_pulse = ps_to_rfbi_ticks(t->cs_pulse_width, div);
	if (cs_pulse > 0x3f)
		return -1;

	l =  wecyc;
	l |= recyc    << 6;
	l |= cs_pulse << 12;
	l |= actim    << 22;

	t->tim[1] = l;

	t->tim[2] = div - 1;

	t->converted = 1;

	return 0;
}

/* xxx FIX module selection missing */
int omap_rfbi_setup_te(enum omap_rfbi_te_mode mode,
			     unsigned hs_pulse_time, unsigned vs_pulse_time,
			     int hs_pol_inv, int vs_pol_inv, int extif_div)
{
	int hs, vs;
	int min;
	u32 l;

	hs = ps_to_rfbi_ticks(hs_pulse_time, 1);
	vs = ps_to_rfbi_ticks(vs_pulse_time, 1);
	if (hs < 2)
		return -EDOM;
	if (mode == OMAP_DSS_RFBI_TE_MODE_2)
		min = 2;
	else /* OMAP_DSS_RFBI_TE_MODE_1 */
		min = 4;
	if (vs < min)
		return -EDOM;
	if (vs == hs)
		return -EINVAL;
	rfbi.te_mode = mode;
	DSSDBG("setup_te: mode %d hs %d vs %d hs_inv %d vs_inv %d\n",
		mode, hs, vs, hs_pol_inv, vs_pol_inv);

	rfbi_write_reg(RFBI_HSYNC_WIDTH, hs);
	rfbi_write_reg(RFBI_VSYNC_WIDTH, vs);

	l = rfbi_read_reg(RFBI_CONFIG(0));
	if (hs_pol_inv)
		l &= ~(1 << 21);
	else
		l |= 1 << 21;
	if (vs_pol_inv)
		l &= ~(1 << 20);
	else
		l |= 1 << 20;

	return 0;
}
EXPORT_SYMBOL(omap_rfbi_setup_te);

/* xxx FIX module selection missing */
int omap_rfbi_enable_te(bool enable, unsigned line)
{
	u32 l;

	DSSDBG("te %d line %d mode %d\n", enable, line, rfbi.te_mode);
	if (line > (1 << 11) - 1)
		return -EINVAL;

	l = rfbi_read_reg(RFBI_CONFIG(0));
	l &= ~(0x3 << 2);
	if (enable) {
		rfbi.te_enabled = 1;
		l |= rfbi.te_mode << 2;
	} else
		rfbi.te_enabled = 0;
	rfbi_write_reg(RFBI_CONFIG(0), l);
	rfbi_write_reg(RFBI_LINE_NUMBER, line);

	return 0;
}
EXPORT_SYMBOL(omap_rfbi_enable_te);

static int rfbi_configure(int rfbi_module, int bpp, int lines)
{
	u32 l;
	int cycle1 = 0, cycle2 = 0, cycle3 = 0;
	enum omap_rfbi_cycleformat cycleformat;
	enum omap_rfbi_datatype datatype;
	enum omap_rfbi_parallelmode parallelmode;

	switch (bpp) {
	case 12:
		datatype = OMAP_DSS_RFBI_DATATYPE_12;
		break;
	case 16:
		datatype = OMAP_DSS_RFBI_DATATYPE_16;
		break;
	case 18:
		datatype = OMAP_DSS_RFBI_DATATYPE_18;
		break;
	case 24:
		datatype = OMAP_DSS_RFBI_DATATYPE_24;
		break;
	default:
		BUG();
		return 1;
	}
	rfbi.datatype = datatype;

	switch (lines) {
	case 8:
		parallelmode = OMAP_DSS_RFBI_PARALLELMODE_8;
		break;
	case 9:
		parallelmode = OMAP_DSS_RFBI_PARALLELMODE_9;
		break;
	case 12:
		parallelmode = OMAP_DSS_RFBI_PARALLELMODE_12;
		break;
	case 16:
		parallelmode = OMAP_DSS_RFBI_PARALLELMODE_16;
		break;
	default:
		BUG();
		return 1;
	}
	rfbi.parallelmode = parallelmode;

	if ((bpp % lines) == 0) {
		switch (bpp / lines) {
		case 1:
			cycleformat = OMAP_DSS_RFBI_CYCLEFORMAT_1_1;
			break;
		case 2:
			cycleformat = OMAP_DSS_RFBI_CYCLEFORMAT_2_1;
			break;
		case 3:
			cycleformat = OMAP_DSS_RFBI_CYCLEFORMAT_3_1;
			break;
		default:
			BUG();
			return 1;
		}
	} else if ((2 * bpp % lines) == 0) {
		if ((2 * bpp / lines) == 3)
			cycleformat = OMAP_DSS_RFBI_CYCLEFORMAT_3_2;
		else {
			BUG();
			return 1;
		}
	} else {
		BUG();
		return 1;
	}

	switch (cycleformat) {
	case OMAP_DSS_RFBI_CYCLEFORMAT_1_1:
		cycle1 = lines;
		break;

	case OMAP_DSS_RFBI_CYCLEFORMAT_2_1:
		cycle1 = lines;
		cycle2 = lines;
		break;

	case OMAP_DSS_RFBI_CYCLEFORMAT_3_1:
		cycle1 = lines;
		cycle2 = lines;
		cycle3 = lines;
		break;

	case OMAP_DSS_RFBI_CYCLEFORMAT_3_2:
		cycle1 = lines;
		cycle2 = (lines / 2) | ((lines / 2) << 16);
		cycle3 = (lines << 16);
		break;
	}

	REG_FLD_MOD(RFBI_CONTROL, 0, 3, 2); /* clear CS */

	l = 0;
	l |= FLD_VAL(parallelmode, 1, 0);
	l |= FLD_VAL(0, 3, 2);		/* TRIGGERMODE: ITE */
	l |= FLD_VAL(0, 4, 4);		/* TIMEGRANULARITY */
	l |= FLD_VAL(datatype, 6, 5);
	/* l |= FLD_VAL(2, 8, 7); */	/* L4FORMAT, 2pix/L4 */
	l |= FLD_VAL(0, 8, 7);	/* L4FORMAT, 1pix/L4 */
	l |= FLD_VAL(cycleformat, 10, 9);
	l |= FLD_VAL(0, 12, 11);	/* UNUSEDBITS */
	l |= FLD_VAL(0, 16, 16);	/* A0POLARITY */
	l |= FLD_VAL(0, 17, 17);	/* REPOLARITY */
	l |= FLD_VAL(0, 18, 18);	/* WEPOLARITY */
	l |= FLD_VAL(0, 19, 19);	/* CSPOLARITY */
	l |= FLD_VAL(1, 20, 20);	/* TE_VSYNC_POLARITY */
	l |= FLD_VAL(1, 21, 21);	/* HSYNCPOLARITY */
	rfbi_write_reg(RFBI_CONFIG(rfbi_module), l);

	rfbi_write_reg(RFBI_DATA_CYCLE1(rfbi_module), cycle1);
	rfbi_write_reg(RFBI_DATA_CYCLE2(rfbi_module), cycle2);
	rfbi_write_reg(RFBI_DATA_CYCLE3(rfbi_module), cycle3);


	l = rfbi_read_reg(RFBI_CONTROL);
	l = FLD_MOD(l, rfbi_module+1, 3, 2); /* Select CSx */
	l = FLD_MOD(l, 0, 1, 1); /* clear bypass */
	rfbi_write_reg(RFBI_CONTROL, l);


	DSSDBG("RFBI config: bpp %d, lines %d, cycles: 0x%x 0x%x 0x%x\n",
	       bpp, lines, cycle1, cycle2, cycle3);

	return 0;
}

int omap_rfbi_configure(struct omap_dss_device *dssdev, int pixel_size,
		int data_lines)
{
	return rfbi_configure(dssdev->phy.rfbi.channel, pixel_size, data_lines);
}
EXPORT_SYMBOL(omap_rfbi_configure);

int omap_rfbi_prepare_update(struct omap_dss_device *dssdev,
		u16 *x, u16 *y, u16 *w, u16 *h)
{
	u16 dw, dh;

	dssdev->driver->get_resolution(dssdev, &dw, &dh);

	if  (*x > dw || *y > dh)
		return -EINVAL;

	if (*x + *w > dw)
		return -EINVAL;

	if (*y + *h > dh)
		return -EINVAL;

	if (*w == 1)
		return -EINVAL;

	if (*w == 0 || *h == 0)
		return -EINVAL;

	dispc_mgr_set_lcd_size(dssdev->manager->id, *w, *h);

	return 0;
}
EXPORT_SYMBOL(omap_rfbi_prepare_update);

int omap_rfbi_update(struct omap_dss_device *dssdev,
		u16 x, u16 y, u16 w, u16 h,
		void (*callback)(void *), void *data)
{
	rfbi_transfer_area(dssdev, w, h, callback, data);
	return 0;
}
EXPORT_SYMBOL(omap_rfbi_update);

void rfbi_dump_regs(struct seq_file *s)
{
#define DUMPREG(r) seq_printf(s, "%-35s %08x\n", #r, rfbi_read_reg(r))

	if (rfbi_runtime_get())
		return;

	DUMPREG(RFBI_REVISION);
	DUMPREG(RFBI_SYSCONFIG);
	DUMPREG(RFBI_SYSSTATUS);
	DUMPREG(RFBI_CONTROL);
	DUMPREG(RFBI_PIXEL_CNT);
	DUMPREG(RFBI_LINE_NUMBER);
	DUMPREG(RFBI_CMD);
	DUMPREG(RFBI_PARAM);
	DUMPREG(RFBI_DATA);
	DUMPREG(RFBI_READ);
	DUMPREG(RFBI_STATUS);

	DUMPREG(RFBI_CONFIG(0));
	DUMPREG(RFBI_ONOFF_TIME(0));
	DUMPREG(RFBI_CYCLE_TIME(0));
	DUMPREG(RFBI_DATA_CYCLE1(0));
	DUMPREG(RFBI_DATA_CYCLE2(0));
	DUMPREG(RFBI_DATA_CYCLE3(0));

	DUMPREG(RFBI_CONFIG(1));
	DUMPREG(RFBI_ONOFF_TIME(1));
	DUMPREG(RFBI_CYCLE_TIME(1));
	DUMPREG(RFBI_DATA_CYCLE1(1));
	DUMPREG(RFBI_DATA_CYCLE2(1));
	DUMPREG(RFBI_DATA_CYCLE3(1));

	DUMPREG(RFBI_VSYNC_WIDTH);
	DUMPREG(RFBI_HSYNC_WIDTH);

	rfbi_runtime_put();
#undef DUMPREG
}

int omapdss_rfbi_display_enable(struct omap_dss_device *dssdev)
{
	int r;

	if (dssdev->manager == NULL) {
		DSSERR("failed to enable display: no manager\n");
		return -ENODEV;
	}

	r = rfbi_runtime_get();
	if (r)
		return r;

	r = omap_dss_start_device(dssdev);
	if (r) {
		DSSERR("failed to start device\n");
		goto err0;
	}

	r = omap_dispc_register_isr(framedone_callback, NULL,
			DISPC_IRQ_FRAMEDONE);
	if (r) {
		DSSERR("can't get FRAMEDONE irq\n");
		goto err1;
	}

	dispc_mgr_set_lcd_display_type(dssdev->manager->id,
			OMAP_DSS_LCD_DISPLAY_TFT);

	dispc_mgr_set_io_pad_mode(DSS_IO_PAD_MODE_RFBI);
	dispc_mgr_enable_stallmode(dssdev->manager->id, true);

	dispc_mgr_set_tft_data_lines(dssdev->manager->id, dssdev->ctrl.pixel_size);

	rfbi_configure(dssdev->phy.rfbi.channel,
			       dssdev->ctrl.pixel_size,
			       dssdev->phy.rfbi.data_lines);

	rfbi_set_timings(dssdev->phy.rfbi.channel,
			 &dssdev->ctrl.rfbi_timings);


	return 0;
err1:
	omap_dss_stop_device(dssdev);
err0:
	rfbi_runtime_put();
	return r;
}
EXPORT_SYMBOL(omapdss_rfbi_display_enable);

void omapdss_rfbi_display_disable(struct omap_dss_device *dssdev)
{
	omap_dispc_unregister_isr(framedone_callback, NULL,
			DISPC_IRQ_FRAMEDONE);
	omap_dss_stop_device(dssdev);

	rfbi_runtime_put();
}
EXPORT_SYMBOL(omapdss_rfbi_display_disable);

int rfbi_init_display(struct omap_dss_device *dssdev)
{
	rfbi.dssdev[dssdev->phy.rfbi.channel] = dssdev;
	dssdev->caps = OMAP_DSS_DISPLAY_CAP_MANUAL_UPDATE;
	return 0;
}

/* RFBI HW IP initialisation */
static int omap_rfbihw_probe(struct platform_device *pdev)
{
	u32 rev;
	struct resource *rfbi_mem;
	struct clk *clk;
	int r;

	rfbi.pdev = pdev;

	sema_init(&rfbi.bus_lock, 1);

	rfbi_mem = platform_get_resource(rfbi.pdev, IORESOURCE_MEM, 0);
	if (!rfbi_mem) {
		DSSERR("can't get IORESOURCE_MEM RFBI\n");
		r = -EINVAL;
		goto err_ioremap;
	}
	rfbi.base = ioremap(rfbi_mem->start, resource_size(rfbi_mem));
	if (!rfbi.base) {
		DSSERR("can't ioremap RFBI\n");
		r = -ENOMEM;
		goto err_ioremap;
	}

	pm_runtime_enable(&pdev->dev);

	r = rfbi_runtime_get();
	if (r)
		goto err_get_rfbi;

	msleep(10);

	clk = clk_get(&pdev->dev, "ick");
	if (IS_ERR(clk)) {
		DSSERR("can't get ick\n");
		r = PTR_ERR(clk);
		goto err_get_ick;
	}

	rfbi.l4_khz = clk_get_rate(clk) / 1000;

	clk_put(clk);

	rev = rfbi_read_reg(RFBI_REVISION);
	dev_dbg(&pdev->dev, "OMAP RFBI rev %d.%d\n",
	       FLD_GET(rev, 7, 4), FLD_GET(rev, 3, 0));

	rfbi_runtime_put();

	return 0;

err_get_ick:
	rfbi_runtime_put();
err_get_rfbi:
	pm_runtime_disable(&pdev->dev);
	iounmap(rfbi.base);
err_ioremap:
	return r;
}

static int omap_rfbihw_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);
	iounmap(rfbi.base);
	return 0;
}

static int rfbi_runtime_suspend(struct device *dev)
{
	dispc_runtime_put();
	dss_runtime_put();

	return 0;
}

static int rfbi_runtime_resume(struct device *dev)
{
	int r;

	r = dss_runtime_get();
	if (r < 0)
		goto err_get_dss;

	r = dispc_runtime_get();
	if (r < 0)
		goto err_get_dispc;

	return 0;

err_get_dispc:
	dss_runtime_put();
err_get_dss:
	return r;
}

static const struct dev_pm_ops rfbi_pm_ops = {
	.runtime_suspend = rfbi_runtime_suspend,
	.runtime_resume = rfbi_runtime_resume,
};

static struct platform_driver omap_rfbihw_driver = {
	.probe          = omap_rfbihw_probe,
	.remove         = omap_rfbihw_remove,
	.driver         = {
		.name   = "omapdss_rfbi",
		.owner  = THIS_MODULE,
		.pm	= &rfbi_pm_ops,
	},
};

int rfbi_init_platform_driver(void)
{
	return platform_driver_register(&omap_rfbihw_driver);
}

void rfbi_uninit_platform_driver(void)
{
	return platform_driver_unregister(&omap_rfbihw_driver);
}
