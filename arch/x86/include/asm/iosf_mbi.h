/*
 * iosf_mbi.h: Intel OnChip System Fabric MailBox access support
 */

#ifndef IOSF_MBI_SYMS_H
#define IOSF_MBI_SYMS_H

#define MBI_MCR_OFFSET		0xD0
#define MBI_MDR_OFFSET		0xD4
#define MBI_MCRX_OFFSET		0xD8

#define MBI_RD_MASK		0xFEFFFFFF
#define MBI_WR_MASK		0X01000000

#define MBI_MASK_HI		0xFFFFFF00
#define MBI_MASK_LO		0x000000FF
#define MBI_ENABLE		0xF0

/* Baytrail available units */
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

/* Baytrail read/write opcodes */
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

/* Quark available units */
#define QRK_MBI_UNIT_HBA	0x00
#define QRK_MBI_UNIT_HB	0x03
#define QRK_MBI_UNIT_RMU	0x04
#define QRK_MBI_UNIT_MM	0x05
#define QRK_MBI_UNIT_MMESRAM	0x05
#define QRK_MBI_UNIT_SOC	0x31

/* Quark read/write opcodes */
#define QRK_MBI_HBA_READ	0x10
#define QRK_MBI_HBA_WRITE	0x11
#define QRK_MBI_HB_READ	0x10
#define QRK_MBI_HB_WRITE	0x11
#define QRK_MBI_RMU_READ	0x10
#define QRK_MBI_RMU_WRITE	0x11
#define QRK_MBI_MM_READ	0x10
#define QRK_MBI_MM_WRITE	0x11
#define QRK_MBI_MMESRAM_READ	0x12
#define QRK_MBI_MMESRAM_WRITE	0x13
#define QRK_MBI_SOC_READ	0x06
#define QRK_MBI_SOC_WRITE	0x07

#if IS_ENABLED(CONFIG_IOSF_MBI)

bool iosf_mbi_available(void);

/**
 * iosf_mbi_read() - MailBox Interface read command
 * @port:	port indicating subunit being accessed
 * @opcode:	port specific read or write opcode
 * @offset:	register address offset
 * @mdr:	register data to be read
 *
 * Locking is handled by spinlock - cannot sleep.
 * Return: Nonzero on error
 */
int iosf_mbi_read(u8 port, u8 opcode, u32 offset, u32 *mdr);

/**
 * iosf_mbi_write() - MailBox unmasked write command
 * @port:	port indicating subunit being accessed
 * @opcode:	port specific read or write opcode
 * @offset:	register address offset
 * @mdr:	register data to be written
 *
 * Locking is handled by spinlock - cannot sleep.
 * Return: Nonzero on error
 */
int iosf_mbi_write(u8 port, u8 opcode, u32 offset, u32 mdr);

/**
 * iosf_mbi_modify() - MailBox masked write command
 * @port:	port indicating subunit being accessed
 * @opcode:	port specific read or write opcode
 * @offset:	register address offset
 * @mdr:	register data being modified
 * @mask:	mask indicating bits in mdr to be modified
 *
 * Locking is handled by spinlock - cannot sleep.
 * Return: Nonzero on error
 */
int iosf_mbi_modify(u8 port, u8 opcode, u32 offset, u32 mdr, u32 mask);

#else /* CONFIG_IOSF_MBI is not enabled */
static inline
bool iosf_mbi_available(void)
{
	return false;
}

static inline
int iosf_mbi_read(u8 port, u8 opcode, u32 offset, u32 *mdr)
{
	WARN(1, "IOSF_MBI driver not available");
	return -EPERM;
}

static inline
int iosf_mbi_write(u8 port, u8 opcode, u32 offset, u32 mdr)
{
	WARN(1, "IOSF_MBI driver not available");
	return -EPERM;
}

static inline
int iosf_mbi_modify(u8 port, u8 opcode, u32 offset, u32 mdr, u32 mask)
{
	WARN(1, "IOSF_MBI driver not available");
	return -EPERM;
}
#endif /* CONFIG_IOSF_MBI */

#endif /* IOSF_MBI_SYMS_H */
