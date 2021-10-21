// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018-2019 Intel Corporation

#include <linux/hwmon.h>
#include <linux/jiffies.h>
#include <linux/mfd/intel-peci-client.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include "peci-hwmon.h"

#define DEFAULT_CHANNEL_NUMS	5
#define MODTEMP_CHANNEL_NUMS	CORE_MASK_BITS_MAX
#define CPUTEMP_CHANNEL_NUMS	(DEFAULT_CHANNEL_NUMS + MODTEMP_CHANNEL_NUMS)
#define BIOS_RST_CPL3		BIT(3)

struct temp_group {
	struct peci_sensor_data		die;
	struct peci_sensor_data		dts;
	struct peci_sensor_data		tcontrol;
	struct peci_sensor_data		tthrottle;
	struct peci_sensor_data		tjmax;
	struct peci_sensor_data		module[MODTEMP_CHANNEL_NUMS];
};

struct peci_cputemp {
	struct peci_client_manager	*mgr;
	struct device			*dev;
	char				name[PECI_NAME_SIZE];
	const struct cpu_gen_info	*gen_info;
	struct temp_group		temp;
	u64				core_mask;
	u32				temp_config[CPUTEMP_CHANNEL_NUMS + 1];
	uint				config_idx;
	struct hwmon_channel_info	temp_info;
	const struct hwmon_channel_info	*info[2];
	struct hwmon_chip_info		chip;
	char				**module_temp_label;
};

enum cputemp_channels {
	channel_die,
	channel_dts,
	channel_tcontrol,
	channel_tthrottle,
	channel_tjmax,
	channel_core,
};

static const u32 config_table[] = {
	/* Die temperature */
	HWMON_T_LABEL | HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_CRIT |
	HWMON_T_CRIT_HYST,

	/* DTS margin */
	HWMON_T_LABEL | HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_CRIT |
	HWMON_T_CRIT_HYST,

	/* Tcontrol temperature */
	HWMON_T_LABEL | HWMON_T_INPUT | HWMON_T_CRIT,

	/* Tthrottle temperature */
	HWMON_T_LABEL | HWMON_T_INPUT,

	/* Tjmax temperature */
	HWMON_T_LABEL | HWMON_T_INPUT,

	/* Core temperature - for all core channels */
	HWMON_T_LABEL | HWMON_T_INPUT,
};

static const char *cputemp_label[DEFAULT_CHANNEL_NUMS] = {
	"Die",
	"DTS",
	"Tcontrol",
	"Tthrottle",
	"Tjmax"
};

static s32 ten_dot_six_to_millidegree(s32 val)
{
	return ((val ^ 0x8000) - 0x8000) * 1000 / 64;
}

static int get_temp_targets(struct peci_cputemp *priv)
{
	struct peci_rd_end_pt_cfg_msg re_msg;
	u32 bios_reset_cpl_cfg;
	s32 tthrottle_offset;
	s32 tcontrol_margin;
	u8  pkg_cfg[4];
	int ret;

	/*
	 * Just use only the tcontrol marker to determine if target values need
	 * update.
	 */
	if (!peci_sensor_need_update(&priv->temp.tcontrol))
		return 0;

	/*
	 * CPU can return invalid temperatures prior to BIOS-PCU handshake
	 * RST_CPL3 completion so filter the invalid readings out.
	 */
	switch (priv->gen_info->model) {
	case INTEL_FAM6_ICELAKE_X:
	case INTEL_FAM6_ICELAKE_XD:
		re_msg.addr = priv->mgr->client->addr;
		re_msg.msg_type = PECI_ENDPTCFG_TYPE_LOCAL_PCI;
		re_msg.params.pci_cfg.seg = 0;
		re_msg.params.pci_cfg.bus = 31;
		re_msg.params.pci_cfg.device = 30;
		re_msg.params.pci_cfg.function = 1;
		re_msg.params.pci_cfg.reg = 0x94;
		re_msg.rx_len = 4;

		ret = peci_command(priv->mgr->client->adapter,
				   PECI_CMD_RD_END_PT_CFG, &re_msg);
		if (ret || re_msg.cc != PECI_DEV_CC_SUCCESS)
			ret = -EAGAIN;
		if (ret)
			return ret;

		bios_reset_cpl_cfg = le32_to_cpup((__le32 *)re_msg.data);
		if (!(bios_reset_cpl_cfg & BIOS_RST_CPL3)) {
			dev_dbg(priv->dev, "BIOS and Pcode Node ID isn't configured, BIOS_RESET_CPL_CFG: 0x%x\n",
				bios_reset_cpl_cfg);
			return -EAGAIN;
		}

		break;
	default:
		/* TODO: Check reset completion for other CPUs if needed */
		break;
	}

	ret = peci_client_read_package_config(priv->mgr,
					      PECI_MBX_INDEX_TEMP_TARGET, 0,
					      pkg_cfg);
	if (ret)
		return ret;

	priv->temp.tjmax.value = pkg_cfg[2] * 1000;

	tcontrol_margin = pkg_cfg[1];
	tcontrol_margin = ((tcontrol_margin ^ 0x80) - 0x80) * 1000;
	priv->temp.tcontrol.value = priv->temp.tjmax.value - tcontrol_margin;

	tthrottle_offset = (pkg_cfg[3] & 0x2f) * 1000;
	priv->temp.tthrottle.value = priv->temp.tjmax.value - tthrottle_offset;

	peci_sensor_mark_updated(&priv->temp.tcontrol);

	return 0;
}

