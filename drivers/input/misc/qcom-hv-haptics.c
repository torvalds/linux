// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */

#include <linux/atomic.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/nvmem-consumer.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/uaccess.h>

/* status register definitions in HAPTICS_CFG module */
#define HAP_CFG_STATUS_DATA_MSB_REG		0x09
/* STATUS_DATA_MSB definitions while MOD_STATUS_SEL is 0 */
#define AUTO_RES_CAL_DONE_BIT			BIT(5)
#define CAL_TLRA_CL_STS_MSB_MASK		GENMASK(4, 0)
/* STATUS_DATA_MSB definition in V2 while MOD_STATUS_SEL is 3 */
#define LAST_GOOD_TLRA_CL_MASK			GENMASK(4, 0)
/* STATUS_DATA_MSB definition in V1 while MOD_STATUS_SEL is 5 */
#define FIFO_REAL_TIME_FILL_STATUS_MASK_V1	GENMASK(6, 0)
/* STATUS DATA_MSB definition in V2 while MOD_STATUS_SEL is 5 */
#define FIFO_REAL_TIME_FILL_STATUS_MSB_MASK_V2	GENMASK(1, 0)

#define HAP_CFG_STATUS_DATA_LSB_REG		0x0A
/* STATUS_DATA_MSB definition while MOD_STATUS_SEL is 0 */
#define CAL_TLRA_CL_STS_LSB_MASK		GENMASK(7, 0)
/* STATUS_DATA_LSB definition in V2 while MOD_STATUS_SEL is 5 */
#define FIFO_REAL_TIME_FILL_STATUS_LSB_MASK_V2	GENMASK(7, 0)

#define HAP_CFG_FAULT_STATUS_REG		0x0C
#define SC_FLAG_BIT				BIT(2)
#define AUTO_RES_ERROR_BIT			BIT(1)
#define HPRW_RDY_FAULT_BIT			BIT(0)

#define HAP_CFG_INT_RT_STS_REG			0x10
#define FIFO_EMPTY_BIT				BIT(1)

/* config register definitions in HAPTICS_CFG module */
#define HAP_CFG_DRV_CTRL_REG			0x47
#define PSTG_DLY_MASK				GENMASK(7, 6)
#define DRV_SLEW_RATE_MASK			GENMASK(2, 0)

#define HAP_CFG_VMAX_REG			0x48
#define VMAX_STEP_MV				50
#define MAX_VMAX_MV				11000
#define CLAMPED_VMAX_MV				5000
#define DEFAULT_VMAX_MV				5000

#define HAP_CFG_DRV_WF_SEL_REG			0x49
#define DRV_WF_FMT_BIT				BIT(4)
#define DRV_WF_SEL_MASK				GENMASK(1, 0)

#define HAP_CFG_AUTO_SHUTDOWN_CFG_REG		0x4A
#define HAP_CFG_TRIG_PRIORITY_REG		0x4B

#define HAP_CFG_SPMI_PLAY_REG			0x4C
#define PLAY_EN_BIT				BIT(7)
#define BRAKE_EN_BIT				BIT(3)
#define PAT_SRC_MASK				GENMASK(2, 0)

#define HAP_CFG_EXT_TRIG_REG			0x4D

#define HAP_CFG_SWR_ACCESS_REG			0x4E
#define SWR_PAT_CFG_EN_BIT			BIT(7)
#define SWR_PAT_INPUT_EN_BIT			BIT(6)
#define SWR_PAT_RES_N_BIT			BIT(5)

#define HAP_CFG_BRAKE_MODE_CFG_REG		0x50
#define BRAKE_MODE_MASK				GENMASK(7, 6)
#define BRAKE_MODE_SHIFT			6
#define BRAKE_SINE_GAIN_MASK			GENMASK(3, 2)
#define BRAKE_SINE_GAIN_SHIFT			2
#define BRAKE_WF_SEL_MASK			GENMASK(1, 0)

#define HAP_CFG_CL_BRAKE_CFG_REG		0x51
#define HAP_CFG_CL_BRAKE_CAL_PARAM_REG		0x52
#define HAP_CFG_CL_BRAKE_RSET_REG		0x53
#define HAP_CFG_PWM_CFG_REG			0x5A

#define HAP_CFG_TLRA_OL_HIGH_REG		0x5C
#define TLRA_OL_MSB_MASK			GENMASK(3, 0)
#define HAP_CFG_TLRA_OL_LOW_REG			0x5D
#define TLRA_OL_LSB_MASK			GENMASK(7, 0)
#define TLRA_STEP_US				5
#define TLRA_MAX_US				20475

#define HAP_CFG_RC_CLK_CAL_COUNT_MSB_REG	0x5E
#define RC_CLK_CAL_COUNT_MSB_MASK		GENMASK(1, 0)
#define HAP_CFG_RC_CLK_CAL_COUNT_LSB_REG	0x5F
#define RC_CLK_CAL_COUNT_LSB_MASK		GENMASK(7, 0)

#define HAP_CFG_DRV_DUTY_CFG_REG		0x60
#define HAP_CFG_ADT_DRV_DUTY_CFG_REG		0x61
#define HAP_CFG_ZX_WIND_CFG_REG			0x62

#define HAP_CFG_AUTORES_CFG_REG			0x63
#define AUTORES_EN_BIT				BIT(7)
#define AUTORES_EN_DLY_MASK			GENMASK(5, 2)
#define AUTORES_EN_DLY_1_CYCLE			0x2
#define AUTORES_EN_DLY_SHIFT			2
#define AUTORES_ERR_WINDOW_MASK			GENMASK(1, 0)
#define AUTORES_ERR_WINDOW_12P5_PERCENT		0x0
#define AUTORES_ERR_WINDOW_25_PERCENT		0x1
#define AUTORES_ERR_WINDOW_50_PERCENT		0x2
#define AUTORES_ERR_WINDOW_100_PERCENT		0x3

#define HAP_CFG_AUTORES_ERR_RECOVERY_REG	0x64
#define EN_HW_RECOVERY_BIT			BIT(1)
#define SW_ERR_DRV_FREQ_BIT			BIT(0)

#define HAP_CFG_FAULT_CLR_REG			0x66
#define SC_CLR_BIT				BIT(2)
#define AUTO_RES_ERR_CLR_BIT			BIT(1)
#define HPWR_RDY_FAULT_CLR_BIT			BIT(0)

#define HAP_CFG_VMAX_HDRM_REG			0x67
#define VMAX_HDRM_MASK				GENMASK(6, 0)
#define VMAX_HDRM_STEP_MV			50
#define VMAX_HDRM_MAX_MV			6350

#define HAP_CFG_MOD_STATUS_SEL_REG		0x70
#define MOD_STATUS_SEL_CAL_TLRA_CL_STS_VAL	0
#define MOD_STATUS_SEL_LAST_GOOD_TLRA_VAL	3
#define MOD_STATUS_SEL_FIFO_FILL_STATUS_VAL	5
#define MOD_STATUS_SEL_BRAKE_CAL_RNAT_RCAL_VAL	6

#define HAP_CFG_MOD_STATUS_XT_V2_REG		0x71
#define MOD_STATUS_XT_V2_FIFO_FILL_STATUS_VAL	0x80
#define MOD_STATUS_XT_SEL_LAST_GOOD_TLRA_VAL	0

#define HAP_CFG_CAL_EN_REG			0x72
#define CAL_RC_CLK_MASK				GENMASK(3, 2)
#define CAL_RC_CLK_SHIFT			2
#define CAL_RC_CLK_AUTO_VAL			1
#define CAL_RC_CLK_MANUAL_VAL			2

/* version register definitions for HAPTICS_PATTERN module */
#define HAP_PTN_REVISION2_REG			0x01
#define HAP_PTN_V1				0x1
#define HAP_PTN_V2				0x2

/* status register definition for HAPTICS_PATTERN module */
#define HAP_PTN_FIFO_READY_STS_REG		0x08
#define FIFO_READY_BIT				BIT(0)

#define HAP_PTN_NUM_PAT_REG			0x09

/* config register definition for HAPTICS_PATTERN module */
/* FIFO configuration registers in V1 chip */
#define HAP_PTN_V1_FIFO_DIN_MSB_REG		0x20
#define HAP_PTN_V1_FIFO_DIN_MSB_BIT		BIT(0)
#define HAP_PTN_V1_FIFO_DIN_LSB_REG		0x21
#define HAP_PTN_V1_FIFO_DIN_LSB_MASK		GENMASK(7, 0)

#define HAP_PTN_V1_FIFO_PLAY_RATE_REG		0x22
#define FIFO_PLAY_RATE_MASK			GENMASK(3, 0)

#define HAP_PTN_V1_FIFO_EMPTY_CFG_REG		0x23
#define EMPTY_THRESH_MASK			GENMASK(3, 0)
#define HAP_PTN_V1_FIFO_THRESH_LSB		4

#define HAP_PTN_V1_FIFO_DEPTH_CFG_REG		0x24

/* FIFO configuration registers in V2 chip */
#define HAP_PTN_V2_FIFO_DIN_0_REG		0x20
#define HAP_PTN_V2_FIFO_DIN_NUM			4

#define HAP_PTN_V2_FIFO_PLAY_RATE_REG		0x24
#define HAP_PTN_V2_FIFO_EMPTY_CFG_REG		0x2A
#define HAP_PTN_V2_FIFO_THRESH_LSB		40
#define HAP_PTN_V2_FIFO_DEPTH_CFG_REG		0x2B
#define HAP_PTN_V2_FIFO_DIN_1B_REG		0x2C

/* shared registers between V1 and V2 chips */
#define HAP_PTN_DIRECT_PLAY_REG			0x26
#define DIRECT_PLAY_MAX_AMPLITUDE		0xFF

#define HAP_PTN_AUTORES_CAL_CFG_REG		0x28

#define HAP_PTN_PTRN1_TLRA_MSB_REG		0x30
#define HAP_PTN_PTRN1_TLRA_LSB_REG		0x31
#define HAP_PTN_PTRN2_TLRA_MSB_REG		0x32
#define HAP_PTN_PTRN2_TLRA_LSB_REG		0x33

#define HAP_PTN_PTRN1_CFG_REG			0x34
#define PTRN_FLRA2X_SHIFT			7
#define PTRN_SAMPLE_PER_MASK			GENMASK(2, 0)

#define PTRN_AMP_MSB_MASK			BIT(0)
#define PTRN_AMP_LSB_MASK			GENMASK(7, 0)

#define HAP_PTN_PTRN2_CFG_REG			0x50

#define HAP_PTN_BRAKE_AMP_REG			0x70

/* register in HBOOST module */
#define HAP_BOOST_REVISION1			0x00
#define HAP_BOOST_REVISION2			0x01
#define HAP_BOOST_V0P0				0x0000
#define HAP_BOOST_V0P1				0x0001

#define HAP_BOOST_V0P0_CLAMP_REG		0xF1
#define HAP_BOOST_V0P1_CLAMP_REG		0x70
#define CLAMP_5V_BIT				BIT(0)

/* constant parameters */
#define SAMPLES_PER_PATTERN			8
#define BRAKE_SAMPLE_COUNT			8
#define DEFAULT_ERM_PLAY_RATE_US		5000
#define MAX_EFFECT_COUNT			64
#define FIFO_READY_TIMEOUT_MS			1000
#define CHAR_PER_PATTERN_S			48
#define CHAR_PER_SAMPLE				8
#define CHAR_MSG_HEADER				16
#define CHAR_BRAKE_MODE				24
#define HW_BRAKE_CYCLES				5
#define F_LRA_VARIATION_HZ			5

#define MAX_FIFO_SAMPLES(chip)		\
	((chip)->ptn_revision == HAP_PTN_V1 ? 104 : 640)
#define FIFO_EMPTY_THRESHOLD(chip)	\
	((chip)->ptn_revision == HAP_PTN_V1 ? 48 : 280)
#define is_between(val, min, max)	\
	(((min) <= (max)) && ((min) <= (val)) && ((val) <= (max)))
#define HAP_BOOST_CLAMP_5V_REG_OFFSET(chip)	\
	((chip)->hbst_revision == HAP_BOOST_V0P0 ? \
	 HAP_BOOST_V0P0_CLAMP_REG : HAP_BOOST_V0P1_CLAMP_REG)

enum drv_sig_shape {
	WF_SQUARE,
	WF_SINE,
	WF_NO_MODULATION,
	WF_RESERVED,
};

enum brake_mode {
	OL_BRAKE,
	CL_BRAKE,
	PREDICT_BRAKE,
	AUTO_BRAKE,
};

enum brake_sine_gain {
	BRAKE_SINE_GAIN_X1,
	BRAKE_SINE_GAIN_X2,
	BRAKE_SINE_GAIN_X4,
	BRAKE_SINE_GAIN_X8,
};

enum pattern_src {
	FIFO,
	DIRECT_PLAY,
	PATTERN1,
	PATTERN2,
	SWR,
	SRC_RESERVED,
};

enum s_period {
	T_LRA = 0,
	T_LRA_DIV_2,
	T_LRA_DIV_4,
	T_LRA_DIV_8,
	T_LRA_X_2,
	T_LRA_X_4,
	T_LRA_X_8,
	T_RESERVED,
	/* F_xKHZ definitions are for FIFO only */
	F_8KHZ,
	F_16KHZ,
	F_24KHZ,
	F_32KHZ,
	F_44P1KHZ,
	F_48KHZ,
	F_RESERVED,
};

enum custom_effect_param {
	CUSTOM_DATA_EFFECT_IDX,
	CUSTOM_DATA_TIMEOUT_SEC_IDX,
	CUSTOM_DATA_TIMEOUT_MSEC_IDX,
	CUSTOM_DATA_LEN,
};

static const char * const src_str[] = {
	"FIFO",
	"DIRECT_PLAY",
	"PATTERN1",
	"PATTERN2",
	"SWR",
	"reserved",
};

static const char * const brake_str[] = {
	"open-loop-brake",
	"close-loop-brake",
	"predictive-brake",
	"auto-brake",
};

static const char * const period_str[] = {
	"T_LRA",
	"T_LRA_DIV_2",
	"T_LRA_DIV_4",
	"T_LRA_DIV_8",
	"T_LRA_X_2",
	"T_LRA_X_4",
	"T_LRA_X_8",
	"reserved_1",
	"F_8KHZ",
	"F_16HKZ",
	"F_24KHZ",
	"F_32KHZ",
	"F_44P1KHZ",
	"F_48KHZ",
	"reserved_2",
};

struct pattern_s {
	u16			amplitude;
	enum s_period		period;
	bool			f_lra_x2;
};

struct pattern_cfg {
	struct pattern_s	samples[SAMPLES_PER_PATTERN];
	u32			play_rate_us;
	u32			play_length_us;
	bool			preload;
};

struct fifo_cfg {
	u8			*samples;
	u32			num_s;
	enum s_period		period_per_s;
	u32			play_length_us;
};

struct brake_cfg {
	u8			samples[BRAKE_SAMPLE_COUNT];
	enum brake_mode		mode;
	enum drv_sig_shape	brake_wf;
	enum brake_sine_gain	sine_gain;
	u32			play_length_us;
	bool			disabled;
};

