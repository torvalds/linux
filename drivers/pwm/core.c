// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Generic pwmlib implementation
 *
 * Copyright (C) 2011 Sascha Hauer <s.hauer@pengutronix.de>
 * Copyright (C) 2011-2012 Avionic Design GmbH
 */

#include <linux/acpi.h>
#include <linux/module.h>
#include <linux/idr.h>
#include <linux/of.h>
#include <linux/pwm.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>

#include <dt-bindings/pwm/pwm.h>

#define CREATE_TRACE_POINTS
#include <trace/events/pwm.h>

/* protects access to pwm_chips */
static DEFINE_MUTEX(pwm_lock);

static DEFINE_IDR(pwm_chips);

static void pwm_apply_debug(struct pwm_device *pwm,
			    const struct pwm_state *state)
{
	struct pwm_state *last = &pwm->last;
	struct pwm_chip *chip = pwm->chip;
	struct pwm_state s1 = { 0 }, s2 = { 0 };
	int err;

	if (!IS_ENABLED(CONFIG_PWM_DEBUG))
		return;

	/* No reasonable diagnosis possible without .get_state() */
	if (!chip->ops->get_state)
		return;

	/*
	 * *state was just applied. Read out the hardware state and do some
	 * checks.
	 */

	err = chip->ops->get_state(chip, pwm, &s1);
	trace_pwm_get(pwm, &s1, err);
	if (err)
		/* If that failed there isn't much to debug */
		return;

	/*
	 * The lowlevel driver either ignored .polarity (which is a bug) or as
	 * best effort inverted .polarity and fixed .duty_cycle respectively.
	 * Undo this inversion and fixup for further tests.
	 */
	if (s1.enabled && s1.polarity != state->polarity) {
		s2.polarity = state->polarity;
		s2.duty_cycle = s1.period - s1.duty_cycle;
		s2.period = s1.period;
		s2.enabled = s1.enabled;
	} else {
		s2 = s1;
	}

	if (s2.polarity != state->polarity &&
	    state->duty_cycle < state->period)
		dev_warn(pwmchip_parent(chip), ".apply ignored .polarity\n");

	if (state->enabled &&
	    last->polarity == state->polarity &&
	    last->period > s2.period &&
	    last->period <= state->period)
		dev_warn(pwmchip_parent(chip),
			 ".apply didn't pick the best available period (requested: %llu, applied: %llu, possible: %llu)\n",
			 state->period, s2.period, last->period);

	if (state->enabled && state->period < s2.period)
		dev_warn(pwmchip_parent(chip),
			 ".apply is supposed to round down period (requested: %llu, applied: %llu)\n",
			 state->period, s2.period);

	if (state->enabled &&
	    last->polarity == state->polarity &&
	    last->period == s2.period &&
	    last->duty_cycle > s2.duty_cycle &&
	    last->duty_cycle <= state->duty_cycle)
		dev_warn(pwmchip_parent(chip),
			 ".apply didn't pick the best available duty cycle (requested: %llu/%llu, applied: %llu/%llu, possible: %llu/%llu)\n",
			 state->duty_cycle, state->period,
			 s2.duty_cycle, s2.period,
			 last->duty_cycle, last->period);

	if (state->enabled && state->duty_cycle < s2.duty_cycle)
		dev_warn(pwmchip_parent(chip),
			 ".apply is supposed to round down duty_cycle (requested: %llu/%llu, applied: %llu/%llu)\n",
			 state->duty_cycle, state->period,
			 s2.duty_cycle, s2.period);

	if (!state->enabled && s2.enabled && s2.duty_cycle > 0)
		dev_warn(pwmchip_parent(chip),
			 "requested disabled, but yielded enabled with duty > 0\n");

	/* reapply the state that the driver reported being configured. */
	err = chip->ops->apply(chip, pwm, &s1);
	trace_pwm_apply(pwm, &s1, err);
	if (err) {
		*last = s1;
		dev_err(pwmchip_parent(chip), "failed to reapply current setting\n");
		return;
	}

	*last = (struct pwm_state){ 0 };
	err = chip->ops->get_state(chip, pwm, last);
	trace_pwm_get(pwm, last, err);
	if (err)
		return;

	/* reapplication of the current state should give an exact match */
	if (s1.enabled != last->enabled ||
	    s1.polarity != last->polarity ||
	    (s1.enabled && s1.period != last->period) ||
	    (s1.enabled && s1.duty_cycle != last->duty_cycle)) {
		dev_err(pwmchip_parent(chip),
			".apply is not idempotent (ena=%d pol=%d %llu/%llu) -> (ena=%d pol=%d %llu/%llu)\n",
			s1.enabled, s1.polarity, s1.duty_cycle, s1.period,
			last->enabled, last->polarity, last->duty_cycle,
			last->period);
	}
}

