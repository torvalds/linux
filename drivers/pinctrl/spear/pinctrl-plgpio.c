/*
 * SPEAr platform PLGPIO driver
 *
 * Copyright (C) 2012 ST Microelectronics
 * Viresh Kumar <viresh.kumar@linaro.org>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/spinlock.h>
#include <asm/mach/irq.h>

#define MAX_GPIO_PER_REG		32
#define PIN_OFFSET(pin)			(pin % MAX_GPIO_PER_REG)
#define REG_OFFSET(base, reg, pin)	(base + reg + (pin / MAX_GPIO_PER_REG) \
							* sizeof(int *))

/*
 * plgpio pins in all machines are not one to one mapped, bitwise with registers
 * bits. These set of macros define register masks for which below functions
 * (pin_to_offset and offset_to_pin) are required to be called.
 */
#define PTO_ENB_REG		0x001
#define PTO_WDATA_REG		0x002
#define PTO_DIR_REG		0x004
#define PTO_IE_REG		0x008
#define PTO_RDATA_REG		0x010
#define PTO_MIS_REG		0x020

struct plgpio_regs {
	u32 enb;		/* enable register */
	u32 wdata;		/* write data register */
	u32 dir;		/* direction set register */
	u32 rdata;		/* read data register */
	u32 ie;			/* interrupt enable register */
	u32 mis;		/* mask interrupt status register */
	u32 eit;		/* edge interrupt type */
};

/*
 * struct plgpio: plgpio driver specific structure
 *
 * lock: lock for guarding gpio registers
 * base: base address of plgpio block
 * irq_base: irq number of plgpio0
 * chip: gpio framework specific chip information structure
 * p2o: function ptr for pin to offset conversion. This is required only for
 *	machines where mapping b/w pin and offset is not 1-to-1.
 * o2p: function ptr for offset to pin conversion. This is required only for
 *	machines where mapping b/w pin and offset is not 1-to-1.
 * p2o_regs: mask of registers for which p2o and o2p are applicable
 * regs: register offsets
 * csave_regs: context save registers for standby/sleep/hibernate cases
 */
struct plgpio {
	spinlock_t		lock;
	void __iomem		*base;
	struct clk		*clk;
	unsigned		irq_base;
	struct irq_domain	*irq_domain;
	struct gpio_chip	chip;
	int			(*p2o)(int pin);	/* pin_to_offset */
	int			(*o2p)(int offset);	/* offset_to_pin */
	u32			p2o_regs;
	struct plgpio_regs	regs;
#ifdef CONFIG_PM
	struct plgpio_regs	*csave_regs;
#endif
};

/* register manipulation inline functions */
static inline u32 is_plgpio_set(void __iomem *base, u32 pin, u32 reg)
{
	u32 offset = PIN_OFFSET(pin);
	void __iomem *reg_off = REG_OFFSET(base, reg, pin);
	u32 val = readl_relaxed(reg_off);

	return !!(val & (1 << offset));
}

static inline void plgpio_reg_set(void __iomem *base, u32 pin, u32 reg)
{
	u32 offset = PIN_OFFSET(pin);
	void __iomem *reg_off = REG_OFFSET(base, reg, pin);
	u32 val = readl_relaxed(reg_off);

	writel_relaxed(val | (1 << offset), reg_off);
}

static inline void plgpio_reg_reset(void __iomem *base, u32 pin, u32 reg)
{
	u32 offset = PIN_OFFSET(pin);
	void __iomem *reg_off = REG_OFFSET(base, reg, pin);
	u32 val = readl_relaxed(reg_off);

	writel_relaxed(val & ~(1 << offset), reg_off);
}

/* gpio framework specific routines */
static int plgpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	struct plgpio *plgpio = container_of(chip, struct plgpio, chip);
	unsigned long flags;

	/* get correct offset for "offset" pin */
	if (plgpio->p2o && (plgpio->p2o_regs & PTO_DIR_REG)) {
		offset = plgpio->p2o(offset);
		if (offset == -1)
			return -EINVAL;
	}

	spin_lock_irqsave(&plgpio->lock, flags);
	plgpio_reg_set(plgpio->base, offset, plgpio->regs.dir);
	spin_unlock_irqrestore(&plgpio->lock, flags);

	return 0;
}

