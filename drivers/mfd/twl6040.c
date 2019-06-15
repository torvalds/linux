/*
 * MFD driver for TWL6040 audio device
 *
 * Authors:	Misael Lopez Cruz <misael.lopez@ti.com>
 *		Jorge Eduardo Candelaria <jorge.candelaria@ti.com>
 *		Peter Ujfalusi <peter.ujfalusi@ti.com>
 *
 * Copyright:	(C) 2011 Texas Instruments, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/mfd/core.h>
#include <linux/mfd/twl6040.h>
#include <linux/regulator/consumer.h>

#define VIBRACTRL_MEMBER(reg) ((reg == TWL6040_REG_VIBCTLL) ? 0 : 1)
#define TWL6040_NUM_SUPPLIES	(2)

static const struct reg_default twl6040_defaults[] = {
	{ 0x01, 0x4B }, /* REG_ASICID	(ro) */
	{ 0x02, 0x00 }, /* REG_ASICREV	(ro) */
	{ 0x03, 0x00 }, /* REG_INTID	*/
	{ 0x04, 0x00 }, /* REG_INTMR	*/
	{ 0x05, 0x00 }, /* REG_NCPCTRL	*/
	{ 0x06, 0x00 }, /* REG_LDOCTL	*/
	{ 0x07, 0x60 }, /* REG_HPPLLCTL	*/
	{ 0x08, 0x00 }, /* REG_LPPLLCTL	*/
	{ 0x09, 0x4A }, /* REG_LPPLLDIV	*/
	{ 0x0A, 0x00 }, /* REG_AMICBCTL	*/
	{ 0x0B, 0x00 }, /* REG_DMICBCTL	*/
	{ 0x0C, 0x00 }, /* REG_MICLCTL	*/
	{ 0x0D, 0x00 }, /* REG_MICRCTL	*/
	{ 0x0E, 0x00 }, /* REG_MICGAIN	*/
	{ 0x0F, 0x1B }, /* REG_LINEGAIN	*/
	{ 0x10, 0x00 }, /* REG_HSLCTL	*/
	{ 0x11, 0x00 }, /* REG_HSRCTL	*/
	{ 0x12, 0x00 }, /* REG_HSGAIN	*/
	{ 0x13, 0x00 }, /* REG_EARCTL	*/
	{ 0x14, 0x00 }, /* REG_HFLCTL	*/
	{ 0x15, 0x00 }, /* REG_HFLGAIN	*/
	{ 0x16, 0x00 }, /* REG_HFRCTL	*/
	{ 0x17, 0x00 }, /* REG_HFRGAIN	*/
	{ 0x18, 0x00 }, /* REG_VIBCTLL	*/
	{ 0x19, 0x00 }, /* REG_VIBDATL	*/
	{ 0x1A, 0x00 }, /* REG_VIBCTLR	*/
	{ 0x1B, 0x00 }, /* REG_VIBDATR	*/
	{ 0x1C, 0x00 }, /* REG_HKCTL1	*/
	{ 0x1D, 0x00 }, /* REG_HKCTL2	*/
	{ 0x1E, 0x00 }, /* REG_GPOCTL	*/
	{ 0x1F, 0x00 }, /* REG_ALB	*/
	{ 0x20, 0x00 }, /* REG_DLB	*/
	/* 0x28, REG_TRIM1 */
	/* 0x29, REG_TRIM2 */
	/* 0x2A, REG_TRIM3 */
	/* 0x2B, REG_HSOTRIM */
	/* 0x2C, REG_HFOTRIM */
	{ 0x2D, 0x08 }, /* REG_ACCCTL	*/
	{ 0x2E, 0x00 }, /* REG_STATUS	(ro) */
};

static struct reg_sequence twl6040_patch[] = {
	/*
	 * Select I2C bus access to dual access registers
	 * Interrupt register is cleared on read
	 * Select fast mode for i2c (400KHz)
	 */
	{ TWL6040_REG_ACCCTL,
		TWL6040_I2CSEL | TWL6040_INTCLRMODE | TWL6040_I2CMODE(1) },
};


static bool twl6040_has_vibra(struct device_node *parent)
{
	struct device_node *node;

	node = of_get_child_by_name(parent, "vibra");
	if (node) {
		of_node_put(node);
		return true;
	}

	return false;
}

