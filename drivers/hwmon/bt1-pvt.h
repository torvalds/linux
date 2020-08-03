/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 BAIKAL ELECTRONICS, JSC
 *
 * Baikal-T1 Process, Voltage, Temperature sensor driver
 */
#ifndef __HWMON_BT1_PVT_H__
#define __HWMON_BT1_PVT_H__

#include <linux/completion.h>
#include <linux/hwmon.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/seqlock.h>

/* Baikal-T1 PVT registers and their bitfields */
#define PVT_CTRL			0x00
#define PVT_CTRL_EN			BIT(0)
#define PVT_CTRL_MODE_FLD		1
#define PVT_CTRL_MODE_MASK		GENMASK(3, PVT_CTRL_MODE_FLD)
#define PVT_CTRL_MODE_TEMP		0x0
#define PVT_CTRL_MODE_VOLT		0x1
#define PVT_CTRL_MODE_LVT		0x2
#define PVT_CTRL_MODE_HVT		0x4
#define PVT_CTRL_MODE_SVT		0x6
#define PVT_CTRL_TRIM_FLD		4
#define PVT_CTRL_TRIM_MASK		GENMASK(8, PVT_CTRL_TRIM_FLD)
#define PVT_DATA			0x04
#define PVT_DATA_VALID			BIT(10)
#define PVT_DATA_DATA_FLD		0
#define PVT_DATA_DATA_MASK		GENMASK(9, PVT_DATA_DATA_FLD)
#define PVT_TTHRES			0x08
#define PVT_VTHRES			0x0C
#define PVT_LTHRES			0x10
#define PVT_HTHRES			0x14
#define PVT_STHRES			0x18
#define PVT_THRES_LO_FLD		0
#define PVT_THRES_LO_MASK		GENMASK(9, PVT_THRES_LO_FLD)
#define PVT_THRES_HI_FLD		10
#define PVT_THRES_HI_MASK		GENMASK(19, PVT_THRES_HI_FLD)
#define PVT_TTIMEOUT			0x1C
#define PVT_INTR_STAT			0x20
#define PVT_INTR_MASK			0x24
#define PVT_RAW_INTR_STAT		0x28
#define PVT_INTR_DVALID			BIT(0)
#define PVT_INTR_TTHRES_LO		BIT(1)
#define PVT_INTR_TTHRES_HI		BIT(2)
#define PVT_INTR_VTHRES_LO		BIT(3)
#define PVT_INTR_VTHRES_HI		BIT(4)
#define PVT_INTR_LTHRES_LO		BIT(5)
#define PVT_INTR_LTHRES_HI		BIT(6)
#define PVT_INTR_HTHRES_LO		BIT(7)
#define PVT_INTR_HTHRES_HI		BIT(8)
#define PVT_INTR_STHRES_LO		BIT(9)
#define PVT_INTR_STHRES_HI		BIT(10)
#define PVT_INTR_ALL			GENMASK(10, 0)
#define PVT_CLR_INTR			0x2C

/*
 * PVT sensors-related limits and default values
 * @PVT_TEMP_MIN: Minimal temperature in millidegrees of Celsius.
 * @PVT_TEMP_MAX: Maximal temperature in millidegrees of Celsius.
 * @PVT_TEMP_CHS: Number of temperature hwmon channels.
 * @PVT_VOLT_MIN: Minimal voltage in mV.
 * @PVT_VOLT_MAX: Maximal voltage in mV.
 * @PVT_VOLT_CHS: Number of voltage hwmon channels.
 * @PVT_DATA_MIN: Minimal PVT raw data value.
 * @PVT_DATA_MAX: Maximal PVT raw data value.
 * @PVT_TRIM_MIN: Minimal temperature sensor trim value.
 * @PVT_TRIM_MAX: Maximal temperature sensor trim value.
 * @PVT_TRIM_DEF: Default temperature sensor trim value (set a proper value
 *		  when one is determined for Baikal-T1 SoC).
 * @PVT_TRIM_TEMP: Maximum temperature encoded by the trim factor.
 * @PVT_TRIM_STEP: Temperature stride corresponding to the trim value.
 * @PVT_TOUT_MIN: Minimal timeout between samples in nanoseconds.
 * @PVT_TOUT_DEF: Default data measurements timeout. In case if alarms are
 *		  activated the PVT IRQ is enabled to be raised after each
 *		  conversion in order to have the thresholds checked and the
 *		  converted value cached. Too frequent conversions may cause
 *		  the system CPU overload. Lets set the 50ms delay between
 *		  them by default to prevent this.
 */
#define PVT_TEMP_MIN		-48380L
#define PVT_TEMP_MAX		147438L
#define PVT_TEMP_CHS		1
#define PVT_VOLT_MIN		620L
#define PVT_VOLT_MAX		1168L
#define PVT_VOLT_CHS		4
#define PVT_DATA_MIN		0
#define PVT_DATA_MAX		(PVT_DATA_DATA_MASK >> PVT_DATA_DATA_FLD)
#define PVT_TRIM_MIN		0
#define PVT_TRIM_MAX		(PVT_CTRL_TRIM_MASK >> PVT_CTRL_TRIM_FLD)
#define PVT_TRIM_TEMP		7130
#define PVT_TRIM_STEP		(PVT_TRIM_TEMP / PVT_TRIM_MAX)
#define PVT_TRIM_DEF		0
#define PVT_TOUT_MIN		(NSEC_PER_SEC / 3000)
#if defined(CONFIG_SENSORS_BT1_PVT_ALARMS)
# define PVT_TOUT_DEF		60000
#else
# define PVT_TOUT_DEF		0
#endif

