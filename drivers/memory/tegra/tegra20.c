// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 NVIDIA CORPORATION.  All rights reserved.
 */

#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/string.h>

#include <dt-bindings/memory/tegra20-mc.h>

#include "mc.h"

#define MC_STAT_CONTROL				0x90
#define MC_STAT_EMC_CLOCK_LIMIT			0xa0
#define MC_STAT_EMC_CLOCKS			0xa4
#define MC_STAT_EMC_CONTROL_0			0xa8
#define MC_STAT_EMC_CONTROL_1			0xac
#define MC_STAT_EMC_COUNT_0			0xb8
#define MC_STAT_EMC_COUNT_1			0xbc

#define MC_STAT_CONTROL_CLIENT_ID		GENMASK(13,  8)
#define MC_STAT_CONTROL_EVENT			GENMASK(23, 16)
#define MC_STAT_CONTROL_PRI_EVENT		GENMASK(25, 24)
#define MC_STAT_CONTROL_FILTER_CLIENT_ENABLE	GENMASK(26, 26)
#define MC_STAT_CONTROL_FILTER_PRI		GENMASK(29, 28)

#define MC_STAT_CONTROL_PRI_EVENT_HP		0
#define MC_STAT_CONTROL_PRI_EVENT_TM		1
#define MC_STAT_CONTROL_PRI_EVENT_BW		2

#define MC_STAT_CONTROL_FILTER_PRI_DISABLE	0
#define MC_STAT_CONTROL_FILTER_PRI_NO		1
#define MC_STAT_CONTROL_FILTER_PRI_YES		2

#define MC_STAT_CONTROL_EVENT_QUALIFIED		0
#define MC_STAT_CONTROL_EVENT_ANY_READ		1
#define MC_STAT_CONTROL_EVENT_ANY_WRITE		2
#define MC_STAT_CONTROL_EVENT_RD_WR_CHANGE	3
#define MC_STAT_CONTROL_EVENT_SUCCESSIVE	4
#define MC_STAT_CONTROL_EVENT_ARB_BANK_AA	5
#define MC_STAT_CONTROL_EVENT_ARB_BANK_BB	6
#define MC_STAT_CONTROL_EVENT_PAGE_MISS		7
#define MC_STAT_CONTROL_EVENT_AUTO_PRECHARGE	8

#define EMC_GATHER_RST				(0 << 8)
#define EMC_GATHER_CLEAR			(1 << 8)
#define EMC_GATHER_DISABLE			(2 << 8)
#define EMC_GATHER_ENABLE			(3 << 8)

#define MC_STAT_SAMPLE_TIME_USEC		16000

/* we store collected statistics as a fixed point values */
#define MC_FX_FRAC_SCALE			100

static DEFINE_MUTEX(tegra20_mc_stat_lock);

struct tegra20_mc_stat_gather {
	unsigned int pri_filter;
	unsigned int pri_event;
	unsigned int result;
	unsigned int client;
	unsigned int event;
	bool client_enb;
};

struct tegra20_mc_stat {
	struct tegra20_mc_stat_gather gather0;
	struct tegra20_mc_stat_gather gather1;
	unsigned int sample_time_usec;
	const struct tegra_mc *mc;
};

struct tegra20_mc_client_stat {
	unsigned int events;
	unsigned int arb_high_prio;
	unsigned int arb_timeout;
	unsigned int arb_bandwidth;
	unsigned int rd_wr_change;
	unsigned int successive;
	unsigned int page_miss;
	unsigned int auto_precharge;
	unsigned int arb_bank_aa;
	unsigned int arb_bank_bb;
};

