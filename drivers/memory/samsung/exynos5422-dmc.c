// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 Samsung Electronics Co., Ltd.
 * Author: Lukasz Luba <l.luba@partner.samsung.com>
 */

#include <linux/clk.h>
#include <linux/devfreq.h>
#include <linux/devfreq-event.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/of_device.h>
#include <linux/pm_opp.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include "../jedec_ddr.h"
#include "../of_memory.h"

static int irqmode;
module_param(irqmode, int, 0644);
MODULE_PARM_DESC(irqmode, "Enable IRQ mode (0=off [default], 1=on)");

#define EXYNOS5_DREXI_TIMINGAREF		(0x0030)
#define EXYNOS5_DREXI_TIMINGROW0		(0x0034)
#define EXYNOS5_DREXI_TIMINGDATA0		(0x0038)
#define EXYNOS5_DREXI_TIMINGPOWER0		(0x003C)
#define EXYNOS5_DREXI_TIMINGROW1		(0x00E4)
#define EXYNOS5_DREXI_TIMINGDATA1		(0x00E8)
#define EXYNOS5_DREXI_TIMINGPOWER1		(0x00EC)
#define CDREX_PAUSE				(0x2091c)
#define CDREX_LPDDR3PHY_CON3			(0x20a20)
#define CDREX_LPDDR3PHY_CLKM_SRC		(0x20700)
#define EXYNOS5_TIMING_SET_SWI			BIT(28)
#define USE_MX_MSPLL_TIMINGS			(1)
#define USE_BPLL_TIMINGS			(0)
#define EXYNOS5_AREF_NORMAL			(0x2e)

#define DREX_PPCCLKCON		(0x0130)
#define DREX_PEREV2CONFIG	(0x013c)
#define DREX_PMNC_PPC		(0xE000)
#define DREX_CNTENS_PPC		(0xE010)
#define DREX_CNTENC_PPC		(0xE020)
#define DREX_INTENS_PPC		(0xE030)
#define DREX_INTENC_PPC		(0xE040)
#define DREX_FLAG_PPC		(0xE050)
#define DREX_PMCNT2_PPC		(0xE130)

/*
 * A value for register DREX_PMNC_PPC which should be written to reset
 * the cycle counter CCNT (a reference wall clock). It sets zero to the
 * CCNT counter.
 */
#define CC_RESET		BIT(2)

/*
 * A value for register DREX_PMNC_PPC which does the reset of all performance
 * counters to zero.
 */
#define PPC_COUNTER_RESET	BIT(1)

/*
 * Enables all configured counters (including cycle counter). The value should
 * be written to the register DREX_PMNC_PPC.
 */
#define PPC_ENABLE		BIT(0)

/* A value for register DREX_PPCCLKCON which enables performance events clock.
 * Must be written before first access to the performance counters register
 * set, otherwise it could crash.
 */
#define PEREV_CLK_EN		BIT(0)

/*
 * Values which are used to enable counters, interrupts or configure flags of
 * the performance counters. They configure counter 2 and cycle counter.
 */
#define PERF_CNT2		BIT(2)
#define PERF_CCNT		BIT(31)

/*
 * Performance event types which are used for setting the preferred event
 * to track in the counters.
 * There is a set of different types, the values are from range 0 to 0x6f.
 * These settings should be written to the configuration register which manages
 * the type of the event (register DREX_PEREV2CONFIG).
 */
#define READ_TRANSFER_CH0	(0x6d)
#define READ_TRANSFER_CH1	(0x6f)

#define PERF_COUNTER_START_VALUE 0xff000000
#define PERF_EVENT_UP_DOWN_THRESHOLD 900000000ULL

/**
 * struct dmc_opp_table - Operating level desciption
 * @freq_hz:		target frequency in Hz
 * @volt_uv:		target voltage in uV
 *
 * Covers frequency and voltage settings of the DMC operating mode.
 */
struct dmc_opp_table {
	u32 freq_hz;
	u32 volt_uv;
};

/**
 * struct exynos5_dmc - main structure describing DMC device
 * @dev:		DMC device
 * @df:			devfreq device structure returned by devfreq framework
 * @gov_data:		configuration of devfreq governor
 * @base_drexi0:	DREX0 registers mapping
 * @base_drexi1:	DREX1 registers mapping
 * @clk_regmap:		regmap for clock controller registers
 * @lock:		protects curr_rate and frequency/voltage setting section
 * @curr_rate:		current frequency
 * @curr_volt:		current voltage
 * @opp:		OPP table
 * @opp_count:		number of 'opp' elements
 * @timings_arr_size:	number of 'timings' elements
 * @timing_row:		values for timing row register, for each OPP
 * @timing_data:	values for timing data register, for each OPP
 * @timing_power:	balues for timing power register, for each OPP
 * @timings:		DDR memory timings, from device tree
 * @min_tck:		DDR memory minimum timing values, from device tree
 * @bypass_timing_row:	value for timing row register for bypass timings
 * @bypass_timing_data:	value for timing data register for bypass timings
 * @bypass_timing_power:	value for timing power register for bypass
 *				timings
 * @vdd_mif:		Memory interface regulator
 * @fout_spll:		clock: SPLL
 * @fout_bpll:		clock: BPLL
 * @mout_spll:		clock: mux SPLL
 * @mout_bpll:		clock: mux BPLL
 * @mout_mclk_cdrex:	clock: mux mclk_cdrex
 * @mout_mx_mspll_ccore:	clock: mux mx_mspll_ccore
 * @counter:		devfreq events
 * @num_counters:	number of 'counter' elements
 * @last_overflow_ts:	time (in ns) of last overflow of each DREX
 * @load:		utilization in percents
 * @total:		total time between devfreq events
 * @in_irq_mode:	whether running in interrupt mode (true)
 *			or polling (false)
 *
 * The main structure for the Dynamic Memory Controller which covers clocks,
 * memory regions, HW information, parameters and current operating mode.
 */
struct exynos5_dmc {
	struct device *dev;
	struct devfreq *df;
	struct devfreq_simple_ondemand_data gov_data;
	void __iomem *base_drexi0;
	void __iomem *base_drexi1;
	struct regmap *clk_regmap;
	/* Protects curr_rate and frequency/voltage setting section */
	struct mutex lock;
	unsigned long curr_rate;
	unsigned long curr_volt;
	struct dmc_opp_table *opp;
	int opp_count;
	u32 timings_arr_size;
	u32 *timing_row;
	u32 *timing_data;
	u32 *timing_power;
	const struct lpddr3_timings *timings;
	const struct lpddr3_min_tck *min_tck;
	u32 bypass_timing_row;
	u32 bypass_timing_data;
	u32 bypass_timing_power;
	struct regulator *vdd_mif;
	struct clk *fout_spll;
	struct clk *fout_bpll;
	struct clk *mout_spll;
	struct clk *mout_bpll;
	struct clk *mout_mclk_cdrex;
	struct clk *mout_mx_mspll_ccore;
	struct devfreq_event_dev **counter;
	int num_counters;
	u64 last_overflow_ts[2];
	unsigned long load;
	unsigned long total;
	bool in_irq_mode;
};

#define TIMING_FIELD(t_name, t_bit_beg, t_bit_end) \
	{ .name = t_name, .bit_beg = t_bit_beg, .bit_end = t_bit_end }

#define TIMING_VAL2REG(timing, t_val)			\
({							\
		u32 __val;				\
		__val = (t_val) << (timing)->bit_beg;	\
		__val;					\
})