struct haptics_effect {
	struct pattern_cfg	*pattern;
	struct fifo_cfg		*fifo;
	struct brake_cfg	*brake;
	u32			id;
	u32			vmax_mv;
	u32			t_lra_us;
	enum pattern_src	src;
	bool			auto_res_disable;
};

/**
 * struct fifo_play_status - Data used for recording the FIFO playing status
 *
 * @samples_written:	The number of the samples that has been written into
 *			FIFO memory.
 * @written_done:	The flag to indicate if all of the FIFO samples has
 *			been written to the FIFO memory.
 * @is_busy:		The flag to indicate if it's in the middle of FIFO
 *			playing.
 * @cancelled:		The flag to indicate if FIFO playing is cancelled due
 *			to a stopping command arrived in the middle of playing.
 */
struct fifo_play_status {
	u32			samples_written;
	atomic_t		written_done;
	atomic_t		is_busy;
	atomic_t		cancelled;
};

struct haptics_play_info {
	struct haptics_effect	*effect;
	struct brake_cfg	*brake;
	struct fifo_play_status	fifo_status;
	struct mutex		lock;
	u32			vmax_mv;
	u32			length_us;
	enum pattern_src	pattern_src;
	bool			in_calibration;
};

struct haptics_hw_config {
	struct brake_cfg	brake;
	u32			vmax_mv;
	u32			t_lra_us;
	u32			cl_t_lra_us;
	u32			preload_effect;
	u32			fifo_empty_thresh;
	enum drv_sig_shape	drv_wf;
	bool			is_erm;
};

struct custom_fifo_data {
	u32	idx;
	u32	length;
	u32	play_rate_hz;
	u8	*data;
};

struct haptics_chip {
	struct device			*dev;
	struct regmap			*regmap;
	struct input_dev		*input_dev;
	struct haptics_hw_config	config;
	struct haptics_effect		*effects;
	struct haptics_effect		*custom_effect;
	struct haptics_play_info	play;
	struct dentry			*debugfs_dir;
	struct regulator_dev		*swr_slave_rdev;
	struct mutex			irq_lock;
	struct nvmem_cell		*cl_brake_nvmem;
	struct class			hap_class;
	int				fifo_empty_irq;
	u32				effects_count;
	u32				cfg_addr_base;
	u32				ptn_addr_base;
	u32				hbst_addr_base;
	u8				ptn_revision;
	u16				hbst_revision;
	bool				fifo_empty_irq_en;
	bool				swr_slave_enabled;
	bool				clamp_at_5v;
};

static int haptics_read(struct haptics_chip *chip,
		u16 base, u8 offset, u8 *val, u32 length)
{
	int rc = 0;
	u16 addr = base + offset;

	rc = regmap_bulk_read(chip->regmap, addr, val, length);
	if (rc < 0)
		dev_err(chip->dev, "read addr %d failed, rc=%d\n", addr, rc);

	return rc;
}

static int haptics_write(struct haptics_chip *chip,
		u16 base, u8 offset, u8 *val, u32 length)
{
	int rc = 0;
	u16 addr = base + offset;

	rc = regmap_bulk_write(chip->regmap, addr, val, length);
	if (rc < 0)
		dev_err(chip->dev, "write addr %d failed, rc=%d\n", addr, rc);

	return rc;
}

static int haptics_masked_write(struct haptics_chip *chip,
		u16 base, u8 offset, u8 mask, u8 val)
{
	int rc = 0;
	u16 addr = base + offset;

	regmap_update_bits(chip->regmap, addr, mask, val);
	if (rc < 0)
		dev_err(chip->dev, "update addr %d failed, rc=%d\n", addr, rc);

	return rc;
}

static void __dump_effects(struct haptics_chip *chip)
{
	struct haptics_effect *effect;
	struct pattern_s *sample;
	char *str;
	u32 size, pos;
	int i, j;

	for (i = 0; i < chip->effects_count; i++) {
		effect = &chip->effects[i];
		if (!effect)
			return;

		dev_dbg(chip->dev, "effect %d\n", effect->id);
		dev_dbg(chip->dev, "vmax_mv = %d\n", effect->vmax_mv);
		if (effect->pattern) {
			for (j = 0; j < SAMPLES_PER_PATTERN; j++) {
				sample = &effect->pattern->samples[j];
				dev_dbg(chip->dev, "pattern = %d, period = %s, f_lra_x2 = %d\n",
						sample->amplitude,
						period_str[sample->period],
						sample->f_lra_x2);
			}

			dev_dbg(chip->dev, "pattern play_rate_us = %d\n",
					effect->pattern->play_rate_us);
			dev_dbg(chip->dev, "pattern play_length_us = %d\n",
					effect->pattern->play_length_us);
			dev_dbg(chip->dev, "pattern preload = %d\n",
					effect->pattern->preload);
		}

		if (effect->fifo) {
			size = effect->fifo->num_s * CHAR_PER_SAMPLE
							+ CHAR_MSG_HEADER;
			str = kzalloc(size, GFP_KERNEL);
			if (str == NULL)
				return;

			pos = 0;
			pos += scnprintf(str, size, "%s", "FIFO data: ");
			for (j = 0; j < effect->fifo->num_s; j++)
				pos += scnprintf(str + pos, size - pos, "%d ",
						(s8)effect->fifo->samples[j]);

			dev_dbg(chip->dev, "%s\n", str);
			kfree(str);
			dev_dbg(chip->dev, "FIFO data play rate: %s\n",
					period_str[effect->fifo->period_per_s]);
			dev_dbg(chip->dev, "FIFO data play length: %dus\n",
					effect->fifo->play_length_us);
		}

		if (effect->brake && !effect->brake->disabled) {
			size = BRAKE_SAMPLE_COUNT * CHAR_PER_SAMPLE
						+ CHAR_MSG_HEADER;
			str = kzalloc(size, GFP_KERNEL);
			if (str == NULL)
				return;

			pos = 0;
			pos += scnprintf(str, size, "%s", "brake pattern: ");
			for (j = 0; j < BRAKE_SAMPLE_COUNT; j++)
				pos += scnprintf(str + pos, size - pos, "%#x ",
						effect->brake->samples[j]);

			dev_dbg(chip->dev, "%s\n", str);
			kfree(str);
			dev_dbg(chip->dev, "brake mode: %s\n",
					brake_str[effect->brake->mode]);
			dev_dbg(chip->dev, "brake play length: %dus\n",
					effect->brake->play_length_us);
		}

		dev_dbg(chip->dev, "pattern src: %s\n", src_str[effect->src]);
		dev_dbg(chip->dev, "auto resonance %s\n",
				effect->auto_res_disable ?
				"disabled" : "enabled");
	}
}

static void verify_brake_samples(struct brake_cfg *brake)
{
	int i;

	if (brake->mode == PREDICT_BRAKE || brake->mode == AUTO_BRAKE)
		return;

	for (i = BRAKE_SAMPLE_COUNT - 1; i > 0; i--) {
		if (brake->samples[i] != 0) {
			brake->disabled = false;
			return;
		}
	}

	brake->disabled = true;
}

static int get_pattern_play_length_us(struct pattern_cfg *pattern)
{
	int i = SAMPLES_PER_PATTERN - 1, j;
	u32 us_per_sample, total_length_us = 0;

	if (!pattern)
		return -EINVAL;

	for (; i >= 0; i--)
		if (pattern->samples[i].amplitude != 0)
			break;

	for (j = 0; j < i; j++) {
		us_per_sample = pattern->play_rate_us;
		switch (pattern->samples[j].period) {
		case T_LRA:
			break;
		case T_LRA_DIV_2:
			us_per_sample /= 2;
			break;
		case T_LRA_DIV_4:
			us_per_sample /= 4;
			break;
		case T_LRA_DIV_8:
			us_per_sample /= 8;
			break;
		case T_LRA_X_2:
			us_per_sample *= 2;
			break;
		case T_LRA_X_4:
			us_per_sample *= 4;
			break;
		case T_LRA_X_8:
			us_per_sample *= 8;
			break;
		default:
			return -EINVAL;
		}

		if (pattern->samples[j].f_lra_x2)
			us_per_sample /= 2;

		total_length_us += us_per_sample;
	}

	return total_length_us;
}

static int get_fifo_play_length_us(struct fifo_cfg *fifo, u32 t_lra_us)
{
	u32 length_us;
	int i;

	if (!fifo)
		return -EINVAL;

	for (i = fifo->num_s; i > 0; i--)
		if (fifo->samples[i] != 0)
			break;

	length_us = (i + 1) * t_lra_us;
	switch (fifo->period_per_s) {
	case T_LRA:
		break;
	case T_LRA_DIV_2:
		length_us /= 2;
		break;
	case T_LRA_DIV_4:
		length_us /= 4;
		break;
	case T_LRA_DIV_8:
		length_us /= 8;
		break;
	case F_8KHZ:
		length_us = 1000 * fifo->num_s / 8;
		break;
	case F_16KHZ:
		length_us = 1000 * fifo->num_s / 16;
		break;
	case F_24KHZ:
		length_us = 1000 * fifo->num_s / 24;
		break;
	case F_32KHZ:
		length_us = 1000 * fifo->num_s / 32;
		break;
	case F_44P1KHZ:
		length_us = 10000 * fifo->num_s / 441;
		break;
	case F_48KHZ:
		length_us = 1000 * fifo->num_s / 48;
		break;
	default:
		length_us = -EINVAL;
		break;
	}

	return length_us;
}

static int get_brake_play_length_us(struct brake_cfg *brake, u32 t_lra_us)
{
	int i = BRAKE_SAMPLE_COUNT - 1;

	if (!brake || brake->disabled)
		return 0;

	if (brake->mode == PREDICT_BRAKE || brake->mode == AUTO_BRAKE)
		return HW_BRAKE_CYCLES * t_lra_us;

	for (; i >= 0; i--)
		if (brake->samples[i] != 0)
			break;

	return t_lra_us * (i + 1);
}

#define V1_CL_TLRA_STEP_AUTO_RES_CAL_NOT_DONE_NS	5000
#define V1_CL_TLRA_STEP_AUTO_RES_CAL_DONE_NS		3333
#define AUTO_CAL_CLK_SCALE_DEN				1000
#define AUTO_CAL_CLK_SCALE_NUM				1024
static int haptics_adjust_lra_period(struct haptics_chip *chip, u32 *t_lra_us)
{
	int rc;
	u8 val;

	rc = haptics_read(chip, chip->cfg_addr_base,
			HAP_CFG_CAL_EN_REG, &val, 1);
	if (rc < 0)
		return rc;

	if ((val & CAL_RC_CLK_MASK) ==
			(CAL_RC_CLK_AUTO_VAL << CAL_RC_CLK_SHIFT))
		*t_lra_us = div_u64((u64)(*t_lra_us) * AUTO_CAL_CLK_SCALE_NUM,
				AUTO_CAL_CLK_SCALE_DEN);

	return 0;
}

static int haptics_get_closeloop_lra_period_v1(
		struct haptics_chip *chip)
{
	struct haptics_hw_config *config = &chip->config;
	int rc;
	u8 val[2];
	u32 tmp, step_ns;
	bool auto_res_cal_done;

	val[0] = MOD_STATUS_SEL_CAL_TLRA_CL_STS_VAL;
	rc = haptics_write(chip, chip->cfg_addr_base,
		HAP_CFG_MOD_STATUS_SEL_REG, val, 1);
	if (rc < 0)
		return rc;

	rc = haptics_read(chip, chip->cfg_addr_base,
		HAP_CFG_STATUS_DATA_MSB_REG, val, 2);
	if (rc < 0)
		return rc;

	/*
	 * Calculate the closed loop T_LRA with the following equations
	 * for PM8350B V1:
	 * LAST_GOOD_TLRA_CL_STS[12:0] = STATUS_DATA_MSB[4:0] |
	 *			STATUS_DATA_LSB[7:0]
	 * CL_TLRA_STEP_US = STATUS_DATA_MSB[5] ? 3.333 us : 5 us
	 * LAST_GOOD_TLRA_CL = LAST_GOOD_TLRA_CL_STS[12:0] *
	 *			CL_TLRA_STEP_US
	 */
	auto_res_cal_done = !!(val[0] & AUTO_RES_CAL_DONE_BIT);
	if (auto_res_cal_done)
		step_ns = V1_CL_TLRA_STEP_AUTO_RES_CAL_DONE_NS;
	else
		step_ns = V1_CL_TLRA_STEP_AUTO_RES_CAL_NOT_DONE_NS;

	tmp = ((val[0] & CAL_TLRA_CL_STS_MSB_MASK) << 8) | val[1];
	config->cl_t_lra_us = (tmp * step_ns) / 1000;

	return 0;
}

#define CL_TLRA_STEP_NS				1666
static int haptics_get_closeloop_lra_period_v2(
		struct haptics_chip *chip)
{
	struct haptics_hw_config *config = &chip->config;
	int rc;
	u8 val[2];
	u32 tmp;

	val[0] = MOD_STATUS_SEL_LAST_GOOD_TLRA_VAL;
	val[1] = MOD_STATUS_XT_SEL_LAST_GOOD_TLRA_VAL;
	rc = haptics_write(chip, chip->cfg_addr_base,
			HAP_CFG_MOD_STATUS_SEL_REG, val, 2);
	if (rc < 0)
		return rc;

	rc = haptics_read(chip, chip->cfg_addr_base,
			HAP_CFG_STATUS_DATA_MSB_REG, val, 2);
	if (rc < 0)
		return rc;
	/*
	 * Calculate the closed loop T_LRA with the following equations
	 * for PM8350B V2:
	 * LAST_GOOD_TLRA_CL_STS[12:0] = STATUS_DATA_MSB[4:0] |
	 *			STATUS_DATA_LSB[7:0]
	 * LAST_GOOD_TLRA_CL = LAST_GOOD_TLRA_CL_STS[12:0] * 1.666 us
	 */
	tmp = ((val[0] & LAST_GOOD_TLRA_CL_MASK) << 8) | val[1];
	config->cl_t_lra_us = (tmp * CL_TLRA_STEP_NS) / 1000;

	return 0;
}

static int haptics_get_closeloop_lra_period(struct haptics_chip *chip)
{
	int rc = 0;

	if (chip->ptn_revision == HAP_PTN_V1)
		rc = haptics_get_closeloop_lra_period_v1(chip);
	else
		rc = haptics_get_closeloop_lra_period_v2(chip);

	if (rc < 0) {
		dev_err(chip->dev, "get close loop T LRA failed, rc=%d\n", rc);
		return rc;
	}

	rc = haptics_adjust_lra_period(chip, &chip->config.cl_t_lra_us);
	if (rc < 0)
		return rc;

	dev_dbg(chip->dev, "OL_TLRA %u us, CL_TLRA %u us\n",
		chip->config.t_lra_us, chip->config.cl_t_lra_us);

	return 0;
}

