/*
 * ntc_thermistor.c - NTC Thermistors
 *
 *  Copyright (C) 2010 Samsung Electronics
 *  MyungJoo Ham <myungjoo.ham@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/math64.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/of_device.h>

#include <linux/platform_data/ntc_thermistor.h>

#include <linux/iio/iio.h>
#include <linux/iio/machine.h>
#include <linux/iio/driver.h>
#include <linux/iio/consumer.h>

#include <linux/hwmon.h>

struct ntc_compensation {
	int		temp_c;
	unsigned int	ohm;
};

/*
 * Used as index in a zero-terminated array, holes not allowed so
 * that NTC_LAST is the first empty array entry.
 */
enum {
	NTC_B57330V2103,
	NTC_B57891S0103,
	NTC_NCP03WB473,
	NTC_NCP03WF104,
	NTC_NCP15WB473,
	NTC_NCP15WL333,
	NTC_NCP15XH103,
	NTC_NCP18WB473,
	NTC_NCP21WB473,
	NTC_LAST,
};

static const struct platform_device_id ntc_thermistor_id[] = {
	[NTC_B57330V2103]     = { "b57330v2103",     TYPE_B57330V2103 },
	[NTC_B57891S0103]     = { "b57891s0103",     TYPE_B57891S0103 },
	[NTC_NCP03WB473]      = { "ncp03wb473",      TYPE_NCPXXWB473 },
	[NTC_NCP03WF104]      = { "ncp03wf104",      TYPE_NCPXXWF104 },
	[NTC_NCP15WB473]      = { "ncp15wb473",      TYPE_NCPXXWB473 },
	[NTC_NCP15WL333]      = { "ncp15wl333",      TYPE_NCPXXWL333 },
	[NTC_NCP15XH103]      = { "ncp15xh103",      TYPE_NCPXXXH103 },
	[NTC_NCP18WB473]      = { "ncp18wb473",      TYPE_NCPXXWB473 },
	[NTC_NCP21WB473]      = { "ncp21wb473",      TYPE_NCPXXWB473 },
	[NTC_LAST]            = { },
};

/*
 * A compensation table should be sorted by the values of .ohm
 * in descending order.
 * The following compensation tables are from the specification of Murata NTC
 * Thermistors Datasheet
 */
