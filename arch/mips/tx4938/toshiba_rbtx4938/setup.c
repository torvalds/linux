/*
 * linux/arch/mips/tx4938/toshiba_rbtx4938/setup.c
 *
 * Setup pointers to hardware-dependent routines.
 * Copyright (C) 2000-2001 Toshiba Corporation
 *
 * 2003-2005 (c) MontaVista Software, Inc. This file is licensed under the
 * terms of the GNU General Public License version 2. This program is
 * licensed "as is" without any warranty of any kind, whether express
 * or implied.
 *
 * Support for TX4938 in 2.6 - Manish Lachwani (mlachwani@mvista.com)
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/ioport.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/console.h>
#include <linux/pci.h>
#include <linux/pm.h>

#include <asm/wbflush.h>
#include <asm/reboot.h>
#include <asm/irq.h>
#include <asm/time.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/bootinfo.h>
#include <asm/tx4938/rbtx4938.h>
#ifdef CONFIG_SERIAL_TXX9
#include <linux/tty.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#endif

extern void rbtx4938_time_init(void) __init;
extern char * __init prom_getcmdline(void);
static inline void tx4938_report_pcic_status1(struct tx4938_pcic_reg *pcicptr);

/* These functions are used for rebooting or halting the machine*/
extern void rbtx4938_machine_restart(char *command);
extern void rbtx4938_machine_halt(void);
extern void rbtx4938_machine_power_off(void);

/* clocks */
unsigned int txx9_master_clock;
unsigned int txx9_cpu_clock;
unsigned int txx9_gbus_clock;

unsigned long rbtx4938_ce_base[8];
unsigned long rbtx4938_ce_size[8];
int txboard_pci66_mode;
static int tx4938_pcic_trdyto;	/* default: disabled */
static int tx4938_pcic_retryto;	/* default: disabled */
static int tx4938_ccfg_toeon = 1;

struct tx4938_pcic_reg *pcicptrs[4] = {
       tx4938_pcicptr  /* default setting for TX4938 */
};

static struct {
	unsigned long base;
	unsigned long size;
} phys_regions[16] __initdata;
static int num_phys_regions  __initdata;

#define PHYS_REGION_MINSIZE	0x10000

void rbtx4938_machine_halt(void)
{
        printk(KERN_NOTICE "System Halted\n");
	local_irq_disable();

	while (1)
		__asm__(".set\tmips3\n\t"
			"wait\n\t"
			".set\tmips0");
}

void rbtx4938_machine_power_off(void)
{
        rbtx4938_machine_halt();
        /* no return */
}

void rbtx4938_machine_restart(char *command)
{
	local_irq_disable();

	printk("Rebooting...");
	*rbtx4938_softresetlock_ptr = 1;
	*rbtx4938_sfvol_ptr = 1;
	*rbtx4938_softreset_ptr = 1;
	wbflush();

	while(1);
}

void __init
txboard_add_phys_region(unsigned long base, unsigned long size)
{
	if (num_phys_regions >= ARRAY_SIZE(phys_regions)) {
		printk("phys_region overflow\n");
		return;
	}
	phys_regions[num_phys_regions].base = base;
	phys_regions[num_phys_regions].size = size;
	num_phys_regions++;
}
unsigned long __init
txboard_find_free_phys_region(unsigned long begin, unsigned long end,
			      unsigned long size)
{
	unsigned long base;
	int i;

	for (base = begin / size * size; base < end; base += size) {
		for (i = 0; i < num_phys_regions; i++) {
			if (phys_regions[i].size &&
			    base <= phys_regions[i].base + (phys_regions[i].size - 1) &&
			    base + (size - 1) >= phys_regions[i].base)
				break;
		}
		if (i == num_phys_regions)
			return base;
	}
	return 0;
}
unsigned long __init
txboard_find_free_phys_region_shrink(unsigned long begin, unsigned long end,
				     unsigned long *size)
{
	unsigned long sz, base;
	for (sz = *size; sz >= PHYS_REGION_MINSIZE; sz /= 2) {
		base = txboard_find_free_phys_region(begin, end, sz);
		if (base) {
			*size = sz;
			return base;
		}
	}
	return 0;
}
unsigned long __init
txboard_request_phys_region_range(unsigned long begin, unsigned long end,
				  unsigned long size)
{
	unsigned long base;
	base = txboard_find_free_phys_region(begin, end, size);
	if (base)
		txboard_add_phys_region(base, size);
	return base;
}
unsigned long __init
txboard_request_phys_region(unsigned long size)
{
	unsigned long base;
	unsigned long begin = 0, end = 0x20000000;	/* search low 512MB */
	base = txboard_find_free_phys_region(begin, end, size);
	if (base)
		txboard_add_phys_region(base, size);
	return base;
}
unsigned long __init
txboard_request_phys_region_shrink(unsigned long *size)
{
	unsigned long base;
	unsigned long begin = 0, end = 0x20000000;	/* search low 512MB */
	base = txboard_find_free_phys_region_shrink(begin, end, size);
	if (base)
		txboard_add_phys_region(base, *size);
	return base;
}

