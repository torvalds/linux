// SPDX-License-Identifier: GPL-2.0-only
/*
 * Rockchip AMP support.
 *
 * Copyright (c) 2021 Rockchip Electronics Co. Ltd.
 * Author: Tony Xie <tony.xie@rock-chips.com>
 */

#include <asm/cputype.h>
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/rockchip/rockchip_sip.h>
#include <soc/rockchip/rockchip_amp.h>

#define RK_CPU_STATUS_OFF		0
#define RK_CPU_STATUS_ON		1
#define RK_CPU_STATUS_BUSY		-1
#define AMP_AFF_MAX_CLUSTER		4
#define AMP_AFF_MAX_CPU			8
#define GPIO_BANK_NUM			16
#define GPIO_GROUP_PRIO_MAX		3

#define AMP_GIC_DBG(fmt, arg...)	do { if (0) { pr_warn(fmt, ##arg); } } while (0)

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

struct amp_gpio_group_s {
	u32 bank_id;
	u32 prio;
	u32 irq_aff[AMP_AFF_MAX_CPU];
	u32 irq_id[AMP_AFF_MAX_CPU];
	u32 en[AMP_AFF_MAX_CPU];
};

struct amp_irq_cfg_s {
	u32 prio;
	u32 cpumask;
	u32 aff;
	int amp_flag;
} irqs_cfg[1024];

static struct amp_gic_ctrl_s {
	struct {
		u32 aff;
		u32 cpumask;
		u32 flag;
	} aff_to_cpumask[AMP_AFF_MAX_CLUSTER][AMP_AFF_MAX_CPU];
	struct amp_irq_cfg_s irqs_cfg[1024];
	u32 validmask[1020 / 32 + 1];
	struct amp_gpio_group_s gpio_grp[GPIO_BANK_NUM][GPIO_GROUP_PRIO_MAX];
	u32 gpio_banks;
} amp_ctrl;

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
		pr_info("failed to get cpu[%lx] status, ret=%lx!\n", cpu_id, res->a0);
		return;
	}

	if (res->a1 == AMP_CPU_STATUS_AMP_DIS)
		pr_info("cpu[%lx] amp is disabled (%ld)\n", cpu_id, res->a1);
	else if (res->a1 == AMP_CPU_STATUS_EN)
		pr_info("cpu[%lx] amp is enabled (%ld)\n", cpu_id, res->a1);
	else if (res->a1 == AMP_CPU_STATUS_ON)
		pr_info("cpu[%lx] amp: cpu is on (%ld)\n", cpu_id, res->a1);
	else if (res->a1 == AMP_CPU_STATUS_OFF)
		pr_info("cpu[%lx] amp: cpu is off(%ld)\n", cpu_id, res->a1);
	else
		pr_info("cpu[%lx] amp status(%ld) is error\n", cpu_id, res->a1);

	if (res->a2 == RK_CPU_STATUS_OFF)
		pr_info("cpu[%lx] status(%ld) is off\n", cpu_id, res->a2);
	else if (res->a2 == RK_CPU_STATUS_ON)
		pr_info("cpu[%lx] status(%ld) is on\n", cpu_id, res->a2);
	else if (res->a2 == RK_CPU_STATUS_BUSY)
		pr_info("cpu[%lx] status(%ld) is busy\n", cpu_id, res->a2);
	else
		pr_info("cpu[%lx] status(%ld) is error\n", cpu_id, res->a2);
}

