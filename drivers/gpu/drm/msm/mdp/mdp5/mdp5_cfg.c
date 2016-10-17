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

const struct mdp5_cfg_hw msm8x74v1_config = {
	.name = "msm8x74v1",
	.mdp = {
		.count = 1,
		.caps = MDP_CAP_SMP |
			0,
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
		.base = { 0x00500, 0x00600, 0x00700, 0x00800, 0x00900 },
		.flush_hw_mask = 0x0003ffff,
	},
	.pipe_vig = {
		.count = 3,
		.base = { 0x01100, 0x01500, 0x01900 },
		.caps = MDP_PIPE_CAP_HFLIP |
			MDP_PIPE_CAP_VFLIP |
			MDP_PIPE_CAP_SCALE |
			MDP_PIPE_CAP_CSC   |
			0,
	},
	.pipe_rgb = {
		.count = 3,
		.base = { 0x01d00, 0x02100, 0x02500 },
		.caps = MDP_PIPE_CAP_HFLIP |
			MDP_PIPE_CAP_VFLIP |
			MDP_PIPE_CAP_SCALE |
			0,
	},
	.pipe_dma = {
		.count = 2,
		.base = { 0x02900, 0x02d00 },
		.caps = MDP_PIPE_CAP_HFLIP |
			MDP_PIPE_CAP_VFLIP |
			0,
	},
	.lm = {
		.count = 5,
		.base = { 0x03100, 0x03500, 0x03900, 0x03d00, 0x04100 },
		.nb_stages = 5,
	},
	.dspp = {
		.count = 3,
		.base = { 0x04500, 0x04900, 0x04d00 },
	},
	.pp = {
		.count = 3,
		.base = { 0x21a00, 0x21b00, 0x21c00 },
	},
	.intf = {
		.base = { 0x21000, 0x21200, 0x21400, 0x21600 },
		.connect = {
			[0] = INTF_eDP,
			[1] = INTF_DSI,
			[2] = INTF_DSI,
			[3] = INTF_HDMI,
		},
	},
	.max_clk = 200000000,
};

