/*******************************************************************************
*
*   (c) 1998 by Computone Corporation
*
********************************************************************************
*
*
*   PACKAGE:     Linux tty Device Driver for IntelliPort II family of multiport
*                serial I/O controllers.
*
*   DESCRIPTION: Driver constants for configuration and tuning
*
*   NOTES:
*
*******************************************************************************/
#ifndef IP2_H
#define IP2_H

#include "ip2types.h"
#include "i2cmd.h"

/*************/
/* Constants */
/*************/

/* Device major numbers - since version 2.0.26. */
#define IP2_TTY_MAJOR      71
#define IP2_CALLOUT_MAJOR  72
#define IP2_IPL_MAJOR      73

/* Board configuration array.
 * This array defines the hardware irq and address for up to IP2_MAX_BOARDS
 * (4 supported per ip2_types.h) ISA board addresses and irqs MUST be specified,
 * PCI and EISA boards are probed for and automagicly configed
 * iff the addresses are set to 1 and 2 respectivily.
 *    0x0100 - 0x03f0 == ISA
 *	         1        == PCI
 *	         2        == EISA
 *	         0        == (skip this board)
 * This array defines the hardware addresses for them. Special 
 * addresses are EISA and PCI which go sniffing for boards. 

 * In a multiboard system the position in the array determines which port
 * devices are assigned to each board: 
 *		board 0 is assigned ttyF0.. to ttyF63, 
 *		board 1 is assigned ttyF64  to ttyF127,
 *		board 2 is assigned ttyF128 to ttyF191,
 *		board 3 is assigned ttyF192 to ttyF255. 
 *
 * In PCI and EISA bus systems each range is mapped to card in 
 * monotonically increasing slot number order, ISA position is as specified
 * here.

 * If the irqs are ALL set to 0,0,0,0 all boards operate in 
 * polled mode. For interrupt operation ISA boards require that the IRQ be 
 * specified, while PCI and EISA boards any nonzero entry 
 * will enable interrupts using the BIOS configured irq for the board. 
 * An invalid irq entry will default to polled mode for that card and print
 * console warning.
 
 * When the driver is loaded as a module these setting can be overridden on the 
 * modprobe command line or on an option line in /etc/modprobe.conf.
 * If the driver is built-in the configuration must be 
 * set here for ISA cards and address set to 1 and 2 for PCI and EISA.
 *
 * Here is an example that shows most if not all possibe combinations:

 *static ip2config_t ip2config =
 *{
 *	{11,1,0,0},		// irqs
 *	{				// Addresses
 *		0x0308,		// Board 0, ttyF0   - ttyF63// ISA card at io=0x308, irq=11
 *		0x0001,		// Board 1, ttyF64  - ttyF127//PCI card configured by BIOS
 *		0x0000,		// Board 2, ttyF128 - ttyF191// Slot skipped
 *		0x0002		// Board 3, ttyF192 - ttyF255//EISA card configured by BIOS
 *												 // but polled not irq driven
 *	}
 *};
 */

 /* this structure is zeroed out because the suggested method is to configure
  * the driver as a module, set up the parameters with an options line in
  * /etc/modprobe.conf and load with modprobe or kmod, the kernel
  * module loader
  */

 /* This structure is NOW always initialized when the driver is initialized.
  * Compiled in defaults MUST be added to the io and irq arrays in
  * ip2.c.  Those values are configurable from insmod parameters in the
  * case of modules or from command line parameters (ip2=io,irq) when
  * compiled in.
  */

static ip2config_t ip2config =
{
	{0,0,0,0},		// irqs
	{				// Addresses
	/* Do NOT set compile time defaults HERE!  Use the arrays in
		ip2.c!  These WILL be overwritten!  =mhw= */
		0x0000,		// Board 0, ttyF0   - ttyF63
		0x0000,		// Board 1, ttyF64  - ttyF127
		0x0000,		// Board 2, ttyF128 - ttyF191
		0x0000		// Board 3, ttyF192 - ttyF255
	}
};

#endif
