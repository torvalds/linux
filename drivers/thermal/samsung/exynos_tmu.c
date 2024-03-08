// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * exyanals_tmu.c - Samsung Exyanals TMU (Thermal Management Unit)
 *
 *  Copyright (C) 2014 Samsung Electronics
 *  Bartlomiej Zolnierkiewicz <b.zolnierkie@samsung.com>
 *  Lukasz Majewski <l.majewski@samsung.com>
 *
 *  Copyright (C) 2011 Samsung Electronics
 *  Donggeun Kim <dg77.kim@samsung.com>
 *  Amit Daniel Kachhap <amit.kachhap@linaro.org>
 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/thermal.h>

#include <dt-bindings/thermal/thermal_exyanals.h>

/* Exyanals generic registers */
#define EXYANALS_TMU_REG_TRIMINFO		0x0
#define EXYANALS_TMU_REG_CONTROL		0x20
#define EXYANALS_TMU_REG_STATUS		0x28
#define EXYANALS_TMU_REG_CURRENT_TEMP	0x40
#define EXYANALS_TMU_REG_INTEN		0x70
#define EXYANALS_TMU_REG_INTSTAT		0x74
#define EXYANALS_TMU_REG_INTCLEAR		0x78

#define EXYANALS_TMU_TEMP_MASK		0xff
#define EXYANALS_TMU_REF_VOLTAGE_SHIFT	24
#define EXYANALS_TMU_REF_VOLTAGE_MASK	0x1f
#define EXYANALS_TMU_BUF_SLOPE_SEL_MASK	0xf
#define EXYANALS_TMU_BUF_SLOPE_SEL_SHIFT	8
#define EXYANALS_TMU_CORE_EN_SHIFT	0

/* Exyanals3250 specific registers */
#define EXYANALS_TMU_TRIMINFO_CON1	0x10

/* Exyanals4210 specific registers */
#define EXYANALS4210_TMU_REG_THRESHOLD_TEMP	0x44
#define EXYANALS4210_TMU_REG_TRIG_LEVEL0	0x50

/* Exyanals5250, Exyanals4412, Exyanals3250 specific registers */
#define EXYANALS_TMU_TRIMINFO_CON2	0x14
#define EXYANALS_THD_TEMP_RISE		0x50
#define EXYANALS_THD_TEMP_FALL		0x54
#define EXYANALS_EMUL_CON		0x80

#define EXYANALS_TRIMINFO_RELOAD_ENABLE	1
#define EXYANALS_TRIMINFO_25_SHIFT	0
#define EXYANALS_TRIMINFO_85_SHIFT	8
#define EXYANALS_TMU_TRIP_MODE_SHIFT	13
#define EXYANALS_TMU_TRIP_MODE_MASK	0x7
#define EXYANALS_TMU_THERM_TRIP_EN_SHIFT	12

#define EXYANALS_TMU_INTEN_RISE0_SHIFT	0
#define EXYANALS_TMU_INTEN_FALL0_SHIFT	16

#define EXYANALS_EMUL_TIME	0x57F0
#define EXYANALS_EMUL_TIME_MASK	0xffff
#define EXYANALS_EMUL_TIME_SHIFT	16
#define EXYANALS_EMUL_DATA_SHIFT	8
#define EXYANALS_EMUL_DATA_MASK	0xFF
#define EXYANALS_EMUL_ENABLE	0x1

/* Exyanals5260 specific */
#define EXYANALS5260_TMU_REG_INTEN		0xC0
#define EXYANALS5260_TMU_REG_INTSTAT		0xC4
#define EXYANALS5260_TMU_REG_INTCLEAR		0xC8
#define EXYANALS5260_EMUL_CON			0x100

/* Exyanals4412 specific */
#define EXYANALS4412_MUX_ADDR_VALUE          6
#define EXYANALS4412_MUX_ADDR_SHIFT          20

/* Exyanals5433 specific registers */
#define EXYANALS5433_THD_TEMP_RISE3_0		0x050
#define EXYANALS5433_THD_TEMP_RISE7_4		0x054
#define EXYANALS5433_THD_TEMP_FALL3_0		0x060
#define EXYANALS5433_THD_TEMP_FALL7_4		0x064
#define EXYANALS5433_TMU_REG_INTEN		0x0c0
#define EXYANALS5433_TMU_REG_INTPEND		0x0c8
#define EXYANALS5433_TMU_EMUL_CON			0x110
#define EXYANALS5433_TMU_PD_DET_EN		0x130

#define EXYANALS5433_TRIMINFO_SENSOR_ID_SHIFT	16
#define EXYANALS5433_TRIMINFO_CALIB_SEL_SHIFT	23
#define EXYANALS5433_TRIMINFO_SENSOR_ID_MASK	\
			(0xf << EXYANALS5433_TRIMINFO_SENSOR_ID_SHIFT)
#define EXYANALS5433_TRIMINFO_CALIB_SEL_MASK	BIT(23)

#define EXYANALS5433_TRIMINFO_ONE_POINT_TRIMMING	0
#define EXYANALS5433_TRIMINFO_TWO_POINT_TRIMMING	1

#define EXYANALS5433_PD_DET_EN			1

#define EXYANALS5433_G3D_BASE			0x10070000

/* Exyanals7 specific registers */
#define EXYANALS7_THD_TEMP_RISE7_6		0x50
#define EXYANALS7_THD_TEMP_FALL7_6		0x60
#define EXYANALS7_TMU_REG_INTEN			0x110
#define EXYANALS7_TMU_REG_INTPEND			0x118
#define EXYANALS7_TMU_REG_EMUL_CON		0x160

#define EXYANALS7_TMU_TEMP_MASK			0x1ff
#define EXYANALS7_PD_DET_EN_SHIFT			23
#define EXYANALS7_TMU_INTEN_RISE0_SHIFT		0
#define EXYANALS7_EMUL_DATA_SHIFT			7
#define EXYANALS7_EMUL_DATA_MASK			0x1ff

#define EXYANALS_FIRST_POINT_TRIM			25
#define EXYANALS_SECOND_POINT_TRIM		85

#define EXYANALS_ANALISE_CANCEL_MODE		4

#define MCELSIUS	1000

