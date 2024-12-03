// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013 Avionic Design GmbH
 * Copyright (C) 2013 NVIDIA Corporation
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/host1x.h>
#include <linux/iommu.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_opp.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>

#include <soc/tegra/common.h>
#include <soc/tegra/pmc.h>

#include "drm.h"
#include "gem.h"
#include "gr3d.h"

enum {
	RST_MC,
	RST_GR3D,
	RST_MC2,
	RST_GR3D2,
	RST_GR3D_MAX,
};

struct gr3d_soc {
	unsigned int version;
	unsigned int num_clocks;
	unsigned int num_resets;
};

struct gr3d {
	struct tegra_drm_client client;
	struct host1x_channel *channel;

	const struct gr3d_soc *soc;
	struct clk_bulk_data *clocks;
	unsigned int nclocks;
	struct reset_control_bulk_data resets[RST_GR3D_MAX];
	unsigned int nresets;
	struct dev_pm_domain_list *pd_list;

	DECLARE_BITMAP(addr_regs, GR3D_NUM_REGS);
};

static inline struct gr3d *to_gr3d(struct tegra_drm_client *client)
{
	return container_of(client, struct gr3d, client);
}

static int gr3d_init(struct host1x_client *client)
{
	struct tegra_drm_client *drm = host1x_to_drm_client(client);
	struct drm_device *dev = dev_get_drvdata(client->host);
	unsigned long flags = HOST1X_SYNCPT_HAS_BASE;
	struct gr3d *gr3d = to_gr3d(drm);
	int err;

	gr3d->channel = host1x_channel_request(client);
	if (!gr3d->channel)
		return -ENOMEM;

	client->syncpts[0] = host1x_syncpt_request(client, flags);
	if (!client->syncpts[0]) {
		err = -ENOMEM;
		dev_err(client->dev, "failed to request syncpoint: %d\n", err);
		goto put;
	}

	err = host1x_client_iommu_attach(client);
	if (err < 0) {
		dev_err(client->dev, "failed to attach to domain: %d\n", err);
		goto free;
	}

	err = tegra_drm_register_client(dev->dev_private, drm);
	if (err < 0) {
		dev_err(client->dev, "failed to register client: %d\n", err);
		goto detach_iommu;
	}

	return 0;

detach_iommu:
	host1x_client_iommu_detach(client);
free:
	host1x_syncpt_put(client->syncpts[0]);
put:
	host1x_channel_put(gr3d->channel);
	return err;
}

static int gr3d_exit(struct host1x_client *client)
{
	struct tegra_drm_client *drm = host1x_to_drm_client(client);
	struct drm_device *dev = dev_get_drvdata(client->host);
	struct gr3d *gr3d = to_gr3d(drm);
	int err;

	err = tegra_drm_unregister_client(dev->dev_private, drm);
	if (err < 0)
		return err;

	pm_runtime_dont_use_autosuspend(client->dev);
	pm_runtime_force_suspend(client->dev);

	host1x_client_iommu_detach(client);
	host1x_syncpt_put(client->syncpts[0]);
	host1x_channel_put(gr3d->channel);

	gr3d->channel = NULL;

	return 0;
}

static const struct host1x_client_ops gr3d_client_ops = {
	.init = gr3d_init,
	.exit = gr3d_exit,
};

static int gr3d_open_channel(struct tegra_drm_client *client,
			     struct tegra_drm_context *context)
{
	struct gr3d *gr3d = to_gr3d(client);

	context->channel = host1x_channel_get(gr3d->channel);
	if (!context->channel)
		return -ENOMEM;

	return 0;
}

static void gr3d_close_channel(struct tegra_drm_context *context)
{
	host1x_channel_put(context->channel);
}

static int gr3d_is_addr_reg(struct device *dev, u32 class, u32 offset)
{
	struct gr3d *gr3d = dev_get_drvdata(dev);

	switch (class) {
	case HOST1X_CLASS_HOST1X:
		if (offset == 0x2b)
			return 1;

		break;

	case HOST1X_CLASS_GR3D:
		if (offset >= GR3D_NUM_REGS)
			break;

		if (test_bit(offset, gr3d->addr_regs))
			return 1;

		break;
	}

	return 0;
}

