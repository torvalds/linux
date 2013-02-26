/*
 * OMAP4 Bandgap temperature sensor driver
 *
 * Copyright (C) 2011 Texas Instruments Incorporated - http://www.ti.com/
 * Contact:
 *   Eduardo Valentin <eduardo.valentin@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */
#ifndef __OMAP_BANDGAP_H
#define __OMAP_BANDGAP_H

#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/err.h>

/* TEMP_SENSOR OMAP4430 */
#define OMAP4430_BGAP_TSHUT_SHIFT			11
#define OMAP4430_BGAP_TSHUT_MASK			(1 << 11)

/* TEMP_SENSOR OMAP4430 */
#define OMAP4430_BGAP_TEMPSOFF_SHIFT			12
#define OMAP4430_BGAP_TEMPSOFF_MASK			(1 << 12)
#define OMAP4430_SINGLE_MODE_SHIFT			10
#define OMAP4430_SINGLE_MODE_MASK			(1 << 10)
#define OMAP4430_BGAP_TEMP_SENSOR_SOC_SHIFT		9
#define OMAP4430_BGAP_TEMP_SENSOR_SOC_MASK		(1 << 9)
#define OMAP4430_BGAP_TEMP_SENSOR_EOCZ_SHIFT		8
#define OMAP4430_BGAP_TEMP_SENSOR_EOCZ_MASK		(1 << 8)
#define OMAP4430_BGAP_TEMP_SENSOR_DTEMP_SHIFT		0
#define OMAP4430_BGAP_TEMP_SENSOR_DTEMP_MASK		(0xff << 0)

#define OMAP4430_ADC_START_VALUE			0
#define OMAP4430_ADC_END_VALUE				127
#define OMAP4430_MAX_FREQ				32768
#define OMAP4430_MIN_FREQ				32768
#define OMAP4430_MIN_TEMP				-40000
#define OMAP4430_MAX_TEMP				125000
#define OMAP4430_HYST_VAL				5000

/* TEMP_SENSOR OMAP4460 */
#define OMAP4460_BGAP_TEMPSOFF_SHIFT			13
#define OMAP4460_BGAP_TEMPSOFF_MASK			(1 << 13)
#define OMAP4460_BGAP_TEMP_SENSOR_SOC_SHIFT		11
#define OMAP4460_BGAP_TEMP_SENSOR_SOC_MASK		(1 << 11)
#define OMAP4460_BGAP_TEMP_SENSOR_EOCZ_SHIFT		10
#define OMAP4460_BGAP_TEMP_SENSOR_EOCZ_MASK		(1 << 10)
#define OMAP4460_BGAP_TEMP_SENSOR_DTEMP_SHIFT		0
#define OMAP4460_BGAP_TEMP_SENSOR_DTEMP_MASK		(0x3ff << 0)

/* BANDGAP_CTRL */
#define OMAP4460_SINGLE_MODE_SHIFT			31
#define OMAP4460_SINGLE_MODE_MASK			(1 << 31)
#define OMAP4460_MASK_HOT_SHIFT				1
#define OMAP4460_MASK_HOT_MASK				(1 << 1)
#define OMAP4460_MASK_COLD_SHIFT			0
#define OMAP4460_MASK_COLD_MASK				(1 << 0)

/* BANDGAP_COUNTER */
#define OMAP4460_COUNTER_SHIFT				0
#define OMAP4460_COUNTER_MASK				(0xffffff << 0)

/* BANDGAP_THRESHOLD */
#define OMAP4460_T_HOT_SHIFT				16
#define OMAP4460_T_HOT_MASK				(0x3ff << 16)
#define OMAP4460_T_COLD_SHIFT				0
#define OMAP4460_T_COLD_MASK				(0x3ff << 0)

/* TSHUT_THRESHOLD */
#define OMAP4460_TSHUT_HOT_SHIFT			16
#define OMAP4460_TSHUT_HOT_MASK				(0x3ff << 16)
#define OMAP4460_TSHUT_COLD_SHIFT			0
#define OMAP4460_TSHUT_COLD_MASK			(0x3ff << 0)

