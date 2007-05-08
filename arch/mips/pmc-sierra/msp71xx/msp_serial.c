/*
 * The setup file for serial related hardware on PMC-Sierra MSP processors.
 *
 * Copyright 2005 PMC-Sierra, Inc.
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

#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/serial_reg.h>

#include <asm/bootinfo.h>
#include <asm/io.h>
#include <asm/processor.h>
#include <asm/serial.h>

#include <msp_prom.h>
#include <msp_int.h>
#include <msp_regs.h>

#ifdef CONFIG_KGDB
/*
 * kgdb uses serial port 1 so the console can remain on port 0.
 * To use port 0 change the definition to read as follows:
 * #define DEBUG_PORT_BASE KSEG1ADDR(MSP_UART0_BASE)
 */
#define DEBUG_PORT_BASE KSEG1ADDR(MSP_UART1_BASE)

int putDebugChar(char c)
{
	volatile uint32_t *uart = (volatile uint32_t *)DEBUG_PORT_BASE;
	uint32_t val = (uint32_t)c;

	local_irq_disable();
	while( !(uart[5] & 0x20) ); /* Wait for TXRDY */
	uart[0] = val;
	while( !(uart[5] & 0x20) ); /* Wait for TXRDY */
	local_irq_enable();

	return 1;
}

char getDebugChar(void)
{
	volatile uint32_t *uart = (volatile uint32_t *)DEBUG_PORT_BASE;
	uint32_t val;

	while( !(uart[5] & 0x01) ); /* Wait for RXRDY */
	val = uart[0];

	return (char)val;
}

void initDebugPort(unsigned int uartclk, unsigned int baudrate)
{
	unsigned int baud_divisor = (uartclk + 8 * baudrate)/(16 * baudrate);

	/* Enable FIFOs */
	writeb(UART_FCR_ENABLE_FIFO | UART_FCR_CLEAR_RCVR |
			UART_FCR_CLEAR_XMIT | UART_FCR_TRIGGER_4,
		(char *)DEBUG_PORT_BASE + (UART_FCR * 4));

	/* Select brtc divisor */
	writeb(UART_LCR_DLAB, (char *)DEBUG_PORT_BASE + (UART_LCR * 4));

	/* Store divisor lsb */
	writeb(baud_divisor, (char *)DEBUG_PORT_BASE + (UART_TX * 4));

	/* Store divisor msb */
	writeb(baud_divisor >> 8, (char *)DEBUG_PORT_BASE + (UART_IER * 4));

	/* Set 8N1 mode */
	writeb(UART_LCR_WLEN8, (char *)DEBUG_PORT_BASE + (UART_LCR * 4));

	/* Disable flow control */
	writeb(0, (char *)DEBUG_PORT_BASE + (UART_MCR * 4));

	/* Disable receive interrupt(!) */
	writeb(0, (char *)DEBUG_PORT_BASE + (UART_IER * 4));
}
#endif

void __init msp_serial_setup(void)
{
	char    *s;
	char    *endp;
	struct uart_port up;
	unsigned int uartclk;

	memset(&up, 0, sizeof(up));

	/* Check if clock was specified in environment */
	s = prom_getenv("uartfreqhz");
	if(!(s && *s && (uartclk = simple_strtoul(s, &endp, 10)) && *endp == 0))
		uartclk = MSP_BASE_BAUD;
	ppfinit("UART clock set to %d\n", uartclk);

	/* Initialize first serial port */
	up.mapbase      = MSP_UART0_BASE;
	up.membase      = ioremap_nocache(up.mapbase,MSP_UART_REG_LEN);
	up.irq          = MSP_INT_UART0;
	up.uartclk      = uartclk;
	up.regshift     = 2;
	up.iotype       = UPIO_DWAPB; /* UPIO_MEM like */
	up.flags        = STD_COM_FLAGS;
	up.type         = PORT_16550A;
	up.line         = 0;
	up.private_data		= (void*)UART0_STATUS_REG;
	if (early_serial_setup(&up))
		printk(KERN_ERR "Early serial init of port 0 failed\n");

	/* Initialize the second serial port, if one exists */
	switch (mips_machtype) {
		case MACH_MSP4200_EVAL:
		case MACH_MSP4200_GW:
		case MACH_MSP4200_FPGA:
		case MACH_MSP7120_EVAL:
		case MACH_MSP7120_GW:
		case MACH_MSP7120_FPGA:
			/* Enable UART1 on MSP4200 and MSP7120 */
			*GPIO_CFG2_REG = 0x00002299;

#ifdef CONFIG_KGDB
			/* Initialize UART1 for kgdb since PMON doesn't */
			if( DEBUG_PORT_BASE == KSEG1ADDR(MSP_UART1_BASE) ) {
				if( mips_machtype == MACH_MSP4200_FPGA
				 || mips_machtype == MACH_MSP7120_FPGA )
					initDebugPort(uartclk,19200);
				else
					initDebugPort(uartclk,57600);
			}
#endif
			break;

		default:
			return; /* No second serial port, good-bye. */
	}

	up.mapbase      = MSP_UART1_BASE;
	up.membase      = ioremap_nocache(up.mapbase,MSP_UART_REG_LEN);
	up.irq          = MSP_INT_UART1;
	up.line         = 1;
	up.private_data		= (void*)UART1_STATUS_REG;
	if (early_serial_setup(&up))
		printk(KERN_ERR "Early serial init of port 1 failed\n");
}
