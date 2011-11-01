/*
 * gpioint.c - Au1300 GPIO+Interrupt controller (I call it "GPIC") support.
 *
 * Copyright (c) 2009-2011 Manuel Lauss <manuel.lauss@googlemail.com>
 *
 * licensed under the GPLv2.
 */

#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/syscore_ops.h>
#include <linux/types.h>

#include <asm/irq_cpu.h>
#include <asm/mach-au1x00/au1000.h>
#include <asm/mach-au1x00/gpio-au1300.h>

static int au1300_gpic_settype(struct irq_data *d, unsigned int type);

/* setup for known onchip sources */
struct gpic_devint_data {
	int irq;	/* linux IRQ number */
	int type;	/* IRQ_TYPE_ */
	int prio;	/* irq priority, 0 highest, 3 lowest */
	int internal;	/* internal source (no ext. pin)? */
};

static const struct gpic_devint_data au1300_devints[] __initdata = {
	/* multifunction: gpio pin or device */
	{ AU1300_UART1_INT,	 IRQ_TYPE_LEVEL_HIGH,	1, 0, },
	{ AU1300_UART2_INT,	 IRQ_TYPE_LEVEL_HIGH,	1, 0, },
	{ AU1300_UART3_INT,	 IRQ_TYPE_LEVEL_HIGH,	1, 0, },
	{ AU1300_SD1_INT,	 IRQ_TYPE_LEVEL_HIGH,	1, 0, },
	{ AU1300_SD2_INT,	 IRQ_TYPE_LEVEL_HIGH,	1, 0, },
	{ AU1300_PSC0_INT,	 IRQ_TYPE_LEVEL_HIGH,	1, 0, },
	{ AU1300_PSC1_INT,	 IRQ_TYPE_LEVEL_HIGH,	1, 0, },
	{ AU1300_PSC2_INT,	 IRQ_TYPE_LEVEL_HIGH,	1, 0, },
	{ AU1300_PSC3_INT,	 IRQ_TYPE_LEVEL_HIGH,	1, 0, },
	{ AU1300_NAND_INT,	 IRQ_TYPE_LEVEL_HIGH,	1, 0, },
	/* au1300 internal */
	{ AU1300_DDMA_INT,	 IRQ_TYPE_LEVEL_HIGH,	1, 1, },
	{ AU1300_MMU_INT,	 IRQ_TYPE_LEVEL_HIGH,	1, 1, },
	{ AU1300_MPU_INT,	 IRQ_TYPE_LEVEL_HIGH,	1, 1, },
	{ AU1300_GPU_INT,	 IRQ_TYPE_LEVEL_HIGH,	1, 1, },
	{ AU1300_UDMA_INT,	 IRQ_TYPE_LEVEL_HIGH,	1, 1, },
	{ AU1300_TOY_INT,	 IRQ_TYPE_EDGE_RISING,	1, 1, },
	{ AU1300_TOY_MATCH0_INT, IRQ_TYPE_EDGE_RISING,	1, 1, },
	{ AU1300_TOY_MATCH1_INT, IRQ_TYPE_EDGE_RISING,	1, 1, },
	{ AU1300_TOY_MATCH2_INT, IRQ_TYPE_EDGE_RISING,	1, 1, },
	{ AU1300_RTC_INT,	 IRQ_TYPE_EDGE_RISING,	1, 1, },
	{ AU1300_RTC_MATCH0_INT, IRQ_TYPE_EDGE_RISING,	1, 1, },
	{ AU1300_RTC_MATCH1_INT, IRQ_TYPE_EDGE_RISING,	1, 1, },
	{ AU1300_RTC_MATCH2_INT, IRQ_TYPE_EDGE_RISING,	0, 1, },
	{ AU1300_UART0_INT,	 IRQ_TYPE_LEVEL_HIGH,	1, 1, },
	{ AU1300_SD0_INT,	 IRQ_TYPE_LEVEL_HIGH,	1, 1, },
	{ AU1300_USB_INT,	 IRQ_TYPE_LEVEL_HIGH,	1, 1, },
	{ AU1300_LCD_INT,	 IRQ_TYPE_LEVEL_HIGH,	1, 1, },
	{ AU1300_BSA_INT,	 IRQ_TYPE_LEVEL_HIGH,	1, 1, },
	{ AU1300_MPE_INT,	 IRQ_TYPE_EDGE_RISING,	1, 1, },
	{ AU1300_ITE_INT,	 IRQ_TYPE_LEVEL_HIGH,	1, 1, },
	{ AU1300_AES_INT,	 IRQ_TYPE_LEVEL_HIGH,	1, 1, },
	{ AU1300_CIM_INT,	 IRQ_TYPE_LEVEL_HIGH,	1, 1, },
	{ -1, },	/* terminator */
};


