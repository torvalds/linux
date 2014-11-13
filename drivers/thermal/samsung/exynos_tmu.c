/*
 * exynos_tmu.c - Samsung EXYNOS TMU (Thermal Management Unit)
 *
 *  Copyright (C) 2011 Samsung Electronics
 *  Donggeun Kim <dg77.kim@samsung.com>
 *  Amit Daniel Kachhap <amit.kachhap@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
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

#include "exynos_thermal_common.h"
#include "exynos_tmu.h"

/* Exynos generic registers */
#define EXYNOS_TMU_REG_TRIMINFO		0x0
#define EXYNOS_TMU_REG_CONTROL		0x20
#define EXYNOS_TMU_REG_STATUS		0x28
#define EXYNOS_TMU_REG_CURRENT_TEMP	0x40
#define EXYNOS_TMU_REG_INTEN		0x70
#define EXYNOS_TMU_REG_INTSTAT		0x74
#define EXYNOS_TMU_REG_INTCLEAR		0x78

#define EXYNOS_TMU_TEMP_MASK		0xff
#define EXYNOS_TMU_REF_VOLTAGE_SHIFT	24
#define EXYNOS_TMU_REF_VOLTAGE_MASK	0x1f
#define EXYNOS_TMU_BUF_SLOPE_SEL_MASK	0xf
#define EXYNOS_TMU_BUF_SLOPE_SEL_SHIFT	8
#define EXYNOS_TMU_CORE_EN_SHIFT	0

/* Exynos3250 specific registers */
#define EXYNOS_TMU_TRIMINFO_CON1	0x10

/* Exynos4210 specific registers */
#define EXYNOS4210_TMU_REG_THRESHOLD_TEMP	0x44
#define EXYNOS4210_TMU_REG_TRIG_LEVEL0	0x50

/* Exynos5250, Exynos4412, Exynos3250 specific registers */
#define EXYNOS_TMU_TRIMINFO_CON2	0x14
#define EXYNOS_THD_TEMP_RISE		0x50
#define EXYNOS_THD_TEMP_FALL		0x54
#define EXYNOS_EMUL_CON		0x80

#define EXYNOS_TRIMINFO_RELOAD_ENABLE	1
#define EXYNOS_TRIMINFO_25_SHIFT	0
#define EXYNOS_TRIMINFO_85_SHIFT	8
#define EXYNOS_TMU_TRIP_MODE_SHIFT	13
#define EXYNOS_TMU_TRIP_MODE_MASK	0x7
#define EXYNOS_TMU_THERM_TRIP_EN_SHIFT	12

#define EXYNOS_TMU_INTEN_RISE0_SHIFT	0
#define EXYNOS_TMU_INTEN_RISE1_SHIFT	4
#define EXYNOS_TMU_INTEN_RISE2_SHIFT	8
#define EXYNOS_TMU_INTEN_RISE3_SHIFT	12
#define EXYNOS_TMU_INTEN_FALL0_SHIFT	16

#define EXYNOS_EMUL_TIME	0x57F0
#define EXYNOS_EMUL_TIME_MASK	0xffff
#define EXYNOS_EMUL_TIME_SHIFT	16
#define EXYNOS_EMUL_DATA_SHIFT	8
#define EXYNOS_EMUL_DATA_MASK	0xFF
#define EXYNOS_EMUL_ENABLE	0x1

/* Exynos5260 specific */
#define EXYNOS5260_TMU_REG_INTEN		0xC0
#define EXYNOS5260_TMU_REG_INTSTAT		0xC4
#define EXYNOS5260_TMU_REG_INTCLEAR		0xC8
#define EXYNOS5260_EMUL_CON			0x100

/* Exynos4412 specific */
#define EXYNOS4412_MUX_ADDR_VALUE          6
#define EXYNOS4412_MUX_ADDR_SHIFT          20

/*exynos5440 specific registers*/
#define EXYNOS5440_TMU_S0_7_TRIM		0x000
#define EXYNOS5440_TMU_S0_7_CTRL		0x020
#define EXYNOS5440_TMU_S0_7_DEBUG		0x040
#define EXYNOS5440_TMU_S0_7_TEMP		0x0f0
#define EXYNOS5440_TMU_S0_7_TH0			0x110
#define EXYNOS5440_TMU_S0_7_TH1			0x130
#define EXYNOS5440_TMU_S0_7_TH2			0x150
#define EXYNOS5440_TMU_S0_7_IRQEN		0x210
#define EXYNOS5440_TMU_S0_7_IRQ			0x230
/* exynos5440 common registers */
#define EXYNOS5440_TMU_IRQ_STATUS		0x000
#define EXYNOS5440_TMU_PMIN			0x004

