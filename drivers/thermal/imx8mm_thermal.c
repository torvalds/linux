// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2020 NXP.
 *
 * Author: Anson Huang <Anson.Huang@nxp.com>
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/nvmem-consumer.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/thermal.h>

#include "thermal_hwmon.h"

#define TER			0x0	/* TMU enable */
#define TPS			0x4
#define TRITSR			0x20	/* TMU immediate temp */
/* TMU calibration data registers */
#define TASR			0x28
#define TASR_BUF_SLOPE_MASK	GENMASK(19, 16)
#define TASR_BUF_VREF_MASK	GENMASK(4, 0)	/* TMU_V1 */
#define TASR_BUF_VERF_SEL_MASK	GENMASK(1, 0)	/* TMU_V2 */
#define TCALIV(n)		(0x30 + ((n) * 4))
#define TCALIV_EN		BIT(31)
#define TCALIV_HR_MASK		GENMASK(23, 16)	/* TMU_V1 */
#define TCALIV_RT_MASK		GENMASK(7, 0)	/* TMU_V1 */
#define TCALIV_SNSR105C_MASK	GENMASK(27, 16)	/* TMU_V2 */
#define TCALIV_SNSR25C_MASK	GENMASK(11, 0)	/* TMU_V2 */
#define TRIM			0x3c
#define TRIM_BJT_CUR_MASK	GENMASK(23, 20)
#define TRIM_BGR_MASK		GENMASK(31, 28)
#define TRIM_VLSB_MASK		GENMASK(15, 12)
#define TRIM_EN_CH		BIT(7)

#define TER_ADC_PD		BIT(30)
#define TER_EN			BIT(31)
#define TRITSR_TEMP0_VAL_MASK	GENMASK(7, 0)
#define TRITSR_TEMP1_VAL_MASK	GENMASK(23, 16)

#define PROBE_SEL_ALL		GENMASK(31, 30)

#define probe_status_offset(x)	(30 + x)
#define SIGN_BIT		BIT(7)
#define TEMP_VAL_MASK		GENMASK(6, 0)

/* TMU OCOTP calibration data bitfields */
#define ANA0_EN			BIT(25)
#define ANA0_BUF_VREF_MASK	GENMASK(24, 20)
#define ANA0_BUF_SLOPE_MASK	GENMASK(19, 16)
#define ANA0_HR_MASK		GENMASK(15, 8)
#define ANA0_RT_MASK		GENMASK(7, 0)
#define TRIM2_VLSB_MASK		GENMASK(23, 20)
#define TRIM2_BGR_MASK		GENMASK(19, 16)
#define TRIM2_BJT_CUR_MASK	GENMASK(15, 12)
#define TRIM2_BUF_SLOP_SEL_MASK	GENMASK(11, 8)
#define TRIM2_BUF_VERF_SEL_MASK	GENMASK(7, 6)
#define TRIM3_TCA25_0_LSB_MASK	GENMASK(31, 28)
#define TRIM3_TCA40_0_MASK	GENMASK(27, 16)
#define TRIM4_TCA40_1_MASK	GENMASK(31, 20)
#define TRIM4_TCA105_0_MASK	GENMASK(19, 8)
#define TRIM4_TCA25_0_MSB_MASK	GENMASK(7, 0)
#define TRIM5_TCA105_1_MASK	GENMASK(23, 12)
#define TRIM5_TCA25_1_MASK	GENMASK(11, 0)

#define VER1_TEMP_LOW_LIMIT	10000
#define VER2_TEMP_LOW_LIMIT	-40000
#define VER2_TEMP_HIGH_LIMIT	125000

#define TMU_VER1		0x1
#define TMU_VER2		0x2

struct thermal_soc_data {
	u32 num_sensors;
	u32 version;
	int (*get_temp)(void *data, int *temp);
};

struct tmu_sensor {
	struct imx8mm_tmu *priv;
	u32 hw_id;
	struct thermal_zone_device *tzd;
};

