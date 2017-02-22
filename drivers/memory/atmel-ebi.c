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

struct at91sam9_smc_timings {
	u32 ncs_rd_setup_ns;
	u32 nrd_setup_ns;
	u32 ncs_wr_setup_ns;
	u32 nwe_setup_ns;
	u32 ncs_rd_pulse_ns;
	u32 nrd_pulse_ns;
	u32 ncs_wr_pulse_ns;
	u32 nwe_pulse_ns;
	u32 nrd_cycle_ns;
	u32 nwe_cycle_ns;
	u32 tdf_ns;
};

struct at91sam9_smc_generic_fields {
	struct regmap_field *setup;
	struct regmap_field *pulse;
	struct regmap_field *cycle;
	struct regmap_field *mode;
};

struct at91sam9_ebi_dev_config {
	struct at91sam9_smc_timings timings;
	u32 mode;
};

struct at91_ebi_dev_config {
	int cs;
	union {
		struct at91sam9_ebi_dev_config sam9;
	};
};

struct at91_ebi;

struct at91_ebi_dev {
	struct list_head node;
	struct at91_ebi *ebi;
	u32 mode;
	int numcs;
	struct at91_ebi_dev_config configs[];
};

struct at91_ebi_caps {
	unsigned int available_cs;
	const struct reg_field *ebi_csa;
	void (*get_config)(struct at91_ebi_dev *ebid,
			   struct at91_ebi_dev_config *conf);
	int (*xlate_config)(struct at91_ebi_dev *ebid,
			    struct device_node *configs_np,
			    struct at91_ebi_dev_config *conf);
	int (*apply_config)(struct at91_ebi_dev *ebid,
			    struct at91_ebi_dev_config *conf);
	int (*init)(struct at91_ebi *ebi);
};

struct at91_ebi {
	struct clk *clk;
	struct regmap *smc;
	struct regmap *matrix;

	struct regmap_field *ebi_csa;

	struct device *dev;
	const struct at91_ebi_caps *caps;
	struct list_head devs;
	union {
		struct at91sam9_smc_generic_fields sam9;
	};
};

static void at91sam9_ebi_get_config(struct at91_ebi_dev *ebid,
				    struct at91_ebi_dev_config *conf)
{
	struct at91sam9_smc_generic_fields *fields = &ebid->ebi->sam9;
	unsigned int clk_rate = clk_get_rate(ebid->ebi->clk);
	struct at91sam9_ebi_dev_config *config = &conf->sam9;
	struct at91sam9_smc_timings *timings = &config->timings;
	unsigned int val;

	regmap_fields_read(fields->mode, conf->cs, &val);
	config->mode = val & ~AT91_SMC_TDF;

	val = (val & AT91_SMC_TDF) >> 16;
	timings->tdf_ns = clk_rate * val;

	regmap_fields_read(fields->setup, conf->cs, &val);
	timings->ncs_rd_setup_ns = (val >> 24) & 0x1f;
	timings->ncs_rd_setup_ns += ((val >> 29) & 0x1) * 128;
	timings->ncs_rd_setup_ns *= clk_rate;
	timings->nrd_setup_ns = (val >> 16) & 0x1f;
	timings->nrd_setup_ns += ((val >> 21) & 0x1) * 128;
	timings->nrd_setup_ns *= clk_rate;
	timings->ncs_wr_setup_ns = (val >> 8) & 0x1f;
	timings->ncs_wr_setup_ns += ((val >> 13) & 0x1) * 128;
	timings->ncs_wr_setup_ns *= clk_rate;
	timings->nwe_setup_ns = val & 0x1f;
	timings->nwe_setup_ns += ((val >> 5) & 0x1) * 128;
	timings->nwe_setup_ns *= clk_rate;

	regmap_fields_read(fields->pulse, conf->cs, &val);
	timings->ncs_rd_pulse_ns = (val >> 24) & 0x3f;
	timings->ncs_rd_pulse_ns += ((val >> 30) & 0x1) * 256;
	timings->ncs_rd_pulse_ns *= clk_rate;
	timings->nrd_pulse_ns = (val >> 16) & 0x3f;
	timings->nrd_pulse_ns += ((val >> 22) & 0x1) * 256;
	timings->nrd_pulse_ns *= clk_rate;
	timings->ncs_wr_pulse_ns = (val >> 8) & 0x3f;
	timings->ncs_wr_pulse_ns += ((val >> 14) & 0x1) * 256;
	timings->ncs_wr_pulse_ns *= clk_rate;
	timings->nwe_pulse_ns = val & 0x3f;
	timings->nwe_pulse_ns += ((val >> 6) & 0x1) * 256;
	timings->nwe_pulse_ns *= clk_rate;

