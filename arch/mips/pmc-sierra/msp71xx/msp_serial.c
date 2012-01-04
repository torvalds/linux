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
#include <linux/slab.h>

#include <asm/bootinfo.h>
#include <asm/io.h>
#include <asm/processor.h>
#include <asm/serial.h>
#include <linux/serial_8250.h>

#include <msp_prom.h>
#include <msp_int.h>
#include <msp_regs.h>

struct msp_uart_data {
	int	last_lcr;
};

static void msp_serial_out(struct uart_port *p, int offset, int value)
{
	struct msp_uart_data *d = p->private_data;

	if (offset == UART_LCR)
		d->last_lcr = value;

	offset <<= p->regshift;
	writeb(value, p->membase + offset);
}

static unsigned int msp_serial_in(struct uart_port *p, int offset)
{
	offset <<= p->regshift;

	return readb(p->membase + offset);
}

static int msp_serial_handle_irq(struct uart_port *p)
{
	struct msp_uart_data *d = p->private_data;
	unsigned int iir = readb(p->membase + (UART_IIR << p->regshift));

	if (serial8250_handle_irq(p, iir)) {
		return 1;
	} else if ((iir & UART_IIR_BUSY) == UART_IIR_BUSY) {
		/*
		 * The DesignWare APB UART has an Busy Detect (0x07) interrupt
		 * meaning an LCR write attempt occurred while the UART was
		 * busy. The interrupt must be cleared by reading the UART
		 * status register (USR) and the LCR re-written.
		 *
		 * Note: MSP reserves 0x20 bytes of address space for the UART
		 * and the USR is mapped in a separate block at an offset of
		 * 0xc0 from the start of the UART.
		 */
		(void)readb(p->membase + 0xc0);
		writeb(d->last_lcr, p->membase + (UART_LCR << p->regshift));

		return 1;
	}

	return 0;
}

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
	up.membase      = ioremap_nocache(up.mapbase, MSP_UART_REG_LEN);
	up.irq          = MSP_INT_UART0;
	up.uartclk      = uartclk;
	up.regshift     = 2;
	up.iotype       = UPIO_MEM;
	up.flags        = ASYNC_BOOT_AUTOCONF | ASYNC_SKIP_TEST;
	up.type         = PORT_16550A;
	up.line         = 0;
	up.serial_out	= msp_serial_out;
	up.serial_in	= msp_serial_in;
	up.handle_irq	= msp_serial_handle_irq;
	up.private_data	= kzalloc(sizeof(struct msp_uart_data), GFP_KERNEL);
	if (!up.private_data) {
		pr_err("failed to allocate uart private data\n");
		return;
	}
	if (early_serial_setup(&up)) {
		kfree(up.private_data);
		pr_err("Early serial init of port 0 failed\n");
	}

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
			break;

		default:
			return; /* No second serial port, good-bye. */
	}

	up.mapbase      = MSP_UART1_BASE;
	up.membase      = ioremap_nocache(up.mapbase, MSP_UART_REG_LEN);
	up.irq          = MSP_INT_UART1;
	up.line         = 1;
	up.private_data		= (void*)UART1_STATUS_REG;
	if (early_serial_setup(&up)) {
		kfree(up.private_data);
		pr_err("Early serial init of port 1 failed\n");
	}
}
