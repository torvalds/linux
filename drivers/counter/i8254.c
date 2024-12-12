// SPDX-License-Identifier: GPL-2.0
/*
 * Intel 8254 Programmable Interval Timer
 * Copyright (C) William Breathitt Gray
 */
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/counter.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/i8254.h>
#include <linux/limits.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/regmap.h>

#include <linux/unaligned.h>

#define I8254_COUNTER_REG(_counter) (_counter)
#define I8254_CONTROL_REG 0x3

#define I8254_SC GENMASK(7, 6)
#define I8254_RW GENMASK(5, 4)
#define I8254_M GENMASK(3, 1)
#define I8254_CONTROL(_sc, _rw, _m) \
	(u8_encode_bits(_sc, I8254_SC) | u8_encode_bits(_rw, I8254_RW) | \
	 u8_encode_bits(_m, I8254_M))

#define I8254_RW_TWO_BYTE 0x3
#define I8254_MODE_INTERRUPT_ON_TERMINAL_COUNT 0
#define I8254_MODE_HARDWARE_RETRIGGERABLE_ONESHOT 1
#define I8254_MODE_RATE_GENERATOR 2
#define I8254_MODE_SQUARE_WAVE_MODE 3
#define I8254_MODE_SOFTWARE_TRIGGERED_STROBE 4
#define I8254_MODE_HARDWARE_TRIGGERED_STROBE 5

#define I8254_COUNTER_LATCH(_counter) I8254_CONTROL(_counter, 0x0, 0x0)
#define I8254_PROGRAM_COUNTER(_counter, _mode) I8254_CONTROL(_counter, I8254_RW_TWO_BYTE, _mode)

#define I8254_NUM_COUNTERS 3

/**
 * struct i8254 - I8254 device private data structure
 * @lock:	synchronization lock to prevent I/O race conditions
 * @preset:	array of Counter Register states
 * @out_mode:	array of mode configuration states
 * @map:	Regmap for the device
 */
struct i8254 {
	struct mutex lock;
	u16 preset[I8254_NUM_COUNTERS];
	u8 out_mode[I8254_NUM_COUNTERS];
	struct regmap *map;
};

static int i8254_count_read(struct counter_device *const counter, struct counter_count *const count,
			    u64 *const val)
{
	struct i8254 *const priv = counter_priv(counter);
	int ret;
	u8 value[2];

	mutex_lock(&priv->lock);

	ret = regmap_write(priv->map, I8254_CONTROL_REG, I8254_COUNTER_LATCH(count->id));
	if (ret) {
		mutex_unlock(&priv->lock);
		return ret;
	}
	ret = regmap_noinc_read(priv->map, I8254_COUNTER_REG(count->id), value, sizeof(value));
	if (ret) {
		mutex_unlock(&priv->lock);
		return ret;
	}

	mutex_unlock(&priv->lock);

	*val = get_unaligned_le16(value);

	return ret;
}

static int i8254_function_read(struct counter_device *const counter,
			       struct counter_count *const count,
			       enum counter_function *const function)
{
	*function = COUNTER_FUNCTION_DECREASE;
	return 0;
}

#define I8254_SYNAPSES_PER_COUNT 2
#define I8254_SIGNAL_ID_CLK 0
#define I8254_SIGNAL_ID_GATE 1

static int i8254_action_read(struct counter_device *const counter,
			     struct counter_count *const count,
			     struct counter_synapse *const synapse,
			     enum counter_synapse_action *const action)
{
	struct i8254 *const priv = counter_priv(counter);

	switch (synapse->signal->id % I8254_SYNAPSES_PER_COUNT) {
	case I8254_SIGNAL_ID_CLK:
		*action = COUNTER_SYNAPSE_ACTION_FALLING_EDGE;
		return 0;
	case I8254_SIGNAL_ID_GATE:
		switch (priv->out_mode[count->id]) {
		case I8254_MODE_HARDWARE_RETRIGGERABLE_ONESHOT:
		case I8254_MODE_RATE_GENERATOR:
		case I8254_MODE_SQUARE_WAVE_MODE:
		case I8254_MODE_HARDWARE_TRIGGERED_STROBE:
			*action = COUNTER_SYNAPSE_ACTION_RISING_EDGE;
			return 0;
		default:
			*action = COUNTER_SYNAPSE_ACTION_NONE;
			return 0;
		}
	default:
		/* should never reach this path */
		return -EINVAL;
	}
}

