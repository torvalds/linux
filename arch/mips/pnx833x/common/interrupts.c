// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  interrupts.c: Interrupt mappings for PNX833X.
 *
 *  Copyright 2008 NXP Semiconductors
 *	  Chris Steel <chris.steel@nxp.com>
 *    Daniel Laird <daniel.j.laird@nxp.com>
 */
#include <linux/kernel.h>
#include <linux/irq.h>
#include <linux/hardirq.h>
#include <linux/interrupt.h>
#include <asm/mipsregs.h>
#include <asm/irq_cpu.h>
#include <asm/setup.h>
#include <irq.h>
#include <irq-mapping.h>
#include <gpio.h>

static int mips_cpu_timer_irq;

static const unsigned int irq_prio[PNX833X_PIC_NUM_IRQ] =
{
    0, /* unused */
    4, /* PNX833X_PIC_I2C0_INT		       1 */
    4, /* PNX833X_PIC_I2C1_INT		       2 */
    1, /* PNX833X_PIC_UART0_INT		       3 */
    1, /* PNX833X_PIC_UART1_INT		       4 */
    6, /* PNX833X_PIC_TS_IN0_DV_INT	       5 */
    6, /* PNX833X_PIC_TS_IN0_DMA_INT	       6 */
    7, /* PNX833X_PIC_GPIO_INT		       7 */
    4, /* PNX833X_PIC_AUDIO_DEC_INT	       8 */
    5, /* PNX833X_PIC_VIDEO_DEC_INT	       9 */
    4, /* PNX833X_PIC_CONFIG_INT	      10 */
    4, /* PNX833X_PIC_AOI_INT		      11 */
    9, /* PNX833X_PIC_SYNC_INT		      12 */
    9, /* PNX8335_PIC_SATA_INT		      13 */
    4, /* PNX833X_PIC_OSD_INT		      14 */
    9, /* PNX833X_PIC_DISP1_INT		      15 */
    4, /* PNX833X_PIC_DEINTERLACER_INT	      16 */
    9, /* PNX833X_PIC_DISPLAY2_INT	      17 */
    4, /* PNX833X_PIC_VC_INT		      18 */
    4, /* PNX833X_PIC_SC_INT		      19 */
    9, /* PNX833X_PIC_IDE_INT		      20 */
    9, /* PNX833X_PIC_IDE_DMA_INT	      21 */
    6, /* PNX833X_PIC_TS_IN1_DV_INT	      22 */
    6, /* PNX833X_PIC_TS_IN1_DMA_INT	      23 */
    4, /* PNX833X_PIC_SGDX_DMA_INT	      24 */
    4, /* PNX833X_PIC_TS_OUT_INT	      25 */
    4, /* PNX833X_PIC_IR_INT		      26 */
    3, /* PNX833X_PIC_VMSP1_INT		      27 */
    3, /* PNX833X_PIC_VMSP2_INT		      28 */
    4, /* PNX833X_PIC_PIBC_INT		      29 */
    4, /* PNX833X_PIC_TS_IN0_TRD_INT	      30 */
    4, /* PNX833X_PIC_SGDX_TPD_INT	      31 */
    5, /* PNX833X_PIC_USB_INT		      32 */
    4, /* PNX833X_PIC_TS_IN1_TRD_INT	      33 */
    4, /* PNX833X_PIC_CLOCK_INT		      34 */
    4, /* PNX833X_PIC_SGDX_PARSER_INT	      35 */
    4, /* PNX833X_PIC_VMSP_DMA_INT	      36 */
#if defined(CONFIG_SOC_PNX8335)
    4, /* PNX8335_PIC_MIU_INT		      37 */
    4, /* PNX8335_PIC_AVCHIP_IRQ_INT	      38 */
    9, /* PNX8335_PIC_SYNC_HD_INT	      39 */
    9, /* PNX8335_PIC_DISP_HD_INT	      40 */
    9, /* PNX8335_PIC_DISP_SCALER_INT	      41 */
    4, /* PNX8335_PIC_OSD_HD1_INT	      42 */
    4, /* PNX8335_PIC_DTL_WRITER_Y_INT	      43 */
    4, /* PNX8335_PIC_DTL_WRITER_C_INT	      44 */
    4, /* PNX8335_PIC_DTL_EMULATOR_Y_IR_INT   45 */
    4, /* PNX8335_PIC_DTL_EMULATOR_C_IR_INT   46 */
    4, /* PNX8335_PIC_DENC_TTX_INT	      47 */
    4, /* PNX8335_PIC_MMI_SIF0_INT	      48 */
    4, /* PNX8335_PIC_MMI_SIF1_INT	      49 */
    4, /* PNX8335_PIC_MMI_CDMMU_INT	      50 */
    4, /* PNX8335_PIC_PIBCS_INT		      51 */
   12, /* PNX8335_PIC_ETHERNET_INT	      52 */
    3, /* PNX8335_PIC_VMSP1_0_INT	      53 */
    3, /* PNX8335_PIC_VMSP1_1_INT	      54 */
    4, /* PNX8335_PIC_VMSP1_DMA_INT	      55 */
    4, /* PNX8335_PIC_TDGR_DE_INT	      56 */
    4, /* PNX8335_PIC_IR1_IRQ_INT	      57 */
#endif
};

