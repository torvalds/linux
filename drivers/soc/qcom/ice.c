// SPDX-License-Identifier: GPL-2.0
/*
 * Qualcomm ICE (Inline Crypto Engine) support.
 *
 * Copyright (c) 2013-2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2019, Google LLC
 * Copyright (c) 2023, Linaro Limited
 */

#include <linux/bitfield.h>
#include <linux/cleanup.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/iopoll.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#include <linux/firmware/qcom/qcom_scm.h>

#include <soc/qcom/ice.h>

#define AES_256_XTS_KEY_SIZE			64

/* QCOM ICE registers */
#define QCOM_ICE_REG_VERSION			0x0008
#define QCOM_ICE_REG_FUSE_SETTING		0x0010
#define QCOM_ICE_REG_BIST_STATUS		0x0070
#define QCOM_ICE_REG_ADVANCED_CONTROL		0x1000

/* BIST ("built-in self-test") status flags */
#define QCOM_ICE_BIST_STATUS_MASK		GENMASK(31, 28)

#define QCOM_ICE_FUSE_SETTING_MASK		0x1
#define QCOM_ICE_FORCE_HW_KEY0_SETTING_MASK	0x2
#define QCOM_ICE_FORCE_HW_KEY1_SETTING_MASK	0x4

#define qcom_ice_writel(engine, val, reg)	\
	writel((val), (engine)->base + (reg))

#define qcom_ice_readl(engine, reg)	\
	readl((engine)->base + (reg))

struct qcom_ice {
	struct device *dev;
	void __iomem *base;

	struct clk *core_clk;
};

static bool qcom_ice_check_supported(struct qcom_ice *ice)
{
	u32 regval = qcom_ice_readl(ice, QCOM_ICE_REG_VERSION);
	struct device *dev = ice->dev;
	int major = FIELD_GET(GENMASK(31, 24), regval);
	int minor = FIELD_GET(GENMASK(23, 16), regval);
	int step = FIELD_GET(GENMASK(15, 0), regval);

	/* For now this driver only supports ICE version 3 and 4. */
	if (major != 3 && major != 4) {
		dev_warn(dev, "Unsupported ICE version: v%d.%d.%d\n",
			 major, minor, step);
		return false;
	}

	dev_info(dev, "Found QC Inline Crypto Engine (ICE) v%d.%d.%d\n",
		 major, minor, step);

	/* If fuses are blown, ICE might not work in the standard way. */
	regval = qcom_ice_readl(ice, QCOM_ICE_REG_FUSE_SETTING);
	if (regval & (QCOM_ICE_FUSE_SETTING_MASK |
		      QCOM_ICE_FORCE_HW_KEY0_SETTING_MASK |
		      QCOM_ICE_FORCE_HW_KEY1_SETTING_MASK)) {
		dev_warn(dev, "Fuses are blown; ICE is unusable!\n");
		return false;
	}

	return true;
}

static void qcom_ice_low_power_mode_enable(struct qcom_ice *ice)
{
	u32 regval;

	regval = qcom_ice_readl(ice, QCOM_ICE_REG_ADVANCED_CONTROL);

	/* Enable low power mode sequence */
	regval |= 0x7000;
	qcom_ice_writel(ice, regval, QCOM_ICE_REG_ADVANCED_CONTROL);
}

static void qcom_ice_optimization_enable(struct qcom_ice *ice)
{
	u32 regval;

	/* ICE Optimizations Enable Sequence */
	regval = qcom_ice_readl(ice, QCOM_ICE_REG_ADVANCED_CONTROL);
	regval |= 0xd807100;
	/* ICE HPG requires delay before writing */
	udelay(5);
	qcom_ice_writel(ice, regval, QCOM_ICE_REG_ADVANCED_CONTROL);
	udelay(5);
}

/*
 * Wait until the ICE BIST (built-in self-test) has completed.
 *
 * This may be necessary before ICE can be used.
 * Note that we don't really care whether the BIST passed or failed;
 * we really just want to make sure that it isn't still running. This is
 * because (a) the BIST is a FIPS compliance thing that never fails in
 * practice, (b) ICE is documented to reject crypto requests if the BIST
 * fails, so we needn't do it in software too, and (c) properly testing
 * storage encryption requires testing the full storage stack anyway,
 * and not relying on hardware-level self-tests.
 */
static int qcom_ice_wait_bist_status(struct qcom_ice *ice)
{
	u32 regval;
	int err;

	err = readl_poll_timeout(ice->base + QCOM_ICE_REG_BIST_STATUS,
				 regval, !(regval & QCOM_ICE_BIST_STATUS_MASK),
				 50, 5000);
	if (err)
		dev_err(ice->dev, "Timed out waiting for ICE self-test to complete\n");

	return err;
}

