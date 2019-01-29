// SPDX-License-Identifier: GPL-2.0
// LPC interface for ChromeOS Embedded Controller
//
// Copyright (C) 2016 Google, Inc

#include <linux/io.h>
#include <linux/mfd/cros_ec.h>
#include <linux/mfd/cros_ec_commands.h>

#include "cros_ec_lpc_mec.h"

static u8 lpc_read_bytes(unsigned int offset, unsigned int length, u8 *dest)
{
	int i;
	int sum = 0;

	for (i = 0; i < length; ++i) {
		dest[i] = inb(offset + i);
		sum += dest[i];
	}

	/* Return checksum of all bytes read */
	return sum;
}

static u8 lpc_write_bytes(unsigned int offset, unsigned int length, u8 *msg)
{
	int i;
	int sum = 0;

	for (i = 0; i < length; ++i) {
		outb(msg[i], offset + i);
		sum += msg[i];
	}

	/* Return checksum of all bytes written */
	return sum;
}

#ifdef CONFIG_CROS_EC_LPC_MEC

u8 cros_ec_lpc_read_bytes(unsigned int offset, unsigned int length, u8 *dest)
{
	if (length == 0)
		return 0;

	/* Access desired range through EMI interface */
	if (offset >= MEC_EMI_RANGE_START && offset <= MEC_EMI_RANGE_END) {
		/* Ensure we don't straddle EMI region */
		if (WARN_ON(offset + length - 1 > MEC_EMI_RANGE_END))
			return 0;

		return cros_ec_lpc_io_bytes_mec(MEC_IO_READ, offset, length,
						dest);
	}

	if (WARN_ON(offset + length > MEC_EMI_RANGE_START &&
		    offset < MEC_EMI_RANGE_START))
		return 0;

	return lpc_read_bytes(offset, length, dest);
}

u8 cros_ec_lpc_write_bytes(unsigned int offset, unsigned int length, u8 *msg)
{
	if (length == 0)
		return 0;

	/* Access desired range through EMI interface */
	if (offset >= MEC_EMI_RANGE_START && offset <= MEC_EMI_RANGE_END) {
		/* Ensure we don't straddle EMI region */
		if (WARN_ON(offset + length - 1 > MEC_EMI_RANGE_END))
			return 0;

		return cros_ec_lpc_io_bytes_mec(MEC_IO_WRITE, offset, length,
						msg);
	}

	if (WARN_ON(offset + length > MEC_EMI_RANGE_START &&
		    offset < MEC_EMI_RANGE_START))
		return 0;

	return lpc_write_bytes(offset, length, msg);
}

void cros_ec_lpc_reg_init(void)
{
	cros_ec_lpc_mec_init();
}

void cros_ec_lpc_reg_destroy(void)
{
	cros_ec_lpc_mec_destroy();
}

#else /* CONFIG_CROS_EC_LPC_MEC */

u8 cros_ec_lpc_read_bytes(unsigned int offset, unsigned int length, u8 *dest)
{
	return lpc_read_bytes(offset, length, dest);
}

u8 cros_ec_lpc_write_bytes(unsigned int offset, unsigned int length, u8 *msg)
{
	return lpc_write_bytes(offset, length, msg);
}

void cros_ec_lpc_reg_init(void)
{
}

void cros_ec_lpc_reg_destroy(void)
{
}

#endif /* CONFIG_CROS_EC_LPC_MEC */
