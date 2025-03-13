// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, Caleb Connolly <caleb@connolly.tech>
 * Qualcomm QPMI haptics driver for pmi8998 and related PMICs.
 */
#include <dt-bindings/input/qcom,spmi-haptics.h>

#include <linux/atomic.h>
#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/errno.h>
#include <linux/gpio/consumer.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/log2.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/types.h>

#define HAP_STATUS_1_REG		0x0A
#define HAP_BUSY_BIT			BIT(1)
#define SC_FLAG_BIT			BIT(3)
#define AUTO_RES_ERROR_BIT		BIT(4)

#define HAP_LRA_AUTO_RES_LO_REG		0x0B
#define HAP_LRA_AUTO_RES_HI_REG		0x0C

#define HAP_EN_CTL_REG			0x46
#define HAP_EN_BIT			BIT(7)

#define HAP_EN_CTL2_REG			0x48
#define BRAKE_EN_BIT			BIT(0)

#define HAP_AUTO_RES_CTRL_REG		0x4B
#define AUTO_RES_EN_BIT			BIT(7)
#define AUTO_RES_ERR_RECOVERY_BIT	BIT(3)
#define AUTO_RES_EN_FLAG_BIT		BIT(0)

#define HAP_CFG1_REG			0x4C
#define HAP_ACT_TYPE_MASK		BIT(0)

#define HAP_CFG2_REG			0x4D
#define HAP_LRA_RES_TYPE_MASK		BIT(0)

#define HAP_SEL_REG			0x4E
#define HAP_WF_SOURCE_MASK		GENMASK(5, 4)
#define HAP_WF_SOURCE_SHIFT		4

#define HAP_LRA_AUTO_RES_REG		0x4F
#define LRA_AUTO_RES_MODE_MASK		GENMASK(6, 4)
#define LRA_AUTO_RES_MODE_SHIFT		4
#define LRA_HIGH_Z_MASK			GENMASK(3, 2)
#define LRA_HIGH_Z_SHIFT		2
#define LRA_RES_CAL_MASK		GENMASK(1, 0)
#define HAP_RES_CAL_PERIOD_MIN		4
#define HAP_RES_CAL_PERIOD_MAX		32

#define HAP_VMAX_CFG_REG			0x51
#define HAP_VMAX_OVD_BIT		BIT(6)
#define HAP_VMAX_MASK			GENMASK(5, 1)
#define HAP_VMAX_SHIFT			1

#define HAP_ILIM_CFG_REG			0x52
#define HAP_ILIM_SEL_MASK		BIT(0)
#define HAP_ILIM_400_MA			0
#define HAP_ILIM_800_MA			1

#define HAP_SC_DEB_REG			0x53
#define HAP_SC_DEB_MASK			GENMASK(2, 0)
#define HAP_SC_DEB_CYCLES_MIN		0
#define HAP_DEF_SC_DEB_CYCLES		8
#define HAP_SC_DEB_CYCLES_MAX		32

#define HAP_RATE_CFG1_REG		0x54
#define HAP_RATE_CFG1_MASK		GENMASK(7, 0)
#define HAP_RATE_CFG2_SHIFT		8 // As CFG2 is the most significant byte

#define HAP_RATE_CFG2_REG		0x55
#define HAP_RATE_CFG2_MASK		GENMASK(3, 0)

#define HAP_SC_CLR_REG			0x59
#define SC_CLR_BIT			BIT(0)

#define HAP_BRAKE_REG			0x5C
#define HAP_BRAKE_PAT_MASK		0x3

#define HAP_WF_REPEAT_REG			0x5E
#define WF_REPEAT_MASK			GENMASK(6, 4)
#define WF_REPEAT_SHIFT			4
#define WF_REPEAT_MIN			1
#define WF_REPEAT_MAX			128
#define WF_S_REPEAT_MASK		GENMASK(1, 0)
#define WF_S_REPEAT_MIN			1
#define WF_S_REPEAT_MAX			8

#define HAP_WF_S1_REG			0x60
#define HAP_WF_SIGN_BIT			BIT(7)
#define HAP_WF_OVD_BIT			BIT(6)
#define HAP_WF_SAMP_MAX			GENMASK(5, 1)
#define HAP_WF_SAMPLE_LEN		8

#define HAP_PLAY_REG			0x70
#define HAP_PLAY_BIT			BIT(7)
#define HAP_PAUSE_BIT			BIT(0)

