/*
 * Copyright (c) 2015 MediaTek Inc.
 * Author: Hanyi Wu <hanyi.wu@mediatek.com>
 *         Sascha Hauer <s.hauer@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/nvmem-consumer.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/thermal.h>
#include <linux/reset.h>
#include <linux/types.h>

/* AUXADC Registers */
#define AUXADC_CON0_V		0x000
#define AUXADC_CON1_V		0x004
#define AUXADC_CON1_SET_V	0x008
#define AUXADC_CON1_CLR_V	0x00c
#define AUXADC_CON2_V		0x010
#define AUXADC_DATA(channel)	(0x14 + (channel) * 4)
#define AUXADC_MISC_V		0x094

#define AUXADC_CON1_CHANNEL(x)	BIT(x)

#define APMIXED_SYS_TS_CON1	0x604

/* Thermal Controller Registers */
#define TEMP_MONCTL0		0x000
#define TEMP_MONCTL1		0x004
#define TEMP_MONCTL2		0x008
#define TEMP_MONIDET0		0x014
#define TEMP_MONIDET1		0x018
#define TEMP_MSRCTL0		0x038
#define TEMP_AHBPOLL		0x040
#define TEMP_AHBTO		0x044
#define TEMP_ADCPNP0		0x048
#define TEMP_ADCPNP1		0x04c
#define TEMP_ADCPNP2		0x050
#define TEMP_ADCPNP3		0x0b4

#define TEMP_ADCMUX		0x054
#define TEMP_ADCEN		0x060
#define TEMP_PNPMUXADDR		0x064
#define TEMP_ADCMUXADDR		0x068
#define TEMP_ADCENADDR		0x074
#define TEMP_ADCVALIDADDR	0x078
#define TEMP_ADCVOLTADDR	0x07c
#define TEMP_RDCTRL		0x080
#define TEMP_ADCVALIDMASK	0x084
#define TEMP_ADCVOLTAGESHIFT	0x088
#define TEMP_ADCWRITECTRL	0x08c
#define TEMP_MSR0		0x090
#define TEMP_MSR1		0x094
#define TEMP_MSR2		0x098
#define TEMP_MSR3		0x0B8

#define TEMP_SPARE0		0x0f0

#define PTPCORESEL		0x400

#define TEMP_MONCTL1_PERIOD_UNIT(x)	((x) & 0x3ff)

#define TEMP_MONCTL2_FILTER_INTERVAL(x)	(((x) & 0x3ff) << 16)
#define TEMP_MONCTL2_SENSOR_INTERVAL(x)	((x) & 0x3ff)

#define TEMP_AHBPOLL_ADC_POLL_INTERVAL(x)	(x)

#define TEMP_ADCWRITECTRL_ADC_PNP_WRITE		BIT(0)
#define TEMP_ADCWRITECTRL_ADC_MUX_WRITE		BIT(1)

#define TEMP_ADCVALIDMASK_VALID_HIGH		BIT(5)
#define TEMP_ADCVALIDMASK_VALID_POS(bit)	(bit)

#define MT8173_TS1	0
#define MT8173_TS2	1
#define MT8173_TS3	2
#define MT8173_TS4	3
#define MT8173_TSABB	4

/* AUXADC channel 11 is used for the temperature sensors */
#define MT8173_TEMP_AUXADC_CHANNEL	11

/* The total number of temperature sensors in the MT8173 */
#define MT8173_NUM_SENSORS		5

/* The number of banks in the MT8173 */
#define MT8173_NUM_ZONES		4

/* The number of sensing points per bank */
#define MT8173_NUM_SENSORS_PER_ZONE	4

/* Layout of the fuses providing the calibration data */
#define MT8173_CALIB_BUF0_VALID		BIT(0)
#define MT8173_CALIB_BUF1_ADC_GE(x)	(((x) >> 22) & 0x3ff)
#define MT8173_CALIB_BUF0_VTS_TS1(x)	(((x) >> 17) & 0x1ff)
#define MT8173_CALIB_BUF0_VTS_TS2(x)	(((x) >> 8) & 0x1ff)
#define MT8173_CALIB_BUF1_VTS_TS3(x)	(((x) >> 0) & 0x1ff)
#define MT8173_CALIB_BUF2_VTS_TS4(x)	(((x) >> 23) & 0x1ff)
#define MT8173_CALIB_BUF2_VTS_TSABB(x)	(((x) >> 14) & 0x1ff)
#define MT8173_CALIB_BUF0_DEGC_CALI(x)	(((x) >> 1) & 0x3f)
#define MT8173_CALIB_BUF0_O_SLOPE(x)	(((x) >> 26) & 0x3f)

