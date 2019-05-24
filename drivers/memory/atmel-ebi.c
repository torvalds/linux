/*
 * EBI driver for Atmel chips
 * inspired by the fsl weim bus driver
 *
 * Copyright (C) 2013 Jean-Jacques Hiblot <jjhiblot@traphandler.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/mfd/syscon/atmel-matrix.h>
#include <linux/mfd/syscon/atmel-smc.h>
#include <linux/init.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <soc/at91/atmel-sfr.h>

struct atmel_ebi_dev_config {
	int cs;
	struct atmel_smc_cs_conf smcconf;
};

struct atmel_ebi;

struct atmel_ebi_dev {
	struct list_head node;
	struct atmel_ebi *ebi;
	u32 mode;
	int numcs;
	struct atmel_ebi_dev_config configs[];
};

struct atmel_ebi_caps {
	unsigned int available_cs;
	unsigned int ebi_csa_offs;
	const char *regmap_name;
	void (*get_config)(struct atmel_ebi_dev *ebid,
			   struct atmel_ebi_dev_config *conf);
	int (*xlate_config)(struct atmel_ebi_dev *ebid,
			    struct device_node *configs_np,
			    struct atmel_ebi_dev_config *conf);
	void (*apply_config)(struct atmel_ebi_dev *ebid,
			     struct atmel_ebi_dev_config *conf);
};

struct atmel_ebi {
	struct clk *clk;
	struct regmap *regmap;
	struct  {
		struct regmap *regmap;
		struct clk *clk;
		const struct atmel_hsmc_reg_layout *layout;
	} smc;

	struct device *dev;
	const struct atmel_ebi_caps *caps;
	struct list_head devs;
};

struct atmel_smc_timing_xlate {
	const char *name;
	int (*converter)(struct atmel_smc_cs_conf *conf,
			 unsigned int shift, unsigned int nycles);
	unsigned int shift;
};

#define ATMEL_SMC_SETUP_XLATE(nm, pos)	\
	{ .name = nm, .converter = atmel_smc_cs_conf_set_setup, .shift = pos}

#define ATMEL_SMC_PULSE_XLATE(nm, pos)	\
	{ .name = nm, .converter = atmel_smc_cs_conf_set_pulse, .shift = pos}

#define ATMEL_SMC_CYCLE_XLATE(nm, pos)	\
	{ .name = nm, .converter = atmel_smc_cs_conf_set_cycle, .shift = pos}

static void at91sam9_ebi_get_config(struct atmel_ebi_dev *ebid,
				    struct atmel_ebi_dev_config *conf)
{
	atmel_smc_cs_conf_get(ebid->ebi->smc.regmap, conf->cs,
			      &conf->smcconf);
}

static void sama5_ebi_get_config(struct atmel_ebi_dev *ebid,
				 struct atmel_ebi_dev_config *conf)
{
	atmel_hsmc_cs_conf_get(ebid->ebi->smc.regmap, ebid->ebi->smc.layout,
			       conf->cs, &conf->smcconf);
}

static const struct atmel_smc_timing_xlate timings_xlate_table[] = {
	ATMEL_SMC_SETUP_XLATE("atmel,smc-ncs-rd-setup-ns",
			      ATMEL_SMC_NCS_RD_SHIFT),
	ATMEL_SMC_SETUP_XLATE("atmel,smc-ncs-wr-setup-ns",
			      ATMEL_SMC_NCS_WR_SHIFT),
	ATMEL_SMC_SETUP_XLATE("atmel,smc-nrd-setup-ns", ATMEL_SMC_NRD_SHIFT),
	ATMEL_SMC_SETUP_XLATE("atmel,smc-nwe-setup-ns", ATMEL_SMC_NWE_SHIFT),
	ATMEL_SMC_PULSE_XLATE("atmel,smc-ncs-rd-pulse-ns",
			      ATMEL_SMC_NCS_RD_SHIFT),
	ATMEL_SMC_PULSE_XLATE("atmel,smc-ncs-wr-pulse-ns",
			      ATMEL_SMC_NCS_WR_SHIFT),
	ATMEL_SMC_PULSE_XLATE("atmel,smc-nrd-pulse-ns", ATMEL_SMC_NRD_SHIFT),
	ATMEL_SMC_PULSE_XLATE("atmel,smc-nwe-pulse-ns", ATMEL_SMC_NWE_SHIFT),
	ATMEL_SMC_CYCLE_XLATE("atmel,smc-nrd-cycle-ns", ATMEL_SMC_NRD_SHIFT),
	ATMEL_SMC_CYCLE_XLATE("atmel,smc-nwe-cycle-ns", ATMEL_SMC_NWE_SHIFT),
};

static int atmel_ebi_xslate_smc_timings(struct atmel_ebi_dev *ebid,
					struct device_node *np,
					struct atmel_smc_cs_conf *smcconf)
{
	unsigned int clk_rate = clk_get_rate(ebid->ebi->clk);
	unsigned int clk_period_ns = NSEC_PER_SEC / clk_rate;
	bool required = false;
	unsigned int ncycles;
	int ret, i;
	u32 val;

	ret = of_property_read_u32(np, "atmel,smc-tdf-ns", &val);
	if (!ret) {
		required = true;
		ncycles = DIV_ROUND_UP(val, clk_period_ns);
		if (ncycles > ATMEL_SMC_MODE_TDF_MAX) {
			ret = -EINVAL;
			goto out;
		}

		if (ncycles < ATMEL_SMC_MODE_TDF_MIN)
			ncycles = ATMEL_SMC_MODE_TDF_MIN;

		smcconf->mode |= ATMEL_SMC_MODE_TDF(ncycles);
	}

	for (i = 0; i < ARRAY_SIZE(timings_xlate_table); i++) {
		const struct atmel_smc_timing_xlate *xlate;

		xlate = &timings_xlate_table[i];

		ret = of_property_read_u32(np, xlate->name, &val);
		if (ret) {
			if (!required)
				continue;
			else
				break;
		}

		if (!required) {
			ret = -EINVAL;
			break;
		}

		ncycles = DIV_ROUND_UP(val, clk_period_ns);
		ret = xlate->converter(smcconf, xlate->shift, ncycles);
		if (ret)
			goto out;
	}

out:
	if (ret) {
		dev_err(ebid->ebi->dev,
			"missing or invalid timings definition in %pOF",
			np);
		return ret;
	}

	return required;
}

static int atmel_ebi_xslate_smc_config(struct atmel_ebi_dev *ebid,
				       struct device_node *np,
				       struct atmel_ebi_dev_config *conf)
{
	struct atmel_smc_cs_conf *smcconf = &conf->smcconf;
	bool required = false;
	const char *tmp_str;
	u32 tmp;
	int ret;

	ret = of_property_read_u32(np, "atmel,smc-bus-width", &tmp);
	if (!ret) {
		switch (tmp) {
		case 8:
			smcconf->mode |= ATMEL_SMC_MODE_DBW_8;
			break;

		case 16:
			smcconf->mode |= ATMEL_SMC_MODE_DBW_16;
			break;

		case 32:
			smcconf->mode |= ATMEL_SMC_MODE_DBW_32;
			break;

		default:
			return -EINVAL;
		}

		required = true;
	}

	if (of_property_read_bool(np, "atmel,smc-tdf-optimized")) {
		smcconf->mode |= ATMEL_SMC_MODE_TDFMODE_OPTIMIZED;
		required = true;
	}

	tmp_str = NULL;
	of_property_read_string(np, "atmel,smc-byte-access-type", &tmp_str);
	if (tmp_str && !strcmp(tmp_str, "write")) {
		smcconf->mode |= ATMEL_SMC_MODE_BAT_WRITE;
		required = true;
	}

	tmp_str = NULL;
	of_property_read_string(np, "atmel,smc-read-mode", &tmp_str);
	if (tmp_str && !strcmp(tmp_str, "nrd")) {
		smcconf->mode |= ATMEL_SMC_MODE_READMODE_NRD;
		required = true;
	}

	tmp_str = NULL;
	of_property_read_string(np, "atmel,smc-write-mode", &tmp_str);
	if (tmp_str && !strcmp(tmp_str, "nwe")) {
		smcconf->mode |= ATMEL_SMC_MODE_WRITEMODE_NWE;
		required = true;
	}

	tmp_str = NULL;
	of_property_read_string(np, "atmel,smc-exnw-mode", &tmp_str);
	if (tmp_str) {
		if (!strcmp(tmp_str, "frozen"))
			smcconf->mode |= ATMEL_SMC_MODE_EXNWMODE_FROZEN;
		else if (!strcmp(tmp_str, "ready"))
			smcconf->mode |= ATMEL_SMC_MODE_EXNWMODE_READY;
		else if (strcmp(tmp_str, "disabled"))
			return -EINVAL;

		required = true;
	}

	ret = of_property_read_u32(np, "atmel,smc-page-mode", &tmp);
	if (!ret) {
		switch (tmp) {
		case 4:
			smcconf->mode |= ATMEL_SMC_MODE_PS_4;
			break;

		case 8:
			smcconf->mode |= ATMEL_SMC_MODE_PS_8;
			break;

		case 16:
			smcconf->mode |= ATMEL_SMC_MODE_PS_16;
			break;

		case 32:
			smcconf->mode |= ATMEL_SMC_MODE_PS_32;
			break;

		default:
			return -EINVAL;
		}

		smcconf->mode |= ATMEL_SMC_MODE_PMEN;
		required = true;
	}

	ret = atmel_ebi_xslate_smc_timings(ebid, np, &conf->smcconf);
	if (ret < 0)
		return -EINVAL;

	if ((ret > 0 && !required) || (!ret && required)) {
		dev_err(ebid->ebi->dev, "missing atmel,smc- properties in %pOF",
			np);
		return -EINVAL;
	}

	return required;
}

static void at91sam9_ebi_apply_config(struct atmel_ebi_dev *ebid,
				      struct atmel_ebi_dev_config *conf)
{
	atmel_smc_cs_conf_apply(ebid->ebi->smc.regmap, conf->cs,
				&conf->smcconf);
}

static void sama5_ebi_apply_config(struct atmel_ebi_dev *ebid,
				   struct atmel_ebi_dev_config *conf)
{
	atmel_hsmc_cs_conf_apply(ebid->ebi->smc.regmap, ebid->ebi->smc.layout,
				 conf->cs, &conf->smcconf);
}

static int atmel_ebi_dev_setup(struct atmel_ebi *ebi, struct device_node *np,
			       int reg_cells)
{
	const struct atmel_ebi_caps *caps = ebi->caps;
	struct atmel_ebi_dev_config conf = { };
	struct device *dev = ebi->dev;
	struct atmel_ebi_dev *ebid;
	unsigned long cslines = 0;
	int ret, numcs = 0, nentries, i;
	bool apply = false;
	u32 cs;

	nentries = of_property_count_elems_of_size(np, "reg",
						   reg_cells * sizeof(u32));
	for (i = 0; i < nentries; i++) {
		ret = of_property_read_u32_index(np, "reg", i * reg_cells,
						 &cs);
		if (ret)
			return ret;

		if (cs >= AT91_MATRIX_EBI_NUM_CS ||
		    !(ebi->caps->available_cs & BIT(cs))) {
			dev_err(dev, "invalid reg property in %pOF\n", np);
			return -EINVAL;
		}

		if (!test_and_set_bit(cs, &cslines))
			numcs++;
	}

	if (!numcs) {
		dev_err(dev, "invalid reg property in %pOF\n", np);
		return -EINVAL;
	}

	ebid = devm_kzalloc(ebi->dev, struct_size(ebid, configs, numcs),
			    GFP_KERNEL);
	if (!ebid)
		return -ENOMEM;

	ebid->ebi = ebi;
	ebid->numcs = numcs;

	ret = caps->xlate_config(ebid, np, &conf);
	if (ret < 0)
		return ret;
	else if (ret)
		apply = true;

	i = 0;
	for_each_set_bit(cs, &cslines, AT91_MATRIX_EBI_NUM_CS) {
		ebid->configs[i].cs = cs;

		if (apply) {
			conf.cs = cs;
			caps->apply_config(ebid, &conf);
		}

		caps->get_config(ebid, &ebid->configs[i]);

		/*
		 * Attach the EBI device to the generic SMC logic if at least
		 * one "atmel,smc-" property is present.
		 */
		if (ebi->caps->ebi_csa_offs && apply)
			regmap_update_bits(ebi->regmap,
					   ebi->caps->ebi_csa_offs,
					   BIT(cs), 0);

		i++;
	}

	list_add_tail(&ebid->node, &ebi->devs);

	return 0;
}