	regmap_fields_read(fields->cycle, conf->cs, &val);
	timings->nrd_cycle_ns = (val >> 16) & 0x7f;
	timings->nrd_cycle_ns += ((val >> 23) & 0x3) * 256;
	timings->nrd_cycle_ns *= clk_rate;
	timings->nwe_cycle_ns = val & 0x7f;
	timings->nwe_cycle_ns += ((val >> 7) & 0x3) * 256;
	timings->nwe_cycle_ns *= clk_rate;
}

static int at91_xlate_timing(struct device_node *np, const char *prop,
			     u32 *val, bool *required)
{
	if (!of_property_read_u32(np, prop, val)) {
		*required = true;
		return 0;
	}

	if (*required)
		return -EINVAL;

	return 0;
}

static int at91sam9_smc_xslate_timings(struct at91_ebi_dev *ebid,
				       struct device_node *np,
				       struct at91sam9_smc_timings *timings,
				       bool *required)
{
	int ret;

	ret = at91_xlate_timing(np, "atmel,smc-ncs-rd-setup-ns",
				&timings->ncs_rd_setup_ns, required);
	if (ret)
		goto out;

	ret = at91_xlate_timing(np, "atmel,smc-nrd-setup-ns",
				&timings->nrd_setup_ns, required);
	if (ret)
		goto out;

	ret = at91_xlate_timing(np, "atmel,smc-ncs-wr-setup-ns",
				&timings->ncs_wr_setup_ns, required);
	if (ret)
		goto out;

	ret = at91_xlate_timing(np, "atmel,smc-nwe-setup-ns",
				&timings->nwe_setup_ns, required);
	if (ret)
		goto out;

	ret = at91_xlate_timing(np, "atmel,smc-ncs-rd-pulse-ns",
				&timings->ncs_rd_pulse_ns, required);
	if (ret)
		goto out;

	ret = at91_xlate_timing(np, "atmel,smc-nrd-pulse-ns",
				&timings->nrd_pulse_ns, required);
	if (ret)
		goto out;

	ret = at91_xlate_timing(np, "atmel,smc-ncs-wr-pulse-ns",
				&timings->ncs_wr_pulse_ns, required);
	if (ret)
		goto out;

	ret = at91_xlate_timing(np, "atmel,smc-nwe-pulse-ns",
				&timings->nwe_pulse_ns, required);
	if (ret)
		goto out;

	ret = at91_xlate_timing(np, "atmel,smc-nwe-cycle-ns",
				&timings->nwe_cycle_ns, required);
	if (ret)
		goto out;

	ret = at91_xlate_timing(np, "atmel,smc-nrd-cycle-ns",
				&timings->nrd_cycle_ns, required);
	if (ret)
		goto out;

	ret = at91_xlate_timing(np, "atmel,smc-tdf-ns",
				&timings->tdf_ns, required);

out:
	if (ret)
		dev_err(ebid->ebi->dev,
			"missing or invalid timings definition in %s",
			np->full_name);

	return ret;
}

static int at91sam9_ebi_xslate_config(struct at91_ebi_dev *ebid,
				      struct device_node *np,
				      struct at91_ebi_dev_config *conf)
{
	struct at91sam9_ebi_dev_config *config = &conf->sam9;
	bool required = false;
	const char *tmp_str;
	u32 tmp;
	int ret;

	ret = of_property_read_u32(np, "atmel,smc-bus-width", &tmp);
	if (!ret) {
		switch (tmp) {
		case 8:
			config->mode |= AT91_SMC_DBW_8;
			break;

		case 16:
			config->mode |= AT91_SMC_DBW_16;
			break;

		case 32:
			config->mode |= AT91_SMC_DBW_32;
			break;

		default:
			return -EINVAL;
		}

		required = true;
	}

