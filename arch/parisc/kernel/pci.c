/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1997, 1998 Ralf Baechle
 * Copyright (C) 1999 SuSE GmbH
 * Copyright (C) 1999-2001 Hewlett-Packard Company
 * Copyright (C) 1999-2001 Grant Grundler
 */
#include <linux/eisa.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/types.h>

#include <asm/io.h>
#include <asm/superio.h>

#define DEBUG_RESOURCES 0
#define DEBUG_CONFIG 0

#if DEBUG_CONFIG
# define DBGC(x...)	printk(KERN_DEBUG x)
#else
# define DBGC(x...)
#endif


#if DEBUG_RESOURCES
#define DBG_RES(x...)	printk(KERN_DEBUG x)
#else
#define DBG_RES(x...)
#endif

/* To be used as: mdelay(pci_post_reset_delay);
 *
 * post_reset is the time the kernel should stall to prevent anyone from
 * accessing the PCI bus once #RESET is de-asserted. 
 * PCI spec somewhere says 1 second but with multi-PCI bus systems,
 * this makes the boot time much longer than necessary.
 * 20ms seems to work for all the HP PCI implementations to date.
 *
 * #define pci_post_reset_delay 50
 */

struct pci_port_ops *pci_port __read_mostly;
struct pci_bios_ops *pci_bios __read_mostly;

static int pci_hba_count __read_mostly;

/* parisc_pci_hba used by pci_port->in/out() ops to lookup bus data.  */
#define PCI_HBA_MAX 32
static struct pci_hba_data *parisc_pci_hba[PCI_HBA_MAX] __read_mostly;


/********************************************************************
**
** I/O port space support
**
*********************************************************************/

/* EISA port numbers and PCI port numbers share the same interface.  Some
 * machines have both EISA and PCI adapters installed.  Rather than turn
 * pci_port into an array, we reserve bus 0 for EISA and call the EISA
 * routines if the access is to a port on bus 0.  We don't want to fix
 * EISA and ISA drivers which assume port space is <= 0xffff.
 */

#ifdef CONFIG_EISA
#define EISA_IN(size) if (EISA_bus && (b == 0)) return eisa_in##size(addr)
#define EISA_OUT(size) if (EISA_bus && (b == 0)) return eisa_out##size(d, addr)
#else
#define EISA_IN(size)
#define EISA_OUT(size)
#endif