static const struct ntc_compensation ncpXXwb473[] = {
	{ .temp_c	= -40, .ohm	= 1747920 },
	{ .temp_c	= -35, .ohm	= 1245428 },
	{ .temp_c	= -30, .ohm	= 898485 },
	{ .temp_c	= -25, .ohm	= 655802 },
	{ .temp_c	= -20, .ohm	= 483954 },
	{ .temp_c	= -15, .ohm	= 360850 },
	{ .temp_c	= -10, .ohm	= 271697 },
	{ .temp_c	= -5, .ohm	= 206463 },
	{ .temp_c	= 0, .ohm	= 158214 },
	{ .temp_c	= 5, .ohm	= 122259 },
	{ .temp_c	= 10, .ohm	= 95227 },
	{ .temp_c	= 15, .ohm	= 74730 },
	{ .temp_c	= 20, .ohm	= 59065 },
	{ .temp_c	= 25, .ohm	= 47000 },
	{ .temp_c	= 30, .ohm	= 37643 },
	{ .temp_c	= 35, .ohm	= 30334 },
	{ .temp_c	= 40, .ohm	= 24591 },
	{ .temp_c	= 45, .ohm	= 20048 },
	{ .temp_c	= 50, .ohm	= 16433 },
	{ .temp_c	= 55, .ohm	= 13539 },
	{ .temp_c	= 60, .ohm	= 11209 },
	{ .temp_c	= 65, .ohm	= 9328 },
	{ .temp_c	= 70, .ohm	= 7798 },
	{ .temp_c	= 75, .ohm	= 6544 },
	{ .temp_c	= 80, .ohm	= 5518 },
	{ .temp_c	= 85, .ohm	= 4674 },
	{ .temp_c	= 90, .ohm	= 3972 },
	{ .temp_c	= 95, .ohm	= 3388 },
	{ .temp_c	= 100, .ohm	= 2902 },
	{ .temp_c	= 105, .ohm	= 2494 },
	{ .temp_c	= 110, .ohm	= 2150 },
	{ .temp_c	= 115, .ohm	= 1860 },
	{ .temp_c	= 120, .ohm	= 1615 },
	{ .temp_c	= 125, .ohm	= 1406 },
};
static const struct ntc_compensation ncpXXwl333[] = {
	{ .temp_c	= -40, .ohm	= 1610154 },
	{ .temp_c	= -35, .ohm	= 1130850 },
	{ .temp_c	= -30, .ohm	= 802609 },
	{ .temp_c	= -25, .ohm	= 575385 },
	{ .temp_c	= -20, .ohm	= 416464 },
	{ .temp_c	= -15, .ohm	= 304219 },
	{ .temp_c	= -10, .ohm	= 224193 },
	{ .temp_c	= -5, .ohm	= 166623 },
	{ .temp_c	= 0, .ohm	= 124850 },
	{ .temp_c	= 5, .ohm	= 94287 },
	{ .temp_c	= 10, .ohm	= 71747 },
	{ .temp_c	= 15, .ohm	= 54996 },
	{ .temp_c	= 20, .ohm	= 42455 },
	{ .temp_c	= 25, .ohm	= 33000 },
	{ .temp_c	= 30, .ohm	= 25822 },
	{ .temp_c	= 35, .ohm	= 20335 },
	{ .temp_c	= 40, .ohm	= 16115 },
	{ .temp_c	= 45, .ohm	= 12849 },
	{ .temp_c	= 50, .ohm	= 10306 },
	{ .temp_c	= 55, .ohm	= 8314 },
	{ .temp_c	= 60, .ohm	= 6746 },
	{ .temp_c	= 65, .ohm	= 5503 },
	{ .temp_c	= 70, .ohm	= 4513 },
	{ .temp_c	= 75, .ohm	= 3721 },
	{ .temp_c	= 80, .ohm	= 3084 },
	{ .temp_c	= 85, .ohm	= 2569 },
	{ .temp_c	= 90, .ohm	= 2151 },
	{ .temp_c	= 95, .ohm	= 1809 },
	{ .temp_c	= 100, .ohm	= 1529 },
	{ .temp_c	= 105, .ohm	= 1299 },
	{ .temp_c	= 110, .ohm	= 1108 },
	{ .temp_c	= 115, .ohm	= 949 },
	{ .temp_c	= 120, .ohm	= 817 },
	{ .temp_c	= 125, .ohm	= 707 },
};

