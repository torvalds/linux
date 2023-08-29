// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2018-2021 Intel Corporation

#include <linux/auxiliary_bus.h>
#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/devm-helpers.h>
#include <linux/hwmon.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/peci.h>
#include <linux/peci-cpu.h>
#include <linux/units.h>
#include <linux/workqueue.h>

#include "common.h"

#define DIMM_MASK_CHECK_DELAY_JIFFIES	msecs_to_jiffies(5000)

/* Max number of channel ranks and DIMM index per channel */
#define CHAN_RANK_MAX_ON_HSX	8
#define DIMM_IDX_MAX_ON_HSX	3
#define CHAN_RANK_MAX_ON_BDX	4
#define DIMM_IDX_MAX_ON_BDX	3
#define CHAN_RANK_MAX_ON_BDXD	2
#define DIMM_IDX_MAX_ON_BDXD	2
#define CHAN_RANK_MAX_ON_SKX	6
#define DIMM_IDX_MAX_ON_SKX	2
#define CHAN_RANK_MAX_ON_ICX	8
#define DIMM_IDX_MAX_ON_ICX	2
#define CHAN_RANK_MAX_ON_ICXD	4
#define DIMM_IDX_MAX_ON_ICXD	2

#define CHAN_RANK_MAX		CHAN_RANK_MAX_ON_HSX
#define DIMM_IDX_MAX		DIMM_IDX_MAX_ON_HSX
#define DIMM_NUMS_MAX		(CHAN_RANK_MAX * DIMM_IDX_MAX)

#define CPU_SEG_MASK		GENMASK(23, 16)
#define GET_CPU_SEG(x)		(((x) & CPU_SEG_MASK) >> 16)
#define CPU_BUS_MASK		GENMASK(7, 0)
#define GET_CPU_BUS(x)		((x) & CPU_BUS_MASK)

#define DIMM_TEMP_MAX		GENMASK(15, 8)
#define DIMM_TEMP_CRIT		GENMASK(23, 16)
#define GET_TEMP_MAX(x)		(((x) & DIMM_TEMP_MAX) >> 8)
#define GET_TEMP_CRIT(x)	(((x) & DIMM_TEMP_CRIT) >> 16)

#define NO_DIMM_RETRY_COUNT_MAX	5

struct peci_dimmtemp;

struct dimm_info {
	int chan_rank_max;
	int dimm_idx_max;
	u8 min_peci_revision;
	int (*read_thresholds)(struct peci_dimmtemp *priv, int dimm_order,
			       int chan_rank, u32 *data);
};

struct peci_dimm_thresholds {
	long temp_max;
	long temp_crit;
	struct peci_sensor_state state;
};

enum peci_dimm_threshold_type {
	temp_max_type,
	temp_crit_type,
};

struct peci_dimmtemp {
	struct peci_device *peci_dev;
	struct device *dev;
	const char *name;
	const struct dimm_info *gen_info;
	struct delayed_work detect_work;
	struct {
		struct peci_sensor_data temp;
		struct peci_dimm_thresholds thresholds;
	} dimm[DIMM_NUMS_MAX];
	char **dimmtemp_label;
	DECLARE_BITMAP(dimm_mask, DIMM_NUMS_MAX);
	u8 no_dimm_retry_count;
};

static u8 __dimm_temp(u32 reg, int dimm_order)
{
	return (reg >> (dimm_order * 8)) & 0xff;
}

static int get_dimm_temp(struct peci_dimmtemp *priv, int dimm_no, long *val)
{
	int dimm_order = dimm_no % priv->gen_info->dimm_idx_max;
	int chan_rank = dimm_no / priv->gen_info->dimm_idx_max;
	int ret = 0;
	u32 data;

	mutex_lock(&priv->dimm[dimm_no].temp.state.lock);
	if (!peci_sensor_need_update(&priv->dimm[dimm_no].temp.state))
		goto skip_update;

	ret = peci_pcs_read(priv->peci_dev, PECI_PCS_DDR_DIMM_TEMP, chan_rank, &data);
	if (ret)
		goto unlock;

	priv->dimm[dimm_no].temp.value = __dimm_temp(data, dimm_order) * MILLIDEGREE_PER_DEGREE;

	peci_sensor_mark_updated(&priv->dimm[dimm_no].temp.state);

skip_update:
	*val = priv->dimm[dimm_no].temp.value;
unlock:
	mutex_unlock(&priv->dimm[dimm_no].temp.state.lock);
	return ret;
}

