// SPDX-License-Identifier: GPL-2.0-only
/*
 * GPIO driver for the IP block found in the Nomadik SoC; it is an AMBA device,
 * managing 32 pins with alternate functions. It can also handle the STA2X11
 * block from ST.
 *
 * The GPIO chips are shared with pinctrl-nomadik if used; it needs access for
 * pinmuxing functionality and others.
 *
 * This driver also handles the mobileye,eyeq5-gpio compatible. It is an STA2X11
 * but with only data, direction and interrupts register active. We want to
 * avoid touching SLPM, RWIMSC, FWIMSC, AFSLA and AFSLB registers; that is,
 * wake and alternate function registers. It is NOT compatible with
 * pinctrl-nomadik.
 *
 * Copyright (C) 2008,2009 STMicroelectronics
 * Copyright (C) 2009 Alessandro Rubini <rubini@unipv.it>
 *   Rewritten based on work by Prafulla WADASKAR <prafulla.wadaskar@st.com>
 * Copyright (C) 2011-2013 Linus Walleij <linus.walleij@linaro.org>
 */
#include <linux/cleanup.h>
#include <linux/clk.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/reset.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/string_choices.h>
#include <linux/types.h>

#include <linux/gpio/gpio-nomadik.h>

#ifndef CONFIG_PINCTRL_NOMADIK
static DEFINE_SPINLOCK(nmk_gpio_slpm_lock);
#endif

void __nmk_gpio_set_slpm(struct nmk_gpio_chip *nmk_chip, unsigned int offset,
			 enum nmk_gpio_slpm mode)
{
	u32 slpm;

	/* We should NOT have been called. */
	if (WARN_ON(nmk_chip->is_mobileye_soc))
		return;

	slpm = readl(nmk_chip->addr + NMK_GPIO_SLPC);
	if (mode == NMK_GPIO_SLPM_NOCHANGE)
		slpm |= BIT(offset);
	else
		slpm &= ~BIT(offset);
	writel(slpm, nmk_chip->addr + NMK_GPIO_SLPC);
}

static void __nmk_gpio_set_output(struct nmk_gpio_chip *nmk_chip,
				  unsigned int offset, int val)
{
	if (val)
		writel(BIT(offset), nmk_chip->addr + NMK_GPIO_DATS);
	else
		writel(BIT(offset), nmk_chip->addr + NMK_GPIO_DATC);
}

void __nmk_gpio_make_output(struct nmk_gpio_chip *nmk_chip,
			    unsigned int offset, int val)
{
	writel(BIT(offset), nmk_chip->addr + NMK_GPIO_DIRS);
	__nmk_gpio_set_output(nmk_chip, offset, val);
}

/* IRQ functions */

static void nmk_gpio_irq_ack(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct nmk_gpio_chip *nmk_chip = gpiochip_get_data(gc);

	clk_enable(nmk_chip->clk);
	writel(BIT(d->hwirq), nmk_chip->addr + NMK_GPIO_IC);
	clk_disable(nmk_chip->clk);
}

enum nmk_gpio_irq_type {
	NORMAL,
	WAKE,
};

static void __nmk_gpio_irq_modify(struct nmk_gpio_chip *nmk_chip,
				  int offset, enum nmk_gpio_irq_type which,
				  bool enable)
{
	u32 *rimscval;
	u32 *fimscval;
	u32 rimscreg;
	u32 fimscreg;

	if (which == NORMAL) {
		rimscreg = NMK_GPIO_RIMSC;
		fimscreg = NMK_GPIO_FIMSC;
		rimscval = &nmk_chip->rimsc;
		fimscval = &nmk_chip->fimsc;
	} else  {
		/* We should NOT have been called. */
		if (WARN_ON(nmk_chip->is_mobileye_soc))
			return;
		rimscreg = NMK_GPIO_RWIMSC;
		fimscreg = NMK_GPIO_FWIMSC;
		rimscval = &nmk_chip->rwimsc;
		fimscval = &nmk_chip->fwimsc;
	}

	/* we must individually set/clear the two edges */
	if (nmk_chip->edge_rising & BIT(offset)) {
		if (enable)
			*rimscval |= BIT(offset);
		else
			*rimscval &= ~BIT(offset);
		writel(*rimscval, nmk_chip->addr + rimscreg);
	}
	if (nmk_chip->edge_falling & BIT(offset)) {
		if (enable)
			*fimscval |= BIT(offset);
		else
			*fimscval &= ~BIT(offset);
		writel(*fimscval, nmk_chip->addr + fimscreg);
	}
}

