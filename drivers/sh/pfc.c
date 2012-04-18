/*
 * Pinmuxed GPIO support for SuperH.
 *
 * Copyright (C) 2008 Magnus Damm
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

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
#include <linux/slab.h>
#include <linux/ioport.h>

static void pfc_iounmap(struct pinmux_info *pip)
{
	int k;

	for (k = 0; k < pip->num_resources; k++)
		if (pip->window[k].virt)
			iounmap(pip->window[k].virt);

	kfree(pip->window);
	pip->window = NULL;
}

static int pfc_ioremap(struct pinmux_info *pip)
{
	struct resource *res;
	int k;

	if (!pip->num_resources)
		return 0;

	pip->window = kzalloc(pip->num_resources * sizeof(*pip->window),
			      GFP_NOWAIT);
	if (!pip->window)
		goto err1;

	for (k = 0; k < pip->num_resources; k++) {
		res = pip->resource + k;
		WARN_ON(resource_type(res) != IORESOURCE_MEM);
		pip->window[k].phys = res->start;
		pip->window[k].size = resource_size(res);
		pip->window[k].virt = ioremap_nocache(res->start,
							 resource_size(res));
		if (!pip->window[k].virt)
			goto err2;
	}

	return 0;

err2:
	pfc_iounmap(pip);
err1:
	return -1;
}

static void __iomem *pfc_phys_to_virt(struct pinmux_info *pip,
				      unsigned long address)
{
	struct pfc_window *window;
	int k;

	/* scan through physical windows and convert address */
	for (k = 0; k < pip->num_resources; k++) {
		window = pip->window + k;

		if (address < window->phys)
			continue;

		if (address >= (window->phys + window->size))
			continue;

		return window->virt + (address - window->phys);
	}

	/* no windows defined, register must be 1:1 mapped virt:phys */
	return (void __iomem *)address;
}

static int enum_in_range(pinmux_enum_t enum_id, struct pinmux_range *r)
{
	if (enum_id < r->begin)
		return 0;

	if (enum_id > r->end)
		return 0;

	return 1;
}

static unsigned long gpio_read_raw_reg(void __iomem *mapped_reg,
				       unsigned long reg_width)
{
	switch (reg_width) {
	case 8:
		return ioread8(mapped_reg);
	case 16:
		return ioread16(mapped_reg);
	case 32:
		return ioread32(mapped_reg);
	}

	BUG();
	return 0;
}

static void gpio_write_raw_reg(void __iomem *mapped_reg,
			       unsigned long reg_width,
			       unsigned long data)
{
	switch (reg_width) {
	case 8:
		iowrite8(data, mapped_reg);
		return;
	case 16:
		iowrite16(data, mapped_reg);
		return;
	case 32:
		iowrite32(data, mapped_reg);
		return;
	}

	BUG();
}

static int gpio_read_bit(struct pinmux_data_reg *dr,
			 unsigned long in_pos)
{
	unsigned long pos;

	pos = dr->reg_width - (in_pos + 1);

	pr_debug("read_bit: addr = %lx, pos = %ld, "
		 "r_width = %ld\n", dr->reg, pos, dr->reg_width);

	return (gpio_read_raw_reg(dr->mapped_reg, dr->reg_width) >> pos) & 1;
}

static void gpio_write_bit(struct pinmux_data_reg *dr,
			   unsigned long in_pos, unsigned long value)
{
	unsigned long pos;

	pos = dr->reg_width - (in_pos + 1);

	pr_debug("write_bit addr = %lx, value = %d, pos = %ld, "
		 "r_width = %ld\n",
		 dr->reg, !!value, pos, dr->reg_width);

	if (value)
		set_bit(pos, &dr->reg_shadow);
	else
		clear_bit(pos, &dr->reg_shadow);

	gpio_write_raw_reg(dr->mapped_reg, dr->reg_width, dr->reg_shadow);
}

static void config_reg_helper(struct pinmux_info *gpioc,
			      struct pinmux_cfg_reg *crp,
			      unsigned long in_pos,
			      void __iomem **mapped_regp,
			      unsigned long *maskp,
			      unsigned long *posp)
{
	int k;

	*mapped_regp = pfc_phys_to_virt(gpioc, crp->reg);

	if (crp->field_width) {
		*maskp = (1 << crp->field_width) - 1;
		*posp = crp->reg_width - ((in_pos + 1) * crp->field_width);
	} else {
		*maskp = (1 << crp->var_field_width[in_pos]) - 1;
		*posp = crp->reg_width;
		for (k = 0; k <= in_pos; k++)
			*posp -= crp->var_field_width[k];
	}
}