#ifdef CONFIG_PCI
void __init
tx4938_pcic_setup(struct tx4938_pcic_reg *pcicptr,
		  struct pci_controller *channel,
		  unsigned long pci_io_base,
		  int extarb)
{
	int i;

	/* Disable All Initiator Space */
	pcicptr->pciccfg &= ~(TX4938_PCIC_PCICCFG_G2PMEN(0)|
			      TX4938_PCIC_PCICCFG_G2PMEN(1)|
			      TX4938_PCIC_PCICCFG_G2PMEN(2)|
			      TX4938_PCIC_PCICCFG_G2PIOEN);

	/* GB->PCI mappings */
	pcicptr->g2piomask = (channel->io_resource->end - channel->io_resource->start) >> 4;
	pcicptr->g2piogbase = pci_io_base |
#ifdef __BIG_ENDIAN
		TX4938_PCIC_G2PIOGBASE_ECHG
#else
		TX4938_PCIC_G2PIOGBASE_BSDIS
#endif
		;
	pcicptr->g2piopbase = 0;
	for (i = 0; i < 3; i++) {
		pcicptr->g2pmmask[i] = 0;
		pcicptr->g2pmgbase[i] = 0;
		pcicptr->g2pmpbase[i] = 0;
	}
	if (channel->mem_resource->end) {
		pcicptr->g2pmmask[0] = (channel->mem_resource->end - channel->mem_resource->start) >> 4;
		pcicptr->g2pmgbase[0] = channel->mem_resource->start |
#ifdef __BIG_ENDIAN
			TX4938_PCIC_G2PMnGBASE_ECHG
#else
			TX4938_PCIC_G2PMnGBASE_BSDIS
#endif
			;
		pcicptr->g2pmpbase[0] = channel->mem_resource->start;
	}
	/* PCI->GB mappings (I/O 256B) */
	pcicptr->p2giopbase = 0; /* 256B */
	pcicptr->p2giogbase = 0;
	/* PCI->GB mappings (MEM 512MB (64MB on R1.x)) */
	pcicptr->p2gm0plbase = 0;
	pcicptr->p2gm0pubase = 0;
	pcicptr->p2gmgbase[0] = 0 |
		TX4938_PCIC_P2GMnGBASE_TMEMEN |
#ifdef __BIG_ENDIAN
		TX4938_PCIC_P2GMnGBASE_TECHG
#else
		TX4938_PCIC_P2GMnGBASE_TBSDIS
#endif
		;
	/* PCI->GB mappings (MEM 16MB) */
	pcicptr->p2gm1plbase = 0xffffffff;
	pcicptr->p2gm1pubase = 0xffffffff;
	pcicptr->p2gmgbase[1] = 0;
	/* PCI->GB mappings (MEM 1MB) */
	pcicptr->p2gm2pbase = 0xffffffff; /* 1MB */
	pcicptr->p2gmgbase[2] = 0;

	pcicptr->pciccfg &= TX4938_PCIC_PCICCFG_GBWC_MASK;
	/* Enable Initiator Memory Space */
	if (channel->mem_resource->end)
		pcicptr->pciccfg |= TX4938_PCIC_PCICCFG_G2PMEN(0);
	/* Enable Initiator I/O Space */
	if (channel->io_resource->end)
		pcicptr->pciccfg |= TX4938_PCIC_PCICCFG_G2PIOEN;
	/* Enable Initiator Config */
	pcicptr->pciccfg |=
		TX4938_PCIC_PCICCFG_ICAEN |
		TX4938_PCIC_PCICCFG_TCAR;

	/* Do not use MEMMUL, MEMINF: YMFPCI card causes M_ABORT. */
	pcicptr->pcicfg1 = 0;

	pcicptr->g2ptocnt &= ~0xffff;

	if (tx4938_pcic_trdyto >= 0) {
		pcicptr->g2ptocnt &= ~0xff;
		pcicptr->g2ptocnt |= (tx4938_pcic_trdyto & 0xff);
	}

	if (tx4938_pcic_retryto >= 0) {
		pcicptr->g2ptocnt &= ~0xff00;
		pcicptr->g2ptocnt |= ((tx4938_pcic_retryto<<8) & 0xff00);
	}

	/* Clear All Local Bus Status */
	pcicptr->pcicstatus = TX4938_PCIC_PCICSTATUS_ALL;
	/* Enable All Local Bus Interrupts */
	pcicptr->pcicmask = TX4938_PCIC_PCICSTATUS_ALL;
	/* Clear All Initiator Status */
	pcicptr->g2pstatus = TX4938_PCIC_G2PSTATUS_ALL;
	/* Enable All Initiator Interrupts */
	pcicptr->g2pmask = TX4938_PCIC_G2PSTATUS_ALL;
	/* Clear All PCI Status Error */
	pcicptr->pcistatus =
		(pcicptr->pcistatus & 0x0000ffff) |
		(TX4938_PCIC_PCISTATUS_ALL << 16);
	/* Enable All PCI Status Error Interrupts */
	pcicptr->pcimask = TX4938_PCIC_PCISTATUS_ALL;

	if (!extarb) {
		/* Reset Bus Arbiter */
		pcicptr->pbacfg = TX4938_PCIC_PBACFG_RPBA;
		pcicptr->pbabm = 0;
		/* Enable Bus Arbiter */
		pcicptr->pbacfg = TX4938_PCIC_PBACFG_PBAEN;
	}

      /* PCIC Int => IRC IRQ16 */
	pcicptr->pcicfg2 =
		    (pcicptr->pcicfg2 & 0xffffff00) | TX4938_IR_PCIC;

	pcicptr->pcistatus = PCI_COMMAND_MASTER |
		PCI_COMMAND_MEMORY |
		PCI_COMMAND_PARITY | PCI_COMMAND_SERR;
}

