// SPDX-License-Identifier: GPL-2.0+
/*
 * DA7280 Haptic device driver
 *
 * Copyright (c) 2020 Dialog Semiconductor.
 * Author: Roy Im <Roy.Im.Opensource@diasemi.com>
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/pwm.h>
#include <linux/regmap.h>
#include <linux/workqueue.h>
#include <linux/uaccess.h>

/* Registers */
#define DA7280_IRQ_EVENT1			0x03
#define DA7280_IRQ_EVENT_WARNING_DIAG		0x04
#define DA7280_IRQ_EVENT_SEQ_DIAG		0x05
#define DA7280_IRQ_STATUS1			0x06
#define DA7280_IRQ_MASK1			0x07
#define DA7280_FRQ_LRA_PER_H			0x0A
#define DA7280_FRQ_LRA_PER_L			0x0B
#define DA7280_ACTUATOR1			0x0C
#define DA7280_ACTUATOR2			0x0D
#define DA7280_ACTUATOR3			0x0E
#define DA7280_CALIB_V2I_H			0x0F
#define DA7280_CALIB_V2I_L			0x10
#define DA7280_TOP_CFG1				0x13
#define DA7280_TOP_CFG2				0x14
#define DA7280_TOP_CFG4				0x16
#define DA7280_TOP_INT_CFG1			0x17
#define DA7280_TOP_CTL1				0x22
#define DA7280_TOP_CTL2				0x23
#define DA7280_SEQ_CTL2				0x28
#define DA7280_GPI_0_CTL			0x29
#define DA7280_GPI_1_CTL			0x2A
#define DA7280_GPI_2_CTL			0x2B
#define DA7280_MEM_CTL1				0x2C
#define DA7280_MEM_CTL2				0x2D
#define DA7280_TOP_CFG5				0x6E
#define DA7280_IRQ_MASK2			0x83
#define DA7280_SNP_MEM_99			0xE7

/* Register field */

/* DA7280_IRQ_EVENT1 (Address 0x03) */
#define DA7280_E_SEQ_CONTINUE_MASK		BIT(0)
#define DA7280_E_UVLO_MASK			BIT(1)
#define DA7280_E_SEQ_DONE_MASK			BIT(2)
#define DA7280_E_OVERTEMP_CRIT_MASK		BIT(3)
#define DA7280_E_SEQ_FAULT_MASK			BIT(4)
#define DA7280_E_WARNING_MASK			BIT(5)
#define DA7280_E_ACTUATOR_FAULT_MASK		BIT(6)
#define DA7280_E_OC_FAULT_MASK			BIT(7)

/* DA7280_IRQ_EVENT_WARNING_DIAG (Address 0x04) */
#define DA7280_E_OVERTEMP_WARN_MASK             BIT(3)
#define DA7280_E_MEM_TYPE_MASK                  BIT(4)
#define DA7280_E_LIM_DRIVE_ACC_MASK             BIT(6)
#define DA7280_E_LIM_DRIVE_MASK                 BIT(7)

/* DA7280_IRQ_EVENT_PAT_DIAG (Address 0x05) */
#define DA7280_E_PWM_FAULT_MASK			BIT(5)
#define DA7280_E_MEM_FAULT_MASK			BIT(6)
#define DA7280_E_SEQ_ID_FAULT_MASK		BIT(7)

/* DA7280_IRQ_STATUS1 (Address 0x06) */
#define DA7280_STA_SEQ_CONTINUE_MASK		BIT(0)
#define DA7280_STA_UVLO_VBAT_OK_MASK		BIT(1)
#define DA7280_STA_SEQ_DONE_MASK		BIT(2)
#define DA7280_STA_OVERTEMP_CRIT_MASK		BIT(3)
#define DA7280_STA_SEQ_FAULT_MASK		BIT(4)
#define DA7280_STA_WARNING_MASK			BIT(5)
#define DA7280_STA_ACTUATOR_MASK		BIT(6)
#define DA7280_STA_OC_MASK			BIT(7)

/* DA7280_IRQ_MASK1 (Address 0x07) */
#define DA7280_SEQ_CONTINUE_M_MASK		BIT(0)
#define DA7280_E_UVLO_M_MASK			BIT(1)
#define DA7280_SEQ_DONE_M_MASK			BIT(2)
#define DA7280_OVERTEMP_CRIT_M_MASK		BIT(3)
#define DA7280_SEQ_FAULT_M_MASK			BIT(4)
#define DA7280_WARNING_M_MASK			BIT(5)
#define DA7280_ACTUATOR_M_MASK			BIT(6)
#define DA7280_OC_M_MASK			BIT(7)

/* DA7280_ACTUATOR3 (Address 0x0e) */
#define DA7280_IMAX_MASK			GENMASK(4, 0)

/* DA7280_TOP_CFG1 (Address 0x13) */
#define DA7280_AMP_PID_EN_MASK			BIT(0)
#define DA7280_RAPID_STOP_EN_MASK		BIT(1)
#define DA7280_ACCELERATION_EN_MASK		BIT(2)
#define DA7280_FREQ_TRACK_EN_MASK		BIT(3)
#define DA7280_BEMF_SENSE_EN_MASK		BIT(4)
#define DA7280_ACTUATOR_TYPE_MASK		BIT(5)

/* DA7280_TOP_CFG2 (Address 0x14) */
#define DA7280_FULL_BRAKE_THR_MASK		GENMASK(3, 0)
#define DA7280_MEM_DATA_SIGNED_MASK		BIT(4)

/* DA7280_TOP_CFG4 (Address 0x16) */
#define DA7280_TST_CALIB_IMPEDANCE_DIS_MASK	BIT(6)
#define DA7280_V2I_FACTOR_FREEZE_MASK		BIT(7)

/* DA7280_TOP_INT_CFG1 (Address 0x17) */
#define DA7280_BEMF_FAULT_LIM_MASK		GENMASK(1, 0)

/* DA7280_TOP_CTL1 (Address 0x22) */
#define DA7280_OPERATION_MODE_MASK		GENMASK(2, 0)
#define DA7280_STANDBY_EN_MASK			BIT(3)
#define DA7280_SEQ_START_MASK			BIT(4)

