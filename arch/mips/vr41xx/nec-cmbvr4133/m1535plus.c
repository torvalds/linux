/*
 * arch/mips/vr41xx/nec-cmbvr4133/m1535plus.c
 *
 * Initialize for ALi M1535+(included M5229 and M5237).
 *
 * Author: Yoichi Yuasa <yyuasa@mvista.com, or source@mvista.com> and
 *         Alex Sapkov <asapkov@ru.mvista.com>
 *
 * 2003-2004 (c) MontaVista, Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 *
 * Support for NEC-CMBVR4133 in 2.6
 * Author: Manish Lachwani (mlachwani@mvista.com)
 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/serial.h>

#include <asm/vr41xx/cmbvr4133.h>
#include <linux/pci.h>
#include <asm/io.h>

#define CONFIG_PORT(port)	((port) ? 0x3f0 : 0x370)
#define DATA_PORT(port)		((port) ? 0x3f1 : 0x371)
#define INDEX_PORT(port)	CONFIG_PORT(port)

#define ENTER_CONFIG_MODE(port)				\
	do {						\
		outb_p(0x51, CONFIG_PORT(port));	\
		outb_p(0x23, CONFIG_PORT(port));	\
	} while(0)

#define SELECT_LOGICAL_DEVICE(port, dev_no)		\
	do {						\
		outb_p(0x07, INDEX_PORT(port));		\
		outb_p((dev_no), DATA_PORT(port));	\
	} while(0)

#define WRITE_CONFIG_DATA(port, index, data)		\
	do {						\
		outb_p((index), INDEX_PORT(port));	\
		outb_p((data), DATA_PORT(port));	\
	} while(0)

#define EXIT_CONFIG_MODE(port)	outb(0xbb, CONFIG_PORT(port))

#define PCI_CONFIG_ADDR	KSEG1ADDR(0x0f000c18)
#define PCI_CONFIG_DATA	KSEG1ADDR(0x0f000c14)

#ifdef CONFIG_BLK_DEV_FD

void __devinit ali_m1535plus_fdc_init(int port)
{
	ENTER_CONFIG_MODE(port);
	SELECT_LOGICAL_DEVICE(port, 0);		/* FDC */
	WRITE_CONFIG_DATA(port, 0x30, 0x01);	/* FDC: enable */
	WRITE_CONFIG_DATA(port, 0x60, 0x03);	/* I/O port base: 0x3f0 */
	WRITE_CONFIG_DATA(port, 0x61, 0xf0);
	WRITE_CONFIG_DATA(port, 0x70, 0x06);	/* IRQ: 6 */
	WRITE_CONFIG_DATA(port, 0x74, 0x02);	/* DMA: channel 2 */
	WRITE_CONFIG_DATA(port, 0xf0, 0x08);
	WRITE_CONFIG_DATA(port, 0xf1, 0x00);
	WRITE_CONFIG_DATA(port, 0xf2, 0xff);
	WRITE_CONFIG_DATA(port, 0xf4, 0x00);
	EXIT_CONFIG_MODE(port);
}

#endif

void __devinit ali_m1535plus_parport_init(int port)
{
	ENTER_CONFIG_MODE(port);
	SELECT_LOGICAL_DEVICE(port, 3);		/* Parallel Port */
	WRITE_CONFIG_DATA(port, 0x30, 0x01);
	WRITE_CONFIG_DATA(port, 0x60, 0x03);	/* I/O port base: 0x378 */
	WRITE_CONFIG_DATA(port, 0x61, 0x78);
	WRITE_CONFIG_DATA(port, 0x70, 0x07);	/* IRQ: 7 */
	WRITE_CONFIG_DATA(port, 0x74, 0x04);	/* DMA: None */
	WRITE_CONFIG_DATA(port, 0xf0, 0x8c);	/* IRQ polarity: Active Low */
	WRITE_CONFIG_DATA(port, 0xf1, 0xc5);
	EXIT_CONFIG_MODE(port);
}

