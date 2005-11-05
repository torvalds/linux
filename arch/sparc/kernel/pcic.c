/*
 * pcic.c: MicroSPARC-IIep PCI controller support
 *
 * Copyright (C) 1998 V. Roganov and G. Raiko
 *
 * Code is derived from Ultra/PCI PSYCHO controller support, see that
 * for author info.
 *
 * Support for diverse IIep based platforms by Pete Zaitcev.
 * CP-1200 by Eric Brower.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/jiffies.h>

#include <asm/ebus.h>
#include <asm/sbus.h> /* for sanity check... */
#include <asm/swift.h> /* for cache flushing. */
#include <asm/io.h>

#include <linux/ctype.h>
#include <linux/pci.h>
#include <linux/time.h>
#include <linux/timex.h>
#include <linux/interrupt.h>

#include <asm/irq.h>
#include <asm/oplib.h>
#include <asm/pcic.h>
#include <asm/timer.h>
#include <asm/uaccess.h>


unsigned int pcic_pin_to_irq(unsigned int pin, char *name);

/*
 * I studied different documents and many live PROMs both from 2.30
 * family and 3.xx versions. I came to the amazing conclusion: there is
 * absolutely no way to route interrupts in IIep systems relying on
 * information which PROM presents. We must hardcode interrupt routing
 * schematics. And this actually sucks.   -- zaitcev 1999/05/12
 *
 * To find irq for a device we determine which routing map
 * is in effect or, in other words, on which machine we are running.
 * We use PROM name for this although other techniques may be used
 * in special cases (Gleb reports a PROMless IIep based system).
 * Once we know the map we take device configuration address and
 * find PCIC pin number where INT line goes. Then we may either program
 * preferred irq into the PCIC or supply the preexisting irq to the device.
 */
struct pcic_ca2irq {
	unsigned char busno;		/* PCI bus number */
	unsigned char devfn;		/* Configuration address */
	unsigned char pin;		/* PCIC external interrupt pin */
	unsigned char irq;		/* Preferred IRQ (mappable in PCIC) */
	unsigned int force;		/* Enforce preferred IRQ */
};

struct pcic_sn2list {
	char *sysname;
	struct pcic_ca2irq *intmap;
	int mapdim;
};

/*
 * JavaEngine-1 apparently has different versions.
 *
 * According to communications with Sun folks, for P2 build 501-4628-03:
 * pin 0 - parallel, audio;
 * pin 1 - Ethernet;
 * pin 2 - su;
 * pin 3 - PS/2 kbd and mouse.
 *
 * OEM manual (805-1486):
 * pin 0: Ethernet
 * pin 1: All EBus
 * pin 2: IGA (unused)
 * pin 3: Not connected
 * OEM manual says that 501-4628 & 501-4811 are the same thing,
 * only the latter has NAND flash in place.
 *
 * So far unofficial Sun wins over the OEM manual. Poor OEMs...
 */
static struct pcic_ca2irq pcic_i_je1a[] = {	/* 501-4811-03 */
	{ 0, 0x00, 2, 12, 0 },		/* EBus: hogs all */
	{ 0, 0x01, 1,  6, 1 },		/* Happy Meal */
	{ 0, 0x80, 0,  7, 0 },		/* IGA (unused) */
};

/* XXX JS-E entry is incomplete - PCI Slot 2 address (pin 7)? */
static struct pcic_ca2irq pcic_i_jse[] = {
	{ 0, 0x00, 0, 13, 0 },		/* Ebus - serial and keyboard */
	{ 0, 0x01, 1,  6, 0 },		/* hme */
	{ 0, 0x08, 2,  9, 0 },		/* VGA - we hope not used :) */
	{ 0, 0x10, 6,  8, 0 },		/* PCI INTA# in Slot 1 */
	{ 0, 0x18, 7, 12, 0 },		/* PCI INTA# in Slot 2, shared w. RTC */
	{ 0, 0x38, 4,  9, 0 },		/* All ISA devices. Read 8259. */
	{ 0, 0x80, 5, 11, 0 },		/* EIDE */
	/* {0,0x88, 0,0,0} - unknown device... PMU? Probably no interrupt. */
	{ 0, 0xA0, 4,  9, 0 },		/* USB */
	/*
	 * Some pins belong to non-PCI devices, we hardcode them in drivers.
	 * sun4m timers - irq 10, 14
	 * PC style RTC - pin 7, irq 4 ?
	 * Smart card, Parallel - pin 4 shared with USB, ISA
	 * audio - pin 3, irq 5 ?
	 */
};

