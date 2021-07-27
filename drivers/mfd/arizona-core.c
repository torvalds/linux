// SPDX-License-Identifier: GPL-2.0-only
/*
 * Arizona core driver
 *
 * Copyright 2012 Wolfson Microelectronics plc
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/machine.h>
#include <linux/slab.h>
#include <linux/ktime.h>
#include <linux/platform_device.h>

#include <linux/mfd/arizona/core.h>
#include <linux/mfd/arizona/registers.h>

#include "arizona.h"

static const char * const wm5102_core_supplies[] = {
	"AVDD",
	"DBVDD1",
};

int arizona_clk32k_enable(struct arizona *arizona)
{
	int ret = 0;

	mutex_lock(&arizona->clk_lock);

	arizona->clk32k_ref++;

	if (arizona->clk32k_ref == 1) {
		switch (arizona->pdata.clk32k_src) {
		case ARIZONA_32KZ_MCLK1:
			ret = pm_runtime_get_sync(arizona->dev);
			if (ret != 0)
				goto err_ref;
			ret = clk_prepare_enable(arizona->mclk[ARIZONA_MCLK1]);
			if (ret != 0) {
				pm_runtime_put_sync(arizona->dev);
				goto err_ref;
			}
			break;
		case ARIZONA_32KZ_MCLK2:
			ret = clk_prepare_enable(arizona->mclk[ARIZONA_MCLK2]);
			if (ret != 0)
				goto err_ref;
			break;
		}

		ret = regmap_update_bits(arizona->regmap, ARIZONA_CLOCK_32K_1,
					 ARIZONA_CLK_32K_ENA,
					 ARIZONA_CLK_32K_ENA);
	}

err_ref:
	if (ret != 0)
		arizona->clk32k_ref--;

	mutex_unlock(&arizona->clk_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(arizona_clk32k_enable);

int arizona_clk32k_disable(struct arizona *arizona)
{
	mutex_lock(&arizona->clk_lock);

	WARN_ON(arizona->clk32k_ref <= 0);

	arizona->clk32k_ref--;

	if (arizona->clk32k_ref == 0) {
		regmap_update_bits(arizona->regmap, ARIZONA_CLOCK_32K_1,
				   ARIZONA_CLK_32K_ENA, 0);

		switch (arizona->pdata.clk32k_src) {
		case ARIZONA_32KZ_MCLK1:
			pm_runtime_put_sync(arizona->dev);
			clk_disable_unprepare(arizona->mclk[ARIZONA_MCLK1]);
			break;
		case ARIZONA_32KZ_MCLK2:
			clk_disable_unprepare(arizona->mclk[ARIZONA_MCLK2]);
			break;
		}
	}

	mutex_unlock(&arizona->clk_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(arizona_clk32k_disable);

static irqreturn_t arizona_clkgen_err(int irq, void *data)
{
	struct arizona *arizona = data;

	dev_err(arizona->dev, "CLKGEN error\n");

	return IRQ_HANDLED;
}

static irqreturn_t arizona_underclocked(int irq, void *data)
{
	struct arizona *arizona = data;
	unsigned int val;
	int ret;

	ret = regmap_read(arizona->regmap, ARIZONA_INTERRUPT_RAW_STATUS_8,
			  &val);
	if (ret != 0) {
		dev_err(arizona->dev, "Failed to read underclock status: %d\n",
			ret);
		return IRQ_NONE;
	}

	if (val & ARIZONA_AIF3_UNDERCLOCKED_STS)
		dev_err(arizona->dev, "AIF3 underclocked\n");
	if (val & ARIZONA_AIF2_UNDERCLOCKED_STS)
		dev_err(arizona->dev, "AIF2 underclocked\n");
	if (val & ARIZONA_AIF1_UNDERCLOCKED_STS)
		dev_err(arizona->dev, "AIF1 underclocked\n");
	if (val & ARIZONA_ISRC3_UNDERCLOCKED_STS)
		dev_err(arizona->dev, "ISRC3 underclocked\n");
	if (val & ARIZONA_ISRC2_UNDERCLOCKED_STS)
		dev_err(arizona->dev, "ISRC2 underclocked\n");
	if (val & ARIZONA_ISRC1_UNDERCLOCKED_STS)
		dev_err(arizona->dev, "ISRC1 underclocked\n");
	if (val & ARIZONA_FX_UNDERCLOCKED_STS)
		dev_err(arizona->dev, "FX underclocked\n");
	if (val & ARIZONA_ASRC_UNDERCLOCKED_STS)
		dev_err(arizona->dev, "ASRC underclocked\n");
	if (val & ARIZONA_DAC_UNDERCLOCKED_STS)
		dev_err(arizona->dev, "DAC underclocked\n");
	if (val & ARIZONA_ADC_UNDERCLOCKED_STS)
		dev_err(arizona->dev, "ADC underclocked\n");
	if (val & ARIZONA_MIXER_UNDERCLOCKED_STS)
		dev_err(arizona->dev, "Mixer dropped sample\n");

	return IRQ_HANDLED;
}

static irqreturn_t arizona_overclocked(int irq, void *data)
{
	struct arizona *arizona = data;
	unsigned int val[3];
	int ret;

	ret = regmap_bulk_read(arizona->regmap, ARIZONA_INTERRUPT_RAW_STATUS_6,
			       &val[0], 3);
	if (ret != 0) {
		dev_err(arizona->dev, "Failed to read overclock status: %d\n",
			ret);
		return IRQ_NONE;
	}

	switch (arizona->type) {
	case WM8998:
	case WM1814:
		/* Some bits are shifted on WM8998,
		 * rearrange to match the standard bit layout
		 */
		val[0] = ((val[0] & 0x60e0) >> 1) |
			 ((val[0] & 0x1e00) >> 2) |
			 (val[0] & 0x000f);
		break;
	default:
		break;
	}

	if (val[0] & ARIZONA_PWM_OVERCLOCKED_STS)
		dev_err(arizona->dev, "PWM overclocked\n");
	if (val[0] & ARIZONA_FX_CORE_OVERCLOCKED_STS)
		dev_err(arizona->dev, "FX core overclocked\n");
	if (val[0] & ARIZONA_DAC_SYS_OVERCLOCKED_STS)
		dev_err(arizona->dev, "DAC SYS overclocked\n");
	if (val[0] & ARIZONA_DAC_WARP_OVERCLOCKED_STS)
		dev_err(arizona->dev, "DAC WARP overclocked\n");
	if (val[0] & ARIZONA_ADC_OVERCLOCKED_STS)
		dev_err(arizona->dev, "ADC overclocked\n");
	if (val[0] & ARIZONA_MIXER_OVERCLOCKED_STS)
		dev_err(arizona->dev, "Mixer overclocked\n");
	if (val[0] & ARIZONA_AIF3_SYNC_OVERCLOCKED_STS)
		dev_err(arizona->dev, "AIF3 overclocked\n");
	if (val[0] & ARIZONA_AIF2_SYNC_OVERCLOCKED_STS)
		dev_err(arizona->dev, "AIF2 overclocked\n");
	if (val[0] & ARIZONA_AIF1_SYNC_OVERCLOCKED_STS)
		dev_err(arizona->dev, "AIF1 overclocked\n");
	if (val[0] & ARIZONA_PAD_CTRL_OVERCLOCKED_STS)
		dev_err(arizona->dev, "Pad control overclocked\n");

	if (val[1] & ARIZONA_SLIMBUS_SUBSYS_OVERCLOCKED_STS)
		dev_err(arizona->dev, "Slimbus subsystem overclocked\n");
	if (val[1] & ARIZONA_SLIMBUS_ASYNC_OVERCLOCKED_STS)
		dev_err(arizona->dev, "Slimbus async overclocked\n");
	if (val[1] & ARIZONA_SLIMBUS_SYNC_OVERCLOCKED_STS)
		dev_err(arizona->dev, "Slimbus sync overclocked\n");
	if (val[1] & ARIZONA_ASRC_ASYNC_SYS_OVERCLOCKED_STS)
		dev_err(arizona->dev, "ASRC async system overclocked\n");
	if (val[1] & ARIZONA_ASRC_ASYNC_WARP_OVERCLOCKED_STS)
		dev_err(arizona->dev, "ASRC async WARP overclocked\n");
	if (val[1] & ARIZONA_ASRC_SYNC_SYS_OVERCLOCKED_STS)
		dev_err(arizona->dev, "ASRC sync system overclocked\n");
	if (val[1] & ARIZONA_ASRC_SYNC_WARP_OVERCLOCKED_STS)
		dev_err(arizona->dev, "ASRC sync WARP overclocked\n");
	if (val[1] & ARIZONA_ADSP2_1_OVERCLOCKED_STS)
		dev_err(arizona->dev, "DSP1 overclocked\n");
	if (val[1] & ARIZONA_ISRC3_OVERCLOCKED_STS)
		dev_err(arizona->dev, "ISRC3 overclocked\n");
	if (val[1] & ARIZONA_ISRC2_OVERCLOCKED_STS)
		dev_err(arizona->dev, "ISRC2 overclocked\n");
	if (val[1] & ARIZONA_ISRC1_OVERCLOCKED_STS)
		dev_err(arizona->dev, "ISRC1 overclocked\n");

	if (val[2] & ARIZONA_SPDIF_OVERCLOCKED_STS)
		dev_err(arizona->dev, "SPDIF overclocked\n");

	return IRQ_HANDLED;
}

