// SPDX-License-Identifier: GPL-2.0
#include <linux/clk-provider.h>
#include <linux/clk/at91_pmc.h>
#include <linux/of.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include "pmc.h"

#define MASTER_SOURCE_MAX	4

#define PERIPHERAL_AT91RM9200	0
#define PERIPHERAL_AT91SAM9X5	1

#define PERIPHERAL_MAX		64

#define PERIPHERAL_ID_MIN	2

#define PROG_SOURCE_MAX		5
#define PROG_ID_MAX		7

#define SYSTEM_MAX_ID		31

#define GCK_INDEX_DT_AUDIO_PLL	5

static DEFINE_SPINLOCK(mck_lock);

#ifdef CONFIG_HAVE_AT91_AUDIO_PLL
static void __init of_sama5d2_clk_audio_pll_frac_setup(struct device_node *np)
{
	struct clk_hw *hw;
	const char *name = np->name;
	const char *parent_name;
	struct regmap *regmap;

	regmap = syscon_node_to_regmap(of_get_parent(np));
	if (IS_ERR(regmap))
		return;

	parent_name = of_clk_get_parent_name(np, 0);

	hw = at91_clk_register_audio_pll_frac(regmap, name, parent_name);
	if (IS_ERR(hw))
		return;

	of_clk_add_hw_provider(np, of_clk_hw_simple_get, hw);
}
CLK_OF_DECLARE(of_sama5d2_clk_audio_pll_frac_setup,
	       "atmel,sama5d2-clk-audio-pll-frac",
	       of_sama5d2_clk_audio_pll_frac_setup);

static void __init of_sama5d2_clk_audio_pll_pad_setup(struct device_node *np)
{
	struct clk_hw *hw;
	const char *name = np->name;
	const char *parent_name;
	struct regmap *regmap;

	regmap = syscon_node_to_regmap(of_get_parent(np));
	if (IS_ERR(regmap))
		return;

	parent_name = of_clk_get_parent_name(np, 0);

	hw = at91_clk_register_audio_pll_pad(regmap, name, parent_name);
	if (IS_ERR(hw))
		return;

	of_clk_add_hw_provider(np, of_clk_hw_simple_get, hw);
}
CLK_OF_DECLARE(of_sama5d2_clk_audio_pll_pad_setup,
	       "atmel,sama5d2-clk-audio-pll-pad",
	       of_sama5d2_clk_audio_pll_pad_setup);

static void __init of_sama5d2_clk_audio_pll_pmc_setup(struct device_node *np)
{
	struct clk_hw *hw;
	const char *name = np->name;
	const char *parent_name;
	struct regmap *regmap;

	regmap = syscon_node_to_regmap(of_get_parent(np));
	if (IS_ERR(regmap))
		return;

	parent_name = of_clk_get_parent_name(np, 0);

	hw = at91_clk_register_audio_pll_pmc(regmap, name, parent_name);
	if (IS_ERR(hw))
		return;

	of_clk_add_hw_provider(np, of_clk_hw_simple_get, hw);
}
CLK_OF_DECLARE(of_sama5d2_clk_audio_pll_pmc_setup,
	       "atmel,sama5d2-clk-audio-pll-pmc",
	       of_sama5d2_clk_audio_pll_pmc_setup);
#endif /* CONFIG_HAVE_AT91_AUDIO_PLL */

static const struct clk_pcr_layout dt_pcr_layout = {
	.offset = 0x10c,
	.cmd = BIT(12),
	.pid_mask = GENMASK(5, 0),
	.div_mask = GENMASK(17, 16),
	.gckcss_mask = GENMASK(10, 8),
};

#ifdef CONFIG_HAVE_AT91_GENERATED_CLK
#define GENERATED_SOURCE_MAX	6

#define GCK_ID_I2S0		54
#define GCK_ID_I2S1		55
#define GCK_ID_CLASSD		59