int twl6040_reg_read(struct twl6040 *twl6040, unsigned int reg)
{
	int ret;
	unsigned int val;

	ret = regmap_read(twl6040->regmap, reg, &val);
	if (ret < 0)
		return ret;

	return val;
}
EXPORT_SYMBOL(twl6040_reg_read);

int twl6040_reg_write(struct twl6040 *twl6040, unsigned int reg, u8 val)
{
	int ret;

	ret = regmap_write(twl6040->regmap, reg, val);

	return ret;
}
EXPORT_SYMBOL(twl6040_reg_write);

int twl6040_set_bits(struct twl6040 *twl6040, unsigned int reg, u8 mask)
{
	return regmap_update_bits(twl6040->regmap, reg, mask, mask);
}
EXPORT_SYMBOL(twl6040_set_bits);

int twl6040_clear_bits(struct twl6040 *twl6040, unsigned int reg, u8 mask)
{
	return regmap_update_bits(twl6040->regmap, reg, mask, 0);
}
EXPORT_SYMBOL(twl6040_clear_bits);

/* twl6040 codec manual power-up sequence */
static int twl6040_power_up_manual(struct twl6040 *twl6040)
{
	u8 ldoctl, ncpctl, lppllctl;
	int ret;

	/* enable high-side LDO, reference system and internal oscillator */
	ldoctl = TWL6040_HSLDOENA | TWL6040_REFENA | TWL6040_OSCENA;
	ret = twl6040_reg_write(twl6040, TWL6040_REG_LDOCTL, ldoctl);
	if (ret)
		return ret;
	usleep_range(10000, 10500);

	/* enable negative charge pump */
	ncpctl = TWL6040_NCPENA;
	ret = twl6040_reg_write(twl6040, TWL6040_REG_NCPCTL, ncpctl);
	if (ret)
		goto ncp_err;
	usleep_range(1000, 1500);

	/* enable low-side LDO */
	ldoctl |= TWL6040_LSLDOENA;
	ret = twl6040_reg_write(twl6040, TWL6040_REG_LDOCTL, ldoctl);
	if (ret)
		goto lsldo_err;
	usleep_range(1000, 1500);

	/* enable low-power PLL */
	lppllctl = TWL6040_LPLLENA;
	ret = twl6040_reg_write(twl6040, TWL6040_REG_LPPLLCTL, lppllctl);
	if (ret)
		goto lppll_err;
	usleep_range(5000, 5500);

	/* disable internal oscillator */
	ldoctl &= ~TWL6040_OSCENA;
	ret = twl6040_reg_write(twl6040, TWL6040_REG_LDOCTL, ldoctl);
	if (ret)
		goto osc_err;

	return 0;

osc_err:
	lppllctl &= ~TWL6040_LPLLENA;
	twl6040_reg_write(twl6040, TWL6040_REG_LPPLLCTL, lppllctl);
lppll_err:
	ldoctl &= ~TWL6040_LSLDOENA;
	twl6040_reg_write(twl6040, TWL6040_REG_LDOCTL, ldoctl);
lsldo_err:
	ncpctl &= ~TWL6040_NCPENA;
	twl6040_reg_write(twl6040, TWL6040_REG_NCPCTL, ncpctl);
ncp_err:
	ldoctl &= ~(TWL6040_HSLDOENA | TWL6040_REFENA | TWL6040_OSCENA);
	twl6040_reg_write(twl6040, TWL6040_REG_LDOCTL, ldoctl);

	dev_err(twl6040->dev, "manual power-up failed\n");
	return ret;
}