static const struct ntc_compensation ncpXXwf104[] = {
	{ .temp_c	= -40, .ohm	= 4397119 },
	{ .temp_c	= -35, .ohm	= 3088599 },
	{ .temp_c	= -30, .ohm	= 2197225 },
	{ .temp_c	= -25, .ohm	= 1581881 },
	{ .temp_c	= -20, .ohm	= 1151037 },
	{ .temp_c	= -15, .ohm	= 846579 },
	{ .temp_c	= -10, .ohm	= 628988 },
	{ .temp_c	= -5, .ohm	= 471632 },
	{ .temp_c	= 0, .ohm	= 357012 },
	{ .temp_c	= 5, .ohm	= 272500 },
	{ .temp_c	= 10, .ohm	= 209710 },
	{ .temp_c	= 15, .ohm	= 162651 },
	{ .temp_c	= 20, .ohm	= 127080 },
	{ .temp_c	= 25, .ohm	= 100000 },
	{ .temp_c	= 30, .ohm	= 79222 },
	{ .temp_c	= 35, .ohm	= 63167 },
	{ .temp_c	= 40, .ohm	= 50677 },
	{ .temp_c	= 45, .ohm	= 40904 },
	{ .temp_c	= 50, .ohm	= 33195 },
	{ .temp_c	= 55, .ohm	= 27091 },
	{ .temp_c	= 60, .ohm	= 22224 },
	{ .temp_c	= 65, .ohm	= 18323 },
	{ .temp_c	= 70, .ohm	= 15184 },
	{ .temp_c	= 75, .ohm	= 12635 },
	{ .temp_c	= 80, .ohm	= 10566 },
	{ .temp_c	= 85, .ohm	= 8873 },
	{ .temp_c	= 90, .ohm	= 7481 },
	{ .temp_c	= 95, .ohm	= 6337 },
	{ .temp_c	= 100, .ohm	= 5384 },
	{ .temp_c	= 105, .ohm	= 4594 },
	{ .temp_c	= 110, .ohm	= 3934 },
	{ .temp_c	= 115, .ohm	= 3380 },
	{ .temp_c	= 120, .ohm	= 2916 },
	{ .temp_c	= 125, .ohm	= 2522 },
};

static const struct ntc_compensation ncpXXxh103[] = {
	{ .temp_c	= -40, .ohm	= 247565 },
	{ .temp_c	= -35, .ohm	= 181742 },
	{ .temp_c	= -30, .ohm	= 135128 },
	{ .temp_c	= -25, .ohm	= 101678 },
	{ .temp_c	= -20, .ohm	= 77373 },
	{ .temp_c	= -15, .ohm	= 59504 },
	{ .temp_c	= -10, .ohm	= 46222 },
	{ .temp_c	= -5, .ohm	= 36244 },
	{ .temp_c	= 0, .ohm	= 28674 },
	{ .temp_c	= 5, .ohm	= 22878 },
	{ .temp_c	= 10, .ohm	= 18399 },
	{ .temp_c	= 15, .ohm	= 14910 },
	{ .temp_c	= 20, .ohm	= 12169 },
	{ .temp_c	= 25, .ohm	= 10000 },
	{ .temp_c	= 30, .ohm	= 8271 },
	{ .temp_c	= 35, .ohm	= 6883 },
	{ .temp_c	= 40, .ohm	= 5762 },
	{ .temp_c	= 45, .ohm	= 4851 },
	{ .temp_c	= 50, .ohm	= 4105 },
	{ .temp_c	= 55, .ohm	= 3492 },
	{ .temp_c	= 60, .ohm	= 2985 },
	{ .temp_c	= 65, .ohm	= 2563 },
	{ .temp_c	= 70, .ohm	= 2211 },
	{ .temp_c	= 75, .ohm	= 1915 },
	{ .temp_c	= 80, .ohm	= 1666 },
	{ .temp_c	= 85, .ohm	= 1454 },
	{ .temp_c	= 90, .ohm	= 1275 },
	{ .temp_c	= 95, .ohm	= 1121 },
	{ .temp_c	= 100, .ohm	= 990 },
	{ .temp_c	= 105, .ohm	= 876 },
	{ .temp_c	= 110, .ohm	= 779 },
	{ .temp_c	= 115, .ohm	= 694 },
	{ .temp_c	= 120, .ohm	= 620 },
	{ .temp_c	= 125, .ohm	= 556 },
};

/*
 * The following compensation tables are from the specifications in EPCOS NTC
 * Thermistors Datasheets
 */
