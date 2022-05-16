// SPDX-License-Identifier: GPL-2.0-only
/*
 * Rockchip AMP support.
 *
 * Copyright (c) 2021 Rockchip Electronics Co. Ltd.
 * Author: Tony Xie <tony.xie@rock-chips.com>
 */

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/rockchip/rockchip_sip.h>

#define RK_CPU_STATUS_OFF		0
#define RK_CPU_STATUS_ON		1
#define RK_CPU_STATUS_BUSY		-1

enum amp_cpu_ctrl_status {
	AMP_CPU_STATUS_AMP_DIS = 0,
	AMP_CPU_STATUS_EN,
	AMP_CPU_STATUS_ON,
	AMP_CPU_STATUS_OFF,
};

#define AMP_FLAG_CPU_ARM64		BIT(1)
#define AMP_FLAG_CPU_EL2_HYP		BIT(2)
#define AMP_FLAG_CPU_ARM32_T		BIT(3)

struct rkamp_device {
	struct device *dev;
	struct clk_bulk_data *clks;
	int num_clks;
	struct device **pd_dev;
	int num_pds;
};

static struct {
	u32 en;
	u32 mode;
	u64 entry;
	u64 cpu_id;
} cpu_boot_info[CONFIG_NR_CPUS];

static int get_cpu_boot_info_idx(unsigned long cpu_id)
{
	int i;

	for (i = 0; i < CONFIG_NR_CPUS; i++) {
		if (cpu_boot_info[i].cpu_id == cpu_id)
			return i;
	}

	return -EINVAL;
}

static ssize_t boot_cpu_show(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	char *str = buf;

	str += sprintf(str, "cpu on/off:\n");
	str += sprintf(str,
		"         echo on/off [cpu id] > /sys/rk_amp/boot_cpu\n");
	str += sprintf(str, "get cpu on/off status:\n");
	str += sprintf(str,
		"         echo status [cpu id] > /sys/rk_amp/boot_cpu\n");
	if (str != buf)
		*(str - 1) = '\n';

	return (str - buf);
}

static void cpu_status_print(unsigned long cpu_id, struct arm_smccc_res *res)
{
	if (res->a0) {
		pr_info("get cpu-0x%lx status(%lx) error!\n", cpu_id, res->a0);
		return;
	}

	if (res->a1 == AMP_CPU_STATUS_AMP_DIS)
		pr_info("cpu-0x%lx amp is disable (%ld)\n", cpu_id, res->a1);
	else if (res->a1 == AMP_CPU_STATUS_EN)
		pr_info("cpu-0x%lx amp is enable (%ld)\n", cpu_id, res->a1);
	else if (res->a1 == AMP_CPU_STATUS_ON)
		pr_info("cpu-0x%lx amp: cpu is on (%ld)\n", cpu_id, res->a1);
	else if (res->a1 == AMP_CPU_STATUS_OFF)
		pr_info("cpu-0x%lx amp: cpu is off(%ld)\n", cpu_id, res->a1);
	else
		pr_info("cpu-0x%lx status(%ld) is error\n", cpu_id, res->a1);

	if (res->a2 == RK_CPU_STATUS_OFF)
		pr_info("cpu-0x%lx status(%ld) is off\n", cpu_id, res->a2);
	else if (res->a2 == RK_CPU_STATUS_ON)
		pr_info("cpu-0x%lx status(%ld) is on\n", cpu_id, res->a2);
	else if (res->a2 == RK_CPU_STATUS_BUSY)
		pr_info("cpu-0x%lx status(%ld) is busy\n", cpu_id, res->a2);
	else
		pr_info("cpu-0x%lx status(%ld) is error\n", cpu_id, res->a2);
}