#define THERMAL_NAME    "mtk-thermal"

struct mtk_thermal;

struct mtk_thermal_bank {
	struct mtk_thermal *mt;
	int id;
};

struct mtk_thermal {
	struct device *dev;
	void __iomem *thermal_base;

	struct clk *clk_peri_therm;
	struct clk *clk_auxadc;

	struct mtk_thermal_bank banks[MT8173_NUM_ZONES];

	/* lock: for getting and putting banks */
	struct mutex lock;

	/* Calibration values */
	s32 adc_ge;
	s32 degc_cali;
	s32 o_slope;
	s32 vts[MT8173_NUM_SENSORS];

};

struct mtk_thermal_bank_cfg {
	unsigned int num_sensors;
	unsigned int sensors[MT8173_NUM_SENSORS_PER_ZONE];
};

static const int sensor_mux_values[MT8173_NUM_SENSORS] = { 0, 1, 2, 3, 16 };

/*
 * The MT8173 thermal controller has four banks. Each bank can read up to
 * four temperature sensors simultaneously. The MT8173 has a total of 5
 * temperature sensors. We use each bank to measure a certain area of the
 * SoC. Since TS2 is located centrally in the SoC it is influenced by multiple
 * areas, hence is used in different banks.
 *
 * The thermal core only gets the maximum temperature of all banks, so
 * the bank concept wouldn't be necessary here. However, the SVS (Smart
 * Voltage Scaling) unit makes its decisions based on the same bank
 * data, and this indeed needs the temperatures of the individual banks
 * for making better decisions.
 */
static const struct mtk_thermal_bank_cfg bank_data[] = {
	{
		.num_sensors = 2,
		.sensors = { MT8173_TS2, MT8173_TS3 },
	}, {
		.num_sensors = 2,
		.sensors = { MT8173_TS2, MT8173_TS4 },
	}, {
		.num_sensors = 3,
		.sensors = { MT8173_TS1, MT8173_TS2, MT8173_TSABB },
	}, {
		.num_sensors = 1,
		.sensors = { MT8173_TS2 },
	},
};

struct mtk_thermal_sense_point {
	int msr;
	int adcpnp;
};

static const struct mtk_thermal_sense_point
		sensing_points[MT8173_NUM_SENSORS_PER_ZONE] = {
	{
		.msr = TEMP_MSR0,
		.adcpnp = TEMP_ADCPNP0,
	}, {
		.msr = TEMP_MSR1,
		.adcpnp = TEMP_ADCPNP1,
	}, {
		.msr = TEMP_MSR2,
		.adcpnp = TEMP_ADCPNP2,
	}, {
		.msr = TEMP_MSR3,
		.adcpnp = TEMP_ADCPNP3,
	},
};

/**
 * raw_to_mcelsius - convert a raw ADC value to mcelsius
 * @mt:		The thermal controller
 * @raw:	raw ADC value
 *
 * This converts the raw ADC value to mcelsius using the SoC specific
 * calibration constants
 */
static int raw_to_mcelsius(struct mtk_thermal *mt, int sensno, s32 raw)
{
	s32 tmp;

	raw &= 0xfff;

	tmp = 203450520 << 3;
	tmp /= 165 + mt->o_slope;
	tmp /= 10000 + mt->adc_ge;
	tmp *= raw - mt->vts[sensno] - 3350;
	tmp >>= 3;

	return mt->degc_cali * 500 - tmp;
}

/**
 * mtk_thermal_get_bank - get bank
 * @bank:	The bank
 *
 * The bank registers are banked, we have to select a bank in the
 * PTPCORESEL register to access it.
 */
static void mtk_thermal_get_bank(struct mtk_thermal_bank *bank)
{
	struct mtk_thermal *mt = bank->mt;
	u32 val;

	mutex_lock(&mt->lock);

	val = readl(mt->thermal_base + PTPCORESEL);
	val &= ~0xf;
	val |= bank->id;
	writel(val, mt->thermal_base + PTPCORESEL);
}

/**
 * mtk_thermal_put_bank - release bank
 * @bank:	The bank
 *
 * release a bank previously taken with mtk_thermal_get_bank,
 */
