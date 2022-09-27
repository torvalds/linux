// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2017-2019, The Linux Foundation. All rights reserved.
 *
 */

#include <linux/bitmap.h>
#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/soc/qcom/llcc-qcom.h>

#define ACTIVATE                      BIT(0)
#define DEACTIVATE                    BIT(1)
#define ACT_CTRL_OPCODE_ACTIVATE      BIT(0)
#define ACT_CTRL_OPCODE_DEACTIVATE    BIT(1)
#define ACT_CTRL_ACT_TRIG             BIT(0)
#define ACT_CTRL_OPCODE_SHIFT         0x01
#define ATTR1_PROBE_TARGET_WAYS_SHIFT 0x02
#define ATTR1_FIXED_SIZE_SHIFT        0x03
#define ATTR1_PRIORITY_SHIFT          0x04
#define ATTR1_MAX_CAP_SHIFT           0x10
#define ATTR0_RES_WAYS_MASK           GENMASK(11, 0)
#define ATTR0_BONUS_WAYS_MASK         GENMASK(27, 16)
#define ATTR0_BONUS_WAYS_SHIFT        0x10
#define LLCC_STATUS_READ_DELAY        100

#define CACHE_LINE_SIZE_SHIFT         6

#define LLCC_COMMON_STATUS0           0x0003000c
#define LLCC_LB_CNT_MASK              GENMASK(31, 28)
#define LLCC_LB_CNT_SHIFT             28

#define MAX_CAP_TO_BYTES(n)           (n * SZ_1K)
#define LLCC_TRP_ACT_CTRLn(n)         (n * SZ_4K)
#define LLCC_TRP_STATUSn(n)           (4 + n * SZ_4K)
#define LLCC_TRP_ATTR0_CFGn(n)        (0x21000 + SZ_8 * n)
#define LLCC_TRP_ATTR1_CFGn(n)        (0x21004 + SZ_8 * n)

#define BANK_OFFSET_STRIDE	      0x80000

/**
 * llcc_slice_config - Data associated with the llcc slice
 * @usecase_id: Unique id for the client's use case
 * @slice_id: llcc slice id for each client
 * @max_cap: The maximum capacity of the cache slice provided in KB
 * @priority: Priority of the client used to select victim line for replacement
 * @fixed_size: Boolean indicating if the slice has a fixed capacity
 * @bonus_ways: Bonus ways are additional ways to be used for any slice,
 *		if client ends up using more than reserved cache ways. Bonus
 *		ways are allocated only if they are not reserved for some
 *		other client.
 * @res_ways: Reserved ways for the cache slice, the reserved ways cannot
 *		be used by any other client than the one its assigned to.
 * @cache_mode: Each slice operates as a cache, this controls the mode of the
 *             slice: normal or TCM(Tightly Coupled Memory)
 * @probe_target_ways: Determines what ways to probe for access hit. When
 *                    configured to 1 only bonus and reserved ways are probed.
 *                    When configured to 0 all ways in llcc are probed.
 * @dis_cap_alloc: Disable capacity based allocation for a client
 * @retain_on_pc: If this bit is set and client has maintained active vote
 *               then the ways assigned to this client are not flushed on power
 *               collapse.
 * @activate_on_init: Activate the slice immediately after it is programmed
 */
struct llcc_slice_config {
	u32 usecase_id;
	u32 slice_id;
	u32 max_cap;
	u32 priority;
	bool fixed_size;
	u32 bonus_ways;
	u32 res_ways;
	u32 cache_mode;
	u32 probe_target_ways;
	bool dis_cap_alloc;
	bool retain_on_pc;
	bool activate_on_init;
};

struct qcom_llcc_config {
	const struct llcc_slice_config *sct_data;
	int size;
};