struct timing_reg {
	char *name;
	int bit_beg;
	int bit_end;
	unsigned int val;
};

static const struct timing_reg timing_row_reg_fields[] = {
	TIMING_FIELD("tRFC", 24, 31),
	TIMING_FIELD("tRRD", 20, 23),
	TIMING_FIELD("tRP", 16, 19),
	TIMING_FIELD("tRCD", 12, 15),
	TIMING_FIELD("tRC", 6, 11),
	TIMING_FIELD("tRAS", 0, 5),
};

static const struct timing_reg timing_data_reg_fields[] = {
	TIMING_FIELD("tWTR", 28, 31),
	TIMING_FIELD("tWR", 24, 27),
	TIMING_FIELD("tRTP", 20, 23),
	TIMING_FIELD("tW2W-C2C", 14, 14),
	TIMING_FIELD("tR2R-C2C", 12, 12),
	TIMING_FIELD("WL", 8, 11),
	TIMING_FIELD("tDQSCK", 4, 7),
	TIMING_FIELD("RL", 0, 3),
};

static const struct timing_reg timing_power_reg_fields[] = {
	TIMING_FIELD("tFAW", 26, 31),
	TIMING_FIELD("tXSR", 16, 25),
	TIMING_FIELD("tXP", 8, 15),
	TIMING_FIELD("tCKE", 4, 7),
	TIMING_FIELD("tMRD", 0, 3),
};

#define TIMING_COUNT (ARRAY_SIZE(timing_row_reg_fields) + \
		      ARRAY_SIZE(timing_data_reg_fields) + \
		      ARRAY_SIZE(timing_power_reg_fields))

static int exynos5_counters_set_event(struct exynos5_dmc *dmc)
{
	int i, ret;

	for (i = 0; i < dmc->num_counters; i++) {
		if (!dmc->counter[i])
			continue;
		ret = devfreq_event_set_event(dmc->counter[i]);
		if (ret < 0)
			return ret;
	}
	return 0;
}

static int exynos5_counters_enable_edev(struct exynos5_dmc *dmc)
{
	int i, ret;

	for (i = 0; i < dmc->num_counters; i++) {
		if (!dmc->counter[i])
			continue;
		ret = devfreq_event_enable_edev(dmc->counter[i]);
		if (ret < 0)
			return ret;
	}
	return 0;
}

static int exynos5_counters_disable_edev(struct exynos5_dmc *dmc)
{
	int i, ret;

	for (i = 0; i < dmc->num_counters; i++) {
		if (!dmc->counter[i])
			continue;
		ret = devfreq_event_disable_edev(dmc->counter[i]);
		if (ret < 0)
			return ret;
	}
	return 0;
}

/**
 * find_target_freq_id() - Finds requested frequency in local DMC configuration
 * @dmc:	device for which the information is checked
 * @target_rate:	requested frequency in KHz
 *
 * Seeks in the local DMC driver structure for the requested frequency value
 * and returns index or error value.
 */
static int find_target_freq_idx(struct exynos5_dmc *dmc,
				unsigned long target_rate)
{
	int i;

	for (i = dmc->opp_count - 1; i >= 0; i--)
		if (dmc->opp[i].freq_hz <= target_rate)
			return i;

	return -EINVAL;
}

/**
 * exynos5_switch_timing_regs() - Changes bank register set for DRAM timings
 * @dmc:	device for which the new settings is going to be applied
 * @set:	boolean variable passing set value
 *
 * Changes the register set, which holds timing parameters.
 * There is two register sets: 0 and 1. The register set 0
 * is used in normal operation when the clock is provided from main PLL.
 * The bank register set 1 is used when the main PLL frequency is going to be
 * changed and the clock is taken from alternative, stable source.
 * This function switches between these banks according to the
 * currently used clock source.
 */
static int exynos5_switch_timing_regs(struct exynos5_dmc *dmc, bool set)
{
	unsigned int reg;
	int ret;

	ret = regmap_read(dmc->clk_regmap, CDREX_LPDDR3PHY_CON3, &reg);
	if (ret)
		return ret;

	if (set)
		reg |= EXYNOS5_TIMING_SET_SWI;
	else
		reg &= ~EXYNOS5_TIMING_SET_SWI;

	regmap_write(dmc->clk_regmap, CDREX_LPDDR3PHY_CON3, reg);

	return 0;
}

/**
 * exynos5_init_freq_table() - Initialized PM OPP framework
 * @dmc:	DMC device for which the frequencies are used for OPP init
 * @profile:	devfreq device's profile
 *
 * Populate the devfreq device's OPP table based on current frequency, voltage.
 */
static int exynos5_init_freq_table(struct exynos5_dmc *dmc,
				   struct devfreq_dev_profile *profile)
{
	int i, ret;
	int idx;
	unsigned long freq;

	ret = dev_pm_opp_of_add_table(dmc->dev);
	if (ret < 0) {
		dev_err(dmc->dev, "Failed to get OPP table\n");
		return ret;
	}

	dmc->opp_count = dev_pm_opp_get_opp_count(dmc->dev);

	dmc->opp = devm_kmalloc_array(dmc->dev, dmc->opp_count,
				      sizeof(struct dmc_opp_table), GFP_KERNEL);
	if (!dmc->opp)
		goto err_opp;

	idx = dmc->opp_count - 1;
	for (i = 0, freq = ULONG_MAX; i < dmc->opp_count; i++, freq--) {
		struct dev_pm_opp *opp;

		opp = dev_pm_opp_find_freq_floor(dmc->dev, &freq);
		if (IS_ERR(opp))
			goto err_opp;

		dmc->opp[idx - i].freq_hz = freq;
		dmc->opp[idx - i].volt_uv = dev_pm_opp_get_voltage(opp);

		dev_pm_opp_put(opp);
	}

	return 0;

err_opp:
	dev_pm_opp_of_remove_table(dmc->dev);

	return -EINVAL;
}

/**
 * exynos5_set_bypass_dram_timings() - Low-level changes of the DRAM timings
 * @dmc:	device for which the new settings is going to be applied
 *
 * Low-level function for changing timings for DRAM memory clocking from
 * 'bypass' clock source (fixed frequency @400MHz).
 * It uses timing bank registers set 1.
 */
static void exynos5_set_bypass_dram_timings(struct exynos5_dmc *dmc)
{
	writel(EXYNOS5_AREF_NORMAL,
	       dmc->base_drexi0 + EXYNOS5_DREXI_TIMINGAREF);

	writel(dmc->bypass_timing_row,
	       dmc->base_drexi0 + EXYNOS5_DREXI_TIMINGROW1);
	writel(dmc->bypass_timing_row,
	       dmc->base_drexi1 + EXYNOS5_DREXI_TIMINGROW1);
	writel(dmc->bypass_timing_data,
	       dmc->base_drexi0 + EXYNOS5_DREXI_TIMINGDATA1);
	writel(dmc->bypass_timing_data,
	       dmc->base_drexi1 + EXYNOS5_DREXI_TIMINGDATA1);
	writel(dmc->bypass_timing_power,
	       dmc->base_drexi0 + EXYNOS5_DREXI_TIMINGPOWER1);
	writel(dmc->bypass_timing_power,
	       dmc->base_drexi1 + EXYNOS5_DREXI_TIMINGPOWER1);
}