int __init
tx4938_report_pciclk(void)
{
	unsigned long pcode = TX4938_REV_PCODE();
	int pciclk = 0;
	printk("TX%lx PCIC --%s PCICLK:",
	       pcode,
	       (tx4938_ccfgptr->ccfg & TX4938_CCFG_PCI66) ? " PCI66" : "");
	if (tx4938_ccfgptr->pcfg & TX4938_PCFG_PCICLKEN_ALL) {

		switch ((unsigned long)tx4938_ccfgptr->ccfg & TX4938_CCFG_PCIDIVMODE_MASK) {
		case TX4938_CCFG_PCIDIVMODE_4:
			pciclk = txx9_cpu_clock / 4; break;
		case TX4938_CCFG_PCIDIVMODE_4_5:
			pciclk = txx9_cpu_clock * 2 / 9; break;
		case TX4938_CCFG_PCIDIVMODE_5:
			pciclk = txx9_cpu_clock / 5; break;
		case TX4938_CCFG_PCIDIVMODE_5_5:
			pciclk = txx9_cpu_clock * 2 / 11; break;
		case TX4938_CCFG_PCIDIVMODE_8:
			pciclk = txx9_cpu_clock / 8; break;
		case TX4938_CCFG_PCIDIVMODE_9:
			pciclk = txx9_cpu_clock / 9; break;
		case TX4938_CCFG_PCIDIVMODE_10:
			pciclk = txx9_cpu_clock / 10; break;
		case TX4938_CCFG_PCIDIVMODE_11:
			pciclk = txx9_cpu_clock / 11; break;
		}
		printk("Internal(%dMHz)", pciclk / 1000000);
	} else {
		printk("External");
		pciclk = -1;
	}
	printk("\n");
	return pciclk;
}

void __init set_tx4938_pcicptr(int ch, struct tx4938_pcic_reg *pcicptr)
{
	pcicptrs[ch] = pcicptr;
}

struct tx4938_pcic_reg *get_tx4938_pcicptr(int ch)
{
       return pcicptrs[ch];
}

static struct pci_dev *fake_pci_dev(struct pci_controller *hose,
                                    int top_bus, int busnr, int devfn)
{
	static struct pci_dev dev;
	static struct pci_bus bus;

	dev.sysdata = (void *)hose;
	dev.devfn = devfn;
	bus.number = busnr;
	bus.ops = hose->pci_ops;
	bus.parent = NULL;
	dev.bus = &bus;

	return &dev;
}

#define EARLY_PCI_OP(rw, size, type)                                    \
static int early_##rw##_config_##size(struct pci_controller *hose,      \
        int top_bus, int bus, int devfn, int offset, type value)        \
{                                                                       \
        return pci_##rw##_config_##size(                                \
                fake_pci_dev(hose, top_bus, bus, devfn),                \
                offset, value);                                         \
}

EARLY_PCI_OP(read, word, u16 *)

int txboard_pci66_check(struct pci_controller *hose, int top_bus, int current_bus)
{
	u32 pci_devfn;
	unsigned short vid;
	int devfn_start = 0;
	int devfn_stop = 0xff;
	int cap66 = -1;
	u16 stat;

	printk("PCI: Checking 66MHz capabilities...\n");

	for (pci_devfn=devfn_start; pci_devfn<devfn_stop; pci_devfn++) {
		early_read_config_word(hose, top_bus, current_bus, pci_devfn,
				       PCI_VENDOR_ID, &vid);

		if (vid == 0xffff) continue;

		/* check 66MHz capability */
		if (cap66 < 0)
			cap66 = 1;
		if (cap66) {
			early_read_config_word(hose, top_bus, current_bus, pci_devfn,
					       PCI_STATUS, &stat);
			if (!(stat & PCI_STATUS_66MHZ)) {
				printk(KERN_DEBUG "PCI: %02x:%02x not 66MHz capable.\n",
				       current_bus, pci_devfn);
				cap66 = 0;
				break;
			}
		}
	}
	return cap66 > 0;
}

int __init
tx4938_pciclk66_setup(void)
{
	int pciclk;

	/* Assert M66EN */
	tx4938_ccfgptr->ccfg |= TX4938_CCFG_PCI66;
	/* Double PCICLK (if possible) */
	if (tx4938_ccfgptr->pcfg & TX4938_PCFG_PCICLKEN_ALL) {
		unsigned int pcidivmode =
			tx4938_ccfgptr->ccfg & TX4938_CCFG_PCIDIVMODE_MASK;
		switch (pcidivmode) {
		case TX4938_CCFG_PCIDIVMODE_8:
		case TX4938_CCFG_PCIDIVMODE_4:
			pcidivmode = TX4938_CCFG_PCIDIVMODE_4;
			pciclk = txx9_cpu_clock / 4;
			break;
		case TX4938_CCFG_PCIDIVMODE_9:
		case TX4938_CCFG_PCIDIVMODE_4_5:
			pcidivmode = TX4938_CCFG_PCIDIVMODE_4_5;
			pciclk = txx9_cpu_clock * 2 / 9;
			break;
		case TX4938_CCFG_PCIDIVMODE_10:
		case TX4938_CCFG_PCIDIVMODE_5:
			pcidivmode = TX4938_CCFG_PCIDIVMODE_5;
			pciclk = txx9_cpu_clock / 5;
			break;
		case TX4938_CCFG_PCIDIVMODE_11:
		case TX4938_CCFG_PCIDIVMODE_5_5:
		default:
			pcidivmode = TX4938_CCFG_PCIDIVMODE_5_5;
			pciclk = txx9_cpu_clock * 2 / 11;
			break;
		}
		tx4938_ccfgptr->ccfg =
			(tx4938_ccfgptr->ccfg & ~TX4938_CCFG_PCIDIVMODE_MASK)
			| pcidivmode;
		printk(KERN_DEBUG "PCICLK: ccfg:%08lx\n",
		       (unsigned long)tx4938_ccfgptr->ccfg);
	} else {
		pciclk = -1;
	}
	return pciclk;
}