/* DA7280_SEQ_CTL2 (Address 0x28) */
#define DA7280_PS_SEQ_ID_MASK			GENMASK(3, 0)
#define DA7280_PS_SEQ_LOOP_MASK			GENMASK(7, 4)

/* DA7280_GPIO_0_CTL (Address 0x29) */
#define DA7280_GPI0_POLARITY_MASK		GENMASK(1, 0)
#define DA7280_GPI0_MODE_MASK			BIT(2)
#define DA7280_GPI0_SEQUENCE_ID_MASK		GENMASK(6, 3)

/* DA7280_GPIO_1_CTL (Address 0x2a) */
#define DA7280_GPI1_POLARITY_MASK		GENMASK(1, 0)
#define DA7280_GPI1_MODE_MASK			BIT(2)
#define DA7280_GPI1_SEQUENCE_ID_MASK		GENMASK(6, 3)

/* DA7280_GPIO_2_CTL (Address 0x2b) */
#define DA7280_GPI2_POLARITY_MASK		GENMASK(1, 0)
#define DA7280_GPI2_MODE_MASK			BIT(2)
#define DA7280_GPI2_SEQUENCE_ID_MASK		GENMASK(6, 3)

/* DA7280_MEM_CTL2 (Address 0x2d) */
#define DA7280_WAV_MEM_LOCK_MASK		BIT(7)

/* DA7280_TOP_CFG5 (Address 0x6e) */
#define DA7280_V2I_FACTOR_OFFSET_EN_MASK	BIT(0)

/* DA7280_IRQ_MASK2 (Address 0x83) */
#define DA7280_ADC_SAT_M_MASK			BIT(7)

/* Controls */

#define DA7280_VOLTAGE_RATE_MAX			6000000
#define DA7280_VOLTAGE_RATE_STEP		23400
#define DA7280_NOMMAX_DFT			0x6B
#define DA7280_ABSMAX_DFT			0x78

#define DA7280_IMPD_MAX				1500000000
#define DA7280_IMPD_DEFAULT			22000000

#define DA7280_IMAX_DEFAULT			0x0E
#define DA7280_IMAX_STEP			7200
#define DA7280_IMAX_LIMIT			252000

#define DA7280_RESONT_FREQH_DFT			0x39
#define DA7280_RESONT_FREQL_DFT			0x32
#define DA7280_MIN_RESONAT_FREQ_HZ		50
#define DA7280_MAX_RESONAT_FREQ_HZ		300

#define DA7280_SEQ_ID_MAX			15
#define DA7280_SEQ_LOOP_MAX			15
#define DA7280_GPI_SEQ_ID_DFT			0
#define DA7280_GPI_SEQ_ID_MAX			2

#define DA7280_SNP_MEM_SIZE			100
#define DA7280_SNP_MEM_MAX			DA7280_SNP_MEM_99

#define DA7280_IRQ_NUM				3

#define DA7280_SKIP_INIT			0x100

#define DA7280_FF_EFFECT_COUNT_MAX		15

/* Maximum gain is 0x7fff for PWM mode */
#define DA7280_MAX_MAGNITUDE_SHIFT		15

enum da7280_haptic_dev_t {
	DA7280_LRA	= 0,
	DA7280_ERM_BAR	= 1,
	DA7280_ERM_COIN	= 2,
	DA7280_DEV_MAX,
};

enum da7280_op_mode {
	DA7280_INACTIVE		= 0,
	DA7280_DRO_MODE		= 1,
	DA7280_PWM_MODE		= 2,
	DA7280_RTWM_MODE	= 3,
	DA7280_ETWM_MODE	= 4,
	DA7280_OPMODE_MAX,
};

#define DA7280_FF_CONSTANT_DRO			1
#define DA7280_FF_PERIODIC_PWM			2
#define DA7280_FF_PERIODIC_RTWM			1
#define DA7280_FF_PERIODIC_ETWM			2

#define DA7280_FF_PERIODIC_MODE			DA7280_RTWM_MODE
#define DA7280_FF_CONSTANT_MODE			DA7280_DRO_MODE

enum da7280_custom_effect_param {
	DA7280_CUSTOM_SEQ_ID_IDX	= 0,
	DA7280_CUSTOM_SEQ_LOOP_IDX	= 1,
	DA7280_CUSTOM_DATA_LEN		= 2,
};

enum da7280_custom_gpi_effect_param {
	DA7280_CUSTOM_GPI_SEQ_ID_IDX	= 0,
	DA7280_CUSTOM_GPI_NUM_IDX	= 2,
	DA7280_CUSTOM_GP_DATA_LEN	= 3,
};

struct da7280_gpi_ctl {
	u8 seq_id;
	u8 mode;
	u8 polarity;
};

struct da7280_haptic {
	struct regmap *regmap;
	struct input_dev *input_dev;
	struct device *dev;
	struct i2c_client *client;
	struct pwm_device *pwm_dev;

	bool legacy;
	struct work_struct work;
	int val;
	u16 gain;
	s16 level;

	u8 dev_type;
	u8 op_mode;
	u8 const_op_mode;
	u8 periodic_op_mode;
	u16 nommax;
	u16 absmax;
	u32 imax;
	u32 impd;
	u32 resonant_freq_h;
	u32 resonant_freq_l;
	bool bemf_sense_en;
	bool freq_track_en;
	bool acc_en;
	bool rapid_stop_en;
	bool amp_pid_en;
	u8 ps_seq_id;
	u8 ps_seq_loop;
	struct da7280_gpi_ctl gpi_ctl[3];
	bool mem_update;
	u8 snp_mem[DA7280_SNP_MEM_SIZE];
	bool active;
	bool suspended;
};

static bool da7280_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case DA7280_IRQ_EVENT1:
	case DA7280_IRQ_EVENT_WARNING_DIAG:
	case DA7280_IRQ_EVENT_SEQ_DIAG:
	case DA7280_IRQ_STATUS1:
	case DA7280_TOP_CTL1:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config da7280_haptic_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = DA7280_SNP_MEM_MAX,
	.volatile_reg = da7280_volatile_register,
};

