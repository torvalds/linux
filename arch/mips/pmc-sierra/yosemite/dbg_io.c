/*
 * Copyright 2003 PMC-Sierra
 * Author: Manish Lachwani (lachwani@pmc-sierra.com)
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * Support for KGDB for the Yosemite board. We make use of single serial
 * port to be used for KGDB as well as console. The second serial port
 * seems to be having a problem. Single IRQ is allocated for both the
 * ports. Hence, the interrupt routing code needs to figure out whether
 * the interrupt came from channel A or B.
 */

#include <asm/serial.h>

/*
 * Baud rate, Parity, Data and Stop bit settings for the
 * serial port on the Yosemite. Note that the Early printk
 * patch has been added. So, we should be all set to go
 */
#define	YOSEMITE_BAUD_2400	2400
#define	YOSEMITE_BAUD_4800	4800
#define	YOSEMITE_BAUD_9600	9600
#define	YOSEMITE_BAUD_19200	19200
#define	YOSEMITE_BAUD_38400	38400
#define	YOSEMITE_BAUD_57600	57600
#define	YOSEMITE_BAUD_115200	115200

#define	YOSEMITE_PARITY_NONE	0
#define	YOSEMITE_PARITY_ODD	0x08
#define	YOSEMITE_PARITY_EVEN	0x18
#define	YOSEMITE_PARITY_MARK	0x28
#define	YOSEMITE_PARITY_SPACE	0x38

#define	YOSEMITE_DATA_5BIT	0x0
#define	YOSEMITE_DATA_6BIT	0x1
#define	YOSEMITE_DATA_7BIT	0x2
#define	YOSEMITE_DATA_8BIT	0x3

#define	YOSEMITE_STOP_1BIT	0x0
#define	YOSEMITE_STOP_2BIT	0x4

/* This is crucial */
#define	SERIAL_REG_OFS		0x1

#define	SERIAL_RCV_BUFFER	0x0
#define	SERIAL_TRANS_HOLD	0x0
#define	SERIAL_SEND_BUFFER	0x0
#define	SERIAL_INTR_ENABLE	(1 * SERIAL_REG_OFS)
#define	SERIAL_INTR_ID		(2 * SERIAL_REG_OFS)
#define	SERIAL_DATA_FORMAT	(3 * SERIAL_REG_OFS)
#define	SERIAL_LINE_CONTROL	(3 * SERIAL_REG_OFS)
#define	SERIAL_MODEM_CONTROL	(4 * SERIAL_REG_OFS)
#define	SERIAL_RS232_OUTPUT	(4 * SERIAL_REG_OFS)
#define	SERIAL_LINE_STATUS	(5 * SERIAL_REG_OFS)
#define	SERIAL_MODEM_STATUS	(6 * SERIAL_REG_OFS)
#define	SERIAL_RS232_INPUT	(6 * SERIAL_REG_OFS)
#define	SERIAL_SCRATCH_PAD	(7 * SERIAL_REG_OFS)

#define	SERIAL_DIVISOR_LSB	(0 * SERIAL_REG_OFS)
#define	SERIAL_DIVISOR_MSB	(1 * SERIAL_REG_OFS)

/*
 * Functions to READ and WRITE to serial port 0
 */
#define	SERIAL_READ(ofs)		(*((volatile unsigned char*)	\
					(TITAN_SERIAL_BASE + ofs)))

#define	SERIAL_WRITE(ofs, val)		((*((volatile unsigned char*)	\
					(TITAN_SERIAL_BASE + ofs))) = val)

/*
 * Functions to READ and WRITE to serial port 1
 */
#define	SERIAL_READ_1(ofs)		(*((volatile unsigned char*)	\
					(TITAN_SERIAL_BASE_1 + ofs)

#define	SERIAL_WRITE_1(ofs, val)	((*((volatile unsigned char*)	\
					(TITAN_SERIAL_BASE_1 + ofs))) = val)

/*
 * Second serial port initialization
 */
void init_second_port(void)
{
	/* Disable Interrupts */
	SERIAL_WRITE_1(SERIAL_LINE_CONTROL, 0x0);
	SERIAL_WRITE_1(SERIAL_INTR_ENABLE, 0x0);

	{
		unsigned int divisor;

		SERIAL_WRITE_1(SERIAL_LINE_CONTROL, 0x80);
		divisor = TITAN_SERIAL_BASE_BAUD / YOSEMITE_BAUD_115200;
		SERIAL_WRITE_1(SERIAL_DIVISOR_LSB, divisor & 0xff);

		SERIAL_WRITE_1(SERIAL_DIVISOR_MSB,
			       (divisor & 0xff00) >> 8);
		SERIAL_WRITE_1(SERIAL_LINE_CONTROL, 0x0);
	}

	SERIAL_WRITE_1(SERIAL_DATA_FORMAT, YOSEMITE_DATA_8BIT |
		       YOSEMITE_PARITY_NONE | YOSEMITE_STOP_1BIT);

	/* Enable Interrupts */
	SERIAL_WRITE_1(SERIAL_INTR_ENABLE, 0xf);
}

/* Initialize the serial port for KGDB debugging */
void debugInit(unsigned int baud, unsigned char data, unsigned char parity,
	       unsigned char stop)
{
	/* Disable Interrupts */
	SERIAL_WRITE(SERIAL_LINE_CONTROL, 0x0);
	SERIAL_WRITE(SERIAL_INTR_ENABLE, 0x0);

	{
		unsigned int divisor;

		SERIAL_WRITE(SERIAL_LINE_CONTROL, 0x80);

		divisor = TITAN_SERIAL_BASE_BAUD / baud;
		SERIAL_WRITE(SERIAL_DIVISOR_LSB, divisor & 0xff);

		SERIAL_WRITE(SERIAL_DIVISOR_MSB, (divisor & 0xff00) >> 8);
		SERIAL_WRITE(SERIAL_LINE_CONTROL, 0x0);
	}

	SERIAL_WRITE(SERIAL_DATA_FORMAT, data | parity | stop);
}

static int remoteDebugInitialized = 0;

unsigned char getDebugChar(void)
{
	if (!remoteDebugInitialized) {
		remoteDebugInitialized = 1;
		debugInit(YOSEMITE_BAUD_115200,
			  YOSEMITE_DATA_8BIT,
			  YOSEMITE_PARITY_NONE, YOSEMITE_STOP_1BIT);
	}

	while ((SERIAL_READ(SERIAL_LINE_STATUS) & 0x1) == 0);
	return SERIAL_READ(SERIAL_RCV_BUFFER);
}

int putDebugChar(unsigned char byte)
{
	if (!remoteDebugInitialized) {
		remoteDebugInitialized = 1;
		debugInit(YOSEMITE_BAUD_115200,
			  YOSEMITE_DATA_8BIT,
			  YOSEMITE_PARITY_NONE, YOSEMITE_STOP_1BIT);
	}

	while ((SERIAL_READ(SERIAL_LINE_STATUS) & 0x20) == 0);
	SERIAL_WRITE(SERIAL_SEND_BUFFER, byte);

	return 1;
}
