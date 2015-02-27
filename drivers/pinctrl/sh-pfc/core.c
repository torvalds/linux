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
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pinctrl/machine.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "core.h"

static int sh_pfc_map_resources(struct sh_pfc *pfc,
				struct platform_device *pdev)
{
	unsigned int num_windows = 0;
	unsigned int num_irqs = 0;
	struct sh_pfc_window *windows;
	unsigned int *irqs = NULL;
	struct resource *res;
	unsigned int i;

	/* Count the MEM and IRQ resources. */
	for (i = 0; i < pdev->num_resources; ++i) {
		switch (resource_type(&pdev->resource[i])) {
		case IORESOURCE_MEM:
			num_windows++;
			break;

		case IORESOURCE_IRQ:
			num_irqs++;
			break;
		}
	}

	if (num_windows == 0)
		return -EINVAL;

	/* Allocate memory windows and IRQs arrays. */
	windows = devm_kzalloc(pfc->dev, num_windows * sizeof(*windows),
			       GFP_KERNEL);
	if (windows == NULL)
		return -ENOMEM;

	pfc->num_windows = num_windows;
	pfc->windows = windows;

	if (num_irqs) {
		irqs = devm_kzalloc(pfc->dev, num_irqs * sizeof(*irqs),
				    GFP_KERNEL);
		if (irqs == NULL)
			return -ENOMEM;

		pfc->num_irqs = num_irqs;
		pfc->irqs = irqs;
	}

