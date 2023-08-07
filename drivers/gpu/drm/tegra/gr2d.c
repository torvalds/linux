// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012-2013, NVIDIA Corporation.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/iommu.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>

#include <soc/tegra/common.h>

#include "drm.h"
#include "gem.h"
#include "gr2d.h"

enum {
	RST_MC,
	RST_GR2D,
	RST_GR2D_MAX,
};

struct gr2d_soc {
	unsigned int version;
};

struct gr2d {
	struct tegra_drm_client client;
	struct host1x_channel *channel;
	struct clk *clk;

	struct reset_control_bulk_data resets[RST_GR2D_MAX];
	unsigned int nresets;

	const struct gr2d_soc *soc;

	DECLARE_BITMAP(addr_regs, GR2D_NUM_REGS);
};

static inline struct gr2d *to_gr2d(struct tegra_drm_client *client)
{
	return container_of(client, struct gr2d, client);
}

static int gr2d_init(struct host1x_client *client)
{
	struct tegra_drm_client *drm = host1x_to_drm_client(client);
	struct drm_device *dev = dev_get_drvdata(client->host);
	unsigned long flags = HOST1X_SYNCPT_HAS_BASE;
	struct gr2d *gr2d = to_gr2d(drm);
	int err;

	gr2d->channel = host1x_channel_request(client);
	if (!gr2d->channel)
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
	host1x_channel_put(gr2d->channel);
	return err;
}

static int gr2d_exit(struct host1x_client *client)
{
	struct tegra_drm_client *drm = host1x_to_drm_client(client);
	struct drm_device *dev = dev_get_drvdata(client->host);
	struct tegra_drm *tegra = dev->dev_private;
	struct gr2d *gr2d = to_gr2d(drm);
	int err;

	err = tegra_drm_unregister_client(tegra, drm);
	if (err < 0)
		return err;

	pm_runtime_dont_use_autosuspend(client->dev);
	pm_runtime_force_suspend(client->dev);

	host1x_client_iommu_detach(client);
	host1x_syncpt_put(client->syncpts[0]);
	host1x_channel_put(gr2d->channel);

	gr2d->channel = NULL;

	return 0;
}

static const struct host1x_client_ops gr2d_client_ops = {
	.init = gr2d_init,
	.exit = gr2d_exit,
};

static int gr2d_open_channel(struct tegra_drm_client *client,
			     struct tegra_drm_context *context)
{
	struct gr2d *gr2d = to_gr2d(client);

	context->channel = host1x_channel_get(gr2d->channel);
	if (!context->channel)
		return -ENOMEM;

	return 0;
}

static void gr2d_close_channel(struct tegra_drm_context *context)
{
	host1x_channel_put(context->channel);
}

static int gr2d_is_addr_reg(struct device *dev, u32 class, u32 offset)
{
	struct gr2d *gr2d = dev_get_drvdata(dev);

	switch (class) {
	case HOST1X_CLASS_HOST1X:
		if (offset == 0x2b)
			return 1;

		break;

	case HOST1X_CLASS_GR2D:
	case HOST1X_CLASS_GR2D_SB:
		if (offset >= GR2D_NUM_REGS)
			break;

		if (test_bit(offset, gr2d->addr_regs))
			return 1;

		break;
	}

	return 0;
}

static int gr2d_is_valid_class(u32 class)
{
	return (class == HOST1X_CLASS_GR2D ||
		class == HOST1X_CLASS_GR2D_SB);
}

static const struct tegra_drm_client_ops gr2d_ops = {
	.open_channel = gr2d_open_channel,
	.close_channel = gr2d_close_channel,
	.is_addr_reg = gr2d_is_addr_reg,
	.is_valid_class = gr2d_is_valid_class,
	.submit = tegra_drm_submit,
};

static const struct gr2d_soc tegra20_gr2d_soc = {
	.version = 0x20,
};

static const struct gr2d_soc tegra30_gr2d_soc = {
	.version = 0x30,
};

static const struct gr2d_soc tegra114_gr2d_soc = {
	.version = 0x35,
};

static const struct of_device_id gr2d_match[] = {
	{ .compatible = "nvidia,tegra114-gr2d", .data = &tegra114_gr2d_soc },
	{ .compatible = "nvidia,tegra30-gr2d", .data = &tegra30_gr2d_soc },
	{ .compatible = "nvidia,tegra20-gr2d", .data = &tegra20_gr2d_soc },
	{ },
};
MODULE_DEVICE_TABLE(of, gr2d_match);

static const u32 gr2d_addr_regs[] = {
	GR2D_UA_BASE_ADDR,
	GR2D_VA_BASE_ADDR,
	GR2D_PAT_BASE_ADDR,
	GR2D_DSTA_BASE_ADDR,
	GR2D_DSTB_BASE_ADDR,
	GR2D_DSTC_BASE_ADDR,
	GR2D_SRCA_BASE_ADDR,
	GR2D_SRCB_BASE_ADDR,
	GR2D_PATBASE_ADDR,
	GR2D_SRC_BASE_ADDR_SB,
	GR2D_DSTA_BASE_ADDR_SB,
	GR2D_DSTB_BASE_ADDR_SB,
	GR2D_UA_BASE_ADDR_SB,
	GR2D_VA_BASE_ADDR_SB,
};

