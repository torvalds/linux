/*      National Semiconductor NS87560UBD Super I/O controller used in
 *      HP [BCJ]x000 workstations.
 *
 *      This chip is a horrid piece of engineering, and National
 *      denies any knowledge of its existence. Thus no datasheet is
 *      available off www.national.com. 
 *
 *	(C) Copyright 2000 Linuxcare, Inc.
 * 	(C) Copyright 2000 Linuxcare Canada, Inc.
 *	(C) Copyright 2000 Martin K. Petersen <mkp@linuxcare.com>
 * 	(C) Copyright 2000 Alex deVries <alex@onefishtwo.ca>
 *      (C) Copyright 2001 John Marvin <jsm fc hp com>
 *      (C) Copyright 2003 Grant Grundler <grundler parisc-linux org>
 *	(C) Copyright 2005 Kyle McMartin <kyle@parisc-linux.org>
 *	(C) Copyright 2006 Helge Deller <deller@gmx.de>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation; either version 2 of
 *	the License, or (at your option) any later version.  
 *
 *	The initial version of this is by Martin Peterson.  Alex deVries
 *	has spent a bit of time trying to coax it into working.
 *
 *      Major changes to get basic interrupt infrastructure working to
 *      hopefully be able to support all SuperIO devices. Currently
 *      works with serial. -- John Marvin <jsm@fc.hp.com>
 *
 *	Converted superio_init() to be a PCI_FIXUP_FINAL callee.
 *         -- Kyle McMartin <kyle@parisc-linux.org>
 */


/* NOTES:
 * 
 * Function 0 is an IDE controller. It is identical to a PC87415 IDE
 * controller (and identifies itself as such).
 *
 * Function 1 is a "Legacy I/O" controller. Under this function is a
 * whole mess of legacy I/O peripherals. Of course, HP hasn't enabled
 * all the functionality in hardware, but the following is available:
 *
 *      Two 16550A compatible serial controllers
 *      An IEEE 1284 compatible parallel port
 *      A floppy disk controller
 *
 * Function 2 is a USB controller.
 *
 * We must be incredibly careful during initialization.  Since all
 * interrupts are routed through function 1 (which is not allowed by
 * the PCI spec), we need to program the PICs on the legacy I/O port
 * *before* we attempt to set up IDE and USB.  @#$!&
 *
 * According to HP, devices are only enabled by firmware if they have
 * a physical device connected.
 *
 * Configuration register bits:
 *     0x5A: FDC, SP1, IDE1, SP2, IDE2, PAR, Reserved, P92
 *     0x5B: RTC, 8259, 8254, DMA1, DMA2, KBC, P61, APM
 *
 */

#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/serial.h>
#include <linux/pci.h>
#include <linux/parport.h>
#include <linux/parport_pc.h>
#include <linux/termios.h>
#include <linux/tty.h>
#include <linux/serial_core.h>
#include <linux/serial_8250.h>
#include <linux/delay.h>

#include <asm/io.h>
#include <asm/hardware.h>
#include <asm/superio.h>

static struct superio_device sio_dev;


#undef DEBUG_SUPERIO_INIT

#ifdef DEBUG_SUPERIO_INIT
#define DBG_INIT(x...)  printk(x)
#else
#define DBG_INIT(x...)
#endif

#define SUPERIO	"SuperIO"
#define PFX	SUPERIO ": "

static irqreturn_t
superio_interrupt(int parent_irq, void *devp)
{
	u8 results;
	u8 local_irq;

	/* Poll the 8259 to see if there's an interrupt. */
	outb (OCW3_POLL,IC_PIC1+0);

	results = inb(IC_PIC1+0);

	/*
	 * Bit    7:	1 = active Interrupt; 0 = no Interrupt pending
	 * Bits 6-3:	zero
	 * Bits 2-0:	highest priority, active requesting interrupt ID (0-7)
	 */
	if ((results & 0x80) == 0) {
		/* I suspect "spurious" interrupts are from unmasking an IRQ.
		 * We don't know if an interrupt was/is pending and thus
		 * just call the handler for that IRQ as if it were pending.
		 */
		return IRQ_NONE;
	}

	/* Check to see which device is interrupting */
	local_irq = results & 0x0f;

	if (local_irq == 2 || local_irq > 7) {
		printk(KERN_ERR PFX "slave interrupted!\n");
		return IRQ_HANDLED;
	}

	if (local_irq == 7) {

		/* Could be spurious. Check in service bits */

		outb(OCW3_ISR,IC_PIC1+0);
		results = inb(IC_PIC1+0);
		if ((results & 0x80) == 0) { /* if ISR7 not set: spurious */
			printk(KERN_WARNING PFX "spurious interrupt!\n");
			return IRQ_HANDLED;
		}
	}

	/* Call the appropriate device's interrupt */
	__do_IRQ(local_irq);

	/* set EOI - forces a new interrupt if a lower priority device
	 * still needs service.
	 */
	outb((OCW2_SEOI|local_irq),IC_PIC1 + 0);
	return IRQ_HANDLED;
}

