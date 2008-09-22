/*
 * arch/arm/mach-sa1100/include/mach/uncompress.h
 *
 * (C) 1999 Nicolas Pitre <nico@cam.org>
 *
 * Reorganised to be machine independent.
 */

#include "hardware.h"

/*
 * The following code assumes the serial port has already been
 * initialized by the bootloader.  We search for the first enabled
 * port in the most probable order.  If you didn't setup a port in
 * your bootloader then nothing will appear (which might be desired).
 */

#define UART(x)		(*(volatile unsigned long *)(serial_port + (x)))

static void putc(int c)
{
	unsigned long serial_port;

	do {
		serial_port = _Ser3UTCR0;
		if (UART(UTCR3) & UTCR3_TXE) break;
		serial_port = _Ser1UTCR0;
		if (UART(UTCR3) & UTCR3_TXE) break;
		serial_port = _Ser2UTCR0;
		if (UART(UTCR3) & UTCR3_TXE) break;
		return;
	} while (0);

	/* wait for space in the UART's transmitter */
	while (!(UART(UTSR1) & UTSR1_TNF))
		barrier();

	/* send the character out. */
	UART(UTDR) = c;
}

static inline void flush(void)
{
}

/*
 * Nothing to do for these
 */
#define arch_decomp_setup()
#define arch_decomp_wdog()