static int i8254_count_ceiling_read(struct counter_device *const counter,
				    struct counter_count *const count, u64 *const ceiling)
{
	struct i8254 *const priv = counter_priv(counter);

	mutex_lock(&priv->lock);

	switch (priv->out_mode[count->id]) {
	case I8254_MODE_RATE_GENERATOR:
		/* Rate Generator decrements 0 by one and the counter "wraps around" */
		*ceiling = (priv->preset[count->id] == 0) ? U16_MAX : priv->preset[count->id];
		break;
	case I8254_MODE_SQUARE_WAVE_MODE:
		if (priv->preset[count->id] % 2)
			*ceiling = priv->preset[count->id] - 1;
		else if (priv->preset[count->id] == 0)
			/* Square Wave Mode decrements 0 by two and the counter "wraps around" */
			*ceiling = U16_MAX - 1;
		else
			*ceiling = priv->preset[count->id];
		break;
	default:
		*ceiling = U16_MAX;
		break;
	}

	mutex_unlock(&priv->lock);

	return 0;
}

static int i8254_count_mode_read(struct counter_device *const counter,
				 struct counter_count *const count,
				 enum counter_count_mode *const count_mode)
{
	const struct i8254 *const priv = counter_priv(counter);

	switch (priv->out_mode[count->id]) {
	case I8254_MODE_INTERRUPT_ON_TERMINAL_COUNT:
		*count_mode = COUNTER_COUNT_MODE_INTERRUPT_ON_TERMINAL_COUNT;
		return 0;
	case I8254_MODE_HARDWARE_RETRIGGERABLE_ONESHOT:
		*count_mode = COUNTER_COUNT_MODE_HARDWARE_RETRIGGERABLE_ONESHOT;
		return 0;
	case I8254_MODE_RATE_GENERATOR:
		*count_mode = COUNTER_COUNT_MODE_RATE_GENERATOR;
		return 0;
	case I8254_MODE_SQUARE_WAVE_MODE:
		*count_mode = COUNTER_COUNT_MODE_SQUARE_WAVE_MODE;
		return 0;
	case I8254_MODE_SOFTWARE_TRIGGERED_STROBE:
		*count_mode = COUNTER_COUNT_MODE_SOFTWARE_TRIGGERED_STROBE;
		return 0;
	case I8254_MODE_HARDWARE_TRIGGERED_STROBE:
		*count_mode = COUNTER_COUNT_MODE_HARDWARE_TRIGGERED_STROBE;
		return 0;
	default:
		/* should never reach this path */
		return -EINVAL;
	}
}

static int i8254_count_mode_write(struct counter_device *const counter,
				  struct counter_count *const count,
				  const enum counter_count_mode count_mode)
{
	struct i8254 *const priv = counter_priv(counter);
	u8 out_mode;
	int ret;

	switch (count_mode) {
	case COUNTER_COUNT_MODE_INTERRUPT_ON_TERMINAL_COUNT:
		out_mode = I8254_MODE_INTERRUPT_ON_TERMINAL_COUNT;
		break;
	case COUNTER_COUNT_MODE_HARDWARE_RETRIGGERABLE_ONESHOT:
		out_mode = I8254_MODE_HARDWARE_RETRIGGERABLE_ONESHOT;
		break;
	case COUNTER_COUNT_MODE_RATE_GENERATOR:
		out_mode = I8254_MODE_RATE_GENERATOR;
		break;
	case COUNTER_COUNT_MODE_SQUARE_WAVE_MODE:
		out_mode = I8254_MODE_SQUARE_WAVE_MODE;
		break;
	case COUNTER_COUNT_MODE_SOFTWARE_TRIGGERED_STROBE:
		out_mode = I8254_MODE_SOFTWARE_TRIGGERED_STROBE;
		break;
	case COUNTER_COUNT_MODE_HARDWARE_TRIGGERED_STROBE:
		out_mode = I8254_MODE_HARDWARE_TRIGGERED_STROBE;
		break;
	default:
		/* should never reach this path */
		return -EINVAL;
	}

	mutex_lock(&priv->lock);

	/* Counter Register is cleared when the counter is programmed */
	priv->preset[count->id] = 0;
	priv->out_mode[count->id] = out_mode;
	ret = regmap_write(priv->map, I8254_CONTROL_REG,
			   I8254_PROGRAM_COUNTER(count->id, out_mode));

	mutex_unlock(&priv->lock);

	return ret;
}

