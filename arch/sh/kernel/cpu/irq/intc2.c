/*
 * Interrupt handling for INTC2-based IRQ.
 *
 * Copyright (C) 2001 David J. Mckay (david.mckay@st.com)
 * Copyright (C) 2005, 2006 Paul Mundt (lethal@linux-sh.org)
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * These are the "new Hitachi style" interrupts, as present on the
 * Hitachi 7751, the STM ST40 STB1, SH7760, and SH7780.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <asm/system.h>
#include <asm/io.h>

static void disable_intc2_irq(unsigned int irq)
{
	struct intc2_data *p = get_irq_chip_data(irq);
	ctrl_outl(1 << p->msk_shift,
		  INTC2_BASE + INTC2_INTMSK_OFFSET + p->msk_offset);
}

static void enable_intc2_irq(unsigned int irq)
{
	struct intc2_data *p = get_irq_chip_data(irq);
	ctrl_outl(1 << p->msk_shift,
		  INTC2_BASE + INTC2_INTMSKCLR_OFFSET + p->msk_offset);
}

static struct irq_chip intc2_irq_chip = {
	.name		= "INTC2",
	.mask		= disable_intc2_irq,
	.unmask		= enable_intc2_irq,
	.mask_ack	= disable_intc2_irq,
};

/*
 * Setup an INTC2 style interrupt.
 * NOTE: Unlike IPR interrupts, parameters are not shifted by this code,
 * allowing the use of the numbers straight out of the datasheet.
 * For example:
 *    PIO1 which is INTPRI00[19,16] and INTMSK00[13]
 * would be:               ^     ^             ^  ^
 *                         |     |             |  |
 *    make_intc2_irq(84,   0,   16,            0, 13);
 */
void make_intc2_irq(struct intc2_data *p)
{
	unsigned int flags;
	unsigned long ipr;

	disable_irq_nosync(p->irq);

	/* Set the priority level */
	local_irq_save(flags);

	ipr = ctrl_inl(INTC2_BASE + INTC2_INTPRI_OFFSET + p->ipr_offset);
	ipr &= ~(0xf << p->ipr_shift);
	ipr |= p->priority << p->ipr_shift;
	ctrl_outl(ipr, INTC2_BASE + INTC2_INTPRI_OFFSET + p->ipr_offset);

	local_irq_restore(flags);

	set_irq_chip_and_handler_name(p->irq, &intc2_irq_chip,
				      handle_level_irq, "level");
	set_irq_chip_data(p->irq, p);

	enable_intc2_irq(p->irq);
}