static int plgpio_direction_output(struct gpio_chip *chip, unsigned offset,
		int value)
{
	struct plgpio *plgpio = container_of(chip, struct plgpio, chip);
	unsigned long flags;
	unsigned dir_offset = offset, wdata_offset = offset, tmp;

	/* get correct offset for "offset" pin */
	if (plgpio->p2o && (plgpio->p2o_regs & (PTO_DIR_REG | PTO_WDATA_REG))) {
		tmp = plgpio->p2o(offset);
		if (tmp == -1)
			return -EINVAL;

		if (plgpio->p2o_regs & PTO_DIR_REG)
			dir_offset = tmp;
		if (plgpio->p2o_regs & PTO_WDATA_REG)
			wdata_offset = tmp;
	}

	spin_lock_irqsave(&plgpio->lock, flags);
	if (value)
		plgpio_reg_set(plgpio->base, wdata_offset,
				plgpio->regs.wdata);
	else
		plgpio_reg_reset(plgpio->base, wdata_offset,
				plgpio->regs.wdata);

	plgpio_reg_reset(plgpio->base, dir_offset, plgpio->regs.dir);
	spin_unlock_irqrestore(&plgpio->lock, flags);

	return 0;
}

static int plgpio_get_value(struct gpio_chip *chip, unsigned offset)
{
	struct plgpio *plgpio = container_of(chip, struct plgpio, chip);

	if (offset >= chip->ngpio)
		return -EINVAL;

	/* get correct offset for "offset" pin */
	if (plgpio->p2o && (plgpio->p2o_regs & PTO_RDATA_REG)) {
		offset = plgpio->p2o(offset);
		if (offset == -1)
			return -EINVAL;
	}

	return is_plgpio_set(plgpio->base, offset, plgpio->regs.rdata);
}

static void plgpio_set_value(struct gpio_chip *chip, unsigned offset, int value)
{
	struct plgpio *plgpio = container_of(chip, struct plgpio, chip);

	if (offset >= chip->ngpio)
		return;

	/* get correct offset for "offset" pin */
	if (plgpio->p2o && (plgpio->p2o_regs & PTO_WDATA_REG)) {
		offset = plgpio->p2o(offset);
		if (offset == -1)
			return;
	}

	if (value)
		plgpio_reg_set(plgpio->base, offset, plgpio->regs.wdata);
	else
		plgpio_reg_reset(plgpio->base, offset, plgpio->regs.wdata);
}

static int plgpio_request(struct gpio_chip *chip, unsigned offset)
{
	struct plgpio *plgpio = container_of(chip, struct plgpio, chip);
	int gpio = chip->base + offset;
	unsigned long flags;
	int ret = 0;

	if (offset >= chip->ngpio)
		return -EINVAL;

	ret = pinctrl_request_gpio(gpio);
	if (ret)
		return ret;

	if (!IS_ERR(plgpio->clk)) {
		ret = clk_enable(plgpio->clk);
		if (ret)
			goto err0;
	}

	if (plgpio->regs.enb == -1)
		return 0;

	/*
	 * put gpio in IN mode before enabling it. This make enabling gpio safe
	 */
	ret = plgpio_direction_input(chip, offset);
	if (ret)
		goto err1;

	/* get correct offset for "offset" pin */
	if (plgpio->p2o && (plgpio->p2o_regs & PTO_ENB_REG)) {
		offset = plgpio->p2o(offset);
		if (offset == -1) {
			ret = -EINVAL;
			goto err1;
		}
	}

	spin_lock_irqsave(&plgpio->lock, flags);
	plgpio_reg_set(plgpio->base, offset, plgpio->regs.enb);
	spin_unlock_irqrestore(&plgpio->lock, flags);
	return 0;

err1:
	if (!IS_ERR(plgpio->clk))
		clk_disable(plgpio->clk);
err0:
	pinctrl_free_gpio(gpio);
	return ret;
}

