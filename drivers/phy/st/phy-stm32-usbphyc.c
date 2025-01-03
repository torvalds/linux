// SPDX-License-Identifier: GPL-2.0
/*
 * STMicroelectronics STM32 USB PHY Controller driver
 *
 * Copyright (C) 2018 STMicroelectronics
 * Author(s): Amelie Delaunay <amelie.delaunay@st.com>.
 */
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/units.h>

#define STM32_USBPHYC_PLL	0x0
#define STM32_USBPHYC_MISC	0x8
#define STM32_USBPHYC_MONITOR(X) (0x108 + ((X) * 0x100))
#define STM32_USBPHYC_TUNE(X)	(0x10C + ((X) * 0x100))
#define STM32_USBPHYC_VERSION	0x3F4

/* STM32_USBPHYC_PLL bit fields */
#define PLLNDIV			GENMASK(6, 0)
#define PLLFRACIN		GENMASK(25, 10)
#define PLLEN			BIT(26)
#define PLLSTRB			BIT(27)
#define PLLSTRBYP		BIT(28)
#define PLLFRACCTL		BIT(29)
#define PLLDITHEN0		BIT(30)
#define PLLDITHEN1		BIT(31)

/* STM32_USBPHYC_MISC bit fields */
#define SWITHOST		BIT(0)

/* STM32_USBPHYC_MONITOR bit fields */
#define STM32_USBPHYC_MON_OUT	GENMASK(3, 0)
#define STM32_USBPHYC_MON_SEL	GENMASK(8, 4)
#define STM32_USBPHYC_MON_SEL_LOCKP 0x1F
#define STM32_USBPHYC_MON_OUT_LOCKP BIT(3)

/* STM32_USBPHYC_TUNE bit fields */
#define INCURREN		BIT(0)
#define INCURRINT		BIT(1)
#define LFSCAPEN		BIT(2)
#define HSDRVSLEW		BIT(3)
#define HSDRVDCCUR		BIT(4)
#define HSDRVDCLEV		BIT(5)
#define HSDRVCURINCR		BIT(6)
#define FSDRVRFADJ		BIT(7)
#define HSDRVRFRED		BIT(8)
#define HSDRVCHKITRM		GENMASK(12, 9)
#define HSDRVCHKZTRM		GENMASK(14, 13)
#define OTPCOMP			GENMASK(19, 15)
#define SQLCHCTL		GENMASK(21, 20)
#define HDRXGNEQEN		BIT(22)
#define HSRXOFF			GENMASK(24, 23)
#define HSFALLPREEM		BIT(25)
#define SHTCCTCTLPROT		BIT(26)
#define STAGSEL			BIT(27)

enum boosting_vals {
	BOOST_1000_UA = 1000,
	BOOST_2000_UA = 2000,
};

enum dc_level_vals {
	DC_NOMINAL,
	DC_PLUS_5_TO_7_MV,
	DC_PLUS_10_TO_14_MV,
	DC_MINUS_5_TO_7_MV,
	DC_MAX,
};

enum current_trim {
	CUR_NOMINAL,
	CUR_PLUS_1_56_PCT,
	CUR_PLUS_3_12_PCT,
	CUR_PLUS_4_68_PCT,
	CUR_PLUS_6_24_PCT,
	CUR_PLUS_7_8_PCT,
	CUR_PLUS_9_36_PCT,
	CUR_PLUS_10_92_PCT,
	CUR_PLUS_12_48_PCT,
	CUR_PLUS_14_04_PCT,
	CUR_PLUS_15_6_PCT,
	CUR_PLUS_17_16_PCT,
	CUR_PLUS_19_01_PCT,
	CUR_PLUS_20_58_PCT,
	CUR_PLUS_22_16_PCT,
	CUR_PLUS_23_73_PCT,
	CUR_MAX,
};