static const struct tegra_mc_client tegra20_mc_clients[] = {
	{
		.id = 0x00,
		.name = "display0a",
	}, {
		.id = 0x01,
		.name = "display0ab",
	}, {
		.id = 0x02,
		.name = "display0b",
	}, {
		.id = 0x03,
		.name = "display0bb",
	}, {
		.id = 0x04,
		.name = "display0c",
	}, {
		.id = 0x05,
		.name = "display0cb",
	}, {
		.id = 0x06,
		.name = "display1b",
	}, {
		.id = 0x07,
		.name = "display1bb",
	}, {
		.id = 0x08,
		.name = "eppup",
	}, {
		.id = 0x09,
		.name = "g2pr",
	}, {
		.id = 0x0a,
		.name = "g2sr",
	}, {
		.id = 0x0b,
		.name = "mpeunifbr",
	}, {
		.id = 0x0c,
		.name = "viruv",
	}, {
		.id = 0x0d,
		.name = "avpcarm7r",
	}, {
		.id = 0x0e,
		.name = "displayhc",
	}, {
		.id = 0x0f,
		.name = "displayhcb",
	}, {
		.id = 0x10,
		.name = "fdcdrd",
	}, {
		.id = 0x11,
		.name = "g2dr",
	}, {
		.id = 0x12,
		.name = "host1xdmar",
	}, {
		.id = 0x13,
		.name = "host1xr",
	}, {
		.id = 0x14,
		.name = "idxsrd",
	}, {
		.id = 0x15,
		.name = "mpcorer",
	}, {
		.id = 0x16,
		.name = "mpe_ipred",
	}, {
		.id = 0x17,
		.name = "mpeamemrd",
	}, {
		.id = 0x18,
		.name = "mpecsrd",
	}, {
		.id = 0x19,
		.name = "ppcsahbdmar",
	}, {
		.id = 0x1a,
		.name = "ppcsahbslvr",
	}, {
		.id = 0x1b,
		.name = "texsrd",
	}, {
		.id = 0x1c,
		.name = "vdebsevr",
	}, {
		.id = 0x1d,
		.name = "vdember",
	}, {
		.id = 0x1e,
		.name = "vdemcer",
	}, {
		.id = 0x1f,
		.name = "vdetper",
	}, {
		.id = 0x20,
		.name = "eppu",
	}, {
		.id = 0x21,
		.name = "eppv",
	}, {
		.id = 0x22,
		.name = "eppy",
	}, {
		.id = 0x23,
		.name = "mpeunifbw",
	}, {
		.id = 0x24,
		.name = "viwsb",
	}, {
		.id = 0x25,
		.name = "viwu",
	}, {
		.id = 0x26,
		.name = "viwv",
	}, {
		.id = 0x27,
		.name = "viwy",
	}, {
		.id = 0x28,
		.name = "g2dw",
	}, {
		.id = 0x29,
		.name = "avpcarm7w",
	}, {
		.id = 0x2a,
		.name = "fdcdwr",
	}, {
		.id = 0x2b,
		.name = "host1xw",
	}, {
		.id = 0x2c,
		.name = "ispw",
	}, {
		.id = 0x2d,
		.name = "mpcorew",
	}, {
		.id = 0x2e,
		.name = "mpecswr",
	}, {
		.id = 0x2f,
		.name = "ppcsahbdmaw",
	}, {
		.id = 0x30,
		.name = "ppcsahbslvw",
	}, {
		.id = 0x31,
		.name = "vdebsevw",
	}, {
		.id = 0x32,
		.name = "vdembew",
	}, {
		.id = 0x33,
		.name = "vdetpmw",
	},
};

#define TEGRA20_MC_RESET(_name, _control, _status, _reset, _bit)	\
	{								\
		.name = #_name,						\
		.id = TEGRA20_MC_RESET_##_name,				\
		.control = _control,					\
		.status = _status,					\
		.reset = _reset,					\
		.bit = _bit,						\
	}

