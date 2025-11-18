// SPDX-License-Identifier: GPL-2.0-only
/*
 * ADF4350/ADF4351 SPI Wideband Synthesizer driver
 *
 * Copyright 2012-2013 Analog Devices Inc.
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/spi/spi.h>
#include <linux/regulator/consumer.h>
#include <linux/err.h>
#include <linux/gcd.h>
#include <linux/gpio/consumer.h>
#include <asm/div64.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/frequency/adf4350.h>

enum {
	ADF4350_FREQ,
	ADF4350_FREQ_REFIN,
	ADF4350_FREQ_RESOLUTION,
	ADF4350_PWRDOWN,
};

struct adf4350_state {
	struct spi_device		*spi;
	struct gpio_desc		*lock_detect_gpiod;
	struct adf4350_platform_data	*pdata;
	struct clk			*clk;
	struct clk			*clkout;
	const char			*clk_out_name;
	struct clk_hw			hw;
	unsigned long			clkin;
	unsigned long			chspc; /* Channel Spacing */
	unsigned long			fpfd; /* Phase Frequency Detector */
	unsigned long			min_out_freq;
	unsigned			r0_fract;
	unsigned			r0_int;
	unsigned			r1_mod;
	unsigned			r4_rf_div_sel;
	unsigned long			regs[6];
	unsigned long			regs_hw[6];
	unsigned long long		freq_req;
	/*
	 * Lock to protect the state of the device from potential concurrent
	 * writes. The device is configured via a sequence of SPI writes,
	 * and this lock is meant to prevent the start of another sequence
	 * before another one has finished.
	 */
	struct mutex			lock;
	/*
	 * DMA (thus cache coherency maintenance) may require that
	 * transfer buffers live in their own cache lines.
	 */
	__be32				val __aligned(IIO_DMA_MINALIGN);
};

#define to_adf4350_state(_hw) container_of(_hw, struct adf4350_state, hw)

static struct adf4350_platform_data default_pdata = {
	.channel_spacing = 10000,
	.r2_user_settings = ADF4350_REG2_PD_POLARITY_POS |
			    ADF4350_REG2_CHARGE_PUMP_CURR_uA(2500),
	.r3_user_settings = ADF4350_REG3_12BIT_CLKDIV_MODE(0),
	.r4_user_settings = ADF4350_REG4_OUTPUT_PWR(3) |
			    ADF4350_REG4_MUTE_TILL_LOCK_EN,
};

static int adf4350_sync_config(struct adf4350_state *st)
{
	int ret, i, doublebuf = 0;

	for (i = ADF4350_REG5; i >= ADF4350_REG0; i--) {
		if ((st->regs_hw[i] != st->regs[i]) ||
			((i == ADF4350_REG0) && doublebuf)) {
			switch (i) {
			case ADF4350_REG1:
			case ADF4350_REG4:
				doublebuf = 1;
				break;
			}

			st->val  = cpu_to_be32(st->regs[i] | i);
			ret = spi_write(st->spi, &st->val, 4);
			if (ret < 0)
				return ret;
			st->regs_hw[i] = st->regs[i];
			dev_dbg(&st->spi->dev, "[%d] 0x%X\n",
				i, (u32)st->regs[i] | i);
		}
	}
	return 0;
}

static int adf4350_reg_access(struct iio_dev *indio_dev,
			      unsigned reg, unsigned writeval,
			      unsigned *readval)
{
	struct adf4350_state *st = iio_priv(indio_dev);
	int ret;

	if (reg > ADF4350_REG5)
		return -EINVAL;

	mutex_lock(&st->lock);
	if (readval == NULL) {
		st->regs[reg] = writeval & ~(BIT(0) | BIT(1) | BIT(2));
		ret = adf4350_sync_config(st);
	} else {
		*readval =  st->regs_hw[reg];
		ret = 0;
	}
	mutex_unlock(&st->lock);

	return ret;
}

static int adf4350_tune_r_cnt(struct adf4350_state *st, unsigned short r_cnt)
{
	struct adf4350_platform_data *pdata = st->pdata;

	do {
		r_cnt++;
		st->fpfd = (st->clkin * (pdata->ref_doubler_en ? 2 : 1)) /
			   (r_cnt * (pdata->ref_div2_en ? 2 : 1));
	} while (st->fpfd > ADF4350_MAX_FREQ_PFD);

	return r_cnt;
}

