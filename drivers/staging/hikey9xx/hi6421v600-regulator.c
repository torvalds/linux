/*
 * Device driver for regulators in Hisi IC
 *
 * Copyright (c) 2013 Linaro Ltd.
 * Copyright (c) 2011 Hisilicon.
 *
 * Guodong Xu <guodong.xu@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/slab.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/mfd/hisi_pmic.h>
#include <linux/delay.h>
#include <linux/time.h>
#include <linux/version.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/spmi.h>

struct hisi_regulator_register_info {
	u32 ctrl_reg;
	u32 enable_mask;
	u32 eco_mode_mask;
	u32 vset_reg;
	u32 vset_mask;
};

struct hisi_regulator {
	const char *name;
	struct hisi_regulator_register_info register_info;
	u32 off_on_delay;
	u32 eco_uA;
	struct regulator_desc rdesc;
	int (*dt_parse)(struct hisi_regulator *, struct spmi_device *);
};

static DEFINE_MUTEX(enable_mutex);

static inline struct hisi_pmic *rdev_to_pmic(struct regulator_dev *dev)
{
	/* regulator_dev parent to->
	 * hisi regulator platform device_dev parent to->
	 * hisi pmic platform device_dev
	 */
	return dev_get_drvdata(rdev_get_dev(dev)->parent->parent);
}

/* helper function to ensure when it returns it is at least 'delay_us'
 * microseconds after 'since'.
 */

static int hisi_regulator_is_enabled(struct regulator_dev *dev)
{
	u32 reg_val;
	struct hisi_regulator *sreg = rdev_get_drvdata(dev);
	struct hisi_pmic *pmic = rdev_to_pmic(dev);

	reg_val = hisi_pmic_read(pmic, sreg->register_info.ctrl_reg);
	pr_debug("<[%s]: ctrl_reg=0x%x,enable_state=%d>\n", __func__, sreg->register_info.ctrl_reg,\
			(reg_val & sreg->register_info.enable_mask));

	return ((reg_val & sreg->register_info.enable_mask) != 0);
}

static int hisi_regulator_enable(struct regulator_dev *dev)
{
	struct hisi_regulator *sreg = rdev_get_drvdata(dev);
	struct hisi_pmic *pmic = rdev_to_pmic(dev);

	/* keep a distance of off_on_delay from last time disabled */
	usleep_range(sreg->off_on_delay, sreg->off_on_delay + 1000);

	pr_debug("<[%s]: off_on_delay=%dus>\n", __func__, sreg->off_on_delay);

	/* cannot enable more than one regulator at one time */
	mutex_lock(&enable_mutex);
	usleep_range(HISI_REGS_ENA_PROTECT_TIME,
		     HISI_REGS_ENA_PROTECT_TIME + 1000);



	/* set enable register */
	hisi_pmic_rmw(pmic, sreg->register_info.ctrl_reg,
				sreg->register_info.enable_mask,
				sreg->register_info.enable_mask);
	pr_debug("<[%s]: ctrl_reg=0x%x,enable_mask=0x%x>\n", __func__, sreg->register_info.ctrl_reg,\
			sreg->register_info.enable_mask);

	mutex_unlock(&enable_mutex);

	return 0;
}

static int hisi_regulator_disable(struct regulator_dev *dev)
{
	struct hisi_regulator *sreg = rdev_get_drvdata(dev);
	struct hisi_pmic *pmic = rdev_to_pmic(dev);

	/* set enable register to 0 */
	hisi_pmic_rmw(pmic, sreg->register_info.ctrl_reg,
				sreg->register_info.enable_mask, 0);

	return 0;
}

static int hisi_regulator_get_voltage(struct regulator_dev *dev)
{
	struct hisi_regulator *sreg = rdev_get_drvdata(dev);
	struct hisi_pmic *pmic = rdev_to_pmic(dev);
	u32 reg_val, selector;

	/* get voltage selector */
	reg_val = hisi_pmic_read(pmic, sreg->register_info.vset_reg);
	pr_debug("<[%s]: vset_reg=0x%x>\n", __func__, sreg->register_info.vset_reg);

	selector = (reg_val & sreg->register_info.vset_mask) >>
				(ffs(sreg->register_info.vset_mask) - 1);

	return sreg->rdesc.ops->list_voltage(dev, selector);
}