/**
 * __pwm_apply() - atomically apply a new state to a PWM device
 * @pwm: PWM device
 * @state: new state to apply
 */
static int __pwm_apply(struct pwm_device *pwm, const struct pwm_state *state)
{
	struct pwm_chip *chip;
	int err;

	if (!pwm || !state || !state->period ||
	    state->duty_cycle > state->period)
		return -EINVAL;

	chip = pwm->chip;

	if (state->period == pwm->state.period &&
	    state->duty_cycle == pwm->state.duty_cycle &&
	    state->polarity == pwm->state.polarity &&
	    state->enabled == pwm->state.enabled &&
	    state->usage_power == pwm->state.usage_power)
		return 0;

	err = chip->ops->apply(chip, pwm, state);
	trace_pwm_apply(pwm, state, err);
	if (err)
		return err;

	pwm->state = *state;

	/*
	 * only do this after pwm->state was applied as some
	 * implementations of .get_state depend on this
	 */
	pwm_apply_debug(pwm, state);

	return 0;
}

/**
 * pwm_apply_might_sleep() - atomically apply a new state to a PWM device
 * Cannot be used in atomic context.
 * @pwm: PWM device
 * @state: new state to apply
 */
int pwm_apply_might_sleep(struct pwm_device *pwm, const struct pwm_state *state)
{
	int err;

	/*
	 * Some lowlevel driver's implementations of .apply() make use of
	 * mutexes, also with some drivers only returning when the new
	 * configuration is active calling pwm_apply_might_sleep() from atomic context
	 * is a bad idea. So make it explicit that calling this function might
	 * sleep.
	 */
	might_sleep();

	if (IS_ENABLED(CONFIG_PWM_DEBUG) && pwm->chip->atomic) {
		/*
		 * Catch any drivers that have been marked as atomic but
		 * that will sleep anyway.
		 */
		non_block_start();
		err = __pwm_apply(pwm, state);
		non_block_end();
	} else {
		err = __pwm_apply(pwm, state);
	}

	return err;
}
EXPORT_SYMBOL_GPL(pwm_apply_might_sleep);

/**
 * pwm_apply_atomic() - apply a new state to a PWM device from atomic context
 * Not all PWM devices support this function, check with pwm_might_sleep().
 * @pwm: PWM device
 * @state: new state to apply
 */
int pwm_apply_atomic(struct pwm_device *pwm, const struct pwm_state *state)
{
	WARN_ONCE(!pwm->chip->atomic,
		  "sleeping PWM driver used in atomic context\n");

	return __pwm_apply(pwm, state);
}
EXPORT_SYMBOL_GPL(pwm_apply_atomic);

/**
 * pwm_adjust_config() - adjust the current PWM config to the PWM arguments
 * @pwm: PWM device
 *
 * This function will adjust the PWM config to the PWM arguments provided
 * by the DT or PWM lookup table. This is particularly useful to adapt
 * the bootloader config to the Linux one.
 */
int pwm_adjust_config(struct pwm_device *pwm)
{
	struct pwm_state state;
	struct pwm_args pargs;

	pwm_get_args(pwm, &pargs);
	pwm_get_state(pwm, &state);

	/*
	 * If the current period is zero it means that either the PWM driver
	 * does not support initial state retrieval or the PWM has not yet
	 * been configured.
	 *
	 * In either case, we setup the new period and polarity, and assign a
	 * duty cycle of 0.
	 */
	if (!state.period) {
		state.duty_cycle = 0;
		state.period = pargs.period;
		state.polarity = pargs.polarity;

		return pwm_apply_might_sleep(pwm, &state);
	}

	/*
	 * Adjust the PWM duty cycle/period based on the period value provided
	 * in PWM args.
	 */
	if (pargs.period != state.period) {
		u64 dutycycle = (u64)state.duty_cycle * pargs.period;

		do_div(dutycycle, state.period);
		state.duty_cycle = dutycycle;
		state.period = pargs.period;
	}

	/*
	 * If the polarity changed, we should also change the duty cycle.
	 */
	if (pargs.polarity != state.polarity) {
		state.polarity = pargs.polarity;
		state.duty_cycle = state.period - state.duty_cycle;
	}

	return pwm_apply_might_sleep(pwm, &state);
}
EXPORT_SYMBOL_GPL(pwm_adjust_config);