static const struct ntc_compensation b57330v2103[] = {
	{ .temp_c	= -40, .ohm	= 190030 },
	{ .temp_c	= -35, .ohm	= 145360 },
	{ .temp_c	= -30, .ohm	= 112060 },
	{ .temp_c	= -25, .ohm	= 87041 },
	{ .temp_c	= -20, .ohm	= 68104 },
	{ .temp_c	= -15, .ohm	= 53665 },
	{ .temp_c	= -10, .ohm	= 42576 },
	{ .temp_c	= -5, .ohm	= 34001 },
	{ .temp_c	= 0, .ohm	= 27326 },
	{ .temp_c	= 5, .ohm	= 22096 },
	{ .temp_c	= 10, .ohm	= 17973 },
	{ .temp_c	= 15, .ohm	= 14703 },
	{ .temp_c	= 20, .ohm	= 12090 },
	{ .temp_c	= 25, .ohm	= 10000 },
	{ .temp_c	= 30, .ohm	= 8311 },
	{ .temp_c	= 35, .ohm	= 6941 },
	{ .temp_c	= 40, .ohm	= 5825 },
	{ .temp_c	= 45, .ohm	= 4911 },
	{ .temp_c	= 50, .ohm	= 4158 },
	{ .temp_c	= 55, .ohm	= 3536 },
	{ .temp_c	= 60, .ohm	= 3019 },
	{ .temp_c	= 65, .ohm	= 2588 },
	{ .temp_c	= 70, .ohm	= 2227 },
	{ .temp_c	= 75, .ohm	= 1924 },
	{ .temp_c	= 80, .ohm	= 1668 },
	{ .temp_c	= 85, .ohm	= 1451 },
	{ .temp_c	= 90, .ohm	= 1266 },
	{ .temp_c	= 95, .ohm	= 1108 },
	{ .temp_c	= 100, .ohm	= 973 },
	{ .temp_c	= 105, .ohm	= 857 },
	{ .temp_c	= 110, .ohm	= 757 },
	{ .temp_c	= 115, .ohm	= 671 },
	{ .temp_c	= 120, .ohm	= 596 },
	{ .temp_c	= 125, .ohm	= 531 },
};

static const struct ntc_compensation b57891s0103[] = {
	{ .temp_c	= -55.0, .ohm	= 878900 },
	{ .temp_c	= -50.0, .ohm	= 617590 },
	{ .temp_c	= -45.0, .ohm	= 439340 },
	{ .temp_c	= -40.0, .ohm	= 316180 },
	{ .temp_c	= -35.0, .ohm	= 230060 },
	{ .temp_c	= -30.0, .ohm	= 169150 },
	{ .temp_c	= -25.0, .ohm	= 125550 },
	{ .temp_c	= -20.0, .ohm	= 94143 },
	{ .temp_c	= -15.0, .ohm	= 71172 },
	{ .temp_c	= -10.0, .ohm	= 54308 },
	{ .temp_c	= -5.0, .ohm	= 41505 },
	{ .temp_c	= 0.0, .ohm	= 32014 },
	{ .temp_c	= 5.0, .ohm	= 25011 },
	{ .temp_c	= 10.0, .ohm	= 19691 },
	{ .temp_c	= 15.0, .ohm	= 15618 },
	{ .temp_c	= 20.0, .ohm	= 12474 },
	{ .temp_c	= 25.0, .ohm	= 10000 },
	{ .temp_c	= 30.0, .ohm	= 8080 },
	{ .temp_c	= 35.0, .ohm	= 6569 },
	{ .temp_c	= 40.0, .ohm	= 5372 },
	{ .temp_c	= 45.0, .ohm	= 4424 },
	{ .temp_c	= 50.0, .ohm	= 3661 },
	{ .temp_c	= 55.0, .ohm	= 3039 },
	{ .temp_c	= 60.0, .ohm	= 2536 },
	{ .temp_c	= 65.0, .ohm	= 2128 },
	{ .temp_c	= 70.0, .ohm	= 1794 },
	{ .temp_c	= 75.0, .ohm	= 1518 },
	{ .temp_c	= 80.0, .ohm	= 1290 },
	{ .temp_c	= 85.0, .ohm	= 1100 },
	{ .temp_c	= 90.0, .ohm	= 942 },
	{ .temp_c	= 95.0, .ohm	= 809 },
	{ .temp_c	= 100.0, .ohm	= 697 },
	{ .temp_c	= 105.0, .ohm	= 604 },
	{ .temp_c	= 110.0, .ohm	= 525 },
	{ .temp_c	= 115.0, .ohm	= 457 },
	{ .temp_c	= 120.0, .ohm	= 400 },
	{ .temp_c	= 125.0, .ohm	= 351 },
	{ .temp_c	= 130.0, .ohm	= 308 },
	{ .temp_c	= 135.0, .ohm	= 272 },
	{ .temp_c	= 140.0, .ohm	= 240 },
	{ .temp_c	= 145.0, .ohm	= 213 },
	{ .temp_c	= 150.0, .ohm	= 189 },
	{ .temp_c	= 155.0, .ohm	= 168 },
};

