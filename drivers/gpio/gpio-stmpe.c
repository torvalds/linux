/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License Terms: GNU General Public License, version 2
 * Author: Rabin Vincent <rabin.vincent@stericsson.com> for ST-Ericsson
 */

#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/mfd/stmpe.h>
#include <linux/seq_file.h>
#include <linux/bitops.h>

/*
 * These registers are modified under the irq bus lock and cached to avoid
 * unnecessary writes in bus_sync_unlock.
 */
enum { REG_RE, REG_FE, REG_IE };

enum { LSB, CSB, MSB };

#define CACHE_NR_REGS	3
/* No variant has more than 24 GPIOs */
#define CACHE_NR_BANKS	(24 / 8)

struct stmpe_gpio {
	struct gpio_chip chip;
	struct stmpe *stmpe;
	struct device *dev;
	struct mutex irq_lock;
	u32 norequest_mask;
	/* Caches of interrupt control registers for bus_lock */
	u8 regs[CACHE_NR_REGS][CACHE_NR_BANKS];
	u8 oldregs[CACHE_NR_REGS][CACHE_NR_BANKS];
};

static int stmpe_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct stmpe_gpio *stmpe_gpio = gpiochip_get_data(chip);
	struct stmpe *stmpe = stmpe_gpio->stmpe;
	u8 reg = stmpe->regs[STMPE_IDX_GPMR_LSB + (offset / 8)];
	u8 mask = BIT(offset % 8);
	int ret;

	ret = stmpe_reg_read(stmpe, reg);
	if (ret < 0)
		return ret;

	return !!(ret & mask);
}

static void stmpe_gpio_set(struct gpio_chip *chip, unsigned offset, int val)
{
	struct stmpe_gpio *stmpe_gpio = gpiochip_get_data(chip);
	struct stmpe *stmpe = stmpe_gpio->stmpe;
	int which = val ? STMPE_IDX_GPSR_LSB : STMPE_IDX_GPCR_LSB;
	u8 reg = stmpe->regs[which + (offset / 8)];
	u8 mask = BIT(offset % 8);

	/*
	 * Some variants have single register for gpio set/clear functionality.
	 * For them we need to write 0 to clear and 1 to set.
	 */
	if (stmpe->regs[STMPE_IDX_GPSR_LSB] == stmpe->regs[STMPE_IDX_GPCR_LSB])
		stmpe_set_bits(stmpe, reg, mask, val ? mask : 0);
	else
		stmpe_reg_write(stmpe, reg, mask);
}

static int stmpe_gpio_get_direction(struct gpio_chip *chip,
				    unsigned offset)
{
	struct stmpe_gpio *stmpe_gpio = gpiochip_get_data(chip);
	struct stmpe *stmpe = stmpe_gpio->stmpe;
	u8 reg = stmpe->regs[STMPE_IDX_GPDR_LSB] - (offset / 8);
	u8 mask = BIT(offset % 8);
	int ret;

	ret = stmpe_reg_read(stmpe, reg);
	if (ret < 0)
		return ret;

	return !(ret & mask);
}

static int stmpe_gpio_direction_output(struct gpio_chip *chip,
					 unsigned offset, int val)
{
	struct stmpe_gpio *stmpe_gpio = gpiochip_get_data(chip);
	struct stmpe *stmpe = stmpe_gpio->stmpe;
	u8 reg = stmpe->regs[STMPE_IDX_GPDR_LSB + (offset / 8)];
	u8 mask = BIT(offset % 8);

	stmpe_gpio_set(chip, offset, val);

	return stmpe_set_bits(stmpe, reg, mask, mask);
}

static int stmpe_gpio_direction_input(struct gpio_chip *chip,
					unsigned offset)
{
	struct stmpe_gpio *stmpe_gpio = gpiochip_get_data(chip);
	struct stmpe *stmpe = stmpe_gpio->stmpe;
	u8 reg = stmpe->regs[STMPE_IDX_GPDR_LSB + (offset / 8)];
	u8 mask = BIT(offset % 8);

	return stmpe_set_bits(stmpe, reg, mask, 0);
}

