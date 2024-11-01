// SPDX-License-Identifier: GPL-2.0-only

#include <linux/gpio/driver.h>
#include <linux/cpumask.h>
#include <linux/irq.h>
#include <linux/minmax.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>

/*
 * Total register block size is 0x1C for one bank of four ports (A, B, C, D).
 * An optional second bank, with ports E, F, G, and H, may be present, starting
 * at register offset 0x1C.
 */

/*
 * Pin select: (0) "normal", (1) "dedicate peripheral"
 * Not used on RTL8380/RTL8390, peripheral selection is managed by control bits
 * in the peripheral registers.
 */
#define REALTEK_GPIO_REG_CNR		0x00
/* Clear bit (0) for input, set bit (1) for output */
#define REALTEK_GPIO_REG_DIR		0x08
#define REALTEK_GPIO_REG_DATA		0x0C
/* Read bit for IRQ status, write 1 to clear IRQ */
#define REALTEK_GPIO_REG_ISR		0x10
/* Two bits per GPIO in IMR registers */
#define REALTEK_GPIO_REG_IMR		0x14
#define REALTEK_GPIO_REG_IMR_AB		0x14
#define REALTEK_GPIO_REG_IMR_CD		0x18
#define REALTEK_GPIO_IMR_LINE_MASK	GENMASK(1, 0)
#define REALTEK_GPIO_IRQ_EDGE_FALLING	1
#define REALTEK_GPIO_IRQ_EDGE_RISING	2
#define REALTEK_GPIO_IRQ_EDGE_BOTH	3

#define REALTEK_GPIO_MAX		32
#define REALTEK_GPIO_PORTS_PER_BANK	4

/**
 * realtek_gpio_ctrl - Realtek Otto GPIO driver data
 *
 * @gc: Associated gpio_chip instance
 * @base: Base address of the register block for a GPIO bank
 * @lock: Lock for accessing the IRQ registers and values
 * @intr_mask: Mask for interrupts lines
 * @intr_type: Interrupt type selection
 * @bank_read: Read a bank setting as a single 32-bit value
 * @bank_write: Write a bank setting as a single 32-bit value
 * @imr_line_pos: Bit shift of an IRQ line's IMR value.
 *
 * The DIR, DATA, and ISR registers consist of four 8-bit port values, packed
 * into a single 32-bit register. Use @bank_read (@bank_write) to get (assign)
 * a value from (to) these registers. The IMR register consists of four 16-bit
 * port values, packed into two 32-bit registers. Use @imr_line_pos to get the
 * bit shift of the 2-bit field for a line's IMR settings. Shifts larger than
 * 32 overflow into the second register.
 *
 * Because the interrupt mask register (IMR) combines the function of IRQ type
 * selection and masking, two extra values are stored. @intr_mask is used to
 * mask/unmask the interrupts for a GPIO line, and @intr_type is used to store
 * the selected interrupt types. The logical AND of these values is written to
 * IMR on changes.
 */
struct realtek_gpio_ctrl {
	struct gpio_chip gc;
	void __iomem *base;
	void __iomem *cpumask_base;
	struct cpumask cpu_irq_maskable;
	raw_spinlock_t lock;
	u8 intr_mask[REALTEK_GPIO_MAX];
	u8 intr_type[REALTEK_GPIO_MAX];
	u32 (*bank_read)(void __iomem *reg);
	void (*bank_write)(void __iomem *reg, u32 value);
	unsigned int (*line_imr_pos)(unsigned int line);
};

/* Expand with more flags as devices with other quirks are added */
enum realtek_gpio_flags {
	/*
	 * Allow disabling interrupts, for cases where the port order is
	 * unknown. This may result in a port mismatch between ISR and IMR.
	 * An interrupt would appear to come from a different line than the
	 * line the IRQ handler was assigned to, causing uncaught interrupts.
	 */
	GPIO_INTERRUPTS_DISABLED = BIT(0),
	/*
	 * Port order is reversed, meaning DCBA register layout for 1-bit
	 * fields, and [BA, DC] for 2-bit fields.
	 */
	GPIO_PORTS_REVERSED = BIT(1),
	/*
	 * Interrupts can be enabled per cpu. This requires a secondary IO
	 * range, where the per-cpu enable masks are located.
	 */
	GPIO_INTERRUPTS_PER_CPU = BIT(2),
};

static struct realtek_gpio_ctrl *irq_data_to_ctrl(struct irq_data *data)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(data);

	return container_of(gc, struct realtek_gpio_ctrl, gc);
}