#define EXYNOS5440_TMU_INTEN_RISE0_SHIFT	0
#define EXYNOS5440_TMU_INTEN_RISE1_SHIFT	1
#define EXYNOS5440_TMU_INTEN_RISE2_SHIFT	2
#define EXYNOS5440_TMU_INTEN_RISE3_SHIFT	3
#define EXYNOS5440_TMU_INTEN_FALL0_SHIFT	4
#define EXYNOS5440_TMU_TH_RISE4_SHIFT		24
#define EXYNOS5440_EFUSE_SWAP_OFFSET		8

/**
 * struct exynos_tmu_data : A structure to hold the private data of the TMU
	driver
 * @id: identifier of the one instance of the TMU controller.
 * @pdata: pointer to the tmu platform/configuration data
 * @base: base address of the single instance of the TMU controller.
 * @base_second: base address of the common registers of the TMU controller.
 * @irq: irq number of the TMU controller.
 * @soc: id of the SOC type.
 * @irq_work: pointer to the irq work structure.
 * @lock: lock to implement synchronization.
 * @clk: pointer to the clock structure.
 * @clk_sec: pointer to the clock structure for accessing the base_second.
 * @temp_error1: fused value of the first point trim.
 * @temp_error2: fused value of the second point trim.
 * @regulator: pointer to the TMU regulator structure.
 * @reg_conf: pointer to structure to register with core thermal.
 * @tmu_initialize: SoC specific TMU initialization method
 * @tmu_control: SoC specific TMU control method
 * @tmu_read: SoC specific TMU temperature read method
 * @tmu_set_emulation: SoC specific TMU emulation setting method
 * @tmu_clear_irqs: SoC specific TMU interrupts clearing method
 */
struct exynos_tmu_data {
	int id;
	struct exynos_tmu_platform_data *pdata;
	void __iomem *base;
	void __iomem *base_second;
	int irq;
	enum soc_type soc;
	struct work_struct irq_work;
	struct mutex lock;
	struct clk *clk, *clk_sec;
	u8 temp_error1, temp_error2;
	struct regulator *regulator;
	struct thermal_sensor_conf *reg_conf;
	int (*tmu_initialize)(struct platform_device *pdev);
	void (*tmu_control)(struct platform_device *pdev, bool on);
	int (*tmu_read)(struct exynos_tmu_data *data);
	void (*tmu_set_emulation)(struct exynos_tmu_data *data,
				  unsigned long temp);
	void (*tmu_clear_irqs)(struct exynos_tmu_data *data);
};

/*
 * TMU treats temperature as a mapped temperature code.
 * The temperature is converted differently depending on the calibration type.
 */
static int temp_to_code(struct exynos_tmu_data *data, u8 temp)
{
	struct exynos_tmu_platform_data *pdata = data->pdata;
	int temp_code;

	switch (pdata->cal_type) {
	case TYPE_TWO_POINT_TRIMMING:
		temp_code = (temp - pdata->first_point_trim) *
			(data->temp_error2 - data->temp_error1) /
			(pdata->second_point_trim - pdata->first_point_trim) +
			data->temp_error1;
		break;
	case TYPE_ONE_POINT_TRIMMING:
		temp_code = temp + data->temp_error1 - pdata->first_point_trim;
		break;
	default:
		temp_code = temp + pdata->default_temp_offset;
		break;
	}

	return temp_code;
}

/*
 * Calculate a temperature value from a temperature code.
 * The unit of the temperature is degree Celsius.
 */
static int code_to_temp(struct exynos_tmu_data *data, u8 temp_code)
{
	struct exynos_tmu_platform_data *pdata = data->pdata;
	int temp;

	switch (pdata->cal_type) {
	case TYPE_TWO_POINT_TRIMMING:
		temp = (temp_code - data->temp_error1) *
			(pdata->second_point_trim - pdata->first_point_trim) /
			(data->temp_error2 - data->temp_error1) +
			pdata->first_point_trim;
		break;
	case TYPE_ONE_POINT_TRIMMING:
		temp = temp_code - data->temp_error1 + pdata->first_point_trim;
		break;
	default:
		temp = temp_code - pdata->default_temp_offset;
		break;
	}

	return temp;
}

static void sanitize_temp_error(struct exynos_tmu_data *data, u32 trim_info)
{
	struct exynos_tmu_platform_data *pdata = data->pdata;

	data->temp_error1 = trim_info & EXYNOS_TMU_TEMP_MASK;
	data->temp_error2 = ((trim_info >> EXYNOS_TRIMINFO_85_SHIFT) &
				EXYNOS_TMU_TEMP_MASK);

	if (!data->temp_error1 ||
		(pdata->min_efuse_value > data->temp_error1) ||
		(data->temp_error1 > pdata->max_efuse_value))
		data->temp_error1 = pdata->efuse_value & EXYNOS_TMU_TEMP_MASK;

	if (!data->temp_error2)
		data->temp_error2 =
			(pdata->efuse_value >> EXYNOS_TRIMINFO_85_SHIFT) &
			EXYNOS_TMU_TEMP_MASK;
}