enum impedance_trim {
	IMP_NOMINAL,
	IMP_MINUS_2_OHMS,
	IMP_MINUS_4_OMHS,
	IMP_MINUS_6_OHMS,
	IMP_MAX,
};

enum squelch_level {
	SQLCH_NOMINAL,
	SQLCH_PLUS_7_MV,
	SQLCH_MINUS_5_MV,
	SQLCH_PLUS_14_MV,
	SQLCH_MAX,
};

enum rx_offset {
	NO_RX_OFFSET,
	RX_OFFSET_PLUS_5_MV,
	RX_OFFSET_PLUS_10_MV,
	RX_OFFSET_MINUS_5_MV,
	RX_OFFSET_MAX,
};

/* STM32_USBPHYC_VERSION bit fields */
#define MINREV			GENMASK(3, 0)
#define MAJREV			GENMASK(7, 4)

#define PLL_FVCO_MHZ		2880
#define PLL_INFF_MIN_RATE_HZ	19200000
#define PLL_INFF_MAX_RATE_HZ	38400000

struct pll_params {
	u8 ndiv;
	u16 frac;
};

struct stm32_usbphyc_phy {
	struct phy *phy;
	struct stm32_usbphyc *usbphyc;
	struct regulator *vbus;
	u32 index;
	bool active;
	u32 tune;
};

struct stm32_usbphyc {
	struct device *dev;
	void __iomem *base;
	struct clk *clk;
	struct reset_control *rst;
	struct stm32_usbphyc_phy **phys;
	int nphys;
	struct regulator *vdda1v1;
	struct regulator *vdda1v8;
	atomic_t n_pll_cons;
	struct clk_hw clk48_hw;
	int switch_setup;
};

static inline void stm32_usbphyc_set_bits(void __iomem *reg, u32 bits)
{
	writel_relaxed(readl_relaxed(reg) | bits, reg);
}

static inline void stm32_usbphyc_clr_bits(void __iomem *reg, u32 bits)
{
	writel_relaxed(readl_relaxed(reg) & ~bits, reg);
}

static int stm32_usbphyc_regulators_enable(struct stm32_usbphyc *usbphyc)
{
	int ret;

	ret = regulator_enable(usbphyc->vdda1v1);
	if (ret)
		return ret;

	ret = regulator_enable(usbphyc->vdda1v8);
	if (ret)
		goto vdda1v1_disable;

	return 0;

vdda1v1_disable:
	regulator_disable(usbphyc->vdda1v1);

	return ret;
}

static int stm32_usbphyc_regulators_disable(struct stm32_usbphyc *usbphyc)
{
	int ret;

	ret = regulator_disable(usbphyc->vdda1v8);
	if (ret)
		return ret;

	ret = regulator_disable(usbphyc->vdda1v1);
	if (ret)
		return ret;

	return 0;
}

static void stm32_usbphyc_get_pll_params(u32 clk_rate,
					 struct pll_params *pll_params)
{
	unsigned long long fvco, ndiv, frac;

	/*    _
	 *   | FVCO = INFF*2*(NDIV + FRACT/2^16) when DITHER_DISABLE[1] = 1
	 *   | FVCO = 2880MHz
	 *  <
	 *   | NDIV = integer part of input bits to set the LDF
	 *   |_FRACT = fractional part of input bits to set the LDF
	 *  =>	PLLNDIV = integer part of (FVCO / (INFF*2))
	 *  =>	PLLFRACIN = fractional part of(FVCO / INFF*2) * 2^16
	 * <=>  PLLFRACIN = ((FVCO / (INFF*2)) - PLLNDIV) * 2^16
	 */
	fvco = (unsigned long long)PLL_FVCO_MHZ * HZ_PER_MHZ;

	ndiv = fvco;
	do_div(ndiv, (clk_rate * 2));
	pll_params->ndiv = (u8)ndiv;

