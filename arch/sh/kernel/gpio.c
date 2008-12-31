/*
 * Pinmuxed GPIO support for SuperH.
 *
 * Copyright (C) 2008 Magnus Damm
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/bitops.h>
#include <linux/gpio.h>

static struct pinmux_info *registered_gpio;

static struct pinmux_info *gpio_controller(unsigned gpio)
{
	if (!registered_gpio)
		return NULL;

	if (gpio < registered_gpio->first_gpio)
		return NULL;

	if (gpio > registered_gpio->last_gpio)
		return NULL;

	return registered_gpio;
}

static int enum_in_range(pinmux_enum_t enum_id, struct pinmux_range *r)
{
	if (enum_id < r->begin)
		return 0;

	if (enum_id > r->end)
		return 0;

	return 1;
}

static int read_write_reg(unsigned long reg, unsigned long reg_width,
			  unsigned long field_width, unsigned long in_pos,
			  unsigned long value, int do_write)
{
	unsigned long data, mask, pos;

	data = 0;
	mask = (1 << field_width) - 1;
	pos = reg_width - ((in_pos + 1) * field_width);

#ifdef DEBUG
	pr_info("%s, addr = %lx, value = %ld, pos = %ld, "
		"r_width = %ld, f_width = %ld\n",
		do_write ? "write" : "read", reg, value, pos,
		reg_width, field_width);
#endif

	switch (reg_width) {
	case 8:
		data = ctrl_inb(reg);
		break;
	case 16:
		data = ctrl_inw(reg);
		break;
	case 32:
		data = ctrl_inl(reg);
		break;
	}

	if (!do_write)
		return (data >> pos) & mask;

	data &= ~(mask << pos);
	data |= value << pos;

	switch (reg_width) {
	case 8:
		ctrl_outb(data, reg);
		break;
	case 16:
		ctrl_outw(data, reg);
		break;
	case 32:
		ctrl_outl(data, reg);
		break;
	}
	return 0;
}

static int get_data_reg(struct pinmux_info *gpioc, unsigned gpio,
			struct pinmux_data_reg **drp, int *bitp)
{
	pinmux_enum_t enum_id = gpioc->gpios[gpio].enum_id;
	struct pinmux_data_reg *data_reg;
	int k, n;

	if (!enum_in_range(enum_id, &gpioc->data))
		return -1;

	k = 0;
	while (1) {
		data_reg = gpioc->data_regs + k;

		if (!data_reg->reg_width)
			break;

		for (n = 0; n < data_reg->reg_width; n++) {
			if (data_reg->enum_ids[n] == enum_id) {
				*drp = data_reg;
				*bitp = n;
				return 0;

			}
		}
		k++;
	}

	return -1;
}

static int get_config_reg(struct pinmux_info *gpioc, pinmux_enum_t enum_id,
			  struct pinmux_cfg_reg **crp, int *indexp,
			  unsigned long **cntp)
{
	struct pinmux_cfg_reg *config_reg;
	unsigned long r_width, f_width;
	int k, n;

	k = 0;
	while (1) {
		config_reg = gpioc->cfg_regs + k;

		r_width = config_reg->reg_width;
		f_width = config_reg->field_width;

		if (!r_width)
			break;
		for (n = 0; n < (r_width / f_width) * 1 << f_width; n++) {
			if (config_reg->enum_ids[n] == enum_id) {
				*crp = config_reg;
				*indexp = n;
				*cntp = &config_reg->cnt[n / (1 << f_width)];
				return 0;
			}
		}
		k++;
	}

	return -1;
}

static int get_gpio_enum_id(struct pinmux_info *gpioc, unsigned gpio,
			    int pos, pinmux_enum_t *enum_idp)
{
	pinmux_enum_t enum_id = gpioc->gpios[gpio].enum_id;
	pinmux_enum_t *data = gpioc->gpio_data;
	int k;

	if (!enum_in_range(enum_id, &gpioc->data)) {
		if (!enum_in_range(enum_id, &gpioc->mark)) {
			pr_err("non data/mark enum_id for gpio %d\n", gpio);
			return -1;
		}
	}

	if (pos) {
		*enum_idp = data[pos + 1];
		return pos + 1;
	}

	for (k = 0; k < gpioc->gpio_data_size; k++) {
		if (data[k] == enum_id) {
			*enum_idp = data[k + 1];
			return k + 1;
		}
	}

	pr_err("cannot locate data/mark enum_id for gpio %d\n", gpio);
	return -1;
}

static int write_config_reg(struct pinmux_info *gpioc,
			    struct pinmux_cfg_reg *crp,
			    int index)
{
	unsigned long ncomb, pos, value;

	ncomb = 1 << crp->field_width;
	pos = index / ncomb;
	value = index % ncomb;

	return read_write_reg(crp->reg, crp->reg_width,
			      crp->field_width, pos, value, 1);
}

static int check_config_reg(struct pinmux_info *gpioc,
			    struct pinmux_cfg_reg *crp,
			    int index)
{
	unsigned long ncomb, pos, value;

	ncomb = 1 << crp->field_width;
	pos = index / ncomb;
	value = index % ncomb;

	if (read_write_reg(crp->reg, crp->reg_width,
			   crp->field_width, pos, 0, 0) == value)
		return 0;

	return -1;
}

enum { GPIO_CFG_DRYRUN, GPIO_CFG_REQ, GPIO_CFG_FREE };

int pinmux_config_gpio(struct pinmux_info *gpioc, unsigned gpio,
		       int pinmux_type, int cfg_mode)
{
	struct pinmux_cfg_reg *cr = NULL;
	pinmux_enum_t enum_id;
	struct pinmux_range *range;
	int in_range, pos, index;
	unsigned long *cntp;

	switch (pinmux_type) {

	case PINMUX_TYPE_FUNCTION:
		range = NULL;
		break;

	case PINMUX_TYPE_OUTPUT:
		range = &gpioc->output;
		break;

	case PINMUX_TYPE_INPUT:
		range = &gpioc->input;
		break;

	case PINMUX_TYPE_INPUT_PULLUP:
		range = &gpioc->input_pu;
		break;

	case PINMUX_TYPE_INPUT_PULLDOWN:
		range = &gpioc->input_pd;
		break;

	default:
		goto out_err;
	}

	pos = 0;
	enum_id = 0;
	index = 0;
	while (1) {
		pos = get_gpio_enum_id(gpioc, gpio, pos, &enum_id);
		if (pos <= 0)
			goto out_err;

		if (!enum_id)
			break;

		in_range = enum_in_range(enum_id, &gpioc->function);
		if (!in_range && range) {
			in_range = enum_in_range(enum_id, range);

			if (in_range && enum_id == range->force)
				continue;
		}

		if (!in_range)
			continue;

		if (get_config_reg(gpioc, enum_id, &cr, &index, &cntp) != 0)
			goto out_err;

		switch (cfg_mode) {
		case GPIO_CFG_DRYRUN:
			if (!*cntp || !check_config_reg(gpioc, cr, index))
				continue;
			break;

		case GPIO_CFG_REQ:
			if (write_config_reg(gpioc, cr, index) != 0)
				goto out_err;
			*cntp = *cntp + 1;
			break;

		case GPIO_CFG_FREE:
			*cntp = *cntp - 1;
			break;
		}
	}

	return 0;
 out_err:
	return -1;
}

static DEFINE_SPINLOCK(gpio_lock);

int __gpio_request(unsigned gpio)
{
	struct pinmux_info *gpioc = gpio_controller(gpio);
	struct pinmux_data_reg *dummy;
	unsigned long flags;
	int i, ret, pinmux_type;

	ret = -EINVAL;

	if (!gpioc)
		goto err_out;

	spin_lock_irqsave(&gpio_lock, flags);

	if ((gpioc->gpios[gpio].flags & PINMUX_FLAG_TYPE) != PINMUX_TYPE_NONE)
		goto err_unlock;

	/* setup pin function here if no data is associated with pin */

	if (get_data_reg(gpioc, gpio, &dummy, &i) != 0)
		pinmux_type = PINMUX_TYPE_FUNCTION;
	else
		pinmux_type = PINMUX_TYPE_GPIO;

	if (pinmux_type == PINMUX_TYPE_FUNCTION) {
		if (pinmux_config_gpio(gpioc, gpio,
				       pinmux_type,
				       GPIO_CFG_DRYRUN) != 0)
			goto err_unlock;

		if (pinmux_config_gpio(gpioc, gpio,
				       pinmux_type,
				       GPIO_CFG_REQ) != 0)
			BUG();
	}

	gpioc->gpios[gpio].flags = pinmux_type;

	ret = 0;
 err_unlock:
	spin_unlock_irqrestore(&gpio_lock, flags);
 err_out:
	return ret;
}
EXPORT_SYMBOL(__gpio_request);

