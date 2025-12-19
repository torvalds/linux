// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2022 Markus Gothe <markus.gothe@genexis.eu>
 * Copyright 2025 Christian Marangi <ansuelsmth@gmail.com>
 *
 *  Limitations:
 *  - Only 8 concurrent waveform generators are available for 8 combinations of
 *    duty_cycle and period. Waveform generators are shared between 16 GPIO
 *    pins and 17 SIPO GPIO pins.
 *  - Supports only normal polarity.
 *  - On configuration the currently running period is completed.
 *  - Minimum supported period is 4 ms
 *  - Maximum supported period is 1s
 */

#include <linux/array_size.h>
#include <linux/bitfield.h>
#include <linux/bitmap.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/math64.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/regmap.h>
#include <linux/types.h>

#define AIROHA_PWM_REG_SGPIO_LED_DATA		0x0024
#define AIROHA_PWM_SGPIO_LED_DATA_SHIFT_FLAG	BIT(31)
#define AIROHA_PWM_SGPIO_LED_DATA_DATA		GENMASK(16, 0)

#define AIROHA_PWM_REG_SGPIO_CLK_DIVR		0x0028
#define AIROHA_PWM_SGPIO_CLK_DIVR		GENMASK(1, 0)
#define AIROHA_PWM_SGPIO_CLK_DIVR_32		FIELD_PREP_CONST(AIROHA_PWM_SGPIO_CLK_DIVR, 3)
#define AIROHA_PWM_SGPIO_CLK_DIVR_16		FIELD_PREP_CONST(AIROHA_PWM_SGPIO_CLK_DIVR, 2)
#define AIROHA_PWM_SGPIO_CLK_DIVR_8		FIELD_PREP_CONST(AIROHA_PWM_SGPIO_CLK_DIVR, 1)
#define AIROHA_PWM_SGPIO_CLK_DIVR_4		FIELD_PREP_CONST(AIROHA_PWM_SGPIO_CLK_DIVR, 0)

#define AIROHA_PWM_REG_SGPIO_CLK_DLY		0x002c

#define AIROHA_PWM_REG_SIPO_FLASH_MODE_CFG	0x0030
#define AIROHA_PWM_SERIAL_GPIO_FLASH_MODE	BIT(1)
#define AIROHA_PWM_SERIAL_GPIO_MODE_74HC164	BIT(0)

#define AIROHA_PWM_REG_GPIO_FLASH_PRD_SET(_n)	(0x003c + (4 * (_n)))
#define AIROHA_PWM_REG_GPIO_FLASH_PRD_SHIFT(_n) (16 * (_n))
#define AIROHA_PWM_GPIO_FLASH_PRD_LOW		GENMASK(15, 8)
#define AIROHA_PWM_GPIO_FLASH_PRD_HIGH		GENMASK(7, 0)

#define AIROHA_PWM_REG_GPIO_FLASH_MAP(_n)	(0x004c + (4 * (_n)))
#define AIROHA_PWM_REG_GPIO_FLASH_MAP_SHIFT(_n) (4 * (_n))
#define AIROHA_PWM_GPIO_FLASH_EN		BIT(3)
#define AIROHA_PWM_GPIO_FLASH_SET_ID		GENMASK(2, 0)

/* Register map is equal to GPIO flash map */
#define AIROHA_PWM_REG_SIPO_FLASH_MAP(_n)	(0x0054 + (4 * (_n)))

#define AIROHA_PWM_REG_CYCLE_CFG_VALUE(_n)	(0x0098 + (4 * (_n)))
#define AIROHA_PWM_REG_CYCLE_CFG_SHIFT(_n)	(8 * (_n))
#define AIROHA_PWM_WAVE_GEN_CYCLE		GENMASK(7, 0)

/* GPIO/SIPO flash map handles 8 pins in one register */
#define AIROHA_PWM_PINS_PER_FLASH_MAP		8
/* Cycle(Period) registers handles 4 generators in one 32-bit register */
#define AIROHA_PWM_BUCKET_PER_CYCLE_CFG		4
/* Flash(Duty) producer handles 2 generators in one 32-bit register */
#define AIROHA_PWM_BUCKET_PER_FLASH_PROD	2

#define AIROHA_PWM_NUM_BUCKETS			8
/*
 * The first 16 GPIO pins, GPIO0-GPIO15, are mapped into 16 PWM channels, 0-15.
 * The SIPO GPIO pins are 17 pins which are mapped into 17 PWM channels, 16-32.
 * However, we've only got 8 concurrent waveform generators and can therefore
 * only use up to 8 different combinations of duty cycle and period at a time.
 */