#define HAP_SEC_ACCESS_REG		0xD0
#define HAP_SEC_ACCESS_UNLOCK		0xA5

#define HAP_TEST2_REG			0xE3


#define HAP_VMAX_MIN_MV			116
#define HAP_VMAX_MAX_MV			3596
#define HAP_VMAX_MAX_MV_STRONG		3596

#define HAP_WAVE_PLAY_RATE_MIN_US	0
#define HAP_WAVE_PLAY_RATE_MAX_US	20475
#define HAP_WAVE_PLAY_TIME_MAX_MS	15000

#define AUTO_RES_ERR_POLL_TIME_NS	(20 * NSEC_PER_MSEC)
#define HAPTICS_BACK_EMF_DELAY_US	20000

#define HAP_BRAKE_PAT_LEN		4
#define HAP_WAVE_SAMP_LEN		8
#define NUM_WF_SET			4
#define HAP_WAVE_SAMP_SET_LEN		(HAP_WAVE_SAMP_LEN * NUM_WF_SET)
#define HAP_RATE_CFG_STEP_US		5

#define SC_MAX_COUNT			5
#define SC_COUNT_RST_DELAY_US		1000000

enum hap_play_control {
	HAP_STOP,
	HAP_PAUSE,
	HAP_PLAY,
};

/**
 * struct spmi_haptics - struct for spmi haptics data.
 *
 * @dev: Our device parent.
 * @regmap: Register map for the hardware block.
 * @input_dev: The input device used to receive events.
 * @work: Work struct to play effects.
 * @base: Base address of the regmap.
 * @active: Atomic value used to track if haptics are currently playing.
 * @play_irq: Fired to load the next wave pattern.
 * @sc_irq: Short circuit irq.
 * @last_sc_time: Time since the short circuit IRQ last fired.
 * @sc_count: Number of times the short circuit IRQ has fired in this interval.
 * @actuator_type: The type of actuator in use.
 * @wave_shape: The shape of the waves to use (sine or square).
 * @play_mode: The play mode to use (direct, buffer, pwm, audio).
 * @magnitude: The strength we should be playing at.
 * @vmax: Max voltage to use when playing.
 * @current_limit: The current limit for this hardware (400mA or 800mA).
 * @play_wave_rate: The wave rate to use for this hardware.
 * @wave_samp: The array of wave samples to write for buffer mode.
 * @brake_pat: The pattern to apply when braking.
 * @play_lock: Lock to be held when updating the hardware state.
 */
struct spmi_haptics {
	struct device *dev;
	struct regmap *regmap;
	struct input_dev *haptics_input_dev;
	struct gpio_desc *boost;
	struct work_struct work;
	u32 base;

	atomic_t active;

	int play_irq;
	int sc_irq;
	ktime_t last_sc_time;
	u8 sc_count;

	u8 actuator_type;
	u8 wave_shape;
	u8 play_mode;
	int magnitude;
	u32 vmax;
	u32 current_limit;
	u32 play_wave_rate;

	u32 wave_samp[HAP_WAVE_SAMP_SET_LEN];
	u8 brake_pat[HAP_BRAKE_PAT_LEN];

	struct mutex play_lock;
};

static inline bool is_secure_addr(u16 addr)
{
	return (addr & 0xFF) > 0xD0;
}

static int spmi_haptics_read(struct spmi_haptics *haptics,
	u16 addr, u8 *val, int len)
{
	int ret;

	ret = regmap_bulk_read(haptics->regmap, addr, val, len);
	if (ret < 0)
		dev_err(haptics->dev, "Error reading address: 0x%x, ret %d\n", addr, ret);

	return ret;
}