#define PCI_PORT_IN(type, size) \
u##size in##type (int addr) \
{ \
	int b = PCI_PORT_HBA(addr); \
	EISA_IN(size); \
	if (!parisc_pci_hba[b]) return (u##size) -1; \
	return pci_port->in##type(parisc_pci_hba[b], PCI_PORT_ADDR(addr)); \
} \
EXPORT_SYMBOL(in##type);

PCI_PORT_IN(b,  8)
PCI_PORT_IN(w, 16)
PCI_PORT_IN(l, 32)


#define PCI_PORT_OUT(type, size) \
void out##type (u##size d, int addr) \
{ \
	int b = PCI_PORT_HBA(addr); \
	EISA_OUT(size); \
	if (!parisc_pci_hba[b]) return; \
	pci_port->out##type(parisc_pci_hba[b], PCI_PORT_ADDR(addr), d); \
} \
EXPORT_SYMBOL(out##type);

PCI_PORT_OUT(b,  8)
PCI_PORT_OUT(w, 16)
PCI_PORT_OUT(l, 32)



/*
 * BIOS32 replacement.
 */
static int __init pcibios_init(void)
{
	if (!pci_bios)
		return -1;

	if (pci_bios->init) {
		pci_bios->init();
	} else {
		printk(KERN_WARNING "pci_bios != NULL but init() is!\n");
	}

	/* Set the CLS for PCI as early as possible. */
	pci_cache_line_size = pci_dfl_cache_line_size;

	return 0;
}


/* Called from pci_do_scan_bus() *after* walking a bus but before walking PPBs. */
void pcibios_fixup_bus(struct pci_bus *bus)
{
	if (pci_bios->fixup_bus) {
		pci_bios->fixup_bus(bus);
	} else {
		printk(KERN_WARNING "pci_bios != NULL but fixup_bus() is!\n");
	}
}


/*
 * Called by pci_set_master() - a driver interface.
 *
 * Legacy PDC guarantees to set:
 *	Map Memory BAR's into PA IO space.
 *	Map Expansion ROM BAR into one common PA IO space per bus.
 *	Map IO BAR's into PCI IO space.
 *	Command (see below)
 *	Cache Line Size
 *	Latency Timer
 *	Interrupt Line
 *	PPB: secondary latency timer, io/mmio base/limit,
 *		bus numbers, bridge control
 *
 */
void pcibios_set_master(struct pci_dev *dev)
{
	u8 lat;

	/* If someone already mucked with this, don't touch it. */
	pci_read_config_byte(dev, PCI_LATENCY_TIMER, &lat);
	if (lat >= 16) return;

	/*
	** HP generally has fewer devices on the bus than other architectures.
	** upper byte is PCI_LATENCY_TIMER.
	*/
	pci_write_config_word(dev, PCI_CACHE_LINE_SIZE,
			      (0x80 << 8) | pci_cache_line_size);
}


void __init pcibios_init_bus(struct pci_bus *bus)
{
	struct pci_dev *dev = bus->self;
	unsigned short bridge_ctl;

	/* We deal only with pci controllers and pci-pci bridges. */
	if (!dev || (dev->class >> 8) != PCI_CLASS_BRIDGE_PCI)
		return;

	/* PCI-PCI bridge - set the cache line and default latency
	   (32) for primary and secondary buses. */
	pci_write_config_byte(dev, PCI_SEC_LATENCY_TIMER, 32);

	pci_read_config_word(dev, PCI_BRIDGE_CONTROL, &bridge_ctl);
	bridge_ctl |= PCI_BRIDGE_CTL_PARITY | PCI_BRIDGE_CTL_SERR;
	pci_write_config_word(dev, PCI_BRIDGE_CONTROL, bridge_ctl);
}

/*
 * pcibios align resources() is called every time generic PCI code
 * wants to generate a new address. The process of looking for
 * an available address, each candidate is first "aligned" and
 * then checked if the resource is available until a match is found.
 *
 * Since we are just checking candidates, don't use any fields other
 * than res->start.
 */
resource_size_t pcibios_align_resource(void *data, const struct resource *res,
				resource_size_t size, resource_size_t alignment)
{
	resource_size_t mask, align, start = res->start;

	DBG_RES("pcibios_align_resource(%s, (%p) [%lx,%lx]/%x, 0x%lx, 0x%lx)\n",
		pci_name(((struct pci_dev *) data)),
		res->parent, res->start, res->end,
		(int) res->flags, size, alignment);

	/* If it's not IO, then it's gotta be MEM */
	align = (res->flags & IORESOURCE_IO) ? PCIBIOS_MIN_IO : PCIBIOS_MIN_MEM;

	/* Align to largest of MIN or input size */
	mask = max(alignment, align) - 1;
	start += mask;
	start &= ~mask;

	return start;
}


/*
 * A driver is enabling the device.  We make sure that all the appropriate
 * bits are set to allow the device to operate as the driver is expecting.
 * We enable the port IO and memory IO bits if the device has any BARs of
 * that type, and we enable the PERR and SERR bits unconditionally.
 * Drivers that do not need parity (eg graphics and possibly networking)
 * can clear these bits if they want.
 */
int pcibios_enable_device(struct pci_dev *dev, int mask)
{
	int err;
	u16 cmd, old_cmd;

	err = pci_enable_resources(dev, mask);
	if (err < 0)
		return err;

	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	old_cmd = cmd;

	cmd |= (PCI_COMMAND_SERR | PCI_COMMAND_PARITY);

#if 0
	/* If bridge/bus controller has FBB enabled, child must too. */
	if (dev->bus->bridge_ctl & PCI_BRIDGE_CTL_FAST_BACK)
		cmd |= PCI_COMMAND_FAST_BACK;
#endif

	if (cmd != old_cmd) {
		dev_info(&dev->dev, "enabling SERR and PARITY (%04x -> %04x)\n",
			old_cmd, cmd);
		pci_write_config_word(dev, PCI_COMMAND, cmd);
	}
	return 0;
}


/* PA-RISC specific */
void pcibios_register_hba(struct pci_hba_data *hba)
{
	if (pci_hba_count >= PCI_HBA_MAX) {
		printk(KERN_ERR "PCI: Too many Host Bus Adapters\n");
		return;
	}

	parisc_pci_hba[pci_hba_count] = hba;
	hba->hba_num = pci_hba_count++;
}

subsys_initcall(pcibios_init);