/*
 * au1300_gpic_chgcfg - change PIN configuration.
 * @gpio:	pin to change (0-based GPIO number from datasheet).
 * @clr:	clear all bits set in 'clr'.
 * @set:	set these bits.
 *
 * modifies a pins' configuration register, bits set in @clr will
 * be cleared in the register, bits in @set will be set.
 */
static inline void au1300_gpic_chgcfg(unsigned int gpio,
				      unsigned long clr,
				      unsigned long set)
{
	void __iomem *r = AU1300_GPIC_ADDR;
	unsigned long l;

	r += gpio * 4;	/* offset into pin config array */
	l = __raw_readl(r + AU1300_GPIC_PINCFG);
	l &= ~clr;
	l |= set;
	__raw_writel(l, r + AU1300_GPIC_PINCFG);
	wmb();
}

/*
 * au1300_pinfunc_to_gpio - assign a pin as GPIO input (GPIO ctrl).
 * @pin:	pin (0-based GPIO number from datasheet).
 *
 * Assigns a GPIO pin to the GPIO controller, so its level can either
 * be read or set through the generic GPIO functions.
 * If you need a GPOUT, use au1300_gpio_set_value(pin, 0/1).
 * REVISIT: is this function really necessary?
 */
void au1300_pinfunc_to_gpio(enum au1300_multifunc_pins gpio)
{
	au1300_gpio_direction_input(gpio + AU1300_GPIO_BASE);
}
EXPORT_SYMBOL_GPL(au1300_pinfunc_to_gpio);

/*
 * au1300_pinfunc_to_dev - assign a pin to the device function.
 * @pin:	pin (0-based GPIO number from datasheet).
 *
 * Assigns a GPIO pin to its associated device function; the pin will be
 * driven by the device and not through GPIO functions.
 */
void au1300_pinfunc_to_dev(enum au1300_multifunc_pins gpio)
{
	void __iomem *r = AU1300_GPIC_ADDR;
	unsigned long bit;

	r += GPIC_GPIO_BANKOFF(gpio);
	bit = GPIC_GPIO_TO_BIT(gpio);
	__raw_writel(bit, r + AU1300_GPIC_DEVSEL);
	wmb();
}
EXPORT_SYMBOL_GPL(au1300_pinfunc_to_dev);

/*
 * au1300_set_irq_priority -  set internal priority of IRQ.
 * @irq:	irq to set priority (linux irq number).
 * @p:		priority (0 = highest, 3 = lowest).
 */
void au1300_set_irq_priority(unsigned int irq, int p)
{
	irq -= ALCHEMY_GPIC_INT_BASE;
	au1300_gpic_chgcfg(irq, GPIC_CFG_IL_MASK, GPIC_CFG_IL_SET(p));
}
EXPORT_SYMBOL_GPL(au1300_set_irq_priority);

/*
 * au1300_set_dbdma_gpio - assign a gpio to one of the DBDMA triggers.
 * @dchan:	dbdma trigger select (0, 1).
 * @gpio:	pin to assign as trigger.
 *
 * DBDMA controller has 2 external trigger sources; this function
 * assigns a GPIO to the selected trigger.
 */
void au1300_set_dbdma_gpio(int dchan, unsigned int gpio)
{
	unsigned long r;

	if ((dchan >= 0) && (dchan <= 1)) {
		r = __raw_readl(AU1300_GPIC_ADDR + AU1300_GPIC_DMASEL);
		r &= ~(0xff << (8 * dchan));
		r |= (gpio & 0x7f) << (8 * dchan);
		__raw_writel(r, AU1300_GPIC_ADDR + AU1300_GPIC_DMASEL);
		wmb();
	}
}

/**********************************************************************/

static inline void gpic_pin_set_idlewake(unsigned int gpio, int allow)
{
	au1300_gpic_chgcfg(gpio, GPIC_CFG_IDLEWAKE,
			   allow ? GPIC_CFG_IDLEWAKE : 0);
}

static void au1300_gpic_mask(struct irq_data *d)
{
	void __iomem *r = AU1300_GPIC_ADDR;
	unsigned long bit, irq = d->irq;

	irq -= ALCHEMY_GPIC_INT_BASE;
	r += GPIC_GPIO_BANKOFF(irq);
	bit = GPIC_GPIO_TO_BIT(irq);
	__raw_writel(bit, r + AU1300_GPIC_IDIS);
	wmb();

	gpic_pin_set_idlewake(irq, 0);
}

