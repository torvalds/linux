// SPDX-License-Identifier: GPL-2.0-only
/*
 * LP5521/LP5523/LP55231/LP5562 Common Driver
 *
 * Copyright 2012 Texas Instruments
 *
 * Author: Milo(Woogyom) Kim <milo.kim@ti.com>
 *
 * Derived from leds-lp5521.c, leds-lp5523.c
 */

#include <linux/bitfield.h>
#include <linux/cleanup.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/i2c.h>
#include <linux/iopoll.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/platform_data/leds-lp55xx.h>
#include <linux/slab.h>
#include <linux/gpio/consumer.h>
#include <dt-bindings/leds/leds-lp55xx.h>

#include "leds-lp55xx-common.h"

/* OP MODE require at least 153 us to clear regs */
#define LP55XX_CMD_SLEEP		200

#define LP55xx_PROGRAM_PAGES		16
#define LP55xx_MAX_PROGRAM_LENGTH	(LP55xx_BYTES_PER_PAGE * 4) /* 128 bytes (4 pages) */

/*
 * Program Memory Operations
 * Same Mask for each engine for both mode and exec
 * ENG1        GENMASK(3, 2)
 * ENG2        GENMASK(5, 4)
 * ENG3        GENMASK(7, 6)
 */
#define LP55xx_MODE_DISABLE_ALL_ENG	0x0
#define LP55xx_MODE_ENG_MASK           GENMASK(1, 0)
#define   LP55xx_MODE_DISABLE_ENG      FIELD_PREP_CONST(LP55xx_MODE_ENG_MASK, 0x0)
#define   LP55xx_MODE_LOAD_ENG         FIELD_PREP_CONST(LP55xx_MODE_ENG_MASK, 0x1)
#define   LP55xx_MODE_RUN_ENG          FIELD_PREP_CONST(LP55xx_MODE_ENG_MASK, 0x2)
#define   LP55xx_MODE_HALT_ENG         FIELD_PREP_CONST(LP55xx_MODE_ENG_MASK, 0x3)

#define   LP55xx_MODE_ENGn_SHIFT(n, shift)	((shift) + (2 * (3 - (n))))
#define   LP55xx_MODE_ENGn_MASK(n, shift)     (LP55xx_MODE_ENG_MASK << LP55xx_MODE_ENGn_SHIFT(n, shift))
#define   LP55xx_MODE_ENGn_GET(n, mode, shift)        \
	(((mode) >> LP55xx_MODE_ENGn_SHIFT(n, shift)) & LP55xx_MODE_ENG_MASK)

#define   LP55xx_EXEC_ENG_MASK         GENMASK(1, 0)
#define   LP55xx_EXEC_HOLD_ENG         FIELD_PREP_CONST(LP55xx_EXEC_ENG_MASK, 0x0)
#define   LP55xx_EXEC_STEP_ENG         FIELD_PREP_CONST(LP55xx_EXEC_ENG_MASK, 0x1)
#define   LP55xx_EXEC_RUN_ENG          FIELD_PREP_CONST(LP55xx_EXEC_ENG_MASK, 0x2)
#define   LP55xx_EXEC_ONCE_ENG         FIELD_PREP_CONST(LP55xx_EXEC_ENG_MASK, 0x3)

#define   LP55xx_EXEC_ENGn_SHIFT(n, shift)    ((shift) + (2 * (3 - (n))))
#define   LP55xx_EXEC_ENGn_MASK(n, shift)     (LP55xx_EXEC_ENG_MASK << LP55xx_EXEC_ENGn_SHIFT(n, shift))

/* Memory Page Selection */
#define LP55xx_REG_PROG_PAGE_SEL	0x4f
/* If supported, each ENGINE have an equal amount of pages offset from page 0 */
#define LP55xx_PAGE_OFFSET(n, pages)	(((n) - 1) * (pages))

#define LED_ACTIVE(mux, led)		(!!((mux) & (0x0001 << (led))))

/* MASTER FADER common property */
#define LP55xx_FADER_MAPPING_MASK	GENMASK(7, 6)

/* External clock rate */
#define LP55XX_CLK_32K			32768

static struct lp55xx_led *cdev_to_lp55xx_led(struct led_classdev *cdev)
{
	return container_of(cdev, struct lp55xx_led, cdev);
}

static struct lp55xx_led *dev_to_lp55xx_led(struct device *dev)
{
	return cdev_to_lp55xx_led(dev_get_drvdata(dev));
}

static struct lp55xx_led *mcled_cdev_to_led(struct led_classdev_mc *mc_cdev)
{
	return container_of(mc_cdev, struct lp55xx_led, mc_cdev);
}

static void lp55xx_wait_opmode_done(struct lp55xx_chip *chip)
{
	const struct lp55xx_device_config *cfg = chip->cfg;
	int __always_unused ret;
	u8 val;

	/*
	 * Recent chip supports BUSY bit for engine.
	 * Check support by checking if val is not 0.
	 * For legacy device, sleep at least 153 us.
	 */
	if (cfg->engine_busy.val) {
		read_poll_timeout(lp55xx_read, ret, !(val & cfg->engine_busy.mask),
				  LP55XX_CMD_SLEEP, LP55XX_CMD_SLEEP * 10, false,
				  chip, cfg->engine_busy.addr, &val);
	} else {
		usleep_range(LP55XX_CMD_SLEEP, LP55XX_CMD_SLEEP * 2);
	}
}