static int stmpe_gpio_request(struct gpio_chip *chip, unsigned offset)
{
	struct stmpe_gpio *stmpe_gpio = gpiochip_get_data(chip);
	struct stmpe *stmpe = stmpe_gpio->stmpe;

	if (stmpe_gpio->norequest_mask & BIT(offset))
		return -EINVAL;

	return stmpe_set_altfunc(stmpe, BIT(offset), STMPE_BLOCK_GPIO);
}

static const struct gpio_chip template_chip = {
	.label			= "stmpe",
	.owner			= THIS_MODULE,
	.get_direction		= stmpe_gpio_get_direction,
	.direction_input	= stmpe_gpio_direction_input,
	.get			= stmpe_gpio_get,
	.direction_output	= stmpe_gpio_direction_output,
	.set			= stmpe_gpio_set,
	.request		= stmpe_gpio_request,
	.can_sleep		= true,
};

static int stmpe_gpio_irq_set_type(struct irq_data *d, unsigned int type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct stmpe_gpio *stmpe_gpio = gpiochip_get_data(gc);
	int offset = d->hwirq;
	int regoffset = offset / 8;
	int mask = BIT(offset % 8);

	if (type & IRQ_TYPE_LEVEL_LOW || type & IRQ_TYPE_LEVEL_HIGH)
		return -EINVAL;

	/* STMPE801 and STMPE 1600 don't have RE and FE registers */
	if (stmpe_gpio->stmpe->partnum == STMPE801 ||
	    stmpe_gpio->stmpe->partnum == STMPE1600)
		return 0;

	if (type & IRQ_TYPE_EDGE_RISING)
		stmpe_gpio->regs[REG_RE][regoffset] |= mask;
	else
		stmpe_gpio->regs[REG_RE][regoffset] &= ~mask;

	if (type & IRQ_TYPE_EDGE_FALLING)
		stmpe_gpio->regs[REG_FE][regoffset] |= mask;
	else
		stmpe_gpio->regs[REG_FE][regoffset] &= ~mask;

	return 0;
}

static void stmpe_gpio_irq_lock(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct stmpe_gpio *stmpe_gpio = gpiochip_get_data(gc);

	mutex_lock(&stmpe_gpio->irq_lock);
}

static void stmpe_gpio_irq_sync_unlock(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct stmpe_gpio *stmpe_gpio = gpiochip_get_data(gc);
	struct stmpe *stmpe = stmpe_gpio->stmpe;
	int num_banks = DIV_ROUND_UP(stmpe->num_gpios, 8);
	static const u8 regmap[CACHE_NR_REGS][CACHE_NR_BANKS] = {
		[REG_RE][LSB] = STMPE_IDX_GPRER_LSB,
		[REG_RE][CSB] = STMPE_IDX_GPRER_CSB,
		[REG_RE][MSB] = STMPE_IDX_GPRER_MSB,
		[REG_FE][LSB] = STMPE_IDX_GPFER_LSB,
		[REG_FE][CSB] = STMPE_IDX_GPFER_CSB,
		[REG_FE][MSB] = STMPE_IDX_GPFER_MSB,
		[REG_IE][LSB] = STMPE_IDX_IEGPIOR_LSB,
		[REG_IE][CSB] = STMPE_IDX_IEGPIOR_CSB,
		[REG_IE][MSB] = STMPE_IDX_IEGPIOR_MSB,
	};
	int i, j;

	/*
	 * STMPE1600: to be able to get IRQ from pins,
	 * a read must be done on GPMR register, or a write in
	 * GPSR or GPCR registers
	 */
	if (stmpe->partnum == STMPE1600) {
		stmpe_reg_read(stmpe, stmpe->regs[STMPE_IDX_GPMR_LSB]);
		stmpe_reg_read(stmpe, stmpe->regs[STMPE_IDX_GPMR_CSB]);
	}

	for (i = 0; i < CACHE_NR_REGS; i++) {
		/* STMPE801 and STMPE1600 don't have RE and FE registers */
		if ((stmpe->partnum == STMPE801 ||
		     stmpe->partnum == STMPE1600) &&
		     (i != REG_IE))
			continue;

		for (j = 0; j < num_banks; j++) {
			u8 old = stmpe_gpio->oldregs[i][j];
			u8 new = stmpe_gpio->regs[i][j];

			if (new == old)
				continue;

			stmpe_gpio->oldregs[i][j] = new;
			stmpe_reg_write(stmpe, stmpe->regs[regmap[i][j]], new);
		}
	}

	mutex_unlock(&stmpe_gpio->irq_lock);
}

