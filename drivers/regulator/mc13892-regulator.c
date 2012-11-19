/*
 * Regulator Driver for Freescale MC13892 PMIC
 *
 * Copyright 2010 Yong Shen <yong.shen@linaro.org>
 *
 * Based on draft driver from Arnaud Patard <arnaud.patard@rtp-net.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/mfd/mc13892.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/driver.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/module.h>
#include "mc13xxx.h"

#define MC13892_REVISION			7

#define MC13892_POWERCTL0			13
#define MC13892_POWERCTL0_USEROFFSPI		3
#define MC13892_POWERCTL0_VCOINCELLVSEL		20
#define MC13892_POWERCTL0_VCOINCELLVSEL_M	(7<<20)
#define MC13892_POWERCTL0_VCOINCELLEN		(1<<23)

#define MC13892_SWITCHERS0_SWxHI		(1<<23)

#define MC13892_SWITCHERS0			24
#define MC13892_SWITCHERS0_SW1VSEL		0
#define MC13892_SWITCHERS0_SW1VSEL_M		(0x1f<<0)
#define MC13892_SWITCHERS0_SW1HI		(1<<23)
#define MC13892_SWITCHERS0_SW1EN		0

#define MC13892_SWITCHERS1			25
#define MC13892_SWITCHERS1_SW2VSEL		0
#define MC13892_SWITCHERS1_SW2VSEL_M		(0x1f<<0)
#define MC13892_SWITCHERS1_SW2HI		(1<<23)
#define MC13892_SWITCHERS1_SW2EN		0

#define MC13892_SWITCHERS2			26
#define MC13892_SWITCHERS2_SW3VSEL		0
#define MC13892_SWITCHERS2_SW3VSEL_M		(0x1f<<0)
#define MC13892_SWITCHERS2_SW3HI		(1<<23)
#define MC13892_SWITCHERS2_SW3EN		0

#define MC13892_SWITCHERS3			27
#define MC13892_SWITCHERS3_SW4VSEL		0
#define MC13892_SWITCHERS3_SW4VSEL_M		(0x1f<<0)
#define MC13892_SWITCHERS3_SW4HI		(1<<23)
#define MC13892_SWITCHERS3_SW4EN		0

#define MC13892_SWITCHERS4			28
#define MC13892_SWITCHERS4_SW1MODE		0
#define MC13892_SWITCHERS4_SW1MODE_AUTO		(8<<0)
#define MC13892_SWITCHERS4_SW1MODE_M		(0xf<<0)
#define MC13892_SWITCHERS4_SW2MODE		10
#define MC13892_SWITCHERS4_SW2MODE_AUTO		(8<<10)
#define MC13892_SWITCHERS4_SW2MODE_M		(0xf<<10)

#define MC13892_SWITCHERS5			29
#define MC13892_SWITCHERS5_SW3MODE		0
#define MC13892_SWITCHERS5_SW3MODE_AUTO		(8<<0)
#define MC13892_SWITCHERS5_SW3MODE_M		(0xf<<0)
#define MC13892_SWITCHERS5_SW4MODE		8
#define MC13892_SWITCHERS5_SW4MODE_AUTO		(8<<8)
#define MC13892_SWITCHERS5_SW4MODE_M		(0xf<<8)
#define MC13892_SWITCHERS5_SWBSTEN		(1<<20)

#define MC13892_REGULATORSETTING0		30
#define MC13892_REGULATORSETTING0_VGEN1VSEL	0
#define MC13892_REGULATORSETTING0_VDIGVSEL	4
#define MC13892_REGULATORSETTING0_VGEN2VSEL	6
#define MC13892_REGULATORSETTING0_VPLLVSEL	9
#define MC13892_REGULATORSETTING0_VUSB2VSEL	11
#define MC13892_REGULATORSETTING0_VGEN3VSEL	14
#define MC13892_REGULATORSETTING0_VCAMVSEL	16

#define MC13892_REGULATORSETTING0_VGEN1VSEL_M	(3<<0)
#define MC13892_REGULATORSETTING0_VDIGVSEL_M	(3<<4)
#define MC13892_REGULATORSETTING0_VGEN2VSEL_M	(7<<6)
#define MC13892_REGULATORSETTING0_VPLLVSEL_M	(3<<9)
#define MC13892_REGULATORSETTING0_VUSB2VSEL_M	(3<<11)
#define MC13892_REGULATORSETTING0_VGEN3VSEL_M	(1<<14)
#define MC13892_REGULATORSETTING0_VCAMVSEL_M	(3<<16)

#define MC13892_REGULATORSETTING1		31
#define MC13892_REGULATORSETTING1_VVIDEOVSEL	2
#define MC13892_REGULATORSETTING1_VAUDIOVSEL	4
#define MC13892_REGULATORSETTING1_VSDVSEL	6

#define MC13892_REGULATORSETTING1_VVIDEOVSEL_M	(3<<2)
#define MC13892_REGULATORSETTING1_VAUDIOVSEL_M	(3<<4)
#define MC13892_REGULATORSETTING1_VSDVSEL_M	(7<<6)

#define MC13892_REGULATORMODE0			32
#define MC13892_REGULATORMODE0_VGEN1EN		(1<<0)
#define MC13892_REGULATORMODE0_VGEN1STDBY	(1<<1)
#define MC13892_REGULATORMODE0_VGEN1MODE	(1<<2)
#define MC13892_REGULATORMODE0_VIOHIEN		(1<<3)
#define MC13892_REGULATORMODE0_VIOHISTDBY	(1<<4)
#define MC13892_REGULATORMODE0_VIOHIMODE	(1<<5)
#define MC13892_REGULATORMODE0_VDIGEN		(1<<9)
#define MC13892_REGULATORMODE0_VDIGSTDBY	(1<<10)
#define MC13892_REGULATORMODE0_VDIGMODE		(1<<11)
#define MC13892_REGULATORMODE0_VGEN2EN		(1<<12)
#define MC13892_REGULATORMODE0_VGEN2STDBY	(1<<13)
#define MC13892_REGULATORMODE0_VGEN2MODE	(1<<14)
#define MC13892_REGULATORMODE0_VPLLEN		(1<<15)
#define MC13892_REGULATORMODE0_VPLLSTDBY	(1<<16)
#define MC13892_REGULATORMODE0_VPLLMODE		(1<<17)
#define MC13892_REGULATORMODE0_VUSB2EN		(1<<18)
#define MC13892_REGULATORMODE0_VUSB2STDBY	(1<<19)
#define MC13892_REGULATORMODE0_VUSB2MODE	(1<<20)

#define MC13892_REGULATORMODE1			33
#define MC13892_REGULATORMODE1_VGEN3EN		(1<<0)
#define MC13892_REGULATORMODE1_VGEN3STDBY	(1<<1)
#define MC13892_REGULATORMODE1_VGEN3MODE	(1<<2)
#define MC13892_REGULATORMODE1_VCAMEN		(1<<6)
#define MC13892_REGULATORMODE1_VCAMSTDBY	(1<<7)
#define MC13892_REGULATORMODE1_VCAMMODE		(1<<8)
#define MC13892_REGULATORMODE1_VCAMCONFIGEN	(1<<9)
#define MC13892_REGULATORMODE1_VVIDEOEN		(1<<12)
#define MC13892_REGULATORMODE1_VVIDEOSTDBY	(1<<13)
#define MC13892_REGULATORMODE1_VVIDEOMODE	(1<<14)
#define MC13892_REGULATORMODE1_VAUDIOEN		(1<<15)
#define MC13892_REGULATORMODE1_VAUDIOSTDBY	(1<<16)
#define MC13892_REGULATORMODE1_VAUDIOMODE	(1<<17)
#define MC13892_REGULATORMODE1_VSDEN		(1<<18)
#define MC13892_REGULATORMODE1_VSDSTDBY		(1<<19)
#define MC13892_REGULATORMODE1_VSDMODE		(1<<20)

#define MC13892_POWERMISC			34
#define MC13892_POWERMISC_GPO1EN		(1<<6)
#define MC13892_POWERMISC_GPO2EN		(1<<8)
#define MC13892_POWERMISC_GPO3EN		(1<<10)
#define MC13892_POWERMISC_GPO4EN		(1<<12)
#define MC13892_POWERMISC_PWGT1SPIEN		(1<<15)
#define MC13892_POWERMISC_PWGT2SPIEN		(1<<16)
#define MC13892_POWERMISC_GPO4ADINEN		(1<<21)

#define MC13892_POWERMISC_PWGTSPI_M		(3 << 15)

#define MC13892_USB1				50
#define MC13892_USB1_VUSBEN			(1<<3)

static const unsigned int mc13892_vcoincell[] = {
	2500000, 2700000, 2800000, 2900000, 3000000, 3100000,
	3200000, 3300000,
};

static const unsigned int mc13892_sw1[] = {
	600000,   625000,  650000,  675000,  700000,  725000,
	750000,   775000,  800000,  825000,  850000,  875000,
	900000,   925000,  950000,  975000, 1000000, 1025000,
	1050000, 1075000, 1100000, 1125000, 1150000, 1175000,
	1200000, 1225000, 1250000, 1275000, 1300000, 1325000,
	1350000, 1375000
};

static const unsigned int mc13892_sw[] = {
	600000,   625000,  650000,  675000,  700000,  725000,
	750000,   775000,  800000,  825000,  850000,  875000,
	900000,   925000,  950000,  975000, 1000000, 1025000,
	1050000, 1075000, 1100000, 1125000, 1150000, 1175000,
	1200000, 1225000, 1250000, 1275000, 1300000, 1325000,
	1350000, 1375000, 1400000, 1425000, 1450000, 1475000,
	1500000, 1525000, 1550000, 1575000, 1600000, 1625000,
	1650000, 1675000, 1700000, 1725000, 1750000, 1775000,
	1800000, 1825000, 1850000, 1875000
};

static const unsigned int mc13892_swbst[] = {
	5000000,
};

static const unsigned int mc13892_viohi[] = {
	2775000,
};

static const unsigned int mc13892_vpll[] = {
	1050000, 1250000, 1650000, 1800000,
};

static const unsigned int mc13892_vdig[] = {
	1050000, 1250000, 1650000, 1800000,
};

static const unsigned int mc13892_vsd[] = {
	1800000, 2000000, 2600000, 2700000,
	2800000, 2900000, 3000000, 3150000,
};

static const unsigned int mc13892_vusb2[] = {
	2400000, 2600000, 2700000, 2775000,
};

static const unsigned int mc13892_vvideo[] = {
	2700000, 2775000, 2500000, 2600000,
};

static const unsigned int mc13892_vaudio[] = {
	2300000, 2500000, 2775000, 3000000,
};

static const unsigned int mc13892_vcam[] = {
	2500000, 2600000, 2750000, 3000000,
};

static const unsigned int mc13892_vgen1[] = {
	1200000, 1500000, 2775000, 3150000,
};

static const unsigned int mc13892_vgen2[] = {
	1200000, 1500000, 1600000, 1800000,
	2700000, 2800000, 3000000, 3150000,
};

static const unsigned int mc13892_vgen3[] = {
	1800000, 2900000,
};

static const unsigned int mc13892_vusb[] = {
	3300000,
};

static const unsigned int mc13892_gpo[] = {
	2750000,
};

static const unsigned int mc13892_pwgtdrv[] = {
	5000000,
};

static struct regulator_ops mc13892_gpo_regulator_ops;
/* sw regulators need special care due to the "hi bit" */
static struct regulator_ops mc13892_sw_regulator_ops;