/**
 * exynos5_dram_change_timings() - Low-level changes of the DRAM final timings
 * @dmc:	device for which the new settings is going to be applied
 * @target_rate:	target frequency of the DMC
 *
 * Low-level function for changing timings for DRAM memory operating from main
 * clock source (BPLL), which can have different frequencies. Thus, each
 * frequency must have corresponding timings register values in order to keep
 * the needed delays.
 * It uses timing bank registers set 0.
 */
static int exynos5_dram_change_timings(struct exynos5_dmc *dmc,
				       unsigned long target_rate)
{
	int idx;

	for (idx = dmc->opp_count - 1; idx >= 0; idx--)
		if (dmc->opp[idx].freq_hz <= target_rate)
			break;

	if (idx < 0)
		return -EINVAL;

	writel(EXYNOS5_AREF_NORMAL,
	       dmc->base_drexi0 + EXYNOS5_DREXI_TIMINGAREF);

	writel(dmc->timing_row[idx],
	       dmc->base_drexi0 + EXYNOS5_DREXI_TIMINGROW0);
	writel(dmc->timing_row[idx],
	       dmc->base_drexi1 + EXYNOS5_DREXI_TIMINGROW0);
	writel(dmc->timing_data[idx],
	       dmc->base_drexi0 + EXYNOS5_DREXI_TIMINGDATA0);
	writel(dmc->timing_data[idx],
	       dmc->base_drexi1 + EXYNOS5_DREXI_TIMINGDATA0);
	writel(dmc->timing_power[idx],
	       dmc->base_drexi0 + EXYNOS5_DREXI_TIMINGPOWER0);
	writel(dmc->timing_power[idx],
	       dmc->base_drexi1 + EXYNOS5_DREXI_TIMINGPOWER0);

	return 0;
}

/**
 * exynos5_dmc_align_target_voltage() - Sets the final voltage for the DMC
 * @dmc:	device for which it is going to be set
 * @target_volt:	new voltage which is chosen to be final
 *
 * Function tries to align voltage to the safe level for 'normal' mode.
 * It checks the need of higher voltage and changes the value. The target
 * voltage might be lower that currently set and still the system will be
 * stable.
 */
static int exynos5_dmc_align_target_voltage(struct exynos5_dmc *dmc,
					    unsigned long target_volt)
{
	int ret = 0;

	if (dmc->curr_volt <= target_volt)
		return 0;

	ret = regulator_set_voltage(dmc->vdd_mif, target_volt,
				    target_volt);
	if (!ret)
		dmc->curr_volt = target_volt;

	return ret;
}

/**
 * exynos5_dmc_align_bypass_voltage() - Sets the voltage for the DMC
 * @dmc:	device for which it is going to be set
 * @target_volt:	new voltage which is chosen to be final
 *
 * Function tries to align voltage to the safe level for the 'bypass' mode.
 * It checks the need of higher voltage and changes the value.
 * The target voltage must not be less than currently needed, because
 * for current frequency the device might become unstable.
 */
static int exynos5_dmc_align_bypass_voltage(struct exynos5_dmc *dmc,
					    unsigned long target_volt)
{
	int ret = 0;

	if (dmc->curr_volt >= target_volt)
		return 0;

	ret = regulator_set_voltage(dmc->vdd_mif, target_volt,
				    target_volt);
	if (!ret)
		dmc->curr_volt = target_volt;

	return ret;
}

/**
 * exynos5_dmc_align_bypass_dram_timings() - Chooses and sets DRAM timings
 * @dmc:	device for which it is going to be set
 * @target_rate:	new frequency which is chosen to be final
 *
 * Function changes the DRAM timings for the temporary 'bypass' mode.
 */
static int exynos5_dmc_align_bypass_dram_timings(struct exynos5_dmc *dmc,
						 unsigned long target_rate)
{
	int idx = find_target_freq_idx(dmc, target_rate);

	if (idx < 0)
		return -EINVAL;

	exynos5_set_bypass_dram_timings(dmc);

	return 0;
}

/**
 * exynos5_dmc_switch_to_bypass_configuration() - Switching to temporary clock
 * @dmc:	DMC device for which the switching is going to happen
 * @target_rate:	new frequency which is going to be set as a final
 * @target_volt:	new voltage which is going to be set as a final
 *
 * Function configures DMC and clocks for operating in temporary 'bypass' mode.
 * This mode is used only temporary but if required, changes voltage and timings
 * for DRAM chips. It switches the main clock to stable clock source for the
 * period of the main PLL reconfiguration.
 */
static int
exynos5_dmc_switch_to_bypass_configuration(struct exynos5_dmc *dmc,
					   unsigned long target_rate,
					   unsigned long target_volt)
{
	int ret;

	/*
	 * Having higher voltage for a particular frequency does not harm
	 * the chip. Use it for the temporary frequency change when one
	 * voltage manipulation might be avoided.
	 */
	ret = exynos5_dmc_align_bypass_voltage(dmc, target_volt);
	if (ret)
		return ret;

	/*
	 * Longer delays for DRAM does not cause crash, the opposite does.
	 */
	ret = exynos5_dmc_align_bypass_dram_timings(dmc, target_rate);
	if (ret)
		return ret;

	/*
	 * Delays are long enough, so use them for the new coming clock.
	 */
	ret = exynos5_switch_timing_regs(dmc, USE_MX_MSPLL_TIMINGS);

	return ret;
}

/**
 * exynos5_dmc_change_freq_and_volt() - Changes voltage and frequency of the DMC
 * using safe procedure
 * @dmc:	device for which the frequency is going to be changed
 * @target_rate:	requested new frequency
 * @target_volt:	requested voltage which corresponds to the new frequency
 *
 * The DMC frequency change procedure requires a few steps.
 * The main requirement is to change the clock source in the clk mux
 * for the time of main clock PLL locking. The assumption is that the
 * alternative clock source set as parent is stable.
 * The second parent's clock frequency is fixed to 400MHz, it is named 'bypass'
 * clock. This requires alignment in DRAM timing parameters for the new
 * T-period. There is two bank sets for keeping DRAM
 * timings: set 0 and set 1. The set 0 is used when main clock source is
 * chosen. The 2nd set of regs is used for 'bypass' clock. Switching between
 * the two bank sets is part of the process.
 * The voltage must also be aligned to the minimum required level. There is
 * this intermediate step with switching to 'bypass' parent clock source.
 * if the old voltage is lower, it requires an increase of the voltage level.
 * The complexity of the voltage manipulation is hidden in low level function.
 * In this function there is last alignment of the voltage level at the end.
 */
static int
exynos5_dmc_change_freq_and_volt(struct exynos5_dmc *dmc,
				 unsigned long target_rate,
				 unsigned long target_volt)
{
	int ret;

	ret = exynos5_dmc_switch_to_bypass_configuration(dmc, target_rate,
							 target_volt);
	if (ret)
		return ret;

	/*
	 * Voltage is set at least to a level needed for this frequency,
	 * so switching clock source is safe now.
	 */
	clk_prepare_enable(dmc->fout_spll);
	clk_prepare_enable(dmc->mout_spll);
	clk_prepare_enable(dmc->mout_mx_mspll_ccore);

	ret = clk_set_parent(dmc->mout_mclk_cdrex, dmc->mout_mx_mspll_ccore);
	if (ret)
		goto disable_clocks;

	/*
	 * We are safe to increase the timings for current bypass frequency.
	 * Thanks to this the settings will be ready for the upcoming clock
	 * source change.
	 */
	exynos5_dram_change_timings(dmc, target_rate);

	clk_set_rate(dmc->fout_bpll, target_rate);

	ret = exynos5_switch_timing_regs(dmc, USE_BPLL_TIMINGS);
	if (ret)
		goto disable_clocks;

	ret = clk_set_parent(dmc->mout_mclk_cdrex, dmc->mout_bpll);
	if (ret)
		goto disable_clocks;

	/*
	 * Make sure if the voltage is not from 'bypass' settings and align to
	 * the right level for power efficiency.
	 */
	ret = exynos5_dmc_align_target_voltage(dmc, target_volt);

disable_clocks:
	clk_disable_unprepare(dmc->mout_mx_mspll_ccore);
	clk_disable_unprepare(dmc->mout_spll);
	clk_disable_unprepare(dmc->fout_spll);

	return ret;
}

