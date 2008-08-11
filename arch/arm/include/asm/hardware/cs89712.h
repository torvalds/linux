/*
 *  arch/arm/include/asm/hardware/cs89712.h
 *
 *  This file contains the hardware definitions of the CS89712
 *  additional internal registers.
 *
 *  Copyright (C) 2001 Thomas Gleixner autronix automation <gleixner@autronix.de>
 *			
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef __ASM_HARDWARE_CS89712_H
#define __ASM_HARDWARE_CS89712_H

/*
*	CS89712 additional registers
*/
                                  
#define PCDR			0x0002	/* Port C Data register ---------------------------- */
#define PCDDR			0x0042	/* Port C Data Direction register ------------------ */
#define SDCONF			0x2300  /* SDRAM Configuration register ---------------------*/
#define SDRFPR			0x2340  /* SDRAM Refresh period register --------------------*/

#define SDCONF_ACTIVE		(1 << 10)
#define SDCONF_CLKCTL		(1 << 9)
#define SDCONF_WIDTH_4		(0 << 7)
#define SDCONF_WIDTH_8		(1 << 7)
#define SDCONF_WIDTH_16		(2 << 7)
#define SDCONF_WIDTH_32		(3 << 7)
#define SDCONF_SIZE_16		(0 << 5)
#define SDCONF_SIZE_64		(1 << 5)
#define SDCONF_SIZE_128		(2 << 5)
#define SDCONF_SIZE_256		(3 << 5)
#define SDCONF_CASLAT_2		(2)
#define SDCONF_CASLAT_3		(3)

#endif /* __ASM_HARDWARE_CS89712_H */