struct ntc_type {
	const struct ntc_compensation *comp;
	int n_comp;
};

#define NTC_TYPE(ntc, compensation) \
[(ntc)] = { .comp = (compensation), .n_comp = ARRAY_SIZE(compensation) }

static const struct ntc_type ntc_type[] = {
	NTC_TYPE(TYPE_B57330V2103, b57330v2103),
	NTC_TYPE(TYPE_B57891S0103, b57891s0103),
	NTC_TYPE(TYPE_NCPXXWB473,  ncpXXwb473),
	NTC_TYPE(TYPE_NCPXXWF104,  ncpXXwf104),
	NTC_TYPE(TYPE_NCPXXWL333,  ncpXXwl333),
	NTC_TYPE(TYPE_NCPXXXH103,  ncpXXxh103),
};

struct ntc_data {
	struct ntc_thermistor_platform_data *pdata;
	const struct ntc_compensation *comp;
	int n_comp;
};

#if defined(CONFIG_OF) && IS_ENABLED(CONFIG_IIO)
static int ntc_adc_iio_read(struct ntc_thermistor_platform_data *pdata)
{
	struct iio_channel *channel = pdata->chan;
	int raw, uv, ret;

	ret = iio_read_channel_raw(channel, &raw);
	if (ret < 0) {
		pr_err("read channel() error: %d\n", ret);
		return ret;
	}

	ret = iio_convert_raw_to_processed(channel, raw, &uv, 1000);
	if (ret < 0) {
		/* Assume 12 bit ADC with vref at pullup_uv */
		uv = (pdata->pullup_uv * (s64)raw) >> 12;
	}

	return uv;
}

static const struct of_device_id ntc_match[] = {
	{ .compatible = "epcos,b57330v2103",
		.data = &ntc_thermistor_id[NTC_B57330V2103]},
	{ .compatible = "epcos,b57891s0103",
		.data = &ntc_thermistor_id[NTC_B57891S0103] },
	{ .compatible = "murata,ncp03wb473",
		.data = &ntc_thermistor_id[NTC_NCP03WB473] },
	{ .compatible = "murata,ncp03wf104",
		.data = &ntc_thermistor_id[NTC_NCP03WF104] },
	{ .compatible = "murata,ncp15wb473",
		.data = &ntc_thermistor_id[NTC_NCP15WB473] },
	{ .compatible = "murata,ncp15wl333",
		.data = &ntc_thermistor_id[NTC_NCP15WL333] },
	{ .compatible = "murata,ncp15xh103",
		.data = &ntc_thermistor_id[NTC_NCP15XH103] },
	{ .compatible = "murata,ncp18wb473",
		.data = &ntc_thermistor_id[NTC_NCP18WB473] },
	{ .compatible = "murata,ncp21wb473",
		.data = &ntc_thermistor_id[NTC_NCP21WB473] },

	/* Usage of vendor name "ntc" is deprecated */
	{ .compatible = "ntc,ncp03wb473",
		.data = &ntc_thermistor_id[NTC_NCP03WB473] },
	{ .compatible = "ntc,ncp15wb473",
		.data = &ntc_thermistor_id[NTC_NCP15WB473] },
	{ .compatible = "ntc,ncp15wl333",
		.data = &ntc_thermistor_id[NTC_NCP15WL333] },
	{ .compatible = "ntc,ncp18wb473",
		.data = &ntc_thermistor_id[NTC_NCP18WB473] },
	{ .compatible = "ntc,ncp21wb473",
		.data = &ntc_thermistor_id[NTC_NCP21WB473] },
	{ },
};
MODULE_DEVICE_TABLE(of, ntc_match);