static void pnx833x_timer_dispatch(void)
{
	do_IRQ(mips_cpu_timer_irq);
}

static void pic_dispatch(void)
{
	unsigned int irq = PNX833X_REGFIELD(PIC_INT_SRC, INT_SRC);

	if ((irq >= 1) && (irq < (PNX833X_PIC_NUM_IRQ))) {
		unsigned long priority = PNX833X_PIC_INT_PRIORITY;
		PNX833X_PIC_INT_PRIORITY = irq_prio[irq];

		if (irq == PNX833X_PIC_GPIO_INT) {
			unsigned long mask = PNX833X_PIO_INT_STATUS & PNX833X_PIO_INT_ENABLE;
			int pin;
			while ((pin = ffs(mask & 0xffff))) {
				pin -= 1;
				do_IRQ(PNX833X_GPIO_IRQ_BASE + pin);
				mask &= ~(1 << pin);
			}
		} else {
			do_IRQ(irq + PNX833X_PIC_IRQ_BASE);
		}

		PNX833X_PIC_INT_PRIORITY = priority;
	} else {
		printk(KERN_ERR "plat_irq_dispatch: unexpected irq %u\n", irq);
	}
}

asmlinkage void plat_irq_dispatch(void)
{
	unsigned int pending = read_c0_status() & read_c0_cause();

	if (pending & STATUSF_IP4)
		pic_dispatch();
	else if (pending & STATUSF_IP7)
		do_IRQ(PNX833X_TIMER_IRQ);
	else
		spurious_interrupt();
}

static inline void pnx833x_hard_enable_pic_irq(unsigned int irq)
{
	/* Currently we do this by setting IRQ priority to 1.
	   If priority support is being implemented, 1 should be repalced
		by a better value. */
	PNX833X_PIC_INT_REG(irq) = irq_prio[irq];
}

static inline void pnx833x_hard_disable_pic_irq(unsigned int irq)
{
	/* Disable IRQ by writing setting it's priority to 0 */
	PNX833X_PIC_INT_REG(irq) = 0;
}

static DEFINE_RAW_SPINLOCK(pnx833x_irq_lock);

static unsigned int pnx833x_startup_pic_irq(unsigned int irq)
{
	unsigned long flags;
	unsigned int pic_irq = irq - PNX833X_PIC_IRQ_BASE;

	raw_spin_lock_irqsave(&pnx833x_irq_lock, flags);
	pnx833x_hard_enable_pic_irq(pic_irq);
	raw_spin_unlock_irqrestore(&pnx833x_irq_lock, flags);
	return 0;
}

static void pnx833x_enable_pic_irq(struct irq_data *d)
{
	unsigned long flags;
	unsigned int pic_irq = d->irq - PNX833X_PIC_IRQ_BASE;

	raw_spin_lock_irqsave(&pnx833x_irq_lock, flags);
	pnx833x_hard_enable_pic_irq(pic_irq);
	raw_spin_unlock_irqrestore(&pnx833x_irq_lock, flags);
}

static void pnx833x_disable_pic_irq(struct irq_data *d)
{
	unsigned long flags;
	unsigned int pic_irq = d->irq - PNX833X_PIC_IRQ_BASE;

	raw_spin_lock_irqsave(&pnx833x_irq_lock, flags);
	pnx833x_hard_disable_pic_irq(pic_irq);
	raw_spin_unlock_irqrestore(&pnx833x_irq_lock, flags);
}

static DEFINE_RAW_SPINLOCK(pnx833x_gpio_pnx833x_irq_lock);

static void pnx833x_enable_gpio_irq(struct irq_data *d)
{
	int pin = d->irq - PNX833X_GPIO_IRQ_BASE;
	unsigned long flags;
	raw_spin_lock_irqsave(&pnx833x_gpio_pnx833x_irq_lock, flags);
	pnx833x_gpio_enable_irq(pin);
	raw_spin_unlock_irqrestore(&pnx833x_gpio_pnx833x_irq_lock, flags);
}