static u32 get_th_reg(struct exynos_tmu_data *data, u32 threshold, bool falling)
{
	struct exynos_tmu_platform_data *pdata = data->pdata;
	int i;

	for (i = 0; i < pdata->non_hw_trigger_levels; i++) {
		u8 temp = pdata->trigger_levels[i];

		if (falling)
			temp -= pdata->threshold_falling;
		else
			threshold &= ~(0xff << 8 * i);

		threshold |= temp_to_code(data, temp) << 8 * i;
	}

	return threshold;
}

static int exynos_tmu_initialize(struct platform_device *pdev)
{
	struct exynos_tmu_data *data = platform_get_drvdata(pdev);
	int ret;

	mutex_lock(&data->lock);
	clk_enable(data->clk);
	if (!IS_ERR(data->clk_sec))
		clk_enable(data->clk_sec);
	ret = data->tmu_initialize(pdev);
	clk_disable(data->clk);
	mutex_unlock(&data->lock);
	if (!IS_ERR(data->clk_sec))
		clk_disable(data->clk_sec);

	return ret;
}

static u32 get_con_reg(struct exynos_tmu_data *data, u32 con)
{
	struct exynos_tmu_platform_data *pdata = data->pdata;

	if (data->soc == SOC_ARCH_EXYNOS4412 ||
	    data->soc == SOC_ARCH_EXYNOS3250)
		con |= (EXYNOS4412_MUX_ADDR_VALUE << EXYNOS4412_MUX_ADDR_SHIFT);

	con &= ~(EXYNOS_TMU_REF_VOLTAGE_MASK << EXYNOS_TMU_REF_VOLTAGE_SHIFT);
	con |= pdata->reference_voltage << EXYNOS_TMU_REF_VOLTAGE_SHIFT;

	con &= ~(EXYNOS_TMU_BUF_SLOPE_SEL_MASK << EXYNOS_TMU_BUF_SLOPE_SEL_SHIFT);
	con |= (pdata->gain << EXYNOS_TMU_BUF_SLOPE_SEL_SHIFT);

	if (pdata->noise_cancel_mode) {
		con &= ~(EXYNOS_TMU_TRIP_MODE_MASK << EXYNOS_TMU_TRIP_MODE_SHIFT);
		con |= (pdata->noise_cancel_mode << EXYNOS_TMU_TRIP_MODE_SHIFT);
	}

	return con;
}

static void exynos_tmu_control(struct platform_device *pdev, bool on)
{
	struct exynos_tmu_data *data = platform_get_drvdata(pdev);

	mutex_lock(&data->lock);
	clk_enable(data->clk);
	data->tmu_control(pdev, on);
	clk_disable(data->clk);
	mutex_unlock(&data->lock);
}

static int exynos4210_tmu_initialize(struct platform_device *pdev)
{
	struct exynos_tmu_data *data = platform_get_drvdata(pdev);
	struct exynos_tmu_platform_data *pdata = data->pdata;
	unsigned int status;
	int ret = 0, threshold_code, i;

	status = readb(data->base + EXYNOS_TMU_REG_STATUS);
	if (!status) {
		ret = -EBUSY;
		goto out;
	}

	sanitize_temp_error(data, readl(data->base + EXYNOS_TMU_REG_TRIMINFO));

	/* Write temperature code for threshold */
	threshold_code = temp_to_code(data, pdata->threshold);
	writeb(threshold_code, data->base + EXYNOS4210_TMU_REG_THRESHOLD_TEMP);

	for (i = 0; i < pdata->non_hw_trigger_levels; i++)
		writeb(pdata->trigger_levels[i], data->base +
		       EXYNOS4210_TMU_REG_TRIG_LEVEL0 + i * 4);

	data->tmu_clear_irqs(data);
out:
	return ret;
}