static void __nmk_gpio_set_wake(struct nmk_gpio_chip *nmk_chip,
				int offset, bool on)
{
	/* We should NOT have been called. */
	if (WARN_ON(nmk_chip->is_mobileye_soc))
		return;

	/*
	 * Ensure WAKEUP_ENABLE is on.  No need to disable it if wakeup is
	 * disabled, since setting SLPM to 1 increases power consumption, and
	 * wakeup is anyhow controlled by the RIMSC and FIMSC registers.
	 */
	if (nmk_chip->sleepmode && on) {
		__nmk_gpio_set_slpm(nmk_chip, offset,
				    NMK_GPIO_SLPM_WAKEUP_ENABLE);
	}

	__nmk_gpio_irq_modify(nmk_chip, offset, WAKE, on);
}

static void nmk_gpio_irq_maskunmask(struct nmk_gpio_chip *nmk_chip,
				    struct irq_data *d, bool enable)
{
	unsigned long flags;

	clk_enable(nmk_chip->clk);
	spin_lock_irqsave(&nmk_gpio_slpm_lock, flags);
	spin_lock(&nmk_chip->lock);

	__nmk_gpio_irq_modify(nmk_chip, d->hwirq, NORMAL, enable);

	if (!nmk_chip->is_mobileye_soc && !(nmk_chip->real_wake & BIT(d->hwirq)))
		__nmk_gpio_set_wake(nmk_chip, d->hwirq, enable);

	spin_unlock(&nmk_chip->lock);
	spin_unlock_irqrestore(&nmk_gpio_slpm_lock, flags);
	clk_disable(nmk_chip->clk);
}

static void nmk_gpio_irq_mask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct nmk_gpio_chip *nmk_chip = gpiochip_get_data(gc);

	nmk_gpio_irq_maskunmask(nmk_chip, d, false);
	gpiochip_disable_irq(gc, irqd_to_hwirq(d));
}

static void nmk_gpio_irq_unmask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct nmk_gpio_chip *nmk_chip = gpiochip_get_data(gc);

	gpiochip_enable_irq(gc, irqd_to_hwirq(d));
	nmk_gpio_irq_maskunmask(nmk_chip, d, true);
}

static int nmk_gpio_irq_set_wake(struct irq_data *d, unsigned int on)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct nmk_gpio_chip *nmk_chip = gpiochip_get_data(gc);
	unsigned long flags;

	/* Handler is registered in all cases. */
	if (nmk_chip->is_mobileye_soc)
		return -ENXIO;

	clk_enable(nmk_chip->clk);
	spin_lock_irqsave(&nmk_gpio_slpm_lock, flags);
	spin_lock(&nmk_chip->lock);

	if (irqd_irq_disabled(d))
		__nmk_gpio_set_wake(nmk_chip, d->hwirq, on);

	if (on)
		nmk_chip->real_wake |= BIT(d->hwirq);
	else
		nmk_chip->real_wake &= ~BIT(d->hwirq);

	spin_unlock(&nmk_chip->lock);
	spin_unlock_irqrestore(&nmk_gpio_slpm_lock, flags);
	clk_disable(nmk_chip->clk);

	return 0;
}

