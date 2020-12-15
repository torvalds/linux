// SPDX-License-Identifier: GPL-2.0
#include <linux/bug.h>
#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/math64.h>
#include <linux/log2.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/units.h>

#include "qcom-vadc-common.h"

/* Voltage to temperature */
static const struct vadc_map_pt adcmap_100k_104ef_104fb[] = {
	{1758,	-40},
	{1742,	-35},
	{1719,	-30},
	{1691,	-25},
	{1654,	-20},
	{1608,	-15},
	{1551,	-10},
	{1483,	-5},
	{1404,	0},
	{1315,	5},
	{1218,	10},
	{1114,	15},
	{1007,	20},
	{900,	25},
	{795,	30},
	{696,	35},
	{605,	40},
	{522,	45},
	{448,	50},
	{383,	55},
	{327,	60},
	{278,	65},
	{237,	70},
	{202,	75},
	{172,	80},
	{146,	85},
	{125,	90},
	{107,	95},
	{92,	100},
	{79,	105},
	{68,	110},
	{59,	115},
	{51,	120},
	{44,	125}
};

/*
 * Voltage to temperature table for 100k pull up for NTCG104EF104 with
 * 1.875V reference.
 */
static const struct vadc_map_pt adcmap_100k_104ef_104fb_1875_vref[] = {
	{ 1831,	-40000 },
	{ 1814,	-35000 },
	{ 1791,	-30000 },
	{ 1761,	-25000 },
	{ 1723,	-20000 },
	{ 1675,	-15000 },
	{ 1616,	-10000 },
	{ 1545,	-5000 },
	{ 1463,	0 },
	{ 1370,	5000 },
	{ 1268,	10000 },
	{ 1160,	15000 },
	{ 1049,	20000 },
	{ 937,	25000 },
	{ 828,	30000 },
	{ 726,	35000 },
	{ 630,	40000 },
	{ 544,	45000 },
	{ 467,	50000 },
	{ 399,	55000 },
	{ 340,	60000 },
	{ 290,	65000 },
	{ 247,	70000 },
	{ 209,	75000 },
	{ 179,	80000 },
	{ 153,	85000 },
	{ 130,	90000 },
	{ 112,	95000 },
	{ 96,	100000 },
	{ 82,	105000 },
	{ 71,	110000 },
	{ 62,	115000 },
	{ 53,	120000 },
	{ 46,	125000 },
};

static const struct vadc_map_pt adcmap7_die_temp[] = {
	{ 433700, 1967},
	{ 473100, 1964},
	{ 512400, 1957},
	{ 551500, 1949},
	{ 590500, 1940},
	{ 629300, 1930},
	{ 667900, 1921},
	{ 706400, 1910},
	{ 744600, 1896},
	{ 782500, 1878},
	{ 820100, 1859},
	{ 857300, 0},
};

/*
 * Resistance to temperature table for 100k pull up for NTCG104EF104.
 */