/**
 * pwm_capture() - capture and report a PWM signal
 * @pwm: PWM device
 * @result: structure to fill with capture result
 * @timeout: time to wait, in milliseconds, before giving up on capture
 *
 * Returns: 0 on success or a negative error code on failure.
 */
int pwm_capture(struct pwm_device *pwm, struct pwm_capture *result,
		unsigned long timeout)
{
	int err;

	if (!pwm || !pwm->chip->ops)
		return -EINVAL;

	if (!pwm->chip->ops->capture)
		return -ENOSYS;

	mutex_lock(&pwm_lock);
	err = pwm->chip->ops->capture(pwm->chip, pwm, result, timeout);
	mutex_unlock(&pwm_lock);

	return err;
}
EXPORT_SYMBOL_GPL(pwm_capture);

static struct pwm_chip *pwmchip_find_by_name(const char *name)
{
	struct pwm_chip *chip;
	unsigned long id, tmp;

	if (!name)
		return NULL;

	mutex_lock(&pwm_lock);

	idr_for_each_entry_ul(&pwm_chips, chip, tmp, id) {
		const char *chip_name = dev_name(pwmchip_parent(chip));

		if (chip_name && strcmp(chip_name, name) == 0) {
			mutex_unlock(&pwm_lock);
			return chip;
		}
	}

	mutex_unlock(&pwm_lock);

	return NULL;
}

static int pwm_device_request(struct pwm_device *pwm, const char *label)
{
	int err;
	struct pwm_chip *chip = pwm->chip;
	const struct pwm_ops *ops = chip->ops;

	if (test_bit(PWMF_REQUESTED, &pwm->flags))
		return -EBUSY;

	if (!try_module_get(chip->owner))
		return -ENODEV;

	if (ops->request) {
		err = ops->request(chip, pwm);
		if (err) {
			module_put(chip->owner);
			return err;
		}
	}

	if (ops->get_state) {
		/*
		 * Zero-initialize state because most drivers are unaware of
		 * .usage_power. The other members of state are supposed to be
		 * set by lowlevel drivers. We still initialize the whole
		 * structure for simplicity even though this might paper over
		 * faulty implementations of .get_state().
		 */
		struct pwm_state state = { 0, };

		err = ops->get_state(chip, pwm, &state);
		trace_pwm_get(pwm, &state, err);

		if (!err)
			pwm->state = state;

		if (IS_ENABLED(CONFIG_PWM_DEBUG))
			pwm->last = pwm->state;
	}

	set_bit(PWMF_REQUESTED, &pwm->flags);
	pwm->label = label;

	return 0;
}

/**
 * pwm_request_from_chip() - request a PWM device relative to a PWM chip
 * @chip: PWM chip
 * @index: per-chip index of the PWM to request
 * @label: a literal description string of this PWM
 *
 * Returns: A pointer to the PWM device at the given index of the given PWM
 * chip. A negative error code is returned if the index is not valid for the
 * specified PWM chip or if the PWM device cannot be requested.
 */
struct pwm_device *pwm_request_from_chip(struct pwm_chip *chip,
					 unsigned int index,
					 const char *label)
{
	struct pwm_device *pwm;
	int err;

	if (!chip || index >= chip->npwm)
		return ERR_PTR(-EINVAL);

	mutex_lock(&pwm_lock);
	pwm = &chip->pwms[index];

	err = pwm_device_request(pwm, label);
	if (err < 0)
		pwm = ERR_PTR(err);

	mutex_unlock(&pwm_lock);
	return pwm;
}
EXPORT_SYMBOL_GPL(pwm_request_from_chip);


struct pwm_device *
of_pwm_xlate_with_flags(struct pwm_chip *chip, const struct of_phandle_args *args)
{
	struct pwm_device *pwm;

	/* period in the second cell and flags in the third cell are optional */
	if (args->args_count < 1)
		return ERR_PTR(-EINVAL);

	pwm = pwm_request_from_chip(chip, args->args[0], NULL);
	if (IS_ERR(pwm))
		return pwm;

	if (args->args_count > 1)
		pwm->args.period = args->args[1];

	pwm->args.polarity = PWM_POLARITY_NORMAL;
	if (args->args_count > 2 && args->args[2] & PWM_POLARITY_INVERTED)
		pwm->args.polarity = PWM_POLARITY_INVERSED;

	return pwm;
}
EXPORT_SYMBOL_GPL(of_pwm_xlate_with_flags);

