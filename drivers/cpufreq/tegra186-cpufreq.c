// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017, NVIDIA CORPORATION. All rights reserved
 */

#include <linux/cpufreq.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/units.h>

#include <soc/tegra/bpmp.h>
#include <soc/tegra/bpmp-abi.h>

#define TEGRA186_NUM_CLUSTERS		2
#define EDVD_OFFSET_A57(core)		((SZ_64K * 6) + (0x20 + (core) * 0x4))
#define EDVD_OFFSET_DENVER(core)	((SZ_64K * 7) + (0x20 + (core) * 0x4))
#define EDVD_CORE_VOLT_FREQ_F_SHIFT	0
#define EDVD_CORE_VOLT_FREQ_F_MASK	0xffff
#define EDVD_CORE_VOLT_FREQ_V_SHIFT	16

struct tegra186_cpufreq_cpu {
	unsigned int bpmp_cluster_id;
	unsigned int edvd_offset;
};

static const struct tegra186_cpufreq_cpu tegra186_cpus[] = {
	/* CPU0 - A57 Cluster */
	{
		.bpmp_cluster_id = 1,
		.edvd_offset = EDVD_OFFSET_A57(0)
	},
	/* CPU1 - Denver Cluster */
	{
		.bpmp_cluster_id = 0,
		.edvd_offset = EDVD_OFFSET_DENVER(0)
	},
	/* CPU2 - Denver Cluster */
	{
		.bpmp_cluster_id = 0,
		.edvd_offset = EDVD_OFFSET_DENVER(1)
	},
	/* CPU3 - A57 Cluster */
	{
		.bpmp_cluster_id = 1,
		.edvd_offset = EDVD_OFFSET_A57(1)
	},
	/* CPU4 - A57 Cluster */
	{
		.bpmp_cluster_id = 1,
		.edvd_offset = EDVD_OFFSET_A57(2)
	},
	/* CPU5 - A57 Cluster */
	{
		.bpmp_cluster_id = 1,
		.edvd_offset = EDVD_OFFSET_A57(3)
	},
};

struct tegra186_cpufreq_cluster {
	struct cpufreq_frequency_table *bpmp_lut;
	u32 ref_clk_khz;
	u32 div;
};

struct tegra186_cpufreq_data {
	void __iomem *regs;
	const struct tegra186_cpufreq_cpu *cpus;
	bool icc_dram_bw_scaling;
	struct tegra186_cpufreq_cluster clusters[];
};

static int tegra_cpufreq_set_bw(struct cpufreq_policy *policy, unsigned long freq_khz)
{
	struct tegra186_cpufreq_data *data = cpufreq_get_driver_data();
	struct device *dev;
	int ret;

	dev = get_cpu_device(policy->cpu);
	if (!dev)
		return -ENODEV;

	struct dev_pm_opp *opp __free(put_opp) =
		dev_pm_opp_find_freq_exact(dev, freq_khz * HZ_PER_KHZ, true);
	if (IS_ERR(opp))
		return PTR_ERR(opp);

	ret = dev_pm_opp_set_opp(dev, opp);
	if (ret)
		data->icc_dram_bw_scaling = false;

	return ret;
}

static int tegra_cpufreq_init_cpufreq_table(struct cpufreq_policy *policy,
					    struct cpufreq_frequency_table *bpmp_lut,
					    struct cpufreq_frequency_table **opp_table)
{
	struct tegra186_cpufreq_data *data = cpufreq_get_driver_data();
	struct cpufreq_frequency_table *freq_table = NULL;
	struct cpufreq_frequency_table *pos;
	struct device *cpu_dev;
	unsigned long rate;
	int ret, max_opps;
	int j = 0;

	cpu_dev = get_cpu_device(policy->cpu);
	if (!cpu_dev) {
		pr_err("%s: failed to get cpu%d device\n", __func__, policy->cpu);
		return -ENODEV;
	}

