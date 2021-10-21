// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018-2019 Intel Corporation

#include <linux/hwmon.h>
#include <linux/jiffies.h>
#include <linux/mfd/intel-peci-client.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include "peci-hwmon.h"

#define DIMM_MASK_CHECK_DELAY_JIFFIES	msecs_to_jiffies(5000)
#define DIMM_MASK_CHECK_RETRY_MAX	-1 /* 60 x 5 secs = 5 minutes */
					   /* -1 = no timeout */
#define DIMM_TEMP_MAX_DEFAULT		90000
#define DIMM_TEMP_CRIT_DEFAULT		100000
#define BIOS_RST_CPL4			BIT(4)

struct peci_dimmtemp {
	struct peci_client_manager	*mgr;
	struct device			*dev;
	char				name[PECI_NAME_SIZE];
	const struct cpu_gen_info	*gen_info;
	struct workqueue_struct		*work_queue;
	struct delayed_work		work_handler;
	struct peci_sensor_data		temp[DIMM_NUMS_MAX];
	long				temp_max[DIMM_NUMS_MAX];
	long				temp_crit[DIMM_NUMS_MAX];
	u32				dimm_mask;
	int				retry_count;
	u32				temp_config[DIMM_NUMS_MAX + 1];
	struct hwmon_channel_info	temp_info;
	const struct hwmon_channel_info	*info[2];
	struct hwmon_chip_info		chip;
	char				**dimmtemp_label;
};

static const u8 support_model[] = {
	INTEL_FAM6_HASWELL_X,
	INTEL_FAM6_BROADWELL_X,
	INTEL_FAM6_SKYLAKE_X,
	INTEL_FAM6_SKYLAKE_XD,
	INTEL_FAM6_ICELAKE_X,
	INTEL_FAM6_ICELAKE_XD,
};

static inline int read_ddr_dimm_temp_config(struct peci_dimmtemp *priv,
					    int chan_rank,
					    u8 *cfg_data)
{
	return peci_client_read_package_config(priv->mgr,
					       PECI_MBX_INDEX_DDR_DIMM_TEMP,
					       chan_rank, cfg_data);
}

