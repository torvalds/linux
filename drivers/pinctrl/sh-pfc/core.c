// SPDX-License-Identifier: GPL-2.0
/*
 * Pin Control and GPIO driver for SuperH Pin Function Controller.
 *
 * Authors: Magnus Damm, Paul Mundt, Laurent Pinchart
 *
 * Copyright (C) 2008 Magnus Damm
 * Copyright (C) 2009 - 2012 Paul Mundt
 */

#define DRV_NAME "sh-pfc"

#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pinctrl/machine.h>
#include <linux/platform_device.h>
#include <linux/psci.h>
#include <linux/slab.h>
#include <linux/sys_soc.h>

#include "core.h"

static int sh_pfc_map_resources(struct sh_pfc *pfc,
				struct platform_device *pdev)
{
	struct sh_pfc_window *windows;
	unsigned int *irqs = NULL;
	unsigned int num_windows;
	struct resource *res;
	unsigned int i;
	int num_irqs;

	/* Count the MEM and IRQ resources. */
	for (num_windows = 0;; num_windows++) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, num_windows);
		if (!res)
			break;
	}
	if (num_windows == 0)
		return -EINVAL;

	num_irqs = platform_irq_count(pdev);
	if (num_irqs < 0)
		return num_irqs;

	/* Allocate memory windows and IRQs arrays. */
	windows = devm_kcalloc(pfc->dev, num_windows, sizeof(*windows),
			       GFP_KERNEL);
	if (windows == NULL)
		return -ENOMEM;

	pfc->num_windows = num_windows;
	pfc->windows = windows;

	if (num_irqs) {
		irqs = devm_kcalloc(pfc->dev, num_irqs, sizeof(*irqs),
				    GFP_KERNEL);
		if (irqs == NULL)
			return -ENOMEM;

		pfc->num_irqs = num_irqs;
		pfc->irqs = irqs;
	}

	/* Fill them. */
	for (i = 0; i < num_windows; i++) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, i);
		windows->phys = res->start;
		windows->size = resource_size(res);
		windows->virt = devm_ioremap_resource(pfc->dev, res);
		if (IS_ERR(windows->virt))
			return -ENOMEM;
		windows++;
	}
	for (i = 0; i < num_irqs; i++)
		*irqs++ = platform_get_irq(pdev, i);

	return 0;
}