#define AIROHA_PWM_NUM_GPIO			16
#define AIROHA_PWM_NUM_SIPO			17
#define AIROHA_PWM_MAX_CHANNELS			(AIROHA_PWM_NUM_GPIO + AIROHA_PWM_NUM_SIPO)

struct airoha_pwm_bucket {
	/* Concurrent access protected by PWM core */
	int used;
	u32 period_ticks;
	u32 duty_ticks;
};

struct airoha_pwm {
	struct regmap *regmap;

	DECLARE_BITMAP(initialized, AIROHA_PWM_MAX_CHANNELS);

	struct airoha_pwm_bucket buckets[AIROHA_PWM_NUM_BUCKETS];

	/* Cache bucket used by each pwm channel */
	u8 channel_bucket[AIROHA_PWM_MAX_CHANNELS];
};

/* The PWM hardware supports periods between 4 ms and 1 s */
#define AIROHA_PWM_PERIOD_TICK_NS	(4 * NSEC_PER_MSEC)
#define AIROHA_PWM_PERIOD_MAX_NS	(1 * NSEC_PER_SEC)
/* It is represented internally as 1/250 s between 1 and 250. Unit is ticks. */
#define AIROHA_PWM_PERIOD_MIN		1
#define AIROHA_PWM_PERIOD_MAX		250
/* Duty cycle is relative with 255 corresponding to 100% */
#define AIROHA_PWM_DUTY_FULL		255

static void airoha_pwm_get_flash_map_addr_and_shift(unsigned int hwpwm,
						    u32 *addr, u32 *shift)
{
	unsigned int offset, hwpwm_bit;

	if (hwpwm >= AIROHA_PWM_NUM_GPIO) {
		unsigned int sipohwpwm = hwpwm - AIROHA_PWM_NUM_GPIO;

		offset = sipohwpwm / AIROHA_PWM_PINS_PER_FLASH_MAP;
		hwpwm_bit = sipohwpwm % AIROHA_PWM_PINS_PER_FLASH_MAP;

		/* One FLASH_MAP register handles 8 pins */
		*shift = AIROHA_PWM_REG_GPIO_FLASH_MAP_SHIFT(hwpwm_bit);
		*addr = AIROHA_PWM_REG_SIPO_FLASH_MAP(offset);
	} else {
		offset = hwpwm / AIROHA_PWM_PINS_PER_FLASH_MAP;
		hwpwm_bit = hwpwm % AIROHA_PWM_PINS_PER_FLASH_MAP;

		/* One FLASH_MAP register handles 8 pins */
		*shift = AIROHA_PWM_REG_GPIO_FLASH_MAP_SHIFT(hwpwm_bit);
		*addr = AIROHA_PWM_REG_GPIO_FLASH_MAP(offset);
	}
}

static u32 airoha_pwm_get_period_ticks_from_ns(u32 period_ns)
{
	return period_ns / AIROHA_PWM_PERIOD_TICK_NS;
}

static u32 airoha_pwm_get_duty_ticks_from_ns(u32 period_ns, u32 duty_ns)
{
	return mul_u64_u32_div(duty_ns, AIROHA_PWM_DUTY_FULL, period_ns);
}

static u32 airoha_pwm_get_period_ns_from_ticks(u32 period_tick)
{
	return period_tick * AIROHA_PWM_PERIOD_TICK_NS;
}

static u32 airoha_pwm_get_duty_ns_from_ticks(u32 period_tick, u32 duty_tick)
{
	u32 period_ns = period_tick * AIROHA_PWM_PERIOD_TICK_NS;

	/*
	 * Overflow can't occur in multiplication as duty_tick is just 8 bit
	 * and period_ns is clamped to AIROHA_PWM_PERIOD_MAX_NS and fit in a
	 * u64.
	 */
	return DIV_U64_ROUND_UP(duty_tick * period_ns, AIROHA_PWM_DUTY_FULL);
}