void __devinit ali_m1535plus_keyboard_init(int port)
{
	ENTER_CONFIG_MODE(port);
	SELECT_LOGICAL_DEVICE(port, 7);		/* KEYBOARD */
	WRITE_CONFIG_DATA(port, 0x30, 0x01);	/* KEYBOARD: eable */
	WRITE_CONFIG_DATA(port, 0x70, 0x01);	/* IRQ: 1 */
	WRITE_CONFIG_DATA(port, 0x72, 0x0c);	/* PS/2 Mouse IRQ: 12 */
	WRITE_CONFIG_DATA(port, 0xf0, 0x00);
	EXIT_CONFIG_MODE(port);
}

void __devinit ali_m1535plus_hotkey_init(int port)
{
	ENTER_CONFIG_MODE(port);
	SELECT_LOGICAL_DEVICE(port, 0xc);	/* HOTKEY */
	WRITE_CONFIG_DATA(port, 0x30, 0x00);
	WRITE_CONFIG_DATA(port, 0xf0, 0x35);
	WRITE_CONFIG_DATA(port, 0xf1, 0x14);
	WRITE_CONFIG_DATA(port, 0xf2, 0x11);
	WRITE_CONFIG_DATA(port, 0xf3, 0x71);
	WRITE_CONFIG_DATA(port, 0xf5, 0x05);
	EXIT_CONFIG_MODE(port);
}

void ali_m1535plus_init(struct pci_dev *dev)
{
	pci_write_config_byte(dev, 0x40, 0x18); /* PCI Interface Control */
	pci_write_config_byte(dev, 0x41, 0xc0); /* PS2 keyb & mouse enable */
	pci_write_config_byte(dev, 0x42, 0x41); /* ISA bus cycle control */
	pci_write_config_byte(dev, 0x43, 0x00); /* ISA bus cycle control 2 */
	pci_write_config_byte(dev, 0x44, 0x5d); /* IDE enable & IRQ 14 */
	pci_write_config_byte(dev, 0x45, 0x0b); /* PCI int polling mode */
	pci_write_config_byte(dev, 0x47, 0x00); /* BIOS chip select control */

	/* IRQ routing */
	pci_write_config_byte(dev, 0x48, 0x03); /* INTA IRQ10, INTB disable */
	pci_write_config_byte(dev, 0x49, 0x00); /* INTC and INTD disable */
	pci_write_config_byte(dev, 0x4a, 0x00); /* INTE and INTF disable */
	pci_write_config_byte(dev, 0x4b, 0x90); /* Audio IRQ11, Modem disable */

	pci_write_config_word(dev, 0x50, 0x4000); /* Parity check IDE enable */
	pci_write_config_word(dev, 0x52, 0x0000); /* USB & RTC disable */
	pci_write_config_word(dev, 0x54, 0x0002); /* ??? no info */
	pci_write_config_word(dev, 0x56, 0x0002); /* PCS1J signal disable */

	pci_write_config_byte(dev, 0x59, 0x00); /* PCSDS */
	pci_write_config_byte(dev, 0x5a, 0x00);
	pci_write_config_byte(dev, 0x5b, 0x00);
	pci_write_config_word(dev, 0x5c, 0x0000);
	pci_write_config_byte(dev, 0x5e, 0x00);
	pci_write_config_byte(dev, 0x5f, 0x00);
	pci_write_config_word(dev, 0x60, 0x0000);

	pci_write_config_byte(dev, 0x6c, 0x00);
	pci_write_config_byte(dev, 0x6d, 0x48); /* ROM address mapping */
	pci_write_config_byte(dev, 0x6e, 0x00); /* ??? what for? */

	pci_write_config_byte(dev, 0x70, 0x12); /* Serial IRQ control */
	pci_write_config_byte(dev, 0x71, 0xEF); /* DMA channel select */
	pci_write_config_byte(dev, 0x72, 0x03); /* USB IDSEL */
	pci_write_config_byte(dev, 0x73, 0x00); /* ??? no info */

	/*
	 * IRQ setup ALi M5237 USB Host Controller
	 * IRQ: 9
	 */
	pci_write_config_byte(dev, 0x74, 0x01); /* USB IRQ9 */

	pci_write_config_byte(dev, 0x75, 0x1f); /* IDE2 IRQ 15  */
	pci_write_config_byte(dev, 0x76, 0x80); /* ACPI disable */
	pci_write_config_byte(dev, 0x77, 0x40); /* Modem disable */
	pci_write_config_dword(dev, 0x78, 0x20000000); /* Pin select 2 */
	pci_write_config_byte(dev, 0x7c, 0x00); /* Pin select 3 */
	pci_write_config_byte(dev, 0x81, 0x00); /* ID read/write control */
	pci_write_config_byte(dev, 0x90, 0x00); /* PCI PM block control */
	pci_write_config_word(dev, 0xa4, 0x0000); /* PMSCR */

#ifdef CONFIG_BLK_DEV_FD
	ali_m1535plus_fdc_init(1);
#endif

	ali_m1535plus_keyboard_init(1);
	ali_m1535plus_hotkey_init(1);
}