static ssize_t boot_cpu_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf,
			      size_t count)
{
	struct arm_smccc_res res = {0};
	unsigned long cpu_id;
	char cmd[10];
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

		res = sip_smc_get_amp_info(RK_AMP_SUB_FUNC_GET_CPU_STATUS, cpu_id);
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
				dev_warn(dev, "failed to request cpu[%lx] off, ret=%d!\n", cpu_id, ret);
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
				dev_warn(dev, "Brought up cpu[%lx] failed, ret=%d\n", cpu_id, ret);
			else
				pr_info("Brought up cpu[%lx] ok.\n", cpu_id);
		} else {
			dev_warn(dev, "cpu[%lx] is unavailable\n", cpu_id);
		}
	} else {
		dev_warn(dev, "unsupported cmd(%s)\n", cmd);
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
	u32 cpu_mode, boot_on;
	int ret;

	if (idx >= CONFIG_NR_CPUS)
		return -1;

	if (of_property_read_u64_array(cpu_node, "id", &cpu_id, 1)) {
		dev_warn(dev, "failed to get 'id'\n");
		return -1;
	}

	if (of_property_read_u64_array(cpu_node, "entry", &cpu_entry, 1)) {
		dev_warn(dev, "failed to get cpu[%llx] 'entry'\n", cpu_id);
		return -1;
	}

	if (!cpu_entry) {
		dev_warn(dev, "invalid cpu[%llx] 'entry': 0\n", cpu_id);
		return -1;
	}

	if (of_property_read_u32_array(cpu_node, "mode", &cpu_mode, 1)) {
		dev_warn(dev, "failed to get cpu[%llx] 'mode'\n", cpu_id);
		return -1;
	}

	if (of_property_read_u32_array(cpu_node, "boot-on", &boot_on, 1))
		boot_on = 1; /* compatible old action */

	cpu_boot_info[idx].entry = cpu_entry;
	cpu_boot_info[idx].mode = cpu_mode;
	cpu_boot_info[idx].cpu_id = cpu_id;

	ret = sip_smc_amp_config(RK_AMP_SUB_FUNC_CFG_MODE, cpu_id, cpu_mode, 0);
	if (ret) {
		dev_warn(dev, "failed to set cpu mode, ret=%d\n", ret);
		return ret;
	}

	if (boot_on) {
		ret = sip_smc_amp_config(RK_AMP_SUB_FUNC_CPU_ON, cpu_id, cpu_entry, 0);
		if (ret) {
			dev_warn(dev, "Brought up cpu[%llx] failed, ret=%d\n", cpu_id, ret);
			return ret;
		} else {
			pr_info("Brought up cpu[%llx] ok.\n", cpu_id);
		}
	}

	cpu_boot_info[idx].en = 1;

	return 0;
}

int rockchip_amp_check_amp_irq(u32 irq)
{
	return amp_ctrl.irqs_cfg[irq].amp_flag;
}

u32 rockchip_amp_get_irq_prio(u32 irq)
{
	return amp_ctrl.irqs_cfg[irq].prio;
}

u32 rockchip_amp_get_irq_cpumask(u32 irq)
{
	return amp_ctrl.irqs_cfg[irq].cpumask;
}

static u32 amp_get_cpumask_bit(u32 aff)
{
	u32 aff_cluster, aff_cpu;

	aff_cluster = MPIDR_AFFINITY_LEVEL(aff, 1);
	aff_cpu = MPIDR_AFFINITY_LEVEL(aff, 0);

	if (aff_cpu >= AMP_AFF_MAX_CPU || aff_cluster >= AMP_AFF_MAX_CLUSTER)
		return 0;

	AMP_GIC_DBG("%s: aff:%d-%d: %x\n", __func__, aff_cluster, aff_cpu,
		    amp_ctrl.aff_to_cpumask[aff_cluster][aff_cpu].cpumask);

	return amp_ctrl.aff_to_cpumask[aff_cluster][aff_cpu].cpumask;
}

