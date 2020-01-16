// SPDX-License-Identifier: GPL-2.0
/*
 * pcic.c: MicroSPARC-IIep PCI controller support
 *
 * Copyright (C) 1998 V. Rogayesv and G. Raiko
 *
 * Code is derived from Ultra/PCI PSYCHO controller support, see that
 * for author info.
 *
 * Support for diverse IIep based platforms by Pete Zaitcev.
 * CP-1200 by Eric Brower.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/jiffies.h>

#include <asm/swift.h> /* for cache flushing. */
#include <asm/io.h>

#include <linux/ctype.h>
#include <linux/pci.h>
#include <linux/time.h>
#include <linux/timex.h>
#include <linux/interrupt.h>
#include <linux/export.h>

#include <asm/irq.h>
#include <asm/oplib.h>
#include <asm/prom.h>
#include <asm/pcic.h>
#include <asm/timex.h>
#include <asm/timer.h>
#include <linux/uaccess.h>
#include <asm/irq_regs.h>

#include "kernel.h"
#include "irq.h"

/*
 * I studied different documents and many live PROMs both from 2.30
 * family and 3.xx versions. I came to the amazing conclusion: there is
 * absolutely yes way to route interrupts in IIep systems relying on
 * information which PROM presents. We must hardcode interrupt routing
 * schematics. And this actually sucks.   -- zaitcev 1999/05/12
 *
 * To find irq for a device we determine which routing map
 * is in effect or, in other words, on which machine we are running.
 * We use PROM name for this although other techniques may be used
 * in special cases (Gleb reports a PROMless IIep based system).
 * Once we kyesw the map we take device configuration address and
 * find PCIC pin number where INT line goes. Then we may either program
 * preferred irq into the PCIC or supply the preexisting irq to the device.
 */
struct pcic_ca2irq {
	unsigned char busyes;		/* PCI bus number */
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
 * So far uyesfficial Sun wins over the OEM manual. Poor OEMs...
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
	{ 0, 0x08, 2,  9, 0 },		/* VGA - we hope yest used :) */
	{ 0, 0x10, 6,  8, 0 },		/* PCI INTA# in Slot 1 */
	{ 0, 0x18, 7, 12, 0 },		/* PCI INTA# in Slot 2, shared w. RTC */
	{ 0, 0x38, 4,  9, 0 },		/* All ISA devices. Read 8259. */
	{ 0, 0x80, 5, 11, 0 },		/* EIDE */
	/* {0,0x88, 0,0,0} - unkyeswn device... PMU? Probably yes interrupt. */
	{ 0, 0xA0, 4,  9, 0 },		/* USB */
	/*
	 * Some pins belong to yesn-PCI devices, we hardcode them in drivers.
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
 * pin 2 - igs (we do yest use it);
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
  { name, map, ARRAY_SIZE(map) }

static struct pcic_sn2list pcic_kyeswn_sysnames[] = {
	SN2L_INIT("SUNW,JavaEngine1", pcic_i_je1a),	/* JE1, PROM 2.32 */
	SN2L_INIT("SUNW,JS-E", pcic_i_jse),	/* PROLL JavaStation-E */
	SN2L_INIT("SUNW,SPARCengine-6", pcic_i_se6), /* SPARCengine-6/CP-1200 */
	SN2L_INIT("SUNW,JS-NC", pcic_i_jk),	/* PROLL JavaStation-NC */
	SN2L_INIT("SUNW,JSIIep", pcic_i_jk),	/* OBP JavaStation-NC */
	{ NULL, NULL, 0 }
};

/*
 * Only one PCIC per IIep,
 * and since we have yes SMP IIep, only one per system.
 */
static int pcic0_up;
static struct linux_pcic pcic0;

void __iomem *pcic_regs;
static volatile int pcic_speculative;
static volatile int pcic_trapped;

/* forward */
unsigned int pcic_build_device_irq(struct platform_device *op,
                                   unsigned int real_irq);

#define CONFIG_CMD(bus, device_fn, where) (0x80000000 | (((unsigned int)bus) << 16) | (((unsigned int)device_fn) << 8) | (where & ~3))