struct imx8mm_tmu {
	void __iomem *base;
	struct clk *clk;
	const struct thermal_soc_data *socdata;
	struct tmu_sensor sensors[];
};

static int imx8mm_tmu_get_temp(void *data, int *temp)
{
	struct tmu_sensor *sensor = data;
	struct imx8mm_tmu *tmu = sensor->priv;
	u32 val;

	val = readl_relaxed(tmu->base + TRITSR) & TRITSR_TEMP0_VAL_MASK;

	/*
	 * Do not validate against the V bit (bit 31) due to errata
	 * ERR051272: TMU: Bit 31 of registers TMU_TSCR/TMU_TRITSR/TMU_TRATSR invalid
	 */

	*temp = val * 1000;
	if (*temp < VER1_TEMP_LOW_LIMIT || *temp > VER2_TEMP_HIGH_LIMIT)
		return -EAGAIN;

	return 0;
}

static int imx8mp_tmu_get_temp(void *data, int *temp)
{
	struct tmu_sensor *sensor = data;
	struct imx8mm_tmu *tmu = sensor->priv;
	unsigned long val;
	bool ready;

	val = readl_relaxed(tmu->base + TRITSR);
	ready = test_bit(probe_status_offset(sensor->hw_id), &val);
	if (!ready)
		return -EAGAIN;

	val = sensor->hw_id ? FIELD_GET(TRITSR_TEMP1_VAL_MASK, val) :
	      FIELD_GET(TRITSR_TEMP0_VAL_MASK, val);
	if (val & SIGN_BIT) /* negative */
		val = (~(val & TEMP_VAL_MASK) + 1);

	*temp = val * 1000;
	if (*temp < VER2_TEMP_LOW_LIMIT || *temp > VER2_TEMP_HIGH_LIMIT)
		return -EAGAIN;

	return 0;
}

static int tmu_get_temp(struct thermal_zone_device *tz, int *temp)
{
	struct tmu_sensor *sensor = thermal_zone_device_priv(tz);
	struct imx8mm_tmu *tmu = sensor->priv;

	return tmu->socdata->get_temp(sensor, temp);
}

static const struct thermal_zone_device_ops tmu_tz_ops = {
	.get_temp = tmu_get_temp,
};

static void imx8mm_tmu_enable(struct imx8mm_tmu *tmu, bool enable)
{
	u32 val;

	val = readl_relaxed(tmu->base + TER);
	val = enable ? (val | TER_EN) : (val & ~TER_EN);
	if (tmu->socdata->version == TMU_VER2)
		val = enable ? (val & ~TER_ADC_PD) : (val | TER_ADC_PD);
	writel_relaxed(val, tmu->base + TER);
}

static void imx8mm_tmu_probe_sel_all(struct imx8mm_tmu *tmu)
{
	u32 val;

	val = readl_relaxed(tmu->base + TPS);
	val |= PROBE_SEL_ALL;
	writel_relaxed(val, tmu->base + TPS);
}

static int imx8mm_tmu_probe_set_calib_v1(struct platform_device *pdev,
					 struct imx8mm_tmu *tmu)
{
	struct device *dev = &pdev->dev;
	u32 ana0;
	int ret;

	ret = nvmem_cell_read_u32(&pdev->dev, "calib", &ana0);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to read OCOTP nvmem cell\n");

	writel(FIELD_PREP(TASR_BUF_VREF_MASK,
			  FIELD_GET(ANA0_BUF_VREF_MASK, ana0)) |
	       FIELD_PREP(TASR_BUF_SLOPE_MASK,
			  FIELD_GET(ANA0_BUF_SLOPE_MASK, ana0)),
	       tmu->base + TASR);

	writel(FIELD_PREP(TCALIV_RT_MASK, FIELD_GET(ANA0_RT_MASK, ana0)) |
	       FIELD_PREP(TCALIV_HR_MASK, FIELD_GET(ANA0_HR_MASK, ana0)) |
	       ((ana0 & ANA0_EN) ? TCALIV_EN : 0),
	       tmu->base + TCALIV(0));

	return 0;
}