static int spmi_haptics_write(struct spmi_haptics *haptics,
			      u16 addr, u8 *val, int len)
{
	int ret, i;

	if (is_secure_addr(addr)) {
		for (i = 0; i < len; i++) {
			dev_dbg(haptics->dev, "%s: unlocking for addr: 0x%x, val: 0x%x", __func__,
				addr, val[i]);
			ret = regmap_write(haptics->regmap,
				haptics->base + HAP_SEC_ACCESS_REG, HAP_SEC_ACCESS_UNLOCK);
			if (ret < 0) {
				dev_err(haptics->dev, "Error writing unlock code, ret %d\n",
					ret);
				return ret;
			}

			ret = regmap_write(haptics->regmap, addr + i, val[i]);
			if (ret < 0) {
				dev_err(haptics->dev, "Error writing address 0x%x, ret %d\n",
					addr + i, ret);
				return ret;
			}
		}
	} else {
		if (len > 1)
			ret = regmap_bulk_write(haptics->regmap, addr, val, len);
		else
			ret = regmap_write(haptics->regmap, addr, *val);
	}

	if (ret < 0)
		dev_err(haptics->dev, "%s: Error writing address: 0x%x, ret %d\n",
			__func__, addr, ret);

	return ret;
}

static int spmi_haptics_write_masked(struct spmi_haptics *haptics,
	u16 addr, u8 mask, u8 val)
{
	int ret;

	if (is_secure_addr(addr)) {
		ret = regmap_write(haptics->regmap,
			haptics->base + HAP_SEC_ACCESS_REG, HAP_SEC_ACCESS_UNLOCK);
		if (ret < 0) {
			dev_err(haptics->dev, "Error writing unlock code - ret %d\n", ret);
			return ret;
		}
	}

	ret = regmap_update_bits(haptics->regmap, addr, mask, val);
	if (ret < 0)
		dev_err(haptics->dev, "Error writing address: 0x%x - ret %d\n", addr, ret);

	return ret;
}

static bool is_haptics_idle(struct spmi_haptics *haptics)
{
	int ret;
	u8 val;

	if (haptics->play_mode == HAP_PLAY_DIRECT ||
			haptics->play_mode == HAP_PLAY_PWM)
		return true;

	ret = spmi_haptics_read(haptics, haptics->base + HAP_STATUS_1_REG, &val, 1);
	if (ret < 0 || (val & HAP_BUSY_BIT))
		return false;

	return true;
}

static int spmi_haptics_module_enable(struct spmi_haptics *haptics, bool enable)
{
	u8 val;

	dev_dbg(haptics->dev, "Setting module enable: %d", enable);

	val = enable ? HAP_EN_BIT : 0;
	return spmi_haptics_write(haptics, haptics->base + HAP_EN_CTL_REG, &val, 1);
}

static int spmi_haptics_write_vmax(struct spmi_haptics *haptics)
{
	u8 val = 0;
	u32 vmax_mv = haptics->vmax;

	vmax_mv = clamp_t(u32, vmax_mv, HAP_VMAX_MIN_MV, HAP_VMAX_MAX_MV);

	dev_dbg(haptics->dev, "Setting vmax to: %d", vmax_mv);

	val = DIV_ROUND_CLOSEST(vmax_mv, HAP_VMAX_MIN_MV);
	val = FIELD_PREP(HAP_VMAX_MASK, val);

	// TODO: pm660 can enable overdrive here

	return spmi_haptics_write_masked(haptics, haptics->base + HAP_VMAX_CFG_REG,
					 HAP_VMAX_MASK | HAP_WF_OVD_BIT, val);
}

static int spmi_haptics_write_current_limit(struct spmi_haptics *haptics)
{
	haptics->current_limit = clamp_t(u32, haptics->current_limit,
					 HAP_ILIM_400_MA, HAP_ILIM_800_MA);

	dev_dbg(haptics->dev, "Setting current_limit to: 0x%x", haptics->current_limit);

	return spmi_haptics_write_masked(haptics, haptics->base + HAP_ILIM_CFG_REG,
			HAP_ILIM_SEL_MASK, haptics->current_limit);
}

static int spmi_haptics_write_play_mode(struct spmi_haptics *haptics)
{
	u8 val = 0;

	if (!is_haptics_idle(haptics))
		return -EBUSY;

	dev_dbg(haptics->dev, "Setting play_mode to: 0x%x", haptics->play_mode);

	val = FIELD_PREP(HAP_WF_SOURCE_MASK, haptics->play_mode);
	return spmi_haptics_write_masked(haptics, haptics->base + HAP_SEL_REG,
					 HAP_WF_SOURCE_MASK, val);

}

static int spmi_haptics_write_play_rate(struct spmi_haptics *haptics, u16 play_rate)
{
	u8 val[2];

	dev_dbg(haptics->dev, "Setting play_rate to: %d", play_rate);

	val[0] = FIELD_PREP(HAP_RATE_CFG1_MASK, play_rate);
	val[1] = FIELD_PREP(HAP_RATE_CFG2_MASK, play_rate >> HAP_RATE_CFG2_SHIFT);
	return spmi_haptics_write(haptics, haptics->base + HAP_RATE_CFG1_REG, val, 2);
}