static int gr2d_get_resets(struct device *dev, struct gr2d *gr2d)
{
	int err;

	gr2d->resets[RST_MC].id = "mc";
	gr2d->resets[RST_GR2D].id = "2d";
	gr2d->nresets = RST_GR2D_MAX;

	err = devm_reset_control_bulk_get_optional_exclusive_released(
				dev, gr2d->nresets, gr2d->resets);
	if (err) {
		dev_err(dev, "failed to get reset: %d\n", err);
		return err;
	}

	if (WARN_ON(!gr2d->resets[RST_GR2D].rstc))
		return -ENOENT;

	return 0;
}

static int gr2d_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct host1x_syncpt **syncpts;
	struct gr2d *gr2d;
	unsigned int i;
	int err;

	gr2d = devm_kzalloc(dev, sizeof(*gr2d), GFP_KERNEL);
	if (!gr2d)
		return -ENOMEM;

	platform_set_drvdata(pdev, gr2d);

	gr2d->soc = of_device_get_match_data(dev);

	syncpts = devm_kzalloc(dev, sizeof(*syncpts), GFP_KERNEL);
	if (!syncpts)
		return -ENOMEM;

	gr2d->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(gr2d->clk)) {
		dev_err(dev, "cannot get clock\n");
		return PTR_ERR(gr2d->clk);
	}

	err = gr2d_get_resets(dev, gr2d);
	if (err)
		return err;

	INIT_LIST_HEAD(&gr2d->client.base.list);
	gr2d->client.base.ops = &gr2d_client_ops;
	gr2d->client.base.dev = dev;
	gr2d->client.base.class = HOST1X_CLASS_GR2D;
	gr2d->client.base.syncpts = syncpts;
	gr2d->client.base.num_syncpts = 1;

	INIT_LIST_HEAD(&gr2d->client.list);
	gr2d->client.version = gr2d->soc->version;
	gr2d->client.ops = &gr2d_ops;

	err = devm_tegra_core_dev_init_opp_table_common(dev);
	if (err)
		return err;

	err = host1x_client_register(&gr2d->client.base);
	if (err < 0) {
		dev_err(dev, "failed to register host1x client: %d\n", err);
		return err;
	}

	/* initialize address register map */
	for (i = 0; i < ARRAY_SIZE(gr2d_addr_regs); i++)
		set_bit(gr2d_addr_regs[i], gr2d->addr_regs);

	return 0;
}

static void gr2d_remove(struct platform_device *pdev)
{
	struct gr2d *gr2d = platform_get_drvdata(pdev);

	pm_runtime_disable(&pdev->dev);
	host1x_client_unregister(&gr2d->client.base);
}

static int __maybe_unused gr2d_runtime_suspend(struct device *dev)
{
	struct gr2d *gr2d = dev_get_drvdata(dev);
	int err;

	host1x_channel_stop(gr2d->channel);
	reset_control_bulk_release(gr2d->nresets, gr2d->resets);

	/*
	 * GR2D module shouldn't be reset while hardware is idling, otherwise
	 * host1x's cmdproc will stuck on trying to access any G2 register
	 * after reset. GR2D module could be either hot-reset or reset after
	 * power-gating of the HEG partition. Hence we will put in reset only
	 * the memory client part of the module, the HEG GENPD will take care
	 * of resetting GR2D module across power-gating.
	 *
	 * On Tegra20 there is no HEG partition, but it's okay to have
	 * undetermined h/w state since userspace is expected to reprogram
	 * the state on each job submission anyways.
	 */
	err = reset_control_acquire(gr2d->resets[RST_MC].rstc);
	if (err) {
		dev_err(dev, "failed to acquire MC reset: %d\n", err);
		goto acquire_reset;
	}

	err = reset_control_assert(gr2d->resets[RST_MC].rstc);
	reset_control_release(gr2d->resets[RST_MC].rstc);
	if (err) {
		dev_err(dev, "failed to assert MC reset: %d\n", err);
		goto acquire_reset;
	}

	clk_disable_unprepare(gr2d->clk);

	return 0;

acquire_reset:
	reset_control_bulk_acquire(gr2d->nresets, gr2d->resets);
	reset_control_bulk_deassert(gr2d->nresets, gr2d->resets);

	return err;
}

static int __maybe_unused gr2d_runtime_resume(struct device *dev)
{
	struct gr2d *gr2d = dev_get_drvdata(dev);
	int err;

	err = reset_control_bulk_acquire(gr2d->nresets, gr2d->resets);
	if (err) {
		dev_err(dev, "failed to acquire reset: %d\n", err);
		return err;
	}

	err = clk_prepare_enable(gr2d->clk);
	if (err) {
		dev_err(dev, "failed to enable clock: %d\n", err);
		goto release_reset;
	}

	usleep_range(2000, 4000);

	/* this is a reset array which deasserts both 2D MC and 2D itself */
	err = reset_control_bulk_deassert(gr2d->nresets, gr2d->resets);
	if (err) {
		dev_err(dev, "failed to deassert reset: %d\n", err);
		goto disable_clk;
	}

	pm_runtime_enable(dev);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_set_autosuspend_delay(dev, 500);

	return 0;

disable_clk:
	clk_disable_unprepare(gr2d->clk);
release_reset:
	reset_control_bulk_release(gr2d->nresets, gr2d->resets);

	return err;
}

static const struct dev_pm_ops tegra_gr2d_pm = {
	SET_RUNTIME_PM_OPS(gr2d_runtime_suspend, gr2d_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
};

struct platform_driver tegra_gr2d_driver = {
	.driver = {
		.name = "tegra-gr2d",
		.of_match_table = gr2d_match,
		.pm = &tegra_gr2d_pm,
	},
	.probe = gr2d_probe,
	.remove_new = gr2d_remove,
};
