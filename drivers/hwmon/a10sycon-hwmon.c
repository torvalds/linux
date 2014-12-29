/*
 *  Copyright (C) 2014 Altera Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * HW Mon control for Altera MAX5 Arria10 System Control
 * Adapted from DA9052
 */

#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mfd/a10sycon.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#define A10SC_1V0_BIT_POSITION          A10SC_PG1_1V0_SHIFT
#define A10SC_0V95_BIT_POSITION         A10SC_PG1_0V95_SHIFT
#define A10SC_0V9_BIT_POSITION          A10SC_PG1_0V9_SHIFT
#define A10SC_10V_BIT_POSITION          A10SC_PG1_10V_SHIFT
#define A10SC_5V0_BIT_POSITION          A10SC_PG1_5V0_SHIFT
#define A10SC_3V3_BIT_POSITION          A10SC_PG1_3V3_SHIFT
#define A10SC_2V5_BIT_POSITION          A10SC_PG1_2V5_SHIFT
#define A10SC_1V8_BIT_POSITION          A10SC_PG1_1V8_SHIFT
#define A10SC_OP_FLAG_BIT_POSITION      A10SC_PG1_OP_FLAG_SHIFT
/* 2nd register needs an offset of 8 to get to 2nd register */
#define A10SC_FBC2MP_BIT_POSITION       (8 + A10SC_PG2_FBC2MP_SHIFT)
#define A10SC_FAC2MP_BIT_POSITION       (8 + A10SC_PG2_FAC2MP_SHIFT)
#define A10SC_FMCBVADJ_BIT_POSITION     (8 + A10SC_PG2_FMCBVADJ_SHIFT)
#define A10SC_FMCAVADJ_BIT_POSITION     (8 + A10SC_PG2_FMCAVADJ_SHIFT)
#define A10SC_HL_VDDQ_BIT_POSITION      (8 + A10SC_PG2_HL_VDDQ_SHIFT)
#define A10SC_HL_VDD_BIT_POSITION       (8 + A10SC_PG2_HL_VDD_SHIFT)
#define A10SC_HL_HPS_BIT_POSITION       (8 + A10SC_PG2_HL_HPS_SHIFT)
#define A10SC_HPS_BIT_POSITION          (8 + A10SC_PG2_HPS_SHIFT)
/* 3rd register needs an offset of 16 to get to 3rd register */
#define A10SC_PCIE_WAKE_BIT_POSITION    (16 + A10SC_PG3_PCIE_WAKE_SHIFT)
#define A10SC_PCIE_PR_BIT_POSITION      (16 + A10SC_PG3_PCIE_PR_SHIFT)
#define A10SC_FMCB_PR_BIT_POSITION      (16 + A10SC_PG3_FMCB_PR_SHIFT)
#define A10SC_FMCA_PR_BIT_POSITION      (16 + A10SC_PG3_FMCA_PR_SHIFT)
#define A10SC_FILE_PR_BIT_POSITION      (16 + A10SC_PG3_FILE_PR_SHIFT)
#define A10SC_BF_PR_BIT_POSITION        (16 + A10SC_PG3_BF_PR_SHIFT)
#define A10SC_10V_FAIL_BIT_POSITION     (16 + A10SC_PG3_10V_FAIL_SHIFT)
#define A10SC_FAM2C_BIT_POSITION        (16 + A10SC_PG3_FAM2C_SHIFT)
/* FMCA/B & PCIE Enables need an offset of 24 */
#define A10SC_FMCB_AUXEN_POSITION       (24 + A10SC_FMCB_AUXEN_SHIFT)
#define A10SC_FMCB_EN_POSITION          (24 + A10SC_FMCB_EN_SHIFT)
#define A10SC_FMCA_AUXEN_POSITION       (24 + A10SC_FMCA_AUXEN_SHIFT)
#define A10SC_FMCA_EN_POSITION          (24 + A10SC_FMCA_EN_SHIFT)
#define A10SC_PCIE_AUXEN_POSITION       (24 + A10SC_PCIE_AUXEN_SHIFT)
#define A10SC_PCIE_EN_POSITION          (24 + A10SC_PCIE_EN_SHIFT)

struct a10sycon_hwmon {
	struct a10sycon	*a10sc;
	struct device	*class_device;
};