/*
 * spmi_haptics_set_auto_res() - Auto resonance
 * allows the haptics to automatically adjust the
 * speed of the oscillation in order to maintain
 * the resonant frequency.
 */
static int spmi_haptics_set_auto_res(struct spmi_haptics *haptics, bool enable)
{
	u8 val;

	// LRAs are the only type to support auto res
	if (haptics->actuator_type != HAP_TYPE_LRA)
		return 0;

	val = enable ? AUTO_RES_EN_BIT : 0;

	return spmi_haptics_write_masked(haptics, haptics->base + HAP_TEST2_REG,
				       AUTO_RES_EN_BIT, val);
}

static int spmi_haptics_write_brake(struct spmi_haptics *haptics)
{
	int ret, i;
	u8 val;

	dev_dbg(haptics->dev, "Configuring brake pattern");

	ret = spmi_haptics_write_masked(haptics, haptics->base + HAP_EN_CTL2_REG,
					BRAKE_EN_BIT, 1);
	if (ret < 0)
		return ret;

	for (i = HAP_BRAKE_PAT_LEN - 1, val = 0; i >= 0; i--) {
		u8 p = haptics->brake_pat[i] & HAP_BRAKE_PAT_MASK;

		val |= p << (i * 2);
	}

	return spmi_haptics_write(haptics, haptics->base + HAP_BRAKE_REG, &val, 1);
}

static int spmi_haptics_write_buffer_config(struct spmi_haptics *haptics)
{
	u8 buf[HAP_WAVE_SAMP_LEN];
	int i;

	dev_dbg(haptics->dev, "Writing buffer config");

	for (i = 0; i < HAP_WAVE_SAMP_LEN; i++)
		buf[i] = haptics->wave_samp[i];

	return spmi_haptics_write(haptics, haptics->base + HAP_WF_S1_REG, buf,
				  HAP_WAVE_SAMP_LEN);
}

/*
 * spmi_haptics_write_wave_repeat() - write wave repeat values.
 */
static int spmi_haptics_write_wave_repeat(struct spmi_haptics *haptics)
{
	u8 val, mask;

	/* The number of times to repeat each wave */
	mask = WF_REPEAT_MASK | WF_S_REPEAT_MASK;
	val = FIELD_PREP(WF_REPEAT_MASK, 0) |
	      FIELD_PREP(WF_S_REPEAT_MASK, 0);

	return spmi_haptics_write_masked(haptics, haptics->base + HAP_WF_REPEAT_REG,
					 mask, val);
}

static int spmi_haptics_write_play_control(struct spmi_haptics *haptics,
						enum hap_play_control ctrl)
{
	u8 val;

	switch (ctrl) {
	case HAP_STOP:
		val = 0;
		break;
	case HAP_PAUSE:
		val = HAP_PAUSE_BIT;
		break;
	case HAP_PLAY:
		val = HAP_PLAY_BIT;
		break;
	default:
		return 0;
	}

	dev_dbg(haptics->dev, "haptics play ctrl: %d\n", ctrl);
	return spmi_haptics_write(haptics, haptics->base + HAP_PLAY_REG, &val, 1);
}

/*
 * This IRQ is fired to tell us to load the next wave sample set.
 * As we only currently support a single sample set, it's unused.
 */
static irqreturn_t spmi_haptics_play_irq_handler(int irq, void *data)
{
	struct spmi_haptics *haptics = data;

	dev_dbg(haptics->dev, "play_irq triggered");

	return IRQ_HANDLED;
}

/*
 * Fires every ~50ms whilst the haptics are active.
 * If the SC_FLAG_BIT is set then that means there isn't a short circuit
 * and we just need to clear the IRQ to indicate that the device should
 * keep vibrating.
 *
 * Otherwise, it means a short circuit situation has occurred.
 */