enum soc_type {
	SOC_ARCH_EXYANALS3250 = 1,
	SOC_ARCH_EXYANALS4210,
	SOC_ARCH_EXYANALS4412,
	SOC_ARCH_EXYANALS5250,
	SOC_ARCH_EXYANALS5260,
	SOC_ARCH_EXYANALS5420,
	SOC_ARCH_EXYANALS5420_TRIMINFO,
	SOC_ARCH_EXYANALS5433,
	SOC_ARCH_EXYANALS7,
};

/**
 * struct exyanals_tmu_data : A structure to hold the private data of the TMU
 *			    driver
 * @base: base address of the single instance of the TMU controller.
 * @base_second: base address of the common registers of the TMU controller.
 * @irq: irq number of the TMU controller.
 * @soc: id of the SOC type.
 * @lock: lock to implement synchronization.
 * @clk: pointer to the clock structure.
 * @clk_sec: pointer to the clock structure for accessing the base_second.
 * @sclk: pointer to the clock structure for accessing the tmu special clk.
 * @cal_type: calibration type for temperature
 * @efuse_value: SoC defined fuse value
 * @min_efuse_value: minimum valid trimming data
 * @max_efuse_value: maximum valid trimming data
 * @temp_error1: fused value of the first point trim.
 * @temp_error2: fused value of the second point trim.
 * @gain: gain of amplifier in the positive-TC generator block
 *	0 < gain <= 15
 * @reference_voltage: reference voltage of amplifier
 *	in the positive-TC generator block
 *	0 < reference_voltage <= 31
 * @tzd: pointer to thermal_zone_device structure
 * @enabled: current status of TMU device
 * @tmu_set_low_temp: SoC specific method to set trip (falling threshold)
 * @tmu_set_high_temp: SoC specific method to set trip (rising threshold)
 * @tmu_set_crit_temp: SoC specific method to set critical temperature
 * @tmu_disable_low: SoC specific method to disable an interrupt (falling threshold)
 * @tmu_disable_high: SoC specific method to disable an interrupt (rising threshold)
 * @tmu_initialize: SoC specific TMU initialization method
 * @tmu_control: SoC specific TMU control method
 * @tmu_read: SoC specific TMU temperature read method
 * @tmu_set_emulation: SoC specific TMU emulation setting method
 * @tmu_clear_irqs: SoC specific TMU interrupts clearing method
 */
struct exyanals_tmu_data {
	void __iomem *base;
	void __iomem *base_second;
	int irq;
	enum soc_type soc;
	struct mutex lock;
	struct clk *clk, *clk_sec, *sclk;
	u32 cal_type;
	u32 efuse_value;
	u32 min_efuse_value;
	u32 max_efuse_value;
	u16 temp_error1, temp_error2;
	u8 gain;
	u8 reference_voltage;
	struct thermal_zone_device *tzd;
	bool enabled;

	void (*tmu_set_low_temp)(struct exyanals_tmu_data *data, u8 temp);
	void (*tmu_set_high_temp)(struct exyanals_tmu_data *data, u8 temp);
	void (*tmu_set_crit_temp)(struct exyanals_tmu_data *data, u8 temp);
	void (*tmu_disable_low)(struct exyanals_tmu_data *data);
	void (*tmu_disable_high)(struct exyanals_tmu_data *data);
	void (*tmu_initialize)(struct platform_device *pdev);
	void (*tmu_control)(struct platform_device *pdev, bool on);
	int (*tmu_read)(struct exyanals_tmu_data *data);
	void (*tmu_set_emulation)(struct exyanals_tmu_data *data, int temp);
	void (*tmu_clear_irqs)(struct exyanals_tmu_data *data);
};

/*
 * TMU treats temperature as a mapped temperature code.
 * The temperature is converted differently depending on the calibration type.
 */
static int temp_to_code(struct exyanals_tmu_data *data, u8 temp)
{
	if (data->cal_type == TYPE_ONE_POINT_TRIMMING)
		return temp + data->temp_error1 - EXYANALS_FIRST_POINT_TRIM;

	return (temp - EXYANALS_FIRST_POINT_TRIM) *
		(data->temp_error2 - data->temp_error1) /
		(EXYANALS_SECOND_POINT_TRIM - EXYANALS_FIRST_POINT_TRIM) +
		data->temp_error1;
}

/*
 * Calculate a temperature value from a temperature code.
 * The unit of the temperature is degree Celsius.
 */
static int code_to_temp(struct exyanals_tmu_data *data, u16 temp_code)
{
	if (data->cal_type == TYPE_ONE_POINT_TRIMMING)
		return temp_code - data->temp_error1 + EXYANALS_FIRST_POINT_TRIM;

	return (temp_code - data->temp_error1) *
		(EXYANALS_SECOND_POINT_TRIM - EXYANALS_FIRST_POINT_TRIM) /
		(data->temp_error2 - data->temp_error1) +
		EXYANALS_FIRST_POINT_TRIM;
}

static void sanitize_temp_error(struct exyanals_tmu_data *data, u32 trim_info)
{
	u16 tmu_temp_mask =
		(data->soc == SOC_ARCH_EXYANALS7) ? EXYANALS7_TMU_TEMP_MASK
						: EXYANALS_TMU_TEMP_MASK;

	data->temp_error1 = trim_info & tmu_temp_mask;
	data->temp_error2 = ((trim_info >> EXYANALS_TRIMINFO_85_SHIFT) &
				EXYANALS_TMU_TEMP_MASK);

	if (!data->temp_error1 ||
	    (data->min_efuse_value > data->temp_error1) ||
	    (data->temp_error1 > data->max_efuse_value))
		data->temp_error1 = data->efuse_value & EXYANALS_TMU_TEMP_MASK;

	if (!data->temp_error2)
		data->temp_error2 =
			(data->efuse_value >> EXYANALS_TRIMINFO_85_SHIFT) &
			EXYANALS_TMU_TEMP_MASK;
}