struct pwm_device *
of_pwm_single_xlate(struct pwm_chip *chip, const struct of_phandle_args *args)
{
	struct pwm_device *pwm;

	pwm = pwm_request_from_chip(chip, 0, NULL);
	if (IS_ERR(pwm))
		return pwm;

	if (args->args_count > 1)
		pwm->args.period = args->args[0];

	pwm->args.polarity = PWM_POLARITY_NORMAL;
	if (args->args_count > 1 && args->args[1] & PWM_POLARITY_INVERTED)
		pwm->args.polarity = PWM_POLARITY_INVERSED;

	return pwm;
}
EXPORT_SYMBOL_GPL(of_pwm_single_xlate);

#define PWMCHIP_ALIGN ARCH_DMA_MINALIGN

static void *pwmchip_priv(struct pwm_chip *chip)
{
	return (void *)chip + ALIGN(sizeof(*chip), PWMCHIP_ALIGN);
}

/* This is the counterpart to pwmchip_alloc() */
void pwmchip_put(struct pwm_chip *chip)
{
	kfree(chip);
}
EXPORT_SYMBOL_GPL(pwmchip_put);

struct pwm_chip *pwmchip_alloc(struct device *parent, unsigned int npwm, size_t sizeof_priv)
{
	struct pwm_chip *chip;
	size_t alloc_size;

	alloc_size = size_add(ALIGN(sizeof(*chip), PWMCHIP_ALIGN), sizeof_priv);

	chip = kzalloc(alloc_size, GFP_KERNEL);
	if (!chip)
		return ERR_PTR(-ENOMEM);

	chip->dev = parent;
	chip->npwm = npwm;

	pwmchip_set_drvdata(chip, pwmchip_priv(chip));

	return chip;
}
EXPORT_SYMBOL_GPL(pwmchip_alloc);

static void devm_pwmchip_put(void *data)
{
	struct pwm_chip *chip = data;

	pwmchip_put(chip);
}

struct pwm_chip *devm_pwmchip_alloc(struct device *parent, unsigned int npwm, size_t sizeof_priv)
{
	struct pwm_chip *chip;
	int ret;

	chip = pwmchip_alloc(parent, npwm, sizeof_priv);
	if (IS_ERR(chip))
		return chip;

	ret = devm_add_action_or_reset(parent, devm_pwmchip_put, chip);
	if (ret)
		return ERR_PTR(ret);

	return chip;
}
EXPORT_SYMBOL_GPL(devm_pwmchip_alloc);

static void of_pwmchip_add(struct pwm_chip *chip)
{
	if (!pwmchip_parent(chip) || !pwmchip_parent(chip)->of_node)
		return;

	if (!chip->of_xlate)
		chip->of_xlate = of_pwm_xlate_with_flags;

	of_node_get(pwmchip_parent(chip)->of_node);
}

static void of_pwmchip_remove(struct pwm_chip *chip)
{
	if (pwmchip_parent(chip))
		of_node_put(pwmchip_parent(chip)->of_node);
}

static bool pwm_ops_check(const struct pwm_chip *chip)
{
	const struct pwm_ops *ops = chip->ops;

	if (!ops->apply)
		return false;

	if (IS_ENABLED(CONFIG_PWM_DEBUG) && !ops->get_state)
		dev_warn(pwmchip_parent(chip),
			 "Please implement the .get_state() callback\n");

	return true;
}

/**
 * __pwmchip_add() - register a new PWM chip
 * @chip: the PWM chip to add
 * @owner: reference to the module providing the chip.
 *
 * Register a new PWM chip. @owner is supposed to be THIS_MODULE, use the
 * pwmchip_add wrapper to do this right.
 *
 * Returns: 0 on success or a negative error code on failure.
 */
