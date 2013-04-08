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

#define DRV_NAME "sh-pfc"

#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pinctrl/machine.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "core.h"

static int sh_pfc_ioremap(struct sh_pfc *pfc, struct platform_device *pdev)
{
	struct resource *res;
	int k;

	if (pdev->num_resources == 0)
		return -EINVAL;

	pfc->window = devm_kzalloc(pfc->dev, pdev->num_resources *
				   sizeof(*pfc->window), GFP_NOWAIT);
	if (!pfc->window)
		return -ENOMEM;

	pfc->num_windows = pdev->num_resources;

	for (k = 0, res = pdev->resource; k < pdev->num_resources; k++, res++) {
		WARN_ON(resource_type(res) != IORESOURCE_MEM);
		pfc->window[k].phys = res->start;
		pfc->window[k].size = resource_size(res);
		pfc->window[k].virt = devm_ioremap_nocache(pfc->dev, res->start,
							   resource_size(res));
		if (!pfc->window[k].virt)
			return -ENOMEM;
	}

	return 0;
}

static void __iomem *sh_pfc_phys_to_virt(struct sh_pfc *pfc,
					 unsigned long address)
{
	struct sh_pfc_window *window;
	unsigned int i;

	/* scan through physical windows and convert address */
	for (i = 0; i < pfc->num_windows; i++) {
		window = pfc->window + i;

		if (address < window->phys)
			continue;

		if (address >= (window->phys + window->size))
			continue;

		return window->virt + (address - window->phys);
	}

	BUG();
	return NULL;
}

int sh_pfc_get_pin_index(struct sh_pfc *pfc, unsigned int pin)
{
	unsigned int offset;
	unsigned int i;

	if (pfc->info->ranges == NULL)
		return pin;

	for (i = 0, offset = 0; i < pfc->info->nr_ranges; ++i) {
		const struct pinmux_range *range = &pfc->info->ranges[i];

		if (pin <= range->end)
			return pin >= range->begin
			     ? offset + pin - range->begin : -1;

		offset += range->end - range->begin + 1;
	}

	return -EINVAL;
}

static int sh_pfc_enum_in_range(pinmux_enum_t enum_id,
				const struct pinmux_range *r)
{
	if (enum_id < r->begin)
		return 0;

	if (enum_id > r->end)
		return 0;

	return 1;
}

unsigned long sh_pfc_read_raw_reg(void __iomem *mapped_reg,
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

void sh_pfc_write_raw_reg(void __iomem *mapped_reg, unsigned long reg_width,
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

static void sh_pfc_config_reg_helper(struct sh_pfc *pfc,
				     const struct pinmux_cfg_reg *crp,
				     unsigned long in_pos,
				     void __iomem **mapped_regp,
				     unsigned long *maskp,
				     unsigned long *posp)
{
	int k;

	*mapped_regp = sh_pfc_phys_to_virt(pfc, crp->reg);

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

static void sh_pfc_write_config_reg(struct sh_pfc *pfc,
				    const struct pinmux_cfg_reg *crp,
				    unsigned long field, unsigned long value)
{
	void __iomem *mapped_reg;
	unsigned long mask, pos, data;

	sh_pfc_config_reg_helper(pfc, crp, field, &mapped_reg, &mask, &pos);

	dev_dbg(pfc->dev, "write_reg addr = %lx, value = %ld, field = %ld, "
		"r_width = %ld, f_width = %ld\n",
		crp->reg, value, field, crp->reg_width, crp->field_width);

	mask = ~(mask << pos);
	value = value << pos;

	data = sh_pfc_read_raw_reg(mapped_reg, crp->reg_width);
	data &= mask;
	data |= value;

	if (pfc->info->unlock_reg)
		sh_pfc_write_raw_reg(
			sh_pfc_phys_to_virt(pfc, pfc->info->unlock_reg), 32,
			~data);

	sh_pfc_write_raw_reg(mapped_reg, crp->reg_width, data);
}

static int sh_pfc_get_config_reg(struct sh_pfc *pfc, pinmux_enum_t enum_id,
				 const struct pinmux_cfg_reg **crp, int *fieldp,
				 int *valuep)
{
	const struct pinmux_cfg_reg *config_reg;
	unsigned long r_width, f_width, curr_width, ncomb;
	int k, m, n, pos, bit_pos;

	k = 0;
	while (1) {
		config_reg = pfc->info->cfg_regs + k;

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
					return 0;
				}
			}
			pos += ncomb;
			m++;
		}
		k++;
	}

	return -EINVAL;
}