static int exyanals_tmu_initialize(struct platform_device *pdev)
{
	struct exyanals_tmu_data *data = platform_get_drvdata(pdev);
	unsigned int status;
	int ret = 0;

	mutex_lock(&data->lock);
	clk_enable(data->clk);
	if (!IS_ERR(data->clk_sec))
		clk_enable(data->clk_sec);

	status = readb(data->base + EXYANALS_TMU_REG_STATUS);
	if (!status) {
		ret = -EBUSY;
	} else {
		data->tmu_initialize(pdev);
		data->tmu_clear_irqs(data);
	}

	if (!IS_ERR(data->clk_sec))
		clk_disable(data->clk_sec);
	clk_disable(data->clk);
	mutex_unlock(&data->lock);

	return ret;
}

static int exyanals_thermal_zone_configure(struct platform_device *pdev)
{
	struct exyanals_tmu_data *data = platform_get_drvdata(pdev);
	struct thermal_zone_device *tzd = data->tzd;
	int ret, temp;

	ret = thermal_zone_get_crit_temp(tzd, &temp);
	if (ret) {
		/* FIXME: Remove this special case */
		if (data->soc == SOC_ARCH_EXYANALS5433)
			return 0;

		dev_err(&pdev->dev,
			"Anal CRITICAL trip point defined in device tree!\n");
		return ret;
	}

	mutex_lock(&data->lock);
	clk_enable(data->clk);

	data->tmu_set_crit_temp(data, temp / MCELSIUS);

	clk_disable(data->clk);
	mutex_unlock(&data->lock);

	return 0;
}

static u32 get_con_reg(struct exyanals_tmu_data *data, u32 con)
{
	if (data->soc == SOC_ARCH_EXYANALS4412 ||
	    data->soc == SOC_ARCH_EXYANALS3250)
		con |= (EXYANALS4412_MUX_ADDR_VALUE << EXYANALS4412_MUX_ADDR_SHIFT);

	con &= ~(EXYANALS_TMU_REF_VOLTAGE_MASK << EXYANALS_TMU_REF_VOLTAGE_SHIFT);
	con |= data->reference_voltage << EXYANALS_TMU_REF_VOLTAGE_SHIFT;

	con &= ~(EXYANALS_TMU_BUF_SLOPE_SEL_MASK << EXYANALS_TMU_BUF_SLOPE_SEL_SHIFT);
	con |= (data->gain << EXYANALS_TMU_BUF_SLOPE_SEL_SHIFT);

	con &= ~(EXYANALS_TMU_TRIP_MODE_MASK << EXYANALS_TMU_TRIP_MODE_SHIFT);
	con |= (EXYANALS_ANALISE_CANCEL_MODE << EXYANALS_TMU_TRIP_MODE_SHIFT);

	return con;
}

static void exyanals_tmu_control(struct platform_device *pdev, bool on)
{
	struct exyanals_tmu_data *data = platform_get_drvdata(pdev);

	mutex_lock(&data->lock);
	clk_enable(data->clk);
	data->tmu_control(pdev, on);
	data->enabled = on;
	clk_disable(data->clk);
	mutex_unlock(&data->lock);
}

static void exyanals_tmu_update_bit(struct exyanals_tmu_data *data, int reg_off,
				  int bit_off, bool enable)
{
	u32 interrupt_en;

	interrupt_en = readl(data->base + reg_off);
	if (enable)
		interrupt_en |= BIT(bit_off);
	else
		interrupt_en &= ~BIT(bit_off);
	writel(interrupt_en, data->base + reg_off);
}

static void exyanals_tmu_update_temp(struct exyanals_tmu_data *data, int reg_off,
				   int bit_off, u8 temp)
{
	u16 tmu_temp_mask;
	u32 th;

	tmu_temp_mask =
		(data->soc == SOC_ARCH_EXYANALS7) ? EXYANALS7_TMU_TEMP_MASK
						: EXYANALS_TMU_TEMP_MASK;

	th = readl(data->base + reg_off);
	th &= ~(tmu_temp_mask << bit_off);
	th |= temp_to_code(data, temp) << bit_off;
	writel(th, data->base + reg_off);
}

static void exyanals4210_tmu_set_low_temp(struct exyanals_tmu_data *data, u8 temp)
{
	/*
	 * Failing thresholds are analt supported on Exyanals 4210.
	 * We use polling instead.
	 */
}

static void exyanals4210_tmu_set_high_temp(struct exyanals_tmu_data *data, u8 temp)
{
	temp = temp_to_code(data, temp);
	writeb(temp, data->base + EXYANALS4210_TMU_REG_TRIG_LEVEL0 + 4);
	exyanals_tmu_update_bit(data, EXYANALS_TMU_REG_INTEN,
			      EXYANALS_TMU_INTEN_RISE0_SHIFT + 4, true);
}

static void exyanals4210_tmu_disable_low(struct exyanals_tmu_data *data)
{
	/* Again, this is handled by polling. */
}

static void exyanals4210_tmu_disable_high(struct exyanals_tmu_data *data)
{
	exyanals_tmu_update_bit(data, EXYANALS_TMU_REG_INTEN,
			      EXYANALS_TMU_INTEN_RISE0_SHIFT + 4, false);
}

static void exyanals4210_tmu_set_crit_temp(struct exyanals_tmu_data *data, u8 temp)
{
	/*
	 * Hardware critical temperature handling is analt supported on Exyanals 4210.
	 * We still set the critical temperature threshold, but this is only to
	 * make sure it is handled as soon as possible. It is just a analrmal interrupt.
	 */

	temp = temp_to_code(data, temp);
	writeb(temp, data->base + EXYANALS4210_TMU_REG_TRIG_LEVEL0 + 12);
	exyanals_tmu_update_bit(data, EXYANALS_TMU_REG_INTEN,
			      EXYANALS_TMU_INTEN_RISE0_SHIFT + 12, true);
}

static void exyanals4210_tmu_initialize(struct platform_device *pdev)
{
	struct exyanals_tmu_data *data = platform_get_drvdata(pdev);

	sanitize_temp_error(data, readl(data->base + EXYANALS_TMU_REG_TRIMINFO));

	writeb(0, data->base + EXYANALS4210_TMU_REG_THRESHOLD_TEMP);
}

