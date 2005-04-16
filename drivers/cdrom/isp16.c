/* -- ISP16 cdrom detection and configuration
 *
 *    Copyright (c) 1995,1996 Eric van der Maarel <H.T.M.v.d.Maarel@marin.nl>
 *
 *    Version 0.6
 *
 *    History:
 *    0.5 First release.
 *        Was included in the sjcd and optcd cdrom drivers.
 *    0.6 First "stand-alone" version.
 *        Removed sound configuration.
 *        Added "module" support.
 *
 *      9 November 1999 -- Make kernel-parameter implementation work with 2.3.x 
 *	                   Removed init_module & cleanup_module in favor of 
 *			   module_init & module_exit.
 *			   Torben Mathiasen <tmm@image.dk>
 *
 *     19 June 2004     -- check_region() converted to request_region()
 *                         and return statement cleanups.
 *                         Jesper Juhl <juhl-lkml@dif.dk>
 *
 *    Detect cdrom interface on ISP16 sound card.
 *    Configure cdrom interface.
 *
 *    Algorithm for the card with OPTi 82C928 taken
 *    from the CDSETUP.SYS driver for MSDOS,
 *    by OPTi Computers, version 2.03.
 *    Algorithm for the card with OPTi 82C929 as communicated
 *    to me by Vadim Model and Leo Spiekman.
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#define ISP16_VERSION_MAJOR 0
#define ISP16_VERSION_MINOR 6

#include <linux/module.h>

#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <asm/io.h>
#include "isp16.h"

static short isp16_detect(void);
static short isp16_c928__detect(void);
static short isp16_c929__detect(void);
static short isp16_cdi_config(int base, u_char drive_type, int irq,
			      int dma);
static short isp16_type;	/* dependent on type of interface card */
static u_char isp16_ctrl;
static u_short isp16_enable_port;

static int isp16_cdrom_base = ISP16_CDROM_IO_BASE;
static int isp16_cdrom_irq = ISP16_CDROM_IRQ;
static int isp16_cdrom_dma = ISP16_CDROM_DMA;
static char *isp16_cdrom_type = ISP16_CDROM_TYPE;

module_param(isp16_cdrom_base, int, 0);
module_param(isp16_cdrom_irq, int, 0);
module_param(isp16_cdrom_dma, int, 0);
module_param(isp16_cdrom_type, charp, 0);

#define ISP16_IN(p) (outb(isp16_ctrl,ISP16_CTRL_PORT), inb(p))
#define ISP16_OUT(p,b) (outb(isp16_ctrl,ISP16_CTRL_PORT), outb(b,p))

#ifndef MODULE

static int
__init isp16_setup(char *str)
{
	int ints[4];

	(void) get_options(str, ARRAY_SIZE(ints), ints);
	if (ints[0] > 0)
		isp16_cdrom_base = ints[1];
	if (ints[0] > 1)
		isp16_cdrom_irq = ints[2];
	if (ints[0] > 2)
		isp16_cdrom_dma = ints[3];
	if (str)
		isp16_cdrom_type = str;

	return 1;
}

__setup("isp16=", isp16_setup);

#endif				/* MODULE */

/*
 *  ISP16 initialisation.
 *
 */