void gpio_free(unsigned gpio)
{
	struct pinmux_info *gpioc = gpio_controller(gpio);
	unsigned long flags;
	int pinmux_type;

	if (!gpioc)
		return;

	spin_lock_irqsave(&gpio_lock, flags);

	pinmux_type = gpioc->gpios[gpio].flags & PINMUX_FLAG_TYPE;
	pinmux_config_gpio(gpioc, gpio, pinmux_type, GPIO_CFG_FREE);
	gpioc->gpios[gpio].flags = PINMUX_TYPE_NONE;

	spin_unlock_irqrestore(&gpio_lock, flags);
}
EXPORT_SYMBOL(gpio_free);

static int pinmux_direction(struct pinmux_info *gpioc,
			    unsigned gpio, int new_pinmux_type)
{
	int ret, pinmux_type;

	ret = -EINVAL;
	pinmux_type = gpioc->gpios[gpio].flags & PINMUX_FLAG_TYPE;

	switch (pinmux_type) {
	case PINMUX_TYPE_GPIO:
		break;
	case PINMUX_TYPE_OUTPUT:
	case PINMUX_TYPE_INPUT:
	case PINMUX_TYPE_INPUT_PULLUP:
	case PINMUX_TYPE_INPUT_PULLDOWN:
		pinmux_config_gpio(gpioc, gpio, pinmux_type, GPIO_CFG_FREE);
		break;
	default:
		goto err_out;
	}

	if (pinmux_config_gpio(gpioc, gpio,
			       new_pinmux_type,
			       GPIO_CFG_DRYRUN) != 0)
		goto err_out;

	if (pinmux_config_gpio(gpioc, gpio,
			       new_pinmux_type,
			       GPIO_CFG_REQ) != 0)
		BUG();

	gpioc->gpios[gpio].flags = new_pinmux_type;

	ret = 0;
 err_out:
	return ret;
}

