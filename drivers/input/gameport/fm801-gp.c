/*
 *  FM801 gameport driver for Linux
 *
 *  Copyright (c) by Takashi Iwai <tiwai@suse.de>
 *
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <asm/io.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/gameport.h>

#define PCI_VENDOR_ID_FORTEMEDIA	0x1319
#define PCI_DEVICE_ID_FM801_GP	0x0802

#define HAVE_COOKED

struct fm801_gp {
	struct gameport *gameport;
	struct resource *res_port;
};

#ifdef HAVE_COOKED
static int fm801_gp_cooked_read(struct gameport *gameport, int *axes, int *buttons)
{
	unsigned short w;

	w = inw(gameport->io + 2);
	*buttons = (~w >> 14) & 0x03;
	axes[0] = (w == 0xffff) ? -1 : ((w & 0x1fff) << 5);
	w = inw(gameport->io + 4);
	axes[1] = (w == 0xffff) ? -1 : ((w & 0x1fff) << 5);
	w = inw(gameport->io + 6);
	*buttons |= ((~w >> 14) & 0x03) << 2;
	axes[2] = (w == 0xffff) ? -1 : ((w & 0x1fff) << 5);
	w = inw(gameport->io + 8);
	axes[3] = (w == 0xffff) ? -1 : ((w & 0x1fff) << 5);
	outw(0xff, gameport->io); /* reset */

        return 0;
}
#endif

static int fm801_gp_open(struct gameport *gameport, int mode)
{
	switch (mode) {
#ifdef HAVE_COOKED
	case GAMEPORT_MODE_COOKED:
		return 0;
#endif
	case GAMEPORT_MODE_RAW:
		return 0;
	default:
		return -1;
	}

	return 0;
}

static int __devinit fm801_gp_probe(struct pci_dev *pci, const struct pci_device_id *id)
{
	struct fm801_gp *gp;
	struct gameport *port;

	gp = kzalloc(sizeof(struct fm801_gp), GFP_KERNEL);
	port = gameport_allocate_port();
	if (!gp || !port) {
		printk(KERN_ERR "fm801-gp: Memory allocation failed\n");
		kfree(gp);
		gameport_free_port(port);
		return -ENOMEM;
	}

	pci_enable_device(pci);

	port->open = fm801_gp_open;
#ifdef HAVE_COOKED
	port->cooked_read = fm801_gp_cooked_read;
#endif
	gameport_set_name(port, "FM801");
	gameport_set_phys(port, "pci%s/gameport0", pci_name(pci));
	port->dev.parent = &pci->dev;
	port->io = pci_resource_start(pci, 0);

	gp->gameport = port;
	gp->res_port = request_region(port->io, 0x10, "FM801 GP");
	if (!gp->res_port) {
		kfree(gp);
		gameport_free_port(port);
		printk(KERN_DEBUG "fm801-gp: unable to grab region 0x%x-0x%x\n",
			port->io, port->io + 0x0f);
		return -EBUSY;
	}

	pci_set_drvdata(pci, gp);

	outb(0x60, port->io + 0x0d); /* enable joystick 1 and 2 */
	gameport_register_port(port);

	return 0;
}

static void __devexit fm801_gp_remove(struct pci_dev *pci)
{
	struct fm801_gp *gp = pci_get_drvdata(pci);

	if (gp) {
		gameport_unregister_port(gp->gameport);
		release_resource(gp->res_port);
		kfree(gp);
	}
}

static struct pci_device_id fm801_gp_id_table[] = {
	{ PCI_VENDOR_ID_FORTEMEDIA, PCI_DEVICE_ID_FM801_GP, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0  },
	{ 0 }
};

static struct pci_driver fm801_gp_driver = {
	.name =		"FM801_gameport",
	.id_table =	fm801_gp_id_table,
	.probe =	fm801_gp_probe,
	.remove =	__devexit_p(fm801_gp_remove),
};

static int __init fm801_gp_init(void)
{
	return pci_register_driver(&fm801_gp_driver);
}

static void __exit fm801_gp_exit(void)
{
	pci_unregister_driver(&fm801_gp_driver);
}

module_init(fm801_gp_init);
module_exit(fm801_gp_exit);

MODULE_DEVICE_TABLE(pci, fm801_gp_id_table);

MODULE_AUTHOR("Takashi Iwai <tiwai@suse.de>");
MODULE_LICENSE("GPL");