static int update_thresholds(struct peci_dimmtemp *priv, int dimm_no)
{
	int dimm_order = dimm_no % priv->gen_info->dimm_idx_max;
	int chan_rank = dimm_no / priv->gen_info->dimm_idx_max;
	u32 data;
	int ret;

	if (!peci_sensor_need_update(&priv->dimm[dimm_no].thresholds.state))
		return 0;

	ret = priv->gen_info->read_thresholds(priv, dimm_order, chan_rank, &data);
	if (ret == -ENODATA) /* Use default or previous value */
		return 0;
	if (ret)
		return ret;

	priv->dimm[dimm_no].thresholds.temp_max = GET_TEMP_MAX(data) * MILLIDEGREE_PER_DEGREE;
	priv->dimm[dimm_no].thresholds.temp_crit = GET_TEMP_CRIT(data) * MILLIDEGREE_PER_DEGREE;

	peci_sensor_mark_updated(&priv->dimm[dimm_no].thresholds.state);

	return 0;
}

static int get_dimm_thresholds(struct peci_dimmtemp *priv, enum peci_dimm_threshold_type type,
			       int dimm_no, long *val)
{
	int ret;

	mutex_lock(&priv->dimm[dimm_no].thresholds.state.lock);
	ret = update_thresholds(priv, dimm_no);
	if (ret)
		goto unlock;

	switch (type) {
	case temp_max_type:
		*val = priv->dimm[dimm_no].thresholds.temp_max;
		break;
	case temp_crit_type:
		*val = priv->dimm[dimm_no].thresholds.temp_crit;
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}
unlock:
	mutex_unlock(&priv->dimm[dimm_no].thresholds.state.lock);

	return ret;
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

	switch (attr) {
	case hwmon_temp_input:
		return get_dimm_temp(priv, channel, val);
	case hwmon_temp_max:
		return get_dimm_thresholds(priv, temp_max_type, channel, val);
	case hwmon_temp_crit:
		return get_dimm_thresholds(priv, temp_crit_type, channel, val);
	default:
		break;
	}

	return -EOPNOTSUPP;
}

static umode_t dimmtemp_is_visible(const void *data, enum hwmon_sensor_types type,
				   u32 attr, int channel)
{
	const struct peci_dimmtemp *priv = data;

	if (test_bit(channel, priv->dimm_mask))
		return 0444;

	return 0;
}

static const struct hwmon_ops peci_dimmtemp_ops = {
	.is_visible = dimmtemp_is_visible,
	.read_string = dimmtemp_read_string,
	.read = dimmtemp_read,
};

static int check_populated_dimms(struct peci_dimmtemp *priv)
{
	int chan_rank_max = priv->gen_info->chan_rank_max;
	int dimm_idx_max = priv->gen_info->dimm_idx_max;
	DECLARE_BITMAP(dimm_mask, DIMM_NUMS_MAX);
	DECLARE_BITMAP(chan_rank_empty, CHAN_RANK_MAX);

	int chan_rank, dimm_idx, ret, i;
	u32 pcs;

	if (chan_rank_max * dimm_idx_max > DIMM_NUMS_MAX) {
		WARN_ONCE(1, "Unsupported number of DIMMs - chan_rank_max: %d, dimm_idx_max: %d",
			  chan_rank_max, dimm_idx_max);
		return -EINVAL;
	}

	bitmap_zero(dimm_mask, DIMM_NUMS_MAX);
	bitmap_zero(chan_rank_empty, CHAN_RANK_MAX);

	for (chan_rank = 0; chan_rank < chan_rank_max; chan_rank++) {
		ret = peci_pcs_read(priv->peci_dev, PECI_PCS_DDR_DIMM_TEMP, chan_rank, &pcs);
		if (ret) {
			/*
			 * Overall, we expect either success or -EINVAL in
			 * order to determine whether DIMM is populated or not.
			 * For anything else we fall back to deferring the
			 * detection to be performed at a later point in time.
			 */
			if (ret == -EINVAL) {
				bitmap_set(chan_rank_empty, chan_rank, 1);
				continue;
			}

			return -EAGAIN;
		}

		for (dimm_idx = 0; dimm_idx < dimm_idx_max; dimm_idx++)
			if (__dimm_temp(pcs, dimm_idx))
				bitmap_set(dimm_mask, chan_rank * dimm_idx_max + dimm_idx, 1);
	}

	/*
	 * If we got all -EINVALs, it means that the CPU doesn't have any
	 * DIMMs. Unfortunately, it may also happen at the very start of
	 * host platform boot. Retrying a couple of times lets us make sure
	 * that the state is persistent.
	 */
	if (bitmap_full(chan_rank_empty, chan_rank_max)) {
		if (priv->no_dimm_retry_count < NO_DIMM_RETRY_COUNT_MAX) {
			priv->no_dimm_retry_count++;

			return -EAGAIN;
		}

		return -ENODEV;
	}

	/*
	 * It's possible that memory training is not done yet. In this case we
	 * defer the detection to be performed at a later point in time.
	 */
	if (bitmap_empty(dimm_mask, DIMM_NUMS_MAX)) {
		priv->no_dimm_retry_count = 0;
		return -EAGAIN;
	}

	for_each_set_bit(i, dimm_mask, DIMM_NUMS_MAX) {
		dev_dbg(priv->dev, "Found DIMM%#x\n", i);
	}

	bitmap_copy(priv->dimm_mask, dimm_mask, DIMM_NUMS_MAX);

	return 0;
}