static int imx8mm_tmu_probe_set_calib_v2(struct platform_device *pdev,
					 struct imx8mm_tmu *tmu)
{
	struct device *dev = &pdev->dev;
	struct nvmem_cell *cell;
	u32 trim[4] = { 0 };
	size_t len;
	void *buf;

	cell = nvmem_cell_get(dev, "calib");
	if (IS_ERR(cell))
		return PTR_ERR(cell);

	buf = nvmem_cell_read(cell, &len);
	nvmem_cell_put(cell);

	if (IS_ERR(buf))
		return PTR_ERR(buf);

	memcpy(trim, buf, min(len, sizeof(trim)));
	kfree(buf);

	if (len != 16) {
		dev_err(dev,
			"OCOTP nvmem cell length is %zu, must be 16.\n", len);
		return -EINVAL;
	}

	/* Blank sample hardware */
	if (!trim[0] && !trim[1] && !trim[2] && !trim[3]) {
		/* Use a default 25C binary codes */
		writel(FIELD_PREP(TCALIV_SNSR25C_MASK, 0x63c),
		       tmu->base + TCALIV(0));
		writel(FIELD_PREP(TCALIV_SNSR25C_MASK, 0x63c),
		       tmu->base + TCALIV(1));
		return 0;
	}

	writel(FIELD_PREP(TASR_BUF_VERF_SEL_MASK,
			  FIELD_GET(TRIM2_BUF_VERF_SEL_MASK, trim[0])) |
	       FIELD_PREP(TASR_BUF_SLOPE_MASK,
			  FIELD_GET(TRIM2_BUF_SLOP_SEL_MASK, trim[0])),
	       tmu->base + TASR);

	writel(FIELD_PREP(TRIM_BJT_CUR_MASK,
			  FIELD_GET(TRIM2_BJT_CUR_MASK, trim[0])) |
	       FIELD_PREP(TRIM_BGR_MASK, FIELD_GET(TRIM2_BGR_MASK, trim[0])) |
	       FIELD_PREP(TRIM_VLSB_MASK, FIELD_GET(TRIM2_VLSB_MASK, trim[0])) |
	       TRIM_EN_CH,
	       tmu->base + TRIM);

	writel(FIELD_PREP(TCALIV_SNSR25C_MASK,
			  FIELD_GET(TRIM3_TCA25_0_LSB_MASK, trim[1]) |
			  (FIELD_GET(TRIM4_TCA25_0_MSB_MASK, trim[2]) << 4)) |
	       FIELD_PREP(TCALIV_SNSR105C_MASK,
			  FIELD_GET(TRIM4_TCA105_0_MASK, trim[2])),
	       tmu->base + TCALIV(0));

	writel(FIELD_PREP(TCALIV_SNSR25C_MASK,
			  FIELD_GET(TRIM5_TCA25_1_MASK, trim[3])) |
	       FIELD_PREP(TCALIV_SNSR105C_MASK,
			  FIELD_GET(TRIM5_TCA105_1_MASK, trim[3])),
	       tmu->base + TCALIV(1));

	writel(FIELD_PREP(TCALIV_SNSR25C_MASK,
			  FIELD_GET(TRIM3_TCA40_0_MASK, trim[1])) |
	       FIELD_PREP(TCALIV_SNSR105C_MASK,
			  FIELD_GET(TRIM4_TCA40_1_MASK, trim[2])),
	       tmu->base + TCALIV(2));

	return 0;
}

static int imx8mm_tmu_probe_set_calib(struct platform_device *pdev,
				      struct imx8mm_tmu *tmu)
{
	struct device *dev = &pdev->dev;

	/*
	 * Lack of calibration data OCOTP reference is not considered
	 * fatal to retain compatibility with old DTs. It is however
	 * strongly recommended to update such old DTs to get correct
	 * temperature compensation values for each SoC.
	 */
	if (!of_property_present(pdev->dev.of_node, "nvmem-cells")) {
		dev_warn(dev,
			 "No OCOTP nvmem reference found, SoC-specific calibration not loaded. Please update your DT.\n");
		return 0;
	}

