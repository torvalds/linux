/*
 *  linux/drivers/char/8250_pci.c
 *
 *  Probe module for 8250/16550-type PCI serial ports.
 *
 *  Based on drivers/char/serial.c, by Linus Torvalds, Theodore Ts'o.
 *
 *  Copyright (C) 2001 Russell King, All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 *
 *  $Id: 8250_pci.c,v 1.28 2002/11/02 11:14:18 rmk Exp $
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/tty.h>
#include <linux/serial_core.h>
#include <linux/8250_pci.h>
#include <linux/bitops.h>

#include <asm/byteorder.h>
#include <asm/io.h>

#include "8250.h"

#undef SERIAL_DEBUG_PCI

/*
 * init function returns:
 *  > 0 - number of ports
 *  = 0 - use board->num_ports
 *  < 0 - error
 */
struct pci_serial_quirk {
	u32	vendor;
	u32	device;
	u32	subvendor;
	u32	subdevice;
	int	(*init)(struct pci_dev *dev);
	int	(*setup)(struct serial_private *, struct pciserial_board *,
			 struct uart_port *, int);
	void	(*exit)(struct pci_dev *dev);
};

#define PCI_NUM_BAR_RESOURCES	6

struct serial_private {
	struct pci_dev		*dev;
	unsigned int		nr;
	void __iomem		*remapped_bar[PCI_NUM_BAR_RESOURCES];
	struct pci_serial_quirk	*quirk;
	int			line[0];
};

static void moan_device(const char *str, struct pci_dev *dev)
{
	printk(KERN_WARNING "%s: %s\n"
	       KERN_WARNING "Please send the output of lspci -vv, this\n"
	       KERN_WARNING "message (0x%04x,0x%04x,0x%04x,0x%04x), the\n"
	       KERN_WARNING "manufacturer and name of serial board or\n"
	       KERN_WARNING "modem board to rmk+serial@arm.linux.org.uk.\n",
	       pci_name(dev), str, dev->vendor, dev->device,
	       dev->subsystem_vendor, dev->subsystem_device);
}

static int
setup_port(struct serial_private *priv, struct uart_port *port,
	   int bar, int offset, int regshift)
{
	struct pci_dev *dev = priv->dev;
	unsigned long base, len;

	if (bar >= PCI_NUM_BAR_RESOURCES)
		return -EINVAL;

	base = pci_resource_start(dev, bar);

	if (pci_resource_flags(dev, bar) & IORESOURCE_MEM) {
		len =  pci_resource_len(dev, bar);

		if (!priv->remapped_bar[bar])
			priv->remapped_bar[bar] = ioremap(base, len);
		if (!priv->remapped_bar[bar])
			return -ENOMEM;

		port->iotype = UPIO_MEM;
		port->iobase = 0;
		port->mapbase = base + offset;
		port->membase = priv->remapped_bar[bar] + offset;
		port->regshift = regshift;
	} else {
		port->iotype = UPIO_PORT;
		port->iobase = base + offset;
		port->mapbase = 0;
		port->membase = NULL;
		port->regshift = 0;
	}
	return 0;
}

/*
 * AFAVLAB uses a different mixture of BARs and offsets
 * Not that ugly ;) -- HW
 */
static int
afavlab_setup(struct serial_private *priv, struct pciserial_board *board,
	      struct uart_port *port, int idx)
{
	unsigned int bar, offset = board->first_offset;
	
	bar = FL_GET_BASE(board->flags);
	if (idx < 4)
		bar += idx;
	else {
		bar = 4;
		offset += (idx - 4) * board->uart_offset;
	}

	return setup_port(priv, port, bar, offset, board->reg_shift);
}

/*
 * HP's Remote Management Console.  The Diva chip came in several
 * different versions.  N-class, L2000 and A500 have two Diva chips, each
 * with 3 UARTs (the third UART on the second chip is unused).  Superdome
 * and Keystone have one Diva chip with 3 UARTs.  Some later machines have
 * one Diva chip, but it has been expanded to 5 UARTs.
 */
static int __devinit pci_hp_diva_init(struct pci_dev *dev)
{
	int rc = 0;

	switch (dev->subsystem_device) {
	case PCI_DEVICE_ID_HP_DIVA_TOSCA1:
	case PCI_DEVICE_ID_HP_DIVA_HALFDOME:
	case PCI_DEVICE_ID_HP_DIVA_KEYSTONE:
	case PCI_DEVICE_ID_HP_DIVA_EVEREST:
		rc = 3;
		break;
	case PCI_DEVICE_ID_HP_DIVA_TOSCA2:
		rc = 2;
		break;
	case PCI_DEVICE_ID_HP_DIVA_MAESTRO:
		rc = 4;
		break;
	case PCI_DEVICE_ID_HP_DIVA_POWERBAR:
	case PCI_DEVICE_ID_HP_DIVA_HURRICANE:
		rc = 1;
		break;
	}

	return rc;
}

/*
 * HP's Diva chip puts the 4th/5th serial port further out, and
 * some serial ports are supposed to be hidden on certain models.
 */
static int
pci_hp_diva_setup(struct serial_private *priv, struct pciserial_board *board,
	      struct uart_port *port, int idx)
{
	unsigned int offset = board->first_offset;
	unsigned int bar = FL_GET_BASE(board->flags);

	switch (priv->dev->subsystem_device) {
	case PCI_DEVICE_ID_HP_DIVA_MAESTRO:
		if (idx == 3)
			idx++;
		break;
	case PCI_DEVICE_ID_HP_DIVA_EVEREST:
		if (idx > 0)
			idx++;
		if (idx > 2)
			idx++;
		break;
	}
	if (idx > 2)
		offset = 0x18;

	offset += idx * board->uart_offset;

	return setup_port(priv, port, bar, offset, board->reg_shift);
}

/*
 * Added for EKF Intel i960 serial boards
 */
static int __devinit pci_inteli960ni_init(struct pci_dev *dev)
{
	unsigned long oldval;

	if (!(dev->subsystem_device & 0x1000))
		return -ENODEV;

	/* is firmware started? */
	pci_read_config_dword(dev, 0x44, (void*) &oldval); 
	if (oldval == 0x00001000L) { /* RESET value */ 
		printk(KERN_DEBUG "Local i960 firmware missing");
		return -ENODEV;
	}
	return 0;
}

/*
 * Some PCI serial cards using the PLX 9050 PCI interface chip require
 * that the card interrupt be explicitly enabled or disabled.  This
 * seems to be mainly needed on card using the PLX which also use I/O
 * mapped memory.
 */
static int __devinit pci_plx9050_init(struct pci_dev *dev)
{
	u8 irq_config;
	void __iomem *p;

	if ((pci_resource_flags(dev, 0) & IORESOURCE_MEM) == 0) {
		moan_device("no memory in bar 0", dev);
		return 0;
	}

	irq_config = 0x41;
	if (dev->vendor == PCI_VENDOR_ID_PANACOM ||
	    dev->subsystem_vendor == PCI_SUBVENDOR_ID_EXSYS) {
		irq_config = 0x43;
	}
	if ((dev->vendor == PCI_VENDOR_ID_PLX) &&
	    (dev->device == PCI_DEVICE_ID_PLX_ROMULUS)) {
		/*
		 * As the megawolf cards have the int pins active
		 * high, and have 2 UART chips, both ints must be
		 * enabled on the 9050. Also, the UARTS are set in
		 * 16450 mode by default, so we have to enable the
		 * 16C950 'enhanced' mode so that we can use the
		 * deep FIFOs
		 */
		irq_config = 0x5b;
	}

	/*
	 * enable/disable interrupts
	 */
	p = ioremap(pci_resource_start(dev, 0), 0x80);
	if (p == NULL)
		return -ENOMEM;
	writel(irq_config, p + 0x4c);

	/*
	 * Read the register back to ensure that it took effect.
	 */
	readl(p + 0x4c);
	iounmap(p);

	return 0;
}

static void __devexit pci_plx9050_exit(struct pci_dev *dev)
{
	u8 __iomem *p;

	if ((pci_resource_flags(dev, 0) & IORESOURCE_MEM) == 0)
		return;

	/*
	 * disable interrupts
	 */
	p = ioremap(pci_resource_start(dev, 0), 0x80);
	if (p != NULL) {
		writel(0, p + 0x4c);

		/*
		 * Read the register back to ensure that it took effect.
		 */
		readl(p + 0x4c);
		iounmap(p);
	}
}

/* SBS Technologies Inc. PMC-OCTPRO and P-OCTAL cards */
static int
sbs_setup(struct serial_private *priv, struct pciserial_board *board,
		struct uart_port *port, int idx)
{
	unsigned int bar, offset = board->first_offset;

	bar = 0;

	if (idx < 4) {
		/* first four channels map to 0, 0x100, 0x200, 0x300 */
		offset += idx * board->uart_offset;
	} else if (idx < 8) {
		/* last four channels map to 0x1000, 0x1100, 0x1200, 0x1300 */
		offset += idx * board->uart_offset + 0xC00;
	} else /* we have only 8 ports on PMC-OCTALPRO */
		return 1;

	return setup_port(priv, port, bar, offset, board->reg_shift);
}

/*
* This does initialization for PMC OCTALPRO cards:
* maps the device memory, resets the UARTs (needed, bc
* if the module is removed and inserted again, the card
* is in the sleep mode) and enables global interrupt.
*/

/* global control register offset for SBS PMC-OctalPro */
#define OCT_REG_CR_OFF		0x500

