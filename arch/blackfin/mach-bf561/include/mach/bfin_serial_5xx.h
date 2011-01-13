/*
 * Copyright 2006-2009 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <asm/dma.h>
#include <asm/portmux.h>

#ifdef CONFIG_BFIN_UART0_CTSRTS
# define CONFIG_SERIAL_BFIN_CTSRTS
# ifndef CONFIG_UART0_CTS_PIN
#  define CONFIG_UART0_CTS_PIN -1
# endif
# ifndef CONFIG_UART0_RTS_PIN
#  define CONFIG_UART0_RTS_PIN -1
# endif
#endif

struct bfin_serial_res {
	unsigned long	uart_base_addr;
	int		uart_irq;
	int		uart_status_irq;
#ifdef CONFIG_SERIAL_BFIN_DMA
	unsigned int	uart_tx_dma_channel;
	unsigned int	uart_rx_dma_channel;
#endif
#ifdef CONFIG_SERIAL_BFIN_CTSRTS
	int		uart_cts_pin;
	int		uart_rts_pin;
#endif
};

struct bfin_serial_res bfin_serial_resource[] = {
	{
	0xFFC00400,
	IRQ_UART_RX,
	IRQ_UART_ERROR,
#ifdef CONFIG_SERIAL_BFIN_DMA
	CH_UART_TX,
	CH_UART_RX,
#endif
#ifdef CONFIG_SERIAL_BFIN_CTSRTS
	CONFIG_UART0_CTS_PIN,
	CONFIG_UART0_RTS_PIN,
#endif
	}
};

#define DRIVER_NAME "bfin-uart"

#include <asm/bfin_serial.h>
