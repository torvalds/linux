/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * OMAP4 Bandgap temperature sensor driver
 *
 * Copyright (C) 2011 Texas Instruments Incorporated - http://www.ti.com/
 * Contact:
 *   Eduardo Valentin <eduardo.valentin@ti.com>
 */
#ifndef __TI_BANDGAP_H
#define __TI_BANDGAP_H

#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/err.h>
#include <linux/cpu_pm.h>
#include <linux/device.h>
#include <linux/pm_runtime.h>
#include <linux/pm.h>

struct gpio_desc;

/**
 * DOC: bandgap driver data structure
 * ==================================
 *
 *   +----------+----------------+
 *   | struct temp_sensor_regval |
 *   +---------------------------+
 *              * (Array of)
 *              |
 *              |
 *   +-------------------+   +-----------------+
 *   | struct ti_bandgap |-->| struct device * |
 *   +----------+--------+   +-----------------+
 *              |
 *              |
 *              V
 *   +------------------------+
 *   | struct ti_bandgap_data |
 *   +------------------------+
 *              |
 *              |
 *              * (Array of)
 * +------------+------------------------------------------------------+
 * | +----------+------------+   +-------------------------+           |
 * | | struct ti_temp_sensor |-->| struct temp_sensor_data |           |
 * | +-----------------------+   +------------+------------+           |
 * |            |                                                      |
 * |            +                                                      |
 * |            V                                                      |
 * | +----------+-------------------+                                  |
 * | | struct temp_sensor_registers |                                  |
 * | +------------------------------+                                  |
 * |                                                                   |
 * +-------------------------------------------------------------------+
 *
 * Above is a simple diagram describing how the data structure below
 * are organized. For each bandgap device there should be a ti_bandgap_data
 * containing the device instance configuration, as well as, an array of
 * sensors, representing every sensor instance present in this bandgap.
 */

/**
 * struct temp_sensor_registers - descriptor to access registers and bitfields
 * @temp_sensor_ctrl: TEMP_SENSOR_CTRL register offset
 * @bgap_tempsoff_mask: mask to temp_sensor_ctrl.tempsoff
 * @bgap_soc_mask: mask to temp_sensor_ctrl.soc
 * @bgap_eocz_mask: mask to temp_sensor_ctrl.eocz
 * @bgap_dtemp_mask: mask to temp_sensor_ctrl.dtemp
 * @bgap_mask_ctrl: BANDGAP_MASK_CTRL register offset
 * @mask_hot_mask: mask to bandgap_mask_ctrl.mask_hot
 * @mask_cold_mask: mask to bandgap_mask_ctrl.mask_cold
 * @mask_counter_delay_mask: mask to bandgap_mask_ctrl.mask_counter_delay
 * @mask_freeze_mask: mask to bandgap_mask_ctrl.mask_free
 * @bgap_mode_ctrl: BANDGAP_MODE_CTRL register offset
 * @mode_ctrl_mask: mask to bandgap_mode_ctrl.mode_ctrl
 * @bgap_counter: BANDGAP_COUNTER register offset
 * @counter_mask: mask to bandgap_counter.counter
 * @bgap_threshold: BANDGAP_THRESHOLD register offset (TALERT thresholds)
 * @threshold_thot_mask: mask to bandgap_threhold.thot
 * @threshold_tcold_mask: mask to bandgap_threhold.tcold
 * @tshut_threshold: TSHUT_THRESHOLD register offset (TSHUT thresholds)
 * @tshut_hot_mask: mask to tshut_threhold.thot
 * @tshut_cold_mask: mask to tshut_threhold.thot
 * @bgap_status: BANDGAP_STATUS register offset
 * @status_hot_mask: mask to bandgap_status.hot
 * @status_cold_mask: mask to bandgap_status.cold
 * @ctrl_dtemp_1: CTRL_DTEMP1 register offset
 * @ctrl_dtemp_2: CTRL_DTEMP2 register offset
 * @bgap_efuse: BANDGAP_EFUSE register offset
 *
 * The register offsets and bitfields might change across
 * OMAP and variants versions. Hence this struct serves as a
 * descriptor map on how to access the registers and the bitfields.
 *
 * This descriptor contains registers of all versions of bandgap chips.
 * Not all versions will use all registers, depending on the available
 * features. Please read TRMs for descriptive explanation on each bitfield.
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
	u32	mask_counter_delay_mask;
	u32	mask_freeze_mask;

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
	u32	status_hot_mask;
	u32	status_cold_mask;

	u32	ctrl_dtemp_1;
	u32	ctrl_dtemp_2;
	u32	bgap_efuse;
};

/**
 * struct temp_sensor_data - The thresholds and limits for temperature sensors.
 * @tshut_hot: temperature to trigger a thermal reset (initial value)
 * @tshut_cold: temp to get the plat out of reset due to thermal (init val)
 * @t_hot: temperature to trigger a thermal alert (high initial value)
 * @t_cold: temperature to trigger a thermal alert (low initial value)
 * @min_freq: sensor minimum clock rate
 * @max_freq: sensor maximum clock rate
 *
 * This data structure will hold the required thresholds and temperature limits
 * for a specific temperature sensor, like shutdown temperature, alert
 * temperature, clock / rate used, ADC conversion limits and update intervals
 */
