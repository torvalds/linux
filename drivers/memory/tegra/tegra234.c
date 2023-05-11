// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022-2023, NVIDIA CORPORATION.  All rights reserved.
 */

#include <soc/tegra/mc.h>

#include <dt-bindings/memory/tegra234-mc.h>
#include <linux/interconnect.h>
#include <linux/tegra-icc.h>

#include <soc/tegra/bpmp.h>
#include "mc.h"

static const struct tegra_mc_client tegra234_mc_clients[] = {
	{
		.id = TEGRA234_MEMORY_CLIENT_MGBEARD,
		.name = "mgbeard",
		.bpmp_id = TEGRA_ICC_BPMP_EQOS,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA234_SID_MGBE,
		.regs = {
			.sid = {
				.override = 0x2c0,
				.security = 0x2c4,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_MGBEBRD,
		.name = "mgbebrd",
		.bpmp_id = TEGRA_ICC_BPMP_EQOS,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA234_SID_MGBE_VF1,
		.regs = {
			.sid = {
				.override = 0x2c8,
				.security = 0x2cc,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_MGBECRD,
		.name = "mgbecrd",
		.bpmp_id = TEGRA_ICC_BPMP_EQOS,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA234_SID_MGBE_VF2,
		.regs = {
			.sid = {
				.override = 0x2d0,
				.security = 0x2d4,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_MGBEDRD,
		.name = "mgbedrd",
		.bpmp_id = TEGRA_ICC_BPMP_EQOS,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA234_SID_MGBE_VF3,
		.regs = {
			.sid = {
				.override = 0x2d8,
				.security = 0x2dc,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_MGBEAWR,
		.bpmp_id = TEGRA_ICC_BPMP_EQOS,
		.type = TEGRA_ICC_NISO,
		.name = "mgbeawr",
		.sid = TEGRA234_SID_MGBE,
		.regs = {
			.sid = {
				.override = 0x2e0,
				.security = 0x2e4,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_MGBEBWR,
		.name = "mgbebwr",
		.bpmp_id = TEGRA_ICC_BPMP_EQOS,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA234_SID_MGBE_VF1,
		.regs = {
			.sid = {
				.override = 0x2f8,
				.security = 0x2fc,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_MGBECWR,
		.name = "mgbecwr",
		.bpmp_id = TEGRA_ICC_BPMP_EQOS,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA234_SID_MGBE_VF2,
		.regs = {
			.sid = {
				.override = 0x308,
				.security = 0x30c,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_SDMMCRAB,
		.name = "sdmmcrab",
		.bpmp_id = TEGRA_ICC_BPMP_SDMMC_4,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA234_SID_SDMMC4,
		.regs = {
			.sid = {
				.override = 0x318,
				.security = 0x31c,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_MGBEDWR,
		.name = "mgbedwr",
		.bpmp_id = TEGRA_ICC_BPMP_EQOS,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA234_SID_MGBE_VF3,
		.regs = {
			.sid = {
				.override = 0x328,
				.security = 0x32c,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_SDMMCWAB,
		.name = "sdmmcwab",
		.bpmp_id = TEGRA_ICC_BPMP_SDMMC_4,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA234_SID_SDMMC4,
		.regs = {
			.sid = {
				.override = 0x338,
				.security = 0x33c,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_BPMPR,
		.name = "bpmpr",
		.sid = TEGRA234_SID_BPMP,
		.regs = {
			.sid = {
				.override = 0x498,
				.security = 0x49c,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_BPMPW,
		.name = "bpmpw",
		.sid = TEGRA234_SID_BPMP,
		.regs = {
			.sid = {
				.override = 0x4a0,
				.security = 0x4a4,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_BPMPDMAR,
		.name = "bpmpdmar",
		.sid = TEGRA234_SID_BPMP,
		.regs = {
			.sid = {
				.override = 0x4a8,
				.security = 0x4ac,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_BPMPDMAW,
		.name = "bpmpdmaw",
		.sid = TEGRA234_SID_BPMP,
		.regs = {
			.sid = {
				.override = 0x4b0,
				.security = 0x4b4,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_APEDMAR,
		.name = "apedmar",
		.bpmp_id = TEGRA_ICC_BPMP_APEDMA,
		.type = TEGRA_ICC_ISO_AUDIO,
		.sid = TEGRA234_SID_APE,
		.regs = {
			.sid = {
				.override = 0x4f8,
				.security = 0x4fc,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_APEDMAW,
		.name = "apedmaw",
		.bpmp_id = TEGRA_ICC_BPMP_APEDMA,
		.type = TEGRA_ICC_ISO_AUDIO,
		.sid = TEGRA234_SID_APE,
		.regs = {
			.sid = {
				.override = 0x500,
				.security = 0x504,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_DLA0RDA,
		.name = "dla0rda",
		.sid = TEGRA234_SID_NVDLA0,
		.regs = {
			.sid = {
				.override = 0x5f0,
				.security = 0x5f4,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_DLA0FALRDB,
		.name = "dla0falrdb",
		.sid = TEGRA234_SID_NVDLA0,
		.regs = {
			.sid = {
				.override = 0x5f8,
				.security = 0x5fc,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_DLA0WRA,
		.name = "dla0wra",
		.sid = TEGRA234_SID_NVDLA0,
		.regs = {
			.sid = {
				.override = 0x600,
				.security = 0x604,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_DLA0RDB,
		.name = "dla0rdb",
		.sid = TEGRA234_SID_NVDLA0,
		.regs = {
			.sid = {
				.override = 0x160,
				.security = 0x164,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_DLA0RDA1,
		.name = "dla0rda1",
		.sid = TEGRA234_SID_NVDLA0,
		.regs = {
			.sid = {
				.override = 0x748,
				.security = 0x74c,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_DLA0FALWRB,
		.name = "dla0falwrb",
		.sid = TEGRA234_SID_NVDLA0,
		.regs = {
			.sid = {
				.override = 0x608,
				.security = 0x60c,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_DLA0RDB1,
		.name = "dla0rdb1",
		.sid = TEGRA234_SID_NVDLA0,
		.regs = {
			.sid = {
				.override = 0x168,
				.security = 0x16c,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_DLA0WRB,
		.name = "dla0wrb",
		.sid = TEGRA234_SID_NVDLA0,
		.regs = {
			.sid = {
				.override = 0x170,
				.security = 0x174,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_DLA1RDA,
		.name = "dla0rda",
		.sid = TEGRA234_SID_NVDLA1,
		.regs = {
			.sid = {
				.override = 0x610,
				.security = 0x614,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_DLA1FALRDB,
		.name = "dla0falrdb",
		.sid = TEGRA234_SID_NVDLA1,
		.regs = {
			.sid = {
				.override = 0x618,
				.security = 0x61c,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_DLA1WRA,
		.name = "dla0wra",
		.sid = TEGRA234_SID_NVDLA1,
		.regs = {
			.sid = {
				.override = 0x620,
				.security = 0x624,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_DLA1RDB,
		.name = "dla0rdb",
		.sid = TEGRA234_SID_NVDLA1,
		.regs = {
			.sid = {
				.override = 0x178,
				.security = 0x17c,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_DLA1RDA1,
		.name = "dla0rda1",
		.sid = TEGRA234_SID_NVDLA1,
		.regs = {
			.sid = {
				.override = 0x750,
				.security = 0x754,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_DLA1FALWRB,
		.name = "dla0falwrb",
		.sid = TEGRA234_SID_NVDLA1,
		.regs = {
			.sid = {
				.override = 0x628,
				.security = 0x62c,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_DLA1RDB1,
		.name = "dla0rdb1",
		.sid = TEGRA234_SID_NVDLA1,
		.regs = {
			.sid = {
				.override = 0x370,
				.security = 0x374,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_DLA1WRB,
		.name = "dla0wrb",
		.sid = TEGRA234_SID_NVDLA1,
		.regs = {
			.sid = {
				.override = 0x378,
				.security = 0x37c,
			},
		},
	},
};

/*
 * tegra234_mc_icc_set() - Pass MC client info to the BPMP-FW
 * @src: ICC node for Memory Controller's (MC) Client
 * @dst: ICC node for Memory Controller (MC)
 *
 * Passing the current request info from the MC to the BPMP-FW where
 * LA and PTSA registers are accessed and the final EMC freq is set
 * based on client_id, type, latency and bandwidth.
 * icc_set_bw() makes set_bw calls for both MC and EMC providers in
 * sequence. Both the calls are protected by 'mutex_lock(&icc_lock)'.
 * So, the data passed won't be updated by concurrent set calls from
 * other clients.
 */
static int tegra234_mc_icc_set(struct icc_node *src, struct icc_node *dst)
{
	struct tegra_mc *mc = icc_provider_to_tegra_mc(dst->provider);
	struct mrq_bwmgr_int_request bwmgr_req = { 0 };
	struct mrq_bwmgr_int_response bwmgr_resp = { 0 };
	const struct tegra_mc_client *pclient = src->data;
	struct tegra_bpmp_message msg;
	int ret;

	/*
	 * Same Src and Dst node will happen during boot from icc_node_add().
	 * This can be used to pre-initialize and set bandwidth for all clients
	 * before their drivers are loaded. We are skipping this case as for us,
	 * the pre-initialization already happened in Bootloader(MB2) and BPMP-FW.
	 */
	if (src->id == dst->id)
		return 0;

	if (!mc->bwmgr_mrq_supported)
		return -EINVAL;

	if (!mc->bpmp) {
		dev_err(mc->dev, "BPMP reference NULL\n");
		return -ENOENT;
	}

	if (pclient->type == TEGRA_ICC_NISO)
		bwmgr_req.bwmgr_calc_set_req.niso_bw = src->avg_bw;
	else
		bwmgr_req.bwmgr_calc_set_req.iso_bw = src->avg_bw;

	bwmgr_req.bwmgr_calc_set_req.client_id = pclient->bpmp_id;

	bwmgr_req.cmd = CMD_BWMGR_INT_CALC_AND_SET;
	bwmgr_req.bwmgr_calc_set_req.mc_floor = src->peak_bw;
	bwmgr_req.bwmgr_calc_set_req.floor_unit = BWMGR_INT_UNIT_KBPS;

	memset(&msg, 0, sizeof(msg));
	msg.mrq = MRQ_BWMGR_INT;
	msg.tx.data = &bwmgr_req;
	msg.tx.size = sizeof(bwmgr_req);
	msg.rx.data = &bwmgr_resp;
	msg.rx.size = sizeof(bwmgr_resp);

	ret = tegra_bpmp_transfer(mc->bpmp, &msg);
	if (ret < 0) {
		dev_err(mc->dev, "BPMP transfer failed: %d\n", ret);
		goto error;
	}
	if (msg.rx.ret < 0) {
		pr_err("failed to set bandwidth for %u: %d\n",
		       bwmgr_req.bwmgr_calc_set_req.client_id, msg.rx.ret);
		ret = -EINVAL;
	}

error:
	return ret;
}

static struct icc_node*
tegra234_mc_of_icc_xlate(struct of_phandle_args *spec, void *data)
{
	struct tegra_mc *mc = icc_provider_to_tegra_mc(data);
	unsigned int cl_id = spec->args[0];
	struct icc_node *node;

	list_for_each_entry(node, &mc->provider.nodes, node_list) {
		if (node->id != cl_id)
			continue;

		return node;
	}

	/*
	 * If a client driver calls devm_of_icc_get() before the MC driver
	 * is probed, then return EPROBE_DEFER to the client driver.
	 */
	return ERR_PTR(-EPROBE_DEFER);
}

static int tegra234_mc_icc_get_init_bw(struct icc_node *node, u32 *avg, u32 *peak)
{
	*avg = 0;
	*peak = 0;

	return 0;
}

static const struct tegra_mc_icc_ops tegra234_mc_icc_ops = {
	.xlate = tegra234_mc_of_icc_xlate,
	.aggregate = icc_std_aggregate,
	.get_bw = tegra234_mc_icc_get_init_bw,
	.set = tegra234_mc_icc_set,
};

const struct tegra_mc_soc tegra234_mc_soc = {
	.num_clients = ARRAY_SIZE(tegra234_mc_clients),
	.clients = tegra234_mc_clients,
	.num_address_bits = 40,
	.num_channels = 16,
	.client_id_mask = 0x1ff,
	.intmask = MC_INT_DECERR_ROUTE_SANITY |
		   MC_INT_DECERR_GENERALIZED_CARVEOUT | MC_INT_DECERR_MTS |
		   MC_INT_SECERR_SEC | MC_INT_DECERR_VPR |
		   MC_INT_SECURITY_VIOLATION | MC_INT_DECERR_EMEM,
	.has_addr_hi_reg = true,
	.ops = &tegra186_mc_ops,
	.icc_ops = &tegra234_mc_icc_ops,
	.ch_intmask = 0x0000ff00,
	.global_intstatus_channel_shift = 8,
	/*
	 * Additionally, there are lite carveouts but those are not currently
	 * supported.
	 */
	.num_carveouts = 32,
};