int __pwmchip_add(struct pwm_chip *chip, struct module *owner)
{
	unsigned int i;
	int ret;

	if (!chip || !pwmchip_parent(chip) || !chip->ops || !chip->npwm)
		return -EINVAL;

	if (!pwm_ops_check(chip))
		return -EINVAL;

	chip->owner = owner;

	chip->pwms = kcalloc(chip->npwm, sizeof(*chip->pwms), GFP_KERNEL);
	if (!chip->pwms)
		return -ENOMEM;

	mutex_lock(&pwm_lock);

	ret = idr_alloc(&pwm_chips, chip, 0, 0, GFP_KERNEL);
	if (ret < 0) {
		mutex_unlock(&pwm_lock);
		kfree(chip->pwms);
		return ret;
	}

	chip->id = ret;

	for (i = 0; i < chip->npwm; i++) {
		struct pwm_device *pwm = &chip->pwms[i];

		pwm->chip = chip;
		pwm->hwpwm = i;
	}

	mutex_unlock(&pwm_lock);

	if (IS_ENABLED(CONFIG_OF))
		of_pwmchip_add(chip);

	pwmchip_sysfs_export(chip);

	return 0;
}
EXPORT_SYMBOL_GPL(__pwmchip_add);

/**
 * pwmchip_remove() - remove a PWM chip
 * @chip: the PWM chip to remove
 *
 * Removes a PWM chip.
 */
void pwmchip_remove(struct pwm_chip *chip)
{
	pwmchip_sysfs_unexport(chip);

	if (IS_ENABLED(CONFIG_OF))
		of_pwmchip_remove(chip);

	mutex_lock(&pwm_lock);

	idr_remove(&pwm_chips, chip->id);

	mutex_unlock(&pwm_lock);

	kfree(chip->pwms);
}
EXPORT_SYMBOL_GPL(pwmchip_remove);

static void devm_pwmchip_remove(void *data)
{
	struct pwm_chip *chip = data;

	pwmchip_remove(chip);
}

int __devm_pwmchip_add(struct device *dev, struct pwm_chip *chip, struct module *owner)
{
	int ret;

	ret = __pwmchip_add(chip, owner);
	if (ret)
		return ret;

	return devm_add_action_or_reset(dev, devm_pwmchip_remove, chip);
}
EXPORT_SYMBOL_GPL(__devm_pwmchip_add);

static struct device_link *pwm_device_link_add(struct device *dev,
					       struct pwm_device *pwm)
{
	struct device_link *dl;

	if (!dev) {
		/*
		 * No device for the PWM consumer has been provided. It may
		 * impact the PM sequence ordering: the PWM supplier may get
		 * suspended before the consumer.
		 */
		dev_warn(pwmchip_parent(pwm->chip),
			 "No consumer device specified to create a link to\n");
		return NULL;
	}

	dl = device_link_add(dev, pwmchip_parent(pwm->chip), DL_FLAG_AUTOREMOVE_CONSUMER);
	if (!dl) {
		dev_err(dev, "failed to create device link to %s\n",
			dev_name(pwmchip_parent(pwm->chip)));
		return ERR_PTR(-EINVAL);
	}

	return dl;
}

static struct pwm_chip *fwnode_to_pwmchip(struct fwnode_handle *fwnode)
{
	struct pwm_chip *chip;
	unsigned long id, tmp;

	mutex_lock(&pwm_lock);

	idr_for_each_entry_ul(&pwm_chips, chip, tmp, id)
		if (pwmchip_parent(chip) && device_match_fwnode(pwmchip_parent(chip), fwnode)) {
			mutex_unlock(&pwm_lock);
			return chip;
		}

	mutex_unlock(&pwm_lock);

	return ERR_PTR(-EPROBE_DEFER);
}

/**
 * of_pwm_get() - request a PWM via the PWM framework
 * @dev: device for PWM consumer
 * @np: device node to get the PWM from
 * @con_id: consumer name
 *
 * Returns the PWM device parsed from the phandle and index specified in the
 * "pwms" property of a device tree node or a negative error-code on failure.
 * Values parsed from the device tree are stored in the returned PWM device
 * object.
 *
 * If con_id is NULL, the first PWM device listed in the "pwms" property will
 * be requested. Otherwise the "pwm-names" property is used to do a reverse
 * lookup of the PWM index. This also means that the "pwm-names" property
 * becomes mandatory for devices that look up the PWM device via the con_id
 * parameter.
 *
 * Returns: A pointer to the requested PWM device or an ERR_PTR()-encoded
 * error code on failure.
 */