	frac = fvco * (1 << 16);
	do_div(frac, (clk_rate * 2));
	frac = frac - (ndiv * (1 << 16));
	pll_params->frac = (u16)frac;
}

static int stm32_usbphyc_pll_init(struct stm32_usbphyc *usbphyc)
{
	struct pll_params pll_params;
	u32 clk_rate = clk_get_rate(usbphyc->clk);
	u32 ndiv, frac;
	u32 usbphyc_pll;

	if ((clk_rate < PLL_INFF_MIN_RATE_HZ) ||
	    (clk_rate > PLL_INFF_MAX_RATE_HZ)) {
		dev_err(usbphyc->dev, "input clk freq (%dHz) out of range\n",
			clk_rate);
		return -EINVAL;
	}

	stm32_usbphyc_get_pll_params(clk_rate, &pll_params);
	ndiv = FIELD_PREP(PLLNDIV, pll_params.ndiv);
	frac = FIELD_PREP(PLLFRACIN, pll_params.frac);

	usbphyc_pll = PLLDITHEN1 | PLLDITHEN0 | PLLSTRBYP | ndiv;

	if (pll_params.frac)
		usbphyc_pll |= PLLFRACCTL | frac;

	writel_relaxed(usbphyc_pll, usbphyc->base + STM32_USBPHYC_PLL);

	dev_dbg(usbphyc->dev, "input clk freq=%dHz, ndiv=%lu, frac=%lu\n",
		clk_rate, FIELD_GET(PLLNDIV, usbphyc_pll),
		FIELD_GET(PLLFRACIN, usbphyc_pll));

	return 0;
}

static int __stm32_usbphyc_pll_disable(struct stm32_usbphyc *usbphyc)
{
	void __iomem *pll_reg = usbphyc->base + STM32_USBPHYC_PLL;
	u32 pllen;

	stm32_usbphyc_clr_bits(pll_reg, PLLEN);

	/* Wait for minimum width of powerdown pulse (ENABLE = Low) */
	if (readl_relaxed_poll_timeout(pll_reg, pllen, !(pllen & PLLEN), 5, 50))
		dev_err(usbphyc->dev, "PLL not reset\n");

	return stm32_usbphyc_regulators_disable(usbphyc);
}

static int stm32_usbphyc_pll_disable(struct stm32_usbphyc *usbphyc)
{
	/* Check if a phy port is still active or clk48 in use */
	if (atomic_dec_return(&usbphyc->n_pll_cons) > 0)
		return 0;

	return __stm32_usbphyc_pll_disable(usbphyc);
}

static int stm32_usbphyc_pll_enable(struct stm32_usbphyc *usbphyc)
{
	void __iomem *pll_reg = usbphyc->base + STM32_USBPHYC_PLL;
	bool pllen = readl_relaxed(pll_reg) & PLLEN;
	int ret;

	/*
	 * Check if a phy port or clk48 prepare has configured the pll
	 * and ensure the PLL is enabled
	 */
	if (atomic_inc_return(&usbphyc->n_pll_cons) > 1 && pllen)
		return 0;

	if (pllen) {
		/*
		 * PLL shouldn't be enabled without known consumer,
		 * disable it and reinit n_pll_cons
		 */
		dev_warn(usbphyc->dev, "PLL enabled without known consumers\n");

		ret = __stm32_usbphyc_pll_disable(usbphyc);
		if (ret)
			goto dec_n_pll_cons;
	}

	ret = stm32_usbphyc_regulators_enable(usbphyc);
	if (ret)
		goto dec_n_pll_cons;

	ret = stm32_usbphyc_pll_init(usbphyc);
	if (ret)
		goto reg_disable;

	stm32_usbphyc_set_bits(pll_reg, PLLEN);

	/* Wait for maximum lock time */
	usleep_range(200, 300);

	return 0;

reg_disable:
	stm32_usbphyc_regulators_disable(usbphyc);

dec_n_pll_cons:
	atomic_dec(&usbphyc->n_pll_cons);

	return ret;
}