/*
 * Normal port order register access
 *
 * Port information is stored with the first port at offset 0, followed by the
 * second, etc. Most registers store one bit per GPIO and use a u8 value per
 * port. The two interrupt mask registers store two bits per GPIO, so use u16
 * values.
 */
static u32 realtek_gpio_bank_read_swapped(void __iomem *reg)
{
	return ioread32be(reg);
}

static void realtek_gpio_bank_write_swapped(void __iomem *reg, u32 value)
{
	iowrite32be(value, reg);
}

static unsigned int realtek_gpio_line_imr_pos_swapped(unsigned int line)
{
	unsigned int port_pin = line % 8;
	unsigned int port = line / 8;

	return 2 * (8 * (port ^ 1) + port_pin);
}

/*
 * Reversed port order register access
 *
 * For registers with one bit per GPIO, all ports are stored as u8-s in one
 * register in reversed order. The two interrupt mask registers store two bits
 * per GPIO, so use u16 values. The first register contains ports 1 and 0, the
 * second ports 3 and 2.
 */
static u32 realtek_gpio_bank_read(void __iomem *reg)
{
	return ioread32(reg);
}

static void realtek_gpio_bank_write(void __iomem *reg, u32 value)
{
	iowrite32(value, reg);
}

static unsigned int realtek_gpio_line_imr_pos(unsigned int line)
{
	return 2 * line;
}

static void realtek_gpio_clear_isr(struct realtek_gpio_ctrl *ctrl, u32 mask)
{
	ctrl->bank_write(ctrl->base + REALTEK_GPIO_REG_ISR, mask);
}

static u32 realtek_gpio_read_isr(struct realtek_gpio_ctrl *ctrl)
{
	return ctrl->bank_read(ctrl->base + REALTEK_GPIO_REG_ISR);
}

/* Set the rising and falling edge mask bits for a GPIO pin */
static void realtek_gpio_update_line_imr(struct realtek_gpio_ctrl *ctrl, unsigned int line)
{
	void __iomem *reg = ctrl->base + REALTEK_GPIO_REG_IMR;
	unsigned int line_shift = ctrl->line_imr_pos(line);
	unsigned int shift = line_shift % 32;
	u32 irq_type = ctrl->intr_type[line];
	u32 irq_mask = ctrl->intr_mask[line];
	u32 reg_val;

	reg += 4 * (line_shift / 32);
	reg_val = ioread32(reg);
	reg_val &= ~(REALTEK_GPIO_IMR_LINE_MASK << shift);
	reg_val |= (irq_type & irq_mask & REALTEK_GPIO_IMR_LINE_MASK) << shift;
	iowrite32(reg_val, reg);
}

static void realtek_gpio_irq_ack(struct irq_data *data)
{
	struct realtek_gpio_ctrl *ctrl = irq_data_to_ctrl(data);
	irq_hw_number_t line = irqd_to_hwirq(data);

	realtek_gpio_clear_isr(ctrl, BIT(line));
}

static void realtek_gpio_irq_unmask(struct irq_data *data)
{
	struct realtek_gpio_ctrl *ctrl = irq_data_to_ctrl(data);
	unsigned int line = irqd_to_hwirq(data);
	unsigned long flags;

	gpiochip_enable_irq(&ctrl->gc, line);

	raw_spin_lock_irqsave(&ctrl->lock, flags);
	ctrl->intr_mask[line] = REALTEK_GPIO_IMR_LINE_MASK;
	realtek_gpio_update_line_imr(ctrl, line);
	raw_spin_unlock_irqrestore(&ctrl->lock, flags);
}

static void realtek_gpio_irq_mask(struct irq_data *data)
{
	struct realtek_gpio_ctrl *ctrl = irq_data_to_ctrl(data);
	unsigned int line = irqd_to_hwirq(data);
	unsigned long flags;

	raw_spin_lock_irqsave(&ctrl->lock, flags);
	ctrl->intr_mask[line] = 0;
	realtek_gpio_update_line_imr(ctrl, line);
	raw_spin_unlock_irqrestore(&ctrl->lock, flags);

	gpiochip_disable_irq(&ctrl->gc, line);
}