static const char *const hwmon_names[] = {
	[A10SC_1V0_BIT_POSITION]       = "1.0V PWR Good",
	[A10SC_0V95_BIT_POSITION]      = "0.95V PWR Good",
	[A10SC_0V9_BIT_POSITION]       = "0.9V PWR Good",
	[A10SC_5V0_BIT_POSITION]       = "5.0V PWR Good",
	[A10SC_3V3_BIT_POSITION]       = "3.3V PWR Good",
	[A10SC_2V5_BIT_POSITION]       = "2.5V PWR Good",
	[A10SC_1V8_BIT_POSITION]       = "1.8V PWR Good",
	[A10SC_OP_FLAG_BIT_POSITION]   = "PWR On Complete",

	[A10SC_FBC2MP_BIT_POSITION]    = "FBC2MP PWR Good",
	[A10SC_FAC2MP_BIT_POSITION]    = "FAC2MP PWR Good",
	[A10SC_FMCBVADJ_BIT_POSITION]  = "FMCBVADJ PWR Good",
	[A10SC_FMCAVADJ_BIT_POSITION]  = "FMCAVADJ PWR Good",
	[A10SC_HL_VDDQ_BIT_POSITION]   = "HILO VDDQ PWR Good",
	[A10SC_HL_VDD_BIT_POSITION]    = "HILO VDD PWR Good",
	[A10SC_HL_HPS_BIT_POSITION]    = "HILO HPS PWR Good",
	[A10SC_HPS_BIT_POSITION]       = "HPS PWR Good",

	[A10SC_PCIE_WAKE_BIT_POSITION] = "PCIE WAKEn",
	[A10SC_PCIE_PR_BIT_POSITION]   = "PCIE PRESENTn",
	[A10SC_FMCB_PR_BIT_POSITION]   = "FMCB PRESENTn",
	[A10SC_FMCA_PR_BIT_POSITION]   = "FMCA PRESENTn",
	[A10SC_FILE_PR_BIT_POSITION]   = "FILE PRESENTn",
	[A10SC_BF_PR_BIT_POSITION]     = "BF PRESENTn",
	[A10SC_10V_FAIL_BIT_POSITION]  = "10V FAILn",
	[A10SC_FAM2C_BIT_POSITION]     = "FAM2C PWR Good",

	[A10SC_FMCB_AUXEN_POSITION]    = "FMCB AUX Enable",
	[A10SC_FMCB_EN_POSITION]       = "FMCB Enable",
	[A10SC_FMCA_AUXEN_POSITION]    = "FMCA AUX Enable",
	[A10SC_FMCA_EN_POSITION]       = "FMCA Enable",
	[A10SC_PCIE_AUXEN_POSITION]    = "PCIE AUX Enable",
	[A10SC_PCIE_EN_POSITION]       = "PCIE Enable",
};

static ssize_t a10sycon_read_status(struct device *dev,
				    struct device_attribute *devattr,
				    char *buf)
{
	struct a10sycon_hwmon *hwmon = dev_get_drvdata(dev);
	int ret, index = to_sensor_dev_attr(devattr)->index;
	unsigned char reg = A10SYCON_PWR_GOOD1_RD_REG +
			    A10SYCON_REG_OFFSET(index);

	ret = a10sycon_reg_read(hwmon->a10sc, reg);
	if (ret < 0)
		return ret;

	return sprintf(buf, "0x%X\n", (ret & A10SYCON_REG_BIT_MASK(index)));
}

static ssize_t a10sycon_hwmon_show_name(struct device *dev,
					struct device_attribute *devattr,
					char *buf)
{
	return sprintf(buf, "a10sycon\n");
}

static ssize_t show_label(struct device *dev,
			  struct device_attribute *devattr, char *buf)
{
	return sprintf(buf, "%s\n",
		       hwmon_names[to_sensor_dev_attr(devattr)->index]);
}

static ssize_t set_enable(struct device *dev,
			  struct device_attribute *dev_attr,
			  const char *buf, size_t count)
{
	struct a10sycon_hwmon *hwmon = dev_get_drvdata(dev);
	int ret, index = to_sensor_dev_attr(dev_attr)->index;
	unsigned char reg = (A10SYCON_PWR_GOOD1_RD_REG & WRITE_REG_MASK) +
			    A10SYCON_REG_OFFSET(index);

	ret = a10sycon_reg_write(hwmon->a10sc, reg,
				 A10SYCON_REG_BIT_MASK(index));
	if (ret < 0)
		return ret;

	return count;
}