void lp55xx_stop_all_engine(struct lp55xx_chip *chip)
{
	const struct lp55xx_device_config *cfg = chip->cfg;

	lp55xx_write(chip, cfg->reg_op_mode.addr, LP55xx_MODE_DISABLE_ALL_ENG);
	lp55xx_wait_opmode_done(chip);
}
EXPORT_SYMBOL_GPL(lp55xx_stop_all_engine);

void lp55xx_load_engine(struct lp55xx_chip *chip)
{
	enum lp55xx_engine_index idx = chip->engine_idx;
	const struct lp55xx_device_config *cfg = chip->cfg;
	u8 mask, val;

	mask = LP55xx_MODE_ENGn_MASK(idx, cfg->reg_op_mode.shift);
	val = LP55xx_MODE_LOAD_ENG << LP55xx_MODE_ENGn_SHIFT(idx, cfg->reg_op_mode.shift);

	lp55xx_update_bits(chip, cfg->reg_op_mode.addr, mask, val);
	lp55xx_wait_opmode_done(chip);

	/* Setup PAGE if supported (pages_per_engine not 0)*/
	if (cfg->pages_per_engine)
		lp55xx_write(chip, LP55xx_REG_PROG_PAGE_SEL,
			     LP55xx_PAGE_OFFSET(idx, cfg->pages_per_engine));
}
EXPORT_SYMBOL_GPL(lp55xx_load_engine);

int lp55xx_run_engine_common(struct lp55xx_chip *chip)
{
	const struct lp55xx_device_config *cfg = chip->cfg;
	u8 mode, exec;
	int i, ret;

	/* To run the engine, both OP MODE and EXEC needs to be put in RUN mode */
	ret = lp55xx_read(chip, cfg->reg_op_mode.addr, &mode);
	if (ret)
		return ret;

	ret = lp55xx_read(chip, cfg->reg_exec.addr, &exec);
	if (ret)
		return ret;

	/* Switch to RUN only for engine that were put in LOAD previously */
	for (i = LP55XX_ENGINE_1; i <= LP55XX_ENGINE_3; i++) {
		if (LP55xx_MODE_ENGn_GET(i, mode, cfg->reg_op_mode.shift) != LP55xx_MODE_LOAD_ENG)
			continue;

		mode &= ~LP55xx_MODE_ENGn_MASK(i, cfg->reg_op_mode.shift);
		mode |= LP55xx_MODE_RUN_ENG << LP55xx_MODE_ENGn_SHIFT(i, cfg->reg_op_mode.shift);
		exec &= ~LP55xx_EXEC_ENGn_MASK(i, cfg->reg_exec.shift);
		exec |= LP55xx_EXEC_RUN_ENG << LP55xx_EXEC_ENGn_SHIFT(i, cfg->reg_exec.shift);
	}

	lp55xx_write(chip, cfg->reg_op_mode.addr, mode);
	lp55xx_wait_opmode_done(chip);
	lp55xx_write(chip, cfg->reg_exec.addr, exec);

	return 0;
}
EXPORT_SYMBOL_GPL(lp55xx_run_engine_common);

