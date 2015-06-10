/*
 * Copyright (c) 2014-2015 The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "mdp5_kms.h"
#include "mdp5_cfg.h"

struct mdp5_cfg_handler {
	int revision;
	struct mdp5_cfg config;
};

/* mdp5_cfg must be exposed (used in mdp5.xml.h) */
const struct mdp5_cfg_hw *mdp5_cfg = NULL;

const struct mdp5_cfg_hw msm8x74_config = {
	.name = "msm8x74",
	.mdp = {
		.count = 1,
		.base = { 0x00100 },
	},
	.smp = {
		.mmb_count = 22,
		.mmb_size = 4096,
		.clients = {
			[SSPP_VIG0] =  1, [SSPP_VIG1] =  4, [SSPP_VIG2] =  7,
			[SSPP_DMA0] = 10, [SSPP_DMA1] = 13,
			[SSPP_RGB0] = 16, [SSPP_RGB1] = 17, [SSPP_RGB2] = 18,
		},
	},
	.ctl = {
		.count = 5,
		.base = { 0x00600, 0x00700, 0x00800, 0x00900, 0x00a00 },
		.flush_hw_mask = 0x0003ffff,
	},
	.pipe_vig = {
		.count = 3,
		.base = { 0x01200, 0x01600, 0x01a00 },
	},
	.pipe_rgb = {
		.count = 3,
		.base = { 0x01e00, 0x02200, 0x02600 },
	},
	.pipe_dma = {
		.count = 2,
		.base = { 0x02a00, 0x02e00 },
	},
	.lm = {
		.count = 5,
		.base = { 0x03200, 0x03600, 0x03a00, 0x03e00, 0x04200 },
		.nb_stages = 5,
	},
	.dspp = {
		.count = 3,
		.base = { 0x04600, 0x04a00, 0x04e00 },
	},
	.ad = {
		.count = 2,
		.base = { 0x13100, 0x13300 }, /* NOTE: no ad in v1.0 */
	},
	.pp = {
		.count = 3,
		.base = { 0x12d00, 0x12e00, 0x12f00 },
	},
	.intf = {
		.base = { 0x12500, 0x12700, 0x12900, 0x12b00 },
		.connect = {
			[0] = INTF_eDP,
			[1] = INTF_DSI,
			[2] = INTF_DSI,
			[3] = INTF_HDMI,
		},
	},
	.max_clk = 200000000,
};

const struct mdp5_cfg_hw apq8084_config = {
	.name = "apq8084",
	.mdp = {
		.count = 1,
		.base = { 0x00100 },
	},
	.smp = {
		.mmb_count = 44,
		.mmb_size = 8192,
		.clients = {
			[SSPP_VIG0] =  1, [SSPP_VIG1] =  4,
			[SSPP_VIG2] =  7, [SSPP_VIG3] = 19,
			[SSPP_DMA0] = 10, [SSPP_DMA1] = 13,
			[SSPP_RGB0] = 16, [SSPP_RGB1] = 17,
			[SSPP_RGB2] = 18, [SSPP_RGB3] = 22,
		},
		.reserved_state[0] = GENMASK(7, 0),	/* first 8 MMBs */
		.reserved = {
			/* Two SMP blocks are statically tied to RGB pipes: */
			[16] = 2, [17] = 2, [18] = 2, [22] = 2,
		},
	},
	.ctl = {
		.count = 5,
		.base = { 0x00600, 0x00700, 0x00800, 0x00900, 0x00a00 },
		.flush_hw_mask = 0x003fffff,
	},
	.pipe_vig = {
		.count = 4,
		.base = { 0x01200, 0x01600, 0x01a00, 0x01e00 },
	},
	.pipe_rgb = {
		.count = 4,
		.base = { 0x02200, 0x02600, 0x02a00, 0x02e00 },
	},
	.pipe_dma = {
		.count = 2,
		.base = { 0x03200, 0x03600 },
	},
	.lm = {
		.count = 6,
		.base = { 0x03a00, 0x03e00, 0x04200, 0x04600, 0x04a00, 0x04e00 },
		.nb_stages = 5,
	},
	.dspp = {
		.count = 4,
		.base = { 0x05200, 0x05600, 0x05a00, 0x05e00 },

	},
	.ad = {
		.count = 3,
		.base = { 0x13500, 0x13700, 0x13900 },
	},
	.pp = {
		.count = 4,
		.base = { 0x12f00, 0x13000, 0x13100, 0x13200 },
	},
	.intf = {
		.base = { 0x12500, 0x12700, 0x12900, 0x12b00, 0x12d00 },
		.connect = {
			[0] = INTF_eDP,
			[1] = INTF_DSI,
			[2] = INTF_DSI,
			[3] = INTF_HDMI,
		},
	},
	.max_clk = 320000000,
};