static int da7280_haptic_mem_update(struct da7280_haptic *haptics)
{
	unsigned int val;
	int error;

	/* The patterns should be updated when haptic is not working */
	error = regmap_read(haptics->regmap, DA7280_IRQ_STATUS1, &val);
	if (error)
		return error;
	if (val & DA7280_STA_WARNING_MASK) {
		dev_warn(haptics->dev,
			 "Warning! Please check HAPTIC status.\n");
		return -EBUSY;
	}

	/* Patterns are not updated if the lock bit is enabled */
	val = 0;
	error = regmap_read(haptics->regmap, DA7280_MEM_CTL2, &val);
	if (error)
		return error;
	if (~val & DA7280_WAV_MEM_LOCK_MASK) {
		dev_warn(haptics->dev, "Please unlock the bit first\n");
		return -EACCES;
	}

	/* Set to Inactive mode to make sure safety */
	error = regmap_update_bits(haptics->regmap,
				   DA7280_TOP_CTL1,
				   DA7280_OPERATION_MODE_MASK,
				   0);
	if (error)
		return error;

	error = regmap_read(haptics->regmap, DA7280_MEM_CTL1, &val);
	if (error)
		return error;

	return regmap_bulk_write(haptics->regmap, val, haptics->snp_mem,
				 DA7280_SNP_MEM_MAX - val + 1);
}

static int da7280_haptic_set_pwm(struct da7280_haptic *haptics, bool enabled)
{
	struct pwm_state state;
	u64 period_mag_multi;
	int error;

	if (!haptics->gain && enabled) {
		dev_err(haptics->dev, "Unable to enable pwm with 0 gain\n");
		return -EINVAL;
	}

	pwm_get_state(haptics->pwm_dev, &state);
	state.enabled = enabled;
	if (enabled) {
		period_mag_multi = (u64)state.period * haptics->gain;
		period_mag_multi >>= DA7280_MAX_MAGNITUDE_SHIFT;

		/*
		 * The interpretation of duty cycle depends on the acc_en,
		 * it should be between 50% and 100% for acc_en = 0.
		 * See datasheet 'PWM mode' section.
		 */
		if (!haptics->acc_en) {
			period_mag_multi += state.period;
			period_mag_multi /= 2;
		}

		state.duty_cycle = period_mag_multi;
	}

	error = pwm_apply_state(haptics->pwm_dev, &state);
	if (error)
		dev_err(haptics->dev, "Failed to apply pwm state: %d\n", error);

	return error;
}

static void da7280_haptic_activate(struct da7280_haptic *haptics)
{
	int error;

	if (haptics->active)
		return;

	switch (haptics->op_mode) {
	case DA7280_DRO_MODE:
		/* the valid range check when acc_en is enabled */
		if (haptics->acc_en && haptics->level > 0x7F)
			haptics->level = 0x7F;
		else if (haptics->level > 0xFF)
			haptics->level = 0xFF;

		/* Set level as a % of ACTUATOR_NOMMAX (nommax) */
		error = regmap_write(haptics->regmap, DA7280_TOP_CTL2,
				     haptics->level);
		if (error) {
			dev_err(haptics->dev,
				"Failed to set level to %d: %d\n",
				haptics->level, error);
			return;
		}
		break;

	case DA7280_PWM_MODE:
		if (da7280_haptic_set_pwm(haptics, true))
			return;
		break;

	case DA7280_RTWM_MODE:
		/*
		 * The pattern will be played by the PS_SEQ_ID and the
		 * PS_SEQ_LOOP
		 */
		break;

	case DA7280_ETWM_MODE:
		/*
		 * The pattern will be played by the GPI[N] state,
		 * GPI(N)_SEQUENCE_ID and the PS_SEQ_LOOP. See the
		 * datasheet for the details.
		 */
		break;

	default:
		dev_err(haptics->dev, "Invalid op mode %d\n", haptics->op_mode);
		return;
	}

	error = regmap_update_bits(haptics->regmap,
				   DA7280_TOP_CTL1,
				   DA7280_OPERATION_MODE_MASK,
				   haptics->op_mode);
	if (error) {
		dev_err(haptics->dev,
			"Failed to set operation mode: %d", error);
		return;
	}

	if (haptics->op_mode == DA7280_PWM_MODE ||
	    haptics->op_mode == DA7280_RTWM_MODE) {
		error = regmap_update_bits(haptics->regmap,
					   DA7280_TOP_CTL1,
					   DA7280_SEQ_START_MASK,
					   DA7280_SEQ_START_MASK);
		if (error) {
			dev_err(haptics->dev,
				"Failed to start sequence: %d\n", error);
			return;
		}
	}

	haptics->active = true;
}

static void da7280_haptic_deactivate(struct da7280_haptic *haptics)
{
	int error;

	if (!haptics->active)
		return;

	/* Set to Inactive mode */
	error = regmap_update_bits(haptics->regmap,
				   DA7280_TOP_CTL1,
				   DA7280_OPERATION_MODE_MASK, 0);
	if (error) {
		dev_err(haptics->dev,
			"Failed to clear operation mode: %d", error);
		return;
	}

	switch (haptics->op_mode) {
	case DA7280_DRO_MODE:
		error = regmap_write(haptics->regmap,
				     DA7280_TOP_CTL2, 0);
		if (error) {
			dev_err(haptics->dev,
				"Failed to disable DRO mode: %d\n", error);
			return;
		}
		break;

	case DA7280_PWM_MODE:
		if (da7280_haptic_set_pwm(haptics, false))
			return;
		break;

	case DA7280_RTWM_MODE:
	case DA7280_ETWM_MODE:
		error = regmap_update_bits(haptics->regmap,
					   DA7280_TOP_CTL1,
					   DA7280_SEQ_START_MASK, 0);
		if (error) {
			dev_err(haptics->dev,
				"Failed to disable RTWM/ETWM mode: %d\n",
				error);
			return;
		}
		break;

	default:
		dev_err(haptics->dev, "Invalid op mode %d\n", haptics->op_mode);
		return;
	}

	haptics->active = false;
}

static void da7280_haptic_work(struct work_struct *work)
{
	struct da7280_haptic *haptics =
		container_of(work, struct da7280_haptic, work);
	int val = haptics->val;

	if (val)
		da7280_haptic_activate(haptics);
	else
		da7280_haptic_deactivate(haptics);
}

