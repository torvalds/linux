#include <linux/types.h>

#include <asm/mach-au1x00/au1000.h>

#ifdef CONFIG_KGDB

/*
 * FIXME the user should be able to select the
 * uart to be used for debugging.
 */
#define DEBUG_BASE  UART_DEBUG_BASE

#define         UART16550_BAUD_2400             2400
#define         UART16550_BAUD_4800             4800
#define         UART16550_BAUD_9600             9600
#define         UART16550_BAUD_19200            19200
#define         UART16550_BAUD_38400            38400
#define         UART16550_BAUD_57600            57600
#define         UART16550_BAUD_115200           115200

#define         UART16550_PARITY_NONE           0
#define         UART16550_PARITY_ODD            0x08
#define         UART16550_PARITY_EVEN           0x18
#define         UART16550_PARITY_MARK           0x28
#define         UART16550_PARITY_SPACE          0x38

#define         UART16550_DATA_5BIT             0x0
#define         UART16550_DATA_6BIT             0x1
#define         UART16550_DATA_7BIT             0x2
#define         UART16550_DATA_8BIT             0x3

#define         UART16550_STOP_1BIT             0x0
#define         UART16550_STOP_2BIT             0x4


#define UART_RX		0	/* Receive buffer */
#define UART_TX		4	/* Transmit buffer */
#define UART_IER	8	/* Interrupt Enable Register */
#define UART_IIR	0xC	/* Interrupt ID Register */
#define UART_FCR	0x10	/* FIFO Control Register */
#define UART_LCR	0x14	/* Line Control Register */
#define UART_MCR	0x18	/* Modem Control Register */
#define UART_LSR	0x1C	/* Line Status Register */
#define UART_MSR	0x20	/* Modem Status Register */
#define UART_CLK	0x28	/* Baud Rat4e Clock Divider */
#define UART_MOD_CNTRL	0x100	/* Module Control */

/* memory-mapped read/write of the port */
#define UART16550_READ(y)     (au_readl(DEBUG_BASE + y) & 0xff)
#define UART16550_WRITE(y, z) (au_writel(z & 0xff, DEBUG_BASE + y))

extern unsigned long calc_clock(void);

void debugInit(u32 baud, u8 data, u8 parity, u8 stop)
{
	if (UART16550_READ(UART_MOD_CNTRL) != 0x3)
		UART16550_WRITE(UART_MOD_CNTRL, 3);
	calc_clock();

	/* disable interrupts */
	UART16550_WRITE(UART_IER, 0);

	/* set up baud rate */
	{
		u32 divisor;

		/* set divisor */
		divisor = get_au1x00_uart_baud_base() / baud;
		UART16550_WRITE(UART_CLK, divisor & 0xffff);
	}

	/* set data format */
	UART16550_WRITE(UART_LCR, (data | parity | stop));
}

static int remoteDebugInitialized;

u8 getDebugChar(void)
{
	if (!remoteDebugInitialized) {
		remoteDebugInitialized = 1;
		debugInit(UART16550_BAUD_115200,
			  UART16550_DATA_8BIT,
			  UART16550_PARITY_NONE,
			  UART16550_STOP_1BIT);
	}

	while ((UART16550_READ(UART_LSR) & 0x1) == 0);
	return UART16550_READ(UART_RX);
}


int putDebugChar(u8 byte)
{
	if (!remoteDebugInitialized) {
		remoteDebugInitialized = 1;
		debugInit(UART16550_BAUD_115200,
			  UART16550_DATA_8BIT,
			  UART16550_PARITY_NONE,
			  UART16550_STOP_1BIT);
	}

	while ((UART16550_READ(UART_LSR) & 0x40) == 0);
	UART16550_WRITE(UART_TX, byte);

	return 1;
}

#endif