static void stmpe_gpio_irq_mask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct stmpe_gpio *stmpe_gpio = gpiochip_get_data(gc);
	int offset = d->hwirq;
	int regoffset = offset / 8;
	int mask = BIT(offset % 8);

	stmpe_gpio->regs[REG_IE][regoffset] &= ~mask;
}

static void stmpe_gpio_irq_unmask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct stmpe_gpio *stmpe_gpio = gpiochip_get_data(gc);
	int offset = d->hwirq;
	int regoffset = offset / 8;
	int mask = BIT(offset % 8);

	stmpe_gpio->regs[REG_IE][regoffset] |= mask;
}

static void stmpe_dbg_show_one(struct seq_file *s,
			       struct gpio_chip *gc,
			       unsigned offset, unsigned gpio)
{
	struct stmpe_gpio *stmpe_gpio = gpiochip_get_data(gc);
	struct stmpe *stmpe = stmpe_gpio->stmpe;
	const char *label = gpiochip_is_requested(gc, offset);
	bool val = !!stmpe_gpio_get(gc, offset);
	u8 bank = offset / 8;
	u8 dir_reg = stmpe->regs[STMPE_IDX_GPDR_LSB + bank];
	u8 mask = BIT(offset % 8);
	int ret;
	u8 dir;

	ret = stmpe_reg_read(stmpe, dir_reg);
	if (ret < 0)
		return;
	dir = !!(ret & mask);

	if (dir) {
		seq_printf(s, " gpio-%-3d (%-20.20s) out %s",
			   gpio, label ?: "(none)",
			   val ? "hi" : "lo");
	} else {
		u8 edge_det_reg;
		u8 rise_reg;
		u8 fall_reg;
		u8 irqen_reg;

		char *edge_det_values[] = {"edge-inactive",
					   "edge-asserted",
					   "not-supported"};
		char *rise_values[] = {"no-rising-edge-detection",
				       "rising-edge-detection",
				       "not-supported"};
		char *fall_values[] = {"no-falling-edge-detection",
				       "falling-edge-detection",
				       "not-supported"};
		#define NOT_SUPPORTED_IDX 2
		u8 edge_det = NOT_SUPPORTED_IDX;
		u8 rise = NOT_SUPPORTED_IDX;
		u8 fall = NOT_SUPPORTED_IDX;
		bool irqen;

		switch (stmpe->partnum) {
		case STMPE610:
		case STMPE811:
		case STMPE1601:
		case STMPE2401:
		case STMPE2403:
			edge_det_reg = stmpe->regs[STMPE_IDX_GPEDR_LSB + bank];
			ret = stmpe_reg_read(stmpe, edge_det_reg);
			if (ret < 0)
				return;
			edge_det = !!(ret & mask);

		case STMPE1801:
			rise_reg = stmpe->regs[STMPE_IDX_GPRER_LSB + bank];
			fall_reg = stmpe->regs[STMPE_IDX_GPFER_LSB + bank];

			ret = stmpe_reg_read(stmpe, rise_reg);
			if (ret < 0)
				return;
			rise = !!(ret & mask);
			ret = stmpe_reg_read(stmpe, fall_reg);
			if (ret < 0)
				return;
			fall = !!(ret & mask);

		case STMPE801:
		case STMPE1600:
			irqen_reg = stmpe->regs[STMPE_IDX_IEGPIOR_LSB + bank];
			break;

		default:
			return;
		}

		ret = stmpe_reg_read(stmpe, irqen_reg);
		if (ret < 0)
			return;
		irqen = !!(ret & mask);

		seq_printf(s, " gpio-%-3d (%-20.20s) in  %s %13s %13s %25s %25s",
			   gpio, label ?: "(none)",
			   val ? "hi" : "lo",
			   edge_det_values[edge_det],
			   irqen ? "IRQ-enabled" : "IRQ-disabled",
			   rise_values[rise],
			   fall_values[fall]);
	}
}

