/*
 * Copyright (c) 2012, 2013, NVIDIA CORPORATION.  All rights reserved.
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

#include <linux/io.h>
#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/clk/tegra.h>

#include "clk.h"
#include "clk-id.h"

#define AUDIO_SYNC_CLK_I2S0 0x4a0
#define AUDIO_SYNC_CLK_I2S1 0x4a4
#define AUDIO_SYNC_CLK_I2S2 0x4a8
#define AUDIO_SYNC_CLK_I2S3 0x4ac
#define AUDIO_SYNC_CLK_I2S4 0x4b0
#define AUDIO_SYNC_CLK_SPDIF 0x4b4
#define AUDIO_SYNC_CLK_DMIC1 0x560
#define AUDIO_SYNC_CLK_DMIC2 0x564
#define AUDIO_SYNC_CLK_DMIC3 0x6b8

#define AUDIO_SYNC_DOUBLER 0x49c

#define PLLA_OUT 0xb4

struct tegra_sync_source_initdata {
	char		*name;
	unsigned long	rate;
	unsigned long	max_rate;
	int		clk_id;
};

#define SYNC(_name) \
	{\
		.name		= #_name,\
		.clk_id		= tegra_clk_ ## _name,\
	}

struct tegra_audio_clk_initdata {
	char		*gate_name;
	char		*mux_name;
	u32		offset;
	int		gate_clk_id;
	int		mux_clk_id;
};

#define AUDIO(_name, _offset) \
	{\
		.gate_name	= #_name,\
		.mux_name	= #_name"_mux",\
		.offset		= _offset,\
		.gate_clk_id	= tegra_clk_ ## _name,\
		.mux_clk_id	= tegra_clk_ ## _name ## _mux,\
	}

struct tegra_audio2x_clk_initdata {
	char		*parent;
	char		*gate_name;
	char		*name_2x;
	char		*div_name;
	int		clk_id;
	int		clk_num;
	u8		div_offset;
};

#define AUDIO2X(_name, _num, _offset) \
	{\
		.parent		= #_name,\
		.gate_name	= #_name"_2x",\
		.name_2x	= #_name"_doubler",\
		.div_name	= #_name"_div",\
		.clk_id		= tegra_clk_ ## _name ## _2x,\
		.clk_num	= _num,\
		.div_offset	= _offset,\
	}

static DEFINE_SPINLOCK(clk_doubler_lock);

static const char * const mux_audio_sync_clk[] = { "spdif_in_sync",
	"i2s0_sync", "i2s1_sync", "i2s2_sync", "i2s3_sync", "i2s4_sync",
	"pll_a_out0", "vimclk_sync",
};

static const char * const mux_dmic_sync_clk[] = { "unused", "i2s0_sync",
	"i2s1_sync", "i2s2_sync", "i2s3_sync", "i2s4_sync", "pll_a_out0",
	"vimclk_sync",
};

static struct tegra_sync_source_initdata sync_source_clks[] __initdata = {
	SYNC(spdif_in_sync),
	SYNC(i2s0_sync),
	SYNC(i2s1_sync),
	SYNC(i2s2_sync),
	SYNC(i2s3_sync),
	SYNC(i2s4_sync),
	SYNC(vimclk_sync),
};

static struct tegra_audio_clk_initdata audio_clks[] = {
	AUDIO(audio0, AUDIO_SYNC_CLK_I2S0),
	AUDIO(audio1, AUDIO_SYNC_CLK_I2S1),
	AUDIO(audio2, AUDIO_SYNC_CLK_I2S2),
	AUDIO(audio3, AUDIO_SYNC_CLK_I2S3),
	AUDIO(audio4, AUDIO_SYNC_CLK_I2S4),
	AUDIO(spdif, AUDIO_SYNC_CLK_SPDIF),
};

static struct tegra_audio_clk_initdata dmic_clks[] = {
	AUDIO(dmic1_sync_clk, AUDIO_SYNC_CLK_DMIC1),
	AUDIO(dmic2_sync_clk, AUDIO_SYNC_CLK_DMIC2),
	AUDIO(dmic3_sync_clk, AUDIO_SYNC_CLK_DMIC3),
};

static struct tegra_audio2x_clk_initdata audio2x_clks[] = {
	AUDIO2X(audio0, 113, 24),
	AUDIO2X(audio1, 114, 25),
	AUDIO2X(audio2, 115, 26),
	AUDIO2X(audio3, 116, 27),
	AUDIO2X(audio4, 117, 28),
	AUDIO2X(spdif, 118, 29),
};

static void __init tegra_audio_sync_clk_init(void __iomem *clk_base,
				      struct tegra_clk *tegra_clks,
				      struct tegra_audio_clk_initdata *sync,
				      int num_sync_clks,
				      const char * const *mux_names,
				      int num_mux_inputs)
{
	struct clk *clk;
	struct clk **dt_clk;
	struct tegra_audio_clk_initdata *data;
	int i;

	for (i = 0, data = sync; i < num_sync_clks; i++, data++) {
		dt_clk = tegra_lookup_dt_id(data->mux_clk_id, tegra_clks);
		if (!dt_clk)
			continue;

		clk = clk_register_mux(NULL, data->mux_name, mux_names,
					num_mux_inputs,
					CLK_SET_RATE_NO_REPARENT,
					clk_base + data->offset, 0, 3, 0,
					NULL);
		*dt_clk = clk;

		dt_clk = tegra_lookup_dt_id(data->gate_clk_id, tegra_clks);
		if (!dt_clk)
			continue;

		clk = clk_register_gate(NULL, data->gate_name, data->mux_name,
					0, clk_base + data->offset, 4,
					CLK_GATE_SET_TO_DISABLE, NULL);
		*dt_clk = clk;
	}
}

void __init tegra_audio_clk_init(void __iomem *clk_base,
			void __iomem *pmc_base, struct tegra_clk *tegra_clks,
			struct tegra_audio_clk_info *audio_info,
			unsigned int num_plls, unsigned long sync_max_rate)
{
	struct clk *clk;
	struct clk **dt_clk;
	int i;

	if (!audio_info || num_plls < 1) {
		pr_err("No audio data passed to tegra_audio_clk_init\n");
		WARN_ON(1);
		return;
	}

	for (i = 0; i < num_plls; i++) {
		struct tegra_audio_clk_info *info = &audio_info[i];

		dt_clk = tegra_lookup_dt_id(info->clk_id, tegra_clks);
		if (dt_clk) {
			clk = tegra_clk_register_pll(info->name, info->parent,
					clk_base, pmc_base, 0, info->pll_params,
					NULL);
			*dt_clk = clk;
		}
	}

	/* PLLA_OUT0 */
	dt_clk = tegra_lookup_dt_id(tegra_clk_pll_a_out0, tegra_clks);
	if (dt_clk) {
		clk = tegra_clk_register_divider("pll_a_out0_div", "pll_a",
				clk_base + PLLA_OUT, 0, TEGRA_DIVIDER_ROUND_UP,
				8, 8, 1, NULL);
		clk = tegra_clk_register_pll_out("pll_a_out0", "pll_a_out0_div",
				clk_base + PLLA_OUT, 1, 0, CLK_IGNORE_UNUSED |
				CLK_SET_RATE_PARENT, 0, NULL);
		*dt_clk = clk;
	}

	for (i = 0; i < ARRAY_SIZE(sync_source_clks); i++) {
		struct tegra_sync_source_initdata *data;

		data = &sync_source_clks[i];

		dt_clk = tegra_lookup_dt_id(data->clk_id, tegra_clks);
		if (!dt_clk)
			continue;

		clk = tegra_clk_register_sync_source(data->name, sync_max_rate);
		*dt_clk = clk;
	}

	tegra_audio_sync_clk_init(clk_base, tegra_clks, audio_clks,
				  ARRAY_SIZE(audio_clks), mux_audio_sync_clk,
				  ARRAY_SIZE(mux_audio_sync_clk));

	/* make sure the DMIC sync clocks have a valid parent */
	for (i = 0; i < ARRAY_SIZE(dmic_clks); i++)
		writel_relaxed(1, clk_base + dmic_clks[i].offset);

	tegra_audio_sync_clk_init(clk_base, tegra_clks, dmic_clks,
				  ARRAY_SIZE(dmic_clks), mux_dmic_sync_clk,
				  ARRAY_SIZE(mux_dmic_sync_clk));

	for (i = 0; i < ARRAY_SIZE(audio2x_clks); i++) {
		struct tegra_audio2x_clk_initdata *data;

		data = &audio2x_clks[i];
		dt_clk = tegra_lookup_dt_id(data->clk_id, tegra_clks);
		if (!dt_clk)
			continue;

		clk = clk_register_fixed_factor(NULL, data->name_2x,
				data->parent, CLK_SET_RATE_PARENT, 2, 1);
		clk = tegra_clk_register_divider(data->div_name,
				data->name_2x, clk_base + AUDIO_SYNC_DOUBLER,
				0, 0, data->div_offset, 1, 0,
				&clk_doubler_lock);
		clk = tegra_clk_register_periph_gate(data->gate_name,
				data->div_name, TEGRA_PERIPH_NO_RESET,
				clk_base, CLK_SET_RATE_PARENT, data->clk_num,
				periph_clk_enb_refcnt);
		*dt_clk = clk;
	}
}