static const struct tegra_drm_client_ops gr3d_ops = {
	.open_channel = gr3d_open_channel,
	.close_channel = gr3d_close_channel,
	.is_addr_reg = gr3d_is_addr_reg,
	.submit = tegra_drm_submit,
};

static const struct gr3d_soc tegra20_gr3d_soc = {
	.version = 0x20,
	.num_clocks = 1,
	.num_resets = 2,
};

static const struct gr3d_soc tegra30_gr3d_soc = {
	.version = 0x30,
	.num_clocks = 2,
	.num_resets = 4,
};

static const struct gr3d_soc tegra114_gr3d_soc = {
	.version = 0x35,
	.num_clocks = 1,
	.num_resets = 2,
};

static const struct of_device_id tegra_gr3d_match[] = {
	{ .compatible = "nvidia,tegra114-gr3d", .data = &tegra114_gr3d_soc },
	{ .compatible = "nvidia,tegra30-gr3d", .data = &tegra30_gr3d_soc },
	{ .compatible = "nvidia,tegra20-gr3d", .data = &tegra20_gr3d_soc },
	{ }
};
MODULE_DEVICE_TABLE(of, tegra_gr3d_match);

static const u32 gr3d_addr_regs[] = {
	GR3D_IDX_ATTRIBUTE( 0),
	GR3D_IDX_ATTRIBUTE( 1),
	GR3D_IDX_ATTRIBUTE( 2),
	GR3D_IDX_ATTRIBUTE( 3),
	GR3D_IDX_ATTRIBUTE( 4),
	GR3D_IDX_ATTRIBUTE( 5),
	GR3D_IDX_ATTRIBUTE( 6),
	GR3D_IDX_ATTRIBUTE( 7),
	GR3D_IDX_ATTRIBUTE( 8),
	GR3D_IDX_ATTRIBUTE( 9),
	GR3D_IDX_ATTRIBUTE(10),
	GR3D_IDX_ATTRIBUTE(11),
	GR3D_IDX_ATTRIBUTE(12),
	GR3D_IDX_ATTRIBUTE(13),
	GR3D_IDX_ATTRIBUTE(14),
	GR3D_IDX_ATTRIBUTE(15),
	GR3D_IDX_INDEX_BASE,
	GR3D_QR_ZTAG_ADDR,
	GR3D_QR_CTAG_ADDR,
	GR3D_QR_CZ_ADDR,
	GR3D_TEX_TEX_ADDR( 0),
	GR3D_TEX_TEX_ADDR( 1),
	GR3D_TEX_TEX_ADDR( 2),
	GR3D_TEX_TEX_ADDR( 3),
	GR3D_TEX_TEX_ADDR( 4),
	GR3D_TEX_TEX_ADDR( 5),
	GR3D_TEX_TEX_ADDR( 6),
	GR3D_TEX_TEX_ADDR( 7),
	GR3D_TEX_TEX_ADDR( 8),
	GR3D_TEX_TEX_ADDR( 9),
	GR3D_TEX_TEX_ADDR(10),
	GR3D_TEX_TEX_ADDR(11),
	GR3D_TEX_TEX_ADDR(12),
	GR3D_TEX_TEX_ADDR(13),
	GR3D_TEX_TEX_ADDR(14),
	GR3D_TEX_TEX_ADDR(15),
	GR3D_DW_MEMORY_OUTPUT_ADDRESS,
	GR3D_GLOBAL_SURFADDR( 0),
	GR3D_GLOBAL_SURFADDR( 1),
	GR3D_GLOBAL_SURFADDR( 2),
	GR3D_GLOBAL_SURFADDR( 3),
	GR3D_GLOBAL_SURFADDR( 4),
	GR3D_GLOBAL_SURFADDR( 5),
	GR3D_GLOBAL_SURFADDR( 6),
	GR3D_GLOBAL_SURFADDR( 7),
	GR3D_GLOBAL_SURFADDR( 8),
	GR3D_GLOBAL_SURFADDR( 9),
	GR3D_GLOBAL_SURFADDR(10),
	GR3D_GLOBAL_SURFADDR(11),
	GR3D_GLOBAL_SURFADDR(12),
	GR3D_GLOBAL_SURFADDR(13),
	GR3D_GLOBAL_SURFADDR(14),
	GR3D_GLOBAL_SURFADDR(15),
	GR3D_GLOBAL_SPILLSURFADDR,
	GR3D_GLOBAL_SURFOVERADDR( 0),
	GR3D_GLOBAL_SURFOVERADDR( 1),
	GR3D_GLOBAL_SURFOVERADDR( 2),
	GR3D_GLOBAL_SURFOVERADDR( 3),
	GR3D_GLOBAL_SURFOVERADDR( 4),
	GR3D_GLOBAL_SURFOVERADDR( 5),
	GR3D_GLOBAL_SURFOVERADDR( 6),
	GR3D_GLOBAL_SURFOVERADDR( 7),
	GR3D_GLOBAL_SURFOVERADDR( 8),
	GR3D_GLOBAL_SURFOVERADDR( 9),
	GR3D_GLOBAL_SURFOVERADDR(10),
	GR3D_GLOBAL_SURFOVERADDR(11),
	GR3D_GLOBAL_SURFOVERADDR(12),
	GR3D_GLOBAL_SURFOVERADDR(13),
	GR3D_GLOBAL_SURFOVERADDR(14),
	GR3D_GLOBAL_SURFOVERADDR(15),
	GR3D_GLOBAL_SAMP01SURFADDR( 0),
	GR3D_GLOBAL_SAMP01SURFADDR( 1),
	GR3D_GLOBAL_SAMP01SURFADDR( 2),
	GR3D_GLOBAL_SAMP01SURFADDR( 3),
	GR3D_GLOBAL_SAMP01SURFADDR( 4),
	GR3D_GLOBAL_SAMP01SURFADDR( 5),
	GR3D_GLOBAL_SAMP01SURFADDR( 6),
	GR3D_GLOBAL_SAMP01SURFADDR( 7),
	GR3D_GLOBAL_SAMP01SURFADDR( 8),
	GR3D_GLOBAL_SAMP01SURFADDR( 9),
	GR3D_GLOBAL_SAMP01SURFADDR(10),
	GR3D_GLOBAL_SAMP01SURFADDR(11),
	GR3D_GLOBAL_SAMP01SURFADDR(12),
	GR3D_GLOBAL_SAMP01SURFADDR(13),
	GR3D_GLOBAL_SAMP01SURFADDR(14),
	GR3D_GLOBAL_SAMP01SURFADDR(15),
	GR3D_GLOBAL_SAMP23SURFADDR( 0),
	GR3D_GLOBAL_SAMP23SURFADDR( 1),
	GR3D_GLOBAL_SAMP23SURFADDR( 2),
	GR3D_GLOBAL_SAMP23SURFADDR( 3),
	GR3D_GLOBAL_SAMP23SURFADDR( 4),
	GR3D_GLOBAL_SAMP23SURFADDR( 5),
	GR3D_GLOBAL_SAMP23SURFADDR( 6),
	GR3D_GLOBAL_SAMP23SURFADDR( 7),
	GR3D_GLOBAL_SAMP23SURFADDR( 8),
	GR3D_GLOBAL_SAMP23SURFADDR( 9),
	GR3D_GLOBAL_SAMP23SURFADDR(10),
	GR3D_GLOBAL_SAMP23SURFADDR(11),
	GR3D_GLOBAL_SAMP23SURFADDR(12),
	GR3D_GLOBAL_SAMP23SURFADDR(13),
	GR3D_GLOBAL_SAMP23SURFADDR(14),
	GR3D_GLOBAL_SAMP23SURFADDR(15),
};