static int airoha_pwm_get_bucket(struct airoha_pwm *pc, int bucket,
				 u64 *period_ns, u64 *duty_ns)
{
	struct regmap *map = pc->regmap;
	u32 period_tick, duty_tick;
	unsigned int offset;
	u32 shift, val;
	int ret;

	offset = bucket / AIROHA_PWM_BUCKET_PER_CYCLE_CFG;
	shift = bucket % AIROHA_PWM_BUCKET_PER_CYCLE_CFG;
	shift = AIROHA_PWM_REG_CYCLE_CFG_SHIFT(shift);

	ret = regmap_read(map, AIROHA_PWM_REG_CYCLE_CFG_VALUE(offset), &val);
	if (ret)
		return ret;

	period_tick = FIELD_GET(AIROHA_PWM_WAVE_GEN_CYCLE, val >> shift);
	*period_ns = airoha_pwm_get_period_ns_from_ticks(period_tick);

	offset = bucket / AIROHA_PWM_BUCKET_PER_FLASH_PROD;
	shift = bucket % AIROHA_PWM_BUCKET_PER_FLASH_PROD;
	shift = AIROHA_PWM_REG_GPIO_FLASH_PRD_SHIFT(shift);

	ret = regmap_read(map, AIROHA_PWM_REG_GPIO_FLASH_PRD_SET(offset),
			  &val);
	if (ret)
		return ret;

	duty_tick = FIELD_GET(AIROHA_PWM_GPIO_FLASH_PRD_HIGH, val >> shift);
	*duty_ns = airoha_pwm_get_duty_ns_from_ticks(period_tick, duty_tick);

	return 0;
}

static int airoha_pwm_get_generator(struct airoha_pwm *pc, u32 duty_ticks,
				    u32 period_ticks)
{
	int best = -ENOENT, unused = -ENOENT;
	u32 duty_ns, best_duty_ns = 0;
	u32 best_period_ticks = 0;
	unsigned int i;

	duty_ns = airoha_pwm_get_duty_ns_from_ticks(period_ticks, duty_ticks);

	for (i = 0; i < ARRAY_SIZE(pc->buckets); i++) {
		struct airoha_pwm_bucket *bucket = &pc->buckets[i];
		u32 bucket_period_ticks = bucket->period_ticks;
		u32 bucket_duty_ticks = bucket->duty_ticks;

		/* If found, save an unused bucket to return it later */
		if (!bucket->used) {
			unused = i;
			continue;
		}

		/* We found a matching bucket, exit early */
		if (duty_ticks == bucket_duty_ticks &&
		    period_ticks == bucket_period_ticks)
			return i;

		/*
		 * Unlike duty cycle zero, which can be handled by
		 * disabling PWM, a generator is needed for full duty
		 * cycle but it can be reused regardless of period
		 */
		if (duty_ticks == AIROHA_PWM_DUTY_FULL &&
		    bucket_duty_ticks == AIROHA_PWM_DUTY_FULL)
			return i;

		/*
		 * With an unused bucket available, skip searching for
		 * a bucket to recycle (closer to the requested period/duty)
		 */
		if (unused >= 0)
			continue;

		/* Ignore bucket with invalid period */
		if (bucket_period_ticks > period_ticks)
			continue;

		/*
		 * Search for a bucket closer to the requested period
		 * that has the maximal possible period that isn't bigger
		 * than the requested period. For that period pick the maximal
		 * duty cycle that isn't bigger than the requested duty_cycle.
		 */
		if (bucket_period_ticks >= best_period_ticks) {
			u32 bucket_duty_ns = airoha_pwm_get_duty_ns_from_ticks(bucket_period_ticks,
									       bucket_duty_ticks);

			/* Skip bucket that goes over the requested duty */
			if (bucket_duty_ns > duty_ns)
				continue;

			if (bucket_duty_ns > best_duty_ns) {
				best_period_ticks = bucket_period_ticks;
				best_duty_ns = bucket_duty_ns;
				best = i;
			}
		}
	}

	/* Return an unused bucket or the best one found (if ever) */
	return unused >= 0 ? unused : best;
}

static void airoha_pwm_release_bucket_config(struct airoha_pwm *pc,
					     unsigned int hwpwm)
{
	int bucket;

	/* Nothing to clear, PWM channel never used */
	if (!test_bit(hwpwm, pc->initialized))
		return;

	bucket = pc->channel_bucket[hwpwm];
	pc->buckets[bucket].used--;
}