static int read_config_reg(struct pinmux_info *gpioc,
			   struct pinmux_cfg_reg *crp,
			   unsigned long field)
{
	void __iomem *mapped_reg;
	unsigned long mask, pos;

	config_reg_helper(gpioc, crp, field, &mapped_reg, &mask, &pos);

	pr_debug("read_reg: addr = %lx, field = %ld, "
		 "r_width = %ld, f_width = %ld\n",
		 crp->reg, field, crp->reg_width, crp->field_width);

	return (gpio_read_raw_reg(mapped_reg, crp->reg_width) >> pos) & mask;
}

static void write_config_reg(struct pinmux_info *gpioc,
			     struct pinmux_cfg_reg *crp,
			     unsigned long field, unsigned long value)
{
	void __iomem *mapped_reg;
	unsigned long mask, pos, data;

	config_reg_helper(gpioc, crp, field, &mapped_reg, &mask, &pos);

	pr_debug("write_reg addr = %lx, value = %ld, field = %ld, "
		 "r_width = %ld, f_width = %ld\n",
		 crp->reg, value, field, crp->reg_width, crp->field_width);

	mask = ~(mask << pos);
	value = value << pos;

	data = gpio_read_raw_reg(mapped_reg, crp->reg_width);
	data &= mask;
	data |= value;

	if (gpioc->unlock_reg)
		gpio_write_raw_reg(pfc_phys_to_virt(gpioc, gpioc->unlock_reg),
				   32, ~data);

	gpio_write_raw_reg(mapped_reg, crp->reg_width, data);
}

static int setup_data_reg(struct pinmux_info *gpioc, unsigned gpio)
{
	struct pinmux_gpio *gpiop = &gpioc->gpios[gpio];
	struct pinmux_data_reg *data_reg;
	int k, n;

	if (!enum_in_range(gpiop->enum_id, &gpioc->data))
		return -1;

	k = 0;
	while (1) {
		data_reg = gpioc->data_regs + k;

		if (!data_reg->reg_width)
			break;

		data_reg->mapped_reg = pfc_phys_to_virt(gpioc, data_reg->reg);

		for (n = 0; n < data_reg->reg_width; n++) {
			if (data_reg->enum_ids[n] == gpiop->enum_id) {
				gpiop->flags &= ~PINMUX_FLAG_DREG;
				gpiop->flags |= (k << PINMUX_FLAG_DREG_SHIFT);
				gpiop->flags &= ~PINMUX_FLAG_DBIT;
				gpiop->flags |= (n << PINMUX_FLAG_DBIT_SHIFT);
				return 0;
			}
		}
		k++;
	}

	BUG();

	return -1;
}

static void setup_data_regs(struct pinmux_info *gpioc)
{
	struct pinmux_data_reg *drp;
	int k;

	for (k = gpioc->first_gpio; k <= gpioc->last_gpio; k++)
		setup_data_reg(gpioc, k);

	k = 0;
	while (1) {
		drp = gpioc->data_regs + k;

		if (!drp->reg_width)
			break;

		drp->reg_shadow = gpio_read_raw_reg(drp->mapped_reg,
						    drp->reg_width);
		k++;
	}
}

static int get_data_reg(struct pinmux_info *gpioc, unsigned gpio,
			struct pinmux_data_reg **drp, int *bitp)
{
	struct pinmux_gpio *gpiop = &gpioc->gpios[gpio];
	int k, n;

	if (!enum_in_range(gpiop->enum_id, &gpioc->data))
		return -1;

	k = (gpiop->flags & PINMUX_FLAG_DREG) >> PINMUX_FLAG_DREG_SHIFT;
	n = (gpiop->flags & PINMUX_FLAG_DBIT) >> PINMUX_FLAG_DBIT_SHIFT;
	*drp = gpioc->data_regs + k;
	*bitp = n;
	return 0;
}