const struct mdp5_cfg_hw msm8x74v2_config = {
	.name = "msm8x74",
	.mdp = {
		.count = 1,
		.caps = MDP_CAP_SMP |
			0,
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
		.base = { 0x00500, 0x00600, 0x00700, 0x00800, 0x00900 },
		.flush_hw_mask = 0x0003ffff,
	},
	.pipe_vig = {
		.count = 3,
		.base = { 0x01100, 0x01500, 0x01900 },
		.caps = MDP_PIPE_CAP_HFLIP | MDP_PIPE_CAP_VFLIP |
				MDP_PIPE_CAP_SCALE | MDP_PIPE_CAP_CSC |
				MDP_PIPE_CAP_DECIMATION,
	},
	.pipe_rgb = {
		.count = 3,
		.base = { 0x01d00, 0x02100, 0x02500 },
		.caps = MDP_PIPE_CAP_HFLIP | MDP_PIPE_CAP_VFLIP |
				MDP_PIPE_CAP_SCALE | MDP_PIPE_CAP_DECIMATION,
	},
	.pipe_dma = {
		.count = 2,
		.base = { 0x02900, 0x02d00 },
		.caps = MDP_PIPE_CAP_HFLIP | MDP_PIPE_CAP_VFLIP,
	},
	.lm = {
		.count = 5,
		.base = { 0x03100, 0x03500, 0x03900, 0x03d00, 0x04100 },
		.nb_stages = 5,
		.max_width = 2048,
		.max_height = 0xFFFF,
	},
	.dspp = {
		.count = 3,
		.base = { 0x04500, 0x04900, 0x04d00 },
	},
	.ad = {
		.count = 2,
		.base = { 0x13000, 0x13200 },
	},
	.pp = {
		.count = 3,
		.base = { 0x12c00, 0x12d00, 0x12e00 },
	},
	.intf = {
		.base = { 0x12400, 0x12600, 0x12800, 0x12a00 },
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
		.caps = MDP_CAP_SMP |
			0,
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
		.base = { 0x00500, 0x00600, 0x00700, 0x00800, 0x00900 },
		.flush_hw_mask = 0x003fffff,
	},
	.pipe_vig = {
		.count = 4,
		.base = { 0x01100, 0x01500, 0x01900, 0x01d00 },
		.caps = MDP_PIPE_CAP_HFLIP | MDP_PIPE_CAP_VFLIP |
				MDP_PIPE_CAP_SCALE | MDP_PIPE_CAP_CSC |
				MDP_PIPE_CAP_DECIMATION,
	},
	.pipe_rgb = {
		.count = 4,
		.base = { 0x02100, 0x02500, 0x02900, 0x02d00 },
		.caps = MDP_PIPE_CAP_HFLIP | MDP_PIPE_CAP_VFLIP |
				MDP_PIPE_CAP_SCALE | MDP_PIPE_CAP_DECIMATION,
	},
	.pipe_dma = {
		.count = 2,
		.base = { 0x03100, 0x03500 },
		.caps = MDP_PIPE_CAP_HFLIP | MDP_PIPE_CAP_VFLIP,
	},
	.lm = {
		.count = 6,
		.base = { 0x03900, 0x03d00, 0x04100, 0x04500, 0x04900, 0x04d00 },
		.nb_stages = 5,
		.max_width = 2048,
		.max_height = 0xFFFF,
	},
	.dspp = {
		.count = 4,
		.base = { 0x05100, 0x05500, 0x05900, 0x05d00 },

	},
	.ad = {
		.count = 3,
		.base = { 0x13400, 0x13600, 0x13800 },
	},
	.pp = {
		.count = 4,
		.base = { 0x12e00, 0x12f00, 0x13000, 0x13100 },
	},
	.intf = {
		.base = { 0x12400, 0x12600, 0x12800, 0x12a00, 0x12c00 },
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
		.base = { 0x0 },
		.caps = MDP_CAP_SMP |
			0,
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
		.base = { 0x01000, 0x01200, 0x01400, 0x01600, 0x01800 },
		.flush_hw_mask = 0x4003ffff,
	},
	.pipe_vig = {
		.count = 1,
		.base = { 0x04000 },
		.caps = MDP_PIPE_CAP_HFLIP | MDP_PIPE_CAP_VFLIP |
				MDP_PIPE_CAP_SCALE | MDP_PIPE_CAP_CSC |
				MDP_PIPE_CAP_DECIMATION,
	},
	.pipe_rgb = {
		.count = 2,
		.base = { 0x14000, 0x16000 },
		.caps = MDP_PIPE_CAP_HFLIP | MDP_PIPE_CAP_VFLIP |
				MDP_PIPE_CAP_DECIMATION,
	},
	.pipe_dma = {
		.count = 1,
		.base = { 0x24000 },
		.caps = MDP_PIPE_CAP_HFLIP | MDP_PIPE_CAP_VFLIP,
	},
	.lm = {
		.count = 2, /* LM0 and LM3 */
		.base = { 0x44000, 0x47000 },
		.nb_stages = 5,
		.max_width = 2048,
		.max_height = 0xFFFF,
	},
	.dspp = {
		.count = 1,
		.base = { 0x54000 },

	},
	.intf = {
		.base = { 0x00000, 0x6a800 },
		.connect = {
			[0] = INTF_DISABLED,
			[1] = INTF_DSI,
		},
	},
	.max_clk = 320000000,
};

const struct mdp5_cfg_hw msm8x94_config = {
	.name = "msm8x94",
	.mdp = {
		.count = 1,
		.caps = MDP_CAP_SMP |
			0,
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
		.reserved_state[0] = GENMASK(23, 0),	/* first 24 MMBs */
		.reserved = {
			 [1] = 1,  [4] = 1,  [7] = 1, [19] = 1,
			[16] = 5, [17] = 5, [18] = 5, [22] = 5,
		},
	},
	.ctl = {
		.count = 5,
		.base = { 0x01000, 0x01200, 0x01400, 0x01600, 0x01800 },
		.flush_hw_mask = 0xf0ffffff,
	},
	.pipe_vig = {
		.count = 4,
		.base = { 0x04000, 0x06000, 0x08000, 0x0a000 },
		.caps = MDP_PIPE_CAP_HFLIP | MDP_PIPE_CAP_VFLIP |
				MDP_PIPE_CAP_SCALE | MDP_PIPE_CAP_CSC |
				MDP_PIPE_CAP_DECIMATION,
	},
	.pipe_rgb = {
		.count = 4,
		.base = { 0x14000, 0x16000, 0x18000, 0x1a000 },
		.caps = MDP_PIPE_CAP_HFLIP | MDP_PIPE_CAP_VFLIP |
				MDP_PIPE_CAP_SCALE | MDP_PIPE_CAP_DECIMATION,
	},
	.pipe_dma = {
		.count = 2,
		.base = { 0x24000, 0x26000 },
		.caps = MDP_PIPE_CAP_HFLIP | MDP_PIPE_CAP_VFLIP,
	},
	.lm = {
		.count = 6,
		.base = { 0x44000, 0x45000, 0x46000, 0x47000, 0x48000, 0x49000 },
		.nb_stages = 8,
		.max_width = 2048,
		.max_height = 0xFFFF,
	},
	.dspp = {
		.count = 4,
		.base = { 0x54000, 0x56000, 0x58000, 0x5a000 },

	},
	.ad = {
		.count = 3,
		.base = { 0x78000, 0x78800, 0x79000 },
	},
	.pp = {
		.count = 4,
		.base = { 0x70000, 0x70800, 0x71000, 0x71800 },
	},
	.intf = {
		.base = { 0x6a000, 0x6a800, 0x6b000, 0x6b800, 0x6c000 },
		.connect = {
			[0] = INTF_DISABLED,
			[1] = INTF_DSI,
			[2] = INTF_DSI,
			[3] = INTF_HDMI,
		},
	},
	.max_clk = 400000000,
};