static int realtek_gpio_irq_set_type(struct irq_data *data, unsigned int flow_type)
{
	struct realtek_gpio_ctrl *ctrl = irq_data_to_ctrl(data);
	unsigned int line = irqd_to_hwirq(data);
	unsigned long flags;
	u8 type;

	switch (flow_type & IRQ_TYPE_SENSE_MASK) {
	case IRQ_TYPE_EDGE_FALLING:
		type = REALTEK_GPIO_IRQ_EDGE_FALLING;
		break;
	case IRQ_TYPE_EDGE_RISING:
		type = REALTEK_GPIO_IRQ_EDGE_RISING;
		break;
	case IRQ_TYPE_EDGE_BOTH:
		type = REALTEK_GPIO_IRQ_EDGE_BOTH;
		break;
	default:
		return -EINVAL;
	}

	irq_set_handler_locked(data, handle_edge_irq);

	raw_spin_lock_irqsave(&ctrl->lock, flags);
	ctrl->intr_type[line] = type;
	realtek_gpio_update_line_imr(ctrl, line);
	raw_spin_unlock_irqrestore(&ctrl->lock, flags);

	return 0;
}

static void realtek_gpio_irq_handler(struct irq_desc *desc)
{
	struct gpio_chip *gc = irq_desc_get_handler_data(desc);
	struct realtek_gpio_ctrl *ctrl = gpiochip_get_data(gc);
	struct irq_chip *irq_chip = irq_desc_get_chip(desc);
	unsigned long status;
	int offset;

	chained_irq_enter(irq_chip, desc);

	status = realtek_gpio_read_isr(ctrl);
	for_each_set_bit(offset, &status, gc->ngpio)
		generic_handle_domain_irq(gc->irq.domain, offset);

	chained_irq_exit(irq_chip, desc);
}

static inline void __iomem *realtek_gpio_irq_cpu_mask(struct realtek_gpio_ctrl *ctrl, int cpu)
{
	return ctrl->cpumask_base + REALTEK_GPIO_PORTS_PER_BANK * cpu;
}

static int realtek_gpio_irq_set_affinity(struct irq_data *data,
	const struct cpumask *dest, bool force)
{
	struct realtek_gpio_ctrl *ctrl = irq_data_to_ctrl(data);
	unsigned int line = irqd_to_hwirq(data);
	void __iomem *irq_cpu_mask;
	unsigned long flags;
	int cpu;
	u32 v;

	if (!ctrl->cpumask_base)
		return -ENXIO;

	raw_spin_lock_irqsave(&ctrl->lock, flags);

	for_each_cpu(cpu, &ctrl->cpu_irq_maskable) {
		irq_cpu_mask = realtek_gpio_irq_cpu_mask(ctrl, cpu);
		v = ctrl->bank_read(irq_cpu_mask);

		if (cpumask_test_cpu(cpu, dest))
			v |= BIT(line);
		else
			v &= ~BIT(line);

		ctrl->bank_write(irq_cpu_mask, v);
	}

	raw_spin_unlock_irqrestore(&ctrl->lock, flags);

	irq_data_update_effective_affinity(data, dest);

	return 0;
}

static int realtek_gpio_irq_init(struct gpio_chip *gc)
{
	struct realtek_gpio_ctrl *ctrl = gpiochip_get_data(gc);
	u32 mask_all = GENMASK(gc->ngpio - 1, 0);
	unsigned int line;
	int cpu;

	for (line = 0; line < gc->ngpio; line++)
		realtek_gpio_update_line_imr(ctrl, line);

	realtek_gpio_clear_isr(ctrl, mask_all);

	for_each_cpu(cpu, &ctrl->cpu_irq_maskable)
		ctrl->bank_write(realtek_gpio_irq_cpu_mask(ctrl, cpu), mask_all);

	return 0;
}

