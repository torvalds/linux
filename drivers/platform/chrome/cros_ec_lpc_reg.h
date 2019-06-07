/* SPDX-License-Identifier: GPL-2.0 */
/*
 * LPC interface for ChromeOS Embedded Controller
 *
 * Copyright (C) 2016 Google, Inc
 */

#ifndef __CROS_EC_LPC_REG_H
#define __CROS_EC_LPC_REG_H

/**
 * cros_ec_lpc_read_bytes - Read bytes from a given LPC-mapped address.
 * Returns 8-bit checksum of all bytes read.
 *
 * @offset: Base read address
 * @length: Number of bytes to read
 * @dest: Destination buffer
 */
u8 cros_ec_lpc_read_bytes(unsigned int offset, unsigned int length, u8 *dest);

/**
 * cros_ec_lpc_write_bytes - Write bytes to a given LPC-mapped address.
 * Returns 8-bit checksum of all bytes written.
 *
 * @offset: Base write address
 * @length: Number of bytes to write
 * @msg: Write data buffer
 */
u8 cros_ec_lpc_write_bytes(unsigned int offset, unsigned int length, u8 *msg);

/**
 * cros_ec_lpc_reg_init
 *
 * Initialize register I/O.
 */
void cros_ec_lpc_reg_init(void);

/**
 * cros_ec_lpc_reg_destroy
 *
 * Cleanup reg I/O.
 */
void cros_ec_lpc_reg_destroy(void);

#endif /* __CROS_EC_LPC_REG_H */