static void __init of_sama5d2_clk_generated_setup(struct device_node *np)
{
	int num;
	u32 id;
	const char *name;
	struct clk_hw *hw;
	unsigned int num_parents;
	const char *parent_names[GENERATED_SOURCE_MAX];
	struct device_node *gcknp;
	struct clk_range range = CLK_RANGE(0, 0);
	struct regmap *regmap;

	num_parents = of_clk_get_parent_count(np);
	if (num_parents == 0 || num_parents > GENERATED_SOURCE_MAX)
		return;

	of_clk_parent_fill(np, parent_names, num_parents);

	num = of_get_child_count(np);
	if (!num || num > PERIPHERAL_MAX)
		return;

	regmap = syscon_node_to_regmap(of_get_parent(np));
	if (IS_ERR(regmap))
		return;

	for_each_child_of_node(np, gcknp) {
		int chg_pid = INT_MIN;

		if (of_property_read_u32(gcknp, "reg", &id))
			continue;

		if (id < PERIPHERAL_ID_MIN || id >= PERIPHERAL_MAX)
			continue;

		if (of_property_read_string(np, "clock-output-names", &name))
			name = gcknp->name;

		of_at91_get_clk_range(gcknp, "atmel,clk-output-range",
				      &range);

		if (of_device_is_compatible(np, "atmel,sama5d2-clk-generated") &&
		    (id == GCK_ID_I2S0 || id == GCK_ID_I2S1 ||
		     id == GCK_ID_CLASSD))
			chg_pid = GCK_INDEX_DT_AUDIO_PLL;

		hw = at91_clk_register_generated(regmap, &pmc_pcr_lock,
						 &dt_pcr_layout, name,
						 parent_names, NULL,
						 num_parents, id, &range,
						 chg_pid);
		if (IS_ERR(hw))
			continue;

		of_clk_add_hw_provider(gcknp, of_clk_hw_simple_get, hw);
	}
}
CLK_OF_DECLARE(of_sama5d2_clk_generated_setup, "atmel,sama5d2-clk-generated",
	       of_sama5d2_clk_generated_setup);
#endif /* CONFIG_HAVE_AT91_GENERATED_CLK */

#ifdef CONFIG_HAVE_AT91_H32MX
static void __init of_sama5d4_clk_h32mx_setup(struct device_node *np)
{
	struct clk_hw *hw;
	const char *name = np->name;
	const char *parent_name;
	struct regmap *regmap;

	regmap = syscon_node_to_regmap(of_get_parent(np));
	if (IS_ERR(regmap))
		return;

	parent_name = of_clk_get_parent_name(np, 0);

	hw = at91_clk_register_h32mx(regmap, name, parent_name);
	if (IS_ERR(hw))
		return;

	of_clk_add_hw_provider(np, of_clk_hw_simple_get, hw);
}
CLK_OF_DECLARE(of_sama5d4_clk_h32mx_setup, "atmel,sama5d4-clk-h32mx",
	       of_sama5d4_clk_h32mx_setup);
#endif /* CONFIG_HAVE_AT91_H32MX */

#ifdef CONFIG_HAVE_AT91_I2S_MUX_CLK
#define	I2S_BUS_NR	2

static void __init of_sama5d2_clk_i2s_mux_setup(struct device_node *np)
{
	struct regmap *regmap_sfr;
	u8 bus_id;
	const char *parent_names[2];
	struct device_node *i2s_mux_np;
	struct clk_hw *hw;
	int ret;

	regmap_sfr = syscon_regmap_lookup_by_compatible("atmel,sama5d2-sfr");
	if (IS_ERR(regmap_sfr))
		return;

	for_each_child_of_node(np, i2s_mux_np) {
		if (of_property_read_u8(i2s_mux_np, "reg", &bus_id))
			continue;

		if (bus_id > I2S_BUS_NR)
			continue;

		ret = of_clk_parent_fill(i2s_mux_np, parent_names, 2);
		if (ret != 2)
			continue;

		hw = at91_clk_i2s_mux_register(regmap_sfr, i2s_mux_np->name,
					       parent_names, 2, bus_id);
		if (IS_ERR(hw))
			continue;

		of_clk_add_hw_provider(i2s_mux_np, of_clk_hw_simple_get, hw);
	}
}
CLK_OF_DECLARE(sama5d2_clk_i2s_mux, "atmel,sama5d2-clk-i2s-mux",
	       of_sama5d2_clk_i2s_mux_setup);
#endif /* CONFIG_HAVE_AT91_I2S_MUX_CLK */

