/*
 * Liebherr LWMON board specific definitions
 *
 * Copyright (c) 2001 Wolfgang Denk (wd@denx.de)
 */

#ifndef __MACH_LWMON_H
#define __MACH_LWMON_H

#include <linux/config.h>

#include <asm/ppcboot.h>

#define	IMAP_ADDR	0xFFF00000	/* physical base address of IMMR area	*/
#define IMAP_SIZE	(64 * 1024)	/* mapped size of IMMR area		*/

/*-----------------------------------------------------------------------
 * PCMCIA stuff
 *-----------------------------------------------------------------------
 *
 */
#define PCMCIA_MEM_SIZE		( 64 << 20 )

#define	MAX_HWIFS	1	/* overwrite default in include/asm-ppc/ide.h	*/

/*
 * Definitions for IDE0 Interface
 */
#define IDE0_BASE_OFFSET		0
#define IDE0_DATA_REG_OFFSET		(PCMCIA_MEM_SIZE + 0x320)
#define IDE0_ERROR_REG_OFFSET		(2 * PCMCIA_MEM_SIZE + 0x320 + 1)
#define IDE0_NSECTOR_REG_OFFSET		(2 * PCMCIA_MEM_SIZE + 0x320 + 2)
#define IDE0_SECTOR_REG_OFFSET		(2 * PCMCIA_MEM_SIZE + 0x320 + 3)
#define IDE0_LCYL_REG_OFFSET		(2 * PCMCIA_MEM_SIZE + 0x320 + 4)
#define IDE0_HCYL_REG_OFFSET		(2 * PCMCIA_MEM_SIZE + 0x320 + 5)
#define IDE0_SELECT_REG_OFFSET		(2 * PCMCIA_MEM_SIZE + 0x320 + 6)
#define IDE0_STATUS_REG_OFFSET		(2 * PCMCIA_MEM_SIZE + 0x320 + 7)
#define IDE0_CONTROL_REG_OFFSET		0x0106
#define IDE0_IRQ_REG_OFFSET		0x000A	/* not used			*/

#define	IDE0_INTERRUPT			13

/*
 * Definitions for I2C devices
 */
#define I2C_ADDR_AUDIO		0x28	/* Audio volume control			*/
#define I2C_ADDR_SYSMON		0x2E	/* LM87 System Monitor			*/
#define I2C_ADDR_RTC		0x51	/* PCF8563 RTC				*/
#define I2C_ADDR_POWER_A	0x52	/* PCMCIA/USB power switch, channel A	*/
#define I2C_ADDR_POWER_B	0x53	/* PCMCIA/USB power switch, channel B	*/
#define I2C_ADDR_KEYBD		0x56	/* PIC LWE keyboard			*/
#define I2C_ADDR_PICIO		0x57	/* PIC IO Expander			*/
#define I2C_ADDR_EEPROM		0x58	/* EEPROM AT24C164			*/


/* We don't use the 8259.
*/
#define NR_8259_INTS	0

#endif	/* __MACH_LWMON_H */