static int gr3d_power_up_legacy_domain(struct device *dev, const char *name,
				       unsigned int id)
{
	struct gr3d *gr3d = dev_get_drvdata(dev);
	struct reset_control *reset;
	struct clk *clk;
	unsigned int i;
	int err;

	/*
	 * Tegra20 device-tree doesn't specify 3d clock name and there is only
	 * one clock for Tegra20. Tegra30+ device-trees always specified names
	 * for the clocks.
	 */
	if (gr3d->nclocks == 1) {
		if (id == TEGRA_POWERGATE_3D1)
			return 0;

		clk = gr3d->clocks[0].clk;
	} else {
		for (i = 0; i < gr3d->nclocks; i++) {
			if (WARN_ON(!gr3d->clocks[i].id))
				continue;

			if (!strcmp(gr3d->clocks[i].id, name)) {
				clk = gr3d->clocks[i].clk;
				break;
			}
		}

		if (WARN_ON(i == gr3d->nclocks))
			return -EINVAL;
	}

	/*
	 * We use array of resets, which includes MC resets, and MC
	 * reset shouldn't be asserted while hardware is gated because
	 * MC flushing will fail for gated hardware. Hence for legacy
	 * PD we request the individual reset separately.
	 */
	reset = reset_control_get_exclusive_released(dev, name);
	if (IS_ERR(reset))
		return PTR_ERR(reset);

	err = reset_control_acquire(reset);
	if (err) {
		dev_err(dev, "failed to acquire %s reset: %d\n", name, err);
	} else {
		err = tegra_powergate_sequence_power_up(id, clk, reset);
		reset_control_release(reset);
	}

	reset_control_put(reset);
	if (err)
		return err;

	/*
	 * tegra_powergate_sequence_power_up() leaves clocks enabled,
	 * while GENPD not. Hence keep clock-enable balanced.
	 */
	clk_disable_unprepare(clk);

	return 0;
}

