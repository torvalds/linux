/*
 * arch/sh64/kernel/led_cayman.c
 *
 * Copyright (C) 2002 Stuart Menefy <stuart.menefy@st.com>
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * Flash the LEDs
 */
#include <asm/io.h>

/*
** It is supposed these functions to be used for a low level
** debugging (via Cayman LEDs), hence to be available as soon
** as possible.
** Unfortunately Cayman LEDs relies on Cayman EPLD to be mapped
** (this happen when IRQ are initialized... quite late).
** These triky dependencies should be removed. Temporary, it
** may be enough to NOP until EPLD is mapped.
*/

extern unsigned long epld_virt;

#define LED_ADDR      (epld_virt + 0x008)
#define HDSP2534_ADDR (epld_virt + 0x100)

void mach_led(int position, int value)
{
	if (!epld_virt)
		return;

	if (value)
		ctrl_outl(0, LED_ADDR);
	else
		ctrl_outl(1, LED_ADDR);

}

void mach_alphanum(int position, unsigned char value)
{
	if (!epld_virt)
		return;

	ctrl_outb(value, HDSP2534_ADDR + 0xe0 + (position << 2));
}

void mach_alphanum_brightness(int setting)
{
	ctrl_outb(setting & 7, HDSP2534_ADDR + 0xc0);
}