extern struct pci_controller tx4938_pci_controller[];
static int __init tx4938_pcibios_init(void)
{
	unsigned long mem_base[2];
	unsigned long mem_size[2] = {TX4938_PCIMEM_SIZE_0,TX4938_PCIMEM_SIZE_1}; /* MAX 128M,64K */
	unsigned long io_base[2];
	unsigned long io_size[2] = {TX4938_PCIIO_SIZE_0,TX4938_PCIIO_SIZE_1}; /* MAX 16M,64K */
	/* TX4938 PCIC1: 64K MEM/IO is enough for ETH0,ETH1 */
	int extarb = !(tx4938_ccfgptr->ccfg & TX4938_CCFG_PCIXARB);

	PCIBIOS_MIN_IO = 0x00001000UL;
	PCIBIOS_MIN_MEM = 0x01000000UL;

	mem_base[0] = txboard_request_phys_region_shrink(&mem_size[0]);
	io_base[0] = txboard_request_phys_region_shrink(&io_size[0]);

	printk("TX4938 PCIC -- DID:%04x VID:%04x RID:%02x Arbiter:%s\n",
	       (unsigned short)(tx4938_pcicptr->pciid >> 16),
	       (unsigned short)(tx4938_pcicptr->pciid & 0xffff),
	       (unsigned short)(tx4938_pcicptr->pciccrev & 0xff),
	       extarb ? "External" : "Internal");

	/* setup PCI area */
	tx4938_pci_controller[0].io_resource->start = io_base[0];
	tx4938_pci_controller[0].io_resource->end = (io_base[0] + io_size[0]) - 1;
	tx4938_pci_controller[0].mem_resource->start = mem_base[0];
	tx4938_pci_controller[0].mem_resource->end = mem_base[0] + mem_size[0] - 1;

	set_tx4938_pcicptr(0, tx4938_pcicptr);

	register_pci_controller(&tx4938_pci_controller[0]);

	if (tx4938_ccfgptr->ccfg & TX4938_CCFG_PCI66) {
		printk("TX4938_CCFG_PCI66 already configured\n");
		txboard_pci66_mode = -1; /* already configured */
	}

	/* Reset PCI Bus */
	*rbtx4938_pcireset_ptr = 0;
	/* Reset PCIC */
	tx4938_ccfgptr->clkctr |= TX4938_CLKCTR_PCIRST;
	if (txboard_pci66_mode > 0)
		tx4938_pciclk66_setup();
	mdelay(10);
	/* clear PCIC reset */
	tx4938_ccfgptr->clkctr &= ~TX4938_CLKCTR_PCIRST;
	*rbtx4938_pcireset_ptr = 1;
	wbflush();
	tx4938_report_pcic_status1(tx4938_pcicptr);

	tx4938_report_pciclk();
	tx4938_pcic_setup(tx4938_pcicptr, &tx4938_pci_controller[0], io_base[0], extarb);
	if (txboard_pci66_mode == 0 &&
	    txboard_pci66_check(&tx4938_pci_controller[0], 0, 0)) {
		/* Reset PCI Bus */
		*rbtx4938_pcireset_ptr = 0;
		/* Reset PCIC */
		tx4938_ccfgptr->clkctr |= TX4938_CLKCTR_PCIRST;
		tx4938_pciclk66_setup();
		mdelay(10);
		/* clear PCIC reset */
		tx4938_ccfgptr->clkctr &= ~TX4938_CLKCTR_PCIRST;
		*rbtx4938_pcireset_ptr = 1;
		wbflush();
		/* Reinitialize PCIC */
		tx4938_report_pciclk();
		tx4938_pcic_setup(tx4938_pcicptr, &tx4938_pci_controller[0], io_base[0], extarb);
	}

	mem_base[1] = txboard_request_phys_region_shrink(&mem_size[1]);
	io_base[1] = txboard_request_phys_region_shrink(&io_size[1]);
	/* Reset PCIC1 */
	tx4938_ccfgptr->clkctr |= TX4938_CLKCTR_PCIC1RST;
	/* PCI1DMD==0 => PCI1CLK==GBUSCLK/2 => PCI66 */
	if (!(tx4938_ccfgptr->ccfg & TX4938_CCFG_PCI1DMD))
		tx4938_ccfgptr->ccfg |= TX4938_CCFG_PCI1_66;
	else
		tx4938_ccfgptr->ccfg &= ~TX4938_CCFG_PCI1_66;
	mdelay(10);
	/* clear PCIC1 reset */
	tx4938_ccfgptr->clkctr &= ~TX4938_CLKCTR_PCIC1RST;
	tx4938_report_pcic_status1(tx4938_pcic1ptr);

	printk("TX4938 PCIC1 -- DID:%04x VID:%04x RID:%02x",
	       (unsigned short)(tx4938_pcic1ptr->pciid >> 16),
	       (unsigned short)(tx4938_pcic1ptr->pciid & 0xffff),
	       (unsigned short)(tx4938_pcic1ptr->pciccrev & 0xff));
	printk("%s PCICLK:%dMHz\n",
	       (tx4938_ccfgptr->ccfg & TX4938_CCFG_PCI1_66) ? " PCI66" : "",
	       txx9_gbus_clock /
	       ((tx4938_ccfgptr->ccfg & TX4938_CCFG_PCI1DMD) ? 4 : 2) /
	       1000000);

	/* assumption: CPHYSADDR(mips_io_port_base) == io_base[0] */
	tx4938_pci_controller[1].io_resource->start =
		io_base[1] - io_base[0];
	tx4938_pci_controller[1].io_resource->end =
		io_base[1] - io_base[0] + io_size[1] - 1;
	tx4938_pci_controller[1].mem_resource->start = mem_base[1];
	tx4938_pci_controller[1].mem_resource->end =
		mem_base[1] + mem_size[1] - 1;
	set_tx4938_pcicptr(1, tx4938_pcic1ptr);

	register_pci_controller(&tx4938_pci_controller[1]);

	tx4938_pcic_setup(tx4938_pcic1ptr, &tx4938_pci_controller[1], io_base[1], extarb);

	/* map ioport 0 to PCI I/O space address 0 */
	set_io_port_base(KSEG1 + io_base[0]);

	return 0;
}