/* twl6040 manual power-down sequence */
static void twl6040_power_down_manual(struct twl6040 *twl6040)
{
	u8 ncpctl, ldoctl, lppllctl;

	ncpctl = twl6040_reg_read(twl6040, TWL6040_REG_NCPCTL);
	ldoctl = twl6040_reg_read(twl6040, TWL6040_REG_LDOCTL);
	lppllctl = twl6040_reg_read(twl6040, TWL6040_REG_LPPLLCTL);

	/* enable internal oscillator */
	ldoctl |= TWL6040_OSCENA;
	twl6040_reg_write(twl6040, TWL6040_REG_LDOCTL, ldoctl);
	usleep_range(1000, 1500);

	/* disable low-power PLL */
	lppllctl &= ~TWL6040_LPLLENA;
	twl6040_reg_write(twl6040, TWL6040_REG_LPPLLCTL, lppllctl);

	/* disable low-side LDO */
	ldoctl &= ~TWL6040_LSLDOENA;
	twl6040_reg_write(twl6040, TWL6040_REG_LDOCTL, ldoctl);

	/* disable negative charge pump */
	ncpctl &= ~TWL6040_NCPENA;
	twl6040_reg_write(twl6040, TWL6040_REG_NCPCTL, ncpctl);

	/* disable high-side LDO, reference system and internal oscillator */
	ldoctl &= ~(TWL6040_HSLDOENA | TWL6040_REFENA | TWL6040_OSCENA);
	twl6040_reg_write(twl6040, TWL6040_REG_LDOCTL, ldoctl);
}

static irqreturn_t twl6040_readyint_handler(int irq, void *data)
{
	struct twl6040 *twl6040 = data;

	complete(&twl6040->ready);

	return IRQ_HANDLED;
}

static irqreturn_t twl6040_thint_handler(int irq, void *data)
{
	struct twl6040 *twl6040 = data;
	u8 status;

	status = twl6040_reg_read(twl6040, TWL6040_REG_STATUS);
	if (status & TWL6040_TSHUTDET) {
		dev_warn(twl6040->dev, "Thermal shutdown, powering-off");
		twl6040_power(twl6040, 0);
	} else {
		dev_warn(twl6040->dev, "Leaving thermal shutdown, powering-on");
		twl6040_power(twl6040, 1);
	}

	return IRQ_HANDLED;
}

static int twl6040_power_up_automatic(struct twl6040 *twl6040)
{
	int time_left;

	gpio_set_value(twl6040->audpwron, 1);

	time_left = wait_for_completion_timeout(&twl6040->ready,
						msecs_to_jiffies(144));
	if (!time_left) {
		u8 intid;

		dev_warn(twl6040->dev, "timeout waiting for READYINT\n");
		intid = twl6040_reg_read(twl6040, TWL6040_REG_INTID);
		if (!(intid & TWL6040_READYINT)) {
			dev_err(twl6040->dev, "automatic power-up failed\n");
			gpio_set_value(twl6040->audpwron, 0);
			return -ETIMEDOUT;
		}
	}

	return 0;
}

int twl6040_power(struct twl6040 *twl6040, int on)
{
	int ret = 0;

	mutex_lock(&twl6040->mutex);

	if (on) {
		/* already powered-up */
		if (twl6040->power_count++)
			goto out;

		ret = clk_prepare_enable(twl6040->clk32k);
		if (ret) {
			twl6040->power_count = 0;
			goto out;
		}

		/* Allow writes to the chip */
		regcache_cache_only(twl6040->regmap, false);

		if (gpio_is_valid(twl6040->audpwron)) {
			/* use automatic power-up sequence */
			ret = twl6040_power_up_automatic(twl6040);
			if (ret) {
				clk_disable_unprepare(twl6040->clk32k);
				twl6040->power_count = 0;
				goto out;
			}
		} else {
			/* use manual power-up sequence */
			ret = twl6040_power_up_manual(twl6040);
			if (ret) {
				clk_disable_unprepare(twl6040->clk32k);
				twl6040->power_count = 0;
				goto out;
			}
		}

		/*
		 * Register access can produce errors after power-up unless we
		 * wait at least 8ms based on measurements on duovero.
		 */
		usleep_range(10000, 12000);

		/* Sync with the HW */
		ret = regcache_sync(twl6040->regmap);
		if (ret) {
			dev_err(twl6040->dev, "Failed to sync with the HW: %i\n",
				ret);
			goto out;
		}

		/* Default PLL configuration after power up */
		twl6040->pll = TWL6040_SYSCLK_SEL_LPPLL;
		twl6040->sysclk_rate = 19200000;
	} else {
		/* already powered-down */
		if (!twl6040->power_count) {
			dev_err(twl6040->dev,
				"device is already powered-off\n");
			ret = -EPERM;
			goto out;
		}

		if (--twl6040->power_count)
			goto out;

		if (gpio_is_valid(twl6040->audpwron)) {
			/* use AUDPWRON line */
			gpio_set_value(twl6040->audpwron, 0);

			/* power-down sequence latency */
			usleep_range(500, 700);
		} else {
			/* use manual power-down sequence */
			twl6040_power_down_manual(twl6040);
		}

		/* Set regmap to cache only and mark it as dirty */
		regcache_cache_only(twl6040->regmap, true);
		regcache_mark_dirty(twl6040->regmap);

		twl6040->sysclk_rate = 0;

		if (twl6040->pll == TWL6040_SYSCLK_SEL_HPPLL) {
			clk_disable_unprepare(twl6040->mclk);
			twl6040->mclk_rate = 0;
		}

		clk_disable_unprepare(twl6040->clk32k);
	}

out:
	mutex_unlock(&twl6040->mutex);
	return ret;
}
EXPORT_SYMBOL(twl6040_power);

