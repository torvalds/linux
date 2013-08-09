/*
 * drivers/regulator/ricoh619-regulator.c
 *
 * Regulator driver for RICOH619 power management chip.
 *
 * Copyright (C) 2012-2013 RICOH COMPANY,LTD
 *
 * Based on code
 *	Copyright (C) 2011 NVIDIA Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
 
/*#define DEBUG			1*/
/*#define VERBOSE_DEBUG		1*/
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/mfd/ricoh619.h>
#include <linux/regulator/ricoh619-regulator.h>

struct ricoh619_regulator {
	int		id;
	int		sleep_id;
	/* Regulator register address.*/
	u8		reg_en_reg;
	u8		en_bit;
	u8		reg_disc_reg;
	u8		disc_bit;
	u8		vout_reg;
	u8		vout_mask;
	u8		vout_reg_cache;
	u8		sleep_reg;
	u8		eco_reg;
	u8		eco_bit;
	u8		eco_slp_reg;
	u8		eco_slp_bit;

	/* chip constraints on regulator behavior */
	int			min_uV;
	int			max_uV;
	int			step_uV;
	int			nsteps;

	/* regulator specific turn-on delay */
	u16			delay;

	/* used by regulator core */
	struct regulator_desc	desc;

	/* Device */
	struct device		*dev;
};

//static unsigned int ricoh619_suspend_status = 0;

static inline struct device *to_ricoh619_dev(struct regulator_dev *rdev)
{
	return rdev_get_dev(rdev)->parent->parent;
}

static int ricoh619_regulator_enable_time(struct regulator_dev *rdev)
{
	struct ricoh619_regulator *ri = rdev_get_drvdata(rdev);

	return ri->delay;
}

static int ricoh619_reg_is_enabled(struct regulator_dev *rdev)
{
	struct ricoh619_regulator *ri = rdev_get_drvdata(rdev);
	struct device *parent = to_ricoh619_dev(rdev);
	uint8_t control;
	int ret;

	ret = ricoh619_read(parent, ri->reg_en_reg, &control);
	if (ret < 0) {
		dev_err(&rdev->dev, "Error in reading the control register\n");
		return ret;
	}
	return (((control >> ri->en_bit) & 1) == 1);
}

static int ricoh619_reg_enable(struct regulator_dev *rdev)
{
	struct ricoh619_regulator *ri = rdev_get_drvdata(rdev);
	struct device *parent = to_ricoh619_dev(rdev);
	int ret;
	ret = ricoh619_set_bits(parent, ri->reg_en_reg, (1 << ri->en_bit));
	if (ret < 0) {
		dev_err(&rdev->dev, "Error in updating the STATE register\n");
		return ret;
	}
	udelay(ri->delay);
	return ret;
}

static int ricoh619_reg_disable(struct regulator_dev *rdev)
{
	struct ricoh619_regulator *ri = rdev_get_drvdata(rdev);
	struct device *parent = to_ricoh619_dev(rdev);
	int ret;
	ret = ricoh619_clr_bits(parent, ri->reg_en_reg, (1 << ri->en_bit));
	if (ret < 0)
		dev_err(&rdev->dev, "Error in updating the STATE register\n");

	return ret;
}

static int ricoh619_list_voltage(struct regulator_dev *rdev, unsigned index)
{
	struct ricoh619_regulator *ri = rdev_get_drvdata(rdev);

	return ri->min_uV + (ri->step_uV * index);
}

static int __ricoh619_set_s_voltage(struct device *parent,
		struct ricoh619_regulator *ri, int min_uV, int max_uV)
{
	int vsel;
	int ret;

	if ((min_uV < ri->min_uV) || (max_uV > ri->max_uV))
		return -EDOM;

	vsel = (min_uV - ri->min_uV + ri->step_uV - 1)/ri->step_uV;
	if (vsel > ri->nsteps)
		return -EDOM;

	ret = ricoh619_update(parent, ri->sleep_reg, vsel, ri->vout_mask);
	if (ret < 0)
		dev_err(ri->dev, "Error in writing the sleep register\n");
	return ret;
}