static int stm32_usbphyc_phy_init(struct phy *phy)
{
	struct stm32_usbphyc_phy *usbphyc_phy = phy_get_drvdata(phy);
	struct stm32_usbphyc *usbphyc = usbphyc_phy->usbphyc;
	u32 reg_mon = STM32_USBPHYC_MONITOR(usbphyc_phy->index);
	u32 monsel = FIELD_PREP(STM32_USBPHYC_MON_SEL,
				STM32_USBPHYC_MON_SEL_LOCKP);
	u32 monout;
	int ret;

	ret = stm32_usbphyc_pll_enable(usbphyc);
	if (ret)
		return ret;

	/* Check that PLL Lock input to PHY is High */
	writel_relaxed(monsel, usbphyc->base + reg_mon);
	ret = readl_relaxed_poll_timeout(usbphyc->base + reg_mon, monout,
					 (monout & STM32_USBPHYC_MON_OUT_LOCKP),
					 100, 1000);
	if (ret) {
		dev_err(usbphyc->dev, "PLL Lock input to PHY is Low (val=%x)\n",
			(u32)(monout & STM32_USBPHYC_MON_OUT));
		goto pll_disable;
	}

	usbphyc_phy->active = true;

	return 0;

pll_disable:
	stm32_usbphyc_pll_disable(usbphyc);

	return ret;
}

static int stm32_usbphyc_phy_exit(struct phy *phy)
{
	struct stm32_usbphyc_phy *usbphyc_phy = phy_get_drvdata(phy);
	struct stm32_usbphyc *usbphyc = usbphyc_phy->usbphyc;

	usbphyc_phy->active = false;

	return stm32_usbphyc_pll_disable(usbphyc);
}

static int stm32_usbphyc_phy_power_on(struct phy *phy)
{
	struct stm32_usbphyc_phy *usbphyc_phy = phy_get_drvdata(phy);

	if (usbphyc_phy->vbus)
		return regulator_enable(usbphyc_phy->vbus);

	return 0;
}

static int stm32_usbphyc_phy_power_off(struct phy *phy)
{
	struct stm32_usbphyc_phy *usbphyc_phy = phy_get_drvdata(phy);

	if (usbphyc_phy->vbus)
		return regulator_disable(usbphyc_phy->vbus);

	return 0;
}

static const struct phy_ops stm32_usbphyc_phy_ops = {
	.init = stm32_usbphyc_phy_init,
	.exit = stm32_usbphyc_phy_exit,
	.power_on = stm32_usbphyc_phy_power_on,
	.power_off = stm32_usbphyc_phy_power_off,
	.owner = THIS_MODULE,
};

static int stm32_usbphyc_clk48_prepare(struct clk_hw *hw)
{
	struct stm32_usbphyc *usbphyc = container_of(hw, struct stm32_usbphyc, clk48_hw);

	return stm32_usbphyc_pll_enable(usbphyc);
}

static void stm32_usbphyc_clk48_unprepare(struct clk_hw *hw)
{
	struct stm32_usbphyc *usbphyc = container_of(hw, struct stm32_usbphyc, clk48_hw);

	stm32_usbphyc_pll_disable(usbphyc);
}

static unsigned long stm32_usbphyc_clk48_recalc_rate(struct clk_hw *hw, unsigned long parent_rate)
{
	return 48000000;
}

static const struct clk_ops usbphyc_clk48_ops = {
	.prepare = stm32_usbphyc_clk48_prepare,
	.unprepare = stm32_usbphyc_clk48_unprepare,
	.recalc_rate = stm32_usbphyc_clk48_recalc_rate,
};

static void stm32_usbphyc_clk48_unregister(void *data)
{
	struct stm32_usbphyc *usbphyc = data;

	of_clk_del_provider(usbphyc->dev->of_node);
	clk_hw_unregister(&usbphyc->clk48_hw);
}