int twl6040_set_pll(struct twl6040 *twl6040, int pll_id,
		    unsigned int freq_in, unsigned int freq_out)
{
	u8 hppllctl, lppllctl;
	int ret = 0;

	mutex_lock(&twl6040->mutex);

	hppllctl = twl6040_reg_read(twl6040, TWL6040_REG_HPPLLCTL);
	lppllctl = twl6040_reg_read(twl6040, TWL6040_REG_LPPLLCTL);

	/* Force full reconfiguration when switching between PLL */
	if (pll_id != twl6040->pll) {
		twl6040->sysclk_rate = 0;
		twl6040->mclk_rate = 0;
	}

	switch (pll_id) {
	case TWL6040_SYSCLK_SEL_LPPLL:
		/* low-power PLL divider */
		/* Change the sysclk configuration only if it has been canged */
		if (twl6040->sysclk_rate != freq_out) {
			switch (freq_out) {
			case 17640000:
				lppllctl |= TWL6040_LPLLFIN;
				break;
			case 19200000:
				lppllctl &= ~TWL6040_LPLLFIN;
				break;
			default:
				dev_err(twl6040->dev,
					"freq_out %d not supported\n",
					freq_out);
				ret = -EINVAL;
				goto pll_out;
			}
			twl6040_reg_write(twl6040, TWL6040_REG_LPPLLCTL,
					  lppllctl);
		}

		/* The PLL in use has not been change, we can exit */
		if (twl6040->pll == pll_id)
			break;

		switch (freq_in) {
		case 32768:
			lppllctl |= TWL6040_LPLLENA;
			twl6040_reg_write(twl6040, TWL6040_REG_LPPLLCTL,
					  lppllctl);
			mdelay(5);
			lppllctl &= ~TWL6040_HPLLSEL;
			twl6040_reg_write(twl6040, TWL6040_REG_LPPLLCTL,
					  lppllctl);
			hppllctl &= ~TWL6040_HPLLENA;
			twl6040_reg_write(twl6040, TWL6040_REG_HPPLLCTL,
					  hppllctl);
			break;
		default:
			dev_err(twl6040->dev,
				"freq_in %d not supported\n", freq_in);
			ret = -EINVAL;
			goto pll_out;
		}

		clk_disable_unprepare(twl6040->mclk);
		break;
	case TWL6040_SYSCLK_SEL_HPPLL:
		/* high-performance PLL can provide only 19.2 MHz */
		if (freq_out != 19200000) {
			dev_err(twl6040->dev,
				"freq_out %d not supported\n", freq_out);
			ret = -EINVAL;
			goto pll_out;
		}

		if (twl6040->mclk_rate != freq_in) {
			hppllctl &= ~TWL6040_MCLK_MSK;

			switch (freq_in) {
			case 12000000:
				/* PLL enabled, active mode */
				hppllctl |= TWL6040_MCLK_12000KHZ |
					    TWL6040_HPLLENA;
				break;
			case 19200000:
				/* PLL enabled, bypass mode */
				hppllctl |= TWL6040_MCLK_19200KHZ |
					    TWL6040_HPLLBP | TWL6040_HPLLENA;
				break;
			case 26000000:
				/* PLL enabled, active mode */
				hppllctl |= TWL6040_MCLK_26000KHZ |
					    TWL6040_HPLLENA;
				break;
			case 38400000:
				/* PLL enabled, bypass mode */
				hppllctl |= TWL6040_MCLK_38400KHZ |
					    TWL6040_HPLLBP | TWL6040_HPLLENA;
				break;
			default:
				dev_err(twl6040->dev,
					"freq_in %d not supported\n", freq_in);
				ret = -EINVAL;
				goto pll_out;
			}

			/* When switching to HPPLL, enable the mclk first */
			if (pll_id != twl6040->pll)
				clk_prepare_enable(twl6040->mclk);
			/*
			 * enable clock slicer to ensure input waveform is
			 * square
			 */
			hppllctl |= TWL6040_HPLLSQRENA;

			twl6040_reg_write(twl6040, TWL6040_REG_HPPLLCTL,
					  hppllctl);
			usleep_range(500, 700);
			lppllctl |= TWL6040_HPLLSEL;
			twl6040_reg_write(twl6040, TWL6040_REG_LPPLLCTL,
					  lppllctl);
			lppllctl &= ~TWL6040_LPLLENA;
			twl6040_reg_write(twl6040, TWL6040_REG_LPPLLCTL,
					  lppllctl);

			twl6040->mclk_rate = freq_in;
		}
		break;
	default:
		dev_err(twl6040->dev, "unknown pll id %d\n", pll_id);
		ret = -EINVAL;
		goto pll_out;
	}

	twl6040->sysclk_rate = freq_out;
	twl6040->pll = pll_id;

pll_out:
	mutex_unlock(&twl6040->mutex);
	return ret;
}
EXPORT_SYMBOL(twl6040_set_pll);

