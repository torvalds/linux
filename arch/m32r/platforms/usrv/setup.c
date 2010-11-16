/*
 *  linux/arch/m32r/platforms/usrv/setup.c
 *
 *  Setup routines for MITSUBISHI uServer
 *
 *  Copyright (c) 2001, 2002, 2003  Hiroyuki Kondo, Hirokazu Takata,
 *                                  Hitoshi Yamamoto
 */

#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <asm/system.h>
#include <asm/m32r.h>
#include <asm/io.h>

#define irq2port(x) (M32R_ICU_CR1_PORTL + ((x - 1) * sizeof(unsigned long)))

icu_data_t icu_data[M32700UT_NUM_CPU_IRQ];

static void disable_mappi_irq(unsigned int irq)
{
	unsigned long port, data;

	port = irq2port(irq);
	data = icu_data[irq].icucr|M32R_ICUCR_ILEVEL7;
	outl(data, port);
}

static void enable_mappi_irq(unsigned int irq)
{
	unsigned long port, data;

	port = irq2port(irq);
	data = icu_data[irq].icucr|M32R_ICUCR_IEN|M32R_ICUCR_ILEVEL6;
	outl(data, port);
}

static void mask_and_ack_mappi(unsigned int irq)
{
	disable_mappi_irq(irq);
}

static void end_mappi_irq(unsigned int irq)
{
	enable_mappi_irq(irq);
}

static unsigned int startup_mappi_irq(unsigned int irq)
{
	enable_mappi_irq(irq);
	return 0;
}

static void shutdown_mappi_irq(unsigned int irq)
{
	unsigned long port;

	port = irq2port(irq);
	outl(M32R_ICUCR_ILEVEL7, port);
}

static struct irq_chip mappi_irq_type =
{
	.name = "M32700-IRQ",
	.startup = startup_mappi_irq,
	.shutdown = shutdown_mappi_irq,
	.enable = enable_mappi_irq,
	.disable = disable_mappi_irq,
	.ack = mask_and_ack_mappi,
	.end = end_mappi_irq
};

/*
 * Interrupt Control Unit of PLD on M32700UT (Level 2)
 */
#define irq2pldirq(x)		((x) - M32700UT_PLD_IRQ_BASE)
#define pldirq2port(x)		(unsigned long)((int)PLD_ICUCR1 + \
				 (((x) - 1) * sizeof(unsigned short)))

typedef struct {
	unsigned short icucr;  /* ICU Control Register */
} pld_icu_data_t;

static pld_icu_data_t pld_icu_data[M32700UT_NUM_PLD_IRQ];

static void disable_m32700ut_pld_irq(unsigned int irq)
{
	unsigned long port, data;
	unsigned int pldirq;

	pldirq = irq2pldirq(irq);
	port = pldirq2port(pldirq);
	data = pld_icu_data[pldirq].icucr|PLD_ICUCR_ILEVEL7;
	outw(data, port);
}

static void enable_m32700ut_pld_irq(unsigned int irq)
{
	unsigned long port, data;
	unsigned int pldirq;

	pldirq = irq2pldirq(irq);
	port = pldirq2port(pldirq);
	data = pld_icu_data[pldirq].icucr|PLD_ICUCR_IEN|PLD_ICUCR_ILEVEL6;
	outw(data, port);
}

static void mask_and_ack_m32700ut_pld(unsigned int irq)
{
	disable_m32700ut_pld_irq(irq);
}

static void end_m32700ut_pld_irq(unsigned int irq)
{
	enable_m32700ut_pld_irq(irq);
	end_mappi_irq(M32R_IRQ_INT1);
}

static unsigned int startup_m32700ut_pld_irq(unsigned int irq)
{
	enable_m32700ut_pld_irq(irq);
	return 0;
}

static void shutdown_m32700ut_pld_irq(unsigned int irq)
{
	unsigned long port;
	unsigned int pldirq;

	pldirq = irq2pldirq(irq);
	port = pldirq2port(pldirq);
	outw(PLD_ICUCR_ILEVEL7, port);
}

static struct irq_chip m32700ut_pld_irq_type =
{
	.name = "USRV-PLD-IRQ",
	.startup = startup_m32700ut_pld_irq,
	.shutdown = shutdown_m32700ut_pld_irq,
	.enable = enable_m32700ut_pld_irq,
	.disable = disable_m32700ut_pld_irq,
	.ack = mask_and_ack_m32700ut_pld,
	.end = end_m32700ut_pld_irq
};