static struct pwm_device *of_pwm_get(struct device *dev, struct device_node *np,
				     const char *con_id)
{
	struct pwm_device *pwm = NULL;
	struct of_phandle_args args;
	struct device_link *dl;
	struct pwm_chip *chip;
	int index = 0;
	int err;

	if (con_id) {
		index = of_property_match_string(np, "pwm-names", con_id);
		if (index < 0)
			return ERR_PTR(index);
	}

	err = of_parse_phandle_with_args(np, "pwms", "#pwm-cells", index,
					 &args);
	if (err) {
		pr_err("%s(): can't parse \"pwms\" property\n", __func__);
		return ERR_PTR(err);
	}

	chip = fwnode_to_pwmchip(of_fwnode_handle(args.np));
	if (IS_ERR(chip)) {
		if (PTR_ERR(chip) != -EPROBE_DEFER)
			pr_err("%s(): PWM chip not found\n", __func__);

		pwm = ERR_CAST(chip);
		goto put;
	}

	pwm = chip->of_xlate(chip, &args);
	if (IS_ERR(pwm))
		goto put;

	dl = pwm_device_link_add(dev, pwm);
	if (IS_ERR(dl)) {
		/* of_xlate ended up calling pwm_request_from_chip() */
		pwm_put(pwm);
		pwm = ERR_CAST(dl);
		goto put;
	}

	/*
	 * If a consumer name was not given, try to look it up from the
	 * "pwm-names" property if it exists. Otherwise use the name of
	 * the user device node.
	 */
	if (!con_id) {
		err = of_property_read_string_index(np, "pwm-names", index,
						    &con_id);
		if (err < 0)
			con_id = np->name;
	}

	pwm->label = con_id;

put:
	of_node_put(args.np);

	return pwm;
}

/**
 * acpi_pwm_get() - request a PWM via parsing "pwms" property in ACPI
 * @fwnode: firmware node to get the "pwms" property from
 *
 * Returns the PWM device parsed from the fwnode and index specified in the
 * "pwms" property or a negative error-code on failure.
 * Values parsed from the device tree are stored in the returned PWM device
 * object.
 *
 * This is analogous to of_pwm_get() except con_id is not yet supported.
 * ACPI entries must look like
 * Package () {"pwms", Package ()
 *     { <PWM device reference>, <PWM index>, <PWM period> [, <PWM flags>]}}
 *
 * Returns: A pointer to the requested PWM device or an ERR_PTR()-encoded
 * error code on failure.
 */
static struct pwm_device *acpi_pwm_get(const struct fwnode_handle *fwnode)
{
	struct pwm_device *pwm;
	struct fwnode_reference_args args;
	struct pwm_chip *chip;
	int ret;

	memset(&args, 0, sizeof(args));

	ret = __acpi_node_get_property_reference(fwnode, "pwms", 0, 3, &args);
	if (ret < 0)
		return ERR_PTR(ret);

	if (args.nargs < 2)
		return ERR_PTR(-EPROTO);

	chip = fwnode_to_pwmchip(args.fwnode);
	if (IS_ERR(chip))
		return ERR_CAST(chip);

	pwm = pwm_request_from_chip(chip, args.args[0], NULL);
	if (IS_ERR(pwm))
		return pwm;

	pwm->args.period = args.args[1];
	pwm->args.polarity = PWM_POLARITY_NORMAL;

	if (args.nargs > 2 && args.args[2] & PWM_POLARITY_INVERTED)
		pwm->args.polarity = PWM_POLARITY_INVERSED;

	return pwm;
}

static DEFINE_MUTEX(pwm_lookup_lock);
static LIST_HEAD(pwm_lookup_list);

/**
 * pwm_add_table() - register PWM device consumers
 * @table: array of consumers to register
 * @num: number of consumers in table
 */
void pwm_add_table(struct pwm_lookup *table, size_t num)
{
	mutex_lock(&pwm_lookup_lock);

	while (num--) {
		list_add_tail(&table->list, &pwm_lookup_list);
		table++;
	}

	mutex_unlock(&pwm_lookup_lock);
}

/**
 * pwm_remove_table() - unregister PWM device consumers
 * @table: array of consumers to unregister
 * @num: number of consumers in table
 */
void pwm_remove_table(struct pwm_lookup *table, size_t num)
{
	mutex_lock(&pwm_lookup_lock);

	while (num--) {
		list_del(&table->list);
		table++;
	}

	mutex_unlock(&pwm_lookup_lock);
}

/**
 * pwm_get() - look up and request a PWM device
 * @dev: device for PWM consumer
 * @con_id: consumer name
 *
 * Lookup is first attempted using DT. If the device was not instantiated from
 * a device tree, a PWM chip and a relative index is looked up via a table
 * supplied by board setup code (see pwm_add_table()).
 *
 * Once a PWM chip has been found the specified PWM device will be requested
 * and is ready to be used.
 *
 * Returns: A pointer to the requested PWM device or an ERR_PTR()-encoded
 * error code on failure.
 */