static int gic_amp_get_gpio_prio_group_info(struct device_node *np,
					    struct amp_gic_ctrl_s *amp_ctrl,
					    int prio_id)
{
	u32 gpio_bank, count0, count1, prio, irq_id, irq_aff;
	int i;
	struct amp_gpio_group_s *gpio_grp;
	struct amp_irq_cfg_s *irqs_cfg;

	if (prio_id >= GPIO_GROUP_PRIO_MAX)
		return -EINVAL;

	if (of_property_read_u32_array(np, "gpio-bank", &gpio_bank, 1))
		return -EINVAL;
	if (gpio_bank >= amp_ctrl->gpio_banks)
		return -EINVAL;

	gpio_grp = &amp_ctrl->gpio_grp[gpio_bank][prio_id];

	if (of_property_read_u32_array(np, "prio", &prio, 1))
		return -EINVAL;

	if (gpio_bank >= GPIO_BANK_NUM)
		return -EINVAL;

	AMP_GIC_DBG("%s: gpio-%d, group prio:%d-%x\n",
		    __func__, gpio_bank, prio_id, prio);

	count0 = of_property_count_u32_elems(np, "girq-id");
	count1 = of_property_count_u32_elems(np, "girq-aff");

	if (count0 != count1)
		return -EINVAL;

	gpio_grp->prio = prio;

	for (i = 0; i < count0; i++) {
		of_property_read_u32_index(np, "girq-id", i, &irq_id);
		gpio_grp->irq_id[i] = irq_id;
		of_property_read_u32_index(np, "girq-aff", i, &irq_aff);

		gpio_grp->irq_aff[i] = irq_aff;

		of_property_read_u32_index(np, "girq-en", i, &gpio_grp->en[i]);

		irqs_cfg = &amp_ctrl->irqs_cfg[irq_id];

		AMP_GIC_DBG(" %s: group cpu-%d, irq-%d: prio-%x, aff-%x en-%d\n",
			    __func__, i, gpio_grp->irq_id[i], gpio_grp->prio,
			    gpio_grp->irq_aff[i], gpio_grp->en[i]);

		if (gpio_grp->en[i]) {
			irqs_cfg->prio = gpio_grp->prio;
			irqs_cfg->aff = irq_aff;
			irqs_cfg->cpumask = amp_get_cpumask_bit(irq_aff);
			irqs_cfg->amp_flag = 1;
		}

		AMP_GIC_DBG("  %s: irqs_cfg prio-%x aff-%x cpumaks-%x en-%d\n",
			    __func__, irqs_cfg->prio, irqs_cfg->aff,
			    irqs_cfg->cpumask, irqs_cfg->amp_flag);
	}

	return 0;
}

static int gic_amp_gpio_group_get_info(struct device_node *group_node,
				       struct amp_gic_ctrl_s *amp_ctrl,
				       int idx)
{
	int i = 0;
	struct device_node *node;

	if (group_node) {
		for_each_available_child_of_node(group_node, node) {
			if (i >= GPIO_GROUP_PRIO_MAX)
				break;
			if (!gic_amp_get_gpio_prio_group_info(node, amp_ctrl,
							      i)) {
				i++;
			}
		}
	}
	return 0;
}

static void gic_of_get_gpio_group(struct device_node *np,
				  struct amp_gic_ctrl_s *amp_ctrl)
{
	struct device_node *gpio_group_node, *node;
	int i = 0;

	if (of_property_read_u32_array(np, "gpio-group-banks",
				       &amp_ctrl->gpio_banks, 1))
		return;

	gpio_group_node = of_get_child_by_name(np, "gpio-group");
	if (gpio_group_node) {
		for_each_available_child_of_node(gpio_group_node, node) {
			if (i >= amp_ctrl->gpio_banks)
				break;
			if (!gic_amp_gpio_group_get_info(node, amp_ctrl, i))
				i++;
		}
	}

	of_node_put(gpio_group_node);
}

static int amp_gic_get_cpumask(struct device_node *np, struct amp_gic_ctrl_s *amp_ctrl)
{
	const struct property *prop;
	int count, i;
	u32 cluster, aff_cpu, aff, cpumask;

	prop = of_find_property(np, "amp-cpu-aff-maskbits", NULL);
	if (!prop)
		return -1;

	if (!prop->value)
		return -1;

	count = of_property_count_u32_elems(np, "amp-cpu-aff-maskbits");
	if (count % 2)
		return -1;

	for (i = 0; i < count / 2; i++) {
		of_property_read_u32_index(np, "amp-cpu-aff-maskbits",
					   2 * i, &aff);
		cluster = MPIDR_AFFINITY_LEVEL(aff, 1);
		aff_cpu = MPIDR_AFFINITY_LEVEL(aff, 0);
		amp_ctrl->aff_to_cpumask[cluster][aff_cpu].aff = aff;

		of_property_read_u32_index(np, "amp-cpu-aff-maskbits",
					   2 * i + 1, &cpumask);

		amp_ctrl->aff_to_cpumask[cluster][aff_cpu].cpumask = cpumask;

		AMP_GIC_DBG("cpumask: %d-%d: aff-%d cpumask-%d\n",
			    cluster, aff_cpu, aff, cpumask);

		if (!cpumask)
			return -1;
	}

	return 0;
}

