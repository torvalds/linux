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
#include <linux/sysdev.h>

#include <mach/hardware.h>
#include <mach/gpio.h>
#include <mach/rk29_iomap.h>
#include <mach/iomux.h>
#include <asm/gpio.h>


#define to_rk29_gpio_chip(c) container_of(c, struct rk29_gpio_chip, chip)

struct rk29_gpio_chip {
	struct gpio_chip        chip;
	struct rk29_gpio_chip	*next;		/* Bank sharing same clock */
	struct rk29_gpio_bank	*bank;		/* Bank definition */
	unsigned char  __iomem	*regbase;	/* Base of register bank */
	struct clk *clk;
};

static struct lock_class_key gpio_lock_class;

static void rk29_gpiolib_dbg_show(struct seq_file *s, struct gpio_chip *chip);
static void rk29_gpiolib_set(struct gpio_chip *chip, unsigned offset, int val);
static int rk29_gpiolib_get(struct gpio_chip *chip, unsigned offset);
static int rk29_gpiolib_direction_output(struct gpio_chip *chip,unsigned offset, int val);
static int rk29_gpiolib_direction_input(struct gpio_chip *chip,unsigned offset);
static int rk29_gpiolib_PullUpDown(struct gpio_chip *chip, unsigned offset, unsigned enable);
static int rk29_gpiolib_to_irq(struct gpio_chip *chip,unsigned offset);

#define RK29_GPIO_CHIP(name, base_gpio, nr_gpio)			\
	{								\
		.chip = {						\
			.label		  = name,			\
			.direction_input  = rk29_gpiolib_direction_input, \
			.direction_output = rk29_gpiolib_direction_output, \
			.get		  = rk29_gpiolib_get,		\
			.set		  = rk29_gpiolib_set,		\
			.pull_updown  = rk29_gpiolib_PullUpDown,         \
			.dbg_show	  = rk29_gpiolib_dbg_show,	\
			.to_irq       = rk29_gpiolib_to_irq,     \
			.base		  = base_gpio,			\
			.ngpio		  = nr_gpio,			\
		},							\
	}

static struct rk29_gpio_chip rk29gpio_chip[] = {
	RK29_GPIO_CHIP("gpio0", PIN_BASE + 0*NUM_GROUP, NUM_GROUP),
	RK29_GPIO_CHIP("gpio1", PIN_BASE + 1*NUM_GROUP, NUM_GROUP),
	RK29_GPIO_CHIP("gpio2", PIN_BASE + 2*NUM_GROUP, NUM_GROUP),
	RK29_GPIO_CHIP("gpio3", PIN_BASE + 3*NUM_GROUP, NUM_GROUP),
	RK29_GPIO_CHIP("gpio4", PIN_BASE + 4*NUM_GROUP, NUM_GROUP),
	RK29_GPIO_CHIP("gpio5", PIN_BASE + 5*NUM_GROUP, NUM_GROUP),
	RK29_GPIO_CHIP("gpio6", PIN_BASE + 6*NUM_GROUP, NUM_GROUP),
};

static inline void rk29_gpio_write(unsigned char  __iomem	*regbase, unsigned int regOff,unsigned int val)
{
	__raw_writel(val,regbase + regOff);
}

static inline unsigned int rk29_gpio_read(unsigned char  __iomem	*regbase, unsigned int regOff)
{
	return __raw_readl(regbase + regOff);
}

static inline void rk29_gpio_bitOp(unsigned char  __iomem	*regbase, unsigned int regOff,unsigned int mask,unsigned char opFlag)
{
	unsigned int valTemp = 0;
	
	if(opFlag == 0)//对寄存器相应位进行与0操作
	{
		valTemp = rk29_gpio_read(regbase,regOff);  
		valTemp &= (~mask);;
		rk29_gpio_write(regbase,regOff,valTemp);
	}
	else if(opFlag == 1)//对寄存器相应位进行或1操作
	{
		valTemp = rk29_gpio_read(regbase,regOff);
		valTemp |= mask;
		rk29_gpio_write(regbase,regOff,valTemp);
	}
}

static inline  struct gpio_chip *pin_to_gpioChip(unsigned pin)
{
	if(pin < PIN_BASE)
		return NULL;
	
	pin -= PIN_BASE;
	pin /= NUM_GROUP;
	if (likely(pin < MAX_BANK))
		return &(rk29gpio_chip[pin].chip);
	return NULL;
}

static inline unsigned  pin_to_mask(unsigned pin)
{
	if(pin < PIN_BASE)
		return 0;
	pin -= PIN_BASE;
	return 1ul << (pin % NUM_GROUP);
}