int twl6040_get_pll(struct twl6040 *twl6040)
{
	if (twl6040->power_count)
		return twl6040->pll;
	else
		return -ENODEV;
}
EXPORT_SYMBOL(twl6040_get_pll);

unsigned int twl6040_get_sysclk(struct twl6040 *twl6040)
{
	return twl6040->sysclk_rate;
}
EXPORT_SYMBOL(twl6040_get_sysclk);

/* Get the combined status of the vibra control register */
int twl6040_get_vibralr_status(struct twl6040 *twl6040)
{
	unsigned int reg;
	int ret;
	u8 status;

	ret = regmap_read(twl6040->regmap, TWL6040_REG_VIBCTLL, &reg);
	if (ret != 0)
		return ret;
	status = reg;

	ret = regmap_read(twl6040->regmap, TWL6040_REG_VIBCTLR, &reg);
	if (ret != 0)
		return ret;
	status |= reg;

	status &= (TWL6040_VIBENA | TWL6040_VIBSEL);

	return status;
}
EXPORT_SYMBOL(twl6040_get_vibralr_status);

static struct resource twl6040_vibra_rsrc[] = {
	{
		.flags = IORESOURCE_IRQ,
	},
};

static struct resource twl6040_codec_rsrc[] = {
	{
		.flags = IORESOURCE_IRQ,
	},
};

static bool twl6040_readable_reg(struct device *dev, unsigned int reg)
{
	/* Register 0 is not readable */
	if (!reg)
		return false;
	return true;
}

static bool twl6040_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TWL6040_REG_ASICID:
	case TWL6040_REG_ASICREV:
	case TWL6040_REG_INTID:
	case TWL6040_REG_LPPLLCTL:
	case TWL6040_REG_HPPLLCTL:
	case TWL6040_REG_STATUS:
		return true;
	default:
		return false;
	}
}

static bool twl6040_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TWL6040_REG_ASICID:
	case TWL6040_REG_ASICREV:
	case TWL6040_REG_STATUS:
		return false;
	default:
		return true;
	}
}

static const struct regmap_config twl6040_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.reg_defaults = twl6040_defaults,
	.num_reg_defaults = ARRAY_SIZE(twl6040_defaults),

	.max_register = TWL6040_REG_STATUS, /* 0x2e */

	.readable_reg = twl6040_readable_reg,
	.volatile_reg = twl6040_volatile_reg,
	.writeable_reg = twl6040_writeable_reg,

	.cache_type = REGCACHE_RBTREE,
	.use_single_rw = true,
};