static void __init of_at91rm9200_clk_main_osc_setup(struct device_node *np)
{
	struct clk_hw *hw;
	const char *name = np->name;
	const char *parent_name;
	struct regmap *regmap;
	bool bypass;

	of_property_read_string(np, "clock-output-names", &name);
	bypass = of_property_read_bool(np, "atmel,osc-bypass");
	parent_name = of_clk_get_parent_name(np, 0);

	regmap = syscon_node_to_regmap(of_get_parent(np));
	if (IS_ERR(regmap))
		return;

	hw = at91_clk_register_main_osc(regmap, name, parent_name, bypass);
	if (IS_ERR(hw))
		return;

	of_clk_add_hw_provider(np, of_clk_hw_simple_get, hw);
}
CLK_OF_DECLARE(at91rm9200_clk_main_osc, "atmel,at91rm9200-clk-main-osc",
	       of_at91rm9200_clk_main_osc_setup);

static void __init of_at91sam9x5_clk_main_rc_osc_setup(struct device_node *np)
{
	struct clk_hw *hw;
	u32 frequency = 0;
	u32 accuracy = 0;
	const char *name = np->name;
	struct regmap *regmap;

	of_property_read_string(np, "clock-output-names", &name);
	of_property_read_u32(np, "clock-frequency", &frequency);
	of_property_read_u32(np, "clock-accuracy", &accuracy);

	regmap = syscon_node_to_regmap(of_get_parent(np));
	if (IS_ERR(regmap))
		return;

	hw = at91_clk_register_main_rc_osc(regmap, name, frequency, accuracy);
	if (IS_ERR(hw))
		return;

	of_clk_add_hw_provider(np, of_clk_hw_simple_get, hw);
}
CLK_OF_DECLARE(at91sam9x5_clk_main_rc_osc, "atmel,at91sam9x5-clk-main-rc-osc",
	       of_at91sam9x5_clk_main_rc_osc_setup);

static void __init of_at91rm9200_clk_main_setup(struct device_node *np)
{
	struct clk_hw *hw;
	const char *parent_name;
	const char *name = np->name;
	struct regmap *regmap;

	parent_name = of_clk_get_parent_name(np, 0);
	of_property_read_string(np, "clock-output-names", &name);

	regmap = syscon_node_to_regmap(of_get_parent(np));
	if (IS_ERR(regmap))
		return;

	hw = at91_clk_register_rm9200_main(regmap, name, parent_name);
	if (IS_ERR(hw))
		return;

	of_clk_add_hw_provider(np, of_clk_hw_simple_get, hw);
}
CLK_OF_DECLARE(at91rm9200_clk_main, "atmel,at91rm9200-clk-main",
	       of_at91rm9200_clk_main_setup);

static void __init of_at91sam9x5_clk_main_setup(struct device_node *np)
{
	struct clk_hw *hw;
	const char *parent_names[2];
	unsigned int num_parents;
	const char *name = np->name;
	struct regmap *regmap;

	num_parents = of_clk_get_parent_count(np);
	if (num_parents == 0 || num_parents > 2)
		return;

	of_clk_parent_fill(np, parent_names, num_parents);
	regmap = syscon_node_to_regmap(of_get_parent(np));
	if (IS_ERR(regmap))
		return;

	of_property_read_string(np, "clock-output-names", &name);

	hw = at91_clk_register_sam9x5_main(regmap, name, parent_names,
					   num_parents);
	if (IS_ERR(hw))
		return;

	of_clk_add_hw_provider(np, of_clk_hw_simple_get, hw);
}
CLK_OF_DECLARE(at91sam9x5_clk_main, "atmel,at91sam9x5-clk-main",
	       of_at91sam9x5_clk_main_setup);

static struct clk_master_characteristics * __init
of_at91_clk_master_get_characteristics(struct device_node *np)
{
	struct clk_master_characteristics *characteristics;

	characteristics = kzalloc(sizeof(*characteristics), GFP_KERNEL);
	if (!characteristics)
		return NULL;

	if (of_at91_get_clk_range(np, "atmel,clk-output-range", &characteristics->output))
		goto out_free_characteristics;

	of_property_read_u32_array(np, "atmel,clk-divisors",
				   characteristics->divisors, 4);

	characteristics->have_div3_pres =
		of_property_read_bool(np, "atmel,master-clk-have-div3-pres");

	return characteristics;

out_free_characteristics:
	kfree(characteristics);
	return NULL;
}