#define MC13892_FIXED_DEFINE(name, reg, voltages)		\
	MC13xxx_FIXED_DEFINE(MC13892_, name, reg, voltages,	\
			mc13xxx_fixed_regulator_ops)

#define MC13892_GPO_DEFINE(name, reg, voltages)			\
	MC13xxx_GPO_DEFINE(MC13892_, name, reg, voltages,	\
			mc13892_gpo_regulator_ops)

#define MC13892_SW_DEFINE(name, reg, vsel_reg, voltages)	\
	MC13xxx_DEFINE(MC13892_, name, reg, vsel_reg, voltages, \
			mc13892_sw_regulator_ops)

#define MC13892_DEFINE_REGU(name, reg, vsel_reg, voltages)	\
	MC13xxx_DEFINE(MC13892_, name, reg, vsel_reg, voltages, \
			mc13xxx_regulator_ops)

static struct mc13xxx_regulator mc13892_regulators[] = {
	MC13892_DEFINE_REGU(VCOINCELL, POWERCTL0, POWERCTL0, mc13892_vcoincell),
	MC13892_SW_DEFINE(SW1, SWITCHERS0, SWITCHERS0, mc13892_sw1),
	MC13892_SW_DEFINE(SW2, SWITCHERS1, SWITCHERS1, mc13892_sw),
	MC13892_SW_DEFINE(SW3, SWITCHERS2, SWITCHERS2, mc13892_sw),
	MC13892_SW_DEFINE(SW4, SWITCHERS3, SWITCHERS3, mc13892_sw),
	MC13892_FIXED_DEFINE(SWBST, SWITCHERS5, mc13892_swbst),
	MC13892_FIXED_DEFINE(VIOHI, REGULATORMODE0, mc13892_viohi),
	MC13892_DEFINE_REGU(VPLL, REGULATORMODE0, REGULATORSETTING0,	\
		mc13892_vpll),
	MC13892_DEFINE_REGU(VDIG, REGULATORMODE0, REGULATORSETTING0,	\
		mc13892_vdig),
	MC13892_DEFINE_REGU(VSD, REGULATORMODE1, REGULATORSETTING1,	\
		mc13892_vsd),
	MC13892_DEFINE_REGU(VUSB2, REGULATORMODE0, REGULATORSETTING0,	\
		mc13892_vusb2),
	MC13892_DEFINE_REGU(VVIDEO, REGULATORMODE1, REGULATORSETTING1,	\
		mc13892_vvideo),
	MC13892_DEFINE_REGU(VAUDIO, REGULATORMODE1, REGULATORSETTING1,	\
		mc13892_vaudio),
	MC13892_DEFINE_REGU(VCAM, REGULATORMODE1, REGULATORSETTING0,	\
		mc13892_vcam),
	MC13892_DEFINE_REGU(VGEN1, REGULATORMODE0, REGULATORSETTING0,	\
		mc13892_vgen1),
	MC13892_DEFINE_REGU(VGEN2, REGULATORMODE0, REGULATORSETTING0,	\
		mc13892_vgen2),
	MC13892_DEFINE_REGU(VGEN3, REGULATORMODE1, REGULATORSETTING0,	\
		mc13892_vgen3),
	MC13892_FIXED_DEFINE(VUSB, USB1, mc13892_vusb),
	MC13892_GPO_DEFINE(GPO1, POWERMISC, mc13892_gpo),
	MC13892_GPO_DEFINE(GPO2, POWERMISC, mc13892_gpo),
	MC13892_GPO_DEFINE(GPO3, POWERMISC, mc13892_gpo),
	MC13892_GPO_DEFINE(GPO4, POWERMISC, mc13892_gpo),
	MC13892_GPO_DEFINE(PWGT1SPI, POWERMISC, mc13892_pwgtdrv),
	MC13892_GPO_DEFINE(PWGT2SPI, POWERMISC, mc13892_pwgtdrv),
};

