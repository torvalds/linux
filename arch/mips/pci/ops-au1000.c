/*
 * BRIEF MODULE DESCRIPTION
 *	Alchemy/AMD Au1x00 pci support.
 *
 * Copyright 2001,2002,2003 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *         	ppopov@mvista.com or source@mvista.com
 *
 *  Support for all devices (greater than 16) added by David Gathright.
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
#include <linux/vmalloc.h>

#include <asm/mach-au1x00/au1000.h>

#undef DEBUG
#ifdef DEBUG
#define DBG(x...) printk(x)
#else
#define DBG(x...)
#endif

#define PCI_ACCESS_READ  0
#define PCI_ACCESS_WRITE 1


int (*board_pci_idsel)(unsigned int devsel, int assert);

void mod_wired_entry(int entry, unsigned long entrylo0,
		unsigned long entrylo1, unsigned long entryhi,
		unsigned long pagemask)
{
	unsigned long old_pagemask;
	unsigned long old_ctx;

	/* Save old context and create impossible VPN2 value */
	old_ctx = read_c0_entryhi() & 0xff;
	old_pagemask = read_c0_pagemask();
	write_c0_index(entry);
	write_c0_pagemask(pagemask);
	write_c0_entryhi(entryhi);
	write_c0_entrylo0(entrylo0);
	write_c0_entrylo1(entrylo1);
	tlb_write_indexed();
	write_c0_entryhi(old_ctx);
	write_c0_pagemask(old_pagemask);
}

struct vm_struct *pci_cfg_vm;
static int pci_cfg_wired_entry;
static int first_cfg = 1;
unsigned long last_entryLo0, last_entryLo1;

static int config_access(unsigned char access_type, struct pci_bus *bus,
			 unsigned int dev_fn, unsigned char where,
			 u32 * data)
{
#if defined( CONFIG_SOC_AU1500 ) || defined( CONFIG_SOC_AU1550 )
	unsigned int device = PCI_SLOT(dev_fn);
	unsigned int function = PCI_FUNC(dev_fn);
	unsigned long offset, status;
	unsigned long cfg_base;
	unsigned long flags;
	int error = PCIBIOS_SUCCESSFUL;
	unsigned long entryLo0, entryLo1;

	if (device > 19) {
		*data = 0xffffffff;
		return -1;
	}

	local_irq_save(flags);
	au_writel(((0x2000 << 16) | (au_readl(Au1500_PCI_STATCMD) & 0xffff)),
			Au1500_PCI_STATCMD);
	au_sync_udelay(1);

	/*
	 * We can't ioremap the entire pci config space because it's
	 * too large. Nor can we call ioremap dynamically because some
	 * device drivers use the pci config routines from within
	 * interrupt handlers and that becomes a problem in get_vm_area().
	 * We use one wired tlb to handle all config accesses for all
	 * busses. To improve performance, if the current device
	 * is the same as the last device accessed, we don't touch the
	 * tlb.
	 */
	if (first_cfg) {
		/* reserve a wired entry for pci config accesses */
		first_cfg = 0;
		pci_cfg_vm = get_vm_area(0x2000, VM_IOREMAP);
		if (!pci_cfg_vm)
			panic (KERN_ERR "PCI unable to get vm area\n");
		pci_cfg_wired_entry = read_c0_wired();
		add_wired_entry(0, 0, (unsigned long)pci_cfg_vm->addr, PM_4K);
		last_entryLo0  = last_entryLo1 = 0xffffffff;
	}

	/* Allow board vendors to implement their own off-chip idsel.
	 * If it doesn't succeed, may as well bail out at this point.
	 */
	if (board_pci_idsel) {
		if (board_pci_idsel(device, 1) == 0) {
			*data = 0xffffffff;
			local_irq_restore(flags);
			return -1;
		}
	}

        /* setup the config window */
        if (bus->number == 0) {
                cfg_base = ((1<<device)<<11);
        } else {
                cfg_base = 0x80000000 | (bus->number<<16) | (device<<11);
        }

        /* setup the lower bits of the 36 bit address */
        offset = (function << 8) | (where & ~0x3);
	/* pick up any address that falls below the page mask */
	offset |= cfg_base & ~PAGE_MASK;

	/* page boundary */
	cfg_base = cfg_base & PAGE_MASK;

	entryLo0 = (6 << 26)  | (cfg_base >> 6) | (2 << 3) | 7;
	entryLo1 = (6 << 26)  | (cfg_base >> 6) | (0x1000 >> 6) | (2 << 3) | 7;

	if ((entryLo0 != last_entryLo0) || (entryLo1 != last_entryLo1)) {
		mod_wired_entry(pci_cfg_wired_entry, entryLo0, entryLo1,
				(unsigned long)pci_cfg_vm->addr, PM_4K);
		last_entryLo0 = entryLo0;
		last_entryLo1 = entryLo1;
	}

	if (access_type == PCI_ACCESS_WRITE) {
		au_writel(*data, (int)(pci_cfg_vm->addr + offset));
	} else {
		*data = au_readl((int)(pci_cfg_vm->addr + offset));
	}
	au_sync_udelay(2);

	DBG("cfg_access %d bus->number %d dev %d at %x *data %x conf %x\n",
			access_type, bus->number, device, where, *data, offset);

	/* check master abort */
	status = au_readl(Au1500_PCI_STATCMD);

	if (status & (1<<29)) {
		*data = 0xffffffff;
		error = -1;
		DBG("Au1x Master Abort\n");
	} else if ((status >> 28) & 0xf) {
		DBG("PCI ERR detected: device %d, status %x\n", device, ((status >> 28) & 0xf));

		/* clear errors */
		au_writel(status & 0xf000ffff, Au1500_PCI_STATCMD);

		*data = 0xffffffff;
		error = -1;
	}

	/* Take away the idsel.
	*/
	if (board_pci_idsel) {
		(void)board_pci_idsel(device, 0);
	}

	local_irq_restore(flags);
	return error;
#endif
}

