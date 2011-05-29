/*
 * Support for common PCI multi-I/O cards (which is most of them)
 *
 * Copyright (C) 2001  Tim Waugh <twaugh@redhat.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 *
 * Multi-function PCI cards are supposed to present separate logical
 * devices on the bus.  A common thing to do seems to be to just use
 * one logical device with lots of base address registers for both
 * parallel ports and serial ports.  This driver is for dealing with
 * that.
 *
 */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/parport.h>
#include <linux/parport_pc.h>
#include <linux/8250_pci.h>

enum parport_pc_pci_cards {
	titan_110l = 0,
	titan_210l,
	netmos_9xx5_combo,
	netmos_9855,
	netmos_9855_2p,
	netmos_9900,
	netmos_9900_2p,
	netmos_99xx_1p,
	avlab_1s1p,
	avlab_1s2p,
	avlab_2s1p,
	siig_1s1p_10x,
	siig_2s1p_10x,
	siig_2p1s_20x,
	siig_1s1p_20x,
	siig_2s1p_20x,
	timedia_4078a,
	timedia_4079h,
	timedia_4085h,
	timedia_4088a,
	timedia_4089a,
	timedia_4095a,
	timedia_4096a,
	timedia_4078u,
	timedia_4079a,
	timedia_4085u,
	timedia_4079r,
	timedia_4079s,
	timedia_4079d,
	timedia_4079e,
	timedia_4079f,
	timedia_9079a,
	timedia_9079b,
	timedia_9079c,
};

/* each element directly indexed from enum list, above */
struct parport_pc_pci {
	int numports;
	struct { /* BAR (base address registers) numbers in the config
                    space header */
		int lo;
		int hi; /* -1 if not there, >6 for offset-method (max
                           BAR is 6) */
	} addr[4];

	/* If set, this is called immediately after pci_enable_device.
	 * If it returns non-zero, no probing will take place and the
	 * ports will not be used. */
	int (*preinit_hook) (struct pci_dev *pdev, struct parport_pc_pci *card,
				int autoirq, int autodma);

	/* If set, this is called after probing for ports.  If 'failed'
	 * is non-zero we couldn't use any of the ports. */
	void (*postinit_hook) (struct pci_dev *pdev,
				struct parport_pc_pci *card, int failed);
};

static int __devinit netmos_parallel_init(struct pci_dev *dev, struct parport_pc_pci *par, int autoirq, int autodma)
{
	/* the rule described below doesn't hold for this device */
	if (dev->device == PCI_DEVICE_ID_NETMOS_9835 &&
			dev->subsystem_vendor == PCI_VENDOR_ID_IBM &&
			dev->subsystem_device == 0x0299)
		return -ENODEV;

	if (dev->device == PCI_DEVICE_ID_NETMOS_9912) {
		par->numports = 1;
	} else {
		/*
		 * Netmos uses the subdevice ID to indicate the number of parallel
		 * and serial ports.  The form is 0x00PS, where <P> is the number of
		 * parallel ports and <S> is the number of serial ports.
		 */
		par->numports = (dev->subsystem_device & 0xf0) >> 4;
		if (par->numports > ARRAY_SIZE(par->addr))
			par->numports = ARRAY_SIZE(par->addr);
	}

	return 0;
}