arch_initcall(tx4938_pcibios_init);

#endif /* CONFIG_PCI */

/* SPI support */

/* chip select for SPI devices */
#define	SEEPROM1_CS	7	/* PIO7 */
#define	SEEPROM2_CS	0	/* IOC */
#define	SEEPROM3_CS	1	/* IOC */
#define	SRTC_CS	2	/* IOC */

static int rbtx4938_spi_cs_func(int chipid, int on)
{
	unsigned char bit;
	switch (chipid) {
	case RBTX4938_SEEPROM1_CHIPID:
		if (on)
			tx4938_pioptr->dout &= ~(1 << SEEPROM1_CS);
		else
			tx4938_pioptr->dout |= (1 << SEEPROM1_CS);
		return 0;
		break;
	case RBTX4938_SEEPROM2_CHIPID:
		bit = (1 << SEEPROM2_CS);
		break;
	case RBTX4938_SEEPROM3_CHIPID:
		bit = (1 << SEEPROM3_CS);
		break;
	case RBTX4938_SRTC_CHIPID:
		bit = (1 << SRTC_CS);
		break;
	default:
		return -ENODEV;
	}
	/* bit1,2,4 are low active, bit3 is high active */
	*rbtx4938_spics_ptr =
		(*rbtx4938_spics_ptr & ~bit) |
		((on ? (bit ^ 0x0b) : ~(bit ^ 0x0b)) & bit);
	return 0;
}

#ifdef CONFIG_PCI
extern int spi_eeprom_read(int chipid, int address, unsigned char *buf, int len);

int rbtx4938_get_tx4938_ethaddr(struct pci_dev *dev, unsigned char *addr)
{
	struct pci_controller *channel = (struct pci_controller *)dev->bus->sysdata;
	static unsigned char dat[17];
	static int read_dat = 0;
	int ch = 0;

	if (channel != &tx4938_pci_controller[1])
		return -ENODEV;
	/* TX4938 PCIC1 */
	switch (PCI_SLOT(dev->devfn)) {
	case TX4938_PCIC_IDSEL_AD_TO_SLOT(31):
		ch = 0;
		break;
	case TX4938_PCIC_IDSEL_AD_TO_SLOT(30):
		ch = 1;
		break;
	default:
		return -ENODEV;
	}
	if (!read_dat) {
		unsigned char sum;
		int i;
		read_dat = 1;
		/* 0-3: "MAC\0", 4-9:eth0, 10-15:eth1, 16:sum */
		if (spi_eeprom_read(RBTX4938_SEEPROM1_CHIPID,
				    0, dat, sizeof(dat))) {
			printk(KERN_ERR "seeprom: read error.\n");
		} else {
			if (strcmp(dat, "MAC") != 0)
				printk(KERN_WARNING "seeprom: bad signature.\n");
			for (i = 0, sum = 0; i < sizeof(dat); i++)
				sum += dat[i];
			if (sum)
				printk(KERN_WARNING "seeprom: bad checksum.\n");
		}
	}
	memcpy(addr, &dat[4 + 6 * ch], 6);
	return 0;
}
#endif /* CONFIG_PCI */

extern void __init txx9_spi_init(unsigned long base, int (*cs_func)(int chipid, int on));
static void __init rbtx4938_spi_setup(void)
{
	/* set SPI_SEL */
	tx4938_ccfgptr->pcfg |= TX4938_PCFG_SPI_SEL;
	/* chip selects for SPI devices */
	tx4938_pioptr->dout |= (1 << SEEPROM1_CS);
	tx4938_pioptr->dir |= (1 << SEEPROM1_CS);
	txx9_spi_init(TX4938_SPI_REG, rbtx4938_spi_cs_func);
}

static struct resource rbtx4938_fpga_resource;

static char pcode_str[8];
static struct resource tx4938_reg_resource = {
	.start	= TX4938_REG_BASE,
	.end	= TX4938_REG_BASE + TX4938_REG_SIZE,
	.name	= pcode_str,
	.flags	= IORESOURCE_MEM
};