static int __ricoh619_set_voltage(struct device *parent,
		struct ricoh619_regulator *ri, int min_uV, int max_uV,
		unsigned *selector)
{
	int vsel;
	int ret;
	uint8_t vout_val;

	if ((min_uV < ri->min_uV) || (max_uV > ri->max_uV))
		return -EDOM;

	vsel = (min_uV - ri->min_uV + ri->step_uV - 1)/ri->step_uV;
	if (vsel > ri->nsteps)
		return -EDOM;

	if (selector)
		*selector = vsel;

	vout_val = (ri->vout_reg_cache & ~ri->vout_mask) |
				(vsel & ri->vout_mask);
	ret = ricoh619_write(parent, ri->vout_reg, vout_val);
	if (ret < 0)
		dev_err(ri->dev, "Error in writing the Voltage register\n");
	else
		ri->vout_reg_cache = vout_val;

	return ret;
}

static int ricoh619_set_voltage(struct regulator_dev *rdev,
		int min_uV, int max_uV, unsigned *selector)
{
	struct ricoh619_regulator *ri = rdev_get_drvdata(rdev);
	struct device *parent = to_ricoh619_dev(rdev);

//	if(ricoh619_suspend_status)
//		return -EBUSY;

	return __ricoh619_set_voltage(parent, ri, min_uV, max_uV, selector);
}

static int ricoh619_set_suspend_voltage(struct regulator_dev *rdev,
		int uV)
{
	struct ricoh619_regulator *ri = rdev_get_drvdata(rdev);
	struct device *parent = to_ricoh619_dev(rdev);

	return __ricoh619_set_s_voltage(parent, ri, uV, uV);
}

static int ricoh619_get_voltage(struct regulator_dev *rdev)
{
	struct ricoh619_regulator *ri = rdev_get_drvdata(rdev);
	uint8_t vsel;

	vsel = ri->vout_reg_cache & ri->vout_mask;
	return ri->min_uV + vsel * ri->step_uV;
}

 int ricoh619_regulator_enable_eco_mode(struct regulator_dev *rdev)
{
	struct ricoh619_regulator *ri = rdev_get_drvdata(rdev);
	struct device *parent = to_ricoh619_dev(rdev);
	int ret;

	ret = ricoh619_set_bits(parent, ri->eco_reg, (1 << ri->eco_bit));
	if (ret < 0)
		dev_err(&rdev->dev, "Error Enable LDO eco mode\n");

	return ret;
}
EXPORT_SYMBOL_GPL(ricoh619_regulator_enable_eco_mode);

int ricoh619_regulator_disable_eco_mode(struct regulator_dev *rdev)
{
	struct ricoh619_regulator *ri = rdev_get_drvdata(rdev);
	struct device *parent = to_ricoh619_dev(rdev);
	int ret;

	ret = ricoh619_clr_bits(parent, ri->eco_reg, (1 << ri->eco_bit));
	if (ret < 0)
		dev_err(&rdev->dev, "Error Disable LDO eco mode\n");

	return ret;
}
EXPORT_SYMBOL_GPL(ricoh619_regulator_disable_eco_mode);

int ricoh619_regulator_enable_eco_slp_mode(struct regulator_dev *rdev)
{
	struct ricoh619_regulator *ri = rdev_get_drvdata(rdev);
	struct device *parent = to_ricoh619_dev(rdev);
	int ret;

	ret = ricoh619_set_bits(parent, ri->eco_slp_reg, (1 << ri->eco_slp_bit));
	if (ret < 0)
		dev_err(&rdev->dev, "Error Enable LDO eco mode in d during sleep\n");

	return ret;
}
EXPORT_SYMBOL_GPL(ricoh619_regulator_enable_eco_slp_mode);

int ricoh619_regulator_disable_eco_slp_mode(struct regulator_dev *rdev)
{
	struct ricoh619_regulator *ri = rdev_get_drvdata(rdev);
	struct device *parent = to_ricoh619_dev(rdev);
	int ret;

	ret = ricoh619_clr_bits(parent, ri->eco_slp_reg, (1 << ri->eco_slp_bit));
	if (ret < 0)
		dev_err(&rdev->dev, "Error Enable LDO eco mode in d during sleep\n");

	return ret;
}
EXPORT_SYMBOL_GPL(ricoh619_regulator_disable_eco_slp_mode);