static void __init
of_at91_clk_master_setup(struct device_node *np,
			 const struct clk_master_layout *layout)
{
	struct clk_hw *hw;
	unsigned int num_parents;
	const char *parent_names[MASTER_SOURCE_MAX];
	const char *name = np->name;
	struct clk_master_characteristics *characteristics;
	struct regmap *regmap;

	num_parents = of_clk_get_parent_count(np);
	if (num_parents == 0 || num_parents > MASTER_SOURCE_MAX)
		return;

	of_clk_parent_fill(np, parent_names, num_parents);

	of_property_read_string(np, "clock-output-names", &name);

	characteristics = of_at91_clk_master_get_characteristics(np);
	if (!characteristics)
		return;

	regmap = syscon_node_to_regmap(of_get_parent(np));
	if (IS_ERR(regmap))
		return;

	hw = at91_clk_register_master_pres(regmap, "masterck_pres", num_parents,
					   parent_names, layout,
					   characteristics, &mck_lock,
					   CLK_SET_RATE_GATE, INT_MIN);
	if (IS_ERR(hw))
		goto out_free_characteristics;

	hw = at91_clk_register_master_div(regmap, name, "masterck_pres",
					  layout, characteristics,
					  &mck_lock, CLK_SET_RATE_GATE);
	if (IS_ERR(hw))
		goto out_free_characteristics;

	of_clk_add_hw_provider(np, of_clk_hw_simple_get, hw);
	return;

out_free_characteristics:
	kfree(characteristics);
}

static void __init of_at91rm9200_clk_master_setup(struct device_node *np)
{
	of_at91_clk_master_setup(np, &at91rm9200_master_layout);
}
CLK_OF_DECLARE(at91rm9200_clk_master, "atmel,at91rm9200-clk-master",
	       of_at91rm9200_clk_master_setup);

static void __init of_at91sam9x5_clk_master_setup(struct device_node *np)
{
	of_at91_clk_master_setup(np, &at91sam9x5_master_layout);
}
CLK_OF_DECLARE(at91sam9x5_clk_master, "atmel,at91sam9x5-clk-master",
	       of_at91sam9x5_clk_master_setup);

static void __init
of_at91_clk_periph_setup(struct device_node *np, u8 type)
{
	int num;
	u32 id;
	struct clk_hw *hw;
	const char *parent_name;
	const char *name;
	struct device_node *periphclknp;
	struct regmap *regmap;

	parent_name = of_clk_get_parent_name(np, 0);
	if (!parent_name)
		return;

	num = of_get_child_count(np);
	if (!num || num > PERIPHERAL_MAX)
		return;

	regmap = syscon_node_to_regmap(of_get_parent(np));
	if (IS_ERR(regmap))
		return;

	for_each_child_of_node(np, periphclknp) {
		if (of_property_read_u32(periphclknp, "reg", &id))
			continue;

		if (id >= PERIPHERAL_MAX)
			continue;

		if (of_property_read_string(np, "clock-output-names", &name))
			name = periphclknp->name;

		if (type == PERIPHERAL_AT91RM9200) {
			hw = at91_clk_register_peripheral(regmap, name,
							  parent_name, id);
		} else {
			struct clk_range range = CLK_RANGE(0, 0);

			of_at91_get_clk_range(periphclknp,
					      "atmel,clk-output-range",
					      &range);

			hw = at91_clk_register_sam9x5_peripheral(regmap,
								 &pmc_pcr_lock,
								 &dt_pcr_layout,
								 name,
								 parent_name,
								 id, &range,
								 INT_MIN);
		}

		if (IS_ERR(hw))
			continue;

		of_clk_add_hw_provider(periphclknp, of_clk_hw_simple_get, hw);
	}
}

static void __init of_at91rm9200_clk_periph_setup(struct device_node *np)
{
	of_at91_clk_periph_setup(np, PERIPHERAL_AT91RM9200);
}
CLK_OF_DECLARE(at91rm9200_clk_periph, "atmel,at91rm9200-clk-peripheral",
	       of_at91rm9200_clk_periph_setup);