	if (of_property_read_bool(np, "atmel,smc-tdf-optimized")) {
		config->mode |= AT91_SMC_TDFMODE_OPTIMIZED;
		required = true;
	}

	tmp_str = NULL;
	of_property_read_string(np, "atmel,smc-byte-access-type", &tmp_str);
	if (tmp_str && !strcmp(tmp_str, "write")) {
		config->mode |= AT91_SMC_BAT_WRITE;
		required = true;
	}

	tmp_str = NULL;
	of_property_read_string(np, "atmel,smc-read-mode", &tmp_str);
	if (tmp_str && !strcmp(tmp_str, "nrd")) {
		config->mode |= AT91_SMC_READMODE_NRD;
		required = true;
	}

	tmp_str = NULL;
	of_property_read_string(np, "atmel,smc-write-mode", &tmp_str);
	if (tmp_str && !strcmp(tmp_str, "nwe")) {
		config->mode |= AT91_SMC_WRITEMODE_NWE;
		required = true;
	}

	tmp_str = NULL;
	of_property_read_string(np, "atmel,smc-exnw-mode", &tmp_str);
	if (tmp_str) {
		if (!strcmp(tmp_str, "frozen"))
			config->mode |= AT91_SMC_EXNWMODE_FROZEN;
		else if (!strcmp(tmp_str, "ready"))
			config->mode |= AT91_SMC_EXNWMODE_READY;
		else if (strcmp(tmp_str, "disabled"))
			return -EINVAL;

		required = true;
	}

	ret = of_property_read_u32(np, "atmel,smc-page-mode", &tmp);
	if (!ret) {
		switch (tmp) {
		case 4:
			config->mode |= AT91_SMC_PS_4;
			break;

		case 8:
			config->mode |= AT91_SMC_PS_8;
			break;

		case 16:
			config->mode |= AT91_SMC_PS_16;
			break;

		case 32:
			config->mode |= AT91_SMC_PS_32;
			break;

		default:
			return -EINVAL;
		}

		config->mode |= AT91_SMC_PMEN;
		required = true;
	}

	ret = at91sam9_smc_xslate_timings(ebid, np, &config->timings,
					  &required);
	if (ret)
		return ret;

	return required;
}

static int at91sam9_ebi_apply_config(struct at91_ebi_dev *ebid,
				     struct at91_ebi_dev_config *conf)
{
	unsigned int clk_rate = clk_get_rate(ebid->ebi->clk);
	struct at91sam9_ebi_dev_config *config = &conf->sam9;
	struct at91sam9_smc_timings *timings = &config->timings;
	struct at91sam9_smc_generic_fields *fields = &ebid->ebi->sam9;
	u32 coded_val;
	u32 val;

	coded_val = at91sam9_smc_setup_ns_to_cycles(clk_rate,
						    timings->ncs_rd_setup_ns);
	val = AT91SAM9_SMC_NCS_NRDSETUP(coded_val);
	coded_val = at91sam9_smc_setup_ns_to_cycles(clk_rate,
						    timings->nrd_setup_ns);
	val |= AT91SAM9_SMC_NRDSETUP(coded_val);
	coded_val = at91sam9_smc_setup_ns_to_cycles(clk_rate,
						    timings->ncs_wr_setup_ns);
	val |= AT91SAM9_SMC_NCS_WRSETUP(coded_val);
	coded_val = at91sam9_smc_setup_ns_to_cycles(clk_rate,
						    timings->nwe_setup_ns);
	val |= AT91SAM9_SMC_NWESETUP(coded_val);
	regmap_fields_write(fields->setup, conf->cs, val);

	coded_val = at91sam9_smc_pulse_ns_to_cycles(clk_rate,
						    timings->ncs_rd_pulse_ns);
	val = AT91SAM9_SMC_NCS_NRDPULSE(coded_val);
	coded_val = at91sam9_smc_pulse_ns_to_cycles(clk_rate,
						    timings->nrd_pulse_ns);
	val |= AT91SAM9_SMC_NRDPULSE(coded_val);
	coded_val = at91sam9_smc_pulse_ns_to_cycles(clk_rate,
						    timings->ncs_wr_pulse_ns);
	val |= AT91SAM9_SMC_NCS_WRPULSE(coded_val);
	coded_val = at91sam9_smc_pulse_ns_to_cycles(clk_rate,
						    timings->nwe_pulse_ns);
	val |= AT91SAM9_SMC_NWEPULSE(coded_val);
	regmap_fields_write(fields->pulse, conf->cs, val);