static const struct vadc_map_pt adcmap7_100k[] = {
	{ 4250657, -40960 },
	{ 3962085, -39936 },
	{ 3694875, -38912 },
	{ 3447322, -37888 },
	{ 3217867, -36864 },
	{ 3005082, -35840 },
	{ 2807660, -34816 },
	{ 2624405, -33792 },
	{ 2454218, -32768 },
	{ 2296094, -31744 },
	{ 2149108, -30720 },
	{ 2012414, -29696 },
	{ 1885232, -28672 },
	{ 1766846, -27648 },
	{ 1656598, -26624 },
	{ 1553884, -25600 },
	{ 1458147, -24576 },
	{ 1368873, -23552 },
	{ 1285590, -22528 },
	{ 1207863, -21504 },
	{ 1135290, -20480 },
	{ 1067501, -19456 },
	{ 1004155, -18432 },
	{ 944935, -17408 },
	{ 889550, -16384 },
	{ 837731, -15360 },
	{ 789229, -14336 },
	{ 743813, -13312 },
	{ 701271, -12288 },
	{ 661405, -11264 },
	{ 624032, -10240 },
	{ 588982, -9216 },
	{ 556100, -8192 },
	{ 525239, -7168 },
	{ 496264, -6144 },
	{ 469050, -5120 },
	{ 443480, -4096 },
	{ 419448, -3072 },
	{ 396851, -2048 },
	{ 375597, -1024 },
	{ 355598, 0 },
	{ 336775, 1024 },
	{ 319052, 2048 },
	{ 302359, 3072 },
	{ 286630, 4096 },
	{ 271806, 5120 },
	{ 257829, 6144 },
	{ 244646, 7168 },
	{ 232209, 8192 },
	{ 220471, 9216 },
	{ 209390, 10240 },
	{ 198926, 11264 },
	{ 189040, 12288 },
	{ 179698, 13312 },
	{ 170868, 14336 },
	{ 162519, 15360 },
	{ 154622, 16384 },
	{ 147150, 17408 },
	{ 140079, 18432 },
	{ 133385, 19456 },
	{ 127046, 20480 },
	{ 121042, 21504 },
	{ 115352, 22528 },
	{ 109960, 23552 },
	{ 104848, 24576 },
	{ 100000, 25600 },
	{ 95402, 26624 },
	{ 91038, 27648 },
	{ 86897, 28672 },
	{ 82965, 29696 },
	{ 79232, 30720 },
	{ 75686, 31744 },
	{ 72316, 32768 },
	{ 69114, 33792 },
	{ 66070, 34816 },
	{ 63176, 35840 },
	{ 60423, 36864 },
	{ 57804, 37888 },
	{ 55312, 38912 },
	{ 52940, 39936 },
	{ 50681, 40960 },
	{ 48531, 41984 },
	{ 46482, 43008 },
	{ 44530, 44032 },
	{ 42670, 45056 },
	{ 40897, 46080 },
	{ 39207, 47104 },
	{ 37595, 48128 },
	{ 36057, 49152 },
	{ 34590, 50176 },
	{ 33190, 51200 },
	{ 31853, 52224 },
	{ 30577, 53248 },
	{ 29358, 54272 },
	{ 28194, 55296 },
	{ 27082, 56320 },
	{ 26020, 57344 },
	{ 25004, 58368 },
	{ 24033, 59392 },
	{ 23104, 60416 },
	{ 22216, 61440 },
	{ 21367, 62464 },
	{ 20554, 63488 },
	{ 19776, 64512 },
	{ 19031, 65536 },
	{ 18318, 66560 },
	{ 17636, 67584 },
	{ 16982, 68608 },
	{ 16355, 69632 },
	{ 15755, 70656 },
	{ 15180, 71680 },
	{ 14628, 72704 },
	{ 14099, 73728 },
	{ 13592, 74752 },
	{ 13106, 75776 },
	{ 12640, 76800 },
	{ 12192, 77824 },
	{ 11762, 78848 },
	{ 11350, 79872 },
	{ 10954, 80896 },
	{ 10574, 81920 },
	{ 10209, 82944 },
	{ 9858, 83968 },
	{ 9521, 84992 },
	{ 9197, 86016 },
	{ 8886, 87040 },
	{ 8587, 88064 },
	{ 8299, 89088 },
	{ 8023, 90112 },
	{ 7757, 91136 },
	{ 7501, 92160 },
	{ 7254, 93184 },
	{ 7017, 94208 },
	{ 6789, 95232 },
	{ 6570, 96256 },
	{ 6358, 97280 },
	{ 6155, 98304 },
	{ 5959, 99328 },
	{ 5770, 100352 },
	{ 5588, 101376 },
	{ 5412, 102400 },
	{ 5243, 103424 },
	{ 5080, 104448 },
	{ 4923, 105472 },
	{ 4771, 106496 },
	{ 4625, 107520 },
	{ 4484, 108544 },
	{ 4348, 109568 },
	{ 4217, 110592 },
	{ 4090, 111616 },
	{ 3968, 112640 },
	{ 3850, 113664 },
	{ 3736, 114688 },
	{ 3626, 115712 },
	{ 3519, 116736 },
	{ 3417, 117760 },
	{ 3317, 118784 },
	{ 3221, 119808 },
	{ 3129, 120832 },
	{ 3039, 121856 },
	{ 2952, 122880 },
	{ 2868, 123904 },
	{ 2787, 124928 },
	{ 2709, 125952 },
	{ 2633, 126976 },
	{ 2560, 128000 },
	{ 2489, 129024 },
	{ 2420, 130048 }
};

static int qcom_vadc_scale_hw_calib_volt(
				const struct vadc_prescale_ratio *prescale,
				const struct adc5_data *data,
				u16 adc_code, int *result_uv);
static int qcom_vadc_scale_hw_calib_therm(
				const struct vadc_prescale_ratio *prescale,
				const struct adc5_data *data,
				u16 adc_code, int *result_mdec);
static int qcom_vadc7_scale_hw_calib_therm(
				const struct vadc_prescale_ratio *prescale,
				const struct adc5_data *data,
				u16 adc_code, int *result_mdec);
