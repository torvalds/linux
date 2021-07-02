// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018-2019 Intel Corporation

#include <linux/bitfield.h>
#include <linux/mfd/core.h>
#include <linux/mfd/intel-peci-client.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/peci.h>

#define CPU_ID_MODEL_MASK	GENMASK(7, 4)
#define CPU_ID_FAMILY_MASK	GENMASK(11, 8)
#define CPU_ID_EXT_MODEL_MASK	GENMASK(19, 16)
#define CPU_ID_EXT_FAMILY_MASK	GENMASK(27, 20)

#define LOWER_NIBBLE_MASK	GENMASK(3, 0)
#define UPPER_NIBBLE_MASK	GENMASK(7, 4)
#define LOWER_BYTE_MASK		GENMASK(7, 0)
#define UPPER_BYTE_MASK		GENMASK(16, 8)

static struct mfd_cell peci_functions[] = {
	{ .name = "peci-cputemp", },
	{ .name = "peci-dimmtemp", },
	{ .name = "peci-cpupower", },
	{ .name = "peci-dimmpower", },
};

static const struct cpu_gen_info cpu_gen_info_table[] = {
	{ /* Haswell Xeon */
		.family         = INTEL_FAM6,
		.model          = INTEL_FAM6_HASWELL_X,
		.core_mask_bits = CORE_MASK_BITS_ON_HSX,
		.chan_rank_max  = CHAN_RANK_MAX_ON_HSX,
		.dimm_idx_max   = DIMM_IDX_MAX_ON_HSX },
	{ /* Broadwell Xeon */
		.family         = INTEL_FAM6,
		.model          = INTEL_FAM6_BROADWELL_X,
		.core_mask_bits = CORE_MASK_BITS_ON_BDX,
		.chan_rank_max  = CHAN_RANK_MAX_ON_BDX,
		.dimm_idx_max   = DIMM_IDX_MAX_ON_BDX },
	{ /* Skylake Xeon */
		.family         = INTEL_FAM6,
		.model          = INTEL_FAM6_SKYLAKE_X,
		.core_mask_bits = CORE_MASK_BITS_ON_SKX,
		.chan_rank_max  = CHAN_RANK_MAX_ON_SKX,
		.dimm_idx_max   = DIMM_IDX_MAX_ON_SKX },
	{ /* Skylake Xeon D */
		.family         = INTEL_FAM6,
		.model          = INTEL_FAM6_SKYLAKE_XD,
		.core_mask_bits = CORE_MASK_BITS_ON_SKXD,
		.chan_rank_max  = CHAN_RANK_MAX_ON_SKXD,
		.dimm_idx_max   = DIMM_IDX_MAX_ON_SKXD },
	{ /* Icelake Xeon */
		.family         = INTEL_FAM6,
		.model          = INTEL_FAM6_ICELAKE_X,
		.core_mask_bits = CORE_MASK_BITS_ON_ICX,
		.chan_rank_max  = CHAN_RANK_MAX_ON_ICX,
		.dimm_idx_max   = DIMM_IDX_MAX_ON_ICX },
	{ /* Icelake Xeon D */
		.family         = INTEL_FAM6,
		.model          = INTEL_FAM6_ICELAKE_XD,
		.core_mask_bits = CORE_MASK_BITS_ON_ICXD,
		.chan_rank_max  = CHAN_RANK_MAX_ON_ICXD,
		.dimm_idx_max   = DIMM_IDX_MAX_ON_ICXD },
};

static int peci_client_get_cpu_gen_info(struct peci_client_manager *priv)
{
	struct device *dev = &priv->client->dev;
	u32 cpu_id;
	u16 family;
	u8 model;
	int ret;
	int i;

	ret = peci_get_cpu_id(priv->client->adapter, priv->client->addr,
			      &cpu_id);
	if (ret)
		return ret;

	family = FIELD_PREP(LOWER_BYTE_MASK,
			    FIELD_GET(CPU_ID_FAMILY_MASK, cpu_id)) |
		 FIELD_PREP(UPPER_BYTE_MASK,
			    FIELD_GET(CPU_ID_EXT_FAMILY_MASK, cpu_id));
	model = FIELD_PREP(LOWER_NIBBLE_MASK,
			   FIELD_GET(CPU_ID_MODEL_MASK, cpu_id)) |
		FIELD_PREP(UPPER_NIBBLE_MASK,
			   FIELD_GET(CPU_ID_EXT_MODEL_MASK, cpu_id));

	for (i = 0; i < ARRAY_SIZE(cpu_gen_info_table); i++) {
		const struct cpu_gen_info *cpu_info = &cpu_gen_info_table[i];

		if (family == cpu_info->family && model == cpu_info->model) {
			priv->gen_info = cpu_info;
			break;
		}
	}

	if (!priv->gen_info) {
		dev_err(dev, "Can't support this CPU: 0x%x\n", cpu_id);
		ret = -ENODEV;
	}

	return ret;
}

static int peci_client_probe(struct peci_client *client)
{
	struct device *dev = &client->dev;
	struct peci_client_manager *priv;
	int device_id;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	dev_set_drvdata(dev, priv);
	priv->client = client;

	ret = peci_client_get_cpu_gen_info(priv);
	if (ret)
		return ret;

	device_id = (client->adapter->nr << 4) | (client->addr - PECI_BASE_ADDR);

	ret = devm_mfd_add_devices(dev, device_id, peci_functions,
				   ARRAY_SIZE(peci_functions), NULL, 0, NULL);
	if (ret < 0) {
		dev_err(dev, "Failed to register child devices: %d\n", ret);
		return ret;
	}

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id peci_client_of_table[] = {
	{ .compatible = "intel,peci-client" },
	{ }
};
MODULE_DEVICE_TABLE(of, peci_client_of_table);
#endif

static const struct peci_device_id peci_client_ids[] = {
	{ .name = "peci-client" },
	{ }
};
MODULE_DEVICE_TABLE(peci, peci_client_ids);

static struct peci_driver peci_client_driver = {
	.probe		= peci_client_probe,
	.id_table	= peci_client_ids,
	.driver		= {
		.name		= KBUILD_MODNAME,
		.of_match_table	= of_match_ptr(peci_client_of_table),
	},
};
module_peci_driver(peci_client_driver);

MODULE_AUTHOR("Jae Hyun Yoo <jae.hyun.yoo@linux.intel.com>");
MODULE_DESCRIPTION("PECI client driver");
MODULE_LICENSE("GPL v2");