/* SPARCengine-6 was the original release name of CP1200.
 * The documentation differs between the two versions
 */
static struct pcic_ca2irq pcic_i_se6[] = {
	{ 0, 0x08, 0,  2, 0 },		/* SCSI	*/
	{ 0, 0x01, 1,  6, 0 },		/* HME	*/
	{ 0, 0x00, 3, 13, 0 },		/* EBus	*/
};

/*
 * Krups (courtesy of Varol Kaptan)
 * No documentation available, but it was easy to guess
 * because it was very similar to Espresso.
 *  
 * pin 0 - kbd, mouse, serial;
 * pin 1 - Ethernet;
 * pin 2 - igs (we do not use it);
 * pin 3 - audio;
 * pin 4,5,6 - unused;
 * pin 7 - RTC (from P2 onwards as David B. says).
 */
static struct pcic_ca2irq pcic_i_jk[] = {
	{ 0, 0x00, 0, 13, 0 },		/* Ebus - serial and keyboard */
	{ 0, 0x01, 1,  6, 0 },		/* hme */
};

/*
 * Several entries in this list may point to the same routing map
 * as several PROMs may be installed on the same physical board.
 */
#define SN2L_INIT(name, map)	\
  { name, map, sizeof(map)/sizeof(struct pcic_ca2irq) }

static struct pcic_sn2list pcic_known_sysnames[] = {
	SN2L_INIT("SUNW,JavaEngine1", pcic_i_je1a),	/* JE1, PROM 2.32 */
	SN2L_INIT("SUNW,JS-E", pcic_i_jse),	/* PROLL JavaStation-E */
	SN2L_INIT("SUNW,SPARCengine-6", pcic_i_se6), /* SPARCengine-6/CP-1200 */
	SN2L_INIT("SUNW,JS-NC", pcic_i_jk),	/* PROLL JavaStation-NC */
	SN2L_INIT("SUNW,JSIIep", pcic_i_jk),	/* OBP JavaStation-NC */
	{ NULL, NULL, 0 }
};

/*
 * Only one PCIC per IIep,
 * and since we have no SMP IIep, only one per system.
 */
static int pcic0_up;
static struct linux_pcic pcic0;

void * __iomem pcic_regs;
volatile int pcic_speculative;
volatile int pcic_trapped;

static void pci_do_gettimeofday(struct timeval *tv);
static int pci_do_settimeofday(struct timespec *tv);

#define CONFIG_CMD(bus, device_fn, where) (0x80000000 | (((unsigned int)bus) << 16) | (((unsigned int)device_fn) << 8) | (where & ~3))

static int pcic_read_config_dword(unsigned int busno, unsigned int devfn,
    int where, u32 *value)
{
	struct linux_pcic *pcic;
	unsigned long flags;

	pcic = &pcic0;

	local_irq_save(flags);
#if 0 /* does not fail here */
	pcic_speculative = 1;
	pcic_trapped = 0;
#endif
	writel(CONFIG_CMD(busno, devfn, where), pcic->pcic_config_space_addr);
#if 0 /* does not fail here */
	nop();
	if (pcic_trapped) {
		local_irq_restore(flags);
		*value = ~0;
		return 0;
	}
#endif
	pcic_speculative = 2;
	pcic_trapped = 0;
	*value = readl(pcic->pcic_config_space_data + (where&4));
	nop();
	if (pcic_trapped) {
		pcic_speculative = 0;
		local_irq_restore(flags);
		*value = ~0;
		return 0;
	}
	pcic_speculative = 0;
	local_irq_restore(flags);
	return 0;
}

static int pcic_read_config(struct pci_bus *bus, unsigned int devfn,
   int where, int size, u32 *val)
{
	unsigned int v;

	if (bus->number != 0) return -EINVAL;
	switch (size) {
	case 1:
		pcic_read_config_dword(bus->number, devfn, where&~3, &v);
		*val = 0xff & (v >> (8*(where & 3)));
		return 0;
	case 2:
		if (where&1) return -EINVAL;
		pcic_read_config_dword(bus->number, devfn, where&~3, &v);
		*val = 0xffff & (v >> (8*(where & 3)));
		return 0;
	case 4:
		if (where&3) return -EINVAL;
		pcic_read_config_dword(bus->number, devfn, where&~3, val);
		return 0;
	}
	return -EINVAL;
}