	coded_val = at91sam9_smc_cycle_ns_to_cycles(clk_rate,
						    timings->nrd_cycle_ns);
	val = AT91SAM9_SMC_NRDCYCLE(coded_val);
	coded_val = at91sam9_smc_cycle_ns_to_cycles(clk_rate,
						    timings->nwe_cycle_ns);
	val |= AT91SAM9_SMC_NWECYCLE(coded_val);
	regmap_fields_write(fields->cycle, conf->cs, val);

	val = DIV_ROUND_UP(timings->tdf_ns, clk_rate);
	if (val > AT91_SMC_TDF_MAX)
		val = AT91_SMC_TDF_MAX;
	regmap_fields_write(fields->mode, conf->cs,
			    config->mode | AT91_SMC_TDF_(val));

	return 0;
}

static int at91sam9_ebi_init(struct at91_ebi *ebi)
{
	struct at91sam9_smc_generic_fields *fields = &ebi->sam9;
	struct reg_field field = REG_FIELD(0, 0, 31);

	field.id_size = fls(ebi->caps->available_cs);
	field.id_offset = AT91SAM9_SMC_GENERIC_BLK_SZ;

	field.reg = AT91SAM9_SMC_SETUP(AT91SAM9_SMC_GENERIC);
	fields->setup = devm_regmap_field_alloc(ebi->dev, ebi->smc, field);
	if (IS_ERR(fields->setup))
		return PTR_ERR(fields->setup);

	field.reg = AT91SAM9_SMC_PULSE(AT91SAM9_SMC_GENERIC);
	fields->pulse = devm_regmap_field_alloc(ebi->dev, ebi->smc, field);
	if (IS_ERR(fields->pulse))
		return PTR_ERR(fields->pulse);

	field.reg = AT91SAM9_SMC_CYCLE(AT91SAM9_SMC_GENERIC);
	fields->cycle = devm_regmap_field_alloc(ebi->dev, ebi->smc, field);
	if (IS_ERR(fields->cycle))
		return PTR_ERR(fields->cycle);

	field.reg = AT91SAM9_SMC_MODE(AT91SAM9_SMC_GENERIC);
	fields->mode = devm_regmap_field_alloc(ebi->dev, ebi->smc, field);
	return PTR_ERR_OR_ZERO(fields->mode);
}

static int sama5d3_ebi_init(struct at91_ebi *ebi)
{
	struct at91sam9_smc_generic_fields *fields = &ebi->sam9;
	struct reg_field field = REG_FIELD(0, 0, 31);

	field.id_size = fls(ebi->caps->available_cs);
	field.id_offset = SAMA5_SMC_GENERIC_BLK_SZ;

	field.reg = AT91SAM9_SMC_SETUP(SAMA5_SMC_GENERIC);
	fields->setup = devm_regmap_field_alloc(ebi->dev, ebi->smc, field);
	if (IS_ERR(fields->setup))
		return PTR_ERR(fields->setup);

	field.reg = AT91SAM9_SMC_PULSE(SAMA5_SMC_GENERIC);
	fields->pulse = devm_regmap_field_alloc(ebi->dev, ebi->smc, field);
	if (IS_ERR(fields->pulse))
		return PTR_ERR(fields->pulse);

	field.reg = AT91SAM9_SMC_CYCLE(SAMA5_SMC_GENERIC);
	fields->cycle = devm_regmap_field_alloc(ebi->dev, ebi->smc, field);
	if (IS_ERR(fields->cycle))
		return PTR_ERR(fields->cycle);

	field.reg = SAMA5_SMC_MODE(SAMA5_SMC_GENERIC);
	fields->mode = devm_regmap_field_alloc(ebi->dev, ebi->smc, field);
	return PTR_ERR_OR_ZERO(fields->mode);
}

