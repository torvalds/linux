// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Hardware monitoring driver for MPS Multi-phase Digital VR Controllers
 *
 * Copyright (C) 2020 Nvidia Technologies Ltd.
 */

#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>

#include "pmbus.h"

/* Vendor specific registers. */
#define MP2975_MFR_APS_HYS_R2		0x0d
#define MP2975_MFR_SLOPE_TRIM3		0x1d
#define MP2975_MFR_VR_MULTI_CONFIG_R1	0x0d
#define MP2975_MFR_VR_MULTI_CONFIG_R2	0x1d
#define MP2975_MFR_APS_DECAY_ADV	0x56
#define MP2975_MFR_DC_LOOP_CTRL		0x59
#define MP2975_MFR_OCP_UCP_PHASE_SET	0x65
#define MP2975_MFR_VR_CONFIG1		0x68
#define MP2975_MFR_READ_CS1_2		0x82
#define MP2975_MFR_READ_CS3_4		0x83
#define MP2975_MFR_READ_CS5_6		0x84
#define MP2975_MFR_READ_CS7_8		0x85
#define MP2975_MFR_READ_CS9_10		0x86
#define MP2975_MFR_READ_CS11_12		0x87
#define MP2975_MFR_READ_IOUT_PK		0x90
#define MP2975_MFR_READ_POUT_PK		0x91
#define MP2975_MFR_READ_VREF_R1		0xa1
#define MP2975_MFR_READ_VREF_R2		0xa3
#define MP2975_MFR_OVP_TH_SET		0xe5
#define MP2975_MFR_UVP_SET		0xe6

#define MP2973_MFR_RESO_SET		0xc7

#define MP2975_VOUT_FORMAT		BIT(15)
#define MP2975_VID_STEP_SEL_R1		BIT(4)
#define MP2975_IMVP9_EN_R1		BIT(13)
#define MP2975_VID_STEP_SEL_R2		BIT(3)
#define MP2975_IMVP9_EN_R2		BIT(12)
#define MP2975_PRT_THRES_DIV_OV_EN	BIT(14)
#define MP2975_DRMOS_KCS		GENMASK(13, 12)
#define MP2975_PROT_DEV_OV_OFF		10
#define MP2975_PROT_DEV_OV_ON		5
#define MP2975_SENSE_AMPL		BIT(11)
#define MP2975_SENSE_AMPL_UNIT		1
#define MP2975_SENSE_AMPL_HALF		2
#define MP2975_VIN_UV_LIMIT_UNIT	8

#define MP2973_VOUT_FORMAT_R1		GENMASK(7, 6)
#define MP2973_VOUT_FORMAT_R2		GENMASK(4, 3)
#define MP2973_VOUT_FORMAT_DIRECT_R1	BIT(7)
#define MP2973_VOUT_FORMAT_LINEAR_R1	BIT(6)
#define MP2973_VOUT_FORMAT_DIRECT_R2	BIT(4)
#define MP2973_VOUT_FORMAT_LINEAR_R2	BIT(3)

#define MP2973_MFR_VR_MULTI_CONFIG_R1	0x0d
#define MP2973_MFR_VR_MULTI_CONFIG_R2	0x1d
#define MP2973_VID_STEP_SEL_R1		BIT(4)
#define MP2973_IMVP9_EN_R1		BIT(14)
#define MP2973_VID_STEP_SEL_R2		BIT(3)
#define MP2973_IMVP9_EN_R2		BIT(13)

#define MP2973_MFR_OCP_TOTAL_SET	0x5f
#define MP2973_OCP_TOTAL_CUR_MASK	GENMASK(6, 0)
#define MP2973_MFR_OCP_LEVEL_RES	BIT(15)

#define MP2973_MFR_READ_IOUT_PK		0x90
#define MP2973_MFR_READ_POUT_PK		0x91

#define MP2975_MAX_PHASE_RAIL1	8
#define MP2975_MAX_PHASE_RAIL2	4

#define MP2973_MAX_PHASE_RAIL1	14
#define MP2973_MAX_PHASE_RAIL2	6

#define MP2971_MAX_PHASE_RAIL1	8
#define MP2971_MAX_PHASE_RAIL2	3

#define MP2975_PAGE_NUM		2

#define MP2975_RAIL2_FUNC	(PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT | \
				 PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT | \
				 PMBUS_HAVE_POUT | PMBUS_PHASE_VIRTUAL)

enum chips {
	mp2971, mp2973, mp2975
};

static const int mp2975_max_phases[][MP2975_PAGE_NUM] = {
	[mp2975] = { MP2975_MAX_PHASE_RAIL1, MP2975_MAX_PHASE_RAIL2 },
	[mp2973] = { MP2973_MAX_PHASE_RAIL1, MP2973_MAX_PHASE_RAIL2 },
	[mp2971] = { MP2971_MAX_PHASE_RAIL1, MP2971_MAX_PHASE_RAIL2 },
};

struct mp2975_driver_info {
	const struct pmbus_driver_info *info;
	enum chips chip_id;
};