static int mc13892_powermisc_rmw(struct mc13xxx_regulator_priv *priv, u32 mask,
				 u32 val)
{
	struct mc13xxx *mc13892 = priv->mc13xxx;
	int ret;
	u32 valread;

	BUG_ON(val & ~mask);

	mc13xxx_lock(priv->mc13xxx);
	ret = mc13xxx_reg_read(mc13892, MC13892_POWERMISC, &valread);
	if (ret)
		goto out;

	/* Update the stored state for Power Gates. */
	priv->powermisc_pwgt_state =
		(priv->powermisc_pwgt_state & ~mask) | val;
	priv->powermisc_pwgt_state &= MC13892_POWERMISC_PWGTSPI_M;

	/* Construct the new register value */
	valread = (valread & ~mask) | val;
	/* Overwrite the PWGTxEN with the stored version */
	valread = (valread & ~MC13892_POWERMISC_PWGTSPI_M) |
		priv->powermisc_pwgt_state;

	ret = mc13xxx_reg_write(mc13892, MC13892_POWERMISC, valread);
out:
	mc13xxx_unlock(priv->mc13xxx);
	return ret;
}

static int mc13892_gpo_regulator_enable(struct regulator_dev *rdev)
{
	struct mc13xxx_regulator_priv *priv = rdev_get_drvdata(rdev);
	int id = rdev_get_id(rdev);
	u32 en_val = mc13892_regulators[id].enable_bit;
	u32 mask = mc13892_regulators[id].enable_bit;

	dev_dbg(rdev_get_dev(rdev), "%s id: %d\n", __func__, id);

	/* Power Gate enable value is 0 */
	if (id == MC13892_PWGT1SPI || id == MC13892_PWGT2SPI)
		en_val = 0;

	if (id == MC13892_GPO4)
		mask |= MC13892_POWERMISC_GPO4ADINEN;

	return mc13892_powermisc_rmw(priv, mask, en_val);
}