static int __devinit sbs_init(struct pci_dev *dev)
{
	u8 __iomem *p;

	p = ioremap(pci_resource_start(dev, 0),pci_resource_len(dev,0));

	if (p == NULL)
		return -ENOMEM;
	/* Set bit-4 Control Register (UART RESET) in to reset the uarts */
	writeb(0x10,p + OCT_REG_CR_OFF);
	udelay(50);
	writeb(0x0,p + OCT_REG_CR_OFF);

	/* Set bit-2 (INTENABLE) of Control Register */
	writeb(0x4, p + OCT_REG_CR_OFF);
	iounmap(p);

	return 0;
}

/*
 * Disables the global interrupt of PMC-OctalPro
 */

static void __devexit sbs_exit(struct pci_dev *dev)
{
	u8 __iomem *p;

	p = ioremap(pci_resource_start(dev, 0),pci_resource_len(dev,0));
	if (p != NULL) {
		writeb(0, p + OCT_REG_CR_OFF);
	}
	iounmap(p);
}

/*
 * SIIG serial cards have an PCI interface chip which also controls
 * the UART clocking frequency. Each UART can be clocked independently
 * (except cards equiped with 4 UARTs) and initial clocking settings
 * are stored in the EEPROM chip. It can cause problems because this
 * version of serial driver doesn't support differently clocked UART's
 * on single PCI card. To prevent this, initialization functions set
 * high frequency clocking for all UART's on given card. It is safe (I
 * hope) because it doesn't touch EEPROM settings to prevent conflicts
 * with other OSes (like M$ DOS).
 *
 *  SIIG support added by Andrey Panin <pazke@donpac.ru>, 10/1999
 * 
 * There is two family of SIIG serial cards with different PCI
 * interface chip and different configuration methods:
 *     - 10x cards have control registers in IO and/or memory space;
 *     - 20x cards have control registers in standard PCI configuration space.
 *
 * Note: all 10x cards have PCI device ids 0x10..
 *       all 20x cards have PCI device ids 0x20..
 *
 * There are also Quartet Serial cards which use Oxford Semiconductor
 * 16954 quad UART PCI chip clocked by 18.432 MHz quartz.
 *
 * Note: some SIIG cards are probed by the parport_serial object.
 */

#define PCI_DEVICE_ID_SIIG_1S_10x (PCI_DEVICE_ID_SIIG_1S_10x_550 & 0xfffc)
#define PCI_DEVICE_ID_SIIG_2S_10x (PCI_DEVICE_ID_SIIG_2S_10x_550 & 0xfff8)

static int pci_siig10x_init(struct pci_dev *dev)
{
	u16 data;
	void __iomem *p;

	switch (dev->device & 0xfff8) {
	case PCI_DEVICE_ID_SIIG_1S_10x:	/* 1S */
		data = 0xffdf;
		break;
	case PCI_DEVICE_ID_SIIG_2S_10x:	/* 2S, 2S1P */
		data = 0xf7ff;
		break;
	default:			/* 1S1P, 4S */
		data = 0xfffb;
		break;
	}

	p = ioremap(pci_resource_start(dev, 0), 0x80);
	if (p == NULL)
		return -ENOMEM;

	writew(readw(p + 0x28) & data, p + 0x28);
	readw(p + 0x28);
	iounmap(p);
	return 0;
}

#define PCI_DEVICE_ID_SIIG_2S_20x (PCI_DEVICE_ID_SIIG_2S_20x_550 & 0xfffc)
#define PCI_DEVICE_ID_SIIG_2S1P_20x (PCI_DEVICE_ID_SIIG_2S1P_20x_550 & 0xfffc)

static int pci_siig20x_init(struct pci_dev *dev)
{
	u8 data;

	/* Change clock frequency for the first UART. */
	pci_read_config_byte(dev, 0x6f, &data);
	pci_write_config_byte(dev, 0x6f, data & 0xef);

	/* If this card has 2 UART, we have to do the same with second UART. */
	if (((dev->device & 0xfffc) == PCI_DEVICE_ID_SIIG_2S_20x) ||
	    ((dev->device & 0xfffc) == PCI_DEVICE_ID_SIIG_2S1P_20x)) {
		pci_read_config_byte(dev, 0x73, &data);
		pci_write_config_byte(dev, 0x73, data & 0xef);
	}
	return 0;
}

static int pci_siig_init(struct pci_dev *dev)
{
	unsigned int type = dev->device & 0xff00;

	if (type == 0x1000)
		return pci_siig10x_init(dev);
	else if (type == 0x2000)
		return pci_siig20x_init(dev);

	moan_device("Unknown SIIG card", dev);
	return -ENODEV;
}

/*
 * Timedia has an explosion of boards, and to avoid the PCI table from
 * growing *huge*, we use this function to collapse some 70 entries
 * in the PCI table into one, for sanity's and compactness's sake.
 */
static unsigned short timedia_single_port[] = {
	0x4025, 0x4027, 0x4028, 0x5025, 0x5027, 0
};

static unsigned short timedia_dual_port[] = {
	0x0002, 0x4036, 0x4037, 0x4038, 0x4078, 0x4079, 0x4085,
	0x4088, 0x4089, 0x5037, 0x5078, 0x5079, 0x5085, 0x6079, 
	0x7079, 0x8079, 0x8137, 0x8138, 0x8237, 0x8238, 0x9079, 
	0x9137, 0x9138, 0x9237, 0x9238, 0xA079, 0xB079, 0xC079,
	0xD079, 0
};

static unsigned short timedia_quad_port[] = {
	0x4055, 0x4056, 0x4095, 0x4096, 0x5056, 0x8156, 0x8157, 
	0x8256, 0x8257, 0x9056, 0x9156, 0x9157, 0x9158, 0x9159, 
	0x9256, 0x9257, 0xA056, 0xA157, 0xA158, 0xA159, 0xB056,
	0xB157, 0
};

static unsigned short timedia_eight_port[] = {
	0x4065, 0x4066, 0x5065, 0x5066, 0x8166, 0x9066, 0x9166, 
	0x9167, 0x9168, 0xA066, 0xA167, 0xA168, 0
};

static const struct timedia_struct {
	int num;
	unsigned short *ids;
} timedia_data[] = {
	{ 1, timedia_single_port },
	{ 2, timedia_dual_port },
	{ 4, timedia_quad_port },
	{ 8, timedia_eight_port },
	{ 0, NULL }
};

static int __devinit pci_timedia_init(struct pci_dev *dev)
{
	unsigned short *ids;
	int i, j;

	for (i = 0; timedia_data[i].num; i++) {
		ids = timedia_data[i].ids;
		for (j = 0; ids[j]; j++)
			if (dev->subsystem_device == ids[j])
				return timedia_data[i].num;
	}
	return 0;
}

/*
 * Timedia/SUNIX uses a mixture of BARs and offsets
 * Ugh, this is ugly as all hell --- TYT
 */
static int
pci_timedia_setup(struct serial_private *priv, struct pciserial_board *board,
		  struct uart_port *port, int idx)
{
	unsigned int bar = 0, offset = board->first_offset;

	switch (idx) {
	case 0:
		bar = 0;
		break;
	case 1:
		offset = board->uart_offset;
		bar = 0;
		break;
	case 2:
		bar = 1;
		break;
	case 3:
		offset = board->uart_offset;
		bar = 1;
	case 4: /* BAR 2 */
	case 5: /* BAR 3 */
	case 6: /* BAR 4 */
	case 7: /* BAR 5 */
		bar = idx - 2;
	}

	return setup_port(priv, port, bar, offset, board->reg_shift);
}

/*
 * Some Titan cards are also a little weird
 */
static int
titan_400l_800l_setup(struct serial_private *priv,
		      struct pciserial_board *board,
		      struct uart_port *port, int idx)
{
	unsigned int bar, offset = board->first_offset;

	switch (idx) {
	case 0:
		bar = 1;
		break;
	case 1:
		bar = 2;
		break;
	default:
		bar = 4;
		offset = (idx - 2) * board->uart_offset;
	}

	return setup_port(priv, port, bar, offset, board->reg_shift);
}

static int __devinit pci_xircom_init(struct pci_dev *dev)
{
	msleep(100);
	return 0;
}

static int __devinit pci_netmos_init(struct pci_dev *dev)
{
	/* subdevice 0x00PS means <P> parallel, <S> serial */
	unsigned int num_serial = dev->subsystem_device & 0xf;

	if (num_serial == 0)
		return -ENODEV;
	return num_serial;
}

static int
pci_default_setup(struct serial_private *priv, struct pciserial_board *board,
		  struct uart_port *port, int idx)
{
	unsigned int bar, offset = board->first_offset, maxnr;

	bar = FL_GET_BASE(board->flags);
	if (board->flags & FL_BASE_BARS)
		bar += idx;
	else
		offset += idx * board->uart_offset;

	maxnr = (pci_resource_len(priv->dev, bar) - board->first_offset) /
		(8 << board->reg_shift);

	if (board->flags & FL_REGION_SZ_CAP && idx >= maxnr)
		return 1;
			
	return setup_port(priv, port, bar, offset, board->reg_shift);
}

/* This should be in linux/pci_ids.h */
#define PCI_VENDOR_ID_SBSMODULARIO	0x124B
#define PCI_SUBVENDOR_ID_SBSMODULARIO	0x124B
#define PCI_DEVICE_ID_OCTPRO		0x0001
#define PCI_SUBDEVICE_ID_OCTPRO232	0x0108
#define PCI_SUBDEVICE_ID_OCTPRO422	0x0208
#define PCI_SUBDEVICE_ID_POCTAL232	0x0308
#define PCI_SUBDEVICE_ID_POCTAL422	0x0408

/*
 * Master list of serial port init/setup/exit quirks.
 * This does not describe the general nature of the port.
 * (ie, baud base, number and location of ports, etc)
 *
 * This list is ordered alphabetically by vendor then device.
 * Specific entries must come before more generic entries.
 */