static struct ntc_thermistor_platform_data *
ntc_thermistor_parse_dt(struct device *dev)
{
	struct iio_channel *chan;
	enum iio_chan_type type;
	struct device_node *np = dev->of_node;
	struct ntc_thermistor_platform_data *pdata;
	int ret;

	if (!np)
		return NULL;

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return ERR_PTR(-ENOMEM);

	chan = devm_iio_channel_get(dev, NULL);
	if (IS_ERR(chan))
		return ERR_CAST(chan);

	ret = iio_get_channel_type(chan, &type);
	if (ret < 0)
		return ERR_PTR(ret);

	if (type != IIO_VOLTAGE)
		return ERR_PTR(-EINVAL);

	if (of_property_read_u32(np, "pullup-uv", &pdata->pullup_uv))
		return ERR_PTR(-ENODEV);
	if (of_property_read_u32(np, "pullup-ohm", &pdata->pullup_ohm))
		return ERR_PTR(-ENODEV);
	if (of_property_read_u32(np, "pulldown-ohm", &pdata->pulldown_ohm))
		return ERR_PTR(-ENODEV);

	if (of_find_property(np, "connected-positive", NULL))
		pdata->connect = NTC_CONNECTED_POSITIVE;
	else /* status change should be possible if not always on. */
		pdata->connect = NTC_CONNECTED_GROUND;

	pdata->chan = chan;
	pdata->read_uv = ntc_adc_iio_read;

	return pdata;
}
#else
static struct ntc_thermistor_platform_data *
ntc_thermistor_parse_dt(struct device *dev)
{
	return NULL;
}

#define ntc_match	NULL

#endif

static inline u64 div64_u64_safe(u64 dividend, u64 divisor)
{
	if (divisor == 0 && dividend == 0)
		return 0;
	if (divisor == 0)
		return UINT_MAX;
	return div64_u64(dividend, divisor);
}

static int get_ohm_of_thermistor(struct ntc_data *data, unsigned int uv)
{
	struct ntc_thermistor_platform_data *pdata = data->pdata;
	u32 puv = pdata->pullup_uv;
	u64 n, puo, pdo;
	puo = pdata->pullup_ohm;
	pdo = pdata->pulldown_ohm;

	if (uv == 0)
		return (pdata->connect == NTC_CONNECTED_POSITIVE) ?
			INT_MAX : 0;
	if (uv >= puv)
		return (pdata->connect == NTC_CONNECTED_POSITIVE) ?
			0 : INT_MAX;

	if (pdata->connect == NTC_CONNECTED_POSITIVE && puo == 0)
		n = div_u64(pdo * (puv - uv), uv);
	else if (pdata->connect == NTC_CONNECTED_GROUND && pdo == 0)
		n = div_u64(puo * uv, puv - uv);
	else if (pdata->connect == NTC_CONNECTED_POSITIVE)
		n = div64_u64_safe(pdo * puo * (puv - uv),
				puo * uv - pdo * (puv - uv));
	else
		n = div64_u64_safe(pdo * puo * uv, pdo * (puv - uv) - puo * uv);

	if (n > INT_MAX)
		n = INT_MAX;
	return n;
}