struct pwm_device *pwm_get(struct device *dev, const char *con_id)
{
	const struct fwnode_handle *fwnode = dev ? dev_fwnode(dev) : NULL;
	const char *dev_id = dev ? dev_name(dev) : NULL;
	struct pwm_device *pwm;
	struct pwm_chip *chip;
	struct device_link *dl;
	unsigned int best = 0;
	struct pwm_lookup *p, *chosen = NULL;
	unsigned int match;
	int err;

	/* look up via DT first */
	if (is_of_node(fwnode))
		return of_pwm_get(dev, to_of_node(fwnode), con_id);

	/* then lookup via ACPI */
	if (is_acpi_node(fwnode)) {
		pwm = acpi_pwm_get(fwnode);
		if (!IS_ERR(pwm) || PTR_ERR(pwm) != -ENOENT)
			return pwm;
	}

	/*
	 * We look up the provider in the static table typically provided by
	 * board setup code. We first try to lookup the consumer device by
	 * name. If the consumer device was passed in as NULL or if no match
	 * was found, we try to find the consumer by directly looking it up
	 * by name.
	 *
	 * If a match is found, the provider PWM chip is looked up by name
	 * and a PWM device is requested using the PWM device per-chip index.
	 *
	 * The lookup algorithm was shamelessly taken from the clock
	 * framework:
	 *
	 * We do slightly fuzzy matching here:
	 *  An entry with a NULL ID is assumed to be a wildcard.
	 *  If an entry has a device ID, it must match
	 *  If an entry has a connection ID, it must match
	 * Then we take the most specific entry - with the following order
	 * of precedence: dev+con > dev only > con only.
	 */
	mutex_lock(&pwm_lookup_lock);

	list_for_each_entry(p, &pwm_lookup_list, list) {
		match = 0;

		if (p->dev_id) {
			if (!dev_id || strcmp(p->dev_id, dev_id))
				continue;

			match += 2;
		}

		if (p->con_id) {
			if (!con_id || strcmp(p->con_id, con_id))
				continue;

			match += 1;
		}

		if (match > best) {
			chosen = p;

			if (match != 3)
				best = match;
			else
				break;
		}
	}

	mutex_unlock(&pwm_lookup_lock);

	if (!chosen)
		return ERR_PTR(-ENODEV);

	chip = pwmchip_find_by_name(chosen->provider);

	/*
	 * If the lookup entry specifies a module, load the module and retry
	 * the PWM chip lookup. This can be used to work around driver load
	 * ordering issues if driver's can't be made to properly support the
	 * deferred probe mechanism.
	 */
	if (!chip && chosen->module) {
		err = request_module(chosen->module);
		if (err == 0)
			chip = pwmchip_find_by_name(chosen->provider);
	}

	if (!chip)
		return ERR_PTR(-EPROBE_DEFER);

	pwm = pwm_request_from_chip(chip, chosen->index, con_id ?: dev_id);
	if (IS_ERR(pwm))
		return pwm;

	dl = pwm_device_link_add(dev, pwm);
	if (IS_ERR(dl)) {
		pwm_put(pwm);
		return ERR_CAST(dl);
	}

	pwm->args.period = chosen->period;
	pwm->args.polarity = chosen->polarity;

	return pwm;
}
EXPORT_SYMBOL_GPL(pwm_get);

/**
 * pwm_put() - release a PWM device
 * @pwm: PWM device
 */
void pwm_put(struct pwm_device *pwm)
{
	if (!pwm)
		return;

	mutex_lock(&pwm_lock);

	if (!test_and_clear_bit(PWMF_REQUESTED, &pwm->flags)) {
		pr_warn("PWM device already freed\n");
		goto out;
	}

	if (pwm->chip->ops->free)
		pwm->chip->ops->free(pwm->chip, pwm);

	pwm->label = NULL;

	module_put(pwm->chip->owner);
out:
	mutex_unlock(&pwm_lock);
}
EXPORT_SYMBOL_GPL(pwm_put);

static void devm_pwm_release(void *pwm)
{
	pwm_put(pwm);
}

/**
 * devm_pwm_get() - resource managed pwm_get()
 * @dev: device for PWM consumer
 * @con_id: consumer name
 *
 * This function performs like pwm_get() but the acquired PWM device will
 * automatically be released on driver detach.
 *
 * Returns: A pointer to the requested PWM device or an ERR_PTR()-encoded
 * error code on failure.
 */