struct temp_sensor_data {
	u32	tshut_hot;
	u32	tshut_cold;
	u32	t_hot;
	u32	t_cold;
	u32	min_freq;
	u32	max_freq;
};

struct ti_bandgap_data;

/**
 * struct temp_sensor_regval - temperature sensor register values and priv data
 * @bg_mode_ctrl: temp sensor control register value
 * @bg_ctrl: bandgap ctrl register value
 * @bg_counter: bandgap counter value
 * @bg_threshold: bandgap threshold register value
 * @tshut_threshold: bandgap tshut register value
 * @data: private data
 *
 * Data structure to save and restore bandgap register set context. Only
 * required registers are shadowed, when needed.
 */
struct temp_sensor_regval {
	u32			bg_mode_ctrl;
	u32			bg_ctrl;
	u32			bg_counter;
	u32			bg_threshold;
	u32			tshut_threshold;
	void			*data;
};

/**
 * struct ti_bandgap - bandgap device structure
 * @dev: struct device pointer
 * @base: io memory base address
 * @conf: struct with bandgap configuration set (# sensors, conv_table, etc)
 * @regval: temperature sensor register values
 * @fclock: pointer to functional clock of temperature sensor
 * @div_clk: pointer to divider clock of temperature sensor fclk
 * @lock: spinlock for ti_bandgap structure
 * @irq: MPU IRQ number for thermal alert
 * @tshut_gpio: GPIO where Tshut signal is routed
 * @clk_rate: Holds current clock rate
 *
 * The bandgap device structure representing the bandgap device instance.
 * It holds most of the dynamic stuff. Configurations and sensor specific
 * entries are inside the @conf structure.
 */
struct ti_bandgap {
	struct device			*dev;
	void __iomem			*base;
	const struct ti_bandgap_data	*conf;
	struct temp_sensor_regval	*regval;
	struct clk			*fclock;
	struct clk			*div_clk;
	spinlock_t			lock; /* shields this struct */
	int				irq;
	struct gpio_desc		*tshut_gpiod;
	u32				clk_rate;
	struct notifier_block		nb;
	unsigned int is_suspended:1;
};

/**
 * struct ti_temp_sensor - bandgap temperature sensor configuration data
 * @ts_data: pointer to struct with thresholds, limits of temperature sensor
 * @registers: pointer to the list of register offsets and bitfields
 * @domain: the name of the domain where the sensor is located
 * @slope_pcb: sensor gradient slope info for hotspot extrapolation equation
 *             with no external influence
 * @constant_pcb: sensor gradient const info for hotspot extrapolation equation
 *             with no external influence
 * @register_cooling: function to describe how this sensor is going to be cooled
 * @unregister_cooling: function to release cooling data
 *
 * Data structure to describe a temperature sensor handled by a bandgap device.
 * It should provide configuration details on this sensor, such as how to
 * access the registers affecting this sensor, shadow register buffer, how to
 * assess the gradient from hotspot, how to cooldown the domain when sensor
 * reports too hot temperature.
 */
struct ti_temp_sensor {
	struct temp_sensor_data		*ts_data;
	struct temp_sensor_registers	*registers;
	char				*domain;
	/* for hotspot extrapolation */
	const int			slope_pcb;
	const int			constant_pcb;
	int (*register_cooling)(struct ti_bandgap *bgp, int id);
	int (*unregister_cooling)(struct ti_bandgap *bgp, int id);
};

/**
 * DOC: ti bandgap feature types
 *
 * TI_BANDGAP_FEATURE_TSHUT - used when the thermal shutdown signal output
 *      of a bandgap device instance is routed to the processor. This means
 *      the system must react and perform the shutdown by itself (handle an
 *      IRQ, for instance).
 *
 * TI_BANDGAP_FEATURE_TSHUT_CONFIG - used when the bandgap device has control
 *      over the thermal shutdown configuration. This means that the thermal
 *      shutdown thresholds are programmable, for instance.
 *
 * TI_BANDGAP_FEATURE_TALERT - used when the bandgap device instance outputs
 *      a signal representing violation of programmable alert thresholds.
 *
 * TI_BANDGAP_FEATURE_MODE_CONFIG - used when it is possible to choose which
 *      mode, continuous or one shot, the bandgap device instance will operate.
 *
 * TI_BANDGAP_FEATURE_COUNTER - used when the bandgap device instance allows
 *      programming the update interval of its internal state machine.
 *
 * TI_BANDGAP_FEATURE_POWER_SWITCH - used when the bandgap device allows
 *      itself to be switched on/off.
 *
 * TI_BANDGAP_FEATURE_CLK_CTRL - used when the clocks feeding the bandgap
 *      device are gateable or not.
 *
 * TI_BANDGAP_FEATURE_FREEZE_BIT - used when the bandgap device features
 *      a history buffer that its update can be freezed/unfreezed.
 *
 * TI_BANDGAP_FEATURE_COUNTER_DELAY - used when the bandgap device features
 *	a delay programming based on distinct values.
 *
 * TI_BANDGAP_FEATURE_HISTORY_BUFFER - used when the bandgap device features
 *	a history buffer of temperatures.
 *
 * TI_BANDGAP_FEATURE_ERRATA_814 - used to workaorund when the bandgap device
 *	has Errata 814
 * TI_BANDGAP_FEATURE_UNRELIABLE - used when the sensor readings are too
 *	inaccurate.
 * TI_BANDGAP_FEATURE_CONT_MODE_ONLY - used when single mode hangs the sensor
 * TI_BANDGAP_HAS(b, f) - macro to check if a bandgap device is capable of a
 *      specific feature (above) or not. Return non-zero, if yes.
 */