static int nmk_gpio_irq_set_type(struct irq_data *d, unsigned int type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct nmk_gpio_chip *nmk_chip = gpiochip_get_data(gc);
	bool enabled = !irqd_irq_disabled(d);
	bool wake = irqd_is_wakeup_set(d);
	unsigned long flags;

	if (type & IRQ_TYPE_LEVEL_HIGH)
		return -EINVAL;
	if (type & IRQ_TYPE_LEVEL_LOW)
		return -EINVAL;

	clk_enable(nmk_chip->clk);
	spin_lock_irqsave(&nmk_chip->lock, flags);

	if (enabled)
		__nmk_gpio_irq_modify(nmk_chip, d->hwirq, NORMAL, false);

	if (!nmk_chip->is_mobileye_soc && (enabled || wake))
		__nmk_gpio_irq_modify(nmk_chip, d->hwirq, WAKE, false);

	nmk_chip->edge_rising &= ~BIT(d->hwirq);
	if (type & IRQ_TYPE_EDGE_RISING)
		nmk_chip->edge_rising |= BIT(d->hwirq);

	nmk_chip->edge_falling &= ~BIT(d->hwirq);
	if (type & IRQ_TYPE_EDGE_FALLING)
		nmk_chip->edge_falling |= BIT(d->hwirq);

	if (enabled)
		__nmk_gpio_irq_modify(nmk_chip, d->hwirq, NORMAL, true);

	if (!nmk_chip->is_mobileye_soc && (enabled || wake))
		__nmk_gpio_irq_modify(nmk_chip, d->hwirq, WAKE, true);

	spin_unlock_irqrestore(&nmk_chip->lock, flags);
	clk_disable(nmk_chip->clk);

	return 0;
}

static unsigned int nmk_gpio_irq_startup(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct nmk_gpio_chip *nmk_chip = gpiochip_get_data(gc);

	clk_enable(nmk_chip->clk);
	nmk_gpio_irq_unmask(d);
	return 0;
}

static void nmk_gpio_irq_shutdown(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct nmk_gpio_chip *nmk_chip = gpiochip_get_data(gc);

	nmk_gpio_irq_mask(d);
	clk_disable(nmk_chip->clk);
}

static irqreturn_t nmk_gpio_irq_handler(int irq, void *dev_id)
{
	struct nmk_gpio_chip *nmk_chip = dev_id;
	struct gpio_chip *chip = &nmk_chip->chip;
	unsigned long mask = GENMASK(chip->ngpio - 1, 0);
	unsigned long status;
	int bit;

	clk_enable(nmk_chip->clk);

	status = readl(nmk_chip->addr + NMK_GPIO_IS);

	/* Ensure we cannot leave pending bits; this should never occur. */
	if (unlikely(status & ~mask))
		writel(status & ~mask, nmk_chip->addr + NMK_GPIO_IC);

	clk_disable(nmk_chip->clk);

	for_each_set_bit(bit, &status, chip->ngpio)
		generic_handle_domain_irq_safe(chip->irq.domain, bit);

	return IRQ_RETVAL((status & mask) != 0);
}

/* I/O Functions */

static int nmk_gpio_get_dir(struct gpio_chip *chip, unsigned int offset)
{
	struct nmk_gpio_chip *nmk_chip = gpiochip_get_data(chip);
	int dir;

	clk_enable(nmk_chip->clk);

	dir = readl(nmk_chip->addr + NMK_GPIO_DIR) & BIT(offset);

	clk_disable(nmk_chip->clk);

	if (dir)
		return GPIO_LINE_DIRECTION_OUT;

	return GPIO_LINE_DIRECTION_IN;
}

static int nmk_gpio_make_input(struct gpio_chip *chip, unsigned int offset)
{
	struct nmk_gpio_chip *nmk_chip = gpiochip_get_data(chip);

	clk_enable(nmk_chip->clk);

	writel(BIT(offset), nmk_chip->addr + NMK_GPIO_DIRC);

	clk_disable(nmk_chip->clk);

	return 0;
}

static int nmk_gpio_get_input(struct gpio_chip *chip, unsigned int offset)
{
	struct nmk_gpio_chip *nmk_chip = gpiochip_get_data(chip);
	int value;

	clk_enable(nmk_chip->clk);

	value = !!(readl(nmk_chip->addr + NMK_GPIO_DAT) & BIT(offset));

	clk_disable(nmk_chip->clk);

	return value;
}

static int nmk_gpio_set_output(struct gpio_chip *chip, unsigned int offset,
			       int val)
{
	struct nmk_gpio_chip *nmk_chip = gpiochip_get_data(chip);

	clk_enable(nmk_chip->clk);

	__nmk_gpio_set_output(nmk_chip, offset, val);

	clk_disable(nmk_chip->clk);

	return 0;
}

static int nmk_gpio_make_output(struct gpio_chip *chip, unsigned int offset,
				int val)
{
	struct nmk_gpio_chip *nmk_chip = gpiochip_get_data(chip);

	clk_enable(nmk_chip->clk);

	__nmk_gpio_make_output(nmk_chip, offset, val);

	clk_disable(nmk_chip->clk);

	return 0;
}