static inline unsigned  offset_to_mask(unsigned offset)
{
	return 1ul << (offset % NUM_GROUP);
}

static int GPIOSetPinLevel(struct gpio_chip *chip, unsigned int mask,eGPIOPinLevel_t level)
{
	struct rk29_gpio_chip *rk29_gpio = to_rk29_gpio_chip(chip);
	unsigned char  __iomem	*gpioRegBase = rk29_gpio->regbase;

	if(!rk29_gpio || !gpioRegBase)
	{
		return -1;
	}
	
	rk29_gpio_bitOp(gpioRegBase,GPIO_SWPORT_DDR,mask,1);
	rk29_gpio_bitOp(gpioRegBase,GPIO_SWPORT_DR,mask,level);
	return 0;
}

static int GPIOGetPinLevel(struct gpio_chip *chip, unsigned int mask)
{
	unsigned int valTemp;
	struct rk29_gpio_chip *rk29_gpio = to_rk29_gpio_chip(chip);
	unsigned char  __iomem	*gpioRegBase = rk29_gpio->regbase;

	if(!rk29_gpio || !gpioRegBase)
	{
		return -1;
	}
	
	valTemp = rk29_gpio_read(gpioRegBase,GPIO_EXT_PORT);
	return ((valTemp & mask) != 0);
}

static int GPIOSetPinDirection(struct gpio_chip *chip, unsigned int mask,eGPIOPinDirection_t direction)
{
	struct rk29_gpio_chip *rk29_gpio = to_rk29_gpio_chip(chip);
	unsigned char  __iomem	*gpioRegBase = rk29_gpio->regbase;

	if(!rk29_gpio || !gpioRegBase)
	{
		return -1;
	}
	
	rk29_gpio_bitOp(gpioRegBase,GPIO_SWPORT_DDR,mask,direction);
	rk29_gpio_bitOp(gpioRegBase,GPIO_DEBOUNCE,mask,1); 

	return 0;

}

static int GPIOEnableIntr(struct gpio_chip *chip, unsigned int mask)
{
	struct rk29_gpio_chip *rk29_gpio = to_rk29_gpio_chip(chip);
	unsigned char  __iomem	*gpioRegBase = rk29_gpio->regbase;

	if(!rk29_gpio || !gpioRegBase)
	{
		return -1;
	}
	
	rk29_gpio_bitOp(gpioRegBase,GPIO_INTEN,mask,1);
	return 0;
}

static int GPIOSetIntrType(struct gpio_chip *chip, unsigned int mask, eGPIOIntType_t IntType)
{
	struct rk29_gpio_chip *rk29_gpio = to_rk29_gpio_chip(chip);
	unsigned char  __iomem	*gpioRegBase = rk29_gpio->regbase;

	if(!rk29_gpio || !gpioRegBase)
	{
		return -1;
	}
	
	switch ( IntType )
	{
	    case GPIOLevelLow:
			rk29_gpio_bitOp(gpioRegBase,GPIO_INT_POLARITY,mask,0);	
			rk29_gpio_bitOp(gpioRegBase,GPIO_INTTYPE_LEVEL,mask,0);	
			break;
	    case GPIOLevelHigh:
			rk29_gpio_bitOp(gpioRegBase,GPIO_INTTYPE_LEVEL,mask,0);	
			rk29_gpio_bitOp(gpioRegBase,GPIO_INT_POLARITY,mask,1);	
			break;
	    case GPIOEdgelFalling:
			rk29_gpio_bitOp(gpioRegBase,GPIO_INTTYPE_LEVEL,mask,1);	
			rk29_gpio_bitOp(gpioRegBase,GPIO_INT_POLARITY,mask,0);	
			break;
	    case GPIOEdgelRising:
			rk29_gpio_bitOp(gpioRegBase,GPIO_INTTYPE_LEVEL,mask,1);	
			rk29_gpio_bitOp(gpioRegBase,GPIO_INT_POLARITY,mask,1);	
			break;
		default:
			return(-1);
	}
	 return(0);
}

