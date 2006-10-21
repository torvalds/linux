/* 
 * Copyright (C) 2001 David J. Mckay (david.mckay@st.com)
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.                            
 *
 * Support functions for the ST40 PCI hardware.
 */

#include <linux/kernel.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/types.h>
#include <asm/pci.h>
#include <linux/irq.h>
#include <linux/interrupt.h>	/* irqreturn_t */

#include "pci-st40.h"

/* This is in P2 of course */
#define ST40PCI_BASE_ADDRESS     (0xb0000000)
#define ST40PCI_MEM_ADDRESS      (ST40PCI_BASE_ADDRESS+0x0)
#define ST40PCI_IO_ADDRESS       (ST40PCI_BASE_ADDRESS+0x06000000)
#define ST40PCI_REG_ADDRESS      (ST40PCI_BASE_ADDRESS+0x07000000)

#define ST40PCI_REG(x) (ST40PCI_REG_ADDRESS+(ST40PCI_##x))
#define ST40PCI_REG_INDEXED(reg, index) 				\
	(ST40PCI_REG(reg##0) +					\
	  ((ST40PCI_REG(reg##1) - ST40PCI_REG(reg##0))*index))

#define ST40PCI_WRITE(reg,val) writel((val),ST40PCI_REG(reg))
#define ST40PCI_WRITE_SHORT(reg,val) writew((val),ST40PCI_REG(reg))
#define ST40PCI_WRITE_BYTE(reg,val) writeb((val),ST40PCI_REG(reg))
#define ST40PCI_WRITE_INDEXED(reg, index, val)				\
	 writel((val), ST40PCI_REG_INDEXED(reg, index));

#define ST40PCI_READ(reg) readl(ST40PCI_REG(reg))
#define ST40PCI_READ_SHORT(reg) readw(ST40PCI_REG(reg))
#define ST40PCI_READ_BYTE(reg) readb(ST40PCI_REG(reg))

#define ST40PCI_SERR_IRQ	64
#define ST40PCI_ERR_IRQ        	65


/* Macros to extract PLL params */
#define PLL_MDIV(reg)  ( ((unsigned)reg) & 0xff )
#define PLL_NDIV(reg) ( (((unsigned)reg)>>8) & 0xff )
#define PLL_PDIV(reg) ( (((unsigned)reg)>>16) & 0x3 )
#define PLL_SETUP(reg) ( (((unsigned)reg)>>19) & 0x1ff )

/* Build up the appropriate settings */
#define PLL_SET(mdiv,ndiv,pdiv,setup) \
( ((mdiv)&0xff) | (((ndiv)&0xff)<<8) | (((pdiv)&3)<<16)| (((setup)&0x1ff)<<19))

#define PLLPCICR (0xbb040000+0x10)

#define PLLPCICR_POWERON (1<<28)
#define PLLPCICR_OUT_EN (1<<29)
#define PLLPCICR_LOCKSELECT (1<<30)
#define PLLPCICR_LOCK (1<<31)


#define PLL_25MHZ 0x793c8512
#define PLL_33MHZ PLL_SET(18,88,3,295)

static void pci_set_rbar_region(unsigned int region,     unsigned long localAddr,
			 unsigned long pciOffset, unsigned long regionSize);

static __init void SetPCIPLL(void)
{
	{
		/* Lets play with the PLL values */
		unsigned long pll1cr1;
		unsigned long mdiv, ndiv, pdiv;
		unsigned long muxcr;
		unsigned int muxcr_ratios[4] = { 8, 16, 21, 1 };
		unsigned int freq;

#define CLKGENA            0xbb040000
#define CLKGENA_PLL2_MUXCR CLKGENA + 0x48
		pll1cr1 = ctrl_inl(PLLPCICR);
		printk("PLL1CR1 %08lx\n", pll1cr1);
		mdiv = PLL_MDIV(pll1cr1);
		ndiv = PLL_NDIV(pll1cr1);
		pdiv = PLL_PDIV(pll1cr1);
		printk("mdiv %02lx ndiv %02lx pdiv %02lx\n", mdiv, ndiv, pdiv);
		freq = ((2*27*ndiv)/mdiv) / (1 << pdiv);
		printk("PLL freq %dMHz\n", freq);
		muxcr = ctrl_inl(CLKGENA_PLL2_MUXCR);
		printk("PCI freq %dMhz\n", freq / muxcr_ratios[muxcr & 3]);
	}
}


struct pci_err {
  unsigned mask;
  const char *error_string;
};

static struct pci_err int_error[]={
  { INT_MNLTDIM,"MNLTDIM: Master non-lock transfer"},
  { INT_TTADI,  "TTADI: Illegal byte enable in I/O transfer"},
  { INT_TMTO,   "TMTO: Target memory read/write timeout"},
  { INT_MDEI,   "MDEI: Master function disable error"},
  { INT_APEDI,  "APEDI: Address parity error"},
  { INT_SDI,    "SDI: SERR detected"},
  { INT_DPEITW, "DPEITW: Data parity error target write"},
  { INT_PEDITR, "PEDITR: PERR detected"},
  { INT_TADIM,  "TADIM: Target abort detected"},
  { INT_MADIM,  "MADIM: Master abort detected"},
  { INT_MWPDI,  "MWPDI: PERR from target at data write"},
  { INT_MRDPEI, "MRDPEI: Master read data parity error"}
};
#define NUM_PCI_INT_ERRS (sizeof(int_error)/sizeof(struct pci_err))

static struct pci_err aint_error[]={
  { AINT_MBI,   "MBI: Master broken"},
  { AINT_TBTOI, "TBTOI: Target bus timeout"},
  { AINT_MBTOI, "MBTOI: Master bus timeout"},
  { AINT_TAI,   "TAI: Target abort"},
  { AINT_MAI,   "MAI: Master abort"},
  { AINT_RDPEI, "RDPEI: Read data parity"},
  { AINT_WDPE,  "WDPE: Write data parity"}
};

#define NUM_PCI_AINT_ERRS (sizeof(aint_error)/sizeof(struct pci_err))

static void print_pci_errors(unsigned reg,struct pci_err *error,int num_errors)
{
  int i;

  for(i=0;i<num_errors;i++) {
    if(reg & error[i].mask) {
      printk("%s\n",error[i].error_string);
    }
  }

}


static char * pci_commands[16]={
	"Int Ack",
	"Special Cycle",
	"I/O Read",
	"I/O Write",
	"Reserved",
	"Reserved",
	"Memory Read",
	"Memory Write",
	"Reserved",
	"Reserved",
	"Configuration Read",
	"Configuration Write",
	"Memory Read Multiple",
	"Dual Address Cycle",
	"Memory Read Line",
	"Memory Write-and-Invalidate"
};

static irqreturn_t st40_pci_irq(int irq, void *dev_instance)
{
	unsigned pci_int, pci_air, pci_cir, pci_aint;
	static int count=0;


	pci_int = ST40PCI_READ(INT);pci_aint = ST40PCI_READ(AINT);
	pci_cir = ST40PCI_READ(CIR);pci_air = ST40PCI_READ(AIR);

	/* Reset state to stop multiple interrupts */
        ST40PCI_WRITE(INT, ~0); ST40PCI_WRITE(AINT, ~0);


	if(++count>1) return IRQ_HANDLED;

	printk("** PCI ERROR **\n");

        if(pci_int) {
		printk("** INT register status\n");
		print_pci_errors(pci_int,int_error,NUM_PCI_INT_ERRS);
	}

        if(pci_aint) {
		printk("** AINT register status\n");
		print_pci_errors(pci_aint,aint_error,NUM_PCI_AINT_ERRS);
	}

	printk("** Address and command info\n");

	printk("** Command  %s : Address 0x%x\n",
	       pci_commands[pci_cir&0xf],pci_air);

	if(pci_cir&CIR_PIOTEM) {
		printk("CIR_PIOTEM:PIO transfer error for master\n");
	}
        if(pci_cir&CIR_RWTET) {
		printk("CIR_RWTET:Read/Write transfer error for target\n");
	}

	return IRQ_HANDLED;
}


/* Rounds a number UP to the nearest power of two. Used for
 * sizing the PCI window.
 */
static u32 r2p2(u32 num)
{
	int i = 31;
	u32 tmp = num;

	if (num == 0)
		return 0;

	do {
		if (tmp & (1 << 31))
			break;
		i--;
		tmp <<= 1;
	} while (i >= 0);

	tmp = 1 << i;
	/* If the original number isn't a power of 2, round it up */
	if (tmp != num)
		tmp <<= 1;

	return tmp;
}

static void __init pci_fixup_ide_bases(struct pci_dev *d)
{
	int i;

	/*
	 * PCI IDE controllers use non-standard I/O port decoding, respect it.
	 */
	if ((d->class >> 8) != PCI_CLASS_STORAGE_IDE)
		return;
	printk("PCI: IDE base address fixup for %s\n", pci_name(d));
	for(i=0; i<4; i++) {
		struct resource *r = &d->resource[i];
		if ((r->start & ~0x80) == 0x374) {
			r->start |= 2;
			r->end = r->start;
		}
	}
}
DECLARE_PCI_FIXUP_HEADER(PCI_ANY_ID, PCI_ANY_ID, pci_fixup_ide_bases);

int __init st40pci_init(unsigned memStart, unsigned memSize)
{
	u32 lsr0;

	SetPCIPLL();

	/* Initialises the ST40 pci subsystem, performing a reset, then programming
	 * up the address space decoders appropriately
	 */

	/* Should reset core here as well methink */

	ST40PCI_WRITE(CR, CR_LOCK_MASK | CR_SOFT_RESET);

	/* Loop while core resets */
	while (ST40PCI_READ(CR) & CR_SOFT_RESET);

	/* Switch off interrupts */
	ST40PCI_WRITE(INTM, 0);
	ST40PCI_WRITE(AINT, 0);

	/* Now, lets reset all the cards on the bus with extreme prejudice */
	ST40PCI_WRITE(CR, CR_LOCK_MASK | CR_RSTCTL);
	udelay(250);

	/* Set bus active, take it out of reset */
	ST40PCI_WRITE(CR, CR_LOCK_MASK | CR_BMAM | CR_CFINT | CR_PFCS | CR_PFE);

	/* The PCI spec says that no access must be made to the bus until 1 second
	 * after reset. This seem ludicrously long, but some delay is needed here
	 */
	mdelay(1000);

	/* Switch off interrupts */
	ST40PCI_WRITE(INTM, 0);
	ST40PCI_WRITE(AINT, 0);

	/* Allow it to be a master */

	ST40PCI_WRITE_SHORT(CSR_CMD,
			    PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER |
			    PCI_COMMAND_IO);

	/* Accesse to the 0xb0000000 -> 0xb6000000 area will go through to 0x10000000 -> 0x16000000
	 * on the PCI bus. This allows a nice 1-1 bus to phys mapping.
	 */


	ST40PCI_WRITE(MBR, 0x10000000);
	/* Always set the max size 128M (actually, it is only 96MB wide) */
	ST40PCI_WRITE(MBMR, 0x07ff0000);

	/* I/O addresses are mapped at 0xb6000000 -> 0xb7000000. These are changed to 0, to 
	 * allow cards that have legacy io such as vga to function correctly. This gives a 
	 * maximum of 64K of io/space as only the bottom 16 bits of the address are copied 
	 * over to the bus  when the transaction is made. 64K of io space is more than enough
	 */
	ST40PCI_WRITE(IOBR, 0x0);
	/* Set up the 64K window */
	ST40PCI_WRITE(IOBMR, 0x0);

	/* Now we set up the mbars so the PCI bus can see the local memory */
	/* Expose a 256M window starting at PCI address 0... */
	ST40PCI_WRITE(CSR_MBAR0, 0);
	ST40PCI_WRITE(LSR0, 0x0fff0001);

	/* ... and set up the initial incomming window to expose all of RAM */
	pci_set_rbar_region(7, memStart, memStart, memSize);

	/* Maximise timeout values */
	ST40PCI_WRITE_BYTE(CSR_TRDY, 0xff);
	ST40PCI_WRITE_BYTE(CSR_RETRY, 0xff);
	ST40PCI_WRITE_BYTE(CSR_MIT, 0xff);

	ST40PCI_WRITE_BYTE(PERF,PERF_MASTER_WRITE_POSTING);

	return 1;
}

char * __init pcibios_setup(char *str)
{
	return str;
}


#define SET_CONFIG_BITS(bus,devfn,where)\
  (((bus) << 16) | ((devfn) << 8) | ((where) & ~3) | (bus!=0))

#define CONFIG_CMD(bus, devfn, where) SET_CONFIG_BITS(bus->number,devfn,where)


static int CheckForMasterAbort(void)
{
	if (ST40PCI_READ(INT) & INT_MADIM) {
		/* Should we clear config space version as well ??? */
		ST40PCI_WRITE(INT, INT_MADIM);
		ST40PCI_WRITE_SHORT(CSR_STATUS, 0);
		return 1;
	}

	return 0;
}

/* Write to config register */
static int st40pci_read(struct pci_bus *bus, unsigned int devfn, int where, int size, u32 * val)
{
	ST40PCI_WRITE(PAR, CONFIG_CMD(bus, devfn, where));
	switch (size) {
		case 1:
			*val = (u8)ST40PCI_READ_BYTE(PDR + (where & 3));
			break;
		case 2:
			*val = (u16)ST40PCI_READ_SHORT(PDR + (where & 2));
			break;
		case 4:
			*val = ST40PCI_READ(PDR);
			break;
	}

	if (CheckForMasterAbort()){
		switch (size) {
			case 1:
				*val = (u8)0xff;
				break;
			case 2:
				*val = (u16)0xffff;
				break;
			case 4:
				*val = 0xffffffff;
				break;
		}
	}

	return PCIBIOS_SUCCESSFUL;
}

static int st40pci_write(struct pci_bus *bus, unsigned int devfn, int where, int size, u32 val)
{
	ST40PCI_WRITE(PAR, CONFIG_CMD(bus, devfn, where));

	switch (size) {
		case 1:
			ST40PCI_WRITE_BYTE(PDR + (where & 3), (u8)val);
			break;
		case 2:
			ST40PCI_WRITE_SHORT(PDR + (where & 2), (u16)val);
			break;
		case 4:
			ST40PCI_WRITE(PDR, val);
			break;
	}

	CheckForMasterAbort();

	return PCIBIOS_SUCCESSFUL;
}

struct pci_ops st40pci_config_ops = {
	.read = 	st40pci_read,
	.write = 	st40pci_write,
};


/* Everything hangs off this */
static struct pci_bus *pci_root_bus;

static int __init pcibios_init(void)
{
	extern unsigned long memory_start, memory_end;

	printk(KERN_ALERT "pci-st40.c: pcibios_init\n");

	if (sh_mv.mv_init_pci != NULL) {
		sh_mv.mv_init_pci();
	}

	/* The pci subsytem needs to know where memory is and how much 
	 * of it there is. I've simply made these globals. A better mechanism
	 * is probably needed.
	 */
	st40pci_init(PHYSADDR(memory_start),
		     PHYSADDR(memory_end) - PHYSADDR(memory_start));

	if (request_irq(ST40PCI_ERR_IRQ, st40_pci_irq, 
                        IRQF_DISABLED, "st40pci", NULL)) {
		printk(KERN_ERR "st40pci: Cannot hook interrupt\n");
		return -EIO;
	}

	/* Enable the PCI interrupts on the device */
	ST40PCI_WRITE(INTM, ~0);
	ST40PCI_WRITE(AINT, ~0);

	/* Map the io address apprioately */
#ifdef CONFIG_HD64465
	hd64465_port_map(PCIBIOS_MIN_IO, (64 * 1024) - PCIBIOS_MIN_IO + 1,
			 ST40_IO_ADDR + PCIBIOS_MIN_IO, 0);
#endif

	/* ok, do the scan man */
	pci_root_bus = pci_scan_bus(0, &st40pci_config_ops, NULL);
	pci_assign_unassigned_resources();

	return 0;
}
subsys_initcall(pcibios_init);

/*
 * Publish a region of local address space over the PCI bus
 * to other devices.
 */
static void pci_set_rbar_region(unsigned int region,     unsigned long localAddr,
			 unsigned long pciOffset, unsigned long regionSize)
{
	unsigned long mask;

	if (region > 7)
		return;

	if (regionSize > (512 * 1024 * 1024))
		return;

	mask = r2p2(regionSize) - 0x10000;

	/* Diable the region (in case currently in use, should never happen) */
	ST40PCI_WRITE_INDEXED(RSR, region, 0);

	/* Start of local address space to publish */
	ST40PCI_WRITE_INDEXED(RLAR, region, PHYSADDR(localAddr) );

	/* Start of region in PCI address space as an offset from MBAR0 */
	ST40PCI_WRITE_INDEXED(RBAR, region, pciOffset);

	/* Size of region */
	ST40PCI_WRITE_INDEXED(RSR, region, mask | 1);
}