static int stm32_usbphyc_clk48_register(struct stm32_usbphyc *usbphyc)
{
	struct device_node *node = usbphyc->dev->of_node;
	struct clk_init_data init = { };
	int ret = 0;

	init.name = "ck_usbo_48m";
	init.ops = &usbphyc_clk48_ops;

	usbphyc->clk48_hw.init = &init;

	ret = clk_hw_register(usbphyc->dev, &usbphyc->clk48_hw);
	if (ret)
		return ret;

	ret = of_clk_add_hw_provider(node, of_clk_hw_simple_get, &usbphyc->clk48_hw);
	if (ret)
		clk_hw_unregister(&usbphyc->clk48_hw);

	return ret;
}

static void stm32_usbphyc_phy_tuning(struct stm32_usbphyc *usbphyc,
				     struct device_node *np, u32 index)
{
	struct stm32_usbphyc_phy *usbphyc_phy = usbphyc->phys[index];
	u32 reg = STM32_USBPHYC_TUNE(index);
	u32 otpcomp, val;
	int ret;

	/* Backup OTP compensation code */
	otpcomp = FIELD_GET(OTPCOMP, readl_relaxed(usbphyc->base + reg));

	ret = of_property_read_u32(np, "st,current-boost-microamp", &val);
	if (ret != -EINVAL) {
		if (!ret && (val == BOOST_1000_UA || val == BOOST_2000_UA)) {
			val = (val == BOOST_2000_UA) ? 1 : 0;
			usbphyc_phy->tune |= INCURREN | FIELD_PREP(INCURRINT, val);
		} else {
			dev_warn(usbphyc->dev, "phy%d: invalid st,current-boost-microamp\n", index);
		}
	}

	if (!of_property_read_bool(np, "st,no-lsfs-fb-cap"))
		usbphyc_phy->tune |= LFSCAPEN;

	if (of_property_read_bool(np, "st,decrease-hs-slew-rate"))
		usbphyc_phy->tune |= HSDRVSLEW;

	ret = of_property_read_u32(np, "st,tune-hs-dc-level", &val);
	if (ret != -EINVAL) {
		if (!ret && val < DC_MAX) {
			if (val == DC_MINUS_5_TO_7_MV) {/* Decreases HS driver DC level */
				usbphyc_phy->tune |= HSDRVDCCUR;
			} else if (val > 0) {		/* Increases HS driver DC level */
				val = (val == DC_PLUS_10_TO_14_MV) ? 1 : 0;
				usbphyc_phy->tune |= HSDRVCURINCR | FIELD_PREP(HSDRVDCLEV, val);
			}
		} else {
			dev_warn(usbphyc->dev, "phy%d: invalid st,tune-hs-dc-level\n", index);
		}
	}

	if (of_property_read_bool(np, "st,enable-fs-rftime-tuning"))
		usbphyc_phy->tune |= FSDRVRFADJ;

	if (of_property_read_bool(np, "st,enable-hs-rftime-reduction"))
		usbphyc_phy->tune |= HSDRVRFRED;

	ret = of_property_read_u32(np, "st,trim-hs-current", &val);
	if (ret != -EINVAL) {
		if (!ret && val < CUR_MAX)
			usbphyc_phy->tune |= FIELD_PREP(HSDRVCHKITRM, val);
		else
			dev_warn(usbphyc->dev, "phy%d: invalid st,trim-hs-current\n", index);
	}

	ret = of_property_read_u32(np, "st,trim-hs-impedance", &val);
	if (ret != -EINVAL) {
		if (!ret && val < IMP_MAX)
			usbphyc_phy->tune |= FIELD_PREP(HSDRVCHKZTRM, val);
		else
			dev_warn(usbphyc->dev, "phy%d: invalid st,trim-hs-impedance\n", index);
	}