static void lookup_comp(struct ntc_data *data, unsigned int ohm,
			int *i_low, int *i_high)
{
	int start, end, mid;

	/*
	 * Handle special cases: Resistance is higher than or equal to
	 * resistance in first table entry, or resistance is lower or equal
	 * to resistance in last table entry.
	 * In these cases, return i_low == i_high, either pointing to the
	 * beginning or to the end of the table depending on the condition.
	 */
	if (ohm >= data->comp[0].ohm) {
		*i_low = 0;
		*i_high = 0;
		return;
	}
	if (ohm <= data->comp[data->n_comp - 1].ohm) {
		*i_low = data->n_comp - 1;
		*i_high = data->n_comp - 1;
		return;
	}

	/* Do a binary search on compensation table */
	start = 0;
	end = data->n_comp;
	while (start < end) {
		mid = start + (end - start) / 2;
		/*
		 * start <= mid < end
		 * data->comp[start].ohm > ohm >= data->comp[end].ohm
		 *
		 * We could check for "ohm == data->comp[mid].ohm" here, but
		 * that is a quite unlikely condition, and we would have to
		 * check again after updating start. Check it at the end instead
		 * for simplicity.
		 */
		if (ohm >= data->comp[mid].ohm) {
			end = mid;
		} else {
			start = mid + 1;
			/*
			 * ohm >= data->comp[start].ohm might be true here,
			 * since we set start to mid + 1. In that case, we are
			 * done. We could keep going, but the condition is quite
			 * likely to occur, so it is worth checking for it.
			 */
			if (ohm >= data->comp[start].ohm)
				end = start;
		}
		/*
		 * start <= end
		 * data->comp[start].ohm >= ohm >= data->comp[end].ohm
		 */
	}
	/*
	 * start == end
	 * ohm >= data->comp[end].ohm
	 */
	*i_low = end;
	if (ohm == data->comp[end].ohm)
		*i_high = end;
	else
		*i_high = end - 1;
}

static int get_temp_mc(struct ntc_data *data, unsigned int ohm)
{
	int low, high;
	int temp;

	lookup_comp(data, ohm, &low, &high);
	if (low == high) {
		/* Unable to use linear approximation */
		temp = data->comp[low].temp_c * 1000;
	} else {
		temp = data->comp[low].temp_c * 1000 +
			((data->comp[high].temp_c - data->comp[low].temp_c) *
			 1000 * ((int)ohm - (int)data->comp[low].ohm)) /
			((int)data->comp[high].ohm - (int)data->comp[low].ohm);
	}
	return temp;
}

static int ntc_thermistor_get_ohm(struct ntc_data *data)
{
	int read_uv;

	if (data->pdata->read_ohm)
		return data->pdata->read_ohm();

	if (data->pdata->read_uv) {
		read_uv = data->pdata->read_uv(data->pdata);
		if (read_uv < 0)
			return read_uv;
		return get_ohm_of_thermistor(data, read_uv);
	}
	return -EINVAL;
}

static int ntc_read(struct device *dev, enum hwmon_sensor_types type,
		    u32 attr, int channel, long *val)
{
	struct ntc_data *data = dev_get_drvdata(dev);
	int ohm;

	switch (type) {
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_input:
			ohm = ntc_thermistor_get_ohm(data);
			if (ohm < 0)
				return ohm;
			*val = get_temp_mc(data, ohm);
			return 0;
		case hwmon_temp_type:
			*val = 4;
			return 0;
		default:
			break;
		}
		break;
	default:
		break;
	}
	return -EINVAL;
}

static umode_t ntc_is_visible(const void *data, enum hwmon_sensor_types type,
			      u32 attr, int channel)
{
	if (type == hwmon_temp) {
		switch (attr) {
		case hwmon_temp_input:
		case hwmon_temp_type:
			return 0444;
		default:
			break;
		}
	}
	return 0;
}

static const u32 ntc_chip_config[] = {
	HWMON_C_REGISTER_TZ,
	0
};

static const struct hwmon_channel_info ntc_chip = {
	.type = hwmon_chip,
	.config = ntc_chip_config,
};