#ifdef CONFIG_DEBUG_FS

static int nmk_gpio_get_mode(struct nmk_gpio_chip *nmk_chip, int offset)
{
	u32 afunc, bfunc;

	/* We don't support modes. */
	if (nmk_chip->is_mobileye_soc)
		return NMK_GPIO_ALT_GPIO;

	clk_enable(nmk_chip->clk);

	afunc = readl(nmk_chip->addr + NMK_GPIO_AFSLA) & BIT(offset);
	bfunc = readl(nmk_chip->addr + NMK_GPIO_AFSLB) & BIT(offset);

	clk_disable(nmk_chip->clk);

	return (afunc ? NMK_GPIO_ALT_A : 0) | (bfunc ? NMK_GPIO_ALT_B : 0);
}

void nmk_gpio_dbg_show_one(struct seq_file *s, struct pinctrl_dev *pctldev,
			   struct gpio_chip *chip, unsigned int offset)
{
	struct nmk_gpio_chip *nmk_chip = gpiochip_get_data(chip);
#ifdef CONFIG_PINCTRL_NOMADIK
	struct gpio_desc *desc;
#endif
	int mode;
	bool is_out;
	bool data_out;
	bool pull;
	static const char * const modes[] = {
		[NMK_GPIO_ALT_GPIO]	= "gpio",
		[NMK_GPIO_ALT_A]	= "altA",
		[NMK_GPIO_ALT_B]	= "altB",
		[NMK_GPIO_ALT_C]	= "altC",
		[NMK_GPIO_ALT_C + 1]	= "altC1",
		[NMK_GPIO_ALT_C + 2]	= "altC2",
		[NMK_GPIO_ALT_C + 3]	= "altC3",
		[NMK_GPIO_ALT_C + 4]	= "altC4",
	};

	char *label = gpiochip_dup_line_label(chip, offset);
	if (IS_ERR(label))
		return;

	clk_enable(nmk_chip->clk);
	is_out = !!(readl(nmk_chip->addr + NMK_GPIO_DIR) & BIT(offset));
	pull = !(readl(nmk_chip->addr + NMK_GPIO_PDIS) & BIT(offset));
	data_out = !!(readl(nmk_chip->addr + NMK_GPIO_DAT) & BIT(offset));
	mode = nmk_gpio_get_mode(nmk_chip, offset);
#ifdef CONFIG_PINCTRL_NOMADIK
	if (mode == NMK_GPIO_ALT_C && pctldev) {
		desc = gpio_device_get_desc(chip->gpiodev, offset);
		mode = nmk_prcm_gpiocr_get_mode(pctldev, desc_to_gpio(desc));
	}
#endif

	if (is_out) {
		seq_printf(s, " gpio-%-3d (%-20.20s) out %s           %s",
			   offset, label ?: "(none)", str_hi_lo(data_out),
			   (mode < 0) ? "unknown" : modes[mode]);
	} else {
		int irq = chip->to_irq(chip, offset);
		const int pullidx = pull ? 1 : 0;
		int val;
		static const char * const pulls[] = {
			"none        ",
			"pull enabled",
		};

		seq_printf(s, " gpio-%-3d (%-20.20s) in  %s %s",
			   offset, label ?: "(none)", pulls[pullidx],
			   (mode < 0) ? "unknown" : modes[mode]);

		val = nmk_gpio_get_input(chip, offset);
		seq_printf(s, " VAL %d", val);

		/*
		 * This races with request_irq(), set_irq_type(),
		 * and set_irq_wake() ... but those are "rare".
		 */
		if (irq > 0 && irq_has_action(irq)) {
			char *trigger;
			bool wake;

			if (nmk_chip->edge_rising & BIT(offset))
				trigger = "edge-rising";
			else if (nmk_chip->edge_falling & BIT(offset))
				trigger = "edge-falling";
			else
				trigger = "edge-undefined";

			wake = !!(nmk_chip->real_wake & BIT(offset));

			seq_printf(s, " irq-%d %s%s",
				   irq, trigger, wake ? " wakeup" : "");
		}
	}
	clk_disable(nmk_chip->clk);
}