/* First Power Good Register Bits */
static SENSOR_DEVICE_ATTR(1v0_input, S_IRUGO, a10sycon_read_status, NULL,
			  A10SC_1V0_BIT_POSITION);
static SENSOR_DEVICE_ATTR(1v0_label, S_IRUGO, show_label, NULL,
			  A10SC_1V0_BIT_POSITION);
static SENSOR_DEVICE_ATTR(0v95_input, S_IRUGO, a10sycon_read_status, NULL,
			  A10SC_0V95_BIT_POSITION);
static SENSOR_DEVICE_ATTR(0v95_label, S_IRUGO, show_label, NULL,
			  A10SC_0V95_BIT_POSITION);
static SENSOR_DEVICE_ATTR(0v9_input, S_IRUGO, a10sycon_read_status, NULL,
			  A10SC_0V9_BIT_POSITION);
static SENSOR_DEVICE_ATTR(0v9_label, S_IRUGO, show_label, NULL,
			  A10SC_0V9_BIT_POSITION);
static SENSOR_DEVICE_ATTR(5v0_input, S_IRUGO, a10sycon_read_status, NULL,
			  A10SC_5V0_BIT_POSITION);
static SENSOR_DEVICE_ATTR(5v0_label, S_IRUGO, show_label, NULL,
			  A10SC_5V0_BIT_POSITION);
static SENSOR_DEVICE_ATTR(3v3_input, S_IRUGO, a10sycon_read_status, NULL,
			  A10SC_3V3_BIT_POSITION);
static SENSOR_DEVICE_ATTR(3v3_label, S_IRUGO, show_label, NULL,
			  A10SC_3V3_BIT_POSITION);
static SENSOR_DEVICE_ATTR(2v5_input, S_IRUGO, a10sycon_read_status, NULL,
			  A10SC_2V5_BIT_POSITION);
static SENSOR_DEVICE_ATTR(2v5_label, S_IRUGO, show_label, NULL,
			  A10SC_2V5_BIT_POSITION);
static SENSOR_DEVICE_ATTR(1v8_input, S_IRUGO, a10sycon_read_status, NULL,
			  A10SC_1V8_BIT_POSITION);
static SENSOR_DEVICE_ATTR(1v8_label, S_IRUGO, show_label, NULL,
			  A10SC_1V8_BIT_POSITION);
static SENSOR_DEVICE_ATTR(opflag_input, S_IRUGO, a10sycon_read_status, NULL,
			  A10SC_OP_FLAG_BIT_POSITION);
static SENSOR_DEVICE_ATTR(opflag_label, S_IRUGO, show_label, NULL,
			  A10SC_OP_FLAG_BIT_POSITION);
/* Second Power Good Register Bits */
static SENSOR_DEVICE_ATTR(fbc2mp_input, S_IRUGO, a10sycon_read_status, NULL,
			  A10SC_FBC2MP_BIT_POSITION);
static SENSOR_DEVICE_ATTR(fbc2mp_label, S_IRUGO, show_label, NULL,
			  A10SC_FBC2MP_BIT_POSITION);
static SENSOR_DEVICE_ATTR(fac2mp_input, S_IRUGO, a10sycon_read_status, NULL,
			  A10SC_FAC2MP_BIT_POSITION);
static SENSOR_DEVICE_ATTR(fac2mp_label, S_IRUGO, show_label, NULL,
			  A10SC_FAC2MP_BIT_POSITION);
static SENSOR_DEVICE_ATTR(fmcbvadj_input, S_IRUGO, a10sycon_read_status, NULL,
			  A10SC_FMCBVADJ_BIT_POSITION);
static SENSOR_DEVICE_ATTR(fmcbvadj_label, S_IRUGO, show_label, NULL,
			  A10SC_FMCBVADJ_BIT_POSITION);
static SENSOR_DEVICE_ATTR(fmcavadj_input, S_IRUGO, a10sycon_read_status, NULL,
			  A10SC_FMCAVADJ_BIT_POSITION);
static SENSOR_DEVICE_ATTR(fmcavadj_label, S_IRUGO, show_label, NULL,
			  A10SC_FMCAVADJ_BIT_POSITION);
static SENSOR_DEVICE_ATTR(hl_vddq_input, S_IRUGO, a10sycon_read_status, NULL,
			  A10SC_HL_VDDQ_BIT_POSITION);
static SENSOR_DEVICE_ATTR(hl_vddq_label, S_IRUGO, show_label, NULL,
			  A10SC_HL_VDDQ_BIT_POSITION);