static int sh_pfc_mark_to_enum(struct sh_pfc *pfc, pinmux_enum_t mark, int pos,
			      pinmux_enum_t *enum_idp)
{
	const pinmux_enum_t *data = pfc->info->gpio_data;
	int k;

	if (pos) {
		*enum_idp = data[pos + 1];
		return pos + 1;
	}

	for (k = 0; k < pfc->info->gpio_data_size; k++) {
		if (data[k] == mark) {
			*enum_idp = data[k + 1];
			return k + 1;
		}
	}

	dev_err(pfc->dev, "cannot locate data/mark enum_id for mark %d\n",
		mark);
	return -EINVAL;
}

int sh_pfc_config_mux(struct sh_pfc *pfc, unsigned mark, int pinmux_type)
{
	const struct pinmux_cfg_reg *cr = NULL;
	pinmux_enum_t enum_id;
	const struct pinmux_range *range;
	int in_range, pos, field, value;
	int ret;

	switch (pinmux_type) {
	case PINMUX_TYPE_GPIO:
	case PINMUX_TYPE_FUNCTION:
		range = NULL;
		break;

	case PINMUX_TYPE_OUTPUT:
		range = &pfc->info->output;
		break;

	case PINMUX_TYPE_INPUT:
		range = &pfc->info->input;
		break;

	case PINMUX_TYPE_INPUT_PULLUP:
		range = &pfc->info->input_pu;
		break;

	case PINMUX_TYPE_INPUT_PULLDOWN:
		range = &pfc->info->input_pd;
		break;

	default:
		return -EINVAL;
	}

	pos = 0;
	enum_id = 0;
	field = 0;
	value = 0;

	/* Iterate over all the configuration fields we need to update. */
	while (1) {
		pos = sh_pfc_mark_to_enum(pfc, mark, pos, &enum_id);
		if (pos < 0)
			return pos;

		if (!enum_id)
			break;

		/* Check if the configuration field selects a function. If it
		 * doesn't, skip the field if it's not applicable to the
		 * requested pinmux type.
		 */
		in_range = sh_pfc_enum_in_range(enum_id, &pfc->info->function);
		if (!in_range) {
			if (pinmux_type == PINMUX_TYPE_FUNCTION) {
				/* Functions are allowed to modify all
				 * fields.
				 */
				in_range = 1;
			} else if (pinmux_type != PINMUX_TYPE_GPIO) {
				/* Input/output types can only modify fields
				 * that correspond to their respective ranges.
				 */
				in_range = sh_pfc_enum_in_range(enum_id, range);

				/*
				 * special case pass through for fixed
				 * input-only or output-only pins without
				 * function enum register association.
				 */
				if (in_range && enum_id == range->force)
					continue;
			}
			/* GPIOs are only allowed to modify function fields. */
		}

		if (!in_range)
			continue;

		ret = sh_pfc_get_config_reg(pfc, enum_id, &cr, &field, &value);
		if (ret < 0)
			return ret;

		sh_pfc_write_config_reg(pfc, cr, field, value);
	}

	return 0;
}

static int sh_pfc_probe(struct platform_device *pdev)
{
	const struct sh_pfc_soc_info *info;
	struct sh_pfc *pfc;
	int ret;

	info = pdev->id_entry->driver_data
	      ? (void *)pdev->id_entry->driver_data : pdev->dev.platform_data;
	if (info == NULL)
		return -ENODEV;

	pfc = devm_kzalloc(&pdev->dev, sizeof(*pfc), GFP_KERNEL);
	if (pfc == NULL)
		return -ENOMEM;

	pfc->info = info;
	pfc->dev = &pdev->dev;

	ret = sh_pfc_ioremap(pfc, pdev);
	if (unlikely(ret < 0))
		return ret;

	spin_lock_init(&pfc->lock);

	pinctrl_provide_dummies();

	/*
	 * Initialize pinctrl bindings first
	 */
	ret = sh_pfc_register_pinctrl(pfc);
	if (unlikely(ret != 0))
		return ret;

#ifdef CONFIG_GPIO_SH_PFC
	/*
	 * Then the GPIO chip
	 */
	ret = sh_pfc_register_gpiochip(pfc);
	if (unlikely(ret != 0)) {
		/*
		 * If the GPIO chip fails to come up we still leave the
		 * PFC state as it is, given that there are already
		 * extant users of it that have succeeded by this point.
		 */
		dev_notice(pfc->dev, "failed to init GPIO chip, ignoring...\n");
	}
#endif

	platform_set_drvdata(pdev, pfc);

	dev_info(pfc->dev, "%s support registered\n", info->name);

	return 0;
}