	ret = of_property_read_u32(np, "st,tune-squelch-level", &val);
	if (ret != -EINVAL) {
		if (!ret && val < SQLCH_MAX)
			usbphyc_phy->tune |= FIELD_PREP(SQLCHCTL, val);
		else
			dev_warn(usbphyc->dev, "phy%d: invalid st,tune-squelch\n", index);
	}

	if (of_property_read_bool(np, "st,enable-hs-rx-gain-eq"))
		usbphyc_phy->tune |= HDRXGNEQEN;

	ret = of_property_read_u32(np, "st,tune-hs-rx-offset", &val);
	if (ret != -EINVAL) {
		if (!ret && val < RX_OFFSET_MAX)
			usbphyc_phy->tune |= FIELD_PREP(HSRXOFF, val);
		else
			dev_warn(usbphyc->dev, "phy%d: invalid st,tune-hs-rx-offset\n", index);
	}

	if (of_property_read_bool(np, "st,no-hs-ftime-ctrl"))
		usbphyc_phy->tune |= HSFALLPREEM;

	if (!of_property_read_bool(np, "st,no-lsfs-sc"))
		usbphyc_phy->tune |= SHTCCTCTLPROT;

	if (of_property_read_bool(np, "st,enable-hs-tx-staggering"))
		usbphyc_phy->tune |= STAGSEL;

	/* Restore OTP compensation code */
	usbphyc_phy->tune |= FIELD_PREP(OTPCOMP, otpcomp);

	/*
	 * By default, if no st,xxx tuning property is used, usbphyc_phy->tune is equal to
	 * STM32_USBPHYC_TUNE reset value (LFSCAPEN | SHTCCTCTLPROT | OTPCOMP).
	 */
	writel_relaxed(usbphyc_phy->tune, usbphyc->base + reg);
}

static void stm32_usbphyc_switch_setup(struct stm32_usbphyc *usbphyc,
				       u32 utmi_switch)
{
	if (!utmi_switch)
		stm32_usbphyc_clr_bits(usbphyc->base + STM32_USBPHYC_MISC,
				       SWITHOST);
	else
		stm32_usbphyc_set_bits(usbphyc->base + STM32_USBPHYC_MISC,
				       SWITHOST);
	usbphyc->switch_setup = utmi_switch;
}

static struct phy *stm32_usbphyc_of_xlate(struct device *dev,
					  const struct of_phandle_args *args)
{
	struct stm32_usbphyc *usbphyc = dev_get_drvdata(dev);
	struct stm32_usbphyc_phy *usbphyc_phy = NULL;
	struct device_node *phynode = args->np;
	int port = 0;

	for (port = 0; port < usbphyc->nphys; port++) {
		if (phynode == usbphyc->phys[port]->phy->dev.of_node) {
			usbphyc_phy = usbphyc->phys[port];
			break;
		}
	}
	if (!usbphyc_phy) {
		dev_err(dev, "failed to find phy\n");
		return ERR_PTR(-EINVAL);
	}

	if (((usbphyc_phy->index == 0) && (args->args_count != 0)) ||
	    ((usbphyc_phy->index == 1) && (args->args_count != 1))) {
		dev_err(dev, "invalid number of cells for phy port%d\n",
			usbphyc_phy->index);
		return ERR_PTR(-EINVAL);
	}

	/* Configure the UTMI switch for PHY port#2 */
	if (usbphyc_phy->index == 1) {
		if (usbphyc->switch_setup < 0) {
			stm32_usbphyc_switch_setup(usbphyc, args->args[0]);
		} else {
			if (args->args[0] != usbphyc->switch_setup) {
				dev_err(dev, "phy port1 already used\n");
				return ERR_PTR(-EBUSY);
			}
		}
	}

	return usbphyc_phy->phy;
}