static unsigned int ricoh619_dcdc_get_mode(struct regulator_dev *rdev)
{
	struct ricoh619_regulator *ri = rdev_get_drvdata(rdev);
	struct device *parent = to_ricoh619_dev(rdev);
	int ret;
	uint8_t control;
	u8 mask = 0x30;
	
	ret = ricoh619_read(parent, ri->reg_en_reg,&control);
        if (ret < 0) {
                return ret;
        }
	control=(control & mask) >> 4;
	switch (control) {
	case 1:
		return REGULATOR_MODE_FAST;
	case 0:
		return REGULATOR_MODE_NORMAL;
	case 2:
		return REGULATOR_MODE_STANDBY;
	case 3:
		return REGULATOR_MODE_NORMAL;
	default:
		return -1;
	}

}
static int ricoh619_dcdc_set_mode(struct regulator_dev *rdev, unsigned int mode)
{
	struct ricoh619_regulator *ri = rdev_get_drvdata(rdev);
	struct device *parent = to_ricoh619_dev(rdev);
	int ret;
	uint8_t control;
	
	ret = ricoh619_read(parent, ri->reg_en_reg,&control);
	switch(mode)
	{
	case REGULATOR_MODE_FAST:
		return ricoh619_write(parent, ri->reg_en_reg, ((control & 0xcf) | 0x10));
	case REGULATOR_MODE_NORMAL:
		return ricoh619_write(parent, ri->reg_en_reg, (control & 0xcf));
	case REGULATOR_MODE_STANDBY:
		return ricoh619_write(parent, ri->reg_en_reg, ((control & 0xcf) | 0x20));	
	default:
		printk("error:pmu_619 only powersave pwm psm mode\n");
		return -EINVAL;
	}
	

}

static int ricoh619_dcdc_set_voltage_time_sel(struct regulator_dev *rdev,   unsigned int old_selector,
				     unsigned int new_selector)
{
	int old_volt, new_volt;
	
	old_volt = ricoh619_list_voltage(rdev, old_selector);
	if (old_volt < 0)
		return old_volt;
	
	new_volt = ricoh619_list_voltage(rdev, new_selector);
	if (new_volt < 0)
		return new_volt;

	return DIV_ROUND_UP(abs(old_volt - new_volt)*2, 14000);
}


static struct regulator_ops ricoh619_ops = {
	.list_voltage			= ricoh619_list_voltage,
	.set_voltage			= ricoh619_set_voltage,
	.get_voltage			= ricoh619_get_voltage,
	.set_suspend_voltage = ricoh619_set_suspend_voltage,
	.set_voltage_time_sel = ricoh619_dcdc_set_voltage_time_sel,
	.get_mode = ricoh619_dcdc_get_mode,
	.set_mode = ricoh619_dcdc_set_mode,
	.enable				= ricoh619_reg_enable,
	.disable				= ricoh619_reg_disable,
	.is_enabled			= ricoh619_reg_is_enabled,
};

#define RICOH619_REG(_id, _en_reg, _en_bit, _disc_reg, _disc_bit, _vout_reg, \
		_vout_mask, _ds_reg, _min_uv, _max_uv, _step_uV, _nsteps,    \
		_ops, _delay, _eco_reg, _eco_bit, _eco_slp_reg, _eco_slp_bit)		\
{								\
	.reg_en_reg	= _en_reg,				\
	.en_bit		= _en_bit,				\
	.reg_disc_reg	= _disc_reg,				\
	.disc_bit	= _disc_bit,				\
	.vout_reg	= _vout_reg,				\
	.vout_mask	= _vout_mask,				\
	.sleep_reg	= _ds_reg,				\
	.min_uV		= _min_uv,			\
	.max_uV		= _max_uv ,			\
	.step_uV	= _step_uV,				\
	.nsteps		= _nsteps,				\
	.delay		= _delay,				\
	.id		= RICOH619_ID_##_id,			\
	.sleep_id	= RICOH619_DS_##_id,			\
	.eco_reg			=  _eco_reg,				\
	.eco_bit			=  _eco_bit,				\
	.eco_slp_reg		=  _eco_slp_reg,				\
	.eco_slp_bit		=  _eco_slp_bit,				\
	.desc = {						\
		.name = ricoh619_rails(_id),			\
		.id = RICOH619_ID_##_id,			\
		.n_voltages = _nsteps,				\
		.ops = &_ops,					\
		.type = REGULATOR_VOLTAGE,			\
		.owner = THIS_MODULE,				\
	},							\
}