static int create_dimm_temp_label(struct peci_dimmtemp *priv, int chan)
{
	int rank = chan / priv->gen_info->dimm_idx_max;
	int idx = chan % priv->gen_info->dimm_idx_max;

	priv->dimmtemp_label[chan] = devm_kasprintf(priv->dev, GFP_KERNEL,
						    "DIMM %c%d", 'A' + rank,
						    idx + 1);
	if (!priv->dimmtemp_label[chan])
		return -ENOMEM;

	return 0;
}

static const struct hwmon_channel_info * const peci_dimmtemp_temp_info[] = {
	HWMON_CHANNEL_INFO(temp,
			   [0 ... DIMM_NUMS_MAX - 1] = HWMON_T_LABEL |
				HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_CRIT),
	NULL
};

static const struct hwmon_chip_info peci_dimmtemp_chip_info = {
	.ops = &peci_dimmtemp_ops,
	.info = peci_dimmtemp_temp_info,
};

static int create_dimm_temp_info(struct peci_dimmtemp *priv)
{
	int ret, i, channels;
	struct device *dev;

	/*
	 * We expect to either find populated DIMMs and carry on with creating
	 * sensors, or find out that there are no DIMMs populated.
	 * All other states mean that the platform never reached the state that
	 * allows to check DIMM state - causing us to retry later on.
	 */
	ret = check_populated_dimms(priv);
	if (ret == -ENODEV) {
		dev_dbg(priv->dev, "No DIMMs found\n");
		return 0;
	} else if (ret) {
		schedule_delayed_work(&priv->detect_work, DIMM_MASK_CHECK_DELAY_JIFFIES);
		dev_dbg(priv->dev, "Deferred populating DIMM temp info\n");
		return ret;
	}

	channels = priv->gen_info->chan_rank_max * priv->gen_info->dimm_idx_max;

	priv->dimmtemp_label = devm_kzalloc(priv->dev, channels * sizeof(char *), GFP_KERNEL);
	if (!priv->dimmtemp_label)
		return -ENOMEM;

	for_each_set_bit(i, priv->dimm_mask, DIMM_NUMS_MAX) {
		ret = create_dimm_temp_label(priv, i);
		if (ret)
			return ret;
		mutex_init(&priv->dimm[i].thresholds.state.lock);
		mutex_init(&priv->dimm[i].temp.state.lock);
	}

	dev = devm_hwmon_device_register_with_info(priv->dev, priv->name, priv,
						   &peci_dimmtemp_chip_info, NULL);
	if (IS_ERR(dev)) {
		dev_err(priv->dev, "Failed to register hwmon device\n");
		return PTR_ERR(dev);
	}

	dev_dbg(priv->dev, "%s: sensor '%s'\n", dev_name(dev), priv->name);

	return 0;
}