int lp55xx_update_program_memory(struct lp55xx_chip *chip,
				 const u8 *data, size_t size)
{
	enum lp55xx_engine_index idx = chip->engine_idx;
	const struct lp55xx_device_config *cfg = chip->cfg;
	u8 pattern[LP55xx_MAX_PROGRAM_LENGTH] = { };
	u8 start_addr = cfg->prog_mem_base.addr;
	int page, i = 0, offset = 0;
	int program_length, ret;

	program_length = LP55xx_BYTES_PER_PAGE;
	if (cfg->pages_per_engine)
		program_length *= cfg->pages_per_engine;

	while ((offset < size - 1) && (i < program_length)) {
		unsigned int cmd;
		int nrchars;
		char c[3];

		/* separate sscanfs because length is working only for %s */
		ret = sscanf(data + offset, "%2s%n ", c, &nrchars);
		if (ret != 1)
			goto err;

		ret = sscanf(c, "%2x", &cmd);
		if (ret != 1)
			goto err;

		pattern[i] = (u8)cmd;
		offset += nrchars;
		i++;
	}

	/* Each instruction is 16bit long. Check that length is even */
	if (i % 2)
		goto err;

	/*
	 * For legacy LED chip with no page support, engine base address are
	 * one after another at offset of 32.
	 * For LED chip that support page, PAGE is already set in load_engine.
	 */
	if (!cfg->pages_per_engine)
		start_addr += LP55xx_BYTES_PER_PAGE * idx;

	for (page = 0; page < program_length / LP55xx_BYTES_PER_PAGE; page++) {
		/* Write to the next page each 32 bytes (if supported) */
		if (cfg->pages_per_engine)
			lp55xx_write(chip, LP55xx_REG_PROG_PAGE_SEL,
				     LP55xx_PAGE_OFFSET(idx, cfg->pages_per_engine) + page);

		for (i = 0; i < LP55xx_BYTES_PER_PAGE; i++) {
			ret = lp55xx_write(chip, start_addr + i,
					   pattern[i + (page * LP55xx_BYTES_PER_PAGE)]);
			if (ret)
				return -EINVAL;
		}
	}

	return size;

err:
	dev_err(&chip->cl->dev, "wrong pattern format\n");
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(lp55xx_update_program_memory);

void lp55xx_firmware_loaded_cb(struct lp55xx_chip *chip)
{
	const struct lp55xx_device_config *cfg = chip->cfg;
	const struct firmware *fw = chip->fw;
	int program_length;

	program_length = LP55xx_BYTES_PER_PAGE;
	if (cfg->pages_per_engine)
		program_length *= cfg->pages_per_engine;

	/*
	 * the firmware is encoded in ascii hex character, with 2 chars
	 * per byte
	 */
	if (fw->size > program_length * 2) {
		dev_err(&chip->cl->dev, "firmware data size overflow: %zu\n",
			fw->size);
		return;
	}

	/*
	 * Program memory sequence
	 *  1) set engine mode to "LOAD"
	 *  2) write firmware data into program memory
	 */

	lp55xx_load_engine(chip);
	lp55xx_update_program_memory(chip, fw->data, fw->size);
}
EXPORT_SYMBOL_GPL(lp55xx_firmware_loaded_cb);

int lp55xx_led_brightness(struct lp55xx_led *led)
{
	struct lp55xx_chip *chip = led->chip;
	const struct lp55xx_device_config *cfg = chip->cfg;
	int ret;

	guard(mutex)(&chip->lock);

	ret = lp55xx_write(chip, cfg->reg_led_pwm_base.addr + led->chan_nr,
			   led->brightness);
	return ret;
}
EXPORT_SYMBOL_GPL(lp55xx_led_brightness);

int lp55xx_multicolor_brightness(struct lp55xx_led *led)
{
	struct lp55xx_chip *chip = led->chip;
	const struct lp55xx_device_config *cfg = chip->cfg;
	int ret;
	int i;

	guard(mutex)(&chip->lock);

	for (i = 0; i < led->mc_cdev.num_colors; i++) {
		ret = lp55xx_write(chip,
				   cfg->reg_led_pwm_base.addr +
				   led->mc_cdev.subled_info[i].channel,
				   led->mc_cdev.subled_info[i].brightness);
		if (ret)
			break;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(lp55xx_multicolor_brightness);

void lp55xx_set_led_current(struct lp55xx_led *led, u8 led_current)
{
	struct lp55xx_chip *chip = led->chip;
	const struct lp55xx_device_config *cfg = chip->cfg;

	led->led_current = led_current;
	lp55xx_write(led->chip, cfg->reg_led_current_base.addr + led->chan_nr,
		     led_current);
}
EXPORT_SYMBOL_GPL(lp55xx_set_led_current);

void lp55xx_turn_off_channels(struct lp55xx_chip *chip)
{
	const struct lp55xx_device_config *cfg = chip->cfg;
	int i;

	for (i = 0; i < cfg->max_channel; i++)
		lp55xx_write(chip, cfg->reg_led_pwm_base.addr + i, 0);
}
EXPORT_SYMBOL_GPL(lp55xx_turn_off_channels);

void lp55xx_stop_engine(struct lp55xx_chip *chip)
{
	enum lp55xx_engine_index idx = chip->engine_idx;
	const struct lp55xx_device_config *cfg = chip->cfg;
	u8 mask;

	mask = LP55xx_MODE_ENGn_MASK(idx, cfg->reg_op_mode.shift);
	lp55xx_update_bits(chip, cfg->reg_op_mode.addr, mask, 0);

	lp55xx_wait_opmode_done(chip);
}
EXPORT_SYMBOL_GPL(lp55xx_stop_engine);

static void lp55xx_reset_device(struct lp55xx_chip *chip)
{
	const struct lp55xx_device_config *cfg = chip->cfg;
	u8 addr = cfg->reset.addr;
	u8 val  = cfg->reset.val;

	/* no error checking here because no ACK from the device after reset */
	lp55xx_write(chip, addr, val);
}

static int lp55xx_detect_device(struct lp55xx_chip *chip)
{
	const struct lp55xx_device_config *cfg = chip->cfg;
	u8 addr = cfg->enable.addr;
	u8 val  = cfg->enable.val;
	int ret;

	ret = lp55xx_write(chip, addr, val);
	if (ret)
		return ret;

	usleep_range(1000, 2000);

	ret = lp55xx_read(chip, addr, &val);
	if (ret)
		return ret;

	if (val != cfg->enable.val)
		return -ENODEV;

	return 0;
}

static int lp55xx_post_init_device(struct lp55xx_chip *chip)
{
	const struct lp55xx_device_config *cfg = chip->cfg;

	if (!cfg->post_init_device)
		return 0;

	return cfg->post_init_device(chip);
}

static ssize_t led_current_show(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct lp55xx_led *led = dev_to_lp55xx_led(dev);

	return sysfs_emit(buf, "%d\n", led->led_current);
}

static ssize_t led_current_store(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t len)
{
	struct lp55xx_led *led = dev_to_lp55xx_led(dev);
	struct lp55xx_chip *chip = led->chip;
	unsigned long curr;

	if (kstrtoul(buf, 0, &curr))
		return -EINVAL;

	if (curr > led->max_current)
		return -EINVAL;

	if (!chip->cfg->set_led_current)
		return len;

	guard(mutex)(&chip->lock);

	chip->cfg->set_led_current(led, (u8)curr);

	return len;
}

static ssize_t max_current_show(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct lp55xx_led *led = dev_to_lp55xx_led(dev);

	return sysfs_emit(buf, "%d\n", led->max_current);
}

static DEVICE_ATTR_RW(led_current);
static DEVICE_ATTR_RO(max_current);

static struct attribute *lp55xx_led_attrs[] = {
	&dev_attr_led_current.attr,
	&dev_attr_max_current.attr,
	NULL,
};
ATTRIBUTE_GROUPS(lp55xx_led);

static int lp55xx_set_mc_brightness(struct led_classdev *cdev,
				    enum led_brightness brightness)
{
	struct led_classdev_mc *mc_dev = lcdev_to_mccdev(cdev);
	struct lp55xx_led *led = mcled_cdev_to_led(mc_dev);
	const struct lp55xx_device_config *cfg = led->chip->cfg;

	led_mc_calc_color_components(&led->mc_cdev, brightness);
	return cfg->multicolor_brightness_fn(led);

}

static int lp55xx_set_brightness(struct led_classdev *cdev,
			     enum led_brightness brightness)
{
	struct lp55xx_led *led = cdev_to_lp55xx_led(cdev);
	const struct lp55xx_device_config *cfg = led->chip->cfg;

	led->brightness = (u8)brightness;
	return cfg->brightness_fn(led);
}

static int lp55xx_init_led(struct lp55xx_led *led,
			struct lp55xx_chip *chip, int chan)
{
	struct lp55xx_platform_data *pdata = chip->pdata;
	const struct lp55xx_device_config *cfg = chip->cfg;
	struct device *dev = &chip->cl->dev;
	int max_channel = cfg->max_channel;
	struct mc_subled *mc_led_info;
	struct led_classdev *led_cdev;
	char name[32];
	int i;
	int ret;

	if (chan >= max_channel) {
		dev_err(dev, "invalid channel: %d / %d\n", chan, max_channel);
		return -EINVAL;
	}

	if (pdata->led_config[chan].led_current == 0)
		return 0;

	if (pdata->led_config[chan].name) {
		led->cdev.name = pdata->led_config[chan].name;
	} else {
		snprintf(name, sizeof(name), "%s:channel%d",
			pdata->label ? : chip->cl->name, chan);
		led->cdev.name = name;
	}

	if (pdata->led_config[chan].num_colors > 1) {
		mc_led_info = devm_kcalloc(dev,
					   pdata->led_config[chan].num_colors,
					   sizeof(*mc_led_info), GFP_KERNEL);
		if (!mc_led_info)
			return -ENOMEM;

		led_cdev = &led->mc_cdev.led_cdev;
		led_cdev->name = led->cdev.name;
		led_cdev->brightness_set_blocking = lp55xx_set_mc_brightness;
		led->mc_cdev.num_colors = pdata->led_config[chan].num_colors;
		for (i = 0; i < led->mc_cdev.num_colors; i++) {
			mc_led_info[i].color_index =
				pdata->led_config[chan].color_id[i];
			mc_led_info[i].channel =
					pdata->led_config[chan].output_num[i];
		}

		led->mc_cdev.subled_info = mc_led_info;
	} else {
		led->cdev.brightness_set_blocking = lp55xx_set_brightness;
	}

	led->cdev.groups = lp55xx_led_groups;
	led->cdev.default_trigger = pdata->led_config[chan].default_trigger;
	led->led_current = pdata->led_config[chan].led_current;
	led->max_current = pdata->led_config[chan].max_current;
	led->chan_nr = pdata->led_config[chan].chan_nr;

	if (led->chan_nr >= max_channel) {
		dev_err(dev, "Use channel numbers between 0 and %d\n",
			max_channel - 1);
		return -EINVAL;
	}

	if (pdata->led_config[chan].num_colors > 1)
		ret = devm_led_classdev_multicolor_register(dev, &led->mc_cdev);
	else
		ret = devm_led_classdev_register(dev, &led->cdev);

	if (ret) {
		dev_err(dev, "led register err: %d\n", ret);
		return ret;
	}

	return 0;
}

static void lp55xx_firmware_loaded(const struct firmware *fw, void *context)
{
	struct lp55xx_chip *chip = context;
	struct device *dev = &chip->cl->dev;
	enum lp55xx_engine_index idx = chip->engine_idx;

	if (!fw) {
		dev_err(dev, "firmware request failed\n");
		return;
	}

	/* handling firmware data is chip dependent */
	scoped_guard(mutex, &chip->lock) {
		chip->engines[idx - 1].mode = LP55XX_ENGINE_LOAD;
		chip->fw = fw;
		if (chip->cfg->firmware_cb)
			chip->cfg->firmware_cb(chip);
	}

	/* firmware should be released for other channel use */
	release_firmware(chip->fw);
	chip->fw = NULL;
}

static int lp55xx_request_firmware(struct lp55xx_chip *chip)
{
	const char *name = chip->cl->name;
	struct device *dev = &chip->cl->dev;

	return request_firmware_nowait(THIS_MODULE, false, name, dev,
				GFP_KERNEL, chip, lp55xx_firmware_loaded);
}

static ssize_t select_engine_show(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	struct lp55xx_led *led = i2c_get_clientdata(to_i2c_client(dev));
	struct lp55xx_chip *chip = led->chip;

	return sprintf(buf, "%d\n", chip->engine_idx);
}

static ssize_t select_engine_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t len)
{
	struct lp55xx_led *led = i2c_get_clientdata(to_i2c_client(dev));
	struct lp55xx_chip *chip = led->chip;
	unsigned long val;
	int ret;

	if (kstrtoul(buf, 0, &val))
		return -EINVAL;

	/* select the engine to be run */

	switch (val) {
	case LP55XX_ENGINE_1:
	case LP55XX_ENGINE_2:
	case LP55XX_ENGINE_3:
		scoped_guard(mutex, &chip->lock) {
			chip->engine_idx = val;
			ret = lp55xx_request_firmware(chip);
		}
		break;
	default:
		dev_err(dev, "%lu: invalid engine index. (1, 2, 3)\n", val);
		return -EINVAL;
	}

	if (ret) {
		dev_err(dev, "request firmware err: %d\n", ret);
		return ret;
	}

	return len;
}

static inline void lp55xx_run_engine(struct lp55xx_chip *chip, bool start)
{
	if (chip->cfg->run_engine)
		chip->cfg->run_engine(chip, start);
}

static ssize_t run_engine_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t len)
{
	struct lp55xx_led *led = i2c_get_clientdata(to_i2c_client(dev));
	struct lp55xx_chip *chip = led->chip;
	unsigned long val;

	if (kstrtoul(buf, 0, &val))
		return -EINVAL;

	/* run or stop the selected engine */

	if (val <= 0) {
		lp55xx_run_engine(chip, false);
		return len;
	}

	guard(mutex)(&chip->lock);

	lp55xx_run_engine(chip, true);

	return len;
}

static DEVICE_ATTR_RW(select_engine);
static DEVICE_ATTR_WO(run_engine);

ssize_t lp55xx_show_engine_mode(struct device *dev,
				struct device_attribute *attr,
				char *buf, int nr)
{
	struct lp55xx_led *led = i2c_get_clientdata(to_i2c_client(dev));
	struct lp55xx_chip *chip = led->chip;
	enum lp55xx_engine_mode mode = chip->engines[nr - 1].mode;

	switch (mode) {
	case LP55XX_ENGINE_RUN:
		return sysfs_emit(buf, "run\n");
	case LP55XX_ENGINE_LOAD:
		return sysfs_emit(buf, "load\n");
	case LP55XX_ENGINE_DISABLED:
	default:
		return sysfs_emit(buf, "disabled\n");
	}
}
EXPORT_SYMBOL_GPL(lp55xx_show_engine_mode);

ssize_t lp55xx_store_engine_mode(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t len, int nr)
{
	struct lp55xx_led *led = i2c_get_clientdata(to_i2c_client(dev));
	struct lp55xx_chip *chip = led->chip;
	const struct lp55xx_device_config *cfg = chip->cfg;
	struct lp55xx_engine *engine = &chip->engines[nr - 1];

	guard(mutex)(&chip->lock);

	chip->engine_idx = nr;

	if (!strncmp(buf, "run", 3)) {
		cfg->run_engine(chip, true);
		engine->mode = LP55XX_ENGINE_RUN;
	} else if (!strncmp(buf, "load", 4)) {
		lp55xx_stop_engine(chip);
		lp55xx_load_engine(chip);
		engine->mode = LP55XX_ENGINE_LOAD;
	} else if (!strncmp(buf, "disabled", 8)) {
		lp55xx_stop_engine(chip);
		engine->mode = LP55XX_ENGINE_DISABLED;
	}

	return len;
}
EXPORT_SYMBOL_GPL(lp55xx_store_engine_mode);

ssize_t lp55xx_store_engine_load(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t len, int nr)
{
	struct lp55xx_led *led = i2c_get_clientdata(to_i2c_client(dev));
	struct lp55xx_chip *chip = led->chip;
	int ret;

	guard(mutex)(&chip->lock);

	chip->engine_idx = nr;
	lp55xx_load_engine(chip);
	ret = lp55xx_update_program_memory(chip, buf, len);

	return ret;
}
EXPORT_SYMBOL_GPL(lp55xx_store_engine_load);

static int lp55xx_mux_parse(struct lp55xx_chip *chip, const char *buf,
			    u16 *mux, size_t len)
{
	const struct lp55xx_device_config *cfg = chip->cfg;
	u16 tmp_mux = 0;
	int i;

	len = min_t(int, len, cfg->max_channel);

	for (i = 0; i < len; i++) {
		switch (buf[i]) {
		case '1':
			tmp_mux |= (1 << i);
			break;
		case '0':
			break;
		case '\n':
			i = len;
			break;
		default:
			return -1;
		}
	}
	*mux = tmp_mux;

	return 0;
}

ssize_t lp55xx_show_engine_leds(struct device *dev,
				struct device_attribute *attr,
				char *buf, int nr)
{
	struct lp55xx_led *led = i2c_get_clientdata(to_i2c_client(dev));
	struct lp55xx_chip *chip = led->chip;
	const struct lp55xx_device_config *cfg = chip->cfg;
	unsigned int led_active;
	int i, pos = 0;

	for (i = 0; i < cfg->max_channel; i++) {
		led_active = LED_ACTIVE(chip->engines[nr - 1].led_mux, i);
		pos += sysfs_emit_at(buf, pos, "%x", led_active);
	}

	pos += sysfs_emit_at(buf, pos, "\n");

	return pos;
}
EXPORT_SYMBOL_GPL(lp55xx_show_engine_leds);

static int lp55xx_load_mux(struct lp55xx_chip *chip, u16 mux, int nr)
{
	struct lp55xx_engine *engine = &chip->engines[nr - 1];
	const struct lp55xx_device_config *cfg = chip->cfg;
	u8 mux_page;
	int ret;

	lp55xx_load_engine(chip);

	/* Derive the MUX page offset by starting at the end of the ENGINE pages */
	mux_page = cfg->pages_per_engine * LP55XX_ENGINE_MAX + (nr - 1);
	ret = lp55xx_write(chip, LP55xx_REG_PROG_PAGE_SEL, mux_page);
	if (ret)
		return ret;

	ret = lp55xx_write(chip, cfg->prog_mem_base.addr, (u8)(mux >> 8));
	if (ret)
		return ret;

	ret = lp55xx_write(chip, cfg->prog_mem_base.addr + 1, (u8)(mux));
	if (ret)
		return ret;

	engine->led_mux = mux;
	return 0;
}

ssize_t lp55xx_store_engine_leds(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t len, int nr)
{
	struct lp55xx_led *led = i2c_get_clientdata(to_i2c_client(dev));
	struct lp55xx_chip *chip = led->chip;
	struct lp55xx_engine *engine = &chip->engines[nr - 1];
	u16 mux = 0;

	if (lp55xx_mux_parse(chip, buf, &mux, len))
		return -EINVAL;

	guard(mutex)(&chip->lock);

	chip->engine_idx = nr;

	if (engine->mode != LP55XX_ENGINE_LOAD)
		return -EINVAL;

	if (lp55xx_load_mux(chip, mux, nr))
		return -EINVAL;

	return len;
}
EXPORT_SYMBOL_GPL(lp55xx_store_engine_leds);

ssize_t lp55xx_show_master_fader(struct device *dev,
				 struct device_attribute *attr,
				 char *buf, int nr)
{
	struct lp55xx_led *led = i2c_get_clientdata(to_i2c_client(dev));
	struct lp55xx_chip *chip = led->chip;
	const struct lp55xx_device_config *cfg = chip->cfg;
	int ret;
	u8 val;

	guard(mutex)(&chip->lock);

	ret = lp55xx_read(chip, cfg->reg_master_fader_base.addr + nr - 1, &val);

	return ret ? ret : sysfs_emit(buf, "%u\n", val);
}
EXPORT_SYMBOL_GPL(lp55xx_show_master_fader);

ssize_t lp55xx_store_master_fader(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t len, int nr)
{
	struct lp55xx_led *led = i2c_get_clientdata(to_i2c_client(dev));
	struct lp55xx_chip *chip = led->chip;
	const struct lp55xx_device_config *cfg = chip->cfg;
	int ret;
	unsigned long val;

	if (kstrtoul(buf, 0, &val))
		return -EINVAL;

	if (val > 0xff)
		return -EINVAL;

	guard(mutex)(&chip->lock);

	ret = lp55xx_write(chip, cfg->reg_master_fader_base.addr + nr - 1,
			   (u8)val);

	return ret ? ret : len;
}
EXPORT_SYMBOL_GPL(lp55xx_store_master_fader);

ssize_t lp55xx_show_master_fader_leds(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	struct lp55xx_led *led = i2c_get_clientdata(to_i2c_client(dev));
	struct lp55xx_chip *chip = led->chip;
	const struct lp55xx_device_config *cfg = chip->cfg;
	int i, ret, pos = 0;
	u8 val;

	guard(mutex)(&chip->lock);

	for (i = 0; i < cfg->max_channel; i++) {
		ret = lp55xx_read(chip, cfg->reg_led_ctrl_base.addr + i, &val);
		if (ret)
			return ret;

		val = FIELD_GET(LP55xx_FADER_MAPPING_MASK, val);
		if (val > FIELD_MAX(LP55xx_FADER_MAPPING_MASK)) {
			return -EINVAL;
		}
		buf[pos++] = val + '0';
	}
	buf[pos++] = '\n';

	return pos;
}
EXPORT_SYMBOL_GPL(lp55xx_show_master_fader_leds);

ssize_t lp55xx_store_master_fader_leds(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t len)
{
	struct lp55xx_led *led = i2c_get_clientdata(to_i2c_client(dev));
	struct lp55xx_chip *chip = led->chip;
	const struct lp55xx_device_config *cfg = chip->cfg;
	int i, n, ret;
	u8 val;

	n = min_t(int, len, cfg->max_channel);

	guard(mutex)(&chip->lock);

	for (i = 0; i < n; i++) {
		if (buf[i] >= '0' && buf[i] <= '3') {
			val = (buf[i] - '0') << __bf_shf(LP55xx_FADER_MAPPING_MASK);
			ret = lp55xx_update_bits(chip,
						 cfg->reg_led_ctrl_base.addr + i,
						 LP55xx_FADER_MAPPING_MASK,
						 val);
			if (ret)
				return ret;
		} else {
			return -EINVAL;
		}
	}

	return len;
}
EXPORT_SYMBOL_GPL(lp55xx_store_master_fader_leds);

static struct attribute *lp55xx_engine_attributes[] = {
	&dev_attr_select_engine.attr,
	&dev_attr_run_engine.attr,
	NULL,
};

static const struct attribute_group lp55xx_engine_attr_group = {
	.attrs = lp55xx_engine_attributes,
};

int lp55xx_write(struct lp55xx_chip *chip, u8 reg, u8 val)
{
	return i2c_smbus_write_byte_data(chip->cl, reg, val);
}
EXPORT_SYMBOL_GPL(lp55xx_write);

int lp55xx_read(struct lp55xx_chip *chip, u8 reg, u8 *val)
{
	s32 ret;

	ret = i2c_smbus_read_byte_data(chip->cl, reg);
	if (ret < 0)
		return ret;

	*val = ret;
	return 0;
}
EXPORT_SYMBOL_GPL(lp55xx_read);

int lp55xx_update_bits(struct lp55xx_chip *chip, u8 reg, u8 mask, u8 val)
{
	int ret;
	u8 tmp;

	ret = lp55xx_read(chip, reg, &tmp);
	if (ret)
		return ret;

	tmp &= ~mask;
	tmp |= val & mask;

	return lp55xx_write(chip, reg, tmp);
}
EXPORT_SYMBOL_GPL(lp55xx_update_bits);

bool lp55xx_is_extclk_used(struct lp55xx_chip *chip)
{
	struct clk *clk;

	clk = devm_clk_get_enabled(&chip->cl->dev, "32k_clk");
	if (IS_ERR(clk))
		goto use_internal_clk;

	if (clk_get_rate(clk) != LP55XX_CLK_32K)
		goto use_internal_clk;

	dev_info(&chip->cl->dev, "%dHz external clock used\n",	LP55XX_CLK_32K);

	return true;

use_internal_clk:
	dev_info(&chip->cl->dev, "internal clock used\n");
	return false;
}
EXPORT_SYMBOL_GPL(lp55xx_is_extclk_used);

static void lp55xx_deinit_device(struct lp55xx_chip *chip)
{
	struct lp55xx_platform_data *pdata = chip->pdata;

	if (pdata->enable_gpiod)
		gpiod_set_value(pdata->enable_gpiod, 0);
}

static int lp55xx_init_device(struct lp55xx_chip *chip)
{
	struct lp55xx_platform_data *pdata;
	const struct lp55xx_device_config *cfg;
	struct device *dev = &chip->cl->dev;
	int ret = 0;

	WARN_ON(!chip);

	pdata = chip->pdata;
	cfg = chip->cfg;

	if (!pdata || !cfg)
		return -EINVAL;

	if (pdata->enable_gpiod) {
		gpiod_direction_output(pdata->enable_gpiod, 0);

		gpiod_set_consumer_name(pdata->enable_gpiod, "LP55xx enable");
		gpiod_set_value_cansleep(pdata->enable_gpiod, 0);
		usleep_range(1000, 2000); /* Keep enable down at least 1ms */
		gpiod_set_value_cansleep(pdata->enable_gpiod, 1);
		usleep_range(1000, 2000); /* 500us abs min. */
	}

	lp55xx_reset_device(chip);

	/*
	 * Exact value is not available. 10 - 20ms
	 * appears to be enough for reset.
	 */
	usleep_range(10000, 20000);

	ret = lp55xx_detect_device(chip);
	if (ret) {
		dev_err(dev, "device detection err: %d\n", ret);
		goto err;
	}

	/* chip specific initialization */
	ret = lp55xx_post_init_device(chip);
	if (ret) {
		dev_err(dev, "post init device err: %d\n", ret);
		goto err_post_init;
	}

	return 0;

err_post_init:
	lp55xx_deinit_device(chip);
err:
	return ret;
}

static int lp55xx_register_leds(struct lp55xx_led *led, struct lp55xx_chip *chip)
{
	struct lp55xx_platform_data *pdata = chip->pdata;
	const struct lp55xx_device_config *cfg = chip->cfg;
	int num_channels = pdata->num_channels;
	struct lp55xx_led *each;
	u8 led_current;
	int ret;
	int i;

	if (!cfg->brightness_fn) {
		dev_err(&chip->cl->dev, "empty brightness configuration\n");
		return -EINVAL;
	}

	for (i = 0; i < num_channels; i++) {

		/* do not initialize channels that are not connected */
		if (pdata->led_config[i].led_current == 0)
			continue;

		led_current = pdata->led_config[i].led_current;
		each = led + i;
		ret = lp55xx_init_led(each, chip, i);
		if (ret)
			goto err_init_led;

		chip->num_leds++;
		each->chip = chip;

		/* setting led current at each channel */
		if (cfg->set_led_current)
			cfg->set_led_current(each, led_current);
	}

	return 0;

err_init_led:
	return ret;
}

static int lp55xx_register_sysfs(struct lp55xx_chip *chip)
{
	struct device *dev = &chip->cl->dev;
	const struct lp55xx_device_config *cfg = chip->cfg;
	int ret;

	if (!cfg->run_engine || !cfg->firmware_cb)
		goto dev_specific_attrs;

	ret = sysfs_create_group(&dev->kobj, &lp55xx_engine_attr_group);
	if (ret)
		return ret;

dev_specific_attrs:
	return cfg->dev_attr_group ?
		sysfs_create_group(&dev->kobj, cfg->dev_attr_group) : 0;
}

static void lp55xx_unregister_sysfs(struct lp55xx_chip *chip)
{
	struct device *dev = &chip->cl->dev;
	const struct lp55xx_device_config *cfg = chip->cfg;

	if (cfg->dev_attr_group)
		sysfs_remove_group(&dev->kobj, cfg->dev_attr_group);

	sysfs_remove_group(&dev->kobj, &lp55xx_engine_attr_group);
}

static int lp55xx_parse_common_child(struct device_node *np,
				     struct lp55xx_led_config *cfg,
				     int led_number, int *chan_nr)
{
	int ret;

	of_property_read_string(np, "chan-name",
				&cfg[led_number].name);
	of_property_read_u8(np, "led-cur",
			    &cfg[led_number].led_current);
	of_property_read_u8(np, "max-cur",
			    &cfg[led_number].max_current);

	ret = of_property_read_u32(np, "reg", chan_nr);
	if (ret)
		return ret;

	if (*chan_nr < 0 || *chan_nr > cfg->max_channel)
		return -EINVAL;

	return 0;
}

static int lp55xx_parse_multi_led_child(struct device_node *child,
					 struct lp55xx_led_config *cfg,
					 int child_number, int color_number)
{
	int chan_nr, color_id, ret;

	ret = lp55xx_parse_common_child(child, cfg, child_number, &chan_nr);
	if (ret)
		return ret;

	ret = of_property_read_u32(child, "color", &color_id);
	if (ret)
		return ret;

	cfg[child_number].color_id[color_number] = color_id;
	cfg[child_number].output_num[color_number] = chan_nr;

	return 0;
}

static int lp55xx_parse_multi_led(struct device_node *np,
				  struct lp55xx_led_config *cfg,
				  int child_number)
{
	int num_colors = 0, ret;

	for_each_available_child_of_node_scoped(np, child) {
		ret = lp55xx_parse_multi_led_child(child, cfg, child_number,
						   num_colors);
		if (ret)
			return ret;
		num_colors++;
	}

	cfg[child_number].num_colors = num_colors;

	return 0;
}

static int lp55xx_parse_logical_led(struct device_node *np,
				   struct lp55xx_led_config *cfg,
				   int child_number)
{
	int led_color, ret;
	int chan_nr = 0;

	cfg[child_number].default_trigger =
		of_get_property(np, "linux,default-trigger", NULL);

	ret = of_property_read_u32(np, "color", &led_color);
	if (ret)
		return ret;

	if (led_color == LED_COLOR_ID_RGB)
		return lp55xx_parse_multi_led(np, cfg, child_number);

	ret =  lp55xx_parse_common_child(np, cfg, child_number, &chan_nr);
	if (ret < 0)
		return ret;

	cfg[child_number].chan_nr = chan_nr;

	return ret;
}

static struct lp55xx_platform_data *lp55xx_of_populate_pdata(struct device *dev,
							     struct device_node *np,
							     struct lp55xx_chip *chip)
{
	struct device_node *child;
	struct lp55xx_platform_data *pdata;
	struct lp55xx_led_config *cfg;
	int num_channels;
	int i = 0;
	int ret;

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return ERR_PTR(-ENOMEM);

	num_channels = of_get_available_child_count(np);
	if (num_channels == 0) {
		dev_err(dev, "no LED channels\n");
		return ERR_PTR(-EINVAL);
	}

	cfg = devm_kcalloc(dev, num_channels, sizeof(*cfg), GFP_KERNEL);
	if (!cfg)
		return ERR_PTR(-ENOMEM);

	pdata->led_config = &cfg[0];
	pdata->num_channels = num_channels;
	cfg->max_channel = chip->cfg->max_channel;

	for_each_available_child_of_node(np, child) {
		ret = lp55xx_parse_logical_led(child, cfg, i);
		if (ret) {
			of_node_put(child);
			return ERR_PTR(-EINVAL);
		}
		i++;
	}

	if (of_property_read_u32(np, "ti,charge-pump-mode", &pdata->charge_pump_mode))
		pdata->charge_pump_mode = LP55XX_CP_AUTO;

	if (pdata->charge_pump_mode > LP55XX_CP_AUTO) {
		dev_err(dev, "invalid charge pump mode %d\n", pdata->charge_pump_mode);
		return ERR_PTR(-EINVAL);
	}

	of_property_read_string(np, "label", &pdata->label);
	of_property_read_u8(np, "clock-mode", &pdata->clock_mode);

	pdata->enable_gpiod = devm_gpiod_get_optional(dev, "enable",
						      GPIOD_ASIS);
	if (IS_ERR(pdata->enable_gpiod))
		return ERR_CAST(pdata->enable_gpiod);

	/* LP8501 specific */
	of_property_read_u8(np, "pwr-sel", (u8 *)&pdata->pwr_sel);

	return pdata;
}

int lp55xx_probe(struct i2c_client *client)
{
	const struct i2c_device_id *id = i2c_client_get_device_id(client);
	int program_length, ret;
	struct lp55xx_chip *chip;
	struct lp55xx_led *led;
	struct lp55xx_platform_data *pdata = dev_get_platdata(&client->dev);
	struct device_node *np = dev_of_node(&client->dev);

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->cfg = i2c_get_match_data(client);

	if (!pdata) {
		if (np) {
			pdata = lp55xx_of_populate_pdata(&client->dev, np,
							 chip);
			if (IS_ERR(pdata))
				return PTR_ERR(pdata);
		} else {
			dev_err(&client->dev, "no platform data\n");
			return -EINVAL;
		}
	}

	/* Validate max program page */
	program_length = LP55xx_BYTES_PER_PAGE;
	if (chip->cfg->pages_per_engine)
		program_length *= chip->cfg->pages_per_engine;

	/* support a max of 128bytes */
	if (program_length > LP55xx_MAX_PROGRAM_LENGTH) {
		dev_err(&client->dev, "invalid pages_per_engine configured\n");
		return -EINVAL;
	}

	led = devm_kcalloc(&client->dev,
			   pdata->num_channels, sizeof(*led), GFP_KERNEL);
	if (!led)
		return -ENOMEM;

	chip->cl = client;
	chip->pdata = pdata;

	mutex_init(&chip->lock);

	i2c_set_clientdata(client, led);

	ret = lp55xx_init_device(chip);
	if (ret)
		goto err_init;

	dev_info(&client->dev, "%s Programmable led chip found\n", id->name);

	ret = lp55xx_register_leds(led, chip);
	if (ret)
		goto err_out;

	ret = lp55xx_register_sysfs(chip);
	if (ret) {
		dev_err(&client->dev, "registering sysfs failed\n");
		goto err_out;
	}

	return 0;

err_out:
	lp55xx_deinit_device(chip);
err_init:
	return ret;
}
EXPORT_SYMBOL_GPL(lp55xx_probe);

void lp55xx_remove(struct i2c_client *client)
{
	struct lp55xx_led *led = i2c_get_clientdata(client);
	struct lp55xx_chip *chip = led->chip;

	lp55xx_stop_all_engine(chip);
	lp55xx_unregister_sysfs(chip);
	lp55xx_deinit_device(chip);
}
EXPORT_SYMBOL_GPL(lp55xx_remove);

MODULE_AUTHOR("Milo Kim <milo.kim@ti.com>");
MODULE_DESCRIPTION("LP55xx Common Driver");
MODULE_LICENSE("GPL");
