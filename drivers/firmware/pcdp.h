/*
 * Definitions for PCDP-defined console devices
 *
 * v1.0a: http://www.dig64.org/specifications/DIG64_HCDPv10a_01.pdf
 * v2.0:  http://www.dig64.org/specifications/DIG64_HCDPv20_042804.pdf
 *
 * (c) Copyright 2002, 2004 Hewlett-Packard Development Company, L.P.
 *	Khalid Aziz <khalid.aziz@hp.com>
 *	Bjorn Helgaas <bjorn.helgaas@hp.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define PCDP_CONSOLE			0
#define PCDP_DEBUG			1
#define PCDP_CONSOLE_OUTPUT		2
#define PCDP_CONSOLE_INPUT		3

#define PCDP_UART			(0 << 3)
#define PCDP_VGA			(1 << 3)
#define PCDP_USB			(2 << 3)

/* pcdp_uart.type and pcdp_device.type */
#define PCDP_CONSOLE_UART		(PCDP_UART | PCDP_CONSOLE)
#define PCDP_DEBUG_UART			(PCDP_UART | PCDP_DEBUG)
#define PCDP_CONSOLE_VGA		(PCDP_VGA  | PCDP_CONSOLE_OUTPUT)
#define PCDP_CONSOLE_USB		(PCDP_USB  | PCDP_CONSOLE_INPUT)

/* pcdp_uart.flags */
#define PCDP_UART_EDGE_SENSITIVE	(1 << 0)
#define PCDP_UART_ACTIVE_LOW		(1 << 1)
#define PCDP_UART_PRIMARY_CONSOLE	(1 << 2)
#define PCDP_UART_IRQ			(1 << 6) /* in pci_func for rev < 3 */
#define PCDP_UART_PCI			(1 << 7) /* in pci_func for rev < 3 */

struct pcdp_uart {
	u8				type;
	u8				bits;
	u8				parity;
	u8				stop_bits;
	u8				pci_seg;
	u8				pci_bus;
	u8				pci_dev;
	u8				pci_func;
	u64				baud;
	struct acpi_generic_address	addr;
	u16				pci_dev_id;
	u16				pci_vendor_id;
	u32				gsi;
	u32				clock_rate;
	u8				pci_prog_intfc;
	u8				flags;
};

struct pcdp_vga {
	u8			count;		/* address space descriptors */
};

/* pcdp_device.flags */
#define PCDP_PRIMARY_CONSOLE	1

struct pcdp_device {
	u8			type;
	u8			flags;
	u16			length;
	u16			efi_index;
};

struct pcdp {
	u8			signature[4];
	u32			length;
	u8			rev;		/* PCDP v2.0 is rev 3 */
	u8			chksum;
	u8			oemid[6];
	u8			oem_tabid[8];
	u32			oem_rev;
	u8			creator_id[4];
	u32			creator_rev;
	u32			num_uarts;
	struct pcdp_uart	uart[0];	/* actual size is num_uarts */
	/* remainder of table is pcdp_device structures */
};
