// SPDX-License-Identifier: GPL-2.0
#include <linux/serial_sci.h>
#include <linux/serial_core.h>
#include <linux/io.h>
#include <cpu/serial.h>

#define SCPCR 0xA4000116
#define SCPDR 0xA4000136

static void sh770x_sci_init_pins(struct uart_port *port, unsigned int cflag)
{
	unsigned short data;

	/* We need to set SCPCR to enable RTS/CTS */
	data = __raw_readw(SCPCR);
	/* Clear out SCP7MD1,0, SCP6MD1,0, SCP4MD1,0*/
	__raw_writew(data & 0x0fcf, SCPCR);

	if (!(cflag & CRTSCTS)) {
		/* We need to set SCPCR to enable RTS/CTS */
		data = __raw_readw(SCPCR);
		/* Clear out SCP7MD1,0, SCP4MD1,0,
		   Set SCP6MD1,0 = {01} (output)  */
		__raw_writew((data & 0x0fcf) | 0x1000, SCPCR);

		data = __raw_readb(SCPDR);
		/* Set /RTS2 (bit6) = 0 */
		__raw_writeb(data & 0xbf, SCPDR);
	}
}

struct plat_sci_port_ops sh770x_sci_port_ops = {
	.init_pins	= sh770x_sci_init_pins,
};