static int adf4350_set_freq(struct adf4350_state *st, unsigned long long freq)
{
	struct adf4350_platform_data *pdata = st->pdata;
	u64 tmp;
	u32 div_gcd, prescaler, chspc;
	u16 mdiv, r_cnt = 0;
	u8 band_sel_div;

	if (freq > ADF4350_MAX_OUT_FREQ || freq < st->min_out_freq)
		return -EINVAL;

	st->r4_rf_div_sel = 0;

	/*
	 * !\TODO: The below computation is making sure we get a power of 2
	 * shift (st->r4_rf_div_sel) so that freq becomes higher or equal to
	 * ADF4350_MIN_VCO_FREQ. This might be simplified with fls()/fls_long()
	 * and friends.
	 */
	while (freq < ADF4350_MIN_VCO_FREQ) {
		freq <<= 1;
		st->r4_rf_div_sel++;
	}

	if (freq > ADF4350_MAX_FREQ_45_PRESC) {
		prescaler = ADF4350_REG1_PRESCALER;
		mdiv = 75;
	} else {
		prescaler = 0;
		mdiv = 23;
	}

	/*
	 * Allow a predefined reference division factor
	 * if not set, compute our own
	 */
	if (pdata->ref_div_factor)
		r_cnt = pdata->ref_div_factor - 1;

	chspc = st->chspc;

	do  {
		do {
			do {
				r_cnt = adf4350_tune_r_cnt(st, r_cnt);
				st->r1_mod = st->fpfd / chspc;
				if (r_cnt > ADF4350_MAX_R_CNT) {
					/* try higher spacing values */
					chspc++;
					r_cnt = 0;
				}
			} while ((st->r1_mod > ADF4350_MAX_MODULUS) && r_cnt);
		} while (r_cnt == 0);

		tmp = freq * (u64)st->r1_mod + (st->fpfd >> 1);
		do_div(tmp, st->fpfd); /* Div round closest (n + d/2)/d */
		st->r0_fract = do_div(tmp, st->r1_mod);
		st->r0_int = tmp;
	} while (mdiv > st->r0_int);

	band_sel_div = DIV_ROUND_UP(st->fpfd, ADF4350_MAX_BANDSEL_CLK);

	if (st->r0_fract && st->r1_mod) {
		div_gcd = gcd(st->r1_mod, st->r0_fract);
		st->r1_mod /= div_gcd;
		st->r0_fract /= div_gcd;
	} else {
		st->r0_fract = 0;
		st->r1_mod = 1;
	}

	dev_dbg(&st->spi->dev, "VCO: %llu Hz, PFD %lu Hz\n"
		"REF_DIV %d, R0_INT %d, R0_FRACT %d\n"
		"R1_MOD %d, RF_DIV %d\nPRESCALER %s, BAND_SEL_DIV %d\n",
		freq, st->fpfd, r_cnt, st->r0_int, st->r0_fract, st->r1_mod,
		1 << st->r4_rf_div_sel, prescaler ? "8/9" : "4/5",
		band_sel_div);

	st->regs[ADF4350_REG0] = ADF4350_REG0_INT(st->r0_int) |
				 ADF4350_REG0_FRACT(st->r0_fract);

	st->regs[ADF4350_REG1] = ADF4350_REG1_PHASE(1) |
				 ADF4350_REG1_MOD(st->r1_mod) |
				 prescaler;

	st->regs[ADF4350_REG2] =
		ADF4350_REG2_10BIT_R_CNT(r_cnt) |
		ADF4350_REG2_DOUBLE_BUFF_EN |
		(pdata->ref_doubler_en ? ADF4350_REG2_RMULT2_EN : 0) |
		(pdata->ref_div2_en ? ADF4350_REG2_RDIV2_EN : 0) |
		(pdata->r2_user_settings & (ADF4350_REG2_PD_POLARITY_POS |
		ADF4350_REG2_LDP_6ns | ADF4350_REG2_LDF_INT_N |
		ADF4350_REG2_CHARGE_PUMP_CURR_uA(5000) |
		ADF4350_REG2_MUXOUT(0x7) | ADF4350_REG2_NOISE_MODE(0x3)));

	st->regs[ADF4350_REG3] = pdata->r3_user_settings &
				 (ADF4350_REG3_12BIT_CLKDIV(0xFFF) |
				 ADF4350_REG3_12BIT_CLKDIV_MODE(0x3) |
				 ADF4350_REG3_12BIT_CSR_EN |
				 ADF4351_REG3_CHARGE_CANCELLATION_EN |
				 ADF4351_REG3_ANTI_BACKLASH_3ns_EN |
				 ADF4351_REG3_BAND_SEL_CLOCK_MODE_HIGH);

	st->regs[ADF4350_REG4] =
		ADF4350_REG4_FEEDBACK_FUND |
		ADF4350_REG4_RF_DIV_SEL(st->r4_rf_div_sel) |
		ADF4350_REG4_8BIT_BAND_SEL_CLKDIV(band_sel_div) |
		ADF4350_REG4_RF_OUT_EN |
		(pdata->r4_user_settings &
		(ADF4350_REG4_OUTPUT_PWR(0x3) |
		ADF4350_REG4_AUX_OUTPUT_PWR(0x3) |
		ADF4350_REG4_AUX_OUTPUT_EN |
		ADF4350_REG4_AUX_OUTPUT_FUND |
		ADF4350_REG4_MUTE_TILL_LOCK_EN));

	st->regs[ADF4350_REG5] = ADF4350_REG5_LD_PIN_MODE_DIGITAL;
	st->freq_req = freq;

	return adf4350_sync_config(st);
}