static int mc13892_gpo_regulator_disable(struct regulator_dev *rdev)
{
	struct mc13xxx_regulator_priv *priv = rdev_get_drvdata(rdev);
	int id = rdev_get_id(rdev);
	u32 dis_val = 0;

	dev_dbg(rdev_get_dev(rdev), "%s id: %d\n", __func__, id);

	/* Power Gate disable value is 1 */
	if (id == MC13892_PWGT1SPI || id == MC13892_PWGT2SPI)
		dis_val = mc13892_regulators[id].enable_bit;

	return mc13892_powermisc_rmw(priv, mc13892_regulators[id].enable_bit,
		dis_val);
}

static int mc13892_gpo_regulator_is_enabled(struct regulator_dev *rdev)
{
	struct mc13xxx_regulator_priv *priv = rdev_get_drvdata(rdev);
	int ret, id = rdev_get_id(rdev);
	unsigned int val;

	mc13xxx_lock(priv->mc13xxx);
	ret = mc13xxx_reg_read(priv->mc13xxx, mc13892_regulators[id].reg, &val);
	mc13xxx_unlock(priv->mc13xxx);

	if (ret)
		return ret;

	/* Power Gates state is stored in powermisc_pwgt_state
	 * where the meaning of bits is negated */
	val = (val & ~MC13892_POWERMISC_PWGTSPI_M) |
		(priv->powermisc_pwgt_state ^ MC13892_POWERMISC_PWGTSPI_M);

	return (val & mc13892_regulators[id].enable_bit) != 0;
}


