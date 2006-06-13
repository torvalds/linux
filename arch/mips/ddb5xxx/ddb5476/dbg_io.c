/*
 * kgdb io functions for DDB5476.  We use the second serial port.
 *
 * Copyright (C) 2001 MontaVista Software Inc.
 * Author: jsun@mvista.com or jsun@junsun.net
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

/* ======================= CONFIG ======================== */

/* [jsun] we use the second serial port for kdb */
#define         BASE                    0xa60002f8
#define         MAX_BAUD                115200

/* distance in bytes between two serial registers */
#define         REG_OFFSET              1

/*
 * 0 - kgdb does serial init
 * 1 - kgdb skip serial init
 */
static int remoteDebugInitialized = 0;

/*
 * the default baud rate *if* kgdb does serial init
 */
#define		BAUD_DEFAULT		UART16550_BAUD_38400

/* ======================= END OF CONFIG ======================== */

typedef unsigned char uint8;
typedef unsigned int uint32;

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

        /* set up baud rate */
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


uint8 getDebugChar(void)
{
        if (!remoteDebugInitialized) {
                remoteDebugInitialized = 1;
                debugInit(BAUD_DEFAULT,
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
                debugInit(BAUD_DEFAULT,
                          UART16550_DATA_8BIT,
                          UART16550_PARITY_NONE, UART16550_STOP_1BIT);
        }

        while ((UART16550_READ(OFS_LINE_STATUS) & 0x20) == 0);
        UART16550_WRITE(OFS_SEND_BUFFER, byte);
        return 1;
}