static int exynos4412_tmu_initialize(struct platform_device *pdev)
{
	struct exynos_tmu_data *data = platform_get_drvdata(pdev);
	struct exynos_tmu_platform_data *pdata = data->pdata;
	unsigned int status, trim_info, con, ctrl, rising_threshold;
	int ret = 0, threshold_code, i;

	status = readb(data->base + EXYNOS_TMU_REG_STATUS);
	if (!status) {
		ret = -EBUSY;
		goto out;
	}

	if (data->soc == SOC_ARCH_EXYNOS3250 ||
	    data->soc == SOC_ARCH_EXYNOS4412 ||
	    data->soc == SOC_ARCH_EXYNOS5250) {
		if (data->soc == SOC_ARCH_EXYNOS3250) {
			ctrl = readl(data->base + EXYNOS_TMU_TRIMINFO_CON1);
			ctrl |= EXYNOS_TRIMINFO_RELOAD_ENABLE;
			writel(ctrl, data->base + EXYNOS_TMU_TRIMINFO_CON1);
		}
		ctrl = readl(data->base + EXYNOS_TMU_TRIMINFO_CON2);
		ctrl |= EXYNOS_TRIMINFO_RELOAD_ENABLE;
		writel(ctrl, data->base + EXYNOS_TMU_TRIMINFO_CON2);
	}

	/* On exynos5420 the triminfo register is in the shared space */
	if (data->soc == SOC_ARCH_EXYNOS5420_TRIMINFO)
		trim_info = readl(data->base_second + EXYNOS_TMU_REG_TRIMINFO);
	else
		trim_info = readl(data->base + EXYNOS_TMU_REG_TRIMINFO);

	sanitize_temp_error(data, trim_info);

	/* Write temperature code for rising and falling threshold */
	rising_threshold = readl(data->base + EXYNOS_THD_TEMP_RISE);
	rising_threshold = get_th_reg(data, rising_threshold, false);
	writel(rising_threshold, data->base + EXYNOS_THD_TEMP_RISE);
	writel(get_th_reg(data, 0, true), data->base + EXYNOS_THD_TEMP_FALL);

	data->tmu_clear_irqs(data);

	/* if last threshold limit is also present */
	i = pdata->max_trigger_level - 1;
	if (pdata->trigger_levels[i] && pdata->trigger_type[i] == HW_TRIP) {
		threshold_code = temp_to_code(data, pdata->trigger_levels[i]);
		/* 1-4 level to be assigned in th0 reg */
		rising_threshold &= ~(0xff << 8 * i);
		rising_threshold |= threshold_code << 8 * i;
		writel(rising_threshold, data->base + EXYNOS_THD_TEMP_RISE);
		con = readl(data->base + EXYNOS_TMU_REG_CONTROL);
		con |= (1 << EXYNOS_TMU_THERM_TRIP_EN_SHIFT);
		writel(con, data->base + EXYNOS_TMU_REG_CONTROL);
	}
out:
	return ret;
}

static int exynos5440_tmu_initialize(struct platform_device *pdev)
{
	struct exynos_tmu_data *data = platform_get_drvdata(pdev);
	struct exynos_tmu_platform_data *pdata = data->pdata;
	unsigned int trim_info = 0, con, rising_threshold;
	int ret = 0, threshold_code, i;

	/*
	 * For exynos5440 soc triminfo value is swapped between TMU0 and
	 * TMU2, so the below logic is needed.
	 */
	switch (data->id) {
	case 0:
		trim_info = readl(data->base + EXYNOS5440_EFUSE_SWAP_OFFSET +
				 EXYNOS5440_TMU_S0_7_TRIM);
		break;
	case 1:
		trim_info = readl(data->base + EXYNOS5440_TMU_S0_7_TRIM);
		break;
	case 2:
		trim_info = readl(data->base - EXYNOS5440_EFUSE_SWAP_OFFSET +
				  EXYNOS5440_TMU_S0_7_TRIM);
	}
	sanitize_temp_error(data, trim_info);

	/* Write temperature code for rising and falling threshold */
	rising_threshold = readl(data->base + EXYNOS5440_TMU_S0_7_TH0);
	rising_threshold = get_th_reg(data, rising_threshold, false);
	writel(rising_threshold, data->base + EXYNOS5440_TMU_S0_7_TH0);
	writel(0, data->base + EXYNOS5440_TMU_S0_7_TH1);

	data->tmu_clear_irqs(data);

	/* if last threshold limit is also present */
	i = pdata->max_trigger_level - 1;
	if (pdata->trigger_levels[i] && pdata->trigger_type[i] == HW_TRIP) {
		threshold_code = temp_to_code(data, pdata->trigger_levels[i]);
		/* 5th level to be assigned in th2 reg */
		rising_threshold =
			threshold_code << EXYNOS5440_TMU_TH_RISE4_SHIFT;
		writel(rising_threshold, data->base + EXYNOS5440_TMU_S0_7_TH2);
		con = readl(data->base + EXYNOS5440_TMU_S0_7_CTRL);
		con |= (1 << EXYNOS_TMU_THERM_TRIP_EN_SHIFT);
		writel(con, data->base + EXYNOS5440_TMU_S0_7_CTRL);
	}
	/* Clear the PMIN in the common TMU register */
	if (!data->id)
		writel(0, data->base_second + EXYNOS5440_TMU_PMIN);
	return ret;
}

