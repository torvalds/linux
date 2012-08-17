/* arch/arm/mach-rk29/gpio.c
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/clk.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/syscore_ops.h>

#include <mach/hardware.h>
#include <mach/gpio.h>
#include <mach/io.h>
#include <mach/iomux.h>
#include <asm/gpio.h>
#include <asm/mach/irq.h>

#ifdef CONFIG_ARCH_RK30
#define MAX_PIN	RK30_PIN6_PB7
#elif defined(CONFIG_ARCH_RK31)
#define MAX_PIN	RK30_PIN3_PD7
#elif defined(CONFIG_ARCH_RK2928)
#define MAX_PIN	RK2928_PIN3_PD7
#define RK30_GPIO0_PHYS	RK2928_GPIO0_PHYS
#define RK30_GPIO0_BASE	RK2928_GPIO0_BASE
#define RK30_GPIO0_SIZE	RK2928_GPIO0_SIZE
#define RK30_GPIO1_PHYS	RK2928_GPIO1_PHYS
#define RK30_GPIO1_BASE	RK2928_GPIO1_BASE
#define RK30_GPIO1_SIZE	RK2928_GPIO1_SIZE
#define RK30_GPIO2_PHYS	RK2928_GPIO2_PHYS
#define RK30_GPIO2_BASE	RK2928_GPIO2_BASE
#define RK30_GPIO2_SIZE	RK2928_GPIO2_SIZE
#define RK30_GPIO3_PHYS	RK2928_GPIO3_PHYS
#define RK30_GPIO3_BASE	RK2928_GPIO3_BASE
#define RK30_GPIO3_SIZE	RK2928_GPIO3_SIZE
#define RK30_GRF_BASE	RK2928_GRF_BASE
#endif

#define to_rk30_gpio_bank(c) container_of(c, struct rk30_gpio_bank, chip)

struct rk30_gpio_bank {
	struct gpio_chip chip;
	unsigned short id;
	short irq;
	void __iomem *regbase;	/* Base of register bank */
	struct clk *clk;
	u32 suspend_wakeup;
	u32 saved_wakeup;
	spinlock_t lock;
};

static struct lock_class_key gpio_lock_class;

static void rk30_gpiolib_dbg_show(struct seq_file *s, struct gpio_chip *chip);
static void rk30_gpiolib_set(struct gpio_chip *chip, unsigned offset, int val);
static int rk30_gpiolib_get(struct gpio_chip *chip, unsigned offset);
static int rk30_gpiolib_direction_output(struct gpio_chip *chip,unsigned offset, int val);
static int rk30_gpiolib_direction_input(struct gpio_chip *chip,unsigned offset);
static int rk30_gpiolib_pull_updown(struct gpio_chip *chip, unsigned offset, unsigned enable);
static int rk30_gpiolib_to_irq(struct gpio_chip *chip,unsigned offset);

#define RK30_GPIO_BANK(ID)			\
	{								\
		.chip = {						\
			.label            = "gpio" #ID,			\
			.direction_input  = rk30_gpiolib_direction_input, \
			.direction_output = rk30_gpiolib_direction_output, \
			.get              = rk30_gpiolib_get,		\
			.set              = rk30_gpiolib_set,		\
			.pull_updown      = rk30_gpiolib_pull_updown,	\
			.dbg_show         = rk30_gpiolib_dbg_show,	\
			.to_irq           = rk30_gpiolib_to_irq,	\
			.base             = ID < 6 ? PIN_BASE + ID*NUM_GROUP : PIN_BASE + 5*NUM_GROUP,	\
			.ngpio            = ID < 6 ? NUM_GROUP : 16,	\
		},							\
		.id = ID, \
		.irq = IRQ_GPIO##ID, \
		.regbase = (unsigned char __iomem *) RK30_GPIO##ID##_BASE, \
	}

static struct rk30_gpio_bank rk30_gpio_banks[] = {
	RK30_GPIO_BANK(0),
	RK30_GPIO_BANK(1),
	RK30_GPIO_BANK(2),
	RK30_GPIO_BANK(3),
#ifdef CONFIG_ARCH_RK30
	RK30_GPIO_BANK(4),
	RK30_GPIO_BANK(6),
#endif
};

static inline void rk30_gpio_bit_op(void __iomem *regbase, unsigned int offset, u32 bit, unsigned char flag)
{
	u32 val = __raw_readl(regbase + offset);
	if (flag)
		val |= bit;
	else
		val &= ~bit;
	__raw_writel(val, regbase + offset);
}

static inline struct gpio_chip *pin_to_gpio_chip(unsigned pin)
{
	if (pin < PIN_BASE || pin > MAX_PIN)
		return NULL;

	pin -= PIN_BASE;
	pin /= NUM_GROUP;
	if (likely(pin < ARRAY_SIZE(rk30_gpio_banks)))
		return &(rk30_gpio_banks[pin].chip);
	return NULL;
}