static int i8254_count_floor_read(struct counter_device *const counter,
				  struct counter_count *const count, u64 *const floor)
{
	struct i8254 *const priv = counter_priv(counter);

	mutex_lock(&priv->lock);

	switch (priv->out_mode[count->id]) {
	case I8254_MODE_RATE_GENERATOR:
		/* counter is always reloaded after 1, but 0 is a possible reload value */
		*floor = (priv->preset[count->id] == 0) ? 0 : 1;
		break;
	case I8254_MODE_SQUARE_WAVE_MODE:
		/* counter is always reloaded after 2 for even preset values */
		*floor = (priv->preset[count->id] % 2 || priv->preset[count->id] == 0) ? 0 : 2;
		break;
	default:
		*floor = 0;
		break;
	}

	mutex_unlock(&priv->lock);

	return 0;
}

static int i8254_count_preset_read(struct counter_device *const counter,
				   struct counter_count *const count, u64 *const preset)
{
	const struct i8254 *const priv = counter_priv(counter);

	*preset = priv->preset[count->id];

	return 0;
}

static int i8254_count_preset_write(struct counter_device *const counter,
				    struct counter_count *const count, const u64 preset)
{
	struct i8254 *const priv = counter_priv(counter);
	int ret;
	u8 value[2];

	if (preset > U16_MAX)
		return -ERANGE;

	mutex_lock(&priv->lock);

	if (priv->out_mode[count->id] == I8254_MODE_RATE_GENERATOR ||
	    priv->out_mode[count->id] == I8254_MODE_SQUARE_WAVE_MODE) {
		if (preset == 1) {
			mutex_unlock(&priv->lock);
			return -EINVAL;
		}
	}

	priv->preset[count->id] = preset;

	put_unaligned_le16(preset, value);
	ret = regmap_noinc_write(priv->map, I8254_COUNTER_REG(count->id), value, 2);

	mutex_unlock(&priv->lock);

	return ret;
}

static int i8254_init_hw(struct regmap *const map)
{
	unsigned long i;
	int ret;

	for (i = 0; i < I8254_NUM_COUNTERS; i++) {
		/* Initialize each counter to Mode 0 */
		ret = regmap_write(map, I8254_CONTROL_REG,
				   I8254_PROGRAM_COUNTER(i, I8254_MODE_INTERRUPT_ON_TERMINAL_COUNT));
		if (ret)
			return ret;
	}

	return 0;
}

static const struct counter_ops i8254_ops = {
	.count_read = i8254_count_read,
	.function_read = i8254_function_read,
	.action_read = i8254_action_read,
};

#define I8254_SIGNAL(_id, _name) {		\
	.id = (_id),				\
	.name = (_name),			\
}

static struct counter_signal i8254_signals[] = {
	I8254_SIGNAL(0, "CLK 0"), I8254_SIGNAL(1, "GATE 0"),
	I8254_SIGNAL(2, "CLK 1"), I8254_SIGNAL(3, "GATE 1"),
	I8254_SIGNAL(4, "CLK 2"), I8254_SIGNAL(5, "GATE 2"),
};

static const enum counter_synapse_action i8254_clk_actions[] = {
	COUNTER_SYNAPSE_ACTION_FALLING_EDGE,
};
static const enum counter_synapse_action i8254_gate_actions[] = {
	COUNTER_SYNAPSE_ACTION_NONE,
	COUNTER_SYNAPSE_ACTION_RISING_EDGE,
};

#define I8254_SYNAPSES_BASE(_id) ((_id) * I8254_SYNAPSES_PER_COUNT)
#define I8254_SYNAPSE_CLK(_id) {					\
	.actions_list	= i8254_clk_actions,				\
	.num_actions	= ARRAY_SIZE(i8254_clk_actions),		\
	.signal		= &i8254_signals[I8254_SYNAPSES_BASE(_id) + 0],	\
}
#define I8254_SYNAPSE_GATE(_id) {					\
	.actions_list	= i8254_gate_actions,				\
	.num_actions	= ARRAY_SIZE(i8254_gate_actions),		\
	.signal		= &i8254_signals[I8254_SYNAPSES_BASE(_id) + 1],	\
}