static int get_dimm_temp(struct peci_dimmtemp *priv, int dimm_no)
{
	int dimm_order = dimm_no % priv->gen_info->dimm_idx_max;
	int chan_rank = dimm_no / priv->gen_info->dimm_idx_max;
	struct peci_rd_pci_cfg_local_msg rp_msg;
	struct peci_rd_end_pt_cfg_msg re_msg;
	u32 bios_reset_cpl_cfg;
	u8  cfg_data[4];
	u8  cpu_seg, cpu_bus;
	int ret;

	if (!peci_sensor_need_update(&priv->temp[dimm_no]))
		return 0;

	ret = read_ddr_dimm_temp_config(priv, chan_rank, cfg_data);
	if (ret || cfg_data[dimm_order] == 0 || cfg_data[dimm_order] == 0xff)
		return -ENODATA;

	priv->temp[dimm_no].value = cfg_data[dimm_order] * 1000;

	/*
	 * CPU can return invalid temperatures prior to BIOS-PCU handshake
	 * RST_CPL4 completion so filter the invalid readings out.
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
		if (!(bios_reset_cpl_cfg & BIOS_RST_CPL4)) {
			dev_dbg(priv->dev, "DRAM parameters aren't calibrated, BIOS_RESET_CPL_CFG: 0x%x\n",
				bios_reset_cpl_cfg);
			return -EAGAIN;
		}

		break;
	default:
		/* TODO: Check reset completion for other CPUs if needed */
		break;
	}

	switch (priv->gen_info->model) {
	case INTEL_FAM6_ICELAKE_X:
	case INTEL_FAM6_ICELAKE_XD:
		re_msg.addr = priv->mgr->client->addr;
		re_msg.rx_len = 4;
		re_msg.msg_type = PECI_ENDPTCFG_TYPE_LOCAL_PCI;
		re_msg.params.pci_cfg.seg = 0;
		re_msg.params.pci_cfg.bus = 13;
		re_msg.params.pci_cfg.device = 0;
		re_msg.params.pci_cfg.function = 2;
		re_msg.params.pci_cfg.reg = 0xd4;

		ret = peci_command(priv->mgr->client->adapter,
				   PECI_CMD_RD_END_PT_CFG, &re_msg);
		if (ret || re_msg.cc != PECI_DEV_CC_SUCCESS ||
		    !(re_msg.data[3] & BIT(7))) {
			/* Use default or previous value */
			ret = 0;
			break;
		}

		re_msg.msg_type = PECI_ENDPTCFG_TYPE_LOCAL_PCI;
		re_msg.params.pci_cfg.reg = 0xd0;

		ret = peci_command(priv->mgr->client->adapter,
				   PECI_CMD_RD_END_PT_CFG, &re_msg);
		if (ret || re_msg.cc != PECI_DEV_CC_SUCCESS) {
			/* Use default or previous value */
			ret = 0;
			break;
		}

		cpu_seg = re_msg.data[2];
		cpu_bus = re_msg.data[0];

		re_msg.addr = priv->mgr->client->addr;
		re_msg.msg_type = PECI_ENDPTCFG_TYPE_MMIO;
		re_msg.params.mmio.seg = cpu_seg;
		re_msg.params.mmio.bus = cpu_bus;
		/*
		 * Device 26, Offset 224e0: IMC 0 channel 0 -> rank 0
		 * Device 26, Offset 264e0: IMC 0 channel 1 -> rank 1
		 * Device 27, Offset 224e0: IMC 1 channel 0 -> rank 2
		 * Device 27, Offset 264e0: IMC 1 channel 1 -> rank 3
		 * Device 28, Offset 224e0: IMC 2 channel 0 -> rank 4
		 * Device 28, Offset 264e0: IMC 2 channel 1 -> rank 5
		 * Device 29, Offset 224e0: IMC 3 channel 0 -> rank 6
		 * Device 29, Offset 264e0: IMC 3 channel 1 -> rank 7
		 */
		re_msg.params.mmio.device = 0x1a + chan_rank / 2;
		re_msg.params.mmio.function = 0;
		re_msg.params.mmio.bar = 0;
		re_msg.params.mmio.addr_type = PECI_ENDPTCFG_ADDR_TYPE_MMIO_Q;
		re_msg.params.mmio.offset = 0x224e0 + dimm_order * 4;
		if (chan_rank % 2)
			re_msg.params.mmio.offset += 0x4000;

		ret = peci_command(priv->mgr->client->adapter,
				   PECI_CMD_RD_END_PT_CFG, &re_msg);
		if (ret || re_msg.cc != PECI_DEV_CC_SUCCESS ||
		    re_msg.data[1] == 0 || re_msg.data[2] == 0) {
			/* Use default or previous value */
			ret = 0;
			break;
		}

		priv->temp_max[dimm_no] = re_msg.data[1] * 1000;
		priv->temp_crit[dimm_no] = re_msg.data[2] * 1000;
		break;
	case INTEL_FAM6_SKYLAKE_X:
		rp_msg.addr = priv->mgr->client->addr;
		rp_msg.bus = 2;
		/*
		 * Device 10, Function 2: IMC 0 channel 0 -> rank 0
		 * Device 10, Function 6: IMC 0 channel 1 -> rank 1
		 * Device 11, Function 2: IMC 0 channel 2 -> rank 2
		 * Device 12, Function 2: IMC 1 channel 0 -> rank 3
		 * Device 12, Function 6: IMC 1 channel 1 -> rank 4
		 * Device 13, Function 2: IMC 1 channel 2 -> rank 5
		 */
		rp_msg.device = 10 + chan_rank / 3 * 2 +
			     (chan_rank % 3 == 2 ? 1 : 0);
		rp_msg.function = chan_rank % 3 == 1 ? 6 : 2;
		rp_msg.reg = 0x120 + dimm_order * 4;
		rp_msg.rx_len = 4;

		ret = peci_command(priv->mgr->client->adapter,
				   PECI_CMD_RD_PCI_CFG_LOCAL, &rp_msg);
		if (ret || rp_msg.cc != PECI_DEV_CC_SUCCESS ||
		    rp_msg.pci_config[1] == 0 || rp_msg.pci_config[2] == 0) {
			/* Use default or previous value */
			ret = 0;
			break;
		}

		priv->temp_max[dimm_no] = rp_msg.pci_config[1] * 1000;
		priv->temp_crit[dimm_no] = rp_msg.pci_config[2] * 1000;
		break;
	case INTEL_FAM6_SKYLAKE_XD:
		rp_msg.addr = priv->mgr->client->addr;
		rp_msg.bus = 2;
		/*
		 * Device 10, Function 2: IMC 0 channel 0 -> rank 0
		 * Device 10, Function 6: IMC 0 channel 1 -> rank 1
		 * Device 12, Function 2: IMC 1 channel 0 -> rank 2
		 * Device 12, Function 6: IMC 1 channel 1 -> rank 3
		 */
		rp_msg.device = 10 + chan_rank / 2 * 2;
		rp_msg.function = (chan_rank % 2) ? 6 : 2;
		rp_msg.reg = 0x120 + dimm_order * 4;
		rp_msg.rx_len = 4;

		ret = peci_command(priv->mgr->client->adapter,
				   PECI_CMD_RD_PCI_CFG_LOCAL, &rp_msg);
		if (ret || rp_msg.cc != PECI_DEV_CC_SUCCESS ||
		    rp_msg.pci_config[1] == 0 || rp_msg.pci_config[2] == 0) {
			/* Use default or previous value */
			ret = 0;
			break;
		}

		priv->temp_max[dimm_no] = rp_msg.pci_config[1] * 1000;
		priv->temp_crit[dimm_no] = rp_msg.pci_config[2] * 1000;
		break;
	case INTEL_FAM6_HASWELL_X:
	case INTEL_FAM6_BROADWELL_X:
		rp_msg.addr = priv->mgr->client->addr;
		rp_msg.bus = 1;
		/*
		 * Device 20, Function 0: IMC 0 channel 0 -> rank 0
		 * Device 20, Function 1: IMC 0 channel 1 -> rank 1
		 * Device 21, Function 0: IMC 0 channel 2 -> rank 2
		 * Device 21, Function 1: IMC 0 channel 3 -> rank 3
		 * Device 23, Function 0: IMC 1 channel 0 -> rank 4
		 * Device 23, Function 1: IMC 1 channel 1 -> rank 5
		 * Device 24, Function 0: IMC 1 channel 2 -> rank 6
		 * Device 24, Function 1: IMC 1 channel 3 -> rank 7
		 */
		rp_msg.device = 20 + chan_rank / 2 + chan_rank / 4;
		rp_msg.function = chan_rank % 2;
		rp_msg.reg = 0x120 + dimm_order * 4;
		rp_msg.rx_len = 4;

		ret = peci_command(priv->mgr->client->adapter,
				   PECI_CMD_RD_PCI_CFG_LOCAL, &rp_msg);
		if (ret || rp_msg.cc != PECI_DEV_CC_SUCCESS ||
		    rp_msg.pci_config[1] == 0 || rp_msg.pci_config[2] == 0) {
			/* Use default or previous value */
			ret = 0;
			break;
		}

		priv->temp_max[dimm_no] = rp_msg.pci_config[1] * 1000;
		priv->temp_crit[dimm_no] = rp_msg.pci_config[2] * 1000;
		break;
	default:
		return -EOPNOTSUPP;
	}

	peci_sensor_mark_updated(&priv->temp[dimm_no]);

	return 0;
}