#define ARIZONA_REG_POLL_DELAY_US 7500

static inline bool arizona_poll_reg_delay(ktime_t timeout)
{
	if (ktime_compare(ktime_get(), timeout) > 0)
		return false;

	usleep_range(ARIZONA_REG_POLL_DELAY_US / 2, ARIZONA_REG_POLL_DELAY_US);

	return true;
}

static int arizona_poll_reg(struct arizona *arizona,
			    int timeout_ms, unsigned int reg,
			    unsigned int mask, unsigned int target)
{
	ktime_t timeout = ktime_add_us(ktime_get(), timeout_ms * USEC_PER_MSEC);
	unsigned int val = 0;
	int ret;

	do {
		ret = regmap_read(arizona->regmap, reg, &val);

		if ((val & mask) == target)
			return 0;
	} while (arizona_poll_reg_delay(timeout));

	if (ret) {
		dev_err(arizona->dev, "Failed polling reg 0x%x: %d\n",
			reg, ret);
		return ret;
	}

	dev_err(arizona->dev, "Polling reg 0x%x timed out: %x\n", reg, val);
	return -ETIMEDOUT;
}

static int arizona_wait_for_boot(struct arizona *arizona)
{
	int ret;

	/*
	 * We can't use an interrupt as we need to runtime resume to do so,
	 * we won't race with the interrupt handler as it'll be blocked on
	 * runtime resume.
	 */
	ret = arizona_poll_reg(arizona, 30, ARIZONA_INTERRUPT_RAW_STATUS_5,
			       ARIZONA_BOOT_DONE_STS, ARIZONA_BOOT_DONE_STS);

	if (!ret)
		regmap_write(arizona->regmap, ARIZONA_INTERRUPT_STATUS_5,
			     ARIZONA_BOOT_DONE_STS);

	pm_runtime_mark_last_busy(arizona->dev);

	return ret;
}

static inline void arizona_enable_reset(struct arizona *arizona)
{
	if (arizona->pdata.reset)
		gpiod_set_raw_value_cansleep(arizona->pdata.reset, 0);
}

static void arizona_disable_reset(struct arizona *arizona)
{
	if (arizona->pdata.reset) {
		switch (arizona->type) {
		case WM5110:
		case WM8280:
			/* Meet requirements for minimum reset duration */
			usleep_range(5000, 10000);
			break;
		default:
			break;
		}

		gpiod_set_raw_value_cansleep(arizona->pdata.reset, 1);
		usleep_range(1000, 5000);
	}
}

