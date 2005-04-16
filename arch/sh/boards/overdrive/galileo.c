/* 
 * Copyright (C) 2000 David J. Mckay (david.mckay@st.com)
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.                            
 *
 * This file contains the PCI routines required for the Galileo GT6411 
 * PCI bridge as used on the Orion and Overdrive boards.
 *
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/types.h>
#include <linux/ioport.h>

#include <asm/overdrive/overdrive.h>
#include <asm/overdrive/gt64111.h>


/* After boot, we shift the Galileo registers so that they appear 
 * in BANK6, along with IO space. This means we can have one contingous
 * lump of PCI address space without these registers appearing in the 
 * middle of them 
 */

#define GT64111_BASE_ADDRESS  0xbb000000
#define GT64111_IO_BASE_ADDRESS  0x1000
/* The GT64111 registers appear at this address to the SH4 after reset */
#define RESET_GT64111_BASE_ADDRESS           0xb4000000

/* Macros used to access the Galileo registers */
#define RESET_GT64111_REG(x) (RESET_GT64111_BASE_ADDRESS+x)
#define GT64111_REG(x) (GT64111_BASE_ADDRESS+x)

#define RESET_GT_WRITE(x,v) writel((v),RESET_GT64111_REG(x))

#define RESET_GT_READ(x) readl(RESET_GT64111_REG(x))

#define GT_WRITE(x,v) writel((v),GT64111_REG(x))
#define GT_WRITE_BYTE(x,v) writeb((v),GT64111_REG(x))
#define GT_WRITE_SHORT(x,v) writew((v),GT64111_REG(x))

#define GT_READ(x)    readl(GT64111_REG(x))
#define GT_READ_BYTE(x)  readb(GT64111_REG(x))
#define GT_READ_SHORT(x) readw(GT64111_REG(x))


/* Where the various SH banks start at */
#define SH_BANK4_ADR 0xb0000000
#define SH_BANK5_ADR 0xb4000000
#define SH_BANK6_ADR 0xb8000000

/* Masks out everything but lines 28,27,26 */
#define BANK_SELECT_MASK 0x1c000000

#define SH4_TO_BANK(x) ( (x) & BANK_SELECT_MASK)

/* 
 * Masks used for address conversaion. Bank 6 is used for IO and 
 * has all the address bits zeroed by the FPGA. Special case this
 */
#define MEMORY_BANK_MASK 0x1fffffff
#define IO_BANK_MASK  0x03ffffff

/* Mark bank 6 as the bank used for IO. You can change this in the FPGA code
 * if you want 
 */
#define IO_BANK_ADR PCI_GTIO_BASE

/* Will select the correct mask to apply depending on the SH$ address */
#define SELECT_BANK_MASK(x) \
   ( (SH4_TO_BANK(x)==SH4_TO_BANK(IO_BANK_ADR)) ? IO_BANK_MASK : MEMORY_BANK_MASK)

/* Converts between PCI space and P2 region */
#define SH4_TO_PCI(x) ((x)&SELECT_BANK_MASK(x))

/* Various macros for figuring out what to stick in the Galileo registers. 
 * You *really* don't want to figure this stuff out by hand, you always get
 * it wrong
 */
#define GT_MEM_LO_ADR(x) ((((unsigned)((x)&SELECT_BANK_MASK(x)))>>21)&0x7ff)
#define GT_MEM_HI_ADR(x) ((((unsigned)((x)&SELECT_BANK_MASK(x)))>>21)&0x7f)
#define GT_MEM_SUB_ADR(x) ((((unsigned)((x)&SELECT_BANK_MASK(x)))>>20)&0xff)