static struct parport_pc_pci cards[] __devinitdata = {
	/* titan_110l */		{ 1, { { 3, -1 }, } },
	/* titan_210l */		{ 1, { { 3, -1 }, } },
	/* netmos_9xx5_combo */		{ 1, { { 2, -1 }, }, netmos_parallel_init },
	/* netmos_9855 */		{ 1, { { 0, -1 }, }, netmos_parallel_init },
	/* netmos_9855_2p */		{ 2, { { 0, -1 }, { 2, -1 }, } },
	/* netmos_9900 */		{1, { { 3, 4 }, }, netmos_parallel_init },
	/* netmos_9900_2p */		{2, { { 0, 1 }, { 3, 4 }, } },
	/* netmos_99xx_1p */		{1, { { 0, 1 }, } },
	/* avlab_1s1p     */		{ 1, { { 1, 2}, } },
	/* avlab_1s2p     */		{ 2, { { 1, 2}, { 3, 4 },} },
	/* avlab_2s1p     */		{ 1, { { 2, 3}, } },
	/* siig_1s1p_10x */		{ 1, { { 3, 4 }, } },
	/* siig_2s1p_10x */		{ 1, { { 4, 5 }, } },
	/* siig_2p1s_20x */		{ 2, { { 1, 2 }, { 3, 4 }, } },
	/* siig_1s1p_20x */		{ 1, { { 1, 2 }, } },
	/* siig_2s1p_20x */		{ 1, { { 2, 3 }, } },
	/* timedia_4078a */		{ 1, { { 2, -1 }, } },
	/* timedia_4079h */             { 1, { { 2, 3 }, } },
	/* timedia_4085h */             { 2, { { 2, -1 }, { 4, -1 }, } },
	/* timedia_4088a */             { 2, { { 2, 3 }, { 4, 5 }, } },
	/* timedia_4089a */             { 2, { { 2, 3 }, { 4, 5 }, } },
	/* timedia_4095a */             { 2, { { 2, 3 }, { 4, 5 }, } },
	/* timedia_4096a */             { 2, { { 2, 3 }, { 4, 5 }, } },
	/* timedia_4078u */             { 1, { { 2, -1 }, } },
	/* timedia_4079a */             { 1, { { 2, 3 }, } },
	/* timedia_4085u */             { 2, { { 2, -1 }, { 4, -1 }, } },
	/* timedia_4079r */             { 1, { { 2, 3 }, } },
	/* timedia_4079s */             { 1, { { 2, 3 }, } },
	/* timedia_4079d */             { 1, { { 2, 3 }, } },
	/* timedia_4079e */             { 1, { { 2, 3 }, } },
	/* timedia_4079f */             { 1, { { 2, 3 }, } },
	/* timedia_9079a */             { 1, { { 2, 3 }, } },
	/* timedia_9079b */             { 1, { { 2, 3 }, } },
	/* timedia_9079c */             { 1, { { 2, 3 }, } },
};