void __init init_IRQ(void)
{
	static int once = 0;
	int i;

	if (once)
		return;
	else
		once++;

	/* MFT2 : system timer */
	irq_desc[M32R_IRQ_MFT2].status = IRQ_DISABLED;
	irq_desc[M32R_IRQ_MFT2].chip = &mappi_irq_type;
	irq_desc[M32R_IRQ_MFT2].action = 0;
	irq_desc[M32R_IRQ_MFT2].depth = 1;
	icu_data[M32R_IRQ_MFT2].icucr = M32R_ICUCR_IEN;
	disable_mappi_irq(M32R_IRQ_MFT2);

#if defined(CONFIG_SERIAL_M32R_SIO)
	/* SIO0_R : uart receive data */
	irq_desc[M32R_IRQ_SIO0_R].status = IRQ_DISABLED;
	irq_desc[M32R_IRQ_SIO0_R].chip = &mappi_irq_type;
	irq_desc[M32R_IRQ_SIO0_R].action = 0;
	irq_desc[M32R_IRQ_SIO0_R].depth = 1;
	icu_data[M32R_IRQ_SIO0_R].icucr = 0;
	disable_mappi_irq(M32R_IRQ_SIO0_R);

	/* SIO0_S : uart send data */
	irq_desc[M32R_IRQ_SIO0_S].status = IRQ_DISABLED;
	irq_desc[M32R_IRQ_SIO0_S].chip = &mappi_irq_type;
	irq_desc[M32R_IRQ_SIO0_S].action = 0;
	irq_desc[M32R_IRQ_SIO0_S].depth = 1;
	icu_data[M32R_IRQ_SIO0_S].icucr = 0;
	disable_mappi_irq(M32R_IRQ_SIO0_S);

	/* SIO1_R : uart receive data */
	irq_desc[M32R_IRQ_SIO1_R].status = IRQ_DISABLED;
	irq_desc[M32R_IRQ_SIO1_R].chip = &mappi_irq_type;
	irq_desc[M32R_IRQ_SIO1_R].action = 0;
	irq_desc[M32R_IRQ_SIO1_R].depth = 1;
	icu_data[M32R_IRQ_SIO1_R].icucr = 0;
	disable_mappi_irq(M32R_IRQ_SIO1_R);

	/* SIO1_S : uart send data */
	irq_desc[M32R_IRQ_SIO1_S].status = IRQ_DISABLED;
	irq_desc[M32R_IRQ_SIO1_S].chip = &mappi_irq_type;
	irq_desc[M32R_IRQ_SIO1_S].action = 0;
	irq_desc[M32R_IRQ_SIO1_S].depth = 1;
	icu_data[M32R_IRQ_SIO1_S].icucr = 0;
	disable_mappi_irq(M32R_IRQ_SIO1_S);
#endif  /* CONFIG_SERIAL_M32R_SIO */

	/* INT#67-#71: CFC#0 IREQ on PLD */
	for (i = 0 ; i < CONFIG_M32R_CFC_NUM ; i++ ) {
		irq_desc[PLD_IRQ_CF0 + i].status = IRQ_DISABLED;
		irq_desc[PLD_IRQ_CF0 + i].chip = &m32700ut_pld_irq_type;
		irq_desc[PLD_IRQ_CF0 + i].action = 0;
		irq_desc[PLD_IRQ_CF0 + i].depth = 1;	/* disable nested irq */
		pld_icu_data[irq2pldirq(PLD_IRQ_CF0 + i)].icucr
			= PLD_ICUCR_ISMOD01;	/* 'L' level sense */
		disable_m32700ut_pld_irq(PLD_IRQ_CF0 + i);
	}

#if defined(CONFIG_SERIAL_8250) || defined(CONFIG_SERIAL_8250_MODULE)
	/* INT#76: 16552D#0 IREQ on PLD */
	irq_desc[PLD_IRQ_UART0].status = IRQ_DISABLED;
	irq_desc[PLD_IRQ_UART0].chip = &m32700ut_pld_irq_type;
	irq_desc[PLD_IRQ_UART0].action = 0;
	irq_desc[PLD_IRQ_UART0].depth = 1;	/* disable nested irq */
	pld_icu_data[irq2pldirq(PLD_IRQ_UART0)].icucr
		= PLD_ICUCR_ISMOD03;	/* 'H' level sense */
	disable_m32700ut_pld_irq(PLD_IRQ_UART0);

	/* INT#77: 16552D#1 IREQ on PLD */
	irq_desc[PLD_IRQ_UART1].status = IRQ_DISABLED;
	irq_desc[PLD_IRQ_UART1].chip = &m32700ut_pld_irq_type;
	irq_desc[PLD_IRQ_UART1].action = 0;
	irq_desc[PLD_IRQ_UART1].depth = 1;	/* disable nested irq */
	pld_icu_data[irq2pldirq(PLD_IRQ_UART1)].icucr
		= PLD_ICUCR_ISMOD03;	/* 'H' level sense */
	disable_m32700ut_pld_irq(PLD_IRQ_UART1);
#endif	/* CONFIG_SERIAL_8250 || CONFIG_SERIAL_8250_MODULE */

#if defined(CONFIG_IDC_AK4524) || defined(CONFIG_IDC_AK4524_MODULE)
	/* INT#80: AK4524 IREQ on PLD */
	irq_desc[PLD_IRQ_SNDINT].status = IRQ_DISABLED;
	irq_desc[PLD_IRQ_SNDINT].chip = &m32700ut_pld_irq_type;
	irq_desc[PLD_IRQ_SNDINT].action = 0;
	irq_desc[PLD_IRQ_SNDINT].depth = 1;	/* disable nested irq */
	pld_icu_data[irq2pldirq(PLD_IRQ_SNDINT)].icucr
		= PLD_ICUCR_ISMOD01;	/* 'L' level sense */
	disable_m32700ut_pld_irq(PLD_IRQ_SNDINT);
#endif	/* CONFIG_IDC_AK4524 || CONFIG_IDC_AK4524_MODULE */

	/*
	 * INT1# is used for UART, MMC, CF Controller in FPGA.
	 * We enable it here.
	 */
	icu_data[M32R_IRQ_INT1].icucr = M32R_ICUCR_ISMOD11;
	enable_mappi_irq(M32R_IRQ_INT1);
}