static void create_dimm_temp_info_delayed(struct work_struct *work)
{
	struct peci_dimmtemp *priv = container_of(to_delayed_work(work),
						  struct peci_dimmtemp,
						  detect_work);
	int ret;

	ret = create_dimm_temp_info(priv);
	if (ret && ret != -EAGAIN)
		dev_err(priv->dev, "Failed to populate DIMM temp info\n");
}

static int peci_dimmtemp_probe(struct auxiliary_device *adev, const struct auxiliary_device_id *id)
{
	struct device *dev = &adev->dev;
	struct peci_device *peci_dev = to_peci_device(dev->parent);
	struct peci_dimmtemp *priv;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->name = devm_kasprintf(dev, GFP_KERNEL, "peci_dimmtemp.cpu%d",
				    peci_dev->info.socket_id);
	if (!priv->name)
		return -ENOMEM;

	priv->dev = dev;
	priv->peci_dev = peci_dev;
	priv->gen_info = (const struct dimm_info *)id->driver_data;

	/*
	 * This is just a sanity check. Since we're using commands that are
	 * guaranteed to be supported on a given platform, we should never see
	 * revision lower than expected.
	 */
	if (peci_dev->info.peci_revision < priv->gen_info->min_peci_revision)
		dev_warn(priv->dev,
			 "Unexpected PECI revision %#x, some features may be unavailable\n",
			 peci_dev->info.peci_revision);

	ret = devm_delayed_work_autocancel(priv->dev, &priv->detect_work,
					   create_dimm_temp_info_delayed);
	if (ret)
		return ret;

	ret = create_dimm_temp_info(priv);
	if (ret && ret != -EAGAIN) {
		dev_err(dev, "Failed to populate DIMM temp info\n");
		return ret;
	}

	return 0;
}

static int
read_thresholds_hsx(struct peci_dimmtemp *priv, int dimm_order, int chan_rank, u32 *data)
{
	u8 dev, func;
	u16 reg;
	int ret;

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
	dev = 20 + chan_rank / 2 + chan_rank / 4;
	func = chan_rank % 2;
	reg = 0x120 + dimm_order * 4;

	ret = peci_pci_local_read(priv->peci_dev, 1, dev, func, reg, data);
	if (ret)
		return ret;

	return 0;
}

static int
read_thresholds_bdxd(struct peci_dimmtemp *priv, int dimm_order, int chan_rank, u32 *data)
{
	u8 dev, func;
	u16 reg;
	int ret;

	/*
	 * Device 10, Function 2: IMC 0 channel 0 -> rank 0
	 * Device 10, Function 6: IMC 0 channel 1 -> rank 1
	 * Device 12, Function 2: IMC 1 channel 0 -> rank 2
	 * Device 12, Function 6: IMC 1 channel 1 -> rank 3
	 */
	dev = 10 + chan_rank / 2 * 2;
	func = (chan_rank % 2) ? 6 : 2;
	reg = 0x120 + dimm_order * 4;

	ret = peci_pci_local_read(priv->peci_dev, 2, dev, func, reg, data);
	if (ret)
		return ret;

	return 0;
}

static int
read_thresholds_skx(struct peci_dimmtemp *priv, int dimm_order, int chan_rank, u32 *data)
{
	u8 dev, func;
	u16 reg;
	int ret;

	/*
	 * Device 10, Function 2: IMC 0 channel 0 -> rank 0
	 * Device 10, Function 6: IMC 0 channel 1 -> rank 1
	 * Device 11, Function 2: IMC 0 channel 2 -> rank 2
	 * Device 12, Function 2: IMC 1 channel 0 -> rank 3
	 * Device 12, Function 6: IMC 1 channel 1 -> rank 4
	 * Device 13, Function 2: IMC 1 channel 2 -> rank 5
	 */
	dev = 10 + chan_rank / 3 * 2 + (chan_rank % 3 == 2 ? 1 : 0);
	func = chan_rank % 3 == 1 ? 6 : 2;
	reg = 0x120 + dimm_order * 4;

	ret = peci_pci_local_read(priv->peci_dev, 2, dev, func, reg, data);
	if (ret)
		return ret;

	return 0;
}