	if (tmu->socdata->version == TMU_VER1)
		return imx8mm_tmu_probe_set_calib_v1(pdev, tmu);

	return imx8mm_tmu_probe_set_calib_v2(pdev, tmu);
}

static int imx8mm_tmu_probe(struct platform_device *pdev)
{
	const struct thermal_soc_data *data;
	struct imx8mm_tmu *tmu;
	int ret;
	int i;

	data = of_device_get_match_data(&pdev->dev);

	tmu = devm_kzalloc(&pdev->dev, struct_size(tmu, sensors,
			   data->num_sensors), GFP_KERNEL);
	if (!tmu)
		return -ENOMEM;

	tmu->socdata = data;

	tmu->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(tmu->base))
		return PTR_ERR(tmu->base);

	tmu->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(tmu->clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(tmu->clk),
				     "failed to get tmu clock\n");

	ret = clk_prepare_enable(tmu->clk);
	if (ret) {
		dev_err(&pdev->dev, "failed to enable tmu clock: %d\n", ret);
		return ret;
	}

	/* disable the monitor during initialization */
	imx8mm_tmu_enable(tmu, false);

	for (i = 0; i < data->num_sensors; i++) {
		tmu->sensors[i].priv = tmu;
		tmu->sensors[i].tzd =
			devm_thermal_of_zone_register(&pdev->dev, i,
						      &tmu->sensors[i],
						      &tmu_tz_ops);
		if (IS_ERR(tmu->sensors[i].tzd)) {
			ret = PTR_ERR(tmu->sensors[i].tzd);
			dev_err(&pdev->dev,
				"failed to register thermal zone sensor[%d]: %d\n",
				i, ret);
			goto disable_clk;
		}
		tmu->sensors[i].hw_id = i;

		devm_thermal_add_hwmon_sysfs(&pdev->dev, tmu->sensors[i].tzd);
	}

	platform_set_drvdata(pdev, tmu);

	ret = imx8mm_tmu_probe_set_calib(pdev, tmu);
	if (ret)
		goto disable_clk;

	/* enable all the probes for V2 TMU */
	if (tmu->socdata->version == TMU_VER2)
		imx8mm_tmu_probe_sel_all(tmu);

	/* enable the monitor */
	imx8mm_tmu_enable(tmu, true);

	return 0;

disable_clk:
	clk_disable_unprepare(tmu->clk);
	return ret;
}

static void imx8mm_tmu_remove(struct platform_device *pdev)
{
	struct imx8mm_tmu *tmu = platform_get_drvdata(pdev);

	/* disable TMU */
	imx8mm_tmu_enable(tmu, false);

	clk_disable_unprepare(tmu->clk);
	platform_set_drvdata(pdev, NULL);
}

static struct thermal_soc_data imx8mm_tmu_data = {
	.num_sensors = 1,
	.version = TMU_VER1,
	.get_temp = imx8mm_tmu_get_temp,
};

static struct thermal_soc_data imx8mp_tmu_data = {
	.num_sensors = 2,
	.version = TMU_VER2,
	.get_temp = imx8mp_tmu_get_temp,
};

static const struct of_device_id imx8mm_tmu_table[] = {
	{ .compatible = "fsl,imx8mm-tmu", .data = &imx8mm_tmu_data, },
	{ .compatible = "fsl,imx8mp-tmu", .data = &imx8mp_tmu_data, },
	{ },
};
MODULE_DEVICE_TABLE(of, imx8mm_tmu_table);

static struct platform_driver imx8mm_tmu = {
	.driver = {
		.name	= "i.mx8mm_thermal",
		.of_match_table = imx8mm_tmu_table,
	},
	.probe = imx8mm_tmu_probe,
	.remove = imx8mm_tmu_remove,
};
module_platform_driver(imx8mm_tmu);

MODULE_AUTHOR("Anson Huang <Anson.Huang@nxp.com>");
MODULE_DESCRIPTION("i.MX8MM Thermal Monitor Unit driver");
MODULE_LICENSE("GPL v2");