static int qcom_vadc_scale_hw_smb_temp(
				const struct vadc_prescale_ratio *prescale,
				const struct adc5_data *data,
				u16 adc_code, int *result_mdec);
static int qcom_vadc_scale_hw_chg5_temp(
				const struct vadc_prescale_ratio *prescale,
				const struct adc5_data *data,
				u16 adc_code, int *result_mdec);
static int qcom_vadc_scale_hw_calib_die_temp(
				const struct vadc_prescale_ratio *prescale,
				const struct adc5_data *data,
				u16 adc_code, int *result_mdec);
static int qcom_vadc7_scale_hw_calib_die_temp(
				const struct vadc_prescale_ratio *prescale,
				const struct adc5_data *data,
				u16 adc_code, int *result_mdec);

static struct qcom_adc5_scale_type scale_adc5_fn[] = {
	[SCALE_HW_CALIB_DEFAULT] = {qcom_vadc_scale_hw_calib_volt},
	[SCALE_HW_CALIB_THERM_100K_PULLUP] = {qcom_vadc_scale_hw_calib_therm},
	[SCALE_HW_CALIB_XOTHERM] = {qcom_vadc_scale_hw_calib_therm},
	[SCALE_HW_CALIB_THERM_100K_PU_PM7] = {
					qcom_vadc7_scale_hw_calib_therm},
	[SCALE_HW_CALIB_PMIC_THERM] = {qcom_vadc_scale_hw_calib_die_temp},
	[SCALE_HW_CALIB_PMIC_THERM_PM7] = {
					qcom_vadc7_scale_hw_calib_die_temp},
	[SCALE_HW_CALIB_PM5_CHG_TEMP] = {qcom_vadc_scale_hw_chg5_temp},
	[SCALE_HW_CALIB_PM5_SMB_TEMP] = {qcom_vadc_scale_hw_smb_temp},
};

static int qcom_vadc_map_voltage_temp(const struct vadc_map_pt *pts,
				      u32 tablesize, s32 input, int *output)
{
	bool descending = 1;
	u32 i = 0;

	if (!pts)
		return -EINVAL;

	/* Check if table is descending or ascending */
	if (tablesize > 1) {
		if (pts[0].x < pts[1].x)
			descending = 0;
	}

	while (i < tablesize) {
		if ((descending) && (pts[i].x < input)) {
			/* table entry is less than measured*/
			 /* value and table is descending, stop */
			break;
		} else if ((!descending) &&
				(pts[i].x > input)) {
			/* table entry is greater than measured*/
			/*value and table is ascending, stop */
			break;
		}
		i++;
	}

	if (i == 0) {
		*output = pts[0].y;
	} else if (i == tablesize) {
		*output = pts[tablesize - 1].y;
	} else {
		/* result is between search_index and search_index-1 */
		/* interpolate linearly */
		*output = (((s32)((pts[i].y - pts[i - 1].y) *
			(input - pts[i - 1].x)) /
			(pts[i].x - pts[i - 1].x)) +
			pts[i - 1].y);
	}

	return 0;
}

static void qcom_vadc_scale_calib(const struct vadc_linear_graph *calib_graph,
				  u16 adc_code,
				  bool absolute,
				  s64 *scale_voltage)
{
	*scale_voltage = (adc_code - calib_graph->gnd);
	*scale_voltage *= calib_graph->dx;
	*scale_voltage = div64_s64(*scale_voltage, calib_graph->dy);
	if (absolute)
		*scale_voltage += calib_graph->dx;

	if (*scale_voltage < 0)
		*scale_voltage = 0;
}

static int qcom_vadc_scale_volt(const struct vadc_linear_graph *calib_graph,
				const struct vadc_prescale_ratio *prescale,
				bool absolute, u16 adc_code,
				int *result_uv)
{
	s64 voltage = 0, result = 0;

	qcom_vadc_scale_calib(calib_graph, adc_code, absolute, &voltage);

	voltage = voltage * prescale->den;
	result = div64_s64(voltage, prescale->num);
	*result_uv = result;

	return 0;
}

static int qcom_vadc_scale_therm(const struct vadc_linear_graph *calib_graph,
				 const struct vadc_prescale_ratio *prescale,
				 bool absolute, u16 adc_code,
				 int *result_mdec)
{
	s64 voltage = 0;
	int ret;

	qcom_vadc_scale_calib(calib_graph, adc_code, absolute, &voltage);

	if (absolute)
		voltage = div64_s64(voltage, 1000);

	ret = qcom_vadc_map_voltage_temp(adcmap_100k_104ef_104fb,
					 ARRAY_SIZE(adcmap_100k_104ef_104fb),
					 voltage, result_mdec);
	if (ret)
		return ret;

	*result_mdec *= 1000;

	return 0;
}

