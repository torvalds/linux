// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2015 Srinivas Kandagatla <srinivas.kandagatla@linaro.org>
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/nvmem-provider.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>

/* Blow timer clock frequency in Mhz */
#define QFPROM_BLOW_TIMER_OFFSET 0x03c

/* Amount of time required to hold charge to blow fuse in micro-seconds */
#define QFPROM_FUSE_BLOW_POLL_US	100
#define QFPROM_FUSE_BLOW_TIMEOUT_US	10000

#define QFPROM_BLOW_STATUS_OFFSET	0x048
#define QFPROM_BLOW_STATUS_BUSY		0x1
#define QFPROM_BLOW_STATUS_READY	0x0

#define QFPROM_ACCEL_OFFSET		0x044

#define QFPROM_VERSION_OFFSET		0x0
#define QFPROM_MAJOR_VERSION_SHIFT	28
#define QFPROM_MAJOR_VERSION_MASK	GENMASK(31, QFPROM_MAJOR_VERSION_SHIFT)
#define QFPROM_MINOR_VERSION_SHIFT	16
#define QFPROM_MINOR_VERSION_MASK	GENMASK(27, QFPROM_MINOR_VERSION_SHIFT)

static bool read_raw_data;
module_param(read_raw_data, bool, 0644);
MODULE_PARM_DESC(read_raw_data, "Read raw instead of corrected data");

/**
 * struct qfprom_soc_data - config that varies from SoC to SoC.
 *
 * @accel_value:             Should contain qfprom accel value.
 * @qfprom_blow_timer_value: The timer value of qfprom when doing efuse blow.
 * @qfprom_blow_set_freq:    The frequency required to set when we start the
 *                           fuse blowing.
 * @qfprom_blow_uV:          LDO voltage to be set when doing efuse blow
 */
struct qfprom_soc_data {
	u32 accel_value;
	u32 qfprom_blow_timer_value;
	u32 qfprom_blow_set_freq;
	int qfprom_blow_uV;
};

/**
 * struct qfprom_priv - structure holding qfprom attributes
 *
 * @qfpraw:       iomapped memory space for qfprom-efuse raw address space.
 * @qfpconf:      iomapped memory space for qfprom-efuse configuration address
 *                space.
 * @qfpcorrected: iomapped memory space for qfprom corrected address space.
 * @qfpsecurity:  iomapped memory space for qfprom security control space.
 * @dev:          qfprom device structure.
 * @secclk:       Clock supply.
 * @vcc:          Regulator supply.
 * @soc_data:     Data that for things that varies from SoC to SoC.
 */
struct qfprom_priv {
	void __iomem *qfpraw;
	void __iomem *qfpconf;
	void __iomem *qfpcorrected;
	void __iomem *qfpsecurity;
	struct device *dev;
	struct clk *secclk;
	struct regulator *vcc;
	const struct qfprom_soc_data *soc_data;
};

/**
 * struct qfprom_touched_values - saved values to restore after blowing
 *
 * @clk_rate: The rate the clock was at before blowing.
 * @accel_val: The value of the accel reg before blowing.
 * @timer_val: The value of the timer before blowing.
 */
struct qfprom_touched_values {
	unsigned long clk_rate;
	u32 accel_val;
	u32 timer_val;
};

/**
 * struct qfprom_soc_compatible_data - Data matched against the SoC
 * compatible string.
 *
 * @keepout: Array of keepout regions for this SoC.
 * @nkeepout: Number of elements in the keepout array.
 */
struct qfprom_soc_compatible_data {
	const struct nvmem_keepout *keepout;
	unsigned int nkeepout;
};

static const struct nvmem_keepout sc7180_qfprom_keepout[] = {
	{.start = 0x128, .end = 0x148},
	{.start = 0x220, .end = 0x228}
};

static const struct qfprom_soc_compatible_data sc7180_qfprom = {
	.keepout = sc7180_qfprom_keepout,
	.nkeepout = ARRAY_SIZE(sc7180_qfprom_keepout)
};

static const struct nvmem_keepout sc7280_qfprom_keepout[] = {
	{.start = 0x128, .end = 0x148},
	{.start = 0x238, .end = 0x248}
};