static const struct tegra_mc_reset tegra20_mc_resets[] = {
	TEGRA20_MC_RESET(AVPC,   0x100, 0x140, 0x104,  0),
	TEGRA20_MC_RESET(DC,     0x100, 0x144, 0x104,  1),
	TEGRA20_MC_RESET(DCB,    0x100, 0x148, 0x104,  2),
	TEGRA20_MC_RESET(EPP,    0x100, 0x14c, 0x104,  3),
	TEGRA20_MC_RESET(2D,     0x100, 0x150, 0x104,  4),
	TEGRA20_MC_RESET(HC,     0x100, 0x154, 0x104,  5),
	TEGRA20_MC_RESET(ISP,    0x100, 0x158, 0x104,  6),
	TEGRA20_MC_RESET(MPCORE, 0x100, 0x15c, 0x104,  7),
	TEGRA20_MC_RESET(MPEA,   0x100, 0x160, 0x104,  8),
	TEGRA20_MC_RESET(MPEB,   0x100, 0x164, 0x104,  9),
	TEGRA20_MC_RESET(MPEC,   0x100, 0x168, 0x104, 10),
	TEGRA20_MC_RESET(3D,     0x100, 0x16c, 0x104, 11),
	TEGRA20_MC_RESET(PPCS,   0x100, 0x170, 0x104, 12),
	TEGRA20_MC_RESET(VDE,    0x100, 0x174, 0x104, 13),
	TEGRA20_MC_RESET(VI,     0x100, 0x178, 0x104, 14),
};

static int tegra20_mc_hotreset_assert(struct tegra_mc *mc,
				      const struct tegra_mc_reset *rst)
{
	unsigned long flags;
	u32 value;

	spin_lock_irqsave(&mc->lock, flags);

	value = mc_readl(mc, rst->reset);
	mc_writel(mc, value & ~BIT(rst->bit), rst->reset);

	spin_unlock_irqrestore(&mc->lock, flags);

	return 0;
}

static int tegra20_mc_hotreset_deassert(struct tegra_mc *mc,
					const struct tegra_mc_reset *rst)
{
	unsigned long flags;
	u32 value;

	spin_lock_irqsave(&mc->lock, flags);

	value = mc_readl(mc, rst->reset);
	mc_writel(mc, value | BIT(rst->bit), rst->reset);

	spin_unlock_irqrestore(&mc->lock, flags);

	return 0;
}

static int tegra20_mc_block_dma(struct tegra_mc *mc,
				const struct tegra_mc_reset *rst)
{
	unsigned long flags;
	u32 value;

	spin_lock_irqsave(&mc->lock, flags);

	value = mc_readl(mc, rst->control) & ~BIT(rst->bit);
	mc_writel(mc, value, rst->control);

	spin_unlock_irqrestore(&mc->lock, flags);

	return 0;
}

static bool tegra20_mc_dma_idling(struct tegra_mc *mc,
				  const struct tegra_mc_reset *rst)
{
	return mc_readl(mc, rst->status) == 0;
}

static int tegra20_mc_reset_status(struct tegra_mc *mc,
				   const struct tegra_mc_reset *rst)
{
	return (mc_readl(mc, rst->reset) & BIT(rst->bit)) == 0;
}

static int tegra20_mc_unblock_dma(struct tegra_mc *mc,
				  const struct tegra_mc_reset *rst)
{
	unsigned long flags;
	u32 value;

	spin_lock_irqsave(&mc->lock, flags);

	value = mc_readl(mc, rst->control) | BIT(rst->bit);
	mc_writel(mc, value, rst->control);

	spin_unlock_irqrestore(&mc->lock, flags);

	return 0;
}

static const struct tegra_mc_reset_ops tegra20_mc_reset_ops = {
	.hotreset_assert = tegra20_mc_hotreset_assert,
	.hotreset_deassert = tegra20_mc_hotreset_deassert,
	.block_dma = tegra20_mc_block_dma,
	.dma_idling = tegra20_mc_dma_idling,
	.unblock_dma = tegra20_mc_unblock_dma,
	.reset_status = tegra20_mc_reset_status,
};