static void mtk_thermal_put_bank(struct mtk_thermal_bank *bank)
{
	struct mtk_thermal *mt = bank->mt;

	mutex_unlock(&mt->lock);
}

/**
 * mtk_thermal_bank_temperature - get the temperature of a bank
 * @bank:	The bank
 *
 * The temperature of a bank is considered the maximum temperature of
 * the sensors associated to the bank.
 */
static int mtk_thermal_bank_temperature(struct mtk_thermal_bank *bank)
{
	struct mtk_thermal *mt = bank->mt;
	int i, temp = INT_MIN, max = INT_MIN;
	u32 raw;

	for (i = 0; i < bank_data[bank->id].num_sensors; i++) {
		raw = readl(mt->thermal_base + sensing_points[i].msr);

		temp = raw_to_mcelsius(mt, bank_data[bank->id].sensors[i], raw);

		/*
		 * The first read of a sensor often contains very high bogus
		 * temperature value. Filter these out so that the system does
		 * not immediately shut down.
		 */
		if (temp > 200000)
			temp = 0;

		if (temp > max)
			max = temp;
	}

	return max;
}

static int mtk_read_temp(void *data, int *temperature)
{
	struct mtk_thermal *mt = data;
	int i;
	int tempmax = INT_MIN;

	for (i = 0; i < MT8173_NUM_ZONES; i++) {
		struct mtk_thermal_bank *bank = &mt->banks[i];

		mtk_thermal_get_bank(bank);

		tempmax = max(tempmax, mtk_thermal_bank_temperature(bank));

		mtk_thermal_put_bank(bank);
	}

	*temperature = tempmax;

	return 0;
}

static const struct thermal_zone_of_device_ops mtk_thermal_ops = {
	.get_temp = mtk_read_temp,
};

static void mtk_thermal_init_bank(struct mtk_thermal *mt, int num,
				  u32 apmixed_phys_base, u32 auxadc_phys_base)
{
	struct mtk_thermal_bank *bank = &mt->banks[num];
	const struct mtk_thermal_bank_cfg *cfg = &bank_data[num];
	int i;

	bank->id = num;
	bank->mt = mt;

	mtk_thermal_get_bank(bank);

	/* bus clock 66M counting unit is 12 * 15.15ns * 256 = 46.540us */
	writel(TEMP_MONCTL1_PERIOD_UNIT(12), mt->thermal_base + TEMP_MONCTL1);

	/*
	 * filt interval is 1 * 46.540us = 46.54us,
	 * sen interval is 429 * 46.540us = 19.96ms
	 */
	writel(TEMP_MONCTL2_FILTER_INTERVAL(1) |
			TEMP_MONCTL2_SENSOR_INTERVAL(429),
			mt->thermal_base + TEMP_MONCTL2);

	/* poll is set to 10u */
	writel(TEMP_AHBPOLL_ADC_POLL_INTERVAL(768),
	       mt->thermal_base + TEMP_AHBPOLL);

	/* temperature sampling control, 1 sample */
	writel(0x0, mt->thermal_base + TEMP_MSRCTL0);

	/* exceed this polling time, IRQ would be inserted */
	writel(0xffffffff, mt->thermal_base + TEMP_AHBTO);

	/* number of interrupts per event, 1 is enough */
	writel(0x0, mt->thermal_base + TEMP_MONIDET0);
	writel(0x0, mt->thermal_base + TEMP_MONIDET1);

	/*
	 * The MT8173 thermal controller does not have its own ADC. Instead it
	 * uses AHB bus accesses to control the AUXADC. To do this the thermal
	 * controller has to be programmed with the physical addresses of the
	 * AUXADC registers and with the various bit positions in the AUXADC.
	 * Also the thermal controller controls a mux in the APMIXEDSYS register
	 * space.
	 */

	/*
	 * this value will be stored to TEMP_PNPMUXADDR (TEMP_SPARE0)
	 * automatically by hw
	 */
	writel(BIT(MT8173_TEMP_AUXADC_CHANNEL), mt->thermal_base + TEMP_ADCMUX);

	/* AHB address for auxadc mux selection */
	writel(auxadc_phys_base + AUXADC_CON1_CLR_V,
	       mt->thermal_base + TEMP_ADCMUXADDR);

	/* AHB address for pnp sensor mux selection */
	writel(apmixed_phys_base + APMIXED_SYS_TS_CON1,
	       mt->thermal_base + TEMP_PNPMUXADDR);

	/* AHB value for auxadc enable */
	writel(BIT(MT8173_TEMP_AUXADC_CHANNEL), mt->thermal_base + TEMP_ADCEN);

	/* AHB address for auxadc enable (channel 0 immediate mode selected) */
	writel(auxadc_phys_base + AUXADC_CON1_SET_V,
	       mt->thermal_base + TEMP_ADCENADDR);

	/* AHB address for auxadc valid bit */
	writel(auxadc_phys_base + AUXADC_DATA(MT8173_TEMP_AUXADC_CHANNEL),
	       mt->thermal_base + TEMP_ADCVALIDADDR);

	/* AHB address for auxadc voltage output */
	writel(auxadc_phys_base + AUXADC_DATA(MT8173_TEMP_AUXADC_CHANNEL),
	       mt->thermal_base + TEMP_ADCVOLTADDR);

	/* read valid & voltage are at the same register */
	writel(0x0, mt->thermal_base + TEMP_RDCTRL);

	/* indicate where the valid bit is */
	writel(TEMP_ADCVALIDMASK_VALID_HIGH | TEMP_ADCVALIDMASK_VALID_POS(12),
	       mt->thermal_base + TEMP_ADCVALIDMASK);

	/* no shift */
	writel(0x0, mt->thermal_base + TEMP_ADCVOLTAGESHIFT);

	/* enable auxadc mux write transaction */
	writel(TEMP_ADCWRITECTRL_ADC_MUX_WRITE,
	       mt->thermal_base + TEMP_ADCWRITECTRL);

	for (i = 0; i < cfg->num_sensors; i++)
		writel(sensor_mux_values[cfg->sensors[i]],
		       mt->thermal_base + sensing_points[i].adcpnp);

	writel((1 << cfg->num_sensors) - 1, mt->thermal_base + TEMP_MONCTL0);

	writel(TEMP_ADCWRITECTRL_ADC_PNP_WRITE |
	       TEMP_ADCWRITECTRL_ADC_MUX_WRITE,
	       mt->thermal_base + TEMP_ADCWRITECTRL);

	mtk_thermal_put_bank(bank);
}