static struct counter_synapse i8254_synapses[] = {
	I8254_SYNAPSE_CLK(0), I8254_SYNAPSE_GATE(0),
	I8254_SYNAPSE_CLK(1), I8254_SYNAPSE_GATE(1),
	I8254_SYNAPSE_CLK(2), I8254_SYNAPSE_GATE(2),
};

static const enum counter_function i8254_functions_list[] = {
	COUNTER_FUNCTION_DECREASE,
};

static const enum counter_count_mode i8254_count_modes[] = {
	COUNTER_COUNT_MODE_INTERRUPT_ON_TERMINAL_COUNT,
	COUNTER_COUNT_MODE_HARDWARE_RETRIGGERABLE_ONESHOT,
	COUNTER_COUNT_MODE_RATE_GENERATOR,
	COUNTER_COUNT_MODE_SQUARE_WAVE_MODE,
	COUNTER_COUNT_MODE_SOFTWARE_TRIGGERED_STROBE,
	COUNTER_COUNT_MODE_HARDWARE_TRIGGERED_STROBE,
};

static DEFINE_COUNTER_AVAILABLE(i8254_count_modes_available, i8254_count_modes);

static struct counter_comp i8254_count_ext[] = {
	COUNTER_COMP_CEILING(i8254_count_ceiling_read, NULL),
	COUNTER_COMP_COUNT_MODE(i8254_count_mode_read, i8254_count_mode_write,
				i8254_count_modes_available),
	COUNTER_COMP_FLOOR(i8254_count_floor_read, NULL),
	COUNTER_COMP_PRESET(i8254_count_preset_read, i8254_count_preset_write),
};

#define I8254_COUNT(_id, _name) {				\
	.id = (_id),						\
	.name = (_name),					\
	.functions_list = i8254_functions_list,			\
	.num_functions = ARRAY_SIZE(i8254_functions_list),	\
	.synapses = &i8254_synapses[I8254_SYNAPSES_BASE(_id)],	\
	.num_synapses =	I8254_SYNAPSES_PER_COUNT,		\
	.ext = i8254_count_ext,					\
	.num_ext = ARRAY_SIZE(i8254_count_ext)			\
}

static struct counter_count i8254_counts[I8254_NUM_COUNTERS] = {
	I8254_COUNT(0, "Counter 0"), I8254_COUNT(1, "Counter 1"), I8254_COUNT(2, "Counter 2"),
};

/**
 * devm_i8254_regmap_register - Register an i8254 Counter device
 * @dev: device that is registering this i8254 Counter device
 * @config: configuration for i8254_regmap_config
 *
 * Registers an Intel 8254 Programmable Interval Timer Counter device. Returns 0 on success and
 * negative error number on failure.
 */
int devm_i8254_regmap_register(struct device *const dev,
			       const struct i8254_regmap_config *const config)
{
	struct counter_device *counter;
	struct i8254 *priv;
	int err;

	if (!config->parent)
		return -EINVAL;

	if (!config->map)
		return -EINVAL;

	counter = devm_counter_alloc(dev, sizeof(*priv));
	if (!counter)
		return -ENOMEM;
	priv = counter_priv(counter);
	priv->map = config->map;

	counter->name = dev_name(config->parent);
	counter->parent = config->parent;
	counter->ops = &i8254_ops;
	counter->counts = i8254_counts;
	counter->num_counts = ARRAY_SIZE(i8254_counts);
	counter->signals = i8254_signals;
	counter->num_signals = ARRAY_SIZE(i8254_signals);

	mutex_init(&priv->lock);

	err = i8254_init_hw(priv->map);
	if (err)
		return err;

	err = devm_counter_add(dev, counter);
	if (err < 0)
		return dev_err_probe(dev, err, "Failed to add counter\n");

	return 0;
}
EXPORT_SYMBOL_NS_GPL(devm_i8254_regmap_register, "I8254");

MODULE_AUTHOR("William Breathitt Gray");
MODULE_DESCRIPTION("Intel 8254 Programmable Interval Timer");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("COUNTER");