static int gpio_irq_set_wake(unsigned irq, unsigned state)
{	
	unsigned int pin = irq_to_gpio(irq);
	unsigned	bank = (pin - PIN_BASE) / NUM_GROUP;
	unsigned int irq_number;

	if (unlikely(bank >= MAX_BANK))
		return -EINVAL;
	
	switch ( rk29gpio_chip[bank].bank->id )
	{
		case RK29_ID_GPIO0:
			irq_number = IRQ_GPIO0;
			break;
		case RK29_ID_GPIO1:
			irq_number = IRQ_GPIO1;
			break;
		case RK29_ID_GPIO2:
			irq_number = IRQ_GPIO2;
			break;
		case RK29_ID_GPIO3:
			irq_number = IRQ_GPIO3;
			break;
		case RK29_ID_GPIO4:
			irq_number = IRQ_GPIO4;
			break;
		case RK29_ID_GPIO5:
			irq_number = IRQ_GPIO5;
			break;
		case RK29_ID_GPIO6:
			irq_number = IRQ_GPIO6;
			break;	
		default:
			return 0;	
	}
	
	set_irq_wake(irq_number, state);
	
	return 0;
}

static int gpio_irq_type(unsigned irq, unsigned type)
{
	unsigned int pin = irq_to_gpio(irq);
	struct gpio_chip *chip = pin_to_gpioChip(pin);
	unsigned	mask = pin_to_mask(pin);
	
	if(!chip || !mask)
		return -EINVAL;
	//设置为中断之前，必须先设置为输入状态
	GPIOSetPinDirection(chip,mask,GPIO_IN);
	
	switch (type) {
		case IRQ_TYPE_NONE:
			break;
		case IRQ_TYPE_EDGE_RISING:
			GPIOSetIntrType(chip,mask,GPIOEdgelRising);
			break;
		case IRQ_TYPE_EDGE_FALLING:
			GPIOSetIntrType(chip,mask,GPIOEdgelFalling);
			break;
		case IRQ_TYPE_EDGE_BOTH:
			break;
		case IRQ_TYPE_LEVEL_HIGH:
			GPIOSetIntrType(chip,mask,GPIOLevelHigh);
			break;
		case IRQ_TYPE_LEVEL_LOW:
			GPIOSetIntrType(chip,mask,GPIOLevelLow);
			break;
		default:
			return -EINVAL;
	}
	return 0;
}

static int GPIOUnInmarkIntr(struct gpio_chip *chip, unsigned int mask)
{
	struct rk29_gpio_chip *rk29_gpio = to_rk29_gpio_chip(chip);
	unsigned char  __iomem	*gpioRegBase = rk29_gpio->regbase;

	if(!rk29_gpio || !gpioRegBase)
	{
		return -1;
	}
	
	rk29_gpio_bitOp(gpioRegBase,GPIO_INTMASK,mask,0);
	return 0;
}

static int GPIOInmarkIntr(struct gpio_chip *chip, unsigned int mask)
{
	struct rk29_gpio_chip *rk29_gpio = to_rk29_gpio_chip(chip);
	unsigned char  __iomem	*gpioRegBase = rk29_gpio->regbase;

	if(!rk29_gpio || !gpioRegBase)
	{
		return -1;
	}
	
	rk29_gpio_bitOp(gpioRegBase,GPIO_INTMASK,mask,1);
	return 0;
}

static int GPIOAckIntr(struct gpio_chip *chip, unsigned int mask)
{
	struct rk29_gpio_chip *rk29_gpio = to_rk29_gpio_chip(chip);
	unsigned char  __iomem	*gpioRegBase = rk29_gpio->regbase;

	if(!rk29_gpio || !gpioRegBase)
	{
		return -1;
	}
	
	rk29_gpio_bitOp(gpioRegBase,GPIO_PORTS_EOI,mask,1);
	return 0;
}

static void gpio_irq_unmask(unsigned irq)
{
	unsigned int pin = irq_to_gpio(irq);
	struct gpio_chip *chip = pin_to_gpioChip(pin);
	unsigned	mask = pin_to_mask(pin);

	if (chip && mask)
		GPIOUnInmarkIntr(chip,mask);
}

static void gpio_irq_mask(unsigned irq)
{
	unsigned int pin = irq_to_gpio(irq);
	struct gpio_chip *chip = pin_to_gpioChip(pin);
	unsigned	mask = pin_to_mask(pin);

	if(chip && mask)
		GPIOInmarkIntr(chip,mask);
}

static void gpio_ack_irq(u32 irq)
{
	unsigned int pin = irq_to_gpio(irq);
	struct gpio_chip *chip = pin_to_gpioChip(pin);
	unsigned	mask = pin_to_mask(pin);

	if(chip && mask)
		GPIOAckIntr(chip,mask);
}

static int GPIODisableIntr(struct gpio_chip *chip, unsigned int mask)
{
	struct rk29_gpio_chip *rk29_gpio = to_rk29_gpio_chip(chip);
	unsigned char  __iomem	*gpioRegBase = rk29_gpio->regbase;

	if(!rk29_gpio || !gpioRegBase)
	{
		return -1;
	}
	
	rk29_gpio_bitOp(gpioRegBase,GPIO_INTEN,mask,0);
	return 0;
}

