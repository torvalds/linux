/*
 * AUTCPU12 specific defines
 *
 * (c) 2001 Thomas Gleixner, autronix automation <gleixner@autronix.de>
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
#ifndef __ASM_ARCH_AUTCPU12_H
#define __ASM_ARCH_AUTCPU12_H

/* The CS8900A ethernet chip has its I/O registers wired to chip select 2 */
#define AUTCPU12_PHYS_CS8900A		CS2_PHYS_BASE

/*
 * The flash bank is wired to chip select 0
 */
#define AUTCPU12_PHYS_FLASH		CS0_PHYS_BASE		/* physical */

/* offset for device specific information structure */
#define AUTCPU12_LCDINFO_OFFS		(0x00010000)	

/* Videomemory in the internal SRAM (CS 6) */
#define AUTCPU12_PHYS_VIDEO		CS6_PHYS_BASE

/*
* All special IO's are tied to CS1
*/
#define AUTCPU12_PHYS_CHAR_LCD         	CS1_PHYS_BASE +0x00000000  /* physical */

#define AUTCPU12_PHYS_NVRAM            	CS1_PHYS_BASE +0x02000000  /* physical */

#define AUTCPU12_PHYS_CSAUX1           	CS1_PHYS_BASE +0x04000000  /* physical */

#define AUTCPU12_PHYS_SMC              	CS1_PHYS_BASE +0x06000000  /* physical */

#define AUTCPU12_PHYS_CAN              	CS1_PHYS_BASE +0x08000000  /* physical */

#define AUTCPU12_PHYS_TOUCH            	CS1_PHYS_BASE +0x0A000000  /* physical */

#define AUTCPU12_PHYS_IO               	CS1_PHYS_BASE +0x0C000000  /* physical */

#define AUTCPU12_PHYS_LPT              	CS1_PHYS_BASE +0x0E000000  /* physical */

/* 
* defines for smartmedia card access 
*/
#define AUTCPU12_SMC_RDY		(1<<2)
#define AUTCPU12_SMC_ALE		(1<<3)
#define AUTCPU12_SMC_CLE  		(1<<4)
#define AUTCPU12_SMC_PORT_OFFSET	PBDR
#define AUTCPU12_SMC_SELECT_OFFSET 	0x10
/*
* defines for lcd contrast 
*/
#define AUTCPU12_DPOT_PORT_OFFSET	PEDR
#define	AUTCPU12_DPOT_CS		(1<<0)
#define AUTCPU12_DPOT_CLK    		(1<<1)
#define	AUTCPU12_DPOT_UD		(1<<2)

#endif