static inline unsigned gpio_to_bit(unsigned gpio)
{
	gpio -= PIN_BASE;
	return 1u << (gpio % NUM_GROUP);
}

static inline unsigned offset_to_bit(unsigned offset)
{
	return 1u << offset;
}

static void GPIOSetPinLevel(void __iomem *regbase, unsigned int bit, eGPIOPinLevel_t level)
{
	rk30_gpio_bit_op(regbase, GPIO_SWPORT_DDR, bit, 1);
	rk30_gpio_bit_op(regbase, GPIO_SWPORT_DR, bit, level);
}

static int GPIOGetPinLevel(void __iomem *regbase, unsigned int bit)
{
	return ((__raw_readl(regbase + GPIO_EXT_PORT) & bit) != 0);
}

static void GPIOSetPinDirection(void __iomem *regbase, unsigned int bit, eGPIOPinDirection_t direction)
{
	rk30_gpio_bit_op(regbase, GPIO_SWPORT_DDR, bit, direction);
	/* Enable debounce may halt cpu on wfi, disable it by default */
	//rk30_gpio_bit_op(regbase, GPIO_DEBOUNCE, bit, 1);
}

static void GPIOEnableIntr(void __iomem *regbase, unsigned int bit)
{
	rk30_gpio_bit_op(regbase, GPIO_INTEN, bit, 1);
}

static void GPIODisableIntr(void __iomem *regbase, unsigned int bit)
{
	rk30_gpio_bit_op(regbase, GPIO_INTEN, bit, 0);
}

static void GPIOAckIntr(void __iomem *regbase, unsigned int bit)
{
	rk30_gpio_bit_op(regbase, GPIO_PORTS_EOI, bit, 1);
}

static void GPIOSetIntrType(void __iomem *regbase, unsigned int bit, eGPIOIntType_t type)
{
	switch (type) {
	case GPIOLevelLow:
		rk30_gpio_bit_op(regbase, GPIO_INT_POLARITY, bit, 0);
		rk30_gpio_bit_op(regbase, GPIO_INTTYPE_LEVEL, bit, 0);
		break;
	case GPIOLevelHigh:
		rk30_gpio_bit_op(regbase, GPIO_INTTYPE_LEVEL, bit, 0);
		rk30_gpio_bit_op(regbase, GPIO_INT_POLARITY, bit, 1);
		break;
	case GPIOEdgelFalling:
		rk30_gpio_bit_op(regbase, GPIO_INTTYPE_LEVEL, bit, 1);
		rk30_gpio_bit_op(regbase, GPIO_INT_POLARITY, bit, 0);
		break;
	case GPIOEdgelRising:
		rk30_gpio_bit_op(regbase, GPIO_INTTYPE_LEVEL, bit, 1);
		rk30_gpio_bit_op(regbase, GPIO_INT_POLARITY, bit, 1);
		break;
	}
}

static int rk30_gpio_irq_set_type(struct irq_data *d, unsigned int type)
{
	struct rk30_gpio_bank *bank = irq_data_get_irq_chip_data(d);
	u32 bit = gpio_to_bit(irq_to_gpio(d->irq));
	eGPIOIntType_t int_type;
	unsigned long flags;

	switch (type) {
	case IRQ_TYPE_EDGE_RISING:
		int_type = GPIOEdgelRising;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		int_type = GPIOEdgelFalling;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		int_type = GPIOLevelHigh;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		int_type = GPIOLevelLow;
		break;
	default:
		return -EINVAL;
	}

	spin_lock_irqsave(&bank->lock, flags);
	//设置为中断之前，必须先设置为输入状态
	GPIOSetPinDirection(bank->regbase, bit, GPIO_IN);
	GPIOSetIntrType(bank->regbase, bit, int_type);
	spin_unlock_irqrestore(&bank->lock, flags);

	if (type & (IRQ_TYPE_LEVEL_LOW | IRQ_TYPE_LEVEL_HIGH))
		__irq_set_handler_locked(d->irq, handle_level_irq);
	else if (type & (IRQ_TYPE_EDGE_FALLING | IRQ_TYPE_EDGE_RISING))
		__irq_set_handler_locked(d->irq, handle_edge_irq);

	return 0;
}

static int rk30_gpio_irq_set_wake(struct irq_data *d, unsigned int on)
{
	struct rk30_gpio_bank *bank = irq_data_get_irq_chip_data(d);
	u32 bit = gpio_to_bit(irq_to_gpio(d->irq));
	unsigned long flags;

	spin_lock_irqsave(&bank->lock, flags);
	if (on)
		bank->suspend_wakeup |= bit;
	else
		bank->suspend_wakeup &= ~bit;
	spin_unlock_irqrestore(&bank->lock, flags);

	return 0;
}