static SENSOR_DEVICE_ATTR(hl_vdd_input, S_IRUGO, a10sycon_read_status, NULL,
			  A10SC_HL_VDD_BIT_POSITION);
static SENSOR_DEVICE_ATTR(hl_vdd_label, S_IRUGO, show_label, NULL,
			  A10SC_HL_VDD_BIT_POSITION);
static SENSOR_DEVICE_ATTR(hlhps_vdd_input, S_IRUGO, a10sycon_read_status, NULL,
			  A10SC_HL_HPS_BIT_POSITION);
static SENSOR_DEVICE_ATTR(hlhps_vdd_label, S_IRUGO, show_label, NULL,
			  A10SC_HL_HPS_BIT_POSITION);
static SENSOR_DEVICE_ATTR(hps_input, S_IRUGO, a10sycon_read_status, NULL,
			  A10SC_HPS_BIT_POSITION);
static SENSOR_DEVICE_ATTR(hps_label, S_IRUGO, show_label, NULL,
			  A10SC_HPS_BIT_POSITION);
/* Third Power Good Register Bits */
static SENSOR_DEVICE_ATTR(pcie_wake_input, S_IRUGO, a10sycon_read_status, NULL,
			  A10SC_PCIE_WAKE_BIT_POSITION);
static SENSOR_DEVICE_ATTR(pcie_wake_label, S_IRUGO, show_label, NULL,
			  A10SC_PCIE_WAKE_BIT_POSITION);
static SENSOR_DEVICE_ATTR(pcie_pr_input, S_IRUGO, a10sycon_read_status, NULL,
			  A10SC_PCIE_PR_BIT_POSITION);
static SENSOR_DEVICE_ATTR(pcie_pr_label, S_IRUGO, show_label, NULL,
			  A10SC_PCIE_PR_BIT_POSITION);
static SENSOR_DEVICE_ATTR(fmcb_pr_input, S_IRUGO, a10sycon_read_status, NULL,
			  A10SC_FMCB_PR_BIT_POSITION);
static SENSOR_DEVICE_ATTR(fmcb_pr_label, S_IRUGO, show_label, NULL,
			  A10SC_FMCB_PR_BIT_POSITION);
static SENSOR_DEVICE_ATTR(fmca_pr_input, S_IRUGO, a10sycon_read_status, NULL,
			  A10SC_FMCA_PR_BIT_POSITION);
static SENSOR_DEVICE_ATTR(fmca_pr_label, S_IRUGO, show_label, NULL,
			  A10SC_FMCA_PR_BIT_POSITION);
static SENSOR_DEVICE_ATTR(file_pr_input, S_IRUGO, a10sycon_read_status, NULL,
			  A10SC_FILE_PR_BIT_POSITION);
static SENSOR_DEVICE_ATTR(file_pr_label, S_IRUGO, show_label, NULL,
			  A10SC_FILE_PR_BIT_POSITION);
static SENSOR_DEVICE_ATTR(bf_pr_input, S_IRUGO, a10sycon_read_status, NULL,
			  A10SC_BF_PR_BIT_POSITION);
static SENSOR_DEVICE_ATTR(bf_pr_label, S_IRUGO, show_label, NULL,
			  A10SC_BF_PR_BIT_POSITION);
static SENSOR_DEVICE_ATTR(10v_fail_input, S_IRUGO, a10sycon_read_status, NULL,
			  A10SC_10V_FAIL_BIT_POSITION);
static SENSOR_DEVICE_ATTR(10v_fail_label, S_IRUGO, show_label, NULL,
			  A10SC_10V_FAIL_BIT_POSITION);
static SENSOR_DEVICE_ATTR(fam2c_input, S_IRUGO, a10sycon_read_status, NULL,
			  A10SC_FAM2C_BIT_POSITION);
static SENSOR_DEVICE_ATTR(fam2c_label, S_IRUGO, show_label, NULL,
			  A10SC_FAM2C_BIT_POSITION);
/* Peripheral Enable bits */
static SENSOR_DEVICE_ATTR(fmcb_aux_en_value, S_IRUGO | S_IWUSR,
			  a10sycon_read_status, set_enable,
			  A10SC_FMCB_AUXEN_POSITION);
static SENSOR_DEVICE_ATTR(fmcb_aux_en_label, S_IRUGO, show_label, NULL,
			  A10SC_FMCB_AUXEN_POSITION);