static int da7280_haptics_upload_effect(struct input_dev *dev,
					struct ff_effect *effect,
					struct ff_effect *old)
{
	struct da7280_haptic *haptics = input_get_drvdata(dev);
	s16 data[DA7280_SNP_MEM_SIZE] = { 0 };
	unsigned int val;
	int tmp, i, num;
	int error;

	/* The effect should be uploaded when haptic is not working */
	if (haptics->active)
		return -EBUSY;

	switch (effect->type) {
	/* DRO/PWM modes support this type */
	case FF_CONSTANT:
		haptics->op_mode = haptics->const_op_mode;
		if (haptics->op_mode == DA7280_DRO_MODE) {
			tmp = effect->u.constant.level * 254;
			haptics->level = tmp / 0x7FFF;
			break;
		}

		haptics->gain =	effect->u.constant.level <= 0 ?
					0 : effect->u.constant.level;
		break;

	/* RTWM/ETWM modes support this type */
	case FF_PERIODIC:
		if (effect->u.periodic.waveform != FF_CUSTOM) {
			dev_err(haptics->dev,
				"Device can only accept FF_CUSTOM waveform\n");
			return -EINVAL;
		}

		/*
		 * Load the data and check the length.
		 * the data will be patterns in this case: 4 < X <= 100,
		 * and will be saved into the waveform memory inside DA728x.
		 * If X = 2, the data will be PS_SEQ_ID and PS_SEQ_LOOP.
		 * If X = 3, the 1st data will be GPIX_SEQUENCE_ID .
		 */
		if (effect->u.periodic.custom_len == DA7280_CUSTOM_DATA_LEN)
			goto set_seq_id_loop;

		if (effect->u.periodic.custom_len == DA7280_CUSTOM_GP_DATA_LEN)
			goto set_gpix_seq_id;

		if (effect->u.periodic.custom_len < DA7280_CUSTOM_DATA_LEN ||
		    effect->u.periodic.custom_len > DA7280_SNP_MEM_SIZE) {
			dev_err(haptics->dev, "Invalid waveform data size\n");
			return -EINVAL;
		}

		if (copy_from_user(data, effect->u.periodic.custom_data,
				   sizeof(s16) *
				   effect->u.periodic.custom_len))
			return -EFAULT;

		memset(haptics->snp_mem, 0, DA7280_SNP_MEM_SIZE);

		for (i = 0; i < effect->u.periodic.custom_len; i++) {
			if (data[i] < 0 || data[i] > 0xff) {
				dev_err(haptics->dev,
					"Invalid waveform data %d at offset %d\n",
					data[i], i);
				return -EINVAL;
			}
			haptics->snp_mem[i] = (u8)data[i];
		}

		error = da7280_haptic_mem_update(haptics);
		if (error) {
			dev_err(haptics->dev,
				"Failed to upload waveform: %d\n", error);
			return error;
		}
		break;

set_seq_id_loop:
		if (copy_from_user(data, effect->u.periodic.custom_data,
				   sizeof(s16) * DA7280_CUSTOM_DATA_LEN))
			return -EFAULT;

		if (data[DA7280_CUSTOM_SEQ_ID_IDX] < 0 ||
		    data[DA7280_CUSTOM_SEQ_ID_IDX] > DA7280_SEQ_ID_MAX ||
		    data[DA7280_CUSTOM_SEQ_LOOP_IDX] < 0 ||
		    data[DA7280_CUSTOM_SEQ_LOOP_IDX] > DA7280_SEQ_LOOP_MAX) {
			dev_err(haptics->dev,
				"Invalid custom id (%d) or loop (%d)\n",
				data[DA7280_CUSTOM_SEQ_ID_IDX],
				data[DA7280_CUSTOM_SEQ_LOOP_IDX]);
			return -EINVAL;
		}

		haptics->ps_seq_id = data[DA7280_CUSTOM_SEQ_ID_IDX] & 0x0f;
		haptics->ps_seq_loop = data[DA7280_CUSTOM_SEQ_LOOP_IDX] & 0x0f;
		haptics->op_mode = haptics->periodic_op_mode;

		val = FIELD_PREP(DA7280_PS_SEQ_ID_MASK, haptics->ps_seq_id) |
			FIELD_PREP(DA7280_PS_SEQ_LOOP_MASK,
				   haptics->ps_seq_loop);
		error = regmap_write(haptics->regmap, DA7280_SEQ_CTL2, val);
		if (error) {
			dev_err(haptics->dev,
				"Failed to update PS sequence: %d\n", error);
			return error;
		}
		break;

set_gpix_seq_id:
		if (copy_from_user(data, effect->u.periodic.custom_data,
				   sizeof(s16) * DA7280_CUSTOM_GP_DATA_LEN))
			return -EFAULT;

		if (data[DA7280_CUSTOM_GPI_SEQ_ID_IDX] < 0 ||
		    data[DA7280_CUSTOM_GPI_SEQ_ID_IDX] > DA7280_SEQ_ID_MAX ||
		    data[DA7280_CUSTOM_GPI_NUM_IDX] < 0 ||
		    data[DA7280_CUSTOM_GPI_NUM_IDX] > DA7280_GPI_SEQ_ID_MAX) {
			dev_err(haptics->dev,
				"Invalid custom GPI id (%d) or num (%d)\n",
				data[DA7280_CUSTOM_GPI_SEQ_ID_IDX],
				data[DA7280_CUSTOM_GPI_NUM_IDX]);
			return -EINVAL;
		}

		num = data[DA7280_CUSTOM_GPI_NUM_IDX] & 0x0f;
		haptics->gpi_ctl[num].seq_id =
			data[DA7280_CUSTOM_GPI_SEQ_ID_IDX] & 0x0f;
		haptics->op_mode = haptics->periodic_op_mode;

		val = FIELD_PREP(DA7280_GPI0_SEQUENCE_ID_MASK,
				 haptics->gpi_ctl[num].seq_id);
		error = regmap_update_bits(haptics->regmap,
					   DA7280_GPI_0_CTL + num,
					   DA7280_GPI0_SEQUENCE_ID_MASK,
					   val);
		if (error) {
			dev_err(haptics->dev,
				"Failed to update GPI sequence: %d\n", error);
			return error;
		}
		break;

	default:
		dev_err(haptics->dev, "Unsupported effect type: %d\n",
			effect->type);
		return -EINVAL;
	}