static void au1300_gpic_unmask(struct irq_data *d)
{
	void __iomem *r = AU1300_GPIC_ADDR;
	unsigned long bit, irq = d->irq;

	irq -= ALCHEMY_GPIC_INT_BASE;

	gpic_pin_set_idlewake(irq, 1);

	r += GPIC_GPIO_BANKOFF(irq);
	bit = GPIC_GPIO_TO_BIT(irq);
	__raw_writel(bit, r + AU1300_GPIC_IEN);
	wmb();
}

static void au1300_gpic_maskack(struct irq_data *d)
{
	void __iomem *r = AU1300_GPIC_ADDR;
	unsigned long bit, irq = d->irq;

	irq -= ALCHEMY_GPIC_INT_BASE;
	r += GPIC_GPIO_BANKOFF(irq);
	bit = GPIC_GPIO_TO_BIT(irq);
	__raw_writel(bit, r + AU1300_GPIC_IPEND);	/* ack */
	__raw_writel(bit, r + AU1300_GPIC_IDIS);	/* mask */
	wmb();

	gpic_pin_set_idlewake(irq, 0);
}

static void au1300_gpic_ack(struct irq_data *d)
{
	void __iomem *r = AU1300_GPIC_ADDR;
	unsigned long bit, irq = d->irq;

	irq -= ALCHEMY_GPIC_INT_BASE;
	r += GPIC_GPIO_BANKOFF(irq);
	bit = GPIC_GPIO_TO_BIT(irq);
	__raw_writel(bit, r + AU1300_GPIC_IPEND);	/* ack */
	wmb();
}

static struct irq_chip au1300_gpic = {
	.name		= "GPIOINT",
	.irq_ack	= au1300_gpic_ack,
	.irq_mask	= au1300_gpic_mask,
	.irq_mask_ack	= au1300_gpic_maskack,
	.irq_unmask	= au1300_gpic_unmask,
	.irq_set_type	= au1300_gpic_settype,
};

static int au1300_gpic_settype(struct irq_data *d, unsigned int type)
{
	unsigned long s;
	unsigned char *name = NULL;
	irq_flow_handler_t hdl = NULL;

	switch (type) {
	case IRQ_TYPE_LEVEL_HIGH:
		s = GPIC_CFG_IC_LEVEL_HIGH;
		name = "high";
		hdl = handle_level_irq;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		s = GPIC_CFG_IC_LEVEL_LOW;
		name = "low";
		hdl = handle_level_irq;
		break;
	case IRQ_TYPE_EDGE_RISING:
		s = GPIC_CFG_IC_EDGE_RISE;
		name = "posedge";
		hdl = handle_edge_irq;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		s = GPIC_CFG_IC_EDGE_FALL;
		name = "negedge";
		hdl = handle_edge_irq;
		break;
	case IRQ_TYPE_EDGE_BOTH:
		s = GPIC_CFG_IC_EDGE_BOTH;
		name = "bothedge";
		hdl = handle_edge_irq;
		break;
	case IRQ_TYPE_NONE:
		s = GPIC_CFG_IC_OFF;
		name = "disabled";
		hdl = handle_level_irq;
		break;
	default:
		return -EINVAL;
	}

	__irq_set_chip_handler_name_locked(d->irq, &au1300_gpic, hdl, name);

	au1300_gpic_chgcfg(d->irq - ALCHEMY_GPIC_INT_BASE, GPIC_CFG_IC_MASK, s);

	return 0;
}

static void __init alchemy_gpic_init_irq(const struct gpic_devint_data *dints)
{
	int i;
	void __iomem *bank_base;

	mips_cpu_irq_init();

	/* disable & ack all possible interrupt sources */
	for (i = 0; i < 4; i++) {
		bank_base = AU1300_GPIC_ADDR + (i * 4);
		__raw_writel(~0UL, bank_base + AU1300_GPIC_IDIS);
		wmb();
		__raw_writel(~0UL, bank_base + AU1300_GPIC_IPEND);
		wmb();
	}

	/* register an irq_chip for them, with 2nd highest priority */
	for (i = ALCHEMY_GPIC_INT_BASE; i <= ALCHEMY_GPIC_INT_LAST; i++) {
		au1300_set_irq_priority(i, 1);
		au1300_gpic_settype(irq_get_irq_data(i), IRQ_TYPE_NONE);
	}

	/* setup known on-chip sources */
	while ((i = dints->irq) != -1) {
		au1300_gpic_settype(irq_get_irq_data(i), dints->type);
		au1300_set_irq_priority(i, dints->prio);

		if (dints->internal)
			au1300_pinfunc_to_dev(i - ALCHEMY_GPIC_INT_BASE);

		dints++;
	}

	set_c0_status(IE_IRQ0 | IE_IRQ1 | IE_IRQ2 | IE_IRQ3);
}