static SENSOR_DEVICE_ATTR(fmcb_en_value, S_IRUGO | S_IWUSR,
			  a10sycon_read_status, set_enable,
			  A10SC_FMCB_EN_POSITION);
static SENSOR_DEVICE_ATTR(fmcb_en_label, S_IRUGO, show_label, NULL,
			  A10SC_FMCB_EN_POSITION);
static SENSOR_DEVICE_ATTR(fmca_aux_en_value, S_IRUGO | S_IWUSR,
			  a10sycon_read_status, set_enable,
			  A10SC_FMCA_AUXEN_POSITION);
static SENSOR_DEVICE_ATTR(fmca_aux_en_label, S_IRUGO, show_label, NULL,
			  A10SC_FMCA_AUXEN_POSITION);
static SENSOR_DEVICE_ATTR(fmca_en_value, S_IRUGO | S_IWUSR,
			  a10sycon_read_status, set_enable,
			  A10SC_FMCA_EN_POSITION);
static SENSOR_DEVICE_ATTR(fmca_en_label, S_IRUGO, show_label, NULL,
			  A10SC_FMCA_EN_POSITION);
static SENSOR_DEVICE_ATTR(pcie_aux_en_value, S_IRUGO | S_IWUSR,
			  a10sycon_read_status, set_enable,
			  A10SC_PCIE_AUXEN_POSITION);
static SENSOR_DEVICE_ATTR(pcie_aux_en_label, S_IRUGO, show_label, NULL,
			  A10SC_PCIE_AUXEN_POSITION);
static SENSOR_DEVICE_ATTR(pcie_en_value, S_IRUGO | S_IWUSR,
			  a10sycon_read_status, set_enable,
			  A10SC_PCIE_EN_POSITION);
static SENSOR_DEVICE_ATTR(pcie_en_label, S_IRUGO, show_label, NULL,
			  A10SC_PCIE_EN_POSITION);

static DEVICE_ATTR(name, S_IRUGO, a10sycon_hwmon_show_name, NULL);

static struct attribute *a10sycon_attr[] = {
	&dev_attr_name.attr,
	/* First Power Good Register */
	&sensor_dev_attr_1v0_input.dev_attr.attr,
	&sensor_dev_attr_1v0_label.dev_attr.attr,
	&sensor_dev_attr_0v95_input.dev_attr.attr,
	&sensor_dev_attr_0v95_label.dev_attr.attr,
	&sensor_dev_attr_0v9_input.dev_attr.attr,
	&sensor_dev_attr_0v9_label.dev_attr.attr,
	&sensor_dev_attr_5v0_input.dev_attr.attr,
	&sensor_dev_attr_5v0_label.dev_attr.attr,
	&sensor_dev_attr_3v3_input.dev_attr.attr,
	&sensor_dev_attr_3v3_label.dev_attr.attr,
	&sensor_dev_attr_2v5_input.dev_attr.attr,
	&sensor_dev_attr_2v5_label.dev_attr.attr,
	&sensor_dev_attr_1v8_input.dev_attr.attr,
	&sensor_dev_attr_1v8_label.dev_attr.attr,
	&sensor_dev_attr_opflag_input.dev_attr.attr,
	&sensor_dev_attr_opflag_label.dev_attr.attr,
	/* Second Power Good Register */
	&sensor_dev_attr_fbc2mp_input.dev_attr.attr,
	&sensor_dev_attr_fbc2mp_label.dev_attr.attr,
	&sensor_dev_attr_fac2mp_input.dev_attr.attr,
	&sensor_dev_attr_fac2mp_label.dev_attr.attr,
	&sensor_dev_attr_fmcbvadj_input.dev_attr.attr,
	&sensor_dev_attr_fmcbvadj_label.dev_attr.attr,
	&sensor_dev_attr_fmcavadj_input.dev_attr.attr,
	&sensor_dev_attr_fmcavadj_label.dev_attr.attr,
	&sensor_dev_attr_hl_vddq_input.dev_attr.attr,
	&sensor_dev_attr_hl_vddq_label.dev_attr.attr,
	&sensor_dev_attr_hl_vdd_input.dev_attr.attr,
	&sensor_dev_attr_hl_vdd_label.dev_attr.attr,
	&sensor_dev_attr_hlhps_vdd_input.dev_attr.attr,
	&sensor_dev_attr_hlhps_vdd_label.dev_attr.attr,
	&sensor_dev_attr_hps_input.dev_attr.attr,
	&sensor_dev_attr_hps_label.dev_attr.attr,
	/* Third Power Good Register */
	&sensor_dev_attr_pcie_wake_input.dev_attr.attr,
	&sensor_dev_attr_pcie_wake_label.dev_attr.attr,
	&sensor_dev_attr_pcie_pr_input.dev_attr.attr,
	&sensor_dev_attr_pcie_pr_label.dev_attr.attr,
	&sensor_dev_attr_fmcb_pr_input.dev_attr.attr,
	&sensor_dev_attr_fmcb_pr_label.dev_attr.attr,
	&sensor_dev_attr_fmca_pr_input.dev_attr.attr,
	&sensor_dev_attr_fmca_pr_label.dev_attr.attr,
	&sensor_dev_attr_file_pr_input.dev_attr.attr,
	&sensor_dev_attr_file_pr_label.dev_attr.attr,
	&sensor_dev_attr_bf_pr_input.dev_attr.attr,
	&sensor_dev_attr_bf_pr_label.dev_attr.attr,
	&sensor_dev_attr_10v_fail_input.dev_attr.attr,
	&sensor_dev_attr_10v_fail_label.dev_attr.attr,
	&sensor_dev_attr_fam2c_input.dev_attr.attr,
	&sensor_dev_attr_fam2c_label.dev_attr.attr,
	/* Peripheral Enable Register */
	&sensor_dev_attr_fmcb_aux_en_value.dev_attr.attr,
	&sensor_dev_attr_fmcb_aux_en_label.dev_attr.attr,
	&sensor_dev_attr_fmcb_en_value.dev_attr.attr,
	&sensor_dev_attr_fmcb_en_label.dev_attr.attr,
	&sensor_dev_attr_fmca_aux_en_value.dev_attr.attr,
	&sensor_dev_attr_fmca_aux_en_label.dev_attr.attr,
	&sensor_dev_attr_fmca_en_value.dev_attr.attr,
	&sensor_dev_attr_fmca_en_label.dev_attr.attr,
	&sensor_dev_attr_pcie_aux_en_value.dev_attr.attr,
	&sensor_dev_attr_pcie_aux_en_label.dev_attr.attr,
	&sensor_dev_attr_pcie_en_value.dev_attr.attr,
	&sensor_dev_attr_pcie_en_label.dev_attr.attr,
	NULL
};