static irqreturn_t spmi_haptics_sc_irq_handler(int irq, void *data)
{
	struct spmi_haptics *haptics = data;
	int ret;
	u8 val;
	s64 sc_delta_time_us;
	ktime_t temp;

	ret = spmi_haptics_read(haptics, haptics->base + HAP_STATUS_1_REG, &val, 1);
	if (ret < 0)
		return IRQ_HANDLED;

	if (!(val & SC_FLAG_BIT)) {
		haptics->sc_count = 0;
		return IRQ_HANDLED;
	}

	temp = ktime_get();
	sc_delta_time_us = ktime_us_delta(temp, haptics->last_sc_time);
	haptics->last_sc_time = temp;

	if (sc_delta_time_us > SC_COUNT_RST_DELAY_US)
		haptics->sc_count = 0;
	else
		haptics->sc_count++;

	// Clear the interrupt flag
	val = SC_CLR_BIT;
	ret = spmi_haptics_write(haptics, haptics->base + HAP_SC_CLR_REG, &val, 1);
	if (ret < 0)
		return IRQ_HANDLED;

	if (haptics->sc_count > SC_MAX_COUNT) {
		dev_crit(haptics->dev, "Short circuit persists, disabling haptics\n");
		ret = spmi_haptics_module_enable(haptics, false);
		if (ret < 0)
			dev_err(haptics->dev, "Error disabling module, rc=%d\n", ret);
	}

	return IRQ_HANDLED;
}


/**
 * spmi_haptics_init() - Initialise haptics hardware for use
 * @haptics: haptics device
 * Returns: 0 on success, < 0 on error
 */
static int spmi_haptics_init(struct spmi_haptics *haptics)
{
	int ret;
	u8 val, mask;
	u16 play_rate;

	ret = spmi_haptics_write_masked(haptics, haptics->base + HAP_CFG1_REG,
		HAP_ACT_TYPE_MASK, haptics->actuator_type);
	if (ret < 0)
		return ret;

	/*
	 * Configure auto resonance
	 * see spmi_haptics_lra_auto_res_config downstream
	 * This is greatly simplified.
	 */
	val = FIELD_PREP(LRA_RES_CAL_MASK, ilog2(32 / HAP_RES_CAL_PERIOD_MIN)) |
	      FIELD_PREP(LRA_AUTO_RES_MODE_MASK, HAP_AUTO_RES_ZXD_EOP) |
	      FIELD_PREP(LRA_HIGH_Z_MASK, 1);

	mask = LRA_AUTO_RES_MODE_MASK | LRA_HIGH_Z_MASK | LRA_RES_CAL_MASK;

	ret = spmi_haptics_write_masked(haptics, haptics->base + HAP_LRA_AUTO_RES_REG,
			mask, val);

	/* Configure the PLAY MODE register */
	ret = spmi_haptics_write_play_mode(haptics);
	if (ret < 0)
		return ret;

	ret = spmi_haptics_write_vmax(haptics);
	if (ret < 0)
		return ret;

	/* Configure the ILIM register */
	ret = spmi_haptics_write_current_limit(haptics);
	if (ret < 0)
		return ret;

	// Configure the debounce for short-circuit detection.
	ret = spmi_haptics_write_masked(haptics, haptics->base + HAP_SC_DEB_REG,
			HAP_SC_DEB_MASK, HAP_SC_DEB_CYCLES_MAX);
	if (ret < 0)
		return ret;

	// write the wave shape
	ret = spmi_haptics_write_masked(haptics, haptics->base + HAP_CFG2_REG,
			HAP_LRA_RES_TYPE_MASK, haptics->wave_shape);
	if (ret < 0)
		return ret;

	play_rate = haptics->play_wave_rate / HAP_RATE_CFG_STEP_US;

	/*
	 * Configure RATE_CFG1 and RATE_CFG2 registers.
	 * Note: For ERM these registers act as play rate and
	 * for LRA these represent resonance period
	 */
	ret = spmi_haptics_write_play_rate(haptics, play_rate);
	if (ret < 0)
		return ret;

	ret = spmi_haptics_write_brake(haptics);
	if (ret < 0)
		return ret;

	if (haptics->play_mode == HAP_PLAY_BUFFER) {
		ret = spmi_haptics_write_wave_repeat(haptics);
		if (ret < 0)
			return ret;

		ret = spmi_haptics_write_buffer_config(haptics);
		if (ret < 0)
			return ret;
	}

	if (haptics->play_irq >= 0) {
		dev_dbg(haptics->dev, "%s: Requesting play IRQ, irq: %d", __func__,
			haptics->play_irq);
		ret = devm_request_threaded_irq(haptics->dev, haptics->play_irq,
			NULL, spmi_haptics_play_irq_handler, IRQF_ONESHOT,
			"haptics_play_irq", haptics);

		if (ret < 0) {
			dev_err(haptics->dev, "Unable to request play IRQ ret=%d\n", ret);
			return ret;
		}

		/* use play_irq only for buffer mode */
		if (haptics->play_mode != HAP_PLAY_BUFFER)
			disable_irq(haptics->play_irq);
	}

	if (haptics->sc_irq >= 0) {
		dev_dbg(haptics->dev, "%s: Requesting play IRQ, irq: %d", __func__,
			haptics->play_irq);
		ret = devm_request_threaded_irq(haptics->dev, haptics->sc_irq,
			NULL, spmi_haptics_sc_irq_handler, IRQF_ONESHOT,
			"haptics_sc_irq", haptics);

		if (ret < 0) {
			dev_err(haptics->dev, "Unable to request sc IRQ ret=%d\n", ret);
			return ret;
		}
	}

	return 0;
}