struct pwm_device *devm_pwm_get(struct device *dev, const char *con_id)
{
	struct pwm_device *pwm;
	int ret;

	pwm = pwm_get(dev, con_id);
	if (IS_ERR(pwm))
		return pwm;

	ret = devm_add_action_or_reset(dev, devm_pwm_release, pwm);
	if (ret)
		return ERR_PTR(ret);

	return pwm;
}
EXPORT_SYMBOL_GPL(devm_pwm_get);

/**
 * devm_fwnode_pwm_get() - request a resource managed PWM from firmware node
 * @dev: device for PWM consumer
 * @fwnode: firmware node to get the PWM from
 * @con_id: consumer name
 *
 * Returns the PWM device parsed from the firmware node. See of_pwm_get() and
 * acpi_pwm_get() for a detailed description.
 *
 * Returns: A pointer to the requested PWM device or an ERR_PTR()-encoded
 * error code on failure.
 */
struct pwm_device *devm_fwnode_pwm_get(struct device *dev,
				       struct fwnode_handle *fwnode,
				       const char *con_id)
{
	struct pwm_device *pwm = ERR_PTR(-ENODEV);
	int ret;

	if (is_of_node(fwnode))
		pwm = of_pwm_get(dev, to_of_node(fwnode), con_id);
	else if (is_acpi_node(fwnode))
		pwm = acpi_pwm_get(fwnode);
	if (IS_ERR(pwm))
		return pwm;

	ret = devm_add_action_or_reset(dev, devm_pwm_release, pwm);
	if (ret)
		return ERR_PTR(ret);

	return pwm;
}
EXPORT_SYMBOL_GPL(devm_fwnode_pwm_get);

#ifdef CONFIG_DEBUG_FS
static void pwm_dbg_show(struct pwm_chip *chip, struct seq_file *s)
{
	unsigned int i;

	for (i = 0; i < chip->npwm; i++) {
		struct pwm_device *pwm = &chip->pwms[i];
		struct pwm_state state;

		pwm_get_state(pwm, &state);

		seq_printf(s, " pwm-%-3d (%-20.20s):", i, pwm->label);

		if (test_bit(PWMF_REQUESTED, &pwm->flags))
			seq_puts(s, " requested");

		if (state.enabled)
			seq_puts(s, " enabled");

		seq_printf(s, " period: %llu ns", state.period);
		seq_printf(s, " duty: %llu ns", state.duty_cycle);
		seq_printf(s, " polarity: %s",
			   state.polarity ? "inverse" : "normal");

		if (state.usage_power)
			seq_puts(s, " usage_power");

		seq_puts(s, "\n");
	}
}

static void *pwm_seq_start(struct seq_file *s, loff_t *pos)
{
	unsigned long id = *pos;
	void *ret;

	mutex_lock(&pwm_lock);
	s->private = "";

	ret = idr_get_next_ul(&pwm_chips, &id);
	*pos = id;
	return ret;
}

static void *pwm_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	unsigned long id = *pos + 1;
	void *ret;

	s->private = "\n";

	ret = idr_get_next_ul(&pwm_chips, &id);
	*pos = id;
	return ret;
}

static void pwm_seq_stop(struct seq_file *s, void *v)
{
	mutex_unlock(&pwm_lock);
}

static int pwm_seq_show(struct seq_file *s, void *v)
{
	struct pwm_chip *chip = v;

	seq_printf(s, "%s%d: %s/%s, %d PWM device%s\n",
		   (char *)s->private, chip->id,
		   pwmchip_parent(chip)->bus ? pwmchip_parent(chip)->bus->name : "no-bus",
		   dev_name(pwmchip_parent(chip)), chip->npwm,
		   (chip->npwm != 1) ? "s" : "");

	pwm_dbg_show(chip, s);

	return 0;
}

static const struct seq_operations pwm_debugfs_sops = {
	.start = pwm_seq_start,
	.next = pwm_seq_next,
	.stop = pwm_seq_stop,
	.show = pwm_seq_show,
};

DEFINE_SEQ_ATTRIBUTE(pwm_debugfs);

static int __init pwm_debugfs_init(void)
{
	debugfs_create_file("pwm", 0444, NULL, NULL, &pwm_debugfs_fops);

	return 0;
}
subsys_initcall(pwm_debugfs_init);
#endif /* CONFIG_DEBUG_FS */