static void rk30_gpio_irq_unmask(struct irq_data *d)
{
	struct rk30_gpio_bank *bank = irq_data_get_irq_chip_data(d);
	u32 bit = gpio_to_bit(irq_to_gpio(d->irq));
	unsigned long flags;

	spin_lock_irqsave(&bank->lock, flags);
	GPIOEnableIntr(bank->regbase, bit);
	spin_unlock_irqrestore(&bank->lock, flags);
}

static void rk30_gpio_irq_mask(struct irq_data *d)
{
	struct rk30_gpio_bank *bank = irq_data_get_irq_chip_data(d);
	u32 bit = gpio_to_bit(irq_to_gpio(d->irq));
	unsigned long flags;

	spin_lock_irqsave(&bank->lock, flags);
	GPIODisableIntr(bank->regbase, bit);
	spin_unlock_irqrestore(&bank->lock, flags);
}

static void rk30_gpio_irq_ack(struct irq_data *d)
{
	struct rk30_gpio_bank *bank = irq_data_get_irq_chip_data(d);
	u32 bit = gpio_to_bit(irq_to_gpio(d->irq));

	GPIOAckIntr(bank->regbase, bit);
}

static int rk30_gpiolib_direction_output(struct gpio_chip *chip, unsigned offset, int val)
{
	struct rk30_gpio_bank *bank = to_rk30_gpio_bank(chip);
	u32 bit = offset_to_bit(offset);
	unsigned long flags;

	spin_lock_irqsave(&bank->lock, flags);
	GPIOSetPinDirection(bank->regbase, bit, GPIO_OUT);
	GPIOSetPinLevel(bank->regbase, bit, val);
	spin_unlock_irqrestore(&bank->lock, flags);
	return 0;
}

static int rk30_gpiolib_direction_input(struct gpio_chip *chip,unsigned offset)
{
	struct rk30_gpio_bank *bank = to_rk30_gpio_bank(chip);
	unsigned long flags;

	spin_lock_irqsave(&bank->lock, flags);
	GPIOSetPinDirection(bank->regbase, offset_to_bit(offset), GPIO_IN);
	spin_unlock_irqrestore(&bank->lock, flags);
	return 0;
}


static int rk30_gpiolib_get(struct gpio_chip *chip, unsigned offset)
{
	return GPIOGetPinLevel(to_rk30_gpio_bank(chip)->regbase, offset_to_bit(offset));
}

static void rk30_gpiolib_set(struct gpio_chip *chip, unsigned offset, int val)
{
	struct rk30_gpio_bank *bank = to_rk30_gpio_bank(chip);
	unsigned long flags;

	spin_lock_irqsave(&bank->lock, flags);
	GPIOSetPinLevel(bank->regbase, offset_to_bit(offset), val);
	spin_unlock_irqrestore(&bank->lock, flags);
}

static int rk30_gpiolib_pull_updown(struct gpio_chip *chip, unsigned offset, unsigned enable)
{
#if defined(CONFIG_ARCH_RK30) || defined(CONFIG_ARCH_RK2928)
	struct rk30_gpio_bank *bank = to_rk30_gpio_bank(chip);
	unsigned long flags;

	spin_lock_irqsave(&bank->lock, flags);
	if(offset>=16)	
		rk30_gpio_bit_op((void *__iomem) RK30_GRF_BASE, GRF_GPIO0H_PULL + bank->id * 8, (1<<offset) | offset_to_bit(offset-16), !enable);
	else	
		rk30_gpio_bit_op((void *__iomem) RK30_GRF_BASE, GRF_GPIO0L_PULL + bank->id * 8, (1<<(offset+16)) | offset_to_bit(offset), !enable);
	spin_unlock_irqrestore(&bank->lock, flags);
#endif
	return 0;
}

static int rk30_gpiolib_to_irq(struct gpio_chip *chip, unsigned offset)
{
	return chip->base + offset;
}

static void rk30_gpiolib_dbg_show(struct seq_file *s, struct gpio_chip *chip)
{
#if 0
	int i;

	for (i = 0; i < chip->ngpio; i++) {
		unsigned pin = chip->base + i;
		struct gpio_chip *chip = pin_to_gpioChip(pin);
		u32 bit = pin_to_bit(pin);
		const char *gpio_label;
		
		if(!chip ||!bit)
			return;
		
		gpio_label = gpiochip_is_requested(chip, i);
		if (gpio_label) {
			seq_printf(s, "[%s] GPIO%s%d: ",
				   gpio_label, chip->label, i);
			
			if (!chip || !bit)
			{
				seq_printf(s, "!chip || !bit\t");
				return;
			}
				
			GPIOSetPinDirection(chip,bit,GPIO_IN);
			seq_printf(s, "pin=%d,level=%d\t", pin,GPIOGetPinLevel(chip,bit));
			seq_printf(s, "\t");
		}
	}
#endif
}