static const struct llcc_slice_config sc7180_data[] =  {
	{ LLCC_CPUSS,    1,  256, 1, 0, 0xf, 0x0, 0, 0, 0, 1, 1 },
	{ LLCC_MDM,      8,  128, 1, 0, 0xf, 0x0, 0, 0, 0, 1, 0 },
	{ LLCC_GPUHTW,   11, 128, 1, 0, 0xf, 0x0, 0, 0, 0, 1, 0 },
	{ LLCC_GPU,      12, 128, 1, 0, 0xf, 0x0, 0, 0, 0, 1, 0 },
};

static const struct llcc_slice_config sdm845_data[] =  {
	{ LLCC_CPUSS,    1,  2816, 1, 0, 0xffc, 0x2,   0, 0, 1, 1, 1 },
	{ LLCC_VIDSC0,   2,  512,  2, 1, 0x0,   0x0f0, 0, 0, 1, 1, 0 },
	{ LLCC_VIDSC1,   3,  512,  2, 1, 0x0,   0x0f0, 0, 0, 1, 1, 0 },
	{ LLCC_ROTATOR,  4,  563,  2, 1, 0x0,   0x00e, 2, 0, 1, 1, 0 },
	{ LLCC_VOICE,    5,  2816, 1, 0, 0xffc, 0x2,   0, 0, 1, 1, 0 },
	{ LLCC_AUDIO,    6,  2816, 1, 0, 0xffc, 0x2,   0, 0, 1, 1, 0 },
	{ LLCC_MDMHPGRW, 7,  1024, 2, 0, 0xfc,  0xf00, 0, 0, 1, 1, 0 },
	{ LLCC_MDM,      8,  2816, 1, 0, 0xffc, 0x2,   0, 0, 1, 1, 0 },
	{ LLCC_CMPT,     10, 2816, 1, 0, 0xffc, 0x2,   0, 0, 1, 1, 0 },
	{ LLCC_GPUHTW,   11, 512,  1, 1, 0xc,   0x0,   0, 0, 1, 1, 0 },
	{ LLCC_GPU,      12, 2304, 1, 0, 0xff0, 0x2,   0, 0, 1, 1, 0 },
	{ LLCC_MMUHWT,   13, 256,  2, 0, 0x0,   0x1,   0, 0, 1, 0, 1 },
	{ LLCC_CMPTDMA,  15, 2816, 1, 0, 0xffc, 0x2,   0, 0, 1, 1, 0 },
	{ LLCC_DISP,     16, 2816, 1, 0, 0xffc, 0x2,   0, 0, 1, 1, 0 },
	{ LLCC_VIDFW,    17, 2816, 1, 0, 0xffc, 0x2,   0, 0, 1, 1, 0 },
	{ LLCC_MDMHPFX,  20, 1024, 2, 1, 0x0,   0xf00, 0, 0, 1, 1, 0 },
	{ LLCC_MDMPNG,   21, 1024, 0, 1, 0x1e,  0x0,   0, 0, 1, 1, 0 },
	{ LLCC_AUDHW,    22, 1024, 1, 1, 0xffc, 0x2,   0, 0, 1, 1, 0 },
};

static const struct qcom_llcc_config sc7180_cfg = {
	.sct_data	= sc7180_data,
	.size		= ARRAY_SIZE(sc7180_data),
};

static const struct qcom_llcc_config sdm845_cfg = {
	.sct_data	= sdm845_data,
	.size		= ARRAY_SIZE(sdm845_data),
};

static struct llcc_drv_data *drv_data = (void *) -EPROBE_DEFER;

/**
 * llcc_slice_getd - get llcc slice descriptor
 * @uid: usecase_id for the client
 *
 * A pointer to llcc slice descriptor will be returned on success and
 * and error pointer is returned on failure
 */
struct llcc_slice_desc *llcc_slice_getd(u32 uid)
{
	const struct llcc_slice_config *cfg;
	struct llcc_slice_desc *desc;
	u32 sz, count;

	if (IS_ERR(drv_data))
		return ERR_CAST(drv_data);

	cfg = drv_data->cfg;
	sz = drv_data->cfg_size;

	for (count = 0; cfg && count < sz; count++, cfg++)
		if (cfg->usecase_id == uid)
			break;

	if (count == sz || !cfg)
		return ERR_PTR(-ENODEV);