/* BANDGAP_STATUS */
#define OMAP4460_CLEAN_STOP_SHIFT			3
#define OMAP4460_CLEAN_STOP_MASK			(1 << 3)
#define OMAP4460_BGAP_ALERT_SHIFT			2
#define OMAP4460_BGAP_ALERT_MASK			(1 << 2)
#define OMAP4460_HOT_FLAG_SHIFT				1
#define OMAP4460_HOT_FLAG_MASK				(1 << 1)
#define OMAP4460_COLD_FLAG_SHIFT			0
#define OMAP4460_COLD_FLAG_MASK				(1 << 0)

/* TEMP_SENSOR OMAP5430 */
#define OMAP5430_BGAP_TEMP_SENSOR_SOC_SHIFT		12
#define OMAP5430_BGAP_TEMP_SENSOR_SOC_MASK		(1 << 12)
#define OMAP5430_BGAP_TEMPSOFF_SHIFT			11
#define OMAP5430_BGAP_TEMPSOFF_MASK			(1 << 11)
#define OMAP5430_BGAP_TEMP_SENSOR_EOCZ_SHIFT		10
#define OMAP5430_BGAP_TEMP_SENSOR_EOCZ_MASK		(1 << 10)
#define OMAP5430_BGAP_TEMP_SENSOR_DTEMP_SHIFT		0
#define OMAP5430_BGAP_TEMP_SENSOR_DTEMP_MASK		(0x3ff << 0)

/* BANDGAP_CTRL */
#define OMAP5430_MASK_HOT_CORE_SHIFT			5
#define OMAP5430_MASK_HOT_CORE_MASK			(1 << 5)
#define OMAP5430_MASK_COLD_CORE_SHIFT			4
#define OMAP5430_MASK_COLD_CORE_MASK			(1 << 4)
#define OMAP5430_MASK_HOT_MM_SHIFT			3
#define OMAP5430_MASK_HOT_MM_MASK			(1 << 3)
#define OMAP5430_MASK_COLD_MM_SHIFT			2
#define OMAP5430_MASK_COLD_MM_MASK			(1 << 2)
#define OMAP5430_MASK_HOT_MPU_SHIFT			1
#define OMAP5430_MASK_HOT_MPU_MASK			(1 << 1)
#define OMAP5430_MASK_COLD_MPU_SHIFT			0
#define OMAP5430_MASK_COLD_MPU_MASK			(1 << 0)

/* BANDGAP_COUNTER */
#define OMAP5430_REPEAT_MODE_SHIFT			31
#define OMAP5430_REPEAT_MODE_MASK			(1 << 31)
#define OMAP5430_COUNTER_SHIFT				0
#define OMAP5430_COUNTER_MASK				(0xffffff << 0)

/* BANDGAP_THRESHOLD */
#define OMAP5430_T_HOT_SHIFT				16
#define OMAP5430_T_HOT_MASK				(0x3ff << 16)
#define OMAP5430_T_COLD_SHIFT				0
#define OMAP5430_T_COLD_MASK				(0x3ff << 0)

/* TSHUT_THRESHOLD */
#define OMAP5430_TSHUT_HOT_SHIFT			16
#define OMAP5430_TSHUT_HOT_MASK				(0x3ff << 16)
#define OMAP5430_TSHUT_COLD_SHIFT			0
#define OMAP5430_TSHUT_COLD_MASK			(0x3ff << 0)

/* BANDGAP_STATUS */
#define OMAP5430_BGAP_ALERT_SHIFT			31
#define OMAP5430_BGAP_ALERT_MASK			(1 << 31)
#define OMAP5430_HOT_CORE_FLAG_SHIFT			5
#define OMAP5430_HOT_CORE_FLAG_MASK			(1 << 5)
#define OMAP5430_COLD_CORE_FLAG_SHIFT			4
#define OMAP5430_COLD_CORE_FLAG_MASK			(1 << 4)
#define OMAP5430_HOT_MM_FLAG_SHIFT			3
#define OMAP5430_HOT_MM_FLAG_MASK			(1 << 3)
#define OMAP5430_COLD_MM_FLAG_SHIFT			2
#define OMAP5430_COLD_MM_FLAG_MASK			(1 << 2)
#define OMAP5430_HOT_MPU_FLAG_SHIFT			1
#define OMAP5430_HOT_MPU_FLAG_MASK			(1 << 1)
#define OMAP5430_COLD_MPU_FLAG_SHIFT			0
#define OMAP5430_COLD_MPU_FLAG_MASK			(1 << 0)