static const struct atmel_ebi_caps at91sam9260_ebi_caps = {
	.available_cs = 0xff,
	.ebi_csa_offs = AT91SAM9260_MATRIX_EBICSA,
	.regmap_name = "atmel,matrix",
	.get_config = at91sam9_ebi_get_config,
	.xlate_config = atmel_ebi_xslate_smc_config,
	.apply_config = at91sam9_ebi_apply_config,
};

static const struct atmel_ebi_caps at91sam9261_ebi_caps = {
	.available_cs = 0xff,
	.ebi_csa_offs = AT91SAM9261_MATRIX_EBICSA,
	.regmap_name = "atmel,matrix",
	.get_config = at91sam9_ebi_get_config,
	.xlate_config = atmel_ebi_xslate_smc_config,
	.apply_config = at91sam9_ebi_apply_config,
};

static const struct atmel_ebi_caps at91sam9263_ebi0_caps = {
	.available_cs = 0x3f,
	.ebi_csa_offs = AT91SAM9263_MATRIX_EBI0CSA,
	.regmap_name = "atmel,matrix",
	.get_config = at91sam9_ebi_get_config,
	.xlate_config = atmel_ebi_xslate_smc_config,
	.apply_config = at91sam9_ebi_apply_config,
};

static const struct atmel_ebi_caps at91sam9263_ebi1_caps = {
	.available_cs = 0x7,
	.ebi_csa_offs = AT91SAM9263_MATRIX_EBI1CSA,
	.regmap_name = "atmel,matrix",
	.get_config = at91sam9_ebi_get_config,
	.xlate_config = atmel_ebi_xslate_smc_config,
	.apply_config = at91sam9_ebi_apply_config,
};