static void exynos4210_tmu_control(struct platform_device *pdev, bool on)
{
	struct exynos_tmu_data *data = platform_get_drvdata(pdev);
	struct exynos_tmu_platform_data *pdata = data->pdata;
	unsigned int con, interrupt_en;

	con = get_con_reg(data, readl(data->base + EXYNOS_TMU_REG_CONTROL));

	if (on) {
		con |= (1 << EXYNOS_TMU_CORE_EN_SHIFT);
		interrupt_en =
			pdata->trigger_enable[3] << EXYNOS_TMU_INTEN_RISE3_SHIFT |
			pdata->trigger_enable[2] << EXYNOS_TMU_INTEN_RISE2_SHIFT |
			pdata->trigger_enable[1] << EXYNOS_TMU_INTEN_RISE1_SHIFT |
			pdata->trigger_enable[0] << EXYNOS_TMU_INTEN_RISE0_SHIFT;
		if (data->soc != SOC_ARCH_EXYNOS4210)
			interrupt_en |=
				interrupt_en << EXYNOS_TMU_INTEN_FALL0_SHIFT;
	} else {
		con &= ~(1 << EXYNOS_TMU_CORE_EN_SHIFT);
		interrupt_en = 0; /* Disable all interrupts */
	}
	writel(interrupt_en, data->base + EXYNOS_TMU_REG_INTEN);
	writel(con, data->base + EXYNOS_TMU_REG_CONTROL);
}

static void exynos5440_tmu_control(struct platform_device *pdev, bool on)
{
	struct exynos_tmu_data *data = platform_get_drvdata(pdev);
	struct exynos_tmu_platform_data *pdata = data->pdata;
	unsigned int con, interrupt_en;

	con = get_con_reg(data, readl(data->base + EXYNOS5440_TMU_S0_7_CTRL));

	if (on) {
		con |= (1 << EXYNOS_TMU_CORE_EN_SHIFT);
		interrupt_en =
			pdata->trigger_enable[3] << EXYNOS5440_TMU_INTEN_RISE3_SHIFT |
			pdata->trigger_enable[2] << EXYNOS5440_TMU_INTEN_RISE2_SHIFT |
			pdata->trigger_enable[1] << EXYNOS5440_TMU_INTEN_RISE1_SHIFT |
			pdata->trigger_enable[0] << EXYNOS5440_TMU_INTEN_RISE0_SHIFT;
		interrupt_en |= interrupt_en << EXYNOS5440_TMU_INTEN_FALL0_SHIFT;
	} else {
		con &= ~(1 << EXYNOS_TMU_CORE_EN_SHIFT);
		interrupt_en = 0; /* Disable all interrupts */
	}
	writel(interrupt_en, data->base + EXYNOS5440_TMU_S0_7_IRQEN);
	writel(con, data->base + EXYNOS5440_TMU_S0_7_CTRL);
}

static int exynos_tmu_read(struct exynos_tmu_data *data)
{
	int ret;

	mutex_lock(&data->lock);
	clk_enable(data->clk);
	ret = data->tmu_read(data);
	if (ret >= 0)
		ret = code_to_temp(data, ret);
	clk_disable(data->clk);
	mutex_unlock(&data->lock);

	return ret;
}

#ifdef CONFIG_THERMAL_EMULATION
static u32 get_emul_con_reg(struct exynos_tmu_data *data, unsigned int val,
			    unsigned long temp)
{
	if (temp) {
		temp /= MCELSIUS;

		if (data->soc != SOC_ARCH_EXYNOS5440) {
			val &= ~(EXYNOS_EMUL_TIME_MASK << EXYNOS_EMUL_TIME_SHIFT);
			val |= (EXYNOS_EMUL_TIME << EXYNOS_EMUL_TIME_SHIFT);
		}
		val &= ~(EXYNOS_EMUL_DATA_MASK << EXYNOS_EMUL_DATA_SHIFT);
		val |= (temp_to_code(data, temp) << EXYNOS_EMUL_DATA_SHIFT) |
			EXYNOS_EMUL_ENABLE;
	} else {
		val &= ~EXYNOS_EMUL_ENABLE;
	}

	return val;
}

static void exynos4412_tmu_set_emulation(struct exynos_tmu_data *data,
					 unsigned long temp)
{
	unsigned int val;
	u32 emul_con;

	if (data->soc == SOC_ARCH_EXYNOS5260)
		emul_con = EXYNOS5260_EMUL_CON;
	else
		emul_con = EXYNOS_EMUL_CON;

	val = readl(data->base + emul_con);
	val = get_emul_con_reg(data, val, temp);
	writel(val, data->base + emul_con);
}

static void exynos5440_tmu_set_emulation(struct exynos_tmu_data *data,
					 unsigned long temp)
{
	unsigned int val;

	val = readl(data->base + EXYNOS5440_TMU_S0_7_DEBUG);
	val = get_emul_con_reg(data, val, temp);
	writel(val, data->base + EXYNOS5440_TMU_S0_7_DEBUG);
}