static int tegra20_mc_icc_set(struct icc_node *src, struct icc_node *dst)
{
	/*
	 * It should be possible to tune arbitration knobs here, but the
	 * default values are known to work well on all devices. Hence
	 * nothing to do here so far.
	 */
	return 0;
}

static int tegra20_mc_icc_aggreate(struct icc_node *node, u32 tag, u32 avg_bw,
				   u32 peak_bw, u32 *agg_avg, u32 *agg_peak)
{
	/*
	 * ISO clients need to reserve extra bandwidth up-front because
	 * there could be high bandwidth pressure during initial filling
	 * of the client's FIFO buffers.  Secondly, we need to take into
	 * account impurities of the memory subsystem.
	 */
	if (tag & TEGRA_MC_ICC_TAG_ISO)
		peak_bw = tegra_mc_scale_percents(peak_bw, 300);

	*agg_avg += avg_bw;
	*agg_peak = max(*agg_peak, peak_bw);

	return 0;
}

static struct icc_node_data *
tegra20_mc_of_icc_xlate_extended(const struct of_phandle_args *spec, void *data)
{
	struct tegra_mc *mc = icc_provider_to_tegra_mc(data);
	unsigned int i, idx = spec->args[0];
	struct icc_node_data *ndata;
	struct icc_node *node;

	list_for_each_entry(node, &mc->provider.nodes, node_list) {
		if (node->id != idx)
			continue;

		ndata = kzalloc(sizeof(*ndata), GFP_KERNEL);
		if (!ndata)
			return ERR_PTR(-ENOMEM);

		ndata->node = node;

		/* these clients are isochronous by default */
		if (strstarts(node->name, "display") ||
		    strstarts(node->name, "vi"))
			ndata->tag = TEGRA_MC_ICC_TAG_ISO;
		else
			ndata->tag = TEGRA_MC_ICC_TAG_DEFAULT;

		return ndata;
	}

	for (i = 0; i < mc->soc->num_clients; i++) {
		if (mc->soc->clients[i].id == idx)
			return ERR_PTR(-EPROBE_DEFER);
	}

	dev_err(mc->dev, "invalid ICC client ID %u\n", idx);

	return ERR_PTR(-EINVAL);
}

static const struct tegra_mc_icc_ops tegra20_mc_icc_ops = {
	.xlate_extended = tegra20_mc_of_icc_xlate_extended,
	.aggregate = tegra20_mc_icc_aggreate,
	.set = tegra20_mc_icc_set,
};

static u32 tegra20_mc_stat_gather_control(const struct tegra20_mc_stat_gather *g)
{
	u32 control;

	control  = FIELD_PREP(MC_STAT_CONTROL_EVENT, g->event);
	control |= FIELD_PREP(MC_STAT_CONTROL_CLIENT_ID, g->client);
	control |= FIELD_PREP(MC_STAT_CONTROL_PRI_EVENT, g->pri_event);
	control |= FIELD_PREP(MC_STAT_CONTROL_FILTER_PRI, g->pri_filter);
	control |= FIELD_PREP(MC_STAT_CONTROL_FILTER_CLIENT_ENABLE, g->client_enb);

	return control;
}

static void tegra20_mc_stat_gather(struct tegra20_mc_stat *stat)
{
	u32 clocks, count0, count1, control_0, control_1;
	const struct tegra_mc *mc = stat->mc;

	control_0 = tegra20_mc_stat_gather_control(&stat->gather0);
	control_1 = tegra20_mc_stat_gather_control(&stat->gather1);

	/*
	 * Reset statistic gathers state, select statistics collection mode
	 * and set clocks counter saturation limit to maximum.
	 */
	mc_writel(mc, 0x00000000, MC_STAT_CONTROL);
	mc_writel(mc,  control_0, MC_STAT_EMC_CONTROL_0);
	mc_writel(mc,  control_1, MC_STAT_EMC_CONTROL_1);
	mc_writel(mc, 0xffffffff, MC_STAT_EMC_CLOCK_LIMIT);

	mc_writel(mc, EMC_GATHER_ENABLE, MC_STAT_CONTROL);
	fsleep(stat->sample_time_usec);
	mc_writel(mc, EMC_GATHER_DISABLE, MC_STAT_CONTROL);

	count0 = mc_readl(mc, MC_STAT_EMC_COUNT_0);
	count1 = mc_readl(mc, MC_STAT_EMC_COUNT_1);
	clocks = mc_readl(mc, MC_STAT_EMC_CLOCKS);
	clocks = max(clocks / 100 / MC_FX_FRAC_SCALE, 1u);

	stat->gather0.result = DIV_ROUND_UP(count0, clocks);
	stat->gather1.result = DIV_ROUND_UP(count1, clocks);
}