static struct pci_serial_quirk pci_serial_quirks[] = {
	/*
	 * AFAVLAB cards.
	 *  It is not clear whether this applies to all products.
	 */
	{
		.vendor		= PCI_VENDOR_ID_AFAVLAB,
		.device		= PCI_ANY_ID,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.setup		= afavlab_setup,
	},
	/*
	 * HP Diva
	 */
	{
		.vendor		= PCI_VENDOR_ID_HP,
		.device		= PCI_DEVICE_ID_HP_DIVA,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.init		= pci_hp_diva_init,
		.setup		= pci_hp_diva_setup,
	},
	/*
	 * Intel
	 */
	{
		.vendor		= PCI_VENDOR_ID_INTEL,
		.device		= PCI_DEVICE_ID_INTEL_80960_RP,
		.subvendor	= 0xe4bf,
		.subdevice	= PCI_ANY_ID,
		.init		= pci_inteli960ni_init,
		.setup		= pci_default_setup,
	},
	/*
	 * Panacom
	 */
	{
		.vendor		= PCI_VENDOR_ID_PANACOM,
		.device		= PCI_DEVICE_ID_PANACOM_QUADMODEM,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.init		= pci_plx9050_init,
		.setup		= pci_default_setup,
		.exit		= __devexit_p(pci_plx9050_exit),
	},		
	{
		.vendor		= PCI_VENDOR_ID_PANACOM,
		.device		= PCI_DEVICE_ID_PANACOM_DUALMODEM,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.init		= pci_plx9050_init,
		.setup		= pci_default_setup,
		.exit		= __devexit_p(pci_plx9050_exit),
	},
	/*
	 * PLX
	 */
	{
		.vendor		= PCI_VENDOR_ID_PLX,
		.device		= PCI_DEVICE_ID_PLX_9050,
		.subvendor	= PCI_SUBVENDOR_ID_EXSYS,
		.subdevice	= PCI_SUBDEVICE_ID_EXSYS_4055,
		.init		= pci_plx9050_init,
		.setup		= pci_default_setup,
		.exit		= __devexit_p(pci_plx9050_exit),
	},
	{
		.vendor		= PCI_VENDOR_ID_PLX,
		.device		= PCI_DEVICE_ID_PLX_9050,
		.subvendor	= PCI_SUBVENDOR_ID_KEYSPAN,
		.subdevice	= PCI_SUBDEVICE_ID_KEYSPAN_SX2,
		.init		= pci_plx9050_init,
		.setup		= pci_default_setup,
		.exit		= __devexit_p(pci_plx9050_exit),
	},
	{
		.vendor		= PCI_VENDOR_ID_PLX,
		.device		= PCI_DEVICE_ID_PLX_ROMULUS,
		.subvendor	= PCI_VENDOR_ID_PLX,
		.subdevice	= PCI_DEVICE_ID_PLX_ROMULUS,
		.init		= pci_plx9050_init,
		.setup		= pci_default_setup,
		.exit		= __devexit_p(pci_plx9050_exit),
	},
	/*
	 * SBS Technologies, Inc., PMC-OCTALPRO 232
	 */
	{
		.vendor		= PCI_VENDOR_ID_SBSMODULARIO,
		.device		= PCI_DEVICE_ID_OCTPRO,
		.subvendor	= PCI_SUBVENDOR_ID_SBSMODULARIO,
		.subdevice	= PCI_SUBDEVICE_ID_OCTPRO232,
		.init		= sbs_init,
		.setup		= sbs_setup,
		.exit		= __devexit_p(sbs_exit),
	},
	/*
	 * SBS Technologies, Inc., PMC-OCTALPRO 422
	 */
	{
		.vendor		= PCI_VENDOR_ID_SBSMODULARIO,
		.device		= PCI_DEVICE_ID_OCTPRO,
		.subvendor	= PCI_SUBVENDOR_ID_SBSMODULARIO,
		.subdevice	= PCI_SUBDEVICE_ID_OCTPRO422,
		.init		= sbs_init,
		.setup		= sbs_setup,
		.exit		= __devexit_p(sbs_exit),
	},
	/*
	 * SBS Technologies, Inc., P-Octal 232
	 */
	{
		.vendor		= PCI_VENDOR_ID_SBSMODULARIO,
		.device		= PCI_DEVICE_ID_OCTPRO,
		.subvendor	= PCI_SUBVENDOR_ID_SBSMODULARIO,
		.subdevice	= PCI_SUBDEVICE_ID_POCTAL232,
		.init		= sbs_init,
		.setup		= sbs_setup,
		.exit		= __devexit_p(sbs_exit),
	},
	/*
	 * SBS Technologies, Inc., P-Octal 422
	 */
	{
		.vendor		= PCI_VENDOR_ID_SBSMODULARIO,
		.device		= PCI_DEVICE_ID_OCTPRO,
		.subvendor	= PCI_SUBVENDOR_ID_SBSMODULARIO,
		.subdevice	= PCI_SUBDEVICE_ID_POCTAL422,
		.init		= sbs_init,
		.setup		= sbs_setup,
		.exit		= __devexit_p(sbs_exit),
	},
	/*
	 * SIIG cards.
	 */
	{
		.vendor		= PCI_VENDOR_ID_SIIG,
		.device		= PCI_ANY_ID,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.init		= pci_siig_init,
		.setup		= pci_default_setup,
	},
	/*
	 * Titan cards
	 */
	{
		.vendor		= PCI_VENDOR_ID_TITAN,
		.device		= PCI_DEVICE_ID_TITAN_400L,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.setup		= titan_400l_800l_setup,
	},
	{
		.vendor		= PCI_VENDOR_ID_TITAN,
		.device		= PCI_DEVICE_ID_TITAN_800L,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.setup		= titan_400l_800l_setup,
	},
	/*
	 * Timedia cards
	 */
	{
		.vendor		= PCI_VENDOR_ID_TIMEDIA,
		.device		= PCI_DEVICE_ID_TIMEDIA_1889,
		.subvendor	= PCI_VENDOR_ID_TIMEDIA,
		.subdevice	= PCI_ANY_ID,
		.init		= pci_timedia_init,
		.setup		= pci_timedia_setup,
	},
	{
		.vendor		= PCI_VENDOR_ID_TIMEDIA,
		.device		= PCI_ANY_ID,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.setup		= pci_timedia_setup,
	},
	/*
	 * Xircom cards
	 */
	{
		.vendor		= PCI_VENDOR_ID_XIRCOM,
		.device		= PCI_DEVICE_ID_XIRCOM_X3201_MDM,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.init		= pci_xircom_init,
		.setup		= pci_default_setup,
	},
	/*
	 * Netmos cards
	 */
	{
		.vendor		= PCI_VENDOR_ID_NETMOS,
		.device		= PCI_ANY_ID,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.init		= pci_netmos_init,
		.setup		= pci_default_setup,
	},
	/*
	 * Default "match everything" terminator entry
	 */
	{
		.vendor		= PCI_ANY_ID,
		.device		= PCI_ANY_ID,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.setup		= pci_default_setup,
	}
};

static inline int quirk_id_matches(u32 quirk_id, u32 dev_id)
{
	return quirk_id == PCI_ANY_ID || quirk_id == dev_id;
}

static struct pci_serial_quirk *find_quirk(struct pci_dev *dev)
{
	struct pci_serial_quirk *quirk;

	for (quirk = pci_serial_quirks; ; quirk++)
		if (quirk_id_matches(quirk->vendor, dev->vendor) &&
		    quirk_id_matches(quirk->device, dev->device) &&
		    quirk_id_matches(quirk->subvendor, dev->subsystem_vendor) &&
		    quirk_id_matches(quirk->subdevice, dev->subsystem_device))
		 	break;
	return quirk;
}

static _INLINE_ int
get_pci_irq(struct pci_dev *dev, struct pciserial_board *board)
{
	if (board->flags & FL_NOIRQ)
		return 0;
	else
		return dev->irq;
}

/*
 * This is the configuration table for all of the PCI serial boards
 * which we support.  It is directly indexed by the pci_board_num_t enum
 * value, which is encoded in the pci_device_id PCI probe table's
 * driver_data member.
 *
 * The makeup of these names are:
 *  pbn_bn{_bt}_n_baud
 *
 *  bn   = PCI BAR number
 *  bt   = Index using PCI BARs
 *  n    = number of serial ports
 *  baud = baud rate
 *
 * This table is sorted by (in order): baud, bt, bn, n.
 *
 * Please note: in theory if n = 1, _bt infix should make no difference.
 * ie, pbn_b0_1_115200 is the same as pbn_b0_bt_1_115200
 */
enum pci_board_num_t {
	pbn_default = 0,

	pbn_b0_1_115200,
	pbn_b0_2_115200,
	pbn_b0_4_115200,
	pbn_b0_5_115200,

	pbn_b0_1_921600,
	pbn_b0_2_921600,
	pbn_b0_4_921600,

	pbn_b0_2_1130000,

	pbn_b0_4_1152000,

	pbn_b0_bt_1_115200,
	pbn_b0_bt_2_115200,
	pbn_b0_bt_8_115200,

	pbn_b0_bt_1_460800,
	pbn_b0_bt_2_460800,
	pbn_b0_bt_4_460800,

	pbn_b0_bt_1_921600,
	pbn_b0_bt_2_921600,
	pbn_b0_bt_4_921600,
	pbn_b0_bt_8_921600,

	pbn_b1_1_115200,
	pbn_b1_2_115200,
	pbn_b1_4_115200,
	pbn_b1_8_115200,

	pbn_b1_1_921600,
	pbn_b1_2_921600,
	pbn_b1_4_921600,
	pbn_b1_8_921600,

	pbn_b1_bt_2_921600,

	pbn_b1_1_1382400,
	pbn_b1_2_1382400,
	pbn_b1_4_1382400,
	pbn_b1_8_1382400,

