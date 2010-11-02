/*
 * Definitions for PCDP-defined console devices
 *
 * For DIG64_HCDPv10a_01.pdf and DIG64_PCDPv20.pdf (v1.0a and v2.0 resp.),
 * please see <http://www.dig64.org/specifications/>
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
	u16				conout_index;
	u32				reserved;
} __attribute__((packed));

#define PCDP_IF_PCI	1

/* pcdp_if_pci.trans */
#define PCDP_PCI_TRANS_IOPORT	0x02
#define PCDP_PCI_TRANS_MMIO	0x01

struct pcdp_if_pci {
	u8			interconnect;
	u8			reserved;
	u16			length;
	u8			segment;
	u8			bus;
	u8			dev;
	u8			fun;
	u16			dev_id;
	u16			vendor_id;
	u32			acpi_interrupt;
	u64			mmio_tra;
	u64			ioport_tra;
	u8			flags;
	u8			trans;
} __attribute__((packed));

struct pcdp_vga {
	u8			count;		/* address space descriptors */
} __attribute__((packed));

/* pcdp_device.flags */
#define PCDP_PRIMARY_CONSOLE	1

struct pcdp_device {
	u8			type;
	u8			flags;
	u16			length;
	u16			efi_index;
	/* next data is pcdp_if_pci or pcdp_if_acpi (not yet supported) */
	/* next data is device specific type (currently only pcdp_vga) */
} __attribute__((packed));

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
} __attribute__((packed));
