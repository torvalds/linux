#include <linux/config.h>

#ifdef CONFIG_KGDB

#include <asm/serial.h> /* For the serial port location and base baud */

/* --- CONFIG --- */

typedef unsigned char uint8;
typedef unsigned int uint32;

/* --- END OF CONFIG --- */

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

/* ----------------------------------------------------- */

/* === CONFIG === */

/* [jsun] we use the second serial port for kdb */
#define         BASE                    OCELOT_SERIAL1_BASE
#define         MAX_BAUD                OCELOT_BASE_BAUD

/* === END OF CONFIG === */

#define         REG_OFFSET              4

/* register offset */
#define         OFS_RCV_BUFFER          0
#define         OFS_TRANS_HOLD          0
#define         OFS_SEND_BUFFER         0
#define         OFS_INTR_ENABLE         (1*REG_OFFSET)
#define         OFS_INTR_ID             (2*REG_OFFSET)
#define         OFS_DATA_FORMAT         (3*REG_OFFSET)
#define         OFS_LINE_CONTROL        (3*REG_OFFSET)
#define         OFS_MODEM_CONTROL       (4*REG_OFFSET)
#define         OFS_RS232_OUTPUT        (4*REG_OFFSET)
#define         OFS_LINE_STATUS         (5*REG_OFFSET)
#define         OFS_MODEM_STATUS        (6*REG_OFFSET)
#define         OFS_RS232_INPUT         (6*REG_OFFSET)
#define         OFS_SCRATCH_PAD         (7*REG_OFFSET)

#define         OFS_DIVISOR_LSB         (0*REG_OFFSET)
#define         OFS_DIVISOR_MSB         (1*REG_OFFSET)


/* memory-mapped read/write of the port */
#define         UART16550_READ(y)    (*((volatile uint8*)(BASE + y)))
#define         UART16550_WRITE(y, z)  ((*((volatile uint8*)(BASE + y))) = z)

void debugInit(uint32 baud, uint8 data, uint8 parity, uint8 stop)
{
	/* disable interrupts */
	UART16550_WRITE(OFS_INTR_ENABLE, 0);

	/* set up buad rate */
	{
		uint32 divisor;

		/* set DIAB bit */
		UART16550_WRITE(OFS_LINE_CONTROL, 0x80);

		/* set divisor */
		divisor = MAX_BAUD / baud;
		UART16550_WRITE(OFS_DIVISOR_LSB, divisor & 0xff);
		UART16550_WRITE(OFS_DIVISOR_MSB, (divisor & 0xff00) >> 8);

		/* clear DIAB bit */
		UART16550_WRITE(OFS_LINE_CONTROL, 0x0);
	}

	/* set data format */
	UART16550_WRITE(OFS_DATA_FORMAT, data | parity | stop);
}

static int remoteDebugInitialized = 0;

uint8 getDebugChar(void)
{
	if (!remoteDebugInitialized) {
		remoteDebugInitialized = 1;
		debugInit(UART16550_BAUD_38400,
			  UART16550_DATA_8BIT,
			  UART16550_PARITY_NONE, UART16550_STOP_1BIT);
	}

	while ((UART16550_READ(OFS_LINE_STATUS) & 0x1) == 0);
	return UART16550_READ(OFS_RCV_BUFFER);
}


int putDebugChar(uint8 byte)
{
	if (!remoteDebugInitialized) {
		remoteDebugInitialized = 1;
		debugInit(UART16550_BAUD_38400,
			  UART16550_DATA_8BIT,
			  UART16550_PARITY_NONE, UART16550_STOP_1BIT);
	}

	while ((UART16550_READ(OFS_LINE_STATUS) & 0x20) == 0);
	UART16550_WRITE(OFS_SEND_BUFFER, byte);
	return 1;
}

#endif