static struct pci_device_id parport_serial_pci_tbl[] = {
	/* PCI cards */
	{ PCI_VENDOR_ID_TITAN, PCI_DEVICE_ID_TITAN_110L,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, titan_110l },
	{ PCI_VENDOR_ID_TITAN, PCI_DEVICE_ID_TITAN_210L,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, titan_210l },
	{ PCI_VENDOR_ID_NETMOS, PCI_DEVICE_ID_NETMOS_9735,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, netmos_9xx5_combo },
	{ PCI_VENDOR_ID_NETMOS, PCI_DEVICE_ID_NETMOS_9745,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, netmos_9xx5_combo },
	{ PCI_VENDOR_ID_NETMOS, PCI_DEVICE_ID_NETMOS_9835,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, netmos_9xx5_combo },
	{ PCI_VENDOR_ID_NETMOS, PCI_DEVICE_ID_NETMOS_9845,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, netmos_9xx5_combo },
	{ PCI_VENDOR_ID_NETMOS, PCI_DEVICE_ID_NETMOS_9855,
	  0x1000, 0x0020, 0, 0, netmos_9855_2p },
	{ PCI_VENDOR_ID_NETMOS, PCI_DEVICE_ID_NETMOS_9855,
	  0x1000, 0x0022, 0, 0, netmos_9855_2p },
	{ PCI_VENDOR_ID_NETMOS, PCI_DEVICE_ID_NETMOS_9855,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, netmos_9855 },
	{ PCI_VENDOR_ID_NETMOS, PCI_DEVICE_ID_NETMOS_9900,
	  0xA000, 0x3011, 0, 0, netmos_9900 },
	{ PCI_VENDOR_ID_NETMOS, PCI_DEVICE_ID_NETMOS_9900,
	  0xA000, 0x3012, 0, 0, netmos_9900 },
	{ PCI_VENDOR_ID_NETMOS, PCI_DEVICE_ID_NETMOS_9900,
	  0xA000, 0x3020, 0, 0, netmos_9900_2p },
	{ PCI_VENDOR_ID_NETMOS, PCI_DEVICE_ID_NETMOS_9912,
	  0xA000, 0x2000, 0, 0, netmos_99xx_1p },
	/* PCI_VENDOR_ID_AVLAB/Intek21 has another bunch of cards ...*/
	{ PCI_VENDOR_ID_AFAVLAB, 0x2110,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, avlab_1s1p },
	{ PCI_VENDOR_ID_AFAVLAB, 0x2111,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, avlab_1s1p },
	{ PCI_VENDOR_ID_AFAVLAB, 0x2112,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, avlab_1s1p },
	{ PCI_VENDOR_ID_AFAVLAB, 0x2140,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, avlab_1s2p },
	{ PCI_VENDOR_ID_AFAVLAB, 0x2141,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, avlab_1s2p },
	{ PCI_VENDOR_ID_AFAVLAB, 0x2142,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, avlab_1s2p },
	{ PCI_VENDOR_ID_AFAVLAB, 0x2160,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, avlab_2s1p },
	{ PCI_VENDOR_ID_AFAVLAB, 0x2161,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, avlab_2s1p },
	{ PCI_VENDOR_ID_AFAVLAB, 0x2162,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, avlab_2s1p },
	{ PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_1S1P_10x_550,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, siig_1s1p_10x },
	{ PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_1S1P_10x_650,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, siig_1s1p_10x },
	{ PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_1S1P_10x_850,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, siig_1s1p_10x },
	{ PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_2S1P_10x_550,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, siig_2s1p_10x },
	{ PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_2S1P_10x_650,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, siig_2s1p_10x },
	{ PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_2S1P_10x_850,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, siig_2s1p_10x },
	{ PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_2P1S_20x_550,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, siig_2p1s_20x },
	{ PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_2P1S_20x_650,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, siig_2p1s_20x },
	{ PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_2P1S_20x_850,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, siig_2p1s_20x },
	{ PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_1S1P_20x_550,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, siig_2s1p_20x },
	{ PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_1S1P_20x_650,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, siig_1s1p_20x },
	{ PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_1S1P_20x_850,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, siig_1s1p_20x },
	{ PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_2S1P_20x_550,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, siig_2s1p_20x },
	{ PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_2S1P_20x_650,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, siig_2s1p_20x },
	{ PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_2S1P_20x_850,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, siig_2s1p_20x },
	/* PCI_VENDOR_ID_TIMEDIA/SUNIX has many differing cards ...*/
	{ 0x1409, 0x7168, 0x1409, 0x4078, 0, 0, timedia_4078a },
	{ 0x1409, 0x7168, 0x1409, 0x4079, 0, 0, timedia_4079h },
	{ 0x1409, 0x7168, 0x1409, 0x4085, 0, 0, timedia_4085h },
	{ 0x1409, 0x7168, 0x1409, 0x4088, 0, 0, timedia_4088a },
	{ 0x1409, 0x7168, 0x1409, 0x4089, 0, 0, timedia_4089a },
	{ 0x1409, 0x7168, 0x1409, 0x4095, 0, 0, timedia_4095a },
	{ 0x1409, 0x7168, 0x1409, 0x4096, 0, 0, timedia_4096a },
	{ 0x1409, 0x7168, 0x1409, 0x5078, 0, 0, timedia_4078u },
	{ 0x1409, 0x7168, 0x1409, 0x5079, 0, 0, timedia_4079a },
	{ 0x1409, 0x7168, 0x1409, 0x5085, 0, 0, timedia_4085u },
	{ 0x1409, 0x7168, 0x1409, 0x6079, 0, 0, timedia_4079r },
	{ 0x1409, 0x7168, 0x1409, 0x7079, 0, 0, timedia_4079s },
	{ 0x1409, 0x7168, 0x1409, 0x8079, 0, 0, timedia_4079d },
	{ 0x1409, 0x7168, 0x1409, 0x9079, 0, 0, timedia_4079e },
	{ 0x1409, 0x7168, 0x1409, 0xa079, 0, 0, timedia_4079f },
	{ 0x1409, 0x7168, 0x1409, 0xb079, 0, 0, timedia_9079a },
	{ 0x1409, 0x7168, 0x1409, 0xc079, 0, 0, timedia_9079b },
	{ 0x1409, 0x7168, 0x1409, 0xd079, 0, 0, timedia_9079c },

	{ 0, } /* terminate list */
};
MODULE_DEVICE_TABLE(pci,parport_serial_pci_tbl);

/*
 * This table describes the serial "geometry" of these boards.  Any
 * quirks for these can be found in drivers/serial/8250_pci.c
 *
 * Cards not tested are marked n/t
 * If you have one of these cards and it works for you, please tell me..
 */