static int haptics_set_vmax_mv(struct haptics_chip *chip, u32 vmax_mv)
{
	int rc = 0;
	u8 val;

	if (vmax_mv > MAX_VMAX_MV) {
		dev_err(chip->dev, "vmax (%d) exceed the max value: %d\n",
					vmax_mv, MAX_VMAX_MV);
		return -EINVAL;
	}

	if (chip->clamp_at_5v && (vmax_mv > CLAMPED_VMAX_MV))
		vmax_mv = CLAMPED_VMAX_MV;

	val = vmax_mv / VMAX_STEP_MV;
	rc = haptics_write(chip, chip->cfg_addr_base,
			HAP_CFG_VMAX_REG, &val, 1);
	if (rc < 0)
		dev_err(chip->dev, "config VMAX failed, rc=%d\n", rc);

	return rc;
}

static int haptics_set_vmax_headroom_mv(struct haptics_chip *chip, u32 hdrm_mv)
{
	int rc = 0;
	u8 val;

	if (hdrm_mv > VMAX_HDRM_MAX_MV) {
		dev_err(chip->dev, "headroom (%d) exceed the max value: %d\n",
					hdrm_mv, VMAX_HDRM_MAX_MV);
		return -EINVAL;
	}

	val = hdrm_mv / VMAX_HDRM_STEP_MV;
	rc = haptics_write(chip, chip->cfg_addr_base,
			HAP_CFG_VMAX_HDRM_REG, &val, 1);
	if (rc < 0)
		dev_err(chip->dev, "config VMAX_HDRM failed, rc=%d\n", rc);

	return rc;
}

static int haptics_enable_autores(struct haptics_chip *chip, bool en)
{
	int rc;

	rc = haptics_masked_write(chip, chip->cfg_addr_base,
			HAP_CFG_AUTORES_CFG_REG, AUTORES_EN_BIT,
			en ? AUTORES_EN_BIT : 0);
	if (rc < 0)
		dev_err(chip->dev, "%s auto resonance failed, rc=%d\n",
				en ? "enable" : "disable", rc);

	return rc;
}

static int haptics_set_direct_play(struct haptics_chip *chip, u8 amplitude)
{
	int rc;

	rc = haptics_write(chip, chip->ptn_addr_base,
			HAP_PTN_DIRECT_PLAY_REG, &amplitude, 1);
	if (rc < 0)
		dev_err(chip->dev, "config DIRECT_PLAY failed, rc=%d\n", rc);

	return rc;
}

static int haptics_enable_play(struct haptics_chip *chip, bool en)
{
	struct haptics_play_info *play = &chip->play;
	int rc;
	u8 val;

	if (en) {
		val = SC_CLR_BIT | AUTO_RES_ERR_CLR_BIT |
			HPWR_RDY_FAULT_CLR_BIT;
		rc = haptics_write(chip, chip->cfg_addr_base,
				HAP_CFG_FAULT_CLR_REG, &val, 1);
		if (rc < 0)
			return rc;
	}

	val = play->pattern_src;
	if (play->brake && !play->brake->disabled)
		val |= BRAKE_EN_BIT;

	if (en)
		val |= PLAY_EN_BIT;

	rc = haptics_write(chip, chip->cfg_addr_base,
			HAP_CFG_SPMI_PLAY_REG, &val, 1);
	if (rc < 0)
		dev_err(chip->dev, "Write SPMI_PLAY failed, rc=%d\n", rc);

	return rc;
}

static int haptics_set_brake(struct haptics_chip *chip, struct brake_cfg *brake)
{
	int rc = 0;
	u8 zero_samples[BRAKE_SAMPLE_COUNT] = {0}, val;

	if (brake->disabled)
		return 0;

	val = brake->mode << BRAKE_MODE_SHIFT |
		brake->sine_gain << BRAKE_SINE_GAIN_SHIFT;
	rc = haptics_masked_write(chip, chip->cfg_addr_base,
			HAP_CFG_BRAKE_MODE_CFG_REG,
			BRAKE_MODE_MASK | BRAKE_SINE_GAIN_MASK, val);
	if (rc < 0) {
		dev_err(chip->dev, "set brake CFG failed, rc=%d\n", rc);
		return rc;
	}

	rc = haptics_write(chip, chip->ptn_addr_base, HAP_PTN_BRAKE_AMP_REG,
			(brake->mode == OL_BRAKE || brake->mode == CL_BRAKE) ?
			brake->samples : zero_samples, BRAKE_SAMPLE_COUNT);
	if (rc < 0) {
		dev_err(chip->dev, "set brake pattern failed, rc=%d\n", rc);
		return rc;
	}

	return 0;
}

static int haptics_set_pattern(struct haptics_chip *chip,
		struct pattern_cfg *pattern,
		enum pattern_src src)
{
	struct pattern_s *sample;
	u8 values[SAMPLES_PER_PATTERN * 3] = { 0 };
	u8 ptn_tlra_addr, ptn_cfg_addr;
	int i, rc, tmp;
	u32 play_rate_us;

	if (src != PATTERN1 && src != PATTERN2) {
		dev_err(chip->dev, "no pattern src specified!\n");
		return -EINVAL;
	}

	ptn_tlra_addr = HAP_PTN_PTRN1_TLRA_MSB_REG;
	ptn_cfg_addr = HAP_PTN_PTRN1_CFG_REG;
	if (src == PATTERN2) {
		ptn_tlra_addr = HAP_PTN_PTRN2_TLRA_MSB_REG;
		ptn_cfg_addr = HAP_PTN_PTRN2_CFG_REG;
	}

	/* Adjust T_LRA before programming it into HW */
	play_rate_us = pattern->play_rate_us;
	rc = haptics_adjust_lra_period(chip, &play_rate_us);
	if (rc < 0)
		return rc;

	/* Configure T_LRA for this pattern */
	tmp = play_rate_us / TLRA_STEP_US;
	values[0] = (tmp >> 8) & TLRA_OL_MSB_MASK;
	values[1] = tmp & TLRA_OL_LSB_MASK;
	rc = haptics_write(chip, chip->ptn_addr_base, ptn_tlra_addr,
			values, 2);
	if (rc < 0) {
		dev_err(chip->dev, "update pattern TLRA failed, rc=%d\n", rc);
		return rc;
	}

	/* Configure pattern registers */
	for (i = 0; i < SAMPLES_PER_PATTERN; i++) {
		sample = &pattern->samples[i];
		values[i * 3] = sample->f_lra_x2 << PTRN_FLRA2X_SHIFT;
		values[i * 3] |= sample->period & PTRN_SAMPLE_PER_MASK;
		values[i * 3 + 1] =
			(sample->amplitude >> 8) & PTRN_AMP_MSB_MASK;
		values[i * 3 + 2] = sample->amplitude & PTRN_AMP_LSB_MASK;
	}

	rc = haptics_write(chip, chip->ptn_addr_base, ptn_cfg_addr,
			values, SAMPLES_PER_PATTERN * 3);
	if (rc < 0) {
		dev_err(chip->dev, "write pattern data failed, rc=%d\n", rc);
		return rc;
	}

	return 0;

}

static int haptics_update_fifo_sample_v1(struct haptics_chip *chip, u8 sample)
{
	int rc = 0;
	u8 val;

	/*
	 * Fill FIFO_DIN registers to update FIFO memory,
	 * need to fill LSB first then MSB.
	 * The FIFO memory width in V1 chip is 9-bit so shift
	 * 1 bit to left on the 8-bit FIFO sample to achieve a
	 * 9-bit data and fill it into the FIFO_DIN registers.
	 */
	val = (sample << 1) & HAP_PTN_V1_FIFO_DIN_LSB_MASK;
	rc = haptics_write(chip, chip->ptn_addr_base,
			HAP_PTN_V1_FIFO_DIN_LSB_REG, &val, 1);
	if (rc < 0) {
		dev_err(chip->dev, "write FIFO LSB failed, rc=%d\n",
				rc);
		return rc;
	}

	val = (sample >> 7) & HAP_PTN_V1_FIFO_DIN_MSB_BIT;
	rc = haptics_write(chip, chip->ptn_addr_base,
			HAP_PTN_V1_FIFO_DIN_MSB_REG, &val, 1);
	if (rc < 0) {
		dev_err(chip->dev, "write FIFO MSB failed, rc=%d\n",
				rc);
		return rc;
	}

	return 0;
}

static int haptics_update_fifo_sample_v2(struct haptics_chip *chip,
					u8 *samples, u32 num)
{
	int rc, i;

	if (num > HAP_PTN_V2_FIFO_DIN_NUM)
		return -EINVAL;

	if (num == HAP_PTN_V2_FIFO_DIN_NUM) {
		rc = haptics_write(chip, chip->ptn_addr_base,
				HAP_PTN_V2_FIFO_DIN_0_REG, samples, num);
		if (rc < 0) {
			dev_err(chip->dev, "bulk write FIFO_DIN failed, rc=%d\n",
					rc);
			return rc;
		}
	} else if (num < HAP_PTN_V2_FIFO_DIN_NUM) {
		for (i = 0; i < num; i++) {
			rc = haptics_write(chip, chip->ptn_addr_base,
					HAP_PTN_V2_FIFO_DIN_1B_REG,
					(samples + i), 1);
			if (rc < 0) {
				dev_err(chip->dev, "write FIFO_DIN_1B failed, rc=%d\n",
						rc);
				return rc;
			}
		}
	}

	return 0;
}

static int haptics_get_available_fifo_memory(struct haptics_chip *chip)
{
	int rc;
	u8 val[2];
	u32 fill, available;

	val[0] = MOD_STATUS_SEL_FIFO_FILL_STATUS_VAL;
	if (chip->ptn_revision == HAP_PTN_V1) {
		rc = haptics_write(chip, chip->cfg_addr_base,
				HAP_CFG_MOD_STATUS_SEL_REG, val, 1);
		if (rc < 0)
			return rc;

		rc = haptics_read(chip, chip->cfg_addr_base,
				HAP_CFG_STATUS_DATA_MSB_REG, val, 1);
		if (rc < 0)
			return rc;

		fill = val[0] & FIFO_REAL_TIME_FILL_STATUS_MASK_V1;
	} else {
		val[1] = MOD_STATUS_XT_V2_FIFO_FILL_STATUS_VAL;
		rc = haptics_write(chip, chip->cfg_addr_base,
				HAP_CFG_MOD_STATUS_SEL_REG, val, 2);
		if (rc < 0)
			return rc;

		rc = haptics_read(chip, chip->cfg_addr_base,
				HAP_CFG_STATUS_DATA_MSB_REG, val, 2);
		if (rc < 0)
			return rc;

		fill = ((val[0] & FIFO_REAL_TIME_FILL_STATUS_MSB_MASK_V2)
				<< 8) | val[1];
	}

	if (fill > MAX_FIFO_SAMPLES(chip)) {
		dev_err(chip->dev, "Filled FIFO number %d exceed the max %d\n",
				fill, MAX_FIFO_SAMPLES(chip));
		return -EINVAL;
	} else if (fill == MAX_FIFO_SAMPLES(chip)) {
		dev_err(chip->dev, "no FIFO space available\n");
		return -EBUSY;
	}

	available = MAX_FIFO_SAMPLES(chip) - fill;
	return available;
}

static int haptics_adjust_rc_clk(struct haptics_chip *chip)
{
	int rc, freq_diff, cal_count, f_lra, cl_f_lra;
	u8 val[2];

	if (!chip->config.t_lra_us || !chip->config.cl_t_lra_us)
		return -EINVAL;

	f_lra = USEC_PER_SEC / chip->config.t_lra_us;
	if (!f_lra)
		return -EINVAL;

	cl_f_lra = USEC_PER_SEC / chip->config.cl_t_lra_us;
	freq_diff = cl_f_lra - f_lra;

	/* RC_CLK_CAL_COUNT = 600*(1-(clk_adjustment/Fifo_pat_freq)) */
	cal_count = 600 - ((600 * freq_diff) / f_lra);
	dev_dbg(chip->dev, "RC clock calibration count:%#x\n", cal_count);

	/* Set FIFO pattern frequency adjustment */
	val[0] = (cal_count >> 8) & RC_CLK_CAL_COUNT_MSB_MASK;
	val[1] = cal_count & RC_CLK_CAL_COUNT_LSB_MASK;
	rc = haptics_write(chip, chip->cfg_addr_base,
			HAP_CFG_RC_CLK_CAL_COUNT_MSB_REG, val, 2);
	if (rc < 0)
		return rc;

	val[0] = CAL_RC_CLK_MANUAL_VAL << CAL_RC_CLK_SHIFT;
	return haptics_masked_write(chip, chip->cfg_addr_base,
			HAP_CFG_CAL_EN_REG, CAL_RC_CLK_MASK, val[0]);
}

static int haptics_update_fifo_samples(struct haptics_chip *chip,
					u8 *samples, u32 length)
{
	int rc, count, i;

	if (chip->ptn_revision == HAP_PTN_V1) {
		for (i = 0; i < length; i++) {
			rc = haptics_update_fifo_sample_v1(chip, samples[i]);
			if (rc < 0)
				return rc;
		}
	} else {
		count = length / HAP_PTN_V2_FIFO_DIN_NUM;
		for (i = 0; i < count; i++) {
			rc = haptics_update_fifo_sample_v2(chip,
					samples, HAP_PTN_V2_FIFO_DIN_NUM);
			if (rc < 0)
				return rc;

			samples += HAP_PTN_V2_FIFO_DIN_NUM;
		}

		if (length % HAP_PTN_V2_FIFO_DIN_NUM) {
			rc = haptics_update_fifo_sample_v2(chip,
					samples,
					length % HAP_PTN_V2_FIFO_DIN_NUM);
			if (rc < 0)
				return rc;
		}
	}

	return 0;
}

static int haptics_set_fifo_playrate(struct haptics_chip *chip,
				enum s_period period_per_s)
{
	int rc;
	u8 reg;

	reg = (chip->ptn_revision == HAP_PTN_V1) ?
		HAP_PTN_V1_FIFO_PLAY_RATE_REG : HAP_PTN_V2_FIFO_PLAY_RATE_REG;
	rc = haptics_masked_write(chip, chip->ptn_addr_base,
			reg, FIFO_PLAY_RATE_MASK, period_per_s);
	if (rc < 0)
		dev_err(chip->dev, "Set FIFO play rate failed, rc=%d\n", rc);

	return rc;
}

static int haptics_set_fifo_empty_threshold(struct haptics_chip *chip,
							u32 thresh)
{
	u8 reg, thresh_per_bit;
	int rc;

	reg = (chip->ptn_revision == HAP_PTN_V1) ?
		HAP_PTN_V1_FIFO_EMPTY_CFG_REG : HAP_PTN_V2_FIFO_EMPTY_CFG_REG;
	thresh_per_bit = (chip->ptn_revision == HAP_PTN_V1) ?
		HAP_PTN_V1_FIFO_THRESH_LSB : HAP_PTN_V2_FIFO_THRESH_LSB;
	rc = haptics_masked_write(chip, chip->ptn_addr_base,
			reg, EMPTY_THRESH_MASK, thresh / thresh_per_bit);
	if (rc < 0)
		dev_err(chip->dev, "Set FIFO empty threshold failed, rc=%d\n",
				rc);

	return rc;
}

