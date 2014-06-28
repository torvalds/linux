/*
 * linux/arch/mips/txx9/pci.c
 *
 * Based on linux/arch/mips/txx9/rbtx4927/setup.c,
 *	    linux/arch/mips/txx9/rbtx4938/setup.c,
 *	    and RBTX49xx patch from CELF patch archive.
 *
 * Copyright 2001-2005 MontaVista Software Inc.
 * Copyright (C) 1996, 97, 2001, 04  Ralf Baechle (ralf@linux-mips.org)
 * (C) Copyright TOSHIBA CORPORATION 2000-2001, 2004-2007
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/io.h>
#include <asm/txx9/generic.h>
#include <asm/txx9/pci.h>
#ifdef CONFIG_TOSHIBA_FPCIB0
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <asm/i8259.h>
#include <asm/txx9/smsc_fdc37m81x.h>
#endif

static int __init
early_read_config_word(struct pci_controller *hose,
		       int top_bus, int bus, int devfn, int offset, u16 *value)
{
	struct pci_dev fake_dev;
	struct pci_bus fake_bus;

	fake_dev.bus = &fake_bus;
	fake_dev.sysdata = hose;
	fake_dev.devfn = devfn;
	fake_bus.number = bus;
	fake_bus.sysdata = hose;
	fake_bus.ops = hose->pci_ops;

	if (bus != top_bus)
		/* Fake a parent bus structure. */
		fake_bus.parent = &fake_bus;
	else
		fake_bus.parent = NULL;

	return pci_read_config_word(&fake_dev, offset, value);
}

int __init txx9_pci66_check(struct pci_controller *hose, int top_bus,
			    int current_bus)
{
	u32 pci_devfn;
	unsigned short vid;
	int cap66 = -1;
	u16 stat;

	/* It seems SLC90E66 needs some time after PCI reset... */
	mdelay(80);

	printk(KERN_INFO "PCI: Checking 66MHz capabilities...\n");

	for (pci_devfn = 0; pci_devfn < 0xff; pci_devfn++) {
		if (PCI_FUNC(pci_devfn))
			continue;
		if (early_read_config_word(hose, top_bus, current_bus,
					   pci_devfn, PCI_VENDOR_ID, &vid) !=
		    PCIBIOS_SUCCESSFUL)
			continue;
		if (vid == 0xffff)
			continue;

		/* check 66MHz capability */
		if (cap66 < 0)
			cap66 = 1;
		if (cap66) {
			early_read_config_word(hose, top_bus, current_bus,
					       pci_devfn, PCI_STATUS, &stat);
			if (!(stat & PCI_STATUS_66MHZ)) {
				printk(KERN_DEBUG
				       "PCI: %02x:%02x not 66MHz capable.\n",
				       current_bus, pci_devfn);
				cap66 = 0;
				break;
			}
		}
	}
	return cap66 > 0;
}

static struct resource primary_pci_mem_res[2] = {
	{ .name = "PCI MEM" },
	{ .name = "PCI MMIO" },
};
static struct resource primary_pci_io_res = { .name = "PCI IO" };
struct pci_controller txx9_primary_pcic = {
	.mem_resource = &primary_pci_mem_res[0],
	.io_resource = &primary_pci_io_res,
};

#ifdef CONFIG_64BIT
int txx9_pci_mem_high __initdata = 1;
#else
int txx9_pci_mem_high __initdata;
#endif

/*
 * allocate pci_controller and resources.
 * mem_base, io_base: physical address.	 0 for auto assignment.
 * mem_size and io_size means max size on auto assignment.
 * pcic must be &txx9_primary_pcic or NULL.
 */
