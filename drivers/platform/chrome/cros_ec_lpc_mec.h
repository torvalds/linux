/* SPDX-License-Identifier: GPL-2.0 */
/*
 * LPC variant I/O for Microchip EC
 *
 * Copyright (C) 2016 Google, Inc
 */

#ifndef __CROS_EC_LPC_MEC_H
#define __CROS_EC_LPC_MEC_H

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

/* EMI registers are relative to base */
#define MEC_EMI_HOST_TO_EC(MEC_EMI_BASE)	((MEC_EMI_BASE) + 0)
#define MEC_EMI_EC_TO_HOST(MEC_EMI_BASE)	((MEC_EMI_BASE) + 1)
#define MEC_EMI_EC_ADDRESS_B0(MEC_EMI_BASE)	((MEC_EMI_BASE) + 2)
#define MEC_EMI_EC_ADDRESS_B1(MEC_EMI_BASE)	((MEC_EMI_BASE) + 3)
#define MEC_EMI_EC_DATA_B0(MEC_EMI_BASE)	((MEC_EMI_BASE) + 4)
#define MEC_EMI_EC_DATA_B1(MEC_EMI_BASE)	((MEC_EMI_BASE) + 5)
#define MEC_EMI_EC_DATA_B2(MEC_EMI_BASE)	((MEC_EMI_BASE) + 6)
#define MEC_EMI_EC_DATA_B3(MEC_EMI_BASE)	((MEC_EMI_BASE) + 7)

/**
 * cros_ec_lpc_mec_init() - Initialize MEC I/O.
 *
 * @base: MEC EMI Base address
 * @end: MEC EMI End address
 */
void cros_ec_lpc_mec_init(unsigned int base, unsigned int end);

/**
 * cros_ec_lpc_mec_in_range() - Determine if addresses are in MEC EMI range.
 *
 * @offset: Address offset
 * @length: Number of bytes to check
 *
 * Return: 1 if in range, 0 if not, and -EINVAL on failure
 *         such as the mec range not being initialized
 */
int cros_ec_lpc_mec_in_range(unsigned int offset, unsigned int length);

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