static int airoha_pwm_apply_bucket_config(struct airoha_pwm *pc, unsigned int bucket,
					  u32 duty_ticks, u32 period_ticks)
{
	u32 mask, shift, val;
	u32 offset;
	int ret;

	offset = bucket / AIROHA_PWM_BUCKET_PER_CYCLE_CFG;
	shift = bucket % AIROHA_PWM_BUCKET_PER_CYCLE_CFG;
	shift = AIROHA_PWM_REG_CYCLE_CFG_SHIFT(shift);

	/* Configure frequency divisor */
	mask = AIROHA_PWM_WAVE_GEN_CYCLE << shift;
	val = FIELD_PREP(AIROHA_PWM_WAVE_GEN_CYCLE, period_ticks) << shift;
	ret = regmap_update_bits(pc->regmap, AIROHA_PWM_REG_CYCLE_CFG_VALUE(offset),
				 mask, val);
	if (ret)
		return ret;

	offset = bucket / AIROHA_PWM_BUCKET_PER_FLASH_PROD;
	shift = bucket % AIROHA_PWM_BUCKET_PER_FLASH_PROD;
	shift = AIROHA_PWM_REG_GPIO_FLASH_PRD_SHIFT(shift);

	/* Configure duty cycle */
	mask = AIROHA_PWM_GPIO_FLASH_PRD_HIGH << shift;
	val = FIELD_PREP(AIROHA_PWM_GPIO_FLASH_PRD_HIGH, duty_ticks) << shift;
	ret = regmap_update_bits(pc->regmap, AIROHA_PWM_REG_GPIO_FLASH_PRD_SET(offset),
				 mask, val);
	if (ret)
		return ret;

	mask = AIROHA_PWM_GPIO_FLASH_PRD_LOW << shift;
	val = FIELD_PREP(AIROHA_PWM_GPIO_FLASH_PRD_LOW,
			 AIROHA_PWM_DUTY_FULL - duty_ticks) << shift;
	return regmap_update_bits(pc->regmap, AIROHA_PWM_REG_GPIO_FLASH_PRD_SET(offset),
				  mask, val);
}

static int airoha_pwm_consume_generator(struct airoha_pwm *pc,
					u32 duty_ticks, u32 period_ticks,
					unsigned int hwpwm)
{
	bool config_bucket = false;
	int bucket, ret;

	/*
	 * Search for a bucket that already satisfies duty and period
	 * or an unused one.
	 * If not found, -ENOENT is returned.
	 */
	bucket = airoha_pwm_get_generator(pc, duty_ticks, period_ticks);
	if (bucket < 0)
		return bucket;

	/* Release previous used bucket (if any) */
	airoha_pwm_release_bucket_config(pc, hwpwm);

	if (!pc->buckets[bucket].used)
		config_bucket = true;
	pc->buckets[bucket].used++;

	if (config_bucket) {
		pc->buckets[bucket].period_ticks = period_ticks;
		pc->buckets[bucket].duty_ticks = duty_ticks;
		ret = airoha_pwm_apply_bucket_config(pc, bucket,
						     duty_ticks,
						     period_ticks);
		if (ret) {
			pc->buckets[bucket].used--;
			return ret;
		}
	}

	return bucket;
}

static int airoha_pwm_sipo_init(struct airoha_pwm *pc)
{
	u32 val;
	int ret;

	ret = regmap_clear_bits(pc->regmap, AIROHA_PWM_REG_SIPO_FLASH_MODE_CFG,
				AIROHA_PWM_SERIAL_GPIO_MODE_74HC164);
	if (ret)
		return ret;

	/* Configure shift register chip clock timings, use 32x divisor */
	ret = regmap_write(pc->regmap, AIROHA_PWM_REG_SGPIO_CLK_DIVR,
			   AIROHA_PWM_SGPIO_CLK_DIVR_32);
	if (ret)
		return ret;

	/*
	 * Configure the shift register chip clock delay. This needs
	 * to be configured based on the chip characteristics when the SoC
	 * apply the shift register configuration.
	 * This doesn't affect actual PWM operation and is only specific to
	 * the shift register chip.
	 *
	 * For 74HC164 we set it to 0.
	 *
	 * For reference, the actual delay applied is the internal clock
	 * feed to the SGPIO chip + 1.
	 *
	 * From documentation is specified that clock delay should not be
	 * greater than (AIROHA_PWM_REG_SGPIO_CLK_DIVR / 2) - 1.
	 */
	ret = regmap_write(pc->regmap, AIROHA_PWM_REG_SGPIO_CLK_DLY, 0);
	if (ret)
		return ret;

	/*
	 * It is necessary to explicitly shift out all zeros after muxing
	 * to initialize the shift register before enabling PWM
	 * mode because in PWM mode SIPO will not start shifting until
	 * it needs to output a non-zero value (bit 31 of led_data
	 * indicates shifting in progress and it must return to zero
	 * before led_data can be written or PWM mode can be set).
	 */
	ret = regmap_read_poll_timeout(pc->regmap, AIROHA_PWM_REG_SGPIO_LED_DATA, val,
				       !(val & AIROHA_PWM_SGPIO_LED_DATA_SHIFT_FLAG),
				       10, 200 * USEC_PER_MSEC);
	if (ret)
		return ret;

	ret = regmap_clear_bits(pc->regmap, AIROHA_PWM_REG_SGPIO_LED_DATA,
				AIROHA_PWM_SGPIO_LED_DATA_DATA);
	if (ret)
		return ret;
	ret = regmap_read_poll_timeout(pc->regmap, AIROHA_PWM_REG_SGPIO_LED_DATA, val,
				       !(val & AIROHA_PWM_SGPIO_LED_DATA_SHIFT_FLAG),
				       10, 200 * USEC_PER_MSEC);
	if (ret)
		return ret;

	/* Set SIPO in PWM mode */
	return regmap_set_bits(pc->regmap, AIROHA_PWM_REG_SIPO_FLASH_MODE_CFG,
			       AIROHA_PWM_SERIAL_GPIO_FLASH_MODE);
}