static int pcic_write_config_dword(unsigned int busno, unsigned int devfn,
    int where, u32 value)
{
	struct linux_pcic *pcic;
	unsigned long flags;

	pcic = &pcic0;

	local_irq_save(flags);
	writel(CONFIG_CMD(busno, devfn, where), pcic->pcic_config_space_addr);
	writel(value, pcic->pcic_config_space_data + (where&4));
	local_irq_restore(flags);
	return 0;
}

static int pcic_write_config(struct pci_bus *bus, unsigned int devfn,
   int where, int size, u32 val)
{
	unsigned int v;

	if (bus->number != 0) return -EINVAL;
	switch (size) {
	case 1:
		pcic_read_config_dword(bus->number, devfn, where&~3, &v);
		v = (v & ~(0xff << (8*(where&3)))) |
		    ((0xff&val) << (8*(where&3)));
		return pcic_write_config_dword(bus->number, devfn, where&~3, v);
	case 2:
		if (where&1) return -EINVAL;
		pcic_read_config_dword(bus->number, devfn, where&~3, &v);
		v = (v & ~(0xffff << (8*(where&3)))) |
		    ((0xffff&val) << (8*(where&3)));
		return pcic_write_config_dword(bus->number, devfn, where&~3, v);
	case 4:
		if (where&3) return -EINVAL;
		return pcic_write_config_dword(bus->number, devfn, where, val);
	}
	return -EINVAL;
}

static struct pci_ops pcic_ops = {
	.read =		pcic_read_config,
	.write =	pcic_write_config,
};

/*
 * On sparc64 pcibios_init() calls pci_controller_probe().
 * We want PCIC probed little ahead so that interrupt controller
 * would be operational.
 */
int __init pcic_probe(void)
{
	struct linux_pcic *pcic;
	struct linux_prom_registers regs[PROMREG_MAX];
	struct linux_pbm_info* pbm;
	char namebuf[64];
	int node;
	int err;

	if (pcic0_up) {
		prom_printf("PCIC: called twice!\n");
		prom_halt();
	}
	pcic = &pcic0;

	node = prom_getchild (prom_root_node);
	node = prom_searchsiblings (node, "pci");
	if (node == 0)
		return -ENODEV;
	/*
	 * Map in PCIC register set, config space, and IO base
	 */
	err = prom_getproperty(node, "reg", (char*)regs, sizeof(regs));
	if (err == 0 || err == -1) {
		prom_printf("PCIC: Error, cannot get PCIC registers "
			    "from PROM.\n");
		prom_halt();
	}

	pcic0_up = 1;

	pcic->pcic_res_regs.name = "pcic_registers";
	pcic->pcic_regs = ioremap(regs[0].phys_addr, regs[0].reg_size);
	if (!pcic->pcic_regs) {
		prom_printf("PCIC: Error, cannot map PCIC registers.\n");
		prom_halt();
	}

	pcic->pcic_res_io.name = "pcic_io";
	if ((pcic->pcic_io = (unsigned long)
	    ioremap(regs[1].phys_addr, 0x10000)) == 0) {
		prom_printf("PCIC: Error, cannot map PCIC IO Base.\n");
		prom_halt();
	}

	pcic->pcic_res_cfg_addr.name = "pcic_cfg_addr";
	if ((pcic->pcic_config_space_addr =
	    ioremap(regs[2].phys_addr, regs[2].reg_size * 2)) == 0) {
		prom_printf("PCIC: Error, cannot map" 
			    "PCI Configuration Space Address.\n");
		prom_halt();
	}

	/*
	 * Docs say three least significant bits in address and data
	 * must be the same. Thus, we need adjust size of data.
	 */
	pcic->pcic_res_cfg_data.name = "pcic_cfg_data";
	if ((pcic->pcic_config_space_data =
	    ioremap(regs[3].phys_addr, regs[3].reg_size * 2)) == 0) {
		prom_printf("PCIC: Error, cannot map" 
			    "PCI Configuration Space Data.\n");
		prom_halt();
	}

	pbm = &pcic->pbm;
	pbm->prom_node = node;
	prom_getstring(node, "name", namebuf, 63);  namebuf[63] = 0;
	strcpy(pbm->prom_name, namebuf);

	{
		extern volatile int t_nmi[1];
		extern int pcic_nmi_trap_patch[1];

		t_nmi[0] = pcic_nmi_trap_patch[0];
		t_nmi[1] = pcic_nmi_trap_patch[1];
		t_nmi[2] = pcic_nmi_trap_patch[2];
		t_nmi[3] = pcic_nmi_trap_patch[3];
		swift_flush_dcache();
		pcic_regs = pcic->pcic_regs;
	}

	prom_getstring(prom_root_node, "name", namebuf, 63);  namebuf[63] = 0;
	{
		struct pcic_sn2list *p;

		for (p = pcic_known_sysnames; p->sysname != NULL; p++) {
			if (strcmp(namebuf, p->sysname) == 0)
				break;
		}
		pcic->pcic_imap = p->intmap;
		pcic->pcic_imdim = p->mapdim;
	}
	if (pcic->pcic_imap == NULL) {
		/*
		 * We do not panic here for the sake of embedded systems.
		 */
		printk("PCIC: System %s is unknown, cannot route interrupts\n",
		    namebuf);
	}

	return 0;
}