/**
 * exynos5_dmc_get_volt_freq() - Gets the frequency and voltage from the OPP
 * table.
 * @dmc:	device for which the frequency is going to be changed
 * @freq:       requested frequency in KHz
 * @target_rate:	returned frequency which is the same or lower than
 *			requested
 * @target_volt:	returned voltage which corresponds to the returned
 *			frequency
 * @flags:	devfreq flags provided for this frequency change request
 *
 * Function gets requested frequency and checks OPP framework for needed
 * frequency and voltage. It populates the values 'target_rate' and
 * 'target_volt' or returns error value when OPP framework fails.
 */
static int exynos5_dmc_get_volt_freq(struct exynos5_dmc *dmc,
				     unsigned long *freq,
				     unsigned long *target_rate,
				     unsigned long *target_volt, u32 flags)
{
	struct dev_pm_opp *opp;

	opp = devfreq_recommended_opp(dmc->dev, freq, flags);
	if (IS_ERR(opp))
		return PTR_ERR(opp);

	*target_rate = dev_pm_opp_get_freq(opp);
	*target_volt = dev_pm_opp_get_voltage(opp);
	dev_pm_opp_put(opp);

	return 0;
}

/**
 * exynos5_dmc_target() - Function responsible for changing frequency of DMC
 * @dev:	device for which the frequency is going to be changed
 * @freq:	requested frequency in KHz
 * @flags:	flags provided for this frequency change request
 *
 * An entry function provided to the devfreq framework which provides frequency
 * change of the DMC. The function gets the possible rate from OPP table based
 * on requested frequency. It calls the next function responsible for the
 * frequency and voltage change. In case of failure, does not set 'curr_rate'
 * and returns error value to the framework.
 */
static int exynos5_dmc_target(struct device *dev, unsigned long *freq,
			      u32 flags)
{
	struct exynos5_dmc *dmc = dev_get_drvdata(dev);
	unsigned long target_rate = 0;
	unsigned long target_volt = 0;
	int ret;

	ret = exynos5_dmc_get_volt_freq(dmc, freq, &target_rate, &target_volt,
					flags);

	if (ret)
		return ret;

	if (target_rate == dmc->curr_rate)
		return 0;

	mutex_lock(&dmc->lock);

	ret = exynos5_dmc_change_freq_and_volt(dmc, target_rate, target_volt);

	if (ret) {
		mutex_unlock(&dmc->lock);
		return ret;
	}

	dmc->curr_rate = target_rate;

	mutex_unlock(&dmc->lock);
	return 0;
}

/**
 * exynos5_counters_get() - Gets the performance counters values.
 * @dmc:	device for which the counters are going to be checked
 * @load_count:	variable which is populated with counter value
 * @total_count:	variable which is used as 'wall clock' reference
 *
 * Function which provides performance counters values. It sums up counters for
 * two DMC channels. The 'total_count' is used as a reference and max value.
 * The ratio 'load_count/total_count' shows the busy percentage [0%, 100%].
 */
static int exynos5_counters_get(struct exynos5_dmc *dmc,
				unsigned long *load_count,
				unsigned long *total_count)
{
	unsigned long total = 0;
	struct devfreq_event_data event;
	int ret, i;

	*load_count = 0;

	/* Take into account only read+write counters, but stop all */
	for (i = 0; i < dmc->num_counters; i++) {
		if (!dmc->counter[i])
			continue;

		ret = devfreq_event_get_event(dmc->counter[i], &event);
		if (ret < 0)
			return ret;

		*load_count += event.load_count;

		if (total < event.total_count)
			total = event.total_count;
	}

	*total_count = total;

	return 0;
}

/**
 * exynos5_dmc_start_perf_events() - Setup and start performance event counters
 * @dmc:	device for which the counters are going to be checked
 * @beg_value:	initial value for the counter
 *
 * Function which enables needed counters, interrupts and sets initial values
 * then starts the counters.
 */
static void exynos5_dmc_start_perf_events(struct exynos5_dmc *dmc,
					  u32 beg_value)
{
	/* Enable interrupts for counter 2 */
	writel(PERF_CNT2, dmc->base_drexi0 + DREX_INTENS_PPC);
	writel(PERF_CNT2, dmc->base_drexi1 + DREX_INTENS_PPC);

	/* Enable counter 2 and CCNT  */
	writel(PERF_CNT2 | PERF_CCNT, dmc->base_drexi0 + DREX_CNTENS_PPC);
	writel(PERF_CNT2 | PERF_CCNT, dmc->base_drexi1 + DREX_CNTENS_PPC);

	/* Clear overflow flag for all counters */
	writel(PERF_CNT2 | PERF_CCNT, dmc->base_drexi0 + DREX_FLAG_PPC);
	writel(PERF_CNT2 | PERF_CCNT, dmc->base_drexi1 + DREX_FLAG_PPC);

	/* Reset all counters */
	writel(CC_RESET | PPC_COUNTER_RESET, dmc->base_drexi0 + DREX_PMNC_PPC);
	writel(CC_RESET | PPC_COUNTER_RESET, dmc->base_drexi1 + DREX_PMNC_PPC);

	/*
	 * Set start value for the counters, the number of samples that
	 * will be gathered is calculated as: 0xffffffff - beg_value
	 */
	writel(beg_value, dmc->base_drexi0 + DREX_PMCNT2_PPC);
	writel(beg_value, dmc->base_drexi1 + DREX_PMCNT2_PPC);

	/* Start all counters */
	writel(PPC_ENABLE, dmc->base_drexi0 + DREX_PMNC_PPC);
	writel(PPC_ENABLE, dmc->base_drexi1 + DREX_PMNC_PPC);
}

/**
 * exynos5_dmc_perf_events_calc() - Calculate utilization
 * @dmc:	device for which the counters are going to be checked
 * @diff_ts:	time between last interrupt and current one
 *
 * Function which calculates needed utilization for the devfreq governor.
 * It prepares values for 'busy_time' and 'total_time' based on elapsed time
 * between interrupts, which approximates utilization.
 */
static void exynos5_dmc_perf_events_calc(struct exynos5_dmc *dmc, u64 diff_ts)
{
	/*
	 * This is a simple algorithm for managing traffic on DMC.
	 * When there is almost no load the counters overflow every 4s,
	 * no mater the DMC frequency.
	 * The high load might be approximated using linear function.
	 * Knowing that, simple calculation can provide 'busy_time' and
	 * 'total_time' to the devfreq governor which picks up target
	 * frequency.
	 * We want a fast ramp up and slow decay in frequency change function.
	 */
	if (diff_ts < PERF_EVENT_UP_DOWN_THRESHOLD) {
		/*
		 * Set higher utilization for the simple_ondemand governor.
		 * The governor should increase the frequency of the DMC.
		 */
		dmc->load = 70;
		dmc->total = 100;
	} else {
		/*
		 * Set low utilization for the simple_ondemand governor.
		 * The governor should decrease the frequency of the DMC.
		 */
		dmc->load = 35;
		dmc->total = 100;
	}

	dev_dbg(dmc->dev, "diff_ts=%llu\n", diff_ts);
}