static void gpio_irq_disable(unsigned irq)
{
	unsigned int pin = irq_to_gpio(irq);
	struct gpio_chip *chip = pin_to_gpioChip(pin);
	unsigned	mask = pin_to_mask(pin);

	if(chip && mask)
		GPIODisableIntr(chip,mask);
}

static void gpio_irq_enable(unsigned irq)
{
	unsigned int pin = irq_to_gpio(irq);
	struct gpio_chip *chip = pin_to_gpioChip(pin);
	unsigned	mask = pin_to_mask(pin);

	if(chip && mask)
		GPIOEnableIntr(chip,mask);
}

static int GPIOPullUpDown(struct gpio_chip *chip, unsigned int offset, unsigned enable)
{
	unsigned char temp=0;
	struct rk29_gpio_chip *rk29_gpio = to_rk29_gpio_chip(chip);
	unsigned char  __iomem *pGrfRegBase = (unsigned char  __iomem *)RK29_GRF_BASE;

	if(!rk29_gpio || !pGrfRegBase)
	{
		return -1;
	}
	
	if(offset >= 32)
	{
		return -1;
	}
	temp = __raw_readl(pGrfRegBase + 0x78 +(rk29_gpio->bank->id)*4);
	if(!enable)
		temp |= 1<<offset;
	else
		temp &= ~(1<<offset);
	//temp = (temp & (~(1 << offset)))| ((~enable) << offset);	
	__raw_writel(temp,pGrfRegBase + 0x78 +(rk29_gpio->bank->id)*4);
	
	return 0;
}


static int rk29_gpiolib_direction_output(struct gpio_chip *chip,unsigned offset, int val)
{
	unsigned	mask = offset_to_mask(offset);
	
	if(GPIOSetPinDirection(chip,mask,GPIO_OUT) == 0)
	{
		return GPIOSetPinLevel(chip,mask,val);
	}
	else
	{
		return -1;
	}
}

static int rk29_gpiolib_direction_input(struct gpio_chip *chip,unsigned offset)
{
	unsigned	mask = offset_to_mask(offset);
	
	return GPIOSetPinDirection(chip,mask,GPIO_IN);
}


static int rk29_gpiolib_get(struct gpio_chip *chip, unsigned offset)
{
	unsigned	mask = offset_to_mask(offset);
	
	return GPIOGetPinLevel(chip,mask);
}

static void rk29_gpiolib_set(struct gpio_chip *chip, unsigned offset, int val)
{
	unsigned	mask = offset_to_mask(offset);
	
	GPIOSetPinLevel(chip,mask,val);
}

static int rk29_gpiolib_PullUpDown(struct gpio_chip *chip, unsigned offset, unsigned enable)
{
	return GPIOPullUpDown(chip, offset, enable);
}

static int rk29_gpiolib_to_irq(struct gpio_chip *chip,
						unsigned offset)
{
    struct rk29_gpio_chip *rk29_gpio = to_rk29_gpio_chip(chip);

    if(!rk29_gpio)
    {
    	 return -1;
    }

    return offset + NR_IRQS;
}

static void rk29_gpiolib_dbg_show(struct seq_file *s, struct gpio_chip *chip)
{

	int i;

	for (i = 0; i < chip->ngpio; i++) {
		unsigned pin = chip->base + i;
		struct gpio_chip *chip = pin_to_gpioChip(pin);
		unsigned mask = pin_to_mask(pin);
		const char *gpio_label;
		
		if(!chip ||!mask)
			return;
		
		gpio_label = gpiochip_is_requested(chip, i);
		if (gpio_label) {
			seq_printf(s, "[%s] GPIO%s%d: ",
				   gpio_label, chip->label, i);
			
			if (!chip || !mask)
			{
				seq_printf(s, "!chip || !mask\t");
				return;
			}
				
			GPIOSetPinDirection(chip,mask,GPIO_IN);
			seq_printf(s, "pin=%d,level=%d\t", pin,GPIOGetPinLevel(chip,mask));
			seq_printf(s, "\t");
		}
	}
}