static void stmpe_dbg_show(struct seq_file *s, struct gpio_chip *gc)
{
	unsigned i;
	unsigned gpio = gc->base;

	for (i = 0; i < gc->ngpio; i++, gpio++) {
		stmpe_dbg_show_one(s, gc, i, gpio);
		seq_printf(s, "\n");
	}
}

static struct irq_chip stmpe_gpio_irq_chip = {
	.name			= "stmpe-gpio",
	.irq_bus_lock		= stmpe_gpio_irq_lock,
	.irq_bus_sync_unlock	= stmpe_gpio_irq_sync_unlock,
	.irq_mask		= stmpe_gpio_irq_mask,
	.irq_unmask		= stmpe_gpio_irq_unmask,
	.irq_set_type		= stmpe_gpio_irq_set_type,
};

static irqreturn_t stmpe_gpio_irq(int irq, void *dev)
{
	struct stmpe_gpio *stmpe_gpio = dev;
	struct stmpe *stmpe = stmpe_gpio->stmpe;
	u8 statmsbreg;
	int num_banks = DIV_ROUND_UP(stmpe->num_gpios, 8);
	u8 status[num_banks];
	int ret;
	int i;

	/*
	 * the stmpe_block_read() call below, imposes to set statmsbreg
	 * with the register located at the lowest address. As STMPE1600
	 * variant is the only one which respect registers address's order
	 * (LSB regs located at lowest address than MSB ones) whereas all
	 * the others have a registers layout with MSB located before the
	 * LSB regs.
	 */
	if (stmpe->partnum == STMPE1600)
		statmsbreg = stmpe->regs[STMPE_IDX_ISGPIOR_LSB];
	else
		statmsbreg = stmpe->regs[STMPE_IDX_ISGPIOR_MSB];

	ret = stmpe_block_read(stmpe, statmsbreg, num_banks, status);
	if (ret < 0)
		return IRQ_NONE;

	for (i = 0; i < num_banks; i++) {
		int bank = (stmpe_gpio->stmpe->partnum == STMPE1600) ? i :
			   num_banks - i - 1;
		unsigned int enabled = stmpe_gpio->regs[REG_IE][bank];
		unsigned int stat = status[i];

		stat &= enabled;
		if (!stat)
			continue;

		while (stat) {
			int bit = __ffs(stat);
			int line = bank * 8 + bit;
			int child_irq = irq_find_mapping(stmpe_gpio->chip.irqdomain,
							 line);

			handle_nested_irq(child_irq);
			stat &= ~BIT(bit);
		}

		/*
		 * interrupt status register write has no effect on
		 * 801/1801/1600, bits are cleared when read.
		 * Edge detect register is not present on 801/1600/1801
		 */
		if (stmpe->partnum != STMPE801 && stmpe->partnum != STMPE1600 &&
		    stmpe->partnum != STMPE1801) {
			stmpe_reg_write(stmpe, statmsbreg + i, status[i]);
			stmpe_reg_write(stmpe,
					stmpe->regs[STMPE_IDX_GPEDR_MSB] + i,
					status[i]);
		}
	}

	return IRQ_HANDLED;
}