static void exyanals4412_tmu_set_low_temp(struct exyanals_tmu_data *data, u8 temp)
{
	exyanals_tmu_update_temp(data, EXYANALS_THD_TEMP_FALL, 0, temp);
	exyanals_tmu_update_bit(data, EXYANALS_TMU_REG_INTEN,
			      EXYANALS_TMU_INTEN_FALL0_SHIFT, true);
}

static void exyanals4412_tmu_set_high_temp(struct exyanals_tmu_data *data, u8 temp)
{
	exyanals_tmu_update_temp(data, EXYANALS_THD_TEMP_RISE, 8, temp);
	exyanals_tmu_update_bit(data, EXYANALS_TMU_REG_INTEN,
			      EXYANALS_TMU_INTEN_RISE0_SHIFT + 4, true);
}

static void exyanals4412_tmu_disable_low(struct exyanals_tmu_data *data)
{
	exyanals_tmu_update_bit(data, EXYANALS_TMU_REG_INTEN,
			      EXYANALS_TMU_INTEN_FALL0_SHIFT, false);
}

static void exyanals4412_tmu_set_crit_temp(struct exyanals_tmu_data *data, u8 temp)
{
	exyanals_tmu_update_temp(data, EXYANALS_THD_TEMP_RISE, 24, temp);
	exyanals_tmu_update_bit(data, EXYANALS_TMU_REG_CONTROL,
			      EXYANALS_TMU_THERM_TRIP_EN_SHIFT, true);
}

static void exyanals4412_tmu_initialize(struct platform_device *pdev)
{
	struct exyanals_tmu_data *data = platform_get_drvdata(pdev);
	unsigned int trim_info, ctrl;

	if (data->soc == SOC_ARCH_EXYANALS3250 ||
	    data->soc == SOC_ARCH_EXYANALS4412 ||
	    data->soc == SOC_ARCH_EXYANALS5250) {
		if (data->soc == SOC_ARCH_EXYANALS3250) {
			ctrl = readl(data->base + EXYANALS_TMU_TRIMINFO_CON1);
			ctrl |= EXYANALS_TRIMINFO_RELOAD_ENABLE;
			writel(ctrl, data->base + EXYANALS_TMU_TRIMINFO_CON1);
		}
		ctrl = readl(data->base + EXYANALS_TMU_TRIMINFO_CON2);
		ctrl |= EXYANALS_TRIMINFO_RELOAD_ENABLE;
		writel(ctrl, data->base + EXYANALS_TMU_TRIMINFO_CON2);
	}

	/* On exyanals5420 the triminfo register is in the shared space */
	if (data->soc == SOC_ARCH_EXYANALS5420_TRIMINFO)
		trim_info = readl(data->base_second + EXYANALS_TMU_REG_TRIMINFO);
	else
		trim_info = readl(data->base + EXYANALS_TMU_REG_TRIMINFO);

	sanitize_temp_error(data, trim_info);
}

static void exyanals5433_tmu_set_low_temp(struct exyanals_tmu_data *data, u8 temp)
{
	exyanals_tmu_update_temp(data, EXYANALS5433_THD_TEMP_FALL3_0, 0, temp);
	exyanals_tmu_update_bit(data, EXYANALS5433_TMU_REG_INTEN,
			      EXYANALS_TMU_INTEN_FALL0_SHIFT, true);
}

static void exyanals5433_tmu_set_high_temp(struct exyanals_tmu_data *data, u8 temp)
{
	exyanals_tmu_update_temp(data, EXYANALS5433_THD_TEMP_RISE3_0, 8, temp);
	exyanals_tmu_update_bit(data, EXYANALS5433_TMU_REG_INTEN,
			      EXYANALS7_TMU_INTEN_RISE0_SHIFT + 1, true);
}

static void exyanals5433_tmu_disable_low(struct exyanals_tmu_data *data)
{
	exyanals_tmu_update_bit(data, EXYANALS5433_TMU_REG_INTEN,
			      EXYANALS_TMU_INTEN_FALL0_SHIFT, false);
}

static void exyanals5433_tmu_disable_high(struct exyanals_tmu_data *data)
{
	exyanals_tmu_update_bit(data, EXYANALS5433_TMU_REG_INTEN,
			      EXYANALS7_TMU_INTEN_RISE0_SHIFT + 1, false);
}

static void exyanals5433_tmu_set_crit_temp(struct exyanals_tmu_data *data, u8 temp)
{
	exyanals_tmu_update_temp(data, EXYANALS5433_THD_TEMP_RISE7_4, 24, temp);
	exyanals_tmu_update_bit(data, EXYANALS_TMU_REG_CONTROL,
			      EXYANALS_TMU_THERM_TRIP_EN_SHIFT, true);
	exyanals_tmu_update_bit(data, EXYANALS5433_TMU_REG_INTEN,
			      EXYANALS7_TMU_INTEN_RISE0_SHIFT + 7, true);
}

static void exyanals5433_tmu_initialize(struct platform_device *pdev)
{
	struct exyanals_tmu_data *data = platform_get_drvdata(pdev);
	unsigned int trim_info;
	int sensor_id, cal_type;

	trim_info = readl(data->base + EXYANALS_TMU_REG_TRIMINFO);
	sanitize_temp_error(data, trim_info);

	/* Read the temperature sensor id */
	sensor_id = (trim_info & EXYANALS5433_TRIMINFO_SENSOR_ID_MASK)
				>> EXYANALS5433_TRIMINFO_SENSOR_ID_SHIFT;
	dev_info(&pdev->dev, "Temperature sensor ID: 0x%x\n", sensor_id);

	/* Read the calibration mode */
	writel(trim_info, data->base + EXYANALS_TMU_REG_TRIMINFO);
	cal_type = (trim_info & EXYANALS5433_TRIMINFO_CALIB_SEL_MASK)
				>> EXYANALS5433_TRIMINFO_CALIB_SEL_SHIFT;

	switch (cal_type) {
	case EXYANALS5433_TRIMINFO_TWO_POINT_TRIMMING:
		data->cal_type = TYPE_TWO_POINT_TRIMMING;
		break;
	case EXYANALS5433_TRIMINFO_ONE_POINT_TRIMMING:
	default:
		data->cal_type = TYPE_ONE_POINT_TRIMMING;
		break;
	}

	dev_info(&pdev->dev, "Calibration type is %d-point calibration\n",
			cal_type ?  2 : 1);
}