static void gpio_irq_handler(unsigned irq, struct irq_desc *desc)
{
	unsigned	pin,gpioToirq=0;
	struct irq_desc	*gpio;
	struct rk29_gpio_chip *rk29_gpio;
	unsigned char  __iomem	*gpioRegBase;
	u32		isr;

	rk29_gpio = get_irq_chip_data(irq+13);
	gpioRegBase = rk29_gpio->regbase;

	//屏蔽中断6或7
	desc->chip->mask(irq);
	if(desc->chip->ack)
		desc->chip->ack(irq);
	//读取当前中断状态，即查询具体是GPIO的哪个PIN引起的中断
	isr = rk29_gpio_read(gpioRegBase,GPIO_INT_STATUS);
	if (!isr) {
			desc->chip->unmask(irq);
			return;
	}

	pin = rk29_gpio->chip.base;
	gpioToirq = gpio_to_irq(pin);
	gpio = &irq_desc[gpioToirq];

	while (isr) {
		if (isr & 1) {
			{
				unsigned int gpio_Int_Level = 0;
				unsigned int mask = pin_to_mask(pin);
				if(!mask)
					break;
				gpio_Int_Level =  rk29_gpio_read(gpioRegBase,GPIO_INTTYPE_LEVEL);
				if(gpio_Int_Level == 0)//表示此中断类型是电平中断
				{
					rk29_gpio_bitOp(gpioRegBase,GPIO_INTMASK,mask,1);
				}
				generic_handle_irq(gpioToirq);
				
				if(gpio_Int_Level)//表示此中断类型是边沿中断
				{
					rk29_gpio_bitOp(gpioRegBase,GPIO_PORTS_EOI,mask,1);
				}
				else//表示此中断类型是电平中断
				{
					rk29_gpio_bitOp(gpioRegBase,GPIO_INTMASK,mask,0);
				}
			}				
		}
		pin++;
		gpio++;
		isr >>= 1;
		gpioToirq = gpio_to_irq(pin);
	}

	desc->chip->unmask(irq);
	/* now it may re-trigger */
}

static struct irq_chip rk29gpio_irqchip = {
	.name		= "RK29_GPIOIRQ",
	.ack 		= gpio_ack_irq,
	.enable 	= gpio_irq_enable,
	.disable	= gpio_irq_disable,
	.mask		= gpio_irq_mask,
	.unmask		= gpio_irq_unmask,
	.set_type	= gpio_irq_type,
	.set_wake	= gpio_irq_set_wake,
};

void __init rk29_gpio_irq_setup(void)
{
	unsigned	int	i,j, pin,irq=IRQ_GPIO0;
	struct rk29_gpio_chip *this;
	
	this = rk29gpio_chip;
	pin = NR_AIC_IRQS;
	for(i=0;i<MAX_BANK;i++)
	{
		rk29_gpio_write(this->regbase,GPIO_INTEN,0);
		for (j = 0; j < 32; j++) 
		{
			lockdep_set_class(&irq_desc[pin+j].lock, &gpio_lock_class);
			set_irq_chip(pin+j, &rk29gpio_irqchip);
			set_irq_handler(pin+j, handle_edge_irq);
			set_irq_flags(pin+j, IRQF_VALID);
		}
		
		switch ( this->bank->id )
		{
			case RK29_ID_GPIO0:
				irq = IRQ_GPIO0;
				break;
			case RK29_ID_GPIO1:
				irq = IRQ_GPIO1;
				break;
			case RK29_ID_GPIO2:
				irq = IRQ_GPIO2;
				break;
			case RK29_ID_GPIO3:
				irq = IRQ_GPIO3;
				break;
			case RK29_ID_GPIO4:
				irq = IRQ_GPIO4;
				break;
			case RK29_ID_GPIO5:
				irq = IRQ_GPIO5;
				break;
			case RK29_ID_GPIO6:
				irq = IRQ_GPIO6;
				break;		
		}

		set_irq_chip_data(NR_AIC_IRQS+this->bank->id,this);
		set_irq_chained_handler(irq, gpio_irq_handler);
		this += 1; 
		pin += 32;
	}
	printk("rk29_gpio_irq_setup: %d gpio irqs in 7 banks\n", pin - PIN_BASE);
}

void __init rk29_gpio_init(struct rk29_gpio_bank *data, int nr_banks)
{
	unsigned	i;
	struct rk29_gpio_chip *rk29_gpio, *last = NULL;
	
	for (i = 0; i < nr_banks; i++) {		
		rk29_gpio = &rk29gpio_chip[i];
		rk29_gpio->bank = &data[i];
		rk29_gpio->regbase = (unsigned char  __iomem *)rk29_gpio->bank->offset;		
		rk29_gpio->clk = clk_get(NULL, rk29_gpio->chip.label);
		clk_enable(rk29_gpio->clk);
		if(last)
			last->next = rk29_gpio;
		last = rk29_gpio;

		gpiochip_add(&rk29_gpio->chip);
	}
	printk("rk29_gpio_init:nr_banks=%d\n",nr_banks);
}
