/*
 *  device driver for Techwell 68xx based cards
 *
 *  Much of this code is derived from the cx88 and sa7134 drivers, which
 *  were in turn derived from the bt87x driver.  The original work was by
 *  Gerd Knorr; more recently the code was enhanced by Mauro Carvalho Chehab,
 *  Hans Verkuil, Andy Walls and many others.  Their work is gratefully
 *  acknowledged.  Full credit goes to them - any problems within this code
 *  are mine.
 *
 *  Copyright (C) 2009  William M. Brack <wbrack@mmm.com.hk>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/i2c.h>		/* must appear before i2c-algo-bit.h */
#include <linux/i2c-algo-bit.h>

#include <media/v4l2-common.h>
#include <media/tveeprom.h>

#include "tw68.h"
#include "tw68-reg.h"

/* commly used strings */
#if 0
static char name_mute[]    = "mute";
static char name_radio[]   = "Radio";
static char name_tv[]      = "Television";
static char name_tv_mono[] = "TV (mono only)";
static char name_svideo[]  = "S-Video";
static char name_comp[]    = "Composite";
#endif
static char name_comp1[]   = "Composite1";
static char name_comp2[]   = "Composite2";
static char name_comp3[]   = "Composite3";
static char name_comp4[]   = "Composite4";

/* ------------------------------------------------------------------ */
/* board config info                                                  */

/* If radio_type !=UNSET, radio_addr should be specified
 */

struct tw68_board tw68_boards[] = {
	[TW68_BOARD_UNKNOWN] = {
		.name		= "GENERIC",
		.tuner_type	= TUNER_ABSENT,
		.radio_type     = UNSET,
		.tuner_addr	= ADDR_UNSET,
		.radio_addr	= ADDR_UNSET,

		.inputs         = {
			{
				.name = name_comp1,
				.vmux = 0,
			}, {
				.name = name_comp2,
				.vmux = 1,
			}, {
				.name = name_comp3,
				.vmux = 2,
			}, {
				.name = name_comp4,
				.vmux = 3,
			}, {	/* Must have a NULL entry at end of list */
				.name = NULL,
				.vmux = 0,
			}
		},
	},
};

const unsigned int tw68_bcount = ARRAY_SIZE(tw68_boards);

/*
 * Please add any new PCI IDs to: http://pci-ids.ucw.cz.  This keeps
 * the PCI ID database up to date.  Note that the entries must be
 * added under vendor 0x1797 (Techwell Inc.) as subsystem IDs.
 */
struct pci_device_id tw68_pci_tbl[] = {
	{
		.vendor		= PCI_VENDOR_ID_TECHWELL,
		.device		= PCI_DEVICE_ID_6800,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.driver_data	= TW68_BOARD_UNKNOWN,
	}, {
		.vendor		= PCI_VENDOR_ID_TECHWELL,
		.device		= PCI_DEVICE_ID_6801,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.driver_data	= TW68_BOARD_UNKNOWN,
	}, {
		.vendor		= PCI_VENDOR_ID_TECHWELL,
		.device		= PCI_DEVICE_ID_6804,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.driver_data	= TW68_BOARD_UNKNOWN,
	}, {
		.vendor		= PCI_VENDOR_ID_TECHWELL,
		.device		= PCI_DEVICE_ID_6816_1,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.driver_data	= TW68_BOARD_UNKNOWN,
	}, {
		.vendor		= PCI_VENDOR_ID_TECHWELL,
		.device		= PCI_DEVICE_ID_6816_2,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.driver_data	= TW68_BOARD_UNKNOWN,
	}, {
		.vendor		= PCI_VENDOR_ID_TECHWELL,
		.device		= PCI_DEVICE_ID_6816_3,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.driver_data	= TW68_BOARD_UNKNOWN,
	}, {
		.vendor		= PCI_VENDOR_ID_TECHWELL,
		.device		= PCI_DEVICE_ID_6816_4,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.driver_data	= TW68_BOARD_UNKNOWN,
	}, {
		/* end of list */
	}
};
MODULE_DEVICE_TABLE(pci, tw68_pci_tbl);

/* ------------------------------------------------------------ */
/* stuff done before i2c enabled */
int tw68_board_init1(struct tw68_dev *dev)
{
	/* Clear GPIO outputs */
	tw_writel(TW68_GPOE, 0);
	/* Remainder of setup according to board ID */
	switch (dev->board) {
	case TW68_BOARD_UNKNOWN:
		printk(KERN_INFO "%s: Unable to determine board type, "
				"using generic values\n", dev->name);
		break;
	}
	dev->input = dev->hw_input = &card_in(dev,0);
	return 0;
}

int tw68_tuner_setup(struct tw68_dev *dev)
{
	return 0;
}

/* stuff which needs working i2c */
int tw68_board_init2(struct tw68_dev *dev)
{
	return 0;
}