int qcom_ice_enable(struct qcom_ice *ice)
{
	qcom_ice_low_power_mode_enable(ice);
	qcom_ice_optimization_enable(ice);

	return qcom_ice_wait_bist_status(ice);
}
EXPORT_SYMBOL_GPL(qcom_ice_enable);

int qcom_ice_resume(struct qcom_ice *ice)
{
	struct device *dev = ice->dev;
	int err;

	err = clk_prepare_enable(ice->core_clk);
	if (err) {
		dev_err(dev, "failed to enable core clock (%d)\n",
			err);
		return err;
	}

	return qcom_ice_wait_bist_status(ice);
}
EXPORT_SYMBOL_GPL(qcom_ice_resume);

int qcom_ice_suspend(struct qcom_ice *ice)
{
	clk_disable_unprepare(ice->core_clk);

	return 0;
}
EXPORT_SYMBOL_GPL(qcom_ice_suspend);

int qcom_ice_program_key(struct qcom_ice *ice,
			 u8 algorithm_id, u8 key_size,
			 const u8 crypto_key[], u8 data_unit_size,
			 int slot)
{
	struct device *dev = ice->dev;
	union {
		u8 bytes[AES_256_XTS_KEY_SIZE];
		u32 words[AES_256_XTS_KEY_SIZE / sizeof(u32)];
	} key;
	int i;
	int err;

	/* Only AES-256-XTS has been tested so far. */
	if (algorithm_id != QCOM_ICE_CRYPTO_ALG_AES_XTS ||
	    key_size != QCOM_ICE_CRYPTO_KEY_SIZE_256) {
		dev_err_ratelimited(dev,
				    "Unhandled crypto capability; algorithm_id=%d, key_size=%d\n",
				    algorithm_id, key_size);
		return -EINVAL;
	}

	memcpy(key.bytes, crypto_key, AES_256_XTS_KEY_SIZE);

	/* The SCM call requires that the key words are encoded in big endian */
	for (i = 0; i < ARRAY_SIZE(key.words); i++)
		__cpu_to_be32s(&key.words[i]);

	err = qcom_scm_ice_set_key(slot, key.bytes, AES_256_XTS_KEY_SIZE,
				   QCOM_SCM_ICE_CIPHER_AES_256_XTS,
				   data_unit_size);

	memzero_explicit(&key, sizeof(key));

	return err;
}
EXPORT_SYMBOL_GPL(qcom_ice_program_key);

int qcom_ice_evict_key(struct qcom_ice *ice, int slot)
{
	return qcom_scm_ice_invalidate_key(slot);
}
EXPORT_SYMBOL_GPL(qcom_ice_evict_key);

static struct qcom_ice *qcom_ice_create(struct device *dev,
					void __iomem *base)
{
	struct qcom_ice *engine;

	if (!qcom_scm_is_available())
		return ERR_PTR(-EPROBE_DEFER);

	if (!qcom_scm_ice_available()) {
		dev_warn(dev, "ICE SCM interface not found\n");
		return NULL;
	}

	engine = devm_kzalloc(dev, sizeof(*engine), GFP_KERNEL);
	if (!engine)
		return ERR_PTR(-ENOMEM);

	engine->dev = dev;
	engine->base = base;

	/*
	 * Legacy DT binding uses different clk names for each consumer,
	 * so lets try those first. If none of those are a match, it means
	 * the we only have one clock and it is part of the dedicated DT node.
	 * Also, enable the clock before we check what HW version the driver
	 * supports.
	 */
	engine->core_clk = devm_clk_get_optional_enabled(dev, "ice_core_clk");
	if (!engine->core_clk)
		engine->core_clk = devm_clk_get_optional_enabled(dev, "ice");
	if (!engine->core_clk)
		engine->core_clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(engine->core_clk))
		return ERR_CAST(engine->core_clk);

	if (!qcom_ice_check_supported(engine))
		return ERR_PTR(-EOPNOTSUPP);

	dev_dbg(dev, "Registered Qualcomm Inline Crypto Engine\n");

	return engine;
}

/**
 * of_qcom_ice_get() - get an ICE instance from a DT node
 * @dev: device pointer for the consumer device
 *
 * This function will provide an ICE instance either by creating one for the
 * consumer device if its DT node provides the 'ice' reg range and the 'ice'
 * clock (for legacy DT style). On the other hand, if consumer provides a
 * phandle via 'qcom,ice' property to an ICE DT, the ICE instance will already
 * be created and so this function will return that instead.
 *
 * Return: ICE pointer on success, NULL if there is no ICE data provided by the
 * consumer or ERR_PTR() on error.
 */