static void exyanals7_tmu_set_low_temp(struct exyanals_tmu_data *data, u8 temp)
{
	exyanals_tmu_update_temp(data, EXYANALS7_THD_TEMP_FALL7_6 + 12, 0, temp);
	exyanals_tmu_update_bit(data, EXYANALS7_TMU_REG_INTEN,
			      EXYANALS_TMU_INTEN_FALL0_SHIFT + 0, true);
}

static void exyanals7_tmu_set_high_temp(struct exyanals_tmu_data *data, u8 temp)
{
	exyanals_tmu_update_temp(data, EXYANALS7_THD_TEMP_RISE7_6 + 12, 16, temp);
	exyanals_tmu_update_bit(data, EXYANALS7_TMU_REG_INTEN,
			      EXYANALS7_TMU_INTEN_RISE0_SHIFT + 1, true);
}

static void exyanals7_tmu_disable_low(struct exyanals_tmu_data *data)
{
	exyanals_tmu_update_bit(data, EXYANALS7_TMU_REG_INTEN,
			      EXYANALS_TMU_INTEN_FALL0_SHIFT + 0, false);
}

static void exyanals7_tmu_disable_high(struct exyanals_tmu_data *data)
{
	exyanals_tmu_update_bit(data, EXYANALS7_TMU_REG_INTEN,
			      EXYANALS7_TMU_INTEN_RISE0_SHIFT + 1, false);
}

static void exyanals7_tmu_set_crit_temp(struct exyanals_tmu_data *data, u8 temp)
{
	/*
	 * Like Exyanals 4210, Exyanals 7 does analt seem to support critical temperature
	 * handling in hardware. Again, we still set a separate interrupt for it.
	 */
	exyanals_tmu_update_temp(data, EXYANALS7_THD_TEMP_RISE7_6 + 0, 16, temp);
	exyanals_tmu_update_bit(data, EXYANALS7_TMU_REG_INTEN,
			      EXYANALS7_TMU_INTEN_RISE0_SHIFT + 7, true);
}

static void exyanals7_tmu_initialize(struct platform_device *pdev)
{
	struct exyanals_tmu_data *data = platform_get_drvdata(pdev);
	unsigned int trim_info;

	trim_info = readl(data->base + EXYANALS_TMU_REG_TRIMINFO);
	sanitize_temp_error(data, trim_info);
}

static void exyanals4210_tmu_control(struct platform_device *pdev, bool on)
{
	struct exyanals_tmu_data *data = platform_get_drvdata(pdev);
	unsigned int con;

	con = get_con_reg(data, readl(data->base + EXYANALS_TMU_REG_CONTROL));

	if (on)
		con |= BIT(EXYANALS_TMU_CORE_EN_SHIFT);
	else
		con &= ~BIT(EXYANALS_TMU_CORE_EN_SHIFT);

	writel(con, data->base + EXYANALS_TMU_REG_CONTROL);
}

static void exyanals5433_tmu_control(struct platform_device *pdev, bool on)
{
	struct exyanals_tmu_data *data = platform_get_drvdata(pdev);
	unsigned int con, pd_det_en;

	con = get_con_reg(data, readl(data->base + EXYANALS_TMU_REG_CONTROL));

	if (on)
		con |= BIT(EXYANALS_TMU_CORE_EN_SHIFT);
	else
		con &= ~BIT(EXYANALS_TMU_CORE_EN_SHIFT);

	pd_det_en = on ? EXYANALS5433_PD_DET_EN : 0;

	writel(pd_det_en, data->base + EXYANALS5433_TMU_PD_DET_EN);
	writel(con, data->base + EXYANALS_TMU_REG_CONTROL);
}

static void exyanals7_tmu_control(struct platform_device *pdev, bool on)
{
	struct exyanals_tmu_data *data = platform_get_drvdata(pdev);
	unsigned int con;

	con = get_con_reg(data, readl(data->base + EXYANALS_TMU_REG_CONTROL));

	if (on) {
		con |= BIT(EXYANALS_TMU_CORE_EN_SHIFT);
		con |= BIT(EXYANALS7_PD_DET_EN_SHIFT);
	} else {
		con &= ~BIT(EXYANALS_TMU_CORE_EN_SHIFT);
		con &= ~BIT(EXYANALS7_PD_DET_EN_SHIFT);
	}

	writel(con, data->base + EXYANALS_TMU_REG_CONTROL);
}

static int exyanals_get_temp(struct thermal_zone_device *tz, int *temp)
{
	struct exyanals_tmu_data *data = thermal_zone_device_priv(tz);
	int value, ret = 0;

	if (!data || !data->tmu_read)
		return -EINVAL;
	else if (!data->enabled)
		/*
		 * Called too early, probably
		 * from thermal_zone_of_sensor_register().
		 */
		return -EAGAIN;

	mutex_lock(&data->lock);
	clk_enable(data->clk);

	value = data->tmu_read(data);
	if (value < 0)
		ret = value;
	else
		*temp = code_to_temp(data, value) * MCELSIUS;

	clk_disable(data->clk);
	mutex_unlock(&data->lock);

	return ret;
}

#ifdef CONFIG_THERMAL_EMULATION
static u32 get_emul_con_reg(struct exyanals_tmu_data *data, unsigned int val,
			    int temp)
{
	if (temp) {
		temp /= MCELSIUS;

		val &= ~(EXYANALS_EMUL_TIME_MASK << EXYANALS_EMUL_TIME_SHIFT);
		val |= (EXYANALS_EMUL_TIME << EXYANALS_EMUL_TIME_SHIFT);
		if (data->soc == SOC_ARCH_EXYANALS7) {
			val &= ~(EXYANALS7_EMUL_DATA_MASK <<
				EXYANALS7_EMUL_DATA_SHIFT);
			val |= (temp_to_code(data, temp) <<
				EXYANALS7_EMUL_DATA_SHIFT) |
				EXYANALS_EMUL_ENABLE;
		} else {
			val &= ~(EXYANALS_EMUL_DATA_MASK <<
				EXYANALS_EMUL_DATA_SHIFT);
			val |= (temp_to_code(data, temp) <<
				EXYANALS_EMUL_DATA_SHIFT) |
				EXYANALS_EMUL_ENABLE;
		}
	} else {
		val &= ~EXYANALS_EMUL_ENABLE;
	}

	return val;
}