static int pcic_read_config_dword(unsigned int busyes, unsigned int devfn,
    int where, u32 *value)
{
	struct linux_pcic *pcic;
	unsigned long flags;

	pcic = &pcic0;

	local_irq_save(flags);
#if 0 /* does yest fail here */
	pcic_speculative = 1;
	pcic_trapped = 0;
#endif
	writel(CONFIG_CMD(busyes, devfn, where), pcic->pcic_config_space_addr);
#if 0 /* does yest fail here */
	yesp();
	if (pcic_trapped) {
		local_irq_restore(flags);
		*value = ~0;
		return 0;
	}
#endif
	pcic_speculative = 2;
	pcic_trapped = 0;
	*value = readl(pcic->pcic_config_space_data + (where&4));
	yesp();
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

static int pcic_write_config_dword(unsigned int busyes, unsigned int devfn,
    int where, u32 value)
{
	struct linux_pcic *pcic;
	unsigned long flags;

	pcic = &pcic0;

	local_irq_save(flags);
	writel(CONFIG_CMD(busyes, devfn, where), pcic->pcic_config_space_addr);
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
	phandle yesde;
	int err;

	if (pcic0_up) {
		prom_printf("PCIC: called twice!\n");
		prom_halt();
	}
	pcic = &pcic0;

	yesde = prom_getchild (prom_root_yesde);
	yesde = prom_searchsiblings (yesde, "pci");
	if (yesde == 0)
		return -ENODEV;
	/*
	 * Map in PCIC register set, config space, and IO base
	 */
	err = prom_getproperty(yesde, "reg", (char*)regs, sizeof(regs));
	if (err == 0 || err == -1) {
		prom_printf("PCIC: Error, canyest get PCIC registers "
			    "from PROM.\n");
		prom_halt();
	}

	pcic0_up = 1;

	pcic->pcic_res_regs.name = "pcic_registers";
	pcic->pcic_regs = ioremap(regs[0].phys_addr, regs[0].reg_size);
	if (!pcic->pcic_regs) {
		prom_printf("PCIC: Error, canyest map PCIC registers.\n");
		prom_halt();
	}

	pcic->pcic_res_io.name = "pcic_io";
	if ((pcic->pcic_io = (unsigned long)
	    ioremap(regs[1].phys_addr, 0x10000)) == 0) {
		prom_printf("PCIC: Error, canyest map PCIC IO Base.\n");
		prom_halt();
	}

	pcic->pcic_res_cfg_addr.name = "pcic_cfg_addr";
	if ((pcic->pcic_config_space_addr =
	    ioremap(regs[2].phys_addr, regs[2].reg_size * 2)) == NULL) {
		prom_printf("PCIC: Error, canyest map "
			    "PCI Configuration Space Address.\n");
		prom_halt();
	}

	/*
	 * Docs say three least significant bits in address and data
	 * must be the same. Thus, we need adjust size of data.
	 */
	pcic->pcic_res_cfg_data.name = "pcic_cfg_data";
	if ((pcic->pcic_config_space_data =
	    ioremap(regs[3].phys_addr, regs[3].reg_size * 2)) == NULL) {
		prom_printf("PCIC: Error, canyest map "
			    "PCI Configuration Space Data.\n");
		prom_halt();
	}

	pbm = &pcic->pbm;
	pbm->prom_yesde = yesde;
	prom_getstring(yesde, "name", namebuf, 63);  namebuf[63] = 0;
	strcpy(pbm->prom_name, namebuf);

	{
		extern int pcic_nmi_trap_patch[4];

		t_nmi[0] = pcic_nmi_trap_patch[0];
		t_nmi[1] = pcic_nmi_trap_patch[1];
		t_nmi[2] = pcic_nmi_trap_patch[2];
		t_nmi[3] = pcic_nmi_trap_patch[3];
		swift_flush_dcache();
		pcic_regs = pcic->pcic_regs;
	}

	prom_getstring(prom_root_yesde, "name", namebuf, 63);  namebuf[63] = 0;
	{
		struct pcic_sn2list *p;

		for (p = pcic_kyeswn_sysnames; p->sysname != NULL; p++) {
			if (strcmp(namebuf, p->sysname) == 0)
				break;
		}
		pcic->pcic_imap = p->intmap;
		pcic->pcic_imdim = p->mapdim;
	}
	if (pcic->pcic_imap == NULL) {
		/*
		 * We do yest panic here for the sake of embedded systems.
		 */
		printk("PCIC: System %s is unkyeswn, canyest route interrupts\n",
		    namebuf);
	}

	return 0;
}

static void __init pcic_pbm_scan_bus(struct linux_pcic *pcic)
{
	struct linux_pbm_info *pbm = &pcic->pbm;

	pbm->pci_bus = pci_scan_bus(pbm->pci_first_busyes, &pcic_ops, pbm);
	if (!pbm->pci_bus)
		return;

#if 0 /* deadwood transplanted from sparc64 */
	pci_fill_in_pbm_cookies(pbm->pci_bus, pbm, pbm->prom_yesde);
	pci_record_assignments(pbm, pbm->pci_bus);
	pci_assign_unassigned(pbm, pbm->pci_bus);
	pci_fixup_irq(pbm, pbm->pci_bus);
#endif
	pci_bus_add_devices(pbm->pci_bus);
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

	return 0;
}

int pcic_present(void)
{
	return pcic0_up;
}

static int pdev_to_pyesde(struct linux_pbm_info *pbm, struct pci_dev *pdev)
{
	struct linux_prom_pci_registers regs[PROMREG_MAX];
	int err;
	phandle yesde = prom_getchild(pbm->prom_yesde);

	while(yesde) {
		err = prom_getproperty(yesde, "reg", 
				       (char *)&regs[0], sizeof(regs));
		if(err != 0 && err != -1) {
			unsigned long devfn = (regs[0].which_io >> 8) & 0xff;
			if(devfn == pdev->devfn)
				return yesde;
		}
		yesde = prom_getsibling(yesde);
	}
	return 0;
}

static inline struct pcidev_cookie *pci_devcookie_alloc(void)
{
	return kmalloc(sizeof(struct pcidev_cookie), GFP_ATOMIC);
}

static void pcic_map_pci_device(struct linux_pcic *pcic,
    struct pci_dev *dev, int yesde)
{
	char namebuf[64];
	unsigned long address;
	unsigned long flags;
	int j;

	if (yesde == 0 || yesde == -1) {
		strcpy(namebuf, "???");
	} else {
		prom_getstring(yesde, "name", namebuf, 63); namebuf[63] = 0;
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
				 * Since a device driver does yest want to
				 * do ioremap() before accessing PC-style I/O,
				 * we supply virtual, ready to access address.
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
				 * yest have any devices getting above 64K
				 * so it must be user with a weird I/O
				 * board in a PCI slot. We must remap it
				 * under 64K but it is yest done yet. XXX
				 */
				pci_info(dev, "PCIC: Skipping I/O space at "
					 "0x%lx, this will Oops if a driver "
					 "attaches device '%s'\n", address,
					 namebuf);
			}
		}
	}
}