	pbn_b2_1_115200,
	pbn_b2_8_115200,

	pbn_b2_1_460800,
	pbn_b2_4_460800,
	pbn_b2_8_460800,
	pbn_b2_16_460800,

	pbn_b2_1_921600,
	pbn_b2_4_921600,
	pbn_b2_8_921600,

	pbn_b2_bt_1_115200,
	pbn_b2_bt_2_115200,
	pbn_b2_bt_4_115200,

	pbn_b2_bt_2_921600,
	pbn_b2_bt_4_921600,

	pbn_b3_4_115200,
	pbn_b3_8_115200,

	/*
	 * Board-specific versions.
	 */
	pbn_panacom,
	pbn_panacom2,
	pbn_panacom4,
	pbn_exsys_4055,
	pbn_plx_romulus,
	pbn_oxsemi,
	pbn_intel_i960,
	pbn_sgi_ioc3,
	pbn_nec_nile4,
	pbn_computone_4,
	pbn_computone_6,
	pbn_computone_8,
	pbn_sbsxrsio,
	pbn_exar_XR17C152,
	pbn_exar_XR17C154,
	pbn_exar_XR17C158,
};

/*
 * uart_offset - the space between channels
 * reg_shift   - describes how the UART registers are mapped
 *               to PCI memory by the card.
 * For example IER register on SBS, Inc. PMC-OctPro is located at
 * offset 0x10 from the UART base, while UART_IER is defined as 1
 * in include/linux/serial_reg.h,
 * see first lines of serial_in() and serial_out() in 8250.c
*/

static struct pciserial_board pci_boards[] __devinitdata = {
	[pbn_default] = {
		.flags		= FL_BASE0,
		.num_ports	= 1,
		.base_baud	= 115200,
		.uart_offset	= 8,
	},
	[pbn_b0_1_115200] = {
		.flags		= FL_BASE0,
		.num_ports	= 1,
		.base_baud	= 115200,
		.uart_offset	= 8,
	},
	[pbn_b0_2_115200] = {
		.flags		= FL_BASE0,
		.num_ports	= 2,
		.base_baud	= 115200,
		.uart_offset	= 8,
	},
	[pbn_b0_4_115200] = {
		.flags		= FL_BASE0,
		.num_ports	= 4,
		.base_baud	= 115200,
		.uart_offset	= 8,
	},
	[pbn_b0_5_115200] = {
		.flags		= FL_BASE0,
		.num_ports	= 5,
		.base_baud	= 115200,
		.uart_offset	= 8,
	},

	[pbn_b0_1_921600] = {
		.flags		= FL_BASE0,
		.num_ports	= 1,
		.base_baud	= 921600,
		.uart_offset	= 8,
	},
	[pbn_b0_2_921600] = {
		.flags		= FL_BASE0,
		.num_ports	= 2,
		.base_baud	= 921600,
		.uart_offset	= 8,
	},
	[pbn_b0_4_921600] = {
		.flags		= FL_BASE0,
		.num_ports	= 4,
		.base_baud	= 921600,
		.uart_offset	= 8,
	},

	[pbn_b0_2_1130000] = {
		.flags          = FL_BASE0,
		.num_ports      = 2,
		.base_baud      = 1130000,
		.uart_offset    = 8,
	},

	[pbn_b0_4_1152000] = {
		.flags		= FL_BASE0,
		.num_ports	= 4,
		.base_baud	= 1152000,
		.uart_offset	= 8,
	},

	[pbn_b0_bt_1_115200] = {
		.flags		= FL_BASE0|FL_BASE_BARS,
		.num_ports	= 1,
		.base_baud	= 115200,
		.uart_offset	= 8,
	},
	[pbn_b0_bt_2_115200] = {
		.flags		= FL_BASE0|FL_BASE_BARS,
		.num_ports	= 2,
		.base_baud	= 115200,
		.uart_offset	= 8,
	},
	[pbn_b0_bt_8_115200] = {
		.flags		= FL_BASE0|FL_BASE_BARS,
		.num_ports	= 8,
		.base_baud	= 115200,
		.uart_offset	= 8,
	},

	[pbn_b0_bt_1_460800] = {
		.flags		= FL_BASE0|FL_BASE_BARS,
		.num_ports	= 1,
		.base_baud	= 460800,
		.uart_offset	= 8,
	},
	[pbn_b0_bt_2_460800] = {
		.flags		= FL_BASE0|FL_BASE_BARS,
		.num_ports	= 2,
		.base_baud	= 460800,
		.uart_offset	= 8,
	},
	[pbn_b0_bt_4_460800] = {
		.flags		= FL_BASE0|FL_BASE_BARS,
		.num_ports	= 4,
		.base_baud	= 460800,
		.uart_offset	= 8,
	},

	[pbn_b0_bt_1_921600] = {
		.flags		= FL_BASE0|FL_BASE_BARS,
		.num_ports	= 1,
		.base_baud	= 921600,
		.uart_offset	= 8,
	},
	[pbn_b0_bt_2_921600] = {
		.flags		= FL_BASE0|FL_BASE_BARS,
		.num_ports	= 2,
		.base_baud	= 921600,
		.uart_offset	= 8,
	},
	[pbn_b0_bt_4_921600] = {
		.flags		= FL_BASE0|FL_BASE_BARS,
		.num_ports	= 4,
		.base_baud	= 921600,
		.uart_offset	= 8,
	},
	[pbn_b0_bt_8_921600] = {
		.flags		= FL_BASE0|FL_BASE_BARS,
		.num_ports	= 8,
		.base_baud	= 921600,
		.uart_offset	= 8,
	},

	[pbn_b1_1_115200] = {
		.flags		= FL_BASE1,
		.num_ports	= 1,
		.base_baud	= 115200,
		.uart_offset	= 8,
	},
	[pbn_b1_2_115200] = {
		.flags		= FL_BASE1,
		.num_ports	= 2,
		.base_baud	= 115200,
		.uart_offset	= 8,
	},
	[pbn_b1_4_115200] = {
		.flags		= FL_BASE1,
		.num_ports	= 4,
		.base_baud	= 115200,
		.uart_offset	= 8,
	},
	[pbn_b1_8_115200] = {
		.flags		= FL_BASE1,
		.num_ports	= 8,
		.base_baud	= 115200,
		.uart_offset	= 8,
	},

	[pbn_b1_1_921600] = {
		.flags		= FL_BASE1,
		.num_ports	= 1,
		.base_baud	= 921600,
		.uart_offset	= 8,
	},
	[pbn_b1_2_921600] = {
		.flags		= FL_BASE1,
		.num_ports	= 2,
		.base_baud	= 921600,
		.uart_offset	= 8,
	},
	[pbn_b1_4_921600] = {
		.flags		= FL_BASE1,
		.num_ports	= 4,
		.base_baud	= 921600,
		.uart_offset	= 8,
	},
	[pbn_b1_8_921600] = {
		.flags		= FL_BASE1,
		.num_ports	= 8,
		.base_baud	= 921600,
		.uart_offset	= 8,
	},

	[pbn_b1_bt_2_921600] = {
		.flags		= FL_BASE1|FL_BASE_BARS,
		.num_ports	= 2,
		.base_baud	= 921600,
		.uart_offset	= 8,
	},

	[pbn_b1_1_1382400] = {
		.flags		= FL_BASE1,
		.num_ports	= 1,
		.base_baud	= 1382400,
		.uart_offset	= 8,
	},
	[pbn_b1_2_1382400] = {
		.flags		= FL_BASE1,
		.num_ports	= 2,
		.base_baud	= 1382400,
		.uart_offset	= 8,
	},
	[pbn_b1_4_1382400] = {
		.flags		= FL_BASE1,
		.num_ports	= 4,
		.base_baud	= 1382400,
		.uart_offset	= 8,
	},
	[pbn_b1_8_1382400] = {
		.flags		= FL_BASE1,
		.num_ports	= 8,
		.base_baud	= 1382400,
		.uart_offset	= 8,
	},

	[pbn_b2_1_115200] = {
		.flags		= FL_BASE2,
		.num_ports	= 1,
		.base_baud	= 115200,
		.uart_offset	= 8,
	},
	[pbn_b2_8_115200] = {
		.flags		= FL_BASE2,
		.num_ports	= 8,
		.base_baud	= 115200,
		.uart_offset	= 8,
	},

	[pbn_b2_1_460800] = {
		.flags		= FL_BASE2,
		.num_ports	= 1,
		.base_baud	= 460800,
		.uart_offset	= 8,
	},
	[pbn_b2_4_460800] = {
		.flags		= FL_BASE2,
		.num_ports	= 4,
		.base_baud	= 460800,
		.uart_offset	= 8,
	},
	[pbn_b2_8_460800] = {
		.flags		= FL_BASE2,
		.num_ports	= 8,
		.base_baud	= 460800,
		.uart_offset	= 8,
	},
	[pbn_b2_16_460800] = {
		.flags		= FL_BASE2,
		.num_ports	= 16,
		.base_baud	= 460800,
		.uart_offset	= 8,
	 },

	[pbn_b2_1_921600] = {
		.flags		= FL_BASE2,
		.num_ports	= 1,
		.base_baud	= 921600,
		.uart_offset	= 8,
	},
	[pbn_b2_4_921600] = {
		.flags		= FL_BASE2,
		.num_ports	= 4,
		.base_baud	= 921600,
		.uart_offset	= 8,
	},
	[pbn_b2_8_921600] = {
		.flags		= FL_BASE2,
		.num_ports	= 8,
		.base_baud	= 921600,
		.uart_offset	= 8,
	},