static int stm32_usbphyc_probe(struct platform_device *pdev)
{
	struct stm32_usbphyc *usbphyc;
	struct device *dev = &pdev->dev;
	struct device_node *child, *np = dev->of_node;
	struct phy_provider *phy_provider;
	u32 pllen, version;
	int ret, port = 0;

	usbphyc = devm_kzalloc(dev, sizeof(*usbphyc), GFP_KERNEL);
	if (!usbphyc)
		return -ENOMEM;
	usbphyc->dev = dev;
	dev_set_drvdata(dev, usbphyc);

	usbphyc->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(usbphyc->base))
		return PTR_ERR(usbphyc->base);

	usbphyc->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(usbphyc->clk))
		return dev_err_probe(dev, PTR_ERR(usbphyc->clk), "clk get_failed\n");

	ret = clk_prepare_enable(usbphyc->clk);
	if (ret) {
		dev_err(dev, "clk enable failed: %d\n", ret);
		return ret;
	}

	usbphyc->rst = devm_reset_control_get(dev, NULL);
	if (!IS_ERR(usbphyc->rst)) {
		reset_control_assert(usbphyc->rst);
		udelay(2);
		reset_control_deassert(usbphyc->rst);
	} else {
		ret = PTR_ERR(usbphyc->rst);
		if (ret == -EPROBE_DEFER)
			goto clk_disable;

		stm32_usbphyc_clr_bits(usbphyc->base + STM32_USBPHYC_PLL, PLLEN);
	}

	/*
	 * Wait for minimum width of powerdown pulse (ENABLE = Low):
	 * we have to ensure the PLL is disabled before phys initialization.
	 */
	if (readl_relaxed_poll_timeout(usbphyc->base + STM32_USBPHYC_PLL,
				       pllen, !(pllen & PLLEN), 5, 50)) {
		dev_warn(usbphyc->dev, "PLL not reset\n");
		ret = -EPROBE_DEFER;
		goto clk_disable;
	}

	usbphyc->switch_setup = -EINVAL;
	usbphyc->nphys = of_get_child_count(np);
	usbphyc->phys = devm_kcalloc(dev, usbphyc->nphys,
				     sizeof(*usbphyc->phys), GFP_KERNEL);
	if (!usbphyc->phys) {
		ret = -ENOMEM;
		goto clk_disable;
	}

	usbphyc->vdda1v1 = devm_regulator_get(dev, "vdda1v1");
	if (IS_ERR(usbphyc->vdda1v1)) {
		ret = dev_err_probe(dev, PTR_ERR(usbphyc->vdda1v1),
				    "failed to get vdda1v1 supply\n");
		goto clk_disable;
	}

	usbphyc->vdda1v8 = devm_regulator_get(dev, "vdda1v8");
	if (IS_ERR(usbphyc->vdda1v8)) {
		ret = dev_err_probe(dev, PTR_ERR(usbphyc->vdda1v8),
				    "failed to get vdda1v8 supply\n");
		goto clk_disable;
	}

	for_each_child_of_node(np, child) {
		struct stm32_usbphyc_phy *usbphyc_phy;
		struct phy *phy;
		u32 index;

		phy = devm_phy_create(dev, child, &stm32_usbphyc_phy_ops);
		if (IS_ERR(phy)) {
			ret = PTR_ERR(phy);
			if (ret != -EPROBE_DEFER)
				dev_err(dev, "failed to create phy%d: %d\n",
					port, ret);
			goto put_child;
		}

		usbphyc_phy = devm_kzalloc(dev, sizeof(*usbphyc_phy),
					   GFP_KERNEL);
		if (!usbphyc_phy) {
			ret = -ENOMEM;
			goto put_child;
		}

		ret = of_property_read_u32(child, "reg", &index);
		if (ret || index > usbphyc->nphys) {
			dev_err(&phy->dev, "invalid reg property: %d\n", ret);
			if (!ret)
				ret = -EINVAL;
			goto put_child;
		}

		usbphyc->phys[port] = usbphyc_phy;
		phy_set_bus_width(phy, 8);
		phy_set_drvdata(phy, usbphyc_phy);

		usbphyc->phys[port]->phy = phy;
		usbphyc->phys[port]->usbphyc = usbphyc;
		usbphyc->phys[port]->index = index;
		usbphyc->phys[port]->active = false;

		usbphyc->phys[port]->vbus = devm_regulator_get_optional(&phy->dev, "vbus");
		if (IS_ERR(usbphyc->phys[port]->vbus)) {
			ret = PTR_ERR(usbphyc->phys[port]->vbus);
			if (ret == -EPROBE_DEFER)
				goto put_child;
			usbphyc->phys[port]->vbus = NULL;
		}

		/* Configure phy tuning */
		stm32_usbphyc_phy_tuning(usbphyc, child, index);

		port++;
	}

	phy_provider = devm_of_phy_provider_register(dev,
						     stm32_usbphyc_of_xlate);
	if (IS_ERR(phy_provider)) {
		ret = PTR_ERR(phy_provider);
		dev_err(dev, "failed to register phy provider: %d\n", ret);
		goto clk_disable;
	}

	ret = stm32_usbphyc_clk48_register(usbphyc);
	if (ret) {
		dev_err(dev, "failed to register ck_usbo_48m clock: %d\n", ret);
		goto clk_disable;
	}

	version = readl_relaxed(usbphyc->base + STM32_USBPHYC_VERSION);
	dev_info(dev, "registered rev:%lu.%lu\n",
		 FIELD_GET(MAJREV, version), FIELD_GET(MINREV, version));

	return 0;