static int at91_ebi_dev_setup(struct at91_ebi *ebi, struct device_node *np,
			      int reg_cells)
{
	const struct at91_ebi_caps *caps = ebi->caps;
	struct at91_ebi_dev_config conf = { };
	struct device *dev = ebi->dev;
	struct at91_ebi_dev *ebid;
	int ret, numcs = 0, i;
	bool apply = false;

	numcs = of_property_count_elems_of_size(np, "reg",
						reg_cells * sizeof(u32));
	if (numcs <= 0) {
		dev_err(dev, "invalid reg property in %s\n", np->full_name);
		return -EINVAL;
	}

	ebid = devm_kzalloc(ebi->dev,
			    sizeof(*ebid) + (numcs * sizeof(*ebid->configs)),
			    GFP_KERNEL);
	if (!ebid)
		return -ENOMEM;

	ebid->ebi = ebi;

	ret = caps->xlate_config(ebid, np, &conf);
	if (ret < 0)
		return ret;
	else if (ret)
		apply = true;

	for (i = 0; i < numcs; i++) {
		u32 cs;

		ret = of_property_read_u32_index(np, "reg", i * reg_cells,
						 &cs);
		if (ret)
			return ret;

		if (cs > AT91_MATRIX_EBI_NUM_CS ||
		    !(ebi->caps->available_cs & BIT(cs))) {
			dev_err(dev, "invalid reg property in %s\n",
				np->full_name);
			return -EINVAL;
		}

		ebid->configs[i].cs = cs;

		if (apply) {
			conf.cs = cs;
			ret = caps->apply_config(ebid, &conf);
			if (ret)
				return ret;
		}

		caps->get_config(ebid, &ebid->configs[i]);

		/*
		 * Attach the EBI device to the generic SMC logic if at least
		 * one "atmel,smc-" property is present.
		 */
		if (ebi->ebi_csa && ret)
			regmap_field_update_bits(ebi->ebi_csa,
						 BIT(cs), 0);
	}

	list_add_tail(&ebid->node, &ebi->devs);

	return 0;
}

static const struct reg_field at91sam9260_ebi_csa =
				REG_FIELD(AT91SAM9260_MATRIX_EBICSA, 0,
					  AT91_MATRIX_EBI_NUM_CS - 1);

static const struct at91_ebi_caps at91sam9260_ebi_caps = {
	.available_cs = 0xff,
	.ebi_csa = &at91sam9260_ebi_csa,
	.get_config = at91sam9_ebi_get_config,
	.xlate_config = at91sam9_ebi_xslate_config,
	.apply_config = at91sam9_ebi_apply_config,
	.init = at91sam9_ebi_init,
};

static const struct reg_field at91sam9261_ebi_csa =
				REG_FIELD(AT91SAM9261_MATRIX_EBICSA, 0,
					  AT91_MATRIX_EBI_NUM_CS - 1);

static const struct at91_ebi_caps at91sam9261_ebi_caps = {
	.available_cs = 0xff,
	.ebi_csa = &at91sam9261_ebi_csa,
	.get_config = at91sam9_ebi_get_config,
	.xlate_config = at91sam9_ebi_xslate_config,
	.apply_config = at91sam9_ebi_apply_config,
	.init = at91sam9_ebi_init,
};

static const struct reg_field at91sam9263_ebi0_csa =
				REG_FIELD(AT91SAM9263_MATRIX_EBI0CSA, 0,
					  AT91_MATRIX_EBI_NUM_CS - 1);

static const struct at91_ebi_caps at91sam9263_ebi0_caps = {
	.available_cs = 0x3f,
	.ebi_csa = &at91sam9263_ebi0_csa,
	.get_config = at91sam9_ebi_get_config,
	.xlate_config = at91sam9_ebi_xslate_config,
	.apply_config = at91sam9_ebi_apply_config,
	.init = at91sam9_ebi_init,
};

static const struct reg_field at91sam9263_ebi1_csa =
				REG_FIELD(AT91SAM9263_MATRIX_EBI1CSA, 0,
					  AT91_MATRIX_EBI_NUM_CS - 1);

static const struct at91_ebi_caps at91sam9263_ebi1_caps = {
	.available_cs = 0x7,
	.ebi_csa = &at91sam9263_ebi1_csa,
	.get_config = at91sam9_ebi_get_config,
	.xlate_config = at91sam9_ebi_xslate_config,
	.apply_config = at91sam9_ebi_apply_config,
	.init = at91sam9_ebi_init,
};