static void __iomem *sh_pfc_phys_to_virt(struct sh_pfc *pfc, u32 reg)
{
	struct sh_pfc_window *window;
	phys_addr_t address = reg;
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

u32 sh_pfc_read_raw_reg(void __iomem *mapped_reg, unsigned int reg_width)
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

void sh_pfc_write_raw_reg(void __iomem *mapped_reg, unsigned int reg_width,
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

u32 sh_pfc_read(struct sh_pfc *pfc, u32 reg)
{
	return sh_pfc_read_raw_reg(sh_pfc_phys_to_virt(pfc, reg), 32);
}

void sh_pfc_write(struct sh_pfc *pfc, u32 reg, u32 data)
{
	if (pfc->info->unlock_reg)
		sh_pfc_write_raw_reg(
			sh_pfc_phys_to_virt(pfc, pfc->info->unlock_reg), 32,
			~data);

	sh_pfc_write_raw_reg(sh_pfc_phys_to_virt(pfc, reg), 32, data);
}

static void sh_pfc_config_reg_helper(struct sh_pfc *pfc,
				     const struct pinmux_cfg_reg *crp,
				     unsigned int in_pos,
				     void __iomem **mapped_regp, u32 *maskp,
				     unsigned int *posp)
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
				    unsigned int field, u32 value)
{
	void __iomem *mapped_reg;
	unsigned int pos;
	u32 mask, data;

	sh_pfc_config_reg_helper(pfc, crp, field, &mapped_reg, &mask, &pos);

	dev_dbg(pfc->dev, "write_reg addr = %x, value = 0x%x, field = %u, "
		"r_width = %u, f_width = %u\n",
		crp->reg, value, field, crp->reg_width, hweight32(mask));

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
				 const struct pinmux_cfg_reg **crp,
				 unsigned int *fieldp, u32 *valuep)
{
	unsigned int k = 0;

	while (1) {
		const struct pinmux_cfg_reg *config_reg =
			pfc->info->cfg_regs + k;
		unsigned int r_width = config_reg->reg_width;
		unsigned int f_width = config_reg->field_width;
		unsigned int curr_width;
		unsigned int bit_pos;
		unsigned int pos = 0;
		unsigned int m = 0;

		if (!r_width)
			break;

		for (bit_pos = 0; bit_pos < r_width; bit_pos += curr_width) {
			u32 ncomb;
			u32 n;

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
	const u16 *data = pfc->info->pinmux_data;
	unsigned int k;

	if (pos) {
		*enum_idp = data[pos + 1];
		return pos + 1;
	}

	for (k = 0; k < pfc->info->pinmux_data_size; k++) {
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
	const struct pinmux_range *range;
	int pos = 0;

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

	/* Iterate over all the configuration fields we need to update. */
	while (1) {
		const struct pinmux_cfg_reg *cr;
		unsigned int field;
		u16 enum_id;
		u32 value;
		int in_range;
		int ret;

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

const struct pinmux_bias_reg *
sh_pfc_pin_to_bias_reg(const struct sh_pfc *pfc, unsigned int pin,
		       unsigned int *bit)
{
	unsigned int i, j;

	for (i = 0; pfc->info->bias_regs[i].puen; i++) {
		for (j = 0; j < ARRAY_SIZE(pfc->info->bias_regs[i].pins); j++) {
			if (pfc->info->bias_regs[i].pins[j] == pin) {
				*bit = j;
				return &pfc->info->bias_regs[i];
			}
		}
	}

	WARN_ONCE(1, "Pin %u is not in bias info list\n", pin);

	return NULL;
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
	pfc->ranges = devm_kcalloc(pfc->dev, nr_ranges, sizeof(*pfc->ranges),
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
#ifdef CONFIG_PINCTRL_PFC_R8A7742
	{
		.compatible = "renesas,pfc-r8a7742",
		.data = &r8a7742_pinmux_info,
	},
#endif
#ifdef CONFIG_PINCTRL_PFC_R8A7743
	{
		.compatible = "renesas,pfc-r8a7743",
		.data = &r8a7743_pinmux_info,
	},
#endif
#ifdef CONFIG_PINCTRL_PFC_R8A7744
	{
		.compatible = "renesas,pfc-r8a7744",
		.data = &r8a7744_pinmux_info,
	},
#endif
#ifdef CONFIG_PINCTRL_PFC_R8A7745
	{
		.compatible = "renesas,pfc-r8a7745",
		.data = &r8a7745_pinmux_info,
	},
#endif
#ifdef CONFIG_PINCTRL_PFC_R8A77470
	{
		.compatible = "renesas,pfc-r8a77470",
		.data = &r8a77470_pinmux_info,
	},
#endif
#ifdef CONFIG_PINCTRL_PFC_R8A774A1
	{
		.compatible = "renesas,pfc-r8a774a1",
		.data = &r8a774a1_pinmux_info,
	},
#endif
#ifdef CONFIG_PINCTRL_PFC_R8A774B1
	{
		.compatible = "renesas,pfc-r8a774b1",
		.data = &r8a774b1_pinmux_info,
	},
#endif
#ifdef CONFIG_PINCTRL_PFC_R8A774C0
	{
		.compatible = "renesas,pfc-r8a774c0",
		.data = &r8a774c0_pinmux_info,
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
#ifdef CONFIG_PINCTRL_PFC_R8A7792
	{
		.compatible = "renesas,pfc-r8a7792",
		.data = &r8a7792_pinmux_info,
	},
#endif
#ifdef CONFIG_PINCTRL_PFC_R8A7793
	{
		.compatible = "renesas,pfc-r8a7793",
		.data = &r8a7793_pinmux_info,
	},
#endif
#ifdef CONFIG_PINCTRL_PFC_R8A7794
	{
		.compatible = "renesas,pfc-r8a7794",
		.data = &r8a7794_pinmux_info,
	},
#endif
/* Both r8a7795 entries must be present to make sanity checks work */
#ifdef CONFIG_PINCTRL_PFC_R8A77950
	{
		.compatible = "renesas,pfc-r8a7795",
		.data = &r8a77950_pinmux_info,
	},
#endif
#ifdef CONFIG_PINCTRL_PFC_R8A77951
	{
		.compatible = "renesas,pfc-r8a7795",
		.data = &r8a77951_pinmux_info,
	},
#endif
#ifdef CONFIG_PINCTRL_PFC_R8A77960
	{
		.compatible = "renesas,pfc-r8a7796",
		.data = &r8a77960_pinmux_info,
	},
#endif
#ifdef CONFIG_PINCTRL_PFC_R8A77961
	{
		.compatible = "renesas,pfc-r8a77961",
		.data = &r8a77961_pinmux_info,
	},
#endif
#ifdef CONFIG_PINCTRL_PFC_R8A77965
	{
		.compatible = "renesas,pfc-r8a77965",
		.data = &r8a77965_pinmux_info,
	},
#endif
#ifdef CONFIG_PINCTRL_PFC_R8A77970
	{
		.compatible = "renesas,pfc-r8a77970",
		.data = &r8a77970_pinmux_info,
	},
#endif
#ifdef CONFIG_PINCTRL_PFC_R8A77980
	{
		.compatible = "renesas,pfc-r8a77980",
		.data = &r8a77980_pinmux_info,
	},
#endif
#ifdef CONFIG_PINCTRL_PFC_R8A77990
	{
		.compatible = "renesas,pfc-r8a77990",
		.data = &r8a77990_pinmux_info,
	},
#endif
#ifdef CONFIG_PINCTRL_PFC_R8A77995
	{
		.compatible = "renesas,pfc-r8a77995",
		.data = &r8a77995_pinmux_info,
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
#endif

#if defined(CONFIG_PM_SLEEP) && defined(CONFIG_ARM_PSCI_FW)
static void sh_pfc_nop_reg(struct sh_pfc *pfc, u32 reg, unsigned int idx)
{
}

static void sh_pfc_save_reg(struct sh_pfc *pfc, u32 reg, unsigned int idx)
{
	pfc->saved_regs[idx] = sh_pfc_read(pfc, reg);
}

static void sh_pfc_restore_reg(struct sh_pfc *pfc, u32 reg, unsigned int idx)
{
	sh_pfc_write(pfc, reg, pfc->saved_regs[idx]);
}

static unsigned int sh_pfc_walk_regs(struct sh_pfc *pfc,
	void (*do_reg)(struct sh_pfc *pfc, u32 reg, unsigned int idx))
{
	unsigned int i, n = 0;

	if (pfc->info->cfg_regs)
		for (i = 0; pfc->info->cfg_regs[i].reg; i++)
			do_reg(pfc, pfc->info->cfg_regs[i].reg, n++);

	if (pfc->info->drive_regs)
		for (i = 0; pfc->info->drive_regs[i].reg; i++)
			do_reg(pfc, pfc->info->drive_regs[i].reg, n++);

	if (pfc->info->bias_regs)
		for (i = 0; pfc->info->bias_regs[i].puen; i++) {
			do_reg(pfc, pfc->info->bias_regs[i].puen, n++);
			if (pfc->info->bias_regs[i].pud)
				do_reg(pfc, pfc->info->bias_regs[i].pud, n++);
		}

	if (pfc->info->ioctrl_regs)
		for (i = 0; pfc->info->ioctrl_regs[i].reg; i++)
			do_reg(pfc, pfc->info->ioctrl_regs[i].reg, n++);

	return n;
}

static int sh_pfc_suspend_init(struct sh_pfc *pfc)
{
	unsigned int n;

	/* This is the best we can do to check for the presence of PSCI */
	if (!psci_ops.cpu_suspend)
		return 0;

	n = sh_pfc_walk_regs(pfc, sh_pfc_nop_reg);
	if (!n)
		return 0;

	pfc->saved_regs = devm_kmalloc_array(pfc->dev, n,
					     sizeof(*pfc->saved_regs),
					     GFP_KERNEL);
	if (!pfc->saved_regs)
		return -ENOMEM;

	dev_dbg(pfc->dev, "Allocated space to save %u regs\n", n);
	return 0;
}

static int sh_pfc_suspend_noirq(struct device *dev)
{
	struct sh_pfc *pfc = dev_get_drvdata(dev);

	if (pfc->saved_regs)
		sh_pfc_walk_regs(pfc, sh_pfc_save_reg);
	return 0;
}

static int sh_pfc_resume_noirq(struct device *dev)
{
	struct sh_pfc *pfc = dev_get_drvdata(dev);

	if (pfc->saved_regs)
		sh_pfc_walk_regs(pfc, sh_pfc_restore_reg);
	return 0;
}

static const struct dev_pm_ops sh_pfc_pm  = {
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(sh_pfc_suspend_noirq, sh_pfc_resume_noirq)
};
#define DEV_PM_OPS	&sh_pfc_pm
#else
static int sh_pfc_suspend_init(struct sh_pfc *pfc) { return 0; }
#define DEV_PM_OPS	NULL
#endif /* CONFIG_PM_SLEEP && CONFIG_ARM_PSCI_FW */

#ifdef DEBUG
#define SH_PFC_MAX_REGS		300
#define SH_PFC_MAX_ENUMS	3000

static unsigned int sh_pfc_errors __initdata = 0;
static unsigned int sh_pfc_warnings __initdata = 0;
static u32 *sh_pfc_regs __initdata = NULL;
static u32 sh_pfc_num_regs __initdata = 0;
static u16 *sh_pfc_enums __initdata = NULL;
static u32 sh_pfc_num_enums __initdata = 0;

#define sh_pfc_err(fmt, ...)					\
	do {							\
		pr_err("%s: " fmt, drvname, ##__VA_ARGS__);	\
		sh_pfc_errors++;				\
	} while (0)
#define sh_pfc_warn(fmt, ...)					\
	do {							\
		pr_warn("%s: " fmt, drvname, ##__VA_ARGS__);	\
		sh_pfc_warnings++;				\
	} while (0)

static bool __init is0s(const u16 *enum_ids, unsigned int n)
{
	unsigned int i;

	for (i = 0; i < n; i++)
		if (enum_ids[i])
			return false;

	return true;
}

static bool __init same_name(const char *a, const char *b)
{
	if (!a || !b)
		return false;

	return !strcmp(a, b);
}

static void __init sh_pfc_check_reg(const char *drvname, u32 reg)
{
	unsigned int i;

	for (i = 0; i < sh_pfc_num_regs; i++)
		if (reg == sh_pfc_regs[i]) {
			sh_pfc_err("reg 0x%x conflict\n", reg);
			return;
		}

	if (sh_pfc_num_regs == SH_PFC_MAX_REGS) {
		pr_warn_once("%s: Please increase SH_PFC_MAX_REGS\n", drvname);
		return;
	}

	sh_pfc_regs[sh_pfc_num_regs++] = reg;
}

static int __init sh_pfc_check_enum(const char *drvname, u16 enum_id)
{
	unsigned int i;

	for (i = 0; i < sh_pfc_num_enums; i++) {
		if (enum_id == sh_pfc_enums[i])
			return -EINVAL;
	}

	if (sh_pfc_num_enums == SH_PFC_MAX_ENUMS) {
		pr_warn_once("%s: Please increase SH_PFC_MAX_ENUMS\n", drvname);
		return 0;
	}

	sh_pfc_enums[sh_pfc_num_enums++] = enum_id;
	return 0;
}

static void __init sh_pfc_check_reg_enums(const char *drvname, u32 reg,
					  const u16 *enums, unsigned int n)
{
	unsigned int i;

	for (i = 0; i < n; i++) {
		if (enums[i] && sh_pfc_check_enum(drvname, enums[i]))
			sh_pfc_err("reg 0x%x enum_id %u conflict\n", reg,
				   enums[i]);
	}
}

static void __init sh_pfc_check_pin(const struct sh_pfc_soc_info *info,
				    u32 reg, unsigned int pin)
{
	const char *drvname = info->name;
	unsigned int i;

	if (pin == SH_PFC_PIN_NONE)
		return;

	for (i = 0; i < info->nr_pins; i++) {
		if (pin == info->pins[i].pin)
			return;
	}

	sh_pfc_err("reg 0x%x: pin %u not found\n", reg, pin);
}

static void __init sh_pfc_check_cfg_reg(const char *drvname,
					const struct pinmux_cfg_reg *cfg_reg)
{
	unsigned int i, n, rw, fw;

	sh_pfc_check_reg(drvname, cfg_reg->reg);

	if (cfg_reg->field_width) {
		n = cfg_reg->reg_width / cfg_reg->field_width;
		/* Skip field checks (done at build time) */
		goto check_enum_ids;
	}

	for (i = 0, n = 0, rw = 0; (fw = cfg_reg->var_field_width[i]); i++) {
		if (fw > 3 && is0s(&cfg_reg->enum_ids[n], 1 << fw))
			sh_pfc_warn("reg 0x%x: reserved field [%u:%u] can be split to reduce table size\n",
				    cfg_reg->reg, rw, rw + fw - 1);
		n += 1 << fw;
		rw += fw;
	}

	if (rw != cfg_reg->reg_width)
		sh_pfc_err("reg 0x%x: var_field_width declares %u instead of %u bits\n",
			   cfg_reg->reg, rw, cfg_reg->reg_width);

	if (n != cfg_reg->nr_enum_ids)
		sh_pfc_err("reg 0x%x: enum_ids[] has %u instead of %u values\n",
			   cfg_reg->reg, cfg_reg->nr_enum_ids, n);

check_enum_ids:
	sh_pfc_check_reg_enums(drvname, cfg_reg->reg, cfg_reg->enum_ids, n);
}

static void __init sh_pfc_check_drive_reg(const struct sh_pfc_soc_info *info,
					  const struct pinmux_drive_reg *drive)
{
	const char *drvname = info->name;
	unsigned long seen = 0, mask;
	unsigned int i;

	sh_pfc_check_reg(info->name, drive->reg);
	for (i = 0; i < ARRAY_SIZE(drive->fields); i++) {
		const struct pinmux_drive_reg_field *field = &drive->fields[i];

		if (!field->pin && !field->offset && !field->size)
			continue;

		mask = GENMASK(field->offset + field->size, field->offset);
		if (mask & seen)
			sh_pfc_err("drive_reg 0x%x: field %u overlap\n",
				   drive->reg, i);
		seen |= mask;

		sh_pfc_check_pin(info, drive->reg, field->pin);
	}
}

static void __init sh_pfc_check_bias_reg(const struct sh_pfc_soc_info *info,
					 const struct pinmux_bias_reg *bias)
{
	unsigned int i;

	sh_pfc_check_reg(info->name, bias->puen);
	if (bias->pud)
		sh_pfc_check_reg(info->name, bias->pud);
	for (i = 0; i < ARRAY_SIZE(bias->pins); i++)
		sh_pfc_check_pin(info, bias->puen, bias->pins[i]);
}

static void __init sh_pfc_check_info(const struct sh_pfc_soc_info *info)
{
	const char *drvname = info->name;
	unsigned int *refcnts;
	unsigned int i, j, k;

	pr_info("Checking %s\n", drvname);
	sh_pfc_num_regs = 0;
	sh_pfc_num_enums = 0;

	/* Check pins */
	for (i = 0; i < info->nr_pins; i++) {
		const struct sh_pfc_pin *pin = &info->pins[i];

		if (!pin->name) {
			sh_pfc_err("empty pin %u\n", i);
			continue;
		}
		for (j = 0; j < i; j++) {
			const struct sh_pfc_pin *pin2 = &info->pins[j];

			if (same_name(pin->name, pin2->name))
				sh_pfc_err("pin %s: name conflict\n",
					   pin->name);

			if (pin->pin != (u16)-1 && pin->pin == pin2->pin)
				sh_pfc_err("pin %s/%s: pin %u conflict\n",
					   pin->name, pin2->name, pin->pin);

			if (pin->enum_id && pin->enum_id == pin2->enum_id)
				sh_pfc_err("pin %s/%s: enum_id %u conflict\n",
					   pin->name, pin2->name,
					   pin->enum_id);
		}
	}

	/* Check groups and functions */
	refcnts = kcalloc(info->nr_groups, sizeof(*refcnts), GFP_KERNEL);
	if (!refcnts)
		return;

	for (i = 0; i < info->nr_functions; i++) {
		const struct sh_pfc_function *func = &info->functions[i];

		if (!func->name) {
			sh_pfc_err("empty function %u\n", i);
			continue;
		}
		for (j = 0; j < i; j++) {
			if (same_name(func->name, info->functions[j].name))
				sh_pfc_err("function %s: name conflict\n",
					   func->name);
		}
		for (j = 0; j < func->nr_groups; j++) {
			for (k = 0; k < info->nr_groups; k++) {
				if (same_name(func->groups[j],
					      info->groups[k].name)) {
					refcnts[k]++;
					break;
				}
			}

			if (k == info->nr_groups)
				sh_pfc_err("function %s: group %s not found\n",
					   func->name, func->groups[j]);
		}
	}

	for (i = 0; i < info->nr_groups; i++) {
		const struct sh_pfc_pin_group *group = &info->groups[i];

		if (!group->name) {
			sh_pfc_err("empty group %u\n", i);
			continue;
		}
		for (j = 0; j < i; j++) {
			if (same_name(group->name, info->groups[j].name))
				sh_pfc_err("group %s: name conflict\n",
					   group->name);
		}
		if (!refcnts[i])
			sh_pfc_err("orphan group %s\n", group->name);
		else if (refcnts[i] > 1)
			sh_pfc_warn("group %s referenced by %u functions\n",
				    group->name, refcnts[i]);
	}

	kfree(refcnts);

	/* Check config register descriptions */
	for (i = 0; info->cfg_regs && info->cfg_regs[i].reg; i++)
		sh_pfc_check_cfg_reg(drvname, &info->cfg_regs[i]);

	/* Check drive strength registers */
	for (i = 0; info->drive_regs && info->drive_regs[i].reg; i++)
		sh_pfc_check_drive_reg(info, &info->drive_regs[i]);

	/* Check bias registers */
	for (i = 0; info->bias_regs && info->bias_regs[i].puen; i++)
		sh_pfc_check_bias_reg(info, &info->bias_regs[i]);

	/* Check ioctrl registers */
	for (i = 0; info->ioctrl_regs && info->ioctrl_regs[i].reg; i++)
		sh_pfc_check_reg(drvname, info->ioctrl_regs[i].reg);

	/* Check data registers */
	for (i = 0; info->data_regs && info->data_regs[i].reg; i++) {
		sh_pfc_check_reg(drvname, info->data_regs[i].reg);
		sh_pfc_check_reg_enums(drvname, info->data_regs[i].reg,
				       info->data_regs[i].enum_ids,
				       info->data_regs[i].reg_width);
	}

#ifdef CONFIG_PINCTRL_SH_FUNC_GPIO
	/* Check function GPIOs */
	for (i = 0; i < info->nr_func_gpios; i++) {
		const struct pinmux_func *func = &info->func_gpios[i];

		if (!func->name) {
			sh_pfc_err("empty function gpio %u\n", i);
			continue;
		}
		for (j = 0; j < i; j++) {
			if (same_name(func->name, info->func_gpios[j].name))
				sh_pfc_err("func_gpio %s: name conflict\n",
					   func->name);
		}
		if (sh_pfc_check_enum(drvname, func->enum_id))
			sh_pfc_err("%s enum_id %u conflict\n", func->name,
				   func->enum_id);
	}
#endif
}

static void __init sh_pfc_check_driver(const struct platform_driver *pdrv)
{
	unsigned int i;

	sh_pfc_regs = kcalloc(SH_PFC_MAX_REGS, sizeof(*sh_pfc_regs),
			      GFP_KERNEL);
	if (!sh_pfc_regs)
		return;

	sh_pfc_enums = kcalloc(SH_PFC_MAX_ENUMS, sizeof(*sh_pfc_enums),
			      GFP_KERNEL);
	if (!sh_pfc_enums)
		goto free_regs;

	pr_warn("Checking builtin pinmux tables\n");

	for (i = 0; pdrv->id_table[i].name[0]; i++)
		sh_pfc_check_info((void *)pdrv->id_table[i].driver_data);

#ifdef CONFIG_OF
	for (i = 0; pdrv->driver.of_match_table[i].compatible[0]; i++)
		sh_pfc_check_info(pdrv->driver.of_match_table[i].data);
#endif

	pr_warn("Detected %u errors and %u warnings\n", sh_pfc_errors,
		sh_pfc_warnings);

	kfree(sh_pfc_enums);
free_regs:
	kfree(sh_pfc_regs);
}

#else /* !DEBUG */
static inline void sh_pfc_check_driver(struct platform_driver *pdrv) {}
#endif /* !DEBUG */

#ifdef CONFIG_OF
static const void *sh_pfc_quirk_match(void)
{
#if defined(CONFIG_PINCTRL_PFC_R8A77950) || \
    defined(CONFIG_PINCTRL_PFC_R8A77951)
	const struct soc_device_attribute *match;
	static const struct soc_device_attribute quirks[] = {
		{
			.soc_id = "r8a7795", .revision = "ES1.*",
			.data = &r8a77950_pinmux_info,
		},
		{
			.soc_id = "r8a7795",
			.data = &r8a77951_pinmux_info,
		},

		{ /* sentinel */ }
	};

	match = soc_device_match(quirks);
	if (match)
		return match->data ?: ERR_PTR(-ENODEV);
#endif /* CONFIG_PINCTRL_PFC_R8A77950 || CONFIG_PINCTRL_PFC_R8A77951 */

	return NULL;
}
#endif /* CONFIG_OF */

static int sh_pfc_probe(struct platform_device *pdev)
{
	const struct sh_pfc_soc_info *info;
	struct sh_pfc *pfc;
	int ret;

#ifdef CONFIG_OF
	if (pdev->dev.of_node) {
		info = sh_pfc_quirk_match();
		if (IS_ERR(info))
			return PTR_ERR(info);

		if (!info)
			info = of_device_get_match_data(&pdev->dev);
	} else
#endif
		info = (const void *)platform_get_device_id(pdev)->driver_data;

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

		/* .init() may have overridden pfc->info */
		info = pfc->info;
	}

	ret = sh_pfc_suspend_init(pfc);
	if (ret)
		return ret;

	/* Enable dummy states for those platforms without pinctrl support */
	if (!of_have_populated_dt())
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

#ifdef CONFIG_PINCTRL_SH_PFC_GPIO
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

static const struct platform_device_id sh_pfc_id_table[] = {
#ifdef CONFIG_PINCTRL_PFC_SH7203
	{ "pfc-sh7203", (kernel_ulong_t)&sh7203_pinmux_info },
#endif
#ifdef CONFIG_PINCTRL_PFC_SH7264
	{ "pfc-sh7264", (kernel_ulong_t)&sh7264_pinmux_info },
#endif
#ifdef CONFIG_PINCTRL_PFC_SH7269
	{ "pfc-sh7269", (kernel_ulong_t)&sh7269_pinmux_info },
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
	{ },
};

static struct platform_driver sh_pfc_driver = {
	.probe		= sh_pfc_probe,
	.id_table	= sh_pfc_id_table,
	.driver		= {
		.name	= DRV_NAME,
		.of_match_table = of_match_ptr(sh_pfc_of_table),
		.pm     = DEV_PM_OPS,
	},
};

static int __init sh_pfc_init(void)
{
	sh_pfc_check_driver(&sh_pfc_driver);
	return platform_driver_register(&sh_pfc_driver);
}
postcore_initcall(sh_pfc_init);