static struct qcom_ice *of_qcom_ice_get(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct qcom_ice *ice;
	struct resource *res;
	void __iomem *base;
	struct device_link *link;

	if (!dev || !dev->of_node)
		return ERR_PTR(-ENODEV);

	/*
	 * In order to support legacy style devicetree bindings, we need
	 * to create the ICE instance using the consumer device and the reg
	 * range called 'ice' it provides.
	 */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "ice");
	if (res) {
		base = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(base))
			return ERR_CAST(base);

		/* create ICE instance using consumer dev */
		return qcom_ice_create(&pdev->dev, base);
	}

	/*
	 * If the consumer node does not provider an 'ice' reg range
	 * (legacy DT binding), then it must at least provide a phandle
	 * to the ICE devicetree node, otherwise ICE is not supported.
	 */
	struct device_node *node __free(device_node) = of_parse_phandle(dev->of_node,
									"qcom,ice", 0);
	if (!node)
		return NULL;

	pdev = of_find_device_by_node(node);
	if (!pdev) {
		dev_err(dev, "Cannot find device node %s\n", node->name);
		return ERR_PTR(-EPROBE_DEFER);
	}

	ice = platform_get_drvdata(pdev);
	if (!ice) {
		dev_err(dev, "Cannot get ice instance from %s\n",
			dev_name(&pdev->dev));
		platform_device_put(pdev);
		return ERR_PTR(-EPROBE_DEFER);
	}

	link = device_link_add(dev, &pdev->dev, DL_FLAG_AUTOREMOVE_SUPPLIER);
	if (!link) {
		dev_err(&pdev->dev,
			"Failed to create device link to consumer %s\n",
			dev_name(dev));
		platform_device_put(pdev);
		ice = ERR_PTR(-EINVAL);
	}

	return ice;
}

static void qcom_ice_put(const struct qcom_ice *ice)
{
	struct platform_device *pdev = to_platform_device(ice->dev);

	if (!platform_get_resource_byname(pdev, IORESOURCE_MEM, "ice"))
		platform_device_put(pdev);
}

static void devm_of_qcom_ice_put(struct device *dev, void *res)
{
	qcom_ice_put(*(struct qcom_ice **)res);
}

/**
 * devm_of_qcom_ice_get() - Devres managed helper to get an ICE instance from
 * a DT node.
 * @dev: device pointer for the consumer device.
 *
 * This function will provide an ICE instance either by creating one for the
 * consumer device if its DT node provides the 'ice' reg range and the 'ice'
 * clock (for legacy DT style). On the other hand, if consumer provides a
 * phandle via 'qcom,ice' property to an ICE DT, the ICE instance will already
 * be created and so this function will return that instead.
 *
 * Return: ICE pointer on success, NULL if there is no ICE data provided by the
 * consumer or ERR_PTR() on error.
 */
struct qcom_ice *devm_of_qcom_ice_get(struct device *dev)
{
	struct qcom_ice *ice, **dr;

	dr = devres_alloc(devm_of_qcom_ice_put, sizeof(*dr), GFP_KERNEL);
	if (!dr)
		return ERR_PTR(-ENOMEM);

	ice = of_qcom_ice_get(dev);
	if (!IS_ERR_OR_NULL(ice)) {
		*dr = ice;
		devres_add(dev, dr);
	} else {
		devres_free(dr);
	}

	return ice;
}
EXPORT_SYMBOL_GPL(devm_of_qcom_ice_get);

static int qcom_ice_probe(struct platform_device *pdev)
{
	struct qcom_ice *engine;
	void __iomem *base;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base)) {
		dev_warn(&pdev->dev, "ICE registers not found\n");
		return PTR_ERR(base);
	}

	engine = qcom_ice_create(&pdev->dev, base);
	if (IS_ERR(engine))
		return PTR_ERR(engine);

	platform_set_drvdata(pdev, engine);

	return 0;
}

static const struct of_device_id qcom_ice_of_match_table[] = {
	{ .compatible = "qcom,inline-crypto-engine" },
	{ },
};
MODULE_DEVICE_TABLE(of, qcom_ice_of_match_table);

static struct platform_driver qcom_ice_driver = {
	.probe	= qcom_ice_probe,
	.driver = {
		.name = "qcom-ice",
		.of_match_table = qcom_ice_of_match_table,
	},
};

module_platform_driver(qcom_ice_driver);

MODULE_DESCRIPTION("Qualcomm Inline Crypto Engine driver");
MODULE_LICENSE("GPL");