static void __init pcic_pbm_scan_bus(struct linux_pcic *pcic)
{
	struct linux_pbm_info *pbm = &pcic->pbm;

	pbm->pci_bus = pci_scan_bus(pbm->pci_first_busno, &pcic_ops, pbm);
#if 0 /* deadwood transplanted from sparc64 */
	pci_fill_in_pbm_cookies(pbm->pci_bus, pbm, pbm->prom_node);
	pci_record_assignments(pbm, pbm->pci_bus);
	pci_assign_unassigned(pbm, pbm->pci_bus);
	pci_fixup_irq(pbm, pbm->pci_bus);
#endif
}

/*
 * Main entry point from the PCI subsystem.
 */
static int __init pcic_init(void)
{
	struct linux_pcic *pcic;

	/*
	 * PCIC should be initialized at start of the timer.
	 * So, here we report the presence of PCIC and do some magic passes.
	 */
	if(!pcic0_up)
		return 0;
	pcic = &pcic0;

	/*
	 *      Switch off IOTLB translation.
	 */
	writeb(PCI_DVMA_CONTROL_IOTLB_DISABLE, 
	       pcic->pcic_regs+PCI_DVMA_CONTROL);

	/*
	 *      Increase mapped size for PCI memory space (DMA access).
	 *      Should be done in that order (size first, address second).
	 *      Why we couldn't set up 4GB and forget about it? XXX
	 */
	writel(0xF0000000UL, pcic->pcic_regs+PCI_SIZE_0);
	writel(0+PCI_BASE_ADDRESS_SPACE_MEMORY, 
	       pcic->pcic_regs+PCI_BASE_ADDRESS_0);

	pcic_pbm_scan_bus(pcic);

	ebus_init();
	return 0;
}

int pcic_present(void)
{
	return pcic0_up;
}

static int __init pdev_to_pnode(struct linux_pbm_info *pbm, 
				    struct pci_dev *pdev)
{
	struct linux_prom_pci_registers regs[PROMREG_MAX];
	int err;
	int node = prom_getchild(pbm->prom_node);

	while(node) {
		err = prom_getproperty(node, "reg", 
				       (char *)&regs[0], sizeof(regs));
		if(err != 0 && err != -1) {
			unsigned long devfn = (regs[0].which_io >> 8) & 0xff;
			if(devfn == pdev->devfn)
				return node;
		}
		node = prom_getsibling(node);
	}
	return 0;
}

static inline struct pcidev_cookie *pci_devcookie_alloc(void)
{
	return kmalloc(sizeof(struct pcidev_cookie), GFP_ATOMIC);
}

static void pcic_map_pci_device(struct linux_pcic *pcic,
    struct pci_dev *dev, int node)
{
	char namebuf[64];
	unsigned long address;
	unsigned long flags;
	int j;

	if (node == 0 || node == -1) {
		strcpy(namebuf, "???");
	} else {
		prom_getstring(node, "name", namebuf, 63); namebuf[63] = 0;
	}

	for (j = 0; j < 6; j++) {
		address = dev->resource[j].start;
		if (address == 0) break;	/* are sequential */
		flags = dev->resource[j].flags;
		if ((flags & IORESOURCE_IO) != 0) {
			if (address < 0x10000) {
				/*
				 * A device responds to I/O cycles on PCI.
				 * We generate these cycles with memory
				 * access into the fixed map (phys 0x30000000).
				 *
				 * Since a device driver does not want to
				 * do ioremap() before accessing PC-style I/O,
				 * we supply virtual, ready to access address.
				 *
				 * Ebus devices do not come here even if
				 * CheerIO makes a similar conversion.
				 * See ebus.c for details.
				 *
				 * Note that request_region()
				 * works for these devices.
				 *
				 * XXX Neat trick, but it's a *bad* idea
				 * to shit into regions like that.
				 * What if we want to allocate one more
				 * PCI base address...
				 */
				dev->resource[j].start =
				    pcic->pcic_io + address;
				dev->resource[j].end = 1;  /* XXX */
				dev->resource[j].flags =
				    (flags & ~IORESOURCE_IO) | IORESOURCE_MEM;
			} else {
				/*
				 * OOPS... PCI Spec allows this. Sun does
				 * not have any devices getting above 64K
				 * so it must be user with a weird I/O
				 * board in a PCI slot. We must remap it
				 * under 64K but it is not done yet. XXX
				 */
				printk("PCIC: Skipping I/O space at 0x%lx,"
				    "this will Oops if a driver attaches;"
				    "device '%s' at %02x:%02x)\n", address,
				    namebuf, dev->bus->number, dev->devfn);
			}
		}
	}
}