const struct mdp5_cfg_hw msm8x96_config = {
	.name = "msm8x96",
	.mdp = {
		.count = 1,
		.caps = MDP_CAP_DSC |
			MDP_CAP_CDM |
			0,
	},
	.ctl = {
		.count = 5,
		.base = { 0x01000, 0x01200, 0x01400, 0x01600, 0x01800 },
		.flush_hw_mask = 0xf4ffffff,
	},
	.pipe_vig = {
		.count = 4,
		.base = { 0x04000, 0x06000, 0x08000, 0x0a000 },
		.caps = MDP_PIPE_CAP_HFLIP	|
			MDP_PIPE_CAP_VFLIP	|
			MDP_PIPE_CAP_SCALE	|
			MDP_PIPE_CAP_CSC	|
			MDP_PIPE_CAP_DECIMATION	|
			MDP_PIPE_CAP_SW_PIX_EXT	|
			0,
	},
	.pipe_rgb = {
		.count = 4,
		.base = { 0x14000, 0x16000, 0x18000, 0x1a000 },
		.caps = MDP_PIPE_CAP_HFLIP	|
			MDP_PIPE_CAP_VFLIP	|
			MDP_PIPE_CAP_SCALE	|
			MDP_PIPE_CAP_DECIMATION	|
			MDP_PIPE_CAP_SW_PIX_EXT	|
			0,
	},
	.pipe_dma = {
		.count = 2,
		.base = { 0x24000, 0x26000 },
		.caps = MDP_PIPE_CAP_HFLIP	|
			MDP_PIPE_CAP_VFLIP	|
			MDP_PIPE_CAP_SW_PIX_EXT	|
			0,
	},
	.lm = {
		.count = 6,
		.base = { 0x44000, 0x45000, 0x46000, 0x47000, 0x48000, 0x49000 },
		.nb_stages = 8,
		.max_width = 2560,
		.max_height = 0xFFFF,
	},
	.dspp = {
		.count = 2,
		.base = { 0x54000, 0x56000 },
	},
	.ad = {
		.count = 3,
		.base = { 0x78000, 0x78800, 0x79000 },
	},
	.pp = {
		.count = 4,
		.base = { 0x70000, 0x70800, 0x71000, 0x71800 },
	},
	.cdm = {
		.count = 1,
		.base = { 0x79200 },
	},
	.dsc = {
		.count = 2,
		.base = { 0x80000, 0x80400 },
	},
	.intf = {
		.base = { 0x6a000, 0x6a800, 0x6b000, 0x6b800, 0x6c000 },
		.connect = {
			[0] = INTF_DISABLED,
			[1] = INTF_DSI,
			[2] = INTF_DSI,
			[3] = INTF_HDMI,
		},
	},
	.max_clk = 412500000,
};

static const struct mdp5_cfg_handler cfg_handlers[] = {
	{ .revision = 0, .config = { .hw = &msm8x74v1_config } },
	{ .revision = 2, .config = { .hw = &msm8x74v2_config } },
	{ .revision = 3, .config = { .hw = &apq8084_config } },
	{ .revision = 6, .config = { .hw = &msm8x16_config } },
	{ .revision = 9, .config = { .hw = &msm8x94_config } },
	{ .revision = 7, .config = { .hw = &msm8x96_config } },
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

	config.iommu = iommu_domain_alloc(&platform_bus_type);

	return &config;
}