static struct regulator_ops mc13892_gpo_regulator_ops = {
	.enable = mc13892_gpo_regulator_enable,
	.disable = mc13892_gpo_regulator_disable,
	.is_enabled = mc13892_gpo_regulator_is_enabled,
	.list_voltage = regulator_list_voltage_table,
	.set_voltage = mc13xxx_fixed_regulator_set_voltage,
};

static int mc13892_sw_regulator_get_voltage_sel(struct regulator_dev *rdev)
{
	struct mc13xxx_regulator_priv *priv = rdev_get_drvdata(rdev);
	int ret, id = rdev_get_id(rdev);
	unsigned int val;

	dev_dbg(rdev_get_dev(rdev), "%s id: %d\n", __func__, id);

	mc13xxx_lock(priv->mc13xxx);
	ret = mc13xxx_reg_read(priv->mc13xxx,
		mc13892_regulators[id].vsel_reg, &val);
	mc13xxx_unlock(priv->mc13xxx);
	if (ret)
		return ret;

	val = (val & mc13892_regulators[id].vsel_mask)
		>> mc13892_regulators[id].vsel_shift;

	dev_dbg(rdev_get_dev(rdev), "%s id: %d val: %d\n", __func__, id, val);

	return val;
}

static int mc13892_sw_regulator_set_voltage_sel(struct regulator_dev *rdev,
						unsigned selector)
{
	struct mc13xxx_regulator_priv *priv = rdev_get_drvdata(rdev);
	int volt, mask, id = rdev_get_id(rdev);
	u32 reg_value;
	int ret;

	volt = rdev->desc->volt_table[selector];
	mask = mc13892_regulators[id].vsel_mask;
	reg_value = selector << mc13892_regulators[id].vsel_shift;

	if (volt > 1375000) {
		mask |= MC13892_SWITCHERS0_SWxHI;
		reg_value |= MC13892_SWITCHERS0_SWxHI;
	} else if (volt < 1100000) {
		mask |= MC13892_SWITCHERS0_SWxHI;
		reg_value &= ~MC13892_SWITCHERS0_SWxHI;
	}

	mc13xxx_lock(priv->mc13xxx);
	ret = mc13xxx_reg_rmw(priv->mc13xxx, mc13892_regulators[id].reg, mask,
			      reg_value);
	mc13xxx_unlock(priv->mc13xxx);

	return ret;
}