static void
pcic_fill_irq(struct linux_pcic *pcic, struct pci_dev *dev, int node)
{
	struct pcic_ca2irq *p;
	int i, ivec;
	char namebuf[64];

	if (node == 0 || node == -1) {
		strcpy(namebuf, "???");
	} else {
		prom_getstring(node, "name", namebuf, sizeof(namebuf));
	}

	if ((p = pcic->pcic_imap) == 0) {
		dev->irq = 0;
		return;
	}
	for (i = 0; i < pcic->pcic_imdim; i++) {
		if (p->busno == dev->bus->number && p->devfn == dev->devfn)
			break;
		p++;
	}
	if (i >= pcic->pcic_imdim) {
		printk("PCIC: device %s devfn %02x:%02x not found in %d\n",
		    namebuf, dev->bus->number, dev->devfn, pcic->pcic_imdim);
		dev->irq = 0;
		return;
	}

	i = p->pin;
	if (i >= 0 && i < 4) {
		ivec = readw(pcic->pcic_regs+PCI_INT_SELECT_LO);
		dev->irq = ivec >> (i << 2) & 0xF;
	} else if (i >= 4 && i < 8) {
		ivec = readw(pcic->pcic_regs+PCI_INT_SELECT_HI);
		dev->irq = ivec >> ((i-4) << 2) & 0xF;
	} else {					/* Corrupted map */
		printk("PCIC: BAD PIN %d\n", i); for (;;) {}
	}
/* P3 */ /* printk("PCIC: device %s pin %d ivec 0x%x irq %x\n", namebuf, i, ivec, dev->irq); */

	/*
	 * dev->irq=0 means PROM did not bother to program the upper
	 * half of PCIC. This happens on JS-E with PROM 3.11, for instance.
	 */
	if (dev->irq == 0 || p->force) {
		if (p->irq == 0 || p->irq >= 15) {	/* Corrupted map */
			printk("PCIC: BAD IRQ %d\n", p->irq); for (;;) {}
		}
		printk("PCIC: setting irq %d at pin %d for device %02x:%02x\n",
		    p->irq, p->pin, dev->bus->number, dev->devfn);
		dev->irq = p->irq;

		i = p->pin;
		if (i >= 4) {
			ivec = readw(pcic->pcic_regs+PCI_INT_SELECT_HI);
			ivec &= ~(0xF << ((i - 4) << 2));
			ivec |= p->irq << ((i - 4) << 2);
			writew(ivec, pcic->pcic_regs+PCI_INT_SELECT_HI);
		} else {
			ivec = readw(pcic->pcic_regs+PCI_INT_SELECT_LO);
			ivec &= ~(0xF << (i << 2));
			ivec |= p->irq << (i << 2);
			writew(ivec, pcic->pcic_regs+PCI_INT_SELECT_LO);
		}
 	}

	return;
}

/*
 * Normally called from {do_}pci_scan_bus...
 */