	/* Initialize OPP table mentioned in operating-points-v2 property in DT */
	ret = dev_pm_opp_of_add_table_indexed(cpu_dev, 0);
	if (ret) {
		dev_err(cpu_dev, "Invalid or empty opp table in device tree\n");
		data->icc_dram_bw_scaling = false;
		return ret;
	}

	max_opps = dev_pm_opp_get_opp_count(cpu_dev);
	if (max_opps <= 0) {
		dev_err(cpu_dev, "Failed to add OPPs\n");
		return max_opps;
	}

	/* Disable all opps and cross-validate against LUT later */
	for (rate = 0; ; rate++) {
		struct dev_pm_opp *opp __free(put_opp) =
			dev_pm_opp_find_freq_ceil(cpu_dev, &rate);
		if (IS_ERR(opp))
			break;

		dev_pm_opp_disable(cpu_dev, rate);
	}

	freq_table = kcalloc((max_opps + 1), sizeof(*freq_table), GFP_KERNEL);
	if (!freq_table)
		return -ENOMEM;

	/*
	 * Cross check the frequencies from BPMP-FW LUT against the OPP's present in DT.
	 * Enable only those DT OPP's which are present in LUT also.
	 */
	cpufreq_for_each_valid_entry(pos, bpmp_lut) {
		struct dev_pm_opp *opp __free(put_opp) =
			dev_pm_opp_find_freq_exact(cpu_dev, pos->frequency * HZ_PER_KHZ, false);
		if (IS_ERR(opp))
			continue;

		ret = dev_pm_opp_enable(cpu_dev, pos->frequency * HZ_PER_KHZ);
		if (ret < 0)
			return ret;

		freq_table[j].driver_data = pos->driver_data;
		freq_table[j].frequency = pos->frequency;
		j++;
	}

	freq_table[j].driver_data = pos->driver_data;
	freq_table[j].frequency = CPUFREQ_TABLE_END;

	*opp_table = &freq_table[0];

	dev_pm_opp_set_sharing_cpus(cpu_dev, policy->cpus);

	/* Prime interconnect data */
	tegra_cpufreq_set_bw(policy, freq_table[j - 1].frequency);

	return ret;
}

static int tegra186_cpufreq_init(struct cpufreq_policy *policy)
{
	struct tegra186_cpufreq_data *data = cpufreq_get_driver_data();
	unsigned int cluster = data->cpus[policy->cpu].bpmp_cluster_id;
	struct cpufreq_frequency_table *freq_table;
	struct cpufreq_frequency_table *bpmp_lut;
	u32 cpu;
	int ret;

	policy->cpuinfo.transition_latency = 300 * 1000;
	policy->driver_data = NULL;

	/* set same policy for all cpus in a cluster */
	for (cpu = 0; cpu < ARRAY_SIZE(tegra186_cpus); cpu++) {
		if (data->cpus[cpu].bpmp_cluster_id == cluster)
			cpumask_set_cpu(cpu, policy->cpus);
	}

	bpmp_lut = data->clusters[cluster].bpmp_lut;

	if (data->icc_dram_bw_scaling) {
		ret = tegra_cpufreq_init_cpufreq_table(policy, bpmp_lut, &freq_table);
		if (!ret) {
			policy->freq_table = freq_table;
			return 0;
		}
	}

	data->icc_dram_bw_scaling = false;
	policy->freq_table = bpmp_lut;
	pr_info("OPP tables missing from DT, EMC frequency scaling disabled\n");

	return 0;
}

static int tegra186_cpufreq_set_target(struct cpufreq_policy *policy,
				       unsigned int index)
{
	struct tegra186_cpufreq_data *data = cpufreq_get_driver_data();
	struct cpufreq_frequency_table *tbl = policy->freq_table + index;
	unsigned int edvd_offset;
	u32 edvd_val = tbl->driver_data;
	u32 cpu;

	for_each_cpu(cpu, policy->cpus) {
		edvd_offset = data->cpus[cpu].edvd_offset;
		writel(edvd_val, data->regs + edvd_offset);
	}

	if (data->icc_dram_bw_scaling)
		tegra_cpufreq_set_bw(policy, tbl->frequency);


	return 0;
}