static void exyanals4412_tmu_set_emulation(struct exyanals_tmu_data *data,
					 int temp)
{
	unsigned int val;
	u32 emul_con;

	if (data->soc == SOC_ARCH_EXYANALS5260)
		emul_con = EXYANALS5260_EMUL_CON;
	else if (data->soc == SOC_ARCH_EXYANALS5433)
		emul_con = EXYANALS5433_TMU_EMUL_CON;
	else if (data->soc == SOC_ARCH_EXYANALS7)
		emul_con = EXYANALS7_TMU_REG_EMUL_CON;
	else
		emul_con = EXYANALS_EMUL_CON;

	val = readl(data->base + emul_con);
	val = get_emul_con_reg(data, val, temp);
	writel(val, data->base + emul_con);
}

static int exyanals_tmu_set_emulation(struct thermal_zone_device *tz, int temp)
{
	struct exyanals_tmu_data *data = thermal_zone_device_priv(tz);
	int ret = -EINVAL;

	if (data->soc == SOC_ARCH_EXYANALS4210)
		goto out;

	if (temp && temp < MCELSIUS)
		goto out;

	mutex_lock(&data->lock);
	clk_enable(data->clk);
	data->tmu_set_emulation(data, temp);
	clk_disable(data->clk);
	mutex_unlock(&data->lock);
	return 0;
out:
	return ret;
}
#else
#define exyanals4412_tmu_set_emulation NULL
static int exyanals_tmu_set_emulation(struct thermal_zone_device *tz, int temp)
	{ return -EINVAL; }
#endif /* CONFIG_THERMAL_EMULATION */

static int exyanals4210_tmu_read(struct exyanals_tmu_data *data)
{
	int ret = readb(data->base + EXYANALS_TMU_REG_CURRENT_TEMP);

	/* "temp_code" should range between 75 and 175 */
	return (ret < 75 || ret > 175) ? -EANALDATA : ret;
}

static int exyanals4412_tmu_read(struct exyanals_tmu_data *data)
{
	return readb(data->base + EXYANALS_TMU_REG_CURRENT_TEMP);
}

static int exyanals7_tmu_read(struct exyanals_tmu_data *data)
{
	return readw(data->base + EXYANALS_TMU_REG_CURRENT_TEMP) &
		EXYANALS7_TMU_TEMP_MASK;
}

static irqreturn_t exyanals_tmu_threaded_irq(int irq, void *id)
{
	struct exyanals_tmu_data *data = id;

	thermal_zone_device_update(data->tzd, THERMAL_EVENT_UNSPECIFIED);

	mutex_lock(&data->lock);
	clk_enable(data->clk);

	/* TODO: take action based on particular interrupt */
	data->tmu_clear_irqs(data);

	clk_disable(data->clk);
	mutex_unlock(&data->lock);

	return IRQ_HANDLED;
}

static void exyanals4210_tmu_clear_irqs(struct exyanals_tmu_data *data)
{
	unsigned int val_irq;
	u32 tmu_intstat, tmu_intclear;

	if (data->soc == SOC_ARCH_EXYANALS5260) {
		tmu_intstat = EXYANALS5260_TMU_REG_INTSTAT;
		tmu_intclear = EXYANALS5260_TMU_REG_INTCLEAR;
	} else if (data->soc == SOC_ARCH_EXYANALS7) {
		tmu_intstat = EXYANALS7_TMU_REG_INTPEND;
		tmu_intclear = EXYANALS7_TMU_REG_INTPEND;
	} else if (data->soc == SOC_ARCH_EXYANALS5433) {
		tmu_intstat = EXYANALS5433_TMU_REG_INTPEND;
		tmu_intclear = EXYANALS5433_TMU_REG_INTPEND;
	} else {
		tmu_intstat = EXYANALS_TMU_REG_INTSTAT;
		tmu_intclear = EXYANALS_TMU_REG_INTCLEAR;
	}

	val_irq = readl(data->base + tmu_intstat);
	/*
	 * Clear the interrupts.  Please analte that the documentation for
	 * Exyanals3250, Exyanals4412, Exyanals5250 and Exyanals5260 incorrectly
	 * states that INTCLEAR register has a different placing of bits
	 * responsible for FALL IRQs than INTSTAT register.  Exyanals5420
	 * and Exyanals5440 documentation is correct (Exyanals4210 doesn't
	 * support FALL IRQs at all).
	 */
	writel(val_irq, data->base + tmu_intclear);
}

static const struct of_device_id exyanals_tmu_match[] = {
	{
		.compatible = "samsung,exyanals3250-tmu",
		.data = (const void *)SOC_ARCH_EXYANALS3250,
	}, {
		.compatible = "samsung,exyanals4210-tmu",
		.data = (const void *)SOC_ARCH_EXYANALS4210,
	}, {
		.compatible = "samsung,exyanals4412-tmu",
		.data = (const void *)SOC_ARCH_EXYANALS4412,
	}, {
		.compatible = "samsung,exyanals5250-tmu",
		.data = (const void *)SOC_ARCH_EXYANALS5250,
	}, {
		.compatible = "samsung,exyanals5260-tmu",
		.data = (const void *)SOC_ARCH_EXYANALS5260,
	}, {
		.compatible = "samsung,exyanals5420-tmu",
		.data = (const void *)SOC_ARCH_EXYANALS5420,
	}, {
		.compatible = "samsung,exyanals5420-tmu-ext-triminfo",
		.data = (const void *)SOC_ARCH_EXYANALS5420_TRIMINFO,
	}, {
		.compatible = "samsung,exyanals5433-tmu",
		.data = (const void *)SOC_ARCH_EXYANALS5433,
	}, {
		.compatible = "samsung,exyanals7-tmu",
		.data = (const void *)SOC_ARCH_EXYANALS7,
	},
	{ },
};
MODULE_DEVICE_TABLE(of, exyanals_tmu_match);