static int stmpe_gpio_probe(struct platform_device *pdev)
{
	struct stmpe *stmpe = dev_get_drvdata(pdev->dev.parent);
	struct device_node *np = pdev->dev.of_node;
	struct stmpe_gpio *stmpe_gpio;
	int ret;
	int irq = 0;

	irq = platform_get_irq(pdev, 0);

	stmpe_gpio = kzalloc(sizeof(struct stmpe_gpio), GFP_KERNEL);
	if (!stmpe_gpio)
		return -ENOMEM;

	mutex_init(&stmpe_gpio->irq_lock);

	stmpe_gpio->dev = &pdev->dev;
	stmpe_gpio->stmpe = stmpe;
	stmpe_gpio->chip = template_chip;
	stmpe_gpio->chip.ngpio = stmpe->num_gpios;
	stmpe_gpio->chip.parent = &pdev->dev;
	stmpe_gpio->chip.of_node = np;
	stmpe_gpio->chip.base = -1;

	if (IS_ENABLED(CONFIG_DEBUG_FS))
                stmpe_gpio->chip.dbg_show = stmpe_dbg_show;

	of_property_read_u32(np, "st,norequest-mask",
			&stmpe_gpio->norequest_mask);
	if (stmpe_gpio->norequest_mask)
		stmpe_gpio->chip.irq_need_valid_mask = true;

	if (irq < 0)
		dev_info(&pdev->dev,
			"device configured in no-irq mode: "
			"irqs are not available\n");

	ret = stmpe_enable(stmpe, STMPE_BLOCK_GPIO);
	if (ret)
		goto out_free;

	ret = gpiochip_add_data(&stmpe_gpio->chip, stmpe_gpio);
	if (ret) {
		dev_err(&pdev->dev, "unable to add gpiochip: %d\n", ret);
		goto out_disable;
	}

	if (irq > 0) {
		ret = devm_request_threaded_irq(&pdev->dev, irq, NULL,
				stmpe_gpio_irq, IRQF_ONESHOT,
				"stmpe-gpio", stmpe_gpio);
		if (ret) {
			dev_err(&pdev->dev, "unable to get irq: %d\n", ret);
			goto out_disable;
		}
		if (stmpe_gpio->norequest_mask) {
			int i;

			/* Forbid unused lines to be mapped as IRQs */
			for (i = 0; i < sizeof(u32); i++)
				if (stmpe_gpio->norequest_mask & BIT(i))
					clear_bit(i, stmpe_gpio->chip.irq_valid_mask);
		}
		ret =  gpiochip_irqchip_add_nested(&stmpe_gpio->chip,
						   &stmpe_gpio_irq_chip,
						   0,
						   handle_simple_irq,
						   IRQ_TYPE_NONE);
		if (ret) {
			dev_err(&pdev->dev,
				"could not connect irqchip to gpiochip\n");
			goto out_disable;
		}

		gpiochip_set_nested_irqchip(&stmpe_gpio->chip,
					    &stmpe_gpio_irq_chip,
					    irq);
	}

	platform_set_drvdata(pdev, stmpe_gpio);

	return 0;

out_disable:
	stmpe_disable(stmpe, STMPE_BLOCK_GPIO);
	gpiochip_remove(&stmpe_gpio->chip);
out_free:
	kfree(stmpe_gpio);
	return ret;
}

static struct platform_driver stmpe_gpio_driver = {
	.driver = {
		.suppress_bind_attrs	= true,
		.name			= "stmpe-gpio",
	},
	.probe		= stmpe_gpio_probe,
};

static int __init stmpe_gpio_init(void)
{
	return platform_driver_register(&stmpe_gpio_driver);
}
subsys_initcall(stmpe_gpio_init);