static void __init of_at91sam9x5_clk_periph_setup(struct device_node *np)
{
	of_at91_clk_periph_setup(np, PERIPHERAL_AT91SAM9X5);
}
CLK_OF_DECLARE(at91sam9x5_clk_periph, "atmel,at91sam9x5-clk-peripheral",
	       of_at91sam9x5_clk_periph_setup);

static struct clk_pll_characteristics * __init
of_at91_clk_pll_get_characteristics(struct device_node *np)
{
	int i;
	int offset;
	u32 tmp;
	int num_output;
	u32 num_cells;
	struct clk_range input;
	struct clk_range *output;
	u8 *out = NULL;
	u16 *icpll = NULL;
	struct clk_pll_characteristics *characteristics;

	if (of_at91_get_clk_range(np, "atmel,clk-input-range", &input))
		return NULL;

	if (of_property_read_u32(np, "#atmel,pll-clk-output-range-cells",
				 &num_cells))
		return NULL;

	if (num_cells < 2 || num_cells > 4)
		return NULL;

	if (!of_get_property(np, "atmel,pll-clk-output-ranges", &tmp))
		return NULL;
	num_output = tmp / (sizeof(u32) * num_cells);

	characteristics = kzalloc(sizeof(*characteristics), GFP_KERNEL);
	if (!characteristics)
		return NULL;

	output = kcalloc(num_output, sizeof(*output), GFP_KERNEL);
	if (!output)
		goto out_free_characteristics;

	if (num_cells > 2) {
		out = kcalloc(num_output, sizeof(*out), GFP_KERNEL);
		if (!out)
			goto out_free_output;
	}

	if (num_cells > 3) {
		icpll = kcalloc(num_output, sizeof(*icpll), GFP_KERNEL);
		if (!icpll)
			goto out_free_output;
	}

	for (i = 0; i < num_output; i++) {
		offset = i * num_cells;
		if (of_property_read_u32_index(np,
					       "atmel,pll-clk-output-ranges",
					       offset, &tmp))
			goto out_free_output;
		output[i].min = tmp;
		if (of_property_read_u32_index(np,
					       "atmel,pll-clk-output-ranges",
					       offset + 1, &tmp))
			goto out_free_output;
		output[i].max = tmp;

		if (num_cells == 2)
			continue;

		if (of_property_read_u32_index(np,
					       "atmel,pll-clk-output-ranges",
					       offset + 2, &tmp))
			goto out_free_output;
		out[i] = tmp;

		if (num_cells == 3)
			continue;

		if (of_property_read_u32_index(np,
					       "atmel,pll-clk-output-ranges",
					       offset + 3, &tmp))
			goto out_free_output;
		icpll[i] = tmp;
	}

	characteristics->input = input;
	characteristics->num_output = num_output;
	characteristics->output = output;
	characteristics->out = out;
	characteristics->icpll = icpll;
	return characteristics;

out_free_output:
	kfree(icpll);
	kfree(out);
	kfree(output);
out_free_characteristics:
	kfree(characteristics);
	return NULL;
}

static void __init
of_at91_clk_pll_setup(struct device_node *np,
		      const struct clk_pll_layout *layout)
{
	u32 id;
	struct clk_hw *hw;
	struct regmap *regmap;
	const char *parent_name;
	const char *name = np->name;
	struct clk_pll_characteristics *characteristics;

	if (of_property_read_u32(np, "reg", &id))
		return;

	parent_name = of_clk_get_parent_name(np, 0);

	of_property_read_string(np, "clock-output-names", &name);

	regmap = syscon_node_to_regmap(of_get_parent(np));
	if (IS_ERR(regmap))
		return;

	characteristics = of_at91_clk_pll_get_characteristics(np);
	if (!characteristics)
		return;

	hw = at91_clk_register_pll(regmap, name, parent_name, id, layout,
				   characteristics);
	if (IS_ERR(hw))
		goto out_free_characteristics;

	of_clk_add_hw_provider(np, of_clk_hw_simple_get, hw);
	return;

out_free_characteristics:
	kfree(characteristics);
}

static void __init of_at91rm9200_clk_pll_setup(struct device_node *np)
{
	of_at91_clk_pll_setup(np, &at91rm9200_pll_layout);
}
CLK_OF_DECLARE(at91rm9200_clk_pll, "atmel,at91rm9200-clk-pll",
	       of_at91rm9200_clk_pll_setup);