static void tegra20_mc_stat_events(const struct tegra_mc *mc,
				   const struct tegra_mc_client *client0,
				   const struct tegra_mc_client *client1,
				   unsigned int pri_filter,
				   unsigned int pri_event,
				   unsigned int event,
				   unsigned int *result0,
				   unsigned int *result1)
{
	struct tegra20_mc_stat stat = {};

	stat.gather0.client = client0 ? client0->id : 0;
	stat.gather0.pri_filter = pri_filter;
	stat.gather0.client_enb = !!client0;
	stat.gather0.pri_event = pri_event;
	stat.gather0.event = event;

	stat.gather1.client = client1 ? client1->id : 0;
	stat.gather1.pri_filter = pri_filter;
	stat.gather1.client_enb = !!client1;
	stat.gather1.pri_event = pri_event;
	stat.gather1.event = event;

	stat.sample_time_usec = MC_STAT_SAMPLE_TIME_USEC;
	stat.mc = mc;

	tegra20_mc_stat_gather(&stat);

	*result0 = stat.gather0.result;
	*result1 = stat.gather1.result;
}

static void tegra20_mc_collect_stats(const struct tegra_mc *mc,
				     struct tegra20_mc_client_stat *stats)
{
	const struct tegra_mc_client *client0, *client1;
	unsigned int i;

	/* collect memory controller utilization percent for each client */
	for (i = 0; i < mc->soc->num_clients; i += 2) {
		client0 = &mc->soc->clients[i];
		client1 = &mc->soc->clients[i + 1];

		if (i + 1 == mc->soc->num_clients)
			client1 = NULL;

		tegra20_mc_stat_events(mc, client0, client1,
				       MC_STAT_CONTROL_FILTER_PRI_DISABLE,
				       MC_STAT_CONTROL_PRI_EVENT_HP,
				       MC_STAT_CONTROL_EVENT_QUALIFIED,
				       &stats[i + 0].events,
				       &stats[i + 1].events);
	}

