// SPDX-License-Identifier: GPL-2.0
/*
 * CS42L43 SoundWire driver
 *
 * Copyright (C) 2022-2023 Cirrus Logic, Inc. and
 *                         Cirrus Logic International Semiconductor Ltd.
 */

#include <linux/array_size.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/mfd/cs42l43.h>
#include <linux/mfd/cs42l43-regs.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/pm.h>
#include <linux/regmap.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_registers.h>
#include <linux/soundwire/sdw_type.h>

#include "cs42l43.h"

#define CS42L43_SDW_PORT(port, chans) { \
	.num = port, \
	.max_ch = chans, \
	.type = SDW_DPN_FULL, \
	.max_word = 24, \
}

static const struct regmap_config cs42l43_sdw_regmap = {
	.reg_bits		= 32,
	.reg_stride		= 4,
	.val_bits		= 32,
	.reg_format_endian	= REGMAP_ENDIAN_LITTLE,
	.val_format_endian	= REGMAP_ENDIAN_LITTLE,

	.max_register		= CS42L43_MCU_RAM_MAX,
	.readable_reg		= cs42l43_readable_register,
	.volatile_reg		= cs42l43_volatile_register,
	.precious_reg		= cs42l43_precious_register,

	.cache_type		= REGCACHE_MAPLE,
	.reg_defaults		= cs42l43_reg_default,
	.num_reg_defaults	= ARRAY_SIZE(cs42l43_reg_default),
};

static const struct sdw_dpn_prop cs42l43_src_port_props[] = {
	CS42L43_SDW_PORT(1, 4),
	CS42L43_SDW_PORT(2, 2),
	CS42L43_SDW_PORT(3, 2),
	CS42L43_SDW_PORT(4, 2),
};

static const struct sdw_dpn_prop cs42l43_sink_port_props[] = {
	CS42L43_SDW_PORT(5, 2),
	CS42L43_SDW_PORT(6, 2),
	CS42L43_SDW_PORT(7, 2),
};

static int cs42l43_read_prop(struct sdw_slave *sdw)
{
	struct sdw_slave_prop *prop = &sdw->prop;
	struct device *dev = &sdw->dev;
	int i;

	prop->use_domain_irq = true;
	prop->paging_support = true;
	prop->wake_capable = true;
	prop->quirks = SDW_SLAVE_QUIRKS_INVALID_INITIAL_PARITY;
	prop->scp_int1_mask = SDW_SCP_INT1_BUS_CLASH | SDW_SCP_INT1_PARITY |
			      SDW_SCP_INT1_IMPL_DEF;

	for (i = 0; i < ARRAY_SIZE(cs42l43_src_port_props); i++)
		prop->source_ports |= BIT(cs42l43_src_port_props[i].num);

	prop->src_dpn_prop = devm_kmemdup(dev, cs42l43_src_port_props,
					  sizeof(cs42l43_src_port_props), GFP_KERNEL);
	if (!prop->src_dpn_prop)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(cs42l43_sink_port_props); i++)
		prop->sink_ports |= BIT(cs42l43_sink_port_props[i].num);

	prop->sink_dpn_prop = devm_kmemdup(dev, cs42l43_sink_port_props,
					   sizeof(cs42l43_sink_port_props), GFP_KERNEL);
	if (!prop->sink_dpn_prop)
		return -ENOMEM;

	return 0;
}

static int cs42l43_sdw_update_status(struct sdw_slave *sdw, enum sdw_slave_status status)
{
	struct cs42l43 *cs42l43 = dev_get_drvdata(&sdw->dev);

	switch (status) {
	case SDW_SLAVE_ATTACHED:
		dev_dbg(cs42l43->dev, "Device attach\n");

		sdw_write_no_pm(sdw, CS42L43_GEN_INT_MASK_1,
				CS42L43_INT_STAT_GEN1_MASK);

		cs42l43->attached = true;

		complete(&cs42l43->device_attach);
		break;
	case SDW_SLAVE_UNATTACHED:
		dev_dbg(cs42l43->dev, "Device detach\n");

		cs42l43->attached = false;

		reinit_completion(&cs42l43->device_attach);
		complete(&cs42l43->device_detach);
		break;
	default:
		break;
	}

	return 0;
}