/* Initialize Super I/O device */
static void
superio_init(struct pci_dev *pcidev)
{
	struct superio_device *sio = &sio_dev;
	struct pci_dev *pdev = sio->lio_pdev;
	u16 word;
	int ret;

	if (sio->suckyio_irq_enabled)
		return;

	BUG_ON(!pdev);
	BUG_ON(!sio->usb_pdev);

	/* use the IRQ iosapic found for USB INT D... */
	pdev->irq = sio->usb_pdev->irq;

	/* ...then properly fixup the USB to point at suckyio PIC */
	sio->usb_pdev->irq = superio_fixup_irq(sio->usb_pdev);

	printk(KERN_INFO PFX "Found NS87560 Legacy I/O device at %s (IRQ %i) \n",
	       pci_name(pdev), pdev->irq);

	pci_read_config_dword (pdev, SIO_SP1BAR, &sio->sp1_base);
	sio->sp1_base &= ~1;
	printk(KERN_INFO PFX "Serial port 1 at 0x%x\n", sio->sp1_base);

	pci_read_config_dword (pdev, SIO_SP2BAR, &sio->sp2_base);
	sio->sp2_base &= ~1;
	printk(KERN_INFO PFX "Serial port 2 at 0x%x\n", sio->sp2_base);

	pci_read_config_dword (pdev, SIO_PPBAR, &sio->pp_base);
	sio->pp_base &= ~1;
	printk(KERN_INFO PFX "Parallel port at 0x%x\n", sio->pp_base);

	pci_read_config_dword (pdev, SIO_FDCBAR, &sio->fdc_base);
	sio->fdc_base &= ~1;
	printk(KERN_INFO PFX "Floppy controller at 0x%x\n", sio->fdc_base);
	pci_read_config_dword (pdev, SIO_ACPIBAR, &sio->acpi_base);
	sio->acpi_base &= ~1;
	printk(KERN_INFO PFX "ACPI at 0x%x\n", sio->acpi_base);

	request_region (IC_PIC1, 0x1f, "pic1");
	request_region (IC_PIC2, 0x1f, "pic2");
	request_region (sio->acpi_base, 0x1f, "acpi");

	/* Enable the legacy I/O function */
	pci_read_config_word (pdev, PCI_COMMAND, &word);
	word |= PCI_COMMAND_SERR | PCI_COMMAND_PARITY | PCI_COMMAND_IO;
	pci_write_config_word (pdev, PCI_COMMAND, word);

	pci_set_master (pdev);
	ret = pci_enable_device(pdev);
	BUG_ON(ret < 0);	/* not too much we can do about this... */

	/*
	 * Next project is programming the onboard interrupt controllers.
	 * PDC hasn't done this for us, since it's using polled I/O.
	 *
	 * XXX Use dword writes to avoid bugs in Elroy or Suckyio Config
	 *     space access.  PCI is by nature a 32-bit bus and config
	 *     space can be sensitive to that.
	 */

	/* 0x64 - 0x67 :
		DMA Rtg 2
		DMA Rtg 3
		DMA Chan Ctl
		TRIGGER_1    == 0x82   USB & IDE level triggered, rest to edge
	*/
	pci_write_config_dword (pdev, 0x64,         0x82000000U);

	/* 0x68 - 0x6b :
		TRIGGER_2    == 0x00   all edge triggered (not used)
		CFG_IR_SER   == 0x43   SerPort1 = IRQ3, SerPort2 = IRQ4
		CFG_IR_PF    == 0x65   ParPort  = IRQ5, FloppyCtlr = IRQ6
		CFG_IR_IDE   == 0x07   IDE1 = IRQ7, reserved
	*/
	pci_write_config_dword (pdev, TRIGGER_2,    0x07654300U);

	/* 0x6c - 0x6f :
		CFG_IR_INTAB == 0x00
		CFG_IR_INTCD == 0x10   USB = IRQ1
		CFG_IR_PS2   == 0x00
		CFG_IR_FXBUS == 0x00
	*/
	pci_write_config_dword (pdev, CFG_IR_INTAB, 0x00001000U);

	/* 0x70 - 0x73 :
		CFG_IR_USB   == 0x00  not used. USB is connected to INTD.
		CFG_IR_ACPI  == 0x00  not used.
		DMA Priority == 0x4c88  Power on default value. NFC.
	*/
	pci_write_config_dword (pdev, CFG_IR_USB, 0x4c880000U);

	/* PIC1 Initialization Command Word register programming */
	outb (0x11,IC_PIC1+0);	/* ICW1: ICW4 write req | ICW1 */
	outb (0x00,IC_PIC1+1);	/* ICW2: interrupt vector table - not used */
	outb (0x04,IC_PIC1+1);	/* ICW3: Cascade */
	outb (0x01,IC_PIC1+1);	/* ICW4: x86 mode */

	/* PIC1 Program Operational Control Words */
	outb (0xff,IC_PIC1+1);	/* OCW1: Mask all interrupts */
	outb (0xc2,IC_PIC1+0);  /* OCW2: priority (3-7,0-2) */

	/* PIC2 Initialization Command Word register programming */
	outb (0x11,IC_PIC2+0);	/* ICW1: ICW4 write req | ICW1 */
	outb (0x00,IC_PIC2+1);	/* ICW2: N/A */
	outb (0x02,IC_PIC2+1);	/* ICW3: Slave ID code */
	outb (0x01,IC_PIC2+1);	/* ICW4: x86 mode */
		
	/* Program Operational Control Words */
	outb (0xff,IC_PIC1+1);	/* OCW1: Mask all interrupts */
	outb (0x68,IC_PIC1+0);	/* OCW3: OCW3 select | ESMM | SMM */

	/* Write master mask reg */
	outb (0xff,IC_PIC1+1);

	/* Setup USB power regulation */
	outb(1, sio->acpi_base + USB_REG_CR);
	if (inb(sio->acpi_base + USB_REG_CR) & 1)
		printk(KERN_INFO PFX "USB regulator enabled\n");
	else
		printk(KERN_ERR PFX "USB regulator not initialized!\n");

	if (request_irq(pdev->irq, superio_interrupt, IRQF_DISABLED,
			SUPERIO, (void *)sio)) {

		printk(KERN_ERR PFX "could not get irq\n");
		BUG();
		return;
	}

	sio->suckyio_irq_enabled = 1;
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_NS, PCI_DEVICE_ID_NS_87560_LIO, superio_init);

