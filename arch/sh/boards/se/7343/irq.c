/*
 * arch/sh/boards/se/7343/irq.c
 *
 */
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/mach/se7343.h>

static void
disable_intreq_irq(unsigned int irq)
{
	int bit = irq - OFFCHIP_IRQ_BASE;
	u16 val;

	val = ctrl_inw(PA_CPLD_IMSK);
	val |= 1 << bit;
	ctrl_outw(val, PA_CPLD_IMSK);
}

static void
enable_intreq_irq(unsigned int irq)
{
	int bit = irq - OFFCHIP_IRQ_BASE;
	u16 val;

	val = ctrl_inw(PA_CPLD_IMSK);
	val &= ~(1 << bit);
	ctrl_outw(val, PA_CPLD_IMSK);
}

static void
mask_and_ack_intreq_irq(unsigned int irq)
{
	disable_intreq_irq(irq);
}

static unsigned int
startup_intreq_irq(unsigned int irq)
{
	enable_intreq_irq(irq);
	return 0;
}

static void
shutdown_intreq_irq(unsigned int irq)
{
	disable_intreq_irq(irq);
}

static void
end_intreq_irq(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED | IRQ_INPROGRESS)))
		enable_intreq_irq(irq);
}

static struct hw_interrupt_type intreq_irq_type = {
	.typename = "FPGA-IRQ",
	.startup = startup_intreq_irq,
	.shutdown = shutdown_intreq_irq,
	.enable = enable_intreq_irq,
	.disable = disable_intreq_irq,
	.ack = mask_and_ack_intreq_irq,
	.end = end_intreq_irq
};

static void
make_intreq_irq(unsigned int irq)
{
	disable_irq_nosync(irq);
	irq_desc[irq].chip = &intreq_irq_type;
	disable_intreq_irq(irq);
}

int
shmse_irq_demux(int irq)
{
	int bit;
	volatile u16 val;

	if (irq == IRQ5_IRQ) {
		/* Read status Register */
		val = ctrl_inw(PA_CPLD_ST);
		bit = ffs(val);
		if (bit != 0)
			return OFFCHIP_IRQ_BASE + bit - 1;
	}
	return irq;
}

/* IRQ5 is multiplexed between the following sources:
 * 1. PC Card socket
 * 2. Extension slot
 * 3. USB Controller
 * 4. Serial Controller
 *
 * We configure IRQ5 as a cascade IRQ.
 */
static struct irqaction irq5 = { no_action, 0, CPU_MASK_NONE, "IRQ5-cascade",
				NULL, NULL};

static struct ipr_data se7343_irq5_ipr_map[] = {
	{ IRQ5_IRQ, IRQ5_IPR_ADDR+2, IRQ5_IPR_POS, IRQ5_PRIORITY },
};
static struct ipr_data se7343_siof0_vpu_ipr_map[] = {
	{ SIOF0_IRQ, SIOF0_IPR_ADDR, SIOF0_IPR_POS, SIOF0_PRIORITY },
	{ VPU_IRQ, VPU_IPR_ADDR, VPU_IPR_POS, 8 },
};
static struct ipr_data se7343_other_ipr_map[] = {
	{ DMTE0_IRQ, DMA1_IPR_ADDR, DMA1_IPR_POS, DMA1_PRIORITY },
	{ DMTE1_IRQ, DMA1_IPR_ADDR, DMA1_IPR_POS, DMA1_PRIORITY },
	{ DMTE2_IRQ, DMA1_IPR_ADDR, DMA1_IPR_POS, DMA1_PRIORITY },
	{ DMTE3_IRQ, DMA1_IPR_ADDR, DMA1_IPR_POS, DMA1_PRIORITY },
	{ DMTE4_IRQ, DMA2_IPR_ADDR, DMA2_IPR_POS, DMA2_PRIORITY },
	{ DMTE5_IRQ, DMA2_IPR_ADDR, DMA2_IPR_POS, DMA2_PRIORITY },