static int hisi_regulator_set_voltage(struct regulator_dev *dev,
				int min_uV, int max_uV, unsigned *selector)
{
	struct hisi_regulator *sreg = rdev_get_drvdata(dev);
	struct hisi_pmic *pmic = rdev_to_pmic(dev);
	u32 vsel;
	int ret = 0;

	for (vsel = 0; vsel < sreg->rdesc.n_voltages; vsel++) {
		int uV = sreg->rdesc.volt_table[vsel];
		/* Break at the first in-range value */
		if (min_uV <= uV && uV <= max_uV)
			break;
	}

	/* unlikely to happen. sanity test done by regulator core */
	if (unlikely(vsel == sreg->rdesc.n_voltages))
		return -EINVAL;

	*selector = vsel;
	/* set voltage selector */
	hisi_pmic_rmw(pmic, sreg->register_info.vset_reg,
		sreg->register_info.vset_mask,
		vsel << (ffs(sreg->register_info.vset_mask) - 1));

	pr_debug("<[%s]: vset_reg=0x%x, vset_mask=0x%x, value=0x%x>\n", __func__,\
			sreg->register_info.vset_reg,\
			sreg->register_info.vset_mask,\
			vsel << (ffs(sreg->register_info.vset_mask) - 1)\
			);

	return ret;
}

static unsigned int hisi_regulator_get_mode(struct regulator_dev *dev)
{
	struct hisi_regulator *sreg = rdev_get_drvdata(dev);
	struct hisi_pmic *pmic = rdev_to_pmic(dev);
	u32 reg_val;

	reg_val = hisi_pmic_read(pmic, sreg->register_info.ctrl_reg);
	pr_debug("<[%s]: reg_val=%d, ctrl_reg=0x%x, eco_mode_mask=0x%x>\n", __func__, reg_val,\
			sreg->register_info.ctrl_reg,\
			sreg->register_info.eco_mode_mask\
		   );

	if (reg_val & sreg->register_info.eco_mode_mask)
		return REGULATOR_MODE_IDLE;
	else
		return REGULATOR_MODE_NORMAL;
}

static int hisi_regulator_set_mode(struct regulator_dev *dev,
						unsigned int mode)
{
	struct hisi_regulator *sreg = rdev_get_drvdata(dev);
	struct hisi_pmic *pmic = rdev_to_pmic(dev);
	u32 eco_mode;

	switch (mode) {
	case REGULATOR_MODE_NORMAL:
		eco_mode = HISI_ECO_MODE_DISABLE;
		break;
	case REGULATOR_MODE_IDLE:
		eco_mode = HISI_ECO_MODE_ENABLE;
		break;
	default:
		return -EINVAL;
	}

	/* set mode */
	hisi_pmic_rmw(pmic, sreg->register_info.ctrl_reg,
		sreg->register_info.eco_mode_mask,
		eco_mode << (ffs(sreg->register_info.eco_mode_mask) - 1));

	pr_debug("<[%s]: ctrl_reg=0x%x, eco_mode_mask=0x%x, value=0x%x>\n", __func__,\
			sreg->register_info.ctrl_reg,\
			sreg->register_info.eco_mode_mask,\
			eco_mode << (ffs(sreg->register_info.eco_mode_mask) - 1)\
		   );
	return 0;
}


unsigned int hisi_regulator_get_optimum_mode(struct regulator_dev *dev,
			int input_uV, int output_uV, int load_uA)
{
	struct hisi_regulator *sreg = rdev_get_drvdata(dev);

	if ((load_uA == 0) || ((unsigned int)load_uA > sreg->eco_uA))
		return REGULATOR_MODE_NORMAL;
	else
		return REGULATOR_MODE_IDLE;
}