static void nmk_gpio_dbg_show(struct seq_file *s, struct gpio_chip *chip)
{
	unsigned int i;

	for (i = 0; i < chip->ngpio; i++) {
		nmk_gpio_dbg_show_one(s, NULL, chip, i);
		seq_puts(s, "\n");
	}
}

#else

#define nmk_gpio_dbg_show	NULL

#endif

/*
 * We will allocate memory for the state container using devm* allocators
 * binding to the first device reaching this point, it doesn't matter if
 * it is the pin controller or GPIO driver. However we need to use the right
 * platform device when looking up resources so pay attention to pdev.
 */
struct nmk_gpio_chip *nmk_gpio_populate_chip(struct fwnode_handle *fwnode,
					     struct platform_device *pdev)
{
	struct nmk_gpio_chip *nmk_chip;
	struct platform_device *gpio_pdev;
	struct device *dev = &pdev->dev;
	struct reset_control *reset;
	struct device *gpio_dev;
	struct gpio_chip *chip;
	struct resource *res;
	struct clk *clk;
	void __iomem *base;
	u32 id, ngpio;
	int ret;

	gpio_dev = bus_find_device_by_fwnode(&platform_bus_type, fwnode);
	if (!gpio_dev) {
		dev_err(dev, "populate \"%pfwP\": device not found\n", fwnode);
		return ERR_PTR(-ENODEV);
	}
	gpio_pdev = to_platform_device(gpio_dev);

	if (device_property_read_u32(gpio_dev, "gpio-bank", &id)) {
		dev_err(dev, "populate: gpio-bank property not found\n");
		platform_device_put(gpio_pdev);
		return ERR_PTR(-EINVAL);
	}

#ifdef CONFIG_PINCTRL_NOMADIK
	if (id >= ARRAY_SIZE(nmk_gpio_chips)) {
		dev_err(dev, "populate: invalid id: %u\n", id);
		platform_device_put(gpio_pdev);
		return ERR_PTR(-EINVAL);
	}
	/* Already populated? */
	nmk_chip = nmk_gpio_chips[id];
	if (nmk_chip) {
		platform_device_put(gpio_pdev);
		return nmk_chip;
	}
#endif

	nmk_chip = devm_kzalloc(dev, sizeof(*nmk_chip), GFP_KERNEL);
	if (!nmk_chip) {
		platform_device_put(gpio_pdev);
		return ERR_PTR(-ENOMEM);
	}

	if (device_property_read_u32(gpio_dev, "ngpios", &ngpio)) {
		ngpio = NMK_GPIO_PER_CHIP;
		dev_dbg(dev, "populate: using default ngpio (%u)\n", ngpio);
	}

	nmk_chip->is_mobileye_soc = device_is_compatible(gpio_dev,
							 "mobileye,eyeq5-gpio");
	nmk_chip->bank = id;
	chip = &nmk_chip->chip;
	chip->base = -1;
	chip->ngpio = ngpio;
	chip->label = dev_name(gpio_dev);
	chip->parent = gpio_dev;

	/* NOTE: different devices! No devm_platform_ioremap_resource() here! */
	res = platform_get_resource(gpio_pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(dev, res);
	if (IS_ERR(base)) {
		platform_device_put(gpio_pdev);
		return ERR_CAST(base);
	}
	nmk_chip->addr = base;

	/* NOTE: do not use devm_ here! */
	clk = clk_get_optional(gpio_dev, NULL);
	if (IS_ERR(clk)) {
		platform_device_put(gpio_pdev);
		return ERR_CAST(clk);
	}
	clk_prepare(clk);
	nmk_chip->clk = clk;

	/* NOTE: do not use devm_ here! */
	reset = reset_control_get_optional_shared(gpio_dev, NULL);
	if (IS_ERR(reset)) {
		clk_unprepare(clk);
		clk_put(clk);
		platform_device_put(gpio_pdev);
		dev_err(dev, "failed getting reset control: %pe\n",
			reset);
		return ERR_CAST(reset);
	}

	/*
	 * Reset might be shared and asserts/deasserts calls are unbalanced. We
	 * only support sharing this reset with other gpio-nomadik devices that
	 * use this reset to ensure deassertion at probe.
	 */
	ret = reset_control_deassert(reset);
	if (ret) {
		reset_control_put(reset);
		clk_unprepare(clk);
		clk_put(clk);
		platform_device_put(gpio_pdev);
		dev_err(dev, "failed reset deassert: %d\n", ret);
		return ERR_PTR(ret);
	}

#ifdef CONFIG_PINCTRL_NOMADIK
	nmk_gpio_chips[id] = nmk_chip;
#endif
	return nmk_chip;
}

static void nmk_gpio_irq_print_chip(struct irq_data *d, struct seq_file *p)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct nmk_gpio_chip *nmk_chip = gpiochip_get_data(gc);