static struct ricoh619_regulator ricoh619_regulator[] = {
  	RICOH619_REG(DC1, 0x2C, 0, 0x2C, 1, 0x36, 0xFF, 0x3B,
			600000, 3500000, 12500, 0xE8, ricoh619_ops, 500,
			0x00, 0, 0x00, 0),

  	RICOH619_REG(DC2, 0x2E, 0, 0x2E, 1, 0x37, 0xFF, 0x3C,
			600000, 3500000, 12500, 0xE8, ricoh619_ops, 500,
			0x00, 0, 0x00, 0),

  	RICOH619_REG(DC3, 0x30, 0, 0x30, 1, 0x38, 0xFF, 0x3D,
			600000, 3500000, 12500, 0xE8, ricoh619_ops, 500,
			0x00, 0, 0x00, 0),

  	RICOH619_REG(DC4, 0x32, 0, 0x32, 1, 0x39, 0xFF, 0x3E,
			600000, 3500000, 12500, 0xE8, ricoh619_ops, 500,
			0x00, 0, 0x00, 0),

  	RICOH619_REG(DC5, 0x34, 0, 0x34, 1, 0x3A, 0xFF, 0x3F,
			600000, 3500000, 12500, 0xE8, ricoh619_ops, 500,
			0x00, 0, 0x00, 0),
			
  	RICOH619_REG(LDO1, 0x44, 0, 0x46, 0, 0x4C, 0x7F, 0x58,
			900000, 3500000, 25000, 0x68, ricoh619_ops, 500,
			0x48, 0, 0x4A, 0),

	RICOH619_REG(LDO2, 0x44, 1, 0x46, 1, 0x4D, 0x7F, 0x59,
			900000, 3500000, 25000, 0x68, ricoh619_ops, 500,
			0x48, 1, 0x4A, 1),

  	RICOH619_REG(LDO3, 0x44, 2, 0x46, 2, 0x4E, 0x7F, 0x5A,
			900000, 3500000, 25000, 0x68, ricoh619_ops, 500,
			0x48, 2, 0x4A, 2),

  	RICOH619_REG(LDO4, 0x44, 3, 0x46, 3, 0x4F, 0x7F, 0x5B,
			900000, 3500000, 25000, 0x68, ricoh619_ops, 500,
			0x48, 3, 0x4A, 3),

  	RICOH619_REG(LDO5, 0x44, 4, 0x46, 4, 0x50, 0x7F, 0x5C,
			600000, 3500000, 25000, 0x74, ricoh619_ops, 500,
			0x48, 4, 0x4A, 4),

  	RICOH619_REG(LDO6, 0x44, 5, 0x46, 5, 0x51, 0x7F, 0x5D,
			600000, 3500000, 25000, 0x74, ricoh619_ops, 500,
			0x48, 5, 0x4A, 5),

  	RICOH619_REG(LDO7, 0x44, 6, 0x46, 6, 0x52, 0x7F, 0x5E,
			900000, 3500000, 25000, 0x68, ricoh619_ops, 500,
			0x00, 0, 0x00, 0),

  	RICOH619_REG(LDO8, 0x44, 7, 0x46, 7, 0x53, 0x7F, 0x5F,
			900000, 3500000, 25000, 0x68, ricoh619_ops, 500,
			0x00, 0, 0x00, 0),

  	RICOH619_REG(LDO9, 0x45, 0, 0x47, 0, 0x54, 0x7F, 0x60,
			900000, 3500000, 25000, 0x68, ricoh619_ops, 500,
			0x00, 0, 0x00, 0),

  	RICOH619_REG(LDO10, 0x45, 1, 0x47, 1, 0x55, 0x7F, 0x61,
			900000, 3500000, 25000, 0x68, ricoh619_ops, 500,
			0x00, 0, 0x00, 0),

  	RICOH619_REG(LDORTC1, 0x45, 4, 0x00, 0, 0x56, 0x7F, 0x00,
			1700000, 3500000, 25000, 0x48, ricoh619_ops, 500,
			0x00, 0, 0x00, 0),