/* Offsets from the base of temperature sensor registers */

/* 4430 - All goes relative to OPP_BGAP */
#define OMAP4430_FUSE_OPP_BGAP				0x0
#define OMAP4430_TEMP_SENSOR_CTRL_OFFSET		0xCC

/* 4460 - All goes relative to OPP_BGAP */
#define OMAP4460_FUSE_OPP_BGAP				0x0
#define OMAP4460_TEMP_SENSOR_CTRL_OFFSET		0xCC
#define OMAP4460_BGAP_CTRL_OFFSET			0x118
#define OMAP4460_BGAP_COUNTER_OFFSET			0x11C
#define OMAP4460_BGAP_THRESHOLD_OFFSET			0x120
#define OMAP4460_BGAP_TSHUT_OFFSET			0x124
#define OMAP4460_BGAP_STATUS_OFFSET			0x128

/* 5430 - All goes relative to OPP_BGAP_GPU */
#define OMAP5430_FUSE_OPP_BGAP_GPU			0x0
#define OMAP5430_TEMP_SENSOR_GPU_OFFSET			0x150
#define OMAP5430_BGAP_COUNTER_GPU_OFFSET		0x1C0
#define OMAP5430_BGAP_THRESHOLD_GPU_OFFSET		0x1A8
#define OMAP5430_BGAP_TSHUT_GPU_OFFSET			0x1B4

#define OMAP5430_FUSE_OPP_BGAP_MPU			0x4
#define OMAP5430_TEMP_SENSOR_MPU_OFFSET			0x14C
#define OMAP5430_BGAP_CTRL_OFFSET			0x1A0
#define OMAP5430_BGAP_COUNTER_MPU_OFFSET		0x1BC
#define OMAP5430_BGAP_THRESHOLD_MPU_OFFSET		0x1A4
#define OMAP5430_BGAP_TSHUT_MPU_OFFSET			0x1B0
#define OMAP5430_BGAP_STATUS_OFFSET			0x1C8

#define OMAP5430_FUSE_OPP_BGAP_CORE			0x8
#define OMAP5430_TEMP_SENSOR_CORE_OFFSET		0x154
#define OMAP5430_BGAP_COUNTER_CORE_OFFSET		0x1C4
#define OMAP5430_BGAP_THRESHOLD_CORE_OFFSET		0x1AC
#define OMAP5430_BGAP_TSHUT_CORE_OFFSET			0x1B8

#define OMAP4460_TSHUT_HOT				900	/* 122 deg C */
#define OMAP4460_TSHUT_COLD				895	/* 100 deg C */
#define OMAP4460_T_HOT					800	/* 73 deg C */
#define OMAP4460_T_COLD					795	/* 71 deg C */
#define OMAP4460_MAX_FREQ				1500000
#define OMAP4460_MIN_FREQ				1000000
#define OMAP4460_MIN_TEMP				-40000
#define OMAP4460_MAX_TEMP				123000
#define OMAP4460_HYST_VAL				5000
#define OMAP4460_ADC_START_VALUE			530
#define OMAP4460_ADC_END_VALUE				932

#define OMAP5430_MPU_TSHUT_HOT				915
#define OMAP5430_MPU_TSHUT_COLD				900
#define OMAP5430_MPU_T_HOT				800
#define OMAP5430_MPU_T_COLD				795
#define OMAP5430_MPU_MAX_FREQ				1500000
#define OMAP5430_MPU_MIN_FREQ				1000000
#define OMAP5430_MPU_MIN_TEMP				-40000
#define OMAP5430_MPU_MAX_TEMP				125000
#define OMAP5430_MPU_HYST_VAL				5000
#define OMAP5430_ADC_START_VALUE			540
#define OMAP5430_ADC_END_VALUE				945