struct mp2975_data {
	struct pmbus_driver_info info;
	enum chips chip_id;
	int vout_scale;
	int max_phases[MP2975_PAGE_NUM];
	int vid_step[MP2975_PAGE_NUM];
	int vref[MP2975_PAGE_NUM];
	int vref_off[MP2975_PAGE_NUM];
	int vout_max[MP2975_PAGE_NUM];
	int vout_ov_fixed[MP2975_PAGE_NUM];
	int curr_sense_gain[MP2975_PAGE_NUM];
};

static const struct regulator_desc __maybe_unused mp2975_reg_desc[] = {
	PMBUS_REGULATOR("vout", 0),
	PMBUS_REGULATOR("vout", 1),
};

#define to_mp2975_data(x)  container_of(x, struct mp2975_data, info)

static int mp2975_read_byte_data(struct i2c_client *client, int page, int reg)
{
	switch (reg) {
	case PMBUS_VOUT_MODE:
		/*
		 * Report direct format as configured by MFR_DC_LOOP_CTRL.
		 * Unlike on MP2971/MP2973 the reported VOUT_MODE isn't automatically
		 * internally updated, but always reads as PB_VOUT_MODE_VID.
		 */
		return PB_VOUT_MODE_DIRECT;
	default:
		return -ENODATA;
	}
}

static int
mp2975_read_word_helper(struct i2c_client *client, int page, int phase, u8 reg,
			u16 mask)
{
	int ret = pmbus_read_word_data(client, page, phase, reg);

	return (ret > 0) ? ret & mask : ret;
}