static struct pciserial_board pci_parport_serial_boards[] __devinitdata = {
	[titan_110l] = {
		.flags		= FL_BASE1 | FL_BASE_BARS,
		.num_ports	= 1,
		.base_baud	= 921600,
		.uart_offset	= 8,
	},
	[titan_210l] = {
		.flags		= FL_BASE1 | FL_BASE_BARS,
		.num_ports	= 2,
		.base_baud	= 921600,
		.uart_offset	= 8,
	},
	[netmos_9xx5_combo] = {
		.flags		= FL_BASE0 | FL_BASE_BARS,
		.num_ports	= 1,
		.base_baud	= 115200,
		.uart_offset	= 8,
	},
	[netmos_9855] = {
		.flags		= FL_BASE2 | FL_BASE_BARS,
		.num_ports	= 1,
		.base_baud	= 115200,
		.uart_offset	= 8,
	},
	[netmos_9855_2p] = {
		.flags		= FL_BASE4 | FL_BASE_BARS,
		.num_ports	= 1,
		.base_baud	= 115200,
		.uart_offset	= 8,
	},
	[netmos_9900] = { /* n/t */
		.flags		= FL_BASE0 | FL_BASE_BARS,
		.num_ports	= 1,
		.base_baud	= 115200,
		.uart_offset	= 8,
	},
	[netmos_9900_2p] = { /* parallel only */ /* n/t */
		.flags		= FL_BASE0,
		.num_ports	= 0,
		.base_baud	= 115200,
		.uart_offset	= 8,
	},
	[netmos_99xx_1p] = { /* parallel only */ /* n/t */
		.flags		= FL_BASE0,
		.num_ports	= 0,
		.base_baud	= 115200,
		.uart_offset	= 8,
	},
	[avlab_1s1p] = { /* n/t */
		.flags		= FL_BASE0 | FL_BASE_BARS,
		.num_ports	= 1,
		.base_baud	= 115200,
		.uart_offset	= 8,
	},
	[avlab_1s2p] = { /* n/t */
		.flags		= FL_BASE0 | FL_BASE_BARS,
		.num_ports	= 1,
		.base_baud	= 115200,
		.uart_offset	= 8,
	},
	[avlab_2s1p] = { /* n/t */
		.flags		= FL_BASE0 | FL_BASE_BARS,
		.num_ports	= 2,
		.base_baud	= 115200,
		.uart_offset	= 8,
	},
	[siig_1s1p_10x] = {
		.flags		= FL_BASE2,
		.num_ports	= 1,
		.base_baud	= 460800,
		.uart_offset	= 8,
	},
	[siig_2s1p_10x] = {
		.flags		= FL_BASE2,
		.num_ports	= 1,
		.base_baud	= 921600,
		.uart_offset	= 8,
	},
	[siig_2p1s_20x] = {
		.flags		= FL_BASE0,
		.num_ports	= 1,
		.base_baud	= 921600,
		.uart_offset	= 8,
	},
	[siig_1s1p_20x] = {
		.flags		= FL_BASE0,
		.num_ports	= 1,
		.base_baud	= 921600,
		.uart_offset	= 8,
	},
	[siig_2s1p_20x] = {
		.flags		= FL_BASE0,
		.num_ports	= 1,
		.base_baud	= 921600,
		.uart_offset	= 8,
	},
	[timedia_4078a] = {
		.flags		= FL_BASE0|FL_BASE_BARS,
		.num_ports	= 1,
		.base_baud	= 921600,
		.uart_offset	= 8,
	},
	[timedia_4079h] = {
		.flags		= FL_BASE0|FL_BASE_BARS,
		.num_ports	= 1,
		.base_baud	= 921600,
		.uart_offset	= 8,
	},
	[timedia_4085h] = {
		.flags		= FL_BASE0|FL_BASE_BARS,
		.num_ports	= 1,
		.base_baud	= 921600,
		.uart_offset	= 8,
	},
	[timedia_4088a] = {
		.flags		= FL_BASE0|FL_BASE_BARS,
		.num_ports	= 1,
		.base_baud	= 921600,
		.uart_offset	= 8,
	},
	[timedia_4089a] = {
		.flags		= FL_BASE0|FL_BASE_BARS,
		.num_ports	= 1,
		.base_baud	= 921600,
		.uart_offset	= 8,
	},
	[timedia_4095a] = {
		.flags		= FL_BASE0|FL_BASE_BARS,
		.num_ports	= 1,
		.base_baud	= 921600,
		.uart_offset	= 8,
	},
	[timedia_4096a] = {
		.flags		= FL_BASE0|FL_BASE_BARS,
		.num_ports	= 1,
		.base_baud	= 921600,
		.uart_offset	= 8,
	},
	[timedia_4078u] = {
		.flags		= FL_BASE0|FL_BASE_BARS,
		.num_ports	= 1,
		.base_baud	= 921600,
		.uart_offset	= 8,
	},
	[timedia_4079a] = {
		.flags		= FL_BASE0|FL_BASE_BARS,
		.num_ports	= 1,
		.base_baud	= 921600,
		.uart_offset	= 8,
	},
	[timedia_4085u] = {
		.flags		= FL_BASE0|FL_BASE_BARS,
		.num_ports	= 1,
		.base_baud	= 921600,
		.uart_offset	= 8,
	},
	[timedia_4079r] = {
		.flags		= FL_BASE0|FL_BASE_BARS,
		.num_ports	= 1,
		.base_baud	= 921600,
		.uart_offset	= 8,
	},
	[timedia_4079s] = {
		.flags		= FL_BASE0|FL_BASE_BARS,
		.num_ports	= 1,
		.base_baud	= 921600,
		.uart_offset	= 8,
	},
	[timedia_4079d] = {
		.flags		= FL_BASE0|FL_BASE_BARS,
		.num_ports	= 1,
		.base_baud	= 921600,
		.uart_offset	= 8,
	},
	[timedia_4079e] = {
		.flags		= FL_BASE0|FL_BASE_BARS,
		.num_ports	= 1,
		.base_baud	= 921600,
		.uart_offset	= 8,
	},
	[timedia_4079f] = {
		.flags		= FL_BASE0|FL_BASE_BARS,
		.num_ports	= 1,
		.base_baud	= 921600,
		.uart_offset	= 8,
	},
	[timedia_9079a] = {
		.flags		= FL_BASE0|FL_BASE_BARS,
		.num_ports	= 1,
		.base_baud	= 921600,
		.uart_offset	= 8,
	},
	[timedia_9079b] = {
		.flags		= FL_BASE0|FL_BASE_BARS,
		.num_ports	= 1,
		.base_baud	= 921600,
		.uart_offset	= 8,
	},
	[timedia_9079c] = {
		.flags		= FL_BASE0|FL_BASE_BARS,
		.num_ports	= 1,
		.base_baud	= 921600,
		.uart_offset	= 8,
	},
};