struct arizona_sysclk_state {
	unsigned int fll;
	unsigned int sysclk;
};

static int arizona_enable_freerun_sysclk(struct arizona *arizona,
					 struct arizona_sysclk_state *state)
{
	int ret, err;

	/* Cache existing FLL and SYSCLK settings */
	ret = regmap_read(arizona->regmap, ARIZONA_FLL1_CONTROL_1, &state->fll);
	if (ret) {
		dev_err(arizona->dev, "Failed to cache FLL settings: %d\n",
			ret);
		return ret;
	}
	ret = regmap_read(arizona->regmap, ARIZONA_SYSTEM_CLOCK_1,
			  &state->sysclk);
	if (ret) {
		dev_err(arizona->dev, "Failed to cache SYSCLK settings: %d\n",
			ret);
		return ret;
	}

	/* Start up SYSCLK using the FLL in free running mode */
	ret = regmap_write(arizona->regmap, ARIZONA_FLL1_CONTROL_1,
			ARIZONA_FLL1_ENA | ARIZONA_FLL1_FREERUN);
	if (ret) {
		dev_err(arizona->dev,
			"Failed to start FLL in freerunning mode: %d\n",
			ret);
		return ret;
	}
	ret = arizona_poll_reg(arizona, 180, ARIZONA_INTERRUPT_RAW_STATUS_5,
			       ARIZONA_FLL1_CLOCK_OK_STS,
			       ARIZONA_FLL1_CLOCK_OK_STS);
	if (ret)
		goto err_fll;

	ret = regmap_write(arizona->regmap, ARIZONA_SYSTEM_CLOCK_1, 0x0144);
	if (ret) {
		dev_err(arizona->dev, "Failed to start SYSCLK: %d\n", ret);
		goto err_fll;
	}

	return 0;

err_fll:
	err = regmap_write(arizona->regmap, ARIZONA_FLL1_CONTROL_1, state->fll);
	if (err)
		dev_err(arizona->dev,
			"Failed to re-apply old FLL settings: %d\n", err);

	return ret;
}

static int arizona_disable_freerun_sysclk(struct arizona *arizona,
					  struct arizona_sysclk_state *state)
{
	int ret;

	ret = regmap_write(arizona->regmap, ARIZONA_SYSTEM_CLOCK_1,
			   state->sysclk);
	if (ret) {
		dev_err(arizona->dev,
			"Failed to re-apply old SYSCLK settings: %d\n", ret);
		return ret;
	}

	ret = regmap_write(arizona->regmap, ARIZONA_FLL1_CONTROL_1, state->fll);
	if (ret) {
		dev_err(arizona->dev,
			"Failed to re-apply old FLL settings: %d\n", ret);
		return ret;
	}

	return 0;
}

static int wm5102_apply_hardware_patch(struct arizona *arizona)
{
	struct arizona_sysclk_state state;
	int err, ret;

	ret = arizona_enable_freerun_sysclk(arizona, &state);
	if (ret)
		return ret;

	/* Start the write sequencer and wait for it to finish */
	ret = regmap_write(arizona->regmap, ARIZONA_WRITE_SEQUENCER_CTRL_0,
			   ARIZONA_WSEQ_ENA | ARIZONA_WSEQ_START | 160);
	if (ret) {
		dev_err(arizona->dev, "Failed to start write sequencer: %d\n",
			ret);
		goto err;
	}

	ret = arizona_poll_reg(arizona, 30, ARIZONA_WRITE_SEQUENCER_CTRL_1,
			       ARIZONA_WSEQ_BUSY, 0);
	if (ret)
		regmap_write(arizona->regmap, ARIZONA_WRITE_SEQUENCER_CTRL_0,
			     ARIZONA_WSEQ_ABORT);

err:
	err = arizona_disable_freerun_sysclk(arizona, &state);

	return ret ?: err;
}

/*
 * Register patch to some of the CODECs internal write sequences
 * to ensure a clean exit from the low power sleep state.
 */
static const struct reg_sequence wm5110_sleep_patch[] = {
	{ 0x337A, 0xC100 },
	{ 0x337B, 0x0041 },
	{ 0x3300, 0xA210 },
	{ 0x3301, 0x050C },
};

static int wm5110_apply_sleep_patch(struct arizona *arizona)
{
	struct arizona_sysclk_state state;
	int err, ret;

	ret = arizona_enable_freerun_sysclk(arizona, &state);
	if (ret)
		return ret;

	ret = regmap_multi_reg_write_bypassed(arizona->regmap,
					      wm5110_sleep_patch,
					      ARRAY_SIZE(wm5110_sleep_patch));

	err = arizona_disable_freerun_sysclk(arizona, &state);

	return ret ?: err;
}

static int wm5102_clear_write_sequencer(struct arizona *arizona)
{
	int ret;

	ret = regmap_write(arizona->regmap, ARIZONA_WRITE_SEQUENCER_CTRL_3,
			   0x0);
	if (ret) {
		dev_err(arizona->dev,
			"Failed to clear write sequencer state: %d\n", ret);
		return ret;
	}

	arizona_enable_reset(arizona);
	regulator_disable(arizona->dcvdd);

	msleep(20);

	ret = regulator_enable(arizona->dcvdd);
	if (ret) {
		dev_err(arizona->dev, "Failed to re-enable DCVDD: %d\n", ret);
		return ret;
	}
	arizona_disable_reset(arizona);

	return 0;
}

#ifdef CONFIG_PM
static int arizona_isolate_dcvdd(struct arizona *arizona)
{
	int ret;

	ret = regmap_update_bits(arizona->regmap,
				 ARIZONA_ISOLATION_CONTROL,
				 ARIZONA_ISOLATE_DCVDD1,
				 ARIZONA_ISOLATE_DCVDD1);
	if (ret != 0)
		dev_err(arizona->dev, "Failed to isolate DCVDD: %d\n", ret);

	return ret;
}

