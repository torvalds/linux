/*
 * intel_baytrail.h: MailBox access support for Intel BayTrail platforms
 */

#ifndef INTEL_BAYTRAIL_MBI_SYMS_H
#define INTEL_BAYTRAIL_MBI_SYMS_H

#define BT_MBI_MCR_OFFSET	0xD0
#define BT_MBI_MDR_OFFSET	0xD4
#define BT_MBI_MCRX_OFFSET	0xD8

#define BT_MBI_RD_MASK		0xFEFFFFFF
#define BT_MBI_WR_MASK		0X01000000

#define BT_MBI_MASK_HI		0xFFFFFF00
#define BT_MBI_MASK_LO		0x000000FF
#define BT_MBI_ENABLE		0xF0

/* BT-SB unit access methods */
#define BT_MBI_UNIT_AUNIT	0x00
#define BT_MBI_UNIT_SMC		0x01
#define BT_MBI_UNIT_CPU		0x02
#define BT_MBI_UNIT_BUNIT	0x03
#define BT_MBI_UNIT_PMC		0x04
#define BT_MBI_UNIT_GFX		0x06
#define BT_MBI_UNIT_SMI		0x0C
#define BT_MBI_UNIT_USB		0x43
#define BT_MBI_UNIT_SATA	0xA3
#define BT_MBI_UNIT_PCIE	0xA6

/* Read/write opcodes */
#define BT_MBI_AUNIT_READ	0x10
#define BT_MBI_AUNIT_WRITE	0x11
#define BT_MBI_SMC_READ		0x10
#define BT_MBI_SMC_WRITE	0x11
#define BT_MBI_CPU_READ		0x10
#define BT_MBI_CPU_WRITE	0x11
#define BT_MBI_BUNIT_READ	0x10
#define BT_MBI_BUNIT_WRITE	0x11
#define BT_MBI_PMC_READ		0x06
#define BT_MBI_PMC_WRITE	0x07
#define BT_MBI_GFX_READ		0x00
#define BT_MBI_GFX_WRITE	0x01
#define BT_MBI_SMIO_READ	0x06
#define BT_MBI_SMIO_WRITE	0x07
#define BT_MBI_USB_READ		0x06
#define BT_MBI_USB_WRITE	0x07
#define BT_MBI_SATA_READ	0x00
#define BT_MBI_SATA_WRITE	0x01
#define BT_MBI_PCIE_READ	0x00
#define BT_MBI_PCIE_WRITE	0x01

/**
 * bt_mbi_read() - MailBox Interface read command
 * @port:	port indicating subunit being accessed
 * @opcode:	port specific read or write opcode
 * @offset:	register address offset
 * @mdr:	register data to be read
 *
 * Locking is handled by spinlock - cannot sleep.
 * Return: Nonzero on error
 */
int bt_mbi_read(u8 port, u8 opcode, u32 offset, u32 *mdr);

/**
 * bt_mbi_write() - MailBox unmasked write command
 * @port:	port indicating subunit being accessed
 * @opcode:	port specific read or write opcode
 * @offset:	register address offset
 * @mdr:	register data to be written
 *
 * Locking is handled by spinlock - cannot sleep.
 * Return: Nonzero on error
 */
int bt_mbi_write(u8 port, u8 opcode, u32 offset, u32 mdr);

/**
 * bt_mbi_modify() - MailBox masked write command
 * @port:	port indicating subunit being accessed
 * @opcode:	port specific read or write opcode
 * @offset:	register address offset
 * @mdr:	register data being modified
 * @mask:	mask indicating bits in mdr to be modified
 *
 * Locking is handled by spinlock - cannot sleep.
 * Return: Nonzero on error
 */
int bt_mbi_modify(u8 port, u8 opcode, u32 offset, u32 mdr, u32 mask);

#endif /* INTEL_BAYTRAIL_MBI_SYMS_H */