static int haptics_set_fifo(struct haptics_chip *chip, struct fifo_cfg *fifo)
{
	struct fifo_play_status *status = &chip->play.fifo_status;
	u32 num, fifo_thresh;
	int rc, available;

	if (atomic_read(&status->is_busy) == 1) {
		dev_err(chip->dev, "FIFO is busy\n");
		return -EBUSY;
	}

	if (chip->ptn_revision == HAP_PTN_V1 &&
			fifo->period_per_s > F_8KHZ &&
			fifo->num_s > MAX_FIFO_SAMPLES(chip)) {
		dev_err(chip->dev, "PM8350B v1 doesn't support playing long FIFO pattern higher than 8 KHz play rate\n");
		return -EINVAL;
	}

	/* Configure FIFO play rate */
	rc = haptics_set_fifo_playrate(chip, fifo->period_per_s);
	if (rc < 0)
		return rc;

	if (fifo->period_per_s >= F_8KHZ) {
		rc = haptics_adjust_rc_clk(chip);
		if (rc < 0) {
			dev_err(chip->dev, "RC CLK adjustment failed, rc=%d\n",
				rc);
			return rc;
		}
	}

	atomic_set(&status->written_done, 0);
	atomic_set(&status->cancelled, 0);
	status->samples_written = 0;

	/*
	 * Write the 1st set of the data into FIFO if there are
	 * more than MAX_FIFO_SAMPLES samples, the rest will be
	 * written if any FIFO memory is available after playing.
	 */
	num = min_t(u32, fifo->num_s, MAX_FIFO_SAMPLES(chip));
	available = haptics_get_available_fifo_memory(chip);
	if (available < 0)
		return available;

	num = min_t(u32, available, num);
	rc = haptics_update_fifo_samples(chip, fifo->samples, num);
	if (rc < 0) {
		dev_err(chip->dev, "write FIFO samples failed, rc=%d\n", rc);
		return rc;
	}

	atomic_set(&status->is_busy, 1);
	status->samples_written = num;
	if (num == fifo->num_s) {
		fifo_thresh = 0;
		atomic_set(&status->written_done, 1);
	} else {
		fifo_thresh = chip->config.fifo_empty_thresh;
	}

	/*
	 * Set FIFO empty threshold here. FIFO empty IRQ will
	 * be enabled after playing FIFO samples so that more
	 * FIFO samples can be written (if available) when
	 * FIFO empty IRQ is triggered.
	 */

	return haptics_set_fifo_empty_threshold(chip, fifo_thresh);
}

static int haptics_load_constant_effect(struct haptics_chip *chip, u8 amplitude)
{
	struct haptics_play_info *play = &chip->play;
	int rc = 0;

	mutex_lock(&chip->play.lock);
	if (chip->play.in_calibration) {
		dev_err(chip->dev, "calibration in progress, ignore playing constant effect\n");
		rc = -EBUSY;
		goto unlock;
	}

	/* configure VMAX in case it was changed in previous effect playing */
	rc = haptics_set_vmax_mv(chip, chip->config.vmax_mv);
	if (rc < 0)
		goto unlock;

	/* Config brake settings if it's necessary */
	play->brake = &chip->config.brake;
	if (play->brake) {
		rc = haptics_set_brake(chip, play->brake);
		if (rc < 0)
			goto unlock;
	}

	rc = haptics_set_direct_play(chip, amplitude);
	if (rc < 0)
		goto unlock;

	/* Always enable LRA auto resonance for DIRECT_PLAY */
	rc = haptics_enable_autores(chip, !chip->config.is_erm);
	if (rc < 0)
		goto unlock;

	play->pattern_src = DIRECT_PLAY;
unlock:
	mutex_unlock(&chip->play.lock);
	return rc;
}

static int haptics_load_predefined_effect(struct haptics_chip *chip,
					struct haptics_effect *effect)
{
	struct haptics_play_info *play = &chip->play;
	int rc;

	if (effect == NULL)
		return -EINVAL;

	play->effect = effect;
	/* Clamp VMAX for different vibration strength */
	rc = haptics_set_vmax_mv(chip, play->vmax_mv);
	if (rc < 0)
		return rc;

	rc = haptics_enable_autores(chip, !play->effect->auto_res_disable);
	if (rc < 0)
		return rc;

	play->pattern_src = play->effect->src;
	if (play->pattern_src != PATTERN1 &&
			play->pattern_src != PATTERN2 &&
			play->pattern_src != FIFO) {
		dev_err(chip->dev, "pattern src %d can't be used for predefined effect\n",
				play->pattern_src);
		return -EINVAL;
	}

	play->brake = play->effect->brake;
	/* Config brake settings if it's necessary */
	if (play->brake) {
		rc = haptics_set_brake(chip, play->brake);
		if (rc < 0)
			return rc;
	}

	if (play->pattern_src == PATTERN1 || play->pattern_src == PATTERN2) {
		if (play->effect->pattern->preload) {
			dev_dbg(chip->dev, "Ignore preloaded effect: %d\n",
					play->effect->id);
			return 0;
		}

		rc = haptics_set_pattern(chip, play->effect->pattern,
						play->pattern_src);
		if (rc < 0)
			return rc;
	}

	if (play->pattern_src == FIFO) {
		rc = haptics_set_fifo(chip, play->effect->fifo);
		if (rc < 0)
			return rc;
	}

	return 0;
}

static int haptics_init_custom_effect(struct haptics_chip *chip)
{
	chip->custom_effect = devm_kzalloc(chip->dev,
			sizeof(*chip->custom_effect), GFP_KERNEL);
	if (!chip->custom_effect)
		return -ENOMEM;

	chip->custom_effect->fifo = devm_kzalloc(chip->dev,
			sizeof(*chip->custom_effect->fifo), GFP_KERNEL);
	if (!chip->custom_effect->fifo)
		return -ENOMEM;

	/* custom effect will be played in FIFO mode without brake */
	chip->custom_effect->pattern = NULL;
	chip->custom_effect->brake = NULL;
	chip->custom_effect->id = UINT_MAX;
	chip->custom_effect->vmax_mv = chip->config.vmax_mv;
	chip->custom_effect->t_lra_us = chip->config.t_lra_us;
	chip->custom_effect->src = FIFO;
	chip->custom_effect->auto_res_disable = false;

	return 0;
}

static int haptics_convert_sample_period(struct haptics_chip *chip,
						u32 play_rate_hz)
{
	enum s_period period;
	u32 f_lra, f_lra_min, f_lra_max;

	if (chip->config.t_lra_us == 0)
		return -EINVAL;

	f_lra = USEC_PER_SEC / chip->config.t_lra_us;
	if (f_lra == 0 || f_lra < F_LRA_VARIATION_HZ)
		return -EINVAL;

	f_lra_min = f_lra - F_LRA_VARIATION_HZ;
	f_lra_max = f_lra + F_LRA_VARIATION_HZ;

	if (play_rate_hz == 8000)
		period = F_8KHZ;
	else if (play_rate_hz == 16000)
		period = F_16KHZ;
	else if (play_rate_hz == 24000)
		period = F_24KHZ;
	else if (play_rate_hz == 32000)
		period = F_32KHZ;
	else if (play_rate_hz == 44100)
		period = F_44P1KHZ;
	else if (play_rate_hz == 48000)
		period = F_48KHZ;
	else if (is_between(play_rate_hz, f_lra_min, f_lra_max))
		period = T_LRA;
	else if (is_between(play_rate_hz / 2, f_lra_min, f_lra_max))
		period = T_LRA_DIV_2;
	else if (is_between(play_rate_hz / 4, f_lra_min, f_lra_max))
		period = T_LRA_DIV_4;
	else if (is_between(play_rate_hz / 8, f_lra_min, f_lra_max))
		period = T_LRA_DIV_8;
	else if (is_between(play_rate_hz * 2, f_lra_min, f_lra_max))
		period = T_LRA_X_2;
	else if (is_between(play_rate_hz * 4, f_lra_min, f_lra_max))
		period = T_LRA_X_4;
	else if (is_between(play_rate_hz * 8, f_lra_min, f_lra_max))
		period = T_LRA_X_8;
	else
		return -EINVAL;

	return period;
}

static int haptics_load_custom_effect(struct haptics_chip *chip,
			s16 __user *data, u32 length, s16 magnitude)
{
	struct haptics_play_info *play = &chip->play;
	struct custom_fifo_data custom_data = {};
	struct fifo_cfg *fifo;
	int rc;

	if (!chip->custom_effect || !chip->custom_effect->fifo)
		return -ENOMEM;

	fifo = chip->custom_effect->fifo;
	if (copy_from_user(&custom_data, data, sizeof(custom_data)))
		return -EFAULT;

	dev_dbg(chip->dev, "custom data length %d with play-rate %d Hz\n",
			custom_data.length, custom_data.play_rate_hz);
	rc = haptics_convert_sample_period(chip, custom_data.play_rate_hz);
	if (rc < 0) {
		dev_err(chip->dev, "Can't support play rate: %d Hz\n",
				custom_data.play_rate_hz);
		return rc;
	}

	fifo->period_per_s = rc;
	/*
	 * Before allocating samples buffer, free the old sample
	 * buffer first if it's not been freed.
	 */
	kfree(fifo->samples);
	fifo->samples = kcalloc(custom_data.length, sizeof(u8), GFP_KERNEL);
	if (!fifo->samples)
		return -ENOMEM;

	if (copy_from_user(fifo->samples,
				(u8 __user *)custom_data.data,
				custom_data.length)) {
		rc = -EFAULT;
		goto cleanup;
	}

	dev_dbg(chip->dev, "Copy custom FIFO samples successfully\n");
	fifo->num_s = custom_data.length;
	fifo->play_length_us = get_fifo_play_length_us(fifo,
			chip->custom_effect->t_lra_us);

	mutex_lock(&chip->play.lock);
	if (chip->play.in_calibration) {
		dev_err(chip->dev, "calibration in progress, ignore playing custom effect\n");
		rc = -EBUSY;
		goto unlock;
	}

	play->effect = chip->custom_effect;
	play->brake = NULL;
	play->vmax_mv = (magnitude * chip->custom_effect->vmax_mv) / 0x7fff;
	rc = haptics_set_vmax_mv(chip, play->vmax_mv);
	if (rc < 0)
		goto unlock;

	rc = haptics_enable_autores(chip, !play->effect->auto_res_disable);
	if (rc < 0)
		goto cleanup;

	play->pattern_src = FIFO;
	rc = haptics_set_fifo(chip, play->effect->fifo);
	if (rc < 0)
		goto unlock;

	mutex_unlock(&chip->play.lock);
	return 0;
unlock:
	mutex_unlock(&chip->play.lock);
cleanup:
	kfree(fifo->samples);
	fifo->samples = NULL;
	return rc;
}

static u32 get_play_length_us(struct haptics_play_info *play)
{
	struct haptics_effect *effect = play->effect;
	u32 length_us = 0;

	if (play->brake)
		length_us = play->brake->play_length_us;

	if ((effect->src == PATTERN1 || effect->src == PATTERN2)
			&& effect->pattern)
		length_us += effect->pattern->play_length_us;
	else if (effect->src == FIFO && effect->fifo)
		length_us += effect->fifo->play_length_us;

	return length_us;
}

static int haptics_load_periodic_effect(struct haptics_chip *chip,
			s16 __user *data, u32 length, s16 magnitude)
{
	struct haptics_play_info *play = &chip->play;
	s16 custom_data[CUSTOM_DATA_LEN] = { 0 };
	int rc, i;

	if (chip->effects_count == 0)
		return -EINVAL;

	if (copy_from_user(custom_data, data, sizeof(custom_data)))
		return -EFAULT;

	for (i = 0; i < chip->effects_count; i++)
		if (chip->effects[i].id == custom_data[CUSTOM_DATA_EFFECT_IDX])
			break;

	if (i == chip->effects_count) {
		dev_err(chip->dev, "effect%d is not supported!\n",
				custom_data[CUSTOM_DATA_EFFECT_IDX]);
		return -EINVAL;
	}

	mutex_lock(&chip->play.lock);
	play->vmax_mv = (magnitude * chip->effects[i].vmax_mv) / 0x7fff;

	dev_dbg(chip->dev, "upload effect %d, vmax_mv=%d\n",
			chip->effects[i].id, play->vmax_mv);

	if (chip->play.in_calibration) {
		dev_err(chip->dev, "calibration in progress, ignore playing predefined effect\n");
		rc = -EBUSY;
		goto unlock;
	}

	rc = haptics_load_predefined_effect(chip, &chip->effects[i]);
	if (rc < 0) {
		dev_err(chip->dev, "Play predefined effect%d failed, rc=%d\n",
				chip->effects[i].id, rc);
		goto unlock;
	}
	mutex_unlock(&chip->play.lock);

	play->length_us = get_play_length_us(play);
	custom_data[CUSTOM_DATA_TIMEOUT_SEC_IDX] =
		play->length_us / USEC_PER_SEC;
	custom_data[CUSTOM_DATA_TIMEOUT_MSEC_IDX] =
		(play->length_us % USEC_PER_SEC) / USEC_PER_MSEC;

	if (copy_to_user(data, custom_data, length))
		return -EFAULT;

	return 0;
unlock:
	mutex_unlock(&chip->play.lock);
	return rc;
}

static int haptics_upload_effect(struct input_dev *dev,
		struct ff_effect *effect, struct ff_effect *old)
{
	struct haptics_chip *chip = input_get_drvdata(dev);
	u32 length_us, tmp;
	s16 level;
	u8 amplitude;
	int rc = 0;

	switch (effect->type) {
	case FF_CONSTANT:
		length_us = effect->replay.length * USEC_PER_MSEC;
		level = effect->u.constant.level;
		tmp = level * DIRECT_PLAY_MAX_AMPLITUDE;
		amplitude = tmp / 0x7fff;
		dev_dbg(chip->dev, "upload constant effect, length = %dus, amplitude = %d\n",
				length_us, amplitude);
		haptics_load_constant_effect(chip, amplitude);
		if (rc < 0) {
			dev_err(chip->dev, "set direct play failed, rc=%d\n",
					rc);
			return rc;
		}

		break;
	case FF_PERIODIC:
		if (effect->u.periodic.waveform != FF_CUSTOM) {
			dev_err(chip->dev, "Only support custom waveforms\n");
			return -EINVAL;
		}

		if (effect->u.periodic.custom_len ==
				sizeof(struct custom_fifo_data)) {
			rc = haptics_load_custom_effect(chip,
					effect->u.periodic.custom_data,
					effect->u.periodic.custom_len,
					effect->u.periodic.magnitude);
			if (rc < 0) {
				dev_err(chip->dev, "Upload custom FIFO data failed\n",
						rc);
				return rc;
			}
		} else if (effect->u.periodic.custom_len ==
				sizeof(s16) * CUSTOM_DATA_LEN) {
			rc = haptics_load_periodic_effect(chip,
					effect->u.periodic.custom_data,
					effect->u.periodic.custom_len,
					effect->u.periodic.magnitude);
			if (rc < 0) {
				dev_err(chip->dev, "Upload periodic effect failed\n",
						rc);
				return rc;
			}
		}

		break;
	default:
		dev_err(chip->dev, "%d effect is not supported\n",
				effect->type);
		return -EINVAL;
	}