static const struct atmel_ebi_caps at91sam9rl_ebi_caps = {
	.available_cs = 0x3f,
	.ebi_csa_offs = AT91SAM9RL_MATRIX_EBICSA,
	.regmap_name = "atmel,matrix",
	.get_config = at91sam9_ebi_get_config,
	.xlate_config = atmel_ebi_xslate_smc_config,
	.apply_config = at91sam9_ebi_apply_config,
};

static const struct atmel_ebi_caps at91sam9g45_ebi_caps = {
	.available_cs = 0x3f,
	.ebi_csa_offs = AT91SAM9G45_MATRIX_EBICSA,
	.regmap_name = "atmel,matrix",
	.get_config = at91sam9_ebi_get_config,
	.xlate_config = atmel_ebi_xslate_smc_config,
	.apply_config = at91sam9_ebi_apply_config,
};

static const struct atmel_ebi_caps at91sam9x5_ebi_caps = {
	.available_cs = 0x3f,
	.ebi_csa_offs = AT91SAM9X5_MATRIX_EBICSA,
	.regmap_name = "atmel,matrix",
	.get_config = at91sam9_ebi_get_config,
	.xlate_config = atmel_ebi_xslate_smc_config,
	.apply_config = at91sam9_ebi_apply_config,
};

static const struct atmel_ebi_caps sama5d3_ebi_caps = {
	.available_cs = 0xf,
	.get_config = sama5_ebi_get_config,
	.xlate_config = atmel_ebi_xslate_smc_config,
	.apply_config = sama5_ebi_apply_config,
};