static unsigned long alchemy_gpic_pmdata[ALCHEMY_GPIC_INT_NUM + 6];

static int alchemy_gpic_suspend(void)
{
	void __iomem *base = (void __iomem *)KSEG1ADDR(AU1300_GPIC_PHYS_ADDR);
	int i;

	/* save 4 interrupt mask status registers */
	alchemy_gpic_pmdata[0] = __raw_readl(base + AU1300_GPIC_IEN + 0x0);
	alchemy_gpic_pmdata[1] = __raw_readl(base + AU1300_GPIC_IEN + 0x4);
	alchemy_gpic_pmdata[2] = __raw_readl(base + AU1300_GPIC_IEN + 0x8);
	alchemy_gpic_pmdata[3] = __raw_readl(base + AU1300_GPIC_IEN + 0xc);

	/* save misc register(s) */
	alchemy_gpic_pmdata[4] = __raw_readl(base + AU1300_GPIC_DMASEL);

	/* molto silenzioso */
	__raw_writel(~0UL, base + AU1300_GPIC_IDIS + 0x0);
	__raw_writel(~0UL, base + AU1300_GPIC_IDIS + 0x4);
	__raw_writel(~0UL, base + AU1300_GPIC_IDIS + 0x8);
	__raw_writel(~0UL, base + AU1300_GPIC_IDIS + 0xc);
	wmb();

	/* save pin/int-type configuration */
	base += AU1300_GPIC_PINCFG;
	for (i = 0; i < ALCHEMY_GPIC_INT_NUM; i++)
		alchemy_gpic_pmdata[i + 5] = __raw_readl(base + (i << 2));

	wmb();

	return 0;
}

static void alchemy_gpic_resume(void)
{
	void __iomem *base = (void __iomem *)KSEG1ADDR(AU1300_GPIC_PHYS_ADDR);
	int i;

	/* disable all first */
	__raw_writel(~0UL, base + AU1300_GPIC_IDIS + 0x0);
	__raw_writel(~0UL, base + AU1300_GPIC_IDIS + 0x4);
	__raw_writel(~0UL, base + AU1300_GPIC_IDIS + 0x8);
	__raw_writel(~0UL, base + AU1300_GPIC_IDIS + 0xc);
	wmb();

	/* restore pin/int-type configurations */
	base += AU1300_GPIC_PINCFG;
	for (i = 0; i < ALCHEMY_GPIC_INT_NUM; i++)
		__raw_writel(alchemy_gpic_pmdata[i + 5], base + (i << 2));
	wmb();

	/* restore misc register(s) */
	base = (void __iomem *)KSEG1ADDR(AU1300_GPIC_PHYS_ADDR);
	__raw_writel(alchemy_gpic_pmdata[4], base + AU1300_GPIC_DMASEL);
	wmb();

	/* finally restore masks */
	__raw_writel(alchemy_gpic_pmdata[0], base + AU1300_GPIC_IEN + 0x0);
	__raw_writel(alchemy_gpic_pmdata[1], base + AU1300_GPIC_IEN + 0x4);
	__raw_writel(alchemy_gpic_pmdata[2], base + AU1300_GPIC_IEN + 0x8);
	__raw_writel(alchemy_gpic_pmdata[3], base + AU1300_GPIC_IEN + 0xc);
	wmb();
}

static struct syscore_ops alchemy_gpic_pmops = {
	.suspend	= alchemy_gpic_suspend,
	.resume		= alchemy_gpic_resume,
};

/**********************************************************************/

void __init arch_init_irq(void)
{
	switch (alchemy_get_cputype()) {
	case ALCHEMY_CPU_AU1300:
		alchemy_gpic_init_irq(&au1300_devints[0]);
		register_syscore_ops(&alchemy_gpic_pmops);
		break;
	}
}

#define CAUSEF_GPIC (CAUSEF_IP2 | CAUSEF_IP3 | CAUSEF_IP4 | CAUSEF_IP5)

void plat_irq_dispatch(void)
{
	unsigned long i, c = read_c0_cause() & read_c0_status();

	if (c & CAUSEF_IP7)				/* c0 timer */
		do_IRQ(MIPS_CPU_IRQ_BASE + 7);
	else if (likely(c & CAUSEF_GPIC)) {
		i = __raw_readl(AU1300_GPIC_ADDR + AU1300_GPIC_PRIENC);
		do_IRQ(i + ALCHEMY_GPIC_INT_BASE);
	} else
		spurious_interrupt();
}