	seq_printf(p, "nmk%u-%u-%u", nmk_chip->bank,
		   gc->base, gc->base + gc->ngpio - 1);
}

static const struct irq_chip nmk_irq_chip = {
	.irq_ack = nmk_gpio_irq_ack,
	.irq_mask = nmk_gpio_irq_mask,
	.irq_unmask = nmk_gpio_irq_unmask,
	.irq_set_type = nmk_gpio_irq_set_type,
	.irq_set_wake = nmk_gpio_irq_set_wake,
	.irq_startup = nmk_gpio_irq_startup,
	.irq_shutdown = nmk_gpio_irq_shutdown,
	.irq_print_chip = nmk_gpio_irq_print_chip,
	.flags = IRQCHIP_MASK_ON_SUSPEND | IRQCHIP_IMMUTABLE,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

static int nmk_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct nmk_gpio_chip *nmk_chip;
	struct gpio_irq_chip *girq;
	bool supports_sleepmode;
	struct gpio_chip *chip;
	int irq;
	int ret;

	nmk_chip = nmk_gpio_populate_chip(dev_fwnode(dev), pdev);
	if (IS_ERR(nmk_chip)) {
		dev_err(dev, "could not populate nmk chip struct\n");
		return PTR_ERR(nmk_chip);
	}

	supports_sleepmode =
		device_property_read_bool(dev, "st,supports-sleepmode");

	/* Correct platform device ID */
	pdev->id = nmk_chip->bank;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	/*
	 * The virt address in nmk_chip->addr is in the nomadik register space,
	 * so we can simply convert the resource address, without remapping
	 */
	nmk_chip->sleepmode = supports_sleepmode;
	spin_lock_init(&nmk_chip->lock);

	chip = &nmk_chip->chip;
	chip->parent = dev;
	chip->request = gpiochip_generic_request;
	chip->free = gpiochip_generic_free;
	chip->get_direction = nmk_gpio_get_dir;
	chip->direction_input = nmk_gpio_make_input;
	chip->get = nmk_gpio_get_input;
	chip->direction_output = nmk_gpio_make_output;
	chip->set = nmk_gpio_set_output;
	chip->dbg_show = nmk_gpio_dbg_show;
	chip->can_sleep = false;
	chip->owner = THIS_MODULE;

	girq = &chip->irq;
	gpio_irq_chip_set_chip(girq, &nmk_irq_chip);
	girq->parent_handler = NULL;
	girq->num_parents = 0;
	girq->parents = NULL;
	girq->default_type = IRQ_TYPE_NONE;
	girq->handler = handle_edge_irq;

	ret = devm_request_irq(dev, irq, nmk_gpio_irq_handler, IRQF_SHARED,
			       dev_name(dev), nmk_chip);
	if (ret) {
		dev_err(dev, "failed requesting IRQ\n");
		return ret;
	}

	if (!nmk_chip->is_mobileye_soc) {
		clk_enable(nmk_chip->clk);
		nmk_chip->lowemi = readl_relaxed(nmk_chip->addr + NMK_GPIO_LOWEMI);
		clk_disable(nmk_chip->clk);
	}

	ret = gpiochip_add_data(chip, nmk_chip);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, nmk_chip);

	dev_info(dev, "chip registered\n");

	return 0;
}

static const struct of_device_id nmk_gpio_match[] = {
	{ .compatible = "st,nomadik-gpio", },
	{ .compatible = "mobileye,eyeq5-gpio", },
	{}
};

static struct platform_driver nmk_gpio_driver = {
	.driver = {
		.name = "nomadik-gpio",
		.of_match_table = nmk_gpio_match,
		.suppress_bind_attrs = true,
	},
	.probe = nmk_gpio_probe,
};

static int __init nmk_gpio_init(void)
{
	return platform_driver_register(&nmk_gpio_driver);
}
subsys_initcall(nmk_gpio_init);