static struct regulator_ops mc13892_sw_regulator_ops = {
	.list_voltage = regulator_list_voltage_table,
	.set_voltage_sel = mc13892_sw_regulator_set_voltage_sel,
	.get_voltage_sel = mc13892_sw_regulator_get_voltage_sel,
};

static int mc13892_vcam_set_mode(struct regulator_dev *rdev, unsigned int mode)
{
	unsigned int en_val = 0;
	struct mc13xxx_regulator_priv *priv = rdev_get_drvdata(rdev);
	int ret, id = rdev_get_id(rdev);

	if (mode == REGULATOR_MODE_FAST)
		en_val = MC13892_REGULATORMODE1_VCAMCONFIGEN;

	mc13xxx_lock(priv->mc13xxx);
	ret = mc13xxx_reg_rmw(priv->mc13xxx, mc13892_regulators[id].reg,
		MC13892_REGULATORMODE1_VCAMCONFIGEN, en_val);
	mc13xxx_unlock(priv->mc13xxx);

	return ret;
}

static unsigned int mc13892_vcam_get_mode(struct regulator_dev *rdev)
{
	struct mc13xxx_regulator_priv *priv = rdev_get_drvdata(rdev);
	int ret, id = rdev_get_id(rdev);
	unsigned int val;

	mc13xxx_lock(priv->mc13xxx);
	ret = mc13xxx_reg_read(priv->mc13xxx, mc13892_regulators[id].reg, &val);
	mc13xxx_unlock(priv->mc13xxx);

	if (ret)
		return ret;

	if (val & MC13892_REGULATORMODE1_VCAMCONFIGEN)
		return REGULATOR_MODE_FAST;

	return REGULATOR_MODE_NORMAL;
}