static int hisi_dt_parse_common(struct hisi_regulator *sreg,
					struct spmi_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct regulator_desc *rdesc = &sreg->rdesc;
	unsigned int register_info[3] = {0};
	int ret = 0;

	/* parse .register_info.ctrl_reg */
	ret = of_property_read_u32_array(np, "hisilicon,hisi-ctrl",
						register_info, 3);
	if (ret) {
		dev_err(dev, "no hisilicon,hisi-ctrl property set\n");
		goto dt_parse_common_end;
	}
	sreg->register_info.ctrl_reg = register_info[0];
	sreg->register_info.enable_mask = register_info[1];
	sreg->register_info.eco_mode_mask = register_info[2];

	/* parse .register_info.vset_reg */
	ret = of_property_read_u32_array(np, "hisilicon,hisi-vset",
						register_info, 2);
	if (ret) {
		dev_err(dev, "no hisilicon,hisi-vset property set\n");
		goto dt_parse_common_end;
	}
	sreg->register_info.vset_reg = register_info[0];
	sreg->register_info.vset_mask = register_info[1];

	/* parse .off-on-delay */
	ret = of_property_read_u32(np, "hisilicon,hisi-off-on-delay-us",
						&sreg->off_on_delay);
	if (ret) {
		dev_err(dev, "no hisilicon,hisi-off-on-delay-us property set\n");
		goto dt_parse_common_end;
	}

	/* parse .enable_time */
	ret = of_property_read_u32(np, "hisilicon,hisi-enable-time-us",
				   &rdesc->enable_time);
	if (ret) {
		dev_err(dev, "no hisilicon,hisi-enable-time-us property set\n");
		goto dt_parse_common_end;
	}

	/* parse .eco_uA */
	ret = of_property_read_u32(np, "hisilicon,hisi-eco-microamp",
				   &sreg->eco_uA);
	if (ret) {
		sreg->eco_uA = 0;
		ret = 0;
	}

dt_parse_common_end:
	return ret;
}

static int hisi_dt_parse_ldo(struct hisi_regulator *sreg,
				struct spmi_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct regulator_desc *rdesc = &sreg->rdesc;
	unsigned int *v_table;
	int ret = 0;

	/* parse .n_voltages, and .volt_table */
	ret = of_property_read_u32(np, "hisilicon,hisi-n-voltages",
				   &rdesc->n_voltages);
	if (ret) {
		dev_err(dev, "no hisilicon,hisi-n-voltages property set\n");
		goto dt_parse_ldo_end;
	}

	/* alloc space for .volt_table */
	v_table = devm_kzalloc(dev, sizeof(unsigned int) * rdesc->n_voltages,
								GFP_KERNEL);
	if (unlikely(!v_table)) {
		ret = -ENOMEM;
		dev_err(dev, "no memory for .volt_table\n");
		goto dt_parse_ldo_end;
	}

	ret = of_property_read_u32_array(np, "hisilicon,hisi-vset-table",
						v_table, rdesc->n_voltages);
	if (ret) {
		dev_err(dev, "no hisilicon,hisi-vset-table property set\n");
		goto dt_parse_ldo_end1;
	}
	rdesc->volt_table = v_table;

	/* parse hisi regulator's dt common part */
	ret = hisi_dt_parse_common(sreg, pdev);
	if (ret) {
		dev_err(dev, "failure in hisi_dt_parse_common\n");
		goto dt_parse_ldo_end1;
	}

	return ret;

dt_parse_ldo_end1:
dt_parse_ldo_end:
	return ret;
}

static struct regulator_ops hisi_ldo_rops = {
	.is_enabled = hisi_regulator_is_enabled,
	.enable = hisi_regulator_enable,
	.disable = hisi_regulator_disable,
	.list_voltage = regulator_list_voltage_table,
	.get_voltage = hisi_regulator_get_voltage,
	.set_voltage = hisi_regulator_set_voltage,
	.get_mode = hisi_regulator_get_mode,
	.set_mode = hisi_regulator_set_mode,
	.get_optimum_mode = hisi_regulator_get_optimum_mode,
};

static const struct hisi_regulator hisi_regulator_ldo = {
	.rdesc = {
	.ops = &hisi_ldo_rops,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
		},
	.dt_parse = hisi_dt_parse_ldo,
};

static struct of_device_id of_hisi_regulator_match_tbl[] = {
	{
		.compatible = "hisilicon-hisi-ldo",
		.data = &hisi_regulator_ldo,
	},
	{ /* end */ }
};