static u64 of_get_phys_base(struct device_node *np)
{
	u64 size64;
	const __be32 *regaddr_p;

	regaddr_p = of_get_address(np, 0, &size64, NULL);
	if (!regaddr_p)
		return OF_BAD_ADDR;

	return of_translate_address(np, regaddr_p);
}

static int mtk_thermal_get_calibration_data(struct device *dev,
					    struct mtk_thermal *mt)
{
	struct nvmem_cell *cell;
	u32 *buf;
	size_t len;
	int i, ret = 0;

	/* Start with default values */
	mt->adc_ge = 512;
	for (i = 0; i < MT8173_NUM_SENSORS; i++)
		mt->vts[i] = 260;
	mt->degc_cali = 40;
	mt->o_slope = 0;

	cell = nvmem_cell_get(dev, "calibration-data");
	if (IS_ERR(cell)) {
		if (PTR_ERR(cell) == -EPROBE_DEFER)
			return PTR_ERR(cell);
		return 0;
	}

	buf = (u32 *)nvmem_cell_read(cell, &len);

	nvmem_cell_put(cell);

	if (IS_ERR(buf))
		return PTR_ERR(buf);

	if (len < 3 * sizeof(u32)) {
		dev_warn(dev, "invalid calibration data\n");
		ret = -EINVAL;
		goto out;
	}

	if (buf[0] & MT8173_CALIB_BUF0_VALID) {
		mt->adc_ge = MT8173_CALIB_BUF1_ADC_GE(buf[1]);
		mt->vts[MT8173_TS1] = MT8173_CALIB_BUF0_VTS_TS1(buf[0]);
		mt->vts[MT8173_TS2] = MT8173_CALIB_BUF0_VTS_TS2(buf[0]);
		mt->vts[MT8173_TS3] = MT8173_CALIB_BUF1_VTS_TS3(buf[1]);
		mt->vts[MT8173_TS4] = MT8173_CALIB_BUF2_VTS_TS4(buf[2]);
		mt->vts[MT8173_TSABB] = MT8173_CALIB_BUF2_VTS_TSABB(buf[2]);
		mt->degc_cali = MT8173_CALIB_BUF0_DEGC_CALI(buf[0]);
		mt->o_slope = MT8173_CALIB_BUF0_O_SLOPE(buf[0]);
	} else {
		dev_info(dev, "Device not calibrated, using default calibration values\n");
	}

out:
	kfree(buf);