static ssize_t adf4350_write(struct iio_dev *indio_dev,
				    uintptr_t private,
				    const struct iio_chan_spec *chan,
				    const char *buf, size_t len)
{
	struct adf4350_state *st = iio_priv(indio_dev);
	unsigned long long readin;
	unsigned long tmp;
	int ret;

	ret = kstrtoull(buf, 10, &readin);
	if (ret)
		return ret;

	mutex_lock(&st->lock);
	switch ((u32)private) {
	case ADF4350_FREQ:
		ret = adf4350_set_freq(st, readin);
		break;
	case ADF4350_FREQ_REFIN:
		if (readin > ADF4350_MAX_FREQ_REFIN) {
			ret = -EINVAL;
			break;
		}

		if (st->clk) {
			tmp = clk_round_rate(st->clk, readin);
			if (tmp != readin) {
				ret = -EINVAL;
				break;
			}
			ret = clk_set_rate(st->clk, tmp);
			if (ret < 0)
				break;
		}
		st->clkin = readin;
		ret = adf4350_set_freq(st, st->freq_req);
		break;
	case ADF4350_FREQ_RESOLUTION:
		if (readin == 0)
			ret = -EINVAL;
		else
			st->chspc = readin;
		break;
	case ADF4350_PWRDOWN:
		if (readin)
			st->regs[ADF4350_REG2] |= ADF4350_REG2_POWER_DOWN_EN;
		else
			st->regs[ADF4350_REG2] &= ~ADF4350_REG2_POWER_DOWN_EN;

		adf4350_sync_config(st);
		break;
	default:
		ret = -EINVAL;
	}
	mutex_unlock(&st->lock);

	return ret ? ret : len;
}

static ssize_t adf4350_read(struct iio_dev *indio_dev,
				   uintptr_t private,
				   const struct iio_chan_spec *chan,
				   char *buf)
{
	struct adf4350_state *st = iio_priv(indio_dev);
	unsigned long long val;
	int ret = 0;

	mutex_lock(&st->lock);
	switch ((u32)private) {
	case ADF4350_FREQ:
		val = (u64)((st->r0_int * st->r1_mod) + st->r0_fract) *
			(u64)st->fpfd;
		do_div(val, st->r1_mod * (1 << st->r4_rf_div_sel));
		/* PLL unlocked? return error */
		if (st->lock_detect_gpiod)
			if (!gpiod_get_value(st->lock_detect_gpiod)) {
				dev_dbg(&st->spi->dev, "PLL un-locked\n");
				ret = -EBUSY;
			}
		break;
	case ADF4350_FREQ_REFIN:
		if (st->clk)
			st->clkin = clk_get_rate(st->clk);

		val = st->clkin;
		break;
	case ADF4350_FREQ_RESOLUTION:
		val = st->chspc;
		break;
	case ADF4350_PWRDOWN:
		val = !!(st->regs[ADF4350_REG2] & ADF4350_REG2_POWER_DOWN_EN);
		break;
	default:
		ret = -EINVAL;
		val = 0;
	}
	mutex_unlock(&st->lock);

	return ret < 0 ? ret : sprintf(buf, "%llu\n", val);
}