void __init pcibios_fixup_bus(struct pci_bus *bus)
{
	struct pci_dev *dev;
	int i, has_io, has_mem;
	unsigned int cmd;
	struct linux_pcic *pcic;
	/* struct linux_pbm_info* pbm = &pcic->pbm; */
	int node;
	struct pcidev_cookie *pcp;

	if (!pcic0_up) {
		printk("pcibios_fixup_bus: no PCIC\n");
		return;
	}
	pcic = &pcic0;

	/*
	 * Next crud is an equivalent of pbm = pcic_bus_to_pbm(bus);
	 */
	if (bus->number != 0) {
		printk("pcibios_fixup_bus: nonzero bus 0x%x\n", bus->number);
		return;
	}

	list_for_each_entry(dev, &bus->devices, bus_list) {

		/*
		 * Comment from i386 branch:
		 *     There are buggy BIOSes that forget to enable I/O and memory
		 *     access to PCI devices. We try to fix this, but we need to
		 *     be sure that the BIOS didn't forget to assign an address
		 *     to the device. [mj]
		 * OBP is a case of such BIOS :-)
		 */
		has_io = has_mem = 0;
		for(i=0; i<6; i++) {
			unsigned long f = dev->resource[i].flags;
			if (f & IORESOURCE_IO) {
				has_io = 1;
			} else if (f & IORESOURCE_MEM)
				has_mem = 1;
		}
		pcic_read_config(dev->bus, dev->devfn, PCI_COMMAND, 2, &cmd);
		if (has_io && !(cmd & PCI_COMMAND_IO)) {
			printk("PCIC: Enabling I/O for device %02x:%02x\n",
				dev->bus->number, dev->devfn);
			cmd |= PCI_COMMAND_IO;
			pcic_write_config(dev->bus, dev->devfn,
			    PCI_COMMAND, 2, cmd);
		}
		if (has_mem && !(cmd & PCI_COMMAND_MEMORY)) {
			printk("PCIC: Enabling memory for device %02x:%02x\n",
				dev->bus->number, dev->devfn);
			cmd |= PCI_COMMAND_MEMORY;
			pcic_write_config(dev->bus, dev->devfn,
			    PCI_COMMAND, 2, cmd);
		}

		node = pdev_to_pnode(&pcic->pbm, dev);
		if(node == 0)
			node = -1;

		/* cookies */
		pcp = pci_devcookie_alloc();
		pcp->pbm = &pcic->pbm;
		pcp->prom_node = node;
		dev->sysdata = pcp;

		/* fixing I/O to look like memory */
		if ((dev->class>>16) != PCI_BASE_CLASS_BRIDGE)
			pcic_map_pci_device(pcic, dev, node);

		pcic_fill_irq(pcic, dev, node);
	}
}

/*
 * pcic_pin_to_irq() is exported to ebus.c.
 */
unsigned int
pcic_pin_to_irq(unsigned int pin, char *name)
{
	struct linux_pcic *pcic = &pcic0;
	unsigned int irq;
	unsigned int ivec;

	if (pin < 4) {
		ivec = readw(pcic->pcic_regs+PCI_INT_SELECT_LO);
		irq = ivec >> (pin << 2) & 0xF;
	} else if (pin < 8) {
		ivec = readw(pcic->pcic_regs+PCI_INT_SELECT_HI);
		irq = ivec >> ((pin-4) << 2) & 0xF;
	} else {					/* Corrupted map */
		printk("PCIC: BAD PIN %d FOR %s\n", pin, name);
		for (;;) {}	/* XXX Cannot panic properly in case of PROLL */
	}
/* P3 */ /* printk("PCIC: dev %s pin %d ivec 0x%x irq %x\n", name, pin, ivec, irq); */
	return irq;
}

/* Makes compiler happy */
static volatile int pcic_timer_dummy;

static void pcic_clear_clock_irq(void)
{
	pcic_timer_dummy = readl(pcic0.pcic_regs+PCI_SYS_LIMIT);
}

static irqreturn_t pcic_timer_handler (int irq, void *h, struct pt_regs *regs)
{
	write_seqlock(&xtime_lock);	/* Dummy, to show that we remember */
	pcic_clear_clock_irq();
	do_timer(regs);
#ifndef CONFIG_SMP
	update_process_times(user_mode(regs));
#endif
	write_sequnlock(&xtime_lock);
	return IRQ_HANDLED;
}

#define USECS_PER_JIFFY  10000  /* We have 100HZ "standard" timer for sparc */
#define TICK_TIMER_LIMIT ((100*1000000/4)/100)

void __init pci_time_init(void)
{
	struct linux_pcic *pcic = &pcic0;
	unsigned long v;
	int timer_irq, irq;

	/* A hack until do_gettimeofday prototype is moved to arch specific headers
	   and btfixupped. Patch do_gettimeofday with ba pci_do_gettimeofday; nop */
	((unsigned int *)do_gettimeofday)[0] = 
	    0x10800000 | ((((unsigned long)pci_do_gettimeofday -
	     (unsigned long)do_gettimeofday) >> 2) & 0x003fffff);
	((unsigned int *)do_gettimeofday)[1] = 0x01000000;
	BTFIXUPSET_CALL(bus_do_settimeofday, pci_do_settimeofday, BTFIXUPCALL_NORM);
	btfixup();

	writel (TICK_TIMER_LIMIT, pcic->pcic_regs+PCI_SYS_LIMIT);
	/* PROM should set appropriate irq */
	v = readb(pcic->pcic_regs+PCI_COUNTER_IRQ);
	timer_irq = PCI_COUNTER_IRQ_SYS(v);
	writel (PCI_COUNTER_IRQ_SET(timer_irq, 0),
		pcic->pcic_regs+PCI_COUNTER_IRQ);
	irq = request_irq(timer_irq, pcic_timer_handler,
			  (SA_INTERRUPT | SA_STATIC_ALLOC), "timer", NULL);
	if (irq) {
		prom_printf("time_init: unable to attach IRQ%d\n", timer_irq);
		prom_halt();
	}
	local_irq_enable();
}