static void plgpio_free(struct gpio_chip *chip, unsigned offset)
{
	struct plgpio *plgpio = container_of(chip, struct plgpio, chip);
	int gpio = chip->base + offset;
	unsigned long flags;

	if (offset >= chip->ngpio)
		return;

	if (plgpio->regs.enb == -1)
		goto disable_clk;

	/* get correct offset for "offset" pin */
	if (plgpio->p2o && (plgpio->p2o_regs & PTO_ENB_REG)) {
		offset = plgpio->p2o(offset);
		if (offset == -1)
			return;
	}

	spin_lock_irqsave(&plgpio->lock, flags);
	plgpio_reg_reset(plgpio->base, offset, plgpio->regs.enb);
	spin_unlock_irqrestore(&plgpio->lock, flags);

disable_clk:
	if (!IS_ERR(plgpio->clk))
		clk_disable(plgpio->clk);

	pinctrl_free_gpio(gpio);
}

static int plgpio_to_irq(struct gpio_chip *chip, unsigned offset)
{
	struct plgpio *plgpio = container_of(chip, struct plgpio, chip);

	if (plgpio->irq_base < 0)
		return -EINVAL;

	return irq_find_mapping(plgpio->irq_domain, offset);
}

/* PLGPIO IRQ */
static void plgpio_irq_disable(struct irq_data *d)
{
	struct plgpio *plgpio = irq_data_get_irq_chip_data(d);
	int offset = d->irq - plgpio->irq_base;
	unsigned long flags;

	/* get correct offset for "offset" pin */
	if (plgpio->p2o && (plgpio->p2o_regs & PTO_IE_REG)) {
		offset = plgpio->p2o(offset);
		if (offset == -1)
			return;
	}

	spin_lock_irqsave(&plgpio->lock, flags);
	plgpio_reg_set(plgpio->base, offset, plgpio->regs.ie);
	spin_unlock_irqrestore(&plgpio->lock, flags);
}

static void plgpio_irq_enable(struct irq_data *d)
{
	struct plgpio *plgpio = irq_data_get_irq_chip_data(d);
	int offset = d->irq - plgpio->irq_base;
	unsigned long flags;

	/* get correct offset for "offset" pin */
	if (plgpio->p2o && (plgpio->p2o_regs & PTO_IE_REG)) {
		offset = plgpio->p2o(offset);
		if (offset == -1)
			return;
	}

	spin_lock_irqsave(&plgpio->lock, flags);
	plgpio_reg_reset(plgpio->base, offset, plgpio->regs.ie);
	spin_unlock_irqrestore(&plgpio->lock, flags);
}

static int plgpio_irq_set_type(struct irq_data *d, unsigned trigger)
{
	struct plgpio *plgpio = irq_data_get_irq_chip_data(d);
	int offset = d->irq - plgpio->irq_base;
	void __iomem *reg_off;
	unsigned int supported_type = 0, val;

	if (offset >= plgpio->chip.ngpio)
		return -EINVAL;

	if (plgpio->regs.eit == -1)
		supported_type = IRQ_TYPE_LEVEL_HIGH;
	else
		supported_type = IRQ_TYPE_EDGE_RISING | IRQ_TYPE_EDGE_FALLING;

	if (!(trigger & supported_type))
		return -EINVAL;

	if (plgpio->regs.eit == -1)
		return 0;

	reg_off = REG_OFFSET(plgpio->base, plgpio->regs.eit, offset);
	val = readl_relaxed(reg_off);

	offset = PIN_OFFSET(offset);
	if (trigger & IRQ_TYPE_EDGE_RISING)
		writel_relaxed(val | (1 << offset), reg_off);
	else
		writel_relaxed(val & ~(1 << offset), reg_off);

	return 0;
}

static struct irq_chip plgpio_irqchip = {
	.name		= "PLGPIO",
	.irq_enable	= plgpio_irq_enable,
	.irq_disable	= plgpio_irq_disable,
	.irq_set_type	= plgpio_irq_set_type,
};