static void __init of_at91sam9g45_clk_pll_setup(struct device_node *np)
{
	of_at91_clk_pll_setup(np, &at91sam9g45_pll_layout);
}
CLK_OF_DECLARE(at91sam9g45_clk_pll, "atmel,at91sam9g45-clk-pll",
	       of_at91sam9g45_clk_pll_setup);

static void __init of_at91sam9g20_clk_pllb_setup(struct device_node *np)
{
	of_at91_clk_pll_setup(np, &at91sam9g20_pllb_layout);
}
CLK_OF_DECLARE(at91sam9g20_clk_pllb, "atmel,at91sam9g20-clk-pllb",
	       of_at91sam9g20_clk_pllb_setup);

static void __init of_sama5d3_clk_pll_setup(struct device_node *np)
{
	of_at91_clk_pll_setup(np, &sama5d3_pll_layout);
}
CLK_OF_DECLARE(sama5d3_clk_pll, "atmel,sama5d3-clk-pll",
	       of_sama5d3_clk_pll_setup);

static void __init
of_at91sam9x5_clk_plldiv_setup(struct device_node *np)
{
	struct clk_hw *hw;
	const char *parent_name;
	const char *name = np->name;
	struct regmap *regmap;

	parent_name = of_clk_get_parent_name(np, 0);

	of_property_read_string(np, "clock-output-names", &name);

	regmap = syscon_node_to_regmap(of_get_parent(np));
	if (IS_ERR(regmap))
		return;

	hw = at91_clk_register_plldiv(regmap, name, parent_name);
	if (IS_ERR(hw))
		return;

	of_clk_add_hw_provider(np, of_clk_hw_simple_get, hw);
}
CLK_OF_DECLARE(at91sam9x5_clk_plldiv, "atmel,at91sam9x5-clk-plldiv",
	       of_at91sam9x5_clk_plldiv_setup);

static void __init
of_at91_clk_prog_setup(struct device_node *np,
		       const struct clk_programmable_layout *layout,
		       u32 *mux_table)
{
	int num;
	u32 id;
	struct clk_hw *hw;
	unsigned int num_parents;
	const char *parent_names[PROG_SOURCE_MAX];
	const char *name;
	struct device_node *progclknp;
	struct regmap *regmap;

	num_parents = of_clk_get_parent_count(np);
	if (num_parents == 0 || num_parents > PROG_SOURCE_MAX)
		return;

	of_clk_parent_fill(np, parent_names, num_parents);

	num = of_get_child_count(np);
	if (!num || num > (PROG_ID_MAX + 1))
		return;

	regmap = syscon_node_to_regmap(of_get_parent(np));
	if (IS_ERR(regmap))
		return;

	for_each_child_of_node(np, progclknp) {
		if (of_property_read_u32(progclknp, "reg", &id))
			continue;

		if (of_property_read_string(np, "clock-output-names", &name))
			name = progclknp->name;

		hw = at91_clk_register_programmable(regmap, name,
						    parent_names, num_parents,
						    id, layout, mux_table);
		if (IS_ERR(hw))
			continue;

		of_clk_add_hw_provider(progclknp, of_clk_hw_simple_get, hw);
	}
}

static void __init of_at91rm9200_clk_prog_setup(struct device_node *np)
{
	of_at91_clk_prog_setup(np, &at91rm9200_programmable_layout, NULL);
}
CLK_OF_DECLARE(at91rm9200_clk_prog, "atmel,at91rm9200-clk-programmable",
	       of_at91rm9200_clk_prog_setup);

static void __init of_at91sam9g45_clk_prog_setup(struct device_node *np)
{
	of_at91_clk_prog_setup(np, &at91sam9g45_programmable_layout, NULL);
}
CLK_OF_DECLARE(at91sam9g45_clk_prog, "atmel,at91sam9g45-clk-programmable",
	       of_at91sam9g45_clk_prog_setup);

static void __init of_at91sam9x5_clk_prog_setup(struct device_node *np)
{
	of_at91_clk_prog_setup(np, &at91sam9x5_programmable_layout, NULL);
}
CLK_OF_DECLARE(at91sam9x5_clk_prog, "atmel,at91sam9x5-clk-programmable",
	       of_at91sam9x5_clk_prog_setup);