static int get_config_reg(struct pinmux_info *gpioc, pinmux_enum_t enum_id,
			  struct pinmux_cfg_reg **crp,
			  int *fieldp, int *valuep,
			  unsigned long **cntp)
{
	struct pinmux_cfg_reg *config_reg;
	unsigned long r_width, f_width, curr_width, ncomb;
	int k, m, n, pos, bit_pos;

	k = 0;
	while (1) {
		config_reg = gpioc->cfg_regs + k;

		r_width = config_reg->reg_width;
		f_width = config_reg->field_width;

		if (!r_width)
			break;

		pos = 0;
		m = 0;
		for (bit_pos = 0; bit_pos < r_width; bit_pos += curr_width) {
			if (f_width)
				curr_width = f_width;
			else
				curr_width = config_reg->var_field_width[m];

			ncomb = 1 << curr_width;
			for (n = 0; n < ncomb; n++) {
				if (config_reg->enum_ids[pos + n] == enum_id) {
					*crp = config_reg;
					*fieldp = m;
					*valuep = n;
					*cntp = &config_reg->cnt[m];
					return 0;
				}
			}
			pos += ncomb;
			m++;
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

enum { GPIO_CFG_DRYRUN, GPIO_CFG_REQ, GPIO_CFG_FREE };

static int pinmux_config_gpio(struct pinmux_info *gpioc, unsigned gpio,
			      int pinmux_type, int cfg_mode)
{
	struct pinmux_cfg_reg *cr = NULL;
	pinmux_enum_t enum_id;
	struct pinmux_range *range;
	int in_range, pos, field, value;
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
	field = 0;
	value = 0;
	while (1) {
		pos = get_gpio_enum_id(gpioc, gpio, pos, &enum_id);
		if (pos <= 0)
			goto out_err;

		if (!enum_id)
			break;

		/* first check if this is a function enum */
		in_range = enum_in_range(enum_id, &gpioc->function);
		if (!in_range) {
			/* not a function enum */
			if (range) {
				/*
				 * other range exists, so this pin is
				 * a regular GPIO pin that now is being
				 * bound to a specific direction.
				 *
				 * for this case we only allow function enums
				 * and the enums that match the other range.
				 */
				in_range = enum_in_range(enum_id, range);

				/*
				 * special case pass through for fixed
				 * input-only or output-only pins without
				 * function enum register association.
				 */
				if (in_range && enum_id == range->force)
					continue;
			} else {
				/*
				 * no other range exists, so this pin
				 * must then be of the function type.
				 *
				 * allow function type pins to select
				 * any combination of function/in/out
				 * in their MARK lists.
				 */
				in_range = 1;
			}
		}

		if (!in_range)
			continue;

		if (get_config_reg(gpioc, enum_id, &cr,
				   &field, &value, &cntp) != 0)
			goto out_err;

		switch (cfg_mode) {
		case GPIO_CFG_DRYRUN:
			if (!*cntp ||
			    (read_config_reg(gpioc, cr, field) != value))
				continue;
			break;

		case GPIO_CFG_REQ:
			write_config_reg(gpioc, cr, field, value);
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

static struct pinmux_info *chip_to_pinmux(struct gpio_chip *chip)
{
	return container_of(chip, struct pinmux_info, chip);
}

static int sh_gpio_request(struct gpio_chip *chip, unsigned offset)
{
	struct pinmux_info *gpioc = chip_to_pinmux(chip);
	struct pinmux_data_reg *dummy;
	unsigned long flags;
	int i, ret, pinmux_type;

	ret = -EINVAL;

	if (!gpioc)
		goto err_out;

	spin_lock_irqsave(&gpio_lock, flags);

	if ((gpioc->gpios[offset].flags & PINMUX_FLAG_TYPE) != PINMUX_TYPE_NONE)
		goto err_unlock;

	/* setup pin function here if no data is associated with pin */

	if (get_data_reg(gpioc, offset, &dummy, &i) != 0)
		pinmux_type = PINMUX_TYPE_FUNCTION;
	else
		pinmux_type = PINMUX_TYPE_GPIO;

	if (pinmux_type == PINMUX_TYPE_FUNCTION) {
		if (pinmux_config_gpio(gpioc, offset,
				       pinmux_type,
				       GPIO_CFG_DRYRUN) != 0)
			goto err_unlock;

		if (pinmux_config_gpio(gpioc, offset,
				       pinmux_type,
				       GPIO_CFG_REQ) != 0)
			BUG();
	}

	gpioc->gpios[offset].flags &= ~PINMUX_FLAG_TYPE;
	gpioc->gpios[offset].flags |= pinmux_type;

	ret = 0;
 err_unlock:
	spin_unlock_irqrestore(&gpio_lock, flags);
 err_out:
	return ret;
}

static void sh_gpio_free(struct gpio_chip *chip, unsigned offset)
{
	struct pinmux_info *gpioc = chip_to_pinmux(chip);
	unsigned long flags;
	int pinmux_type;

	if (!gpioc)
		return;

	spin_lock_irqsave(&gpio_lock, flags);

	pinmux_type = gpioc->gpios[offset].flags & PINMUX_FLAG_TYPE;
	pinmux_config_gpio(gpioc, offset, pinmux_type, GPIO_CFG_FREE);
	gpioc->gpios[offset].flags &= ~PINMUX_FLAG_TYPE;
	gpioc->gpios[offset].flags |= PINMUX_TYPE_NONE;

	spin_unlock_irqrestore(&gpio_lock, flags);
}

static int pinmux_direction(struct pinmux_info *gpioc,
			    unsigned gpio, int new_pinmux_type)
{
	int pinmux_type;
	int ret = -EINVAL;

	if (!gpioc)
		goto err_out;

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

	gpioc->gpios[gpio].flags &= ~PINMUX_FLAG_TYPE;
	gpioc->gpios[gpio].flags |= new_pinmux_type;

	ret = 0;
 err_out:
	return ret;
}

static int sh_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	struct pinmux_info *gpioc = chip_to_pinmux(chip);
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&gpio_lock, flags);
	ret = pinmux_direction(gpioc, offset, PINMUX_TYPE_INPUT);
	spin_unlock_irqrestore(&gpio_lock, flags);

	return ret;
}

static void sh_gpio_set_value(struct pinmux_info *gpioc,
			     unsigned gpio, int value)
{
	struct pinmux_data_reg *dr = NULL;
	int bit = 0;

	if (!gpioc || get_data_reg(gpioc, gpio, &dr, &bit) != 0)
		BUG();
	else
		gpio_write_bit(dr, bit, value);
}

static int sh_gpio_direction_output(struct gpio_chip *chip, unsigned offset,
				    int value)
{
	struct pinmux_info *gpioc = chip_to_pinmux(chip);
	unsigned long flags;
	int ret;

	sh_gpio_set_value(gpioc, offset, value);
	spin_lock_irqsave(&gpio_lock, flags);
	ret = pinmux_direction(gpioc, offset, PINMUX_TYPE_OUTPUT);
	spin_unlock_irqrestore(&gpio_lock, flags);

	return ret;
}

static int sh_gpio_get_value(struct pinmux_info *gpioc, unsigned gpio)
{
	struct pinmux_data_reg *dr = NULL;
	int bit = 0;

	if (!gpioc || get_data_reg(gpioc, gpio, &dr, &bit) != 0)
		return -EINVAL;

	return gpio_read_bit(dr, bit);
}

static int sh_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	return sh_gpio_get_value(chip_to_pinmux(chip), offset);
}

static void sh_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	sh_gpio_set_value(chip_to_pinmux(chip), offset, value);
}

static int sh_gpio_to_irq(struct gpio_chip *chip, unsigned offset)
{
	struct pinmux_info *gpioc = chip_to_pinmux(chip);
	pinmux_enum_t enum_id;
	pinmux_enum_t *enum_ids;
	int i, k, pos;

	pos = 0;
	enum_id = 0;
	while (1) {
		pos = get_gpio_enum_id(gpioc, offset, pos, &enum_id);
		if (pos <= 0 || !enum_id)
			break;

		for (i = 0; i < gpioc->gpio_irq_size; i++) {
			enum_ids = gpioc->gpio_irq[i].enum_ids;
			for (k = 0; enum_ids[k]; k++) {
				if (enum_ids[k] == enum_id)
					return gpioc->gpio_irq[i].irq;
			}
		}
	}

	return -ENOSYS;
}

int register_pinmux(struct pinmux_info *pip)
{
	struct gpio_chip *chip = &pip->chip;
	int ret;

	pr_info("%s handling gpio %d -> %d\n",
		pip->name, pip->first_gpio, pip->last_gpio);

	ret = pfc_ioremap(pip);
	if (ret < 0)
		return ret;

	setup_data_regs(pip);

	chip->request = sh_gpio_request;
	chip->free = sh_gpio_free;
	chip->direction_input = sh_gpio_direction_input;
	chip->get = sh_gpio_get;
	chip->direction_output = sh_gpio_direction_output;
	chip->set = sh_gpio_set;
	chip->to_irq = sh_gpio_to_irq;

	WARN_ON(pip->first_gpio != 0); /* needs testing */

	chip->label = pip->name;
	chip->owner = THIS_MODULE;
	chip->base = pip->first_gpio;
	chip->ngpio = (pip->last_gpio - pip->first_gpio) + 1;

	ret = gpiochip_add(chip);
	if (ret < 0)
		pfc_iounmap(pip);

	return ret;
}

int unregister_pinmux(struct pinmux_info *pip)
{
	pr_info("%s deregistering\n", pip->name);
	pfc_iounmap(pip);
	return gpiochip_remove(&pip->chip);
}