static int hisi_regulator_probe(struct spmi_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct regulator_desc *rdesc;
	struct regulator_dev *rdev;
	struct hisi_regulator *sreg = NULL;
	struct regulator_init_data *initdata;
	struct regulator_config config = { };
	const struct of_device_id *match;
	struct regulation_constraints *constraint;
	const char *supplyname = NULL;
	unsigned int temp_modes;

	const struct hisi_regulator *template = NULL;
	int ret = 0;
	/* to check which type of regulator this is */
	match = of_match_device(of_hisi_regulator_match_tbl, &pdev->dev);
	if (NULL == match) {
		pr_err("get hisi regulator fail!\n\r");
		return -EINVAL;
	}

	template = match->data;
	initdata = of_get_regulator_init_data(dev, np, NULL);
	if (NULL == initdata) {
		pr_err("get regulator init data error !\n");
		return -EINVAL;
	}

	/* hisi regulator supports two modes */
	constraint = &initdata->constraints;

	ret = of_property_read_u32_array(np, "hisilicon,valid-modes-mask",
						&(constraint->valid_modes_mask), 1);
	if (ret) {
		pr_err("no hisilicon,valid-modes-mask property set\n");
		ret = -ENODEV;
		return ret;
	}
	ret = of_property_read_u32_array(np, "hisilicon,valid-idle-mask",
						&temp_modes, 1);
	if (ret) {
		pr_err("no hisilicon,valid-modes-mask property set\n");
		ret = -ENODEV;
		return ret;
	}
	constraint->valid_ops_mask |= temp_modes;

	sreg = kmemdup(template, sizeof(*sreg), GFP_KERNEL);
	if (!sreg) {
		pr_err("template kememdup is fail. \n");
		return -ENOMEM;
	}
	sreg->name = initdata->constraints.name;
	rdesc = &sreg->rdesc;
	rdesc->name = sreg->name;
	rdesc->min_uV = initdata->constraints.min_uV;
	supplyname = of_get_property(np, "hisilicon,supply_name", NULL);
	if (supplyname != NULL) {
		initdata->supply_regulator = supplyname;
	}

	/* to parse device tree data for regulator specific */
	ret = sreg->dt_parse(sreg, pdev);
	if (ret) {
		dev_err(dev, "device tree parameter parse error!\n");
		goto hisi_probe_end;
	}

	config.dev = &pdev->dev;
	config.init_data = initdata;
	config.driver_data = sreg;
	config.of_node = pdev->dev.of_node;

	/* register regulator */
	rdev = regulator_register(rdesc, &config);
	if (IS_ERR(rdev)) {
		dev_err(dev, "failed to register %s\n",
			rdesc->name);
		ret = PTR_ERR(rdev);
		goto hisi_probe_end;
	}

	pr_debug("[%s]:valid_modes_mask[0x%x], valid_ops_mask[0x%x]\n", rdesc->name,\
			constraint->valid_modes_mask, constraint->valid_ops_mask);

	dev_set_drvdata(dev, rdev);
hisi_probe_end:
	if (ret)
		kfree(sreg);
	return ret;
}

static void hisi_regulator_remove(struct spmi_device *pdev)
{
	struct regulator_dev *rdev = dev_get_drvdata(&pdev->dev);
	struct hisi_regulator *sreg = rdev_get_drvdata(rdev);

	regulator_unregister(rdev);

	/* TODO: should i worry about that? devm_kzalloc */
	if (sreg->rdesc.volt_table)
		devm_kfree(&pdev->dev, (unsigned int *)sreg->rdesc.volt_table);

	kfree(sreg);
}
static int hisi_regulator_suspend(struct device *dev, pm_message_t state)
{
	struct hisi_regulator *hisi_regulator = dev_get_drvdata(dev);

	if (NULL == hisi_regulator) {
		pr_err("%s:regulator is NULL\n", __func__);
		return -ENOMEM;
	}

	pr_info("%s:+\n", __func__);
	pr_info("%s:-\n", __func__);

	return 0;
}/*lint !e715 */

static int hisi_regulator_resume(struct device *dev)
{
	struct hisi_regulator *hisi_regulator = dev_get_drvdata(dev);

	if (NULL == hisi_regulator) {
		pr_err("%s:regulator is NULL\n", __func__);
		return -ENOMEM;
	}

	pr_info("%s:+\n", __func__);
	pr_info("%s:-\n", __func__);

	return 0;
}

static struct spmi_driver hisi_pmic_driver = {
	.driver = {
		.name	= "hisi_regulator",
		.owner  = THIS_MODULE,
		.of_match_table = of_hisi_regulator_match_tbl,
		.suspend = hisi_regulator_suspend,
		.resume = hisi_regulator_resume,
	},
	.probe	= hisi_regulator_probe,
	.remove	= hisi_regulator_remove,
};

static int __init hisi_regulator_init(void)
{
	return spmi_driver_register(&hisi_pmic_driver);
}

static void __exit hisi_regulator_exit(void)
{
	spmi_driver_unregister(&hisi_pmic_driver);
}

fs_initcall(hisi_regulator_init);
module_exit(hisi_regulator_exit);

MODULE_DESCRIPTION("Hisi regulator driver");
MODULE_LICENSE("GPL v2");