static const struct irq_chip realtek_gpio_irq_chip = {
	.name = "realtek-otto-gpio",
	.irq_ack = realtek_gpio_irq_ack,
	.irq_mask = realtek_gpio_irq_mask,
	.irq_unmask = realtek_gpio_irq_unmask,
	.irq_set_type = realtek_gpio_irq_set_type,
	.irq_set_affinity = realtek_gpio_irq_set_affinity,
	.flags = IRQCHIP_IMMUTABLE,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

static const struct of_device_id realtek_gpio_of_match[] = {
	{
		.compatible = "realtek,otto-gpio",
		.data = (void *)GPIO_INTERRUPTS_DISABLED,
	},
	{
		.compatible = "realtek,rtl8380-gpio",
	},
	{
		.compatible = "realtek,rtl8390-gpio",
	},
	{
		.compatible = "realtek,rtl9300-gpio",
		.data = (void *)(GPIO_PORTS_REVERSED | GPIO_INTERRUPTS_PER_CPU)
	},
	{
		.compatible = "realtek,rtl9310-gpio",
	},
	{}
};
MODULE_DEVICE_TABLE(of, realtek_gpio_of_match);

static int realtek_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	unsigned long bgpio_flags;
	unsigned int dev_flags;
	struct gpio_irq_chip *girq;
	struct realtek_gpio_ctrl *ctrl;
	struct resource *res;
	u32 ngpios;
	unsigned int nr_cpus;
	int cpu, err, irq;

	ctrl = devm_kzalloc(dev, sizeof(*ctrl), GFP_KERNEL);
	if (!ctrl)
		return -ENOMEM;

	dev_flags = (unsigned int) device_get_match_data(dev);

	ngpios = REALTEK_GPIO_MAX;
	device_property_read_u32(dev, "ngpios", &ngpios);

	if (ngpios > REALTEK_GPIO_MAX) {
		dev_err(&pdev->dev, "invalid ngpios (max. %d)\n",
			REALTEK_GPIO_MAX);
		return -EINVAL;
	}

	ctrl->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(ctrl->base))
		return PTR_ERR(ctrl->base);

	raw_spin_lock_init(&ctrl->lock);

	if (dev_flags & GPIO_PORTS_REVERSED) {
		bgpio_flags = 0;
		ctrl->bank_read = realtek_gpio_bank_read;
		ctrl->bank_write = realtek_gpio_bank_write;
		ctrl->line_imr_pos = realtek_gpio_line_imr_pos;
	} else {
		bgpio_flags = BGPIOF_BIG_ENDIAN_BYTE_ORDER;
		ctrl->bank_read = realtek_gpio_bank_read_swapped;
		ctrl->bank_write = realtek_gpio_bank_write_swapped;
		ctrl->line_imr_pos = realtek_gpio_line_imr_pos_swapped;
	}

	err = bgpio_init(&ctrl->gc, dev, 4,
		ctrl->base + REALTEK_GPIO_REG_DATA, NULL, NULL,
		ctrl->base + REALTEK_GPIO_REG_DIR, NULL,
		bgpio_flags);
	if (err) {
		dev_err(dev, "unable to init generic GPIO");
		return err;
	}

	ctrl->gc.ngpio = ngpios;
	ctrl->gc.owner = THIS_MODULE;

	irq = platform_get_irq_optional(pdev, 0);
	if (!(dev_flags & GPIO_INTERRUPTS_DISABLED) && irq > 0) {
		girq = &ctrl->gc.irq;
		gpio_irq_chip_set_chip(girq, &realtek_gpio_irq_chip);
		girq->default_type = IRQ_TYPE_NONE;
		girq->handler = handle_bad_irq;
		girq->parent_handler = realtek_gpio_irq_handler;
		girq->num_parents = 1;
		girq->parents = devm_kcalloc(dev, girq->num_parents,
					sizeof(*girq->parents),	GFP_KERNEL);
		if (!girq->parents)
			return -ENOMEM;
		girq->parents[0] = irq;
		girq->init_hw = realtek_gpio_irq_init;
	}

	cpumask_clear(&ctrl->cpu_irq_maskable);

	if ((dev_flags & GPIO_INTERRUPTS_PER_CPU) && irq > 0) {
		ctrl->cpumask_base = devm_platform_get_and_ioremap_resource(pdev, 1, &res);
		if (IS_ERR(ctrl->cpumask_base))
			return dev_err_probe(dev, PTR_ERR(ctrl->cpumask_base),
				"missing CPU IRQ mask registers");

		nr_cpus = resource_size(res) / REALTEK_GPIO_PORTS_PER_BANK;
		nr_cpus = min(nr_cpus, num_present_cpus());

		for (cpu = 0; cpu < nr_cpus; cpu++)
			cpumask_set_cpu(cpu, &ctrl->cpu_irq_maskable);
	}

	return devm_gpiochip_add_data(dev, &ctrl->gc, ctrl);
}

static struct platform_driver realtek_gpio_driver = {
	.driver = {
		.name = "realtek-otto-gpio",
		.of_match_table	= realtek_gpio_of_match,
	},
	.probe = realtek_gpio_probe,
};
module_platform_driver(realtek_gpio_driver);

MODULE_DESCRIPTION("Realtek Otto GPIO support");
MODULE_AUTHOR("Sander Vanheule <sander@svanheule.net>");
MODULE_LICENSE("GPL v2");