static void __init of_at91sam9260_clk_slow_setup(struct device_node *np)
{
	struct clk_hw *hw;
	const char *parent_names[2];
	unsigned int num_parents;
	const char *name = np->name;
	struct regmap *regmap;

	num_parents = of_clk_get_parent_count(np);
	if (num_parents != 2)
		return;

	of_clk_parent_fill(np, parent_names, num_parents);
	regmap = syscon_node_to_regmap(of_get_parent(np));
	if (IS_ERR(regmap))
		return;

	of_property_read_string(np, "clock-output-names", &name);

	hw = at91_clk_register_sam9260_slow(regmap, name, parent_names,
					    num_parents);
	if (IS_ERR(hw))
		return;

	of_clk_add_hw_provider(np, of_clk_hw_simple_get, hw);
}
CLK_OF_DECLARE(at91sam9260_clk_slow, "atmel,at91sam9260-clk-slow",
	       of_at91sam9260_clk_slow_setup);

#ifdef CONFIG_HAVE_AT91_SMD
#define SMD_SOURCE_MAX		2

static void __init of_at91sam9x5_clk_smd_setup(struct device_node *np)
{
	struct clk_hw *hw;
	unsigned int num_parents;
	const char *parent_names[SMD_SOURCE_MAX];
	const char *name = np->name;
	struct regmap *regmap;

	num_parents = of_clk_get_parent_count(np);
	if (num_parents == 0 || num_parents > SMD_SOURCE_MAX)
		return;

	of_clk_parent_fill(np, parent_names, num_parents);

	of_property_read_string(np, "clock-output-names", &name);

	regmap = syscon_node_to_regmap(of_get_parent(np));
	if (IS_ERR(regmap))
		return;

	hw = at91sam9x5_clk_register_smd(regmap, name, parent_names,
					 num_parents);
	if (IS_ERR(hw))
		return;

	of_clk_add_hw_provider(np, of_clk_hw_simple_get, hw);
}
CLK_OF_DECLARE(at91sam9x5_clk_smd, "atmel,at91sam9x5-clk-smd",
	       of_at91sam9x5_clk_smd_setup);
#endif /* CONFIG_HAVE_AT91_SMD */

static void __init of_at91rm9200_clk_sys_setup(struct device_node *np)
{
	int num;
	u32 id;
	struct clk_hw *hw;
	const char *name;
	struct device_node *sysclknp;
	const char *parent_name;
	struct regmap *regmap;

	num = of_get_child_count(np);
	if (num > (SYSTEM_MAX_ID + 1))
		return;

	regmap = syscon_node_to_regmap(of_get_parent(np));
	if (IS_ERR(regmap))
		return;

	for_each_child_of_node(np, sysclknp) {
		if (of_property_read_u32(sysclknp, "reg", &id))
			continue;

		if (of_property_read_string(np, "clock-output-names", &name))
			name = sysclknp->name;

		parent_name = of_clk_get_parent_name(sysclknp, 0);

		hw = at91_clk_register_system(regmap, name, parent_name, id);
		if (IS_ERR(hw))
			continue;

		of_clk_add_hw_provider(sysclknp, of_clk_hw_simple_get, hw);
	}
}
CLK_OF_DECLARE(at91rm9200_clk_sys, "atmel,at91rm9200-clk-system",
	       of_at91rm9200_clk_sys_setup);

#ifdef CONFIG_HAVE_AT91_USB_CLK
#define USB_SOURCE_MAX		2

static void __init of_at91sam9x5_clk_usb_setup(struct device_node *np)
{
	struct clk_hw *hw;
	unsigned int num_parents;
	const char *parent_names[USB_SOURCE_MAX];
	const char *name = np->name;
	struct regmap *regmap;

	num_parents = of_clk_get_parent_count(np);
	if (num_parents == 0 || num_parents > USB_SOURCE_MAX)
		return;

	of_clk_parent_fill(np, parent_names, num_parents);

	of_property_read_string(np, "clock-output-names", &name);

	regmap = syscon_node_to_regmap(of_get_parent(np));
	if (IS_ERR(regmap))
		return;

	hw = at91sam9x5_clk_register_usb(regmap, name, parent_names,
					 num_parents);
	if (IS_ERR(hw))
		return;

	of_clk_add_hw_provider(np, of_clk_hw_simple_get, hw);
}
CLK_OF_DECLARE(at91sam9x5_clk_usb, "atmel,at91sam9x5-clk-usb",
	       of_at91sam9x5_clk_usb_setup);

