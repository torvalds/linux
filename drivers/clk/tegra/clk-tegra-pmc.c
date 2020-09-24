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
#include <linux/clkdev.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/clk/tegra.h>

#include "clk.h"
#include "clk-id.h"

#define PMC_CLK_OUT_CNTRL 0x1a8
#define PMC_DPD_PADS_ORIDE 0x1c
#define PMC_DPD_PADS_ORIDE_BLINK_ENB 20
#define PMC_CTRL 0
#define PMC_CTRL_BLINK_ENB 7
#define PMC_BLINK_TIMER 0x40

struct pmc_clk_init_data {
	char *mux_name;
	char *gate_name;
	const char **parents;
	int num_parents;
	int mux_id;
	int gate_id;
	char *dev_name;
	u8 mux_shift;
	u8 gate_shift;
};

#define PMC_CLK(_num, _mux_shift, _gate_shift)\
	{\
		.mux_name = "clk_out_" #_num "_mux",\
		.gate_name = "clk_out_" #_num,\
		.parents = clk_out ##_num ##_parents,\
		.num_parents = ARRAY_SIZE(clk_out ##_num ##_parents),\
		.mux_id = tegra_clk_clk_out_ ##_num ##_mux,\
		.gate_id = tegra_clk_clk_out_ ##_num,\
		.dev_name = "extern" #_num,\
		.mux_shift = _mux_shift,\
		.gate_shift = _gate_shift,\
	}

static DEFINE_SPINLOCK(clk_out_lock);

static const char *clk_out1_parents[] = { "osc", "osc_div2",
	"osc_div4", "extern1",
};

static const char *clk_out2_parents[] = { "osc", "osc_div2",
	"osc_div4", "extern2",
};

static const char *clk_out3_parents[] = { "osc", "osc_div2",
	"osc_div4", "extern3",
};

static struct pmc_clk_init_data pmc_clks[] = {
	PMC_CLK(1, 6, 2),
	PMC_CLK(2, 14, 10),
	PMC_CLK(3, 22, 18),
};

void __init tegra_pmc_clk_init(void __iomem *pmc_base,
				struct tegra_clk *tegra_clks)
{
	struct clk *clk;
	struct clk **dt_clk;
	int i;

	for (i = 0; i < ARRAY_SIZE(pmc_clks); i++) {
		struct pmc_clk_init_data *data;

		data = pmc_clks + i;

		dt_clk = tegra_lookup_dt_id(data->mux_id, tegra_clks);
		if (!dt_clk)
			continue;

		clk = clk_register_mux(NULL, data->mux_name, data->parents,
				data->num_parents,
				CLK_SET_RATE_NO_REPARENT | CLK_SET_RATE_PARENT,
				pmc_base + PMC_CLK_OUT_CNTRL, data->mux_shift,
				3, 0, &clk_out_lock);
		*dt_clk = clk;


		dt_clk = tegra_lookup_dt_id(data->gate_id, tegra_clks);
		if (!dt_clk)
			continue;

		clk = clk_register_gate(NULL, data->gate_name, data->mux_name,
					CLK_SET_RATE_PARENT,
					pmc_base + PMC_CLK_OUT_CNTRL,
					data->gate_shift, 0, &clk_out_lock);
		*dt_clk = clk;
		clk_register_clkdev(clk, data->dev_name, data->gate_name);
	}

	/* blink */
	writel_relaxed(0, pmc_base + PMC_BLINK_TIMER);
	clk = clk_register_gate(NULL, "blink_override", "clk_32k", 0,
				pmc_base + PMC_DPD_PADS_ORIDE,
				PMC_DPD_PADS_ORIDE_BLINK_ENB, 0, NULL);

	dt_clk = tegra_lookup_dt_id(tegra_clk_blink, tegra_clks);
	if (!dt_clk)
		return;

	clk = clk_register_gate(NULL, "blink", "blink_override", 0,
				pmc_base + PMC_CTRL,
				PMC_CTRL_BLINK_ENB, 0, NULL);
	clk_register_clkdev(clk, "blink", NULL);
	*dt_clk = clk;
}