static const struct reg_field at91sam9rl_ebi_csa =
				REG_FIELD(AT91SAM9RL_MATRIX_EBICSA, 0,
					  AT91_MATRIX_EBI_NUM_CS - 1);

static const struct at91_ebi_caps at91sam9rl_ebi_caps = {
	.available_cs = 0x3f,
	.ebi_csa = &at91sam9rl_ebi_csa,
	.get_config = at91sam9_ebi_get_config,
	.xlate_config = at91sam9_ebi_xslate_config,
	.apply_config = at91sam9_ebi_apply_config,
	.init = at91sam9_ebi_init,
};

static const struct reg_field at91sam9g45_ebi_csa =
				REG_FIELD(AT91SAM9G45_MATRIX_EBICSA, 0,
					  AT91_MATRIX_EBI_NUM_CS - 1);

static const struct at91_ebi_caps at91sam9g45_ebi_caps = {
	.available_cs = 0x3f,
	.ebi_csa = &at91sam9g45_ebi_csa,
	.get_config = at91sam9_ebi_get_config,
	.xlate_config = at91sam9_ebi_xslate_config,
	.apply_config = at91sam9_ebi_apply_config,
	.init = at91sam9_ebi_init,
};

static const struct at91_ebi_caps at91sam9x5_ebi_caps = {
	.available_cs = 0x3f,
	.ebi_csa = &at91sam9263_ebi0_csa,
	.get_config = at91sam9_ebi_get_config,
	.xlate_config = at91sam9_ebi_xslate_config,
	.apply_config = at91sam9_ebi_apply_config,
	.init = at91sam9_ebi_init,
};

static const struct at91_ebi_caps sama5d3_ebi_caps = {
	.available_cs = 0xf,
	.get_config = at91sam9_ebi_get_config,
	.xlate_config = at91sam9_ebi_xslate_config,
	.apply_config = at91sam9_ebi_apply_config,
	.init = sama5d3_ebi_init,
};

static const struct of_device_id at91_ebi_id_table[] = {
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
	{ /* sentinel */ }
};

static int at91_ebi_dev_disable(struct at91_ebi *ebi, struct device_node *np)
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

static int at91_ebi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *child, *np = dev->of_node;
	const struct of_device_id *match;
	struct at91_ebi *ebi;
	int ret, reg_cells;
	struct clk *clk;
	u32 val;

	match = of_match_device(at91_ebi_id_table, dev);
	if (!match || !match->data)
		return -EINVAL;

	ebi = devm_kzalloc(dev, sizeof(*ebi), GFP_KERNEL);
	if (!ebi)
		return -ENOMEM;

	INIT_LIST_HEAD(&ebi->devs);
	ebi->caps = match->data;
	ebi->dev = dev;

	clk = devm_clk_get(dev, NULL);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	ebi->clk = clk;

	ebi->smc = syscon_regmap_lookup_by_phandle(np, "atmel,smc");
	if (IS_ERR(ebi->smc))
		return PTR_ERR(ebi->smc);

	/*
	 * The sama5d3 does not provide an EBICSA register and thus does need
	 * to access the matrix registers.
	 */
	if (ebi->caps->ebi_csa) {
		ebi->matrix =
			syscon_regmap_lookup_by_phandle(np, "atmel,matrix");
		if (IS_ERR(ebi->matrix))
			return PTR_ERR(ebi->matrix);

		ebi->ebi_csa = regmap_field_alloc(ebi->matrix,
						  *ebi->caps->ebi_csa);
		if (IS_ERR(ebi->ebi_csa))
			return PTR_ERR(ebi->ebi_csa);
	}

	ret = ebi->caps->init(ebi);
	if (ret)
		return ret;

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

		ret = at91_ebi_dev_setup(ebi, child, reg_cells);
		if (ret) {
			dev_err(dev, "failed to configure EBI bus for %s, disabling the device",
				child->full_name);

			ret = at91_ebi_dev_disable(ebi, child);
			if (ret)
				return ret;
		}
	}

	return of_platform_populate(np, NULL, NULL, dev);
}

static struct platform_driver at91_ebi_driver = {
	.driver = {
		.name = "atmel-ebi",
		.of_match_table	= at91_ebi_id_table,
	},
};
builtin_platform_driver_probe(at91_ebi_driver, at91_ebi_probe);