static int qcom_vadc_scale_die_temp(const struct vadc_linear_graph *calib_graph,
				    const struct vadc_prescale_ratio *prescale,
				    bool absolute,
				    u16 adc_code, int *result_mdec)
{
	s64 voltage = 0;
	u64 temp; /* Temporary variable for do_div */

	qcom_vadc_scale_calib(calib_graph, adc_code, absolute, &voltage);

	if (voltage > 0) {
		temp = voltage * prescale->den;
		do_div(temp, prescale->num * 2);
		voltage = temp;
	} else {
		voltage = 0;
	}

	*result_mdec = milli_kelvin_to_millicelsius(voltage);

	return 0;
}

static int qcom_vadc_scale_chg_temp(const struct vadc_linear_graph *calib_graph,
				    const struct vadc_prescale_ratio *prescale,
				    bool absolute,
				    u16 adc_code, int *result_mdec)
{
	s64 voltage = 0, result = 0;

	qcom_vadc_scale_calib(calib_graph, adc_code, absolute, &voltage);

	voltage = voltage * prescale->den;
	voltage = div64_s64(voltage, prescale->num);
	voltage = ((PMI_CHG_SCALE_1) * (voltage * 2));
	voltage = (voltage + PMI_CHG_SCALE_2);
	result =  div64_s64(voltage, 1000000);
	*result_mdec = result;

	return 0;
}

static int qcom_vadc_scale_code_voltage_factor(u16 adc_code,
				const struct vadc_prescale_ratio *prescale,
				const struct adc5_data *data,
				unsigned int factor)
{
	s64 voltage, temp, adc_vdd_ref_mv = 1875;

	/*
	 * The normal data range is between 0V to 1.875V. On cases where
	 * we read low voltage values, the ADC code can go beyond the
	 * range and the scale result is incorrect so we clamp the values
	 * for the cases where the code represents a value below 0V
	 */
	if (adc_code > VADC5_MAX_CODE)
		adc_code = 0;

	/* (ADC code * vref_vadc (1.875V)) / full_scale_code */
	voltage = (s64) adc_code * adc_vdd_ref_mv * 1000;
	voltage = div64_s64(voltage, data->full_scale_code_volt);
	if (voltage > 0) {
		voltage *= prescale->den;
		temp = prescale->num * factor;
		voltage = div64_s64(voltage, temp);
	} else {
		voltage = 0;
	}

	return (int) voltage;
}

static int qcom_vadc7_scale_hw_calib_therm(
				const struct vadc_prescale_ratio *prescale,
				const struct adc5_data *data,
				u16 adc_code, int *result_mdec)
{
	s64 resistance = adc_code;
	int ret, result;

	if (adc_code >= RATIO_MAX_ADC7)
		return -EINVAL;

	/* (ADC code * R_PULLUP (100Kohm)) / (full_scale_code - ADC code)*/
	resistance *= R_PU_100K;
	resistance = div64_s64(resistance, RATIO_MAX_ADC7 - adc_code);

	ret = qcom_vadc_map_voltage_temp(adcmap7_100k,
				 ARRAY_SIZE(adcmap7_100k),
				 resistance, &result);
	if (ret)
		return ret;

	*result_mdec = result;

	return 0;
}

static int qcom_vadc_scale_hw_calib_volt(
				const struct vadc_prescale_ratio *prescale,
				const struct adc5_data *data,
				u16 adc_code, int *result_uv)
{
	*result_uv = qcom_vadc_scale_code_voltage_factor(adc_code,
				prescale, data, 1);

	return 0;
}

static int qcom_vadc_scale_hw_calib_therm(
				const struct vadc_prescale_ratio *prescale,
				const struct adc5_data *data,
				u16 adc_code, int *result_mdec)
{
	int voltage;

	voltage = qcom_vadc_scale_code_voltage_factor(adc_code,
				prescale, data, 1000);

	/* Map voltage to temperature from look-up table */
	return qcom_vadc_map_voltage_temp(adcmap_100k_104ef_104fb_1875_vref,
				 ARRAY_SIZE(adcmap_100k_104ef_104fb_1875_vref),
				 voltage, result_mdec);
}

static int qcom_vadc_scale_hw_calib_die_temp(
				const struct vadc_prescale_ratio *prescale,
				const struct adc5_data *data,
				u16 adc_code, int *result_mdec)
{
	*result_mdec = qcom_vadc_scale_code_voltage_factor(adc_code,
				prescale, data, 2);
	*result_mdec = milli_kelvin_to_millicelsius(*result_mdec);

	return 0;
}

