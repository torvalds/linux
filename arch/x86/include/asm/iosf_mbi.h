/*
 * Intel OnChip System Fabric MailBox access support
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

/* IOSF SB read/write opcodes */
#define MBI_MMIO_READ		0x00
#define MBI_MMIO_WRITE		0x01
#define MBI_CFG_READ		0x04
#define MBI_CFG_WRITE		0x05
#define MBI_CR_READ		0x06
#define MBI_CR_WRITE		0x07
#define MBI_REG_READ		0x10
#define MBI_REG_WRITE		0x11
#define MBI_ESRAM_READ		0x12
#define MBI_ESRAM_WRITE		0x13

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

/* Quark available units */
#define QRK_MBI_UNIT_HBA	0x00
#define QRK_MBI_UNIT_HB		0x03
#define QRK_MBI_UNIT_RMU	0x04
#define QRK_MBI_UNIT_MM		0x05
#define QRK_MBI_UNIT_SOC	0x31

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

/**
 * iosf_mbi_punit_acquire() - Acquire access to the P-Unit
 *
 * One some systems the P-Unit accesses the PMIC to change various voltages
 * through the same bus as other kernel drivers use for e.g. battery monitoring.
 *
 * If a driver sends requests to the P-Unit which require the P-Unit to access
 * the PMIC bus while another driver is also accessing the PMIC bus various bad
 * things happen.
 *
 * To avoid these problems this function must be called before accessing the
 * P-Unit or the PMIC, be it through iosf_mbi* functions or through other means.
 *
 * Note on these systems the i2c-bus driver will request a sempahore from the
 * P-Unit for exclusive access to the PMIC bus when i2c drivers are accessing
 * it, but this does not appear to be sufficient, we still need to avoid making
 * certain P-Unit requests during the access window to avoid problems.
 *
 * This function locks a mutex, as such it may sleep.
 */
void iosf_mbi_punit_acquire(void);

/**
 * iosf_mbi_punit_release() - Release access to the P-Unit
 */
void iosf_mbi_punit_release(void);

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

static inline void iosf_mbi_punit_acquire(void) {}
static inline void iosf_mbi_punit_release(void) {}

#endif /* CONFIG_IOSF_MBI */

#endif /* IOSF_MBI_SYMS_H */