static int gr3d_init_power(struct device *dev, struct gr3d *gr3d)
{
	struct dev_pm_domain_attach_data pd_data = {
		.pd_names = (const char *[]) { "3d0", "3d1" },
		.num_pd_names = 2,
		.pd_flags = PD_FLAG_REQUIRED_OPP,
	};
	int err;

	err = of_count_phandle_with_args(dev->of_node, "power-domains",
					 "#power-domain-cells");
	if (err < 0) {
		if (err != -ENOENT)
			return err;

		/*
		 * Older device-trees don't use GENPD. In this case we should
		 * toggle power domain manually.
		 */
		err = gr3d_power_up_legacy_domain(dev, "3d",
						  TEGRA_POWERGATE_3D);
		if (err)
			return err;

		err = gr3d_power_up_legacy_domain(dev, "3d2",
						  TEGRA_POWERGATE_3D1);
		if (err)
			return err;

		return 0;
	}

	/*
	 * The PM domain core automatically attaches a single power domain,
	 * otherwise it skips attaching completely. We have a single domain
	 * on Tegra20 and two domains on Tegra30+.
	 */
	if (dev->pm_domain)
		return 0;

	err = devm_pm_domain_attach_list(dev, &pd_data, &gr3d->pd_list);
	if (err < 0)
		return err;

	return 0;
}

static int gr3d_get_clocks(struct device *dev, struct gr3d *gr3d)
{
	int err;

	err = devm_clk_bulk_get_all(dev, &gr3d->clocks);
	if (err < 0) {
		dev_err(dev, "failed to get clock: %d\n", err);
		return err;
	}
	gr3d->nclocks = err;

	if (gr3d->nclocks != gr3d->soc->num_clocks) {
		dev_err(dev, "invalid number of clocks: %u\n", gr3d->nclocks);
		return -ENOENT;
	}

	return 0;
}

static int gr3d_get_resets(struct device *dev, struct gr3d *gr3d)
{
	int err;

	gr3d->resets[RST_MC].id = "mc";
	gr3d->resets[RST_MC2].id = "mc2";
	gr3d->resets[RST_GR3D].id = "3d";
	gr3d->resets[RST_GR3D2].id = "3d2";
	gr3d->nresets = gr3d->soc->num_resets;

	err = devm_reset_control_bulk_get_optional_exclusive_released(
				dev, gr3d->nresets, gr3d->resets);
	if (err) {
		dev_err(dev, "failed to get reset: %d\n", err);
		return err;
	}

	if (WARN_ON(!gr3d->resets[RST_GR3D].rstc) ||
	    WARN_ON(!gr3d->resets[RST_GR3D2].rstc && gr3d->nresets == 4))
		return -ENOENT;

	return 0;
}