static void rk30_gpio_irq_handler(unsigned int irq, struct irq_desc *desc)
{
	struct rk30_gpio_bank *bank = irq_get_handler_data(irq);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	unsigned gpio_irq;
	u32 isr, ilr;
	unsigned pin;
	unsigned unmasked = 0;

	chained_irq_enter(chip, desc);

	isr = __raw_readl(bank->regbase + GPIO_INT_STATUS);
	ilr = __raw_readl(bank->regbase + GPIO_INTTYPE_LEVEL);

	gpio_irq = gpio_to_irq(bank->chip.base);

	while (isr) {
		pin = fls(isr) - 1;
		/* if gpio is edge triggered, clear condition
		 * before executing the hander so that we don't
		 * miss edges
                 */
		if (ilr & (1 << pin)) {
			unmasked = 1;
			chained_irq_exit(chip, desc);
		}

		generic_handle_irq(gpio_irq + pin);
		isr &= ~(1 << pin);
	}

	if (!unmasked)
		chained_irq_exit(chip, desc);
}

static struct irq_chip rk30_gpio_irq_chip = {
	.name		= "GPIO",
	.irq_ack 	= rk30_gpio_irq_ack,
	.irq_disable	= rk30_gpio_irq_mask,
	.irq_mask	= rk30_gpio_irq_mask,
	.irq_unmask	= rk30_gpio_irq_unmask,
	.irq_set_type	= rk30_gpio_irq_set_type,
	.irq_set_wake	= rk30_gpio_irq_set_wake,
};

void __init rk30_gpio_init(void)
{
	unsigned int i, j, pin;
	struct rk30_gpio_bank *bank;

	bank = rk30_gpio_banks;
	pin = PIN_BASE;

	for (i = 0; i < ARRAY_SIZE(rk30_gpio_banks); i++, bank++) {
		spin_lock_init(&bank->lock);
		bank->clk = clk_get(NULL, bank->chip.label);
		clk_enable(bank->clk);
		gpiochip_add(&bank->chip);

		__raw_writel(0, bank->regbase + GPIO_INTEN);
		for (j = 0; j < 32; j++) {
			unsigned int irq = gpio_to_irq(pin);
			if (pin > MAX_PIN)
				break;
			irq_set_lockdep_class(irq, &gpio_lock_class);
			irq_set_chip_data(irq, bank);
			irq_set_chip_and_handler(irq, &rk30_gpio_irq_chip, handle_level_irq);
			set_irq_flags(irq, IRQF_VALID);
			pin++;
		}

		irq_set_handler_data(bank->irq, bank);
		irq_set_chained_handler(bank->irq, rk30_gpio_irq_handler);
	}
	printk("%s: %d gpio irqs in %d banks\n", __func__, pin - PIN_BASE, ARRAY_SIZE(rk30_gpio_banks));
}

#ifdef CONFIG_PM
__weak void rk30_setgpio_suspend_board(void)
{
}

__weak void rk30_setgpio_resume_board(void)
{
}

static int rk30_gpio_suspend(void)
{
	unsigned i;
	
	rk30_setgpio_suspend_board();

	for (i = 0; i < ARRAY_SIZE(rk30_gpio_banks); i++) {
		struct rk30_gpio_bank *bank = &rk30_gpio_banks[i];

		bank->saved_wakeup = __raw_readl(bank->regbase + GPIO_INTEN);
		__raw_writel(bank->suspend_wakeup, bank->regbase + GPIO_INTEN);

		if (!bank->suspend_wakeup)
			clk_disable(bank->clk);
	}

	return 0;
}

static void rk30_gpio_resume(void)
{
	unsigned i;

	for (i = 0; i < ARRAY_SIZE(rk30_gpio_banks); i++) {
		struct rk30_gpio_bank *bank = &rk30_gpio_banks[i];
		u32 isr;

		if (!bank->suspend_wakeup)
			clk_enable(bank->clk);

		/* keep enable for resume irq */
		isr = __raw_readl(bank->regbase + GPIO_INT_STATUS);
		__raw_writel(bank->saved_wakeup | (bank->suspend_wakeup & isr), bank->regbase + GPIO_INTEN);
	}

	rk30_setgpio_resume_board();
}

static struct syscore_ops rk30_gpio_syscore_ops = {
	.suspend	= rk30_gpio_suspend,
	.resume		= rk30_gpio_resume,
};

static int __init rk30_gpio_sysinit(void)
{
	register_syscore_ops(&rk30_gpio_syscore_ops);
        return 0;
}

arch_initcall(rk30_gpio_sysinit);
#endif