static int arizona_connect_dcvdd(struct arizona *arizona)
{
	int ret;

	ret = regmap_update_bits(arizona->regmap,
				 ARIZONA_ISOLATION_CONTROL,
				 ARIZONA_ISOLATE_DCVDD1, 0);
	if (ret != 0)
		dev_err(arizona->dev, "Failed to connect DCVDD: %d\n", ret);

	return ret;
}

static int arizona_is_jack_det_active(struct arizona *arizona)
{
	unsigned int val;
	int ret;

	ret = regmap_read(arizona->regmap, ARIZONA_JACK_DETECT_ANALOGUE, &val);
	if (ret) {
		dev_err(arizona->dev,
			"Failed to check jack det status: %d\n", ret);
		return ret;
	} else if (val & ARIZONA_JD1_ENA) {
		return 1;
	} else {
		return 0;
	}
}

static int arizona_runtime_resume(struct device *dev)
{
	struct arizona *arizona = dev_get_drvdata(dev);
	int ret;

	dev_dbg(arizona->dev, "Leaving AoD mode\n");

	if (arizona->has_fully_powered_off) {
		dev_dbg(arizona->dev, "Re-enabling core supplies\n");

		ret = regulator_bulk_enable(arizona->num_core_supplies,
					    arizona->core_supplies);
		if (ret) {
			dev_err(dev, "Failed to enable core supplies: %d\n",
				ret);
			return ret;
		}
	}

	ret = regulator_enable(arizona->dcvdd);
	if (ret != 0) {
		dev_err(arizona->dev, "Failed to enable DCVDD: %d\n", ret);
		if (arizona->has_fully_powered_off)
			regulator_bulk_disable(arizona->num_core_supplies,
					       arizona->core_supplies);
		return ret;
	}

	if (arizona->has_fully_powered_off) {
		arizona_disable_reset(arizona);
		enable_irq(arizona->irq);
		arizona->has_fully_powered_off = false;
	}

	regcache_cache_only(arizona->regmap, false);

	switch (arizona->type) {
	case WM5102:
		if (arizona->external_dcvdd) {
			ret = arizona_connect_dcvdd(arizona);
			if (ret != 0)
				goto err;
		}

		ret = wm5102_patch(arizona);
		if (ret != 0) {
			dev_err(arizona->dev, "Failed to apply patch: %d\n",
				ret);
			goto err;
		}

		ret = wm5102_apply_hardware_patch(arizona);
		if (ret) {
			dev_err(arizona->dev,
				"Failed to apply hardware patch: %d\n",
				ret);
			goto err;
		}
		break;
	case WM5110:
	case WM8280:
		ret = arizona_wait_for_boot(arizona);
		if (ret)
			goto err;

		if (arizona->external_dcvdd) {
			ret = arizona_connect_dcvdd(arizona);
			if (ret != 0)
				goto err;
		} else {
			/*
			 * As this is only called for the internal regulator
			 * (where we know voltage ranges available) it is ok
			 * to request an exact range.
			 */
			ret = regulator_set_voltage(arizona->dcvdd,
						    1200000, 1200000);
			if (ret < 0) {
				dev_err(arizona->dev,
					"Failed to set resume voltage: %d\n",
					ret);
				goto err;
			}
		}

		ret = wm5110_apply_sleep_patch(arizona);
		if (ret) {
			dev_err(arizona->dev,
				"Failed to re-apply sleep patch: %d\n",
				ret);
			goto err;
		}
		break;
	case WM1831:
	case CS47L24:
		ret = arizona_wait_for_boot(arizona);
		if (ret != 0)
			goto err;
		break;
	default:
		ret = arizona_wait_for_boot(arizona);
		if (ret != 0)
			goto err;

		if (arizona->external_dcvdd) {
			ret = arizona_connect_dcvdd(arizona);
			if (ret != 0)
				goto err;
		}
		break;
	}

	ret = regcache_sync(arizona->regmap);
	if (ret != 0) {
		dev_err(arizona->dev, "Failed to restore register cache\n");
		goto err;
	}

	return 0;

err:
	regcache_cache_only(arizona->regmap, true);
	regulator_disable(arizona->dcvdd);
	return ret;
}

static int arizona_runtime_suspend(struct device *dev)
{
	struct arizona *arizona = dev_get_drvdata(dev);
	int jd_active = 0;
	int ret;

	dev_dbg(arizona->dev, "Entering AoD mode\n");

	switch (arizona->type) {
	case WM5110:
	case WM8280:
		jd_active = arizona_is_jack_det_active(arizona);
		if (jd_active < 0)
			return jd_active;

		if (arizona->external_dcvdd) {
			ret = arizona_isolate_dcvdd(arizona);
			if (ret != 0)
				return ret;
		} else {
			/*
			 * As this is only called for the internal regulator
			 * (where we know voltage ranges available) it is ok
			 * to request an exact range.
			 */
			ret = regulator_set_voltage(arizona->dcvdd,
						    1175000, 1175000);
			if (ret < 0) {
				dev_err(arizona->dev,
					"Failed to set suspend voltage: %d\n",
					ret);
				return ret;
			}
		}
		break;
	case WM5102:
		jd_active = arizona_is_jack_det_active(arizona);
		if (jd_active < 0)
			return jd_active;

		if (arizona->external_dcvdd) {
			ret = arizona_isolate_dcvdd(arizona);
			if (ret != 0)
				return ret;
		}

		if (!jd_active) {
			ret = regmap_write(arizona->regmap,
					   ARIZONA_WRITE_SEQUENCER_CTRL_3, 0x0);
			if (ret) {
				dev_err(arizona->dev,
					"Failed to clear write sequencer: %d\n",
					ret);
				return ret;
			}
		}
		break;
	case WM1831:
	case CS47L24:
		break;
	default:
		jd_active = arizona_is_jack_det_active(arizona);
		if (jd_active < 0)
			return jd_active;

		if (arizona->external_dcvdd) {
			ret = arizona_isolate_dcvdd(arizona);
			if (ret != 0)
				return ret;
		}
		break;
	}

	regcache_cache_only(arizona->regmap, true);
	regcache_mark_dirty(arizona->regmap);
	regulator_disable(arizona->dcvdd);

	/* Allow us to completely power down if no jack detection */
	if (!jd_active) {
		dev_dbg(arizona->dev, "Fully powering off\n");

		arizona->has_fully_powered_off = true;

		disable_irq_nosync(arizona->irq);
		arizona_enable_reset(arizona);
		regulator_bulk_disable(arizona->num_core_supplies,
				       arizona->core_supplies);
	}

	return 0;
}
#endif