void __init tx4938_board_setup(void)
{
	int i;
	unsigned long divmode;
	int cpuclk = 0;
	unsigned long pcode = TX4938_REV_PCODE();

	ioport_resource.start = 0x1000;
	ioport_resource.end = 0xffffffff;
	iomem_resource.start = 0x1000;
	iomem_resource.end = 0xffffffff;	/* expand to 4GB */

	sprintf(pcode_str, "TX%lx", pcode);
	/* SDRAMC,EBUSC are configured by PROM */
	for (i = 0; i < 8; i++) {
		if (!(tx4938_ebuscptr->cr[i] & 0x8))
			continue;	/* disabled */
		rbtx4938_ce_base[i] = (unsigned long)TX4938_EBUSC_BA(i);
		txboard_add_phys_region(rbtx4938_ce_base[i], TX4938_EBUSC_SIZE(i));
	}

	/* clocks */
	if (txx9_master_clock) {
		/* calculate gbus_clock and cpu_clock from master_clock */
		divmode = (unsigned long)tx4938_ccfgptr->ccfg & TX4938_CCFG_DIVMODE_MASK;
		switch (divmode) {
		case TX4938_CCFG_DIVMODE_8:
		case TX4938_CCFG_DIVMODE_10:
		case TX4938_CCFG_DIVMODE_12:
		case TX4938_CCFG_DIVMODE_16:
		case TX4938_CCFG_DIVMODE_18:
			txx9_gbus_clock = txx9_master_clock * 4; break;
		default:
			txx9_gbus_clock = txx9_master_clock;
		}
		switch (divmode) {
		case TX4938_CCFG_DIVMODE_2:
		case TX4938_CCFG_DIVMODE_8:
			cpuclk = txx9_gbus_clock * 2; break;
		case TX4938_CCFG_DIVMODE_2_5:
		case TX4938_CCFG_DIVMODE_10:
			cpuclk = txx9_gbus_clock * 5 / 2; break;
		case TX4938_CCFG_DIVMODE_3:
		case TX4938_CCFG_DIVMODE_12:
			cpuclk = txx9_gbus_clock * 3; break;
		case TX4938_CCFG_DIVMODE_4:
		case TX4938_CCFG_DIVMODE_16:
			cpuclk = txx9_gbus_clock * 4; break;
		case TX4938_CCFG_DIVMODE_4_5:
		case TX4938_CCFG_DIVMODE_18:
			cpuclk = txx9_gbus_clock * 9 / 2; break;
		}
		txx9_cpu_clock = cpuclk;
	} else {
		if (txx9_cpu_clock == 0) {
			txx9_cpu_clock = 300000000;	/* 300MHz */
		}
		/* calculate gbus_clock and master_clock from cpu_clock */
		cpuclk = txx9_cpu_clock;
		divmode = (unsigned long)tx4938_ccfgptr->ccfg & TX4938_CCFG_DIVMODE_MASK;
		switch (divmode) {
		case TX4938_CCFG_DIVMODE_2:
		case TX4938_CCFG_DIVMODE_8:
			txx9_gbus_clock = cpuclk / 2; break;
		case TX4938_CCFG_DIVMODE_2_5:
		case TX4938_CCFG_DIVMODE_10:
			txx9_gbus_clock = cpuclk * 2 / 5; break;
		case TX4938_CCFG_DIVMODE_3:
		case TX4938_CCFG_DIVMODE_12:
			txx9_gbus_clock = cpuclk / 3; break;
		case TX4938_CCFG_DIVMODE_4:
		case TX4938_CCFG_DIVMODE_16:
			txx9_gbus_clock = cpuclk / 4; break;
		case TX4938_CCFG_DIVMODE_4_5:
		case TX4938_CCFG_DIVMODE_18:
			txx9_gbus_clock = cpuclk * 2 / 9; break;
		}
		switch (divmode) {
		case TX4938_CCFG_DIVMODE_8:
		case TX4938_CCFG_DIVMODE_10:
		case TX4938_CCFG_DIVMODE_12:
		case TX4938_CCFG_DIVMODE_16:
		case TX4938_CCFG_DIVMODE_18:
			txx9_master_clock = txx9_gbus_clock / 4; break;
		default:
			txx9_master_clock = txx9_gbus_clock;
		}
	}
	/* change default value to udelay/mdelay take reasonable time */
	loops_per_jiffy = txx9_cpu_clock / HZ / 2;

	/* CCFG */
	/* clear WatchDogReset,BusErrorOnWrite flag (W1C) */
	tx4938_ccfgptr->ccfg |= TX4938_CCFG_WDRST | TX4938_CCFG_BEOW;
	/* clear PCIC1 reset */
	if (tx4938_ccfgptr->clkctr & TX4938_CLKCTR_PCIC1RST)
		tx4938_ccfgptr->clkctr &= ~TX4938_CLKCTR_PCIC1RST;

	/* enable Timeout BusError */
	if (tx4938_ccfg_toeon)
		tx4938_ccfgptr->ccfg |= TX4938_CCFG_TOE;

	/* DMA selection */
	tx4938_ccfgptr->pcfg &= ~TX4938_PCFG_DMASEL_ALL;

	/* Use external clock for external arbiter */
	if (!(tx4938_ccfgptr->ccfg & TX4938_CCFG_PCIXARB))
		tx4938_ccfgptr->pcfg &= ~TX4938_PCFG_PCICLKEN_ALL;

	printk("%s -- %dMHz(M%dMHz) CRIR:%08lx CCFG:%Lx PCFG:%Lx\n",
	       pcode_str,
	       cpuclk / 1000000, txx9_master_clock / 1000000,
	       (unsigned long)tx4938_ccfgptr->crir,
	       tx4938_ccfgptr->ccfg,
	       tx4938_ccfgptr->pcfg);

	printk("%s SDRAMC --", pcode_str);
	for (i = 0; i < 4; i++) {
		unsigned long long cr = tx4938_sdramcptr->cr[i];
		unsigned long ram_base, ram_size;
		if (!((unsigned long)cr & 0x00000400))
			continue;	/* disabled */
		ram_base = (unsigned long)(cr >> 49) << 21;
		ram_size = ((unsigned long)(cr >> 33) + 1) << 21;
		if (ram_base >= 0x20000000)
			continue;	/* high memory (ignore) */
		printk(" CR%d:%016Lx", i, cr);
		txboard_add_phys_region(ram_base, ram_size);
	}
	printk(" TR:%09Lx\n", tx4938_sdramcptr->tr);

	/* SRAM */
	if (pcode == 0x4938 && tx4938_sramcptr->cr & 1) {
		unsigned int size = 0x800;
		unsigned long base =
			(tx4938_sramcptr->cr >> (39-11)) & ~(size - 1);
		 txboard_add_phys_region(base, size);
	}

	/* IRC */
	/* disable interrupt control */
	tx4938_ircptr->cer = 0;

	/* TMR */
	/* disable all timers */
	for (i = 0; i < TX4938_NR_TMR; i++) {
		tx4938_tmrptr(i)->tcr  = 0x00000020;
		tx4938_tmrptr(i)->tisr = 0;
		tx4938_tmrptr(i)->cpra = 0xffffffff;
		tx4938_tmrptr(i)->itmr = 0;
		tx4938_tmrptr(i)->ccdr = 0;
		tx4938_tmrptr(i)->pgmr = 0;
	}

	/* enable DMA */
	TX4938_WR64(0xff1fb150, TX4938_DMA_MCR_MSTEN);
	TX4938_WR64(0xff1fb950, TX4938_DMA_MCR_MSTEN);

	/* PIO */
	tx4938_pioptr->maskcpu = 0;
	tx4938_pioptr->maskext = 0;

	/* TX4938 internal registers */
	if (request_resource(&iomem_resource, &tx4938_reg_resource))
		printk("request resource for internal registers failed\n");
}