	desc = kzalloc(sizeof(*desc), GFP_KERNEL);
	if (!desc)
		return ERR_PTR(-ENOMEM);

	desc->slice_id = cfg->slice_id;
	desc->slice_size = cfg->max_cap;

	return desc;
}
EXPORT_SYMBOL_GPL(llcc_slice_getd);

/**
 * llcc_slice_putd - llcc slice descritpor
 * @desc: Pointer to llcc slice descriptor
 */
void llcc_slice_putd(struct llcc_slice_desc *desc)
{
	if (!IS_ERR_OR_NULL(desc))
		kfree(desc);
}
EXPORT_SYMBOL_GPL(llcc_slice_putd);

static int llcc_update_act_ctrl(u32 sid,
				u32 act_ctrl_reg_val, u32 status)
{
	u32 act_ctrl_reg;
	u32 status_reg;
	u32 slice_status;
	int ret;

	if (IS_ERR(drv_data))
		return PTR_ERR(drv_data);

	act_ctrl_reg = LLCC_TRP_ACT_CTRLn(sid);
	status_reg = LLCC_TRP_STATUSn(sid);

	/* Set the ACTIVE trigger */
	act_ctrl_reg_val |= ACT_CTRL_ACT_TRIG;
	ret = regmap_write(drv_data->bcast_regmap, act_ctrl_reg,
				act_ctrl_reg_val);
	if (ret)
		return ret;

	/* Clear the ACTIVE trigger */
	act_ctrl_reg_val &= ~ACT_CTRL_ACT_TRIG;
	ret = regmap_write(drv_data->bcast_regmap, act_ctrl_reg,
				act_ctrl_reg_val);
	if (ret)
		return ret;

	ret = regmap_read_poll_timeout(drv_data->bcast_regmap, status_reg,
				      slice_status, !(slice_status & status),
				      0, LLCC_STATUS_READ_DELAY);
	return ret;
}

/**
 * llcc_slice_activate - Activate the llcc slice
 * @desc: Pointer to llcc slice descriptor
 *
 * A value of zero will be returned on success and a negative errno will
 * be returned in error cases
 */