static int get_die_temp(struct peci_cputemp *priv)
{
	struct peci_get_temp_msg msg;
	int ret;

	if (!peci_sensor_need_update(&priv->temp.die))
		return 0;

	msg.addr = priv->mgr->client->addr;

	ret = peci_command(priv->mgr->client->adapter, PECI_CMD_GET_TEMP, &msg);
	if (ret)
		return ret;

	/* Note that the tjmax should be available before calling it */
	priv->temp.die.value = priv->temp.tjmax.value +
			       (msg.temp_raw * 1000 / 64);

	peci_sensor_mark_updated(&priv->temp.die);

	return 0;
}

static int get_dts(struct peci_cputemp *priv)
{
	s32 dts_margin;
	u8  pkg_cfg[4];
	int ret;

	if (!peci_sensor_need_update(&priv->temp.dts))
		return 0;

	ret = peci_client_read_package_config(priv->mgr,
					      PECI_MBX_INDEX_DTS_MARGIN, 0,
					      pkg_cfg);

	if (ret)
		return ret;

	dts_margin = le16_to_cpup((__le16 *)pkg_cfg);

	/**
	 * Processors return a value of DTS reading in 10.6 format
	 * (10 bits signed decimal, 6 bits fractional).
	 * Error codes:
	 *   0x8000: General sensor error
	 *   0x8001: Reserved
	 *   0x8002: Underflow on reading value
	 *   0x8003-0x81ff: Reserved
	 */
	if (dts_margin >= 0x8000 && dts_margin <= 0x81ff)
		return -EIO;

	dts_margin = ten_dot_six_to_millidegree(dts_margin);

	/* Note that the tcontrol should be available before calling it */
	priv->temp.dts.value = priv->temp.tcontrol.value - dts_margin;

	peci_sensor_mark_updated(&priv->temp.dts);

	return 0;
}

static int get_module_temp(struct peci_cputemp *priv, int index)
{
	s32 module_dts_margin;
	u8  pkg_cfg[4];
	int ret;

	if (!peci_sensor_need_update(&priv->temp.module[index]))
		return 0;

	ret = peci_client_read_package_config(priv->mgr,
					      PECI_MBX_INDEX_MODULE_TEMP,
					      index, pkg_cfg);
	if (ret)
		return ret;

	module_dts_margin = le16_to_cpup((__le16 *)pkg_cfg);

	/*
	 * Processors return a value of the DTS reading in 10.6 format
	 * (10 bits signed decimal, 6 bits fractional).
	 * Error codes:
	 *   0x8000: General sensor error
	 *   0x8001: Reserved
	 *   0x8002: Underflow on reading value
	 *   0x8003-0x81ff: Reserved
	 */
	if (module_dts_margin >= 0x8000 && module_dts_margin <= 0x81ff)
		return -EIO;

	module_dts_margin = ten_dot_six_to_millidegree(module_dts_margin);

	/* Note that the tjmax should be available before calling it */
	priv->temp.module[index].value = priv->temp.tjmax.value +
					 module_dts_margin;

	peci_sensor_mark_updated(&priv->temp.module[index]);

	return 0;
}