#ifdef CONFIG_PM_SLEEP
static int arizona_suspend(struct device *dev)
{
	struct arizona *arizona = dev_get_drvdata(dev);

	dev_dbg(arizona->dev, "Suspend, disabling IRQ\n");
	disable_irq(arizona->irq);

	return 0;
}

static int arizona_suspend_noirq(struct device *dev)
{
	struct arizona *arizona = dev_get_drvdata(dev);

	dev_dbg(arizona->dev, "Late suspend, reenabling IRQ\n");
	enable_irq(arizona->irq);

	return 0;
}

static int arizona_resume_noirq(struct device *dev)
{
	struct arizona *arizona = dev_get_drvdata(dev);

	dev_dbg(arizona->dev, "Early resume, disabling IRQ\n");
	disable_irq(arizona->irq);

	return 0;
}

static int arizona_resume(struct device *dev)
{
	struct arizona *arizona = dev_get_drvdata(dev);

	dev_dbg(arizona->dev, "Resume, reenabling IRQ\n");
	enable_irq(arizona->irq);

	return 0;
}
#endif

const struct dev_pm_ops arizona_pm_ops = {
	SET_RUNTIME_PM_OPS(arizona_runtime_suspend,
			   arizona_runtime_resume,
			   NULL)
	SET_SYSTEM_SLEEP_PM_OPS(arizona_suspend, arizona_resume)
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(arizona_suspend_noirq,
				      arizona_resume_noirq)
};
EXPORT_SYMBOL_GPL(arizona_pm_ops);

#ifdef CONFIG_OF
static int arizona_of_get_core_pdata(struct arizona *arizona)
{
	struct arizona_pdata *pdata = &arizona->pdata;
	int ret, i;

	/* Handle old non-standard DT binding */
	pdata->reset = devm_gpiod_get(arizona->dev, "wlf,reset", GPIOD_OUT_LOW);
	if (IS_ERR(pdata->reset)) {
		ret = PTR_ERR(pdata->reset);

		/*
		 * Reset missing will be caught when other binding is read
		 * but all other errors imply this binding is in use but has
		 * encountered a problem so should be handled.
		 */
		if (ret == -EPROBE_DEFER)
			return ret;
		else if (ret != -ENOENT && ret != -ENOSYS)
			dev_err(arizona->dev, "Reset GPIO malformed: %d\n",
				ret);

		pdata->reset = NULL;
	}

	ret = of_property_read_u32_array(arizona->dev->of_node,
					 "wlf,gpio-defaults",
					 pdata->gpio_defaults,
					 ARRAY_SIZE(pdata->gpio_defaults));
	if (ret >= 0) {
		/*
		 * All values are literal except out of range values
		 * which are chip default, translate into platform
		 * data which uses 0 as chip default and out of range
		 * as zero.
		 */
		for (i = 0; i < ARRAY_SIZE(pdata->gpio_defaults); i++) {
			if (pdata->gpio_defaults[i] > 0xffff)
				pdata->gpio_defaults[i] = 0;
			else if (pdata->gpio_defaults[i] == 0)
				pdata->gpio_defaults[i] = 0x10000;
		}
	} else {
		dev_err(arizona->dev, "Failed to parse GPIO defaults: %d\n",
			ret);
	}

	return 0;
}

const struct of_device_id arizona_of_match[] = {
	{ .compatible = "wlf,wm5102", .data = (void *)WM5102 },
	{ .compatible = "wlf,wm5110", .data = (void *)WM5110 },
	{ .compatible = "wlf,wm8280", .data = (void *)WM8280 },
	{ .compatible = "wlf,wm8997", .data = (void *)WM8997 },
	{ .compatible = "wlf,wm8998", .data = (void *)WM8998 },
	{ .compatible = "wlf,wm1814", .data = (void *)WM1814 },
	{ .compatible = "wlf,wm1831", .data = (void *)WM1831 },
	{ .compatible = "cirrus,cs47l24", .data = (void *)CS47L24 },
	{},
};
EXPORT_SYMBOL_GPL(arizona_of_match);
#else
static inline int arizona_of_get_core_pdata(struct arizona *arizona)
{
	return 0;
}
#endif

static const struct mfd_cell early_devs[] = {
	{ .name = "arizona-ldo1" },
};

static const char * const wm5102_supplies[] = {
	"MICVDD",
	"DBVDD2",
	"DBVDD3",
	"CPVDD",
	"SPKVDDL",
	"SPKVDDR",
};

static const struct mfd_cell wm5102_devs[] = {
	{ .name = "arizona-micsupp" },
	{ .name = "arizona-gpio" },
	{ .name = "arizona-haptics" },
	{ .name = "arizona-pwm" },
	{
		.name = "wm5102-codec",
		.parent_supplies = wm5102_supplies,
		.num_parent_supplies = ARRAY_SIZE(wm5102_supplies),
	},
};

static const struct mfd_cell wm5110_devs[] = {
	{ .name = "arizona-micsupp" },
	{ .name = "arizona-gpio" },
	{ .name = "arizona-haptics" },
	{ .name = "arizona-pwm" },
	{
		.name = "wm5110-codec",
		.parent_supplies = wm5102_supplies,
		.num_parent_supplies = ARRAY_SIZE(wm5102_supplies),
	},
};

