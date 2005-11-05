/*
 * Copyright 2003 PMC-Sierra
 * Author: Manish Lachwani (lachwani@pmc-sierra.com)
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

#include <linux/config.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <asm/pci.h>
#include <asm/io.h>

#include <linux/init.h>
#include <asm/titan_dep.h>

#ifdef CONFIG_HYPERTRANSPORT


/*
 * This function check if the Hypertransport Link Initialization completed. If
 * it did, then proceed further with scanning bus #2
 */
static __inline__ int check_titan_htlink(void)
{
        u32 val;

        val = *(volatile uint32_t *)(RM9000x2_HTLINK_REG);
        if (val & 0x00000020)
                /* HT Link Initialization completed */
                return 1;
        else
                return 0;
}

static int titan_ht_config_read_dword(struct pci_dev *device,
                                             int offset, u32* val)
{
        int dev, bus, func;
        uint32_t address_reg, data_reg;
        uint32_t address;

        bus = device->bus->number;
        dev = PCI_SLOT(device->devfn);
        func = PCI_FUNC(device->devfn);

	/* XXX Need to change the Bus # */
        if (bus > 2)
                address = (bus << 16) | (dev << 11) | (func << 8) | (offset & 0xfc) |
                                                        0x80000000 | 0x1;
        else
                address = (dev << 11) | (func << 8) | (offset & 0xfc) | 0x80000000;

        address_reg = RM9000x2_OCD_HTCFGA;
        data_reg =  RM9000x2_OCD_HTCFGD;

        RM9K_WRITE(address_reg, address);
        RM9K_READ(data_reg, val);

        return PCIBIOS_SUCCESSFUL;
}


static int titan_ht_config_read_word(struct pci_dev *device,
                                             int offset, u16* val)
{
        int dev, bus, func;
        uint32_t address_reg, data_reg;
        uint32_t address;

        bus = device->bus->number;
        dev = PCI_SLOT(device->devfn);
        func = PCI_FUNC(device->devfn);

	/* XXX Need to change the Bus # */
        if (bus > 2)
                address = (bus << 16) | (dev << 11) | (func << 8) | (offset & 0xfc) |
                                                0x80000000 | 0x1;
        else
                address = (dev << 11) | (func << 8) | (offset & 0xfc) | 0x80000000;

        address_reg = RM9000x2_OCD_HTCFGA;
        data_reg =  RM9000x2_OCD_HTCFGD;

        if ((offset & 0x3) == 0)
                offset = 0x2;
        else
                offset = 0x0;

        RM9K_WRITE(address_reg, address);
        RM9K_READ_16(data_reg + offset, val);

        return PCIBIOS_SUCCESSFUL;
}


u32 longswap(unsigned long l)
{
        unsigned char b1,b2,b3,b4;

        b1 = l&255;
        b2 = (l>>8)&255;
        b3 = (l>>16)&255;
        b4 = (l>>24)&255;

        return ((b1<<24) + (b2<<16) + (b3<<8) + b4);
}


static int titan_ht_config_read_byte(struct pci_dev *device,
                                             int offset, u8* val)
{
        int dev, bus, func;
        uint32_t address_reg, data_reg;
        uint32_t address;
        int offset1;

        bus = device->bus->number;
        dev = PCI_SLOT(device->devfn);
        func = PCI_FUNC(device->devfn);

	/* XXX Need to change the Bus # */
        if (bus > 2)
                address = (bus << 16) | (dev << 11) | (func << 8) | (offset & 0xfc) |
                                                        0x80000000 | 0x1;
        else
                address = (dev << 11) | (func << 8) | (offset & 0xfc) | 0x80000000;

        address_reg = RM9000x2_OCD_HTCFGA;
        data_reg =  RM9000x2_OCD_HTCFGD;

        RM9K_WRITE(address_reg, address);

        if ((offset & 0x3) == 0) {
                offset1 = 0x3;
        }
        if ((offset & 0x3) == 1) {
                offset1 = 0x2;
        }
        if ((offset & 0x3) == 2) {
                offset1 = 0x1;
        }
        if ((offset & 0x3) == 3) {
                offset1 = 0x0;
        }
        RM9K_READ_8(data_reg + offset1, val);

        return PCIBIOS_SUCCESSFUL;
}