	return 0;
}

static void haptics_fifo_empty_irq_config(struct haptics_chip *chip,
						bool enable)
{
	mutex_lock(&chip->irq_lock);
	if (!chip->fifo_empty_irq_en && enable) {
		enable_irq(chip->fifo_empty_irq);
		chip->fifo_empty_irq_en = true;
	} else if (chip->fifo_empty_irq_en && !enable) {
		disable_irq_nosync(chip->fifo_empty_irq);
		chip->fifo_empty_irq_en = false;
	}
	mutex_unlock(&chip->irq_lock);
}

static int haptics_stop_fifo_play(struct haptics_chip *chip)
{
	int rc;
	u8 val;

	if (atomic_read(&chip->play.fifo_status.is_busy) == 0) {
		dev_dbg(chip->dev, "FIFO playing is not in progress\n");
		return 0;
	}

	rc = haptics_enable_play(chip, false);
	if (rc < 0)
		return rc;

	/* restore FIFO play rate back to T_LRA */
	rc = haptics_set_fifo_playrate(chip, T_LRA);
	if (rc < 0)
		return rc;

	haptics_fifo_empty_irq_config(chip, false);
	kfree(chip->custom_effect->fifo->samples);
	chip->custom_effect->fifo->samples = NULL;

	atomic_set(&chip->play.fifo_status.is_busy, 0);

	/*
	 * All other playing modes would use AUTO mode RC
	 * calibration except FIFO streaming mode, so restore
	 * back to AUTO RC calibration after FIFO playing.
	 */
	val = CAL_RC_CLK_AUTO_VAL << CAL_RC_CLK_SHIFT;
	rc = haptics_masked_write(chip, chip->cfg_addr_base,
			HAP_CFG_CAL_EN_REG, CAL_RC_CLK_MASK, val);
	if (rc < 0)
		return rc;

	dev_dbg(chip->dev, "stopped FIFO playing successfully\n");
	return 0;
}

static int haptics_playback(struct input_dev *dev, int effect_id, int val)
{
	struct haptics_chip *chip = input_get_drvdata(dev);
	struct haptics_play_info *play = &chip->play;
	int rc;

	dev_dbg(chip->dev, "playback val = %d\n", val);
	if (!!val) {
		rc = haptics_enable_play(chip, true);
		if (rc < 0)
			return rc;

		if (play->pattern_src == FIFO)
			haptics_fifo_empty_irq_config(chip, true);
	} else {
		if (play->pattern_src == FIFO &&
				atomic_read(&play->fifo_status.is_busy)) {
			dev_dbg(chip->dev, "FIFO playing is not done yet, defer stopping in erase\n");
			return 0;
		}

		rc = haptics_enable_play(chip, false);
	}

	return rc;
}

static int haptics_erase(struct input_dev *dev, int effect_id)
{
	struct haptics_chip *chip = input_get_drvdata(dev);
	struct haptics_play_info *play = &chip->play;
	int rc;

	if ((play->pattern_src == FIFO) &&
			atomic_read(&play->fifo_status.is_busy)) {
		if (atomic_read(&play->fifo_status.written_done) == 0) {
			dev_dbg(chip->dev, "cancelling FIFO playing\n");
			atomic_set(&play->fifo_status.cancelled, 1);
		}

		rc = haptics_stop_fifo_play(chip);
		if (rc < 0) {
			dev_err(chip->dev, "stop FIFO playing failed, rc=%d\n");
			return rc;
		}
	}

	return 0;
}

static void haptics_set_gain(struct input_dev *dev, u16 gain)
{
	struct haptics_chip *chip = input_get_drvdata(dev);
	struct haptics_hw_config *config = &chip->config;
	struct haptics_play_info *play = &chip->play;
	u32 vmax_mv;

	if (gain == 0)
		return;

	if (gain > 0x7fff)
		gain = 0x7fff;

	vmax_mv = config->vmax_mv;
	if (play->effect)
		vmax_mv = play->effect->vmax_mv;

	if (chip->clamp_at_5v && (vmax_mv > CLAMPED_VMAX_MV))
		vmax_mv = CLAMPED_VMAX_MV;

	play->vmax_mv = ((u32)(gain * vmax_mv)) / 0x7fff;
	haptics_set_vmax_mv(chip, play->vmax_mv);
}

static int haptics_store_cl_brake_settings(struct haptics_chip *chip)
{
	int rc = 0;
	u8 val;

	if (!chip->cl_brake_nvmem)
		return 0;

	val = MOD_STATUS_SEL_BRAKE_CAL_RNAT_RCAL_VAL;
	rc = haptics_write(chip, chip->cfg_addr_base,
			HAP_CFG_MOD_STATUS_SEL_REG, &val, 1);
	if (rc < 0)
		return rc;

	rc = haptics_read(chip, chip->cfg_addr_base,
			HAP_CFG_STATUS_DATA_LSB_REG, &val, 1);
	if (rc < 0)
		return rc;

	rc = nvmem_cell_write(chip->cl_brake_nvmem, &val, sizeof(val));
	if (rc < 0)
		dev_err(chip->dev, "store RNAT/RCAL to SDAM failed, rc=%d\n");

	return rc;
}

static int haptics_config_openloop_lra_period(struct haptics_chip *chip,
							u32 t_lra_us)
{
	u32 tmp;
	u8 val[2];
	int rc;

	rc = haptics_adjust_lra_period(chip, &t_lra_us);
	if (rc < 0)
		return rc;

	tmp = t_lra_us / TLRA_STEP_US;
	val[0] = (tmp >> 8) & TLRA_OL_MSB_MASK;
	val[1] = tmp & TLRA_OL_LSB_MASK;

	return haptics_write(chip, chip->cfg_addr_base,
			HAP_CFG_TLRA_OL_HIGH_REG, val, 2);
}

static int haptics_hw_init(struct haptics_chip *chip)
{
	struct haptics_hw_config *config = &chip->config;
	struct haptics_effect *effect;
	int rc = 0, i;
	u8 val[2];

	/* Store CL brake settings */
	rc = haptics_store_cl_brake_settings(chip);
	if (rc < 0)
		return rc;

	rc = haptics_read(chip, chip->hbst_addr_base,
			HAP_BOOST_REVISION1, val, 2);
	if (rc < 0)
		return rc;

	chip->hbst_revision = (val[1] << 8) | val[0];
	rc = haptics_read(chip, chip->hbst_addr_base,
			HAP_BOOST_CLAMP_5V_REG_OFFSET(chip), val, 1);
	if (rc < 0)
		return rc;

	chip->clamp_at_5v = val[0] & CLAMP_5V_BIT;
	/* Config VMAX */
	rc = haptics_set_vmax_mv(chip, config->vmax_mv);
	if (rc < 0)
		return rc;

	/* Config driver waveform shape */
	rc = haptics_masked_write(chip, chip->cfg_addr_base,
			HAP_CFG_DRV_WF_SEL_REG,
			DRV_WF_SEL_MASK, config->drv_wf);
	if (rc < 0)
		return rc;

	/* Config brake mode and waveform shape */
	val[0] = (config->brake.mode << BRAKE_MODE_SHIFT)
		| config->brake.sine_gain << BRAKE_SINE_GAIN_SHIFT
		| config->brake.brake_wf;
	rc = haptics_masked_write(chip, chip->cfg_addr_base,
			HAP_CFG_BRAKE_MODE_CFG_REG,
			BRAKE_MODE_MASK | BRAKE_SINE_GAIN_MASK
			| BRAKE_WF_SEL_MASK, val[0]);
	if (rc < 0)
		return rc;

	if (config->is_erm)
		return 0;

	rc = haptics_get_closeloop_lra_period(chip);
	if (rc < 0)
		return rc;

	/* Config T_LRA */
	rc = haptics_config_openloop_lra_period(chip, chip->config.cl_t_lra_us);
	if (rc < 0)
		return rc;

	/* Config to use 2's complement values for sample data */
	rc = haptics_masked_write(chip, chip->cfg_addr_base,
			HAP_CFG_DRV_WF_SEL_REG, DRV_WF_FMT_BIT, 0);
	if (rc < 0)
		return rc;

	/* preload effect */
	if (config->preload_effect != -EINVAL) {
		for (i = 0; i < chip->effects_count; i++)
			if (chip->effects[i].id == config->preload_effect)
				break;

		if (i == chip->effects_count) {
			dev_err(chip->dev, "preload effect %d is not found\n",
					config->preload_effect);
			return -EINVAL;
		}

		effect = &chip->effects[i];

		rc = haptics_set_pattern(chip, effect->pattern, effect->src);
		if (rc < 0) {
			dev_err(chip->dev, "Preload effect failed, rc=%d\n",
					rc);
			return rc;
		}
	}

	return rc;
}

static irqreturn_t fifo_empty_irq_handler(int irq, void *data)
{
	struct haptics_chip *chip = data;
	struct fifo_cfg *fifo = chip->play.effect->fifo;
	struct fifo_play_status *status = &chip->play.fifo_status;
	u32 samples_left;
	u8 *samples, val;
	int rc, num;

	rc = haptics_read(chip, chip->cfg_addr_base,
			HAP_CFG_INT_RT_STS_REG, &val, 1);
	if (rc < 0)
		return IRQ_HANDLED;

	if (!(val & FIFO_EMPTY_BIT)) {
		dev_dbg(chip->dev, "Ignore spurious/falling IRQ, INT_RT_STS = %#x\n",
				val);
		return IRQ_HANDLED;
	}

	if (atomic_read(&chip->play.fifo_status.written_done) == 1) {
		/*
		 * Check the FIFO real time fill status before stopping
		 * play to make sure that all FIFO samples can be played
		 * successfully. If there are still samples left in FIFO
		 * memory, defer the stop into erase() function.
		 */
		num = haptics_get_available_fifo_memory(chip);
		if (num != MAX_FIFO_SAMPLES(chip)) {
			dev_dbg(chip->dev, "%d FIFO samples still in playing\n",
					MAX_FIFO_SAMPLES(chip) - num);
			return IRQ_HANDLED;
		}

		rc = haptics_stop_fifo_play(chip);
		if (rc < 0)
			return IRQ_HANDLED;

		dev_dbg(chip->dev, "FIFO playing is done\n");
	} else {
		if (atomic_read(&status->cancelled) == 1) {
			dev_dbg(chip->dev, "FIFO programming got cancelled\n");
			return IRQ_HANDLED;
		}

		samples_left = fifo->num_s - status->samples_written;
		num = haptics_get_available_fifo_memory(chip);
		if (num < 0)
			return IRQ_HANDLED;

		if (samples_left <= num)
			num = samples_left;

		samples = fifo->samples + status->samples_written;

		/* Write more pattern data into FIFO memory. */
		rc = haptics_update_fifo_samples(chip, samples, num);
		if (rc < 0) {
			dev_err(chip->dev, "Update FIFO samples failed, rc=%d\n",
					rc);
			return IRQ_HANDLED;
		}

		status->samples_written += num;
		if (status->samples_written == fifo->num_s) {
			dev_dbg(chip->dev, "FIFO programming is done\n");
			atomic_set(&chip->play.fifo_status.written_done, 1);
			haptics_set_fifo_empty_threshold(chip, 0);
		}
	}

	return IRQ_HANDLED;
}

#ifdef CONFIG_DEBUG_FS
static int vmax_dbgfs_read(void *data, u64 *val)
{
	struct haptics_effect *effect = data;

	*val = effect->vmax_mv;

	return 0;
}