/**
 * exynos5_dmc_perf_events_check() - Checks the status of the counters
 * @dmc:	device for which the counters are going to be checked
 *
 * Function which is called from threaded IRQ to check the counters state
 * and to call approximation for the needed utilization.
 */
static void exynos5_dmc_perf_events_check(struct exynos5_dmc *dmc)
{
	u32 val;
	u64 diff_ts, ts;

	ts = ktime_get_ns();

	/* Stop all counters */
	writel(0, dmc->base_drexi0 + DREX_PMNC_PPC);
	writel(0, dmc->base_drexi1 + DREX_PMNC_PPC);

	/* Check the source in interrupt flag registers (which channel) */
	val = readl(dmc->base_drexi0 + DREX_FLAG_PPC);
	if (val) {
		diff_ts = ts - dmc->last_overflow_ts[0];
		dmc->last_overflow_ts[0] = ts;
		dev_dbg(dmc->dev, "drex0 0xE050 val= 0x%08x\n",  val);
	} else {
		val = readl(dmc->base_drexi1 + DREX_FLAG_PPC);
		diff_ts = ts - dmc->last_overflow_ts[1];
		dmc->last_overflow_ts[1] = ts;
		dev_dbg(dmc->dev, "drex1 0xE050 val= 0x%08x\n",  val);
	}

	exynos5_dmc_perf_events_calc(dmc, diff_ts);

	exynos5_dmc_start_perf_events(dmc, PERF_COUNTER_START_VALUE);
}

/**
 * exynos5_dmc_enable_perf_events() - Enable performance events
 * @dmc:	device for which the counters are going to be checked
 *
 * Function which is setup needed environment and enables counters.
 */
static void exynos5_dmc_enable_perf_events(struct exynos5_dmc *dmc)
{
	u64 ts;

	/* Enable Performance Event Clock */
	writel(PEREV_CLK_EN, dmc->base_drexi0 + DREX_PPCCLKCON);
	writel(PEREV_CLK_EN, dmc->base_drexi1 + DREX_PPCCLKCON);

	/* Select read transfers as performance event2 */
	writel(READ_TRANSFER_CH0, dmc->base_drexi0 + DREX_PEREV2CONFIG);
	writel(READ_TRANSFER_CH1, dmc->base_drexi1 + DREX_PEREV2CONFIG);

	ts = ktime_get_ns();
	dmc->last_overflow_ts[0] = ts;
	dmc->last_overflow_ts[1] = ts;

	/* Devfreq shouldn't be faster than initialization, play safe though. */
	dmc->load = 99;
	dmc->total = 100;
}

/**
 * exynos5_dmc_disable_perf_events() - Disable performance events
 * @dmc:	device for which the counters are going to be checked
 *
 * Function which stops, disables performance event counters and interrupts.
 */
static void exynos5_dmc_disable_perf_events(struct exynos5_dmc *dmc)
{
	/* Stop all counters */
	writel(0, dmc->base_drexi0 + DREX_PMNC_PPC);
	writel(0, dmc->base_drexi1 + DREX_PMNC_PPC);

	/* Disable interrupts for counter 2 */
	writel(PERF_CNT2, dmc->base_drexi0 + DREX_INTENC_PPC);
	writel(PERF_CNT2, dmc->base_drexi1 + DREX_INTENC_PPC);

	/* Disable counter 2 and CCNT  */
	writel(PERF_CNT2 | PERF_CCNT, dmc->base_drexi0 + DREX_CNTENC_PPC);
	writel(PERF_CNT2 | PERF_CCNT, dmc->base_drexi1 + DREX_CNTENC_PPC);

	/* Clear overflow flag for all counters */
	writel(PERF_CNT2 | PERF_CCNT, dmc->base_drexi0 + DREX_FLAG_PPC);
	writel(PERF_CNT2 | PERF_CCNT, dmc->base_drexi1 + DREX_FLAG_PPC);
}

/**
 * exynos5_dmc_get_status() - Read current DMC performance statistics.
 * @dev:	device for which the statistics are requested
 * @stat:	structure which has statistic fields
 *
 * Function reads the DMC performance counters and calculates 'busy_time'
 * and 'total_time'. To protect from overflow, the values are shifted right
 * by 10. After read out the counters are setup to count again.
 */
static int exynos5_dmc_get_status(struct device *dev,
				  struct devfreq_dev_status *stat)
{
	struct exynos5_dmc *dmc = dev_get_drvdata(dev);
	unsigned long load, total;
	int ret;

	if (dmc->in_irq_mode) {
		mutex_lock(&dmc->lock);
		stat->current_frequency = dmc->curr_rate;
		mutex_unlock(&dmc->lock);

		stat->busy_time = dmc->load;
		stat->total_time = dmc->total;
	} else {
		ret = exynos5_counters_get(dmc, &load, &total);
		if (ret < 0)
			return -EINVAL;

		/* To protect from overflow, divide by 1024 */
		stat->busy_time = load >> 10;
		stat->total_time = total >> 10;

		ret = exynos5_counters_set_event(dmc);
		if (ret < 0) {
			dev_err(dev, "could not set event counter\n");
			return ret;
		}
	}

	return 0;
}

/**
 * exynos5_dmc_get_cur_freq() - Function returns current DMC frequency
 * @dev:	device for which the framework checks operating frequency
 * @freq:	returned frequency value
 *
 * It returns the currently used frequency of the DMC. The real operating
 * frequency might be lower when the clock source value could not be divided
 * to the requested value.
 */
static int exynos5_dmc_get_cur_freq(struct device *dev, unsigned long *freq)
{
	struct exynos5_dmc *dmc = dev_get_drvdata(dev);

	mutex_lock(&dmc->lock);
	*freq = dmc->curr_rate;
	mutex_unlock(&dmc->lock);

	return 0;
}

/*
 * exynos5_dmc_df_profile - Devfreq governor's profile structure
 *
 * It provides to the devfreq framework needed functions and polling period.
 */
static struct devfreq_dev_profile exynos5_dmc_df_profile = {
	.timer = DEVFREQ_TIMER_DELAYED,
	.target = exynos5_dmc_target,
	.get_dev_status = exynos5_dmc_get_status,
	.get_cur_freq = exynos5_dmc_get_cur_freq,
};

/**
 * exynos5_dmc_align_initial_frequency() - Align initial frequency value
 * @dmc:	device for which the frequency is going to be set
 * @bootloader_init_freq:	initial frequency set by the bootloader in KHz
 *
 * The initial bootloader frequency, which is present during boot, might be
 * different that supported frequency values in the driver. It is possible
 * due to different PLL settings or used PLL as a source.
 * This function provides the 'initial_freq' for the devfreq framework
 * statistics engine which supports only registered values. Thus, some alignment
 * must be made.
 */