static int airoha_pwm_config_flash_map(struct airoha_pwm *pc,
				       unsigned int hwpwm, int index)
{
	unsigned int addr;
	u32 shift;
	int ret;

	airoha_pwm_get_flash_map_addr_and_shift(hwpwm, &addr, &shift);

	/* negative index means disable PWM channel */
	if (index < 0) {
		/*
		 * If we need to disable the PWM, we just put low the
		 * GPIO. No need to setup buckets.
		 */
		return regmap_clear_bits(pc->regmap, addr,
					 AIROHA_PWM_GPIO_FLASH_EN << shift);
	}

	ret = regmap_update_bits(pc->regmap, addr,
				 AIROHA_PWM_GPIO_FLASH_SET_ID << shift,
				 FIELD_PREP(AIROHA_PWM_GPIO_FLASH_SET_ID, index) << shift);
	if (ret)
		return ret;

	return regmap_set_bits(pc->regmap, addr, AIROHA_PWM_GPIO_FLASH_EN << shift);
}

static int airoha_pwm_config(struct airoha_pwm *pc, struct pwm_device *pwm,
			     u32 period_ticks, u32 duty_ticks)
{
	unsigned int hwpwm = pwm->hwpwm;
	int bucket, ret;

	bucket = airoha_pwm_consume_generator(pc, duty_ticks, period_ticks,
					      hwpwm);
	if (bucket < 0)
		return bucket;

	ret = airoha_pwm_config_flash_map(pc, hwpwm, bucket);
	if (ret) {
		pc->buckets[bucket].used--;
		return ret;
	}

	__set_bit(hwpwm, pc->initialized);
	pc->channel_bucket[hwpwm] = bucket;

	/*
	 * SIPO are special GPIO attached to a shift register chip. The handling
	 * of this chip is internal to the SoC that takes care of applying the
	 * values based on the flash map. To apply a new flash map, it's needed
	 * to trigger a refresh on the shift register chip.
	 * If a SIPO is getting configuring , always reinit the shift register
	 * chip to make sure the correct flash map is applied.
	 * Skip reconfiguring the shift register if the related hwpwm
	 * is disabled (as it doesn't need to be mapped).
	 */
	if (hwpwm >= AIROHA_PWM_NUM_GPIO) {
		ret = airoha_pwm_sipo_init(pc);
		if (ret) {
			airoha_pwm_release_bucket_config(pc, hwpwm);
			return ret;
		}
	}

	return 0;
}

static void airoha_pwm_disable(struct airoha_pwm *pc, struct pwm_device *pwm)
{
	/* Disable PWM and release the bucket */
	airoha_pwm_config_flash_map(pc, pwm->hwpwm, -1);
	airoha_pwm_release_bucket_config(pc, pwm->hwpwm);

	__clear_bit(pwm->hwpwm, pc->initialized);

	/* If no SIPO is used, disable the shift register chip */
	if (!bitmap_read(pc->initialized,
			 AIROHA_PWM_NUM_GPIO, AIROHA_PWM_NUM_SIPO))
		regmap_clear_bits(pc->regmap, AIROHA_PWM_REG_SIPO_FLASH_MODE_CFG,
				  AIROHA_PWM_SERIAL_GPIO_FLASH_MODE);
}

