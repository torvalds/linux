/*
 *  Copyright (c) 2001 Vojtech Pavlik
 */

/*
 * EMU10k1 - SB Live / Audigy - gameport driver for Linux
 */

/*
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Should you need to contact me, the author, you can do so either by
 * e-mail - mail your message to <vojtech@ucw.cz>, or by paper mail:
 * Vojtech Pavlik, Simunkova 1594, Prague 8, 182 00 Czech Republic
 */

#include <asm/io.h>

#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/gameport.h>
#include <linux/slab.h>
#include <linux/pci.h>

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION("EMU10k1 gameport driver");
MODULE_LICENSE("GPL");

struct emu {
	struct pci_dev *dev;
	struct gameport *gameport;
	int io;
	int size;
};

static const struct pci_device_id emu_tbl[] = {

	{ 0x1102, 0x7002, PCI_ANY_ID, PCI_ANY_ID }, /* SB Live gameport */
	{ 0x1102, 0x7003, PCI_ANY_ID, PCI_ANY_ID }, /* Audigy gameport */
	{ 0x1102, 0x7004, PCI_ANY_ID, PCI_ANY_ID }, /* Dell SB Live */
	{ 0x1102, 0x7005, PCI_ANY_ID, PCI_ANY_ID }, /* Audigy LS gameport */
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, emu_tbl);

static int emu_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct emu *emu;
	struct gameport *port;
	int error;

	emu = kzalloc(sizeof(struct emu), GFP_KERNEL);
	port = gameport_allocate_port();
	if (!emu || !port) {
		printk(KERN_ERR "emu10k1-gp: Memory allocation failed\n");
		error = -ENOMEM;
		goto err_out_free;
	}

	error = pci_enable_device(pdev);
	if (error)
		goto err_out_free;

	emu->io = pci_resource_start(pdev, 0);
	emu->size = pci_resource_len(pdev, 0);

	emu->dev = pdev;
	emu->gameport = port;

	gameport_set_name(port, "EMU10K1");
	gameport_set_phys(port, "pci%s/gameport0", pci_name(pdev));
	port->dev.parent = &pdev->dev;
	port->io = emu->io;

	if (!request_region(emu->io, emu->size, "emu10k1-gp")) {
		printk(KERN_ERR "emu10k1-gp: unable to grab region 0x%x-0x%x\n",
			emu->io, emu->io + emu->size - 1);
		error = -EBUSY;
		goto err_out_disable_dev;
	}

	pci_set_drvdata(pdev, emu);

	gameport_register_port(port);

	return 0;

 err_out_disable_dev:
	pci_disable_device(pdev);
 err_out_free:
	gameport_free_port(port);
	kfree(emu);
	return error;
}

static void emu_remove(struct pci_dev *pdev)
{
	struct emu *emu = pci_get_drvdata(pdev);

	gameport_unregister_port(emu->gameport);
	release_region(emu->io, emu->size);
	kfree(emu);

	pci_disable_device(pdev);
}

static struct pci_driver emu_driver = {
        .name =         "Emu10k1_gameport",
        .id_table =     emu_tbl,
        .probe =        emu_probe,
	.remove =	emu_remove,
};

module_pci_driver(emu_driver);