static void __init of_at91sam9n12_clk_usb_setup(struct device_node *np)
{
	struct clk_hw *hw;
	const char *parent_name;
	const char *name = np->name;
	struct regmap *regmap;

	parent_name = of_clk_get_parent_name(np, 0);
	if (!parent_name)
		return;

	of_property_read_string(np, "clock-output-names", &name);

	regmap = syscon_node_to_regmap(of_get_parent(np));
	if (IS_ERR(regmap))
		return;

	hw = at91sam9n12_clk_register_usb(regmap, name, parent_name);
	if (IS_ERR(hw))
		return;

	of_clk_add_hw_provider(np, of_clk_hw_simple_get, hw);
}
CLK_OF_DECLARE(at91sam9n12_clk_usb, "atmel,at91sam9n12-clk-usb",
	       of_at91sam9n12_clk_usb_setup);

static void __init of_at91rm9200_clk_usb_setup(struct device_node *np)
{
	struct clk_hw *hw;
	const char *parent_name;
	const char *name = np->name;
	u32 divisors[4] = {0, 0, 0, 0};
	struct regmap *regmap;

	parent_name = of_clk_get_parent_name(np, 0);
	if (!parent_name)
		return;

	of_property_read_u32_array(np, "atmel,clk-divisors", divisors, 4);
	if (!divisors[0])
		return;

	of_property_read_string(np, "clock-output-names", &name);

	regmap = syscon_node_to_regmap(of_get_parent(np));
	if (IS_ERR(regmap))
		return;
	hw = at91rm9200_clk_register_usb(regmap, name, parent_name, divisors);
	if (IS_ERR(hw))
		return;

	of_clk_add_hw_provider(np, of_clk_hw_simple_get, hw);
}
CLK_OF_DECLARE(at91rm9200_clk_usb, "atmel,at91rm9200-clk-usb",
	       of_at91rm9200_clk_usb_setup);
#endif /* CONFIG_HAVE_AT91_USB_CLK */

#ifdef CONFIG_HAVE_AT91_UTMI
static void __init of_at91sam9x5_clk_utmi_setup(struct device_node *np)
{
	struct clk_hw *hw;
	const char *parent_name;
	const char *name = np->name;
	struct regmap *regmap_pmc, *regmap_sfr;

	parent_name = of_clk_get_parent_name(np, 0);

	of_property_read_string(np, "clock-output-names", &name);

	regmap_pmc = syscon_node_to_regmap(of_get_parent(np));
	if (IS_ERR(regmap_pmc))
		return;

	/*
	 * If the device supports different mainck rates, this value has to be
	 * set in the UTMI Clock Trimming register.
	 * - 9x5: mainck supports several rates but it is indicated that a
	 *   12 MHz is needed in case of USB.
	 * - sama5d3 and sama5d2: mainck supports several rates. Configuring
	 *   the FREQ field of the UTMI Clock Trimming register is mandatory.
	 * - sama5d4: mainck is at 12 MHz.
	 *
	 * We only need to retrieve sama5d3 or sama5d2 sfr regmap.
	 */
	regmap_sfr = syscon_regmap_lookup_by_compatible("atmel,sama5d3-sfr");
	if (IS_ERR(regmap_sfr)) {
		regmap_sfr = syscon_regmap_lookup_by_compatible("atmel,sama5d2-sfr");
		if (IS_ERR(regmap_sfr))
			regmap_sfr = NULL;
	}

	hw = at91_clk_register_utmi(regmap_pmc, regmap_sfr, name, parent_name);
	if (IS_ERR(hw))
		return;

	of_clk_add_hw_provider(np, of_clk_hw_simple_get, hw);
}
CLK_OF_DECLARE(at91sam9x5_clk_utmi, "atmel,at91sam9x5-clk-utmi",
	       of_at91sam9x5_clk_utmi_setup);
#endif /* CONFIG_HAVE_AT91_UTMI */
