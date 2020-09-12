// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved
 */

#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <asm/smp_plat.h>

#include <soc/tegra/bpmp.h>
#include <soc/tegra/bpmp-abi.h>

#define KHZ                     1000
#define REF_CLK_MHZ             408 /* 408 MHz */
#define US_DELAY                500
#define US_DELAY_MIN            2
#define CPUFREQ_TBL_STEP_HZ     (50 * KHZ * KHZ)
#define MAX_CNT                 ~0U

/* cpufreq transisition latency */
#define TEGRA_CPUFREQ_TRANSITION_LATENCY (300 * 1000) /* unit in nanoseconds */

enum cluster {
	CLUSTER0,
	CLUSTER1,
	CLUSTER2,
	CLUSTER3,
	MAX_CLUSTERS,
};

struct tegra194_cpufreq_data {
	void __iomem *regs;
	size_t num_clusters;
	struct cpufreq_frequency_table **tables;
};

struct tegra_cpu_ctr {
	u32 cpu;
	u32 delay;
	u32 coreclk_cnt, last_coreclk_cnt;
	u32 refclk_cnt, last_refclk_cnt;
};

struct read_counters_work {
	struct work_struct work;
	struct tegra_cpu_ctr c;
};

static struct workqueue_struct *read_counters_wq;

static void get_cpu_cluster(void *cluster)
{
	u64 mpidr = read_cpuid_mpidr() & MPIDR_HWID_BITMASK;

	*((uint32_t *)cluster) = MPIDR_AFFINITY_LEVEL(mpidr, 1);
}

/*
 * Read per-core Read-only system register NVFREQ_FEEDBACK_EL1.
 * The register provides frequency feedback information to
 * determine the average actual frequency a core has run at over
 * a period of time.
 *	[31:0] PLLP counter: Counts at fixed frequency (408 MHz)
 *	[63:32] Core clock counter: counts on every core clock cycle
 *			where the core is architecturally clocking
 */
static u64 read_freq_feedback(void)
{
	u64 val = 0;

	asm volatile("mrs %0, s3_0_c15_c0_5" : "=r" (val) : );

	return val;
}

static inline u32 map_ndiv_to_freq(struct mrq_cpu_ndiv_limits_response
				   *nltbl, u16 ndiv)
{
	return nltbl->ref_clk_hz / KHZ * ndiv / (nltbl->pdiv * nltbl->mdiv);
}

static void tegra_read_counters(struct work_struct *work)
{
	struct read_counters_work *read_counters_work;
	struct tegra_cpu_ctr *c;
	u64 val;

	/*
	 * ref_clk_counter(32 bit counter) runs on constant clk,
	 * pll_p(408MHz).
	 * It will take = 2 ^ 32 / 408 MHz to overflow ref clk counter
	 *              = 10526880 usec = 10.527 sec to overflow
	 *
	 * Like wise core_clk_counter(32 bit counter) runs on core clock.
	 * It's synchronized to crab_clk (cpu_crab_clk) which runs at
	 * freq of cluster. Assuming max cluster clock ~2000MHz,
	 * It will take = 2 ^ 32 / 2000 MHz to overflow core clk counter
	 *              = ~2.147 sec to overflow
	 */
	read_counters_work = container_of(work, struct read_counters_work,
					  work);
	c = &read_counters_work->c;

	val = read_freq_feedback();
	c->last_refclk_cnt = lower_32_bits(val);
	c->last_coreclk_cnt = upper_32_bits(val);
	udelay(c->delay);
	val = read_freq_feedback();
	c->refclk_cnt = lower_32_bits(val);
	c->coreclk_cnt = upper_32_bits(val);
}

/*
 * Return instantaneous cpu speed
 * Instantaneous freq is calculated as -
 * -Takes sample on every query of getting the freq.
 *	- Read core and ref clock counters;
 *	- Delay for X us
 *	- Read above cycle counters again
 *	- Calculates freq by subtracting current and previous counters
 *	  divided by the delay time or eqv. of ref_clk_counter in delta time
 *	- Return Kcycles/second, freq in KHz
 *
 *	delta time period = x sec
 *			  = delta ref_clk_counter / (408 * 10^6) sec
 *	freq in Hz = cycles/sec
 *		   = (delta cycles / x sec
 *		   = (delta cycles * 408 * 10^6) / delta ref_clk_counter
 *	in KHz	   = (delta cycles * 408 * 10^3) / delta ref_clk_counter
 *
 * @cpu - logical cpu whose freq to be updated
 * Returns freq in KHz on success, 0 if cpu is offline
 */