#define _ADF4350_EXT_INFO(_name, _ident) { \
	.name = _name, \
	.read = adf4350_read, \
	.write = adf4350_write, \
	.private = _ident, \
	.shared = IIO_SEPARATE, \
}

static const struct iio_chan_spec_ext_info adf4350_ext_info[] = {
	/* Ideally we use IIO_CHAN_INFO_FREQUENCY, but there are
	 * values > 2^32 in order to support the entire frequency range
	 * in Hz. Using scale is a bit ugly.
	 */
	_ADF4350_EXT_INFO("frequency", ADF4350_FREQ),
	_ADF4350_EXT_INFO("frequency_resolution", ADF4350_FREQ_RESOLUTION),
	_ADF4350_EXT_INFO("refin_frequency", ADF4350_FREQ_REFIN),
	_ADF4350_EXT_INFO("powerdown", ADF4350_PWRDOWN),
	{ }
};

static const struct iio_chan_spec adf4350_chan = {
	.type = IIO_ALTVOLTAGE,
	.indexed = 1,
	.output = 1,
	.ext_info = adf4350_ext_info,
};

static const struct iio_info adf4350_info = {
	.debugfs_reg_access = &adf4350_reg_access,
};

static void adf4350_clk_del_provider(void *data)
{
	struct adf4350_state *st = data;

	of_clk_del_provider(st->spi->dev.of_node);
}

static unsigned long adf4350_clk_recalc_rate(struct clk_hw *hw,
					     unsigned long parent_rate)
{
	struct adf4350_state *st = to_adf4350_state(hw);
	unsigned long long tmp;

	tmp = (u64)(st->r0_int * st->r1_mod + st->r0_fract) * st->fpfd;
	do_div(tmp, st->r1_mod * (1 << st->r4_rf_div_sel));

	return tmp;
}

static int adf4350_clk_set_rate(struct clk_hw *hw,
				unsigned long rate,
				unsigned long parent_rate)
{
	struct adf4350_state *st = to_adf4350_state(hw);

	if (parent_rate == 0 || parent_rate > ADF4350_MAX_FREQ_REFIN)
		return -EINVAL;

	st->clkin = parent_rate;

	return adf4350_set_freq(st, rate);
}

static int adf4350_clk_prepare(struct clk_hw *hw)
{
	struct adf4350_state *st = to_adf4350_state(hw);

	st->regs[ADF4350_REG2] &= ~ADF4350_REG2_POWER_DOWN_EN;

	return adf4350_sync_config(st);
}

static void adf4350_clk_unprepare(struct clk_hw *hw)
{
	struct adf4350_state *st = to_adf4350_state(hw);

	st->regs[ADF4350_REG2] |= ADF4350_REG2_POWER_DOWN_EN;

	adf4350_sync_config(st);
}

static int adf4350_clk_is_enabled(struct clk_hw *hw)
{
	struct adf4350_state *st = to_adf4350_state(hw);

	return (st->regs[ADF4350_REG2] & ADF4350_REG2_POWER_DOWN_EN);
}

static const struct clk_ops adf4350_clk_ops = {
	.recalc_rate = adf4350_clk_recalc_rate,
	.set_rate = adf4350_clk_set_rate,
	.prepare = adf4350_clk_prepare,
	.unprepare = adf4350_clk_unprepare,
	.is_enabled = adf4350_clk_is_enabled,
};

static int adf4350_clk_register(struct adf4350_state *st)
{
	struct spi_device *spi = st->spi;
	struct clk_init_data init;
	struct clk *clk;
	const char *parent_name;
	int ret;

	if (!device_property_present(&spi->dev, "#clock-cells"))
		return 0;

	if (device_property_read_string(&spi->dev, "clock-output-names", &init.name)) {
		init.name = devm_kasprintf(&spi->dev, GFP_KERNEL, "%s-clk",
					   fwnode_get_name(dev_fwnode(&spi->dev)));
		if (!init.name)
			return -ENOMEM;
	}

	parent_name = of_clk_get_parent_name(spi->dev.of_node, 0);
	if (!parent_name)
		return -EINVAL;

	init.ops = &adf4350_clk_ops;
	init.parent_names = &parent_name;
	init.num_parents = 1;
	init.flags = CLK_SET_RATE_PARENT;

	st->hw.init = &init;
	clk = devm_clk_register(&spi->dev, &st->hw);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	ret = of_clk_add_provider(spi->dev.of_node, of_clk_src_simple_get, clk);
	if (ret)
		return ret;

	st->clkout = clk;

	return devm_add_action_or_reset(&spi->dev, adf4350_clk_del_provider, st);
}

