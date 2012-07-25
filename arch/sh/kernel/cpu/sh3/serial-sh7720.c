#include <linux/serial_sci.h>
#include <linux/serial_core.h>
#include <linux/io.h>
#include <cpu/serial.h>
#include <cpu/gpio.h>

static void sh7720_sci_init_pins(struct uart_port *port, unsigned int cflag)
{
	unsigned short data;

	if (cflag & CRTSCTS) {
		/* enable RTS/CTS */
		if (port->mapbase == 0xa4430000) { /* SCIF0 */
			/* Clear PTCR bit 9-2; enable all scif pins but sck */
			data = __raw_readw(PORT_PTCR);
			__raw_writew((data & 0xfc03), PORT_PTCR);
		} else if (port->mapbase == 0xa4438000) { /* SCIF1 */
			/* Clear PVCR bit 9-2 */
			data = __raw_readw(PORT_PVCR);
			__raw_writew((data & 0xfc03), PORT_PVCR);
		}
	} else {
		if (port->mapbase == 0xa4430000) { /* SCIF0 */
			/* Clear PTCR bit 5-2; enable only tx and rx  */
			data = __raw_readw(PORT_PTCR);
			__raw_writew((data & 0xffc3), PORT_PTCR);
		} else if (port->mapbase == 0xa4438000) { /* SCIF1 */
			/* Clear PVCR bit 5-2 */
			data = __raw_readw(PORT_PVCR);
			__raw_writew((data & 0xffc3), PORT_PVCR);
		}
	}
}

struct plat_sci_port_ops sh7720_sci_port_ops = {
	.init_pins	= sh7720_sci_init_pins,
};