static const struct regmap_irq twl6040_irqs[] = {
	{ .reg_offset = 0, .mask = TWL6040_THINT, },
	{ .reg_offset = 0, .mask = TWL6040_PLUGINT | TWL6040_UNPLUGINT, },
	{ .reg_offset = 0, .mask = TWL6040_HOOKINT, },
	{ .reg_offset = 0, .mask = TWL6040_HFINT, },
	{ .reg_offset = 0, .mask = TWL6040_VIBINT, },
	{ .reg_offset = 0, .mask = TWL6040_READYINT, },
};

static struct regmap_irq_chip twl6040_irq_chip = {
	.name = "twl6040",
	.irqs = twl6040_irqs,
	.num_irqs = ARRAY_SIZE(twl6040_irqs),

	.num_regs = 1,
	.status_base = TWL6040_REG_INTID,
	.mask_base = TWL6040_REG_INTMR,
};

static int twl6040_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device_node *node = client->dev.of_node;
	struct twl6040 *twl6040;
	struct mfd_cell *cell = NULL;
	int irq, ret, children = 0;

	if (!node) {
		dev_err(&client->dev, "of node is missing\n");
		return -EINVAL;
	}

	/* In order to operate correctly we need valid interrupt config */
	if (!client->irq) {
		dev_err(&client->dev, "Invalid IRQ configuration\n");
		return -EINVAL;
	}

	twl6040 = devm_kzalloc(&client->dev, sizeof(struct twl6040),
			       GFP_KERNEL);
	if (!twl6040)
		return -ENOMEM;

	twl6040->regmap = devm_regmap_init_i2c(client, &twl6040_regmap_config);
	if (IS_ERR(twl6040->regmap))
		return PTR_ERR(twl6040->regmap);

	i2c_set_clientdata(client, twl6040);

	twl6040->clk32k = devm_clk_get(&client->dev, "clk32k");
	if (IS_ERR(twl6040->clk32k)) {
		if (PTR_ERR(twl6040->clk32k) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		dev_dbg(&client->dev, "clk32k is not handled\n");
		twl6040->clk32k = NULL;
	}

	twl6040->mclk = devm_clk_get(&client->dev, "mclk");
	if (IS_ERR(twl6040->mclk)) {
		if (PTR_ERR(twl6040->mclk) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		dev_dbg(&client->dev, "mclk is not handled\n");
		twl6040->mclk = NULL;
	}

	twl6040->supplies[0].supply = "vio";
	twl6040->supplies[1].supply = "v2v1";
	ret = devm_regulator_bulk_get(&client->dev, TWL6040_NUM_SUPPLIES,
				      twl6040->supplies);
	if (ret != 0) {
		dev_err(&client->dev, "Failed to get supplies: %d\n", ret);
		return ret;
	}

	ret = regulator_bulk_enable(TWL6040_NUM_SUPPLIES, twl6040->supplies);
	if (ret != 0) {
		dev_err(&client->dev, "Failed to enable supplies: %d\n", ret);
		return ret;
	}

	twl6040->dev = &client->dev;
	twl6040->irq = client->irq;

	mutex_init(&twl6040->mutex);
	init_completion(&twl6040->ready);

	regmap_register_patch(twl6040->regmap, twl6040_patch,
			      ARRAY_SIZE(twl6040_patch));

	twl6040->rev = twl6040_reg_read(twl6040, TWL6040_REG_ASICREV);
	if (twl6040->rev < 0) {
		dev_err(&client->dev, "Failed to read revision register: %d\n",
			twl6040->rev);
		ret = twl6040->rev;
		goto gpio_err;
	}

	/* ERRATA: Automatic power-up is not possible in ES1.0 */
	if (twl6040_get_revid(twl6040) > TWL6040_REV_ES1_0)
		twl6040->audpwron = of_get_named_gpio(node,
						      "ti,audpwron-gpio", 0);
	else
		twl6040->audpwron = -EINVAL;

	if (gpio_is_valid(twl6040->audpwron)) {
		ret = devm_gpio_request_one(&client->dev, twl6040->audpwron,
					    GPIOF_OUT_INIT_LOW, "audpwron");
		if (ret)
			goto gpio_err;

		/* Clear any pending interrupt */
		twl6040_reg_read(twl6040, TWL6040_REG_INTID);
	}

	ret = regmap_add_irq_chip(twl6040->regmap, twl6040->irq, IRQF_ONESHOT,
				  0, &twl6040_irq_chip, &twl6040->irq_data);
	if (ret < 0)
		goto gpio_err;

	twl6040->irq_ready = regmap_irq_get_virq(twl6040->irq_data,
						 TWL6040_IRQ_READY);
	twl6040->irq_th = regmap_irq_get_virq(twl6040->irq_data,
					      TWL6040_IRQ_TH);

	ret = devm_request_threaded_irq(twl6040->dev, twl6040->irq_ready, NULL,
					twl6040_readyint_handler, IRQF_ONESHOT,
					"twl6040_irq_ready", twl6040);
	if (ret) {
		dev_err(twl6040->dev, "READY IRQ request failed: %d\n", ret);
		goto readyirq_err;
	}

	ret = devm_request_threaded_irq(twl6040->dev, twl6040->irq_th, NULL,
					twl6040_thint_handler, IRQF_ONESHOT,
					"twl6040_irq_th", twl6040);
	if (ret) {
		dev_err(twl6040->dev, "Thermal IRQ request failed: %d\n", ret);
		goto readyirq_err;
	}

	/*
	 * The main functionality of twl6040 to provide audio on OMAP4+ systems.
	 * We can add the ASoC codec child whenever this driver has been loaded.
	 */
	irq = regmap_irq_get_virq(twl6040->irq_data, TWL6040_IRQ_PLUG);
	cell = &twl6040->cells[children];
	cell->name = "twl6040-codec";
	twl6040_codec_rsrc[0].start = irq;
	twl6040_codec_rsrc[0].end = irq;
	cell->resources = twl6040_codec_rsrc;
	cell->num_resources = ARRAY_SIZE(twl6040_codec_rsrc);
	children++;

	/* Vibra input driver support */
	if (twl6040_has_vibra(node)) {
		irq = regmap_irq_get_virq(twl6040->irq_data, TWL6040_IRQ_VIB);

		cell = &twl6040->cells[children];
		cell->name = "twl6040-vibra";
		twl6040_vibra_rsrc[0].start = irq;
		twl6040_vibra_rsrc[0].end = irq;
		cell->resources = twl6040_vibra_rsrc;
		cell->num_resources = ARRAY_SIZE(twl6040_vibra_rsrc);
		children++;
	}

	/* GPO support */
	cell = &twl6040->cells[children];
	cell->name = "twl6040-gpo";
	children++;

	/* PDM clock support  */
	cell = &twl6040->cells[children];
	cell->name = "twl6040-pdmclk";
	children++;

	/* The chip is powered down so mark regmap to cache only and dirty */
	regcache_cache_only(twl6040->regmap, true);
	regcache_mark_dirty(twl6040->regmap);

	ret = mfd_add_devices(&client->dev, -1, twl6040->cells, children,
			      NULL, 0, NULL);
	if (ret)
		goto readyirq_err;

	return 0;

readyirq_err:
	regmap_del_irq_chip(twl6040->irq, twl6040->irq_data);
gpio_err:
	regulator_bulk_disable(TWL6040_NUM_SUPPLIES, twl6040->supplies);
	return ret;
}

static int twl6040_remove(struct i2c_client *client)
{
	struct twl6040 *twl6040 = i2c_get_clientdata(client);

	if (twl6040->power_count)
		twl6040_power(twl6040, 0);

	regmap_del_irq_chip(twl6040->irq, twl6040->irq_data);

	mfd_remove_devices(&client->dev);

	regulator_bulk_disable(TWL6040_NUM_SUPPLIES, twl6040->supplies);

	return 0;
}

static const struct i2c_device_id twl6040_i2c_id[] = {
	{ "twl6040", 0, },
	{ "twl6041", 0, },
	{ },
};
MODULE_DEVICE_TABLE(i2c, twl6040_i2c_id);

static struct i2c_driver twl6040_driver = {
	.driver = {
		.name = "twl6040",
	},
	.probe		= twl6040_probe,
	.remove		= twl6040_remove,
	.id_table	= twl6040_i2c_id,
};

module_i2c_driver(twl6040_driver);

MODULE_DESCRIPTION("TWL6040 MFD");
MODULE_AUTHOR("Misael Lopez Cruz <misael.lopez@ti.com>");
MODULE_AUTHOR("Jorge Eduardo Candelaria <jorge.candelaria@ti.com>");
MODULE_LICENSE("GPL");
