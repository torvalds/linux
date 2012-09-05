/*
 * SuperH Pin Function Controller support.
 *
 * Copyright (C) 2008 Magnus Damm
 * Copyright (C) 2009 - 2012 Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#define pr_fmt(fmt) "sh_pfc " KBUILD_MODNAME ": " fmt

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/sh_pfc.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/bitops.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/pinctrl/machine.h>

static struct sh_pfc *sh_pfc __read_mostly;

static inline bool sh_pfc_initialized(void)
{
	return !!sh_pfc;
}

static void pfc_iounmap(struct sh_pfc *pfc)
{
	int k;

	for (k = 0; k < pfc->num_resources; k++)
		if (pfc->window[k].virt)
			iounmap(pfc->window[k].virt);

	kfree(pfc->window);
	pfc->window = NULL;
}

static int pfc_ioremap(struct sh_pfc *pfc)
{
	struct resource *res;
	int k;

	if (!pfc->num_resources)
		return 0;

	pfc->window = kzalloc(pfc->num_resources * sizeof(*pfc->window),
			      GFP_NOWAIT);
	if (!pfc->window)
		goto err1;

	for (k = 0; k < pfc->num_resources; k++) {
		res = pfc->resource + k;
		WARN_ON(resource_type(res) != IORESOURCE_MEM);
		pfc->window[k].phys = res->start;
		pfc->window[k].size = resource_size(res);
		pfc->window[k].virt = ioremap_nocache(res->start,
							 resource_size(res));
		if (!pfc->window[k].virt)
			goto err2;
	}

	return 0;

err2:
	pfc_iounmap(pfc);
err1:
	return -1;
}

static void __iomem *pfc_phys_to_virt(struct sh_pfc *pfc,
				      unsigned long address)
{
	struct pfc_window *window;
	int k;

	/* scan through physical windows and convert address */
	for (k = 0; k < pfc->num_resources; k++) {
		window = pfc->window + k;

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

int sh_pfc_read_bit(struct pinmux_data_reg *dr, unsigned long in_pos)
{
	unsigned long pos;

	pos = dr->reg_width - (in_pos + 1);

	pr_debug("read_bit: addr = %lx, pos = %ld, "
		 "r_width = %ld\n", dr->reg, pos, dr->reg_width);

	return (gpio_read_raw_reg(dr->mapped_reg, dr->reg_width) >> pos) & 1;
}
EXPORT_SYMBOL_GPL(sh_pfc_read_bit);

void sh_pfc_write_bit(struct pinmux_data_reg *dr, unsigned long in_pos,
		      unsigned long value)
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
EXPORT_SYMBOL_GPL(sh_pfc_write_bit);

static void config_reg_helper(struct sh_pfc *pfc,
			      struct pinmux_cfg_reg *crp,
			      unsigned long in_pos,
			      void __iomem **mapped_regp,
			      unsigned long *maskp,
			      unsigned long *posp)
{
	int k;

	*mapped_regp = pfc_phys_to_virt(pfc, crp->reg);

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

static int read_config_reg(struct sh_pfc *pfc,
			   struct pinmux_cfg_reg *crp,
			   unsigned long field)
{
	void __iomem *mapped_reg;
	unsigned long mask, pos;

	config_reg_helper(pfc, crp, field, &mapped_reg, &mask, &pos);

	pr_debug("read_reg: addr = %lx, field = %ld, "
		 "r_width = %ld, f_width = %ld\n",
		 crp->reg, field, crp->reg_width, crp->field_width);

	return (gpio_read_raw_reg(mapped_reg, crp->reg_width) >> pos) & mask;
}

static void write_config_reg(struct sh_pfc *pfc,
			     struct pinmux_cfg_reg *crp,
			     unsigned long field, unsigned long value)
{
	void __iomem *mapped_reg;
	unsigned long mask, pos, data;

	config_reg_helper(pfc, crp, field, &mapped_reg, &mask, &pos);

	pr_debug("write_reg addr = %lx, value = %ld, field = %ld, "
		 "r_width = %ld, f_width = %ld\n",
		 crp->reg, value, field, crp->reg_width, crp->field_width);

	mask = ~(mask << pos);
	value = value << pos;

	data = gpio_read_raw_reg(mapped_reg, crp->reg_width);
	data &= mask;
	data |= value;

	if (pfc->unlock_reg)
		gpio_write_raw_reg(pfc_phys_to_virt(pfc, pfc->unlock_reg),
				   32, ~data);

	gpio_write_raw_reg(mapped_reg, crp->reg_width, data);
}

static int setup_data_reg(struct sh_pfc *pfc, unsigned gpio)
{
	struct pinmux_gpio *gpiop = &pfc->gpios[gpio];
	struct pinmux_data_reg *data_reg;
	int k, n;

	if (!enum_in_range(gpiop->enum_id, &pfc->data))
		return -1;

	k = 0;
	while (1) {
		data_reg = pfc->data_regs + k;

		if (!data_reg->reg_width)
			break;

		data_reg->mapped_reg = pfc_phys_to_virt(pfc, data_reg->reg);

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

static void setup_data_regs(struct sh_pfc *pfc)
{
	struct pinmux_data_reg *drp;
	int k;

	for (k = pfc->first_gpio; k <= pfc->last_gpio; k++)
		setup_data_reg(pfc, k);

	k = 0;
	while (1) {
		drp = pfc->data_regs + k;

		if (!drp->reg_width)
			break;

		drp->reg_shadow = gpio_read_raw_reg(drp->mapped_reg,
						    drp->reg_width);
		k++;
	}
}

int sh_pfc_get_data_reg(struct sh_pfc *pfc, unsigned gpio,
			struct pinmux_data_reg **drp, int *bitp)
{
	struct pinmux_gpio *gpiop = &pfc->gpios[gpio];
	int k, n;

	if (!enum_in_range(gpiop->enum_id, &pfc->data))
		return -1;

	k = (gpiop->flags & PINMUX_FLAG_DREG) >> PINMUX_FLAG_DREG_SHIFT;
	n = (gpiop->flags & PINMUX_FLAG_DBIT) >> PINMUX_FLAG_DBIT_SHIFT;
	*drp = pfc->data_regs + k;
	*bitp = n;
	return 0;
}
EXPORT_SYMBOL_GPL(sh_pfc_get_data_reg);

static int get_config_reg(struct sh_pfc *pfc, pinmux_enum_t enum_id,
			  struct pinmux_cfg_reg **crp,
			  int *fieldp, int *valuep,
			  unsigned long **cntp)
{
	struct pinmux_cfg_reg *config_reg;
	unsigned long r_width, f_width, curr_width, ncomb;
	int k, m, n, pos, bit_pos;

	k = 0;
	while (1) {
		config_reg = pfc->cfg_regs + k;

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

int sh_pfc_gpio_to_enum(struct sh_pfc *pfc, unsigned gpio, int pos,
			pinmux_enum_t *enum_idp)
{
	pinmux_enum_t enum_id = pfc->gpios[gpio].enum_id;
	pinmux_enum_t *data = pfc->gpio_data;
	int k;

	if (!enum_in_range(enum_id, &pfc->data)) {
		if (!enum_in_range(enum_id, &pfc->mark)) {
			pr_err("non data/mark enum_id for gpio %d\n", gpio);
			return -1;
		}
	}

	if (pos) {
		*enum_idp = data[pos + 1];
		return pos + 1;
	}

	for (k = 0; k < pfc->gpio_data_size; k++) {
		if (data[k] == enum_id) {
			*enum_idp = data[k + 1];
			return k + 1;
		}
	}

	pr_err("cannot locate data/mark enum_id for gpio %d\n", gpio);
	return -1;
}
EXPORT_SYMBOL_GPL(sh_pfc_gpio_to_enum);

int sh_pfc_config_gpio(struct sh_pfc *pfc, unsigned gpio, int pinmux_type,
		       int cfg_mode)
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
		range = &pfc->output;
		break;

	case PINMUX_TYPE_INPUT:
		range = &pfc->input;
		break;

	case PINMUX_TYPE_INPUT_PULLUP:
		range = &pfc->input_pu;
		break;

	case PINMUX_TYPE_INPUT_PULLDOWN:
		range = &pfc->input_pd;
		break;

	default:
		goto out_err;
	}

	pos = 0;
	enum_id = 0;
	field = 0;
	value = 0;
	while (1) {
		pos = sh_pfc_gpio_to_enum(pfc, gpio, pos, &enum_id);
		if (pos <= 0)
			goto out_err;

		if (!enum_id)
			break;

		/* first check if this is a function enum */
		in_range = enum_in_range(enum_id, &pfc->function);
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

		if (get_config_reg(pfc, enum_id, &cr,
				   &field, &value, &cntp) != 0)
			goto out_err;

		switch (cfg_mode) {
		case GPIO_CFG_DRYRUN:
			if (!*cntp ||
			    (read_config_reg(pfc, cr, field) != value))
				continue;
			break;

		case GPIO_CFG_REQ:
			write_config_reg(pfc, cr, field, value);
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
EXPORT_SYMBOL_GPL(sh_pfc_config_gpio);

int register_sh_pfc(struct sh_pfc *pfc)
{
	int (*initroutine)(struct sh_pfc *) = NULL;
	int ret;

	/*
	 * Ensure that the type encoding fits
	 */
	BUILD_BUG_ON(PINMUX_FLAG_TYPE > ((1 << PINMUX_FLAG_DBIT_SHIFT) - 1));

	if (sh_pfc)
		return -EBUSY;

	ret = pfc_ioremap(pfc);
	if (unlikely(ret < 0))
		return ret;

	spin_lock_init(&pfc->lock);

	pinctrl_provide_dummies();
	setup_data_regs(pfc);

	sh_pfc = pfc;

	/*
	 * Initialize pinctrl bindings first
	 */
	initroutine = symbol_request(sh_pfc_register_pinctrl);
	if (initroutine) {
		ret = (*initroutine)(pfc);
		symbol_put_addr(initroutine);

		if (unlikely(ret != 0))
			goto err;
	} else {
		pr_err("failed to initialize pinctrl bindings\n");
		goto err;
	}

	/*
	 * Then the GPIO chip
	 */
	initroutine = symbol_request(sh_pfc_register_gpiochip);
	if (initroutine) {
		ret = (*initroutine)(pfc);
		symbol_put_addr(initroutine);

		/*
		 * If the GPIO chip fails to come up we still leave the
		 * PFC state as it is, given that there are already
		 * extant users of it that have succeeded by this point.
		 */
		if (unlikely(ret != 0)) {
			pr_notice("failed to init GPIO chip, ignoring...\n");
			ret = 0;
		}
	}

	pr_info("%s support registered\n", pfc->name);

	return 0;

err:
	pfc_iounmap(pfc);
	sh_pfc = NULL;

	return ret;
}