static unsigned long
exynos5_dmc_align_init_freq(struct exynos5_dmc *dmc,
			    unsigned long bootloader_init_freq)
{
	unsigned long aligned_freq;
	int idx;

	idx = find_target_freq_idx(dmc, bootloader_init_freq);
	if (idx >= 0)
		aligned_freq = dmc->opp[idx].freq_hz;
	else
		aligned_freq = dmc->opp[dmc->opp_count - 1].freq_hz;

	return aligned_freq;
}

/**
 * create_timings_aligned() - Create register values and align with standard
 * @dmc:	device for which the frequency is going to be set
 * @reg_timing_row:	array to fill with values for timing row register
 * @reg_timing_data:	array to fill with values for timing data register
 * @reg_timing_power:	array to fill with values for timing power register
 * @clk_period_ps:	the period of the clock, known as tCK
 *
 * The function calculates timings and creates a register value ready for
 * a frequency transition. The register contains a few timings. They are
 * shifted by a known offset. The timing value is calculated based on memory
 * specyfication: minimal time required and minimal cycles required.
 */
static int create_timings_aligned(struct exynos5_dmc *dmc, u32 *reg_timing_row,
				  u32 *reg_timing_data, u32 *reg_timing_power,
				  u32 clk_period_ps)
{
	u32 val;
	const struct timing_reg *reg;

	if (clk_period_ps == 0)
		return -EINVAL;

	*reg_timing_row = 0;
	*reg_timing_data = 0;
	*reg_timing_power = 0;

	val = dmc->timings->tRFC / clk_period_ps;
	val += dmc->timings->tRFC % clk_period_ps ? 1 : 0;
	val = max(val, dmc->min_tck->tRFC);
	reg = &timing_row_reg_fields[0];
	*reg_timing_row |= TIMING_VAL2REG(reg, val);

	val = dmc->timings->tRRD / clk_period_ps;
	val += dmc->timings->tRRD % clk_period_ps ? 1 : 0;
	val = max(val, dmc->min_tck->tRRD);
	reg = &timing_row_reg_fields[1];
	*reg_timing_row |= TIMING_VAL2REG(reg, val);

	val = dmc->timings->tRPab / clk_period_ps;
	val += dmc->timings->tRPab % clk_period_ps ? 1 : 0;
	val = max(val, dmc->min_tck->tRPab);
	reg = &timing_row_reg_fields[2];
	*reg_timing_row |= TIMING_VAL2REG(reg, val);

	val = dmc->timings->tRCD / clk_period_ps;
	val += dmc->timings->tRCD % clk_period_ps ? 1 : 0;
	val = max(val, dmc->min_tck->tRCD);
	reg = &timing_row_reg_fields[3];
	*reg_timing_row |= TIMING_VAL2REG(reg, val);

	val = dmc->timings->tRC / clk_period_ps;
	val += dmc->timings->tRC % clk_period_ps ? 1 : 0;
	val = max(val, dmc->min_tck->tRC);
	reg = &timing_row_reg_fields[4];
	*reg_timing_row |= TIMING_VAL2REG(reg, val);

	val = dmc->timings->tRAS / clk_period_ps;
	val += dmc->timings->tRAS % clk_period_ps ? 1 : 0;
	val = max(val, dmc->min_tck->tRAS);
	reg = &timing_row_reg_fields[5];
	*reg_timing_row |= TIMING_VAL2REG(reg, val);

	/* data related timings */
	val = dmc->timings->tWTR / clk_period_ps;
	val += dmc->timings->tWTR % clk_period_ps ? 1 : 0;
	val = max(val, dmc->min_tck->tWTR);
	reg = &timing_data_reg_fields[0];
	*reg_timing_data |= TIMING_VAL2REG(reg, val);

	val = dmc->timings->tWR / clk_period_ps;
	val += dmc->timings->tWR % clk_period_ps ? 1 : 0;
	val = max(val, dmc->min_tck->tWR);
	reg = &timing_data_reg_fields[1];
	*reg_timing_data |= TIMING_VAL2REG(reg, val);

	val = dmc->timings->tRTP / clk_period_ps;
	val += dmc->timings->tRTP % clk_period_ps ? 1 : 0;
	val = max(val, dmc->min_tck->tRTP);
	reg = &timing_data_reg_fields[2];
	*reg_timing_data |= TIMING_VAL2REG(reg, val);

	val = dmc->timings->tW2W_C2C / clk_period_ps;
	val += dmc->timings->tW2W_C2C % clk_period_ps ? 1 : 0;
	val = max(val, dmc->min_tck->tW2W_C2C);
	reg = &timing_data_reg_fields[3];
	*reg_timing_data |= TIMING_VAL2REG(reg, val);

	val = dmc->timings->tR2R_C2C / clk_period_ps;
	val += dmc->timings->tR2R_C2C % clk_period_ps ? 1 : 0;
	val = max(val, dmc->min_tck->tR2R_C2C);
	reg = &timing_data_reg_fields[4];
	*reg_timing_data |= TIMING_VAL2REG(reg, val);

	val = dmc->timings->tWL / clk_period_ps;
	val += dmc->timings->tWL % clk_period_ps ? 1 : 0;
	val = max(val, dmc->min_tck->tWL);
	reg = &timing_data_reg_fields[5];
	*reg_timing_data |= TIMING_VAL2REG(reg, val);

	val = dmc->timings->tDQSCK / clk_period_ps;
	val += dmc->timings->tDQSCK % clk_period_ps ? 1 : 0;
	val = max(val, dmc->min_tck->tDQSCK);
	reg = &timing_data_reg_fields[6];
	*reg_timing_data |= TIMING_VAL2REG(reg, val);

	val = dmc->timings->tRL / clk_period_ps;
	val += dmc->timings->tRL % clk_period_ps ? 1 : 0;
	val = max(val, dmc->min_tck->tRL);
	reg = &timing_data_reg_fields[7];
	*reg_timing_data |= TIMING_VAL2REG(reg, val);

	/* power related timings */
	val = dmc->timings->tFAW / clk_period_ps;
	val += dmc->timings->tFAW % clk_period_ps ? 1 : 0;
	val = max(val, dmc->min_tck->tFAW);
	reg = &timing_power_reg_fields[0];
	*reg_timing_power |= TIMING_VAL2REG(reg, val);

	val = dmc->timings->tXSR / clk_period_ps;
	val += dmc->timings->tXSR % clk_period_ps ? 1 : 0;
	val = max(val, dmc->min_tck->tXSR);
	reg = &timing_power_reg_fields[1];
	*reg_timing_power |= TIMING_VAL2REG(reg, val);

	val = dmc->timings->tXP / clk_period_ps;
	val += dmc->timings->tXP % clk_period_ps ? 1 : 0;
	val = max(val, dmc->min_tck->tXP);
	reg = &timing_power_reg_fields[2];
	*reg_timing_power |= TIMING_VAL2REG(reg, val);

	val = dmc->timings->tCKE / clk_period_ps;
	val += dmc->timings->tCKE % clk_period_ps ? 1 : 0;
	val = max(val, dmc->min_tck->tCKE);
	reg = &timing_power_reg_fields[3];
	*reg_timing_power |= TIMING_VAL2REG(reg, val);

	val = dmc->timings->tMRD / clk_period_ps;
	val += dmc->timings->tMRD % clk_period_ps ? 1 : 0;
	val = max(val, dmc->min_tck->tMRD);
	reg = &timing_power_reg_fields[4];
	*reg_timing_power |= TIMING_VAL2REG(reg, val);

	return 0;
}