	return 0;
}

static int da7280_haptics_playback(struct input_dev *dev,
				   int effect_id, int val)
{
	struct da7280_haptic *haptics = input_get_drvdata(dev);

	if (!haptics->op_mode) {
		dev_warn(haptics->dev, "No effects have been uploaded\n");
		return -EINVAL;
	}

	if (likely(!haptics->suspended)) {
		haptics->val = val;
		schedule_work(&haptics->work);
	}

	return 0;
}

static int da7280_haptic_start(struct da7280_haptic *haptics)
{
	int error;

	error = regmap_update_bits(haptics->regmap,
				   DA7280_TOP_CTL1,
				   DA7280_STANDBY_EN_MASK,
				   DA7280_STANDBY_EN_MASK);
	if (error) {
		dev_err(haptics->dev, "Unable to enable device: %d\n", error);
		return error;
	}

	return 0;
}

static void da7280_haptic_stop(struct da7280_haptic *haptics)
{
	int error;

	cancel_work_sync(&haptics->work);


	da7280_haptic_deactivate(haptics);

	error = regmap_update_bits(haptics->regmap, DA7280_TOP_CTL1,
				   DA7280_STANDBY_EN_MASK, 0);
	if (error)
		dev_err(haptics->dev, "Failed to disable device: %d\n", error);
}

static int da7280_haptic_open(struct input_dev *dev)
{
	struct da7280_haptic *haptics = input_get_drvdata(dev);

	return da7280_haptic_start(haptics);
}

static void da7280_haptic_close(struct input_dev *dev)
{
	struct da7280_haptic *haptics = input_get_drvdata(dev);

	da7280_haptic_stop(haptics);
}

static u8 da7280_haptic_of_mode_str(struct device *dev,
				    const char *str)
{
	if (!strcmp(str, "LRA")) {
		return DA7280_LRA;
	} else if (!strcmp(str, "ERM-bar")) {
		return DA7280_ERM_BAR;
	} else if (!strcmp(str, "ERM-coin")) {
		return DA7280_ERM_COIN;
	} else {
		dev_warn(dev, "Invalid string - set to LRA\n");
		return DA7280_LRA;
	}
}

static u8 da7280_haptic_of_gpi_mode_str(struct device *dev,
					const char *str)
{
	if (!strcmp(str, "Single-pattern")) {
		return 0;
	} else if (!strcmp(str, "Multi-pattern")) {
		return 1;
	} else {
		dev_warn(dev, "Invalid string - set to Single-pattern\n");
		return 0;
	}
}

static u8 da7280_haptic_of_gpi_pol_str(struct device *dev,
				       const char *str)
{
	if (!strcmp(str, "Rising-edge")) {
		return 0;
	} else if (!strcmp(str, "Falling-edge")) {
		return 1;
	} else if (!strcmp(str, "Both-edge")) {
		return 2;
	} else {
		dev_warn(dev, "Invalid string - set to Rising-edge\n");
		return 0;
	}
}

static u8 da7280_haptic_of_volt_rating_set(u32 val)
{
	u32 voltage = val / DA7280_VOLTAGE_RATE_STEP + 1;

	return min_t(u32, voltage, 0xff);
}

static void da7280_parse_properties(struct device *dev,
				    struct da7280_haptic *haptics)
{
	unsigned int i, mem[DA7280_SNP_MEM_SIZE];
	char gpi_str1[] = "dlg,gpi0-seq-id";
	char gpi_str2[] = "dlg,gpi0-mode";
	char gpi_str3[] = "dlg,gpi0-polarity";
	const char *str;
	u32 val;
	int error;

	/*
	 * If there is no property, then use the mode programmed into the chip.
	 */
	haptics->dev_type = DA7280_DEV_MAX;
	error = device_property_read_string(dev, "dlg,actuator-type", &str);
	if (!error)
		haptics->dev_type = da7280_haptic_of_mode_str(dev, str);

	haptics->const_op_mode = DA7280_DRO_MODE;
	error = device_property_read_u32(dev, "dlg,const-op-mode", &val);
	if (!error && val == DA7280_FF_PERIODIC_PWM)
		haptics->const_op_mode = DA7280_PWM_MODE;

	haptics->periodic_op_mode = DA7280_RTWM_MODE;
	error = device_property_read_u32(dev, "dlg,periodic-op-mode", &val);
	if (!error && val == DA7280_FF_PERIODIC_ETWM)
		haptics->periodic_op_mode = DA7280_ETWM_MODE;

	haptics->nommax = DA7280_SKIP_INIT;
	error = device_property_read_u32(dev, "dlg,nom-microvolt", &val);
	if (!error && val < DA7280_VOLTAGE_RATE_MAX)
		haptics->nommax = da7280_haptic_of_volt_rating_set(val);

	haptics->absmax = DA7280_SKIP_INIT;
	error = device_property_read_u32(dev, "dlg,abs-max-microvolt", &val);
	if (!error && val < DA7280_VOLTAGE_RATE_MAX)
		haptics->absmax = da7280_haptic_of_volt_rating_set(val);

	haptics->imax = DA7280_IMAX_DEFAULT;
	error = device_property_read_u32(dev, "dlg,imax-microamp", &val);
	if (!error && val < DA7280_IMAX_LIMIT)
		haptics->imax = (val - 28600) / DA7280_IMAX_STEP + 1;

	haptics->impd = DA7280_IMPD_DEFAULT;
	error = device_property_read_u32(dev, "dlg,impd-micro-ohms", &val);
	if (!error && val <= DA7280_IMPD_MAX)
		haptics->impd = val;

	haptics->resonant_freq_h = DA7280_SKIP_INIT;
	haptics->resonant_freq_l = DA7280_SKIP_INIT;
	error = device_property_read_u32(dev, "dlg,resonant-freq-hz", &val);
	if (!error) {
		if (val < DA7280_MAX_RESONAT_FREQ_HZ &&
		    val > DA7280_MIN_RESONAT_FREQ_HZ) {
			haptics->resonant_freq_h =
				((1000000000 / (val * 1333)) >> 7) & 0xFF;
			haptics->resonant_freq_l =
				(1000000000 / (val * 1333)) & 0x7F;
		} else {
			haptics->resonant_freq_h = DA7280_RESONT_FREQH_DFT;
			haptics->resonant_freq_l = DA7280_RESONT_FREQL_DFT;
		}
	}