	[pbn_b2_bt_1_115200] = {
		.flags		= FL_BASE2|FL_BASE_BARS,
		.num_ports	= 1,
		.base_baud	= 115200,
		.uart_offset	= 8,
	},
	[pbn_b2_bt_2_115200] = {
		.flags		= FL_BASE2|FL_BASE_BARS,
		.num_ports	= 2,
		.base_baud	= 115200,
		.uart_offset	= 8,
	},
	[pbn_b2_bt_4_115200] = {
		.flags		= FL_BASE2|FL_BASE_BARS,
		.num_ports	= 4,
		.base_baud	= 115200,
		.uart_offset	= 8,
	},

	[pbn_b2_bt_2_921600] = {
		.flags		= FL_BASE2|FL_BASE_BARS,
		.num_ports	= 2,
		.base_baud	= 921600,
		.uart_offset	= 8,
	},
	[pbn_b2_bt_4_921600] = {
		.flags		= FL_BASE2|FL_BASE_BARS,
		.num_ports	= 4,
		.base_baud	= 921600,
		.uart_offset	= 8,
	},

	[pbn_b3_4_115200] = {
		.flags		= FL_BASE3,
		.num_ports	= 4,
		.base_baud	= 115200,
		.uart_offset	= 8,
	},
	[pbn_b3_8_115200] = {
		.flags		= FL_BASE3,
		.num_ports	= 8,
		.base_baud	= 115200,
		.uart_offset	= 8,
	},

	/*
	 * Entries following this are board-specific.
	 */

	/*
	 * Panacom - IOMEM
	 */
	[pbn_panacom] = {
		.flags		= FL_BASE2,
		.num_ports	= 2,
		.base_baud	= 921600,
		.uart_offset	= 0x400,
		.reg_shift	= 7,
	},
	[pbn_panacom2] = {
		.flags		= FL_BASE2|FL_BASE_BARS,
		.num_ports	= 2,
		.base_baud	= 921600,
		.uart_offset	= 0x400,
		.reg_shift	= 7,
	},
	[pbn_panacom4] = {
		.flags		= FL_BASE2|FL_BASE_BARS,
		.num_ports	= 4,
		.base_baud	= 921600,
		.uart_offset	= 0x400,
		.reg_shift	= 7,
	},

	[pbn_exsys_4055] = {
		.flags		= FL_BASE2,
		.num_ports	= 4,
		.base_baud	= 115200,
		.uart_offset	= 8,
	},

	/* I think this entry is broken - the first_offset looks wrong --rmk */
	[pbn_plx_romulus] = {
		.flags		= FL_BASE2,
		.num_ports	= 4,
		.base_baud	= 921600,
		.uart_offset	= 8 << 2,
		.reg_shift	= 2,
		.first_offset	= 0x03,
	},

	/*
	 * This board uses the size of PCI Base region 0 to
	 * signal now many ports are available
	 */
	[pbn_oxsemi] = {
		.flags		= FL_BASE0|FL_REGION_SZ_CAP,
		.num_ports	= 32,
		.base_baud	= 115200,
		.uart_offset	= 8,
	},

	/*
	 * EKF addition for i960 Boards form EKF with serial port.
	 * Max 256 ports.
	 */
	[pbn_intel_i960] = {
		.flags		= FL_BASE0,
		.num_ports	= 32,
		.base_baud	= 921600,
		.uart_offset	= 8 << 2,
		.reg_shift	= 2,
		.first_offset	= 0x10000,
	},
	[pbn_sgi_ioc3] = {
		.flags		= FL_BASE0|FL_NOIRQ,
		.num_ports	= 1,
		.base_baud	= 458333,
		.uart_offset	= 8,
		.reg_shift	= 0,
		.first_offset	= 0x20178,
	},

	/*
	 * NEC Vrc-5074 (Nile 4) builtin UART.
	 */
	[pbn_nec_nile4] = {
		.flags		= FL_BASE0,
		.num_ports	= 1,
		.base_baud	= 520833,
		.uart_offset	= 8 << 3,
		.reg_shift	= 3,
		.first_offset	= 0x300,
	},

	/*
	 * Computone - uses IOMEM.
	 */
	[pbn_computone_4] = {
		.flags		= FL_BASE0,
		.num_ports	= 4,
		.base_baud	= 921600,
		.uart_offset	= 0x40,
		.reg_shift	= 2,
		.first_offset	= 0x200,
	},
	[pbn_computone_6] = {
		.flags		= FL_BASE0,
		.num_ports	= 6,
		.base_baud	= 921600,
		.uart_offset	= 0x40,
		.reg_shift	= 2,
		.first_offset	= 0x200,
	},
	[pbn_computone_8] = {
		.flags		= FL_BASE0,
		.num_ports	= 8,
		.base_baud	= 921600,
		.uart_offset	= 0x40,
		.reg_shift	= 2,
		.first_offset	= 0x200,
	},
	[pbn_sbsxrsio] = {
		.flags		= FL_BASE0,
		.num_ports	= 8,
		.base_baud	= 460800,
		.uart_offset	= 256,
		.reg_shift	= 4,
	},
	/*
	 * Exar Corp. XR17C15[248] Dual/Quad/Octal UART
	 *  Only basic 16550A support.
	 *  XR17C15[24] are not tested, but they should work.
	 */
	[pbn_exar_XR17C152] = {
		.flags		= FL_BASE0,
		.num_ports	= 2,
		.base_baud	= 921600,
		.uart_offset	= 0x200,
	},
	[pbn_exar_XR17C154] = {
		.flags		= FL_BASE0,
		.num_ports	= 4,
		.base_baud	= 921600,
		.uart_offset	= 0x200,
	},
	[pbn_exar_XR17C158] = {
		.flags		= FL_BASE0,
		.num_ports	= 8,
		.base_baud	= 921600,
		.uart_offset	= 0x200,
	},
};

/*
 * Given a complete unknown PCI device, try to use some heuristics to
 * guess what the configuration might be, based on the pitiful PCI
 * serial specs.  Returns 0 on success, 1 on failure.
 */
static int __devinit
serial_pci_guess_board(struct pci_dev *dev, struct pciserial_board *board)
{
	int num_iomem, num_port, first_port = -1, i;
	
	/*
	 * If it is not a communications device or the programming
	 * interface is greater than 6, give up.
	 *
	 * (Should we try to make guesses for multiport serial devices
	 * later?) 
	 */
	if ((((dev->class >> 8) != PCI_CLASS_COMMUNICATION_SERIAL) &&
	     ((dev->class >> 8) != PCI_CLASS_COMMUNICATION_MODEM)) ||
	    (dev->class & 0xff) > 6)
		return -ENODEV;

	num_iomem = num_port = 0;
	for (i = 0; i < PCI_NUM_BAR_RESOURCES; i++) {
		if (pci_resource_flags(dev, i) & IORESOURCE_IO) {
			num_port++;
			if (first_port == -1)
				first_port = i;
		}
		if (pci_resource_flags(dev, i) & IORESOURCE_MEM)
			num_iomem++;
	}

	/*
	 * If there is 1 or 0 iomem regions, and exactly one port,
	 * use it.  We guess the number of ports based on the IO
	 * region size.
	 */
	if (num_iomem <= 1 && num_port == 1) {
		board->flags = first_port;
		board->num_ports = pci_resource_len(dev, first_port) / 8;
		return 0;
	}

	/*
	 * Now guess if we've got a board which indexes by BARs.
	 * Each IO BAR should be 8 bytes, and they should follow
	 * consecutively.
	 */
	first_port = -1;
	num_port = 0;
	for (i = 0; i < PCI_NUM_BAR_RESOURCES; i++) {
		if (pci_resource_flags(dev, i) & IORESOURCE_IO &&
		    pci_resource_len(dev, i) == 8 &&
		    (first_port == -1 || (first_port + num_port) == i)) {
			num_port++;
			if (first_port == -1)
				first_port = i;
		}
	}

	if (num_port > 1) {
		board->flags = first_port | FL_BASE_BARS;
		board->num_ports = num_port;
		return 0;
	}

	return -ENODEV;
}

static inline int
serial_pci_matches(struct pciserial_board *board,
		   struct pciserial_board *guessed)
{
	return
	    board->num_ports == guessed->num_ports &&
	    board->base_baud == guessed->base_baud &&
	    board->uart_offset == guessed->uart_offset &&
	    board->reg_shift == guessed->reg_shift &&
	    board->first_offset == guessed->first_offset;
}

struct serial_private *
pciserial_init_ports(struct pci_dev *dev, struct pciserial_board *board)
{
	struct uart_port serial_port;
	struct serial_private *priv;
	struct pci_serial_quirk *quirk;
	int rc, nr_ports, i;

	nr_ports = board->num_ports;

	/*
	 * Find an init and setup quirks.
	 */
	quirk = find_quirk(dev);

	/*
	 * Run the new-style initialization function.
	 * The initialization function returns:
	 *  <0  - error
	 *   0  - use board->num_ports
	 *  >0  - number of ports
	 */
	if (quirk->init) {
		rc = quirk->init(dev);
		if (rc < 0) {
			priv = ERR_PTR(rc);
			goto err_out;
		}
		if (rc)
			nr_ports = rc;
	}

	priv = kmalloc(sizeof(struct serial_private) +
		       sizeof(unsigned int) * nr_ports,
		       GFP_KERNEL);
	if (!priv) {
		priv = ERR_PTR(-ENOMEM);
		goto err_deinit;
	}

	memset(priv, 0, sizeof(struct serial_private) +
			sizeof(unsigned int) * nr_ports);

	priv->dev = dev;
	priv->quirk = quirk;

	memset(&serial_port, 0, sizeof(struct uart_port));
	serial_port.flags = UPF_SKIP_TEST | UPF_BOOT_AUTOCONF | UPF_SHARE_IRQ;
	serial_port.uartclk = board->base_baud * 16;
	serial_port.irq = get_pci_irq(dev, board);
	serial_port.dev = &dev->dev;

	for (i = 0; i < nr_ports; i++) {
		if (quirk->setup(priv, board, &serial_port, i))
			break;

#ifdef SERIAL_DEBUG_PCI
		printk("Setup PCI port: port %x, irq %d, type %d\n",
		       serial_port.iobase, serial_port.irq, serial_port.iotype);
#endif
		
		priv->line[i] = serial8250_register_port(&serial_port);
		if (priv->line[i] < 0) {
			printk(KERN_WARNING "Couldn't register serial port %s: %d\n", pci_name(dev), priv->line[i]);
			break;
		}
	}

	priv->nr = i;

	return priv;

 err_deinit:
	if (quirk->exit)
		quirk->exit(dev);
 err_out:
	return priv;
}
EXPORT_SYMBOL_GPL(pciserial_init_ports);