const struct mdp5_cfg_hw msm8x16_config = {
	.name = "msm8x16",
	.mdp = {
		.count = 1,
		.base = { 0x01000 },
	},
	.smp = {
		.mmb_count = 8,
		.mmb_size = 8192,
		.clients = {
			[SSPP_VIG0] = 1, [SSPP_DMA0] = 4,
			[SSPP_RGB0] = 7, [SSPP_RGB1] = 8,
		},
	},
	.ctl = {
		.count = 5,
		.base = { 0x02000, 0x02200, 0x02400, 0x02600, 0x02800 },
		.flush_hw_mask = 0x4003ffff,
	},
	.pipe_vig = {
		.count = 1,
		.base = { 0x05000 },
	},
	.pipe_rgb = {
		.count = 2,
		.base = { 0x15000, 0x17000 },
	},
	.pipe_dma = {
		.count = 1,
		.base = { 0x25000 },
	},
	.lm = {
		.count = 2, /* LM0 and LM3 */
		.base = { 0x45000, 0x48000 },
		.nb_stages = 5,
	},
	.dspp = {
		.count = 1,
		.base = { 0x55000 },

	},
	.intf = {
		.base = { 0x00000, 0x6b800 },
		.connect = {
			[0] = INTF_DISABLED,
			[1] = INTF_DSI,
		},
	},
	.max_clk = 320000000,
};

static const struct mdp5_cfg_handler cfg_handlers[] = {
	{ .revision = 0, .config = { .hw = &msm8x74_config } },
	{ .revision = 2, .config = { .hw = &msm8x74_config } },
	{ .revision = 3, .config = { .hw = &apq8084_config } },
	{ .revision = 6, .config = { .hw = &msm8x16_config } },
};


static struct mdp5_cfg_platform *mdp5_get_config(struct platform_device *dev);

const struct mdp5_cfg_hw *mdp5_cfg_get_hw_config(struct mdp5_cfg_handler *cfg_handler)
{
	return cfg_handler->config.hw;
}

struct mdp5_cfg *mdp5_cfg_get_config(struct mdp5_cfg_handler *cfg_handler)
{
	return &cfg_handler->config;
}

int mdp5_cfg_get_hw_rev(struct mdp5_cfg_handler *cfg_handler)
{
	return cfg_handler->revision;
}

void mdp5_cfg_destroy(struct mdp5_cfg_handler *cfg_handler)
{
	kfree(cfg_handler);
}

struct mdp5_cfg_handler *mdp5_cfg_init(struct mdp5_kms *mdp5_kms,
		uint32_t major, uint32_t minor)
{
	struct drm_device *dev = mdp5_kms->dev;
	struct platform_device *pdev = dev->platformdev;
	struct mdp5_cfg_handler *cfg_handler;
	struct mdp5_cfg_platform *pconfig;
	int i, ret = 0;

	cfg_handler = kzalloc(sizeof(*cfg_handler), GFP_KERNEL);
	if (unlikely(!cfg_handler)) {
		ret = -ENOMEM;
		goto fail;
	}

	if (major != 1) {
		dev_err(dev->dev, "unexpected MDP major version: v%d.%d\n",
				major, minor);
		ret = -ENXIO;
		goto fail;
	}

	/* only after mdp5_cfg global pointer's init can we access the hw */
	for (i = 0; i < ARRAY_SIZE(cfg_handlers); i++) {
		if (cfg_handlers[i].revision != minor)
			continue;
		mdp5_cfg = cfg_handlers[i].config.hw;

		break;
	}
	if (unlikely(!mdp5_cfg)) {
		dev_err(dev->dev, "unexpected MDP minor revision: v%d.%d\n",
				major, minor);
		ret = -ENXIO;
		goto fail;
	}

	cfg_handler->revision = minor;
	cfg_handler->config.hw = mdp5_cfg;

	pconfig = mdp5_get_config(pdev);
	memcpy(&cfg_handler->config.platform, pconfig, sizeof(*pconfig));

	DBG("MDP5: %s hw config selected", mdp5_cfg->name);

	return cfg_handler;

fail:
	if (cfg_handler)
		mdp5_cfg_destroy(cfg_handler);

	return NULL;
}

static struct mdp5_cfg_platform *mdp5_get_config(struct platform_device *dev)
{
	static struct mdp5_cfg_platform config = {};
#ifdef CONFIG_OF
	/* TODO */
#endif
	config.iommu = iommu_domain_alloc(&platform_bus_type);

	return &config;
}