  	RICOH619_REG(LDORTC2, 0x45, 5, 0x00, 0, 0x57, 0x7F, 0x00,
			900000, 3500000, 25000, 0x68, ricoh619_ops, 500,
			0x00, 0, 0x00, 0),
};
static inline struct ricoh619_regulator *find_regulator_info(int id)
{
	struct ricoh619_regulator *ri;
	int i;

	for (i = 0; i < ARRAY_SIZE(ricoh619_regulator); i++) {
		ri = &ricoh619_regulator[i];
		if (ri->desc.id == id)
			return ri;
	}
	return NULL;
}

static int ricoh619_regulator_preinit(struct device *parent,
		struct ricoh619_regulator *ri,
		struct ricoh619_regulator_platform_data *ricoh619_pdata)
{
	int ret = 0;

	if (!ricoh619_pdata->init_apply)
		return 0;
/*
	if (ricoh619_pdata->init_uV >= 0) {
		ret = __ricoh619_set_voltage(parent, ri,
				ricoh619_pdata->init_uV,
				ricoh619_pdata->init_uV, 0);
		if (ret < 0) {
			dev_err(ri->dev, "Not able to initialize voltage %d "
				"for rail %d err %d\n", ricoh619_pdata->init_uV,
				ri->desc.id, ret);
			return ret;
		}
	}
*/
	if (ricoh619_pdata->init_enable)
		ret = ricoh619_set_bits(parent, ri->reg_en_reg,
							(1 << ri->en_bit));
	else
		ret = ricoh619_clr_bits(parent, ri->reg_en_reg,
							(1 << ri->en_bit));
	if (ret < 0)
		dev_err(ri->dev, "Not able to %s rail %d err %d\n",
			(ricoh619_pdata->init_enable) ? "enable" : "disable",
			ri->desc.id, ret);

	return ret;
}

static inline int ricoh619_cache_regulator_register(struct device *parent,
	struct ricoh619_regulator *ri)
{
	ri->vout_reg_cache = 0;
	return ricoh619_read(parent, ri->vout_reg, &ri->vout_reg_cache);
}

static int __devinit ricoh619_regulator_probe(struct platform_device *pdev)
{
	struct ricoh619_regulator *ri = NULL;
	struct regulator_dev *rdev;
	struct ricoh619_regulator_platform_data *tps_pdata;
	int id = pdev->id;
	int err;

	ri = find_regulator_info(id);
	if (ri == NULL) {
		dev_err(&pdev->dev, "invalid regulator ID specified\n");
		return -EINVAL;
	}
	tps_pdata = pdev->dev.platform_data;
	ri->dev = &pdev->dev;
/*
	err = ricoh619_cache_regulator_register(pdev->dev.parent, ri);
	if (err) {
		dev_err(&pdev->dev, "Fail in caching register\n");
		return err;
	}

	err = ricoh619_regulator_preinit(pdev->dev.parent, ri, tps_pdata);
	if (err) {
		dev_err(&pdev->dev, "Fail in pre-initialisation\n");
		return err;
	}
	*/
	rdev = regulator_register(&ri->desc, &pdev->dev,
				&tps_pdata->regulator, ri);
	if (IS_ERR_OR_NULL(rdev)) {
		dev_err(&pdev->dev, "failed to register regulator %s\n",
				ri->desc.name);
		return PTR_ERR(rdev);
	}

	platform_set_drvdata(pdev, rdev);
	return 0;
}

static int __devexit ricoh619_regulator_remove(struct platform_device *pdev)
{
	struct regulator_dev *rdev = platform_get_drvdata(pdev);

	regulator_unregister(rdev);
	return 0;
}

static struct platform_driver ricoh619_regulator_driver = {
	.driver	= {
		.name	= "ricoh619-regulator",
		.owner	= THIS_MODULE,
	},
	.probe		= ricoh619_regulator_probe,
	.remove		= __devexit_p(ricoh619_regulator_remove),
};

static int __init ricoh619_regulator_init(void)
{

	return platform_driver_register(&ricoh619_regulator_driver);
}
subsys_initcall_sync(ricoh619_regulator_init);

static void __exit ricoh619_regulator_exit(void)
{
	platform_driver_unregister(&ricoh619_regulator_driver);
}
module_exit(ricoh619_regulator_exit);

MODULE_DESCRIPTION("RICOH619 regulator driver");
MODULE_ALIAS("platform:ricoh619-regulator");
MODULE_AUTHOR("zhangqing <zhangqing@rock-chips.com>");
MODULE_LICENSE("GPL");