static const struct atmel_ebi_caps sam9x60_ebi_caps = {
	.available_cs = 0x3f,
	.ebi_csa_offs = AT91_SFR_CCFG_EBICSA,
	.regmap_name = "microchip,sfr",
	.get_config = at91sam9_ebi_get_config,
	.xlate_config = atmel_ebi_xslate_smc_config,
	.apply_config = at91sam9_ebi_apply_config,
};

static const struct of_device_id atmel_ebi_id_table[] = {
	{
		.compatible = "atmel,at91sam9260-ebi",
		.data = &at91sam9260_ebi_caps,
	},
	{
		.compatible = "atmel,at91sam9261-ebi",
		.data = &at91sam9261_ebi_caps,
	},
	{
		.compatible = "atmel,at91sam9263-ebi0",
		.data = &at91sam9263_ebi0_caps,
	},
	{
		.compatible = "atmel,at91sam9263-ebi1",
		.data = &at91sam9263_ebi1_caps,
	},
	{
		.compatible = "atmel,at91sam9rl-ebi",
		.data = &at91sam9rl_ebi_caps,
	},
	{
		.compatible = "atmel,at91sam9g45-ebi",
		.data = &at91sam9g45_ebi_caps,
	},
	{
		.compatible = "atmel,at91sam9x5-ebi",
		.data = &at91sam9x5_ebi_caps,
	},
	{
		.compatible = "atmel,sama5d3-ebi",
		.data = &sama5d3_ebi_caps,
	},
	{
		.compatible = "microchip,sam9x60-ebi",
		.data = &sam9x60_ebi_caps,
	},
	{ /* sentinel */ }
};