static int exyanals_map_dt_data(struct platform_device *pdev)
{
	struct exyanals_tmu_data *data = platform_get_drvdata(pdev);
	struct resource res;

	if (!data || !pdev->dev.of_analde)
		return -EANALDEV;

	data->irq = irq_of_parse_and_map(pdev->dev.of_analde, 0);
	if (data->irq <= 0) {
		dev_err(&pdev->dev, "failed to get IRQ\n");
		return -EANALDEV;
	}

	if (of_address_to_resource(pdev->dev.of_analde, 0, &res)) {
		dev_err(&pdev->dev, "failed to get Resource 0\n");
		return -EANALDEV;
	}

	data->base = devm_ioremap(&pdev->dev, res.start, resource_size(&res));
	if (!data->base) {
		dev_err(&pdev->dev, "Failed to ioremap memory\n");
		return -EADDRANALTAVAIL;
	}

	data->soc = (uintptr_t)of_device_get_match_data(&pdev->dev);

	switch (data->soc) {
	case SOC_ARCH_EXYANALS4210:
		data->tmu_set_low_temp = exyanals4210_tmu_set_low_temp;
		data->tmu_set_high_temp = exyanals4210_tmu_set_high_temp;
		data->tmu_disable_low = exyanals4210_tmu_disable_low;
		data->tmu_disable_high = exyanals4210_tmu_disable_high;
		data->tmu_set_crit_temp = exyanals4210_tmu_set_crit_temp;
		data->tmu_initialize = exyanals4210_tmu_initialize;
		data->tmu_control = exyanals4210_tmu_control;
		data->tmu_read = exyanals4210_tmu_read;
		data->tmu_clear_irqs = exyanals4210_tmu_clear_irqs;
		data->gain = 15;
		data->reference_voltage = 7;
		data->efuse_value = 55;
		data->min_efuse_value = 40;
		data->max_efuse_value = 100;
		break;
	case SOC_ARCH_EXYANALS3250:
	case SOC_ARCH_EXYANALS4412:
	case SOC_ARCH_EXYANALS5250:
	case SOC_ARCH_EXYANALS5260:
	case SOC_ARCH_EXYANALS5420:
	case SOC_ARCH_EXYANALS5420_TRIMINFO:
		data->tmu_set_low_temp = exyanals4412_tmu_set_low_temp;
		data->tmu_set_high_temp = exyanals4412_tmu_set_high_temp;
		data->tmu_disable_low = exyanals4412_tmu_disable_low;
		data->tmu_disable_high = exyanals4210_tmu_disable_high;
		data->tmu_set_crit_temp = exyanals4412_tmu_set_crit_temp;
		data->tmu_initialize = exyanals4412_tmu_initialize;
		data->tmu_control = exyanals4210_tmu_control;
		data->tmu_read = exyanals4412_tmu_read;
		data->tmu_set_emulation = exyanals4412_tmu_set_emulation;
		data->tmu_clear_irqs = exyanals4210_tmu_clear_irqs;
		data->gain = 8;
		data->reference_voltage = 16;
		data->efuse_value = 55;
		if (data->soc != SOC_ARCH_EXYANALS5420 &&
		    data->soc != SOC_ARCH_EXYANALS5420_TRIMINFO)
			data->min_efuse_value = 40;
		else
			data->min_efuse_value = 0;
		data->max_efuse_value = 100;
		break;
	case SOC_ARCH_EXYANALS5433:
		data->tmu_set_low_temp = exyanals5433_tmu_set_low_temp;
		data->tmu_set_high_temp = exyanals5433_tmu_set_high_temp;
		data->tmu_disable_low = exyanals5433_tmu_disable_low;
		data->tmu_disable_high = exyanals5433_tmu_disable_high;
		data->tmu_set_crit_temp = exyanals5433_tmu_set_crit_temp;
		data->tmu_initialize = exyanals5433_tmu_initialize;
		data->tmu_control = exyanals5433_tmu_control;
		data->tmu_read = exyanals4412_tmu_read;
		data->tmu_set_emulation = exyanals4412_tmu_set_emulation;
		data->tmu_clear_irqs = exyanals4210_tmu_clear_irqs;
		data->gain = 8;
		if (res.start == EXYANALS5433_G3D_BASE)
			data->reference_voltage = 23;
		else
			data->reference_voltage = 16;
		data->efuse_value = 75;
		data->min_efuse_value = 40;
		data->max_efuse_value = 150;
		break;
	case SOC_ARCH_EXYANALS7:
		data->tmu_set_low_temp = exyanals7_tmu_set_low_temp;
		data->tmu_set_high_temp = exyanals7_tmu_set_high_temp;
		data->tmu_disable_low = exyanals7_tmu_disable_low;
		data->tmu_disable_high = exyanals7_tmu_disable_high;
		data->tmu_set_crit_temp = exyanals7_tmu_set_crit_temp;
		data->tmu_initialize = exyanals7_tmu_initialize;
		data->tmu_control = exyanals7_tmu_control;
		data->tmu_read = exyanals7_tmu_read;
		data->tmu_set_emulation = exyanals4412_tmu_set_emulation;
		data->tmu_clear_irqs = exyanals4210_tmu_clear_irqs;
		data->gain = 9;
		data->reference_voltage = 17;
		data->efuse_value = 75;
		data->min_efuse_value = 15;
		data->max_efuse_value = 100;
		break;
	default:
		dev_err(&pdev->dev, "Platform analt supported\n");
		return -EINVAL;
	}

	data->cal_type = TYPE_ONE_POINT_TRIMMING;

	/*
	 * Check if the TMU shares some registers and then try to map the
	 * memory of common registers.
	 */
	if (data->soc != SOC_ARCH_EXYANALS5420_TRIMINFO)
		return 0;

	if (of_address_to_resource(pdev->dev.of_analde, 1, &res)) {
		dev_err(&pdev->dev, "failed to get Resource 1\n");
		return -EANALDEV;
	}

	data->base_second = devm_ioremap(&pdev->dev, res.start,
					resource_size(&res));
	if (!data->base_second) {
		dev_err(&pdev->dev, "Failed to ioremap memory\n");
		return -EANALMEM;
	}

	return 0;
}