struct parport_serial_private {
	struct serial_private	*serial;
	int num_par;
	struct parport *port[PARPORT_MAX];
	struct parport_pc_pci par;
};

/* Register the serial port(s) of a PCI card. */
static int __devinit serial_register (struct pci_dev *dev,
				      const struct pci_device_id *id)
{
	struct parport_serial_private *priv = pci_get_drvdata (dev);
	struct pciserial_board *board;
	struct serial_private *serial;

	board = &pci_parport_serial_boards[id->driver_data];

	if (board->num_ports == 0)
		return 0;

	serial = pciserial_init_ports(dev, board);

	if (IS_ERR(serial))
		return PTR_ERR(serial);

	priv->serial = serial;
	return 0;
}

/* Register the parallel port(s) of a PCI card. */
static int __devinit parport_register (struct pci_dev *dev,
				       const struct pci_device_id *id)
{
	struct parport_pc_pci *card;
	struct parport_serial_private *priv = pci_get_drvdata (dev);
	int n, success = 0;

	priv->par = cards[id->driver_data];
	card = &priv->par;
	if (card->preinit_hook &&
	    card->preinit_hook (dev, card, PARPORT_IRQ_NONE, PARPORT_DMA_NONE))
		return -ENODEV;

	for (n = 0; n < card->numports; n++) {
		struct parport *port;
		int lo = card->addr[n].lo;
		int hi = card->addr[n].hi;
		unsigned long io_lo, io_hi;
		int irq;

		if (priv->num_par == ARRAY_SIZE (priv->port)) {
			printk (KERN_WARNING
				"parport_serial: %s: only %zu parallel ports "
				"supported (%d reported)\n", pci_name (dev),
				ARRAY_SIZE(priv->port), card->numports);
			break;
		}

		io_lo = pci_resource_start (dev, lo);
		io_hi = 0;
		if ((hi >= 0) && (hi <= 6))
			io_hi = pci_resource_start (dev, hi);
		else if (hi > 6)
			io_lo += hi; /* Reinterpret the meaning of
                                        "hi" as an offset (see SYBA
                                        def.) */
		/* TODO: test if sharing interrupts works */
		irq = dev->irq;
		if (irq == IRQ_NONE) {
			dev_dbg(&dev->dev,
			"PCI parallel port detected: I/O at %#lx(%#lx)\n",
				io_lo, io_hi);
			irq = PARPORT_IRQ_NONE;
		} else {
			dev_dbg(&dev->dev,
		"PCI parallel port detected: I/O at %#lx(%#lx), IRQ %d\n",
				io_lo, io_hi, irq);
		}
		port = parport_pc_probe_port (io_lo, io_hi, irq,
			      PARPORT_DMA_NONE, &dev->dev, IRQF_SHARED);
		if (port) {
			priv->port[priv->num_par++] = port;
			success = 1;
		}
	}

	if (card->postinit_hook)
		card->postinit_hook (dev, card, !success);

	return 0;
}