void pciserial_remove_ports(struct serial_private *priv)
{
	struct pci_serial_quirk *quirk;
	int i;

	for (i = 0; i < priv->nr; i++)
		serial8250_unregister_port(priv->line[i]);

	for (i = 0; i < PCI_NUM_BAR_RESOURCES; i++) {
		if (priv->remapped_bar[i])
			iounmap(priv->remapped_bar[i]);
		priv->remapped_bar[i] = NULL;
	}

	/*
	 * Find the exit quirks.
	 */
	quirk = find_quirk(priv->dev);
	if (quirk->exit)
		quirk->exit(priv->dev);

	kfree(priv);
}
EXPORT_SYMBOL_GPL(pciserial_remove_ports);

void pciserial_suspend_ports(struct serial_private *priv)
{
	int i;

	for (i = 0; i < priv->nr; i++)
		if (priv->line[i] >= 0)
			serial8250_suspend_port(priv->line[i]);
}
EXPORT_SYMBOL_GPL(pciserial_suspend_ports);

void pciserial_resume_ports(struct serial_private *priv)
{
	int i;

	/*
	 * Ensure that the board is correctly configured.
	 */
	if (priv->quirk->init)
		priv->quirk->init(priv->dev);

	for (i = 0; i < priv->nr; i++)
		if (priv->line[i] >= 0)
			serial8250_resume_port(priv->line[i]);
}
EXPORT_SYMBOL_GPL(pciserial_resume_ports);

/*
 * Probe one serial board.  Unfortunately, there is no rhyme nor reason
 * to the arrangement of serial ports on a PCI card.
 */
static int __devinit
pciserial_init_one(struct pci_dev *dev, const struct pci_device_id *ent)
{
	struct serial_private *priv;
	struct pciserial_board *board, tmp;
	int rc;

	if (ent->driver_data >= ARRAY_SIZE(pci_boards)) {
		printk(KERN_ERR "pci_init_one: invalid driver_data: %ld\n",
			ent->driver_data);
		return -EINVAL;
	}

	board = &pci_boards[ent->driver_data];

	rc = pci_enable_device(dev);
	if (rc)
		return rc;

	if (ent->driver_data == pbn_default) {
		/*
		 * Use a copy of the pci_board entry for this;
		 * avoid changing entries in the table.
		 */
		memcpy(&tmp, board, sizeof(struct pciserial_board));
		board = &tmp;

		/*
		 * We matched one of our class entries.  Try to
		 * determine the parameters of this board.
		 */
		rc = serial_pci_guess_board(dev, board);
		if (rc)
			goto disable;
	} else {
		/*
		 * We matched an explicit entry.  If we are able to
		 * detect this boards settings with our heuristic,
		 * then we no longer need this entry.
		 */
		memcpy(&tmp, &pci_boards[pbn_default],
		       sizeof(struct pciserial_board));
		rc = serial_pci_guess_board(dev, &tmp);
		if (rc == 0 && serial_pci_matches(board, &tmp))
			moan_device("Redundant entry in serial pci_table.",
				    dev);
	}

	priv = pciserial_init_ports(dev, board);
	if (!IS_ERR(priv)) {
		pci_set_drvdata(dev, priv);
		return 0;
	}

	rc = PTR_ERR(priv);

 disable:
	pci_disable_device(dev);
	return rc;
}

static void __devexit pciserial_remove_one(struct pci_dev *dev)
{
	struct serial_private *priv = pci_get_drvdata(dev);

	pci_set_drvdata(dev, NULL);

	pciserial_remove_ports(priv);

	pci_disable_device(dev);
}

static int pciserial_suspend_one(struct pci_dev *dev, pm_message_t state)
{
	struct serial_private *priv = pci_get_drvdata(dev);

	if (priv)
		pciserial_suspend_ports(priv);

	pci_save_state(dev);
	pci_set_power_state(dev, pci_choose_state(dev, state));
	return 0;
}

static int pciserial_resume_one(struct pci_dev *dev)
{
	struct serial_private *priv = pci_get_drvdata(dev);

	pci_set_power_state(dev, PCI_D0);
	pci_restore_state(dev);

	if (priv) {
		/*
		 * The device may have been disabled.  Re-enable it.
		 */
		pci_enable_device(dev);

		pciserial_resume_ports(priv);
	}
	return 0;
}

static struct pci_device_id serial_pci_tbl[] = {
	{	PCI_VENDOR_ID_V3, PCI_DEVICE_ID_V3_V960,
		PCI_SUBVENDOR_ID_CONNECT_TECH,
		PCI_SUBDEVICE_ID_CONNECT_TECH_BH8_232, 0, 0,
		pbn_b1_8_1382400 },
	{	PCI_VENDOR_ID_V3, PCI_DEVICE_ID_V3_V960,
		PCI_SUBVENDOR_ID_CONNECT_TECH,
		PCI_SUBDEVICE_ID_CONNECT_TECH_BH4_232, 0, 0,
		pbn_b1_4_1382400 },
	{	PCI_VENDOR_ID_V3, PCI_DEVICE_ID_V3_V960,
		PCI_SUBVENDOR_ID_CONNECT_TECH,
		PCI_SUBDEVICE_ID_CONNECT_TECH_BH2_232, 0, 0,
		pbn_b1_2_1382400 },
	{	PCI_VENDOR_ID_V3, PCI_DEVICE_ID_V3_V351,
		PCI_SUBVENDOR_ID_CONNECT_TECH,
		PCI_SUBDEVICE_ID_CONNECT_TECH_BH8_232, 0, 0,
		pbn_b1_8_1382400 },
	{	PCI_VENDOR_ID_V3, PCI_DEVICE_ID_V3_V351,
		PCI_SUBVENDOR_ID_CONNECT_TECH,
		PCI_SUBDEVICE_ID_CONNECT_TECH_BH4_232, 0, 0,
		pbn_b1_4_1382400 },
	{	PCI_VENDOR_ID_V3, PCI_DEVICE_ID_V3_V351,
		PCI_SUBVENDOR_ID_CONNECT_TECH,
		PCI_SUBDEVICE_ID_CONNECT_TECH_BH2_232, 0, 0,
		pbn_b1_2_1382400 },
	{	PCI_VENDOR_ID_V3, PCI_DEVICE_ID_V3_V351,
		PCI_SUBVENDOR_ID_CONNECT_TECH,
		PCI_SUBDEVICE_ID_CONNECT_TECH_BH8_485, 0, 0,
		pbn_b1_8_921600 },
	{	PCI_VENDOR_ID_V3, PCI_DEVICE_ID_V3_V351,
		PCI_SUBVENDOR_ID_CONNECT_TECH,
		PCI_SUBDEVICE_ID_CONNECT_TECH_BH8_485_4_4, 0, 0,
		pbn_b1_8_921600 },
	{	PCI_VENDOR_ID_V3, PCI_DEVICE_ID_V3_V351,
		PCI_SUBVENDOR_ID_CONNECT_TECH,
		PCI_SUBDEVICE_ID_CONNECT_TECH_BH4_485, 0, 0,
		pbn_b1_4_921600 },
	{	PCI_VENDOR_ID_V3, PCI_DEVICE_ID_V3_V351,
		PCI_SUBVENDOR_ID_CONNECT_TECH,
		PCI_SUBDEVICE_ID_CONNECT_TECH_BH4_485_2_2, 0, 0,
		pbn_b1_4_921600 },
	{	PCI_VENDOR_ID_V3, PCI_DEVICE_ID_V3_V351,
		PCI_SUBVENDOR_ID_CONNECT_TECH,
		PCI_SUBDEVICE_ID_CONNECT_TECH_BH2_485, 0, 0,
		pbn_b1_2_921600 },
	{	PCI_VENDOR_ID_V3, PCI_DEVICE_ID_V3_V351,
		PCI_SUBVENDOR_ID_CONNECT_TECH,
		PCI_SUBDEVICE_ID_CONNECT_TECH_BH8_485_2_6, 0, 0,
		pbn_b1_8_921600 },
	{	PCI_VENDOR_ID_V3, PCI_DEVICE_ID_V3_V351,
		PCI_SUBVENDOR_ID_CONNECT_TECH,
		PCI_SUBDEVICE_ID_CONNECT_TECH_BH081101V1, 0, 0,
		pbn_b1_8_921600 },
	{	PCI_VENDOR_ID_V3, PCI_DEVICE_ID_V3_V351,
		PCI_SUBVENDOR_ID_CONNECT_TECH,
		PCI_SUBDEVICE_ID_CONNECT_TECH_BH041101V1, 0, 0,
		pbn_b1_4_921600 },

