/*
 * clk-dfll.c - Tegra DFLL clock source common code
 *
 * Copyright (C) 2012-2019 NVIDIA Corporation. All rights reserved.
 *
 * Aleksandr Frid <afrid@nvidia.com>
 * Paul Walmsley <pwalmsley@nvidia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * This library is for the DVCO and DFLL IP blocks on the Tegra124
 * SoC. These IP blocks together are also known at NVIDIA as
 * "CL-DVFS". To try to avoid confusion, this code refers to them
 * collectively as the "DFLL."
 *
 * The DFLL is a root clocksource which tolerates some amount of
 * supply voltage noise. Tegra124 uses it to clock the fast CPU
 * complex when the target CPU speed is above a particular rate. The
 * DFLL can be operated in either open-loop mode or closed-loop mode.
 * In open-loop mode, the DFLL generates an output clock appropriate
 * to the supply voltage. In closed-loop mode, when configured with a
 * target frequency, the DFLL minimizes supply voltage while
 * delivering an average frequency equal to the target.
 *
 * Devices clocked by the DFLL must be able to tolerate frequency
 * variation. In the case of the CPU, it's important to note that the
 * CPU cycle time will vary. This has implications for
 * performance-measurement code and any code that relies on the CPU
 * cycle time to delay for a certain length of time.
 *
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pm_opp.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>
#include <linux/seq_file.h>

#include "clk-dfll.h"
#include "cvb.h"

/*
 * DFLL control registers - access via dfll_{readl,writel}
 */

/* DFLL_CTRL: DFLL control register */
#define DFLL_CTRL			0x00
#define DFLL_CTRL_MODE_MASK		0x03

/* DFLL_CONFIG: DFLL sample rate control */
#define DFLL_CONFIG			0x04
#define DFLL_CONFIG_DIV_MASK		0xff
#define DFLL_CONFIG_DIV_PRESCALE	32

/* DFLL_PARAMS: tuning coefficients for closed loop integrator */
#define DFLL_PARAMS			0x08
#define DFLL_PARAMS_CG_SCALE		(0x1 << 24)
#define DFLL_PARAMS_FORCE_MODE_SHIFT	22
#define DFLL_PARAMS_FORCE_MODE_MASK	(0x3 << DFLL_PARAMS_FORCE_MODE_SHIFT)
#define DFLL_PARAMS_CF_PARAM_SHIFT	16
#define DFLL_PARAMS_CF_PARAM_MASK	(0x3f << DFLL_PARAMS_CF_PARAM_SHIFT)
#define DFLL_PARAMS_CI_PARAM_SHIFT	8
#define DFLL_PARAMS_CI_PARAM_MASK	(0x7 << DFLL_PARAMS_CI_PARAM_SHIFT)
#define DFLL_PARAMS_CG_PARAM_SHIFT	0
#define DFLL_PARAMS_CG_PARAM_MASK	(0xff << DFLL_PARAMS_CG_PARAM_SHIFT)

/* DFLL_TUNE0: delay line configuration register 0 */
#define DFLL_TUNE0			0x0c

/* DFLL_TUNE1: delay line configuration register 1 */
#define DFLL_TUNE1			0x10

/* DFLL_FREQ_REQ: target DFLL frequency control */
#define DFLL_FREQ_REQ			0x14
#define DFLL_FREQ_REQ_FORCE_ENABLE	(0x1 << 28)
#define DFLL_FREQ_REQ_FORCE_SHIFT	16
#define DFLL_FREQ_REQ_FORCE_MASK	(0xfff << DFLL_FREQ_REQ_FORCE_SHIFT)
#define FORCE_MAX			2047
#define FORCE_MIN			-2048
#define DFLL_FREQ_REQ_SCALE_SHIFT	8
#define DFLL_FREQ_REQ_SCALE_MASK	(0xff << DFLL_FREQ_REQ_SCALE_SHIFT)
#define DFLL_FREQ_REQ_SCALE_MAX		256
#define DFLL_FREQ_REQ_FREQ_VALID	(0x1 << 7)
#define DFLL_FREQ_REQ_MULT_SHIFT	0
#define DFLL_FREQ_REG_MULT_MASK		(0x7f << DFLL_FREQ_REQ_MULT_SHIFT)
#define FREQ_MAX			127

/* DFLL_DROOP_CTRL: droop prevention control */
#define DFLL_DROOP_CTRL			0x1c

/* DFLL_OUTPUT_CFG: closed loop mode control registers */
/* NOTE: access via dfll_i2c_{readl,writel} */
#define DFLL_OUTPUT_CFG			0x20
#define DFLL_OUTPUT_CFG_I2C_ENABLE	(0x1 << 30)
#define OUT_MASK			0x3f
#define DFLL_OUTPUT_CFG_SAFE_SHIFT	24
#define DFLL_OUTPUT_CFG_SAFE_MASK	\
		(OUT_MASK << DFLL_OUTPUT_CFG_SAFE_SHIFT)
#define DFLL_OUTPUT_CFG_MAX_SHIFT	16
#define DFLL_OUTPUT_CFG_MAX_MASK	\
		(OUT_MASK << DFLL_OUTPUT_CFG_MAX_SHIFT)
#define DFLL_OUTPUT_CFG_MIN_SHIFT	8
#define DFLL_OUTPUT_CFG_MIN_MASK	\
		(OUT_MASK << DFLL_OUTPUT_CFG_MIN_SHIFT)
#define DFLL_OUTPUT_CFG_PWM_DELTA	(0x1 << 7)
#define DFLL_OUTPUT_CFG_PWM_ENABLE	(0x1 << 6)
#define DFLL_OUTPUT_CFG_PWM_DIV_SHIFT	0
#define DFLL_OUTPUT_CFG_PWM_DIV_MASK	\
		(OUT_MASK << DFLL_OUTPUT_CFG_PWM_DIV_SHIFT)

/* DFLL_OUTPUT_FORCE: closed loop mode voltage forcing control */
#define DFLL_OUTPUT_FORCE		0x24
#define DFLL_OUTPUT_FORCE_ENABLE	(0x1 << 6)
#define DFLL_OUTPUT_FORCE_VALUE_SHIFT	0
#define DFLL_OUTPUT_FORCE_VALUE_MASK	\
		(OUT_MASK << DFLL_OUTPUT_FORCE_VALUE_SHIFT)

/* DFLL_MONITOR_CTRL: internal monitor data source control */
#define DFLL_MONITOR_CTRL		0x28
#define DFLL_MONITOR_CTRL_FREQ		6

/* DFLL_MONITOR_DATA: internal monitor data output */
#define DFLL_MONITOR_DATA		0x2c
#define DFLL_MONITOR_DATA_NEW_MASK	(0x1 << 16)
#define DFLL_MONITOR_DATA_VAL_SHIFT	0
#define DFLL_MONITOR_DATA_VAL_MASK	(0xFFFF << DFLL_MONITOR_DATA_VAL_SHIFT)

/*
 * I2C output control registers - access via dfll_i2c_{readl,writel}
 */

/* DFLL_I2C_CFG: I2C controller configuration register */
#define DFLL_I2C_CFG			0x40
#define DFLL_I2C_CFG_ARB_ENABLE		(0x1 << 20)
#define DFLL_I2C_CFG_HS_CODE_SHIFT	16
#define DFLL_I2C_CFG_HS_CODE_MASK	(0x7 << DFLL_I2C_CFG_HS_CODE_SHIFT)
#define DFLL_I2C_CFG_PACKET_ENABLE	(0x1 << 15)
#define DFLL_I2C_CFG_SIZE_SHIFT		12
#define DFLL_I2C_CFG_SIZE_MASK		(0x7 << DFLL_I2C_CFG_SIZE_SHIFT)
#define DFLL_I2C_CFG_SLAVE_ADDR_10	(0x1 << 10)
#define DFLL_I2C_CFG_SLAVE_ADDR_SHIFT_7BIT	1
#define DFLL_I2C_CFG_SLAVE_ADDR_SHIFT_10BIT	0

/* DFLL_I2C_VDD_REG_ADDR: PMIC I2C address for closed loop mode */
#define DFLL_I2C_VDD_REG_ADDR		0x44

/* DFLL_I2C_STS: I2C controller status */
#define DFLL_I2C_STS			0x48
#define DFLL_I2C_STS_I2C_LAST_SHIFT	1
#define DFLL_I2C_STS_I2C_REQ_PENDING	0x1

/* DFLL_INTR_STS: DFLL interrupt status register */
#define DFLL_INTR_STS			0x5c

/* DFLL_INTR_EN: DFLL interrupt enable register */
#define DFLL_INTR_EN			0x60
#define DFLL_INTR_MIN_MASK		0x1
#define DFLL_INTR_MAX_MASK		0x2

/*
 * Integrated I2C controller registers - relative to td->i2c_controller_base
 */