static const struct qfprom_soc_compatible_data sc7280_qfprom = {
	.keepout = sc7280_qfprom_keepout,
	.nkeepout = ARRAY_SIZE(sc7280_qfprom_keepout)
};

/**
 * qfprom_disable_fuse_blowing() - Undo enabling of fuse blowing.
 * @priv: Our driver data.
 * @old:  The data that was stashed from before fuse blowing.
 *
 * Resets the value of the blow timer, accel register and the clock
 * and voltage settings.
 *
 * Prints messages if there are errors but doesn't return an error code
 * since there's not much we can do upon failure.
 */
static void qfprom_disable_fuse_blowing(const struct qfprom_priv *priv,
					const struct qfprom_touched_values *old)
{
	int ret;

	writel(old->timer_val, priv->qfpconf + QFPROM_BLOW_TIMER_OFFSET);
	writel(old->accel_val, priv->qfpconf + QFPROM_ACCEL_OFFSET);

	dev_pm_genpd_set_performance_state(priv->dev, 0);
	pm_runtime_put(priv->dev);

	/*
	 * This may be a shared rail and may be able to run at a lower rate
	 * when we're not blowing fuses.  At the moment, the regulator framework
	 * applies voltage constraints even on disabled rails, so remove our
	 * constraints and allow the rail to be adjusted by other users.
	 */
	ret = regulator_set_voltage(priv->vcc, 0, INT_MAX);
	if (ret)
		dev_warn(priv->dev, "Failed to set 0 voltage (ignoring)\n");

	ret = regulator_disable(priv->vcc);
	if (ret)
		dev_warn(priv->dev, "Failed to disable regulator (ignoring)\n");

	ret = clk_set_rate(priv->secclk, old->clk_rate);
	if (ret)
		dev_warn(priv->dev,
			 "Failed to set clock rate for disable (ignoring)\n");

	clk_disable_unprepare(priv->secclk);
}

/**
 * qfprom_enable_fuse_blowing() - Enable fuse blowing.
 * @priv: Our driver data.
 * @old:  We'll stash stuff here to use when disabling.
 *
 * Sets the value of the blow timer, accel register and the clock
 * and voltage settings.
 *
 * Prints messages if there are errors so caller doesn't need to.
 *
 * Return: 0 or -err.
 */
static int qfprom_enable_fuse_blowing(const struct qfprom_priv *priv,
				      struct qfprom_touched_values *old)
{
	int ret;
	int qfprom_blow_uV = priv->soc_data->qfprom_blow_uV;

	ret = clk_prepare_enable(priv->secclk);
	if (ret) {
		dev_err(priv->dev, "Failed to enable clock\n");
		return ret;
	}

	old->clk_rate = clk_get_rate(priv->secclk);
	ret = clk_set_rate(priv->secclk, priv->soc_data->qfprom_blow_set_freq);
	if (ret) {
		dev_err(priv->dev, "Failed to set clock rate for enable\n");
		goto err_clk_prepared;
	}

	/*
	 * Hardware requires a minimum voltage for fuse blowing.
	 * This may be a shared rail so don't specify a maximum.
	 * Regulator constraints will cap to the actual maximum.
	 */
	ret = regulator_set_voltage(priv->vcc, qfprom_blow_uV, INT_MAX);
	if (ret) {
		dev_err(priv->dev, "Failed to set %duV\n", qfprom_blow_uV);
		goto err_clk_rate_set;
	}

	ret = regulator_enable(priv->vcc);
	if (ret) {
		dev_err(priv->dev, "Failed to enable regulator\n");
		goto err_clk_rate_set;
	}

	ret = pm_runtime_resume_and_get(priv->dev);
	if (ret < 0) {
		dev_err(priv->dev, "Failed to enable power-domain\n");
		goto err_reg_enable;
	}
	dev_pm_genpd_set_performance_state(priv->dev, INT_MAX);