	{	PCI_VENDOR_ID_SEALEVEL, PCI_DEVICE_ID_SEALEVEL_U530,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 
		pbn_b2_bt_1_115200 },
	{	PCI_VENDOR_ID_SEALEVEL, PCI_DEVICE_ID_SEALEVEL_UCOMM2,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 
		pbn_b2_bt_2_115200 },
	{	PCI_VENDOR_ID_SEALEVEL, PCI_DEVICE_ID_SEALEVEL_UCOMM422,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 
		pbn_b2_bt_4_115200 },
	{	PCI_VENDOR_ID_SEALEVEL, PCI_DEVICE_ID_SEALEVEL_UCOMM232,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 
		pbn_b2_bt_2_115200 },
	{	PCI_VENDOR_ID_SEALEVEL, PCI_DEVICE_ID_SEALEVEL_COMM4,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 
		pbn_b2_bt_4_115200 },
	{	PCI_VENDOR_ID_SEALEVEL, PCI_DEVICE_ID_SEALEVEL_COMM8,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 
		pbn_b2_8_115200 },
	{	PCI_VENDOR_ID_SEALEVEL, PCI_DEVICE_ID_SEALEVEL_UCOMM8,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b2_8_115200 },

	{	PCI_VENDOR_ID_PLX, PCI_DEVICE_ID_PLX_GTEK_SERIAL2,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b2_bt_2_115200 },
	{	PCI_VENDOR_ID_PLX, PCI_DEVICE_ID_PLX_SPCOM200,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b2_bt_2_921600 },
	/*
	 * VScom SPCOM800, from sl@s.pl
	 */
	{	PCI_VENDOR_ID_PLX, PCI_DEVICE_ID_PLX_SPCOM800, 
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 
		pbn_b2_8_921600 },
	{	PCI_VENDOR_ID_PLX, PCI_DEVICE_ID_PLX_1077,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 
		pbn_b2_4_921600 },
	{	PCI_VENDOR_ID_PLX, PCI_DEVICE_ID_PLX_9050,
		PCI_SUBVENDOR_ID_KEYSPAN,
		PCI_SUBDEVICE_ID_KEYSPAN_SX2, 0, 0,
		pbn_panacom },
	{	PCI_VENDOR_ID_PANACOM, PCI_DEVICE_ID_PANACOM_QUADMODEM,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_panacom4 },
	{	PCI_VENDOR_ID_PANACOM, PCI_DEVICE_ID_PANACOM_DUALMODEM,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_panacom2 },
	{	PCI_VENDOR_ID_PLX, PCI_DEVICE_ID_PLX_9050,
		PCI_SUBVENDOR_ID_CHASE_PCIFAST,
		PCI_SUBDEVICE_ID_CHASE_PCIFAST4, 0, 0, 
		pbn_b2_4_460800 },
	{	PCI_VENDOR_ID_PLX, PCI_DEVICE_ID_PLX_9050,
		PCI_SUBVENDOR_ID_CHASE_PCIFAST,
		PCI_SUBDEVICE_ID_CHASE_PCIFAST8, 0, 0, 
		pbn_b2_8_460800 },
	{	PCI_VENDOR_ID_PLX, PCI_DEVICE_ID_PLX_9050,
		PCI_SUBVENDOR_ID_CHASE_PCIFAST,
		PCI_SUBDEVICE_ID_CHASE_PCIFAST16, 0, 0, 
		pbn_b2_16_460800 },
	{	PCI_VENDOR_ID_PLX, PCI_DEVICE_ID_PLX_9050,
		PCI_SUBVENDOR_ID_CHASE_PCIFAST,
		PCI_SUBDEVICE_ID_CHASE_PCIFAST16FMC, 0, 0, 
		pbn_b2_16_460800 },
	{	PCI_VENDOR_ID_PLX, PCI_DEVICE_ID_PLX_9050,
		PCI_SUBVENDOR_ID_CHASE_PCIRAS,
		PCI_SUBDEVICE_ID_CHASE_PCIRAS4, 0, 0, 
		pbn_b2_4_460800 },
	{	PCI_VENDOR_ID_PLX, PCI_DEVICE_ID_PLX_9050,
		PCI_SUBVENDOR_ID_CHASE_PCIRAS,
		PCI_SUBDEVICE_ID_CHASE_PCIRAS8, 0, 0, 
		pbn_b2_8_460800 },
	{	PCI_VENDOR_ID_PLX, PCI_DEVICE_ID_PLX_9050,
		PCI_SUBVENDOR_ID_EXSYS,
		PCI_SUBDEVICE_ID_EXSYS_4055, 0, 0,
		pbn_exsys_4055 },
	/*
	 * Megawolf Romulus PCI Serial Card, from Mike Hudson
	 * (Exoray@isys.ca)
	 */
	{	PCI_VENDOR_ID_PLX, PCI_DEVICE_ID_PLX_ROMULUS,
		0x10b5, 0x106a, 0, 0,
		pbn_plx_romulus },
	{	PCI_VENDOR_ID_QUATECH, PCI_DEVICE_ID_QUATECH_QSC100,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b1_4_115200 },
	{	PCI_VENDOR_ID_QUATECH, PCI_DEVICE_ID_QUATECH_DSC100,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b1_2_115200 },
	{	PCI_VENDOR_ID_QUATECH, PCI_DEVICE_ID_QUATECH_ESC100D,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b1_8_115200 },
	{	PCI_VENDOR_ID_QUATECH, PCI_DEVICE_ID_QUATECH_ESC100M,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b1_8_115200 },
	{	PCI_VENDOR_ID_SPECIALIX, PCI_DEVICE_ID_OXSEMI_16PCI954,
		PCI_VENDOR_ID_SPECIALIX, PCI_SUBDEVICE_ID_SPECIALIX_SPEED4, 0, 0,
		pbn_b0_4_921600 },
	{	PCI_VENDOR_ID_OXSEMI, PCI_DEVICE_ID_OXSEMI_16PCI954,
		PCI_SUBVENDOR_ID_SIIG, PCI_SUBDEVICE_ID_SIIG_QUARTET_SERIAL, 0, 0,
		pbn_b0_4_1152000 },

		/*
		 * The below card is a little controversial since it is the
		 * subject of a PCI vendor/device ID clash.  (See
		 * www.ussg.iu.edu/hypermail/linux/kernel/0303.1/0516.html).
		 * For now just used the hex ID 0x950a.
		 */
	{	PCI_VENDOR_ID_OXSEMI, 0x950a,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b0_2_1130000 },
	{	PCI_VENDOR_ID_OXSEMI, PCI_DEVICE_ID_OXSEMI_16PCI954,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b0_4_115200 },
	{	PCI_VENDOR_ID_OXSEMI, PCI_DEVICE_ID_OXSEMI_16PCI952,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b0_bt_2_921600 },

	/*
	 * SBS Technologies, Inc. P-Octal and PMC-OCTPRO cards,
	 * from skokodyn@yahoo.com
	 */
	{	PCI_VENDOR_ID_SBSMODULARIO, PCI_DEVICE_ID_OCTPRO,
		PCI_SUBVENDOR_ID_SBSMODULARIO, PCI_SUBDEVICE_ID_OCTPRO232, 0, 0,
		pbn_sbsxrsio },
	{	PCI_VENDOR_ID_SBSMODULARIO, PCI_DEVICE_ID_OCTPRO,
		PCI_SUBVENDOR_ID_SBSMODULARIO, PCI_SUBDEVICE_ID_OCTPRO422, 0, 0,
		pbn_sbsxrsio },
	{	PCI_VENDOR_ID_SBSMODULARIO, PCI_DEVICE_ID_OCTPRO,
		PCI_SUBVENDOR_ID_SBSMODULARIO, PCI_SUBDEVICE_ID_POCTAL232, 0, 0,
		pbn_sbsxrsio },
	{	PCI_VENDOR_ID_SBSMODULARIO, PCI_DEVICE_ID_OCTPRO,
		PCI_SUBVENDOR_ID_SBSMODULARIO, PCI_SUBDEVICE_ID_POCTAL422, 0, 0,
		pbn_sbsxrsio },

	/*
	 * Digitan DS560-558, from jimd@esoft.com
	 */
	{	PCI_VENDOR_ID_ATT, PCI_DEVICE_ID_ATT_VENUS_MODEM,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 
		pbn_b1_1_115200 },

	/*
	 * Titan Electronic cards
	 *  The 400L and 800L have a custom setup quirk.
	 */
	{	PCI_VENDOR_ID_TITAN, PCI_DEVICE_ID_TITAN_100,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 
		pbn_b0_1_921600 },
	{	PCI_VENDOR_ID_TITAN, PCI_DEVICE_ID_TITAN_200,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 
		pbn_b0_2_921600 },
	{	PCI_VENDOR_ID_TITAN, PCI_DEVICE_ID_TITAN_400,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 
		pbn_b0_4_921600 },
	{	PCI_VENDOR_ID_TITAN, PCI_DEVICE_ID_TITAN_800B,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 
		pbn_b0_4_921600 },
	{	PCI_VENDOR_ID_TITAN, PCI_DEVICE_ID_TITAN_100L,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b1_1_921600 },
	{	PCI_VENDOR_ID_TITAN, PCI_DEVICE_ID_TITAN_200L,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b1_bt_2_921600 },
	{	PCI_VENDOR_ID_TITAN, PCI_DEVICE_ID_TITAN_400L,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b0_bt_4_921600 },
	{	PCI_VENDOR_ID_TITAN, PCI_DEVICE_ID_TITAN_800L,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b0_bt_8_921600 },