#ifdef CONFIG_PCI
static inline void tx4938_report_pcic_status1(struct tx4938_pcic_reg *pcicptr)
{
	unsigned short pcistatus = (unsigned short)(pcicptr->pcistatus >> 16);
	unsigned long g2pstatus = pcicptr->g2pstatus;
	unsigned long pcicstatus = pcicptr->pcicstatus;
	static struct {
		unsigned long flag;
		const char *str;
	} pcistat_tbl[] = {
		{ PCI_STATUS_DETECTED_PARITY,	"DetectedParityError" },
		{ PCI_STATUS_SIG_SYSTEM_ERROR,	"SignaledSystemError" },
		{ PCI_STATUS_REC_MASTER_ABORT,	"ReceivedMasterAbort" },
		{ PCI_STATUS_REC_TARGET_ABORT,	"ReceivedTargetAbort" },
		{ PCI_STATUS_SIG_TARGET_ABORT,	"SignaledTargetAbort" },
		{ PCI_STATUS_PARITY,	"MasterParityError" },
	}, g2pstat_tbl[] = {
		{ TX4938_PCIC_G2PSTATUS_TTOE,	"TIOE" },
		{ TX4938_PCIC_G2PSTATUS_RTOE,	"RTOE" },
	}, pcicstat_tbl[] = {
		{ TX4938_PCIC_PCICSTATUS_PME,	"PME" },
		{ TX4938_PCIC_PCICSTATUS_TLB,	"TLB" },
		{ TX4938_PCIC_PCICSTATUS_NIB,	"NIB" },
		{ TX4938_PCIC_PCICSTATUS_ZIB,	"ZIB" },
		{ TX4938_PCIC_PCICSTATUS_PERR,	"PERR" },
		{ TX4938_PCIC_PCICSTATUS_SERR,	"SERR" },
		{ TX4938_PCIC_PCICSTATUS_GBE,	"GBE" },
		{ TX4938_PCIC_PCICSTATUS_IWB,	"IWB" },
	};
	int i;

	printk("pcistat:%04x(", pcistatus);
	for (i = 0; i < ARRAY_SIZE(pcistat_tbl); i++)
		if (pcistatus & pcistat_tbl[i].flag)
			printk("%s ", pcistat_tbl[i].str);
	printk("), g2pstatus:%08lx(", g2pstatus);
	for (i = 0; i < ARRAY_SIZE(g2pstat_tbl); i++)
		if (g2pstatus & g2pstat_tbl[i].flag)
			printk("%s ", g2pstat_tbl[i].str);
	printk("), pcicstatus:%08lx(", pcicstatus);
	for (i = 0; i < ARRAY_SIZE(pcicstat_tbl); i++)
		if (pcicstatus & pcicstat_tbl[i].flag)
			printk("%s ", pcicstat_tbl[i].str);
	printk(")\n");
}

void tx4938_report_pcic_status(void)
{
	int i;
	struct tx4938_pcic_reg *pcicptr;
	for (i = 0; (pcicptr = get_tx4938_pcicptr(i)) != NULL; i++)
		tx4938_report_pcic_status1(pcicptr);
}

#endif /* CONFIG_PCI */

/* We use onchip r4k counter or TMR timer as our system wide timer
 * interrupt running at 100HZ. */

extern void __init rtc_rx5c348_init(int chipid);
void __init rbtx4938_time_init(void)
{
	rtc_rx5c348_init(RBTX4938_SRTC_CHIPID);
	mips_hpt_frequency = txx9_cpu_clock / 2;
}