static void plgpio_irq_handler(unsigned irq, struct irq_desc *desc)
{
	struct plgpio *plgpio = irq_get_handler_data(irq);
	struct irq_chip *irqchip = irq_desc_get_chip(desc);
	int regs_count, count, pin, offset, i = 0;
	unsigned long pending;

	count = plgpio->chip.ngpio;
	regs_count = DIV_ROUND_UP(count, MAX_GPIO_PER_REG);

	chained_irq_enter(irqchip, desc);
	/* check all plgpio MIS registers for a possible interrupt */
	for (; i < regs_count; i++) {
		pending = readl_relaxed(plgpio->base + plgpio->regs.mis +
				i * sizeof(int *));
		if (!pending)
			continue;

		/* clear interrupts */
		writel_relaxed(~pending, plgpio->base + plgpio->regs.mis +
				i * sizeof(int *));
		/*
		 * clear extra bits in last register having gpios < MAX/REG
		 * ex: Suppose there are max 102 plgpios. then last register
		 * must have only (102 - MAX_GPIO_PER_REG * 3) = 6 relevant bits
		 * so, we must not take other 28 bits into consideration for
		 * checking interrupt. so clear those bits.
		 */
		count = count - i * MAX_GPIO_PER_REG;
		if (count < MAX_GPIO_PER_REG)
			pending &= (1 << count) - 1;

		for_each_set_bit(offset, &pending, MAX_GPIO_PER_REG) {
			/* get correct pin for "offset" */
			if (plgpio->o2p && (plgpio->p2o_regs & PTO_MIS_REG)) {
				pin = plgpio->o2p(offset);
				if (pin == -1)
					continue;
			} else
				pin = offset;

			/* get correct irq line number */
			pin = i * MAX_GPIO_PER_REG + pin;
			generic_handle_irq(plgpio_to_irq(&plgpio->chip, pin));
		}
	}
	chained_irq_exit(irqchip, desc);
}

/*
 * pin to offset and offset to pin converter functions
 *
 * In spear310 there is inconsistency among bit positions in plgpio regiseters,
 * for different plgpio pins. For example: for pin 27, bit offset is 23, pin
 * 28-33 are not supported, pin 95 has offset bit 95, bit 100 has offset bit 1
 */
static int spear310_p2o(int pin)
{
	int offset = pin;

	if (pin <= 27)
		offset += 4;
	else if (pin <= 33)
		offset = -1;
	else if (pin <= 97)
		offset -= 2;
	else if (pin <= 101)
		offset = 101 - pin;
	else
		offset = -1;

	return offset;
}

int spear310_o2p(int offset)
{
	if (offset <= 3)
		return 101 - offset;
	else if (offset <= 31)
		return offset - 4;
	else
		return offset + 2;
}

static int __devinit plgpio_probe_dt(struct platform_device *pdev,
		struct plgpio *plgpio)
{
	struct device_node *np = pdev->dev.of_node;
	int ret = -EINVAL;
	u32 val;

	if (of_machine_is_compatible("st,spear310")) {
		plgpio->p2o = spear310_p2o;
		plgpio->o2p = spear310_o2p;
		plgpio->p2o_regs = PTO_WDATA_REG | PTO_DIR_REG | PTO_IE_REG |
			PTO_RDATA_REG | PTO_MIS_REG;
	}

	if (!of_property_read_u32(np, "st-plgpio,ngpio", &val)) {
		plgpio->chip.ngpio = val;
	} else {
		dev_err(&pdev->dev, "DT: Invalid ngpio field\n");
		goto end;
	}

	if (!of_property_read_u32(np, "st-plgpio,enb-reg", &val))
		plgpio->regs.enb = val;
	else
		plgpio->regs.enb = -1;

	if (!of_property_read_u32(np, "st-plgpio,wdata-reg", &val)) {
		plgpio->regs.wdata = val;
	} else {
		dev_err(&pdev->dev, "DT: Invalid wdata reg\n");
		goto end;
	}

	if (!of_property_read_u32(np, "st-plgpio,dir-reg", &val)) {
		plgpio->regs.dir = val;
	} else {
		dev_err(&pdev->dev, "DT: Invalid dir reg\n");
		goto end;
	}

	if (!of_property_read_u32(np, "st-plgpio,ie-reg", &val)) {
		plgpio->regs.ie = val;
	} else {
		dev_err(&pdev->dev, "DT: Invalid ie reg\n");
		goto end;
	}

	if (!of_property_read_u32(np, "st-plgpio,rdata-reg", &val)) {
		plgpio->regs.rdata = val;
	} else {
		dev_err(&pdev->dev, "DT: Invalid rdata reg\n");
		goto end;
	}

	if (!of_property_read_u32(np, "st-plgpio,mis-reg", &val)) {
		plgpio->regs.mis = val;
	} else {
		dev_err(&pdev->dev, "DT: Invalid mis reg\n");
		goto end;
	}

	if (!of_property_read_u32(np, "st-plgpio,eit-reg", &val))
		plgpio->regs.eit = val;
	else
		plgpio->regs.eit = -1;

