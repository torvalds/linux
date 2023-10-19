/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *	Copyright (c) 2001, 2003  Maciej W. Rozycki
 *
 *	DEC MS02-NV (54-20948-01) battery backed-up NVRAM module for
 *	DECstation/DECsystem 5000/2x0 and DECsystem 5900 and 5900/260
 *	systems.
 */

#include <linux/ioport.h>
#include <linux/mtd/mtd.h>

/*
 * Addresses are decoded as follows:
 *
 * 0x000000 - 0x3fffff	SRAM
 * 0x400000 - 0x7fffff	CSR
 *
 * Within the SRAM area the following ranges are forced by the system
 * firmware:
 *
 * 0x000000 - 0x0003ff	diagnostic area, destroyed upon a reboot
 * 0x000400 - ENDofRAM	storage area, available to operating systems
 *
 * but we can't really use the available area right from 0x000400 as
 * the first word is used by the firmware as a status flag passed
 * from an operating system.  If anything but the valid data magic
 * ID value is found, the firmware considers the SRAM clean, i.e.
 * containing no valid data, and disables the battery resulting in
 * data being erased as soon as power is switched off.  So the choice
 * for the start address of the user-available is 0x001000 which is
 * nicely page aligned.  The area between 0x000404 and 0x000fff may
 * be used by the driver for own needs.
 *
 * The diagnostic area defines two status words to be read by an
 * operating system, a magic ID to distinguish a MS02-NV board from
 * anything else and a status information providing results of tests
 * as well as the size of SRAM available, which can be 1MiB or 2MiB
 * (that's what the firmware handles; no idea if 2MiB modules ever
 * existed).
 *
 * The firmware only handles the MS02-NV board if installed in the
 * last (15th) slot, so for any other location the status information
 * stored in the SRAM cannot be relied upon.  But from the hardware
 * point of view there is no problem using up to 14 such boards in a
 * system -- only the 1st slot needs to be filled with a DRAM module.
 * The MS02-NV board is ECC-protected, like other MS02 memory boards.
 *
 * The state of the battery as provided by the CSR is reflected on
 * the two onboard LEDs.  When facing the battery side of the board,
 * with the LEDs at the top left and the battery at the bottom right
 * (i.e. looking from the back side of the system box), their meaning
 * is as follows (the system has to be powered on):
 *
 * left LED		battery disable status: lit = enabled
 * right LED		battery condition status: lit = OK
 */

/* MS02-NV iomem register offsets. */
#define MS02NV_CSR		0x400000	/* control & status register */

/* MS02-NV CSR status bits. */
#define MS02NV_CSR_BATT_OK	0x01		/* battery OK */
#define MS02NV_CSR_BATT_OFF	0x02		/* battery disabled */


/* MS02-NV memory offsets. */
#define MS02NV_DIAG		0x0003f8	/* diagnostic status */
#define MS02NV_MAGIC		0x0003fc	/* MS02-NV magic ID */
#define MS02NV_VALID		0x000400	/* valid data magic ID */
#define MS02NV_RAM		0x001000	/* user-exposed RAM start */

/* MS02-NV diagnostic status bits. */
#define MS02NV_DIAG_TEST	0x01		/* SRAM test done (?) */
#define MS02NV_DIAG_RO		0x02		/* SRAM r/o test done */
#define MS02NV_DIAG_RW		0x04		/* SRAM r/w test done */
#define MS02NV_DIAG_FAIL	0x08		/* SRAM test failed */
#define MS02NV_DIAG_SIZE_MASK	0xf0		/* SRAM size mask */
#define MS02NV_DIAG_SIZE_SHIFT	0x10		/* SRAM size shift (left) */

/* MS02-NV general constants. */
#define MS02NV_ID		0x03021966	/* MS02-NV magic ID value */
#define MS02NV_VALID_ID		0xbd100248	/* valid data magic ID value */
#define MS02NV_SLOT_SIZE	0x800000	/* size of the address space
						   decoded by the module */


typedef volatile u32 ms02nv_uint;

struct ms02nv_private {
	struct mtd_info *next;
	struct {
		struct resource *module;
		struct resource *diag_ram;
		struct resource *user_ram;
		struct resource *csr;
	} resource;
	u_char *addr;
	size_t size;
	u_char *uaddr;
};