	/* If no property, set to zero as default is to do nothing. */
	haptics->ps_seq_id = 0;
	error = device_property_read_u32(dev, "dlg,ps-seq-id", &val);
	if (!error && val <= DA7280_SEQ_ID_MAX)
		haptics->ps_seq_id = val;

	haptics->ps_seq_loop = 0;
	error = device_property_read_u32(dev, "dlg,ps-seq-loop", &val);
	if (!error && val <= DA7280_SEQ_LOOP_MAX)
		haptics->ps_seq_loop = val;

	/* GPI0~2 Control */
	for (i = 0; i <= DA7280_GPI_SEQ_ID_MAX; i++) {
		gpi_str1[7] = '0' + i;
		haptics->gpi_ctl[i].seq_id = DA7280_GPI_SEQ_ID_DFT + i;
		error = device_property_read_u32 (dev, gpi_str1, &val);
		if (!error && val <= DA7280_SEQ_ID_MAX)
			haptics->gpi_ctl[i].seq_id = val;

		gpi_str2[7] = '0' + i;
		haptics->gpi_ctl[i].mode = 0;
		error = device_property_read_string(dev, gpi_str2, &str);
		if (!error)
			haptics->gpi_ctl[i].mode =
				da7280_haptic_of_gpi_mode_str(dev, str);

		gpi_str3[7] = '0' + i;
		haptics->gpi_ctl[i].polarity = 0;
		error = device_property_read_string(dev, gpi_str3, &str);
		if (!error)
			haptics->gpi_ctl[i].polarity =
				da7280_haptic_of_gpi_pol_str(dev, str);
	}

	haptics->bemf_sense_en =
		device_property_read_bool(dev, "dlg,bemf-sens-enable");
	haptics->freq_track_en =
		device_property_read_bool(dev, "dlg,freq-track-enable");
	haptics->acc_en =
		device_property_read_bool(dev, "dlg,acc-enable");
	haptics->rapid_stop_en =
		device_property_read_bool(dev, "dlg,rapid-stop-enable");
	haptics->amp_pid_en =
		device_property_read_bool(dev, "dlg,amp-pid-enable");

	haptics->mem_update = false;
	error = device_property_read_u32_array(dev, "dlg,mem-array",
					       &mem[0], DA7280_SNP_MEM_SIZE);
	if (!error) {
		haptics->mem_update = true;
		memset(haptics->snp_mem, 0, DA7280_SNP_MEM_SIZE);
		for (i = 0; i < DA7280_SNP_MEM_SIZE; i++) {
			if (mem[i] <= 0xff) {
				haptics->snp_mem[i] = (u8)mem[i];
			} else {
				dev_err(haptics->dev,
					"Invalid data in mem-array at %d: %x\n",
					i, mem[i]);
				haptics->mem_update = false;
				break;
			}
		}
	}
}

static irqreturn_t da7280_irq_handler(int irq, void *data)
{
	struct da7280_haptic *haptics = data;
	struct device *dev = haptics->dev;
	u8 events[DA7280_IRQ_NUM];
	int error;

	/* Check what events have happened */
	error = regmap_bulk_read(haptics->regmap, DA7280_IRQ_EVENT1,
				 events, sizeof(events));
	if (error) {
		dev_err(dev, "failed to read interrupt data: %d\n", error);
		goto out;
	}

	/* Clear events */
	error = regmap_write(haptics->regmap, DA7280_IRQ_EVENT1, events[0]);
	if (error) {
		dev_err(dev, "failed to clear interrupts: %d\n", error);
		goto out;
	}

	if (events[0] & DA7280_E_SEQ_FAULT_MASK) {
		/*
		 * Stop first if haptic is active, otherwise, the fault may
		 * happen continually even though the bit is cleared.
		 */
		error = regmap_update_bits(haptics->regmap, DA7280_TOP_CTL1,
					   DA7280_OPERATION_MODE_MASK, 0);
		if (error)
			dev_err(dev, "failed to clear op mode on fault: %d\n",
				error);
	}

	if (events[0] & DA7280_E_SEQ_DONE_MASK)
		haptics->active = false;

	if (events[0] & DA7280_E_WARNING_MASK) {
		if (events[1] & DA7280_E_LIM_DRIVE_MASK ||
		    events[1] & DA7280_E_LIM_DRIVE_ACC_MASK)
			dev_warn(dev, "Please reduce the driver level\n");
		if (events[1] & DA7280_E_MEM_TYPE_MASK)
			dev_warn(dev, "Please check the mem data format\n");
		if (events[1] & DA7280_E_OVERTEMP_WARN_MASK)
			dev_warn(dev, "Over-temperature warning\n");
	}

	if (events[0] & DA7280_E_SEQ_FAULT_MASK) {
		if (events[2] & DA7280_E_SEQ_ID_FAULT_MASK)
			dev_info(dev, "Please reload PS_SEQ_ID & mem data\n");
		if (events[2] & DA7280_E_MEM_FAULT_MASK)
			dev_info(dev, "Please reload the mem data\n");
		if (events[2] & DA7280_E_PWM_FAULT_MASK)
			dev_info(dev, "Please restart PWM interface\n");
	}

out:
	return IRQ_HANDLED;
}