#define PROGRAM_HI_LO(block,a,s) \
    GT_WRITE(block##_LO_DEC_ADR,GT_MEM_LO_ADR(a));\
    GT_WRITE(block##_HI_DEC_ADR,GT_MEM_HI_ADR(a+s-1))

#define PROGRAM_SUB_HI_LO(block,a,s) \
    GT_WRITE(block##_LO_DEC_ADR,GT_MEM_SUB_ADR(a));\
    GT_WRITE(block##_HI_DEC_ADR,GT_MEM_SUB_ADR(a+s-1))

/* We need to set the size, and the offset register */

#define GT_BAR_MASK(x) ((x)&~0xfff)

/* Macro to set up the BAR in the Galileo. Essentially used for the DRAM */
#define PROGRAM_GT_BAR(block,a,s) \
  GT_WRITE(PCI_##block##_BANK_SIZE,GT_BAR_MASK((s-1)));\
  write_config_to_galileo(PCI_CONFIG_##block##_BASE_ADR,\
			     GT_BAR_MASK(a))

#define DISABLE_GT_BAR(block) \
  GT_WRITE(PCI_##block##_BANK_SIZE,0),\
  GT_CONFIG_WRITE(PCI_CONFIG_##block##_BASE_ADR,\
    0x80000000)

/* Macros to disable things we are not going to use */
#define DISABLE_DECODE(x) GT_WRITE(x##_LO_DEC_ADR,0x7ff);\
                          GT_WRITE(x##_HI_DEC_ADR,0x00)

#define DISABLE_SUB_DECODE(x) GT_WRITE(x##_LO_DEC_ADR,0xff);\
                              GT_WRITE(x##_HI_DEC_ADR,0x00)

static void __init reset_pci(void)
{
	/* Set RESET_PCI bit high */
	writeb(readb(OVERDRIVE_CTRL) | ENABLE_PCI_BIT, OVERDRIVE_CTRL);
	udelay(250);

	/* Set RESET_PCI bit low */
	writeb(readb(OVERDRIVE_CTRL) & RESET_PCI_MASK, OVERDRIVE_CTRL);
	udelay(250);

	writeb(readb(OVERDRIVE_CTRL) | ENABLE_PCI_BIT, OVERDRIVE_CTRL);
	udelay(250);
}

static int write_config_to_galileo(int where, u32 val);
#define GT_CONFIG_WRITE(where,val) write_config_to_galileo(where,val)

#define ENABLE_PCI_DRAM


#ifdef TEST_DRAM
/* Test function to check out if the PCI DRAM is working OK */
static int  /* __init */ test_dram(unsigned *base, unsigned size)
{
	unsigned *p = base;
	unsigned *end = (unsigned *) (((unsigned) base) + size);
	unsigned w;

	for (p = base; p < end; p++) {
		*p = 0xffffffff;
		if (*p != 0xffffffff) {
			printk("AAARGH -write failed!!! at %p is %x\n", p,
			       *p);
			return 0;
		}
		*p = 0x0;
		if (*p != 0x0) {
			printk("AAARGH -write failed!!!\n");
			return 0;
		}
	}

	for (p = base; p < end; p++) {
		*p = (unsigned) p;
		if (*p != (unsigned) p) {
			printk("Failed at 0x%p, actually is 0x%x\n", p,
			       *p);
			return 0;
		}
	}

	for (p = base; p < end; p++) {
		w = ((unsigned) p & 0xffff0000);
		*p = w | (w >> 16);
	}

	for (p = base; p < end; p++) {
		w = ((unsigned) p & 0xffff0000);
		w |= (w >> 16);
		if (*p != w) {
			printk
			    ("Failed at 0x%p, should be 0x%x actually is 0x%x\n",
			     p, w, *p);
			return 0;
		}
	}

	return 1;
}
#endif


/* Function to set up and initialise the galileo. This sets up the BARS,
 * maps the DRAM into the address space etc,etc
 */
int __init galileo_init(void)
{
	reset_pci();

	/* Now shift the galileo regs into this block */
	RESET_GT_WRITE(INTERNAL_SPACE_DEC,
		       GT_MEM_LO_ADR(GT64111_BASE_ADDRESS));

	/* Should have a sanity check here, that you can read back  at the new
	 * address what you just wrote 
	 */

	/* Disable decode for all regions */
	DISABLE_DECODE(RAS10);
	DISABLE_DECODE(RAS32);
	DISABLE_DECODE(CS20);
	DISABLE_DECODE(CS3);
	DISABLE_DECODE(PCI_IO);
	DISABLE_DECODE(PCI_MEM0);
	DISABLE_DECODE(PCI_MEM1);

	/* Disable all BARS */
	GT_WRITE(BAR_ENABLE_ADR, 0x1ff);
	DISABLE_GT_BAR(RAS10);
	DISABLE_GT_BAR(RAS32);
	DISABLE_GT_BAR(CS20);
	DISABLE_GT_BAR(CS3);

	/* Tell the BAR where the IO registers now are */
	GT_CONFIG_WRITE(PCI_CONFIG_INT_REG_IO_ADR,GT_BAR_MASK(
					    (GT64111_IO_BASE_ADDRESS &
					     IO_BANK_MASK)));
	/* set up a 112 Mb decode */
	PROGRAM_HI_LO(PCI_MEM0, SH_BANK4_ADR, 112 * 1024 * 1024);

	/* Set up a 32 MB io space decode */
	PROGRAM_HI_LO(PCI_IO, IO_BANK_ADR, 32 * 1024 * 1024);

#ifdef ENABLE_PCI_DRAM
	/* Program up the DRAM configuration - there is DRAM only in bank 0 */
	/* Now set up the DRAM decode */
	PROGRAM_HI_LO(RAS10, PCI_DRAM_BASE, PCI_DRAM_SIZE);
	/* And the sub decode */
	PROGRAM_SUB_HI_LO(RAS0, PCI_DRAM_BASE, PCI_DRAM_SIZE);

	DISABLE_SUB_DECODE(RAS1);

	/* Set refresh rate */
	GT_WRITE(DRAM_BANK0_PARMS, 0x3f);
	GT_WRITE(DRAM_CFG, 0x100);

	/* we have to lob off the top bits rememeber!! */
	PROGRAM_GT_BAR(RAS10, SH4_TO_PCI(PCI_DRAM_BASE), PCI_DRAM_SIZE);

#endif

	/* We are only interested in decoding RAS10 and the Galileo's internal 
	 * registers (as IO) on the PCI bus
	 */
#ifdef ENABLE_PCI_DRAM
	GT_WRITE(BAR_ENABLE_ADR, (~((1 << 8) | (1 << 3))) & 0x1ff);
#else
	GT_WRITE(BAR_ENABLE_ADR, (~(1 << 3)) & 0x1ff);
#endif

	/* Change the class code to host bridge, it actually powers up 
	 * as a memory controller
         */
	GT_CONFIG_WRITE(8, 0x06000011);

	/* Allow the galileo to master the PCI bus */
	GT_CONFIG_WRITE(PCI_COMMAND,
			PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER |
			PCI_COMMAND_IO);


#if 0
        printk("Testing PCI DRAM - ");
	if(test_dram(PCI_DRAM_BASE,PCI_DRAM_SIZE)) {
		printk("Passed\n");
	}else {
		printk("FAILED\n");
	}
#endif
	return 0;

}


#define SET_CONFIG_BITS(bus,devfn,where)\
  ((1<<31) | ((bus) << 16) | ((devfn) << 8) | ((where) & ~3))

#define CONFIG_CMD(dev, where) SET_CONFIG_BITS((dev)->bus->number,(dev)->devfn,where)

/* This write to the galileo config registers, unlike the functions below, can
 * be used before the PCI subsystem has started up
 */
static int __init write_config_to_galileo(int where, u32 val)
{
	GT_WRITE(PCI_CFG_ADR, SET_CONFIG_BITS(0, 0, where));

	GT_WRITE(PCI_CFG_DATA, val);
	return 0;
}

/* We exclude the galileo and slot 31, the galileo because I don't know how to stop
 * the setup code shagging up the setup I have done on it, and 31 because the whole
 * thing locks up if you try to access that slot (which doesn't exist of course anyway
 */

#define EXCLUDED_DEV(dev) ((dev->bus->number==0) && ((PCI_SLOT(dev->devfn)==0) || (PCI_SLOT(dev->devfn) == 31)))

static int galileo_read_config_byte(struct pci_dev *dev, int where,
				    u8 * val)
{

        
	/* I suspect this doesn't work because this drives a special cycle ? */
	if (EXCLUDED_DEV(dev)) {
		*val = 0xff;
		return PCIBIOS_SUCCESSFUL;
	}
	/* Start the config cycle */
	GT_WRITE(PCI_CFG_ADR, CONFIG_CMD(dev, where));
	/* Read back the result */
	*val = GT_READ_BYTE(PCI_CFG_DATA + (where & 3));

	return PCIBIOS_SUCCESSFUL;
}


static int galileo_read_config_word(struct pci_dev *dev, int where,
				    u16 * val)
{

        if (EXCLUDED_DEV(dev)) {
		*val = 0xffff;
		return PCIBIOS_SUCCESSFUL;
	}

	GT_WRITE(PCI_CFG_ADR, CONFIG_CMD(dev, where));
	*val = GT_READ_SHORT(PCI_CFG_DATA + (where & 2));

	return PCIBIOS_SUCCESSFUL;
}


static int galileo_read_config_dword(struct pci_dev *dev, int where,
				     u32 * val)
{
	if (EXCLUDED_DEV(dev)) {
		*val = 0xffffffff;
		return PCIBIOS_SUCCESSFUL;
	}

	GT_WRITE(PCI_CFG_ADR, CONFIG_CMD(dev, where));
	*val = GT_READ(PCI_CFG_DATA);

	return PCIBIOS_SUCCESSFUL;
}

static int galileo_write_config_byte(struct pci_dev *dev, int where,
				     u8 val)
{
	GT_WRITE(PCI_CFG_ADR, CONFIG_CMD(dev, where));

	GT_WRITE_BYTE(PCI_CFG_DATA + (where & 3), val);

	return PCIBIOS_SUCCESSFUL;
}


static int galileo_write_config_word(struct pci_dev *dev, int where,
				     u16 val)
{
	GT_WRITE(PCI_CFG_ADR, CONFIG_CMD(dev, where));

	GT_WRITE_SHORT(PCI_CFG_DATA + (where & 2), val);

	return PCIBIOS_SUCCESSFUL;
}

static int galileo_write_config_dword(struct pci_dev *dev, int where,
				      u32 val)
{
	GT_WRITE(PCI_CFG_ADR, CONFIG_CMD(dev, where));

	GT_WRITE(PCI_CFG_DATA, val);

	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops pci_config_ops = {
	galileo_read_config_byte,
	galileo_read_config_word,
	galileo_read_config_dword,
	galileo_write_config_byte,
	galileo_write_config_word,
	galileo_write_config_dword
};


/* Everything hangs off this */
static struct pci_bus *pci_root_bus;


static u8 __init no_swizzle(struct pci_dev *dev, u8 * pin)
{
	return PCI_SLOT(dev->devfn);
}

static int __init map_od_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	/* Slot 1: Galileo 
	 * Slot 2: PCI Slot 1
	 * Slot 3: PCI Slot 2
	 * Slot 4: ESS
	 */
	switch (slot) {
	case 2: 
		return OVERDRIVE_PCI_IRQ1;
	case 3:
		/* Note this assumes you have a hacked card in slot 2 */
		return OVERDRIVE_PCI_IRQ2;
	case 4:
		return OVERDRIVE_ESS_IRQ;
	default:
		/* printk("PCI: Unexpected IRQ mapping request for slot %d\n", slot); */
		return -1;
	}
}



void __init
pcibios_fixup_pbus_ranges(struct pci_bus *bus, struct pbus_set_ranges_data *ranges)
{
        ranges->io_start -= bus->resource[0]->start;
        ranges->io_end -= bus->resource[0]->start;
        ranges->mem_start -= bus->resource[1]->start;
        ranges->mem_end -= bus->resource[1]->start;
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

void __init pcibios_init(void)
{
	static struct resource galio,galmem;

        /* Allocate the registers used by the Galileo */
        galio.flags = IORESOURCE_IO;
        galio.name  = "Galileo GT64011";
        galmem.flags = IORESOURCE_MEM|IORESOURCE_PREFETCH;
        galmem.name  = "Galileo GT64011 DRAM";

        allocate_resource(&ioport_resource, &galio, 256,
		    GT64111_IO_BASE_ADDRESS,GT64111_IO_BASE_ADDRESS+256, 256, NULL, NULL);
        allocate_resource(&iomem_resource, &galmem,PCI_DRAM_SIZE,
		    PHYSADDR(PCI_DRAM_BASE), PHYSADDR(PCI_DRAM_BASE)+PCI_DRAM_SIZE, 
			     PCI_DRAM_SIZE, NULL, NULL);

  	/* ok, do the scan man */
	pci_root_bus = pci_scan_bus(0, &pci_config_ops, NULL);

        pci_assign_unassigned_resources();
	pci_fixup_irqs(no_swizzle, map_od_irq);

#ifdef TEST_DRAM
        printk("Testing PCI DRAM - ");
	if(test_dram(PCI_DRAM_BASE,PCI_DRAM_SIZE)) {
		printk("Passed\n");
	}else {
		printk("FAILED\n");
	}
#endif

}

char * __init pcibios_setup(char *str)
{
	return str;
}



int pcibios_enable_device(struct pci_dev *dev)
{

	u16 cmd, old_cmd;
	int idx;
	struct resource *r;

	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	old_cmd = cmd;
	for (idx = 0; idx < 6; idx++) {
		r = dev->resource + idx;
		if (!r->start && r->end) {
			printk(KERN_ERR
			       "PCI: Device %s not available because"
			       " of resource collisions\n",
			       pci_name(dev));
			return -EINVAL;
		}
		if (r->flags & IORESOURCE_IO)
			cmd |= PCI_COMMAND_IO;
		if (r->flags & IORESOURCE_MEM)
			cmd |= PCI_COMMAND_MEMORY;
	}
	if (cmd != old_cmd) {
		printk("PCI: enabling device %s (%04x -> %04x)\n",
		       pci_name(dev), old_cmd, cmd);
		pci_write_config_word(dev, PCI_COMMAND, cmd);
	}
	return 0;

}

/* We should do some optimisation work here I think. Ok for now though */
void __init pcibios_fixup_bus(struct pci_bus *bus)
{

}

void pcibios_align_resource(void *data, struct resource *res,
			    unsigned long size)
{
}

void __init pcibios_update_resource(struct pci_dev *dev, struct resource *root,
			     struct resource *res, int resource)
{

	unsigned long where, size;
	u32 reg;
	

	printk("PCI: Assigning %3s %08lx to %s\n",
	       res->flags & IORESOURCE_IO ? "IO" : "MEM",
	       res->start, dev->name);

	where = PCI_BASE_ADDRESS_0 + resource * 4;
	size = res->end - res->start;

	pci_read_config_dword(dev, where, &reg);
	reg = (reg & size) | (((u32) (res->start - root->start)) & ~size);
	pci_write_config_dword(dev, where, reg);
}


void __init pcibios_update_irq(struct pci_dev *dev, int irq)
{
	printk("PCI: Assigning IRQ %02d to %s\n", irq, dev->name);
	pci_write_config_byte(dev, PCI_INTERRUPT_LINE, irq);
}

/*
 *  If we set up a device for bus mastering, we need to check the latency
 *  timer as certain crappy BIOSes forget to set it properly.
 */
unsigned int pcibios_max_latency = 255;

void pcibios_set_master(struct pci_dev *dev)
{
	u8 lat;
	pci_read_config_byte(dev, PCI_LATENCY_TIMER, &lat);
	if (lat < 16)
		lat = (64 <= pcibios_max_latency) ? 64 : pcibios_max_latency;
	else if (lat > pcibios_max_latency)
		lat = pcibios_max_latency;
	else
		return;
	printk("PCI: Setting latency timer of device %s to %d\n", pci_name(dev), lat);
	pci_write_config_byte(dev, PCI_LATENCY_TIMER, lat);
}