static const char * const cs47l24_supplies[] = {
	"MICVDD",
	"CPVDD",
	"SPKVDD",
};

static const struct mfd_cell cs47l24_devs[] = {
	{ .name = "arizona-gpio" },
	{ .name = "arizona-haptics" },
	{ .name = "arizona-pwm" },
	{
		.name = "cs47l24-codec",
		.parent_supplies = cs47l24_supplies,
		.num_parent_supplies = ARRAY_SIZE(cs47l24_supplies),
	},
};

static const char * const wm8997_supplies[] = {
	"MICVDD",
	"DBVDD2",
	"CPVDD",
	"SPKVDD",
};

static const struct mfd_cell wm8997_devs[] = {
	{ .name = "arizona-micsupp" },
	{ .name = "arizona-gpio" },
	{ .name = "arizona-haptics" },
	{ .name = "arizona-pwm" },
	{
		.name = "wm8997-codec",
		.parent_supplies = wm8997_supplies,
		.num_parent_supplies = ARRAY_SIZE(wm8997_supplies),
	},
};

static const struct mfd_cell wm8998_devs[] = {
	{ .name = "arizona-micsupp" },
	{ .name = "arizona-gpio" },
	{ .name = "arizona-haptics" },
	{ .name = "arizona-pwm" },
	{
		.name = "wm8998-codec",
		.parent_supplies = wm5102_supplies,
		.num_parent_supplies = ARRAY_SIZE(wm5102_supplies),
	},
};

