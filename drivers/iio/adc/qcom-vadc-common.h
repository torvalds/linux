/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Code shared between the different Qualcomm PMIC voltage ADCs
 */

#ifndef QCOM_VADC_COMMON_H
#define QCOM_VADC_COMMON_H

#define VADC_CONV_TIME_MIN_US			2000
#define VADC_CONV_TIME_MAX_US			2100

/* Min ADC code represents 0V */
#define VADC_MIN_ADC_CODE			0x6000
/* Max ADC code represents full-scale range of 1.8V */
#define VADC_MAX_ADC_CODE			0xa800

#define VADC_ABSOLUTE_RANGE_UV			625000
#define VADC_RATIOMETRIC_RANGE			1800

#define VADC_DEF_PRESCALING			0 /* 1:1 */
#define VADC_DEF_DECIMATION			0 /* 512 */
#define VADC_DEF_HW_SETTLE_TIME			0 /* 0 us */
#define VADC_DEF_AVG_SAMPLES			0 /* 1 sample */
#define VADC_DEF_CALIB_TYPE			VADC_CALIB_ABSOLUTE

#define VADC_DECIMATION_MIN			512
#define VADC_DECIMATION_MAX			4096

#define VADC_HW_SETTLE_DELAY_MAX		10000
#define VADC_AVG_SAMPLES_MAX			512

#define KELVINMIL_CELSIUSMIL			273150

#define PMI_CHG_SCALE_1				-138890
#define PMI_CHG_SCALE_2				391750000000LL

/**
 * struct vadc_map_pt - Map the graph representation for ADC channel
 * @x: Represent the ADC digitized code.
 * @y: Represent the physical data which can be temperature, voltage,
 *     resistance.
 */
struct vadc_map_pt {
	s32 x;
	s32 y;
};

/*
 * VADC_CALIB_ABSOLUTE: uses the 625mV and 1.25V as reference channels.
 * VADC_CALIB_RATIOMETRIC: uses the reference voltage (1.8V) and GND for
 * calibration.
 */
enum vadc_calibration {
	VADC_CALIB_ABSOLUTE = 0,
	VADC_CALIB_RATIOMETRIC
};

/**
 * struct vadc_linear_graph - Represent ADC characteristics.
 * @dy: numerator slope to calculate the gain.
 * @dx: denominator slope to calculate the gain.
 * @gnd: A/D word of the ground reference used for the channel.
 *
 * Each ADC device has different offset and gain parameters which are
 * computed to calibrate the device.
 */
struct vadc_linear_graph {
	s32 dy;
	s32 dx;
	s32 gnd;
};

/**
 * struct vadc_prescale_ratio - Represent scaling ratio for ADC input.
 * @num: the inverse numerator of the gain applied to the input channel.
 * @den: the inverse denominator of the gain applied to the input channel.
 */
struct vadc_prescale_ratio {
	u32 num;
	u32 den;
};

/**
 * enum vadc_scale_fn_type - Scaling function to convert ADC code to
 *				physical scaled units for the channel.
 * SCALE_DEFAULT: Default scaling to convert raw adc code to voltage (uV).
 * SCALE_THERM_100K_PULLUP: Returns temperature in millidegC.
 *				 Uses a mapping table with 100K pullup.
 * SCALE_PMIC_THERM: Returns result in milli degree's Centigrade.
 * SCALE_XOTHERM: Returns XO thermistor voltage in millidegC.
 * SCALE_PMI_CHG_TEMP: Conversion for PMI CHG temp
 */
enum vadc_scale_fn_type {
	SCALE_DEFAULT = 0,
	SCALE_THERM_100K_PULLUP,
	SCALE_PMIC_THERM,
	SCALE_XOTHERM,
	SCALE_PMI_CHG_TEMP,
};

int qcom_vadc_scale(enum vadc_scale_fn_type scaletype,
		    const struct vadc_linear_graph *calib_graph,
		    const struct vadc_prescale_ratio *prescale,
		    bool absolute,
		    u16 adc_code, int *result_mdec);

int qcom_vadc_decimation_from_dt(u32 value);

#endif /* QCOM_VADC_COMMON_H */