	{	PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_1S_10x_550,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b2_1_460800 },
	{	PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_1S_10x_650,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b2_1_460800 },
	{	PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_1S_10x_850,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b2_1_460800 },
	{	PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_2S_10x_550,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b2_bt_2_921600 },
	{	PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_2S_10x_650,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b2_bt_2_921600 },
	{	PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_2S_10x_850,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b2_bt_2_921600 },
	{	PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_4S_10x_550,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b2_bt_4_921600 },
	{	PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_4S_10x_650,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b2_bt_4_921600 },
	{	PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_4S_10x_850,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b2_bt_4_921600 },
	{	PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_1S_20x_550,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b0_1_921600 },
	{	PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_1S_20x_650,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b0_1_921600 },
	{	PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_1S_20x_850,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b0_1_921600 },
	{	PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_2S_20x_550,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b0_bt_2_921600 },
	{	PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_2S_20x_650,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b0_bt_2_921600 },
	{	PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_2S_20x_850,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b0_bt_2_921600 },
	{	PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_4S_20x_550,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b0_bt_4_921600 },
	{	PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_4S_20x_650,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b0_bt_4_921600 },
	{	PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_4S_20x_850,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b0_bt_4_921600 },

	/*
	 * Computone devices submitted by Doug McNash dmcnash@computone.com
	 */
	{	PCI_VENDOR_ID_COMPUTONE, PCI_DEVICE_ID_COMPUTONE_PG,
		PCI_SUBVENDOR_ID_COMPUTONE, PCI_SUBDEVICE_ID_COMPUTONE_PG4,
		0, 0, pbn_computone_4 },
	{	PCI_VENDOR_ID_COMPUTONE, PCI_DEVICE_ID_COMPUTONE_PG,
		PCI_SUBVENDOR_ID_COMPUTONE, PCI_SUBDEVICE_ID_COMPUTONE_PG8,
		0, 0, pbn_computone_8 },
	{	PCI_VENDOR_ID_COMPUTONE, PCI_DEVICE_ID_COMPUTONE_PG,
		PCI_SUBVENDOR_ID_COMPUTONE, PCI_SUBDEVICE_ID_COMPUTONE_PG6,
		0, 0, pbn_computone_6 },

	{	PCI_VENDOR_ID_OXSEMI, PCI_DEVICE_ID_OXSEMI_16PCI95N,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_oxsemi },
	{	PCI_VENDOR_ID_TIMEDIA, PCI_DEVICE_ID_TIMEDIA_1889,
		PCI_VENDOR_ID_TIMEDIA, PCI_ANY_ID, 0, 0,
		pbn_b0_bt_1_921600 },

	/*
	 * AFAVLAB serial card, from Harald Welte <laforge@gnumonks.org>
	 */
	{	PCI_VENDOR_ID_AFAVLAB, PCI_DEVICE_ID_AFAVLAB_P028,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b0_bt_8_115200 },
	{	PCI_VENDOR_ID_AFAVLAB, PCI_DEVICE_ID_AFAVLAB_P030,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b0_bt_8_115200 },

	{	PCI_VENDOR_ID_LAVA, PCI_DEVICE_ID_LAVA_DSERIAL,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b0_bt_2_115200 },
	{	PCI_VENDOR_ID_LAVA, PCI_DEVICE_ID_LAVA_QUATRO_A,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b0_bt_2_115200 },
	{	PCI_VENDOR_ID_LAVA, PCI_DEVICE_ID_LAVA_QUATRO_B,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b0_bt_2_115200 },
	{	PCI_VENDOR_ID_LAVA, PCI_DEVICE_ID_LAVA_OCTO_A,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b0_bt_4_460800 },
	{	PCI_VENDOR_ID_LAVA, PCI_DEVICE_ID_LAVA_OCTO_B,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b0_bt_4_460800 },
	{	PCI_VENDOR_ID_LAVA, PCI_DEVICE_ID_LAVA_PORT_PLUS,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b0_bt_2_460800 },
	{	PCI_VENDOR_ID_LAVA, PCI_DEVICE_ID_LAVA_QUAD_A,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b0_bt_2_460800 },
	{	PCI_VENDOR_ID_LAVA, PCI_DEVICE_ID_LAVA_QUAD_B,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b0_bt_2_460800 },
	{	PCI_VENDOR_ID_LAVA, PCI_DEVICE_ID_LAVA_SSERIAL,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b0_bt_1_115200 },
	{	PCI_VENDOR_ID_LAVA, PCI_DEVICE_ID_LAVA_PORT_650,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b0_bt_1_460800 },

	/*
	 * Dell Remote Access Card 4 - Tim_T_Murphy@Dell.com
	 */
	{	PCI_VENDOR_ID_DELL, PCI_DEVICE_ID_DELL_RAC4,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b1_1_1382400 },

	/*
	 * Dell Remote Access Card III - Tim_T_Murphy@Dell.com
	 */
	{	PCI_VENDOR_ID_DELL, PCI_DEVICE_ID_DELL_RACIII,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b1_1_1382400 },

	/*
	 * RAStel 2 port modem, gerg@moreton.com.au
	 */
	{	PCI_VENDOR_ID_MORETON, PCI_DEVICE_ID_RASTEL_2PORT,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b2_bt_2_115200 },

	/*
	 * EKF addition for i960 Boards form EKF with serial port
	 */
	{	PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_80960_RP,
		0xE4BF, PCI_ANY_ID, 0, 0,
		pbn_intel_i960 },

	/*
	 * Xircom Cardbus/Ethernet combos
	 */
	{	PCI_VENDOR_ID_XIRCOM, PCI_DEVICE_ID_XIRCOM_X3201_MDM,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b0_1_115200 },
	/*
	 * Xircom RBM56G cardbus modem - Dirk Arnold (temp entry)
	 */
	{	PCI_VENDOR_ID_XIRCOM, PCI_DEVICE_ID_XIRCOM_RBM56G,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b0_1_115200 },

	/*
	 * Untested PCI modems, sent in from various folks...
	 */

	/*
	 * Elsa Model 56K PCI Modem, from Andreas Rath <arh@01019freenet.de>
	 */
	{	PCI_VENDOR_ID_ROCKWELL, 0x1004,
		0x1048, 0x1500, 0, 0,
		pbn_b1_1_115200 },

	{	PCI_VENDOR_ID_SGI, PCI_DEVICE_ID_SGI_IOC3,
		0xFF00, 0, 0, 0,
		pbn_sgi_ioc3 },

	/*
	 * HP Diva card
	 */
	{	PCI_VENDOR_ID_HP, PCI_DEVICE_ID_HP_DIVA,
		PCI_VENDOR_ID_HP, PCI_DEVICE_ID_HP_DIVA_RMP3, 0, 0,
		pbn_b1_1_115200 },
	{	PCI_VENDOR_ID_HP, PCI_DEVICE_ID_HP_DIVA,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b0_5_115200 },
	{	PCI_VENDOR_ID_HP, PCI_DEVICE_ID_HP_DIVA_AUX,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b2_1_115200 },

	/*
	 * NEC Vrc-5074 (Nile 4) builtin UART.
	 */
	{	PCI_VENDOR_ID_NEC, PCI_DEVICE_ID_NEC_NILE4,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_nec_nile4 },

	{	PCI_VENDOR_ID_DCI, PCI_DEVICE_ID_DCI_PCCOM4,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b3_4_115200 },
	{	PCI_VENDOR_ID_DCI, PCI_DEVICE_ID_DCI_PCCOM8,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b3_8_115200 },

	/*
	 * Exar Corp. XR17C15[248] Dual/Quad/Octal UART
	 */
	{	PCI_VENDOR_ID_EXAR, PCI_DEVICE_ID_EXAR_XR17C152,
		PCI_ANY_ID, PCI_ANY_ID,
		0,
		0, pbn_exar_XR17C152 },
	{	PCI_VENDOR_ID_EXAR, PCI_DEVICE_ID_EXAR_XR17C154,
		PCI_ANY_ID, PCI_ANY_ID,
		0,
		0, pbn_exar_XR17C154 },
	{	PCI_VENDOR_ID_EXAR, PCI_DEVICE_ID_EXAR_XR17C158,
		PCI_ANY_ID, PCI_ANY_ID,
		0,
		0, pbn_exar_XR17C158 },

	/*
	 * Topic TP560 Data/Fax/Voice 56k modem (reported by Evan Clarke)
	 */
	{	PCI_VENDOR_ID_TOPIC, PCI_DEVICE_ID_TOPIC_TP560,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b0_1_115200 },

	/*
	 * These entries match devices with class COMMUNICATION_SERIAL,
	 * COMMUNICATION_MODEM or COMMUNICATION_MULTISERIAL
	 */
	{	PCI_ANY_ID, PCI_ANY_ID,
		PCI_ANY_ID, PCI_ANY_ID,
		PCI_CLASS_COMMUNICATION_SERIAL << 8,
		0xffff00, pbn_default },
	{	PCI_ANY_ID, PCI_ANY_ID,
		PCI_ANY_ID, PCI_ANY_ID,
		PCI_CLASS_COMMUNICATION_MODEM << 8,
		0xffff00, pbn_default },
	{	PCI_ANY_ID, PCI_ANY_ID,
		PCI_ANY_ID, PCI_ANY_ID,
		PCI_CLASS_COMMUNICATION_MULTISERIAL << 8,
		0xffff00, pbn_default },
	{ 0, }
};

static struct pci_driver serial_pci_driver = {
	.name		= "serial",
	.probe		= pciserial_init_one,
	.remove		= __devexit_p(pciserial_remove_one),
	.suspend	= pciserial_suspend_one,
	.resume		= pciserial_resume_one,
	.id_table	= serial_pci_tbl,
};

static int __init serial8250_pci_init(void)
{
	return pci_register_driver(&serial_pci_driver);
}

static void __exit serial8250_pci_exit(void)
{
	pci_unregister_driver(&serial_pci_driver);
}

module_init(serial8250_pci_init);
module_exit(serial8250_pci_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Generic 8250/16x50 PCI serial probe module");
MODULE_DEVICE_TABLE(pci, serial_pci_tbl);