int gpio_direction_input(unsigned gpio)
{
	struct pinmux_info *gpioc = gpio_controller(gpio);
	unsigned long flags;
	int ret = -EINVAL;

	if (!gpioc)
		goto err_out;

	spin_lock_irqsave(&gpio_lock, flags);
	ret = pinmux_direction(gpioc, gpio, PINMUX_TYPE_INPUT);
	spin_unlock_irqrestore(&gpio_lock, flags);
 err_out:
	return ret;
}
EXPORT_SYMBOL(gpio_direction_input);

static int __gpio_get_set_value(struct pinmux_info *gpioc,
				unsigned gpio, int value,
				int do_write)
{
	struct pinmux_data_reg *dr = NULL;
	int bit = 0;

	if (get_data_reg(gpioc, gpio, &dr, &bit) != 0)
		BUG();
	else
		value = read_write_reg(dr->reg, dr->reg_width,
				       1, bit, !!value, do_write);

	return value;
}

int gpio_direction_output(unsigned gpio, int value)
{
	struct pinmux_info *gpioc = gpio_controller(gpio);
	unsigned long flags;
	int ret = -EINVAL;

	if (!gpioc)
		goto err_out;

	spin_lock_irqsave(&gpio_lock, flags);
	__gpio_get_set_value(gpioc, gpio, value, 1);
	ret = pinmux_direction(gpioc, gpio, PINMUX_TYPE_OUTPUT);
	spin_unlock_irqrestore(&gpio_lock, flags);
 err_out:
	return ret;
}
EXPORT_SYMBOL(gpio_direction_output);

int gpio_get_value(unsigned gpio)
{
	struct pinmux_info *gpioc = gpio_controller(gpio);
	unsigned long flags;
	int value = 0;

	if (!gpioc)
		BUG();
	else {
		spin_lock_irqsave(&gpio_lock, flags);
		value = __gpio_get_set_value(gpioc, gpio, 0, 0);
		spin_unlock_irqrestore(&gpio_lock, flags);
	}

	return value;
}
EXPORT_SYMBOL(gpio_get_value);

void gpio_set_value(unsigned gpio, int value)
{
	struct pinmux_info *gpioc = gpio_controller(gpio);
	unsigned long flags;

	if (!gpioc)
		BUG();
	else {
		spin_lock_irqsave(&gpio_lock, flags);
		__gpio_get_set_value(gpioc, gpio, value, 1);
		spin_unlock_irqrestore(&gpio_lock, flags);
	}
}
EXPORT_SYMBOL(gpio_set_value);

int register_pinmux(struct pinmux_info *pip)
{
	registered_gpio = pip;
	pr_info("pinmux: %s handling gpio %d -> %d\n",
		pip->name, pip->first_gpio, pip->last_gpio);

	return 0;
}