static int airoha_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			    const struct pwm_state *state)
{
	struct airoha_pwm *pc = pwmchip_get_drvdata(chip);
	u32 period_ticks, duty_ticks;
	u32 period_ns, duty_ns;

	if (!state->enabled) {
		airoha_pwm_disable(pc, pwm);
		return 0;
	}

	/* Only normal polarity is supported */
	if (state->polarity == PWM_POLARITY_INVERSED)
		return -EINVAL;

	/* Exit early if period is less than minimum supported */
	if (state->period < AIROHA_PWM_PERIOD_TICK_NS)
		return -EINVAL;

	/* Clamp period to MAX supported value */
	if (state->period > AIROHA_PWM_PERIOD_MAX_NS)
		period_ns = AIROHA_PWM_PERIOD_MAX_NS;
	else
		period_ns = state->period;

	/* Validate duty to configured period */
	if (state->duty_cycle > period_ns)
		duty_ns = period_ns;
	else
		duty_ns = state->duty_cycle;

	/* Convert period ns to ticks */
	period_ticks = airoha_pwm_get_period_ticks_from_ns(period_ns);
	/* Convert period ticks to ns again for cosistent duty tick calculation */
	period_ns = airoha_pwm_get_period_ns_from_ticks(period_ticks);
	duty_ticks = airoha_pwm_get_duty_ticks_from_ns(period_ns, duty_ns);

	return airoha_pwm_config(pc, pwm, period_ticks, duty_ticks);
}

static int airoha_pwm_get_state(struct pwm_chip *chip, struct pwm_device *pwm,
				struct pwm_state *state)
{
	struct airoha_pwm *pc = pwmchip_get_drvdata(chip);
	int ret, hwpwm = pwm->hwpwm;
	u32 addr, shift, val;
	u8 bucket;

	airoha_pwm_get_flash_map_addr_and_shift(hwpwm, &addr, &shift);

	ret = regmap_read(pc->regmap, addr, &val);
	if (ret)
		return ret;

	state->enabled = FIELD_GET(AIROHA_PWM_GPIO_FLASH_EN, val >> shift);
	if (!state->enabled)
		return 0;

	state->polarity = PWM_POLARITY_NORMAL;

	bucket = FIELD_GET(AIROHA_PWM_GPIO_FLASH_SET_ID, val >> shift);
	return airoha_pwm_get_bucket(pc, bucket, &state->period,
				     &state->duty_cycle);
}

static const struct pwm_ops airoha_pwm_ops = {
	.apply = airoha_pwm_apply,
	.get_state = airoha_pwm_get_state,
};

static int airoha_pwm_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct airoha_pwm *pc;
	struct pwm_chip *chip;
	int ret;

	chip = devm_pwmchip_alloc(dev, AIROHA_PWM_MAX_CHANNELS, sizeof(*pc));
	if (IS_ERR(chip))
		return PTR_ERR(chip);

	chip->ops = &airoha_pwm_ops;
	pc = pwmchip_get_drvdata(chip);

	pc->regmap = device_node_to_regmap(dev_of_node(dev->parent));
	if (IS_ERR(pc->regmap))
		return dev_err_probe(dev, PTR_ERR(pc->regmap), "Failed to get PWM regmap\n");

	ret = devm_pwmchip_add(dev, chip);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to add PWM chip\n");

	return 0;
}

static const struct of_device_id airoha_pwm_of_match[] = {
	{ .compatible = "airoha,en7581-pwm" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, airoha_pwm_of_match);

static struct platform_driver airoha_pwm_driver = {
	.driver = {
		.name = "pwm-airoha",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table = airoha_pwm_of_match,
	},
	.probe = airoha_pwm_probe,
};
module_platform_driver(airoha_pwm_driver);

MODULE_AUTHOR("Lorenzo Bianconi <lorenzo@kernel.org>");
MODULE_AUTHOR("Markus Gothe <markus.gothe@genexis.eu>");
MODULE_AUTHOR("Benjamin Larsson <benjamin.larsson@genexis.eu>");
MODULE_AUTHOR("Christian Marangi <ansuelsmth@gmail.com>");
MODULE_DESCRIPTION("Airoha EN7581 PWM driver");
MODULE_LICENSE("GPL");