static __inline__ unsigned long do_gettimeoffset(void)
{
	/*
	 * We devide all to 100
	 * to have microsecond resolution and to avoid overflow
	 */
	unsigned long count =
	    readl(pcic0.pcic_regs+PCI_SYS_COUNTER) & ~PCI_SYS_COUNTER_OVERFLOW;
	count = ((count/100)*USECS_PER_JIFFY) / (TICK_TIMER_LIMIT/100);
	return count;
}

extern unsigned long wall_jiffies;

static void pci_do_gettimeofday(struct timeval *tv)
{
	unsigned long flags;
	unsigned long seq;
	unsigned long usec, sec;
	unsigned long max_ntp_tick = tick_usec - tickadj;

	do {
		unsigned long lost;

		seq = read_seqbegin_irqsave(&xtime_lock, flags);
		usec = do_gettimeoffset();
		lost = jiffies - wall_jiffies;

		/*
		 * If time_adjust is negative then NTP is slowing the clock
		 * so make sure not to go into next possible interval.
		 * Better to lose some accuracy than have time go backwards..
		 */
		if (unlikely(time_adjust < 0)) {
			usec = min(usec, max_ntp_tick);

			if (lost)
				usec += lost * max_ntp_tick;
		}
		else if (unlikely(lost))
			usec += lost * tick_usec;

		sec = xtime.tv_sec;
		usec += (xtime.tv_nsec / 1000);
	} while (read_seqretry_irqrestore(&xtime_lock, seq, flags));

	while (usec >= 1000000) {
		usec -= 1000000;
		sec++;
	}

	tv->tv_sec = sec;
	tv->tv_usec = usec;
}

static int pci_do_settimeofday(struct timespec *tv)
{
	if ((unsigned long)tv->tv_nsec >= NSEC_PER_SEC)
		return -EINVAL;

	/*
	 * This is revolting. We need to set "xtime" correctly. However, the
	 * value in this location is the value at the most recent update of
	 * wall time.  Discover what correction gettimeofday() would have
	 * made, and then undo it!
	 */
	tv->tv_nsec -= 1000 * (do_gettimeoffset() + 
				(jiffies - wall_jiffies) * (USEC_PER_SEC / HZ));
	while (tv->tv_nsec < 0) {
		tv->tv_nsec += NSEC_PER_SEC;
		tv->tv_sec--;
	}

	wall_to_monotonic.tv_sec += xtime.tv_sec - tv->tv_sec;
	wall_to_monotonic.tv_nsec += xtime.tv_nsec - tv->tv_nsec;

	if (wall_to_monotonic.tv_nsec > NSEC_PER_SEC) {
		wall_to_monotonic.tv_nsec -= NSEC_PER_SEC;
		wall_to_monotonic.tv_sec++;
	}
	if (wall_to_monotonic.tv_nsec < 0) {
		wall_to_monotonic.tv_nsec += NSEC_PER_SEC;
		wall_to_monotonic.tv_sec--;
	}

	xtime.tv_sec = tv->tv_sec;
	xtime.tv_nsec = tv->tv_nsec;
	ntp_clear();
	return 0;
}

#if 0
static void watchdog_reset() {
	writeb(0, pcic->pcic_regs+PCI_SYS_STATUS);
}
#endif

/*
 * Other archs parse arguments here.
 */
char * __init pcibios_setup(char *str)
{
	return str;
}

void pcibios_align_resource(void *data, struct resource *res,
			    unsigned long size, unsigned long align)
{
}

int pcibios_enable_device(struct pci_dev *pdev, int mask)
{
	return 0;
}

/*
 * NMI
 */