struct pci_controller *__init
txx9_alloc_pci_controller(struct pci_controller *pcic,
			  unsigned long mem_base, unsigned long mem_size,
			  unsigned long io_base, unsigned long io_size)
{
	struct pcic {
		struct pci_controller c;
		struct resource r_mem[2];
		struct resource r_io;
	} *new = NULL;
	int min_size = 0x10000;

	if (!pcic) {
		new = kzalloc(sizeof(*new), GFP_KERNEL);
		if (!new)
			return NULL;
		new->r_mem[0].name = "PCI mem";
		new->r_mem[1].name = "PCI mmio";
		new->r_io.name = "PCI io";
		new->c.mem_resource = new->r_mem;
		new->c.io_resource = &new->r_io;
		pcic = &new->c;
	} else
		BUG_ON(pcic != &txx9_primary_pcic);
	pcic->io_resource->flags = IORESOURCE_IO;

	/*
	 * for auto assignment, first search a (big) region for PCI
	 * MEM, then search a region for PCI IO.
	 */
	if (mem_base) {
		pcic->mem_resource[0].start = mem_base;
		pcic->mem_resource[0].end = mem_base + mem_size - 1;
		if (request_resource(&iomem_resource, &pcic->mem_resource[0]))
			goto free_and_exit;
	} else {
		unsigned long min = 0, max = 0x20000000; /* low 512MB */
		if (!mem_size) {
			/* default size for auto assignment */
			if (txx9_pci_mem_high)
				mem_size = 0x20000000;	/* mem:512M(max) */
			else
				mem_size = 0x08000000;	/* mem:128M(max) */
		}
		if (txx9_pci_mem_high) {
			min = 0x20000000;
			max = 0xe0000000;
		}
		/* search free region for PCI MEM */
		for (; mem_size >= min_size; mem_size /= 2) {
			if (allocate_resource(&iomem_resource,
					      &pcic->mem_resource[0],
					      mem_size, min, max,
					      mem_size, NULL, NULL) == 0)
				break;
		}
		if (mem_size < min_size)
			goto free_and_exit;
	}

	pcic->mem_resource[1].flags = IORESOURCE_MEM | IORESOURCE_BUSY;
	if (io_base) {
		pcic->mem_resource[1].start = io_base;
		pcic->mem_resource[1].end = io_base + io_size - 1;
		if (request_resource(&iomem_resource, &pcic->mem_resource[1]))
			goto release_and_exit;
	} else {
		if (!io_size)
			/* default size for auto assignment */
			io_size = 0x01000000;	/* io:16M(max) */
		/* search free region for PCI IO in low 512MB */
		for (; io_size >= min_size; io_size /= 2) {
			if (allocate_resource(&iomem_resource,
					      &pcic->mem_resource[1],
					      io_size, 0, 0x20000000,
					      io_size, NULL, NULL) == 0)
				break;
		}
		if (io_size < min_size)
			goto release_and_exit;
		io_base = pcic->mem_resource[1].start;
	}

	pcic->mem_resource[0].flags = IORESOURCE_MEM;
	if (pcic == &txx9_primary_pcic &&
	    mips_io_port_base == (unsigned long)-1) {
		/* map ioport 0 to PCI I/O space address 0 */
		set_io_port_base(IO_BASE + pcic->mem_resource[1].start);
		pcic->io_resource->start = 0;
		pcic->io_offset = 0;	/* busaddr == ioaddr */
		pcic->io_map_base = IO_BASE + pcic->mem_resource[1].start;
	} else {
		/* physaddr to ioaddr */
		pcic->io_resource->start =
			io_base - (mips_io_port_base - IO_BASE);
		pcic->io_offset = io_base - (mips_io_port_base - IO_BASE);
		pcic->io_map_base = mips_io_port_base;
	}
	pcic->io_resource->end = pcic->io_resource->start + io_size - 1;

	pcic->mem_offset = 0;	/* busaddr == physaddr */

	printk(KERN_INFO "PCI: IO %pR MEM %pR\n",
	       &pcic->mem_resource[1], &pcic->mem_resource[0]);

	/* register_pci_controller() will request MEM resource */
	release_resource(&pcic->mem_resource[0]);
	return pcic;
 release_and_exit:
	release_resource(&pcic->mem_resource[0]);
 free_and_exit:
	kfree(new);
	printk(KERN_ERR "PCI: Failed to allocate resources.\n");
	return NULL;
}

static int __init
txx9_arch_pci_init(void)
{
	PCIBIOS_MIN_IO = 0x8000;	/* reseve legacy I/O space */
	return 0;
}
arch_initcall(txx9_arch_pci_init);

/* IRQ/IDSEL mapping */
int txx9_pci_option =
#ifdef CONFIG_PICMG_PCI_BACKPLANE_DEFAULT
	TXX9_PCI_OPT_PICMG |
#endif
	TXX9_PCI_OPT_CLK_AUTO;