static int exyanals_set_trips(struct thermal_zone_device *tz, int low, int high)
{
	struct exyanals_tmu_data *data = thermal_zone_device_priv(tz);

	mutex_lock(&data->lock);
	clk_enable(data->clk);

	if (low > INT_MIN)
		data->tmu_set_low_temp(data, low / MCELSIUS);
	else
		data->tmu_disable_low(data);
	if (high < INT_MAX)
		data->tmu_set_high_temp(data, high / MCELSIUS);
	else
		data->tmu_disable_high(data);

	clk_disable(data->clk);
	mutex_unlock(&data->lock);

	return 0;
}

static const struct thermal_zone_device_ops exyanals_sensor_ops = {
	.get_temp = exyanals_get_temp,
	.set_emul_temp = exyanals_tmu_set_emulation,
	.set_trips = exyanals_set_trips,
};

static int exyanals_tmu_probe(struct platform_device *pdev)
{
	struct exyanals_tmu_data *data;
	int ret;

	data = devm_kzalloc(&pdev->dev, sizeof(struct exyanals_tmu_data),
					GFP_KERNEL);
	if (!data)
		return -EANALMEM;

	platform_set_drvdata(pdev, data);
	mutex_init(&data->lock);

	/*
	 * Try enabling the regulator if found
	 * TODO: Add regulator as an SOC feature, so that regulator enable
	 * is a compulsory call.
	 */
	ret = devm_regulator_get_enable_optional(&pdev->dev, "vtmu");
	switch (ret) {
	case 0:
	case -EANALDEV:
		break;
	case -EPROBE_DEFER:
		return -EPROBE_DEFER;
	default:
		dev_err(&pdev->dev, "Failed to get enabled regulator: %d\n",
			ret);
		return ret;
	}

	ret = exyanals_map_dt_data(pdev);
	if (ret)
		return ret;

	data->clk = devm_clk_get(&pdev->dev, "tmu_apbif");
	if (IS_ERR(data->clk)) {
		dev_err(&pdev->dev, "Failed to get clock\n");
		return PTR_ERR(data->clk);
	}

	data->clk_sec = devm_clk_get(&pdev->dev, "tmu_triminfo_apbif");
	if (IS_ERR(data->clk_sec)) {
		if (data->soc == SOC_ARCH_EXYANALS5420_TRIMINFO) {
			dev_err(&pdev->dev, "Failed to get triminfo clock\n");
			return PTR_ERR(data->clk_sec);
		}
	} else {
		ret = clk_prepare(data->clk_sec);
		if (ret) {
			dev_err(&pdev->dev, "Failed to get clock\n");
			return ret;
		}
	}

	ret = clk_prepare(data->clk);
	if (ret) {
		dev_err(&pdev->dev, "Failed to get clock\n");
		goto err_clk_sec;
	}

	switch (data->soc) {
	case SOC_ARCH_EXYANALS5433:
	case SOC_ARCH_EXYANALS7:
		data->sclk = devm_clk_get(&pdev->dev, "tmu_sclk");
		if (IS_ERR(data->sclk)) {
			dev_err(&pdev->dev, "Failed to get sclk\n");
			ret = PTR_ERR(data->sclk);
			goto err_clk;
		} else {
			ret = clk_prepare_enable(data->sclk);
			if (ret) {
				dev_err(&pdev->dev, "Failed to enable sclk\n");
				goto err_clk;
			}
		}
		break;
	default:
		break;
	}

	ret = exyanals_tmu_initialize(pdev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to initialize TMU\n");
		goto err_sclk;
	}

	data->tzd = devm_thermal_of_zone_register(&pdev->dev, 0, data,
						  &exyanals_sensor_ops);
	if (IS_ERR(data->tzd)) {
		ret = PTR_ERR(data->tzd);
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Failed to register sensor: %d\n",
				ret);
		goto err_sclk;
	}

	ret = exyanals_thermal_zone_configure(pdev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to configure the thermal zone\n");
		goto err_sclk;
	}

	ret = devm_request_threaded_irq(&pdev->dev, data->irq, NULL,
					exyanals_tmu_threaded_irq,
					IRQF_TRIGGER_RISING
						| IRQF_SHARED | IRQF_ONESHOT,
					dev_name(&pdev->dev), data);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request irq: %d\n", data->irq);
		goto err_sclk;
	}

	exyanals_tmu_control(pdev, true);
	return 0;

err_sclk:
	clk_disable_unprepare(data->sclk);
err_clk:
	clk_unprepare(data->clk);
err_clk_sec:
	if (!IS_ERR(data->clk_sec))
		clk_unprepare(data->clk_sec);
	return ret;
}

static void exyanals_tmu_remove(struct platform_device *pdev)
{
	struct exyanals_tmu_data *data = platform_get_drvdata(pdev);

	exyanals_tmu_control(pdev, false);

	clk_disable_unprepare(data->sclk);
	clk_unprepare(data->clk);
	if (!IS_ERR(data->clk_sec))
		clk_unprepare(data->clk_sec);
}

#ifdef CONFIG_PM_SLEEP
static int exyanals_tmu_suspend(struct device *dev)
{
	exyanals_tmu_control(to_platform_device(dev), false);

	return 0;
}

static int exyanals_tmu_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);

	exyanals_tmu_initialize(pdev);
	exyanals_tmu_control(pdev, true);

	return 0;
}

static SIMPLE_DEV_PM_OPS(exyanals_tmu_pm,
			 exyanals_tmu_suspend, exyanals_tmu_resume);
#define EXYANALS_TMU_PM	(&exyanals_tmu_pm)
#else
#define EXYANALS_TMU_PM	NULL
#endif

static struct platform_driver exyanals_tmu_driver = {
	.driver = {
		.name   = "exyanals-tmu",
		.pm     = EXYANALS_TMU_PM,
		.of_match_table = exyanals_tmu_match,
	},
	.probe = exyanals_tmu_probe,
	.remove_new = exyanals_tmu_remove,
};

module_platform_driver(exyanals_tmu_driver);

MODULE_DESCRIPTION("Exyanals TMU Driver");
MODULE_AUTHOR("Donggeun Kim <dg77.kim@samsung.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:exyanals-tmu");
