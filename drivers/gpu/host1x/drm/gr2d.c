/*
 * drivers/video/tegra/host/gr2d/gr2d.c
 *
 * Tegra Graphics 2D
 *
 * Copyright (c) 2012-2013, NVIDIA Corporation.
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
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/export.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/clk.h>

#include "channel.h"
#include "drm.h"
#include "gem.h"
#include "job.h"
#include "host1x.h"
#include "host1x_bo.h"
#include "host1x_client.h"
#include "syncpt.h"

struct gr2d {
	struct host1x_client client;
	struct clk *clk;
	struct host1x_channel *channel;
	unsigned long *addr_regs;
};

static inline struct gr2d *to_gr2d(struct host1x_client *client)
{
	return container_of(client, struct gr2d, client);
}

static int gr2d_is_addr_reg(struct device *dev, u32 class, u32 reg);

static int gr2d_client_init(struct host1x_client *client,
			    struct drm_device *drm)
{
	return 0;
}

static int gr2d_client_exit(struct host1x_client *client)
{
	return 0;
}

static int gr2d_open_channel(struct host1x_client *client,
			     struct host1x_drm_context *context)
{
	struct gr2d *gr2d = to_gr2d(client);

	context->channel = host1x_channel_get(gr2d->channel);

	if (!context->channel)
		return -ENOMEM;

	return 0;
}

static void gr2d_close_channel(struct host1x_drm_context *context)
{
	host1x_channel_put(context->channel);
}

static struct host1x_bo *host1x_bo_lookup(struct drm_device *drm,
					  struct drm_file *file,
					  u32 handle)
{
	struct drm_gem_object *gem;
	struct tegra_bo *bo;

	gem = drm_gem_object_lookup(drm, file, handle);
	if (!gem)
		return 0;

	mutex_lock(&drm->struct_mutex);
	drm_gem_object_unreference(gem);
	mutex_unlock(&drm->struct_mutex);

	bo = to_tegra_bo(gem);
	return &bo->base;
}

static int gr2d_submit(struct host1x_drm_context *context,
		       struct drm_tegra_submit *args, struct drm_device *drm,
		       struct drm_file *file)
{
	struct host1x_job *job;
	unsigned int num_cmdbufs = args->num_cmdbufs;
	unsigned int num_relocs = args->num_relocs;
	unsigned int num_waitchks = args->num_waitchks;
	struct drm_tegra_cmdbuf __user *cmdbufs =
		(void * __user)(uintptr_t)args->cmdbufs;
	struct drm_tegra_reloc __user *relocs =
		(void * __user)(uintptr_t)args->relocs;
	struct drm_tegra_waitchk __user *waitchks =
		(void * __user)(uintptr_t)args->waitchks;
	struct drm_tegra_syncpt syncpt;
	int err;

	/* We don't yet support other than one syncpt_incr struct per submit */
	if (args->num_syncpts != 1)
		return -EINVAL;

	job = host1x_job_alloc(context->channel, args->num_cmdbufs,
			       args->num_relocs, args->num_waitchks);
	if (!job)
		return -ENOMEM;

	job->num_relocs = args->num_relocs;
	job->num_waitchk = args->num_waitchks;
	job->client = (u32)args->context;
	job->class = context->client->class;
	job->serialize = true;

	while (num_cmdbufs) {
		struct drm_tegra_cmdbuf cmdbuf;
		struct host1x_bo *bo;

		err = copy_from_user(&cmdbuf, cmdbufs, sizeof(cmdbuf));
		if (err)
			goto fail;

		bo = host1x_bo_lookup(drm, file, cmdbuf.handle);
		if (!bo) {
			err = -ENOENT;
			goto fail;
		}

		host1x_job_add_gather(job, bo, cmdbuf.words, cmdbuf.offset);
		num_cmdbufs--;
		cmdbufs++;
	}

	err = copy_from_user(job->relocarray, relocs,
			     sizeof(*relocs) * num_relocs);
	if (err)
		goto fail;

	while (num_relocs--) {
		struct host1x_reloc *reloc = &job->relocarray[num_relocs];
		struct host1x_bo *cmdbuf, *target;

		cmdbuf = host1x_bo_lookup(drm, file, (u32)reloc->cmdbuf);
		target = host1x_bo_lookup(drm, file, (u32)reloc->target);

		reloc->cmdbuf = cmdbuf;
		reloc->target = target;

		if (!reloc->target || !reloc->cmdbuf) {
			err = -ENOENT;
			goto fail;
		}
	}

	err = copy_from_user(job->waitchk, waitchks,
			     sizeof(*waitchks) * num_waitchks);
	if (err)
		goto fail;

	err = copy_from_user(&syncpt, (void * __user)(uintptr_t)args->syncpts,
			     sizeof(syncpt));
	if (err)
		goto fail;

	job->syncpt_id = syncpt.id;
	job->syncpt_incrs = syncpt.incrs;
	job->timeout = 10000;
	job->is_addr_reg = gr2d_is_addr_reg;

	if (args->timeout && args->timeout < 10000)
		job->timeout = args->timeout;

	err = host1x_job_pin(job, context->client->dev);
	if (err)
		goto fail;

	err = host1x_job_submit(job);
	if (err)
		goto fail_submit;

	args->fence = job->syncpt_end;

	host1x_job_put(job);
	return 0;