enum txx9_pci_err_action txx9_pci_err_action = TXX9_PCI_ERR_REPORT;

#ifdef CONFIG_TOSHIBA_FPCIB0
static irqreturn_t i8259_interrupt(int irq, void *dev_id)
{
	int isairq;

	isairq = i8259_irq();
	if (unlikely(isairq <= I8259A_IRQ_BASE))
		return IRQ_NONE;
	generic_handle_irq(isairq);
	return IRQ_HANDLED;
}

static int txx9_i8259_irq_setup(int irq)
{
	int err;

	init_i8259_irqs();
	err = request_irq(irq, &i8259_interrupt, IRQF_SHARED,
			  "cascade(i8259)", (void *)(long)irq);
	if (!err)
		printk(KERN_INFO "PCI-ISA bridge PIC (irq %d)\n", irq);
	return err;
}

static void quirk_slc90e66_bridge(struct pci_dev *dev)
{
	int irq;	/* PCI/ISA Bridge interrupt */
	u8 reg_64;
	u32 reg_b0;
	u8 reg_e1;
	irq = pcibios_map_irq(dev, PCI_SLOT(dev->devfn), 1); /* INTA */
	if (!irq)
		return;
	txx9_i8259_irq_setup(irq);
	pci_read_config_byte(dev, 0x64, &reg_64);
	pci_read_config_dword(dev, 0xb0, &reg_b0);
	pci_read_config_byte(dev, 0xe1, &reg_e1);
	/* serial irq control */
	reg_64 = 0xd0;
	/* serial irq pin */
	reg_b0 |= 0x00010000;
	/* ide irq on isa14 */
	reg_e1 &= 0xf0;
	reg_e1 |= 0x0d;
	pci_write_config_byte(dev, 0x64, reg_64);
	pci_write_config_dword(dev, 0xb0, reg_b0);
	pci_write_config_byte(dev, 0xe1, reg_e1);

	smsc_fdc37m81x_init(0x3f0);
	smsc_fdc37m81x_config_beg();
	smsc_fdc37m81x_config_set(SMSC_FDC37M81X_DNUM,
				  SMSC_FDC37M81X_KBD);
	smsc_fdc37m81x_config_set(SMSC_FDC37M81X_INT, 1);
	smsc_fdc37m81x_config_set(SMSC_FDC37M81X_INT2, 12);
	smsc_fdc37m81x_config_set(SMSC_FDC37M81X_ACTIVE,
				  1);
	smsc_fdc37m81x_config_end();
}

static void quirk_slc90e66_ide(struct pci_dev *dev)
{
	unsigned char dat;
	int regs[2] = {0x41, 0x43};
	int i;

	/* SMSC SLC90E66 IDE uses irq 14, 15 (default) */
	pci_write_config_byte(dev, PCI_INTERRUPT_LINE, 14);
	pci_read_config_byte(dev, PCI_INTERRUPT_LINE, &dat);
	printk(KERN_INFO "PCI: %s: IRQ %02x", pci_name(dev), dat);
	/* enable SMSC SLC90E66 IDE */
	for (i = 0; i < ARRAY_SIZE(regs); i++) {
		pci_read_config_byte(dev, regs[i], &dat);
		pci_write_config_byte(dev, regs[i], dat | 0x80);
		pci_read_config_byte(dev, regs[i], &dat);
		printk(KERN_CONT " IDETIM%d %02x", i, dat);
	}
	pci_read_config_byte(dev, 0x5c, &dat);
	/*
	 * !!! DO NOT REMOVE THIS COMMENT IT IS REQUIRED BY SMSC !!!
	 *
	 * This line of code is intended to provide the user with a work
	 * around solution to the anomalies cited in SMSC's anomaly sheet
	 * entitled, "SLC90E66 Functional Rev.J_0.1 Anomalies"".
	 *
	 * !!! DO NOT REMOVE THIS COMMENT IT IS REQUIRED BY SMSC !!!
	 */
	dat |= 0x01;
	pci_write_config_byte(dev, 0x5c, dat);
	pci_read_config_byte(dev, 0x5c, &dat);
	printk(KERN_CONT " REG5C %02x", dat);
	printk(KERN_CONT "\n");
}
#endif /* CONFIG_TOSHIBA_FPCIB0 */