/**
 * of_get_dram_timings() - helper function for parsing DT settings for DRAM
 * @dmc:        device for which the frequency is going to be set
 *
 * The function parses DT entries with DRAM information.
 */
static int of_get_dram_timings(struct exynos5_dmc *dmc)
{
	int ret = 0;
	int idx;
	struct device_node *np_ddr;
	u32 freq_mhz, clk_period_ps;

	np_ddr = of_parse_phandle(dmc->dev->of_node, "device-handle", 0);
	if (!np_ddr) {
		dev_warn(dmc->dev, "could not find 'device-handle' in DT\n");
		return -EINVAL;
	}

	dmc->timing_row = devm_kmalloc_array(dmc->dev, TIMING_COUNT,
					     sizeof(u32), GFP_KERNEL);
	if (!dmc->timing_row)
		return -ENOMEM;

	dmc->timing_data = devm_kmalloc_array(dmc->dev, TIMING_COUNT,
					      sizeof(u32), GFP_KERNEL);
	if (!dmc->timing_data)
		return -ENOMEM;

	dmc->timing_power = devm_kmalloc_array(dmc->dev, TIMING_COUNT,
					       sizeof(u32), GFP_KERNEL);
	if (!dmc->timing_power)
		return -ENOMEM;

	dmc->timings = of_lpddr3_get_ddr_timings(np_ddr, dmc->dev,
						 DDR_TYPE_LPDDR3,
						 &dmc->timings_arr_size);
	if (!dmc->timings) {
		of_node_put(np_ddr);
		dev_warn(dmc->dev, "could not get timings from DT\n");
		return -EINVAL;
	}

	dmc->min_tck = of_lpddr3_get_min_tck(np_ddr, dmc->dev);
	if (!dmc->min_tck) {
		of_node_put(np_ddr);
		dev_warn(dmc->dev, "could not get tck from DT\n");
		return -EINVAL;
	}

	/* Sorted array of OPPs with frequency ascending */
	for (idx = 0; idx < dmc->opp_count; idx++) {
		freq_mhz = dmc->opp[idx].freq_hz / 1000000;
		clk_period_ps = 1000000 / freq_mhz;

		ret = create_timings_aligned(dmc, &dmc->timing_row[idx],
					     &dmc->timing_data[idx],
					     &dmc->timing_power[idx],
					     clk_period_ps);
	}

	of_node_put(np_ddr);

	/* Take the highest frequency's timings as 'bypass' */
	dmc->bypass_timing_row = dmc->timing_row[idx - 1];
	dmc->bypass_timing_data = dmc->timing_data[idx - 1];
	dmc->bypass_timing_power = dmc->timing_power[idx - 1];

	return ret;
}

/**
 * exynos5_dmc_init_clks() - Initialize clocks needed for DMC operation.
 * @dmc:	DMC structure containing needed fields
 *
 * Get the needed clocks defined in DT device, enable and set the right parents.
 * Read current frequency and initialize the initial rate for governor.
 */
static int exynos5_dmc_init_clks(struct exynos5_dmc *dmc)
{
	int ret;
	unsigned long target_volt = 0;
	unsigned long target_rate = 0;
	unsigned int tmp;

	dmc->fout_spll = devm_clk_get(dmc->dev, "fout_spll");
	if (IS_ERR(dmc->fout_spll))
		return PTR_ERR(dmc->fout_spll);

	dmc->fout_bpll = devm_clk_get(dmc->dev, "fout_bpll");
	if (IS_ERR(dmc->fout_bpll))
		return PTR_ERR(dmc->fout_bpll);

	dmc->mout_mclk_cdrex = devm_clk_get(dmc->dev, "mout_mclk_cdrex");
	if (IS_ERR(dmc->mout_mclk_cdrex))
		return PTR_ERR(dmc->mout_mclk_cdrex);

	dmc->mout_bpll = devm_clk_get(dmc->dev, "mout_bpll");
	if (IS_ERR(dmc->mout_bpll))
		return PTR_ERR(dmc->mout_bpll);

	dmc->mout_mx_mspll_ccore = devm_clk_get(dmc->dev,
						"mout_mx_mspll_ccore");
	if (IS_ERR(dmc->mout_mx_mspll_ccore))
		return PTR_ERR(dmc->mout_mx_mspll_ccore);

	dmc->mout_spll = devm_clk_get(dmc->dev, "ff_dout_spll2");
	if (IS_ERR(dmc->mout_spll)) {
		dmc->mout_spll = devm_clk_get(dmc->dev, "mout_sclk_spll");
		if (IS_ERR(dmc->mout_spll))
			return PTR_ERR(dmc->mout_spll);
	}

	/*
	 * Convert frequency to KHz values and set it for the governor.
	 */
	dmc->curr_rate = clk_get_rate(dmc->mout_mclk_cdrex);
	dmc->curr_rate = exynos5_dmc_align_init_freq(dmc, dmc->curr_rate);
	exynos5_dmc_df_profile.initial_freq = dmc->curr_rate;

	ret = exynos5_dmc_get_volt_freq(dmc, &dmc->curr_rate, &target_rate,
					&target_volt, 0);
	if (ret)
		return ret;

	dmc->curr_volt = target_volt;

	ret = clk_set_parent(dmc->mout_mx_mspll_ccore, dmc->mout_spll);
	if (ret)
		return ret;

	clk_prepare_enable(dmc->fout_bpll);
	clk_prepare_enable(dmc->mout_bpll);

	/*
	 * Some bootloaders do not set clock routes correctly.
	 * Stop one path in clocks to PHY.
	 */
	regmap_read(dmc->clk_regmap, CDREX_LPDDR3PHY_CLKM_SRC, &tmp);
	tmp &= ~(BIT(1) | BIT(0));
	regmap_write(dmc->clk_regmap, CDREX_LPDDR3PHY_CLKM_SRC, tmp);

	return 0;
}

/**
 * exynos5_performance_counters_init() - Initializes performance DMC's counters
 * @dmc:	DMC for which it does the setup
 *
 * Initialization of performance counters in DMC for estimating usage.
 * The counter's values are used for calculation of a memory bandwidth and based
 * on that the governor changes the frequency.
 * The counters are not used when the governor is GOVERNOR_USERSPACE.
 */
static int exynos5_performance_counters_init(struct exynos5_dmc *dmc)
{
	int ret, i;

	dmc->num_counters = devfreq_event_get_edev_count(dmc->dev,
							"devfreq-events");
	if (dmc->num_counters < 0) {
		dev_err(dmc->dev, "could not get devfreq-event counters\n");
		return dmc->num_counters;
	}

	dmc->counter = devm_kcalloc(dmc->dev, dmc->num_counters,
				    sizeof(*dmc->counter), GFP_KERNEL);
	if (!dmc->counter)
		return -ENOMEM;

	for (i = 0; i < dmc->num_counters; i++) {
		dmc->counter[i] =
			devfreq_event_get_edev_by_phandle(dmc->dev,
						"devfreq-events", i);
		if (IS_ERR_OR_NULL(dmc->counter[i]))
			return -EPROBE_DEFER;
	}

	ret = exynos5_counters_enable_edev(dmc);
	if (ret < 0) {
		dev_err(dmc->dev, "could not enable event counter\n");
		return ret;
	}

	ret = exynos5_counters_set_event(dmc);
	if (ret < 0) {
		exynos5_counters_disable_edev(dmc);
		dev_err(dmc->dev, "could not set event counter\n");
		return ret;
	}

	return 0;
}