	old->timer_val = readl(priv->qfpconf + QFPROM_BLOW_TIMER_OFFSET);
	old->accel_val = readl(priv->qfpconf + QFPROM_ACCEL_OFFSET);
	writel(priv->soc_data->qfprom_blow_timer_value,
	       priv->qfpconf + QFPROM_BLOW_TIMER_OFFSET);
	writel(priv->soc_data->accel_value,
	       priv->qfpconf + QFPROM_ACCEL_OFFSET);

	return 0;

err_reg_enable:
	regulator_disable(priv->vcc);
err_clk_rate_set:
	clk_set_rate(priv->secclk, old->clk_rate);
err_clk_prepared:
	clk_disable_unprepare(priv->secclk);
	return ret;
}

/**
 * qfprom_reg_write() - Write to fuses.
 * @context: Our driver data.
 * @reg:     The offset to write at.
 * @_val:    Pointer to data to write.
 * @bytes:   The number of bytes to write.
 *
 * Writes to fuses.  WARNING: THIS IS PERMANENT.
 *
 * Return: 0 or -err.
 */
static int qfprom_reg_write(void *context, unsigned int reg, void *_val,
			    size_t bytes)
{
	struct qfprom_priv *priv = context;
	struct qfprom_touched_values old;
	int words = bytes / 4;
	u32 *value = _val;
	u32 blow_status;
	int ret;
	int i;

	dev_dbg(priv->dev,
		"Writing to raw qfprom region : %#010x of size: %zu\n",
		reg, bytes);

	/*
	 * The hardware only allows us to write word at a time, but we can
	 * read byte at a time.  Until the nvmem framework allows a separate
	 * word_size and stride for reading vs. writing, we'll enforce here.
	 */
	if (bytes % 4) {
		dev_err(priv->dev,
			"%zu is not an integral number of words\n", bytes);
		return -EINVAL;
	}
	if (reg % 4) {
		dev_err(priv->dev,
			"Invalid offset: %#x.  Must be word aligned\n", reg);
		return -EINVAL;
	}

	ret = qfprom_enable_fuse_blowing(priv, &old);
	if (ret)
		return ret;

	ret = readl_relaxed_poll_timeout(
		priv->qfpconf + QFPROM_BLOW_STATUS_OFFSET,
		blow_status, blow_status == QFPROM_BLOW_STATUS_READY,
		QFPROM_FUSE_BLOW_POLL_US, QFPROM_FUSE_BLOW_TIMEOUT_US);

	if (ret) {
		dev_err(priv->dev,
			"Timeout waiting for initial ready; aborting.\n");
		goto exit_enabled_fuse_blowing;
	}

	for (i = 0; i < words; i++)
		writel(value[i], priv->qfpraw + reg + (i * 4));

	ret = readl_relaxed_poll_timeout(
		priv->qfpconf + QFPROM_BLOW_STATUS_OFFSET,
		blow_status, blow_status == QFPROM_BLOW_STATUS_READY,
		QFPROM_FUSE_BLOW_POLL_US, QFPROM_FUSE_BLOW_TIMEOUT_US);

	/* Give an error, but not much we can do in this case */
	if (ret)
		dev_err(priv->dev, "Timeout waiting for finish.\n");

exit_enabled_fuse_blowing:
	qfprom_disable_fuse_blowing(priv, &old);

	return ret;
}

static int qfprom_reg_read(void *context,
			unsigned int reg, void *_val, size_t bytes)
{
	struct qfprom_priv *priv = context;
	u8 *val = _val;
	int i = 0, words = bytes;
	void __iomem *base = priv->qfpcorrected;

	if (read_raw_data && priv->qfpraw)
		base = priv->qfpraw;

	while (words--)
		*val++ = readb(base + reg + i++);

	return 0;
}

static void qfprom_runtime_disable(void *data)
{
	pm_runtime_disable(data);
}

static const struct qfprom_soc_data qfprom_7_8_data = {
	.accel_value = 0xD10,
	.qfprom_blow_timer_value = 25,
	.qfprom_blow_set_freq = 4800000,
	.qfprom_blow_uV = 1800000,
};

static const struct qfprom_soc_data qfprom_7_15_data = {
	.accel_value = 0xD08,
	.qfprom_blow_timer_value = 24,
	.qfprom_blow_set_freq = 4800000,
	.qfprom_blow_uV = 1900000,
};