	/* Fill them. */
	for (i = 0, res = pdev->resource; i < pdev->num_resources; i++, res++) {
		switch (resource_type(res)) {
		case IORESOURCE_MEM:
			windows->phys = res->start;
			windows->size = resource_size(res);
			windows->virt = devm_ioremap_resource(pfc->dev, res);
			if (IS_ERR(windows->virt))
				return -ENOMEM;
			windows++;
			break;

		case IORESOURCE_IRQ:
			*irqs++ = res->start;
			break;
		}
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
		window = pfc->windows + i;

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

	for (i = 0, offset = 0; i < pfc->nr_ranges; ++i) {
		const struct sh_pfc_pin_range *range = &pfc->ranges[i];

		if (pin <= range->end)
			return pin >= range->start
			     ? offset + pin - range->start : -1;

		offset += range->end - range->start + 1;
	}

	return -EINVAL;
}

static int sh_pfc_enum_in_range(u16 enum_id, const struct pinmux_range *r)
{
	if (enum_id < r->begin)
		return 0;

	if (enum_id > r->end)
		return 0;

	return 1;
}

u32 sh_pfc_read_raw_reg(void __iomem *mapped_reg, unsigned long reg_width)
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
			  u32 data)
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
				     void __iomem **mapped_regp, u32 *maskp,
				     unsigned long *posp)
{
	unsigned int k;

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
				    unsigned long field, u32 value)
{
	void __iomem *mapped_reg;
	unsigned long pos;
	u32 mask, data;

	sh_pfc_config_reg_helper(pfc, crp, field, &mapped_reg, &mask, &pos);

	dev_dbg(pfc->dev, "write_reg addr = %lx, value = 0x%x, field = %ld, "
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

static int sh_pfc_get_config_reg(struct sh_pfc *pfc, u16 enum_id,
				 const struct pinmux_cfg_reg **crp, int *fieldp,
				 u32 *valuep)
{
	const struct pinmux_cfg_reg *config_reg;
	unsigned long r_width, f_width, curr_width;
	unsigned int k, m, pos, bit_pos;
	u32 ncomb, n;

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

static int sh_pfc_mark_to_enum(struct sh_pfc *pfc, u16 mark, int pos,
			      u16 *enum_idp)
{
	const u16 *data = pfc->info->gpio_data;
	unsigned int k;

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
	u16 enum_id;
	const struct pinmux_range *range;
	int in_range, pos, field;
	u32 value;
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

static int sh_pfc_init_ranges(struct sh_pfc *pfc)
{
	struct sh_pfc_pin_range *range;
	unsigned int nr_ranges;
	unsigned int i;

	if (pfc->info->pins[0].pin == (u16)-1) {
		/* Pin number -1 denotes that the SoC doesn't report pin numbers
		 * in its pin arrays yet. Consider the pin numbers range as
		 * continuous and allocate a single range.
		 */
		pfc->nr_ranges = 1;
		pfc->ranges = devm_kzalloc(pfc->dev, sizeof(*pfc->ranges),
					   GFP_KERNEL);
		if (pfc->ranges == NULL)
			return -ENOMEM;

		pfc->ranges->start = 0;
		pfc->ranges->end = pfc->info->nr_pins - 1;
		pfc->nr_gpio_pins = pfc->info->nr_pins;

		return 0;
	}

	/* Count, allocate and fill the ranges. The PFC SoC data pins array must
	 * be sorted by pin numbers, and pins without a GPIO port must come
	 * last.
	 */
	for (i = 1, nr_ranges = 1; i < pfc->info->nr_pins; ++i) {
		if (pfc->info->pins[i-1].pin != pfc->info->pins[i].pin - 1)
			nr_ranges++;
	}

	pfc->nr_ranges = nr_ranges;
	pfc->ranges = devm_kzalloc(pfc->dev, sizeof(*pfc->ranges) * nr_ranges,
				   GFP_KERNEL);
	if (pfc->ranges == NULL)
		return -ENOMEM;

	range = pfc->ranges;
	range->start = pfc->info->pins[0].pin;

	for (i = 1; i < pfc->info->nr_pins; ++i) {
		if (pfc->info->pins[i-1].pin == pfc->info->pins[i].pin - 1)
			continue;

		range->end = pfc->info->pins[i-1].pin;
		if (!(pfc->info->pins[i-1].configs & SH_PFC_PIN_CFG_NO_GPIO))
			pfc->nr_gpio_pins = range->end + 1;

		range++;
		range->start = pfc->info->pins[i].pin;
	}

	range->end = pfc->info->pins[i-1].pin;
	if (!(pfc->info->pins[i-1].configs & SH_PFC_PIN_CFG_NO_GPIO))
		pfc->nr_gpio_pins = range->end + 1;

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id sh_pfc_of_table[] = {
#ifdef CONFIG_PINCTRL_PFC_EMEV2
	{
		.compatible = "renesas,pfc-emev2",
		.data = &emev2_pinmux_info,
	},
#endif
#ifdef CONFIG_PINCTRL_PFC_R8A73A4
	{
		.compatible = "renesas,pfc-r8a73a4",
		.data = &r8a73a4_pinmux_info,
	},
#endif
#ifdef CONFIG_PINCTRL_PFC_R8A7740
	{
		.compatible = "renesas,pfc-r8a7740",
		.data = &r8a7740_pinmux_info,
	},
#endif
#ifdef CONFIG_PINCTRL_PFC_R8A7778
	{
		.compatible = "renesas,pfc-r8a7778",
		.data = &r8a7778_pinmux_info,
	},
#endif
#ifdef CONFIG_PINCTRL_PFC_R8A7779
	{
		.compatible = "renesas,pfc-r8a7779",
		.data = &r8a7779_pinmux_info,
	},
#endif
#ifdef CONFIG_PINCTRL_PFC_R8A7790
	{
		.compatible = "renesas,pfc-r8a7790",
		.data = &r8a7790_pinmux_info,
	},
#endif
#ifdef CONFIG_PINCTRL_PFC_R8A7791
	{
		.compatible = "renesas,pfc-r8a7791",
		.data = &r8a7791_pinmux_info,
	},
#endif
#ifdef CONFIG_PINCTRL_PFC_SH73A0
	{
		.compatible = "renesas,pfc-sh73a0",
		.data = &sh73a0_pinmux_info,
	},
#endif
	{ },
};
MODULE_DEVICE_TABLE(of, sh_pfc_of_table);
#endif

static int sh_pfc_probe(struct platform_device *pdev)
{
	const struct platform_device_id *platid = platform_get_device_id(pdev);
#ifdef CONFIG_OF
	struct device_node *np = pdev->dev.of_node;
#endif
	const struct sh_pfc_soc_info *info;
	struct sh_pfc *pfc;
	int ret;

#ifdef CONFIG_OF
	if (np)
		info = of_match_device(sh_pfc_of_table, &pdev->dev)->data;
	else
#endif
		info = platid ? (const void *)platid->driver_data : NULL;

	if (info == NULL)
		return -ENODEV;

	pfc = devm_kzalloc(&pdev->dev, sizeof(*pfc), GFP_KERNEL);
	if (pfc == NULL)
		return -ENOMEM;

	pfc->info = info;
	pfc->dev = &pdev->dev;

	ret = sh_pfc_map_resources(pfc, pdev);
	if (unlikely(ret < 0))
		return ret;

	spin_lock_init(&pfc->lock);

	if (info->ops && info->ops->init) {
		ret = info->ops->init(pfc);
		if (ret < 0)
			return ret;
	}

	pinctrl_provide_dummies();

	ret = sh_pfc_init_ranges(pfc);
	if (ret < 0)
		return ret;

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

	return 0;
}

static const struct platform_device_id sh_pfc_id_table[] = {
#ifdef CONFIG_PINCTRL_PFC_EMEV2
	{ "pfc-emev2", (kernel_ulong_t)&emev2_pinmux_info },
#endif
#ifdef CONFIG_PINCTRL_PFC_R8A73A4
	{ "pfc-r8a73a4", (kernel_ulong_t)&r8a73a4_pinmux_info },
#endif
#ifdef CONFIG_PINCTRL_PFC_R8A7740
	{ "pfc-r8a7740", (kernel_ulong_t)&r8a7740_pinmux_info },
#endif
#ifdef CONFIG_PINCTRL_PFC_R8A7778
	{ "pfc-r8a7778", (kernel_ulong_t)&r8a7778_pinmux_info },
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
		.of_match_table = of_match_ptr(sh_pfc_of_table),
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