	/* collect more info from active clients */
	for (i = 0; i < mc->soc->num_clients; i++) {
		unsigned int clienta, clientb = mc->soc->num_clients;

		for (client0 = NULL; i < mc->soc->num_clients; i++) {
			if (stats[i].events) {
				client0 = &mc->soc->clients[i];
				clienta = i++;
				break;
			}
		}

		for (client1 = NULL; i < mc->soc->num_clients; i++) {
			if (stats[i].events) {
				client1 = &mc->soc->clients[i];
				clientb = i;
				break;
			}
		}

		if (!client0 && !client1)
			break;

		tegra20_mc_stat_events(mc, client0, client1,
				       MC_STAT_CONTROL_FILTER_PRI_YES,
				       MC_STAT_CONTROL_PRI_EVENT_HP,
				       MC_STAT_CONTROL_EVENT_QUALIFIED,
				       &stats[clienta].arb_high_prio,
				       &stats[clientb].arb_high_prio);

		tegra20_mc_stat_events(mc, client0, client1,
				       MC_STAT_CONTROL_FILTER_PRI_YES,
				       MC_STAT_CONTROL_PRI_EVENT_TM,
				       MC_STAT_CONTROL_EVENT_QUALIFIED,
				       &stats[clienta].arb_timeout,
				       &stats[clientb].arb_timeout);

		tegra20_mc_stat_events(mc, client0, client1,
				       MC_STAT_CONTROL_FILTER_PRI_YES,
				       MC_STAT_CONTROL_PRI_EVENT_BW,
				       MC_STAT_CONTROL_EVENT_QUALIFIED,
				       &stats[clienta].arb_bandwidth,
				       &stats[clientb].arb_bandwidth);

		tegra20_mc_stat_events(mc, client0, client1,
				       MC_STAT_CONTROL_FILTER_PRI_DISABLE,
				       MC_STAT_CONTROL_PRI_EVENT_HP,
				       MC_STAT_CONTROL_EVENT_RD_WR_CHANGE,
				       &stats[clienta].rd_wr_change,
				       &stats[clientb].rd_wr_change);

		tegra20_mc_stat_events(mc, client0, client1,
				       MC_STAT_CONTROL_FILTER_PRI_DISABLE,
				       MC_STAT_CONTROL_PRI_EVENT_HP,
				       MC_STAT_CONTROL_EVENT_SUCCESSIVE,
				       &stats[clienta].successive,
				       &stats[clientb].successive);

		tegra20_mc_stat_events(mc, client0, client1,
				       MC_STAT_CONTROL_FILTER_PRI_DISABLE,
				       MC_STAT_CONTROL_PRI_EVENT_HP,
				       MC_STAT_CONTROL_EVENT_PAGE_MISS,
				       &stats[clienta].page_miss,
				       &stats[clientb].page_miss);
	}
}

static void tegra20_mc_printf_percents(struct seq_file *s,
				       const char *fmt,
				       unsigned int percents_fx)
{
	char percents_str[8];

	snprintf(percents_str, ARRAY_SIZE(percents_str), "%3u.%02u%%",
		 percents_fx / MC_FX_FRAC_SCALE, percents_fx % MC_FX_FRAC_SCALE);

	seq_printf(s, fmt, percents_str);
}

static int tegra20_mc_stats_show(struct seq_file *s, void *unused)
{
	const struct tegra_mc *mc = dev_get_drvdata(s->private);
	struct tegra20_mc_client_stat *stats;
	unsigned int i;

	stats = kcalloc(mc->soc->num_clients + 1, sizeof(*stats), GFP_KERNEL);
	if (!stats)
		return -ENOMEM;

	mutex_lock(&tegra20_mc_stat_lock);

	tegra20_mc_collect_stats(mc, stats);

	mutex_unlock(&tegra20_mc_stat_lock);

	seq_puts(s, "Memory client   Events   Timeout   High priority   Bandwidth ARB   RW change   Successive   Page miss\n");
	seq_puts(s, "-----------------------------------------------------------------------------------------------------\n");

	for (i = 0; i < mc->soc->num_clients; i++) {
		seq_printf(s, "%-14s  ", mc->soc->clients[i].name);

		/* An event is generated when client performs R/W request. */
		tegra20_mc_printf_percents(s,  "%-9s", stats[i].events);

		/*
		 * An event is generated based on the timeout (TM) signal
		 * accompanying a request for arbitration.
		 */
		tegra20_mc_printf_percents(s, "%-10s", stats[i].arb_timeout);

		/*
		 * An event is generated based on the high-priority (HP) signal
		 * accompanying a request for arbitration.
		 */
		tegra20_mc_printf_percents(s, "%-16s", stats[i].arb_high_prio);

		/*
		 * An event is generated based on the bandwidth (BW) signal
		 * accompanying a request for arbitration.
		 */
		tegra20_mc_printf_percents(s, "%-16s", stats[i].arb_bandwidth);

		/*
		 * An event is generated when the memory controller switches
		 * between making a read request to making a write request.
		 */
		tegra20_mc_printf_percents(s, "%-12s", stats[i].rd_wr_change);

		/*
		 * An even generated when the chosen client has wins arbitration
		 * when it was also the winner at the previous request.  If a
		 * client makes N requests in a row that are honored, SUCCESSIVE
		 * will be counted (N-1) times.  Large values for this event
		 * imply that if we were patient enough, all of those requests
		 * could have been coalesced.
		 */
		tegra20_mc_printf_percents(s, "%-13s", stats[i].successive);

		/*
		 * An event is generated when the memory controller detects a
		 * page miss for the current request.
		 */
		tegra20_mc_printf_percents(s, "%-12s\n", stats[i].page_miss);
	}

	kfree(stats);

	return 0;
}