/**
 * spmi_haptics_enable - handler to start/stop vibration
 * @haptics: pointer to haptics struct
 * @enable: state to set
 * Returns: 0 on success, < 0 on failure
 */
static int spmi_haptics_enable(struct spmi_haptics *haptics)
{
	int ret;

	mutex_lock(&haptics->play_lock);
	if (haptics->sc_count > SC_MAX_COUNT) {
		dev_err(haptics->dev, "Can't play while in short circuit");
		ret = -1;
		goto out;
	}
	ret = spmi_haptics_set_auto_res(haptics, false);
	if (ret < 0) {
		dev_err(haptics->dev, "Error disabling auto_res, ret=%d\n", ret);
		goto out;
	}

	ret = spmi_haptics_module_enable(haptics, true);
	if (ret < 0) {
		dev_err(haptics->dev, "Error enabling module, ret=%d\n", ret);
		goto out;
	}

	ret = spmi_haptics_write_play_control(haptics, HAP_PLAY);
	if (ret < 0) {
		dev_err(haptics->dev, "Error enabling play, ret=%d\n", ret);
		goto out;
	}

	ret = spmi_haptics_set_auto_res(haptics, true);
	if (ret < 0) {
		dev_err(haptics->dev, "Error enabling auto_res, ret=%d\n", ret);
		goto out;
	}

out:
	mutex_unlock(&haptics->play_lock);
	return ret;
}

/**
 * spmi_haptics_enable - handler to start/stop vibration
 * @haptics: pointer to haptics struct
 * @enable: state to set
 * Returns: 0 on success, < 0 on failure
 */
static int spmi_haptics_disable(struct spmi_haptics *haptics)
{
	int ret;

	mutex_lock(&haptics->play_lock);

	ret = spmi_haptics_write_play_control(haptics, HAP_STOP);
	if (ret < 0) {
		dev_err(haptics->dev, "Error disabling play, ret=%d\n", ret);
		goto out;
	}

	ret = spmi_haptics_module_enable(haptics, false);
	if (ret < 0) {
		dev_err(haptics->dev, "Error disabling module, ret=%d\n", ret);
		goto out;
	}

out:
	mutex_unlock(&haptics->play_lock);
	return ret;
}

/*
 * Threaded function to update the haptics state.
 */
static void spmi_haptics_work(struct work_struct *work)
{
	struct spmi_haptics *haptics = container_of(work, struct spmi_haptics, work);

	int ret;
	bool enable;

	enable = atomic_read(&haptics->active);
	dev_dbg(haptics->dev, "%s: state: %d\n", __func__, enable);

	if (enable)
		ret = spmi_haptics_enable(haptics);
	else
		ret = spmi_haptics_disable(haptics);
	if (ret < 0)
		dev_err(haptics->dev, "Error setting haptics, ret=%d", ret);
}

/**
 * spmi_haptics_close - callback for input device close
 * @dev: input device pointer
 *
 * Turns off the vibrator.
 */
static void spmi_haptics_close(struct input_dev *dev)
{
	struct spmi_haptics *haptics = input_get_drvdata(dev);

	cancel_work_sync(&haptics->work);
	if (atomic_read(&haptics->active)) {
		atomic_set(&haptics->active, 0);
		schedule_work(&haptics->work);
	}
}

