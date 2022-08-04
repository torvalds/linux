// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2016 NVIDIA Corporation
 */

#include <linux/reset-controller.h>

#include <soc/tegra/bpmp.h>
#include <soc/tegra/bpmp-abi.h>

static struct tegra_bpmp *to_tegra_bpmp(struct reset_controller_dev *rstc)
{
	return container_of(rstc, struct tegra_bpmp, rstc);
}

static int tegra_bpmp_reset_common(struct reset_controller_dev *rstc,
				   enum mrq_reset_commands command,
				   unsigned int id)
{
	struct tegra_bpmp *bpmp = to_tegra_bpmp(rstc);
	struct mrq_reset_request request;
	struct tegra_bpmp_message msg;
	int err;

	memset(&request, 0, sizeof(request));
	request.cmd = command;
	request.reset_id = id;

	memset(&msg, 0, sizeof(msg));
	msg.mrq = MRQ_RESET;
	msg.tx.data = &request;
	msg.tx.size = sizeof(request);

	err = tegra_bpmp_transfer(bpmp, &msg);
	if (err)
		return err;
	if (msg.rx.ret)
		return -EINVAL;

	return 0;
}

static int tegra_bpmp_reset_module(struct reset_controller_dev *rstc,
				   unsigned long id)
{
	return tegra_bpmp_reset_common(rstc, CMD_RESET_MODULE, id);
}

static int tegra_bpmp_reset_assert(struct reset_controller_dev *rstc,
				   unsigned long id)
{
	return tegra_bpmp_reset_common(rstc, CMD_RESET_ASSERT, id);
}

static int tegra_bpmp_reset_deassert(struct reset_controller_dev *rstc,
				     unsigned long id)
{
	return tegra_bpmp_reset_common(rstc, CMD_RESET_DEASSERT, id);
}

static const struct reset_control_ops tegra_bpmp_reset_ops = {
	.reset = tegra_bpmp_reset_module,
	.assert = tegra_bpmp_reset_assert,
	.deassert = tegra_bpmp_reset_deassert,
};

int tegra_bpmp_init_resets(struct tegra_bpmp *bpmp)
{
	bpmp->rstc.ops = &tegra_bpmp_reset_ops;
	bpmp->rstc.owner = THIS_MODULE;
	bpmp->rstc.of_node = bpmp->dev->of_node;
	bpmp->rstc.nr_resets = bpmp->soc->num_resets;

	return devm_reset_controller_register(bpmp->dev, &bpmp->rstc);
}
