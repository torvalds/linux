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
#define ADC5_DEF_VBAT_PRESCALING		1 /* 1:3 */
#define ADC5_DECIMATION_SHORT			250
#define ADC5_DECIMATION_MEDIUM			420
#define ADC5_DECIMATION_LONG			840
/* Default decimation - 1024 for rev2, 840 for pmic5 */
#define ADC5_DECIMATION_DEFAULT			2
#define ADC5_DECIMATION_SAMPLES_MAX		3

#define VADC_HW_SETTLE_DELAY_MAX		10000
#define VADC_HW_SETTLE_SAMPLES_MAX		16
#define VADC_AVG_SAMPLES_MAX			512
#define ADC5_AVG_SAMPLES_MAX			16

#define PMIC5_CHG_TEMP_SCALE_FACTOR		377500
#define PMIC5_SMB_TEMP_CONSTANT			419400
#define PMIC5_SMB_TEMP_SCALE_FACTOR		356

#define PMI_CHG_SCALE_1				-138890
#define PMI_CHG_SCALE_2				391750000000LL

#define VADC5_MAX_CODE				0x7fff
#define ADC5_FULL_SCALE_CODE			0x70e4
#define ADC5_USR_DATA_CHECK			0x8000

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
 * SCALE_HW_CALIB_DEFAULT: Default scaling to convert raw adc code to
 *	voltage (uV) with hardware applied offset/slope values to adc code.
 * SCALE_HW_CALIB_THERM_100K_PULLUP: Returns temperature in millidegC using
 *	lookup table. The hardware applies offset/slope to adc code.
 * SCALE_HW_CALIB_XOTHERM: Returns XO thermistor voltage in millidegC using
 *	100k pullup. The hardware applies offset/slope to adc code.
 * SCALE_HW_CALIB_PMIC_THERM: Returns result in milli degree's Centigrade.
 *	The hardware applies offset/slope to adc code.
 * SCALE_HW_CALIB_PM5_CHG_TEMP: Returns result in millidegrees for PMIC5
 *	charger temperature.
 * SCALE_HW_CALIB_PM5_SMB_TEMP: Returns result in millidegrees for PMIC5
 *	SMB1390 temperature.
 */
enum vadc_scale_fn_type {
	SCALE_DEFAULT = 0,
	SCALE_THERM_100K_PULLUP,
	SCALE_PMIC_THERM,
	SCALE_XOTHERM,
	SCALE_PMI_CHG_TEMP,
	SCALE_HW_CALIB_DEFAULT,
	SCALE_HW_CALIB_THERM_100K_PULLUP,
	SCALE_HW_CALIB_XOTHERM,
	SCALE_HW_CALIB_PMIC_THERM,
	SCALE_HW_CALIB_PM5_CHG_TEMP,
	SCALE_HW_CALIB_PM5_SMB_TEMP,
	SCALE_HW_CALIB_INVALID,
};

struct adc5_data {
	const u32	full_scale_code_volt;
	const u32	full_scale_code_cur;
	const struct adc5_channels *adc_chans;
	unsigned int	*decimation;
	unsigned int	*hw_settle_1;
	unsigned int	*hw_settle_2;
};

int qcom_vadc_scale(enum vadc_scale_fn_type scaletype,
		    const struct vadc_linear_graph *calib_graph,
		    const struct vadc_prescale_ratio *prescale,
		    bool absolute,
		    u16 adc_code, int *result_mdec);

struct qcom_adc5_scale_type {
	int (*scale_fn)(const struct vadc_prescale_ratio *prescale,
		const struct adc5_data *data, u16 adc_code, int *result);
};

int qcom_adc5_hw_scale(enum vadc_scale_fn_type scaletype,
		    const struct vadc_prescale_ratio *prescale,
		    const struct adc5_data *data,
		    u16 adc_code, int *result_mdec);

int qcom_vadc_decimation_from_dt(u32 value);

#endif /* QCOM_VADC_COMMON_H */
