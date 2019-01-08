/*
 * Rockchip emmc PHY driver
 *
 * Copyright (C) 2016 Shawn Lin <shawn.lin@rock-chips.com>
 * Copyright (C) 2016 ROCKCHIP, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

/*
 * The higher 16-bit of this register is used for write protection
 * only if BIT(x + 16) set to 1 the BIT(x) can be written.
 */
#define HIWORD_UPDATE(val, mask, shift) \
		((val) << (shift) | (mask) << ((shift) + 16))

/* Register definition */
#define GRF_EMMCPHY_CON0		0x0
#define GRF_EMMCPHY_CON1		0x4
#define GRF_EMMCPHY_CON2		0x8
#define GRF_EMMCPHY_CON3		0xc
#define GRF_EMMCPHY_CON4		0x10
#define GRF_EMMCPHY_CON5		0x14
#define GRF_EMMCPHY_CON6		0x18
#define GRF_EMMCPHY_STATUS		0x20

#define PHYCTRL_PDB_MASK		0x1
#define PHYCTRL_PDB_SHIFT		0x0
#define PHYCTRL_PDB_PWR_ON		0x1
#define PHYCTRL_PDB_PWR_OFF		0x0
#define PHYCTRL_ENDLL_MASK		0x1
#define PHYCTRL_ENDLL_SHIFT		0x1
#define PHYCTRL_ENDLL_ENABLE		0x1
#define PHYCTRL_ENDLL_DISABLE		0x0
#define PHYCTRL_CALDONE_MASK		0x1
#define PHYCTRL_CALDONE_SHIFT		0x6
#define PHYCTRL_CALDONE_DONE		0x1
#define PHYCTRL_CALDONE_GOING		0x0
#define PHYCTRL_DLLRDY_MASK		0x1
#define PHYCTRL_DLLRDY_SHIFT		0x5
#define PHYCTRL_DLLRDY_DONE		0x1
#define PHYCTRL_DLLRDY_GOING		0x0
#define PHYCTRL_FREQSEL_200M		0x0
#define PHYCTRL_FREQSEL_50M		0x1
#define PHYCTRL_FREQSEL_100M		0x2
#define PHYCTRL_FREQSEL_150M		0x3
#define PHYCTRL_FREQSEL_MASK		0x3
#define PHYCTRL_FREQSEL_SHIFT		0xc
#define PHYCTRL_DR_MASK			0x7
#define PHYCTRL_DR_SHIFT		0x4
#define PHYCTRL_DR_50OHM		0x0
#define PHYCTRL_DR_33OHM		0x1
#define PHYCTRL_DR_66OHM		0x2
#define PHYCTRL_DR_100OHM		0x3
#define PHYCTRL_DR_40OHM		0x4
#define PHYCTRL_OTAPDLYENA		0x1
#define PHYCTRL_OTAPDLYENA_MASK		0x1
#define PHYCTRL_OTAPDLYENA_SHIFT	0xb
#define PHYCTRL_OTAPDLYSEL_MASK		0xf
#define PHYCTRL_OTAPDLYSEL_SHIFT	0x7
#define PHYCTRL_REN_STRB_ENABLE		0x1
#define PHYCTRL_REN_STRB_MASK		0x1
#define PHYCTRL_REN_STRB_SHIFT		0x9

#define PHYCTRL_IS_CALDONE(x) \
	((((x) >> PHYCTRL_CALDONE_SHIFT) & \
	  PHYCTRL_CALDONE_MASK) == PHYCTRL_CALDONE_DONE)
#define PHYCTRL_IS_DLLRDY(x) \
	((((x) >> PHYCTRL_DLLRDY_SHIFT) & \
	  PHYCTRL_DLLRDY_MASK) == PHYCTRL_DLLRDY_DONE)

struct rockchip_emmc_phy {
	unsigned int	reg_offset;
	struct regmap	*reg_base;
	struct clk	*emmcclk;
};