/**
 * spmi_haptics_play_effect - play haptics effects
 * @dev: input device pointer
 * @data: data of effect
 * @effect: effect to play
 */
static int spmi_haptics_play_effect(struct input_dev *dev, void *data,
					struct ff_effect *effect)
{
	struct spmi_haptics *haptics = input_get_drvdata(dev);

	dev_dbg(haptics->dev, "%s: Rumbling with strong: %d and weak: %d", __func__,
		effect->u.rumble.strong_magnitude, effect->u.rumble.weak_magnitude);

	haptics->magnitude = effect->u.rumble.strong_magnitude >> 8;
	if (!haptics->magnitude)
		haptics->magnitude = effect->u.rumble.weak_magnitude >> 10;

	if (!haptics->magnitude) {
		atomic_set(&haptics->active, 0);
		goto end;
	}

	atomic_set(&haptics->active, 1);

	haptics->vmax = ((HAP_VMAX_MAX_MV - HAP_VMAX_MIN_MV) * haptics->magnitude) / 100 +
					HAP_VMAX_MIN_MV;

	dev_dbg(haptics->dev, "%s: magnitude: %d, vmax: %d", __func__,
		haptics->magnitude, haptics->vmax);

	spmi_haptics_write_vmax(haptics);

end:
	schedule_work(&haptics->work);

	return 0;
}

static int spmi_haptics_probe(struct platform_device *pdev)
{
	struct spmi_haptics *haptics;
	struct device_node *node;
	struct input_dev *input_dev;
	int ret;
	u32 val;
	int i;

	haptics = devm_kzalloc(&pdev->dev, sizeof(*haptics), GFP_KERNEL);
	if (!haptics)
		return -ENOMEM;

	haptics->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!haptics->regmap)
		return -ENODEV;

	node = pdev->dev.of_node;

	haptics->dev = &pdev->dev;

	ret = of_property_read_u32(node, "reg", &haptics->base);
	if (ret < 0) {
		dev_err(haptics->dev, "Couldn't find reg in node = %s ret = %d\n",
			node->full_name, ret);
		return ret;
	}

	haptics->play_irq = platform_get_irq_byname(pdev, "play");
	if (haptics->play_irq < 0) {
		dev_err(&pdev->dev, "Unable to get play irq\n");
		ret = haptics->play_irq;
		goto register_fail;
	}

	haptics->sc_irq = platform_get_irq_byname(pdev, "short");
	if (haptics->sc_irq < 0) {
		dev_err(&pdev->dev, "Unable to get sc irq\n");
		ret = haptics->sc_irq;
		goto register_fail;
	}

	// We only support LRAs for now
	haptics->actuator_type = HAP_TYPE_LRA;
	ret = of_property_read_u32(node, "qcom,actuator-type", &val);
	if (!ret) {
		haptics->actuator_type = val;
	}

	// Only buffer mode is currently supported
	haptics->play_mode = HAP_PLAY_BUFFER;
	ret = of_property_read_u32(node, "qcom,play-mode", &val);
	if (!ret) {
		if (val != HAP_PLAY_BUFFER) {
			dev_err(&pdev->dev, "qcom,play-mode (%d) isn't supported\n", val);
			ret = -EINVAL;
			goto register_fail;
		}
		haptics->play_mode = val;
	}

	ret = of_property_read_u32(node, "qcom,wave-play-rate-us", &val);
	if (!ret) {
		haptics->play_wave_rate = val;
	} else if (ret != -EINVAL) {
		dev_err(haptics->dev, "Unable to read play rate ret=%d\n", ret);
		goto register_fail;
	}

	haptics->play_wave_rate =
		clamp_t(u32, haptics->play_wave_rate,
			HAP_WAVE_PLAY_RATE_MIN_US, HAP_WAVE_PLAY_RATE_MAX_US);

	haptics->wave_shape = HAP_WAVE_SINE;
	ret = of_property_read_u32(node, "qcom,wave-shape", &val);
	if (!ret) {
		if (val != HAP_WAVE_SINE && val != HAP_WAVE_SQUARE) {
			dev_err(&pdev->dev, "qcom,wave-shape is invalid: %d\n", val);
			ret = -EINVAL;
			goto register_fail;
		}
		haptics->wave_shape = val;
	}

	haptics->brake_pat[0] = 0x3;
	haptics->brake_pat[1] = 0x3;
	haptics->brake_pat[2] = 0x2;
	haptics->brake_pat[3] = 0x1;

	ret = of_property_read_u8_array(node, "qcom,brake-pattern", haptics->brake_pat, 4);
	if (ret < 0 && ret != -EINVAL) {
		dev_err(&pdev->dev, "qcom,brake-pattern is invalid, ret = %d\n", ret);
		goto register_fail;
	}

	haptics->boost =
		devm_gpiod_get_optional(haptics->dev, "qcom,boost", GPIOD_OUT_HIGH);
	if (IS_ERR(haptics->boost)) {
		ret = PTR_ERR(haptics->boost);
		dev_warn(haptics->dev, "Unable to get boost gpio: %d\n", ret);
	}

	if (haptics->boost)
		gpiod_set_value_cansleep(haptics->boost, 1);

	haptics->current_limit = HAP_ILIM_400_MA;

	for (i = 0; i < HAP_WAVE_SAMP_LEN; i++)
		haptics->wave_samp[i] = HAP_WF_SAMP_MAX;

	ret = spmi_haptics_init(haptics);
	if (ret < 0) {
		dev_err(&pdev->dev, "Error initialising haptics, ret=%d\n",
			ret);
		goto register_fail;
	}

	platform_set_drvdata(pdev, haptics);

	input_dev = devm_input_allocate_device(&pdev->dev);
	if (!input_dev)
		return -ENOMEM;

	INIT_WORK(&haptics->work, spmi_haptics_work);
	haptics->haptics_input_dev = input_dev;

	input_dev->name = "spmi_haptics";
	input_dev->id.version = 1;
	input_dev->close = spmi_haptics_close;
	input_set_drvdata(input_dev, haptics);
	// Figure out how to make this FF_PERIODIC
	input_set_capability(haptics->haptics_input_dev, EV_FF, FF_RUMBLE);

	ret = input_ff_create_memless(input_dev, NULL,
					spmi_haptics_play_effect);
	if (ret) {
		dev_err(&pdev->dev,
			"couldn't register vibrator as FF device\n");
		goto register_fail;
	}

	ret = input_register_device(input_dev);
	if (ret) {
		dev_err(&pdev->dev, "couldn't register input device\n");
		goto register_fail;
	}

	return 0;

