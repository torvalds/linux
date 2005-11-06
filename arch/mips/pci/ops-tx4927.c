/*
 * Copyright 2001 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *              ahennessy@mvista.com
 *
 * Copyright (C) 2000-2001 Toshiba Corporation
 * Copyright (C) 2004 by Ralf Baechle (ralf@linux-mips.org)
 *
 * Based on arch/mips/ddb5xxx/ddb5477/pci_ops.c
 *
 *     Define the pci_ops for the Toshiba rbtx4927
 *
 * Much of the code is derived from the original DDB5074 port by
 * Geert Uytterhoeven <geert@sonycom.com>
 *
 * Copyright 2004 MontaVista Software Inc.
 * Author: Manish Lachwani (mlachwani@mvista.com)
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <asm/addrspace.h>
#include <asm/byteorder.h>
#include <asm/tx4927/tx4927_pci.h>

/* initialize in setup */
struct resource pci_io_resource = {
	.name	= "TX4927 PCI IO SPACE",
	.start	= 0x1000,
	.end	= (0x1000 + (TX4927_PCIIO_SIZE)) - 1,
	.flags	= IORESOURCE_IO
};

/* initialize in setup */
struct resource pci_mem_resource = {
	.name	= "TX4927 PCI MEM SPACE",
	.start	= TX4927_PCIMEM,
	.end	= TX4927_PCIMEM + TX4927_PCIMEM_SIZE - 1,
	.flags	= IORESOURCE_MEM
};

static int mkaddr(int bus, int dev_fn, int where, int *flagsp)
{
	if (bus > 0) {
		/* Type 1 configuration */
		tx4927_pcicptr->g2pcfgadrs = ((bus & 0xff) << 0x10) |
		    ((dev_fn & 0xff) << 0x08) | (where & 0xfc) | 1;
	} else {
		if (dev_fn >= PCI_DEVFN(TX4927_PCIC_MAX_DEVNU, 0))
			return -1;

		/* Type 0 configuration */
		tx4927_pcicptr->g2pcfgadrs = ((bus & 0xff) << 0x10) |
		    ((dev_fn & 0xff) << 0x08) | (where & 0xfc);
	}
	/* clear M_ABORT and Disable M_ABORT Int. */
	tx4927_pcicptr->pcistatus =
	    (tx4927_pcicptr->pcistatus & 0x0000ffff) |
	    (PCI_STATUS_REC_MASTER_ABORT << 16);
	tx4927_pcicptr->pcimask &= ~PCI_STATUS_REC_MASTER_ABORT;
	return 0;
}

static int check_abort(int flags)
{
	int code = PCIBIOS_SUCCESSFUL;
	if (tx4927_pcicptr->
	    pcistatus & (PCI_STATUS_REC_MASTER_ABORT << 16)) {
		tx4927_pcicptr->pcistatus =
		    (tx4927_pcicptr->
		     pcistatus & 0x0000ffff) | (PCI_STATUS_REC_MASTER_ABORT
						<< 16);
		tx4927_pcicptr->pcimask |= PCI_STATUS_REC_MASTER_ABORT;
		code = PCIBIOS_DEVICE_NOT_FOUND;
	}
	return code;
}

static int tx4927_pcibios_read_config(struct pci_bus *bus, unsigned int devfn, int where,
		int size, u32 * val)
{
	int flags, retval, dev, busno, func;

	busno = bus->number;
        dev = PCI_SLOT(devfn);
        func = PCI_FUNC(devfn);

	/* check if the bus is top-level */
	if (bus->parent != NULL) {
		busno = bus->number;
	} else {
		busno = 0;
	}

	if (mkaddr(busno, devfn, where, &flags))
		return -1;

	switch (size) {
	case 1:
		*val = *(volatile u8 *) ((ulong) & tx4927_pcicptr->
                              g2pcfgdata |
#ifdef __LITTLE_ENDIAN
						(where & 3));
#else
						((where & 0x3) ^ 0x3));
#endif
		break;
	case 2:
		*val = *(volatile u16 *) ((ulong) & tx4927_pcicptr->
                               g2pcfgdata |
#ifdef __LITTLE_ENDIAN
						(where & 3));
#else
						((where & 0x3) ^ 0x2));
#endif
		break;
	case 4:
		*val = tx4927_pcicptr->g2pcfgdata;
		break;
	}

	retval = check_abort(flags);
	if (retval == PCIBIOS_DEVICE_NOT_FOUND)
		*val = 0xffffffff;

	return retval;
}

static int tx4927_pcibios_write_config(struct pci_bus *bus, unsigned int devfn, int where,
				int size, u32 val)
{
	int flags, dev, busno, func;
	busno = bus->number;
        dev = PCI_SLOT(devfn);
        func = PCI_FUNC(devfn);

	/* check if the bus is top-level */
	if (bus->parent != NULL) {
		busno = bus->number;
	} else {
		busno = 0;
	}

	if (mkaddr(busno, devfn, where, &flags))
		return -1;

	switch (size) {
	case 1:
		 *(volatile u8 *) ((ulong) & tx4927_pcicptr->
                          g2pcfgdata |
#ifdef __LITTLE_ENDIAN
					(where & 3)) = val;
#else
					((where & 0x3) ^ 0x3)) = val;
#endif
		break;

	case 2:
		*(volatile u16 *) ((ulong) & tx4927_pcicptr->
                           g2pcfgdata |
#ifdef __LITTLE_ENDIAN
					(where & 3)) = val;
#else
					((where & 0x3) ^ 0x2)) = val;
#endif
		break;
	case 4:
		tx4927_pcicptr->g2pcfgdata = val;
		break;
	}

	return check_abort(flags);
}

struct pci_ops tx4927_pci_ops = {
	tx4927_pcibios_read_config,
	tx4927_pcibios_write_config
};

/*
 * h/w only supports devices 0x00 to 0x14
 */
struct pci_controller tx4927_controller = {
	.pci_ops        = &tx4927_pci_ops,
	.io_resource    = &pci_io_resource,
	.mem_resource   = &pci_mem_resource,
};