#define OMAP5430_GPU_TSHUT_HOT				915
#define OMAP5430_GPU_TSHUT_COLD				900
#define OMAP5430_GPU_T_HOT				800
#define OMAP5430_GPU_T_COLD				795
#define OMAP5430_GPU_MAX_FREQ				1500000
#define OMAP5430_GPU_MIN_FREQ				1000000
#define OMAP5430_GPU_MIN_TEMP				-40000
#define OMAP5430_GPU_MAX_TEMP				125000
#define OMAP5430_GPU_HYST_VAL				5000

#define OMAP5430_CORE_TSHUT_HOT				915
#define OMAP5430_CORE_TSHUT_COLD			900
#define OMAP5430_CORE_T_HOT				800
#define OMAP5430_CORE_T_COLD				795
#define OMAP5430_CORE_MAX_FREQ				1500000
#define OMAP5430_CORE_MIN_FREQ				1000000
#define OMAP5430_CORE_MIN_TEMP				-40000
#define OMAP5430_CORE_MAX_TEMP				125000
#define OMAP5430_CORE_HYST_VAL				5000

/**
 * The register offsets and bit fields might change across
 * OMAP versions hence populating them in this structure.
 */

struct temp_sensor_registers {
	u32	temp_sensor_ctrl;
	u32	bgap_tempsoff_mask;
	u32	bgap_soc_mask;
	u32	bgap_eocz_mask;
	u32	bgap_dtemp_mask;

	u32	bgap_mask_ctrl;
	u32	mask_hot_mask;
	u32	mask_cold_mask;

	u32	bgap_mode_ctrl;
	u32	mode_ctrl_mask;

	u32	bgap_counter;
	u32	counter_mask;

	u32	bgap_threshold;
	u32	threshold_thot_mask;
	u32	threshold_tcold_mask;

	u32	tshut_threshold;
	u32	tshut_hot_mask;
	u32	tshut_cold_mask;

	u32	bgap_status;
	u32	status_clean_stop_mask;
	u32	status_bgap_alert_mask;
	u32	status_hot_mask;
	u32	status_cold_mask;

	u32	bgap_efuse;
};

/**
 * The thresholds and limits for temperature sensors.
 */
struct temp_sensor_data {
	u32	tshut_hot;
	u32	tshut_cold;
	u32	t_hot;
	u32	t_cold;
	u32	min_freq;
	u32	max_freq;
	int     max_temp;
	int     min_temp;
	int     hyst_val;
	u32     adc_start_val;
	u32     adc_end_val;
	u32     update_int1;
	u32     update_int2;
};

struct omap_bandgap_data;

/**
 * struct omap_bandgap - bandgap device structure
 * @dev: device pointer
 * @conf: platform data with sensor data
 * @fclock: pointer to functional clock of temperature sensor
 * @div_clk: pointer to parent clock of temperature sensor fclk
 * @conv_table: Pointer to adc to temperature conversion table
 * @bg_mutex: Mutex for sysfs, irq and PM
 * @irq: MPU Irq number for thermal alert
 * @tshut_gpio: GPIO where Tshut signal is routed
 * @clk_rate: Holds current clock rate
 */
struct omap_bandgap {
	struct device			*dev;
	void __iomem			*base;
	struct omap_bandgap_data	*conf;
	struct clk			*fclock;
	struct clk			*div_clk;
	const int			*conv_table;
	struct mutex			bg_mutex; /* Mutex for irq and PM */
	int				irq;
	int				tshut_gpio;
	u32				clk_rate;
};

/**
 * struct temp_sensor_regval - temperature sensor register values
 * @bg_mode_ctrl: temp sensor control register value
 * @bg_ctrl: bandgap ctrl register value
 * @bg_counter: bandgap counter value
 * @bg_threshold: bandgap threshold register value
 * @tshut_threshold: bandgap tshut register value
 */
struct temp_sensor_regval {
	u32			bg_mode_ctrl;
	u32			bg_ctrl;
	u32			bg_counter;
	u32			bg_threshold;
	u32			tshut_threshold;
};