static ssize_t boot_cpu_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf,
			      size_t count)
{
	char cmd[10];
	unsigned long cpu_id;
	struct arm_smccc_res res = {0};
	int ret, idx;

	ret = sscanf(buf, "%s", cmd);
	if (ret != 1) {
		pr_info("Use on/off [cpu id] or status [cpu id]\n");
		return -EINVAL;
	}

	if (!strncmp(cmd, "status", strlen("status"))) {
		ret = sscanf(buf, "%s %lx", cmd, &cpu_id);

		if (ret != 2)
			return -EINVAL;

		res = sip_smc_get_amp_info(RK_AMP_SUB_FUNC_GET_CPU_STATUS,
					   cpu_id);
		cpu_status_print(cpu_id, &res);
	} else if (!strncmp(cmd, "off", strlen("off"))) {
		ret = sscanf(buf, "%s %lx", cmd, &cpu_id);
		if (ret != 2)
			return -EINVAL;

		idx = get_cpu_boot_info_idx(cpu_id);

		if (idx >= 0 && cpu_boot_info[idx].en) {
			ret = sip_smc_amp_config(RK_AMP_SUB_FUNC_REQ_CPU_OFF,
						 cpu_id, 0, 0);
			if (ret)
				pr_info("requesting a cpu off is error(%d)!\n",
					ret);
		}
	} else if (!strncmp(cmd, "on", strlen("on"))) {
		ret = sscanf(buf, "%s %lx", cmd, &cpu_id);

		if (ret != 2)
			return -EINVAL;

		idx = get_cpu_boot_info_idx(cpu_id);
		if (idx >= 0 && cpu_boot_info[idx].en) {
			ret = sip_smc_amp_config(RK_AMP_SUB_FUNC_CPU_ON,
						 cpu_id,
						 cpu_boot_info[idx].entry,
						 0);
			if (ret)
				pr_info("booting up a cpu is error(%d)!\n",
					ret);
		}
	} else {
		pr_info("unsupported cmd(%s)\n", cmd);
	}

	return count;
}

static struct kobject *rk_amp_kobj;
static struct device_attribute rk_amp_attrs[] = {
	__ATTR(boot_cpu, 0664, boot_cpu_show, boot_cpu_store),
};

static int rockchip_amp_boot_cpus(struct device *dev,
				  struct device_node *cpu_node, int idx)
{
	u64 cpu_entry, cpu_id;
	u32 cpu_mode;
	int ret;

	if (idx >= CONFIG_NR_CPUS)
		return -1;

	if (of_property_read_u64_array(cpu_node, "entry", &cpu_entry, 1)) {
		dev_warn(dev, "can not get the entry\n");
		return -1;
	}

	if (!cpu_entry) {
		dev_warn(dev, "cpu-entry is 0\n");
		return -1;
	}

	if (of_property_read_u64_array(cpu_node, "id", &cpu_id, 1)) {
		dev_warn(dev, "can not get the cpu id\n");
		return -1;
	}

	if (of_property_read_u32_array(cpu_node, "mode", &cpu_mode, 1)) {
		dev_warn(dev, "can not get the cpu mode\n");
		return -1;
	}

	cpu_boot_info[idx].entry = cpu_entry;
	cpu_boot_info[idx].mode = cpu_mode;
	cpu_boot_info[idx].cpu_id = cpu_id;

	ret = sip_smc_amp_config(RK_AMP_SUB_FUNC_CFG_MODE, cpu_id, cpu_mode, 0);
	if (ret) {
		dev_warn(dev, "setting cpu mode is error(%d)!\n", ret);
		return ret;
	}

	ret = sip_smc_amp_config(RK_AMP_SUB_FUNC_CPU_ON, cpu_id, cpu_entry, 0);
	if (ret) {
		dev_warn(dev, "booting up a cpu is error(%d)!\n", ret);
		return ret;
	}

	cpu_boot_info[idx].en = 1;

	return 0;
}