static struct adf4350_platform_data *adf4350_parse_dt(struct device *dev)
{
	struct adf4350_platform_data *pdata;
	unsigned int tmp;

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return NULL;

	snprintf(pdata->name, sizeof(pdata->name), "%pfw", dev_fwnode(dev));

	tmp = 10000;
	device_property_read_u32(dev, "adi,channel-spacing", &tmp);
	pdata->channel_spacing = tmp;

	tmp = 0;
	device_property_read_u32(dev, "adi,power-up-frequency", &tmp);
	pdata->power_up_frequency = tmp;

	tmp = 0;
	device_property_read_u32(dev, "adi,reference-div-factor", &tmp);
	pdata->ref_div_factor = tmp;

	pdata->ref_doubler_en = device_property_read_bool(dev, "adi,reference-doubler-enable");
	pdata->ref_div2_en = device_property_read_bool(dev, "adi,reference-div2-enable");

	/* r2_user_settings */
	pdata->r2_user_settings = 0;
	if (device_property_read_bool(dev, "adi,phase-detector-polarity-positive-enable"))
		pdata->r2_user_settings |= ADF4350_REG2_PD_POLARITY_POS;
	if (device_property_read_bool(dev, "adi,lock-detect-precision-6ns-enable"))
		pdata->r2_user_settings |= ADF4350_REG2_LDP_6ns;
	if (device_property_read_bool(dev, "adi,lock-detect-function-integer-n-enable"))
		pdata->r2_user_settings |= ADF4350_REG2_LDF_INT_N;

	tmp = 2500;
	device_property_read_u32(dev, "adi,charge-pump-current", &tmp);
	pdata->r2_user_settings |= ADF4350_REG2_CHARGE_PUMP_CURR_uA(tmp);

	tmp = 0;
	device_property_read_u32(dev, "adi,muxout-select", &tmp);
	pdata->r2_user_settings |= ADF4350_REG2_MUXOUT(tmp);

	if (device_property_read_bool(dev, "adi,low-spur-mode-enable"))
		pdata->r2_user_settings |= ADF4350_REG2_NOISE_MODE(0x3);

	/* r3_user_settings */

	pdata->r3_user_settings = 0;
	if (device_property_read_bool(dev, "adi,cycle-slip-reduction-enable"))
		pdata->r3_user_settings |= ADF4350_REG3_12BIT_CSR_EN;
	if (device_property_read_bool(dev, "adi,charge-cancellation-enable"))
		pdata->r3_user_settings |= ADF4351_REG3_CHARGE_CANCELLATION_EN;
	if (device_property_read_bool(dev, "adi,anti-backlash-3ns-enable"))
		pdata->r3_user_settings |= ADF4351_REG3_ANTI_BACKLASH_3ns_EN;
	if (device_property_read_bool(dev, "adi,band-select-clock-mode-high-enable"))
		pdata->r3_user_settings |= ADF4351_REG3_BAND_SEL_CLOCK_MODE_HIGH;

	tmp = 0;
	device_property_read_u32(dev, "adi,12bit-clk-divider", &tmp);
	pdata->r3_user_settings |= ADF4350_REG3_12BIT_CLKDIV(tmp);

	tmp = 0;
	device_property_read_u32(dev, "adi,clk-divider-mode", &tmp);
	pdata->r3_user_settings |= ADF4350_REG3_12BIT_CLKDIV_MODE(tmp);

	/* r4_user_settings */

	pdata->r4_user_settings = 0;
	if (device_property_read_bool(dev, "adi,aux-output-enable"))
		pdata->r4_user_settings |= ADF4350_REG4_AUX_OUTPUT_EN;
	if (device_property_read_bool(dev, "adi,aux-output-fundamental-enable"))
		pdata->r4_user_settings |= ADF4350_REG4_AUX_OUTPUT_FUND;
	if (device_property_read_bool(dev, "adi,mute-till-lock-enable"))
		pdata->r4_user_settings |= ADF4350_REG4_MUTE_TILL_LOCK_EN;