static int rockchip_emmc_phy_power(struct phy *phy, bool on_off)
{
	struct rockchip_emmc_phy *rk_phy = phy_get_drvdata(phy);
	unsigned int caldone;
	unsigned int dllrdy;
	unsigned int freqsel = PHYCTRL_FREQSEL_200M;
	unsigned long rate;

	/*
	 * Keep phyctrl_pdb and phyctrl_endll low to allow
	 * initialization of CALIO state M/C DFFs
	 */
	regmap_write(rk_phy->reg_base,
		     rk_phy->reg_offset + GRF_EMMCPHY_CON6,
		     HIWORD_UPDATE(PHYCTRL_PDB_PWR_OFF,
				   PHYCTRL_PDB_MASK,
				   PHYCTRL_PDB_SHIFT));
	regmap_write(rk_phy->reg_base,
		     rk_phy->reg_offset + GRF_EMMCPHY_CON6,
		     HIWORD_UPDATE(PHYCTRL_ENDLL_DISABLE,
				   PHYCTRL_ENDLL_MASK,
				   PHYCTRL_ENDLL_SHIFT));

	/* Already finish power_off above */
	if (on_off == PHYCTRL_PDB_PWR_OFF)
		return 0;

	rate = clk_get_rate(rk_phy->emmcclk);

	if (rate != 0) {
		unsigned long ideal_rate;
		unsigned long diff;

		switch (rate) {
		case 1 ... 74999999:
			ideal_rate = 50000000;
			freqsel = PHYCTRL_FREQSEL_50M;
			break;
		case 75000000 ... 124999999:
			ideal_rate = 100000000;
			freqsel = PHYCTRL_FREQSEL_100M;
			break;
		case 125000000 ... 174999999:
			ideal_rate = 150000000;
			freqsel = PHYCTRL_FREQSEL_150M;
			break;
		default:
			ideal_rate = 200000000;
			break;
		};

		diff = (rate > ideal_rate) ?
			rate - ideal_rate : ideal_rate - rate;

		/*
		 * In order for tuning delays to be accurate we need to be
		 * pretty spot on for the DLL range, so warn if we're too
		 * far off.  Also warn if we're above the 200 MHz max.  Don't
		 * warn for really slow rates since we won't be tuning then.
		 */
		if ((rate > 50000000 && diff > 15000000) || (rate > 200000000))
			dev_warn(&phy->dev, "Unsupported rate: %lu\n", rate);
	}

	/*
	 * According to the user manual, calpad calibration
	 * cycle takes more than 2us without the minimal recommended
	 * value, so we may need a little margin here
	 */
	udelay(3);
	regmap_write(rk_phy->reg_base,
		     rk_phy->reg_offset + GRF_EMMCPHY_CON6,
		     HIWORD_UPDATE(PHYCTRL_PDB_PWR_ON,
				   PHYCTRL_PDB_MASK,
				   PHYCTRL_PDB_SHIFT));

	/*
	 * According to the user manual, it asks driver to wait 5us for
	 * calpad busy trimming. However it is documented that this value is
	 * PVT(A.K.A process,voltage and temperature) relevant, so some
	 * failure cases are found which indicates we should be more tolerant
	 * to calpad busy trimming.
	 */
	if (regmap_read_poll_timeout(rk_phy->reg_base,
				     rk_phy->reg_offset + GRF_EMMCPHY_STATUS,
				     caldone, PHYCTRL_IS_CALDONE(caldone),
				     5, 500)) {
		pr_err("rockchip_emmc_phy_power: caldone timeout.\n");
		return -ETIMEDOUT;
	}

	/* Set the frequency of the DLL operation */
	regmap_write(rk_phy->reg_base,
		     rk_phy->reg_offset + GRF_EMMCPHY_CON0,
		     HIWORD_UPDATE(freqsel, PHYCTRL_FREQSEL_MASK,
				   PHYCTRL_FREQSEL_SHIFT));

	/* Turn on the DLL */
	regmap_write(rk_phy->reg_base,
		     rk_phy->reg_offset + GRF_EMMCPHY_CON6,
		     HIWORD_UPDATE(PHYCTRL_ENDLL_ENABLE,
				   PHYCTRL_ENDLL_MASK,
				   PHYCTRL_ENDLL_SHIFT));

	/*
	 * We turned on the DLL even though the rate was 0 because we the
	 * clock might be turned on later.  ...but we can't wait for the DLL
	 * to lock when the rate is 0 because it will never lock with no
	 * input clock.
	 *
	 * Technically we should be checking the lock later when the clock
	 * is turned on, but for now we won't.
	 */
	if (rate == 0)
		return 0;

	/*
	 * After enabling analog DLL circuits docs say that we need 10.2 us if
	 * our source clock is at 50 MHz and that lock time scales linearly
	 * with clock speed.  If we are powering on the PHY and the card clock
	 * is super slow (like 100 kHZ) this could take as long as 5.1 ms as
	 * per the math: 10.2 us * (50000000 Hz / 100000 Hz) => 5.1 ms
	 * Hopefully we won't be running at 100 kHz, but we should still make
	 * sure we wait long enough.
	 *
	 * NOTE: There appear to be corner cases where the DLL seems to take
	 * extra long to lock for reasons that aren't understood.  In some
	 * extreme cases we've seen it take up to over 10ms (!).  We'll be
	 * generous and give it 50ms.  We still busy wait here because:
	 * - In most cases it should be super fast.
	 * - This is not called lots during normal operation so it shouldn't
	 *   be a power or performance problem to busy wait.  We expect it
	 *   only at boot / resume.  In both cases, eMMC is probably on the
	 *   critical path so busy waiting a little extra time should be OK.
	 */
	if (regmap_read_poll_timeout(rk_phy->reg_base,
				     rk_phy->reg_offset + GRF_EMMCPHY_STATUS,
				     dllrdy, PHYCTRL_IS_DLLRDY(dllrdy),
				     1, 50 * USEC_PER_MSEC)) {
		pr_err("rockchip_emmc_phy_power: dllrdy timeout.\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static int rockchip_emmc_phy_init(struct phy *phy)
{
	struct rockchip_emmc_phy *rk_phy = phy_get_drvdata(phy);
	int ret = 0;

	/*
	 * We purposely get the clock here and not in probe to avoid the
	 * circular dependency problem.  We expect:
	 * - PHY driver to probe
	 * - SDHCI driver to start probe
	 * - SDHCI driver to register it's clock
	 * - SDHCI driver to get the PHY
	 * - SDHCI driver to init the PHY
	 *
	 * The clock is optional, so upon any error we just set to NULL.
	 *
	 * NOTE: we don't do anything special for EPROBE_DEFER here.  Given the
	 * above expected use case, EPROBE_DEFER isn't sensible to expect, so
	 * it's just like any other error.
	 */
	rk_phy->emmcclk = clk_get(&phy->dev, "emmcclk");
	if (IS_ERR(rk_phy->emmcclk)) {
		dev_dbg(&phy->dev, "Error getting emmcclk: %d\n", ret);
		rk_phy->emmcclk = NULL;
	}

	return ret;
}

static int rockchip_emmc_phy_exit(struct phy *phy)
{
	struct rockchip_emmc_phy *rk_phy = phy_get_drvdata(phy);

	clk_put(rk_phy->emmcclk);

	return 0;
}

static int rockchip_emmc_phy_power_off(struct phy *phy)
{
	/* Power down emmc phy analog blocks */
	return rockchip_emmc_phy_power(phy, PHYCTRL_PDB_PWR_OFF);
}

static int rockchip_emmc_phy_power_on(struct phy *phy)
{
	struct rockchip_emmc_phy *rk_phy = phy_get_drvdata(phy);

	/* Drive impedance: 50 Ohm */
	regmap_write(rk_phy->reg_base,
		     rk_phy->reg_offset + GRF_EMMCPHY_CON6,
		     HIWORD_UPDATE(PHYCTRL_DR_50OHM,
				   PHYCTRL_DR_MASK,
				   PHYCTRL_DR_SHIFT));

	/* Output tap delay: enable */
	regmap_write(rk_phy->reg_base,
		     rk_phy->reg_offset + GRF_EMMCPHY_CON0,
		     HIWORD_UPDATE(PHYCTRL_OTAPDLYENA,
				   PHYCTRL_OTAPDLYENA_MASK,
				   PHYCTRL_OTAPDLYENA_SHIFT));

	/* Output tap delay */
	regmap_write(rk_phy->reg_base,
		     rk_phy->reg_offset + GRF_EMMCPHY_CON0,
		     HIWORD_UPDATE(4,
				   PHYCTRL_OTAPDLYSEL_MASK,
				   PHYCTRL_OTAPDLYSEL_SHIFT));

	/* Internal pull-down for strobe line: enable */
	regmap_write(rk_phy->reg_base,
		     rk_phy->reg_offset + GRF_EMMCPHY_CON2,
		     HIWORD_UPDATE(PHYCTRL_REN_STRB_ENABLE,
				   PHYCTRL_REN_STRB_MASK,
				   PHYCTRL_REN_STRB_SHIFT));

	/* Power up emmc phy analog blocks */
	return rockchip_emmc_phy_power(phy, PHYCTRL_PDB_PWR_ON);
}

static const struct phy_ops ops = {
	.init		= rockchip_emmc_phy_init,
	.exit		= rockchip_emmc_phy_exit,
	.power_on	= rockchip_emmc_phy_power_on,
	.power_off	= rockchip_emmc_phy_power_off,
	.owner		= THIS_MODULE,
};

static int rockchip_emmc_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rockchip_emmc_phy *rk_phy;
	struct phy *generic_phy;
	struct phy_provider *phy_provider;
	struct regmap *grf;
	unsigned int reg_offset;

	if (!dev->parent || !dev->parent->of_node)
		return -ENODEV;

	grf = syscon_node_to_regmap(dev->parent->of_node);
	if (IS_ERR(grf)) {
		dev_err(dev, "Missing rockchip,grf property\n");
		return PTR_ERR(grf);
	}

	rk_phy = devm_kzalloc(dev, sizeof(*rk_phy), GFP_KERNEL);
	if (!rk_phy)
		return -ENOMEM;

	if (of_property_read_u32(dev->of_node, "reg", &reg_offset)) {
		dev_err(dev, "missing reg property in node %s\n",
			dev->of_node->name);
		return -EINVAL;
	}

	rk_phy->reg_offset = reg_offset;
	rk_phy->reg_base = grf;

	generic_phy = devm_phy_create(dev, dev->of_node, &ops);
	if (IS_ERR(generic_phy)) {
		dev_err(dev, "failed to create PHY\n");
		return PTR_ERR(generic_phy);
	}

	phy_set_drvdata(generic_phy, rk_phy);
	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);

	return PTR_ERR_OR_ZERO(phy_provider);
}

static const struct of_device_id rockchip_emmc_phy_dt_ids[] = {
	{ .compatible = "rockchip,rk3399-emmc-phy" },
	{}
};

MODULE_DEVICE_TABLE(of, rockchip_emmc_phy_dt_ids);

static struct platform_driver rockchip_emmc_driver = {
	.probe		= rockchip_emmc_phy_probe,
	.driver		= {
		.name	= "rockchip-emmc-phy",
		.of_match_table = rockchip_emmc_phy_dt_ids,
	},
};

module_platform_driver(rockchip_emmc_driver);

MODULE_AUTHOR("Shawn Lin <shawn.lin@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip EMMC PHY driver");
MODULE_LICENSE("GPL v2");