static int qcom_vadc7_scale_hw_calib_die_temp(
				const struct vadc_prescale_ratio *prescale,
				const struct adc5_data *data,
				u16 adc_code, int *result_mdec)
{

	int voltage, vtemp0, temp, i;

	voltage = qcom_vadc_scale_code_voltage_factor(adc_code,
				prescale, data, 1);

	if (adcmap7_die_temp[0].x > voltage) {
		*result_mdec = DIE_TEMP_ADC7_SCALE_1;
		return 0;
	}

	if (adcmap7_die_temp[ARRAY_SIZE(adcmap7_die_temp) - 1].x <= voltage) {
		*result_mdec = DIE_TEMP_ADC7_MAX;
		return 0;
	}

	for (i = 0; i < ARRAY_SIZE(adcmap7_die_temp); i++)
		if (adcmap7_die_temp[i].x > voltage)
			break;

	vtemp0 = adcmap7_die_temp[i - 1].x;
	voltage = voltage - vtemp0;
	temp = div64_s64(voltage * DIE_TEMP_ADC7_SCALE_FACTOR,
		adcmap7_die_temp[i - 1].y);
	temp += DIE_TEMP_ADC7_SCALE_1 + (DIE_TEMP_ADC7_SCALE_2 * (i - 1));
	*result_mdec = temp;

	return 0;
}

static int qcom_vadc_scale_hw_smb_temp(
				const struct vadc_prescale_ratio *prescale,
				const struct adc5_data *data,
				u16 adc_code, int *result_mdec)
{
	*result_mdec = qcom_vadc_scale_code_voltage_factor(adc_code * 100,
				prescale, data, PMIC5_SMB_TEMP_SCALE_FACTOR);
	*result_mdec = PMIC5_SMB_TEMP_CONSTANT - *result_mdec;

	return 0;
}

static int qcom_vadc_scale_hw_chg5_temp(
				const struct vadc_prescale_ratio *prescale,
				const struct adc5_data *data,
				u16 adc_code, int *result_mdec)
{
	*result_mdec = qcom_vadc_scale_code_voltage_factor(adc_code,
				prescale, data, 4);
	*result_mdec = PMIC5_CHG_TEMP_SCALE_FACTOR - *result_mdec;

	return 0;
}

int qcom_vadc_scale(enum vadc_scale_fn_type scaletype,
		    const struct vadc_linear_graph *calib_graph,
		    const struct vadc_prescale_ratio *prescale,
		    bool absolute,
		    u16 adc_code, int *result)
{
	switch (scaletype) {
	case SCALE_DEFAULT:
		return qcom_vadc_scale_volt(calib_graph, prescale,
					    absolute, adc_code,
					    result);
	case SCALE_THERM_100K_PULLUP:
	case SCALE_XOTHERM:
		return qcom_vadc_scale_therm(calib_graph, prescale,
					     absolute, adc_code,
					     result);
	case SCALE_PMIC_THERM:
		return qcom_vadc_scale_die_temp(calib_graph, prescale,
						absolute, adc_code,
						result);
	case SCALE_PMI_CHG_TEMP:
		return qcom_vadc_scale_chg_temp(calib_graph, prescale,
						absolute, adc_code,
						result);
	default:
		return -EINVAL;
	}
}
EXPORT_SYMBOL(qcom_vadc_scale);

int qcom_adc5_hw_scale(enum vadc_scale_fn_type scaletype,
		    const struct vadc_prescale_ratio *prescale,
		    const struct adc5_data *data,
		    u16 adc_code, int *result)
{
	if (!(scaletype >= SCALE_HW_CALIB_DEFAULT &&
		scaletype < SCALE_HW_CALIB_INVALID)) {
		pr_err("Invalid scale type %d\n", scaletype);
		return -EINVAL;
	}

	return scale_adc5_fn[scaletype].scale_fn(prescale, data,
					adc_code, result);
}
EXPORT_SYMBOL(qcom_adc5_hw_scale);

int qcom_vadc_decimation_from_dt(u32 value)
{
	if (!is_power_of_2(value) || value < VADC_DECIMATION_MIN ||
	    value > VADC_DECIMATION_MAX)
		return -EINVAL;

	return __ffs64(value / VADC_DECIMATION_MIN);
}
EXPORT_SYMBOL(qcom_vadc_decimation_from_dt);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Qualcomm ADC common functionality");