static int qfprom_probe(struct platform_device *pdev)
{
	struct nvmem_config econfig = {
		.name = "qfprom",
		.stride = 1,
		.word_size = 1,
		.id = NVMEM_DEVID_AUTO,
		.reg_read = qfprom_reg_read,
	};
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct nvmem_device *nvmem;
	const struct qfprom_soc_compatible_data *soc_data;
	struct qfprom_priv *priv;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	/* The corrected section is always provided */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->qfpcorrected = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->qfpcorrected))
		return PTR_ERR(priv->qfpcorrected);

	econfig.size = resource_size(res);
	econfig.dev = dev;
	econfig.priv = priv;

	priv->dev = dev;
	soc_data = device_get_match_data(dev);
	if (soc_data) {
		econfig.keepout = soc_data->keepout;
		econfig.nkeepout = soc_data->nkeepout;
	}

	/*
	 * If more than one region is provided then the OS has the ability
	 * to write.
	 */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (res) {
		u32 version;
		int major_version, minor_version;

		priv->qfpraw = devm_ioremap_resource(dev, res);
		if (IS_ERR(priv->qfpraw))
			return PTR_ERR(priv->qfpraw);
		res = platform_get_resource(pdev, IORESOURCE_MEM, 2);
		priv->qfpconf = devm_ioremap_resource(dev, res);
		if (IS_ERR(priv->qfpconf))
			return PTR_ERR(priv->qfpconf);
		res = platform_get_resource(pdev, IORESOURCE_MEM, 3);
		priv->qfpsecurity = devm_ioremap_resource(dev, res);
		if (IS_ERR(priv->qfpsecurity))
			return PTR_ERR(priv->qfpsecurity);

		version = readl(priv->qfpsecurity + QFPROM_VERSION_OFFSET);
		major_version = (version & QFPROM_MAJOR_VERSION_MASK) >>
				QFPROM_MAJOR_VERSION_SHIFT;
		minor_version = (version & QFPROM_MINOR_VERSION_MASK) >>
				QFPROM_MINOR_VERSION_SHIFT;

		if (major_version == 7 && minor_version == 8)
			priv->soc_data = &qfprom_7_8_data;
		else if (major_version == 7 && minor_version == 15)
			priv->soc_data = &qfprom_7_15_data;

		priv->vcc = devm_regulator_get(&pdev->dev, "vcc");
		if (IS_ERR(priv->vcc))
			return PTR_ERR(priv->vcc);

		priv->secclk = devm_clk_get(dev, "core");
		if (IS_ERR(priv->secclk)) {
			ret = PTR_ERR(priv->secclk);
			if (ret != -EPROBE_DEFER)
				dev_err(dev, "Error getting clock: %d\n", ret);
			return ret;
		}

		/* Only enable writing if we have SoC data. */
		if (priv->soc_data)
			econfig.reg_write = qfprom_reg_write;
	}

	pm_runtime_enable(dev);
	ret = devm_add_action_or_reset(dev, qfprom_runtime_disable, dev);
	if (ret)
		return ret;

	nvmem = devm_nvmem_register(dev, &econfig);

	return PTR_ERR_OR_ZERO(nvmem);
}

static const struct of_device_id qfprom_of_match[] = {
	{ .compatible = "qcom,qfprom",},
	{ .compatible = "qcom,sc7180-qfprom", .data = &sc7180_qfprom},
	{ .compatible = "qcom,sc7280-qfprom", .data = &sc7280_qfprom},
	{/* sentinel */},
};
MODULE_DEVICE_TABLE(of, qfprom_of_match);

static struct platform_driver qfprom_driver = {
	.probe = qfprom_probe,
	.driver = {
		.name = "qcom,qfprom",
		.of_match_table = qfprom_of_match,
	},
};
module_platform_driver(qfprom_driver);
MODULE_AUTHOR("Srinivas Kandagatla <srinivas.kandagatla@linaro.org>");
MODULE_DESCRIPTION("Qualcomm QFPROM driver");
MODULE_LICENSE("GPL v2");