void pcic_nmi(unsigned int pend, struct pt_regs *regs)
{

	pend = flip_dword(pend);

	if (!pcic_speculative || (pend & PCI_SYS_INT_PENDING_PIO) == 0) {
		/*
		 * XXX On CP-1200 PCI #SERR may happen, we do not know
		 * what to do about it yet.
		 */
		printk("Aiee, NMI pend 0x%x pc 0x%x spec %d, hanging\n",
		    pend, (int)regs->pc, pcic_speculative);
		for (;;) { }
	}
	pcic_speculative = 0;
	pcic_trapped = 1;
	regs->pc = regs->npc;
	regs->npc += 4;
}

static inline unsigned long get_irqmask(int irq_nr)
{
	return 1 << irq_nr;
}

static inline char *pcic_irq_itoa(unsigned int irq)
{
	static char buff[16];
	sprintf(buff, "%d", irq);
	return buff;
}

static void pcic_disable_irq(unsigned int irq_nr)
{
	unsigned long mask, flags;

	mask = get_irqmask(irq_nr);
	local_irq_save(flags);
	writel(mask, pcic0.pcic_regs+PCI_SYS_INT_TARGET_MASK_SET);
	local_irq_restore(flags);
}

static void pcic_enable_irq(unsigned int irq_nr)
{
	unsigned long mask, flags;

	mask = get_irqmask(irq_nr);
	local_irq_save(flags);
	writel(mask, pcic0.pcic_regs+PCI_SYS_INT_TARGET_MASK_CLEAR);
	local_irq_restore(flags);
}

static void pcic_clear_profile_irq(int cpu)
{
	printk("PCIC: unimplemented code: FILE=%s LINE=%d", __FILE__, __LINE__);
}

static void pcic_load_profile_irq(int cpu, unsigned int limit)
{
	printk("PCIC: unimplemented code: FILE=%s LINE=%d", __FILE__, __LINE__);
}

/* We assume the caller has disabled local interrupts when these are called,
 * or else very bizarre behavior will result.
 */
static void pcic_disable_pil_irq(unsigned int pil)
{
	writel(get_irqmask(pil), pcic0.pcic_regs+PCI_SYS_INT_TARGET_MASK_SET);
}

static void pcic_enable_pil_irq(unsigned int pil)
{
	writel(get_irqmask(pil), pcic0.pcic_regs+PCI_SYS_INT_TARGET_MASK_CLEAR);
}

void __init sun4m_pci_init_IRQ(void)
{
	BTFIXUPSET_CALL(enable_irq, pcic_enable_irq, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(disable_irq, pcic_disable_irq, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(enable_pil_irq, pcic_enable_pil_irq, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(disable_pil_irq, pcic_disable_pil_irq, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(clear_clock_irq, pcic_clear_clock_irq, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(clear_profile_irq, pcic_clear_profile_irq, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(load_profile_irq, pcic_load_profile_irq, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(__irq_itoa, pcic_irq_itoa, BTFIXUPCALL_NORM);
}

int pcibios_assign_resource(struct pci_dev *pdev, int resource)
{
	return -ENXIO;
}

/*
 * This probably belongs here rather than ioport.c because
 * we do not want this crud linked into SBus kernels.
 * Also, think for a moment about likes of floppy.c that
 * include architecture specific parts. They may want to redefine ins/outs.
 *
 * We do not use horroble macroses here because we want to
 * advance pointer by sizeof(size).
 */
void outsb(unsigned long addr, const void *src, unsigned long count)
{
	while (count) {
		count -= 1;
		outb(*(const char *)src, addr);
		src += 1;
		/* addr += 1; */
	}
}

void outsw(unsigned long addr, const void *src, unsigned long count)
{
	while (count) {
		count -= 2;
		outw(*(const short *)src, addr);
		src += 2;
		/* addr += 2; */
	}
}

void outsl(unsigned long addr, const void *src, unsigned long count)
{
	while (count) {
		count -= 4;
		outl(*(const long *)src, addr);
		src += 4;
		/* addr += 4; */
	}
}

void insb(unsigned long addr, void *dst, unsigned long count)
{
	while (count) {
		count -= 1;
		*(unsigned char *)dst = inb(addr);
		dst += 1;
		/* addr += 1; */
	}
}

void insw(unsigned long addr, void *dst, unsigned long count)
{
	while (count) {
		count -= 2;
		*(unsigned short *)dst = inw(addr);
		dst += 2;
		/* addr += 2; */
	}
}

void insl(unsigned long addr, void *dst, unsigned long count)
{
	while (count) {
		count -= 4;
		/*
		 * XXX I am sure we are in for an unaligned trap here.
		 */
		*(unsigned long *)dst = inl(addr);
		dst += 4;
		/* addr += 4; */
	}
}

subsys_initcall(pcic_init);