static int __init isp16_init(void)
{
	u_char expected_drive;

	printk(KERN_INFO
	       "ISP16: configuration cdrom interface, version %d.%d.\n",
	       ISP16_VERSION_MAJOR, ISP16_VERSION_MINOR);

	if (!strcmp(isp16_cdrom_type, "noisp16")) {
		printk("ISP16: no cdrom interface configured.\n");
		return 0;
	}

	if (!request_region(ISP16_IO_BASE, ISP16_IO_SIZE, "isp16")) {
		printk("ISP16: i/o ports already in use.\n");
		goto out;
	}

	if ((isp16_type = isp16_detect()) < 0) {
		printk("ISP16: no cdrom interface found.\n");
		goto cleanup_out;
	}

	printk(KERN_INFO
	       "ISP16: cdrom interface (with OPTi 82C92%d chip) detected.\n",
	       (isp16_type == 2) ? 9 : 8);

	if (!strcmp(isp16_cdrom_type, "Sanyo"))
		expected_drive =
		    (isp16_type ? ISP16_SANYO1 : ISP16_SANYO0);
	else if (!strcmp(isp16_cdrom_type, "Sony"))
		expected_drive = ISP16_SONY;
	else if (!strcmp(isp16_cdrom_type, "Panasonic"))
		expected_drive =
		    (isp16_type ? ISP16_PANASONIC1 : ISP16_PANASONIC0);
	else if (!strcmp(isp16_cdrom_type, "Mitsumi"))
		expected_drive = ISP16_MITSUMI;
	else {
		printk("ISP16: %s not supported by cdrom interface.\n",
		       isp16_cdrom_type);
		goto cleanup_out;
	}

	if (isp16_cdi_config(isp16_cdrom_base, expected_drive,
			     isp16_cdrom_irq, isp16_cdrom_dma) < 0) {
		printk
		    ("ISP16: cdrom interface has not been properly configured.\n");
		goto cleanup_out;
	}
	printk(KERN_INFO
	       "ISP16: cdrom interface set up with io base 0x%03X, irq %d, dma %d,"
	       " type %s.\n", isp16_cdrom_base, isp16_cdrom_irq,
	       isp16_cdrom_dma, isp16_cdrom_type);
	return 0;

cleanup_out:
	release_region(ISP16_IO_BASE, ISP16_IO_SIZE);
out:
	return -EIO;
}

static short __init isp16_detect(void)
{

	if (isp16_c929__detect() >= 0)
		return 2;
	else
		return (isp16_c928__detect());
}

static short __init isp16_c928__detect(void)
{
	u_char ctrl;
	u_char enable_cdrom;
	u_char io;
	short i = -1;

	isp16_ctrl = ISP16_C928__CTRL;
	isp16_enable_port = ISP16_C928__ENABLE_PORT;

	/* read' and write' are a special read and write, respectively */

	/* read' ISP16_CTRL_PORT, clear last two bits and write' back the result */
	ctrl = ISP16_IN(ISP16_CTRL_PORT) & 0xFC;
	ISP16_OUT(ISP16_CTRL_PORT, ctrl);

	/* read' 3,4 and 5-bit from the cdrom enable port */
	enable_cdrom = ISP16_IN(ISP16_C928__ENABLE_PORT) & 0x38;

	if (!(enable_cdrom & 0x20)) {	/* 5-bit not set */
		/* read' last 2 bits of ISP16_IO_SET_PORT */
		io = ISP16_IN(ISP16_IO_SET_PORT) & 0x03;
		if (((io & 0x01) << 1) == (io & 0x02)) {	/* bits are the same */
			if (io == 0) {	/* ...the same and 0 */
				i = 0;
				enable_cdrom |= 0x20;
			} else {	/* ...the same and 1 *//* my card, first time 'round */
				i = 1;
				enable_cdrom |= 0x28;
			}
			ISP16_OUT(ISP16_C928__ENABLE_PORT, enable_cdrom);
		} else {	/* bits are not the same */
			ISP16_OUT(ISP16_CTRL_PORT, ctrl);
			return i;	/* -> not detected: possibly incorrect conclusion */
		}
	} else if (enable_cdrom == 0x20)
		i = 0;
	else if (enable_cdrom == 0x28)	/* my card, already initialised */
		i = 1;

	ISP16_OUT(ISP16_CTRL_PORT, ctrl);

	return i;
}

static short __init isp16_c929__detect(void)
{
	u_char ctrl;
	u_char tmp;

	isp16_ctrl = ISP16_C929__CTRL;
	isp16_enable_port = ISP16_C929__ENABLE_PORT;

	/* read' and write' are a special read and write, respectively */

	/* read' ISP16_CTRL_PORT and save */
	ctrl = ISP16_IN(ISP16_CTRL_PORT);

	/* write' zero to the ctrl port and get response */
	ISP16_OUT(ISP16_CTRL_PORT, 0);
	tmp = ISP16_IN(ISP16_CTRL_PORT);

	if (tmp != 2)		/* isp16 with 82C929 not detected */
		return -1;

	/* restore ctrl port value */
	ISP16_OUT(ISP16_CTRL_PORT, ctrl);

	return 2;
}