static int
read_thresholds_icx(struct peci_dimmtemp *priv, int dimm_order, int chan_rank, u32 *data)
{
	u32 reg_val;
	u64 offset;
	int ret;
	u8 dev;

	ret = peci_ep_pci_local_read(priv->peci_dev, 0, 13, 0, 2, 0xd4, &reg_val);
	if (ret || !(reg_val & BIT(31)))
		return -ENODATA; /* Use default or previous value */

	ret = peci_ep_pci_local_read(priv->peci_dev, 0, 13, 0, 2, 0xd0, &reg_val);
	if (ret)
		return -ENODATA; /* Use default or previous value */

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
	dev = 26 + chan_rank / 2;
	offset = 0x224e0 + dimm_order * 4 + (chan_rank % 2) * 0x4000;

	ret = peci_mmio_read(priv->peci_dev, 0, GET_CPU_SEG(reg_val), GET_CPU_BUS(reg_val),
			     dev, 0, offset, data);
	if (ret)
		return ret;

	return 0;
}

static const struct dimm_info dimm_hsx = {
	.chan_rank_max	= CHAN_RANK_MAX_ON_HSX,
	.dimm_idx_max	= DIMM_IDX_MAX_ON_HSX,
	.min_peci_revision = 0x33,
	.read_thresholds = &read_thresholds_hsx,
};

static const struct dimm_info dimm_bdx = {
	.chan_rank_max	= CHAN_RANK_MAX_ON_BDX,
	.dimm_idx_max	= DIMM_IDX_MAX_ON_BDX,
	.min_peci_revision = 0x33,
	.read_thresholds = &read_thresholds_hsx,
};

static const struct dimm_info dimm_bdxd = {
	.chan_rank_max	= CHAN_RANK_MAX_ON_BDXD,
	.dimm_idx_max	= DIMM_IDX_MAX_ON_BDXD,
	.min_peci_revision = 0x33,
	.read_thresholds = &read_thresholds_bdxd,
};

static const struct dimm_info dimm_skx = {
	.chan_rank_max	= CHAN_RANK_MAX_ON_SKX,
	.dimm_idx_max	= DIMM_IDX_MAX_ON_SKX,
	.min_peci_revision = 0x33,
	.read_thresholds = &read_thresholds_skx,
};

static const struct dimm_info dimm_icx = {
	.chan_rank_max	= CHAN_RANK_MAX_ON_ICX,
	.dimm_idx_max	= DIMM_IDX_MAX_ON_ICX,
	.min_peci_revision = 0x40,
	.read_thresholds = &read_thresholds_icx,
};

static const struct dimm_info dimm_icxd = {
	.chan_rank_max	= CHAN_RANK_MAX_ON_ICXD,
	.dimm_idx_max	= DIMM_IDX_MAX_ON_ICXD,
	.min_peci_revision = 0x40,
	.read_thresholds = &read_thresholds_icx,
};

static const struct auxiliary_device_id peci_dimmtemp_ids[] = {
	{
		.name = "peci_cpu.dimmtemp.hsx",
		.driver_data = (kernel_ulong_t)&dimm_hsx,
	},
	{
		.name = "peci_cpu.dimmtemp.bdx",
		.driver_data = (kernel_ulong_t)&dimm_bdx,
	},
	{
		.name = "peci_cpu.dimmtemp.bdxd",
		.driver_data = (kernel_ulong_t)&dimm_bdxd,
	},
	{
		.name = "peci_cpu.dimmtemp.skx",
		.driver_data = (kernel_ulong_t)&dimm_skx,
	},
	{
		.name = "peci_cpu.dimmtemp.icx",
		.driver_data = (kernel_ulong_t)&dimm_icx,
	},
	{
		.name = "peci_cpu.dimmtemp.icxd",
		.driver_data = (kernel_ulong_t)&dimm_icxd,
	},
	{ }
};
MODULE_DEVICE_TABLE(auxiliary, peci_dimmtemp_ids);

static struct auxiliary_driver peci_dimmtemp_driver = {
	.probe		= peci_dimmtemp_probe,
	.id_table	= peci_dimmtemp_ids,
};

module_auxiliary_driver(peci_dimmtemp_driver);

MODULE_AUTHOR("Jae Hyun Yoo <jae.hyun.yoo@linux.intel.com>");
MODULE_AUTHOR("Iwona Winiarska <iwona.winiarska@intel.com>");
MODULE_DESCRIPTION("PECI dimmtemp driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(PECI_CPU);
