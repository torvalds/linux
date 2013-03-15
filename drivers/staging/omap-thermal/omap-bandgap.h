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
	u32	mask_sidlemode_mask;
	u32	mask_freeze_mask;
	u32	mask_clear_mask;
	u32	mask_clear_accum_mask;

	u32	bgap_mode_ctrl;
	u32	mode_ctrl_mask;

	u32	bgap_counter;
	u32	counter_mask;

	u32	bgap_threshold;
	u32	threshold_thot_mask;
	u32	threshold_tcold_mask;

	u32	tshut_threshold;
	u32	tshut_efuse_mask;
	u32	tshut_efuse_shift;
	u32	tshut_hot_mask;
	u32	tshut_cold_mask;

	u32	bgap_status;
	u32	status_clean_stop_mask;
	u32	status_bgap_alert_mask;
	u32	status_hot_mask;
	u32	status_cold_mask;

	u32	bgap_cumul_dtemp;
	u32	ctrl_dtemp_0;
	u32	ctrl_dtemp_1;
	u32	ctrl_dtemp_2;
	u32	ctrl_dtemp_3;
	u32	ctrl_dtemp_4;
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
#define OMAP_BANDGAP_FEATURE_TSHUT		BIT(0)
#define OMAP_BANDGAP_FEATURE_TSHUT_CONFIG	BIT(1)
#define OMAP_BANDGAP_FEATURE_TALERT		BIT(2)
#define OMAP_BANDGAP_FEATURE_MODE_CONFIG	BIT(3)
#define OMAP_BANDGAP_FEATURE_COUNTER		BIT(4)
#define OMAP_BANDGAP_FEATURE_POWER_SWITCH	BIT(5)
#define OMAP_BANDGAP_FEATURE_CLK_CTRL		BIT(6)
#define OMAP_BANDGAP_FEATURE_FREEZE_BIT		BIT(7)
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