/**
 * struct omap_temp_sensor - bandgap temperature sensor platform data
 * @ts_data: pointer to struct with thresholds, limits of temperature sensor
 * @registers: pointer to the list of register offsets and bitfields
 * @regval: temperature sensor register values
 * @domain: the name of the domain where the sensor is located
 * @cooling_data: description on how the zone should be cooled off.
 * @slope: sensor gradient slope info for hotspot extrapolation
 * @const: sensor gradient const info for hotspot extrapolation
 * @slope_pcb: sensor gradient slope info for hotspot extrapolation
 *             with no external influence
 * @const_pcb: sensor gradient const info for hotspot extrapolation
 *             with no external influence
 * @data: private data
 * @register_cooling: function to describe how this sensor is going to be cooled
 * @unregister_cooling: function to release cooling data
 */
struct omap_temp_sensor {
	struct temp_sensor_data		*ts_data;
	struct temp_sensor_registers	*registers;
	struct temp_sensor_regval	regval;
	char				*domain;
	/* for hotspot extrapolation */
	const int			slope;
	const int			constant;
	const int			slope_pcb;
	const int			constant_pcb;
	void				*data;
	int (*register_cooling)(struct omap_bandgap *bg_ptr, int id);
	int (*unregister_cooling)(struct omap_bandgap *bg_ptr, int id);
};

/**
 * struct omap_bandgap_data - bandgap platform data structure
 * @features: a bitwise flag set to describe the device features
 * @conv_table: Pointer to adc to temperature conversion table
 * @fclock_name: clock name of the functional clock
 * @div_ck_nme: clock name of the clock divisor
 * @sensor_count: count of temperature sensor device in scm
 * @sensors: array of sensors present in this bandgap instance
 * @expose_sensor: callback to export sensor to thermal API
 */
struct omap_bandgap_data {
#define OMAP_BANDGAP_FEATURE_TSHUT		(1 << 0)
#define OMAP_BANDGAP_FEATURE_TSHUT_CONFIG	(1 << 1)
#define OMAP_BANDGAP_FEATURE_TALERT		(1 << 2)
#define OMAP_BANDGAP_FEATURE_MODE_CONFIG	(1 << 3)
#define OMAP_BANDGAP_FEATURE_COUNTER		(1 << 4)
#define OMAP_BANDGAP_FEATURE_POWER_SWITCH	(1 << 5)
#define OMAP_BANDGAP_FEATURE_CLK_CTRL		(1 << 6)
#define OMAP_BANDGAP_HAS(b, f)			\
			((b)->conf->features & OMAP_BANDGAP_FEATURE_ ## f)
	unsigned int			features;
	const int			*conv_table;
	char				*fclock_name;
	char				*div_ck_name;
	int				sensor_count;
	int (*report_temperature)(struct omap_bandgap *bg_ptr, int id);
	int (*expose_sensor)(struct omap_bandgap *bg_ptr, int id, char *domain);
	int (*remove_sensor)(struct omap_bandgap *bg_ptr, int id);

	/* this needs to be at the end */
	struct omap_temp_sensor		sensors[];
};

int omap_bandgap_read_thot(struct omap_bandgap *bg_ptr, int id, int *thot);
int omap_bandgap_write_thot(struct omap_bandgap *bg_ptr, int id, int val);
int omap_bandgap_read_tcold(struct omap_bandgap *bg_ptr, int id, int *tcold);
int omap_bandgap_write_tcold(struct omap_bandgap *bg_ptr, int id, int val);
int omap_bandgap_read_update_interval(struct omap_bandgap *bg_ptr, int id,
				      int *interval);
int omap_bandgap_write_update_interval(struct omap_bandgap *bg_ptr, int id,
				       u32 interval);
int omap_bandgap_read_temperature(struct omap_bandgap *bg_ptr, int id,
				  int *temperature);
int omap_bandgap_set_sensor_data(struct omap_bandgap *bg_ptr, int id,
				 void *data);
void *omap_bandgap_get_sensor_data(struct omap_bandgap *bg_ptr, int id);

#ifdef CONFIG_OMAP4_THERMAL
extern const struct omap_bandgap_data omap4430_data;
extern const struct omap_bandgap_data omap4460_data;
extern const struct omap_bandgap_data omap4470_data;
#else
#define omap4430_data					NULL
#define omap4460_data					NULL
#define omap4470_data					NULL
#endif

#ifdef CONFIG_OMAP5_THERMAL
extern const struct omap_bandgap_data omap5430_data;
#else
#define omap5430_data					NULL
#endif

#endif
