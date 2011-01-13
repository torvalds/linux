/*
 * Copyright 2006-2009 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later
 */

#include <asm/dma.h>
#include <asm/portmux.h>

#if defined(CONFIG_BFIN_UART0_CTSRTS) || defined(CONFIG_BFIN_UART1_CTSRTS)
# define CONFIG_SERIAL_BFIN_CTSRTS

# ifndef CONFIG_UART0_CTS_PIN
#  define CONFIG_UART0_CTS_PIN -1
# endif

# ifndef CONFIG_UART0_RTS_PIN
#  define CONFIG_UART0_RTS_PIN -1
# endif

# ifndef CONFIG_UART1_CTS_PIN
#  define CONFIG_UART1_CTS_PIN -1
# endif

# ifndef CONFIG_UART1_RTS_PIN
#  define CONFIG_UART1_RTS_PIN -1
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
	int	uart_cts_pin;
	int	uart_rts_pin;
#endif
};

struct bfin_serial_res bfin_serial_resource[] = {
#ifdef CONFIG_SERIAL_BFIN_UART0
	{
	0xFFC00400,
	IRQ_UART0_RX,
	IRQ_UART0_ERROR,
#ifdef CONFIG_SERIAL_BFIN_DMA
	CH_UART0_TX,
	CH_UART0_RX,
#endif
#ifdef CONFIG_SERIAL_BFIN_CTSRTS
	CONFIG_UART0_CTS_PIN,
	CONFIG_UART0_RTS_PIN,
#endif
	},
#endif
#ifdef CONFIG_SERIAL_BFIN_UART1
	{
	0xFFC02000,
	IRQ_UART1_RX,
	IRQ_UART1_ERROR,
#ifdef CONFIG_SERIAL_BFIN_DMA
	CH_UART1_TX,
	CH_UART1_RX,
#endif
#ifdef CONFIG_SERIAL_BFIN_CTSRTS
	CONFIG_UART1_CTS_PIN,
	CONFIG_UART1_RTS_PIN,
#endif
	},
#endif
};

#define DRIVER_NAME "bfin-uart"

#include <asm/bfin_serial.h>
