/*
 * Arizona core driver
 *
 * Copyright 2012 Wolfson Microelectronics plc
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/machine.h>
#include <linux/slab.h>

#include <linux/mfd/arizona/core.h>
#include <linux/mfd/arizona/registers.h>

#include "arizona.h"

static const char *wm5102_core_supplies[] = {
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
				goto out;
			break;
		}

		ret = regmap_update_bits(arizona->regmap, ARIZONA_CLOCK_32K_1,
					 ARIZONA_CLK_32K_ENA,
					 ARIZONA_CLK_32K_ENA);
	}

out:
	if (ret != 0)
		arizona->clk32k_ref--;

	mutex_unlock(&arizona->clk_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(arizona_clk32k_enable);

int arizona_clk32k_disable(struct arizona *arizona)
{
	int ret = 0;

	mutex_lock(&arizona->clk_lock);

	BUG_ON(arizona->clk32k_ref <= 0);

	arizona->clk32k_ref--;

	if (arizona->clk32k_ref == 0) {
		regmap_update_bits(arizona->regmap, ARIZONA_CLOCK_32K_1,
				   ARIZONA_CLK_32K_ENA, 0);

		switch (arizona->pdata.clk32k_src) {
		case ARIZONA_32KZ_MCLK1:
			pm_runtime_put_sync(arizona->dev);
			break;
		}
	}

	mutex_unlock(&arizona->clk_lock);

	return ret;
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
	unsigned int val[2];
	int ret;
	
	ret = regmap_bulk_read(arizona->regmap, ARIZONA_INTERRUPT_RAW_STATUS_6,
			       &val[0], 2);
	if (ret != 0) {
		dev_err(arizona->dev, "Failed to read overclock status: %d\n",
			ret);
		return IRQ_NONE;
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
	if (val[1] & ARIZONA_ISRC2_OVERCLOCKED_STS)
		dev_err(arizona->dev, "ISRC2 overclocked\n");
	if (val[1] & ARIZONA_ISRC1_OVERCLOCKED_STS)
		dev_err(arizona->dev, "ISRC1 overclocked\n");

	return IRQ_HANDLED;
}

static int arizona_poll_reg(struct arizona *arizona,
			    int timeout, unsigned int reg,
			    unsigned int mask, unsigned int target)
{
	unsigned int val = 0;
	int ret, i;

	for (i = 0; i < timeout; i++) {
		ret = regmap_read(arizona->regmap, reg, &val);
		if (ret != 0) {
			dev_err(arizona->dev, "Failed to read reg %u: %d\n",
				reg, ret);
			continue;
		}

		if ((val & mask) == target)
			return 0;

		msleep(1);
	}

	dev_err(arizona->dev, "Polling reg %u timed out: %x\n", reg, val);
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
	ret = arizona_poll_reg(arizona, 5, ARIZONA_INTERRUPT_RAW_STATUS_5,
			       ARIZONA_BOOT_DONE_STS, ARIZONA_BOOT_DONE_STS);

	if (!ret)
		regmap_write(arizona->regmap, ARIZONA_INTERRUPT_STATUS_5,
			     ARIZONA_BOOT_DONE_STS);

	pm_runtime_mark_last_busy(arizona->dev);

	return ret;
}

static int arizona_apply_hardware_patch(struct arizona* arizona)
{
	unsigned int fll, sysclk;
	int ret, err;

	regcache_cache_bypass(arizona->regmap, true);

	/* Cache existing FLL and SYSCLK settings */
	ret = regmap_read(arizona->regmap, ARIZONA_FLL1_CONTROL_1, &fll);
	if (ret != 0) {
		dev_err(arizona->dev, "Failed to cache FLL settings: %d\n",
			ret);
		return ret;
	}
	ret = regmap_read(arizona->regmap, ARIZONA_SYSTEM_CLOCK_1, &sysclk);
	if (ret != 0) {
		dev_err(arizona->dev, "Failed to cache SYSCLK settings: %d\n",
			ret);
		return ret;
	}

	/* Start up SYSCLK using the FLL in free running mode */
	ret = regmap_write(arizona->regmap, ARIZONA_FLL1_CONTROL_1,
			ARIZONA_FLL1_ENA | ARIZONA_FLL1_FREERUN);
	if (ret != 0) {
		dev_err(arizona->dev,
			"Failed to start FLL in freerunning mode: %d\n",
			ret);
		return ret;
	}
	ret = arizona_poll_reg(arizona, 25, ARIZONA_INTERRUPT_RAW_STATUS_5,
			       ARIZONA_FLL1_CLOCK_OK_STS,
			       ARIZONA_FLL1_CLOCK_OK_STS);
	if (ret != 0) {
		ret = -ETIMEDOUT;
		goto err_fll;
	}

	ret = regmap_write(arizona->regmap, ARIZONA_SYSTEM_CLOCK_1, 0x0144);
	if (ret != 0) {
		dev_err(arizona->dev, "Failed to start SYSCLK: %d\n", ret);
		goto err_fll;
	}

	/* Start the write sequencer and wait for it to finish */
	ret = regmap_write(arizona->regmap, ARIZONA_WRITE_SEQUENCER_CTRL_0,
			ARIZONA_WSEQ_ENA | ARIZONA_WSEQ_START | 160);
	if (ret != 0) {
		dev_err(arizona->dev, "Failed to start write sequencer: %d\n",
			ret);
		goto err_sysclk;
	}
	ret = arizona_poll_reg(arizona, 5, ARIZONA_WRITE_SEQUENCER_CTRL_1,
			       ARIZONA_WSEQ_BUSY, 0);
	if (ret != 0) {
		regmap_write(arizona->regmap, ARIZONA_WRITE_SEQUENCER_CTRL_0,
				ARIZONA_WSEQ_ABORT);
		ret = -ETIMEDOUT;
	}