static unsigned int tegra194_get_speed_common(u32 cpu, u32 delay)
{
	struct read_counters_work read_counters_work;
	struct tegra_cpu_ctr c;
	u32 delta_refcnt;
	u32 delta_ccnt;
	u32 rate_mhz;

	/*
	 * udelay() is required to reconstruct cpu frequency over an
	 * observation window. Using workqueue to call udelay() with
	 * interrupts enabled.
	 */
	read_counters_work.c.cpu = cpu;
	read_counters_work.c.delay = delay;
	INIT_WORK_ONSTACK(&read_counters_work.work, tegra_read_counters);
	queue_work_on(cpu, read_counters_wq, &read_counters_work.work);
	flush_work(&read_counters_work.work);
	c = read_counters_work.c;

	if (c.coreclk_cnt < c.last_coreclk_cnt)
		delta_ccnt = c.coreclk_cnt + (MAX_CNT - c.last_coreclk_cnt);
	else
		delta_ccnt = c.coreclk_cnt - c.last_coreclk_cnt;
	if (!delta_ccnt)
		return 0;

	/* ref clock is 32 bits */
	if (c.refclk_cnt < c.last_refclk_cnt)
		delta_refcnt = c.refclk_cnt + (MAX_CNT - c.last_refclk_cnt);
	else
		delta_refcnt = c.refclk_cnt - c.last_refclk_cnt;
	if (!delta_refcnt) {
		pr_debug("cpufreq: %d is idle, delta_refcnt: 0\n", cpu);
		return 0;
	}
	rate_mhz = ((unsigned long)(delta_ccnt * REF_CLK_MHZ)) / delta_refcnt;

	return (rate_mhz * KHZ); /* in KHz */
}

static unsigned int tegra194_get_speed(u32 cpu)
{
	return tegra194_get_speed_common(cpu, US_DELAY);
}

static int tegra194_cpufreq_init(struct cpufreq_policy *policy)
{
	struct tegra194_cpufreq_data *data = cpufreq_get_driver_data();
	u32 cpu;
	u32 cl;

	smp_call_function_single(policy->cpu, get_cpu_cluster, &cl, true);

	if (cl >= data->num_clusters)
		return -EINVAL;

	/* boot freq */
	policy->cur = tegra194_get_speed_common(policy->cpu, US_DELAY_MIN);

	/* set same policy for all cpus in a cluster */
	for (cpu = (cl * 2); cpu < ((cl + 1) * 2); cpu++)
		cpumask_set_cpu(cpu, policy->cpus);

	policy->freq_table = data->tables[cl];
	policy->cpuinfo.transition_latency = TEGRA_CPUFREQ_TRANSITION_LATENCY;

	return 0;
}

static void set_cpu_ndiv(void *data)
{
	struct cpufreq_frequency_table *tbl = data;
	u64 ndiv_val = (u64)tbl->driver_data;

	asm volatile("msr s3_0_c15_c0_4, %0" : : "r" (ndiv_val));
}

static int tegra194_cpufreq_set_target(struct cpufreq_policy *policy,
				       unsigned int index)
{
	struct cpufreq_frequency_table *tbl = policy->freq_table + index;

	/*
	 * Each core writes frequency in per core register. Then both cores
	 * in a cluster run at same frequency which is the maximum frequency
	 * request out of the values requested by both cores in that cluster.
	 */
	on_each_cpu_mask(policy->cpus, set_cpu_ndiv, tbl, true);

	return 0;
}

static struct cpufreq_driver tegra194_cpufreq_driver = {
	.name = "tegra194",
	.flags = CPUFREQ_STICKY | CPUFREQ_CONST_LOOPS |
		CPUFREQ_NEED_INITIAL_FREQ_CHECK,
	.verify = cpufreq_generic_frequency_table_verify,
	.target_index = tegra194_cpufreq_set_target,
	.get = tegra194_get_speed,
	.init = tegra194_cpufreq_init,
	.attr = cpufreq_generic_attr,
};

static void tegra194_cpufreq_free_resources(void)
{
	destroy_workqueue(read_counters_wq);
}

