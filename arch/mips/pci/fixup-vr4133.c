/*
 * arch/mips/vr41xx/nec-cmbvr4133/pci_fixup.c
 *
 * The NEC CMB-VR4133 Board specific PCI fixups.
 *
 * Author: Yoichi Yuasa <yyuasa@mvista.com, or source@mvista.com> and
 *         Alex Sapkov <asapkov@ru.mvista.com>
 *
 * 2003-2004 (c) MontaVista, Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 *
 * Modified for support in 2.6
 * Author: Manish Lachwani (mlachwani@mvista.com)
 *
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/pci.h>

#include <asm/io.h>
#include <asm/vr41xx/cmbvr4133.h>

extern int vr4133_rockhopper;
extern void ali_m1535plus_init(struct pci_dev *dev);
extern void ali_m5229_init(struct pci_dev *dev);

/* Do platform specific device initialization at pci_enable_device() time */
int pcibios_plat_dev_init(struct pci_dev *dev)
{
	/*
	 * We have to reset AMD PCnet adapter on Rockhopper since
	 * PMON leaves it enabled and generating interrupts. This leads
	 * to a lock if some PCI device driver later enables the IRQ line
	 * shared with PCnet and there is no AMD PCnet driver to catch its
	 * interrupts.
	 */
#ifdef CONFIG_ROCKHOPPER
	if (dev->vendor == PCI_VENDOR_ID_AMD &&
		dev->device == PCI_DEVICE_ID_AMD_LANCE) {
		inl(pci_resource_start(dev, 0) + 0x18);
	}
#endif

	/*
	 * we have to open the bridges' windows down to 0 because otherwise
 	 * we cannot access ISA south bridge I/O registers that get mapped from
	 * 0. for example, 8259 PIC would be unaccessible without that
	 */
	if(dev->vendor == PCI_VENDOR_ID_INTEL && dev->device == PCI_DEVICE_ID_INTEL_S21152BB) {
		pci_write_config_byte(dev, PCI_IO_BASE, 0);
		if(dev->bus->number == 0) {
			pci_write_config_word(dev, PCI_IO_BASE_UPPER16, 0);
		} else {
			pci_write_config_word(dev, PCI_IO_BASE_UPPER16, 1);
		}
	}

	return 0;
}

/*
 * M1535 IRQ mapping
 * Feel free to change this, although it shouldn't be needed
 */
#define M1535_IRQ_INTA  7
#define M1535_IRQ_INTB  9
#define M1535_IRQ_INTC  10
#define M1535_IRQ_INTD  11

#define M1535_IRQ_USB   9
#define M1535_IRQ_IDE   14
#define M1535_IRQ_IDE2  15
#define M1535_IRQ_PS2   12
#define M1535_IRQ_RTC   8
#define M1535_IRQ_FDC   6
#define M1535_IRQ_AUDIO 5
#define M1535_IRQ_COM1  4
#define M1535_IRQ_COM2  4
#define M1535_IRQ_IRDA  3
#define M1535_IRQ_KBD   1
#define M1535_IRQ_TMR   0

/* Rockhopper "slots" assignment; this is hard-coded ... */
#define ROCKHOPPER_M5451_SLOT  1
#define ROCKHOPPER_M1535_SLOT  2
#define ROCKHOPPER_M5229_SLOT  11
#define ROCKHOPPER_M5237_SLOT  15
#define ROCKHOPPER_PMU_SLOT    12
/* ... and hard-wired. */
#define ROCKHOPPER_PCI1_SLOT   3
#define ROCKHOPPER_PCI2_SLOT   4
#define ROCKHOPPER_PCI3_SLOT   5
#define ROCKHOPPER_PCI4_SLOT   6
#define ROCKHOPPER_PCNET_SLOT  1

#define M1535_IRQ_MASK(n) (1 << (n))

#define M1535_IRQ_EDGE  (M1535_IRQ_MASK(M1535_IRQ_TMR)  | \
                         M1535_IRQ_MASK(M1535_IRQ_KBD)  | \
                         M1535_IRQ_MASK(M1535_IRQ_COM1) | \
                         M1535_IRQ_MASK(M1535_IRQ_COM2) | \
                         M1535_IRQ_MASK(M1535_IRQ_IRDA) | \
                         M1535_IRQ_MASK(M1535_IRQ_RTC)  | \
                         M1535_IRQ_MASK(M1535_IRQ_FDC)  | \
                         M1535_IRQ_MASK(M1535_IRQ_PS2))