err_sysclk:
	err = regmap_write(arizona->regmap, ARIZONA_SYSTEM_CLOCK_1, sysclk);
	if (err != 0) {
		dev_err(arizona->dev,
			"Failed to re-apply old SYSCLK settings: %d\n",
			err);
	}

err_fll:
	err = regmap_write(arizona->regmap, ARIZONA_FLL1_CONTROL_1, fll);
	if (err != 0) {
		dev_err(arizona->dev,
			"Failed to re-apply old FLL settings: %d\n",
			err);
	}

	regcache_cache_bypass(arizona->regmap, false);

	if (ret != 0)
		return ret;
	else
		return err;
}

#ifdef CONFIG_PM_RUNTIME
static int arizona_runtime_resume(struct device *dev)
{
	struct arizona *arizona = dev_get_drvdata(dev);
	int ret;

	dev_dbg(arizona->dev, "Leaving AoD mode\n");

	ret = regulator_enable(arizona->dcvdd);
	if (ret != 0) {
		dev_err(arizona->dev, "Failed to enable DCVDD: %d\n", ret);
		return ret;
	}

	regcache_cache_only(arizona->regmap, false);

	switch (arizona->type) {
	case WM5102:
		if (arizona->external_dcvdd) {
			ret = regmap_update_bits(arizona->regmap,
						 ARIZONA_ISOLATION_CONTROL,
						 ARIZONA_ISOLATE_DCVDD1, 0);
			if (ret != 0) {
				dev_err(arizona->dev,
					"Failed to connect DCVDD: %d\n", ret);
				goto err;
			}
		}

		ret = wm5102_patch(arizona);
		if (ret != 0) {
			dev_err(arizona->dev, "Failed to apply patch: %d\n",
				ret);
			goto err;
		}

		ret = arizona_apply_hardware_patch(arizona);
		if (ret != 0) {
			dev_err(arizona->dev,
				"Failed to apply hardware patch: %d\n",
				ret);
			goto err;
		}
		break;
	default:
		ret = arizona_wait_for_boot(arizona);
		if (ret != 0) {
			goto err;
		}

		if (arizona->external_dcvdd) {
			ret = regmap_update_bits(arizona->regmap,
						 ARIZONA_ISOLATION_CONTROL,
						 ARIZONA_ISOLATE_DCVDD1, 0);
			if (ret != 0) {
				dev_err(arizona->dev,
					"Failed to connect DCVDD: %d\n", ret);
				goto err;
			}
		}
		break;
	}

	switch (arizona->type) {
	case WM5102:
		ret = wm5102_patch(arizona);
		if (ret != 0) {
			dev_err(arizona->dev, "Failed to apply patch: %d\n",
				ret);
			goto err;
		}
	default:
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
	int ret;

	dev_dbg(arizona->dev, "Entering AoD mode\n");

	if (arizona->external_dcvdd) {
		ret = regmap_update_bits(arizona->regmap,
					 ARIZONA_ISOLATION_CONTROL,
					 ARIZONA_ISOLATE_DCVDD1,
					 ARIZONA_ISOLATE_DCVDD1);
		if (ret != 0) {
			dev_err(arizona->dev, "Failed to isolate DCVDD: %d\n",
				ret);
			return ret;
		}
	}

	regulator_disable(arizona->dcvdd);
	regcache_cache_only(arizona->regmap, true);
	regcache_mark_dirty(arizona->regmap);

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

static int arizona_suspend_late(struct device *dev)
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

	dev_dbg(arizona->dev, "Late resume, reenabling IRQ\n");
	enable_irq(arizona->irq);

	return 0;
}
#endif

const struct dev_pm_ops arizona_pm_ops = {
	SET_RUNTIME_PM_OPS(arizona_runtime_suspend,
			   arizona_runtime_resume,
			   NULL)
	SET_SYSTEM_SLEEP_PM_OPS(arizona_suspend, arizona_resume)
#ifdef CONFIG_PM_SLEEP
	.suspend_late = arizona_suspend_late,
	.resume_noirq = arizona_resume_noirq,
#endif
};
EXPORT_SYMBOL_GPL(arizona_pm_ops);

#ifdef CONFIG_OF
int arizona_of_get_type(struct device *dev)
{
	const struct of_device_id *id = of_match_device(arizona_of_match, dev);

	if (id)
		return (int)id->data;
	else
		return 0;
}
EXPORT_SYMBOL_GPL(arizona_of_get_type);

static int arizona_of_get_core_pdata(struct arizona *arizona)
{
	int ret, i;

	arizona->pdata.reset = of_get_named_gpio(arizona->dev->of_node,
						 "wlf,reset", 0);
	if (arizona->pdata.reset < 0)
		arizona->pdata.reset = 0;

	arizona->pdata.ldoena = of_get_named_gpio(arizona->dev->of_node,
						  "wlf,ldoena", 0);
	if (arizona->pdata.ldoena < 0)
		arizona->pdata.ldoena = 0;

	ret = of_property_read_u32_array(arizona->dev->of_node,
					 "wlf,gpio-defaults",
					 arizona->pdata.gpio_defaults,
					 ARRAY_SIZE(arizona->pdata.gpio_defaults));
	if (ret >= 0) {
		/*
		 * All values are literal except out of range values
		 * which are chip default, translate into platform
		 * data which uses 0 as chip default and out of range
		 * as zero.
		 */
		for (i = 0; i < ARRAY_SIZE(arizona->pdata.gpio_defaults); i++) {
			if (arizona->pdata.gpio_defaults[i] > 0xffff)
				arizona->pdata.gpio_defaults[i] = 0;
			if (arizona->pdata.gpio_defaults[i] == 0)
				arizona->pdata.gpio_defaults[i] = 0x10000;
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
	{},
};
EXPORT_SYMBOL_GPL(arizona_of_match);
#else
static inline int arizona_of_get_core_pdata(struct arizona *arizona)
{
	return 0;
}
#endif

static struct mfd_cell early_devs[] = {
	{ .name = "arizona-ldo1" },
};

static struct mfd_cell wm5102_devs[] = {
	{ .name = "arizona-micsupp" },
	{ .name = "arizona-extcon" },
	{ .name = "arizona-gpio" },
	{ .name = "arizona-haptics" },
	{ .name = "arizona-pwm" },
	{ .name = "wm5102-codec" },
};

static struct mfd_cell wm5110_devs[] = {
	{ .name = "arizona-micsupp" },
	{ .name = "arizona-extcon" },
	{ .name = "arizona-gpio" },
	{ .name = "arizona-haptics" },
	{ .name = "arizona-pwm" },
	{ .name = "wm5110-codec" },
};

int arizona_dev_init(struct arizona *arizona)
{
	struct device *dev = arizona->dev;
	const char *type_name;
	unsigned int reg, val;
	int (*apply_patch)(struct arizona *) = NULL;
	int ret, i;

	dev_set_drvdata(arizona->dev, arizona);
	mutex_init(&arizona->clk_lock);

	arizona_of_get_core_pdata(arizona);

	if (dev_get_platdata(arizona->dev))
		memcpy(&arizona->pdata, dev_get_platdata(arizona->dev),
		       sizeof(arizona->pdata));

	regcache_cache_only(arizona->regmap, true);

	switch (arizona->type) {
	case WM5102:
	case WM5110:
		for (i = 0; i < ARRAY_SIZE(wm5102_core_supplies); i++)
			arizona->core_supplies[i].supply
				= wm5102_core_supplies[i];
		arizona->num_core_supplies = ARRAY_SIZE(wm5102_core_supplies);
		break;
	default:
		dev_err(arizona->dev, "Unknown device type %d\n",
			arizona->type);
		return -EINVAL;
	}

	ret = mfd_add_devices(arizona->dev, -1, early_devs,
			      ARRAY_SIZE(early_devs), NULL, 0, NULL);
	if (ret != 0) {
		dev_err(dev, "Failed to add early children: %d\n", ret);
		return ret;
	}

	ret = devm_regulator_bulk_get(dev, arizona->num_core_supplies,
				      arizona->core_supplies);
	if (ret != 0) {
		dev_err(dev, "Failed to request core supplies: %d\n",
			ret);
		goto err_early;
	}

	arizona->dcvdd = devm_regulator_get(arizona->dev, "DCVDD");
	if (IS_ERR(arizona->dcvdd)) {
		ret = PTR_ERR(arizona->dcvdd);
		dev_err(dev, "Failed to request DCVDD: %d\n", ret);
		goto err_early;
	}

	if (arizona->pdata.reset) {
		/* Start out with /RESET low to put the chip into reset */
		ret = gpio_request_one(arizona->pdata.reset,
				       GPIOF_DIR_OUT | GPIOF_INIT_LOW,
				       "arizona /RESET");
		if (ret != 0) {
			dev_err(dev, "Failed to request /RESET: %d\n", ret);
			goto err_early;
		}
	}

	ret = regulator_bulk_enable(arizona->num_core_supplies,
				    arizona->core_supplies);
	if (ret != 0) {
		dev_err(dev, "Failed to enable core supplies: %d\n",
			ret);
		goto err_early;
	}

	ret = regulator_enable(arizona->dcvdd);
	if (ret != 0) {
		dev_err(dev, "Failed to enable DCVDD: %d\n", ret);
		goto err_enable;
	}

	if (arizona->pdata.reset) {
		gpio_set_value_cansleep(arizona->pdata.reset, 1);
		msleep(1);
	}

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
		break;
	default:
		dev_err(arizona->dev, "Unknown device ID: %x\n", reg);
		goto err_reset;
	}

	/* If we have a /RESET GPIO we'll already be reset */
	if (!arizona->pdata.reset) {
		regcache_mark_dirty(arizona->regmap);

		ret = regmap_write(arizona->regmap, ARIZONA_SOFTWARE_RESET, 0);
		if (ret != 0) {
			dev_err(dev, "Failed to reset device: %d\n", ret);
			goto err_reset;
		}

		msleep(1);

		ret = regcache_sync(arizona->regmap);
		if (ret != 0) {
			dev_err(dev, "Failed to sync device: %d\n", ret);
			goto err_reset;
		}
	}

	/* Ensure device startup is complete */
	switch (arizona->type) {
	case WM5102:
		ret = regmap_read(arizona->regmap, 0x19, &val);
		if (ret != 0)
			dev_err(dev,
				"Failed to check write sequencer state: %d\n",
				ret);
		else if (val & 0x01)
			break;
		/* Fall through */
	default:
		ret = arizona_wait_for_boot(arizona);
		if (ret != 0) {
			dev_err(arizona->dev,
				"Device failed initial boot: %d\n", ret);
			goto err_reset;
		}
		break;
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
#ifdef CONFIG_MFD_WM5102
	case 0x5102:
		type_name = "WM5102";
		if (arizona->type != WM5102) {
			dev_err(arizona->dev, "WM5102 registered as %d\n",
				arizona->type);
			arizona->type = WM5102;
		}
		apply_patch = wm5102_patch;
		arizona->rev &= 0x7;
		break;
#endif
#ifdef CONFIG_MFD_WM5110
	case 0x5110:
		type_name = "WM5110";
		if (arizona->type != WM5110) {
			dev_err(arizona->dev, "WM5110 registered as %d\n",
				arizona->type);
			arizona->type = WM5110;
		}
		apply_patch = wm5110_patch;
		break;
#endif
	default:
		dev_err(arizona->dev, "Unknown device ID %x\n", reg);
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
			ret = arizona_apply_hardware_patch(arizona);
			if (ret != 0) {
				dev_err(arizona->dev,
					"Failed to apply hardware patch: %d\n",
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

	/*
	 * LDO1 can only be used to supply DCVDD so if it has no
	 * consumers then DCVDD is supplied externally.
	 */
	if (arizona->pdata.ldo1 &&
	    arizona->pdata.ldo1->num_consumer_supplies == 0)
		arizona->external_dcvdd = true;

	pm_runtime_set_autosuspend_delay(arizona->dev, 100);
	pm_runtime_use_autosuspend(arizona->dev);
	pm_runtime_enable(arizona->dev);

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
				   ARIZONA_MICB1_DISCH |
				   ARIZONA_MICB1_BYPASS |
				   ARIZONA_MICB1_RATE, val);
	}

	for (i = 0; i < ARIZONA_MAX_INPUT; i++) {
		/* Default for both is 0 so noop with defaults */
		val = arizona->pdata.dmic_ref[i]
			<< ARIZONA_IN1_DMIC_SUP_SHIFT;
		val |= arizona->pdata.inmode[i] << ARIZONA_IN1_MODE_SHIFT;

		regmap_update_bits(arizona->regmap,
				   ARIZONA_IN1L_CONTROL + (i * 8),
				   ARIZONA_IN1_DMIC_SUP_MASK |
				   ARIZONA_IN1_MODE_MASK, val);
	}

	for (i = 0; i < ARIZONA_MAX_OUTPUT; i++) {
		/* Default is 0 so noop with defaults */
		if (arizona->pdata.out_mono[i])
			val = ARIZONA_OUT1_MONO;
		else
			val = 0;

		regmap_update_bits(arizona->regmap,
				   ARIZONA_OUTPUT_PATH_CONFIG_1L + (i * 8),
				   ARIZONA_OUT1_MONO, val);
	}

	for (i = 0; i < ARIZONA_MAX_PDM_SPK; i++) {
		if (arizona->pdata.spk_mute[i])
			regmap_update_bits(arizona->regmap,
					   ARIZONA_PDM_SPK1_CTRL_1 + (i * 2),
					   ARIZONA_SPK1_MUTE_ENDIAN_MASK |
					   ARIZONA_SPK1_MUTE_SEQ1_MASK,
					   arizona->pdata.spk_mute[i]);

		if (arizona->pdata.spk_fmt[i])
			regmap_update_bits(arizona->regmap,
					   ARIZONA_PDM_SPK1_CTRL_2 + (i * 2),
					   ARIZONA_SPK1_FMT_MASK,
					   arizona->pdata.spk_fmt[i]);
	}

	/* Set up for interrupts */
	ret = arizona_irq_init(arizona);
	if (ret != 0)
		goto err_reset;

	arizona_request_irq(arizona, ARIZONA_IRQ_CLKGEN_ERR, "CLKGEN error",
			    arizona_clkgen_err, arizona);
	arizona_request_irq(arizona, ARIZONA_IRQ_OVERCLOCKED, "Overclocked",
			    arizona_overclocked, arizona);
	arizona_request_irq(arizona, ARIZONA_IRQ_UNDERCLOCKED, "Underclocked",
			    arizona_underclocked, arizona);

	switch (arizona->type) {
	case WM5102:
		ret = mfd_add_devices(arizona->dev, -1, wm5102_devs,
				      ARRAY_SIZE(wm5102_devs), NULL, 0, NULL);
		break;
	case WM5110:
		ret = mfd_add_devices(arizona->dev, -1, wm5110_devs,
				      ARRAY_SIZE(wm5110_devs), NULL, 0, NULL);
		break;
	}

	if (ret != 0) {
		dev_err(arizona->dev, "Failed to add subdevices: %d\n", ret);
		goto err_irq;
	}

#ifdef CONFIG_PM_RUNTIME
	regulator_disable(arizona->dcvdd);
#endif

	return 0;

err_irq:
	arizona_irq_exit(arizona);
err_reset:
	if (arizona->pdata.reset) {
		gpio_set_value_cansleep(arizona->pdata.reset, 0);
		gpio_free(arizona->pdata.reset);
	}
	regulator_disable(arizona->dcvdd);
err_enable:
	regulator_bulk_disable(arizona->num_core_supplies,
			       arizona->core_supplies);
err_early:
	mfd_remove_devices(dev);
	return ret;
}
EXPORT_SYMBOL_GPL(arizona_dev_init);

int arizona_dev_exit(struct arizona *arizona)
{
	mfd_remove_devices(arizona->dev);
	arizona_free_irq(arizona, ARIZONA_IRQ_UNDERCLOCKED, arizona);
	arizona_free_irq(arizona, ARIZONA_IRQ_OVERCLOCKED, arizona);
	arizona_free_irq(arizona, ARIZONA_IRQ_CLKGEN_ERR, arizona);
	pm_runtime_disable(arizona->dev);
	arizona_irq_exit(arizona);
	if (arizona->pdata.reset)
		gpio_set_value_cansleep(arizona->pdata.reset, 0);
	regulator_disable(arizona->dcvdd);
	regulator_bulk_disable(ARRAY_SIZE(arizona->core_supplies),
			       arizona->core_supplies);
	return 0;
}
EXPORT_SYMBOL_GPL(arizona_dev_exit);