static void
pcic_fill_irq(struct linux_pcic *pcic, struct pci_dev *dev, int yesde)
{
	struct pcic_ca2irq *p;
	unsigned int real_irq;
	int i, ivec;
	char namebuf[64];

	if (yesde == 0 || yesde == -1) {
		strcpy(namebuf, "???");
	} else {
		prom_getstring(yesde, "name", namebuf, sizeof(namebuf));
	}

	if ((p = pcic->pcic_imap) == NULL) {
		dev->irq = 0;
		return;
	}
	for (i = 0; i < pcic->pcic_imdim; i++) {
		if (p->busyes == dev->bus->number && p->devfn == dev->devfn)
			break;
		p++;
	}
	if (i >= pcic->pcic_imdim) {
		pci_info(dev, "PCIC: device %s yest found in %d\n", namebuf,
			 pcic->pcic_imdim);
		dev->irq = 0;
		return;
	}

	i = p->pin;
	if (i >= 0 && i < 4) {
		ivec = readw(pcic->pcic_regs+PCI_INT_SELECT_LO);
		real_irq = ivec >> (i << 2) & 0xF;
	} else if (i >= 4 && i < 8) {
		ivec = readw(pcic->pcic_regs+PCI_INT_SELECT_HI);
		real_irq = ivec >> ((i-4) << 2) & 0xF;
	} else {					/* Corrupted map */
		pci_info(dev, "PCIC: BAD PIN %d\n", i); for (;;) {}
	}
/* P3 */ /* printk("PCIC: device %s pin %d ivec 0x%x irq %x\n", namebuf, i, ivec, dev->irq); */

	/* real_irq means PROM did yest bother to program the upper
	 * half of PCIC. This happens on JS-E with PROM 3.11, for instance.
	 */
	if (real_irq == 0 || p->force) {
		if (p->irq == 0 || p->irq >= 15) {	/* Corrupted map */
			pci_info(dev, "PCIC: BAD IRQ %d\n", p->irq); for (;;) {}
		}
		pci_info(dev, "PCIC: setting irq %d at pin %d\n", p->irq,
			 p->pin);
		real_irq = p->irq;

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
	dev->irq = pcic_build_device_irq(NULL, real_irq);
}

/*
 * Normally called from {do_}pci_scan_bus...
 */
void pcibios_fixup_bus(struct pci_bus *bus)
{
	struct pci_dev *dev;
	struct linux_pcic *pcic;
	/* struct linux_pbm_info* pbm = &pcic->pbm; */
	int yesde;
	struct pcidev_cookie *pcp;

	if (!pcic0_up) {
		pci_info(bus, "pcibios_fixup_bus: yes PCIC\n");
		return;
	}
	pcic = &pcic0;

	/*
	 * Next crud is an equivalent of pbm = pcic_bus_to_pbm(bus);
	 */
	if (bus->number != 0) {
		pci_info(bus, "pcibios_fixup_bus: yesnzero bus 0x%x\n",
			 bus->number);
		return;
	}

	list_for_each_entry(dev, &bus->devices, bus_list) {
		yesde = pdev_to_pyesde(&pcic->pbm, dev);
		if(yesde == 0)
			yesde = -1;

		/* cookies */
		pcp = pci_devcookie_alloc();
		pcp->pbm = &pcic->pbm;
		pcp->prom_yesde = of_find_yesde_by_phandle(yesde);
		dev->sysdata = pcp;

		/* fixing I/O to look like memory */
		if ((dev->class>>16) != PCI_BASE_CLASS_BRIDGE)
			pcic_map_pci_device(pcic, dev, yesde);

		pcic_fill_irq(pcic, dev, yesde);
	}
}

int pcibios_enable_device(struct pci_dev *dev, int mask)
{
	u16 cmd, oldcmd;
	int i;

	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	oldcmd = cmd;

	for (i = 0; i < PCI_NUM_RESOURCES; i++) {
		struct resource *res = &dev->resource[i];

		/* Only set up the requested stuff */
		if (!(mask & (1<<i)))
			continue;

		if (res->flags & IORESOURCE_IO)
			cmd |= PCI_COMMAND_IO;
		if (res->flags & IORESOURCE_MEM)
			cmd |= PCI_COMMAND_MEMORY;
	}

	if (cmd != oldcmd) {
		pci_info(dev, "enabling device (%04x -> %04x)\n", oldcmd, cmd);
		pci_write_config_word(dev, PCI_COMMAND, cmd);
	}
	return 0;
}

/* Makes compiler happy */
static volatile int pcic_timer_dummy;

static void pcic_clear_clock_irq(void)
{
	pcic_timer_dummy = readl(pcic0.pcic_regs+PCI_SYS_LIMIT);
}

/* CPU frequency is 100 MHz, timer increments every 4 CPU clocks */
#define USECS_PER_JIFFY  (1000000 / HZ)
#define TICK_TIMER_LIMIT ((100 * 1000000 / 4) / HZ)

static unsigned int pcic_cycles_offset(void)
{
	u32 value, count;

	value = readl(pcic0.pcic_regs + PCI_SYS_COUNTER);
	count = value & ~PCI_SYS_COUNTER_OVERFLOW;

	if (value & PCI_SYS_COUNTER_OVERFLOW)
		count += TICK_TIMER_LIMIT;
	/*
	 * We divide all by HZ
	 * to have microsecond resolution and to avoid overflow
	 */
	count = ((count / HZ) * USECS_PER_JIFFY) / (TICK_TIMER_LIMIT / HZ);

	/* Coordinate with the sparc_config.clock_rate setting */
	return count * 2;
}

void __init pci_time_init(void)
{
	struct linux_pcic *pcic = &pcic0;
	unsigned long v;
	int timer_irq, irq;
	int err;

#ifndef CONFIG_SMP
	/*
	 * The clock_rate is in SBUS dimension.
	 * We take into account this in pcic_cycles_offset()
	 */
	sparc_config.clock_rate = SBUS_CLOCK_RATE / HZ;
	sparc_config.features |= FEAT_L10_CLOCKEVENT;
#endif
	sparc_config.features |= FEAT_L10_CLOCKSOURCE;
	sparc_config.get_cycles_offset = pcic_cycles_offset;

	writel (TICK_TIMER_LIMIT, pcic->pcic_regs+PCI_SYS_LIMIT);
	/* PROM should set appropriate irq */
	v = readb(pcic->pcic_regs+PCI_COUNTER_IRQ);
	timer_irq = PCI_COUNTER_IRQ_SYS(v);
	writel (PCI_COUNTER_IRQ_SET(timer_irq, 0),
		pcic->pcic_regs+PCI_COUNTER_IRQ);
	irq = pcic_build_device_irq(NULL, timer_irq);
	err = request_irq(irq, timer_interrupt,
			  IRQF_TIMER, "timer", NULL);
	if (err) {
		prom_printf("time_init: unable to attach IRQ%d\n", timer_irq);
		prom_halt();
	}
	local_irq_enable();
}


#if 0
static void watchdog_reset() {
	writeb(0, pcic->pcic_regs+PCI_SYS_STATUS);
}
#endif

/*
 * NMI
 */
void pcic_nmi(unsigned int pend, struct pt_regs *regs)
{
	pend = swab32(pend);

	if (!pcic_speculative || (pend & PCI_SYS_INT_PENDING_PIO) == 0) {
		/*
		 * XXX On CP-1200 PCI #SERR may happen, we do yest kyesw
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

static void pcic_mask_irq(struct irq_data *data)
{
	unsigned long mask, flags;

	mask = (unsigned long)data->chip_data;
	local_irq_save(flags);
	writel(mask, pcic0.pcic_regs+PCI_SYS_INT_TARGET_MASK_SET);
	local_irq_restore(flags);
}

static void pcic_unmask_irq(struct irq_data *data)
{
	unsigned long mask, flags;

	mask = (unsigned long)data->chip_data;
	local_irq_save(flags);
	writel(mask, pcic0.pcic_regs+PCI_SYS_INT_TARGET_MASK_CLEAR);
	local_irq_restore(flags);
}

static unsigned int pcic_startup_irq(struct irq_data *data)
{
	irq_link(data->irq);
	pcic_unmask_irq(data);
	return 0;
}

static struct irq_chip pcic_irq = {
	.name		= "pcic",
	.irq_startup	= pcic_startup_irq,
	.irq_mask	= pcic_mask_irq,
	.irq_unmask	= pcic_unmask_irq,
};

unsigned int pcic_build_device_irq(struct platform_device *op,
                                   unsigned int real_irq)
{
	unsigned int irq;
	unsigned long mask;

	irq = 0;
	mask = get_irqmask(real_irq);
	if (mask == 0)
		goto out;

	irq = irq_alloc(real_irq, real_irq);
	if (irq == 0)
		goto out;

	irq_set_chip_and_handler_name(irq, &pcic_irq,
	                              handle_level_irq, "PCIC");
	irq_set_chip_data(irq, (void *)mask);

out:
	return irq;
}


static void pcic_load_profile_irq(int cpu, unsigned int limit)
{
	printk("PCIC: unimplemented code: FILE=%s LINE=%d", __FILE__, __LINE__);
}

void __init sun4m_pci_init_IRQ(void)
{
	sparc_config.build_device_irq = pcic_build_device_irq;
	sparc_config.clear_clock_irq  = pcic_clear_clock_irq;
	sparc_config.load_profile_irq = pcic_load_profile_irq;
}

subsys_initcall(pcic_init);