static int atmel_ebi_dev_disable(struct atmel_ebi *ebi, struct device_node *np)
{
	struct device *dev = ebi->dev;
	struct property *newprop;

	newprop = devm_kzalloc(dev, sizeof(*newprop), GFP_KERNEL);
	if (!newprop)
		return -ENOMEM;

	newprop->name = devm_kstrdup(dev, "status", GFP_KERNEL);
	if (!newprop->name)
		return -ENOMEM;

	newprop->value = devm_kstrdup(dev, "disabled", GFP_KERNEL);
	if (!newprop->value)
		return -ENOMEM;

	newprop->length = sizeof("disabled");

	return of_update_property(np, newprop);
}

static int atmel_ebi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *child, *np = dev->of_node, *smc_np;
	const struct of_device_id *match;
	struct atmel_ebi *ebi;
	int ret, reg_cells;
	struct clk *clk;
	u32 val;

	match = of_match_device(atmel_ebi_id_table, dev);
	if (!match || !match->data)
		return -EINVAL;

	ebi = devm_kzalloc(dev, sizeof(*ebi), GFP_KERNEL);
	if (!ebi)
		return -ENOMEM;

	platform_set_drvdata(pdev, ebi);

	INIT_LIST_HEAD(&ebi->devs);
	ebi->caps = match->data;
	ebi->dev = dev;

	clk = devm_clk_get(dev, NULL);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	ebi->clk = clk;

	smc_np = of_parse_phandle(dev->of_node, "atmel,smc", 0);

	ebi->smc.regmap = syscon_node_to_regmap(smc_np);
	if (IS_ERR(ebi->smc.regmap))
		return PTR_ERR(ebi->smc.regmap);

	ebi->smc.layout = atmel_hsmc_get_reg_layout(smc_np);
	if (IS_ERR(ebi->smc.layout))
		return PTR_ERR(ebi->smc.layout);

	ebi->smc.clk = of_clk_get(smc_np, 0);
	if (IS_ERR(ebi->smc.clk)) {
		if (PTR_ERR(ebi->smc.clk) != -ENOENT)
			return PTR_ERR(ebi->smc.clk);

		ebi->smc.clk = NULL;
	}
	ret = clk_prepare_enable(ebi->smc.clk);
	if (ret)
		return ret;

	/*
	 * The sama5d3 does not provide an EBICSA register and thus does need
	 * to access it.
	 */
	if (ebi->caps->ebi_csa_offs) {
		ebi->regmap =
			syscon_regmap_lookup_by_phandle(np,
							ebi->caps->regmap_name);
		if (IS_ERR(ebi->regmap))
			return PTR_ERR(ebi->regmap);
	}

	ret = of_property_read_u32(np, "#address-cells", &val);
	if (ret) {
		dev_err(dev, "missing #address-cells property\n");
		return ret;
	}

	reg_cells = val;

	ret = of_property_read_u32(np, "#size-cells", &val);
	if (ret) {
		dev_err(dev, "missing #address-cells property\n");
		return ret;
	}

	reg_cells += val;

	for_each_available_child_of_node(np, child) {
		if (!of_find_property(child, "reg", NULL))
			continue;

		ret = atmel_ebi_dev_setup(ebi, child, reg_cells);
		if (ret) {
			dev_err(dev, "failed to configure EBI bus for %pOF, disabling the device",
				child);

			ret = atmel_ebi_dev_disable(ebi, child);
			if (ret)
				return ret;
		}
	}

	return of_platform_populate(np, NULL, NULL, dev);
}

static __maybe_unused int atmel_ebi_resume(struct device *dev)
{
	struct atmel_ebi *ebi = dev_get_drvdata(dev);
	struct atmel_ebi_dev *ebid;

	list_for_each_entry(ebid, &ebi->devs, node) {
		int i;

		for (i = 0; i < ebid->numcs; i++)
			ebid->ebi->caps->apply_config(ebid, &ebid->configs[i]);
	}

	return 0;
}

static SIMPLE_DEV_PM_OPS(atmel_ebi_pm_ops, NULL, atmel_ebi_resume);

static struct platform_driver atmel_ebi_driver = {
	.driver = {
		.name = "atmel-ebi",
		.of_match_table	= atmel_ebi_id_table,
		.pm = &atmel_ebi_pm_ops,
	},
};
builtin_platform_driver_probe(atmel_ebi_driver, atmel_ebi_probe);
