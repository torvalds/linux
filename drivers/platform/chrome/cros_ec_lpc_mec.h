/*
 * cros_ec_lpc_mec - LPC variant I/O for Microchip EC
 *
 * Copyright (C) 2016 Google, Inc
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * This driver uses the Chrome OS EC byte-level message-based protocol for
 * communicating the keyboard state (which keys are pressed) from a keyboard EC
 * to the AP over some bus (such as i2c, lpc, spi).  The EC does debouncing,
 * but everything else (including deghosting) is done here.  The main
 * motivation for this is to keep the EC firmware as simple as possible, since
 * it cannot be easily upgraded and EC flash/IRAM space is relatively
 * expensive.
 */

#ifndef __CROS_EC_LPC_MEC_H
#define __CROS_EC_LPC_MEC_H

#include <linux/mfd/cros_ec_commands.h>

enum cros_ec_lpc_mec_emi_access_mode {
	/* 8-bit access */
	ACCESS_TYPE_BYTE = 0x0,
	/* 16-bit access */
	ACCESS_TYPE_WORD = 0x1,
	/* 32-bit access */
	ACCESS_TYPE_LONG = 0x2,
	/*
	 * 32-bit access, read or write of MEC_EMI_EC_DATA_B3 causes the
	 * EC data register to be incremented.
	 */
	ACCESS_TYPE_LONG_AUTO_INCREMENT = 0x3,
};

enum cros_ec_lpc_mec_io_type {
	MEC_IO_READ,
	MEC_IO_WRITE,
};

/* Access IO ranges 0x800 thru 0x9ff using EMI interface instead of LPC */
#define MEC_EMI_RANGE_START EC_HOST_CMD_REGION0
#define MEC_EMI_RANGE_END   (EC_LPC_ADDR_MEMMAP + EC_MEMMAP_SIZE)

/* EMI registers are relative to base */
#define MEC_EMI_BASE 0x800
#define MEC_EMI_HOST_TO_EC (MEC_EMI_BASE + 0)
#define MEC_EMI_EC_TO_HOST (MEC_EMI_BASE + 1)
#define MEC_EMI_EC_ADDRESS_B0 (MEC_EMI_BASE + 2)
#define MEC_EMI_EC_ADDRESS_B1 (MEC_EMI_BASE + 3)
#define MEC_EMI_EC_DATA_B0 (MEC_EMI_BASE + 4)
#define MEC_EMI_EC_DATA_B1 (MEC_EMI_BASE + 5)
#define MEC_EMI_EC_DATA_B2 (MEC_EMI_BASE + 6)
#define MEC_EMI_EC_DATA_B3 (MEC_EMI_BASE + 7)

/*
 * cros_ec_lpc_mec_init
 *
 * Initialize MEC I/O.
 */
void cros_ec_lpc_mec_init(void);

/*
 * cros_ec_lpc_mec_destroy
 *
 * Cleanup MEC I/O.
 */
void cros_ec_lpc_mec_destroy(void);

/**
 * cros_ec_lpc_io_bytes_mec - Read / write bytes to MEC EMI port
 *
 * @io_type: MEC_IO_READ or MEC_IO_WRITE, depending on request
 * @offset:  Base read / write address
 * @length:  Number of bytes to read / write
 * @buf:     Destination / source buffer
 *
 * @return 8-bit checksum of all bytes read / written
 */
u8 cros_ec_lpc_io_bytes_mec(enum cros_ec_lpc_mec_io_type io_type,
			    unsigned int offset, unsigned int length, u8 *buf);

#endif /* __CROS_EC_LPC_MEC_H */