static int mc13892_regulator_probe(struct platform_device *pdev)
{
	struct mc13xxx_regulator_priv *priv;
	struct mc13xxx *mc13892 = dev_get_drvdata(pdev->dev.parent);
	struct mc13xxx_regulator_platform_data *pdata =
		dev_get_platdata(&pdev->dev);
	struct mc13xxx_regulator_init_data *mc13xxx_data;
	struct regulator_config config = { };
	int i, ret;
	int num_regulators = 0;
	u32 val;

	num_regulators = mc13xxx_get_num_regulators_dt(pdev);
	if (num_regulators <= 0 && pdata)
		num_regulators = pdata->num_regulators;
	if (num_regulators <= 0)
		return -EINVAL;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv) +
		num_regulators * sizeof(priv->regulators[0]),
		GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->num_regulators = num_regulators;
	priv->mc13xxx_regulators = mc13892_regulators;
	priv->mc13xxx = mc13892;
	platform_set_drvdata(pdev, priv);

	mc13xxx_lock(mc13892);
	ret = mc13xxx_reg_read(mc13892, MC13892_REVISION, &val);
	if (ret)
		goto err_unlock;

	/* enable switch auto mode */
	if ((val & 0x0000FFFF) == 0x45d0) {
		ret = mc13xxx_reg_rmw(mc13892, MC13892_SWITCHERS4,
			MC13892_SWITCHERS4_SW1MODE_M |
			MC13892_SWITCHERS4_SW2MODE_M,
			MC13892_SWITCHERS4_SW1MODE_AUTO |
			MC13892_SWITCHERS4_SW2MODE_AUTO);
		if (ret)
			goto err_unlock;

		ret = mc13xxx_reg_rmw(mc13892, MC13892_SWITCHERS5,
			MC13892_SWITCHERS5_SW3MODE_M |
			MC13892_SWITCHERS5_SW4MODE_M,
			MC13892_SWITCHERS5_SW3MODE_AUTO |
			MC13892_SWITCHERS5_SW4MODE_AUTO);
		if (ret)
			goto err_unlock;
	}
	mc13xxx_unlock(mc13892);

	mc13892_regulators[MC13892_VCAM].desc.ops->set_mode
		= mc13892_vcam_set_mode;
	mc13892_regulators[MC13892_VCAM].desc.ops->get_mode
		= mc13892_vcam_get_mode;

	mc13xxx_data = mc13xxx_parse_regulators_dt(pdev, mc13892_regulators,
					ARRAY_SIZE(mc13892_regulators));
	for (i = 0; i < num_regulators; i++) {
		struct regulator_init_data *init_data;
		struct regulator_desc *desc;
		struct device_node *node = NULL;
		int id;

		if (mc13xxx_data) {
			id = mc13xxx_data[i].id;
			init_data = mc13xxx_data[i].init_data;
			node = mc13xxx_data[i].node;
		} else {
			id = pdata->regulators[i].id;
			init_data = pdata->regulators[i].init_data;
		}
		desc = &mc13892_regulators[id].desc;

		config.dev = &pdev->dev;
		config.init_data = init_data;
		config.driver_data = priv;
		config.of_node = node;

		priv->regulators[i] = regulator_register(desc, &config);
		if (IS_ERR(priv->regulators[i])) {
			dev_err(&pdev->dev, "failed to register regulator %s\n",
				mc13892_regulators[i].desc.name);
			ret = PTR_ERR(priv->regulators[i]);
			goto err;
		}
	}

	return 0;
err:
	while (--i >= 0)
		regulator_unregister(priv->regulators[i]);
	return ret;

err_unlock:
	mc13xxx_unlock(mc13892);
	return ret;
}

static int __devexit mc13892_regulator_remove(struct platform_device *pdev)
{
	struct mc13xxx_regulator_priv *priv = platform_get_drvdata(pdev);
	int i;

	platform_set_drvdata(pdev, NULL);

	for (i = 0; i < priv->num_regulators; i++)
		regulator_unregister(priv->regulators[i]);

	return 0;
}

static struct platform_driver mc13892_regulator_driver = {
	.driver	= {
		.name	= "mc13892-regulator",
		.owner	= THIS_MODULE,
	},
	.remove	= mc13892_regulator_remove,
	.probe	= mc13892_regulator_probe,
};

static int __init mc13892_regulator_init(void)
{
	return platform_driver_register(&mc13892_regulator_driver);
}
subsys_initcall(mc13892_regulator_init);

static void __exit mc13892_regulator_exit(void)
{
	platform_driver_unregister(&mc13892_regulator_driver);
}
module_exit(mc13892_regulator_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Yong Shen <yong.shen@linaro.org>");
MODULE_DESCRIPTION("Regulator Driver for Freescale MC13892 PMIC");
MODULE_ALIAS("platform:mc13892-regulator");
