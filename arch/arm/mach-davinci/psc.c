/*
 * TI DaVinci Power and Sleep Controller (PSC)
 *
 * Copyright (C) 2006 Texas Instruments.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>

#include <asm/io.h>
#include <asm/hardware.h>
#include <asm/arch/psc.h>

#define PTCMD	     __REG(0x01C41120)
#define PDSTAT	     __REG(0x01C41200)
#define PDCTL1	     __REG(0x01C41304)
#define EPCPR	     __REG(0x01C41070)
#define PTSTAT	     __REG(0x01C41128)

#define MDSTAT	     IO_ADDRESS(0x01C41800)
#define MDCTL	     IO_ADDRESS(0x01C41A00)

#define PINMUX0	     __REG(0x01c40000)
#define PINMUX1	     __REG(0x01c40004)
#define VDD3P3V_PWDN __REG(0x01C40048)

static void davinci_psc_mux(unsigned int id)
{
	switch (id) {
	case DAVINCI_LPSC_ATA:
		PINMUX0 |= (1 << 17) | (1 << 16);
		break;
	case DAVINCI_LPSC_MMC_SD:
		/* VDD power manupulations are done in U-Boot for CPMAC
		 * so applies to MMC as well
		 */
		/*Set up the pull regiter for MMC */
		VDD3P3V_PWDN = 0x0;
		PINMUX1 &= (~(1 << 9));
		break;
	case DAVINCI_LPSC_I2C:
		PINMUX1 |= (1 << 7);
		break;
	case DAVINCI_LPSC_McBSP:
		PINMUX1 |= (1 << 10);
		break;
	default:
		break;
	}
}

/* Enable or disable a PSC domain */
void davinci_psc_config(unsigned int domain, unsigned int id, char enable)
{
	volatile unsigned int *mdstat = (unsigned int *)((int)MDSTAT + 4 * id);
	volatile unsigned int *mdctl = (unsigned int *)((int)MDCTL + 4 * id);

	if (id < 0)
		return;

	if (enable)
		*mdctl |= 0x00000003;	/* Enable Module */
	else
		*mdctl &= 0xFFFFFFF2;	/* Disable Module */

	if ((PDSTAT & 0x00000001) == 0) {
		PDCTL1 |= 0x1;
		PTCMD = (1 << domain);
		while ((((EPCPR >> domain) & 1) == 0));

		PDCTL1 |= 0x100;
		while (!(((PTSTAT >> domain) & 1) == 0));
	} else {
		PTCMD = (1 << domain);
		while (!(((PTSTAT >> domain) & 1) == 0));
	}

	if (enable)
		while (!((*mdstat & 0x0000001F) == 0x3));
	else
		while (!((*mdstat & 0x0000001F) == 0x2));

	if (enable)
		davinci_psc_mux(id);
}

void __init davinci_psc_init(void)
{
	davinci_psc_config(DAVINCI_GPSC_ARMDOMAIN, DAVINCI_LPSC_VPSSMSTR, 1);
	davinci_psc_config(DAVINCI_GPSC_ARMDOMAIN, DAVINCI_LPSC_VPSSSLV, 1);
	davinci_psc_config(DAVINCI_GPSC_ARMDOMAIN, DAVINCI_LPSC_TPCC, 1);
	davinci_psc_config(DAVINCI_GPSC_ARMDOMAIN, DAVINCI_LPSC_TPTC0, 1);
	davinci_psc_config(DAVINCI_GPSC_ARMDOMAIN, DAVINCI_LPSC_TPTC1, 1);
	davinci_psc_config(DAVINCI_GPSC_ARMDOMAIN, DAVINCI_LPSC_GPIO, 1);

	/* Turn on WatchDog timer LPSC.	 Needed for RESET to work */
	davinci_psc_config(DAVINCI_GPSC_ARMDOMAIN, DAVINCI_LPSC_TIMER2, 1);
}