static int titan_ht_config_write_dword(struct pci_dev *device,
                                             int offset, u8 val)
{
        int dev, bus, func;
        uint32_t address_reg, data_reg;
        uint32_t address;

        bus = device->bus->number;
        dev = PCI_SLOT(device->devfn);
        func = PCI_FUNC(device->devfn);

	/* XXX Need to change the Bus # */
        if (bus > 2)
                address = (bus << 16) | (dev << 11) | (func << 8) | (offset & 0xfc) |
                                                        0x80000000 | 0x1;
        else
              address = (dev << 11) | (func << 8) | (offset & 0xfc) | 0x80000000;

        address_reg = RM9000x2_OCD_HTCFGA;
        data_reg =  RM9000x2_OCD_HTCFGD;

        RM9K_WRITE(address_reg, address);
        RM9K_WRITE(data_reg, val);

        return PCIBIOS_SUCCESSFUL;
}

static int titan_ht_config_write_word(struct pci_dev *device,
                                             int offset, u8 val)
{
        int dev, bus, func;
        uint32_t address_reg, data_reg;
        uint32_t address;

        bus = device->bus->number;
        dev = PCI_SLOT(device->devfn);
        func = PCI_FUNC(device->devfn);

	/* XXX Need to change the Bus # */
        if (bus > 2)
                address = (bus << 16) | (dev << 11) | (func << 8) | (offset & 0xfc) |
                                0x80000000 | 0x1;
        else
                address = (dev << 11) | (func << 8) | (offset & 0xfc) | 0x80000000;

        address_reg = RM9000x2_OCD_HTCFGA;
        data_reg =  RM9000x2_OCD_HTCFGD;

        if ((offset & 0x3) == 0)
                offset = 0x2;
        else
                offset = 0x0;

        RM9K_WRITE(address_reg, address);
        RM9K_WRITE_16(data_reg + offset, val);

        return PCIBIOS_SUCCESSFUL;
}

static int titan_ht_config_write_byte(struct pci_dev *device,
                                             int offset, u8 val)
{
        int dev, bus, func;
        uint32_t address_reg, data_reg;
        uint32_t address;
        int offset1;

        bus = device->bus->number;
        dev = PCI_SLOT(device->devfn);
        func = PCI_FUNC(device->devfn);

	/* XXX Need to change the Bus # */
        if (bus > 2)
                address = (bus << 16) | (dev << 11) | (func << 8) | (offset & 0xfc) |
                                0x80000000 | 0x1;
        else
                address = (dev << 11) | (func << 8) | (offset & 0xfc) | 0x80000000;

        address_reg = RM9000x2_OCD_HTCFGA;
        data_reg =  RM9000x2_OCD_HTCFGD;

        RM9K_WRITE(address_reg, address);

        if ((offset & 0x3) == 0) {
             offset1 = 0x3;
        }
        if ((offset & 0x3) == 1) {
             offset1 = 0x2;
        }
        if ((offset & 0x3) == 2) {
             offset1 = 0x1;
        }
        if ((offset & 0x3) == 3) {
            offset1 = 0x0;
        }

        RM9K_WRITE_8(data_reg + offset1, val);
        return PCIBIOS_SUCCESSFUL;
}


static void titan_pcibios_set_master(struct pci_dev *dev)
{
        u16 cmd;
        int bus = dev->bus->number;

	if (check_titan_htlink())
            titan_ht_config_read_word(dev, PCI_COMMAND, &cmd);

	cmd |= PCI_COMMAND_MASTER;

	if (check_titan_htlink())
            titan_ht_config_write_word(dev, PCI_COMMAND, cmd);
}