	/* I2C block */
	{ IIC0_ALI_IRQ, IIC0_IPR_ADDR, IIC0_IPR_POS, IIC0_PRIORITY },
	{ IIC0_TACKI_IRQ, IIC0_IPR_ADDR, IIC0_IPR_POS, IIC0_PRIORITY },
	{ IIC0_WAITI_IRQ, IIC0_IPR_ADDR, IIC0_IPR_POS, IIC0_PRIORITY },
	{ IIC0_DTEI_IRQ, IIC0_IPR_ADDR, IIC0_IPR_POS, IIC0_PRIORITY },

	{ IIC1_ALI_IRQ, IIC1_IPR_ADDR, IIC1_IPR_POS, IIC1_PRIORITY },
	{ IIC1_TACKI_IRQ, IIC1_IPR_ADDR, IIC1_IPR_POS, IIC1_PRIORITY },
	{ IIC1_WAITI_IRQ, IIC1_IPR_ADDR, IIC1_IPR_POS, IIC1_PRIORITY },
	{ IIC1_DTEI_IRQ, IIC1_IPR_ADDR, IIC1_IPR_POS, IIC1_PRIORITY },

	/* SIOF */
	{ SIOF0_IRQ, SIOF0_IPR_ADDR, SIOF0_IPR_POS, SIOF0_PRIORITY },

	/* SIU */
	{ SIU_IRQ, SIU_IPR_ADDR, SIU_IPR_POS, SIU_PRIORITY },

	/* VIO interrupt */
	{ CEU_IRQ, VIO_IPR_ADDR, VIO_IPR_POS, VIO_PRIORITY },
	{ BEU_IRQ, VIO_IPR_ADDR, VIO_IPR_POS, VIO_PRIORITY },
	{ VEU_IRQ, VIO_IPR_ADDR, VIO_IPR_POS, VIO_PRIORITY },

	/*MFI interrupt*/

	{ MFI_IRQ, MFI_IPR_ADDR, MFI_IPR_POS, MFI_PRIORITY },

	/* LCD controller */
	{ LCDC_IRQ, LCDC_IPR_ADDR, LCDC_IPR_POS, LCDC_PRIORITY },
};

/*
 * Initialize IRQ setting
 */
void __init
init_7343se_IRQ(void)
{
	/* Setup Multiplexed interrupts */
	ctrl_outw(8, PA_CPLD_MODESET);	/* Set all CPLD interrupts to active
					 * low.
					 */
	/* Mask all CPLD controller interrupts */
	ctrl_outw(0x0fff, PA_CPLD_IMSK);

	/* PC Card interrupts */
	make_intreq_irq(PC_IRQ0);
	make_intreq_irq(PC_IRQ1);
	make_intreq_irq(PC_IRQ2);
	make_intreq_irq(PC_IRQ3);

	/* Extension Slot Interrupts */
	make_intreq_irq(EXT_IRQ0);
	make_intreq_irq(EXT_IRQ1);
	make_intreq_irq(EXT_IRQ2);
	make_intreq_irq(EXT_IRQ3);

	/* USB Controller interrupts */
	make_intreq_irq(USB_IRQ0);
	make_intreq_irq(USB_IRQ1);

	/* Serial Controller interrupts */
	make_intreq_irq(UART_IRQ0);
	make_intreq_irq(UART_IRQ1);

	/* Setup all external interrupts to be active low */
	ctrl_outw(0xaaaa, INTC_ICR1);

	make_ipr_irq(se7343_irq5_ipr_map, ARRAY_SIZE(se7343_irq5_ipr_map));

	setup_irq(IRQ5_IRQ, &irq5);
	/* Set port control to use IRQ5 */
	*(u16 *)0xA4050108 &= ~0xc;

	make_ipr_irq(se7343_siof0_vpu_ipr_map, ARRAY_SIZE(se7343_siof0_vpu_ipr_map));

	ctrl_outb(0x0f, INTC_IMCR5);	/* enable SCIF IRQ */

	make_ipr_irq(se7343_other_ipr_map, ARRAY_SIZE(se7343_other_ipr_map));

	ctrl_outw(0x2000, PA_MRSHPC + 0x0c);	/* mrshpc irq enable */
}