static int dimmtemp_read_string(struct device *dev,
				enum hwmon_sensor_types type,
				u32 attr, int channel, const char **str)
{
	struct peci_dimmtemp *priv = dev_get_drvdata(dev);

	if (attr != hwmon_temp_label)
		return -EOPNOTSUPP;

	*str = (const char *)priv->dimmtemp_label[channel];

	return 0;
}

static int dimmtemp_read(struct device *dev, enum hwmon_sensor_types type,
			 u32 attr, int channel, long *val)
{
	struct peci_dimmtemp *priv = dev_get_drvdata(dev);
	int ret;

	ret = get_dimm_temp(priv, channel);
	if (ret)
		return ret;

	switch (attr) {
	case hwmon_temp_input:
		*val = priv->temp[channel].value;
		break;
	case hwmon_temp_max:
		*val = priv->temp_max[channel];
		break;
	case hwmon_temp_crit:
		*val = priv->temp_crit[channel];
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	return ret;
}

static umode_t dimmtemp_is_visible(const void *data,
				   enum hwmon_sensor_types type,
				   u32 attr, int channel)
{
	const struct peci_dimmtemp *priv = data;

	if (priv->temp_config[channel] & BIT(attr) &&
	    priv->dimm_mask & BIT(channel))
		return 0444;

	return 0;
}

static const struct hwmon_ops dimmtemp_ops = {
	.is_visible = dimmtemp_is_visible,
	.read_string = dimmtemp_read_string,
	.read = dimmtemp_read,
};

static int check_populated_dimms(struct peci_dimmtemp *priv)
{
	u32 chan_rank_max = priv->gen_info->chan_rank_max;
	u32 dimm_idx_max = priv->gen_info->dimm_idx_max;
	int chan_rank;
	u8  cfg_data[4];

	for (chan_rank = 0; chan_rank < chan_rank_max; chan_rank++) {
		int ret, idx;

		ret = read_ddr_dimm_temp_config(priv, chan_rank, cfg_data);
		if (ret) {
			if (ret == -EAGAIN)
				continue;

			priv->dimm_mask = 0;
			return ret;
		}

		for (idx = 0; idx < dimm_idx_max; idx++) {
			if (cfg_data[idx]) {
				uint chan = chan_rank * dimm_idx_max + idx;
				priv->dimm_mask |= BIT(chan);
				priv->temp_max[chan] = DIMM_TEMP_MAX_DEFAULT;
				priv->temp_crit[chan] = DIMM_TEMP_CRIT_DEFAULT;
			}
		}
	}

	if (!priv->dimm_mask)
		return -EAGAIN;

	dev_dbg(priv->dev, "Scanned populated DIMMs: 0x%x\n", priv->dimm_mask);

	return 0;
}

static int create_dimm_temp_label(struct peci_dimmtemp *priv, int chan)
{
	int rank, idx;

	priv->dimmtemp_label[chan] = devm_kzalloc(priv->dev,
						  PECI_HWMON_LABEL_STR_LEN,
						  GFP_KERNEL);
	if (!priv->dimmtemp_label[chan])
		return -ENOMEM;

	rank = chan / priv->gen_info->dimm_idx_max;
	idx = chan % priv->gen_info->dimm_idx_max;

	snprintf(priv->dimmtemp_label[chan], PECI_HWMON_LABEL_STR_LEN,
		 "DIMM %c%d", 'A' + rank, idx + 1);

	return 0;
}

static int create_dimm_temp_info(struct peci_dimmtemp *priv)
{
	int ret, i, config_idx, channels;
	struct device *dev;

	ret = check_populated_dimms(priv);
	if (ret) {
		if (ret == -EAGAIN) {
			if (DIMM_MASK_CHECK_RETRY_MAX == -1 ||
			    priv->retry_count < DIMM_MASK_CHECK_RETRY_MAX) {
				queue_delayed_work(priv->work_queue,
						   &priv->work_handler,
						 DIMM_MASK_CHECK_DELAY_JIFFIES);
				priv->retry_count++;
				dev_dbg(priv->dev,
					"Deferred DIMM temp info creation\n");
			} else {
				dev_err(priv->dev,
					"Timeout DIMM temp info creation\n");
				ret = -ETIMEDOUT;
			}
		}

		return ret;
	}

	channels = priv->gen_info->chan_rank_max *
		   priv->gen_info->dimm_idx_max;

	priv->dimmtemp_label = devm_kzalloc(priv->dev,
					    channels * sizeof(char *),
					    GFP_KERNEL);
	if (!priv->dimmtemp_label)
		return -ENOMEM;

	for (i = 0, config_idx = 0; i < channels; i++)
		if (priv->dimm_mask & BIT(i)) {
			while (i >= config_idx)
				priv->temp_config[config_idx++] =
					HWMON_T_LABEL | HWMON_T_INPUT |
					HWMON_T_MAX | HWMON_T_CRIT;

			ret = create_dimm_temp_label(priv, i);
			if (ret)
				return ret;
		}

	priv->chip.ops = &dimmtemp_ops;
	priv->chip.info = priv->info;

	priv->info[0] = &priv->temp_info;

	priv->temp_info.type = hwmon_temp;
	priv->temp_info.config = priv->temp_config;

	dev = devm_hwmon_device_register_with_info(priv->dev,
						   priv->name,
						   priv,
						   &priv->chip,
						   NULL);
	if (IS_ERR(dev)) {
		dev_err(priv->dev, "Failed to register hwmon device\n");
		return PTR_ERR(dev);
	}

	dev_dbg(priv->dev, "%s: sensor '%s'\n", dev_name(dev), priv->name);

	return 0;
}

static void create_dimm_temp_info_delayed(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct peci_dimmtemp *priv = container_of(dwork, struct peci_dimmtemp,
						  work_handler);
	int ret;

	ret = create_dimm_temp_info(priv);
	if (ret && ret != -EAGAIN)
		dev_dbg(priv->dev, "Failed to create DIMM temp info\n");
}

static int peci_dimmtemp_probe(struct platform_device *pdev)
{
	struct peci_client_manager *mgr = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = &pdev->dev;
	struct peci_dimmtemp *priv;
	int ret, i;

	if ((mgr->client->adapter->cmd_mask &
	    (BIT(PECI_CMD_GET_TEMP) | BIT(PECI_CMD_RD_PKG_CFG))) !=
	    (BIT(PECI_CMD_GET_TEMP) | BIT(PECI_CMD_RD_PKG_CFG)))
		return -ENODEV;

	for (i = 0; i < ARRAY_SIZE(support_model); i++) {
		if (mgr->gen_info->model == support_model[i])
			break;
	}
	if (i == ARRAY_SIZE(support_model))
		return -ENODEV;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	dev_set_drvdata(dev, priv);
	priv->mgr = mgr;
	priv->dev = dev;
	priv->gen_info = mgr->gen_info;

	snprintf(priv->name, PECI_NAME_SIZE, "peci_dimmtemp.cpu%d",
		 priv->mgr->client->addr - PECI_BASE_ADDR);

	priv->work_queue = alloc_ordered_workqueue(priv->name, 0);
	if (!priv->work_queue)
		return -ENOMEM;

	INIT_DELAYED_WORK(&priv->work_handler, create_dimm_temp_info_delayed);

	ret = create_dimm_temp_info(priv);
	if (ret && ret != -EAGAIN) {
		dev_dbg(dev, "Failed to create DIMM temp info\n");
		goto err_free_wq;
	}

	return 0;

err_free_wq:
	destroy_workqueue(priv->work_queue);
	return ret;
}

static int peci_dimmtemp_remove(struct platform_device *pdev)
{
	struct peci_dimmtemp *priv = dev_get_drvdata(&pdev->dev);

	cancel_delayed_work_sync(&priv->work_handler);
	destroy_workqueue(priv->work_queue);

	return 0;
}

static const struct platform_device_id peci_dimmtemp_ids[] = {
	{ .name = "peci-dimmtemp", .driver_data = 0 },
	{ }
};
MODULE_DEVICE_TABLE(platform, peci_dimmtemp_ids);

static struct platform_driver peci_dimmtemp_driver = {
	.probe		= peci_dimmtemp_probe,
	.remove		= peci_dimmtemp_remove,
	.id_table	= peci_dimmtemp_ids,
	.driver		= { .name = KBUILD_MODNAME, },
};
module_platform_driver(peci_dimmtemp_driver);

MODULE_AUTHOR("Jae Hyun Yoo <jae.hyun.yoo@linux.intel.com>");
MODULE_DESCRIPTION("PECI dimmtemp driver");
MODULE_LICENSE("GPL v2");