int pcibios_enable_resources(struct pci_dev *dev)
{
        u16 cmd, old_cmd;
        u8 tmp1;
        int idx;
        struct resource *r;
        int bus = dev->bus->number;

	if (check_titan_htlink())
            titan_ht_config_read_word(dev, PCI_COMMAND, &cmd);

	old_cmd = cmd;
        for (idx = 0; idx < 6; idx++) {
                r = &dev->resource[idx];
                if (!r->start && r->end) {
                        printk(KERN_ERR
                               "PCI: Device %s not available because of "
                               "resource collisions\n", pci_name(dev));
                        return -EINVAL;
                }
                if (r->flags & IORESOURCE_IO)
                        cmd |= PCI_COMMAND_IO;
                if (r->flags & IORESOURCE_MEM)
                        cmd |= PCI_COMMAND_MEMORY;
        }
        if (cmd != old_cmd) {
		if (check_titan_htlink())
                   titan_ht_config_write_word(dev, PCI_COMMAND, cmd);
	}

	if (check_titan_htlink())
		titan_ht_config_read_byte(dev, PCI_CACHE_LINE_SIZE, &tmp1);

	if (tmp1 != 8) {
                printk(KERN_WARNING "PCI setting cache line size to 8 from "
                       "%d\n", tmp1);
	}

	if (check_titan_htlink())
		titan_ht_config_write_byte(dev, PCI_CACHE_LINE_SIZE, 8);

	if (check_titan_htlink())
		titan_ht_config_read_byte(dev, PCI_LATENCY_TIMER, &tmp1);

	if (tmp1 < 32 || tmp1 == 0xff) {
                printk(KERN_WARNING "PCI setting latency timer to 32 from %d\n",
                       tmp1);
	}

	if (check_titan_htlink())
		titan_ht_config_write_byte(dev, PCI_LATENCY_TIMER, 32);

	return 0;
}


int pcibios_enable_device(struct pci_dev *dev, int mask)
{
        return pcibios_enable_resources(dev);
}



void pcibios_update_resource(struct pci_dev *dev, struct resource *root,
                             struct resource *res, int resource)
{
        u32 new, check;
        int reg;

        return;

        new = res->start | (res->flags & PCI_REGION_FLAG_MASK);
        if (resource < 6) {
                reg = PCI_BASE_ADDRESS_0 + 4 * resource;
        } else if (resource == PCI_ROM_RESOURCE) {
		res->flags |= IORESOURCE_ROM_ENABLE;
                reg = dev->rom_base_reg;
        } else {
                /*
                 * Somebody might have asked allocation of a non-standard
                 * resource
                 */
                return;
        }

        pci_write_config_dword(dev, reg, new);
        pci_read_config_dword(dev, reg, &check);
        if ((new ^ check) &
            ((new & PCI_BASE_ADDRESS_SPACE_IO) ? PCI_BASE_ADDRESS_IO_MASK :
             PCI_BASE_ADDRESS_MEM_MASK)) {
                printk(KERN_ERR "PCI: Error while updating region "
                       "%s/%d (%08x != %08x)\n", pci_name(dev), resource,
                       new, check);
        }
}


void pcibios_align_resource(void *data, struct resource *res,
                            unsigned long size, unsigned long align)
{
        struct pci_dev *dev = data;

        if (res->flags & IORESOURCE_IO) {
                unsigned long start = res->start;

                /* We need to avoid collisions with `mirrored' VGA ports
                   and other strange ISA hardware, so we always want the
                   addresses kilobyte aligned.  */
                if (size > 0x100) {
                        printk(KERN_ERR "PCI: I/O Region %s/%d too large"
                               " (%ld bytes)\n", pci_name(dev),
                                dev->resource - res, size);
                }

                start = (start + 1024 - 1) & ~(1024 - 1);
                res->start = start;
        }
}

struct pci_ops titan_pci_ops = {
        titan_ht_config_read_byte,
        titan_ht_config_read_word,
        titan_ht_config_read_dword,
        titan_ht_config_write_byte,
        titan_ht_config_write_word,
        titan_ht_config_write_dword
};

void __init pcibios_fixup_bus(struct pci_bus *c)
{
        titan_ht_pcibios_fixup_bus(c);
}

void __init pcibios_init(void)
{

        /* Reset PCI I/O and PCI MEM values */
	/* XXX Need to add the proper values here */
        ioport_resource.start = 0xe0000000;
        ioport_resource.end   = 0xe0000000 + 0x20000000 - 1;
        iomem_resource.start  = 0xc0000000;
        iomem_resource.end    = 0xc0000000 + 0x20000000 - 1;

	/* XXX Need to add bus values */
        pci_scan_bus(2, &titan_pci_ops, NULL);
        pci_scan_bus(3, &titan_pci_ops, NULL);
}

/*
 * for parsing "pci=" kernel boot arguments.
 */
char *pcibios_setup(char *str)
{
        printk(KERN_INFO "rr: pcibios_setup\n");
        /* Nothing to do for now.  */

        return str;
}

unsigned __init int pcibios_assign_all_busses(void)
{
        /* We want to use the PCI bus detection done by PMON */
        return 0;
}

#endif /* CONFIG_HYPERTRANSPORT */