static unsigned int tegra186_cpufreq_get(unsigned int cpu)
{
	struct cpufreq_policy *policy __free(put_cpufreq_policy) = cpufreq_cpu_get(cpu);
	struct tegra186_cpufreq_data *data = cpufreq_get_driver_data();
	struct tegra186_cpufreq_cluster *cluster;
	unsigned int edvd_offset, cluster_id;
	u32 ndiv;

	if (!policy)
		return 0;

	edvd_offset = data->cpus[policy->cpu].edvd_offset;
	ndiv = readl(data->regs + edvd_offset) & EDVD_CORE_VOLT_FREQ_F_MASK;
	cluster_id = data->cpus[policy->cpu].bpmp_cluster_id;
	cluster = &data->clusters[cluster_id];

	return (cluster->ref_clk_khz * ndiv) / cluster->div;
}

static struct cpufreq_driver tegra186_cpufreq_driver = {
	.name = "tegra186",
	.flags = CPUFREQ_HAVE_GOVERNOR_PER_POLICY |
			CPUFREQ_NEED_INITIAL_FREQ_CHECK,
	.get = tegra186_cpufreq_get,
	.verify = cpufreq_generic_frequency_table_verify,
	.target_index = tegra186_cpufreq_set_target,
	.init = tegra186_cpufreq_init,
};

static struct cpufreq_frequency_table *tegra_cpufreq_bpmp_read_lut(
	struct platform_device *pdev, struct tegra_bpmp *bpmp,
	struct tegra186_cpufreq_cluster *cluster, unsigned int cluster_id,
	int *num_rates)
{
	struct cpufreq_frequency_table *table;
	struct mrq_cpu_vhint_request req;
	struct tegra_bpmp_message msg;
	struct cpu_vhint_data *data;
	int err, i, j;
	dma_addr_t phys;
	void *virt;

	virt = dma_alloc_coherent(bpmp->dev, sizeof(*data), &phys,
				  GFP_KERNEL);
	if (!virt)
		return ERR_PTR(-ENOMEM);

	data = (struct cpu_vhint_data *)virt;

	memset(&req, 0, sizeof(req));
	req.addr = phys;
	req.cluster_id = cluster_id;

	memset(&msg, 0, sizeof(msg));
	msg.mrq = MRQ_CPU_VHINT;
	msg.tx.data = &req;
	msg.tx.size = sizeof(req);

	err = tegra_bpmp_transfer(bpmp, &msg);
	if (err) {
		table = ERR_PTR(err);
		goto free;
	}
	if (msg.rx.ret) {
		table = ERR_PTR(-EINVAL);
		goto free;
	}

	*num_rates = 0;
	for (i = data->vfloor; i <= data->vceil; i++) {
		u16 ndiv = data->ndiv[i];

		if (ndiv < data->ndiv_min || ndiv > data->ndiv_max)
			continue;

		/* Only store lowest voltage index for each rate */
		if (i > 0 && ndiv == data->ndiv[i - 1])
			continue;

		(*num_rates)++;
	}

	table = devm_kcalloc(&pdev->dev, *num_rates + 1, sizeof(*table),
			     GFP_KERNEL);
	if (!table) {
		table = ERR_PTR(-ENOMEM);
		goto free;
	}

	cluster->ref_clk_khz = data->ref_clk_hz / 1000;
	cluster->div = data->pdiv * data->mdiv;

	for (i = data->vfloor, j = 0; i <= data->vceil; i++) {
		struct cpufreq_frequency_table *point;
		u16 ndiv = data->ndiv[i];
		u32 edvd_val = 0;

		if (ndiv < data->ndiv_min || ndiv > data->ndiv_max)
			continue;

		/* Only store lowest voltage index for each rate */
		if (i > 0 && ndiv == data->ndiv[i - 1])
			continue;

		edvd_val |= i << EDVD_CORE_VOLT_FREQ_V_SHIFT;
		edvd_val |= ndiv << EDVD_CORE_VOLT_FREQ_F_SHIFT;

		point = &table[j++];
		point->driver_data = edvd_val;
		point->frequency = (cluster->ref_clk_khz * ndiv) / cluster->div;
	}

	table[j].frequency = CPUFREQ_TABLE_END;

free:
	dma_free_coherent(bpmp->dev, sizeof(*data), virt, phys);

	return table;
}