static const struct attribute_group a10sycon_attr_group = {
	.attrs = a10sycon_attr
};

static int a10sycon_hwmon_probe(struct platform_device *pdev)
{
	struct a10sycon_hwmon *hwmon;
	int ret;

	hwmon = devm_kzalloc(&pdev->dev, sizeof(*hwmon), GFP_KERNEL);
	if (!hwmon)
		return -ENOMEM;

	hwmon->a10sc = dev_get_drvdata(pdev->dev.parent);

	platform_set_drvdata(pdev, hwmon);

	ret = sysfs_create_group(&pdev->dev.kobj, &a10sycon_attr_group);
	if (ret)
		goto err_mem;

	hwmon->class_device = hwmon_device_register(&pdev->dev);
	if (IS_ERR(hwmon->class_device)) {
		ret = PTR_ERR(hwmon->class_device);
		goto err_sysfs;
	}

	return 0;

err_sysfs:
	sysfs_remove_group(&pdev->dev.kobj, &a10sycon_attr_group);
err_mem:
	return ret;
}

static int a10sycon_hwmon_remove(struct platform_device *pdev)
{
	struct a10sycon_hwmon *hwmon = platform_get_drvdata(pdev);

	hwmon_device_unregister(hwmon->class_device);
	sysfs_remove_group(&pdev->dev.kobj, &a10sycon_attr_group);

	return 0;
}

static const struct of_device_id a10sycon_hwmon_of_match[] = {
	{ .compatible = "altr,a10sycon-hwmon" },
	{ },
};
MODULE_DEVICE_TABLE(of, a10sycon_hwmon_of_match);

static struct platform_driver a10sycon_hwmon_driver = {
	.probe = a10sycon_hwmon_probe,
	.remove = a10sycon_hwmon_remove,
	.driver = {
		.name = "a10sycon-hwmon",
		.of_match_table = a10sycon_hwmon_of_match,
	},
};

module_platform_driver(a10sycon_hwmon_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Thor Thayer");
MODULE_DESCRIPTION("HW Monitor driver for Altera Arria10 System Control Chip");