static struct intc2_data intc2_irq_table[] = {
#if defined(CONFIG_CPU_SUBTYPE_ST40)
	{64,  0,  0, 0,  0, 13},	/* PCI serr */
	{65,  0,  4, 0,  1, 13},	/* PCI err */
	{66,  0,  4, 0,  2, 13},	/* PCI ad */
	{67,  0,  4, 0,  3, 13},	/* PCI pwd down */
	{72,  0,  8, 0,  5, 13},	/* DMAC INT0 */
	{73,  0,  8, 0,  6, 13},	/* DMAC INT1 */
	{74,  0,  8, 0,  7, 13},	/* DMAC INT2 */
	{75,  0,  8, 0,  8, 13},	/* DMAC INT3 */
	{76,  0,  8, 0,  9, 13},	/* DMAC INT4 */
	{78,  0,  8, 0, 11, 13},	/* DMAC ERR */
	{80,  0, 12, 0, 12, 13},	/* PIO0 */
	{84,  0, 16, 0, 13, 13},	/* PIO1 */
	{88,  0, 20, 0, 14, 13},	/* PIO2 */
	{112, 4,  0, 4,  0, 13},	/* Mailbox */
 #ifdef CONFIG_CPU_SUBTYPE_ST40GX1
	{116, 4,  4, 4,  4, 13},	/* SSC0 */
	{120, 4,  8, 4,  8, 13},	/* IR Blaster */
	{124, 4, 12, 4, 12, 13},	/* USB host */
	{128, 4, 16, 4, 16, 13},	/* Video processor BLITTER */
	{132, 4, 20, 4, 20, 13},	/* UART0 */
	{134, 4, 20, 4, 22, 13},	/* UART2 */
	{136, 4, 24, 4, 24, 13},	/* IO_PIO0 */
	{140, 4, 28, 4, 28, 13},	/* EMPI */
	{144, 8,  0, 8,  0, 13},	/* MAFE */
	{148, 8,  4, 8,  4, 13},	/* PWM */
	{152, 8,  8, 8,  8, 13},	/* SSC1 */
	{156, 8, 12, 8, 12, 13},	/* IO_PIO1 */
	{160, 8, 16, 8, 16, 13},	/* USB target */
	{164, 8, 20, 8, 20, 13},	/* UART1 */
	{168, 8, 24, 8, 24, 13},	/* Teletext */
	{172, 8, 28, 8, 28, 13},	/* VideoSync VTG */
	{173, 8, 28, 8, 29, 13},	/* VideoSync DVP0 */
	{174, 8, 28, 8, 30, 13},	/* VideoSync DVP1 */
#endif
#elif defined(CONFIG_CPU_SUBTYPE_SH7760)
/*
 * SH7760 INTC2-Style interrupts, vectors IRQ48-111 INTEVT 0x800-0xFE0
 */
	/* INTPRIO0 | INTMSK0 */
	{48,  0, 28, 0, 31,  3},	/* IRQ 4 */
	{49,  0, 24, 0, 30,  3},	/* IRQ 3 */
	{50,  0, 20, 0, 29,  3},	/* IRQ 2 */
	{51,  0, 16, 0, 28,  3},	/* IRQ 1 */
	/* 52-55 (INTEVT 0x880-0x8E0) unused/reserved */
	/* INTPRIO4 | INTMSK0 */
	{56,  4, 28, 0, 25,  3},	/* HCAN2_CHAN0 */
	{57,  4, 24, 0, 24,  3},	/* HCAN2_CHAN1 */
	{58,  4, 20, 0, 23,  3},	/* I2S_CHAN0   */
	{59,  4, 16, 0, 22,  3},	/* I2S_CHAN1   */
	{60,  4, 12, 0, 21,  3},	/* AC97_CHAN0  */
	{61,  4,  8, 0, 20,  3},	/* AC97_CHAN1  */
	{62,  4,  4, 0, 19,  3},	/* I2C_CHAN0   */
	{63,  4,  0, 0, 18,  3},	/* I2C_CHAN1   */
	/* INTPRIO8 | INTMSK0 */
	{52,  8, 16, 0, 11,  3},	/* SCIF0_ERI_IRQ */
	{53,  8, 16, 0, 10,  3},	/* SCIF0_RXI_IRQ */
	{54,  8, 16, 0,  9,  3},	/* SCIF0_BRI_IRQ */
	{55,  8, 16, 0,  8,  3},	/* SCIF0_TXI_IRQ */
	{64,  8, 28, 0, 17,  3},	/* USBHI_IRQ */
	{65,  8, 24, 0, 16,  3},	/* LCDC      */
	/* 66, 67 unused */
	{68,  8, 20, 0, 14, 13},	/* DMABRGI0_IRQ */
	{69,  8, 20, 0, 13, 13},	/* DMABRGI1_IRQ */
	{70,  8, 20, 0, 12, 13},	/* DMABRGI2_IRQ */
	/* 71 unused */
	{72,  8, 12, 0,  7,  3},	/* SCIF1_ERI_IRQ */
	{73,  8, 12, 0,  6,  3},	/* SCIF1_RXI_IRQ */
	{74,  8, 12, 0,  5,  3},	/* SCIF1_BRI_IRQ */
	{75,  8, 12, 0,  4,  3},	/* SCIF1_TXI_IRQ */
	{76,  8,  8, 0,  3,  3},	/* SCIF2_ERI_IRQ */
	{77,  8,  8, 0,  2,  3},	/* SCIF2_RXI_IRQ */
	{78,  8,  8, 0,  1,  3},	/* SCIF2_BRI_IRQ */
	{79,  8,  8, 0,  0,  3},	/* SCIF2_TXI_IRQ */
	/*          | INTMSK4 */
	{80,  8,  4, 4, 23,  3},	/* SIM_ERI */
	{81,  8,  4, 4, 22,  3},	/* SIM_RXI */
	{82,  8,  4, 4, 21,  3},	/* SIM_TXI */
	{83,  8,  4, 4, 20,  3},	/* SIM_TEI */
	{84,  8,  0, 4, 19,  3},	/* HSPII */
	/* INTPRIOC | INTMSK4 */
	/* 85-87 unused/reserved */
	{88, 12, 20, 4, 18,  3},	/* MMCI0 */
	{89, 12, 20, 4, 17,  3},	/* MMCI1 */
	{90, 12, 20, 4, 16,  3},	/* MMCI2 */
	{91, 12, 20, 4, 15,  3},	/* MMCI3 */
	{92, 12, 12, 4,  6,  3},	/* MFI (unsure, bug? in my 7760 manual*/
	/* 93-107 reserved/undocumented */
	{108,12,  4, 4,  1,  3},	/* ADC  */
	{109,12,  0, 4,  0,  3},	/* CMTI */
	/* 110-111 reserved/unused */
#elif defined(CONFIG_CPU_SUBTYPE_SH7780)
	{ TIMER_IRQ, 0, 24, 0, INTC_TMU0_MSK, 2},
	{ 21, 1, 0, 0, INTC_RTC_MSK, TIMER_PRIORITY },
	{ 22, 1, 1, 0, INTC_RTC_MSK, TIMER_PRIORITY },
	{ 23, 1, 2, 0, INTC_RTC_MSK, TIMER_PRIORITY },
	{ SCIF0_ERI_IRQ, 8, 24, 0, INTC_SCIF0_MSK, SCIF0_PRIORITY },
	{ SCIF0_RXI_IRQ, 8, 24, 0, INTC_SCIF0_MSK, SCIF0_PRIORITY },
	{ SCIF0_BRI_IRQ, 8, 24, 0, INTC_SCIF0_MSK, SCIF0_PRIORITY },
	{ SCIF0_TXI_IRQ, 8, 24, 0, INTC_SCIF0_MSK, SCIF0_PRIORITY },