static int exynos_tmu_set_emulation(void *drv_data, unsigned long temp)
{
	struct exynos_tmu_data *data = drv_data;
	int ret = -EINVAL;

	if (data->soc == SOC_ARCH_EXYNOS4210)
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
#define exynos4412_tmu_set_emulation NULL
#define exynos5440_tmu_set_emulation NULL
static int exynos_tmu_set_emulation(void *drv_data,	unsigned long temp)
	{ return -EINVAL; }
#endif/*CONFIG_THERMAL_EMULATION*/

static int exynos4210_tmu_read(struct exynos_tmu_data *data)
{
	int ret = readb(data->base + EXYNOS_TMU_REG_CURRENT_TEMP);

	/* "temp_code" should range between 75 and 175 */
	return (ret < 75 || ret > 175) ? -ENODATA : ret;
}

static int exynos4412_tmu_read(struct exynos_tmu_data *data)
{
	return readb(data->base + EXYNOS_TMU_REG_CURRENT_TEMP);
}

static int exynos5440_tmu_read(struct exynos_tmu_data *data)
{
	return readb(data->base + EXYNOS5440_TMU_S0_7_TEMP);
}

static void exynos_tmu_work(struct work_struct *work)
{
	struct exynos_tmu_data *data = container_of(work,
			struct exynos_tmu_data, irq_work);
	unsigned int val_type;

	if (!IS_ERR(data->clk_sec))
		clk_enable(data->clk_sec);
	/* Find which sensor generated this interrupt */
	if (data->soc == SOC_ARCH_EXYNOS5440) {
		val_type = readl(data->base_second + EXYNOS5440_TMU_IRQ_STATUS);
		if (!((val_type >> data->id) & 0x1))
			goto out;
	}
	if (!IS_ERR(data->clk_sec))
		clk_disable(data->clk_sec);

	exynos_report_trigger(data->reg_conf);
	mutex_lock(&data->lock);
	clk_enable(data->clk);

	/* TODO: take action based on particular interrupt */
	data->tmu_clear_irqs(data);

	clk_disable(data->clk);
	mutex_unlock(&data->lock);
out:
	enable_irq(data->irq);
}

static void exynos4210_tmu_clear_irqs(struct exynos_tmu_data *data)
{
	unsigned int val_irq;
	u32 tmu_intstat, tmu_intclear;

	if (data->soc == SOC_ARCH_EXYNOS5260) {
		tmu_intstat = EXYNOS5260_TMU_REG_INTSTAT;
		tmu_intclear = EXYNOS5260_TMU_REG_INTCLEAR;
	} else {
		tmu_intstat = EXYNOS_TMU_REG_INTSTAT;
		tmu_intclear = EXYNOS_TMU_REG_INTCLEAR;
	}

	val_irq = readl(data->base + tmu_intstat);
	/*
	 * Clear the interrupts.  Please note that the documentation for
	 * Exynos3250, Exynos4412, Exynos5250 and Exynos5260 incorrectly
	 * states that INTCLEAR register has a different placing of bits
	 * responsible for FALL IRQs than INTSTAT register.  Exynos5420
	 * and Exynos5440 documentation is correct (Exynos4210 doesn't
	 * support FALL IRQs at all).
	 */
	writel(val_irq, data->base + tmu_intclear);
}

static void exynos5440_tmu_clear_irqs(struct exynos_tmu_data *data)
{
	unsigned int val_irq;

	val_irq = readl(data->base + EXYNOS5440_TMU_S0_7_IRQ);
	/* clear the interrupts */
	writel(val_irq, data->base + EXYNOS5440_TMU_S0_7_IRQ);
}

static irqreturn_t exynos_tmu_irq(int irq, void *id)
{
	struct exynos_tmu_data *data = id;

	disable_irq_nosync(irq);
	schedule_work(&data->irq_work);

	return IRQ_HANDLED;
}

static const struct of_device_id exynos_tmu_match[] = {
	{
		.compatible = "samsung,exynos3250-tmu",
		.data = &exynos3250_default_tmu_data,
	},
	{
		.compatible = "samsung,exynos4210-tmu",
		.data = &exynos4210_default_tmu_data,
	},
	{
		.compatible = "samsung,exynos4412-tmu",
		.data = &exynos4412_default_tmu_data,
	},
	{
		.compatible = "samsung,exynos5250-tmu",
		.data = &exynos5250_default_tmu_data,
	},
	{
		.compatible = "samsung,exynos5260-tmu",
		.data = &exynos5260_default_tmu_data,
	},
	{
		.compatible = "samsung,exynos5420-tmu",
		.data = &exynos5420_default_tmu_data,
	},
	{
		.compatible = "samsung,exynos5420-tmu-ext-triminfo",
		.data = &exynos5420_default_tmu_data,
	},
	{
		.compatible = "samsung,exynos5440-tmu",
		.data = &exynos5440_default_tmu_data,
	},
	{},
};
MODULE_DEVICE_TABLE(of, exynos_tmu_match);

static inline struct  exynos_tmu_platform_data *exynos_get_driver_data(
			struct platform_device *pdev, int id)
{
	struct  exynos_tmu_init_data *data_table;
	struct exynos_tmu_platform_data *tmu_data;
	const struct of_device_id *match;

	match = of_match_node(exynos_tmu_match, pdev->dev.of_node);
	if (!match)
		return NULL;
	data_table = (struct exynos_tmu_init_data *) match->data;
	if (!data_table || id >= data_table->tmu_count)
		return NULL;
	tmu_data = data_table->tmu_data;
	return (struct exynos_tmu_platform_data *) (tmu_data + id);
}

static int exynos_map_dt_data(struct platform_device *pdev)
{
	struct exynos_tmu_data *data = platform_get_drvdata(pdev);
	struct exynos_tmu_platform_data *pdata;
	struct resource res;
	int ret;

	if (!data || !pdev->dev.of_node)
		return -ENODEV;

	/*
	 * Try enabling the regulator if found
	 * TODO: Add regulator as an SOC feature, so that regulator enable
	 * is a compulsory call.
	 */
	data->regulator = devm_regulator_get(&pdev->dev, "vtmu");
	if (!IS_ERR(data->regulator)) {
		ret = regulator_enable(data->regulator);
		if (ret) {
			dev_err(&pdev->dev, "failed to enable vtmu\n");
			return ret;
		}
	} else {
		dev_info(&pdev->dev, "Regulator node (vtmu) not found\n");
	}

	data->id = of_alias_get_id(pdev->dev.of_node, "tmuctrl");
	if (data->id < 0)
		data->id = 0;

	data->irq = irq_of_parse_and_map(pdev->dev.of_node, 0);
	if (data->irq <= 0) {
		dev_err(&pdev->dev, "failed to get IRQ\n");
		return -ENODEV;
	}

	if (of_address_to_resource(pdev->dev.of_node, 0, &res)) {
		dev_err(&pdev->dev, "failed to get Resource 0\n");
		return -ENODEV;
	}

	data->base = devm_ioremap(&pdev->dev, res.start, resource_size(&res));
	if (!data->base) {
		dev_err(&pdev->dev, "Failed to ioremap memory\n");
		return -EADDRNOTAVAIL;
	}

	pdata = exynos_get_driver_data(pdev, data->id);
	if (!pdata) {
		dev_err(&pdev->dev, "No platform init data supplied.\n");
		return -ENODEV;
	}

	data->pdata = pdata;
	data->soc = pdata->type;

	switch (data->soc) {
	case SOC_ARCH_EXYNOS4210:
		data->tmu_initialize = exynos4210_tmu_initialize;
		data->tmu_control = exynos4210_tmu_control;
		data->tmu_read = exynos4210_tmu_read;
		data->tmu_clear_irqs = exynos4210_tmu_clear_irqs;
		break;
	case SOC_ARCH_EXYNOS3250:
	case SOC_ARCH_EXYNOS4412:
	case SOC_ARCH_EXYNOS5250:
	case SOC_ARCH_EXYNOS5260:
	case SOC_ARCH_EXYNOS5420:
	case SOC_ARCH_EXYNOS5420_TRIMINFO:
		data->tmu_initialize = exynos4412_tmu_initialize;
		data->tmu_control = exynos4210_tmu_control;
		data->tmu_read = exynos4412_tmu_read;
		data->tmu_set_emulation = exynos4412_tmu_set_emulation;
		data->tmu_clear_irqs = exynos4210_tmu_clear_irqs;
		break;
	case SOC_ARCH_EXYNOS5440:
		data->tmu_initialize = exynos5440_tmu_initialize;
		data->tmu_control = exynos5440_tmu_control;
		data->tmu_read = exynos5440_tmu_read;
		data->tmu_set_emulation = exynos5440_tmu_set_emulation;
		data->tmu_clear_irqs = exynos5440_tmu_clear_irqs;
		break;
	default:
		dev_err(&pdev->dev, "Platform not supported\n");
		return -EINVAL;
	}

	/*
	 * Check if the TMU shares some registers and then try to map the
	 * memory of common registers.
	 */
	if (data->soc != SOC_ARCH_EXYNOS5420_TRIMINFO &&
	    data->soc != SOC_ARCH_EXYNOS5440)
		return 0;

	if (of_address_to_resource(pdev->dev.of_node, 1, &res)) {
		dev_err(&pdev->dev, "failed to get Resource 1\n");
		return -ENODEV;
	}

	data->base_second = devm_ioremap(&pdev->dev, res.start,
					resource_size(&res));
	if (!data->base_second) {
		dev_err(&pdev->dev, "Failed to ioremap memory\n");
		return -ENOMEM;
	}

	return 0;
}

static int exynos_tmu_probe(struct platform_device *pdev)
{
	struct exynos_tmu_data *data;
	struct exynos_tmu_platform_data *pdata;
	struct thermal_sensor_conf *sensor_conf;
	int ret, i;

	data = devm_kzalloc(&pdev->dev, sizeof(struct exynos_tmu_data),
					GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	platform_set_drvdata(pdev, data);
	mutex_init(&data->lock);

	ret = exynos_map_dt_data(pdev);
	if (ret)
		return ret;

	pdata = data->pdata;

	INIT_WORK(&data->irq_work, exynos_tmu_work);

	data->clk = devm_clk_get(&pdev->dev, "tmu_apbif");
	if (IS_ERR(data->clk)) {
		dev_err(&pdev->dev, "Failed to get clock\n");
		return  PTR_ERR(data->clk);
	}

	data->clk_sec = devm_clk_get(&pdev->dev, "tmu_triminfo_apbif");
	if (IS_ERR(data->clk_sec)) {
		if (data->soc == SOC_ARCH_EXYNOS5420_TRIMINFO) {
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

	ret = exynos_tmu_initialize(pdev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to initialize TMU\n");
		goto err_clk;
	}

	exynos_tmu_control(pdev, true);

	/* Allocate a structure to register with the exynos core thermal */
	sensor_conf = devm_kzalloc(&pdev->dev,
				sizeof(struct thermal_sensor_conf), GFP_KERNEL);
	if (!sensor_conf) {
		ret = -ENOMEM;
		goto err_clk;
	}
	sprintf(sensor_conf->name, "therm_zone%d", data->id);
	sensor_conf->read_temperature = (int (*)(void *))exynos_tmu_read;
	sensor_conf->write_emul_temp =
		(int (*)(void *, unsigned long))exynos_tmu_set_emulation;
	sensor_conf->driver_data = data;
	sensor_conf->trip_data.trip_count = pdata->trigger_enable[0] +
			pdata->trigger_enable[1] + pdata->trigger_enable[2]+
			pdata->trigger_enable[3];

	for (i = 0; i < sensor_conf->trip_data.trip_count; i++) {
		sensor_conf->trip_data.trip_val[i] =
			pdata->threshold + pdata->trigger_levels[i];
		sensor_conf->trip_data.trip_type[i] =
					pdata->trigger_type[i];
	}

	sensor_conf->trip_data.trigger_falling = pdata->threshold_falling;

	sensor_conf->cooling_data.freq_clip_count = pdata->freq_tab_count;
	for (i = 0; i < pdata->freq_tab_count; i++) {
		sensor_conf->cooling_data.freq_data[i].freq_clip_max =
					pdata->freq_tab[i].freq_clip_max;
		sensor_conf->cooling_data.freq_data[i].temp_level =
					pdata->freq_tab[i].temp_level;
	}
	sensor_conf->dev = &pdev->dev;
	/* Register the sensor with thermal management interface */
	ret = exynos_register_thermal(sensor_conf);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register thermal interface\n");
		goto err_clk;
	}
	data->reg_conf = sensor_conf;

	ret = devm_request_irq(&pdev->dev, data->irq, exynos_tmu_irq,
		IRQF_TRIGGER_RISING | IRQF_SHARED, dev_name(&pdev->dev), data);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request irq: %d\n", data->irq);
		goto err_clk;
	}

	return 0;
err_clk:
	clk_unprepare(data->clk);
err_clk_sec:
	if (!IS_ERR(data->clk_sec))
		clk_unprepare(data->clk_sec);
	return ret;
}

static int exynos_tmu_remove(struct platform_device *pdev)
{
	struct exynos_tmu_data *data = platform_get_drvdata(pdev);

	exynos_unregister_thermal(data->reg_conf);

	exynos_tmu_control(pdev, false);

	clk_unprepare(data->clk);
	if (!IS_ERR(data->clk_sec))
		clk_unprepare(data->clk_sec);

	if (!IS_ERR(data->regulator))
		regulator_disable(data->regulator);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int exynos_tmu_suspend(struct device *dev)
{
	exynos_tmu_control(to_platform_device(dev), false);

	return 0;
}

static int exynos_tmu_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);

	exynos_tmu_initialize(pdev);
	exynos_tmu_control(pdev, true);

	return 0;
}

static SIMPLE_DEV_PM_OPS(exynos_tmu_pm,
			 exynos_tmu_suspend, exynos_tmu_resume);
#define EXYNOS_TMU_PM	(&exynos_tmu_pm)
#else
#define EXYNOS_TMU_PM	NULL
#endif

static struct platform_driver exynos_tmu_driver = {
	.driver = {
		.name   = "exynos-tmu",
		.owner  = THIS_MODULE,
		.pm     = EXYNOS_TMU_PM,
		.of_match_table = exynos_tmu_match,
	},
	.probe = exynos_tmu_probe,
	.remove	= exynos_tmu_remove,
};

module_platform_driver(exynos_tmu_driver);

MODULE_DESCRIPTION("EXYNOS TMU Driver");
MODULE_AUTHOR("Donggeun Kim <dg77.kim@samsung.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:exynos-tmu");