static void amp_gic_get_irqs_config(struct device_node *np,
				    struct amp_gic_ctrl_s *amp_ctrl)
{
	const struct property *prop;
	int count, i;
	u32 irq, prio, aff;

	prop = of_find_property(np, "amp-irqs", NULL);
	if (!prop)
		return;

	if (!prop->value)
		return;

	count = of_property_count_u32_elems(np, "amp-irqs");

	if (count < 0 || count % 3)
		return;

	for (i = 0; i < count / 3; i++) {
		of_property_read_u32_index(np, "amp-irqs", 3 * i, &irq);

		if (irq > 1020)
			break;

		of_property_read_u32_index(np, "amp-irqs", 3 * i + 1, &prio);
		of_property_read_u32_index(np, "amp-irqs", 3 * i + 2, &aff);

		AMP_GIC_DBG("%s: irq-%d aff-%d prio-%x\n",
			    __func__, irq, aff, prio);

		amp_ctrl->irqs_cfg[irq].prio = prio;
		amp_ctrl->irqs_cfg[irq].aff = aff;
		amp_ctrl->irqs_cfg[irq].cpumask = amp_get_cpumask_bit(aff);

		if (!amp_ctrl->irqs_cfg[irq].cpumask) {
			AMP_GIC_DBG("%s: get cpumask error\n", __func__);
			break;
		}

		if (!amp_ctrl->irqs_cfg[irq].aff &&
		    !amp_ctrl->irqs_cfg[irq].prio)
			break;

		amp_ctrl->irqs_cfg[irq].amp_flag = 1;

		AMP_GIC_DBG("%s: irq-%d aff-%d cpumask-%d pri-%x\n",
			    __func__, irq, amp_ctrl->irqs_cfg[irq].aff,
			    amp_ctrl->irqs_cfg[irq].cpumask,
			    amp_ctrl->irqs_cfg[irq].prio);
	}
}

void rockchip_amp_get_gic_info(void)
{
	struct device_node *np;

	np = of_find_node_by_name(NULL, "rockchip-amp");
	if (!np)
		return;

	if (amp_gic_get_cpumask(np, &amp_ctrl)) {
		pr_err("%s: get amp gic cpu mask error\n", __func__);
		goto exit;
	}
	gic_of_get_gpio_group(np, &amp_ctrl);
	amp_gic_get_irqs_config(np, &amp_ctrl);

exit:
	of_node_put(np);
}

static int rockchip_amp_probe(struct platform_device *pdev)
{
	struct device_node *cpus_node, *cpu_node;
	struct rkamp_device *rkamp_dev;
	int ret, i, idx = 0;

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

	rkamp_dev->num_pds =
		of_count_phandle_with_args(pdev->dev.of_node, "power-domains",
					   "#power-domain-cells");
	if (rkamp_dev->num_pds > 0) {
		rkamp_dev->pd_dev =
			devm_kmalloc_array(&pdev->dev, rkamp_dev->num_pds,
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
			if (!rockchip_amp_boot_cpus(&pdev->dev, cpu_node, idx))
				idx++;
		}
		of_node_put(cpus_node);
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
	struct rkamp_device *rkamp_dev = platform_get_drvdata(pdev);
	int i;

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
	{ .compatible = "rockchip,amp" },
	{ .compatible = "rockchip,mcu-amp" },
	{ .compatible = "rockchip,rk3568-amp" },
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