/**
 * exynos5_dmc_set_pause_on_switching() - Controls a pause feature in DMC
 * @dmc:	device which is used for changing this feature
 *
 * There is a need of pausing DREX DMC when divider or MUX in clock tree
 * changes its configuration. In such situation access to the memory is blocked
 * in DMC automatically. This feature is used when clock frequency change
 * request appears and touches clock tree.
 */
static inline int exynos5_dmc_set_pause_on_switching(struct exynos5_dmc *dmc)
{
	unsigned int val;
	int ret;

	ret = regmap_read(dmc->clk_regmap, CDREX_PAUSE, &val);
	if (ret)
		return ret;

	val |= 1UL;
	regmap_write(dmc->clk_regmap, CDREX_PAUSE, val);

	return 0;
}

static irqreturn_t dmc_irq_thread(int irq, void *priv)
{
	int res;
	struct exynos5_dmc *dmc = priv;

	mutex_lock(&dmc->df->lock);
	exynos5_dmc_perf_events_check(dmc);
	res = update_devfreq(dmc->df);
	mutex_unlock(&dmc->df->lock);

	if (res)
		dev_warn(dmc->dev, "devfreq failed with %d\n", res);

	return IRQ_HANDLED;
}

/**
 * exynos5_dmc_probe() - Probe function for the DMC driver
 * @pdev:	platform device for which the driver is going to be initialized
 *
 * Initialize basic components: clocks, regulators, performance counters, etc.
 * Read out product version and based on the information setup
 * internal structures for the controller (frequency and voltage) and for DRAM
 * memory parameters: timings for each operating frequency.
 * Register new devfreq device for controlling DVFS of the DMC.
 */
static int exynos5_dmc_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct exynos5_dmc *dmc;
	int irq[2];

	dmc = devm_kzalloc(dev, sizeof(*dmc), GFP_KERNEL);
	if (!dmc)
		return -ENOMEM;

	mutex_init(&dmc->lock);

	dmc->dev = dev;
	platform_set_drvdata(pdev, dmc);

	dmc->base_drexi0 = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(dmc->base_drexi0))
		return PTR_ERR(dmc->base_drexi0);

	dmc->base_drexi1 = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(dmc->base_drexi1))
		return PTR_ERR(dmc->base_drexi1);

	dmc->clk_regmap = syscon_regmap_lookup_by_phandle(np,
							  "samsung,syscon-clk");
	if (IS_ERR(dmc->clk_regmap))
		return PTR_ERR(dmc->clk_regmap);

	ret = exynos5_init_freq_table(dmc, &exynos5_dmc_df_profile);
	if (ret) {
		dev_warn(dev, "couldn't initialize frequency settings\n");
		return ret;
	}

	dmc->vdd_mif = devm_regulator_get(dev, "vdd");
	if (IS_ERR(dmc->vdd_mif)) {
		ret = PTR_ERR(dmc->vdd_mif);
		return ret;
	}

	ret = exynos5_dmc_init_clks(dmc);
	if (ret)
		return ret;

	ret = of_get_dram_timings(dmc);
	if (ret) {
		dev_warn(dev, "couldn't initialize timings settings\n");
		goto remove_clocks;
	}

	ret = exynos5_dmc_set_pause_on_switching(dmc);
	if (ret) {
		dev_warn(dev, "couldn't get access to PAUSE register\n");
		goto remove_clocks;
	}

	/* There is two modes in which the driver works: polling or IRQ */
	irq[0] = platform_get_irq_byname(pdev, "drex_0");
	irq[1] = platform_get_irq_byname(pdev, "drex_1");
	if (irq[0] > 0 && irq[1] > 0 && irqmode) {
		ret = devm_request_threaded_irq(dev, irq[0], NULL,
						dmc_irq_thread, IRQF_ONESHOT,
						dev_name(dev), dmc);
		if (ret) {
			dev_err(dev, "couldn't grab IRQ\n");
			goto remove_clocks;
		}

		ret = devm_request_threaded_irq(dev, irq[1], NULL,
						dmc_irq_thread, IRQF_ONESHOT,
						dev_name(dev), dmc);
		if (ret) {
			dev_err(dev, "couldn't grab IRQ\n");
			goto remove_clocks;
		}

		/*
		 * Setup default thresholds for the devfreq governor.
		 * The values are chosen based on experiments.
		 */
		dmc->gov_data.upthreshold = 55;
		dmc->gov_data.downdifferential = 5;

		exynos5_dmc_enable_perf_events(dmc);

		dmc->in_irq_mode = 1;
	} else {
		ret = exynos5_performance_counters_init(dmc);
		if (ret) {
			dev_warn(dev, "couldn't probe performance counters\n");
			goto remove_clocks;
		}

		/*
		 * Setup default thresholds for the devfreq governor.
		 * The values are chosen based on experiments.
		 */
		dmc->gov_data.upthreshold = 10;
		dmc->gov_data.downdifferential = 5;

		exynos5_dmc_df_profile.polling_ms = 100;
	}

	dmc->df = devm_devfreq_add_device(dev, &exynos5_dmc_df_profile,
					  DEVFREQ_GOV_SIMPLE_ONDEMAND,
					  &dmc->gov_data);

	if (IS_ERR(dmc->df)) {
		ret = PTR_ERR(dmc->df);
		goto err_devfreq_add;
	}

	if (dmc->in_irq_mode)
		exynos5_dmc_start_perf_events(dmc, PERF_COUNTER_START_VALUE);

	dev_info(dev, "DMC initialized, in irq mode: %d\n", dmc->in_irq_mode);

	return 0;

err_devfreq_add:
	if (dmc->in_irq_mode)
		exynos5_dmc_disable_perf_events(dmc);
	else
		exynos5_counters_disable_edev(dmc);
remove_clocks:
	clk_disable_unprepare(dmc->mout_bpll);
	clk_disable_unprepare(dmc->fout_bpll);

	return ret;
}

/**
 * exynos5_dmc_remove() - Remove function for the platform device
 * @pdev:	platform device which is going to be removed
 *
 * The function relies on 'devm' framework function which automatically
 * clean the device's resources. It just calls explicitly disable function for
 * the performance counters.
 */
static int exynos5_dmc_remove(struct platform_device *pdev)
{
	struct exynos5_dmc *dmc = dev_get_drvdata(&pdev->dev);

	if (dmc->in_irq_mode)
		exynos5_dmc_disable_perf_events(dmc);
	else
		exynos5_counters_disable_edev(dmc);

	clk_disable_unprepare(dmc->mout_bpll);
	clk_disable_unprepare(dmc->fout_bpll);

	dev_pm_opp_remove_table(dmc->dev);

	return 0;
}

static const struct of_device_id exynos5_dmc_of_match[] = {
	{ .compatible = "samsung,exynos5422-dmc", },
	{ },
};
MODULE_DEVICE_TABLE(of, exynos5_dmc_of_match);

static struct platform_driver exynos5_dmc_platdrv = {
	.probe	= exynos5_dmc_probe,
	.remove = exynos5_dmc_remove,
	.driver = {
		.name	= "exynos5-dmc",
		.of_match_table = exynos5_dmc_of_match,
	},
};
module_platform_driver(exynos5_dmc_platdrv);
MODULE_DESCRIPTION("Driver for Exynos5422 Dynamic Memory Controller dynamic frequency and voltage change");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Lukasz Luba");