static inline void ali_config_writeb(u8 reg, u8 val, int devfn)
{
	u32 data;
	int shift;

	writel((1 << 16) | (devfn << 8) | (reg & 0xfc) | 1UL, PCI_CONFIG_ADDR);
        data = readl(PCI_CONFIG_DATA);

	shift = (reg & 3) << 3;
	data &= ~(0xff << shift);
	data |= (((u32)val) << shift);

	writel(data, PCI_CONFIG_DATA);
}

static inline u8 ali_config_readb(u8 reg, int devfn)
{
	u32 data;

	writel((1 << 16) | (devfn << 8) | (reg & 0xfc) | 1UL, PCI_CONFIG_ADDR);
	data = readl(PCI_CONFIG_DATA);

	return (u8)(data >> ((reg & 3) << 3));
}

static inline u16 ali_config_readw(u8 reg, int devfn)
{
	u32 data;

	writel((1 << 16) | (devfn << 8) | (reg & 0xfc) | 1UL, PCI_CONFIG_ADDR);
	data = readl(PCI_CONFIG_DATA);

	return (u16)(data >> ((reg & 2) << 3));
}

int vr4133_rockhopper = 0;
void __init ali_m5229_preinit(void)
{
	if (ali_config_readw(PCI_VENDOR_ID, 16) == PCI_VENDOR_ID_AL &&
	    ali_config_readw(PCI_DEVICE_ID, 16) == PCI_DEVICE_ID_AL_M1533) {
		printk(KERN_INFO "Found an NEC Rockhopper \n");
		vr4133_rockhopper = 1;
		/*
		 * Enable ALi M5229 IDE Controller (both channels)
		 * IDSEL: A27
		 */
		ali_config_writeb(0x58, 0x4c, 16);
	}
}

void __init ali_m5229_init(struct pci_dev *dev)
{
	/*
	 * Enable Primary/Secondary Channel Cable Detect 40-Pin
	 */
	pci_write_config_word(dev, 0x4a, 0xc023);

	/*
	 * Set only the 3rd byteis for the master IDE's cycle and
	 * enable Internal IDE Function
	 */
	pci_write_config_byte(dev, 0x50, 0x23); /* Class code attr register */

	pci_write_config_byte(dev, 0x09, 0xff); /* Set native mode & stuff */
	pci_write_config_byte(dev, 0x52, 0x00); /* use timing registers */
	pci_write_config_byte(dev, 0x58, 0x02); /* Primary addr setup timing */
	pci_write_config_byte(dev, 0x59, 0x22); /* Primary cmd block timing */
	pci_write_config_byte(dev, 0x5a, 0x22); /* Pr drv 0 R/W timing */
	pci_write_config_byte(dev, 0x5b, 0x22); /* Pr drv 1 R/W timing */
	pci_write_config_byte(dev, 0x5c, 0x02); /* Sec addr setup timing */
	pci_write_config_byte(dev, 0x5d, 0x22); /* Sec cmd block timing */
	pci_write_config_byte(dev, 0x5e, 0x22); /* Sec drv 0 R/W timing */
	pci_write_config_byte(dev, 0x5f, 0x22); /* Sec drv 1 R/W timing */
	pci_write_config_byte(dev, PCI_LATENCY_TIMER, 0x20);
	pci_write_config_word(dev, PCI_COMMAND,
	                           PCI_COMMAND_PARITY | PCI_COMMAND_MASTER |
				   PCI_COMMAND_IO);
}