/* DFLL_I2C_CLK_DIVISOR: I2C controller clock divisor */
#define DFLL_I2C_CLK_DIVISOR		0x6c
#define DFLL_I2C_CLK_DIVISOR_MASK	0xffff
#define DFLL_I2C_CLK_DIVISOR_FS_SHIFT	16
#define DFLL_I2C_CLK_DIVISOR_HS_SHIFT	0
#define DFLL_I2C_CLK_DIVISOR_PREDIV	8
#define DFLL_I2C_CLK_DIVISOR_HSMODE_PREDIV	12

/*
 * Other constants
 */

/* MAX_DFLL_VOLTAGES: number of LUT entries in the DFLL IP block */
#define MAX_DFLL_VOLTAGES		33

/*
 * REF_CLK_CYC_PER_DVCO_SAMPLE: the number of ref_clk cycles that the hardware
 *    integrates the DVCO counter over - used for debug rate monitoring and
 *    droop control
 */
#define REF_CLK_CYC_PER_DVCO_SAMPLE	4

/*
 * REF_CLOCK_RATE: the DFLL reference clock rate currently supported by this
 * driver, in Hz
 */
#define REF_CLOCK_RATE			51000000UL

#define DVCO_RATE_TO_MULT(rate, ref_rate)	((rate) / ((ref_rate) / 2))
#define MULT_TO_DVCO_RATE(mult, ref_rate)	((mult) * ((ref_rate) / 2))

/**
 * enum dfll_ctrl_mode - DFLL hardware operating mode
 * @DFLL_UNINITIALIZED: (uninitialized state - not in hardware bitfield)
 * @DFLL_DISABLED: DFLL not generating an output clock
 * @DFLL_OPEN_LOOP: DVCO running, but DFLL not adjusting voltage
 * @DFLL_CLOSED_LOOP: DVCO running, and DFLL adjusting voltage to match
 *		      the requested rate
 *
 * The integer corresponding to the last two states, minus one, is
 * written to the DFLL hardware to change operating modes.
 */
enum dfll_ctrl_mode {
	DFLL_UNINITIALIZED = 0,
	DFLL_DISABLED = 1,
	DFLL_OPEN_LOOP = 2,
	DFLL_CLOSED_LOOP = 3,
};

/**
 * enum dfll_tune_range - voltage range that the driver believes it's in
 * @DFLL_TUNE_UNINITIALIZED: DFLL tuning not yet programmed
 * @DFLL_TUNE_LOW: DFLL in the low-voltage range (or open-loop mode)
 *
 * Some DFLL tuning parameters may need to change depending on the
 * DVCO's voltage; these states represent the ranges that the driver
 * supports. These are software states; these values are never
 * written into registers.
 */
enum dfll_tune_range {
	DFLL_TUNE_UNINITIALIZED = 0,
	DFLL_TUNE_LOW = 1,
};


enum tegra_dfll_pmu_if {
	TEGRA_DFLL_PMU_I2C = 0,
	TEGRA_DFLL_PMU_PWM = 1,
};

/**
 * struct dfll_rate_req - target DFLL rate request data
 * @rate: target frequency, after the postscaling
 * @dvco_target_rate: target frequency, after the postscaling
 * @lut_index: LUT index at which voltage the dvco_target_rate will be reached
 * @mult_bits: value to program to the MULT bits of the DFLL_FREQ_REQ register
 * @scale_bits: value to program to the SCALE bits of the DFLL_FREQ_REQ register
 */
struct dfll_rate_req {
	unsigned long rate;
	unsigned long dvco_target_rate;
	int lut_index;
	u8 mult_bits;
	u8 scale_bits;
};

struct tegra_dfll {
	struct device			*dev;
	struct tegra_dfll_soc_data	*soc;

	void __iomem			*base;
	void __iomem			*i2c_base;
	void __iomem			*i2c_controller_base;
	void __iomem			*lut_base;

	struct regulator		*vdd_reg;
	struct clk			*soc_clk;
	struct clk			*ref_clk;
	struct clk			*i2c_clk;
	struct clk			*dfll_clk;
	struct reset_control		*dvco_rst;
	unsigned long			ref_rate;
	unsigned long			i2c_clk_rate;
	unsigned long			dvco_rate_min;

	enum dfll_ctrl_mode		mode;
	enum dfll_tune_range		tune_range;
	struct dentry			*debugfs_dir;
	struct clk_hw			dfll_clk_hw;
	const char			*output_clock_name;
	struct dfll_rate_req		last_req;
	unsigned long			last_unrounded_rate;

	/* Parameters from DT */
	u32				droop_ctrl;
	u32				sample_rate;
	u32				force_mode;
	u32				cf;
	u32				ci;
	u32				cg;
	bool				cg_scale;

	/* I2C interface parameters */
	u32				i2c_fs_rate;
	u32				i2c_reg;
	u32				i2c_slave_addr;

	/* lut array entries are regulator framework selectors or PWM values*/
	unsigned			lut[MAX_DFLL_VOLTAGES];
	unsigned long			lut_uv[MAX_DFLL_VOLTAGES];
	int				lut_size;
	u8				lut_bottom, lut_min, lut_max, lut_safe;

	/* PWM interface */
	enum tegra_dfll_pmu_if		pmu_if;
	unsigned long			pwm_rate;
	struct pinctrl			*pwm_pin;
	struct pinctrl_state		*pwm_enable_state;
	struct pinctrl_state		*pwm_disable_state;
	u32				reg_init_uV;
};

#define clk_hw_to_dfll(_hw) container_of(_hw, struct tegra_dfll, dfll_clk_hw)

/* mode_name: map numeric DFLL modes to names for friendly console messages */
static const char * const mode_name[] = {
	[DFLL_UNINITIALIZED] = "uninitialized",
	[DFLL_DISABLED] = "disabled",
	[DFLL_OPEN_LOOP] = "open_loop",
	[DFLL_CLOSED_LOOP] = "closed_loop",
};

/*
 * Register accessors
 */

static inline u32 dfll_readl(struct tegra_dfll *td, u32 offs)
{
	return __raw_readl(td->base + offs);
}

static inline void dfll_writel(struct tegra_dfll *td, u32 val, u32 offs)
{
	WARN_ON(offs >= DFLL_I2C_CFG);
	__raw_writel(val, td->base + offs);
}

static inline void dfll_wmb(struct tegra_dfll *td)
{
	dfll_readl(td, DFLL_CTRL);
}

/* I2C output control registers - for addresses above DFLL_I2C_CFG */

static inline u32 dfll_i2c_readl(struct tegra_dfll *td, u32 offs)
{
	return __raw_readl(td->i2c_base + offs);
}

static inline void dfll_i2c_writel(struct tegra_dfll *td, u32 val, u32 offs)
{
	__raw_writel(val, td->i2c_base + offs);
}

static inline void dfll_i2c_wmb(struct tegra_dfll *td)
{
	dfll_i2c_readl(td, DFLL_I2C_CFG);
}

/**
 * dfll_is_running - is the DFLL currently generating a clock?
 * @td: DFLL instance
 *
 * If the DFLL is currently generating an output clock signal, return
 * true; otherwise return false.
 */
static bool dfll_is_running(struct tegra_dfll *td)
{
	return td->mode >= DFLL_OPEN_LOOP;
}

/*
 * Runtime PM suspend/resume callbacks
 */

/**
 * tegra_dfll_runtime_resume - enable all clocks needed by the DFLL
 * @dev: DFLL device *
 *
 * Enable all clocks needed by the DFLL. Assumes that clk_prepare()
 * has already been called on all the clocks.
 *
 * XXX Should also handle context restore when returning from off.
 */
