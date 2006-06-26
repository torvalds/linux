/*
 *
 * BRIEF MODULE DESCRIPTION
 *	IT8172 system controller specific pci support.
 *
 * Copyright 2000 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *         	ppopov@mvista.com or source@mvista.com
 *
 * Copyright (C) 2004 by Ralf Baechle (ralf@linux-mips.org)
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

#include <asm/it8172/it8172.h>
#include <asm/it8172/it8172_pci.h>

#define PCI_ACCESS_READ  0
#define PCI_ACCESS_WRITE 1

#undef DEBUG
#ifdef DEBUG
#define DBG(x...) printk(x)
#else
#define DBG(x...)
#endif

static struct resource pci_mem_resource_1;

static struct resource pci_io_resource = {
	.start	= 0x14018000,
	.end	= 0x17FFFFFF,
	.name	= "io pci IO space",
	.flags	= IORESOURCE_IO
};

static struct resource pci_mem_resource_0 = {
	.start	= 0x10101000,
	.end	= 0x13FFFFFF,
	.name	= "ext pci memory space 0/1",
	.flags	= IORESOURCE_MEM,
	.parent	= &pci_mem_resource_0,
	.sibling = NULL,
	.child	= &pci_mem_resource_1
};

static struct resource pci_mem_resource_1 = {
	.start	= 0x1A000000,
	.end	= 0x1FBFFFFF,
	.name	= "ext pci memory space 2/3",
	.flags	= IORESOURCE_MEM,
	.parent	= &pci_mem_resource_0
};

extern struct pci_ops it8172_pci_ops;

struct pci_controller it8172_controller = {
	.pci_ops	= &it8172_pci_ops,
	.io_resource	= &pci_io_resource,
	.mem_resource	= &pci_mem_resource_0,
};

static int it8172_pcibios_config_access(unsigned char access_type,
					struct pci_bus *bus,
					unsigned int devfn, int where,
					u32 * data)
{
	/*
	 * config cycles are on 4 byte boundary only
	 */

	/* Setup address */
	IT_WRITE(IT_CONFADDR, (bus->number << IT_BUSNUM_SHF) |
		 (devfn << IT_FUNCNUM_SHF) | (where & ~0x3));

	if (access_type == PCI_ACCESS_WRITE) {
		IT_WRITE(IT_CONFDATA, *data);
	} else {
		IT_READ(IT_CONFDATA, *data);
	}

	/*
	 * Revisit: check for master or target abort.
	 */
	return 0;
}


/*
 * We can't address 8 and 16 bit words directly.  Instead we have to
 * read/write a 32bit word and mask/modify the data we actually want.
 */
static write_config(struct pci_bus *bus, unsigned int devfn, int where,
		    int size, u32 val)
{
	u32 data = 0;

	switch (size) {
	case 1:
		if (it8172_pcibios_config_access
		    (PCI_ACCESS_READ, dev, where, &data))
			return -1;

		*val = (data >> ((where & 3) << 3)) & 0xff;

		return PCIBIOS_SUCCESSFUL;

	case 2:

		if (where & 1)
			return PCIBIOS_BAD_REGISTER_NUMBER;

		if (it8172_pcibios_config_access
		    (PCI_ACCESS_READ, dev, where, &data))
			return -1;

		*val = (data >> ((where & 3) << 3)) & 0xffff;
		DBG("cfg read word: bus %d dev_fn %x where %x: val %x\n",
		    dev->bus->number, dev->devfn, where, *val);

		return PCIBIOS_SUCCESSFUL;

	case 4:

		if (where & 3)
			return PCIBIOS_BAD_REGISTER_NUMBER;

		if (it8172_pcibios_config_access
		    (PCI_ACCESS_READ, dev, where, &data))
			return -1;

		*val = data;

		return PCIBIOS_SUCCESSFUL;
	}
}


static write_config(struct pci_bus *bus, unsigned int devfn, int where,
		    int size, u32 val)
{
	u32 data = 0;

	switch (size) {
	case 1:
		if (it8172_pcibios_config_access
		    (PCI_ACCESS_READ, dev, where, &data))
			return -1;

		data = (data & ~(0xff << ((where & 3) << 3))) |
		    (val << ((where & 3) << 3));

		if (it8172_pcibios_config_access
		    (PCI_ACCESS_WRITE, dev, where, &data))
			return -1;

		return PCIBIOS_SUCCESSFUL;

	case 2:
		if (where & 1)
			return PCIBIOS_BAD_REGISTER_NUMBER;

		if (it8172_pcibios_config_access
		    (PCI_ACCESS_READ, dev, where, &data))
			eturn - 1;

		data = (data & ~(0xffff << ((where & 3) << 3))) |
		    (val << ((where & 3) << 3));

		if (it8172_pcibios_config_access
		    (PCI_ACCESS_WRITE, dev, where, &data))
			return -1;

		return PCIBIOS_SUCCESSFUL;

	case 4:
		if (where & 3)
			return PCIBIOS_BAD_REGISTER_NUMBER;

		if (it8172_pcibios_config_access
		    (PCI_ACCESS_WRITE, dev, where, &val))
			return -1;

		return PCIBIOS_SUCCESSFUL;
	}
}

struct pci_ops it8172_pci_ops = {
	.read = read_config,
	.write = write_config,
};