static struct cpufreq_frequency_table *
init_freq_table(struct platform_device *pdev, struct tegra_bpmp *bpmp,
		unsigned int cluster_id)
{
	struct cpufreq_frequency_table *freq_table;
	struct mrq_cpu_ndiv_limits_response resp;
	unsigned int num_freqs, ndiv, delta_ndiv;
	struct mrq_cpu_ndiv_limits_request req;
	struct tegra_bpmp_message msg;
	u16 freq_table_step_size;
	int err, index;

	memset(&req, 0, sizeof(req));
	req.cluster_id = cluster_id;

	memset(&msg, 0, sizeof(msg));
	msg.mrq = MRQ_CPU_NDIV_LIMITS;
	msg.tx.data = &req;
	msg.tx.size = sizeof(req);
	msg.rx.data = &resp;
	msg.rx.size = sizeof(resp);

	err = tegra_bpmp_transfer(bpmp, &msg);
	if (err)
		return ERR_PTR(err);

	/*
	 * Make sure frequency table step is a multiple of mdiv to match
	 * vhint table granularity.
	 */
	freq_table_step_size = resp.mdiv *
			DIV_ROUND_UP(CPUFREQ_TBL_STEP_HZ, resp.ref_clk_hz);

	dev_dbg(&pdev->dev, "cluster %d: frequency table step size: %d\n",
		cluster_id, freq_table_step_size);

	delta_ndiv = resp.ndiv_max - resp.ndiv_min;

	if (unlikely(delta_ndiv == 0)) {
		num_freqs = 1;
	} else {
		/* We store both ndiv_min and ndiv_max hence the +1 */
		num_freqs = delta_ndiv / freq_table_step_size + 1;
	}

	num_freqs += (delta_ndiv % freq_table_step_size) ? 1 : 0;

	freq_table = devm_kcalloc(&pdev->dev, num_freqs + 1,
				  sizeof(*freq_table), GFP_KERNEL);
	if (!freq_table)
		return ERR_PTR(-ENOMEM);

	for (index = 0, ndiv = resp.ndiv_min;
			ndiv < resp.ndiv_max;
			index++, ndiv += freq_table_step_size) {
		freq_table[index].driver_data = ndiv;
		freq_table[index].frequency = map_ndiv_to_freq(&resp, ndiv);
	}

	freq_table[index].driver_data = resp.ndiv_max;
	freq_table[index++].frequency = map_ndiv_to_freq(&resp, resp.ndiv_max);
	freq_table[index].frequency = CPUFREQ_TABLE_END;

	return freq_table;
}

static int tegra194_cpufreq_probe(struct platform_device *pdev)
{
	struct tegra194_cpufreq_data *data;
	struct tegra_bpmp *bpmp;
	int err, i;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->num_clusters = MAX_CLUSTERS;
	data->tables = devm_kcalloc(&pdev->dev, data->num_clusters,
				    sizeof(*data->tables), GFP_KERNEL);
	if (!data->tables)
		return -ENOMEM;

	platform_set_drvdata(pdev, data);

	bpmp = tegra_bpmp_get(&pdev->dev);
	if (IS_ERR(bpmp))
		return PTR_ERR(bpmp);

	read_counters_wq = alloc_workqueue("read_counters_wq", __WQ_LEGACY, 1);
	if (!read_counters_wq) {
		dev_err(&pdev->dev, "fail to create_workqueue\n");
		err = -EINVAL;
		goto put_bpmp;
	}

	for (i = 0; i < data->num_clusters; i++) {
		data->tables[i] = init_freq_table(pdev, bpmp, i);
		if (IS_ERR(data->tables[i])) {
			err = PTR_ERR(data->tables[i]);
			goto err_free_res;
		}
	}

	tegra194_cpufreq_driver.driver_data = data;

	err = cpufreq_register_driver(&tegra194_cpufreq_driver);
	if (!err)
		goto put_bpmp;

err_free_res:
	tegra194_cpufreq_free_resources();
put_bpmp:
	tegra_bpmp_put(bpmp);
	return err;
}

static int tegra194_cpufreq_remove(struct platform_device *pdev)
{
	cpufreq_unregister_driver(&tegra194_cpufreq_driver);
	tegra194_cpufreq_free_resources();

	return 0;
}

static const struct of_device_id tegra194_cpufreq_of_match[] = {
	{ .compatible = "nvidia,tegra194-ccplex", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, tegra194_cpufreq_of_match);

static struct platform_driver tegra194_ccplex_driver = {
	.driver = {
		.name = "tegra194-cpufreq",
		.of_match_table = tegra194_cpufreq_of_match,
	},
	.probe = tegra194_cpufreq_probe,
	.remove = tegra194_cpufreq_remove,
};
module_platform_driver(tegra194_ccplex_driver);

MODULE_AUTHOR("Mikko Perttunen <mperttunen@nvidia.com>");
MODULE_AUTHOR("Sumit Gupta <sumitg@nvidia.com>");
MODULE_DESCRIPTION("NVIDIA Tegra194 cpufreq driver");
MODULE_LICENSE("GPL v2");