#define M1535_IRQ_LEVEL (M1535_IRQ_MASK(M1535_IRQ_IDE)  | \
                         M1535_IRQ_MASK(M1535_IRQ_USB)  | \
                         M1535_IRQ_MASK(M1535_IRQ_INTA) | \
                         M1535_IRQ_MASK(M1535_IRQ_INTB) | \
                         M1535_IRQ_MASK(M1535_IRQ_INTC) | \
                         M1535_IRQ_MASK(M1535_IRQ_INTD))

struct irq_map_entry {
	u16 bus;
	u8 slot;
	u8 irq;
};
static struct irq_map_entry int_map[] = {
	{1, ROCKHOPPER_M5451_SLOT, M1535_IRQ_AUDIO},	/* Audio controller */
	{1, ROCKHOPPER_PCI1_SLOT, M1535_IRQ_INTD},	/* PCI slot #1 */
	{1, ROCKHOPPER_PCI2_SLOT, M1535_IRQ_INTC},	/* PCI slot #2 */
	{1, ROCKHOPPER_M5237_SLOT, M1535_IRQ_USB},	/* USB host controller */
	{1, ROCKHOPPER_M5229_SLOT, IDE_PRIMARY_IRQ},	/* IDE controller */
	{2, ROCKHOPPER_PCNET_SLOT, M1535_IRQ_INTD},	/* AMD Am79c973 on-board
							   ethernet */
	{2, ROCKHOPPER_PCI3_SLOT, M1535_IRQ_INTB},	/* PCI slot #3 */
	{2, ROCKHOPPER_PCI4_SLOT, M1535_IRQ_INTC}	/* PCI slot #4 */
};

static int pci_intlines[] =
    { M1535_IRQ_INTA, M1535_IRQ_INTB, M1535_IRQ_INTC, M1535_IRQ_INTD };

/* Determine the Rockhopper IRQ line number for the PCI device */
int rockhopper_get_irq(struct pci_dev *dev, u8 pin, u8 slot)
{
	struct pci_bus *bus;
	int i;

	bus = dev->bus;
	if (bus == NULL)
		return -1;

	for (i = 0; i < sizeof (int_map) / sizeof (int_map[0]); i++) {
		if (int_map[i].bus == bus->number && int_map[i].slot == slot) {
			int line;
			for (line = 0; line < 4; line++)
				if (pci_intlines[line] == int_map[i].irq)
					break;
			if (line < 4)
				return pci_intlines[(line + (pin - 1)) % 4];
			else
				return int_map[i].irq;
		}
	}
	return -1;
}

#ifdef CONFIG_ROCKHOPPER
void i8259_init(void)
{
	outb(0x11, 0x20);		/* Master ICW1 */
	outb(I8259_IRQ_BASE, 0x21);	/* Master ICW2 */
	outb(0x04, 0x21);		/* Master ICW3 */
	outb(0x01, 0x21);		/* Master ICW4 */
	outb(0xff, 0x21);		/* Master IMW */

	outb(0x11, 0xa0);		/* Slave ICW1 */
	outb(I8259_IRQ_BASE + 8, 0xa1);	/* Slave ICW2 */
	outb(0x02, 0xa1);		/* Slave ICW3 */
	outb(0x01, 0xa1);		/* Slave ICW4 */
	outb(0xff, 0xa1);		/* Slave IMW */

	outb(0x00, 0x4d0);
	outb(0x02, 0x4d1);	/* USB IRQ9 is level */
}
#endif

int __init pcibios_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	extern int pci_probe_only;
	pci_probe_only = 1;

#ifdef CONFIG_ROCKHOPPER
	if( dev->bus->number == 1 && vr4133_rockhopper )  {
		if(slot == ROCKHOPPER_PCI1_SLOT || slot == ROCKHOPPER_PCI2_SLOT)
			dev->irq = CMBVR41XX_INTA_IRQ;
		else
			dev->irq = rockhopper_get_irq(dev, pin, slot);
	} else
		dev->irq = CMBVR41XX_INTA_IRQ;
#else
	dev->irq = CMBVR41XX_INTA_IRQ;
#endif

	return dev->irq;
}

DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_AL, PCI_DEVICE_ID_AL_M1533, ali_m1535plus_init);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_AL, PCI_DEVICE_ID_AL_M5229, ali_m5229_init);