static const u32 ntc_temp_config[] = {
	HWMON_T_INPUT | HWMON_T_TYPE,
	0
};

static const struct hwmon_channel_info ntc_temp = {
	.type = hwmon_temp,
	.config = ntc_temp_config,
};

static const struct hwmon_channel_info *ntc_info[] = {
	&ntc_chip,
	&ntc_temp,
	NULL
};

static const struct hwmon_ops ntc_hwmon_ops = {
	.is_visible = ntc_is_visible,
	.read = ntc_read,
};

static const struct hwmon_chip_info ntc_chip_info = {
	.ops = &ntc_hwmon_ops,
	.info = ntc_info,
};

static int ntc_thermistor_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct of_device_id *of_id =
			of_match_device(of_match_ptr(ntc_match), dev);
	const struct platform_device_id *pdev_id;
	struct ntc_thermistor_platform_data *pdata;
	struct device *hwmon_dev;
	struct ntc_data *data;

	pdata = ntc_thermistor_parse_dt(dev);
	if (IS_ERR(pdata))
		return PTR_ERR(pdata);
	else if (pdata == NULL)
		pdata = dev_get_platdata(dev);

	if (!pdata) {
		dev_err(dev, "No platform init data supplied.\n");
		return -ENODEV;
	}

	/* Either one of the two is required. */
	if (!pdata->read_uv && !pdata->read_ohm) {
		dev_err(dev,
			"Both read_uv and read_ohm missing. Need either one of the two.\n");
		return -EINVAL;
	}

	if (pdata->read_uv && pdata->read_ohm) {
		dev_warn(dev,
			 "Only one of read_uv and read_ohm is needed; ignoring read_uv.\n");
		pdata->read_uv = NULL;
	}

	if (pdata->read_uv && (pdata->pullup_uv == 0 ||
				(pdata->pullup_ohm == 0 && pdata->connect ==
				 NTC_CONNECTED_GROUND) ||
				(pdata->pulldown_ohm == 0 && pdata->connect ==
				 NTC_CONNECTED_POSITIVE) ||
				(pdata->connect != NTC_CONNECTED_POSITIVE &&
				 pdata->connect != NTC_CONNECTED_GROUND))) {
		dev_err(dev, "Required data to use read_uv not supplied.\n");
		return -EINVAL;
	}

	data = devm_kzalloc(dev, sizeof(struct ntc_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	pdev_id = of_id ? of_id->data : platform_get_device_id(pdev);

	data->pdata = pdata;

	if (pdev_id->driver_data >= ARRAY_SIZE(ntc_type)) {
		dev_err(dev, "Unknown device type: %lu(%s)\n",
				pdev_id->driver_data, pdev_id->name);
		return -EINVAL;
	}

	data->comp   = ntc_type[pdev_id->driver_data].comp;
	data->n_comp = ntc_type[pdev_id->driver_data].n_comp;

	hwmon_dev = devm_hwmon_device_register_with_info(dev, pdev_id->name,
							 data, &ntc_chip_info,
							 NULL);
	if (IS_ERR(hwmon_dev)) {
		dev_err(dev, "unable to register as hwmon device.\n");
		return PTR_ERR(hwmon_dev);
	}

	dev_info(dev, "Thermistor type: %s successfully probed.\n",
		 pdev_id->name);

	return 0;
}

static struct platform_driver ntc_thermistor_driver = {
	.driver = {
		.name = "ntc-thermistor",
		.of_match_table = of_match_ptr(ntc_match),
	},
	.probe = ntc_thermistor_probe,
	.id_table = ntc_thermistor_id,
};

module_platform_driver(ntc_thermistor_driver);

MODULE_DESCRIPTION("NTC Thermistor Driver");
MODULE_AUTHOR("MyungJoo Ham <myungjoo.ham@samsung.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:ntc-thermistor");