static int rockchip_amp_probe(struct platform_device *pdev)
{
	struct rkamp_device *rkamp_dev = NULL;
	int ret, i, idx = 0;
	struct device_node *cpus_node, *cpu_node;

	rkamp_dev = devm_kzalloc(&pdev->dev, sizeof(*rkamp_dev), GFP_KERNEL);
	if (!rkamp_dev)
		return -ENOMEM;

	rkamp_dev->num_clks = devm_clk_bulk_get_all(&pdev->dev, &rkamp_dev->clks);
	if (rkamp_dev->num_clks < 0)
		return -ENODEV;
	ret = clk_bulk_prepare_enable(rkamp_dev->num_clks, rkamp_dev->clks);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "failed to prepare enable clks: %d\n", ret);

	pm_runtime_enable(&pdev->dev);

	rkamp_dev->num_pds = of_count_phandle_with_args(pdev->dev.of_node, "power-domains",
							"#power-domain-cells");

	if (rkamp_dev->num_pds > 0) {
		rkamp_dev->pd_dev = devm_kmalloc_array(&pdev->dev, rkamp_dev->num_pds,
						       sizeof(*rkamp_dev->pd_dev), GFP_KERNEL);
		if (!rkamp_dev->pd_dev)
			return -ENOMEM;

		if (rkamp_dev->num_pds == 1) {
			ret = pm_runtime_resume_and_get(&pdev->dev);
			if (ret < 0)
				return dev_err_probe(&pdev->dev, ret,
						     "failed to get power-domain\n");
		} else {
			for (i = 0; i < rkamp_dev->num_pds; i++) {
				rkamp_dev->pd_dev[i] = dev_pm_domain_attach_by_id(&pdev->dev, i);
				ret = pm_runtime_resume_and_get(rkamp_dev->pd_dev[i]);
				if (ret < 0)
					return dev_err_probe(&pdev->dev, ret,
							     "failed to get pd_dev[%d]\n", i);
			}
		}
	}

	cpus_node = of_get_child_by_name(pdev->dev.of_node, "amp-cpus");

	if (cpus_node) {
		for_each_available_child_of_node(cpus_node, cpu_node) {
			if (!rockchip_amp_boot_cpus(&pdev->dev, cpu_node,
						    idx)) {
				idx++;
			}
		}
	}

	rk_amp_kobj = kobject_create_and_add("rk_amp", NULL);
	if (!rk_amp_kobj)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(rk_amp_attrs); i++) {
		ret = sysfs_create_file(rk_amp_kobj, &rk_amp_attrs[i].attr);
		if (ret)
			return dev_err_probe(&pdev->dev, ret, "create file index %d error\n", i);
	}

	return 0;
}

static int rockchip_amp_remove(struct platform_device *pdev)
{
	int i;
	struct rkamp_device *rkamp_dev = platform_get_drvdata(pdev);

	clk_bulk_disable_unprepare(rkamp_dev->num_clks, rkamp_dev->clks);

	if (rkamp_dev->num_pds == 1) {
		pm_runtime_put_sync(&pdev->dev);
	} else if (rkamp_dev->num_pds > 1) {
		for (i = 0; i < rkamp_dev->num_pds; i++) {
			pm_runtime_put_sync(rkamp_dev->pd_dev[i]);
			dev_pm_domain_detach(rkamp_dev->pd_dev[i], true);
			rkamp_dev->pd_dev[i] = NULL;
		}
	}

	pm_runtime_disable(&pdev->dev);

	for (i = 0; i < ARRAY_SIZE(rk_amp_attrs); i++)
		sysfs_remove_file(rk_amp_kobj, &rk_amp_attrs[i].attr);

	kobject_put(rk_amp_kobj);

	return 0;
}

static const struct of_device_id rockchip_amp_match[] = {
	{
		.compatible = "rockchip,amp",
	},
	{ /* sentinel */ },
};

MODULE_DEVICE_TABLE(of, rockchip_amp_match);

static struct platform_driver rockchip_amp_driver = {
	.probe = rockchip_amp_probe,
	.remove = rockchip_amp_remove,
	.driver = {
		.name  = "rockchip-amp",
		.of_match_table = rockchip_amp_match,
	},
};
module_platform_driver(rockchip_amp_driver);

MODULE_DESCRIPTION("Rockchip AMP driver");
MODULE_AUTHOR("Tony xie<tony.xie@rock-chips.com>");
MODULE_LICENSE("GPL");
