/*
 * Copyright 2005-2009 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 *
 * Set up the interrupt priorities
 */

#include <linux/module.h>
#include <linux/irq.h>
#include <asm/blackfin.h>

#include <asm/irq_handler.h>
#include <asm/bfin5xx_spi.h>
#include <asm/bfin_sport.h>
#include <asm/bfin_can.h>
#include <asm/bfin_dma.h>
#include <asm/dpmc.h>

void __init program_IAR(void)
{
	/* Program the IAR0 Register with the configured priority */
	bfin_write_SIC_IAR0(((CONFIG_IRQ_PLL_WAKEUP - 7) << IRQ_PLL_WAKEUP_POS) |
			    ((CONFIG_IRQ_DMA_ERROR - 7) << IRQ_DMA_ERROR_POS) |
			    ((CONFIG_IRQ_ERROR - 7) << IRQ_ERROR_POS) |
			    ((CONFIG_IRQ_RTC - 7) << IRQ_RTC_POS) |
			    ((CONFIG_IRQ_PPI - 7) << IRQ_PPI_POS) |
			    ((CONFIG_IRQ_SPORT0_RX - 7) << IRQ_SPORT0_RX_POS) |
			    ((CONFIG_IRQ_SPORT0_TX - 7) << IRQ_SPORT0_TX_POS) |
			    ((CONFIG_IRQ_SPORT1_RX - 7) << IRQ_SPORT1_RX_POS));

	bfin_write_SIC_IAR1(((CONFIG_IRQ_SPORT1_TX - 7) << IRQ_SPORT1_TX_POS) |
			    ((CONFIG_IRQ_TWI - 7) << IRQ_TWI_POS) |
			    ((CONFIG_IRQ_SPI - 7) << IRQ_SPI_POS) |
			    ((CONFIG_IRQ_UART0_RX - 7) << IRQ_UART0_RX_POS) |
			    ((CONFIG_IRQ_UART0_TX - 7) << IRQ_UART0_TX_POS) |
			    ((CONFIG_IRQ_UART1_RX - 7) << IRQ_UART1_RX_POS) |
			    ((CONFIG_IRQ_UART1_TX - 7) << IRQ_UART1_TX_POS) |
			    ((CONFIG_IRQ_CAN_RX - 7) << IRQ_CAN_RX_POS));

	bfin_write_SIC_IAR2(((CONFIG_IRQ_CAN_TX - 7) << IRQ_CAN_TX_POS) |
			    ((CONFIG_IRQ_MAC_RX - 7) << IRQ_MAC_RX_POS) |
			    ((CONFIG_IRQ_MAC_TX - 7) << IRQ_MAC_TX_POS) |
			    ((CONFIG_IRQ_TIMER0 - 7) << IRQ_TIMER0_POS) |
			    ((CONFIG_IRQ_TIMER1 - 7) << IRQ_TIMER1_POS) |
			    ((CONFIG_IRQ_TIMER2 - 7) << IRQ_TIMER2_POS) |
			    ((CONFIG_IRQ_TIMER3 - 7) << IRQ_TIMER3_POS) |
			    ((CONFIG_IRQ_TIMER4 - 7) << IRQ_TIMER4_POS));

	bfin_write_SIC_IAR3(((CONFIG_IRQ_TIMER5 - 7) << IRQ_TIMER5_POS) |
			    ((CONFIG_IRQ_TIMER6 - 7) << IRQ_TIMER6_POS) |
			    ((CONFIG_IRQ_TIMER7 - 7) << IRQ_TIMER7_POS) |
			    ((CONFIG_IRQ_PROG_INTA - 7) << IRQ_PROG_INTA_POS) |
			    ((CONFIG_IRQ_PORTG_INTB - 7) << IRQ_PORTG_INTB_POS) |
			    ((CONFIG_IRQ_MEM_DMA0 - 7) << IRQ_MEM_DMA0_POS) |
			    ((CONFIG_IRQ_MEM_DMA1 - 7) << IRQ_MEM_DMA1_POS) |
			    ((CONFIG_IRQ_WATCH - 7) << IRQ_WATCH_POS));

	SSYNC();
}

#define SPI_ERR_MASK   (BIT_STAT_TXCOL | BIT_STAT_RBSY | BIT_STAT_MODF | BIT_STAT_TXE)	/* SPI_STAT */
#define SPORT_ERR_MASK (ROVF | RUVF | TOVF | TUVF)	/* SPORT_STAT */
#define PPI_ERR_MASK   (0xFFFF & ~FLD)	/* PPI_STATUS */
#define EMAC_ERR_MASK  (PHYINT | MMCINT | RXFSINT | TXFSINT | WAKEDET | RXDMAERR | TXDMAERR | STMDONE)	/* EMAC_SYSTAT */
#define UART_ERR_MASK  (0x6)	/* UART_IIR */
#define CAN_ERR_MASK   (EWTIF | EWRIF | EPIF | BOIF | WUIF | UIAIF | AAIF | RMLIF | UCEIF | EXTIF | ADIF)	/* CAN_GIF */

static int error_int_mask;

static void bf537_generic_error_mask_irq(struct irq_data *d)
{
	error_int_mask &= ~(1L << (d->irq - IRQ_PPI_ERROR));
	if (!error_int_mask)
		bfin_internal_mask_irq(IRQ_GENERIC_ERROR);
}

static void bf537_generic_error_unmask_irq(struct irq_data *d)
{
	bfin_internal_unmask_irq(IRQ_GENERIC_ERROR);
	error_int_mask |= 1L << (d->irq - IRQ_PPI_ERROR);
}