static int da7280_init(struct da7280_haptic *haptics)
{
	unsigned int val = 0;
	u32 v2i_factor;
	int error, i;
	u8 mask = 0;

	/*
	 * If device type is DA7280_DEV_MAX then simply use currently
	 * programmed mode.
	 */
	if (haptics->dev_type == DA7280_DEV_MAX) {
		error = regmap_read(haptics->regmap, DA7280_TOP_CFG1, &val);
		if (error)
			goto out_err;

		haptics->dev_type = val & DA7280_ACTUATOR_TYPE_MASK ?
					DA7280_ERM_COIN : DA7280_LRA;
	}

	/* Apply user settings */
	if (haptics->dev_type == DA7280_LRA &&
	    haptics->resonant_freq_l != DA7280_SKIP_INIT) {
		error = regmap_write(haptics->regmap, DA7280_FRQ_LRA_PER_H,
				     haptics->resonant_freq_h);
		if (error)
			goto out_err;
		error = regmap_write(haptics->regmap, DA7280_FRQ_LRA_PER_L,
				     haptics->resonant_freq_l);
		if (error)
			goto out_err;
	} else if (haptics->dev_type == DA7280_ERM_COIN) {
		error = regmap_update_bits(haptics->regmap, DA7280_TOP_INT_CFG1,
					   DA7280_BEMF_FAULT_LIM_MASK, 0);
		if (error)
			goto out_err;

		mask = DA7280_TST_CALIB_IMPEDANCE_DIS_MASK |
			DA7280_V2I_FACTOR_FREEZE_MASK;
		val = DA7280_TST_CALIB_IMPEDANCE_DIS_MASK |
			DA7280_V2I_FACTOR_FREEZE_MASK;
		error = regmap_update_bits(haptics->regmap, DA7280_TOP_CFG4,
					   mask, val);
		if (error)
			goto out_err;

		haptics->acc_en = false;
		haptics->rapid_stop_en = false;
		haptics->amp_pid_en = false;
	}

	mask = DA7280_ACTUATOR_TYPE_MASK |
			DA7280_BEMF_SENSE_EN_MASK |
			DA7280_FREQ_TRACK_EN_MASK |
			DA7280_ACCELERATION_EN_MASK |
			DA7280_RAPID_STOP_EN_MASK |
			DA7280_AMP_PID_EN_MASK;
	val = FIELD_PREP(DA7280_ACTUATOR_TYPE_MASK,
			 (haptics->dev_type ? 1 : 0)) |
		FIELD_PREP(DA7280_BEMF_SENSE_EN_MASK,
			   (haptics->bemf_sense_en ? 1 : 0)) |
		FIELD_PREP(DA7280_FREQ_TRACK_EN_MASK,
			   (haptics->freq_track_en ? 1 : 0)) |
		FIELD_PREP(DA7280_ACCELERATION_EN_MASK,
			   (haptics->acc_en ? 1 : 0)) |
		FIELD_PREP(DA7280_RAPID_STOP_EN_MASK,
			   (haptics->rapid_stop_en ? 1 : 0)) |
		FIELD_PREP(DA7280_AMP_PID_EN_MASK,
			   (haptics->amp_pid_en ? 1 : 0));

	error = regmap_update_bits(haptics->regmap, DA7280_TOP_CFG1, mask, val);
	if (error)
		goto out_err;

	error = regmap_update_bits(haptics->regmap, DA7280_TOP_CFG5,
				   DA7280_V2I_FACTOR_OFFSET_EN_MASK,
				   haptics->acc_en ?
					DA7280_V2I_FACTOR_OFFSET_EN_MASK : 0);
	if (error)
		goto out_err;

	error = regmap_update_bits(haptics->regmap,
				   DA7280_TOP_CFG2,
				   DA7280_MEM_DATA_SIGNED_MASK,
				   haptics->acc_en ?
					0 : DA7280_MEM_DATA_SIGNED_MASK);
	if (error)
		goto out_err;

	if (haptics->nommax != DA7280_SKIP_INIT) {
		error = regmap_write(haptics->regmap, DA7280_ACTUATOR1,
				     haptics->nommax);
		if (error)
			goto out_err;
	}

	if (haptics->absmax != DA7280_SKIP_INIT) {
		error = regmap_write(haptics->regmap, DA7280_ACTUATOR2,
				     haptics->absmax);
		if (error)
			goto out_err;
	}

	error = regmap_update_bits(haptics->regmap, DA7280_ACTUATOR3,
				   DA7280_IMAX_MASK, haptics->imax);
	if (error)
		goto out_err;

	v2i_factor = haptics->impd * (haptics->imax + 4) / 1610400;
	error = regmap_write(haptics->regmap, DA7280_CALIB_V2I_L,
			     v2i_factor & 0xff);
	if (error)
		goto out_err;
	error = regmap_write(haptics->regmap, DA7280_CALIB_V2I_H,
			     v2i_factor >> 8);
	if (error)
		goto out_err;

	error = regmap_update_bits(haptics->regmap,
				   DA7280_TOP_CTL1,
				   DA7280_STANDBY_EN_MASK,
				   DA7280_STANDBY_EN_MASK);
	if (error)
		goto out_err;

	if (haptics->mem_update) {
		error = da7280_haptic_mem_update(haptics);
		if (error)
			goto out_err;
	}

	/* Set  PS_SEQ_ID and PS_SEQ_LOOP */
	val = FIELD_PREP(DA7280_PS_SEQ_ID_MASK, haptics->ps_seq_id) |
		FIELD_PREP(DA7280_PS_SEQ_LOOP_MASK, haptics->ps_seq_loop);
	error = regmap_write(haptics->regmap, DA7280_SEQ_CTL2, val);
	if (error)
		goto out_err;

	/* GPI(N) CTL */
	for (i = 0; i < 3; i++) {
		val = FIELD_PREP(DA7280_GPI0_SEQUENCE_ID_MASK,
				 haptics->gpi_ctl[i].seq_id) |
			FIELD_PREP(DA7280_GPI0_MODE_MASK,
				   haptics->gpi_ctl[i].mode) |
			FIELD_PREP(DA7280_GPI0_POLARITY_MASK,
				   haptics->gpi_ctl[i].polarity);
		error = regmap_write(haptics->regmap,
				     DA7280_GPI_0_CTL + i, val);
		if (error)
			goto out_err;
	}

	/* Mask ADC_SAT_M bit as default */
	error = regmap_update_bits(haptics->regmap,
				   DA7280_IRQ_MASK2,
				   DA7280_ADC_SAT_M_MASK,
				   DA7280_ADC_SAT_M_MASK);
	if (error)
		goto out_err;

	/* Clear Interrupts */
	error = regmap_write(haptics->regmap, DA7280_IRQ_EVENT1, 0xff);
	if (error)
		goto out_err;

	error = regmap_update_bits(haptics->regmap,
				   DA7280_IRQ_MASK1,
				   DA7280_SEQ_FAULT_M_MASK |
					DA7280_SEQ_DONE_M_MASK,
				   0);
	if (error)
		goto out_err;

	haptics->active = false;
	return 0;

out_err:
	dev_err(haptics->dev, "chip initialization error: %d\n", error);
	return error;
}