put_child:
	of_node_put(child);
clk_disable:
	clk_disable_unprepare(usbphyc->clk);

	return ret;
}

static void stm32_usbphyc_remove(struct platform_device *pdev)
{
	struct stm32_usbphyc *usbphyc = dev_get_drvdata(&pdev->dev);
	int port;

	/* Ensure PHYs are not active, to allow PLL disabling */
	for (port = 0; port < usbphyc->nphys; port++)
		if (usbphyc->phys[port]->active)
			stm32_usbphyc_phy_exit(usbphyc->phys[port]->phy);

	stm32_usbphyc_clk48_unregister(usbphyc);

	clk_disable_unprepare(usbphyc->clk);
}

static int __maybe_unused stm32_usbphyc_resume(struct device *dev)
{
	struct stm32_usbphyc *usbphyc = dev_get_drvdata(dev);
	struct stm32_usbphyc_phy *usbphyc_phy;
	int port;

	if (usbphyc->switch_setup >= 0)
		stm32_usbphyc_switch_setup(usbphyc, usbphyc->switch_setup);

	for (port = 0; port < usbphyc->nphys; port++) {
		usbphyc_phy = usbphyc->phys[port];
		writel_relaxed(usbphyc_phy->tune, usbphyc->base + STM32_USBPHYC_TUNE(port));
	}

	return 0;
}

static SIMPLE_DEV_PM_OPS(stm32_usbphyc_pm_ops, NULL, stm32_usbphyc_resume);

static const struct of_device_id stm32_usbphyc_of_match[] = {
	{ .compatible = "st,stm32mp1-usbphyc", },
	{ },
};
MODULE_DEVICE_TABLE(of, stm32_usbphyc_of_match);

static struct platform_driver stm32_usbphyc_driver = {
	.probe = stm32_usbphyc_probe,
	.remove = stm32_usbphyc_remove,
	.driver = {
		.of_match_table = stm32_usbphyc_of_match,
		.name = "stm32-usbphyc",
		.pm = &stm32_usbphyc_pm_ops,
	}
};
module_platform_driver(stm32_usbphyc_driver);

MODULE_DESCRIPTION("STMicroelectronics STM32 USBPHYC driver");
MODULE_AUTHOR("Amelie Delaunay <amelie.delaunay@st.com>");
MODULE_LICENSE("GPL v2");
