// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017, NVIDIA CORPORATION. All rights reserved
 */

#include <linux/cpufreq.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

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
	struct cpufreq_frequency_table *table;
	u32 ref_clk_khz;
	u32 div;
};

struct tegra186_cpufreq_data {
	void __iomem *regs;
	const struct tegra186_cpufreq_cpu *cpus;
	struct tegra186_cpufreq_cluster clusters[];
};

static int tegra186_cpufreq_init(struct cpufreq_policy *policy)
{
	struct tegra186_cpufreq_data *data = cpufreq_get_driver_data();
	unsigned int cluster = data->cpus[policy->cpu].bpmp_cluster_id;

	policy->freq_table = data->clusters[cluster].table;
	policy->cpuinfo.transition_latency = 300 * 1000;
	policy->driver_data = NULL;

	return 0;
}

static int tegra186_cpufreq_set_target(struct cpufreq_policy *policy,
				       unsigned int index)
{
	struct tegra186_cpufreq_data *data = cpufreq_get_driver_data();
	struct cpufreq_frequency_table *tbl = policy->freq_table + index;
	unsigned int edvd_offset = data->cpus[policy->cpu].edvd_offset;
	u32 edvd_val = tbl->driver_data;

	writel(edvd_val, data->regs + edvd_offset);

	return 0;
}

static unsigned int tegra186_cpufreq_get(unsigned int cpu)
{
	struct tegra186_cpufreq_data *data = cpufreq_get_driver_data();
	struct tegra186_cpufreq_cluster *cluster;
	struct cpufreq_policy *policy;
	unsigned int edvd_offset, cluster_id;
	u32 ndiv;

	policy = cpufreq_cpu_get(cpu);
	if (!policy)
		return 0;

	edvd_offset = data->cpus[policy->cpu].edvd_offset;
	ndiv = readl(data->regs + edvd_offset) & EDVD_CORE_VOLT_FREQ_F_MASK;
	cluster_id = data->cpus[policy->cpu].bpmp_cluster_id;
	cluster = &data->clusters[cluster_id];
	cpufreq_cpu_put(policy);

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
	.attr = cpufreq_generic_attr,
};

static struct cpufreq_frequency_table *init_vhint_table(
	struct platform_device *pdev, struct tegra_bpmp *bpmp,
	struct tegra186_cpufreq_cluster *cluster, unsigned int cluster_id)
{
	struct cpufreq_frequency_table *table;
	struct mrq_cpu_vhint_request req;
	struct tegra_bpmp_message msg;
	struct cpu_vhint_data *data;
	int err, i, j, num_rates = 0;
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

	for (i = data->vfloor; i <= data->vceil; i++) {
		u16 ndiv = data->ndiv[i];

		if (ndiv < data->ndiv_min || ndiv > data->ndiv_max)
			continue;

		/* Only store lowest voltage index for each rate */
		if (i > 0 && ndiv == data->ndiv[i - 1])
			continue;

		num_rates++;
	}

	table = devm_kcalloc(&pdev->dev, num_rates + 1, sizeof(*table),
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
	unsigned int i = 0, err;

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

		cluster->table = init_vhint_table(pdev, bpmp, cluster, i);
		if (IS_ERR(cluster->table)) {
			err = PTR_ERR(cluster->table);
			goto put_bpmp;
		}
	}

	tegra186_cpufreq_driver.driver_data = data;

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
	.remove_new = tegra186_cpufreq_remove,
};
module_platform_driver(tegra186_cpufreq_platform_driver);

MODULE_AUTHOR("Mikko Perttunen <mperttunen@nvidia.com>");
MODULE_DESCRIPTION("NVIDIA Tegra186 cpufreq driver");
MODULE_LICENSE("GPL v2");