static int da7280_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct da7280_haptic *haptics;
	struct input_dev *input_dev;
	struct pwm_state state;
	struct ff_device *ff;
	int error;

	if (!client->irq) {
		dev_err(dev, "No IRQ configured\n");
		return -EINVAL;
	}

	haptics = devm_kzalloc(dev, sizeof(*haptics), GFP_KERNEL);
	if (!haptics)
		return -ENOMEM;

	haptics->dev = dev;

	da7280_parse_properties(dev, haptics);

	if (haptics->const_op_mode == DA7280_PWM_MODE) {
		haptics->pwm_dev = devm_pwm_get(dev, NULL);
		error = PTR_ERR_OR_ZERO(haptics->pwm_dev);
		if (error) {
			if (error != -EPROBE_DEFER)
				dev_err(dev, "Unable to request PWM: %d\n",
					error);
			return error;
		}

		/* Sync up PWM state and ensure it is off. */
		pwm_init_state(haptics->pwm_dev, &state);
		state.enabled = false;
		error = pwm_apply_state(haptics->pwm_dev, &state);
		if (error) {
			dev_err(dev, "Failed to apply PWM state: %d\n", error);
			return error;
		}

		/*
		 * Check PWM period, PWM freq = 1000000 / state.period.
		 * The valid PWM freq range: 10k ~ 250kHz.
		 */
		if (state.period > 100000 || state.period < 4000) {
			dev_err(dev, "Unsupported PWM period: %lld\n",
				state.period);
			return -EINVAL;
		}
	}

	INIT_WORK(&haptics->work, da7280_haptic_work);

	haptics->client = client;
	i2c_set_clientdata(client, haptics);

	haptics->regmap = devm_regmap_init_i2c(client,
					       &da7280_haptic_regmap_config);
	error = PTR_ERR_OR_ZERO(haptics->regmap);
	if (error) {
		dev_err(dev, "Failed to allocate register map: %d\n", error);
		return error;
	}

	error = da7280_init(haptics);
	if (error) {
		dev_err(dev, "Failed to initialize device: %d\n", error);
		return error;
	}

	/* Initialize input device for haptic device */
	input_dev = devm_input_allocate_device(dev);
	if (!input_dev) {
		dev_err(dev, "Failed to allocate input device\n");
		return -ENOMEM;
	}

	input_dev->name = "da7280-haptic";
	input_dev->dev.parent = client->dev.parent;
	input_dev->open = da7280_haptic_open;
	input_dev->close = da7280_haptic_close;
	input_set_drvdata(input_dev, haptics);
	haptics->input_dev = input_dev;

	input_set_capability(haptics->input_dev, EV_FF, FF_PERIODIC);
	input_set_capability(haptics->input_dev, EV_FF, FF_CUSTOM);
	input_set_capability(haptics->input_dev, EV_FF, FF_CONSTANT);
	input_set_capability(haptics->input_dev, EV_FF, FF_GAIN);

	error = input_ff_create(haptics->input_dev,
				DA7280_FF_EFFECT_COUNT_MAX);
	if (error) {
		dev_err(dev, "Failed to create FF input device: %d\n", error);
		return error;
	}

	ff = input_dev->ff;
	ff->upload = da7280_haptics_upload_effect;
	ff->playback = da7280_haptics_playback;

	error = input_register_device(input_dev);
	if (error) {
		dev_err(dev, "Failed to register input device: %d\n", error);
		return error;
	}

	error = devm_request_threaded_irq(dev, client->irq,
					  NULL, da7280_irq_handler,
					  IRQF_ONESHOT,
					  "da7280-haptics", haptics);
	if (error) {
		dev_err(dev, "Failed to request IRQ %d: %d\n",
			client->irq, error);
		return error;
	}

	return 0;
}

static int da7280_suspend(struct device *dev)
{
	struct da7280_haptic *haptics = dev_get_drvdata(dev);

	mutex_lock(&haptics->input_dev->mutex);

	/*
	 * Make sure no new requests will be submitted while device is
	 * suspended.
	 */
	spin_lock_irq(&haptics->input_dev->event_lock);
	haptics->suspended = true;
	spin_unlock_irq(&haptics->input_dev->event_lock);

	da7280_haptic_stop(haptics);

	mutex_unlock(&haptics->input_dev->mutex);

	return 0;
}

static int da7280_resume(struct device *dev)
{
	struct da7280_haptic *haptics = dev_get_drvdata(dev);
	int retval;

	mutex_lock(&haptics->input_dev->mutex);

	retval = da7280_haptic_start(haptics);
	if (!retval) {
		spin_lock_irq(&haptics->input_dev->event_lock);
		haptics->suspended = false;
		spin_unlock_irq(&haptics->input_dev->event_lock);
	}

	mutex_unlock(&haptics->input_dev->mutex);
	return retval;
}

#ifdef CONFIG_OF
static const struct of_device_id da7280_of_match[] = {
	{ .compatible = "dlg,da7280", },
	{ }
};
MODULE_DEVICE_TABLE(of, da7280_of_match);
#endif

static const struct i2c_device_id da7280_i2c_id[] = {
	{ "da7280", },
	{ }
};
MODULE_DEVICE_TABLE(i2c, da7280_i2c_id);

static DEFINE_SIMPLE_DEV_PM_OPS(da7280_pm_ops, da7280_suspend, da7280_resume);

static struct i2c_driver da7280_driver = {
	.driver = {
		.name = "da7280",
		.of_match_table = of_match_ptr(da7280_of_match),
		.pm = pm_sleep_ptr(&da7280_pm_ops),
	},
	.probe_new = da7280_probe,
	.id_table = da7280_i2c_id,
};
module_i2c_driver(da7280_driver);

MODULE_DESCRIPTION("DA7280 haptics driver");
MODULE_AUTHOR("Roy Im <Roy.Im.Opensource@diasemi.com>");
MODULE_LICENSE("GPL");