	return ret;
}

static int mtk_thermal_probe(struct platform_device *pdev)
{
	int ret, i;
	struct device_node *auxadc, *apmixedsys, *np = pdev->dev.of_node;
	struct mtk_thermal *mt;
	struct resource *res;
	u64 auxadc_phys_base, apmixed_phys_base;

	mt = devm_kzalloc(&pdev->dev, sizeof(*mt), GFP_KERNEL);
	if (!mt)
		return -ENOMEM;

	mt->clk_peri_therm = devm_clk_get(&pdev->dev, "therm");
	if (IS_ERR(mt->clk_peri_therm))
		return PTR_ERR(mt->clk_peri_therm);

	mt->clk_auxadc = devm_clk_get(&pdev->dev, "auxadc");
	if (IS_ERR(mt->clk_auxadc))
		return PTR_ERR(mt->clk_auxadc);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	mt->thermal_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(mt->thermal_base))
		return PTR_ERR(mt->thermal_base);

	ret = mtk_thermal_get_calibration_data(&pdev->dev, mt);
	if (ret)
		return ret;

	mutex_init(&mt->lock);

	mt->dev = &pdev->dev;

	auxadc = of_parse_phandle(np, "mediatek,auxadc", 0);
	if (!auxadc) {
		dev_err(&pdev->dev, "missing auxadc node\n");
		return -ENODEV;
	}

	auxadc_phys_base = of_get_phys_base(auxadc);

	of_node_put(auxadc);

	if (auxadc_phys_base == OF_BAD_ADDR) {
		dev_err(&pdev->dev, "Can't get auxadc phys address\n");
		return -EINVAL;
	}

	apmixedsys = of_parse_phandle(np, "mediatek,apmixedsys", 0);
	if (!apmixedsys) {
		dev_err(&pdev->dev, "missing apmixedsys node\n");
		return -ENODEV;
	}

	apmixed_phys_base = of_get_phys_base(apmixedsys);

	of_node_put(apmixedsys);

	if (apmixed_phys_base == OF_BAD_ADDR) {
		dev_err(&pdev->dev, "Can't get auxadc phys address\n");
		return -EINVAL;
	}

	ret = clk_prepare_enable(mt->clk_auxadc);
	if (ret) {
		dev_err(&pdev->dev, "Can't enable auxadc clk: %d\n", ret);
		return ret;
	}

	ret = device_reset(&pdev->dev);
	if (ret)
		goto err_disable_clk_auxadc;

	ret = clk_prepare_enable(mt->clk_peri_therm);
	if (ret) {
		dev_err(&pdev->dev, "Can't enable peri clk: %d\n", ret);
		goto err_disable_clk_auxadc;
	}

	for (i = 0; i < MT8173_NUM_ZONES; i++)
		mtk_thermal_init_bank(mt, i, apmixed_phys_base,
				      auxadc_phys_base);

	platform_set_drvdata(pdev, mt);

	devm_thermal_zone_of_sensor_register(&pdev->dev, 0, mt,
					     &mtk_thermal_ops);

	return 0;

err_disable_clk_auxadc:
	clk_disable_unprepare(mt->clk_auxadc);

	return ret;
}

static int mtk_thermal_remove(struct platform_device *pdev)
{
	struct mtk_thermal *mt = platform_get_drvdata(pdev);

	clk_disable_unprepare(mt->clk_peri_therm);
	clk_disable_unprepare(mt->clk_auxadc);

	return 0;
}

static const struct of_device_id mtk_thermal_of_match[] = {
	{
		.compatible = "mediatek,mt8173-thermal",
	}, {
	},
};

static struct platform_driver mtk_thermal_driver = {
	.probe = mtk_thermal_probe,
	.remove = mtk_thermal_remove,
	.driver = {
		.name = THERMAL_NAME,
		.of_match_table = mtk_thermal_of_match,
	},
};

module_platform_driver(mtk_thermal_driver);

MODULE_AUTHOR("Sascha Hauer <s.hauer@pengutronix.de>");
MODULE_AUTHOR("Hanyi Wu <hanyi.wu@mediatek.com>");
MODULE_DESCRIPTION("Mediatek thermal driver");
MODULE_LICENSE("GPL v2");