static int
mp2975_vid2direct(int vrf, int val)
{
	switch (vrf) {
	case vr12:
		if (val >= 0x01)
			return 250 + (val - 1) * 5;
		break;
	case vr13:
		if (val >= 0x01)
			return 500 + (val - 1) * 10;
		break;
	case imvp9:
		if (val >= 0x01)
			return 200 + (val - 1) * 10;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

#define MAX_LIN_MANTISSA	(1023 * 1000)
#define MIN_LIN_MANTISSA	(511 * 1000)

/* Converts a milli-unit DIRECT value to LINEAR11 format */
static u16 mp2975_data2reg_linear11(s64 val)
{
	s16 exponent = 0, mantissa;
	bool negative = false;

	/* simple case */
	if (val == 0)
		return 0;

	/* Reduce large mantissa until it fits into 10 bit */
	while (val >= MAX_LIN_MANTISSA && exponent < 15) {
		exponent++;
		val >>= 1;
	}
	/* Increase small mantissa to improve precision */
	while (val < MIN_LIN_MANTISSA && exponent > -15) {
		exponent--;
		val <<= 1;
	}

	/* Convert mantissa from milli-units to units */
	mantissa = clamp_val(DIV_ROUND_CLOSEST_ULL(val, 1000), 0, 0x3ff);

	/* restore sign */
	if (negative)
		mantissa = -mantissa;

	/* Convert to 5 bit exponent, 11 bit mantissa */
	return (mantissa & 0x7ff) | ((exponent << 11) & 0xf800);
}

static int
mp2975_read_phase(struct i2c_client *client, struct mp2975_data *data,
		  int page, int phase, u8 reg)
{
	int ph_curr, ret;

	ret = pmbus_read_word_data(client, page, phase, reg);
	if (ret < 0)
		return ret;

	if (!((phase + 1) % MP2975_PAGE_NUM))
		ret >>= 8;
	ret &= 0xff;

	/*
	 * Output value is calculated as: (READ_CSx / 80 – 1.23) / (Kcs * Rcs)
	 * where:
	 * - Kcs is the DrMOS current sense gain of power stage, which is
	 *   obtained from the register MP2975_MFR_VR_CONFIG1, bits 13-12 with
	 *   the following selection of DrMOS (data->curr_sense_gain[page]):
	 *   00b - 5µA/A, 01b - 8.5µA/A, 10b - 9.7µA/A, 11b - 10µA/A.
	 * - Rcs is the internal phase current sense resistor which is constant
	 *   value 1kΩ.
	 */
	ph_curr = ret * 100 - 9800;

	/*
	 * Current phase sensing, providing by the device is not accurate
	 * for the light load. This because sampling of current occurrence of
	 * bit weight has a big deviation for light load. For handling such
	 * case phase current is represented as the maximum between the value
	 * calculated  above and total rail current divided by number phases.
	 */
	ret = pmbus_read_word_data(client, page, phase, PMBUS_READ_IOUT);
	if (ret < 0)
		return ret;

	return max_t(int, DIV_ROUND_CLOSEST(ret, data->info.phases[page]),
		     DIV_ROUND_CLOSEST(ph_curr, data->curr_sense_gain[page]));
}

static int
mp2975_read_phases(struct i2c_client *client, struct mp2975_data *data,
		   int page, int phase)
{
	int ret;

	if (page) {
		switch (phase) {
		case 0 ... 1:
			ret = mp2975_read_phase(client, data, page, phase,
						MP2975_MFR_READ_CS7_8);
			break;
		case 2 ... 3:
			ret = mp2975_read_phase(client, data, page, phase,
						MP2975_MFR_READ_CS9_10);
			break;
		case 4 ... 5:
			ret = mp2975_read_phase(client, data, page, phase,
						MP2975_MFR_READ_CS11_12);
			break;
		default:
			return -ENODATA;
		}
	} else {
		switch (phase) {
		case 0 ... 1:
			ret = mp2975_read_phase(client, data, page, phase,
						MP2975_MFR_READ_CS1_2);
			break;
		case 2 ... 3:
			ret = mp2975_read_phase(client, data, page, phase,
						MP2975_MFR_READ_CS3_4);
			break;
		case 4 ... 5:
			ret = mp2975_read_phase(client, data, page, phase,
						MP2975_MFR_READ_CS5_6);
			break;
		case 6 ... 7:
			ret = mp2975_read_phase(client, data, page, phase,
						MP2975_MFR_READ_CS7_8);
			break;
		case 8 ... 9:
			ret = mp2975_read_phase(client, data, page, phase,
						MP2975_MFR_READ_CS9_10);
			break;
		case 10 ... 11:
			ret = mp2975_read_phase(client, data, page, phase,
						MP2975_MFR_READ_CS11_12);
			break;
		default:
			return -ENODATA;
		}
	}
	return ret;
}

static int mp2973_read_word_data(struct i2c_client *client, int page,
				 int phase, int reg)
{
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	struct mp2975_data *data = to_mp2975_data(info);
	int ret;

	switch (reg) {
	case PMBUS_STATUS_WORD:
		/* MP2973 & MP2971 return PGOOD instead of PB_STATUS_POWER_GOOD_N. */
		ret = pmbus_read_word_data(client, page, phase, reg);
		ret ^= PB_STATUS_POWER_GOOD_N;
		break;
	case PMBUS_OT_FAULT_LIMIT:
		ret = mp2975_read_word_helper(client, page, phase, reg,
					      GENMASK(7, 0));
		break;
	case PMBUS_VIN_OV_FAULT_LIMIT:
		ret = mp2975_read_word_helper(client, page, phase, reg,
					      GENMASK(7, 0));
		if (ret < 0)
			return ret;

		ret = DIV_ROUND_CLOSEST(ret, MP2975_VIN_UV_LIMIT_UNIT);
		break;
	case PMBUS_VOUT_OV_FAULT_LIMIT:
		/*
		 * MP2971 and mp2973 only supports tracking (ovp1) mode.
		 */
		ret = mp2975_read_word_helper(client, page, phase,
					      MP2975_MFR_OVP_TH_SET,
					      GENMASK(2, 0));
		if (ret < 0)
			return ret;

		ret = data->vout_max[page] + 50 * (ret + 1);
		break;
	case PMBUS_VOUT_UV_FAULT_LIMIT:
		ret = mp2975_read_word_helper(client, page, phase, reg,
					      GENMASK(8, 0));
		if (ret < 0)
			return ret;
		ret = mp2975_vid2direct(info->vrm_version[page], ret);
		break;
	case PMBUS_VIRT_READ_POUT_MAX:
		ret = pmbus_read_word_data(client, page, phase,
					   MP2973_MFR_READ_POUT_PK);
		break;
	case PMBUS_VIRT_READ_IOUT_MAX:
		ret = pmbus_read_word_data(client, page, phase,
					   MP2973_MFR_READ_IOUT_PK);
		break;
	case PMBUS_IOUT_OC_FAULT_LIMIT:
		ret = mp2975_read_word_helper(client, page, phase,
					      MP2973_MFR_OCP_TOTAL_SET,
					      GENMASK(15, 0));
		if (ret < 0)
			return ret;

		if (ret & MP2973_MFR_OCP_LEVEL_RES)
			ret = 2 * (ret & MP2973_OCP_TOTAL_CUR_MASK);
		else
			ret = ret & MP2973_OCP_TOTAL_CUR_MASK;

		ret = mp2975_data2reg_linear11(ret * info->phases[page] * 1000);
		break;
	case PMBUS_UT_WARN_LIMIT:
	case PMBUS_UT_FAULT_LIMIT:
	case PMBUS_VIN_UV_WARN_LIMIT:
	case PMBUS_VIN_UV_FAULT_LIMIT:
	case PMBUS_VOUT_UV_WARN_LIMIT:
	case PMBUS_VOUT_OV_WARN_LIMIT:
	case PMBUS_VIN_OV_WARN_LIMIT:
	case PMBUS_IIN_OC_FAULT_LIMIT:
	case PMBUS_IOUT_OC_LV_FAULT_LIMIT:
	case PMBUS_IOUT_OC_WARN_LIMIT:
	case PMBUS_IOUT_UC_FAULT_LIMIT:
	case PMBUS_POUT_OP_FAULT_LIMIT:
	case PMBUS_POUT_OP_WARN_LIMIT:
	case PMBUS_PIN_OP_WARN_LIMIT:
		return -ENXIO;
	default:
		return -ENODATA;
	}

	return ret;
}

static int mp2973_write_word_data(struct i2c_client *client, int page,
				  int reg, u16 word)
{
	u8 target, mask;
	long ret;

	if (reg != PMBUS_SMBALERT_MASK)
		return -ENODATA;

	/*
	 * Vendor-specific SMBALERT_MASK register with 16 maskable bits.
	 */
	ret = pmbus_read_word_data(client, 0, 0, PMBUS_SMBALERT_MASK);
	if (ret < 0)
		return ret;

	target = word & 0xff;
	mask = word >> 8;

/*
 * Set/Clear 'bit' in 'ret' based on condition followed by define for each bit in SMBALERT_MASK.
 * Also bit 2 & 15 are reserved.
 */

#define MP2973_TEMP_OT		0
#define MP2973_VIN_UVLO		1
#define MP2973_VIN_OVP		3
#define MP2973_MTP_FAULT	4
#define MP2973_OTHER_COMM	5
#define MP2973_MTP_BLK_TRIG	6
#define MP2973_PACKET_ERROR	7
#define MP2973_INVALID_DATA	8
#define MP2973_INVALID_COMMAND	9
#define MP2973_IOUT_OC_LV	10
#define MP2973_IOUT_OC		11
#define MP2973_VOUT_MAX_MIN_WARNING 12
#define MP2973_VOLTAGE_UV	13
#define MP2973_VOLTAGE_OV	14

	switch (target) {
	case PMBUS_STATUS_CML:
		__assign_bit(MP2973_INVALID_DATA, &ret, !(mask & PB_CML_FAULT_INVALID_DATA));
		__assign_bit(MP2973_INVALID_COMMAND, &ret, !(mask & PB_CML_FAULT_INVALID_COMMAND));
		__assign_bit(MP2973_OTHER_COMM, &ret, !(mask & PB_CML_FAULT_OTHER_COMM));
		__assign_bit(MP2973_PACKET_ERROR, &ret, !(mask & PB_CML_FAULT_PACKET_ERROR));
		break;
	case PMBUS_STATUS_VOUT:
		__assign_bit(MP2973_VOLTAGE_UV, &ret, !(mask & PB_VOLTAGE_UV_FAULT));
		__assign_bit(MP2973_VOLTAGE_OV, &ret, !(mask & PB_VOLTAGE_OV_FAULT));
		break;
	case PMBUS_STATUS_IOUT:
		__assign_bit(MP2973_IOUT_OC, &ret, !(mask & PB_IOUT_OC_FAULT));
		__assign_bit(MP2973_IOUT_OC_LV, &ret, !(mask & PB_IOUT_OC_LV_FAULT));
		break;
	case PMBUS_STATUS_TEMPERATURE:
		__assign_bit(MP2973_TEMP_OT, &ret, !(mask & PB_TEMP_OT_FAULT));
		break;
	/*
	 * Map remaining bits to MFR specific to let the PMBUS core mask
	 * those bits by default.
	 */
	case PMBUS_STATUS_MFR_SPECIFIC:
		__assign_bit(MP2973_VIN_UVLO, &ret, !(mask & BIT(1)));
		__assign_bit(MP2973_VIN_OVP, &ret, !(mask & BIT(3)));
		__assign_bit(MP2973_MTP_FAULT, &ret, !(mask & BIT(4)));
		__assign_bit(MP2973_MTP_BLK_TRIG, &ret, !(mask & BIT(6)));
		break;
	default:
		return 0;
	}

	return pmbus_write_word_data(client, 0, PMBUS_SMBALERT_MASK, ret);
}

static int mp2975_read_word_data(struct i2c_client *client, int page,
				 int phase, int reg)
{
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	struct mp2975_data *data = to_mp2975_data(info);
	int ret;

	switch (reg) {
	case PMBUS_OT_FAULT_LIMIT:
		ret = mp2975_read_word_helper(client, page, phase, reg,
					      GENMASK(7, 0));
		break;
	case PMBUS_VIN_OV_FAULT_LIMIT:
		ret = mp2975_read_word_helper(client, page, phase, reg,
					      GENMASK(7, 0));
		if (ret < 0)
			return ret;

		ret = DIV_ROUND_CLOSEST(ret, MP2975_VIN_UV_LIMIT_UNIT);
		break;
	case PMBUS_VOUT_OV_FAULT_LIMIT:
		/*
		 * Register provides two values for over-voltage protection
		 * threshold for fixed (ovp2) and tracking (ovp1) modes. The
		 * minimum of these two values is provided as over-voltage
		 * fault alarm.
		 */
		ret = mp2975_read_word_helper(client, page, phase,
					      MP2975_MFR_OVP_TH_SET,
					      GENMASK(2, 0));
		if (ret < 0)
			return ret;

		ret = min_t(int, data->vout_max[page] + 50 * (ret + 1),
			    data->vout_ov_fixed[page]);
		break;
	case PMBUS_VOUT_UV_FAULT_LIMIT:
		ret = mp2975_read_word_helper(client, page, phase,
					      MP2975_MFR_UVP_SET,
					      GENMASK(2, 0));
		if (ret < 0)
			return ret;

		ret = DIV_ROUND_CLOSEST(data->vref[page] * 10 - 50 *
					(ret + 1) * data->vout_scale, 10);
		break;
	case PMBUS_VIRT_READ_POUT_MAX:
		ret = mp2975_read_word_helper(client, page, phase,
					      MP2975_MFR_READ_POUT_PK,
					      GENMASK(12, 0));
		if (ret < 0)
			return ret;

		ret = DIV_ROUND_CLOSEST(ret, 4);
		break;
	case PMBUS_VIRT_READ_IOUT_MAX:
		ret = mp2975_read_word_helper(client, page, phase,
					      MP2975_MFR_READ_IOUT_PK,
					      GENMASK(12, 0));
		if (ret < 0)
			return ret;

		ret = DIV_ROUND_CLOSEST(ret, 4);
		break;
	case PMBUS_READ_IOUT:
		ret = mp2975_read_phases(client, data, page, phase);
		if (ret < 0)
			return ret;

		break;
	case PMBUS_UT_WARN_LIMIT:
	case PMBUS_UT_FAULT_LIMIT:
	case PMBUS_VIN_UV_WARN_LIMIT:
	case PMBUS_VIN_UV_FAULT_LIMIT:
	case PMBUS_VOUT_UV_WARN_LIMIT:
	case PMBUS_VOUT_OV_WARN_LIMIT:
	case PMBUS_VIN_OV_WARN_LIMIT:
	case PMBUS_IIN_OC_FAULT_LIMIT:
	case PMBUS_IOUT_OC_LV_FAULT_LIMIT:
	case PMBUS_IIN_OC_WARN_LIMIT:
	case PMBUS_IOUT_OC_WARN_LIMIT:
	case PMBUS_IOUT_OC_FAULT_LIMIT:
	case PMBUS_IOUT_UC_FAULT_LIMIT:
	case PMBUS_POUT_OP_FAULT_LIMIT:
	case PMBUS_POUT_OP_WARN_LIMIT:
	case PMBUS_PIN_OP_WARN_LIMIT:
		return -ENXIO;
	default:
		return -ENODATA;
	}

	return ret;
}

static int mp2975_identify_multiphase_rail2(struct i2c_client *client,
					    struct mp2975_data *data)
{
	int ret;

	/*
	 * Identify multiphase for rail 2 - could be from 0 to data->max_phases[1].
	 * In case phase number is zero – only page zero is supported
	 */
	ret = i2c_smbus_write_byte_data(client, PMBUS_PAGE, 2);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_read_word_data(client, MP2975_MFR_VR_MULTI_CONFIG_R2);
	if (ret < 0)
		return ret;

	ret &= GENMASK(2, 0);
	return (ret >= data->max_phases[1]) ? data->max_phases[1] : ret;
}

static void mp2975_set_phase_rail1(struct pmbus_driver_info *info)
{
	int i;

	for (i = 0 ; i < info->phases[0]; i++)
		info->pfunc[i] = PMBUS_HAVE_IOUT;
}

static void
mp2975_set_phase_rail2(struct pmbus_driver_info *info, int num_phases)
{
	int i;

	/* Set phases for rail 2 from upper to lower. */
	for (i = 1; i <= num_phases; i++)
		info->pfunc[MP2975_MAX_PHASE_RAIL1 - i] = PMBUS_HAVE_IOUT;
}

static int
mp2975_identify_multiphase(struct i2c_client *client, struct mp2975_data *data,
			   struct pmbus_driver_info *info)
{
	int num_phases2, ret;

	ret = i2c_smbus_write_byte_data(client, PMBUS_PAGE, 2);
	if (ret < 0)
		return ret;

	/* Identify multiphase for rail 1 - could be from 1 to data->max_phases[0]. */
	ret = i2c_smbus_read_word_data(client, MP2975_MFR_VR_MULTI_CONFIG_R1);
	if (ret <= 0)
		return ret;

	info->phases[0] = ret & GENMASK(3, 0);

	/*
	 * The device provides a total of $n PWM pins, and can be configured
	 * to different phase count applications for rail 1 and rail 2.
	 * Rail 1 can be set to $n phases, while rail 2 can be set to less than
	 * that. When rail 1’s phase count is configured as 0, rail
	 * 1 operates with 1-phase DCM. When rail 2 phase count is configured
	 * as 0, rail 2 is disabled.
	 */
	if (info->phases[0] > data->max_phases[0])
		return -EINVAL;

	if (data->chip_id == mp2975) {
		mp2975_set_phase_rail1(info);
		num_phases2 = min(data->max_phases[0] - info->phases[0],
				  data->max_phases[1]);
		if (info->phases[1] && info->phases[1] <= num_phases2)
			mp2975_set_phase_rail2(info, num_phases2);
	}

	return 0;
}

static int
mp2975_identify_vid(struct i2c_client *client, struct mp2975_data *data,
		    struct pmbus_driver_info *info, u32 reg, int page,
		    u32 imvp_bit, u32 vr_bit)
{
	int ret;

	/* Identify VID mode and step selection. */
	ret = i2c_smbus_read_word_data(client, reg);
	if (ret < 0)
		return ret;

	if (ret & imvp_bit) {
		info->vrm_version[page] = imvp9;
		data->vid_step[page] = MP2975_PROT_DEV_OV_OFF;
	} else if (ret & vr_bit) {
		info->vrm_version[page] = vr12;
		data->vid_step[page] = MP2975_PROT_DEV_OV_ON;
	} else {
		info->vrm_version[page] = vr13;
		data->vid_step[page] = MP2975_PROT_DEV_OV_OFF;
	}

	return 0;
}

static int
mp2975_identify_rails_vid(struct i2c_client *client, struct mp2975_data *data,
			  struct pmbus_driver_info *info)
{
	int ret;

	ret = i2c_smbus_write_byte_data(client, PMBUS_PAGE, 2);
	if (ret < 0)
		return ret;

	/* Identify VID mode for rail 1. */
	ret = mp2975_identify_vid(client, data, info,
				  MP2975_MFR_VR_MULTI_CONFIG_R1, 0,
				  MP2975_IMVP9_EN_R1, MP2975_VID_STEP_SEL_R1);
	if (ret < 0)
		return ret;

	/* Identify VID mode for rail 2, if connected. */
	if (info->phases[1])
		ret = mp2975_identify_vid(client, data, info,
					  MP2975_MFR_VR_MULTI_CONFIG_R2, 1,
					  MP2975_IMVP9_EN_R2,
					  MP2975_VID_STEP_SEL_R2);

	return ret;
}

static int
mp2973_identify_rails_vid(struct i2c_client *client, struct mp2975_data *data,
			  struct pmbus_driver_info *info)
{
	int ret;

	ret = i2c_smbus_write_byte_data(client, PMBUS_PAGE, 2);
	if (ret < 0)
		return ret;

	/* Identify VID mode for rail 1. */
	ret = mp2975_identify_vid(client, data, info,
				  MP2973_MFR_VR_MULTI_CONFIG_R1, 0,
				  MP2973_IMVP9_EN_R1, MP2973_VID_STEP_SEL_R1);

	if (ret < 0)
		return ret;

	/* Identify VID mode for rail 2, if connected. */
	if (info->phases[1])
		ret = mp2975_identify_vid(client, data, info,
					  MP2973_MFR_VR_MULTI_CONFIG_R2, 1,
					  MP2973_IMVP9_EN_R2,
					  MP2973_VID_STEP_SEL_R2);

	return ret;
}

static int
mp2975_current_sense_gain_get(struct i2c_client *client,
			      struct mp2975_data *data)
{
	int i, ret;

	/*
	 * Obtain DrMOS current sense gain of power stage from the register
	 * MP2975_MFR_VR_CONFIG1, bits 13-12. The value is selected as below:
	 * 00b - 5µA/A, 01b - 8.5µA/A, 10b - 9.7µA/A, 11b - 10µA/A. Other
	 * values are invalid.
	 */
	for (i = 0 ; i < data->info.pages; i++) {
		ret = i2c_smbus_write_byte_data(client, PMBUS_PAGE, i);
		if (ret < 0)
			return ret;
		ret = i2c_smbus_read_word_data(client,
					       MP2975_MFR_VR_CONFIG1);
		if (ret < 0)
			return ret;

		switch ((ret & MP2975_DRMOS_KCS) >> 12) {
		case 0:
			data->curr_sense_gain[i] = 50;
			break;
		case 1:
			data->curr_sense_gain[i] = 85;
			break;
		case 2:
			data->curr_sense_gain[i] = 97;
			break;
		default:
			data->curr_sense_gain[i] = 100;
			break;
		}
	}

	return 0;
}

static int
mp2975_vref_get(struct i2c_client *client, struct mp2975_data *data,
		struct pmbus_driver_info *info)
{
	int ret;

	ret = i2c_smbus_write_byte_data(client, PMBUS_PAGE, 3);
	if (ret < 0)
		return ret;

	/* Get voltage reference value for rail 1. */
	ret = i2c_smbus_read_word_data(client, MP2975_MFR_READ_VREF_R1);
	if (ret < 0)
		return ret;

	data->vref[0] = ret * data->vid_step[0];

	/* Get voltage reference value for rail 2, if connected. */
	if (data->info.pages == MP2975_PAGE_NUM) {
		ret = i2c_smbus_read_word_data(client, MP2975_MFR_READ_VREF_R2);
		if (ret < 0)
			return ret;

		data->vref[1] = ret * data->vid_step[1];
	}
	return 0;
}

static int
mp2975_vref_offset_get(struct i2c_client *client, struct mp2975_data *data,
		       int page)
{
	int ret;

	ret = i2c_smbus_read_word_data(client, MP2975_MFR_OVP_TH_SET);
	if (ret < 0)
		return ret;

	switch ((ret & GENMASK(5, 3)) >> 3) {
	case 1:
		data->vref_off[page] = 140;
		break;
	case 2:
		data->vref_off[page] = 220;
		break;
	case 4:
		data->vref_off[page] = 400;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int
mp2975_vout_max_get(struct i2c_client *client, struct mp2975_data *data,
		    struct pmbus_driver_info *info, int page)
{
	int ret;

	/* Get maximum reference voltage of VID-DAC in VID format. */
	ret = i2c_smbus_read_word_data(client, PMBUS_VOUT_MAX);
	if (ret < 0)
		return ret;

	data->vout_max[page] = mp2975_vid2direct(info->vrm_version[page], ret &
						 GENMASK(8, 0));
	return 0;
}

static int
mp2975_set_vout_format(struct i2c_client *client,
		       struct mp2975_data *data, int page)
{
	int ret, i;

	/* Enable DIRECT VOUT format 1mV/LSB */
	if (data->chip_id == mp2975) {
		ret = i2c_smbus_read_word_data(client, MP2975_MFR_DC_LOOP_CTRL);
		if (ret < 0)
			return ret;
		if (ret & MP2975_VOUT_FORMAT) {
			ret &= ~MP2975_VOUT_FORMAT;
			ret = i2c_smbus_write_word_data(client, MP2975_MFR_DC_LOOP_CTRL, ret);
		}
	} else {
		ret = i2c_smbus_read_word_data(client, MP2973_MFR_RESO_SET);
		if (ret < 0)
			return ret;
		i = ret;

		if (page == 0) {
			i &= ~MP2973_VOUT_FORMAT_R1;
			i |= MP2973_VOUT_FORMAT_DIRECT_R1;
		} else {
			i &= ~MP2973_VOUT_FORMAT_R2;
			i |= MP2973_VOUT_FORMAT_DIRECT_R2;
		}
		if (i != ret)
			ret = i2c_smbus_write_word_data(client, MP2973_MFR_RESO_SET, i);
	}
	return ret;
}

static int
mp2975_vout_ov_scale_get(struct i2c_client *client, struct mp2975_data *data,
			 struct pmbus_driver_info *info)
{
	int thres_dev, sense_ampl, ret;

	ret = i2c_smbus_write_byte_data(client, PMBUS_PAGE, 0);
	if (ret < 0)
		return ret;

	/*
	 * Get divider for over- and under-voltage protection thresholds
	 * configuration from the Advanced Options of Auto Phase Shedding and
	 * decay register.
	 */
	ret = i2c_smbus_read_word_data(client, MP2975_MFR_APS_DECAY_ADV);
	if (ret < 0)
		return ret;
	thres_dev = ret & MP2975_PRT_THRES_DIV_OV_EN ? MP2975_PROT_DEV_OV_ON :
						       MP2975_PROT_DEV_OV_OFF;

	/* Select the gain of remote sense amplifier. */
	ret = i2c_smbus_read_word_data(client, PMBUS_VOUT_SCALE_LOOP);
	if (ret < 0)
		return ret;
	sense_ampl = ret & MP2975_SENSE_AMPL ? MP2975_SENSE_AMPL_HALF :
					       MP2975_SENSE_AMPL_UNIT;

	data->vout_scale = sense_ampl * thres_dev;

	return 0;
}

static int
mp2975_vout_per_rail_config_get(struct i2c_client *client,
				struct mp2975_data *data,
				struct pmbus_driver_info *info)
{
	int i, ret;

	for (i = 0; i < data->info.pages; i++) {
		ret = i2c_smbus_write_byte_data(client, PMBUS_PAGE, i);
		if (ret < 0)
			continue;

		/* Set VOUT format for READ_VOUT command : direct. */
		ret = mp2975_set_vout_format(client, data, i);
		if (ret < 0)
			return ret;

		/* Obtain maximum voltage values. */
		ret = mp2975_vout_max_get(client, data, info, i);
		if (ret < 0)
			return ret;

		/* Skip if reading Vref is unsupported */
		if (data->chip_id != mp2975)
			continue;

		/* Obtain voltage reference offsets. */
		ret = mp2975_vref_offset_get(client, data, i);
		if (ret < 0)
			return ret;

		/*
		 * Set over-voltage fixed value. Thresholds are provided as
		 * fixed value, and tracking value. The minimum of them are
		 * exposed as over-voltage critical threshold.
		 */
		data->vout_ov_fixed[i] = data->vref[i] +
					 DIV_ROUND_CLOSEST(data->vref_off[i] *
							   data->vout_scale,
							   10);
	}

	return 0;
}

static const struct pmbus_driver_info mp2975_info = {
	.pages = 1,
	.format[PSC_VOLTAGE_IN] = linear,
	.format[PSC_VOLTAGE_OUT] = direct,
	.format[PSC_TEMPERATURE] = direct,
	.format[PSC_CURRENT_IN] = linear,
	.format[PSC_CURRENT_OUT] = direct,
	.format[PSC_POWER] = direct,
	.m[PSC_TEMPERATURE] = 1,
	.m[PSC_VOLTAGE_OUT] = 1,
	.R[PSC_VOLTAGE_OUT] = 3,
	.m[PSC_CURRENT_OUT] = 1,
	.m[PSC_POWER] = 1,
	.func[0] = PMBUS_HAVE_VIN | PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT |
		PMBUS_HAVE_IIN | PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT |
		PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP | PMBUS_HAVE_POUT |
		PMBUS_HAVE_PIN | PMBUS_HAVE_STATUS_INPUT | PMBUS_PHASE_VIRTUAL,
	.read_byte_data = mp2975_read_byte_data,
	.read_word_data = mp2975_read_word_data,
#if IS_ENABLED(CONFIG_SENSORS_MP2975_REGULATOR)
	.num_regulators = 1,
	.reg_desc = mp2975_reg_desc,
#endif
};

static const struct pmbus_driver_info mp2973_info = {
	.pages = 1,
	.format[PSC_VOLTAGE_IN] = linear,
	.format[PSC_VOLTAGE_OUT] = direct,
	.format[PSC_TEMPERATURE] = linear,
	.format[PSC_CURRENT_IN] = linear,
	.format[PSC_CURRENT_OUT] = linear,
	.format[PSC_POWER] = linear,
	.m[PSC_VOLTAGE_OUT] = 1,
	.R[PSC_VOLTAGE_OUT] = 3,
	.func[0] = PMBUS_HAVE_VIN | PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT |
		PMBUS_HAVE_IIN | PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT |
		PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP | PMBUS_HAVE_POUT |
		PMBUS_HAVE_PIN | PMBUS_HAVE_STATUS_INPUT,
	.read_word_data = mp2973_read_word_data,
	.write_word_data = mp2973_write_word_data,
#if IS_ENABLED(CONFIG_SENSORS_MP2975_REGULATOR)
	.num_regulators = 1,
	.reg_desc = mp2975_reg_desc,
#endif
};

static const struct mp2975_driver_info mp2975_ddinfo[] = {
	[mp2975] = { .info = &mp2975_info, .chip_id = mp2975 },
	[mp2973] = { .info = &mp2973_info, .chip_id = mp2973 },
	[mp2971] = { .info = &mp2973_info, .chip_id = mp2971 },
};

static int mp2975_probe(struct i2c_client *client)
{
	const struct mp2975_driver_info *ddinfo;
	struct pmbus_driver_info *info;
	struct mp2975_data *data;
	int ret;

	ddinfo = i2c_get_match_data(client);
	if (!ddinfo)
		return -ENODEV;

	data = devm_kzalloc(&client->dev, sizeof(struct mp2975_data),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->chip_id = ddinfo->chip_id;

	memcpy(data->max_phases, mp2975_max_phases[data->chip_id],
	       sizeof(data->max_phases));

	memcpy(&data->info, ddinfo->info, sizeof(data->info));

	info = &data->info;

	/* Identify multiphase configuration for rail 2. */
	ret = mp2975_identify_multiphase_rail2(client, data);
	if (ret < 0)
		return ret;

	if (ret) {
		/* Two rails are connected. */
		data->info.pages = MP2975_PAGE_NUM;
		data->info.phases[1] = ret;
		data->info.func[1] = MP2975_RAIL2_FUNC;
		if (IS_ENABLED(CONFIG_SENSORS_MP2975_REGULATOR))
			data->info.num_regulators = MP2975_PAGE_NUM;
	}

	/* Identify multiphase configuration. */
	ret = mp2975_identify_multiphase(client, data, info);
	if (ret)
		return ret;

	if (data->chip_id == mp2975) {
		/* Identify VID setting per rail. */
		ret = mp2975_identify_rails_vid(client, data, info);
		if (ret < 0)
			return ret;

		/* Obtain current sense gain of power stage. */
		ret = mp2975_current_sense_gain_get(client, data);
		if (ret)
			return ret;

		/* Obtain voltage reference values. */
		ret = mp2975_vref_get(client, data, info);
		if (ret)
			return ret;

		/* Obtain vout over-voltage scales. */
		ret = mp2975_vout_ov_scale_get(client, data, info);
		if (ret < 0)
			return ret;
	} else {
		/* Identify VID setting per rail. */
		ret = mp2973_identify_rails_vid(client, data, info);
		if (ret < 0)
			return ret;
	}

	/* Obtain offsets, maximum and format for vout. */
	ret = mp2975_vout_per_rail_config_get(client, data, info);
	if (ret)
		return ret;

	return pmbus_do_probe(client, info);
}

static const struct of_device_id mp2975_of_match[] = {
	{.compatible = "mps,mp2971", .data = &mp2975_ddinfo[mp2971]},
	{.compatible = "mps,mp2973", .data = &mp2975_ddinfo[mp2973]},
	{.compatible = "mps,mp2975", .data = &mp2975_ddinfo[mp2975]},
	{}
};
MODULE_DEVICE_TABLE(of, mp2975_of_match);

static const struct i2c_device_id mp2975_id[] = {
	{"mp2971", (kernel_ulong_t)&mp2975_ddinfo[mp2971]},
	{"mp2973", (kernel_ulong_t)&mp2975_ddinfo[mp2973]},
	{"mp2975", (kernel_ulong_t)&mp2975_ddinfo[mp2975]},
	{}
};
MODULE_DEVICE_TABLE(i2c, mp2975_id);

static struct i2c_driver mp2975_driver = {
	.driver = {
		.name = "mp2975",
		.of_match_table = mp2975_of_match,
	},
	.probe = mp2975_probe,
	.id_table = mp2975_id,
};

module_i2c_driver(mp2975_driver);

MODULE_AUTHOR("Vadim Pasternak <vadimp@nvidia.com>");
MODULE_DESCRIPTION("PMBus driver for MPS MP2975 device");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("PMBUS");