static int read_config_byte(struct pci_bus *bus, unsigned int devfn,
			    int where, u8 * val)
{
	u32 data;
	int ret;

	ret = config_access(PCI_ACCESS_READ, bus, devfn, where, &data);
	if (where & 1)
		data >>= 8;
	if (where & 2)
		data >>= 16;
	*val = data & 0xff;
	return ret;
}


static int read_config_word(struct pci_bus *bus, unsigned int devfn,
			    int where, u16 * val)
{
	u32 data;
	int ret;

	ret = config_access(PCI_ACCESS_READ, bus, devfn, where, &data);
	if (where & 2)
		data >>= 16;
	*val = data & 0xffff;
	return ret;
}

static int read_config_dword(struct pci_bus *bus, unsigned int devfn,
			     int where, u32 * val)
{
	int ret;

	ret = config_access(PCI_ACCESS_READ, bus, devfn, where, val);
	return ret;
}

static int
write_config_byte(struct pci_bus *bus, unsigned int devfn, int where,
		  u8 val)
{
	u32 data = 0;

	if (config_access(PCI_ACCESS_READ, bus, devfn, where, &data))
		return -1;

	data = (data & ~(0xff << ((where & 3) << 3))) |
	    (val << ((where & 3) << 3));

	if (config_access(PCI_ACCESS_WRITE, bus, devfn, where, &data))
		return -1;

	return PCIBIOS_SUCCESSFUL;
}

static int
write_config_word(struct pci_bus *bus, unsigned int devfn, int where,
		  u16 val)
{
	u32 data = 0;

	if (config_access(PCI_ACCESS_READ, bus, devfn, where, &data))
		return -1;

	data = (data & ~(0xffff << ((where & 3) << 3))) |
	    (val << ((where & 3) << 3));

	if (config_access(PCI_ACCESS_WRITE, bus, devfn, where, &data))
		return -1;


	return PCIBIOS_SUCCESSFUL;
}

static int
write_config_dword(struct pci_bus *bus, unsigned int devfn, int where,
		   u32 val)
{
	if (config_access(PCI_ACCESS_WRITE, bus, devfn, where, &val))
		return -1;

	return PCIBIOS_SUCCESSFUL;
}

static int config_read(struct pci_bus *bus, unsigned int devfn,
		       int where, int size, u32 * val)
{
	switch (size) {
	case 1: {
			u8 _val;
			int rc = read_config_byte(bus, devfn, where, &_val);
			*val = _val;
			return rc;
		}
       case 2: {
			u16 _val;
			int rc = read_config_word(bus, devfn, where, &_val);
			*val = _val;
			return rc;
		}
	default:
		return read_config_dword(bus, devfn, where, val);
	}
}

static int config_write(struct pci_bus *bus, unsigned int devfn,
			int where, int size, u32 val)
{
	switch (size) {
	case 1:
		return write_config_byte(bus, devfn, where, (u8) val);
	case 2:
		return write_config_word(bus, devfn, where, (u16) val);
	default:
		return write_config_dword(bus, devfn, where, val);
	}
}


struct pci_ops au1x_pci_ops = {
	config_read,
	config_write
};