static void superio_disable_irq(unsigned int irq)
{
	u8 r8;

	if ((irq < 1) || (irq == 2) || (irq > 7)) {
		printk(KERN_ERR PFX "Illegal irq number.\n");
		BUG();
		return;
	}

	/* Mask interrupt */

	r8 = inb(IC_PIC1+1);
	r8 |= (1 << irq);
	outb (r8,IC_PIC1+1);
}

static void superio_enable_irq(unsigned int irq)
{
	u8 r8;

	if ((irq < 1) || (irq == 2) || (irq > 7)) {
		printk(KERN_ERR PFX "Illegal irq number (%d).\n", irq);
		BUG();
		return;
	}

	/* Unmask interrupt */
	r8 = inb(IC_PIC1+1);
	r8 &= ~(1 << irq);
	outb (r8,IC_PIC1+1);
}

static unsigned int superio_startup_irq(unsigned int irq)
{
	superio_enable_irq(irq);
	return 0;
}

static struct hw_interrupt_type superio_interrupt_type = {
	.typename =	SUPERIO,
	.startup =	superio_startup_irq,
	.shutdown =	superio_disable_irq,
	.enable =	superio_enable_irq,
	.disable =	superio_disable_irq,
	.ack =		no_ack_irq,
	.end =		no_end_irq,
};

#ifdef DEBUG_SUPERIO_INIT
static unsigned short expected_device[3] = {
	PCI_DEVICE_ID_NS_87415,
	PCI_DEVICE_ID_NS_87560_LIO,
	PCI_DEVICE_ID_NS_87560_USB
};
#endif

int superio_fixup_irq(struct pci_dev *pcidev)
{
	int local_irq, i;

#ifdef DEBUG_SUPERIO_INIT
	int fn;
	fn = PCI_FUNC(pcidev->devfn);

	/* Verify the function number matches the expected device id. */
	if (expected_device[fn] != pcidev->device) {
		BUG();
		return -1;
	}
	printk("superio_fixup_irq(%s) ven 0x%x dev 0x%x from %p\n",
		pci_name(pcidev),
		pcidev->vendor, pcidev->device,
		__builtin_return_address(0));
#endif

	for (i = 0; i < 16; i++) {
		struct irq_desc *desc = irq_to_desc(i);

		desc->chip = &superio_interrupt_type;
	}

	/*
	 * We don't allocate a SuperIO irq for the legacy IO function,
	 * since it is a "bridge". Instead, we will allocate irq's for
	 * each legacy device as they are initialized.
	 */

	switch(pcidev->device) {
	case PCI_DEVICE_ID_NS_87415:		/* Function 0 */
		local_irq = IDE_IRQ;
		break;
	case PCI_DEVICE_ID_NS_87560_LIO:	/* Function 1 */
		sio_dev.lio_pdev = pcidev;	/* save for superio_init() */
		return -1;
	case PCI_DEVICE_ID_NS_87560_USB:	/* Function 2 */
		sio_dev.usb_pdev = pcidev;	/* save for superio_init() */
		local_irq = USB_IRQ;
		break;
	default:
		local_irq = -1;
		BUG();
		break;
	}

	return local_irq;
}