	return 0;

end:
	return ret;
}
static int __devinit plgpio_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct plgpio *plgpio;
	struct resource *res;
	int ret, irq, i;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "invalid IORESOURCE_MEM\n");
		return -EBUSY;
	}

	plgpio = devm_kzalloc(&pdev->dev, sizeof(*plgpio), GFP_KERNEL);
	if (!plgpio) {
		dev_err(&pdev->dev, "memory allocation fail\n");
		return -ENOMEM;
	}

	plgpio->base = devm_request_and_ioremap(&pdev->dev, res);
	if (!plgpio->base) {
		dev_err(&pdev->dev, "request and ioremap fail\n");
		return -ENOMEM;
	}

	ret = plgpio_probe_dt(pdev, plgpio);
	if (ret) {
		dev_err(&pdev->dev, "DT probe failed\n");
		return ret;
	}

	plgpio->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(plgpio->clk))
		dev_warn(&pdev->dev, "clk_get() failed, work without it\n");

#ifdef CONFIG_PM
	plgpio->csave_regs = devm_kzalloc(&pdev->dev,
			sizeof(*plgpio->csave_regs) *
			DIV_ROUND_UP(plgpio->chip.ngpio, MAX_GPIO_PER_REG),
			GFP_KERNEL);
	if (!plgpio->csave_regs) {
		dev_err(&pdev->dev, "csave registers memory allocation fail\n");
		return -ENOMEM;
	}
#endif

	platform_set_drvdata(pdev, plgpio);
	spin_lock_init(&plgpio->lock);

	plgpio->irq_base = -1;
	plgpio->chip.base = -1;
	plgpio->chip.request = plgpio_request;
	plgpio->chip.free = plgpio_free;
	plgpio->chip.direction_input = plgpio_direction_input;
	plgpio->chip.direction_output = plgpio_direction_output;
	plgpio->chip.get = plgpio_get_value;
	plgpio->chip.set = plgpio_set_value;
	plgpio->chip.to_irq = plgpio_to_irq;
	plgpio->chip.label = dev_name(&pdev->dev);
	plgpio->chip.dev = &pdev->dev;
	plgpio->chip.owner = THIS_MODULE;

	if (!IS_ERR(plgpio->clk)) {
		ret = clk_prepare(plgpio->clk);
		if (ret) {
			dev_err(&pdev->dev, "clk prepare failed\n");
			return ret;
		}
	}

	ret = gpiochip_add(&plgpio->chip);
	if (ret) {
		dev_err(&pdev->dev, "unable to add gpio chip\n");
		goto unprepare_clk;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_info(&pdev->dev, "irqs not supported\n");
		return 0;
	}

	plgpio->irq_base = irq_alloc_descs(-1, 0, plgpio->chip.ngpio, 0);
	if (IS_ERR_VALUE(plgpio->irq_base)) {
		/* we would not support irq for gpio */
		dev_warn(&pdev->dev, "couldn't allocate irq base\n");
		return 0;
	}

	plgpio->irq_domain = irq_domain_add_legacy(np, plgpio->chip.ngpio,
			plgpio->irq_base, 0, &irq_domain_simple_ops, NULL);
	if (WARN_ON(!plgpio->irq_domain)) {
		dev_err(&pdev->dev, "irq domain init failed\n");
		irq_free_descs(plgpio->irq_base, plgpio->chip.ngpio);
		ret = -ENXIO;
		goto remove_gpiochip;
	}

	irq_set_chained_handler(irq, plgpio_irq_handler);
	for (i = 0; i < plgpio->chip.ngpio; i++) {
		irq_set_chip_and_handler(i + plgpio->irq_base, &plgpio_irqchip,
				handle_simple_irq);
		set_irq_flags(i + plgpio->irq_base, IRQF_VALID);
		irq_set_chip_data(i + plgpio->irq_base, plgpio);
	}

	irq_set_handler_data(irq, plgpio);
	dev_info(&pdev->dev, "PLGPIO registered with IRQs\n");

	return 0;

remove_gpiochip:
	dev_info(&pdev->dev, "Remove gpiochip\n");
	if (gpiochip_remove(&plgpio->chip))
		dev_err(&pdev->dev, "unable to remove gpiochip\n");