static int cs42l43_sdw_interrupt(struct sdw_slave *sdw,
				 struct sdw_slave_intr_status *status)
{
	/*
	 * The IRQ itself was handled through the regmap_irq handler, this is
	 * just clearing up the additional Cirrus SoundWire registers that are
	 * not covered by the SoundWire framework or the IRQ handler itself.
	 * There is only a single bit in GEN_INT_STAT_1 and it doesn't clear if
	 * IRQs are still pending so doing a read/write here after handling the
	 * IRQ is fine.
	 */
	sdw_read_no_pm(sdw, CS42L43_GEN_INT_STAT_1);
	sdw_write_no_pm(sdw, CS42L43_GEN_INT_STAT_1, CS42L43_INT_STAT_GEN1_MASK);

	return 0;
}

static int cs42l43_sdw_bus_config(struct sdw_slave *sdw,
				  struct sdw_bus_params *params)
{
	struct cs42l43 *cs42l43 = dev_get_drvdata(&sdw->dev);
	int ret = 0;

	mutex_lock(&cs42l43->pll_lock);

	if (cs42l43->sdw_freq != params->curr_dr_freq / 2) {
		if (cs42l43->sdw_pll_active) {
			dev_err(cs42l43->dev,
				"PLL active can't change SoundWire bus clock\n");
			ret = -EBUSY;
		} else {
			cs42l43->sdw_freq = params->curr_dr_freq / 2;
		}
	}

	mutex_unlock(&cs42l43->pll_lock);

	return ret;
}

static const struct sdw_slave_ops cs42l43_sdw_ops = {
	.read_prop		= cs42l43_read_prop,
	.update_status		= cs42l43_sdw_update_status,
	.interrupt_callback	= cs42l43_sdw_interrupt,
	.bus_config		= cs42l43_sdw_bus_config,
};

static int cs42l43_sdw_probe(struct sdw_slave *sdw, const struct sdw_device_id *id)
{
	struct cs42l43 *cs42l43;
	struct device *dev = &sdw->dev;

	cs42l43 = devm_kzalloc(dev, sizeof(*cs42l43), GFP_KERNEL);
	if (!cs42l43)
		return -ENOMEM;

	cs42l43->dev = dev;
	cs42l43->sdw = sdw;

	cs42l43->regmap = devm_regmap_init_sdw(sdw, &cs42l43_sdw_regmap);
	if (IS_ERR(cs42l43->regmap))
		return dev_err_probe(cs42l43->dev, PTR_ERR(cs42l43->regmap),
				     "Failed to allocate regmap\n");

	return cs42l43_dev_probe(cs42l43);
}

static int cs42l43_sdw_remove(struct sdw_slave *sdw)
{
	struct cs42l43 *cs42l43 = dev_get_drvdata(&sdw->dev);

	cs42l43_dev_remove(cs42l43);

	return 0;
}

static const struct sdw_device_id cs42l43_sdw_id[] = {
	SDW_SLAVE_ENTRY(0x01FA, 0x4243, 0),
	{}
};
MODULE_DEVICE_TABLE(sdw, cs42l43_sdw_id);

static struct sdw_driver cs42l43_sdw_driver = {
	.driver = {
		.name		= "cs42l43",
		.pm		= pm_ptr(&cs42l43_pm_ops),
	},

	.probe		= cs42l43_sdw_probe,
	.remove		= cs42l43_sdw_remove,
	.id_table	= cs42l43_sdw_id,
	.ops		= &cs42l43_sdw_ops,
};
module_sdw_driver(cs42l43_sdw_driver);

MODULE_IMPORT_NS(MFD_CS42L43);

MODULE_DESCRIPTION("CS42L43 SoundWire Driver");
MODULE_AUTHOR("Lucas Tanure <tanureal@opensource.cirrus.com>");
MODULE_LICENSE("GPL");