static int __devinit parport_serial_pci_probe (struct pci_dev *dev,
					       const struct pci_device_id *id)
{
	struct parport_serial_private *priv;
	int err;

	priv = kzalloc (sizeof *priv, GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	pci_set_drvdata (dev, priv);

	err = pci_enable_device (dev);
	if (err) {
		pci_set_drvdata (dev, NULL);
		kfree (priv);
		return err;
	}

	if (parport_register (dev, id)) {
		pci_set_drvdata (dev, NULL);
		kfree (priv);
		return -ENODEV;
	}

	if (serial_register (dev, id)) {
		int i;
		for (i = 0; i < priv->num_par; i++)
			parport_pc_unregister_port (priv->port[i]);
		pci_set_drvdata (dev, NULL);
		kfree (priv);
		return -ENODEV;
	}

	return 0;
}

static void __devexit parport_serial_pci_remove (struct pci_dev *dev)
{
	struct parport_serial_private *priv = pci_get_drvdata (dev);
	int i;

	pci_set_drvdata(dev, NULL);

	// Serial ports
	if (priv->serial)
		pciserial_remove_ports(priv->serial);

	// Parallel ports
	for (i = 0; i < priv->num_par; i++)
		parport_pc_unregister_port (priv->port[i]);

	kfree (priv);
	return;
}

#ifdef CONFIG_PM
static int parport_serial_pci_suspend(struct pci_dev *dev, pm_message_t state)
{
	struct parport_serial_private *priv = pci_get_drvdata(dev);

	if (priv->serial)
		pciserial_suspend_ports(priv->serial);

	/* FIXME: What about parport? */

	pci_save_state(dev);
	pci_set_power_state(dev, pci_choose_state(dev, state));
	return 0;
}

static int parport_serial_pci_resume(struct pci_dev *dev)
{
	struct parport_serial_private *priv = pci_get_drvdata(dev);
	int err;

	pci_set_power_state(dev, PCI_D0);
	pci_restore_state(dev);

	/*
	 * The device may have been disabled.  Re-enable it.
	 */
	err = pci_enable_device(dev);
	if (err) {
		printk(KERN_ERR "parport_serial: %s: error enabling "
			"device for resume (%d)\n", pci_name(dev), err);
		return err;
	}

	if (priv->serial)
		pciserial_resume_ports(priv->serial);

	/* FIXME: What about parport? */

	return 0;
}
#endif

static struct pci_driver parport_serial_pci_driver = {
	.name		= "parport_serial",
	.id_table	= parport_serial_pci_tbl,
	.probe		= parport_serial_pci_probe,
	.remove		= __devexit_p(parport_serial_pci_remove),
#ifdef CONFIG_PM
	.suspend	= parport_serial_pci_suspend,
	.resume		= parport_serial_pci_resume,
#endif
};


static int __init parport_serial_init (void)
{
	return pci_register_driver (&parport_serial_pci_driver);
}

static void __exit parport_serial_exit (void)
{
	pci_unregister_driver (&parport_serial_pci_driver);
	return;
}

MODULE_AUTHOR("Tim Waugh <twaugh@redhat.com>");
MODULE_DESCRIPTION("Driver for common parallel+serial multi-I/O PCI cards");
MODULE_LICENSE("GPL");

module_init(parport_serial_init);
module_exit(parport_serial_exit);