	{ SCIF1_ERI_IRQ, 8, 16, 0, INTC_SCIF1_MSK, SCIF1_PRIORITY },
	{ SCIF1_RXI_IRQ, 8, 16, 0, INTC_SCIF1_MSK, SCIF1_PRIORITY },
	{ SCIF1_BRI_IRQ, 8, 16, 0, INTC_SCIF1_MSK, SCIF1_PRIORITY },
	{ SCIF1_TXI_IRQ, 8, 16, 0, INTC_SCIF1_MSK, SCIF1_PRIORITY },

	{ PCIC0_IRQ, 0x10,  8, 0, INTC_PCIC0_MSK, PCIC0_PRIORITY },
	{ PCIC1_IRQ, 0x10,  0, 0, INTC_PCIC1_MSK, PCIC1_PRIORITY },
	{ PCIC2_IRQ, 0x14, 24, 0, INTC_PCIC2_MSK, PCIC2_PRIORITY },
	{ PCIC3_IRQ, 0x14, 16, 0, INTC_PCIC3_MSK, PCIC3_PRIORITY },
	{ PCIC4_IRQ, 0x14,  8, 0, INTC_PCIC4_MSK, PCIC4_PRIORITY },
#endif
};

void __init init_IRQ_intc2(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(intc2_irq_table); i++)
		make_intc2_irq(intc2_irq_table + i);
}