static int vmax_dbgfs_write(void *data, u64 val)
{
	struct haptics_effect *effect = data;

	if (val > MAX_VMAX_MV)
		val = MAX_VMAX_MV;

	effect->vmax_mv = (u32) val;

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(vmax_debugfs_ops, vmax_dbgfs_read,
		vmax_dbgfs_write, "%llu\n");

static int auto_res_en_dbgfs_read(void *data, u64 *val)
{
	struct haptics_effect *effect = data;

	*val = !effect->auto_res_disable;

	return 0;
}

static int auto_res_en_dbgfs_write(void *data, u64 val)
{
	struct haptics_effect *effect = data;

	effect->auto_res_disable = !val;

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(auto_res_en_debugfs_ops,  auto_res_en_dbgfs_read,
		auto_res_en_dbgfs_write, "%llu\n");

static ssize_t pattern_s_dbgfs_read(struct file *fp, char __user *buf,
		size_t count, loff_t *ppos)
{
	struct haptics_effect *effect = fp->private_data;
	u32 pos = 0, size = CHAR_PER_PATTERN_S * SAMPLES_PER_PATTERN;
	char *str;
	int i = 0, rc;

	if (!effect->pattern)
		return 0;

	str = kzalloc(size, GFP_KERNEL);
	if (!str)
		return -ENOMEM;

	for (i = 0; i < SAMPLES_PER_PATTERN; i++) {
		pos += scnprintf(str + pos, size - pos, "0x%03x  ",
				effect->pattern->samples[i].amplitude);
		pos += scnprintf(str + pos, size - pos, "%s(0x%02x)  ",
				period_str[effect->pattern->samples[i].period],
				effect->pattern->samples[i].period);
		pos += scnprintf(str + pos, size - pos, "F_LRA_X2(%1d)\n",
				 effect->pattern->samples[i].f_lra_x2);
	}

	rc = simple_read_from_buffer(buf, count, ppos, str, pos);
	kfree(str);

	return rc;
}

static ssize_t pattern_s_dbgfs_write(struct file *fp,
		const char __user *buf, size_t count, loff_t *ppos)
{
	struct haptics_effect *effect = fp->private_data;
	struct pattern_s patterns[SAMPLES_PER_PATTERN] = {{0, 0, 0},};
	char *str, *token;
	u32 val, tmp[3 * SAMPLES_PER_PATTERN] = {0};
	int rc, i = 0, j = 0;

	if (count > CHAR_PER_PATTERN_S * SAMPLES_PER_PATTERN)
		return -EINVAL;

	str = kzalloc(CHAR_PER_PATTERN_S * SAMPLES_PER_PATTERN + 1, GFP_KERNEL);
	if (!str)
		return -ENOMEM;

	rc = copy_from_user(str, buf, count);
	if (rc > 0) {
		rc = -EFAULT;
		goto exit;
	}

	str[count] = '\0';
	*ppos += count;

	while ((token = strsep((char **)&str, " ")) != NULL) {
		rc = kstrtouint(token, 0, &val);
		if (rc < 0) {
			rc = -EINVAL;
			goto exit;
		}

		tmp[i++] = val;
	}

	if (i % 3)
		pr_warn("Tuple should be having 3 elements, discarding tuple %d\n",
				i / 3);

	for (j = 0; j < i / 3; j++) {
		if (tmp[3 * j] > 0x1ff || tmp[3 * j + 1] > T_LRA_X_8 ||
				tmp[3 * j + 2] > 1) {
			pr_err("allowed tuples: [amplitude(<= 0x1ff) period(<=6(T_LRA_X_8)) f_lra_x2(0,1)]\n");
			rc = -EINVAL;
			goto exit;
		}

		patterns[j].amplitude = (u16)tmp[3 * j];
		patterns[j].period = (enum s_period)tmp[3 * j + 1];
		patterns[j].f_lra_x2 = !!tmp[3 * j + 2];
	}

	memcpy(effect->pattern->samples, patterns,
			sizeof(effect->pattern->samples));

	/* recalculate the play length */
	effect->pattern->play_length_us =
		get_pattern_play_length_us(effect->pattern);
	if (effect->pattern->play_length_us == -EINVAL) {
		pr_err("get pattern play length failed\n");
		rc = -EINVAL;
		goto exit;
	}

	rc = count;
exit:
	kfree(str);
	return rc;
}

static const struct file_operations pattern_s_dbgfs_ops = {
	.read = pattern_s_dbgfs_read,
	.write = pattern_s_dbgfs_write,
	.open = simple_open,
};

static int pattern_play_rate_us_dbgfs_read(void *data, u64 *val)
{
	struct haptics_effect *effect = data;

	*val = effect->pattern->play_rate_us;

	return 0;
}

static int pattern_play_rate_us_dbgfs_write(void *data, u64 val)
{
	struct haptics_effect *effect = data;

	if (val > TLRA_MAX_US)
		val = TLRA_MAX_US;

	effect->pattern->play_rate_us = (u32)val;
	/* recalculate the play length */
	effect->pattern->play_length_us =
		get_pattern_play_length_us(effect->pattern);
	if (effect->pattern->play_length_us == -EINVAL) {
		pr_err("get pattern play length failed\n");
		return -EINVAL;
	}

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(pattern_play_rate_dbgfs_ops,
		pattern_play_rate_us_dbgfs_read,
		pattern_play_rate_us_dbgfs_write, "%llu\n");

static ssize_t fifo_s_dbgfs_read(struct file *fp,
		char __user *buf, size_t count, loff_t *ppos)
{
	struct haptics_effect *effect = fp->private_data;
	struct fifo_cfg *fifo = effect->fifo;
	char *kbuf;
	int rc, i;
	u32 size, pos = 0;

	size = CHAR_PER_SAMPLE * fifo->num_s + 1;
	kbuf = kzalloc(size, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	for (i = 0; i < fifo->num_s; i++)
		pos += scnprintf(kbuf + pos, size - pos,
				"%d ", (s8)fifo->samples[i]);

	pos += scnprintf(kbuf + pos, size - pos, "%s", "\n");
	rc = simple_read_from_buffer(buf, count, ppos, kbuf, pos);
	kfree(kbuf);

	return rc;
}

static ssize_t fifo_s_dbgfs_write(struct file *fp,
		const char __user *buf, size_t count, loff_t *ppos)
{
	struct haptics_effect *effect = fp->private_data;
	struct fifo_cfg *fifo = effect->fifo;
	char *kbuf, *token;
	int rc, i = 0;
	int val;
	u8 *samples;

	kbuf = kzalloc(count + 1, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	rc = copy_from_user(kbuf, buf, count);
	if (rc > 0) {
		rc = -EFAULT;
		goto exit;
	}

	kbuf[count] = '\0';
	*ppos += count;

	samples = kcalloc(fifo->num_s, sizeof(*samples), GFP_KERNEL);
	if (!samples) {
		rc = -ENOMEM;
		goto exit;
	}

	while ((token = strsep(&kbuf, " ")) != NULL) {
		rc = kstrtoint(token, 0, &val);
		if (rc < 0) {
			rc = -EINVAL;
			goto exit2;
		}

		if (val > 0xff)
			val = 0xff;

		samples[i++] = (u8)val;
		/* only support fifo pattern no longer than before */
		if (i >= fifo->num_s)
			break;
	}

	memcpy(fifo->samples, samples, fifo->num_s);
	fifo->play_length_us = get_fifo_play_length_us(fifo, effect->t_lra_us);
	if (fifo->play_length_us == -EINVAL) {
		pr_err("get fifo play length failed\n");
		rc = -EINVAL;
		goto exit2;
	}

	rc = count;
exit2:
	kfree(samples);
exit:
	kfree(kbuf);
	return rc;
}

static const struct file_operations fifo_s_dbgfs_ops = {
	.read = fifo_s_dbgfs_read,
	.write = fifo_s_dbgfs_write,
	.owner = THIS_MODULE,
	.open = simple_open,
};

static int fifo_period_dbgfs_read(void *data, u64 *val)
{
	struct haptics_effect *effect = data;

	*val = effect->fifo->period_per_s;

	return 0;
}

static int fifo_period_dbgfs_write(void *data, u64 val)
{
	struct haptics_effect *effect = data;
	struct fifo_cfg *fifo = effect->fifo;

	if (val > F_48KHZ)
		return -EINVAL;

	fifo->period_per_s = (enum s_period)val;
	fifo->play_length_us = get_fifo_play_length_us(fifo, effect->t_lra_us);
	if (fifo->play_length_us == -EINVAL) {
		pr_err("get fifo play length failed\n");
		return -EINVAL;
	}

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fifo_period_dbgfs_ops,
		fifo_period_dbgfs_read,
		fifo_period_dbgfs_write, "%llu\n");

static ssize_t brake_s_dbgfs_read(struct file *fp,
		char __user *buf, size_t count, loff_t *ppos)
{
	struct haptics_effect *effect = fp->private_data;
	struct brake_cfg *brake = effect->brake;
	char *str;
	int rc, i;
	u32 size, pos = 0;

	size = CHAR_PER_SAMPLE * BRAKE_SAMPLE_COUNT + 1;
	str = kzalloc(size, GFP_KERNEL);
	if (!str)
		return -ENOMEM;

	for (i = 0; i < BRAKE_SAMPLE_COUNT; i++)
		pos += scnprintf(str + pos, size - pos, "0x%02x ",
				brake->samples[i]);

	pos += scnprintf(str + pos, size - pos, "%s", "\n");
	rc = simple_read_from_buffer(buf, count, ppos, str, pos);
	kfree(str);

	return rc;
}

static ssize_t brake_s_dbgfs_write(struct file *fp,
		const char __user *buf, size_t count, loff_t *ppos)
{
	struct haptics_effect *effect = fp->private_data;
	struct brake_cfg *brake = effect->brake;
	char *str, *token;
	int rc, i = 0;
	u32 val;
	u8 samples[BRAKE_SAMPLE_COUNT] = {0};

	if (count > CHAR_PER_SAMPLE * BRAKE_SAMPLE_COUNT)
		return -EINVAL;

	str = kzalloc(CHAR_PER_SAMPLE * BRAKE_SAMPLE_COUNT + 1, GFP_KERNEL);
	if (!str)
		return -ENOMEM;

	rc = copy_from_user(str, buf, count);
	if (rc > 0) {
		rc = -EFAULT;
		goto exit;
	}

	str[count] = '\0';
	*ppos += count;

	while ((token = strsep((char **)&str, " ")) != NULL) {
		rc = kstrtouint(token, 0, &val);
		if (rc < 0) {
			rc = -EINVAL;
			goto exit;
		}

		if (val > 0xff)
			val = 0xff;

		samples[i++] = (u8)val;
		if (i >= BRAKE_SAMPLE_COUNT)
			break;
	}

	memcpy(brake->samples, samples, BRAKE_SAMPLE_COUNT);
	verify_brake_samples(brake);
	brake->play_length_us =
		get_brake_play_length_us(brake, effect->t_lra_us);

	rc = count;
exit:
	kfree(str);
	return rc;
}

static const struct file_operations brake_s_dbgfs_ops = {
	.read = brake_s_dbgfs_read,
	.write = brake_s_dbgfs_write,
	.open = simple_open,
};

static ssize_t brake_mode_dbgfs_read(struct file *fp,
		char __user *buf, size_t count, loff_t *ppos)
{
	struct haptics_effect *effect = fp->private_data;
	struct brake_cfg *brake = effect->brake;
	char str[CHAR_BRAKE_MODE] = {0};
	u32 size;
	int rc;

	size = scnprintf(str, ARRAY_SIZE(str), "%s\n", brake_str[brake->mode]);
	rc = simple_read_from_buffer(buf, count, ppos, str, size);

	return rc;
}

static ssize_t brake_mode_dbgfs_write(struct file *fp,
		const char __user *buf, size_t count, loff_t *ppos)
{
	struct haptics_effect *effect = fp->private_data;
	struct brake_cfg *brake = effect->brake;
	char *kbuf;
	int rc;

	kbuf = kzalloc(count + 1, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	rc = copy_from_user(kbuf, buf, count);
	if (rc > 0) {
		rc = -EFAULT;
		goto exit;
	}

	kbuf[count] = '\0';
	*ppos += count;
	rc = count;
	if (strcmp(kbuf, "open-loop") == 0) {
		brake->mode = OL_BRAKE;
	} else if (strcmp(kbuf, "close-loop") == 0) {
		brake->mode = CL_BRAKE;
	} else if (strcmp(kbuf, "predictive") == 0) {
		brake->mode = PREDICT_BRAKE;
	} else if (strcmp(kbuf, "auto") == 0) {
		brake->mode = AUTO_BRAKE;
	} else {
		pr_err("%s brake mode is not supported\n", kbuf);
		rc = -EINVAL;
	}

exit:
	kfree(kbuf);
	return rc;
}

static const struct file_operations brake_mode_dbgfs_ops = {
	.read = brake_mode_dbgfs_read,
	.write = brake_mode_dbgfs_write,
	.open = simple_open,
};

static int brake_en_dbgfs_read(void *data, u64 *val)
{
	struct haptics_effect *effect = data;

	*val = !effect->brake->disabled;

	return 0;
}

static int brake_en_dbgfs_write(void *data, u64 val)
{
	struct haptics_effect *effect = data;

	effect->brake->disabled = !val;

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(brake_en_dbgfs_ops,  brake_en_dbgfs_read,
		brake_en_dbgfs_write, "%llu\n");

static int brake_sine_gain_dbgfs_read(void *data, u64 *val)
{
	struct haptics_effect *effect = data;

	*val = effect->brake->sine_gain;

	return 0;
}

static int brake_sine_gain_dbgfs_write(void *data, u64 val)
{
	struct haptics_effect *effect = data;

	if (val > BRAKE_SINE_GAIN_X8)
		return -EINVAL;

	effect->brake->sine_gain = val;

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(brake_sine_gain_dbgfs_ops,
		brake_sine_gain_dbgfs_read,
		brake_sine_gain_dbgfs_write, "%llu\n");

static int preload_effect_idx_dbgfs_read(void *data, u64 *val)
{
	struct haptics_chip *chip = data;

	*val = chip->config.preload_effect;

	return 0;
}

static int preload_effect_idx_dbgfs_write(void *data, u64 val)
{
	struct haptics_chip *chip = data;
	struct haptics_effect *new, *old;
	int rc, i;

	for (i = 0; i < chip->effects_count; i++)
		if (chip->effects[i].id == val)
			break;

	if (i == chip->effects_count)
		return -EINVAL;

	new = &chip->effects[i];

	for (i = 0; i < chip->effects_count; i++)
		if (chip->effects[i].id == chip->config.preload_effect)
			break;

	old = &chip->effects[i];

	chip->config.preload_effect = (u32)val;

	new->pattern->preload = true;
	new->src = PATTERN2;
	rc = haptics_set_pattern(chip, new->pattern, new->src);
	if (rc < 0)
		return rc;

	old->src = PATTERN1;
	old->pattern->preload = false;

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(preload_effect_idx_dbgfs_ops,
		preload_effect_idx_dbgfs_read,
		preload_effect_idx_dbgfs_write, "%llu\n");

static int haptics_add_effects_debugfs(struct haptics_effect *effect,
		struct dentry *dir)
{
	struct dentry *file, *pattern_dir, *fifo_dir, *brake_dir;

	file = debugfs_create_file_unsafe("vmax_mv", 0644, dir,
			effect, &vmax_debugfs_ops);
	if (IS_ERR(file))
		return PTR_ERR(file);

	file = debugfs_create_file_unsafe("lra_auto_res_en", 0644, dir,
			effect, &auto_res_en_debugfs_ops);
	if (IS_ERR(file))
		return PTR_ERR(file);

	/* effect can have either pattern or FIFO */
	if (effect->pattern) {
		pattern_dir = debugfs_create_dir("pattern", dir);
		if (IS_ERR(pattern_dir))
			return PTR_ERR(pattern_dir);

		file = debugfs_create_file("samples", 0644, pattern_dir,
				effect, &pattern_s_dbgfs_ops);
		if (IS_ERR(file))
			return PTR_ERR(file);

		file = debugfs_create_file_unsafe("play_rate_us", 0644,
				pattern_dir, effect,
				&pattern_play_rate_dbgfs_ops);
		if (IS_ERR(file))
			return PTR_ERR(file);
	} else if (effect->fifo) {
		fifo_dir = debugfs_create_dir("fifo", dir);
		if (IS_ERR(fifo_dir))
			return PTR_ERR(fifo_dir);

		file = debugfs_create_file("samples", 0644, fifo_dir,
				effect, &fifo_s_dbgfs_ops);
		if (IS_ERR(file))
			return PTR_ERR(file);

		file = debugfs_create_file_unsafe("period", 0644, fifo_dir,
				effect, &fifo_period_dbgfs_ops);
		if (IS_ERR(file))
			return PTR_ERR(file);
	}

	if (effect->brake) {
		brake_dir = debugfs_create_dir("brake", dir);
		if (IS_ERR(brake_dir))
			return PTR_ERR(brake_dir);

		file = debugfs_create_file("samples", 0644, brake_dir,
				effect, &brake_s_dbgfs_ops);
		if (IS_ERR(file))
			return PTR_ERR(file);

		file = debugfs_create_file("mode", 0644, brake_dir,
				effect, &brake_mode_dbgfs_ops);
		if (IS_ERR(file))
			return PTR_ERR(file);

		file = debugfs_create_file_unsafe("enable", 0644, brake_dir,
				effect, &brake_en_dbgfs_ops);
		if (IS_ERR(file))
			return PTR_ERR(file);

		file = debugfs_create_file_unsafe("sine_gain", 0644, brake_dir,
				effect, &brake_sine_gain_dbgfs_ops);
		if (IS_ERR(file))
			return PTR_ERR(file);
	}

	return 0;
}

#define EFFECT_NAME_SIZE		12
static int haptics_create_debugfs(struct haptics_chip *chip)
{
	struct dentry *hap_dir, *effect_dir, *file;
	char str[EFFECT_NAME_SIZE] = {0};
	int rc, i;

	hap_dir = debugfs_create_dir("haptics", NULL);
	if (IS_ERR(hap_dir)) {
		rc = PTR_ERR(hap_dir);
		dev_err(chip->dev, "create haptics debugfs directory failed, rc=%d\n",
				rc);
		return rc;
	}

	for (i = 0; i < chip->effects_count; i++) {
		scnprintf(str, ARRAY_SIZE(str), "effect%d",
				chip->effects[i].id);
		effect_dir = debugfs_create_dir(str, hap_dir);
		if (IS_ERR(effect_dir)) {
			rc = PTR_ERR(effect_dir);
			dev_err(chip->dev, "create %s debugfs directory failed, rc=%d\n",
					str, rc);
			goto exit;
		}

		rc = haptics_add_effects_debugfs(&chip->effects[i], effect_dir);
		if (rc < 0) {
			dev_err(chip->dev, "create debugfs nodes for %s failed, rc=%d\n",
					str, rc);
			goto exit;
		}
	}

	file = debugfs_create_file_unsafe("preload_effect_idx", 0644, hap_dir,
			chip, &preload_effect_idx_dbgfs_ops);
	if (IS_ERR(file)) {
		rc = PTR_ERR(file);
		dev_err(chip->dev, "create preload_effect_idx debugfs failed, rc=%d\n",
				rc);
		goto exit;
	}

	file = debugfs_create_u32("fifo_empty_thresh", 0600, hap_dir,
			&chip->config.fifo_empty_thresh);
	if (IS_ERR(file)) {
		rc = PTR_ERR(file);
		dev_err(chip->dev, "create fifo_empty_thresh debugfs failed, rc=%d\n",
				rc);
		goto exit;
	}

	chip->debugfs_dir = hap_dir;
	return 0;

exit:
	debugfs_remove_recursive(hap_dir);
	return rc;
}
#endif

static int haptics_parse_per_effect_dt(struct haptics_chip *chip,
		struct device_node *node, struct haptics_effect *effect)
{
	struct haptics_hw_config *config = &chip->config;
	u32 data[SAMPLES_PER_PATTERN * 3];
	int rc, tmp, i;

	if (!effect)
		return -EINVAL;

	rc = of_property_read_u32(node, "qcom,effect-id", &effect->id);
	if (rc < 0) {
		dev_err(chip->dev, "Read qcom,effect-id failed, rc=%d\n",
				rc);
		return rc;
	}

	effect->vmax_mv = config->vmax_mv;
	rc = of_property_read_u32(node, "qcom,wf-vmax-mv", &tmp);
	if (rc < 0)
		dev_dbg(chip->dev, "Read qcom,wf-vmax-mv failed, rc=%d\n",
				rc);
	else
		effect->vmax_mv = tmp;

	if (effect->vmax_mv > MAX_VMAX_MV) {
		dev_err(chip->dev, "qcom,wf-vmax-mv (%d) exceed the max value: %d\n",
				effect->vmax_mv, MAX_VMAX_MV);
		return -EINVAL;
	}

	effect->t_lra_us = config->t_lra_us;
	tmp = of_property_count_elems_of_size(node,
			"qcom,wf-pattern-data", sizeof(u32));
	if (tmp > SAMPLES_PER_PATTERN * 3) {
		dev_err(chip->dev, "Pattern src can only play 8 samples at max\n");
		return -EINVAL;
	}

	if (tmp > 0) {
		effect->pattern = devm_kzalloc(chip->dev,
				sizeof(*effect->pattern), GFP_KERNEL);
		if (!effect->pattern)
			return -ENOMEM;

		rc = of_property_read_u32_array(node,
				"qcom,wf-pattern-data", data, tmp);
		if (rc < 0) {
			dev_err(chip->dev, "Read wf-pattern-data failed, rc=%d\n",
					rc);
			return rc;
		}

		for (i = 0; i < tmp / 3; i++) {
			if (data[3 * i] > 0x1ff || data[3 * i + 1] > T_LRA_X_8
					|| data[3 * i + 2] > 1) {
				dev_err(chip->dev, "allowed tuples: [amplitude(<= 0x1ff) period(<=6(T_LRA_X_8)) f_lra_x2(0,1)]\n");
				return -EINVAL;
			}

			effect->pattern->samples[i].amplitude =
				(u16)data[3 * i];
			effect->pattern->samples[i].period =
				(enum s_period)data[3 * i + 1];
			effect->pattern->samples[i].f_lra_x2 =
				(bool)data[3 * i + 2];
		}

		effect->pattern->preload = of_property_read_bool(node,
				"qcom,wf-pattern-preload");
		/*
		 * Use PATTERN1 src by default, effect with preloaded
		 * pattern will use PATTERN2 by default and only the
		 * 1st preloaded pattern will be served.
		 */
		effect->src = PATTERN1;
		if (effect->pattern->preload) {
			if (config->preload_effect != -EINVAL) {
				dev_err(chip->dev, "effect %d has been defined as preloaded\n",
						config->preload_effect);
				effect->pattern->preload = false;
			} else {
				config->preload_effect = effect->id;
				effect->src = PATTERN2;
			}
		}
	}

	tmp = of_property_count_u8_elems(node, "qcom,wf-fifo-data");
	if (tmp > 0) {
		effect->fifo = devm_kzalloc(chip->dev,
				sizeof(*effect->fifo), GFP_KERNEL);
		if (!effect->fifo)
			return -ENOMEM;

		effect->fifo->samples = devm_kcalloc(chip->dev,
				tmp, sizeof(u8), GFP_KERNEL);
		if (!effect->fifo->samples)
			return -ENOMEM;

		rc = of_property_read_u8_array(node, "qcom,wf-fifo-data",
				effect->fifo->samples, tmp);
		if (rc < 0) {
			dev_err(chip->dev, "Read wf-fifo-data failed, rc=%d\n",
					rc);
			return rc;
		}

		effect->fifo->num_s = tmp;
	}

	if (!effect->pattern && !effect->fifo) {
		dev_err(chip->dev, "no pattern specified for effect %d\n",
				effect->id);
		return -EINVAL;
	}

	if (effect->pattern) {
		if (config->is_erm)
			effect->pattern->play_rate_us =
				DEFAULT_ERM_PLAY_RATE_US;
		else
			effect->pattern->play_rate_us = config->t_lra_us;

		rc = of_property_read_u32(node, "qcom,wf-pattern-period-us",
					&tmp);
		if (rc < 0)
			dev_dbg(chip->dev, "Read qcom,wf-pattern-period-us failed, rc=%d\n",
					rc);
		else
			effect->pattern->play_rate_us = tmp;

		if (effect->pattern->play_rate_us > TLRA_MAX_US) {
			dev_err(chip->dev, "qcom,wf-pattern-period-us (%d) exceed the max value: %d\n",
					effect->pattern->play_rate_us,
					TLRA_MAX_US);
			return -EINVAL;
		}

		effect->pattern->play_length_us =
			get_pattern_play_length_us(effect->pattern);
		if (effect->pattern->play_length_us == -EINVAL) {
			dev_err(chip->dev, "get pattern play length failed\n");
			return -EINVAL;
		}

		if (effect->fifo)
			dev_dbg(chip->dev, "Ignore FIFO data if pattern is specified!\n");

	} else if (effect->fifo) {
		effect->fifo->period_per_s = T_LRA;
		rc = of_property_read_u32(node, "qcom,wf-fifo-period", &tmp);
		if (tmp > F_48KHZ) {
			dev_err(chip->dev, "FIFO playing period %d is not supported\n",
					tmp);
			return -EINVAL;
		} else if (!rc) {
			effect->fifo->period_per_s = tmp;
		}

		effect->fifo->play_length_us =
			get_fifo_play_length_us(effect->fifo, config->t_lra_us);
		if (effect->fifo->play_length_us == -EINVAL) {
			dev_err(chip->dev, "get fifo play length failed\n");
			return -EINVAL;
		}

		effect->src = FIFO;
	}

	effect->brake = devm_kzalloc(chip->dev,
			sizeof(*effect->brake), GFP_KERNEL);
	if (!effect->brake)
		return -ENOMEM;

	memcpy(effect->brake, &config->brake, sizeof(*effect->brake));

	of_property_read_u32(node, "qcom,wf-brake-mode", &effect->brake->mode);
	if (effect->brake->mode > AUTO_BRAKE) {
		dev_err(chip->dev, "can't support brake mode: %d\n",
				effect->brake->mode);
		return -EINVAL;
	}

	if (effect->brake->brake_wf == WF_SINE) {
		of_property_read_u32(node, "qcom,wf-brake-sine-gain",
				&effect->brake->sine_gain);
		if (effect->brake->sine_gain > BRAKE_SINE_GAIN_X8) {
			dev_err(chip->dev, "can't support brake sine gain: %d\n",
					effect->brake->sine_gain);
			return -EINVAL;
		}
	}

	effect->brake->disabled =
		of_property_read_bool(node, "qcom,wf-brake-disable");
	tmp = of_property_count_u8_elems(node, "qcom,wf-brake-pattern");
	if (tmp > BRAKE_SAMPLE_COUNT) {
		dev_err(chip->dev, "more than %d brake samples\n",
				BRAKE_SAMPLE_COUNT);
		return -EINVAL;
	}

	if (tmp > 0) {
		memset(effect->brake->samples, 0,
				sizeof(u8) * BRAKE_SAMPLE_COUNT);
		rc = of_property_read_u8_array(node, "qcom,wf-brake-pattern",
				effect->brake->samples, tmp);
		if (rc < 0) {
			dev_err(chip->dev, "Read wf-brake-pattern failed, rc=%d\n",
					rc);
			return rc;
		}
		verify_brake_samples(effect->brake);
	} else {
		if (effect->brake->mode == OL_BRAKE ||
				effect->brake->mode == CL_BRAKE)
			effect->brake->disabled = true;
	}

	effect->brake->play_length_us =
		get_brake_play_length_us(effect->brake, config->t_lra_us);

	if (config->is_erm)
		return 0;

	/* LRA specific per-effect settings are parsed below */
	effect->auto_res_disable = of_property_read_bool(node,
			"qcom,wf-auto-res-disable");

	return 0;
}

static int haptics_parse_effects_dt(struct haptics_chip *chip)
{
	struct device_node *node = chip->dev->of_node;
	struct device_node *child;
	int rc, i = 0, num = 0;

	for_each_available_child_of_node(node, child) {
		if (of_find_property(child, "qcom,effect-id", NULL))
			num++;
	}
	if (num == 0)
		return 0;

	chip->effects = devm_kcalloc(chip->dev, num,
			sizeof(*chip->effects), GFP_KERNEL);
	if (!chip->effects)
		return -ENOMEM;

	for_each_available_child_of_node(node, child) {
		if (!of_find_property(child, "qcom,effect-id", NULL))
			continue;

		rc = haptics_parse_per_effect_dt(chip, child,
					&chip->effects[i]);
		if (rc < 0) {
			dev_err(chip->dev, "parse effect %d failed, rc=%d\n",
					i);
			of_node_put(child);
			return rc;
		}
		i++;
	}

	chip->effects_count = i;
	__dump_effects(chip);

	return 0;
}

static int haptics_parse_lra_dt(struct haptics_chip *chip)
{
	struct haptics_hw_config *config = &chip->config;
	struct device_node *node = chip->dev->of_node;
	int rc;

	rc = of_property_read_u32(node, "qcom,lra-period-us",
					&config->t_lra_us);
	if (rc < 0) {
		dev_err(chip->dev, "Read T-LRA failed, rc=%d\n", rc);
		return rc;
	}

	if (config->t_lra_us > TLRA_MAX_US) {
		dev_err(chip->dev, "qcom,lra-period-us (%d) exceed the max value: %d\n",
				config->t_lra_us, TLRA_MAX_US);
		return -EINVAL;
	}

	config->drv_wf = WF_SINE;
	of_property_read_u32(node, "qcom,drv-sig-shape", &config->drv_wf);
	if (config->drv_wf >= WF_RESERVED) {
		dev_err(chip->dev, "Can't support drive shape: %d\n",
				config->drv_wf);
		return -EINVAL;
	}

	config->brake.brake_wf = WF_SINE;
	of_property_read_u32(node, "qcom,brake-sig-shape",
			&config->brake.brake_wf);
	if (config->brake.brake_wf >= WF_RESERVED) {
		dev_err(chip->dev, "Can't support brake shape: %d\n",
				config->brake.brake_wf);
		return -EINVAL;
	}

	if (config->brake.brake_wf == WF_SINE) {
		config->brake.sine_gain = BRAKE_SINE_GAIN_X1;
		of_property_read_u32(node, "qcom,brake-sine-gain",
				&config->brake.sine_gain);
		if (config->brake.sine_gain > BRAKE_SINE_GAIN_X8) {
			dev_err(chip->dev, "Can't support brake sine gain: %d\n",
					config->brake.sine_gain);
			return -EINVAL;
		}
	}

	return 0;
}

static int haptics_get_revision(struct haptics_chip *chip)
{
	int rc;
	u8 val;

	rc = haptics_read(chip, chip->ptn_addr_base,
			HAP_PTN_REVISION2_REG, &val, 1);
	if (rc < 0)
		return rc;

	chip->ptn_revision = val;
	dev_dbg(chip->dev, "haptics ptn module revision: %#x\n",
			chip->ptn_revision);
	return 0;
}

static int haptics_parse_dt(struct haptics_chip *chip)
{
	struct haptics_hw_config *config = &chip->config;
	struct device_node *node = chip->dev->of_node;
	struct platform_device *pdev = to_platform_device(chip->dev);
	const __be32 *addr;
	int rc = 0, tmp;

	if (of_find_property(node, "nvmem-cells", NULL)) {
		chip->cl_brake_nvmem = devm_nvmem_cell_get(chip->dev,
						"hap_cl_brake");
		if (IS_ERR(chip->cl_brake_nvmem)) {
			rc = PTR_ERR(chip->cl_brake_nvmem);
			if (rc != -EPROBE_DEFER)
				dev_err(chip->dev, "Failed to get nvmem-cells, rc=%d\n",
							rc);

			return rc;
		}
	}

	addr = of_get_address(node, 0, NULL, NULL);
	if (!addr) {
		dev_err(chip->dev, "Read HAPTICS_CFG address failed\n");
		return -EINVAL;
	}

	chip->cfg_addr_base = be32_to_cpu(*addr);
	addr = of_get_address(node, 1, NULL, NULL);
	if (!addr) {
		dev_err(chip->dev, "Read HAPTICS_PATTERN address failed\n");
		return -EINVAL;
	}

	chip->ptn_addr_base = be32_to_cpu(*addr);
	addr = of_get_address(node, 2, NULL, NULL);
	if (!addr) {
		dev_err(chip->dev, "Read HAPTICS_HBOOST address failed\n");
		return -EINVAL;
	}

	chip->hbst_addr_base = be32_to_cpu(*addr);
	rc = haptics_get_revision(chip);
	if (rc < 0) {
		dev_err(chip->dev, "Get revision failed, rc=%d\n", rc);
		return rc;
	}

	chip->fifo_empty_irq = platform_get_irq_byname(pdev, "fifo-empty");
	if (!chip->fifo_empty_irq) {
		dev_err(chip->dev, "Get fifo-empty IRQ failed\n");
		return -EINVAL;
	}

	config->vmax_mv = DEFAULT_VMAX_MV;
	of_property_read_u32(node, "qcom,vmax-mv", &config->vmax_mv);
	if (config->vmax_mv >= MAX_VMAX_MV) {
		dev_err(chip->dev, "qcom,vmax-mv (%d) exceed the max value: %d\n",
				config->vmax_mv, MAX_VMAX_MV);
		return -EINVAL;
	}

	config->fifo_empty_thresh = FIFO_EMPTY_THRESHOLD(chip);
	of_property_read_u32(node, "qcom,fifo-empty-threshold",
			&config->fifo_empty_thresh);
	if (config->fifo_empty_thresh >= MAX_FIFO_SAMPLES(chip)) {
		dev_err(chip->dev, "FIFO empty threshold (%d) should be less than %d\n",
			config->fifo_empty_thresh, MAX_FIFO_SAMPLES(chip));
		return -EINVAL;
	}

	config->brake.mode = AUTO_BRAKE;
	of_property_read_u32(node, "qcom,brake-mode", &config->brake.mode);
	if (config->brake.mode > AUTO_BRAKE) {
		dev_err(chip->dev, "Can't support brake mode: %d\n",
				config->brake.mode);
		return -EINVAL;
	}

	config->brake.disabled =
		of_property_read_bool(node, "qcom,brake-disable");
	tmp = of_property_count_u8_elems(node, "qcom,brake-pattern");
	if (tmp > BRAKE_SAMPLE_COUNT) {
		dev_err(chip->dev, "more than %d brake samples\n",
				BRAKE_SAMPLE_COUNT);
		return -EINVAL;
	}

	if (tmp > 0) {
		rc = of_property_read_u8_array(node, "qcom,brake-pattern",
				config->brake.samples, tmp);
		if (rc < 0) {
			dev_err(chip->dev, "Read brake-pattern failed, rc=%d\n",
					rc);
			return rc;
		}
		verify_brake_samples(&config->brake);
	} else {
		if (config->brake.mode == OL_BRAKE ||
				config->brake.mode == CL_BRAKE)
			config->brake.disabled = true;
	}

	config->is_erm = of_property_read_bool(node, "qcom,use-erm");
	if (config->is_erm) {
		config->drv_wf = WF_NO_MODULATION;
		config->brake.brake_wf = WF_NO_MODULATION;
	} else {
		rc = haptics_parse_lra_dt(chip);
		if (rc < 0) {
			dev_err(chip->dev, "Parse device-tree for LRA failed, rc=%d\n",
					rc);
			return rc;
		}
	}

	config->preload_effect = -EINVAL;
	rc = haptics_parse_effects_dt(chip);
	if (rc < 0) {
		dev_err(chip->dev, "Parse device-tree for effects failed, rc=%d\n",
				 rc);
		return rc;
	}

	return 0;
}

static int swr_slave_reg_enable(struct regulator_dev *rdev)
{
	struct haptics_chip *chip = rdev_get_drvdata(rdev);
	int rc;
	u8 mask = SWR_PAT_INPUT_EN_BIT | SWR_PAT_RES_N_BIT | SWR_PAT_CFG_EN_BIT;

	rc = haptics_masked_write(chip, chip->cfg_addr_base,
			HAP_CFG_SWR_ACCESS_REG, mask, mask);
	if (rc < 0) {
		dev_err(chip->dev, "Failed to enable SWR_PAT, rc=%d\n",
				rc);
		return rc;
	}

	chip->swr_slave_enabled = true;
	return 0;
}

static int swr_slave_reg_disable(struct regulator_dev *rdev)
{
	struct haptics_chip *chip = rdev_get_drvdata(rdev);
	int rc;

	rc = haptics_masked_write(chip, chip->cfg_addr_base,
			HAP_CFG_SWR_ACCESS_REG,
			SWR_PAT_INPUT_EN_BIT | SWR_PAT_RES_N_BIT |
			SWR_PAT_CFG_EN_BIT, 0);
	if (rc < 0) {
		dev_err(chip->dev, "Failed to disable SWR_PAT, rc=%d\n", rc);
		return rc;
	}

	chip->swr_slave_enabled = false;
	return 0;
}

static int swr_slave_reg_is_enabled(struct regulator_dev *rdev)
{
	struct haptics_chip *chip = rdev_get_drvdata(rdev);

	return chip->swr_slave_enabled;
}

static const struct regulator_ops swr_slave_reg_ops = {
	.enable		= swr_slave_reg_enable,
	.disable	= swr_slave_reg_disable,
	.is_enabled	= swr_slave_reg_is_enabled,
};

static struct regulator_desc swr_slave_reg_rdesc = {
	.owner		= THIS_MODULE,
	.of_match	= "qcom,hap-swr-slave-reg",
	.name		= "hap-swr-slave-reg",
	.type		= REGULATOR_VOLTAGE,
	.ops		= &swr_slave_reg_ops,
};

static int haptics_init_swr_slave_regulator(struct haptics_chip *chip)
{
	struct regulator_config cfg = {};
	u8 val, mask;
	int rc = 0;

	rc = haptics_read(chip, chip->cfg_addr_base,
			HAP_CFG_SWR_ACCESS_REG, &val, 1);
	if (rc < 0) {
		dev_err(chip->dev, "Failed to read SWR_ACCESS, rc=%d\n",
				rc);
		return rc;
	}

	mask = SWR_PAT_INPUT_EN_BIT | SWR_PAT_RES_N_BIT | SWR_PAT_CFG_EN_BIT;
	val &= mask;
	chip->swr_slave_enabled = ((val == mask) ? true : false);

	cfg.dev = chip->dev;
	cfg.driver_data = chip;
	chip->swr_slave_rdev = devm_regulator_register(chip->dev,
			&swr_slave_reg_rdesc, &cfg);
	if (IS_ERR(chip->swr_slave_rdev)) {
		rc = PTR_ERR(chip->swr_slave_rdev);
		chip->swr_slave_rdev = NULL;

		if (rc != -EPROBE_DEFER)
			dev_err(chip->dev, "register swr-slave-reg regulator failed, rc=%d\n",
					rc);
	}

	return rc;
}

#define LRA_CALIBRATION_VMAX_HDRM_MV	500
static int haptics_start_lra_calibration(struct haptics_chip *chip)
{
	int rc;
	u8 autores_cfg;

	mutex_lock(&chip->play.lock);
	/*
	 * Ignore calibration if it's in FIFO playing to avoid
	 * messing up the FIFO playing status
	 */
	if ((chip->play.pattern_src == FIFO) &&
			atomic_read(&chip->play.fifo_status.is_busy)) {
		dev_err(chip->dev, "In FIFO playing, ignore calibration\n");
		rc = -EBUSY;
		goto unlock;
	}

	/* Stop other mode playing if there is any */
	rc = haptics_enable_play(chip, false);
	if (rc < 0) {
		dev_err(chip->dev, "Stop playing failed, rc=%d\n", rc);
		goto unlock;
	}

	chip->play.in_calibration = true;

	rc = haptics_read(chip, chip->cfg_addr_base,
			HAP_CFG_AUTORES_CFG_REG, &autores_cfg, 1);
	if (rc < 0) {
		dev_err(chip->dev, "Read AUTORES_CFG failed, rc=%d\n", rc);
		goto unlock;
	}

	rc = haptics_masked_write(chip, chip->cfg_addr_base,
			HAP_CFG_AUTORES_CFG_REG, AUTORES_EN_BIT |
			AUTORES_EN_DLY_MASK | AUTORES_ERR_WINDOW_MASK,
			AUTORES_EN_DLY_1_CYCLE << AUTORES_EN_DLY_SHIFT
			| AUTORES_ERR_WINDOW_50_PERCENT | AUTORES_EN_BIT);
	if (rc < 0)
		goto unlock;

	rc = haptics_config_openloop_lra_period(chip, chip->config.t_lra_us);
	if (rc < 0)
		goto restore;

	rc = haptics_set_vmax_mv(chip, chip->config.vmax_mv);
	if (rc < 0)
		goto restore;

	rc = haptics_set_vmax_headroom_mv(chip, LRA_CALIBRATION_VMAX_HDRM_MV);
	if (rc < 0)
		goto restore;

	rc = haptics_set_direct_play(chip, DIRECT_PLAY_MAX_AMPLITUDE);
	if (rc < 0)
		goto restore;

	chip->play.pattern_src = DIRECT_PLAY;
	rc = haptics_enable_play(chip, true);
	if (rc < 0) {
		dev_err(chip->dev, "Start calibration failed, rc=%d\n", rc);
		goto restore;
	}

	/* wait for ~150ms to get the LRA calibration result */
	usleep_range(150000, 155000);

	rc = haptics_get_closeloop_lra_period(chip);
	if (rc < 0)
		goto restore;

	rc = haptics_enable_play(chip, false);
	if (rc < 0)
		goto restore;

	haptics_config_openloop_lra_period(chip, chip->config.cl_t_lra_us);

restore:
	rc = haptics_write(chip, chip->cfg_addr_base,
			HAP_CFG_AUTORES_CFG_REG, &autores_cfg, 1);
unlock:
	chip->play.in_calibration = false;
	mutex_unlock(&chip->play.lock);
	return rc;
}

static ssize_t lra_calibration_store(struct class *c,
		struct class_attribute *attr, const char *buf, size_t count)
{
	struct haptics_chip *chip = container_of(c,
			struct haptics_chip, hap_class);
	bool val;
	int rc;

	if (kstrtobool(buf, &val))
		return -EINVAL;

	if (val) {
		rc = haptics_start_lra_calibration(chip);
		if (rc < 0)
			return rc;
	}

	return count;
}
static CLASS_ATTR_WO(lra_calibration);

static ssize_t lra_frequency_hz_show(struct class *c,
		struct class_attribute *attr, char *buf)
{
	struct haptics_chip *chip = container_of(c,
			struct haptics_chip, hap_class);
	u32 cl_f_lra;

	if (chip->config.cl_t_lra_us == 0)
		return -EINVAL;

	cl_f_lra = USEC_PER_SEC / chip->config.cl_t_lra_us;
	return scnprintf(buf, PAGE_SIZE, "%d Hz\n", cl_f_lra);
}
static CLASS_ATTR_RO(lra_frequency_hz);

static struct attribute *hap_class_attrs[] = {
	&class_attr_lra_calibration.attr,
	&class_attr_lra_frequency_hz.attr,
	NULL,
};
ATTRIBUTE_GROUPS(hap_class);

static int haptics_probe(struct platform_device *pdev)
{
	struct haptics_chip *chip;
	struct input_dev *input_dev;
	struct ff_device *ff_dev;
	int rc, count;

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	input_dev = devm_input_allocate_device(&pdev->dev);
	if (!input_dev)
		return -ENOMEM;

	chip->dev = &pdev->dev;
	chip->regmap = dev_get_regmap(chip->dev->parent, NULL);
	if (!chip->regmap) {
		dev_err(chip->dev, "Get regmap failed\n");
		return -ENXIO;
	}

	rc = haptics_parse_dt(chip);
	if (rc < 0) {
		dev_err(chip->dev, "Parse device-tree failed, rc = %d\n", rc);
		return rc;
	}

	rc = haptics_init_custom_effect(chip);
	if (rc < 0) {
		dev_err(chip->dev, "Init custom effect failed, rc=%d\n", rc);
		return rc;
	}

	rc = haptics_hw_init(chip);
	if (rc < 0) {
		dev_err(chip->dev, "Initialize HW failed, rc = %d\n", rc);
		return rc;
	}

	rc = haptics_init_swr_slave_regulator(chip);
	if (rc < 0) {
		dev_err(chip->dev, "Initialize swr slave regulator failed, rc = %d\n",
				rc);
		return rc;
	}

	rc = devm_request_threaded_irq(chip->dev, chip->fifo_empty_irq,
			NULL, fifo_empty_irq_handler,
			IRQF_ONESHOT, "fifo-empty", chip);
	if (rc < 0) {
		dev_err(chip->dev, "request fifo-empty IRQ failed, rc=%d\n",
				rc);
		return rc;
	}

	mutex_init(&chip->irq_lock);
	mutex_init(&chip->play.lock);
	disable_irq_nosync(chip->fifo_empty_irq);
	chip->fifo_empty_irq_en = false;

	atomic_set(&chip->play.fifo_status.is_busy, 0);
	atomic_set(&chip->play.fifo_status.written_done, 0);
	atomic_set(&chip->play.fifo_status.cancelled, 0);
	input_dev->name = "qcom-hv-haptics";
	input_set_drvdata(input_dev, chip);
	chip->input_dev = input_dev;

	input_set_capability(input_dev, EV_FF, FF_CONSTANT);
	input_set_capability(input_dev, EV_FF, FF_GAIN);
	if (chip->effects_count != 0) {
		input_set_capability(input_dev, EV_FF, FF_PERIODIC);
		input_set_capability(input_dev, EV_FF, FF_CUSTOM);
	}

	if (chip->effects_count < MAX_EFFECT_COUNT)
		count = chip->effects_count + 1;
	else
		count = MAX_EFFECT_COUNT;

	rc = input_ff_create(input_dev, count);
	if (rc < 0) {
		dev_err(chip->dev, "create input FF device failed, rc=%d\n",
				rc);
		return rc;
	}

	ff_dev = input_dev->ff;
	ff_dev->upload = haptics_upload_effect;
	ff_dev->playback = haptics_playback;
	ff_dev->erase = haptics_erase;
	ff_dev->set_gain = haptics_set_gain;
	rc = input_register_device(input_dev);
	if (rc < 0) {
		dev_err(chip->dev, "register input device failed, rc=%d\n",
				rc);
		goto destroy_ff;
	}

	dev_set_drvdata(chip->dev, chip);
	chip->hap_class.name = "qcom-haptics";
	chip->hap_class.class_groups = hap_class_groups;
	rc = class_register(&chip->hap_class);
	if (rc < 0) {
		dev_err(chip->dev, "register hap_class failed, rc=%d\n");
		goto destroy_ff;
	}

#ifdef CONFIG_DEBUG_FS
	rc = haptics_create_debugfs(chip);
	if (rc < 0)
		dev_err(chip->dev, "Creating debugfs failed, rc=%d\n", rc);
#endif
	return 0;
destroy_ff:
	input_ff_destroy(chip->input_dev);
	return rc;
}

static int haptics_remove(struct platform_device *pdev)
{
	struct haptics_chip *chip = dev_get_drvdata(&pdev->dev);

	class_unregister(&chip->hap_class);
#ifdef CONFIG_DEBUG_FS
	debugfs_remove_recursive(chip->debugfs_dir);
#endif
	input_ff_destroy(chip->input_dev);
	dev_set_drvdata(chip->dev, NULL);

	return 0;
}

static const struct of_device_id haptics_match_table[] = {
	{ .compatible = "qcom,hv-haptics" },
	{ .compatible = "qcom,pm8350b-haptics" },
	{},
};

static struct platform_driver haptics_driver = {
	.driver		= {
		.name = "qcom-hv-haptics",
		.of_match_table = haptics_match_table,
	},
	.probe		= haptics_probe,
	.remove		= haptics_remove,
};
module_platform_driver(haptics_driver);

MODULE_DESCRIPTION("Qualcomm Technologies, Inc. High-Voltage Haptics driver");
MODULE_LICENSE("GPL v2");