static int gr3d_probe(struct platform_device *pdev)
{
	struct host1x_syncpt **syncpts;
	struct gr3d *gr3d;
	unsigned int i;
	int err;

	gr3d = devm_kzalloc(&pdev->dev, sizeof(*gr3d), GFP_KERNEL);
	if (!gr3d)
		return -ENOMEM;

	platform_set_drvdata(pdev, gr3d);

	gr3d->soc = of_device_get_match_data(&pdev->dev);

	syncpts = devm_kzalloc(&pdev->dev, sizeof(*syncpts), GFP_KERNEL);
	if (!syncpts)
		return -ENOMEM;

	err = gr3d_get_clocks(&pdev->dev, gr3d);
	if (err)
		return err;

	err = gr3d_get_resets(&pdev->dev, gr3d);
	if (err)
		return err;

	err = gr3d_init_power(&pdev->dev, gr3d);
	if (err)
		return err;

	INIT_LIST_HEAD(&gr3d->client.base.list);
	gr3d->client.base.ops = &gr3d_client_ops;
	gr3d->client.base.dev = &pdev->dev;
	gr3d->client.base.class = HOST1X_CLASS_GR3D;
	gr3d->client.base.syncpts = syncpts;
	gr3d->client.base.num_syncpts = 1;

	INIT_LIST_HEAD(&gr3d->client.list);
	gr3d->client.version = gr3d->soc->version;
	gr3d->client.ops = &gr3d_ops;

	err = devm_tegra_core_dev_init_opp_table_common(&pdev->dev);
	if (err)
		return err;

	err = host1x_client_register(&gr3d->client.base);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to register host1x client: %d\n",
			err);
		return err;
	}

	/* initialize address register map */
	for (i = 0; i < ARRAY_SIZE(gr3d_addr_regs); i++)
		set_bit(gr3d_addr_regs[i], gr3d->addr_regs);

	return 0;
}

static void gr3d_remove(struct platform_device *pdev)
{
	struct gr3d *gr3d = platform_get_drvdata(pdev);

	pm_runtime_disable(&pdev->dev);
	host1x_client_unregister(&gr3d->client.base);
}

static int __maybe_unused gr3d_runtime_suspend(struct device *dev)
{
	struct gr3d *gr3d = dev_get_drvdata(dev);
	int err;

	host1x_channel_stop(gr3d->channel);

	err = reset_control_bulk_assert(gr3d->nresets, gr3d->resets);
	if (err) {
		dev_err(dev, "failed to assert reset: %d\n", err);
		return err;
	}

	usleep_range(10, 20);

	/*
	 * Older device-trees don't specify MC resets and power-gating can't
	 * be done safely in that case. Hence we will keep the power ungated
	 * for older DTBs. For newer DTBs, GENPD will perform the power-gating.
	 */

	clk_bulk_disable_unprepare(gr3d->nclocks, gr3d->clocks);
	reset_control_bulk_release(gr3d->nresets, gr3d->resets);

	return 0;
}

static int __maybe_unused gr3d_runtime_resume(struct device *dev)
{
	struct gr3d *gr3d = dev_get_drvdata(dev);
	int err;

	err = reset_control_bulk_acquire(gr3d->nresets, gr3d->resets);
	if (err) {
		dev_err(dev, "failed to acquire reset: %d\n", err);
		return err;
	}

	err = clk_bulk_prepare_enable(gr3d->nclocks, gr3d->clocks);
	if (err) {
		dev_err(dev, "failed to enable clock: %d\n", err);
		goto release_reset;
	}

	err = reset_control_bulk_deassert(gr3d->nresets, gr3d->resets);
	if (err) {
		dev_err(dev, "failed to deassert reset: %d\n", err);
		goto disable_clk;
	}

	pm_runtime_enable(dev);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_set_autosuspend_delay(dev, 500);

	return 0;

disable_clk:
	clk_bulk_disable_unprepare(gr3d->nclocks, gr3d->clocks);
release_reset:
	reset_control_bulk_release(gr3d->nresets, gr3d->resets);

	return err;
}

static const struct dev_pm_ops tegra_gr3d_pm = {
	SET_RUNTIME_PM_OPS(gr3d_runtime_suspend, gr3d_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
};

struct platform_driver tegra_gr3d_driver = {
	.driver = {
		.name = "tegra-gr3d",
		.of_match_table = tegra_gr3d_match,
		.pm = &tegra_gr3d_pm,
	},
	.probe = gr3d_probe,
	.remove = gr3d_remove,
};
