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
#ifdef CONFIG_HISI_PMIC_DEBUG
#include <linux/debugfs.h>
#endif
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/spmi.h>

#if 1
#define BRAND_DEBUG(args...) pr_debug(args);
#else
#define BRAND_DEBUG(args...)
#endif

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
	struct timeval last_off_time;
	u32 off_on_delay;
	u32 eco_uA;
	struct regulator_desc rdesc;
	int (*dt_parse)(struct hisi_regulator *, struct spmi_device *);
};

static DEFINE_MUTEX(enable_mutex);
struct timeval last_enabled;


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
static void ensured_time_after(struct timeval since, u32 delay_us)
{
	struct timeval now;
	u64 elapsed_ns64, delay_ns64;
	u32 actual_us32;

	delay_ns64 = delay_us * NSEC_PER_USEC;
	do_gettimeofday(&now);
	elapsed_ns64 = timeval_to_ns(&now) - timeval_to_ns(&since);
	if (delay_ns64 > elapsed_ns64) {
		actual_us32 = ((u32)(delay_ns64 - elapsed_ns64) /
							NSEC_PER_USEC);
		if (actual_us32 >= 1000) {
			mdelay(actual_us32 / 1000); /*lint !e647 */
			udelay(actual_us32 % 1000);
		} else if (actual_us32 > 0) {
			udelay(actual_us32);
		}
	}
	return;
}

static int hisi_regulator_is_enabled(struct regulator_dev *dev)
{
	u32 reg_val;
	struct hisi_regulator *sreg = rdev_get_drvdata(dev);
	struct hisi_pmic *pmic = rdev_to_pmic(dev);

	reg_val = hisi_pmic_read(pmic, sreg->register_info.ctrl_reg);
	BRAND_DEBUG("<[%s]: ctrl_reg=0x%x,enable_state=%d>\n", __func__, sreg->register_info.ctrl_reg,\
			(reg_val & sreg->register_info.enable_mask));

	return ((reg_val & sreg->register_info.enable_mask) != 0);
}

static int hisi_regulator_enable(struct regulator_dev *dev)
{
	struct hisi_regulator *sreg = rdev_get_drvdata(dev);
	struct hisi_pmic *pmic = rdev_to_pmic(dev);

	/* keep a distance of off_on_delay from last time disabled */
	ensured_time_after(sreg->last_off_time, sreg->off_on_delay);

	BRAND_DEBUG("<[%s]: off_on_delay=%dus>\n", __func__, sreg->off_on_delay);

	/* cannot enable more than one regulator at one time */
	mutex_lock(&enable_mutex);
	ensured_time_after(last_enabled, HISI_REGS_ENA_PROTECT_TIME);

	/* set enable register */
	hisi_pmic_rmw(pmic, sreg->register_info.ctrl_reg,
				sreg->register_info.enable_mask,
				sreg->register_info.enable_mask);
	BRAND_DEBUG("<[%s]: ctrl_reg=0x%x,enable_mask=0x%x>\n", __func__, sreg->register_info.ctrl_reg,\
			sreg->register_info.enable_mask);

	do_gettimeofday(&last_enabled);
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

	do_gettimeofday(&sreg->last_off_time);

	return 0;
}