	tmp = 0;
	device_property_read_u32(dev, "adi,output-power", &tmp);
	pdata->r4_user_settings |= ADF4350_REG4_OUTPUT_PWR(tmp);

	tmp = 0;
	device_property_read_u32(dev, "adi,aux-output-power", &tmp);
	pdata->r4_user_settings |= ADF4350_REG4_AUX_OUTPUT_PWR(tmp);

	return pdata;
}

static void adf4350_power_down(void *data)
{
	struct iio_dev *indio_dev = data;
	struct adf4350_state *st = iio_priv(indio_dev);

	st->regs[ADF4350_REG2] |= ADF4350_REG2_POWER_DOWN_EN;
	adf4350_sync_config(st);
}

static int adf4350_probe(struct spi_device *spi)
{
	struct adf4350_platform_data *pdata;
	struct iio_dev *indio_dev;
	struct adf4350_state *st;
	struct clk *clk = NULL;
	int ret;

	if (dev_fwnode(&spi->dev)) {
		pdata = adf4350_parse_dt(&spi->dev);
		if (pdata == NULL)
			return -EINVAL;
	} else {
		pdata = dev_get_platdata(&spi->dev);
	}

	if (!pdata) {
		dev_warn(&spi->dev, "no platform data? using default\n");
		pdata = &default_pdata;
	}

	if (!pdata->clkin) {
		clk = devm_clk_get_enabled(&spi->dev, "clkin");
		if (IS_ERR(clk))
			return PTR_ERR(clk);
	}

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (indio_dev == NULL)
		return -ENOMEM;

	st = iio_priv(indio_dev);

	ret = devm_regulator_get_enable(&spi->dev, "vcc");
	if (ret)
		return ret;

	st->spi = spi;
	st->pdata = pdata;

	indio_dev->name = (pdata->name[0] != 0) ? pdata->name :
		spi_get_device_id(spi)->name;

	indio_dev->info = &adf4350_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	mutex_init(&st->lock);

	st->chspc = pdata->channel_spacing;
	if (clk) {
		st->clk = clk;
		st->clkin = clk_get_rate(clk);
	} else {
		st->clkin = pdata->clkin;
	}

	st->min_out_freq = spi_get_device_id(spi)->driver_data == 4351 ?
		ADF4351_MIN_OUT_FREQ : ADF4350_MIN_OUT_FREQ;

	memset(st->regs_hw, 0xFF, sizeof(st->regs_hw));

	st->lock_detect_gpiod = devm_gpiod_get_optional(&spi->dev, NULL,
							GPIOD_IN);
	if (IS_ERR(st->lock_detect_gpiod))
		return PTR_ERR(st->lock_detect_gpiod);

	if (pdata->power_up_frequency) {
		ret = adf4350_set_freq(st, pdata->power_up_frequency);
		if (ret)
			return ret;
	}

	ret = adf4350_clk_register(st);
	if (ret)
		return ret;

	if (!st->clkout) {
		indio_dev->channels = &adf4350_chan;
		indio_dev->num_channels = 1;
	}

	ret = devm_add_action_or_reset(&spi->dev, adf4350_power_down, indio_dev);
	if (ret)
		return ret;

	return devm_iio_device_register(&spi->dev, indio_dev);
}

static const struct of_device_id adf4350_of_match[] = {
	{ .compatible = "adi,adf4350", },
	{ .compatible = "adi,adf4351", },
	{ }
};
MODULE_DEVICE_TABLE(of, adf4350_of_match);

static const struct spi_device_id adf4350_id[] = {
	{"adf4350", 4350},
	{"adf4351", 4351},
	{ }
};
MODULE_DEVICE_TABLE(spi, adf4350_id);

static struct spi_driver adf4350_driver = {
	.driver = {
		.name	= "adf4350",
		.of_match_table = adf4350_of_match,
	},
	.probe		= adf4350_probe,
	.id_table	= adf4350_id,
};
module_spi_driver(adf4350_driver);

MODULE_AUTHOR("Michael Hennerich <michael.hennerich@analog.com>");
MODULE_DESCRIPTION("Analog Devices ADF4350/ADF4351 PLL");
MODULE_LICENSE("GPL v2");