static int sh_pfc_remove(struct platform_device *pdev)
{
	struct sh_pfc *pfc = platform_get_drvdata(pdev);

#ifdef CONFIG_GPIO_SH_PFC
	sh_pfc_unregister_gpiochip(pfc);
#endif
	sh_pfc_unregister_pinctrl(pfc);

	platform_set_drvdata(pdev, NULL);

	return 0;
}

static const struct platform_device_id sh_pfc_id_table[] = {
#ifdef CONFIG_PINCTRL_PFC_R8A73A4
	{ "pfc-r8a73a4", (kernel_ulong_t)&r8a73a4_pinmux_info },
#endif
#ifdef CONFIG_PINCTRL_PFC_R8A7740
	{ "pfc-r8a7740", (kernel_ulong_t)&r8a7740_pinmux_info },
#endif
#ifdef CONFIG_PINCTRL_PFC_R8A7779
	{ "pfc-r8a7779", (kernel_ulong_t)&r8a7779_pinmux_info },
#endif
#ifdef CONFIG_PINCTRL_PFC_R8A7790
	{ "pfc-r8a7790", (kernel_ulong_t)&r8a7790_pinmux_info },
#endif
#ifdef CONFIG_PINCTRL_PFC_SH7203
	{ "pfc-sh7203", (kernel_ulong_t)&sh7203_pinmux_info },
#endif
#ifdef CONFIG_PINCTRL_PFC_SH7264
	{ "pfc-sh7264", (kernel_ulong_t)&sh7264_pinmux_info },
#endif
#ifdef CONFIG_PINCTRL_PFC_SH7269
	{ "pfc-sh7269", (kernel_ulong_t)&sh7269_pinmux_info },
#endif
#ifdef CONFIG_PINCTRL_PFC_SH7372
	{ "pfc-sh7372", (kernel_ulong_t)&sh7372_pinmux_info },
#endif
#ifdef CONFIG_PINCTRL_PFC_SH73A0
	{ "pfc-sh73a0", (kernel_ulong_t)&sh73a0_pinmux_info },
#endif
#ifdef CONFIG_PINCTRL_PFC_SH7720
	{ "pfc-sh7720", (kernel_ulong_t)&sh7720_pinmux_info },
#endif
#ifdef CONFIG_PINCTRL_PFC_SH7722
	{ "pfc-sh7722", (kernel_ulong_t)&sh7722_pinmux_info },
#endif
#ifdef CONFIG_PINCTRL_PFC_SH7723
	{ "pfc-sh7723", (kernel_ulong_t)&sh7723_pinmux_info },
#endif
#ifdef CONFIG_PINCTRL_PFC_SH7724
	{ "pfc-sh7724", (kernel_ulong_t)&sh7724_pinmux_info },
#endif
#ifdef CONFIG_PINCTRL_PFC_SH7734
	{ "pfc-sh7734", (kernel_ulong_t)&sh7734_pinmux_info },
#endif
#ifdef CONFIG_PINCTRL_PFC_SH7757
	{ "pfc-sh7757", (kernel_ulong_t)&sh7757_pinmux_info },
#endif
#ifdef CONFIG_PINCTRL_PFC_SH7785
	{ "pfc-sh7785", (kernel_ulong_t)&sh7785_pinmux_info },
#endif
#ifdef CONFIG_PINCTRL_PFC_SH7786
	{ "pfc-sh7786", (kernel_ulong_t)&sh7786_pinmux_info },
#endif
#ifdef CONFIG_PINCTRL_PFC_SHX3
	{ "pfc-shx3", (kernel_ulong_t)&shx3_pinmux_info },
#endif
	{ "sh-pfc", 0 },
	{ },
};
MODULE_DEVICE_TABLE(platform, sh_pfc_id_table);

static struct platform_driver sh_pfc_driver = {
	.probe		= sh_pfc_probe,
	.remove		= sh_pfc_remove,
	.id_table	= sh_pfc_id_table,
	.driver		= {
		.name	= DRV_NAME,
		.owner	= THIS_MODULE,
	},
};

static int __init sh_pfc_init(void)
{
	return platform_driver_register(&sh_pfc_driver);
}
postcore_initcall(sh_pfc_init);

static void __exit sh_pfc_exit(void)
{
	platform_driver_unregister(&sh_pfc_driver);
}
module_exit(sh_pfc_exit);

MODULE_AUTHOR("Magnus Damm, Paul Mundt, Laurent Pinchart");
MODULE_DESCRIPTION("Pin Control and GPIO driver for SuperH pin function controller");
MODULE_LICENSE("GPL v2");