static void tc35815_fixup(struct pci_dev *dev)
{
	/* This device may have PM registers but not they are not supported. */
	if (dev->pm_cap) {
		dev_info(&dev->dev, "PM disabled\n");
		dev->pm_cap = 0;
	}
}

static void final_fixup(struct pci_dev *dev)
{
	unsigned char bist;

	/* Do build-in self test */
	if (pci_read_config_byte(dev, PCI_BIST, &bist) == PCIBIOS_SUCCESSFUL &&
	    (bist & PCI_BIST_CAPABLE)) {
		unsigned long timeout;
		pci_set_power_state(dev, PCI_D0);
		printk(KERN_INFO "PCI: %s BIST...", pci_name(dev));
		pci_write_config_byte(dev, PCI_BIST, PCI_BIST_START);
		timeout = jiffies + HZ * 2;	/* timeout after 2 sec */
		do {
			pci_read_config_byte(dev, PCI_BIST, &bist);
			if (time_after(jiffies, timeout))
				break;
		} while (bist & PCI_BIST_START);
		if (bist & (PCI_BIST_CODE_MASK | PCI_BIST_START))
			printk(KERN_CONT "failed. (0x%x)\n", bist);
		else
			printk(KERN_CONT "OK.\n");
	}
}

#ifdef CONFIG_TOSHIBA_FPCIB0
#define PCI_DEVICE_ID_EFAR_SLC90E66_0 0x9460
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_EFAR, PCI_DEVICE_ID_EFAR_SLC90E66_0,
	quirk_slc90e66_bridge);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_EFAR, PCI_DEVICE_ID_EFAR_SLC90E66_1,
	quirk_slc90e66_ide);
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_EFAR, PCI_DEVICE_ID_EFAR_SLC90E66_1,
	quirk_slc90e66_ide);
#endif
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_TOSHIBA_2,
			PCI_DEVICE_ID_TOSHIBA_TC35815_NWU, tc35815_fixup);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_TOSHIBA_2,
			PCI_DEVICE_ID_TOSHIBA_TC35815_TX4939, tc35815_fixup);
DECLARE_PCI_FIXUP_FINAL(PCI_ANY_ID, PCI_ANY_ID, final_fixup);
DECLARE_PCI_FIXUP_RESUME(PCI_ANY_ID, PCI_ANY_ID, final_fixup);

int pcibios_plat_dev_init(struct pci_dev *dev)
{
	return 0;
}

int __init pcibios_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	return txx9_board_vec->pci_map_irq(dev, slot, pin);
}

char * (*txx9_board_pcibios_setup)(char *str) __initdata;

char *__init txx9_pcibios_setup(char *str)
{
	if (txx9_board_pcibios_setup && !txx9_board_pcibios_setup(str))
		return NULL;
	if (!strcmp(str, "picmg")) {
		/* PICMG compliant backplane (TOSHIBA JMB-PICMG-ATX
		   (5V or 3.3V), JMB-PICMG-L2 (5V only), etc.) */
		txx9_pci_option |= TXX9_PCI_OPT_PICMG;
		return NULL;
	} else if (!strcmp(str, "nopicmg")) {
		/* non-PICMG compliant backplane (TOSHIBA
		   RBHBK4100,RBHBK4200, Interface PCM-PCM05, etc.) */
		txx9_pci_option &= ~TXX9_PCI_OPT_PICMG;
		return NULL;
	} else if (!strncmp(str, "clk=", 4)) {
		char *val = str + 4;
		txx9_pci_option &= ~TXX9_PCI_OPT_CLK_MASK;
		if (strcmp(val, "33") == 0)
			txx9_pci_option |= TXX9_PCI_OPT_CLK_33;
		else if (strcmp(val, "66") == 0)
			txx9_pci_option |= TXX9_PCI_OPT_CLK_66;
		else /* "auto" */
			txx9_pci_option |= TXX9_PCI_OPT_CLK_AUTO;
		return NULL;
	} else if (!strncmp(str, "err=", 4)) {
		if (!strcmp(str + 4, "panic"))
			txx9_pci_err_action = TXX9_PCI_ERR_PANIC;
		else if (!strcmp(str + 4, "ignore"))
			txx9_pci_err_action = TXX9_PCI_ERR_IGNORE;
		return NULL;
	}
	return str;
}