int arizona_dev_init(struct arizona *arizona)
{
	static const char * const mclk_name[] = { "mclk1", "mclk2" };
	struct device *dev = arizona->dev;
	const char *type_name = NULL;
	unsigned int reg, val;
	int (*apply_patch)(struct arizona *) = NULL;
	const struct mfd_cell *subdevs = NULL;
	int n_subdevs = 0, ret, i;

	dev_set_drvdata(arizona->dev, arizona);
	mutex_init(&arizona->clk_lock);

	if (dev_get_platdata(arizona->dev)) {
		memcpy(&arizona->pdata, dev_get_platdata(arizona->dev),
		       sizeof(arizona->pdata));
	} else {
		ret = arizona_of_get_core_pdata(arizona);
		if (ret < 0)
			return ret;
	}

	BUILD_BUG_ON(ARRAY_SIZE(arizona->mclk) != ARRAY_SIZE(mclk_name));
	for (i = 0; i < ARRAY_SIZE(arizona->mclk); i++) {
		arizona->mclk[i] = devm_clk_get(arizona->dev, mclk_name[i]);
		if (IS_ERR(arizona->mclk[i])) {
			dev_info(arizona->dev, "Failed to get %s: %ld\n",
				 mclk_name[i], PTR_ERR(arizona->mclk[i]));
			arizona->mclk[i] = NULL;
		}
	}

	regcache_cache_only(arizona->regmap, true);

	switch (arizona->type) {
	case WM5102:
	case WM5110:
	case WM8280:
	case WM8997:
	case WM8998:
	case WM1814:
	case WM1831:
	case CS47L24:
		for (i = 0; i < ARRAY_SIZE(wm5102_core_supplies); i++)
			arizona->core_supplies[i].supply
				= wm5102_core_supplies[i];
		arizona->num_core_supplies = ARRAY_SIZE(wm5102_core_supplies);
		break;
	default:
		dev_err(arizona->dev, "Unknown device type %d\n",
			arizona->type);
		return -ENODEV;
	}

	/* Mark DCVDD as external, LDO1 driver will clear if internal */
	arizona->external_dcvdd = true;

	switch (arizona->type) {
	case WM1831:
	case CS47L24:
		break; /* No LDO1 regulator */
	default:
		ret = mfd_add_devices(arizona->dev, -1, early_devs,
				      ARRAY_SIZE(early_devs), NULL, 0, NULL);
		if (ret != 0) {
			dev_err(dev, "Failed to add early children: %d\n", ret);
			return ret;
		}
		break;
	}

	ret = devm_regulator_bulk_get(dev, arizona->num_core_supplies,
				      arizona->core_supplies);
	if (ret != 0) {
		dev_err(dev, "Failed to request core supplies: %d\n",
			ret);
		goto err_early;
	}

	/**
	 * Don't use devres here because the only device we have to get
	 * against is the MFD device and DCVDD will likely be supplied by
	 * one of its children. Meaning that the regulator will be
	 * destroyed by the time devres calls regulator put.
	 */
	arizona->dcvdd = regulator_get(arizona->dev, "DCVDD");
	if (IS_ERR(arizona->dcvdd)) {
		ret = PTR_ERR(arizona->dcvdd);
		dev_err(dev, "Failed to request DCVDD: %d\n", ret);
		goto err_early;
	}

	if (!arizona->pdata.reset) {
		/* Start out with /RESET low to put the chip into reset */
		arizona->pdata.reset = devm_gpiod_get(arizona->dev, "reset",
						      GPIOD_OUT_LOW);
		if (IS_ERR(arizona->pdata.reset)) {
			ret = PTR_ERR(arizona->pdata.reset);
			if (ret == -EPROBE_DEFER)
				goto err_dcvdd;

			dev_err(arizona->dev,
				"Reset GPIO missing/malformed: %d\n", ret);

			arizona->pdata.reset = NULL;
		}
	}

	ret = regulator_bulk_enable(arizona->num_core_supplies,
				    arizona->core_supplies);
	if (ret != 0) {
		dev_err(dev, "Failed to enable core supplies: %d\n",
			ret);
		goto err_dcvdd;
	}

	ret = regulator_enable(arizona->dcvdd);
	if (ret != 0) {
		dev_err(dev, "Failed to enable DCVDD: %d\n", ret);
		goto err_enable;
	}

	arizona_disable_reset(arizona);

	regcache_cache_only(arizona->regmap, false);

	/* Verify that this is a chip we know about */
	ret = regmap_read(arizona->regmap, ARIZONA_SOFTWARE_RESET, &reg);
	if (ret != 0) {
		dev_err(dev, "Failed to read ID register: %d\n", ret);
		goto err_reset;
	}

	switch (reg) {
	case 0x5102:
	case 0x5110:
	case 0x6349:
	case 0x6363:
	case 0x8997:
		break;
	default:
		dev_err(arizona->dev, "Unknown device ID: %x\n", reg);
		ret = -ENODEV;
		goto err_reset;
	}

	/* If we have a /RESET GPIO we'll already be reset */
	if (!arizona->pdata.reset) {
		ret = regmap_write(arizona->regmap, ARIZONA_SOFTWARE_RESET, 0);
		if (ret != 0) {
			dev_err(dev, "Failed to reset device: %d\n", ret);
			goto err_reset;
		}

		usleep_range(1000, 5000);
	}

	/* Ensure device startup is complete */
	switch (arizona->type) {
	case WM5102:
		ret = regmap_read(arizona->regmap,
				  ARIZONA_WRITE_SEQUENCER_CTRL_3, &val);
		if (ret) {
			dev_err(dev,
				"Failed to check write sequencer state: %d\n",
				ret);
		} else if (val & 0x01) {
			ret = wm5102_clear_write_sequencer(arizona);
			if (ret)
				return ret;
		}
		break;
	default:
		break;
	}

	ret = arizona_wait_for_boot(arizona);
	if (ret) {
		dev_err(arizona->dev, "Device failed initial boot: %d\n", ret);
		goto err_reset;
	}

	/* Read the device ID information & do device specific stuff */
	ret = regmap_read(arizona->regmap, ARIZONA_SOFTWARE_RESET, &reg);
	if (ret != 0) {
		dev_err(dev, "Failed to read ID register: %d\n", ret);
		goto err_reset;
	}

	ret = regmap_read(arizona->regmap, ARIZONA_DEVICE_REVISION,
			  &arizona->rev);
	if (ret != 0) {
		dev_err(dev, "Failed to read revision register: %d\n", ret);
		goto err_reset;
	}
	arizona->rev &= ARIZONA_DEVICE_REVISION_MASK;

	switch (reg) {
	case 0x5102:
		if (IS_ENABLED(CONFIG_MFD_WM5102)) {
			type_name = "WM5102";
			if (arizona->type != WM5102) {
				dev_warn(arizona->dev,
					 "WM5102 registered as %d\n",
					 arizona->type);
				arizona->type = WM5102;
			}

			apply_patch = wm5102_patch;
			arizona->rev &= 0x7;
			subdevs = wm5102_devs;
			n_subdevs = ARRAY_SIZE(wm5102_devs);
		}
		break;
	case 0x5110:
		if (IS_ENABLED(CONFIG_MFD_WM5110)) {
			switch (arizona->type) {
			case WM5110:
				type_name = "WM5110";
				break;
			case WM8280:
				type_name = "WM8280";
				break;
			default:
				type_name = "WM5110";
				dev_warn(arizona->dev,
					 "WM5110 registered as %d\n",
					 arizona->type);
				arizona->type = WM5110;
				break;
			}

			apply_patch = wm5110_patch;
			subdevs = wm5110_devs;
			n_subdevs = ARRAY_SIZE(wm5110_devs);
		}
		break;
	case 0x6363:
		if (IS_ENABLED(CONFIG_MFD_CS47L24)) {
			switch (arizona->type) {
			case CS47L24:
				type_name = "CS47L24";
				break;

			case WM1831:
				type_name = "WM1831";
				break;

			default:
				dev_warn(arizona->dev,
					 "CS47L24 registered as %d\n",
					 arizona->type);
				arizona->type = CS47L24;
				break;
			}

			apply_patch = cs47l24_patch;
			subdevs = cs47l24_devs;
			n_subdevs = ARRAY_SIZE(cs47l24_devs);
		}
		break;
	case 0x8997:
		if (IS_ENABLED(CONFIG_MFD_WM8997)) {
			type_name = "WM8997";
			if (arizona->type != WM8997) {
				dev_warn(arizona->dev,
					 "WM8997 registered as %d\n",
					 arizona->type);
				arizona->type = WM8997;
			}

			apply_patch = wm8997_patch;
			subdevs = wm8997_devs;
			n_subdevs = ARRAY_SIZE(wm8997_devs);
		}
		break;
	case 0x6349:
		if (IS_ENABLED(CONFIG_MFD_WM8998)) {
			switch (arizona->type) {
			case WM8998:
				type_name = "WM8998";
				break;

			case WM1814:
				type_name = "WM1814";
				break;

			default:
				type_name = "WM8998";
				dev_warn(arizona->dev,
					 "WM8998 registered as %d\n",
					 arizona->type);
				arizona->type = WM8998;
			}

			apply_patch = wm8998_patch;
			subdevs = wm8998_devs;
			n_subdevs = ARRAY_SIZE(wm8998_devs);
		}
		break;
	default:
		dev_err(arizona->dev, "Unknown device ID %x\n", reg);
		ret = -ENODEV;
		goto err_reset;
	}

	if (!subdevs) {
		dev_err(arizona->dev,
			"No kernel support for device ID %x\n", reg);
		ret = -ENODEV;
		goto err_reset;
	}

	dev_info(dev, "%s revision %c\n", type_name, arizona->rev + 'A');

	if (apply_patch) {
		ret = apply_patch(arizona);
		if (ret != 0) {
			dev_err(arizona->dev, "Failed to apply patch: %d\n",
				ret);
			goto err_reset;
		}

		switch (arizona->type) {
		case WM5102:
			ret = wm5102_apply_hardware_patch(arizona);
			if (ret) {
				dev_err(arizona->dev,
					"Failed to apply hardware patch: %d\n",
					ret);
				goto err_reset;
			}
			break;
		case WM5110:
		case WM8280:
			ret = wm5110_apply_sleep_patch(arizona);
			if (ret) {
				dev_err(arizona->dev,
					"Failed to apply sleep patch: %d\n",
					ret);
				goto err_reset;
			}
			break;
		default:
			break;
		}
	}

	for (i = 0; i < ARRAY_SIZE(arizona->pdata.gpio_defaults); i++) {
		if (!arizona->pdata.gpio_defaults[i])
			continue;

		regmap_write(arizona->regmap, ARIZONA_GPIO1_CTRL + i,
			     arizona->pdata.gpio_defaults[i]);
	}

	/* Chip default */
	if (!arizona->pdata.clk32k_src)
		arizona->pdata.clk32k_src = ARIZONA_32KZ_MCLK2;

	switch (arizona->pdata.clk32k_src) {
	case ARIZONA_32KZ_MCLK1:
	case ARIZONA_32KZ_MCLK2:
		regmap_update_bits(arizona->regmap, ARIZONA_CLOCK_32K_1,
				   ARIZONA_CLK_32K_SRC_MASK,
				   arizona->pdata.clk32k_src - 1);
		arizona_clk32k_enable(arizona);
		break;
	case ARIZONA_32KZ_NONE:
		regmap_update_bits(arizona->regmap, ARIZONA_CLOCK_32K_1,
				   ARIZONA_CLK_32K_SRC_MASK, 2);
		break;
	default:
		dev_err(arizona->dev, "Invalid 32kHz clock source: %d\n",
			arizona->pdata.clk32k_src);
		ret = -EINVAL;
		goto err_reset;
	}

	for (i = 0; i < ARIZONA_MAX_MICBIAS; i++) {
		if (!arizona->pdata.micbias[i].mV &&
		    !arizona->pdata.micbias[i].bypass)
			continue;

		/* Apply default for bypass mode */
		if (!arizona->pdata.micbias[i].mV)
			arizona->pdata.micbias[i].mV = 2800;

		val = (arizona->pdata.micbias[i].mV - 1500) / 100;

		val <<= ARIZONA_MICB1_LVL_SHIFT;

		if (arizona->pdata.micbias[i].ext_cap)
			val |= ARIZONA_MICB1_EXT_CAP;

		if (arizona->pdata.micbias[i].discharge)
			val |= ARIZONA_MICB1_DISCH;

		if (arizona->pdata.micbias[i].soft_start)
			val |= ARIZONA_MICB1_RATE;

		if (arizona->pdata.micbias[i].bypass)
			val |= ARIZONA_MICB1_BYPASS;

		regmap_update_bits(arizona->regmap,
				   ARIZONA_MIC_BIAS_CTRL_1 + i,
				   ARIZONA_MICB1_LVL_MASK |
				   ARIZONA_MICB1_EXT_CAP |
				   ARIZONA_MICB1_DISCH |
				   ARIZONA_MICB1_BYPASS |
				   ARIZONA_MICB1_RATE, val);
	}

	pm_runtime_set_active(arizona->dev);
	pm_runtime_enable(arizona->dev);

	/* Set up for interrupts */
	ret = arizona_irq_init(arizona);
	if (ret != 0)
		goto err_pm;

	pm_runtime_set_autosuspend_delay(arizona->dev, 100);
	pm_runtime_use_autosuspend(arizona->dev);

	arizona_request_irq(arizona, ARIZONA_IRQ_CLKGEN_ERR, "CLKGEN error",
			    arizona_clkgen_err, arizona);
	arizona_request_irq(arizona, ARIZONA_IRQ_OVERCLOCKED, "Overclocked",
			    arizona_overclocked, arizona);
	arizona_request_irq(arizona, ARIZONA_IRQ_UNDERCLOCKED, "Underclocked",
			    arizona_underclocked, arizona);

	ret = mfd_add_devices(arizona->dev, PLATFORM_DEVID_NONE,
			      subdevs, n_subdevs, NULL, 0, NULL);

	if (ret) {
		dev_err(arizona->dev, "Failed to add subdevices: %d\n", ret);
		goto err_irq;
	}

	return 0;

err_irq:
	arizona_irq_exit(arizona);
err_pm:
	pm_runtime_disable(arizona->dev);

	switch (arizona->pdata.clk32k_src) {
	case ARIZONA_32KZ_MCLK1:
	case ARIZONA_32KZ_MCLK2:
		arizona_clk32k_disable(arizona);
		break;
	default:
		break;
	}
err_reset:
	arizona_enable_reset(arizona);
	regulator_disable(arizona->dcvdd);
err_enable:
	regulator_bulk_disable(arizona->num_core_supplies,
			       arizona->core_supplies);
err_dcvdd:
	regulator_put(arizona->dcvdd);
err_early:
	mfd_remove_devices(dev);
	return ret;
}
EXPORT_SYMBOL_GPL(arizona_dev_init);

int arizona_dev_exit(struct arizona *arizona)
{
	disable_irq(arizona->irq);
	pm_runtime_disable(arizona->dev);

	regulator_disable(arizona->dcvdd);
	regulator_put(arizona->dcvdd);

	switch (arizona->pdata.clk32k_src) {
	case ARIZONA_32KZ_MCLK1:
	case ARIZONA_32KZ_MCLK2:
		arizona_clk32k_disable(arizona);
		break;
	default:
		break;
	}

	mfd_remove_devices(arizona->dev);
	arizona_free_irq(arizona, ARIZONA_IRQ_UNDERCLOCKED, arizona);
	arizona_free_irq(arizona, ARIZONA_IRQ_OVERCLOCKED, arizona);
	arizona_free_irq(arizona, ARIZONA_IRQ_CLKGEN_ERR, arizona);
	arizona_irq_exit(arizona);
	arizona_enable_reset(arizona);

	regulator_bulk_disable(arizona->num_core_supplies,
			       arizona->core_supplies);
	return 0;
}
EXPORT_SYMBOL_GPL(arizona_dev_exit);

MODULE_LICENSE("GPL v2");