void __init toshiba_rbtx4938_setup(void)
{
	unsigned long long pcfg;
	char *argptr;

	iomem_resource.end = 0xffffffff;	/* 4GB */

	if (txx9_master_clock == 0)
		txx9_master_clock = 25000000; /* 25MHz */
	tx4938_board_setup();
	/* setup irq stuff */
	TX4938_WR(TX4938_MKA(TX4938_IRC_IRDM0), 0x00000000);	/* irq trigger */
	TX4938_WR(TX4938_MKA(TX4938_IRC_IRDM1), 0x00000000);	/* irq trigger */
	/* setup serial stuff */
	TX4938_WR(0xff1ff314, 0x00000000);	/* h/w flow control off */
	TX4938_WR(0xff1ff414, 0x00000000);	/* h/w flow control off */

#ifndef CONFIG_PCI
	set_io_port_base(RBTX4938_ETHER_BASE);
#endif

#ifdef CONFIG_SERIAL_TXX9
	{
		extern int early_serial_txx9_setup(struct uart_port *port);
		int i;
		struct uart_port req;
		for(i = 0; i < 2; i++) {
			memset(&req, 0, sizeof(req));
			req.line = i;
			req.iotype = UPIO_MEM;
			req.membase = (char *)(0xff1ff300 + i * 0x100);
			req.mapbase = 0xff1ff300 + i * 0x100;
			req.irq = 32 + i;
			req.flags |= UPF_BUGGY_UART /*HAVE_CTS_LINE*/;
			req.uartclk = 50000000;
			early_serial_txx9_setup(&req);
		}
	}
#ifdef CONFIG_SERIAL_TXX9_CONSOLE
        argptr = prom_getcmdline();
        if (strstr(argptr, "console=") == NULL) {
                strcat(argptr, " console=ttyS0,38400");
        }
#endif
#endif

#ifdef CONFIG_TOSHIBA_RBTX4938_MPLEX_PIO58_61
	printk("PIOSEL: disabling both ata and nand selection\n");
	local_irq_disable();
	tx4938_ccfgptr->pcfg &= ~(TX4938_PCFG_NDF_SEL | TX4938_PCFG_ATA_SEL);
#endif

#ifdef CONFIG_TOSHIBA_RBTX4938_MPLEX_NAND
	printk("PIOSEL: enabling nand selection\n");
	tx4938_ccfgptr->pcfg |= TX4938_PCFG_NDF_SEL;
	tx4938_ccfgptr->pcfg &= ~TX4938_PCFG_ATA_SEL;
#endif

#ifdef CONFIG_TOSHIBA_RBTX4938_MPLEX_ATA
	printk("PIOSEL: enabling ata selection\n");
	tx4938_ccfgptr->pcfg |= TX4938_PCFG_ATA_SEL;
	tx4938_ccfgptr->pcfg &= ~TX4938_PCFG_NDF_SEL;
#endif

#ifdef CONFIG_IP_PNP
	argptr = prom_getcmdline();
	if (strstr(argptr, "ip=") == NULL) {
		strcat(argptr, " ip=any");
	}
#endif


#ifdef CONFIG_FB
	{
		conswitchp = &dummy_con;
	}
#endif

	rbtx4938_spi_setup();
	pcfg = tx4938_ccfgptr->pcfg;	/* updated */
	/* fixup piosel */
	if ((pcfg & (TX4938_PCFG_ATA_SEL | TX4938_PCFG_NDF_SEL)) ==
	    TX4938_PCFG_ATA_SEL) {
		*rbtx4938_piosel_ptr = (*rbtx4938_piosel_ptr & 0x03) | 0x04;
	}
	else if ((pcfg & (TX4938_PCFG_ATA_SEL | TX4938_PCFG_NDF_SEL)) ==
	    TX4938_PCFG_NDF_SEL) {
		*rbtx4938_piosel_ptr = (*rbtx4938_piosel_ptr & 0x03) | 0x08;
	}
	else {
		*rbtx4938_piosel_ptr &= ~(0x08 | 0x04);
	}

	rbtx4938_fpga_resource.name = "FPGA Registers";
	rbtx4938_fpga_resource.start = CPHYSADDR(RBTX4938_FPGA_REG_ADDR);
	rbtx4938_fpga_resource.end = CPHYSADDR(RBTX4938_FPGA_REG_ADDR) + 0xffff;
	rbtx4938_fpga_resource.flags = IORESOURCE_MEM | IORESOURCE_BUSY;
	if (request_resource(&iomem_resource, &rbtx4938_fpga_resource))
		printk("request resource for fpga failed\n");

	/* disable all OnBoard I/O interrupts */
	*rbtx4938_imask_ptr = 0;

	_machine_restart = rbtx4938_machine_restart;
	_machine_halt = rbtx4938_machine_halt;
	pm_power_off = rbtx4938_machine_power_off;

	*rbtx4938_led_ptr = 0xff;
	printk("RBTX4938 --- FPGA(Rev %02x)", *rbtx4938_fpga_rev_ptr);
	printk(" DIPSW:%02x,%02x\n",
	       *rbtx4938_dipsw_ptr, *rbtx4938_bdipsw_ptr);
}

#ifdef CONFIG_PROC_FS
extern void spi_eeprom_proc_create(struct proc_dir_entry *dir, int chipid);
static int __init tx4938_spi_proc_setup(void)
{
	struct proc_dir_entry *tx4938_spi_eeprom_dir;

	tx4938_spi_eeprom_dir = proc_mkdir("spi_eeprom", 0);

	if (!tx4938_spi_eeprom_dir)
		return -ENOMEM;

	/* don't allow user access to RBTX4938_SEEPROM1_CHIPID
	 * as it contains eth0 and eth1 MAC addresses
	 */
	spi_eeprom_proc_create(tx4938_spi_eeprom_dir, RBTX4938_SEEPROM2_CHIPID);
	spi_eeprom_proc_create(tx4938_spi_eeprom_dir, RBTX4938_SEEPROM3_CHIPID);

	return 0;
}

__initcall(tx4938_spi_proc_setup);
#endif