#define TI_BANDGAP_FEATURE_TSHUT		BIT(0)
#define TI_BANDGAP_FEATURE_TSHUT_CONFIG		BIT(1)
#define TI_BANDGAP_FEATURE_TALERT		BIT(2)
#define TI_BANDGAP_FEATURE_MODE_CONFIG		BIT(3)
#define TI_BANDGAP_FEATURE_COUNTER		BIT(4)
#define TI_BANDGAP_FEATURE_POWER_SWITCH		BIT(5)
#define TI_BANDGAP_FEATURE_CLK_CTRL		BIT(6)
#define TI_BANDGAP_FEATURE_FREEZE_BIT		BIT(7)
#define TI_BANDGAP_FEATURE_COUNTER_DELAY	BIT(8)
#define TI_BANDGAP_FEATURE_HISTORY_BUFFER	BIT(9)
#define TI_BANDGAP_FEATURE_ERRATA_814		BIT(10)
#define TI_BANDGAP_FEATURE_UNRELIABLE		BIT(11)
#define TI_BANDGAP_FEATURE_CONT_MODE_ONLY	BIT(12)
#define TI_BANDGAP_HAS(b, f)			\
			((b)->conf->features & TI_BANDGAP_FEATURE_ ## f)

/**
 * struct ti_bandgap_data - ti bandgap data configuration structure
 * @features: a bitwise flag set to describe the device features
 * @conv_table: Pointer to ADC to temperature conversion table
 * @adc_start_val: ADC conversion table starting value
 * @adc_end_val: ADC conversion table ending value
 * @fclock_name: clock name of the functional clock
 * @div_ck_name: clock name of the clock divisor
 * @sensor_count: count of temperature sensor within this bandgap device
 * @report_temperature: callback to report thermal alert to thermal API
 * @expose_sensor: callback to export sensor to thermal API
 * @remove_sensor: callback to destroy sensor from thermal API
 * @sensors: array of sensors present in this bandgap instance
 *
 * This is a data structure which should hold most of the static configuration
 * of a bandgap device instance. It should describe which features this instance
 * is capable of, the clock names to feed this device, the amount of sensors and
 * their configuration representation, and how to export and unexport them to
 * a thermal API.
 */
struct ti_bandgap_data {
	unsigned int			features;
	const int			*conv_table;
	u32				adc_start_val;
	u32				adc_end_val;
	char				*fclock_name;
	char				*div_ck_name;
	int				sensor_count;
	int (*report_temperature)(struct ti_bandgap *bgp, int id);
	int (*expose_sensor)(struct ti_bandgap *bgp, int id, char *domain);
	int (*remove_sensor)(struct ti_bandgap *bgp, int id);

	/* this needs to be at the end */
	struct ti_temp_sensor		sensors[];
};

int ti_bandgap_read_update_interval(struct ti_bandgap *bgp, int id,
				    int *interval);
int ti_bandgap_write_update_interval(struct ti_bandgap *bgp, int id,
				     u32 interval);
int ti_bandgap_read_temperature(struct ti_bandgap *bgp, int id,
				  int *temperature);
int ti_bandgap_set_sensor_data(struct ti_bandgap *bgp, int id, void *data);
void *ti_bandgap_get_sensor_data(struct ti_bandgap *bgp, int id);
int ti_bandgap_get_trend(struct ti_bandgap *bgp, int id, int *trend);

#ifdef CONFIG_OMAP3_THERMAL
extern const struct ti_bandgap_data omap34xx_data;
extern const struct ti_bandgap_data omap36xx_data;
#else
#define omap34xx_data					NULL
#define omap36xx_data					NULL
#endif

#ifdef CONFIG_OMAP4_THERMAL
extern const struct ti_bandgap_data omap4430_data;
extern const struct ti_bandgap_data omap4460_data;
extern const struct ti_bandgap_data omap4470_data;
#else
#define omap4430_data					NULL
#define omap4460_data					NULL
#define omap4470_data					NULL
#endif

#ifdef CONFIG_OMAP5_THERMAL
extern const struct ti_bandgap_data omap5430_data;
#else
#define omap5430_data					NULL
#endif

#ifdef CONFIG_DRA752_THERMAL
extern const struct ti_bandgap_data dra752_data;
#else
#define dra752_data					NULL
#endif
#endif
