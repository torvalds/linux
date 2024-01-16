// SPDX-License-Identifier: GPL-2.0+

#include <asm/machvec.h>
#include "8250.h"

bool alpha_jensen(void)
{
	return !strcmp(alpha_mv.vector_name, "Jensen");
}

void alpha_jensen_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	/*
	 * Digital did something really horribly wrong with the OUT1 and OUT2
	 * lines on Alpha Jensen.  The failure mode is that if either is
	 * cleared, the machine locks up with endless interrupts.
	 */
	mctrl |= TIOCM_OUT1 | TIOCM_OUT2;

	serial8250_do_set_mctrl(port, mctrl);
}