static int hisi_regulator_get_voltage(struct regulator_dev *dev)
{
	struct hisi_regulator *sreg = rdev_get_drvdata(dev);
	struct hisi_pmic *pmic = rdev_to_pmic(dev);
	u32 reg_val, selector;

	/* get voltage selector */
	reg_val = hisi_pmic_read(pmic, sreg->register_info.vset_reg);
	BRAND_DEBUG("<[%s]: vset_reg=0x%x>\n", __func__, sreg->register_info.vset_reg);

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

	BRAND_DEBUG("<[%s]: vset_reg=0x%x, vset_mask=0x%x, value=0x%x>\n", __func__,\
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
	BRAND_DEBUG("<[%s]: reg_val=%d, ctrl_reg=0x%x, eco_mode_mask=0x%x>\n", __func__, reg_val,\
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

	BRAND_DEBUG("<[%s]: ctrl_reg=0x%x, eco_mode_mask=0x%x, value=0x%x>\n", __func__,\
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

#ifdef CONFIG_HISI_PMIC_DEBUG
extern void get_current_regulator_dev(struct seq_file *s);
extern void set_regulator_state(char *ldo_name, int value);
extern void get_regulator_state(char *ldo_name);
extern int set_regulator_voltage(char *ldo_name, unsigned int vol_value);

u32 pmu_atoi(char *s)
{
	char *p = s;
	char c;
	u64 ret = 0;
	if (s == NULL)
		return 0;
	while ((c = *p++) != '\0') {
		if ('0' <= c && c <= '9') {
			ret *= 10;
			ret += (u64)((unsigned char)c - '0');
			if (ret > U32_MAX)
				return 0;
		} else {
			break;
		}
	}
	return (u32)ret;
}
static int dbg_hisi_regulator_show(struct seq_file *s, void *data)
{
	seq_printf(s, "\n\r");
	seq_printf(s, "%-13s %-15s %-15s %-15s %-15s\n\r",
			"LDO_NAME", "ON/OFF", "Use_count", "Open_count", "Always_on");
	seq_printf(s, "-----------------------------------------"
			"-----------------------------------------------\n\r");
	get_current_regulator_dev(s);
	return 0;
}

static int dbg_hisi_regulator_open(struct inode *inode, struct file *file)
{
	return single_open(file, dbg_hisi_regulator_show, inode->i_private);
}

static const struct file_operations debug_regulator_state_fops = {
	.open		= dbg_hisi_regulator_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int dbg_control_regulator_show(struct seq_file *s, void *data)
{
	printk("                                                                             \n\r \
		---------------------------------------------------------------------------------\n\r \
		|usage:                                                                         |\n\r \
		|	S = state	R = read	V = voltage                                         |\n\r \
		|	set ldo state and voltage                                                   |\n\r \
		|	get ldo state and current voltage                                           |\n\r \
		|example:                                                                       |\n\r \
		|	echo S ldo16 0   > control_regulator	:disable ldo16                      |\n\r \
		|	echo S ldo16 1   > control_regulator	:enable ldo16                       |\n\r \
		|	echo R ldo16     > control_regulator	:get ldo16 state and voltage        |\n\r \
		|	echo V ldo16 xxx > control_regulator	:set ldo16 voltage                  |\n\r \
		---------------------------------------------------------------------------------\n\r");
	return 0;
}
static ssize_t dbg_control_regulator_set_value(struct file *filp, const char __user *buffer,
	size_t count, loff_t *ppos)
{
	char tmp[128] = {0};
	char ptr[128] = {0};
	char *vol = NULL;
	char num = 0;
	unsigned int i;
	int next_flag = 1;

	if (count >= 128) {
		pr_info("error! buffer size big than internal buffer\n");
		return -EFAULT;
	}

	if (copy_from_user(tmp, buffer, count)) {
		pr_info("error!\n");
		return -EFAULT;
	}

	if (tmp[0] == 'R' || tmp[0] == 'r') {
		for (i = 2; i < (count - 1); i++) {
			ptr[i - 2] = tmp[i];
		}
		ptr[i - 2] = '\0';
		get_regulator_state(ptr);
	} else if (tmp[0] == 'S' || tmp[0] == 's') {
		for (i = 2; i < (count - 1); i++) {
			if (tmp[i] == ' ') {
				next_flag = 0;
				ptr[i - 2] = '\0';
				continue;
			}
			if (next_flag) {
				ptr[i - 2] = tmp[i];
			} else {
				num = tmp[i] - 48;
			}
		}
		set_regulator_state(ptr, num);
	} else if (tmp[0] == 'V' || tmp[0] == 'v') {
		for (i = 2; i < (count - 1); i++) {
			if (tmp[i] == ' ') {
				next_flag = 0;
				ptr[i - 2] = '\0';
				continue;
			}
			if (next_flag) {
				ptr[i - 2] = tmp[i];
			} else {
				vol = &tmp[i];
				break;
			}
		}
		set_regulator_voltage(ptr, pmu_atoi(vol));
	}

	*ppos += count;

	return count;
}

static int dbg_control_regulator_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return single_open(file, dbg_control_regulator_show, &inode->i_private);
}

static const struct file_operations set_control_regulator_fops = {
	.open		= dbg_control_regulator_open,
	.read		= seq_read,
	.write		= dbg_control_regulator_set_value,
	.llseek		= seq_lseek,
	.release	= single_release,
};
#endif

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
#ifdef CONFIG_HISI_PMIC_DEBUG
	struct dentry *d;
	static int debugfs_flag;
#endif
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
#if (LINUX_VERSION_CODE > KERNEL_VERSION(3, 13, 0))
	initdata = of_get_regulator_init_data(dev, np, NULL);
#else
	initdata = of_get_regulator_init_data(dev, np);
#endif
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

	BRAND_DEBUG("[%s]:valid_modes_mask[0x%x], valid_ops_mask[0x%x]\n", rdesc->name,\
			constraint->valid_modes_mask, constraint->valid_ops_mask);

	dev_set_drvdata(dev, rdev);
#ifdef CONFIG_HISI_PMIC_DEBUG
	if (debugfs_flag == 0) {
		d = debugfs_create_dir("hisi_regulator_debugfs", NULL);
		if (!d) {
			dev_err(dev, "failed to create hisi regulator debugfs dir !\n");
			ret = -ENOMEM;
			goto hisi_probe_fail;
		}
		(void) debugfs_create_file("regulator_state", S_IRUSR,
						d, NULL, &debug_regulator_state_fops);

		(void) debugfs_create_file("control_regulator", S_IRUSR,
						d, NULL, &set_control_regulator_fops);
		debugfs_flag = 1;
	}
#endif

#ifdef CONFIG_HISI_PMIC_DEBUG
hisi_probe_fail:
	if (ret)
		regulator_unregister(rdev);
#endif
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