static struct irq_chip bf537_generic_error_irqchip = {
	.name = "ERROR",
	.irq_ack = bfin_ack_noop,
	.irq_mask_ack = bf537_generic_error_mask_irq,
	.irq_mask = bf537_generic_error_mask_irq,
	.irq_unmask = bf537_generic_error_unmask_irq,
};

static void bf537_demux_error_irq(struct irq_desc *inta_desc)
{
	int irq = 0;

#if (defined(CONFIG_BF537) || defined(CONFIG_BF536))
	if (bfin_read_EMAC_SYSTAT() & EMAC_ERR_MASK)
		irq = IRQ_MAC_ERROR;
	else
#endif
	if (bfin_read_SPORT0_STAT() & SPORT_ERR_MASK)
		irq = IRQ_SPORT0_ERROR;
	else if (bfin_read_SPORT1_STAT() & SPORT_ERR_MASK)
		irq = IRQ_SPORT1_ERROR;
	else if (bfin_read_PPI_STATUS() & PPI_ERR_MASK)
		irq = IRQ_PPI_ERROR;
	else if (bfin_read_CAN_GIF() & CAN_ERR_MASK)
		irq = IRQ_CAN_ERROR;
	else if (bfin_read_SPI_STAT() & SPI_ERR_MASK)
		irq = IRQ_SPI_ERROR;
	else if ((bfin_read_UART0_IIR() & UART_ERR_MASK) == UART_ERR_MASK)
		irq = IRQ_UART0_ERROR;
	else if ((bfin_read_UART1_IIR() & UART_ERR_MASK) == UART_ERR_MASK)
		irq = IRQ_UART1_ERROR;

	if (irq) {
		if (error_int_mask & (1L << (irq - IRQ_PPI_ERROR)))
			bfin_handle_irq(irq);
		else {

			switch (irq) {
			case IRQ_PPI_ERROR:
				bfin_write_PPI_STATUS(PPI_ERR_MASK);
				break;
#if (defined(CONFIG_BF537) || defined(CONFIG_BF536))
			case IRQ_MAC_ERROR:
				bfin_write_EMAC_SYSTAT(EMAC_ERR_MASK);
				break;
#endif
			case IRQ_SPORT0_ERROR:
				bfin_write_SPORT0_STAT(SPORT_ERR_MASK);
				break;

			case IRQ_SPORT1_ERROR:
				bfin_write_SPORT1_STAT(SPORT_ERR_MASK);
				break;

			case IRQ_CAN_ERROR:
				bfin_write_CAN_GIS(CAN_ERR_MASK);
				break;

			case IRQ_SPI_ERROR:
				bfin_write_SPI_STAT(SPI_ERR_MASK);
				break;

			default:
				break;
			}

			pr_debug("IRQ %d:"
				 " MASKED PERIPHERAL ERROR INTERRUPT ASSERTED\n",
				 irq);
		}
	} else
		pr_err("%s: IRQ ?: PERIPHERAL ERROR INTERRUPT ASSERTED BUT NO SOURCE FOUND\n",
		       __func__);

}

#if defined(CONFIG_BFIN_MAC) || defined(CONFIG_BFIN_MAC_MODULE)
static int mac_rx_int_mask;

static void bf537_mac_rx_mask_irq(struct irq_data *d)
{
	mac_rx_int_mask &= ~(1L << (d->irq - IRQ_MAC_RX));
	if (!mac_rx_int_mask)
		bfin_internal_mask_irq(IRQ_PH_INTA_MAC_RX);
}

static void bf537_mac_rx_unmask_irq(struct irq_data *d)
{
	bfin_internal_unmask_irq(IRQ_PH_INTA_MAC_RX);
	mac_rx_int_mask |= 1L << (d->irq - IRQ_MAC_RX);
}

static struct irq_chip bf537_mac_rx_irqchip = {
	.name = "ERROR",
	.irq_ack = bfin_ack_noop,
	.irq_mask_ack = bf537_mac_rx_mask_irq,
	.irq_mask = bf537_mac_rx_mask_irq,
	.irq_unmask = bf537_mac_rx_unmask_irq,
};

static void bf537_demux_mac_rx_irq(struct irq_desc *desc)
{
	if (bfin_read_DMA1_IRQ_STATUS() & (DMA_DONE | DMA_ERR))
		bfin_handle_irq(IRQ_MAC_RX);
	else
		bfin_demux_gpio_irq(desc);
}
#endif

void __init init_mach_irq(void)
{
	int irq;

#if defined(CONFIG_BF537) || defined(CONFIG_BF536)
	/* Clear EMAC Interrupt Status bits so we can demux it later */
	bfin_write_EMAC_SYSTAT(-1);
#endif

	irq_set_chained_handler(IRQ_GENERIC_ERROR, bf537_demux_error_irq);
	for (irq = IRQ_PPI_ERROR; irq <= IRQ_UART1_ERROR; irq++)
		irq_set_chip_and_handler(irq, &bf537_generic_error_irqchip,
					 handle_level_irq);

#if defined(CONFIG_BFIN_MAC) || defined(CONFIG_BFIN_MAC_MODULE)
	irq_set_chained_handler(IRQ_PH_INTA_MAC_RX, bf537_demux_mac_rx_irq);
	irq_set_chip_and_handler(IRQ_MAC_RX, &bf537_mac_rx_irqchip, handle_level_irq);
	irq_set_chip_and_handler(IRQ_PORTH_INTA, &bf537_mac_rx_irqchip, handle_level_irq);

	irq_set_chained_handler(IRQ_MAC_ERROR, bfin_demux_mac_status_irq);
#endif
}