register_fail:
	cancel_work_sync(&haptics->work);
	mutex_destroy(&haptics->play_lock);

	return ret;
}

static int __maybe_unused spmi_haptics_suspend(struct device *dev)
{
	struct spmi_haptics *haptics = dev_get_drvdata(dev);

	cancel_work_sync(&haptics->work);
	spmi_haptics_disable(haptics);

	return 0;
}

static SIMPLE_DEV_PM_OPS(spmi_haptics_pm_ops, spmi_haptics_suspend, NULL);

static void spmi_haptics_remove(struct platform_device *pdev)
{
	struct spmi_haptics *haptics = dev_get_drvdata(&pdev->dev);

	cancel_work_sync(&haptics->work);
	mutex_destroy(&haptics->play_lock);
	input_unregister_device(haptics->haptics_input_dev);

	if (haptics->boost)
		gpiod_set_value_cansleep(haptics->boost, 0);
}

static void spmi_haptics_shutdown(struct platform_device *pdev)
{
	struct spmi_haptics *haptics = dev_get_drvdata(&pdev->dev);

	cancel_work_sync(&haptics->work);

	spmi_haptics_disable(haptics);

	if (haptics->boost)
		gpiod_set_value_cansleep(haptics->boost, 0);
}

static const struct of_device_id spmi_haptics_match_table[] = {
	{ .compatible = "qcom,spmi-haptics" },
	{ }
};
MODULE_DEVICE_TABLE(of, spmi_haptics_match_table);

static struct platform_driver spmi_haptics_driver = {
	.probe		= spmi_haptics_probe,
	.remove		= spmi_haptics_remove,
	.shutdown	= spmi_haptics_shutdown,
	.driver		= {
		.name	= "spmi-haptics",
		.pm	= &spmi_haptics_pm_ops,
		.of_match_table = spmi_haptics_match_table,
	},
};
module_platform_driver(spmi_haptics_driver);

MODULE_DESCRIPTION("spmi haptics driver using ff-memless framework");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Caleb Connolly <caleb@connolly.tech>");