static void pnx833x_disable_gpio_irq(struct irq_data *d)
{
	int pin = d->irq - PNX833X_GPIO_IRQ_BASE;
	unsigned long flags;
	raw_spin_lock_irqsave(&pnx833x_gpio_pnx833x_irq_lock, flags);
	pnx833x_gpio_disable_irq(pin);
	raw_spin_unlock_irqrestore(&pnx833x_gpio_pnx833x_irq_lock, flags);
}

static int pnx833x_set_type_gpio_irq(struct irq_data *d, unsigned int flow_type)
{
	int pin = d->irq - PNX833X_GPIO_IRQ_BASE;
	int gpio_mode;

	switch (flow_type) {
	case IRQ_TYPE_EDGE_RISING:
		gpio_mode = GPIO_INT_EDGE_RISING;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		gpio_mode = GPIO_INT_EDGE_FALLING;
		break;
	case IRQ_TYPE_EDGE_BOTH:
		gpio_mode = GPIO_INT_EDGE_BOTH;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		gpio_mode = GPIO_INT_LEVEL_HIGH;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		gpio_mode = GPIO_INT_LEVEL_LOW;
		break;
	default:
		gpio_mode = GPIO_INT_NONE;
		break;
	}

	pnx833x_gpio_setup_irq(gpio_mode, pin);

	return 0;
}

static struct irq_chip pnx833x_pic_irq_type = {
	.name = "PNX-PIC",
	.irq_enable = pnx833x_enable_pic_irq,
	.irq_disable = pnx833x_disable_pic_irq,
};

static struct irq_chip pnx833x_gpio_irq_type = {
	.name = "PNX-GPIO",
	.irq_enable = pnx833x_enable_gpio_irq,
	.irq_disable = pnx833x_disable_gpio_irq,
	.irq_set_type = pnx833x_set_type_gpio_irq,
};

void __init arch_init_irq(void)
{
	unsigned int irq;

	/* setup standard internal cpu irqs */
	mips_cpu_irq_init();

	/* Set IRQ information in irq_desc */
	for (irq = PNX833X_PIC_IRQ_BASE; irq < (PNX833X_PIC_IRQ_BASE + PNX833X_PIC_NUM_IRQ); irq++) {
		pnx833x_hard_disable_pic_irq(irq);
		irq_set_chip_and_handler(irq, &pnx833x_pic_irq_type,
					 handle_simple_irq);
	}

	for (irq = PNX833X_GPIO_IRQ_BASE; irq < (PNX833X_GPIO_IRQ_BASE + PNX833X_GPIO_NUM_IRQ); irq++)
		irq_set_chip_and_handler(irq, &pnx833x_gpio_irq_type,
					 handle_simple_irq);

	/* Set PIC priority limiter register to 0 */
	PNX833X_PIC_INT_PRIORITY = 0;

	/* Setup GPIO IRQ dispatching */
	pnx833x_startup_pic_irq(PNX833X_PIC_GPIO_INT);

	/* Enable PIC IRQs (HWIRQ2) */
	if (cpu_has_vint)
		set_vi_handler(4, pic_dispatch);

	write_c0_status(read_c0_status() | IE_IRQ2);
}

unsigned int get_c0_compare_int(void)
{
	if (cpu_has_vint)
		set_vi_handler(cp0_compare_irq, pnx833x_timer_dispatch);

	mips_cpu_timer_irq = MIPS_CPU_IRQ_BASE + cp0_compare_irq;
	return mips_cpu_timer_irq;
}

void __init plat_time_init(void)
{
	/* calculate mips_hpt_frequency based on PNX833X_CLOCK_CPUCP_CTL reg */

	extern unsigned long mips_hpt_frequency;
	unsigned long reg = PNX833X_CLOCK_CPUCP_CTL;

	if (!(PNX833X_BIT(reg, CLOCK_CPUCP_CTL, EXIT_RESET))) {
		/* Functional clock is disabled so use crystal frequency */
		mips_hpt_frequency = 25;
	} else {
#if defined(CONFIG_SOC_PNX8335)
		/* Functional clock is enabled, so get clock multiplier */
		mips_hpt_frequency = 90 + (10 * PNX8335_REGFIELD(CLOCK_PLL_CPU_CTL, FREQ));
#else
		static const unsigned long int freq[4] = {240, 160, 120, 80};
		mips_hpt_frequency = freq[PNX833X_FIELD(reg, CLOCK_CPUCP_CTL, DIV_CLOCK)];
#endif
	}

	printk(KERN_INFO "CPU clock is %ld MHz\n", mips_hpt_frequency);

	mips_hpt_frequency *= 500000;
}