int tegra_dfll_runtime_resume(struct device *dev)
{
	struct tegra_dfll *td = dev_get_drvdata(dev);
	int ret;

	ret = clk_enable(td->ref_clk);
	if (ret) {
		dev_err(dev, "could not enable ref clock: %d\n", ret);
		return ret;
	}

	ret = clk_enable(td->soc_clk);
	if (ret) {
		dev_err(dev, "could not enable register clock: %d\n", ret);
		clk_disable(td->ref_clk);
		return ret;
	}

	ret = clk_enable(td->i2c_clk);
	if (ret) {
		dev_err(dev, "could not enable i2c clock: %d\n", ret);
		clk_disable(td->soc_clk);
		clk_disable(td->ref_clk);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(tegra_dfll_runtime_resume);

/**
 * tegra_dfll_runtime_suspend - disable all clocks needed by the DFLL
 * @dev: DFLL device *
 *
 * Disable all clocks needed by the DFLL. Assumes that other code
 * will later call clk_unprepare().
 */
int tegra_dfll_runtime_suspend(struct device *dev)
{
	struct tegra_dfll *td = dev_get_drvdata(dev);

	clk_disable(td->ref_clk);
	clk_disable(td->soc_clk);
	clk_disable(td->i2c_clk);

	return 0;
}
EXPORT_SYMBOL(tegra_dfll_runtime_suspend);

/*
 * DFLL tuning operations (per-voltage-range tuning settings)
 */

/**
 * dfll_tune_low - tune to DFLL and CPU settings valid for any voltage
 * @td: DFLL instance
 *
 * Tune the DFLL oscillator parameters and the CPU clock shaper for
 * the low-voltage range. These settings are valid for any voltage,
 * but may not be optimal.
 */
static void dfll_tune_low(struct tegra_dfll *td)
{
	td->tune_range = DFLL_TUNE_LOW;

	dfll_writel(td, td->soc->cvb->cpu_dfll_data.tune0_low, DFLL_TUNE0);
	dfll_writel(td, td->soc->cvb->cpu_dfll_data.tune1, DFLL_TUNE1);
	dfll_wmb(td);

	if (td->soc->set_clock_trimmers_low)
		td->soc->set_clock_trimmers_low();
}

/*
 * Output clock scaler helpers
 */

/**
 * dfll_scale_dvco_rate - calculate scaled rate from the DVCO rate
 * @scale_bits: clock scaler value (bits in the DFLL_FREQ_REQ_SCALE field)
 * @dvco_rate: the DVCO rate
 *
 * Apply the same scaling formula that the DFLL hardware uses to scale
 * the DVCO rate.
 */
static unsigned long dfll_scale_dvco_rate(int scale_bits,
					  unsigned long dvco_rate)
{
	return (u64)dvco_rate * (scale_bits + 1) / DFLL_FREQ_REQ_SCALE_MAX;
}

/*
 * DFLL mode switching
 */

/**
 * dfll_set_mode - change the DFLL control mode
 * @td: DFLL instance
 * @mode: DFLL control mode (see enum dfll_ctrl_mode)
 *
 * Change the DFLL's operating mode between disabled, open-loop mode,
 * and closed-loop mode, or vice versa.
 */
static void dfll_set_mode(struct tegra_dfll *td,
			  enum dfll_ctrl_mode mode)
{
	td->mode = mode;
	dfll_writel(td, mode - 1, DFLL_CTRL);
	dfll_wmb(td);
}

/*
 * DVCO rate control
 */

static unsigned long get_dvco_rate_below(struct tegra_dfll *td, u8 out_min)
{
	struct dev_pm_opp *opp;
	unsigned long rate, prev_rate;
	unsigned long uv, min_uv;

	min_uv = td->lut_uv[out_min];
	for (rate = 0, prev_rate = 0; ; rate++) {
		opp = dev_pm_opp_find_freq_ceil(td->soc->dev, &rate);
		if (IS_ERR(opp))
			break;

		uv = dev_pm_opp_get_voltage(opp);
		dev_pm_opp_put(opp);

		if (uv && uv > min_uv)
			return prev_rate;

		prev_rate = rate;
	}

	return prev_rate;
}

/*
 * DFLL-to-I2C controller interface
 */

/**
 * dfll_i2c_set_output_enabled - enable/disable I2C PMIC voltage requests
 * @td: DFLL instance
 * @enable: whether to enable or disable the I2C voltage requests
 *
 * Set the master enable control for I2C control value updates. If disabled,
 * then I2C control messages are inhibited, regardless of the DFLL mode.
 */
static int dfll_i2c_set_output_enabled(struct tegra_dfll *td, bool enable)
{
	u32 val;

	val = dfll_i2c_readl(td, DFLL_OUTPUT_CFG);

	if (enable)
		val |= DFLL_OUTPUT_CFG_I2C_ENABLE;
	else
		val &= ~DFLL_OUTPUT_CFG_I2C_ENABLE;

	dfll_i2c_writel(td, val, DFLL_OUTPUT_CFG);
	dfll_i2c_wmb(td);

	return 0;
}


/*
 * DFLL-to-PWM controller interface
 */

/**
 * dfll_pwm_set_output_enabled - enable/disable PWM voltage requests
 * @td: DFLL instance
 * @enable: whether to enable or disable the PWM voltage requests
 *
 * Set the master enable control for PWM control value updates. If disabled,
 * then the PWM signal is not driven. Also configure the PWM output pad
 * to the appropriate state.
 */
static int dfll_pwm_set_output_enabled(struct tegra_dfll *td, bool enable)
{
	int ret;
	u32 val, div;

	if (enable) {
		ret = pinctrl_select_state(td->pwm_pin, td->pwm_enable_state);
		if (ret < 0) {
			dev_err(td->dev, "setting enable state failed\n");
			return -EINVAL;
		}
		val = dfll_readl(td, DFLL_OUTPUT_CFG);
		val &= ~DFLL_OUTPUT_CFG_PWM_DIV_MASK;
		div = DIV_ROUND_UP(td->ref_rate, td->pwm_rate);
		val |= (div << DFLL_OUTPUT_CFG_PWM_DIV_SHIFT)
				& DFLL_OUTPUT_CFG_PWM_DIV_MASK;
		dfll_writel(td, val, DFLL_OUTPUT_CFG);
		dfll_wmb(td);

		val |= DFLL_OUTPUT_CFG_PWM_ENABLE;
		dfll_writel(td, val, DFLL_OUTPUT_CFG);
		dfll_wmb(td);
	} else {
		ret = pinctrl_select_state(td->pwm_pin, td->pwm_disable_state);
		if (ret < 0)
			dev_warn(td->dev, "setting disable state failed\n");

		val = dfll_readl(td, DFLL_OUTPUT_CFG);
		val &= ~DFLL_OUTPUT_CFG_PWM_ENABLE;
		dfll_writel(td, val, DFLL_OUTPUT_CFG);
		dfll_wmb(td);
	}

	return 0;
}

/**
 * dfll_set_force_output_value - set fixed value for force output
 * @td: DFLL instance
 * @out_val: value to force output
 *
 * Set the fixed value for force output, DFLL will output this value when
 * force output is enabled.
 */
static u32 dfll_set_force_output_value(struct tegra_dfll *td, u8 out_val)
{
	u32 val = dfll_readl(td, DFLL_OUTPUT_FORCE);

	val = (val & DFLL_OUTPUT_FORCE_ENABLE) | (out_val & OUT_MASK);
	dfll_writel(td, val, DFLL_OUTPUT_FORCE);
	dfll_wmb(td);

	return dfll_readl(td, DFLL_OUTPUT_FORCE);
}

/**
 * dfll_set_force_output_enabled - enable/disable force output
 * @td: DFLL instance
 * @enable: whether to enable or disable the force output
 *
 * Set the enable control for fouce output with fixed value.
 */
static void dfll_set_force_output_enabled(struct tegra_dfll *td, bool enable)
{
	u32 val = dfll_readl(td, DFLL_OUTPUT_FORCE);

	if (enable)
		val |= DFLL_OUTPUT_FORCE_ENABLE;
	else
		val &= ~DFLL_OUTPUT_FORCE_ENABLE;

	dfll_writel(td, val, DFLL_OUTPUT_FORCE);
	dfll_wmb(td);
}

/**
 * dfll_force_output - force output a fixed value
 * @td: DFLL instance
 * @out_sel: value to force output
 *
 * Set the fixed value for force output, DFLL will output this value.
 */
static int dfll_force_output(struct tegra_dfll *td, unsigned int out_sel)
{
	u32 val;

	if (out_sel > OUT_MASK)
		return -EINVAL;

	val = dfll_set_force_output_value(td, out_sel);
	if ((td->mode < DFLL_CLOSED_LOOP) &&
	    !(val & DFLL_OUTPUT_FORCE_ENABLE)) {
		dfll_set_force_output_enabled(td, true);
	}

	return 0;
}

/**
 * dfll_load_lut - load the voltage lookup table
 * @td: struct tegra_dfll *
 *
 * Load the voltage-to-PMIC register value lookup table into the DFLL
 * IP block memory. Look-up tables can be loaded at any time.
 */
static void dfll_load_i2c_lut(struct tegra_dfll *td)
{
	int i, lut_index;
	u32 val;

	for (i = 0; i < MAX_DFLL_VOLTAGES; i++) {
		if (i < td->lut_min)
			lut_index = td->lut_min;
		else if (i > td->lut_max)
			lut_index = td->lut_max;
		else
			lut_index = i;

		val = regulator_list_hardware_vsel(td->vdd_reg,
						     td->lut[lut_index]);
		__raw_writel(val, td->lut_base + i * 4);
	}

	dfll_i2c_wmb(td);
}

/**
 * dfll_init_i2c_if - set up the DFLL's DFLL-I2C interface
 * @td: DFLL instance
 *
 * During DFLL driver initialization, program the DFLL-I2C interface
 * with the PMU slave address, vdd register offset, and transfer mode.
 * This data is used by the DFLL to automatically construct I2C
 * voltage-set commands, which are then passed to the DFLL's internal
 * I2C controller.
 */
static void dfll_init_i2c_if(struct tegra_dfll *td)
{
	u32 val;

	if (td->i2c_slave_addr > 0x7f) {
		val = td->i2c_slave_addr << DFLL_I2C_CFG_SLAVE_ADDR_SHIFT_10BIT;
		val |= DFLL_I2C_CFG_SLAVE_ADDR_10;
	} else {
		val = td->i2c_slave_addr << DFLL_I2C_CFG_SLAVE_ADDR_SHIFT_7BIT;
	}
	val |= DFLL_I2C_CFG_SIZE_MASK;
	val |= DFLL_I2C_CFG_ARB_ENABLE;
	dfll_i2c_writel(td, val, DFLL_I2C_CFG);

	dfll_i2c_writel(td, td->i2c_reg, DFLL_I2C_VDD_REG_ADDR);

	val = DIV_ROUND_UP(td->i2c_clk_rate, td->i2c_fs_rate * 8);
	BUG_ON(!val || (val > DFLL_I2C_CLK_DIVISOR_MASK));
	val = (val - 1) << DFLL_I2C_CLK_DIVISOR_FS_SHIFT;

	/* default hs divisor just in case */
	val |= 1 << DFLL_I2C_CLK_DIVISOR_HS_SHIFT;
	__raw_writel(val, td->i2c_controller_base + DFLL_I2C_CLK_DIVISOR);
	dfll_i2c_wmb(td);
}

/**
 * dfll_init_out_if - prepare DFLL-to-PMIC interface
 * @td: DFLL instance
 *
 * During DFLL driver initialization or resume from context loss,
 * disable the I2C command output to the PMIC, set safe voltage and
 * output limits, and disable and clear limit interrupts.
 */
static void dfll_init_out_if(struct tegra_dfll *td)
{
	u32 val;

	td->lut_min = td->lut_bottom;
	td->lut_max = td->lut_size - 1;
	td->lut_safe = td->lut_min + (td->lut_min < td->lut_max ? 1 : 0);

	/* clear DFLL_OUTPUT_CFG before setting new value */
	dfll_writel(td, 0, DFLL_OUTPUT_CFG);
	dfll_wmb(td);

	val = (td->lut_safe << DFLL_OUTPUT_CFG_SAFE_SHIFT) |
	      (td->lut_max << DFLL_OUTPUT_CFG_MAX_SHIFT) |
	      (td->lut_min << DFLL_OUTPUT_CFG_MIN_SHIFT);
	dfll_writel(td, val, DFLL_OUTPUT_CFG);
	dfll_wmb(td);

	dfll_writel(td, 0, DFLL_OUTPUT_FORCE);
	dfll_i2c_writel(td, 0, DFLL_INTR_EN);
	dfll_i2c_writel(td, DFLL_INTR_MAX_MASK | DFLL_INTR_MIN_MASK,
			DFLL_INTR_STS);

	if (td->pmu_if == TEGRA_DFLL_PMU_PWM) {
		u32 vinit = td->reg_init_uV;
		int vstep = td->soc->alignment.step_uv;
		unsigned long vmin = td->lut_uv[0];

		/* set initial voltage */
		if ((vinit >= vmin) && vstep) {
			unsigned int vsel;

			vsel = DIV_ROUND_UP((vinit - vmin), vstep);
			dfll_force_output(td, vsel);
		}
	} else {
		dfll_load_i2c_lut(td);
		dfll_init_i2c_if(td);
	}
}

/*
 * Set/get the DFLL's targeted output clock rate
 */

/**
 * find_lut_index_for_rate - determine I2C LUT index for given DFLL rate
 * @td: DFLL instance
 * @rate: clock rate
 *
 * Determines the index of a I2C LUT entry for a voltage that approximately
 * produces the given DFLL clock rate. This is used when forcing a value
 * to the integrator during rate changes. Returns -ENOENT if a suitable
 * LUT index is not found.
 */
static int find_lut_index_for_rate(struct tegra_dfll *td, unsigned long rate)
{
	struct dev_pm_opp *opp;
	int i, align_step;

	opp = dev_pm_opp_find_freq_ceil(td->soc->dev, &rate);
	if (IS_ERR(opp))
		return PTR_ERR(opp);

	align_step = dev_pm_opp_get_voltage(opp) / td->soc->alignment.step_uv;
	dev_pm_opp_put(opp);

	for (i = td->lut_bottom; i < td->lut_size; i++) {
		if ((td->lut_uv[i] / td->soc->alignment.step_uv) >= align_step)
			return i;
	}

	return -ENOENT;
}

/**
 * dfll_calculate_rate_request - calculate DFLL parameters for a given rate
 * @td: DFLL instance
 * @req: DFLL-rate-request structure
 * @rate: the desired DFLL rate
 *
 * Populate the DFLL-rate-request record @req fields with the scale_bits
 * and mult_bits fields, based on the target input rate. Returns 0 upon
 * success, or -EINVAL if the requested rate in req->rate is too high
 * or low for the DFLL to generate.
 */
static int dfll_calculate_rate_request(struct tegra_dfll *td,
				       struct dfll_rate_req *req,
				       unsigned long rate)
{
	u32 val;

	/*
	 * If requested rate is below the minimum DVCO rate, active the scaler.
	 * In the future the DVCO minimum voltage should be selected based on
	 * chip temperature and the actual minimum rate should be calibrated
	 * at runtime.
	 */
	req->scale_bits = DFLL_FREQ_REQ_SCALE_MAX - 1;
	if (rate < td->dvco_rate_min) {
		int scale;

		scale = DIV_ROUND_CLOSEST(rate / 1000 * DFLL_FREQ_REQ_SCALE_MAX,
					  td->dvco_rate_min / 1000);
		if (!scale) {
			dev_err(td->dev, "%s: Rate %lu is too low\n",
				__func__, rate);
			return -EINVAL;
		}
		req->scale_bits = scale - 1;
		rate = td->dvco_rate_min;
	}

	/* Convert requested rate into frequency request and scale settings */
	val = DVCO_RATE_TO_MULT(rate, td->ref_rate);
	if (val > FREQ_MAX) {
		dev_err(td->dev, "%s: Rate %lu is above dfll range\n",
			__func__, rate);
		return -EINVAL;
	}
	req->mult_bits = val;
	req->dvco_target_rate = MULT_TO_DVCO_RATE(req->mult_bits, td->ref_rate);
	req->rate = dfll_scale_dvco_rate(req->scale_bits,
					 req->dvco_target_rate);
	req->lut_index = find_lut_index_for_rate(td, req->dvco_target_rate);
	if (req->lut_index < 0)
		return req->lut_index;

	return 0;
}

/**
 * dfll_set_frequency_request - start the frequency change operation
 * @td: DFLL instance
 * @req: rate request structure
 *
 * Tell the DFLL to try to change its output frequency to the
 * frequency represented by @req. DFLL must be in closed-loop mode.
 */
static void dfll_set_frequency_request(struct tegra_dfll *td,
				       struct dfll_rate_req *req)
{
	u32 val = 0;
	int force_val;
	int coef = 128; /* FIXME: td->cg_scale? */;

	force_val = (req->lut_index - td->lut_safe) * coef / td->cg;
	force_val = clamp(force_val, FORCE_MIN, FORCE_MAX);

	val |= req->mult_bits << DFLL_FREQ_REQ_MULT_SHIFT;
	val |= req->scale_bits << DFLL_FREQ_REQ_SCALE_SHIFT;
	val |= ((u32)force_val << DFLL_FREQ_REQ_FORCE_SHIFT) &
		DFLL_FREQ_REQ_FORCE_MASK;
	val |= DFLL_FREQ_REQ_FREQ_VALID | DFLL_FREQ_REQ_FORCE_ENABLE;

	dfll_writel(td, val, DFLL_FREQ_REQ);
	dfll_wmb(td);
}

/**
 * tegra_dfll_request_rate - set the next rate for the DFLL to tune to
 * @td: DFLL instance
 * @rate: clock rate to target
 *
 * Convert the requested clock rate @rate into the DFLL control logic
 * settings. In closed-loop mode, update new settings immediately to
 * adjust DFLL output rate accordingly. Otherwise, just save them
 * until the next switch to closed loop. Returns 0 upon success,
 * -EPERM if the DFLL driver has not yet been initialized, or -EINVAL
 * if @rate is outside the DFLL's tunable range.
 */
static int dfll_request_rate(struct tegra_dfll *td, unsigned long rate)
{
	int ret;
	struct dfll_rate_req req;

	if (td->mode == DFLL_UNINITIALIZED) {
		dev_err(td->dev, "%s: Cannot set DFLL rate in %s mode\n",
			__func__, mode_name[td->mode]);
		return -EPERM;
	}

	ret = dfll_calculate_rate_request(td, &req, rate);
	if (ret)
		return ret;

	td->last_unrounded_rate = rate;
	td->last_req = req;

	if (td->mode == DFLL_CLOSED_LOOP)
		dfll_set_frequency_request(td, &td->last_req);

	return 0;
}

/*
 * DFLL enable/disable & open-loop <-> closed-loop transitions
 */

/**
 * dfll_disable - switch from open-loop mode to disabled mode
 * @td: DFLL instance
 *
 * Switch from OPEN_LOOP state to DISABLED state. Returns 0 upon success
 * or -EPERM if the DFLL is not currently in open-loop mode.
 */
static int dfll_disable(struct tegra_dfll *td)
{
	if (td->mode != DFLL_OPEN_LOOP) {
		dev_err(td->dev, "cannot disable DFLL in %s mode\n",
			mode_name[td->mode]);
		return -EINVAL;
	}

	dfll_set_mode(td, DFLL_DISABLED);
	pm_runtime_put_sync(td->dev);

	return 0;
}

/**
 * dfll_enable - switch a disabled DFLL to open-loop mode
 * @td: DFLL instance
 *
 * Switch from DISABLED state to OPEN_LOOP state. Returns 0 upon success
 * or -EPERM if the DFLL is not currently disabled.
 */
static int dfll_enable(struct tegra_dfll *td)
{
	if (td->mode != DFLL_DISABLED) {
		dev_err(td->dev, "cannot enable DFLL in %s mode\n",
			mode_name[td->mode]);
		return -EPERM;
	}

	pm_runtime_get_sync(td->dev);
	dfll_set_mode(td, DFLL_OPEN_LOOP);

	return 0;
}

/**
 * dfll_set_open_loop_config - prepare to switch to open-loop mode
 * @td: DFLL instance
 *
 * Prepare to switch the DFLL to open-loop mode. This switches the
 * DFLL to the low-voltage tuning range, ensures that I2C output
 * forcing is disabled, and disables the output clock rate scaler.
 * The DFLL's low-voltage tuning range parameters must be
 * characterized to keep the downstream device stable at any DVCO
 * input voltage. No return value.
 */
static void dfll_set_open_loop_config(struct tegra_dfll *td)
{
	u32 val;

	/* always tune low (safe) in open loop */
	if (td->tune_range != DFLL_TUNE_LOW)
		dfll_tune_low(td);

	val = dfll_readl(td, DFLL_FREQ_REQ);
	val |= DFLL_FREQ_REQ_SCALE_MASK;
	val &= ~DFLL_FREQ_REQ_FORCE_ENABLE;
	dfll_writel(td, val, DFLL_FREQ_REQ);
	dfll_wmb(td);
}

/**
 * tegra_dfll_lock - switch from open-loop to closed-loop mode
 * @td: DFLL instance
 *
 * Switch from OPEN_LOOP state to CLOSED_LOOP state. Returns 0 upon success,
 * -EINVAL if the DFLL's target rate hasn't been set yet, or -EPERM if the
 * DFLL is not currently in open-loop mode.
 */
static int dfll_lock(struct tegra_dfll *td)
{
	struct dfll_rate_req *req = &td->last_req;

	switch (td->mode) {
	case DFLL_CLOSED_LOOP:
		return 0;

	case DFLL_OPEN_LOOP:
		if (req->rate == 0) {
			dev_err(td->dev, "%s: Cannot lock DFLL at rate 0\n",
				__func__);
			return -EINVAL;
		}

		if (td->pmu_if == TEGRA_DFLL_PMU_PWM)
			dfll_pwm_set_output_enabled(td, true);
		else
			dfll_i2c_set_output_enabled(td, true);

		dfll_set_mode(td, DFLL_CLOSED_LOOP);
		dfll_set_frequency_request(td, req);
		dfll_set_force_output_enabled(td, false);
		return 0;

	default:
		BUG_ON(td->mode > DFLL_CLOSED_LOOP);
		dev_err(td->dev, "%s: Cannot lock DFLL in %s mode\n",
			__func__, mode_name[td->mode]);
		return -EPERM;
	}
}

/**
 * tegra_dfll_unlock - switch from closed-loop to open-loop mode
 * @td: DFLL instance
 *
 * Switch from CLOSED_LOOP state to OPEN_LOOP state. Returns 0 upon success,
 * or -EPERM if the DFLL is not currently in open-loop mode.
 */
static int dfll_unlock(struct tegra_dfll *td)
{
	switch (td->mode) {
	case DFLL_CLOSED_LOOP:
		dfll_set_open_loop_config(td);
		dfll_set_mode(td, DFLL_OPEN_LOOP);
		if (td->pmu_if == TEGRA_DFLL_PMU_PWM)
			dfll_pwm_set_output_enabled(td, false);
		else
			dfll_i2c_set_output_enabled(td, false);
		return 0;

	case DFLL_OPEN_LOOP:
		return 0;

	default:
		BUG_ON(td->mode > DFLL_CLOSED_LOOP);
		dev_err(td->dev, "%s: Cannot unlock DFLL in %s mode\n",
			__func__, mode_name[td->mode]);
		return -EPERM;
	}
}

/*
 * Clock framework integration
 *
 * When the DFLL is being controlled by the CCF, always enter closed loop
 * mode when the clk is enabled. This requires that a DFLL rate request
 * has been set beforehand, which implies that a clk_set_rate() call is
 * always required before a clk_enable().
 */

static int dfll_clk_is_enabled(struct clk_hw *hw)
{
	struct tegra_dfll *td = clk_hw_to_dfll(hw);

	return dfll_is_running(td);
}

static int dfll_clk_enable(struct clk_hw *hw)
{
	struct tegra_dfll *td = clk_hw_to_dfll(hw);
	int ret;

	ret = dfll_enable(td);
	if (ret)
		return ret;

	ret = dfll_lock(td);
	if (ret)
		dfll_disable(td);

	return ret;
}

static void dfll_clk_disable(struct clk_hw *hw)
{
	struct tegra_dfll *td = clk_hw_to_dfll(hw);
	int ret;

	ret = dfll_unlock(td);
	if (!ret)
		dfll_disable(td);
}

static unsigned long dfll_clk_recalc_rate(struct clk_hw *hw,
					  unsigned long parent_rate)
{
	struct tegra_dfll *td = clk_hw_to_dfll(hw);

	return td->last_unrounded_rate;
}

/* Must use determine_rate since it allows for rates exceeding 2^31-1 */
static int dfll_clk_determine_rate(struct clk_hw *hw,
				   struct clk_rate_request *clk_req)
{
	struct tegra_dfll *td = clk_hw_to_dfll(hw);
	struct dfll_rate_req req;
	int ret;

	ret = dfll_calculate_rate_request(td, &req, clk_req->rate);
	if (ret)
		return ret;

	/*
	 * Don't set the rounded rate, since it doesn't really matter as
	 * the output rate will be voltage controlled anyway, and cpufreq
	 * freaks out if any rounding happens.
	 */

	return 0;
}

static int dfll_clk_set_rate(struct clk_hw *hw, unsigned long rate,
			     unsigned long parent_rate)
{
	struct tegra_dfll *td = clk_hw_to_dfll(hw);

	return dfll_request_rate(td, rate);
}

static const struct clk_ops dfll_clk_ops = {
	.is_enabled	= dfll_clk_is_enabled,
	.enable		= dfll_clk_enable,
	.disable	= dfll_clk_disable,
	.recalc_rate	= dfll_clk_recalc_rate,
	.determine_rate	= dfll_clk_determine_rate,
	.set_rate	= dfll_clk_set_rate,
};

static struct clk_init_data dfll_clk_init_data = {
	.ops		= &dfll_clk_ops,
	.num_parents	= 0,
};

/**
 * dfll_register_clk - register the DFLL output clock with the clock framework
 * @td: DFLL instance
 *
 * Register the DFLL's output clock with the Linux clock framework and register
 * the DFLL driver as an OF clock provider. Returns 0 upon success or -EINVAL
 * or -ENOMEM upon failure.
 */
static int dfll_register_clk(struct tegra_dfll *td)
{
	int ret;

	dfll_clk_init_data.name = td->output_clock_name;
	td->dfll_clk_hw.init = &dfll_clk_init_data;

	td->dfll_clk = clk_register(td->dev, &td->dfll_clk_hw);
	if (IS_ERR(td->dfll_clk)) {
		dev_err(td->dev, "DFLL clock registration error\n");
		return -EINVAL;
	}

	ret = of_clk_add_provider(td->dev->of_node, of_clk_src_simple_get,
				  td->dfll_clk);
	if (ret) {
		dev_err(td->dev, "of_clk_add_provider() failed\n");

		clk_unregister(td->dfll_clk);
		return ret;
	}

	return 0;
}

/**
 * dfll_unregister_clk - unregister the DFLL output clock
 * @td: DFLL instance
 *
 * Unregister the DFLL's output clock from the Linux clock framework
 * and from clkdev. No return value.
 */
static void dfll_unregister_clk(struct tegra_dfll *td)
{
	of_clk_del_provider(td->dev->of_node);
	clk_unregister(td->dfll_clk);
	td->dfll_clk = NULL;
}

/*
 * Debugfs interface
 */

#ifdef CONFIG_DEBUG_FS
/*
 * Monitor control
 */

/**
 * dfll_calc_monitored_rate - convert DFLL_MONITOR_DATA_VAL rate into real freq
 * @monitor_data: value read from the DFLL_MONITOR_DATA_VAL bitfield
 * @ref_rate: DFLL reference clock rate
 *
 * Convert @monitor_data from DFLL_MONITOR_DATA_VAL units into cycles
 * per second. Returns the converted value.
 */
static u64 dfll_calc_monitored_rate(u32 monitor_data,
				    unsigned long ref_rate)
{
	return monitor_data * (ref_rate / REF_CLK_CYC_PER_DVCO_SAMPLE);
}

/**
 * dfll_read_monitor_rate - return the DFLL's output rate from internal monitor
 * @td: DFLL instance
 *
 * If the DFLL is enabled, return the last rate reported by the DFLL's
 * internal monitoring hardware. This works in both open-loop and
 * closed-loop mode, and takes the output scaler setting into account.
 * Assumes that the monitor was programmed to monitor frequency before
 * the sample period started. If the driver believes that the DFLL is
 * currently uninitialized or disabled, it will return 0, since
 * otherwise the DFLL monitor data register will return the last
 * measured rate from when the DFLL was active.
 */
static u64 dfll_read_monitor_rate(struct tegra_dfll *td)
{
	u32 v, s;
	u64 pre_scaler_rate, post_scaler_rate;

	if (!dfll_is_running(td))
		return 0;

	v = dfll_readl(td, DFLL_MONITOR_DATA);
	v = (v & DFLL_MONITOR_DATA_VAL_MASK) >> DFLL_MONITOR_DATA_VAL_SHIFT;
	pre_scaler_rate = dfll_calc_monitored_rate(v, td->ref_rate);

	s = dfll_readl(td, DFLL_FREQ_REQ);
	s = (s & DFLL_FREQ_REQ_SCALE_MASK) >> DFLL_FREQ_REQ_SCALE_SHIFT;
	post_scaler_rate = dfll_scale_dvco_rate(s, pre_scaler_rate);

	return post_scaler_rate;
}

static int attr_enable_get(void *data, u64 *val)
{
	struct tegra_dfll *td = data;

	*val = dfll_is_running(td);

	return 0;
}
static int attr_enable_set(void *data, u64 val)
{
	struct tegra_dfll *td = data;

	return val ? dfll_enable(td) : dfll_disable(td);
}
DEFINE_DEBUGFS_ATTRIBUTE(enable_fops, attr_enable_get, attr_enable_set,
			 "%llu\n");

static int attr_lock_get(void *data, u64 *val)
{
	struct tegra_dfll *td = data;

	*val = (td->mode == DFLL_CLOSED_LOOP);

	return 0;
}
static int attr_lock_set(void *data, u64 val)
{
	struct tegra_dfll *td = data;

	return val ? dfll_lock(td) :  dfll_unlock(td);
}
DEFINE_DEBUGFS_ATTRIBUTE(lock_fops, attr_lock_get, attr_lock_set, "%llu\n");

static int attr_rate_get(void *data, u64 *val)
{
	struct tegra_dfll *td = data;

	*val = dfll_read_monitor_rate(td);

	return 0;
}

static int attr_rate_set(void *data, u64 val)
{
	struct tegra_dfll *td = data;

	return dfll_request_rate(td, val);
}
DEFINE_DEBUGFS_ATTRIBUTE(rate_fops, attr_rate_get, attr_rate_set, "%llu\n");

static int attr_registers_show(struct seq_file *s, void *data)
{
	u32 val, offs;
	struct tegra_dfll *td = s->private;

	seq_puts(s, "CONTROL REGISTERS:\n");
	for (offs = 0; offs <= DFLL_MONITOR_DATA; offs += 4) {
		if (offs == DFLL_OUTPUT_CFG)
			val = dfll_i2c_readl(td, offs);
		else
			val = dfll_readl(td, offs);
		seq_printf(s, "[0x%02x] = 0x%08x\n", offs, val);
	}

	seq_puts(s, "\nI2C and INTR REGISTERS:\n");
	for (offs = DFLL_I2C_CFG; offs <= DFLL_I2C_STS; offs += 4)
		seq_printf(s, "[0x%02x] = 0x%08x\n", offs,
			   dfll_i2c_readl(td, offs));
	for (offs = DFLL_INTR_STS; offs <= DFLL_INTR_EN; offs += 4)
		seq_printf(s, "[0x%02x] = 0x%08x\n", offs,
			   dfll_i2c_readl(td, offs));

	if (td->pmu_if == TEGRA_DFLL_PMU_I2C) {
		seq_puts(s, "\nINTEGRATED I2C CONTROLLER REGISTERS:\n");
		offs = DFLL_I2C_CLK_DIVISOR;
		seq_printf(s, "[0x%02x] = 0x%08x\n", offs,
			   __raw_readl(td->i2c_controller_base + offs));

		seq_puts(s, "\nLUT:\n");
		for (offs = 0; offs <  4 * MAX_DFLL_VOLTAGES; offs += 4)
			seq_printf(s, "[0x%02x] = 0x%08x\n", offs,
				   __raw_readl(td->lut_base + offs));
	}

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(attr_registers);

static void dfll_debug_init(struct tegra_dfll *td)
{
	struct dentry *root;

	if (!td || (td->mode == DFLL_UNINITIALIZED))
		return;

	root = debugfs_create_dir("tegra_dfll_fcpu", NULL);
	td->debugfs_dir = root;

	debugfs_create_file_unsafe("enable", 0644, root, td,
				   &enable_fops);
	debugfs_create_file_unsafe("lock", 0444, root, td, &lock_fops);
	debugfs_create_file_unsafe("rate", 0444, root, td, &rate_fops);
	debugfs_create_file("registers", 0444, root, td, &attr_registers_fops);
}

#else
static void inline dfll_debug_init(struct tegra_dfll *td) { }
#endif /* CONFIG_DEBUG_FS */

/*
 * DFLL initialization
 */

/**
 * dfll_set_default_params - program non-output related DFLL parameters
 * @td: DFLL instance
 *
 * During DFLL driver initialization or resume from context loss,
 * program parameters for the closed loop integrator, DVCO tuning,
 * voltage droop control and monitor control.
 */
static void dfll_set_default_params(struct tegra_dfll *td)
{
	u32 val;

	val = DIV_ROUND_UP(td->ref_rate, td->sample_rate * 32);
	BUG_ON(val > DFLL_CONFIG_DIV_MASK);
	dfll_writel(td, val, DFLL_CONFIG);

	val = (td->force_mode << DFLL_PARAMS_FORCE_MODE_SHIFT) |
		(td->cf << DFLL_PARAMS_CF_PARAM_SHIFT) |
		(td->ci << DFLL_PARAMS_CI_PARAM_SHIFT) |
		(td->cg << DFLL_PARAMS_CG_PARAM_SHIFT) |
		(td->cg_scale ? DFLL_PARAMS_CG_SCALE : 0);
	dfll_writel(td, val, DFLL_PARAMS);

	dfll_tune_low(td);
	dfll_writel(td, td->droop_ctrl, DFLL_DROOP_CTRL);
	dfll_writel(td, DFLL_MONITOR_CTRL_FREQ, DFLL_MONITOR_CTRL);
}

/**
 * dfll_init_clks - clk_get() the DFLL source clocks
 * @td: DFLL instance
 *
 * Call clk_get() on the DFLL source clocks and save the pointers for later
 * use. Returns 0 upon success or error (see devm_clk_get) if one or more
 * of the clocks couldn't be looked up.
 */
static int dfll_init_clks(struct tegra_dfll *td)
{
	td->ref_clk = devm_clk_get(td->dev, "ref");
	if (IS_ERR(td->ref_clk)) {
		dev_err(td->dev, "missing ref clock\n");
		return PTR_ERR(td->ref_clk);
	}

	td->soc_clk = devm_clk_get(td->dev, "soc");
	if (IS_ERR(td->soc_clk)) {
		dev_err(td->dev, "missing soc clock\n");
		return PTR_ERR(td->soc_clk);
	}

	td->i2c_clk = devm_clk_get(td->dev, "i2c");
	if (IS_ERR(td->i2c_clk)) {
		dev_err(td->dev, "missing i2c clock\n");
		return PTR_ERR(td->i2c_clk);
	}
	td->i2c_clk_rate = clk_get_rate(td->i2c_clk);

	return 0;
}

/**
 * dfll_init - Prepare the DFLL IP block for use
 * @td: DFLL instance
 *
 * Do everything necessary to prepare the DFLL IP block for use. The
 * DFLL will be left in DISABLED state. Called by dfll_probe().
 * Returns 0 upon success, or passes along the error from whatever
 * function returned it.
 */
static int dfll_init(struct tegra_dfll *td)
{
	int ret;

	td->ref_rate = clk_get_rate(td->ref_clk);
	if (td->ref_rate != REF_CLOCK_RATE) {
		dev_err(td->dev, "unexpected ref clk rate %lu, expecting %lu",
			td->ref_rate, REF_CLOCK_RATE);
		return -EINVAL;
	}

	reset_control_deassert(td->dvco_rst);

	ret = clk_prepare(td->ref_clk);
	if (ret) {
		dev_err(td->dev, "failed to prepare ref_clk\n");
		return ret;
	}

	ret = clk_prepare(td->soc_clk);
	if (ret) {
		dev_err(td->dev, "failed to prepare soc_clk\n");
		goto di_err1;
	}

	ret = clk_prepare(td->i2c_clk);
	if (ret) {
		dev_err(td->dev, "failed to prepare i2c_clk\n");
		goto di_err2;
	}

	td->last_unrounded_rate = 0;

	pm_runtime_enable(td->dev);
	pm_runtime_get_sync(td->dev);

	dfll_set_mode(td, DFLL_DISABLED);
	dfll_set_default_params(td);

	if (td->soc->init_clock_trimmers)
		td->soc->init_clock_trimmers();

	dfll_set_open_loop_config(td);

	dfll_init_out_if(td);

	pm_runtime_put_sync(td->dev);

	return 0;

di_err2:
	clk_unprepare(td->soc_clk);
di_err1:
	clk_unprepare(td->ref_clk);

	reset_control_assert(td->dvco_rst);

	return ret;
}

/*
 * DT data fetch
 */

/*
 * Find a PMIC voltage register-to-voltage mapping for the given voltage.
 * An exact voltage match is required.
 */
static int find_vdd_map_entry_exact(struct tegra_dfll *td, int uV)
{
	int i, n_voltages, reg_uV,reg_volt_id, align_step;

	if (WARN_ON(td->pmu_if == TEGRA_DFLL_PMU_PWM))
		return -EINVAL;

	align_step = uV / td->soc->alignment.step_uv;
	n_voltages = regulator_count_voltages(td->vdd_reg);
	for (i = 0; i < n_voltages; i++) {
		reg_uV = regulator_list_voltage(td->vdd_reg, i);
		if (reg_uV < 0)
			break;

		reg_volt_id = reg_uV / td->soc->alignment.step_uv;

		if (align_step == reg_volt_id)
			return i;
	}

	dev_err(td->dev, "no voltage map entry for %d uV\n", uV);
	return -EINVAL;
}

/*
 * Find a PMIC voltage register-to-voltage mapping for the given voltage,
 * rounding up to the closest supported voltage.
 * */
static int find_vdd_map_entry_min(struct tegra_dfll *td, int uV)
{
	int i, n_voltages, reg_uV, reg_volt_id, align_step;

	if (WARN_ON(td->pmu_if == TEGRA_DFLL_PMU_PWM))
		return -EINVAL;

	align_step = uV / td->soc->alignment.step_uv;
	n_voltages = regulator_count_voltages(td->vdd_reg);
	for (i = 0; i < n_voltages; i++) {
		reg_uV = regulator_list_voltage(td->vdd_reg, i);
		if (reg_uV < 0)
			break;

		reg_volt_id = reg_uV / td->soc->alignment.step_uv;

		if (align_step <= reg_volt_id)
			return i;
	}

	dev_err(td->dev, "no voltage map entry rounding to %d uV\n", uV);
	return -EINVAL;
}

/*
 * dfll_build_pwm_lut - build the PWM regulator lookup table
 * @td: DFLL instance
 * @v_max: Vmax from OPP table
 *
 * Look-up table in h/w is ignored when PWM is used as DFLL interface to PMIC.
 * In this case closed loop output is controlling duty cycle directly. The s/w
 * look-up that maps PWM duty cycle to voltage is still built by this function.
 */
static int dfll_build_pwm_lut(struct tegra_dfll *td, unsigned long v_max)
{
	int i;
	unsigned long rate, reg_volt;
	u8 lut_bottom = MAX_DFLL_VOLTAGES;
	int v_min = td->soc->cvb->min_millivolts * 1000;

	for (i = 0; i < MAX_DFLL_VOLTAGES; i++) {
		reg_volt = td->lut_uv[i];

		/* since opp voltage is exact mv */
		reg_volt = (reg_volt / 1000) * 1000;
		if (reg_volt > v_max)
			break;

		td->lut[i] = i;
		if ((lut_bottom == MAX_DFLL_VOLTAGES) && (reg_volt >= v_min))
			lut_bottom = i;
	}

	/* determine voltage boundaries */
	td->lut_size = i;
	if ((lut_bottom == MAX_DFLL_VOLTAGES) ||
	    (lut_bottom + 1 >= td->lut_size)) {
		dev_err(td->dev, "no voltage above DFLL minimum %d mV\n",
			td->soc->cvb->min_millivolts);
		return -EINVAL;
	}
	td->lut_bottom = lut_bottom;

	/* determine rate boundaries */
	rate = get_dvco_rate_below(td, td->lut_bottom);
	if (!rate) {
		dev_err(td->dev, "no opp below DFLL minimum voltage %d mV\n",
			td->soc->cvb->min_millivolts);
		return -EINVAL;
	}
	td->dvco_rate_min = rate;

	return 0;
}

/**
 * dfll_build_i2c_lut - build the I2C voltage register lookup table
 * @td: DFLL instance
 * @v_max: Vmax from OPP table
 *
 * The DFLL hardware has 33 bytes of look-up table RAM that must be filled with
 * PMIC voltage register values that span the entire DFLL operating range.
 * This function builds the look-up table based on the OPP table provided by
 * the soc-specific platform driver (td->soc->opp_dev) and the PMIC
 * register-to-voltage mapping queried from the regulator framework.
 *
 * On success, fills in td->lut and returns 0, or -err on failure.
 */
static int dfll_build_i2c_lut(struct tegra_dfll *td, unsigned long v_max)
{
	unsigned long rate, v, v_opp;
	int ret = -EINVAL;
	int j, selector, lut;

	v = td->soc->cvb->min_millivolts * 1000;
	lut = find_vdd_map_entry_exact(td, v);
	if (lut < 0)
		goto out;
	td->lut[0] = lut;
	td->lut_bottom = 0;

	for (j = 1, rate = 0; ; rate++) {
		struct dev_pm_opp *opp;

		opp = dev_pm_opp_find_freq_ceil(td->soc->dev, &rate);
		if (IS_ERR(opp))
			break;
		v_opp = dev_pm_opp_get_voltage(opp);

		if (v_opp <= td->soc->cvb->min_millivolts * 1000)
			td->dvco_rate_min = dev_pm_opp_get_freq(opp);

		dev_pm_opp_put(opp);

		for (;;) {
			v += max(1UL, (v_max - v) / (MAX_DFLL_VOLTAGES - j));
			if (v >= v_opp)
				break;

			selector = find_vdd_map_entry_min(td, v);
			if (selector < 0)
				goto out;
			if (selector != td->lut[j - 1])
				td->lut[j++] = selector;
		}

		v = (j == MAX_DFLL_VOLTAGES - 1) ? v_max : v_opp;
		selector = find_vdd_map_entry_exact(td, v);
		if (selector < 0)
			goto out;
		if (selector != td->lut[j - 1])
			td->lut[j++] = selector;

		if (v >= v_max)
			break;
	}
	td->lut_size = j;

	if (!td->dvco_rate_min)
		dev_err(td->dev, "no opp above DFLL minimum voltage %d mV\n",
			td->soc->cvb->min_millivolts);
	else {
		ret = 0;
		for (j = 0; j < td->lut_size; j++)
			td->lut_uv[j] =
				regulator_list_voltage(td->vdd_reg,
						       td->lut[j]);
	}

out:
	return ret;
}

static int dfll_build_lut(struct tegra_dfll *td)
{
	unsigned long rate, v_max;
	struct dev_pm_opp *opp;

	rate = ULONG_MAX;
	opp = dev_pm_opp_find_freq_floor(td->soc->dev, &rate);
	if (IS_ERR(opp)) {
		dev_err(td->dev, "couldn't get vmax opp, empty opp table?\n");
		return -EINVAL;
	}
	v_max = dev_pm_opp_get_voltage(opp);
	dev_pm_opp_put(opp);

	if (td->pmu_if == TEGRA_DFLL_PMU_PWM)
		return dfll_build_pwm_lut(td, v_max);
	else
		return dfll_build_i2c_lut(td, v_max);
}

/**
 * read_dt_param - helper function for reading required parameters from the DT
 * @td: DFLL instance
 * @param: DT property name
 * @dest: output pointer for the value read
 *
 * Read a required numeric parameter from the DFLL device node, or complain
 * if the property doesn't exist. Returns a boolean indicating success for
 * easy chaining of multiple calls to this function.
 */
static bool read_dt_param(struct tegra_dfll *td, const char *param, u32 *dest)
{
	int err = of_property_read_u32(td->dev->of_node, param, dest);

	if (err < 0) {
		dev_err(td->dev, "failed to read DT parameter %s: %d\n",
			param, err);
		return false;
	}

	return true;
}

/**
 * dfll_fetch_i2c_params - query PMIC I2C params from DT & regulator subsystem
 * @td: DFLL instance
 *
 * Read all the parameters required for operation in I2C mode. The parameters
 * can originate from the device tree or the regulator subsystem.
 * Returns 0 on success or -err on failure.
 */
static int dfll_fetch_i2c_params(struct tegra_dfll *td)
{
	struct regmap *regmap;
	struct device *i2c_dev;
	struct i2c_client *i2c_client;
	int vsel_reg, vsel_mask;
	int ret;

	if (!read_dt_param(td, "nvidia,i2c-fs-rate", &td->i2c_fs_rate))
		return -EINVAL;

	regmap = regulator_get_regmap(td->vdd_reg);
	i2c_dev = regmap_get_device(regmap);
	i2c_client = to_i2c_client(i2c_dev);

	td->i2c_slave_addr = i2c_client->addr;

	ret = regulator_get_hardware_vsel_register(td->vdd_reg,
						   &vsel_reg,
						   &vsel_mask);
	if (ret < 0) {
		dev_err(td->dev,
			"regulator unsuitable for DFLL I2C operation\n");
		return -EINVAL;
	}
	td->i2c_reg = vsel_reg;

	return 0;
}

static int dfll_fetch_pwm_params(struct tegra_dfll *td)
{
	int ret, i;
	u32 pwm_period;

	if (!td->soc->alignment.step_uv || !td->soc->alignment.offset_uv) {
		dev_err(td->dev,
			"Missing step or alignment info for PWM regulator");
		return -EINVAL;
	}
	for (i = 0; i < MAX_DFLL_VOLTAGES; i++)
		td->lut_uv[i] = td->soc->alignment.offset_uv +
				i * td->soc->alignment.step_uv;

	ret = read_dt_param(td, "nvidia,pwm-tristate-microvolts",
			    &td->reg_init_uV);
	if (!ret) {
		dev_err(td->dev, "couldn't get initialized voltage\n");
		return ret;
	}

	ret = read_dt_param(td, "nvidia,pwm-period-nanoseconds", &pwm_period);
	if (!ret) {
		dev_err(td->dev, "couldn't get PWM period\n");
		return ret;
	}
	td->pwm_rate = (NSEC_PER_SEC / pwm_period) * (MAX_DFLL_VOLTAGES - 1);

	td->pwm_pin = devm_pinctrl_get(td->dev);
	if (IS_ERR(td->pwm_pin)) {
		dev_err(td->dev, "DT: missing pinctrl device\n");
		return PTR_ERR(td->pwm_pin);
	}

	td->pwm_enable_state = pinctrl_lookup_state(td->pwm_pin,
						    "dvfs_pwm_enable");
	if (IS_ERR(td->pwm_enable_state)) {
		dev_err(td->dev, "DT: missing pwm enabled state\n");
		return PTR_ERR(td->pwm_enable_state);
	}

	td->pwm_disable_state = pinctrl_lookup_state(td->pwm_pin,
						     "dvfs_pwm_disable");
	if (IS_ERR(td->pwm_disable_state)) {
		dev_err(td->dev, "DT: missing pwm disabled state\n");
		return PTR_ERR(td->pwm_disable_state);
	}

	return 0;
}

/**
 * dfll_fetch_common_params - read DFLL parameters from the device tree
 * @td: DFLL instance
 *
 * Read all the DT parameters that are common to both I2C and PWM operation.
 * Returns 0 on success or -EINVAL on any failure.
 */
static int dfll_fetch_common_params(struct tegra_dfll *td)
{
	bool ok = true;

	ok &= read_dt_param(td, "nvidia,droop-ctrl", &td->droop_ctrl);
	ok &= read_dt_param(td, "nvidia,sample-rate", &td->sample_rate);
	ok &= read_dt_param(td, "nvidia,force-mode", &td->force_mode);
	ok &= read_dt_param(td, "nvidia,cf", &td->cf);
	ok &= read_dt_param(td, "nvidia,ci", &td->ci);
	ok &= read_dt_param(td, "nvidia,cg", &td->cg);
	td->cg_scale = of_property_read_bool(td->dev->of_node,
					     "nvidia,cg-scale");

	if (of_property_read_string(td->dev->of_node, "clock-output-names",
				    &td->output_clock_name)) {
		dev_err(td->dev, "missing clock-output-names property\n");
		ok = false;
	}

	return ok ? 0 : -EINVAL;
}

/*
 * API exported to per-SoC platform drivers
 */

/**
 * tegra_dfll_register - probe a Tegra DFLL device
 * @pdev: DFLL platform_device *
 * @soc: Per-SoC integration and characterization data for this DFLL instance
 *
 * Probe and initialize a DFLL device instance. Intended to be called
 * by a SoC-specific shim driver that passes in per-SoC integration
 * and configuration data via @soc. Returns 0 on success or -err on failure.
 */
int tegra_dfll_register(struct platform_device *pdev,
			struct tegra_dfll_soc_data *soc)
{
	struct resource *mem;
	struct tegra_dfll *td;
	int ret;

	if (!soc) {
		dev_err(&pdev->dev, "no tegra_dfll_soc_data provided\n");
		return -EINVAL;
	}

	td = devm_kzalloc(&pdev->dev, sizeof(*td), GFP_KERNEL);
	if (!td)
		return -ENOMEM;
	td->dev = &pdev->dev;
	platform_set_drvdata(pdev, td);

	td->soc = soc;

	td->dvco_rst = devm_reset_control_get(td->dev, "dvco");
	if (IS_ERR(td->dvco_rst)) {
		dev_err(td->dev, "couldn't get dvco reset\n");
		return PTR_ERR(td->dvco_rst);
	}

	ret = dfll_fetch_common_params(td);
	if (ret) {
		dev_err(td->dev, "couldn't parse device tree parameters\n");
		return ret;
	}

	if (of_property_read_bool(td->dev->of_node, "nvidia,pwm-to-pmic")) {
		td->pmu_if = TEGRA_DFLL_PMU_PWM;
		ret = dfll_fetch_pwm_params(td);
	} else  {
		td->vdd_reg = devm_regulator_get(td->dev, "vdd-cpu");
		if (IS_ERR(td->vdd_reg)) {
			dev_err(td->dev, "couldn't get vdd_cpu regulator\n");
			return PTR_ERR(td->vdd_reg);
		}
		td->pmu_if = TEGRA_DFLL_PMU_I2C;
		ret = dfll_fetch_i2c_params(td);
	}
	if (ret)
		return ret;

	ret = dfll_build_lut(td);
	if (ret) {
		dev_err(td->dev, "couldn't build LUT\n");
		return ret;
	}

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem) {
		dev_err(td->dev, "no control register resource\n");
		return -ENODEV;
	}

	td->base = devm_ioremap(td->dev, mem->start, resource_size(mem));
	if (!td->base) {
		dev_err(td->dev, "couldn't ioremap DFLL control registers\n");
		return -ENODEV;
	}

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!mem) {
		dev_err(td->dev, "no i2c_base resource\n");
		return -ENODEV;
	}

	td->i2c_base = devm_ioremap(td->dev, mem->start, resource_size(mem));
	if (!td->i2c_base) {
		dev_err(td->dev, "couldn't ioremap i2c_base resource\n");
		return -ENODEV;
	}

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 2);
	if (!mem) {
		dev_err(td->dev, "no i2c_controller_base resource\n");
		return -ENODEV;
	}

	td->i2c_controller_base = devm_ioremap(td->dev, mem->start,
					       resource_size(mem));
	if (!td->i2c_controller_base) {
		dev_err(td->dev,
			"couldn't ioremap i2c_controller_base resource\n");
		return -ENODEV;
	}

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 3);
	if (!mem) {
		dev_err(td->dev, "no lut_base resource\n");
		return -ENODEV;
	}

	td->lut_base = devm_ioremap(td->dev, mem->start, resource_size(mem));
	if (!td->lut_base) {
		dev_err(td->dev,
			"couldn't ioremap lut_base resource\n");
		return -ENODEV;
	}

	ret = dfll_init_clks(td);
	if (ret) {
		dev_err(&pdev->dev, "DFLL clock init error\n");
		return ret;
	}

	/* Enable the clocks and set the device up */
	ret = dfll_init(td);
	if (ret)
		return ret;

	ret = dfll_register_clk(td);
	if (ret) {
		dev_err(&pdev->dev, "DFLL clk registration failed\n");
		return ret;
	}

	dfll_debug_init(td);

	return 0;
}
EXPORT_SYMBOL(tegra_dfll_register);

/**
 * tegra_dfll_unregister - release all of the DFLL driver resources for a device
 * @pdev: DFLL platform_device *
 *
 * Unbind this driver from the DFLL hardware device represented by
 * @pdev. The DFLL must be disabled for this to succeed. Returns a
 * soc pointer upon success or -EBUSY if the DFLL is still active.
 */
struct tegra_dfll_soc_data *tegra_dfll_unregister(struct platform_device *pdev)
{
	struct tegra_dfll *td = platform_get_drvdata(pdev);

	/* Try to prevent removal while the DFLL is active */
	if (td->mode != DFLL_DISABLED) {
		dev_err(&pdev->dev,
			"must disable DFLL before removing driver\n");
		return ERR_PTR(-EBUSY);
	}

	debugfs_remove_recursive(td->debugfs_dir);

	dfll_unregister_clk(td);
	pm_runtime_disable(&pdev->dev);

	clk_unprepare(td->ref_clk);
	clk_unprepare(td->soc_clk);
	clk_unprepare(td->i2c_clk);

	reset_control_assert(td->dvco_rst);

	return td->soc;
}
EXPORT_SYMBOL(tegra_dfll_unregister);