/*
 * enum pvt_sensor_type - Baikal-T1 PVT sensor types (correspond to each PVT
 *			  sampling mode)
 * @PVT_SENSOR*: helpers to traverse the sensors in loops.
 * @PVT_TEMP: PVT Temperature sensor.
 * @PVT_VOLT: PVT Voltage sensor.
 * @PVT_LVT: PVT Low-Voltage threshold sensor.
 * @PVT_HVT: PVT High-Voltage threshold sensor.
 * @PVT_SVT: PVT Standard-Voltage threshold sensor.
 */
enum pvt_sensor_type {
	PVT_SENSOR_FIRST,
	PVT_TEMP = PVT_SENSOR_FIRST,
	PVT_VOLT,
	PVT_LVT,
	PVT_HVT,
	PVT_SVT,
	PVT_SENSOR_LAST = PVT_SVT,
	PVT_SENSORS_NUM
};

/*
 * enum pvt_clock_type - Baikal-T1 PVT clocks.
 * @PVT_CLOCK_APB: APB clock.
 * @PVT_CLOCK_REF: PVT reference clock.
 */
enum pvt_clock_type {
	PVT_CLOCK_APB,
	PVT_CLOCK_REF,
	PVT_CLOCK_NUM
};

/*
 * struct pvt_sensor_info - Baikal-T1 PVT sensor informational structure
 * @channel: Sensor channel ID.
 * @label: hwmon sensor label.
 * @mode: PVT mode corresponding to the channel.
 * @thres_base: upper and lower threshold values of the sensor.
 * @thres_sts_lo: low threshold status bitfield.
 * @thres_sts_hi: high threshold status bitfield.
 * @type: Sensor type.
 * @attr_min_alarm: Min alarm attribute ID.
 * @attr_min_alarm: Max alarm attribute ID.
 */
struct pvt_sensor_info {
	int channel;
	const char *label;
	u32 mode;
	unsigned long thres_base;
	u32 thres_sts_lo;
	u32 thres_sts_hi;
	enum hwmon_sensor_types type;
	u32 attr_min_alarm;
	u32 attr_max_alarm;
};

#define PVT_SENSOR_INFO(_ch, _label, _type, _mode, _thres)	\
	{							\
		.channel = _ch,					\
		.label = _label,				\
		.mode = PVT_CTRL_MODE_ ##_mode,			\
		.thres_base = PVT_ ##_thres,			\
		.thres_sts_lo = PVT_INTR_ ##_thres## _LO,	\
		.thres_sts_hi = PVT_INTR_ ##_thres## _HI,	\
		.type = _type,					\
		.attr_min_alarm = _type## _min,			\
		.attr_max_alarm = _type## _max,			\
	}

/*
 * struct pvt_cache - PVT sensors data cache
 * @data: data cache in raw format.
 * @thres_sts_lo: low threshold status saved on the previous data conversion.
 * @thres_sts_hi: high threshold status saved on the previous data conversion.
 * @data_seqlock: cached data seq-lock.
 * @conversion: data conversion completion.
 */
struct pvt_cache {
	u32 data;
#if defined(CONFIG_SENSORS_BT1_PVT_ALARMS)
	seqlock_t data_seqlock;
	u32 thres_sts_lo;
	u32 thres_sts_hi;
#else
	struct completion conversion;
#endif
};

/*
 * struct pvt_hwmon - Baikal-T1 PVT private data
 * @dev: device structure of the PVT platform device.
 * @hwmon: hwmon device structure.
 * @regs: pointer to the Baikal-T1 PVT registers region.
 * @irq: PVT events IRQ number.
 * @clks: Array of the PVT clocks descriptor (APB/ref clocks).
 * @ref_clk: Pointer to the reference clocks descriptor.
 * @iface_mtx: Generic interface mutex (used to lock the alarm registers
 *	       when the alarms enabled, or the data conversion interface
 *	       if alarms are disabled).
 * @sensor: current PVT sensor the data conversion is being performed for.
 * @cache: data cache descriptor.
 */
struct pvt_hwmon {
	struct device *dev;
	struct device *hwmon;

	void __iomem *regs;
	int irq;

	struct clk_bulk_data clks[PVT_CLOCK_NUM];

	struct mutex iface_mtx;
	enum pvt_sensor_type sensor;
	struct pvt_cache cache[PVT_SENSORS_NUM];
};

/*
 * struct pvt_poly_term - a term descriptor of the PVT data translation
 *			  polynomial
 * @deg: degree of the term.
 * @coef: multiplication factor of the term.
 * @divider: distributed divider per each degree.
 * @divider_leftover: divider leftover, which couldn't be redistributed.
 */
struct pvt_poly_term {
	unsigned int deg;
	long coef;
	long divider;
	long divider_leftover;
};

/*
 * struct pvt_poly - PVT data translation polynomial descriptor
 * @total_divider: total data divider.
 * @terms: polynomial terms up to a free one.
 */
struct pvt_poly {
	long total_divider;
	struct pvt_poly_term terms[];
};

#endif /* __HWMON_BT1_PVT_H__ */