static short __init
isp16_cdi_config(int base, u_char drive_type, int irq, int dma)
{
	u_char base_code;
	u_char irq_code;
	u_char dma_code;
	u_char i;

	if ((drive_type == ISP16_MITSUMI) && (dma != 0))
		printk("ISP16: Mitsumi cdrom drive has no dma support.\n");

	switch (base) {
	case 0x340:
		base_code = ISP16_BASE_340;
		break;
	case 0x330:
		base_code = ISP16_BASE_330;
		break;
	case 0x360:
		base_code = ISP16_BASE_360;
		break;
	case 0x320:
		base_code = ISP16_BASE_320;
		break;
	default:
		printk
		    ("ISP16: base address 0x%03X not supported by cdrom interface.\n",
		     base);
		return -1;
	}
	switch (irq) {
	case 0:
		irq_code = ISP16_IRQ_X;
		break;		/* disable irq */
	case 5:
		irq_code = ISP16_IRQ_5;
		printk("ISP16: irq 5 shouldn't be used by cdrom interface,"
		       " due to possible conflicts with the sound card.\n");
		break;
	case 7:
		irq_code = ISP16_IRQ_7;
		printk("ISP16: irq 7 shouldn't be used by cdrom interface,"
		       " due to possible conflicts with the sound card.\n");
		break;
	case 3:
		irq_code = ISP16_IRQ_3;
		break;
	case 9:
		irq_code = ISP16_IRQ_9;
		break;
	case 10:
		irq_code = ISP16_IRQ_10;
		break;
	case 11:
		irq_code = ISP16_IRQ_11;
		break;
	default:
		printk("ISP16: irq %d not supported by cdrom interface.\n",
		       irq);
		return -1;
	}
	switch (dma) {
	case 0:
		dma_code = ISP16_DMA_X;
		break;		/* disable dma */
	case 1:
		printk("ISP16: dma 1 cannot be used by cdrom interface,"
		       " due to conflict with the sound card.\n");
		return -1;
		break;
	case 3:
		dma_code = ISP16_DMA_3;
		break;
	case 5:
		dma_code = ISP16_DMA_5;
		break;
	case 6:
		dma_code = ISP16_DMA_6;
		break;
	case 7:
		dma_code = ISP16_DMA_7;
		break;
	default:
		printk("ISP16: dma %d not supported by cdrom interface.\n",
		       dma);
		return -1;
	}

	if (drive_type != ISP16_SONY && drive_type != ISP16_PANASONIC0 &&
	    drive_type != ISP16_PANASONIC1 && drive_type != ISP16_SANYO0 &&
	    drive_type != ISP16_SANYO1 && drive_type != ISP16_MITSUMI &&
	    drive_type != ISP16_DRIVE_X) {
		printk
		    ("ISP16: drive type (code 0x%02X) not supported by cdrom"
		     " interface.\n", drive_type);
		return -1;
	}

	/* set type of interface */
	i = ISP16_IN(ISP16_DRIVE_SET_PORT) & ISP16_DRIVE_SET_MASK;	/* clear some bits */
	ISP16_OUT(ISP16_DRIVE_SET_PORT, i | drive_type);

	/* enable cdrom on interface with 82C929 chip */
	if (isp16_type > 1)
		ISP16_OUT(isp16_enable_port, ISP16_ENABLE_CDROM);

	/* set base address, irq and dma */
	i = ISP16_IN(ISP16_IO_SET_PORT) & ISP16_IO_SET_MASK;	/* keep some bits */
	ISP16_OUT(ISP16_IO_SET_PORT, i | base_code | irq_code | dma_code);

	return 0;
}

static void __exit isp16_exit(void)
{
	release_region(ISP16_IO_BASE, ISP16_IO_SIZE);
	printk(KERN_INFO "ISP16: module released.\n");
}

module_init(isp16_init);
module_exit(isp16_exit);

MODULE_LICENSE("GPL");