static int tegra186_cpufreq_probe(struct platform_device *pdev)
{
	struct tegra186_cpufreq_data *data;
	struct tegra_bpmp *bpmp;
	struct device *cpu_dev;
	unsigned int i = 0, err, edvd_offset;
	int num_rates = 0;
	u32 edvd_val, cpu;

	data = devm_kzalloc(&pdev->dev,
			    struct_size(data, clusters, TEGRA186_NUM_CLUSTERS),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->cpus = tegra186_cpus;

	bpmp = tegra_bpmp_get(&pdev->dev);
	if (IS_ERR(bpmp))
		return PTR_ERR(bpmp);

	data->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(data->regs)) {
		err = PTR_ERR(data->regs);
		goto put_bpmp;
	}

	for (i = 0; i < TEGRA186_NUM_CLUSTERS; i++) {
		struct tegra186_cpufreq_cluster *cluster = &data->clusters[i];

		cluster->bpmp_lut = tegra_cpufreq_bpmp_read_lut(pdev, bpmp, cluster, i, &num_rates);
		if (IS_ERR(cluster->bpmp_lut)) {
			err = PTR_ERR(cluster->bpmp_lut);
			goto put_bpmp;
		} else if (!num_rates) {
			err = -EINVAL;
			goto put_bpmp;
		}

		for (cpu = 0; cpu < ARRAY_SIZE(tegra186_cpus); cpu++) {
			if (data->cpus[cpu].bpmp_cluster_id == i) {
				edvd_val = cluster->bpmp_lut[num_rates - 1].driver_data;
				edvd_offset = data->cpus[cpu].edvd_offset;
				writel(edvd_val, data->regs + edvd_offset);
			}
		}
	}

	tegra186_cpufreq_driver.driver_data = data;

	/* Check for optional OPPv2 and interconnect paths on CPU0 to enable ICC scaling */
	cpu_dev = get_cpu_device(0);
	if (!cpu_dev) {
		err = -EPROBE_DEFER;
		goto put_bpmp;
	}

	if (dev_pm_opp_of_get_opp_desc_node(cpu_dev)) {
		err = dev_pm_opp_of_find_icc_paths(cpu_dev, NULL);
		if (!err)
			data->icc_dram_bw_scaling = true;
	}

	err = cpufreq_register_driver(&tegra186_cpufreq_driver);

put_bpmp:
	tegra_bpmp_put(bpmp);

	return err;
}

static void tegra186_cpufreq_remove(struct platform_device *pdev)
{
	cpufreq_unregister_driver(&tegra186_cpufreq_driver);
}

static const struct of_device_id tegra186_cpufreq_of_match[] = {
	{ .compatible = "nvidia,tegra186-ccplex-cluster", },
	{ }
};
MODULE_DEVICE_TABLE(of, tegra186_cpufreq_of_match);

static struct platform_driver tegra186_cpufreq_platform_driver = {
	.driver = {
		.name = "tegra186-cpufreq",
		.of_match_table = tegra186_cpufreq_of_match,
	},
	.probe = tegra186_cpufreq_probe,
	.remove = tegra186_cpufreq_remove,
};
module_platform_driver(tegra186_cpufreq_platform_driver);

MODULE_AUTHOR("Mikko Perttunen <mperttunen@nvidia.com>");
MODULE_DESCRIPTION("NVIDIA Tegra186 cpufreq driver");
MODULE_LICENSE("GPL v2");