unprepare_clk:
	if (!IS_ERR(plgpio->clk))
		clk_unprepare(plgpio->clk);

	return ret;
}

#ifdef CONFIG_PM
static int plgpio_suspend(struct device *dev)
{
	struct plgpio *plgpio = dev_get_drvdata(dev);
	int i, reg_count = DIV_ROUND_UP(plgpio->chip.ngpio, MAX_GPIO_PER_REG);
	void __iomem *off;

	for (i = 0; i < reg_count; i++) {
		off = plgpio->base + i * sizeof(int *);

		if (plgpio->regs.enb != -1)
			plgpio->csave_regs[i].enb =
				readl_relaxed(plgpio->regs.enb + off);
		if (plgpio->regs.eit != -1)
			plgpio->csave_regs[i].eit =
				readl_relaxed(plgpio->regs.eit + off);
		plgpio->csave_regs[i].wdata = readl_relaxed(plgpio->regs.wdata +
				off);
		plgpio->csave_regs[i].dir = readl_relaxed(plgpio->regs.dir +
				off);
		plgpio->csave_regs[i].ie = readl_relaxed(plgpio->regs.ie + off);
	}

	return 0;
}

/*
 * This is used to correct the values in end registers. End registers contain
 * extra bits that might be used for other purpose in platform. So, we shouldn't
 * overwrite these bits. This macro, reads given register again, preserves other
 * bit values (non-plgpio bits), and retain captured value (plgpio bits).
 */
#define plgpio_prepare_reg(__reg, _off, _mask, _tmp)		\
{								\
	_tmp = readl_relaxed(plgpio->regs.__reg + _off);		\
	_tmp &= ~_mask;						\
	plgpio->csave_regs[i].__reg =				\
		_tmp | (plgpio->csave_regs[i].__reg & _mask);	\
}

static int plgpio_resume(struct device *dev)
{
	struct plgpio *plgpio = dev_get_drvdata(dev);
	int i, reg_count = DIV_ROUND_UP(plgpio->chip.ngpio, MAX_GPIO_PER_REG);
	void __iomem *off;
	u32 mask, tmp;

	for (i = 0; i < reg_count; i++) {
		off = plgpio->base + i * sizeof(int *);

		if (i == reg_count - 1) {
			mask = (1 << (plgpio->chip.ngpio - i *
						MAX_GPIO_PER_REG)) - 1;

			if (plgpio->regs.enb != -1)
				plgpio_prepare_reg(enb, off, mask, tmp);

			if (plgpio->regs.eit != -1)
				plgpio_prepare_reg(eit, off, mask, tmp);

			plgpio_prepare_reg(wdata, off, mask, tmp);
			plgpio_prepare_reg(dir, off, mask, tmp);
			plgpio_prepare_reg(ie, off, mask, tmp);
		}

		writel_relaxed(plgpio->csave_regs[i].wdata, plgpio->regs.wdata +
				off);
		writel_relaxed(plgpio->csave_regs[i].dir, plgpio->regs.dir +
				off);

		if (plgpio->regs.eit != -1)
			writel_relaxed(plgpio->csave_regs[i].eit,
					plgpio->regs.eit + off);

		writel_relaxed(plgpio->csave_regs[i].ie, plgpio->regs.ie + off);

		if (plgpio->regs.enb != -1)
			writel_relaxed(plgpio->csave_regs[i].enb,
					plgpio->regs.enb + off);
	}

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(plgpio_dev_pm_ops, plgpio_suspend, plgpio_resume);

static const struct of_device_id plgpio_of_match[] = {
	{ .compatible = "st,spear-plgpio" },
	{}
};
MODULE_DEVICE_TABLE(of, plgpio_of_match);

static struct platform_driver plgpio_driver = {
	.probe = plgpio_probe,
	.driver = {
		.owner = THIS_MODULE,
		.name = "spear-plgpio",
		.pm = &plgpio_dev_pm_ops,
		.of_match_table = of_match_ptr(plgpio_of_match),
	},
};

static int __init plgpio_init(void)
{
	return platform_driver_register(&plgpio_driver);
}
subsys_initcall(plgpio_init);

MODULE_AUTHOR("Viresh Kumar <viresh.kumar@linaro.org>");
MODULE_DESCRIPTION("ST Microlectronics SPEAr PLGPIO driver");
MODULE_LICENSE("GPL");