int llcc_slice_activate(struct llcc_slice_desc *desc)
{
	int ret;
	u32 act_ctrl_val;

	if (IS_ERR(drv_data))
		return PTR_ERR(drv_data);

	if (IS_ERR_OR_NULL(desc))
		return -EINVAL;

	mutex_lock(&drv_data->lock);
	if (test_bit(desc->slice_id, drv_data->bitmap)) {
		mutex_unlock(&drv_data->lock);
		return 0;
	}

	act_ctrl_val = ACT_CTRL_OPCODE_ACTIVATE << ACT_CTRL_OPCODE_SHIFT;

	ret = llcc_update_act_ctrl(desc->slice_id, act_ctrl_val,
				  DEACTIVATE);
	if (ret) {
		mutex_unlock(&drv_data->lock);
		return ret;
	}

	__set_bit(desc->slice_id, drv_data->bitmap);
	mutex_unlock(&drv_data->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(llcc_slice_activate);

/**
 * llcc_slice_deactivate - Deactivate the llcc slice
 * @desc: Pointer to llcc slice descriptor
 *
 * A value of zero will be returned on success and a negative errno will
 * be returned in error cases
 */
int llcc_slice_deactivate(struct llcc_slice_desc *desc)
{
	u32 act_ctrl_val;
	int ret;

	if (IS_ERR(drv_data))
		return PTR_ERR(drv_data);

	if (IS_ERR_OR_NULL(desc))
		return -EINVAL;

	mutex_lock(&drv_data->lock);
	if (!test_bit(desc->slice_id, drv_data->bitmap)) {
		mutex_unlock(&drv_data->lock);
		return 0;
	}
	act_ctrl_val = ACT_CTRL_OPCODE_DEACTIVATE << ACT_CTRL_OPCODE_SHIFT;

	ret = llcc_update_act_ctrl(desc->slice_id, act_ctrl_val,
				  ACTIVATE);
	if (ret) {
		mutex_unlock(&drv_data->lock);
		return ret;
	}

	__clear_bit(desc->slice_id, drv_data->bitmap);
	mutex_unlock(&drv_data->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(llcc_slice_deactivate);

/**
 * llcc_get_slice_id - return the slice id
 * @desc: Pointer to llcc slice descriptor
 */
int llcc_get_slice_id(struct llcc_slice_desc *desc)
{
	if (IS_ERR_OR_NULL(desc))
		return -EINVAL;

	return desc->slice_id;
}
EXPORT_SYMBOL_GPL(llcc_get_slice_id);

/**
 * llcc_get_slice_size - return the slice id
 * @desc: Pointer to llcc slice descriptor
 */
size_t llcc_get_slice_size(struct llcc_slice_desc *desc)
{
	if (IS_ERR_OR_NULL(desc))
		return 0;

	return desc->slice_size;
}
EXPORT_SYMBOL_GPL(llcc_get_slice_size);

static int qcom_llcc_cfg_program(struct platform_device *pdev)
{
	int i;
	u32 attr1_cfg;
	u32 attr0_cfg;
	u32 attr1_val;
	u32 attr0_val;
	u32 max_cap_cacheline;
	u32 sz;
	int ret = 0;
	const struct llcc_slice_config *llcc_table;
	struct llcc_slice_desc desc;

	sz = drv_data->cfg_size;
	llcc_table = drv_data->cfg;

	for (i = 0; i < sz; i++) {
		attr1_cfg = LLCC_TRP_ATTR1_CFGn(llcc_table[i].slice_id);
		attr0_cfg = LLCC_TRP_ATTR0_CFGn(llcc_table[i].slice_id);

		attr1_val = llcc_table[i].cache_mode;
		attr1_val |= llcc_table[i].probe_target_ways <<
				ATTR1_PROBE_TARGET_WAYS_SHIFT;
		attr1_val |= llcc_table[i].fixed_size <<
				ATTR1_FIXED_SIZE_SHIFT;
		attr1_val |= llcc_table[i].priority <<
				ATTR1_PRIORITY_SHIFT;

		max_cap_cacheline = MAX_CAP_TO_BYTES(llcc_table[i].max_cap);

		/* LLCC instances can vary for each target.
		 * The SW writes to broadcast register which gets propagated
		 * to each llcc instace (llcc0,.. llccN).
		 * Since the size of the memory is divided equally amongst the
		 * llcc instances, we need to configure the max cap accordingly.
		 */
		max_cap_cacheline = max_cap_cacheline / drv_data->num_banks;
		max_cap_cacheline >>= CACHE_LINE_SIZE_SHIFT;
		attr1_val |= max_cap_cacheline << ATTR1_MAX_CAP_SHIFT;

		attr0_val = llcc_table[i].res_ways & ATTR0_RES_WAYS_MASK;
		attr0_val |= llcc_table[i].bonus_ways << ATTR0_BONUS_WAYS_SHIFT;

		ret = regmap_write(drv_data->bcast_regmap, attr1_cfg,
					attr1_val);
		if (ret)
			return ret;
		ret = regmap_write(drv_data->bcast_regmap, attr0_cfg,
					attr0_val);
		if (ret)
			return ret;
		if (llcc_table[i].activate_on_init) {
			desc.slice_id = llcc_table[i].slice_id;
			ret = llcc_slice_activate(&desc);
		}
	}
	return ret;
}

static int qcom_llcc_remove(struct platform_device *pdev)
{
	/* Set the global pointer to a error code to avoid referencing it */
	drv_data = ERR_PTR(-ENODEV);
	return 0;
}

static struct regmap *qcom_llcc_init_mmio(struct platform_device *pdev,
		const char *name)
{
	void __iomem *base;
	struct regmap_config llcc_regmap_config = {
		.reg_bits = 32,
		.reg_stride = 4,
		.val_bits = 32,
		.fast_io = true,
	};

	base = devm_platform_ioremap_resource_byname(pdev, name);
	if (IS_ERR(base))
		return ERR_CAST(base);

	llcc_regmap_config.name = name;
	return devm_regmap_init_mmio(&pdev->dev, base, &llcc_regmap_config);
}

static int qcom_llcc_probe(struct platform_device *pdev)
{
	u32 num_banks;
	struct device *dev = &pdev->dev;
	int ret, i;
	struct platform_device *llcc_edac;
	const struct qcom_llcc_config *cfg;
	const struct llcc_slice_config *llcc_cfg;
	u32 sz;

	drv_data = devm_kzalloc(dev, sizeof(*drv_data), GFP_KERNEL);
	if (!drv_data) {
		ret = -ENOMEM;
		goto err;
	}

	drv_data->regmap = qcom_llcc_init_mmio(pdev, "llcc_base");
	if (IS_ERR(drv_data->regmap)) {
		ret = PTR_ERR(drv_data->regmap);
		goto err;
	}

	drv_data->bcast_regmap =
		qcom_llcc_init_mmio(pdev, "llcc_broadcast_base");
	if (IS_ERR(drv_data->bcast_regmap)) {
		ret = PTR_ERR(drv_data->bcast_regmap);
		goto err;
	}

	ret = regmap_read(drv_data->regmap, LLCC_COMMON_STATUS0,
						&num_banks);
	if (ret)
		goto err;

	num_banks &= LLCC_LB_CNT_MASK;
	num_banks >>= LLCC_LB_CNT_SHIFT;
	drv_data->num_banks = num_banks;

	cfg = of_device_get_match_data(&pdev->dev);
	llcc_cfg = cfg->sct_data;
	sz = cfg->size;

	for (i = 0; i < sz; i++)
		if (llcc_cfg[i].slice_id > drv_data->max_slices)
			drv_data->max_slices = llcc_cfg[i].slice_id;

	drv_data->offsets = devm_kcalloc(dev, num_banks, sizeof(u32),
							GFP_KERNEL);
	if (!drv_data->offsets) {
		ret = -ENOMEM;
		goto err;
	}

	for (i = 0; i < num_banks; i++)
		drv_data->offsets[i] = i * BANK_OFFSET_STRIDE;

	drv_data->bitmap = devm_kcalloc(dev,
	BITS_TO_LONGS(drv_data->max_slices), sizeof(unsigned long),
						GFP_KERNEL);
	if (!drv_data->bitmap) {
		ret = -ENOMEM;
		goto err;
	}

	drv_data->cfg = llcc_cfg;
	drv_data->cfg_size = sz;
	mutex_init(&drv_data->lock);
	platform_set_drvdata(pdev, drv_data);

	ret = qcom_llcc_cfg_program(pdev);
	if (ret)
		goto err;

	drv_data->ecc_irq = platform_get_irq(pdev, 0);
	if (drv_data->ecc_irq >= 0) {
		llcc_edac = platform_device_register_data(&pdev->dev,
						"qcom_llcc_edac", -1, drv_data,
						sizeof(*drv_data));
		if (IS_ERR(llcc_edac))
			dev_err(dev, "Failed to register llcc edac driver\n");
	}

	return 0;
err:
	drv_data = ERR_PTR(-ENODEV);
	return ret;
}

static const struct of_device_id qcom_llcc_of_match[] = {
	{ .compatible = "qcom,sc7180-llcc", .data = &sc7180_cfg },
	{ .compatible = "qcom,sdm845-llcc", .data = &sdm845_cfg },
	{ }
};
MODULE_DEVICE_TABLE(of, qcom_llcc_of_match);

static struct platform_driver qcom_llcc_driver = {
	.driver = {
		.name = "qcom-llcc",
		.of_match_table = qcom_llcc_of_match,
	},
	.probe = qcom_llcc_probe,
	.remove = qcom_llcc_remove,
};
module_platform_driver(qcom_llcc_driver);

MODULE_DESCRIPTION("Qualcomm Last Level Cache Controller");
MODULE_LICENSE("GPL v2");