static void __init superio_serial_init(void)
{
#ifdef CONFIG_SERIAL_8250
	int retval;
	struct uart_port serial_port;

	memset(&serial_port, 0, sizeof(serial_port));
	serial_port.iotype	= UPIO_PORT;
	serial_port.type	= PORT_16550A;
	serial_port.uartclk	= 115200*16;
	serial_port.fifosize	= 16;

	/* serial port #1 */
	serial_port.iobase	= sio_dev.sp1_base;
	serial_port.irq		= SP1_IRQ;
	serial_port.line	= 0;
	retval = early_serial_setup(&serial_port);
	if (retval < 0) {
		printk(KERN_WARNING PFX "Register Serial #0 failed.\n");
		return;
	}

	/* serial port #2 */
	serial_port.iobase	= sio_dev.sp2_base;
	serial_port.irq		= SP2_IRQ;
	serial_port.line	= 1;
	retval = early_serial_setup(&serial_port);
	if (retval < 0)
		printk(KERN_WARNING PFX "Register Serial #1 failed.\n");
#endif /* CONFIG_SERIAL_8250 */
}


static void __init superio_parport_init(void)
{
#ifdef CONFIG_PARPORT_PC
	if (!parport_pc_probe_port(sio_dev.pp_base,
			0 /*base_hi*/,
			PAR_IRQ, 
			PARPORT_DMA_NONE /* dma */,
			NULL /*struct pci_dev* */),
			0 /* shared irq flags */ )

		printk(KERN_WARNING PFX "Probing parallel port failed.\n");
#endif	/* CONFIG_PARPORT_PC */
}


static void superio_fixup_pci(struct pci_dev *pdev)
{
	u8 prog;

	pdev->class |= 0x5;
	pci_write_config_byte(pdev, PCI_CLASS_PROG, pdev->class);

	pci_read_config_byte(pdev, PCI_CLASS_PROG, &prog);
	printk("PCI: Enabled native mode for NS87415 (pif=0x%x)\n", prog);
}
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_NS, PCI_DEVICE_ID_NS_87415, superio_fixup_pci);


static int __init
superio_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	struct superio_device *sio = &sio_dev;

	/*
	** superio_probe(00:0e.0) ven 0x100b dev 0x2 sv 0x0 sd 0x0 class 0x1018a
	** superio_probe(00:0e.1) ven 0x100b dev 0xe sv 0x0 sd 0x0 class 0x68000
	** superio_probe(00:0e.2) ven 0x100b dev 0x12 sv 0x0 sd 0x0 class 0xc0310
	*/
	DBG_INIT("superio_probe(%s) ven 0x%x dev 0x%x sv 0x%x sd 0x%x class 0x%x\n",
		pci_name(dev),
		dev->vendor, dev->device,
		dev->subsystem_vendor, dev->subsystem_device,
		dev->class);

	BUG_ON(!sio->suckyio_irq_enabled);	/* Enabled by PCI_FIXUP_FINAL */

	if (dev->device == PCI_DEVICE_ID_NS_87560_LIO) {	/* Function 1 */
		superio_parport_init();
		superio_serial_init();
		/* REVISIT XXX : superio_fdc_init() ? */
		return 0;
	} else if (dev->device == PCI_DEVICE_ID_NS_87415) {	/* Function 0 */
		DBG_INIT("superio_probe: ignoring IDE 87415\n");
	} else if (dev->device == PCI_DEVICE_ID_NS_87560_USB) {	/* Function 2 */
		DBG_INIT("superio_probe: ignoring USB OHCI controller\n");
	} else {
		DBG_INIT("superio_probe: WTF? Fire Extinguisher?\n");
	}

	/* Let appropriate other driver claim this device. */
	return -ENODEV;
}

static const struct pci_device_id superio_tbl[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_NS, PCI_DEVICE_ID_NS_87560_LIO) },
	{ PCI_DEVICE(PCI_VENDOR_ID_NS, PCI_DEVICE_ID_NS_87560_USB) },
	{ PCI_DEVICE(PCI_VENDOR_ID_NS, PCI_DEVICE_ID_NS_87415) },
	{ 0, }
};

static struct pci_driver superio_driver = {
	.name =         SUPERIO,
	.id_table =     superio_tbl,
	.probe =        superio_probe,
};

static int __init superio_modinit(void)
{
	return pci_register_driver(&superio_driver);
}

static void __exit superio_exit(void)
{
	pci_unregister_driver(&superio_driver);
}

module_init(superio_modinit);
module_exit(superio_exit);