fail_submit:
	host1x_job_unpin(job);
fail:
	host1x_job_put(job);
	return err;
}

static struct host1x_client_ops gr2d_client_ops = {
	.drm_init = gr2d_client_init,
	.drm_exit = gr2d_client_exit,
	.open_channel = gr2d_open_channel,
	.close_channel = gr2d_close_channel,
	.submit = gr2d_submit,
};

static void gr2d_init_addr_reg_map(struct device *dev, struct gr2d *gr2d)
{
	const u32 gr2d_addr_regs[] = {0x1a, 0x1b, 0x26, 0x2b, 0x2c, 0x2d, 0x31,
				      0x32, 0x48, 0x49, 0x4a, 0x4b, 0x4c};
	unsigned long *bitmap;
	int i;

	bitmap = devm_kzalloc(dev, DIV_ROUND_UP(256, BITS_PER_BYTE),
			      GFP_KERNEL);

	for (i = 0; i < ARRAY_SIZE(gr2d_addr_regs); ++i) {
		u32 reg = gr2d_addr_regs[i];
		bitmap[BIT_WORD(reg)] |= BIT_MASK(reg);
	}

	gr2d->addr_regs = bitmap;
}

static int gr2d_is_addr_reg(struct device *dev, u32 class, u32 reg)
{
	struct gr2d *gr2d = dev_get_drvdata(dev);

	switch (class) {
	case HOST1X_CLASS_HOST1X:
		return reg == 0x2b;
	case HOST1X_CLASS_GR2D:
	case HOST1X_CLASS_GR2D_SB:
		reg &= 0xff;
		if (gr2d->addr_regs[BIT_WORD(reg)] & BIT_MASK(reg))
			return 1;
	default:
		return 0;
	}
}

static const struct of_device_id gr2d_match[] = {
	{ .compatible = "nvidia,tegra30-gr2d" },
	{ .compatible = "nvidia,tegra20-gr2d" },
	{ },
};

static int gr2d_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct host1x_drm *host1x = host1x_get_drm_data(dev->parent);
	int err;
	struct gr2d *gr2d = NULL;
	struct host1x_syncpt **syncpts;

	gr2d = devm_kzalloc(dev, sizeof(*gr2d), GFP_KERNEL);
	if (!gr2d)
		return -ENOMEM;

	syncpts = devm_kzalloc(dev, sizeof(*syncpts), GFP_KERNEL);
	if (!syncpts)
		return -ENOMEM;

	gr2d->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(gr2d->clk)) {
		dev_err(dev, "cannot get clock\n");
		return PTR_ERR(gr2d->clk);
	}

	err = clk_prepare_enable(gr2d->clk);
	if (err) {
		dev_err(dev, "cannot turn on clock\n");
		return err;
	}

	gr2d->channel = host1x_channel_request(dev);
	if (!gr2d->channel)
		return -ENOMEM;

	*syncpts = host1x_syncpt_request(dev, 0);
	if (!(*syncpts)) {
		host1x_channel_free(gr2d->channel);
		return -ENOMEM;
	}

	gr2d->client.ops = &gr2d_client_ops;
	gr2d->client.dev = dev;
	gr2d->client.class = HOST1X_CLASS_GR2D;
	gr2d->client.syncpts = syncpts;
	gr2d->client.num_syncpts = 1;

	err = host1x_register_client(host1x, &gr2d->client);
	if (err < 0) {
		dev_err(dev, "failed to register host1x client: %d\n", err);
		return err;
	}

	gr2d_init_addr_reg_map(dev, gr2d);

	platform_set_drvdata(pdev, gr2d);

	return 0;
}

static int __exit gr2d_remove(struct platform_device *pdev)
{
	struct host1x_drm *host1x = host1x_get_drm_data(pdev->dev.parent);
	struct gr2d *gr2d = platform_get_drvdata(pdev);
	unsigned int i;
	int err;

	err = host1x_unregister_client(host1x, &gr2d->client);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to unregister client: %d\n", err);
		return err;
	}

	for (i = 0; i < gr2d->client.num_syncpts; i++)
		host1x_syncpt_free(gr2d->client.syncpts[i]);

	host1x_channel_free(gr2d->channel);
	clk_disable_unprepare(gr2d->clk);

	return 0;
}

struct platform_driver tegra_gr2d_driver = {
	.probe = gr2d_probe,
	.remove = __exit_p(gr2d_remove),
	.driver = {
		.owner = THIS_MODULE,
		.name = "gr2d",
		.of_match_table = gr2d_match,
	}
};