static int tegra20_mc_probe(struct tegra_mc *mc)
{
	debugfs_create_devm_seqfile(mc->dev, "stats", mc->debugfs.root,
				    tegra20_mc_stats_show);

	return 0;
}

static irqreturn_t tegra20_mc_handle_irq(int irq, void *data)
{
	struct tegra_mc *mc = data;
	unsigned long status;
	unsigned int bit;

	/* mask all interrupts to avoid flooding */
	status = mc_readl(mc, MC_INTSTATUS) & mc->soc->intmask;
	if (!status)
		return IRQ_NONE;

	for_each_set_bit(bit, &status, 32) {
		const char *error = tegra_mc_status_names[bit];
		const char *direction = "read", *secure = "";
		const char *client, *desc;
		phys_addr_t addr;
		u32 value, reg;
		u8 id, type;

		switch (BIT(bit)) {
		case MC_INT_DECERR_EMEM:
			reg = MC_DECERR_EMEM_OTHERS_STATUS;
			value = mc_readl(mc, reg);

			id = value & mc->soc->client_id_mask;
			desc = tegra_mc_error_names[2];

			if (value & BIT(31))
				direction = "write";
			break;

		case MC_INT_INVALID_GART_PAGE:
			reg = MC_GART_ERROR_REQ;
			value = mc_readl(mc, reg);

			id = (value >> 1) & mc->soc->client_id_mask;
			desc = tegra_mc_error_names[2];

			if (value & BIT(0))
				direction = "write";
			break;

		case MC_INT_SECURITY_VIOLATION:
			reg = MC_SECURITY_VIOLATION_STATUS;
			value = mc_readl(mc, reg);

			id = value & mc->soc->client_id_mask;
			type = (value & BIT(30)) ? 4 : 3;
			desc = tegra_mc_error_names[type];
			secure = "secure ";

			if (value & BIT(31))
				direction = "write";
			break;

		default:
			continue;
		}

		client = mc->soc->clients[id].name;
		addr = mc_readl(mc, reg + sizeof(u32));

		dev_err_ratelimited(mc->dev, "%s: %s%s @%pa: %s (%s)\n",
				    client, secure, direction, &addr, error,
				    desc);
	}

	/* clear interrupts */
	mc_writel(mc, status, MC_INTSTATUS);

	return IRQ_HANDLED;
}

static const struct tegra_mc_ops tegra20_mc_ops = {
	.probe = tegra20_mc_probe,
	.handle_irq = tegra20_mc_handle_irq,
};

const struct tegra_mc_soc tegra20_mc_soc = {
	.clients = tegra20_mc_clients,
	.num_clients = ARRAY_SIZE(tegra20_mc_clients),
	.num_address_bits = 32,
	.client_id_mask = 0x3f,
	.intmask = MC_INT_SECURITY_VIOLATION | MC_INT_INVALID_GART_PAGE |
		   MC_INT_DECERR_EMEM,
	.reset_ops = &tegra20_mc_reset_ops,
	.resets = tegra20_mc_resets,
	.num_resets = ARRAY_SIZE(tegra20_mc_resets),
	.icc_ops = &tegra20_mc_icc_ops,
	.ops = &tegra20_mc_ops,
};
