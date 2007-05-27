/*
 *  linux/drivers/mmc/core/sdio_io.c
 *
 *  Copyright 2007 Pierre Ossman
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#include <linux/mmc/host.h>
#include <linux/mmc/card.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/sdio_func.h>

#include "sdio_ops.h"

/**
 *	sdio_claim_host - exclusively claim a bus for a certain SDIO function
 *	@func: SDIO function that will be accessed
 *
 *	Claim a bus for a set of operations. The SDIO function given
 *	is used to figure out which bus is relevant.
 */
void sdio_claim_host(struct sdio_func *func)
{
	BUG_ON(!func);
	BUG_ON(!func->card);

	mmc_claim_host(func->card->host);
}
EXPORT_SYMBOL_GPL(sdio_claim_host);

/**
 *	sdio_release_host - release a bus for a certain SDIO function
 *	@func: SDIO function that was accessed
 *
 *	Release a bus, allowing others to claim the bus for their
 *	operations.
 */
void sdio_release_host(struct sdio_func *func)
{
	BUG_ON(!func);
	BUG_ON(!func->card);

	mmc_release_host(func->card->host);
}
EXPORT_SYMBOL_GPL(sdio_release_host);

/**
 *	sdio_enable_func - enables a SDIO function for usage
 *	@func: SDIO function to enable
 *
 *	Powers up and activates a SDIO function so that register
 *	access is possible.
 */
int sdio_enable_func(struct sdio_func *func)
{
	int ret;
	unsigned char reg;
	unsigned long timeout;

	BUG_ON(!func);
	BUG_ON(!func->card);

	pr_debug("SDIO: Enabling device %s...\n", sdio_func_id(func));

	ret = mmc_io_rw_direct(func->card, 0, 0, SDIO_CCCR_IOEx, 0, &reg);
	if (ret)
		goto err;

	reg |= 1 << func->num;

	ret = mmc_io_rw_direct(func->card, 1, 0, SDIO_CCCR_IOEx, reg, NULL);
	if (ret)
		goto err;

	/*
	 * FIXME: This should timeout based on information in the CIS,
	 * but we don't have card to parse that yet.
	 */
	timeout = jiffies + HZ;

	while (1) {
		ret = mmc_io_rw_direct(func->card, 0, 0, SDIO_CCCR_IORx, 0, &reg);
		if (ret)
			goto err;
		if (reg & (1 << func->num))
			break;
		ret = -ETIME;
		if (time_after(jiffies, timeout))
			goto err;
	}

	pr_debug("SDIO: Enabled device %s\n", sdio_func_id(func));

	return 0;

err:
	pr_debug("SDIO: Failed to enable device %s\n", sdio_func_id(func));
	return ret;
}
EXPORT_SYMBOL_GPL(sdio_enable_func);

/**
 *	sdio_disable_func - disable a SDIO function
 *	@func: SDIO function to disable
 *
 *	Powers down and deactivates a SDIO function. Register access
 *	to this function will fail until the function is reenabled.
 */
int sdio_disable_func(struct sdio_func *func)
{
	int ret;
	unsigned char reg;

	BUG_ON(!func);
	BUG_ON(!func->card);

	pr_debug("SDIO: Disabling device %s...\n", sdio_func_id(func));

	ret = mmc_io_rw_direct(func->card, 0, 0, SDIO_CCCR_IOEx, 0, &reg);
	if (ret)
		goto err;

	reg &= ~(1 << func->num);

	ret = mmc_io_rw_direct(func->card, 1, 0, SDIO_CCCR_IOEx, reg, NULL);
	if (ret)
		goto err;

	pr_debug("SDIO: Disabled device %s\n", sdio_func_id(func));

	return 0;

err:
	pr_debug("SDIO: Failed to disable device %s\n", sdio_func_id(func));
	return -EIO;
}
EXPORT_SYMBOL_GPL(sdio_disable_func);

/**
 *	sdio_readb - read a single byte from a SDIO function
 *	@func: SDIO function to access
 *	@addr: address to read
 *	@err_ret: optional status value from transfer
 *
 *	Reads a single byte from the address space of a given SDIO
 *	function. If there is a problem reading the address, 0xff
 *	is returned and @err_ret will contain the error code.
 */
unsigned char sdio_readb(struct sdio_func *func, unsigned int addr,
	int *err_ret)
{
	int ret;
	unsigned char val;

	BUG_ON(!func);

	if (err_ret)
		*err_ret = 0;

	ret = mmc_io_rw_direct(func->card, 0, func->num, addr, 0, &val);
	if (ret) {
		if (err_ret)
			*err_ret = ret;
		return 0xFF;
	}

	return val;
}
EXPORT_SYMBOL_GPL(sdio_readb);

/**
 *	sdio_writeb - write a single byte to a SDIO function
 *	@func: SDIO function to access
 *	@b: byte to write
 *	@addr: address to write to
 *	@err_ret: optional status value from transfer
 *
 *	Writes a single byte to the address space of a given SDIO
 *	function. @err_ret will contain the status of the actual
 *	transfer.
 */
void sdio_writeb(struct sdio_func *func, unsigned char b, unsigned int addr,
	int *err_ret)
{
	int ret;

	BUG_ON(!func);

	ret = mmc_io_rw_direct(func->card, 1, func->num, addr, b, NULL);
	if (err_ret)
		*err_ret = ret;
}
EXPORT_SYMBOL_GPL(sdio_writeb);