static int cputemp_read_string(struct device *dev,
			       enum hwmon_sensor_types type,
			       u32 attr, int channel, const char **str)
{
	struct peci_cputemp *priv = dev_get_drvdata(dev);

	if (attr != hwmon_temp_label)
		return -EOPNOTSUPP;

	*str = (channel < DEFAULT_CHANNEL_NUMS) ?
	       cputemp_label[channel] :
	       (const char *)priv->module_temp_label[channel -
						     DEFAULT_CHANNEL_NUMS];

	return 0;
}

static int cputemp_read(struct device *dev,
			enum hwmon_sensor_types type,
			u32 attr, int channel, long *val)
{
	struct peci_cputemp *priv = dev_get_drvdata(dev);
	int ret, module_index;

	if (channel >= CPUTEMP_CHANNEL_NUMS ||
	    !(priv->temp_config[channel] & BIT(attr)))
		return -EOPNOTSUPP;

	ret = get_temp_targets(priv);
	if (ret)
		return ret;

	switch (attr) {
	case hwmon_temp_input:
		switch (channel) {
		case channel_die:
			ret = get_die_temp(priv);
			if (ret)
				break;

			*val = priv->temp.die.value;
			break;
		case channel_dts:
			ret = get_dts(priv);
			if (ret)
				break;

			*val = priv->temp.dts.value;
			break;
		case channel_tcontrol:
			*val = priv->temp.tcontrol.value;
			break;
		case channel_tthrottle:
			*val = priv->temp.tthrottle.value;
			break;
		case channel_tjmax:
			*val = priv->temp.tjmax.value;
			break;
		default:
			module_index = channel - DEFAULT_CHANNEL_NUMS;
			ret = get_module_temp(priv, module_index);
			if (ret)
				break;

			*val = priv->temp.module[module_index].value;
			break;
		}
		break;
	case hwmon_temp_max:
		*val = priv->temp.tcontrol.value;
		break;
	case hwmon_temp_crit:
		*val = priv->temp.tjmax.value;
		break;
	case hwmon_temp_crit_hyst:
		*val = priv->temp.tjmax.value - priv->temp.tcontrol.value;
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	return ret;
}

static umode_t cputemp_is_visible(const void *data,
				  enum hwmon_sensor_types type,
				  u32 attr, int channel)
{
	const struct peci_cputemp *priv = data;

	if (channel < ARRAY_SIZE(priv->temp_config) &&
	    (priv->temp_config[channel] & BIT(attr)) &&
	    (channel < DEFAULT_CHANNEL_NUMS ||
	     (channel >= DEFAULT_CHANNEL_NUMS &&
	      (priv->core_mask & BIT(channel - DEFAULT_CHANNEL_NUMS)))))
		return 0444;

	return 0;
}

static const struct hwmon_ops cputemp_ops = {
	.is_visible = cputemp_is_visible,
	.read_string = cputemp_read_string,
	.read = cputemp_read,
};

static int check_resolved_cores(struct peci_cputemp *priv)
{
	struct peci_rd_pci_cfg_local_msg msg;
	int ret;

	/* Get the RESOLVED_CORES register value */
	switch (priv->gen_info->model) {
	case INTEL_FAM6_ICELAKE_X:
	case INTEL_FAM6_ICELAKE_XD:
		msg.addr = priv->mgr->client->addr;
		msg.device = 30;
		msg.function = 3;
		msg.bus = 14;
		msg.reg = 0xd4;
		msg.rx_len = 4;

		ret = peci_command(priv->mgr->client->adapter,
				   PECI_CMD_RD_PCI_CFG_LOCAL, &msg);
		if (msg.cc != PECI_DEV_CC_SUCCESS)
			ret = -EAGAIN;
		if (ret)
			return ret;

		priv->core_mask = le32_to_cpup((__le32 *)msg.pci_config);
		priv->core_mask <<= 32;

		msg.reg = 0xd0;

		ret = peci_command(priv->mgr->client->adapter,
				   PECI_CMD_RD_PCI_CFG_LOCAL, &msg);

		if (msg.cc != PECI_DEV_CC_SUCCESS)
			ret = -EAGAIN;
		if (ret) {
			priv->core_mask = 0;
			return ret;
		}

		priv->core_mask |= le32_to_cpup((__le32 *)msg.pci_config);
		break;
	default:
		msg.addr = priv->mgr->client->addr;
		msg.device = 30;
		msg.function = 3;
		msg.bus = 1;
		msg.reg = 0xb4;
		msg.rx_len = 4;

		ret = peci_command(priv->mgr->client->adapter,
				   PECI_CMD_RD_PCI_CFG_LOCAL, &msg);
		if (msg.cc != PECI_DEV_CC_SUCCESS)
			ret = -EAGAIN;
		if (ret)
			return ret;

		priv->core_mask = le32_to_cpup((__le32 *)msg.pci_config);
		break;
	}

	if (!priv->core_mask)
		return -EAGAIN;

	dev_dbg(priv->dev, "Scanned resolved cores: 0x%llx\n", priv->core_mask);

	return 0;
}

static int create_module_temp_label(struct peci_cputemp *priv, int idx)
{
	priv->module_temp_label[idx] = devm_kzalloc(priv->dev,
						    PECI_HWMON_LABEL_STR_LEN,
						    GFP_KERNEL);
	if (!priv->module_temp_label[idx])
		return -ENOMEM;

	sprintf(priv->module_temp_label[idx], "Core %d", idx);

	return 0;
}

static int create_module_temp_info(struct peci_cputemp *priv)
{
	int ret, i;

	ret = check_resolved_cores(priv);
	if (ret)
		return ret;

	priv->module_temp_label = devm_kzalloc(priv->dev,
					       MODTEMP_CHANNEL_NUMS *
					       sizeof(char *),
					       GFP_KERNEL);
	if (!priv->module_temp_label)
		return -ENOMEM;

	for (i = 0; i < MODTEMP_CHANNEL_NUMS; i++) {
		priv->temp_config[priv->config_idx++] = config_table[channel_core];

		if (i < priv->gen_info->core_mask_bits && priv->core_mask & BIT(i)) {
			ret = create_module_temp_label(priv, i);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static int peci_cputemp_probe(struct platform_device *pdev)
{
	struct peci_client_manager *mgr = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = &pdev->dev;
	struct peci_cputemp *priv;
	struct device *hwmon_dev;
	int ret;

	if ((mgr->client->adapter->cmd_mask &
	    (BIT(PECI_CMD_GET_TEMP) | BIT(PECI_CMD_RD_PKG_CFG))) !=
	    (BIT(PECI_CMD_GET_TEMP) | BIT(PECI_CMD_RD_PKG_CFG)))
		return -ENODEV;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	dev_set_drvdata(dev, priv);
	priv->mgr = mgr;
	priv->dev = dev;
	priv->gen_info = mgr->gen_info;

	snprintf(priv->name, PECI_NAME_SIZE, "peci_cputemp.cpu%d",
		 mgr->client->addr - PECI_BASE_ADDR);

	priv->temp_config[priv->config_idx++] = config_table[channel_die];
	priv->temp_config[priv->config_idx++] = config_table[channel_dts];
	priv->temp_config[priv->config_idx++] = config_table[channel_tcontrol];
	priv->temp_config[priv->config_idx++] = config_table[channel_tthrottle];
	priv->temp_config[priv->config_idx++] = config_table[channel_tjmax];

	ret = create_module_temp_info(priv);
	if (ret)
		dev_dbg(dev, "Skipped creating core temp info\n");

	priv->chip.ops = &cputemp_ops;
	priv->chip.info = priv->info;

	priv->info[0] = &priv->temp_info;

	priv->temp_info.type = hwmon_temp;
	priv->temp_info.config = priv->temp_config;

	hwmon_dev = devm_hwmon_device_register_with_info(priv->dev,
							 priv->name,
							 priv,
							 &priv->chip,
							 NULL);

	if (IS_ERR(hwmon_dev))
		return PTR_ERR(hwmon_dev);

	dev_dbg(dev, "%s: sensor '%s'\n", dev_name(hwmon_dev), priv->name);

	return 0;
}

static const struct platform_device_id peci_cputemp_ids[] = {
	{ .name = "peci-cputemp", .driver_data = 0 },
	{ }
};
MODULE_DEVICE_TABLE(platform, peci_cputemp_ids);

static struct platform_driver peci_cputemp_driver = {
	.probe		= peci_cputemp_probe,
	.id_table	= peci_cputemp_ids,
	.driver		= { .name = KBUILD_MODNAME, },
};
module_platform_driver(peci_cputemp_driver);

MODULE_AUTHOR("Jae Hyun Yoo <jae.hyun.yoo@linux.intel.com>");
MODULE_DESCRIPTION("PECI cputemp driver");
MODULE_LICENSE("GPL v2");
